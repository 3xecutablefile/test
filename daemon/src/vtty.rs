#![cfg(windows)]

use anyhow::Result;
use crossbeam_channel::{bounded, Receiver};
use std::time::Duration;

use crate::iocp::{IoctlRequest, Reactor};

const IOCTL_COLINUX_VTTY_PUSH: u32 = 0x0022201C; // 0x22<<16 | 0x807<<2
const IOCTL_COLINUX_VTTY_PULL: u32 = 0x00222020; // 0x22<<16 | 0x808<<2

pub struct Vtty {
    reactor: Reactor,
}

impl Vtty {
    pub fn new(reactor: Reactor) -> Self { Self { reactor } }

    pub fn push(&self, data: &[u8]) -> Result<usize> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VTTY_PUSH,
            inbuf: Some(data.to_vec()),
            out_capacity: 0,
            prefill_out: None,
            reply: tx,
        })?;
        let out = rx.recv_timeout(Duration::from_secs(2)).map_err(|_| anyhow::anyhow!("vtty push timeout"))??;
        Ok(out.len())
    }

    pub fn pull(&self, capacity: usize, timeout: Duration) -> Result<Vec<u8>> {
        let (tx, rx) = bounded(1);
        self.reactor.submit(IoctlRequest {
            code: IOCTL_COLINUX_VTTY_PULL,
            inbuf: None,
            out_capacity: capacity,
            prefill_out: None,
            reply: tx,
        })?;
        let out = rx.recv_timeout(timeout).map_err(|_| anyhow::anyhow!("vtty pull timeout"))??;
        Ok(out)
    }
}
