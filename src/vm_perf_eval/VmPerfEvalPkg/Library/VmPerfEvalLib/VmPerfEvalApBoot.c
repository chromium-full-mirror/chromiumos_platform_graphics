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
#include "ApBoot/ApBoot.h"
#include <VmPerfEvalApicLib.h>

/*
* Stacks are arranged as follows (assume a 3 page total stack size)
* <TopGuard> (Page 2)
* <Stack> (Page 1)
* <BottomGuard> (Page 0)
*
* So the initial stack pointer will be
* StackMem + (BottomGuard + Stack) * PAGE_SIZE
*
* Page 0 and Page 2 will be setup so that exceptions are triggered
* if an access is attempted. This is mainly to help catch (any) stack issues
* in client code.
*
* Offsets are relative to the base address of the stack memory
*/
typedef struct {
    UINT32  TopGuardPages;
    UINT32  BottomGuardPages;
    UINT32  StackPages;
    UINT32  InitialStackOffset;
    UINT32  BaseOffsetForMapping;
} ApStackConfig;


static VOID VmPerfEvalCalcStackConfig(
    IN UINT32 TotalStackPages,
    OUT ApStackConfig *Output
)
{
    Output->StackPages = (TotalStackPages / 2);
    Output->TopGuardPages = (TotalStackPages - Output->StackPages) / 2;
    Output->BottomGuardPages = TotalStackPages -
        (Output->TopGuardPages + Output->StackPages);
    Output->InitialStackOffset = (Output->StackPages * EFI_PAGE_SIZE) +
                                 (Output->BottomGuardPages * EFI_PAGE_SIZE);

    Output->BaseOffsetForMapping = (Output->BottomGuardPages * EFI_PAGE_SIZE);
}

static BOOLEAN VmPerfEvalHandlePageTables(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN CORE_ENTRY_INFO *Entry,
    IN VM_PERF_EVAL_ENUM_CPU *Cpu,
    IN volatile PCIB *CpuPCIB
)
{
    BOOLEAN Handled = TRUE;
    UINT64 PtSize;
    EFI_STATUS status;

    /*
    * Handle the page table buffer here, this page table buffer is "growable"
    * It is expected that a mapping may change for a particular core during a
    * test run and this can result in a larger footprint page table.
    */

    if (!Entry->PtBuilder) {
        CpuPCIB->lm_pt = 0;
        return FALSE;
    }

    /* Map the APIC into the address space (uncached) */
    IdentityPtMap(Entry->PtBuilder, Ctx->LapicAddress, 1,
                    TAG_ACCESS_RW | TAG_CACHE_UC, 0);

    /* Map the client communication page */
    IdentityPtMap(Entry->PtBuilder, Cpu->CoreApPage, 1,
                    TAG_ACCESS_RW | TAG_CACHE_WB, 0);

    /* Map the PCIB */
    IdentityPtMap(Entry->PtBuilder, Cpu->CorePcibPage, 1,
                    TAG_ACCESS_RW | TAG_CACHE_WB, 0);

    /* Map the private block */
    IdentityPtMap(Entry->PtBuilder, Cpu->CorePrivBlock,
                  Cpu->CorePrivBlockPages, TAG_ACCESS_RW | TAG_CACHE_WB, 0);

    /*
    * Map the ApBootCode area as well (as RO)
    * A small portion of the area is used for variable storage,
    * for the early stages of the AP boot process.
    * Once the target CPU has left real mode, and is using page tables,
    * it is no longer safe to write to the scratch area of the
    * boot code page as doing so will trigger a page fault (and halt
    * the target CPU).
    */
    IdentityPtMap(Entry->PtBuilder, Ctx->ApBootCodePage, 1,
                    TAG_ACCESS_RO | TAG_CACHE_WB, 0);

    /* Map the trigger page IF and ONLY IF we are in a trigger mode */
    if (Entry->TriggerMode != CoreTriggerImmediate) {
        /*
        * The AP will need read only access,
        * only the main processor should be resetting the trigger */
        IdentityPtMap(Entry->PtBuilder, Ctx->ApTriggerPage, 1,
                        TAG_ACCESS_RO | TAG_CACHE_WB, 0);
    }

    PtSize = IdentityPtGetPtSize(Entry->PtBuilder, EFI_PAGE_SIZE);

    if ((PtSize / EFI_PAGE_SIZE) > Cpu->PageTablePages) {
        /* Free the current Page table area */

        if (Cpu->PageTablePages > 0)
            gBS->FreePages(Cpu->CorePageTable, Cpu->PageTablePages);

        /* Allocate another set, keep it in the lower 4GiB */
        Cpu->CorePageTable = BASE_4GB - 1;

        status = gBS->AllocatePages(AllocateMaxAddress, EfiLoaderData,
                                    PtSize / EFI_PAGE_SIZE,
                                    &Cpu->CorePageTable);

        Handled = (status == EFI_SUCCESS) ? TRUE : FALSE;

        if (Handled) {
            /* Zero out the page table memory */
            SetMem(
                (VOID *)Cpu->CorePageTable,
                EFI_PAGES_TO_SIZE(Cpu->PageTablePages),
                0
            );
            Cpu->PageTablePages = PtSize / EFI_PAGE_SIZE;
        } else {
            Cpu->CorePageTable = 0;
            Cpu->PageTablePages = 0;
        }
    }

    if (Handled) {
        /* Now write the resulting page table to memory */
        IdentityPtWritePt(Entry->PtBuilder, (VOID *)Cpu->CorePageTable);

        /* Instruct bootstrapper to use this set of page tables */
        CpuPCIB->lm_pt = Cpu->CorePageTable;
    }

    return Handled;
}

static VOID VmPerfEvalHandleStacks(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN CORE_ENTRY_INFO *Entry,
    IN VM_PERF_EVAL_ENUM_CPU *Cpu,
    IN volatile PCIB *CpuPCIB
)
{
    ApStackConfig StackConfig, CoreStackConfig;
    UINT32 IndividualExceptionStackSize = 0;

    /* Handle the stacks associated with this core */

    if (Entry->EntryType == CoreEntryRealMode) {
        /*
        * Real mode code is expected to use the real mode
        * work area as stack space if needed.
        */
        CpuPCIB->lm_excpstack =
        CpuPCIB->lm_nmistack =
        CpuPCIB->lm_intstack =
        CpuPCIB->lm_stack = 0;
    } else {
        /*
        * Calculate the stack area sizes as needed and if needed
        * tag the memory area in the page table.
        */
        VmPerfEvalCalcStackConfig(Cpu->CoreStackPages, &CoreStackConfig);

        CpuPCIB->lm_stack = Cpu->CoreStack + CoreStackConfig.InitialStackOffset;

        /*
        * Now calculate the same for the exception stacks
        */
        IndividualExceptionStackSize = Cpu->CoreInterruptStackPages / 3;
        VmPerfEvalCalcStackConfig(IndividualExceptionStackSize, &StackConfig);

        CpuPCIB->lm_excpstack = Cpu->CoreExceptionStack +
                                StackConfig.InitialStackOffset;

        CpuPCIB->lm_intstack =  Cpu->CoreInterruptStack +
                                StackConfig.InitialStackOffset;

        CpuPCIB->lm_nmistack =  Cpu->CoreNMIStack +
                                StackConfig.InitialStackOffset;

        /* Map all the stacks if we have an available PtBuilder object */
        if (Entry->PtBuilder) {

            /* Map the main stack */
            IdentityPtMap(Entry->PtBuilder,
                          Cpu->CoreStack +
                          CoreStackConfig.BaseOffsetForMapping,
                          CoreStackConfig.StackPages,
                          TAG_ACCESS_RW | TAG_CACHE_WB, 0);

            /* Map the Exception/NMI/Interrupt stacks */
            IdentityPtMap(Entry->PtBuilder,
                          Cpu->CoreExceptionStack +
                          StackConfig.BaseOffsetForMapping,
                          StackConfig.StackPages,
                          TAG_ACCESS_RW | TAG_CACHE_WB, 0);

            IdentityPtMap(Entry->PtBuilder,
                          Cpu->CoreInterruptStack +
                          StackConfig.BaseOffsetForMapping,
                          StackConfig.StackPages,
                          TAG_ACCESS_RW | TAG_CACHE_WB, 0);

            IdentityPtMap(Entry->PtBuilder,
                          Cpu->CoreNMIStack +
                          StackConfig.BaseOffsetForMapping,
                          StackConfig.StackPages,
                          TAG_ACCESS_RW | TAG_CACHE_WB, 0);
        }
    }
}


BOOLEAN VmPerfEvalBootCore(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index,
    CORE_ENTRY_INFO *Entry
)
{
    BOOLEAN CpuBooted = FALSE;
    VM_PERF_EVAL_ENUM_CPU *enumCpu;
    volatile PCIB *CpuPCIB;
    volatile UINT32 *ApBootCodePatch = (UINT32 *)Ctx->ApBootCodePage;
    UINT32 PollLoopCount;

    if (Index >= Ctx->NumAcpiCores)
        return FALSE;

    enumCpu = &Ctx->AcpiCores[Index];

    /* If core is not in service or core is running, fail this call */
    if (!enumCpu->InService || enumCpu->CoreIsRunning)
        return FALSE;

    /*
    * A PtBuilder is required if we are operating in the following modes:
    * 1) CoreEntryProtectedModeNormalPaging
    * 2) CoreEntryProtectedModePAEPaging
    * 3) CoreEntryLongMode
    */
    switch (Entry->EntryType) {
        case CoreEntryProtectedModeNormalPaging:
        case CoreEntryProtectedModePAEPaging:
        case CoreEntryLongMode:
            if (Entry->PtBuilder == NULL)
                return FALSE;
            break;

        default:
            break;
    }

    CpuPCIB = (volatile PCIB *)enumCpu->CorePcibPage;

    /*
    * The stack handling function MAY add stack mappings
    * in the page table, so handle this first before calling the code
    * which handles page tables.
    */
    VmPerfEvalHandleStacks(Ctx, Entry, enumCpu, CpuPCIB);
    VmPerfEvalHandlePageTables(Ctx, Entry, enumCpu, CpuPCIB);

    /* Select the entry mode and the entry point */
    switch (Entry->EntryType) {
        case CoreEntryRealMode:
            CpuPCIB->entry_type = ENTRY_REAL_MODE;
            CpuPCIB->entry_point = (Entry->EntryPoint & 0xFFFF) |
                                   ((Entry->EntryPoint >> 16) & 0xFFFF) << 16;
            break;

        case CoreEntryProtectedModeNoPaging:
            CpuPCIB->entry_type = ENTRY_PM_NO_PAGING;
            CpuPCIB->entry_point = (UINT32)(Entry->EntryPoint);
            break;

        case CoreEntryProtectedModeNormalPaging:
            CpuPCIB->entry_type = ENTRY_PM_PAGING;
            CpuPCIB->entry_point = (UINT32)(Entry->EntryPoint);
            break;

        case CoreEntryProtectedModePAEPaging:
            CpuPCIB->entry_type = ENTRY_PM_PAE_PAGING;
            CpuPCIB->entry_point = (UINT32)(Entry->EntryPoint);
            break;

        case CoreEntryLongMode:
            CpuPCIB->entry_type = ENTRY_LONG_MODE;
            CpuPCIB->entry_point = Entry->EntryPoint;
            break;
    }

    /* Set up the pointer to the private block and other areas */
    CpuPCIB->priv_block = enumCpu->CorePrivBlock;
    CpuPCIB->ap_buffer = enumCpu->CoreApPage;
    CpuPCIB->ap_size = EFI_PAGE_SIZE;
    CpuPCIB->apic_base = Ctx->LapicAddress;

    /* Set the trigger address based on the trigger mode */
    if (Entry->TriggerMode == CoreTriggerImmediate) {
        CpuPCIB->trigger_addr = 0;
    } else {
        CpuPCIB->trigger_addr = (UINT32)(Ctx->ApTriggerPage);
    }

    /* Patch the necessary data into the ApBootCode parameter area */
    ApBootCodePatch[1023] = Ctx->ApBootCodePage;
    ApBootCodePatch[1022] = enumCpu->CorePcibPage;
    ApBootCodePatch[1021] = Ctx->ApRealModeWorkPage;

    /* Send a 'start-up' command to the target CPU */
    VmPerfEvalApicSendSIpi(
        Ctx->LapicAddress,
        enumCpu->ApicId,
        (UINT32)Ctx->ApBootCodePage);

    /*
    * Spin until:
    * 1. Statuscode is either TRIGGER_WAIT, IN_ENTRY_POINT,
    *    SUCCESSFUL_RETURN or EXCEPTION
    * 2. A certain amount of time has elapsed (~30ms).
    */
    PollLoopCount = 30;
    while (PollLoopCount > 0) {
        UINT32 StatusCode = CpuPCIB->statuscode;

        StatusCode &= STATUSCODE_MASK;
        switch (StatusCode) {
            case STATUSCODE_IN_ENTRY_POINT:
            case STATUSCODE_EXCEPTION_LONG_MODE:
            case STATUSCODE_EXCEPTION_PROTECTED_MODE:
            case STATUSCODE_EXCEPTION_REAL_MODE:
            case STATUSCODE_SUCCESSFUL_RETURN:
            case STATUSCODE_TRIGGER_WAIT:
                /*
                * Any of the above status codes indicates that the
                * core has started up
                */
                enumCpu->CoreIsRunning = TRUE;
                CpuBooted = TRUE;
                break;
        }

        if (CpuBooted)
            break;

        /* Stall this CPU for 1ms */
        gBS->Stall(1000);
        PollLoopCount--;
    }

    return CpuBooted;
}

UINT64 VmPerfEvalGetReturnFromPCIB(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
)
{
    VM_PERF_EVAL_ENUM_CPU *enumCpu;
    volatile PCIB *CpuPCIB;
    UINT64 ReturnValue = 0;

    if (Index >= Ctx->NumAcpiCores)
        return 0;

    enumCpu = &Ctx->AcpiCores[Index];

    CpuPCIB = (volatile PCIB *)enumCpu->CorePcibPage;
    if (CpuPCIB) {
        ReturnValue = CpuPCIB->returnvalue;
    }

    return ReturnValue;
}
