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

VM_PERF_EVAL_CTX Ctx;
IDENTITY_PT_BUILDER Builder;

BOOLEAN
RealModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize);

BOOLEAN
ProtectedModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize);

BOOLEAN
LongModeTest(
    EFI_SYSTEM_TABLE *SystemTable,
    VM_PERF_EVAL_CTX *Ctx,
    UINT64 AvailableCoresMask,
    VOID *LoadedImageBase,
    UINT64 LoadedImageSize);

EFI_STATUS
EFIAPI
UefiMain (
    IN EFI_HANDLE       ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable
)
{
    UINT64 EnumeratedCpusMask;
    EFI_GUID LoadedImageGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImageProtocol;
    EFI_STATUS Status;

    Status = SystemTable->BootServices->OpenProtocol(
                    ImageHandle,
                    &LoadedImageGUID,
                    (VOID *)&LoadedImageProtocol,
                    ImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
            );


    if (Status != EFI_SUCCESS) {
        VmPerfLog(&Ctx, 0, L"Failed to retrieve loaded image protocol.\n");

        /* Exit out of this test application */
        return EFI_SUCCESS;
    }

    /* Initialize the library */
    VmPerfInitialize(&Ctx);

    EnumeratedCpusMask = VmPerfGetEnumeratedCoreMask(&Ctx);

    if (RealModeTest(SystemTable, &Ctx, EnumeratedCpusMask,
                     LoadedImageProtocol->ImageBase,
                     LoadedImageProtocol->ImageSize)) {
        /* Real mode test passed */
        VmPerfLog(&Ctx, 0, L"Real mode test passed!\n");
    } else {
        VmPerfLog(&Ctx, 0, L"Real mode test failed!\n");
    }

    VmPerfLog(&Ctx, 0, L"Testing Protected Mode...\n");

    if (ProtectedModeTest(SystemTable, &Ctx, EnumeratedCpusMask,
                     LoadedImageProtocol->ImageBase,
                     LoadedImageProtocol->ImageSize)) {
        VmPerfLog(&Ctx, 0, L"Protected Mode test passed!\n");
    } else {
        VmPerfLog(&Ctx, 0, L"Protected Mode test failed!\n");
    }

    if (LongModeTest(SystemTable, &Ctx, EnumeratedCpusMask,
                     LoadedImageProtocol->ImageBase,
                     LoadedImageProtocol->ImageSize)) {
        VmPerfLog(&Ctx, 0, L"Long Mode test passed!\n");
    } else {
        VmPerfLog(&Ctx, 0, L"Long Mode test failed!\n");
    }


    VmPerfShutdown(&Ctx);
    SystemTable->BootServices->Stall(5000000);
    return EFI_SUCCESS;
}
