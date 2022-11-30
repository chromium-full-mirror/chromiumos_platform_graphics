/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef __IDENTITY_PT_LIB__
#define __IDENTITY_PT_LIB__

#include <Uefi.h>


#define     MAX_LEVELS      8

/*
* We are using a single byte for each page table entry in our "tag" buffer
* This is an intermediate step that can be used to tag certain addresses as
* visible to the CPU from the virtual space. Since the virtual addresses
* correlate 1:1 with the physical addresses we do not need to store the
* physical addreses within the tag buffer.
*
* Below are the bit defines:
*/
#define     TAG_BITS_MASK_MAPPING       0x03
#define         TAG_MAPPING_NONE        0x00    /* No mapping present */
#define         TAG_MAPPING_DOWNLEVEL   0x01    /* Translate further */
#define         TAG_MAPPING_DIRECT      0x02    /* Translation unit defined */

#define     TAG_BITS_MASK_CACHE_TYPE    0x0C
#define         TAG_CACHE_UC            0x00    /* Uncached mapping */
#define         TAG_CACHE_WC            0x04    /* Write combined mapping */
#define         TAG_CACHE_WT            0x08    /* Write through mapping */
#define         TAG_CACHE_WB            0x0C    /* Write Back Mapping */

#define     TAG_BITS_MASK_ACCESS_TYPE   0x10
#define         TAG_ACCESS_RO           0x00    /* Read only access */
#define         TAG_ACCESS_RW           0x10    /* R/W access */
/*
*  Function prototype to write a page table entry
*  The will be used to write entries into the page table
*/
typedef void (*WritePteEntry)(void *Pte, UINT8 Tag, UINT64 Phys, UINT32 Level);

/*
* Set this bit on "level_flags" to indicate that we can map a block
* of this size directly without further descension into another level
*/
#define     LEVEL_FLAG_CAN_AGGREGATE    0x1

typedef struct {
    /* Number of bits used to index at this level */
    UINT32 Bits;
    /* Number of bytes per entry at this level */
    UINT32 BytesPerEntry;
    /* Level Flags for this level */
    UINT32 LevelFlags;
    /*
    * The address alignment needed for a table at this level
    * Assumed to be a power of two
    */
    UINT32 LevelAlignment;
    /* The value to set each NotPresent entry to */
    UINT64 NotPresentWord;
} IDENTITY_PT_LEVEL_DESC;

/*
 * Function prototype to determine if aggregation can be done at this
 * level. The level description will describe what is possible with the
 * page table format, however, it may not be possible to support aggregation
 * because of the device we are running on.
 *
 * Example, PML4 supports 1GiB pages, but certain x86-64 CPU's do not support
 * this (Sandy Bridge client devices for example).
*/
typedef BOOLEAN (*CanAggregateAtLevel)(IDENTITY_PT_LEVEL_DESC *LevelDesc,
                                       UINT32 Level);

typedef struct {
    /* Number of levels in the overall page table */
    UINT32               NumLevels;
    /* Vritual address bits */
    UINT32               VaBits;
    /* Physical address bits */
    UINT32               PaBits;
    /* Final page size, assumed to be a power of two */
    UINT32               EndPageSize;
    /* Page Table Entry write function */
    WritePteEntry        FuncWritePte;

    /*
    * Function hook to check if the device supports aggregation
    * at a particular level.
    * Setting this to NULL will result in LevelFlags
    * making the sole decision.
    * As with most other decisions with respect to aggregation
    * all votes must agree. If any of the votes indicate NO,
    * aggregation will not be performed.
    */
    CanAggregateAtLevel  FuncCanAggregate;

    /* Description array for each level */
    IDENTITY_PT_LEVEL_DESC  Levels[MAX_LEVELS];
} IDENTITY_PT_DESC;

typedef struct {
    /* Mask to isolate index for this level */
    UINT64  VaMask;
    /* The number of bits to shift to return it to LSB */
    UINT32  ShiftBits;
    /* Number of bits for this field */
    UINT32  IndexBits;
    /* Entries in this table in actual use */
    UINT32  PopulatedEntries;
    /* Offset in the tag buffer for this level (for tagging mappings) */
    UINT64  BufferOffset;
    /* Block size -> Size of the tag buffer for this level */
    UINT64  BlockSize;
    /* Number of end pages represented by each entry at this level */
    UINT64  EndPagesPerBlock;
} IDENTITY_PT_LEVEL_INFO;

typedef struct {
    IDENTITY_PT_LEVEL_INFO Info[MAX_LEVELS];
} IDENTITY_PT_INFO;

typedef struct {
    /* Offset from base of the PT to the start of tables for this level */
    UINT64  PtOffset;
    /* The total amount of memory needed for this level of the page table */
    UINT64  PtLevelSize;
    /* The size of one single table at this level (memory footprint) */
    UINT32  PtBlockSize;
} IDENTITY_PT_BUILD_LEVEL_INFO;

/* This structure is used to hold layout information of the page table */
typedef struct {
    IDENTITY_PT_BUILD_LEVEL_INFO BuildInfo[MAX_LEVELS];
} IDENTITY_PT_BUILD_INFO;

/* Main builder object */
typedef struct {
    IDENTITY_PT_DESC        BuilderPtDesc;
    IDENTITY_PT_INFO        BuilderPtInfo;
    IDENTITY_PT_BUILD_INFO  PtBuildInfo;
    UINT64                  TagBufferSize;
    UINT8                   *TagBuffer;
    UINT64                  LevelMapCount[MAX_LEVELS];
} IDENTITY_PT_BUILDER;


/* Default Page Table Descriptions for the following modes: */
/* PML4 = Long Mode (IA-32e or 64-bit mode) */
/* PM   = Protected Mode (base IA-32 mode) */
/* PAE  = Protected Mode w/ PAE (IA32 + Physical Addressing Extension) */
extern IDENTITY_PT_DESC g_Pml4Desc;
extern IDENTITY_PT_DESC g_X86PmDesc;
extern IDENTITY_PT_DESC g_X86PaeDesc;

/*
* IdentityPt public API definitions
*/

/**
*   Initializes an IDENTITY_PT_BUILDER object for use
*   The function will return the size of the tag buffer necessary to support
*   the page table description and PaBits passed into it.
*   To retrieve the size of tag buffer necessary, first pass NULL to TagBuffer.
*   Allocate a buffer of the appropriate size and then call IdentityPtInit
*   once again (with the same parameters) with the pointer to the allocated
*   buffer.
*
*   @param  Builder     Pointer to a Builder object structure
*   @param  Desc        Pointer to a Page Table Description structure
*   @param  PaBits      Number of Physical Address Bits to support
*   @param  TagBuffer   Pointer to a tag buffer that can be used by the builder
**/
UINT64
EFIAPI
IdentityPtInit(
    IN IDENTITY_PT_BUILDER *Builder,
    IN IDENTITY_PT_DESC    *Desc,
    IN UINT32              PaBits,
    IN VOID                *TagBuffer
);

/**
 *  Reset the IDENTITY_PT_BUILDER object.
 *  The IDENTITY_PT_BUILDER object must be initialized before calling
 *  this function.
 *
 *  This function will clear the TagBuffer. Once complete, nothing will be
 *  mapped within the address space described by the Builder Object.
 *
 *  @param  Builder     Pointer to the Builder object structure
 **/
VOID
EFIAPI
IdentityPtReset(
    IN IDENTITY_PT_BUILDER *Builder
);

/**
 *  Retrieves the memory footprint necessary to generate a page table which
 *  represents the areas currently mapped in the builder object.
 *  The returned value will be in bytes. But it can be rounded up to a block
 *  size for convenience.
 *  For example, if BlockSize is set to 4096, the size will be a multiple
 *  of 4096. If the resulting size does not fit evenly within a block,
 *  it will be rounded up to the next multiple. Set BlockSize to 0 if
 *  this behavior is not desired.
 *
 *  @param  Builder     Pointer to the Builder object structure
 *  @param  BlockSize   Block size to use (set to 1 for no rounding)
 **/
UINT64
EFIAPI
IdentityPtGetPtSize(
    IN IDENTITY_PT_BUILDER *Builder,
    IN UINT32              BlockSize
);

/**
 * Writes the page table to a block of memory.
 * The memory footprint MUST have been previously retrieved
 * by IdentityPtGetPtSize.
 *
 * @param   Builder     Pointer to the Builder object structure
 * @param   PT          Pointer to memory to write the page table into
 **/
VOID
EFIAPI
IdentityPtWritePt(
    IN IDENTITY_PT_BUILDER   *Builder,
    OUT VOID                 *PT
);

/**
 * Maps a particular range of addresses into the page table builder.
 * The aggregation mask allows the client to specify which levels are
 * permitted to use aggregation for a particular mapping. Set to 0 to allow no
 * aggregation, set to all 1's to always allow aggregation.
 * For each page table level (Ln), the aggregation mask will be tested using
 * AggregationMask & (1 << n) to determine if aggregation is desired.
 * 0x6 => Aggregation on L1 and L2, not L0 or L3
 * For aggregation to take place, at the very least the aggregation mask and
 * the level description within the page table description MUST agree on
 * whether aggregation is permitted.
 *
 * @param   Builder         Pointer to the Builder object structure
 * @param   BaseAddress     The base address in the physical space
 * @param   NumEndPages     The size of the space to map in end pages
 * @param   Tag             Tag mask describing mapping details (cache, r/w, ..)
 * @param   AggregationMask Specifies which levels aggregation can be performed
 **/
VOID
EFIAPI
IdentityPtMap(
    IN IDENTITY_PT_BUILDER   *Builder,
    IN UINT64                BaseAddress,
    IN UINT32                NumEndPages,
    IN UINT8                 Tag,
    IN UINT8                 AggregationMask
);

/**
 * This function is offered as a helper function.
 *
 * Its purpose is to calculate the number of physical address bits needed
 * to access the entire memory address space as described by the EFI memory
 * map. This can be used to restrict the amount of tag memory space
 * needed by the identity page table generator.
 *
 * @returns UINT32  Number of Physical Address Bits
*/
UINT32
EFIAPI
IdentityPtGetSystemPaBits();

#endif  // __IDENTITY_PT_LIB__
