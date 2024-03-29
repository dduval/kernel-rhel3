/**
 * @file event_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 *
 * This is the global event buffer that the user-space
 * daemon reads from. The event buffer is an untyped array
 * of unsigned longs. Entries are prefixed by the
 * escape value ESCAPE_CODE followed by an identifying code.
 */

#include <linux/vmalloc.h>
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/dcookies.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
 
#include "oprof.h"
#include "event_buffer.h"
#include "oprofile_stats.h"

DECLARE_MUTEX(buffer_sem);
 
static unsigned long buffer_opened;
static DECLARE_WAIT_QUEUE_HEAD(buffer_wait);
static unsigned long * event_buffer;
static unsigned long buffer_size;
static unsigned long buffer_watershed;
static size_t buffer_pos;
/* atomic_t because wait_event checks it outside of buffer_sem */
static atomic_t buffer_ready = ATOMIC_INIT(0);

/* Add an entry to the event buffer. When we
 * get near to the end we wake up the process
 * sleeping on the read() of the file.
 */
void add_event_entry(unsigned long value)
{
	if (buffer_pos == buffer_size) {
		atomic_inc(&oprofile_stats.event_lost_overflow);
		return;
	}

	if (event_buffer) {
		event_buffer[buffer_pos] = value;
		if (++buffer_pos == buffer_size - buffer_watershed) {
			atomic_set(&buffer_ready, 1);
			wake_up(&buffer_wait);
		}
	}
}


/* Wake up the waiting process if any. This happens
 * on "echo 0 >/dev/oprofile/enable" so the daemon
 * processes the data remaining in the event buffer.
 */
void wake_up_buffer_waiter(void)
{
	down(&buffer_sem);
	atomic_set(&buffer_ready, 1);
	wake_up(&buffer_wait);
	up(&buffer_sem);
}

 
int alloc_event_buffer(void)
{
	int err = -ENOMEM;
	unsigned long * tmp;

	/*
	 * Do some sanity checking
	 */
	if (fs_buffer_watershed >= fs_buffer_size) {
		return -EINVAL;
	} 

	/*
	 * Check to see if event_buffer has
	 * already been allocated.
	 */
	if (event_buffer != 0)
		return 0;

	tmp = vmalloc(sizeof(unsigned long) * fs_buffer_size);
	if (!tmp)
		goto out; 

	down(&buffer_sem);
	if (event_buffer == 0) {
		buffer_size = fs_buffer_size;
		buffer_watershed = fs_buffer_watershed;
		event_buffer = tmp;
		tmp = 0;
	}
	up(&buffer_sem);

	if (tmp != 0)
		vfree(tmp);

	err = 0;
out:
	return err;
}


void free_event_buffer(void)
{
	unsigned long * tmp = 0;
	
	down(&buffer_sem);
	tmp = event_buffer;
	event_buffer = 0;
	up(&buffer_sem);

	if(tmp)
		vfree(tmp);
}

 
int event_buffer_open(struct inode * inode, struct file * file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit(0, &buffer_opened))
		return -EBUSY;

	/* Register as a user of dcookies
	 * to ensure they persist for the lifetime of
	 * the open event file
	 */
	err = -EINVAL;
	file->private_data = dcookie_register();
	if (!file->private_data)
		goto out;
		 
	if ((err = oprofile_setup()))
		goto fail;

	/* NB: the actual start happens from userspace
	 * echo 1 >/dev/oprofile/enable
	 */
 
	return 0;

fail:
	dcookie_unregister(file->private_data);
out:
	clear_bit(0, &buffer_opened);
	return err;
}


int event_buffer_release(struct inode * inode, struct file * file)
{
	oprofile_stop();
	oprofile_shutdown();
	dcookie_unregister(file->private_data);
	buffer_pos = 0;
	atomic_set(&buffer_ready, 0);
	clear_bit(0, &buffer_opened);
	return 0;
}


ssize_t event_buffer_read(struct file * file, char * buf, size_t count, loff_t * offset)
{
	int retval = -EINVAL;
	size_t const max = buffer_size * sizeof(unsigned long);
	unsigned long * tmp = 0;

	/* handling partial reads is more trouble than it's worth */
	if (count != max || *offset)
		return -EINVAL;

	if (file->f_flags & O_NDELAY) {
		if (down_trylock(&buffer_sem)) {
			/* Do some quick sanity checking */
			if (event_buffer == 0)
				goto out;
			retval = 0;
			if (atomic_read(&buffer_ready))
				goto do_user_copy;
			goto out;
		}
		return 0;
	}

tryagain:
	/* wait for the event buffer to fill up with some data */
	wait_event_interruptible(buffer_wait, atomic_read(&buffer_ready));
	if (signal_pending(current))
		return -EINTR;

	down(&buffer_sem);

	if (atomic_read(&buffer_ready))
		atomic_set(&buffer_ready, 0);
	else {
		up(&buffer_sem);
		goto tryagain;
	}

do_user_copy:

	/* Do some quick sanity checking */
	if (event_buffer == 0)
		goto out;

	count = buffer_pos * sizeof(unsigned long);

	/* XXX: If we sleep here, the O_NDELAY semantics breaks.. */
	tmp = vmalloc(count);
	if (tmp == 0) {
		retval = -ENOMEM;
		goto out;
	}
	memcpy(tmp, event_buffer, count);
	retval = count;
	buffer_pos = 0;

out:
	up(&buffer_sem);
	if (tmp) {
		if (copy_to_user(buf, tmp, count))
			retval = -EFAULT;
		vfree(tmp);
	}
	return retval;
}
 
struct file_operations event_buffer_fops = {
	.open		= event_buffer_open,
	.release	= event_buffer_release,
	.read		= event_buffer_read,
};
