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

/* A delay to give the user time to read the output */
#define DEFAULT_DELAY       5000000

/* The name of the options file we are to load */
#define OPTION_FILE_NAME    L"FullLoad"

/*
* The option name representing the number of cores/APs
* that should be spun up.
*
* Note that this does NOT include the main core (the one that runs UefiMain).
*
* For a 4 core/4 thread CPU (3 APs):
*
* Cores=4 will result in 3 APs being spun up
* Cores=2 will result in 2 APs being spun up
*
* For a 16core/32 thread CPU (31 APs):
*
* Cores=16 will result in 16 APs being spun up
* Cores=32 will result in 31 APs being spun up
*/
#define OPTION_CORE_COUNT_NAME  "Cores"

/* Our context structure */
VM_PERF_EVAL_CTX Ctx;

/* Our page table builder structure */
IDENTITY_PT_BUILDER Builder;

/*
* This code in this function is not executed by the main processor,
* but by all the processors started up by the code below.
*
* Any code which is to serve as an entry point by other processors
* *MUST* be marked with the EFIAPI attribute in order to compile it
* with the correct ABI.
*
* The proper way to deal with this separation is to have code
* to be executed by the AP in a separate file (or even directory).
* This is done mainly as a quick example (and a test "driver").
*/
UINT64
EFIAPI
ApLoop (VOID *ApPage)
{
    UINT32 count = 0;

    /* Infinite spin loop */
    while (1) {
        count++;
    }

    /* Should never come here */
    return 0;
}

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE       ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    EFI_STATUS Status;
    EFI_TPL LastTpl;
    UINT64 TagBufferSize;
    UINT32 CpusBoughtUp, i, CpusInServiceCount, CurrentCpu;
    UINT64 CpusInServiceMask;
    EFI_GUID LoadedImageGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImageProtocol;
    CORE_ENTRY_INFO Info;
    UINT32 SystemPaBits;
    UINTN CoreCount = VM_PERF_MAX_CORES;
    UINT64 CoreServiceMask;

    /* Initialize the library */
    if (!VmPerfInitialize(&Ctx)) {
        Print(L"VmPerfInitialize failed!\n");
        SystemTable->BootServices->Stall(DEFAULT_DELAY);
        return EFI_SUCCESS;
    }

    /*
    * We will try and load the options file and parse it
    * to see how many cores the user wants to spin in a loop
    */
    if (VmPerfLoadOptions(&Ctx, OPTION_FILE_NAME)) {
        /* Determine how many cores the user wants */
        if (VmPerfGetOptionUintn(&Ctx, OPTION_CORE_COUNT_NAME, &CoreCount)) {

            CoreServiceMask = MAX_UINT64;

            if (CoreCount < 64) {
                CoreServiceMask = (1ULL << CoreCount) - 1;
            }
        }
    }

    /* Bring all available AP's into service */
    CpusInServiceMask = VmPerfPutInService(&Ctx, CoreTypeAll, 0);

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

    /* Disable the Watchdog Timer */
    SystemTable->BootServices->SetWatchdogTimer(0, 0, 0, NULL);

    /*
    * Here is where we take into account the options set by the user.
    * We will and the CpusInServiceMask with the mask obtained from the
    * options core count. We can limit the number of cores started this
    * way.
    */
    CpusInServiceMask &= CoreServiceMask;

    CpusBoughtUp = 0;
    CpusInServiceCount = BitFieldCountOnes64(CpusInServiceMask, 0, 63);

    /* We enter the ApLoop above, in long mode, with no trigger */
    Info.EntryPoint = (EFI_PHYSICAL_ADDRESS)ApLoop;
    Info.EntryType = CoreEntryLongMode;
    Info.PtBuilder = &Builder;
    Info.TriggerMode = CoreTriggerImmediate;

    for (i = 0; i < CpusInServiceCount; i++) {
        CurrentCpu = __builtin_ctzll(CpusInServiceMask);

        /* Map the code for this image as read only */
        IdentityPtMap(&Builder,
                        (UINT64)LoadedImageProtocol->ImageBase,
                        EFI_SIZE_TO_PAGES(LoadedImageProtocol->ImageSize),
                        TAG_CACHE_WB | TAG_ACCESS_RO, 0xFF);

        if (VmPerfStartCore(&Ctx, CurrentCpu, &Info))
            CpusBoughtUp++;

        /* Clear all mappings, we want to boot up the next core */
        IdentityPtReset(&Builder);

        /* Remove this one from the mask */
        CpusInServiceMask &= (~(1ULL << CurrentCpu));
    }

    Print(L"%u/%u AP(s) spun up.\n",
          CpusBoughtUp, CpusInServiceCount);

    /*
    * Stall this processor in an infinite
    * loop now that the others are booted
    *
    * Raise the TPL and disable interrupts
    * (no guest induced VM-exits from this core as well)
    * Do this just before the infinite loop and after
    * initialization so interrupts are not re-enabled.
    *
    */
    LastTpl = SystemTable->BootServices->RaiseTPL(TPL_HIGH_LEVEL);
    DisableInterrupts();
    while (1) {

    }

    VmPerfShutdown(&Ctx);
    return EFI_SUCCESS;
}
