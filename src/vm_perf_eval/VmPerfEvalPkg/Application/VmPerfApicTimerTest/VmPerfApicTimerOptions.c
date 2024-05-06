/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <VmPerfEvalLib.h>
#include <Register/Intel/Cpuid.h>
#include <Library/UefiBootServicesTableLib.h>
#include "VmPerfApicTimerApCommon.h"
#include <Library/PrintLib.h>

/* Maximum number of test items that can be specified in the options file */
#define MAX_TEST_ITEMS      64

/* Default to 256K records if not specified by options file */
#define DEFAULT_PER_CORE_BUFFER_SIZE    (256 * 1024)

/* Default to 45 minutes for the poll timeout */
#define DEFAULT_POLL_TIMEOUT        (45)

#define OPTION_FILE_NAME    L"ApicTimerTest"
#define OPTION_CORE_COUNT_NAME  "Cores"
#define OPTION_PER_CORE_RECORD_BUFFER_SIZE "PerCoreRecordBufferSize"
#define OPTION_POLL_TIMEOUT     "PollLoopTimeout"
#define OPTION_CORE_TRIGGER_MODE "CoreTriggerMode"
#define OPTION_RESET_AT_TEST_END    "ResetAtTestEnd"
#define OPTION_ENABLE_DISK_ACCESS "EnableDiskAccesses"
#define OPTION_DISK_BUFFER_SIZE   "DiskBufferSize"

/*
* Our default test item, used when none are specified in the options
* file
*/
APIC_TIMER_TEST_ITEM    gDefaultTestItem;

/*
* We use a simple Finite State Machine to parse a test item line,
* starting with the frequency and moving onto subsequent parts.
* The ItemFlags state continues indefinitely after this as after
* the main parts of the test item have been specified, it is expected
* only flags will be left after.
*/
typedef enum {
    ParseFrequency,
    ParseDutyCycle,
    ParseSampleCount,
    ParseCStateIndex,
    ParseItemFlags
} TEST_ITEM_PARSE_STATE;

/*
* Our structure used to parse a test item, contains the state and the
* resulting test item.
*/
typedef struct {
    /*
    * This is the current state of the Finite State Machine we use to parse
    * the test item. See @TEST_ITEM_PARSE_STATE for more information about
    * the possible states.
    */
    TEST_ITEM_PARSE_STATE State;

    /* This is the test item that results from parsing the option line. */
    APIC_TIMER_TEST_ITEM TestItem;
} TEST_ITEM_PARSE_DATA;

static
VOID
EFIAPI
VmPerfSetItemDefaults(
    APIC_TIMER_TEST_ITEM *TestItem
)
{
    /*
    * 50 % Duty Cycle @ 500Hz -> 1ms per phase, HLT idle
    * Continue until test record buffer is exhausted
    */
    TestItem->CStateIndex = CSTATE_USE_HALT;
    TestItem->DutyCycle = 50 << 8;
    TestItem->Frequency = 500;
    TestItem->SampleCount = MAX_UINT32;
    TestItem->Pad1 = TestItem->Pad2 = 0;
    TestItem->ItemFlags = 0;
}

static
VOID
EFIAPI
VmPerfSetGlobalOptionsDefault(
    APIC_TIMER_TEST_GLOBAL_PARAMS *GlobalOptions
)
{
    GlobalOptions->CoreCount = VM_PERF_MAX_CORES;
    GlobalOptions->PerCoreRecordBufferSize = DEFAULT_PER_CORE_BUFFER_SIZE;
    GlobalOptions->PollLoopTimeoutInMinutes = DEFAULT_POLL_TIMEOUT;

    VmPerfSetItemDefaults(&gDefaultTestItem);
    GlobalOptions->TestItems = &gDefaultTestItem;
    GlobalOptions->TestItemCount = 1;
    GlobalOptions->TestItemArrayShouldBeFreed = FALSE;
    GlobalOptions->CoreTriggerMode = 0;
    GlobalOptions->ResetSystemAtTestEnd = TRUE;
    GlobalOptions->EnableDiskAccesses = FALSE;

    /*
    * The default buffer size is a compromise between a having enough "runway"
    * to keep the disk busy and the memory footprint of the test.
    *
    * Given a typical 512 byte block size, this leaves us with 131072 blocks.
    * We don't want the amount of time spent servicing the disk requests
    * to be too short as we will have difficulty seeing the effect they have
    * on the vCPU's. We also don't want this buffer to be too big as it
    * does affect the memory footprint of the test.
    *
    * A default of 64MiB serves as a good starting point.
    */
    GlobalOptions->DiskBufferSize = 64 * 1048576;
}

static
VOID
EFIAPI
VmPerfResetParseData(
    TEST_ITEM_PARSE_DATA *ParseData
)
{
    VmPerfSetItemDefaults(&ParseData->TestItem);

    /* We start with parsing the frequency */
    ParseData->State = ParseFrequency;
}

static
VOID
EFIAPI
VmPerfParseItemFlag(
    TEST_ITEM_PARSE_DATA *ParseData,
    CHAR8 *ItemString,
    UINT32 ItemStringLength
)
{
    if (AsciiStrCmp(ItemString, "HP_OFF") == 0) {
        ParseData->TestItem.ItemFlags |= ITEM_FLAG_HALT_POLL_OFF;
    } else if (AsciiStrCmp(ItemString, "HP_ON") == 0) {
        ParseData->TestItem.ItemFlags |= ITEM_FLAG_HALT_POLL_ON;
    }
}

static
VOID
EFIAPI
VmPerfParseData(
    TEST_ITEM_PARSE_DATA *ParseData,
    CHAR8 *ItemString,
    UINT32 ItemStringLength
)
{
    UINTN ItemData;

    /* We are using a fairly simple FSM for this */
    switch (ParseData->State) {
        case ParseFrequency:
            ItemData = AsciiStrDecimalToUintn(ItemString);
            if (ItemData <= MAX_UINT16 && ItemData > 0) {
                ParseData->TestItem.Frequency = (UINT16)ItemData;
            }
            ParseData->State = ParseDutyCycle;
            break;

        case ParseDutyCycle:
            ItemData = AsciiStrDecimalToUintn(ItemString);
            if (ItemData <= MAX_UINT16 && ItemData > 0) {
                ParseData->TestItem.DutyCycle = (UINT16)ItemData;
            }
            ParseData->State = ParseSampleCount;
            break;

        case ParseSampleCount:
            ItemData = AsciiStrDecimalToUintn(ItemString);
            if (ItemData <= MAX_UINT32 && ItemData > 0) {
                ParseData->TestItem.SampleCount = (UINT32)ItemData;
            }
            ParseData->State = ParseCStateIndex;
            break;

        case ParseCStateIndex:
            ItemData = AsciiStrDecimalToUintn(ItemString);
            if (ItemData >= 128 && ItemData > 0) {
                ParseData->TestItem.CStateIndex = CSTATE_USE_HALT;
            } else {
                ParseData->TestItem.CStateIndex = ItemData;
            }
            ParseData->State = ParseItemFlags;
            break;

        case ParseItemFlags:
            /*
            * Delegate item flag parsing to this function and
            * do not switch state
            */
            VmPerfParseItemFlag(ParseData, ItemString, ItemStringLength);
            break;
    }
}

static
VOID
EFIAPI
VmPerfExtractItems(
    CHAR8 *ItemString,
    UINT32 ItemStringLength,
    APIC_TIMER_TEST_ITEM *Item
)
{
    CHAR8 ParseBuffer[32];
    UINT32 ItemStringIndex;
    UINT32 ParseBufferIndex;
    TEST_ITEM_PARSE_DATA ParseData;

    /*
    * We are simply going to parse this as a comma separated list of values
    * Said string will be as follows:
    * <Freq>,<DutyCycle>,<Samples>,<CStateIndex>,<1OrMoreCommaSplittedFlags>
    *
    * It is not ideal to parse it this way, but we are working within a very
    * limited environment.
    */

    ItemStringIndex = ParseBufferIndex = 0;

    VmPerfResetParseData(&ParseData);

    while (ItemStringIndex < ItemStringLength) {
        if (ItemString[ItemStringIndex] != ',') {
            ParseBuffer[ParseBufferIndex] = ItemString[ItemStringIndex];
            ParseBufferIndex++;
        } else {
            ParseBuffer[ParseBufferIndex] = 0;

            VmPerfParseData(&ParseData, ParseBuffer, ParseBufferIndex);

            ParseBufferIndex = 0;
        }
        ItemStringIndex++;
    }

    if (ParseBufferIndex > 0) {
        /* Parse the last remaining item */
        ParseBuffer[ParseBufferIndex] = 0;
        VmPerfParseData(&ParseData, ParseBuffer, ParseBufferIndex);
    }

    /* Copy the parsed memory item over */
    gBS->CopyMem(Item, &ParseData.TestItem, sizeof(ParseData.TestItem));
}

UINT32
EFIAPI
VmPerfApicParseTestItems(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    OUT APIC_TIMER_TEST_ITEM **OutputItemPointer
)
{
    UINT32 ValidItems, FoundItems, i;
    CHAR16 ItemName[32];
    CHAR8 FinalItemName[32];
    CHAR8 TestItemString[128];
    APIC_TIMER_TEST_ITEM *ItemArray;
    EFI_PHYSICAL_ADDRESS ItemArrayAddress;

    ValidItems = FoundItems = 0;
    *OutputItemPointer = NULL;

    /*
    * First we scan for any valid items and count them.
    * If we find at least one, the set specified
    * within the options file will supersede the default.
    */
    for (i = 0; i < MAX_TEST_ITEMS; i++) {
        UnicodeSPrint(
            ItemName, sizeof(ItemName),
            L"Item%u", i
        );

        UnicodeStrToAsciiStrS(
            ItemName, FinalItemName, sizeof(FinalItemName)
        );

        /* Try and probe for this option */
        if (VmPerfGetOptionString(Ctx, FinalItemName, NULL, 0)) {
            FoundItems++;
        }
    }

    if (FoundItems > 0) {
        /* Allocate memory for the test item buffer */
        if (SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            EFI_SIZE_TO_PAGES(FoundItems * sizeof(APIC_TIMER_TEST_ITEM)),
            &ItemArrayAddress
        ) != EFI_SUCCESS) {
            VmPerfLog(Ctx, 0, L"Failed to allocate memory for %u test items.\n",
                FoundItems);
            return 0;
        }

        ItemArray = (APIC_TIMER_TEST_ITEM *)ItemArrayAddress;

        /* Parse each individual test item */
        for (i = 0; i < MAX_TEST_ITEMS; i++) {
            UnicodeSPrint(
                ItemName, sizeof(ItemName),
                L"Item%u", i
            );

            UnicodeStrToAsciiStrS(
                ItemName, FinalItemName, sizeof(FinalItemName)
            );

            /* Retrieve the option string for this particular item */
            if (VmPerfGetOptionString(Ctx, FinalItemName,
                    TestItemString, sizeof(TestItemString)
                )) {

                VmPerfExtractItems(
                    TestItemString,
                    AsciiStrLen(TestItemString),
                    &ItemArray[ValidItems]);

                ValidItems++;
            }
        }

        *OutputItemPointer = ItemArray;
    }

    return ValidItems;
}

BOOLEAN
VmPerfApicTimerGetGlobalOptions(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN EFI_SYSTEM_TABLE *SystemTable,
    IN APIC_TIMER_TEST_GLOBAL_PARAMS *GlobalOptions
)
{
    BOOLEAN Vret = FALSE;
    BOOLEAN OptionsLoaded = FALSE;
    UINT32 ItemsFound;
    APIC_TIMER_TEST_ITEM *Items;
    UINTN GenericFlag;

    /* Reset to known defaults */
    VmPerfSetGlobalOptionsDefault(GlobalOptions);
    OptionsLoaded = VmPerfLoadOptions(Ctx, OPTION_FILE_NAME);

    if (OptionsLoaded) {
        /* Extract any user specified test items */
        ItemsFound = VmPerfApicParseTestItems(
            Ctx,
            SystemTable,
            &Items
        );

        /*
        * Check if we found any test items from the options file.
        * If we don't, just leave the default as is.
        */
        if (ItemsFound > 0 && Items) {
            GlobalOptions->TestItemCount = ItemsFound;
            GlobalOptions->TestItems = Items;
            GlobalOptions->TestItemArrayShouldBeFreed = TRUE;
        }

        /* Retrieve the desired options from the file */
        VmPerfGetOptionUintn(
            Ctx,
            OPTION_CORE_COUNT_NAME,
            &GlobalOptions->CoreCount
        );

        VmPerfGetOptionUintn(
            Ctx,
            OPTION_PER_CORE_RECORD_BUFFER_SIZE,
            &GlobalOptions->PerCoreRecordBufferSize
        );

        VmPerfGetOptionUintn(
            Ctx,
            OPTION_POLL_TIMEOUT,
            &GlobalOptions->PollLoopTimeoutInMinutes
        );

        /* Fetch the Boolean reset at end flag */
        GenericFlag = 1;
        VmPerfGetOptionUintn(
            Ctx,
            OPTION_RESET_AT_TEST_END,
            &GenericFlag
        );
        GlobalOptions->ResetSystemAtTestEnd = (GenericFlag) ? TRUE : FALSE;

        VmPerfGetOptionUintn(
            Ctx,
            OPTION_CORE_TRIGGER_MODE,
            &GlobalOptions->CoreTriggerMode
        );

        VmPerfGetOptionUintn(
            Ctx,
            OPTION_DISK_BUFFER_SIZE,
            &GlobalOptions->DiskBufferSize
        );

        /* Enable disk access flag */
        GenericFlag = 0;
        VmPerfGetOptionUintn(
            Ctx,
            OPTION_ENABLE_DISK_ACCESS,
            &GenericFlag
        );
        GlobalOptions->EnableDiskAccesses = (GenericFlag) ? TRUE : FALSE;
    }

    Vret = TRUE;
    return Vret;
}
