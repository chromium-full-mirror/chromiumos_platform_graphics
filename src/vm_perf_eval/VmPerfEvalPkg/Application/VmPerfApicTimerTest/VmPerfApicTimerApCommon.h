/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef __VM_PERF_APIC_TIMER_COMMON_H__
#define __VM_PERF_APIC_TIMER_COMMON_H__

#include <Uefi.h>

/* The APIC timer test record definition */
#include "VmPerfApicTimerRecord.h"

/* Used to just use a basic HLT instruction */
#define     CSTATE_USE_HALT     0xFF

/* C-State information as retrieved from CPUID */
typedef struct {
    /*
    * Number of available C-states reported by this processor,
    * through the MONITOR/MWAIT CPUID leaf.
    *
    * A value of 0 means that we cannot use MONITOR/MWAIT
    * and must rely on the HLT instruction.
    */
    UINT32      AvailableCStates;

    /*
    * This information is retrieved from the CPUID instruction.
    * It defines the size of memory that can be monitored during
    * an idle period. Typically these are both 64 bytes.
    * The test does not make use of the monitoring functionality.
    */
    UINT32      MaxMonitorLineSize;
    UINT32      MinMonitorLineSize;

    /*
    * A synthesized list of available hints that can be used
    * by this processor. C0 -> C7, with 16 possible substates
    * for each Cstate.
    *
    * The Cstates will start from the least aggressive state
    * to the most aggressive state.
    */
    UINT8       CStateHints[128];
} APIC_TIMER_CSTATE_INFO;

/* Common defines for each AP code and BSP -> AP comm */

typedef struct {
    /*
    * CState information, this will be retrieved by running CPUID
    * on the core this structure is "handed" off to.
    */
    APIC_TIMER_CSTATE_INFO  CStateInfo;

    /*
    * TSC Frequency. Used for internal timer calculations.
    * It is assumed all cores on the system share this frequency.
    */
    UINT64                  TscFrequency;

    /*
    * The test items used to guide the tests run on the APs.
    * It includes the list of items and the item length
    */
    APIC_TIMER_TEST_ITEM    *TestItemList;
    UINT32                  TestItemLength;

    /*
    * The 'records' are the test results
    */
    APIC_TIMER_RECORD       *TestRecordBuffer;
    UINT32                  TestRecordBufferLength;
    UINT32                  TestRecordsFilled;

    /*
    * Current item (to be used by the ISR on the AP)
    */
    UINT64                  TicksActive;
    UINT64                  TicksInactive;
    UINT32                  ItemRecordsToWrite;
    UINT32                  ItemRecordsIndex;

    UINT32                  EndItemCount;

    /*
    * The test ends when we:
    *
    * 1) Fill the test records buffer
    * 2) We have completed all test items
    *
    * The ISR will set the TestEndCondition when:
    * It has completed the test items
    * Buffer is full
    *
    * The main code will quit once all test items have been completed
    */
    UINT32                  TestEndCondition;

    /*
    * Inactive/Active flag, used by the ISR to indicate to the main part of the
    * code to either do busy work or sleep.
    */
    BOOLEAN                 Inactive;

    /*
    * Used to indicate the last Cstate index used by the idle function.
    * This will not necessarily be the same as what was requested by
    * the test item as it will depend on the results of the Cstate
    * query performed by the remote processor near the beginning
    * of the test entry point.
    */
    UINT8                   LastUsedCstateIndex;

    /*
    * A flag set by the main processor to indicate that we are running
    * under KVM AND the halt poll control MSR is available.
    */
    BOOLEAN                 KvmHaltPollControlMsrAvailable;

    /*
    * This variable contains the "idealized" TSC value at the current
    * point in the test. At the end of the test, this variable will
    * contain the TSC value that an ideal CPU would have provided
    * it had no processing overhead or delays.
    *
    * This can be used to condense the overhead experienced by
    * the test from external factors into a set of values. These
    * values can then be easily compared with each other.
    */
    UINT64                  IdealizedCurrentTsc;

    /*
    * Contains the TSC used as a base to program the deadline
    * timer with the first event.
    */
    UINT64                  TscAtTestStart;

    /*
    * Contains the TSC at the end of the test run. This will be sampled
    * right before the core returns from the test entry point.
    */
    UINT64                  TscAtTestEnd;
} APIC_TIMER_TEST_AP;

/*
* A structure used to hold information about how the test is to proceed
*/
typedef struct {
    /*
    * Set to TRUE if the test item array should be freed after test completion
    * This is used because we may point the test item array to a statically
    * defined "default" based on what is present in the options file.
    */
    BOOLEAN                 TestItemArrayShouldBeFreed;

    /*
    * The list of test items to run. A test item defines certain characteristics
    * of a portion of the test such as:
    * 1) Frequency: An estimate of how often to fire the interrupt
    * 2) Duty Cycle: Amount of time per cycle to keep the CPU active
    * 3) SampleCount: Number of cycles to run (2 samples per cycle)
    * 4) Flags: Any additional information about a test item
    */
    APIC_TIMER_TEST_ITEM    *TestItems;

    /*
    * Number of test items present in the test item array
    */
    UINT32                  TestItemCount;

    /*
    * The per core record buffer size. This defines the amount of memory
    * to be allocated for each active core to dump the results from a test.
    *
    * This is specified in record count (a record = 1 sample). This buffer
    * is filled at a rate of 2x the current test item frequency.
    *
    * The test running on each core will stop if the test buffer has been
    * completely filled up
    *
    */
    UINTN                   PerCoreRecordBufferSize;

    /*
    * The maximum amount of time that can be spent in the poll loop.
    * The poll loop waits for the remote processors to complete their
    * data collection.
    *
    * This option is specified in minutes (multiples of 60 seconds).
    */
    UINTN                   PollLoopTimeoutInMinutes;

    /*
    * Number of cores to bring up for the test.
    * */
    UINTN                   CoreCount;

    /*
    * Sets the core trigger mode for the remote cores running the test.
    * Set to 0 for immediate start mode (no trigger), 1 to wait for
    * trigger mode.
    *
    * This can be used to help align the cycles of each of the remote
    * cores.
    */
    UINTN                   CoreTriggerMode;

    /*
    * Enable the disk access by the main processor.
    * This can be used to excercise the VirtIO block device
    * to see how they interact with virtualized CPUs.
    */
    BOOLEAN                 EnableDiskAccesses;
    /*
    * The size of the memory buffer used for disk accesses.
    */
    UINTN                   DiskBufferSize;

    /*
    * Determines what to do at the end of the test
    * There are two options here:
    * 1. Request a power off
    * 2. Request a system reset
    *
    * By default, the test will request a system reset (TRUE).
    */
    BOOLEAN                 ResetSystemAtTestEnd;
}   APIC_TIMER_TEST_GLOBAL_PARAMS;

/**
 * @brief This is the timer deadline test entry point for remote processors
 *
 * This is the entry point to the function which performs the test and
 * populates the record buffers on remote processors. This will be the function
 * that is passed into the StartCore function.
 *
 * @param           TimerTestApBuffer   Pointer to a APIC_TIMER_TEST_AP
 * @return          INTN                Number of filled test records
 */
INTN
EFIAPI
ApicTimerDeadlineTest(
    volatile VOID *TimerTestApBuffer
);

/**
 * @brief Calculates active/inactive ticks given test item information
 *
 * Given the TSC frequency, the cycle frequency and the duty cycle in 8.8,
 * this function will calculate the amount of ticks to be spent in
 * the active and inactive portions of the cycle.
 *
 * This information is then used to generate the TSC values used
 * to program the deadline timer.
 *
 * @param       TscFrequency        Frequency of the CPU's TSC
 * @param       Frequency           Cycle frequency of the test (from item)
 * @param       DutyCycle           Duty Cycle (in 8.8 fixed point)
 * @param       TscTicksActive      Output: Number of active ticks
 * @param       TscTicksInactive    Output: Number of inactive ticks
 */
VOID
EFIAPI
ApicTimerDeadlineCalculateTicks(
    IN UINT64 TscFrequency,
    IN UINT64 Frequency,
    IN UINT16 DutyCycle,
    OUT volatile UINT64 *TscTicksActive,
    OUT volatile UINT64 *TscTicksInactive
);

/**
 * @brief Probes the C-state information on this CPU using CPUID
 *
 * This function will generate a list of supported C-state hints
 * as reported by the CPUID instruction. These hints can be used
 * by the MWAIT instruction to ask the CPU to enter a lower power
 * idle state. A APIC_TIMER_CSTATE_INFO structure is filled
 * with the resulting information that was probed from the CPU.
 *
 * @param       CStateInfo       Pointer to structure to be filled
 */
VOID
EFIAPI
VmPerfApicTimerProbeCStates(
    OUT APIC_TIMER_CSTATE_INFO  *CStateInfo
);

/**
 * @brief Retrieve the actual C-state index to use for a C-state request
 *
 * A test item can request any arbitrary C-state index. However, the system
 * may not support the requested C-state index. This function will be used
 * to ensure that we do not request an unsupported C-state when we put
 * the CPU in idle as a result of this test.
 *
 * For example, under KVM C-state hints are not supported. So all C-state
 * requests will be forced to use the basic halting instruction (HLT)
 * instead.
 *
 * @param   CStateInfo      Pointer to previously probed C-state info
 * @param   CStateIndex     Requested C-state index
 * @return  UINT32          Actual C-state index (or CSTATE_USE_HALT)
 */
UINT32
EFIAPI
VmPerfGetActualCStateIndex(
    APIC_TIMER_CSTATE_INFO *CStateInfo,
    UINT32 CStateIndex
);

/**
 * @brief Under KVM, halt polling can be enabled/disabled through the use
 * of a specific halt polling control MSR.
 *
 * If running under KVM, and if the associated CPUID feature bit is set, this
 * function will allow the toggling of host side halt polling.
 *
 * This function MUST NOT be called unless the test is running under KVM.
 *
 * @param   HaltPollEnabled   Halt poll enable/disable (TRUE = Enable)
 */
VOID
EFIAPI
VmPerfApSetHaltPollMode(
    BOOLEAN HaltPollEnabled
);

/**
 * @brief Returns TRUE if running under KVM (also logs a message if found)
 *
 * This function will check if we are running under KVM and log the
 * information out to the console log.
 *
 * @param       Ctx         Pointer to Vm Perf Eval context
 * @return      BOOLEAN     Returns TRUE if running under KVM
 */
BOOLEAN
EFIAPI
VmPerfApicTimerDetectKVM(
    IN VM_PERF_EVAL_CTX *Ctx
);

/**
 * @brief This function parses test items within the config file (if present)
 *
 * This function will parse test items from the configuration file. The items
 * should be listed as "ItemN" where N is 0 to 63. This function will return
 * the number of items found within the config file and provide a list
 * of test items found in the options file.
 *
 * This function, generally, SHOULD NOT be used directly. It is mainly intended
 * to be used with VmPerfApicTimerGetGlobalOptions.
 *
 * @param   Ctx                 Pointer to Vm Perf Eval context
 * @param   SystemTable         Pointer to the EFI_SYSTEM_TABLE
 * @param   OutputItemPointer   Receives a pointer to an array of test items
 *
 * @return  UINT32              The number of valid items found in the options
 */
UINT32
EFIAPI
VmPerfApicParseTestItems(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    OUT APIC_TIMER_TEST_ITEM **OutputItemPointer
);

/**
 * @brief Generate the global test options that will be used for the test
 *
 * The global options contain settings which affect the entire test.
 * This function will retrieve the necessary options or set them to a
 * reasonable default if not present. See the definition of the
 * APIC_TIMER_TEST_GLOBAL_PARAMS for details on the global options.
 *
 * @param   Ctx             Pointer to Vm Perf Eval context
 * @param   SystemTable     Pointer to the EFI_SYSTEM_TABLE
 * @param   GlobalOptions   Pointer to a global test params structure to fill
 * @return  BOOLEAN         True if successful, false otherwise
 */
BOOLEAN
VmPerfApicTimerGetGlobalOptions(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    IN APIC_TIMER_TEST_GLOBAL_PARAMS *GlobalOptions
);

/**
 * @brief Dumps the test record buffer into a CSV for offline analysis
 *
 * This function will dump the supplied test records as a CSV file
 * for offline analysis. These (per-core) files along with the system
 * information and test item files can be used to generate a set of graphs.
 *
 * @param   Ctx             Pointer to a Vm Perf Eval context
 * @param   CoreIndex       The core index this record buffer belongs to
 * @param   RecordsToDump   Number of records in the buffer to dump
 * @param   Records         Pointer to the array of records
 *
 * @return  BOOLEAN         True if successful, false otherwise
 */
BOOLEAN EFIAPI DumpApicTimerRecord(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 CoreIndex,
    UINT32 RecordsToDump,
    APIC_TIMER_RECORD *Records
);

/**
 * @brief Dumps the list of test items as a CSV file for offline analysis
 *
 * This function will dump the supplied list of test items as a CSV file
 * to aid in offline analysis.
 *
 * @param   Ctx         Pointer to a Vm Perf Eval context
 * @param   ItemCount   The number of test items in the array
 * @param   Items       Pointer to the test item array
 * @param   CStateInfo  C-state info structure for the main core
 *
 * @return  BOOLEAN     True if successfully dumped, false otherwise
 */
BOOLEAN EFIAPI DumpApicTimerItems(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 ItemCount,
    APIC_TIMER_TEST_ITEM *Items,
    APIC_TIMER_CSTATE_INFO *CStateInfo
);

/**
* @brief Dumps system information into a CSV file
*
* This function dumps the appropriate system information into
* a CSV file for offline processing.
*
* Currently only the CPU name and the estimated TSC frequency
* is dumped.
*
* @param        Ctx         Pointer to a Vm Perf Eval context structure
*
* @returns      True if the output file was opened successfully
*/
BOOLEAN EFIAPI DumpSystemInfo(
    VM_PERF_EVAL_CTX *Ctx
);

#endif              /* __VM_PERF_APIC_TIMER_COMMON_H__ */
