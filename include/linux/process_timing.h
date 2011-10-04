/*
 *  Process Timing 
 *
 *  Author: Norm Murray (nmurray@redhat.com)
 *
 *  This header file contains the definitions needed to implement
 *  absolute, clock based, process timestamp accounting rather than the
 *  old style statistical time accounting. 
 *  
 *  Copyright (C) 2003 Red Hat Software
 *
 */

#ifndef _LINUX_PROCESS_TIMING_H
#define _LINUX_PROCESS_TIMING_H

struct kernel_timeval {
	unsigned int	tv_sec;
	unsigned int	tv_usec;
};

/*
 * The process timing code treats the values in timeval as unsigned long.
 * We also normalize the usec field each time we add usecs, so we don't have
 * to worry about it overflowing.  On the second field we don't worry about
 * overflow either because it's just a wrap and will only happen after over
 * 4 billion seconds of CPU time have been accounted to the process.  So,
 * since we don't really do timeval math on anything but internal structures,
 * we use these simple timeval functions.  If you need to deal with user
 * supplied timeval structs like in itimer.c, then you will need to use more
 * robust timeval functions.
 */
static inline void kernel_timeval_add_usec(struct kernel_timeval *time,
                                           unsigned long usecs)
{
        time->tv_usec += usecs;
        /* This loop is faster than a divide/mod sequence for typical cases */
        while(time->tv_usec >= 1000000) {
                time->tv_sec++;
                time->tv_usec -= 1000000;
        }
}
        
static inline void kernel_timeval_addto(struct kernel_timeval *dest,
					struct kernel_timeval *add)
{
        dest->tv_sec += add->tv_sec;    
        kernel_timeval_add_usec(dest, add->tv_usec);
}
                
static inline void kernel_timeval_add(struct kernel_timeval *sum,
				      struct kernel_timeval *time1,
                                      struct kernel_timeval *time2)
{       
        sum->tv_sec = time1->tv_sec + time2->tv_sec;
        sum->tv_usec = time1->tv_usec + time2->tv_usec;
        while(sum->tv_usec >= 1000000) {
                sum->tv_sec++;
                sum->tv_usec -= 1000000;
        }
}

/* Used in almost all internal calculations where we want the time spent on
 * this process in jiffie increments */
static inline unsigned long kernel_timeval_to_jiffies(struct kernel_timeval *time)
{
        return ((unsigned long)time->tv_sec * HZ + (time->tv_usec / (1000000/HZ)));
}       

/* Used in the implementation of the times() syscall, returns user time and
 * sys time with a normalized HZ of CLOCKS_PER_SEC */
static inline unsigned long kernel_timeval_to_clock_t(struct kernel_timeval *time)
{
        return ((unsigned long)time->tv_sec * CLOCKS_PER_SEC + (time->tv_usec / (1000000/CLOCKS_PER_SEC)));
}




typedef enum {
	PROCESS_TIMING_USER,
	PROCESS_TIMING_SYSTEM,
	PROCESS_TIMING_IRQ,
	PROCESS_TIMING_SOFTIRQ
} process_timing_time_type;

/* Need the definition of union process_timing_state_method from the asm header */
#include <asm/process_timing.h>

/*
 * We could have used a struct list_head in here, but we don't need full list
 * functions, only head and tail of our interrupt state stack.  So, we do it
 * by hand instead for speed/ease.
 */
struct process_timing_state {
	process_timing_time_type		type;
	union process_timing_state_method	method;
};

/*
 * enabled, sys_enter, and sys_exit have hardcoded offsets in
 * arch/i386/kernel/entry.S.  Don't move them around.
 */
struct process_timing_entry {
    	int    flags;
	void (*sys_enter)(void);
	void (*sys_exit)(void);
	void (*task_switch)(struct task_struct *prev, struct task_struct *next);
	process_timing_time_type (*intr_enter)(process_timing_time_type now);
	void (*intr_exit)(process_timing_time_type now,
			  process_timing_time_type prev);
	void (*tick)(void);
};

extern struct process_timing_entry process_timing;
extern int process_timing_setup_flags;

/*
 * These are the various flags present in pa_flags that control how the system
 * uses accurate accounting code.
 */
#define PT_FLAGS_IRQ		0x00000001	/* Account around interrupts */
#define PT_FLAGS_SOFTIRQ	0x00000002	/* Account around softirqs */
#define PT_FLAGS_PROCESS	0x00000004	/* Enabled process accounting */
#define PT_FLAGS_ALL_PROCESS	0x00000008	/* Account all processes */
#define PT_FLAGS_EVERYTHING	0x0000000f	/* Everything on */

#endif	/* _LINUX_PROCESS_TIMING_H */
