
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
 * ubsstruc.h:  ubsec internal structure definition header file
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 02/05/2001 PW Added code for key self test 
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#ifndef _UBSSTRUC_H_
#define _UBSSTRUC_H_


#define UBSEC_CIPHER_LIST 0
#define UBSEC_KEY_LIST 1
#define UBSEC_NUM_MCR_LISTS 2
#define UBSEC_MAX_MCR_LISTS 2

/* MCR statistics */
#define NUM_MCR_REGS 2
typedef struct MCRStat_s
{
UBS_UINT32 no_free_mcr_ret;
UBS_UINT32 num_packets_stat[MCR_MAXIMUM_PACKETS];
UBS_UINT32 push_mcr_stat[NUM_MCR_REGS];
UBS_UINT32 mcr_full_stat[NUM_MCR_REGS];
}MCRStat_t;

/*
 * Device Information structure. This contains all the state information
 * for the device. The device context points to this structure.
 */
typedef struct DeviceInfo_s {
  /* housekeeping information */
  UBS_UINT32 DeviceID;      /* Type of device */
  unsigned long BaseAddress;   /* Physical base address 	   	    */
  unsigned int IRQ;            /* IRQ associated with device. 	   	    */
  ubsec_Status_t Status;
  VOLATILE UBS_UINT32 *ControlReg[UBSEC_MAXREGISTERS];/* device register */
  unsigned int NumberOfMCRs[UBSEC_MAX_MCR_LISTS];   /* Number of MCRs allocated to this device.  */
  MasterCommand_pt  MCRList[UBSEC_MAX_MCR_LISTS];        /* MCR List, 0 for MCR1, 1 for MCR2  */
  MasterCommand_pt  NextFreeMCR[UBSEC_MAX_MCR_LISTS];    /* Next MCR to fill 	  	    */
  MasterCommand_pt  NextDeviceMCR[UBSEC_MAX_MCR_LISTS];  /* Next MCR to push to device. 	    */
  MasterCommand_pt  NextDoneMCR[UBSEC_MAX_MCR_LISTS];    /* Next MCR to be checked for        */
  VOLATILE int IRQEnabled;              /* Interrupt enabled indicator 	    */
  UBS_UINT32 InCriticalSection;      /* Primitive (single thread) mutex   */
  UBS_UINT32 NumberOfMCRLists;       /* Number of MCR Lists in this device */
  UBS_UINT32 IntEnableMask;          /* Interrupt mask values */
  UBS_UINT32 IntAckMask;             /* Interrupt mask values */
  UBS_UINT32 ResetConfig;            /* DMA Control reg state after reset */
  unsigned short InterruptSuppress;     /* Interrupt Suppress setting for MCR Flags field */
  unsigned short Reserved;              /* To maintain alignment */
  unsigned char *SelfTestMemArea;       /* Test memory area */
  OS_MemHandle_t SelfTestMemAreaHandle; /* Memory area handle */

  void (*SelfTestCallBack)(unsigned long Context,ubsec_Status_t Result);  /* Test user callback */
  unsigned long SelfTestContext; /* Self test user context */

  void (*KeySelfTestCallBack)(unsigned long Context,ubsec_Status_t Result);  /* Test user callback */
  unsigned long KeySelfTestContext; /* Self test user context */

  OS_DeviceInfo_t OsDeviceInfo;	/* pointer to structure containing info specific to an OS */
#ifdef UBSEC_STATS
  ubsec_Statistics_t Statistics;
#endif	
#ifdef DVT
  UBS_UINT32 DVTOptions;
#endif
#ifdef MCR_STATS
  MCRStat_t MCRstat;
#endif 
} DeviceInfo_t,*DeviceInfo_pt;

#define NULL_DEVICE_INFO (DeviceInfo_pt) 0

/*
 * Sizes for ubs Bud
 */
#define TEST_CIPHER_BLOCK_SIZE 64
#define TEST_AUTH_DATA_SIZE 32
#define TEST_DATA_SIZE (3*(TEST_CIPHER_BLOCK_SIZE+TEST_AUTH_DATA_SIZE))

/* need a array of 64 char (512 bits) for each parameter N G UserX Y and X, so the totoal length
   is 64 * 5 = 320 
*/
#define TEST_KEY_BLOCK_SIZE 64
#define TEST_KEY_OFFSET_SIZE 128
#define KEY_TEST_DATA_SIZE 2048 

#define FLUSH_ONLY_PUSHED 0
#define FLUSH_ALL         1


#endif  /* _UBSSTRUC_H_ */

