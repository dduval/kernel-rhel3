/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *  devfs support - jj, rgooch, 980122
 *
 *  Moved partition checking code to fs/partitions* - Russell King
 *  (linux@arm.uk.linux.org)
 */

/*
 * TODO:  rip out the remaining init crap from this file  --hch
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>


/*
 * Global kernel list of partitioning information.
 *
 *	you should _never_ access this directly but use the access functions
 */
static struct gendisk *gendisk_head;
static struct gendisk *gendisk_array[MAX_BLKDEV];


/* Global kernel list of callback functions for formatting
 * device and partition names correctly for registered drivers
 * in /proc/partitions
 */
devname_t callback_devname_table[MAX_BLKDEV];
static spinlock_t devname_lock = SPIN_LOCK_UNLOCKED;


/**
 * register_callback_devname_table - Used by drivers to specify the
 * callback functions to be called in the partition check path to
 * get the right names for the driver controlled devices in /proc/partitions
 *
 * This will be invoked during the initialization of such drivers.
 *
 * Returns: 0 --> registration of callback function SUCCESS
            < 0--> registration of callback function FAILURE
 */
int
register_callback_devname_table(int major_num, void *function)
{
	if (major_num > MAX_BLKDEV)
		return -EINVAL;
	spin_lock(&devname_lock);
	callback_devname_table[major_num] = function;
	spin_unlock(&devname_lock);
	return 0;
}

EXPORT_SYMBOL(register_callback_devname_table);

/**
 * unregister_callback_devname_table - Will be used by the drivers which
 * had registered their callback functions to format the device names
 * during driver init to do the corresponding unregistrations.
 *
 * This will be called during the driver exit paths.
 */
int
unregister_callback_devname_table(int major_num)
{
	if (major_num > MAX_BLKDEV)
		return -EINVAL;
	spin_lock(&devname_lock);
	callback_devname_table[major_num] = NULL;
	spin_unlock(&devname_lock);
	return 0;
}

EXPORT_SYMBOL(unregister_callback_devname_table);

/**
 * get_callback_function_for_disk_name  - Returns the callback function
 * which is previously registered for that particular driver (major).
 */
void *
get_callback_from_devname_table(int major_num)
{
	return (void *)callback_devname_table[major_num];
}

EXPORT_SYMBOL(get_callback_from_devname_table);

/**
 * add_gendisk - add partitioning information to kernel list
 * @gp: per-device partitioning information
 *
 * This function registers the partitioning information in @gp
 * with the kernel.
 */
void
add_gendisk(struct gendisk *gp)
{
	struct gendisk *sgp;
	unsigned long flags;

	br_write_lock_irqsave(BR_GENHD_LOCK, flags);

	/*
 	 *	In 2.5 this will go away. Fix the drivers who rely on
 	 *	old behaviour.
 	 */

	for (sgp = gendisk_head; sgp; sgp = sgp->next)
	{
		if (sgp == gp)
		{
//			printk(KERN_ERR "add_gendisk: device major %d is buggy and added a live gendisk!\n",
//				sgp->major)
			goto out;
		}
	}
	gendisk_array[gp->major] = gp;
	gp->next = gendisk_head;
	gendisk_head = gp;
out:
	br_write_unlock_irqrestore(BR_GENHD_LOCK, flags);
}

EXPORT_SYMBOL(add_gendisk);


/**
 * del_gendisk - remove partitioning information from kernel list
 * @gp: per-device partitioning information
 *
 * This function unregisters the partitioning information in @gp
 * with the kernel.
 */
void
del_gendisk(struct gendisk *gp)
{
	struct gendisk **gpp;
	unsigned long flags;

	br_write_lock_irqsave(BR_GENHD_LOCK, flags);
	gendisk_array[gp->major] = NULL;
	for (gpp = &gendisk_head; *gpp; gpp = &((*gpp)->next))
		if (*gpp == gp)
			break;
	if (*gpp)
		*gpp = (*gpp)->next;
	br_write_unlock_irqrestore(BR_GENHD_LOCK, flags);
}

EXPORT_SYMBOL(del_gendisk);


/**
 * get_gendisk - get partitioning information for a given device
 * @dev: device to get partitioning information for
 *
 * This function gets the structure containing partitioning
 * information for the given device @dev.
 */
struct gendisk *
get_gendisk(kdev_t dev)
{
	struct gendisk *gp = NULL;
	int maj = MAJOR(dev);

	br_read_lock(BR_GENHD_LOCK);
	if (likely((gp = gendisk_array[maj]) != NULL))
		goto out;

	/* This is needed for early 2.4 source compatiblity.  --hch */
	for (gp = gendisk_head; gp; gp = gp->next)
		if (gp->major == maj)
			break;
out:
	br_read_unlock(BR_GENHD_LOCK);
	return gp;
}

EXPORT_SYMBOL(get_gendisk);


/**
 * walk_gendisk - issue a command for every registered gendisk
 * @walk: user-specified callback
 * @data: opaque data for the callback
 *
 * This function walks through the gendisk chain and calls back
 * into @walk for every element.
 */
int
walk_gendisk(int (*walk)(struct gendisk *, void *), void *data)
{
	struct gendisk *gp;
	int error = 0;

	br_read_lock(BR_GENHD_LOCK);
	for (gp = gendisk_head; gp; gp = gp->next)
		if ((error = walk(gp, data)))
			break;
	br_read_unlock(BR_GENHD_LOCK);

	return error;
}

#ifdef CONFIG_PROC_FS
/* iterator */
static void *part_start(struct seq_file *s, loff_t *ppos)
{
	struct gendisk *gp;
	loff_t pos = *ppos;

	br_read_lock(BR_GENHD_LOCK);
	for (gp = gendisk_head; gp; gp = gp->next)
		if (!pos--)
			return gp;
	return NULL;
}

static void *part_next(struct seq_file *s, void *v, loff_t *pos)
{
	++*pos;
	return ((struct gendisk *)v)->next;
}

static void part_stop(struct seq_file *s, void *v)
{
	br_read_unlock(BR_GENHD_LOCK);
}

static int part_show(struct seq_file *s, void *v)
{
	struct gendisk *gp = v;
	char buf[64];
	int n;

	if (gp == gendisk_head) {
		seq_puts(s, "major minor  #blocks  name"
#ifdef CONFIG_BLK_STATS
			    "     rio rmerge rsect ruse wio wmerge "
			    "wsect wuse running use aveq"
#endif
			   "\n\n");
	}

	/* show the full disk and all non-0 size partitions of it */
	for (n = 0; n < (gp->nr_real << gp->minor_shift); n++) {
		if (gp->part[n].nr_sects) {
#ifdef CONFIG_BLK_STATS
			struct hd_struct *hd = &gp->part[n];

			disk_round_stats(hd);
			seq_printf(s, "%4d  %4d %10d %s "
				      "%u %u %u %u %u %u %u %u %u %u %u\n",
				      gp->major, n, gp->sizes[n],
				      disk_name(gp, n, buf),
				      hd->rd_ios, hd->rd_merges,
#define MSEC(x) ((x) * 1000 / HZ)
				      hd->rd_sectors, MSEC(hd->rd_ticks),
				      hd->wr_ios, hd->wr_merges,
				      hd->wr_sectors, MSEC(hd->wr_ticks),
				      hd->ios_in_flight, MSEC(hd->io_ticks),
				      MSEC(hd->aveq));
#else
			seq_printf(s, "%4d  %4d %10d %s\n",
				   gp->major, n, gp->sizes[n],
				   disk_name(gp, n, buf));
#endif /* CONFIG_BLK_STATS */
		}
	}

	return 0;
}

struct seq_operations partitions_op = {
	.start		= part_start,
	.next		= part_next,
	.stop		= part_stop,
	.show		= part_show,
};
#endif

extern int blk_dev_init(void);
extern int net_dev_init(void);
extern void console_map_init(void);
extern int atmdev_init(void);

int __init device_init(void)
{
	blk_dev_init();
	sti();
#ifdef CONFIG_NET
	net_dev_init();
#endif
#ifdef CONFIG_ATM
	(void) atmdev_init();
#endif
#ifdef CONFIG_VT
	console_map_init();
#endif
	return 0;
}

__initcall(device_init);
