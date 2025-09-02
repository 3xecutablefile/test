//! Stealth networking backend (skeleton)
//! Shared host IP via slirp/host-socket proxy. Outbound TCP/UDP only.

#[allow(dead_code)]
pub struct StealthNet;

impl StealthNet {
    pub fn new() -> Self { Self }
    pub fn start(&self) {
        // TODO: implement TCP connect/send/recv proxy; DNS resolver
    }
}

