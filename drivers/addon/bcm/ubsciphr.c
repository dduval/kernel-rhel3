
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
 * ubsciphr.c: Cipher commands are handled by this module
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 12/01/1999 DWP Modified to handle Big Endian setup.
 * 12/03/1999 SOR Modifications to do all static address computations at init time
 * 07/06/2000 DPA Fixes for SMP operation
 * 09/21/2000 SOR Upgrade to make 5820 enable
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/20/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port.
 */

#include "ubsincl.h"

/*
 * ubsec_CipherCommand: Process a list of Cipher commands.
 *
 * Immediate Status is returned. Completion status is returned
 * on a per command callback
 */
ubsec_Status_t 
ubsec_CipherCommand(ubsec_DeviceContext_t Context,
	      ubsec_CipherCommandInfo_pt pCommand,
	      int *NumCommands)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  VOLATILE MasterCommand_t  *pMCR;
  VOLATILE Packet_t         *pPacket;
  VOLATILE PacketContext_t  *pContext;
  VOLATILE CipherContext_t  *pCipherContext;
  VOLATILE int             PacketIndex;
  VOLATILE int  NumFrags;   /* Number of fragments */
  ubsec_FragmentInfo_t ExtraFragment, *pExtraFragment;
  int CommandIndex=0;
  int CommandCount=*NumCommands;
  ubsec_Status_t Status;
  UBS_UINT32 SaveConfig = 0;

  Dbg_Print(DBG_CMD,( "ubsec:  ubsec command %d",*NumCommands ));

  /*
   * Check some parameters
   */    
  if(pDevice==NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,( "NO DEV\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
  Dbg_Print(DBG_CMD,( "\n"));

  if (OS_EnterCriticalSection(pDevice,SaveConfig)) {
    return(UBSEC_STATUS_DEVICE_BUSY);
  }

  /* Get the next MCR to load */
 Get_New_MCR:
  *NumCommands=CommandIndex; /* Update number completed */
  if ((pMCR=GetFreeMCR(pDevice,UBSEC_CIPHER_LIST,&Status))== NULL_MASTER_COMMAND) {
    Dbg_Print(DBG_CMD_FAIL,("ubsec: device busy MCR %x\n", Status));
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
      Dbg_Print(DBG_CMD,( "ubsec:  overran mcr buffer. %d %d\n",PacketIndex,CommandIndex ));
      /* 
       * We have filled this MCR with the max # of packets,
       * but still have more packets (commands) to do.
       * Advance next free. Wrap around if necessary
       */
      pDevice->NextFreeMCR[UBSEC_CIPHER_LIST]=
	(MasterCommand_pt) pMCR->pNextMCR;

      /* For crypto MCRs, the contexts are accessed using a single handle   */
      /* for an array of contexts. This means that all contexts for an MCR  */
      /* are contiguous in memory, and that we can sync all contexts at     */
      /* once (now that we know that we're finished loading this MCR).      */
      /* Make DMA memory actually hold CPU-initialized context data         */
      Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_CipherCommand Sync %d Contexts to Device (0x%08X,%d,%d)\n", 
			 pMCR->NumberOfPackets,
			 pMCR->ContextListHandle[0],
			 0,
			 pMCR->NumberOfPackets * sizeof(PacketContext_t)));
      OS_SyncToDevice(pMCR->ContextListHandle[0],0,
		      pMCR->NumberOfPackets * sizeof(PacketContext_t));

      PushMCR(pDevice); /* Get it going (pipeline) */
      goto Get_New_MCR; /* Try to add to the next MCR */
    }
    
    /* Save the callback information. */
    pMCR->CompletionArray[PacketIndex].CompletionCallback = pCommand->CompletionCallback;
    pMCR->CompletionArray[PacketIndex].CommandContext = pCommand->CommandContext;

    /* Now set up the packet processing parameters */
    Dbg_Print(DBG_PACKET,( "ubsec: packet_Index %d, Context Buf %0x\n",PacketIndex,pContext ));
    pPacket->PacketContextBuffer=pContext->PhysicalAddress;
    pCipherContext=&pContext->Context.Cipher;
    RTL_MemZero(pCipherContext,sizeof(*pCipherContext));
#ifdef UBSEC_582x_CLASS_DEVICE
    /* Some extra fields to be filled in . */
    pContext->cmd_structure_length= CPU_TO_CTRL_SHORT(sizeof(*pCipherContext)+4); /* For header. */
    pContext->operation_type=OPERATION_IPSEC; /* send mode for DH */
#endif    
    /*
     * Now add the packet input fragment information
     * First fragment will need to skip the MAC Header
     * We need at least one fragment.
     */
    /* Sanity checks.*/
    if (!(NumFrags=pCommand->NumSource)) {
      Dbg_Print(DBG_PACKET,( "ubsec:  No Input fragments\n" ));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto MCR_Done;
    }
    if (NumFrags>(UBSEC_MAX_FRAGMENTS+1)) {
      Dbg_Print(DBG_PACKET,( "ubsec:  Too Many Input fragments\n" ));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto MCR_Done;
    }

    Dbg_Print(DBG_PACKET,( "ubsec: Num Input Frags %d \n",NumFrags));

    /* SetupInputFragmentList will always be successful here because of */
    /* the sanity checks performed above.                               */
    SetupInputFragmentList((MasterCommand_t *)pMCR, (Packet_t *)pPacket,NumFrags,pCommand->SourceFragments);

    /*
     * Now add the packet output fragment information
     * We need at least one fragment.
     */
    /* Sanity checks */
    if (!(NumFrags=pCommand->NumDestination)) {
      Dbg_Print(DBG_PACKET,( "ubsec:  No Output fragments\n" ));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto MCR_Done;
    }
    if (NumFrags > (UBSEC_MAX_FRAGMENTS+1)) {
      Dbg_Print(DBG_PACKET,( "ubsec:  Too Many Output fragments\n" ));
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto MCR_Done;
    }

    Dbg_Print(DBG_PACKET,( "ubsec: Num Output Frags %d \n",NumFrags));

    if (UBSEC_USING_MAC(pCommand->Command)) { 
      /* We need an 'extra' fragment info struct for the auth data */
      ExtraFragment.FragmentAddress = 
	pCommand->AuthenticationInfo.FragmentAddress;
      /* Easy to do check here for invalid 'extra' fragment address */
      if ( (long) ExtraFragment.FragmentAddress & 0x03 ) {
	Dbg_Print(DBG_PACKET,("ubsec:  ################INVALID HMAC ADDRESS %08x\n",ExtraFragment.FragmentAddress));
	Status=UBSEC_STATUS_INVALID_PARAMETER;
	goto Error_Return;
      }
      /* The CryptoNet chip knows how big the auth fragment is, but */
      /* SetupOutputFragmentList() needs to see a length of zero.   */
      ExtraFragment.FragmentLength = 0;
      pExtraFragment = &ExtraFragment;
    }
    else { /* not doing authentication; pass NULL extra fragment info */
      pExtraFragment = (ubsec_FragmentInfo_pt) 0;
    }
    /* SetupOutputFragmentList() checks frag list for allowable fragment */
    /* addresses (4-byte aligned) and lengths (4-byte multiples).        */
    if (SetupOutputFragmentList((MasterCommand_t *)pMCR,(Packet_t *)pPacket,NumFrags,
			    pCommand->DestinationFragments,pExtraFragment)) {
      Status=UBSEC_STATUS_INVALID_PARAMETER;
      goto Error_Return;
    }

    /* Set up the context flags */
    if (pCommand->Command & UBSEC_ENCODE)
      pCipherContext->CryptoFlag = CF_ENCODE;
    else
      pCipherContext->CryptoFlag = CF_DECODE;

    if (UBSEC_USING_CRYPT( pCommand->Command )) {
      pCipherContext->CryptoFlag |= CF_3DES;
      pCipherContext->CryptoOffset = CPU_TO_CTRL_SHORT( pCommand->CryptHeaderSkip );
      if (pCommand->Command &UBSEC_3DES) {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
	RTL_Memcpy( &pCipherContext->CryptoKey1[0], pCommand->CryptKey, 24);
#else
	copywords((UBS_UINT32 *)&pCipherContext->CryptoKey1[0], (UBS_UINT32 *)pCommand->CryptKey, 6);
#endif
      } 
      else {  
	/* Des is implemented by using 3 copies of the same DES key */
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
	RTL_Memcpy( &pCipherContext->CryptoKey1[0], pCommand->CryptKey, 8); 
#else
	copywords((UBS_UINT32 *) &pCipherContext->CryptoKey1[0], (UBS_UINT32 *) pCommand->CryptKey, 2); 
#endif
	RTL_Memcpy( &pCipherContext->CryptoKey2[0],&pCipherContext->CryptoKey1[0],sizeof(pCipherContext->CryptoKey1));
	RTL_Memcpy( &pCipherContext->CryptoKey3[0],&pCipherContext->CryptoKey1[0],sizeof(pCipherContext->CryptoKey1));
      }
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN)
      RTL_Memcpy(&pCipherContext->ComputedIV[0],&pCommand->InitialVector[0],8);
#else
      copywords((UBS_UINT32 *) &pCipherContext->ComputedIV[0], (UBS_UINT32 *) &pCommand->InitialVector[0],2);
#endif
    }
    /* If using HMAC then copy the authentication state to the context. */
    if( UBSEC_USING_MAC( pCommand->Command ) ) {
      RTL_Memcpy( &pCipherContext->HMACInnerState[0],
		  pCommand->HMACState,
		  sizeof(ubsec_HMAC_State_t));
      if( UBSEC_MAC_MD5 & pCommand->Command )
	    pCipherContext->CryptoFlag |= CF_MD5;
      else if( UBSEC_MAC_SHA1 & pCommand->Command )
	    pCipherContext->CryptoFlag |= CF_SHA1;
    }
    
    Dbg_Print( DBG_PACKET, ("ubsec:  CryptoOffset and Flag [%04x][%04x]\n",
        CTRL_TO_CPU_SHORT( pCipherContext->CryptoOffset ), 
        CTRL_TO_CPU_SHORT( pCipherContext->CryptoFlag )) );

#ifdef UBSEC_STATS
    if (pCipherContext->CryptoFlag & CF_DECODE) {
      pDevice->Statistics.BlocksDecryptedCount++;
      pDevice->Statistics.BytesDecryptedCount+=CTRL_TO_CPU_SHORT(pPacket->PacketLength);
    }
    else {
      pDevice->Statistics.BlocksEncryptedCount++;
      pDevice->Statistics.BytesEncryptedCount+=CTRL_TO_CPU_SHORT(pPacket->PacketLength);
    }
#endif

   /* Now inc the number of packets and prepare for the next command. */
    pMCR->NumberOfPackets++;
    pCommand++;
    PacketIndex++;
    pPacket++;
    pContext++;

  } /* For NumCommands-- */

  /*
   * If we are here then the last packet(s) (commands) have been added to
   * the current MCR.
   * Push the MCR to the device. 
   */
 MCR_Done:
  *NumCommands=CommandIndex; /* Update number completed */

  /* For crypto MCRs, the contexts are accessed using a single handle   */
  /* for an array of contexts. This means that all contexts for an MCR  */
  /* are contiguous in memory, and that we can sync all contexts at     */
  /* once (now that we know that we're finished loading this MCR).      */
  /* Make DMA memory actually hold CPU-initialized context data         */
  Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_CipherCommand Sync %d Contexts to Device (0x%08X,%d,%d)\n", 
			 pMCR->NumberOfPackets,
			 pMCR->ContextListHandle[0],
			 0,
			 pMCR->NumberOfPackets * sizeof(PacketContext_t)));
  OS_SyncToDevice(pMCR->ContextListHandle[0],0,
		  pMCR->NumberOfPackets * sizeof(PacketContext_t));

  PushMCR(pDevice);

#ifdef BLOCK 
  /* Wait for all outstanding  to complete */
    while ((Status=WaitForCompletion(pDevice,(UBS_UINT32)100000,UBSEC_CIPHER_LIST))
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
			pDevice->Statistics.CryptoFailedCount++;
#endif
   return(Status);
}


/*
 * InitHMACState: Initialize the inner and outer state of the HMAC
 *
 * This is to allow it to be used for the authentication commands.
 *
 */
ubsec_Status_t
ubsec_InitHMACState(ubsec_HMAC_State_pt HMAC_State,
		      ubsec_CipherCommand_t type,
		      ubsec_HMAC_Key_pt Key)
{
  RTL_MemZero(HMAC_State,sizeof(*HMAC_State));
  if (type==UBSEC_MAC_SHA1) {
    InitSHA1State(HMAC_State,Key);
    return(UBSEC_STATUS_SUCCESS);
  }
  else
    if (type== UBSEC_MAC_MD5) {
      InitMD5State(HMAC_State,Key);
      return(UBSEC_STATUS_SUCCESS);
    }
return(UBSEC_STATUS_INVALID_PARAMETER);
}





