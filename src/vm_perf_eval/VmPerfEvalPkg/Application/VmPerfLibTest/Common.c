/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <VmPerfEvalLib.h>

/* In units of 10 ms */
#define PROBE_ITERATIONS            15

BOOLEAN
WaitForTargetCpuState(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    VM_PERF_CORE_STATUS TargetStatus
)
{
    BOOLEAN InDesiredStatus = FALSE;
    VM_PERF_CORE_STATUS TargetCpuStatus;
    UINT32 ProbeCount = PROBE_ITERATIONS;

    /*
    * Probe the current state of the CPU,
    * if the CPU doesn't enter the desired
    * in the alloted time, return FALSE
    */
    TargetCpuStatus = VmPerfProbeInService(Ctx, TargetCpu);

    while (TargetCpuStatus != TargetStatus &&
        ProbeCount > 0) {
        TargetCpuStatus = VmPerfProbeInService(Ctx, TargetCpu);

        /* 10 ms */
        SystemTable->BootServices->Stall(10000);
        ProbeCount--;
    }

    if (TargetCpuStatus == TargetStatus) {
        /* We must be in this state ! */
        InDesiredStatus = TRUE;
    }

    return InDesiredStatus;
}

BOOLEAN
IsCoreFinished(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu
)
{
    return WaitForTargetCpuState(
        Ctx, SystemTable,
        TargetCpu, CoreStatusFinished);
}

BOOLEAN
IsCoreInTriggerWait(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu
)
{
    return WaitForTargetCpuState(
        Ctx, SystemTable,
        TargetCpu, CoreStatusNotTriggered);
}
