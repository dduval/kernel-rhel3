/*
 *  Fast Userspace Mutexes (which I call "Futexes!").
 *  (C) Rusty Russell, IBM 2002
 *
 *  Generalized futexes, futex requeueing, misc fixes by Ingo Molnar
 *  (C) Copyright 2003 Red Hat Inc, All Rights Reserved
 *
 *  Thanks to Ben LaHaise for yelling "hashed waitqueues" loudly
 *  enough at me, Linus for the original (flawed) idea, Matthew
 *  Kirkwood for proof-of-concept implementation.
 *
 *  "The futexes are also cursed."
 *  "But they come in a choice of three flavours!"
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/futex.h>
#include <linux/vcache.h>
#include <linux/mount.h>
#include <linux/highmem.h>

#define FUTEX_HASHBITS 8

/*
 * We use this hashed waitqueue instead of a normal wait_queue_t, so
 * we can wake only the relevant ones (hashed queues may be shared):
 */
struct futex_q {
	struct list_head list;
	wait_queue_head_t waiters;

	/* Page struct and offset within it. */
	struct page *page;
	int offset;

	/* the virtual => physical COW-safe cache */
	vcache_t vcache;

	/* For fd, sigio sent using these. */
	int fd;
	struct file *filp;
};

/* The key for the hash is the address + index + offset within page */
static struct list_head futex_queues[1<<FUTEX_HASHBITS];
static spinlock_t futex_lock = SPIN_LOCK_UNLOCKED;

extern void send_sigio(struct fown_struct *fown, int fd, int band);

/* Futex-fs vfsmount entry: */
static struct vfsmount *futex_mnt;

/*
 * These are all locks that are necessery to look up a physical
 * mapping safely, and modify/search the futex hash, atomically:
 */
static inline void lock_futex_mm(void)
{
	spin_lock(&current->mm->page_table_lock);
	spin_lock(&vcache_lock);
	spin_lock(&futex_lock);
}

static inline void unlock_futex_mm(void)
{
	spin_unlock(&futex_lock);
	spin_unlock(&vcache_lock);
	spin_unlock(&current->mm->page_table_lock);
}

/*
 * The physical page is shared, so we can hash on its address:
 */
static inline struct list_head *hash_futex(struct page *page, int offset)
{
	return &futex_queues[hash_long((unsigned long)page + offset,
							FUTEX_HASHBITS)];
}

/*
 * Get kernel address of the user page and pin it.
 *
 * Must be called with (and returns with) all futex-MM locks held.
 */
static inline struct page *__pin_page_atomic (struct page *page)
{
	if (!PageReserved(page))
		get_page(page);
	return page;
}

static struct page *__pin_page(unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	struct page *page, *tmp;
	int err;

	/*
	 * Do a quick atomic lookup first - this is the fastpath.
	 */
	page = follow_page(mm, addr, 0);
	if (likely(page != NULL))
		return __pin_page_atomic(page);

	/*
	 * No luck - need to fault in the page:
	 */
repeat_lookup:

	unlock_futex_mm();

	down_read(&mm->mmap_sem);
	err = get_user_pages(current, mm, addr, 1, 0, 0, &page, NULL);
	up_read(&mm->mmap_sem);

	lock_futex_mm();

	if (err < 0)
		return NULL;
	/*
	 * Since the faulting happened with locks released, we have to
	 * check for races:
	 */
	tmp = follow_page(mm, addr, 0);
	if (tmp != page) {
		put_page(page);
		goto repeat_lookup;
	}

	return page;
}

/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static inline int futex_wake(unsigned long uaddr, int offset, int num)
{
	struct list_head *i, *next, *head;
	struct page *page;
	int ret = 0;

	lock_futex_mm();

	page = __pin_page(uaddr - offset);
	if (!page) {
		unlock_futex_mm();
		return -EFAULT;
	}

	head = hash_futex(page, offset);

	list_for_each_safe(i, next, head) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (this->page == page && this->offset == offset) {
			list_del_init(i);
			__detach_vcache(&this->vcache);
			wake_up_all(&this->waiters);
			if (this->filp)
				send_sigio(&this->filp->f_owner, this->fd, POLL_IN);
			ret++;
			if (ret >= num)
				break;
		}
	}

	unlock_futex_mm();
	put_page(page);

	return ret;
}

/*
 * This gets called by the COW code, we have to rehash any
 * futexes that were pending on the old physical page, and
 * rehash it to the new physical page. The pagetable_lock
 * and vcache_lock is already held:
 */
static void futex_vcache_callback(vcache_t *vcache, struct page *new_page)
{
	struct futex_q *q = container_of(vcache, struct futex_q, vcache);
	struct list_head *head = hash_futex(new_page, q->offset);

	spin_lock(&futex_lock);

	if (!list_empty(&q->list)) {
		put_page(q->page);
		q->page = new_page;
		__pin_page_atomic(new_page);
		list_del(&q->list);
		list_add_tail(&q->list, head);
	}

	spin_unlock(&futex_lock);
}

/*
 * Requeue all waiters hashed on one physical page to another
 * physical page.
 */
static inline int futex_requeue(unsigned long uaddr1, int offset1,
	unsigned long uaddr2, int offset2, int nr_wake, int nr_requeue,
	int *valp)
{
	struct list_head *i, *next, *head1, *head2;
	struct page *page1 = NULL, *page2 = NULL;
	int ret = 0;

	lock_futex_mm();

	page1 = __pin_page(uaddr1 - offset1);
	if (!page1)
		goto out;
	page2 = __pin_page(uaddr2 - offset2);
	if (!page2)
		goto out;

	head1 = hash_futex(page1, offset1);
	head2 = hash_futex(page2, offset2);

	if (likely (valp != NULL)) {
		void *kaddr;
		int curval;

		if (!access_ok(VERIFY_READ, uaddr1, 4)) {
			ret = -EFAULT;
			goto out;
		}
		kaddr = kmap_atomic(page1, KM_USER0);
		curval = *(int*)(kaddr + offset1);
		kunmap_atomic(kaddr, KM_USER0);

		if (curval != *valp) {
			ret = -EAGAIN;
			goto out;
		}
	}

	list_for_each_safe(i, next, head1) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (this->page == page1 && this->offset == offset1) {
			list_del_init(i);
			__detach_vcache(&this->vcache);
			if (++ret <= nr_wake) {
				wake_up_all(&this->waiters);
				if (this->filp)
					send_sigio(&this->filp->f_owner,
							this->fd, POLL_IN);
			} else {
				put_page(this->page);
				__pin_page_atomic (page2);
				list_add_tail(i, head2);
				__attach_vcache(&this->vcache, uaddr2,
					current->mm, futex_vcache_callback);
				this->offset = offset2;
				this->page = page2;
				if (ret - nr_wake >= nr_requeue)
					break;
			}
		}
	}

out:
	unlock_futex_mm();

	if (page1)
		put_page(page1);
	if (page2)
		put_page(page2);

	return ret;
}

static inline void __queue_me(struct futex_q *q, struct page *page,
				unsigned long uaddr, int offset,
				int fd, struct file *filp)
{
	struct list_head *head = hash_futex(page, offset);

	q->offset = offset;
	q->fd = fd;
	q->filp = filp;
	q->page = page;

	list_add_tail(&q->list, head);
	/*
	 * We register a futex callback to this virtual address,
	 * to make sure a COW properly rehashes the futex-queue.
	 */
	__attach_vcache(&q->vcache, uaddr, current->mm, futex_vcache_callback);
}

/* Return 1 if we were still queued (ie. 0 means we were woken) */
static inline int __unqueue_me(struct futex_q *q)
{
	if (!list_empty(&q->list)) {
		list_del(&q->list);
		__detach_vcache(&q->vcache);
		return 1;
	}
	return 0;
}

static inline int unqueue_me(struct futex_q *q)
{
	int ret = 0;

	spin_lock(&vcache_lock);
	spin_lock(&futex_lock);
	ret = __unqueue_me(q);
	spin_unlock(&futex_lock);
	spin_unlock(&vcache_lock);
	return ret;
}

static inline int futex_wait(unsigned long uaddr,
		      int offset,
		      int val,
		      unsigned long time)
{
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0, curval;
	struct page *page;
	struct futex_q q;
	void *kaddr;

	init_waitqueue_head(&q.waiters);

	lock_futex_mm();

	page = __pin_page(uaddr - offset);
	if (!page) {
		unlock_futex_mm();
		return -EFAULT;
	}
	__queue_me(&q, page, uaddr, offset, -1, NULL);

	/*
	 * Page is pinned, but may no longer be in this address space.
	 * It cannot schedule, so we access it with the spinlock held.
	 */
	if (!access_ok(VERIFY_READ, uaddr, 4)) {
		__unqueue_me(&q);
		unlock_futex_mm();
		ret = -EFAULT;
		goto out;
	}
	kaddr = kmap_atomic(page, KM_USER0);
	curval = *(int*)(kaddr + offset);
	kunmap_atomic(kaddr, KM_USER0);

	if (curval != val) {
		__unqueue_me(&q);
		unlock_futex_mm();
		ret = -EWOULDBLOCK;
		goto out;
	}
	/*
	 * The get_user() above might fault and schedule so we
	 * cannot just set TASK_INTERRUPTIBLE state when queueing
	 * ourselves into the futex hash. This code thus has to
	 * rely on the futex_wake() code doing a wakeup after removing
	 * the waiter from the list.
	 */
	add_wait_queue(&q.waiters, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	if (!list_empty(&q.list)) {
		unlock_futex_mm();
		time = schedule_timeout(time);
	}
	set_current_state(TASK_RUNNING);
	/*
	 * NOTE: we don't remove ourselves from the waitqueue because
	 * we are the only user of it.
	 */
	if (time == 0) {
		ret = -ETIMEDOUT;
		goto out_wait;
	}
	if (signal_pending(current))
		ret = -EINTR;
out_wait:
	/* Were we woken up anyway? */
	if (!unqueue_me(&q))
		ret = 0;
out:
	put_page(q.page);

	return ret;
}

long do_futex(unsigned long uaddr, int op, int val, unsigned long timeout,
		unsigned long uaddr2, int val2, int val3)
{
	unsigned long pos_in_page;
	int ret;

	if (!access_ok(VERIFY_READ, uaddr, sizeof(unsigned long)))
		return -EFAULT;

	pos_in_page = uaddr % PAGE_SIZE;

	/* Must be "naturally" aligned */
	if (pos_in_page % sizeof(u32))
		return -EINVAL;

	switch (op) {
	case FUTEX_WAIT:
		ret = futex_wait(uaddr, pos_in_page, val, timeout);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(uaddr, pos_in_page, val);
		break;
	case FUTEX_REQUEUE:
	{
		unsigned long pos_in_page2;

		if (!access_ok(VERIFY_READ, uaddr2, sizeof(unsigned long)))
			return -EFAULT;

		pos_in_page2 = uaddr2 % PAGE_SIZE;

		/* Must be "naturally" aligned */
		if (pos_in_page2 % sizeof(u32))
			return -EINVAL;

		ret = futex_requeue(uaddr, pos_in_page, uaddr2, pos_in_page2,
				    val, val2, NULL);
		break;
	}
	case FUTEX_CMP_REQUEUE:
	{
		unsigned long pos_in_page2;

		if (!access_ok(VERIFY_READ, uaddr2, sizeof(unsigned long)))
			return -EFAULT;

		pos_in_page2 = uaddr2 % PAGE_SIZE;

		/* Must be "naturally" aligned */
		if (pos_in_page2 % sizeof(u32))
			return -EINVAL;

		ret = futex_requeue(uaddr, pos_in_page, uaddr2, pos_in_page2,
				    val, val2, &val3);
		break;
	}
	default:
		ret = -ENOSYS;
	}
	return ret;
}


asmlinkage long sys_futex(u32 __user *uaddr, int op, int val,
			  struct timespec __user *utime, u32 __user *uaddr2,
			  int val3)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (copy_from_user(&t, utime, sizeof(t)) != 0)
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	/*
	 * requeue parameter in 'utime' if op == FUTEX_REQUEUE.
	 */
	if (op >= FUTEX_REQUEUE)
		val2 = (int) (long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2, val3);
}

static int __init init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(futex_queues); i++)
		INIT_LIST_HEAD(&futex_queues[i]);
	return 0;
}
__initcall(init);
