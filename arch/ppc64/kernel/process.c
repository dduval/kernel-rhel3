/*
 *  linux/arch/ppc64/kernel/process.c
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995  Linus Torvalds
 *
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu) and
 *  Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/prom.h>
#include <asm/ppcdebug.h>
#include <asm/machdep.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/cputable.h>

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs);

struct task_struct *last_task_used_math = NULL;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

struct mm_struct ioremap_mm = { pgd             : ioremap_dir  
                               ,page_table_lock : SPIN_LOCK_UNLOCKED };

/* this is 16-byte aligned because it has a stack in it */
union task_union __attribute((aligned(16))) init_task_union = {
	INIT_TASK(init_task_union.task)
};

#ifdef CONFIG_SMP
struct current_set_struct current_set[NR_CPUS] = {{&init_task, 0}, };
#endif

char *sysmap = NULL; 
unsigned long sysmap_size = 0;

extern char __toc_start;

#undef SHOW_TASK_SWITCHES

void
enable_kernel_fp(void)
{
#ifdef CONFIG_SMP
	if (current->thread.regs && (current->thread.regs->msr & MSR_FP))
		giveup_fpu(current);
	else
		giveup_fpu(NULL);	/* just enables FP for kernel */
#else
	giveup_fpu(last_task_used_math);
#endif /* CONFIG_SMP */
}

int
dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs)
{
	if (regs && regs->msr & MSR_FP)
		giveup_fpu(current);
	memcpy(fpregs, &current->thread.fpr[0], sizeof(*fpregs));
	return 1;
}

struct task_struct *__switch_to(struct task_struct *prev, struct task_struct *new)
{
	struct thread_struct *new_thread, *old_thread;
	unsigned long s;
	struct task_struct *last;
	
	__save_flags(s);
	__cli();

#ifdef SHOW_TASK_SWITCHES
	printk("%s/%d -> %s/%d NIP %08lx cpu %d root %x/%x\n",
	       prev->comm,prev->pid,
	       new->comm,new->pid,new->thread.regs->nip,new->processor,
	       new->fs->root,prev->fs->root);
#endif
#ifdef CONFIG_SMP
	/* avoid complexity of lazy save/restore of fpu
	 * by just saving it every time we switch out if
	 * this task used the fpu during the last quantum.
	 * 
	 * If it tries to use the fpu again, it'll trap and
	 * reload its fp regs.  So we don't have to do a restore
	 * every switch, just a save.
	 *  -- Cort
	 */
	if ( prev->thread.regs && (prev->thread.regs->msr & MSR_FP) )
		giveup_fpu(prev);

	/* prev->last_processor = prev->processor; */
	current_set[smp_processor_id()].task = new;
#endif /* CONFIG_SMP */
	new_thread = &new->thread;
	old_thread = &current->thread;
	last = _switch(old_thread, new_thread);
	__restore_flags(s);
	return last;
}

/*
 * If the address is either in the .text section of the
 * kernel, or in the vmalloc'ed module regions, it *may* 
 * be the address of a calling routine
 */

#ifdef CONFIG_MODULES

extern struct module *module_list;
extern struct module kernel_module;
extern char _stext[], _etext[];

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


void show_regs(struct pt_regs * regs)
{
	int i;
	static char buffer[512];

	print_modules();
	printk("NIP: %016lx XER: %016lx LR: %016lx REGS: %p TRAP: %04lx    %s\n",
	       regs->nip, regs->xer, regs->link, regs,regs->trap, print_tainted());
	lookup_symbol(regs->nip, buffer, 512);
	printk("NIP is at %s (" UTS_RELEASE ")\n", buffer);
	printk("MSR: %016lx EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	printk("TASK = %p[%d] '%s' ",
	       current, current->pid, current->comm);
	printk("Last syscall: %ld\n", current->thread.last_syscall);
	printk("last math %p ", last_task_used_math);
	
#ifdef CONFIG_SMP
	printk("CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */
	
	for (i = 0;  i < 32;  i++)
	{
		long r;
		if ((i % 4) == 0)
			printk("\nGPR%02d: ", i);

		if ( __get_user(r, &(regs->gpr[i])) )
		    return;

		printk("%016lx ", r);
	}
	printk("\n");
	print_backtrace((unsigned long *)regs->gpr[1]);
}

void exit_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void flush_thread(void)
{
	if (last_task_used_math == current)
		last_task_used_math = NULL;
}

void
release_thread(struct task_struct *t)
{
}

/*
 * Copy a thread..
 */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused,
	    struct task_struct * p, struct pt_regs * regs)
{
	unsigned long msr;
	struct pt_regs * childregs, *kregs;
	extern void ret_from_fork(void);

	p->set_child_tid = p->clear_child_tid = NULL;

	/* Copy registers */
	childregs = ((struct pt_regs *)
		     ((unsigned long)p + sizeof(union task_union)
		      - STACK_FRAME_OVERHEAD)) - 2;
	*childregs = *regs;
	childregs->gpr[3] = 0;  /* Result from fork() */
	p->thread.regs = childregs;
	p->thread.ksp = (unsigned long) childregs - STACK_FRAME_OVERHEAD;
	p->thread.ksp -= sizeof(struct pt_regs ) + STACK_FRAME_OVERHEAD;
	kregs = (struct pt_regs *)(p->thread.ksp + STACK_FRAME_OVERHEAD);
	/* The PPC64 compiler makes use of a TOC to contain function 
	 * pointers.  The function (ret_from_except) is actually a pointer
	 * to the TOC entry.  The first entry is a pointer to the actual
	 * function.
 	 */
	kregs->nip = *((unsigned long *)ret_from_fork);
	asm volatile("mfmsr %0" : "=r" (msr):);
	kregs->msr = msr;
	kregs->gpr[1] = (unsigned long)childregs - STACK_FRAME_OVERHEAD;
	kregs->gpr[2] = (((unsigned long)&__toc_start) + 0x8000);
	
	if (usp >= (unsigned long) regs) {
		/* Stack is in kernel space - must adjust */
		childregs->gpr[1] = (unsigned long)(childregs + 1);
		*((unsigned long *) childregs->gpr[1]) = 0;
		childregs->gpr[13] = (unsigned long) p;
	} else {
		/* Provided stack is in user space */
		childregs->gpr[1] = usp;
		if (clone_flags & CLONE_SETTLS) {
			if (current->thread.flags & PPC_FLAG_32BIT)
				childregs->gpr[2] = childregs->gpr[6];
			else
				childregs->gpr[13] = childregs->gpr[6];
		}
	}
	p->thread.last_syscall = -1;
	  
	if ((childregs->msr & MSR_PR) == 0) {
		/* no user register state for kernel thread */
		p->thread.regs = NULL;
		/* 
		 * Turn off the 32bit flag, make sure the iSeries
		 * run light flag is on (no effect on pSeries).
		 * Those are the only two flags.
		 */
		p->thread.flags = PPC_FLAG_RUN_LIGHT;
	}
	/*
	 * copy fpu info - assume lazy fpu switch now always
	 *  -- Cort
	 */
	if (regs->msr & MSR_FP) {
		giveup_fpu(current);
		childregs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1);
	}
	memcpy(&p->thread.fpr, &current->thread.fpr, sizeof(p->thread.fpr));
	p->thread.fpscr = current->thread.fpscr;
	p->thread.fpexc_mode = current->thread.fpexc_mode;

	return 0;
}

void show_stack(unsigned long *esp)
{
	register unsigned long *sp asm("r1");
	if (esp == NULL)
		esp = sp;
	print_backtrace(esp);
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long fdptr, unsigned long sp)
{
	unsigned long entry, toc, load_addr = regs->gpr[2];

	/* fdptr is a relocated pointer to the function descriptor for
         * the elf _start routine.  The first entry in the function
         * descriptor is the entry address of _start and the second
         * entry is the TOC value we need to use.
         */
	set_fs(USER_DS);
	__get_user(entry, (unsigned long *)fdptr);
	__get_user(toc, (unsigned long *)fdptr+1);

	/* Check whether the e_entry function descriptor entries
	 * need to be relocated before we can use them.
	 */
	if ( load_addr != 0 ) {
		entry += load_addr;
		toc   += load_addr;
	}

	regs->nip = entry;
	regs->gpr[1] = sp;
	regs->gpr[2] = toc;
	regs->msr = MSR_USER64;
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->thread.fpscr = 0;
}

# define PR_FP_EXC_DISABLED     0       /* FP exceptions disabled */
# define PR_FP_EXC_NONRECOV     1       /* async non-recoverable exc. mode */
# define PR_FP_EXC_ASYNC        2       /* async recoverable exception mode */
# define PR_FP_EXC_PRECISE      3       /* precise exception mode */

int set_fpexc_mode(struct task_struct *tsk, unsigned int val)
{
	struct pt_regs *regs = tsk->thread.regs;

	if (val > PR_FP_EXC_PRECISE)
		return -EINVAL;
	tsk->thread.fpexc_mode = __pack_fe01(val);
	if (regs != NULL && (regs->msr & MSR_FP) != 0)
		regs->msr = (regs->msr & ~(MSR_FE0|MSR_FE1))
			| tsk->thread.fpexc_mode;
	return 0;
}

int get_fpexc_mode(struct task_struct *tsk, unsigned long adr)
{
	unsigned int val;

	val = __unpack_fe01(tsk->thread.fpexc_mode);
	return put_user(val, (unsigned int *) adr);
}

int sys_clone(unsigned long clone_flags, unsigned long p2, unsigned long p3,
              unsigned long p4, unsigned long p5, unsigned long p6,
              struct pt_regs *regs)
{
        unsigned long parent_tidptr = 0;
        unsigned long child_tidptr = 0;

        if (p2 == 0)
                p2 = regs->gpr[1];      /* stack pointer for child */

        if (clone_flags & (CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
                           CLONE_CHILD_CLEARTID)) {
                parent_tidptr = p3;
                child_tidptr = p5;
		if (current->thread.flags & PPC_FLAG_32BIT) {
                        parent_tidptr &= 0xffffffff;
                        child_tidptr &= 0xffffffff;
                }
        }

        if (regs->msr & MSR_FP)
                giveup_fpu(current);

        return do_fork(clone_flags & ~CLONE_IDLETASK, p2, regs, 0,
                    (int *)parent_tidptr, (int *)child_tidptr);
}

int sys_fork(int p1, int p2, int p3, int p4, int p5, int p6,
	     struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->gpr[1], regs, 0, NULL, NULL);
}

int sys_vfork(int p1, int p2, int p3, int p4, int p5, int p6,
			 struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gpr[1], regs, 0, NULL, NULL);
}

int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	       unsigned long a3, unsigned long a4, unsigned long a5,
	       struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
  
	error = do_execve(filename, (char **) a1, (char **) a2, regs);
  
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
}

struct task_struct * alloc_task_struct(void)
{
	struct task_struct * new_task_ptr;
    
	new_task_ptr = ((struct task_struct *) 
			__get_free_pages(GFP_KERNEL, get_order(THREAD_SIZE)));
    
	return new_task_ptr;
}

void free_task_struct(struct task_struct * task_ptr)
{
	free_pages((unsigned long)(task_ptr), get_order(THREAD_SIZE));
}

void initialize_paca_hardware_interrupt_stack(void)
{
	extern struct systemcfg *systemcfg;

	int i;
	unsigned long stack;
	unsigned long end_of_stack =0;

	for (i=1; i < systemcfg->processorCount; i++) {
		/* Carve out storage for the hardware interrupt stack */
		stack = __get_free_pages(GFP_KERNEL, get_order(8*PAGE_SIZE));

		if ( !stack ) {     
			printk("ERROR, cannot find space for hardware stack.\n");
			panic(" no hardware stack ");
		}


		/* Store the stack value in the PACA for the processor */
		paca[i].xHrdIntStack = stack + (8*PAGE_SIZE) - STACK_FRAME_OVERHEAD;
		paca[i].xHrdIntCount = 0;

	}

	/*
	 * __get_free_pages() might give us a page > KERNBASE+256M which
	 * is mapped with large ptes so we can't set up the guard page.
	 */
	if (cur_cpu_spec->cpu_features & CPU_FTR_16M_PAGE)
		return;

	for (i=0; i < systemcfg->processorCount; i++) {
		/* set page at the top of stack to be protected - prevent overflow */
		end_of_stack = paca[i].xHrdIntStack - (8*PAGE_SIZE - STACK_FRAME_OVERHEAD);
		ppc_md.hpte_updateboltedpp(PP_RXRX,end_of_stack);
	}
}

void
print_backtrace(unsigned long *sp)
{
	int cnt = 0;
	unsigned long i;
	char buffer[512];

	printk("Call Trace: \n");
	while (sp) {
		if (__get_user(i, &sp[2]))
                	break;
		if (kernel_text_address(i)) {
			if (__get_user(sp, (unsigned long **)sp))
				break;
			lookup_symbol(i, buffer, 512);
			printk("[<%016lx>] %s\n", i, buffer);
		}
		if (cnt++ > 32) break;
	}
	printk("\n");
}

#define PPC64_RET_FROM_SYCALL		".ret_from_syscall"
#define PPC64_DO_PAGE_FAULT		".do_page_fault"
#define SYMBUF_LEN			256

#define WCHAN_BAD_SP(sp, taskaddr)					\
	(unsigned long)(sp) < ((taskaddr) + (2 * PAGE_SIZE)) ||		\
	(unsigned long)(sp) >= ((taskaddr) + THREAD_SIZE)

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ip = 0L;
        unsigned long prev_ip;
	unsigned long *sp;
	unsigned long stack_page = (unsigned long)p;
	int count = 0;
	char buffer[SYMBUF_LEN];
	int len;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;
	/*
	 * Dereference the stack pointer saved in p->thread.ksp twice
	 * to get past stack frame for _switch() which does not save
	 * the link register.
	 */
	sp = (unsigned long *)p->thread.ksp;
	if (WCHAN_BAD_SP(sp, stack_page) || __get_user(sp, sp))
		return 0;
	if (WCHAN_BAD_SP(sp, stack_page) || __get_user(sp, sp))
		return 0;
	do {
		if (WCHAN_BAD_SP(sp, stack_page))
			break;
		prev_ip = ip;
		if (__get_user(ip, &sp[2]))
			break;
		len = lookup_symbol(ip, buffer, SYMBUF_LEN);
		if (len > 0) {
			if (!strncmp(buffer, PPC64_RET_FROM_SYCALL,
				     strlen(PPC64_RET_FROM_SYCALL)))
				return prev_ip;
			if (!strncmp(buffer, PPC64_DO_PAGE_FAULT,
				     strlen(PPC64_DO_PAGE_FAULT)))
				return ip;
		}
		if (__get_user(sp, sp))
			break;
	} while (++count < 16);
	return 0;
}

void show_trace_task(struct task_struct *p)
{
	unsigned long ip, sp;
	unsigned long stack_page = (unsigned long)p;
	int count = 0;
	static char buffer[512];

	if (!p)
		return;

	printk("Call Trace: ");
	sp = p->thread.ksp;
	do {
		sp = *(unsigned long *)sp;
		if (sp < (stack_page + (2 * PAGE_SIZE)) ||
		    sp >= (stack_page + THREAD_SIZE))
			break;
		if (count > 0) {
			ip = *(unsigned long *)(sp + 16);
			lookup_symbol(ip, buffer, 512);
			printk("[<%016lx>] %s\n", ip, buffer);
		}
	} while (count++ < 16);
	printk("\n");
}
