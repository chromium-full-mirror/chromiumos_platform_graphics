/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "VmPerfApicTimerApCommon.h"
#include <ApLib.h>
#include <Library/BaseLib.h>

static
VOID
EFIAPI
ApicDeadlineTimerEnterIdle(
    volatile APIC_TIMER_TEST_AP *ApicTimerTest,
    UINT32 CStateIndex
)
{
    UINT32 UsedCStateIndex;

    UsedCStateIndex = VmPerfGetActualCStateIndex(
        (APIC_TIMER_CSTATE_INFO *)&ApicTimerTest->CStateInfo, CStateIndex);

    if (UsedCStateIndex == CSTATE_USE_HALT) {
        /* Use the HLT instruction, has no C-state hint */
        asm("hlt" :::);
    } else {
        /* Use the MONITOR/MWAIT pair, has a C-state hint */
        AsmMonitor((UINTN)&ApicTimerTest->Inactive, 0, 0);
        AsmMwait(
            ApicTimerTest->CStateInfo.CStateHints[UsedCStateIndex], 0);
    }

    /* Save the last used C-State index */
    ApicTimerTest->LastUsedCstateIndex = UsedCStateIndex;
}

/*
* This is the interrupt handler for the APIC TSC deadline timer.
*/
VOID EFIAPI
ApicDeadlineTimerInterrupt(
    VOID *ApBuffer,
    UINT64 Tsc,
    UINT32 Vector
)
{
    UINT64 IntEntryTsc = AsmReadTsc();
    UINT64 ProgramStart, ProgramEnd;

    volatile APIC_TIMER_TEST_AP *ApicTimerTest =
        (volatile APIC_TIMER_TEST_AP *)ApBuffer;
    APIC_TIMER_RECORD *CurrentRecord, *NextRecord;
    UINT64 TicksAdvance = (ApicTimerTest->Inactive ?
                            ApicTimerTest->TicksActive :
                            ApicTimerTest->TicksInactive);

    UINT64 NextTsc = Tsc + TicksAdvance;
    UINT32 RecordIndex = ApicTimerTest->TestRecordsFilled;

    CurrentRecord = &ApicTimerTest->TestRecordBuffer[RecordIndex];

    /* Check to see if we are still within our bounds */
    NextRecord = (RecordIndex < (ApicTimerTest->TestRecordBufferLength - 1) &&
                  ApicTimerTest->ItemRecordsIndex <
                  (ApicTimerTest->ItemRecordsToWrite - 1)) ?
                  &ApicTimerTest->TestRecordBuffer[RecordIndex+1] :
                  NULL;

    CurrentRecord->InterruptReceiveTsc = Tsc;
    CurrentRecord->InterruptEntryTsc = IntEntryTsc;
    CurrentRecord->Flags = (ApicTimerTest->Inactive) ? 0 : FLAG_ACTIVE_PART;
    if (ApicTimerTest->Inactive) {
        /* Save the last used C-state index */
        CurrentRecord->CStateValue = ApicTimerTest->LastUsedCstateIndex;
    }

    if (NextRecord) {
        NextRecord->DesiredReceptionTsc = NextTsc;
    }

    /* Flip the state */
    ApicTimerTest->Inactive = (ApicTimerTest->Inactive) ? FALSE : TRUE;

    /* Increment our pointers */
    ApicTimerTest->ItemRecordsIndex++; ApicTimerTest->TestRecordsFilled++;

    /*
    * Check for ending conditions, if we still have room
    * for the next event.
    */
    if (ApicTimerTest->ItemRecordsIndex >= ApicTimerTest->ItemRecordsToWrite ||
        ApicTimerTest->TestRecordsFilled >=
        ApicTimerTest->TestRecordBufferLength) {
        ApicTimerTest->TestEndCondition = 1;
        CurrentRecord->ProgramTime = 0;
    } else {
        /* Program the timer */
        ProgramStart = AsmReadTsc();
        ApApicTscDeadline(NextTsc);
        ProgramEnd = AsmReadTsc();
        CurrentRecord->ProgramTime = ProgramEnd - ProgramStart;

        ApicTimerTest->IdealizedCurrentTsc += TicksAdvance;
    }

    CurrentRecord->InterruptExitTsc = AsmReadTsc();
    return;
}


VOID
EFIAPI
ApicTimerInitTest(volatile APIC_TIMER_TEST_AP *ApBuffer)
{
    ApBuffer->TestEndCondition = 0;
    ApBuffer->TestRecordsFilled = 0;
}

VOID
EFIAPI
ApicTimerDeadlineCalculateTicks(
    IN UINT64 TscFrequency,
    IN UINT64 Frequency,
    IN UINT16 DutyCycle,
    OUT volatile UINT64 *TscTicksActive,
    OUT volatile UINT64 *TscTicksInactive
)
{
    /*
    * This will give us the rough period of the cycle in TSC ticks
    * The duty cycle is specified in 8.8 fixed point.
    *
    * From this, the number of ticks for both the active cycle
    * and inactive cycle will be performed.
    */
    UINT64 TscPeriod = TscFrequency / Frequency;
    UINT64 TicksActive = ((DutyCycle * TscPeriod) >> 8) / 100;
    UINT64 TicksInactive = TscPeriod - TicksActive;

    /* Return the results */
    *TscTicksActive = TicksActive;
    *TscTicksInactive = TicksInactive;
}

static
VOID
EFIAPI
ApicTimerHandleItemFlags(
    volatile APIC_TIMER_TEST_AP *ApicTimerTest,
    volatile APIC_TIMER_TEST_ITEM *ApicItem
)
{
    /* Check if we have any KVM flags only if we are running on KVM */
    if (ApicTimerTest->KvmHaltPollControlMsrAvailable) {
        if (ApicItem->ItemFlags & ITEM_FLAG_HALT_POLL_OFF) {
            VmPerfApSetHaltPollMode(FALSE);
        }

        if (ApicItem->ItemFlags & ITEM_FLAG_HALT_POLL_ON) {
            VmPerfApSetHaltPollMode(TRUE);
        }
    }

    /* Check any flags which do not depend on KVM here */
}

VOID
EFIAPI
ApicTimerKickoffItem(
    volatile APIC_TIMER_TEST_AP *ApicTimerTest,
    volatile APIC_TIMER_TEST_ITEM *ApicItem
)
{
    UINT64 BeforeProgram;

    ApicTimerDeadlineCalculateTicks(
        ApicTimerTest->TscFrequency,
        ApicItem->Frequency,
        ApicItem->DutyCycle,
        &ApicTimerTest->TicksActive,
        &ApicTimerTest->TicksInactive
    );

    /* Set up the records write count (and associated data) */
    ApicTimerTest->ItemRecordsToWrite = ApicItem->SampleCount;
    ApicTimerTest->ItemRecordsIndex = 0;
    ApicTimerTest->Inactive = FALSE;
    ApicTimerTest->TestEndCondition = 0;

    /* Perform any actions necessary as indicated by the flags */
    ApicTimerHandleItemFlags(
        ApicTimerTest, ApicItem
    );

    /* Prime the first interrupt here */
    UINT64 NextTsc = AsmReadTsc() + ApicTimerTest->TicksActive;
    UINT32 RecordsFilled = ApicTimerTest->TestRecordsFilled;

    ApicTimerTest->TestRecordBuffer[RecordsFilled].DesiredReceptionTsc =
        NextTsc;

    BeforeProgram = AsmReadTsc();
    ApApicTscDeadline(NextTsc);
    BeforeProgram = AsmReadTsc() - BeforeProgram;

    if (RecordsFilled > 0) {
        ApicTimerTest->TestRecordBuffer[RecordsFilled - 1].ProgramTime =
        BeforeProgram;

        ApicTimerTest->IdealizedCurrentTsc += ApicTimerTest->TicksActive;
    } else if (RecordsFilled == 0) {
        /* Use this */
        ApicTimerTest->IdealizedCurrentTsc = NextTsc;
        ApicTimerTest->TscAtTestStart = NextTsc;
    }
}

INTN
EFIAPI
ApicTimerDeadlineTest(
    volatile VOID *TimerTestApBuffer
)
{
    volatile APIC_TIMER_TEST_AP *ApicTimerTest =
        (volatile APIC_TIMER_TEST_AP *)TimerTestApBuffer;
    UINT32 ItemIndex;
    APIC_TIMER_TEST_ITEM *CurrentItem;

    /* Retrieve C-State information from this core */
    VmPerfApicTimerProbeCStates(
        (APIC_TIMER_CSTATE_INFO *)&ApicTimerTest->CStateInfo
    );

    ApApicInit();
    ApicTimerInitTest(ApicTimerTest);

    ApApicSetMode(FALSE, APIC_TIMER_TSC_DEADLINE, 1, 255);
    ApPatchIsr(255, ApicDeadlineTimerInterrupt);
    EnableInterrupts();

    /*
    * Do an initial check to see if we can actually
    * generate some data on the output.
    */
    if (ApicTimerTest->TestRecordBufferLength == 0 ||
        ApicTimerTest->TestItemLength == 0) {
        return 0;
    }

    ItemIndex = 0;
    CurrentItem = &ApicTimerTest->TestItemList[0];

    /* Kick off this item */
    ApicTimerKickoffItem(
        ApicTimerTest,
        CurrentItem
    );

    while (1) {
        if (ApicTimerTest->TestEndCondition) {
            /* Proceed to next test item */
            ItemIndex++;

            /*
            * Terminate test if we have processed all test items or
            * the record buffer has been filled.
            */
            if (ItemIndex >= ApicTimerTest->TestItemLength ||
                (ApicTimerTest->TestRecordsFilled >=
                 ApicTimerTest->TestRecordBufferLength)) {
                ApicTimerTest->TscAtTestEnd = AsmReadTsc();
                ApicTimerTest->EndItemCount = ItemIndex;
                return ApicTimerTest->TestRecordsFilled;
            }

            CurrentItem = &ApicTimerTest->TestItemList[ItemIndex];
            ApicTimerKickoffItem(
                ApicTimerTest,
                CurrentItem
            );

        } else {
            /* Continue the test */
            if (ApicTimerTest->Inactive) {
                /* Sleep the CPU */

                ApicDeadlineTimerEnterIdle(
                    ApicTimerTest,
                    CurrentItem->CStateIndex
                );
            } else {
                /*
                * We can do nothing and keep the poll loop active or
                * we can do actual work here.
                */
            }
        }
    }

    return 0;
}
