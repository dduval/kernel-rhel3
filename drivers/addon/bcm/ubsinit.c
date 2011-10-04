
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
 * ubsinit.c:  ubsec initialization and cleanup modules.
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

/*
 * ubsec_InitDevice:
 *
 * Initialize a device for operation. 
 * Return a status indicating result of the initialization 
 * along with a context to be used in further device operations.
 */
ubsec_Status_t
ubsec_InitDevice(unsigned short DeviceID,   /* PCI device ID */
		 unsigned long BaseAddress,  /* Physical address of device */
		 unsigned int irq,           /* IRQ of device. */
		 unsigned int CipherPipeLineDepth, /* Number of Cipher MCRs */
		 unsigned int KeyPipeLineDepth,   /* Number of key MCRS */
		 ubsec_DeviceContext_pt Context,  /* Pointer to context */
		 OS_DeviceInfo_t OSContext)	/* pointer to OS-specific device context */
{
  DeviceInfo_pt pDevice;
  UBS_UINT32 *tmpAddress;
  int i;
  int NumRegs;

    /* announce ourselves */
  Dbg_Print(DBG_VERSION,( "ubSec: SRL v%d.%x%c\n",UBSEC_VERSION_MAJOR,UBSEC_VERSION_MINOR,UBSEC_VERSION_REV));
  
#ifdef UBSEC_582x_CLASS_DEVICE
  if (DeviceID < BROADCOM_DEVICE_ID_5820) {
    Dbg_Print(DBG_INITD,( "ubSec:  SRL configuration not compatible with the BCM580x!\n" ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
#else
  if (DeviceID > BROADCOM_DEVICE_ID_5805) {
    Dbg_Print(DBG_INITD,( "ubSec:  SRL configuration not compatible with the BCM5820/5821!\n" ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
#endif

  /*
   * Allocate and initialize the device information structure.
   */
  if ((pDevice = AllocDeviceInfo((unsigned long) DeviceID,CipherPipeLineDepth,KeyPipeLineDepth,OSContext)) == NULL) {
    Dbg_Print(DBG_INITD,( "ubSec:  Alloc context failed!\n" ));
    return(UBSEC_STATUS_NO_RESOURCE );
  }
  Dbg_Print(DBG_INITD,( "ubSec: Alloc Info OK\n"));

  /* 
   * Save pointer to OS device context
   */
  pDevice->OsDeviceInfo = OSContext;
  pDevice->Status=UBSEC_STATUS_SUCCESS;
	
  /* 
   * Save Device ID information.
   */
  pDevice->BaseAddress=BaseAddress; /* Save for ID purpose */
  pDevice->IRQ=irq;
    /* Save the OS context */

  if (UBSEC_IS_CRYPTO_DEVICEID(DeviceID)) {
    NumRegs= UBSEC_CRYPTO_DEVICE_REGISTERS;
    pDevice->IntAckMask=UBSEC_CRYPTO_DEVICE_IACK_MASK;
    pDevice->IntEnableMask=UBSEC_CRYPTO_DEVICE_IENABLE_MASK;
  }
  else {
    NumRegs=UBSEC_KEY_DEVICE_REGISTERS;
    pDevice->IntAckMask=UBSEC_KEY_DEVICE_IACK_MASK;
    pDevice->IntEnableMask=UBSEC_KEY_DEVICE_IENABLE_MASK;
  }

  if (DeviceID >= BROADCOM_DEVICE_ID_5821) {
    /* The MCRn_ALL_EMPTY bits do not have their own enable bits. */
    /* They will cause H/W interrupts if the associated MCRn_DONE */
    /* interrupt is enabled. Even though the EMPTY bits are reset */
    /* when the associated MCRn_DONE bit is ack'd, they should be */
    /* ack'd along with the DONE bits just in case the associated */
    /* MCRn_DONE interrupts were suppressed.                      */
    pDevice->IntAckMask |= (MCR1_ALL_EMPTY | MCR2_ALL_EMPTY);
    /* Set the CryptoNet endianess bits here */
   #if (defined(UBSEC_582x) && (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN))
    /* SRL was built with BCM582x in mind; enable H/W big endian mode */
    pDevice->ResetConfig = UBS_BIG_ENDIAN_MODE;
   #else
    /* Device must use H/W little endian mode */
    pDevice->ResetConfig = UBS_LITTLE_ENDIAN_MODE;
   #endif
    /* Set the Interrupt Suppress bit here */
   #if (defined(UBSEC_582x) && defined(COMPLETE_ON_COMMAND_THREAD)) 
    /* SRL was built with BCM582x in mind, and we complete during command issuance */
    pDevice->InterruptSuppress = MCR_INTERRUPT_SUPPRESS;
   #else /* 5820 mode (fall back to BCM5820 functionality) */
    pDevice->InterruptSuppress = 0;
   #endif /* InterruptSuppress en/disable */
   #ifndef UBSEC_HW_NORMALIZE
    /* To enable SW normalization in 5821 chip */
    pDevice->ResetConfig |= SW_NORM_EN;
   #endif
  }
  else if (DeviceID == BROADCOM_DEVICE_ID_5820) {
    /* Set the CryptoNet endianess bits here */
    pDevice->ResetConfig = UBS_LITTLE_ENDIAN_MODE;
    /* Set the Interrupt Suppress bit here */
    pDevice->InterruptSuppress = 0;
   #ifndef UBSEC_HW_NORMALIZE
    /* To enable HW normalization in 5820 chip */
    pDevice->ResetConfig |= SW_NORM_EN;
   #endif
  }
  else {
    /* Set the CryptoNet endianess bits here */
    pDevice->ResetConfig = UBS_LITTLE_ENDIAN_MODE;
    /* Set the Interrupt Suppress bit here */
    pDevice->InterruptSuppress = 0;
  }
      


  /* Map the physical base address to our address space. */
  tmpAddress=(UBS_UINT32 *) OS_MapPhysToIO(pDevice,pDevice->BaseAddress,NumRegs*sizeof(UBS_UINT32));

  Dbg_Print(DBG_INITD,( "ubSec: Map Address %08x\n",tmpAddress));

    /* Save the device registers for easy access */
  for (i=0; i < UBSEC_MAXREGISTERS; i++)
    pDevice->ControlReg[i] = tmpAddress++; 

    /* Just in case acknowledge pending interrupts */
  UBSEC_ACK_INT(pDevice);

    /* Make sure that nothing is happening. */
  UBSEC_RESET_DEVICE(pDevice);

  *Context= (ubsec_DeviceContext_t) pDevice; 

  Dbg_Print(DBG_INITD,( "ubSec: Device Reset\n"));

#ifndef POLL
    /* Allocate our ISR and enable interrupts */
  OS_AllocateISR(irq, pDevice, ubsec_ISR);
  UBSEC_ENABLE_INT(pDevice);
#else
  UBSEC_DISABLE_INT(pDevice);
#endif


  OS_InitCriticalSection(pDevice);

  Dbg_Print(DBG_INITD,( "ubSec: Init OK\n"));

  return (UBSEC_STATUS_SUCCESS);
}

/*
 * ubsec_ShutdownDevice:
 *
 * Disable the device from operating and free up any related resources.
 */
ubsec_Status_t
ubsec_ShutdownDevice( ubsec_DeviceContext_t Context)
{
  unsigned long i;

  /* To keep the compiler happy.... */
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
    
  Dbg_Print(DBG_INIT,( "ubSec:  Shutdown device \n" ));
  UBSEC_DISABLE_INT(pDevice);

#ifndef POLL  
  /* free up irq and disable device interrupts */
  OS_FreeISR(pDevice->IRQ,pDevice);
#endif

  /* Wait for outstanding requests to complete */
  for (i=0; i < pDevice->NumberOfMCRLists ; i++) {
    while (WaitForCompletion(pDevice,1000000,i)
	   == UBSEC_STATUS_SUCCESS);
  }

  UBSEC_RESET_DEVICE(pDevice);

  UBSEC_DISABLE_INT(pDevice);
  UBSEC_ACK_INT(pDevice);

  /* Complete pending events that have not completed*/
  FlushDevice(pDevice,UBSEC_STATUS_CANCELLED,FLUSH_ALL);

  /* Free up the device information resource. */
  FreeDeviceInfo( pDevice );
  return(UBSEC_STATUS_SUCCESS);
}

/*
 * ubsec_ResetDevice:
 *
 * Reset the device.
 */
ubsec_Status_t
ubsec_ResetDevice( ubsec_DeviceContext_t Context)
{
  unsigned long i;
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  unsigned long SaveConfig;

  if (OS_EnterCriticalSection(pDevice,SaveConfig)) {
    return(UBSEC_STATUS_DEVICE_BUSY);
  }

 
#ifndef POLL
 /* Disable interrupts while resetting. */
  UBSEC_DISABLE_INT(pDevice);
#endif

  /* Wait for outstanding requests to complete */
  for (i=0; i < pDevice->NumberOfMCRLists ; i++) {
    while (WaitForCompletion( pDevice,1000000,i)
	 == UBSEC_STATUS_SUCCESS);
  }

  UBSEC_RESET_DEVICE(pDevice);

  /* Complete pending events that have not completed*/
  FlushDevice(pDevice,UBSEC_STATUS_CANCELLED,FLUSH_ALL);
  pDevice->Status=UBSEC_STATUS_SUCCESS;

#ifndef POLL
  /* Reenable interrupts */
    UBSEC_ENABLE_INT(pDevice);
#endif

#ifdef UBSEC_STATS
  RTL_MemZero(&pDevice->Statistics,sizeof(pDevice->Statistics));
#endif	

  OS_LeaveCriticalSection(pDevice,SaveConfig);
  return(UBSEC_STATUS_SUCCESS);
}








