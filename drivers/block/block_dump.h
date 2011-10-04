
#ifndef BLOCKDUMP_H
#define BLOCKDUMP_H

/*
 * Extended block operations for dump for preserving binary compatibility.
 */
struct block_dump_ops {
	int (*sanity_check)(void *device);
	int (*rw_block)(void *device, int rw, unsigned long dump_block_nr, void *buf, int len, unsigned long start_sect, unsigned long nr_sects);
	int (*quiesce)(void *device);
	int (*shutdown)(void *device);
};

typedef struct __block_device_operations_dump {
	struct block_device_operations blk_fops;
	struct block_dump_ops *block_dump_ops;
	void *(*block_probe)(kdev_t dev);
	unsigned int (*block_add_device)(void *device);
	unsigned long (*poll)(int ctlr);
} block_device_operations_dump;

typedef struct _device_info_t
{
	void *device;
	void *blk_dump_ops;
	struct module *module;
} device_info_t;

#endif // BLOCKDUMP_H
