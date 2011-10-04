
/*
 * Broadcom Cryptonet Driver software is distributed as is, without any warranty
 * of any kind, either express or implied as further specified in the GNU Public
 * License. This software may be used and distributed according to the terms of
 * the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */

/******************************************************************************
 *
 * Copyright 2000
 * Broadcom Corporation
 * 16215 Alton Parkway
 * PO Box 57013
 * Irvine CA 92619-7013
 *
 *****************************************************************************/

/* 
 * Broadcom Corporation uBSec SDK 
 */

/*
 * ubssys.h:  ubsec operating system dependencies
 */

/*
 * Revision History:
 *
 * 09/xx/99 SOR Created.
 * 07/26/00 SOR Virtual/Physical Memory manipulation modifications
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel 
 * 10/09/2001 SRM 64 bit port.
 */

#ifndef _UBSLINUX_H
#define _UBSLINUX_H

#ifdef LINUX_DEVICE

#include <linux/version.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <asm/irq.h>           /* For NR_IRQS only. */
#include <asm/bitops.h>
#include <asm/io.h>                   


#include <linux/tqueue.h>
#include <linux/delay.h>

#define VOLATILE volatile
#define UBSECAPI
#define OS_MemHandle_t void *
#define OS_DeviceInfo_t void *


#ifdef UBSDBG
#define DbgPrint(x) printk x
#endif


extern void *LinuxMapPhysToIO(unsigned long Physical_Address, int size);
extern void LinuxUnMapIO(unsigned long ioaddr);
extern void LinuxAllocateIRQ(int irq,void *context);
extern void LinuxFreeIRQ(int irq, void *context);
extern void LinuxWaitus( int wait_us );
extern void *LinuxAllocateMemory(unsigned long size);
extern void *LinuxAllocateDMAMemory(unsigned long size);
extern void LinuxFreeMemory(void *virtual);
extern void LinuxFreeDMAMemory(void *virtual);
extern unsigned long LinuxGetPhysicalAddress(void *virtual);
extern unsigned long LinuxGetVirtualAddress(void *virtual);


/*************************** 
 * Memory allocations 
 ***************************/ 

#define OS_AllocateMemory(size)    	       LinuxAllocateMemory(size)
#define OS_AllocateDMAMemory(pDevice,size)     LinuxAllocateDMAMemory(size)
#define OS_FreeMemory(mem,size)     	       LinuxFreeMemory(mem);
#define OS_FreeDMAMemory(mem,size)  	       LinuxFreeDMAMemory(mem);

/*************************** 
 * Interrupt related functions
 ***************************/ 
#define OS_AllocateISR(irq,context,callback)   LinuxAllocateIRQ(irq,(void *)context->OsDeviceInfo)
#define OS_FreeISR(irq,context)                LinuxFreeIRQ(irq,(void *)context->OsDeviceInfo)
#define OS_ScheduleCallBack(callback,Context)  LinuxScheduleCallback((void *)Context,Context->OsDeviceInfo)
#define OS_Waitus(wait_us) 	               LinuxWaitus(wait_us) 

/*************************** 
 * Critical section functions
 ***************************/ 
#if 0
extern void 		LinuxInitCriticalSection(OS_DeviceInfo_t);
extern unsigned long 	LinuxEnterCriticalSection(OS_DeviceInfo_t);
extern unsigned long 	LinuxTestCriticalSection(OS_DeviceInfo_t);
extern void  		LinuxLeaveCriticalSection(OS_DeviceInfo_t);
#endif

#define OS_InitCriticalSection(pDevice)     	       LinuxInitCriticalSection(pDevice->OsDeviceInfo)
#define OS_EnterCriticalSection(pDevice,SaveConfig)    LinuxEnterCriticalSection(pDevice->OsDeviceInfo)
#define OS_LeaveCriticalSection(pDevice,SaveConfig)    LinuxLeaveCriticalSection(pDevice->OsDeviceInfo) 
#define OS_TestCriticalSection(pDevice,SaveConfig)     LinuxTestCriticalSection(pDevice->OsDeviceInfo)

/*************
 * IO Mapping 
 *************/ 
#define OS_MapPhysToIO(pDevice,Physical_Address,size)  LinuxMapPhysToIO(Physical_Address,size )
#define OS_UnMapIO(pDevice,ioaddr)     		       LinuxUnMapIO(ioaddr)

/********************************* 
 * virtal and physical addreesses 
 *********************************/ 
#define OS_GetPhysicalAddress(MemHandle) 	       LinuxGetPhysicalAddress(MemHandle)
#define OS_GetVirtualAddress(MemHandle)                LinuxGetVirtualAddress(MemHandle) 

/*************************** 
 * system memory functions 
 ***************************/ 
#define  RTL_MemZero(mem,bytes)        memset(( void *)mem,0,bytes)
#define  RTL_Memset(mem,val,bytes)     memset(( void *)mem,val,bytes)
#define  RTL_Memcpy(dest,source,bytes) memcpy(( void *)dest,( void *)source,bytes)
#define  RTL_Memcmp(dest,source,bytes) memcmp(( void *)dest,( void *)source,bytes)

/*************************** 
 * Access to our chip 
 ***************************/ 
#if 0
#define OS_IOMemWrite32(Address,val)   (*Address)=val
#define OS_IOMemRead32(Address)        (*Address)
#else
#define OS_IOMemWrite32(Address,val)   writel(val, Address)
#define OS_IOMemRead32(Address)        readl(Address)
#endif

#endif /* LINUX_DEVICE */

#endif /* _UBSLINUX_H_ */
