/*
 *  linux/arch/i386/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/version.h>

#ifdef CONFIG_MCA
#include <linux/mca.h>
#include <asm/processor.h>
#endif

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/nmi.h>

#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>

#ifdef CONFIG_X86_VISWS_APIC
#include <asm/cobalt.h>
#include <asm/lithium.h>
#endif

#include <linux/irq.h>
#include <linux/module.h>

asmlinkage int system_call(void);
asmlinkage void lcall7(void);
asmlinkage void lcall27(void);

struct desc_struct default_ldt[] __attribute__((__section__(".data.default_ldt"))) = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
struct page *default_ldt_page;

/*
 * The IDT has to be page-aligned to simplify the Pentium
 * F0 0F bug workaround.. We have a special link segment
 * for this.
 */
struct desc_struct idt_table[256] __attribute__((__section__(".data.idt"))) = { {0, 0}, };

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void double_fault(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void simd_coprocessor_error(void);
asmlinkage void alignment_check(void);
asmlinkage void spurious_interrupt_bug(void);
asmlinkage void machine_check(void);

int kstack_depth_to_print = 24;


/*
 * If the address is either in the .text section of the
 * kernel, or in the vmalloc'ed module regions, it *may* 
 * be the address of a calling routine
 */

#ifdef CONFIG_MODULES

extern struct module *module_list;
extern struct module kernel_module;

static inline int kernel_text_address(unsigned long addr)
{
	int retval = 0;
	struct module *mod;

	if (addr >= (unsigned long) &_stext &&
	    addr <= (unsigned long) &_etext)
		return 1;

	for (mod = module_list; mod != &kernel_module; mod = mod->next) {
		/* mod_bound tests for addr being inside the vmalloc'ed
		 * module area. Of course it'd be better to test only
		 * for the .text subset... */
		if (mod_bound(addr, 0, mod)) {
			retval = 1;
			break;
		}
	}

	return retval;
}

#else

static inline int kernel_text_address(unsigned long addr)
{
	return (addr >= (unsigned long) &_stext &&
		addr <= (unsigned long) &_etext);
}

#endif

void show_trace(unsigned long * stack)
{
#if !CONFIG_FRAME_POINTER
	int i;
#endif
	unsigned long addr, limit;
	/* static to not take up stackspace; if we race here too bad */
	static char buffer[512];

	if (!stack)
		stack = (unsigned long*)&stack;

	printk("Call Trace:   ");
	/*
	 * If we have frame pointers then use them to get
	 * a 100% exact backtrace, up until the entry frame:
	 */
#if CONFIG_FRAME_POINTER
#define DO(n) \
	addr = (int)__builtin_return_address(n);	\
	if (!kernel_text_address(addr))			\
		goto out;				\
	lookup_symbol(addr, buffer, 512);		\
	printk("[<%08lx>] %s\n", addr, buffer);

	DO(0); DO(1); DO(2); DO(3); DO(4); DO(5); DO(7); DO(8); DO(9);
	DO(10); DO(11); DO(12); DO(13); DO(14); DO(15); DO(17); DO(18); DO(19);
out:
#else
	i = 1;
	limit = ((unsigned long)stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE - 3;
	while ((unsigned long)stack < limit) {
		addr = *stack++;
		if (kernel_text_address(addr)) {
			lookup_symbol(addr, buffer, 512);
			printk("[<%08lx>] %s (0x%p)\n", addr,buffer,stack-1);
			i++;
		}
	}
#endif
	printk("\n");
}

void show_trace_task(struct task_struct *tsk)
{
	unsigned long esp = tsk->thread.esp;

	/* User space on another CPU? */
	if ((esp ^ (unsigned long)tsk) & (PAGE_MASK<<1))
		return;
	show_trace((unsigned long *)esp);
}

void show_stack(unsigned long * esp)
{
	unsigned long *stack, limit;
	int i;

	// debugging aid: "show_stack(NULL);" prints the
	// back trace for this cpu.

	if(esp==NULL)
		esp=(unsigned long*)&esp;

	stack = esp;
	limit = ((unsigned long)stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE - 3;
	for(i=0; i < kstack_depth_to_print; i++) {
		if ((unsigned long)stack >= limit)
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", *stack++);
	}
	printk("\n");
	show_trace(esp);
}

/*
 * The architecture-independent backtrace generator
 */
void dump_stack(void)
{
	show_stack(0);
}

#ifdef CONFIG_MK7
#define ARCHIT "/athlon"
#else
#define ARCHIT "/i686"
#endif

void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = 1;
	unsigned long esp;
	unsigned short ss;
	static char buffer[512];

	esp = (unsigned long) (&regs->esp);
	ss = __KERNEL_DS;
	if (regs->xcs & 3) {
		in_kernel = 0;
		esp = regs->esp;
		ss = regs->xss & 0xffff;
	}

	print_modules();
	lookup_symbol(regs->eip, buffer, 512);
	printk("CPU:    %d\nEIP:    %04x:[<%08lx>]    %s\nEFLAGS: %08lx\n",
		smp_processor_id(), 0xffff & regs->xcs, regs->eip, print_tainted(), regs->eflags);
	printk("\nEIP is at %s (" UTS_RELEASE ARCHIT ")\n",buffer);
	printk("eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
		regs->eax, regs->ebx, regs->ecx, regs->edx);
	printk("esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
		regs->esi, regs->edi, regs->ebp, esp);
	printk("ds: %04x   es: %04x   ss: %04x\n",
		regs->xds & 0xffff, regs->xes & 0xffff, ss);
	printk("Process %s (pid: %d, stackpage=%08lx)",
		current->comm, current->pid, 4096+(unsigned long)current);
	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {

		printk("\nStack: ");
		show_stack((unsigned long*)esp);

		printk("Code:");
		if(regs->eip < PAGE_OFFSET)
			goto bad;

		for(i=0;i<20;i++)
		{
			unsigned char c;
			if(__get_user(c, &((unsigned char*)regs->eip)[i])) {
bad:
				printk(" Bad EIP value.");
				break;
			}
			printk(" %02x", c);
		}
		printk("\n");
	}
	printk("\n");
}	

static void handle_BUG(struct pt_regs *regs)
{
	unsigned short ud2;
	unsigned short line;
	char *file;
	char c;
	unsigned long eip;

	if (regs->xcs & 3)
		goto no_bug;		/* Not in kernel */

	eip = regs->eip;

	if (eip < PAGE_OFFSET)
		goto no_bug;
	if (__get_user(ud2, (unsigned short *)eip))
		goto no_bug;
	if (ud2 != 0x0b0f)
		goto no_bug;
	if (__get_user(line, (unsigned short *)(eip + 2)))
		goto bug;
	if (__get_user(file, (char **)(eip + 4)) ||
		(unsigned long)file < PAGE_OFFSET || __get_user(c, file))
		file = "<bad filename>";
	printk("------------[ cut here ]------------\n");
	printk("kernel BUG at %s:%d!\n", file, line);

no_bug:
	return;

	/* Here we know it was a BUG but file-n-line is unavailable */
bug:
	printk("Kernel BUG\n");
}

spinlock_t die_lock = SPIN_LOCK_UNLOCKED;
static int die_owner = -1;

void die(const char * str, struct pt_regs * regs, long err)
{
#ifdef CONFIG_PNPBIOS		
	if (regs->xcs == 0x60 || regs->xcs == 0x68)
	{
		extern u32 pnp_bios_fault_eip, pnp_bios_fault_esp;
		extern u32 pnp_bios_is_utter_crap;
		pnp_bios_is_utter_crap = 1;
		printk(KERN_CRIT "PNPBIOS fault.. attempting recovery.\n");
		__asm__ volatile(
			"movl %0, %%esp\n\t"
			"jmp %1\n\t"
			: "=a" (pnp_bios_fault_esp), "=b" (pnp_bios_fault_eip));
		panic("do_trap: can't hit this");
	}
#endif	
	console_verbose();
	local_irq_disable();
	if (!spin_trylock(&die_lock)) {
		if (smp_processor_id() != die_owner)
			spin_lock(&die_lock);
		/* allow recursive die to fall through */
	}
	die_owner = smp_processor_id();
	bust_spinlocks(1);
	handle_BUG(regs);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_registers(regs);
	try_crashdump(regs);
	if (panic_on_oops)
		panic("Fatal exception");
	bust_spinlocks(0);
	die_owner = -1;
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!(regs->eflags & VM_MASK) && !(3 & regs->xcs))
		die(str, regs, err);
}

static inline unsigned long get_cr2(void)
{
	unsigned long address;

	/* get the address */
	__asm__("movl %%cr2,%0":"=r" (address));
	return address;
}

static void inline do_trap(int trapnr, int signr, char *str, int vm86,
			   struct pt_regs * regs, long error_code, siginfo_t *info)
{
	if (regs->eflags & VM_MASK) {
		if (vm86)
			goto vm86_trap;
		else
			goto trap_signal;
	}

	if (!(regs->xcs & 3))
		goto kernel_trap;

	trap_signal: {
		struct task_struct *tsk = current;
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;
		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	}

	kernel_trap: {
		unsigned long fixup = search_exception_table(regs->eip);
		if (fixup)
			regs->eip = fixup;
		else	
			die(str, regs, error_code);
		return;
	}

	vm86_trap: {
		int ret = handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, trapnr);
		if (ret) goto trap_signal;
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	do_trap(trapnr, signr, str, 0, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void *)siaddr; \
	do_trap(trapnr, signr, str, 0, regs, error_code, &info); \
}

#define DO_VM86_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	do_trap(trapnr, signr, str, 1, regs, error_code, NULL); \
}

#define DO_VM86_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void *)siaddr; \
	do_trap(trapnr, signr, str, 1, regs, error_code, &info); \
}

DO_VM86_ERROR_INFO( 0, SIGFPE,  "divide error", divide_error, FPE_INTDIV, regs->eip)
DO_VM86_ERROR( 3, SIGTRAP, "int3", int3)
DO_VM86_ERROR( 4, SIGSEGV, "overflow", overflow)
DO_VM86_ERROR( 5, SIGSEGV, "bounds", bounds)
DO_ERROR_INFO( 6, SIGILL,  "invalid operand", invalid_op, ILL_ILLOPN, regs->eip)
DO_VM86_ERROR( 7, SIGSEGV, "device not available", device_not_available)
DO_ERROR( 8, SIGSEGV, "double fault", double_fault)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, get_cr2())

/*
 * the original non-exec stack patch was written by
 * Solar Designer <solar at openwall.com>. Thanks!
 */
asmlinkage void do_general_protection(struct pt_regs * regs, long error_code)
{
	if (regs->eflags & VM_MASK)
		goto gp_in_vm86;

	if (!(regs->xcs & 3))
		goto gp_in_kernel;

	/*
	 * lazy-check for CS validity on exec-shield binaries:
	 */
	if (current->mm) {
		int cpu = smp_processor_id();
		struct desc_struct *desc1, *desc2;
		struct vm_area_struct *vma;
		unsigned long limit = 0;
		
		spin_lock(&current->mm->page_table_lock);
		for (vma = current->mm->mmap; vma; vma = vma->vm_next)
			if ((vma->vm_flags & VM_EXEC) && (vma->vm_end > limit))
				limit = vma->vm_end;
		spin_unlock(&current->mm->page_table_lock);

		current->mm->context.exec_limit = limit;
		set_user_cs(&current->mm->context.user_cs, limit);

		desc1 = &current->mm->context.user_cs;
		desc2 = cpu_gdt_table[cpu] + GDT_ENTRY_DEFAULT_USER_CS;

		/*
		 * The CS was not in sync - reload it and retry the
		 * instruction. If the instruction still faults then
		 * we wont hit this branch next time around.
		 */
		if (desc1->a != desc2->a || desc1->b != desc2->b) {
			if (print_fatal_signals >= 3) {
				printk("#GPF fixup (%ld[seg:%lx]) at %08lx, CPU#%d.\n", error_code, error_code/8, regs->eip, smp_processor_id());
				printk(" exec_limit: %08lx, user_cs: %08lx/%08lx, CPU_cs: %08lx/%08lx.\n", current->mm->context.exec_limit, desc1->a, desc1->b, desc2->a, desc2->b);
			}
			load_user_cs_desc(cpu, current->mm);
			return;
		}
	}
	if (print_fatal_signals) {
		printk("#GPF(%ld[seg:%lx]) at %08lx, CPU#%d.\n", error_code, error_code/8, regs->eip, smp_processor_id());
		printk(" exec_limit: %08lx, user_cs: %08lx/%08lx.\n", current->mm->context.exec_limit, current->mm->context.user_cs.a, current->mm->context.user_cs.b);
	}

	current->thread.error_code = error_code;
	current->thread.trap_no = 13;
	force_sig(SIGSEGV, current);
	return;

gp_in_vm86:
	handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
	return;

gp_in_kernel:
	{
		unsigned long fixup;
		fixup = search_exception_table(regs->eip);
		if (fixup) {
			regs->eip = fixup;
			return;
		}
		die("general protection fault", regs, error_code);
	}
}

static void mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk("Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
	printk("You probably have a hardware problem with your RAM chips\n");

	/* Clear and disable the memory parity error line. */
	reason = (reason & 0xf) | 4;
	outb(reason, 0x61);
}

static void io_check_error(unsigned char reason, struct pt_regs * regs)
{
	unsigned long i;

	printk("NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	i = 2000;
	while (--i) udelay(1000);
	reason &= ~8;
	outb(reason, 0x61);
}

static void unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
#ifdef CONFIG_MCA
	/* Might actually be able to figure out what the guilty party
	* is. */
	if( MCA_bus ) {
		mca_handle_nmi();
		return;
	}
#endif
	printk("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
		reason, smp_processor_id());
	printk("Dazed and confused, but trying to continue\n");
	printk("Do you have a strange power saving mode enabled?\n");
}

int unknown_nmi_panic = 0;
int mem_nmi_panic = 0;

static spinlock_t nmi_print_lock = SPIN_LOCK_UNLOCKED;

void die_nmi(struct pt_regs *regs, const char *msg)
{
	spin_lock(&nmi_print_lock);
	/*
	 * We are in trouble anyway, lets at least try
	 * to get a message out.
	 */
	bust_spinlocks(1);
	printk("%s on CPU%d, eip %08lx, registers:\n",
		msg, smp_processor_id(), regs->eip);
	show_registers(regs);
	try_crashdump(regs);
	printk("console shuts up ...\n");
	console_silent();
	spin_unlock(&nmi_print_lock);
	bust_spinlocks(0);
	do_exit(SIGSEGV);
}

static void default_do_nmi(struct pt_regs * regs)
{
	unsigned char reason = inb(0x61);
 
	if (!(reason & 0xc0)) {
#if CONFIG_X86_LOCAL_APIC
		/*
		 * Ok, so this is none of the documented NMI sources,
		 * so it must be the NMI watchdog.
		 */
		if (nmi_watchdog) {
			nmi_watchdog_tick(regs);
			return;
		}
#endif
		if (unknown_nmi_panic) {
			char buf[64];
			sprintf(buf, "NMI received for unknown reason %02x", reason);
			die_nmi(regs, buf);
		}
		unknown_nmi_error(reason, regs);
		return;
	}
	if (reason & 0x80) {
		if (mem_nmi_panic) {
			char buf[64];
			sprintf(buf, "NMI received for possible memory parity error %02x", reason);
			die_nmi(regs, buf);
		}
		mem_parity_error(reason, regs);
	}
	if (reason & 0x40)
		io_check_error(reason, regs);
	/*
	 * Reassert NMI in case it became active meanwhile
	 * as it's edge-triggered.
	 */
	outb(0x8f, 0x70);
	inb(0x71);		/* dummy */
	outb(0x0f, 0x70);
	inb(0x71);		/* dummy */
}

static int dummy_nmi_callback(struct pt_regs * regs, int cpu)
{
	return 0;
}
 
static nmi_callback_t nmi_callback = dummy_nmi_callback;
 
asmlinkage void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu = smp_processor_id();

	++nmi_count(cpu);

	if (!nmi_callback(regs, cpu))
		default_do_nmi(regs);
}

void set_nmi_callback(nmi_callback_t callback)
{
	nmi_callback = callback;
}

void unset_nmi_callback(void)
{
	nmi_callback = dummy_nmi_callback;
}

/*
 * Our handling of the processor debug registers is non-trivial.
 * We do not clear them on entry and exit from the kernel. Therefore
 * it is possible to get a watchpoint trap here from inside the kernel.
 * However, the code in ./ptrace.c has ensured that the user can
 * only set watchpoints on userspace addresses. Therefore the in-kernel
 * watchpoint trap can only occur in code which is reading/writing
 * from user space. Such code must not hold kernel locks (since it
 * can equally take a page fault), therefore it is safe to call
 * force_sig_info even though that claims and releases locks.
 * 
 * Code in ./signal.c ensures that the debug control register
 * is restored before we deliver any signal, and therefore that
 * user code runs with the correct debug control register even though
 * we clear it here.
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code)
 */
asmlinkage void do_debug(struct pt_regs * regs, long error_code)
{
	unsigned int condition;
	struct task_struct *tsk = current;
	unsigned long eip = regs->eip;
	siginfo_t info;

	__asm__ __volatile__("movl %%db6,%0" : "=r" (condition));

	/* If the user set TF, it's simplest to clear it right away. */
	if ((eip >= PAGE_OFFSET_USER) && (regs->eflags & TF_MASK))
		goto clear_TF;

	/*
	 * Mask out spurious debug traps due to lazy DR7 setting
	 * and due to kernel-space faults due to 4G/4G
	 */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->thread.debugreg[7] || !user_mode(regs))
			goto clear_dr7;
	}

	if (regs->eflags & VM_MASK)
		goto debug_vm86;

	/* Save debug status register where ptrace can see it */
	tsk->thread.debugreg[6] = condition;

	/* Mask out spurious TF errors due to lazy TF clearing */
	if (condition & DR_STEP) {
		/*
		 * The TF error should be masked out only if the current
		 * process is not traced and if the TRAP flag has been set
		 * previously by a tracing process (condition detected by
		 * the PT_DTRACE flag); remember that the i386 TRAP flag
		 * can be modified by the process itself in user mode,
		 * allowing programs to debug themselves without the ptrace()
		 * interface.
		 */
		if ((regs->xcs & 3) == 0)
			goto clear_TF;
		if ((tsk->ptrace & (PT_DTRACE|PT_PTRACED)) == PT_DTRACE)
			goto clear_TF;
	}

	/* Ok, finally something we can handle */
	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	
	/* If this is a kernel mode trap, save the user PC on entry to 
	 * the kernel, that's what the debugger can make sense of.
	 */
	info.si_addr = ((regs->xcs & 3) == 0) ? (void *)tsk->thread.eip : 
	                                        (void *)regs->eip;
	force_sig_info(SIGTRAP, &info, tsk);

	/* Disable additional traps. They'll be re-enabled when
	 * the signal is delivered.
	 */
clear_dr7:
	__asm__("movl %0,%%db7"
		: /* no output */
		: "r" (0));
	return;

debug_vm86:
	handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, 1);
	return;

clear_TF:
	regs->eflags &= ~TF_MASK;
	return;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void *eip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short cwd, swd;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 16;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = eip;
	/*
	 * (~cwd & swd) will mask out exceptions that are not set to unmasked
	 * status.  0x3f is the exception bits in these regs, 0x200 is the
	 * C1 reg you need in case of a stack fault, 0x040 is the stack
	 * fault bit.  We should only be taking one exception at a time,
	 * so if this combination doesn't produce any single exception,
	 * then we have a bad program that isn't syncronizing its FPU usage
	 * and it will suffer the consequences since we won't be able to
	 * fully reproduce the context of the exception
	 */
	cwd = get_fpu_cwd(task);
	swd = get_fpu_swd(task);
	switch (((~cwd) & swd & 0x3f) | (swd & 0x240)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
		case 0x041: /* Stack Fault */
		case 0x241: /* Stack Fault | Direction */
			info.si_code = FPE_FLTINV;
			/* Should we clear the SF or let user space do it ???? */
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
	ignore_irq13 = 1;
	math_error((void *)regs->eip);
}

void simd_math_error(void *eip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short mxcsr;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 19;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = eip;
	/*
	 * The SIMD FPU exceptions are handled a little differently, as there
	 * is only a single status/control register.  Thus, to determine which
	 * unmasked exception was caught we must mask the exception mask bits
	 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
	 */
	mxcsr = get_fpu_mxcsr(task);
	switch (~((mxcsr & 0x1f80) >> 7) & (mxcsr & 0x3f)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_simd_coprocessor_error(struct pt_regs * regs,
					  long error_code)
{
	if (cpu_has_xmm) {
		/* Handle SIMD FPU exceptions on PIII+ processors. */
		ignore_irq13 = 1;
		simd_math_error((void *)regs->eip);
	} else {
		/*
		 * Handle strange cache flush from user space exception
		 * in all other cases.  This is undocumented behaviour.
		 */
		if (regs->eflags & VM_MASK) {
			handle_vm86_fault((struct kernel_vm86_regs *)regs,
					  error_code);
			return;
		}
		die_if_kernel("cache flush denied", regs, error_code);
		current->thread.trap_no = 19;
		current->thread.error_code = error_code;
		force_sig(SIGSEGV, current);
	}
}

asmlinkage void do_spurious_interrupt_bug(struct pt_regs * regs,
					  long error_code)
{
#if 0
	/* No need to warn about this any longer. */
	printk("Ignoring P6 Local APIC Spurious Interrupt Bug...\n");
#endif
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(struct pt_regs regs)
{
	__asm__ __volatile__("clts");		/* Allow maths ops (or we recurse) */

	if (current->used_math) {
		restore_fpu(current);
	} else {
		init_fpu();
	}
	current->flags |= PF_USEDFPU;	/* So we fnsave on switch_to() */
}

#ifndef CONFIG_MATH_EMULATION

asmlinkage void math_emulate(long arg)
{
	printk("math-emulation not enabled and no coprocessor found.\n");
	printk("killing %s.\n",current->comm);
	force_sig(SIGFPE,current);
	schedule();
}

#endif /* CONFIG_MATH_EMULATION */

void __init trap_init_virtual_IDT(void)
{
	/*
	 * "idt" is magic - it overlaps the idt_descr
	 * variable so that updating idt will automatically
	 * update the idt descriptor..
	 */
	__set_fixmap(FIX_IDT, __pa(&idt_table), PAGE_KERNEL_RO);
	idt_descr.address = __fix_to_virt(FIX_IDT);

	__asm__ __volatile__("lidt %0": "=m" (idt_descr));
}

void __init trap_init_virtual_GDT(void)
{
	int cpu = smp_processor_id();
	struct Xgt_desc_struct *gdt_desc = cpu_gdt_descr + cpu;
	struct Xgt_desc_struct tmp_desc = {0, 0};
	struct tss_struct * t;

	__asm__ __volatile__("sgdt %0": "=m" (tmp_desc): :"memory");

#if CONFIG_X86_HIGH_ENTRY
	if (!cpu) {
		__set_fixmap(FIX_GDT_0, __pa(cpu_gdt_table), PAGE_KERNEL);
		__set_fixmap(FIX_GDT_1, __pa(cpu_gdt_table) + PAGE_SIZE, PAGE_KERNEL);
		__set_fixmap(FIX_TSS_0, __pa(init_tss), PAGE_KERNEL);
		__set_fixmap(FIX_TSS_1, __pa(init_tss) + PAGE_SIZE, PAGE_KERNEL);
	}

	gdt_desc->address = __fix_to_virt(FIX_GDT_0) + sizeof(cpu_gdt_table[0]) * cpu;
#else
	gdt_desc->address = (unsigned long)cpu_gdt_table[cpu];
#endif
	__asm__ __volatile__("lgdt %0": "=m" (*gdt_desc));

#if CONFIG_X86_HIGH_ENTRY
	t = (struct tss_struct *) __fix_to_virt(FIX_TSS_0) + cpu;
#else
	t = init_tss + cpu;
#endif
	set_tss_desc(cpu, t);
	cpu_gdt_table[cpu][GDT_ENTRY_TSS].b &= 0xfffffdff;
	load_TR_desc();
}

#define _set_gate(gate_addr,type,dpl,addr,seg) \
do { \
  int __d0, __d1; \
  __asm__ __volatile__ ("movw %%dx,%%ax\n\t" \
	"movw %4,%%dx\n\t" \
	"movl %%eax,%0\n\t" \
	"movl %%edx,%1" \
	:"=m" (*((long *) (gate_addr))), \
	 "=m" (*(1+(long *) (gate_addr))), "=&a" (__d0), "=&d" (__d1) \
	:"i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	 "3" ((char *) (addr)),"2" ((seg) << 16)); \
} while (0)

/*
 * This needs to use 'idt_table' rather than 'idt', and
 * thus use the _nonmapped_ version of the IDT, as the
 * Pentium F0 0F bugfix can have resulted in the mapped
 * IDT being write-protected.
 */
void set_intr_gate(unsigned int n, void *addr)
{
	addr = ENTRY_TRAMP_ADDR(addr);
	_set_gate(idt_table+n,14,0,addr,__KERNEL_CS);
}

void __init set_trap_gate(unsigned int n, void *addr)
{
	addr = ENTRY_TRAMP_ADDR(addr);
	_set_gate(idt_table+n,15,0,addr,__KERNEL_CS);
}

void __init set_system_gate(unsigned int n, void *addr)
{
	addr = ENTRY_TRAMP_ADDR(addr);
	_set_gate(idt_table+n,15,3,addr,__KERNEL_CS);
}

#if 0
static void __init set_call_gate(void *a, void *addr)
{
	addr = ENTRY_TRAMP_ADDR(addr);
	_set_gate(a,12,3,addr);
}
#endif
  
static void __init set_task_gate(unsigned int n, unsigned int gdt_entry)
{
	_set_gate(idt_table+n,5,0,0,(gdt_entry<<3));
}

#ifdef CONFIG_X86_VISWS_APIC

/*
 * On Rev 005 motherboards legacy device interrupt lines are wired directly
 * to Lithium from the 307.  But the PROM leaves the interrupt type of each
 * 307 logical device set appropriate for the 8259.  Later we'll actually use
 * the 8259, but for now we have to flip the interrupt types to
 * level triggered, active lo as required by Lithium.
 */

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */

static void
superio_outb(int dev, int reg, int val)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	outb(val, VAL);
}

static int __attribute__ ((unused))
superio_inb(int dev, int reg)
{
	outb(DEV, REG);
	outb(dev, VAL);
	outb(reg, REG);
	return inb(VAL);
}

#define	FLOP	3	/* floppy logical device */
#define	PPORT	4	/* parallel logical device */
#define	UART5	5	/* uart2 logical device (not wired up) */
#define	UART6	6	/* uart1 logical device (THIS is the serial port!) */
#define	IDEST	0x70	/* int. destination (which 307 IRQ line) reg. */
#define	ITYPE	0x71	/* interrupt type register */

/* interrupt type bits */
#define	LEVEL	0x01	/* bit 0, 0 == edge triggered */
#define	ACTHI	0x02	/* bit 1, 0 == active lo */

static void
superio_init(void)
{
	if (visws_board_type == VISWS_320 && visws_board_rev == 5) {
		superio_outb(UART6, IDEST, 0);	/* 0 means no intr propagated */
		printk("SGI 320 rev 5: disabling 307 uart1 interrupt\n");
	}
}

static void
lithium_init(void)
{
	set_fixmap(FIX_LI_PCIA, LI_PCI_A_PHYS);
	printk("Lithium PCI Bridge A, Bus Number: %d\n",
				li_pcia_read16(LI_PCI_BUSNUM) & 0xff);
	set_fixmap(FIX_LI_PCIB, LI_PCI_B_PHYS);
	printk("Lithium PCI Bridge B (PIIX4), Bus Number: %d\n",
				li_pcib_read16(LI_PCI_BUSNUM) & 0xff);

	/* XXX blindly enables all interrupts */
	li_pcia_write16(LI_PCI_INTEN, 0xffff);
	li_pcib_write16(LI_PCI_INTEN, 0xffff);
}

static void
cobalt_init(void)
{
	/*
	 * On normal SMP PC this is used only with SMP, but we have to
	 * use it and set it up here to start the Cobalt clock
	 */
	set_fixmap(FIX_APIC_BASE, APIC_DEFAULT_PHYS_BASE);
	printk("Local APIC ID %lx\n", apic_read(APIC_ID));
	printk("Local APIC Version %lx\n", apic_read(APIC_LVR));

	set_fixmap(FIX_CO_CPU, CO_CPU_PHYS);
	printk("Cobalt Revision %lx\n", co_cpu_read(CO_CPU_REV));

	set_fixmap(FIX_CO_APIC, CO_APIC_PHYS);
	printk("Cobalt APIC ID %lx\n", co_apic_read(CO_APIC_ID));

	/* Enable Cobalt APIC being careful to NOT change the ID! */
	co_apic_write(CO_APIC_ID, co_apic_read(CO_APIC_ID)|CO_APIC_ENABLE);

	printk("Cobalt APIC enabled: ID reg %lx\n", co_apic_read(CO_APIC_ID));
}
#endif
void __init trap_init(void)
{
#ifdef CONFIG_EISA
	if (isa_readl(0x0FFFD9) == 'E'+('I'<<8)+('S'<<16)+('A'<<24))
		EISA_bus = 1;
#endif

#ifdef CONFIG_X86_LOCAL_APIC
	init_apic_mappings();
#endif
	init_entry_mappings();

	set_trap_gate(0,&divide_error);
	set_trap_gate(1,&debug);
	set_intr_gate(2,&nmi);
	set_system_gate(3,&int3);	/* int3-5 can be called from all */
	set_system_gate(4,&overflow);
	set_system_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_intr_gate(14,&page_fault);
	set_trap_gate(15,&spurious_interrupt_bug);
	set_trap_gate(16,&coprocessor_error);
	set_trap_gate(17,&alignment_check);
	set_trap_gate(18,&machine_check);
	set_trap_gate(19,&simd_coprocessor_error);

	set_system_gate(SYSCALL_VECTOR,&system_call);
	set_task_gate(8,GDT_ENTRY_DOUBLEFAULT_TSS);

	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();

#ifdef CONFIG_X86_VISWS_APIC
	superio_init();
	lithium_init();
	cobalt_init();
#endif
}

#if CONFIG_X86_LOCAL_APIC
EXPORT_SYMBOL_GPL(nmi_watchdog);
#endif

