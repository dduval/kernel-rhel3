/*
 *  linux/arch/i386/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/elfcore.h>
#include <linux/version.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/irq.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>
#include <asm/atomic_kmap.h>
#ifdef CONFIG_MATH_EMULATION
#include <asm/math_emu.h>
#endif

#include <linux/irq.h>

asmlinkage void ret_from_fork(void) __asm__("ret_from_fork");

int hlt_counter;

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

void disable_hlt(void)
{
	hlt_counter++;
}

void enable_hlt(void)
{
	hlt_counter--;
}

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
	if (current_cpu_data.hlt_works_ok && !hlt_counter) {
		__cli();
		if (!need_resched())
			safe_halt();
		else
			__sti();
	}
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 * Note: on pIV systems this actually saves power!
 */
static void poll_idle (void)
{
	int oldval;

	__sti();

	/*
	 * Deal with another CPU just having chosen a thread to
	 * run here:
	 */
	oldval = xchg(&current->need_resched, -1);

	if (!oldval)
		asm volatile(
			"2:"
			"cmpl $-1, %0;"
			"rep; nop;"
			"rep; nop;"
			"rep; nop;"
			"rep; nop;"
			"rep; nop;"
			"rep; nop;"
			"je 2b;"
				: :"m" (current->need_resched));
}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	/* endless idle loop with no priority at all */

	while (1) {
		void (*idle)(void) = pm_idle;
		if (!idle)
			idle = default_idle;
		/*
		 * We use the last_run timestamp to measure the idleness
		 * of a CPU.
		 */
		current->last_run = jiffies;

		while (!current->need_resched)
			idle();
		schedule();
		check_pgt_cache();
	}
}

static int __init idle_setup (char *str)
{
	if (!strncmp(str, "poll", 4)) {
		printk("using polling idle threads.\n");
		pm_idle = poll_idle;
	}

	return 1;
}

__setup("idle=", idle_setup);

static long no_idt[2];
static int reboot_mode;
int reboot_thru_bios;

#ifdef CONFIG_SMP
int reboot_smp = 0;
static int reboot_cpu = -1;
/* shamelessly grabbed from lib/vsprintf.c for readability */
#define is_digit(c)	((c) >= '0' && (c) <= '9')
#endif
static int __init reboot_setup(char *str)
{
	while(1) {
		switch (*str) {
		case 'w': /* "warm" reboot (no memory testing etc) */
			reboot_mode = 0x1234;
			break;
		case 'c': /* "cold" reboot (with memory testing etc) */
			reboot_mode = 0x0;
			break;
		case 'b': /* "bios" reboot by jumping through the BIOS */
			reboot_thru_bios = 1;
			break;
		case 'h': /* "hard" reboot by toggling RESET and/or crashing the CPU */
			reboot_thru_bios = 0;
			break;
#ifdef CONFIG_SMP
		case 's': /* "smp" reboot by executing reset on BSP or other CPU*/
			reboot_smp = 1;
			if (is_digit(*(str+1))) {
				reboot_cpu = (int) (*(str+1) - '0');
				if (is_digit(*(str+2))) 
					reboot_cpu = reboot_cpu*10 + (int)(*(str+2) - '0');
			}
				/* we will leave sorting out the final value 
				when we are ready to reboot, since we might not
 				have set up boot_cpu_id or smp_num_cpu */
			break;
#endif
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);

/* The following code and data reboots the machine by switching to real
   mode and jumping to the BIOS reset entry point, as if the CPU has
   really been reset.  The previous version asked the keyboard
   controller to pulse the CPU reset line, which is more thorough, but
   doesn't work with at least one type of 486 motherboard.  It is easy
   to stop this code working; hence the copious comments. */

static unsigned long long
real_mode_gdt_entries [3] =
{
	0x0000000000000000ULL,	/* Null descriptor */
	0x00009a000000ffffULL,	/* 16-bit real-mode 64k code at 0x00000000 */
	0x000092000100ffffULL	/* 16-bit real-mode 64k data at 0x00000100 */
};

static struct
{
	unsigned short       size __attribute__ ((packed));
	unsigned long long * base __attribute__ ((packed));
}
real_mode_gdt = { sizeof (real_mode_gdt_entries) - 1, real_mode_gdt_entries },
real_mode_idt = { 0x3ff, 0 };

/* This is 16-bit protected mode code to disable paging and the cache,
   switch to real mode and jump to the BIOS reset code.

   The instruction that switches to real mode by writing to CR0 must be
   followed immediately by a far jump instruction, which set CS to a
   valid value for real mode, and flushes the prefetch queue to avoid
   running instructions that have already been decoded in protected
   mode.

   Clears all the flags except ET, especially PG (paging), PE
   (protected-mode enable) and TS (task switch for coprocessor state
   save).  Flushes the TLB after paging has been disabled.  Sets CD and
   NW, to disable the cache on a 486, and invalidates the cache.  This
   is more like the state of a 486 after reset.  I don't know if
   something else should be done for other chips.

   More could be done here to set up the registers as if a CPU reset had
   occurred; hopefully real BIOSs don't assume much. */

static unsigned char real_mode_switch [] =
{
	0x66, 0x0f, 0x20, 0xc0,			/*    movl  %cr0,%eax        */
	0x66, 0x83, 0xe0, 0x11,			/*    andl  $0x00000011,%eax */
	0x66, 0x0d, 0x00, 0x00, 0x00, 0x60,	/*    orl   $0x60000000,%eax */
	0x66, 0x0f, 0x22, 0xc0,			/*    movl  %eax,%cr0        */
	0x66, 0x0f, 0x22, 0xd8,			/*    movl  %eax,%cr3        */
	0x66, 0x0f, 0x20, 0xc3,			/*    movl  %cr0,%ebx        */
	0x66, 0x81, 0xe3, 0x00, 0x00, 0x00, 0x60,	/*    andl  $0x60000000,%ebx */
	0x74, 0x02,				/*    jz    f                */
	0x0f, 0x09,				/*    wbinvd                 */
	0x24, 0x10,				/* f: andb  $0x10,al         */
	0x66, 0x0f, 0x22, 0xc0			/*    movl  %eax,%cr0        */
};
static unsigned char jump_to_bios [] =
{
	0xea, 0x00, 0x00, 0xff, 0xff		/*    ljmp  $0xffff,$0x0000  */
};

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

extern void setup_identity_mappings(pgd_t *pgd_base, unsigned long start, unsigned long end);

/*
 * Switch to real mode and then execute the code
 * specified by the code and length parameters.
 * We assume that length will aways be less that 100!
 */
void machine_real_restart(unsigned char *code, int length)
{
	unsigned long flags;

	cli();

	/* Write zero to CMOS register number 0x0f, which the BIOS POST
	   routine will recognize as telling it to do a proper reboot.  (Well
	   that's what this book in front of me says -- it may only apply to
	   the Phoenix BIOS though, it's not clear).  At the same time,
	   disable NMIs by setting the top bit in the CMOS address register,
	   as we're about to do peculiar things to the CPU.  I'm not sure if
	   `outb_p' is needed instead of just `outb'.  Use it to be on the
	   safe side.  (Yes, CMOS_WRITE does outb_p's. -  Paul G.)
	 */

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock_irqrestore(&rtc_lock, flags);

	/*
	 * Remap the first 16 MB of RAM at virtual address zero
	 * (this includes the kernel image)
	 */
	setup_identity_mappings(swapper_pg_dir, 0, 16*1024*1024);

	/*
	 * Use `swapper_pg_dir' as our page directory.
	 */
	load_cr3(swapper_pg_dir);

	/* Write 0x1234 to absolute memory location 0x472.  The BIOS reads
	   this on booting to tell it to "Bypass memory test (also warm
	   boot)".  This seems like a fairly standard thing that gets set by
	   REBOOT.COM programs, and the previous reset routine did this
	   too. */

	*((unsigned short *)0x472) = reboot_mode;

	/* For the switch to real mode, copy some code to low memory.  It has
	   to be in the first 64k because it is running in 16-bit mode, and it
	   has to have the same physical and virtual address, because it turns
	   off paging.  Copy it near the end of the first page, out of the way
	   of BIOS variables. */

	memcpy ((void *) (0x1000 - sizeof (real_mode_switch) - 100),
		real_mode_switch, sizeof (real_mode_switch));
	memcpy ((void *) (0x1000 - 100), code, length);

	/* Set up the IDT for real mode. */

	__asm__ __volatile__ ("lidt %0" : : "m" (real_mode_idt));

	/* Set up a GDT from which we can load segment descriptors for real
	   mode.  The GDT is not used in real mode; it is just needed here to
	   prepare the descriptors. */

	__asm__ __volatile__ ("lgdt %0" : : "m" (real_mode_gdt));

	/* Load the data segment registers, and thus the descriptors ready for
	   real mode.  The base address of each segment is 0x100, 16 times the
	   selector value being loaded here.  This is so that the segment
	   registers don't have to be reloaded after switching to real mode:
	   the values are consistent for real mode operation already. */

	__asm__ __volatile__ ("movl $0x0010,%%eax\n"
				"\tmovl %%eax,%%ds\n"
				"\tmovl %%eax,%%es\n"
				"\tmovl %%eax,%%fs\n"
				"\tmovl %%eax,%%gs\n"
				"\tmovl %%eax,%%ss" : : : "eax");

	/* Jump to the 16-bit code that we copied earlier.  It disables paging
	   and the cache, switches to real mode, and jumps to the BIOS reset
	   entry point. */

	__asm__ __volatile__ ("ljmp $0x0008,%0"
				:
				: "i" ((void *) (0x1000 - sizeof (real_mode_switch) - 100)));
}

void machine_restart(char * __unused)
{
#if CONFIG_SMP
	int cpuid;
	
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	if (reboot_smp) {

		/* check to see if reboot_cpu is valid 
		   if its not, default to the BSP */
		if ((reboot_cpu < 0) ||
		    (reboot_cpu > (NR_CPUS - 1)) ||
		    !(phys_cpu_present_map & (1 << reboot_cpu)))
			reboot_cpu = boot_cpu_physical_apicid;

		reboot_smp = 0;  /* use this as a flag to only go through this once*/
		wmb();
		/* re-run this function on the other CPUs
		   it will fall though this section since we have 
		   cleared reboot_smp, and do the reboot if it is the
		   correct CPU, otherwise it halts. */
		if (reboot_cpu != cpuid)
			smp_call_function((void *)machine_restart , NULL, 1, 0);
	}

	/* if we don't pass reboot=s<number> to the kernel command line, then
	   reboot_cpu will be -1, in which case we run on whatever CPU we
	   happen to be on.  If we do pass reboot=s<number> though, then we
	   need to make sure we are the right CPU before continuing. */
	if ((reboot_cpu != -1) && (reboot_cpu != cpuid)) {
		for (;;)
		__asm__ __volatile__ ("hlt");
	}
	/*
	 * Stop all CPUs and turn off local APICs and the IO-APIC, so
	 * other OSs see a clean IRQ state.
	 */
	if (!crashdump_mode())
		smp_send_stop();
	disable_IO_APIC();
#endif

	if(!reboot_thru_bios) {
		/* rebooting needs to touch the page at absolute addr 0 */
		*((unsigned short *)__va(0x472)) = reboot_mode;
		for (;;) {
			int i;
			for (i=0; i<100; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe,0x64);         /* pulse reset low */
				udelay(50);
			}
			/* That didn't work - force a triple fault.. */
			__asm__ __volatile__("lidt %0": :"m" (no_idt));
			__asm__ __volatile__("int3");
		}
	}

	machine_real_restart(jump_to_bios, sizeof(jump_to_bios));
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

extern void show_trace(unsigned long* esp);

void show_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;
	static char buffer[512];
	unsigned int fs = 0, gs = 0;

	lookup_symbol(regs->eip,buffer,512);

	savesegment(fs, fs);
	savesegment(gs, gs);

	printk("\n");
	printk("Pid/TGid: %d/%d, comm: %20s\n", current->pid, current->tgid, current->comm);
	printk("EIP: %04x:[<%08lx>] CPU: %d",0xffff & regs->xcs,regs->eip, smp_processor_id());
	printk("\nEIP is at %s (" UTS_RELEASE ")\n",buffer);
	printk(" ESP: %04x:%08lx",0xffff & regs->xss,regs->esp);
	printk(" EFLAGS: %08lx    %s\n",regs->eflags, print_tainted());
	printk("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		regs->eax,regs->ebx,regs->ecx,regs->edx);
	printk("ESI: %08lx EDI: %08lx EBP: %08lx",
		regs->esi, regs->edi, regs->ebp);
	printk(" DS: %04x ES: %04x FS: %04x GS: %04x\n",
		0xffff & regs->xds,0xffff & regs->xes, fs, gs);

	__asm__("movl %%cr0, %0": "=r" (cr0));
	__asm__("movl %%cr2, %0": "=r" (cr2));
	__asm__("movl %%cr3, %0": "=r" (cr3));
	/* This could fault if %cr4 does not exist */
	__asm__("1: movl %%cr4, %0		\n"
		"2:				\n"
		".section __ex_table,\"a\"	\n"
		".long 1b,2b			\n"
		".previous			\n"
		: "=r" (cr4): "0" (0));
	printk("CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n", cr0, cr2, cr3, cr4);
	show_trace(&regs->esp);
}

/*
 * This gets run with %ebx containing the
 * function to call, and %edx containing
 * the "args".
 */
extern void kernel_thread_helper(void);
__asm__(".align 4\n"
	"kernel_thread_helper:\n\t"
	"movl %edx,%eax\n\t"
	"pushl %edx\n\t"
	"call *%ebx\n\t"
	"pushl %eax\n\t"
	"call do_exit");

/*
 * Create a kernel thread
 */
int arch_kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.ebx = (unsigned long) fn;
	regs.edx = (unsigned long) arg;

	regs.xds = __KERNEL_DS;
	regs.xes = __KERNEL_DS;
	regs.orig_eax = -1;
	regs.eip = (unsigned long) kernel_thread_helper;
	regs.xcs = __KERNEL_CS;
	regs.eflags = 0x286;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, 
		       NULL, NULL);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	/* nothing to do ... */
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.debugreg, 0, sizeof(unsigned long)*8);
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));	
	/*
	 * Forget coprocessor state..
	 */
	clear_fpu(tsk);
	tsk->used_math = 0;
}

void release_thread(struct task_struct *dead_task)
{
	if (dead_task->mm) {
		// temporary debugging check
		if (dead_task->mm->context.size) {
			printk("WARNING: dead process %8s still has LDT? <%d>\n",
					dead_task->comm,
					dead_task->mm->context.size);
			BUG();
		}
	}
}

/*
 * Save a segment.
 */
#define savesegment(seg,value) \
	asm volatile("movl %%" #seg ",%0":"=m" (*(int *)&(value)))

int copy_thread(int nr, unsigned long clone_flags, unsigned long esp,
	unsigned long unused,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct task_struct *tsk;

	childregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) p)) - 1;
	struct_cpy(childregs, regs);
	childregs->eax = 0;
	childregs->esp = esp;
	p->set_child_tid = p->clear_child_tid = NULL;

	p->thread.esp = (unsigned long) childregs;
	p->thread.esp0 = (unsigned long) (childregs+1);

	/*
	 * get the two stack pages, for the virtual stack.
	 *
	 * IMPORTANT: this code relies on the fact that the task
	 * structure is an 8K aligned piece of physical memory.
	 */
	p->thread.stack_page0 = virt_to_page((unsigned long)p);
	p->thread.stack_page1 = virt_to_page((unsigned long)p + PAGE_SIZE);

	p->thread.eip = (unsigned long) __ENTRY_TRAMP_ADDR(ret_from_fork);
	p->real_stack = p;

	savesegment(fs,p->thread.fs);
	savesegment(gs,p->thread.gs);

	tsk = current;
	unlazy_fpu(tsk);
	struct_cpy(&p->thread.i387, &tsk->thread.i387);

	/*
	 * Set a new TLS for the child thread?
	 */
	if (clone_flags & CLONE_SETTLS) {
		struct desc_struct *desc;
		struct user_desc info;
		int idx;

		if (copy_from_user(&info, (void *)childregs->esi, sizeof(info)))
			return -EFAULT;
		if (LDT_empty(&info))
			return -EINVAL;

		idx = info.entry_number;
		if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
			return -EINVAL;

		desc = p->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}
	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	int i;

/* changed the size calculations - should hopefully work better. lbt */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->esp & ~(PAGE_SIZE - 1);
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;
	for (i = 0; i < 8; i++)
		dump->u_debugreg[i] = current->thread.debugreg[i];  

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->regs.ebx = regs->ebx;
	dump->regs.ecx = regs->ecx;
	dump->regs.edx = regs->edx;
	dump->regs.esi = regs->esi;
	dump->regs.edi = regs->edi;
	dump->regs.ebp = regs->ebp;
	dump->regs.eax = regs->eax;
	dump->regs.ds = regs->xds;
	dump->regs.es = regs->xes;
	savesegment(fs,dump->regs.fs);
	savesegment(gs,dump->regs.gs);
	dump->regs.orig_eax = regs->orig_eax;
	dump->regs.eip = regs->eip;
	dump->regs.cs = regs->xcs;
	dump->regs.eflags = regs->eflags;
	dump->regs.esp = regs->esp;
	dump->regs.ss = regs->xss;

	dump->u_fpvalid = dump_fpu (regs, &dump->i387);
}

/* 
 * Capture the user space registers if the task is not running (in user space)
 */
int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs ptregs;
	
	ptregs = *(struct pt_regs *)((unsigned long)tsk + THREAD_SIZE - sizeof(struct pt_regs));

	ptregs.xcs &= 0xffff;
	ptregs.xds &= 0xffff;
	ptregs.xes &= 0xffff;
	ptregs.xss &= 0xffff;

	elf_core_copy_regs(regs, &ptregs);

	return 1;
}

/*
 * This special macro can be used to load a debugging register
 */
#define loaddebug(thread,register) \
		__asm__("movl %0,%%db" #register  \
			: /* no output */ \
			:"r" (thread->debugreg[register]))

/*
 *	switch_to(x,yn) should switch tasks from x to y.
 *
 * We fsave/fwait so that an exception goes off at the right time
 * (as a call from the fsave or fwait in effect) rather than to
 * the wrong process. Lazy FP saving no longer makes any sense
 * with modern CPU's, and this simplifies a lot of things (SMP
 * and UP become the same).
 *
 * NOTE! We used to use the x86 hardware context switching. The
 * reason for not using it any more becomes apparent when you
 * try to recover gracefully from saved state that is no longer
 * valid (stale segment register values in particular). With the
 * hardware task-switch, there is no way to fix up bad state in
 * a reasonable manner.
 *
 * The fact that Intel documents the hardware task-switching to
 * be slow is a fairly red herring - this code is not noticeably
 * faster. However, there _is_ some room for improvement here,
 * so the performance issues may eventually be a valid point.
 * More important, however, is the fact that this allows us much
 * more flexibility.
 */
struct task_struct *  __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
				 *next = &next_p->thread;
	int cpu = smp_processor_id();
	struct tss_struct *tss = init_tss + cpu;

	/* never put a printk in __switch_to... printk() calls wake_up*() indirectly */

	unlazy_fpu(prev_p);

	if (next_p->mm)
		load_user_cs_desc(cpu, next_p->mm);

#if CONFIG_X86_HIGH_ENTRY
	/*
	 * Set the ptes of the virtual stack. (NOTE: a TLB flush is
	 * needed because otherwise NMIs could interrupt the
	 * user-return code with a virtual stack and stale TLBs.)
	 */
	__kunmap_atomic_type(KM_VSTACK0);
	__kunmap_atomic_type(KM_VSTACK1);
	__kmap_atomic(next->stack_page0, KM_VSTACK0);
	__kmap_atomic(next->stack_page1, KM_VSTACK1);

	/*
	 * Reload esp0:
	 */
	/*
	 * NOTE: here we rely on the task being the stack as well
	 */
	next_p->virtual_stack = (void *)__kmap_atomic_vaddr(KM_VSTACK0);
#endif
	tss->esp0 = virtual_esp0(next_p);
	/*
	 * Load the per-thread Thread-Local Storage descriptor.
	 */
	load_TLS(next, cpu);

	/*
	 * Save away %fs and %gs. No need to save %es and %ds, as
	 * those are always kernel segments while inside the kernel.
	 */
	asm volatile("movl %%fs,%0":"=m" (*(int *)&prev->fs));
	asm volatile("movl %%gs,%0":"=m" (*(int *)&prev->gs));

	/*
	 * Restore %fs and %gs if needed.
	 */
	if (unlikely(prev->fs | prev->gs | next->fs | next->gs)) {
		loadsegment(fs, next->fs);
		loadsegment(gs, next->gs);
	}

	/*
	 * Now maybe reload the debug registers
	 */
	if (unlikely(next->debugreg[7])) {
		loaddebug(next, 0);
		loaddebug(next, 1);
		loaddebug(next, 2);
		loaddebug(next, 3);
		/* no 4 and 5 */
		loaddebug(next, 6);
		loaddebug(next, 7);
	}

	if (unlikely(prev->ioperm || next->ioperm)) {
		if (next->ioperm) {
			/*
			 * 4 cachelines copy ... not good, but not that
			 * bad either. Anyone got something better?
			 * This only affects processes which use ioperm().
			 * [Putting the TSSs into 4k-tlb mapped regions
			 * and playing VM tricks to switch the IO bitmap
			 * is not really acceptable.]
			 */
			memcpy(tss->io_bitmap, next->io_bitmap,
				 IO_BITMAP_BYTES);
			tss->bitmap = IO_BITMAP_OFFSET;
		} else
			/*
			 * a bitmap offset pointing outside of the TSS limit
			 * causes a nicely controllable SIGSEGV if a process
			 * tries to use a port IO instruction. The first
			 * sys_ioperm() call sets up the bitmap properly.
			 */
			tss->bitmap = INVALID_IO_BITMAP_OFFSET;
	}
	return prev_p;
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	return do_fork(SIGCHLD, regs.esp, &regs, 0, NULL, NULL);
}

asmlinkage int sys_clone(struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int *parent_tidptr, *child_tidptr;

	clone_flags = regs.ebx;
	newsp = regs.ecx;
	parent_tidptr = (int *)regs.edx;
	child_tidptr = (int *)regs.edi;
	if (!newsp)
		newsp = regs.esp;
	return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0, 
		       parent_tidptr, child_tidptr);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys_vfork(struct pt_regs regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.esp, &regs, 0, 
		       NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) regs.ebx);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) regs.ecx, (char **) regs.edx, &regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
out:
	return error;
}

/*
 * These bracket the sleeping functions..
 */
extern void scheduling_functions_start_here(void);
extern void scheduling_functions_end_here(void);
#define first_sched	((unsigned long) scheduling_functions_start_here)
#define last_sched	((unsigned long) scheduling_functions_end_here)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ebp, esp, eip;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	stack_page = (unsigned long)p;
	esp = p->thread.esp;
	if (!stack_page || esp < stack_page || esp > 8188+stack_page)
		return 0;
	/* include/asm-i386/system.h:switch_to() pushes ebp last. */
	ebp = *(unsigned long *) esp;
	do {
		if (ebp < stack_page || ebp > 8184+stack_page)
			return 0;
		eip = *(unsigned long *) (ebp+4);
		if (eip < first_sched || eip >= last_sched)
			return eip;
		ebp = *(unsigned long *) ebp;
	} while (count++ < 16);
	return 0;
}
#undef last_sched
#undef first_sched

/*
 * sys_alloc_thread_area: get a yet unused TLS descriptor index.
 */
static int get_free_idx(void)
{
	struct thread_struct *t = &current->thread;
	int idx;

	for (idx = 0; idx < GDT_ENTRY_TLS_ENTRIES; idx++)
		if (desc_empty(t->tls_array + idx))
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

/*
 * Set a given TLS descriptor:
 */
asmlinkage int sys_set_thread_area(struct user_desc *u_info)
{
	struct thread_struct *t = &current->thread;
	struct user_desc info;
	struct desc_struct *desc;
	int cpu, idx;

	if (copy_from_user(&info, u_info, sizeof(info)))
		return -EFAULT;
	idx = info.entry_number;

	/*
	 * index -1 means the kernel should try to find and
	 * allocate an empty descriptor:
	 */
	if (idx == -1) {
		idx = get_free_idx();
		if (idx < 0)
			return idx;
		if (put_user(idx, &u_info->entry_number))
			return -EFAULT;
	}

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = t->tls_array + idx - GDT_ENTRY_TLS_MIN;

	cpu = smp_processor_id();

	if (LDT_empty(&info)) {
		desc->a = 0;
		desc->b = 0;
	} else {
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}
	load_TLS(t, cpu);


	return 0;
}

/*
 * Get the current Thread-Local Storage area:
 */

#define GET_BASE(desc) ( \
	(((desc)->a >> 16) & 0x0000ffff) | \
	(((desc)->b << 16) & 0x00ff0000) | \
	( (desc)->b        & 0xff000000)   )

#define GET_LIMIT(desc) ( \
	((desc)->a & 0x0ffff) | \
	 ((desc)->b & 0xf0000) )
	
#define GET_32BIT(desc)		(((desc)->b >> 23) & 1)
#define GET_CONTENTS(desc)	(((desc)->b >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)->b >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)->b >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)->b >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)->b >> 20) & 1)

asmlinkage int sys_get_thread_area(struct user_desc *u_info)
{
	struct user_desc info;
	struct desc_struct *desc;
	int idx;

	if (get_user(idx, &u_info->entry_number))
		return -EFAULT;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	memset(&info, 0, sizeof(info));

	desc = current->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;

	info.entry_number = idx;
	info.base_addr = GET_BASE(desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/*
 * Get a random word:
 */
static inline unsigned int get_random_int(void)
{
	unsigned int val = 0;

	if (!exec_shield_randomize)
		return 0;

#ifdef CONFIG_X86_HAS_TSC
	rdtscl(val);
#endif
	val += current->pid + jiffies + (int)&val;

	/*
	 * Use IP's RNG. It suits our purpose perfectly: it re-keys itself
	 * every second, from the entropy pool (and thus creates a limited
	 * drain on it), and uses halfMD4Transform within the second. We
	 * also spice it with the TSC (if available), jiffies, PID and the
	 * stack address:
	 */
	return secure_ip_id(val);
}

unsigned long arch_align_stack(unsigned long sp)
{
	if (current->flags & PF_RELOCEXEC)
		sp -= ((get_random_int() % 1024) << 4);
	return sp & ~0xf;
}

#if SHLIB_BASE >= 0x01000000
# error SHLIB_BASE must be under 16MB!
#endif

static unsigned long
arch_get_unmapped_nonexecutable_area(struct mm_struct *mm, unsigned long addr, unsigned long len)
{
	struct vm_area_struct *vma, *prev_vma;
	unsigned long stack_limit, mmap_top, reserved;
	int first_time = 1;

	/* requested length too big for entire address space */
	if (len > TASK_SIZE) 
		return -ENOMEM;

	/* don't allow allocations above current stack limit */
	mmap_top = (arch_align_stack(mm->start_stack) & PAGE_MASK);
	reserved = current->rlim[RLIMIT_STACK].rlim_cur;
	if (mmap_top == 0UL || mmap_top > TASK_SIZE)
		mmap_top = TASK_SIZE;
	if (reserved < mmap_top - STACK_BUFFER_SPACE)
		reserved += STACK_BUFFER_SPACE;
	else
		reserved = mmap_top;

	/* now convert "reserved" to be distance down from TASK_SIZE */
	reserved += (TASK_SIZE - mmap_top);
	if (reserved > current->rlim[RLIMIT_STACK].rlim_max)
		reserved = current->rlim[RLIMIT_STACK].rlim_max;
	if (reserved < TASK_SIZE - STACK_BUFFER_SPACE - mm->brk)
		stack_limit = TASK_SIZE - PAGE_ALIGN(reserved);
	else
		stack_limit = 0UL;
	if (mm->non_executable_cache > stack_limit)
		mm->non_executable_cache = stack_limit;

	/* requesting a specific address */
        if (addr) {
                addr = PAGE_ALIGN(addr);
                vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && 
		    (!vma || addr + len <= vma->vm_start))
			return addr;
        }

	/* make sure it can fit in the remaining address space */
	if (len > mm->non_executable_cache) {
		if (len > stack_limit)
			return -ENOMEM;
		first_time = 0;
		mm->non_executable_cache = stack_limit;
	}

	/* either no address requested or cant fit in requested address hole */
try_again:
        addr = (mm->non_executable_cache - len)&PAGE_MASK;
	do {
       	 	if (!(vma = find_vma_prev(mm, addr, &prev_vma)))
                        return -ENOMEM;

		/* Do not allocate non-MAP_FIXED all the way down to zero. */
		if (addr < SHLIB_BASE)
			goto fail;

		/* new region fits between prev_vma->vm_end and vma->vm_start, use it */
		if (addr+len <= vma->vm_start && (!prev_vma || (addr >= prev_vma->vm_end))) {
			/* remember the address as a hint for next time */
			mm->non_executable_cache = addr;
			return addr;

		/* pull non_executable_cache down to the first hole */
		} else if (mm->non_executable_cache == vma->vm_end)
				mm->non_executable_cache = vma->vm_start;	

		/* try just below the current vma->vm_start */
		addr = vma->vm_start-len;
        } while (len <= vma->vm_start);

fail:
	/* if hint left us with no space for the requested mapping try again */
	if (first_time && len <= stack_limit) {
		first_time = 0;
		mm->non_executable_cache = stack_limit;
		goto try_again;
	}
	return -ENOMEM;
}

static unsigned long randomize_range(unsigned long start, unsigned long end, unsigned long len)
{
	unsigned long range = end - len - start;
	if (end <= start + len)
		return 0;
	return PAGE_ALIGN(get_random_int() % range + start);
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags,
		unsigned long prot)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int ascii_shield = 0;
	unsigned long tmp;

	if (len > TASK_SIZE)
		return -ENOMEM;

	/*
	 * Default to the old behavior if executable relocation
	 * is disabled:
	 */
	if (!(current->flags & PF_RELOCEXEC))
		prot &= ~PROT_EXEC;

	if (!addr && (prot & PROT_EXEC) && !(flags & MAP_FIXED))
		addr = randomize_range(SHLIB_BASE, 0x01000000, len);

	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	if (prot & PROT_EXEC) {
		ascii_shield = 1;
		addr = SHLIB_BASE;
	} else {
		/* this can fail if the stack was unlimited */
search_all:
		if ((tmp = arch_get_unmapped_nonexecutable_area(mm, addr, len)) != -ENOMEM)
			return tmp;

		/* the following is like arch_align_stack(), but upwards */
		addr = TASK_UNMAPPED_BASE;
		if (current->flags & PF_RELOCEXEC)
			addr += (((get_random_int() % 1024) << 4) & PAGE_MASK);
	}

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Must not let a PROT_EXEC mapping get into the
			 * brk area:
			 */
			if (ascii_shield && (addr + len > mm->brk)) {
				ascii_shield = 0;
				goto search_all;
			}
			/*
			 * Up until the brk area we randomize addresses
			 * as much as possible:
			 */
			if (ascii_shield && (addr >= 0x01000000)) {
				tmp = randomize_range(0x01000000, PAGE_ALIGN(max(mm->start_brk, 0x08000000UL)), len);
				vma = find_vma(mm, tmp);
				if (TASK_SIZE - len >= tmp &&
				    (!vma || tmp + len <= vma->vm_start))
					return tmp;
			}
			/*
			 * Ok, randomization didnt work out - return
			 * the result of the linear search:
			 */
			return addr;
		}
		addr = vma->vm_end;
	}
}

void arch_add_exec_range(struct mm_struct *mm, unsigned long limit)
{
	if (limit > mm->context.exec_limit) {
		mm->context.exec_limit = limit;
		set_user_cs(&mm->context.user_cs, limit);
		if (mm == current->mm)
			load_user_cs_desc(smp_processor_id(), mm);
	}
}

void arch_remove_exec_range(struct mm_struct *mm, unsigned long old_end)
{
	struct vm_area_struct *vma;
	unsigned long limit = 0;

	if (old_end == mm->context.exec_limit) {
		for (vma = mm->mmap; vma; vma = vma->vm_next)
			if ((vma->vm_flags & VM_EXEC) && (vma->vm_end > limit))
				limit = vma->vm_end;

		mm->context.exec_limit = limit;
		set_user_cs(&mm->context.user_cs, limit);
		if (mm == current->mm)
			load_user_cs_desc(smp_processor_id(), mm);
	}
}

void arch_flush_exec_range(struct mm_struct *mm)
{
	mm->context.exec_limit = 0;
	set_user_cs(&mm->context.user_cs, 0);
}

/*
 * Generate random brk address between 128MB and 196MB. (if the layout
 * allows it.)
 */
void randomize_brk(unsigned long old_brk)
{
	unsigned long new_brk, range_start, range_end;

	range_start = 0x08000000;
	if (current->mm->brk >= range_start)
		range_start = current->mm->brk;
	range_end = range_start + 0x02000000;
	new_brk = randomize_range(range_start, range_end, 0);
	if (new_brk)
		current->mm->brk = new_brk;
}

