// Virtual block IOCTL handling with async completion using work items.
// Wire format (METHOD_BUFFERED): [op:1][resv:3][lba:8][len:4][data...]

#include <ntddk.h>
#include "include/colinux_ioctls.h"

#define TAG_VBLK 'kbLV'

static HANDLE g_vblk_file = NULL;
static const ULONG SECTOR_SIZE = 512; // LBA in sectors
static const ULONG MAX_XFER = 128 * 1024; // cap single transfer

typedef struct _VBLK_WORK {
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    PIO_WORKITEM WorkItem;
    UCHAR op;
    ULONGLONG lba;
    ULONG len;
    PUCHAR in_data;   // pointer into captured buffer (copy)
    ULONG in_len;
} VBLK_WORK, *PVBLK_WORK;

static VOID VblkWorkRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PVBLK_WORK work = (PVBLK_WORK)Context;
    PIRP Irp = work->Irp;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    // METHOD_BUFFERED: SystemBuffer is both in/out. OutputBufferLength is max we can return.
    PUCHAR sys = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG out_max = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG bytes = 0;

    if (!g_vblk_file) {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        if (work->in_data) ExFreePoolWithTag(work->in_data, TAG_VBLK);
        if (work->WorkItem) IoFreeWorkItem(work->WorkItem);
        ExFreePoolWithTag(work, TAG_VBLK);
        return;
    }

    // Sector-based addressing with alignment check
    ULONG len = work->len;
    if (len == 0 || len > MAX_XFER || (len % SECTOR_SIZE) != 0) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        if (work->in_data) ExFreePoolWithTag(work->in_data, TAG_VBLK);
        if (work->WorkItem) IoFreeWorkItem(work->WorkItem);
        ExFreePoolWithTag(work, TAG_VBLK);
        return;
    }
    LARGE_INTEGER offset; offset.QuadPart = (LONGLONG)work->lba * SECTOR_SIZE;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = STATUS_SUCCESS;

    if (work->op == 0 /* Read */) {
        if (len > out_max) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            status = ZwReadFile(g_vblk_file, NULL, NULL, NULL, &iosb, sys, len, &offset, NULL);
            if (NT_SUCCESS(status)) {
                bytes = (ULONG)iosb.Information;
            }
        }
    } else { // Write
        ULONG header = 1 + 3 + 8 + 4;
        if (work->in_len < header + len) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else {
            PUCHAR payload = sys + header; // original input lives in SystemBuffer too
            if (work->in_data) payload = work->in_data; // prefer copied buffer if available
            status = ZwWriteFile(g_vblk_file, NULL, NULL, NULL, &iosb, payload, len, &offset, NULL);
            if (NT_SUCCESS(status)) {
                bytes = 0; // no output
            }
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytes;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    if (work->in_data) ExFreePoolWithTag(work->in_data, TAG_VBLK);
    if (work->WorkItem) IoFreeWorkItem(work->WorkItem);
    ExFreePoolWithTag(work, TAG_VBLK);
}

NTSTATUS CoLinuxHandleVblkSubmit(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    // Parse METHOD_BUFFERED input
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < (1 + 3 + 8 + 4)) {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_BUFFER_TOO_SMALL;
    }
    PUCHAR buf = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    if (!buf) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    UCHAR op = buf[0];
    ULONGLONG lba = *(ULONGLONG*)(buf + 4);
    ULONG len = *(ULONG*)(buf + 12);

    // Capture input payload for WRITE into non-paged pool (optional; we don't use it here)
    PUCHAR in_data = NULL;
    ULONG in_len = 0;
    ULONG header = 1 + 3 + 8 + 4;
    if (op == 1) { // Write
        in_len = IrpSp->Parameters.DeviceIoControl.InputBufferLength > header ?
            (IrpSp->Parameters.DeviceIoControl.InputBufferLength - header) : 0;
        if (in_len) {
            in_data = (PUCHAR)ExAllocatePoolWithTag(NonPagedPoolNx, in_len, TAG_VBLK);
            if (!in_data) {
                Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlCopyMemory(in_data, buf + header, in_len);
        }
    }

    PVBLK_WORK work = (PVBLK_WORK)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(VBLK_WORK), TAG_VBLK);
    if (!work) {
        if (in_data) ExFreePoolWithTag(in_data, TAG_VBLK);
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(work, sizeof(*work));
    work->DeviceObject = DeviceObject;
    work->Irp = Irp;
    work->WorkItem = IoAllocateWorkItem(DeviceObject);
    work->op = op;
    work->lba = lba;
    work->len = len;
    work->in_data = in_data;
    work->in_len = in_len;

    if (!work->WorkItem) {
        if (work->in_data) ExFreePoolWithTag(work->in_data, TAG_VBLK);
        ExFreePoolWithTag(work, TAG_VBLK);
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoMarkIrpPending(Irp);
    // Queue to the system worker; this makes completion asynchronous, matching IOCP expectations
    IoQueueWorkItem(work->WorkItem, VblkWorkRoutine, DelayedWorkQueue, work);
    return STATUS_PENDING;
}

// Open a backing file for vblk I/O. Input buffer is a UTF-16 Windows path (e.g., C:\\path\\file.img),
// we prepend "\\??\\" to form a kernel path.
NTSTATUS CoLinuxHandleVblkSetBacking(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(WCHAR) * 3) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }
    USHORT in_bytes = (USHORT)IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    PWCHAR in_buf = (PWCHAR)Irp->AssociatedIrp.SystemBuffer;
    if (!in_buf) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    // Build kernel path: "\\??\\" + input
    static const WCHAR prefix[] = L"\\??\\"; // \??\
    USHORT prefix_bytes = (USHORT)(sizeof(prefix) - sizeof(WCHAR)); // exclude null
    USHORT total = prefix_bytes + in_bytes;
    PWCHAR buf = (PWCHAR)ExAllocatePoolWithTag(NonPagedPoolNx, total + sizeof(WCHAR), TAG_VBLK);
    if (!buf) {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(buf, prefix, prefix_bytes);
    RtlCopyMemory((PUCHAR)buf + prefix_bytes, in_buf, in_bytes);
    ((PWCHAR)((PUCHAR)buf + total))[0] = L'\0';

    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, buf);

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &uname, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    IO_STATUS_BLOCK iosb;

    HANDLE h = NULL;
    NTSTATUS status = ZwCreateFile(
        &h,
        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
        &oa,
        &iosb,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
        NULL,
        0);

    ExFreePoolWithTag(buf, TAG_VBLK);

    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    if (g_vblk_file) ZwClose(g_vblk_file);
    g_vblk_file = h;

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

VOID CoLinuxVblkCloseBackingOnUnload(VOID) {
    if (g_vblk_file) {
        ZwClose(g_vblk_file);
        g_vblk_file = NULL;
    }
}

typedef struct _VBLK_RW_HDR {
    ULONGLONG lba;
    ULONG len;
    ULONG flags;
} VBLK_RW_HDR, *PVBLK_RW_HDR;

// Direct I/O read: METHOD_OUT_DIRECT, input carries hdr, output MDL holds destination buffer
NTSTATUS CoLinuxHandleVblkRead(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (!g_vblk_file) { Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_DEVICE_NOT_READY; }
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(VBLK_RW_HDR) || !Irp->AssociatedIrp.SystemBuffer || !Irp->MdlAddress) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INVALID_PARAMETER;
    }
    PVBLK_RW_HDR hdr = (PVBLK_RW_HDR)Irp->AssociatedIrp.SystemBuffer;
    ULONG len = hdr->len;
    if (len == 0 || len > MAX_XFER || (len % SECTOR_SIZE) != 0) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INVALID_PARAMETER;
    }
    PVOID out_sys = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!out_sys) { Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INSUFFICIENT_RESOURCES; }
    LARGE_INTEGER off; off.QuadPart = (LONGLONG)hdr->lba * SECTOR_SIZE;
    IO_STATUS_BLOCK iosb; NTSTATUS status = ZwReadFile(g_vblk_file, NULL, NULL, NULL, &iosb, out_sys, len, &off, NULL);
    if (NT_SUCCESS(status)) { Irp->IoStatus.Information = (ULONG)iosb.Information; } else { Irp->IoStatus.Information = 0; }
    Irp->IoStatus.Status = status; IoCompleteRequest(Irp, IO_NO_INCREMENT); return status;
}

// Direct I/O write: METHOD_IN_DIRECT. Input buffer carries hdr; MDL points to payload.
NTSTATUS CoLinuxHandleVblkWrite(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp) {
    if (!g_vblk_file) { Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_DEVICE_NOT_READY; }
    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(VBLK_RW_HDR) || !Irp->AssociatedIrp.SystemBuffer || !Irp->MdlAddress) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INVALID_PARAMETER;
    }
    PVBLK_RW_HDR hdr = (PVBLK_RW_HDR)Irp->AssociatedIrp.SystemBuffer;
    ULONG len = hdr->len;
    if (len == 0 || len > MAX_XFER || (len % SECTOR_SIZE) != 0) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INVALID_PARAMETER;
    }
    PVOID in_sys = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!in_sys) { Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES; Irp->IoStatus.Information = 0; IoCompleteRequest(Irp, IO_NO_INCREMENT); return STATUS_INSUFFICIENT_RESOURCES; }
    LARGE_INTEGER off; off.QuadPart = (LONGLONG)hdr->lba * SECTOR_SIZE;
    IO_STATUS_BLOCK iosb; NTSTATUS status = ZwWriteFile(g_vblk_file, NULL, NULL, NULL, &iosb, in_sys, len, &off, NULL);
    if (NT_SUCCESS(status)) { Irp->IoStatus.Information = 0; } else { Irp->IoStatus.Information = 0; }
    Irp->IoStatus.Status = status; IoCompleteRequest(Irp, IO_NO_INCREMENT); return status;
}
