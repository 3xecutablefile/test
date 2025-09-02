use anyhow::{Result, Context};
use crossbeam_channel::{bounded, Receiver};
use std::time::Duration;

use crate::iocp::{IoctlRequest, Reactor};

// These constants must match the kernel driver's IOCTL codes (METHOD_BUFFERED)
// CTL_CODE(FILE_DEVICE_UNKNOWN(0x22), 0x801.., METHOD_BUFFERED, FILE_ANY_ACCESS)
const IOCTL_COLINUX_MAP_SHARED: u32 = 0x00222004; // 0x22 <<16 | 0x801<<2
const IOCTL_COLINUX_RUN_TICK: u32 = 0x00222008;  // 0x22 <<16 | 0x802<<2
const IOCTL_COLINUX_VBLK_SUBMIT: u32 = 0x0022200C; // 0x22 <<16 | 0x803<<2
const IOCTL_COLINUX_VBLK_SET_BACKING: u32 = 0x00222010; // 0x22 <<16 | 0x804<<2
// Direct I/O variants (match ctl codes from driver_c/include/colinux_ioctls.h)
// CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
// CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_IN_DIRECT,  FILE_ANY_ACCESS)
const IOCTL_COLINUX_VBLK_READ: u32  = 0x00222016; // 0x22<<16 | 0x805<<2 | 2
const IOCTL_COLINUX_VBLK_WRITE: u32 = 0x00222019; // 0x22<<16 | 0x806<<2 | 1
const IOCTL_COLINUX_VTTY_PUSH: u32 = 0x0022201C; // 0x22<<16 | 0x807<<2 (METHOD_BUFFERED)
const IOCTL_COLINUX_VTTY_PULL: u32 = 0x00222020; // 0x22<<16 | 0x808<<2 (METHOD_BUFFERED)

pub struct Device {
    reactor: Reactor,
}

impl Device {
    pub fn open() -> Result<Self> {
        let reactor = Reactor::open_dev(r"\\.\coLinux")?;
        Ok(Self { reactor })
    }

#[derive(Debug, Clone, Copy)]
pub struct MapInfo { pub user_base: usize, pub kernel_base: u64, pub size: u64, pub ver: u32, pub flags: u32 }

    /// Map N pages of shared memory (synchronous helper with timeout). Returns mapping descriptor.
    pub fn map_shared_sync(&self, pages: u32, timeout: Duration) -> Result<MapInfo> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_MAP_SHARED,
            inbuf: Some(pages.to_le_bytes().to_vec()),
            out_capacity: 32, // MAP_INFO_OUT size
            prefill_out: None,
            reply: tx,
        })?;
        let out = recv_with_timeout(rx, timeout)??;
        if out.len() < 32 { anyhow::bail!("map_shared: short reply {}", out.len()); }
        let user_base = usize::from_le_bytes(out[0..8].try_into().unwrap());
        let kernel_base = u64::from_le_bytes(out[8..16].try_into().unwrap());
        let size = u64::from_le_bytes(out[16..24].try_into().unwrap());
        let ver = u32::from_le_bytes(out[24..28].try_into().unwrap());
        let flags = u32::from_le_bytes(out[28..32].try_into().unwrap());
        Ok(MapInfo { user_base, kernel_base, size, ver, flags })
    }

    /// Run one scheduler tick with a budget (synchronous).
    pub fn run_tick_sync(&self, budget: u32, timeout: Duration) -> Result<()> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_RUN_TICK,
            inbuf: Some(budget.to_le_bytes().to_vec()),
            out_capacity: 0,
            reply: tx,
        })?;
        let _ = recv_with_timeout(rx, timeout)??;
        Ok(())
    }

    /// Submit a virtual block I/O (async): returns a receiver you can await.
    pub fn vblk_submit_async(&self, req: &[u8], out_capacity: usize) -> Result<Receiver<Result<Vec<u8>>>> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VBLK_SUBMIT,
            inbuf: Some(req.to_vec()),
            out_capacity,
            prefill_out: None,
            reply: tx,
        })?;
        Ok(rx)
    }

    /// Configure vblk backing path (UTF-16). Path is Win32 style (e.g., C:\path\file.img).
    pub fn vblk_set_backing_sync(&self, path: &str, timeout: Duration) -> Result<()> {
        // Encode as UTF-16 without explicit null terminator; driver will treat buffer length accordingly
        let wide: Vec<u16> = path.encode_utf16().collect();
        let bytes: Vec<u8> = wide.iter().flat_map(|w| w.to_le_bytes()).collect();
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VBLK_SET_BACKING,
            inbuf: Some(bytes),
            out_capacity: 0,
            prefill_out: None,
            reply: tx,
        })?;
        let _ = recv_with_timeout(rx, timeout)??;
        Ok(())
    }

    pub fn vblk_read_sync(&self, lba_sectors: u64, len: u32, timeout: Duration) -> Result<Vec<u8>> {
        let mut hdr = Vec::with_capacity(16);
        hdr.extend_from_slice(&lba_sectors.to_le_bytes());
        hdr.extend_from_slice(&len.to_le_bytes());
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VBLK_READ,
            inbuf: Some(hdr),
            out_capacity: len as usize,
            prefill_out: None,
            reply: tx,
        })?;
        let out = recv_with_timeout(rx, timeout)??;
        Ok(out)
    }

    pub fn vblk_write_sync(&self, lba_sectors: u64, payload: &[u8], timeout: Duration) -> Result<()> {
        let len = payload.len() as u32;
        let mut hdr = Vec::with_capacity(16);
        hdr.extend_from_slice(&lba_sectors.to_le_bytes());
        hdr.extend_from_slice(&len.to_le_bytes());
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VBLK_WRITE,
            inbuf: Some(hdr),
            out_capacity: payload.len(),
            prefill_out: Some(payload.to_vec()),
            reply: tx,
        })?;
        let _ = recv_with_timeout(rx, timeout)??;
        Ok(())
    }

    pub fn vtty_push(&self, data: &[u8], timeout: Duration) -> Result<usize> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VTTY_PUSH,
            inbuf: Some(data.to_vec()),
            out_capacity: 0,
            prefill_out: None,
            reply: tx,
        })?;
        let out = recv_with_timeout(rx, timeout)??;
        Ok(out.len())
    }

    pub fn vtty_pull(&self, capacity: usize, timeout: Duration) -> Result<Vec<u8>> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VTTY_PULL,
            inbuf: None,
            out_capacity: capacity,
            prefill_out: None,
            reply: tx,
        })?;
        let out = recv_with_timeout(rx, timeout)??;
        Ok(out)
    }
}

fn recv_with_timeout<T>(rx: Receiver<T>, timeout: Duration) -> Result<T> {
    rx.recv_timeout(timeout)
        .map_err(|_| anyhow::anyhow!("ioctl timeout"))
}
