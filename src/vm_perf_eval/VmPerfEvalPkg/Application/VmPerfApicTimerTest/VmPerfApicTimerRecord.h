/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef __VM_PERF_APIC_TIMER_RECORD_H__
#define __VM_PERF_APIC_TIMER_RECORD_H__

#include <Uefi.h>
#include "VmPerfEvalLib.h"


/* Set this flag to indicate that this is the active portion of the cycle */
#define     FLAG_ACTIVE_PART            0x1

/*
* Used to record the results of each interrupt received by a core.
* Can be used for offline analysis.
*/
typedef struct {
    /* Entry point for ISR (from ISR stub) */
    UINT64  InterruptReceiveTsc;

    /* Entry point time of ISR dispatch function */
    UINT64  InterruptEntryTsc;

    /* Exit time of ISR dispatch function */
    UINT64  InterruptExitTsc;

    /*
    * The actual point timer was programmed to fire (populated by n-1) for
    * this event.
    *
    * The timer programming happens in a chain like fashion. T=0 knows
    * when T=1 is to fire, T=1 knows when T=2 is to fire and so on...
    * The desired reception TSC is known at T=n-1 (i.e T=0 for T=1).
    * For this reason, we set this value at the previous timer event
    * OR at the kick off function (lets call this T=-1 for the sake of
    * clarity).
    *
    * As a final note, this is also the value that is programmed into the
    * timer hardware by T=n-1.
    */
    UINT64  DesiredReceptionTsc;

    /* Number of ticks it took to program the timer */
    UINT64  ProgramTime;

    /* Flags values */
    UINT64  Flags;

    /* Pad this structure to 64-bytes */
    UINT64  Pad1;
    UINT32  Pad2;
    UINT16  Pad3;
    UINT8   Pad4;

    /* Used to index the available C-states for inactive part */
    UINT8   CStateValue;
} APIC_TIMER_RECORD;

/*
* All cores will use a global list of these to determine how to proceed through
* the test. This list will be readonly and shared with all the cores.
*/
typedef struct {
    /* Number of records to collect using this data */
    UINT32 SampleCount;

    /* Frequency determines our period */
    UINT16 Frequency;

    /* Duty cycle in 8.8 */
    UINT16 DutyCycle;

    /* Specific flags set for this timer test item */
    UINT32 ItemFlags;
    UINT16 Pad1;
    UINT8  Pad2;

    /* Requested index into C-state array to use for sleep */
    UINT8  CStateIndex;
} APIC_TIMER_TEST_ITEM;

/* List of item flags */

/* Set this flag to EXPLICITLY enable halt polling */
#define     ITEM_FLAG_HALT_POLL_ON          (1ULL << 0)

/* Set this flag to EXPLICITLY disable halt polling */
#define     ITEM_FLAG_HALT_POLL_OFF         (1ULL << 1)

#endif          /* __VM_PERF_APIC_TIMER_RECORD_H__ */
