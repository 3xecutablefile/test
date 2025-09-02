use crate::device::{Device, MapInfo};
use anyhow::{bail, Result};
use std::mem::size_of;
use std::ptr::NonNull;

const VBLK_RING_OFF: usize = 0x1000;
const VBLK_DATA_OFF: usize = 0x4000;
const VBLK_SLOT_DATA_STRIDE: usize = 128 * 1024;
// Prototype maximum data window: matches mem.c default cap (8 slots)
const VBLK_DATA_MAX: usize = VBLK_SLOT_DATA_STRIDE * 8;

const OP_READ: u8 = 0;
const OP_WRITE: u8 = 1;

const ST_OK: u8 = 0;
const ST_EINVAL: u8 = 1;
const ST_EIO: u8 = 5;

#[repr(C)]
struct RingCtrl {
    prod: u32,
    cons: u32,
    cap: u32,
    slot_size: u32,
}

#[repr(C)]
struct VblkSlot {
    id: u64,
    op: u8,
    status: u8,
    _pad: u16,
    lba: u64,
    len: u32,
    data_off: u32,
}

pub struct VblkRing<'a> {
    dev: &'a Device,
    base: NonNull<u8>,
    size: usize,
}

impl<'a> VblkRing<'a> {
    pub fn new(dev: &'a Device, map: MapInfo) -> Result<Self> {
        if map.size as usize <= VBLK_DATA_OFF + VBLK_DATA_MAX {
            bail!("shared map too small for vblk ring");
        }
        let base = NonNull::new(map.user_base as *mut u8).ok_or_else(|| anyhow::anyhow!("null map base"))?;
        Ok(Self { dev, base, size: map.size as usize })
    }

    unsafe fn ptr<T>(&self, off: usize) -> *mut T {
        self.base.as_ptr().add(off) as *mut T
    }

    pub fn pump(&self) -> Result<()> {
        unsafe {
            let ctrl = &mut *self.ptr::<RingCtrl>(VBLK_RING_OFF);
            let cap = ctrl.cap as usize;
            let slots_base = self.ptr::<u8>(VBLK_RING_OFF + size_of::<RingCtrl>()) as *mut VblkSlot;
            while ctrl.prod != ctrl.cons {
                let idx = (ctrl.cons % (cap as u32)) as usize;
                let slot = &mut *slots_base.add(idx);
                // validate
                let len = slot.len as usize;
                let data_off = slot.data_off as usize;
                if len == 0 || (len & 511) != 0 || len > VBLK_SLOT_DATA_STRIDE || data_off + len > (VBLK_SLOT_DATA_STRIDE * cap) {
                    slot.status = ST_EINVAL;
                    ctrl.cons = ctrl.cons.wrapping_add(1);
                    continue;
                }
                let lba = slot.lba;
                let data_ptr = self.ptr::<u8>(VBLK_DATA_OFF + data_off);
                let data = core::slice::from_raw_parts_mut(data_ptr, len);
                // service
                let res = match slot.op {
                    OP_READ => self.dev.vblk_read_sync(lba, slot.len, std::time::Duration::from_secs(2)).map(|out| {
                        let n = out.len().min(len);
                        data[..n].copy_from_slice(&out[..n]);
                    }),
                    OP_WRITE => self.dev.vblk_write_sync(lba, data, std::time::Duration::from_secs(2)).map(|_| {}),
                    _ => { slot.status = ST_EINVAL; ctrl.cons = ctrl.cons.wrapping_add(1); continue; }
                };
                slot.status = if res.is_ok() { ST_OK } else { ST_EIO };
                ctrl.cons = ctrl.cons.wrapping_add(1);
            }
        }
        Ok(())
    }
}
