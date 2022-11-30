
/* Copyright 2022 The ChromiumOS Authors
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

/* This is an auto generated file, DO NOT EDIT! */
#include <Uefi.h>
#pragma pack( push )
#pragma pack( 1 )
#define PRIV_GDT 0x0000
#define PRIV_IDT 0x1000
#define PRIV_TSS 0x2000
#define PRIV_IOPM 0x2100
#define PRIV_INTJMP 0x4100
#define PRIV_INTCNT 0x4900
#define PRIV_SCRATCH 0x4D00
#define ENTRY_REAL_MODE 0x0A
#define ENTRY_PM_NO_PAGING 0x0B
#define ENTRY_PM_PAGING 0x0C
#define ENTRY_PM_PAE_PAGING 0x0D
#define ENTRY_LONG_MODE 0x0E
#define GENREG_MULT 8
#define IDX_AX 0
#define IDX_CX 1
#define IDX_DX 2
#define IDX_BX 3
#define IDX_SP 4
#define IDX_BP 5
#define IDX_SI 6
#define IDX_DI 7
#define IDX_R8 8
#define IDX_R9 9
#define IDX_R10 10
#define IDX_R11 11
#define IDX_R12 12
#define IDX_R13 13
#define IDX_R14 14
#define IDX_R15 15
#define STATUSCODE_IN_ENTRY_POINT 0x60000000
#define STATUSCODE_TRIGGER_WAIT 0x70000000
#define STATUSCODE_SUCCESSFUL_RETURN 0x80000000
#define STATUSCODE_EXCEPTION_REAL_MODE 0x90000000
#define STATUSCODE_EXCEPTION_PROTECTED_MODE 0xA0000000
#define STATUSCODE_EXCEPTION_LONG_MODE 0xB0000000
#define STATUSCODE_MASK 0xF0000000
typedef struct {
UINT32 lm_pt;
UINT32 lm_stack;
UINT32 lm_intstack;
UINT32 lm_nmistack;
UINT32 lm_excpstack;
UINT32 trigger_addr;
UINT32 priv_block;
UINT32 ap_buffer;
UINT32 ap_size;
UINT64 entry_point;
UINT32 entry_type;
UINT32 apic_base;
UINT32 statuscode;
UINT64 returnvalue;
UINT64 cr0value;
UINT64 cr2value;
UINT16 excp_ds;
UINT16 excp_es;
UINT16 excp_ss;
UINT16 excp_cs;
UINT64 genregs[16];
UINT32 errorcode;
UINT32 flagsreg;
UINT64 excp_rip;
UINT32 pcib_run_base;
} PCIB;
#pragma pack( pop )
