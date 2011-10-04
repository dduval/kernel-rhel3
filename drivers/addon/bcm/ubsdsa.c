
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
 * ubsdsa.c: DSA parameter setup functions.
 *
 * Revision History:
 *
 * May  2000 SOR Created
 * 04/18/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

#ifdef UBSEC_PKEY_SUPPORT

/* 
 * Function: DSA_SetupSignParams()
 * Set up KeyContext Buffer, MCR Input Buffer and MCR Output Buffer 
 * for DSA Sign  operation with parameters provided by pDSAParams 
 */

ubsec_Status_t 
DSA_SetupSignParams(MasterCommand_pt pMCR, ubsec_DSA_Params_pt pDSAParams)

     /* pDSAParams points to a structure which contains all of the info
	needed for the DSA operations. In addition to regular
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
#ifdef UBSEC_DSA_SUPPORT
  volatile DataBufChainList_t   *FragPtr = NULL, *NextFragPtr;
  int                            DataLength;
  volatile Packet_t 		*pPacket;
  VOLATILE DSA_Sign_CtxCmdBuf_t	*pDSACtx;
  int                  PacketIndex;
  int fragnum; 
  int Offset;
  int element;
  int NormBits,NormalizeLen;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  PacketIndex = pMCR->NumberOfPackets;
  pDSACtx = &pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.DSA_Sign_CtxCmdBuf;

  /* Zero out the parameters */
  RTL_MemZero(pDSACtx,sizeof(*pDSACtx));

  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet */
  pDSACtx->sha1_enable= pDSAParams->HashEnable ? CPU_TO_CTRL_SHORT(1) : 0;

  pDSACtx->p_length = (unsigned short)CPU_TO_CTRL_SHORT(pDSAParams->ModP.KeyLength);
  if (pDSAParams->ModP.KeyLength <=512)
    NormalizeLen=512;
  else
    if (pDSAParams->ModP.KeyLength  <= 768)
      NormalizeLen=768;
    else
    if (pDSAParams->ModP.KeyLength  <= 1024)
      NormalizeLen=1024;
    else
#ifdef UBSEC_582x_CLASS_DEVICE
      if (pDSAParams->ModP.KeyLength <=1536)
	NormalizeLen=1536;
      else
	if (pDSAParams->ModP.KeyLength<=2048)
	  NormalizeLen=2048;
	else
#endif
      return(UBSEC_STATUS_INVALID_PARAMETER);



  /*
   * Q Needs to be normalized on 160 bits.
   * P & G need to be shifted the same amount
   */
#ifndef UBSEC_HW_NORMALIZE
  NormBits = ubsec_NormalizeDataTo(&pDSAParams->ModQ,160);
  NormBits=ubsec_NormalizeDataTo(&pDSAParams->ModP, NormalizeLen); 
  ubsec_ShiftData(&pDSAParams->BaseG, NormBits); 
#else
  NormBits=0;
#endif
  NormalizeLen/=8;

  Offset=0;
  /* q setup, Always 160 Bits. */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	     20/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[0],OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),20);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->ModQ.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[0],OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->ModQ.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=20;

  /* p setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	     NormalizeLen/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->ModP.KeyValue)
	      ,NormalizeLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->ModP.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->ModP.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=NormalizeLen;

  /* g setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	     NormalizeLen/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),NormalizeLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->BaseG.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->BaseG.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=NormalizeLen;

  /* x setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	     20/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->Key.KeyValue),20);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->Key.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->Key.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=20;

  /* Set total length */
  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=Offset;

#ifdef UBSDBG
  /* Print out the context information if required */
  {
  int WordLen,i;
  WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-DSA_STATIC_SIGN_CONTEXT_SIZE)/4;
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- DSA Mod P Length [%d] \n",
			   CTRL_TO_CPU_SHORT(pDSACtx->p_length))); 
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- DSA SHA Enabled  [%d]\n",
			   CTRL_TO_CPU_SHORT(pDSACtx->sha1_enable)));
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- Context Len %d Value -\n[",
			   pMCR->KeyContextList[PacketIndex]->cmd_structure_length )); 

  for ( i=0 ; i < WordLen ; i++) {
    Dbg_Print(DBG_DSAKEY,( "%08x ",SYS_TO_BE_LONG(pDSACtx->CtxParams[i])));
  }
  Dbg_Print(DBG_DSAKEY,( "]\n"));
  }
#endif

  /* Input Buffer setup for DSA Sign. */
  pPacket->PacketLength = 0;
  /* The DSA message is a bytestream. Treat it just like a crypto buffer. */
  NextFragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  for (fragnum=0;fragnum<(int)pDSAParams->NumInputFragments;fragnum++) {
    FragPtr = NextFragPtr;
    PhysAddr=pDSAParams->InputFragments[fragnum].FragmentAddress;
    DataLength=pDSAParams->InputFragments[fragnum].FragmentLength;
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    /* Add (endian-adjusted) fragment length into current packet structure */
    pPacket->PacketLength += (unsigned short)DataLength; 
    if (fragnum==0) { /* Next frag descriptor is packet's InputFragmentList */
      NextFragPtr=&pMCR->InputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
    }
    else { /* Next frag descriptor is next InputFragmentList entry */
      NextFragPtr=&FragPtr[1]; 
    }
    FragPtr->pNext = NextFragPtr->PhysicalAddress;
    Dbg_Print(DBG_DSAKEY,( "DSA Sign InputKeyInfo: IFrag[%d] <%d,%08x %08x>\n",
			   fragnum, DataLength, 
			   CTRL_TO_CPU_LONG( FragPtr->DataAddress ), 
			   FragPtr));
  } /* for each input fragment of the unhashed message bytestream */

  /* ->PacketLength is only for the message 'm'; it does not count the */
  /* size of the fragment used for the random number (if present)      */
  /* Therefore, we're finished updating ->PacketLength.                */
    
#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 
  /* fix up the packet length endianess in DMA control memory */
  pPacket->PacketLength = CPU_TO_CTRL_SHORT( pPacket->PacketLength );
#endif 

    if (!pDSAParams->RNGEnable) { /* CryptoNet RNG generation not requested */
    /* The random number is provided by the user (not CryptoNet). It    */
    /* will use the next available frag descriptor in InputFragmentList */
    pDSACtx->rng_enable=0;

    /* If here we need to use an additional input frag descriptor    */
    /* FragPtr is pointing at the last filled fragment descriptor    */
    /* NextFragPtr is pointing at next available fragment descriptor */
    /* FragPtr->pNext is pointing at NextFragPtr's physical address  */
    /* However, first check for an excessively long fragment list    */
    if (pDSAParams->NumInputFragments > UBSEC_MAX_FRAGMENTS)
      return(UBSEC_STATUS_INVALID_PARAMETER); 

    FragPtr=NextFragPtr;
    PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->Random.KeyValue)); 
 #if defined(UBS_ENABLE_KEY_SWAP)
    longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->Random.KeyValue);
    for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDSAParams->Random.KeyLength)/32 ; element++) 
      longkey[element] = BYTESWAPLONG(longkey[element]);
 #endif /* UBS_ENABLE_KEY_SWAP */
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
    DataLength=(pDSAParams->Random.KeyLength+7)/8; /* map the length from bits to bytes */
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    fragnum++; /* Increment for upcoming OS_SyncToDevice() call */
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupSignParams Sync Random Fragment to Device (0x%08X,%d,%d)\n", 
			      pDSAParams->Random.KeyValue,
			      0,
			      DataLength));
    OS_SyncToDevice(pDSAParams->Random.KeyValue,
		    0,
		    DataLength);
    Dbg_Print(DBG_DSAKEY,( " DSA Random Num Fragment: <%d,%08x (%08x)>\n",
		DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));

  }
  else {
    /* The random number will be generated by the CryptoNet chip.   */
    /* No additional input fragment descriptor required.            */
    pDSACtx->rng_enable=CPU_TO_CTRL_SHORT(1);
    pDSAParams->Random.KeyLength = 
      ROUNDUP_TO_32_BIT(pDSAParams->Random.KeyLength);
  }

  FragPtr->pNext = 0; /* Terminate the input fragment descriptor list */
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- RNG_Enabled [%d]\n",pDSACtx->rng_enable));

#ifndef STATIC_F_LIST
  /* Fragment lists are external to MCR structures, must sync separately */
  if (fragnum > 1) { /* We're using at least one InputFragmentList entry */
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupSignParams Sync %d IFrag Descriptor(s) to Device (0x%08X,%d,%d)\n", fragnum-1, pMCR->InputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      (fragnum-1)*sizeof(DataBufChainList_t)));
    OS_SyncToDevice(pMCR->InputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    (fragnum-1)*sizeof(DataBufChainList_t));
  }
#endif /* STATIC_F_LIST not defined */

  /* Now setup the output fragment descriptor list. Always 2 fragments */

  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->SigR.KeyValue));
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDSAParams->SigR.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

        /* get the next fragment pointer */
  NextFragPtr=&pMCR->OutputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
  FragPtr->pNext =NextFragPtr->PhysicalAddress;
  Dbg_Print(DBG_DSAKEY,( "DSA Sign Sig_R: <%d,%08x, (%08x,Next-%08x)>\n",
      DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));
  FragPtr=NextFragPtr;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->SigS.KeyValue));
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDSAParams->SigS.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  FragPtr->pNext = 0;

  Dbg_Print(DBG_DSAKEY,( "DSA Sign Sig_S: <%d,%08x, (%08x,Next-%08x)>\n",
      DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

#ifndef STATIC_F_LIST
  /* Fragment lists are external to MCR structures, must sync separately */
  /* Always 2 output frags, need to sync one entry in OutputFragmentList */
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupSignParams Sync OFrag Descriptor to Device (0x%08X,%d,%d)\n", pMCR->OutputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      sizeof(DataBufChainList_t)));
  OS_SyncToDevice(pMCR->OutputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    sizeof(DataBufChainList_t));
#endif /* STATIC_F_LIST not defined */

#ifndef UBSEC_HW_NORMALIZE
  pMCR->KeyContextList[PacketIndex]->NormBits=0;
#endif
  pMCR->KeyContextList[PacketIndex]->ResultKey[0] = &pDSAParams->SigR; /* Save for post-processing callback */
  pMCR->KeyContextList[PacketIndex]->ResultKey[1] = &pDSAParams->SigS; /* Save for post-processing callback */
  pMCR->KeyContextList[PacketIndex]->ResultRNG = NULL; /* Not used */
  return(UBSEC_STATUS_SUCCESS);
#else /* UBSEC_DSA_SUPPORT not defined */
    return(UBSEC_STATUS_NO_DEVICE);
#endif
} /* end DSA_SetupSignParams() */



/* 
 * Function: DSA_SetupVerifyParams()
 * Set up KeyContext Buffer, MCR Input Buffer and MCR Output Buffer 
 * for DSA Verify operation with parameters provided by pDSAParams 
 */

ubsec_Status_t 
DSA_SetupVerifyParams(MasterCommand_pt pMCR, ubsec_DSA_Params_pt pDSAParams)
{
#ifdef UBSEC_DSA_SUPPORT
  volatile DataBufChainList_t   *FragPtr, *NextFragPtr;
  int                            DataLength;
  volatile Packet_t 		*pPacket;
  VOLATILE DSA_Verify_CtxCmdBuf_t	*pDSACtx;
  int                  PacketIndex;
  int fragnum; 
  int Offset;
  int element;
  int NormBits,NormalizeLen;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  /* First do a sanity check for an excessively long input fragment list */
  if (pDSAParams->NumInputFragments > (UBSEC_MAX_FRAGMENTS - 1))
    return(UBSEC_STATUS_INVALID_PARAMETER); 

  PacketIndex = pMCR->NumberOfPackets;
  pDSACtx = &pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.DSA_Verify_CtxCmdBuf;
  RTL_MemZero(pDSACtx,sizeof(*pDSACtx));

  pDSACtx->sha1_enable= pDSAParams->HashEnable ? CPU_TO_CTRL_SHORT(1) : 0;
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet */

  pDSACtx->p_length = (unsigned short)CPU_TO_CTRL_SHORT(pDSAParams->ModP.KeyLength) ;

  if (pDSAParams->ModP.KeyLength <=512)
    NormalizeLen=512;
  else
    if (pDSAParams->ModP.KeyLength <= 768)
      NormalizeLen=768;
    else
    if (pDSAParams->ModP.KeyLength  <= 1024)
      NormalizeLen=1024;
    else
#ifdef UBSEC_582x_CLASS_DEVICE
	if (pDSAParams->ModP.KeyLength <=1536)
	  NormalizeLen=1536;
	else
	  NormalizeLen=2048;
#else
        return(UBSEC_STATUS_INVALID_PARAMETER);
#endif

#ifndef UBSEC_HW_NORMALIZE
  /*
   * Q Needs to be normalized on 160 bits.
   * P & G need to be shifted the same amount
   */
  NormBits = ubsec_NormalizeDataTo(&pDSAParams->ModQ,160);
  NormBits=ubsec_NormalizeDataTo(&pDSAParams->ModP, NormalizeLen); 
  ubsec_ShiftData(&pDSAParams->BaseG, NormBits); 
  if (NormBits)
    ubsec_ShiftData(&pDSAParams->Key, NormBits); 
#else
  NormBits=0;
#endif
  NormalizeLen/=8;
  Offset=0;

  /* Pad out those parameters that need it. */
  pDSAParams->SigS.KeyLength = ROUNDUP_TO_32_BIT(pDSAParams->SigS.KeyLength);
  pDSAParams->SigR.KeyLength = ROUNDUP_TO_32_BIT(pDSAParams->SigR.KeyLength);

  /* q setup, Always 160 Bits. */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	     20/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[0],OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),20);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->ModQ.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[0],OS_GetVirtualAddress(pDSAParams->ModQ.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->ModQ.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=20;

  /* p setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	     NormalizeLen/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	      NormalizeLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->ModP.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->ModP.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->ModP.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=NormalizeLen;

  /* g setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	     NormalizeLen/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	      NormalizeLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->BaseG.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->BaseG.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->BaseG.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=NormalizeLen;

  /* Y setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	     NormalizeLen/4);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	      NormalizeLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pDSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	     ROUNDUP_TO_32_BIT(pDSAParams->Key.KeyLength)/32);
 #else
  RTL_Memcpy( &pDSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pDSAParams->Key.KeyValue),
	      ROUNDUP_TO_32_BIT(pDSAParams->Key.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=NormalizeLen;

  /* Set total length */
  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=Offset;

#ifdef UBSDBG
  /* Print out the context information if required */
  {
  int WordLen,i;
  WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-DSA_STATIC_SIGN_CONTEXT_SIZE)/4;
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- DSA Verify Mod P Length [%d] \n",pDSACtx->p_length)); 
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- DSA SHA Enabled  [%d]\n",pDSACtx->sha1_enable));
  Dbg_Print(DBG_DSAKEY,(   "ubsec:  ---- Context Len %d Value -\n[",
			   pMCR->KeyContextList[PacketIndex]->cmd_structure_length )); 

  for ( i=0 ; i < WordLen ; i++) {
    Dbg_Print(DBG_DSAKEY,( "%08x ",SYS_TO_BE_LONG(pDSACtx->CtxParams[i])));
  }
  Dbg_Print(DBG_DSAKEY,( "]\n"));
  }
#endif

  /* Input Buffer(s) setup for DSA Verify */
  pPacket->PacketLength = 0;
  /* The DSA message is a bytestream. Treat it just like a crypto buffer. */
  NextFragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  for (fragnum=0;fragnum<(int)pDSAParams->NumInputFragments;fragnum++) {
    FragPtr = NextFragPtr;
    PhysAddr=pDSAParams->InputFragments[fragnum].FragmentAddress;
    DataLength=pDSAParams->InputFragments[fragnum].FragmentLength;
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    /* Add (endian-adjusted) fragment length into MCR structure */
    pPacket->PacketLength += (unsigned short)DataLength;
    if (fragnum==0) { /* Next frag descriptor is packet's InputFragmentList */
      NextFragPtr=&pMCR->InputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
    }
    else { /* Next frag descriptor is next InputFragmentList entry */
      NextFragPtr=&FragPtr[1]; 
    }
    FragPtr->pNext = NextFragPtr->PhysicalAddress;
    
    Dbg_Print(DBG_DSAKEY,( "DSA Verify InputKeyInfo : IFrag[%d] <%d,%08x %08x>\n",
			   fragnum, DataLength, 
			   CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr)); 
  } /* for each input fragment */
  
  /* ->PacketLength is only for the message 'm'; it does not  */
  /* count the sizes of the fragments used for 'R' and 'S'.   */    
  /* Therefore, we're finished updating ->PacketLength.       */

#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 
  /* fix up the packet length endianess in DMA control memory */
  pPacket->PacketLength = CPU_TO_CTRL_SHORT( pPacket->PacketLength );
#endif

  /* Here with FragPtr pointing at last filled fragment descriptor */
  /* NextFragPtr is pointing at next available fragment descriptor */
  /* FragPtr->pNext is pointing at NextFragPtr's physical address  */

  /* Now setup R */
  FragPtr=NextFragPtr;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->SigR.KeyValue));
 #if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->SigR.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDSAParams->SigR.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
 #endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDSAParams->SigR.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

  /* Get and goto the next fragment */
  NextFragPtr=&FragPtr[1];
  FragPtr->pNext =NextFragPtr->PhysicalAddress;
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupVerifyParams Sync SigR Fragment to Device (0x%08X,%d,%d)\n", 
			    pDSAParams->SigR.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pDSAParams->SigR.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_DSAKEY,( "DSA Verify Sig_R: <%d,%08x, (%08x,Next-%08x)>\n",
      DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

  FragPtr=NextFragPtr;
  /* Set up S */
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->SigS.KeyValue));
 #if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pDSAParams->SigS.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pDSAParams->SigS.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
 #endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDSAParams->SigS.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupVerifyParams Sync SigS Fragment to Device (0x%08X,%d,%d)\n", 
			    pDSAParams->SigS.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pDSAParams->SigS.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_DSAKEY,( "DSA Verify Sig_S: <%d,%08x, (%08x,Next-%08x)>\n",
      DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

  FragPtr->pNext = 0; /* Terminate the InputFragmentList. */

#ifndef STATIC_F_LIST
  /* Fragment lists are external to MCR structures, must sync separately */
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: DSA_SetupSignParams Sync %d IFrag Descriptor(s) to Device (0x%08X,%d,%d)\n", fragnum+1, pMCR->InputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      (fragnum+1)*sizeof(DataBufChainList_t)));
  OS_SyncToDevice(pMCR->InputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    (fragnum+1)*sizeof(DataBufChainList_t));
#endif /* STATIC_F_LIST not defined */

  /* Output Buffers setup for DSA Verify. Always only one fragment */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDSAParams->V.KeyValue));
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pDSAParams->V.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  FragPtr->pNext = 0;

  Dbg_Print(DBG_DSAKEY,( "DSA Verify V: <%d,%08x, (%08x,Next-%08x)>\n",
      DataLength,CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr,FragPtr->pNext));

  /* The (only) Output Fragment descriptor (inside the MCR packet struct) */
  /* will get sync'd to the CryptoNet device when the MCR gets sync'd.    */

#ifndef UBSEC_HW_NORMALIZE
  pMCR->KeyContextList[PacketIndex]->NormBits=0;
#endif

  pMCR->KeyContextList[PacketIndex]->ResultKey[0] = &pDSAParams->V; /* Save for post-processing callback */
  pMCR->KeyContextList[PacketIndex]->ResultKey[1] = NULL; /* Not used */
  pMCR->KeyContextList[PacketIndex]->ResultRNG = NULL; /* Not used */

  return(UBSEC_STATUS_SUCCESS);
#else
    return(UBSEC_STATUS_NO_DEVICE);
#endif
} /* end DSA_SetupVerifyParams() */




#endif /* UBSEC_PKEY_SUPPORT */



