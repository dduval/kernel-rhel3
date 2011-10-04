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
 * ubssolaris.h:  ubsec solaris dependent routines 
 */
/*
 * Revision History:
 *
 * 6/27/99 PW Created.
 */


#ifndef _UBSSOLARIS_H
#define _UBSSOLARIS_H

#ifdef SOLARIS_DEVICE

#define UBS_UINT32  unsigned int

#define OS_DeviceInfo_t PhysDeviceInfo_t *
#define OS_MemHandle_t  UbsDMAHandle *
#define OS_DeviceInfo_pt PhysDeviceInfo_t *

#define OS_DeviceStruct_pt Ubsio *

#include "cdevdrv.h"

#ifdef UBSDBG
#define DbgPrint(x) printf x
#else
#define DbgPrint(x) 
#endif

#define VOLATILE volatile
#define UBSECAPI

#define OS_GetVirtualAddress(MemHandle)   ((OS_MemHandle_t *)(MemHandle)->virt_addr)
/*#define OS_GetPhysicalAddress(MemHandle)  ((UBS_UINT32 *)(OS_MemHandle_t *)(MemHandle)->phys_addr)*/
#define OS_GetPhysicalAddress(MemHandle)  ((UBS_UINT32 )(MemHandle)->phys_addr)


extern unsigned long Solaris_TestCriticalSection( OS_DeviceStruct_pt ubs_p);
extern unsigned long Solaris_EnterCriticalSection( OS_DeviceStruct_pt ubs_p);
extern void Solaris_LeaveCriticalSection( OS_DeviceStruct_pt ubs_p);

#define OS_TestCriticalSection(pDevice,SaveConfig) Solaris_TestCriticalSection(pDevice->OsDeviceInfo->ubsio_pt)
#define OS_EnterCriticalSection(pDevice,SaveConfig) Solaris_EnterCriticalSection(pDevice->OsDeviceInfo->ubsio_pt)
#define OS_LeaveCriticalSection(pDevice,SaveConfig) Solaris_LeaveCriticalSection(pDevice->OsDeviceInfo->ubsio_pt)

#define OS_InitCriticalSection(pDevice)

extern void *  Solaris_AllocateDMAMemory(OS_DeviceStruct_pt handle,int size);
extern void Solaris_FreeDMAMemory(void *mem,int size);
extern void *Solaris_AllocateMemory(int size);
extern void Solaris_FreeMemory(void *mem,int size);



/*************************** 
 * Memory allocations 
 ***************************/ 

#define OS_AllocateMemory(size)    	       Solaris_AllocateMemory(size)
#define OS_AllocateDMAMemory( pDevice, size)   Solaris_AllocateDMAMemory( pDevice->OsDeviceInfo->ubsio_pt,size)
#define OS_FreeMemory(mem,size)     	       Solaris_FreeMemory(mem,size);
#define OS_FreeDMAMemory(mem,size)  	       Solaris_FreeDMAMemory(mem,size)


#define OS_SyncToDevice(MemHandle,offset,size) (ddi_dma_sync((OS_MemHandle_t *)(MemHandle)->iopbhdl,(off_t)offset,(size_t)size,(u_int)DDI_DMA_SYNC_FORDEV))

#define OS_SyncToCPU(MemHandle,offset,size) (ddi_dma_sync((OS_MemHandle_t *)(MemHandle)->iopbhdl,(off_t)offset,(size_t)size,(u_int)DDI_DMA_SYNC_FORKERNEL))

#define OS_FreeISR(irq,context)

#define OS_AllocateISR(irq,context,callback)   Solaris_AllocateISR(irq,context,callback)
#if 0 /* FIX Me  -Gigi */
#define OS_ScheduleCallBack(CallBack,Context) ddi_trigger_softintr( (DeviceInfo_pt)Context->OsDeviceInfo->ubsio_pt->softint_id)  
#else
#define OS_ScheduleCallBack(CallBack,Context)
#endif

#define OS_MapPhysToIO(pDevice,Physical_Address,size)  (Physical_Address) 

#define OS_UnMapIO(pDevice,ioaddr)     
#define OS_IOMemWrite32(Address,val)  \
(*Address)=val

#define OS_IOMemRead32(Address) (*Address)
#define OS_Waitus(wait_us) delay(drv_usectohz(wait_us))

#define  RTL_MemZero(mem,bytes)   bzero((char *)mem,bytes)
#define  RTL_Memcpy(dest,source,bytes) bcopy((char *)source,(char *)dest,bytes)
#define  RTL_Memcmp(dest,source,bytes) bcmp((char *)dest,(char *)source,bytes)
#define  RTL_Memset(mem,val,bytes)   bzero((char *)mem,bytes)


#endif /* SOLARIS_DEVICE */
#endif  _UBSSOLARIS_H
