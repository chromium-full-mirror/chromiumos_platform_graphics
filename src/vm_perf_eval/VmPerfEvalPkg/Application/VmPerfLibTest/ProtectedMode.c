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
#include <IdentityPtLib.h>
#include "Common.h"

/* Defines the size of the fill buffer for the fill test (* 4 KiB) */
#define FILL_PAGES                  4096

/* Defines the number of us to stall after starting a fill on a core */
#define FILL_STALL_US               100000

/* Multiply inputs */
#define MULT_INPUT0                 0x23
#define MULT_INPUT1                 0x4FF

static const UINT8 ProtectedModeCode[] = {
    0x89, 0xe5,                 /* mov ebp, esp*/
    0x60,                       /* pushad */
    0x0f, 0x31,                 /* rdtsc */
    0x52,                       /* push edx */
    0x50,                       /* push eax */
    0x8b, 0x75, 0x04,           /* mov esi, [ebp+4] */
    0x8b, 0x06,                 /* mov eax, [esi] */
    0x8b, 0x5e, 0x04,           /* mov ebx, [esi+4] */
    0xf7, 0xe3,                 /* mul ebx */
    0x89, 0x46, 0x08,           /* mov [esi+8], eax */
    0x89, 0x56, 0x0c,           /* mov [esi+0xC], edx */
    0x8f, 0x46, 0x10,           /* pop dword [esi+0x10]  */
    0x8f, 0x46, 0x14,           /* pop dword [esi+0x14] */
    0x61,                       /* popad */
    0x8b, 0x75, 0x04,           /* mov esi, [ebp+4] */
    0x8b, 0x46, 0x10,           /* mov eax, [esi+0x10] */
    0xc2, 0x04, 0x00            /* ret 4 */
};

/* Fills the specified buffer with incrementing 32-bit values */
static const UINT8 ProtectedModeFillCode[] = {
    0x89, 0xE5,                 /* mov ebp, esp */
    0x8B, 0x75, 0x04,           /* mov esi, [ebp+4] */
    0x8B, 0x3E,                 /* mov edi, [esi] */
    0x8B, 0x4E, 0x04,           /* mov ecx, [esi+4] */
    0x31, 0xC0,                 /* xor eax, eax */
    0x89, 0x07,                 /* 0xC: mov [edi], eax */
    0x40,                       /* inc eax */
    0x83, 0xC7, 0x04,           /* add edi, 4 */
    0x49,                       /* dec ecx */
    0x75, 0xF7,                 /* jnz 0xC */
    0xC2, 0x04, 0x00            /* ret 4 */
};

IDENTITY_PT_BUILDER ProtectedModeBuilder;

typedef struct {
    CONST CHAR16 *Name;
    VM_PERF_CORE_ENTRY_TYPE EntryType;
    IDENTITY_PT_DESC *PtDesc;
    BOOLEAN UsesTrigger;
} PM_TEST_VARIANT;


typedef BOOLEAN
(*SingleCoreTest)(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
);

typedef BOOLEAN
(*MultiCoreTest)(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT64 TargetCpuMask,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
);


static
BOOLEAN
PMMultiCoreTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT64 TargetCpuMask,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
);

static
BOOLEAN
PMSingleCoreFillTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    EFI_PHYSICAL_ADDRESS PMFillCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
);

static
BOOLEAN
PMSingleCoreTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
);

typedef struct {
    CONST CHAR16 *Name;
    SingleCoreTest  SingleCoreTestFunc;
    BOOLEAN UseFillCode;
} PM_SINGLE_CORE_FUNC;


typedef struct {
    CONST CHAR16 *Name;
    MultiCoreTest  MultiCoreTestFunc;
} PM_MULTI_CORE_FUNC;


static const PM_TEST_VARIANT PMVariants[] =
{
    /* Tests without trigger functionality */
    {L"No Paging, No Trigger", CoreEntryProtectedModeNoPaging, NULL,  FALSE},
    {L"32b Paging, No Trigger",
        CoreEntryProtectedModeNormalPaging, &g_X86PmDesc, FALSE},
    {L"PAE Paging, No Trigger",
        CoreEntryProtectedModePAEPaging, &g_X86PaeDesc,  FALSE},

    /* Tests trigger functionality */
    {L"No Paging, Trigger", CoreEntryProtectedModeNoPaging, NULL, TRUE},
    {L"PAE Paging, Trigger",
        CoreEntryProtectedModePAEPaging, &g_X86PaeDesc, TRUE},
    {L"32b Paging, Trigger",
        CoreEntryProtectedModeNormalPaging, &g_X86PmDesc, TRUE},
};

static const UINT32 PMVariantsCount =
    sizeof(PMVariants) / sizeof(PMVariants[0]);


/* "Multicore" Tests: Tests which are capable of testing a set of cores */
static const PM_MULTI_CORE_FUNC MultiCoreTestFuncs[] =
{
    {L"Basic Multicore boot test", PMMultiCoreTest}
};

static const PM_SINGLE_CORE_FUNC SingleCoreTestFuncs[] =
{
    {L"Single core fill test", PMSingleCoreFillTest, TRUE},
    {L"Basic Single core boot test", PMSingleCoreTest, FALSE}
};

static const UINT32 MultiCoreTestFuncCount = sizeof(MultiCoreTestFuncs) /
                                             sizeof(MultiCoreTestFuncs[0]);

static const UINT32 SingleCoreTestFuncCount = sizeof(SingleCoreTestFuncs) /
                                              sizeof(SingleCoreTestFuncs[0]);


static
BOOLEAN
VerifyApResults(
    VM_PERF_EVAL_CTX *Ctx,
    UINT32 TargetCpu
)
{
    BOOLEAN ApResultsOK = FALSE;
    VOID *ApPointer;
    volatile UINT32 *ApPointerU32;

    UINT64 MultiplyResult;
    UINT64 TargetCpuReturnValue;
    UINT64 GeneratedMultiplyResult;

    ApPointer = VmPerfGetApPage(Ctx, TargetCpu);
    ApPointerU32 = (volatile UINT32 *)ApPointer;

    if (ApPointer != NULL) {

        /*
        * Inspect the memory in the target CPU's page and verify
        * that it has been populated with the expected values.
        */
        TargetCpuReturnValue = VmPerfGetReturnValue(Ctx, TargetCpu);

        MultiplyResult = (UINT64)ApPointerU32[2] |
                     (((UINT64)ApPointerU32[3]) << 32);
        GeneratedMultiplyResult = MultU64x64(ApPointerU32[0], ApPointerU32[1]);

        /*
        * The bootstrapper will sign extend 32-bit return results
        * mask off the upper
        * 32-bits of the return value before performing the comparison with
        * the 5th UINT32 (index 4)
        */
        TargetCpuReturnValue &= 0xFFFFFFFFULL;

        if (GeneratedMultiplyResult == MultiplyResult &&
            TargetCpuReturnValue == ApPointerU32[4]) {
            ApResultsOK = TRUE;
        }
    }

    return ApResultsOK;
}

static
BOOLEAN
PMMultiCoreTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT64 TargetCpuMask,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
)
{
    BOOLEAN TestPassed = TRUE;
    UINT64 CurrentCpuMask;
    UINT64 InServiceMask;
    UINT32 CurrentTargetCpu;
    UINT32 CpusInService;
    CORE_ENTRY_INFO EntryInfo;
    VOID *ApPointer;
    volatile UINT32 *ApPointerU32;

    /*
    * If no CPU's are specified, fail the test immediately
    * If we are to use a mode OTHER than NoPaging and Builder is NULL
    */
    if (TargetCpuMask == 0 ||
        (EntryType != CoreEntryProtectedModeNoPaging && Builder == NULL)) {
        return FALSE;
    }

    CurrentCpuMask = TargetCpuMask;
    CpusInService = 0;

    /* Put all the CPU's in service */
    while (CurrentCpuMask) {
        CurrentTargetCpu = __builtin_ffsll(CurrentCpuMask) - 1;
        CurrentCpuMask &= (~(1ULL << CurrentTargetCpu));

        /* Bring 'CurrentTargetCpu' in service */
        InServiceMask =
            VmPerfPutInService(Ctx, CoreTypeIndividual, CurrentTargetCpu);
        if (BitFieldCountOnes64(InServiceMask, 0, 63) != 1) {
            /* Failed to bring this CPU in service */
            return FALSE;
        }

        CpusInService++;
    }

    /* Start the processor's */
    CurrentCpuMask = TargetCpuMask;

    EntryInfo.TriggerMode = (UseTrigger) ?  CoreTriggerWait :
                                            CoreTriggerImmediate;
    EntryInfo.EntryPoint = PMCodeBuffer;
    EntryInfo.EntryType = EntryType;

    while (CurrentCpuMask) {
        CurrentTargetCpu = __builtin_ffsll(CurrentCpuMask) - 1;
        CurrentCpuMask &= (~(1ULL << CurrentTargetCpu));

        if (EntryType == CoreEntryProtectedModeNoPaging) {
            EntryInfo.PtBuilder = NULL;
        } else {
            EntryInfo.PtBuilder = Builder;

            /* Map the code buffer */
            IdentityPtMap(Builder, PMCodeBuffer, 1,
                TAG_CACHE_WB | TAG_ACCESS_RO, 0xFF);
        }

        /*
        * Set up arguments for the multiply
        * to be done on the remote processor
        * */
        ApPointer = VmPerfGetApPage(Ctx, CurrentTargetCpu);
        ApPointerU32 = (volatile UINT32 *)ApPointer;

        /* Let's try and generate a unique result for each core */
        ApPointerU32[0] = 0x4FF;
        ApPointerU32[1] = CurrentTargetCpu;

        if (!VmPerfStartCore(Ctx, CurrentTargetCpu, &EntryInfo)) {
            /* Failed to start this core */
            return FALSE;
        }

        /* Clear out the page table builder for the next core */
        if (EntryType != CoreEntryProtectedModeNoPaging)
            IdentityPtReset(Builder);
    }

    /*
    * If we are to use the trigger, check that each started CPU
    * is waiting on the trigger word write if this is the desirted
    * test.
    *
    * */
    if (UseTrigger) {

        CurrentCpuMask = TargetCpuMask;

        while (CurrentCpuMask) {
            CurrentTargetCpu = __builtin_ffsll(CurrentCpuMask) - 1;
            CurrentCpuMask &= (~(1ULL << CurrentTargetCpu));

            if (!IsCoreInTriggerWait(Ctx, SystemTable, CurrentTargetCpu)) {
                /* We must be in this state ! */
                return FALSE;
            }
        }

        /* If all CPU's are waiting, set the trigger word */
        VmPerfTrigger(Ctx, TRUE);
    }

    /* Check that all CPU's have completed the test */
    CurrentCpuMask = TargetCpuMask;
    while (CurrentCpuMask) {
        CurrentTargetCpu = __builtin_ffsll(CurrentCpuMask) - 1;
        CurrentCpuMask &= (~(1ULL << CurrentTargetCpu));

        if (!IsCoreFinished(Ctx, SystemTable, CurrentTargetCpu)) {
            /* The target CPU must have returned */
            return FALSE;
        }

        /* Verify the result of this processor */
        if (!VerifyApResults(Ctx, CurrentTargetCpu))
            TestPassed = FALSE;
    }

    return TestPassed;
}

static
BOOLEAN
VerifyApFillBuffer(
    EFI_PHYSICAL_ADDRESS FillBuffer,
    UINT32 FillBufferSizeInPages
)
{
    BOOLEAN FillOK = TRUE;
    UINT32 FillBufferSizeInDwords;
    UINT32 i;
    UINT32 *FillBufferU32 = (UINT32 *)FillBuffer;

    FillBufferSizeInDwords =
    FillBufferSizeInPages *
    (EFI_PAGE_SIZE/sizeof(UINT32));

    for (i = 0; i < FillBufferSizeInDwords; i++) {
        if (FillBufferU32[i] != i) {
            FillOK = FALSE;
            break;
        }
    }

    return FillOK;
}

static
BOOLEAN
PMSingleCoreFillTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    EFI_PHYSICAL_ADDRESS PMFillCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
)
{
    BOOLEAN TestPassed = TRUE;
    UINT64 InServiceMask = 0;
    CORE_ENTRY_INFO EntryInfo;
    VOID *ApPointer;
    volatile UINT32 *ApPointerU32;
    EFI_PHYSICAL_ADDRESS FillBuffer;
    EFI_STATUS Status;
    UINT32 AggregationMaskCount = 0;
    UINT32 CurrentAggregationMask;

    if (EntryType != CoreEntryProtectedModeNoPaging &&
        Builder == NULL) {
            return FALSE;
    }

    FillBuffer = BASE_4GB - 1;

    /*
    *  Allocate a sufficiently large enough block of memory
    *  (to test 4K/4M/2M aggregation).
    */
    Status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress,
        EfiLoaderData,
        FILL_PAGES,
        &FillBuffer
    );

    if (Status != EFI_SUCCESS) {
        /* We need this memory in order to do the fill test */
        return FALSE;
    }

    /* We will loop through all possible aggregation mask levels */
    if (Builder) {
        AggregationMaskCount = 1 << Builder->BuilderPtDesc.NumLevels;
    } else {
        /* For no paging mode, only run this test once */
        AggregationMaskCount = 1;
    }

    EntryInfo.EntryPoint = PMFillCodeBuffer;
    EntryInfo.TriggerMode = CoreTriggerImmediate;
    EntryInfo.EntryType = EntryType;
    EntryInfo.PtBuilder = Builder;

    for (CurrentAggregationMask = 0;
         CurrentAggregationMask < AggregationMaskCount;
         CurrentAggregationMask++) {

        /* Bring the core into service */
        InServiceMask = VmPerfPutInService(Ctx, CoreTypeIndividual, TargetCpu);
        if (BitFieldCountOnes64(InServiceMask, 0, 63) != 1) {
            SystemTable->BootServices->FreePages(FillBuffer, FILL_PAGES);
            return FALSE;
        }

        /* Clear the fill buffer */
        SystemTable->BootServices->SetMem((VOID *)FillBuffer,
                                          EFI_PAGES_TO_SIZE(FILL_PAGES),
                                          0);

        if (Builder) {
            IdentityPtReset(Builder);

            /* Map our fill buffer using the current aggregation mask */
            IdentityPtMap(Builder,
                          FillBuffer,
                          FILL_PAGES,
                          TAG_ACCESS_RW | TAG_CACHE_WB,
                          CurrentAggregationMask);

            /* Map the fill test code */
            IdentityPtMap(
                Builder, PMFillCodeBuffer, 1, TAG_ACCESS_RO | TAG_CACHE_WB, 0);

        }

        /* Put the data in the AP page (0 = Pointer, 1 = DWORD count) */
        ApPointer = VmPerfGetApPage(Ctx, TargetCpu);
        ApPointerU32 = (UINT32 *)ApPointer;

        ApPointerU32[0] = (UINT32)FillBuffer;
        ApPointerU32[1] = FILL_PAGES * (EFI_PAGE_SIZE / sizeof(UINT32));

        /*
        * Start the core up. After starting the core, wait for a bit as it
        * may take some time for it to fill the buffer up.
        */
        if (!VmPerfStartCore(Ctx, TargetCpu, &EntryInfo)) {
            /* Fail this call */
            TestPassed = FALSE;
            break;
        }

        /* Stall for 100 ms */
        SystemTable->BootServices->Stall(FILL_STALL_US);

        /*
        * The code should return once it has completed, verify that this
        * is the case.
        */
        if (!IsCoreFinished(Ctx, SystemTable, TargetCpu)) {
            TestPassed = FALSE;
            break;
        }

        /* Verify that the remote core filled up the buffer as expected */
        if (!VerifyApFillBuffer(FillBuffer, FILL_PAGES)) {
            TestPassed = FALSE;
            break;
        }

        /* Prepare the core for the next iteration */
        VmPerfRemoveFromService(Ctx, CoreTypeIndividual, TargetCpu);
    }

    /* Used as a catch all */
    VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);
    SystemTable->BootServices->FreePages(FillBuffer, FILL_PAGES);
    return TestPassed;
}

static
BOOLEAN
PMSingleCoreTest(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 TargetCpu,
    EFI_PHYSICAL_ADDRESS PMCodeBuffer,
    BOOLEAN UseTrigger,
    VM_PERF_CORE_ENTRY_TYPE EntryType,
    IDENTITY_PT_BUILDER *Builder
)
{
    BOOLEAN TestPassed = TRUE;
    UINT64 InServiceMask = 0;
    CORE_ENTRY_INFO EntryInfo;
    VOID *ApPointer;
    volatile UINT32 *ApPointerU32;

    if (EntryType != CoreEntryProtectedModeNoPaging &&
        Builder == NULL) {
            return FALSE;
    }

    InServiceMask = VmPerfPutInService(Ctx, CoreTypeIndividual, TargetCpu);
    if (BitFieldCountOnes64(InServiceMask, 0, 63) != 1) {
        return FALSE;
    }

    /* Get a pointer to the AP page */
    ApPointer = VmPerfGetApPage(Ctx, TargetCpu);
    ApPointerU32 = (volatile UINT32 *)ApPointer;

    if (ApPointer == NULL) {
        return FALSE;
    }

    ApPointerU32[0] = MULT_INPUT0;
    ApPointerU32[1] = MULT_INPUT1;

    EntryInfo.EntryPoint = PMCodeBuffer;
    EntryInfo.EntryType = EntryType;
    EntryInfo.TriggerMode = (UseTrigger) ?
        CoreTriggerWait :
        CoreTriggerImmediate;

    if (EntryType == CoreEntryProtectedModeNoPaging) {
        /* No paging is needed */
        EntryInfo.PtBuilder = NULL;
    } else {
        /* Paging is needed, map our code buffer */
        EntryInfo.PtBuilder = Builder;
        IdentityPtMap(Builder, PMCodeBuffer, 1,
                      TAG_CACHE_WB | TAG_ACCESS_RO, 0xFF);
    }

    /* Fire the core up */
    if (!VmPerfStartCore(Ctx, TargetCpu, &EntryInfo)) {
        /* Core Startup failure */
        return FALSE;
    }

    /* Wait for the core to trigger */
    if (UseTrigger) {

        if (!IsCoreInTriggerWait(Ctx, SystemTable, TargetCpu)) {
            /* We must be in this state ! */
            return FALSE;
        }

        /* Set trigger word */
        VmPerfTrigger(Ctx, TRUE);
    }

    /* Wait for the processor to finish */
    /* Check both the return value, wait for 150ms */
    if (!IsCoreFinished(Ctx, SystemTable, TargetCpu)) {
        return FALSE;
    }

    VerifyApResults(Ctx, TargetCpu);

    VmPerfRemoveFromService(Ctx, CoreTypeIndividual, TargetCpu);
    return TestPassed;
}

/**
 * The protected mode test ensures that the remote cores function
 * correctly when operating in protected mode. This mode is a lot more
 * useful than real mode, but is ultimately still not the main mode
 * intended to be used with this package.
 *
 * Intel's Software Developer Manual, Volume 1 Chapter 3 has details
 * on this. In very basic terms, it is a 32-bit mode and acts like a
 * 32-bit processor.
 *
 * Since protected mode has 3 different paging modes (none, regular and PAE),
 * we test all three to ensure that both the page table builder and the
 * associated boot code function correctly (and can be relied upon to
 * build tests).
 *
 * In addition to the paging modes, we also test both trigger wait and non
 * trigger wait modes. With the trigger wait ensuring that the target core hits
 * the correct state.
 *
 * There are currently two types of tests being run here:
 *
 * 1. A basic return value test, similar to what is done in real mode
 * 2. A buffer fill test to ensure that both cores are accessing the same
 * piece of physical memory (regardless of the paging type).
 *
 * Test 1 is run in both single and multicore variants
 * Test 2 is run in single core variant only.
*/
BOOLEAN
ProtectedModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize)
{
    EFI_PHYSICAL_ADDRESS PMCodeBuffer, PMFillCodeBuffer;
    EFI_STATUS Status;
    UINT64 TagSize;
    UINT32 TargetCpu;
    EFI_PHYSICAL_ADDRESS TagBuffer;
    UINT32 CurrentVariant, CurrentFunc;
    IDENTITY_PT_BUILDER *Builder;
    SingleCoreTest CurrentSingleCoreTestFunc;
    MultiCoreTest CurrentMultiCoreTestFunc;
    BOOLEAN TestPassed = TRUE;
    BOOLEAN UsingTagBuffer = FALSE;

    /* Allocate a memory buffer suitable for use in protected mode code */
    PMCodeBuffer = BASE_4GB - 1;

    Status = SystemTable->BootServices->AllocatePages(
        AllocateMaxAddress, EfiLoaderData, 2, &PMCodeBuffer);

    if (Status != EFI_SUCCESS) {
        /* Need this memory to hold the code */
        VmPerfLog(Ctx, 0, L"Protected Mode Code Buffer Allocation failure.\n");
        return FALSE;
    }

    PMFillCodeBuffer = PMCodeBuffer + EFI_PAGE_SIZE;

    /* Copy over the code */
    SystemTable->BootServices->CopyMem(
        (VOID *)PMCodeBuffer,
        (VOID *)ProtectedModeCode,
        sizeof(ProtectedModeCode));

    SystemTable->BootServices->CopyMem(
        (VOID *)PMFillCodeBuffer,
        (VOID *)ProtectedModeFillCode,
        sizeof(ProtectedModeFillCode)
    );

    TargetCpu = __builtin_ffsll(AvailableCoresMask) - 1;

    for (CurrentFunc = 0;
         CurrentFunc < SingleCoreTestFuncCount;
         CurrentFunc++) {

        VmPerfLog(Ctx, 0, L"%S ...\n",
        SingleCoreTestFuncs[CurrentFunc].Name);

        CurrentSingleCoreTestFunc =
        SingleCoreTestFuncs[CurrentFunc].SingleCoreTestFunc;

        for (
         CurrentVariant = 0;
         CurrentVariant < PMVariantsCount;
         CurrentVariant++)
        {
            VmPerfLog(Ctx, 0, L"%S -> ", PMVariants[CurrentVariant].Name);
            /* Loop through the PM test variants */
            if (PMVariants[CurrentVariant].PtDesc) {
                TagSize = IdentityPtInit(&ProtectedModeBuilder,
                                            PMVariants[CurrentVariant].PtDesc,
                                            32, NULL);

                /* Allocate memory for this PtBuilder */
                Status = SystemTable->BootServices->AllocatePages(
                    AllocateAnyPages, EfiLoaderData,
                    EFI_SIZE_TO_PAGES(TagSize), &TagBuffer);
                if (Status != EFI_SUCCESS) {
                    TestPassed = FALSE;
                    goto PMTestExit;
                }

                TagSize = IdentityPtInit(&ProtectedModeBuilder,
                                        PMVariants[CurrentVariant].PtDesc,
                                        32, (VOID *)TagBuffer);
                Builder = &ProtectedModeBuilder;
                UsingTagBuffer = TRUE;
            } else {
                Builder = NULL;
            }

            if (!CurrentSingleCoreTestFunc(
                Ctx, SystemTable, TargetCpu,
                SingleCoreTestFuncs[CurrentFunc].UseFillCode ?
                PMFillCodeBuffer : PMCodeBuffer,
                PMVariants[CurrentVariant].UsesTrigger,
                PMVariants[CurrentVariant].EntryType,
                Builder
            )) {
                VmPerfLog(Ctx, 0, L"Failed\n");
                TestPassed = FALSE;
                goto PMTestExit;
            } else {
                VmPerfLog(Ctx, 0, L"Passed\n");
            }

            /*
            * Park all cores
            * (catch all even though we are using a single core)
            */
            VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

            /* Free the tag memory if we were to use paging */
            if (PMVariants[CurrentVariant].PtDesc) {
                SystemTable->BootServices->FreePages(
                    TagBuffer,
                    EFI_SIZE_TO_PAGES(TagSize));
                UsingTagBuffer = FALSE;
            }

            /* Clear the trigger memory if we are using trigger */
            if (PMVariants[CurrentVariant].UsesTrigger) {
                VmPerfTrigger(Ctx, FALSE);
            }
        }
    }

    /* Remove all cores from service */
    VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

    for (CurrentFunc = 0;
         CurrentFunc < MultiCoreTestFuncCount;
         CurrentFunc++) {

        VmPerfLog(Ctx, 0, L"%S ...\n",
        MultiCoreTestFuncs[CurrentFunc].Name);

        CurrentMultiCoreTestFunc =
        MultiCoreTestFuncs[CurrentFunc].MultiCoreTestFunc;

        for (CurrentVariant = 0;
         CurrentVariant < PMVariantsCount;
         CurrentVariant++)
        {
            VmPerfLog(Ctx, 0, L"%S -> ", PMVariants[CurrentVariant].Name);

            if (PMVariants[CurrentVariant].PtDesc) {
                TagSize = IdentityPtInit(&ProtectedModeBuilder,
                                        PMVariants[CurrentVariant].PtDesc,
                                        32, NULL);

                /* Allocate memory for this PtBuilder */
                Status = SystemTable->BootServices->AllocatePages(
                    AllocateAnyPages, EfiLoaderData,
                    EFI_SIZE_TO_PAGES(TagSize), &TagBuffer);
                if (Status != EFI_SUCCESS) {
                    TestPassed = FALSE;
                    goto PMTestExit;
                }

                TagSize = IdentityPtInit(&ProtectedModeBuilder,
                                        PMVariants[CurrentVariant].PtDesc,
                                        32, (VOID *)TagBuffer);
                Builder = &ProtectedModeBuilder;
                UsingTagBuffer = TRUE;
            } else {
                Builder = NULL;
            }

            if (!CurrentMultiCoreTestFunc(
                Ctx, SystemTable, AvailableCoresMask,
                PMCodeBuffer,   /* Other test types not supported */
                PMVariants[CurrentVariant].UsesTrigger,
                PMVariants[CurrentVariant].EntryType,
                Builder
            )) {
                VmPerfLog(Ctx, 0, L"Failed\n");
                TestPassed = FALSE;
                goto PMTestExit;
            } else {
                VmPerfLog(Ctx, 0, L"Passed\n");
            }

            /* Park all cores */
            VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

            /* Free the tag memory if we were to use paging */
            if (PMVariants[CurrentVariant].PtDesc) {
                SystemTable->BootServices->FreePages(
                    TagBuffer,
                    EFI_SIZE_TO_PAGES(TagSize)
                );
                UsingTagBuffer = FALSE;
            }

            /* Clear the trigger memory if we are using trigger */
            if (PMVariants[CurrentVariant].UsesTrigger) {
                VmPerfTrigger(Ctx, FALSE);
            }
        }
    }

    /* Easy clean up */
PMTestExit:
    /* Remove all cores from service */
    VmPerfRemoveFromService(Ctx, CoreTypeAll, 0);

    /* Free the tag memory */
    if (UsingTagBuffer)
        SystemTable->BootServices->FreePages(
            TagBuffer,
            EFI_SIZE_TO_PAGES(TagSize)
        );

    SystemTable->BootServices->FreePages(PMCodeBuffer, 2);
    return TestPassed;
}
