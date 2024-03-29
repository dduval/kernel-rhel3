/**
 * @file oprofile_stats.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 */

#include <linux/oprofile.h>
#include <linux/smp.h>
#include <linux/threads.h>
 
#include "oprofile_stats.h"
#include "cpu_buffer.h"
 
struct oprofile_stat_struct oprofile_stats;
 
void oprofile_reset_stats(void)
{
	struct oprofile_cpu_buffer * cpu_buf; 
	int i;
 
	for (i = 0; i < NR_CPUS; ++i) {
		if (!cpu_possible(i))
			continue;

		cpu_buf = &cpu_buffer[i]; 
		cpu_buf->sample_received = 0;
		cpu_buf->sample_lost_overflow = 0;
		cpu_buf->sample_lost_task_exit = 0;
	}
 
	atomic_set(&oprofile_stats.sample_lost_mmap_sem, 0);
	atomic_set(&oprofile_stats.event_lost_overflow, 0);
}


void oprofile_create_stats_files(struct super_block * sb, struct dentry * root)
{
	struct oprofile_cpu_buffer * cpu_buf;
	struct dentry * cpudir;
	struct dentry * dir;
	char buf[10];
	int i;

	dir = oprofilefs_mkdir(sb, root, "stats");
	if (!dir)
		return;

	for (i = 0; i < NR_CPUS; ++i) {
		if (!cpu_possible(i))
			continue;

		cpu_buf = &cpu_buffer[i]; 
		snprintf(buf, 6, "cpu%d", i);
		cpudir = oprofilefs_mkdir(sb, dir, buf);
 
		/* Strictly speaking access to these ulongs is racy,
		 * but we can't simply lock them, and they are
		 * informational only.
		 */
		oprofilefs_create_ro_ulong(sb, cpudir, "sample_received",
			&cpu_buf->sample_received);
		oprofilefs_create_ro_ulong(sb, cpudir, "sample_lost_overflow",
			&cpu_buf->sample_lost_overflow);
		oprofilefs_create_ro_ulong(sb, cpudir, "sample_lost_task_exit",
			&cpu_buf->sample_lost_task_exit);
	}
 
	oprofilefs_create_ro_atomic(sb, dir, "sample_lost_mmap_sem",
		&oprofile_stats.sample_lost_mmap_sem);
	oprofilefs_create_ro_atomic(sb, dir, "event_lost_overflow",
		&oprofile_stats.event_lost_overflow);
}
