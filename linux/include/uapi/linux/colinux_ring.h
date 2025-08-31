/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _UAPI_LINUX_COLINUX_RING_H
#define _UAPI_LINUX_COLINUX_RING_H

#include <linux/types.h>

/* Shared ring header placed at offset 0 of the shared mapping */
struct colx_ring_hdr {
    __u32 ver;
    __u32 flags;
    __u64 tick_count;
    __u32 ping_req;
    __u32 ping_resp;
};

#define COLX_VER_1 1

/* Offsets within the shared mapping for simple prototype rings */
#define COLX_VBLK_RING_OFF  0x1000
#define COLX_VBLK_DATA_OFF  0x2000
#define COLX_VBLK_DATA_MAX  (128 * 1024)

/* VBLK opcodes */
#define COLX_VBLK_OP_READ   0
#define COLX_VBLK_OP_WRITE  1

/* VBLK ring indices */
struct colx_ring_idx {
    __u32 prod; /* producer increments on submit */
    __u32 cons; /* consumer increments on complete */
};

/* VBLK request/response (single-slot prototype) */
struct colx_vblk_req {
    __u64 id;      /* opaque */
    __u8  op;      /* COLX_VBLK_OP_* */
    __u8  _pad[7];
    __u64 lba;     /* sector units (512B) */
    __u32 len;     /* bytes, multiple of 512, <= COLX_VBLK_DATA_MAX */
    __u32 status;  /* 0 = pending, 1 = ok, else errno-like */
};

/* VTTY byte rings (prototype) */
#define COLX_VTTY_TX_OFF   0x40000 /* host->guest */
#define COLX_VTTY_RX_OFF   0x50000 /* guest->host */
#define COLX_VTTY_CAP      (64 * 1024)

struct colx_vtty_ring {
    __u32 head;   /* write position */
    __u32 tail;   /* read position */
    __u32 cap;    /* capacity in bytes */
    __u32 _rsvd;
    __u8  buf[COLX_VTTY_CAP];
};

#endif /* _UAPI_LINUX_COLINUX_RING_H */
