/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include "VmPerfEvalInternal.h"

/* Boot strapper code for the AP's on the system */
static const UINT8 ApBootCode[] = {
#include "ApBoot/ApBootCode.h"
};

static
VOID
ReplaceInstanceInString(
    CHAR16 *String,
    CHAR16 *MatchCharacter,
    CHAR16 *NewCharacter
)
{
    CHAR16 *CurrentStringPtr = String;

    while ((CurrentStringPtr = StrStr(CurrentStringPtr, MatchCharacter))) {
        *CurrentStringPtr = *NewCharacter;
        /* Replace the character */
    }
}

static
VOID
VmPerfSetupGetTestDirectoryName(
    CHAR16 *OutputBuffer,
    UINTN   OutputBufferLength
)
{
    EFI_STATUS Status;
    EFI_TIME CurrentTime;

    Status = gRT->GetTime(&CurrentTime, NULL);
    if (Status != EFI_SUCCESS) {
        /*
        * TODO: Use an alternate directory name, like TESTnnnn
        */
        StrCpyS(OutputBuffer, OutputBufferLength, L"UNNAMED");
        return;
    }

    UnicodeSPrint(OutputBuffer, OutputBufferLength, L"%t", &CurrentTime);

    ReplaceInstanceInString(OutputBuffer, L" ", L"_");
    ReplaceInstanceInString(OutputBuffer, L":", L"_");
    ReplaceInstanceInString(OutputBuffer, L"/", L"_");
    ReplaceInstanceInString(OutputBuffer, L"\\", L"_");
}

static
BOOLEAN VmPerfSetupFilesystemAcess(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_STATUS Status;
    CHAR16 DirectoryName[128];
    EFI_GUID SimpleFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    Ctx->RootDevice = NULL;
    Ctx->RootDirectory = NULL;
    Ctx->TestDirectory = NULL;
    Ctx->FileAccessOK = FALSE;

    /*
    * Open up the Simple file system protocol for the device
    * on which this program image has been loaded from.
    */
    Status = gBS->OpenProtocol(
        Ctx->LoadedImage->DeviceHandle,
        &SimpleFileSystemGuid,
        (VOID **)&Ctx->RootDevice,
        gImageHandle, NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (Status != EFI_SUCCESS) {
        return FALSE;
    }

    /* Open the root volume here */
    Status = Ctx->RootDevice->OpenVolume(Ctx->RootDevice, &Ctx->RootDirectory);
    if (Status != EFI_SUCCESS) {
        /* We need this to perform any file system access */
        return FALSE;
    }

    VmPerfSetupGetTestDirectoryName(
        DirectoryName,
        sizeof(DirectoryName)
    );

    /* Now create the directory for all test output files */
    Status = Ctx->RootDirectory->Open(
        Ctx->RootDirectory,
        &Ctx->TestDirectory,
        DirectoryName,
        EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
        EFI_FILE_DIRECTORY
    );

    if (Status != EFI_SUCCESS) {
        /* We also need a directory (to keep things organized) */
        return FALSE;
    }

    Ctx->TestDirectory->Flush(Ctx->TestDirectory);

    Ctx->FileAccessOK = TRUE;
    return TRUE;
}

BOOLEAN EFIAPI VmPerfInitialize(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_STATUS status;
    EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    status = gBS->OpenProtocol(
        gImageHandle,
        &LoadedImageGuid,
        (VOID **)&Ctx->LoadedImage,
        gImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
    );

    if (status != EFI_SUCCESS) {
        Print(L"WARNING: Could not retrieve LoadedImageProtocol!\n");
        return FALSE;
    }

    /* Must be setup first */
    if (!VmPerfSetupFilesystemAcess(Ctx)) {
        Print(L"WARNING: Could not retrieve simple file system protocol!\n");
        Print(L"WARNING: File logging and output will be disabled!.\n");
    }

    if (!VmPerfLoggingInit(Ctx)) {
        Print(L"WARNING: Logging initialization failed.\n");
        Print(L"WARNING: Log output will not be available.\n");
    }

    Ctx->ApBootCodePage = BASE_1MB - 1;
    Ctx->ApTriggerPage = BASE_1MB - 1;
    Ctx->ApRealModeWorkPage = BASE_1MB - 1;

    /* Allocate memory for the AP boot code */
    status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                &Ctx->ApBootCodePage);
    if (status != EFI_SUCCESS) {
        return FALSE;
    }

    /* Allocate memory for the trigger page */
    status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                &Ctx->ApTriggerPage);
    if (status != EFI_SUCCESS) {
        gBS->FreePages(Ctx->ApBootCodePage, 1);
        return FALSE;
    }

    status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                &Ctx->ApRealModeWorkPage);
    if (status != EFI_SUCCESS) {
        gBS->FreePages(Ctx->ApBootCodePage, 1);
        gBS->FreePages(Ctx->ApTriggerPage, 1);
        return FALSE;
    }

    /* Clear out the trigger page */
    SetMem((VOID *)Ctx->ApTriggerPage, EFI_PAGE_SIZE, 0);

    /* Copy the AP boot Code binary */
    CopyMem((VOID *)Ctx->ApBootCodePage, ApBootCode, sizeof(ApBootCode));

    /*
    * Scan the ACPI tables to determine how many cores are available
    * on the machine we are running on.
    */
    Ctx->NumAcpiCores = VmPerfEvalScanCpus(Ctx);
    return TRUE;
}

UINT64 EFIAPI VmPerfPutInService(
    IN VM_PERF_EVAL_CTX  *Ctx,
    IN VM_PERF_CORE_TYPE CoreType,
    IN UINT32 Index
)
{
    UINT64 CoresInServiceMask = 0;
    UINT32 i;

    if (CoreType == CoreTypeAll) {
        for (i = 0; i < Ctx->NumAcpiCores; i++) {
            if (VmPerfEvalPutCpuInService(Ctx, i))
                CoresInServiceMask |= (1ULL << i);
        }
    } else if (CoreType == CoreTypeIndividual) {
        if (VmPerfEvalPutCpuInService(Ctx, Index))
            CoresInServiceMask |= (1ULL << Index);
    }

    return CoresInServiceMask;
}

UINT64 EFIAPI VmPerfRemoveFromService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_CORE_TYPE CoreType,
    IN UINT32 Index
)
{
    UINT64 CoresRemovedFromServiceMask = 0;
    UINT32 i;

    if (CoreType == CoreTypeAll) {
        for (i = 0; i < Ctx->NumAcpiCores; i++) {
            if (VmPerfEvalRemoveCpuFromService(Ctx, i))
                CoresRemovedFromServiceMask |= (1ULL << i);
        }
    } else if (CoreType == CoreTypeIndividual) {
        if (VmPerfEvalRemoveCpuFromService(Ctx, Index))
                CoresRemovedFromServiceMask |= (1ULL << Index);
    }

    return CoresRemovedFromServiceMask;
}

VOID EFIAPI VmPerfTrigger(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN BOOLEAN  TriggerSet
)
{
    volatile UINT32 *TriggerPage = (volatile UINT32 *)Ctx->ApTriggerPage;

    /* Write the value to the first DWORD in the trigger page */
    *TriggerPage = (TriggerSet) ? 1 : 0;
}

VM_PERF_CORE_STATUS EFIAPI VmPerfProbeInService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    VM_PERF_EVAL_ENUM_CPU *enumCpu;

    if (Index >= Ctx->NumAcpiCores)
        return CoreStatusIdle;

    enumCpu = &Ctx->AcpiCores[Index];
    if (!enumCpu->InService)
        return CoreStatusIdle;

    return VmPerfEvalProbeStatus(Ctx, enumCpu);
}

BOOLEAN EFIAPI VmPerfStartCore(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index,
    IN CORE_ENTRY_INFO *EntryInfo
)
{
    return VmPerfEvalBootCore(Ctx, Index, EntryInfo);
}

VOID EFIAPI VmPerfShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    /*
    * Shutdown all CPU's and free any associated memory
    */
    UINT32 i;

    VmPerfLoggingShutdown(Ctx);

    for (i = 0; i < Ctx->NumAcpiCores; i++) {
        VmPerfEvalShutdownCpu(Ctx, i);
    }

    /* Free any pages associated with the global state */
    gBS->FreePages(Ctx->ApBootCodePage, 1);
    gBS->FreePages(Ctx->ApTriggerPage, 1);
    gBS->FreePages(Ctx->ApRealModeWorkPage, 1);

    /* Release our directory handles */
    if (Ctx->TestDirectory)
        Ctx->TestDirectory->Close(Ctx->TestDirectory);

    if (Ctx->RootDirectory)
        Ctx->RootDirectory->Close(Ctx->RootDirectory);

    Ctx->TestDirectory = NULL;
    Ctx->RootDirectory = NULL;
}

VOID* EFIAPI VmPerfGetApPage(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    /*
    * Under the EFI Virtual Addresses = Physical Addresses
    * so this case is OK.
    */
    if (Index < Ctx->NumAcpiCores) {
        return (VOID *)Ctx->AcpiCores[Index].CoreApPage;
    } else {
        return NULL;
    }
}

UINT64 EFIAPI VmPerfGetReturnValue(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    return VmPerfEvalGetReturnFromPCIB(Ctx, Index);
}

UINT64 EFIAPI VmPerfGetEnumeratedCoreMask(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    UINT64 Mask;

    if (Ctx->NumAcpiCores >= 64) {
        Mask = 0xFFFFFFFFFFFFFFFFULL;
    } else {
        Mask = (1ULL << Ctx->NumAcpiCores) - 1;
    }

    return Mask;
}

EFI_PHYSICAL_ADDRESS EFIAPI VmPerfMakeRealModeEntryPoint(
    IN UINT64 RealModeAddress
)
{
    EFI_PHYSICAL_ADDRESS EntryPoint;
    UINT32 CSValue;

    /*
    * In Real mode the segment registers are shifted left by 4 and then
    * summed up with the offset to get the physical address.
    * Since we assume that the entry point is 4K aligned, the entry point is:
    *
    * NN000
    *
    * Shifting to the right 4: NN00
    * Then we can set IP to 0, so the EntryPoint value is to be set to
    * NN000000 (or NN00:0000)
    */

    CSValue = (RealModeAddress & 0xFFFFF) >> 4;
    EntryPoint = (CSValue << 16);

    return EntryPoint;
}
