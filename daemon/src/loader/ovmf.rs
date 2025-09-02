//! OVMF loader (skeleton)
//! Wires UEFI firmware to the WHP partition and lets the guest boot via UEFI.

#[allow(dead_code)]
pub struct OvmfLoader;

impl OvmfLoader {
    pub fn new() -> Self { Self }
    pub fn load(&self) {
        // TODO: map OVMF FD and required NVRAM pages, configure boot order, attach disks
    }
}

