use crate::device::Device;
use anyhow::Result;
use std::io::{Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::Duration;

pub struct ConsoleBridge<'a> {
    dev: &'a Device,
    stop: Arc<AtomicBool>,
    in_thr: Option<JoinHandle<()>>,
    out_thr: Option<JoinHandle<()>>,
}

impl<'a> ConsoleBridge<'a> {
    pub fn new(dev: &'a Device) -> Self {
        Self { dev, stop: Arc::new(AtomicBool::new(false)), in_thr: None, out_thr: None }
    }

    pub fn start(&mut self) -> Result<()> {
        let stop_in = self.stop.clone();
        let dev_in = unsafe { std::mem::transmute::<&Device, &'static Device>(self.dev) };
        let in_thr = thread::spawn(move || {
            let mut stdin = std::io::stdin();
            let mut buf = [0u8; 4096];
            while !stop_in.load(Ordering::Relaxed) {
                match stdin.read(&mut buf) {
                    Ok(0) => thread::sleep(Duration::from_millis(5)),
                    Ok(n) => {
                        let mut off = 0;
                        while off < n {
                            match dev_in.vtty_push(&buf[off..n], Duration::from_millis(200)) {
                                Ok(w) if w > 0 => off += w,
                                Ok(_) => thread::sleep(Duration::from_millis(2)),
                                Err(_) => thread::sleep(Duration::from_millis(10)),
                            }
                            if stop_in.load(Ordering::Relaxed) { break; }
                        }
                    }
                    Err(_) => thread::sleep(Duration::from_millis(10)),
                }
            }
        });

        let stop_out = self.stop.clone();
        let dev_out = unsafe { std::mem::transmute::<&Device, &'static Device>(self.dev) };
        let out_thr = thread::spawn(move || {
            let mut stdout = std::io::stdout();
            while !stop_out.load(Ordering::Relaxed) {
                match dev_out.vtty_pull(4096, Duration::from_millis(50)) {
                    Ok(data) if !data.is_empty() => {
                        let _ = stdout.write_all(&data);
                        let _ = stdout.flush();
                    }
                    _ => {
                        // nothing available
                    }
                }
            }
        });

        self.in_thr = Some(in_thr);
        self.out_thr = Some(out_thr);
        Ok(())
    }

    pub fn stop(mut self) {
        self.stop.store(true, Ordering::Relaxed);
        if let Some(h) = self.in_thr.take() { let _ = h.join(); }
        if let Some(h) = self.out_thr.take() { let _ = h.join(); }
    }
}

