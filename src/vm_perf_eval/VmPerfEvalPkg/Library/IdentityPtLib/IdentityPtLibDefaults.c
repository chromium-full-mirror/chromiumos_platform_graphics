/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include <IdentityPtLib.h>
#include <Library/BaseLib.h>
#include <Register/Intel/Cpuid.h>


VOID Pml4WritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level);
VOID X86PmWritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level);
VOID X86PaeWritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level);
BOOLEAN Pml4CanAggregateAtLevel(IDENTITY_PT_LEVEL_DESC *LevelDesc,
                                UINT32 Level);

#define IDENTITY_PT_MASK_4K     (~((UINT64)0xFFF))
#define IDENTITY_PT_MASK_2M     (~((UINT64)0x1FFFFF))
#define IDENTITY_PT_MASK_4M     (~((UINT64)0x3FFFFF))
#define IDENTITY_PT_MASK_1G     (~((UINT64)0x3FFFFFFF))

/* The typical PML4 paging scheme for x86-64 */
IDENTITY_PT_DESC g_Pml4Desc = {
    .NumLevels = 4,
    .EndPageSize = 4096,
    .VaBits = 48,
    .PaBits = 48,
    .FuncWritePte = Pml4WritePtEntry,
    .Levels =
    {
        /* 512G */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = 0,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 1G */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = LEVEL_FLAG_CAN_AGGREGATE,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 2M */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = LEVEL_FLAG_CAN_AGGREGATE,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 4K */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = 0,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        }
    }
};

/* Standard 32-bit x86 paging */
IDENTITY_PT_DESC g_X86PmDesc = {
    .NumLevels = 2,
    .EndPageSize = 4096,
    .VaBits = 32,
    .PaBits = 32,
    .FuncWritePte = X86PmWritePtEntry,
    .FuncCanAggregate = NULL,
    .Levels =
    {
        /* 4M */
        {
            .Bits = 10,
            .BytesPerEntry = 4,
            .LevelFlags = LEVEL_FLAG_CAN_AGGREGATE,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 4K */
        {   .Bits = 10,
            .BytesPerEntry = 4,
            .LevelFlags = 0,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        }
    }
};

/* PAE 32-bit x86 paging */
IDENTITY_PT_DESC g_X86PaeDesc = {
    .NumLevels = 3,
    .EndPageSize = 4096,
    .VaBits = 32,
    .PaBits = 32,
    .FuncWritePte = X86PaeWritePtEntry,
    .FuncCanAggregate = NULL,
    .Levels =
    {
        /* 1G */
        {
            .Bits = 2,
            .BytesPerEntry = 8,
            .LevelFlags = 0,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 2M */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = LEVEL_FLAG_CAN_AGGREGATE,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        },
        /* 4K */
        {
            .Bits = 9,
            .BytesPerEntry = 8,
            .LevelFlags = 0,
            .LevelAlignment = 4096,
            .NotPresentWord = 0
        }
    }
};


#define PML4_PRESENT            0x1
#define PML4_RW                 0x2
#define PML4_RO                 0x0
#define PML4_PAGE_SIZE          0x80

#define PML4_PWT                0x8
#define PML4_PCD                0x10

/*
* The PAT bit is in different locations depending
* on whether we are using a 4K direct page or a
* larger (1G, 2M) page size.
*/
#define PML4_PAT                0x1000
#define PML4_PAT_4K             0x80

#define X86_PM_PRESENT          0x1
#define X86_PM_PAGE_SIZE        0x80
#define X86_PM_PWT              0x8
#define X86_PM_PCD              0x10
#define X86_PM_RW               0x2
#define X86_PM_RO               0x0


BOOLEAN Pml4CanAggregateAtLevel(IDENTITY_PT_LEVEL_DESC *LevelDesc, UINT32 Level)
{
    BOOLEAN CanAggregate = TRUE;
    UINT32 Edx;
    UINT32 Eax;
    CPUID_EXTENDED_CPU_SIG_EDX ExtendedSigEdx;

    if (Level == 1) {
        /* This is the 1GiB level */

        /* TODO: This might be a little heavy to call every single time when
        *  building a page table, see if the results can be cached.
        */
        AsmCpuid(CPUID_EXTENDED_FUNCTION, &Eax, NULL, NULL, NULL);
        if (Eax < CPUID_EXTENDED_CPU_SIG) {
            /* CPU DEFINITELY does not support 1GiB pages */
            CanAggregate = FALSE;
        } else {
            /* Determine if the CPU supports 1GiB pages */
            AsmCpuid(CPUID_EXTENDED_CPU_SIG, NULL, NULL, NULL, &Edx);

            ExtendedSigEdx.Uint32 = Edx;
            CanAggregate = (ExtendedSigEdx.Bits.Page1GB) ? TRUE : FALSE;
        }
    }

    return CanAggregate;
}

/*
* The PAT/PCD/PWT bits actually encode an index
* into the PAT register, rather than directly encoding the
* caching behavior.
*
* Bits will be (PAT, PCD, PWT)
* i.e PAT=1, PCD=0, PWT=0 will have the CPU select entry 4
*
* The PAT bit does not exist in downlevel pointer entries
* So only the first 4 entries are accessible.
* Since the powerup defaults repeat the same sequence twice
* this isnt an issue.
* We will assume the power up defaults in the EFI:
* PAT0 = WB
* PAT1 = WT
* PAT2 = UC-
* PAT3 = UC
* PAT4 = WB
* PAT5 = WT
* PAT6 = UC-
* PAT7 = UC
*/
UINT64 Pml4HandleCache(UINT64 CurrentPte, UINT8 Tag)
{
    UINT64 newPte = CurrentPte;
    /*
    * We don't bother manipulating the PAT bit
    * as we assume they will be 0
    * It would complicate this code as they are in different
    * positions depending on which level we are on.
    */

    switch (Tag & TAG_BITS_MASK_CACHE_TYPE) {
        case TAG_CACHE_UC:
        case TAG_CACHE_WC:
            /*
            * We want PAT3
            * WC is not in the power up defaults
            * for the PAT. Force this to UC.
            * Set both PCD/PWT, leave PAT at 0
            */
            newPte |= (PML4_PCD | PML4_PWT);
            break;

        case TAG_CACHE_WB:
            /*
            * We want PAT0
            * PCD/PWT = 0, PAT is also 0
            */
            newPte &= (~(PML4_PCD | PML4_PWT));
            break;

        case TAG_CACHE_WT:
            /*
            * We want PAT1
            * Set PWT
            * Clear PCD
            */
            newPte |= PML4_PWT;
            newPte &= (~PML4_PCD);
            break;

        default:
            /* Should probably flag this one */
            break;
    }

    return newPte;
}

/* The exact same logic as above applies here */
UINT32 X86PmHandleCache(UINT32 CurrentPte, UINT8 Tag)
{
    UINT32 newPte = CurrentPte;

    switch (Tag & TAG_BITS_MASK_CACHE_TYPE) {
        case TAG_CACHE_UC:
        case TAG_CACHE_WC:

            newPte |= (X86_PM_PCD | X86_PM_PWT);
            break;

        case TAG_CACHE_WB:

            newPte &= (~(X86_PM_PCD | X86_PM_PWT));
            break;

        case TAG_CACHE_WT:
            newPte |= X86_PM_PWT;
            newPte &= (~X86_PM_PCD);
            break;

        default:
            /* Should probably flag this one */
            break;
    }

    return newPte;
}

VOID Pml4WritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level)
{
    /*
    * Bit 0 is the Present flag for all levels for PML4
    * This function will not be called for an entry which is not
    * to be marked as present
    *
    * R/W bits will be set for all except on the direct mapped level
    * This is so that only that bit will take effect
    *
    * A similar approach will be taken for caching
    * By default, all bits are 0 and will use a WB caching policy
    * If we are pointing to a down level table, we want to make use of
    * caching. If we are not using a downlevel table (direct), we will use the
    * desired caching policy that is indicated in the Tag.
    */
    UINT64 PteValue = PML4_PRESENT;
    UINT64 *PtePtr = (UINT64 *)Pte;


    switch (Level) {
        case 0:
            /* This level MUST be a downlevel table pointer */
            PteValue |= PML4_RW;
            PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            break;

        case 1:
            if ( (Tag & TAG_BITS_MASK_MAPPING) == TAG_MAPPING_DIRECT) {
                PteValue |= PML4_PAGE_SIZE;

                if ((Tag & TAG_BITS_MASK_ACCESS_TYPE) == TAG_ACCESS_RW)
                    PteValue |= PML4_RW;
                else
                    PteValue |= PML4_RO;

                /* Handle caching access here */
                PteValue = Pml4HandleCache(PteValue, Tag);
                PteValue |= (Phys & IDENTITY_PT_MASK_1G);
            } else {
                PteValue |= PML4_RW;
                PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            }
            break;

        case 2:
            if ( (Tag & TAG_BITS_MASK_MAPPING) == TAG_MAPPING_DIRECT) {
                PteValue |= PML4_PAGE_SIZE;

                if ((Tag & TAG_BITS_MASK_ACCESS_TYPE) == TAG_ACCESS_RW)
                    PteValue |= PML4_RW;
                else
                    PteValue |= PML4_RO;

                /* Handle caching access here */
                PteValue = Pml4HandleCache(PteValue, Tag);
                PteValue |= (Phys & IDENTITY_PT_MASK_2M);
            } else {
                PteValue |= PML4_RW;
                PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            }
            break;

        case 3:
            if ((Tag & TAG_BITS_MASK_ACCESS_TYPE) == TAG_ACCESS_RW)
                PteValue |= PML4_RW;
            else
                PteValue |= PML4_RO;

            /* Handle caching access here */
            PteValue = Pml4HandleCache(PteValue, Tag);
            PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            break;
    }

    *PtePtr = PteValue;
}

VOID X86PmWritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level)
{
    UINT32 PteValue = X86_PM_PRESENT;
    UINT32 *PtePtr = (UINT32 *)Pte;

    switch (Level) {
        case 0:
            if ((Tag & TAG_BITS_MASK_MAPPING) == TAG_MAPPING_DIRECT) {
                PteValue |= X86_PM_PAGE_SIZE;

                if ((Tag & TAG_BITS_MASK_ACCESS_TYPE) == TAG_ACCESS_RW)
                    PteValue |= X86_PM_RW;
                else
                    PteValue |= X86_PM_RO;

                PteValue = X86PmHandleCache(PteValue, Tag);
                PteValue |= (Phys & IDENTITY_PT_MASK_4M);
            } else {
                PteValue |= X86_PM_RW;
                PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            }
            break;

        case 1:
            if ((Tag & TAG_BITS_MASK_ACCESS_TYPE) == TAG_ACCESS_RW)
                PteValue |= X86_PM_RW;
            else
                PteValue |= X86_PM_RO;

            PteValue = X86PmHandleCache(PteValue, Tag);
            PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            break;
    }

    *PtePtr = PteValue;
}

/* We can use the defines from the PML4 as they are pretty much similar */
VOID X86PaeWritePtEntry(VOID *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level)
{
    UINT64 PteValue = PML4_PRESENT;
    UINT64 *PtePtr = (UINT64 *)Pte;

    switch (Level) {
        case 0:
            /*
            * PDPTE, has PCD/PWT bits, but since it is a downlevel pointer
            * we will leave this as 0.
            */
            PteValue |= (Phys & IDENTITY_PT_MASK_4K);
            break;

        case 1:
        case 2:
            /*
            * For these cases, we will use the PML4 methods with Level+1
            * Also return early as it will have already written the PTE value
            */
            Pml4WritePtEntry(Pte, Tag, Phys, Level + 1);
            return;
            break;
    }

    *PtePtr = PteValue;
}