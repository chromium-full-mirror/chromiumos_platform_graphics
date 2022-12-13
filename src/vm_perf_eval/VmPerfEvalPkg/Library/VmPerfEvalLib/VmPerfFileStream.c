/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <Uefi.h>
#include <VmPerfEvalLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>

#define     WRITEF_BUFFER_LEN   4096
#define     FILENAME_BUFFER_LEN 128

static CHAR16 gWriteFBuffer[WRITEF_BUFFER_LEN];
static CHAR8 gWriteFAsciiBuffer[WRITEF_BUFFER_LEN];

/* <test>_c<n>_apic<n>_<suffix><num>.<ext> */

BOOLEAN EFIAPI VmPerfOpenFileForWrite(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN UINT32 CoreIndex,
    IN CHAR16 *TestName,
    IN CHAR16 *TestSuffix,
    IN UINT32 SuffixIndex,
    IN CHAR16 *Extension
)
{
    CHAR16 FileName[FILENAME_BUFFER_LEN];
    CHAR16 Temp[64];
    EFI_STATUS Status;

    File->File = NULL;

    /* These are necessary, no point in going further if these checks fail */
    if (TestName == NULL ||
        !Ctx->FileAccessOK)
        return FALSE;

    FileName[0] = 0;
    Temp[0] = 0;

    /* Add the first part of the file name */
    StrCatS(FileName, FILENAME_BUFFER_LEN, TestName);

    /* Append the core id's if needed */
    if (CoreIndex < VM_PERF_MAX_CORES) {
        UnicodeSPrint(
            Temp, sizeof(Temp),
            L"_c%u_apic%u",
            CoreIndex, Ctx->AcpiCores[CoreIndex].ApicId);
        StrCatS(FileName, FILENAME_BUFFER_LEN, Temp);
    }

    /* Append the suffix if needed */
    if (TestSuffix) {
        UnicodeSPrint(
            Temp, sizeof(Temp),
            L"_%s%u",
            TestSuffix, SuffixIndex
        );

        StrCatS(FileName, FILENAME_BUFFER_LEN, Temp);
    }

    StrCatS(FileName, FILENAME_BUFFER_LEN, L".");
    StrCatS(FileName, FILENAME_BUFFER_LEN, Extension);

    Status = Ctx->TestDirectory->Open(
        Ctx->TestDirectory,
        &File->File,
        FileName,
        EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ | EFI_FILE_MODE_CREATE,
        0
    );

    if (Status != EFI_SUCCESS) {
        return FALSE;
    }

    /* Ensure that the file lands on the filesystem immediately */
    File->File->Flush(File->File);

    return TRUE;
}

BOOLEAN EFIAPI VmPerfOpenFileForRead(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN CHAR16 *FilePath
)
{
    EFI_STATUS Status;

    File->File = NULL;

    if (!Ctx->FileAccessOK)
        return FALSE;

    Status = Ctx->RootDirectory->Open(
        Ctx->RootDirectory,
        &File->File,
        FilePath,
        EFI_FILE_MODE_READ, 0
    );

    if (Status != EFI_SUCCESS) {
        return FALSE;
    }

    return TRUE;
}

BOOLEAN EFIAPI VmPerfCloseFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File
)
{
    if (!Ctx->FileAccessOK ||
        !File->File)
        return FALSE;

    File->File->Close(File->File);
    return TRUE;
}

UINTN EFIAPI VmPerfReadFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN VOID *Buffer,
    IN UINT32 BytesToRead
)
{
    EFI_STATUS Status;
    UINTN BufferSize = BytesToRead;

    if (!Ctx->FileAccessOK ||
        !File->File)
        return 0;

    Status = File->File->Read(
        File->File,
        &BufferSize,
        Buffer
    );

    if (Status != EFI_SUCCESS) {
        return 0;
    }

    return BufferSize;
}

UINTN EFIAPI VmPerfWriteFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN VOID *Buffer,
    IN UINT32 BytesToWrite
)
{
    UINTN Bytes = BytesToWrite;
    EFI_STATUS Status;

    if (!Ctx->FileAccessOK ||
        !File->File)
        return 0;

    Status =
    File->File->Write(
        File->File,
        &Bytes,
        Buffer
    );

    if (Status != EFI_SUCCESS)
        return 0;
    else
        return Bytes;
}

UINTN EFIAPI VmPerfWriteFileF(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN CHAR16 *Format,
    ...
)
{
    VA_LIST Marker;
    UINTN LogSize = 0, AsciiSize = 0;

    if (!Ctx->FileAccessOK ||
        !File->File)
        return 0;

    VA_START(Marker, Format);
    LogSize = UnicodeVSPrint(
        gWriteFBuffer,
        WRITEF_BUFFER_LEN,
        Format, Marker
    );
    VA_END(Marker);

    /* Convert this string to ASCII/UTF-8 */
    UnicodeStrnToAsciiStrS(
        gWriteFBuffer, LogSize,
        gWriteFAsciiBuffer, WRITEF_BUFFER_LEN,
        &AsciiSize
    );

    /* Write the data to the file */
    return VmPerfWriteFile(Ctx, File, gWriteFAsciiBuffer, AsciiSize);
}

VOID EFIAPI VmPerfFlushFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File
)
{
    if (!Ctx->FileAccessOK ||
        !File->File)
        return;

    File->File->Flush(
        File->File
    );
}
