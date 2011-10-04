/*
 *  drivers/s390/char/tape_block.c
 *    block device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/cdrom.h>

#include <asm/debug.h>
#include <asm/irq.h>
#include <asm/s390dyn.h>

#include "tape.h"
#include "tape_std.h"

#define PRINTK_HEADER "TBLOCK:"

#define TAPEBLOCK_DEVFSMODE	0060644	/* brwxrw-rw- */
#define TAPEBLOCK_MAX_SEC	100
#define TAPEBLOCK_MIN_REQUEUE   3

/*
 * file operation structure for tape block frontend
 */
static int tapeblock_open(struct inode *, struct file *);
static int tapeblock_release(struct inode *, struct file *);
static int tapeblock_ioctl(
	struct inode *, struct file *, unsigned int, unsigned long);

static struct block_device_operations tapeblock_bdops = {
	.owner		= THIS_MODULE,
	.open		= tapeblock_open,
	.release	= tapeblock_release,
	.ioctl          = tapeblock_ioctl,
};

int tapeblock_major = 0;

/*
 * Some helper inlines
 */
static inline int tapeblock_size(int minor) {
	return blk_size[tapeblock_major][minor];
}
static inline int tapeblock_ssize(int minor) {
	return blksize_size[tapeblock_major][minor];
}
static inline int tapeblock_hw_ssize(int minor) {
	return hardsect_size[tapeblock_major][minor];
}

/*
 * Post finished request.
 */
static inline void
tapeblock_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, "tBLK"))
		BUG();
	end_that_request_last(req);
}

static void
__tapeblock_end_request(struct tape_request *ccw_req, void *data)
{
	struct tape_device *device;
	struct request     *req;

	device = ccw_req->device;
	req    = (struct request *) data;
	if(!device || !req)
		BUG();

	tapeblock_end_request(req, ccw_req->rc == 0);
	if (ccw_req->rc == 0)
		/* Update position. */
		device->blk_data.block_position = 
			(req->sector + req->nr_sectors) >> TAPEBLOCK_HSEC_S2B;
	else
		/* We lost the position information due to an error. */
		device->blk_data.block_position = -1;

	device->discipline->free_bread(ccw_req);

	if (!list_empty(&device->req_queue) ||
	    !list_empty(&device->blk_data.request_queue.queue_head))
		tasklet_schedule(&device->blk_data.tasklet);
}

/*
 * Fetch requests from block device queue.
 */
static inline void
__tape_process_blk_queue(struct tape_device *device, struct list_head *new_req)
{
	request_queue_t     *queue;
	struct list_head    *l;
	struct request      *req;
	struct tape_request *ccw_req;
	int                  nr_queued;

	if (!TAPE_BLOCKDEV(device)) {
		PRINT_WARN("can't process queue. Not a tape blockdevice.\n");
		return;
	}

	nr_queued = 0;
	queue     = &device->blk_data.request_queue;

	/* Count number of requests on ccw queue. */
	list_for_each(l, &device->req_queue)
		nr_queued++;

	while (
		!queue->plugged &&
		!list_empty(&queue->queue_head) &&
		nr_queued < TAPEBLOCK_MIN_REQUEUE
	) {
		/* tape_block_next_request(queue); */
		req = blkdev_entry_next_request(&queue->queue_head);

		if (req->cmd == WRITE) {
			DBF_EVENT(1, "TBLOCK: Rejecting write request\n");
			blkdev_dequeue_request(req);
			tapeblock_end_request(req, 0);
			continue;
		}
		ccw_req = device->discipline->bread(device, req);
		if (IS_ERR(ccw_req)) {
			if (PTR_ERR(ccw_req) == -ENOMEM)
				break; /* don't try again */
			DBF_EVENT(1, "TBLOCK: bread failed\n");
			blkdev_dequeue_request(req);
			tapeblock_end_request(req, 0);
			continue;
		}
		blkdev_dequeue_request(req);
		ccw_req->callback      = __tapeblock_end_request;
		ccw_req->callback_data = (void *) req;
		ccw_req->retries       = TAPEBLOCK_RETRIES;

		list_add_tail(&ccw_req->list, new_req);
		nr_queued++;
	}
}

/*
 * Feed requests to the tape device.
 */
static inline int
tape_queue_requests(struct tape_device *device, struct list_head *new_req)
{
	struct list_head *l, *n;
	struct tape_request *ccw_req;
	struct request *req;
	int rc, fail;

	fail = 0;
	list_for_each_safe(l, n, new_req) {
		ccw_req = list_entry(l, struct tape_request, list);
		list_del(&ccw_req->list);

		rc = tape_do_io_async(device, ccw_req);
		if (rc) {
			/*
			 * Start/enqueueing failed. No retries in
			 * this case.
			 */
			DBF_EVENT(5, "enqueueing failed\n");
			req = (struct request *) ccw_req->callback_data;
			tapeblock_end_request(req, 0);
			device->discipline->free_bread(ccw_req);
			fail = 1;
		}
	}
	return fail;
}

/*
 * Tape request queue function. Called from ll_rw_blk.c
 */
static void
tapeblock_request_fn(request_queue_t *queue)
{
	struct list_head    new_req;
	struct tape_device *device;

	device = (struct tape_device *) queue->queuedata;
	if(device == NULL)
		BUG();

	while (!list_empty(&queue->queue_head)) {
		INIT_LIST_HEAD(&new_req);
		spin_lock(get_irq_lock(device->devinfo.irq));
		__tape_process_blk_queue(device, &new_req);
		spin_unlock(get_irq_lock(device->devinfo.irq));
		/*
		 * Now queue the new request to the tape. This needs to be
		 * done without the device lock held.
		 */
		if (tape_queue_requests(device, &new_req) == 0)
			/* All requests queued. Thats enough for now. */
			break;
	}
}

/*
 * Returns block frontend request queue for a tape device.
 * FIXME: on shutdown make sure ll_rw_blk can put requests on a dead queue.
 */
static request_queue_t *
tapeblock_get_queue(kdev_t kdev)
{
	struct tape_device *device;
	request_queue_t    *queue;

	if (major(kdev) != tapeblock_major)
		return NULL;

	device = tape_get_device(minor(kdev) >> 1);
	if (IS_ERR(device))
		return NULL;

	queue = &device->blk_data.request_queue;
	tape_put_device(device);
	return queue;
}

/*
 * Acquire the device lock and process queues for the device.
 */
static void
tapeblock_tasklet(unsigned long data)
{
	struct list_head    new_req;
	struct tape_device *device;

	device = (struct tape_device *) data;
	while (!list_empty(&device->blk_data.request_queue.queue_head)) {
		INIT_LIST_HEAD(&new_req);
		spin_lock_irq(get_irq_lock(device->devinfo.irq));
		__tape_process_blk_queue(device, &new_req);
		spin_unlock_irq(get_irq_lock(device->devinfo.irq));
		/*
		 * Now queue the new request to the tape. This needs to be
		 * done without the device lock held.
		 */
		if (tape_queue_requests(device, &new_req) == 0)
			/* All requests queued. Thats enough for now. */
			break;
	}
}

/*
 * Create block directory with disc entries
 */
static int
tapeblock_mkdevfstree (struct tape_device *device)
{
#ifdef CONFIG_DEVFS_FS
	device->blk_data.devfs_block_dir = 
		devfs_mk_dir (device->devfs_dir, "block", device);
	if (device->blk_data.devfs_block_dir == 0)
		return -ENOENT;
	device->blk_data.devfs_disc = 
		devfs_register(device->blk_data.devfs_block_dir,
			       "disc", DEVFS_FL_DEFAULT,
			       tapeblock_major, device->first_minor,
			       TAPEBLOCK_DEVFSMODE, &tapeblock_bdops, device);
	if (device->blk_data.devfs_disc == NULL) {
		devfs_unregister(device->blk_data.devfs_block_dir);
		return -ENOENT;
	}
#endif
	return 0;
}

/*
 * Remove devfs entries
 */
static void
tapeblock_rmdevfstree (struct tape_device *device)
{
#ifdef CONFIG_DEVFS_FS
	if (device->blk_data.devfs_disc)
		devfs_unregister(device->blk_data.devfs_disc);
	if (device->blk_data.devfs_block_dir)
		devfs_unregister(device->blk_data.devfs_block_dir);
#endif
}

/*
 * This function is called for every new tapedevice
 */
int
tapeblock_setup_device(struct tape_device * device)
{
	int rc;

	/* FIXME: We should be able to sense the sector size */
	blk_size[tapeblock_major][device->first_minor]      = 0;
	blksize_size[tapeblock_major][device->first_minor]  =
	hardsect_size[tapeblock_major][device->first_minor] =
		TAPEBLOCK_HSEC_SIZE;

	/* Create devfs entries. */
	rc = tapeblock_mkdevfstree(device);
	if (rc)
		return rc;

	/* Setup request queue and initialize gendisk for this device. */
	device->blk_data.request_queue.queuedata = tape_clone_device(device);


	/* As long as the tasklet is running it may access the device */
	tasklet_init(&device->blk_data.tasklet, tapeblock_tasklet,
		     (unsigned long) tape_clone_device(device));

	blk_init_queue(&device->blk_data.request_queue, tapeblock_request_fn);
	blk_queue_headactive(&device->blk_data.request_queue, 0);

	tape_hotplug_event(device, tapeblock_major, TAPE_HOTPLUG_BLOCK_ADD);

	set_device_ro(mk_kdev(tapeblock_major, device->first_minor), 1);
	return 0;
}

void
tapeblock_cleanup_device(struct tape_device *device)
{
	/* Prevent further requests to the block request queue. */
	blk_size[tapeblock_major][device->first_minor] = 0;

	tapeblock_rmdevfstree(device);

	/* With the tasklet gone the reference is gone as well. */
	tasklet_kill(&device->blk_data.tasklet);
	tape_put_device(device);

	/* Cleanup the request queue. */
	blk_cleanup_queue(&device->blk_data.request_queue);

	/* Remove reference in private data */
	device->blk_data.request_queue.queuedata = NULL;
	tape_put_device(device);

	tape_hotplug_event(device, tapeblock_major, TAPE_HOTPLUG_BLOCK_REMOVE);
}

/*
 * Detect number of blocks of the tape.
 * FIXME: can we extent this to detect the blocks size as well ?
 * FIXME: (minor) On 34xx the block id also contains a format specification
 *                which is unknown before the block was skipped or read at
 *                least once. So detection is sometimes done a second time.
 */
int tapeblock_mediumdetect(struct tape_device *device)
{
	unsigned int bid;
	unsigned int nr_of_blks;
	int          rc;

	/*
	 * Identify the first records format
	 */
	if((rc = tape_mtop(device, MTFSR, 1)) < 0)
		return rc;
	if((rc = tape_mtop(device, MTBSR, 1)) < 0)
		return rc;

	device->blk_data.block_position = 0;
	if (tape_std_read_block_id(device, &bid)) {
		rc = tape_mtop(device, MTREW, 1);
		if (rc) {
			device->blk_data.block_position                = -1;
			blk_size[tapeblock_major][device->first_minor] =  0;
			return rc;
		}
		bid = 0;
	}

	if(bid != device->blk_data.start_block_id) {
		device->blk_data.start_block_id = bid;
		blk_size[tapeblock_major][device->first_minor] =  0;
	}

	if(blk_size[tapeblock_major][device->first_minor] > 0)
		return 0;

	PRINT_INFO("Detecting media size...\n");
	blk_size[tapeblock_major][device->first_minor] = 0;

	rc = tape_mtop(device, MTFSF, 1);
	if (rc)
		return rc;

	rc = tape_mtop(device, MTTELL, 1);
	if (rc < 0)
		return rc;
	nr_of_blks = rc - 1; /* don't count FM */

	if (device->blk_data.start_block_id) {
		rc = tape_std_seek_block_id(
			device,
			device->blk_data.start_block_id);
	} else {
		rc = tape_mtop(device, MTREW, 1);
	}
	if (rc)
		return rc;

	rc = tape_mtop(device, MTTELL, 1);
	if (rc < 0)
		return rc;

	/* Don't include start offset */
	nr_of_blks -= rc;

	PRINT_INFO("Found %i blocks on media\n", nr_of_blks);
	if (tapeblock_hw_ssize(device->first_minor) > 1024) {
		nr_of_blks *= tapeblock_hw_ssize(device->first_minor) / 1024;
	} else {
		nr_of_blks /= 1024 / tapeblock_hw_ssize(device->first_minor);
	}
	PRINT_INFO("Tape block device size is %i KB\n", nr_of_blks);
	blk_size[tapeblock_major][device->first_minor] = nr_of_blks;

	return 0;
}

/*
 * This function has to be called whenever a new medium has been inserted
 * into the drive.
 */
void
tapeblock_medium_change(struct tape_device *device) {
	device->blk_data.start_block_id                = 0;
	blk_size[tapeblock_major][device->first_minor] = 0;
}

/*
 * Block frontend tape device open function.
 */
int
tapeblock_open(struct inode *inode, struct file *filp) {
	struct tape_device *device;
	int                 rc;

	if (major(filp->f_dentry->d_inode->i_rdev) != tapeblock_major)
		return -ENODEV;

	MOD_INC_USE_COUNT;
	device = tape_get_device(minor(filp->f_dentry->d_inode->i_rdev) >> 1);
	if (IS_ERR(device)) {
		MOD_DEC_USE_COUNT;
		return PTR_ERR(device);
	}

	DBF_EVENT(6, "TBLOCK: open:  %x\n", device->first_minor);

	if(device->required_tapemarks) {
		DBF_EVENT(2, "TBLOCK: missing tapemarks\n");
		PRINT_ERR("TBLOCK: Refusing to open tape with missing"
			" end of file marks.\n");
		tape_put_device(device);
		MOD_DEC_USE_COUNT;
		return -EPERM;
	}

	rc = tape_open(device);
	if (rc == 0) {
		rc = tape_assign(device, TAPE_STATUS_ASSIGN_A);
		if (rc == 0) {
			rc = tapeblock_mediumdetect(device);
			if (rc == 0) {
				TAPE_SET_STATE(device, TAPE_STATUS_BLOCKDEV);
				tape_put_device(device);
				return 0;
			}
			tape_unassign(device, TAPE_STATUS_ASSIGN_A);
		}
		tape_release(device);
	}
	tape_put_device(device);
	MOD_DEC_USE_COUNT;
	return rc;
}

/*
 * Block frontend tape device release function.
 */
int
tapeblock_release(struct inode *inode, struct file *filp) {
	struct tape_device *device;

	device = tape_get_device(minor(inode->i_rdev) >> 1);

	DBF_EVENT(4, "TBLOCK: release %i\n", device->first_minor);

	/* Remove all buffers at device close. */
	/* FIXME: can we do that a tape unload ? */
	invalidate_buffers(inode->i_rdev);

	if (device->blk_data.start_block_id) {
		tape_std_seek_block_id(device, device->blk_data.start_block_id);
	} else {
		tape_mtop(device, MTREW, 1);
	}
	TAPE_CLEAR_STATE(device, TAPE_STATUS_BLOCKDEV);
	tape_unassign(device, TAPE_STATUS_ASSIGN_A);
	tape_release(device);
	tape_put_device(device);
	MOD_DEC_USE_COUNT;

	return 0;
}

int
tapeblock_ioctl(
	struct inode	*inode,
	struct file	*file,
	unsigned int	 command,
	unsigned long	 arg
) {
	int	 rc     = 0;
	int	 minor  = minor(inode->i_rdev);

	DBF_EVENT(6, "tapeblock_ioctl(%x)\n", command);

	switch(command) {
		case BLKSSZGET:
			if(put_user(tapeblock_ssize(minor), (int *) arg))
				rc = -EFAULT;
			break;
		case BLKGETSIZE:
			if(
				put_user(
					tapeblock_size(minor),
					(unsigned long *) arg
				)
			)
				rc = -EFAULT;
			break;
#ifdef BLKGETSIZE64
		case BLKGETSIZE64:
			if(put_user(tapeblock_size(minor) << 9, (u64 *) arg))
				rc = -EFAULT;
			break;
#endif
		case CDROMMULTISESSION:
		case CDROMREADTOCENTRY:
			/* No message for these... */
			rc = -EINVAL;
			break;
		default:
			PRINT_WARN("invalid ioctl 0x%x\n", command);
			rc = -EINVAL;
	}
	return rc;
}

/*
 * Initialize block device frontend.
 */
int
tapeblock_init(void) 
{
	int rc;

	/* Register the tape major number to the kernel */
#ifdef CONFIG_DEVFS_FS
	if (tapeblock_major == 0)
		tapeblock_major = devfs_alloc_major(DEVFS_SPECIAL_BLK);
#endif
	rc = register_blkdev(tapeblock_major, "tBLK", &tapeblock_bdops);
	if (rc < 0) {
		PRINT_ERR("can't get major %d for block device\n",
			  tapeblock_major);
		return rc;
	}
	if(tapeblock_major == 0)
		tapeblock_major = rc;

	/* Allocate memory for kernel block device tables */
	rc = -ENOMEM;
	blk_size[tapeblock_major] = kmalloc(256*sizeof(int), GFP_KERNEL);
	if(blk_size[tapeblock_major] == NULL)
		goto tapeblock_init_fail;
	memset(blk_size[tapeblock_major], 0, 256*sizeof(int));
	blksize_size[tapeblock_major] = kmalloc(256*sizeof(int), GFP_KERNEL);
	if(blksize_size[tapeblock_major] == NULL)
		goto tapeblock_init_fail;
	memset(blksize_size[tapeblock_major], 0, 256*sizeof(int));
	hardsect_size[tapeblock_major] = kmalloc(256*sizeof(int), GFP_KERNEL);
	if(hardsect_size[tapeblock_major] == NULL)
		goto tapeblock_init_fail;
	memset(hardsect_size[tapeblock_major], 0, 256*sizeof(int));
	max_sectors[tapeblock_major] = kmalloc(256*sizeof(int), GFP_KERNEL);
	if(max_sectors[tapeblock_major] == NULL)
		goto tapeblock_init_fail;
	memset(max_sectors[tapeblock_major], 0, 256*sizeof(int));

	blk_dev[tapeblock_major].queue = tapeblock_get_queue;

	PRINT_INFO("tape gets major %d for block device\n", tapeblock_major);
	DBF_EVENT(3, "TBLOCK: major = %d\n", tapeblock_major);
	DBF_EVENT(3, "TBLOCK: init ok\n");

	return 0;

tapeblock_init_fail:
	if(tapeblock_major > 0) {
		if(blk_size[tapeblock_major]) {
			kfree(blk_size[tapeblock_major]);
			blk_size[tapeblock_major] = NULL;
		}
		if(blksize_size[tapeblock_major]) {
			kfree(blksize_size[tapeblock_major]);
			blksize_size[tapeblock_major] = NULL;
		}
		if(hardsect_size[tapeblock_major]) {
			kfree(hardsect_size[tapeblock_major]);
			hardsect_size[tapeblock_major] = NULL;
		}
		if(max_sectors[tapeblock_major]) {
			kfree(max_sectors[tapeblock_major]);
			max_sectors[tapeblock_major] = NULL;
		}
#ifdef CONFIG_DEVFS_FS
		devfs_unregister_blkdev(tapeblock_major, "tBLK");
#else
		unregister_blkdev(tapeblock_major, "tBLK");
#endif
		tapeblock_major = -1;
	}

	DBF_EVENT(3, "TBLOCK: init failed(%d)\n", rc);
	return rc;
}

/*
 * Deregister major for block device frontend
 */
void
tapeblock_exit(void)
{
	if(blk_size[tapeblock_major]) {
		kfree(blk_size[tapeblock_major]);
		blk_size[tapeblock_major] = NULL;
	}
	if(blksize_size[tapeblock_major]) {
		kfree(blksize_size[tapeblock_major]);
		blksize_size[tapeblock_major] = NULL;
	}
	if(hardsect_size[tapeblock_major]) {
		kfree(hardsect_size[tapeblock_major]);
		hardsect_size[tapeblock_major] = NULL;
	}
	if(max_sectors[tapeblock_major]) {
		kfree(max_sectors[tapeblock_major]);
		max_sectors[tapeblock_major] = NULL;
	}
	blk_dev[tapeblock_major].queue = NULL;
	unregister_blkdev(tapeblock_major, "tBLK");
}
