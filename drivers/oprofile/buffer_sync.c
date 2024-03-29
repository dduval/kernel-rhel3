/**
 * @file buffer_sync.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 *
 * This is the core of the buffer management. Each
 * CPU buffer is processed and entered into the
 * global event buffer. Such processing is necessary
 * in several circumstances, mentioned below.
 *
 * The processing does the job of converting the
 * transitory EIP value into a persistent dentry/offset
 * value that the profiler can record at its leisure.
 *
 * See fs/dcookies.c for a description of the dentry/offset
 * objects.
 */

#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/dcookies.h>
#include <linux/profile.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/spinlock.h>

#include <asm/hardirq.h>
 
#include "oprofile_stats.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
 
#define DEFAULT_EXPIRE (HZ / 4)
 
static void wq_sync_buffers(void *);
#if 0
static DECLARE_WORK(sync_wq, wq_sync_buffers, 0);
#else
static struct tq_struct sync_wq = {
	routine: wq_sync_buffers,
};
#endif
 
static struct timer_list sync_timer;
static void timer_ping(unsigned long data);
static void sync_cpu_buffers(void);

 
/* We must make sure to process every entry in the CPU buffers
 * before a task got the PF_EXITING flag, otherwise we will hold
 * references to a possibly freed task_struct. We are safe with
 * samples past the PF_EXITING point in do_exit(), because we
 * explicitly check for that in cpu_buffer.c 
 */
static int exit_task_notify(struct notifier_block * self, unsigned long val, void * data)
{
	sync_cpu_buffers();
	return 0;
}
 
/* There are two cases of tasks modifying task->mm->mmap list we
 * must concern ourselves with. First, when a task is about to
 * exit (exit_mmap()), we should process the buffer to deal with
 * any samples in the CPU buffer, before we lose the ->mmap information
 * we need. Second, a task may unmap (part of) an executable mmap,
 * so we want to process samples before that happens too
 */
static int mm_notify(struct notifier_block * self, unsigned long val, void * data)
{
	sync_cpu_buffers();
	return 0;
}

 
/* We need to be told about new modules so we don't attribute to a previously
 * loaded module, or drop the samples on the floor.
 */
static int module_load_notify(struct notifier_block * self, unsigned long val, void * data)
{
#ifdef CONFIG_MODULES
	if (val != MODULE_STATE_COMING)
		return 0;

	sync_cpu_buffers();
	down(&buffer_sem);
	add_event_entry(ESCAPE_CODE);
	add_event_entry(MODULE_LOADED_CODE);
	up(&buffer_sem);
#endif
	return 0;
}

 
static struct notifier_block exit_task_nb = {
	.notifier_call	= exit_task_notify,
};

static struct notifier_block exec_unmap_nb = {
	.notifier_call	= mm_notify,
};

static struct notifier_block exit_mmap_nb = {
	.notifier_call	= mm_notify,
};

static struct notifier_block module_load_nb = {
	.notifier_call = module_load_notify,
};

 
int sync_start(void)
{
	int err;

	init_timer(&sync_timer);
	sync_timer.function = timer_ping;
	sync_timer.expires = jiffies + DEFAULT_EXPIRE;
	add_timer(&sync_timer);

	err = profile_event_register(EXIT_TASK, &exit_task_nb);
	if (err)
		goto out1;
	err = profile_event_register(EXIT_MMAP, &exit_mmap_nb);
	if (err)
		goto out2;
	err = profile_event_register(EXEC_UNMAP, &exec_unmap_nb);
	if (err)
		goto out3;
	err = register_module_notifier(&module_load_nb);
	if (err)
		goto out4;

out:
	return err;
out4:
	profile_event_unregister(EXEC_UNMAP, &exec_unmap_nb);
out3:
	profile_event_unregister(EXIT_MMAP, &exit_mmap_nb);
out2:
	profile_event_unregister(EXIT_TASK, &exit_task_nb);
out1:
	del_timer_sync(&sync_timer);
	goto out;
}


void sync_stop(void)
{
	unregister_module_notifier(&module_load_nb);
	profile_event_unregister(EXIT_TASK, &exit_task_nb);
	profile_event_unregister(EXIT_MMAP, &exit_mmap_nb);
	profile_event_unregister(EXEC_UNMAP, &exec_unmap_nb);
	del_timer_sync(&sync_timer);
}

 
/* Optimisation. We can manage without taking the dcookie sem
 * because we cannot reach this code without at least one
 * dcookie user still being registered (namely, the reader
 * of the event buffer). */
static inline unsigned long fast_get_dcookie(struct dentry * dentry,
	struct vfsmount * vfsmnt)
{
	unsigned long cookie;
 
	if (dentry->d_cookie)
		return (unsigned long)dentry;
	get_dcookie(dentry, vfsmnt, &cookie);
	return cookie;
}

 
/* Look up the dcookie for the task's first VM_EXECUTABLE mapping,
 * which corresponds loosely to "application name". This is
 * not strictly necessary but allows oprofile to associate
 * shared-library samples with particular applications
 */
static unsigned long get_exec_dcookie(struct mm_struct * mm)
{
	unsigned long cookie = 0;
	struct vm_area_struct * vma;
 
	if (!mm)
		goto out;
 
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (!vma->vm_file)
			continue;
		if (!(vma->vm_flags & VM_EXECUTABLE))
			continue;
		cookie = fast_get_dcookie(vma->vm_file->f_dentry,
			vma->vm_file->f_vfsmnt);
		break;
	}

out:
	return cookie;
}


/* Convert the EIP value of a sample into a persistent dentry/offset
 * pair that can then be added to the global event buffer. We make
 * sure to do this lookup before a mm->mmap modification happens so
 * we don't lose track.
 */
static unsigned long lookup_dcookie(struct mm_struct * mm, unsigned long addr, off_t * offset)
{
	unsigned long cookie = 0;
	struct vm_area_struct * vma;

	for (vma = find_vma(mm, addr); vma; vma = vma->vm_next) {
		if (!vma)
			goto out;
 
		if (!vma->vm_file)
			continue;

		if (addr < vma->vm_start || addr >= vma->vm_end)
			continue;

		cookie = fast_get_dcookie(vma->vm_file->f_dentry,
			vma->vm_file->f_vfsmnt);
		*offset = (vma->vm_pgoff << PAGE_SHIFT) + addr - vma->vm_start; 
		break;
	}
out:
	return cookie;
}


static unsigned long last_cookie = ~0UL;
 
static void add_cpu_switch(int i)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(CPU_SWITCH_CODE);
	add_event_entry(i);
	last_cookie = ~0UL;
}

static void add_kernel_ctx_switch(unsigned int in_kernel)
{
	add_event_entry(ESCAPE_CODE);
	if (in_kernel)
		add_event_entry(KERNEL_ENTER_SWITCH_CODE); 
	else
		add_event_entry(KERNEL_EXIT_SWITCH_CODE); 
}
 
static void add_user_ctx_switch(pid_t pid, unsigned long cookie)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(CTX_SWITCH_CODE); 
	add_event_entry(pid);
	add_event_entry(cookie);
}

 
static void add_cookie_switch(unsigned long cookie)
{
	add_event_entry(ESCAPE_CODE);
	add_event_entry(COOKIE_SWITCH_CODE);
	add_event_entry(cookie);
}

 
static void add_sample_entry(unsigned long offset, unsigned long event)
{
	add_event_entry(offset);
	add_event_entry(event);
}


static void add_us_sample(struct mm_struct * mm, struct op_sample * s)
{
	unsigned long cookie;
	off_t offset;
 
 	cookie = lookup_dcookie(mm, s->eip, &offset);
 
	if (!cookie)
		return;

	if (cookie != last_cookie) {
		add_cookie_switch(cookie);
		last_cookie = cookie;
	}

	add_sample_entry(offset, s->event);
}

 
/* Add a sample to the global event buffer. If possible the
 * sample is converted into a persistent dentry/offset pair
 * for later lookup from userspace.
 */
static void add_sample(struct mm_struct * mm, struct op_sample * s, int in_kernel)
{
	if (in_kernel) {
		add_sample_entry(s->eip, s->event);
	} else if (mm) {
		add_us_sample(mm, s);
	}
}
 
 
static void release_mm(struct mm_struct * mm)
{
	if (mm) {
		up_read(&mm->mmap_sem);
		atomic_dec(&mm->mm_users);
	}
}


/* Take the task's mmap_sem to protect ourselves from
 * races when we do lookup_dcookie().
 */
static struct mm_struct * take_task_mm(struct task_struct * task)
{
	struct mm_struct *mm = NULL;

	/* if task->mm !NULL, mm_count must be at least 1. It cannot
	 * drop to 0 without the task exiting, which will have to sleep
	 * on buffer_sem first. So we do not need to mark mm_count
	 * ourselves.
	 */
	if (in_interrupt()) {
		if (spin_trylock(&task->alloc_lock) == 0)
			return mm;
	} else
		task_lock(task);

    mm = task->mm;
	if (mm) {
		/* More ugliness. If a task took its mmap
		 * sem then came to sleep on buffer_sem we
		 * will deadlock waiting for it. So we can
		 * but try. This will lose samples :/
		 */
		if (!down_read_trylock(&mm->mmap_sem)) {
			/* FIXME: this underestimates samples lost */
			atomic_inc(&oprofile_stats.sample_lost_mmap_sem);
			mm = NULL;
		} else {
			/*
			 * Tell mmput that we have a reference
			 */
			atomic_inc(&mm->mm_users);
		}
	}
	task_unlock(task);
 
	return mm;
}
 
 
static inline int is_ctx_switch(unsigned long val)
{
	return val == ~0UL;
}
 

/* compute number of filled slots in cpu_buffer queue */
static unsigned long nr_filled_slots(struct oprofile_cpu_buffer * b)
{
	unsigned long head = b->head_pos;
	unsigned long tail = b->tail_pos;

	if (head >= tail)
		return head - tail;

	return head + (b->buffer_size - tail);
}


static void increment_tail(struct oprofile_cpu_buffer * b)
{
	unsigned long new_tail = b->tail_pos + 1;

	rmb();

	if (new_tail < (b->buffer_size))
		b->tail_pos = new_tail;
	else
		b->tail_pos = 0;
}


/* Sync one of the CPU's buffers into the global event buffer.
 * Here we need to go through each batch of samples punctuated
 * by context switch notes, taking the task's mmap_sem and doing
 * lookup in task->mm->mmap to convert EIP into dcookie/offset
 * value.
 */
static void sync_buffer(struct oprofile_cpu_buffer * cpu_buf)
{
	struct mm_struct * mm = 0;
	struct task_struct * new;
	unsigned long cookie = 0;
	int in_kernel = 1;
	unsigned int i;
 
	/* Remember, only we can modify tail_pos */

	unsigned long const available_elements = nr_filled_slots(cpu_buf);
  
	for (i=0; i < available_elements; ++i) {
		struct op_sample * s = &cpu_buf->buffer[cpu_buf->tail_pos];
 
		if (is_ctx_switch(s->eip)) {
			if (s->event <= 1) {
				/* kernel/userspace switch */
				in_kernel = s->event;
				add_kernel_ctx_switch(s->event);
			} else {
				struct mm_struct * oldmm = mm;

				/* userspace context switch */
				new = (struct task_struct *)s->event;

				release_mm(oldmm);
				mm = take_task_mm(new);
				if (mm != oldmm)
					cookie = get_exec_dcookie(mm);
				add_user_ctx_switch(new->pid, cookie);
			}
		} else {
			add_sample(mm, s, in_kernel);
		}

		increment_tail(cpu_buf);
	}
	release_mm(mm);

	cpu_buffer_reset(cpu_buf);
}
 
 
/* Process each CPU's local buffer into the global
 * event buffer.
 */
static void sync_cpu_buffers(void)
{
	int i;

	down(&buffer_sem);
 
	for (i = 0; i < NR_CPUS; ++i) {
		struct oprofile_cpu_buffer * cpu_buf;
 
		if (!cpu_possible(i))
			continue;
 
		cpu_buf = &cpu_buffer[i];
 
		add_cpu_switch(i);
		sync_buffer(cpu_buf);
	}

	up(&buffer_sem);
 
	mod_timer(&sync_timer, jiffies + DEFAULT_EXPIRE);
}
 

static void wq_sync_buffers(void * data)
{
	sync_cpu_buffers();
}
 
 
/* It is possible that we could have no munmap() or
 * other events for a period of time. This will lead
 * the CPU buffers to overflow and lose samples and
 * context switches. We try to reduce the problem
 * by timing out when nothing happens for a while.
 */
static void timer_ping(unsigned long data)
{
	schedule_task(&sync_wq);
	/* timer is re-added by the scheduled task */
}

