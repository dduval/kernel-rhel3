
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
 * ubskey.c: Public Key commands are handled by this module
 *
 *
 * Revision History:
 *
 * 01/03/2000 PW Created
 * 07/06/2000 DPA Fixes for SMP operation
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/20/2001 RJT Added support for CPU-DMA memory synchronization
 * 04/24/2001 DPA Allow for unnormalize of D-H random number (x) output for BCM5805
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

void significant_bits(ubsec_LongKey_pt key, int native_endianess);

/*
 * ubsec_KeyCommand: Process a list of Cipher commands.
 *
 * Immediate Status is returned. Completion status is returned
 * on a per command callback
 */
ubsec_Status_t 
ubsec_KeyCommand(ubsec_DeviceContext_t Context,
	      ubsec_KeyCommandInfo_pt pCommand,
	      int *NumCommands)
{

#ifdef UBSEC_PKEY_SUPPORT
  DeviceInfo_pt 		pDevice=(DeviceInfo_pt)Context;
  VOLATILE MasterCommand_t  	*pMCR;
  VOLATILE Packet_t         	*pPacket;
  VOLATILE KeyContext_t  	*pContext;
  VOLATILE int             	PacketIndex;
  int 				CommandIndex=0;
  int 				CommandCount=*NumCommands;
  ubsec_Status_t 		Status;
  UBS_UINT32 			SaveConfig;
  CallBackInfo_pt pCompletionContext;

  if (!(UBSEC_IS_KEY_DEVICE(pDevice))) {
    Dbg_Print(DBG_FATAL,( "ubsec: KEY Command for a crypto device %x\n ",pDevice->DeviceID ));
    return(UBSEC_STATUS_NO_DEVICE );
  }


  Dbg_Print(DBG_KEY,( "ubsec:  Key command %d ",*NumCommands ));
  /*
   * Check some parameters
   */    
  if(pDevice==NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,( "NO DEV\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
  Dbg_Print(DBG_KEY,( "\n"));

  if (OS_EnterCriticalSection(pDevice,SaveConfig)) {
    return(UBSEC_STATUS_DEVICE_BUSY);
  }

  /* Get the next MCR to load */
 Get_New_MCR:
  *NumCommands=CommandIndex; /* Update number completed */


  if ((pMCR=GetFreeMCR(pDevice,UBSEC_KEY_LIST,&Status))== NULL_MASTER_COMMAND)
    goto Error_Return;

  /* Add packets to this MCR. */

  Dbg_Print(DBG_KEY,( "ubsec: mcr_index %d MCR <%0x,%0x>\n",pMCR->Index,pMCR,pMCR->MCRPhysicalAddress));
  /* Initialize the packet information */
  PacketIndex = pMCR->NumberOfPackets; 
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */
  pContext = pMCR->KeyContextList[PacketIndex]; 
  Status=UBSEC_STATUS_SUCCESS; /* Wishful thinking? */

  Dbg_Print(DBG_KEY,( "ubsec: PacketIndex %d \n",pMCR->NumberOfPackets));

  /* Process all the commands in the command list. */
  for (; CommandIndex < CommandCount ; CommandIndex++) { /* Add all the packets to the MCR*/
    if( PacketIndex >= MCR_MAXIMUM_PACKETS ) {
      Dbg_Print(DBG_KEY,( "ubsec:  overran mcr buffer. %d\n",PacketIndex,CommandIndex ));
      /* 
       * We have filled this MCR. 
       * Advance next free. Wrap around if necessary
       */
      pDevice->NextFreeMCR[UBSEC_KEY_LIST]=(MasterCommand_pt)pMCR->pNextMCR;
      Dbg_Print(DBG_KEY,( "ubsec:  PushMCR ..." ));
      PushMCR(pDevice); /* Get it going (pipeline) */
      goto Get_New_MCR; /* Try to add to the next MCR */
    }
	pContext = pMCR->KeyContextList[PacketIndex]; 

    pCompletionContext=(CallBackInfo_pt)&pMCR->CompletionArray[PacketIndex];

    /* Now set up the packet processing parameters */
    Dbg_Print(DBG_KEY,( "ubsec: packet_Index %d, Context Buf <%0x,%0x>\n",PacketIndex,pContext,pContext->PhysicalAddress ));
    pPacket->PacketContextBuffer=pContext->PhysicalAddress;
    
    switch (pCommand->Command) {
      case UBSEC_DH_PUBLIC:
	Dbg_Print(DBG_KEY,( "ubsec: UBSEC_DH_PUBLIC\n" ));
	pContext->cmd_structure_length= DH_STATIC_SEND_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_DH_PUBLIC; /* send mode for DH */
	Status=DH_SetupPublicParams((MasterCommand_pt)pMCR,&pCommand->Parameters.DHParams);

	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.DHPublicCount++;
#endif
        break;

      case UBSEC_DH_SHARED:
	Dbg_Print(DBG_KEY,( "ubsec: UBSEC_DH_SHARED\n" ));
	pContext->cmd_structure_length= DH_STATIC_REC_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_DH_SHARED; /* send mode for DH */
	Status=DH_SetupSharedParams((MasterCommand_pt)pMCR,&pCommand->Parameters.DHParams);
	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.DHSharedCount++;
#endif
	break;

      case UBSEC_RSA_PUBLIC:
	pContext->cmd_structure_length=RSA_STATIC_PUBLIC_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_RSA_PUBLIC; /* send mode for DH */
	Status=RSA_SetupPublicParams((MasterCommand_pt)pMCR,&pCommand->Parameters.RSAParams);
	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.RSAPublicCount++;
#endif
	break;

      case UBSEC_RSA_PRIVATE:
	pContext->cmd_structure_length=RSA_STATIC_PRIVATE_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_RSA_PRIVATE; /* send mode for DH */
	Status=RSA_SetupPrivateParams((MasterCommand_pt)pMCR,&pCommand->Parameters.RSAParams);
	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.RSAPrivateCount++;
#endif
	break;

      case UBSEC_DSA_SIGN:
	pContext->cmd_structure_length= DSA_STATIC_SIGN_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_DSA_SIGN; /* send mode for DH */
	Status=DSA_SetupSignParams((MasterCommand_pt)pMCR,&pCommand->Parameters.DSAParams);
	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.DSASignCount++;
#endif
	break;

      case UBSEC_DSA_VERIFY:
	pContext->cmd_structure_length= DSA_STATIC_VERIFY_CONTEXT_SIZE;
	pContext->operation_type	= OPERATION_DSA_VERIFY; /* send mode for DH */

	Status=DSA_SetupVerifyParams((MasterCommand_pt)pMCR,&pCommand->Parameters.DSAParams);
	if (Status != UBSEC_STATUS_SUCCESS)
	  goto Error_Return;
#ifdef UBSEC_STATS
	pDevice->Statistics.DSAVerifyCount++;
#endif
	break;

    default:
      return(UBSEC_STATUS_INVALID_PARAMETER);
    }

    /* Save the user callback information. We use an intermediate callback for LongKey numbers */

    /* Always save the user callback parameters (to be called by the intermediate callback) */
    pContext->UserCallback = pCommand->CompletionCallback;
    pContext->UserContext = pCommand->CommandContext;
    /* The intermediate callback needs to get this command context passed to it */
    pCompletionContext->CompletionCallback = KeyFinishResult;
    pCompletionContext->CommandContext = (unsigned long)pContext;


#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 
    pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(pContext->cmd_structure_length);
#endif

    /* For key MCRs, contexts are accessed by an array of handles.        */
    /* This means that memory for each context was separately allocated.  */
    /* Therefore we must sync each context separately as it is built.     */
    Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_KeyCommand() Sync Context to Device (0x%08X,%d,%d)\n", pMCR->ContextListHandle[PacketIndex],
		       0,
		       CTRL_TO_CPU_SHORT(pContext->cmd_structure_length)));
    OS_SyncToDevice(pMCR->ContextListHandle[PacketIndex],
		    0,
		    CTRL_TO_CPU_SHORT(pContext->cmd_structure_length));

    /* Now inc the number of packets and prepare for the next command. */
    pMCR->NumberOfPackets++;
    pCommand++;
    PacketIndex++;
    pPacket++;
#ifdef UBSEC_STATS
    pDevice->Statistics.IKECount++;
#endif
  } /* For NumCommands-- */

  /*
   * If we are here then the MCR is built.
   * Push it to the device. 
   */
  *NumCommands=CommandIndex; /* Update number completed */
  PushMCR(pDevice);

#ifdef BLOCK 
  /* Wait for all outstanding  to complete */
    while ((Status=WaitForCompletion(pDevice,(UBS_UINT32)1000000,UBSEC_KEY_LIST))
	   == UBSEC_STATUS_SUCCESS);
    if (Status!=UBSEC_STATUS_TIMEOUT) /* We are nested, return success */
      Status=UBSEC_STATUS_SUCCESS;
 Error_Return:
#else

 Error_Return:  /* Label to make sure that IRQs are enabled. */

#ifdef COMPLETE_ON_COMMAND_THREAD
    ubsec_PollDevice(pDevice);  /* Try to complete some & cut down on ints */
#endif

#endif
    OS_LeaveCriticalSection(pDevice,SaveConfig);

#ifdef UBSEC_STATS
		if (Status != UBSEC_STATUS_SUCCESS)
			pDevice->Statistics.IKEFailedCount++;
#endif

    return(Status);
#else /* Support */
    return(UBSEC_STATUS_NO_DEVICE);
#endif
}


/*
 * Intermediate callback routine to finish/unshift "key" (multi-precision integers) results data.
 * This routine counts significant bits in the LongKey result(s). It also adjusts keys to CPU native 
 * endianess (if needed) and/or un-normalizes keys (if needed)
 */
void KeyFinishResult(unsigned long Context,ubsec_Status_t Result)
{
  KeyContext_pt pContext=(KeyContext_pt)Context;
  long NormBits_RNG;
  int key_num, element_num, native_endianess;
  UBS_UINT32 *key_array;

  /* We are assuming that the KeyLength field value passed in refers to a key array element that
     is not less significant (lower array index) than the actual most significant array element,
     and not so large as to exceed the allocated space for the KeyValue array */

  if (Result==UBSEC_STATUS_SUCCESS) {

#if defined(UBS_OVERRIDE_LONG_KEY_MODE)
    native_endianess = 0; /* Key arrays NOT returned in the CPU's native endianess */
#else
    native_endianess = 1; /* Key arrays returned in the CPU's native endianess */
#endif /* Returned endianess of the key arrays */

    /* Process one or both of the ResultKey arrays */
    for (key_num=0 ; key_num<2 ; key_num++) {
      if ((pContext->ResultKey[key_num] == NULL) || \
	  (pContext->ResultKey[key_num]->KeyValue == NULL) || \
	  (pContext->ResultKey[key_num]->KeyLength == 0))
	continue;

      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: KeyFinishResult Sync Output Frag #%d to CPU (0x%08X,%d,%d)\n",
				key_num+1, 
				pContext->ResultKey[key_num]->KeyValue,
				0,
				ROUNDUP_TO_32_BIT(pContext->ResultKey[key_num]->KeyLength)/8));
      OS_SyncToCPU(pContext->ResultKey[key_num]->KeyValue,
		   0,
		   ROUNDUP_TO_32_BIT(pContext->ResultKey[key_num]->KeyLength)/8);

#if defined(UBS_ENABLE_KEY_SWAP) 
      key_array = (UBS_UINT32 *)OS_GetVirtualAddress(pContext->ResultKey[key_num]->KeyValue);
      for (element_num=0; element_num<((pContext->ResultKey[key_num]->KeyLength+31)/32) ; element_num++)
	key_array[element_num] = BYTESWAPLONG(key_array[element_num]);
#endif /* UBS_ENABLE_KEY_SWAP */
 
#ifndef UBSEC_HW_NORMALIZE
      if (pContext->NormBits) 
	ubsec_ShiftData(pContext->ResultKey[key_num], -pContext->NormBits);
#endif /* UBSEC_HW_NORMALIZE */

      significant_bits(pContext->ResultKey[key_num], native_endianess);

    } /* for each ResultKey[key_num] to be finished */

    /* Now process the RNG key array result (if present) */
    if ((pContext->ResultRNG != NULL) && \
	(pContext->ResultRNG->KeyValue != NULL) && \
	pContext->ResultRNG->KeyLength ) {

      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: KeyFinishResult Sync RNG Frag to CPU (0x%08X,%d,%d)\n",
				pContext->ResultRNG->KeyValue,
				0,
				ROUNDUP_TO_32_BIT(pContext->ResultRNG->KeyLength)/8));
      OS_SyncToCPU(pContext->ResultRNG->KeyValue,
		   0,
		   ROUNDUP_TO_32_BIT(pContext->ResultRNG->KeyLength)/8);

#if defined(UBS_ENABLE_KEY_SWAP) 
      key_array = (UBS_UINT32 *)OS_GetVirtualAddress(pContext->ResultRNG->KeyValue);
      for (element_num=0; element_num<((pContext->ResultRNG->KeyLength+31)/32) ; element_num++)
	key_array[element_num] = BYTESWAPLONG(key_array[element_num]);
#endif /* UBS_ENABLE_KEY_SWAP */

#ifndef UBSEC_HW_NORMALIZE
      if ((pContext->operation_type == OPERATION_DH_PUBLIC) && pContext->CtxCmdBuf.DH_Send_CtxCmdBuf.rng_enable) {
	NormBits_RNG = pContext->CtxCmdBuf.DH_Send_CtxCmdBuf.private_key_length & 0x1f;
	if (NormBits_RNG) {
	  ubsec_ShiftData(pContext->ResultRNG, NormBits_RNG - 32);
	}
      }
#endif /* UBSEC_HW_NORMALIZE */
      significant_bits(pContext->ResultRNG, native_endianess);
    }

  } /* Result == UBSEC_STATUS_SUCCESS */

  if (pContext->UserCallback) 
    (*pContext->UserCallback)( pContext->UserContext,Result);
}


void significant_bits(ubsec_LongKey_pt key, int native_endianess)
{
  int element_num;
  UBS_UINT32 element, *key_array;

  if ((key == NULL) || !key->KeyLength) {
    Dbg_Print(DBG_FATAL,("significant_bits: invalid/unworkable input\n"));
    return;
  }

  /* Round up to next 32-bit multiple */
  key->KeyLength = (key->KeyLength + 31) & ~31;

  /* Start search at appropriate 32-bit array element */
  key_array = (UBS_UINT32 *)OS_GetVirtualAddress(key->KeyValue);
  element_num = (key->KeyLength / 32) - 1; 

  do {
    if (key_array[element_num] == 0) {
      key->KeyLength -= 32;
      continue;
    }
    else {
      if (native_endianess) 
	element = key_array[element_num]; /* Key array in CPU's native endianess */
      else
	element = BYTESWAPLONG(key_array[element_num]); /* Key array NOT in CPU's native endianess */
      while (!(element & 0x80000000)) {
	element = element << 1;
	key->KeyLength--;
      }
      break;
    }
  } while (--element_num >= 0);
  return;
}





