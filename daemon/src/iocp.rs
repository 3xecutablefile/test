//! Minimal IOCP + overlapped DeviceIoControl reactor for coLinux 2.0.
//! Safe(ish) wrapper: all unsafe is concentrated and documented.

use anyhow::{anyhow, bail, Context, Result};
use crossbeam_channel::{Receiver, RecvTimeoutError, Sender};
use std::ptr::{addr_of_mut, null_mut};
use std::thread;
use std::time::Duration;
use windows::core::PCWSTR;
use windows::Win32::Foundation::{CloseHandle, GetLastError, BOOL, HANDLE};
use windows::Win32::Storage::FileSystem::{
    CreateFileW, FILE_ATTRIBUTE_NORMAL, FILE_FLAG_OVERLAPPED, FILE_FLAGS_AND_ATTRIBUTES,
    FILE_SHARE_READ, FILE_SHARE_WRITE, GENERIC_READ, GENERIC_WRITE, OPEN_EXISTING,
};
use windows::Win32::System::IO::{
    CreateIoCompletionPort, DeviceIoControl, GetQueuedCompletionStatus, OVERLAPPED,
};

const COMPLETION_KEY_IOCTL: usize = 1;

pub struct IoctlRequest {
    pub code: u32,
    pub inbuf: Option<Vec<u8>>,      // parameters (METHOD_* params)
    pub out_capacity: usize,         // size of out/other buffer (MDL target for direct I/O, or read buffer)
    pub prefill_out: Option<Vec<u8>>,// if provided, initializes out buffer (e.g., write payload for METHOD_IN_DIRECT)
    pub reply: Sender<Result<Vec<u8>>>,
}

pub struct Reactor {
    iocp: HANDLE,
    dev: HANDLE,
    tx_submit: Sender<IoctlRequest>,
    worker: thread::JoinHandle<()>,
}

unsafe impl Send for Reactor {}
unsafe impl Sync for Reactor {}

impl Reactor {
    /// Open \\.\u{005c}coLinux with FILE_FLAG_OVERLAPPED and bind to a new IOCP.
    pub fn open_dev(path: &str) -> Result<Self> {
        let wide: Vec<u16> = path.encode_utf16().chain([0]).collect();
        let dev = unsafe {
            CreateFileW(
                PCWSTR::from_raw(wide.as_ptr()),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                None,
                OPEN_EXISTING,
                FILE_FLAGS_AND_ATTRIBUTES(FILE_ATTRIBUTE_NORMAL.0 | FILE_FLAG_OVERLAPPED.0),
                None,
            )
        };
        if dev.is_invalid() {
            bail!("CreateFileW({}) failed (last={:?})", path, unsafe { GetLastError() });
        }

        let iocp = unsafe { CreateIoCompletionPort(dev, HANDLE(0), COMPLETION_KEY_IOCTL, 0) };
        if iocp.0 == 0 {
            unsafe { CloseHandle(dev) };
            bail!("CreateIoCompletionPort failed (last={:?})", unsafe { GetLastError() });
        }

        let (tx, rx) = crossbeam_channel::unbounded::<IoctlRequest>();
        let worker = spawn_worker(dev, iocp, rx);

        Ok(Self {
            iocp,
            dev,
            tx_submit: tx,
            worker,
        })
    }

    pub fn submit(&self, req: IoctlRequest) -> Result<()> {
        self.tx_submit
            .send(req)
            .map_err(|e| anyhow!("submit failed: {e}"))
    }
}

impl Drop for Reactor {
    fn drop(&mut self) {
        // Best-effort cleanup. IOCP closes when handle drops; worker exits on channel close.
        unsafe {
            let _ = CloseHandle(self.iocp);
            let _ = CloseHandle(self.dev);
        }
        // Allow worker to exit when tx channel drops.
    }
}

struct OverlappedBox {
    /// Windows OVERLAPPED structure (must be first).
    ov: OVERLAPPED,
    inbuf: Vec<u8>,
    out: Vec<u8>,
    reply: Sender<Result<Vec<u8>>>,
}
impl OverlappedBox {
    fn new(inbuf: Option<Vec<u8>>, out_capacity: usize, prefill_out: Option<Vec<u8>>, reply: Sender<Result<Vec<u8>>>) -> Box<Self> {
        let mut out = vec![0u8; out_capacity];
        if let Some(mut p) = prefill_out {
            if p.len() != out_capacity { p.resize(out_capacity, 0); }
            out.copy_from_slice(&p);
        }
        Box::new(Self { ov: OVERLAPPED::default(), inbuf: inbuf.unwrap_or_default(), out, reply })
    }
}

/// Worker thread: pumps two loops
/// 1) posts new IOCTLs received over `rx`
/// 2) collects completions from IOCP using GetQueuedCompletionStatus
fn spawn_worker(dev: HANDLE, iocp: HANDLE, rx: Receiver<IoctlRequest>) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        loop {
            // 1) Post any queued IOCTLs (non-blocking check)
            match rx.recv_timeout(Duration::from_millis(1)) {
                Ok(req) => unsafe {
                    // Box our overlapped + buffers + channel
                    let mut obox = OverlappedBox::new(req.inbuf, req.out_capacity, req.prefill_out, req.reply);
                    let p_ov: *mut OVERLAPPED = addr_of_mut!(obox.ov);

                    // Prepare in/out pointers
                    let (in_ptr, in_len) = if obox.inbuf.is_empty() {
                        (None, 0u32)
                    } else {
                        (Some(obox.inbuf.as_ptr() as _), obox.inbuf.len() as u32)
                    };

                    let mut bytes_ret: u32 = 0;
                    let ok: BOOL = DeviceIoControl(
                        dev,
                        req.code,
                        in_ptr,
                        in_len,
                        if obox.out.is_empty() {
                            None
                        } else {
                            Some(obox.out.as_mut_ptr() as _)
                        },
                        obox.out.len() as u32,
                        &mut bytes_ret,
                        Some(p_ov),
                    );
                    if ok.as_bool() {
                        // Completed immediately; emulate completion via channel
                        let mut out = obox.out;
                        out.truncate(bytes_ret as usize);
                        let _ = obox.reply.send(Ok(out));
                        // Box drops here
                    } else {
                        // EXPECT: ERROR_IO_PENDING → completion will arrive via IOCP
                        let err = GetLastError().0;
                        const ERROR_IO_PENDING: u32 = 997;
                        if err != ERROR_IO_PENDING {
                            let _ = obox
                                .reply
                                .send(Err(anyhow!("DeviceIoControl failed: {err}")));
                            // drop box; no completion will arrive
                        } else {
                            // leak Box until completion; IOCP will give back the pointer
                            Box::into_raw(obox);
                        }
                    }
                },
                Err(RecvTimeoutError::Timeout) => { /* fall through to completions */ }
                Err(RecvTimeoutError::Disconnected) => break,
            }

            // 2) Collect at most one completion per tick to stay responsive
            unsafe {
                let mut bytes: u32 = 0;
                let mut key: usize = 0;
                let mut lpov: *mut OVERLAPPED = null_mut();
                let ok = GetQueuedCompletionStatus(iocp, &mut bytes, &mut key, &mut lpov, 1);

                if !lpov.is_null() && key == COMPLETION_KEY_IOCTL {
                    // Recover our OverlappedBox
                    let obox_ptr = lpov as *mut OverlappedBox;
                    let mut obox: Box<OverlappedBox> = Box::from_raw(obox_ptr);
                    if ok.as_bool() {
                        obox.out.truncate(bytes as usize);
                        let _ = obox.reply.send(Ok(obox.out));
                    } else {
                        let err = GetLastError().0;
                        let _ = obox.reply.send(Err(anyhow!("ioctl completion failed: {err}")));
                    }
                    // Box drops here
                }
            }
        }
    })
}
