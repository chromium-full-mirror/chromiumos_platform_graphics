/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/
#include "LongModeAp.h"


UINTN EFIAPI ApLongModeEntryLongFill(
    LONG_MODE_AP_FILL *Fill
)
{
    UINT32 *Buffer = (UINT32 *)Fill->FillBuffer;
    UINT32 i;

    for (i = 0; i < Fill->NumU32s; i++) {
        Buffer[i] = i;
    }

    return 1;
}

UINTN EFIAPI ApLongModeEntryMultiply(
    LONG_MODE_AP_MULTIPLY *Multiply
)
{
    UINT64 Inp0 = Multiply->Input0;
    UINT64 Inp1 = Multiply->Input1;

    Multiply->Output = Inp0 * Inp1;

    return 1;
}
