/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef     __VM_PERF_EVAL_APIC_LIB_H__
#define     __VM_PERF_EVAL_APIC_LIB_H__

#include <Uefi.h>

/*
* This is a public definition for accessing a cores local APIC.
*/

/*
* Sends a Startup Inter Processor Interrupt to a target core.
* After this is sent, execution will start at StartAddress on the
* target core. The StartAddress must be 4K aligned.
* This function should generally not be used by AP's under a
* tests control.
*
* @param    LapicBase       The base address of the APIC
* @param    ApicId          The APIC Id of the desired core
* @param    StartAddress    The entry point at which the core is to start
*/
VOID EFIAPI VmPerfEvalApicSendSIpi(
    IN EFI_PHYSICAL_ADDRESS LapicBase,
    IN UINT32 ApicId,
    IN UINT32 StartAddress
);

/*
* Send an INIT to a target core.
* This essentially puts the core in a reset state. Once in
* this state, a startup IPI is necessary to get the core to
* leave this state.
* This function should generally not be used by AP's under a
* tests control.
*
* @param    LapicBase       The base address of the APIC
* @param    ApicId          The APIC Id of the desired core
*/
VOID EFIAPI VmPerfEvalApicSendInit(
    IN EFI_PHYSICAL_ADDRESS LapicBase,
    IN UINT32 ApicId
);

/*
* Retrieves the APIC ID of the core calling this function.
*
* @param    LapicBase       The base address of the APIC
*
* @return   The APIC ID of the local core
*/
UINT32 EFIAPI VmPerfEvalApicGetApicId(
    IN EFI_PHYSICAL_ADDRESS LapicBase
);

#endif      /* __VM_PERF_EVAL_APIC_LIB_H__ */
