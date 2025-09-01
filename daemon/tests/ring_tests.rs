use colinux_daemon::ring::ByteRing;

#[test]
fn ring_wrap_and_full() {
    let mut r = ByteRing::with_capacity(16);
    assert_eq!(r.write(&[1, 2, 3, 4, 5, 6, 7]), 7);
    let mut tmp = [0u8; 4];
    assert_eq!(r.read(&mut tmp), 4);
    assert_eq!(&tmp, &[1, 2, 3, 4]);
    assert_eq!(r.write(&[8, 9, 10, 11, 12, 13, 14, 15, 16, 17]), 10);
    let mut out = vec![0u8; 12];
    let n = r.read(&mut out);
    assert_eq!(n, 12);
}
