/*
 *  linux/drivers/block/diskdump.c
 *
 *  Copyright (C) 2004  FUJITSU LIMITED
 *  Copyright (C) 2002  Red Hat, Inc.
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
 *  Some codes were derived from netdump and copyright belongs to
 *  Red Hat, Inc.
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/utsname.h>
#include <linux/console.h>
#include <linux/smp_lock.h>
#include <linux/nmi.h>
#include <linux/genhd.h>
#include <linux/crc32.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/diskdump.h>
#include <linux/diskdumplib.h>
#include <linux/blk.h>
#include <asm/diskdump.h>

#define DEBUG 0
#if DEBUG
# define Dbg(x, ...) printk(KERN_INFO "disk_dump:" x "\n", ## __VA_ARGS__)
#else
# define Dbg(x...)
#endif

#define Err(x, ...)	printk(KERN_ERR "disk_dump: " x "\n", ## __VA_ARGS__);
#define Warn(x, ...)	printk(KERN_WARNING "disk_dump: " x "\n", ## __VA_ARGS__)
#define Info(x, ...)	printk(KERN_INFO "disk_dump: " x "\n", ## __VA_ARGS__)

#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

#define KM_DISKDUMP		KM_NETDUMP

#define ROUNDUP(x, y)	(((x) + ((y)-1))/(y))

/* 512byte sectors to blocks */
#define SECTOR_BLOCK(s)	((s) >> (DUMP_BLOCK_SHIFT - 9))

/* The number of block which is used for saving format information */
#define USER_PARAM_BLOCK	2

static unsigned int fallback_on_err = 1;
static unsigned int allow_risky_dumps = 1;
static unsigned int block_order = 2;
static unsigned int sample_rate = 8;
MODULE_PARM(fallback_on_err, "i");
MODULE_PARM(allow_risky_dumps, "i");
MODULE_PARM(block_order, "i");
MODULE_PARM(sample_rate, "i");

static unsigned long timestamp_1sec;
static uint32_t module_crc;
static char *scratch;
static struct disk_dump_header dump_header;
static struct disk_dump_sub_header dump_sub_header;

/* Registered dump devices */
static LIST_HEAD(disk_dump_devices);

/* Registered dump types, e.g. SCSI, ... */
static LIST_HEAD(disk_dump_types);

static spinlock_t disk_dump_lock = SPIN_LOCK_UNLOCKED;


static unsigned int header_blocks;		/* The size of all headers */
static unsigned int bitmap_blocks;		/* The size of bitmap header */
static unsigned int total_ram_blocks;		/* The size of memory */
static unsigned int total_blocks;		/* The sum of above */

#if CONFIG_SMP
static void freeze_cpu(void *dummy)
{
	unsigned int cpu = smp_processor_id();

	dump_header.tasks[cpu] = current;

	platform_freeze_cpu();
}
#endif

static int lapse = 0;		/* 200msec unit */

static inline unsigned long eta(unsigned long nr, unsigned long maxnr)
{
	unsigned long long eta;

	eta = ((maxnr << 8) / nr) * (unsigned long long)lapse;

	return (unsigned long)(eta >> 8) - lapse;
}

static inline void print_status(unsigned int nr, unsigned int maxnr)
{
	static char *spinner = "/|\\-";
	static unsigned long long prev_timestamp = 0;
	unsigned long long timestamp;

	platform_timestamp(timestamp);

	if (timestamp - prev_timestamp > (timestamp_1sec/5)) {
		prev_timestamp = timestamp;
		lapse++;
		printk("%u/%u    %lu ETA %c          \r",
			nr, maxnr, eta(nr, maxnr) / 5, spinner[lapse & 3]);
	}
}

static inline void clear_status(int nr, int maxnr)
{
	printk("                                       \r");
	lapse = 0;
}

/*
 * Checking the signature on a block. The format is as follows.
 *
 * 1st word = 'disk'
 * 2nd word = 'dump'
 * 3rd word = block number
 * 4th word = ((block number + 7) * 11) & 0xffffffff
 * 5th word = ((4th word + 7)* 11) & 0xffffffff
 * ..
 *
 * Return TRUE if the signature is correct, else return FALSE
 */
static int check_block_signature(void *buf, unsigned int block_nr)
{
	int word_nr = PAGE_SIZE / sizeof(int);
	int *words = buf;
	unsigned int val;
	int i;

	/*
	* Block 2 is used for the area which formatter saves options like
	* the sampling rate or the number of blocks. the Kernel part does not
	* check this block.
	*/
	if (block_nr == USER_PARAM_BLOCK)
		return 1;

	if (memcmp(buf, DUMP_PARTITION_SIGNATURE, sizeof(*words)))
		return FALSE;

	val = block_nr;
	for (i = 2; i < word_nr; i++) {
		if (words[i] != val)
			return FALSE;
		val = (val + 7) * 11;
	}

	return TRUE;
}

/*
 * Read one block into the dump partition
 */
static int read_blocks(struct disk_dump_partition *dump_part, unsigned int nr, char *buf, int len)
{
	struct disk_dump_device *device = dump_part->device;
	int ret;

	touch_nmi_watchdog();
	__cli();
	ret = device->ops.rw_block(dump_part, READ, nr, buf, len);
	if (ret < 0) {
		Err("read error on block %u", nr);
		return ret;
	}
	return 0;
}

static int write_blocks(struct disk_dump_partition *dump_part, unsigned int offs, char *buf, int len)
{
	struct disk_dump_device *device = dump_part->device;
	int ret;

	touch_nmi_watchdog();
	__cli();
	ret = device->ops.rw_block(dump_part, WRITE, offs, buf, len);
	if (ret < 0) {
		Err("write error on block %u", offs);
		return ret;
	}
	return 0;
}

/*
 * Initialize the common header
 */

/*
 * Write the common header
 */
static int write_header(struct disk_dump_partition *dump_part)
{
	memset(scratch, '\0', PAGE_SIZE);
	memcpy(scratch, &dump_header, sizeof(dump_header));

	return write_blocks(dump_part, 1, scratch, 1);
}

/*
 * Check the signaures in all blocks of the dump partition
 * Return TRUE if the signature is correct, else return FALSE
 */
static int check_dump_partition(struct disk_dump_partition *dump_part, unsigned int partition_size)
{
	unsigned int blk;
	int ret;
	unsigned int chunk_blks, skips;
	int i;

	if (sample_rate < 0)		/* No check */
		return TRUE;

	/*
	 * If the device has limitations of transfer size, use it.
	 */
	chunk_blks = 1 << block_order;
	if (dump_part->device->max_blocks &&
	    dump_part->device->max_blocks < chunk_blks)
		Warn("I/O size exceeds the maximum block size of SCSI device, signature check may fail");
	skips = chunk_blks << sample_rate;

	lapse = 0;
	for (blk = 0; blk < partition_size; blk += skips) {
		unsigned int len;
redo:
		len = min(chunk_blks, partition_size - blk);
		if ((ret = read_blocks(dump_part, blk, scratch, len)) < 0)
			return FALSE;
		print_status(blk + 1, partition_size);
		for (i = 0; i < len; i++)
			if (!check_block_signature(scratch + i * DUMP_BLOCK_SIZE, blk + i)) {
				Err("bad signature in block %u", blk + i);
				return FALSE;
			}
	}
	/* Check the end of the dump partition */
	if (blk - skips + chunk_blks < partition_size) {
		blk = partition_size - chunk_blks;
		goto redo;
	}
	clear_status(blk, partition_size);
	return TRUE;
}

/*
 * Check the signaures in the first blocks of the swap partition
 * Return 1 if the signature is correct, else return 0
 */
static int check_swap_partition(struct disk_dump_partition *dump_part,
					    unsigned int partition_size)
{
	int ret;
	union swap_header *swh;

	if ((ret = read_blocks(dump_part, 0, scratch, 1)) < 0)
	   return 0;

	swh = (union swap_header *)scratch;

	if (memcmp(swh->magic.magic, "SWAPSPACE2",
						sizeof("SWAPSPACE2") - 1) != 0)
								     return 0;

	if (swh->info.version != 1)
	   return 0;

	if (swh->info.last_page + 1 != SECTOR_BLOCK(dump_part->nr_sects) ||
				  swh->info.last_page < partition_size)
						      return 0;

	return 1;
}

/*
 * Write memory bitmap after location of dump headers.
 */
#define IDX2PAGENR(nr, byte, bit)	(((nr) * PAGE_SIZE + (byte)) * 8 + (bit))
static int write_bitmap(struct disk_dump_partition *dump_part, unsigned int bitmap_offset, unsigned int bitmap_blocks)
{
	unsigned int nr;
	int bit, byte;
	int ret = 0;
	unsigned char val;

	for (nr = 0; nr < bitmap_blocks; nr++) {
		for (byte = 0; byte < PAGE_SIZE; byte++) {
			val = 0;
			for (bit = 0; bit < 8; bit++)
				if (page_is_ram(IDX2PAGENR(nr, byte, bit)))
					val |= (1 << bit);
			scratch[byte] = (char)val;
		}
		if ((ret = write_blocks(dump_part, bitmap_offset + nr, scratch, 1)) < 0) {
			Err("I/O error %d on block %u", ret, bitmap_offset + nr);
			break;
		}
	}
	return ret;
}

/*
 * Write whole memory to dump partition.
 * Return value is the number of writen blocks.
 */
static int write_memory(struct disk_dump_partition *dump_part, int offset, unsigned int max_blocks_written, unsigned int *blocks_written)
{
	char *kaddr;
	unsigned int blocks = 0;
	struct page *page;
	unsigned long nr;
	int ret = 0;
	int blk_in_chunk = 0;

	for (nr = 0; nr < max_mapnr; nr++) {
		if (!page_is_ram(nr))
			continue;

		if (blocks >= max_blocks_written) {
			Warn("dump device is too small. %lu pages were not saved", max_mapnr - blocks);
			goto out;
		}
		page = pfn_to_page(nr);
		kaddr = kmap_atomic(page, KM_DISKDUMP);

		if (kern_addr_valid((unsigned long)kaddr)) {
			/*
			 * need to copy because adapter drivers use virt_to_bus()
			 */
			memcpy(scratch + blk_in_chunk * PAGE_SIZE, kaddr, PAGE_SIZE);
		} else {
			memset(scratch + blk_in_chunk * PAGE_SIZE, 0, PAGE_SIZE);
			sprintf(scratch + blk_in_chunk * PAGE_SIZE,
				"Unmapped page. PFN %lu\n", nr);
			printk("Unmapped page. PFN %lu\n", nr);
		}

		blk_in_chunk++;
		blocks++;
		kunmap_atomic(kaddr, KM_DISKDUMP);

		if (blk_in_chunk >= (1 << block_order)) {
			ret = write_blocks(dump_part, offset, scratch, blk_in_chunk);
			if (ret < 0) {
				Err("I/O error %d on block %u", ret, offset);
				break;
			}
			offset += blk_in_chunk;
			blk_in_chunk = 0;
			print_status(blocks, max_blocks_written);
		}
	}
	if (ret >= 0 && blk_in_chunk > 0) {
		ret = write_blocks(dump_part, offset, scratch, blk_in_chunk);
		if (ret < 0)
			Err("I/O error %d on block %u", ret, offset);
	}

out:
	clear_status(nr, max_blocks_written);

	*blocks_written = blocks;
	return ret;
}

/*
 * Select most suitable dump device. sanity_check() returns the state
 * of each dump device. 0 means OK, negative value means NG, and
 * positive value means it maybe work. select_dump_partition() first
 * try to select a sane device and if it has no sane device and
 * allow_risky_dumps is set, it select one from maybe OK devices.
 *
 * XXX We cannot handle multiple partitions yet.
 */
static struct disk_dump_partition *select_dump_partition(void)
{
	struct disk_dump_device *dump_device;
	struct disk_dump_partition *dump_part;
	int sanity;
	int strict_check = 1;

redo:
	/*
	 * Select a sane polling driver.
	 */
	list_for_each_entry(dump_device, &disk_dump_devices, list) {
		sanity = 0;
		if (dump_device->ops.sanity_check)
			sanity = dump_device->ops.sanity_check(dump_device);
		if (sanity < 0 || (sanity > 0 && strict_check))
			continue;
		list_for_each_entry(dump_part, &dump_device->partitions, list)
				return dump_part;
	}
	if (allow_risky_dumps && strict_check) {
		strict_check = 0;
		goto redo;
	}
	return NULL;
}

static void disk_dump(struct pt_regs *regs, void *platform_arg)
{
	unsigned long flags;
	int ret = -EIO;
	struct pt_regs myregs;
	unsigned int max_written_blocks, written_blocks;
	int i;
	struct disk_dump_device *dump_device = NULL;
	struct disk_dump_partition *dump_part = NULL;

	/* Inhibit interrupt and stop other CPUs */
	__save_flags(flags);
	__cli();

	diskdump_lib_init();

	/*
	 * Check the checksum of myself
	 */
	spin_trylock(&disk_dump_lock);
	if (!check_crc_module()) {
		Err("checksum error. diskdump common module may be compromised.");
		goto done;
	}

	diskdump_mode = 1;

	Dbg("notify dump start.");
	notifier_call_chain(&disk_dump_notifier_list, 0, NULL);

#if CONFIG_SMP
	dump_smp_call_function(freeze_cpu, NULL);
	mdelay(3000);
	printk("CPU frozen: ");
	for (i = 0; i < NR_CPUS; i++) {
		if (dump_header.tasks[i] != NULL)
			printk("#%d", i);

	}
	printk("\n");
	printk("CPU#%d is executing diskdump.\n", smp_processor_id());
#else
	mdelay(1000);
#endif
	dump_header.tasks[smp_processor_id()] = current;

	platform_fix_regs();

	if (list_empty(&disk_dump_devices)) {
		Err("adapter driver is not registered.");
		goto done;
	}

	printk("start dumping\n");

	if (!(dump_part = select_dump_partition())) {
		Err("No sane dump device found");
		goto done;
	}
	dump_device = dump_part->device;

	/* Force to Initialize io_request_lock */
	io_request_lock = SPIN_LOCK_UNLOCKED;

	/*
	 * Stop ongoing I/O with polling driver and make the shift to I/O mode
	 * for dump
	 */
	Dbg("do quiesce");
	if (dump_device->ops.quiesce)
		if ((ret = dump_device->ops.quiesce(dump_device)) < 0) {
			Err("quiesce failed. error %d", ret);
			goto done;
		}

	if (SECTOR_BLOCK(dump_part->nr_sects) < header_blocks + bitmap_blocks) {
		Warn("dump partition is too small. Aborted");
		ret = -EIO;
		goto done;
	}

	/* Check dump partition */
	printk("check dump partition...\n");
	if (!check_swap_partition(dump_part, total_blocks) &&
	    !check_dump_partition(dump_part, total_blocks)) {
		Err("check partition failed.");
		ret = -EIO;
		goto done;
	}

	/*
	 * Write the common header
	 */
	memcpy(dump_header.signature, DISK_DUMP_SIGNATURE, sizeof(dump_header.signature));
	dump_header.header_version   = DISK_DUMP_HEADER_VERSION;
	dump_header.utsname	     = system_utsname;
	dump_header.timestamp	     = xtime;
	dump_header.status	     = DUMP_HEADER_INCOMPLETED;
	dump_header.block_size	     = PAGE_SIZE;
	dump_header.sub_hdr_size     = size_of_sub_header();
	dump_header.bitmap_blocks    = bitmap_blocks;
	dump_header.max_mapnr	     = max_mapnr;
	dump_header.total_ram_blocks = total_ram_blocks;
	dump_header.device_blocks    = SECTOR_BLOCK(dump_part->nr_sects);
	dump_header.current_cpu      = smp_processor_id();
	dump_header.nr_cpus          = NR_CPUS;
	dump_header.written_blocks   = 2;

	write_header(dump_part);

	/*
	 * Write the architecture dependent header
	 */
	Dbg("write sub header");
	if ((ret = write_sub_header()) < 0) {
		Err("writing sub header failed. error %d", ret);
		goto done;
	}

	Dbg("writing memory bitmaps..");
	if ((ret = write_bitmap(dump_part, header_blocks, bitmap_blocks)) < 0)
		goto done;

	max_written_blocks = total_ram_blocks;
	if (dump_header.device_blocks < total_blocks) {
		Warn("dump partition is too small. actual blocks %u. expected blocks %u. whole memory will not be saved",
				dump_header.device_blocks, total_blocks);
		max_written_blocks -= (total_blocks - dump_header.device_blocks);
	}

	dump_header.written_blocks += dump_header.sub_hdr_size;
	dump_header.written_blocks += dump_header.bitmap_blocks;
	write_header(dump_part);

	printk("dumping memory..\n");
	if ((ret = write_memory(dump_part, header_blocks + bitmap_blocks,
				max_written_blocks, &written_blocks)) < 0)
		goto done;

	/*
	 * Set the number of block that is written into and write it
	 * into partition again.
	 */
	dump_header.written_blocks += written_blocks;
	dump_header.status = DUMP_HEADER_COMPLETED;
	write_header(dump_part);

	ret = 0;

done:
	Dbg("do adapter shutdown.");
	if (dump_device && dump_device->ops.shutdown)
		if (dump_device->ops.shutdown(dump_device))
			Err("adapter shutdown failed.");

	/*
	 * If diskdump failed and fallback_on_err is set,
	 * We just return and leave panic to netdump.
	 */
	if (fallback_on_err && ret != 0)
		return;

	Dbg("notify panic.");
	notifier_call_chain(&panic_notifier_list, 0, NULL);

	diskdump_lib_exit();

	if (panic_timeout > 0) {
		int i;

		printk(KERN_EMERG "Rebooting in %d second%s..",
			panic_timeout, "s" + (panic_timeout == 1));
		for (i = 0; i < panic_timeout; i++) {
			touch_nmi_watchdog();
			mdelay(1000);
		}
		printk("\n");
		machine_restart(NULL);
	}
	printk(KERN_EMERG "halt\n");
	for (;;) {
		touch_nmi_watchdog();
		machine_halt();
		mdelay(1000);
	}
}

static struct disk_dump_partition *find_dump_partition(kdev_t dev)
{
	struct disk_dump_device *dump_device;
	struct disk_dump_partition *dump_part;

	list_for_each_entry(dump_device, &disk_dump_devices, list)
		list_for_each_entry(dump_part, &dump_device->partitions, list)
			if (dump_part->dentry->d_inode->i_rdev == dev)
				return dump_part;
	return NULL;
}

static struct disk_dump_device *find_dump_device(void *real_device)
{
	struct disk_dump_device *dump_device;

	list_for_each_entry(dump_device, &disk_dump_devices, list)
		if (real_device == dump_device->device)
			return  dump_device;
	return NULL;
}

static void *find_real_device(kdev_t dev, struct disk_dump_type **_dump_type)
{
	void *real_device;
	struct disk_dump_type *dump_type;
	list_t *t;

	list_for_each_entry(dump_type, &disk_dump_types, list)
		if ((real_device = dump_type->probe(dev)) != NULL) {
			*_dump_type = dump_type;
			return real_device;
		}
	return NULL;
}

/*
 * Add dump partition structure corresponding to file to the dump device
 * structure.
 */
static int add_dump_partition(struct disk_dump_device *dump_device, struct file *file)
{
	struct disk_dump_partition *dump_part;
	struct inode *inode = file->f_dentry->d_inode;
	kdev_t dev = inode->i_rdev;
	struct gendisk *gd;

	if (!(dump_part = kmalloc(sizeof(*dump_part), GFP_KERNEL)))
		return -ENOMEM;

	dump_part->device   = dump_device;
	dump_part->vfsmount = mntget(file->f_vfsmnt);
	dump_part->dentry   = dget(file->f_dentry);

	gd = get_gendisk(inode->i_rdev);
	if (!gd)
		return -EINVAL;
	dump_part->nr_sects   = gd->part[MINOR(dev)].nr_sects;
	dump_part->start_sect = gd->part[MINOR(dev)].start_sect;

	if (SECTOR_BLOCK(dump_part->nr_sects) < total_blocks)
		Warn("%s is too small to save whole system memory\n", kdevname(dev));

	list_add(&dump_part->list, &dump_device->partitions);

	return 0;
}

/*
 * Add dump partition corresponding to file.
 * Must be called with disk_dump_lock held.
 */
static int add_dump(struct file *file)
{
	struct disk_dump_type *dump_type = NULL;
	struct disk_dump_device *dump_device;
	void *real_device;
	kdev_t dev = file->f_dentry->d_inode->i_rdev;
	int ret;

	/* Check whether this inode is already registered */
	if (find_dump_partition(dev))
		return -EEXIST;

	/* find dump_type and real device for this inode */
	if (!(real_device = find_real_device(dev, &dump_type)))
		return -ENXIO;

	dump_device = find_dump_device(real_device);
	if (dump_device == NULL) {
		/* real_device is not registered. create new dump_device */
		if (!(dump_device = kmalloc(sizeof(*dump_device), GFP_KERNEL)))
			return -ENOMEM;

		memset(dump_device, 0, sizeof(*dump_device));
		INIT_LIST_HEAD(&dump_device->partitions);

		dump_device->dump_type = dump_type;
		dump_device->device = real_device;
		if ((ret = dump_type->add_device(dump_device)) < 0) {
			kfree(dump_device);
			return ret;
		}
		__MOD_INC_USE_COUNT(dump_type->owner);
		list_add(&dump_device->list, &disk_dump_devices);
	}

	ret = add_dump_partition(dump_device, file);
	if (ret < 0) {
		dump_type->remove_device(dump_device);
		__MOD_DEC_USE_COUNT(dump_type->owner);
		list_del(&dump_device->list);
		kfree(dump_device);
	}

	return ret;
}

/*
 * Remove user specified dump partition.
 * Must be called with disk_dump_lock held.
 */
static int remove_dump(kdev_t dev)
{
	struct disk_dump_device *dump_device;
	struct disk_dump_partition *dump_part;
	struct disk_dump_type *dump_type;

	if (!(dump_part = find_dump_partition(dev)))
		return -ENOENT;

	dump_device = dump_part->device;

	list_del(&dump_part->list);
	mntput(dump_part->vfsmount);
	dput(dump_part->dentry);
	kfree(dump_part);

	if (list_empty(&dump_device->partitions)) {
		dump_type = dump_device->dump_type;
		dump_type->remove_device(dump_device);
		__MOD_DEC_USE_COUNT(dump_type->owner);
		list_del(&dump_device->list);
		kfree(dump_device);
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
static int proc_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long param)
{
	int fd = (int)param;
	int ret;
	struct file *dump_file;
	struct inode *dump_inode;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	dump_file = fget(fd);
	if (!dump_file)
		return -EBADF;
	dump_inode = dump_file->f_dentry->d_inode;
	if (!S_ISBLK(dump_inode->i_mode)) {
		fput(dump_file);
		return -EBADF;
	}

	spin_lock(&disk_dump_lock);
	switch (cmd) {
	case BLKADDDUMPDEVICE:
		ret = add_dump(dump_file);
		break;
	case BLKREMOVEDUMPDEVICE:
		ret = remove_dump(dump_inode->i_rdev);
		break;
	default:
		ret = -EINVAL;
	}

	set_crc_modules();
	spin_unlock(&disk_dump_lock);

	fput(dump_file);

	return ret;
}

static struct disk_dump_partition *dump_part_by_pos(struct seq_file *seq,
						    loff_t pos)
{
	struct disk_dump_device *dump_device;
	struct disk_dump_partition *dump_part;
	list_t *p;

	list_for_each_entry(dump_device, &disk_dump_devices, list) {
		seq->private = dump_device;
		list_for_each_entry(dump_part, &dump_device->partitions, list)
			if (!pos--)
				return dump_part;
	}
	return NULL;
}

static void *disk_dump_seq_start(struct seq_file *seq, loff_t *pos)
{
	loff_t n = *pos;

	if (!n--)
		return (void *)1;	/* header */

	spin_lock(&disk_dump_lock);
	return dump_part_by_pos(seq, n);
}

static void *disk_dump_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	list_t *partition = v;
	list_t *device = seq->private;
	struct disk_dump_device *dump_device;

	(*pos)++;
	if (v == (void *)1)
		return dump_part_by_pos(seq, 0);

	dump_device = list_entry(device, struct disk_dump_device, list);

	partition = partition->next;
	if (partition != &dump_device->partitions)
		return partition;

	device = device->next;
	seq->private = device;
	if (device == &disk_dump_devices)
		return NULL;

	dump_device = list_entry(device, struct disk_dump_device, list);

	return dump_device->partitions.next;
}

static void disk_dump_seq_stop(struct seq_file *seq, void *v)
{
	spin_unlock(&disk_dump_lock);
}

static int disk_dump_seq_show(struct seq_file *seq, void *v)
{
	struct disk_dump_partition *dump_part = v;
	char *page;
	char *path;

	if (v == (void *)1) {	/* header */
		seq_printf(seq, "# sample_rate: %u\n", sample_rate);
		seq_printf(seq, "# block_order: %u\n", block_order);
		seq_printf(seq, "# fallback_on_err: %u\n", fallback_on_err);
		seq_printf(seq, "# allow_risky_dumps: %u\n", allow_risky_dumps);
		seq_printf(seq, "# total_blocks: %u\n", total_blocks);
		seq_printf(seq, "#\n");

		return 0;
	}

	if (!(page = (char *)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	path = d_path(dump_part->dentry, dump_part->vfsmount, page, PAGE_SIZE);
	seq_printf(seq, "%s %lu %lu\n",
		path, dump_part->start_sect, dump_part->nr_sects);
	free_page((unsigned long)page);
	return 0;
}

static struct seq_operations disk_dump_seq_ops = {
	.start	= disk_dump_seq_start,
	.next	= disk_dump_seq_next,
	.stop	= disk_dump_seq_stop,
	.show	= disk_dump_seq_show,
};

static int disk_dump_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &disk_dump_seq_ops);
}

static struct file_operations disk_dump_fops = {
	.owner		= THIS_MODULE,
	.open		= disk_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.ioctl		= proc_ioctl,
};
#endif


int register_disk_dump_type(struct disk_dump_type *dump_type)
{
	spin_lock(&disk_dump_lock);
	list_add(&dump_type->list, &disk_dump_types);
	set_crc_modules();
	list_for_each_entry(dump_type, &disk_dump_types, list)
		if (dump_type->compute_cksum)
			dump_type->compute_cksum();
	spin_unlock(&disk_dump_lock);

	return 0;
}

int unregister_disk_dump_type(struct disk_dump_type *dump_type)
{
	spin_lock(&disk_dump_lock);
	list_del(&dump_type->list);
	set_crc_modules();
	spin_unlock(&disk_dump_lock);

	return 0;
}

EXPORT_SYMBOL(register_disk_dump_type);
EXPORT_SYMBOL(unregister_disk_dump_type);


static void compute_total_blocks(void)
{
	unsigned int nr;

	/*
	 * the number of block of the common header and the header
	 * that is depend on the architecture
	 *
	 * block 0:		dump partition header
	 * block 1:		dump header
	 * block 2:		dump subheader
	 * block 3..n:		memory bitmap
	 * block (n + 1)...:	saved memory
	 *
	 * We never overwrite block 0
	 */
	header_blocks = 2 + size_of_sub_header();

	total_ram_blocks = 0;
	for (nr = 0; nr < max_mapnr; nr++) {
		if (page_is_ram(nr))
			total_ram_blocks++;
	}

	bitmap_blocks = ROUNDUP(max_mapnr, 8 * PAGE_SIZE);

	/*
	 * The necessary size of area for dump is:
	 * 1 block for common header
	 * m blocks for architecture dependent header
	 * n blocks for memory bitmap
	 * and whole memory
	 */
	total_blocks = header_blocks + bitmap_blocks + total_ram_blocks;

	Info("total blocks required: %u (header %u + bitmap %u + memory %u)",
		total_blocks, header_blocks, bitmap_blocks, total_ram_blocks);
}

static int init_diskdump(void)
{
	unsigned long long t0;
	unsigned long long t1;
	struct page *page;

	if (!platform_supports_diskdump) {
		Err("platform does not support diskdump.");
		return -1;
	}

	/* Allocate one block that is used temporally */
	do {
		page = alloc_pages(GFP_KERNEL, block_order);
		if (page != NULL)
			break;
	} while (--block_order >= 0);
	if (!page) {
		Err("alloc_pages failed.");
		return -1;
	}
	scratch = page_address(page);
	Info("Maximum block size: %lu", PAGE_SIZE << block_order);

	if (diskdump_register_hook(disk_dump)) {
		Err("failed to register hooks.");
		return -1;
	}

	compute_total_blocks();

	platform_timestamp(t0);
	mdelay(1);
	platform_timestamp(t1);
	timestamp_1sec = (unsigned long)(t1 - t0) * 1000;

#ifdef CONFIG_PROC_FS
	{
		struct proc_dir_entry *p;

		p = create_proc_entry("diskdump", S_IRUGO|S_IWUSR, NULL);
		if (p)
			p->proc_fops = &disk_dump_fops;
	}
#endif

	return 0;
}

static void cleanup_diskdump(void)
{
	Info("shut down.");
	diskdump_unregister_hook();
	free_pages((unsigned long)scratch, block_order);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("diskdump", NULL);
#endif
}

module_init(init_diskdump);
module_exit(cleanup_diskdump);
MODULE_LICENSE("GPL");
