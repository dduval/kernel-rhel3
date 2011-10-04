
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 * cdevincl.h: Major include file for cdev
 *
 *
 * Revision History:
 *
 * May 2000 SOR/JTT Created
 * March 2001 PW Release for Linux 2.4 kernel 
 * Oct  2001 SRM 64 bit port
 */

#ifndef _CDEVINCL_H_
#define _CDEVINCL_H_

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <linux/version.h>
#define __NO_VERSION__ /* don't define kernel_verion in module.h */
#include <linux/config.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0))
#define MODVERSIONS
#define LINUX2dot2
#endif

#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif

#include <linux/types.h>
#include <linux/kernel.h> /* printk() */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wrapper.h>
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/string.h> /* memset(), memcpy() */
#include <linux/mm.h>     /* get_free_pages() */
#include <linux/time.h>
#include <linux/delay.h>   /* udelay() */

#include <linux/ptrace.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/segment.h>   
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/irq.h>
#include <asm/semaphore.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>


#ifndef UBS_UINT32
#define UBS_UINT32 unsigned int
#endif

#define vtophys(x) virt_to_bus(x)

#include "ubsec.h"
#include "ubsdefs.h"
#include "ubsio.h"
#include "keydefs.h"
#include "cdevextrn.h"
#include "snmp.h"

typedef struct CommandContext_s {
#ifndef LINUX2dot2
  wait_queue_head_t 	WaitQ; 
#else
  struct wait_queue     *WaitQ;
#endif
  struct timeval   	tv_start;
  unsigned long		CallBackStatus;
  int 			Status;
  int			pid;
} CommandContext_t, *CommandContext_pt;

#define MAX_SUPPORTED_DEVICES 10
#define SLEEP_ON_SRL_SEM

typedef struct DeviceInfo_s {
  ubsec_DeviceContext_t Context;
  struct pci_dev* pDev;
  struct tq_struct completion_handler_task;
  struct semaphore Semaphore_SRL;
  volatile u_int32_t	DeviceStatus;
  u_int32_t	DeviceFailuresCount;
  unsigned int  Features;
} DeviceInfo_t, *DeviceInfo_pt;

extern int NumDevices;
#ifndef INCL_BCM_OEM_1
extern DeviceInfo_t DeviceInfoList[MAX_SUPPORTED_DEVICES];
#endif

/* device status functions for failover */

int
GetDeviceStatus(DeviceInfo_t Device);

int
SetDeviceStatus(DeviceInfo_pt Device, int Status);


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)) /* Enable only for > 7.x */
#define UBSEC_SNMP_2_2
#else
#define UBSEC_SNMP_2_4
#endif

/* Device Type */
#ifndef UBS_DEVICE_TYPE
#if defined(UBSEC_5820)
#define UBS_DEVICE_TYPE "BCM5820"
#elif defined(UBSEC_582x)
#define UBS_DEVICE_TYPE "BCM582x"
#else
#define UBS_DEVICE_TYPE "BCM5805"
#endif
#endif

/* Debug Info  */
/* Define this tag in any file for a specific debug tag for the file */
#ifndef FILE_DEBUG_TAG
#define FILE_DEBUG_TAG  UBS_DEVICE_TYPE 
#endif

#define PRINTK printk("%s: ",FILE_DEBUG_TAG); printk

/* Validation of the Length */

#ifdef UBSEC_582x_CLASS_DEVICE
#define MAX_KEY_LENGTH_BITS 2048
#define MAX_MATH_LENGTH_BITS 2048
#else
#define MAX_KEY_LENGTH_BITS 1024
#define MAX_MATH_LENGTH_BITS 1024
#endif

#define MAX_RNG_LENGTH_BITS 4096
#define MAX_RSA_PRIVATE_KEY_LENGTH_BITS 1024
#define MAX_DSA_KEY_LENGTH_BITS 1024
#define MAX_DSA_MODQ_LENGTH_BITS 160
#define MAX_DSA_SIG_LENGTH_BITS 160
#define MAX_CRYPT_LENGTH_BITS 65535

#define CHECK_SIZE(param,maxsize) if ( param > maxsize){ 	\
 	PRINTK("Maximum length supported  %d. Given length %d.\n",maxsize,(int)param);						\
	return -EINVAL;						\
 }
#endif /*  _CDEVINCL_H_ */
