// Simple WDM driver: creates \\Device\\coLinux and symlink \\.\\coLinux,
// handles IOCTLs via IRP_MJ_DEVICE_CONTROL and completes IRPs (some async).

#include <ntddk.h>
#include "include/colinux_ioctls.h"

#define TAG_COLX 'xlOC'

DRIVER_UNLOAD CoLinuxUnload;
DRIVER_DISPATCH CoLinuxCreate;
DRIVER_DISPATCH CoLinuxClose;
DRIVER_DISPATCH CoLinuxCleanup;
DRIVER_DISPATCH CoLinuxDeviceControl;

extern NTSTATUS CoLinuxHandleMapShared(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
extern NTSTATUS CoLinuxHandleRunTick(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
extern NTSTATUS CoLinuxHandleVblkSubmit(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
extern NTSTATUS CoLinuxHandleVblkSetBacking(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
extern VOID CoLinuxVblkCloseBackingOnUnload(VOID);
extern VOID CoLinuxOnCreate(_In_ PFILE_OBJECT FileObject);
extern VOID CoLinuxOnCleanup(_In_ PFILE_OBJECT FileObject);
extern NTSTATUS CoLinuxHandleVttyPush(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
extern NTSTATUS CoLinuxHandleVttyPull(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);

UNICODE_STRING g_DeviceName;
UNICODE_STRING g_SymLink;

void CoLinuxUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    IoDeleteSymbolicLink(&g_SymLink);
    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
    CoLinuxVblkCloseBackingOnUnload();
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    RtlInitUnicodeString(&g_DeviceName, L"\\Device\\coLinux");
    RtlInitUnicodeString(&g_SymLink,   L"\\DosDevices\\coLinux");

    PDEVICE_OBJECT device = NULL;
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0, // no device extension for now
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        return status;
    }

    for (UINT32 i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) { DriverObject->MajorFunction[i] = NULL; }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CoLinuxCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CoLinuxClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = CoLinuxCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = CoLinuxDeviceControl;
    DriverObject->DriverUnload = CoLinuxUnload;

    device->Flags |= DO_DIRECT_IO; // use direct I/O for large transfers
    device->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS CoLinuxCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    if (irpSp && irpSp->FileObject) { CoLinuxOnCreate(irpSp->FileObject); }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS CoLinuxClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    // Close after cleanup; nothing else to do
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS CoLinuxCleanup(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    if (irpSp && irpSp->FileObject) { CoLinuxOnCleanup(irpSp->FileObject); }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS CoLinuxDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = irpSp->Parameters.DeviceIoControl.IoControlCode;
    switch (code) {
        case IOCTL_COLINUX_MAP_SHARED:
            return CoLinuxHandleMapShared(Irp, irpSp);
        case IOCTL_COLINUX_RUN_TICK:
            return CoLinuxHandleRunTick(Irp, irpSp);
        case IOCTL_COLINUX_VBLK_SUBMIT:
            return CoLinuxHandleVblkSubmit(DeviceObject, Irp, irpSp);
        case IOCTL_COLINUX_VBLK_READ:
            extern NTSTATUS CoLinuxHandleVblkRead(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
            return CoLinuxHandleVblkRead(Irp, irpSp);
        case IOCTL_COLINUX_VBLK_WRITE:
            extern NTSTATUS CoLinuxHandleVblkWrite(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp);
            return CoLinuxHandleVblkWrite(Irp, irpSp);
        case IOCTL_COLINUX_VBLK_SET_BACKING:
            return CoLinuxHandleVblkSetBacking(Irp, irpSp);
        case IOCTL_COLINUX_VTTY_PUSH:
            return CoLinuxHandleVttyPush(Irp, irpSp);
        case IOCTL_COLINUX_VTTY_PULL:
            return CoLinuxHandleVttyPull(Irp, irpSp);
        default:
            Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}
