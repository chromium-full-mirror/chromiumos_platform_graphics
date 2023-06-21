/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

/**
 * This file defines some KVM interface functions.
 */
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <VmPerfEvalLib.h>
#include <Register/Intel/Cpuid.h>
#include "VmPerfApicTimerApCommon.h"

/*
* This define has been copied from the Linux KVM kernel code
* A list of KVM specific MSR's can be found within the kernel source
* tree.
*
* See Documentation/virt/kvm/msr.txt
*/
#define MSR_KVM_POLL_CONTROL    0x4b564d05

BOOLEAN
EFIAPI
VmPerfApicTimerDetectKVM(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    VM_PERF_CPU_INFORMATION *CpuInfo = &Ctx->CpuInformation;

    if (CpuInfo->Virtualized && CpuInfo->RunningUnderKvm) {
        VmPerfLog(Ctx, 0, L"Linux KVM Hypervisor detected.\n");
        if (CpuInfo->KvmHaltPollControl) {
            VmPerfLog(Ctx, 0, L"Halt Poll Control MSR supported.\n");
        }
        return TRUE;
    }

    return FALSE;
}

VOID
EFIAPI
VmPerfApSetHaltPollMode(
    BOOLEAN HaltPollEnabled
)
{
    AsmWriteMsr64(MSR_KVM_POLL_CONTROL, HaltPollEnabled ? 1 : 0);
}
