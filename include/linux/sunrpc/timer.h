/*
 *  linux/include/linux/sunrpc/timer.h
 *
 *  Declarations for the RPC transport timer.
 *
 *  Copyright (C) 2002 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#ifndef _LINUX_SUNRPC_TIMER_H
#define _LINUX_SUNRPC_TIMER_H

#include <asm/atomic.h>

struct rpc_rtt {
	long timeo;		/* default timeout value */
	long srtt[5];		/* smoothed round trip time << 3 */
	long sdrtt[5];		/* soothed medium deviation of RTT */
#ifdef __GENKSYMS__ /* preserve KMI/ABI ksyms compatibility for mod linkage */
	atomic_t ntimeouts;	/* obsolete */
#else
	int  ntimeouts[5];	/* Number of timeouts for the last request */
#endif
};


extern void rpc_init_rtt(struct rpc_rtt *rt, long timeo);
extern void rpc_update_rtt(struct rpc_rtt *rt, int timer, long m);
extern long rpc_calc_rto(struct rpc_rtt *rt, int timer);

static inline void rpc_set_timeo(struct rpc_rtt *rt, int timer, int ntimeo)
{
	int *t;
	if (!timer)
		return;
	t = &rt->ntimeouts[timer-1];
	if (ntimeo < *t) {
		if (*t > 0)
			(*t)--;
	} else {
		if (ntimeo > 8)
			ntimeo = 8;
		*t = ntimeo;
	}
}

static inline int rpc_ntimeo(struct rpc_rtt *rt, int timer)
{
	if (!timer)
		return 0;
	return rt->ntimeouts[timer-1];
}

#endif /* _LINUX_SUNRPC_TIMER_H */
