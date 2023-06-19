/** @file
*
* Copyright 2023 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include "VmPerfEvalInternal.h"
#include <Library/PrintLib.h>

#define OPTIONS_FILE_NAME_LEN   128
#define OPTION_NAME_LENGTH      128
#define OPTION_VALUE_LENGTH     256
#define OPTIONS_LIMIT           1024

CHAR8 gOptionsPage[EFI_PAGE_SIZE];
UINTN gOptionsLength;
UINT16 gOptionsIndicies[OPTIONS_LIMIT];
UINT16 gOptionsCount = 0;

/* Internal structure for split <option>=<value> pair */
typedef struct
{
    /* The portion before the equal sign */
    CHAR8   OptionName[OPTION_NAME_LENGTH];

    /* The portion after the equal sign */
    CHAR8   Value[OPTION_VALUE_LENGTH];
} OPT_SPLIT;

static
VOID
VmPerfPreProcess()
{
    CHAR8 *gOptionsChar = (CHAR8 *)gOptionsPage;
    CHAR8 *CurrentBase = gOptionsChar;
    CHAR8 *gOptionsEndChar = (CHAR8 *)(gOptionsChar + gOptionsLength);
    CHAR8 *NextLine;

    gOptionsCount = 0;

    /* Put on the NULL terminator */
    gOptionsChar[gOptionsLength] = 0;
    while (CurrentBase < gOptionsEndChar) {
        NextLine = AsciiStrStr(CurrentBase, "\n");
        if (NextLine) {
            *NextLine = 0;          /* Patch out newline for null string */
        }

        /*
        * Do not add if there is no "=" in the line
        * */
        if (AsciiStrStr(CurrentBase, "=")) {
            /* Start index of this (presumed) option=value pair */
            gOptionsIndicies[gOptionsCount] =
                CurrentBase - gOptionsChar;

            gOptionsCount++;
            if (gOptionsCount == OPTIONS_LIMIT) {
                /* We hit the limit, cease processing */
                break;
            }
        }

        /* Skip the patched NULL character */
        if (NextLine) {
            CurrentBase = NextLine+1;
        } else {
            break;
        }
    }
}

BOOLEAN EFIAPI VmPerfLoadOptions(VM_PERF_EVAL_CTX *Ctx, CHAR16 *Name) {
    CHAR16 OptionsFileName[OPTIONS_FILE_NAME_LEN];
    VM_PERF_FILE OptionsFile;
    UINTN BytesRead;

    OptionsFileName[0] = 0;

    StrCatS(OptionsFileName, OPTIONS_FILE_NAME_LEN, Name);
    StrCatS(
        OptionsFileName,
        OPTIONS_FILE_NAME_LEN,
        L".opt"
    );

    /* Open the file */
    if (!VmPerfOpenFileForRead(
        Ctx,
        &OptionsFile,
        OptionsFileName
    )) {
        VmPerfLog(
            Ctx, 0, L"Failed to load options file %s\n", OptionsFileName);
        return FALSE;
    }

    /* Read up to 4095 bytes */
    BytesRead = VmPerfReadFile(
        Ctx,
        &OptionsFile,
        gOptionsPage,
        EFI_PAGE_SIZE - 1
    );

    /* File doesn't have any data? */
    if (BytesRead == 0) {
        return FALSE;
    }

    /*
    * Pre-process the data so it's easier to scan when given subsequent data
    * requests.
    */
    gOptionsLength = BytesRead;
    VmPerfPreProcess();

    /* Print out options that were read from the file */
    VmPerfLog(Ctx, 0, L"%u Options\n", gOptionsCount);

    for (UINT32 i = 0 ; i < gOptionsCount; i++) {
        VmPerfLog(
            Ctx, 0, L"%a\n", (CHAR8 *)&gOptionsPage[gOptionsIndicies[i]] );
    }

    return TRUE;
}

BOOLEAN SplitOptionString(CHAR8 *OptionString, OPT_SPLIT *Split) {
    UINTN NameLength, ValueLength, TotalLength, ValueStartingIndex;
    CHAR8 *EqualSignPointer;

    TotalLength = AsciiStrLen(OptionString);

    /* Try and find the '=' sign */
    EqualSignPointer = AsciiStrStr(OptionString, "=");
    if (EqualSignPointer) {
        /* Calculate the length of the constituent parts */
        NameLength = EqualSignPointer - OptionString;

        /* <OptionName>= */
        if (TotalLength <= (NameLength + 1)) {
            return FALSE;
        }

        /* Skip the '=' sign */
        ValueStartingIndex = NameLength + 1;
        ValueLength = TotalLength - ValueStartingIndex;

        /* Populate the structure */
        gBS->SetMem(Split->OptionName, sizeof(Split->OptionName), 0);
        gBS->SetMem(Split->Value, sizeof(Split->Value), 0);

        AsciiStrnCpyS(Split->OptionName, sizeof(Split->OptionName),
                      (CHAR8 *)OptionString, NameLength);

        AsciiStrnCpyS(
            Split->Value,
            sizeof(Split->Value),
            (CHAR8 *)&OptionString[ValueStartingIndex],
            ValueLength
        );
    }

    return (EqualSignPointer) ? TRUE : FALSE;
}

BOOLEAN GetOption(CHAR8 *OptionsName, OPT_SPLIT *Split) {
    UINTN OptionLength = AsciiStrLen(OptionsName);
    UINT32 i;
    BOOLEAN OptionFound = FALSE;
    CHAR8 *TargetOption;

    if (OptionLength >= OPTION_NAME_LENGTH)
        return FALSE;

    /* Perform the split once we find the option */
    for (i = 0 ; i < gOptionsCount; i++) {
        TargetOption = (CHAR8 *)&gOptionsPage[ gOptionsIndicies[i] ];

        if (!AsciiStrnCmp(OptionsName, TargetOption, OptionLength)) {
            /* We found the option break out */
            OptionFound = TRUE;
            break;
        }
    }

    if (OptionFound) {
        /* Perform the split */
        OptionFound = SplitOptionString(
            TargetOption,
            Split
        );

        if (OptionFound) {
            /* Parse the Value portion and convert the value */
            Print(L"%a -> %a\n", Split->OptionName, Split->Value);
        }
    }

    return OptionFound;
}

BOOLEAN EFIAPI VmPerfGetOptionUintn(
    VM_PERF_EVAL_CTX *Ctx,
    CHAR8 *OptionName,
    UINTN *OptionValue
)
{
    OPT_SPLIT OptionSplit;

    if (GetOption(OptionName, &OptionSplit)) {
        AsciiStrDecimalToUintnS(
            OptionSplit.Value,
            NULL,
            OptionValue
        );
        return TRUE;
    }

    return FALSE;
}

BOOLEAN EFIAPI VmPerfGetOptionString(
    VM_PERF_EVAL_CTX *Ctx,
    CHAR8 *OptionName,
    CHAR8 *OptionStringValue,
    UINT32 OptionStringLength
)
{
    OPT_SPLIT OptionSplit;

    if (GetOption(OptionName, &OptionSplit)) {
        /* Copy the string over */
        if (OptionStringValue && OptionStringLength > 0) {
            AsciiStrnCpyS(
                OptionStringValue,
                OptionStringLength,
                OptionSplit.Value,
                AsciiStrLen(OptionSplit.Value)
            );
        }
        return TRUE;
    }
    return FALSE;
}
