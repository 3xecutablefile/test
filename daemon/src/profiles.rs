use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Profile {
    pub boot_mode: String,     // "pvh" | "ovmf"
    pub net_mode: String,      // "stealth" | "passthrough"
    pub kernel: String,
    pub initrd: Option<String>,
    pub cmdline: String,
}

impl Default for Profile {
    fn default() -> Self {
        Self {
            boot_mode: "pvh".into(),
            net_mode: "stealth".into(),
            kernel: String::new(),
            initrd: None,
            cmdline: "console=ttyS0".into(),
        }
    }
}

pub fn save_profile(path: &str, p: &Profile) -> anyhow::Result<()> {
    let data = serde_yaml::to_string(p)?;
    fs::write(path, data)?;
    Ok(())
}

pub fn load_profile(path: &str) -> anyhow::Result<Profile> {
    if !Path::new(path).exists() {
        anyhow::bail!("profile not found: {}", path);
    }
    let raw = fs::read_to_string(path)?;
    let p: Profile = serde_yaml::from_str(&raw)?;
    Ok(p)
}

