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

union process_timing_state_method {
	int dummy;
};

#define process_timing_init()	do { } while (0)

#endif	/* _LINUX_PROCESS_TIMING_ASM_H */
