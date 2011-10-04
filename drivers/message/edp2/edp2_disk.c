/*
 *	EDP2 Block Storage
 *	This is a work in progress and not yet functional.
 *
 *	Copyright 2003 Red Hat Inc. All Rights Reserved.
 *	Based on drivers/message/i2o/i2o_block.c
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/major.h>

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/spinlock.h>

#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/completion.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>

#define MAJOR_NR 	250

#include <linux/blk.h>

#define MAX_EDP2_DISK	16

#define MAX_EDP2_DEPTH	8
#define MAX_EDP2_RETRIES 8

#define DRIVERDEBUG
#ifdef DRIVERDEBUG
#define DEBUG( s ) printk( s )
#else
#define DEBUG( s )
#endif

/*
 *	Some of these can be made smaller later
 */

static int edp2_blksizes[MAX_EDP2_DISK<<4];
static int edp2_hardsizes[MAX_EDP2_DISK<<4];
static int edp2_sizes[MAX_EDP2_DISK<<4];

static int edp2_context;

/*
 * I2O Block device descriptor 
 */
struct edp2_device
{
	struct net_device *dev;
	u8 mac[6];			/* Drive MAC address */
	int flags;
	int refcnt;
	struct request *head, *tail;
	request_queue_t *req_queue;
	int done_flag;
	int depth;
};

/*
 *	FIXME:
 *	We should cache align these to avoid ping-ponging lines on SMP
 *	boxes under heavy I/O load...
 */

struct edp2_request
{
	struct edp2_request *next;
	struct request *req;
	u16 tag;		/* EDP2 tag */
	u16 type;		/* Type of request, 0 for normal I/O */
	unsigned long sent;	/* For retransmissions */
	
};

/*
 * Per EDP request queue information
 *
 * We have a separate requeust_queue_t per EDP
 */

struct edp2_iop_queue
{
	atomic_t queue_depth;
	struct edp2_request request_queue[MAX_EDP2_DEPTH];
	struct edp2_request *edp2_qhead;
	request_queue_t req_queue;
};
static struct edp2_iop_queue *edp2_queues[MAX_EDP2_DISK];

/*
 *	Each EDP2 disk is one of these.
 */

static struct edp2_device edp2_dev[MAX_EDP2_DISK<<4];
static int edp2_dev_count = 0;
static struct hd_struct edp2_b[MAX_EDP2_DISK<<4];
static struct gendisk edp2_gendisk;	/* Declared later */

static void edp2_block_reply(struct i2o_handler *, struct i2o_controller *,
	 struct i2o_message *);
static void edp2_new_device(struct edp2_device *);
static void edp2_del_device(struct edp2_device *);
static int edp2_install_device(struct edp2_device *, int);
static void edp2_end_request(struct request *);
static void edp2_request(request_queue_t *);
static int edp2_init_unit(unsigned int);
static request_queue_t* edp2_get_queue(kdev_t);
static int edp2_query_device(struct edp2_device *, int, int, void*, int);
static int do_edp2_revalidate(kdev_t, int);
static int edp2_evt(void *);

/**
 *	edp2_get	-	get a message
 *	@dev: device to get message for
 *	
 *	Obtain and fill in the basics of an EDPv2 message
 *	buffer. Right now we always grab 1514 bytes, but that
 *	should be fixed because we can do grab a small buffer
 *	on read I/O. The pre-computed cached EDP header is
 *	added and the tag incremented. The caller needs to
 *	overwrite the function number if its not an I/O
 */
 
static struct sk_buff *edp2_get(struct edp2_device *dev)
{
	struct sk_buff * skb = alloc_skb(1514, GF_ATOMIC);
	memcpy(skb_put(skb, 20), dev->header.bits, 20);
	dev->header.struct.tag++;
	return skb;
}
 
/**
 *	edp2_send_write		-	Outgoing I/O path
 *	@skb: buffer from edp2_get
 *	@dev: EDP2 device
 *	@ireq: Request structure
 *	@base: Partition offset
 *	@unit: Device identity
 *
 *	We build a descriptor for the I/O request and fire at at the network
 *	interface. Retramsission and timeouts are handled by the layer
 *	above.
 */
 
static int edp2_send(struct sk_buff *skb, struct edp2_device *dev, struct edp2_request *ireq, u32 base, int unit)
{
	u64 offset;
	struct request *req = ireq->req;
	struct buffer_head *bh = req->bh;
	struct edp_ata_fid0 *io;
	u8 *data;
	
	/* We don't support GigE big frames for EDP */
	if(count > 1024)
		BUG();

	io = skb_put(skb, sizeof(edp_ata_fid0));

	/* 
	 * Mask out partitions from now on
	 */
	unit &= 0xF0;
		
	/* This can be optimised later - just want to be sure its right for
	   starters */
	offset = ((u64)(req->sector+base)) << 9;

	/* Pack the I/O request for LBA28 format */
	/* FIXME: use LBA48 when needed */	
	io->lba0 = offset & 0xFF:
	io->lba1 = (offset >> 8) & 0xFF:
	io->lba2 = (offset >> 16) & 0xFF:
	io->lba3 = (offset >> 24) & 0x0F + ??;
	io->cmd_status = req->nr_sectors;
	io->err_feature = 0;

	data = skb_put(skb, rq->nr_sectors << 9);
	
	/* Write I/O requires we attach the data too */
	if(req->cmd == WRITE)
	{	
		io->flag_ver = EDP_ATA_WRITE;
		while(bh!=NULL)
		{
			memcpy(data, bh->b_data, bh->b_size);
			data += bh->b_size;
			bh = bh->b_reqnext;
		}
	}
	
	epd2_post_message(dev, skb);
	atomic_inc(&edp2_queues[dev->unit]->queue_depth);
	return 0;
}

/**
 *	edp2_unhook_request	-	dequeue request from locked list
 *	@ireq: Request to dequeue
 *	@disk: Disk to dequeue from
 *
 *	Remove a request from the _locked_ request list. We update both the
 *	list chain and if this is the last item the tail pointer. Caller
 *	must hold the lock.
 */
 
static inline void edp2_unhook_request(struct edp2_request *ireq,  unsigned int disk)
{
	ireq->next = edp2_queues[disk]->edp2_qhead;
	edp2_queues[disk]->edp2_qhead = ireq;
}

/**
 *	edi2_end_request	-	request completion handler
 *	@req: block request to complete
 *
 *	Called when the EDP2 layer completes an I/O request and
 *	wants to complete it with the kernel block layer
 */
 
static inline void edp2_end_request(struct request *req)
{
	/*
	 * Loop until all of the buffers that are linked
	 * to this request have been marked updated and
	 * unlocked.
	 */

	while (end_that_request_first( req, !req->errors, "edp2" ));

	/*
	 * It is now ok to complete the request.
	 */
	end_that_request_last( req );
}

/**
 *	edp2_report_data	-	decode edp2 data
 *	@data: error bits to decode
 *
 *	Called to decode EDP level error events from ATA FID0 frames
 */

static void edp2_report_error(struct edp2_device *dev, u8 data)
{
	switch(data)
	{
		case EDPT2_ATA_BADPARAM:
			printk(KERN_ERR "%s: Parameter error reported by controller.\n", dev->name);
			break;
		case EDPT2_ATA_DISKFAIL:
			printk(KERN_ERR "%s: Disk failure reported by controller.\n", dev->name);
			break;
		default:
			printk(KERN_ERR "%s: Error %d reported by controller.\n", dev->name, data);
			break;
	}
}

/**
 *	edp2_report_ata_error	-	report ATA error
 *	@dev: device we are talking with
 *	@io: io reply
 *
 *	Called when an ATA error has been reported by the remote target.
 *	We decode the unpacked return registers and report the error
 *	to the user in the style of a local ATA device
 */
 
static void edp2_report_ata_error(struct edp2_device *dev, struct edp2_ata_fid0 *io)
{
	/*
	 *	Dump EDP2 ATA errors in the same style as the IDE
	 *	driver. No point making people learn to interpret
	 *	twice as much crap
	 */
	printk(KERN_WARNING "%s: logged error: status=0x%02x { ", dev->name, stat);
	/* We should never see "busy" */
	if(stat&BUSY_STAT)
		printk("Busy Â);
	else
	{
		if (stat & READY_STAT)	printk("DriveReady ");
		if (stat & WRERR_STAT)	printk("DeviceFault ");
		if (stat & SEEK_STAT)	printk("SeekComplete ");
		if (stat & DRQ_STAT)	printk("DataRequest ");
		if (stat & ECC_STAT)	printk("CorrectedError ");
		if (stat & INDEX_STAT)	printk("Index ");
		if (stat & ERR_STAT)	printk("Error ");
	}		
	printk("}\n");
	if((status & (BUSY_STAT|ERR_STAT)) == ERR_STAT)
	{
		u8 err = io->err_feature;
		printk(" { ");
		if (err & ABRT_ERR)	printk("DriveStatusError ");
		if (err & ICRC_ERR)	printk("Bad%s ", (err & ABRT_ERR) ? "CRC" : "Sector");
		if (err & ECC_ERR)	printk("UncorrectableError ");
		if (err & ID_ERR)	printk("SectorIdNotFound ");
		if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (err & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
	}
	/* We know the block ID but don't dump it yet */
}

/**
 *	edp2_io_reply	-	I/O reply handler
 *	@edp: EDP header of reply
 *	@skb: skbuff holding remainder of data
 *
 *	Called when the EDP protocol layer receives a FID 0 frame
 *	for this host.
 */

static int edp2_block_reply(struct edp *edp, struct sk_buff *skb)
{
	unsigned long flags;
	struct edp2_request *ireq = NULL;
	struct edp2_device *dev = edp2_find(skb->mac.raw+6);
	struct edp2_ata_fid0 *io;
	u8 status;
	
	/* Refuse short frames */
	if(skb->len < sizeof(struct edp2_ata_fid0))
		return 0;
		
	io = skb_pull(skb, sizeof(struct edp2_ata_fid0));

	/* Find the request */
	ireq = edp2_find_request(edp->tag);
	if(ireq == NULL)
		return 0;

	/* We don't yet know if this is a "real" block request or
	   a handler completion. ireq->type tells us this */
	   			
	spin_lock_prefetch(&io_request_lock);

	/* Error from the controller layer */
	if(edp->flag_err & EDP_F_ERROR)
	{		
		edp_report_error(edp->flag_err & EDP_F_ERRMASK);
		spin_lock_irqsave(&io_request_lock, flags);
		edp2_unhook_request(ireq, c->unit);
		if(ireq->type == 0)
			ireq->req->errors++;
		edp2_end_request(ireq->req);
		spin_unlock_irqrestore(&io_request_lock, flags);
		return;
	}

	/* Now check for ATA error */
	status = io->cmd_status;
	
	if((status&(READY_STAT|BAD_STAT))==BAD_STAT)
	{
		edp2_report_ata_error(dev, io);
		ireq->req->errors++;	
	}
	else
		ireq->req->errors = 0;

	/*
	 *	Dequeue the request. We use irqsave locks as one day we
	 *	may be running polled controllers from a BH...
	 */
	
	spin_lock_irqsave(&io_request_lock, flags);
	edp2_unhook_request(ireq, dev->unit);
	edp2_end_request(ireq->req);
	atomic_dec(&edp2_queues[dev->unit]->queue_depth);
	
	/*
	 *	We may be able to do more I/O
	 */
	 
	edp2_request(dev->req_queue);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

/*
 *	The EDP2 block driver is listed as one of those that pulls the
 *	front entry off the queue before processing it. This is important
 *	to remember here. If we drop the io lock then CURRENT will change
 *	on us. We must unlink CURRENT in this routine before we return, if
 *	we use it.
 */

static void edp2_request(request_queue_t *q)
{
	struct request *req;
	struct edp2_request *ireq;
	int unit;
	struct edp2_device *dev;
	struct sk_buff *m;
	
	while (!list_empty(&q->queue_head)) {
		/*
		 *	On an IRQ completion if there is an inactive
		 *	request on the queue head it means it isnt yet
		 *	ready to dispatch.
		 */
		req = blkdev_entry_next_request(&q->queue_head);

		if(req->rq_status == RQ_INACTIVE)
			return;
			
		unit = MINOR(req->rq_dev);
		dev = &edp2_dev[(unit&0xF0)];

		if(atomic_read(&edp2_queues[dev->unit]->queue_depth) >= dev->depth)
			break;
		
		/* Get a message */
		m = edp2_get(dev, 1514);

		if(m==NULL)
		{
			/* We need a deadlock breaker timeout here */
			if(atomic_read(&edp2_queues[dev->unit]->queue_depth) == 0)
				printk(KERN_ERR "edp2: message queue and request queue empty!!\n");
			break;
		}
		/*
		 * Everything ok, so pull from kernel queue onto our queue
		 */
		req->errors = 0;
		blkdev_dequeue_request(req);	
		req->waiting = NULL;
		
		ireq = edp2_queues[dev->unit]->edp2_qhead;
		edp2_queues[dev->unit]->edp2_qhead = ireq->next;
		ireq->req = req;

		edp2_send(m, dev, ireq, edp2_b[unit].start_sect, (unit&0xF0));
	}
}


/*
 *	SCSI-CAM for ioctl geometry mapping
 *	Duplicated with SCSI - this should be moved into somewhere common
 *	perhaps genhd ?
 *
 * LBA -> CHS mapping table taken from:
 *
 * "Incorporating the I2O Architecture into BIOS for Intel Architecture 
 *  Platforms" 
 *
 * This is an I2O document that is only available to I2O members,
 * not developers.
 *
 * From my understanding, this is how all the I2O cards do this
 *
 * Disk Size      | Sectors | Heads | Cylinders
 * ---------------+---------+-------+-------------------
 * 1 < X <= 528M  | 63      | 16    | X/(63 * 16 * 512)
 * 528M < X <= 1G | 63      | 32    | X/(63 * 32 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 *
 */
#define	BLOCK_SIZE_528M		1081344
#define	BLOCK_SIZE_1G		2097152
#define	BLOCK_SIZE_21G		4403200
#define	BLOCK_SIZE_42G		8806400
#define	BLOCK_SIZE_84G		17612800

static void edp2_block_biosparam(
	unsigned long capacity,
	unsigned short *cyls,
	unsigned char *hds,
	unsigned char *secs) 
{ 
	unsigned long heads, sectors, cylinders; 

	sectors = 63L;      			/* Maximize sectors per track */ 
	if(capacity <= BLOCK_SIZE_528M)
		heads = 16;
	else if(capacity <= BLOCK_SIZE_1G)
		heads = 32;
	else if(capacity <= BLOCK_SIZE_21G)
		heads = 64;
	else if(capacity <= BLOCK_SIZE_42G)
		heads = 128;
	else
		heads = 255;

	cylinders = capacity / (heads * sectors);

	*cyls = (unsigned short) cylinders;	/* Stuff return values */ 
	*secs = (unsigned char) sectors; 
	*hds  = (unsigned char) heads; 
}


/*
 *	Rescan the partition tables
 */
 
static int do_edp2_revalidate(kdev_t dev, int maxu)
{
	int minor=MINOR(dev);
	int i;
	
	minor&=0xF0;

	edp2_dev[minor].refcnt++;
	if(edp2_dev[minor].refcnt>maxu+1)
	{
		edp2_dev[minor].refcnt--;
		return -EBUSY;
	}
	
	for( i = 15; i>=0 ; i--)
	{
		int m = minor+i;
		invalidate_device(MKDEV(MAJOR_NR, m), 1);
		edp2_gendisk.part[m].start_sect = 0;
		edp2_gendisk.part[m].nr_sects = 0;
	}

	/*
	 *	Do a physical check and then reconfigure
	 */
	 
	edp2_install_device(edp2_dev[minor].controller, edp2_dev[minor].i2odev,
		minor);
	edp2_dev[minor].refcnt--;
	return 0;
}

/*
 *	Issue device specific ioctl calls.
 */

static int edp2_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct edp2_device *dev;
	int minor;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_EDP2_DISK<<4))
		return -ENODEV;

	dev = &edp2_dev[minor];
	switch (cmd) {
		case HDIO_GETGEO:
		{
			struct hd_geometry g;
			int u=minor&0xF0;
			i2o_block_biosparam(edp2_sizes[u]<<1, 
				&g.cylinders, &g.heads, &g.sectors);
			g.start = edp2_b[minor].start_sect;
			return copy_to_user((void *)arg,&g, sizeof(g))?-EFAULT:0;
		}
		
		case BLKRRPART:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return do_edp2_revalidate(inode->i_rdev,1);
			
		default:
			return blk_ioctl(inode->i_rdev, cmd, arg);
	}
	return 0;
}

/*
 *	Close the block device down
 */
 
static int edp2_release(struct inode *inode, struct file *file)
{
	struct edp2_device *dev;
	int minor;

	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_EDP2_DISK<<4))
		return -ENODEV;
	dev = &edp2_dev[(minor&0xF0)];

	/*
	 * This is to deail with the case of an application
	 * opening a device and then the device dissapears while
	 * it's in use, and then the application tries to release
	 * it.  ex: Unmounting a deleted RAID volume at reboot. 
	 * If we send messages, it will just cause FAILs since
	 * the TID no longer exists.
	 */
	if(!dev->i2odev)
		return 0;

	if (dev->refcnt <= 0)
		printk(KERN_ALERT "edp2_release: refcount(%d) <= 0\n", dev->refcnt);
	dev->refcnt--;
	if(dev->refcnt==0)
	{
		epd2_flush_cache_wait(dev);
		if(edp2_unclaim(dev))
			printk(KERN_ERR "edp2_release: controller rejected unclaim.\n");
	}
	return 0;
}

/*
 *	Open the block device.
 */
 
static int edp2_open(struct inode *inode, struct file *file)
{
	int minor;
	struct edp2_device *dev;
	
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= MAX_EDP2_DISK<<4)
		return -ENODEV;
	dev=&edp2_dev[(minor&0xF0)];

	if(!dev->dev)	
		return -ENODEV;
	
	if(dev->refcnt++==0)
	{ 
		if(edp2_claim_wait(dev)<0)
		{
			dev->refcnt--;
			printk(KERN_INFO "EDP2: Could not claim device.\n");
			return -EBUSY;
		}
		DEBUG("Claimed ");
	}		
	return 0;
}

/*
 *	Install the EDP2 block device we found.
 */
 
static int edp2_install_device(struct edp2_device *dev)
{
	u64 size;
	u32 blocksize;
	u8 type;
	u16 power;
	u32 flags, status;
	int i;

	/*
	 * For logging purposes...
	 */
	printk(KERN_INFO "edp2_b: Installing tid %d device at unit %d\n", 
			d->lct_data.tid, unit);	

	/*
	 *	Ask for the current media data. If that isn't supported
	 *	then we ask for the device capacity data
	 */
	if(edp2_query_device(dev, 0x0004, 1, &blocksize, 4) != 0
	  || edp2_query_device(dev, 0x0004, 0, &size, 8) !=0 )
	{
		edp2_query_device(dev, 0x0000, 3, &blocksize, 4);
		edp2_query_device(dev, 0x0000, 4, &size, 8);
	}
	
	if(edp2_query_device(dev, 0x0000, 2, &power, 2)!=0)
		power = 0;
	edp2_query_device(dev, 0x0000, 5, &flags, 4);
	edp2_query_device(dev, 0x0000, 6, &status, 4);
	edp2_sizes[unit] = (int)(size>>10);
	for(i=unit; i <= unit+15 ; i++)
		edp2_hardsizes[i] = blocksize;
	edp2_gendisk.part[unit].nr_sects = size>>9;
	edp2_b[unit].nr_sects = (int)(size>>9);

	/*
	 * Max number of Scatter-Gather Elements
	 */	

	edp2_dev[unit].power = power;	/* Save power state in device proper */
	edp2_dev[unit].flags = flags;

	for(i=unit;i<=unit+15;i++)
	{
		edp2_dev[i].power = power;	/* Save power state */
		edp2_dev[unit].flags = flags;	/* Keep the type info */
		edp2_max_sectors[i] = 96;	/* 256 might be nicer but many controllers 
						   explode on 65536 or higher */
		edp2_dev[i].max_segments = (d->controller->status_block->inbound_frame_size - 7) / 2;
		
		edp2_dev[i].rcache = CACHE_SMARTFETCH;
		edp2_dev[i].wcache = CACHE_WRITETHROUGH;
		
		if(d->controller->battery == 0)
			edp2_dev[i].wcache = CACHE_WRITETHROUGH;

		if(d->controller->type == I2O_TYPE_PCI && d->controller->bus.pci.promise)
			edp2_dev[i].wcache = CACHE_WRITETHROUGH;

		if(d->controller->type == I2O_TYPE_PCI && d->controller->bus.pci.short_req)
		{
			edp2_max_sectors[i] = 8;
			edp2_dev[i].max_segments = 8;
		}
	}

	sprintf(d->dev_name, "%s%c", edp2_gendisk.major_name, 'a' + (unit>>4));

	printk(KERN_INFO "%s: Max segments %d, queue depth %d, byte limit %d.\n",
		 d->dev_name, edp2_dev[unit].max_segments, edp2_dev[unit].depth, edp2_max_sectors[unit]<<9);

	edp2_query_device(dev, 0x0000, 0, &type, 1);

	printk(KERN_INFO "%s: ", d->dev_name);
	switch(type)
	{
		case 0: printk("Disk Storage");break;
		case 4: printk("WORM");break;
		case 5: printk("CD-ROM");break;
		case 7:	printk("Optical device");break;
		default:
			printk("Type %d", type);
	}
	if(status&(1<<10))
		printk("(RAID)");

	if((flags^status)&(1<<4|1<<3))	/* Missing media or device */
	{
		printk(KERN_INFO " Not loaded.\n");
		/* Device missing ? */
		if((flags^status)&(1<<4))
			return 1;
	}
	else
	{
		printk(": %dMB, %d byte sectors",
			(int)(size>>20), blocksize);
	}
	if(status&(1<<0))
	{
		u32 cachesize;
		edp2_query_device(dev, 0x0003, 0, &cachesize, 4);
		cachesize>>=10;
		if(cachesize>4095)
			printk(", %dMb cache", cachesize>>10);
		else
			printk(", %dKb cache", cachesize);
	}
	printk(".\n");
	printk(KERN_INFO "%s: Maximum sectors/read set to %d.\n", 
		d->dev_name, edp2_max_sectors[unit]);

	/* 
	 * If this is the first I2O block device found on this IOP,
	 * we need to initialize all the queue data structures
	 * before any I/O can be performed. If it fails, this
	 * device is useless.
	 */
	if(!edp2_queues[c->unit]) {
		if(edp2_init_iop(c->unit))
			return 1;
	}

	/* 
	 * This will save one level of lookup/indirection in critical 
	 * code so that we can directly get the queue ptr from the
	 * device instead of having to go the IOP data structure.
	 */
	dev->req_queue = &edp2_queues[c->unit]->req_queue;

	grok_partitions(&edp2_gendisk, unit>>4, 1<<4, (long)(size>>9));

	return 0;
}

/*
 * Initialize IOP specific queue structures.  This is called
 * once for each IOP that has a block device sitting behind it.
 */
static int edp2_init_iop(unsigned int unit)
{
	int i;

	edp2_queues[unit] = (struct edp2_iop_queue *) kmalloc(sizeof(struct edp2_iop_queue), GFP_ATOMIC);
	if(!edp2_queues[unit])
	{
		printk(KERN_WARNING "Could not allocate request queue for I2O block device!\n");
		return -1;
	}

	for(i = 0; i< MAX_EDP2_DEPTH; i++)
	{
		edp2_queues[unit]->request_queue[i].next =  &edp2_queues[unit]->request_queue[i+1];
		edp2_queues[unit]->request_queue[i].num = i;
	}
	
	/* Queue is MAX_EDP2_DISK + 1... */
	edp2_queues[unit]->request_queue[i].next = NULL;
	edp2_queues[unit]->edp2_qhead = &edp2_queues[unit]->request_queue[0];
	atomic_set(&edp2_queues[unit]->queue_depth, 0);

	blk_init_queue(&edp2_queues[unit]->req_queue, edp2_request);
	blk_queue_headactive(&edp2_queues[unit]->req_queue, 0);
	edp2_queues[unit]->req_queue.back_merge_fn = edp2_back_merge;
	edp2_queues[unit]->req_queue.front_merge_fn = edp2_front_merge;
	edp2_queues[unit]->req_queue.merge_requests_fn = edp2_merge_requests;
	edp2_queues[unit]->req_queue.queuedata = &edp2_queues[unit];

	return 0;
}

/*
 * Get the request queue for the given device.
 */	
static request_queue_t* edp2_get_queue(kdev_t dev)
{
	int unit = MINOR(dev)&0xF0;
	return edp2_dev[unit].req_queue;
}

/*
 * Probe the I2O subsytem for block class devices
 */
static void edp2_scan(int bios)
{
	int i;
	int warned = 0;

	struct i2o_device *d, *b=NULL;
	struct i2o_controller *c;
	struct edp2_device *dev;
		
	for(i=0; i< MAX_I2O_CONTROLLERS; i++)
	{
		c=i2o_find_controller(i);
	
		if(c==NULL)
			continue;

		/*
		 *    The device list connected to the I2O Controller is doubly linked
		 * Here we traverse the end of the list , and start claiming devices
		 * from that end. This assures that within an I2O controller atleast
		 * the newly created volumes get claimed after the older ones, thus
		 * mapping to same major/minor (and hence device file name) after 
		 * every reboot.
		 * The exception being: 
		 * 1. If there was a TID reuse.
		 * 2. There was more than one I2O controller. 
		 */

		if(!bios)
		{
			for (d=c->devices;d!=NULL;d=d->next)
			if(d->next == NULL)
				b = d;
		}
		else
			b = c->devices;

		while(b != NULL)
		{
			d=b;
			if(bios)
				b = b->next;
			else
				b = b->prev;

			if(d->lct_data.class_id!=I2O_CLASS_RANDOM_BLOCK_STORAGE)
				continue;

			if(d->lct_data.user_tid != 0xFFF)
				continue;

			if(bios)
			{
				if(d->lct_data.bios_info != 0x80)
					continue;
				printk(KERN_INFO "Claiming as Boot device: Controller %d, TID %d\n", c->unit, d->lct_data.tid);
			}
			else
			{
				if(d->lct_data.bios_info == 0x80)
					continue; /*Already claimed on pass 1 */
			}

			if(i2o_claim_device(d, &i2o_block_handler))
			{
				printk(KERN_WARNING "i2o_block: Controller %d, TID %d\n", c->unit,
					d->lct_data.tid);
				printk(KERN_WARNING "\t%sevice refused claim! Skipping installation\n", bios?"Boot d":"D");
				continue;
			}

			if(scan_unit<MAX_EDP2_DISK<<4)
			{
 				/*
				 * Get the device and fill in the
				 * Tid and controller.
				 */
				dev=&edp2_dev[scan_unit];
				dev->i2odev = d; 
				dev->controller = c;
				dev->unit = c->unit;
				dev->tid = d->lct_data.tid;

				if(edp2_install_device(c,d,scan_unit))
					printk(KERN_WARNING "Could not install I2O block device\n");
				else
				{
					scan_unit+=16;
					edp2_dev_count++;

					/* We want to know when device goes away */
					i2o_device_notify_on(d, &i2o_block_handler);
				}
			}
			else
			{
				if(!warned++)
					printk(KERN_WARNING "i2o_block: too many device, registering only %d.\n", scan_unit>>4);
			}
			i2o_release_device(d, &i2o_block_handler);
		}
		i2o_unlock_controller(c);
	}
}

static void edp2_probe(void)
{
	/*
	 *      Some overhead/redundancy involved here, while trying to
	 *      claim the first boot volume encountered as /dev/i2o/hda
	 *      everytime. All the i2o_controllers are searched and the
	 *      first i2o block device marked as bootable is claimed
	 *      If an I2O block device was booted off , the bios sets
	 *      its bios_info field to 0x80, this what we search for.
	 *      Assuming that the bootable volume is /dev/i2o/hda
	 *      everytime will prevent any kernel panic while mounting
	 *      root partition
	 */

	printk(KERN_INFO "i2o_block: Checking for Boot device...\n");
	edp2_scan(1);

	/*
	 *      Now the remainder.
	 */
	printk(KERN_INFO "i2o_block: Checking for I2O Block devices...\n");
	edp2_scan(0);
}


/*
 * New device notification handler.  Called whenever a new
 * EDP2 block storage device is added to the system.
 * 
 * Should we spin lock around this to keep multiple devs from 
 * getting updated at the same time? 
 * 
 */
void edp2_new_device(struct i2o_controller *c, struct i2o_device *d)
{
	struct edp2_device *dev;
	int unit = 0;

	printk(KERN_INFO "i2o_block: New device detected\n");
	printk(KERN_INFO "   Controller %d Tid %d\n",c->unit, d->lct_data.tid);

	/* Check for available space */
	if(edp2_dev_count>=MAX_EDP2_DISK<<4)
	{
		printk(KERN_ERR "i2o_block: No more devices allowed!\n");
		return;
	}
	for(unit = 0; unit < (MAX_EDP2_DISK<<4); unit += 16)
	{
		if(!edp2_dev[unit].i2odev)
			break;
	}

	if(i2o_claim_device(d, &i2o_block_handler))
	{
		printk(KERN_INFO "i2o_block: Unable to claim device. Installation aborted\n");
		return;
	}

	dev = &edp2_dev[unit];
	dev->i2odev = d; 
	dev->controller = c;
	dev->tid = d->lct_data.tid;

	if(edp2_install_device(c,d,unit))
		printk(KERN_ERR "i2o_block: Could not install new device\n");
	else	
	{
		edp2_dev_count++;
		i2o_device_notify_on(d, &i2o_block_handler);
	}

	i2o_release_device(d, &i2o_block_handler);
 
	return;
}

/*
 * Deleted device notification handler.  Called when a device we
 * are talking to has been deleted by the user or some other
 * mysterious fource outside the kernel.
 */
 
void edp2_del_device(struct i2o_controller *c, struct i2o_device *d)
{	
	int unit = 0;
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);

	printk(KERN_INFO "EDP2 Block Device Deleted\n");

	for(unit = 0; unit < MAX_EDP2_DISK<<4; unit += 16)
	{
		if(edp2_dev[unit].i2odev == d)
		{
			printk(KERN_INFO "  /dev/%s: Controller %d Tid %d\n", 
				d->dev_name, c->unit, d->lct_data.tid);
			break;
		}
	}
	if(unit >= MAX_EDP2_DISK<<4)
	{
		printk(KERN_ERR "edp2_del_device called, but not in dev table!\n");
		spin_unlock_irqrestore(&io_request_lock, flags);
		return;
	}

	/* 
	 * This will force errors when edp2_get_queue() is called
	 * by the kenrel.
	 */
	edp2_dev[unit].req_queue = NULL;
	for(i = unit; i <= unit+15; i++)
	{
		edp2_dev[i].i2odev = NULL;
		edp2_sizes[i] = 0;
		edp2_hardsizes[i] = 0;
		edp2_max_sectors[i] = 0;
		edp2_b[i].nr_sects = 0;
		edp2_gendisk.part[i].nr_sects = 0;
	}
	spin_unlock_irqrestore(&io_request_lock, flags);

	/*
	 * Decrease usage count for module
	 */	

	while(edp2_dev[unit].refcnt--)
		MOD_DEC_USE_COUNT;

	edp2_dev[unit].refcnt = 0;
	
	edp2_dev_count--;	
}

/*
 *	Have we seen a media change ?
 */

static int edp2_media_change(kdev_t dev)
{
	return 0;
}

static int edp2_revalidate(kdev_t dev)
{
	return do_edp2_revalidate(dev, 0);
}

/*
 * Reboot notifier.  This is called by kernel core when the system
 * shuts down.
 */
static void edp2_reboot_event(void)
{
	int i;
	
	for(i=0;i<MAX_EDP2_DISK;i++)
	{
		struct edp2_device *dev=&edp2_dev[(i<<4)];
		if(dev->refcnt!=0)
			edp2_flush_cache_wait(dev);
	}	
}

static struct block_device_operations edp2_fops =
{
	owner:			THIS_MODULE,
	open:			edp2_open,
	release:		edp2_release,
	ioctl:			edp2_ioctl,
	check_media_change:	edp2_media_change,
	revalidate:		edp2_revalidate,
};

static struct gendisk edp2_gendisk = 
{
	major:		MAJOR_NR,
	major_name:	"edp2/hd",
	minor_shift:	4,
	max_p:		1<<4,
	part:		edp2_b,
	sizes:		edp2_sizes,
	nr_real:	MAX_EDP2_DISK,
	fops:		&edp2_fops,
};


/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static int edp2_block_init(void)
{
	int i;

	printk(KERN_INFO "EDP2 Coraid Storage v0.01\n");
	printk(KERN_INFO "   (c) Copyright 2003 Red Hat Software.\n");
	
	/*
	 *	Register the block device interfaces
	 */

	if (register_blkdev(MAJOR_NR, "edp2", &edp2_fops)) {
		printk(KERN_ERR "Unable to get major number %d for i2o_block\n",
		       MAJOR_NR);
		return -EIO;
	}

	/*
	 *	Now fill in the boiler plate
	 */
	 
	blksize_size[MAJOR_NR] = edp2_blksizes;
	hardsect_size[MAJOR_NR] = edp2_hardsizes;
	blk_size[MAJOR_NR] = edp2_sizes;
	max_sectors[MAJOR_NR] = edp2_max_sectors;
	blk_dev[MAJOR_NR].queue = edp2_get_queue;
	
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), edp2_request);
	blk_queue_headactive(BLK_DEFAULT_QUEUE(MAJOR_NR), 0);

	for (i = 0; i < MAX_EDP2_DISK; i++) {
		edp2_dev[i].refcnt = 0;
		edp2_dev[i].flags = 0;
		edp2_dev[i].controller = NULL;
		edp2_dev[i].i2odev = NULL;
		edp2_dev[i].tid = 0;
		edp2_dev[i].head = NULL;
		edp2_dev[i].tail = NULL;
		edp2_dev[i].depth = MAX_EDP2_DEPTH;
		edp2_blksizes[i] = 512;
		edp2_max_sectors[i] = 2;
		edp2_queues[i] = NULL;
	}

	/*
	 *	Register the OSM handler as we will need this to probe for
	 *	drives, geometry and other goodies.
	 */

	if(i2o_install_handler(&i2o_block_handler)<0)
	{
		unregister_blkdev(MAJOR_NR, "i2o_block");
		blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
		printk(KERN_ERR "i2o_block: unable to register OSM.\n");
		return -EINVAL;
	}
	edp2_context = i2o_block_handler.context;	 

	/*
	 *	Finally see what is actually plugged in to our controllers
	 */
	for (i = 0; i < MAX_EDP2_DISK; i++)
		register_disk(&edp2_gendisk, MKDEV(MAJOR_NR,i<<4), 1<<4,
			&edp2_fops, 0);
	edp2_probe();

	/*
	 *	Adding edp2_gendisk into the gendisk list.
	 */
	add_gendisk(&edp2_gendisk);

	return 0;
}


static void edp2_block_exit(void)
{
	int i;
	
	/*
	 *	Flush the OSM
	 */

	i2o_remove_handler(&i2o_block_handler);
		 
	/*
	 *	Return the block device
	 */
	if (unregister_blkdev(MAJOR_NR, "edp2") != 0)
		printk("i2o_block: cleanup_module failed\n");

	/*
	 * free request queue
	 */
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
	del_gendisk(&edp2_gendisk);
}

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("Coraid EDP2");
MODULE_LICENSE("GPL");

module_init(edp2_block_init);
module_exit(edp2_block_exit);

