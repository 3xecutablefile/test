use serde::{Deserialize, Serialize};
use anyhow::{Context, Result, bail};
use std::path::Path;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct SharedMount {
    pub host_path: String,
    pub guest_path: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Config {
    pub memory_mb: u32,
    pub ringbuf_mb: u32,
    pub vblk_backing: String,    // e.g. C:\\KaliSync\\kali-rootfs-amd64.img
    pub vblk_queue_depth: u32,
    pub vnet_mode: String,       // "bridge" | "nat"
    pub console_mode: String,    // "winpty"
    pub shared: SharedMount,
    pub tick_budget: u32,        // scheduler quantum
}

pub fn load(path: &str) -> Result<Config> {
    let raw = std::fs::read_to_string(path).with_context(|| format!("reading config: {}", path))?;
    let cfg: Config = serde_yaml::from_str(&raw).context("parsing YAML")?;
    validate(&cfg).context("config validation failed")?;
    Ok(cfg)
}

pub fn validate(cfg: &Config) -> Result<()> {
    if cfg.memory_mb < 256 || cfg.memory_mb > 65536 { bail!("memory_mb out of range (256..65536)"); }
    if cfg.ringbuf_mb < 4 || cfg.ringbuf_mb > 1024 { bail!("ringbuf_mb out of range (4..1024)"); }
    if cfg.vblk_queue_depth == 0 || cfg.vblk_queue_depth > 1024 { bail!("vblk_queue_depth out of range (1..1024)"); }
    if cfg.tick_budget == 0 || cfg.tick_budget > 100_000 { bail!("tick_budget out of range (1..100000)"); }
    if !Path::new(&cfg.vblk_backing).exists() { bail!("vblk_backing not found: {}", cfg.vblk_backing); }
    Ok(())
}
