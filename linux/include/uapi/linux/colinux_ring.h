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

/* Status codes (align loosely with errno on Linux) */
#define COLX_ST_OK      0
#define COLX_ST_EINVAL  1
#define COLX_ST_EIO     5
#define COLX_ST_ENOSPC  28
#define COLX_ST_ETIME   62

/* Offsets within the shared mapping for VBLK rings */
#define COLX_VBLK_RING_OFF   0x1000
#define COLX_VBLK_DATA_OFF   0x4000
#define COLX_VBLK_SLOT_DATA_STRIDE (128 * 1024)
/* Prototype ring capacity used by initial host mapping (see mem.c ctrl->cap = 8) */
#define COLX_VBLK_RING_CAP   8
/* Maximum contiguous data area usable by the prototype */
#define COLX_VBLK_DATA_MAX   (COLX_VBLK_SLOT_DATA_STRIDE * COLX_VBLK_RING_CAP)

/* VBLK opcodes */
#define COLX_VBLK_OP_READ   0
#define COLX_VBLK_OP_WRITE  1

/* Generic ring control (single producer/consumer) */
struct colx_ring_ctrl {
    __u32 prod;      /* producer increments on submit */
    __u32 cons;      /* consumer increments on complete */
    __u32 cap;       /* number of slots */
    __u32 slot_size; /* sizeof(struct colx_vblk_slot) */
};

/* VBLK ring slot (metadata) */
struct colx_vblk_slot {
    __u64 id;      /* opaque */
    __u8  op;      /* COLX_VBLK_OP_* */
    __u8  status;  /* COLX_ST_* */
    __u16 _rsvd;
    __u64 lba;     /* sector units (512B) */
    __u32 len;     /* bytes, multiple of 512, <= stride */
    __u32 data_off;/* offset from COLX_VBLK_DATA_OFF to data */
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
