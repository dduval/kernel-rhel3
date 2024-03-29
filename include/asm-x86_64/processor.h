/*
 * include/asm-x86_64/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_X86_64_PROCESSOR_H
#define __ASM_X86_64_PROCESSOR_H

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/sigcontext.h>
#include <asm/cpufeature.h>
#include <linux/config.h>
#include <linux/threads.h>
#include <asm/msr.h>
#include <asm/current.h>
#include <asm/system.h>

#define TF_MASK		0x00000100
#define IF_MASK		0x00000200
#define IOPL_MASK	0x00003000
#define NT_MASK		0x00004000
#define VM_MASK		0x00020000
#define AC_MASK		0x00040000
#define VIF_MASK	0x00080000	/* virtual interrupt flag */
#define VIP_MASK	0x00100000	/* virtual interrupt pending */
#define ID_MASK		0x00200000

typedef unsigned long desc_struct;

#define desc_empty(desc) \
		(!((desc)->a | (desc)->b))

#define desc_equal(desc1, desc2) \
		(((desc1)->a == (desc2)->a) && ((desc1)->b == (desc2)->b))


/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; asm volatile("leaq 1f(%%rip),%0\n1:":"=r"(pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 */

struct cpuinfo_x86 {
	__u8	x86;		/* CPU family */
	__u8	x86_vendor;	/* CPU vendor */
	__u8	x86_model;
	__u8	x86_mask;
	int	cpuid_level;	/* Maximum supported CPUID level, -1=no CPUID */
	__u32	x86_capability[NCAPINTS];
	char	x86_vendor_id[16];
	char	x86_model_id[64];
	int 	x86_cache_size;  /* in KB - valid for CPUS which support this
				    call  */
	int	x86_clflush_size;
	int	x86_tlbsize;	/* number of 4K pages in DTLB/ITLB combined(in pages)*/
        __u8    x86_virt_bits, x86_phys_bits;
        __u32   x86_power; 
	unsigned long loops_per_jiffy;
} ____cacheline_aligned;

#define X86_VENDOR_INTEL 0
#define X86_VENDOR_CYRIX 1
#define X86_VENDOR_AMD 2
#define X86_VENDOR_UMC 3
#define X86_VENDOR_NEXGEN 4
#define X86_VENDOR_CENTAUR 5
#define X86_VENDOR_RISE 6
#define X86_VENDOR_TRANSMETA 7
#define X86_VENDOR_UNKNOWN 0xff

extern struct cpuinfo_x86 boot_cpu_data;
extern struct tss_struct init_tss[NR_CPUS];

#ifdef CONFIG_SMP
extern struct cpuinfo_x86 cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

#define cpu_has_pge	(test_bit(X86_FEATURE_PGE,  boot_cpu_data.x86_capability))
#define cpu_has_pse	(test_bit(X86_FEATURE_PSE,  boot_cpu_data.x86_capability))
#define cpu_has_pae	(test_bit(X86_FEATURE_PAE,  boot_cpu_data.x86_capability))
#define cpu_has_tsc	(test_bit(X86_FEATURE_TSC,  boot_cpu_data.x86_capability))
#define cpu_has_de	(test_bit(X86_FEATURE_DE,   boot_cpu_data.x86_capability))
#define cpu_has_vme	(test_bit(X86_FEATURE_VME,  boot_cpu_data.x86_capability))
#define cpu_has_fxsr	(test_bit(X86_FEATURE_FXSR, boot_cpu_data.x86_capability))
#define cpu_has_xmm	(test_bit(X86_FEATURE_XMM,  boot_cpu_data.x86_capability))
#define cpu_has_fpu	(test_bit(X86_FEATURE_FPU,  boot_cpu_data.x86_capability))
#define cpu_has_apic	(test_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability))
#define cpu_has_nx	(test_bit(X86_FEATURE_NX, boot_cpu_data.x86_capability))

extern char ignore_irq13;

extern void identify_cpu(struct cpuinfo_x86 *);
extern void print_cpu_info(struct cpuinfo_x86 *);
extern void dodgy_tsc(void);

/*
 * EFLAGS bits
 */
#define X86_EFLAGS_CF	0x00000001 /* Carry Flag */
#define X86_EFLAGS_PF	0x00000004 /* Parity Flag */
#define X86_EFLAGS_AF	0x00000010 /* Auxillary carry Flag */
#define X86_EFLAGS_ZF	0x00000040 /* Zero Flag */
#define X86_EFLAGS_SF	0x00000080 /* Sign Flag */
#define X86_EFLAGS_TF	0x00000100 /* Trap Flag */
#define X86_EFLAGS_IF	0x00000200 /* Interrupt Flag */
#define X86_EFLAGS_DF	0x00000400 /* Direction Flag */
#define X86_EFLAGS_OF	0x00000800 /* Overflow Flag */
#define X86_EFLAGS_IOPL	0x00003000 /* IOPL mask */
#define X86_EFLAGS_NT	0x00004000 /* Nested Task */
#define X86_EFLAGS_RF	0x00010000 /* Resume Flag */
#define X86_EFLAGS_VM	0x00020000 /* Virtual Mode */
#define X86_EFLAGS_AC	0x00040000 /* Alignment Check */
#define X86_EFLAGS_VIF	0x00080000 /* Virtual Interrupt Flag */
#define X86_EFLAGS_VIP	0x00100000 /* Virtual Interrupt Pending */
#define X86_EFLAGS_ID	0x00200000 /* CPUID detection flag */

/*
 *	Generic CPUID function
 * 	FIXME: This really belongs to msr.h
 */
extern inline void cpuid(int op, int *eax, int *ebx, int *ecx, int *edx)
{
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (op));
}

/*
 * CPUID functions returning a single datum
 */
extern inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax;

	__asm__("cpuid"
		: "=a" (eax)
		: "0" (op)
		: "bx", "cx", "dx");
	return eax;
}
extern inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int eax, ebx;

	__asm__("cpuid"
		: "=a" (eax), "=b" (ebx)
		: "0" (op)
		: "cx", "dx" );
	return ebx;
}
extern inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int eax, ecx;

	__asm__("cpuid"
		: "=a" (eax), "=c" (ecx)
		: "0" (op)
		: "bx", "dx" );
	return ecx;
}
extern inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int eax, edx;

	__asm__("cpuid"
		: "=a" (eax), "=d" (edx)
		: "0" (op)
		: "bx", "cx");
	return edx;
}

/*
 * Intel CPU features in CR4
 */
#define X86_CR4_VME		0x0001	/* enable vm86 extensions */
#define X86_CR4_PVI		0x0002	/* virtual interrupts flag enable */
#define X86_CR4_TSD		0x0004	/* disable time stamp at ipl 3 */
#define X86_CR4_DE		0x0008	/* enable debugging extensions */
#define X86_CR4_PSE		0x0010	/* enable page size extensions */
#define X86_CR4_PAE		0x0020	/* enable physical address extensions */
#define X86_CR4_MCE		0x0040	/* Machine check enable */
#define X86_CR4_PGE		0x0080	/* enable global pages */
#define X86_CR4_PCE		0x0100	/* enable performance counters at ipl 3 */
#define X86_CR4_OSFXSR		0x0200	/* enable fast FPU save and restore */
#define X86_CR4_OSXMMEXCPT	0x0400	/* enable unmasked SSE exceptions */

/*
 * Save the cr4 feature set we're using (ie
 * Pentium 4MB enable and PPro Global page
 * enable), so that any CPU's that boot up
 * after us can get the correct flags.
 */
extern unsigned long mmu_cr4_features;

static inline void set_in_cr4 (unsigned long mask)
{
	mmu_cr4_features |= mask;
	__asm__("movq %%cr4,%%rax\n\t"
		"orq %0,%%rax\n\t"
		"movq %%rax,%%cr4\n"
		: : "irg" (mask)
		:"ax");
}

static inline void clear_in_cr4 (unsigned long mask)
{
	mmu_cr4_features &= ~mask;
	__asm__("movq %%cr4,%%rax\n\t"
		"andq %0,%%rax\n\t"
		"movq %%rax,%%cr4\n"
		: : "irg" (~mask)
		:"ax");
}

/*
 *      Cyrix CPU configuration register indexes
 */
#define CX86_CCR0 0xc0
#define CX86_CCR1 0xc1
#define CX86_CCR2 0xc2
#define CX86_CCR3 0xc3
#define CX86_CCR4 0xe8
#define CX86_CCR5 0xe9
#define CX86_CCR6 0xea
#define CX86_CCR7 0xeb
#define CX86_DIR0 0xfe
#define CX86_DIR1 0xff
#define CX86_ARR_BASE 0xc4
#define CX86_RCR_BASE 0xdc

/*
 *      Cyrix CPU indexed register access macros
 */

#define getCx86(reg) ({ outb((reg), 0x22); inb(0x23); })

#define setCx86(reg, data) do { \
	outb((reg), 0x22); \
	outb((data), 0x23); \
} while (0)

/*
 * Bus types
 */
#define EISA_bus 0
#define MCA_bus 0
#define MCA_bus__is_a_macro


/*
 * User space process size: 512GB - 1GB (default).
 */
#define TASK_SIZE_64	(0x0000007fc0000000)
#define TASK_SIZE	TASK_SIZE_64

/*
 *  32 bit user space process size - determined by personality
 */
#define IA32_PAGE_OFFSET ((current->personality & ADDR_LIMIT_3GB) ? 0xc0000000 : 0xFFFFe000)
#define TASK_SIZE_32	IA32_PAGE_OFFSET

#define HUGE_TASK_SIZE	\
	((current->thread.flags & THREAD_IA32) ? TASK_SIZE_32 : TASK_SIZE_64)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_32 0x40000000
#define TASK_UNMAPPED_64 (TASK_SIZE/3) 
#define TASK_UNMAPPED_BASE	\
	((current->thread.flags & THREAD_IA32) ? TASK_UNMAPPED_32 : TASK_UNMAPPED_64)  

#define __HAVE_ARCH_ALIGN_STACK
extern unsigned long arch_align_stack(unsigned long);

/*
 * Size of io_bitmap in longwords: 32 is ports 0-0x3ff.
 */
#define IO_BITMAP_SIZE	32
#define IO_BITMAP_OFFSET offsetof(struct tss_struct,io_bitmap)
#define INVALID_IO_BITMAP_OFFSET 0x8000

struct i387_fxsave_struct {
	u16	cwd;
	u16	swd;
	u16	twd;
	u16	fop;
	u64	rip;
	u64	rdp; 
	u32	mxcsr;
	u32	mxcsr_mask;
	u32	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	u32	xmm_space[64];	/* 16*16 bytes for each XMM-reg = 128 bytes */
	u32	padding[24];
} __attribute__ ((aligned (16)));

union i387_union {
	struct i387_fxsave_struct	fxsave;
};

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct tss_struct {
	u32 reserved1;
	u64 rsp0;	
	u64 rsp1;
	u64 rsp2;
	u64 reserved2;
	u64 ist[7];
	u32 reserved3;
	u32 reserved4;
	u16 reserved5;
	u16 io_map_base;
	u32 io_bitmap[IO_BITMAP_SIZE+1];
	/*
	 * pads the TSS to be cacheline-aligned (size is 0x100)
	 */
	u32 __cacheline_filler[5];
} __attribute__((packed)) ____cacheline_aligned;

struct thread_struct {
/* cached TLS descriptors. */
	desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long	rsp0;
	unsigned long	rip;
	unsigned long	rsp;
	unsigned long 	userrsp;	/* Copy from PDA */ 
	unsigned long	fs;
	unsigned long	gs;
	unsigned short	es, ds, fsindex, gsindex;	
	enum { 
		THREAD_IA32 = 0x0001,
	} flags;
/* Hardware debugging registers */
	unsigned long	debugreg[8];  /* %%db0-7 debug registers */
/* fault info */
	unsigned long	cr2, trap_no, error_code;
/* floating point info */
	union i387_union	i387;
	u32	*io_bitmap_ptr;
};

#define INIT_THREAD  {						\
}

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define STACKFAULT_STACK 1
#define DOUBLEFAULT_STACK 2 
#define NMI_STACK 3 
#define N_EXCEPTION_STACKS 3  /* hw limit: 7 */
#define EXCEPTION_STKSZ PAGE_SIZE
#define EXCEPTION_STK_ORDER 0

extern void load_gs_index(unsigned);

#define start_thread(regs,new_rip,new_rsp) do { \
	asm volatile("movl %0,%%fs; movl %0,%%es; movl %0,%%ds": :"r" (0));	 \
	load_gs_index(0);							\
	(regs)->rip = (new_rip);						 \
	(regs)->rsp = (new_rsp);						 \
	write_pda(oldrsp, (new_rsp));						 \
	(regs)->cs = __USER_CS;							 \
	(regs)->ss = __USER_DS;							 \
	(regs)->eflags = 0x200;							 \
	set_fs(USER_DS);							 \
} while(0) 

struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
/*
 * create a kernel thread without removing it from tasklists
 */
extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
static inline void copy_segments(struct task_struct *p, struct mm_struct * mm) { }
static inline void release_segments(struct mm_struct * mm) { }

/*
 * Return saved PC of a blocked thread.
 * What is this good for? it will be always the scheduler or ret_from_fork.
 */

extern inline unsigned long thread_saved_pc(struct thread_struct *t)
{ 
	return *(unsigned long *)(t->rsp - 8);
} 

extern unsigned long get_wchan(struct task_struct *p);
#define KSTK_EIP(tsk) \
	(((struct pt_regs *)(tsk->thread.rsp0 - sizeof(struct pt_regs)))->rip)
#define KSTK_ESP(tsk) -1 /* sorry. doesn't work for syscall. */

/* Note: most of the infrastructure to separate stack and task_struct
   are already there. When you run out of stack try this first. */

#define __alloc_task_struct() ((struct task_struct *) __get_free_pages(GFP_KERNEL,1))
#define  __free_task_struct(p) free_pages((unsigned long) (p), 1)
  
#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

struct microcode_header {
	unsigned int hdrver;
	unsigned int rev;
	unsigned int date;
	unsigned int sig;
	unsigned int cksum;
	unsigned int ldrver;
	unsigned int pf;
	unsigned int datasize;
	unsigned int totalsize;
	unsigned int reserved[3];
};

struct microcode {
	struct microcode_header hdr;
	unsigned int bits[0];
};

typedef struct microcode microcode_t;
typedef struct microcode_header microcode_header_t;

/* microcode format is extended from prescott processors */
struct extended_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int cksum;
};

struct extended_sigtable {
	unsigned int count;
	unsigned int cksum;
	unsigned int reserved[3];
	struct extended_signature sigs[0];
};

/* '6' because it used to be for P6 only (but now covers Pentium 4 as well) */
#define MICROCODE_IOCFREE	_IO('6',0)

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
extern inline void rep_nop(void)
{
	__asm__ __volatile__("rep;nop" ::: "memory");
}

static __inline__ void __monitor(const void *eax, unsigned long ecx,
               unsigned long edx)
{
       /* "monitor %eax,%ecx,%edx;" */
       asm volatile(
               ".byte 0x0f,0x01,0xc8;"
               : :"a" (eax), "c" (ecx), "d"(edx));
}

static __inline__ void __mwait(unsigned long eax, unsigned long ecx)
{
       /* "mwait %eax,%ecx;" */
       asm volatile(
               ".byte 0x0f,0x01,0xc9;"
               : :"a" (eax), "c" (ecx));
}

/* Avoid speculative execution by the CPU */
extern inline void sync_core(void)
{ 
	int tmp;
	asm volatile("cpuid" : "=a" (tmp) : "0" (1) : "ebx","ecx","edx","memory");
} 

#define USE_PREFETCH 0
#if USE_PREFETCH

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

#define prefetch(x) __builtin_prefetch((x),0,1)
#define prefetchw(x) __builtin_prefetch((x),1,1)
#define spin_lock_prefetch(x)  prefetchw(x)

#endif /* USE_PREFETCH */

#define cpu_relax()   rep_nop()


#endif /* __ASM_X86_64_PROCESSOR_H */
