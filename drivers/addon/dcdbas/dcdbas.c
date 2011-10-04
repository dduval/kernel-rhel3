/*
 *  dcdbas.c: Dell Systems Management Base Driver
 *
 *  Copyright (C) 1995-2005 Dell Inc.
 *
 *  The Dell Systems Management Base driver is a character driver that
 *  implements ioctls for Dell systems management software to use to
 *  communicate with the driver.  The driver provides support for Dell
 *  systems management software to manage the following Dell PowerEdge
 *  systems: 300, 1300, 1400, 400SC, 500SC, 1500SC, 1550, 600SC, 1600SC,
 *  650, 1655MC, 700, and 750.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License v2.0 as published by
 *  the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <asm/io.h>
#include <asm/scatterlist.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "dcdbas.h"

#define DRIVER_NAME		"dcdbas"
#define DRIVER_VERSION		"5.6.0-1"
#define DRIVER_DESCRIPTION	"Dell Systems Management Base Driver"

static int driver_major;
static int hold_on_shutdown;
static struct semaphore tvm_lock;
static u8 *tvm_dma_buf;
static u32 tvm_dma_buf_phys_addr;
static unsigned int tvm_dma_buf_size;
static u8 tvm_hc_action;
static u8 tvm_smi_type;

/**
 * dcdbas_alloc_32bit - allocate 32-bit addressable memory
 */
static void *dcdbas_alloc_32bit(size_t size, unsigned int flags)
{
	void *mem;
	u64 mask = 0xffffffff;
	unsigned int order = get_order(size);

	while ((mem = (void *)__get_free_pages(flags, order)) != NULL) {
		if (((u64)virt_to_phys(mem) & ~mask) == 0) {
			memset(mem, 0, size);
			break;
		}
		free_pages((unsigned long)mem, order);
		mem = NULL;
		if (flags & GFP_DMA)
			break;
		flags |= GFP_DMA;
	}
	return mem;
}

/**
 * dcdbas_free_32bit - free 32-bit addressable memory
 */
static void dcdbas_free_32bit(void *mem, size_t size)
{
	free_pages((unsigned long)mem, get_order(size));
}

/**
 * tvm_free_dma_buf - free buffer allocated for TVM systems management
 */
static void tvm_free_dma_buf(void)
{
	if (tvm_dma_buf == NULL)
		return;

	pr_debug("%s: phys: %x size: %u\n",
		__FUNCTION__, tvm_dma_buf_phys_addr, tvm_dma_buf_size);

	dcdbas_free_32bit(tvm_dma_buf, tvm_dma_buf_size);
	tvm_dma_buf = NULL;
	tvm_dma_buf_phys_addr = 0;
	tvm_dma_buf_size = 0;
}

/**
 * tvm_realloc_dma_buf - reallocate buffer for TVM systems management if needed
 */
static int tvm_realloc_dma_buf(unsigned int size)
{
	void *buf;

	if (size > MAX_TVM_DMA_BUF_SIZE)
		return -EINVAL;

	if (tvm_dma_buf_size >= size) {
		if ((size != 0) && (tvm_dma_buf == NULL)) {
			pr_debug("%s: corruption detected\n", __FUNCTION__);
			return -EFAULT;
		}

		/* current buffer is big enough */
		return 0;
	}

	/* new buffer is needed */
	buf = dcdbas_alloc_32bit(size, GFP_KERNEL);
	if (buf == NULL) {
		printk(KERN_INFO"%s: failed to allocate TVM memory size %u\n",
			DRIVER_NAME, size);
		return -ENOMEM;
	}

	/* free any existing buffer */
	tvm_free_dma_buf();

	/* set up new buffer for use */
	tvm_dma_buf = buf;
	tvm_dma_buf_phys_addr = (u32)virt_to_phys(buf);
	tvm_dma_buf_size = size;

	pr_debug("%s: phys: %x size: %u\n",
		__FUNCTION__, tvm_dma_buf_phys_addr, tvm_dma_buf_size);

	return 0;
}

/**
 * tvm_read_dma_buf - read systems management command response from TVM buffer
 * @ireq: IOCTL data
 */
static int tvm_read_dma_buf(struct dcdbas_ioctl_req *ireq)
{
	struct dcdbas_tvm_mem_read *tmr;
	struct apm_cmd *apm_cmd;
	unsigned int data_size_needed, buf_size_needed;

	data_size_needed = sizeof(struct dcdbas_tvm_mem_read);
	if (ireq->hdr.data_size < data_size_needed)
		return -EINVAL;

	tmr = &ireq->data.tvm_mem_read;
	pr_debug("%s: size: %u\n", __FUNCTION__, tmr->size);
	if (tmr->size < ESM_APM_CMD_HEADER_SIZE ||
	    tmr->size > MAX_DCDBAS_IOCTL_DATA_SIZE)
		return -EINVAL;

	data_size_needed = sizeof(struct dcdbas_tvm_mem_read) -
			   sizeof(tmr->buffer) +
			   tmr->size;
	if (ireq->hdr.data_size < data_size_needed)
		return -EINVAL;

	if (tvm_dma_buf == NULL ||
	    tvm_dma_buf_size < ESM_APM_CMD_HEADER_SIZE) {
		ireq->hdr.status = ESM_STATUS_CMD_DEVICE_BAD;
		return 0;
	}

	apm_cmd = (struct apm_cmd *)tvm_dma_buf;

	buf_size_needed = tmr->size;
	if (apm_cmd->command & ESM_APM_LONG_CMD_FORMAT)
		buf_size_needed +=
			(sizeof(struct apm_cmd) - ESM_APM_CMD_HEADER_SIZE);

	if (tvm_dma_buf_size < buf_size_needed) {
		ireq->hdr.status = ESM_STATUS_CMD_DEVICE_BAD;
		return 0;
	}

	if (apm_cmd->command & ESM_APM_LONG_CMD_FORMAT) {
		/* long command */
		memcpy(tmr->buffer, tvm_dma_buf, ESM_APM_CMD_HEADER_SIZE);
		if (tmr->size > ESM_APM_CMD_HEADER_SIZE) {
			memcpy(tmr->buffer + ESM_APM_CMD_HEADER_SIZE,
				tvm_dma_buf + sizeof(struct apm_cmd),
				tmr->size - ESM_APM_CMD_HEADER_SIZE);
		}
	} else {
		/* short command */
		memcpy(tmr->buffer, tvm_dma_buf, tmr->size);
	}

	ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
	return 0;
}

/**
 * tvm_write_dma_buf - write systems management command request to TVM buffer
 * @ireq: IOCTL data
 */
static int tvm_write_dma_buf(struct dcdbas_ioctl_req *ireq)
{
	struct dcdbas_tvm_mem_write *tmw;
	struct apm_cmd *apm_cmd;
	unsigned int data_size_needed, buf_size_needed;
	int ret;

	data_size_needed = sizeof(struct dcdbas_tvm_mem_write);
	if (ireq->hdr.data_size < data_size_needed)
		return -EINVAL;

	tmw = &ireq->data.tvm_mem_write;
	pr_debug("%s: size: %u\n", __FUNCTION__, tmw->size);
	if (tmw->size < ESM_APM_CMD_HEADER_SIZE ||
	    tmw->size > MAX_DCDBAS_IOCTL_DATA_SIZE)
		return -EINVAL;

	data_size_needed = sizeof(struct dcdbas_tvm_mem_write) -
			   sizeof(tmw->buffer) +
			   tmw->size;
	if (ireq->hdr.data_size < data_size_needed)
		return -EINVAL;

	apm_cmd = (struct apm_cmd *)tmw->buffer;
	buf_size_needed = tmw->size;
	if (apm_cmd->command & ESM_APM_LONG_CMD_FORMAT)
		buf_size_needed +=
			(sizeof(struct apm_cmd) - ESM_APM_CMD_HEADER_SIZE);

	/* make sure buffer is big enough for command */
	ret = tvm_realloc_dma_buf(buf_size_needed);
	if (ret)
		return ret;

	if (apm_cmd->command & ESM_APM_LONG_CMD_FORMAT) {
		/* long command */
		memcpy(tvm_dma_buf, tmw->buffer, ESM_APM_CMD_HEADER_SIZE);
		if (tmw->size > ESM_APM_CMD_HEADER_SIZE) {
			memcpy(tvm_dma_buf + sizeof(struct apm_cmd),
				tmw->buffer + ESM_APM_CMD_HEADER_SIZE,
				tmw->size - ESM_APM_CMD_HEADER_SIZE);
		}

		/* create scatter/gather list */
		apm_cmd = (struct apm_cmd *)tvm_dma_buf;
		apm_cmd->parameters.longreq.num_sg_entries = 1;
		apm_cmd->parameters.longreq.sglist[0].size =
			(tmw->size - ESM_APM_CMD_HEADER_SIZE);
		apm_cmd->parameters.longreq.sglist[0].addr =
			(tvm_dma_buf_phys_addr + sizeof(struct apm_cmd));
	} else {
		/* short command */
		memcpy(tvm_dma_buf, tmw->buffer, tmw->size);
	}

	tmw->phys_address = tvm_dma_buf_phys_addr;

	ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
	return 0;
}

/**
 * tvm_alloc_dma_buf - allocate buffer for TVM systems management
 * @ireq: IOCTL data
 */
static int tvm_alloc_dma_buf(struct dcdbas_ioctl_req *ireq)
{
	struct dcdbas_tvm_mem_alloc *tma;
	int ret;

	if (ireq->hdr.data_size < sizeof(struct dcdbas_tvm_mem_alloc))
		return -EINVAL;

	tma = &ireq->data.tvm_mem_alloc;
	pr_debug("%s: size: %u\n", __FUNCTION__, tma->size);

	ret = tvm_realloc_dma_buf(tma->size);
	if (ret)
		return ret;

	tma->phys_address = tvm_dma_buf_phys_addr;

	ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
	return 0;
}

/**
 * tvm_set_hc_action - set TVM system host control action
 * @ireq: IOCTL data
 */
static int tvm_set_hc_action(struct dcdbas_ioctl_req *ireq)
{
	struct dcdbas_tvm_hc_action *thca;
	int ret;

	if (ireq->hdr.data_size < sizeof(struct dcdbas_tvm_hc_action))
		return -EINVAL;

	/* make sure buffer is available for host control command */
	ret = tvm_realloc_dma_buf(sizeof(struct apm_cmd));
	if (ret)
		return ret;

	thca = &ireq->data.tvm_hc_action;
	pr_debug("%s: action_bitmap: %x smi_type: %u\n",
		__FUNCTION__, thca->action_bitmap, thca->smi_type);

	tvm_hc_action = thca->action_bitmap;
	tvm_smi_type = thca->smi_type;

	ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
	return 0;
}

/**
 * tvm_perform_cmd - perform command for TVM systems management
 *
 * The caller must set up the command in tvm_dma_buf.
 */
static s32 tvm_perform_cmd(void)
{
	struct apm_cmd *apm_cmd;
	u8 *data;
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
	unsigned long flags;
#endif
	u32 num_ticks;
	s8 cmd_status;
	u8 index;

	apm_cmd = (struct apm_cmd *)tvm_dma_buf;
	apm_cmd->status = ESM_STATUS_CMD_UNSUCCESSFUL;

	switch (tvm_smi_type) {
	case TVM_SMITYPE_TYPE1:
	{
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
		spin_lock_irqsave(&rtc_lock, flags);
#endif
		/* write physical address one byte at a time */
		data = (u8 *)&tvm_dma_buf_phys_addr;
		for (index = PE1300_CMOS_CMD_STRUCT_PTR;
		     index < (PE1300_CMOS_CMD_STRUCT_PTR + 4);
		     index++, data++) {
			outb(index,
			     (CMOS_BASE_PORT + CMOS_PAGE2_INDEX_PORT_PIIX4));
			outb(*data,
			     (CMOS_BASE_PORT + CMOS_PAGE2_DATA_PORT_PIIX4));
		}

		/* first set status to -1 as called by spec */
		cmd_status = ESM_STATUS_CMD_UNSUCCESSFUL;
		outb((u8)cmd_status, PCAT_APM_STATUS_PORT);

		/* generate SMM call */
		outb(ESM_APM_CMD, PCAT_APM_CONTROL_PORT);

#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
		spin_unlock_irqrestore(&rtc_lock, flags);
#endif

		/* wait a few to see if it executed */
		num_ticks = TIMEOUT_USEC_SHORT_SEMA_BLOCKING;
		while ((cmd_status = inb(PCAT_APM_STATUS_PORT))
		       == ESM_STATUS_CMD_UNSUCCESSFUL) {
			num_ticks--;
			if (num_ticks == EXPIRED_TIMER)
				return ESM_STATUS_CMD_TIMEOUT;
		}
	}
	break;

	case TVM_SMITYPE_TYPE2:
	case TVM_SMITYPE_TYPE3:
	{
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
		spin_lock_irqsave(&rtc_lock, flags);
#endif
		/* write physical address one byte at a time */
		data = (u8 *)&tvm_dma_buf_phys_addr;
		for (index = PE1400_CMOS_CMD_STRUCT_PTR;
		     index < (PE1400_CMOS_CMD_STRUCT_PTR + 4);
		     index++, data++) {
			outb(index, (CMOS_BASE_PORT + CMOS_PAGE1_INDEX_PORT));
			outb(*data, (CMOS_BASE_PORT + CMOS_PAGE1_DATA_PORT));
		}

		/* generate SMM call */
		if (tvm_smi_type == TVM_SMITYPE_TYPE3)
			outb(ESM_APM_CMD, PCAT_APM_CONTROL_PORT);
		else
			outb(ESM_APM_CMD, PE1400_APM_CONTROL_PORT);

#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
		/* restore RTC index pointer since it was written to above */
		CMOS_READ(RTC_REG_C);
		spin_unlock_irqrestore(&rtc_lock, flags);
#endif

		/* read control port back to serialize write */
		cmd_status = inb(PE1400_APM_CONTROL_PORT);

		/* wait a few to see if it executed */
		num_ticks = TIMEOUT_USEC_SHORT_SEMA_BLOCKING;
		while (apm_cmd->status == ESM_STATUS_CMD_UNSUCCESSFUL) {
			num_ticks--;
			if (num_ticks == EXPIRED_TIMER)
				return ESM_STATUS_CMD_TIMEOUT;
		}
	}
	break;

	default:
		return ESM_STATUS_CMD_NOT_IMPLEMENTED;
	}

	return ESM_STATUS_CMD_SUCCESS;
}

/**
 * tvm_host_control - initiate host control action on TVM system
 */
static s32 tvm_host_control(void)
{
	struct apm_cmd *apm_cmd;

	if (tvm_dma_buf == NULL ||
	    tvm_dma_buf_size < sizeof(struct apm_cmd)) {
		pr_debug("%s: TVM buffer error\n", __FUNCTION__);
		return ESM_STATUS_CMD_DEVICE_BAD;
	}

	apm_cmd = (struct apm_cmd *)tvm_dma_buf;

	/* power off takes precedence */
	if (tvm_hc_action & HC_ACTION_HOST_CONTROL_POWEROFF) {
		tvm_hc_action = HC_ACTION_NONE;

		apm_cmd->command = ESM_APM_POWER_CYCLE;
		apm_cmd->reserved = 0;
		*((s16 *)&apm_cmd->parameters.shortreq.parm[0]) = (s16)0;

		return tvm_perform_cmd();
	}
	if (tvm_hc_action & HC_ACTION_HOST_CONTROL_POWERCYCLE) {
		tvm_hc_action = HC_ACTION_NONE;

		apm_cmd->command = ESM_APM_POWER_CYCLE;
		apm_cmd->reserved = 0;
		*((s16 *)&apm_cmd->parameters.shortreq.parm[0]) = (s16)20;

		return tvm_perform_cmd();
	}

	tvm_hc_action = HC_ACTION_NONE;

	return ESM_STATUS_CMD_UNSUCCESSFUL;
}

/**
 * callintf_generate_smi - generate SMI for calling interface request
 * @ireq: IOCTL data
 */
static int callintf_generate_smi(struct dcdbas_ioctl_req *ireq)
{
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)
	struct dcdbas_callintf_cmd *ci_cmd;
	u32 command_buffer_phys_addr;
	cpumask_t old_mask;

	if (ireq->hdr.data_size < sizeof(struct dcdbas_callintf_cmd))
		return -EINVAL;

	ci_cmd = &ireq->data.callintf_cmd;
	command_buffer_phys_addr = virt_to_phys(ci_cmd->command_buffer);

	/* SMI requires CPU 0 */
	old_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(0));
	if (smp_processor_id() != 0) {
		pr_debug("%s: failed to get CPU 0\n", __FUNCTION__);
		set_cpus_allowed(current, old_mask);
		return -EBUSY;
	}

	/*
	 * SMI requires command buffer physical address in ebx and
	 * signature in ecx.
	 */

	/* generate SMI */
	asm volatile (
		"outb %b0,%w1"
		: /* no output args */
		: "a" (ci_cmd->command_code), 
		  "d" (ci_cmd->command_address), 
		  "b" (command_buffer_phys_addr), 
		  "c" (ci_cmd->signature)  
		: "memory"
	);

	set_cpus_allowed(current, old_mask);
	ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
	return 0;
#else
	return -ENOSYS;
#endif
}

/**
 * dcdbas_host_control - initiate host control action
 */
static void dcdbas_host_control(void)
{
	if (tvm_hc_action != HC_ACTION_NONE)
		tvm_host_control();
}

/**
 * dcdbas_dispatch_ioctl - dispatch IOCTL request
 * @ireq: IOCTL request
 */
static int dcdbas_dispatch_ioctl(struct dcdbas_ioctl_req *ireq)
{
	int retval = 0;

	pr_debug("%s: req_type: %u\n", __FUNCTION__, ireq->hdr.req_type);

	switch (ireq->hdr.req_type) {
	case ESM_TVM_READ_MEM:
		if (down_interruptible(&tvm_lock))
			return -ERESTARTSYS;
		retval = tvm_read_dma_buf(ireq);
		up(&tvm_lock);
		break;

	case ESM_TVM_WRITE_MEM:
		if (down_interruptible(&tvm_lock))
			return -ERESTARTSYS;
		retval = tvm_write_dma_buf(ireq);
		up(&tvm_lock);
		break;

	case ESM_TVM_ALLOC_MEM:
		if (down_interruptible(&tvm_lock))
			return -ERESTARTSYS;
		retval = tvm_alloc_dma_buf(ireq);
		up(&tvm_lock);
		break;

	case ESM_TVM_HC_ACTION:
		if (down_interruptible(&tvm_lock))
			return -ERESTARTSYS;
		retval = tvm_set_hc_action(ireq);
		up(&tvm_lock);
		break;

	case ESM_CALLINTF_REQ:
		retval = callintf_generate_smi(ireq);
		break;

	case ESM_HOLD_OS_ON_SHUTDOWN:
		/* firmware is going to perform host control action */
		hold_on_shutdown = 1;
		ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
		break;

	case ESM_CANCEL_HOLD_OS_ON_SHUTDOWN:
		hold_on_shutdown = 0;
		ireq->hdr.status = ESM_STATUS_CMD_SUCCESS;
		break;

	default:
		pr_debug("%s: unsupported req_type\n", __FUNCTION__);
		ireq->hdr.status = ESM_STATUS_CMD_NOT_IMPLEMENTED;
		break;
	}

	return retval;
}

/**
 * dcdbas_do_ioctl - process ioctl request
 * @filp: file object for device
 * @cmd: IOCTL command
 * @arg: IOCTL request data
 */
static int dcdbas_do_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct dcdbas_ioctl_req __user *ubuf;
	struct dcdbas_ioctl_req *kbuf;
	struct dcdbas_ioctl_hdr hdr;
	unsigned long size;
	int ret;

	if (cmd != IOCTL_DCDBAS_CMD) {
		ret = -EINVAL;
		goto out1;
	}

	ubuf = (struct dcdbas_ioctl_req __user *)arg;
	if (copy_from_user(&hdr, ubuf, sizeof(struct dcdbas_ioctl_hdr))) {
		ret = -EFAULT;
		goto out1;
	}

	if (hdr.data_size > MAX_DCDBAS_IOCTL_DATA_SIZE) {
		ret = -EINVAL;
		goto out1;
	}

	size = sizeof(struct dcdbas_ioctl_hdr) + hdr.data_size;
	if ((kbuf = dcdbas_alloc_32bit(size, GFP_KERNEL)) == NULL) {
		printk(KERN_INFO
			"%s: failed to allocate ioctl memory size %lu\n",
			DRIVER_NAME, size);
		ret = -ENOMEM;
		goto out1;
	}

	if (copy_from_user(kbuf, ubuf, size)) {
		ret = -EFAULT;
		goto out2;
	}

	if ((ret = dcdbas_dispatch_ioctl(kbuf)) != 0)
		goto out2;

	if (copy_to_user(ubuf, kbuf, size))
		ret = -EFAULT;

out2:
	dcdbas_free_32bit(kbuf, size);
out1:
	return ret;
}

/**
 * dcdbas_ioctl - ioctl handler
 * @inode: inode for device
 * @filp: file object for device
 * @cmd: IOCTL command
 * @arg: IOCTL request data
 */
static int dcdbas_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	return dcdbas_do_ioctl(filp, cmd, arg);
}

#ifdef HAVE_COMPAT_IOCTL
/**
 * dcdbas_compat_ioctl - compat ioctl handler
 * @filp: file object for device
 * @cmd: IOCTL command
 * @arg: IOCTL request data
 */
static long dcdbas_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	return dcdbas_do_ioctl(filp, cmd, arg);
}
#endif

/**
 * dcdbas_reboot_notify - handle reboot notification
 * @nb: info about registered reboot notifier
 * @code: notification code
 * @unused: unused argument
 */
static int dcdbas_reboot_notify(struct notifier_block *nb, unsigned long code,
				void *unused)
{
	static unsigned int notify_cnt = 0;

	switch (code) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		if (hold_on_shutdown) {
			/* firmware is going to perform host control action */
			if (++notify_cnt == 2) {
				printk(KERN_WARNING
					"Please wait for shutdown "
					"action to complete...\n");
				dcdbas_host_control();
			}
			/*
			 * register again and initiate the host control
			 * action on the second notification to allow
			 * everyone that registered to be notified
			 */
			register_reboot_notifier(nb);
		}
		break;
	}

	return NOTIFY_DONE;
}

static struct file_operations dcdbas_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	dcdbas_ioctl,
#ifdef HAVE_COMPAT_IOCTL
	.compat_ioctl =	dcdbas_compat_ioctl,
#endif
};

static struct notifier_block dcdbas_reboot_nb = {
	.notifier_call = dcdbas_reboot_notify,
	.next =		 NULL,
	.priority =	 0
};

/**
 * dcdbas_init - initialize driver
 */
static int __init dcdbas_init(void)
{
	int ret;

	tvm_hc_action = HC_ACTION_NONE;
	tvm_smi_type = TVM_SMITYPE_NONE;
	sema_init(&tvm_lock, 1);

	ret = register_chrdev(0, DRIVER_NAME, &dcdbas_fops);
	if (ret < 0) {
		printk(KERN_INFO"%s: register_chrdev failed with error %d\n",
			DRIVER_NAME, ret);
		goto error1;
	}
	driver_major = ret;

	register_reboot_notifier(&dcdbas_reboot_nb);
	dcdbas_register_ioctl32(IOCTL_DCDBAS_CMD);

	printk(KERN_INFO"%s: %s (version %s)\n",
		DRIVER_NAME, DRIVER_DESCRIPTION, DRIVER_VERSION);

	return 0;

error1:
	return ret;
}

/**
 * dcdbas_exit - perform driver cleanup
 */
static void __exit dcdbas_exit(void)
{
	dcdbas_unregister_ioctl32(IOCTL_DCDBAS_CMD);
	unregister_reboot_notifier(&dcdbas_reboot_nb);
	unregister_chrdev(driver_major, DRIVER_NAME);
	tvm_free_dma_buf();
}

module_init(dcdbas_init);
module_exit(dcdbas_exit);

MODULE_DESCRIPTION(DRIVER_DESCRIPTION" (version "DRIVER_VERSION")");
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Dell Inc.");
MODULE_LICENSE("GPL");

