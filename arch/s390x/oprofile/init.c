/**
 * @file init.c
 *
 * @remark Copyright 2002-2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Will Cohen <wcohen@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/init.h>
 
/* We support CPUs that have performance counters like the IA64
 * with irq mode samples.
 */
 
extern int irq_init(struct oprofile_operations ** ops);
extern void timer_init(struct oprofile_operations ** ops);

int __init oprofile_arch_init(struct oprofile_operations ** ops)
{
	timer_init(ops);
	return 0;
}
