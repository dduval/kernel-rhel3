/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *	(c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/mach_apic.h>

/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP errata are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP errata are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP errata are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesnt have the work
 *	arounds. Basically thats so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

/* The 'big kernel lock' */
spinlock_cacheline_t kernel_flag_cacheline = {SPIN_LOCK_UNLOCKED};

struct tlb_state cpu_tlbstate[NR_CPUS] __cacheline_aligned = {[0 ... NR_CPUS-1] = { &init_mm, 0, }};

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 */

static volatile unsigned long flush_cpumask;
static struct mm_struct * flush_mm;
static unsigned long flush_va;
static spinlock_t tlbstate_lock = SPIN_LOCK_UNLOCKED;
#define FLUSH_ALL	0xffffffff

/*
 * We cannot call mmdrop() because we are in interrupt context, 
 * instead update mm->cpu_vm_mask.
 */
static void inline leave_mm (unsigned long cpu)
{
	if (cpu_tlbstate[cpu].state == TLBSTATE_OK)
		BUG();
	clear_bit(cpu, &cpu_tlbstate[cpu].active_mm->cpu_vm_mask);
	*read_pda(level4_pgt) = 0;
	/* flush TLB before it goes away. this stops speculative prefetches */
	__flush_tlb(); 
}

/*
 *
 * The flush IPI assumes that a thread switch happens in this order:
 * [cpu0: the cpu that switches]
 * 1) switch_mm() either 1a) or 1b)
 * 1a) thread switch to a different mm
 * 1a1) clear_bit(cpu, &old_mm->cpu_vm_mask);
 * 	Stop ipi delivery for the old mm. This is not synchronized with
 * 	the other cpus, but smp_invalidate_interrupt ignore flush ipis
 * 	for the wrong mm, and in the worst case we perform a superflous
 * 	tlb flush.
 * 1a2) set cpu_tlbstate to TLBSTATE_OK
 * 	Now the smp_invalidate_interrupt won't call leave_mm if cpu0
 *	was in lazy tlb mode.
 * 1a3) update cpu_tlbstate[].active_mm
 * 	Now cpu0 accepts tlb flushes for the new mm.
 * 1a4) set_bit(cpu, &new_mm->cpu_vm_mask);
 * 	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1b) thread switch without mm change
 *	cpu_tlbstate[].active_mm is correct, cpu0 already handles
 *	flush ipis.
 * 1b1) set cpu_tlbstate to TLBSTATE_OK
 * 1b2) test_and_set the cpu bit in cpu_vm_mask.
 * 	Atomically set the bit [other cpus will start sending flush ipis],
 * 	and test the bit.
 * 1b3) if the bit was 0: leave_mm was called, flush the tlb.
 * 2) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu_tlbstate is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush IPI:
 *
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 */

asmlinkage void smp_invalidate_interrupt (void)
{
	unsigned long cpu = smp_processor_id();

	if (!test_bit(cpu, &flush_cpumask))
		return;
		/* 
		 * This was a BUG() but until someone can quote me the
		 * line from the intel manual that guarantees an IPI to
		 * multiple CPUs is retried _only_ on the erroring CPUs
		 * its staying as a return
		 *
		 * BUG();
		 */
		 
	if (flush_mm == cpu_tlbstate[cpu].active_mm) {
		if (cpu_tlbstate[cpu].state == TLBSTATE_OK) {
			if (flush_va == FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	ack_APIC_irq();
	clear_bit(cpu, &flush_cpumask);
}

static void flush_tlb_others (unsigned long cpumask, struct mm_struct *mm,
						unsigned long va)
{
	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - we do not send IPIs to not-yet booted CPUs.
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	if (!cpumask)
		BUG();
	if ((cpumask & cpu_online_map) != cpumask)
		BUG();
	if (cpumask & (1 << smp_processor_id()))
		BUG();
	if (!mm)
		BUG();

	/*
	 * i'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * Temporarily this turns IRQs off, so that lockups are
	 * detected by the NMI watchdog.
	 */
	spin_lock(&tlbstate_lock);
	
	flush_mm = mm;
	flush_va = va;
	atomic_set_mask(cpumask, &flush_cpumask);
	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_VECTOR);

	while (flush_cpumask)
		/* nothing. lockup detection does not belong here */;

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}
	
void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	local_flush_tlb();
	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
}

void flush_tlb_mm (struct mm_struct * mm)
{
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
}

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long cpu_mask = mm->cpu_vm_mask & ~(1 << smp_processor_id());

	if (current->active_mm == mm) {
		if(current->mm)
			__flush_tlb_one(va);
		 else
		 	leave_mm(smp_processor_id());
	}

	if (cpu_mask)
		flush_tlb_others(cpu_mask, mm, va);
}

static inline void do_flush_tlb_all_local(void)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (cpu_tlbstate[cpu].state == TLBSTATE_LAZY)
		leave_mm(cpu);
}

static void flush_tlb_all_ipi(void* info)
{
	do_flush_tlb_all_local();
}

void flush_tlb_all(void)
{
	smp_call_function (flush_tlb_all_ipi,0,1,1);

	do_flush_tlb_all_local();
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_mask(1 << cpu, RESCHEDULE_VECTOR);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t dump_call_lock = SPIN_LOCK_UNLOCKED;

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
};

static struct call_data_struct * call_data;
static struct call_data_struct * saved_call_data;

/*
 * dump version of smp_call_function to avoid deadlock in call_lock
 */
void dump_smp_call_function (void (*func) (void *info), void *info)
{
	static struct call_data_struct dumpdata;
	static int dumping_cpu = -1;
	int waitcount;

	spin_lock(&dump_call_lock);
	/*
	 * The cpu that reaches here first will do dumping.  Only the dumping
	 * cpu skips the if-statement below ONLY ONCE.  The other cpus freeze
	 * themselves here.
	 */
	if (dumpdata.func) {
		spin_unlock(&dump_call_lock);
		/*
		 * The dumping cpu reaches here in case that the netdump starts
		 * after the diskdump fails.  In the case, the dumping cpu
		 * needs to return to continue the netdump.  In other cases,
		 * freezes itself by calling func().
		 */
		if (dumping_cpu == smp_processor_id())
			return;

		func(info);
		for (;;);
		/* NOTREACHED */
	}

	dumping_cpu = smp_processor_id();

	/* freeze call_lock or wait for on-going IPIs to settle down */
	waitcount = 0;
	while (!spin_trylock(&call_lock)) {
		if (waitcount++ > 1000) {
			/* save original for dump analysis */
			saved_call_data = call_data;
			break;
		}
		udelay(1000);
		barrier();
	}

	dumpdata.func = func;
	dumpdata.info = info;
	dumpdata.wait = 0; /* not used */
	atomic_set(&dumpdata.started, 0); /* not used */
	atomic_set(&dumpdata.finished, 0); /* not used */

	call_data = &dumpdata;
	wmb();
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);
	/* Don't wait */
	spin_unlock(&dump_call_lock);
}

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
/*
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has complete on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
{
	struct call_data_struct data;
	int cpus = smp_num_cpus-1;

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	spin_unlock(&call_lock);

	return 0;
}

static void stop_this_cpu (void * dummy)
{
	/*
	 * Remove this CPU:
	 */
	clear_bit(smp_processor_id(), &cpu_online_map);
	__cli();
	disable_local_APIC();
	for(;;) __asm__("hlt");
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;

	__cli();
	disable_local_APIC();
	__sti();
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

asmlinkage void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	ack_APIC_irq();
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	mb();
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
}

/* Slow. Should be only used for debugging. */
int slow_smp_processor_id(void)
{ 
	int stack_location;
	unsigned long sp = (unsigned long)&stack_location; 
	int offset = 0, cpu;

	for (offset = 0; (cpu_online_map >> offset); offset = cpu + 1) { 
		cpu = ffz(~(cpu_online_map >> offset));

		if (sp >= (u64)cpu_pda[cpu].irqstackptr - IRQSTACKSIZE && 
		    sp <= (u64)cpu_pda[cpu].irqstackptr)
			return cpu;

		unsigned long estack = init_tss[cpu].ist[0] - EXCEPTION_STKSZ;
		if (sp >= estack && sp <= estack+(1<<(PAGE_SHIFT+EXCEPTION_STK_ORDER)))
			return cpu;			
	}

	return stack_smp_processor_id();
} 

