/*
 *  linux/drivers/block/diskdumplib.c
 *
 *  Copyright (C) 2004  FUJITSU LIMITED
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/smp_lock.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/diskdump.h>
#include <linux/diskdumplib.h>
#include <asm/diskdump.h>

/*
 * timer list and tasklet_struct holder
 */
unsigned long volatile diskdump_base_jiffies;
static unsigned long long timestamp_base;
static unsigned long timestamp_hz;

/* notifiers to be called before starting dump */
struct notifier_block *disk_dump_notifier_list;

#define DISKDUMP_NUM_TASKLETS	8

/*
 * We can't use next field of tasklet because it breaks the original
 * tasklets chain and we have no way to know which chain the tasklet is
 * linked.
 */
static struct tasklet_struct	*diskdump_tasklets[DISKDUMP_NUM_TASKLETS];

static LIST_HEAD(diskdump_timers);
static LIST_HEAD(diskdump_taskq);

static void (*diskdump_poll_func)(void *);
static void *diskdump_poll_device;

void diskdump_register_poll(void *device, void (*poll_func)(void *))
{
	diskdump_poll_device = device;
	diskdump_poll_func = poll_func;
}

static int woken;

void _diskdump_wake_up(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	woken = 1;
}

static int store_tasklet(struct tasklet_struct *tasklet)
{
	int i;

	for (i = 0; i < DISKDUMP_NUM_TASKLETS; i++)
		if (diskdump_tasklets[i] == NULL) {
			diskdump_tasklets[i] = tasklet;
			return 0;
		}
	return -1;
}

static struct tasklet_struct *find_tasklet(struct tasklet_struct *tasklet)
{
	int i;

	for (i = 0; i < DISKDUMP_NUM_TASKLETS; i++)
		if (diskdump_tasklets[i] == tasklet)
			return diskdump_tasklets[i];
	return NULL;
}

void _diskdump_tasklet_schedule(struct tasklet_struct *tasklet)
{
	struct tasklet_struct *tasklet_addr;

	if (!find_tasklet(tasklet))
		if (store_tasklet(tasklet))
			printk(KERN_ERR "diskdumplib: too many tasklet. Ignored\n");
	set_bit(TASKLET_STATE_SCHED, &tasklet->state);
}

int _diskdump_schedule_task(struct tq_struct *task)
{
	list_add_tail(&task->list, &diskdump_taskq);
	return 1;
}

void _diskdump_add_timer(struct timer_list *timer)
{
	timer->base = (void *)1;
	list_add(&timer->entry, &diskdump_timers);
}

int _diskdump_del_timer(struct timer_list *timer)
{
	if (timer->base != NULL) {
		list_del(&timer->entry);
		timer->base = NULL;
		return 1;
	} else {
		return 0;
	}
}

int _diskdump_mod_timer(struct timer_list *timer, unsigned long expires)
{
	int ret;

	ret = _diskdump_del_timer(timer);
	timer->expires = expires;
	_diskdump_add_timer(timer);

	return ret;
}

static void update_jiffies(void)
{
	unsigned long long t;

	platform_timestamp(t);
	while (t > timestamp_base + timestamp_hz) {
		timestamp_base += timestamp_hz;
		jiffies++;
		platform_timestamp(t);
	}
}

void diskdump_update(void)
{
	struct tasklet_struct *tasklet;
	struct tq_struct *task;
	struct timer_list *timer;
	list_t *t, *n, head;
	int i;

	update_jiffies();

	/* run timers */
	list_for_each_safe(t, n, &diskdump_timers) {
		timer = list_entry(t, struct timer_list, entry);
		if (time_before_eq(timer->expires, jiffies)) {
			list_del(t);
			timer->base = NULL;
			timer->function(timer->data);
		}
	}

	/* run tasklet */
	for (i = 0; i < DISKDUMP_NUM_TASKLETS; i++)
		if ((tasklet = diskdump_tasklets[i]))
			if (!atomic_read(&tasklet->count))
				if (test_and_clear_bit(TASKLET_STATE_SCHED, &tasklet->state))
					tasklet->func(tasklet->data);

	/* run task queue */
	list_add(&head, &diskdump_taskq);
	list_del_init(&diskdump_taskq);
	n = head.next;
	while (n != &head) {
		task = list_entry(t, struct tq_struct, list);
		n = n->next;
		if (task->routine)
			task->routine(task->data);
	}
}

void _diskdump_schedule(void)
{
	woken = 0;
	while (!woken) {
		diskdump_poll_func(diskdump_poll_device);
		udelay(100);
		diskdump_update();
	}
}

signed long _diskdump_schedule_timeout(signed long timeout)
{
	unsigned long expire;

	expire = timeout + jiffies;
	woken = 0;
	while (time_before(jiffies, expire)) {
		diskdump_poll_func(diskdump_poll_device);
		udelay(100);
		diskdump_update();
		if (woken)
			return (signed long)(expire - jiffies);
	}
	return 0;
}

void diskdump_lib_init(void)
{
	unsigned long long t;

	/* Save original jiffies value */
	diskdump_base_jiffies = jiffies;

	platform_timestamp(timestamp_base);
	udelay(1000000/HZ);
	platform_timestamp(t);
	timestamp_hz = (unsigned long)(t - timestamp_base);

	diskdump_update();
}

void diskdump_lib_exit(void)
{
	/* Resotre original jiffies. */
	jiffies = diskdump_base_jiffies;
}

EXPORT_SYMBOL(disk_dump_notifier_list);
EXPORT_SYMBOL(diskdump_lib_init);
EXPORT_SYMBOL(diskdump_lib_exit);
EXPORT_SYMBOL(diskdump_update);
EXPORT_SYMBOL(_diskdump_add_timer);
EXPORT_SYMBOL(_diskdump_del_timer);
EXPORT_SYMBOL(_diskdump_mod_timer);
EXPORT_SYMBOL(_diskdump_tasklet_schedule);
EXPORT_SYMBOL(_diskdump_schedule_task);
EXPORT_SYMBOL(_diskdump_schedule);
EXPORT_SYMBOL(_diskdump_schedule_timeout);
EXPORT_SYMBOL(_diskdump_wake_up);
EXPORT_SYMBOL(diskdump_register_poll);

MODULE_LICENSE("GPL");
