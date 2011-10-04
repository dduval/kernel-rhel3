
/*
 *    Block dump driver for block drivers to support diskdump functionality
 *    without depending on diskdump driver.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/crc32.h>
#include <linux/diskdump.h>
#include <linux/diskdumplib.h>
#include "block_dump.h"

/* Embedded module documentation macros - see modules.h */
MODULE_AUTHOR("Hewlett-Packard Company");
MODULE_DESCRIPTION("Mid-level Block Driver for diskdump");
MODULE_LICENSE("GPL");

static uint32_t module_crc;

/* function prototypes */
static int block_dump_sanity_check(struct disk_dump_device *dump_device);
static int block_dump_rw_block(struct disk_dump_partition *dump_part, int rw, unsigned long dump_block_nr, void *buf, int len);
static int block_dump_quiesce(struct disk_dump_device *dump_device);
static int block_dump_shutdown(struct disk_dump_device *dump_device);
static void *block_dump_probe(kdev_t dev);
static int block_dump_add_device(struct disk_dump_device *dump_device);
static void block_dump_remove_device(struct disk_dump_device *dump_device);
static void block_dump_compute_cksum(void);

/*
 * This MACRO is to get the starting address of blcok_device_operations_dump strucutre using blk_fops pointer * , which is a member of block_device_operations_dump structure 
 */
#define BLOCK_OPS_EXT(blk_fops)	container_of(blk_fops, block_device_operations_dump, blk_fops)

static struct disk_dump_type block_dump_type = {
	.probe		= block_dump_probe,
	.add_device	= block_dump_add_device,
	.remove_device	= block_dump_remove_device,
	.compute_cksum	= block_dump_compute_cksum,
	.owner		= THIS_MODULE,
};

static struct disk_dump_device_ops block_dump_device_ops = {
	.sanity_check	= block_dump_sanity_check,
	.rw_block	= block_dump_rw_block,
	.quiesce	= block_dump_quiesce,
	.shutdown	= block_dump_shutdown,
};


static int block_dump_shutdown(struct disk_dump_device *dump_device) {

	device_info_t *device_info = dump_device->device;
	block_device_operations_dump *blk_dev_ops = device_info->blk_dump_ops;

	if ( blk_dev_ops->block_dump_ops->shutdown != NULL ) {
		return blk_dev_ops->block_dump_ops->shutdown(device_info->device);
	}

	return -1;	
}

static int block_dump_quiesce(struct disk_dump_device *dump_device) {

	device_info_t *device_info = dump_device->device;
	block_device_operations_dump *blk_dev_ops = device_info->blk_dump_ops;

	if ( blk_dev_ops->block_dump_ops->quiesce == NULL ) {
		return -1;
	}

	blk_dev_ops->block_dump_ops->quiesce(device_info->device);
	diskdump_register_poll(device_info->device, (void *)blk_dev_ops->poll);

	return 0;	
}

static int block_dump_rw_block(struct disk_dump_partition *dump_part, 
			int rw, unsigned long dump_block_nr, void *buf, int len) {

	device_info_t *device_info = ((struct disk_dump_device *)dump_part->device)->device;
	block_device_operations_dump *blk_dev_ops = device_info->blk_dump_ops;
	struct disk_dump_device *dump_device = dump_part->device;

	if ( blk_dev_ops->block_dump_ops->rw_block != NULL ) {
		return blk_dev_ops->block_dump_ops->rw_block(device_info->device, rw, 
			dump_block_nr, buf, len, dump_part->start_sect, dump_part->nr_sects);
	}

	return -1;	
}

static int block_dump_sanity_check(struct disk_dump_device *dump_device)
{
	device_info_t *device_info = dump_device->device;
	block_device_operations_dump *blk_dev_ops = device_info->blk_dump_ops;

	if (!check_crc_module()) {
		printk("<1>checksum error.  block dump module may be compromised\n");
		return -EINVAL;
	}

	if ( blk_dev_ops->block_dump_ops->sanity_check != NULL ) {
		return blk_dev_ops->block_dump_ops->sanity_check(device_info->device);
	}

	return -1;	
}

static void *block_dump_probe(kdev_t dev)
{
	const struct block_device_operations *blk_fops;
	struct block_device *blk_dev; 
	block_device_operations_dump *blk_dump_ops;
	device_info_t *device_info;
	
	set_crc_modules();	
	device_info =  kmalloc(sizeof(device_info_t), GFP_KERNEL);

	blk_dev = bdget( (dev_t)dev);
	blk_fops = blk_dev->bd_op;

	if (strcmp("cciss", blk_fops->owner->name)==0)
		blk_dump_ops = BLOCK_OPS_EXT(blk_fops);
	else
		return NULL;
	
	if ( blk_dump_ops->block_probe != NULL ) {
		device_info->device = blk_dump_ops->block_probe(dev);	
		/* Save driver module so usage count can be modified when
		 * in use.
		 */
		device_info->module = blk_fops->owner;
		device_info->blk_dump_ops = blk_dump_ops;
		return device_info;
	}

	return NULL;
}

static int block_dump_add_device(struct disk_dump_device *dump_device)
{
	device_info_t *device_info = dump_device->device;
	block_device_operations_dump *blk_dev_ops = device_info->blk_dump_ops;

	if(!memcpy(&dump_device->ops, &block_dump_device_ops, sizeof(struct disk_dump_device_ops)))
		return -1;

	if ( device_info->module )
		__MOD_INC_USE_COUNT(device_info->module);

	if ( blk_dev_ops->block_add_device != NULL ) {
		dump_device->max_blocks = blk_dev_ops->block_add_device(device_info->device);
	}

	return 0;
}

static void block_dump_remove_device(struct disk_dump_device *dump_device)
{
	device_info_t *device_info = dump_device->device;

	if (device_info->module)
		__MOD_DEC_USE_COUNT(device_info->module);
}

static void block_dump_compute_cksum(void)
{
	set_crc_modules();
}

static int __init init_block_dump_module(void)
{
	int ret;

	/* register with diskdump here. */
	if ((ret = register_disk_dump_type(&block_dump_type)) < 0 ) {
		printk("<1>Register of diskdump type failed\n");
		return ret;
	}

	set_crc_modules();	

	return ret;
}

static void __exit cleanup_block_dump_module(void)
{
	if (unregister_disk_dump_type(&block_dump_type) < 0 )
		printk("<1>Error unregistering diskdump\n");
}

module_init(init_block_dump_module);
module_exit(cleanup_block_dump_module);

