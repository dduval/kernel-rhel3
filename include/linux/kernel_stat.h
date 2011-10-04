#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/config.h>
#include <asm/irq.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/process_timing.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

#define DK_MAX_MAJOR 16
#define DK_MAX_DISK 16

/* We only roll the accounted times into the kstat struct at each timer tick,
 * this way we can handle both processes with and without accounting enabled
 * on them.  We use our accumulated accounting time at each tick to subtract
 * from the number of usecs that *would* be handed out by the timer tick
 * accounting, that way we don't get accounting errors.
 */
struct kernel_stat_tick_times {
	unsigned long u_usec;		/* user time */
	unsigned long n_usec;		/* nice user time */
	unsigned long s_usec;		/* system time */
	unsigned long irq_usec;
	unsigned long softirq_usec;
	unsigned long iowait_usec;
	unsigned long idle_usec;
};

struct kernel_stat_percpu {
	struct kernel_timeval user, nice, system;
	struct kernel_timeval irq, softirq, iowait, idle;
	struct kernel_stat_tick_times accumulated_time;
	struct task_struct *unaccounted_task;
	unsigned int dk_drive[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_rio[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_wio[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_rblk[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int dk_drive_wblk[DK_MAX_MAJOR][DK_MAX_DISK];
	unsigned int pgpgin, pgpgout;
	unsigned int pswpin, pswpout;
#if defined (__hppa__) 
	unsigned int irqs[NR_IRQ_REGS][IRQ_PER_REGION];
#elif !defined(CONFIG_ARCH_S390)
	unsigned int irqs[NR_IRQS];
#endif
} ____cacheline_aligned;

extern struct kernel_stat_percpu kstat_percpu[NR_CPUS] ____cacheline_aligned;

#define kstat_sum(field)						\
({									\
	unsigned int __cpu, sum = 0;					\
									\
	for (__cpu = 0 ; __cpu < smp_num_cpus ; __cpu++)		\
		sum += kstat_percpu[cpu_logical_map(__cpu)].field;	\
									\
	sum;								\
})

extern unsigned long nr_context_switches(void);

#if defined (__hppa__) 
/*
 * Number of interrupts per specific IRQ source, since bootup
 */
static inline int kstat_irqs (int irq)
{

	int i, sum=0; 

	for (i = 0 ; i < smp_num_cpus ; i++)
		sum += kstat_percpu[i].irqs[IRQ_REGION(irq)][IRQ_OFFSET(irq)];
 
	return sum;
}
#elif !defined(CONFIG_ARCH_S390)
/*
 * Number of interrupts per specific IRQ source, since bootup
 */
# define kstat_irqs(irq) kstat_sum(irqs[irq])
#endif

#endif /* _LINUX_KERNEL_STAT_H */
