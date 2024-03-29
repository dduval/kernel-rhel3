/*
 *  arch/ppc/kernel/irq.c
 *
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/random.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/iSeries/LparData.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/perfmon.h>

#include "local_irq.h"

atomic_t ipi_recv;
atomic_t ipi_sent;
void enable_irq(unsigned int irq_nr);
void disable_irq(unsigned int irq_nr);

#ifdef CONFIG_SMP
extern void iSeries_smp_message_recv( struct pt_regs * );
#endif

volatile unsigned char *chrp_int_ack_special;
static void register_irq_proc (unsigned int irq);

irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned =
	{ [0 ... NR_IRQS-1] = { 0, NULL, NULL, 0, SPIN_LOCK_UNLOCKED}};
	
int ppc_spurious_interrupts = 0;
unsigned long lpEvent_count = 0;
#ifdef CONFIG_XMON
extern void xmon(struct pt_regs *regs);
extern int xmon_bpt(struct pt_regs *regs);
extern int xmon_sstep(struct pt_regs *regs);
extern int xmon_iabr_match(struct pt_regs *regs);
extern int xmon_dabr_match(struct pt_regs *regs);
extern void (*xmon_fault_handler)(struct pt_regs *regs);
#endif
#ifdef CONFIG_XMON
extern void (*debugger)(struct pt_regs *regs);
extern int (*debugger_bpt)(struct pt_regs *regs);
extern int (*debugger_sstep)(struct pt_regs *regs);
extern int (*debugger_iabr_match)(struct pt_regs *regs);
extern int (*debugger_dabr_match)(struct pt_regs *regs);
extern void (*debugger_fault_handler)(struct pt_regs *regs);
#endif

/* nasty hack for shared irq's since we need to do kmalloc calls but
 * can't very early in the boot when we need to do a request irq.
 * this needs to be removed.
 * -- Cort
 */
#define IRQ_KMALLOC_ENTRIES 16
static int cache_bitmask = 0;
static struct irqaction malloc_cache[IRQ_KMALLOC_ENTRIES];
extern int mem_init_done;

void *irq_kmalloc(size_t size, int pri)
{
	unsigned int i;
	if ( mem_init_done )
		return kmalloc(size,pri);
	for ( i = 0; i < IRQ_KMALLOC_ENTRIES ; i++ )
		if ( ! ( cache_bitmask & (1<<i) ) ) {
			cache_bitmask |= (1<<i);
			return (void *)(&malloc_cache[i]);
		}
	return 0;
}

void irq_kfree(void *ptr)
{
	unsigned int i;
	for ( i = 0 ; i < IRQ_KMALLOC_ENTRIES ; i++ )
		if ( ptr == &malloc_cache[i] ) {
			cache_bitmask &= ~(1<<i);
			return;
		}
	kfree(ptr);
}

int
setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	unsigned long flags;
	struct irqaction *old, **p;
	irq_desc_t *desc = irq_desc + irq;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/* Call the controller's startup function for this irq */
	if (desc->handler && desc->handler->startup)
		desc->handler->startup(irq);

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&desc->lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		desc->depth = 0;
		desc->status &= ~(IRQ_DISABLED | IRQ_AUTODETECT | IRQ_WAITING);
		unmask_irq(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	register_irq_proc(irq);
	return 0;
}

/* This could be promoted to a real free_irq() ... */
static int
do_free_irq(int irq, void* dev_id)
{
	irq_desc_t *desc;
	struct irqaction **p;
	unsigned long flags;

	desc = irq_desc + irq;
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	for (;;) {
		struct irqaction * action = *p;
		if (action) {
			struct irqaction **pp = p;
			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found it - now remove it from the list of entries */
			*pp = action->next;
			if (!desc->action) {
				desc->status |= IRQ_DISABLED;
				mask_irq(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);

#ifdef CONFIG_SMP
			/* Wait to make sure it's not being used on another CPU */
			while (desc->status & IRQ_INPROGRESS)
				barrier();
#endif
			irq_kfree(action);
			return 0;
		}
		printk("Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		break;
	}
	return -ENOENT;
}

int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction *action;
	int retval;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		/* We could implement really free_irq() instead of that... */
		return do_free_irq(irq, dev_id);
	
	action = (struct irqaction *)
		irq_kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action) {
		printk(KERN_ERR "irq_kmalloc() failed for irq %d !\n", irq);
		return -ENOMEM;
	}
	
	action->handler = handler;
	action->flags = irqflags;					
	action->mask = 0;
	action->name = devname;
	action->dev_id = dev_id;
	action->next = NULL;
	
	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
		
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	request_irq(irq, NULL, 0, NULL, dev_id);
}

/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock. 
 */
 
/**
 *	disable_irq_nosync - disable an irq without waiting
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line. Disables of an interrupt
 *	stack. Unlike disable_irq(), this function does not ensure existing
 *	instances of the IRQ handler have completed before returning.
 *
 *	This function may be called from IRQ context.
 */
 
 void disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		if (!(desc->status & IRQ_PER_CPU))
			desc->status |= IRQ_DISABLED;
		mask_irq(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

/**
 *	disable_irq - disable an irq and wait for completion
 *	@irq: Interrupt to disable
 *
 *	Disable the selected interrupt line. Disables of an interrupt
 *	stack. That is for two disables you need two enables. This
 *	function waits for any pending IRQ handlers for this interrupt
 *	to complete before returning. If you use this function while
 *	holding a resource the IRQ handler may need you will deadlock.
 *
 *	This function may be called - with care - from IRQ context.
 */
 
void disable_irq(unsigned int irq)
{
	disable_irq_nosync(irq);

	if (!local_irq_count(smp_processor_id())) {
		do {
			barrier();
		} while (irq_desc[irq].status & IRQ_INPROGRESS);
	}
}

/**
 *	enable_irq - enable interrupt handling on an irq
 *	@irq: Interrupt to enable
 *
 *	Re-enables the processing of interrupts on this IRQ line
 *	providing no disable_irq calls are now in effect.
 *
 *	This function may be called from IRQ context.
 */
 
void enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;
		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		unmask_irq(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
		break;
	case 0:
		printk("enable_irq(%u) unbalanced\n", irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i, j;
	struct irqaction * action;
	unsigned long flags;

	seq_printf(p, "           ");
	for (j=0; j<smp_num_cpus; j++)
		seq_printf(p, "CPU%d       ",j);
	seq_putc(p, '\n');

	for (i = 0 ; i < NR_IRQS ; i++) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action || !action->handler)
			goto skip;
		seq_printf(p, "%3d: ", i);		
#ifdef CONFIG_SMP
		for (j = 0; j < smp_num_cpus; j++)
			seq_printf(p, "%10u ",
				kstat_percpu[cpu_logical_map(j)].irqs[i]);
#else		
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (irq_desc[i].handler)		
			seq_printf(p, " %s ", irq_desc[i].handler->typename );
		else
			seq_printf(p, "  None      ");
		seq_printf(p, "%s", (irq_desc[i].status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s",action->name);
		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
#ifdef CONFIG_SMP
	/* should this be per processor send/receive? */
	seq_printf(p, "IPI (recv/sent): %10u/%u\n",
		       atomic_read(&ipi_recv), atomic_read(&ipi_sent));
#endif
	seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	return 0;
}

static inline void
handle_irq_event(int irq, struct pt_regs *regs, struct irqaction *action)
{
	int status = 0;

	if (!(action->flags & SA_INTERRUPT))
		__sti();

	do {
		status |= action->flags;
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);
	if (status & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	__cli();
}

/*
 * Eventually, this should take an array of interrupts and an array size
 * so it can dispatch multiple interrupts.
 */
void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq)
{
	int status;
	struct irqaction *action;
	int cpu = smp_processor_id();
	irq_desc_t *desc = irq_desc + irq;

	kstat_percpu[cpu].irqs[irq]++;
	spin_lock(&desc->lock);
	ack_irq(irq);	
	/*
	   REPLAY is when Linux resends an IRQ that was dropped earlier
	   WAITING is used by probe to mark irqs that are being tested
	   */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	if (!(status & IRQ_PER_CPU))
		status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		if (!action || !action->handler) {
			ppc_spurious_interrupts++;
			printk(KERN_DEBUG "Unhandled interrupt %x, disabled\n", irq);
			/* We can't call disable_irq here, it would deadlock */
			if (!desc->depth)
				desc->depth = 1;
			desc->status |= IRQ_DISABLED;
			/* This is not a real spurrious interrupt, we
			 * have to eoi it, so we jump to out
			 */
			mask_irq(irq);
			goto out;
		}
		status &= ~IRQ_PENDING; /* we commit to handling */
		if (!(status & IRQ_PER_CPU))
			status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	   Since we set PENDING, if another processor is handling
	   a different instance of this same irq, the other processor
	   will take care of it.
	 */
	if (!action)
		goto out;


	/*
	 * Edge triggered interrupts need to remember
	 * pending events.
	 * This applies to any hw interrupts that allow a second
	 * instance of the same irq to arrive while we are in do_IRQ
	 * or in the handler. But the code here only handles the _second_
	 * instance of the irq, not the third or fourth. So it is mostly
	 * useful for irq hardware that does not mask cleanly in an
	 * SMP environment.
	 */
	for (;;) {
		spin_unlock(&desc->lock);
		handle_irq_event(irq, regs, action);
		spin_lock(&desc->lock);
		
		if (!(desc->status & IRQ_PENDING))
			break;
		desc->status &= ~IRQ_PENDING;
	}
	desc->status &= ~IRQ_INPROGRESS;
out:
	/*
	 * The ->end() handler has to deal with interrupts which got
	 * disabled while the handler was running.
	 */
	if (irq_desc[irq].handler) {
		if (irq_desc[irq].handler->end)
			irq_desc[irq].handler->end(irq);
		else if (irq_desc[irq].handler->enable)
			irq_desc[irq].handler->enable(irq);
	}
	spin_unlock(&desc->lock);
}

int do_IRQ(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int irq, first = 1;
#ifdef CONFIG_PPC_ISERIES
	struct paca_struct *lpaca;
	struct ItLpQueue *lpq;
#endif

	irq_enter(cpu);

#ifdef CONFIG_PPC_ISERIES
	lpaca = get_paca();
#ifdef CONFIG_SMP
	if (lpaca->xLpPaca.xIntDword.xFields.xIpiCnt) {
		lpaca->xLpPaca.xIntDword.xFields.xIpiCnt = 0;
		iSeries_smp_message_recv(regs);
	}
#endif /* CONFIG_SMP */
	lpq = lpaca->lpQueuePtr;
	if (lpq && ItLpQueue_isLpIntPending(lpq))
		lpEvent_count += ItLpQueue_process(lpq, regs);
#else
	/*
	 * Every arch is required to implement ppc_md.get_irq.
	 * This function will either return an irq number or -1 to
	 * indicate there are no more pending.  But the first time
	 * through the loop this means there wasn't an IRQ pending.
	 * The value -2 is for buggy hardware and means that this IRQ
	 * has already been handled. -- Tom
	 */
	while ((irq = ppc_md.get_irq(regs)) >= 0) {
		ppc_irq_dispatch_handler(regs, irq);
		first = 0;
	}
	if (irq != -2 && first)
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;
#endif

        irq_exit(cpu);

#ifdef CONFIG_PPC_ISERIES
	if (lpaca->xLpPaca.xIntDword.xFields.xDecrInt) {
		lpaca->xLpPaca.xIntDword.xFields.xDecrInt = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt(regs);
	}

	if (lpaca->xLpPaca.xIntDword.xFields.xPdcInt) {
		lpaca->xLpPaca.xIntDword.xFields.xPdcInt = 0;
		/* Signal a fake PMC interrupt */
		PerformanceMonitorException();
	}
#endif

	if (softirq_pending(cpu))
		do_softirq();

	return 1; /* lets ret_from_int know we can do checks */
}

unsigned long probe_irq_on (void)
{
	return 0;
}

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

unsigned int probe_irq_mask(unsigned long irqs)
{
	return 0;
}

void __init init_IRQ(void)
{
	static int once = 0;

	if ( once )
		return;
	else
		once++;
	
	ppc_md.init_IRQ();
}

#ifdef CONFIG_SMP
unsigned char global_irq_holder = NO_PROC_ID;

static void show(char * str)
{
	int cpu = smp_processor_id();
	int i;

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [ ", irqs_running());
	for (i = 0; i < smp_num_cpus; i++)
		printk("%u ", __brlock_array[i][BR_GLOBALIRQ_LOCK]);
	printk("]\nbh:   %d [ ",
		(spin_is_locked(&global_bh_lock) ? 1 : 0));
	for (i = 0; i < smp_num_cpus; i++)
		printk("%u ", local_bh_count(i));
	printk("]\n");
}

#define MAXCOUNT 10000000

void synchronize_irq(void)
{
	if (irqs_running()) {
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu)
{
        int count;

        if ((unsigned char)cpu == global_irq_holder)
                return;

        count = MAXCOUNT;
again:
        br_write_lock(BR_GLOBALIRQ_LOCK);
        for (;;) {
                spinlock_t *lock;

                if (!irqs_running() &&
                    (local_bh_count(smp_processor_id()) || !spin_is_locked(&global_bh_lock)))
                        break;

                br_write_unlock(BR_GLOBALIRQ_LOCK);
                lock = &__br_write_locks[BR_GLOBALIRQ_LOCK].lock;
                while (irqs_running() ||
                       spin_is_locked(lock) ||
                       (!local_bh_count(smp_processor_id()) && spin_is_locked(&global_bh_lock))) {
                        if (!--count) {
                                show("get_irqlock");
                                count = (~0 >> 1);
                        }
                        __sti();
                        barrier();
                        __cli();
                }
                goto again;
        }

        global_irq_holder = cpu;
}

/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 *
 * If we already have local interrupts disabled,
 * this will not turn a local disable into a
 * global one (problems with spinlocks: this makes
 * save_flags+cli+sti usable inside a spinlock).
 */
void __global_cli(void)
{
	unsigned long flags;
	
	__save_flags(flags);
	if (flags & (1UL << 15)) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count(cpu))
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count(cpu))
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long __global_save_flags(void)
{
	int retval;
	int local_enabled;
	unsigned long flags;

	__save_flags(flags);
	local_enabled = (flags >> 15) & 1;
	/* default to local */
	retval = 2 + local_enabled;

	/* check for global flags if we're not in an interrupt */
	if (!local_irq_count(smp_processor_id())) {
		if (local_enabled)
			retval = 1;
		if (global_irq_holder == (unsigned char) smp_processor_id())
			retval = 0;
	}
	return retval;
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
	case 0:
		__global_cli();
		break;
	case 1:
		__global_sti();
		break;
	case 2:
		__cli();
		break;
	case 3:
		__sti();
		break;
	default:
		printk("global_restore_flags: %016lx caller %p\n",
			flags, __builtin_return_address(0));
	}
}

#endif /* CONFIG_SMP */

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir [NR_IRQS];
static struct proc_dir_entry * smp_affinity_entry [NR_IRQS];

#ifdef CONFIG_IRQ_ALL_CPUS
unsigned int irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = 0xffffffff};
#else  /* CONFIG_IRQ_ALL_CPUS */
unsigned int irq_affinity [NR_IRQS] = { [0 ... NR_IRQS-1] = 0x00000000};
#endif /* CONFIG_IRQ_ALL_CPUS */

#define HEX_DIGITS 8

static int irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	if (count < HEX_DIGITS+1)
		return -EINVAL;
	return sprintf (page, "%08x\n", irq_affinity[(int)(long)data]);
}

static unsigned int parse_hex_value (const char *buffer,
		unsigned long count, unsigned long *ret)
{
	unsigned char hexnum [HEX_DIGITS];
	unsigned long value;
	int i;

	if (!count)
		return -EINVAL;
	if (count > HEX_DIGITS)
		count = HEX_DIGITS;
	if (copy_from_user(hexnum, buffer, count))
		return -EFAULT;

	/*
	 * Parse the first 8 characters as a hex string, any non-hex char
	 * is end-of-string. '00e1', 'e1', '00E1', 'E1' are all the same.
	 */
	value = 0;

	for (i = 0; i < count; i++) {
		unsigned int c = hexnum[i];

		switch (c) {
			case '0' ... '9': c -= '0'; break;
			case 'a' ... 'f': c -= 'a'-10; break;
			case 'A' ... 'F': c -= 'A'-10; break;
		default:
			goto out;
		}
		value = (value << 4) | c;
	}
out:
	*ret = value;
	return 0;
}

static int irq_affinity_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	int irq = (int)(long) data, full_count = count, err;
	unsigned long new_value;

	if (!irq_desc[irq].handler->set_affinity)
		return -EIO;

	err = parse_hex_value(buffer, count, &new_value);

/* Why is this disabled ? --BenH */
#if 0/*CONFIG_SMP*/
	/*
	 * Do not allow disabling IRQs completely - it's a too easy
	 * way to make the system unusable accidentally :-) At least
	 * one online CPU still has to be targeted.
	 */
	if (!(new_value & cpu_online_map))
		return -EINVAL;
#endif

	irq_affinity[irq] = new_value;
	irq_desc[irq].handler->set_affinity(irq, new_value);

	return full_count;
}

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	unsigned long *mask = (unsigned long *) data;
	if (count < HEX_DIGITS+1)
		return -EINVAL;
	return sprintf (page, "%08lx\n", *mask);
}

static int prof_cpu_mask_write_proc (struct file *file, const char *buffer,
					unsigned long count, void *data)
{
	unsigned long *mask = (unsigned long *) data, full_count = count, err;
	unsigned long new_value;

	err = parse_hex_value(buffer, count, &new_value);
	if (err)
		return err;

	*mask = new_value;

#ifdef CONFIG_PPC_ISERIES
	{
		unsigned i;
		for (i=0; i<MAX_PACAS; ++i) {
			if ( paca[i].prof_buffer && (new_value & 1) )
				paca[i].prof_mode = PMC_STATE_DECR_PROFILE;
			else {
				if(paca[i].prof_mode != PMC_STATE_INITIAL) 
					paca[i].prof_mode = PMC_STATE_READY;
			}
			new_value >>= 1;
		}
	}
#endif

	return full_count;
}

#define MAX_NAMELEN 10

static void register_irq_proc (unsigned int irq)
{
	struct proc_dir_entry *entry;
	char name [MAX_NAMELEN];

	if (!root_irq_dir || (irq_desc[irq].handler == NULL) || irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);
	if (irq_dir[irq] == NULL) {
		printk(KERN_ERR "register_irq_proc: proc_mkdir failed.\n");
		return;
	}

	/* create /proc/irq/1234/smp_affinity */
	entry = create_proc_entry("smp_affinity", 0600, irq_dir[irq]);

	if (!entry) {
		printk(KERN_ERR "register_irq_proc: create_proc_entry failed.\n");
		return;
	}
	entry->nlink = 1;
	entry->data = (void *)(long)irq;
	entry->read_proc = irq_affinity_read_proc;
	entry->write_proc = irq_affinity_write_proc;

	smp_affinity_entry[irq] = entry;
}

unsigned long prof_cpu_mask = -1;

void init_irq_proc (void)
{
	struct proc_dir_entry *entry;
	int i;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", 0);
	if (root_irq_dir == NULL)
		printk(KERN_ERR "init_irq_proc: proc_mkdir failed.\n");

	/* create /proc/irq/prof_cpu_mask */
	entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir);

	if (!entry) {
		printk(KERN_ERR "init_irq_proc: create_proc_entry failed.\n");
		return;
	}
	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;
}

void no_action(int irq, void *dev, struct pt_regs *regs)
{
}

/*
 * Virtual IRQ mapping code, used on systems with XICS interrupt controllers.
 */

#define UNDEFINED_IRQ 0xffffffffU
unsigned int virt_irq_to_real_map[NR_IRQS];

/*
 * Don't use virtual irqs 0, 1, 2 for devices.
 * The pcnet32 driver considers interrupt numbers < 2 to be invalid,
 * and 2 is the XICS IPI interrupt.
 * We limit virtual irqs to 17 less than NR_IRQS so that when we
 * offset them by 16 (to reserve the first 16 for ISA interrupts)
 * we don't end up with an interrupt number >= NR_IRQS.
 */
#define MIN_VIRT_IRQ	3
#define MAX_VIRT_IRQ	(NR_IRQS - NUM_ISA_INTERRUPTS - 1)
#define NR_VIRT_IRQS	(MAX_VIRT_IRQ - MIN_VIRT_IRQ + 1)

void
virt_irq_init(void)
{
	int i;
	for (i = 0; i < NR_IRQS; i++)
		virt_irq_to_real_map[i] = UNDEFINED_IRQ;
}

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq)
{
	unsigned int virq, first_virq;
	static int warned;

	if (naca->interrupt_controller == IC_OPEN_PIC)
		return real_irq;	/* no mapping for openpic (for now) */

	/* don't map interrupts < MIN_VIRT_IRQ */
	if (real_irq < MIN_VIRT_IRQ) {
		virt_irq_to_real_map[real_irq] = real_irq;
		return real_irq;
	}

	/* map to a number between MIN_VIRT_IRQ and MAX_VIRT_IRQ */
	virq = real_irq;
	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	/* search for this number or a free slot */
	first_virq = virq;
	while (virt_irq_to_real_map[virq] != UNDEFINED_IRQ) {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;
		if (++virq > MAX_VIRT_IRQ)
			virq = MIN_VIRT_IRQ;
		if (virq == first_virq)
			goto nospace;	/* oops, no free slots */
	}

	virt_irq_to_real_map[virq] = real_irq;
	return virq;

 nospace:
	if (!warned) {
		printk(KERN_CRIT "Interrupt table is full\n");
		printk(KERN_CRIT "Increase NR_IRQS (currently %d) "
		       "in your kernel sources and rebuild.\n", NR_IRQS);
		warned = 1;
	}
	return NO_IRQ;
}

/*
 * In most cases we will get a hit on the very first slot checked in
 * the virt_irq_to_real_map.  Only when there are a large number of
 * IRQs will this be expensive.
 */
int real_irq_to_virt_slow(unsigned int real_irq)
{
	unsigned int virq;
	unsigned int first_virq;

	virq = real_irq;

	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	first_virq = virq;
	do {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;
		virq++;
		if (virq >= MAX_VIRT_IRQ)
			virq = 0;
	} while (first_virq != virq);

	return NO_IRQ;
}

