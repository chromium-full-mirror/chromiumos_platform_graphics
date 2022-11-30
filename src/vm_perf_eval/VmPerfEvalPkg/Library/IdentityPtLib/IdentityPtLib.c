/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <IdentityPtLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

/* A structure used to hold the "broken down" address */
typedef struct
{
    UINT32 Index[MAX_LEVELS];
} IDENTITY_PT_PART_ADDR;


static UINT64 IdentityPtCalcTagIndex (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *Addr,
    IN UINT32 Level
)
{
    UINT64 Index = 0;
    UINT32 i;

    if (Level == 0) {
        return Addr->Index[0];
    } else {
        Index = Addr->Index[0];

        for (i = 1; i <= Level; i++) {
            Index =
                ((Index * Builder->BuilderPtInfo.Info[i].PopulatedEntries)
                 + (Addr->Index[i]));
        }
    }

    return (Index + Builder->BuilderPtInfo.Info[Level].BufferOffset);
}

static BOOLEAN IdentityPtRemainingLevelsZero (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *Addr,
    IN UINT32 Level
)
{
    BOOLEAN LevelsZero = TRUE;
    for (UINT32 i = (Level+1); i < Builder->BuilderPtDesc.NumLevels; i++) {
        if (Addr->Index[i] != 0) {
            LevelsZero = FALSE;
            break;
        }
    }

    return LevelsZero;
}

static VOID IdentityPtPartitionAddr (
    IN IDENTITY_PT_BUILDER *Builder,
    OUT IDENTITY_PT_PART_ADDR *Addr,
    IN UINT64 Address
)
{
    UINT32 i;

    for (i = 0; i < MAX_LEVELS; i++) {
        IDENTITY_PT_LEVEL_INFO *LvlInfo = &Builder->BuilderPtInfo.Info[i];

        if (i < Builder->BuilderPtDesc.NumLevels) {
            Addr->Index[i] = (Address & LvlInfo->VaMask) >> LvlInfo->ShiftBits;
        } else {
            Addr->Index[i] = 0;
        }
    }
}

static UINT64 IdentityPtJoinAddr (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *Addr
)
{
    UINT32 i;
    UINT64 JoinedAddr = 0;

    for (i = 0; i < Builder->BuilderPtDesc.NumLevels; i++) {
        IDENTITY_PT_LEVEL_INFO *LvlInfo = &Builder->BuilderPtInfo.Info[i];

        JoinedAddr |= (((UINT64)Addr->Index[i] << LvlInfo->ShiftBits)
                       & LvlInfo->VaMask);
    }

    return JoinedAddr;
}

static VOID IdentityPtStepPartAddr (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *Addr,
    IN UINT32 Level
)
{
    INT32 CurrentLevel = Level;

    while (CurrentLevel >= 0) {
        IDENTITY_PT_LEVEL_INFO *LvlInfo =
            &Builder->BuilderPtInfo.Info[CurrentLevel];

        if (Addr->Index[CurrentLevel] == (LvlInfo->PopulatedEntries - 1)) {
            Addr->Index[CurrentLevel] = 0;
            /* We simulate a carry by allowing this loop to
             * continue backwards
             */
        } else {
            Addr->Index[CurrentLevel]++;
            break;
        }
        CurrentLevel--;
    }
}

static BOOLEAN IdentityPtCanAggregate (
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT32 Level,
    IN UINT32 AggregationMask
)
{
    BOOLEAN CanAggregate = FALSE;
    IDENTITY_PT_LEVEL_DESC *LevelDesc;
    IDENTITY_PT_DESC *PtDesc;

    PtDesc = &Builder->BuilderPtDesc;

    if (Level < PtDesc->NumLevels) {
        LevelDesc = &PtDesc->Levels[Level];

        /*
        * Check if this level supports aggregation according to the
        * page table description.
        */
        if (LevelDesc->LevelFlags & LEVEL_FLAG_CAN_AGGREGATE &&
            (AggregationMask & (1 << Level))) {
            if (!PtDesc->FuncCanAggregate) {
                /* If NULL and flag is set, return TRUE */
                CanAggregate = TRUE;
            } else {
                /* Consult the function */
                CanAggregate = PtDesc->FuncCanAggregate(LevelDesc, Level);
            }
        }
    }

    return CanAggregate;
}

static UINT64 IdentityPtMarkPages (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *Addr,
    IN UINT32 Level,
    IN UINT64 PagesToMark,
    IN UINT8 AggregationMask,
    IN UINT8 Tag
)
{
    UINT64 PagesMarked = 0;
    IDENTITY_PT_LEVEL_INFO *LvlInfo = &Builder->BuilderPtInfo.Info[Level];
    UINT64 RemainingPages = PagesToMark;
    UINT8 *BaseTag = Builder->TagBuffer;

    /* The offset into the tag buffer given the partitioned address */
    UINT64 OffsetInBase = IdentityPtCalcTagIndex(Builder, Addr, Level);
    BaseTag += OffsetInBase;

    UINT32 EntriesLeftInBlock = LvlInfo->PopulatedEntries -
                                Addr->Index[Level];

    while (RemainingPages > 0 && EntriesLeftInBlock > 0) {

        /*
        * In order to aggregate pages from a lower level here,
        * the following needs to be true:
        *
        * 1. The bits in the address for (ALL) the lower levels needs
        * to be zero
        * 2. We must have at least 1 blocks worth of end pages remaining
        * 3. Aggregation must be supported for this level as indicated
        * in the page table description
        * 4. The appropriate bit in the aggregation mask must be set
        * 5. Aggregation for this level must be supported on this CPU.
        */
        if (IdentityPtRemainingLevelsZero(Builder, Addr, Level) &&
            RemainingPages >= LvlInfo->EndPagesPerBlock &&
            IdentityPtCanAggregate(Builder, Level, AggregationMask)) {
            /* Combine into a large translation unit */
            RemainingPages -= LvlInfo->EndPagesPerBlock;
            PagesMarked += LvlInfo->EndPagesPerBlock;
            IdentityPtStepPartAddr(Builder, Addr, Level);
            Builder->LevelMapCount[Level]++;
            *BaseTag = (Tag | TAG_MAPPING_DIRECT);
            BaseTag++;
        } else {
            if (Level == Builder->BuilderPtDesc.NumLevels - 1) {
                /* Marking End pages */
                RemainingPages--;
                PagesMarked++;

                *BaseTag = (Tag | TAG_MAPPING_DIRECT);
                BaseTag++;
                IdentityPtStepPartAddr(Builder, Addr, Level);
                Builder->LevelMapCount[Level]++;
            } else {
                /* We need to use a smaller translation unit size */
                UINT64 LevelMarkedPages =
                    IdentityPtMarkPages(Builder, Addr, Level + 1,
                                        RemainingPages, AggregationMask,
                                        Tag);

                RemainingPages -= LevelMarkedPages;
                PagesMarked += LevelMarkedPages;

                *BaseTag = (Tag | TAG_MAPPING_DOWNLEVEL);
                BaseTag++;
            }
        }

        EntriesLeftInBlock--;
    }

    return PagesMarked;
}

static UINT64 IdentityPtGenMask(UINT32 bitcount, UINT32 shift)
{
    UINT64 mask = (1ULL << bitcount) - 1;

    mask <<= shift;
    return mask;
}

static UINT64 IdentityPtCalcTagBufferSize (
    IN IDENTITY_PT_BUILDER *Builder
)
{
    UINT64 BufferSize = 0;
    UINT64 PreviousPopulatedSize = 0;
    UINT64 PreviousOffset = 0;
    UINT32 i, TableEntries;
    IDENTITY_PT_LEVEL_INFO *LevelInfo;

    /* Each tag element is a single byte in size */
    for (i = 0 ; i < Builder->BuilderPtDesc.NumLevels; i++) {
        LevelInfo = &Builder->BuilderPtInfo.Info[i];

        if (i == 0) {
            /* For the first level, we will always have a single table entry */
            TableEntries = LevelInfo->PopulatedEntries == 0 ? 1 :
                           LevelInfo->PopulatedEntries;

            BufferSize += TableEntries;
            PreviousPopulatedSize =
                LevelInfo->PopulatedEntries == 0 ?
                1 : LevelInfo->PopulatedEntries;
            LevelInfo->BufferOffset = PreviousOffset;

            PreviousOffset += BufferSize;
            LevelInfo->BlockSize = BufferSize;
        } else {
            UINT64 AddedSize;

            AddedSize = LevelInfo->PopulatedEntries;

            AddedSize *= PreviousPopulatedSize;
            BufferSize += AddedSize;

            PreviousPopulatedSize *= LevelInfo->PopulatedEntries;
            LevelInfo->BufferOffset = PreviousOffset;
            PreviousOffset += AddedSize;
            LevelInfo->BlockSize = AddedSize;
        }
    }

    return BufferSize;
}



static void IdentityPtGeneratePtInfo (
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT32 PaBits
)
{
    if (Builder->BuilderPtDesc.NumLevels == 0 ||
        Builder->BuilderPtDesc.VaBits < PaBits ||
        Builder->BuilderPtDesc.PaBits < PaBits)
        return;

    INT32 i = (Builder->BuilderPtDesc.NumLevels - 1);

    UINT32 PaBitsRemaining =
        PaBits - __builtin_ctz(Builder->BuilderPtDesc.EndPageSize);
    UINT32 VaBitsRemaining =
        Builder->BuilderPtDesc.VaBits -
        __builtin_ctz(Builder->BuilderPtDesc.EndPageSize);
    UINT64 PrevPagesPerBlock = 1;
    IDENTITY_PT_LEVEL_INFO *LevelInfo;
    IDENTITY_PT_LEVEL_DESC *LevelDesc;

    while (i >= 0) {
        LevelInfo = &Builder->BuilderPtInfo.Info[i];
        LevelDesc = &Builder->BuilderPtDesc.Levels[i];

        if (i == 0) {
            /*
            * We will always have a single table at this point
            * This will be where the CPU's page table pointer will point to
            */
            LevelInfo->IndexBits = PaBitsRemaining;
            LevelInfo->PopulatedEntries = (PaBitsRemaining == 0) ?
                1 : 1 << PaBitsRemaining;

            LevelInfo->ShiftBits =
                Builder->BuilderPtDesc.VaBits - VaBitsRemaining;
            LevelInfo->VaMask =
                IdentityPtGenMask(LevelInfo->IndexBits, LevelInfo->ShiftBits);
        } else {
            if (PaBitsRemaining > LevelDesc->Bits) {
                /* Use the full table size */
                LevelInfo->ShiftBits = PaBits - PaBitsRemaining;
                LevelInfo->IndexBits = LevelDesc->Bits;
                LevelInfo->PopulatedEntries = 1 << LevelInfo->IndexBits;

                PaBitsRemaining -= LevelDesc->Bits;
                VaBitsRemaining -= LevelDesc->Bits;
            } else {
                /* In this case, we will need to factor in the va_bits */
                LevelInfo->ShiftBits = (VaBitsRemaining == 0) ?
                    0 : Builder->BuilderPtDesc.VaBits - VaBitsRemaining;

                LevelInfo->IndexBits = (PaBitsRemaining == 0) ?
                    0 : PaBitsRemaining;
                LevelInfo->PopulatedEntries = (PaBitsRemaining == 0) ?
                    1 : (1 << PaBitsRemaining);

                PaBitsRemaining = 0;

                if (VaBitsRemaining >= LevelDesc->Bits)
                    VaBitsRemaining -= LevelDesc->Bits;
            }
            LevelInfo->VaMask =
                IdentityPtGenMask(LevelInfo->IndexBits, LevelInfo->ShiftBits);
        }

        LevelInfo->EndPagesPerBlock = PrevPagesPerBlock;
        PrevPagesPerBlock <<= LevelInfo->IndexBits;
        i--;
    }
}

static UINT64 IdentityPtAlign(
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT64 Offset,
    IN UINT32 Level
)
{
    UINT64 AlignedOffset = Offset;
    UINT64 Alignment =
        Builder->BuilderPtDesc.Levels[Level].LevelAlignment;

    /* Assume Alignment is a Power Of Two */
    if (Alignment > 1) {
        AlignedOffset += (Alignment - 1);
        AlignedOffset &= (~(Alignment - 1));
    }

    return AlignedOffset;
}

static UINT64 IdentityPtCalcPtSize(
    IN IDENTITY_PT_BUILDER *Builder
)
{
    UINT32 i;
    UINT64 j;
    UINT64 TotalSize = 0;
    UINT64 LevelSize;
    UINT8 *TagBuffer;

    /*
    * We will always have a single table at L0, this table is also at offset 0
    * Since the L0 table will be at the base address of the page table memory,
    * we assume that the base addresses alignment is sufficient for this
    * purpose.
    */
    TotalSize =
        (1ULL << Builder->BuilderPtDesc.Levels[0].Bits) *
        Builder->BuilderPtDesc.Levels[0].BytesPerEntry;
    Builder->PtBuildInfo.BuildInfo[0].PtOffset = 0;
    Builder->PtBuildInfo.BuildInfo[0].PtLevelSize = TotalSize;

    /* Starting from L0 */
    for (i = 0; i < Builder->BuilderPtDesc.NumLevels - 1; i++) {
        TagBuffer = Builder->TagBuffer +
                    Builder->BuilderPtInfo.Info[i].BufferOffset;

        UINT64 NextLevelTableSize =
            (1ULL << Builder->BuilderPtDesc.Levels[i + 1].Bits) *
            Builder->BuilderPtDesc.Levels[i + 1].BytesPerEntry;

        /*
        * We want to ensure both the offset and the table size
        * meet the level alignment requirements.
        * This is because we would like to pack all the tables for a
        * single level as closely as possible.
        */

        NextLevelTableSize =
         IdentityPtAlign(Builder, NextLevelTableSize, i + 1);

        Builder->PtBuildInfo.BuildInfo[i + 1].PtBlockSize = NextLevelTableSize;

        LevelSize = 0;

        for (j = 0; j < Builder->BuilderPtInfo.Info[i].BlockSize; j++) {
            /*
            * For each entry here we add another full n+1 table to the next
            * level size if there is a downlevel requested.
            */
            if ((TagBuffer[j] & TAG_BITS_MASK_MAPPING) ==
                TAG_MAPPING_DOWNLEVEL) {
                TotalSize += NextLevelTableSize;
                LevelSize += NextLevelTableSize;
            }
        }

        /* Calculate the offset for the subsequent level on here */
        Builder->PtBuildInfo.BuildInfo[i + 1].PtOffset =
            Builder->PtBuildInfo.BuildInfo[i].PtOffset +
            Builder->PtBuildInfo.BuildInfo[i].PtLevelSize;

        UINT64 TempPtOffset = Builder->PtBuildInfo.BuildInfo[i + 1].PtOffset;

        Builder->PtBuildInfo.BuildInfo[i + 1].PtOffset =
            IdentityPtAlign(Builder, TempPtOffset, i + 1);

        TotalSize += (Builder->PtBuildInfo.BuildInfo[i + 1].PtOffset -
                      TempPtOffset);
        Builder->PtBuildInfo.BuildInfo[i + 1].PtLevelSize = LevelSize;
    }

    return TotalSize;
}

static VOID IdentityPtPopulateLevel (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_PART_ADDR *PartedAddr,
    IN UINT32 Level,
    IN UINT64 *LevelOffsets,
    OUT VOID *PtBuffer
)
{
    UINT64 PtBase = (UINT64)PtBuffer;
    UINT32 i;
    UINT8 *TagBuffer = Builder->TagBuffer;
    UINT8 TagByte;
    UINT32 LevelEntries =
        1 << Builder->BuilderPtDesc.Levels[Level].Bits;
    UINT32 PopulatedEntries =
        Builder->BuilderPtInfo.Info[Level].PopulatedEntries;
    UINT64 TagOffset, DirectMapAddress, DownLevelPageTableAddr;
    UINT64 NextLevelTableSize =
        (Level < Builder->BuilderPtDesc.NumLevels - 1) ?
        Builder->PtBuildInfo.BuildInfo[Level + 1].PtBlockSize : 0;
    UINT64 NotPresentWord =
        Builder->BuilderPtDesc.Levels[Level].NotPresentWord;

    TagOffset = IdentityPtCalcTagIndex(Builder, PartedAddr, Level);
    TagBuffer += TagOffset;

    PtBase += LevelOffsets[Level];

    for (i = 0; i < LevelEntries; i++) {
        /*
        * Loop through an entire level of entries
        * (using the index bits in the description)
        * But only examine "PopulatedEntries" entries,
        * the others above this limit will not be present.
        */

        if (i < PopulatedEntries) {
            TagByte = TagBuffer[i];
        } else {
            TagByte = TAG_MAPPING_NONE;
        }


        switch (TagByte & TAG_BITS_MASK_MAPPING) {
            case TAG_MAPPING_DIRECT:
                DirectMapAddress = IdentityPtJoinAddr(Builder, PartedAddr);

                /* Invoke the write PTE function */
                Builder->BuilderPtDesc.FuncWritePte((void *)PtBase, TagByte,
                                                    DirectMapAddress, Level);

                /* Step "over" this portion */
                IdentityPtStepPartAddr(Builder, PartedAddr, Level);
                break;

            case TAG_MAPPING_DOWNLEVEL:
                DownLevelPageTableAddr =
                    (UINT64)PtBuffer + LevelOffsets[Level + 1];

                Builder->BuilderPtDesc.FuncWritePte((void *)PtBase, TagByte,
                                                    DownLevelPageTableAddr,
                                                    Level);

                IdentityPtPopulateLevel(Builder, PartedAddr,
                                        Level + 1, LevelOffsets, PtBuffer);

                LevelOffsets[Level + 1] += NextLevelTableSize;
                break;

            case TAG_MAPPING_NONE:
                switch (Builder->BuilderPtDesc.Levels[Level].BytesPerEntry) {
                    case 4:
                        *((UINT32 *)(PtBase)) = (UINT32)NotPresentWord;
                        break;
                    case 8:
                        *((UINT64 *)(PtBase)) = (UINT64)NotPresentWord;
                        break;
                }

                /* Step "over" this non-mapped portion */
                IdentityPtStepPartAddr(Builder, PartedAddr, Level);
                break;
        }

        PtBase += Builder->BuilderPtDesc.Levels[Level].BytesPerEntry;
    }
}

UINT64 EFIAPI IdentityPtInit (
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_DESC    *Desc,
    IN UINT32              PaBits,
    IN VOID                *TagBuffer
)
{
    UINT64 TagBufferSize = 0;

    /* Copy over the Desc into the Builder structure */
    CopyMem(&Builder->BuilderPtDesc, Desc, sizeof(IDENTITY_PT_DESC));
    IdentityPtGeneratePtInfo(Builder, PaBits);
    TagBufferSize = IdentityPtCalcTagBufferSize(Builder);

    if (TagBuffer) {
        Builder->TagBuffer = TagBuffer;
        Builder->TagBufferSize = TagBufferSize;

        /* Clear the tag buffer */
        IdentityPtReset(Builder);
    } else {
        /* A query of the tag buffer size is desired, do nothing here */
    }

    return TagBufferSize;
}

VOID
EFIAPI
IdentityPtReset (
    IN IDENTITY_PT_BUILDER *Builder
)
{
    UINT32 i;

    /* Zero out the tag buffer */
    SetMem(Builder->TagBuffer, Builder->TagBufferSize, 0);

    /* Zero our map level counts */
    for (i = 0; i < MAX_LEVELS; i++) {
        Builder->LevelMapCount[i] = 0;
    }

    return;
}

UINT64
EFIAPI
IdentityPtGetPtSize (
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT32             BlockSize
)
{
    UINT64 PtSize = IdentityPtCalcPtSize(Builder);
    UINT64 BlockMask = BlockSize - 1;

    if (BlockSize > 1) {
        /* Assume a Power of two value here */
        PtSize += BlockMask;
        PtSize &= (~BlockMask);
    }

    return PtSize;
}

VOID
EFIAPI
IdentityPtWritePt (
    IN IDENTITY_PT_BUILDER    *Builder,
    OUT VOID                 *PtBuffer
)
{
    UINT64 LevelOffsets[MAX_LEVELS];
    UINT32 i;
    IDENTITY_PT_PART_ADDR PartedAddr;

    /* Start at address 0 */
    IdentityPtPartitionAddr(Builder, &PartedAddr, 0);

    for (i = 0; i < MAX_LEVELS; i++) {
        if (i < Builder->BuilderPtDesc.NumLevels) {
            LevelOffsets[i] = Builder->PtBuildInfo.BuildInfo[i].PtOffset;
        } else {
            LevelOffsets[i] = 0;
        }
    }

    /* Kick off the process by starting at L0 */
    IdentityPtPopulateLevel(Builder, &PartedAddr, 0, LevelOffsets, PtBuffer);
    return;
}

VOID
EFIAPI
IdentityPtMap (
    IN IDENTITY_PT_BUILDER    *Builder,
    IN UINT64               BaseAddress,
    IN UINT32               NumEndPages,
    IN UINT8                Tag,
    IN UINT8                AggregationMask
)
{
    IDENTITY_PT_PART_ADDR PartedAddr;

    if (Builder->BuilderPtDesc.PaBits < 64) {
       UINT64 PaMask = (1ULL << Builder->BuilderPtDesc.PaBits) - 1;
       BaseAddress &= PaMask;

        /*
        * TODO: We can add a check here to ensure we get a valid address
        * Use the NOT of the PaMask and AND it with the BaseAddress
        * If the result is non-zero, we got an invalid address
        **/
    }

    IdentityPtPartitionAddr(Builder, &PartedAddr, BaseAddress);

    IdentityPtMarkPages(Builder, &PartedAddr, 0, NumEndPages,
                        AggregationMask, Tag);

    return;
}

/*
* We need to do it this way because it is possible for DescriptorSize
* to not be equal to the structure size. In fact, this is the case
* on all systems tested.
*/
static
EFI_PHYSICAL_ADDRESS
IdentityPtGetMaxEndAddress(
    VOID *MemoryMap,
    UINT32 DescriptorSize,
    UINTN MemoryMapSize
)
{
    EFI_PHYSICAL_ADDRESS MaxEndAddress = 0;
    EFI_PHYSICAL_ADDRESS CurrentEndAddress = 0;
    EFI_MEMORY_DESCRIPTOR *MemoryDescriptor;
    VOID *CurrentDescriptor;

    CurrentDescriptor = MemoryMap;

    while (MemoryMapSize > 0) {
        MemoryDescriptor = (EFI_MEMORY_DESCRIPTOR *)CurrentDescriptor;

        CurrentEndAddress = MemoryDescriptor->PhysicalStart +
                            EFI_PAGES_TO_SIZE(MemoryDescriptor->NumberOfPages);

        CurrentEndAddress--;

        if (MaxEndAddress < CurrentEndAddress)
            MaxEndAddress = CurrentEndAddress;

        CurrentDescriptor += DescriptorSize;
        MemoryMapSize -= DescriptorSize;
    }

    return MaxEndAddress;
}

UINT32
EFIAPI
IdentityPtGetSystemPaBits()
{
    UINT32 PaBits = 0;
    EFI_PHYSICAL_ADDRESS MapMemory, MaxEndAddress;
    UINT32 MapMemoryPages;
    UINTN MemoryMapSize = 0;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    UINTN MapKey;
    EFI_STATUS status;

    status = gBS->GetMemoryMap(&MemoryMapSize, NULL, &MapKey,
                               &DescriptorSize, &DescriptorVersion);
    if (status == EFI_BUFFER_TOO_SMALL) {
        /* We expect this return code to come to us */

        /*
        * Given the above information, calculate how many pages
        * we should allocate from the system.
        */
        MapMemoryPages = EFI_SIZE_TO_PAGES(MemoryMapSize);

        /**
         * We need to account for the fact that allocating memory will
         * complicate the memory map a bit. In order to do this,
         * we will check to see how close we are to a page boundary,
         * if too close, tack on an extra page
         *
         * Assumes that a DescriptorSize is no where near EFI_PAGE_SIZE
        */
        if ((EFI_PAGE_SIZE - (MemoryMapSize % EFI_PAGE_SIZE) <
            (4 * DescriptorSize))) {
                MapMemoryPages++;
        }

        status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                    MapMemoryPages, &MapMemory);
        if (status == EFI_SUCCESS) {
            /* Fetch the memory map*/
            MemoryMapSize = EFI_PAGES_TO_SIZE(MapMemoryPages);

            status = gBS->GetMemoryMap(&MemoryMapSize,
                                       (EFI_MEMORY_DESCRIPTOR *)MapMemory,
                                       &MapKey, &DescriptorSize,
                                       &DescriptorVersion);

            if (status == EFI_SUCCESS) {
                MaxEndAddress = IdentityPtGetMaxEndAddress((VOID *)MapMemory,
                                                           DescriptorSize,
                                                           MemoryMapSize);
                PaBits = 64 - __builtin_clzll(MaxEndAddress);
            }

            /* Release the memory back to the system */
            gBS->FreePages(MapMemory, MapMemoryPages);
        }
    }

    /* If any of the above has failed, use a reasonable default */
    if (PaBits == 0) {
        PaBits = 36;
    }

    return PaBits;
}
