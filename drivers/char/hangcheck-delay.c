/*
 * hangcheck-delay.c
 *
 * Test driver to cause a delay.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */


#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

#define VERSION_STR "0.7.0"

static int hangcheck_delay = 30;  /* 30 seconds */

MODULE_PARM(hangcheck_delay,"i");
MODULE_PARM_DESC(hangcheck_delay, "The amount of time to delay the system.");
MODULE_LICENSE("GPL");

static int __init hangcheck_init(void)
{
	int i;

	printk("Starting hangcheck delay %s of %d seconds.\n",
	       VERSION_STR, hangcheck_delay);

	cli();
	mdelay(hangcheck_delay * 1000);
	sti();

	return -ETIMEDOUT;
}  /* hangcheck_init() */


static void __exit hangcheck_exit(void)
{
	printk("Done delaying.\n");

}  /* hangcheck_exit() */

module_init(hangcheck_init);
module_exit(hangcheck_exit);
