use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct SharedMount {
    pub host_path: String,
    pub guest_path: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Config {
    pub memory_mb: u32,
    pub ringbuf_mb: u32,
    pub vblk_backing: String,    // e.g. C:\\KaliSync\\kali-rootfs.vhdx
    pub vblk_queue_depth: u32,
    pub vnet_mode: String,       // "bridge" | "nat"
    pub console_mode: String,    // "winpty"
    pub shared: SharedMount,
    pub tick_budget: u32,        // scheduler quantum
}

pub fn load(path: &str) -> anyhow::Result<Config> {
    Ok(serde_yaml::from_str(&std::fs::read_to_string(path)?)?)
}

