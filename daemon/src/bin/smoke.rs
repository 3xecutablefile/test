use anyhow::Result;
use colinux_daemon::device::Device; // if crate name differs, adjust use path

fn main() -> Result<()> {
    // Accept optional cfg path
    let cfg_path = std::env::args().nth(1).unwrap_or_else(|| "config/colinux.yaml".into());

    // Use the library modules directly
    runner(&cfg_path)
}

fn runner(cfg_path: &str) -> Result<()> {
    let cfg = colinux_daemon::config::load(cfg_path)?;
    let dev = Device::open()?;

    // Backing file
    dev.vblk_set_backing_sync(&cfg.vblk_backing, std::time::Duration::from_secs(2))?;

    // Map shared
    let pages = (cfg.memory_mb as usize * 1024 * 1024 / 4096) as u32;
    let map = dev.map_shared_sync(pages, std::time::Duration::from_secs(2))?;
    println!("mapped user_base=0x{:x} size={} ver={} flags={}",
        map.user_base, map.size, map.ver, map.flags);

    // Ping: write ping_req++, run ticks until ping_resp matches
    unsafe {
        #[repr(C)]
        struct RingHeader { ver:u32, flags:u32, tick_count:u64, ping_req:u32, ping_resp:u32 }
        let hdr = map.user_base as *mut RingHeader;
        if !hdr.is_null() {
            let seq = (*hdr).ping_req.wrapping_add(1);
            (*hdr).ping_req = seq;
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(1);
            let mut ok = false;
            while std::time::Instant::now() < deadline {
                dev.run_tick_sync(cfg.tick_budget, std::time::Duration::from_millis(100))?;
                if (*hdr).ping_resp == seq { ok = true; break; }
            }
            println!("ping {}", if ok {"ok"} else {"timeout"});
        }
    }

    // VBLK write then read back 4KiB at LBA 0x100 (sector units)
    let lba = 0x100u64;
    let len = 4096u32; // multiple of 512
    let mut data = vec![0u8; len as usize];
    for (i, b) in data.iter_mut().enumerate() { *b = (i as u8).wrapping_mul(3).wrapping_add(1); }
    dev.vblk_write_sync(lba, &data, std::time::Duration::from_secs(2))?;

    let out = dev.vblk_read_sync(lba, len, std::time::Duration::from_secs(2))?;
    if out == data { println!("vblk r/w ok"); } else { println!("vblk r/w mismatch: got {} bytes", out.len()); }

    Ok(())
}

