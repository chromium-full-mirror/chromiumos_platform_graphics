/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <VmPerfEvalLib.h>
#include <Protocol/LoadedImage.h>
#include <Register/Intel/Cpuid.h>
#include "VmPerfApicTimerApCommon.h"

/**
 * This is the main implementation of the APIC timer test UEFI program.
 *
 * This program will make use of the APIC deadline timer functionality
 * to determine how long it takes from APIC timer fire to execution start
 * of the interrupt service routine. We can do it with deadline mode as the
 * absolute time at which we want the interrupt to fire is programmed rather
 * than a relative time.
 *
 */

#define DEFAULT_DELAY   5000000

APIC_TIMER_CSTATE_INFO gCStateInfo;
APIC_TIMER_TEST_GLOBAL_PARAMS gOptions;
EFI_PHYSICAL_ADDRESS gDiskBufferAddress;
VM_PERF_RAW_DISK_INFORMATION gRawDiskInformation;

UINT64 gCurrentBlock = 0;
UINTN gLastReadBlockCount = 0;


/*
* We use this timer event to time the 1 second intervals between
* passes.
*/
EFI_EVENT   gTimerEvent;


VOID
EFIAPI
DumpCoreTestResults(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    IN UINT32 ItemCount,
    IN APIC_TIMER_TEST_ITEM *Items,
    IN UINT64 CoresMask
)
{
    UINT32 CurrentCore;
    volatile APIC_TIMER_TEST_AP *CoreStruct;
    VM_PERF_FILE PerCoreSummaryFile;
    BOOLEAN IdealizedFileOK;

    /* Dump the actual timer item list */
    DumpApicTimerItems(
        Ctx, ItemCount, Items,
        &gCStateInfo
    );

    IdealizedFileOK =
    VmPerfOpenFileForWrite(
        Ctx, &PerCoreSummaryFile, VM_PERF_MAX_CORES, L"PerCoreSummary",
        NULL, 0, L"csv"
    );

    if (!IdealizedFileOK) {
        VmPerfLog(Ctx, 0, L"Failed to open PerCoreSummary file.\n");
    } else {
        /* Dump the header out */
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"TscAtStart, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"TscAtEnd, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"IdealizedTscAtEnd, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"ActualMinusIdealized, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"TestDurationTscTicks, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"CoreId, ");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"APIC ID");
        VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"\n");
    }

    while (CoresMask) {
        CurrentCore = __builtin_ffs(CoresMask) - 1;
        CoreStruct = VmPerfGetApPage(Ctx, CurrentCore);

        DumpApicTimerRecord(
            Ctx, CurrentCore, CoreStruct->TestRecordsFilled,
            CoreStruct->TestRecordBuffer
        );

        /* We can dump all this data into a single CSV file (for CPUs) */
        if (IdealizedFileOK) {
            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%lu, ",
                CoreStruct->TscAtTestStart);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%lu, ",
                CoreStruct->TscAtTestEnd);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%lu, ",
                CoreStruct->IdealizedCurrentTsc);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%lu, ",
                CoreStruct->TscAtTestEnd -
                CoreStruct->IdealizedCurrentTsc);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%lu, ",
                CoreStruct->TscAtTestEnd -
                CoreStruct->TscAtTestStart);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%u, ",
                CurrentCore);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"%u",
                Ctx->AcpiCores[CurrentCore].ApicId);

            VmPerfWriteFileF(Ctx, &PerCoreSummaryFile, L"\n");
        }

        CoresMask &= (~(1ULL << CurrentCore));
    }

    if (IdealizedFileOK) {
        /* Close the file after we are done with it */
        VmPerfFlushFile(Ctx, &PerCoreSummaryFile);
        VmPerfCloseFile(Ctx, &PerCoreSummaryFile);
    }
}

VOID
EFIAPI
DumpRemoteCoreStatus(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT64 PollCoresMask
)
{
    UINT32 CurrentCore;
    volatile APIC_TIMER_TEST_AP *CoreStruct;
    VM_PERF_CORE_STATUS CoreStatus;

    while (PollCoresMask) {
        CurrentCore = __builtin_ffs(PollCoresMask) - 1;
        CoreStruct = VmPerfGetApPage(Ctx, CurrentCore);

        /* Retrieve the core status */
        CoreStatus = VmPerfProbeInService(Ctx, CurrentCore);

        switch (CoreStatus) {
            case CoreStatusRunning:
                VmPerfLog(Ctx, 0, L"Core %u [APIC %u]: RUNNING[%u/%u]\n",
                    CurrentCore, Ctx->AcpiCores[CurrentCore].ApicId,
                    CoreStruct->TestRecordsFilled,
                    CoreStruct->TestRecordBufferLength);
                break;

            case CoreStatusException:
                VmPerfLog(Ctx, 0, L"Core %u [APIC %u]: EXCEPTION[%u/%u]\n",
                    CurrentCore, Ctx->AcpiCores[CurrentCore].ApicId,
                    CoreStruct->TestRecordsFilled,
                    CoreStruct->TestRecordBufferLength);
                break;

            case CoreStatusFinished:
                VmPerfLog(Ctx, 0, L"Core %u [APIC %u]: COMPLETED[%u/%u]\n",
                    CurrentCore, Ctx->AcpiCores[CurrentCore].ApicId,
                    CoreStruct->TestRecordsFilled,
                    CoreStruct->TestRecordBufferLength);
                break;

            default:
                VmPerfLog(Ctx, 0, L"Core %u [APIC %u]: UNKNOWN\n",
                    CurrentCore, Ctx->AcpiCores[CurrentCore].ApicId);
                break;
        }

        PollCoresMask &= (~(1ULL << CurrentCore));
    }
}

BOOLEAN
EFIAPI
RemoteCorePollPass(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    IN UINT64 PollCoresMask,
    OUT BOOLEAN *IsError
)
{
    BOOLEAN IsDone = TRUE;
    BOOLEAN NoErrors = TRUE;
    UINT32 CurrentCore;
    VM_PERF_CORE_STATUS CoreStatus;

    while (PollCoresMask) {
        CurrentCore = __builtin_ffs(PollCoresMask) - 1;

        /* Retrieve the core status */
        CoreStatus = VmPerfProbeInService(Ctx, CurrentCore);

        switch (CoreStatus) {
            case CoreStatusRunning:
                IsDone = FALSE;
                break;

            case CoreStatusException:
                NoErrors = FALSE;
                break;

            case CoreStatusFinished:
                break;

            default:
                NoErrors = FALSE;
                break;
        }

        PollCoresMask &= (~(1ULL << CurrentCore));
    }

    if (!NoErrors) {
        /* An error occured, report this */
        VmPerfLog(Ctx, 0, L"\nAn error occured on one or more cores\n");
        DumpRemoteCoreStatus(Ctx, PollCoresMask);
    } else if (IsDone) {
        /* Test has completed, dump results of all cores */
        VmPerfLog(Ctx, 0, L"\n");
        DumpRemoteCoreStatus(Ctx, PollCoresMask);
    } else {
        /* Log a simple "alive" indicator */
        VmPerfLog(Ctx, 0, L".");
    }

    *IsError = !NoErrors;
    return IsDone;
}

BOOLEAN
EFIAPI
StartTimerTests(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT64 CoresToRun,
    IN APIC_TIMER_TEST_ITEM *ItemList,
    IN UINT32 ItemCount,
    IN UINT32 RecordBufferSizePerCore
)
{
    UINT32 CurrentCore;
    volatile APIC_TIMER_TEST_AP *CoreStruct;
    EFI_PHYSICAL_ADDRESS CoreRecordBufferPhysical;
    EFI_STATUS Status;
    CORE_ENTRY_INFO EntryInfo;
    VM_PERF_CPU_INFORMATION *CpuInfo = &Ctx->CpuInformation;

    /* Clear out the trigger value */
    VmPerfTrigger(Ctx, FALSE);

    while (CoresToRun) {
        CurrentCore = __builtin_ffsll(CoresToRun) - 1;

        CoreStruct = (volatile APIC_TIMER_TEST_AP *)VmPerfGetApPage(
            Ctx, CurrentCore
        );

        /* Fill in the information for this core */
        CoreStruct->TestItemList = ItemList;
        CoreStruct->TestItemLength = ItemCount;
        CoreStruct->TscFrequency = Ctx->EstimatedTscFrequency;
        CoreStruct->KvmHaltPollControlMsrAvailable =
            CpuInfo->KvmHaltPollControl;


        /* Allocate a memory buffer for the record data */
        Status = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            EFI_SIZE_TO_PAGES(
                sizeof(APIC_TIMER_RECORD) * RecordBufferSizePerCore
            ),
            &CoreRecordBufferPhysical
        );

        if (Status != EFI_SUCCESS) {
            VmPerfLog(
                Ctx, 0,
                L"Record Buffer allocation failed for core %u\n",
                CurrentCore);

            VmPerfLog(
                Ctx, 0,
                L"Status = %r\n",
                Status
            );
        }

        /* Fill out test record buffer information */
        CoreStruct->TestRecordBuffer =
            (APIC_TIMER_RECORD *)CoreRecordBufferPhysical;
        CoreStruct->TestRecordBufferLength =
            RecordBufferSizePerCore;

        /* Generate the page table for the target core */
        IdentityPtReset(Builder);

        /*
        * Map the item buffer.
        * This should only be done if we allocated the item list memory
        * buffer.
        */
        if (gOptions.TestItemArrayShouldBeFreed)
            IdentityPtMap(Builder,
                (UINT64)ItemList,
                EFI_SIZE_TO_PAGES(ItemCount * sizeof(APIC_TIMER_TEST_ITEM)),
                TAG_CACHE_WB | TAG_ACCESS_RO, 0xFF);

        /* Map the test record buffer */
        IdentityPtMap(Builder,
            (UINT64)CoreRecordBufferPhysical,
            EFI_SIZE_TO_PAGES(
                RecordBufferSizePerCore * sizeof(APIC_TIMER_RECORD)
            ),
            TAG_CACHE_WB | TAG_ACCESS_RW, 0xFF
        );

        /* Map this code area (we need access to some data regions as well) */
        IdentityPtMap(Builder,
            (UINT64)Ctx->LoadedImage->ImageBase,
            EFI_SIZE_TO_PAGES(Ctx->LoadedImage->ImageSize),
            TAG_CACHE_WB | TAG_ACCESS_RO, 0xFF);


        /* Fire off the core now that everything is setup */
        EntryInfo.EntryPoint = (EFI_PHYSICAL_ADDRESS)ApicTimerDeadlineTest;
        EntryInfo.EntryType = CoreEntryLongMode;
        EntryInfo.PtBuilder = Builder;
        EntryInfo.TriggerMode =
            (gOptions.CoreTriggerMode) ? CoreTriggerWait : CoreTriggerImmediate;

        /* Start the core */
        if (!VmPerfStartCore(Ctx, CurrentCore, &EntryInfo)) {
            VmPerfLog(Ctx, 0, L"Failed to start core %u\n", CurrentCore);
            return FALSE;
        }

        CoresToRun &= (~(1ULL << CurrentCore));
    }

    /* Fire off the cores if the trigger mode was requested */
    if (gOptions.CoreTriggerMode) {
        VmPerfTrigger(Ctx, TRUE);
    }

    return TRUE;
}

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE       ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    VM_PERF_EVAL_CTX Ctx;
    UINT64 CoreServiceMask;
    IDENTITY_PT_BUILDER Builder;
    UINT32 SystemPaBits;
    UINT64 TagBufferSize;
    EFI_STATUS Status;
    UINT32 PollTimeoutInSeconds;
    BOOLEAN PollDone = FALSE, PollError = FALSE;

    if (!VmPerfInitialize(&Ctx)) {
        Print(L"Failed to initialize VmPerf library.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    /* Shut off watchdog timer */
    SystemTable->BootServices->SetWatchdogTimer(
        0, 0, 0, NULL
    );

    /* Log CPU information, probe CPU C-states and retrieve test options */
    VmPerfLogCpuInformation(&Ctx);
    VmPerfApicTimerProbeCStates(&gCStateInfo);
    VmPerfApicTimerGetGlobalOptions(
        &Ctx, SystemTable,
        &gOptions
    );

    /* Log more information if we are running under KVM/Virtualization */
    VmPerfApicTimerDetectKVM(&Ctx);

    /* Determine if we can run this test */
    if (!Ctx.CpuInformation.TscDeadline) {
        VmPerfLog(&Ctx, 0, L"This test requires TSC deadline support.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    /* Put all cores in service */
    CoreServiceMask = VmPerfPutInService(&Ctx, CoreTypeAll, 0);
    if (BitFieldCountOnes64(CoreServiceMask, 0, 63) < 1) {
        VmPerfLog(&Ctx, 0, L"This test requires at least 1 remote core.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    /* Mask off the core service mask */
    if (gOptions.CoreCount < 64) {
        CoreServiceMask &=
            ( (1ULL << gOptions.CoreCount) -1);
    }

    /* Set up disk access information as needed */
    if (gOptions.EnableDiskAccesses) {
        /* Allocate the disk buffer memory */
        Status = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages, EfiLoaderData,
            EFI_SIZE_TO_PAGES(gOptions.DiskBufferSize),
            &gDiskBufferAddress
        );

        if (Status != EFI_SUCCESS) {
            VmPerfLog(&Ctx, 0, L"Failed to allocate disk buffer memory\n");
            SystemTable->BootServices->Stall(DEFAULT_DELAY);
            return EFI_SUCCESS;
        }

        if (!VmPerfGetRawDiskInformation(&Ctx, &gRawDiskInformation)) {
            VmPerfLog(&Ctx, 0, L"Failed to retrieve raw disk information.\n");
            SystemTable->BootServices->Stall(DEFAULT_DELAY);
            return EFI_SUCCESS;
        }
    }

    /* Create an timer event (to throttle remote core polling) */
    Status = SystemTable->BootServices->CreateEvent(
        EVT_TIMER,
        TPL_CALLBACK,
        NULL, NULL,
        &gTimerEvent
    );

    if (Status != EFI_SUCCESS) {
        VmPerfLog(&Ctx, 0, L"Failed to create timer event for async timing.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    } else {
        VmPerfLog(&Ctx, 0, L"Timer successfully allocated.\n");
    }

    /*
    * Retrieve an estimated number of physical address bits based on the
    * EFI memory map.
    */
    SystemPaBits = IdentityPtGetSystemPaBits();

    TagBufferSize = IdentityPtInit(&Builder, &g_Pml4Desc, SystemPaBits, NULL);

    EFI_PHYSICAL_ADDRESS TagBuffer = BASE_4GB - 1;

    Status = SystemTable->BootServices->AllocatePages(
                            AllocateMaxAddress, EfiLoaderData,
                            EFI_SIZE_TO_PAGES(TagBufferSize),
                            &TagBuffer);

    if (Status != EFI_SUCCESS) {
        Print(L"Failed to allocate Paging Tag Buffer.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    TagBufferSize = IdentityPtInit(&Builder, &g_Pml4Desc, SystemPaBits,
                                   (VOID *)TagBuffer);

    StartTimerTests(
        &Ctx, SystemTable, &Builder,
        CoreServiceMask, (APIC_TIMER_TEST_ITEM *)gOptions.TestItems,
        gOptions.TestItemCount,
        gOptions.PerCoreRecordBufferSize
    );

    PollTimeoutInSeconds = gOptions.PollLoopTimeoutInMinutes * 60;

    Status = SystemTable->BootServices->SetTimer(
        gTimerEvent, TimerRelative, 10000000
    );

    if (Status != EFI_SUCCESS) {
        VmPerfLog(&Ctx, 0, L"Failed to set Timer Event.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return FALSE;
    }

    if (gOptions.EnableDiskAccesses) {
        /* Kick off the first access */
        UINTN BlockCountToRead =
            gOptions.DiskBufferSize / gRawDiskInformation.BlockSize;
        BlockCountToRead =
            (BlockCountToRead > gRawDiskInformation.RawDiskSizeInBlocks) ?
            gRawDiskInformation.RawDiskSizeInBlocks : BlockCountToRead;

        gLastReadBlockCount = BlockCountToRead;
        if (!VmPerfReadRawDisk(
            &Ctx, gCurrentBlock, BlockCountToRead,
            (void *)gDiskBufferAddress
        ))
        {
            VmPerfLog(&Ctx, 0, L"Raw read failed.\n");
            SystemTable->BootServices->Stall(DEFAULT_DELAY);
            return FALSE;
        }
    }

    /*
    * Continue until
    * 1. PollTimeoutInSeconds is > 0
    * 2. PollDone is false (We have not finished)
    * 3. PollError is false (there has been no detected errors on remote
    * cores)
    */
    while (!PollDone && PollTimeoutInSeconds > 0 && !PollError) {
        /* We don't want to wait here as we want to keep the main CPU loaded */
        if (SystemTable->BootServices->CheckEvent(gTimerEvent) == EFI_SUCCESS) {
            PollDone = RemoteCorePollPass(
                &Ctx, SystemTable,
                CoreServiceMask, &PollError);
            PollTimeoutInSeconds--;

            /* Reset the event */
            Status = SystemTable->BootServices->SetTimer(
                gTimerEvent, TimerRelative, 10000000
            );
        }

        if (gOptions.EnableDiskAccesses) {
            if (VmPerfCheckRawDiskStatus(&Ctx)) {
                /* Kick off next read */
                UINT64 MaxBlockReadSize;

                gCurrentBlock += gLastReadBlockCount;

                if (gCurrentBlock >= gRawDiskInformation.RawDiskSizeInBlocks) {
                    gCurrentBlock = 0;
                }

                MaxBlockReadSize = gRawDiskInformation.RawDiskSizeInBlocks -
                                        gCurrentBlock;

                /* Initiate the read */
                BOOLEAN ReadSucceeded = VmPerfReadRawDisk(&Ctx, gCurrentBlock,
                    (gOptions.DiskBufferSize /
                     gRawDiskInformation.BlockSize) > MaxBlockReadSize ?
                    MaxBlockReadSize :
                    (gOptions.DiskBufferSize / gRawDiskInformation.BlockSize),
                    (void *)gDiskBufferAddress  );

                if (!ReadSucceeded) {
                    VmPerfLog(&Ctx, 0, L"Raw read failed.\n");
                    SystemTable->BootServices->Stall(DEFAULT_DELAY);
                    return FALSE;
                }
            } else {
                VmPerfLog(&Ctx, 0, L"Raw read failed.\n");
                SystemTable->BootServices->Stall(DEFAULT_DELAY);
                return FALSE;
            }
        }
    }

    /* Dump results only when all cores have finished the test */
    if (PollDone) {
        DumpCoreTestResults(
            &Ctx, SystemTable,
            gOptions.TestItemCount, (APIC_TIMER_TEST_ITEM *)gOptions.TestItems,
            CoreServiceMask
        );
    } else if (PollTimeoutInSeconds == 0) {
        VmPerfLog(
            &Ctx, 0, L"Poll timeout limit hit. No results will be dumped\n");
    } else if (PollError) {
        VmPerfLog(&Ctx, 0, L"An error occured on a remote processor. ");
        VmPerfLog(&Ctx, 0, L"No results will be dumped.\n");
    }

    DumpSystemInfo(&Ctx);

    VmPerfLog(&Ctx, 0, L"Shutting down ...\n");
    SystemTable->BootServices->Stall(DEFAULT_DELAY);

    VmPerfShutdown(&Ctx);

    /* Shutdown the system based on what the user wants */
    SystemTable->RuntimeServices->ResetSystem(
        (gOptions.ResetSystemAtTestEnd) ? EfiResetCold : EfiResetShutdown,
        EFI_SUCCESS,
        0, NULL
    );

    return EFI_SUCCESS;
}
