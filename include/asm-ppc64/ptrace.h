#ifndef _PPC64_PTRACE_H
#define _PPC64_PTRACE_H

/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 *
 * this should only contain volatile regs
 * since we can keep non-volatile in the thread_struct
 * should set this up when only volatiles are saved
 * by intr code.
 *
 * Since this is going on the stack, *CARE MUST BE TAKEN* to insure
 * that the overall structure is a multiple of 16 bytes in length.
 *
 * Note that the offsets of the fields in this struct correspond with
 * the PT_* values below.  This simplifies arch/ppc/kernel/ptrace.c.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define INIT_TASK_SIZE			4*PAGE_SIZE

#ifndef __ASSEMBLY__
#define PPC_REG unsigned long
struct pt_regs {
	PPC_REG gpr[32];
	PPC_REG nip;
	PPC_REG msr;
	PPC_REG orig_gpr3;	/* Used for restarting system calls */
	PPC_REG ctr;
	PPC_REG link;
	PPC_REG xer;
	PPC_REG ccr;
	PPC_REG softe;		/* Soft enabled/disabled */
	PPC_REG trap;		/* Reason for being here */
	PPC_REG dar;		/* Fault registers */
	PPC_REG dsisr;
	PPC_REG result; 	/* Result of a system call */
};

#define PPC_REG_32 unsigned int
struct pt_regs32 {
	PPC_REG_32 gpr[32];
	PPC_REG_32 nip;
	PPC_REG_32 msr;
	PPC_REG_32 orig_gpr3;	/* Used for restarting system calls */
	PPC_REG_32 ctr;
	PPC_REG_32 link;
	PPC_REG_32 xer;
	PPC_REG_32 ccr;
	PPC_REG_32 mq;		/* 601 only (not used at present) */
				/* Used on APUS to hold IPL value. */
	PPC_REG_32 trap;		/* Reason for being here */
	PPC_REG_32 dar;		/* Fault registers */
	PPC_REG_32 dsisr;
	PPC_REG_32 result; 	/* Result of a system call */
};

#endif

#define STACK_FRAME_OVERHEAD	112	/* size of minimum stack frame */

/* Size of dummy stack frame allocated when calling signal handler. */
#define __SIGNAL_FRAMESIZE	128
#define __SIGNAL_FRAMESIZE32	64

#define instruction_pointer(regs) ((regs)->nip)
#define user_mode(regs) ((((regs)->msr) >> MSR_PR_LG) & 0x1)

/*
 * Offsets used by 'ptrace' system call interface.
 */
#define PT_R0	0
#define PT_R1	1
#define PT_R2	2
#define PT_R3	3
#define PT_R4	4
#define PT_R5	5
#define PT_R6	6
#define PT_R7	7
#define PT_R8	8
#define PT_R9	9
#define PT_R10	10
#define PT_R11	11
#define PT_R12	12
#define PT_R13	13
#define PT_R14	14
#define PT_R15	15
#define PT_R16	16
#define PT_R17	17
#define PT_R18	18
#define PT_R19	19
#define PT_R20	20
#define PT_R21	21
#define PT_R22	22
#define PT_R23	23
#define PT_R24	24
#define PT_R25	25
#define PT_R26	26
#define PT_R27	27
#define PT_R28	28
#define PT_R29	29
#define PT_R30	30
#define PT_R31	31

#define PT_NIP	32
#define PT_MSR	33
#ifdef __KERNEL__
#define PT_ORIG_R3 34
#endif
#define PT_CTR	35
#define PT_LNK	36
#define PT_XER	37
#define PT_CCR	38
#define PT_SOFTE 39
#define PT_RESULT 43

#define PT_FPR0	48
#ifdef __KERNEL__
#define PT_FPSCR (PT_FPR0 + 32 + 1)	  /* each FP reg occupies 1 slot in this space */
#define PT_FPSCR32 (PT_FPR0 + 2*32 + 1)	  /* To the 32-bit user - each FP reg occupies 2 slots in this space */
#else
#define PT_FPSCR (PT_FPR0 + 2*32 + 1)	/* each FP reg occupies 2 slots in this space -- Fix when 64-bit apps. */
#endif

/* Additional PTRACE requests implemented on PowerPC. */
#define PPC_PTRACE_GETREGS	      0x99  /* Get GPRs 0 - 31 */
#define PPC_PTRACE_SETREGS	      0x98  /* Set GPRs 0 - 31 */
#define PPC_PTRACE_GETFPREGS	    0x97  /* Get FPRs 0 - 31 */
#define PPC_PTRACE_SETFPREGS	    0x96  /* Set FPRs 0 - 31 */
#define PPC_PTRACE_PEEKTEXT_3264  0x95  /* Read word at location ADDR on a 64-bit process from a 32-bit process. */
#define PPC_PTRACE_PEEKDATA_3264  0x94  /* Read word at location ADDR on a 64-bit process from a 32-bit process. */
#define PPC_PTRACE_POKETEXT_3264  0x93  /* Write word at location ADDR on a 64-bit process from a 32-bit process. */
#define PPC_PTRACE_POKEDATA_3264  0x92  /* Write word at location ADDR on a 64-bit process from a 32-bit process. */
#define PPC_PTRACE_PEEKUSR_3264   0x91  /* Read a register (specified by ADDR) out of the "user area" on a 64-bit process from a 32-bit process. */
#define PPC_PTRACE_POKEUSR_3264   0x90  /* Write DATA into location ADDR within the "user area" on a 64-bit process from a 32-bit process. */

/* Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go */
#define PPC_PTRACE_GETVRREGS	18
#define PPC_PTRACE_SETVRREGS	19

#endif /* _PPC64_PTRACE_H */
