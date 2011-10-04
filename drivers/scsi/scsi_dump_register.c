/*
 *  linux/drivers/scsi/scsi_register.c
 *
 *  Copyright (C) 2005  FUJITSU LIMITED
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
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

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/diskdump.h>
#include "scsi.h"
#include "hosts.h"
#include "scsi_dump.h"


#define Warn(x, ...)	printk(KERN_WARNING "scsi_dump: " x "\n", ## __VA_ARGS__)

/*
 * Registered dump_ops entry
 */
struct scsi_dump_ops_holder {
	list_t list;
	Scsi_Host_Template *hostt;
	struct scsi_dump_ops *dump_ops;
};

static spinlock_t scsi_dump_lock = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(scsi_dump_ops_holders);


static struct scsi_dump_ops_holder *
find_template(Scsi_Host_Template *hostt)
{
	struct scsi_dump_ops_holder *holder;

	list_for_each_entry(holder, &scsi_dump_ops_holders, list)
		if (holder->hostt == hostt)
			return holder;
	return NULL;
}

/*
 * Search registered dump_ops list by passed Scsi_Host_Template.
 * If not exists, return NULL.
 */
struct scsi_dump_ops *
scsi_dump_find_template(Scsi_Host_Template *hostt)
{
	struct scsi_dump_ops_holder *holder;
	struct scsi_dump_ops *dump_ops;

	spin_lock(&scsi_dump_lock);
	holder = find_template(hostt);
	dump_ops = holder ? holder->dump_ops : NULL;
	spin_unlock(&scsi_dump_lock);

	return dump_ops;
}

/*
 * Register dump_ops to the list. If dump_ops exists, return -EXIST,
 * If succeeeded, return 0.
 * This is a new interface that the device driver which supports diskdump
 * register their scsi_dump_ops. Existing interface also works.
 */
int
scsi_dump_register(Scsi_Host_Template *hostt, struct scsi_dump_ops *dump_ops)
{
	struct scsi_dump_ops_holder *holder;

	if (!(holder = kmalloc(sizeof(*holder), GFP_KERNEL)))
		return -ENOMEM;

	holder->hostt = hostt;
	holder->dump_ops = dump_ops;

	spin_lock(&scsi_dump_lock);
	if (find_template(hostt)) {
		Warn("Host Template %s is already registered", hostt->name);
		spin_unlock(&scsi_dump_lock);
		kfree(holder);
		return -EEXIST;
	}

	list_add(&holder->list, &scsi_dump_ops_holders);
	spin_unlock(&scsi_dump_lock);

	return 0;
}

/*
 * Unregister dump_ops from the list. If dump_ops does exists, return -ENOENT,
 * If succeeeded, return 0.
 */
int
scsi_dump_unregister(Scsi_Host_Template *hostt)
{
	struct scsi_dump_ops_holder *holder;

	spin_lock(&scsi_dump_lock);
	holder = find_template(hostt);
	if (!holder) {
		Warn("Host Template %s is not registered", hostt->name);
		spin_unlock(&scsi_dump_lock);
		return -ENOENT;
	}

	list_del(&holder->list);
	spin_unlock(&scsi_dump_lock);
	kfree(holder);

	return 0;
}

static int init_scsi_dump_register(void)
{
	return 0;
}

static void cleanup_scsi_dump_regiser(void)
{
	struct scsi_dump_ops_holder *holder, *tmp;

	/* Flush all entries */
	spin_lock(&scsi_dump_lock);
	list_for_each_entry_safe(holder, tmp, &scsi_dump_ops_holders, list) {
		list_del(&holder->list);
		kfree(holder);
	}
	spin_unlock(&scsi_dump_lock);
}

EXPORT_SYMBOL(scsi_dump_find_template);
EXPORT_SYMBOL(scsi_dump_register);
EXPORT_SYMBOL(scsi_dump_unregister);

module_init(init_scsi_dump_register);
module_exit(cleanup_scsi_dump_regiser);
MODULE_LICENSE("GPL");
