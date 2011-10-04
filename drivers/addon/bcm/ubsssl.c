
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
 * ubsssl.c: SSL/TLS/ARC4 commands are handled by this module
 */

/*
 * Revision History:
 *
 * 10/xx/2000 SOR Created
 * 04/19/2001 RJT Added support for CPU-DMA memory synchronization
 * 10/09/2001 SRM 64 bit port.
 */

#include "ubsincl.h"


/*
 * ubsec_SSLCommand: Process a list of Cipher commands.
 *
 * Immediate Status is returned. Completion status is returned
 * on a per command callback
 */
ubsec_Status_t 
ubsec_SSLCommand(ubsec_DeviceContext_t Context,
	      ubsec_SSLCommandInfo_pt pCommand,
	      int *NumCommands)
{
#ifdef UBSEC_SSL_SUPPORT
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  VOLATILE MasterCommand_t   *pMCR;
  VOLATILE Packet_t          *pPacket;
  VOLATILE PacketContext_t   *pContext;
  VOLATILE SSL_MACContext_t  *pSSLMACContext;
  VOLATILE TLS_HMACContext_t *pTLSHMACContext;
  VOLATILE SSL_CryptoContext_t *pSSLCryptoContext;
  VOLATILE ARC4_CryptoContext_t *pARC4Context;
  VOLATILE Hash_Context_t *pHashContext;
  int i;
  long *plong;
  VOLATILE int             PacketIndex;

  int CommandIndex=0;
  int CommandCount=*NumCommands;
  ubsec_Status_t Status;
  unsigned long SaveConfig;
  ubsec_FragmentInfo_pt pExtraFragment=(ubsec_FragmentInfo_pt) 0;

  Dbg_Print(DBG_CMD,( "ubsec:  SSL command %d",*NumCommands ));
  /*
   * Check some parameters
   */    
  if(pDevice==NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,( "NO DEV\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
  Dbg_Print(DBG_CMD,( "\n"));

  if (!(UBSEC_IS_SSL_DEVICE(pDevice))) {
    Dbg_Print(DBG_FATAL,( "ubsec: SSL Command for a non SSL device %x \n ",pDevice->DeviceID ));
    return(UBSEC_STATUS_NO_DEVICE );
  }

  /*  SaveConfig=OS_EnterCriticalSection(pDevice); */
  OS_EnterCriticalSection(pDevice, SaveConfig);

  /* Get the next MCR to load */
 Get_New_MCR:
  *NumCommands=CommandIndex; /* Update number completed */
  if ((pMCR=GetFreeMCR(pDevice,UBSEC_CIPHER_LIST,&Status))== NULL_MASTER_COMMAND) {
    Dbg_Print(DBG_CMD_FAIL,("ubsec: device busy MCR %x\n",Status));
    goto Error_Return;
  }

  /* Add packets to this MCR. */

  Dbg_Print(DBG_CMD,( "ubsec: mcr_index %d MCR %0x\n",pMCR->Index,pMCR));
  /* Initialize the packet information */

  PacketIndex = pMCR->NumberOfPackets; 
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */
  pContext = &pMCR->ContextList[PacketIndex]; 
  Status=UBSEC_STATUS_SUCCESS; /* Wishful thinking? */

  /* Process all the commands in the command list. */
  for (; CommandIndex < CommandCount ; CommandIndex++) { /* Add all the packets to the MCR */
    if( PacketIndex >= MCR_MAXIMUM_PACKETS ) {
      Dbg_Print(DBG_CMD,( "ubsec:  overran mcr buffer. %d\n",PacketIndex,CommandIndex ));
      /* 
       * We have filled this MCR. 
       * Advance next free. Wrap around if necessary
       */
      pDevice->NextFreeMCR[0]=(MasterCommand_pt) pMCR->pNextMCR;
      PushMCR(pDevice); /* Get it going (pipeline) */
      goto Get_New_MCR; /* Try to add to the next MCR */
    }
    
    pPacket->PacketContextBuffer=pContext->PhysicalAddress; 
    
    /* Save the callback information. */
    pMCR->CompletionArray[PacketIndex].CompletionCallback = pCommand->CompletionCallback;
    pMCR->CompletionArray[PacketIndex].CommandContext = pCommand->CommandContext;

    /* Now set up the packet processing parameters */
    Dbg_Print(DBG_PACKET,( "ubsec: packet_Index %d, Context Buf %0x\n",PacketIndex,pContext ));

        /* Now setup the particular context */

    pExtraFragment=(ubsec_FragmentInfo_pt) 0;
    switch (UBSEC_SSL_COMMAND(pCommand->Command)) {


    case UBSEC_SSL_MAC:
      pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(SSLMAC_CONTEXT_SIZE);
      pContext->operation_type=OPERATION_SSL_MAC;
      pCommand->NumDestination=0; /* Make sure */

      pSSLMACContext=&pContext->Context.SSL_Mac;
      RTL_MemZero(pSSLMACContext,sizeof(*pSSLMACContext));

#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
      pSSLMACContext->SequenceHigh=pCommand->Parameters.SSLMACParams.SequenceNumber.HighWord;
      pSSLMACContext->SequenceLow=pCommand->Parameters.SSLMACParams.SequenceNumber.LowWord;
      RTL_Memcpy(&pSSLMACContext->HMACKey[0],&pCommand->Parameters.SSLMACParams.key[0],20);
#else
      pSSLMACContext->SequenceHigh=BYTESWAPLONG(pCommand->Parameters.SSLMACParams.SequenceNumber.HighWord);
      pSSLMACContext->SequenceLow=BYTESWAPLONG(pCommand->Parameters.SSLMACParams.SequenceNumber.LowWord);
      copywords((UBS_UINT32 *)&pSSLMACContext->HMACKey[0],
		(UBS_UINT32 *)&pCommand->Parameters.SSLMACParams.key[0],5);
#endif

      pSSLMACContext->DataLength=CPU_TO_CTRL_SHORT(pCommand->Parameters.SSLMACParams.DataLength);
      pSSLMACContext->ContentType=pCommand->Parameters.SSLMACParams.ContentType;

      /*
      for (i=0,plong=(long *)&pSSLMACContext->HMACPad; i <SSL_MAC_PAD_LENGTH_LONG; i++)
	*plong++=SSL_MAC_PAD_VALUE_LONG;
      */
      RTL_Memset((unsigned char*)pSSLMACContext->HMACPad, 0x36, 48);

      pExtraFragment=&pCommand->Parameters.HashParams.OutputHMAC;
      if( UBSEC_MAC_MD5 & pCommand->Command )
	pSSLMACContext->CryptoFlag = CF_MD5;
      else if( UBSEC_MAC_SHA1 & pCommand->Command )
	pSSLMACContext->CryptoFlag = CF_SHA1;
      
      break;

    case UBSEC_TLS: 
      pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(TLSHMAC_CONTEXT_SIZE);
      pContext->operation_type=OPERATION_TLS_HMAC;
      pCommand->NumDestination=0; /* Make sure */
           
      pTLSHMACContext=&pContext->Context.TLS_HMac;
      RTL_MemZero(pTLSHMACContext,sizeof(*pTLSHMACContext));

#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
      /* Assume sequence numbers are in proper format. */
      pTLSHMACContext->SequenceHigh=pCommand->Parameters.TLSHMACParams.SequenceNumber.HighWord;
      pTLSHMACContext->SequenceLow=pCommand->Parameters.TLSHMACParams.SequenceNumber.LowWord;
#else
      /* Assume sequence numbers are in proper format. */
      pTLSHMACContext->SequenceHigh=BYTESWAPLONG(pCommand->Parameters.TLSHMACParams.SequenceNumber.HighWord);
      pTLSHMACContext->SequenceLow=BYTESWAPLONG(pCommand->Parameters.TLSHMACParams.SequenceNumber.LowWord);
#endif      
      RTL_Memcpy( &pTLSHMACContext->HMACInnerState[0],
		  pCommand->Parameters.TLSHMACParams.HMACState,sizeof(ubsec_HMAC_State_t)); 
      /* printk("md5 = x%x sha = x%x command = x%x\n", UBSEC_MAC_MD5, UBSEC_MAC_SHA1, pCommand->Command); */
      pTLSHMACContext->CryptoFlag = 0;
      if( UBSEC_MAC_MD5 & pCommand->Command )
	pTLSHMACContext->CryptoFlag |= CF_MD5;
      else if( UBSEC_MAC_SHA1 & pCommand->Command )
	pTLSHMACContext->CryptoFlag |= CF_SHA1;

      pTLSHMACContext->ContentType=pCommand->Parameters.TLSHMACParams.ContentType;
      pTLSHMACContext->Version = CPU_TO_CTRL_SHORT(pCommand->Parameters.TLSHMACParams.Version);
      pTLSHMACContext->DataLengthHi=HIGH_BYTE(pCommand->Parameters.TLSHMACParams.DataLength);
	pTLSHMACContext->DataLengthLo=LOW_BYTE(pCommand->Parameters.TLSHMACParams.DataLength);
      pExtraFragment=&pCommand->Parameters.TLSHMACParams.OutputHMAC;
      break;
    case UBSEC_SSL_CRYPTO:
      pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(SSLCRYPTO_CONTEXT_SIZE);
      pContext->operation_type=OPERATION_SSL_CRYPTO;
      pSSLCryptoContext=&pContext->Context.SSL_Crypto;
      RTL_MemZero(pSSLCryptoContext,sizeof(*pSSLCryptoContext));

      if (UBSEC_USING_CRYPT( pCommand->Command )) {
	pSSLCryptoContext->CryptoFlag |= CF_3DES;
	  
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
	RTL_Memcpy(&pSSLCryptoContext->CryptoKey1[0], pCommand->Parameters.SSLCipherParams.CryptKey, 24);
	RTL_Memcpy(&pSSLCryptoContext->ComputedIV[0], &pCommand->Parameters.SSLCipherParams.InitialVector[0],8);
#else
	copywords((UBS_UINT32 *)&pSSLCryptoContext->CryptoKey1[0], pCommand->Parameters.SSLCipherParams.CryptKey, 6);
	copywords((UBS_UINT32 *)&pSSLCryptoContext->ComputedIV[0], (UBS_UINT32 *)&pCommand->Parameters.SSLCipherParams.InitialVector[0],2);
#endif
	}
        /* Set up the context flags for direction */
      if (pCommand->Command & UBSEC_ENCODE)
	pSSLCryptoContext->CryptoFlag = CF_ENCODE;
      else
	pSSLCryptoContext->CryptoFlag = CF_DECODE;

      break;

    case UBSEC_HASH:
      pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(HASH_CONTEXT_SIZE);
      pContext->operation_type=OPERATION_HASH; 
      pHashContext=&pContext->Context.Hash;
      RTL_MemZero(pHashContext,sizeof(*pHashContext));

      if( UBSEC_MAC_MD5 & pCommand->Command )
	pHashContext->CryptoFlag |= CF_MD5;
      else if( UBSEC_MAC_SHA1 & pCommand->Command )
	pHashContext->CryptoFlag |= CF_SHA1;
      pExtraFragment=&pCommand->Parameters.HashParams.OutputHMAC;
      pExtraFragment->FragmentLength=0; /* Only pointer is used .*/
      pCommand->NumDestination=0; /* Should already be but... */
      break;

    case UBSEC_ARC4:

    #if defined(UBSEC_582x) 
      /* ARC4_NULL_DATA mode supported in "582x mode" driver for BCM5821 and later chips only */
      if ((pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_NULL_DATA) && \
	   (pDevice->DeviceID < BROADCOM_DEVICE_ID_5821)) {
	Status=UBSEC_STATUS_INVALID_PARAMETER;
	goto MCR_Done;
      }
    #else
      /* ARC4_NULL_DATA mode not supported in "5820 mode" driver */
      if (pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_NULL_DATA) {
	Status=UBSEC_STATUS_INVALID_PARAMETER;
	goto MCR_Done;
      }       
#endif /* UBSEC_582x */

      pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(ARC4_CONTEXT_SIZE);
      pContext->operation_type=OPERATION_ARC4; 
      pARC4Context=&pContext->Context.ARC4_Crypto;
      RTL_MemZero(pARC4Context,sizeof(*pARC4Context));

      if (pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_WRITEBACK) {
	/*  printk("\nSRL: keystateflag = %d\n", pCommand->Parameters.ARC4Params.KeyStateFlag); */
	pARC4Context->StateInfo|=ARC4_STATE_WRITEBACK;
      }
      if (pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_STATEKEY) {
	/*  printk("\nSRL: keystateflag = %d\n", pCommand->Parameters.ARC4Params.KeyStateFlag); */
	pARC4Context->StateInfo|=ARC4_STATE_STATEKEY;
      }
    #if defined(UBSEC_582x) 
      if (pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_NULL_DATA) {
	/* printk("\nSRL: keystateflag = %d\n", pCommand->Parameters.ARC4Params.KeyStateFlag); */
	pARC4Context->StateInfo|=ARC4_STATE_NULL_DATA;
      } 
    #endif /* UBSEC_582x */


#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
      /* The initial "packed key" must be byteswapped for big endian CryptoNet builds */
      if (pCommand->Parameters.ARC4Params.KeyStateFlag & UBSEC_ARC4_STATE_STATEKEY) 
	copywords( (UBS_UINT32 *)pARC4Context->KeyState,
		   (UBS_UINT32 *)pCommand->Parameters.ARC4Params.KeyStateIn,
		   sizeof(ubsec_ARC4_State_t)/4);
      else
#endif
      RTL_Memcpy( pARC4Context->KeyState,pCommand->Parameters.ARC4Params.KeyStateIn,sizeof(ubsec_ARC4_State_t));


      pExtraFragment=&pCommand->Parameters.ARC4Params.state_out;
      break;
    default:
      Dbg_Print(DBG_CMD,( "ubsec:  SSL Invalid Command %x\n",pCommand->Command ));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto MCR_Done;
    };


    /*
     * Now add the packet input fragment information
     * First fragment will need to skip the MAC Header
     * Must have at least one fragment (pCommand->NumSource > 0).
     *
     * For ARC4_NULL_DATA mode, we still need to know how big the message is.
     * You can actually build a DMA-able input fragment list just like if you were
     * not using ARC4_NULL_DATA mode, but that method incurs unnecessary CPU cycles.
     * The fastest way is to create a single "dummy" input fragment, with
     * a FragmentLength equal to the length of the "virtual" message. 
     * The "dummy" fragment's DataAddress will be ignored. Either way,
     * at least one input fragment must be present.
     */
    if ((Status=SetupInputFragmentList((MasterCommand_pt)pMCR, (Packet_t *)pPacket,pCommand->NumSource,pCommand->SourceFragments))) {
      goto MCR_Done;
    }

    /*
     * Now add the packet output fragment information
     */
    if ((Status=SetupOutputFragmentList((MasterCommand_pt)pMCR, (Packet_t *)pPacket,pCommand->NumDestination,pCommand->DestinationFragments,pExtraFragment))) {
      goto MCR_Done;
    }

    /* Sync the current context memory region for CryptoNet DMA use */
    Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_SSLCommand() Sync Context to Device (0x%08X,%d,%d)\n", pMCR->ContextListHandle[PacketIndex],
		       0,
		       CTRL_TO_CPU_SHORT(pContext->cmd_structure_length)));
    OS_SyncToDevice(pMCR->ContextListHandle[PacketIndex],
		    0,
		    CTRL_TO_CPU_SHORT(pContext->cmd_structure_length));


#ifdef UBSEC_STATS
    if (UBSEC_SSL_COMMAND(pCommand->Command)== UBSEC_SSL_CRYPTO){
      	if (pCommand->Command & UBSEC_ENCODE){
      		pDevice->Statistics.BlocksEncryptedCount++;
      		pDevice->Statistics.BytesEncryptedCount+=CTRL_TO_CPU_SHORT(pPacket->PacketLength);
    		}
    		else {
      		pDevice->Statistics.BlocksDecryptedCount++;
      		pDevice->Statistics.BytesDecryptedCount+=CTRL_TO_CPU_SHORT(pPacket->PacketLength);
    		}
	}
#endif

   /* Now inc the number of packets and prepare for the next command. */
    pMCR->NumberOfPackets++;
    pCommand++;
    PacketIndex++;
    pPacket++;
    pContext++;
  } /* For (;CommandIndex < CommandCount ; CommandIndex++) */

#ifdef UBSDBG
  /* Print out the context information if required */
  DumpCipherMCR(pMCR);
#endif


  /*
   * If we are here then the MCR is built.
   * Either everything went great or we came straight here at the first
   * error condition we encountered. The MCR is filled only with those
   * packets that were built successfully (before any encountered error). 
   * Push the MCR to the device. 
   */
 MCR_Done:
  *NumCommands=CommandIndex; /* Update number completed */

  PushMCR(pDevice);

#ifdef BLOCK 
  /* Wait for all outstanding  to complete */
    while ((Status=WaitForCompletion(pDevice,(unsigned long)100000,UBSEC_CIPHER_LIST))
	   == UBSEC_STATUS_SUCCESS);
    if (Status!=UBSEC_STATUS_TIMEOUT) /* We are nested, return success */
      Status=UBSEC_STATUS_SUCCESS;
 Error_Return:
#else /* not BLOCKing */

 Error_Return:  /* Label to make sure that IRQs are enabled. */
#ifdef COMPLETE_ON_COMMAND_THREAD
    ubsec_PollDevice(pDevice);  /* Try to complete some & cut down on ints */
#endif

#endif /* BLOCK */
    OS_LeaveCriticalSection(pDevice,SaveConfig);

#ifdef UBSEC_STATS
		if (Status != UBSEC_STATUS_SUCCESS)
			pDevice->Statistics.CryptoFailedCount++;
#endif
   return(Status);

#else /* UBSEC_SSL_SUPPORT not defined */
    return(UBSEC_STATUS_NO_DEVICE);
#endif /* UBSEC_SSL_SUPPORT */

}





