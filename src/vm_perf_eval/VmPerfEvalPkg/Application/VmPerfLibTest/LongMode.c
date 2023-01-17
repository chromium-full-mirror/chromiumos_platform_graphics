/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <VmPerfEvalLib.h>
#include <IdentityPtLib.h>
#include "Common.h"
#include "LongModeAp.h"

IDENTITY_PT_BUILDER LongModeBuilder;


/*
* Number of us to wait after a core has been started
* on the fill test.
*/
#define FILL_STALL_US       1000000

/* Some random values chosen for this */
#define INPUT0_VALUE        0xAFFFFE
#define INPUT1_BIAS         0xFDAE


/**
 * This test ensures that remote processors can be operated in long mode.
 * This set of tests is structured very similar to the protected mode tests,
 * however since there is only a single mode of paging that we are interested
 * in within long mode, there are fewer permutations.
 *
 * Two variants:
 * 1. A basic boot+return value check
 * 2. A fill test with aggregation mask variants.
 *
 * The aggregation mask is a mask passed into the page table builder.
 * Each bit represents each level within a page table. A set bit on a
 * particular level will allow the page table builder to combine multiple
 * smaller translation units into a single larger translation unit. This will
 * be done provided other criteria is met (such as alignment and map size).
 *
 * For example, for x86-64 if the conditions are correct, 512 4K pages for
 * a mapping can be combined into a single 2M page.
 *
 * The test will loop through all possible aggregation mask combinations and
 * ensure that the filled buffer contains the correct values after the test.
 *
 * If the test passes, we can assume that the page table was built correctly
 * for each variant of the aggregation mask.
*/
BOOLEAN
LongModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize)
{
    UINT32 TargetCpu;
    UINT32 CurrentCpu;
    UINT64 CpuEnumerationMask;
    UINT64 ReturnedMask;
    UINT64 TagSize;
    EFI_PHYSICAL_ADDRESS LongModeTagBuffer;
    EFI_PHYSICAL_ADDRESS LongModeFillBuffer;
    EFI_STATUS Status;
    VOID *ApPage;
    CORE_ENTRY_INFO EntryInfo;
    UINT32 SystemPaBits = IdentityPtGetSystemPaBits();
    UINT64 MultiplyResult;
    UINT32 i;
    UINT32 CurrentAggregationMask;

    TargetCpu = __builtin_ffsll(AvailableCoresMask) - 1;
    TagSize = IdentityPtInit(&LongModeBuilder, &g_Pml4Desc,
                             SystemPaBits,
                             NULL
    );

    /* Bring the cores into service */
    /**
     * The AvailableCoresMask is a mask that represents the available
     * remote cores in the machine. When we put a core in service, we can
     * only bring up one core individually or all of them.
     *
     * It is possible to not want to bring up all cores
     * (P-core v E-core for example).
     *
     * What we are doing here is we are scanning through the core mask
     * and bringing up each core individually.
     *
     * Once the API call returns, we check to see if it was actually brought
     * up correctly.
     *
     * builtin_ffsll finds the first set bit starting from bit 0.
     * This function treats bit 0 (the very first bit) as bit 1, so we
     * subtract 1 from it.
     *
     * We use the return value to remove/clear the bit from the
     * enumeration/scanning mask and to bring the core represented
     * by that bit into service.
    */
    CpuEnumerationMask = AvailableCoresMask;
    while (CpuEnumerationMask) {
        CurrentCpu = __builtin_ffsll(CpuEnumerationMask) - 1;
        CpuEnumerationMask &= (~(1ULL << CurrentCpu));

        ReturnedMask = VmPerfPutInService(Ctx, CoreTypeIndividual, CurrentCpu);
        if (BitFieldCountOnes64(ReturnedMask, 0, 63) != 1) {
            return FALSE;
        }
    }

    /* Allocate tag memory for the page table builder */
    Status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES(TagSize),
        &LongModeTagBuffer
    );

    if (Status != EFI_SUCCESS) {
        VmPerfLog(Ctx, 0, L"Failed to allocate tag memory buffer\n");
        return FALSE;
    }

    TagSize = IdentityPtInit(&LongModeBuilder, &g_Pml4Desc,
                             SystemPaBits, (VOID *)LongModeTagBuffer);

    /* Fire up the cores will the basic multicore boot test */
    CpuEnumerationMask = AvailableCoresMask;
    while (CpuEnumerationMask) {
        CurrentCpu = __builtin_ffsll(CpuEnumerationMask) - 1;
        CpuEnumerationMask &= (~(1ULL << CurrentCpu));

        /* Map our program image into memory */
        IdentityPtMap(
            &LongModeBuilder,
            (UINT64)LoadedImageBase,
            EFI_SIZE_TO_PAGES(LoadedImageSize),
            TAG_CACHE_WB | TAG_ACCESS_RO,
            0
        );

        ApPage = VmPerfGetApPage(Ctx, CurrentCpu);

        /* Write the data for this AP */
        volatile LONG_MODE_AP_MULTIPLY *ApMultiply =
        (volatile LONG_MODE_AP_MULTIPLY *)ApPage;

        /* Just some random numbers */
        ApMultiply->Input0 = INPUT0_VALUE;
        ApMultiply->Input1 = (CurrentCpu + INPUT1_BIAS);

        /* Fire off the core */
        EntryInfo.EntryPoint = (EFI_PHYSICAL_ADDRESS)ApLongModeEntryMultiply;
        EntryInfo.EntryType = CoreEntryLongMode;
        EntryInfo.PtBuilder = &LongModeBuilder;
        EntryInfo.TriggerMode = CoreTriggerImmediate;

        if (!VmPerfStartCore(Ctx, CurrentCpu, &EntryInfo)) {
            VmPerfLog(Ctx, 0, L"Startup failure on CPU %u\n", CurrentCpu);
            return FALSE;
        }

        IdentityPtReset(&LongModeBuilder);
    }

    /* Verify the results */
    CpuEnumerationMask = AvailableCoresMask;
    while (CpuEnumerationMask) {
        CurrentCpu = __builtin_ffsll(CpuEnumerationMask) - 1;
        CpuEnumerationMask &= (~(1ULL << CurrentCpu));

        if (!IsCoreFinished(Ctx, SystemTable, CurrentCpu)) {
            return FALSE;
        }

        MultiplyResult = MultU64x64(
            INPUT0_VALUE,
            (CurrentCpu + INPUT1_BIAS)
        );

        ApPage = VmPerfGetApPage(Ctx, CurrentCpu);

        /* Write the data for this AP */
        volatile LONG_MODE_AP_MULTIPLY *ApMultiply =
        (volatile LONG_MODE_AP_MULTIPLY *)ApPage;

        if (ApMultiply->Output != MultiplyResult ||
            VmPerfGetReturnValue(Ctx, CurrentCpu) != 1) {
                /* Fail this CPU */
                VmPerfLog(Ctx, 0,
                    L"Verification failure on CPU %u\n", CurrentCpu);
                VmPerfLog(Ctx, 0,
                    L"Result = 0x%016lX\n", ApMultiply->Output);
                VmPerfLog(Ctx, 0,
                    L"MultiplyResult = 0x%016lX\n", MultiplyResult);
                return FALSE;
        }
    }

    /* Run the fill test now */
    VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

    /* Allocate a buffer of 1GiB in size */
    Status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, EFI_SIZE_TO_PAGES(BASE_1GB),
        &LongModeFillBuffer
    );

    if (Status != EFI_SUCCESS) {
        SystemTable->BootServices->FreePages(
            LongModeTagBuffer,
            EFI_SIZE_TO_PAGES(TagSize)
        );

        return FALSE;
    }

    /* Loop through all possible aggregation masks */
    for (CurrentAggregationMask = 0;
         CurrentAggregationMask < (1 << g_Pml4Desc.NumLevels);
         CurrentAggregationMask++
    )
    {
        ReturnedMask = VmPerfPutInService(Ctx, CoreTypeIndividual, TargetCpu);
        if (BitFieldCountOnes64(ReturnedMask, 0, 63) != 1) {
            return FALSE;
        }

        ApPage = VmPerfGetApPage(Ctx, TargetCpu);
        volatile LONG_MODE_AP_FILL *LongModeFill =
        (volatile LONG_MODE_AP_FILL *)ApPage;

        LongModeFill->FillBuffer = (VOID *)LongModeFillBuffer;
        LongModeFill->NumU32s = BASE_1GB / sizeof(UINT32);


        /* Map our program image into memory */
        IdentityPtMap(
            &LongModeBuilder,
            (UINT64)LoadedImageBase,
            EFI_SIZE_TO_PAGES(LoadedImageSize),
            TAG_CACHE_WB | TAG_ACCESS_RO,
            0
        );

        IdentityPtMap(
            &LongModeBuilder,
            (UINT64)LongModeFillBuffer,
            EFI_SIZE_TO_PAGES(BASE_1GB),
            TAG_CACHE_WB | TAG_ACCESS_RW,
            CurrentAggregationMask
        );

        /* Start the core */
        EntryInfo.EntryPoint = (EFI_PHYSICAL_ADDRESS)ApLongModeEntryLongFill;
        EntryInfo.EntryType = CoreEntryLongMode;
        EntryInfo.PtBuilder = &LongModeBuilder;
        EntryInfo.TriggerMode = CoreTriggerImmediate;

        if (!VmPerfStartCore(Ctx, TargetCpu, &EntryInfo)) {
            return FALSE;
        }

        VmPerfLog(Ctx, 0, L".");

        /* Stall for 1 s, as this buffer is 1GiB */
        SystemTable->BootServices->Stall(FILL_STALL_US);

        if (!IsCoreFinished(Ctx, SystemTable, TargetCpu)) {
            return FALSE;
        }

        /* Verify the results */
        if (VmPerfGetReturnValue(Ctx, TargetCpu) != 1) {
            return FALSE;
        }

        UINT32 *FillBufferU32 = (UINT32 *)LongModeFillBuffer;
        for (i = 0; i < (BASE_1GB / sizeof(UINT32)); i++ ) {
            if (FillBufferU32[i] != i) {
                return FALSE;
            }
        }

        /* Reset the page table builder for the next iteration */
        IdentityPtReset(&LongModeBuilder);

        /* Remove the CPU from service */
        VmPerfRemoveFromService(Ctx, CoreTypeIndividual, TargetCpu);
    }

    VmPerfLog(Ctx, 0, L"\n");
    return TRUE;
}
