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

#ifndef _LINUX_PROCESS_TIMING_ASM_H
#define _LINUX_PROCESS_TIMING_ASM_H

extern void process_timing_init(void);

struct tsc_state {
	unsigned long timestamp;
	unsigned long u_cycles;
	unsigned long n_cycles;
	unsigned long s_cycles;
};

struct tsc_state_cpu {
	unsigned long timestamp;
	unsigned long softirq_cycles;
	unsigned long irq_cycles;
	int in_intr;
	process_timing_time_type intr_type;
} ____cacheline_aligned;

union process_timing_state_method {
	struct tsc_state tsc;
};


#endif	/* _LINUX_PROCESS_TIMING_ASM_H */
