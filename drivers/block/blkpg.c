/*
 * Partition table and disk geometry handling
 *
 * This obsoletes the partition-handling code in genhd.c:
 * Userspace can look at a disk in arbitrary format and tell
 * the kernel what partitions there are on the disk, and how
 * these should be numbered.
 * It also allows one to repartition a disk that is being used.
 *
 * A single ioctl with lots of subfunctions:
 *
 * Device number stuff:
 *    get_whole_disk()          (given the device number of a partition, find
 *                               the device number of the encompassing disk)
 *    get_all_partitions()      (given the device number of a disk, return the
 *                               device numbers of all its known partitions)
 *
 * Partition stuff:
 *    add_partition()
 *    delete_partition()
 *    test_partition_in_use()   (also for test_disk_in_use)
 *
 * Geometry stuff:
 *    get_geometry()
 *    set_geometry()
 *    get_bios_drivedata()
 *
 * For today, only the partition stuff - aeb, 990515
 */

#include <linux/errno.h>
#include <linux/fs.h>			/* for BLKRASET, ... */
#include <linux/sched.h>		/* for capable() */
#include <linux/blk.h>			/* for set_device_ro() */
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/genhd.h>
#include <linux/swap.h>			/* for is_swap_partition() */
#include <linux/module.h>               /* for EXPORT_SYMBOL */

#include <asm/uaccess.h>

/*
 *  Dump  stuff.
 */
void (*diskdump_func)(struct pt_regs *regs, void *platform_arg) = NULL;

int diskdump_register_hook(void (*dump_func)(struct pt_regs *, void *))
{
	if (diskdump_func)
		return -EEXIST;

	diskdump_func = dump_func;

	return 0;
}

void diskdump_unregister_hook(void)
{
	diskdump_func = NULL;
}

#if defined(CONFIG_IA64) 
static int set_last_sector( kdev_t dev, const void *param );
static int get_last_sector( kdev_t dev, const void *param );
#endif

/*
 * What is the data describing a partition?
 *
 * 1. a device number (kdev_t)
 * 2. a starting sector and number of sectors (hd_struct)
 *    given in the part[] array of the gendisk structure for the drive.
 *
 * The number of sectors is replicated in the sizes[] array of
 * the gendisk structure for the major, which again is copied to
 * the blk_size[][] array.
 * (However, hd_struct has the number of 512-byte sectors,
 *  g->sizes[] and blk_size[][] have the number of 1024-byte blocks.)
 * Note that several drives may have the same major.
 */

/*
 * Add a partition.
 *
 * returns: EINVAL: bad parameters
 *          ENXIO: cannot find drive
 *          EBUSY: proposed partition overlaps an existing one
 *                 or has the same number as an existing one
 *          0: all OK.
 */
int add_partition(kdev_t dev, struct blkpg_partition *p) {
	struct gendisk *g;
	long long ppstart, pplength;
	unsigned long pstart, plength;
	int i, drive, first_minor, end_minor, minor;

	/* convert bytes to sectors, check for fit in a hd_struct */
	ppstart = (p->start >> 9);
	pplength = (p->length >> 9);
	pstart = ppstart;
	plength = pplength;
	if (pstart != ppstart || plength != pplength
	    || pstart < 0 || plength < 0)
		return -EINVAL;

	/* find the drive major */
	g = get_gendisk(dev);
	if (!g)
		return -ENXIO;

	/* existing drive? */
	drive = (MINOR(dev) >> g->minor_shift);
	first_minor = (drive << g->minor_shift);
	end_minor   = first_minor + g->max_p;
	if (drive >= g->nr_real)
		return -ENXIO;

	/* drive and partition number OK? */
	if (first_minor != MINOR(dev) || p->pno <= 0 || p->pno >= g->max_p)
		return -EINVAL;

	/* partition number in use? */
	minor = first_minor + p->pno;
	if (g->part[minor].nr_sects != 0)
		return -EBUSY;

	/* overlap? */
	for (i=first_minor+1; i<end_minor; i++)
		if (!(pstart+plength <= g->part[i].start_sect ||
		      pstart >= g->part[i].start_sect + g->part[i].nr_sects))
			return -EBUSY;

	/* all seems OK */
	g->part[minor].start_sect = pstart;
	g->part[minor].nr_sects = plength;
	if (g->sizes)
		g->sizes[minor] = (plength >> (BLOCK_SIZE_BITS - 9));
	devfs_register_partitions (g, first_minor, 0);
	return 0;
}

/*
 * Delete a partition given by partition number
 *
 * returns: EINVAL: bad parameters
 *          ENXIO: cannot find partition
 *          EBUSY: partition is busy
 *          0: all OK.
 *
 * Note that the dev argument refers to the entire disk, not the partition.
 */
int del_partition(kdev_t dev, struct blkpg_partition *p) {
	struct gendisk *g;
	kdev_t devp;
	int drive, first_minor, minor;

	/* find the drive major */
	g = get_gendisk(dev);
	if (!g)
		return -ENXIO;

	/* drive and partition number OK? */
	drive = (MINOR(dev) >> g->minor_shift);
	first_minor = (drive << g->minor_shift);
	if (first_minor != MINOR(dev) || p->pno <= 0 || p->pno >= g->max_p)
		return -EINVAL;

	/* existing drive and partition? */
	minor = first_minor + p->pno;
	if (drive >= g->nr_real || g->part[minor].nr_sects == 0)
		return -ENXIO;

	/* partition in use? Incomplete check for now. */
	devp = MKDEV(MAJOR(dev), minor);
	if (is_mounted(devp) || is_swap_partition(devp))
		return -EBUSY;

	/* all seems OK */
	fsync_dev(devp);
	invalidate_buffers(devp);

	g->part[minor].start_sect = 0;
	g->part[minor].nr_sects = 0;
	if (g->sizes)
		g->sizes[minor] = 0;
	devfs_register_partitions (g, first_minor, 0);

	return 0;
}

int blkpg_ioctl(kdev_t dev, struct blkpg_ioctl_arg *arg)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	int len;

	if (copy_from_user(&a, arg, sizeof(struct blkpg_ioctl_arg)))
		return -EFAULT;

	switch (a.op) {
		case BLKPG_ADD_PARTITION:
		case BLKPG_DEL_PARTITION:
			len = a.datalen;
			if (len < sizeof(struct blkpg_partition))
				return -EINVAL;
			if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
				return -EFAULT;
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (a.op == BLKPG_ADD_PARTITION)
				return add_partition(dev, &p);
			else
				return del_partition(dev, &p);
		default:
			return -EINVAL;
	}
}

/*
 * Common ioctl's for block devices
 */

int blk_ioctl(kdev_t dev, unsigned int cmd, unsigned long arg)
{
	struct gendisk *g;
	u64 ullval = 0;
	int intval;

	if (!dev)
		return -EINVAL;

	switch (cmd) {
#if defined(CONFIG_IA64)
		case BLKGETLASTSECT:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return get_last_sector(dev, (char *)(arg));

		case BLKSETLASTSECT:
			if( is_read_only(dev) )
				return -EACCES;
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return set_last_sector(dev, (char *)(arg));
#endif
		case BLKROSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if (get_user(intval, (int *)(arg)))
				return -EFAULT;
			set_device_ro(dev, intval);
			return 0;
		case BLKROGET:
			intval = (is_read_only(dev) != 0);
			return put_user(intval, (int *)(arg));

		case BLKRASET:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			if(arg > 0xff)
				return -EINVAL;
			read_ahead[MAJOR(dev)] = arg;
			return 0;
		case BLKRAGET:
			if (!arg)
				return -EINVAL;
			return put_user(read_ahead[MAJOR(dev)], (long *) arg);

		case BLKFLSBUF:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			fsync_dev(dev);
			invalidate_buffers(dev);
			return 0;

		case BLKSSZGET:
			/* get block device sector size as needed e.g. by fdisk */
			intval = get_hardsect_size(dev);
			return put_user(intval, (int *) arg);

		case BLKGETSIZE:
		case BLKGETSIZE64:
			g = get_gendisk(dev);
			if (g)
				ullval = g->part[MINOR(dev)].nr_sects;

			if (cmd == BLKGETSIZE)
				return put_user((unsigned long)ullval, (unsigned long *)arg);
			else
				return put_user(ullval << 9, (u64 *)arg);
#if 0
		case BLKRRPART: /* Re-read partition tables */
			if (!capable(CAP_SYS_ADMIN)) 
				return -EACCES;
			return reread_partitions(dev, 1);
#endif

		case BLKPG:
			return blkpg_ioctl(dev, (struct blkpg_ioctl_arg *) arg);
			
		case BLKELVGET:
			return blkelvget_ioctl(&blk_get_queue(dev)->elevator,
					       (blkelv_ioctl_arg_t *) arg);
		case BLKELVSET:
			return blkelvset_ioctl(&blk_get_queue(dev)->elevator,
					       (blkelv_ioctl_arg_t *) arg);

		case BLKBSZGET:
			/* get the logical block size (cf. BLKSSZGET) */
			intval = BLOCK_SIZE;
			if (blksize_size[MAJOR(dev)])
				intval = blksize_size[MAJOR(dev)][MINOR(dev)];
			return put_user (intval, (int *) arg);

		case BLKBSZSET:
			/* set the logical block size */
			if (!capable (CAP_SYS_ADMIN))
				return -EACCES;
			if (!dev || !arg)
				return -EINVAL;
			if (get_user (intval, (int *) arg))
				return -EFAULT;
			if (intval > PAGE_SIZE || intval < 512 ||
			    (intval & (intval - 1)))
				return -EINVAL;
			if (is_mounted (dev) || is_swap_partition (dev))
				return -EBUSY;
			set_blocksize (dev, intval);
			return 0;

		default:
			return -EINVAL;
	}
}

EXPORT_SYMBOL(blk_ioctl);

#if defined(CONFIG_IA64)

 /*********************
  * get_last_sector()
  *  
  * Description: This function will read any inaccessible blocks at the end
  * 	of a device
  * Why: Normal read/write calls through the block layer will not read the 
  *      last sector of an odd-size disk. 
  * parameters: 
  *    dev: kdev_t -- which device to read
  *    param: a pointer to a userspace struct. The struct has these members: 
  *	block:  an int which denotes which block to return:
  *		0 == Last block
  * 		1 == Last block - 1
  * 		n == Last block - n
  *		This is validated so that only values of 
  *		  <= ((total_sects + 1) % logical_block_size)  ||  0
  *		  are allowed.
  * 	block_contents: a pointer to userspace char*, this is where we write 
  *	 returned blocks to.
  * 	content_length: How big the userspace buffer is.
  * return: 
  *    0 on success
  *   -ERRVAL on error.
  *********************/
int get_last_sector( kdev_t dev, const void *param )
{   
        struct buffer_head *bh;
        struct gendisk *g;
        int rc = 0;
        unsigned int lastlba, readlba;
        int orig_blksize = BLOCK_SIZE;
        int hardblocksize;

	struct {
		unsigned int block;
		size_t content_length;
		char *block_contents;
	} blk_ioctl_parameter;

        if( !dev ) return -EINVAL;

        if(copy_from_user(&blk_ioctl_parameter, param, sizeof(blk_ioctl_parameter)))
		return -EFAULT;

        g = get_gendisk( dev );

        if( !g ) return -EINVAL;

        lastlba = g->part[MINOR(dev)].nr_sects;

        if( !lastlba ) return -EINVAL;

        hardblocksize = get_hardsect_size(dev);
        if( ! hardblocksize ) hardblocksize = 512;

         /* Need to change the block size that the block layer uses */
        if (blksize_size[MAJOR(dev)]){
                orig_blksize = blksize_size[MAJOR(dev)][MINOR(dev)];
        }

         /* validate userspace input */
        if( blk_ioctl_parameter.block == 0 )
		goto good_params;

	/* so we don't divide by zero below */  
	if(orig_blksize == 0) 
		return -EINVAL; 

        if( blk_ioctl_parameter.block <= (lastlba % (orig_blksize / hardblocksize)))
		goto good_params;

	return -EINVAL; 

good_params:
        readlba = lastlba - blk_ioctl_parameter.block - 1;

        if (orig_blksize != hardblocksize)
                   set_blocksize(dev, hardblocksize);

        bh =  bread(dev, readlba, hardblocksize);
        if (!bh) {
		/* We hit the end of the disk */
		printk(KERN_WARNING
			"get_last_sector ioctl: bread returned NULL.\n");
		rc = -EIO;
		goto out;
        }

	if (copy_to_user(blk_ioctl_parameter.block_contents, bh->b_data, 
		(bh->b_size > blk_ioctl_parameter.content_length) ? 
		blk_ioctl_parameter.content_length : bh->b_size))
		rc = -EFAULT;

out:
        brelse(bh);

        /* change block size back */
        if (orig_blksize != hardblocksize)
                   set_blocksize(dev, orig_blksize);
   
        return rc;
}

 /*********************
  * set_last_sector()
  *  
  * Description: This function will write to any inaccessible blocks at the end
  * 	of a device
  * Why: Normal read/write calls through the block layer will not read the 
  *      last sector of an odd-size disk. 
  * parameters: 
  *    dev: kdev_t -- which device to read
  *    sect: a pointer to a userspace struct. The struct has these members: 
  *	block:  an int which denotes which block to return:
  *		0 == Last block
  * 		1 == Last block - 1
  * 		n == Last block - n
  *		This is validated so that only values of 
  *		  <= ((total_sects + 1) % logical_block_size)  ||  0
  *		  are allowed.
  * 	block_contents: a pointer to userspace char*, this is where we write 
  *	 returned blocks to.
  * 	content_length: How big the userspace buffer is.
  * return: 
  *    0 on success
  *   -ERRVAL on error.
  *********************/
int set_last_sector( kdev_t dev, const void *param ) 
{
        struct buffer_head *bh;
        struct gendisk *g;
        int rc = 0;
        unsigned int lastlba, writelba;
        int orig_blksize = BLOCK_SIZE;
        int hardblocksize;

	struct {
		unsigned int block;
		size_t content_length;
		char *block_contents;
	} blk_ioctl_parameter;

        if( !dev ) return -EINVAL;

	if(copy_from_user(&blk_ioctl_parameter, param, sizeof(blk_ioctl_parameter)))
		return -EFAULT;

        g = get_gendisk( dev );

        if( !g ) return -EINVAL;
    
        lastlba = g->part[MINOR(dev)].nr_sects ;
    
        if( !lastlba ) return -EINVAL;
    
        hardblocksize = get_hardsect_size(dev);
        if( ! hardblocksize ) hardblocksize = 512;
    
         /* Need to change the block size that the block layer uses */
        if (blksize_size[MAJOR(dev)]){
                orig_blksize = blksize_size[MAJOR(dev)][MINOR(dev)];
        }

         /* validate userspace input */
        if( blk_ioctl_parameter.block == 0 )
		goto good_params;

	/* so we don't divide by zero below */  
	if(orig_blksize == 0) 
		return -EINVAL; 

        if( blk_ioctl_parameter.block <= (lastlba % (orig_blksize / hardblocksize)))
		goto good_params;

	return -EINVAL; 

good_params:
        writelba = lastlba - blk_ioctl_parameter.block - 1;

        if (orig_blksize != hardblocksize)
                 set_blocksize(dev, hardblocksize);
    
        bh =  bread(dev, writelba, hardblocksize);
        if (!bh) {
		/* We hit the end of the disk */
		printk(KERN_WARNING
			"get_last_sector ioctl: getblk returned NULL.\n");
		rc = -EIO;
		goto out;
        }
    
        if (copy_from_user(bh->b_data, blk_ioctl_parameter.block_contents, 
		(bh->b_size > blk_ioctl_parameter.content_length) ? 
		blk_ioctl_parameter.content_length : bh->b_size)) {
		rc = -EFAULT;
		goto out_brelse;
	}
    
        mark_buffer_dirty(bh);
        ll_rw_block (WRITE, 1, &bh);
        wait_on_buffer (bh);
        if (!buffer_uptodate(bh))
		rc = -EIO; 
    
out_brelse:
        brelse(bh);
    
out:
        /* change block size back */
        if (orig_blksize != hardblocksize)
                 set_blocksize(dev, orig_blksize);
       
       return rc;
}

#endif /* CONFIG_IA64 */

