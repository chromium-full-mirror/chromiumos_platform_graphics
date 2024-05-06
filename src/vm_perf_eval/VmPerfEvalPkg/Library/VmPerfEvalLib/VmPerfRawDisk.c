/** @file
*
* Copyright 2024 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "VmPerfEvalInternal.h"
#include <Library/PrintLib.h>


#define RAW_DISK_PARTITION_NAME_0 L"EFIEXPERIMENT"
#define RAW_DISK_PARTITION_NAME_1 L"UEFIEXPERIMENT"

static
VOID
VmPerfRawDiskDumpMedia(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_BLOCK_IO_MEDIA *Media
)
{
    VmPerfLog(Ctx, 0, L"MediaId = %08X\n", Media->MediaId);
    VmPerfLog(
        Ctx, 0, L"ReadOnly = %a\n", (Media->ReadOnly) ? "TRUE" : "FALSE");
    VmPerfLog(
        Ctx, 0, L"WriteCaching = %a\n",
        (Media->WriteCaching) ? "TRUE" : "FALSE");
    VmPerfLog(Ctx, 0, L"BlockSize = %u\n", Media->BlockSize);
    VmPerfLog(Ctx, 0, L"IoAlign = %u\n", Media->IoAlign);
    VmPerfLog(Ctx, 0, L"LastBlock = %lu\n", Media->LastBlock);
}

static
BOOLEAN
VmPerfCanUseRawPartition(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_PARTITION_INFO_PROTOCOL *PartitionInfo
)
{
    /* Must be a GPT type partition */
    if (PartitionInfo->Type != PARTITION_TYPE_GPT)
        return FALSE;

    /* Must be named appropriately ("[U]EFIEXPERIMENT") */
    if (StrCmp(
        PartitionInfo->Info.Gpt.PartitionName,
        RAW_DISK_PARTITION_NAME_0) == 0 ||
        StrCmp(
        PartitionInfo->Info.Gpt.PartitionName,
        RAW_DISK_PARTITION_NAME_1) == 0) {
            return TRUE;
    }

    return FALSE;
}

BOOLEAN VmPerfRawDiskInit(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_GUID BlockProtocolGUID = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_GUID BlockProtocol2GUID = EFI_BLOCK_IO2_PROTOCOL_GUID;
    EFI_GUID PartitionInformationGUID = EFI_PARTITION_INFO_PROTOCOL_GUID;
    EFI_HANDLE *HandleArray = NULL;
    EFI_STATUS Status;
    UINTN NumHandles, i;
    BOOLEAN UseBlockIO2 = FALSE;
    EFI_PARTITION_INFO_PROTOCOL *PartitionInfo = NULL;
    EFI_BLOCK_IO2_PROTOCOL *BlockIo2Protocol;
    EFI_BLOCK_IO_PROTOCOL *BlockIoProtocol;

    Ctx->RawDisk.BlockIo2 = NULL;
    Ctx->RawDisk.BlockIo = NULL;
    Ctx->RawDisk.IsAvailable = FALSE;
    Ctx->RawDisk.UsingBlockIo2 = FALSE;

    Status = gBS->CreateEvent(
        0, TPL_CALLBACK, NULL, NULL, &Ctx->RawDisk.RawDiskEvent
    );

    /* This is needed for async access */
    if (EFI_ERROR(Status)) {
        VmPerfLog(Ctx, 0, L"Failed to create raw disk event.\n");
        return FALSE;
    }

    /*
    * Block IO2 Protocol is given preference as it supports non blocking IO.
    * With this protocol, we can generate disk accesses without putting the
    * main CPU to sleep. This could skew the results in performance tests
    * by idling the main CPU when it typically would not be idle.
    */
    Status = gBS->LocateHandleBuffer(
        ByProtocol,
        &BlockProtocol2GUID, NULL,
        &NumHandles, &HandleArray
    );

    if (Status != EFI_SUCCESS || NumHandles == 0) {
        /* Fallback to block protocol 1 */
        Status = gBS->LocateHandleBuffer(
            ByProtocol,
            &BlockProtocolGUID, NULL,
            &NumHandles, &HandleArray
        );

        if (Status != EFI_SUCCESS || NumHandles == 0) {
            /*
            * We can't really use raw disk return a false.
            * It is very unlikely for us to reach this path as
            * we need at least one block IO device to boot
            * the system.
            */
            Print(L"Found no BlockIO2 or BlockIO2 devices.\n");
            return FALSE;
        }

        /* Use Block IO protocol 1 */
        UseBlockIO2 = FALSE;
    } else {
        /* Use Block IO protocol 2 */
        UseBlockIO2 = TRUE;
    }

    /*
    * Loop through the available Block IO devices and try to retrieve
    * the partition information from the handle. In order to not risk
    * damage to any of the disk partitions on the system, the following
    * must be true in order for the partition to be used by testing:
    *
    * 1. A valid partition information protocol must have been successfully
    * fetched from the handle.
    * 2. It must be an EFI partition with a specific name ("EFIEXPERIMENT")
    */
    for (i = 0; i < NumHandles; i++) {
        Status = gBS->OpenProtocol(
            HandleArray[i],
            &PartitionInformationGUID,
            (VOID **)&PartitionInfo, gImageHandle,
            NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL
        );

        if (EFI_ERROR(Status)) {
            /* Failed to retrieve the partition information */
            continue;
        }

        if (!VmPerfCanUseRawPartition(Ctx, PartitionInfo)) {
            /* Partition verification failed */
            gBS->CloseProtocol(
                HandleArray[i],
                &PartitionInformationGUID,
                gImageHandle, NULL
            );

            continue;
        }

        /* Open the block IO protocol to retrieve further information */
        Status = gBS->OpenProtocol(
            HandleArray[i],
            (UseBlockIO2) ? &BlockProtocol2GUID : &BlockProtocolGUID,
            (UseBlockIO2) ? (VOID **)&BlockIo2Protocol :
                            (VOID **)&BlockIoProtocol,
            gImageHandle, NULL,
            EFI_OPEN_PROTOCOL_GET_PROTOCOL
        );

        if (EFI_ERROR(Status))
            continue;

        /*
        * All checks have passed, we can use this partition.
        * Save this information in the context and return TRUE
        */
        Ctx->RawDisk.IsAvailable = TRUE;
        Ctx->RawDisk.UsingBlockIo2 = UseBlockIO2;
        Ctx->RawDisk.EndingLBA = PartitionInfo->Info.Gpt.EndingLBA;
        Ctx->RawDisk.StartingLBA = PartitionInfo->Info.Gpt.StartingLBA;
        Ctx->RawDisk.BlockDeviceHandle = HandleArray[i];

        VmPerfLog(Ctx, 0, L"Raw Disk available for use.\n");

        /* We'll dump out information about the partition selected for use */
        if (UseBlockIO2) {
            Ctx->RawDisk.BlockIo2 = BlockIo2Protocol;
            VmPerfLog(Ctx, 0, L"Using BlockIo2 Protocol.\n");
            VmPerfRawDiskDumpMedia(Ctx, BlockIo2Protocol->Media);
        } else {
            Ctx->RawDisk.BlockIo = BlockIoProtocol;
            VmPerfLog(Ctx, 0, L"Using BlockIo Protocol (rev = %lu).\n",
                      BlockIoProtocol->Revision);
            VmPerfRawDiskDumpMedia(Ctx, BlockIoProtocol->Media);
        }

        VmPerfLog(Ctx, 0, L"Partition GUID = %g\n",
            &PartitionInfo->Info.Gpt.UniquePartitionGUID);
        VmPerfLog(Ctx, 0, L"Partition Type GUID = %g\n",
            &PartitionInfo->Info.Gpt.PartitionTypeGUID);

        /* Close the partition information protocol as it is no longer needed */
        gBS->CloseProtocol(
            HandleArray[i],
            &PartitionInformationGUID,
            gImageHandle, NULL
        );

        return TRUE;
    }

    return FALSE;
}

VOID VmPerfRawDiskShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    return;
}

BOOLEAN EFIAPI VmPerfGetRawDiskInformation(
    VM_PERF_EVAL_CTX *Ctx,
    VM_PERF_RAW_DISK_INFORMATION *RawDiskInformation
)
{
    BOOLEAN RawDiskAvailable = FALSE;
    EFI_BLOCK_IO_MEDIA *Media;

    if (Ctx->RawDisk.IsAvailable) {
        Media = (Ctx->RawDisk.UsingBlockIo2 ? Ctx->RawDisk.BlockIo2->Media :
                                             Ctx->RawDisk.BlockIo->Media);

        RawDiskInformation->BlockSize = Media->BlockSize;
        RawDiskInformation->BufferAlign = Media->IoAlign;
        RawDiskInformation->RawDiskSizeInBlocks = Media->LastBlock;
        RawDiskInformation->ReadOnly = Media->ReadOnly;
        RawDiskInformation->WriteCaching = Media->WriteCaching;
        RawDiskInformation->AsyncCapable = Ctx->RawDisk.UsingBlockIo2;

        RawDiskAvailable = TRUE;
    }

    return RawDiskAvailable;
}

BOOLEAN EFIAPI VmPerfReadRawDisk(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_LBA LbaStart,
    UINTN BlockCount,
    VOID *Buffer
)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO_MEDIA *Media;


    if (!Ctx->RawDisk.IsAvailable)
        return FALSE;

    Media = (Ctx->RawDisk.UsingBlockIo2) ?
            Ctx->RawDisk.BlockIo2->Media :
            Ctx->RawDisk.BlockIo->Media;

    if (Ctx->RawDisk.UsingBlockIo2) {
        Ctx->RawDisk.Token.Event = Ctx->RawDisk.RawDiskEvent;

        Status = Ctx->RawDisk.BlockIo2->ReadBlocksEx(
            Ctx->RawDisk.BlockIo2,
            Media->MediaId,
            LbaStart,
            &Ctx->RawDisk.Token,
            BlockCount * Media->BlockSize,
            Buffer
        );
    } else {

        Status = Ctx->RawDisk.BlockIo->ReadBlocks(
            Ctx->RawDisk.BlockIo,
            Media->MediaId, LbaStart,
            BlockCount * Media->BlockSize,
            Buffer
        );
    }

     if (EFI_ERROR(Status)) {
        return FALSE;
    }

    /* Read was successfully queued up. */
    return TRUE;
}

BOOLEAN EFIAPI VmPerfCheckRawDiskStatus(
    VM_PERF_EVAL_CTX *Ctx
)
{
    if (!Ctx->RawDisk.IsAvailable)
        return FALSE;

    /*
    * For non-blocking IO, just return TRUE as it should have finished
    * by now.
    */
    if (!Ctx->RawDisk.UsingBlockIo2)
        return TRUE;

    if (gBS->CheckEvent(Ctx->RawDisk.RawDiskEvent) != EFI_SUCCESS)
        return FALSE;

    return TRUE;
}

BOOLEAN EFIAPI VmPerfWriteRawDisk(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_LBA LbaStart,
    UINTN BlockCount,
    VOID *Buffer
)
{
    EFI_STATUS Status;
    EFI_BLOCK_IO_MEDIA *Media;


    if (!Ctx->RawDisk.IsAvailable)
        return FALSE;

    Media = (Ctx->RawDisk.UsingBlockIo2) ?
            Ctx->RawDisk.BlockIo2->Media :
            Ctx->RawDisk.BlockIo->Media;

    if (Ctx->RawDisk.UsingBlockIo2) {
        Ctx->RawDisk.Token.Event = Ctx->RawDisk.RawDiskEvent;

        Status = Ctx->RawDisk.BlockIo2->WriteBlocksEx(
            Ctx->RawDisk.BlockIo2,
            Media->MediaId,
            LbaStart,
            &Ctx->RawDisk.Token,
            BlockCount * Media->BlockSize,
            Buffer
        );
    } else {

        Status = Ctx->RawDisk.BlockIo->WriteBlocks(
            Ctx->RawDisk.BlockIo,
            Media->MediaId, LbaStart,
            BlockCount * Media->BlockSize,
            Buffer
        );
    }

     if (EFI_ERROR(Status)) {
        return FALSE;
    }

    /* Read was successfully queued up. */
    return TRUE;
}
