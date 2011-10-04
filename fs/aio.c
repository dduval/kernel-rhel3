/* fs/aio.c
 *	An async IO implementation for Linux
 *	Written by Benjamin LaHaise <bcrl@redhat.com>
 *
 *	Implements an efficient asynchronous io interface.
 *
 *	Copyright 2000, 2001, 2002 Red Hat, Inc.  All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
//#define DEBUG 1

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/vmalloc.h>
#include <linux/iobuf.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/brlock.h>
#include <linux/aio.h>
#include <linux/smp_lock.h>
#include <linux/compiler.h>
#include <linux/poll.h>
#include <linux/brlock.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <linux/highmem.h>

#if DEBUG > 1
#define dprintk		printk
#else
#define dprintk(x...)	do { ; } while (0)
#endif

/*------ sysctl variables----*/
unsigned aio_nr;		/* current system wide number of aio requests */
unsigned aio_max_nr = 0x10000;	/* system wide maximum number of aio requests */
unsigned aio_max_size = 0x20000;	/* 128KB per chunk */
unsigned aio_max_pinned;		/* set to mem/4 in aio_setup */
/*----end sysctl variables---*/

static kmem_cache_t	*kiocb_cachep;
static kmem_cache_t	*kioctx_cachep;

/* tunable.  Needs to be added to sysctl. */
int max_aio_reqs = 0x10000;

/* Used for rare fput completion. */
static void aio_fput_routine(void *);
static struct tq_struct	fput_tqueue = {
	routine:	aio_fput_routine,
};

static spinlock_t	fput_lock = SPIN_LOCK_UNLOCKED;
LIST_HEAD(fput_head);

/* forward prototypes */
static void generic_aio_complete_read(void *_iocb, struct kvec *vec, ssize_t res);
static void generic_aio_complete_write(void *_iocb, struct kvec *vec, ssize_t res);

/* aio_setup
 *	Creates the slab caches used by the aio routines, panic on
 *	failure as this is done early during the boot sequence.
 */
static int __init aio_setup(void)
{
	kiocb_cachep = kmem_cache_create("kiocb", sizeof(struct kiocb),
				0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!kiocb_cachep)
		panic("unable to create kiocb cache\n");

	kioctx_cachep = kmem_cache_create("kioctx", sizeof(struct kioctx),
				0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!kioctx_cachep)
		panic("unable to create kioctx cache");

	aio_max_pinned = num_physpages/4;

	printk(KERN_NOTICE "aio_setup: num_physpages = %u\n", aio_max_pinned);
	printk(KERN_NOTICE "aio_setup: sizeof(struct page) = %d\n", (int)sizeof(struct page));

	return 0;
}

static void ioctx_free_reqs(struct kioctx *ctx)
{
	struct list_head *pos, *next;
	list_for_each_safe(pos, next, &ctx->free_reqs) {
		struct kiocb *iocb = list_kiocb(pos);
		list_del(&iocb->list);
		kmem_cache_free(kiocb_cachep, iocb);
	}
}

static void aio_free_ring(struct kioctx *ctx)
{
	struct aio_ring_info *info = &ctx->ring_info;

	if (info->kvec) {
		unmap_kvec(info->kvec, 1);
		free_kvec(info->kvec);
	}

	if (info->mmap_size) {
		down_write(&ctx->mm->mmap_sem);
		do_munmap(ctx->mm, info->mmap_base, info->mmap_size, 0);
		up_write(&ctx->mm->mmap_sem);
	}

	if (info->ring_pages && info->ring_pages != info->internal_pages)
		kfree(info->ring_pages);
	info->ring_pages = NULL;
	info->nr = 0;
}

static int aio_setup_ring(struct kioctx *ctx)
{
	struct aio_ring *ring;
	struct aio_ring_info *info = &ctx->ring_info;
	unsigned nr_reqs = ctx->max_reqs;
	unsigned long size;
	int nr_pages, i;

	/* Compensate for the ring buffer's head/tail overlap entry */
	nr_reqs += 1;

	size = sizeof(struct aio_ring);
	size += sizeof(struct io_event) * nr_reqs;
	nr_pages = (size + PAGE_SIZE-1) >> PAGE_SHIFT;

	if (nr_pages < 0)
		return -EINVAL;

	info->nr_pages = nr_pages;

	nr_reqs = (PAGE_SIZE * nr_pages - sizeof(struct aio_ring)) / sizeof(struct io_event);

	info->nr = 0;
	info->ring_pages = info->internal_pages;
	if (nr_pages > AIO_RING_PAGES) {
		info->ring_pages = kmalloc(sizeof(struct page *) * nr_pages, GFP_KERNEL);
		if (!info->ring_pages)
			return -ENOMEM;
		memset(info->ring_pages, 0, sizeof(struct page *) * nr_pages);
	}

	info->mmap_size = nr_pages * PAGE_SIZE;
	dprintk("attempting mmap of %lu bytes\n", info->mmap_size);
	down_write(&ctx->mm->mmap_sem);
	info->mmap_base = do_mmap(NULL, 0, info->mmap_size, 
				  PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
				  0);
	up_write(&ctx->mm->mmap_sem);
	if (IS_ERR((void *)info->mmap_base)) {
		dprintk("mmap err: %ld\n", -info->mmap_base);
		info->mmap_size = 0;
		aio_free_ring(ctx);
		return -EAGAIN;
	}
	dprintk("mmap address: 0x%08lx\n", info->mmap_base);
	info->kvec = map_user_kvec(READ, info->mmap_base, info->mmap_size);
	if (unlikely(IS_ERR(info->kvec))) {
		info->kvec = NULL;
		aio_free_ring(ctx);
		return -EAGAIN;
	}

	if (unlikely(info->kvec->nr != nr_pages))
		BUG();

	for (i=0; i<nr_pages; i++) {
		if (unlikely(info->kvec->veclet[i].offset))
			BUG();
		info->ring_pages[i] = info->kvec->veclet[i].page;
		//printk("[%d] %p -> %p\n", i, info->kvec->veclet[i].page,
		//	info->pages[i]);
	}


	ctx->user_id = info->mmap_base;

	info->nr = nr_reqs;		/* trusted copy */

	ring = kmap_atomic(info->ring_pages[0], KM_USER0);
	ring->nr = nr_reqs;	/* user copy */
	ring->id = ctx->user_id;
	ring->head = ring->tail = 0;
	ring->magic = AIO_RING_MAGIC;
	ring->compat_features = AIO_RING_COMPAT_FEATURES;
	ring->incompat_features = AIO_RING_INCOMPAT_FEATURES;
	ring->header_length = sizeof(struct aio_ring);
	kunmap_atomic(ring, KM_USER0);

	return 0;
}

/* aio_ring_event: returns a pointer to the event at the given index from
 * kmap_atomic(, km).  Release the pointer with put_aio_ring_event();
 */
static inline struct io_event *aio_ring_event(struct aio_ring_info *info, int nr, enum km_type km)
{
	struct io_event *events;
#define AIO_EVENTS_PER_PAGE	(PAGE_SIZE / sizeof(struct io_event))
#define AIO_EVENTS_FIRST_PAGE	((PAGE_SIZE - sizeof(struct aio_ring)) / sizeof(struct io_event))

	if (nr < AIO_EVENTS_FIRST_PAGE) {
		struct aio_ring *ring;
		ring = kmap_atomic(info->ring_pages[0], km);
		return &ring->io_events[nr];
	}
	nr -= AIO_EVENTS_FIRST_PAGE;

	events = kmap_atomic(info->ring_pages[1 + nr / AIO_EVENTS_PER_PAGE], km);

	return events + (nr % AIO_EVENTS_PER_PAGE);
}

static inline void put_aio_ring_event(struct io_event *event, enum km_type km)
{
	void *p = (void *)((unsigned long)event & PAGE_MASK);
	kunmap_atomic(p, km);
}

/* ioctx_alloc
 *	Allocates and initializes an ioctx.  Returns an ERR_PTR if it failed.
 */
static struct kioctx *ioctx_alloc(unsigned nr_reqs)
{
	struct kioctx *ctx;
	unsigned i;

	/* Prevent overflows */
	if ((nr_reqs > (0x10000000U / sizeof(struct io_event))) ||
	    (nr_reqs > (0x10000000U / sizeof(struct kiocb)))) {
		pr_debug("ENOMEM: nr_reqs too high\n");
		return ERR_PTR(-EINVAL);
	}

	if (nr_reqs > aio_max_nr)
		return ERR_PTR(-EAGAIN);

	ctx = kmem_cache_alloc(kioctx_cachep, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	memset(ctx, 0, sizeof(*ctx));
	ctx->max_reqs = nr_reqs;
	ctx->mm = current->mm;
	atomic_inc(&ctx->mm->mm_count);

	atomic_set(&ctx->users, 1);
	spin_lock_init(&ctx->lock);
	spin_lock_init(&ctx->ring_info.ring_lock);
	init_waitqueue_head(&ctx->wait);

	INIT_LIST_HEAD(&ctx->free_reqs);
	INIT_LIST_HEAD(&ctx->active_reqs);
	//ctx->user_id = ++current->mm->new_ioctx_id;

	if (aio_setup_ring(ctx) < 0)
		goto out_freectx;

	/* Allocate nr_reqs iocbs for io.  Free iocbs are on the 
	 * ctx->free_reqs list.  When active they migrate to the 
	 * active_reqs list.  During completion and cancellation 
	 * the request may temporarily not be on any list.
	 */
	for (i=0; i<nr_reqs; i++) {
		struct kiocb *iocb = kmem_cache_alloc(kiocb_cachep, GFP_KERNEL);
		if (!iocb)
			goto out_freering;
		memset(iocb, 0, sizeof(*iocb));
		iocb->key = i;
		iocb->users = 0;
		list_add(&iocb->list, &ctx->free_reqs);
	}

	/* now link into global list.  kludge.  FIXME */
	br_write_lock(BR_AIO_REQ_LOCK);			
	if (unlikely(aio_nr + ctx->max_reqs > aio_max_nr))
		goto out_cleanup;
	aio_nr += ctx->max_reqs;	/* undone by __put_ioctx */
	ctx->next = current->mm->ioctx_list;
	current->mm->ioctx_list = ctx;
	br_write_unlock(BR_AIO_REQ_LOCK);

	dprintk("aio: allocated ioctx %p[%ld]: mm=%p mask=0x%x\n",
		ctx, ctx->user_id, current->mm, ctx->ring_info.nr);
	return ctx;

out_cleanup:
	br_write_unlock(BR_AIO_REQ_LOCK);
	ctx->max_reqs = 0;	/* prevent __put_ioctx from sub'ing aio_nr */
	__put_ioctx(ctx);
	return ERR_PTR(-EAGAIN);

out_freering:
	aio_free_ring(ctx);
	ioctx_free_reqs(ctx);
out_freectx:
	mmdrop(ctx->mm);
	kmem_cache_free(kioctx_cachep, ctx);
	ctx = ERR_PTR(-ENOMEM);

	dprintk("aio: error allocating ioctx %p\n", ctx);
	return ctx;
}

/* aio_cancel_all
 *	Cancels all outstanding aio requests on an aio context.  Used 
 *	when the processes owning a context have all exited to encourage 
 *	the rapid destruction of the kioctx.
 */
static void aio_cancel_all(struct kioctx *ctx)
{
	int (*cancel)(struct kiocb *, struct io_event *);
	struct io_event res;
	spin_lock_irq(&ctx->lock);
	ctx->dead = 1;
	while (!list_empty(&ctx->active_reqs)) {
		struct list_head *pos = ctx->active_reqs.next;
		struct kiocb *iocb = list_kiocb(pos);
		list_del_init(&iocb->list);
		cancel = iocb->cancel;
		if (cancel)
			iocb->users++;
		spin_unlock_irq(&ctx->lock);
		if (cancel)
			cancel(iocb, &res);
		spin_lock_irq(&ctx->lock);
	}
	spin_unlock_irq(&ctx->lock);
}

void wait_for_all_aios(struct kioctx *ctx)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	if (!ctx->reqs_active)
		return;

	add_wait_queue(&ctx->wait, &wait);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	while (ctx->reqs_active) {
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}
	set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(&ctx->wait, &wait);
}

/* exit_aio: called when the last user of mm goes away.  At this point, 
 * there is no way for any new requests to be submited or any of the 
 * io_* syscalls to be called on the context.  However, there may be 
 * outstanding requests which hold references to the context; as they 
 * go away, they will call put_ioctx and release any pinned memory
 * associated with the request (held via struct page * references).
 */
void exit_aio(struct mm_struct *mm)
{
	struct kioctx *ctx = mm->ioctx_list;
	mm->ioctx_list = NULL;
	while (ctx) {
		struct kioctx *next = ctx->next;
		ctx->next = NULL;
		aio_cancel_all(ctx);

		wait_for_all_aios(ctx);

		if (1 != atomic_read(&ctx->users))
			printk(KERN_DEBUG
				"exit_aio:ioctx still alive: %d %d %d\n",
				atomic_read(&ctx->users), ctx->dead,
				ctx->reqs_active);
		put_ioctx(ctx);
		ctx = next;
	}
}

/* __put_ioctx
 *	Called when the last user of an aio context has gone away,
 *	and the struct needs to be freed.
 */
void __put_ioctx(struct kioctx *ctx)
{
	unsigned nr_reqs = ctx->max_reqs;

	if (unlikely(ctx->reqs_active))
		BUG();

	aio_free_ring(ctx);
	mmdrop(ctx->mm);
	ctx->mm = NULL;
	pr_debug("__put_ioctx: freeing %p\n", ctx);
	ioctx_free_reqs(ctx);
	kmem_cache_free(kioctx_cachep, ctx);

	br_write_lock(BR_AIO_REQ_LOCK);
	aio_nr -= nr_reqs;
	br_write_unlock(BR_AIO_REQ_LOCK);
}

/* aio_get_req
 *	Allocate a slot for an aio request.  Increments the users count
 * of the kioctx so that the kioctx stays around until all requests are
 * complete.  Returns -EAGAIN if no requests are free.
 */
static struct kiocb *FASTCALL(__aio_get_req(struct kioctx *ctx));
static struct kiocb *__aio_get_req(struct kioctx *ctx)
{
	struct kiocb *req = NULL;
	struct aio_ring *ring;

	/* Use cmpxchg instead of spin_lock? */
	spin_lock_irq(&ctx->lock);
	ring = kmap_atomic(ctx->ring_info.ring_pages[0], KM_USER0);
	if (likely(!list_empty(&ctx->free_reqs) &&
	    (ctx->reqs_active < aio_ring_avail(&ctx->ring_info, ring)))) {
		req = list_kiocb(ctx->free_reqs.next);
		list_del(&req->list);
		list_add(&req->list, &ctx->active_reqs);
		ctx->reqs_active++;
		req->user_obj = NULL;
		get_ioctx(ctx);

		if (unlikely(req->ctx != NULL))
			BUG();
		req->ctx = ctx;
		if (unlikely(req->users))
			BUG();
		req->users = 1;
	}
	kunmap_atomic(ring, KM_USER0);
	spin_unlock_irq(&ctx->lock);

	return req;
}

static inline struct kiocb *aio_get_req(struct kioctx *ctx)
{
	struct kiocb *req;
	/* Handle a potential starvation case -- should be exceedingly rare as 
	 * requests will be stuck on fput_head only if the aio_fput_routine is 
	 * delayed and the requests were the last user of the struct file.
	 */
	req = __aio_get_req(ctx);
	if (unlikely(NULL == req)) {
		aio_fput_routine(NULL);
		req = __aio_get_req(ctx);
	}
	return req;
}

static inline void really_put_req(struct kioctx *ctx, struct kiocb *req)
{
	req->ctx = NULL;
	req->filp = NULL;
	req->user_obj = NULL;
	ctx->reqs_active--;
	list_add(&req->list, &ctx->free_reqs);

	if (unlikely(!ctx->reqs_active && ctx->dead))
		wake_up(&ctx->wait);
}

static void aio_fput_routine(void *data)
{
	spin_lock_irq(&fput_lock);
	while (likely(!list_empty(&fput_head))) {
		struct kiocb *req = list_kiocb(fput_head.next);
		struct kioctx *ctx = req->ctx;

		list_del(&req->list);
		spin_unlock_irq(&fput_lock);

		/* Complete the fput */
		__fput(req->filp);

		/* Link the iocb into the context's free list */
		spin_lock_irq(&ctx->lock);
		really_put_req(ctx, req);
		spin_unlock_irq(&ctx->lock);

		put_ioctx(ctx);
		spin_lock_irq(&fput_lock);
	}
	spin_unlock_irq(&fput_lock);
}

/* __aio_put_req
 *	Returns true if this put was the last user of the request.
 */
static inline int __aio_put_req(struct kioctx *ctx, struct kiocb *req)
{
	dprintk(KERN_DEBUG "aio_put(%p): f_count=%d\n",
		req, atomic_read(&req->filp->f_count));

	req->users --;
	if (unlikely(req->users < 0))
		BUG();
	if (likely(req->users))
		return 0;
	list_del(&req->list);		/* remove from active_reqs */
	req->cancel = NULL;

	/* Must be done under the lock to serialise against cancellation.
	 * Call this aio_fput as it duplicates fput via the fput_tqueue.
	 */
	if (unlikely(atomic_dec_and_test(&req->filp->f_count))) {
		get_ioctx(ctx);
		spin_lock(&fput_lock);
		list_add(&req->list, &fput_head);
		spin_unlock(&fput_lock);
		schedule_task(&fput_tqueue);
	} else
		really_put_req(ctx, req);
	return 1;
}

/* aio_put_req
 *	Returns true if this put was the last user of the kiocb,
 *	false if the request is still in use.
 */
int aio_put_req(struct kiocb *req)
{
	struct kioctx *ctx = req->ctx;
	int ret;
	spin_lock_irq(&ctx->lock);
	ret = __aio_put_req(ctx, req);
	spin_unlock_irq(&ctx->lock);
	if (ret)
		put_ioctx(ctx);
	return ret;
}

/*	Lookup an ioctx id.  ioctx_list is lockless for reads.
 *	FIXME: this is O(n) and is only suitable for development.
 */
struct kioctx *lookup_ioctx(unsigned long ctx_id)
{
	struct kioctx *ioctx;
	struct mm_struct *mm;

	br_read_lock(BR_AIO_REQ_LOCK);
	mm = current->mm;
	for (ioctx = mm->ioctx_list; ioctx; ioctx = ioctx->next)
		if (likely(ioctx->user_id == ctx_id && !ioctx->dead)) {
			get_ioctx(ioctx);
			break;
		}
	br_read_unlock(BR_AIO_REQ_LOCK);

	return ioctx;
}

/* aio_complete
 *	Called when the io request on the given iocb is complete.
 *	Returns true if this is the last user of the request.  The 
 *	only other user of the request can be the cancellation code.
 */
int aio_complete(struct kiocb *iocb, long res, long res2)
{
	struct kioctx	*ctx = iocb->ctx;
	struct aio_ring_info	*info = &ctx->ring_info;
	struct aio_ring	*ring;
	struct io_event	*event;
	unsigned long	flags;
	unsigned long	tail;
	int		ret;

	/* add a completion event to the ring buffer.
	 * must be done holding ctx->lock to prevent
	 * other code from messing with the tail
	 * pointer since we might be called from irq
	 * context.
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	ring = kmap_atomic(info->ring_pages[0], KM_IRQ1);

	tail = info->tail;
	event = aio_ring_event(info, tail, KM_IRQ0);
	tail = (tail + 1) % info->nr;

	event->obj = (u64)(unsigned long)iocb->user_obj;
	event->data = iocb->user_data;
	event->res = res;
	event->res2 = res2;

	dprintk("aio_complete: %p[%lu]: %p: %p %Lx %lx %lx\n",
		ctx, tail, iocb, iocb->user_obj, iocb->user_data, res, res2);

	/* after flagging the request as done, we
	 * must never even look at it again
	 */
	wmb();  /* prevent the tail update from becoming visible before
		 * the event */

	info->tail = tail;
	ring->tail = tail;

	wmb();

	put_aio_ring_event(event, KM_IRQ0);
	kunmap_atomic(ring, KM_IRQ1);

	pr_debug("added to ring %p at [%lu]\n", iocb, tail);

	/* everything turned out well, dispose of the aiocb. */
	ret = __aio_put_req(ctx, iocb);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (waitqueue_active(&ctx->wait))
		wake_up(&ctx->wait);

	if (ret)
		put_ioctx(ctx);

	return ret;
}

/* aio_read_evt
 *	Pull an event off of the ioctx's event ring.  Returns the number of 
 *	events fetched (0 or 1 ;-)
 *	FIXME: make this use cmpxchg.
 *	TODO: make the ringbuffer user mmap()able (requires FIXME).
 */
static int aio_read_evt(struct kioctx *ioctx, struct io_event *ent)
{
	struct aio_ring_info *info = &ioctx->ring_info;
	struct aio_ring *ring;
	unsigned long head;
	int ret = 0;

	ring = kmap_atomic(info->ring_pages[0], KM_USER0);
	dprintk("in aio_read_evt h%lu t%lu m%lu\n",
		 (unsigned long)ring->head, (unsigned long)ring->tail,
		 (unsigned long)ring->nr);
	barrier();
	if (ring->head == ring->tail)
		goto out;

	spin_lock(&info->ring_lock);	/* implicite rmb() */

	head = ring->head % info->nr;
	if (head != ring->tail) {
		struct io_event *evp = aio_ring_event(info, head, KM_USER1);
		*ent = *evp;
		head = (head + 1) % info->nr;
		/* the update of head must occur after we read the event */
		mb();
		ring->head = head;
		ret = 1;
		put_aio_ring_event(evp, KM_USER1);
	}
	spin_unlock(&info->ring_lock);

out:
	kunmap_atomic(ring, KM_USER0);
	dprintk("leaving aio_read_evt: %d  h%lu t%lu\n", ret,
		 (unsigned long)ring->head, (unsigned long)ring->tail);
	return ret;
}

struct timeout {
	struct timer_list	timer;
	int			timed_out;
	struct task_struct	*p;
};

static void timeout_func(unsigned long data)
{
	struct timeout *to = (struct timeout *)data;

	to->timed_out = 1;
	wake_up_process(to->p);
}

static inline void init_timeout(struct timeout *to)
{
	init_timer(&to->timer);
	to->timer.data = (unsigned long)to;
	to->timer.function = timeout_func;
	to->timed_out = 0;
	to->p = current;
}

static inline void set_timeout(struct timeout *to, const struct timespec *ts)
{
	unsigned long how_long;

	if (!ts->tv_sec && !ts->tv_nsec) {
		to->timed_out = 1;
		return;
	}

	how_long = ts->tv_sec * HZ;
#define HZ_NS (1000000000 / HZ)
	how_long += (ts->tv_nsec + HZ_NS - 1) / HZ_NS;
	
	to->timer.expires = jiffies + how_long;
	add_timer(&to->timer);
}

static inline void update_ts(struct timespec *ts, long jiffies)
{
	struct timespec tmp;
	jiffies_to_timespec(jiffies, &tmp);
	ts->tv_sec -= tmp.tv_sec;
	ts->tv_nsec -= tmp.tv_nsec;
	if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000;
		ts->tv_sec -= 1;
	}
	if (ts->tv_sec < 0)
		ts->tv_sec = ts->tv_nsec = 0;
}

static inline void clear_timeout(struct timeout *to)
{
	del_timer_sync(&to->timer);
}

static int read_events(struct kioctx *ctx,
			long min_nr, long nr, 
			struct io_event *event,
			struct timespec *timeout)
{
	long			start_jiffies = jiffies;
	struct task_struct	*tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	int			ret;
	int			i = 0;
	struct io_event		ent;
	struct timeout		to;
	struct timespec		ts;

	/* needed to zero any padding within an entry (there shouldn't be 
	 * any, but C is fun!
	 */
	memset(&ent, 0, sizeof(ent));
	ret = 0;

	while (likely(i < nr)) {
		ret = aio_read_evt(ctx, &ent);
		if (unlikely(ret <= 0))
			break;

		dprintk("read event: %Lx %Lx %Lx %Lx\n",
			ent.data, ent.obj, ent.res, ent.res2);

		/* FIXME: split checks in two */
		ret = -EFAULT;
		if (unlikely(copy_to_user(event, &ent, sizeof(ent)))) {
			dprintk("aio: lost an event due to EFAULT.\n");
			break;
		}
		ret = 0;

		/* Good, event copied to userland, update counts. */
		event ++;
		i ++;
	}

	if (i && min_nr <= i)
		return i;
	if (ret)
		return i ? i : ret;

	/* End fast path */

	init_timeout(&to);
	if (timeout) {
		ret = -EFAULT;
		if (unlikely(copy_from_user(&ts, timeout, sizeof(ts))))
			goto out;

		set_timeout(&to, &ts);
		if (to.timed_out) {
			timeout = 0;
			clear_timeout(&to);
		}
	}

	while (likely(i < nr)) {
		add_wait_queue_exclusive_lifo(&ctx->wait, &wait);
		do {
			set_task_state(tsk, TASK_INTERRUPTIBLE);

			ret = aio_read_evt(ctx, &ent);
			if (ret)
				break;
			if (i && min_nr <= i)
				break;
			ret = 0;
			if (to.timed_out)	/* Only check after read evt */
				break;
			schedule();
			if (signal_pending(tsk)) {
				ret = -EINTR;
				break;
			}
			/*ret = aio_read_evt(ctx, &ent);*/
		} while (1) ;

		set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&ctx->wait, &wait);

		if (unlikely(ret <= 0))
			break;

		ret = -EFAULT;
		if (unlikely(copy_to_user(event, &ent, sizeof(ent)))) {
			dprintk("aio: lost an event due to EFAULT.\n");
			break;
		}

		/* Good, event copied to userland, update counts. */
		event ++;
		i ++;
	}

	if (timeout) {
		clear_timeout(&to);
		update_ts(&ts, jiffies - start_jiffies);
		if (copy_to_user(timeout, &ts, sizeof(ts)))
			ret = -EFAULT;
	}
out:
	return i ? i : ret;
}

/* Take an ioctx and remove it from the list of ioctx's.  Protects 
 * against races with itself via ->dead.
 */
static void io_destroy(struct kioctx *ioctx)
{
	struct kioctx **tmp;
	int was_dead;

	/* delete the entry from the list is someone else hasn't already */
	br_write_lock(BR_AIO_REQ_LOCK);
	was_dead = ioctx->dead;
	ioctx->dead = 1;
	for (tmp = &current->mm->ioctx_list; *tmp && *tmp != ioctx;
	     tmp = &(*tmp)->next)
		;
	if (*tmp)
		*tmp = ioctx->next;
	br_write_unlock(BR_AIO_REQ_LOCK);

	dprintk("aio_release(%p)\n", ioctx);
	if (likely(!was_dead))
		put_ioctx(ioctx);	/* twice for the list */

	aio_cancel_all(ioctx);
	wait_for_all_aios(ioctx);
	put_ioctx(ioctx);	/* once for the lookup */
}

asmlinkage long sys_io_setup(unsigned nr_reqs, aio_context_t *ctxp)
{
	struct kioctx *ioctx = NULL;
	unsigned long ctx;
	long ret;

	ret = get_user(ctx, ctxp);
	if (unlikely(ret))
		goto out;

	ret = -EINVAL;
	if (unlikely(ctx || !nr_reqs || (int)nr_reqs < 0)) {
		pr_debug("EINVAL: io_setup: ctx or nr_reqs > max\n");
		goto out;
	}

	ret = -EAGAIN;
	if (unlikely(nr_reqs > max_aio_reqs))
		goto out;

	ioctx = ioctx_alloc(nr_reqs);
	ret = PTR_ERR(ioctx);
	if (!IS_ERR(ioctx)) {
		ret = put_user(ioctx->user_id, ctxp);
		if (!ret)
			return 0;

		get_ioctx(ioctx); /* io_destroy() expects us to hold a ref */
		io_destroy(ioctx);
	}

out:
	return ret;
}

/* aio_release
 *	Release the kioctx associated with the userspace handle.
 */
asmlinkage long sys_io_destroy(aio_context_t ctx)
{
	struct kioctx *ioctx = lookup_ioctx(ctx);
	if (likely(NULL != ioctx)) {
		io_destroy(ioctx);
		return 0;
	}
	pr_debug("EINVAL: io_destroy: invalid context id\n");
	return -EINVAL;
}

int io_submit_one (struct kioctx *ctx, struct iocb *user_iocb,
		   struct iocb *iocb)
{
	struct kiocb *req;
	struct file *file;
	int ret = 0;
	ssize_t (*op)(struct file *, struct kiocb *, struct iocb *);

	/* enforce forwards compatibility on users */
	if (unlikely(iocb->aio_reserved1 || iocb->aio_reserved2 ||
		     iocb->aio_reserved3)) {
		pr_debug("EINVAL: io_submit: reserve field set\n");
		return -EINVAL;
	}

	/* prevent overflows */
	if (unlikely(
	    (iocb->aio_buf != (unsigned long)iocb->aio_buf) ||
	    (iocb->aio_nbytes != (size_t)iocb->aio_nbytes) ||
	    ((ssize_t)iocb->aio_nbytes < 0)
	    )) {
	  	pr_debug("EINVAL: io_submit: overflow check\n");
		return -EINVAL;
	}

	file = fget(iocb->aio_fildes);
	if (unlikely(!file))
	  	return -EBADF;

	req = aio_get_req(ctx);
	if (unlikely(!req)) {
	  	fput(file);
		return -EAGAIN;
	}

	req->filp = file;
	iocb->aio_key = req->key;
	ret = put_user(iocb->aio_key, &user_iocb->aio_key);
	if (unlikely(ret)) {
	  	dprintk("EFAULT: aio_key\n");
		goto out_put_req;
	}

	req->user_obj = user_iocb;
	req->user_data = iocb->aio_data;
	req->buf = iocb->aio_buf;
	req->pos = iocb->aio_offset;
	req->size = iocb->aio_nbytes;
	req->nr_transferred = 0;
	req->rlim_fsize = current->rlim[RLIMIT_FSIZE].rlim_cur;

	ret = -EBADF;
	if (IOCB_CMD_PREAD == iocb->aio_lio_opcode) {
	  	op = file->f_op->aio_read;
		if (unlikely(!(file->f_mode & FMODE_READ)))
		  	goto out_put_req;
	} else if (IOCB_CMD_PREADX == iocb->aio_lio_opcode) {
	  	op = file->f_op->aio_readx;
		if (unlikely(!(file->f_mode & FMODE_READ)))
		  	goto out_put_req;
	} else if (IOCB_CMD_PWRITE == iocb->aio_lio_opcode) {
		op = file->f_op->aio_write;
		if (unlikely(!(file->f_mode & FMODE_WRITE)))
			goto out_put_req;
	} else if (IOCB_CMD_FSYNC == iocb->aio_lio_opcode) {
	  	op = file->f_op->aio_fsync;
	} else if (IOCB_CMD_POLL == iocb->aio_lio_opcode) {
		op = generic_aio_poll;
	} else
	  	op = NULL;

	if (unlikely(!op)) {
	  	dprintk("EINVAL: io_submit: no operation provided\n");
		ret = -EINVAL;
		goto out_put_req;
	}

	ret = op(file, req, iocb);
	if (likely(!ret))
	  	return 0;

	pr_debug("io_submit: op returned %ld\n", ret);
	aio_complete(req, ret, 0);
	return 0;		/* A completion event was sent, so 
				 * submit is a success. */
out_put_req:
	aio_put_req(req);
	return ret;
}

ssize_t generic_aio_poll(struct file *file, struct kiocb *req, struct iocb *iocb)
{
	unsigned events = iocb->aio_buf;

	/* Did the user set any bits they weren't supposed to? (The
	 * above is actually a cast.
	 */
	if (unlikely(events != iocb->aio_buf))
		return -EINVAL;

	return async_poll(req, events);
}

/* sys_io_submit
 *	Copy an aiocb from userspace into kernel space, then convert it to
 *	a kiocb, submit and repeat until done.  Error codes on copy/submit
 *	only get returned for the first aiocb copied as otherwise the size
 *	of aiocbs copied is returned (standard write sematics).
 */
asmlinkage long sys_io_submit(aio_context_t ctx_id, long nr, 
			      struct iocb **iocbpp)
{
	struct kioctx *ctx;
	long ret = 0;
	int i;

	if (unlikely(nr < 0))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_READ, iocbpp, (nr*sizeof(*iocbpp)))))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx)) {
		pr_debug("EINVAL: io_submit: invalid context id\n");
		return -EINVAL;
	}

	for (i=0; i<nr; i++) {
		struct iocb *user_iocb, tmp;

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

static void generic_aio_next_chunk(void *_iocb)
{
	int (*kvec_op)(struct file *, kvec_cb_t, size_t, loff_t);
	struct kiocb *iocb = _iocb;
	int rw = iocb->this_size;
	unsigned long buf = iocb->buf;
	unsigned long old_fsize;
	kvec_cb_t cb;
	ssize_t res;

	iocb->this_size = iocb->size - iocb->nr_transferred;
	if (iocb->this_size > aio_max_size)
		iocb->this_size = aio_max_size;

	buf += iocb->nr_transferred;
	cb.vec = mm_map_user_kvec(iocb->ctx->mm, rw, buf, iocb->this_size);
	cb.fn = (rw == READ) ? generic_aio_complete_read
			     : generic_aio_complete_write;
	cb.data = iocb;

	dprintk("generic_aio_rw: cb.vec=%p\n", cb.vec);
	if (unlikely(IS_ERR(cb.vec)))
		goto done;

	old_fsize = current->rlim[RLIMIT_FSIZE].rlim_cur;
	current->rlim[RLIMIT_FSIZE].rlim_cur = iocb->rlim_fsize;
	kvec_op = (rw == READ) ? iocb->filp->f_op->kvec_read
			       : iocb->filp->f_op->kvec_write;
	dprintk("submit: %d %d %d\n", iocb->this_size, iocb->nr_transferred, iocb->size);
	res = kvec_op(iocb->filp, cb, iocb->this_size,
		      iocb->pos + iocb->nr_transferred);
	current->rlim[RLIMIT_FSIZE].rlim_cur = old_fsize;
	if (!res) {
		dprintk("submit okay\n");
		return;
	}
	dprintk("submit failed: %d\n", res);
	
	cb.fn(cb.data, cb.vec, res);
	return;

done:
	if (unlikely(!iocb->nr_transferred))
		BUG();
	aio_complete(iocb, iocb->nr_transferred, 0);
}

static void generic_aio_complete_rw(int rw, void *_iocb, struct kvec *vec, ssize_t res)
{
	struct kiocb *iocb = _iocb;

	unmap_kvec(vec, rw == READ);
	free_kvec(vec);

	if (res > 0)
		iocb->nr_transferred += res;

	/* Was this chunk successful?  Is there more left to transfer? */
	if (!iocb->ctx->dead &&
	    res == iocb->this_size && iocb->nr_transferred < iocb->size) {
		/* We may be in irq context, so queue processing in 
		 * process context.
		 */
		iocb->this_size = rw;
		INIT_TQUEUE(&iocb->u.tq, generic_aio_next_chunk, iocb);
		schedule_task(&iocb->u.tq);
		return;
	}

	aio_complete(iocb, iocb->nr_transferred ? iocb->nr_transferred : res,
		     0);
}

static void generic_aio_complete_read(void *_iocb, struct kvec *vec, ssize_t res)
{
	generic_aio_complete_rw(READ, _iocb, vec, res);
}

static void generic_aio_complete_write(void *_iocb, struct kvec *vec, ssize_t res)
{
	generic_aio_complete_rw(WRITE, _iocb, vec, res);
}

ssize_t generic_aio_rw(int rw, struct file *file, struct kiocb *req, struct iocb *iocb, size_t min_size)
{
	int (*kvec_op)(struct file *, kvec_cb_t, size_t, loff_t);
	unsigned long buf = iocb->aio_buf;
	size_t size = iocb->aio_nbytes;
	loff_t pos = iocb->aio_offset;
	kvec_cb_t cb;
	ssize_t res;

	req->nr_transferred = 0;
	if (size > aio_max_size)
		/* We have to split up the request.  Pin the mm
		 * struct for further use with map_user_kvec later.
		 */
		size = aio_max_size;
	else
		req->buf = 0;

	req->this_size = size;

	cb.vec = map_user_kvec(rw, buf, size);
	cb.fn = (rw == READ) ? generic_aio_complete_read
			     : generic_aio_complete_write;
	cb.data = req;

	dprintk("generic_aio_rw: cb.vec=%p\n", cb.vec);
	if (IS_ERR(cb.vec))
		return PTR_ERR(cb.vec);

	kvec_op = (rw == READ) ? file->f_op->kvec_read : file->f_op->kvec_write;

	res = kvec_op(file, cb, size, pos);
	if (unlikely(res != 0)) {
		/* If the first chunk was successful, we have to run
		 * the callback to attempt the rest of the io.
		 */
		if (res == size && req->buf) {
			cb.fn(cb.data, cb.vec, res);
			return 0;
		}

		unmap_kvec(cb.vec, rw == READ);
		free_kvec(cb.vec);
	}
	return res;
}

inline ssize_t generic_file_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb)
{
	return generic_aio_rw(READ, file, req, iocb, iocb->aio_nbytes);  
}

inline ssize_t generic_sock_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb)
{
	return generic_aio_rw(READ, file, req, iocb, 1);
}

inline ssize_t generic_aio_write(struct file *file, struct kiocb *req, struct iocb *iocb, size_t min_size)
{
	return generic_aio_rw(WRITE, file, req, iocb, 1);
}

inline ssize_t generic_file_aio_write(struct file *file, struct kiocb *req, struct iocb *iocb)
{
	return generic_aio_write(file, req, iocb, iocb->aio_nbytes);	
}

/* lookup_kiocb
 *	Finds a given iocb for cancellation.
 *	MUST be called with ctx->lock held.
 */
struct kiocb *lookup_kiocb(struct kioctx *ctx, struct iocb *iocb, u32 key)
{
	struct list_head *pos;
	/* TODO: use a hash or array, this sucks. */
	list_for_each(pos, &ctx->active_reqs) {
		struct kiocb *kiocb = list_kiocb(pos);
		if (kiocb->user_obj == iocb && kiocb->key == key)
			return kiocb;
	}
	return NULL;
}

asmlinkage long sys_io_cancel(aio_context_t ctx_id, struct iocb *iocb, struct io_event *result)
{
	int (*cancel)(struct kiocb *iocb, struct io_event *res);
	struct kioctx *ctx;
	struct kiocb *kiocb;
	u32 key;
	int ret;

	ret = get_user(key, &iocb->aio_key);
	if (unlikely(ret))
		return ret;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx))
		return -EINVAL;

	spin_lock_irq(&ctx->lock);
	ret = -EAGAIN;
	kiocb = lookup_kiocb(ctx, iocb, key);
	if (kiocb && kiocb->cancel) {
		cancel = kiocb->cancel;
		kiocb->users ++;
	} else
		cancel = NULL;
	spin_unlock_irq(&ctx->lock);

	if (NULL != cancel) {
		struct io_event tmp;
		memset(&tmp, 0, sizeof(tmp));
		tmp.obj = (u64)(unsigned long)kiocb->user_obj;
		tmp.data = kiocb->user_data;
		ret = cancel(kiocb, &tmp);
		if (!ret) {
			/* Cancellation succeeded -- copy the result
			 * into the user's buffer.
			 */
			if (copy_to_user(result, &tmp, sizeof(tmp)))
				ret = -EFAULT;
		}
	} else
		dprintk("iocb has no cancel operation\n");

	put_ioctx(ctx);

	return ret;
}

asmlinkage long sys_io_getevents(aio_context_t ctx_id,
				 long min_nr,
				 long nr,
				 struct io_event *events,
				 struct timespec *timeout)
{
	struct kioctx *ioctx = lookup_ioctx(ctx_id);
	long ret = -EINVAL;

	if (likely(NULL != ioctx)) {
		ret = read_events(ioctx, min_nr, nr, events, timeout);
		put_ioctx(ioctx);
	}

	return ret;
}

__initcall(aio_setup);

EXPORT_SYMBOL_GPL(generic_file_kvec_read);
EXPORT_SYMBOL_GPL(generic_file_aio_read);
EXPORT_SYMBOL_GPL(generic_file_kvec_write);
EXPORT_SYMBOL_GPL(generic_file_aio_write);
EXPORT_SYMBOL_GPL(aio_max_size);
EXPORT_SYMBOL_GPL(aio_put_req);
EXPORT_SYMBOL_GPL(aio_complete);
