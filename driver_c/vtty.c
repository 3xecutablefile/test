// VTTY byte-stream over shared mapping: host<->guest console rings.
#include <ntddk.h>
#include "include/colinux_ioctls.h"

#define TAG_VTTY 'ytVC'

// Mirror of mem.c context (local subset)
typedef struct _FILE_CTX {
    HANDLE Section;
    PVOID  UserBase;
    SIZE_T UserSize;
    PVOID  KernelBase;
    SIZE_T KernelSize;
} FILE_CTX, *PFILE_CTX;

// Offsets and capacity must match UAPI
#define COLX_VTTY_TX_OFF   0x40000
#define COLX_VTTY_RX_OFF   0x50000
#define COLX_VTTY_CAP      (64 * 1024)

typedef struct _VTTY_RING {
    volatile ULONG head;
    volatile ULONG tail;
    ULONG cap;
    ULONG _pad;
    UCHAR buf[COLX_VTTY_CAP];
} VTTY_RING, *PVTTY_RING;

static __forceinline ULONG vmin(ULONG a, ULONG b) { return a < b ? a : b; }

static ULONG vtty_write_ring(PVTTY_RING ring, const UCHAR* src, ULONG len) {
    ULONG head = ring->head, tail = ring->tail, cap = ring->cap;
    ULONG used = (head - tail) & (cap - 1);
    ULONG free = cap - used - 1;
    ULONG n = vmin(len, free);
    ULONG first = vmin(n, cap - (head & (cap - 1)));
    if (first) RtlCopyMemory(&ring->buf[head & (cap - 1)], src, first);
    if (n > first) RtlCopyMemory(&ring->buf[0], src + first, n - first);
    KeMemoryBarrier();
    ring->head = (head + n) & (cap - 1);
    return n;
}

static ULONG vtty_read_ring(PVTTY_RING ring, UCHAR* dst, ULONG len) {
    ULONG head = ring->head, tail = ring->tail, cap = ring->cap;
    ULONG used = (head - tail) & (cap - 1);
    ULONG n = vmin(len, used);
    ULONG first = vmin(n, cap - (tail & (cap - 1)));
    if (first) RtlCopyMemory(dst, &ring->buf[tail & (cap - 1)], first);
    if (n > first) RtlCopyMemory(dst + first, &ring->buf[0], n - first);
    KeMemoryBarrier();
    ring->tail = (tail + n) & (cap - 1);
    return n;
}

NTSTATUS CoLinuxHandleVttyPush(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (!IrpSp->FileObject || !IrpSp->FileObject->FsContext) goto invalid;
    PFILE_CTX ctx = (PFILE_CTX)IrpSp->FileObject->FsContext;
    if (!ctx->KernelBase || ctx->KernelSize < (COLX_VTTY_TX_OFF + sizeof(VTTY_RING))) goto invalid;
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength == 0 || Irp->AssociatedIrp.SystemBuffer == NULL) goto invalid;
    PVTTY_RING tx = (PVTTY_RING)((PUCHAR)ctx->KernelBase + COLX_VTTY_TX_OFF);
    if (tx->cap == 0) tx->cap = COLX_VTTY_CAP; // init on first use
    ULONG n = vtty_write_ring(tx, (const UCHAR*)Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.DeviceIoControl.InputBufferLength);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = n;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
invalid:
    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CoLinuxHandleVttyPull(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (!IrpSp->FileObject || !IrpSp->FileObject->FsContext) goto invalid;
    PFILE_CTX ctx = (PFILE_CTX)IrpSp->FileObject->FsContext;
    if (!ctx->KernelBase || ctx->KernelSize < (COLX_VTTY_RX_OFF + sizeof(VTTY_RING))) goto invalid;
    if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength == 0 || Irp->AssociatedIrp.SystemBuffer == NULL) goto invalid;
    PVTTY_RING rx = (PVTTY_RING)((PUCHAR)ctx->KernelBase + COLX_VTTY_RX_OFF);
    if (rx->cap == 0) rx->cap = COLX_VTTY_CAP; // init on first use
    ULONG n = vtty_read_ring(rx, (UCHAR*)Irp->AssociatedIrp.SystemBuffer, IrpSp->Parameters.DeviceIoControl.OutputBufferLength);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = n;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
invalid:
    Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_PARAMETER;
}

