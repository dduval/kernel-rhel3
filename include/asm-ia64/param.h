#ifndef _ASM_IA64_PARAM_H
#define _ASM_IA64_PARAM_H

/*
 * Fundamental kernel parameters.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#ifdef CONFIG_IA64_HP_SIM
/*
 * Yeah, simulating stuff is slow, so let us catch some breath between
 * timer interrupts...
 */
# define HZ	  32
#else
# define HZ	1024
#endif

#define hz_to_std(a) (a)

#define EXEC_PAGESIZE	65536

#ifndef NGROUPS
# define NGROUPS	32
#endif

#ifndef NOGROUP
# define NOGROUP	(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#ifdef __KERNEL__
# define CLOCKS_PER_SEC	HZ	/* frequency at which times() counts */
#endif

#endif /* _ASM_IA64_PARAM_H */
