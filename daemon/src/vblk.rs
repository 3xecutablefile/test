#![cfg(windows)]

//! VBLK ring + dispatcher over IOCP-backed DeviceIoControl.
//! This is a queueing layer that enforces queue_depth and tracks completions.

use crossbeam_channel::{Receiver, TryRecvError};
use std::collections::{HashMap, VecDeque};
use uuid::Uuid;

use anyhow::Result;

use crate::device::Device;

#[derive(Clone, Copy, Debug)]
pub enum Op {
    Read,
    Write,
}

#[derive(Debug)]
pub struct VblkReq {
    pub op: Op,
    pub lba: u64,
    pub len: u32,
    pub buf: Vec<u8>, // for Write: payload; for Read: capacity (len) will be used
}

// Wire format to driver: [op:1][reserved:3][lba:8][len:4][data…]
fn encode_req(req: &VblkReq) -> Vec<u8> {
    let mut out = Vec::with_capacity(1 + 3 + 8 + 4 + req.buf.len());
    out.push(match req.op {
        Op::Read => 0u8,
        Op::Write => 1u8,
    });
    out.extend_from_slice(&[0, 0, 0]);
    out.extend_from_slice(&req.lba.to_le_bytes());
    out.extend_from_slice(&req.len.to_le_bytes());
    out.extend_from_slice(&req.buf);
    out
}

pub struct Inflight {
    pub id: Uuid,
    pub rx: Receiver<Result<Vec<u8>>>,
    pub op: Op,
    pub lba: u64,
    pub len: u32,
}

pub struct Vblk<'a> {
    dev: &'a Device,
    depth: usize,
    pending: VecDeque<(Uuid, VblkReq)>,
    inflight: HashMap<Uuid, Inflight>,
}

impl<'a> Vblk<'a> {
    pub fn new(dev: &'a Device, depth: usize) -> Self {
        Self {
            dev,
            depth,
            pending: VecDeque::new(),
            inflight: HashMap::new(),
        }
    }

    pub fn submit(&mut self, req: VblkReq) {
        let id = Uuid::new_v4();
        self.pending.push_back((id, req));
        self.kick();
    }

    fn kick(&mut self) {
        while self.inflight.len() < self.depth {
            if let Some((id, req)) = self.pending.pop_front() {
                let rx = match req.op {
                    Op::Read => {
                        let (tx, ch) = crossbeam_channel::bounded(1);
                        // Submit via direct read IOCTL using Device wrapper, but adapt to existing async pattern by spawning a lightweight task thread.
                        let dev = self.dev;
                        let lba = req.lba;
                        let len = req.len;
                        std::thread::spawn(move || {
                            let res =
                                dev.vblk_read_sync(lba, len, std::time::Duration::from_secs(2));
                            let _ = tx.send(res);
                        });
                        ch
                    }
                    Op::Write => {
                        let (tx, ch) = crossbeam_channel::bounded(1);
                        let dev = self.dev;
                        let lba = req.lba;
                        let buf = req.buf.clone();
                        std::thread::spawn(move || {
                            let res = dev
                                .vblk_write_sync(lba, &buf, std::time::Duration::from_secs(2))
                                .map(|_| Vec::new());
                            let _ = tx.send(res);
                        });
                        ch
                    }
                };
                self.inflight.insert(
                    id,
                    Inflight {
                        id,
                        rx,
                        op: req.op,
                        lba: req.lba,
                        len: req.len,
                    },
                );
            } else {
                break;
            }
        }
    }

    pub fn drain_completions(&mut self) {
        // try each inflight; finished ones are collected
        let mut done_ids = Vec::new();
        for (id, infl) in self.inflight.iter() {
            match infl.rx.try_recv() {
                Ok(Ok(buf)) => {
                    // TODO: hand off to upper layers (e.g., filesystem, boot logic)
                    if matches!(infl.op, Op::Read) && buf.len() != infl.len as usize {
                        tracing::warn!(
                            "vblk short read at LBA {}: got {}, expected {}",
                            infl.lba,
                            buf.len(),
                            infl.len
                        );
                    }
                    done_ids.push(*id);
                }
                Ok(Err(e)) => {
                    tracing::error!("vblk error at LBA {}: {:?}", infl.lba, e);
                    done_ids.push(*id);
                }
                Err(TryRecvError::Empty) => { /* not ready */ }
                Err(TryRecvError::Disconnected) => {
                    tracing::error!("vblk channel closed for LBA {}", infl.lba);
                    done_ids.push(*id);
                }
            }
        }
        for id in done_ids {
            self.inflight.remove(&id);
        }
        // backfill queue
        self.kick();
    }
}
