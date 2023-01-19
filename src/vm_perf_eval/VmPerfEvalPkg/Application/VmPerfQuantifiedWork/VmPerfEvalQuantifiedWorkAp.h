/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
#include <Uefi.h>

typedef struct {
    VOID    *WorkPage;
    BOOLEAN CanReadAperfMperf;
    UINT64  TscTicksPerReport;
    UINT64  CurrentIterationCount;
    UINT64  EntryPointTsc;
    UINT64  APerf;
    UINT64  MPerf;
}   VM_PERF_QUANTIFIED_WORK_AP;

INTN
EFIAPI
QuantifiedWorkApFunc(
    VM_PERF_QUANTIFIED_WORK_AP *Structure
);

VOID
EFIAPI
IncrementUint32sInPage(
    UINT32 *Page
);
