#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@redhat.com>
 */

#include <linux/config.h>
#include <linux/kernel.h>  /* For BUG_ON(). */
#include <asm/page.h>  /* Just for BUG(): ugh. */

#ifdef CONFIG_SMP

#include <linux/kernel.h>
#include <asm/smp.h>

/*
 * main cross-CPU interfaces, handles INIT, TLB flush, STOP, etc.
 * (defined in asm header):
 */ 

/*
 * stops all CPUs but the current one:
 */
extern void smp_send_stop(void);

/*
 * sends a 'reschedule' event to another CPU:
 */
extern void FASTCALL(smp_send_reschedule(int cpu));


/*
 * Boot processor call to load the other CPU's
 */
extern void smp_boot_cpus(void);

/*
 * Processor call in. Must hold processors until ..
 */
extern void smp_callin(void);

/*
 * Multiprocessors may now schedule
 */
extern void smp_commence(void);

/*
 * Call a function on all other processors
 */
extern int smp_call_function (void (*func) (void *info), void *info,
			      int retry, int wait);
extern void dump_smp_call_function (void (*func) (void *info), void *info);

/*
 * Call a function on all processors
 */
static inline int on_each_cpu(void (*func) (void *info), void *info,
			      int retry, int wait)
{
	int ret = 0;

#ifdef CONFIG_PREEMPT
	preempt_disable();
#endif
	ret = smp_call_function(func, info, retry, wait);
	func(info);
#ifdef CONFIG_PREEMPT
	preempt_enable();
#endif
	return ret;
}

/*
 * True once the per process idle is forked
 */
extern int smp_threads_ready;

extern int smp_num_cpus;

extern volatile unsigned long smp_msg_data;
extern volatile int smp_src_cpu;
extern volatile int smp_msg_id;

#define MSG_ALL_BUT_SELF	0x8000	/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001	/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002	/* Sent to shut down slave CPU's
					 * when rebooting
					 */
#define MSG_RESCHEDULE		0x0003	/* Reschedule request from master CPU*/
#define MSG_CALL_FUNCTION       0x0004  /* Call function on all other CPUs */

#else

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
 
#define smp_num_cpus				1
#define smp_processor_id()			0
#define hard_smp_processor_id()			0
#define smp_threads_ready			1
#define kernel_lock()
#define cpu_logical_map(cpu)			0
#define cpu_number_map(cpu)			0
#define cpu_online(cpu)				({ BUG_ON((cpu) != 0); 1; })
#define smp_call_function(func,info,retry,wait)	({ 0; })
#define cpu_online_map				1
#define cpu_possible(cpu)			({ BUG_ON((cpu) != 0); 1; })
#define on_each_cpu(func,info,retry,wait)	({ func(info); 0; })
static inline void smp_send_reschedule(int cpu) { }
static inline void smp_send_reschedule_all(void) { }
#endif

/*
 * Common definitions:
 */
#define cpu()					smp_processor_id()
#define get_cpu()				smp_processor_id()
#define put_cpu()				do { } while (0)

#endif
