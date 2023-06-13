/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef __AP_LIB_H__
#define __AP_LIB_H__

#include <Uefi.h>

/* A define for the desired mode of operation for the APIC timer */
typedef enum {
    /*
    * Single shot mode: One interrupt delivered for
    * each programmed timer interval.
    * */
    APIC_TIMER_ONESHOT,

    /*
    * Periodic mode: Similar to oneshot mode, except that
    * the timer will re-arm itself after expiry. It will continue
    * re-arming itself until it is stopped.
    */
    APIC_TIMER_PERIODIC,

    /*
    * Rather than writing an interval value, an absolute TSC value is
    * programmed. The CPU is interrupted when the current TSC value
    * has surpassed the one programmed in the MSR. This functions
    * similarly to one shot mode.
    */
    APIC_TIMER_TSC_DEADLINE
} APIC_TIMER_MODE;


/*
* The spurious vector ID. The spurious interrupt is generated in cases
* where the APIC cannot determine without ambiguity which interrupt
* is to be delievered to the CPU. See Intel or AMD's architecture documents
* for details.
*
* It is not expected for this interrupt to be delivered. If it is the CPU
* will be halted.
*/
#define APIC_SPURIOUS_INT_VECTOR        32

/*
* This allows some code to retrieve the value of a field within
* the calling CPU's PCIB. x contains the destination value,
* and field contains the field from the PCIB structure to load.
*/
#define THIS_CPU_GET_PCIB(x, field)\
{\
    asm volatile("mov %%fs:%c1, %0" :"=r" (x):"i" (OFFSET_OF(PCIB, field) ));\
}

/**
 * @brief Interrupt Entry Function Prototype
 *
 * @param   ApBuffer        Pointer to the Application Buffer for this CPU
 * @param   EntryTsc        The value of the CPU's TSC at ISR stub entry
 * @param   Vector          The vector number of this interrupt
 *
*/
typedef void (EFIAPI *IsrEntry)(
    VOID *ApBuffer,
    UINT64 EntryTsc,
    UINT32 Vector
);


/**
 * @brief Allows a program running on an AP to set an interrupt handler
 *
 * @param   Vector          The vector number for the interrupt
 * @param   IsrPointer      The pointer to the interrupt handler function
 *
*/
VOID EFIAPI ApPatchIsr(UINT32 Vector, IsrEntry IsrPointer);

/**
 * @brief Removes/Uninstalls a previously set interrupt handler
 *
 * @param   Vector          The vector number for the interrupt
 *
*/
VOID EFIAPI ApRemoveIsr(UINT32 Vector);

/**
 * @brief Initialize this AP's local APIC for use.
 *
 * This function MUST be called on an AP before
 * interacting with the APIC.
 *
*/
VOID EFIAPI ApApicInit();

/**
 * @brief Sets the operating mode of a AP's local APIC
 *
 * @param   TimerMasked     Set to TRUE to mask the APIC timer interrupt
 * @param   Mode            The mode to put the APIC timer in (APIC_TIMER_MODE)
 * @param   Divide          The frequency divisor to use on the timer
 * @param   Vector          The vector number to use for the timer interrupt
*/
VOID EFIAPI ApApicSetMode(
    BOOLEAN TimerMasked,
    APIC_TIMER_MODE Mode,
    UINT32 Divide,
    UINT32 Vector
);

/**
 * @brief Returns the value of the APIC version register
 *
 * @returns The value of the APIC version register
*/
UINT32 EFIAPI ApApicVersion();

/**
 * @brief Returns the value of the APIC error register
 *
 * @returns The value of the APIC error register
*/
UINT32 EFIAPI ApApicError();

/**
 * @brief Program the APIC timer initial counter register
 *
 * @param   InitCounter     The value to program this register with
 *
*/
VOID EFIAPI ApApicTimer(UINT32 InitCounter);

/**
 * @brief Program the APIC timer TSC deadline MSR
 *
 * @param   TscDeadlineValue     The TSC deadline value
 *
*/
VOID EFIAPI ApApicTscDeadline(UINT64 TscDeadlineValue);

/**
 * @brief Returns the value of the APIC current count register
 *
 * @returns The value of the APIC current count register
*/
UINT32 EFIAPI ApApicTimerCurrent();

#endif          /* __AP_LIB_H__ */
