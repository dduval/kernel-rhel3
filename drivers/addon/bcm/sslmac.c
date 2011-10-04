
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

unsigned char * pKernSourceBuffer = NULL;
unsigned char * pKernDestBuffer   = NULL;
static ubsec_MemAddress_t      PhysSourceBuf;
static ubsec_MemAddress_t      PhysDestBuf;

#endif /* ifdef STATIC_ALLOC_OF_CRYPTO_BUFFERS */


/**************************************************************************
 *
 * Function:  init_sslmacif
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
init_sslmacif(void) {
  
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
 * Function: shutdown_sslmacif
 *
 * Called from: cleaup_module() in Linux/dispatch.c
 *
 * Description:
 *
 *************************************************************************/

void 
shutdown_sslmacif(void) {
  
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
 * Function:  ubsec_sslmac
 * 
 * Called from: ubsec_ioctl() in Linux/dispatch.c
 *
 * Purpose:
 *          Wrapper function between user call and call to SRL.
 *
 * Description: 
 *              Prepare all the data from the user's call for a call to 
 *              the SRL, set up the timer, and finally place this thread
 *              on the wait queue and go to sleep. When ubsec_sslmac is 
 *              called, the relevant data for an SSL MAC op reside in
 *              user space. The struct ubsec_sslmac_io_pt argument, pIOInfo, 
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
ubsec_sslmac(
	     ubsec_DeviceContext_t pContext,
	     ubsec_sslmac_io_t    *pIOInfo
	     ) {
  
  ubsec_sslmac_io_t          IOInfo;
  /* ubsec_sslmac_io_pt         pIOInfo           = &IOInfo; */
  ubsec_SSLCommandInfo_t     SslCommand;
  ubsec_SSLCommandInfo_pt    pSslCommand       = &SslCommand;
  ubsec_SSLMACParams_pt      pSSLMACParams     = &(pSslCommand->Parameters.SSLMACParams);
  volatile  CommandContext_t  CommandContext;
  CommandContext_t          *pCommandContext   = (CommandContext_t *)&CommandContext;
  int                        NumCommands       = 1;
  unsigned long		     delayTotalUs      = 0;
  ubsec_FragmentInfo_pt      pSourceFragments  = NULL;
  unsigned char *            pUserSourceBuffer = NULL;
  unsigned int               SourceBufferBytes = 0;
  unsigned int               DestBufferBytes   = 0;
  int                        error             = 0;

#ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS
  
  unsigned char *           pKernSourceBuffer = NULL;
  unsigned char *           pKernDestBuffer   = NULL;
  ubsec_MemAddress_t        PhysSourceBuf;
  ubsec_MemAddress_t        PhysDestBuf;
  
#endif /* ifndef STATIC_ALLOC_OF_CRYPTO_BUFFERS */

  /* Grab Data From User Space */
  
  copy_from_user(&IOInfo, pIOInfo, sizeof(ubsec_sslmac_io_t));
  
#if 0
  PRINTK("content type = %d\n", IOInfo.ContentType);
  PRINTK("data length = %d\n", IOInfo.DataLength);
#endif

  pUserSourceBuffer = IOInfo.SourceBuffer;
  SourceBufferBytes = IOInfo.SourceBufferBytes;
  
#if 0
  PRINTK("source bytes = %d\n", IOInfo.SourceBufferBytes);
  PRINTK("hmac bytes = %d\n", IOInfo.HmacBufferBytes);
  PRINTK("context type = %02X\n", IOInfo.ContentType);
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
  
  pKernDestBuffer = (char *)kmalloc((UBSEC_HMAC_LENGTH),GFP_KERNEL|GFP_ATOMIC);
  if(pKernDestBuffer == NULL) {
    PRINTK("no memory for dest buffer %d\n", UBSEC_HMAC_LENGTH);
    error = -ENOMEM;
    goto ReturnErrorLabel;
  }
  memset(pKernDestBuffer,0, UBSEC_HMAC_LENGTH);
  
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

  /* Assemble Destination Fragment */

  pSslCommand->NumDestination             = 0;
  pSSLMACParams->OutputHMAC.FragmentAddress = PhysDestBuf;
  pSSLMACParams->OutputHMAC.FragmentLength  = 0; /* fixed output length */

  /* Set Data */

  memcpy(&(pSSLMACParams->key), &(IOInfo.key), sizeof(ubsec_SSLMAC_key_t));  /* memcpy for portability */
  pSSLMACParams->SequenceNumber = IOInfo.SequenceNumber;
  pSSLMACParams->ContentType    = IOInfo.ContentType;
  pSSLMACParams->DataLength     = IOInfo.DataLength;

  /* Assemble SSL Command */

  pSslCommand->Command = UBSEC_SSL_MAC;
  if(IOInfo.HashAlgorithm == MAC_SHA1) {
    pSslCommand->Command |= UBSEC_SSL_MAC_SHA1;
    DestBufferBytes = UBSEC_SHA1_LENGTH;  
  }         
  else {
    pSslCommand->Command |= UBSEC_SSL_MAC_MD5;
    DestBufferBytes = UBSEC_MD5_LENGTH;  
  }
#if 0
  PRINTK("command = x%x\n", pSslCommand->Command);
#endif
  pCommandContext->CallBackStatus         = 0;
  pSslCommand->CommandContext             = (unsigned long) pCommandContext;
  pSslCommand->CompletionCallback         = CmdCompleteCallback;
  
  start_time(&(pCommandContext->tv_start));
  
#if 0
  PRINTK("Linux:ubsec_sslmac just before SRL call...\n");
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
  
  if(!(pCommandContext->CallBackStatus))  /* Just in case completed on same thread. */
    Gotosleep(&pCommandContext->WaitQ);
    if (!pCommandContext->CallBackStatus) {
              pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
      	      error = ETIMEDOUT;
      	      ubsec_ResetDevice(pContext);
      	      goto ReturnErrorLabel;

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

#if 0
  PRINTK("Linux:ubsec_hash: Dest Buffer (post-SRL)\n");
  for(i = 0; i < UBSEC_HMAC_LENGTH; i++) {
    PRINTK("%02X", pKernDestBuffer[i]);
  }
  PRINTK("\n");
#endif

  IOInfo.time_us = CommandContext.tv_start.tv_sec * 1000000 + CommandContext.tv_start.tv_usec;
  if(IOInfo.result_status == UBSEC_STATUS_SUCCESS) {
    copy_to_user(IOInfo.HmacBuffer, pKernDestBuffer, DestBufferBytes);
#if 1
    copy_to_user(pIOInfo, &IOInfo, sizeof(ubsec_sslmac_io_t));
#endif
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

