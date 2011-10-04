
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
 * ubsint.c: Ubsec interrupt processing functions.
 *
 * ubsec Interrupt service routine. Most OS's (at least the multitasking ones
 * treat interrupt processing as a two stage process. One is to servce the IRQ
 * delete the interrupt condition and schedule a callback to do the bulk processing
 * at a lower priority time.
 *
 * The second process is to do the bulk processing itself. This module handles that
 * case. In the case that all int processing can be done at IRQ time then scheduling
 * the callback can call the bulk processing call directly
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 12/02/1999 DWP Modified to handle Big Endian architecture
 * 07/06/2000 DPA Fixes for SMP operation
 * 04/23/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port.
 */

#include "ubsincl.h"

/*
 * ubsec_ISR
 * 
 * Called when interrupt occurs. If interrupt is from us, disable
 * further interrupts and return an indication that the interupt
 * came from us. Otherwise just return 0.
 */
long 
ubsec_ISR(ubsec_DeviceContext_t Context)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  VOLATILE UBS_UINT32 InterruptCondition=0;    

  Dump_Registers(pDevice,DBG_IRQ);
  
  if (UBSEC_IRQ_ENABLED(pDevice)) {
    InterruptCondition=(UBSEC_READ_STATUS(pDevice) & pDevice->IntAckMask);
    if (InterruptCondition) {  
      /* Disable the interrupt mask on device and ... */
      UBSEC_DISABLE_INT(pDevice);

	 UBSEC_ACK_CONDITION(pDevice,InterruptCondition);
      if (InterruptCondition & DMA_ERROR ) {
	Dump_Registers(pDevice,DBG_FATAL);
	pDevice->Status=UBSEC_STATUS_DEVICE_FAILED;
      }

      if (InterruptCondition & MCR1_DONE ) {
	Dbg_Print(DBG_IRQ,( "ubsec:  irq handler mcr1 done\n" ));
      }
      else if (InterruptCondition & MCR1_ALL_EMPTY ) {
	Dbg_Print(DBG_IRQ,( "ubsec:  irq handler mcr1 empty\n" ));
      }
      if (InterruptCondition & MCR2_DONE ) {
	Dbg_Print(DBG_IRQ,( "ubsec:  irq handler mcr2 done\n" ));
      }
      else if (InterruptCondition & MCR2_ALL_EMPTY ) {
	Dbg_Print(DBG_IRQ,( "ubsec:  irq handler mcr2 empty\n" ));
      }

      Dump_Registers(pDevice,DBG_IRQ);
      OS_ScheduleCallBack(ubsec_ISRCallback,pDevice);
    }
  }
 return(InterruptCondition);	

}

/*
 * ubsec_ISRCallback:
 *
 * This routine is scheduled at ISR time to do batch
 * processing for the device.
 * 
 * Interrupts are also reenabled for the device.
 */
void
ubsec_ISRCallback(ubsec_DeviceContext_t Context)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  unsigned long InterruptCondition;
  unsigned long SaveConfig;

  /* Reread the latest status. */
  Dbg_Print(DBG_TEST,("ubsec_ISRCallback Entered\n"));
  InterruptCondition=(UBSEC_READ_STATUS(pDevice) & pDevice->IntAckMask);
  /* Now ack the interrupt condition. */
  UBSEC_ACK_CONDITION(pDevice,InterruptCondition);

#ifndef COMPLETE_ON_COMMAND_THREAD
  ubsec_PollDevice(pDevice); /* Always poll for good completions */
#endif
  if (InterruptCondition & DMA_ERROR) 
     pDevice->Status=UBSEC_STATUS_DEVICE_FAILED;
  if (pDevice->Status!=UBSEC_STATUS_SUCCESS)  {
    Dbg_Print(DBG_FATAL,( "CryptoNet:  DMA Reset\n"));	  
#ifdef UBSEC_STATS
      pDevice->Statistics.DMAErrorCount++;
#endif
    FlushDevice(pDevice,pDevice->Status,FLUSH_ONLY_PUSHED);
    UBSEC_RESET_DEVICE(pDevice);
    pDevice->Status=UBSEC_STATUS_SUCCESS;
  }
  /*
   * Check to see if more need to be pushed to the device.
   */
  if (OS_TestCriticalSection(pDevice,SaveConfig)==0) {
    PushMCR(pDevice);
#ifdef COMPLETE_ON_COMMAND_THREAD
    ubsec_PollDevice(pDevice); /* Always poll for good completions */
#endif
    OS_LeaveCriticalSection(pDevice,SaveConfig);
  }

#ifndef POLL
  /* reenable interrupts */
  UBSEC_ENABLE_INT(pDevice);
#endif

}


/*
 * ubsec_PollDevice:
 *
 * This routine is used to poll a device for command completion.
 * Commands will be completed (callbacks called) from within
 * this call.
 *
 * Returns UBSEC_STATUS_SUCCESS
 */
ubsec_Status_t
ubsec_PollDevice(ubsec_DeviceContext_t Context)
{
DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
VOLATILE MasterCommand_t *pMCR;
VOLATILE int j;
VOLATILE CallBackInfo_t *pCallBack;
int NumberOfPackets;
unsigned long MCRListIndex;
ubsec_Status_t  Status=UBSEC_STATUS_SUCCESS;

Dbg_Print(DBG_IRQ,( "ubsec completion handler \n" ));

pDevice->InCriticalSection++;
     
  /* While there are MCRs to complete, do it. */
for (MCRListIndex=0; MCRListIndex < pDevice->NumberOfMCRLists ;MCRListIndex++) {
  for (;;) {
     pMCR = pDevice->NextDoneMCR[MCRListIndex];
     Dbg_Print(DBG_MCR_SYNC,( "ubsec: ubsec_PollDevice() Sync Flags to CPU (0x%08X,%d,%d)\n",pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,4));
     OS_SyncToCPU(pMCR->MCRMemHandle,
		  pMCR->MCRMemHandleOffset,4); /* Just need to sync flags */
      /* Now check to see if there are any to complete */
     if (pMCR->Flags & MCR_FLAG_COMPLETION)  {
#ifdef UBSDBG
/* Debug code for 5805 key operation. */
     if (UBSEC_IS_KEY_DEVICE(pDevice) && (MCRListIndex)) {
         if (pMCR->Flags & 0x02)
	    Dbg_Print(DBG_CMD_FAIL,( "ubsec:  Command Fail %x\n", pMCR->Flags));	  
	}
#endif
	pMCR->Flags = 0; /* Do it here for reentrancy issues */
	/* unload the packets */
	NumberOfPackets = CTRL_TO_CPU_SHORT( pMCR->NumberOfPackets );
	Dbg_Print(DBG_CMD,( "ubsec:  Complete MCR %x with %d packets \n",
			    pMCR, NumberOfPackets ));
	pCallBack = &pMCR->CompletionArray[0];
	for( j = 0; j < NumberOfPackets; j++ ) {
	  if( pCallBack->CompletionCallback) {
	    (*pCallBack->CompletionCallback)( pCallBack->CommandContext,UBSEC_STATUS_SUCCESS);
	    pCallBack->CompletionCallback=0;
	  }
	  pCallBack++;
	}
	pMCR->NumberOfPackets = 0; /* This frees it up */
	pMCR->MCRState = MCR_STATE_FREE; /* Set it to free */
	pDevice->NextDoneMCR[MCRListIndex] = (MasterCommand_pt)pMCR->pNextMCR;
      }
      else {
	break;
      }
  }
}
    /* MCRListIndex for loop */

 Dump_Registers(pDevice,DBG_IRQ);

 pDevice->InCriticalSection--;
 return(Status);
}

/*
 * Disable interrupts and return interrupt mask status
 */
unsigned long
ubsec_DisableInterrupt(ubsec_DeviceContext_t Context)
{
DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
UBS_UINT32 saveval;

saveval=(UBSEC_READ_STATUS(pDevice) & pDevice->IntAckMask);
UBSEC_DISABLE_INT(pDevice);
return(saveval);
}

/* 
 * Enable device interrupts
 *
 * Return success
 */
ubsec_Status_t
ubsec_EnableInterrupt( ubsec_DeviceContext_t Context)
{
#ifndef POLL
DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
UBSEC_ENABLE_INT(pDevice);
#endif
return(UBSEC_STATUS_SUCCESS);
}





