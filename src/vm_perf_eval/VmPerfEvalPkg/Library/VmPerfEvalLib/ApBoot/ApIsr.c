/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <ApLib.h>
#include "ApBoot.h"

STATIC VOID *GetThisPrivate(UINT32 PrivBlockOffset) {
    UINT32 priv_block_u32;
    UINT64 priv_block_u64;

    THIS_CPU_GET_PCIB(priv_block_u32, priv_block);

    /* Assume a zero extend here, that is what we want */
    priv_block_u64 = priv_block_u32 + PrivBlockOffset;
    return (VOID *)priv_block_u64;
}


VOID EFIAPI ApPatchIsr(UINT32 Vector, IsrEntry IsrPointer)
{
    UINT64 *Private = GetThisPrivate(PRIV_INTJMP);

    if (Vector < 256) {
        Private[Vector] = (UINT64)IsrPointer;
    }
}

VOID EFIAPI ApRemoveIsr(UINT32 Vector)
{
    UINT64 *Private = GetThisPrivate(PRIV_INTJMP);
    if (Vector < 256) {
        Private[Vector] = 0;
    }
}
