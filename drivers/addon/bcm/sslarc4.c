
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/

/*******************************************************************************
 *
 * File: Linux/sslarc4.c
 * 
 * Description: 
 *              Character driver interface to SSL ARC4 routines in the 
 *              ubsec driver.
 *
 * Revision History:
 *                   When       Who   What
 *                   10/11/00   DNA   Created
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 * 10/09/2001 SRM 64 bit port
 *
 ******************************************************************************/

#include "cdevincl.h"

#ifdef FILE_DEBUG_TAG
#undef FILE_DEBUG_TAG
#endif
#define FILE_DEBUG_TAG  "BCMSSL"


/* This is useful only for diagnostics. */
#undef STATIC_ALLOC_OF_CRYPTO_BUFFERS

#ifdef POLL
#undef GOTOSLEEP
#endif

#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS

unsigned char *  pKernSourceBuffer = NULL;
unsigned char *  pKernDestBuffer   = NULL;
unsigned char *  pKernStateBuffer  = NULL;
static ubsec_MemAddress_t       PhysSourceBuf;
static ubsec_MemAddress_t       PhysDestBuf;
static ubsec_MemAddress_t       PhysStateBuf;

#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */


/**************************************************************************
 *
 * Function:  init_arc4if
 * 
 * Called from: init_module() in Linux/dispatch.c
 *
 * Description: 
 *              Buffer sent to SRL is not malloc'd here after all, to allow 
 *              for re-entrant code.
 *
 * Return Values:
 *                == 0   =>   Success
 *                != 0   =>   Failure
 *
 *************************************************************************/

int 
init_arc4if(void) {
  
  int error = 0;
  
#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  pKernSourceBuffer = pKernDestBuffer = (char *)kmalloc((MAX_FILE_SIZE),GFP_KERNEL|GFP_ATOMIC);
  if(pKernSourceBuffer == NULL) {
    PRINTK("no memory for source buffer %d\n", MAX_FILE_SIZE);
    error = -ENOMEM;
    return(error);
  }
  memset(pKernSourceBuffer,0, MAX_FILE_SIZE);
  
  pKernStateBuffer = (char *)kmalloc((260),GFP_KERNEL|GFP_ATOMIC);
  if(pKernStateBuffer == NULL) {
    PRINTK("no memory for source buffer %d\n", 260);
    error = -ENOMEM;
    return(error);
  }
  memset(pKernStateBuffer,0, 260);
  
#ifdef DEBUG
  PRINTK("Allocate Source: %x %x\n", pKernSourceBuffer, vtophys(pKernSourceBuffer));
  PRINTK("Allocate State: %x %x\n", pKernStateBuffer, vtophys(pKernStateBuffer));
#endif
  
  PhysSourceBuf = PhysDestBuf = (ubsec_MemAddress_t)(virt_to_bus(pKernSourceBuffer));
  PhysStateBuf  = (ubsec_MemAddress_t)(virt_to_bus(pKernStateBuffer));

#ifdef DEBUG
  PRINTK("Memory Alloc Source %x %x Dest %x %x for source buffer\n",
	 pKernSourceBuffer, PhysSourceBuf, KernDestBuffer, PhysDestBuf);
  PRINTK("Memory Alloc State %x %x state buffer\n", pKernStateBuffer, PhysStateBuf);
#endif
  
#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */
  
  return(error);
}


/**************************************************************************
 *
 * Function: shutdown_arc4if
 *
 * Called from: cleanup_module() in Linux/dispatch.c
 *
 * Description:
 *
 *************************************************************************/

void 
shutdown_arc4if(void) {
  
#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  if(pKernSourceBuffer != NULL)
    kfree(pKernSourceBuffer);
  if(pKernStateBuffer != NULL)
    kfree(pKernStateBuffer);
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */
  
  return;
}

/**************************************************************************
 *
 * Function:  ubsec_arc4
 * 
 * Called from: ubsec_ioctl() in Linux/dispatch.c
 *
 * Purpose:
 *          Wrapper function between user call and call to SRL.
 *
 * Description: 
 *              Prepare all the data from the user's call for a call to 
 *              the SRL, set up the timer, and finally place this thread
 *              on the wait queue and go to sleep. When ubsec_arc4 is 
 *              called, the relevant data for an SSL ARC4 op reside in
 *              user space. The struct ubecom_arc4_io_pt argument, pIOInfo, 
 *              points to data on the stack and in kernal space. However, 
 *              pIOInfo is a struct that contains pointers which still 
 *              point to memory in user space. ubecom_ssl copies the 
 *              contents of pIOInfo as well as the input data it points
 *              to into memory allocated in kernal space with LinuxAllocateMemory.
 *
 * Return Values: 
 *                == 0   =>   Success
 *                != 0   =>   Failure
 *
 *************************************************************************/

int
ubsec_sslarc4(
	      ubsec_DeviceContext_t pContext,
	      ubsec_arc4_io_t      *pIOInfo
	      ) {
  
  ubsec_arc4_io_t               IOInfo;
  ubsec_SSLCommandInfo_t        SslCommand;
  ubsec_SSLCommandInfo_pt       pSslCommand       = &SslCommand;
  ubsec_ARC4Params_pt           pArc4Params       = &(pSslCommand->Parameters.ARC4Params);
  volatile  CommandContext_t  CommandContext;
  CommandContext_t          *pCommandContext   = (CommandContext_t *)&CommandContext;
  int                           NumCommands       = 1;
  unsigned long		        delayTotalUs      = 0;
  ubsec_FragmentInfo_pt         pSourceFragments  = NULL;
  unsigned char *               pUserSourceBuffer;
  unsigned int                  SourceBufferBytes;
  int                           error             = 0;

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  unsigned char *               pKernSourceBuffer = NULL;
  unsigned char *               pKernDestBuffer   = NULL;
  unsigned char *               pKernStateBuffer  = NULL;
  ubsec_MemAddress_t            PhysSourceBuf;
  ubsec_MemAddress_t            PhysDestBuf;
  ubsec_MemAddress_t            PhysStateBuf;
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */

  if (copy_from_user(&IOInfo, pIOInfo, sizeof(ubsec_arc4_io_t)))
    return -EFAULT;
  pUserSourceBuffer = IOInfo.SourceBuffer;
  SourceBufferBytes = IOInfo.SourceBufferBytes;

  /* 
   *  First, allocate memory for the source buffer (which doubles as 
   *  the destination buffer) and the state output buffer. Translate
   *  both addresses to physical addresses and clear this memory.
   */
  
#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  if(SourceBufferBytes > MAX_FILE_SIZE) {
    PRINTK("input file too large <%d,%d>\n", SourceBufferBytes, MAX_FILE_SIZE);
    error = -EINVAL;
    return(error);
  }
  
#else
  
  pKernSourceBuffer = pKernDestBuffer = (char *)kmalloc((SourceBufferBytes),GFP_KERNEL|GFP_ATOMIC);
  if(pKernSourceBuffer == NULL) {
    PRINTK("no memory for source buffer %d\n", SourceBufferBytes);
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pKernSourceBuffer,0, SourceBufferBytes);
  
  pKernStateBuffer = (char *)kmalloc((260),GFP_KERNEL|GFP_ATOMIC);
  if(pKernStateBuffer == NULL) {
    PRINTK("no memory for source buffer %d\n", 260);
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pKernStateBuffer,0, 260);
  
#ifdef DEBUG
  PRINTK("Allocate Source: %x %x\n", pKernSourceBuffer, vtophys(pKernSourceBuffer));
  PRINTK("Allocate State: %x %x\n", pKernStateBuffer, vtophys(pKernStateBuffer));
#endif /* ifdef DEBUG */
  
  PhysSourceBuf = PhysDestBuf = (ubsec_MemAddress_t)(virt_to_bus(pKernSourceBuffer));
  PhysStateBuf  = (ubsec_MemAddress_t)(virt_to_bus(pKernStateBuffer));

#ifdef DEBUG
  PRINTK("Memory Alloc Source %x %x Dest %x %x for source buffer\n",
	 pKernSourceBuffer, PhysSourceBuf, pKernDestBuffer, PhysDestBuf);
  PRINTK("Memory Alloc State %x %x state buffer\n", pKernStateBuffer, PhysStateBuf);
#endif
  
#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */
  
  /*
   * Next fill data buffers and data structures with appropriate data
   * for this ARC4 op.
   */

  pArc4Params->KeyStateFlag = 0;
  if( (IOInfo.KeyStateFlag & ARC4_STATE_SUPPRESS_WRITEBACK)  != ARC4_STATE_SUPPRESS_WRITEBACK)
  	pArc4Params->KeyStateFlag |= UBSEC_ARC4_STATE_WRITEBACK; 
  if( (IOInfo.KeyStateFlag & ARC4_KEY)  == ARC4_KEY)
    	pArc4Params->KeyStateFlag |= UBSEC_ARC4_STATE_STATEKEY;  
  if( (IOInfo.KeyStateFlag & ARC4_NULL_DATA_MODE)  == ARC4_NULL_DATA_MODE)
    	pArc4Params->KeyStateFlag |= UBSEC_ARC4_STATE_NULL_DATA;

#if DEBUG
  PRINTK("keystateflag io = %d\n", IOInfo.KeyStateFlag);
  PRINTK("keystateflag pa = %d\n", pArc4Params->KeyStateFlag);
#endif
  

  /* 
   *  The formats of IOInfo.KeyStateInBuffer, IOInfo.StateOutBuffer and pARC4Params->KeyStateIn are 
   *  the exact same format as the ARC4 context command structure (with state, index_i and index_j). 
   *  Note that the internal byte ordering within these structures changes depending on
   *  the endianess setting of the CryptoNet device. Other than that, these structures
   *  should be treated simply as 260-byte buffers.
   */

  pArc4Params->KeyStateIn = (ubsec_ARC4_State_pt)kmalloc(sizeof(ubsec_ARC4_State_t),GFP_KERNEL|GFP_ATOMIC);
  if(pArc4Params->KeyStateIn == NULL) {
    PRINTK("no memory for KeyStateIn buffer %d\n", sizeof(ubsec_ARC4_State_t));
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pArc4Params->KeyStateIn,0, sizeof(ubsec_ARC4_State_t));
  if (copy_from_user(pArc4Params->KeyStateIn, IOInfo.KeyStateInBuffer, sizeof(ubsec_ARC4_State_t))) {
    error = -EFAULT;
    goto ReturnErrorLabel;
  }


  /* Assemble Source Fragments */
  
  pSourceFragments = kmalloc((((sizeof(ubsec_FragmentInfo_t) * UBSEC_MAX_FRAGMENTS))),GFP_KERNEL|GFP_ATOMIC);
  if(pSourceFragments == NULL) {
    PRINTK("no memory for fragment buffer\n");
    error = -ENOMEM;
    return(error);
  }
  
  pSslCommand->SourceFragments = pSourceFragments;
  if ((pSslCommand->NumSource = SetupFragmentList(pSslCommand->SourceFragments,
						   pKernSourceBuffer,
						   SourceBufferBytes)) == 0) {
    /* The input/output data requires more fragments than the current driver build can provide; return error */
    error = UBSEC_STATUS_NO_RESOURCE;
    goto ReturnErrorLabel;
  }
  
  if( (IOInfo.KeyStateFlag & ARC4_NULL_DATA_MODE)  != ARC4_NULL_DATA_MODE)
  	if (copy_from_user(pKernSourceBuffer, pUserSourceBuffer, SourceBufferBytes)) {
          error = -EFAULT;
          goto ReturnErrorLabel;
        }
  
  /* Assemble Destination Fragments */

  pSslCommand->NumDestination            = pSslCommand->NumSource;
  pSslCommand->DestinationFragments      = pSslCommand->SourceFragments;
  pArc4Params->state_out.FragmentAddress = PhysStateBuf;
  pArc4Params->state_out.FragmentLength  = 260;
  
  pCommandContext->CallBackStatus = 0;
  pSslCommand->CommandContext     = (unsigned long)pCommandContext;
  pSslCommand->CompletionCallback = CmdCompleteCallback;
  pSslCommand->Command            = UBSEC_ARC4;
  
  start_time(&(pCommandContext->tv_start));
  
#ifdef DEBUG
  PRINTK("Linux:ubsec_sslarc4 just before SRL call...\n");
#endif

#ifndef LINUX2dot2
  init_waitqueue_head(&pCommandContext->WaitQ);
#else
   pCommandContext->WaitQ         = 0;
#endif 

  IOInfo.result_status = ubsec_SSLCommand(pContext, pSslCommand, &NumCommands);
  
  switch(IOInfo.result_status) {
  case UBSEC_STATUS_SUCCESS:
    break;
    
  case UBSEC_STATUS_TIMEOUT:
    PRINTK(" ubsec_SslCommand() timeout\n");
    ubsec_ResetDevice(pContext);
    error = -ETIMEDOUT;
    goto ReturnErrorLabel;
    break;
    
  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK("  ubsec_SslCommand() invalid parameter\n");
    error = -EINVAL;
    goto ReturnErrorLabel;
    break;
    
  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK(" ubsec_SslCommand() no resource. Number done: %d\n", NumCommands);
    error = -ENOBUFS;
    goto ReturnErrorLabel;
    break;
    
  default:
    error = -ENOMSG;
    goto ReturnErrorLabel;
    break;
  }
  
#ifdef GOTOSLEEP
  
  if(!(pCommandContext->CallBackStatus))  /* Just in case completed on same thread. */
    Gotosleep(&pCommandContext->WaitQ);
    if (!pCommandContext->CallBackStatus) {
         pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
         ubsec_ResetDevice(pContext);
         error = -ETIMEDOUT;
         goto ReturnErrorLabel;
    }
#else
  
  for(delayTotalUs = 1; !(CommandContext.CallBackStatus); delayTotalUs++) {
    
#ifdef POLL
    
    /* We need to poll the device if we are operating in POLL mode. */
    ubsec_PollDevice(pContext);
    
#endif
    
    if(delayTotalUs >= 3000000) {
      error = -ETIMEDOUT;
      ubsec_ResetDevice(pContext);
      goto ReturnErrorLabel;
    }
    udelay(1);
  }
  
#endif

#if DEBUG
  PRINTK("Linux: Dest Buffer (post-SRL)\n");
  for(i = 0; i < 32; i++) {
    PRINTK("%02X", pKernDestBuffer[i]);
  }
  PRINTK("\n");
#endif
#if DEBUG
  PRINTK("Linux: State Buffer (post-SRL)\n");
  for(i = 0; i < 260; i++) {
    PRINTK("%02X", pKernStateBuffer[i]);
  }
  PRINTK("\n");
#endif
  
  IOInfo.time_us = CommandContext.tv_start.tv_sec * 1000000 + CommandContext.tv_start.tv_usec;
  if(IOInfo.result_status == UBSEC_STATUS_SUCCESS) {
    if (copy_to_user(IOInfo.DestBuffer, pKernDestBuffer, SourceBufferBytes)) {
      error = -EFAULT;
      goto ReturnErrorLabel;
    }
    if( (IOInfo.KeyStateFlag & ARC4_STATE_SUPPRESS_WRITEBACK) != ARC4_STATE_SUPPRESS_WRITEBACK){
      if (copy_to_user(IOInfo.StateOutBuffer, pKernStateBuffer, 260)) {
        error = -EFAULT;
        goto ReturnErrorLabel;
      }
    }
    if (copy_to_user(pIOInfo, &IOInfo, sizeof(ubsec_arc4_io_t))) {
      error = -EFAULT;
      goto ReturnErrorLabel;
    }
  }
  
 ReturnErrorLabel:
  
#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  if(pKernSourceBuffer != NULL)
    kfree(pKernSourceBuffer);
  if(pKernStateBuffer != NULL)
    kfree(pKernStateBuffer);
  if(pArc4Params->KeyStateIn != NULL)
    kfree(pArc4Params->KeyStateIn);
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */
  
  if(pSourceFragments != NULL)
    kfree(pSourceFragments);
  
  return(error);
}

