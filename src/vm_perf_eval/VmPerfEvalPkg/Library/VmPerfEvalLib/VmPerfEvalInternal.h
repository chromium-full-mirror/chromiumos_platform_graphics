/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>

#define         VMPERF_LOG_FILE_NAME                    L"Log.txt"
/*
* Total number of stack pages (including guard memory)
* Half will go to guard pages and half will be used
* for actual stack space.
*/
#define         VMPERF_CORE_STACK_PAGES                 (64)

/*
* Defines the interrupt stack block. This is a combined
* interrupt/NMI/exception stack. Must be at least 9 pages.
* Should be a multiple of 3.
*/
#define         VMPERF_CORE_INTERRUPT_STACK_PAGES       (9)

/*
* Defines the number of pages allocated for each cores private storage
* 32KiB is currently a safe number, but ensure that there is enough
* space for all the defines in the AP bootstrapper.
**/
#define         VMPERF_CORE_PRIVATE_PAGES               (8)

#define RELEASE_PAGE(x)\
if (x) {\
gBS->FreePages(x, 1);\
x = 0;\
}

#define RELEASE_PAGES(x, cnt)\
if (x) {\
gBS->FreePages(x, cnt);\
cnt = 0;\
}


UINT32 VmPerfEvalScanCpus(
    IN VM_PERF_EVAL_CTX *Ctx
);

BOOLEAN VmPerfEvalPutCpuInService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

VOID VmPerfEvalShutdownCpu(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

BOOLEAN VmPerfEvalRemoveCpuFromService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

BOOLEAN VmPerfEvalBootCore(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index,
    IN CORE_ENTRY_INFO *Entry
);

/*
* A core should already be in-service in order
* to make this call.
*/
VM_PERF_CORE_STATUS VmPerfEvalProbeStatus(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_EVAL_ENUM_CPU *Cpu
);

UINT64 VmPerfEvalGetReturnFromPCIB(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

BOOLEAN VmPerfLoggingInit(
    IN VM_PERF_EVAL_CTX *Ctx
);

VOID VmPerfLoggingShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
);

BOOLEAN VmPerfRawDiskInit(
    IN VM_PERF_EVAL_CTX *Ctx
);

VOID VmPerfRawDiskShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
);
