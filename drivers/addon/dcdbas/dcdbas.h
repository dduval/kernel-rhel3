/*
 *  dcdbas.h: Definitions for Dell Systems Management Base driver
 *
 *  Copyright (C) 1995-2005 Dell Inc.
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

#ifndef _DCDBAS_H_
#define _DCDBAS_H_

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/version.h>

#ifdef HAVE_COMPAT_IOCTL

#define dcdbas_register_ioctl32(cmd)	{}
#define dcdbas_unregister_ioctl32(cmd)	{}

#else

#ifdef CONFIG_X86_64
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/ioctl32.h>
#else
#include <asm/ioctl32.h>
#endif
#endif

#ifdef CONFIG_X86_64
/* use 64bit ioctl handler for 32bit ioctls */
#define dcdbas_register_ioctl32(cmd)	register_ioctl32_conversion(cmd, NULL)
#define dcdbas_unregister_ioctl32(cmd)	unregister_ioctl32_conversion(cmd)
#else
#define dcdbas_register_ioctl32(cmd)	{}
#define dcdbas_unregister_ioctl32(cmd)	{}
#endif

#endif /* HAVE_COMPAT_IOCTL */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define MODULE_VERSION(VERSION)
#define cpumask_of_cpu(cpu)		(1 << cpu_logical_map(cpu))
typedef unsigned long cpumask_t;
#ifndef __user
#define __user
#endif
#endif

/*
 * IOCTL command values
 */
#define DCDBAS_IOC_TYPE				'U'
#define IOCTL_DCDBAS_CMD			_IO(DCDBAS_IOC_TYPE, 1)

/*
 * IOCTL request type values
 */
#define ESM_HOLD_OS_ON_SHUTDOWN			(41)
#define ESM_CANCEL_HOLD_OS_ON_SHUTDOWN		(42)
#define ESM_TVM_HC_ACTION			(43)
#define ESM_TVM_ALLOC_MEM			(44)
#define ESM_CALLINTF_REQ			(47)
#define ESM_TVM_READ_MEM			(48)
#define ESM_TVM_WRITE_MEM			(49)

#define MAX_DCDBAS_IOCTL_DATA_SIZE		(256 * 1024)
#define MAX_TVM_DMA_BUF_SIZE			(257 * 1024)

/*
 * IOCTL status values
 */
#define ESM_STATUS_CMD_UNSUCCESSFUL		(-1)
#define ESM_STATUS_CMD_SUCCESS			(0)
#define ESM_STATUS_CMD_NOT_IMPLEMENTED		(1)
#define ESM_STATUS_CMD_BAD			(2)
#define ESM_STATUS_CMD_TIMEOUT			(3)
#define ESM_STATUS_CMD_NO_SUCH_DEVICE		(7)
#define ESM_STATUS_CMD_DEVICE_BAD		(9)

/*
 * Host control action values
 */
#define HC_ACTION_NONE				(0)
#define HC_ACTION_HOST_CONTROL_POWEROFF		BIT(1)
#define HC_ACTION_HOST_CONTROL_POWERCYCLE	BIT(2)

/*
 * TVM SMI type values
 */
#define TVM_SMITYPE_NONE			(0)
#define TVM_SMITYPE_TYPE1			(1)
#define TVM_SMITYPE_TYPE2			(2)
#define TVM_SMITYPE_TYPE3			(3)

/*
 * APM command values
 */
#define ESM_APM_CMD				(0x0A0)
#define ESM_APM_CMD_HEADER_SIZE			(4)
#define ESM_APM_POWER_CYCLE			(0x10)
#define ESM_APM_LONG_CMD_FORMAT			BIT(7)

#define CMOS_BASE_PORT				(0x070)
#define CMOS_PAGE1_INDEX_PORT			(0)
#define CMOS_PAGE1_DATA_PORT			(1)
#define CMOS_PAGE2_INDEX_PORT_PIIX4		(2)
#define CMOS_PAGE2_DATA_PORT_PIIX4		(3)
#define PE1400_APM_CONTROL_PORT			(0x0B0)
#define PCAT_APM_CONTROL_PORT			(0x0B2)
#define PCAT_APM_STATUS_PORT			(0x0B3)
#define PE1300_CMOS_CMD_STRUCT_PTR		(0x38)
#define PE1400_CMOS_CMD_STRUCT_PTR		(0x70)

#define MAX_SYSMGMT_SHORTCMD_PARMBUF_LEN	(14)
#define MAX_SYSMGMT_LONGCMD_SGENTRY_NUM		(16)

#define TIMEOUT_USEC_SHORT_SEMA_BLOCKING	(10000)
#define EXPIRED_TIMER				(0)

struct dcdbas_ioctl_hdr {
	u64 reserved;
	s32 status;
	u32 req_type;
	u32 data_size;
} __attribute__ ((packed));

struct dcdbas_tvm_mem_alloc {
	u32 phys_address;
	u32 size;
} __attribute__ ((packed));

struct dcdbas_tvm_mem_read {
	u32 size;
	u8 buffer[1];
} __attribute__ ((packed));

struct dcdbas_tvm_mem_write {
	u32 phys_address;
	u32 size;
	u8 buffer[1];
} __attribute__ ((packed));

struct dcdbas_tvm_hc_action {
	u8 action_bitmap;
	u8 smi_type;
} __attribute__ ((packed));

struct dcdbas_callintf_cmd {
	u16 command_address;
	u8 command_code;
	u8 reserved[1];
	u32 signature;
	u32 command_buffer_size;
	u8 command_buffer[1];
} __attribute__ ((packed));

struct dcdbas_ioctl_req {
	struct dcdbas_ioctl_hdr hdr;
	union {
		struct dcdbas_tvm_mem_alloc tvm_mem_alloc;
		struct dcdbas_tvm_mem_read  tvm_mem_read;
		struct dcdbas_tvm_mem_write tvm_mem_write;
		struct dcdbas_tvm_hc_action tvm_hc_action;
		struct dcdbas_callintf_cmd  callintf_cmd;
	} __attribute__ ((packed)) data;
} __attribute__ ((packed));

struct apm_cmd {
	u8 command;
	s8 status;
	u16 reserved;
	union {
		struct {
			u8 parm[MAX_SYSMGMT_SHORTCMD_PARMBUF_LEN];
		} __attribute__ ((packed)) shortreq;

		struct {
			u16 num_sg_entries;
			struct {
				u32 size;
				u64 addr;
			} __attribute__ ((packed))
			sglist[MAX_SYSMGMT_LONGCMD_SGENTRY_NUM];
		} __attribute__ ((packed)) longreq;
	} __attribute__ ((packed)) parameters;
} __attribute__ ((packed));

#endif /* _DCDBAS_H_ */

