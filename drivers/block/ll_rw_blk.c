/*
 *  linux/drivers/block/ll_rw_blk.c
 *
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au> -  July2000
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/bootmem.h>

#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/module.h>

/*
 * MAC Floppy IWM hooks
 */

#ifdef CONFIG_MAC_FLOPPY_IWM
extern int mac_floppy_init(void);
#endif

/*
 * For the allocated request tables
 */
static kmem_cache_t *request_cachep;

/*
 * The "disk" task queue is used to start the actual requests
 * after a plug
 */
DECLARE_TASK_QUEUE(tq_disk);

/*
 * Protect the request list against multiple users..
 *
 * With this spinlock the Linux block IO subsystem is 100% SMP threaded
 * from the IRQ event side, and almost 100% SMP threaded from the syscall
 * side (we still have protect against block device array operations, and
 * the do_request() side is casually still unsafe. The kernel lock protects
 * this part currently.).
 *
 * there is a fair chance that things will work just OK if these functions
 * are called with no global kernel lock held ...
 */
spinlock_t io_request_lock = SPIN_LOCK_UNLOCKED;

/* This specifies how many sectors to read ahead on the disk. */

int read_ahead[MAX_BLKDEV];

/* blk_dev_struct is:
 *	*request_fn
 *	*current_request
 */
struct blk_dev_struct blk_dev[MAX_BLKDEV]; /* initialized by blk_dev_init() */

/*
 * blk_size contains the size of all block-devices in units of 1024 byte
 * sectors:
 *
 * blk_size[MAJOR][MINOR]
 *
 * if (!blk_size[MAJOR]) then no minor size checking is done.
 */
int * blk_size[MAX_BLKDEV];

/*
 * blksize_size contains the size of all block-devices:
 *
 * blksize_size[MAJOR][MINOR]
 *
 * if (!blksize_size[MAJOR]) then 1024 bytes is assumed.
 */
int * blksize_size[MAX_BLKDEV];

/*
 * hardsect_size contains the size of the hardware sector of a device.
 *
 * hardsect_size[MAJOR][MINOR]
 *
 * if (!hardsect_size[MAJOR])
 *		then 512 bytes is assumed.
 * else
 *		sector_size is hardsect_size[MAJOR][MINOR]
 * This is currently set by some scsi devices and read by the msdos fs driver.
 * Other uses may appear later.
 */
int * hardsect_size[MAX_BLKDEV];

/*
 * The following tunes the read-ahead algorithm in mm/filemap.c
 */
int * max_readahead[MAX_BLKDEV];

/*
 * Max number of sectors per request
 */
int * max_sectors[MAX_BLKDEV];

/*
 * blkdev_varyio indicates if variable size IO can be done on a device.
 *
 * Currently used for doing variable size IO on RAW devices.
 */
char * blkdev_varyio[MAX_BLKDEV];

unsigned long blk_max_low_pfn, blk_max_pfn;
int blk_nohighio = 0;

static inline int get_max_sectors(kdev_t dev)
{
	if (!max_sectors[MAJOR(dev)])
		return MAX_SECTORS;
	return max_sectors[MAJOR(dev)][MINOR(dev)];
}

inline request_queue_t *blk_get_queue(kdev_t dev)
{
	struct blk_dev_struct *bdev = blk_dev + MAJOR(dev);

	if (bdev->queue)
		return bdev->queue(dev);
	else
		return &blk_dev[MAJOR(dev)].request_queue;
}

static int __blk_cleanup_queue(struct request_list *list)
{
	struct list_head *head = &list->free;
	struct request *rq;
	int i = 0;

	while (!list_empty(head)) {
		rq = list_entry(head->next, struct request, queue);
		list_del(&rq->queue);
		kmem_cache_free(request_cachep, rq);
		i++;
	};

	if (i != list->count)
		printk("request list leak!\n");

	list->count = 0;
	return i;
}

/**
 * blk_cleanup_queue: - release a &request_queue_t when it is no longer needed
 * @q:    the request queue to be released
 *
 * Description:
 *     blk_cleanup_queue is the pair to blk_init_queue().  It should
 *     be called when a request queue is being released; typically
 *     when a block device is being de-registered.  Currently, its
 *     primary task it to free all the &struct request structures that
 *     were allocated to the queue.
 * Caveat: 
 *     Hopefully the low level driver will have finished any
 *     outstanding requests first...
 **/
void blk_cleanup_queue(request_queue_t * q)
{
	int count = q->nr_requests;

	count -= __blk_cleanup_queue(&q->rq[READ]);
	count -= __blk_cleanup_queue(&q->rq[WRITE]);

	if (count)
		printk("blk_cleanup_queue: leaked requests (%d)\n", count);

	memset(q, 0, sizeof(*q));
}

/**
 * blk_queue_superbh - indicate whether queue can accept superbhs or not
 * @q:       The queue which this applies to.
 * @superbh: Max size in sectors
 *
 **/
void __blk_queue_superbh(request_queue_t *q, int superbh, int limit)
{
	int i = 0;

	if ((superbh << 9) > limit)
		superbh = limit >> 9;

	/* Force it to be a power of 2 */
	if (superbh)
		for (i = 1; (i<<1) <= superbh; i <<= 1);

	q->superbh_queue = i << 9;
}

void blk_queue_superbh(request_queue_t *q, int sectors)
{
	__blk_queue_superbh(q, sectors, MAX_SUPERBH);
}

/* Allow a driver to request a larger superbh limit: we only expect
 * varyio-capable drivers to call this.
 *
 * Limit the larger superbh depending on the caller's scatter-gather
 * segment limit.  We determine the maximum size that a scatter- gather
 * list can be given the number of sg segments, and use that as an upper
 * bound for the superbh.  In no cases do we force the superbh to be
 * below the old limit of MAX_SUPERBH due to this new bound.
 */
void blk_queue_large_superbh(request_queue_t *q, int sectors, int segments)
{
	int seg_sectors = (PAGE_SIZE / 512) * segments + 1;
	if (sectors > seg_sectors && seg_sectors > (MAX_SUPERBH >> 9))
		sectors = seg_sectors;
	__blk_queue_superbh(q, sectors, MAX_SUPERBH_NOBOUNCE);
}

/**
 * blk_queue_headactive - indicate whether head of request queue may be active
 * @q:       The queue which this applies to.
 * @active:  A flag indication where the head of the queue is active.
 *
 * Description:
 *    The driver for a block device may choose to leave the currently active
 *    request on the request queue, removing it only when it has completed.
 *    The queue handling routines assume this by default for safety reasons
 *    and will not involve the head of the request queue in any merging or
 *    reordering of requests when the queue is unplugged (and thus may be
 *    working on this particular request).
 *
 *    If a driver removes requests from the queue before processing them, then
 *    it may indicate that it does so, there by allowing the head of the queue
 *    to be involved in merging and reordering.  This is done be calling
 *    blk_queue_headactive() with an @active flag of %0.
 *
 *    If a driver processes several requests at once, it must remove them (or
 *    at least all but one of them) from the request queue.
 *
 *    When a queue is plugged the head will be assumed to be inactive.
 **/
 
void blk_queue_headactive(request_queue_t * q, int active)
{
	q->head_active = active;
}

/**
 * blk_queue_make_request - define an alternate make_request function for a device
 * @q:  the request queue for the device to be affected
 * @mfn: the alternate make_request function
 *
 * Description:
 *    The normal way for &struct buffer_heads to be passed to a device
 *    driver is for them to be collected into requests on a request
 *    queue, and then to allow the device driver to select requests
 *    off that queue when it is ready.  This works well for many block
 *    devices. However some block devices (typically virtual devices
 *    such as md or lvm) do not benefit from the processing on the
 *    request queue, and are served best by having the requests passed
 *    directly to them.  This can be achieved by providing a function
 *    to blk_queue_make_request().
 *
 * Caveat:
 *    The driver that does this *must* be able to deal appropriately
 *    with buffers in "highmemory", either by calling bh_kmap() to get
 *    a kernel mapping, to by calling create_bounce() to create a
 *    buffer in normal memory.
 **/

void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	q->make_request_fn = mfn;

	/*
	 * clear this, if queue can take a superbh it needs to enable this
	 * manually
	 */
	blk_queue_superbh(q, 0);
}

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q:  the request queue for the device
 * @dma_addr:   bus address limit
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @page. By default
 *    the block layer sets this to the highest numbered "low" memory page.
 **/
void blk_queue_bounce_limit(request_queue_t *q, u64 dma_addr)
{
	unsigned long bounce_pfn = dma_addr >> PAGE_SHIFT;
	unsigned long mb = dma_addr >> 20;
	static request_queue_t *old_q;

	/*
	 * keep this for debugging for now...
	 */
	if (dma_addr != BLK_BOUNCE_HIGH && q != old_q) {
		old_q = q;
		printk(KERN_INFO "blk: queue %p, ", q);
		if (dma_addr == BLK_BOUNCE_ANY)
			printk("no I/O memory limit\n");
		else
			printk("I/O limit %luMb (mask 0x%Lx)\n", mb,
			       (long long) dma_addr);
	}

	q->bounce_pfn = bounce_pfn;
}


/*
 * can we merge the two segments, or do we need to start a new one?
 */
inline int blk_seg_merge_ok(struct buffer_head *bh, struct buffer_head *nxt)
{
	/*
	 * if bh and nxt are contigous and don't cross a 4g boundary, it's ok
	 */
	if (BH_CONTIG(bh, nxt) && BH_PHYS_4G(bh, nxt))
		return 1;

	return 0;
}

static inline int ll_new_segment(request_queue_t *q, struct request *req, int max_segments)
{
	if (req->nr_segments < max_segments) {
		req->nr_segments++;
		return 1;
	}
	return 0;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct buffer_head *bh, int max_segments)
{
	if (blk_seg_merge_ok(req->bhtail, bh))
		return 1;

	return ll_new_segment(q, req, max_segments);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct buffer_head *bh, int max_segments)
{
	if (blk_seg_merge_ok(bh, req->bh))
		return 1;

	return ll_new_segment(q, req, max_segments);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next, int max_segments)
{
	int total_segments = req->nr_segments + next->nr_segments;

	if (blk_seg_merge_ok(req->bhtail, next->bh))
		total_segments--;

	if (total_segments > max_segments)
		return 0;

	req->nr_segments = total_segments;
	return 1;
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue.
 * (and with the request spinlock acquired)
 */
static void generic_plug_device(request_queue_t *q, kdev_t dev)
{
	/*
	 * no need to replug device
	 */
	if (!list_empty(&q->queue_head) || q->plugged)
		return;

	q->plugged = 1;
	queue_task(&q->plug_tq, &tq_disk);
}

/*
 * remove the plug and let it rip..
 */
static inline void __generic_unplug_device(request_queue_t *q)
{
	if (q->plugged) {
		q->plugged = 0;
		if (!list_empty(&q->queue_head)) {
			if (q->last_request ==
			    blkdev_entry_next_request(&q->queue_head))
				q->last_request = NULL;
			q->request_fn(q);
		}
	}
}

void generic_unplug_device(void *data)
{
	request_queue_t *q = (request_queue_t *) data;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__generic_unplug_device(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/** blk_grow_request_list
 *  @q: The &request_queue_t
 *  @nr_requests: how many requests are desired
 *
 * More free requests are added to the queue's free lists, bringing
 * the total number of requests to @nr_requests.
 *
 * The requests are added equally to the request queue's read
 * and write freelists.
 *
 * This function can sleep.
 *
 * Returns the (new) number of requests which the queue has available.
 */
int blk_grow_request_list(request_queue_t *q, int nr_requests)
{
	unsigned long flags;
	/* Several broken drivers assume that this function doesn't sleep,
	 * this causes system hangs during boot.
	 * As a temporary fix, make the function non-blocking.
	 */
	spin_lock_irqsave(q->queue_lock, flags);
	while (q->nr_requests < nr_requests) {
		struct request *rq;
		int rw;

		rq = kmem_cache_alloc(request_cachep, SLAB_ATOMIC);
		if (rq == NULL)
			break;
		memset(rq, 0, sizeof(*rq));
		rq->rq_status = RQ_INACTIVE;
		rw = q->nr_requests & 1;
		list_add(&rq->queue, &q->rq[rw].free);
		q->rq[rw].count++;
		q->nr_requests++;
	}
	q->batch_requests = q->nr_requests / 4;
	if (q->batch_requests > 32)
		q->batch_requests = 32;
	spin_unlock_irqrestore(q->queue_lock, flags);
	return q->nr_requests;
}

static void blk_init_free_list(request_queue_t *q)
{
	struct sysinfo si;
	int megs;		/* Total memory, in megabytes */
	int nr_requests;

	INIT_LIST_HEAD(&q->rq[READ].free);
	INIT_LIST_HEAD(&q->rq[WRITE].free);
	q->rq[READ].count = 0;
	q->rq[WRITE].count = 0;
	q->nr_requests = 0;

	si_meminfo(&si);
	megs = si.totalram >> (20 - PAGE_SHIFT);
	nr_requests = (megs * 2) & ~15;	/* One per half-megabyte */
	if (nr_requests < 32)
		nr_requests = 32;
	if (nr_requests > 1024)
		nr_requests = 1024;
	blk_grow_request_list(q, nr_requests);

	init_waitqueue_head(&q->wait_for_requests[0]);
	init_waitqueue_head(&q->wait_for_requests[1]);
}

static int __make_request(request_queue_t * q, int rw, struct buffer_head * bh);

/**
 * blk_init_queue  - prepare a request queue for use with a block device
 * @q:    The &request_queue_t to be initialised
 * @rfn:  The function to be called to process requests that have been
 *        placed on the queue.
 *
 * Description:
 *    If a block device wishes to use the standard request handling procedures,
 *    which sorts requests and coalesces adjacent requests, then it must
 *    call blk_init_queue().  The function @rfn will be called when there
 *    are requests on the queue that need to be processed.  If the device
 *    supports plugging, then @rfn may not be called immediately when requests
 *    are available on the queue, but may be called at some time later instead.
 *    Plugged queues are generally unplugged when a buffer belonging to one
 *    of the requests on the queue is needed, or due to memory pressure.
 *
 *    @rfn is not required, or even expected, to remove all requests off the
 *    queue, but only as many as it can handle at a time.  If it does leave
 *    requests on the queue, it is responsible for arranging that the requests
 *    get dealt with eventually.
 *
 *    A global spin lock $io_request_lock must be held while manipulating the
 *    requests on the request queue.
 *
 *    The request on the head of the queue is by default assumed to be
 *    potentially active, and it is not considered for re-ordering or merging
 *    whenever the given queue is unplugged. This behaviour can be changed with
 *    blk_queue_headactive().
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup_queue() call
 *    when the block device is deactivated (such as at module unload).
 **/
void blk_init_queue(request_queue_t * q, request_fn_proc * rfn)
{
	INIT_LIST_HEAD(&q->queue_head);
	elevator_init(&q->elevator, ELEVATOR_LINUS);
	q->queue_lock		= &io_request_lock;
	blk_init_free_list(q);
	q->request_fn     	= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->make_request_fn	= __make_request;
	q->plug_tq.sync		= 0;
	q->plug_tq.routine	= &generic_unplug_device;
	q->plug_tq.data		= q;
	q->plugged        	= 0;
	q->last_request		= NULL;
	/*
	 * These booleans describe the queue properties.  We set the
	 * default (and most common) values here.  Other drivers can
	 * use the appropriate functions to alter the queue properties.
	 * as appropriate.
	 */
	q->plug_device_fn 	= generic_plug_device;
	q->head_active    	= 1;
	q->superbh_queue	= 0;
	q->max_segments		= MAX_SEGMENTS;

	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);
}

void blk_start_queue_timer(request_queue_t * q)
{
}

#define blkdev_free_rq(list) list_entry((list)->next, struct request, queue);
/*
 * Get a free request. io_request_lock must be held and interrupts
 * disabled on the way in.  Returns NULL if there are no free requests.
 */
static struct request *get_request(request_queue_t *q, int rw)
{
	struct request *rq = NULL;
	struct request_list *rl = q->rq + rw;

	if (!list_empty(&rl->free)) {
		rq = blkdev_free_rq(&rl->free);
		list_del(&rq->queue);
		rl->count--;
		rq->rq_status = RQ_ACTIVE;
		rq->cmd = rw;
		rq->special = NULL;
		rq->q = q;
	}

	return rq;
}

/*
 * Here's the request allocation design:
 *
 * 1: Blocking on request exhaustion is a key part of I/O throttling.
 * 
 * 2: We want to be `fair' to all requesters.  We must avoid starvation, and
 *    attempt to ensure that all requesters sleep for a similar duration.  Hence
 *    no stealing requests when there are other processes waiting.
 * 
 * 3: We also wish to support `batching' of requests.  So when a process is
 *    woken, we want to allow it to allocate a decent number of requests
 *    before it blocks again, so they can be nicely merged (this only really
 *    matters if the process happens to be adding requests near the head of
 *    the queue).
 * 
 * 4: We want to avoid scheduling storms.  This isn't really important, because
 *    the system will be I/O bound anyway.  But it's easy.
 * 
 *    There is tension between requirements 2 and 3.  Once a task has woken,
 *    we don't want to allow it to sleep as soon as it takes its second request.
 *    But we don't want currently-running tasks to steal all the requests
 *    from the sleepers.  We handle this with wakeup hysteresis around
 *    0 .. batch_requests and with the assumption that request taking is much,
 *    much faster than request freeing.
 * 
 * So here's what we do:
 * 
 *    a) A READA requester fails if free_requests < batch_requests
 * 
 *       We don't want READA requests to prevent sleepers from ever
 *       waking.  Note that READA is used extremely rarely - a few
 *       filesystems use it for directory readahead.
 * 
 *  When a process wants a new request:
 * 
 *    b) If free_requests == 0, the requester sleeps in FIFO manner.
 * 
 *    b) If 0 <  free_requests < batch_requests and there are waiters,
 *       we still take a request non-blockingly.  This provides batching.
 *
 *    c) If free_requests >= batch_requests, the caller is immediately
 *       granted a new request.
 * 
 *  When a request is released:
 * 
 *    d) If free_requests < batch_requests, do nothing.
 * 
 *    f) If free_requests >= batch_requests, wake up a single waiter.
 * 
 *   The net effect is that when a process is woken at the batch_requests level,
 *   it will be able to take approximately (batch_requests) requests before
 *   blocking again (at the tail of the queue).
 * 
 *   This all assumes that the rate of taking requests is much, much higher
 *   than the rate of releasing them.  Which is very true.
 *
 * -akpm, Feb 2002.
 */

static struct request *__get_request_wait(request_queue_t *q, int rw)
{
	register struct request *rq;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&q->wait_for_requests[rw], &wait);
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		generic_unplug_device(q);
		if (q->rq[rw].count == 0) {
			/*
			 * All we care about is not to stall if any request
			 * is released after we set TASK_UNINTERRUPTIBLE.
			 * This is the most efficient place to unplug the queue
			 * in case we hit the race and we can get the request
			 * without waiting.
			 */
			generic_unplug_device(q);
			io_schedule_timeout(HZ);
		}
		spin_lock_irq(q->queue_lock);
		rq = get_request(q, rw);
		spin_unlock_irq(q->queue_lock);
	} while (rq == NULL);
	remove_wait_queue(&q->wait_for_requests[rw], &wait);
	current->state = TASK_RUNNING;
	return rq;
}

/* RO fail safe mechanism */

static long ro_bits[MAX_BLKDEV][8];

int is_read_only(kdev_t dev)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return 0;
	return ro_bits[major][minor >> 5] & (1 << (minor & 31));
}

void set_device_ro(kdev_t dev,int flag)
{
	int minor,major;

	major = MAJOR(dev);
	minor = MINOR(dev);
	if (major < 0 || major >= MAX_BLKDEV) return;
	if (flag) ro_bits[major][minor >> 5] |= 1 << (minor & 31);
	else ro_bits[major][minor >> 5] &= ~(1 << (minor & 31));
}

inline void drive_stat_acct (kdev_t dev, int rw,
				unsigned long nr_sectors, int new_io)
{
	unsigned int major = MAJOR(dev);
	unsigned int index;

	index = disk_index(dev);
	if ((index >= DK_MAX_DISK) || (major >= DK_MAX_MAJOR))
		return;

	kstat_percpu[smp_processor_id()].dk_drive[major][index] += new_io;
	if (rw == READ) {
		kstat_percpu[smp_processor_id()].dk_drive_rio[major][index] += new_io;
		kstat_percpu[smp_processor_id()].dk_drive_rblk[major][index] += nr_sectors;
	} else if (rw == WRITE) {
		kstat_percpu[smp_processor_id()].dk_drive_wio[major][index] += new_io;
		kstat_percpu[smp_processor_id()].dk_drive_wblk[major][index] += nr_sectors;
	} else
		printk(KERN_ERR "drive_stat_acct: cmd not R/W?\n");
}

#ifdef CONFIG_BLK_STATS
/*
 * Return up to two hd_structs on which to do IO accounting for a given
 * request.
 *
 * On a partitioned device, we want to account both against the partition
 * and against the whole disk.
 */
static void locate_hd_struct(struct request *req, 
			     struct hd_struct **hd1,
			     struct hd_struct **hd2)
{
	struct gendisk *gd;

	*hd1 = NULL;
	*hd2 = NULL;
	
	gd = get_gendisk(req->rq_dev);
	if (gd && gd->part) {
		/* Mask out the partition bits: account for the entire disk */
		int devnr = MINOR(req->rq_dev) >> gd->minor_shift;
		int whole_minor = devnr << gd->minor_shift;

		*hd1 = &gd->part[whole_minor];
		if (whole_minor != MINOR(req->rq_dev))
			*hd2= &gd->part[MINOR(req->rq_dev)];
	}
}

/*
 * Round off the performance stats on an hd_struct.
 *
 * The average IO queue length and utilisation statistics are maintained
 * by observing the current state of the queue length and the amount of
 * time it has been in this state for.
 * Normally, that accounting is done on IO completion, but that can result
 * in more than a second's worth of IO being accounted for within any one
 * second, leading to >100% utilisation.  To deal with that, we do a
 * round-off before returning the results when reading /proc/partitions,
 * accounting immediately for all queue usage up to the current jiffies and
 * restarting the counters again.
 */
void disk_round_stats(struct hd_struct *hd)
{
	unsigned long now = jiffies;
	
	hd->aveq += (hd->ios_in_flight * (jiffies - hd->last_queue_change));
	hd->last_queue_change = now;

	if (hd->ios_in_flight)
		hd->io_ticks += (now - hd->last_idle_time);
	hd->last_idle_time = now;	
}

static inline void down_ios(struct hd_struct *hd)
{
	disk_round_stats(hd);	
	--hd->ios_in_flight;
}

static inline void up_ios(struct hd_struct *hd)
{
	disk_round_stats(hd);
	++hd->ios_in_flight;
}

static void account_io_start(struct hd_struct *hd, struct request *req,
			     int merge, int sectors)
{
	switch (req->cmd) {
	case READ:
		if (merge)
			hd->rd_merges++;
		hd->rd_sectors += sectors;
		break;
	case WRITE:
		if (merge)
			hd->wr_merges++;
		hd->wr_sectors += sectors;
		break;
	}
	if (!merge)
		up_ios(hd);
}

static void account_io_end(struct hd_struct *hd, struct request *req)
{
	unsigned long duration = jiffies - req->start_time;
	switch (req->cmd) {
	case READ:
		hd->rd_ticks += duration;
		hd->rd_ios++;
		break;
	case WRITE:
		hd->wr_ticks += duration;
		hd->wr_ios++;
		break;
	}
	down_ios(hd);
}

void req_new_io(struct request *req, int merge, int sectors)
{
	struct hd_struct *hd1, *hd2;

	locate_hd_struct(req, &hd1, &hd2);
	if (hd1)
		account_io_start(hd1, req, merge, sectors);
	if (hd2)
		account_io_start(hd2, req, merge, sectors);
}

void req_merged_io(struct request *req)
{
	struct hd_struct *hd1, *hd2;

	locate_hd_struct(req, &hd1, &hd2);
	if (hd1)
		down_ios(hd1);
	if (hd2)	
		down_ios(hd2);
}

void req_finished_io(struct request *req)
{
	struct hd_struct *hd1, *hd2;

	if (blk_fs_request(req)) {
		locate_hd_struct(req, &hd1, &hd2);
		if (hd1)
			account_io_end(hd1, req);
		if (hd2)
			account_io_end(hd2, req);
	}
}
EXPORT_SYMBOL(req_finished_io);
#endif /* CONFIG_BLK_STATS */

/*
 * add-request adds a request to the linked list.
 * io_request_lock is held and interrupts disabled, as we muck with the
 * request queue list.
 *
 * By this point, req->cmd is always either READ/WRITE, never READA,
 * which is important for drive_stat_acct() above.
 */
static inline void add_request(request_queue_t * q, struct request * req,
			       struct list_head *insert_here)
{
	drive_stat_acct(req->rq_dev, req->cmd, req->nr_sectors, 1);

	if (!q->plugged && q->head_active && insert_here == &q->queue_head) {
		spin_unlock_irq(q->queue_lock);
		BUG();
	}

	/*
	 * elevator indicated where it wants this request to be
	 * inserted at elevator_merge time
	 */
	list_add(&req->queue, insert_here);
	q->last_request = req;
}

/*
 * Must be called with io_request_lock held and interrupts disabled
 */
void blkdev_release_request(struct request *req)
{
	request_queue_t *q = req->q;
	int rw = req->cmd;

	req->rq_status = RQ_INACTIVE;
	req->q = NULL;

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * assume it has free buffers and check waiters
	 */
	if (q) {
		list_add(&req->queue, &q->rq[rw].free);
		if (q->last_request == req)
			q->last_request = NULL;
		
		if (++q->rq[rw].count >= q->batch_requests)
			wake_up(&q->wait_for_requests[rw]);
	}
}

/*
 * Has to be called with the request spinlock acquired
 */
static void attempt_merge(request_queue_t * q,
			  struct request *req,
			  int max_sectors,
			  int max_segments)
{
	struct request *next;
  
	next = blkdev_next_request(req);
	if (req->sector + req->nr_sectors != next->sector)
		return;
	if (req->cmd != next->cmd
	    || req->rq_dev != next->rq_dev
	    || req->nr_sectors + next->nr_sectors > max_sectors
	    || next->waiting)
		return;
	/*
	 * If we are not allowed to merge these requests, then
	 * return.  If we are allowed to merge, then the count
	 * will have been updated to the appropriate number,
	 * and we shouldn't do it here too.
	 */
	if (!q->merge_requests_fn(q, req, next, max_segments))
		return;

	q->elevator.elevator_merge_req_fn(req, next);
	req->bhtail->b_reqnext = next->bh;
	req->bhtail = next->bhtail;
	req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;
	list_del(&next->queue);
	q->last_request = req;

	/* One last thing: we have removed a request, so we now have one
	   less expected IO to complete for accounting purposes. */
	req_merged_io(req);

	blkdev_release_request(next);
}

static inline void attempt_back_merge(request_queue_t * q,
				      struct request *req,
				      int max_sectors,
				      int max_segments)
{
	if (&req->queue == q->queue_head.prev)
		return;
	attempt_merge(q, req, max_sectors, max_segments);
}

static inline void attempt_front_merge(request_queue_t * q,
				       struct list_head * head,
				       struct request *req,
				       int max_sectors,
				       int max_segments)
{
	struct list_head * prev;

	prev = req->queue.prev;
	if (head == prev)
		return;
	attempt_merge(q, blkdev_entry_to_request(prev), max_sectors, max_segments);
}

static inline void req_add_bh(struct request *rq, struct buffer_head *bh)
{
	unsigned short tot_size = bh->b_size >> 9;
	struct buffer_head *bhtail = bh;
	int segments = 1;

	if (buffer_superbh(bh)) {
		segments = superbh_segments(bh);
		bhtail = superbh_bhtail(bh);

		/*
		 * now bh points to the first real bh, superbh is used no more
		 */
		bh = superbh_bh(bh);
	}

	rq->hard_nr_sectors = rq->nr_sectors = tot_size;
	rq->current_nr_sectors = rq->hard_cur_sectors = bh->b_size >> 9;
	rq->nr_segments = segments;
	rq->nr_hw_segments = segments;
	rq->buffer = bh->b_data;
	rq->bh = bh;
	rq->bhtail = bhtail;
}

static int __make_request(request_queue_t *q, int rw, struct buffer_head *bh)
{
	unsigned int sector, count;
	int max_segments = MAX_SEGMENTS;
	struct request * req, *freereq = NULL;
	int rw_ahead, max_sectors, el_ret;
	struct list_head *head, *insert_here;
	int latency;
	elevator_t *elevator = &q->elevator;

	count = bh->b_size >> 9;
	sector = bh->b_rsector;

	rw_ahead = 0;	/* normal case; gets changed below for READA */
	switch (rw) {
		case READA:
#if 0	/* bread() misinterprets failed READA attempts as IO errors on SMP */
			rw_ahead = 1;
#endif
			rw = READ;	/* drop into READ */
		case READ:
		case WRITE:
			latency = elevator_request_latency(elevator, rw);
			break;
		default:
			BUG();
			goto end_io;
	}

	/* We'd better have a real physical mapping!
	   Check this bit only if the buffer was dirty and just locked
	   down by us so at this point flushpage will block and
	   won't clear the mapped bit under us. */
	if (!buffer_mapped(bh))
		BUG();

	/*
	 * Temporary solution - in 2.5 this will be done by the lowlevel
	 * driver. Create a bounce buffer if the buffer data points into
	 * high memory - keep the original buffer otherwise.
	 */
	bh = blk_queue_bounce(q, rw, bh);

	/*
	 * Try to coalesce the new request with old requests
	 */
	max_sectors = get_max_sectors(bh->b_rdev);

again:
	req = NULL;
	head = &q->queue_head;
	/*
	 * Now we acquire the request spinlock, we have to be mega careful
	 * not to schedule or do something nonatomic
	 */
	spin_lock_irq(q->queue_lock);

	insert_here = head->prev;
	if (list_empty(head)) {
		q->plug_device_fn(q, bh->b_rdev); /* is atomic */
		goto get_rq;
	} else if (q->head_active && !q->plugged)
		head = head->next;

	/*
	 * potentially we could easily allow merge with superbh, however
	 * it requires extensive changes to merge functions etc, so I prefer
	 * to only let merges happen the other way around (ie normal bh _can_
	 * be merged into a request which originated from a superbh). look
	 * into doing this, if MAX_GROUPED is not enough to pull full io
	 * bandwidth from device.
	 */
	if (buffer_superbh(bh))
		goto get_rq;

	el_ret = elevator->elevator_merge_fn(q, &req, head, bh, rw,max_sectors);
	switch (el_ret) {

		case ELEVATOR_BACK_MERGE:
			if (!q->back_merge_fn(q, req, bh, max_segments)) {
				insert_here = &req->queue;
				break;
			}
			req->bhtail->b_reqnext = bh;
			req->bhtail = bh;
			req->nr_sectors = req->hard_nr_sectors += count;
			blk_started_io(count);
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);
			req_new_io(req, 1, count);
			attempt_back_merge(q, req, max_sectors, max_segments);
			goto out;

		case ELEVATOR_FRONT_MERGE:
			if (!q->front_merge_fn(q, req, bh, max_segments)) {
				insert_here = req->queue.prev;
				break;
			}
			bh->b_reqnext = req->bh;
			req->bh = bh;
			/*
			 * may not be valid, but queues not having bounce
			 * enabled for highmem pages must not look at
			 * ->buffer anyway
			 */
			req->buffer = bh->b_data;
			req->current_nr_sectors = req->hard_cur_sectors = count;
			req->sector = req->hard_sector = sector;
			req->nr_sectors = req->hard_nr_sectors += count;
			blk_started_io(count);
			drive_stat_acct(req->rq_dev, req->cmd, count, 0);
			req_new_io(req, 1, count);
			attempt_front_merge(q, head, req, max_sectors, max_segments);
			goto out;

		/*
		 * elevator says don't/can't merge. get new request
		 */
		case ELEVATOR_NO_MERGE:
			/*
			 * use elevator hints as to where to insert the
			 * request. if no hints, just add it to the back
			 * of the queue
			 */
			if (req)
				insert_here = &req->queue;
			break;

		default:
			printk("elevator returned crap (%d)\n", el_ret);
			BUG();
	}
		
get_rq:
	if (freereq) {
		req = freereq;
		freereq = NULL;
	} else {
		/*
		 * See description above __get_request_wait()
		 */
		if (rw_ahead) {
			if (q->rq[rw].count < q->batch_requests) {
				spin_unlock_irq(q->queue_lock);
				goto end_io;
			}
			req = get_request(q, rw);
			if (req == NULL)
				BUG();
		} else {
			req = get_request(q, rw);
			if (req == NULL) {
				spin_unlock_irq(q->queue_lock);
				freereq = __get_request_wait(q, rw);
				goto again;
			}
		}
	}

	/*
	 * fill up the request-info, and add it to the queue
	 */
	req->elevator_sequence = latency;
	req->cmd = rw;
	req->errors = 0;
	req->hard_sector = req->sector = sector;
	req->waiting = NULL;
	req->rq_dev = bh->b_rdev;
	req->start_time = jiffies;
	req->seg_invalid = buffer_superbh(bh);
	req_add_bh(req, bh);
	req_new_io(req, 0, count);
	blk_started_io(count);
	add_request(q, req, insert_here);
out:
	if (freereq)
		blkdev_release_request(freereq);
	spin_unlock_irq(q->queue_lock);
	return 0;
end_io:
	bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
	return 0;
}

/**
 * generic_make_request: hand a buffer head to it's device driver for I/O
 * @rw:  READ, WRITE, or READA - what sort of I/O is desired.
 * @bh:  The buffer head describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct buffer_head and a &rw value.  The
 * %READ and %WRITE options are (hopefully) obvious in meaning.  The
 * %READA value means that a read is required, but that the driver is
 * free to fail the request if, for example, it cannot get needed
 * resources immediately.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bh->b_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that b_page,
 * b_addr, b_size are set to describe the memory buffer, that b_rdev
 * and b_rsector are set to describe the device address, and the
 * b_end_io and optionally b_private are set to describe how
 * completion notification should be signaled.  BH_Mapped should also
 * be set (to confirm that b_dev and b_blocknr are valid).
 *
 * generic_make_request and the drivers it calls may use b_reqnext,
 * and may change b_rdev and b_rsector.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 * Because of this, the caller should record the device address
 * information in b_dev and b_blocknr.
 *
 * Apart from those fields mentioned above, no other fields, and in
 * particular, no other flags, are changed by generic_make_request or
 * any lower level drivers.
 * */
void generic_make_request (int rw, struct buffer_head * bh)
{
	int major = MAJOR(bh->b_rdev);
	int minorsize = 0;
	request_queue_t *q;

	if (!bh->b_end_io)
		BUG();

	/* Test device size, when known. */
	if (blk_size[major])
		minorsize = blk_size[major][MINOR(bh->b_rdev)];
	if (minorsize) {
		unsigned long maxsector = (minorsize << 1) + 1;
		unsigned long sector = bh->b_rsector;
		unsigned int count = bh->b_size >> 9;

		if (maxsector < count || maxsector - count < sector) {
			/* Yecch */
			bh->b_state &= ~(1 << BH_Dirty);

			/* This may well happen - the kernel calls bread()
			   without checking the size of the device, e.g.,
			   when mounting a device. */
			printk(KERN_INFO
			       "attempt to access beyond end of device\n");
			printk(KERN_INFO "%s: rw=%d, want=%lu, limit=%u\n",
			       kdevname(bh->b_rdev), rw,
			       (sector + count)>>1, minorsize);

			bh->b_end_io(bh, 0);
			return;
		}
	}

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 */
	/* NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	do {
		q = blk_get_queue(bh->b_rdev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access "
			       "nonexistent block-device %s (%lu)\n",
			       kdevname(bh->b_rdev), bh->b_rsector);
			buffer_IO_error(bh);
			break;
		}

		/*
		 * superbh_end_io() will gracefully resubmit, this is a soft
		 * error. if !grouped_queue, fail hard
		 */
		if (buffer_superbh(bh) && !q->superbh_queue) {
			buffer_IO_error(bh);
			break;
		}

	} while (q->make_request_fn(q, rw, bh));
}

/**
 * submit_bh: submit a buffer_head to the block device later for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bh: The &struct buffer_head which describes the I/O
 *
 * submit_bh() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work.
 *
 * The extra functionality provided by submit_bh is to determine
 * b_rsector from b_blocknr and b_size, and to set b_rdev from b_dev.
 * This is is appropriate for IO requests that come from the buffer
 * cache and page cache which (currently) always use aligned blocks.
 */
void submit_bh_rsector(int rw, struct buffer_head * bh)
{
	int count = bh->b_size >> 9;

	if (unlikely(!test_bit(BH_Lock, &bh->b_state)))
		BUG();

	set_bit(BH_Req, &bh->b_state);
	set_bit(BH_Launder, &bh->b_state);

	/*
	 * First step, 'identity mapping' - RAID or LVM might
	 * further remap this.
	 */
	bh->b_rdev = bh->b_dev;

	get_bh(bh);
	generic_make_request(rw, bh);

	/* fix race condition with wait_on_buffer() */
	smp_mb(); /* spin_unlock may have inclusive semantics */
	if (waitqueue_active(&bh->b_wait))
		wake_up(&bh->b_wait);
	put_bh(bh);

	switch (rw) {
		case WRITE:
			kstat_percpu[smp_processor_id()].pgpgout += count;
			break;
		default:
			kstat_percpu[smp_processor_id()].pgpgin += count;
			break;
	}
}

/*
 * if this is called, it's prior to the super_bh being submitted. so
 * we can fallback to normal IO submission here. uptodate bool means
 * absolutely nothing here.
 */
void superbh_end_io(struct buffer_head *superbh, int uptodate)
{
	struct buffer_head *bh = superbh_bh(superbh);
	const int rw = superbh_rw(superbh);
	struct buffer_head *next_bh;

	if (!buffer_superbh(superbh))
		BUG();

	/*
	 * detach each bh and resubmit, or completely fail if its a grouped bh
	 */
	do {
		next_bh = bh->b_reqnext;
		bh->b_reqnext = NULL;

		submit_bh_rsector(rw, bh);
	} while ((bh = next_bh));
}

static inline int bounce_initialised(void)
{
#ifdef CONFIG_HIGHMEM
	extern int emergency_bounce_initialised;
	return emergency_bounce_initialised;
#else
	return 0;
#endif
}

/**
 * submit_bh_linked: submit a list of buffer_heads for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bh: The first &struct buffer_head
 *
 * submit_bh_linked() acts like submit_bh(), but it can submit more than
 * one buffer_head in one go. It sets up a "superbh" describing the combined
 * transfer of the linked buffer_heads, and submit these in one go. This
 * heavily cuts down on time spent inside io scheduler.
 *
 * The buffer_head strings must be linked via b_reqnext. All
 * buffer_heads must be valid and pre-setup like the caller would for
 * submit_bh(), except that the caller is responsible for setting up
 * b_rsector.
 */
int submit_bh_linked(int rw, struct buffer_head *bh)
{
	struct buffer_head superbh, *tmp_bh, *bhprev, *bhfirst;
	request_queue_t *q = blk_get_queue(bh->b_dev);
	int size, segments, max_size;
	unsigned long next_sector;

	/*
	 * these must be the same for all buffer_heads
	 */
	superbh_rw(&superbh) = rw;
	superbh.b_rdev = bh->b_dev;
	superbh.b_state = bh->b_state | (1 << BH_Super);
	superbh.b_end_io = superbh_end_io;

	/*
	 * not really needed...
	 */
	superbh.b_data = NULL;
	superbh.b_page = NULL;

	tmp_bh = bh;

queue_next:
	bhprev = bhfirst = tmp_bh;
	segments = size = 0;
	next_sector = -1;

	superbh_bh(&superbh) = bhfirst;

	if (!q)
		goto punt;

	max_size = q->superbh_queue;

	/* If this queue may need bounce buffering, enforce a stricter
	 * upper bound on the superbh size for bounce buffer deadlock
	 * safety. */
	if (q->bounce_pfn < blk_max_pfn && bounce_initialised() &&
	    superbh_will_bounce(q->bounce_pfn, &superbh)) {
		if (max_size > MAX_SUPERBH)
			max_size = MAX_SUPERBH;
	}

	/*
	 * doesn't support superbh queueing, punt
	 */
	if (!max_size || !q->max_segments)
		goto punt;

	do {
		if (!buffer_locked(tmp_bh))
			BUG();

		/*
		 * submit this bit if it would exceed the allowed size
		 * or if it would result in a non-contiguous IO
		 */
		if ((size + tmp_bh->b_size > max_size)
		    || (segments >= q->max_segments) 
		    || (next_sector != -1 && tmp_bh->b_rsector != next_sector)
		    ) {
			bhprev->b_reqnext = NULL;
			break;
		}

		/*
		 * init each bh like submit_bh() does
		 */
		tmp_bh->b_rdev = tmp_bh->b_dev;
		set_bit(BH_Req, &tmp_bh->b_state);
		set_bit(BH_Launder, &tmp_bh->b_state);

		size += tmp_bh->b_size;
		bhprev = tmp_bh;
		segments++;
		next_sector = tmp_bh->b_rsector + (tmp_bh->b_size >> 9);
	} while ((tmp_bh = tmp_bh->b_reqnext));

	/*
	 * this is a super bh, it's only valid for io submission. it describes
	 * in size the entire bh list submitted.
	 */
	superbh.b_size = size;
	superbh_segments(&superbh) = segments;
	superbh_bhtail(&superbh) = bhprev;
	superbh.b_rsector = bhfirst->b_rsector;

	generic_make_request(rw, &superbh);

	switch (rw) {
		case WRITE:
			kstat_percpu[smp_processor_id()].pgpgout += size >> 9;
			break;
		default:
			kstat_percpu[smp_processor_id()].pgpgin += size >> 9;
			break;
	}

	/*
	 * not done
	 */
	if (tmp_bh)
		goto queue_next;

	return 0;
punt:
	superbh.b_end_io(&superbh, 0);
	return 1;
}

/**
 * ll_rw_block: low-level access to block devices
 * @rw: whether to %READ or %WRITE or maybe %READA (readahead)
 * @nr: number of &struct buffer_heads in the array
 * @bhs: array of pointers to &struct buffer_head
 *
 * ll_rw_block() takes an array of pointers to &struct buffer_heads,
 * and requests an I/O operation on them, either a %READ or a %WRITE.
 * The third %READA option is described in the documentation for
 * generic_make_request() which ll_rw_block() calls.
 *
 * This function provides extra functionality that is not in
 * generic_make_request() that is relevant to buffers in the buffer
 * cache or page cache.  In particular it drops any buffer that it
 * cannot get a lock on (with the BH_Lock state bit), any buffer that
 * appears to be clean when doing a write request, and any buffer that
 * appears to be up-to-date when doing read request.  Further it marks
 * as clean buffers that are processed for writing (the buffer cache
 * wont assume that they are actually clean until the buffer gets
 * unlocked).
 *
 * ll_rw_block sets b_end_io to simple completion handler that marks
 * the buffer up-to-date (if approriate), unlocks the buffer and wakes
 * any waiters.  As client that needs a more interesting completion
 * routine should call submit_bh() (or generic_make_request())
 * directly.
 *
 * Caveat:
 *  All of the buffers must be for the same device, and must also be
 *  of the current approved size for the device.  */

void ll_rw_block(int rw, int nr, struct buffer_head * bhs[])
{
	unsigned int major;
	int correct_size;
	int i;

	if (!nr)
		return;

	major = MAJOR(bhs[0]->b_dev);

	/* Determine correct block size for this device. */
	correct_size = get_hardsect_size(bhs[0]->b_dev);

	/* Verify requested block sizes. */
	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = bhs[i];
		if (bh->b_size % correct_size) {
			printk(KERN_NOTICE "ll_rw_block: device %s: "
			       "only %d-char blocks implemented (%u)\n",
			       kdevname(bhs[0]->b_dev),
			       correct_size, bh->b_size);
			goto sorry;
		}
	}

	if ((rw & WRITE) && is_read_only(bhs[0]->b_dev)) {
		printk(KERN_NOTICE "Can't write to read-only device %s\n",
		       kdevname(bhs[0]->b_dev));
		goto sorry;
	}

	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = bhs[i];

		/* Only one thread can actually submit the I/O. */
		if (test_and_set_bit(BH_Lock, &bh->b_state))
			continue;

		/* We have the buffer lock */
		atomic_inc(&bh->b_count);
		bh->b_end_io = end_buffer_io_sync;

		switch(rw) {
		case WRITE:
			if (!atomic_set_buffer_clean(bh))
				/* Hmmph! Nothing to write */
				goto end_io;
			__mark_buffer_clean(bh);
			break;

		case READA:
		case READ:
			if (buffer_uptodate(bh))
				/* Hmmph! Already have it */
				goto end_io;
			break;
		default:
			BUG();
	end_io:
			bh->b_end_io(bh, test_bit(BH_Uptodate, &bh->b_state));
			continue;
		}

		submit_bh(rw, bh);
	}
	return;

sorry:
	/* Make sure we don't get infinite dirty retries.. */
	for (i = 0; i < nr; i++)
		mark_buffer_clean(bhs[i]);
}

#ifdef CONFIG_STRAM_SWAP
extern int stram_device_init (void);
#endif


/**
 * end_that_request_first - end I/O on one buffer.
 * @req:      the request being processed
 * @uptodate: 0 for I/O error
 * @name:     the name printed for an I/O error
 *
 * Description:
 *     Ends I/O on the first buffer attached to @req, and sets it up
 *     for the next buffer_head (if any) in the cluster.
 *     
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 *
 * Caveat: 
 *     Drivers implementing their own end_request handling must call
 *     blk_finished_io() appropriately.
 **/

int end_that_request_first (struct request *req, int uptodate, char *name)
{
	struct buffer_head * bh;
	int nsect;

	req->errors = 0;
	if (!uptodate)
		printk("end_request: I/O error, dev %s (%s), sector %lu\n",
			kdevname(req->rq_dev), name, req->sector);

	if ((bh = req->bh) != NULL) {
		nsect = bh->b_size >> 9;
		blk_finished_io(nsect);
		req->bh = bh->b_reqnext;
		bh->b_reqnext = NULL;
		bh->b_end_io(bh, uptodate);
		if ((bh = req->bh) != NULL) {
			req->hard_sector += nsect;
			req->hard_nr_sectors -= nsect;
			req->sector = req->hard_sector;
			req->nr_sectors = req->hard_nr_sectors;

			req->current_nr_sectors = bh->b_size >> 9;
			req->hard_cur_sectors = req->current_nr_sectors;
			if (req->nr_sectors < req->current_nr_sectors) {
				req->nr_sectors = req->current_nr_sectors;
				printk("end_request: buffer-list destroyed\n");
			}
			req->buffer = bh->b_data;
			return 1;
		}
	}
	return 0;
}

void end_that_request_last(struct request *req)
{
	struct completion *waiting = req->waiting;

	req_finished_io(req);
	blkdev_release_request(req);
	if (waiting)
		complete(waiting);
}

int __init blk_dev_init(void)
{
	struct blk_dev_struct *dev;

	request_cachep = kmem_cache_create("blkdev_requests",
					   sizeof(struct request),
					   0, SLAB_HWCACHE_ALIGN, NULL, NULL);

	if (!request_cachep)
		panic("Can't create request pool slab cache\n");

	for (dev = blk_dev + MAX_BLKDEV; dev-- != blk_dev;)
		dev->queue = NULL;

	memset(ro_bits,0,sizeof(ro_bits));
	memset(max_readahead, 0, sizeof(max_readahead));
	memset(max_sectors, 0, sizeof(max_sectors));

	blk_max_low_pfn = max_low_pfn - 1;
	blk_max_pfn = max_pfn - 1;

#ifdef CONFIG_AMIGA_Z2RAM
	z2_init();
#endif
#ifdef CONFIG_STRAM_SWAP
	stram_device_init();
#endif
#ifdef CONFIG_ISP16_CDI
	isp16_init();
#endif
#ifdef CONFIG_BLK_DEV_PS2
	ps2esdi_init();
#endif
#ifdef CONFIG_BLK_DEV_XD
	xd_init();
#endif
#ifdef CONFIG_BLK_DEV_MFM
	mfm_init();
#endif
#ifdef CONFIG_PARIDE
	{ extern void paride_init(void); paride_init(); };
#endif
#ifdef CONFIG_MAC_FLOPPY
	swim3_init();
#endif
#ifdef CONFIG_BLK_DEV_SWIM_IOP
	swimiop_init();
#endif
#ifdef CONFIG_AMIGA_FLOPPY
	amiga_floppy_init();
#endif
#ifdef CONFIG_ATARI_FLOPPY
	atari_floppy_init();
#endif
#ifdef CONFIG_BLK_DEV_FD
	floppy_init();
#else
#if defined(__i386__)	/* Do we even need this? */
	outb_p(0xc, 0x3f2);
#endif
#endif
#ifdef CONFIG_CDU31A
	cdu31a_init();
#endif
#ifdef CONFIG_ATARI_ACSI
	acsi_init();
#endif
#ifdef CONFIG_MCD
	mcd_init();
#endif
#ifdef CONFIG_MCDX
	mcdx_init();
#endif
#ifdef CONFIG_SBPCD
	sbpcd_init();
#endif
#ifdef CONFIG_AZTCD
	aztcd_init();
#endif
#ifdef CONFIG_CDU535
	sony535_init();
#endif
#ifdef CONFIG_GSCD
	gscd_init();
#endif
#ifdef CONFIG_CM206
	cm206_init();
#endif
#ifdef CONFIG_OPTCD
	optcd_init();
#endif
#ifdef CONFIG_SJCD
	sjcd_init();
#endif
#ifdef CONFIG_APBLOCK
	ap_init();
#endif
#ifdef CONFIG_DDV
	ddv_init();
#endif
#ifdef CONFIG_MDISK
	mdisk_init();
#endif
#ifdef CONFIG_DASD
	dasd_init();
#endif
#if defined(CONFIG_S390_TAPE) && defined(CONFIG_S390_TAPE_BLOCK)
	tapeblock_init();
#endif
#ifdef CONFIG_BLK_DEV_XPRAM
        xpram_init();
#endif

#ifdef CONFIG_SUN_JSFLASH
	jsfd_init();
#endif
	return 0;
};

EXPORT_SYMBOL(io_request_lock);
EXPORT_SYMBOL(end_that_request_first);
EXPORT_SYMBOL(end_that_request_last);
EXPORT_SYMBOL(blk_grow_request_list);
EXPORT_SYMBOL(blk_init_queue);
EXPORT_SYMBOL(blk_get_queue);
EXPORT_SYMBOL(blk_cleanup_queue);
EXPORT_SYMBOL(blk_queue_headactive);
EXPORT_SYMBOL(blk_queue_superbh);
EXPORT_SYMBOL(blk_queue_large_superbh);
EXPORT_SYMBOL(blk_queue_make_request);
EXPORT_SYMBOL_GPL(blk_start_queue_timer);
EXPORT_SYMBOL(generic_make_request);
EXPORT_SYMBOL(blkdev_release_request);
EXPORT_SYMBOL(generic_unplug_device);
EXPORT_SYMBOL(blk_queue_bounce_limit);
EXPORT_SYMBOL(blk_max_low_pfn);
EXPORT_SYMBOL(blk_max_pfn);
EXPORT_SYMBOL(blk_seg_merge_ok);
EXPORT_SYMBOL(blk_nohighio);

EXPORT_SYMBOL(ll_rw_block);
EXPORT_SYMBOL(submit_bh_rsector);
EXPORT_SYMBOL(submit_bh_linked);
