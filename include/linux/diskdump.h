#ifndef _LINUX_DISKDUMP_H
#define _LINUX_DISKDUMP_H

/*
 * linux/include/linux/diskdump.h
 *
 * Copyright (c) 2004 FUJITSU LIMITED
 *
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

#include <linux/list.h>
#include <linux/mount.h>
#include <linux/dcache.h>
#include <linux/blkdev.h>
#include <linux/utsname.h>
#include <linux/notifier.h>

/* The minimum Dump I/O unit. Must be the same of PAGE_SIZE */
#define DUMP_BLOCK_SIZE		PAGE_SIZE
#define DUMP_BLOCK_SHIFT	PAGE_SHIFT

/* Dump ioctls */
#define BLKADDDUMPDEVICE	0xdf00		/* Add a dump device */
#define BLKREMOVEDUMPDEVICE	0xdf01		/* Delete a dump device */

int diskdump_register_hook(void (*dump_func)(struct pt_regs *, void *));
void diskdump_unregister_hook(void);

/* notifiers to be called before starting dump */
extern struct notifier_block *disk_dump_notifier_list;

/*
 * The handler that adapter driver provides for the common module of
 * dump
 */
struct disk_dump_partition;
struct disk_dump_device;

struct disk_dump_type {
	void *(*probe)(kdev_t);
	int (*add_device)(struct disk_dump_device *);
	void (*remove_device)(struct disk_dump_device *);
	void (*compute_cksum)(void);
	struct module *owner;
	list_t list;
};

struct disk_dump_device_ops {
	int (*sanity_check)(struct disk_dump_device *);
	int (*quiesce)(struct disk_dump_device *);
	int (*shutdown)(struct disk_dump_device *);
	int (*rw_block)(struct disk_dump_partition *, int rw, unsigned long block, void *buf, int nr_blocks);
};

/* The data structure for a dump device */
struct disk_dump_device {
	list_t list;
	struct disk_dump_device_ops ops;
	struct disk_dump_type *dump_type;
	void *device;
	unsigned int max_blocks;
	list_t partitions;
};

/* The data structure for a dump partition */
struct disk_dump_partition {
	list_t list;
	struct disk_dump_device *device;
	struct vfsmount *vfsmount;
	struct dentry *dentry;
	unsigned long start_sect;
	unsigned long nr_sects;
};


int register_disk_dump_type(struct disk_dump_type *);
int unregister_disk_dump_type(struct disk_dump_type *);


/* The signature which is written in each block in the dump partition */
#define DUMP_PARTITION_SIGNATURE	"diskdump"

/*
 * Architecture-independent dump header
 */
#define DISK_DUMP_SIGNATURE		"DISKDUMP"
#define DISK_DUMP_HEADER_VERSION	1
#define DUMP_HEADER_COMPLETED	0
#define DUMP_HEADER_INCOMPLETED	1

struct disk_dump_header {
	char			signature[8];	/* = "DISKDUMP" */
	int			header_version;	/* Dump header version */
	struct new_utsname	utsname;	/* copy of system_utsname */
	struct timeval		timestamp;	/* Time stamp */
	unsigned int		status;		/* Above flags */
	int			block_size;	/* Size of a block in byte */
	int			sub_hdr_size;	/* Size of arch dependent
						   header in blocks */
	unsigned int		bitmap_blocks;	/* Size of Memory bitmap in
						   block */
	unsigned int		max_mapnr;	/* = max_mapnr */
	unsigned int		total_ram_blocks;/* Size of Memory in block */
	unsigned int		device_blocks;	/* Number of total blocks in
						 * the dump device */
	unsigned int		written_blocks;	/* Number of written blocks */
	unsigned int		current_cpu;	/* CPU# which handles dump */
	int			nr_cpus;	/* Number of CPUs */
	struct task_struct	*tasks[NR_CPUS];
};

/*
 * Calculate the check sum of the whole module
 */
#define get_crc_module()						\
({									\
	struct module *module = &__this_module;				\
	crc32_le(0, (char *)(module + 1), module->size - sizeof(*module)); \
})

#define set_crc_modules()						\
({									\
	module_crc = 0;							\
	module_crc = get_crc_module();					\
})

/*
 * Compare the checksum value that is stored in module_crc to the check
 * sum of current whole module. Must be called with holding disk_dump_lock.
 * Return TRUE if they are the same, else return FALSE
 *
 */
#define check_crc_module()						\
({									\
	uint32_t orig_crc, cur_crc;					\
									\
	orig_crc = module_crc; module_crc = 0;				\
	cur_crc = get_crc_module();					\
	module_crc = orig_crc;						\
	orig_crc == cur_crc;						\
})


#endif /* _LINUX_DISKDUMP_H */
