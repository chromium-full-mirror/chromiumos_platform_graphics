/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>

BOOLEAN
WaitForTargetCpuState(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    VM_PERF_CORE_STATUS TargetStatus
);

BOOLEAN
IsCoreFinished(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu
);

BOOLEAN
IsCoreInTriggerWait(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu
);
