// Shared memory mapping via section object; per-handle context tracks maps.
#include <ntddk.h>
#include "include/colinux_ioctls.h"

#define TAG_MEM 'meLC'

typedef struct _FILE_CTX {
    HANDLE Section;
    PVOID  UserBase;
    SIZE_T UserSize;
    PVOID  KernelBase;
    SIZE_T KernelSize;
} FILE_CTX, *PFILE_CTX;

static PFILE_CTX GetFileCtx(PFILE_OBJECT fo) {
    return (PFILE_CTX)fo->FsContext;
}

VOID CoLinuxOnCreate(_In_ PFILE_OBJECT FileObject) {
    // Allocate per-handle context
    PFILE_CTX ctx = (PFILE_CTX)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(FILE_CTX), TAG_MEM);
    if (ctx) {
        RtlZeroMemory(ctx, sizeof(*ctx));
        FileObject->FsContext = ctx;
    }
}

VOID CoLinuxOnCleanup(_In_ PFILE_OBJECT FileObject) {
    PFILE_CTX ctx = GetFileCtx(FileObject);
    if (!ctx) return;
    if (ctx->UserBase) {
        ZwUnmapViewOfSection(ZwCurrentProcess(), ctx->UserBase);
        ctx->UserBase = NULL; ctx->UserSize = 0;
    }
    if (ctx->KernelBase) {
        MmUnmapViewInSystemSpace(ctx->KernelBase);
        ctx->KernelBase = NULL; ctx->KernelSize = 0;
    }
    if (ctx->Section) {
        ZwClose(ctx->Section);
        ctx->Section = NULL;
    }
    ExFreePoolWithTag(ctx, TAG_MEM);
    FileObject->FsContext = NULL;
}

typedef struct _MAP_INFO_OUT {
    ULONGLONG user_base;
    ULONGLONG kernel_base;
    ULONGLONG size;
    ULONG     ver;
    ULONG     flags;
} MAP_INFO_OUT, *PMAP_INFO_OUT;

typedef struct _RING_HEADER {
    ULONG ver;
    ULONG flags;
    ULONGLONG tick_count;
    ULONG ping_req;
    ULONG ping_resp;
} RING_HEADER, *PRING_HEADER;

typedef struct _RING_CTRL {
    ULONG prod;
    ULONG cons;
    ULONG cap;
    ULONG slot_size;
} RING_CTRL, *PRING_CTRL;

typedef struct _VBLK_SLOT {
    ULONGLONG id;
    UCHAR op;
    UCHAR status;
    USHORT rsvd;
    ULONGLONG lba;
    ULONG len;
    ULONG data_off;
} VBLK_SLOT, *PVBLK_SLOT;

NTSTATUS CoLinuxHandleMapShared(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MAP_INFO_OUT) ||
        Irp->AssociatedIrp.SystemBuffer == NULL) {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_BUFFER_TOO_SMALL;
    }

    ULONG pages = *(ULONG*)Irp->AssociatedIrp.SystemBuffer;
    SIZE_T size = (SIZE_T)pages * 4096ULL;

    PIO_STACK_LOCATION sp = irpSp; // alias
    PFILE_OBJECT fo = sp->FileObject;
    if (!fo) {
        Irp->IoStatus.Status = STATUS_INVALID_HANDLE;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_HANDLE;
    }
    PFILE_CTX ctx = GetFileCtx(fo);
    if (!ctx) {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Create a pagefile-backed section
    LARGE_INTEGER max; max.QuadPart = (LONGLONG)size;
    HANDLE sect = NULL;
    OBJECT_ATTRIBUTES oa; InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    NTSTATUS status = ZwCreateSection(&sect, SECTION_ALL_ACCESS, &oa, &max, PAGE_READWRITE, SEC_COMMIT, NULL);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    // Map into system space for kernel access
    PVOID kbase = NULL; SIZE_T kview = 0;
    status = MmMapViewInSystemSpace(sect, &kbase, &kview);
    if (!NT_SUCCESS(status)) {
        ZwClose(sect);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    // Map into caller process
    PVOID ubase = NULL; SIZE_T uview = 0; LARGE_INTEGER off; off.QuadPart = 0;
    status = ZwMapViewOfSection(sect, ZwCurrentProcess(), &ubase, 0, 0, &off, &uview, ViewUnmap, 0, PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        MmUnmapViewInSystemSpace(kbase);
        ZwClose(sect);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    // Save to context for cleanup
    ctx->Section = sect;
    ctx->KernelBase = kbase;
    ctx->KernelSize = kview;
    ctx->UserBase = ubase;
    ctx->UserSize = uview;

    // Initialize ring header at start of mapping
    if (kbase && kview >= sizeof(RING_HEADER)) {
        PRING_HEADER hdr = (PRING_HEADER)kbase;
        hdr->ver = 1; hdr->flags = 0; hdr->tick_count = 0; hdr->ping_req = 0; hdr->ping_resp = 0;
    }

    // Initialize VBLK ring control (multi-slot) at COLX_VBLK_RING_OFF
    if (kbase && kview >= (0x1000 + sizeof(RING_CTRL))) {
        PRING_CTRL ctrl = (PRING_CTRL)((PUCHAR)kbase + 0x1000);
        ctrl->prod = 0; ctrl->cons = 0; ctrl->cap = 8; ctrl->slot_size = sizeof(VBLK_SLOT);
        // Zero slots area just after ctrl
        SIZE_T slots_bytes = ctrl->cap * ctrl->slot_size;
        if (kview >= (0x1000 + sizeof(RING_CTRL) + slots_bytes)) {
            RtlZeroMemory((PUCHAR)ctrl + sizeof(RING_CTRL), slots_bytes);
        }
    }

    PMAP_INFO_OUT out = (PMAP_INFO_OUT)Irp->AssociatedIrp.SystemBuffer;
    out->user_base = (ULONGLONG)(ULONG_PTR)ubase;
    out->kernel_base = (ULONGLONG)(ULONG_PTR)kbase;
    out->size = (ULONGLONG)uview;
    out->ver = 1;
    out->flags = 0;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(MAP_INFO_OUT);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS CoLinuxHandleRunTick(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    UNREFERENCED_PARAMETER(IrpSp);
    PIO_STACK_LOCATION sp = IrpSp;
    PFILE_OBJECT fo = sp->FileObject;
    if (!fo || !fo->FsContext) {
        Irp->IoStatus.Status = STATUS_INVALID_HANDLE;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_HANDLE;
    }
    PFILE_CTX ctx = (PFILE_CTX)fo->FsContext;
    if (!ctx->KernelBase || ctx->KernelSize < sizeof(RING_HEADER)) {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    PRING_HEADER hdr = (PRING_HEADER)ctx->KernelBase;
    // Increment tick and echo ping
    hdr->tick_count++;
    if (hdr->ping_req != hdr->ping_resp) {
        hdr->ping_resp = hdr->ping_req;
        KeMemoryBarrier();
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
