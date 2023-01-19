/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include "VmPerfEvalQuantifiedWorkAp.h"
#include <Register/Intel/ArchitecturalMsr.h>

VOID
EFIAPI
IncrementUint32sInPage(
    UINT32 *Page
)
{
    UINT32 i;

    /* Simply increment each DWORD in the page */
    for (i = 0; i < EFI_PAGE_SIZE/sizeof(UINT32); i++) {
        Page[i]++;
    }

    return;
}

VOID
EFIAPI
ClearPage(
    UINT32 *Page
)
{
    UINT32 i;

    for (i = 0 ; i < EFI_PAGE_SIZE/sizeof(UINT32); i++) {
        Page[i] = 0;
    }

    return;
}

INTN
EFIAPI
QuantifiedWorkApFunc(
    VM_PERF_QUANTIFIED_WORK_AP *Structure
)
{
    Structure->EntryPointTsc = AsmReadTsc();
    UINT64 TscReport = AsmReadTsc() + Structure->TscTicksPerReport;
    UINT64 IterationCount = 0;
    UINT32 *Page = (UINT32 *)(Structure->WorkPage);
    UINT64 CurrentTsc;
    UINT64 CurrentMPerf = 0;
    UINT64 CurrentAPerf = 0;
    BOOLEAN CanUseAperf = FALSE;

    /* Let's clear the page on the AP */
    ClearPage(Page);
    CanUseAperf = Structure->CanReadAperfMperf;

    while (1) {
        IncrementUint32sInPage(Page);
        IterationCount++;
        CurrentTsc = AsmReadTsc();

        if (CurrentTsc >= TscReport) {
            /* Calculate new TSC */
            TscReport = CurrentTsc + Structure->TscTicksPerReport;
            Structure->CurrentIterationCount = IterationCount;

            /* Read the MSR, we can't read these in a virtual env */
            if (CanUseAperf) {
                CurrentAPerf = AsmReadMsr64(MSR_IA32_APERF) - CurrentAPerf;
                CurrentMPerf = AsmReadMsr64(MSR_IA32_MPERF) - CurrentMPerf;
            }

            Structure->APerf = CurrentAPerf;
            Structure->MPerf = CurrentMPerf;
        }
    }

    return 0;
}
