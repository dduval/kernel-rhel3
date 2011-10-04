/*
 *  Process Accounting
 *
 *  Author: Doug Ledford <dledford@redhat.com>
 *
 *  This file includes the x86 arch dependant setup routines and the various
 *  accounting method functions.
 *
 *  Copyright (C) 2003 Red Hat Software
 *
 */

#include <linux/config.h>
#include <linux/sched.h> /* for struct task_struct */
#include <linux/time.h> /* for struct timeval and timeval functions */
#include <linux/irq.h> /* for local_irq_save/local_irq_restore */
#include <linux/kernel_stat.h> /* for kernel_stat_tick_times */
#include <asm/msr.h> /* for rdtscl() */
#include <linux/process_timing.h>
#include <asm/processor.h> /* for cpu capability info */

/* These are in arch/i386/kernel/time.c and are setup during time_init */
extern int use_tsc;
extern unsigned long fast_gettimeoffset_quotient;

static void tsc_sysenter(void);
static void tsc_sysexit(void);
static void tsc_task_switch(task_t *prev, task_t *next);
static process_timing_time_type tsc_intr_enter(process_timing_time_type);
static void tsc_intr_exit(process_timing_time_type, process_timing_time_type);
static void tsc_tick(void);

static struct tsc_state_cpu tsc_state[NR_CPUS] __cacheline_aligned;

static inline unsigned long tsc_to_usecs(unsigned long cycles)
{
	register unsigned long eax = cycles, edx;
	__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"rm" (fast_gettimeoffset_quotient),
		 "0" (eax));
	return edx;
}

void __init process_timing_init(void)
{
	int i;
	task_t *p;
	
	printk("Process timing init...");
	if (process_timing_setup_flags && use_tsc) {
		printk("tsc timing selected...");
		for (i=0; i < smp_num_cpus; i++) {
			memset(&tsc_state[cpu_logical_map(i)], 0,
			       sizeof(struct tsc_state_cpu));
			p = cpu_idle_ptr(i);
			kstat_percpu[cpu_logical_map(i)].unaccounted_task = p;
			if (process_timing_setup_flags & PT_FLAGS_ALL_PROCESS)
				p->flags |= PF_TIMING;
			memset(&p->timing_state, 0, sizeof(p->timing_state));
			p->timing_state.type = PROCESS_TIMING_SYSTEM;
			rdtscl(p->timing_state.method.tsc.timestamp);
		}
		if (process_timing_setup_flags & PT_FLAGS_ALL_PROCESS) {
			current->flags |= PF_TIMING;
			current->timing_state.type = PROCESS_TIMING_USER;
			rdtscl(current->timing_state.method.tsc.timestamp);
		}
		process_timing.sys_enter = tsc_sysenter;
		process_timing.sys_exit = tsc_sysexit;
		process_timing.task_switch = tsc_task_switch;
		process_timing.intr_enter = tsc_intr_enter;
		process_timing.intr_exit = tsc_intr_exit;
		process_timing.tick = tsc_tick;
		process_timing.flags = process_timing_setup_flags;
	}
	printk("done.\n");
}

asmlinkage static void tsc_sysenter(void)
{
	task_t *p = current;
	unsigned long flags, timestamp, delta;

	local_irq_save(flags);
	rdtscl(timestamp);
	delta = timestamp - p->timing_state.method.tsc.timestamp;
	p->timing_state.method.tsc.timestamp = timestamp;
	if (task_nice(p) > 0)
		p->timing_state.method.tsc.n_cycles += delta;
	else
		p->timing_state.method.tsc.u_cycles += delta;
	p->timing_state.type = PROCESS_TIMING_SYSTEM;
	local_irq_restore(flags);
}

asmlinkage static void tsc_sysexit(void)
{
	task_t *p = current;
	unsigned long flags, timestamp, delta;

	local_irq_save(flags);
	rdtscl(timestamp);
	delta = timestamp - p->timing_state.method.tsc.timestamp;
	p->timing_state.method.tsc.timestamp = timestamp;
	p->timing_state.method.tsc.s_cycles += delta;
	p->timing_state.type = PROCESS_TIMING_USER;
	local_irq_restore(flags);
}

static void tsc_task_switch(task_t *prev, task_t *next)
{
	unsigned long flags, timestamp, delta;
	struct kernel_stat_tick_times time;
	int cpu = smp_processor_id();

	if (!tsc_state[cpu].in_intr) {
		rdtscl(timestamp);
		next->timing_state.method.tsc.timestamp = timestamp;

		if (!(prev->flags & PF_TIMING)) {
			kstat_percpu[cpu].unaccounted_task = prev;
			return;
		} else {
			delta = timestamp -
				prev->timing_state.method.tsc.timestamp;
			if (prev->timing_state.type == PROCESS_TIMING_SYSTEM)
				prev->timing_state.method.tsc.s_cycles += delta;
			else if (task_nice(prev) > 0)
				prev->timing_state.method.tsc.n_cycles += delta;
			else
				prev->timing_state.method.tsc.u_cycles += delta;
		}
	}

	memset(&time, 0, sizeof(time));
	time.u_usec = tsc_to_usecs(prev->timing_state.method.tsc.u_cycles);
	time.n_usec = tsc_to_usecs(prev->timing_state.method.tsc.n_cycles);
	time.s_usec = tsc_to_usecs(prev->timing_state.method.tsc.s_cycles);
	prev->timing_state.method.tsc.u_cycles = 0;
	prev->timing_state.method.tsc.n_cycles = 0;
	prev->timing_state.method.tsc.s_cycles = 0;
	update_process_time_intertick(prev, &time);
}

static process_timing_time_type tsc_intr_enter(process_timing_time_type type)
{
	unsigned long flags, timestamp, delta;
	int cpu = smp_processor_id();
	process_timing_time_type old_type;

	local_irq_save(flags);
	tsc_state[cpu].in_intr++;
	if (type == tsc_state[cpu].intr_type) {
		local_irq_restore(flags);
		return type;
	}
	rdtscl(timestamp);
	if (tsc_state[cpu].in_intr == 1) {
		task_t *p = current;
		if (p->flags & PF_TIMING) {
			delta = timestamp - p->timing_state.method.tsc.timestamp;
			if (p->timing_state.type == PROCESS_TIMING_SYSTEM)
				p->timing_state.method.tsc.s_cycles += delta;
			else if (task_nice(p) > 0)
				p->timing_state.method.tsc.n_cycles += delta;
			else
				p->timing_state.method.tsc.u_cycles += delta;
		}
		old_type = p->timing_state.type;
	} else {
		delta = timestamp - tsc_state[cpu].timestamp;
		switch(tsc_state[cpu].intr_type) {
			default:
			case PROCESS_TIMING_IRQ:
				tsc_state[cpu].irq_cycles += delta;
				break;
			case PROCESS_TIMING_SOFTIRQ:
				tsc_state[cpu].softirq_cycles += delta;
				break;
		}
		old_type = tsc_state[cpu].intr_type;
	}
	tsc_state[cpu].timestamp = timestamp;
	tsc_state[cpu].intr_type = type;
	local_irq_restore(flags);
	return old_type;
}

static void tsc_intr_exit(process_timing_time_type type,
			  process_timing_time_type prev_type)
{
	unsigned long flags, timestamp, delta;
	int cpu = smp_processor_id();

	local_irq_save(flags);
	tsc_state[cpu].in_intr--;
	if (type == prev_type) {
		local_irq_restore(flags);
		return;
	}
	rdtscl(timestamp);
	delta = timestamp - tsc_state[cpu].timestamp;
	switch(type) {
		default:
		case PROCESS_TIMING_IRQ:
			tsc_state[cpu].irq_cycles += delta;
			break;
		case PROCESS_TIMING_SOFTIRQ:
			tsc_state[cpu].softirq_cycles += delta;
			break;
	}
	if (tsc_state[cpu].in_intr) {
		tsc_state[cpu].timestamp = timestamp;
		tsc_state[cpu].intr_type = prev_type;
	} else {
		task_t *p = current;
		p->timing_state.method.tsc.timestamp = timestamp;
		tsc_state[cpu].intr_type = PROCESS_TIMING_USER;
	}
	local_irq_restore(flags);
}

/* Ticks are a bit special.  We want to close out the currently running
 * process times and update the process.  If the system is all accounted,
 * not mixed, then we can just let the intr stack be and use the timestamp
 * in current->timing_state.last->method.tsc.tsc_low for all the calculations.
 * But, if the system is in mixed mode, then we *have* to close out the
 * entire intr stack and reset all of the intr states to use the timestamp
 * from right now so that the "remainder" of unaccounted time won't get off
 * in the update_process_times_mixed() calculations.
 */

static void tsc_tick(void)
{
	unsigned long flags, timestamp, delta;
	task_t *p = current;
	struct kernel_stat_tick_times time;
	int cpu = smp_processor_id();

	local_irq_save(flags);
	if (tsc_state[cpu].in_intr) {
		rdtscl(timestamp);
		delta = timestamp - tsc_state[cpu].timestamp;
		tsc_state[cpu].timestamp = timestamp;
		switch (tsc_state[cpu].intr_type) {
			default:
			case PROCESS_TIMING_IRQ:
				tsc_state[cpu].irq_cycles += delta;
				break;
			case PROCESS_TIMING_SOFTIRQ:
				tsc_state[cpu].softirq_cycles += delta;
				break;
		}
	} else if (p->flags & PF_TIMING) {
		rdtscl(timestamp);
		delta = timestamp - p->timing_state.method.tsc.timestamp;
		p->timing_state.method.tsc.timestamp = timestamp;
		switch (p->timing_state.type) {
			default:
			case PROCESS_TIMING_USER:
				if (task_nice(p) > 0)
					p->timing_state.method.tsc.n_cycles +=
						delta;
				else
					p->timing_state.method.tsc.u_cycles +=
						delta;
				break;
			case PROCESS_TIMING_SYSTEM:
				p->timing_state.method.tsc.s_cycles += delta;
				break;
		}
	}
	memset(&time, 0, sizeof(time));
	time.u_usec = tsc_to_usecs(p->timing_state.method.tsc.u_cycles);
	time.n_usec = tsc_to_usecs(p->timing_state.method.tsc.n_cycles);
	time.s_usec = tsc_to_usecs(p->timing_state.method.tsc.s_cycles);
	time.irq_usec = tsc_to_usecs(tsc_state[cpu].irq_cycles);
	time.softirq_usec = tsc_to_usecs(tsc_state[cpu].softirq_cycles);
	p->timing_state.method.tsc.u_cycles = 0;
	p->timing_state.method.tsc.n_cycles = 0;
	p->timing_state.method.tsc.s_cycles = 0;
	tsc_state[cpu].irq_cycles = 0;
	tsc_state[cpu].softirq_cycles = 0;
	update_process_time_intertick(p, &time);
	local_irq_restore(flags);
}


