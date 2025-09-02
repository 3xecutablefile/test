//! Passthrough networking backend (skeleton)
//! L2 presence via TAP/Wintun or USB/Ethernet NIC forwarding.

#[allow(dead_code)]
pub struct PassThroughNet;

impl PassThroughNet {
    pub fn new() -> Self { Self }
    pub fn start(&self) {
        // TODO: bind to TAP/USB NIC, bridge frames to virtio-net device model
    }
}

