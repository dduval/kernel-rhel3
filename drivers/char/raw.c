/*
 * linux/drivers/char/raw.c
 *
 * Front-end raw character devices.  These can be bound to any block
 * devices to provide genuine Unix raw character device semantics.
 *
 * We reserve minor number 0 for a control interface.  ioctl()s on this
 * device are used to bind the other minor numbers to block devices.
 */

#include <linux/fs.h>
#include <linux/iobuf.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/raw.h>
#include <linux/capability.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/kiovec.h>
#include <linux/slab.h>

#define dprintk(x...) 

typedef struct raw_device_data_s {
	struct block_device *binding;
	int inuse, sector_size, sector_bits;
	struct semaphore mutex;
	unsigned varyio;
} raw_device_data_t;

static raw_device_data_t raw_devices[256];

static ssize_t rw_raw_dev(int rw, struct file *, char *, size_t, loff_t *);

ssize_t	raw_read(struct file *, char *, size_t, loff_t *);
ssize_t	raw_write(struct file *, const char *, size_t, loff_t *);
int	raw_open(struct inode *, struct file *);
int	raw_release(struct inode *, struct file *);
int	raw_ctl_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int	raw_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
int	raw_kvec_read(struct file *filp, kvec_cb_t cb, size_t size, loff_t pos);
int	raw_kvec_write(struct file *filp, kvec_cb_t cb, size_t size, loff_t pos);
ssize_t raw_readv(struct file *, const struct iovec *, unsigned long, loff_t *);
ssize_t raw_writev(struct file *, const struct iovec *, unsigned long, loff_t *);

static struct file_operations raw_fops = {
	read:		raw_read,
	write:		raw_write,
	open:		raw_open,
	release:	raw_release,
	readv:		raw_readv,
	writev:		raw_writev,
	ioctl:		raw_ioctl,
	aio_read:	generic_file_aio_read,
	aio_write:	generic_file_aio_write,
	kvec_read:	raw_kvec_read,
	kvec_write:	raw_kvec_write,
};

static struct file_operations raw_ctl_fops = {
	ioctl:		raw_ctl_ioctl,
	open:		raw_open,
};

extern int map_user_kiobuf_iovecs(int, struct kiobuf *, const struct iovec *, int);

static int __init raw_init(void)
{
	int i;
	register_chrdev(RAW_MAJOR, "raw", &raw_fops);

	for (i = 0; i < 256; i++)
		init_MUTEX(&raw_devices[i].mutex);

	return 0;
}

__initcall(raw_init);

/* 
 * Open/close code for raw IO.
 */

int raw_open(struct inode *inode, struct file *filp)
{
	int minor;
	struct block_device * bdev;
	kdev_t rdev;	/* it should eventually go away */
	int err;
	int sector_size;
	int sector_bits;

	minor = MINOR(inode->i_rdev);
	
	/* 
	 * Is it the control device? 
	 */
	
	if (minor == 0) {
		filp->f_op = &raw_ctl_fops;
		return 0;
	}

	down(&raw_devices[minor].mutex);
	/*
	 * No, it is a normal raw device.  All we need to do on open is
	 * to check that the device is bound, and force the underlying
	 * block device to a sector-size blocksize. 
	 */

	bdev = raw_devices[minor].binding;
	err = -ENODEV;
	if (!bdev)
		goto out;

	atomic_inc(&bdev->bd_count);
	rdev = to_kdev_t(bdev->bd_dev);
	err = blkdev_get(bdev, filp->f_mode, 0, BDEV_RAW);
	if (err)
		goto out;
	
	/*
	 * Don't change the blocksize if we already have users using
	 * this device 
	 */

	if (raw_devices[minor].inuse++)
		goto out;

	raw_devices[minor].varyio = get_blkdev_varyio(rdev);
	/* 
	 * Don't interfere with mounted devices: we cannot safely set
	 * the blocksize on a device which is already mounted.  
	 */
	
	sector_size = 512;
	if (is_mounted(rdev)) {
		if (blksize_size[MAJOR(rdev)])
			sector_size = blksize_size[MAJOR(rdev)][MINOR(rdev)];
		 raw_devices[minor].varyio = 0;
	} else {
		if (hardsect_size[MAJOR(rdev)])
			sector_size = hardsect_size[MAJOR(rdev)][MINOR(rdev)];
	}

	set_blocksize(rdev, sector_size);
	raw_devices[minor].sector_size = sector_size;

	for (sector_bits = 0; !(sector_size & 1); )
		sector_size>>=1, sector_bits++;
	raw_devices[minor].sector_bits = sector_bits;

 out:
	up(&raw_devices[minor].mutex);
	
	return err;
}

int raw_release(struct inode *inode, struct file *filp)
{
	int minor;
	struct block_device *bdev;
	
	minor = MINOR(inode->i_rdev);
	down(&raw_devices[minor].mutex);
	bdev = raw_devices[minor].binding;
	raw_devices[minor].inuse--;
	up(&raw_devices[minor].mutex);
	blkdev_put(bdev, BDEV_RAW);
	return 0;
}



/* Forward ioctls to the underlying block device. */ 
int raw_ioctl(struct inode *inode, 
		  struct file *flip,
		  unsigned int command, 
		  unsigned long arg)
{
	int minor = minor(inode->i_rdev), err; 
	struct block_device *b; 
	if (minor < 1 || minor > 255)
		return -ENODEV;

	b = raw_devices[minor].binding;
	err = -EINVAL; 
	if (b && b->bd_inode && b->bd_op && b->bd_op->ioctl) { 
		err = b->bd_op->ioctl(b->bd_inode, NULL, command, arg); 
	} 
	return err;
} 

/*
 * Deal with ioctls against the raw-device control interface, to bind
 * and unbind other raw devices.  
 */

int raw_ctl_ioctl(struct inode *inode, 
		  struct file *flip,
		  unsigned int command, 
		  unsigned long arg)
{
	struct raw_config_request rq;
	int err = 0;
	int minor;
	
	switch (command) {
	case RAW_SETBIND:
	case RAW_GETBIND:

		/* First, find out which raw minor we want */

		err = copy_from_user(&rq, (void *) arg, sizeof(rq));
		if (err)
			break;
		
		minor = rq.raw_minor;
		if (minor <= 0 || minor > MINORMASK) {
			err = -EINVAL;
			break;
		}

		if (command == RAW_SETBIND) {
			/*
			 * This is like making block devices, so demand the
			 * same capability
			 */
			if (!capable(CAP_SYS_ADMIN)) {
				err = -EPERM;
				break;
			}

			/* 
			 * For now, we don't need to check that the underlying
			 * block device is present or not: we can do that when
			 * the raw device is opened.  Just check that the
			 * major/minor numbers make sense. 
			 */

			if ((rq.block_major == NODEV && 
			     rq.block_minor != NODEV) ||
			    rq.block_major > MAX_BLKDEV ||
			    rq.block_minor > MINORMASK) {
				err = -EINVAL;
				break;
			}
			
			down(&raw_devices[minor].mutex);
			if (raw_devices[minor].inuse) {
				up(&raw_devices[minor].mutex);
				err = -EBUSY;
				break;
			}
			if (raw_devices[minor].binding)
				bdput(raw_devices[minor].binding);
			raw_devices[minor].binding = 
				bdget(kdev_t_to_nr(MKDEV(rq.block_major, rq.block_minor)));
			up(&raw_devices[minor].mutex);
		} else {
			struct block_device *bdev;
			kdev_t dev;

			bdev = raw_devices[minor].binding;
			if (bdev) {
				dev = to_kdev_t(bdev->bd_dev);
				rq.block_major = MAJOR(dev);
				rq.block_minor = MINOR(dev);
			} else {
				rq.block_major = rq.block_minor = 0;
			}
			err = copy_to_user((void *) arg, &rq, sizeof(rq));
		}
		break;
		
	default:
		err = -EINVAL;
	}
	
	return err;
}


ssize_t	raw_read(struct file *filp, char * buf, 
		 size_t size, loff_t *offp)
{
	return rw_raw_dev(READ, filp, buf, size, offp);
}

ssize_t	raw_write(struct file *filp, const char *buf, 
		  size_t size, loff_t *offp)
{
	return rw_raw_dev(WRITE, filp, (char *) buf, size, offp);
}

#define SECTOR_BITS 9
#define SECTOR_SIZE (1U << SECTOR_BITS)
#define SECTOR_MASK (SECTOR_SIZE - 1)

ssize_t	rw_raw_dev(int rw, struct file *filp, char *buf, 
		   size_t size, loff_t *offp)
{
	struct kiobuf * iobuf;
	int		err = 0;
	unsigned long	blocknr, blocks;
	size_t		transferred;
	int		iosize;
	int		i;
	int		minor;
	kdev_t		dev;
	unsigned long	limit;
	loff_t		off = *offp;

	int		sector_size, sector_bits, sector_mask;
	int		max_sectors;
	
	/*
	 * First, a few checks on device size limits 
	 */

	minor = MINOR(filp->f_dentry->d_inode->i_rdev);

	if (off < 0)
		return -EINVAL;

	err = alloc_kiovec(1, &iobuf);
	if (err)
		return err;
	iobuf->varyio = raw_devices[minor].varyio;
	
	dev = to_kdev_t(raw_devices[minor].binding->bd_dev);
	sector_size = raw_devices[minor].sector_size;
	sector_bits = raw_devices[minor].sector_bits;
	sector_mask = sector_size- 1;
	max_sectors = KIO_MAX_SECTORS >> (sector_bits - 9);
	
	if (blk_size[MAJOR(dev)])
		limit = (((loff_t) blk_size[MAJOR(dev)][MINOR(dev)]) << BLOCK_SIZE_BITS) >> sector_bits;
	else
		limit = INT_MAX;
	dprintk ("rw_raw_dev: dev %d:%d (+%d)\n",
		 MAJOR(dev), MINOR(dev), limit);
	
	err = -EINVAL;
	if ((off & sector_mask) || (size & sector_mask))
		goto out_free;
	err = 0;
	if (size)
		err = -ENXIO;
	if ((off >> sector_bits) >= limit)
		goto out_free;

	/*
	 * Split the IO into KIO_MAX_SECTORS chunks, mapping and
	 * unmapping the single kiobuf as we go to perform each chunk of
	 * IO.  
	 */

	transferred = 0;
	blocknr = off >> sector_bits;
	while (size > 0) {
		blocks = size >> sector_bits;
		if (blocks > max_sectors)
			blocks = max_sectors;
		if (blocks > limit - blocknr)
			blocks = limit - blocknr;
		if (!blocks)
			break;

		iosize = blocks << sector_bits;

		err = map_user_kiobuf(rw, iobuf, (unsigned long) buf, iosize);
		if (err)
			break;

		for (i=0; i < blocks; i++) 
			iobuf->blocks[i] = blocknr++;
		
		err = brw_kiovec(rw, 1, &iobuf, dev, iobuf->blocks, sector_size);

		if (rw == READ && err > 0)
			mark_dirty_kiobuf(iobuf, err);
		
		if (err >= 0) {
			transferred += err;
			size -= err;
			buf += err;
		}

		unmap_kiobuf(iobuf);

		if (err != iosize)
			break;
	}
	
	if (transferred) {
		*offp = off + transferred;
		err = transferred;
	}

 out_free:
	free_kiovec(1, &iobuf);
	return err;
}

static int raw_kvec_rw(struct file *filp, int rw, kvec_cb_t cb, size_t size, loff_t pos);
int raw_kvec_read(struct file *file, kvec_cb_t cb, size_t size, loff_t pos)
{
	return raw_kvec_rw(file, READ, cb, size, pos);
}

int raw_kvec_write(struct file *file, kvec_cb_t cb, size_t size, loff_t pos)
{
	return raw_kvec_rw(file, WRITE, cb, size, pos);
}

int	raw_kvec_rw(struct file *filp, int rw, kvec_cb_t cb, size_t size, loff_t pos)
{
	int		err;
	unsigned	minor;
	kdev_t		dev;
	unsigned long	limit, blocknr, blocks;

	unsigned	sector_size, sector_bits, sector_mask;
	unsigned	max_sectors;
	unsigned	i;

	pr_debug("raw_kvec_rw: %p %d %d %p %d %d %Lu\n", filp, rw, nr, kiovec, flags, size, pos);
	/*
	 * First, a few checks on device size limits 
	 */

	minor = MINOR(filp->f_dentry->d_inode->i_rdev);
	dev = to_kdev_t(raw_devices[minor].binding->bd_dev);
	sector_size = raw_devices[minor].sector_size;
	sector_bits = raw_devices[minor].sector_bits;
	sector_mask = sector_size- 1;
	max_sectors = 25000; //KIO_MAX_SECTORS >> (sector_bits - 9);
	
	if (blk_size[MAJOR(dev)])
		limit = (((loff_t) blk_size[MAJOR(dev)][MINOR(dev)]) << BLOCK_SIZE_BITS) >> sector_bits;
	else
		limit = INT_MAX;
	pr_debug ("raw_kvec_rw: dev %d:%d (+%d)\n",
		 MAJOR(dev), MINOR(dev), limit);

	/* EOF at the end */
	err = 0;
	if (!size || (pos >> sector_bits) == limit) {
		pr_debug("raw_kvec_rw: %Lu > %lu, %d\n", pos >> sector_bits, limit, sector_bits);
		cb.fn(cb.data, cb.vec, err);
		return 0;
	}

	/* ENXIO for io beyond the end */
	err = -ENXIO;
	if ((pos >> sector_bits) >= limit) {
		pr_debug("raw_kvec_rw: %Lu > %lu, %d\n", pos >> sector_bits, limit, sector_bits);
		goto out;
	}

	err = -EINVAL;
	if ((pos < 0) || (pos & sector_mask) || (size & sector_mask)) {
		pr_debug("pos(%Ld)/size(%lu) wrong(%d)\n", pos, size, sector_mask);
		goto out;
	}

	/* Verify that the scatter-gather list is sector aligned. */
	for (i=0; i<cb.vec->nr; i++)
		if ((cb.vec->veclet[i].offset & sector_mask) ||
		    (cb.vec->veclet[i].length & sector_mask)) {
			pr_debug("veclet offset/length wrong");
			goto out;
		}

	/*
	 * Split the IO into KIO_MAX_SECTORS chunks, mapping and
	 * unmapping the single kiobuf as we go to perform each chunk of
	 * IO.  
	 */

	blocknr = pos >> sector_bits;
	blocks = size >> sector_bits;
	if (blocks > max_sectors)
		blocks = max_sectors;
	if (blocks > limit - blocknr)
		blocks = limit - blocknr;
	err = -ENXIO;
	if (!blocks) {
		pr_debug("raw: !blocks %d %ld %ld\n", max_sectors, limit, blocknr);
		goto out;
	}

	err = brw_kvec_async(rw, cb, dev, blocks, blocknr, sector_bits);
out:
	if (err)
		pr_debug(KERN_DEBUG "raw_kvec_rw: ret is %d\n", err);
	return err;
}

/*
 * Process the readv/writev request by coalescing the iovecs into a 
 * single kiobuf.  Some restrictions apply:  iov_base and iov_len must 
 * be aligned to or a multiple of pagesize, and the total io size must be
 * small enough (i.e., less than max_sectors) to go to brw_kiovec in one
 * shot.  If these restrictions are not met, fall back to using normal
 * read/write calls.
 */
ssize_t rwvec_raw_dev(int rw, struct file * filp, const struct iovec *iov, 
		ulong iov_count, loff_t *offp)
{
	kdev_t	dev;
	struct	kiobuf *iobuf;
	ulong	blocknr, blocks, limit;
	int	i, minor, err;
	int	sector_size, sector_bits, sector_mask, max_sectors;
	ssize_t	tot_len;
	loff_t  off = *offp;

	if (off < 0)
		return -EINVAL;

	for (i = 0, tot_len = 0; i < iov_count; i++) {
		ssize_t tmp = tot_len;
		ssize_t len = (ssize_t) iov[i].iov_len;
		ulong base = (ulong) iov[i].iov_base;
		if ((len & ~PAGE_MASK) || (base & ~PAGE_MASK))
			goto fallback;
		if (len < 0)	/* size_t not fitting a ssize_t */
			return -EINVAL;	
		tot_len += len;
		if (tot_len < tmp) /* math overflow on the ssize_t */
			return -EINVAL;
	}

	minor = MINOR(filp->f_dentry->d_inode->i_rdev);

	err = alloc_kiovec(1, &iobuf);
	if (err)
		return err;
	iobuf->varyio = raw_devices[minor].varyio;

	dev = to_kdev_t(raw_devices[minor].binding->bd_dev);
	sector_size = raw_devices[minor].sector_size;
	sector_bits = raw_devices[minor].sector_bits;
	sector_mask = sector_size- 1;
	max_sectors = KIO_MAX_SECTORS >> (sector_bits - 9);

	if (blk_size[MAJOR(dev)])
		limit = (((loff_t) blk_size[MAJOR(dev)][MINOR(dev)]) << BLOCK_SIZE_BITS) >> sector_bits;
	else
		limit = INT_MAX;

	dprintk("rwvec_raw_dev: dev %d:%d (+%d)\n", 
		MAJOR(dev), MINOR(dev), limit);

	err = -EINVAL;
	if ((off & sector_mask) || (tot_len & sector_mask))
		goto out;
	err = 0;
	if (tot_len)
		err = -ENXIO;
	if ((off >> sector_bits) >= limit)
		goto out;

	blocknr = off >> sector_bits;
	blocks = tot_len >> sector_bits;

	if (!blocks)
		goto out;

	if ((blocks > max_sectors) || (blocks > limit - blocknr))
		goto fallback_free;

	err = expand_kiobuf(iobuf, blocks);
	if (err)
		goto out;
	err = map_user_kiobuf_iovecs(rw, iobuf, iov, iov_count);
	if (err)
		goto out;

	for (i=0; i < blocks; i++)
		iobuf->blocks[i] = blocknr++;

	err = brw_kiovec(rw, 1, &iobuf, dev, iobuf->blocks, sector_size);

	if (rw == READ && err > 0)
		mark_dirty_kiobuf(iobuf, err);

	unmap_kiobuf(iobuf);

	if (err > 0) 
		*offp = off + err;
out:
	free_kiovec(1, &iobuf);
	return err;

fallback_free:
	free_kiovec(1, &iobuf);
fallback:
	return fallback_readv_writev(rw, filp, iov, iov_count, offp);
}

ssize_t raw_readv(struct file *filp, const struct iovec *iov,
		unsigned long nr, loff_t *offp)
{
	return rwvec_raw_dev(READ, filp, iov, nr, offp);
}

ssize_t raw_writev(struct file *filp, const struct iovec *iov,
		unsigned long nr, loff_t *offp) 
{
	return rwvec_raw_dev(WRITE, filp, iov, nr, offp);
}
