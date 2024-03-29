/**
 * @file oprof.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/oprofile.h>
#include <asm/semaphore.h>

#include "oprof.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprofile_stats.h"
 
struct oprofile_operations * oprofile_ops;
unsigned long oprofile_started;
static unsigned long is_setup;
static DECLARE_MUTEX(start_sem);

int oprofile_setup(void)
{
	int err;
 
	if ((err = alloc_cpu_buffers()))
		goto out;

	if ((err = alloc_event_buffer()))
		goto out1;
 
	if (oprofile_ops->setup && (err = oprofile_ops->setup()))
		goto out2;
 
	/* Note even though this starts part of the
	 * profiling overhead, it's necessary to prevent
	 * us missing task deaths and eventually oopsing
	 * when trying to process the event buffer.
	 */
	if ((err = sync_start()))
		goto out3;

	down(&start_sem);
	is_setup = 1;
	up(&start_sem);
	return 0;
 
out3:
	if (oprofile_ops->shutdown)
		oprofile_ops->shutdown();
out2:
	free_event_buffer();
out1:
	free_cpu_buffers();
out:
	return err;
}


/* Actually start profiling (echo 1>/dev/oprofile/enable) */
int oprofile_start(void)
{
	int err = -EINVAL;
 
	down(&start_sem);
 
	if (!is_setup)
		goto out;

	err = 0; 
 
	if (oprofile_started)
		goto out;
 
	oprofile_reset_stats();

	if ((err = oprofile_ops->start()))
		goto out;

	oprofile_started = 1;
out:
	up(&start_sem); 
	return err;
}

 
/* echo 0>/dev/oprofile/enable */
void oprofile_stop(void)
{
	down(&start_sem);
	if (!oprofile_started)
		goto out;
	oprofile_ops->stop();
	oprofile_started = 0;
	/* wake up the daemon to read what remains */
	wake_up_buffer_waiter();
out:
	up(&start_sem);
}


void oprofile_shutdown(void)
{
	sync_stop();
	if (oprofile_ops->shutdown)
		oprofile_ops->shutdown(); 
	/* down() is also necessary to synchronise all pending events
	 * before freeing */
	down(&start_sem);
	is_setup = 0;
	up(&start_sem);
	free_event_buffer();
	free_cpu_buffers();
}

 
static int __init oprofile_init(void)
{
	int err;

	/* Architecture must fill in the interrupt ops and the
	 * logical CPU type.
	 */
	err = oprofile_arch_init(&oprofile_ops);
	if (err)
		goto out;

	if (!oprofile_ops->cpu_type) {
		printk(KERN_ERR "oprofile: cpu_type not set !\n");
		err = -EFAULT;
		goto out;
	}

	err = oprofilefs_register();
	if (err)
		goto out;
 
out:
	return err;
}


static void __exit oprofile_exit(void)
{
	oprofilefs_unregister();
}

 
module_init(oprofile_init);
module_exit(oprofile_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Levon <levon@movementarian.org>");
MODULE_DESCRIPTION("OProfile system profiler");
