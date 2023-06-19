; Copyright 2022 The ChromiumOS Authors
; Use of this source code is governed by a BSD-style license that can be
; found in the LICENSE file.
;

;UEFI VM Performance Eval Application Processor bootstrapper

;This bootstrap code is to fit in a single page (4KiB)
;It is also meant to be position independent

;The application processor will start in what is known as "real mode".
;In this mode, the CPU essentially functions much like the original
;Intel 8086:
;1. Only has access to the first 1MiB of physical address space
;2. 16-bit registers
;
;This is why the main communication structure between main processor and the
;application processor is placed in the first 1MiB of system address space.
;
;The next operating mode is known as protected mode.
;We have 32-bit registers and a 32-bit address space.
;We can operate in 3 paging modes:
;1. None
;2. Paging On
;3. PAE Paging
;
;This is supported as we need to "pass through" this mode to enable 64-bit mode
;(IA-32e or Long Mode).
;In addition, the no paging mode could be useful for some tests as it will
;remove the need for page tables on the guest.
;
;Some of the core data structures (stacks, private working areas)
;are placed in the first 4GiB of system address space to allow
;the CPU to operate in this mode.
;
;The last mode is long mode (a.k.a x86-64 or IA-32e mode).
;64-bit registers are available as well as access to additional
;8 registers on both the interger and FP register files.
;This is generally the mode that the CPU is expected to operate in.
;
;TLDR:
;The purpose of the code below is to perform any necessary setup
;to put the CPU into a usable state and to steer it towards an
;entry point of the main processors choosing. It is expected that
;each test will have its own code to run on the application processor and
;return back once it has finished.
;
;The page tables, when paging is used, offer some isolation to prevent
;an errant program on an application processor from crashing the machine.
;If used without paging, then there is no isolation.
;Stands to say that you are on your own regardless of which mode you choose...
;
;Once this processor has passed the initialization phase and is ready to go
;into an entry point, data contained within this block MUST not be used.
;The data usage should be switched to this cores private block. Code in this
;block can be referenced without issue provided it uses the FS/GS segment
;registers to access core data structures.
;
;For real mode entry point
;
;The lower 16-bits is the IP
;The upper 16-bits is CS
;(This is of entry_point value)
;On entry
;ES=<PCIB>
;DS=Points to Bootstrapper
;On exit, AX will be sign extended to 64-bits
;and placed into returnValue in the PCIB
;Interrupts CANNOT be used for this entry point type
;
; Follow this prototype for ap function entry
; ap_entry(ap_buffer *p)
;For PM (No paging, paging, PAE paging):
;FS = <Reference PCB pointer>   [0 indexed]
;GS = <Application Pointer>     [0 indexed]
;Low 32-bits => EIP
;If Paging/PAE paging
;CR3 will be loaded with the page table pointer
;and paging will be turned on before calling the
;entry point function
;
;For Long Mode:
;FS = <Reference PCB pointer> [0 indexed]
;GS = <Application Pointer>   [0 indexed]
;64-bits => RIP of entry point
;A set of page tables MUST be set to use this mode.

;Include the defines that are to be shared between C code and this
;bootstrapper
%include "ApBoot.inc"

;The EOI regiter of the APIC
;A write of 0 to this register tells the APIC
;that we have finished servicing the
;delivered interrupt.
%define XAPIC_EOI_REGISTER      0B0h

;Used to dump real mode register state
;DS has the segment for the PCIB
%macro dump16to64 1
 mov ds:[di], %1
 mov word ds:[di+2], 0
 mov word ds:[di+4], 0
 mov word ds:[di+6], 0
%endmacro

;The parameter here is expected to be a 16-bit register
%macro dump16to32_excpcode 1
 or %1, (STATUSCODE_EXCEPTION_REAL_MODE & 0xFFFF)
 mov ds:[di], %1
 mov word ds:[di+2], STATUSCODE_EXCEPTION_REAL_MODE >> 16
%endmacro

;In this case ES has the segment to dump to
%macro dump16to32_success 1
 mov word es:[di],%1 | (STATUSCODE_SUCCESSFUL_RETURN & 0xFFFF)
 mov word es:[di+2], STATUSCODE_SUCCESSFUL_RETURN >> 16
%endmacro

%macro dump16to32 1
 mov ds:[di], %1
 mov word ds:[di+2], 0
%endmacro


;******************************************************************************
; Real Mode Entry Point
;
; This is the EXACT starting location of an AP when instructed to do so by the
; main processor running the UEFI application. It will be executing in real
; mode (hence the 'BITS 16' directive).
;
; An IDT will be setup to catch exceptions in either the real mode portion of
; the bootstrapper or a real mode entry point.
;
; If a protected mode/long mode entry point is desired, the bootstrapper will
; load up a bootstrap GDT, switch to protected mode and jump to the protected
; mode entry point.
;
; A 4KiB work page will be allocated for sole use by the bootstrapper in real
; mode. The first 1KiB of this page will be used for the IDT and the remainder
; will be used for the real mode stack.
;
; Entry:
; -> None. No entry values are expected when entering this point
;
; Exit:
; -> If instructed to enter a real mode function:
;       DS = Points to bootstrapper
;       ES = Points to the PCIB (Processor Control/Info Block)
;       -> On return: AX is sign extended to 64-bits and placed into
;          'returnvalue'
; -> If instructed to enter either protected mode/long mode:
;       This part of the code will set up some basic bootstrap structures
;       and jump into protected mode. EDX will contain the address of the
;       bootstrapper when entering protected mode.
;******************************************************************************
ORG 0x0
BITS 16
rm_entry:
cli
mov eax, cr0                            ;Clear bits 30 and 29 (CD and WT) to
and eax, 9FFFFFFFh                      ;enable writeback caching on this core
mov cr0, eax
mov ax,cs                               ;Move the CS segment value to others
mov ds,ax
mov es,ax

fclex
mov word [gdtlimit], 037Fh              ;UEFI spec stipulates that the x87
fldcw [gdtlimit]                        ;control word be set to 0x37F.
                                        ;Do this here

mov eax,[rm_work]                       ;Setup the real mode stack
shr eax, 4                              ;Stack pointer grows 'downward' so
mov ss, ax                              ;set to 1000h
mov sp,1000h

mov es, ax
mov dx, cs

xor cx,cx
xor di, di
lea ax, [rm_exceptiontable]

;******************************************************************************
; Real Mode IDT setup
; We only set the first 32 vectors as we are not interested in handling
; hardware interrupts at this point.
;******************************************************************************
rm_idtloop:
 mov es:[di], ax                        ;Offset (+0)
 mov es:[di+2], dx                      ;CS (+2)
 add ax, (rm_exceptionentryend - rm_exceptiontable)
 inc cx
 add di, 4
 cmp cx, 32
 jne rm_idtloop

mov word ds:[gdtlimit], 127             ;128-1 bytes (32 vectors * 4 bytes)
mov eax, ds:[rm_work]
mov ds:[gdtptr], eax                    ;Write the base
lidt ds:[gdtlimit]                      ;Set up our real mode IDT

mov ecx,[pcb_ptr]                       ;Set up ES to point to the PCIB
shr ecx, 4
mov es,cx

mov word es:[statuscode],1              ;We've made it past the IDT setup phase

mov edx, [run_base]
mov es:[pcib_run_base], edx             ;Save the bootstrapper base for later

mov eax, es:[entry_type]                ;We've done enough setup now, we can
cmp eax, ENTRY_REAL_MODE                ;look at the entry point type
jnz no_rm_entry

mov eax, es:[trigger_addr]              ;If trigger_addr != 0, wait for trigger
test eax, eax
jz rm_no_trigger

mov dword es:[statuscode], STATUSCODE_TRIGGER_WAIT

mov edx, eax                            ;This code block waits for the trigger
shr edx, 4                              ;value to be set

push ds
mov ds, dx
and ax, 0Fh
mov si, ax

rm_trigger_loop:                        ;Trigger word wait loop
mov ebx, ds:[si]
test ebx, ebx
jz rm_trigger_loop

pop ds

rm_no_trigger:
mov ax, es:[entry_point]                ;Set up our far call target
mov [rm_farjmp], ax
mov ax, es:[entry_point + 2]
mov [rm_farjmp + 2], ax

mov dword es:[statuscode], STATUSCODE_IN_ENTRY_POINT

push ds                                 ;Save our segment registers
push es
call far [rm_farjmp]                    ;Call the real mode entry point
pop es
pop ds
xor edx, edx
movsx eax, ax                           ;Sign extend the return value into a
mov es:[returnvalue], eax               ;32-bit value

test ax, ax                             ;Generate the upper 32-bits based on
jns rm_ret_not_neg                      ;the sign of the return value from the
dec edx                                 ;real mode code.
rm_ret_not_neg:
mov es:[returnvalue + 4], edx

lea di, [statuscode]                    ;We're done, indicate this to the main
dump16to32_success 0                    ;processor and halt this core.
jmp rm_halt


;******************************************************************************
; Preparation for entering protected mode
;
; We are not to stay in real mode, so perform any preperations we need to
; do in order to enter protected mode.
;******************************************************************************
no_rm_entry:
mov word [gdtlimit], (pm_gdt_end - pm_gdt) - 1
mov edx,[run_base]
lea eax, [edx + pm_gdt]
mov [gdtptr], eax
lgdt [gdtlimit]                         ;Load our bootstrap GDT

mov word es:[statuscode],2

mov edx,[run_base]                      ;Setup our long jump into the protected
add edx, pm_entry                       ;mode entry point
mov [pm_jmpoffset], edx

cpuid                                   ;Since this is technically self
                                        ;modifying code, we need to execute
                                        ;a serializing instruction after
                                        ;modifying the code. CPUID serves this
                                        ;purpose.

mov edx,[run_base]                      ;To pass into the PM entry point
mov eax, cr0
or eax, 10001h                          ;Enable protected mode and write
mov cr0, eax                            ;protect bits in CR0

db 66h                                  ;Operand Size Prefix
db 0eah                                 ;JMP Long
pm_jmpoffset dd 0                       ;JMP offset (patched above)
dw 0008h                                ;Code segment in bootstrap GDT

rm_halt:
cli                                     ;This loop will clear interrupts and
hlt                                     ;halt the CPU. This effectively halts
jmp rm_halt                             ;the CPU until it is reset.


rm_exceptiontable:                      ;Some macro magic to generate real mode
%assign i 0                             ;exception handlers
%rep 32
 pusha                                  ;Save all registers onto the stack
 mov al,i                               ;Put the vector number into AL
 jmp rm_exception                       ;Jump to generic exception handler
%if i == 0
 rm_exceptionentryend:                  ;Put the tag here so we can determine
%endif                                  ;how many bytes are used for each code
%assign i i+1                           ;block
%endrep


;******************************************************************************
; Real Mode Exception Handler
;
; Inputs:
;   -> AL = Vector Number (see the above)
;
; This code will dump the state of the CPU into the PCIB and halt itself.
;******************************************************************************
rm_exception:
push ds
push es
push ss

mov bx, cs
mov ds, bx
mov es, bx
mov ebx, [pcb_ptr]                      ;Set up a pointer to the PCIB
shr ebx, 4
mov ds, bx
lea di,[genregs + (7*8)]                ;Point to the end of the general
                                        ;register array

pop bx                                  ;Dump SS
mov ds:[excp_ss], bx
pop bx                                  ;Dump ES
mov ds:[excp_es], bx
pop bx                                  ;Dump DS
mov ds:[excp_ds], bx

mov cx, 8

rm_regdump_loop:
pop bx                                  ;Restore the register from the
dump16to64 bx                           ;stack and extended it to 64-bits
sub di, 8                               ;PUSHA used above saves the registers.
dec cx                                  ;So we need to pull them off in the
jnz rm_regdump_loop                     ;following order
                                        ;DI, SI, BP, SP, BX, DX, CX, AX

pop bx                                  ;Dump the IP
lea di,[excp_rip]
dump16to64 bx

pop bx                                  ;Dump CS
lea di,[excp_cs]
mov ds:[di], bx

lea di, [flagsreg]                      ;Dump the flags register
pop bx
dump16to32 bx

lea di,[statuscode]                     ;Set the status code and halt the CPU
movzx ax, al
dump16to32_excpcode ax
jmp rm_halt

TIMES 1640-($-$$) db 0

;******************************************************************************
; Bootstrap GDT
;
; We have 1 NULL descriptor, 1 Code descriptor, 1 Data descriptor and
; 1 64-bit code descriptor (for long mode use).
;
; This is a pre-generated GDT to use for the initial switch into protected
; mode.
;******************************************************************************

pm_gdt  dd 00000000h                    ;Descriptor 0 (Null)
        dd 00000000h
pm_code dd 0000FFFFh                    ;Descriptor 1 (Code)
        dd 00CF9A00h                    ;Execute/Read
pm_data dd 0000FFFFh                    ;Descriptor 2 (Data)
        dd 00CF9200h                    ;Read/Write
pm_64b  dd 0000FFFFh                    ;Descriptor 3 (64-bit code)
        dd 00AF9A00h                    ;64-bit (Do not use in protected mode)

%rep 32
        dd 0000FFFFh                    ;Descriptor n (Code)
        dd 00CF9A00h                    ;Execute/Read
%endrep
pm_gdt_end:


;******************************************************************************
; Protected Mode Entry Point
;
; After the real mode portion of the bootstrapper has switched the CPU into
; protected mode, it will start execution here.
;
; The protected mode portion of the code will setup both a GDT and IDT suitable
; for use by this core in its private memory block. Once it has completed this
; work, it will once again check the entry point to determine if we should stay
; in protected mode or continue on into long mode.
;
; Entry:
;  EDX = Bootstrapper Base Address
;
; Exit:
;
;******************************************************************************
BITS 32
pm_entry:

mov ax, 10h                             ;We need to use descriptor 2 (* 8 here)
mov ds, ax                              ;For data references
mov es, ax
mov esi, edx                            ;Transfer bootstrapper base
mov edx,[esi + pcb_ptr]                 ;Grab PCIB for this processor

mov ss, ax                              ;Set up stack pointer
mov esp, [edx + lm_stack]

mov dword [edx + statuscode], 5         ;Indicate that we have entered
                                        ;proctected mode
mov edi, [edx + priv_block]
mov ebp, edx

lea edi, [edi + PRIV_GDT]               ;Start our private GDT setup

mov dword [edi],   0                    ;NULL descriptor
mov dword [edi+4], 0

add edi, 8

mov eax, [esi + pm_code]                ;Grab the 32-bit code descriptors
mov edx, [esi + pm_code + 4]

mov ecx, 256                            ;We will setup 256 GDT values as
                                        ;we can use this to figure out
                                        ;which vector is being processed
                                        ;by the core. A generic handler
                                        ;can be used if it is setup this way

pmdesc_loop:
 mov [edi], eax                         ;Write the code descriptor
 mov [edi + 4], edx
 add edi, 8
 dec ecx
 jnz pmdesc_loop

mov eax, [esi + pm_data]                ;Tack on a data descriptor
mov edx, [esi + pm_data + 4]
mov [edi], eax
mov [edi + 4], edx
add edi, 8

mov eax, [esi + pm_64b]                 ;Tack on a 64-bit code descriptor
mov edx, [esi + pm_64b + 4]
mov [edi], eax
mov [edi + 4], edx
add edi, 8


mov ebx, [esi + pcb_ptr]                ;PCIB descriptor (it is a single page)
mov eax, 4096
call pm_post_descriptor

add edi, 8

mov ebx, [ebp + ap_buffer]              ;AP buffer block
mov eax, [ebp + ap_size]
call pm_post_descriptor
add edi, 8

mov eax,[ebp + priv_block]              ;Setup the TSS descriptor
lea eax, [eax + PRIV_TSS]               ;It is only used in long mode

add edi, 8

mov dword [edi+12], 0
mov dword [edi+8], 0                    ;Assume 63:32 bits are 0
mov edx, eax
shl edx, 16
or edx, (8192 + 256) - 1                ;TSS is in the first 256 bytes
mov [edi], edx                          ;Plus 8KiB for the IOPM bitmap
                                        ;64K IOPort space / 8 bits
mov edx, eax
and edx, 0FF000000h
and eax,0FF0000h
shr eax, 16
or edx, eax
or edx, 808900h
mov [edi+4], edx

add edi, 16


;******************************************************************************
; The private GDT has been setup by the time the code has reached this point
;
; The private GDT is structured as follows:
; 0         = NULL Descriptor
; 1 - 256   = Code Descriptors
; 257       = Data Descriptor
; 258       = 64-bit code Descriptor
; 259       = FS Descriptor (for direct PCIB access)
; 260       = GS Descriptor (for direct AP buffer access)
; 261       = Dummy descriptor
; 262 + 263 = TSS Descriptor (for 64-bit mode)
;
; Remember when loading these values into segment registers, they needed to be
; shifted to the left by 3 places (Table Select and Requested Privilege
; Level bits).
;******************************************************************************

sub edi, [ebp + priv_block]
sub edi, 1                              ;Calculate the new GDT limit

mov ebx, [ebp + priv_block]
lea ebx, [ebx + PRIV_GDT]               ;Grab the pointer to the new GDT

mov esi, [ebp + priv_block]
lea esi, [esi + PRIV_SCRATCH]

mov [esi], di
mov [esi + 2], ebx

lgdt [esi]                              ;Use the scratch to load the new GDT

mov dx, 257 << 3                        ;With the new GDT set, reload the
mov ds, dx                              ;descriptors
mov es, dx
mov dx, 259 << 3                        ;FS
mov fs, dx
mov dx, 260 << 3                        ;GS
mov gs, dx

mov eax, fs:[pcib_run_base]

lea edx,[eax + pm_errorcode]            ;EDX = Error Code Exception Hanlder
lea esi, [eax + pm_noerrorcode]         ;ESI = No Error Code Exception Handler

mov edi, fs:[priv_block]
lea edi, [edi + PRIV_IDT]
xor ecx, ecx

;******************************************************************************
; This is our protected mode IDT build code
;
; Each entry within the IDT points to one of the following 3 handlers:
;
; 1. Error Code Exception Handler
;    For some exceptions, the CPU will push an error code onto the stack.
;    The information conveyed by this error code depends on the exception type.
;    See either Intel's or AMD's documentation on this for more details.
;
; 2. No Error Code Exception Handler
;    For these exceptions, the CPU will NOT push an error code onto the stack.
;
; 3. Interrupt Handler Stub
;    This code's responsibility to perform any early data capture and pass
;    control onto a handler of the applications choice. This is a generic
;    handler that uses the value of the CS register to determine which
;    handler to invoke.
;******************************************************************************
idt_buildloop:
 mov eax, edx                           ;Start off with the Error Code Handler
 cmp ecx, 8                             ;Exception 8 (#DF)
 jz pm_idt_build
 cmp ecx, 10                            ;Exception 10 (#TS)
 jz pm_idt_build
 cmp ecx, 11                            ;Exception 11 (#NP)
 jz pm_idt_build
 cmp ecx, 12                            ;Exception 12 (#SS)
 jz pm_idt_build
 cmp ecx, 13                            ;Exception 13 (#GP)
 jz pm_idt_build
 cmp ecx, 14                            ;Exception 14 (#PF)
 jz pm_idt_build
 cmp ecx, 17                            ;Exception 17 (#AC)
 jz pm_idt_build
 cmp ecx, 21                            ;Exception 21 (#CP)
 jz pm_idt_build
 mov eax, esi                           ;If all the above checks fail, grab the
                                        ;non-error code handler

 cmp ecx, 32                            ;For vector numbers 32 or more
 jb pm_idt_build                        ;use the interrupt handler stub

 mov eax, fs:[pcib_run_base]
 lea eax, [eax + pm_ext_int]            ;Calculate absolute offset of stub

pm_idt_build:
 mov ebx, eax                           ;Grab the interrupt offset
 and ebx, 0FFFFh

 add ecx, 1
 shl ecx, 19                            ;Insert RPL/TI bits
                                        ;(RPL = 0 TI = 0 (GDT))

 or ebx, ecx                            ;Insert segment descriptor in Gate
 shr ecx, 19
 sub ecx, 1

 mov [edi], ebx                         ;Low 32-bits of the Interrupt Gate

 mov ebx, eax
 and ebx, 0FFFF0000h
 mov bx, 8E00h                          ;Segment Present = 1, DPL = 0
                                        ;32-bit gate size

 mov [edi + 4], ebx                     ;Upper 32-bits of the Interrupt Gate

 add edi, 8                             ;Move to next descriptor in memory
 inc ecx
 cmp ecx, 256                           ;CX < 256, if so branch back
 jb idt_buildloop

mov edi, fs:[priv_block]
lea edi, [edi + PRIV_IDT]

mov eax, fs:[priv_block]
lea eax, [eax + PRIV_SCRATCH]

mov word [eax], 2047                    ;Limit is (8 * 256) - 1
mov dword [eax + 2], edi
lidt [eax]                              ;Load the new IDT

mov edx, cr4                            ;Enable OSFXSR and 4M PSE for SIMD
or edx, 210h                            ;access and 32-bit paging respectively
mov cr4, edx                            ;We can't access MXCSR without this


mov dword [eax], 01F80h                 ;UEFI spec stipulates that MXCSR should
ldmxcsr [eax]                           ;be set to 0x1F80, do this here.

mov edi, fs:[priv_block]
lea edi, [edi + PRIV_INTJMP]

mov ecx, 0
xor eax, eax                            ;Set up interrupt jump table

pm_intjmploop:
 mov [edi], eax
 inc ecx
 add edi, 4
 cmp ecx, 256
 jb pm_intjmploop


;******************************************************************************
; We have completed basic protected mode initialization once we have reached
; this point.
; At this point, we are ready to either enter a protected mode entry point
; or we can continue on to long mode.
;
; The steps we take will depend on what entry point type we are using.
; ENTRY_PM_NO_PAGING:
;   No further work is necessary as we have setup what is needed to continue.
;   We will either wait for the trigger or go directly into the entry point.
;
; ENTRY_PM_PAGING:
;   We will load the page table pointer into the CR3 register and then switch
;   on paging. Once that is done, we will continue to check for trigger and
;   then proceed to the entry point.
;
; ENTRY_PM_PAE_PAGING:
;   We will also load the page table pointer into CR3. PAE will be turned on
;   first, then paging will be turned on after. Once that has been completed,
;   we will continue on as normal.
;
; If we are to go onto long mode, paging will be left off. This is because,
; we still need to patch the jump offset for entry into long mode.
;
; If the protected mode entry point returns, the return value in EAX will be
; sign extended and placed in the PCIB for analysis by the main processor.
;******************************************************************************
mov eax, fs:[entry_type]
cmp eax, ENTRY_PM_NO_PAGING
jnz no_entry_pm_no_paging               ;If it's NO_PAGING, we'll fall through


pm_enter_ap_func:                       ;Common point that is used to enter
mov eax, fs:[trigger_addr]              ;the user specified entry point
test eax, eax
jz pm_no_trigger

mov dword fs:[statuscode], STATUSCODE_TRIGGER_WAIT


pm_trigger_loop:                        ;Wait for trigger loop
mov edx, [eax]
test edx, edx
jz pm_trigger_loop

pm_no_trigger:
mov dword fs:[statuscode], STATUSCODE_IN_ENTRY_POINT

mov eax, fs:[entry_point]               ;Call entry point with the necessary
push dword fs:[ap_buffer]               ;arguments (callee popped)
call eax

xor edx, edx                            ;Sign extend return value
test eax, eax
jns pm_notsigned
dec edx
pm_notsigned:
mov fs:[returnvalue], eax
mov fs:[returnvalue + 4], edx
mov dword fs:[statuscode], STATUSCODE_SUCCESSFUL_RETURN
jmp pm_halt


no_entry_pm_no_paging:
cmp eax, ENTRY_PM_PAGING
jz pm_entry_paging
cmp eax, ENTRY_PM_PAE_PAGING
jz pm_entry_paging
jmp pm_init_continue                    ;Ostensibly long mode is desired, but
                                        ;no explicit check
pm_entry_paging:
mov edx, fs:[lm_pt]                     ;Load the page table pointer into CR3
mov cr3, edx


cmp eax, ENTRY_PM_PAE_PAGING            ;Check if PAE paging is desired
jnz pm_no_pae

mov edx, cr4
or edx, 20h                             ;Enable PAE
mov cr4, edx

pm_no_pae:
mov edx, cr0
or edx, 80000000h                       ;Enable paging
mov cr0, edx
jmp pm_enter_ap_func                    ;Jump back to enter the protected mode
                                        ;entry point

;******************************************************************************
; Long Mode Setup
;   Protected Mode is not desired at this point, so the remaining option is
;   long mode.
;
;   This code will do the very minimum necessary to switch the CPU into long
;   mode. This is the mode in which 64-bit code can be executed.
;******************************************************************************
pm_init_continue:
mov edx, cr4
or edx, 20h                             ;Enable PAE (required for long mode)
mov cr4, edx

mov edx, fs:[pcib_run_base]             ;Same as done for real -> protected
mov ecx, edx                            ;Patch the upcoming offset
add edx, lm_entry
mov [ecx + lm_jmpoffset], edx

cpuid                                   ;Synchronization (as before)

mov eax, fs:[lm_pt]                     ;Load the page table pointer
mov cr3, eax

mov ecx, 0C0000080h                     ;IA32_EFER MSR
rdmsr
or eax, 100h                            ;Enable Long Mode
wrmsr

mov dword fs:[statuscode], 9            ;Long mode has been activated

mov eax, cr0
or eax, 80000000h                       ;Enable paging (required for long mode)
mov cr0, eax

db 0eah                                 ;Long jump into long mode code
lm_jmpoffset dd 0                       ;Jump offset (patched above)
dw 0810h                                ;64-bit code segment in GDT

pm_halt:
cli
hlt
jmp pm_halt


pm_dumpregs:
mov eax, cr2                            ;Dump CR2 (extend to 64-bits)
mov fs:[cr2value], eax
mov dword fs:[cr2value+4], 0

mov eax, cr0                            ;Dump CR0 (also extend to 64-bits)
mov fs:[cr0value], eax
mov dword fs:[cr0value+4], 0

mov ax, ss                              ;Dump SS
mov fs:[excp_ss], ax

mov ax, es                              ;Dump ES
mov fs:[excp_es], ax

mov ax, ds                              ;Dump DS
mov fs:[excp_ds], ax

mov edi, genregs + (7 * 8)              ;Point to last entry in the general
                                        ;register array

mov ecx, 8                              ;8 registers to save
pm_regsaveloop:
 mov eax, [ebp]                         ;Grab 32-bit register
 mov fs:[edi], eax                      ;Zero extend to 64-bits in array
 mov dword fs:[edi+4], 0
 add ebp, 4
 sub edi, 8
 dec ecx
 jnz pm_regsaveloop
 ret


pm_noerrorcode:
pushad                                  ;Save all registers on the stack
mov ebp, esp                            ;Save the current stack pointer
call pm_dumpregs
mov dword fs:[errorcode], 0             ;Clear out error code
popad                                   ;Remove what we pushed on the stack

pm_exception_end:
mov eax, [esp]                          ;Dump EIP (zero extend to 64-bits)
mov fs:[excp_rip], eax
mov dword fs:[excp_rip + 4], 0

mov eax, [esp + 4]                      ;Dump CS
mov fs:[excp_cs], ax

mov eax, [esp + 8]
mov fs:[flagsreg], eax                  ;Dump EFLAGS


mov ax, cs                              ;Indicate to the main processor
movzx eax, ax                           ;the exception vector that caused this
shr eax, 3                              ;Remove the TI and RPL bits
sub eax, 1
or eax, STATUSCODE_EXCEPTION_PROTECTED_MODE
mov dword fs:[statuscode], eax
jmp pm_halt


pm_errorcode:
pushad                                  ;Save all registers on stack
mov ebp, esp
call pm_dumpregs
popad                                   ;Remove what we pushed off the stack

mov eax, [esp]                          ;Save exception error code in PCIB
mov fs:[errorcode], eax

add esp, 4                              ;Skip the error code
jmp pm_exception_end


;******************************************************************************
; Protected Mode Interrupt Stub
;   This is the very first piece of code that is executed when an interrupt
;   is triggered on this CPU.
;
;   The TSC will be sampled very early in the interrupt service routine
;   to get an idea of when the interrupt service began execution.
;
;   We are only saving registers that are not expected to be saved by the
;   interrupt hanlder function (using the EFI ABI).
;
;   Interrupt Function Prototype:
;    ap_interrupt(ap_buffer *p, uint64_t tsc, uint32_t vector)
;******************************************************************************
pm_ext_int:
push eax                                ;Save these registers
push edx
lfence                                  ;To sample the TSC at this point
rdtsc                                   ;Read the TSC (into EDX:EAX)
lfence
push ecx

mov cx, cs                              ;Figure out which vector this
shr cx, 3                               ;interrupt originated from
sub cx, 1
movzx ecx, cx
push ecx                                ;vector
push edx                                ;tsc high
push eax                                ;tsc low
push dword fs:[ap_buffer]               ;ap_buffer
mov edx, fs:[priv_block]

mov eax, [edx + PRIV_INTJMP + (ecx * 4)]
inc dword [edx + PRIV_INTCNT + (ecx * 4)]

test eax, eax
jz pm_int_no_handler
call eax                                ;Call the interrupt handler

pm_int_no_handler:
mov ecx, fs:[apic_base]                 ;Send an EOI to the APIC
mov dword [ecx + XAPIC_EOI_REGISTER], 0

pop ecx
pop edx
pop eax
iret

;******************************************************************************
; Generates a GDT-suitable descriptor based on a base address and size
; For use in protected mode code. But the generated descriptor is suitable for
; use in long mode (only on FS and GS segment registers)
;
; Entry:
;  EDI = Destination address to write the descriptor to (8 bytes)
;  EBX = Base address
;  EAX = Number of bytes pointed to by the base address
;******************************************************************************
pm_post_descriptor:
pushad
mov ebx, [esi + pcb_ptr]
push ebx

shl ebx, 16
sub eax, 1
or ebx, eax                             ;Limit = EAX - 1

mov [edi], ebx

pop ebx
mov edx, ebx

and ebx, 0FF000000h
and edx, 0FF0000h
shr edx, 16
or ebx, edx                             ;Fill in 31:24 and 23:16 of the base
or ebx, 00C09200h                       ;D/B = 1, present, s =1 (code/data)
mov [edi+4], ebx                        ;type=Data, R/W
popad
ret

;******************************************************************************
; Long Mode Entry Point
;
; After long mode has been enabled, this will be the point in which the CPU
; will start executing 64-bit code. However, there is still a bit more work
; to do before we can enter a long mode entry point.
;
; 1. The IDT must be moved to a Long Mode compatible one. This is for both
;    exceptions and external interrupts.
;
; 2. The code descriptors in the GDT need to be replaced with their 64-bit
;    equivalents.
;
; 3. A TSS needs to be generated, this just means setting up separate stacks
;    for interrupts, non-maskable interrupts and exceptions.
;
; Entry:
;   Nothing
;
; Exit:
;   No other options remain, it will be going into the user specified entry
;   point.
;******************************************************************************
BITS 64
lm_entry:

mov ebp, fs:[pcib_run_base]
mov edi, fs:[priv_block]
lea rdi, [rdi + PRIV_GDT]               ;Grab a pointer to the GDT

mov rax, [rbp + pm_64b]                 ;Read the 64-bit code descriptor

xor rdx, rdx
mov [rdi], rdx                          ;Replace the NULL descriptor
add edi, 8

mov ecx, 256
lm_codedesc:                            ;Fill in 256 64-bit code descriptors
mov [rdi], rax
add edi, 8
dec ecx
jnz lm_codedesc

mov ax, ds                              ;Reload the descriptors now that we are
mov ds, ax                              ;in 64-bit mode
mov ss, ax
mov esp, fs:[lm_stack]                  ;Reload SS/RSP

mov ax, es                              ;Reload descriptors
mov es, ax

mov ax, fs
mov fs, ax

mov ax, gs
mov gs, ax


mov edi,fs:[priv_block]
mov r11d, edi

lea rdi, [rdi + PRIV_IDT]
lea r11, [r11 + PRIV_SCRATCH]


;******************************************************************************
; This is our long mode IDT build code
;
; Very similar to the protected mode code, but with 64-bit offsets and a
; different descriptor format
;******************************************************************************

mov word [r11], 4095                    ;Limit
mov [r11+2], rdi                        ;Offset
lidt [r11]                              ;Reload IDT for long mode

lea r8, [rbp + lm_exception]
lea r9, [rbp + lm_exception_error_code]
lea r10, [rbp + lm_ext_int]

xor ecx, ecx

lm_idtloop:
 mov rax, r9
 cmp ecx, 8                             ;Exception 8 (#DF)
 jz lm_idtbuild
 cmp ecx, 10                            ;Exception 10 (#TS)
 jz lm_idtbuild
 cmp ecx, 11                            ;Exception 11 (#NP)
 jz lm_idtbuild
 cmp ecx, 12                            ;Exception 12 (#SS)
 jz lm_idtbuild
 cmp ecx, 13                            ;Exception 13 (#GP)
 jz lm_idtbuild
 cmp ecx, 14                            ;Exception 14 (#PF)
 jz lm_idtbuild
 cmp ecx, 17                            ;Exception 17 (#AC)
 jz lm_idtbuild
 cmp ecx, 21                            ;Exception 21 (#CP)
 jz lm_idtbuild
 mov rax, r8

 cmp ecx, 32
 jb lm_idtbuild                         ;If < 32, point to int stub

 mov rax, r10                           ;Pointer to the external int stub

lm_idtbuild:
 mov rbx, rax                           ;Formatting the interrupt handler
 shr rbx, 32                            ;offset into the descriptor

 mov dword [rdi + 12], 0
 mov [rdi + 8], ebx
 mov rbx, rax

 and ebx, 0FFFFh
 mov r11d, ecx
 add r11d, 1
 shl r11d, 19
 or ebx, r11d
 mov [rdi], ebx

 and eax, 0FFFF0000h

 cmp ecx, 2
 jnz not_nmi

  or eax, 08E03h                        ;Set to IST 3 (NMI)
  jmp lm_idtdone
not_nmi:
 cmp ecx, 32
 jae lm_useintstack

  or eax, 8E01h                         ;Set to IST 1 (Exception)
  jmp lm_idtdone

lm_useintstack:
 or eax, 08E02h                         ;Set to IST 2 (Interrupt)

lm_idtdone:
 mov [rdi + 4], eax
 add rdi, 16
 inc ecx
 cmp ecx, 256
 jnz lm_idtloop

mov edi, fs:[priv_block]
lea rdi, [rdi + PRIV_TSS]               ;Get the pointer to the TSS

mov eax, fs:[lm_stack]
mov [rdi + 4], rax                      ;Set up the stack

mov eax, fs:[lm_intstack]               ;Set up the interrupt/exception/nmi
mov [rdi + 36], rax                     ;stacks

mov eax, fs:[lm_excpstack]
mov [rdi + 44], rax

mov rax, fs:[lm_nmistack]
mov [rdi + 52], rax

mov dword [rdi + 100], (256) << 16      ;Set the IOPM base to be +256 bytes

mov eax, 262 << 3                       ;Load the task register
ltr ax

mov eax, fs:[priv_block]
lea rdi, [rax + PRIV_INTJMP]            ;Set up the interrupt jump table
xor eax, eax

mov ecx, 0

lm_intjmploop:
mov [rdi], rax
add rdi, 8
inc ecx
cmp ecx, 256
jb lm_intjmploop


mov dword fs:[statuscode], 11

mov eax, fs:[trigger_addr]              ;Test to see if we should wait for
test eax, eax                           ;a trigger write
jz lm_no_trigger

mov dword fs:[statuscode], STATUSCODE_TRIGGER_WAIT

lm_trigger_loop:                        ;Wait for trigger loop
mov edx, [eax]
test edx, edx
jz lm_trigger_loop


lm_no_trigger:
mov dword fs:[statuscode], STATUSCODE_IN_ENTRY_POINT
mov rax, fs:[entry_point]

lea rsp, [rsp - 32]                     ;Allocate shadow storage space
mov ecx, fs:[ap_buffer]                 ;Pointer to the AP buffer
call rax                                ;Call the long mode entry point

mov fs:[returnvalue], rax               ;Save return value and halt CPU
mov dword fs:[statuscode], STATUSCODE_SUCCESSFUL_RETURN

lm_halt:
cli
hlt
jmp lm_halt

lm_exception_dumpregs:
mov fs:[genregs + (IDX_AX * 8)], rax    ;Save the registers to the PCIB
mov fs:[genregs + (IDX_BP * 8)], rbp    ;PUSHA/PUSHAD is an illegal instruction
mov fs:[genregs + (IDX_CX * 8)], rcx    ;in long mode, so it cant be used.
mov fs:[genregs + (IDX_DX * 8)], rdx
mov fs:[genregs + (IDX_SI * 8)], rsi
mov fs:[genregs + (IDX_DI * 8)], rdi
mov fs:[genregs + (IDX_BX * 8)], rbx



mov fs:[genregs + (IDX_R8 * 8)], r8
mov fs:[genregs + (IDX_R9 * 8)], r9
mov fs:[genregs + (IDX_R10 * 8)], r10
mov fs:[genregs + (IDX_R11 * 8)], r11
mov fs:[genregs + (IDX_R12 * 8)], r12
mov fs:[genregs + (IDX_R13 * 8)], r13
mov fs:[genregs + (IDX_R14 * 8)], r14
mov fs:[genregs + (IDX_R15 * 8)], r15


mov ax, ds                              ;Dump DS
mov fs:[excp_ds], ax

mov ax, es                              ;Dump ES
mov fs:[excp_es], ax

mov rax, cr2                            ;Dump CR2
mov fs:[cr2value], rax

mov rax, cr0                            ;Dump CR0
mov fs:[cr0value], rax
ret



lm_exception_error_code:
call lm_exception_dumpregs
mov rbp, rsp

mov rax, [rbp]
mov fs:[errorcode], eax                 ;Dump the error code

add rbp, 8

lm_handle_exception_frame:
mov rax, [rbp]                          ;Dump RIP
mov fs:[excp_rip], rax

mov rax, [rbp + 8]                      ;Dump CS
mov fs:[excp_cs], ax

mov rax, [rbp + 16]                     ;Dump EFLAGS (32-bits!)
mov fs:[flagsreg], eax

mov rax, [rbp + 24]
mov fs:[genregs + (IDX_SP) * 8], rax    ;Dump RSP

mov rax, [rbp + 32]                     ;Dump SS
mov fs:[excp_ss], ax
jmp lm_exception_done


lm_exception:
call lm_exception_dumpregs              ;Dump
mov rbp, rsp
jmp lm_handle_exception_frame

lm_exception_done:
mov eax, STATUSCODE_EXCEPTION_LONG_MODE
mov bx, cs                              ;Get the exception vector
shr bx, 3                               ;Set the status code and halt the CPU
sub bx, 1
movzx ebx, bx
or eax, ebx
mov fs:[statuscode], eax
jmp lm_halt


;******************************************************************************
; Long Mode Interrupt Stub Code
;
; Very similar to the protected mode code, but handles things differently
; because we are in 64-bit mode.
;
; Similar to the protected mode, the stub will save registers that are not
; expected to be saved by the interrupt handler function.
;
; Interrupt Function Prototype:
;  ap_interrupt(ap_buffer *p, uint64_t tsc, uint32_t vector)
;******************************************************************************
lm_ext_int:
push rax                               ;Same as protected mode
push rdx
lfence                                 ;We are surrounding RDTSC with lfence
rdtsc                                  ;as we want the TSC to be sampled at this
lfence                                 ;point in the instruction stream
shl rdx, 32
or rdx, rax                            ;Combine TSC sample into single 64-bit value
push rcx
push r8
push r9
push r10
push r11
push rbx
push rbp

mov ecx, fs:[ap_buffer]
mov ax, cs
shr ax, 3
sub ax, 1
movzx r8d, ax
mov eax, fs:[priv_block]
mov rbx, [rax + PRIV_INTJMP + (r8 * 8)]
inc dword [rax + PRIV_INTCNT + (r8 * 4)]
test rbx, rbx
jz no_lm_int_handler
mov rbp, rsp                            ;This register MUST be saved by callee if used
lea rsp, [rsp - 32]                     ;Allocate shadow stack space before call
call rbx
mov rsp, rbp                            ;Restore it
no_lm_int_handler:

mov r11d, fs:[apic_base]                ;Send an EOI to APIC
mov dword [r11d + XAPIC_EOI_REGISTER], 0
pop rbp
pop rbx
pop r11
pop r10
pop r9
pop r8
pop rcx
pop rdx
pop rax
iretq

TIMES 4074-($-$$) db 0
rm_farjmp dw 0                          ;IP
          dw 0                          ;CS
gdtlimit  dw 0                          ;GDT Limit Value
gdtptr    dd 0                          ;GDT pointer value

;These variables are to be patched by the main processor before bootstrapping
;this core.
rm_work   dd 0                          ;+4084 (1021) Real Mode Work space memory
pcb_ptr   dd 0		                ;+4088 (1022) Processor Control/Info Block
run_base  dd 0		                ;+4092 (1023) Base Address (32-bit) of this boot page
