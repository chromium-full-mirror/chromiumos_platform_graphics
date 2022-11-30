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
#include "VmPerfEvalInternal.h"

/* Boot strapper code for the AP's on the system */
static const UINT8 ApBootCode[] = {
#include "ApBoot/ApBootCode.h"
};

BOOLEAN EFIAPI VmPerfInitialize(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_STATUS status;

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

    for (i = 0; i < Ctx->NumAcpiCores; i++) {
        VmPerfEvalShutdownCpu(Ctx, i);
    }

    /* Free any pages associated with the global state */
    gBS->FreePages(Ctx->ApBootCodePage, 1);
    gBS->FreePages(Ctx->ApTriggerPage, 1);
    gBS->FreePages(Ctx->ApRealModeWorkPage, 1);
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
