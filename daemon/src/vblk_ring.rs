use crate::device::{Device, MapInfo};
use anyhow::{bail, Result};
use std::mem::{size_of};
use std::ptr::NonNull;

const VBLK_RING_OFF: usize = 0x1000;
const VBLK_DATA_OFF: usize = 0x2000;
const VBLK_DATA_MAX: usize = 128 * 1024;

const OP_READ: u8 = 0;
const OP_WRITE: u8 = 1;

#[repr(C)]
struct RingIdx { prod: u32, cons: u32 }

#[repr(C)]
struct VblkReq {
    id: u64,
    op: u8,
    _pad: [u8;7],
    lba: u64,
    len: u32,
    status: u32,
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
            let idx = &mut *self.ptr::<RingIdx>(VBLK_RING_OFF);
            let req = &mut *self.ptr::<VblkReq>(VBLK_RING_OFF + size_of::<RingIdx>());
            while idx.prod != idx.cons {
                let len = req.len as usize;
                if len == 0 || len > VBLK_DATA_MAX || (len & 511) != 0 { req.status = 2; idx.cons = idx.cons.wrapping_add(1); continue; }
                let lba = req.lba;
                let data = core::slice::from_raw_parts_mut(self.ptr::<u8>(VBLK_DATA_OFF), len);
                match req.op {
                    OP_READ => {
                        let out = self.dev.vblk_read_sync(lba, req.len, std::time::Duration::from_secs(2))?;
                        data[..out.len()].copy_from_slice(&out);
                        req.status = 1;
                    }
                    OP_WRITE => {
                        self.dev.vblk_write_sync(lba, data, std::time::Duration::from_secs(2))?;
                        req.status = 1;
                    }
                    _ => req.status = 3,
                }
                idx.cons = idx.cons.wrapping_add(1);
            }
        }
        Ok(())
    }
}

