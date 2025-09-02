// Experimental: minimal Windows Hypervisor Platform (WHP) scaffolding.
// Goal: provide a foundation to boot a real Linux kernel (bzImage) under WHP,
// independent from the cooperative path. This is Windows-only.

#![cfg(windows)]

use anyhow::{anyhow, bail, Context, Result};
use std::ffi::c_void;
use std::ptr::null_mut;
use windows::core::HRESULT;
use windows::Win32::Foundation::HANDLE;
use windows::Win32::System::Hypervisor::*;
use windows::Win32::System::Memory::*;
use dialoguer::{theme::ColorfulTheme, Input, Select};
use crate::profiles::{self, Profile};

#[derive(Clone, Copy, Debug)]
pub enum NetMode { Stealth, Passthrough }

#[derive(Clone, Copy, Debug)]
pub enum BootMode { PVH, OVMF }

pub struct BootOptions<'a> {
    pub bzimage: &'a str,
    pub initrd: Option<&'a str>,
    pub cmdline: Option<&'a str>,
    pub net: NetMode,
    pub boot: BootMode,
}

pub fn boot_kernel(opts: &BootOptions) -> Result<()> {
    // Sanity checks (files exist)
    if !std::path::Path::new(opts.bzimage).exists() {
        bail!("kernel image not found: {}", opts.bzimage);
    }
    if let Some(i) = opts.initrd {
        if !std::path::Path::new(i).exists() {
            bail!("initrd not found: {}", i);
        }
    }

    // 1) Check WHP availability
    ensure_whp_present()?;

    // 2) Create partition
    let part = create_partition()?;
    unsafe { set_processor_count(part, 1)?; }
    unsafe { WHvSetupPartition(part).ok()?; }

    // 3) Allocate guest memory (prototype: 512 MiB)
    let mem_size: usize = 512 * 1024 * 1024;
    let mem = unsafe {
        VirtualAlloc(null_mut(), mem_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
    } as *mut u8;
    if mem.is_null() {
        unsafe { WHvDeletePartition(part) };
        bail!("VirtualAlloc guest memory failed");
    }

    // 4) Map memory into GPA space at 0
    unsafe {
        WHvMapGpaRange(
            part,
            mem as *const c_void,
            0,
            mem_size,
            WHV_MAP_GPA_RANGE_FLAGS(
                WHV_MAP_GPA_RANGE_FLAG_READ.0 | WHV_MAP_GPA_RANGE_FLAG_WRITE.0 | WHV_MAP_GPA_RANGE_FLAG_EXECUTE.0,
            ),
        )
        .ok()?;
    }

    // 5) Load and prepare kernel per boot mode (placeholder wiring)
    let _ = (opts.initrd, opts.cmdline);
    match opts.boot {
        BootMode::PVH => {
            let kernel_bytes = std::fs::read(opts.bzimage).with_context(|| "read bzImage")?;
            // Prototype: load at 2MB; real PVH loader must parse headers and set CPU/boot params properly
            unsafe { std::ptr::copy_nonoverlapping(kernel_bytes.as_ptr(), mem.add(0x200000), kernel_bytes.len().min(mem_size - 0x200000)); }
        }
        BootMode::OVMF => {
            // TODO: map OVMF firmware and boot via UEFI
            tracing::info!("hypervisor.boot", mode="OVMF", "OVMF path not implemented (stub)");
        }
    }

    // 5b) Networking backend selection (skeleton)
    match opts.net {
        NetMode::Stealth => {
            // Placeholder: would initialize a slirp/host-socket proxy backend and expose a minimal virtio-net device to the guest.
            tracing::info!("hypervisor.net", mode = "stealth", "Using host-socket proxy (slirp-like) [stub]");
        }
        NetMode::Passthrough => {
            // Placeholder: would attach a TAP/USB NIC and bridge or forward frames at L2.
            tracing::info!("hypervisor.net", mode = "passthrough", "Using NIC passthrough/bridge [stub]");
        }
    }

    // 6) Create one vCPU
    unsafe { WHvCreateVirtualProcessor(part, 0, 0).ok()?; }

    // 7) Run loop: capture port I/O to print early serial (COM1 @ 0x3F8). This allows observing progress
    let mut printed = 0usize;
    loop {
        let mut exit_ctx: WHV_RUN_VP_EXIT_CONTEXT = unsafe { std::mem::zeroed() };
        let mut bytes_ret: u32 = 0;
        let hr = unsafe {
            WHvRunVirtualProcessor(
                part,
                0,
                &mut exit_ctx as *mut _ as *mut c_void,
                std::mem::size_of::<WHV_RUN_VP_EXIT_CONTEXT>() as u32,
                &mut bytes_ret,
            )
        };
        if !hr.is_ok() {
            unsafe { WHvDeletePartition(part); }
            return Err(anyhow!("WHvRunVirtualProcessor failed: 0x{:08x}", hr.0 as u32));
        }
        unsafe {
            match exit_ctx.ExitReason {
                WHV_RUN_VP_EXIT_REASON_X64_IO_PORT_ACCESS => {
                    let io = exit_ctx.__bindgen_anon_1.X64IoPortAccess; // union view
                    let port = io.PortNumber;
                    let is_write = io.AccessInfo.IsWrite() != 0;
                    // Data bytes are in io.Data[0..]
                    if is_write && port == 0x3F8 { // COM1 THR
                        let ch = io.__bindgen_anon_1.Data[0] as char;
                        print!("{}", ch);
                        let _ = std::io::Write::flush(&mut std::io::stdout());
                        printed += 1;
                    }
                }
                WHV_RUN_VP_EXIT_REASON_X64_HALT => {
                    println!("[whp] guest halted ({} chars printed)", printed);
                    break;
                }
                _ => {
                    // Continue until we have a fully initialized CPU state
                    // Avoid hot loop by yielding slightly
                    std::thread::yield_now();
                }
            }
        }
        if printed > 0 && printed % 160 == 0 { tracing::debug!("serial progress", printed); }
        // For now, limit runtime to avoid runaway loop without initialized guest
        if printed > 1024 { break; }
    }
    unsafe { WHvDeletePartition(part); }
    bail!("hypervisor PVH path not fully implemented: serial capture loop exited")
}

fn ensure_whp_present() -> Result<()> {
    let mut present: u8 = 0;
    let mut written = 0u32;
    unsafe {
        WHvGetCapability(
            WHV_CAPABILITY_CODE(WHvCapabilityCodeHypervisorPresent.0),
            &mut present as *mut _ as *mut c_void,
            std::mem::size_of::<u8>() as u32,
            &mut written,
        )
        .ok()?;
    }
    if present == 0 {
        bail!("Windows Hypervisor Platform not available. Enable Hyper-V / virtualization in BIOS and Windows Features.");
    }
    Ok(())
}

fn create_partition() -> Result<WHV_PARTITION_HANDLE> {
    let mut part: WHV_PARTITION_HANDLE = HANDLE(0);
    unsafe { WHvCreatePartition(&mut part as *mut _) }.ok()?;
    Ok(part)
}

unsafe fn set_processor_count(part: WHV_PARTITION_HANDLE, count: u32) -> Result<()> {
    let mut prop: WHV_PARTITION_PROPERTY = std::mem::zeroed();
    prop.ProcessorCount = count;
    WHvSetPartitionProperty(
        part,
        WHV_PARTITION_PROPERTY_CODE(WHvPartitionPropertyCodeProcessorCount.0),
        &prop as *const _ as *const c_void,
        std::mem::size_of::<WHV_PARTITION_PROPERTY>() as u32,
    )
    .ok()?;
    Ok(())
}

// Interactive operator console
pub fn run_operator_menu() -> Result<()> {
    banner();
    let theme = ColorfulTheme::default();

    // Optional: load profile
    let action_idx = Select::with_theme(&theme)
        .with_prompt("Action")
        .items(&["Launch", "Load profile", "Save profile", "Exit"])
        .default(0)
        .interact()?;
    if action_idx == 3 { return Ok(()); }
    let mut loaded: Option<Profile> = None;
    if action_idx == 1 {
        let ppath: String = Input::with_theme(&theme).with_prompt("Profile path (YAML)").default("profile.yaml".into()).interact_text()?;
        match profiles::load_profile(&ppath) {
            Ok(p) => { println!("Loaded profile: {}", &ppath); loaded = Some(p); },
            Err(e) => { eprintln!("Failed to load profile: {e}"); }
        }
    }

    // Boot mode
    let boot = if let Some(p) = &loaded {
        if p.boot_mode.eq_ignore_ascii_case("ovmf") { BootMode::OVMF } else { BootMode::PVH }
    } else {
        let boot_idx = Select::with_theme(&theme)
            .with_prompt("Choose Boot Mode")
            .items(&["PVH (default, stealth, fast)", "OVMF (UEFI, broad compatibility)"])
            .default(0)
            .interact()?;
        if boot_idx == 0 { BootMode::PVH } else { BootMode::OVMF }
    };

    // Network mode
    let net = if let Some(p) = &loaded {
        if p.net_mode.eq_ignore_ascii_case("passthrough") { NetMode::Passthrough } else { NetMode::Stealth }
    } else {
        let net_idx = Select::with_theme(&theme)
            .with_prompt("Choose Network Mode")
            .items(&["Stealth (shared host IP)", "Passthrough (dedicated NIC)"])
            .default(0)
            .interact()?;
        if net_idx == 0 { NetMode::Stealth } else { NetMode::Passthrough }
    };

    // Paths and cmdline
    let kernel: String = if let Some(p) = &loaded { p.kernel.clone() } else {
        Input::with_theme(&theme)
            .with_prompt("Enter kernel path")
            .validate_with(|s: &String| -> Result<(), &str> { if std::path::Path::new(s).exists() { Ok(()) } else { Err("file not found") } })
            .interact_text()?
    };

    let initrd: String = if let Some(p) = &loaded { p.initrd.clone().unwrap_or_default() } else {
        Input::with_theme(&theme)
            .with_prompt("Enter initrd path (blank = none)")
            .allow_empty(true)
            .interact_text()?
    };

    let cmdline: String = if let Some(p) = &loaded { p.cmdline.clone() } else {
        Input::with_theme(&theme)
            .with_prompt("Enter kernel cmdline")
            .default("console=ttyS0".into())
            .interact_text()?
    };

    // Maybe save profile
    if action_idx == 2 {
        let savep: String = Input::with_theme(&theme).with_prompt("Save profile path (YAML)").default("profile.yaml".into()).interact_text()?;
        let prof = Profile {
            boot_mode: match boot { BootMode::PVH => "pvh".into(), BootMode::OVMF => "ovmf".into() },
            net_mode: match net { NetMode::Stealth => "stealth".into(), NetMode::Passthrough => "passthrough".into() },
            kernel: kernel.clone(),
            initrd: if initrd.trim().is_empty() { None } else { Some(initrd.clone()) },
            cmdline: cmdline.clone(),
        };
        if let Err(e) = profiles::save_profile(&savep, &prof) { eprintln!("Failed to save profile: {e}"); } else { println!("Saved profile: {}", savep); }
    }

    println!("\n[+] Launching Linux guest...");
    println!("Boot Mode : {:?}", boot);
    println!("Net Mode  : {:?}", net);
    println!("Kernel    : {}", std::path::Path::new(&kernel).file_name().and_then(|s| s.to_str()).unwrap_or(&kernel));

    let opts = BootOptions {
        bzimage: &kernel,
        initrd: if initrd.trim().is_empty() { None } else { Some(initrd.trim()) },
        cmdline: if cmdline.trim().is_empty() { None } else { Some(cmdline.trim()) },
        net,
        boot,
    };
    boot_kernel(&opts)
}

fn banner() {
    println!("\x1b[1;36m[ coLinux 2.0 - WHP Edition ]\x1b[0m\n\x1b[90m=================================\x1b[0m");
}
