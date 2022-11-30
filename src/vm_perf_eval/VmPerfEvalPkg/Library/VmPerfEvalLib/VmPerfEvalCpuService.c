/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <VmPerfEvalApicLib.h>
#include "ApBoot/ApBoot.h"
#include "VmPerfEvalInternal.h"

BOOLEAN VmPerfEvalPutCpuInService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    VM_PERF_EVAL_ENUM_CPU *enumCpu;
    EFI_STATUS status;
    UINT32 PerStackPages;

    if (Index >= Ctx->NumAcpiCores)
        return FALSE;

    enumCpu = &Ctx->AcpiCores[Index];

    /* Check if core is already in service and bail out */
    if (enumCpu->InService)
        return FALSE;

    /* Allocate the necessary memory for this core */

    /* AP page (for client to communicate with code running on AP) */
    if (!enumCpu->CoreApPage) {
        enumCpu->CoreApPage = BASE_4GB - 1;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                    &enumCpu->CoreApPage);

        if (status != EFI_SUCCESS) {
            enumCpu->CoreApPage = 0;
            return FALSE;
        }
    }

    /*
    * PCIB page (Processor Control/Info Block):
    * Used for processor control and status
    * */
    if (!enumCpu->CorePcibPage) {
        enumCpu->CorePcibPage = BASE_1MB - 1;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1,
                                    &enumCpu->CorePcibPage);

        if (status != EFI_SUCCESS) {
            gBS->FreePages(enumCpu->CoreApPage, 1);
            enumCpu->CorePcibPage =
            enumCpu->CoreApPage = 0;
            return FALSE;
        }
    }

    /*
    * Private block to set up core data structures to be used when the CPU is
    * operating in Protected Mode or Long Mode.
    */
    if (!enumCpu->CorePrivBlock) {
        enumCpu->CorePrivBlock = BASE_4GB - 1;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    VMPERF_CORE_PRIVATE_PAGES,
                                    &enumCpu->CorePrivBlock);

        if (status != EFI_SUCCESS) {
            gBS->FreePages(enumCpu->CoreApPage, 1);
            gBS->FreePages(enumCpu->CorePcibPage, 1);
            enumCpu->CorePcibPage =
            enumCpu->CoreApPage =
            enumCpu->CorePrivBlock = 0;
            return FALSE;
        }

        enumCpu->CorePrivBlockPages = VMPERF_CORE_PRIVATE_PAGES;
    }

    /* Allocate the stack memory needed for operating this core */
    if (!enumCpu->CoreStack) {
        enumCpu->CoreStack = BASE_4GB - 1;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    VMPERF_CORE_STACK_PAGES,
                                    &enumCpu->CoreStack);
        if (status != EFI_SUCCESS) {
            gBS->FreePages(enumCpu->CoreApPage, 1);
            gBS->FreePages(enumCpu->CorePcibPage, 1);
            enumCpu->CoreApPage =
            enumCpu->CorePcibPage =
            enumCpu->CorePrivBlock = 0;
            return FALSE;
        }

        enumCpu->CoreStackPages = VMPERF_CORE_STACK_PAGES;
    }

    /*
    * Allocate Interrupt/NMI/Exception stacks (as a single block)
    */
    if (!enumCpu->CoreInterruptStack) {
        enumCpu->CoreInterruptStack = BASE_4GB - 1;
        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    VMPERF_CORE_INTERRUPT_STACK_PAGES,
                                    &enumCpu->CoreInterruptStack);
        if (status != EFI_SUCCESS) {
            gBS->FreePages(enumCpu->CoreApPage, 1);
            gBS->FreePages(enumCpu->CorePcibPage, 1);
            gBS->FreePages(enumCpu->CoreStack, 1);
            enumCpu->CoreApPage =
            enumCpu->CorePcibPage =
            enumCpu->CorePrivBlock =
            enumCpu->CoreStack = 0;
            return FALSE;
        }

        PerStackPages = VMPERF_CORE_INTERRUPT_STACK_PAGES / 3;

        enumCpu->CoreNMIStack = enumCpu->CoreInterruptStack +
                                (PerStackPages * EFI_PAGE_SIZE);
        enumCpu->CoreExceptionStack = enumCpu->CoreNMIStack +
                                      (PerStackPages * EFI_PAGE_SIZE);
        enumCpu->CoreInterruptStackPages = VMPERF_CORE_INTERRUPT_STACK_PAGES;
    }

    /* Clear the pages that should be cleared */
    SetMem((VOID *)enumCpu->CoreApPage, EFI_PAGE_SIZE, 0);
    SetMem((VOID *)enumCpu->CorePcibPage, EFI_PAGE_SIZE, 0);
    SetMem((VOID *)enumCpu->CorePrivBlock, EFI_PAGE_SIZE, 0);

    enumCpu->InService = TRUE;

    return TRUE;
}

VOID VmPerfEvalShutdownCpu(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    VM_PERF_EVAL_ENUM_CPU *enumCpu;

    if (Index >= Ctx->NumAcpiCores)
        return;

    enumCpu = &Ctx->AcpiCores[Index];

    /* Shutdown the CPU if it is running */
    if (VmPerfProbeInService(Ctx, Index) != CoreStatusIdle) {
        VmPerfEvalRemoveCpuFromService(Ctx, Index);
    }

    /* Free all memory associated with this core */
    RELEASE_PAGE(enumCpu->CoreApPage)
    RELEASE_PAGE(enumCpu->CorePcibPage)

    RELEASE_PAGES(enumCpu->CoreInterruptStack,
                  enumCpu->CoreInterruptStackPages)

    RELEASE_PAGES(enumCpu->CorePageTable,
                  enumCpu->PageTablePages)

    RELEASE_PAGES(enumCpu->CoreStack,
                  enumCpu->CoreStackPages)

    RELEASE_PAGES(enumCpu->CorePrivBlock,
                  enumCpu->CorePrivBlockPages)
}

BOOLEAN VmPerfEvalRemoveCpuFromService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    VM_PERF_EVAL_ENUM_CPU *enumCpu;

    if (Index >= Ctx->NumAcpiCores)
        return FALSE;

    enumCpu = &Ctx->AcpiCores[Index];

    if (!enumCpu->InService)
        return FALSE;

    enumCpu->InService = FALSE;

    /*
    * Send an INIT to the target CPU
    * The memory allocated for the core will not be released
    * under the expectation that the core could be put in service
    * again shortly.
    * Once memory is allocated for a core, it will be held onto until
    * the library is shutdown.
    */
    if (enumCpu->CoreIsRunning) {
        VmPerfEvalApicSendInit(Ctx->LapicAddress, enumCpu->ApicId);
        enumCpu->CoreIsRunning = FALSE;
    }

    return TRUE;
}

VM_PERF_CORE_STATUS VmPerfEvalProbeStatus(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_EVAL_ENUM_CPU *Cpu
)
{
    VM_PERF_CORE_STATUS Status = CoreStatusIdle;
    volatile PCIB *CpuPCIB = (volatile PCIB *)Cpu->CorePcibPage;
    UINT32 PcibStatus;

    if (CpuPCIB && Cpu->CoreIsRunning) {

        /* Grab the error code from the target processors info block */
        PcibStatus = CpuPCIB->statuscode;
        PcibStatus &= STATUSCODE_MASK;

        switch (PcibStatus) {
            case STATUSCODE_IN_ENTRY_POINT:
                /* CPU is still running */
                Status = CoreStatusRunning;
                break;

            case STATUSCODE_EXCEPTION_REAL_MODE:
            case STATUSCODE_EXCEPTION_PROTECTED_MODE:
            case STATUSCODE_EXCEPTION_LONG_MODE:
                /* CPU hit an exception and is halted */
                Status = CoreStatusException;
                break;

            case STATUSCODE_TRIGGER_WAIT:
                /* CPU is waiting for trigger word write */
                Status = CoreStatusNotTriggered;
                break;

            case STATUSCODE_SUCCESSFUL_RETURN:
                /* CPU has finished executing client code and is halted */
                Status = CoreStatusFinished;
                break;

            default:
                /* Leave as Idle status */
                break;
        }
    }

    return Status;
}
