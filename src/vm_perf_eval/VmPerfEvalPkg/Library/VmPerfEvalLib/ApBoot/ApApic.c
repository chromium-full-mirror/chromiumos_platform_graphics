/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <ApLib.h>
#include "ApBoot.h"
#include <Register/Intel/LocalApic.h>
#include <Library/BaseLib.h>

#define IA32_TSC_DEADLINE_MSR           (0x6E0)

VOID EFIAPI
ApicSpuriousInterrupt(
    VOID *ApBuffer,
    UINT64 Tsc,
    UINT32 Vector
)
{
    /* Halt this CPU */
    while (1) {
        asm volatile("hlt");
    }
    return;
}

STATIC VOID ApApicWrite(UINT32 Offset, UINT32 Value) {
    UINT32 ApicBase;
    UINT64 ApicBaseU64;
    volatile UINT32 *Apic;

    THIS_CPU_GET_PCIB(ApicBase, apic_base);
    ApicBaseU64 = (ApicBase + Offset);

    Apic = (volatile UINT32 *)(ApicBaseU64);
    *Apic = Value;
}

STATIC UINT32 ApApicRead(UINT32 Offset) {
    UINT32 ApicBase;
    UINT64 ApicBaseU64;
    volatile UINT32 *Apic;

    THIS_CPU_GET_PCIB(ApicBase, apic_base);
    ApicBaseU64 = (ApicBase + Offset);

    Apic = (volatile UINT32 *)(ApicBaseU64);
    return *Apic;
}

VOID EFIAPI ApApicInit()
{
    LOCAL_APIC_SVR Svr;

    /* Install our spurious vector interrupt */
    ApPatchIsr(APIC_SPURIOUS_INT_VECTOR, ApicSpuriousInterrupt);

    /* Setup the spurious interrupt vector */
    Svr.Uint32 = 0;
    Svr.Bits.SpuriousVector = APIC_SPURIOUS_INT_VECTOR;

    /* APIC will not respond to programming if this is not set */
    Svr.Bits.SoftwareEnable = 1;

    ApApicWrite(XAPIC_SPURIOUS_VECTOR_OFFSET, Svr.Uint32);
}

VOID EFIAPI ApApicTimer(UINT32 InitCounter)
{
    ApApicWrite(XAPIC_TIMER_INIT_COUNT_OFFSET, InitCounter);
}

VOID EFIAPI ApApicTscDeadline(UINT64 TscDeadlineValue)
{
    /*
    * Writes to this MSR are not synchronized with other memory requests.
    * Add a memory sync here to ensure the MSR is written only after
    * all preceding memory requests have been fulfilled.
    */
    asm volatile("mfence; lfence" : : : "memory");
    AsmWriteMsr64(IA32_TSC_DEADLINE_MSR, TscDeadlineValue);
}

UINT32 EFIAPI ApApicVersion()
{
    return ApApicRead(XAPIC_VERSION_OFFSET);
}

UINT32 EFIAPI ApApicError()
{
    return ApApicRead(XAPIC_ID_OFFSET);
}

UINT32 EFIAPI ApApicTimerCurrent()
{
    return ApApicRead(XAPIC_TIMER_CURRENT_COUNT_OFFSET);
}

VOID EFIAPI ApApicSetMode(
    BOOLEAN TimerMasked,
    APIC_TIMER_MODE Mode,
    UINT32 Divide,
    UINT32 Vector
)
{
    LOCAL_APIC_LVT_TIMER LvtTimer;
    LOCAL_APIC_DCR ApicDcr;

    LvtTimer.Uint32 = 0;
    ApicDcr.Uint32 = 0;

    LvtTimer.Bits.Vector = Vector;
    LvtTimer.Bits.Mask = (TimerMasked) ? 1 : 0;

    /*
    * We have to use a bit of a hack here as the structure
    * we are using does not have the appropriate bit fields
    * necessary to set the TSC deadline mode.
    *
    * Attempting to use TSC deadline on a CPU that does not
    * support it will result in issues. The calling code
    * should ensure that it is supported before using it.
    */
    switch (Mode) {
        case APIC_TIMER_ONESHOT:
            LvtTimer.Bits.TimerMode = 0;
            break;

        case APIC_TIMER_PERIODIC:
            LvtTimer.Bits.TimerMode = 1;
            break;

        case APIC_TIMER_TSC_DEADLINE:
            LvtTimer.Bits.TimerMode = 0;
            LvtTimer.Uint32 |= (1 << 18);
            break;
    }

    /*
    * The APIC divisor is structured a bit weird in this register
    */
    switch (Divide) {
        case 2:
            ApicDcr.Bits.DivideValue1 = 0;
            ApicDcr.Bits.DivideValue2 = 0;
            break;

        case 4:
            ApicDcr.Bits.DivideValue1 = 1;
            ApicDcr.Bits.DivideValue2 = 0;
            break;

        case 8:
            ApicDcr.Bits.DivideValue1 = 2;
            ApicDcr.Bits.DivideValue2 = 0;
            break;

        case 16:
            ApicDcr.Bits.DivideValue1 = 3;
            ApicDcr.Bits.DivideValue2 = 0;
            break;

        case 32:
            ApicDcr.Bits.DivideValue1 = 0;
            ApicDcr.Bits.DivideValue2 = 1;
            break;

        case 64:
            ApicDcr.Bits.DivideValue1 = 1;
            ApicDcr.Bits.DivideValue2 = 1;
            break;

        case 128:
            ApicDcr.Bits.DivideValue1 = 2;
            ApicDcr.Bits.DivideValue2 = 1;
            break;

        /* Divisor is 1 for the others */
        default:
            ApicDcr.Bits.DivideValue1 = 3;
            ApicDcr.Bits.DivideValue2 = 1;
            break;
    }

    /* Write to the APIC vector register */
    ApApicWrite(XAPIC_LVT_TIMER_OFFSET, LvtTimer.Uint32);

    /* Write the APIC DCR register (for the divisor) */
    ApApicWrite(XAPIC_TIMER_DIVIDE_CONFIGURATION_OFFSET, ApicDcr.Uint32);
}
