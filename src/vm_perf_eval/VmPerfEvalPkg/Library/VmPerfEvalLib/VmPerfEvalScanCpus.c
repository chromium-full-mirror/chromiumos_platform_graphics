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
#include <IndustryStandard/Acpi10.h>
#include <VmPerfEvalApicLib.h>

typedef struct {
    UINT32 RemainingLength;
    UINT8 *Pointer;
} ApicTableReader;

static VOID SetAcpiCoreDefault(VM_PERF_EVAL_ENUM_CPU *CpuCtxt)
{
    CpuCtxt->AcpiUid = CpuCtxt->ApicId = 0;
    CpuCtxt->CorePageTable = CpuCtxt->CorePcibPage =
    CpuCtxt->CoreApPage = 0;
    CpuCtxt->CorePrivBlock = 0;

    CpuCtxt->CoreStackPages =
    CpuCtxt->CorePrivBlockPages =
    CpuCtxt->CoreInterruptStackPages = 0;

    CpuCtxt->CoreStack =
    CpuCtxt->CoreExceptionStack =
    CpuCtxt->CoreInterruptStack =
    CpuCtxt->CoreNMIStack = 0;

    CpuCtxt->CoreType = CoreTypeIndividual;
    CpuCtxt->InService = FALSE;
    CpuCtxt->CoreIsRunning = FALSE;
    CpuCtxt->PageTablePages = 0;
}

VOID InitApicReader(ApicTableReader *Reader, EFI_ACPI_COMMON_HEADER *hdr)
{
    Reader->RemainingLength = hdr->Length;
    Reader->Pointer = (UINT8 *)(hdr+=1);
}

UINT8 Read8(ApicTableReader *Reader)
{
    UINT8 Val = *Reader->Pointer;
    Reader->Pointer++;
    Reader->RemainingLength--;

    return Val;
}

UINT16 Read16(ApicTableReader *Reader)
{
    UINT16 Val = *(UINT16 *)Reader->Pointer;
    Reader->Pointer += 2;
    Reader->RemainingLength-=2;

    return Val;
}

UINT32 Read32(ApicTableReader *Reader)
{
    UINT32 Val = *(UINT32 *)Reader->Pointer;

    Reader->Pointer+= 4;
    Reader->RemainingLength-=4;

    return Val;
}

VOID ReadDiscard(ApicTableReader *Reader, UINT32 Bytes)
{
    Reader->Pointer += Bytes;

    if (Reader->RemainingLength < Bytes) {
        Reader->RemainingLength = 0;
    } else {
        Reader->RemainingLength -= Bytes;
    }
}

static UINT32 ScanCpus(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_ACPI_COMMON_HEADER *Apic
)
{
    UINT32 CpuCount = 0;
    ApicTableReader Reader;
    UINT32 i;
    VOID *SubTablePointer;
    UINT8 TableId, TableLength;

    /* Table structure Definitions */
    EFI_ACPI_1_0_PROCESSOR_LOCAL_APIC_STRUCTURE *LocalApic;

    /* Just an attempt to make this less painful */
    InitApicReader(&Reader, Apic);

    /* Revision */
    Read8(&Reader);

    /* Checksum */
    Read8(&Reader);

    /* OEM ID */
    for (i = 0 ; i < 6; i++) {
        Read8(&Reader);
    }

    /* Manufacturer */
    for (i = 0 ; i < 8; i++) {
        Read8(&Reader);
    }

    /* OEM Revision */
    Read32(&Reader);

    /* Creator ID */
    Read32(&Reader);

    /* Creator Rev */
    Read32(&Reader);

    /* LAPIC Base Address */
    Ctx->LapicAddress = Read32(&Reader);

    /* Multi APIC Flags */
    Read32(&Reader);

    /* Now we are parsing sub-tables within the ACPI APIC table */
    while (Reader.RemainingLength > 0 && CpuCount < VM_PERF_MAX_CORES) {
        SubTablePointer = (VOID *)Reader.Pointer;
        TableId = Read8(&Reader);
        TableLength = Read8(&Reader);

        switch (TableId) {
            case EFI_ACPI_1_0_PROCESSOR_LOCAL_APIC:
                LocalApic =
                (EFI_ACPI_1_0_PROCESSOR_LOCAL_APIC_STRUCTURE *)SubTablePointer;

                if (LocalApic->Flags & EFI_ACPI_1_0_LOCAL_APIC_ENABLED &&
                    LocalApic->ApicId !=
                    VmPerfEvalApicGetApicId(Ctx->LapicAddress)) {

                    /* Add this CPU to the array */
                    SetAcpiCoreDefault(&Ctx->AcpiCores[CpuCount]);

                    Ctx->AcpiCores[CpuCount].ApicId =
                        LocalApic->ApicId;
                    Ctx->AcpiCores[CpuCount].AcpiUid =
                        LocalApic->AcpiProcessorId;

                    /* Send an INIT to the Core */
                    VmPerfEvalApicSendInit(Ctx->LapicAddress,
                                           LocalApic->ApicId);
                    CpuCount++;
                }
                break;

            default:
                break;
        }

        /* We should account for the 2 bytes we already read */
        ReadDiscard(&Reader, TableLength - 2);
    }

    return CpuCount;
}

#define TSC_SAMPLE_FREQ_COUNT 4

UINT32 VmPerfEvalScanCpus(
    IN VM_PERF_EVAL_CTX *Ctx
)
{
    EFI_ACPI_COMMON_HEADER *ApicTable;
    UINT64 Tsc0, Tsc1;
    UINT64 Freqs[TSC_SAMPLE_FREQ_COUNT];
    UINT64 SumFreqs = 0;
    UINT32 i;

    /*
    * If we can't find an APIC table, we cannot make use
    * of any other CPU's in the system.
    */
    ApicTable = EfiLocateFirstAcpiTable(SIGNATURE_32('A', 'P', 'I', 'C'));
    if (!ApicTable)
        return 0;

    /* Estimate the TSC frequency */
    for (i = 0 ; i < TSC_SAMPLE_FREQ_COUNT; i++) {
        Tsc0 = AsmReadTsc();
        gBS->Stall(200000);
        Tsc1 = AsmReadTsc();

        Freqs[i] = (Tsc1 - Tsc0) * 5;
        SumFreqs += Freqs[i];
    }

    Ctx->EstimatedTscFrequency = (SumFreqs / TSC_SAMPLE_FREQ_COUNT);
    return ScanCpus(Ctx, ApicTable);
}
