/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_cdev.c 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif
#include <linux/version.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/utsname.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/if_arp.h>
#include <linux/spinlock.h>

/* From drivers/scsi */
#include <sd.h>
#include <hosts.h>
#include <scsi.h>
#include <linux/ctype.h>

#include "lpfcdfc_version.h"
#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc_dfc.h"
#include "lpfc_diag.h"
#if defined(CONFIG_PPC64) || defined(CONFIG_X86_64)
extern int sys_ioctl(unsigned int, unsigned int, unsigned long);
extern int register_ioctl32_conversion(unsigned int cmd,
				       int (*handler)(unsigned int,
						      unsigned int,
						      unsigned long,
						      struct file *));
extern int unregister_ioctl32_conversion(unsigned int cmd);
#endif
#include "lpfc_ioctl.h"

#include <linux/rtnetlink.h>
#include <asm/byteorder.h>
#include <linux/module.h>

/* Configuration parameters defined */
#define LPFC_DEF_ICFG
#include "lpfc_cfgparm.h"
#include "lpfc_module_param.h"
#include "lpfc.conf"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"
#include "lpfc_util_ioctl.h"
#include "lpfc_hbaapi_ioctl.h"
#include "lpfc_debug_ioctl.h"

typedef int (*LPFC_IOCTL_FN)(lpfcHBA_t *, LPFCCMDINPUT_t *);

extern lpfcDRVR_t lpfcDRVR;
extern char* lpfc_release_version;

int lpfc_diag_init(void);
int lpfc_diag_uninit(void);
static int lpfc_major = 0;

/* A chrdev is used for diagnostic interface */
int lpfcdiag_ioctl(struct inode *inode, struct file *file,
		   unsigned int cmd, unsigned long arg);

static struct file_operations lpfc_fops = {
	.owner = THIS_MODULE,
	.ioctl = lpfcdiag_ioctl,
};

#define LPFCDFC_DRIVER_NAME "lpfcdfc"

int
lpfc_diag_init(void)
{
	int result;
	result = register_chrdev(lpfc_major, LPFCDFC_DRIVER_NAME, &lpfc_fops);
	if (result < 0)
		return (result);
	if (lpfc_major == 0)
		lpfc_major = result;	/* dynamic */
	return (0);
}

int
lpfc_diag_uninit(void)
{
	if (lpfc_major) {
		unregister_chrdev(lpfc_major, LPFCDFC_DRIVER_NAME);
		lpfc_major = 0;
	}
	return (0);
}

struct ioctls_registry_entry {
	struct list_head list;
	LPFC_IOCTL_FN lpfc_ioctl_fn;
};

struct ioctls_registry_entry lpfc_ioctls_registry = {
	.list = LIST_HEAD_INIT(lpfc_ioctls_registry.list)
};

int
reg_ioctl_entry(LPFC_IOCTL_FN fn)
{
	struct ioctls_registry_entry *new_lpfc_ioctls_registry_entry =
	    kmalloc(sizeof (struct ioctls_registry_entry), GFP_KERNEL);
	if (new_lpfc_ioctls_registry_entry == 0)
		return -ENOMEM;
	new_lpfc_ioctls_registry_entry->lpfc_ioctl_fn = fn;
	if (fn != 0) {
		list_add(&(new_lpfc_ioctls_registry_entry->list),
			 &(lpfc_ioctls_registry.list));
	}
	return 0;
}

int
unreg_ioctl_entry(LPFC_IOCTL_FN fn)
{
	struct list_head *p, *n;
	struct ioctls_registry_entry *entry;

	list_for_each_safe(p, n, &(lpfc_ioctls_registry.list)) {
		entry = list_entry(p, struct ioctls_registry_entry, list);
		if (entry->lpfc_ioctl_fn == fn) {
			list_del(p);
			kfree(entry);
			break;
		}
	}
	return 0;
}

void
unreg_all_ioctl_entries(void)
{
	struct list_head *p,*n;
	struct ioctls_registry_entry *entry;

	list_for_each_safe(p, n, &(lpfc_ioctls_registry.list)) {
		entry = list_entry(p, struct ioctls_registry_entry, list);
		list_del(p);
		kfree(entry);
	}
	return ;
}

int
lpfcdiag_ioctl(struct inode *inode,
	       struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = EINVAL, fd;
	lpfcHBA_t *phba;
	LPFCCMDINPUT_t *ci;
	unsigned long iflag;
	struct list_head *p;
	struct ioctls_registry_entry *entry;

	if (!arg)
		return (-EINVAL);

	ci = (LPFCCMDINPUT_t *) kmalloc(sizeof (LPFCCMDINPUT_t), GFP_ATOMIC);

	if (!ci)
		return (-ENOMEM);

	if (copy_from_user
	    ((uint8_t *) ci, (uint8_t *) arg, sizeof (LPFCCMDINPUT_t))) {
		kfree(ci);
		return (-EIO);
	}

	fd = ci->lpfc_brd;
	if (fd >= lpfcDRVR.num_devs) {
		kfree(ci);
		return (-EINVAL);
	}

	if ((phba = lpfc_get_phba_by_inst(fd)) == NULL) {
		kfree(ci);
		return (-EINVAL);
	}

        
	if ( phba->reset_pending) {
		kfree(ci);
		return(-ENODEV);
	}


	LPFC_DRVR_LOCK(phba, iflag);
	list_for_each(p, &(lpfc_ioctls_registry.list)) {
		entry = list_entry(p, struct ioctls_registry_entry, list);
		if (entry->lpfc_ioctl_fn) {
			rc = entry->lpfc_ioctl_fn(phba, ci);
			if (rc != -1)
				break;	/* This IOCTL has been serviced. Do not
					 bother to pass it to the ohter entries in
					 the registry */
		}
	}
	LPFC_DRVR_UNLOCK(phba, iflag);
	kfree(ci);
	return (-rc);
}
#if defined(CONFIG_PPC64) || defined(CONFIG_X86_64)
int
lpfc_ioctl32_handler(unsigned int fd, unsigned int cmd, unsigned long arg, struct file *file)
{
	LPFCCMDINPUT32_t arg32;
	LPFCCMDINPUT_t arg64;
	mm_segment_t old_fs;
	int ret;

	if(copy_from_user(&arg32, (void*)arg, sizeof(LPFCCMDINPUT32_t)))
		return -EFAULT;


	arg64.lpfc_brd = arg32.lpfc_brd;
	arg64.lpfc_ring = arg32.lpfc_ring;
	arg64.lpfc_iocb = arg32.lpfc_iocb;
	arg64.lpfc_flag = arg32.lpfc_flag;
	arg64.lpfc_arg1 = (void*)(unsigned long) arg32.lpfc_arg1;
	arg64.lpfc_arg2 = (void *)(unsigned long)arg32.lpfc_arg2;
	arg64.lpfc_arg3 = (void *)(unsigned long) arg32.lpfc_arg3;
	arg64.lpfc_dataout = (void *)(unsigned long) arg32.lpfc_dataout;
	arg64.lpfc_cmd = arg32.lpfc_cmd;
	arg64.lpfc_outsz = arg32.lpfc_outsz;
	arg64.lpfc_arg4 = arg32.lpfc_arg4;
	arg64.lpfc_arg5 = arg32.lpfc_arg5;
	arg64.lpfc_cntl = arg32.lpfc_cntl;
	

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_ioctl(fd, LPFC_DFC_CMD_IOCTL , (unsigned long)&arg64);
	set_fs(old_fs);
	
	
	arg32.lpfc_brd = arg64.lpfc_brd;
	arg32.lpfc_ring = arg64.lpfc_ring;
	arg32.lpfc_iocb = arg64.lpfc_iocb;
	arg32.lpfc_flag = arg64.lpfc_flag;
	arg32.lpfc_arg1 = (u32)(unsigned long)(arg64.lpfc_arg1);
	arg32.lpfc_arg2 = (u32)(unsigned long)(arg64.lpfc_arg2);
	arg32.lpfc_arg3 = (u32)(unsigned long) (arg64.lpfc_arg3);
	arg32.lpfc_dataout = (u32)(unsigned long) (arg64.lpfc_dataout);
	arg32.lpfc_cmd = arg64.lpfc_cmd;
	arg32.lpfc_outsz = arg64.lpfc_outsz;
	arg32.lpfc_arg4 = arg64.lpfc_arg4;
	arg32.lpfc_arg5 = arg64.lpfc_arg5;
	arg32.lpfc_cntl = arg64.lpfc_cntl;

	if(copy_to_user((void*)arg, &arg32, sizeof(LPFCCMDINPUT32_t)))
		return -EFAULT;

	return ret;
}
#endif
static int __init
lpfc_cdev_init(void)
{
	printk(LPFCDFC_MODULE_DESC "\n");
	printk(LPFCDFC_COPYRIGHT "\n");

	if (strcmp(lpfc_release_version, LPFC_DRIVER_VERSION) != 0) {
		printk("lpfcdfc requires lpfc version %s.  Current version is %s.\n",
		       LPFC_DRIVER_VERSION, lpfc_release_version);
		return -ENODEV;
	}

	if(unlikely(reg_ioctl_entry(lpfc_process_ioctl_util) != 0)) goto errexit;
	if(unlikely(reg_ioctl_entry(lpfc_process_ioctl_hbaapi) != 0)) goto errexit;
	if(unlikely(reg_ioctl_entry(lpfc_process_ioctl_dfc) != 0)) goto errexit;
	if(unlikely(lpfc_diag_init()!=0 )) goto errexit;
#if defined(CONFIG_PPC64) || defined(CONFIG_X86_64)
	if(register_ioctl32_conversion(LPFC_DFC_CMD_IOCTL32, lpfc_ioctl32_handler) !=0) goto errexit;
#endif
	return 0;

	errexit:
	unreg_all_ioctl_entries();
	return -ENODEV;		
}

static void __exit
lpfc_cdev_exit(void)
{
	unreg_ioctl_entry(lpfc_process_ioctl_util);
	unreg_ioctl_entry(lpfc_process_ioctl_hbaapi);
	unreg_ioctl_entry(lpfc_process_ioctl_dfc);
	lpfc_diag_uninit();
#if defined(CONFIG_PPC64) || defined(CONFIG_X86_64)
	unregister_ioctl32_conversion(LPFC_DFC_CMD_IOCTL32);
#endif
}

module_init(lpfc_cdev_init);
module_exit(lpfc_cdev_exit);

MODULE_DESCRIPTION("Emulex LightPulse Fibre Channel driver IOCTL support");
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");
MODULE_LICENSE("GPL");
