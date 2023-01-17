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
#include "Common.h"

/* Expected return value from the AP operating in real mode */
#define TARGET_RETURN_CODE          0x1234

/*
*  Source:
*   BITS 16
*   mov ax, 1234h
*   retf
*
*  Copy to a text file (eg realmode.asm)
*  Run nasm realmode.asm
*  Use hexdump -C realmode to get byte array
*/
static const UINT8 RealModeCode[] = {
    0xB8, 0x34, 0x12,               /* mov ax, 1234h */
    0xCB                            /* retf */
};

static
BOOLEAN
RealModeExecutionTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    UINT64 RealModeCodeBuffer,
    BOOLEAN UseTrigger
)
{
    UINT64 InServiceMask;
    UINT64 TargetCpuReturnValue;
    BOOLEAN TestPassed = FALSE;
    CORE_ENTRY_INFO EntryInfo;

    InServiceMask = VmPerfPutInService(Ctx, CoreTypeIndividual, TargetCpu);

    if (BitFieldCountOnes64(InServiceMask, 0,63) != 1) {
        /* Core could not be brought into service */
        VmPerfLog(Ctx, 0, L"Core In Service Failure\n");
        return FALSE;
    }

    /* Start the core up */
    EntryInfo.EntryType = CoreEntryRealMode;
    EntryInfo.PtBuilder = NULL;
    EntryInfo.TriggerMode = (UseTrigger) ?  CoreTriggerWait :
                                            CoreTriggerImmediate;
    EntryInfo.EntryPoint = VmPerfMakeRealModeEntryPoint(RealModeCodeBuffer);

    if (!VmPerfStartCore(Ctx, TargetCpu, &EntryInfo)) {
        /* Core could not be started */
        VmPerfLog(Ctx, 0, L"Core Start Failure\n");
        return FALSE;
    }

    if (UseTrigger) {
        /* Wait for processor to enter wait for trigger state */
        if (!IsCoreInTriggerWait(Ctx, SystemTable, TargetCpu))
            return FALSE;

        /* Set trigger word */
        VmPerfTrigger(Ctx, TRUE);
    }

    /* Check the return value and ensure target CPU has completed */
    if (IsCoreFinished(Ctx, SystemTable, TargetCpu)) {
        TargetCpuReturnValue = VmPerfGetReturnValue(Ctx, TargetCpu);
        TestPassed = (TargetCpuReturnValue == TARGET_RETURN_CODE);
    }

    return TestPassed;
}

/**
 * This code tests that the remote cores (APs) can be booted
 * successfully in real mode (16-bit).
 * See Intel Software Developers Manual Chapter 3 for an intro.
 *
 * The AP starts executing code in this mode when kicked off.
 * This mode is very limiting so there isn't a lot of test code here.
 *
 * The test tests both non-trigger start and trigger start. The trigger
 * start will ensure that the core actually enters the state in which
 * it is waiting for the trigger word to be written by the main core.
 *
 * The test will execute a very small piece of code which will return
 * a constant value.
 * We will check that this value successfully makes its way to
 * VmPerfGetReturnValue. This function should be called only after the core
 * has finished, see IsCoreFinished for details on how that is determined.
 */
BOOLEAN
RealModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize)
{
    BOOLEAN TestPassed = FALSE;
    EFI_PHYSICAL_ADDRESS RealModeCodeBuffer;
    EFI_STATUS Status;
    UINT32 TargetCpu;

    /*
    * We can only use a single core in this mode
    * It is assumed that all cores are parked
    */
    RealModeCodeBuffer = BASE_1MB - 1;

    Status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 1, &RealModeCodeBuffer);

    if (Status != EFI_SUCCESS) {
        /* Could not allocate memory for the code buffer */
        VmPerfLog(Ctx, 0, L"Failed to alloc memory\n");
        return FALSE;
    }

    /* Copy the test code into an appropriate place in memory */
    SystemTable->BootServices->CopyMem(
        (VOID *)RealModeCodeBuffer,
        (VOID *)RealModeCode, sizeof(RealModeCode));

    /* Find the first bit set as a way to select a target CPU */
    TargetCpu = __builtin_ffsll(AvailableCoresMask) - 1;

    TestPassed = TRUE;

    if (!RealModeExecutionTest(
            Ctx, SystemTable, TargetCpu, RealModeCodeBuffer, FALSE)) {
        VmPerfLog(Ctx, 0, L"Non-trigger Real Mode Test failed!\n");
        TestPassed = FALSE;
    }

    /* Park/reset the core and try the next test */
    VmPerfRemoveFromService(Ctx, CoreTypeIndividual, TargetCpu);

    if (!RealModeExecutionTest(
            Ctx, SystemTable, TargetCpu, RealModeCodeBuffer, TRUE)) {
        VmPerfLog(Ctx, 0, L"Trigger Real Mode Test failed!\n");
        TestPassed = FALSE;
    }

    /* Park all the cores in the system (used as a catch all) */
    VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

    /* Clear the trigger page */
    VmPerfTrigger(Ctx, FALSE);

    /* Release the code buffer memory */
    SystemTable->BootServices->FreePages(RealModeCodeBuffer, 1);
    return TestPassed;
}
