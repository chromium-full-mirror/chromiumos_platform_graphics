/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalApicLib.h>
#include <Register/Intel/LocalApic.h>
#include <Library/BaseLib.h>

static VOID VmPerfEvalProgramIcr(
    IN UINT64 LapicBase,
    IN LOCAL_APIC_ICR_HIGH LocalApicHigh,
    IN LOCAL_APIC_ICR_LOW LocalApicLow
)
{
    volatile UINT32 *ApicIcrLow =
        (volatile UINT32 *)(LapicBase + XAPIC_ICR_LOW_OFFSET);

    volatile UINT32 *ApicIcrHigh =
        (volatile UINT32 *)(LapicBase + XAPIC_ICR_HIGH_OFFSET);
    BOOLEAN CurrentInterruptState;

    /*
    * This library is to be shared with both the main processor
    * and any AP's. Functions which do not rely on firmware variables
    * in any way shape or form can be used in this code base.
    * The interrupt management functions below meet this criteria.
    */
    CurrentInterruptState = SaveAndDisableInterrupts();

    *ApicIcrHigh = LocalApicHigh.Uint32;
    *ApicIcrLow = LocalApicLow.Uint32;

    /* Wait for delivery to finish */
    while (1) {
        LocalApicLow.Uint32 = *ApicIcrLow;

        /* Set to 0 when delivery is finished */
        if (!LocalApicLow.Bits.DeliveryStatus)
            break;
    }

    /* Restore interrupt state here */
    SetInterruptState(CurrentInterruptState);
}

VOID EFIAPI VmPerfEvalApicSendSIpi(
    IN EFI_PHYSICAL_ADDRESS LapicBase,
    IN UINT32 ApicId,
    IN UINT32 StartAddress
)
{
    LOCAL_APIC_ICR_HIGH LocalApicHigh;
    LOCAL_APIC_ICR_LOW  LocalApicLow;


    /* Preclear the data before we fill out what is needed */
    LocalApicHigh.Uint32 = 0;
    LocalApicLow.Uint32 = 0;

    /* The StartAddress must be in the lower 1MiB AND be 4K aligned */
    LocalApicLow.Bits.DeliveryMode = LOCAL_APIC_DELIVERY_MODE_STARTUP;
    LocalApicLow.Bits.DestinationShorthand =
        LOCAL_APIC_DESTINATION_SHORTHAND_NO_SHORTHAND;
    LocalApicLow.Bits.Vector = (StartAddress & 0xFF000) >> 12;

    LocalApicHigh.Bits.Destination = ApicId;

    VmPerfEvalProgramIcr(LapicBase, LocalApicHigh, LocalApicLow);
}

VOID EFIAPI VmPerfEvalApicSendInit(
    IN EFI_PHYSICAL_ADDRESS LapicBase,
    IN UINT32 ApicId
)
{
    LOCAL_APIC_ICR_HIGH LocalApicHigh;
    LOCAL_APIC_ICR_LOW  LocalApicLow;


    /* Preclear the data before we fill out what is needed */
    LocalApicHigh.Uint32 = 0;
    LocalApicLow.Uint32 = 0;

    LocalApicLow.Bits.DeliveryMode = LOCAL_APIC_DELIVERY_MODE_INIT;
    LocalApicLow.Bits.DestinationShorthand =
        LOCAL_APIC_DESTINATION_SHORTHAND_NO_SHORTHAND;

    LocalApicHigh.Bits.Destination = ApicId;

    VmPerfEvalProgramIcr(LapicBase, LocalApicHigh, LocalApicLow);
}

UINT32 EFIAPI VmPerfEvalApicGetApicId(
    IN EFI_PHYSICAL_ADDRESS LapicBase
)
{
    volatile UINT32 *ApicIdRegister =
        (volatile UINT32 *)(LapicBase + XAPIC_ID_OFFSET);
    UINT32 ApicId;

    ApicId = *ApicIdRegister;

    /* The APIC ID for this processor is in the upper 8-bits of this DWORD */
    return ApicId >> 24;
}
