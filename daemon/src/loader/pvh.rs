//! PVH loader (skeleton)
//! Parses bzImage header, prepares boot params, and enters 64-bit PVH entry.
//! This is a placeholder to structure future work.

#[allow(dead_code)]
pub struct PvhLoader;

impl PvhLoader {
    pub fn new() -> Self { Self }
    pub fn load(&self) {
        // TODO: implement PVH setup according to Linux x86 boot protocol
    }
}

