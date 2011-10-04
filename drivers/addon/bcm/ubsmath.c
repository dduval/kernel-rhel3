
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
 * ubsmath.c: Math function acceleration functions
 *
 * Revision History:
 *
 * 03/17/2000 SOR Created
 * 07/06/2000 DPA Fixes for SMP operation
 * 07/26/00 SOR Virtual/Physical Memory manipulation modifications
 * 04/20/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

/*
 * ubsec_MathCommand: Process a list of Math commands.
 *
 * Immediate Status is returned. Completion status is returned
 * on a per command callback
 */
ubsec_Status_t 
ubsec_MathCommand(ubsec_DeviceContext_t Context,
	      ubsec_MathCommandInfo_pt pCommand,
	      int *NumCommands)
{
  DeviceInfo_pt 		pDevice=(DeviceInfo_pt)Context;
  VOLATILE MasterCommand_t  	*pMCR;
  VOLATILE Packet_t         	*pPacket;
  VOLATILE KeyContext_t  	*pContext;
  VOLATILE int             	PacketIndex;
  int 				CommandIndex=0;
  int 				CommandCount=*NumCommands;
  ubsec_Status_t 		Status;
  unsigned long 		SaveConfig;
  ubsec_MathCommandParams_pt    pParams;
  VOLATILE Math_CtxCmdBuf_t     *pMathContext;
  int offset;
  VOLATILE DataBufChainList_t   *FragPtr, *NextFragPtr;
  int DataLength;
  int NormalizeLen,NrmBits=0;
  int element;
  UBS_UINT32 *longkey;
  ubsec_MemAddress_t PhysAddr;
  CallBackInfo_pt pCompletionContext;

  if (!UBSEC_IS_KEY_DEVICE(pDevice)) {
    Dbg_Print(DBG_FATAL,( "ubsec: Math Command for a crypto device\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }

  Dbg_Print(DBG_MATH,( "ubsec:  Math command %d ",*NumCommands ));
  /*
   * Check some parameters
   */    
  if(pDevice==NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,( "NO DEV\n " ));
    return(UBSEC_STATUS_NO_DEVICE );
  }
  Dbg_Print(DBG_MATH,( "\n"));

  if (OS_EnterCriticalSection(pDevice,SaveConfig)) {
    return(UBSEC_STATUS_DEVICE_BUSY);
  }

  /* Get the next MCR to load */
 Get_New_MCR:
  *NumCommands=CommandIndex; /* Update number completed */

  if ((pMCR=GetFreeMCR(pDevice,UBSEC_KEY_LIST,&Status))== NULL_MASTER_COMMAND) 
    goto Error_Return;

  /* Add packets to this MCR. */

  Dbg_Print(DBG_MATH,( "ubsec: mcr_index %d MCR <%0x,%0x>\n",pMCR->Index,pMCR,pMCR->MCRPhysicalAddress));
  /* Initialize the packet information */
  PacketIndex = pMCR->NumberOfPackets; 
  pPacket = &(pMCR->PacketArray[PacketIndex]); /* Set up the current packet. */
  pContext = pMCR->KeyContextList[PacketIndex]; 
  pMathContext=&pContext->CtxCmdBuf.Math_CtxCmdBuf;
  Status=UBSEC_STATUS_SUCCESS; /* Wishful thinking? */


  Dbg_Print(DBG_MATH,( "ubsec: PacketIndex %d \n",pMCR->NumberOfPackets));

  /* Process all the commands in the command list. */
  for (; CommandIndex < CommandCount ; CommandIndex++) { /* Add all the packets to the MCR*/
    if( PacketIndex >= MCR_MAXIMUM_PACKETS ) {
      Dbg_Print(DBG_MATH,( "ubsec:  overran mcr buffer. %d\n",PacketIndex,CommandIndex ));
      /* 
       * We have filled this MCR. 
       * Advance next free. Wrap around if necessary
       */
      pDevice->NextFreeMCR[UBSEC_KEY_LIST]=(MasterCommand_pt)pMCR->pNextMCR;
      Dbg_Print(DBG_MATH,( "ubsec:  PushMCR ..." ));
      PushMCR(pDevice); /* Get it going (pipeline) */
      goto Get_New_MCR; /* Try to add to the next MCR */
    }

    pCompletionContext=(CallBackInfo_pt)&pMCR->CompletionArray[PacketIndex];

    /* First set up the command type and parameters. */
    Dbg_Print(DBG_MATH,( "ubsec: Math Command packet_Index %d, Context Buf <%0x,%0x>\n",PacketIndex,pContext,pContext->PhysicalAddress ));
    pPacket->PacketContextBuffer=pContext->PhysicalAddress;
    
    switch (pCommand->Command) {
    case UBSEC_MATH_MODADD :
      pContext->operation_type	= OPERATION_MOD_ADD;
      break;
    case UBSEC_MATH_MODSUB :
      pContext->operation_type	= OPERATION_MOD_SUB;
      break;
    case UBSEC_MATH_MODMUL :
      pContext->operation_type	= OPERATION_MOD_MULT;
      break;
    case UBSEC_MATH_MODEXP :
      pContext->operation_type	= OPERATION_MOD_EXPON;
      break;
    case UBSEC_MATH_MODREM :
      pContext->operation_type	= OPERATION_MOD_REDUCT;
      break;
#if defined(UBSEC_582x)
    case UBSEC_MATH_DBLMODEXP :
      /* DBLMODEXP supported in "582x mode" driver for BCM5821 and later chips only */
      if (pDevice->DeviceID < BROADCOM_DEVICE_ID_5821) {
	Status=(UBSEC_STATUS_INVALID_PARAMETER);
	goto Error_Return;
      }
      pContext->operation_type	= OPERATION_MOD_DBLEXP;
      break;
#endif /* UBSEC_582x */

    default:
      Status=(UBSEC_STATUS_INVALID_PARAMETER);
      goto Error_Return;
    }

    pParams=&pCommand->Parameters;

    /* Clear the context. */
    RTL_MemZero(pMathContext,sizeof(Math_CtxCmdBuf_t));

    pContext->cmd_structure_length= MATH_STATIC_CONTEXT_SIZE;

    if ( pCommand->Command != UBSEC_MATH_DBLMODEXP) {
      /* The modulus needs to be aligned on a 512/768 or 1024 bit boundary. */
      /*
       * Save amount to normalize/renormalize.
       */
      if (pParams->ModN.KeyLength <=512)
	NormalizeLen=512;
      else if (pParams->ModN.KeyLength <=768)
	NormalizeLen=768;
      else if (pParams->ModN.KeyLength <=1024)
	NormalizeLen=1024;
#ifdef UBSEC_582x_CLASS_DEVICE
      else if (pParams->ModN.KeyLength <=1536)
	NormalizeLen=1536;
      else
	NormalizeLen=2048;
#else
      else
	return(UBSEC_STATUS_INVALID_PARAMETER);
#endif
    } /* end non-DBLMODEXP modulus size alignment block */
    else { 
      /* DBLMODEXP operation */
      NormalizeLen=512; /* The DBLMODEXP moduli must both be 512 bits (or fewer) long */
    }

#ifndef UBSEC_HW_NORMALIZE
    if ((NrmBits = ubsec_NormalizeDataTo(&pParams->ModN,NormalizeLen))) {
      Dbg_Print(DBG_FATAL,("ubsec: MATH NrmBits %d\n",NrmBits));
      ubsec_ShiftData(&pParams->ParamA, NrmBits);
    }

    pMCR->KeyContextList[PacketIndex]->NormBits=NrmBits;
#else
    NrmBits=0;
#endif
    pMCR->KeyContextList[PacketIndex]->ResultKey[0]=&pParams->Result; /* Save here for post-command finishing */
    if (pCommand->Command == UBSEC_MATH_DBLMODEXP) {
      pMCR->KeyContextList[PacketIndex]->ResultKey[1]=&pParams->Result2; /* Save here for post-command finishing */
    }
    else {
      pMCR->KeyContextList[PacketIndex]->ResultKey[1]=NULL; /* Not used */
    }
    pMCR->KeyContextList[PacketIndex]->ResultRNG=NULL; /* Not used */

    /*
     * Output value may need to be rounded up to represent an integral
     * number of 32 bit words, same total length as modulus N.
     */

    /* N Copy the modulo value modulo */
    pMathContext->modulus_length = (unsigned short)CPU_TO_CTRL_SHORT(pParams->ModN.KeyLength);
    offset=NormalizeLen/8;

#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
    copywords((UBS_UINT32 *)&pMathContext->NE[0],
	      (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ModN.KeyValue),
	      offset/4);
 #else
    RTL_Memcpy(&pMathContext->NE[0],OS_GetVirtualAddress(pParams->ModN.KeyValue),offset);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
    copywords((UBS_UINT32 *)&pMathContext->NE[0],
	      (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ModN.KeyValue),
	      ROUNDUP_TO_32_BIT(pParams->ModN.KeyLength)/32);
 #else
    RTL_Memcpy(&pMathContext->NE[0],OS_GetVirtualAddress(pParams->ModN.KeyValue),
	       ROUNDUP_TO_32_BIT(pParams->ModN.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif

    /* Update the total context length to reflect the modulus. */
    pContext->cmd_structure_length+=(offset);

    if (pCommand->Command==UBSEC_MATH_DBLMODEXP) {

      /* Second modulus needs to be copied to the context for dblmodexp */
#ifndef UBSEC_HW_NORMALIZE
 #if defined(UBS_ENABLE_KEY_SWAP)
      copywords((UBS_UINT32 *)&pMathContext->NE[NormalizeLen/32],
		(UBS_UINT32 *)OS_GetVirtualAddress(pParams->ModN2.KeyValue),
		offset/4);
 #else
      RTL_Memcpy(&pMathContext->NE[NormalizeLen/32],OS_GetVirtualAddress(pParams->ModN2.KeyValue),offset);
 #endif /* UBS_ENABLE_KEY_SWAP */
#else
 #if defined(UBS_ENABLE_KEY_SWAP)
      copywords((UBS_UINT32 *)&pMathContext->NE[NormalizeLen/32],
		(UBS_UINT32 *)OS_GetVirtualAddress(pParams->ModN2.KeyValue),
		ROUNDUP_TO_32_BIT(pParams->ModN2.KeyLength)/32);
 #else
      RTL_Memcpy(&pMathContext->NE[NormalizeLen/32],OS_GetVirtualAddress(pParams->ModN2.KeyValue),
		 ROUNDUP_TO_32_BIT(pParams->ModN2.KeyLength)/8);
 #endif /* UBS_ENABLE_KEY_SWAP */
#endif

      /* Update the total context length to reflect the second modulus. */
      pContext->cmd_structure_length+=(offset);

      /* Second modulus' length needs to be present in context for dblmodexp  */
      /* Place the two lengths in their proper context fields (see datasheet) */

      pMathContext->exponent_length = pMathContext->modulus_length;
      pMathContext->modulus_length = 
	(unsigned short)CPU_TO_CTRL_SHORT(pParams->ModN2.KeyLength); 

    } /* end second modulus copy for dblmodexp */


    if (pCommand->Command==UBSEC_MATH_MODREM) {
      /* Message length needs to be present in context for modrem */
      pMathContext->exponent_length=
	(unsigned short)CPU_TO_CTRL_SHORT(pParams->ParamA.KeyLength);
    }
    else if (pCommand->Command==UBSEC_MATH_MODEXP) {
      /* Exponent length needs to be present in context for modexp */
      pMathContext->exponent_length= 
	(unsigned short)CPU_TO_CTRL_SHORT(pParams->ParamB.KeyLength); 
    }
    /* Otherwise leave pMathContext->exponent_length field 0 (reserved) */
    

#ifdef UBSDBG
  /* Print out the context information if required */
  {
    int WordLen,i;
    WordLen=(pContext->cmd_structure_length-MATH_STATIC_CONTEXT_SIZE)/4;
    Dbg_Print(DBG_MATH,(   "ubsec:  ---- DH Math Modulus Length = %d, Exponent Length = %d\n",
			   CTRL_TO_CPU_SHORT(pMathContext->modulus_length),
			   CTRL_TO_CPU_SHORT(pMathContext->exponent_length))); 
    Dbg_Print(DBG_MATH,(   "Context Len %d Context Value=[",
			    (pContext->cmd_structure_length))); 
    for ( i=0 ; i < WordLen ; i++) {
      Dbg_Print(DBG_MATH,( "%08x ",SYS_TO_BE_LONG(pMathContext->NE[i])));
    }
    Dbg_Print(DBG_MATH,( "]\n"));
    }
#endif


    /**************************************************/
    /* ParamA setup (all ops). Input buffer fragment. */
    /**************************************************/


    /* Now do the Input parameter A (1st input buffer). All math ops have one. */
    FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
    PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->ParamA.KeyValue)); 
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
#if defined(UBS_ENABLE_KEY_SWAP)
    longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ParamA.KeyValue);
    for (element = 0 ; element < ROUNDUP_TO_32_BIT(pParams->ParamA.KeyLength)/32 ; element++) 
      longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
    DataLength=NormalizeLen/8;
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync ParamA Fragment to Device (0x%08X,%d,%d)\n", 
			      pParams->ParamA.KeyValue,
			      0,
			      DataLength));
    OS_SyncToDevice(pParams->ParamA.KeyValue,
		    0,
		    DataLength);
    Dbg_Print(DBG_MATH,( "Input Param 1: <%d,%08x (%08x)>\n",
			 DataLength, 
			 CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));
    /* End ParamA input fragment setup */    


    /****************************************************************/
    /* ParamB setup (all ops except MODREM). Input buffer fragment. */
    /****************************************************************/


    if (pCommand->Command != UBSEC_MATH_MODREM) {
#ifndef UBSEC_HW_NORMALIZE
      ubsec_ShiftData(&pParams->ParamB, NrmBits);
#endif
      /* get the next fragment pointer */
      NextFragPtr=&pMCR->InputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
      FragPtr->pNext =NextFragPtr->PhysicalAddress;
      FragPtr=NextFragPtr;
      /* The second input data buffer has 2nd math parameter (B or E) */
      PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->ParamB.KeyValue)); 
      FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
#if defined(UBS_ENABLE_KEY_SWAP)
      longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ParamB.KeyValue);
      for (element = 0 ; element < ROUNDUP_TO_32_BIT(pParams->ParamB.KeyLength)/32 ; element++) 
	longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
#if 0
      DataLength=ROUNDUP_TO_32_BIT(pParams->ParamB.KeyLength+NrmBits);
#else
      if (pCommand->Command==UBSEC_MATH_MODEXP) {
	/* Exponent parameter fragment sizes need to be byte multiples only */
	DataLength = (pParams->ParamB.KeyLength + 7)/8;
      }
      else {
	/* Non-exponent parameter fragment sizes need to be equal to the "key size" (in bytes) */
	DataLength = NormalizeLen/8;
      }
#endif
      FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync ParamB Fragment to Device (0x%08X,%d,%d)\n", 
				pParams->ParamB.KeyValue,
				0,
				DataLength));
      OS_SyncToDevice(pParams->ParamB.KeyValue,
		      0,
		      DataLength);
      Dbg_Print(DBG_MATH,( "Input Param 2: <%d,%08x (%08x)>\n",
			 DataLength, 
			 CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));

      if (pCommand->Command != UBSEC_MATH_DBLMODEXP) {
	/* No more input fragments */
	FragPtr->pNext=0; /* Terminate the input fragment descriptor list */
#ifndef STATIC_F_LIST
	/* Sync single fragment descriptor (external to MCR) */
	FragPtr->pNext=0; /* Terminate the input fragment descriptor list before syncing. */
	Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync IFrag Descriptor to Device (0x%08X,%d,%d)\n", 
				  pMCR->InputFragmentListHandle,
				  PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
				  sizeof(DataBufChainList_t)));
	OS_SyncToDevice(pMCR->InputFragmentListHandle,
			PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
			sizeof(DataBufChainList_t));
#endif
      }

    } 
    /* End ParamB input fragment setup */

    if (pCommand->Command == UBSEC_MATH_DBLMODEXP) {


      /*********************************************************/
      /* ParamC setup (DBLMODEXP only). Input buffer fragment. */
      /*********************************************************/


      /* get the next fragment pointer */
      NextFragPtr=&FragPtr[1];
      FragPtr->pNext =NextFragPtr->PhysicalAddress;
      FragPtr=NextFragPtr;
      /* The third input data buffer has 2nd DBLMODEXP base (A1) */
      PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->ParamC.KeyValue)); 
      FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
#if defined(UBS_ENABLE_KEY_SWAP)
      longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ParamC.KeyValue);
      for (element = 0 ; element < ROUNDUP_TO_32_BIT(pParams->ParamC.KeyLength)/32 ; element++) 
	longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
      DataLength = NormalizeLen/8;
      FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync ParamC Fragment to Device (0x%08X,%d,%d)\n", 
				pParams->ParamC.KeyValue,
				0,
				DataLength));
      OS_SyncToDevice(pParams->ParamC.KeyValue,
		      0,
		      DataLength);
      Dbg_Print(DBG_MATH,( "Input Param 3: <%d,%08x (%08x)>\n",
			 DataLength, 
			 CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));

      /* End ParamC input fragment setup */


      /*********************************************************/
      /* ParamD setup (DBLMODEXP only). Input buffer fragment. */
      /*********************************************************/


#ifndef UBSEC_HW_NORMALIZE
      ubsec_ShiftData(&pParams->ParamD, NrmBits);
#endif
      /* get the next fragment pointer */
      NextFragPtr=&FragPtr[1];
      FragPtr->pNext =NextFragPtr->PhysicalAddress;
      FragPtr=NextFragPtr;
      /* The fourth input data buffer has 2nd DBLMODEXP exponent (E1) */
      PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->ParamD.KeyValue)); 
      FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
#if defined(UBS_ENABLE_KEY_SWAP)
      longkey = (UBS_UINT32 *)OS_GetVirtualAddress(pParams->ParamD.KeyValue);
      for (element = 0 ; element < ROUNDUP_TO_32_BIT(pParams->ParamD.KeyLength)/32 ; element++) 
	longkey[element] = BYTESWAPLONG(longkey[element]);
#endif /* UBS_ENABLE_KEY_SWAP */
      DataLength = NormalizeLen/8;
      FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync ParamD Fragment to Device (0x%08X,%d,%d)\n", 
				pParams->ParamD.KeyValue,
				0,
				DataLength));
      OS_SyncToDevice(pParams->ParamD.KeyValue,
		      0,
		      DataLength);
      Dbg_Print(DBG_MATH,( "Input Param 4: <%d,%08x (%08x)>\n",
			 DataLength, 
			 CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));

      /* End ParamD input fragment setup */

      FragPtr->pNext=0; /* Terminate the input fragment descriptor list. */

#ifndef STATIC_F_LIST
      /* sync the three fragment descriptors for ParamB, ParamC and ParamD */
      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync IFrag Descriptors to Device (0x%08X,%d,%d)\n", 
				pMCR->InputFragmentListHandle,
				PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
				3*sizeof(DataBufChainList_t)));
      OS_SyncToDevice(pMCR->InputFragmentListHandle,
		      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
		      3*sizeof(DataBufChainList_t));
#endif
    } /* end ParamC and ParamD setup (DBLMODEXP only) */

    /* (At this point, the input fragment descriptor list has been terminated already) */



    /* Now do the Output data buffer. All operations have at least one */

    FragPtr=(DataBufChainList_pt)&pPacket->OutputHead;
    PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->Result.KeyValue)); 
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );

    DataLength=NormalizeLen/8;

    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

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

    if (pCommand->Command == UBSEC_MATH_DBLMODEXP) {
      NextFragPtr=&pMCR->OutputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
      FragPtr->pNext =NextFragPtr->PhysicalAddress;
      FragPtr =NextFragPtr;
      PhysAddr=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pParams->Result2.KeyValue)); 
      FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)PhysAddr );
      DataLength=NormalizeLen/8;
      FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

#ifndef STATIC_F_LIST
      FragPtr->pNext=0; /* Terminate the output fragment descriptor list before syncing */
      Dbg_Print(DBG_FRAG_SYNC,( "ubsec: ubsec_MathCommand Sync 2nd OFrag Descriptor to Device (0x%08X,%d,%d)\n", 
				pMCR->OutputFragmentListHandle,
				PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
				sizeof(DataBufChainList_t)));
      OS_SyncToDevice(pMCR->OutputFragmentListHandle,
		      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
		      sizeof(DataBufChainList_t));
#endif

#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
      if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
	Dbg_Print(DBG_FATAL,("ubsec:MATH DBLMODEXP#########INVALID OUTPUT ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
	Status=UBSEC_STATUS_INVALID_PARAMETER;
	goto Error_Return;
      }
      if ((DataLength) & 0x03) {
	Dbg_Print(DBG_FATAL,("ubsec:MATH DBLMODEXP#########INVALID OUTPUT LENGTH %08x\n", DataLength)); 
	Status=UBSEC_STATUS_INVALID_PARAMETER;
	goto Error_Return;
      }
#endif 
    } /* end second output buffer setup (DBLMODEXP only) */

    FragPtr->pNext=0; /* Terminate the output fragment descriptor list (if not already done). */

    Dbg_Print(DBG_MATH,( "Result : <%d,%08x (%08x)>\n",
			 DataLength, 
			 CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));


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

    /* For key (math) MCRs, contexts are accessed by an array of handles. */
    /* This means that memory for each context was separately allocated.  */
    /* Therefore we must sync each context separately as it is built.     */
    Dbg_Print(DBG_CNTXT_SYNC,( "ubsec: ubsec_MathCommand Sync Context to Device (0x%08X,%d,%d)\n", pMCR->ContextListHandle[PacketIndex],
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
    pContext++;

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
#else

 Error_Return:  /* Label to make sure that IRQs are enabled. */
#ifdef COMPLETE_ON_COMMAND_THREAD
    ubsec_PollDevice(pDevice);  /* Try to complete some & cut down on ints */
#endif

#endif
    OS_LeaveCriticalSection(pDevice,SaveConfig);
    return(Status);
}
