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
#include <Library/UefiRuntimeServicesTableLib.h>
#include "VmPerfEvalInternal.h"
#include <Library/PrintLib.h>


#define LOGGING_SPRINT_BUF_SZ       1024

CHAR16 g_MessageBuffer[LOGGING_SPRINT_BUF_SZ];
CHAR8 g_MessageBufferAscii[LOGGING_SPRINT_BUF_SZ];

BOOLEAN VmPerfLoggingInit(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_STATUS Status;
    Ctx->LogFile = NULL;

    if (!Ctx->FileAccessOK) {
        /* We need file access */
        return FALSE;
    }

    /* Create the log file name (under the test directory) */
    Status = Ctx->TestDirectory->Open(
        Ctx->TestDirectory,
        &Ctx->LogFile,
        VMPERF_LOG_FILE_NAME,
        EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
        0);

    if (Status != EFI_SUCCESS) {
        /* We need this file to be opened .. */
        return FALSE;
    }

    Ctx->LogFile->Flush(Ctx->LogFile);
    return TRUE;
}

VOID VmPerfLoggingShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    if (!Ctx->FileAccessOK ||
        !Ctx->LogFile) {
        return;
    }

    /* Close the log file */
    Ctx->LogFile->Close(Ctx->LogFile);
    Ctx->LogFile = NULL;
}

VOID EFIAPI VmPerfLog(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 LogLevel,
    IN CHAR16 *Msg,
    ...
)
{
    VA_LIST Marker;
    CHAR16 MessageBuffer[1024];
    UINTN MessageSize, MessageAsciiSize;
    UINTN BufferSize;

    VA_START(Marker, Msg);
    MessageSize = UnicodeVSPrint(
        MessageBuffer,
        sizeof(MessageBuffer),
        Msg,
        Marker
    );
    VA_END(Marker);

    /* Print it to the output */
    Print(MessageBuffer);

    if (Ctx->LogFile) {
        /* Also print this to the log file */
        UnicodeStrnToAsciiStrS(
            MessageBuffer,
            MessageSize,
            g_MessageBufferAscii,
            sizeof(g_MessageBufferAscii),
            &MessageAsciiSize
        );

        BufferSize = AsciiStrLen(g_MessageBufferAscii);
        Ctx->LogFile->Write(
            Ctx->LogFile,
            &BufferSize,
            g_MessageBufferAscii
        );

        /* Flush all writes made to the log file */
        Ctx->LogFile->Flush(Ctx->LogFile);
    }
}
