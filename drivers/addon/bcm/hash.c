
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
/*
 * Broadcom Corporation uBSec SDK
 */
/*
 * pkey.c: Character driver interface to public key routines.
 */
/*
 * Revision History:
 *
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 * 10/09/2001 SRM 64 bit port
 */      

#include "cdevincl.h"

/* This is useful only for diagnostics. */
#undef STATIC_ALLOC_OF_CRYPTO_BUFFERS

#ifdef POLL
#undef GOTOSLEEP
#endif

/*
 * Global Variables:
 */

#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS

unsigned char * pKernSourceBuffer = NULL;
unsigned char * pKernDestBuffer   = NULL;
static ubsec_MemAddress_t      PhysSourceBuf;
static ubsec_MemAddress_t      PhysDestBuf;

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
init_hashif(void) {
  
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
  PRINTK("Allocate Dest: %x %x\n", pKernSourceBuffer, vtophys(pKernSourceBuffer));

#endif /* ifdef DEBUG */
  
  PhysSourceBuf = PhysDestBuf = (ubsec_MemAddress_t)(virt_to_bus(pKernSourceBuffer));

#ifdef DEBUG
  
  PRINTK("Memory Alloc Source %x %x Dest %x %x for source buffer\n",
	 pKernSourceBuffer, PhysSourceBuf, KernDestBuffer, PhysDestBuf);
    
#endif /* ifdef DEBUG */
  
#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */

  return(error);
}


/**************************************************************************
 *
 * Function: shutdown_arc4if
 *
 * Called from: cleaup_module() in Linux/dispatch.c
 *
 * Description:
 *
 *************************************************************************/

void 
shutdown_hashif(void) {
  
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
 * Function:  ubsec_hash
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
ubsec_hash(
	   ubsec_DeviceContext_t pContext,
	   ubsec_hash_io_t      *pIOInfo
	   ) {
  
  ubsec_hash_io_t                IOInfo;
  ubsec_SSLCommandInfo_t         SslCommand;
  ubsec_SSLCommandInfo_pt        pSslCommand       = &SslCommand;
  ubsec_HashParams_pt            pHashParams       = &(pSslCommand->Parameters.HashParams);
  volatile CommandContext_t  CommandContext;
  CommandContext_t          *pCommandContext   = (CommandContext_t *)&CommandContext;
  int                            NumCommands       = 1;
  unsigned long		         delayTotalUs      = 0;
  ubsec_FragmentInfo_pt          pSourceFragments  = NULL;
  unsigned char *                pUserSourceBuffer = NULL;
  unsigned int                   SourceBufferBytes = 0;
  unsigned int                   DestBufferBytes   = 0;
  int                            error             = 0;

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  unsigned char *                pKernSourceBuffer = NULL;
  unsigned char *                pKernDestBuffer   = NULL;
  ubsec_MemAddress_t             PhysSourceBuf     = 0;
  ubsec_MemAddress_t             PhysDestBuf       = 0;
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */

  copy_from_user(&IOInfo, pIOInfo, sizeof(ubsec_hash_io_t));
  pUserSourceBuffer = IOInfo.SourceBuffer;
  SourceBufferBytes = IOInfo.SourceBufferBytes;

  DestBufferBytes   = IOInfo.HashBufferBytes;
  SourceBufferBytes = IOInfo.SourceBufferBytes;
  pUserSourceBuffer = IOInfo.SourceBuffer;
  
#if DEBUG
  PRINTK("bytes= %u\n", IOInfo.SourceBufferBytes);
  PRINTK("bytes= %u\n", SourceBufferBytes);
  PRINTK("auth_alg = %u\n", IOInfo.auth_alg);
  PRINTK("bytes= %u\n", IOInfo.HashBufferBytes);
  PRINTK("bytes= %u\n", DestBufferBytes);
#endif

#ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  if(SourceBufferBytes > MAX_FILE_SIZE) {
    PRINTK("input file too large <%d,%d>\n", SourceBufferBytes, MAX_FILE_SIZE);
    error = EINVAL;
    return(error);
  }

#else
  
  pKernSourceBuffer = (char *)kmalloc((SourceBufferBytes),GFP_KERNEL|GFP_ATOMIC);
  if(pKernSourceBuffer == NULL) {
    PRINTK("no memory for source buffer %d\n", SourceBufferBytes);
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pKernSourceBuffer,0, SourceBufferBytes);
  
  pKernDestBuffer = (char *)kmalloc((DestBufferBytes),GFP_KERNEL|GFP_ATOMIC);
  if(pKernDestBuffer == NULL) {
    PRINTK("no memory for dest buffer %d\n", DestBufferBytes);
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pKernDestBuffer,0, DestBufferBytes);
  
#ifdef DEBUG
  PRINTK("Allocate Source: %x %x\n", pKernSourceBuffer, vtophys(pKernSourceBuffer));
  PRINTK("Allocate Source: %x %x\n", pKernDestBuffer, vtophys(pKernDestBuffer));
#endif /* ifdef DEBUG */
  
  PhysSourceBuf = (ubsec_MemAddress_t)(virt_to_bus(pKernSourceBuffer));
  PhysDestBuf   = (ubsec_MemAddress_t)(virt_to_bus(pKernDestBuffer));
  
#ifdef DEBUG
  PRINTK("Memory Alloc Source %x %x Dest %x %x \n", pKernSourceBuffer, PhysSourceBuf, pKernDestBuffer, PhysDestBuf);
#endif /* ifdef DEBUG */
  
#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */

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
    /* The input data requires more fragments than the current driver build can provide; return error */
    error = UBSEC_STATUS_NO_RESOURCE;
    goto ReturnErrorLabel;
  }
  
  copy_from_user(pKernSourceBuffer, pUserSourceBuffer, SourceBufferBytes);
  pSslCommand->Command = UBSEC_HASH;
  if(IOInfo.auth_alg == MAC_SHA1) {
    pSslCommand->Command |= UBSEC_MAC_SHA1;
  } else {
    pSslCommand->Command |= UBSEC_MAC_MD5;
  }
  
#if DEBUG
  PRINTK("auth_alg = x%x\n", IOInfo.auth_alg);
  PRINTK("command = x%x\n", pSslCommand->Command);
#endif
  
  /* Assemble Destination Fragment */

  pSslCommand->NumDestination             = 0;
  pHashParams->OutputHMAC.FragmentAddress = PhysDestBuf;
  pHashParams->OutputHMAC.FragmentLength  = 0; /* fixed output length */
  
  pCommandContext->CallBackStatus = 0;
  pSslCommand->CommandContext     = (unsigned long) pCommandContext;
  pSslCommand->CompletionCallback = CmdCompleteCallback;

  start_time(&(pCommandContext->tv_start));
  
#if DEBUG
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
    error = ETIMEDOUT;
    goto ReturnErrorLabel;
    break;
    
  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK("  ubsec_SslCommand() invalid parameter\n");
    error = EINVAL;
    goto ReturnErrorLabel;
    break;
    
  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK(" ubsec_SslCommand() no resource. Number done: %d\n", NumCommands);
    error = ENOBUFS;
    goto ReturnErrorLabel;
    break;
    
  default:
    error = ENOMSG;
    goto ReturnErrorLabel;
    break;
  }
  
#ifdef GOTOSLEEP
  
  if(!(pCommandContext->CallBackStatus))  {/* Just in case completed on same thread. */
    Gotosleep(&pCommandContext->WaitQ);
    if (!pCommandContext->CallBackStatus) {
              pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
              error = ETIMEDOUT;
              ubsec_ResetDevice(pContext);
              goto ReturnErrorLabel;
         }
    }                          
  
#else
  
  for(delayTotalUs = 1; !(CommandContext.CallBackStatus); delayTotalUs++) {
    
#ifdef POLL
    
    /* We need to poll the device if we are operating in POLL mode. */
    ubsec_PollDevice(pContext);
    
#endif
    
    if(delayTotalUs >= 3000000) {
      error = ETIMEDOUT;
      ubsec_ResetDevice(pContext);
      goto ReturnErrorLabel;
    }
    udelay(1);
  }
  
#endif

#if DEBUG
  PRINTK("Linux:ubsec_hash: Dest Buffer (post-SRL)\n");
  for(i = 0; i < UBSEC_HMAC_LENGTH; i++) {
    PRINTK("%02X", pKernDestBuffer[i]);
  }
  PRINTK("\n");
#endif

  IOInfo.time_us = CommandContext.tv_start.tv_sec * 1000000 + CommandContext.tv_start.tv_usec;
  if(IOInfo.result_status == UBSEC_STATUS_SUCCESS) {
    copy_to_user(pIOInfo, &IOInfo, sizeof(ubsec_hash_io_t));
    copy_to_user(pIOInfo->HashBuffer, pKernDestBuffer, DestBufferBytes);
  }
  
 ReturnErrorLabel:
  
#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  if(pKernSourceBuffer != NULL) {
    kfree(pKernSourceBuffer);
    pKernSourceBuffer = NULL;
  }
  if(pKernDestBuffer != NULL) {
    kfree(pKernDestBuffer);
    pKernDestBuffer = NULL;
  }
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */
  
  if(pSourceFragments != NULL) {
    kfree(pSourceFragments);
    pSourceFragments = NULL;
  }

  return(error);
}

