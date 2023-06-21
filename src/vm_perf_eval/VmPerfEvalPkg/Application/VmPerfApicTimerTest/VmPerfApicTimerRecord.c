/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "VmPerfApicTimerApCommon.h"
#include <Library/BaseLib.h>


static BOOLEAN EFIAPI WriteSpecificRecords(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 CoreIndex,
    APIC_TIMER_RECORD *Records,
    UINT32 RecordsToDump,
    UINT64 FlagsMask,
    UINT64 FlagsCompare,
    CHAR16 *Suffix
) {
    VM_PERF_FILE File;
    UINT32 i;
    BOOLEAN FileOpened;

    FileOpened = VmPerfOpenFileForWrite(
        Ctx, &File, CoreIndex, L"ApicTimer", Suffix,
        0, L"csv"
    );

    if (!FileOpened) {
        VmPerfLog(Ctx, 0, L"Failed to open file for record writing.\n");
        return FALSE;
    }

    /* Dump the header */
    VmPerfWriteFileF(Ctx, &File, L"DesiredTsc, ");
    VmPerfWriteFileF(Ctx, &File, L"ReceiveTsc, ");
    VmPerfWriteFileF(Ctx, &File, L"IsrEntryTsc, ");
    VmPerfWriteFileF(Ctx, &File, L"IsrExitTsc, ");
    VmPerfWriteFileF(Ctx, &File, L"ProgramTime, ");
    VmPerfWriteFileF(Ctx, &File, L"DesiredToReceive, ");
    VmPerfWriteFileF(Ctx, &File, L"IsrExecTime,");
    VmPerfWriteFileF(Ctx, &File, L"RecordBufferIndex");
    VmPerfWriteFileF(Ctx, &File, L"\n");

    for (i = 0; i < RecordsToDump; i++) {
        if ((Records[i].Flags & FlagsMask) == FlagsCompare) {
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,", Records[i].DesiredReceptionTsc);
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,", Records[i].InterruptReceiveTsc);
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,", Records[i].InterruptEntryTsc);
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,", Records[i].InterruptExitTsc);
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,", Records[i].ProgramTime);
            VmPerfWriteFileF(
                Ctx, &File, L"%llu,",
                Records[i].InterruptReceiveTsc -
                Records[i].DesiredReceptionTsc);

            VmPerfWriteFileF(Ctx, &File, L"%llu,",
                Records[i].InterruptExitTsc - Records[i].InterruptEntryTsc);

            VmPerfWriteFileF(Ctx, &File, L"%u", i);

            VmPerfWriteFileF(Ctx, &File, L"\n");
        }
    }

    /* Flush the file output and close it since we are done */
    VmPerfFlushFile(Ctx, &File);
    VmPerfCloseFile(Ctx, &File);

    return TRUE;
}

BOOLEAN EFIAPI DumpApicTimerRecord(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 CoreIndex,
    UINT32 RecordsToDump,
    APIC_TIMER_RECORD *Records
)
{
    UINT32 Inactive = 0;
    UINT32 Active = 0;
    UINT32 i;
    BOOLEAN SuccessActive = TRUE;
    BOOLEAN SuccessInactive = TRUE;

    /*
    * The records will contain active (interrupt fired during active execution)
    * and inactive (interrupt fired during idle side).
    *
    * We want these to be split up into separate CSV files. So before dumping
    * them let's count how many active/inactive ones we have.
    */

    for (i = 0; i < RecordsToDump; i++) {
        if (Records[i].Flags & FLAG_ACTIVE_PART) {
            Active++;
        } else {
            Inactive++;
        }
    }

    VmPerfLog(Ctx, 0, L"Dumping record buffer for Core %u [APIC %u]...\n",
        CoreIndex, Ctx->AcpiCores[CoreIndex].ApicId);

    /* Write the active side records if any have been found */
    if (Active > 0) {
        SuccessActive = WriteSpecificRecords(
            Ctx,
            CoreIndex,
            Records, RecordsToDump,
            FLAG_ACTIVE_PART, FLAG_ACTIVE_PART, L"Active"
        );

        if (SuccessActive) {
            VmPerfLog(Ctx, 0, L"Active side done.\n");
        } else {
            VmPerfLog(Ctx, 0, L"Active side failed.\n");
        }
    }

    /* Write the inactive side records if any have been found */
    if (Inactive > 0) {
        SuccessInactive = WriteSpecificRecords(
            Ctx,
            CoreIndex,
            Records, RecordsToDump,
            FLAG_ACTIVE_PART, 0, L"Inactive"
        );

        if (SuccessInactive) {
            VmPerfLog(Ctx, 0, L"Inactive side done.\n");
        } else {
            VmPerfLog(Ctx, 0, L"Inactive side failed.\n");
        }
    }

    /*
    * Both of these should default to TRUE so the case where we have none of
    * either is handled correctly.
    */
    return SuccessActive && SuccessInactive;
}

BOOLEAN EFIAPI DumpApicTimerItems(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 ItemCount,
    APIC_TIMER_TEST_ITEM *Items,
    APIC_TIMER_CSTATE_INFO *CStateInfo
)
{
    /* This code will dump the test items as test metadata */
    BOOLEAN FileOpened;
    VM_PERF_FILE File;
    UINT32 i;
    UINT32 CurrentIndexBase = 0;
    UINT32 CStateIndex;
    volatile UINT64 ActiveTicks, InactiveTicks;

    FileOpened = VmPerfOpenFileForWrite(
        Ctx, &File, VM_PERF_MAX_CORES, L"Items", NULL, 0, L"csv"
    );

    if (!FileOpened) {
        VmPerfLog(Ctx, 0, L"Failed to open test items file for writing.\n");
        return FALSE;
    }

    VmPerfWriteFileF(Ctx, &File, L"Frequency, ");
    VmPerfWriteFileF(Ctx, &File, L"DutyCycle (%%), ");
    VmPerfWriteFileF(Ctx, &File, L"Count, ");
    VmPerfWriteFileF(Ctx, &File, L"IndexStart, ");
    VmPerfWriteFileF(Ctx, &File, L"IndexEnd, ");
    VmPerfWriteFileF(Ctx, &File, L"CStateHint, ");
    VmPerfWriteFileF(Ctx, &File, L"ActiveTicks, ");
    VmPerfWriteFileF(Ctx, &File, L"InactiveTicks");
    VmPerfWriteFileF(Ctx, &File, L"\n");

    for (i = 0; i < ItemCount; i++) {
        VmPerfWriteFileF(Ctx, &File, L"%u,", Items[i].Frequency);
        VmPerfWriteFileF(Ctx, &File, L"%u,", Items[i].DutyCycle >> 8);
        VmPerfWriteFileF(Ctx, &File, L"%u,", Items[i].SampleCount);
        VmPerfWriteFileF(Ctx, &File, L"%u,", CurrentIndexBase);
        VmPerfWriteFileF(Ctx, &File, L"%u,",
            CurrentIndexBase + (Items[i].SampleCount - 1));

        CStateIndex = VmPerfGetActualCStateIndex(
            CStateInfo, Items[i].CStateIndex
        );

        if (CStateIndex == CSTATE_USE_HALT) {
            VmPerfWriteFileF(Ctx, &File, L"HLT, ");
        } else {
            VmPerfWriteFileF(Ctx, &File, L"MWAIT[0x%02X], ",
                CStateInfo->CStateHints[CStateIndex]);
        }

        /* Retrieve the tick counts */
        ApicTimerDeadlineCalculateTicks(
            Ctx->EstimatedTscFrequency,
            Items[i].Frequency,
            Items[i].DutyCycle,
            &ActiveTicks,
            &InactiveTicks
        );

        VmPerfWriteFileF(Ctx, &File, L"%lu, ", ActiveTicks);
        VmPerfWriteFileF(Ctx, &File, L"%lu", InactiveTicks);

        /* Move onto the next one */
        CurrentIndexBase += Items[i].SampleCount;
        VmPerfWriteFileF(Ctx, &File, L"\n");
    }

    VmPerfFlushFile(Ctx, &File);
    VmPerfCloseFile(Ctx, &File);

    return TRUE;
}

/* This will dump system info into a CSV file */
BOOLEAN EFIAPI DumpSystemInfo(
    VM_PERF_EVAL_CTX *Ctx
)
{
    BOOLEAN FileOpened;
    VM_PERF_FILE File;
    VM_PERF_CPU_INFORMATION *CpuInfo = &Ctx->CpuInformation;
    CHAR8 *CpuName;

    FileOpened = VmPerfOpenFileForWrite(
        Ctx, &File, VM_PERF_MAX_CORES, L"SystemInfo", NULL, 0, L"csv"
    );

    if (!FileOpened) {
        VmPerfLog(Ctx, 0, L"Failed to open test items file for writing.\n");
        return FALSE;
    }

    /*
    * Most contemporary CPU's support brand name identification, so this is
    * generally not needed.
    */
    if (AsciiStrCmp(CpuInfo->BrandName, "") == 0) {
        CpuName = CpuInfo->VendorString;
    } else {
        CpuName = CpuInfo->BrandName;
    }

    VmPerfWriteFileF(Ctx, &File, L"CPU, ");
    VmPerfWriteFileF(Ctx, &File, L"TSC Frequency");
    VmPerfWriteFileF(Ctx, &File, L"\n");

    /* Dump the appropriate information (CPU) */
    if (CpuInfo->Virtualized) {
        VmPerfWriteFileF(Ctx, &File, L"%a [%a], ",
            CpuName,
            (CpuInfo->RunningUnderKvm) ? "KVM" : CpuInfo->HypervisorName
        );
    } else {
        VmPerfWriteFileF(Ctx, &File, L"%a, ",
            CpuName);
    }

    /* Dump the TSC frequency */
    VmPerfWriteFileF(Ctx, &File, L"%lu", Ctx->EstimatedTscFrequency);

    VmPerfWriteFileF(Ctx, &File, L"\n");

    VmPerfFlushFile(Ctx, &File);
    VmPerfCloseFile(Ctx, &File);

    return TRUE;
}
