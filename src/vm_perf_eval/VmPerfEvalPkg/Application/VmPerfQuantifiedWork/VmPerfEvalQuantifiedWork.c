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
#include "VmPerfEvalQuantifiedWorkAp.h"
#include <Register/Intel/Cpuid.h>

/* A delay to give the user time to read the output */
#define DEFAULT_DELAY       5000000

#define OPTION_FILE_NAME    L"QuantifiedWork"
#define OPTION_CORE_COUNT_NAME  "Cores"
#define OPTION_SECONDS_PER_REPORT_NAME "SecondsPerReport"
#define OPTIONS_TEST_DURATION_NAME "TestDuration"
#define OPTIONS_USE_APERF_MPERF "UseAperfMPerf"

UINT32 gCurrentSecondsCount = 0;

/* A cache of iteration counts grabbed from each core */
UINT64 gIterationCounts[2][VM_PERF_MAX_CORES];
UINT32 gIterationArrayIndex = 0;       /* 0 or 1 */

/* The CSV file that contains the output */
VM_PERF_FILE gCsvFile;

/* The address of the sum page used for the main processor */
EFI_PHYSICAL_ADDRESS gMyPage;

/* Test parameter variables */
UINTN gTestSeconds = (15 * 60);
UINTN gSecondsPerReport = 60;
UINTN gCoreCount = VM_PERF_MAX_CORES;
BOOLEAN gCanUseAperfMperf = FALSE;
BOOLEAN gVirtualized = FALSE;

/**
 * A check to see if the APERF/MPERF MSR's can be read.
 * If they can, we can get a rough idea of the CPU's frequency
 * We can't do so in a virtualized environment at the moment
 * unforunately. It probably wouldn't be a good idea unless
 * the vCPU threads are pinned anyway.
*/
BOOLEAN
EFIAPI
CanUseAperfMerf()
{
    BOOLEAN CanUse = FALSE;
    UINT32 Eax;
    CPUID_THERMAL_POWER_MANAGEMENT_ECX TpmEcx;

    AsmCpuid(CPUID_SIGNATURE, &Eax, NULL, NULL, NULL);
    if (Eax >= CPUID_THERMAL_POWER_MANAGEMENT) {
        AsmCpuid(
            CPUID_THERMAL_POWER_MANAGEMENT, NULL, NULL, &TpmEcx.Uint32, NULL);
        CanUse = (TpmEcx.Bits.HardwareCoordinationFeedback);
    }

    return CanUse;
}

BOOLEAN
EFIAPI
IsVirtualized()
{
    BOOLEAN Virtualized = FALSE;
    UINT32 Eax;
    CPUID_VERSION_INFO_ECX VersionInfoEcx;

    AsmCpuid(CPUID_SIGNATURE, &Eax, NULL, NULL, NULL);
    if (Eax >= CPUID_VERSION_INFO) {
        AsmCpuid(
            CPUID_VERSION_INFO, &Eax, NULL, &VersionInfoEcx.Uint32, NULL);
        Virtualized = (VersionInfoEcx.Bits.ParaVirtualized);
    }

    return Virtualized;
}

double
EFIAPI
GetFrequency(
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 APerfMsrDelta,
    UINT64 MPerfMsrDelta
)
{
    double APerf = (double)APerfMsrDelta;
    double MPerf = (double)MPerfMsrDelta;

    /* APerf/MPerf * C0 frequency gives CPU frequency */
    return (double)Ctx->EstimatedTscFrequency * (APerf/MPerf);
}

/*
* Each AP will sample the TSC when it enters the entry point.
* Dump this out to the log.
*/
VOID
DumpEntryTscs (
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 CoresRunningMask
)
{
    UINT32 CurrentCoreId;
    volatile VM_PERF_QUANTIFIED_WORK_AP *QuantifiedWorkAp;

    VmPerfLog(Ctx, 0, L"Core Entry TSC's:\n");

    while (CoresRunningMask) {
        CurrentCoreId = __builtin_ffsll(CoresRunningMask) - 1;

        QuantifiedWorkAp =
            (volatile VM_PERF_QUANTIFIED_WORK_AP *)VmPerfGetApPage(
            Ctx, CurrentCoreId);

        VmPerfLog(Ctx, 0, L"Core %u: %025lu\n",
            CurrentCoreId, QuantifiedWorkAp->EntryPointTsc);
        CoresRunningMask &= (~(1ULL << CurrentCoreId));
    }
}

/**
 * This function collects all the data from the running AP's and dumps
 * their information to the log.
*/
BOOLEAN
DumpIterations (
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 CoresRunningMask,
    EFI_PHYSICAL_ADDRESS *WorkPageArray
)
{
    UINT32 CurrentCoreId;
    UINT64 PrintCoresMask = CoresRunningMask;
    UINT64 TotalIterationsPerPass;
    UINT64 TotalIterationsCompleted;
    UINT64 CoreIterations;
    UINT64 CoreIterationDelta;
    volatile VM_PERF_QUANTIFIED_WORK_AP *QuantifiedWorkAp;
    double CoreFrequency;
    UINT32 WholeFrequency;
    UINT32 PartFrequency;

    /*
    * Each and every page must have a non-zero IterationCount in
    * order to print it out
    */
    while (PrintCoresMask) {
        CurrentCoreId = __builtin_ffsll(PrintCoresMask) - 1;

        QuantifiedWorkAp =
            (volatile VM_PERF_QUANTIFIED_WORK_AP *)VmPerfGetApPage(
            Ctx, CurrentCoreId);

        if (QuantifiedWorkAp->CurrentIterationCount != 0)
            gIterationCounts[gIterationArrayIndex][CurrentCoreId] =
                QuantifiedWorkAp->CurrentIterationCount;
        else {
            /* Exit early if any of the expected cores are 0 */
            return FALSE;
        }

        PrintCoresMask &= (~(1ULL << CurrentCoreId));
    }

    /* We have samples available for every active processor, report them */
    gCurrentSecondsCount += gSecondsPerReport;

    VmPerfLog(Ctx, 0, L"Iterations @ %u seconds\n", gCurrentSecondsCount);
    TotalIterationsPerPass = TotalIterationsCompleted = 0;

    /* Seconds field in the CSV (first one) */
    VmPerfWriteFileF(
        Ctx, &gCsvFile, L"%u,", gCurrentSecondsCount
    );

    /* Dump the iteration count into the CSV */
    PrintCoresMask = CoresRunningMask;
    while (PrintCoresMask) {
        CurrentCoreId = __builtin_ffsll(PrintCoresMask) - 1;

        QuantifiedWorkAp =
            (volatile VM_PERF_QUANTIFIED_WORK_AP *)VmPerfGetApPage(
                Ctx, CurrentCoreId);

        QuantifiedWorkAp->CurrentIterationCount = 0;

        /* Grab and calculate any information we want to print out */
        CoreIterations =
            gIterationCounts[gIterationArrayIndex][CurrentCoreId];

        CoreIterationDelta =
            CoreIterations -
            gIterationCounts[gIterationArrayIndex ^ 1][CurrentCoreId];

        /* Dump the output to the log/screen */
        VmPerfLog(Ctx, 0, L"Core %u [AP%u]: %025lu (+%lu)\n",
            CurrentCoreId, Ctx->AcpiCores[CurrentCoreId].ApicId,
            CoreIterations,
            CoreIterationDelta);

        /* Write to the CSV */
        VmPerfWriteFileF(Ctx, &gCsvFile, L"%lu,", CoreIterationDelta);
        TotalIterationsPerPass += CoreIterationDelta;
        TotalIterationsCompleted+= CoreIterations;

        /* We have serviced this core, remove it from the index */
        PrintCoresMask &= (~(1ULL << CurrentCoreId));
    }

    /* Dump the frequency if we can use APERF/MPERF */
    if (gCanUseAperfMperf) {
        PrintCoresMask = CoresRunningMask;

        while (PrintCoresMask) {
            CurrentCoreId = __builtin_ffsll(PrintCoresMask) - 1;

            QuantifiedWorkAp =
                (volatile VM_PERF_QUANTIFIED_WORK_AP *)VmPerfGetApPage(
                    Ctx, CurrentCoreId);

            if (QuantifiedWorkAp->APerf != 0 ||
                QuantifiedWorkAp->MPerf != 0)
            {
                CoreFrequency = GetFrequency(
                    Ctx,
                    QuantifiedWorkAp->APerf,
                    QuantifiedWorkAp->MPerf
                );
            } else {
                CoreFrequency = 0;
            }

            /* Split up the frequency */
            WholeFrequency = (UINT32)(CoreFrequency / 1000000.0);
            PartFrequency = ((UINT64)CoreFrequency % 1000000);

            /* Dump the output to the log/screen */
            VmPerfLog(Ctx, 0, L"Core %u [AP%u] MHz: %u.%u\n",
                CurrentCoreId, Ctx->AcpiCores[CurrentCoreId].ApicId,
                WholeFrequency,
                PartFrequency);

            /* Write to the CSV */
            VmPerfWriteFileF(Ctx, &gCsvFile, L"%u.%u MHz,",
                WholeFrequency,
                PartFrequency);

            /* We have serviced this core, remove it from the index */
            PrintCoresMask &= (~(1ULL << CurrentCoreId));
        }
    }

    /* Log file additions */
    VmPerfLog(Ctx, 0,
        L"Total Iterations: %lu\n", TotalIterationsPerPass);
    VmPerfLog(Ctx, 0,
        L"Total Completed Iterations: %lu\n", TotalIterationsCompleted);

    /* CSV additions */
    VmPerfWriteFileF(
        Ctx, &gCsvFile, L"%lu,", TotalIterationsPerPass
    );
    VmPerfWriteFileF(
        Ctx, &gCsvFile, L"%lu", TotalIterationsCompleted
    );

    VmPerfWriteFileF(
        Ctx, &gCsvFile, L"\n", TotalIterationsCompleted
    );
    VmPerfFlushFile(Ctx, &gCsvFile);

    /* Swap target iteration array index */
    gIterationArrayIndex++;
    gIterationArrayIndex &= 0x1;

    return TRUE;
}

VOID
DumpCsvHeader(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *CsvFile,
    IN UINT64 AvailableCoresMask
)
{
    UINT32 CurrentCore;
    UINT64 FrequencyCoreMask = AvailableCoresMask;

    /* The time will be the first element in the table */
    VmPerfWriteFileF(Ctx, CsvFile, L"Time (seconds), ");

    /**
     * Next we will have the actually number of iterations completed
     * for each core. Print a field out for each core.
    */
    while (AvailableCoresMask) {
        CurrentCore = __builtin_ffs(AvailableCoresMask) - 1;

        VmPerfWriteFileF(
            Ctx, CsvFile, L"Core %u APIC %u, ",
            CurrentCore,
            Ctx->AcpiCores[CurrentCore].ApicId);

        AvailableCoresMask &= (~(1ULL << CurrentCore));
    }

    /* Dump frequency headers if we can make use of them */
    if (gCanUseAperfMperf) {
        while (FrequencyCoreMask) {
            CurrentCore = __builtin_ffs(FrequencyCoreMask) - 1;

            VmPerfWriteFileF(
                Ctx, CsvFile, L"Core %u APIC %u MHz, ",
                CurrentCore,
                Ctx->AcpiCores[CurrentCore].ApicId);

            FrequencyCoreMask &= (~(1ULL << CurrentCore));
        }
    }

    /*
    * The next 2 elements are all core iterations
    * and total iterations completed
    * */
    VmPerfWriteFileF(
        Ctx, CsvFile,
        L"Iterations by all cores, Iterations Completed at Time\n"
    );

    VmPerfFlushFile(Ctx, CsvFile);
}

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE       ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    VM_PERF_EVAL_CTX EvalCtx;
    EFI_PHYSICAL_ADDRESS PtTagBuffer;
    UINT64 PtTagSize;
    UINT32 PaBits, i;
    UINT32 CurrentCoreId;
    EFI_STATUS Status;
    UINT64 CurrentCoresMask;
    UINT64 CoreServiceMask;
    IDENTITY_PT_BUILDER PtBuilder;
    CORE_ENTRY_INFO EntryInfo;
    EFI_GUID LoadedImageGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImageProtocol;
    EFI_PHYSICAL_ADDRESS WorkPages[VM_PERF_MAX_CORES];
    VM_PERF_QUANTIFIED_WORK_AP *QuantifiedWorkAp;

    for (i = 0; i < VM_PERF_MAX_CORES; i++)
        WorkPages[i] = BASE_4GB - 1;

    /* Zero out the memory associated with the iteration data store */
    SystemTable->BootServices->SetMem(
        gIterationCounts, sizeof(gIterationCounts), 0
    );

    /* Obtain some information about the system we are running on */
    gVirtualized = IsVirtualized();

    /*
    * Shut off the watchdog timer
    *
    * If we don't do this, the system will automatically
    * reboot after 5 minutes.
    */
    SystemTable->BootServices->SetWatchdogTimer(
        0, 0, 0, NULL
    );

    /* Get the LoadedImage protocol to map this program */
    Status = SystemTable->BootServices->OpenProtocol(
                                        ImageHandle,
                                        &LoadedImageGUID,
                                        (VOID *)&LoadedImageProtocol,
                                        ImageHandle,
                                        NULL,
                                        EFI_OPEN_PROTOCOL_GET_PROTOCOL);

    if (Status != EFI_SUCCESS) {
        Print(L"Failed to retrieve LoadedImageProtocol.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    if (!VmPerfInitialize(&EvalCtx)) {
        Print(L"Failed to initialize VmPerf library.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    if (VmPerfLoadOptions(&EvalCtx, OPTION_FILE_NAME)) {
        UINTN OptionUseAperfMperf = 0;
        /*
        * Load up all the options here, we assume that the variables
        * will not be touched if the option was not found.
        */

        /* Seconds Per Report */
        VmPerfGetOptionUintn(
            &EvalCtx,
            OPTION_SECONDS_PER_REPORT_NAME,
            &gSecondsPerReport
        );

        /* Test Duration (in seconds) */
        VmPerfGetOptionUintn(
            &EvalCtx,
            OPTIONS_TEST_DURATION_NAME,
            &gTestSeconds
        );

        /* Core count */
        VmPerfGetOptionUintn(
            &EvalCtx,
            OPTION_CORE_COUNT_NAME,
            &gCoreCount
        );

        /* Only check if we can actually use this */
        if (CanUseAperfMerf()) {
            VmPerfGetOptionUintn(
                &EvalCtx,
                OPTIONS_USE_APERF_MPERF,
                &OptionUseAperfMperf
            );

            gCanUseAperfMperf = OptionUseAperfMperf;
        }
    }

    /* Dump out the options used to the log */
    VmPerfLog(&EvalCtx, 0, L"Running with:\n");
    VmPerfLog(&EvalCtx, 0, L"SecondsPerReport = %u s\n", gSecondsPerReport);
    VmPerfLog(&EvalCtx, 0, L"Test time = %u s\n", gTestSeconds);
    VmPerfLog(&EvalCtx, 0, L"Desired AP core count = %u\n", gCoreCount);
    VmPerfLog(&EvalCtx, 0, L"APERF/MPERF is Accessible = %s\n",
        (CanUseAperfMerf()) ? L"YES" : L"NO");
    VmPerfLog(&EvalCtx, 0, L"APERF/MPERF access Enabled = %s\n",
        (gCanUseAperfMperf) ? L"YES" : L"NO");
    VmPerfLog(&EvalCtx, 0, L"Virtualized = %s\n",
        (gVirtualized) ? L"YES" : L"NO");

    PaBits = IdentityPtGetSystemPaBits();

    /* We are going to make use of long mode */
    PtTagSize = IdentityPtInit(&PtBuilder, &g_Pml4Desc, PaBits, NULL);

    /* Allocate the memory */
    PtTagBuffer = BASE_4GB - 1;
    Status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress,
        EfiLoaderData,
        EFI_SIZE_TO_PAGES(PtTagSize),
        &PtTagBuffer
    );

    if (Status != EFI_SUCCESS) {
        VmPerfLog(&EvalCtx, 0, L"Failed to allocate tag buffer.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    PtTagSize = IdentityPtInit(
        &PtBuilder,
        &g_Pml4Desc,
        PaBits,
        (VOID *)PtTagBuffer
    );

    /*
    * Allocate a private page for this core (create similar load for main core)
    */
    gMyPage = BASE_4GB - 1;
    Status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress,
        EfiLoaderData,
        1,
        &gMyPage
    );

    if (Status != EFI_SUCCESS) {
        VmPerfLog(&EvalCtx, 0, L"Failed to allocate local sum page.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    CoreServiceMask = VmPerfPutInService(&EvalCtx, CoreTypeAll, 0);

    /*
    * Adjust the CoreServiceMask based on the active core count
    * specified by the options file.
    */
    if (gCoreCount < 64) {
        UINT64 CoreCountMask = (1ULL << gCoreCount) - 1;

        /* Mask this off */
        CoreServiceMask &= CoreCountMask;
    }

    /* We need at least one other processor to run this test */
    if (CoreServiceMask == 0) {
        VmPerfLog(
            &EvalCtx, 0, L"This test requires at least 1 remote core.\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    /* Sum page allocation for each active core */
    CurrentCoresMask = CoreServiceMask;

    while (CurrentCoresMask) {
        CurrentCoreId = __builtin_ffsll(CurrentCoresMask) - 1;

        /* Allocate a sum page for this particular core */
        Status = SystemTable->BootServices->AllocatePages(
            AllocateMaxAddress,
            EfiLoaderData,
            1,
            &WorkPages[CurrentCoreId]
        );

        if (Status != EFI_SUCCESS) {
            VmPerfLog(&EvalCtx, 0, L"Failed to allocate AP work page\n");
            SystemTable->BootServices->Stall(DEFAULT_DELAY);
            return EFI_SUCCESS;
        }

        CurrentCoresMask &= (~(1ULL << CurrentCoreId));
    }

    /* Core startup loop */
    CurrentCoresMask = CoreServiceMask;

    if (CurrentCoresMask) {
        /* Boot up the cores */
        while (CurrentCoresMask) {
            CurrentCoreId = __builtin_ffsll(CurrentCoresMask) - 1;

            /*
            * We will use long mode and instruct the cores to wait for
            * a trigger write before entering the entry point.
            */
            EntryInfo.TriggerMode = CoreTriggerWait;
            EntryInfo.EntryType = CoreEntryLongMode;
            EntryInfo.EntryPoint = (EFI_PHYSICAL_ADDRESS)QuantifiedWorkApFunc;
            EntryInfo.PtBuilder = &PtBuilder;

            /* Map this code */
            IdentityPtMap(
                &PtBuilder,
                (UINT64)LoadedImageProtocol->ImageBase,
                EFI_SIZE_TO_PAGES(LoadedImageProtocol->ImageSize),
                TAG_ACCESS_RO | TAG_CACHE_WB,
                0xFF
            );

            /* Map the work page for this AP */
            IdentityPtMap(
                &PtBuilder,
                WorkPages[CurrentCoreId],
                1,
                TAG_ACCESS_RW | TAG_CACHE_WB,
                0
            );

            QuantifiedWorkAp = (VM_PERF_QUANTIFIED_WORK_AP *)VmPerfGetApPage(
                &EvalCtx, CurrentCoreId);

            /* Populate the page for this CPU/core */
            QuantifiedWorkAp->WorkPage = (VOID *)WorkPages[CurrentCoreId];
            QuantifiedWorkAp->CurrentIterationCount = 0;
            QuantifiedWorkAp->CanReadAperfMperf = gCanUseAperfMperf;
            QuantifiedWorkAp->TscTicksPerReport =
                (EvalCtx.EstimatedTscFrequency * (UINT64)gSecondsPerReport);

            /* Start the core up (make sure it is waiting for the trigger) */
            if (!VmPerfStartCore(&EvalCtx, CurrentCoreId, &EntryInfo)) {
                VmPerfLog(&EvalCtx, 0,
                    L"Failed to start core index %u\n", CurrentCoreId);
                SystemTable->BootServices->Stall(DEFAULT_DELAY);
                return EFI_SUCCESS;
            }

            IdentityPtReset(&PtBuilder);
            CurrentCoresMask &= (~(1ULL << CurrentCoreId));
        }

        /* Write the trigger page (and start the entry point) */
        VmPerfTrigger(&EvalCtx, TRUE);
    }

    /* Open the CSV file for output */
    if (!VmPerfOpenFileForWrite(
            &EvalCtx,
            &gCsvFile,
            VM_PERF_MAX_CORES,
            L"WorkCsv", NULL, 0, L"csv"
        )
    ) {
        VmPerfLog(&EvalCtx, 0, L"Failed to create output CSV file\n");
    } else {
        /* Write the header to the CSV */
        DumpCsvHeader(
            &EvalCtx, &gCsvFile, CoreServiceMask
        );
    }
    /* Stall for 30 ms */
    SystemTable->BootServices->Stall(30000);

    /* Dump the entry point TSC's of each core */
    DumpEntryTscs(&EvalCtx, CoreServiceMask);

    /* Clear the trigger value */
    VmPerfTrigger(&EvalCtx, FALSE);

    UINT64 TscEnd =
        AsmReadTsc() + (gTestSeconds * EvalCtx.EstimatedTscFrequency);

    while (AsmReadTsc() < TscEnd) {
        /**
         * This loop is essentially a remote CPU observer
         * and a result aggregator and dumper.
         * */
        DumpIterations(&EvalCtx, CoreServiceMask, WorkPages);
        IncrementUint32sInPage((VOID *)gMyPage);
    }

    VmPerfLog(&EvalCtx, 0, L"Test completed! Shutting Down...\n");
    VmPerfCloseFile(&EvalCtx, &gCsvFile);

    /* Shut down the VmPerf library */
    VmPerfShutdown(&EvalCtx);

    /* Stall before shutting down the machine ... */
    SystemTable->BootServices->Stall(DEFAULT_DELAY);

    if (gVirtualized) {
        /*
        * For a virtualized system on CrosVM,
        * the EFI API call does not seem to work.
        *
        * The other cores will have been made idle, so we just need to get the
        * main processor to shut down.
        *
        * We do this by filling a page with 0 and setting the page table
        * pointer on the CPU to that zero filled page. This should cause
        * cascading faults which will force exit the hypervisor. This assumes
        * that the main processor is operating in long mode or any other mode
        * which requires the use of page tables.
        */

        /* We'll repurpose the local sum page */
        SystemTable->BootServices->SetMem((VOID *)gMyPage, EFI_PAGE_SIZE, 0);

        /* We should shut down after CR3 is updated ... */
        AsmWriteCr3(gMyPage);
    } else {
        SystemTable->RuntimeServices->ResetSystem(
            EfiResetShutdown,
            EFI_SUCCESS,
            0,
            NULL
        );
    }

    return EFI_SUCCESS;
}
