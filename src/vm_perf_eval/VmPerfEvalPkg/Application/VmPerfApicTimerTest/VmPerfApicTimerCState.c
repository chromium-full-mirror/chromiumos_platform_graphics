/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "VmPerfApicTimerApCommon.h"
#include <ApLib.h>
#include <Library/BaseLib.h>
#include <Register/Intel/Cpuid.h>

UINT32
EFIAPI
VmPerfGetActualCStateIndex(
    APIC_TIMER_CSTATE_INFO *CStateInfo,
    UINT32 CStateIndex
)
{
    UINT32 ActualCStateIndex = CSTATE_USE_HALT;
    UINT32 CStateClamp;

    /* If we do not have any C-states availble, just use HLT */
    if (CStateInfo->AvailableCStates > 0 &&
        CStateIndex != CSTATE_USE_HALT) {
        /* Generate a "clamp" index */
        CStateClamp = CStateInfo->AvailableCStates - 1;

        /* Generate the index we will "really" use */
        ActualCStateIndex =
            (CStateIndex <= CStateClamp) ?
            CStateIndex : CStateClamp;
    }

    return ActualCStateIndex;
}

/*
* Records all possible values for a particular C-state and
* its associated substates.
*/
static
VOID
EFIAPI
VmPerfApicTimerRecordStates(
    APIC_TIMER_CSTATE_INFO  *CStateInfo,
    UINT32 CStateNumber,
    UINT32 SubstateCount
)
{
    UINT32 Index = CStateInfo->AvailableCStates;
    UINT32 Substate;

    for (Substate = 0; Substate < SubstateCount; Substate++) {

        /*
        * We generate the appropriate hint byte and place it in the
        * C-state hint array.
        *
        * In order to make use of these, we need to take the desired
        * C-state and substate number and pass them as an argument
        * to a special CPU instruction (MWAIT).
        *
        * This instruction requires a byte, which contains the desired
        * C-state in the upper 4-bits and the desired substate in the
        * lower 4-bits.
        *
        * This function is populating a section of the available hints
        * array based on the C-state number and the number of available
        * substates as reported by the CPU from CPUID.
        * */
        CStateInfo->CStateHints[Index] =
            (CStateNumber << 4) | (Substate & 0xF);
        Index++;
    }

    CStateInfo->AvailableCStates = Index;
}

/*
* This function probes the available C-states as reported by the CPU.
* It works as follows:
* 1. First checking if we have support for the appropriate leafs
* 2. Making use of those leafs to probe for available C-states, going
* from C0 -> C7.
* 3. Generating all possible MWAIT hint values based on this information.
*/
VOID
EFIAPI
VmPerfApicTimerProbeCStates(
    OUT APIC_TIMER_CSTATE_INFO  *CStateInfo
)
{
    UINT32 Eax, Ebx, Ecx, Edx;
    CPUID_MONITOR_MWAIT_EAX MwaitEax;
    CPUID_MONITOR_MWAIT_EBX MwaitEbx;
    CPUID_MONITOR_MWAIT_ECX MwaitEcx;
    CPUID_MONITOR_MWAIT_EDX MwaitEdx;

    CStateInfo->AvailableCStates = 0;

    /* Figure out the maximum index supported by this CPU */
    AsmCpuid(CPUID_SIGNATURE, &Eax, NULL, NULL, NULL);

    if (Eax >= CPUID_MONITOR_MWAIT) {
        AsmCpuid(CPUID_MONITOR_MWAIT, &Eax, &Ebx, &Ecx, &Edx);

        /* Generate the list of supported MWAIT hints */
        MwaitEax.Uint32 = Eax;
        MwaitEbx.Uint32 = Ebx;
        MwaitEcx.Uint32 = Ecx;

        CStateInfo->MinMonitorLineSize = MwaitEax.Bits.SmallestMonitorLineSize;
        CStateInfo->MaxMonitorLineSize = MwaitEbx.Bits.LargestMonitorLineSize;

        if (MwaitEcx.Bits.ExtensionsSupported) {
            MwaitEdx.Uint32 = Edx;

            /*
            * Populate the hint array, according to Intel's documentation
            * requesting C0 means using 0xF, C1 = 0, C2 = 1, etc..
            */
            if (MwaitEdx.Bits.C0States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 0xF, MwaitEdx.Bits.C0States);
            }

            if (MwaitEdx.Bits.C1States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 0, MwaitEdx.Bits.C1States);
            }

            if (MwaitEdx.Bits.C2States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 1, MwaitEdx.Bits.C2States);
            }

            if (MwaitEdx.Bits.C3States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 2, MwaitEdx.Bits.C3States);
            }

            if (MwaitEdx.Bits.C4States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 3, MwaitEdx.Bits.C4States);
            }

            if (MwaitEdx.Bits.C5States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 4, MwaitEdx.Bits.C5States);
            }

            if (MwaitEdx.Bits.C6States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 5, MwaitEdx.Bits.C6States);
            }

            if (MwaitEdx.Bits.C7States) {
                VmPerfApicTimerRecordStates(
                    CStateInfo, 6, MwaitEdx.Bits.C7States);
            }
        }
    }
}
