/** @file
*
* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#ifndef __VM_PERF_EVAL_LIB_H__
#define __VM_PERF_EVAL_LIB_H__

#include <Uefi.h>
#include <IdentityPtLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/BlockIo.h>
#include <Protocol/BlockIo2.h>

/*
* Defines the maximum number of 'remote' cores. Cores which
* are not the boot core.
*/
#define VM_PERF_MAX_CORES   64

typedef enum {
    /*
    * Immediate Trigger Mode
    * In this mode, the target CPU will immediately enter
    * the entry point as soon as initialization is complete.
    * */
    CoreTriggerImmediate = 0,

    /*
    * Wait for Trigger Mode
    *
    * In this mode, the target CPU will complete initialization
    * and then spin on a trigger value.
    *
    * Once this trigger value has been set, it will then
    * enter the specified entry point.
    *
    * Can be useful in some cases, to start all cores at a
    * certain point in time (or as closely as possible).
    * */
    CoreTriggerWait
}   VM_PERF_CORE_TRIGGER;

typedef enum {
    /*
    * Real Mode Entry Point
    * No paging
    * Only one core can be (safely) in this mode at a time.
    * In this mode the CPU only has access to the lower 1MiB of
    * physical address space.
    * Entry Point = Lower 16-bits (IP), Upper 16-bits CS
    *
    * */
    CoreEntryRealMode = 0,

    /*
    * Protected Mode
    * This mode operates the core in protected mode (32-bit).
    * The benefit here is that there is a 1:1 relationship between a
    * virtual or "linear" address and a physical address without the use of
    * page tables. May be beneficial for some tests provided
    * the hurdle of adding 32-bit code in a 64-bit binary is dealt with
    * in a clean way.
    *
    * Since the CPU is operating in a 32-bit mode, it only has access
    * to the lower 4GiB of physical address space. It also has full
    * unrestricted access to this space in this mode.
    *
    * An errant program on the CPU is almost guaranteed to crash the
    * system as there are critical data structures there. Becareful..
    *
    * Entry = Lower 32-bits into EIP
    **/
    CoreEntryProtectedModeNoPaging,

    /*
    * Protected Mode w/ Paging
    * Normal x86 32-bit mode with paging.
    *
    * Same restrictions as base Protected Mode apply. However, there is
    * some protection offered by the use of page tables.
    *
    * Entry = Lower 32-bits into EIP
    * */
    CoreEntryProtectedModeNormalPaging,

    /*
    * Protected Mode w/ PAE Paging
    * Normal x86 32-bit mode with PAE paging.
    *
    * Same restrictions as base Protected Mode with paging apply.
    * While the PAE paging allows a larger physical address space to be used,
    * you are still limited to accessing 32-bits worth at a time.
    *
    * Entry = Lower 32-bits into EIP
    **/
    CoreEntryProtectedModePAEPaging,

    /*
    * Long Mode (64-bit, x86-64)
    * It is expected that most tests will operate the AP's in this mode.
    * Paging is required in this mode.
    *
    * Entry = Full 64-bits into RIP
    */
    CoreEntryLongMode

}   VM_PERF_CORE_ENTRY_TYPE;

typedef enum {
    /* Individual Core */
    CoreTypeIndividual = 0,

    /* All cores on the sytem (Index is ignored) */
    CoreTypeAll,
} VM_PERF_CORE_TYPE;

typedef enum {
    /* Idle Core (Doing nothing, not in service) */
    CoreStatusIdle = 0,

    /* Core has initialized, but is waiting on trigger */
    CoreStatusNotTriggered,

    /* Core is currently running */
    CoreStatusRunning,

    /* Core has hit an exception */
    CoreStatusException,

    /*
    * Core has successfully returned from the entry point.
    * This does not mean that the test completed successfully.
    * The value returned from the entry point code is used to indicate this.
    */
    CoreStatusFinished
} VM_PERF_CORE_STATUS;

/*
* Structure used to specify entry information when starting
* a particular processor.
*/
typedef struct {
    /* The entry point type (RealMode, LongMode, etc...) */
    VM_PERF_CORE_ENTRY_TYPE EntryType;

    /* Trigger Mode (Immediate, TriggerWait, etc...) */
    VM_PERF_CORE_TRIGGER    TriggerMode;

    /* The entry point of the desired code to run on the target CPU */
    EFI_PHYSICAL_ADDRESS    EntryPoint;

    /*
    * The page table builder to use for entry point types that require it.
    * Before attempting to start the core, any memory referenced by the code
    * pointed at by the entry point MUST be mapped in.
    *
    * StartCore will write the page tables to memory and pass them onto the
    * target CPU for use. Core data structures like stacks and other memory
    * will be mapped automatically by StartCore before writing out the final
    * set of page tables.
    */
    IDENTITY_PT_BUILDER     *PtBuilder;
}   CORE_ENTRY_INFO;

/**
 * Structure to hold information about
 * the CPU this application is running on.
 */
typedef struct {
    /*
    * Holds the vendor string returned by the CPUID instruction
    * on the very first standard space leaf. It is generally
    * a good indicator of the CPU vendor:
    *
    * "AuthenticAMD"
    * "GenuineIntel"
    */
    CHAR8       VendorString[16];

    /*
    * If (from the CPUID instruction) we determine we are running
    * under a VM, this field will contain the hypervisor name.
    * Currently the only known hypervisor is "KVMKVMKVM",
    * which is the Linux KVM. It MAY be possible
    * for virtual machine monitor software to change this.
    */
    CHAR8       HypervisorName[16];

    /*
    * If supported by the CPU (can support extended space CPUID),
    * this will contain the brand name string. This will generally
    * include the actual brand name for this processor and may
    * include the base clock frequency of the CPU.
    */
    CHAR8       BrandName[64];

    /* Set to TRUE if the application is running on a VM */
    BOOLEAN     Virtualized;

    /* Set to TRUE if the TSC deadline timer mode is supported by this CPU */
    BOOLEAN     TscDeadline;

    /*
    * Set to TRUE if MONITOR/MWAIT instructions are supported (extended sleep)
    */
    BOOLEAN     MonitorMwait;

    /*
    * Set to TRUE if we are running under KVM
    * (a VM + Hypervisor String = KVMKVMKVM)
    */
    BOOLEAN     RunningUnderKvm;

    /* Set to TRUE if the KVM Halt Poll control MSR is supported */
    BOOLEAN     KvmHaltPollControl;
} VM_PERF_CPU_INFORMATION;

typedef struct {
    /*
    * The block size (in bytes) in the disk.
    * Accesses are made in even multiples of this value.
    */
    UINTN       BlockSize;

    /*
    * Size of the raw disk in blocks (bytes = BlockSize * RawDiskSizeInBlocks)
    */
    EFI_LBA     RawDiskSizeInBlocks;

    /*
    * The required alignment for the read/write buffer used for accessing
    * the disk. 0 or 1 means no alignment restrictions.
    *
    * The UEFI spec refers to this as IoAlign.
    */
    UINTN       BufferAlign;

    /* Indicates if the selected partition for raw disk access is read only */
    BOOLEAN     ReadOnly;

    /* Indicates if write caching will be used on write accesses to this disk */
    BOOLEAN     WriteCaching;

    /* Indicates if disk accesses can be done asynchronously */
    BOOLEAN     AsyncCapable;
} VM_PERF_RAW_DISK_INFORMATION;

typedef struct {
    BOOLEAN     IsAvailable;
    BOOLEAN     UsingBlockIo2;
    EFI_BLOCK_IO2_PROTOCOL *BlockIo2;
    EFI_BLOCK_IO_PROTOCOL *BlockIo;
    EFI_HANDLE BlockDeviceHandle;
    EFI_LBA     StartingLBA;
    EFI_LBA     EndingLBA;
    EFI_EVENT   RawDiskEvent;
    EFI_BLOCK_IO2_TOKEN Token;
} VM_PERF_RAW_DISK;

typedef struct {
    /* The APIC ID of this core */
    UINT32                  ApicId;

    /* ACPI UID given to us for this core */
    UINT32                  AcpiUid;

    /*
    * CoreTypeIndividual
    * Do not set to CoreTypeAll...
    */
    VM_PERF_CORE_TYPE       CoreType;

    /*
    * Pointer to the Core PCIB
    * This is the block of memory used to communicate with the main
    * processor. This will not be used (directly) by the code running
    * on the AP.
    * Some variables can be referenced by certain API calls.
    */
    EFI_PHYSICAL_ADDRESS    CorePcibPage;

    /*
    * Pointer to a private block of memory for this core to use
    * to set up essential data structures.
    * Generally of 32 ->  64KiB of size.
    */
    EFI_PHYSICAL_ADDRESS    CorePrivBlock;

    /*
    * Size of the private block for this core in pages.
    */
    UINT32                  CorePrivBlockPages;

    /*
    * Pointer to the Core AP page
    * This is a block of memory intended to be used by the client
    * to communicate with code running on the AP.
    */
    EFI_PHYSICAL_ADDRESS    CoreApPage;

    /*
    * Pointer to the Core Page Table Buffer
    * This is used to hold the page tables for this core.
    * This will be the target for these writes.
    * */
    EFI_PHYSICAL_ADDRESS    CorePageTable;

    /*
    * The size of the buffer that is assigned to this core
    * for its page tables. In EFI_PAGE_SIZE units.
    */
    UINT32                  PageTablePages;

    /*
    * The block of memory allocated for this cores stack
    * < 4GiB
    * As stated in the EFI standard, at least 128KiB of stack space
    * will be available
    */
    EFI_PHYSICAL_ADDRESS    CoreStack;

    /*
    * The number of pages allocated for core stack usage
    */
    UINT32                  CoreStackPages;

    /*
    * A stack space dedicated solely for interrupt processing
    * in long mode.
    * Will be much smaller than the core stack
    * All < 4GiB
    */
    EFI_PHYSICAL_ADDRESS    CoreInterruptStack;

    /*
    * Same as the interrupt stack but for exceptions.
    */
    EFI_PHYSICAL_ADDRESS    CoreExceptionStack;

    /*
    * Same as the interrupt stack but for NMI.
    */
    EFI_PHYSICAL_ADDRESS    CoreNMIStack;

    /* The NMI/Exception and interrupt stack will share the same block */
    UINT32                  CoreInterruptStackPages;

    /*
    * A flag indicating whether this particular core is in service
    */
    BOOLEAN                 InService;

    /*
    * A flag indicating whether this particular core is running
    */
    BOOLEAN                 CoreIsRunning;
}   VM_PERF_EVAL_ENUM_CPU;

typedef struct {
    /* Number of cores from ACPI table (we skip the one running the UEFI) */
    UINT32                      NumAcpiCores;

    /* Local APIC address (common for each core) */
    EFI_PHYSICAL_ADDRESS        LapicAddress;

    /* The physical address of the boot strapper code (< 1 MiB) */
    EFI_PHYSICAL_ADDRESS        ApBootCodePage;

    /* The physical address of the trigger page (< 1 MiB) */
    EFI_PHYSICAL_ADDRESS        ApTriggerPage;

    /*
    * The physical address of a work page for the real mode part of
    * the code bootstrapper. < 1MiB
    */
    EFI_PHYSICAL_ADDRESS        ApRealModeWorkPage;

    /* The esimtated TSC frequency */
    UINT64                      EstimatedTscFrequency;

    /* Some details about the hosting application */
    EFI_LOADED_IMAGE_PROTOCOL   *LoadedImage;

    /* Set if file access is available */
    BOOLEAN                     FileAccessOK;

    /* The root device of the device from which this program was loaded */
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *RootDevice;

    /* The root directory of the device from which this program was loaded */
    EFI_FILE_PROTOCOL           *RootDirectory;

    /* The directory created to house the artifacts from the test */
    EFI_FILE_PROTOCOL           *TestDirectory;

    /* The log file handle */
    EFI_FILE_PROTOCOL           *LogFile;

    /* The array containing basic info about each enumerated core */
    VM_PERF_EVAL_ENUM_CPU       AcpiCores[VM_PERF_MAX_CORES];

    /* Instance of the CPU information structure */
    VM_PERF_CPU_INFORMATION     CpuInformation;

    /* Instance of the raw disk access structure */
    VM_PERF_RAW_DISK            RawDisk;
}   VM_PERF_EVAL_CTX;

/**
 * Simple structure to hold the reference to the
 * FILE_PROTOCOL instance used to access a file.
*/
typedef struct {
    EFI_FILE_PROTOCOL   *File;
}   VM_PERF_FILE;

/**
* Initializes the VmPerfEval context for later use
* It currently will scan the ACPI tables for the available
* cores on the system. It will also allocate memory for the
* AP bootstrapper code and associated data structures.
*
* @param  Ctx           Pointer to the Vm Perf Eval context
*
* @return BOOLEAN       Indicates whether initialization completed successfully
*/
BOOLEAN EFIAPI VmPerfInitialize(
    IN VM_PERF_EVAL_CTX *Ctx
);

/**
 * Shuts down the VmPerfEval context. It will release all allocated memory
 * currently being held by this context. It will also park all active cores.
*/
VOID EFIAPI VmPerfShutdown(
    IN VM_PERF_EVAL_CTX *Ctx
);

/**
* Puts one or more cores in "service".
* Adding a core(s) into service prepares it for start-up.
*
* Setting CoreType to CoreTypeIndividual will put a single core
* in service by the EnumIndex.
*
* Other CoreType's will add multiple cores in "service".
*
* Returns the mask of cores in-service
*
* @param    Ctx         Pointer to the Vm Perf Eval context
* @param    CoreType    The type of core to put in service
* @param    Index       The core index
*
* @return UINT64        Bitmask of cores put in-service
*/
UINT64 EFIAPI VmPerfPutInService(
    IN VM_PERF_EVAL_CTX  *Ctx,
    IN VM_PERF_CORE_TYPE CoreType,
    IN UINT32 Index
);

/**
 * @brief Starts a core that has been previously put in service
 *
 * This function will start a core that was put in service.
 * EntryInfo contains the details that will be used to start the core.
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   Index           The core index to start
 * @param   EntryInfo       The entry information for this core
 * @return  BOOLEAN         TRUE if core has been kicked off, FALSE otherwise
 */
BOOLEAN EFIAPI VmPerfStartCore(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index,
    IN CORE_ENTRY_INFO *EntryInfo
);

/**
 * @brief A function to modify the trigger state
 *
 * Manipulate the trigger word that cores will be waiting on
 * if they are entered with the TriggerWait trigger mode.
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   TriggerSet  TRUE = Set Trigger, FALSE = Reset Trigger
 */
VOID EFIAPI VmPerfTrigger(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN BOOLEAN  TriggerSet
);

/**
 * @brief   Removes a core(s) from service
 *
 * Removing a core from service involves "parking" the core(s).
 * The memory associated with a core will be lazily allocated and will
 * be held onto until the library is shutdown.
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   CoreType    The core(s) type to remove
 * @param   Index       For CoreTypeIndividual, the specific core to remove
 *
 * @return  UINT64      Bitmask of cores removed from service
 */
UINT64 EFIAPI VmPerfRemoveFromService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_CORE_TYPE CoreType,
    IN UINT32 Index
);

/**
 * @brief Retrieves a pointer to the main CPU -> target CPU comms page
 *
 * This function allows the client to retrieve a pointer to the buffer
 * used to communicate with the target processor.
 * The particular core at 'Index' MUST be in service.
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   Index       The core index
 *
 * @return  A pointer that can be used to access this page.
*/
VOID* EFIAPI VmPerfGetApPage(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

/**
 * @brief Probes a core in service
 *
 * @param   Ctx                 Pointer to Vm Perf Eval context
 * @param   Index               The core to probe
 * @return  VM_PERF_CORE_STATUS The status of the core that is to be probed
 */
VM_PERF_CORE_STATUS EFIAPI VmPerfProbeInService(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

/**
 * @brief Retrieves the current return value from a core
 *
 * This function returns the current return value that is present
 * in a cores control block. It is up to the client to ensure
 * there is valid data here.
 *
 * @param   Ctx                 Pointer to Vm Perf Eval context
 * @param   Index               Core Index
 * @return  The return value of the core
*/
UINT64 EFIAPI VmPerfGetReturnValue(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 Index
);

/**
 * @brief Retrieves a mask representing the cores that were enumerated
 * during the library initialization.
 *
 * @param   Ctx                 Pointer to Vm Perf Eval context
 * @return  A bitmask indicating the number of cores enumerated
*/
UINT64 EFIAPI VmPerfGetEnumeratedCoreMask(
    IN VM_PERF_EVAL_CTX *Ctx
);

/**
 * @brief Makes a real mode entry point value from a physical address
 *
 * This function will take a flat address in the lower 1MiB region
 * of physical address space and create a partitioned CS:IP starting point.
 * For use with real mode only.
 *
 * Assumes that the entry point is 4K aligned.
 *
 * @param   RealModeAddress     The address to start execution at (flat)
 * @return  The value to be used in CORE_ENTRY_INFO
 *
*/
EFI_PHYSICAL_ADDRESS EFIAPI VmPerfMakeRealModeEntryPoint(
    IN UINT64 RealModeAddress
);

/* VERY basic file access to dump test result data to the volume */

/**
 * @brief Opens a new file for writing within the testing directory
 *
 * This function will take numerous parameters to form a unique and
 * identifiable filename to house test result data.
 *
 * The core information can be omitted by setting CoreIndex to
 * VM_PERF_MAX_CORES.
 *
 * The suffix portion (Suffix and Suffix number) can be omitted by
 * setting TestSuffix to NULL.
 *
 * Example (filename):
 * <testName>_c<CoreIndex>_apic<ApicID>_<suffix><Number>
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   File        Pointer to a file handle structure
 * @param   CoreIndex   The core index the data comes from
 * @param   TestName    A test name (included in the file name)
 * @param   TestSuffix  A small suffix for this particular file
 * @param   SuffixIndex A number to append right after the suffix
 * @param   Extension   3 letter extension to use for the file
 *
 * @returns BOOLEAN     TRUE if file has been opened OK, FALSE otherwise
 *
*/
BOOLEAN EFIAPI VmPerfOpenFileForWrite(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN UINT32 CoreIndex,
    IN CHAR16 *TestName,
    IN CHAR16 *TestSuffix,
    IN UINT32 SuffixIndex,
    IN CHAR16 *Extension
);

/**
 * @brief Opens a new file for read access
 *
 * This will be open relative to the base directory of the device
 * from which the program has been loaded from. This is different
 * from the behaviour exhibited by the OpenForWrite function.
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   File        Pointer to a file handle structure
 * @param   FilePath    File path to open
 *
 * @returns TRUE if the file was opened OK, FALSE otherwise
*/
BOOLEAN EFIAPI VmPerfOpenFileForRead(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN CHAR16 *FilePath
);

/**
 * @brief Closes a previously open file
 *
 * @param   Ctx         Pointer to the Vm Perf Eval context
 * @param   File        Pointer to a file handle
 *
 * @returns BOOLEAN     TRUE if file has been closed OK, FALSE otherwise
*/
BOOLEAN EFIAPI VmPerfCloseFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File
);

/**
 * @brief Write data to a file previously opened for write access
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   File            Pointer to a file handle to write to
 * @param   Buffer          Pointer to the buffer
 * @param   BytesToWrite    Number of bytes to write
 *
 * @returns UINTN           The number of bytes actually written to the file
*/
UINTN EFIAPI VmPerfWriteFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN VOID *Buffer,
    IN UINT32 BytesToWrite
);

/**
 * @brief Read data from a file
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   File            Pointer to a file handle to read from
 * @param   Buffer          Pointer to the buffer
 * @param   BytesToRead     Number of bytes to read (size of buffer)
 *
 * @returns UINTN           The number of bytes actually read into the buffer
*/
UINTN EFIAPI VmPerfReadFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN VOID *Buffer,
    IN UINT32 BytesToRead
);

/**
 * @brief Write a formatted string to a file
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   File            Pointer to the file handle to write to
 * @param   Format          Format string
 * @param   ...             Remaining arguments
 *
 * @returns UINTN           The number of bytes actually written to the file
*/
UINTN EFIAPI VmPerfWriteFileF(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File,
    IN CHAR16 *Format,
    ...
);

/**
 * @brief Flush file buffers (for files opened for write access)
 *
 * Flushes the file buffers associated with File.
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   File            Pointer to the file handle to flush
*/
VOID EFIAPI VmPerfFlushFile(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN VM_PERF_FILE *File
);

/**
 * @brief Flush file buffers (for files opened for write access)
 *
 * Flushes the file buffers associated with File.
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   LogLevel        The log level associated with this message
 * @param   Msg             The message (and format)
 * @param   ...             The (optional and variable number) format arguments
*/
VOID EFIAPI VmPerfLog(
    IN VM_PERF_EVAL_CTX *Ctx,
    IN UINT32 LogLevel,
    IN CHAR16 *Msg,
    ...
);

/**
 * @brief Load an options file (can be used to configure test parameters)
 *
 * @param   Ctx             Pointer to the Vm Perf Eval context
 * @param   Name            The name of the options file (<Name>.opt in root)
 *
 * @returns BOOLEAN         TRUE if the file loaded OK, false otherwise
*/
BOOLEAN EFIAPI VmPerfLoadOptions(
    VM_PERF_EVAL_CTX *Ctx,
    CHAR16 *Name
);

BOOLEAN EFIAPI VmPerfGetOptionUintn(
    VM_PERF_EVAL_CTX *Ctx,
    CHAR8 *OptionName,
    UINTN *OptionValue
);

BOOLEAN EFIAPI VmPerfGetOptionString(
    VM_PERF_EVAL_CTX *Ctx,
    CHAR8 *OptionName,
    CHAR8 *OptionStringValue,
    UINT32 OptionStringLength
);

/**
 * @brief Retrieves some information about the CPU the application is currently
 * running on.
 *
 * The information retrieved includes vendor string, brand name string and
 * some pertinent CPU feature flags.
 *
 * @param   CpuInfo         Pointer to VM_PERF_CPU_INFORMATION
*/
VOID EFIAPI VmPerfGetCpuInformation(
    VM_PERF_CPU_INFORMATION *CpuInfo
);

/**
 * @brief Logs CPU information on the console.
 *
 * @param   Ctx             Pointer to Vm Perf Eval context
*/
VOID EFIAPI VmPerfLogCpuInformation(
    VM_PERF_EVAL_CTX *Ctx
);

BOOLEAN EFIAPI VmPerfReadRawDisk(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_LBA LbaStart,
    UINTN BlockCount,
    VOID *Buffer
);

BOOLEAN EFIAPI VmPerfWriteRawDisk(
    VM_PERF_EVAL_CTX *Ctx,
    EFI_LBA LbaStart,
    UINTN BlockCount,
    VOID *Buffer
);

BOOLEAN EFIAPI VmPerfCheckRawDiskStatus(
    VM_PERF_EVAL_CTX *Ctx
);

BOOLEAN EFIAPI VmPerfGetRawDiskInformation(
    VM_PERF_EVAL_CTX *Ctx,
    VM_PERF_RAW_DISK_INFORMATION *RawDiskInformation
);


#endif      /* __VM_PERF_EVAL_LIB_H__ */
