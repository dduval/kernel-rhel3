#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/threads.h>
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#endif
#endif

#ifndef ASSEMBLY
extern u8 bios_cpu_apicid[];

/*
 * Some lowlevel functions might want to know about
 * the real APIC ID <-> CPU # mapping.
 */
extern u8 x86_cpu_to_log_apicid[NR_CPUS];
extern u8 x86_cpu_to_apicid[NR_CPUS];
#endif

#ifdef CONFIG_SMP
#ifndef ASSEMBLY

#include <asm/pda.h>


/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern unsigned long phys_cpu_present_map;
extern unsigned long cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern int smp_num_siblings;
extern int smp_num_cores;
extern int cpu_sibling_map[];
extern u8 phys_proc_id[];
extern u8 cpu_core_id[];

extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);

/* Newer 2.5 Linux kernels have cpu_possible. This provides equivalent function
   for the older 2.4 kernels */
static inline int cpu_possible(int cpu)
{
	return (cpu_online_map & (1<<(cpu)));
} 

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */
extern inline int cpu_logical_map(int cpu)
{
	return cpu;
}
extern inline int cpu_number_map(int cpu)
{
	return cpu;
}

static inline char x86_apicid_to_cpu(char apicid)
{
	int i;

	for (i = 0; i < NR_CPUS; ++i)
		if (x86_cpu_to_apicid[i] == apicid)
			return i;

	return -1;
}


static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < NR_CPUS)
		return (int)bios_cpu_apicid[mps_cpu];
	else
		return BAD_APICID;
}

/*
 * General functions that each host system must provide.
 */
 
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);		/* Store per CPU info (like the initial udelay numbers */

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */

#define smp_processor_id() read_pda(cpunumber)

#define stack_smp_processor_id() (stack_current()->cpu)


extern __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned *)(APIC_BASE+APIC_ID));
}

extern int slow_smp_processor_id(void);
#define safe_smp_processor_id() x86_apicid_to_cpu(hard_smp_processor_id())

#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

/*
 *	This magic constant controls our willingness to transfer
 *	a process across CPUs. Such a transfer incurs misses on the L1
 *	cache, and on a P6 or P5 with multiple L2 caches L2 hits. My
 *	gut feeling is this will vary by board in value. For a board
 *	with separate L2 cache it probably depends also on the RSS, and
 *	for a board with shared L2 cache it ought to decay fast as other
 *	processes are run.
 */
 
#define PROC_CHANGE_PENALTY	15		/* Schedule penalty */



#endif /* CONFIG_SMP */

#ifndef CONFIG_SMP
#define stack_smp_processor_id() 0
#define safe_smp_processor_id() 0
#endif
#endif
