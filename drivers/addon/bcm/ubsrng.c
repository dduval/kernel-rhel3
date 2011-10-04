
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
 * ubsrng.c: Random Number Generator Code
 */

/*
 * Revision History:
 *
 * 03/17/2000 SOR Created
 * 07/06/2000 DPA Fixes for SMP operation
 * 04/20/2001 RJT Added support for CPU-DMA memory synchronization
 */

#include "ubsincl.h"

void RNGFinishResult(unsigned long Context,ubsec_Status_t Result);

/*
 * ubsec_rngCommand: Process a list of rng commands.
 *
 * Immediate Status is returned. Completion status is returned
 * on a per command callback
 */
ubsec_Status_t 
ubsec_RNGCommand(ubsec_DeviceContext_t Context,
	      ubsec_RNGCommandInfo_pt pCommand,
	      int *NumCommands)
{
#ifdef UBSEC_RNG_SUPPORT
  DeviceInfo_pt 		pDevice=(DeviceInfo_pt)Context;
  VOLATILE MasterCommand_t  	*pMCR;
  VOLATILE Packet_t         	*pPacket;
  VOLATILE KeyContext_t  	*pContext;
  VOLATILE int             	PacketIndex;
  int 				CommandIndex=0;
  int 				CommandCount=*NumCommands;
  ubsec_Status_t 		Status;
  unsigned long 		SaveConfig;
  ubsec_RNGCommandParams_pt    pParams;
  VOLATILE DataBufChainList_t  *FragPtr;
  int                           DataLength;

  if (!UBSEC_IS_KEY_DEVICE(pDevice)) {
    Dbg_Print(DBG_FATAL,( "ubsec: RNG Command for a crypto device\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }

  Dbg_Print(DBG_RNG,( "ubsec:  Rng command %d ",*NumCommands ));
  /*
   * Check some parameters
   */    
  if(pDevice==NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,( "NO DEV\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }

  Dbg_Print(DBG_RNG,( "\n"));

  if (OS_EnterCriticalSection(pDevice,SaveConfig)) {
    return(UBSEC_STATUS_DEVICE_BUSY);
  }

  /* Get the next MCR to load */
 Get_New_MCR:
  *NumCommands=CommandIndex; /* Update number completed */

  if ((pMCR=GetFreeMCR(pDevice,UBSEC_KEY_LIST,&Status))== NULL_MASTER_COMMAND) 
    goto Error_Return;

  /* Add packets to this MCR. */

  Dbg_Print(DBG_RNG,( "ubsec: mcr_index %d MCR <%0x,%0x>\n",pMCR->Index,pMCR,pMCR->MCRPhysicalAddress));
  /* Initialize the packet information */
  PacketIndex = pMCR->NumberOfPackets; 
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */
  pContext = pMCR->KeyContextList[PacketIndex]; 

#if 0
  RTL_MemZero(pContext,sizeof(*pContext));
#endif

  Status=UBSEC_STATUS_SUCCESS; /* Wishful thinking? */

  Dbg_Print(DBG_RNG,( "ubsec: PacketIndex %d \n",pMCR->NumberOfPackets));

  /* Process all the commands in the command list. */
  for (; CommandIndex < CommandCount ; CommandIndex++) { /* Add all the packets to the MCR*/
    if( PacketIndex >= MCR_MAXIMUM_PACKETS ) {
      Dbg_Print(DBG_RNG,( "ubsec:  overran mcr buffer. %d\n",PacketIndex,CommandIndex ));
      /* 
       * We have filled this MCR. 
       * Advance next free. Wrap around if necessary
       */
      pDevice->NextFreeMCR[UBSEC_KEY_LIST]=(MasterCommand_pt)pMCR->pNextMCR;
      Dbg_Print(DBG_RNG,( "ubsec:  PushMCR ..." ));
      PushMCR(pDevice); /* Get it going (pipeline) */
      goto Get_New_MCR; /* Try to add to the next MCR */
    }

    /* First set up the command type and parameters. */
    Dbg_Print(DBG_RNG,( "ubsec: packet_Index %d, Context Buf <%0x,%0x>\n",PacketIndex,pContext,pContext->PhysicalAddress ));
    pPacket->PacketContextBuffer=pContext->PhysicalAddress;
    
    switch (pCommand->Command) {
    case UBSEC_RNG_DIRECT:
      pContext->operation_type	= OPERATION_RNG_DIRECT;
      break;
    case UBSEC_RNG_SHA1:
      pContext->operation_type	= OPERATION_RNG_SHA1;
      break;

    default:
      Status=(UBSEC_STATUS_INVALID_PARAMETER);
      goto Error_Return;
    }

    pParams=&pCommand->Parameters;
    pContext->cmd_structure_length= RNG_STATIC_CONTEXT_SIZE;

    /* Now do the Output data buffer. All operations have exactly one */
    FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)(OS_GetPhysicalAddress(pParams->Result.KeyValue)) ); 
    DataLength=(pParams->Result.KeyLength+7)/8; /* map the length from bits to bytes */
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    FragPtr->pNext=0; /* Terminate the input fragment descriptor list. */

#ifdef UBSDBG
  /* Print out the context and fragment information if required */
  {
    Dbg_Print(DBG_RNG,(   "ubsec:  ---- RNG Context: Len %d Operation %x\n",
	  pContext->cmd_structure_length,pContext->operation_type));
    Dbg_Print(DBG_RNG,( "OutputFragment: <%d, %08x, (%08x,Next-%08x)>\n",
      DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));
  }
#endif


#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
    if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
      Dbg_Print(DBG_FATAL,("ubsec:MATH #########INVALID OUTPUT ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto Error_Return;
    }
    if ((DataLength) & 0x03) {
      Dbg_Print(DBG_FATAL,("ubsec:MATH #########INVALID OUTPUT LENGTH %08x\n", DataLength)); 
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto Error_Return;
    }
#endif 

    /* Save the callback information. */

    pContext->ResultRNG = &pParams->Result; /* Save here for post-command processing */
    pContext->ResultKey[0]=NULL; /* Not used */
    pContext->ResultKey[1]=NULL; /* Not used */
    pContext->UserCallback = pCommand->CompletionCallback;
    pContext->UserContext = pCommand->CommandContext;

    /* The intermediate callback needs to get this command context passed to it */
    pMCR->CompletionArray[PacketIndex].CompletionCallback = RNGFinishResult;
    pMCR->CompletionArray[PacketIndex].CommandContext = (unsigned long)pContext;


#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 
    pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(pContext->cmd_structure_length);
#endif

    /* For key (RNG) MCRs, contexts are accessed by an array of handles.  */
    /* This means that memory for each context was separately allocated.  */
    /* Therefore we must sync each context separately as it is built.     */
    /* Since the CryptoNet RNG function only reads the first 32-bit word  */
    /* of the context, we only need to sync the first four bytes.         */
    Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_RNGCommand Sync Context to Device (0x%08X,%d,%d)\n", pMCR->ContextListHandle[PacketIndex],0,4));
    OS_SyncToDevice(pMCR->ContextListHandle[PacketIndex],0,4);

    /* Now inc the number of packets and prepare for the next command. */
    pMCR->NumberOfPackets++;
    pCommand++;
    PacketIndex++;
    pPacket++;
  } /* For NumCommands-- */

  /*
   * If we are here then the MCR is built.
   * Push it to the device. 
   */
  *NumCommands=CommandIndex; /* Update number completed */
  PushMCR(pDevice);

#ifdef BLOCK 
  /* Wait for all outstanding  to complete */
    while ((Status=WaitForCompletion(pDevice,(unsigned long)1000000,UBSEC_KEY_LIST))
	   == UBSEC_STATUS_SUCCESS);
    if (Status!=UBSEC_STATUS_TIMEOUT) /* We are nested, return success */
      Status=UBSEC_STATUS_SUCCESS;
 Error_Return:
#else /* Not BLOCKing */

 Error_Return:  /* Label to make sure that IRQs are enabled. */
#ifdef COMPLETE_ON_COMMAND_THREAD
    ubsec_PollDevice(pDevice);  /* Try to complete some & cut down on ints */
#endif 

#endif /* BLOCK */
    OS_LeaveCriticalSection(pDevice,SaveConfig);
    return(Status);
#else /* UBSEC_RNG_SUPPORT not defined */
    return(UBSEC_STATUS_NO_DEVICE);
#endif /* UBSEC_RNG_SUPPORT */
}


/*
 * Intermediate callback routine to finish a Random number "key" (multi-precision integer).
 * This routine zeros out the un-asked-for random bits (the chip generates them in groups of 8).
 */
void RNGFinishResult(unsigned long Context,ubsec_Status_t Result)
{
  KeyContext_pt pContext=(KeyContext_pt)Context;
  UBS_UINT32 rng_mask, *key_array;

  if ((Result==UBSEC_STATUS_SUCCESS) && (pContext->ResultRNG != NULL)) {
    
  /* The KeyLength field value passed in refers to the desired number of bits */

    /* Chop off the un-asked-for bits so that the RNG array makes sense to the CPU */
#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE)
    rng_mask = 0xFFFFFFFF << (32 - (pContext->ResultRNG->KeyLength & 31));  
#else
    rng_mask = 0xFFFFFFFF >> (32 - (pContext->ResultRNG->KeyLength & 31)); 
#endif 
    
    ((UBS_UINT32 *)OS_GetVirtualAddress(pContext->ResultRNG->KeyValue))[(pContext->ResultRNG->KeyLength-1)/32] &= rng_mask;

  } /* Result == UBSEC_STATUS_SUCCESS */

  if (pContext->UserCallback) 
    (*pContext->UserCallback)( pContext->UserContext,Result);
}

