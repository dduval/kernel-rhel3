/* -*- linux-c -*-
 * viodasd.c
 *  Authors: Dave Boutcher <boutcher@us.ibm.com>
 *           Ryan Arnold <ryanarn@us.ibm.com>
 *           Colin Devilbiss <devilbis@us.ibm.com>
 *
 * (C) Copyright 2000 IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 ***************************************************************************
 * This routine provides access to disk space (termed "DASD" in historical
 * IBM terms) owned and managed by an OS/400 partition running on the
 * same box as this Linux partition.
 *
 * All disk operations are performed by sending messages back and forth to 
 * the OS/400 partition. 
 * 
 * This device driver can either use its own major number, or it can
 * pretend to be an IDE drive (grep 'IDE[0-9]_MAJOR' ../../include/linux/major.h).
 * This is controlled with a CONFIG option.  You can either call this an
 * elegant solution to the fact that a lot of software doesn't recognize
 * a new disk major number...or you can call this a really ugly hack.
 * Your choice.
 */

#include <linux/major.h>
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/types.h>
#include <asm/semaphore.h>
#include <linux/seq_file.h>

/* Changelog:
	2001-11-27	devilbis	Added first pass at complete IDE emulation
	2002-07-07      boutcher        Added randomness
 */

/* Decide if we are using our own major or pretending to be an IDE drive
 *
 * If we are using our own major, we only support 7 partitions per physical
 * disk....so with minor numbers 0-255 we get a maximum of 32 disks.  If we
 * are emulating IDE, we get 63 partitions per disk, with a maximum of 4
 * disks per major, but common practice is to place only 2 devices in /dev
 * for each IDE major, for a total of 20 (since there are 10 IDE majors).
 */

static const int major_table[] = {
	VIODASD_MAJOR,
};
enum {
	DEV_PER_MAJOR = 32,
	PARTITION_SHIFT = 3,
};
static inline int major_to_index(int major)
{
	if(major != VIODASD_MAJOR)
		return -1;
	return 0;
}
#define VIOD_DEVICE_NAME "viod"
#ifdef CONFIG_DEVFS_FS
#define VIOD_GENHD_NAME "viod"
#else
#define VIOD_GENHD_NAME "iseries/vd"
#endif

#define DEVICE_NR(dev) (devt_to_diskno(dev))
#define LOCAL_END_REQUEST

#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/fd.h>
#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/capability.h>

#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/iSeries/HvLpConfig.h>
#include "vio.h"
#include <asm/iSeries/iSeries_proc.h>

MODULE_DESCRIPTION("iSeries Virtual DASD");
MODULE_AUTHOR("Dave Boutcher");
MODULE_LICENSE("GPL");

#define VIODASD_VERS "1.60"

enum {
	NUM_MAJORS = sizeof(major_table) / sizeof(major_table[0]),
	MAX_DISKNO = DEV_PER_MAJOR * NUM_MAJORS,
	MAX_MAJOR_NAME = 16 + 1, /* maximum length of a gendisk->name */
};

static volatile int viodasd_max_disk = MAX_DISKNO - 1;

static inline int diskno_to_major(int diskno)
{
	if (diskno >= MAX_DISKNO)
		return -1;
	return major_table[diskno / DEV_PER_MAJOR];
}
static inline int devt_to_diskno(kdev_t dev)
{
	return major_to_index(MAJOR(dev)) * DEV_PER_MAJOR +
	    (MINOR(dev) >> PARTITION_SHIFT);
}
static inline int diskno_to_devt(int diskno, int partition)
{
	return MKDEV(diskno_to_major(diskno),
		     ((diskno % DEV_PER_MAJOR) << PARTITION_SHIFT) +
		     partition);
}

#define VIOMAXREQ 16
#define VIOMAXBLOCKDMA        12

extern struct pci_dev *iSeries_vio_dev;

struct openData {
	u64 mDiskLen;
	u16 mMaxDisks;
	u16 mCylinders;
	u16 mTracks;
	u16 mSectors;
	u16 mBytesPerSector;
};

struct rwData {			// Used during rw
	u64 mOffset;
	struct {
		u32 mToken;
		u32 reserved;
		u64 mLen;
	} dmaInfo[VIOMAXBLOCKDMA];
};

struct vioblocklpevent {
	struct HvLpEvent event;
	u32 mReserved1;
	u16 mVersion;
	u16 mSubTypeRc;
	u16 mDisk;
	u16 mFlags;
	union {
		struct openData openData;
		struct rwData rwData;
		struct {
			u64 changed;
		} check;
	} u;
};

#define vioblockflags_ro   0x0001

enum vioblocksubtype {
	vioblockopen = 0x0001,
	vioblockclose = 0x0002,
	vioblockread = 0x0003,
	vioblockwrite = 0x0004,
	vioblockflush = 0x0005,
	vioblockcheck = 0x0007
};

/* In a perfect world we will perform better if we get page-aligned I/O
 * requests, in multiples of pages.  At least peg our block size to the
 * actual page size.
 */
static int blksize = HVPAGESIZE;	/* in bytes */

static DECLARE_WAIT_QUEUE_HEAD(viodasd_wait);
struct viodasd_waitevent {
	struct semaphore *sem;
	int rc;
	union {
		int changed;	/* Used only for check_change */
		u16 subRC;
	} data;
};

static const struct vio_error_entry viodasd_err_table[] = {
	{0x0201, EINVAL, "Invalid Range"},
	{0x0202, EINVAL, "Invalid Token"},
	{0x0203, EIO, "DMA Error"},
	{0x0204, EIO, "Use Error"},
	{0x0205, EIO, "Release Error"},
	{0x0206, EINVAL, "Invalid Disk"},
	{0x0207, EBUSY, "Cant Lock"},
	{0x0208, EIO, "Already Locked"},
	{0x0209, EIO, "Already Unlocked"},
	{0x020A, EIO, "Invalid Arg"},
	{0x020B, EIO, "Bad IFS File"},
	{0x020C, EROFS, "Read Only Device"},
	{0x02FF, EIO, "Internal Error"},
	{0x0000, 0, NULL},
};

/* Our gendisk table
 */
static struct gendisk viodasd_gendisk[NUM_MAJORS];

static inline struct gendisk *major_to_gendisk(int major)
{
	int index = major_to_index(major);
	return index < 0 ? NULL : &viodasd_gendisk[index];
}
static inline struct hd_struct *devt_to_partition(kdev_t dev)
{
	return &major_to_gendisk(MAJOR(dev))->part[MINOR(dev)];
}

/* Figure out the biggest I/O request (in sectors) we can accept
 */
#define VIODASD_MAXSECTORS (4096 / 512 * VIOMAXBLOCKDMA)

/* Keep some statistics on what's happening for the PROC file system
 */
static struct {
	long tot;
	long nobh;
	long ntce[VIOMAXBLOCKDMA];
} viod_stats[MAX_DISKNO][2];

/* Number of disk I/O requests we've sent to OS/400
 */
static int num_req_outstanding;

/* This is our internal structure for keeping track of disk devices
 */
struct viodasd_device {
	int useCount;
	u16 cylinders;
	u16 tracks;
	u16 sectors;
	u16 bytesPerSector;
	u64 size;
	int readOnly;
} *viodasd_devices;

/* When we get a disk I/O request we take it off the general request queue
 * and put it here.
 */
static LIST_HEAD(reqlist);

/* Handle reads from the proc file system
 */
static int show_viodasd(struct seq_file *m, void *v)
{
	int i;
	int j;

#if defined(MODULE)
	    seq_printf(m,
		    "viod Module opened %d times.  Major number %d\n",
		    MOD_IN_USE, major_table[0]);
#endif
	seq_printf(m, "viod %d possible devices\n", MAX_DISKNO);

	for (i = 0; i <= viodasd_max_disk && i < MAX_DISKNO; i++) {
		if (viod_stats[i][0].tot || viod_stats[i][1].tot) {
			seq_printf(m,
				    "DISK %2.2d: rd %-10.10ld wr %-10.10ld (no buffer list rd %-10.10ld wr %-10.10ld\n",
				    i, viod_stats[i][0].tot,
				    viod_stats[i][1].tot,
				    viod_stats[i][0].nobh,
				    viod_stats[i][1].nobh);

			seq_printf(m, "rd DMA: ");

			for (j = 0; j < VIOMAXBLOCKDMA; j++)
				seq_printf(m, " [%2.2d] %ld",
					       j,
					       viod_stats[i][0].ntce[j]);

			seq_printf(m, "\nwr DMA: ");

			for (j = 0; j < VIOMAXBLOCKDMA; j++)
				seq_printf(m, " [%2.2d] %ld",
					       j,
					       viod_stats[i][1].ntce[j]);
			seq_printf(m, "\n");
		}
	}

	return 0;
}

/* associate proc_viodasd_sops with this seq_file */
static int viodasd_proc_open(struct inode *inode, struct file *file)
{
	size_t size = PAGE_SIZE;
	char *buf = kmalloc(size, GFP_KERNEL);
	struct seq_file *m;
	int status;

	if (!buf)
		return -ENOMEM;
	status = single_open(file, show_viodasd, NULL);
	if (!status) {
		m = file->private_data;
		m->buf = buf;
		m->size = size;
	} else
		kfree(buf);
	return status;
}

static struct file_operations proc_viodasd_fops = {
        .open = viodasd_proc_open,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

/* setup our proc file system entries
 */
void viodasd_proc_init(struct proc_dir_entry *iSeries_proc)
{
	struct proc_dir_entry *ent;
	ent =
	    create_proc_entry("viodasd", S_IFREG | S_IRUSR, iSeries_proc);
	if (!ent)
		return;
	ent->owner = THIS_MODULE;
	ent->proc_fops = &proc_viodasd_fops;
}

/* clean up our proc file system entries
 */
void viodasd_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	remove_proc_entry("viodasd", iSeries_proc);
}

/* Strip a single buffer-head off of a request; if it's the last
 * one, also dequeue it and clean it up
 */
static int viodasd_end_request(struct request *req, int uptodate)
{
	if (end_that_request_first(req, uptodate, VIOD_DEVICE_NAME))
		return 1;

	add_blkdev_randomness(MAJOR(req->rq_dev));

	list_del(&req->queue);
	end_that_request_last(req);
	return 0;
}

static void viodasd_end_request_with_error(struct request *req)
{
	while(viodasd_end_request(req, 0))
		;
}

/* This rebuilds the partition information for a single disk device
 */
static int viodasd_revalidate(kdev_t dev)
{
	int i;
	int device_no = DEVICE_NR(dev);
	int dev_within_major = device_no % DEV_PER_MAJOR;
	int part0 = (dev_within_major << PARTITION_SHIFT);
	int npart = (1 << PARTITION_SHIFT);
	int major = MAJOR(dev);
	struct gendisk *gendisk = major_to_gendisk(major);

	if (viodasd_devices[device_no].size == 0)
		return 0;

	for (i = npart - 1; i >= 0; i--) {
		int minor = part0 + i;
		struct hd_struct *partition = &gendisk->part[minor];

		if (partition->nr_sects != 0) {
			kdev_t devp = MKDEV(major, minor);
			struct super_block *sb;
			fsync_dev(devp);

			sb = get_super(devp);
			if (sb)
				invalidate_inodes(sb);

			invalidate_buffers(devp);
		}

		partition->start_sect = 0;
		partition->nr_sects = 0;
	}

	grok_partitions(gendisk, dev_within_major, npart,
			viodasd_devices[device_no].size >> 9);

	return 0;
}


static inline u16 access_flags(mode_t mode)
{
	u16 flags = 0;
	if (!(mode & FMODE_WRITE))
		flags |= vioblockflags_ro;
	return flags;
}

static void internal_register_disk(int diskno);

/* This is the actual open code.  It gets called from the external
 * open entry point, as well as from the init code when we're figuring
 * out what disks we have
 */
static int internal_open(int device_no, u16 flags)
{
	int i;
	const int dev_within_major = device_no % DEV_PER_MAJOR;
	struct gendisk *gendisk =
	    major_to_gendisk(diskno_to_major(device_no));
	HvLpEvent_Rc hvrc;
	/* This semaphore is raised in the interrupt handler                     */
	DECLARE_MUTEX_LOCKED(Semaphore);
	struct viodasd_waitevent we = { sem:&Semaphore };

	/* Check that we are dealing with a valid hosting partition              */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -ENXIO;
	}

	/* Send the open event to OS/400                                         */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
					     HvLpEvent_Type_VirtualIo,
					     viomajorsubtype_blockio |
					     vioblockopen,
					     HvLpEvent_AckInd_DoAck,
					     HvLpEvent_AckType_ImmediateAck,
					     viopath_sourceinst
					     (viopath_hostLp),
					     viopath_targetinst
					     (viopath_hostLp),
					     (u64) (unsigned long) &we,
					     VIOVERSION << 16,
					     ((u64) device_no << 48) |
					     ((u64) flags << 32), 0, 0, 0);

	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEvent %d\n",
		       (int) hvrc);
		return -ENXIO;
	}

	/* Wait for the interrupt handler to get the response                    */
	down(&Semaphore);

	/* Check the return code                                                 */
	if (we.rc != 0) {
		const struct vio_error_entry *err =
		    vio_lookup_rc(viodasd_err_table, we.data.subRC);
		/* Temporary patch to quiet down the viodasd when drivers are probing    */
		/* for drives, especially lvm.  Collin is aware and is working on this.  */
		/* printk(KERN_WARNING_VIO                                               */
		/*       "bad rc opening disk: %d:0x%04x (%s)\n",                        */
		/*       (int) we.rc, we.data.subRC, err->msg);                          */
		return -err->errno;
	}
	
	/* If this is the first open of this device, update the device information */
	/* If this is NOT the first open, assume that it isn't changing            */
	if (viodasd_devices[device_no].useCount == 0) {
		if (viodasd_devices[device_no].size > 0) {
			/* divide by 512 */
			u64 tmpint = viodasd_devices[device_no].size >> 9;
			gendisk->part[dev_within_major << PARTITION_SHIFT].nr_sects = tmpint;
			/* Now the value divided by 1024 */
			tmpint = tmpint >> 1;
			gendisk->sizes[dev_within_major << PARTITION_SHIFT] = tmpint;

			for (i = dev_within_major << PARTITION_SHIFT;
			     i < ((dev_within_major + 1) << PARTITION_SHIFT);
			     i++)
			{
				hardsect_size[diskno_to_major(device_no)][i] =
				    viodasd_devices[device_no].bytesPerSector;
			}
		}
	} else {
		/* If the size of the device changed, weird things are happening!     */
		if (gendisk->sizes[dev_within_major << PARTITION_SHIFT] !=
		    viodasd_devices[device_no].size >> 10) {
			printk(KERN_WARNING_VIO
			       "disk size change (%dK to %dK) for device %d\n",
			       gendisk->sizes[dev_within_major << PARTITION_SHIFT],
			       (int) viodasd_devices[device_no].size >> 10, device_no);
		}
	}

	internal_register_disk(device_no);

	/* Bump the use count                                                      */
	viodasd_devices[device_no].useCount++;
	return 0;
}

/* This is the actual release code.  It gets called from the external
 * release entry point, as well as from the init code when we're figuring
 * out what disks we have
 */
static int internal_release(int device_no, u16 flags)
{
	/* Send the event to OS/400.  We DON'T expect a response                 */
	HvLpEvent_Rc hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
							  HvLpEvent_Type_VirtualIo,
							  viomajorsubtype_blockio
							  | vioblockclose,
							  HvLpEvent_AckInd_NoAck,
							  HvLpEvent_AckType_ImmediateAck,
							  viopath_sourceinst
							  (viopath_hostLp),
							  viopath_targetinst
							  (viopath_hostLp),
							  0,
							  VIOVERSION << 16,
							  ((u64) device_no
							   << 48) | ((u64)
								     flags
								     <<
								     32),
							  0, 0, 0);

	viodasd_devices[device_no].useCount--;

	if (hvrc != 0) {
		printk(KERN_WARNING_VIO
		       "bad rc sending event to OS/400 %d\n", (int) hvrc);
		return -EIO;
	}
	return 0;
}


/* External open entry point.
 */
static int viodasd_open(struct inode *ino, struct file *fil)
{
	int device_no;
	int old_max_disk = viodasd_max_disk;

	/* Do a bunch of sanity checks                                           */
	if (!ino) {
		printk(KERN_WARNING_VIO "no inode provided in open\n");
		return -ENXIO;
	}

	if (major_to_index(MAJOR(ino->i_rdev)) < 0) {
		printk(KERN_WARNING_VIO
		       "Weird error...wrong major number on open\n");
		return -ENXIO;
	}

	device_no = DEVICE_NR(ino->i_rdev);
	if (device_no > MAX_DISKNO || device_no < 0) {
		printk(KERN_WARNING_VIO
		       "Invalid device number %d in open\n", device_no);
		return -ENXIO;
	}

	/* Call the actual open code                                             */
	if (internal_open(device_no, access_flags(fil ? fil->f_mode : 0)) == 0) {
		int i;
		/* For each new disk: */
		/* update the disk's geometry via internal_open and register it */
		for (i = old_max_disk + 1; i <= viodasd_max_disk; ++i) {
			internal_open(i, vioblockflags_ro);
			internal_release(i, vioblockflags_ro);
		}
		if(devt_to_partition(ino->i_rdev)->nr_sects == 0) {
			internal_release(device_no, access_flags(fil ? fil->f_mode : 0));
			return -ENXIO;
		}
		return 0;
	} else {
		return -ENXIO;
	}
}

/* External release entry point.
 */
static int viodasd_release(struct inode *ino, struct file *fil)
{
	int device_no;

	/* Do a bunch of sanity checks                                           */
	if (!ino)
		BUG();

	if (major_to_index(MAJOR(ino->i_rdev)) < 0)
		BUG();

	device_no = DEVICE_NR(ino->i_rdev);

	if (device_no > MAX_DISKNO || device_no < 0)
		BUG();

	/* Call the actual release code                                          */
	internal_release(device_no, access_flags(fil ? fil->f_mode : 0));

	return 0;
}

/* External ioctl entry point.
 */
static int viodasd_ioctl(struct inode *ino, struct file *fil,
			 unsigned int cmd, unsigned long arg)
{
	int device_no;
	int err;
	HvLpEvent_Rc hvrc;
	struct hd_struct *partition;
	DECLARE_MUTEX_LOCKED(Semaphore);

	/* Sanity checks                                                        */
	if (!ino) {
		printk(KERN_WARNING_VIO "no inode provided in ioctl\n");
		return -ENODEV;
	}

	if (major_to_index(MAJOR(ino->i_rdev)) < 0) {
		printk(KERN_WARNING_VIO
		       "Weird error...wrong major number on ioctl\n");
		return -ENODEV;
	}

	partition = devt_to_partition(ino->i_rdev);

	device_no = DEVICE_NR(ino->i_rdev);
	if (device_no > viodasd_max_disk) {
		printk(KERN_WARNING_VIO
		       "Invalid device number %d in ioctl\n", device_no);
		return -ENODEV;
	}

	switch (cmd) {
	case BLKPG:
		return blk_ioctl(ino->i_rdev, cmd, arg);
	case BLKGETSIZE:
		/* return the device size in sectors */
		if (!arg)
			return -EINVAL;
		err =
		    verify_area(VERIFY_WRITE, (long *) arg, sizeof(long));
		if (err)
			return err;

		put_user(partition->nr_sects, (long *) arg);
		return 0;

	case FDFLUSH:
	case BLKFLSBUF:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		fsync_dev(ino->i_rdev);
		invalidate_buffers(ino->i_rdev);
		hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
						     HvLpEvent_Type_VirtualIo,
						     viomajorsubtype_blockio
						     | vioblockflush,
						     HvLpEvent_AckInd_DoAck,
						     HvLpEvent_AckType_ImmediateAck,
						     viopath_sourceinst
						     (viopath_hostLp),
						     viopath_targetinst
						     (viopath_hostLp),
						     (u64) (unsigned long)
						     &Semaphore,
						     VIOVERSION << 16,
						     ((u64) device_no <<
						      48), 0, 0, 0);


		if (hvrc != 0) {
			printk(KERN_WARNING_VIO
			       "bad rc on sync signalLpEvent %d\n",
			       (int) hvrc);
			return -EIO;
		}

		down(&Semaphore);

		return 0;

	case BLKRAGET:
		if (!arg)
			return -EINVAL;
		err = put_user(read_ahead[MAJOR(ino->i_rdev)], (long *) arg);
		return err;

	case BLKRASET:
		if (!suser())
			return -EACCES;
		if (arg > 0x00ff)
			return -EINVAL;
		read_ahead[MAJOR(ino->i_rdev)] = arg;
		return 0;

	case BLKRRPART:
		viodasd_revalidate(ino->i_rdev);
		return 0;

	case HDIO_GETGEO:
		{
			unsigned char sectors;
			unsigned char heads;
			unsigned short cylinders;

			struct hd_geometry *geo =
			    (struct hd_geometry *) arg;
			if (geo == NULL)
				return -EINVAL;

			err = verify_area(VERIFY_WRITE, geo, sizeof(*geo));
			if (err)
				return err;

			sectors = viodasd_devices[device_no].sectors;
			if (sectors == 0)
				sectors = 32;

			heads = viodasd_devices[device_no].tracks;
			if (heads == 0)
				heads = 64;

			cylinders = viodasd_devices[device_no].cylinders;
			if (cylinders == 0)
				cylinders =
				    partition->nr_sects / (sectors *
							   heads);

			put_user(sectors, &geo->sectors);
			put_user(heads, &geo->heads);
			put_user(cylinders, &geo->cylinders);

			put_user(partition->start_sect,
				 (long *) &geo->start);

			return 0;
		}

	case HDIO_GETGEO_BIG:
		{
			unsigned char sectors;
			unsigned char heads;
			unsigned int cylinders;

			struct hd_big_geometry *geo =
			    (struct hd_big_geometry *) arg;
			if (geo == NULL)
				return -EINVAL;

			err = verify_area(VERIFY_WRITE, geo, sizeof(*geo));
			if (err)
				return err;

			sectors = viodasd_devices[device_no].sectors;
			if (sectors == 0)
				sectors = 32;

			heads = viodasd_devices[device_no].tracks;
			if (heads == 0)
				heads = 64;

			cylinders = viodasd_devices[device_no].cylinders;
			if (cylinders == 0)
				cylinders =
				    partition->nr_sects / (sectors *
							   heads);

			put_user(sectors, &geo->sectors);
			put_user(heads, &geo->heads);
			put_user(cylinders, &geo->cylinders);

			put_user(partition->start_sect,
				 (long *) &geo->start);

			return 0;
		}

#define PRTIOC(x) case x: printk(KERN_WARNING_VIO "got unsupported FD ioctl " #x "\n"); \
                          return -EINVAL;

		PRTIOC(FDCLRPRM);
		PRTIOC(FDSETPRM);
		PRTIOC(FDDEFPRM);
		PRTIOC(FDGETPRM);
		PRTIOC(FDMSGON);
		PRTIOC(FDMSGOFF);
		PRTIOC(FDFMTBEG);
		PRTIOC(FDFMTTRK);
		PRTIOC(FDFMTEND);
		PRTIOC(FDSETEMSGTRESH);
		PRTIOC(FDSETMAXERRS);
		PRTIOC(FDGETMAXERRS);
		PRTIOC(FDGETDRVTYP);
		PRTIOC(FDSETDRVPRM);
		PRTIOC(FDGETDRVPRM);
		PRTIOC(FDGETDRVSTAT);
		PRTIOC(FDPOLLDRVSTAT);
		PRTIOC(FDRESET);
		PRTIOC(FDGETFDCSTAT);
		PRTIOC(FDWERRORCLR);
		PRTIOC(FDWERRORGET);
		PRTIOC(FDRAWCMD);
		PRTIOC(FDEJECT);
		PRTIOC(FDTWADDLE);

	}

	return -EINVAL;
}

/* Send an actual I/O request to OS/400
 */
static int send_request(struct request *req)
{
	u64 sect_size;
	u64 start;
	u64 len;
	int direction;
	int nsg;
	u16 viocmd;
	HvLpEvent_Rc hvrc;
	struct vioblocklpevent *bevent;
	struct scatterlist sg[VIOMAXBLOCKDMA];
	struct buffer_head *bh;
	int sgindex;
	int device_no = DEVICE_NR(req->rq_dev);
	int dev_within_major = device_no % DEV_PER_MAJOR;
	int statindex;
	struct hd_struct *partition = devt_to_partition(req->rq_dev);

	if (device_no > viodasd_max_disk || device_no < 0)
		BUG();
	
	/* Note that this SHOULD always be 512...but lets be architecturally correct */
	sect_size = hardsect_size[MAJOR(req->rq_dev)][dev_within_major];

	/* Figure out the starting sector and length                                 */
	start = (req->sector + partition->start_sect) * sect_size;
	len = req->nr_sectors * sect_size;

	/* More paranoia checks                                                      */
	if ((req->sector + req->nr_sectors) >
	    (partition->start_sect + partition->nr_sects)) {
		printk(KERN_WARNING_VIO
		       "Invalid request offset & length\n");
		printk(KERN_WARNING_VIO
		       "req->sector: %ld, req->nr_sectors: %ld\n",
		       req->sector, req->nr_sectors);
		printk(KERN_WARNING_VIO "major: %d, minor: %d\n",
		       MAJOR(req->rq_dev), MINOR(req->rq_dev));
		return -1;
	}

	if (req->cmd == READ || req->cmd == READA) {
		direction = PCI_DMA_FROMDEVICE;
		viocmd = viomajorsubtype_blockio | vioblockread;
		statindex = 0;
	} else {
		direction = PCI_DMA_TODEVICE;
		viocmd = viomajorsubtype_blockio | vioblockwrite;
		statindex = 1;
	}

	/* Update totals */
	viod_stats[device_no][statindex].tot++;

	/* Now build the scatter-gather list                                        */
	memset(&sg, 0x00, sizeof(sg));
	sgindex = 0;

	/* See if this is a swap I/O (without a bh pointer) or a regular I/O        */
	if (req->bh) {
		/* OK...this loop takes buffers from the request and adds them to the SG
		   until we're done, or until we hit a maximum.  If we hit a maximum we'll
		   just finish this request later                                       */
		bh = req->bh;
		while ((bh) && (sgindex < VIOMAXBLOCKDMA)) {
			sg[sgindex].address = bh->b_data;
			sg[sgindex].length = bh->b_size;

			sgindex++;
			bh = bh->b_reqnext;
		}
		nsg = pci_map_sg(iSeries_vio_dev, sg, sgindex, direction);
		if ((nsg == 0) || (sg[0].dma_length == 0)
		    || (sg[0].dma_address == 0xFFFFFFFF)) {
			printk(KERN_WARNING_VIO "error getting sg tces\n");
			return -1;
		}

	} else {
		/* Update stats */
		viod_stats[device_no][statindex].nobh++;

		sg[0].dma_address =
		    pci_map_single(iSeries_vio_dev, req->buffer, len,
				   direction);
		sg[0].dma_length = len;
		nsg = 1;
	}

	/* Update stats */
	viod_stats[device_no][statindex].ntce[sgindex]++;

	/* This optimization handles a single DMA block                          */
	if (sgindex == 1) {
		/* Send the open event to OS/400                                         */
		hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
						     HvLpEvent_Type_VirtualIo,
						     viomajorsubtype_blockio
						     | viocmd,
						     HvLpEvent_AckInd_DoAck,
						     HvLpEvent_AckType_ImmediateAck,
						     viopath_sourceinst
						     (viopath_hostLp),
						     viopath_targetinst
						     (viopath_hostLp),
						     (u64) (unsigned long)
						     req,
						     VIOVERSION << 16,
						     ((u64) device_no <<
						      48), start,
						     ((u64) sg[0].
						      dma_address) << 32,
						     sg[0].dma_length);
	} else {
		bevent =
		    (struct vioblocklpevent *)
		    vio_get_event_buffer(viomajorsubtype_blockio);
		if (bevent == NULL) {
			printk(KERN_WARNING_VIO
			       "error allocating disk event buffer\n");
			/* Free up DMA resources */
			pci_unmap_sg(iSeries_vio_dev, sg, nsg, direction);
			return -1;
		}

		/* Now build up the actual request.  Note that we store the pointer      */
		/* to the request buffer in the correlation token so we can match        */
		/* this response up later                                                */
		memset(bevent, 0x00, sizeof(struct vioblocklpevent));
		bevent->event.xFlags.xValid = 1;
		bevent->event.xFlags.xFunction = HvLpEvent_Function_Int;
		bevent->event.xFlags.xAckInd = HvLpEvent_AckInd_DoAck;
		bevent->event.xFlags.xAckType =
		    HvLpEvent_AckType_ImmediateAck;
		bevent->event.xType = HvLpEvent_Type_VirtualIo;
		bevent->event.xSubtype = viocmd;
		bevent->event.xSourceLp = HvLpConfig_getLpIndex();
		bevent->event.xTargetLp = viopath_hostLp;
		bevent->event.xSizeMinus1 =
		    offsetof(struct vioblocklpevent,
			     u.rwData.dmaInfo) +
		    (sizeof(bevent->u.rwData.dmaInfo[0]) * (sgindex)) - 1;
		bevent->event.xSourceInstanceId =
		    viopath_sourceinst(viopath_hostLp);
		bevent->event.xTargetInstanceId =
		    viopath_targetinst(viopath_hostLp);
		bevent->event.xCorrelationToken =
		    (u64) (unsigned long) req;
		bevent->mVersion = VIOVERSION;
		bevent->mDisk = device_no;
		bevent->u.rwData.mOffset = start;

		/* Copy just the dma information from the sg list into the request */
		for (sgindex = 0; sgindex < nsg; sgindex++) {
			bevent->u.rwData.dmaInfo[sgindex].mToken =
			    sg[sgindex].dma_address;
			bevent->u.rwData.dmaInfo[sgindex].mLen =
			    sg[sgindex].dma_length;
		}

		/* Send the request                                               */
		hvrc = HvCallEvent_signalLpEvent(&bevent->event);
		vio_free_event_buffer(viomajorsubtype_blockio, bevent);
	}

	if (hvrc != HvLpEvent_Rc_Good) {
		printk(KERN_WARNING_VIO
		       "error sending disk event to OS/400 (rc %d)\n",
		       (int) hvrc);
		/* Free up DMA resources */
		pci_unmap_sg(iSeries_vio_dev, sg, nsg, direction);
		return -1;
	} else {
		/* If the request was successful, bump the number of outstanding */
		num_req_outstanding++;
	}
	return 0;
}

/* This is the external request processing routine
 */
static void do_viodasd_request(request_queue_t * q)
{
	int device_no;
	for (;;) {
		struct request *req;
		struct gendisk *gendisk;

		/* inlined INIT_REQUEST here because we don't define MAJOR_NR before blk.h */
		if (list_empty(&q->queue_head))
			return;
		req = blkdev_entry_next_request(&q->queue_head);
		if (major_to_index(MAJOR(req->rq_dev)) < 0)
			panic(VIOD_DEVICE_NAME ": request list destroyed");
		if (req->bh) {
			if (!buffer_locked(req->bh))
				panic(VIOD_DEVICE_NAME
				      ": block not locked");
		}

		gendisk = major_to_gendisk(MAJOR(req->rq_dev));

		device_no = DEVICE_NR(req->rq_dev);
		if (device_no > MAX_DISKNO || device_no < 0) {
			printk(KERN_WARNING_VIO "Invalid device # %d\n", device_no);
			viodasd_end_request_with_error(req);
			continue;
		}
		
		if (gendisk->sizes == NULL) {
			printk(KERN_WARNING_VIO "Ouch! gendisk->sizes is NULL\n");
			viodasd_end_request_with_error(req);
			continue;
		}

		/* If the queue is plugged, don't dequeue anything right now */
		if ((q) && (q->plugged)) {
			return;
		}

		/* If we already have the maximum number of requests outstanding to OS/400
		   just bail out. We'll come back later                              */
		if (num_req_outstanding >= VIOMAXREQ) {
			return;
		}

		/* Try sending the request                                           */
		if (send_request(req) == 0) {
			// it worked--transfer it to our internal queue
			blkdev_dequeue_request(req);
			list_add_tail(&req->queue, &reqlist);
		} else {
			// strip off one bh and try again
			viodasd_end_request(req, 0);
		}
	}
}

/* Check for changed disks
 */
static int viodasd_check_change(kdev_t dev)
{
	struct viodasd_waitevent we;
	HvLpEvent_Rc hvrc;
	int device_no = DEVICE_NR(dev);

	/* This semaphore is raised in the interrupt handler                     */
	DECLARE_MUTEX_LOCKED(Semaphore);

	/* Check that we are dealing with a valid hosting partition              */
	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "Invalid hosting partition\n");
		return -EIO;
	}

	we.sem = &Semaphore;

	/* Send the open event to OS/400                                         */
	hvrc = HvCallEvent_signalLpEventFast(viopath_hostLp,
					     HvLpEvent_Type_VirtualIo,
					     viomajorsubtype_blockio |
					     vioblockcheck,
					     HvLpEvent_AckInd_DoAck,
					     HvLpEvent_AckType_ImmediateAck,
					     viopath_sourceinst
					     (viopath_hostLp),
					     viopath_targetinst
					     (viopath_hostLp),
					     (u64) (unsigned long) &we,
					     VIOVERSION << 16,
					     ((u64) device_no << 48), 0, 0,
					     0);

	if (hvrc != 0) {
		printk(KERN_WARNING_VIO "bad rc on signalLpEvent %d\n",
		       (int) hvrc);
		return -EIO;
	}

	/* Wait for the interrupt handler to get the response                    */
	down(&Semaphore);

	/* Check the return code.  If bad, assume no change                      */
	if (we.rc != 0) {
		printk(KERN_WARNING_VIO
		       "bad rc %d on check_change. Assuming no change\n",
		       (int) we.rc);
		return 0;
	}

	return we.data.changed;
}

/* Our file operations table
 */
static struct block_device_operations viodasd_fops = {
	owner:THIS_MODULE,
	open:viodasd_open,
	release:viodasd_release,
	ioctl:viodasd_ioctl,
	check_media_change:viodasd_check_change,
	revalidate:viodasd_revalidate
};

/* returns the total number of scatterlist elements converted */
static int block_event_to_scatterlist(const struct vioblocklpevent *bevent,
				      struct scatterlist *sg,
				      int *total_len)
{
	int i, numsg;
	const struct rwData *rwData = &bevent->u.rwData;
	static const int offset =
	    offsetof(struct vioblocklpevent, u.rwData.dmaInfo);
	static const int element_size = sizeof(rwData->dmaInfo[0]);

	numsg = ((bevent->event.xSizeMinus1 + 1) - offset) / element_size;
	if (numsg > VIOMAXBLOCKDMA)
		panic("[viodasd] I/O completion too large "
		      "(numsg %d, bevent %p)\n", numsg, bevent);

	*total_len = 0;
	memset(sg, 0x00, sizeof(sg[0]) * VIOMAXBLOCKDMA);

	for (i = 0; (i < numsg) && (rwData->dmaInfo[i].mLen > 0); ++i) {
		sg[i].dma_address = rwData->dmaInfo[i].mToken;
		sg[i].dma_length = rwData->dmaInfo[i].mLen;
		*total_len += rwData->dmaInfo[i].mLen;
	}
	return i;
}

static struct request *find_request_with_token(u64 token)
{
	struct request *req = blkdev_entry_to_request(reqlist.next);
	while ((&req->queue != &reqlist) &&
	       ((u64) (unsigned long) req != token))
		req = blkdev_entry_to_request(req->queue.next);
	if (&req->queue == &reqlist) {
		return NULL;
	}
	return req;
}

/* Restart all queues, starting with the one _after_ the major given, */
/* thus reducing the chance of starvation of disks with late majors. */
static void viodasd_restart_all_queues_starting_from(int first_major)
{
	int i, first_index = major_to_index(first_major);
	for(i = first_index + 1; i < NUM_MAJORS; ++i)
		do_viodasd_request(BLK_DEFAULT_QUEUE(major_table[i]));
	for(i = 0; i <= first_index; ++i)
		do_viodasd_request(BLK_DEFAULT_QUEUE(major_table[i]));
}

/* For read and write requests, decrement the number of outstanding requests,
 * Free the DMA buffers we allocated, and find the matching request by
 * using the buffer pointer we stored in the correlation token.
 */
static int viodasd_handleReadWrite(struct vioblocklpevent *bevent)
{
	int num_sg, num_sect, pci_direction, total_len, major;
	struct request *req;
	struct scatterlist sg[VIOMAXBLOCKDMA];
	struct HvLpEvent *event = &bevent->event;
	unsigned long irq_flags;

	num_sg = block_event_to_scatterlist(bevent, sg, &total_len);
	num_sect = total_len >> 9;
	if (event->xSubtype == (viomajorsubtype_blockio | vioblockread))
		pci_direction = PCI_DMA_FROMDEVICE;
	else
		pci_direction = PCI_DMA_TODEVICE;
	pci_unmap_sg(iSeries_vio_dev, sg, num_sg, pci_direction);


	/* Since this is running in interrupt mode, we need to make sure we're not
	 * stepping on any global I/O operations
	 */
	spin_lock_irqsave(&io_request_lock, irq_flags);

	num_req_outstanding--;

	/* Now find the matching request in OUR list (remember we moved the request
	 * from the global list to our list when we got it)
	 */
	req = find_request_with_token(bevent->event.xCorrelationToken);
	if (req == NULL) {
		printk(KERN_WARNING_VIO
		       "[viodasd] No request found matching token 0x%lx\n",
		       bevent->event.xCorrelationToken);
		spin_unlock_irqrestore(&io_request_lock, irq_flags);
		return -1;
	}

	/* Record this event's major number so we can check that queue again */
	major = MAJOR(req->rq_dev);

	if (!req->bh) {
		if (event->xRc != HvLpEvent_Rc_Good) {
			const struct vio_error_entry *err =
			    vio_lookup_rc(viodasd_err_table,
					  bevent->mSubTypeRc);
			printk(KERN_WARNING_VIO
			       "read/write error %d:0x%04x (%s)\n",
			       event->xRc, bevent->mSubTypeRc, err->msg);
			viodasd_end_request(req, 0);
		} else {
			if (num_sect != req->current_nr_sectors) {
				printk(KERN_WARNING_VIO
				       "Yikes...non bh i/o # sect doesn't match!!!\n");
			}
			viodasd_end_request(req, 1);
		}
	} else {
		int success = (event->xRc == HvLpEvent_Rc_Good);
		if (!success) {
			const struct vio_error_entry *err =
			    vio_lookup_rc(viodasd_err_table,
					  bevent->mSubTypeRc);
			printk(KERN_WARNING_VIO
			       "read/write error %d:0x%04x (%s)\n",
			       event->xRc, bevent->mSubTypeRc, err->msg);
		}
		/* record having received the answers we did */
		while ((num_sect > 0) && (req->bh)) {
			num_sect -= req->current_nr_sectors;
			viodasd_end_request(req, success);
		}
		/* if they somehow answered _more_ than we asked for,
		 * data corruption has occurred */
		if (num_sect)
			panic("[viodasd] %d sectors left over on a request "
			       "(bevent %p, starting num_sect %d)\n", 
			       num_sect, bevent, total_len >> 9);

		/* if they didn't answer the whole request this time, re-submit the request */
		if (req->bh) {
			if (send_request(req) != 0) {
				// we'll never know to resubmit this one
				// we _could_ try to put it out on the request queue for the
				// related device...
				viodasd_end_request_with_error(req);
			}
		}
	}

	/* Finally, try to get more requests off of this device's queue */
	viodasd_restart_all_queues_starting_from(major);

	spin_unlock_irqrestore(&io_request_lock, irq_flags);

	return 0;
}

/* This routine handles incoming block LP events */
static void vioHandleBlockEvent(struct HvLpEvent *event)
{
	struct vioblocklpevent *bevent = (struct vioblocklpevent *) event;
	struct viodasd_waitevent *pwe;

	if (event == NULL) {
		/* Notification that a partition went away! */
		return;
	}
	// First, we should NEVER get an int here...only acks
	if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		printk(KERN_WARNING_VIO
		       "Yikes! got an int in viodasd event handler!\n");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}

	switch (event->xSubtype & VIOMINOR_SUBTYPE_MASK) {

		/* Handle a response to an open request.  We get all the disk information
		 * in the response, so update it.  The correlation token contains a pointer to
		 * a waitevent structure that has a semaphore in it.  update the return code
		 * in the waitevent structure and post the semaphore to wake up the guy who
		 * sent the request */
	case vioblockopen:
		pwe =
		    (struct viodasd_waitevent *) (unsigned long) event->
		    xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->data.subRC = bevent->mSubTypeRc;
		if (event->xRc == HvLpEvent_Rc_Good) {
			const struct openData *data = &bevent->u.openData;
			struct viodasd_device *device =
			    &viodasd_devices[bevent->mDisk];
			device->readOnly =
			    bevent->mFlags & vioblockflags_ro;
			device->size = data->mDiskLen;
			device->cylinders = data->mCylinders;
			device->tracks = data->mTracks;
			device->sectors = data->mSectors;
			device->bytesPerSector = data->mBytesPerSector;
			viodasd_max_disk = data->mMaxDisks;
			if (viodasd_max_disk > MAX_DISKNO - 1)
				viodasd_max_disk = MAX_DISKNO - 1;
		}
		up(pwe->sem);
		break;
	case vioblockclose:
		break;
	case vioblockcheck:
		pwe =
		    (struct viodasd_waitevent *) (unsigned long) event->
		    xCorrelationToken;
		pwe->rc = event->xRc;
		pwe->data.changed = bevent->u.check.changed;
		up(pwe->sem);
		break;
	case vioblockflush:
		up((void *) (unsigned long) event->xCorrelationToken);
		break;
	case vioblockread:
	case vioblockwrite:
		viodasd_handleReadWrite(bevent);
		break;

	default:
		printk(KERN_WARNING_VIO "invalid subtype!");
		if (event->xFlags.xAckInd == HvLpEvent_AckInd_DoAck) {
			event->xRc = HvLpEvent_Rc_InvalidSubtype;
			HvCallEvent_ackLpEvent(event);
		}
	}
}

static const char *major_name(int major)
{
	static char major_names[NUM_MAJORS][MAX_MAJOR_NAME];
	int index = major_to_index(major);

	if(index < 0)
		return NULL;
	if(major_names[index][0] == '\0') {
		if(index == 0)
			strcpy(major_names[index], VIOD_GENHD_NAME);
		else
			sprintf(major_names[index], VIOD_GENHD_NAME"%d", index);
	}
	return major_names[index];
}

static const char *device_name(int major)
{
	static char device_names[NUM_MAJORS][MAX_MAJOR_NAME];
	int index = major_to_index(major);

	if(index < 0)
		return NULL;
	if(device_names[index][0] == '\0') {
		strcpy(device_names[index], VIOD_DEVICE_NAME);
	}
	return device_names[index];
}

/* This routine tries to clean up anything we allocated/registered
 */
static void viodasd_cleanup_major(int major)
{
	const int num_partitions = DEV_PER_MAJOR << PARTITION_SHIFT;
	int minor;

	for (minor = 0; minor < num_partitions; minor++)
		fsync_dev(MKDEV(major, minor));

	blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));

	read_ahead[major] = 0;

	kfree(blk_size[major]);
	kfree(blksize_size[major]);
	kfree(hardsect_size[major]);
	kfree(max_sectors[major]);
	kfree(major_to_gendisk(major)->part);

	blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));

	devfs_unregister_blkdev(major, device_name(major));
}

/* in case of bad return code, caller must viodasd_cleanup_major() for this major */
static int viodasd_init_major(int major)
{
	int i;
	const int numpart = DEV_PER_MAJOR << PARTITION_SHIFT;
	int *sizes, *sectsizes, *blksizes, *maxsectors;
	struct hd_struct *partitions;
	struct gendisk *gendisk = major_to_gendisk(major);

	/*
	 * Do the devfs_register.  This works even if devfs is not
	 * configured
	 */
	if (devfs_register_blkdev(major, device_name(major), &viodasd_fops)) {
		printk(KERN_WARNING_VIO
		       "%s: can't register major number %d\n",
		       device_name(major), major);
		return -1;
	}

	blk_init_queue(BLK_DEFAULT_QUEUE(major), do_viodasd_request);
	blk_queue_headactive(BLK_DEFAULT_QUEUE(major), 0);

	read_ahead[major] = 8;	/* 8 sector (4kB) read ahead */

	/* initialize the struct */
	gendisk->major = major;
	gendisk->major_name = major_name(major);
	gendisk->minor_shift = PARTITION_SHIFT;
	gendisk->max_p = 1 << PARTITION_SHIFT;
	gendisk->nr_real = DEV_PER_MAJOR;
	gendisk->fops = &viodasd_fops;

	/* to be assigned later */
	gendisk->next = NULL;
	gendisk->part = NULL;
	gendisk->sizes = NULL;
	gendisk->de_arr = NULL;
	gendisk->flags = NULL;

	/* register us in the global list */
	add_gendisk(gendisk);

	/*
	 * Now fill in all the device driver info     
	 */
	sizes = kmalloc(numpart * sizeof(int), GFP_KERNEL);
	if (!sizes)
		return -ENOMEM;
	memset(sizes, 0x00, numpart * sizeof(int));
	blk_size[major] = gendisk->sizes = sizes;

	partitions =
	    kmalloc(numpart * sizeof(struct hd_struct), GFP_KERNEL);
	if (!partitions)
		return -ENOMEM;
	memset(partitions, 0x00, numpart * sizeof(struct hd_struct));
	gendisk->part = partitions;

	blksizes = kmalloc(numpart * sizeof(int), GFP_KERNEL);
	if (!blksizes)
		return -ENOMEM;
	for (i = 0; i < numpart; i++)
		blksizes[i] = blksize;
	blksize_size[major] = blksizes;

	sectsizes = kmalloc(numpart * sizeof(int), GFP_KERNEL);
	if (!sectsizes)
		return -ENOMEM;
	for (i = 0; i < numpart; i++)
		sectsizes[i] = 0;
	hardsect_size[major] = sectsizes;

	maxsectors = kmalloc(numpart * sizeof(int), GFP_KERNEL);
	if (!maxsectors)
		return -ENOMEM;
	for (i = 0; i < numpart; i++)
		maxsectors[i] = VIODASD_MAXSECTORS;
	max_sectors[major] = maxsectors;

	return 0;
}

static void internal_register_disk(int diskno)
{
	static int registered[MAX_DISKNO] = { 0, };
	int major = diskno_to_major(diskno);
	int dev_within_major = diskno % DEV_PER_MAJOR;
	struct gendisk *gendisk = major_to_gendisk(major);
	int i;

	if(registered[diskno])
		return;
	registered[diskno] = 1;

	if (diskno == 0) {
		printk(KERN_INFO_VIO
		       "%s: Currently %d disks connected\n",
		       VIOD_DEVICE_NAME, (int) viodasd_max_disk + 1);
		if (viodasd_max_disk > MAX_DISKNO - 1)
			printk(KERN_INFO_VIO
			       "Only examining the first %d\n",
			       MAX_DISKNO);
	}

	register_disk(gendisk,
		      MKDEV(major,
			    dev_within_major <<
			    PARTITION_SHIFT),
		      1 << PARTITION_SHIFT, &viodasd_fops,
		      gendisk->
		      part[dev_within_major << PARTITION_SHIFT].nr_sects);

	printk(KERN_INFO_VIO
	       "%s: Disk %2.2d size %dM, sectors %d, heads %d, cylinders %d, sectsize %d\n",
	       VIOD_DEVICE_NAME,
	       diskno,
	       (int) (viodasd_devices[diskno].size /
		      (1024 * 1024)),
	       (int) viodasd_devices[diskno].sectors,
	       (int) viodasd_devices[diskno].tracks,
	       (int) viodasd_devices[diskno].cylinders,
	       (int) hardsect_size[major][dev_within_major <<
					  PARTITION_SHIFT]);

	for (i = 1; i < (1 << PARTITION_SHIFT); ++i) {
		int minor = (dev_within_major << PARTITION_SHIFT) + i;
		struct hd_struct *partition = &gendisk->part[minor];
		if (partition->nr_sects)
			printk(KERN_INFO_VIO
			       "%s: Disk %2.2d partition %2.2d start sector %ld, # sector %ld\n",
			       VIOD_DEVICE_NAME, diskno, i,
			       partition->start_sect, partition->nr_sects);
	}
}

/* Initialize the whole device driver.  Handle module and non-module
 * versions
 */
int __init viodasd_init(void)
{
	int i, j;
	int rc;

	/* Try to open to our host lp
	 */
	if (viopath_hostLp == HvLpIndexInvalid) {
		vio_set_hostlp();
	}

	if (viopath_hostLp == HvLpIndexInvalid) {
		printk(KERN_WARNING_VIO "%s: invalid hosting partition\n",
		       VIOD_DEVICE_NAME);
		rc = -EIO;
		goto no_hosting_partition;
	}

	printk(KERN_INFO_VIO
	       "%s: Disk vers %s, major %d, max disks %d, hosting partition %d\n",
	       VIOD_DEVICE_NAME, VIODASD_VERS, major_table[0], MAX_DISKNO,
	       viopath_hostLp);

	if (ROOT_DEV == NODEV) {
		/* first disk, first partition */
		ROOT_DEV = diskno_to_devt(0, 3);

		printk(KERN_INFO_VIO
		       "Claiming root file system as first partition of first virtual disk");
	}

	/* Actually open the path to the hosting partition           */
	rc = viopath_open(viopath_hostLp, viomajorsubtype_blockio,
			  VIOMAXREQ + 2);
	if (rc) {
		printk(KERN_WARNING_VIO
		       "error opening path to host partition %d\n",
		       viopath_hostLp);
		goto viopath_open_failed;
	} else {
		printk(KERN_INFO_VIO "%s: opened path to hosting partition %d\n",
		       VIOD_DEVICE_NAME, viopath_hostLp);
	}

	viodasd_devices =
	    kmalloc(MAX_DISKNO * sizeof(struct viodasd_device),
		    GFP_KERNEL);
	if (!viodasd_devices)
	{
		printk(KERN_WARNING_VIO "couldn't allocate viodasd_devices\n");
		rc = -ENOMEM;
		goto viodasd_devices_failed;
	}
	memset(viodasd_devices, 0x00,
	       MAX_DISKNO * sizeof(struct viodasd_device));

	/*
	 * Initialize our request handler
	 */
	vio_setHandler(viomajorsubtype_blockio, vioHandleBlockEvent);

	for (i = 0; i < NUM_MAJORS; ++i) {
		int init_rc = viodasd_init_major(major_table[i]);
		if (init_rc < 0) {
			for (j = 0; j <= i; ++j)
				viodasd_cleanup_major(major_table[j]);
			return init_rc;
		}
	}

	viodasd_max_disk = MAX_DISKNO - 1;
	for (i = 0; i <= viodasd_max_disk && i < MAX_DISKNO; i++) {
		// Note that internal_open has side effects:
		//  a) it updates the size of the disk
		//  b) it updates viodasd_max_disk
		//  c) it registers the disk if it has not done so already
		if (internal_open(i, vioblockflags_ro) == 0)
			internal_release(i, vioblockflags_ro);
	}

	/* 
	 * Create the proc entry
	 */
	iSeries_proc_callback(&viodasd_proc_init);

	return 0;
viodasd_devices_failed:
	viopath_close(viopath_hostLp, viomajorsubtype_blockio, VIOMAXREQ + 2);
viopath_open_failed:
no_hosting_partition:
	return rc;
}

void __exit viodasd_exit(void)
{
	int i;
	for(i = 0; i < NUM_MAJORS; ++i)
		viodasd_cleanup_major(major_table[i]);

	kfree(viodasd_devices);

	viopath_close(viopath_hostLp, viomajorsubtype_blockio, VIOMAXREQ + 2);
	iSeries_proc_callback(&viodasd_proc_delete);

}

module_init(viodasd_init);
module_exit(viodasd_exit);
