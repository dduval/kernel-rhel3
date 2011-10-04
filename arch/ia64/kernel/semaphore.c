/*
 * IA-64 semaphore implementation (derived from x86 version).
 *
 * Copyright (C) 1999-2000 Hewlett-Packard Co
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * Semaphores are implemented using a two-way counter: The "count"
 * variable is decremented for each process that tries to acquire the
 * semaphore, while the "sleepers" variable is a count of such
 * acquires.
 *
 * Notably, the inline "up()" and "down()" functions can efficiently
 * test if they need to do any extra work (up needs to do something
 * only if count was negative before the increment operation.
 *
 * "sleepers" and the contention routine ordering is protected by the
 * semaphore spinlock.
 *
 * Note that these functions are only called when there is contention
 * on the lock, and as such all this is the "non-critical" part of the
 * whole semaphore business. The critical part is the inline stuff in
 * <asm/semaphore.h> where we want to avoid any extra jumps and calls.
 */
#include <linux/sched.h>
#include <linux/worktodo.h>
#include <asm/semaphore.h>

/*
 * Logic:
 *  - Only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - When we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleepers" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void
__up (struct semaphore *sem)
{
	wake_up(&sem->wait);
}

static spinlock_t semaphore_lock = SPIN_LOCK_UNLOCKED;

void __wtd_down(struct semaphore * sem, struct worktodo *wtd);
void __wtd_down_from_wakeup(struct semaphore * sem, struct worktodo *wtd);

void __wtd_down_action(void *data)
{
	struct worktodo *wtd = data;
	struct semaphore *sem;

	wtd_pop(wtd);
	sem = wtd->data;

	__wtd_down_from_wakeup(sem, wtd);
}

void __wtd_down_waiter(wait_queue_t *wait)
{
	struct worktodo *wtd = (struct worktodo *)wait;
	struct semaphore *sem = wtd->data;

	__remove_wait_queue(&sem->wait, &wtd->wait);
	wtd_push(wtd, __wtd_down_action, wtd);
	wtd_queue(wtd);
}

/*
 *  Same as __wtd_down, but sem->sleepers is not incremented when
 *  coming from a wakeup.
 */
void wtd_down_common(struct semaphore * sem, struct worktodo *wtd,
			int do_incr)
{
	int gotit;
	int sleepers;

	init_waitqueue_func_entry(&wtd->wait, __wtd_down_waiter);
	wtd->data = sem;

	spin_lock_irq(&semaphore_lock);
	sem->sleepers += do_incr;
	sleepers = sem->sleepers;
	gotit = add_wait_queue_exclusive_cond(&sem->wait, &wtd->wait,
			atomic_add_negative(sleepers - 1, &sem->count));
	if (gotit)
		sem->sleepers = 0;
	else
		sem->sleepers = 1;
	spin_unlock_irq(&semaphore_lock);

	if (gotit) {
		wake_up(&sem->wait);
		wtd_queue(wtd);
	}
}

void __wtd_down(struct semaphore * sem, struct worktodo *wtd)
{
	wtd_down_common(sem, wtd, 1);
}

/*
 * Same as __wtd_down, but sem->sleepers is not incremented when
 * coming from a wakeup.
 */
void __wtd_down_from_wakeup(struct semaphore * sem, struct worktodo *wtd)
{
	wtd_down_common(sem, wtd, 0);
}

/* Returns 0 if we acquired the semaphore, 1 if it was queued. */
int wtd_down(struct worktodo *wtd, struct semaphore *sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_dec_return(&sem->count) < 0) {
		__wtd_down(sem, wtd);
		return 1;
	}
	else {
		return 0;
	}
}


void
__down (struct semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_UNINTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
	wake_up(&sem->wait);
}

int
__down_interruptible (struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_INTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers ++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
		if (signal_pending(current)) {
			retval = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock. The
		 * "-1" is because we're still hoping to get
		 * the lock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_INTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	wake_up(&sem->wait);
	return retval;
}

/*
 * Trylock failed - make sure we correct for having decremented the
 * count.
 */
int
__down_trylock (struct semaphore *sem)
{
	unsigned long flags;
	int sleepers;

	spin_lock_irqsave(&semaphore_lock, flags);
	sleepers = sem->sleepers + 1;
	sem->sleepers = 0;

	/*
	 * Add "everybody else" and us into it. They aren't
	 * playing, because we own the spinlock.
	 */
	if (!atomic_add_negative(sleepers, &sem->count))
		wake_up(&sem->wait);

	spin_unlock_irqrestore(&semaphore_lock, flags);
	return 1;
}
