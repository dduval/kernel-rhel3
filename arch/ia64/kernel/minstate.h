#include <linux/config.h>

#include "entry.h"

/*
 * A couple of convenience macros that make writing and reading
 * SAVE_MIN and SAVE_REST easier.
 */
#define rARPR		r31
#define rCRIFS		r30
#define rCRIPSR		r29
#define rCRIIP		r28
#define rARRSC		r27
#define rARPFS		r26
#define rARUNAT		r25
#define rARRNAT		r24
#define rARBSPSTORE	r23
#define rKRBS		r22
#define rB6		r21
#define rR1		r20

/*
 * Here start the source dependent macros.
 */

/*
 * For ivt.s we want to access the stack virtually so we dont have to disable translation
 * on interrupts.
 */
#define MINSTATE_START_SAVE_MIN_VIRT								\
	dep r1=-1,r1,61,3;				/* r1 = current (virtual) */		\
(pUser)	mov ar.rsc=0;		/* set enforced lazy mode, pl 0, little-endian, loadrs=0 */	\
	;;											\
(pUser)	addl rKRBS=IA64_RBS_OFFSET,r1;			/* compute base of RBS */		\
(pUser)	mov rARRNAT=ar.rnat;									\
(pKern) mov r1=sp;					/* get sp  */				\
	;;											\
(pUser)	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r1;	/* compute base of memory stack */	\
(pUser)	mov rARBSPSTORE=ar.bspstore;			/* save ar.bspstore */			\
	;;											\
(pKern) addl r1=-IA64_PT_REGS_SIZE,r1;			/* if in kernel mode, use sp (r12) */	\
(pUser)	mov ar.bspstore=rKRBS;				/* switch to kernel RBS */		\
	;;											\
(pUser)	mov r18=ar.bsp;										\
(pUser)	mov ar.rsc=0x3;		/* set eager mode, pl 0, little-endian, loadrs=0 */		\

#define MINSTATE_END_SAVE_MIN_VIRT								\
	or r13=r13,r14;		/* make `current' a kernel virtual address */			\
	bsw.1;			/* switch back to bank 1 (must be last in insn group) */	\
	;;

/*
 * For mca_asm.S we want to access the stack physically since the state is saved before we
 * go virtual and dont want to destroy the iip or ipsr.
 */
#define MINSTATE_START_SAVE_MIN_PHYS								\
(pKern) adds r16=IA64_TASK_CPU_OFFSET,r1;;	/* current->cpu */				\
(pKern) ld4 r16=[r16];				/* smp_processor_id */				\
(pKern) movl r1=ia64_init_stack_addr;;								\
(pKern) shladd r1=r16,3,r1;;			/* ia64_init_stack_addr[cpu] */			\
(pKern) dep r16=0,r1,61,3;;			/* compute physical addr */			\
(pKern) ld8 r1=[r16];;				/* ia64_init_stack[cpu] */ 			\
(pKern) addl sp=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r1;;						\
(pUser)	mov ar.rsc=0;		/* set enforced lazy mode, pl 0, little-endian, loadrs=0 */	\
(pUser)	addl rKRBS=IA64_RBS_OFFSET,r1;		/* compute base of register backing store */	\
	;;											\
(pUser)	mov rARRNAT=ar.rnat;									\
(pKern) dep r1=0,sp,61,3;				/* compute physical addr of sp	*/	\
(pUser)	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r1;	/* compute base of memory stack */	\
(pUser)	mov rARBSPSTORE=ar.bspstore;			/* save ar.bspstore */			\
(pUser)	dep rKRBS=-1,rKRBS,61,3;			/* compute kernel virtual addr of RBS */\
	;;											\
(pKern) addl r1=-IA64_PT_REGS_SIZE,r1;		/* if in kernel mode, use sp (r12) */		\
(pUser)	mov ar.bspstore=rKRBS;			/* switch to kernel RBS */			\
	;;											\
(pUser)	mov r18=ar.bsp;										\
(pUser)	mov ar.rsc=0x3;		/* set eager mode, pl 0, little-endian, loadrs=0 */		\

#define MINSTATE_END_SAVE_MIN_PHYS								\
	or r12=r12,r14;		/* make sp a kernel virtual address */				\
	or r13=r13,r14;		/* make `current' a kernel virtual address */			\
	;;

#ifdef MINSTATE_VIRT
# define MINSTATE_START_SAVE_MIN	MINSTATE_START_SAVE_MIN_VIRT
# define MINSTATE_END_SAVE_MIN		MINSTATE_END_SAVE_MIN_VIRT
#endif

#ifdef MINSTATE_PHYS
# define MINSTATE_START_SAVE_MIN	MINSTATE_START_SAVE_MIN_PHYS
# define MINSTATE_END_SAVE_MIN		MINSTATE_END_SAVE_MIN_PHYS
#endif

/*
 * DO_SAVE_MIN switches to the kernel stacks (if necessary) and saves
 * the minimum state necessary that allows us to turn psr.ic back
 * on.
 *
 * Assumed state upon entry:
 *	psr.ic: off
 *	r31:	contains saved predicates (pr)
 *
 * Upon exit, the state is as follows:
 *	psr.ic: off
 *	r2 = points to &pt_regs.r16
 *	r12 = kernel sp (kernel virtual address)
 *	r13 = points to current task_struct (kernel virtual address)
 *	p15 = TRUE if psr.i is set in cr.ipsr
 *	predicate registers (other than p2, p3, and p15), b6, r3, r8, r9, r10, r11, r14, r15:
 *		preserved
 *
 * Note that psr.ic is NOT turned on by this macro.  This is so that
 * we can pass interruption state as arguments to a handler.
 */
#define DO_SAVE_MIN(COVER,SAVE_IFS,EXTRA)							  \
	mov rARRSC=ar.rsc;									  \
	mov rARPFS=ar.pfs;									  \
	mov rR1=r1;										  \
	mov rARUNAT=ar.unat;									  \
	mov rCRIPSR=cr.ipsr;									  \
	mov rB6=b6;				/* rB6 = branch reg 6 */			  \
	mov rCRIIP=cr.iip;									  \
	mov r1=IA64_KR(CURRENT);		/* r1 = current (physical) */			  \
	COVER;											  \
	;;											  \
	invala;											  \
	extr.u r16=rCRIPSR,32,2;		/* extract psr.cpl */				  \
	;;											  \
	cmp.eq pKern,pUser=r0,r16;		/* are we in kernel mode already? (psr.cpl==0) */ \
	/* switch from user to kernel RBS: */							  \
	;;											  \
	SAVE_IFS;										  \
	MINSTATE_START_SAVE_MIN									  \
	;;											  \
	mov r16=r1;					/* initialize first base pointer */	  \
	adds r17=8,r1;					/* initialize second base pointer */	  \
	;;											  \
	st8 [r16]=rCRIPSR,16;	/* save cr.ipsr */						  \
	st8 [r17]=rCRIIP,16;	/* save cr.iip */						  \
(pKern)	mov r18=r0;		/* make sure r18 isn't NaT */					  \
	;;											  \
	st8 [r16]=rCRIFS,16;	/* save cr.ifs */						  \
	st8 [r17]=rARUNAT,16;	/* save ar.unat */						  \
(pUser)	sub r18=r18,rKRBS;	/* r18=RSE.ndirty*8 */						  \
	;;											  \
	st8 [r16]=rARPFS,16;	/* save ar.pfs */						  \
	st8 [r17]=rARRSC,16;	/* save ar.rsc */						  \
	tbit.nz p15,p0=rCRIPSR,IA64_PSR_I_BIT							  \
	;;			/* avoid RAW on r16 & r17 */					  \
(pKern)	adds r16=16,r16;	/* skip over ar_rnat field */					  \
(pKern)	adds r17=16,r17;	/* skip over ar_bspstore field */				  \
(pUser)	st8 [r16]=rARRNAT,16;	/* save ar.rnat */						  \
(pUser)	st8 [r17]=rARBSPSTORE,16;	/* save ar.bspstore */					  \
	;;											  \
	st8 [r16]=rARPR,16;	/* save predicates */						  \
	st8 [r17]=rB6,16;	/* save b6 */							  \
	shl r18=r18,16;		/* compute ar.rsc to be used for "loadrs" */			  \
	;;											  \
	st8 [r16]=r18,16;	/* save ar.rsc value for "loadrs" */				  \
	st8.spill [r17]=rR1,16;	/* save original r1 */						  \
	;;											  \
.mem.offset 0,0;	st8.spill [r16]=r2,16;							  \
.mem.offset 8,0;	st8.spill [r17]=r3,16;							  \
	adds r2=IA64_PT_REGS_R16_OFFSET,r1;							  \
	;;											  \
.mem.offset 0,0;		st8.spill [r16]=r12,16;						  \
.mem.offset 8,0;		st8.spill [r17]=r13,16;						  \
	cmp.eq pNonSys,pSys=r0,r0	/* initialize pSys=0, pNonSys=1 */			  \
	;;											  \
.mem.offset 0,0;		st8.spill [r16]=r14,16;						  \
.mem.offset 8,0;		st8.spill [r17]=r15,16;						  \
	dep r14=-1,r0,61,3;									  \
	;;											  \
.mem.offset 0,0;		st8.spill [r16]=r8,16;						  \
.mem.offset 8,0;		st8.spill [r17]=r9,16;						  \
	adds r12=-16,r1;	/* switch to kernel memory stack (with 16 bytes of scratch) */	  \
	;;											  \
.mem.offset 0,0;		st8.spill [r16]=r10,16;						  \
.mem.offset 8,0;		st8.spill [r17]=r11,16;						  \
	mov r13=IA64_KR(CURRENT);	/* establish `current' */				  \
	;;											  \
	EXTRA;											  \
	movl r1=__gp;		/* establish kernel global pointer */				  \
	;;											  \
	MINSTATE_END_SAVE_MIN

/*
 * SAVE_REST saves the remainder of pt_regs (with psr.ic on).  This
 * macro guarantees to preserve all predicate registers, r8, r9, r10,
 * r11, r14, and r15.
 *
 * Assumed state upon entry:
 *	psr.ic: on
 *	r2:	points to &pt_regs.r16
 *	r3:	points to &pt_regs.r17
 */
#define SAVE_REST				\
.mem.offset 0,0;	st8.spill [r2]=r16,16;	\
.mem.offset 8,0;	st8.spill [r3]=r17,16;	\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r18,16;	\
.mem.offset 8,0;	st8.spill [r3]=r19,16;	\
	;;					\
	mov r16=ar.ccv;		/* M-unit */	\
	movl r18=FPSR_DEFAULT	/* L-unit */	\
	;;					\
	mov r17=ar.fpsr;	/* M-unit */	\
	mov ar.fpsr=r18;	/* M-unit */	\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r20,16;	\
.mem.offset 8,0;	st8.spill [r3]=r21,16;	\
	mov r18=b0;				\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r22,16;	\
.mem.offset 8,0;	st8.spill [r3]=r23,16;	\
	mov r19=b7;				\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r24,16;	\
.mem.offset 8,0;	st8.spill [r3]=r25,16;	\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r26,16;	\
.mem.offset 8,0;	st8.spill [r3]=r27,16;	\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r28,16;	\
.mem.offset 8,0;	st8.spill [r3]=r29,16;	\
	;;					\
.mem.offset 0,0;	st8.spill [r2]=r30,16;	\
.mem.offset 8,0;	st8.spill [r3]=r31,16;	\
	;;					\
	st8 [r2]=r16,16;	/* ar.ccv */	\
	st8 [r3]=r17,16;	/* ar.fpsr */	\
	;;					\
	st8 [r2]=r18,16;	/* b0 */	\
	st8 [r3]=r19,16+8;	/* b7 */	\
	;;					\
	stf.spill [r2]=f6,32;			\
	stf.spill [r3]=f7,32;			\
	;;					\
	stf.spill [r2]=f8,32;			\
	stf.spill [r3]=f9,32

#define SAVE_MIN_WITH_COVER	DO_SAVE_MIN(cover, mov rCRIFS=cr.ifs,)
#define SAVE_MIN_WITH_COVER_R19	DO_SAVE_MIN(cover, mov rCRIFS=cr.ifs, mov r15=r19)
#define SAVE_MIN		DO_SAVE_MIN(     , mov rCRIFS=r0, )
