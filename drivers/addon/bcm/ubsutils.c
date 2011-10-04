
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
 * ubsutils.c: ubsec library utility and support functions.
 */

/*
 * Revision History:
 * 12/02/1999 DWP Modified source to handle big endian host.
 * 07/06/2000 DPA Fixes for SMP operation
 * 04/18/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"

void DumpCipherMCR(MasterCommand_pt pMCR);
void DumpKeyMCR(MasterCommand_pt pMCR);
/* Reverse 'len' bytes from 'st'. Used to convert between big and little endian */
void
revBytes(void *st, int len)
{
	int i;
	unsigned char *bst = (unsigned char *) st;

	for (i = 0; i < len/2; ++i) {
		unsigned char temp = bst[i];
		bst[i] = bst[len-1-i];
		bst[len-1-i] = temp;
	}
}

/* Reasonably fast rotate left of x by 'n' bits */
UBS_UINT32
rol(UBS_UINT32 x, int n)
{
	unsigned long result = (x << n) | (x >> (32-n));
	return (result);
}

void
/*
 * Copy DWORD values to destination of opposite endianess (of source endianess)
 */
copywords(UBS_UINT32 *out, UBS_UINT32 *in, int num)
{
  /*
   * This routine is used as the anti-endian version of RTL_Memcpy().
   */
  for (;num--;) {
    out[num]=BYTESWAPLONG(in[num]); /* Change the 32-bit endianess in S/W */
  }
}


void ubs_copylongs_cpu_ctrlmem(unsigned long *dest,unsigned long *src, int num)
{
  for ( ; --num >= 0 ; ) 
    dest[num] = CPU_TO_CTRL_LONG(src[num]);
}

void ubs_copylongs_byteswap(unsigned long *dest,unsigned long *src, int num)
{
  for ( ; --num >= 0 ; ) 
    dest[num] = BYTESWAPLONG(src[num]);
}


#ifdef MCR_STATS
#define STAT_NOOFMCRNOFREERETHITS() pDevice->MCRstat.no_free_mcr_ret++;
#define STAT_NOOFPACKETS(num)  \
	if (num <= MCR_MAXIMUM_PACKETS) \
		pDevice->MCRstat.num_packets_stat[num-1]++; \

#define STAT_NOOFMCRHITS(mcrnum) pDevice->MCRstat.push_mcr_stat[mcrnum-1]++;
#define STAT_NOOFMCRFULLHITS(mcrnum) pDevice->MCRstat.mcr_full_stat[mcrnum-1]++;
#else
#define STAT_NOOFMCRNOFREERETHITS() 
#define STAT_NOOFPACKETS(mcrnum)  
#define STAT_NOOFMCRHITS(mcrnum) 
#define STAT_NOOFMCRFULLHITS(mcrnum) 
#endif /* MCR_STATS */

/*
 * GetFreeMCR: Get the next free MCR from the MCR List.
 * Return MCR and/or error status if unable to do so.
 */
MasterCommand_pt GetFreeMCR(  DeviceInfo_pt pDevice,int MCRList,ubsec_Status_t *Status)
{

 MasterCommand_pt  	pMCR;

  pMCR = pDevice->NextFreeMCR[MCRList];
  /* Check to see if it is busy. If not then the list is full */
  if (pMCR->MCRState&MCR_STATE_PUSHED) { /* If it has been pushed. */
    /* Check to see if the command has completed, if so we can free it up. */

    /* First make sure the CPU sees any CryptoNet DMA updates.             */
    Dbg_Print(DBG_MCR_SYNC,( "ubsec: GetFreeMCR() Sync Flags to CPU (0x%08X,%d,%d)\n", 
			pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,4));
    OS_SyncToCPU(pMCR->MCRMemHandle,
		 pMCR->MCRMemHandleOffset,4); /* Need to sync DMA'd flags */

    if( pMCR->Flags & MCR_FLAG_COMPLETION ) {
#ifdef COMPLETE_ON_COMMAND_THREAD
      ubsec_PollDevice(pDevice); /* Free some up */
#endif
      /* Try again */
      pMCR = pDevice->NextFreeMCR[MCRList];
      if (pMCR->MCRState)  /* Still busy */ {
	*Status=UBSEC_STATUS_NO_RESOURCE;
	goto Error_Return;
      }
    }
    else {
      /* Are we blocked, just check to see if device is running.*/
      if ( UBSEC_READ_STATUS(pDevice) & DMA_ERROR) {
	Dump_Registers(pDevice,DBG_FATAL); 
	*Status=UBSEC_STATUS_DEVICE_FAILED;
	goto Error_Return;
      }
      else {
	*Status=UBSEC_STATUS_NO_RESOURCE;
	goto Error_Return;
      }
    }
  }

  *Status=UBSEC_STATUS_SUCCESS;

  return(pMCR);

 Error_Return:
	 STAT_NOOFMCRNOFREERETHITS();
  return(NULL_MASTER_COMMAND);
}


/*
 *
 * PushMCR: Push an MCR onto the device if there is an MCR waiting
 * and the device is ready to accept the waiting MCR. 
 *
 * MCRs in each list waiting to be pushed are pointed to by NextDeviceMCR
 *
 */



void
PushMCR(DeviceInfo_pt pDevice)
{
  VOLATILE MasterCommand_t  *pMCR;
  unsigned int MCRListNum; /* MCR list we are dealing with */


  for (MCRListNum=0;MCRListNum < pDevice->NumberOfMCRLists ;MCRListNum++ ) {
    pMCR = pDevice->NextDeviceMCR[MCRListNum];

    /* Is this MCR ready for pushing. (packets+notpushed)*/
    if (!pMCR->NumberOfPackets ||
	(pMCR->MCRState==MCR_STATE_PUSHED))
	continue;  /* Nope, go to next MCRList */

	STAT_NOOFPACKETS(pMCR->NumberOfPackets);

    /* Is the device ready to accept an MCR */
    if ( !MCRListNum) { /* check MCR1_FULL (crypto) */
#ifdef DVT
      if (pDevice->DVTOptions & UBSEC_SUSPEND_MCR1)
	continue;
#endif /* DVT */
      if (UBSEC_READ_STATUS(pDevice) & MCR1_FULL) {
#ifdef CHECK_BUSY_BIT_TWICE
	/* Wait one us and try again. */
	OS_Waitus(1);
	if (UBSEC_READ_STATUS(pDevice) & MCR1_FULL) 
#endif
	  STAT_NOOFMCRFULLHITS(1); /* MCR_STATS */
	  continue; /* can't push crypto MCR, try next MCRList */
      }
	STAT_NOOFMCRHITS(1); /* MCR_STATS */
    }
    else { /* check MCR2_FULL (key) */
#ifdef DVT
      if (pDevice->DVTOptions & UBSEC_SUSPEND_MCR2)
	continue;
#endif /* DVT */
      if (UBSEC_READ_STATUS(pDevice) & MCR2_FULL) {
#ifdef CHECK_BUSY_BIT_TWICE
	/* Wait one us and try again. */
	OS_Waitus(1);
	if (UBSEC_READ_STATUS(pDevice) & MCR2_FULL)
#endif
	  STAT_NOOFMCRFULLHITS(2); /* MCR_STATS */
	  continue; /* can't push key MCR, try next MCRList (or quit if this is last MCRList) */
       }
	STAT_NOOFMCRHITS(2); /* MCR_STATS */
     } /* end of MCR2_FULL check */ 

    Dbg_Print(DBG_CMD,( "ubsec:  push a new mcr %d: %d Packets\n", pDevice->NextDeviceMCR[MCRListNum]->Index,pMCR->NumberOfPackets ));

    /* If we are pushing the current free MCR then we will need to
       advance it to ensure that we maintain list integrity */
    if (pMCR==pDevice->NextFreeMCR[MCRListNum])
      pDevice->NextFreeMCR[MCRListNum]=(MasterCommand_pt)pMCR->pNextMCR;

    /* Go to the next MCR. Wrap around if necessary. */
    pDevice->NextDeviceMCR[MCRListNum]=(MasterCommand_pt)pMCR->pNextMCR;
    /* Now push the current MCR to the device */
    pMCR->NumberOfPackets = CPU_TO_CTRL_SHORT( pMCR->NumberOfPackets );

    /* For the BCM582x, let MCRn_ALL_DONE (not MCRn_INTR) cause the H/W interrupt.   */
    /* This line of code has no effect on these CryptoNet devices: BCM580x, BCM5820 */
    pMCR->Flags = pDevice->InterruptSuppress; /* UBSEC_582x */

    Dbg_Print(DBG_MCR_SYNC,( "ubsec: PushMCR() Sync MCR to device (0x%08X,%d,%d)\n", 
			pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,
			sizeof(*pMCR)));
    OS_SyncToDevice(pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,sizeof(*pMCR));

    pMCR->MCRState|=MCR_STATE_PUSHED;
    UBSEC_WRITE_MCR(pDevice,pMCR,MCRListNum);
  } /* for each MCRList */

} /* end of PushMCR */


/*
 * dumpMCR: Dump the contents of the MCR with associated structures etc.
 *
 */
int
dump_MCR(DeviceInfo_pt pDevice,MasterCommand_pt pMCR,unsigned long MCRListIndex)
{
int NumCommands;


Dbg_Print(DBG_CMD,("ubsec:  --------------- MCR DUMP (%d) ----------------\n",MCRListIndex));
 Dbg_Print(DBG_CMD,("ubsec: MCR Index:  Free(%02d) Device(%02d) Done(%02d)\n",
		      pDevice->NextFreeMCR[MCRListIndex]->Index, pDevice->NextDeviceMCR[MCRListIndex]->Index, pDevice->NextDoneMCR[MCRListIndex]->Index));

 if (pMCR->MCRState & MCR_STATE_PUSHED)
   NumCommands=CPU_TO_CTRL_SHORT(pMCR->NumberOfPackets);
 else
   NumCommands=pMCR->NumberOfPackets;
 Dbg_Print(DBG_CMD,("ubsec:  Info for MCR %d\n", pMCR->Index ));
 Dbg_Print(DBG_CMD,("ubsec:  -- Virtual Address  [%08x]\n", pMCR ));
 Dbg_Print(DBG_CMD,("ubsec:  -- Physical Address [%08x]\n", pMCR->MCRPhysicalAddress ));
 Dbg_Print(DBG_CMD,("ubsec:  -- Number of Packets [%04d] Flags [%04x]", NumCommands, pMCR->Flags));
 
#ifdef UBSDBG
 if ((unsigned int) pMCR->Index >= pDevice->NumberOfMCRs[MCRListIndex]) {
   Dbg_Print(DBG_FATAL,( "\nubsec:  -- Invalid Index %d\n ", pMCR->Index ));
   return(UBSEC_STATUS_INVALID_PARAMETER);
 }
#endif

 if (NumCommands > MCR_MAXIMUM_PACKETS) {
   Dbg_Print(DBG_FATAL,( "\nubsec:  -- Too many packets %d Just do 1\n ",NumCommands ));
   NumCommands=1;
  }


 /* Type of MCR depends on the list */
 if(MCRListIndex==0){
   DumpCipherMCR(pMCR);
 }
 else {
   DumpKeyMCR(pMCR);
 }

 return 0;


}

/*
 * Dump the contents of a "cipher" MCR.
 */
void DumpCipherMCR(MasterCommand_pt pMCR)
{
Packet_pt         pPacket;
PacketContext_pt  pContext;
int             PacketIndex;
int  FragNum;     /* Index into packet fragment array. */
VOLATILE DataBufChain_t  *FragPtr;
VOLATILE DataBufChain_t  *tFragPtr;
int NumCommands;
#ifdef UBSEC_582x_CLASS_DEVICE
 int WordLen,i;
 long *p;
#endif

 VOLATILE CipherContext_t  *pCipherContext;

 PacketIndex = 0;
 pPacket = (Packet_pt)&(pMCR->PacketArray[0]);
 pContext = (PacketContext_pt)&(pMCR->ContextList[0]);
 if (pMCR->MCRState & MCR_STATE_PUSHED)
   NumCommands=CPU_TO_CTRL_SHORT(pMCR->NumberOfPackets);
 else
   NumCommands=pMCR->NumberOfPackets;

 for (; NumCommands-- ; PacketIndex++,pPacket++,pContext++) { /* Add all the packets to the MCR */
   Dbg_Print(DBG_CMD,( "\nubsec:  ---- Packet %d\n", PacketIndex ));
   Dbg_Print(DBG_CMD,(   "ubsec:  ---- Context: Virtual Address  [%08x]\n", pContext ));
   Dbg_Print(DBG_CMD,(   "ubsec:  ----          Physical Address [%08x]\n", CPU_TO_CTRL_LONG(pContext->PhysicalAddress))); 
#ifdef UBSEC_582x_CLASS_DEVICE
   if (pContext->operation_type!=OPERATION_IPSEC) {
     WordLen=(pContext->cmd_structure_length)/4;
     Dbg_Print(DBG_CMD,( "Context - "));
     p=(long *)pContext;
     for ( i=0 ; i < WordLen ; i++,p++) {
       Dbg_Print(DBG_CMD,( "%08x ",CPU_TO_CTRL_LONG(*p)));
     }
     Dbg_Print(DBG_CMD,( "]\n"));
   }
   else
#endif /* UBSEC_582x_CLASS_DEVICE */
     {
    pCipherContext=&pContext->Context.Cipher;

     Dbg_Print(DBG_CMD,(   "ubsec:  ----          Crypto Key [%08x][%08x]\n", pCipherContext->CryptoKey1[0],pCipherContext->CryptoKey1[1] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----                     [%08x][%08x]\n", pCipherContext->CryptoKey2[0],pCipherContext->CryptoKey2[1] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----                     [%08x][%08x]\n", pCipherContext->CryptoKey3[0],pCipherContext->CryptoKey3[1] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----          HMAC Inner [%08x][%08x][%08x][%08x][%08x]\n",
			pCipherContext->HMACInnerState[0],
			pCipherContext->HMACInnerState[1],
			pCipherContext->HMACInnerState[2],
			pCipherContext->HMACInnerState[3],
			pCipherContext->HMACInnerState[4] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----          HMAC Outer [%08x][%08x][%08x][%08x][%08x]\n",
			pCipherContext->HMACOuterState[0],
			pCipherContext->HMACOuterState[1],
			  pCipherContext->HMACOuterState[2],
			pCipherContext->HMACOuterState[3],
			pCipherContext->HMACOuterState[4] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----          Computed IV [%08x][%08x]\n", pCipherContext->ComputedIV[0], pCipherContext->ComputedIV[1] ));
     Dbg_Print(DBG_CMD,(   "ubsec:  ----          Crypto O/F  [%04x][%04x]\n", 
                        CTRL_TO_CPU_SHORT( pCipherContext->CryptoOffset ), 
			CTRL_TO_CPU_SHORT( pCipherContext->CryptoFlag )));
     }
    /*
     * Now add the packet input fragment information
     * First fragment will need to skip the MAC Header
     * We are assuming at least one fragment.
     */

   Dbg_Print(DBG_CMD,( "ubsec:  Packet len %d\n",CTRL_TO_CPU_SHORT(pPacket->PacketLength))); 

     FragPtr=&pPacket->InputHead;
     for (FragNum=0 ;  ;FragNum++ ) {
       Dbg_Print(DBG_CMD,( "ubsec:  ---- Input Fragment(%02d): Length (%04d)\n", 
			   FragNum, CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
       Dbg_Print(DBG_CMD,( "ubsec:  ----          Physical Address [%08x]\n",
               CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
       if (FragNum>UBSEC_MAX_FRAGMENTS) {
	 Dbg_Print(DBG_CMD,( "ubsec:  Lost input fragment list\n" ));
	 return;
       }
       if (!FragPtr->pNext) {
	  break;
       }
       tFragPtr=FragPtr;
       FragPtr=(DataBufChain_pt)&pMCR->InputFragmentList[(PacketIndex*(UBSEC_MAX_FRAGMENTS))];
       FragPtr+=FragNum;
     }
      /* print the packet size */
	 Dbg_Print(DBG_CMD,( "ubsec:  ---- Packet Length (%04d)\n",CTRL_TO_CPU_SHORT(pPacket->PacketLength)));

      /* print the output fragment information */
     FragPtr=&pPacket->OutputHead;
     for (FragNum=0 ;  ;FragNum++ ) {
       Dbg_Print(DBG_CMD,( "ubsec:  ---- Output Fragment(%02d): Length (%04d)\n",
			   FragNum, CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
       Dbg_Print(DBG_CMD,( "ubsec:  ----          Physical Address [%08x]\n",
			   CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
       Dbg_Print(DBG_CMD,( "ubsec:  ----          Next Physical Addr [%08x]\n",
			   CTRL_TO_CPU_LONG(FragPtr->pNext)));

       if (FragNum>UBSEC_MAX_FRAGMENTS) {
	 Dbg_Print(DBG_CMD,( "ubsec:  Lost output fragment list\n" ));
	 return;
       }

       if (!FragPtr->pNext) {
	 break;
       }
       tFragPtr=FragPtr;
       FragPtr=(DataBufChain_pt)&pMCR->OutputFragmentList[(PacketIndex*(UBSEC_MAX_FRAGMENTS))];
       FragPtr+=FragNum;
       if ( CTRL_TO_CPU_LONG(FragPtr->DataAddress) & 0x03)
	 Dbg_Print(DBG_CMD,("ubsec: ################INVALID OUTPUT ADDRESS %0x\n",CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
       if ( CTRL_TO_CPU_SHORT(FragPtr->DataLength) & 0x03)
	 Dbg_Print(DBG_CMD,("ubsec: ################INVALID OUTPUT LENGTH %0x\n",CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
     }
   }
}



/*
 * Dump the contents of a "key" MCR.
 */
void DumpKeyMCR(MasterCommand_pt pMCR)
{
#if 0
  Packet_pt         pPacket;
  KeyContext_pt  pContext;
  int             PacketIndex;
  int NumCommands;
  DH_Send_CtxCmdBuf_pt 	pDHSendCtx;
  DH_REC_CtxCmdBuf_pt	pDHRecCtx;
  Pub_RSA_CtxCmdBuf_pt		pRSACtx;
  Pri_RSA_CtxCmdBuf_pt		pPrivRSACtx;

  int i;
  int  FragNum;     /* Index into packet fragment array. */
  VOLATILE DataBufChain_t *FragPtr;
  VOLATILE DataBufChain_t *tFragPtr;
  int WordLen;

  PacketIndex = 0;
  pPacket = &(pMCR->PacketArray[0]);
  pContext = pMCR->KeyContextList[0];
  if (pMCR->MCRState & MCR_STATE_PUSHED)
    NumCommands=CPU_TO_CTRL_SHORT(pMCR->NumberOfPackets);
  else
    NumCommands=pMCR->NumberOfPackets;

  for (; NumCommands-- ; PacketIndex++,pPacket++) { // Add all the packets to the MCR
    pContext = pMCR->KeyContextList[PacketIndex];
     Dbg_Print(DBG_KEY,( "\nubsec:  ---- Packet %d\n", PacketIndex ));
     Dbg_Print(DBG_KEY,(   "ubsec:  ---- Context: Virtual Address  [%08x]\n", pContext ));
     Dbg_Print(DBG_KEY,(   "ubsec:  ----          Physical Address [%08x]\n", CPU_TO_CTRL_LONG(pContext->PhysicalAddress))); 
     switch (pContext->operation_type) {
     case OPERATION_DH_PUBLIC:
	pDHSendCtx = &pContext->CtxCmdBuf.DH_Send_CtxCmdBuf; 
	WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-DH_STATIC_SEND_CONTEXT_SIZE)/4;

	Dbg_Print(DBG_KEY,(   "ubsec:  ---- DH_Public - RNG-Enable [%d] Private Klen [%d] Generator Len [%d]\n",
			      pDHSendCtx->rng_enable,pDHSendCtx->private_key_length,pDHSendCtx->generator_length)); 

	Dbg_Print(DBG_KEY,(   "ubsec:  ---- Modulus Length [%d] ",pDHSendCtx->modulus_length)); 
	Dbg_Print(DBG_KEY,(   "Generator Length [%d] Context Value=[",pDHSendCtx->generator_length)); 

	for ( i=0 ; i < WordLen ; i++) {
	   Dbg_Print(DBG_KEY,( "%08x",SYS_TO_BE_LONG(pDHSendCtx->Ng[i])));
	}
	Dbg_Print(DBG_KEY,( "]\n"));

	break;
     case OPERATION_DH_SHARED:

	pDHRecCtx = &pContext->CtxCmdBuf.DH_REC_CtxCmdBuf; 
	WordLen=(pDHRecCtx->modulus_length+31)/32;

	Dbg_Print(DBG_KEY,(   "ubsec:  ---- DH Shared Modulus Length [%d] Value -\n[",pDHRecCtx->modulus_length)); 
	for ( i=0 ; i < WordLen ; i++) {
	   Dbg_Print(DBG_KEY,( "%08x",SYS_TO_BE_LONG(pDHRecCtx->N[i])));
	}
	Dbg_Print(DBG_KEY,( "]\n"));

       break;
     case OPERATION_RSA_PUBLIC:
       pRSACtx = &pContext->CtxCmdBuf.Pub_RSA_CtxCmdBuf;
       WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-RSA_STATIC_PUBLIC_CONTEXT_SIZE)/4;
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Public Modulus Length [%d] \n[",pRSACtx->modulus_length)); 
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Public G Length [%d] Context-\n[",pRSACtx->exponent_length)); 
       for ( i=0 ; i < WordLen ; i++) {
	 Dbg_Print(DBG_KEY,( "%08x",SYS_TO_BE_LONG(pRSACtx->Ng[i])));
       }
       Dbg_Print(DBG_KEY,( "]\n"));

       break;
     case OPERATION_RSA_PRIVATE:
       pPrivRSACtx = &pContext->CtxCmdBuf.Pri_RSA_CtxCmdBuf;
       WordLen=(pMCR->KeyContextList[PacketIndex]->cmd_structure_length-RSA_STATIC_PRIVATE_CONTEXT_SIZE)/4;
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Private Prime Q Length [%d] \n[",pPrivRSACtx->q_length)); 
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Private Prime P Length [%d] \n[",pPrivRSACtx->p_length)); 
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Private Prime EDQ Length [%d] \n[",pPrivRSACtx->dq_length)); 
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Private Prime EDP Length [%d] \n[",pPrivRSACtx->dp_length)); 
       Dbg_Print(DBG_KEY,(   "ubsec:  ---- RSA Private Prime Pinv Length [%d] Context Value -\n[",pPrivRSACtx->pinv_length)); 

       for ( i=0 ; i < WordLen ; i++) {
	 Dbg_Print(DBG_KEY,( "%08x",SYS_TO_BE_LONG(pPrivRSACtx->CtxParams[i])));
       }
       Dbg_Print(DBG_KEY,( "]\n"));

       break;
     default:
       Dbg_Print(DBG_KEY,(   "ubsec:  ----          Unknown Key Command %x\n",pContext->operation_type)); 
     }
  
    /*
     * Now add the packet input fragment information
     * First fragment will need to skip the MAC Header
     * We are assuming at least one fragment.
     */
	FragPtr=&pPacket->InputHead;
	for (FragNum=0 ;  ;FragNum++ ) {
	  Dbg_Print(DBG_CMD,( "ubsec:  ---- Input Fragment(%02d): Length (%04d)\n", 
			      FragNum, CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
	  Dbg_Print(DBG_CMD,( "ubsec:  ----          Physical Address [%08x]\n",
			      CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
	  if (FragNum>UBSEC_MAX_FRAGMENTS) {
	    Dbg_Print(DBG_CMD,( "ubsec:  Lost input fragment list\n" ));
	    return(-3);
	  }

	  if (FragNum>UBSEC_MAX_FRAGMENTS) {
	    Dbg_Print(DBG_CMD,( "ubsec:  Lost output fragment list\n" ));
	    return(-3);
	  }

	  if (!FragPtr->pNext) {
	    break;
	  }
	  tFragPtr=FragPtr;
	  FragPtr=&pMCR->InputFragmentList[(PacketIndex*(UBSEC_MAX_FRAGMENTS))];
	  FragPtr+=FragNum;
	}
	/* print the packet size */
	   Dbg_Print(DBG_CMD,( "ubsec:  ---- Packet Length (%04d)\n",CTRL_TO_CPU_SHORT(pPacket->PacketLength)));

      /* print the output fragment information */
	FragPtr=&pPacket->OutputHead;
	for (FragNum=0 ;  ;FragNum++ ) {
	  Dbg_Print(DBG_CMD,( "ubsec:  ---- Output Fragment(%02d): Length (%04d)\n",
			      FragNum, CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
	  Dbg_Print(DBG_CMD,( "ubsec:  ----          Physical Address [%08x]\n",
			      CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
	  Dbg_Print(DBG_CMD,( "ubsec:  ----          Next Physical Addr [%08x]\n",
			      CTRL_TO_CPU_LONG(FragPtr->pNext)));
	  if (FragNum>UBSEC_MAX_FRAGMENTS) {
	    Dbg_Print(DBG_CMD,( "ubsec:  Lost output fragment list\n" ));
	    return(-3);
	  }

	  if (FragNum>UBSEC_MAX_FRAGMENTS) {
	    Dbg_Print(DBG_CMD,( "ubsec:  Lost output fragment list\n" ));
	    return(-3);
	  }

	  if (!FragPtr->pNext) {
	    break;
	  }
	  tFragPtr=FragPtr;
	  FragPtr=&pMCR->OutputFragmentList[(PacketIndex*(UBSEC_MAX_FRAGMENTS))];
	  FragPtr+=FragNum;
	  if ( CTRL_TO_CPU_LONG(FragPtr->DataAddress) & 0x03)
	    Dbg_Print(DBG_CMD,("ubsec: ################INVALID OUTPUT ADDRESS %0x\n",CTRL_TO_CPU_LONG(FragPtr->DataAddress)));
	  if ( CTRL_TO_CPU_SHORT(FragPtr->DataLength) & 0x03)
	    Dbg_Print(DBG_CMD,("ubsec: ################INVALID OUTPUT LENGTH %0x\n",CTRL_TO_CPU_SHORT(FragPtr->DataLength)));
	}
  }
#endif
}


/*
 * Poll with timeout the next MCR to be done. Complete the MCR
 * and return if it completes.
 */
int
WaitForCompletion(DeviceInfo_pt pDevice,unsigned long blockus,unsigned long MCRListIndex)
{
  VOLATILE  int wait_us;
  VOLATILE  unsigned long delay_total_us=0;
  MasterCommand_pt pMCR;

  pMCR =  pDevice->NextDoneMCR[MCRListIndex];

  /* First make sure this MCR has been pushed. */
  if (!(pMCR->MCRState&MCR_STATE_PUSHED))
    return(UBSEC_STATUS_INVALID_PARAMETER);

  if (pDevice->InCriticalSection) {
    Dbg_Print((DBG_CMD|DBG_KEY),( "ubsec:  ubsec Completion Nested call %d ",pDevice->InCriticalSection));
    return(UBSEC_STATUS_DEVICE_BUSY); 
  }

  wait_us=1;

  Dbg_Print(DBG_MCR_SYNC,( "ubsec: WaitForCompletion() Sync Flags to CPU (0x%08X,%d,%d)\n", 
		      pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,4));
  OS_SyncToCPU(pMCR->MCRMemHandle,
	       pMCR->MCRMemHandleOffset,4); /* Need to sync DMA'd flags */

  while (!( pMCR->Flags & MCR_FLAG_COMPLETION)) {
    delay_total_us+=wait_us;
    if (delay_total_us >= blockus) {
      Dbg_Print(DBG_CMD_FAIL,("ubsec: Command Timeout %x\n",pMCR->Flags));
      Dump_Registers(pDevice,DBG_CMD_FAIL);
      return(UBSEC_STATUS_TIMEOUT);
    }

    Dbg_Print(DBG_MCR_SYNC,( "ubsec: WaitForCompletion() Sync Flags to CPU (0x%08X,%d,%d)\n", 
			pMCR->MCRMemHandle,pMCR->MCRMemHandleOffset,4));
    OS_SyncToCPU(pMCR->MCRMemHandle,
		 pMCR->MCRMemHandleOffset,4); /* Sync flags before checking */

    OS_Waitus(wait_us);
    wait_us=1;
  }

  /* Now complete the request */
  ubsec_PollDevice((ubsec_DeviceContext_t)pDevice);
#if defined BLOCK || defined COMPLETE_PENDING_REQUESTS
  PushMCR(pDevice);
#endif
  return(UBSEC_STATUS_SUCCESS);
}

/*
 * FlushDevice:
 *
 * This routine is used to flush the device request queue.
 * Commands will be completed (callbacks called) from within
 * this call with the status as passed to this routine
 *
 * Flushall when set will cause entire queues to be complete
 * Otherwise they will only complete if they have been pushed.
 *
 */
void
FlushDevice(DeviceInfo_pt pDevice,ubsec_Status_t Status,unsigned int FlushType)
{
  VOLATILE MasterCommand_t *pMCR;
  VOLATILE int j;
  VOLATILE CallBackInfo_t  *pCallBack;
  int NumberOfPackets;
  unsigned long MCRListIndex;

  Dbg_Print(DBG_CMD,( "ubsec:  flush device handler \n" ));

  /* While there are MCRs to complete, do it. */
  for(MCRListIndex=0; MCRListIndex< pDevice->NumberOfMCRLists ;MCRListIndex++) {
    for(;;) {
    /* Get next device. */
      pMCR = pDevice->NextDoneMCR[MCRListIndex];
      /* Now check to see if there are any to complete */
      if  ((pMCR->MCRState) ||
	  ((FlushType==FLUSH_ALL) && (pMCR->NumberOfPackets))) { /* Flush all packets */
	if (pMCR->MCRState & MCR_STATE_PUSHED)
	  NumberOfPackets = CPU_TO_CTRL_SHORT(pMCR->NumberOfPackets);
	else
	  NumberOfPackets = pMCR->NumberOfPackets;
	/* unload the packets */
	pCallBack = &pMCR->CompletionArray[0];
	for( j = 0; j < NumberOfPackets; j++ ) {
	  if( pCallBack->CompletionCallback) {
	    (*pCallBack->CompletionCallback)( pCallBack->CommandContext,Status);
	    pCallBack->CompletionCallback=0;
	  }
	  pCallBack++;
	}
	pMCR->NumberOfPackets = 0; /* This frees it up */
	pMCR->Flags = 0; 
	pMCR->MCRState = MCR_STATE_FREE; /* Set it to free */
	pDevice->NextDoneMCR[MCRListIndex] = (MasterCommand_pt)pMCR->pNextMCR;
      }
      else {
		  if (FlushType==FLUSH_ALL) {
			/* reinit buffer pointers */
			pDevice->NextFreeMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
			pDevice->NextDeviceMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
			pDevice->NextDoneMCR[MCRListIndex]=pDevice->MCRList[MCRListIndex];
		  }
	break;
      }
    }
  }
return;
}




/*
 *
 */
int
ubsec_chipinfo(
	       DeviceInfo_pt   pDevice,
	       ubsec_chipinfo_io_pt   pExtChipInfo
	       ) {
  
  if (pExtChipInfo->Status != sizeof(*pExtChipInfo))
    /* Current driver build is incompatible with calling app */
    return UBSEC_STATUS_NO_DEVICE; 

  pExtChipInfo->DeviceID = pDevice->DeviceID;
  pExtChipInfo->BaseAddress = pDevice->BaseAddress;
  pExtChipInfo->IRQ = pDevice->IRQ;

#ifdef UBSEC_582x_CLASS_DEVICE
  pExtChipInfo->MaxKeyLen = 2048;
#else
  pExtChipInfo->MaxKeyLen = 1024;
#endif
  

  pExtChipInfo->Features = ~(0); /* Start with all flags enabled */

#if (UBS_CRYPTONET_ATTRIBUTE != UBS_BIG_ENDIAN)
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_SRL_BE);
#endif /* Little Endian CryptoNet build */

#if (UBS_CPU_ATTRIBUTE != UBS_BIG_ENDIAN)
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_CPU_BE);
#endif /* Little Endian CPU build */

#if !defined(UBS_OVERRIDE_LONG_KEY_MODE)
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_KEY_OVERRIDE);
#endif /* Normal LongKey mode build */

#if (defined(UBSEC_5820) || defined(UBSEC_5805)) 
  /* SRL was built in "5820 mode" or for 580x chips */
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_ARC4);
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_ARC4_NULL);
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_DBLMODEXP);
#endif /* UBSEC_5820 or UBSEC_5805 */

#if defined(UBSEC_5805) 
  pExtChipInfo->Features &= ~(UBSEC_EXTCHIPINFO_SSL);
#endif /* UBSEC_5805 */

  pExtChipInfo->Status = UBSEC_STATUS_SUCCESS;
  return UBSEC_STATUS_SUCCESS;
}




/*
 *
 */
void
Dump_Registers(DeviceInfo_pt pDevice,int dbg_flag)
{
    unsigned long a,b,c,d,e; /* conseq reads help in debugging on PCI trace. */
    
    /* check that we will print before polling the device */
    if( !Dbg_Test( dbg_flag ) )
	return;

    a=PCI_TO_CPU_LONG( OS_IOMemRead32(pDevice->ControlReg[0]));
    b=PCI_TO_CPU_LONG( OS_IOMemRead32(pDevice->ControlReg[1]));
    c=PCI_TO_CPU_LONG( OS_IOMemRead32(pDevice->ControlReg[2]));
    d=PCI_TO_CPU_LONG( OS_IOMemRead32(pDevice->ControlReg[3]));

    if (UBSEC_IS_KEY_DEVICE(pDevice)) {
      e=PCI_TO_CPU_LONG( OS_IOMemRead32(pDevice->ControlReg[4])); 

      Dbg_Print((unsigned int)dbg_flag,( "ubsec: -------------------------- Register Dump --------------------------\n" ));
      Dbg_Print((unsigned int)dbg_flag,( " MCR1[%08x] ctrl[%08x] stat[%08x] dma[%08x] MCR2[%08x]\n",a,b,c,d,e));
      Dbg_Print((unsigned int)dbg_flag,( "ubsec: -------------------------------------------------------------------\n" ));
    }
    else {
      Dbg_Print((unsigned int)dbg_flag,( "ubsec:  --------------- Register Dump ----------------\n" ));
      Dbg_Print((unsigned int)dbg_flag,( "ubsec:  MCR [%08x] ctrl[%08x] stat[%08x] dma[%08x]\n",a,b,c,d));
    }

}


#ifdef DVT

int ubsec_dvt_handler(void *context, void *arg)
{
  MasterCommand_pt pMCR;
  unsigned long save_config = 0;
  DVT_Params_pt DVTparams = (DVT_Params_pt) arg;
  DeviceInfo_pt pDevice = (DeviceInfo_pt) context;
  if (DVTparams->Status < sizeof(*DVTparams))
    /* Current driver build is incompatible with calling app */
    return UBSEC_STATUS_NO_DEVICE;
  DVTparams->Status = UBSEC_STATUS_SUCCESS;
  switch (DVTparams->Command) {
  case UBSEC_DVT_MCR1_SUSPEND:
    pDevice->DVTOptions |= UBSEC_SUSPEND_MCR1;
    DVTparams->OutParameter = pDevice->NextFreeMCR[UBSEC_CIPHER_LIST]->Index; 
    break;
  case UBSEC_DVT_MCR2_SUSPEND:
    pDevice->DVTOptions |= UBSEC_SUSPEND_MCR2;
    DVTparams->OutParameter = pDevice->NextFreeMCR[UBSEC_KEY_LIST]->Index; 
    break;
  case UBSEC_DVT_MCR1_RESUME:
    pDevice->DVTOptions &= ~UBSEC_SUSPEND_MCR1;
    PushMCR(pDevice);
    break;
  case UBSEC_DVT_MCR2_RESUME:
    pDevice->DVTOptions &= ~UBSEC_SUSPEND_MCR2;
    PushMCR(pDevice);
    break;
  case UBSEC_DVT_ALL_MCR_RESUME:
    pDevice->DVTOptions &= ~(UBSEC_SUSPEND_MCR1 | UBSEC_SUSPEND_MCR2);
    PushMCR(pDevice);
    break;
  case UBSEC_DVT_NEXT_MCR1:
    if (OS_EnterCriticalSection(pDevice,save_config)) {
      DVTparams->Status = UBSEC_STATUS_DEVICE_BUSY;
      break;
    }
    pMCR = pDevice->NextFreeMCR[UBSEC_CIPHER_LIST];
    if (pMCR->MCRState)  /* Still busy */ 
      DVTparams->Status=UBSEC_STATUS_NO_RESOURCE;
    else if (!pMCR->NumberOfPackets) {
      /* Do not jump if current MCR has no packets */
    }
    else {
      pDevice->NextFreeMCR[UBSEC_CIPHER_LIST] = (MasterCommand_pt)pMCR->pNextMCR;
    } 
    DVTparams->OutParameter = pDevice->NextFreeMCR[UBSEC_CIPHER_LIST]->Index; 
    OS_LeaveCriticalSection(pDevice,save_config);
    break;
  case UBSEC_DVT_NEXT_MCR2:
    if (OS_EnterCriticalSection(pDevice,save_config)) {
      DVTparams->Status = UBSEC_STATUS_DEVICE_BUSY;
      break;
    }
    pMCR = pDevice->NextFreeMCR[UBSEC_KEY_LIST];
    if (pMCR->MCRState)  /* Still busy */ 
      DVTparams->Status=UBSEC_STATUS_NO_RESOURCE;
    else if (!pMCR->NumberOfPackets) {
      /* Do not jump if current MCR has no packets */
    }
    else {
      pDevice->NextFreeMCR[UBSEC_KEY_LIST] = (MasterCommand_pt)pMCR->pNextMCR;
    }
    DVTparams->OutParameter = pDevice->NextFreeMCR[UBSEC_KEY_LIST]->Index; 
    OS_LeaveCriticalSection(pDevice,save_config);
    break;
  default:
    DVTparams->Status = UBSEC_STATUS_INVALID_PARAMETER;
    break;
  };
  return (DVTparams->Status);
}

#endif /* DVT */


void
ubsec_DumpDeviceInfo(ubsec_DeviceContext_t Context)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;
  Dump_Registers(pDevice,DBG_LOG);
  Dbg_Print(DBG_LOG,("CryptoNet: MCR-0 Index:  Free(%02d) Device(%02d) Done(%02d)\n",
		     pDevice->NextFreeMCR[0]->Index, pDevice->NextDeviceMCR[0]->Index, pDevice->NextDoneMCR[0]->Index));
  Dbg_Print(DBG_LOG,("CryptoNet: MCR-1 Index:  Free(%02d) Device(%02d) Done(%02d)\n",
		     pDevice->NextFreeMCR[1]->Index, pDevice->NextDeviceMCR[1]->Index, pDevice->NextDoneMCR[1]->Index));
#ifdef UBSEC_STATS
    Dbg_Print(DBG_LOG,("CryptoNet: DMA Errors %02d\n",
		       pDevice->Statistics.DMAErrorCount));
#endif
}


