
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
 * ubsstruc.c: Internal structure manipulation routines.
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 12/02/1999 DWP Modified source to handle Big Endianness
 * 12/03/1999 DWP Changed size AllocInfoSize to fix customer noticed bug
 * 12/20/1999 SOR Modifications to do all static address computations at init time
 * 07/07/2000 SOR Debug format change.
 * 07/26/2000 SOR Virtual/Physical Memory manipulation modifications
 *                NumberOfKeyMCRs addition.
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/18/2001 RJT Added support for CPU-DMA memory synchronization
 * 10/09/2001 SRM 64 bit port
 *
 */

#include "ubsincl.h"


/*
 * AllocDeviceInfo:
 *
 * Allocate and initialize the resources associated with a device.
 * Return a pointer to the initialized device structure or zero
 * if device initialization failed.
 * Also physical addresses are calculated here.  We set up the physical addresses
 * now because it is an expensive function to do at command time.
 */
DeviceInfo_pt
AllocDeviceInfo(unsigned long DeviceID,int NumberOfCipherMCRs,int NumberOfKeyMCRs,OS_DeviceInfo_t OSContext)
{

  DeviceInfo_pt    pDevice = NULL_DEVICE_INFO;
  UBS_UINT32       IListPhysicalAddress;
  UBS_UINT32       OListPhysicalAddress;
  UBS_UINT32       ContextPhysicalAddress;
  UBS_UINT32       MCRPhysicalAddress;
  MasterCommand_pt pMCR;
  MasterCommand_pt pMCR_Temp = NULL;
  unsigned long		   MCRListIndex;
#ifdef CONTIG_MCR
  unsigned int MCRAllocSize;
  OS_MemHandle_t pContigMCRBaseHandle;
#endif
  OS_MemHandle_t pGeneric;

  /* 
   * Calculate the allocation size
   * Note that crypto contexts are allocated as a block for all packets, 
   * but key contexts  have one block for each packet
   */
  long ContextAllocSize = sizeof( PacketContext_t ) * MCR_MAXIMUM_PACKETS;
  long KeyContextAllocSize = sizeof( KeyContext_t );

#ifndef STATIC_F_LIST
  int FragListAllocSize=sizeof(DataBufChainList_t)*MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS;
#endif
  unsigned DInfoAllocSize = sizeof(DeviceInfo_t);
  unsigned int i,j;

  Dbg_Print(DBG_INITS,( "ubSec: %d-%d MCRs .\n",NumberOfCipherMCRs,NumberOfKeyMCRs));

  /*
   *  Allocate memory for main block
   *  (Note later it is more efficient to allocate one big block
   *  for all memory.
   */
  if ((pDevice = OS_AllocateMemory(DInfoAllocSize))
      == NULL_DEVICE_INFO) {
    Dbg_Print(DBG_FATAL,("ubSec:  unable to allocate memory for driver object <%d>\n",DInfoAllocSize));
    return(NULL_DEVICE_INFO);
  }

  RTL_MemZero( pDevice, DInfoAllocSize ); 

  /* This is a hokey way to fix the problem of 0 MCRs  but we need at least one
     for a potential self test anyway. */
  if (NumberOfCipherMCRs)
    pDevice->NumberOfMCRs[UBSEC_CIPHER_LIST]=NumberOfCipherMCRs;
  else
    pDevice->NumberOfMCRs[UBSEC_CIPHER_LIST]=1;

  if (NumberOfKeyMCRs)
    pDevice->NumberOfMCRs[UBSEC_KEY_LIST]=NumberOfKeyMCRs;
  else
    pDevice->NumberOfMCRs[UBSEC_KEY_LIST]=1;

  /* set up the master command record array */

    /* Set up the number of MCR lists this device has */
  pDevice->DeviceID=DeviceID;
#ifdef UBSEC_PKEY_SUPPORT
  pDevice->NumberOfMCRLists = UBSEC_IS_KEY_DEVICE(pDevice) ? 2 : 1;
#else
  pDevice->NumberOfMCRLists = 1;
#endif
  pDevice->Status=UBSEC_STATUS_SUCCESS;
  pDevice->OsDeviceInfo = OSContext;

  for(MCRListIndex=0; MCRListIndex< pDevice->NumberOfMCRLists ;MCRListIndex++) {
  Dbg_Print(DBG_INITS,( "ubSec: Initializing MCR List %d.\n",MCRListIndex));

#ifdef CONTIG_MCR
  /* Allocate one big block of memory for the array of MCRs */
  /* This means there is only one MemHandle for the whole list */
  /* We'll do the sub allocations for each MCR later. */
    MCRAllocSize = sizeof( MasterCommand_t ) * pDevice->NumberOfMCRs[MCRListIndex];
    Dbg_Print(DBG_INITS_LIST,( "ubsec:  MCRAllocSize %d\n", MCRAllocSize ));
    if ((pGeneric = OS_AllocateDMAMemory(pDevice,MCRAllocSize))
	== NULL_MASTER_COMMAND) {
      Dbg_Print(DBG_FATAL,( "ubsec:  unable to allocate a lot of memory for mcrs %d.\n",MCRAllocSize));
      goto InitDeviceInfoFail;
    }
    pMCR = (MasterCommand_pt)  OS_GetVirtualAddress(pGeneric);
    RTL_MemZero( pMCR, MCRAllocSize );
    pMCR->MCRMemHandle = pContigMCRBaseHandle = pGeneric;
    MCRPhysicalAddress=(UBS_UINT32)((unsigned char*)OS_GetPhysicalAddress(pGeneric) + MCR_DMA_MEM_OFFSET);
#endif

    for( i = 0; i < pDevice->NumberOfMCRs[MCRListIndex]; i++ ) {
      Dbg_Print(DBG_INITS,( "ubSec: Initializing MCR %d.",i));

#ifndef CONTIG_MCR
      /* Do a separate memory allocation for each MCR */
      /* Each MCR then gets its own MCRMemHandle, with
	 an MCRMemHandleOffset of MCR_DMA_MEM_OFFSET. */
      /* Now do the sub allocations for this device.  */
      if ((pGeneric= OS_AllocateDMAMemory(pDevice,sizeof(*pMCR)))
	  == NULL_MASTER_COMMAND) {
	Dbg_Print(DBG_FATAL,( "ubsec:  unable to allocate all  mcrs %d.\n",i));
	pDevice->NumberOfMCRs[MCRListIndex]=i; /* To free them up */
	goto InitDeviceInfoFail;
      }
      pMCR = (MasterCommand_pt)  OS_GetVirtualAddress(pGeneric);
      RTL_MemZero( pMCR, sizeof(*pMCR));
      pMCR->MCRMemHandle=pGeneric;
      pMCR->MCRMemHandleOffset = MCR_DMA_MEM_OFFSET;
	  MCRPhysicalAddress=(UBS_UINT32)((unsigned char*)OS_GetPhysicalAddress(pGeneric) + MCR_DMA_MEM_OFFSET);
#else /* CONTIG_MCR is defined; MCRs built as a contiguous array  */
      /* There is only the one MemHandle for the entire MCR array */
      /* We'll need to calculate an offset from it for each MCR   */
      pMCR->MCRMemHandle = pContigMCRBaseHandle;
      pMCR->MCRMemHandleOffset = (i * sizeof(*pMCR)) + MCR_DMA_MEM_OFFSET;
#endif

      if (i) {
	/* Wrap around the pointer */
	pMCR_Temp->pNextMCR=pMCR;
      }
      else {
	pDevice->MCRList[MCRListIndex]=pMCR; /* First one */
      }
    /* Assume the last. It will be eventually */
      pMCR->pNextMCR=pDevice->MCRList[MCRListIndex];
      pMCR_Temp=pMCR; /* For next loop through */

      /* We'll save the MCRPhysicalAddress here. We'll pre-byteswap     */
      /* the address value (if the CPU is big-endian). This will        */
      /* eliminate the need to swap bytes (during run time) when we     */
      /* push this MCR.                                                 */
      /* Even though the value lives in CTRLMEM, its swappability is    */
      /* determined by the CPU-PCI endianess match (not CPU-CTRLMEM).   */
      /* When the MCR gets pushed, this pointer is simply read from     */
      /* here and written to the MCRn (PCI) register without using      */
      /* any CPU-CTRLMEM macros (for maximum runtime performance).      */
      /* So we'll pre-adjust its required endianess here.               */
      /*                                                                */
      /* This treatment applies only to pMCR->MCRPhysicalAddress, as    */
      /* it is the only physical address written to a device register   */
      /* by the CPU. All other physical addresses are read from memory  */
      /* by the CryptoNet device (during PCI bus master DMA accesses).  */

      pMCR->MCRPhysicalAddress = CPU_TO_PCI_LONG(MCRPhysicalAddress);

      pMCR->MCRState=0;
      pMCR->NumberOfPackets=0;
      pMCR->Index=i;

      if(!MCRListIndex) {
      Dbg_Print(DBG_INITS,( " Cipher-C"));
	/* only allocate Cipher ContextList for MCR1 */
	if ((pGeneric = OS_AllocateDMAMemory(pDevice,ContextAllocSize)) ==
	    NULL_PACKET_CONTEXT)
	  goto InitDeviceInfoFail;
	pMCR->ContextList =  (PacketContext_t *)OS_GetVirtualAddress(pGeneric);
	RTL_MemZero(pMCR->ContextList, ContextAllocSize ); 
	pMCR->ContextListHandle[0]=pGeneric;
	/* Save physical address */
	ContextPhysicalAddress=OS_GetPhysicalAddress(pMCR->ContextListHandle[0]); 
	Dbg_Print(DBG_INITS_LIST,("MCRListIndex: %d CryptC-Physical Addrs:  MCR[%08x] Context[%08x]\n",
			  MCRListIndex,
			  PCI_TO_CPU_LONG( pMCR->MCRPhysicalAddress ),
			  ContextPhysicalAddress));
    /* 
     * We want to fix up the physical addresses of static memory locations
     * so as not to do the calculation at runtime.
     */
	for (j=0; j <  MCR_MAXIMUM_PACKETS; j++){
	  pMCR->ContextList[j].PhysicalAddress=
	    CPU_TO_CTRL_LONG(ContextPhysicalAddress);
	  ContextPhysicalAddress+=(sizeof( PacketContext_t));
	}
      } /* crypto MCR */
      else {
	/* only allocate Key ContextList for MCR2 */
	Dbg_Print(DBG_INITS,( " Key-C "));
	for (j=0; j <  MCR_MAXIMUM_PACKETS; j++){
	  if ((pGeneric = OS_AllocateDMAMemory(pDevice,KeyContextAllocSize)) ==
	      NULL_KEY_CONTEXT)
	    goto InitDeviceInfoFail;
	  pMCR->ContextListHandle[j]=pGeneric;
	  pMCR->KeyContextList[j]=(KeyContext_t *)OS_GetVirtualAddress(pMCR->ContextListHandle[j]); /* Get Virtual address */
	/* Save physical address */
	  ContextPhysicalAddress=OS_GetPhysicalAddress(pMCR->ContextListHandle[j]); 
	  RTL_MemZero(pMCR->KeyContextList[j], KeyContextAllocSize ); 
	  Dbg_Print(DBG_INITS_LIST,("MCRListIndex: %d MCR %d  KeyC-Physical Addrs:  MCR[%08x] KeyContext[%08x]\n",
			    MCRListIndex,pMCR->Index,
			    PCI_TO_CPU_LONG(pMCR->MCRPhysicalAddress),
			    ContextPhysicalAddress)); 

    /* 
     * We want to fix up the physical addresses of static memory locations
     * so as not to do the calculation at runtime.
     */
	  pMCR->KeyContextList[j]->PhysicalAddress=
	    CPU_TO_CTRL_LONG(ContextPhysicalAddress);
	}
      } /* key MCR */

      Dbg_Print(DBG_INITS,( " Frag "));
#ifndef STATIC_F_LIST
      /* The Fragment Descriptor Lists will be built outside of each MCR */
      pGeneric = (DataBufChainList_pt) OS_AllocateDMAMemory(pDevice,FragListAllocSize);
      if (pGeneric == (void *) 0)
	goto InitDeviceInfoFail;
      pMCR->InputFragmentList = (DataBufChainList_pt) OS_GetVirtualAddress(pGeneric);
      RTL_MemZero(pMCR->InputFragmentList, FragListAllocSize ); 
      pMCR->InputFragmentListHandle=pGeneric;
      if ((pGeneric= (DataBufChainList_pt) OS_AllocateDMAMemory(pDevice,FragListAllocSize))
	  == (void *)0)
	goto InitDeviceInfoFail;

      pMCR->OutputFragmentList=(DataBufChainList_pt) OS_GetVirtualAddress(pGeneric);
      RTL_MemZero(pMCR->OutputFragmentList,FragListAllocSize ); 
      pMCR->OutputFragmentListHandle=pGeneric;
    /* Now set up the physical addresses of the fragment descriptor list */
    /* First get the base physical addresses. */
      IListPhysicalAddress=OS_GetPhysicalAddress(pMCR->InputFragmentListHandle); /* Save physical address */
      OListPhysicalAddress=OS_GetPhysicalAddress(pMCR->OutputFragmentListHandle); /* Save physical address */
#else /* STATIC_F_LIST is defined */
      /* The fragment lists are inside each MCR (statically allocated). */
      /* pMCR->MCRPhysicalAddress was stored in CTRLMEM using the     */
      /* CPU_TO_PCI_LONG macro. Since we need the address for numerical  */
      /* calculations, we need to use the PCI_TO_CPU_LONG macro here. */
      IListPhysicalAddress = PCI_TO_CPU_LONG(pMCR->MCRPhysicalAddress) +
	(UBS_UINT32)(&pMCR->InputFragmentList[0])-(UBS_UINT32)pMCR-
	MCR_DMA_MEM_OFFSET;
      OListPhysicalAddress = PCI_TO_CPU_LONG(pMCR->MCRPhysicalAddress) +
	(UBS_UINT32)(&pMCR->OutputFragmentList[0])-(UBS_UINT32)pMCR-
	MCR_DMA_MEM_OFFSET;
#endif /* STATIC_F_LIST */
      /* Here with IListPhysicalAddress and OListPhysicalAddress set
	 to the beginning of the Fragment Descriptor lists */
      /* Now fixup (pre-swap) the input and outputlist PhysicalAddress */
      for (j=0; j < (MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS); j++) {
	pMCR->InputFragmentList[j].PhysicalAddress=
	  CPU_TO_CTRL_LONG(IListPhysicalAddress);
	pMCR->OutputFragmentList[j].PhysicalAddress=
	  CPU_TO_CTRL_LONG(OListPhysicalAddress);
	Dbg_Print(DBG_INITS_LIST,("MCRListIndex: %d MCR-%d, I-Frag-%d Physical Addrs:%08x, O-Frag-%d Physical Addrs:%08x\n",
			    MCRListIndex,pMCR->Index,j,
			    IListPhysicalAddress,j,
			    OListPhysicalAddress));
	IListPhysicalAddress+=sizeof(DataBufChainList_t);
	OListPhysicalAddress+=sizeof(DataBufChainList_t);
      }
#ifdef CONTIG_MCR    
      pMCR++;
      MCRPhysicalAddress+=sizeof(*pMCR); /* non-swapped pointer */
#endif
      Dbg_Print(DBG_INITS,( " OK\n"));
    } /* for each MCR in current MCRList */

  /* Initialize the working pointers */
    pDevice->NextFreeMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
    pDevice->NextDeviceMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
    pDevice->NextDoneMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
  Dbg_Print(DBG_INITS,( "ubSec: NextFreeMCR[%d]=0x%08X\n",MCRListIndex,pDevice->MCRList[MCRListIndex]));
  Dbg_Print(DBG_INITS,( "ubSec: NextDeviceMCR[%d]=0x%08X\n",MCRListIndex,pDevice->MCRList[MCRListIndex]));
  Dbg_Print(DBG_INITS,( "ubSec: NextDoneMCR[%d]=0x%08X\n",MCRListIndex,pDevice->MCRList[MCRListIndex]));
  } /* for(MCRListIndex=0 .. */

  Dbg_Print(DBG_INITS,( "ubSec: Alloc Device info - init struc done\n"));
  pDevice->SelfTestMemArea=(unsigned char *)0;

  if ((pGeneric= OS_AllocateDMAMemory(pDevice,TEST_DATA_SIZE)) == 0) {
    Dbg_Print(DBG_FATAL,( "ubsec:  unable to allocate memory for BUDs\n"));
    /* Not a fatal condition if we get this far */
  }

  pDevice->SelfTestMemArea= (unsigned char *)  OS_GetVirtualAddress(pGeneric);
  pDevice->SelfTestMemAreaHandle= (unsigned char *)  pGeneric;

  Dbg_Print(DBG_INITS,( "ubSec: Device struc initialized\n"));
  return( pDevice );

 InitDeviceInfoFail:
  Dbg_Print(DBG_INITS,( "ubSec: Device Struc Alloc failed\n"));
  FreeDeviceInfo(pDevice);
  return(NULL_DEVICE_INFO);
} /* end of AllocDeviceInfo() */




/*
 * Free all resources held by a device info type structure.
 *
 * This can be called during initialization so checks must
 * be done before memory is freed.
 */
void FreeDeviceInfo(DeviceInfo_pt pDevice )
{
  unsigned long 		   i,j;
  unsigned long              MCRListIndex;
  MasterCommand_pt pMCR;
#ifndef CONTIG_MCR
  MasterCommand_pt pMCR_temp;
#endif

  Dbg_Print(DBG_INITS,( "ubSec: Free Device Info "));
 
  /* Make sure that the interrupts are disabled */
  UBSEC_DISABLE_INT(pDevice);
  if( pDevice != NULL_DEVICE_INFO)     
    for(MCRListIndex=0; MCRListIndex< pDevice->NumberOfMCRLists ;MCRListIndex++) {
      if( pDevice->MCRList[MCRListIndex] != NULL_MASTER_COMMAND) {
	for( i = 0,pMCR=pDevice->MCRList[MCRListIndex]; i < pDevice->NumberOfMCRs[MCRListIndex]; i++ ) {
	  /* for key MCRs, context list is an array of handles, but for crypto 
	     MCRs it's just one handle */
	  if (MCRListIndex) {
	    for ( j=0; j<MCR_MAXIMUM_PACKETS; j++) {
	      if  (pMCR->ContextListHandle[j])
		OS_FreeDMAMemory( pMCR->ContextListHandle[j],
				  sizeof(KeyContext_t));
	    } 
	  }
	  else  {
	      if  (pMCR->ContextListHandle[0])
		OS_FreeDMAMemory( pMCR->ContextListHandle[0],(sizeof(PacketContext_t)*MCR_MAXIMUM_PACKETS));
	  }
#ifndef STATIC_F_LIST
	  if (pMCR->InputFragmentList != NULL_DATA_CHAIN)
	    OS_FreeDMAMemory(pMCR->InputFragmentListHandle,
			     (sizeof(DataBufChainList_t)*
			      MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS));
	  if (pMCR->OutputFragmentList != NULL_DATA_CHAIN)
	    OS_FreeDMAMemory(pMCR->OutputFragmentListHandle,
			     (sizeof(DataBufChainList_t)*
			      MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS));
			     
#endif
#ifdef CONTIG_MCR
	  pMCR=pMCR->pNextMCR;
#else
	  pMCR_temp=pMCR;
	  pMCR=(MasterCommand_pt)pMCR->pNextMCR;
	  OS_FreeDMAMemory(pMCR_temp->MCRMemHandle,sizeof(*pMCR));
#endif
	  }

#ifdef CONTIG_MCR
      OS_FreeDMAMemory( pDevice->MCRList[MCRListIndex]->MCRMemHandle,
			(pDevice->NumberOfMCRs[MCRListIndex]*sizeof(*pMCR)));
#endif
	}
} /* MCRListIndex for loop */

  if (pDevice->SelfTestMemArea!=(unsigned char *)0) {
    OS_FreeDMAMemory( pDevice->SelfTestMemAreaHandle,TEST_DATA_SIZE);
  }

  if (pDevice->ControlReg[0])
    OS_UnMapIO(pDevice,(void*) pDevice->ControlReg[0] );
  OS_FreeMemory(pDevice ,sizeof(*pDevice));

  Dbg_Print(DBG_INITS,( "ubSec: Done!\n"));

} /* end of FreeDeviceInfo() */



/*
 * SetupInputFragmentList: Setup the fragment list 
 * This routine processes an input fragment list that was built outside
 * the SRL. That list's info is put into the context and fragment list 
 * (if multi-fragment) structures in the MCR structure, converted to 
 * CryptoNet device format.
 * Return error if failure.
 */
ubsec_Status_t
SetupInputFragmentList(MasterCommand_pt  pMCR,
		       Packet_pt pPacket,
		       int NumSource,
		       ubsec_FragmentInfo_pt SourceFragments)
{
  VOLATILE DataBufChainList_t *FragPtr, *NextFragPtr;
  int NumFrags,FragNum;
  int DataLength=0;
  int PacketIndex;

  Dbg_Print(DBG_CMD,( "ubsec: Num Input Frags %d \n",NumSource));

  if (!(NumFrags=NumSource)) {
    Dbg_Print(DBG_CMD,( "ubsec:  No Input fragments\n" ));
    return(UBSEC_STATUS_INVALID_PARAMETER);
  }

  /* Sanity check.*/
  if (NumFrags>(UBSEC_MAX_FRAGMENTS+1)) { 
    Dbg_Print(DBG_CMD,( "ubsec:  Too Many Input fragments\n" ));
    return(UBSEC_STATUS_INVALID_PARAMETER);
  }


  PacketIndex = pMCR->NumberOfPackets; 
  /* Initialize the Fragment pointer. First is part of the context*/
  FragPtr=(DataBufChainList_pt)&pPacket->InputHead;
  pPacket->PacketLength=0;

    /* Now create a device fragment list from those of the parameter fragments. */
  for (FragNum=0 ;NumFrags-- ;FragNum++ ) {
    FragPtr->DataAddress= CPU_TO_CTRL_LONG( (UBS_UINT32)(SourceFragments[FragNum].FragmentAddress) ); 
    DataLength= SourceFragments[FragNum].FragmentLength;
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
    pPacket->PacketLength += (unsigned short)DataLength;   /* Increment the total packet length. */

    Dbg_Print(DBG_PACKET,( "ubsec:  FragI <%d,%d,%08x %08x>\n",
			   FragNum, 
			   DataLength, 
			   CTRL_TO_CPU_LONG( FragPtr->DataAddress ), 
			   FragPtr));
      /* 
       * The first fragment info case is a special case since
       * it lives in the context structure.
       * Subsequent fragment info (if multi-fragment) is placed in
       * the InputFragmentList structure of the MCR.
       *  1) If there are extra fragments we need to jump into
       *  a new fragment list.
       */
    if (FragNum==0) {   
      /* Processing the frag info that is part of the context structure */
      if (NumFrags) { /* This is not the last fragment */
	NextFragPtr=(DataBufChainList_pt)&pMCR->InputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
	FragPtr->pNext =NextFragPtr->PhysicalAddress;
	FragPtr=NextFragPtr;
      }
      else /* Current fragment is the last fragment */
	FragPtr->pNext = 0;
    }
    else { /* We are processing non-context frag info (multifragment) */
      if (NumFrags) { /* The current fragment is not the last fragment */
	FragPtr->pNext =FragPtr[1].PhysicalAddress;
	FragPtr++;
      }
      else /* Current fragment is the last fragment */
	FragPtr->pNext = 0;
    }
  } /* end of 'for each fragment' for-loop */

#ifndef STATIC_F_LIST 
  /* The input fragment descriptor list is not inside the MCR.   */
  /* If just initialized, it needs to be synced separately.      */ 
  if (NumSource > 1) {
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: SetupInputFragmentList Sync %d Descriptors to Device (0x%08X,%d,%d)\n", NumSource-1,
	      pMCR->InputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      (NumSource-1)*sizeof(DataBufChainList_t)));
    OS_SyncToDevice(pMCR->InputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    (NumSource-1)*sizeof(DataBufChainList_t));
  }
#endif

#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 
    /* fix up the packet length endianess in DMA control memory */
  pPacket->PacketLength = CPU_TO_CTRL_SHORT( pPacket->PacketLength );
#endif 

  return(UBSEC_STATUS_SUCCESS);
} /* end of SetupInputFragmentList() */



/*
 * SetupOutputFragmentList: Setup the output fragment list 
 * This routine processes an output fragment list that was built outside
 * the SRL. That list's info is put into the context and fragment list 
 * (if multi-fragment) structures in the MCR structure, converted to 
 * CryptoNet device format.
 * Return error if failure.
 */
ubsec_Status_t
SetupOutputFragmentList(MasterCommand_pt  pMCR,
			Packet_pt pPacket,
			int NumFrags,
			ubsec_FragmentInfo_pt DestinationFragments,
			ubsec_FragmentInfo_pt pExtraFragment)
{
  VOLATILE DataBufChainList_t *FragPtr, *NextFragPtr;
  int FragNum;
  int DataLength;
  int PacketIndex;

  /* Sanity check for excessively long output fragment list */
  if ( pExtraFragment && pExtraFragment->FragmentLength && \
      (NumFrags>UBSEC_MAX_FRAGMENTS) ) { 
    Dbg_Print(DBG_PACKET,( "ubsec:  Too Many Output fragments\n" ));
    return(UBSEC_STATUS_INVALID_PARAMETER);
  }
  else if (NumFrags>(UBSEC_MAX_FRAGMENTS+1)) { 
    Dbg_Print(DBG_PACKET,( "ubsec:  Too Many Output fragments\n" ));
    return(UBSEC_STATUS_INVALID_PARAMETER);
  }

  PacketIndex = pMCR->NumberOfPackets; 
  Dbg_Print(DBG_PACKET,( "ubsec: Num Output Frags %d \n",NumFrags));

    /* First fragment is part of the context. */
  FragPtr=(DataBufChainList_pt)&pPacket->OutputHead; 

    /* Now add them all */
  for (FragNum=0 ;NumFrags-- ;FragNum++ ) {
    FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)(DestinationFragments[FragNum].FragmentAddress) ); 
    DataLength = DestinationFragments[FragNum].FragmentLength; 
    FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );

#ifdef UBSDBG
      /* Sanity check debug info for conditions that will hang the chip. */
    if ( (CTRL_TO_CPU_LONG( FragPtr->DataAddress )) & 0x03) {
      Dbg_Print(DBG_FATAL,("ubsec:  ################INVALID OUTPUT ADDRESS %08x\n", CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
      return(UBSEC_STATUS_INVALID_PARAMETER);
    }
#if 0
/* Causes problems with less than 4 byte packets */
    if ((DataLength) & 0x03) {
      Dbg_Print(DBG_FATAL,("ubsec:  ################INVALID OUTPUT LENGTH %08x\n", DataLength)); 
      return(UBSEC_STATUS_INVALID_PARAMETER);
    A
}
#endif
#endif 

    Dbg_Print(DBG_PACKET,( "ubsec: FragO <%d, %d, %08x, %08x>\n",
			   FragNum, DataLength, CTRL_TO_CPU_LONG( FragPtr->DataAddress ), FragPtr));

      /* 
       * First case is a special case since fragment is inside pPacket struct
       * If there are additional fragments we need to link into the 
       * OutputFragmentList belonging to the current pPacket.
       */
    if (FragNum==0) {   
      /* We are working on the fragment descriptor inside pPacket struct */
      /* See if there are more fragments to build */
      if ((NumFrags)) { /* need to link into current OutputFragmentList */
	NextFragPtr=(DataBufChainList_pt)&pMCR->OutputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
	FragPtr->pNext =NextFragPtr->PhysicalAddress;
	FragPtr=NextFragPtr;
      }
      else /* Only the one frag descriptor in the current pPacket struct */
	FragPtr->pNext = 0;
    }
    else { /* We are already in the OutputFragmentList */
      /* See if there are more fragments to build */
      if ((NumFrags)) { /* point to next descriptor in list */
	FragPtr->pNext =FragPtr[1].PhysicalAddress;
	FragPtr++;
      }
      else /* last fragment descriptor, terminate the list (for now) */
	FragPtr->pNext = 0;
    }
  }

  /*
   * If we are doing HMAC/authentication then we need to incorporate
   * the ExtraFragment info into the output frag descriptor list.
   * HMAC/authentication is indicated by a non-NULL pExtraFragment pointer.
   * If pExtraFragment->FragmentLength == 0, then the output 
   * length of the HMAC/auth frag is implied (fixed by CryptoNet chip design).
   * In this case, the last regular frag descriptor's pNext pointer
   * points directly to the extra fragment's data, NOT to the extra fragment 
   * descriptor structure itself.
   * Otherwise (if pExtraFragment->FragmentLength != 0), treat
   * ExtraFragment as simply another fragment descriptor.
   * Here with FragPtr pointing to last filled fragment descriptor,
   * and Fragnum indicating the next (uninitialized) frag descriptor.
   */
  if (pExtraFragment) { /* There is an extra fragment descriptor */
    if (pExtraFragment->FragmentLength==0) {
      /* Make FragPtr->pNext point to the extra fragment data buffer,
	 NOT the extra fragment descriptor itself */
      FragPtr->pNext = CPU_TO_CTRL_LONG(pExtraFragment->FragmentAddress );
      if ( (int) (CTRL_TO_CPU_LONG( FragPtr->pNext )) & 0x03) {
	Dbg_Print(DBG_PACKET,("ubsec:  ################INVALID HMAC ADDRESS %08x\n",FragPtr->pNext));
	return(UBSEC_STATUS_INVALID_PARAMETER);
      }
    }
    else { /* add the extra frag descriptor to the descriptor list */
      if (FragNum==1) {   
	NextFragPtr=(DataBufChainList_pt)&pMCR->OutputFragmentList[PacketIndex*(UBSEC_MAX_FRAGMENTS)];
	FragPtr->pNext =NextFragPtr->PhysicalAddress;
	FragPtr=NextFragPtr;
      }
      else {
	FragPtr->pNext =FragPtr[1].PhysicalAddress;
	FragPtr++;
      }
      /* Here with FragPtr pointing to the added (last) extra frag     */
      /* descriptor. Increment NumFrags for OS_SyncToDevice() below    */
      NumFrags++; 
      FragPtr->DataAddress = CPU_TO_CTRL_LONG( (UBS_UINT32)(pExtraFragment->FragmentAddress) );
      DataLength = pExtraFragment->FragmentLength;
      FragPtr->DataLength = CPU_TO_CTRL_SHORT( (unsigned short)DataLength );
      FragPtr->pNext = 0;
    }
  } /* if(pExtraFragment) */

#ifndef STATIC_F_LIST 
  /* The output fragment descriptor list is not inside the MCR.  */
  /* If just initialized, it needs to be synced separately.      */ 
  if (NumFrags > 1) {
    Dbg_Print(DBG_FRAG_SYNC,( "ubsec: SetupOutputFragmentList Sync %d Descriptors to Device (0x%08X,%d,%d)\n", NumFrags-1,
	      pMCR->OutputFragmentListHandle,
	      PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	      (NumFrags-1)*sizeof(DataBufChainList_t)));
    OS_SyncToDevice(pMCR->OutputFragmentListHandle,
	    PacketIndex*(UBSEC_MAX_FRAGMENTS)*sizeof(DataBufChainList_t),
	    (NumFrags-1)*sizeof(DataBufChainList_t));
  }
#endif

  return(UBSEC_STATUS_SUCCESS);
} /* end of SetupOutputFragmentList() */
