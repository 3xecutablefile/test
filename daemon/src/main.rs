mod config;
mod logging;

// Windows-only modules
#[cfg(windows)]
mod console;
#[cfg(windows)]
mod device;
#[cfg(windows)]
mod iocp; // IOCP reactor
#[cfg(windows)]
mod service; // Windows Service wrapper
#[cfg(windows)]
mod vblk; // VBLK ring/dispatcher
#[cfg(windows)]
mod vblk_ring; // VBLK shared ring service // VTTY console bridge

use anyhow::Result;
#[cfg(windows)]
use std::time::{Duration, Instant};
#[cfg(windows)]
use vblk::Vblk;

#[cfg(windows)]
fn console_main(cfg_path: &str) -> Result<()> {
    logging::init();
    let cfg = config::load(cfg_path)?;
    let dev = device::Device::open()?;

    // Set up vblk ring/dispatcher and shared-ring service
    let mut vblk = Vblk::new(&dev, cfg.vblk_queue_depth as usize);

    // Configure vblk backing file (must exist)
    dev.vblk_set_backing_sync(&cfg.vblk_backing, Duration::from_secs(2))?;

    // Map shared pages
    let pages = (cfg.memory_mb as usize * 1024 * 1024 / 4096) as u32;
    let map = dev.map_shared_sync(pages, Duration::from_secs(2))?;
    tracing::info!(
        user_base = format!("0x{:x}", map.user_base).as_str(),
        size = map.size,
        "Mapped shared memory"
    );
    let vblk_ring = vblk_ring::VblkRing::new(&dev, map)?;

    // Start console bridge (stdin/stdout <-> vtty)
    let mut bridge = console::ConsoleBridge::new(&dev);
    bridge.start()?;

    // Main loop: ticks + (placeholder) vblk pump
    let mut last_pump = Instant::now();
    loop {
        // honor service stop if running as service
        if service::STOP_FLAG.load(std::sync::atomic::Ordering::SeqCst) {
            break;
        }

        dev.run_tick_sync(cfg.tick_budget, Duration::from_millis(250))?;

        if last_pump.elapsed() >= Duration::from_millis(5) {
            // ring-backed vblk from guest
            let _ = vblk_ring.pump();
            // ioctls-backed vblk submissions
            vblk.drain_completions();
            last_pump = Instant::now();
        }

        // cooperative yield
        std::thread::yield_now();
    }
    // Stop console bridge before exiting
    bridge.stop();
    Ok(())
}

#[cfg(windows)]
fn maybe_handle_cli() -> Option<anyhow::Result<()>> {
    if std::env::args().any(|a| a == "--install") {
        let bin = std::env::current_exe()
            .unwrap()
            .to_string_lossy()
            .to_string();
        return Some(service::install_service(&bin).map(|_| {
            println!("Installed service coLinux2");
        }));
    }
    if std::env::args().any(|a| a == "--uninstall") {
        return Some(service::uninstall_service().map(|_| {
            println!("Uninstalled service coLinux2");
        }));
    }
    None
}

#[cfg(windows)]
fn main() -> Result<()> {
    if let Some(res) = maybe_handle_cli() {
        return res;
    }
    let cfg_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "config/colinux.yaml".into());
    let cfg_owned = cfg_path.clone();
    if service::maybe_run_as_service(move || console_main(&cfg_owned))? {
        return Ok(());
    }
    console_main(&cfg_path)
}

#[cfg(not(windows))]
fn main() -> Result<()> {
    logging::init();
    eprintln!("coLinux 2.0 daemon runs on Windows only (x86_64).");
    eprintln!("Build and run on Windows 10/11 with the WDK installed.");
    Ok(())
}
