/*
 *  linux/arch/i386/kernel/head.S -- the 32-bit startup code.
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Enhanced CPU detection and feature setting code by Mike Jagdis
 *  and Martin Mares, November 1997.
 */

.text
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

#define OLD_CL_MAGIC_ADDR	0x90020
#define OLD_CL_MAGIC		0xA33F
#define OLD_CL_BASE_ADDR	0x90000
#define OLD_CL_OFFSET		0x90022
#define NEW_CL_POINTER		0x228	/* Relative to real mode data */

/*
 * References to members of the boot_cpu_data structure.
 */

#define CPU_PARAMS	SYMBOL_NAME(boot_cpu_data)
#define X86		CPU_PARAMS+0
#define X86_VENDOR	CPU_PARAMS+1
#define X86_MODEL	CPU_PARAMS+2
#define X86_MASK	CPU_PARAMS+3
#define X86_HARD_MATH	CPU_PARAMS+6
#define X86_CPUID	CPU_PARAMS+8
#define X86_CAPABILITY	CPU_PARAMS+12
#define X86_VENDOR_ID	CPU_PARAMS+28

/*
 * swapper_pg_dir is the main page directory, address 0x00101000
 *
 * On entry, %esi points to the real-mode code as a 32-bit pointer.
 */
startup_32:
/*
 * Set segments to known values
 */
	cld
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
#ifdef CONFIG_SMP
	orw %bx,%bx
	jz 1f

/*
 *	New page tables may be in 4Mbyte page mode and may
 *	be using the global pages. 
 *
 *	NOTE! If we are on a 486 we may have no cr4 at all!
 *	So we do not try to touch it unless we really have
 *	some bits in it to set.  This won't work if the BSP
 *	implements cr4 but this AP does not -- very unlikely
 *	but be warned!  The same applies to the pse feature
 *	if not equally supported. --macro
 *
 *	NOTE! We have to correct for the fact that we're
 *	not yet offset PAGE_OFFSET..
 */
#define cr4_bits mmu_cr4_features-__PAGE_OFFSET
	cmpl $0,cr4_bits
	je 3f
	movl %cr4,%eax		# Turn on paging options (PSE,PAE,..)
	orl cr4_bits,%eax
	movl %eax,%cr4

	btl     $5, %eax        # check if PAE is enabled
	jnc     6f

	/* Check if extended functions are implemented */               
	movl    $0x80000000, %eax
	cpuid
	cmpl    $0x80000000, %eax
	jbe     6f
	mov     $0x80000001, %eax
	cpuid
	/* Execute Disable bit supported? */     
	btl     $20, %edx
	jnc     6f

	/* Setup EFER (Extended Feature Enable Register) */
	movl    $0xc0000080, %ecx
	rdmsr
 
	btsl    $11, %eax
	/* Make changes effective */
	wrmsr

6:
	/* cpuid clobbered ebx, set it up again: */
	xorl %ebx,%ebx
	incl %ebx

	jmp 3f
1:
#endif
/*
 * Initialize page tables
 */
	movl $pg0-__PAGE_OFFSET,%edi /* initialize page tables */
	movl $007,%eax		/* "007" doesn't mean with right to kill, but
				   PRESENT+RW+USER */
2:	stosl
	add $0x1000,%eax
	cmp $empty_zero_page-__PAGE_OFFSET,%edi
	jne 2b

/*
 * Enable paging
 */
3:
	movl $swapper_pg_dir-__PAGE_OFFSET,%eax
	movl %eax,%cr3		/* set the page table pointer.. */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* ..and set paging (PG) bit */
	jmp 1f			/* flush the prefetch-queue */
1:
	movl $1f,%eax
	jmp *%eax		/* make sure eip is relocated */
1:
	/* Set up the stack pointer */
	lss stack_start,%esp

#ifdef CONFIG_SMP
	orw  %bx,%bx
	jz  1f				/* Initial CPU cleans BSS */
	pushl $0
	popfl
	jmp checkCPUtype
1:
#endif CONFIG_SMP

/*
 * Clear BSS first so that there are no surprises...
 * No need to cld as DF is already clear from cld above...
 */
	xorl %eax,%eax
	movl $ SYMBOL_NAME(__bss_start),%edi
	movl $ SYMBOL_NAME(_end),%ecx
	subl %edi,%ecx
	rep
	stosb

/*
 * start system 32-bit setup. We need to re-do some of the things done
 * in 16-bit mode for the "real" operations.
 */
	call setup_idt
/*
 * Initialize eflags.  Some BIOS's leave bits like NT set.  This would
 * confuse the debugger if this code is traced.
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl
/*
 * Copy bootup parameters out of the way. First 2kB of
 * _empty_zero_page is for boot parameters, second 2kB
 * is for the command line.
 *
 * Note: %esi still has the pointer to the real-mode data.
 */
	movl $ SYMBOL_NAME(empty_zero_page),%edi
	movl $512,%ecx
	cld
	rep
	movsl
	xorl %eax,%eax
	movl $512,%ecx
	rep
	stosl
	movl SYMBOL_NAME(empty_zero_page)+NEW_CL_POINTER,%esi
	andl %esi,%esi
	jnz 2f			# New command line protocol
	cmpw $(OLD_CL_MAGIC),OLD_CL_MAGIC_ADDR
	jne 1f
	movzwl OLD_CL_OFFSET,%esi
	addl $(OLD_CL_BASE_ADDR),%esi
2:
	movl $ SYMBOL_NAME(empty_zero_page)+2048,%edi
	movl $512,%ecx
	rep
	movsl
1:
checkCPUtype:

	movl $-1,X86_CPUID		#  -1 for no CPUID initially

/* check if it is 486 or 386. */
/*
 * XXX - this does a lot of unnecessary setup.  Alignment checks don't
 * apply at our cpl of 0 and the stack ought to be aligned already, and
 * we don't need to preserve eflags.
 */

	movb $3,X86		# at least 386
	pushfl			# push EFLAGS
	popl %eax		# get EFLAGS
	movl %eax,%ecx		# save original EFLAGS
	xorl $0x40000,%eax	# flip AC bit in EFLAGS
	pushl %eax		# copy to EFLAGS
	popfl			# set EFLAGS
	pushfl			# get new EFLAGS
	popl %eax		# put it in eax
	xorl %ecx,%eax		# change in flags
	andl $0x40000,%eax	# check if AC bit changed
	je is386

	movb $4,X86		# at least 486
	movl %ecx,%eax
	xorl $0x200000,%eax	# check ID flag
	pushl %eax
	popfl			# if we are on a straight 486DX, SX, or
	pushfl			# 487SX we can't change it
	popl %eax
	xorl %ecx,%eax
	pushl %ecx		# restore original EFLAGS
	popfl
	andl $0x200000,%eax
	je is486

	/* get vendor info */
	xorl %eax,%eax			# call CPUID with 0 -> return vendor ID
	cpuid
	movl %eax,X86_CPUID		# save CPUID level
	movl %ebx,X86_VENDOR_ID		# lo 4 chars
	movl %edx,X86_VENDOR_ID+4	# next 4 chars
	movl %ecx,X86_VENDOR_ID+8	# last 4 chars

	orl %eax,%eax			# do we have processor info as well?
	je is486

	movl $1,%eax		# Use the CPUID instruction to get CPU type
	cpuid
	movb %al,%cl		# save reg for future use
	andb $0x0f,%ah		# mask processor family
	movb %ah,X86
	andb $0xf0,%al		# mask model
	shrb $4,%al
	movb %al,X86_MODEL
	andb $0x0f,%cl		# mask mask revision
	movb %cl,X86_MASK
	movl %edx,X86_CAPABILITY

is486:
	movl %cr0,%eax		# 486 or better
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl $0x50022,%eax	# set AM, WP, NE and MP
	jmp 2f

is386:	pushl %ecx		# restore original EFLAGS
	popfl
	movl %cr0,%eax		# 386
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl $2,%eax		# set MP
2:	movl %eax,%cr0
	call check_x87
	incb ready
	lgdt cpu_gdt_descr
	lidt idt_descr
	ljmp $(__KERNEL_CS),$1f
1:	movl $(__KERNEL_DS),%eax	# reload all the segment registers
	movl %eax,%ds		# after changing gdt.
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
	movl %eax,%ss
	xorl %eax,%eax
	lldt %ax
	cld			# gcc2 wants the direction flag cleared at all times
#ifdef CONFIG_SMP
	movb ready, %cl	
	cmpb $1,%cl
	je 1f			# the first CPU calls start_kernel
				# all other CPUs call initialize_secondary
	call SYMBOL_NAME(initialize_secondary)
	jmp L6
1:
#endif
	call SYMBOL_NAME(start_kernel)
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

ready:	.byte 0

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	movb $0,X86_HARD_MATH
	clts
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f
	movl %cr0,%eax		/* no coprocessor: have to set bits */
	xorl $4,%eax		/* set EM */
	movl %eax,%cr0
	ret
	ALIGN
1:	movb $1,X86_HARD_MATH
	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It doesn't actually load
 *  idt - that can be done only after paging has been enabled
 *  and the kernel moved to PAGE_OFFSET. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok.
 */
setup_idt:
	lea ignore_int,%edx
	movl $(__KERNEL_CS << 16),%eax
	movw %dx,%ax		/* selector = 0x0010 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea SYMBOL_NAME(idt_table),%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	ret

ENTRY(stack_start)
	.long SYMBOL_NAME(init_task_union)+8192
	.long __KERNEL_DS

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n"
	ALIGN
ignore_int:
	cld
	pushl %eax
	pushl %ecx
	pushl %edx
	pushl %es
	pushl %ds
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	pushl $int_msg
	call SYMBOL_NAME(printk)
	popl %eax
	popl %ds
	popl %es
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * The IDT and GDT 'descriptors' are a strange 48-bit object
 * only used by the lidt and lgdt instructions. They are not
 * like usual segment descriptors - they consist of a 16-bit
 * segment size, and 32-bit linear address value:
 */

.globl SYMBOL_NAME(idt_descr)
.globl SYMBOL_NAME(cpu_gdt_descr)

	ALIGN
	.word 0				# 32-bit align idt_desc.address

SYMBOL_NAME(idt_descr):
	.word IDT_ENTRIES*8-1		# idt contains 256 entries
	.long SYMBOL_NAME(idt_table)

SYMBOL_NAME(cpu_gdt_descr):
	.word GDT_ENTRIES*8-1
	.long SYMBOL_NAME(cpu_gdt_table)

	.fill NR_CPUS-1,6,0		# space for the other GDT descriptors


/*
 * This is initialized to create an identity-mapping at 0-16M (for bootup
 * purposes) and another mapping of the 0-16M area at virtual address
 * PAGE_OFFSET.
 */
.org 0x1000
ENTRY(swapper_pg_dir)
	.long 0x00102007
	.long 0x00103007
	.long 0x00104007
	.long 0x00105007
	.fill BOOT_USER_PGD_PTRS-4,4,0
	/* default: 766 entries */
	.long 0x00102007
	.long 0x00103007
	.long 0x00104007
	.long 0x00105007
	/* default: 254 entries */
	.fill BOOT_KERNEL_PGD_PTRS-4,4,0

/*
 * The page tables are initialized to only 16MB here - the final page
 * tables are set up later depending on memory size.
 */
.org 0x2000
ENTRY(pg0)

.org 0x3000
ENTRY(pg1)

.org 0x4000
ENTRY(pg2)

.org 0x5000
ENTRY(pg3)

/*
 * empty_zero_page must immediately follow the page tables ! (The
 * initialization loop counts until empty_zero_page)
 */

.org 0x6000
ENTRY(empty_zero_page)

.org 0x7000

/*
 * Real beginning of normal "text" segment
 */
ENTRY(stext)
ENTRY(_stext)

.data

.align 4096

/*
 * The Global Descriptor Table contains 28 quadwords, per-CPU.
 */
ENTRY(cpu_gdt_table)
	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x0000000000000000	/* 0x0b reserved */
	.quad 0x0000000000000000	/* 0x13 reserved */
	.quad 0x0000000000000000	/* 0x1b reserved */
	.quad 0x00cffa000000ffff	/* 0x23 user 4GB code at 0x00000000 */
	.quad 0x00cff2000000ffff	/* 0x2b user 4GB data at 0x00000000 */
	.quad 0x0000000000000000	/* 0x33 TLS entry 1 */
	.quad 0x0000000000000000	/* 0x3b TLS entry 2 */
	.quad 0x0000000000000000	/* 0x43 TLS entry 3 */
	.quad 0x0000000000000000	/* 0x4b reserved */
	.quad 0x0000000000000000	/* 0x53 reserved */
	.quad 0x0000000000000000	/* 0x5b reserved */

	.quad 0x00cf9a000000ffff	/* 0x60 kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* 0x68 kernel 4GB data at 0x00000000 */
	.quad 0x0000000000000000	/* 0x70 TSS descriptor */
	.quad 0x0000000000000000	/* 0x78 LDT descriptor */

	/* Segments used for calling PnP BIOS */
	.quad 0x00c09a0000000000	/* 0x80 32-bit code */
	.quad 0x00809a0000000000	/* 0x88 16-bit code */
	.quad 0x0080920000000000	/* 0x90 16-bit data */
	.quad 0x0080920000000000	/* 0x98 16-bit data */
	.quad 0x0080920000000000	/* 0xa0 16-bit data */
	/*
	 * The APM segments have byte granularity and their bases
	 * and limits are set at run time.
	 */
	.quad 0x00409a0000000000	/* 0xa8 APM CS    code */
	.quad 0x00009a0000000000	/* 0xb0 APM CS 16 code (16 bit) */
	.quad 0x0040920000000000	/* 0xb8 APM DS    data */

	.quad 0x0000000000000000	/* 0xd0 - unused */
	.quad 0x0000000000000000	/* 0xd8 - unused */
	.quad 0x0000000000000000	/* 0xe0 - unused */
	.quad 0x0000000000000000	/* 0xe8 - unused */
	.quad 0x0000000000000000	/* 0xf0 - unused */
	.quad 0x0000000000000000	/* 0xf8 - GDT entry 31: double-fault TSS */

#if CONFIG_SMP
	.fill (NR_CPUS-1)*GDT_ENTRIES,8,0 /* other CPU's GDT */
#endif

