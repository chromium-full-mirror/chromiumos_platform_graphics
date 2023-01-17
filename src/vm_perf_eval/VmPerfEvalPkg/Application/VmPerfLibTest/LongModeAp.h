/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
#include <Uefi.h>


typedef struct
{
    VOID *FillBuffer;
    UINT32 NumU32s;
} LONG_MODE_AP_FILL;

typedef struct
{
    UINT32 Input0;
    UINT32 Input1;
    UINT64 Output;
} LONG_MODE_AP_MULTIPLY;

UINTN EFIAPI ApLongModeEntryLongFill(
    LONG_MODE_AP_FILL *Fill
);

UINTN EFIAPI ApLongModeEntryMultiply(
    LONG_MODE_AP_MULTIPLY *Multiply
);
