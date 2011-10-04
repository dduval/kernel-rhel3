/*
 * iobuf.c
 *
 * Keep track of the general-purpose IO-buffer structures used to track
 * abstract kernel-space io buffers.
 * 
 */

#include <linux/slab.h>
#include <linux/iobuf.h>
#include <linux/vmalloc.h>
#include <linux/locks.h>
#include <linux/interrupt.h>

kmem_cache_t *kiobuf_cachep;

void end_kio_request(struct kiobuf *kiobuf, int uptodate)
{
	if ((!uptodate) && !kiobuf->errno)
		kiobuf->errno = -EIO;

	if (atomic_dec_and_test(&kiobuf->io_count)) {
		if (kiobuf->end_io)
			kiobuf->end_io(kiobuf);
		wake_up(&kiobuf->wait_queue);
	}
}

static int kiobuf_init(struct kiobuf *iobuf)
{
	int retval;

	init_waitqueue_head(&iobuf->wait_queue);
	iobuf->array_len = 0;
	iobuf->bh_len = 0;
	iobuf->nr_pages = 0;
	iobuf->locked = 0;
	iobuf->varyio = 0;
	iobuf->bh = NULL;
	iobuf->blocks = NULL;
	atomic_set(&iobuf->io_count, 0);
	iobuf->end_io = NULL;
	iobuf->initialized = 0;
	if (!in_interrupt()) {
		retval = expand_kiobuf(iobuf, KIO_STATIC_PAGES);
		if (retval)
			return retval;
		iobuf->initialized = 1;
	}
	return 0;
}

int alloc_kiobuf_bhs(struct kiobuf * kiobuf, int nr, int gfp_mask)
{
	int i;
	struct buffer_head **new_bh;
	if (kiobuf->bh_len >= nr)
		return 0;
	
	new_bh = kmalloc(sizeof(*new_bh) * nr, gfp_mask);
	if (unlikely(!new_bh))
		goto nomem;

	/* Copy any existing bh'es into the new vector */
	if (kiobuf->bh_len) {
		memcpy(new_bh, kiobuf->bh, 
		       kiobuf->bh_len * sizeof(*new_bh));
	}

	for (i = kiobuf->bh_len; i < nr; i++) {
		new_bh[i] = kmem_cache_alloc(bh_cachep, gfp_mask);
		if (unlikely(!new_bh[i]))
			goto nomem2;
	}

	kfree(kiobuf->bh);
	kiobuf->bh = new_bh;
	kiobuf->bh_len = nr;
	return 0;

nomem2:
	while (i-- >= kiobuf->bh_len)
		kmem_cache_free(bh_cachep, new_bh[i]);
	kfree(new_bh);
nomem:
	return -ENOMEM;
}

void free_kiobuf_bhs(struct kiobuf * kiobuf)
{
	int i;

	if (kiobuf->bh) {
		for (i = 0; i < kiobuf->bh_len; i++)
			if (kiobuf->bh[i])
				kmem_cache_free(bh_cachep, kiobuf->bh[i]);
		kfree(kiobuf->bh);
		kiobuf->bh = NULL;
		kiobuf->bh_len = 0;
	}
}

void kiobuf_ctor(void * objp, kmem_cache_t * cachep, unsigned long flag)
{
	struct kiobuf * iobuf = (struct kiobuf *) objp;
	kiobuf_init(iobuf);
}

void kiobuf_dtor(void * objp, kmem_cache_t * cachep, unsigned long flag)
{
	struct kiobuf * iobuf = (struct kiobuf *) objp;
	if (iobuf->initialized) {
		kfree(iobuf->maplist);
		kfree(iobuf->blocks);
		free_kiobuf_bhs(iobuf);
	}
}

static spinlock_t kiobuf_lock = SPIN_LOCK_UNLOCKED;

struct kiobuf *kiobuf_cache_list = NULL;
int kiobuf_cache = 0;
int kiobuf_cache_max = 0;

int alloc_kiovec(int nr, struct kiobuf **bufp)
{
	int i;
	struct kiobuf *iobuf;
	unsigned long flags;
	
	for (i = 0; i < nr; i++) {
		spin_lock_irqsave(&kiobuf_lock, flags);
		if (kiobuf_cache) {
			iobuf = kiobuf_cache_list;
			kiobuf_cache_list = (struct kiobuf *)iobuf->end_io;
			iobuf->end_io = NULL;
			kiobuf_cache--;
			spin_unlock_irqrestore(&kiobuf_lock, flags);
		} else {
			spin_unlock_irqrestore(&kiobuf_lock, flags);
			iobuf = kmem_cache_alloc(kiobuf_cachep, GFP_KERNEL);
		}
		if (unlikely(!iobuf))
			goto nomem;
		if (unlikely(!iobuf->initialized)) {
			/* try again to complete previously failed ctor */
			if (unlikely(kiobuf_init(iobuf)))
				goto nomem2;
		}
		bufp[i] = iobuf;
	}
	
	return 0;

nomem2:
	kmem_cache_free(kiobuf_cachep, iobuf);
nomem:
	free_kiovec(i, bufp);
	return -ENOMEM;
}

void free_kiovec(int nr, struct kiobuf **bufp) 
{
	int i;
	struct kiobuf *iobuf;
	unsigned long flags;
	
	for (i = 0; i < nr; i++) {
		iobuf = bufp[i];
		init_waitqueue_head(&iobuf->wait_queue);
		iobuf->io_count.counter = 0;
		iobuf->end_io = NULL;
		if (kiobuf_cache_max) {
			spin_lock_irqsave(&kiobuf_lock, flags);
			if (kiobuf_cache < kiobuf_cache_max) {
				iobuf->end_io =
				  (void (*)(struct kiobuf *))kiobuf_cache_list;
				kiobuf_cache_list = iobuf;
				kiobuf_cache++;
				spin_unlock_irqrestore(&kiobuf_lock, flags);
			} else {
				spin_unlock_irqrestore(&kiobuf_lock, flags);
				kiobuf_dtor(iobuf, kiobuf_cachep, 0);
				kiobuf_ctor(iobuf, kiobuf_cachep, 0);
				kmem_cache_free(kiobuf_cachep, iobuf);
			}
		} else
			kmem_cache_free(kiobuf_cachep, iobuf);
	}
}

#define SECTOR_SIZE 512
#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)

int expand_kiobuf(struct kiobuf *iobuf, int wanted)
{
	struct page ** maplist;
	unsigned long *blocks;
	
	if (iobuf->array_len >= wanted)
		return 0;
	
	maplist = kmalloc(wanted * sizeof(struct page **), GFP_KERNEL);
	if (unlikely(!maplist))
		return -ENOMEM;
	blocks = kmalloc(wanted * SECTORS_PER_PAGE * sizeof(unsigned long), 
			 GFP_KERNEL);
	if (unlikely(!blocks)) {
		kfree(maplist);
		return -ENOMEM;
	}
	
	/* Did it grow while we waited? */
	if (unlikely(iobuf->array_len >= wanted)) {
		kfree(maplist);
		kfree(blocks);
		return 0;
	}

	if (iobuf->array_len) {
		memcpy(maplist, iobuf->maplist, 
		       iobuf->array_len * sizeof(*maplist));
		memcpy(blocks, iobuf->blocks,
		       iobuf->array_len * sizeof(*blocks) * SECTORS_PER_PAGE);
		kfree(iobuf->maplist);
		kfree(iobuf->blocks);
	}
	
	iobuf->maplist   = maplist;
	iobuf->blocks    = blocks;
	iobuf->array_len = wanted;
	return 0;
}

void kiobuf_wait_for_io(struct kiobuf *kiobuf)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	if (atomic_read(&kiobuf->io_count) == 0)
		return;

	add_wait_queue(&kiobuf->wait_queue, &wait);
repeat:
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	if (atomic_read(&kiobuf->io_count) != 0) {
		run_task_queue(&tq_disk);
		io_schedule();
		if (atomic_read(&kiobuf->io_count) != 0)
			goto repeat;
	}
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&kiobuf->wait_queue, &wait);
}

void __init iobuf_cache_init(void)
{
	kiobuf_cachep = kmem_cache_create("kiobuf", sizeof(struct kiobuf),
					  0, SLAB_HWCACHE_ALIGN, kiobuf_ctor, kiobuf_dtor);
	if (!kiobuf_cachep)
		panic("Cannot create kiobuf SLAB cache");
}
