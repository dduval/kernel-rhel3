
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
 * ubsdh.c: Diffie Hellman parameter setup functions.
 *
 * Revision History:
 *
 * May 2000 SOR Created
 * Sep 2000 SOR 5820 upgrade
 * 04/20/2001 RJT Added support for CPU-DMA memory synchronization
 * 04/24/2001 DPA Allow for unnormalize of D-H random number (x) output for BCM5805
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port.
 */

#include "ubsincl.h"

#ifdef UBSEC_PKEY_SUPPORT

/*
 * DH_SetupPublicParams:
 */
ubsec_Status_t 
DH_SetupPublicParams(VOLATILE MasterCommand_pt 	pMCR,
		     ubsec_DH_Params_pt pDHParams)

     /* pDHParams points to a structure which contains all of the info
	needed for Diffie-Hellman operations. In addition to regular
	numerical parameters, "key" memory buffer locations are passed 
	using memory "handles". Handles are defined as memory descriptors
	from which BOTH the virtual and physical addresses of the designated
	memory can be derived.

	The virtual and physical pointers associated with the handle 
	must be extracted by using the following macros:

	  OS_GetVirtualAddress()
	  OS_GetPhysicalAddress()
	
	Results from OS_GetPhysicalAddress() may be written to CryptoNet
	control structures in (DMA) memory. 
	Results from OS_GetVirtualAddress() may be used (if necessary) as
	regular old pointers.
     */

{
#ifdef UBSEC_DH_SUPPORT
  VOLATILE DataBufChainList_t  *FragPtr,*NextFragPtr;
  int 		     DataLength;
  VOLATILE Packet_t *pPacket;
  DH_Send_CtxCmdBuf_pt pDHSendCtx;
  VOLATILE int             	PacketIndex;
  ubsec_Status_t Status=UBSEC_STATUS_SUCCESS;
  int NgLen;
  int NormalizeLen,NrmBits;
  int element;
  unsigned char *pNg;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  PacketIndex = pMCR->NumberOfPackets; 
  pDHSendCtx = (DH_Send_CtxCmdBuf_t *) &pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.DH_Send_CtxCmdBuf; 
  RTL_MemZero(pDHSendCtx,sizeof(*pDHSendCtx));
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */

  pDHSendCtx->rng_enable= CPU_TO_CTRL_SHORT(pDHParams->RNGEnable); 

  /* The modulus needs to be aligned on a 512/768 or 1024 bit boundary.
   (2048 for 5820) */

  /*
   * Save amount to normalize/renormalize.
   */
  if (pDHParams->N.KeyLength <=512)
    NormalizeLen=512;
  else
    if (pDHParams->N.KeyLength <=768)
      NormalizeLen=768;
    else
      if (pDHParams->N.KeyLength <=1024)
	NormalizeLen=1024;
      else
#ifdef UBSEC_582x_CLASS_DEVICE
	if (pDHParams->N.KeyLength <=1536)
	  NormalizeLen=1536;
	else
	  if (pDHParams->N.KeyLength <= 2048)
	    NormalizeLen=2048;
	  else
#endif
        return(UBSEC_STATUS_INVALID_PARAMETER);

#ifndef UBSEC_HW_NORMALIZE
  if ((NrmBits = ubsec_NormalizeDataTo(&pDHParams->N,NormalizeLen))) {
      ubsec_ShiftData(&pDHParams->G, NrmBits);
  }

  pMCR->KeyContextList[PacketIndex]->NormBits=NrmBits;
#else
  NrmBits=0;
#endif
  pMCR->KeyContextList[PacketIndex]->ResultKey[0]=&(pDHParams->Y); /* Save here for post-command finishing */
  pMCR->KeyContextList[PacketIndex]->ResultKey[1]=NULL; /* Not used */ 

  /*
   * Output Y value may need to be rounded up to represent an integral
   * number of 32 bit words, same total length as modulus N.
   */

#ifndef UBSEC_HW_NORMALIZE
  pDHParams->Y.KeyLength = NormalizeLen;    
#else
#if 0
  pDHParams->Y.KeyLength = ROUNDUP_TO_32_BIT(pDHParams->Y.KeyLength);
#else
  pDHParams->Y.KeyLength = NormalizeLen;    
#endif
#endif

  /* Now setup some of the parameters that need to be aligned. */
  /* RJT_TEST why is this rounded up? */
  pDHParams->X.KeyLength = ROUNDUP_TO_32_BIT(pDHParams->X.KeyLength);
  pMCR->KeyContextList[PacketIndex]->ResultRNG=&(pDHParams->X); /* Save here for post-command finishing */

	/* N Copy the modulo value */
  pDHSendCtx->modulus_length = (unsigned short)CPU_TO_CTRL_SHORT(pDHParams->N.KeyLength);
  pNg=(unsigned char *)&pDHSendCtx->Ng[0]; /* For convenience */
  NgLen=NormalizeLen/8;
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords((UBS_UINT32 *)&pNg[0],(UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->N.KeyValue),NgLen/4);
 #else
  RTL_Memcpy(&pNg[0],OS_GetVirtualAddress(pDHParams->N.KeyValue),NgLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else /* HW does the normalization */
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords((UBS_UINT32 *)&pNg[0],
	    (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->N.KeyValue),
	    ROUNDUP_TO_32_BIT(pDHParams->N.KeyLength)/32);
 #else
  RTL_Memcpy(&pNg[0],OS_GetVirtualAddress(pDHParams->N.KeyValue),ROUNDUP_TO_32_BIT(pDHParams->N.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif

  /*
   * G  Copy the input value. This is the public key for private
   * operation or the Baseg value for public key generation.
   * It also needs to be aligned on the same  length
   * as N
   */
  pNg+=NgLen; /* Starting G location. */
  pDHSendCtx->generator_length=(unsigned short)CPU_TO_CTRL_SHORT(pDHParams->G.KeyLength);
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords((UBS_UINT32 *)&pNg[0],(UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->G.KeyValue),NgLen/4);
 #else
  RTL_Memcpy(&pNg[0],OS_GetVirtualAddress(pDHParams->G.KeyValue),NgLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else /* HW does the normalization */
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords((UBS_UINT32 *)&pNg[0],
	    (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->G.KeyValue),
	    ROUNDUP_TO_32_BIT(pDHParams->G.KeyLength)/32);
 #else
  RTL_Memcpy(&pNg[0],OS_GetVirtualAddress(pDHParams->G.KeyValue),ROUNDUP_TO_32_BIT(pDHParams->G.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  
  /* "extra" length is always 2 x Ng length */
  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=(NgLen*2);

  /*
   * Input Buffer setup for DH Send (Public): 
   * If the private key x is provided by software, save
   * it in the first input data buffer, otherwise, the 
   * input data buffer will not be used by the chip
   */
  FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  if (! pDHParams->RNGEnable ) { /* Random number manually provided */
    /* RJT_TEST why is this rounded up? */
    pDHParams->UserX.KeyLength = ROUNDUP_TO_32_BIT(pDHParams->UserX.KeyLength);
    PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->UserX.KeyValue));
#if defined(UBS_ENABLE_KEY_SWAP)
    longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->UserX.KeyValue);
    for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDHParams->UserX.KeyLength)/32 ; element++) 
      longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
    DataLength=(pDHParams->UserX.KeyLength+7)/8; /* map the length from bits to bytes */
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    pDHSendCtx->private_key_length = (unsigned short)CPU_TO_CTRL_SHORT(pDHParams->UserX.KeyLength);
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DH_SetupPublicParams Sync UserX Fragment to Device (0x%08X,%d,%d)\n", 
			      pDHParams->UserX.KeyValue,
			      0,
			      DataLength));
    OS_SyncToDevice(pDHParams->UserX.KeyValue,
		    0,
		    DataLength);
    Dbg_Print(DBG_DHKEY,( "Public (Send) NormBits %d Input Key, UserX: <%d,%08x (%08x)>\n",NrmBits,DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
  }
  else { /* CryptoNet chip to internally generate random number X */
    pDHSendCtx->private_key_length=CPU_TO_CTRL_SHORT(pDHParams->RandomKeyLen);
    FragPtr->DataLength = 0;
    FragPtr->DataAddress = 0;
    DataLength=0;
  } /* Manual/CryptoNet random number generation if-else */
  /* The CryptoNet chip ignores the pPacket->PacketLength field for this */
  /* operation. We'll zero that field out for consistency's sake.        */
  pPacket->PacketLength = 0;
  FragPtr->pNext = 0; /* Terminate the (empty or single) input fragment list */
#ifdef UBSDBG
  /* Print out the context information if required */
  {
    int WordLen,i;
    WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-DH_STATIC_SEND_CONTEXT_SIZE)/4;
    Dbg_Print(DBG_DHKEY,(   "ubsec:  ---- DH_Public - RNG-Enable [%d] Private Klen [%d] Generator Len [%d]\n",
			    CTRL_TO_CPU_SHORT(pDHSendCtx->rng_enable),
			    CTRL_TO_CPU_SHORT(pDHSendCtx->private_key_length),
			    CTRL_TO_CPU_SHORT(pDHSendCtx->generator_length))); 

    Dbg_Print(DBG_DHKEY,(   "ubsec:  ---- Modulus Length [%d] ",
			    CTRL_TO_CPU_SHORT(pDHSendCtx->modulus_length))); 
    Dbg_Print(DBG_DHKEY,(   "Context Len %d Context Value=[",
			    (pMCR->KeyContextList[PacketIndex]->cmd_structure_length))); 
    for ( i=0 ; i < WordLen ; i++) {
      Dbg_Print(DBG_DHKEY,( "%08x ",SYS_TO_BE_LONG(pDHSendCtx->Ng[i])));
    }
    Dbg_Print(DBG_DHKEY,( "]\n"));

  }
#endif

  /* Output Buffer setup for DH Send (Public):
   * The output buffer has Public Key Y, followed by
   * Private Key X
   */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
        /* The first output data buffer has Public Key Y */
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->Y.KeyValue)); 
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDHParams->Y.KeyLength+7)/8; /* map the length from bits to bytes */
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
  if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
  if ((DataLength) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT LENGTH %08x\n", DataLength)); 
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
#endif 
        /* get the next fragment pointer */
  NextFragPtr=&pMCR->OutputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
  FragPtr->pNext =NextFragPtr->PhysicalAddress; 
  Dbg_Print(DBG_DHKEY,( "Public (Send) NormBits %d Output Key  Y: <%d,%08x, (%08x,Next-%08x)>\n",
      NrmBits,DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));
  FragPtr=NextFragPtr;
        /* The second output data buffer has Private Key X */
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->X.KeyValue)); 
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDHParams->X.KeyLength+7)/8; /* map the length from bits to bytes */
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

  Dbg_Print(DBG_DHKEY,( "Public (Send) Output Key X: <%d, %08x, (%08x,Next-%08x)>\n",
      DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
  if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT_B ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
  if ((DataLength) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT_B LENGTH %08x\n", DataLength)); 
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
#endif 
  
  FragPtr->pNext = 0;

#ifndef STATIC_F_LIST
  /* Fragment lists are external to MCR structures, must sync separately */
  /* Always 2 output frags, need to sync one entry in OutputFragmentList */
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DH_SetupPublicParams Sync OFrag Descriptor to Device (0x%08X,%d,%d)\n", pMCR->OutputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      sizeof(DataBufChainList_t)));
  OS_SyncToDevice(pMCR->OutputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    sizeof(DataBufChainList_t));
#endif /* STATIC_F_LIST not defined */

#ifdef UBSDBG
 Error_Return:
#endif 

  return(Status);
#else
    return(UBSEC_STATUS_NO_DEVICE);
#endif


}



/*
 * DH_SetupSharedParams:
 */
ubsec_Status_t 
DH_SetupSharedParams(VOLATILE MasterCommand_pt 	pMCR,
		     ubsec_DH_Params_pt pDHParams)
{
#ifdef UBSEC_DH_SUPPORT
  VOLATILE DataBufChainList_t  *FragPtr, *NextFragPtr;
  int 		DataLength;
  ubsec_Status_t Status=UBSEC_STATUS_SUCCESS;
  VOLATILE Packet_t *pPacket;
  DH_REC_CtxCmdBuf_pt pDHRecCtx;
  VOLATILE int             	PacketIndex;
  int NgLen;
  int element;
  int NormalizeLen,NrmBits=0;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  PacketIndex = pMCR->NumberOfPackets; 
  pDHRecCtx = (DH_REC_CtxCmdBuf_t *) &pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.DH_REC_CtxCmdBuf; 
  RTL_MemZero(pDHRecCtx,sizeof(*pDHRecCtx));
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */

  if (pDHParams->N.KeyLength <=512)
    NormalizeLen=512;
  else
    if (pDHParams->N.KeyLength <=768)
      NormalizeLen=768;
    else
      if (pDHParams->N.KeyLength <=1024)
	NormalizeLen=1024;
      else
#ifdef UBSEC_582x_CLASS_DEVICE
	if (pDHParams->N.KeyLength <=1536)
	  NormalizeLen=1536;
	else
	  NormalizeLen=2048;
#else
        return(UBSEC_STATUS_INVALID_PARAMETER);
#endif

    /*
     * Output K value may need to be rounded up to represent an integral
     * number of 32 bit words, same total length as modulus N.
     */
#ifndef UBSEC_HW_NORMALIZE
  pDHParams->K.KeyLength = NormalizeLen;    
  if ((NrmBits = ubsec_NormalizeDataTo(&pDHParams->N,NormalizeLen))) {
    ubsec_ShiftData(&pDHParams->Y, NrmBits);
  }
  pMCR->KeyContextList[PacketIndex]->NormBits=NrmBits;
#else
#if 1
  pDHParams->K.KeyLength = NormalizeLen;    
#else
  pDHParams->K.KeyLength = ROUNDUP_TO_32_BIT(pDHParams->K.KeyLength);
#endif
  NrmBits=0;
#endif
  pMCR->KeyContextList[PacketIndex]->ResultKey[0]=&pDHParams->K; /* Save here for post-command finishing */
  pMCR->KeyContextList[PacketIndex]->ResultKey[1]=NULL; /* Not used */ 
  pMCR->KeyContextList[PacketIndex]->ResultRNG=NULL; /* Not used */


  pDHParams->Y.KeyLength = NormalizeLen;    

  /* Now setup some of the parameters that need to be aligned. */
  /* RJT_TEST why is this rounded up? */
  pDHParams->X.KeyLength = ROUNDUP_TO_32_BIT(pDHParams->X.KeyLength);

  NgLen=NormalizeLen/8;

  /* N Copy the modulo value modulo */
  pDHRecCtx->modulus_length = (unsigned short)CPU_TO_CTRL_SHORT(pDHParams->N.KeyLength);

#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDHRecCtx->N[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->N.KeyValue),
	     NgLen/4);
 #else
  RTL_Memcpy( &pDHRecCtx->N[0],OS_GetVirtualAddress(pDHParams->N.KeyValue),NgLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else /* HW does the normalization */
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDHRecCtx->N[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->N.KeyValue),
	     ROUNDUP_TO_32_BIT(pDHParams->N.KeyLength)/32);
 #else
  RTL_Memcpy( &pDHRecCtx->N[0],OS_GetVirtualAddress(pDHParams->N.KeyValue),ROUNDUP_TO_32_BIT(pDHParams->N.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=NgLen;

  /* Set the private key value */
  pDHRecCtx->modulus_length = (unsigned short)CPU_TO_CTRL_SHORT(pDHParams->N.KeyLength);
  pDHRecCtx->private_key_length=(unsigned short)CPU_TO_CTRL_SHORT(pDHParams->X.KeyLength);

#ifdef UBSDBG
  /* Print out the context information if required */
  {
  int WordLen,i;
  WordLen=(NgLen/4);
  Dbg_Print(DBG_DHKEY,(   "ubsec:  ---- DH Shared Mod Length [%d] Pkey Len [%d] Context Len %d, Value -\n[",
			  CTRL_TO_CPU_SHORT(pDHRecCtx->modulus_length),
			  CTRL_TO_CPU_SHORT(pDHRecCtx->private_key_length),		
			  pMCR->KeyContextList[PacketIndex]->cmd_structure_length)); 
  for ( i=0 ; i < WordLen ; i++) {
    Dbg_Print(DBG_DHKEY,( "%08x ",SYS_TO_BE_LONG(pDHRecCtx->N[i])));
  }
  Dbg_Print(DBG_DHKEY,( "]\n"));
  }
#endif

  /* Input Buffer setup for DH Receive (Shared): */
  FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
     /* The first fragment has Y */
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->Y.KeyValue));
#if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->Y.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDHParams->Y.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDHParams->Y.KeyLength+7)/8; /* map the length from bits to bytes */
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  /* The CryptoNet chip ignores the pPacket->PacketLength field for this */
  /* operation. We'll zero that field out for consistency's sake.        */
  pPacket->PacketLength = 0;
        /* get the next fragment pointer */
  NextFragPtr=&pMCR->InputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
  FragPtr->pNext =NextFragPtr->PhysicalAddress; 
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DH_SetupSharedParams Sync Y Fragment to Device (0x%08X,%d,%d)\n", 
			    pDHParams->Y.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pDHParams->Y.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_DHKEY,( "DH Shared  Y:  FragI <%d,%08x %08x,Next-%08x>\n",
		      DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

  FragPtr=NextFragPtr;
        /* The second Input data buffer has Private Key x */
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->X.KeyValue));
#if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDHParams->X.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDHParams->X.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDHParams->X.KeyLength+7)/8; /* map the length from bits to bytes */
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DH_SetupSharedParams Sync X Fragment to Device (0x%08X,%d,%d)\n", 
			    pDHParams->X.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pDHParams->X.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_DHKEY,( "Shared Private Key X: <%d, %08x, (%08x,Next-%08x)>\n",
      DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));
  FragPtr->pNext = 0;

        /* Output Buffer setup for DH Received (Shared):
	 * The output buffer contains shared secret K 
	 */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDHParams->K.KeyValue)); 
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDHParams->K.KeyLength+7)/8; /* map the length from bits to bytes */
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
  if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
  if ((DataLength) & 0x03) {
    Dbg_Print(DBG_FATAL,("ubsec:#########INVALID OUTPUT LENGTH %08x\n", DataLength)); 
    Status=UBSEC_STATUS_INVALID_PARAMETER;
    goto Error_Return;
  }
#endif 
  Dbg_Print(DBG_DHKEY,( "Receive Buffer K:FragO <%d, %08x>\n",
		      DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress )));
  FragPtr->pNext = 0;


#ifdef UBSDBG
 Error_Return:
#endif 


  return(Status);
#else
    return(UBSEC_STATUS_NO_DEVICE);
#endif

}


#endif /* UBSEC_PKEY_SUPPORT */






