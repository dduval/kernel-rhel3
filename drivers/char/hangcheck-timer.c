/*
 * hangcheck-timer.c
 *
 * Driver for a little io fencing timer.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
 *
 * Author: Joel Becker <joel.becker@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

/*
 * The hangcheck-timer driver uses the TSC to catch delays that
 * jiffies does not notice.  A timer is set.  When the timer fires, it
 * checks whether it was delayed and if that delay exceeds a given
 * margin of error.  The hangcheck_tick module paramter takes the timer
 * duration in seconds.  The hangcheck_margin parameter defines the
 * margin of error, in seconds.  The defaults are 60 seconds for the
 * timer and 180 seconds for the margin of error.  IOW, a timer is set
 * for 60 seconds.  When the timer fires, the callback checks the
 * actual duration that the timer waited.  If the duration exceeds the
 * alloted time and margin (here 60 + 180, or 240 seconds), the machine
 * is restarted.  A healthy machine will have the duration match the
 * expected timeout very closely.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/sysrq.h>

#define VERSION_STR "0.8.0"

#define DEFAULT_IOFENCE_MARGIN 60	/* Default fudge factor, in seconds */
#define DEFAULT_IOFENCE_TICK 180	/* Default timer timeout, in seconds */

static int hangcheck_tick = DEFAULT_IOFENCE_TICK;
static int hangcheck_margin = DEFAULT_IOFENCE_MARGIN;
static int hangcheck_reboot = 1;  /* Reboot */
static int hangcheck_dump_tasks = 0;  /* SysRQ T */

/* options - modular */
MODULE_PARM(hangcheck_tick,"i");
MODULE_PARM_DESC(hangcheck_tick, "Timer delay.");
MODULE_PARM(hangcheck_margin,"i");
MODULE_PARM_DESC(hangcheck_margin, "If the hangcheck timer has been delayed more than hangcheck_margin seconds, the driver will fire.");
MODULE_PARM(hangcheck_reboot,"i");
MODULE_PARM_DESC(hangcheck_reboot, "If nonzero, the machine will reboot when the timer margin is exceeded.");
MODULE_PARM(hangcheck_dump_tasks,"i");
MODULE_PARM_DESC(hangcheck_dump_tasks, "If nonzero, the machine will dump the system task state when the timer margin is exceeded.");

MODULE_DESCRIPTION("Hangcheck-timer detects when the system has gone out to lunch past a certain margin.");
MODULE_AUTHOR("Joel Becker");
MODULE_LICENSE("GPL");

/* options - nonmodular */
#ifndef MODULE

static int __init hangcheck_parse_tick(char *str) {
	int par;
	if (get_option(&str,&par))
		hangcheck_tick = par;
	return 1;
}

static int __init hangcheck_parse_margin(char *str) {
	int par;
	if (get_option(&str,&par))
		hangcheck_margin = par;
	return 1;
}

static int __init hangcheck_parse_reboot(char *str) {
	int par;
	if (get_option(&str,&par))
		hangcheck_reboot = par;
	return 1;
}

static int __init hangcheck_parse_dump_tasks(char *str) {
       int par;
       if (get_option(&str,&par))
               hangcheck_dump_tasks = par;
       return 1;
}

__setup("hcheck_tick", hangcheck_parse_tick);
__setup("hcheck_margin", hangcheck_parse_margin);
__setup("hcheck_reboot", hangcheck_parse_reboot);
__setup("hcheck_dump_margin", hangcheck_parse_dump_tasks);
#endif /* not MODULE */

static int use_cyclone = 0;
#define COUNTER_MASK (use_cyclone ? CYCLONE_TIMER_MASK : ~0ULL)
#ifdef __ia64__
# define TIMER_FREQ ((unsigned long long)local_cpu_data->itc_freq)
#elif defined(__powerpc64__)
# define TIMER_FREQ (HZ*loops_per_jiffy)
#else
# define TIMER_FREQ (use_cyclone ? CYCLONE_TIMER_FREQ : HZ*(unsigned long long)current_cpu_data.loops_per_jiffy)
#endif  /* __ia64__ */

#ifdef HAVE_MONOTONIC
extern unsigned long long monotonic_clock(void);
#else
# ifdef __i386__
#  include "hangcheck-cyclone.h"
# else
#  define CYCLONE_TIMER_MASK (~0ULL)
#  define CYCLONE_TIMER_FREQ (0)
# endif  /* __i386__ */
static inline unsigned long long monotonic_clock(void)
{
# ifdef __i386__
	if(use_cyclone)
		return get_cyclone();
# endif  /* __i386__ */
	return get_cycles();
}
#endif  /* HAVE_MONOTONIC */


/* Last time scheduled */
static unsigned long long hangcheck_tsc, hangcheck_tsc_margin;

static void hangcheck_fire(unsigned long);

static struct timer_list hangcheck_ticktock = {
	function:	hangcheck_fire,
};


static void hangcheck_fire(unsigned long data)
{
	unsigned long long cur_tsc, tsc_diff;

	cur_tsc = monotonic_clock();

	tsc_diff = (cur_tsc - hangcheck_tsc)&COUNTER_MASK;

	if (tsc_diff > hangcheck_tsc_margin) {
		if (hangcheck_dump_tasks) {
			printk(KERN_CRIT "Hangcheck: Task state:\n");
			handle_sysrq('t', NULL, NULL, NULL);
		}
		if (hangcheck_reboot) {
			printk(KERN_CRIT "Hangcheck: hangcheck is restarting the machine.\n");
			machine_restart(NULL);
		} else {
			printk(KERN_CRIT "Hangcheck: hangcheck value past margin!\n");
		}
	}
	mod_timer(&hangcheck_ticktock, jiffies + (hangcheck_tick*HZ));
	hangcheck_tsc = monotonic_clock();
}  /* hangcheck_fire() */


static int __init hangcheck_init(void)
{
	printk("Hangcheck: starting hangcheck timer %s (tick is %d seconds, margin is %d seconds).\n",
	       VERSION_STR, hangcheck_tick, hangcheck_margin);
#if !defined(HAVE_MONOTONIC) && defined(__i386__)
	if(!init_cyclone()){
		use_cyclone = 1;
		printk("Hangcheck: Using cyclone counter.\n");
	}else{
		printk("Hangcheck: Using TSC.\n");
	}
#endif
	hangcheck_tsc_margin = 
		(unsigned long long)(hangcheck_margin + hangcheck_tick);
	hangcheck_tsc_margin *= (unsigned long long)TIMER_FREQ;

	hangcheck_tsc = monotonic_clock();
	init_timer(&hangcheck_ticktock);
	mod_timer(&hangcheck_ticktock, jiffies + (hangcheck_tick*HZ));

	return 0;
}  /* hangcheck_init() */


static void __exit hangcheck_exit(void)
{
	printk("Stopping hangcheck timer.\n");

	lock_kernel();
	del_timer(&hangcheck_ticktock);
	unlock_kernel();
}  /* hangcheck_exit() */

module_init(hangcheck_init);
module_exit(hangcheck_exit);
