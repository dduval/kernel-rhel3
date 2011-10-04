
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
 * ubsrsa.c: RSA parameter setup functions.
 *
 *
 * Revision History:
 *
 * May 2000 SOR Created
 * 07/26/2000 SOR Virtual/Physical Memory manipulation modifications
 * 09/xx/2000 DRE BCM5820 Support
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

#ifdef UBSEC_PKEY_SUPPORT


/* 
 * Function: RSA_SetupPublicParams()
 * Set up KeyContext Buffer, MCR Input Buffer and MCR Output Buffer 
 * for RSA Public operation with parameters provided by pRSAParams 
 * The KeyContext Buffer includes exponent_length, modulus_length,
 * N, and g
 */

ubsec_Status_t 
RSA_SetupPublicParams(MasterCommand_pt pMCR, ubsec_RSA_Params_pt pRSAParams)

     /* pRSAParams points to a structure which contains all of the info
	needed for RSA operations. In addition to regular numerical 
	parameters, "key" memory buffer locations are passed using memory 
	"handles". Handles are defined as memory descriptors from which 
	BOTH the virtual and physical addresses of the designated
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
#ifdef UBSEC_RSA_SUPPORT
  VOLATILE DataBufChainList_t   *FragPtr;
  int                            DataLength;
  VOLATILE Packet_t 		*pPacket;
  Pub_RSA_CtxCmdBuf_pt		pRSACtx;
  VOLATILE int                  PacketIndex;
  ubsec_Status_t Status=UBSEC_STATUS_SUCCESS;
  int ModNLen,NormBits;
  int element;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  PacketIndex = pMCR->NumberOfPackets;
  pRSACtx = (Pub_RSA_CtxCmdBuf_pt)&pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.Pub_RSA_CtxCmdBuf;
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet */
  RTL_MemZero(pRSACtx,sizeof(*pRSACtx));

  pRSACtx->modulus_length  = (unsigned short)CPU_TO_CTRL_SHORT(pRSAParams->ModN.KeyLength) ;
  pRSAParams->ExpE.KeyLength=ROUNDUP_TO_32_BIT(pRSAParams->ExpE.KeyLength);
  pRSACtx->exponent_length = (unsigned short) CPU_TO_CTRL_SHORT(pRSAParams->ExpE.KeyLength);

  if (pRSAParams->ModN.KeyLength <=512)
    ModNLen=512;
  else
    if (pRSAParams->ModN.KeyLength <=768)
      ModNLen=768;
    else
    if (pRSAParams->ModN.KeyLength  <= 1024)
      ModNLen=1024;
    else
#ifdef UBSEC_582x_CLASS_DEVICE
      if (pRSAParams->ModN.KeyLength <=1536)
	ModNLen=1536;
      else
	if (pRSAParams->ModN.KeyLength <= 2048)
	  ModNLen=2048;
	else
#endif
      return(UBSEC_STATUS_INVALID_PARAMETER);

#ifndef UBSEC_HW_NORMALIZE
  NormBits = ubsec_NormalizeDataTo(&pRSAParams->ModN,ModNLen);
  pRSAParams->OutputKeyInfo.KeyLength=ModNLen;
  if (NormBits) {
    /*
     * Normalize message to align with modulus.
     */
    ubsec_ShiftData(&pRSAParams->InputKeyInfo, NormBits);
    /* RJT_TEST why is this rounded up? */
    pRSAParams->InputKeyInfo.KeyLength=
      ROUNDUP_TO_32_BIT(pRSAParams->InputKeyInfo.KeyLength+NormBits);
  }
  pMCR->KeyContextList[PacketIndex]->NormBits=NormBits;
#else
  NormBits=ModNLen-pRSAParams->ModN.KeyLength;
  pRSAParams->OutputKeyInfo.KeyLength=ModNLen;
  pRSAParams->InputKeyInfo.KeyLength=ModNLen;
  pMCR->KeyContextList[PacketIndex]->NormBits=NormBits;
#endif

  pMCR->KeyContextList[PacketIndex]->ResultKey[0] = &pRSAParams->OutputKeyInfo; /* Save here for post-command finishing */ 
  pMCR->KeyContextList[PacketIndex]->ResultKey[1] = NULL; /* Not used */
  pMCR->KeyContextList[PacketIndex]->ResultRNG = NULL; /* Not used */
  ModNLen/=8;

  /* N, g setup. These parameters are copied to the CryptoNet context structure */

#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->Ng[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->ModN.KeyValue),
	     ModNLen/4);
  copywords( (UBS_UINT32 *)&pRSACtx->Ng[ModNLen/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->ExpE.KeyValue),
	     pRSAParams->ExpE.KeyLength/32);
#else /* No key swap */
  RTL_Memcpy( &pRSACtx->Ng[0],OS_GetVirtualAddress(pRSAParams->ModN.KeyValue),ModNLen);
  RTL_Memcpy( &pRSACtx->Ng[ModNLen/4],OS_GetVirtualAddress(pRSAParams->ExpE.KeyValue),
	      pRSAParams->ExpE.KeyLength/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else /* H/W normalization */
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->Ng[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->ModN.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->ModN.KeyLength)/32);
  copywords( (UBS_UINT32 *)&pRSACtx->Ng[ModNLen/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->ExpE.KeyValue),
	     pRSAParams->ExpE.KeyLength/32);
#else /* No key swap */
  RTL_Memcpy( &pRSACtx->Ng[0],OS_GetVirtualAddress(pRSAParams->ModN.KeyValue),ROUNDUP_TO_32_BIT(pRSAParams->ModN.KeyLength)/8);
  RTL_Memcpy( &pRSACtx->Ng[ModNLen/4],OS_GetVirtualAddress(pRSAParams->ExpE.KeyValue),
	      pRSAParams->ExpE.KeyLength/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif

  ModNLen+=pRSAParams->ExpE.KeyLength/8;
  /* Now set the extra length. */
  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=(ModNLen);

#ifdef UBSDBG
  /* Print out the context information if required */
  {
  int WordLen,i;
  WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-RSA_STATIC_PUBLIC_CONTEXT_SIZE)/4;
  Dbg_Print(DBG_RSAKEY,(   "ubsec:  ---- RSA Public Modulus Length [%d] Normbits [%d]\n[",
			   pRSACtx->modulus_length,NormBits)); 
  Dbg_Print(DBG_RSAKEY,(   "ubsec:  ---- RSA Public G Length [%d] Context Len %d Value-\n[",
			   CTRL_TO_CPU_SHORT(pRSACtx->exponent_length),
			   pMCR->KeyContextList[PacketIndex]->cmd_structure_length)); 
  for ( i=0 ; i < WordLen ; i++) {
    Dbg_Print(DBG_RSAKEY,( "%08x ",SYS_TO_BE_LONG(pRSACtx->Ng[i])));
  }
  Dbg_Print(DBG_RSAKEY,( "]\n"));
  }
#endif

  /* Input Buffer setup for RSA Public */
  FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress( pRSAParams->InputKeyInfo.KeyValue));
#if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->InputKeyInfo.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pRSAParams->InputKeyInfo.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pRSAParams->InputKeyInfo.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
 /* Copy (endian-adjusted) fragment length into MCR structure */
  pPacket->PacketLength = FragPtr->DataLength;
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: RSA_SetupPublicParams Sync InputKeyInfo Fragment to Device (0x%08X,%d,%d)\n", 
			    pRSAParams->InputKeyInfo.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pRSAParams->InputKeyInfo.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_RSAKEY,( "RSA Public Key, InputKeyInfo:  FragI <%d,%08x %08x>\n",
            DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
  FragPtr->pNext = 0;

  /* Output Buffer setup for RSA Public */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pRSAParams->OutputKeyInfo.KeyValue));
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pRSAParams->OutputKeyInfo.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  Dbg_Print(DBG_RSAKEY,( "RSA Public Key, OutputKeyInfo:  FragO <%d,%08x %08x>\n",
            DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
  FragPtr->pNext = 0;

  pRSAParams->OutputKeyInfo.KeyLength-=NormBits;
  return(UBSEC_STATUS_SUCCESS);
#else
    return(UBSEC_STATUS_NO_DEVICE);
#endif
}

/* 
 * Function: RSA_SetupPrivateParams()
 * Set up KeyContext Buffer, MCR Input Buffer and MCR Output Buffer 
 * for RSA Private operation with parameters provided by pRSAParams 
 * The KeyContext Buffer includes q_length,p_length, dq_length,                
 * dp_length, pinv_length, p, q, dp, dq, and  pinv 
 */
ubsec_Status_t 
RSA_SetupPrivateParams(MasterCommand_pt pMCR, ubsec_RSA_Params_pt pRSAParams)
{
#ifdef UBSEC_RSA_SUPPORT
  VOLATILE DataBufChainList_t   *FragPtr;
  int                            DataLength;
  VOLATILE Packet_t 		*pPacket;
  VOLATILE Pri_RSA_CtxCmdBuf_t	*pRSACtx;
  int                  PacketIndex;
  int Offset;
  int element;
  int ParamLen,NormBits;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;

  PacketIndex = pMCR->NumberOfPackets;
  pRSACtx = &pMCR->KeyContextList[PacketIndex]->CtxCmdBuf.Pri_RSA_CtxCmdBuf;
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet */
  RTL_MemZero(pRSACtx,sizeof(*pRSACtx));
  pRSACtx->q_length    = (unsigned short)CPU_TO_CTRL_SHORT(pRSAParams->PrimeQ.KeyLength) ;
  pRSACtx->p_length    = (unsigned short)CPU_TO_CTRL_SHORT(pRSAParams->PrimeP.KeyLength) ;

  /*
   * All parameters need to be aligned on the 
   * same length so we use the length of the 
   * largest.
   */

  /* Both P & Q Must be normalized and aligned on the largest 
     length. */
  if ((pRSAParams->PrimeQ.KeyLength > pRSAParams->PrimeP.KeyLength)) 
    ParamLen=pRSAParams->PrimeQ.KeyLength;
  else
    ParamLen=pRSAParams->PrimeP.KeyLength;
    
  if (ParamLen <=256)
    ParamLen=256;
  else
    if (ParamLen <= 384)
      ParamLen=384;
    else
      if (ParamLen <= 512)
	ParamLen=512;
      else
#ifdef UBSEC_582x_CLASS_DEVICE
	if (ParamLen <= 768)
	  ParamLen=768;
	else
	  if (ParamLen <= 1024)
	    ParamLen=1024;
	else
#endif
	  return(UBSEC_STATUS_INVALID_PARAMETER);

#ifndef UBSEC_HW_NORMALIZE
  NormBits = ubsec_NormalizeDataTo(&pRSAParams->PrimeP,ParamLen);
  if (NormBits)
    ubsec_ShiftData(&pRSAParams->PrimeEdp, NormBits);
  NormBits = ubsec_NormalizeDataTo(&pRSAParams->PrimeQ,ParamLen);
  if (NormBits) {
    ubsec_ShiftData(&pRSAParams->PrimeEdq, NormBits); 
    ubsec_ShiftData(&pRSAParams->Pinv, NormBits); 
  }

  /* Return the number of bits the result will have to be shifted by. */
  NormBits=((ParamLen*2)
	    -(pRSAParams->PrimeQ.KeyLength+pRSAParams->PrimeP.KeyLength)); 

  if (NormBits) {
    pRSAParams->OutputKeyInfo.KeyLength = pRSAParams->PrimeQ.KeyLength+pRSAParams->PrimeP.KeyLength; 
  }
  pMCR->KeyContextList[PacketIndex]->NormBits=NormBits;
#else
  NormBits=((ParamLen*2)
	    -(pRSAParams->PrimeQ.KeyLength+pRSAParams->PrimeP.KeyLength)); 

  pMCR->KeyContextList[PacketIndex]->NormBits=NormBits;
  pRSAParams->OutputKeyInfo.KeyLength=ParamLen*2;
#endif

  pRSAParams->InputKeyInfo.KeyLength = 2*ParamLen; /* Pad it out.*/
  pRSAParams->OutputKeyInfo.KeyLength=2*ParamLen;

  pMCR->KeyContextList[PacketIndex]->ResultKey[0] = &pRSAParams->OutputKeyInfo; /* Save here for post-command finishing */
  pMCR->KeyContextList[PacketIndex]->ResultKey[1] = NULL; /* Not used */
  pMCR->KeyContextList[PacketIndex]->ResultRNG = NULL; /* Not used */
  ParamLen/=8;

  /* p setup */

#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeP.KeyValue),
	     ParamLen/4);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[0],OS_GetVirtualAddress(pRSAParams->PrimeP.KeyValue),
	      ParamLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[0],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeP.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->PrimeP.KeyLength)/32);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[0],OS_GetVirtualAddress(pRSAParams->PrimeP.KeyValue),
	      ROUNDUP_TO_32_BIT(pRSAParams->PrimeP.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset=ParamLen;

  /* q setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeQ.KeyValue),
	     ParamLen/4);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeQ.KeyValue),
	      ParamLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeQ.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->PrimeQ.KeyLength)/32);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeQ.KeyValue),
	      ROUNDUP_TO_32_BIT(pRSAParams->PrimeQ.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=ParamLen;

  /* dp setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeEdp.KeyValue),
	     ParamLen/4);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeEdp.KeyValue),
	      ParamLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeEdp.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->PrimeEdp.KeyLength)/32);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeEdp.KeyValue),
	      ROUNDUP_TO_32_BIT(pRSAParams->PrimeEdp.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=ParamLen;

  /* dq setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeEdq.KeyValue),
	     ParamLen/4);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeEdq.KeyValue),
	      ParamLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->PrimeEdq.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->PrimeEdq.KeyLength)/32);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->PrimeEdq.KeyValue),
	      ROUNDUP_TO_32_BIT(pRSAParams->PrimeEdq.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif

  Offset+=ParamLen;

  /* pinv setup */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->Pinv.KeyValue),
	     ParamLen/4);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->Pinv.KeyValue),
	      ParamLen);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
  copywords( (UBS_UINT32 *)&pRSACtx->CtxParams[Offset/4],
	     (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->Pinv.KeyValue),
	     ROUNDUP_TO_32_BIT(pRSAParams->Pinv.KeyLength)/32);
 #else
  RTL_Memcpy( &pRSACtx->CtxParams[Offset/4],OS_GetVirtualAddress(pRSAParams->Pinv.KeyValue),
	      ROUNDUP_TO_32_BIT(pRSAParams->Pinv.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif
  Offset+=ParamLen;

  pMCR->KeyContextList[PacketIndex]->cmd_structure_length+=(Offset);

#ifdef UBSDBG
  /* Print out the context information if required */
  {
  int WordLen,i;
  WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-RSA_STATIC_PRIVATE_CONTEXT_SIZE)/4;
  Dbg_Print(DBG_RSAKEY,(   "ubsec:  ---- RSA Private P-Q Length [%d] P-D Length [%d] \n[",pRSACtx->q_length,pRSACtx->p_length)); 
  Dbg_Print(DBG_RSAKEY,(   "ubsec:  ---- ParamLen %d Context Len %d Value -\n[",
			   CTRL_TO_CPU_SHORT(ParamLen),pMCR->KeyContextList[PacketIndex]->cmd_structure_length )); 

  for ( i=0 ; i < WordLen ; i++) {
    Dbg_Print(DBG_RSAKEY,( "%08x ",SYS_TO_BE_LONG(pRSACtx->CtxParams[i])));
  }
  Dbg_Print(DBG_RSAKEY,( "]\n"));
  }
#endif

  /* Input Buffer setup for RSA Private */
  FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pRSAParams->InputKeyInfo.KeyValue));
#if defined(UBS_ENABLE_KEY_SWAP)
  longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pRSAParams->InputKeyInfo.KeyValue);
  for (element = 0 ; element < ROUNDUP_TO_32_BIT(pRSAParams->InputKeyInfo.KeyLength)/32 ; element++) 
    longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pRSAParams->InputKeyInfo.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
 /* Copy (endian-adjusted) fragment length into MCR structure */
  pPacket->PacketLength = FragPtr->DataLength;
  Dbg_Print(DBG_FRAG_SYNC,( "ubsec: RSA_SetupPrivateParams Sync InputKeyInfo Fragment to Device (0x%08X,%d,%d)\n", 
			    pRSAParams->InputKeyInfo.KeyValue,
			    0,
			    DataLength));
  OS_SyncToDevice(pRSAParams->InputKeyInfo.KeyValue,
		  0,
		  DataLength);
  Dbg_Print(DBG_RSAKEY,( "RSA Private Key, InputKeyInfo:  FragI <%d,%08x %08x>\n",
            DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
  FragPtr->pNext = 0;

  /* Output Buffer setup for RSA Public */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
  PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pRSAParams->OutputKeyInfo.KeyValue));
  FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
  DataLength=(pRSAParams->OutputKeyInfo.KeyLength+7)/8;
  FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
  Dbg_Print(DBG_RSAKEY,( "RSA Private Key, OutputKeyInfo:  FragO <%d,%08x %08x>\n",
            DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
  FragPtr->pNext = 0;

  pRSAParams->OutputKeyInfo.KeyLength-=NormBits;

  return(UBSEC_STATUS_SUCCESS);
#else
    return(UBSEC_STATUS_NO_DEVICE);
#endif
}


#endif /* UBSEC_PKEY_SUPPORT */ 
