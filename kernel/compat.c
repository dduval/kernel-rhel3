/*
 *  linux/kernel/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002-2003 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/signal.h>
#include <linux/sched.h>	/* for MAX_SCHEDULE_TIMEOUT */
#include <linux/futex.h>	/* for FUTEX_WAIT */
#include <linux/aio_abi.h>

#include <asm/uaccess.h>

int get_compat_timespec(struct timespec *ts, struct compat_timespec *cts)
{
	return (verify_area(VERIFY_READ, cts, sizeof(*cts)) ||
			__get_user(ts->tv_sec, &cts->tv_sec) ||
			__get_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

int put_compat_timespec(struct timespec *ts, struct compat_timespec *cts)
{
	return (verify_area(VERIFY_WRITE, cts, sizeof(*cts)) ||
			__put_user(ts->tv_sec, &cts->tv_sec) ||
			__put_user(ts->tv_nsec, &cts->tv_nsec)) ? -EFAULT : 0;
}

asmlinkage int compat_sys_futex(u32 *uaddr, int op, int val,
		struct compat_timespec *utime, u32 *uaddr2, int val3)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (get_compat_timespec(&t, utime))
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	if (op >= FUTEX_REQUEUE)
		val2 = (int) (long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2, val3);
}

extern asmlinkage int sys_sched_setaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int compat_sys_sched_setaffinity(compat_pid_t pid, 
					     unsigned int len,
					     compat_ulong_t *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	if (get_user(kernel_mask, user_mask_ptr))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_setaffinity(pid,
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	return ret;
}

extern asmlinkage int sys_sched_getaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int compat_sys_sched_getaffinity(compat_pid_t pid, unsigned int len,
					    compat_ulong_t *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_getaffinity(pid,
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	if (ret > 0) {
		if (put_user(kernel_mask, user_mask_ptr))
			ret = -EFAULT;
		ret = sizeof(compat_ulong_t);
	}

	return ret;
}

extern asmlinkage long sys_io_setup(unsigned nr_reqs, aio_context_t *ctx);

asmlinkage long compat_sys_io_setup(unsigned nr_reqs, u32 *ctx32p)
{
	long ret;
	aio_context_t ctx64;
	mm_segment_t oldfs = get_fs();

	if (get_user((u32)ctx64, ctx32p))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_io_setup(nr_reqs, &ctx64);
	set_fs(oldfs);

	/* truncating is ok because it's a user address */
	if (!ret)
		ret = put_user((u32)ctx64, ctx32p);

	return ret;
}

extern asmlinkage long sys_io_getevents(aio_context_t ctx_id, long min_nr, 
					long nr, struct io_event *events, 
					struct timespec *timeout);

asmlinkage long compat_sys_io_getevents(aio_context_t ctx_id, u32 min_nr, 
					u32 nr, struct io_event *events, 
					struct compat_timespec *t32)
{
	struct timespec t;
	long ret;
	mm_segment_t oldfs = get_fs();

	if (t32) {
		if (get_user(t.tv_sec, &t32->tv_sec) ||
		    __get_user(t.tv_nsec, &t32->tv_nsec))
			return -EFAULT;
	}

	if (verify_area(VERIFY_WRITE, events, nr * sizeof(*events)))
		return -EFAULT;

	set_fs(KERNEL_DS);
	/* sign extend min_nr and nr */
	ret = sys_io_getevents(ctx_id, (long)(int)min_nr, (long)(int)nr, 
			       events, t32 ? &t : NULL);
	set_fs(oldfs);

	return ret;
}

extern int io_submit_one (struct kioctx *ctx, struct iocb *user_iocb,
			  struct iocb *iocb);

extern struct kioctx *lookup_ioctx(unsigned long ctx_id);

asmlinkage long compat_sys_io_submit(aio_context_t ctx_id, u32 number, 
				     u32 *iocbpp)
{
	struct kioctx *ctx;
	long ret = 0;
	int i;
	int nr = (int)number;	/* sign extend */

	if (unlikely(nr < 0))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_READ, iocbpp, (nr*sizeof(u32)))))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx)) {
		pr_debug("EINVAL: io_submit: invalid context id\n");
		return -EINVAL;
	}

	for (i=0; i<nr; i++) {
		struct iocb tmp;
		u32 *user_iocb;

		if (unlikely(__get_user(user_iocb, iocbpp + i))) {
			ret = -EFAULT;
			break;
		}

		if (unlikely(copy_from_user(&tmp, user_iocb, sizeof(tmp)))) {
			ret = -EFAULT;
			break;
		}

		ret = io_submit_one(ctx, user_iocb, &tmp);
		if (ret)
			break;
	}

	put_ioctx(ctx);
	return i ? i : ret;
}
