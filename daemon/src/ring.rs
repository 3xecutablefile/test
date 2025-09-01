use std::sync::atomic::{AtomicU32, Ordering};

pub struct ByteRing {
    buf: Vec<u8>,
    cap: u32,
    head: AtomicU32,
    tail: AtomicU32,
}

impl ByteRing {
    pub fn with_capacity(cap: usize) -> Self {
        assert!(cap.is_power_of_two());
        Self {
            buf: vec![0u8; cap],
            cap: cap as u32,
            head: AtomicU32::new(0),
            tail: AtomicU32::new(0),
        }
    }

    #[inline]
    fn mask(&self) -> u32 { self.cap - 1 }

    pub fn write(&self, src: &[u8]) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        let used = head.wrapping_sub(tail) & self.mask();
        let free = self.cap - used - 1;
        let n = free.min(src.len() as u32) as usize;
        let idx = (head & self.mask()) as usize;
        let first = n.min(self.buf.len() - idx);
        if first > 0 { self.buf[idx..idx+first].copy_from_slice(&src[..first]); }
        if n > first { self.buf[..(n-first)].copy_from_slice(&src[first..n]); }
        self.head.store(head.wrapping_add(n as u32) & self.mask(), Ordering::Release);
        n
    }

    pub fn read(&self, dst: &mut [u8]) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        let used = head.wrapping_sub(tail) & self.mask();
        let n = used.min(dst.len() as u32) as usize;
        let idx = (tail & self.mask()) as usize;
        let first = n.min(self.buf.len() - idx);
        if first > 0 { dst[..first].copy_from_slice(&self.buf[idx..idx+first]); }
        if n > first { dst[first..n].copy_from_slice(&self.buf[..(n-first)]); }
        self.tail.store(tail.wrapping_add(n as u32) & self.mask(), Ordering::Release);
        n
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wraparound() {
        let r = ByteRing::with_capacity(8);
        assert_eq!(r.write(&[1,2,3,4,5]), 5);
        let mut out = [0u8;3];
        assert_eq!(r.read(&mut out), 3);
        assert_eq!(&out, &[1,2,3]);
        assert_eq!(r.write(&[6,7,8,9]), 4);
        let mut out2 = [0u8;6];
        let n = r.read(&mut out2);
        assert_eq!(n, 6);
        assert_eq!(&out2[..n], &[4,5,6,7,8,9]);
    }

    #[test]
    fn full_empty() {
        let r = ByteRing::with_capacity(8);
        let mut total = 0;
        total += r.write(&[0;7]);
        assert_eq!(total,7);
        total += r.write(&[1,2,3]);
        assert_eq!(total,7); // full minus 1 sentinel
        let mut out = [0u8;4];
        assert_eq!(r.read(&mut out), 4);
        assert_eq!(r.read(&mut out), 3);
        assert_eq!(r.read(&mut out), 0);
    }
}

