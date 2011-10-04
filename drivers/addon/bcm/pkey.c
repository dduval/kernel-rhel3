
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/*
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
 * May 2000 SOR/JTT Created.
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 */

#include "cdevincl.h"	

#ifdef POLL
#undef GOTOSLEEP
#endif


/**************************************************************************
 *
 *  Function:  init_pkeyif
 *   
 *************************************************************************/
int init_keyif(void)
{
 return 0; /* success */
}

/**************************************************************************
 *
 *  Function:  cleanup_module
 *
 *************************************************************************/
void shutdown_keyif(void)
{
  return;
}

/*
 * ubsec_keysetup:
 */
int ubsec_keysetup(ubsec_DeviceContext_t pContext, ubsec_key_io_t *pKeyIOInfo)
{
  ubsec_KeyCommandInfo_pt	kcmd=NULL;
  unsigned char			*KeyLoc=NULL;
  unsigned int			num_commands=1;
  int				error = 0;
  ubsec_key_io_pt		KeyIOInfo=NULL;
  CommandContext_pt		pCommandContext=NULL;
  unsigned char			*pkey_buf = NULL;
  ubsec_key_io_t                KeyIOInfoForDSA;
  int                           add_dsa_buf_bytes = 0;
  int i;
  int timeout;
  int message_alignment;  

  /* This DSA message aligner ensures that the DSA message will begin on a 64-byte boundary,     */
  /* which guarantees that (non-last) message fragments will have sizes that are integer         */
  /* multiples of 64 (a chip requirement for DSA messages)                                       */

#define DSA_MESSAGE_ALIGNMENT ((64-((sizeof(*pCommandContext)+sizeof(*kcmd)+\
sizeof(*KeyIOInfo)+(MAX_KEY_BYTE_SIZE*DSA_IN_OFFSET))%64))&63)

#ifndef GOTOSLEEP
  unsigned long			delay_total_us;
#endif
  ubsec_FragmentInfo_t DSAMessageFragList[MAX_FRAGMENTS]; 

  memset(DSAMessageFragList,0,sizeof(DSAMessageFragList)); 

  copy_from_user( &KeyIOInfoForDSA,pKeyIOInfo, sizeof(KeyIOInfoForDSA));

  if((KeyIOInfoForDSA.command == UBSEC_DSA_SIGN) || (KeyIOInfoForDSA.command == UBSEC_DSA_VERIFY)) {
    message_alignment = DSA_MESSAGE_ALIGNMENT;
    add_dsa_buf_bytes = KeyIOInfoForDSA.key.DSAParams.InputFragments->FragmentLength + DSA_MESSAGE_ALIGNMENT;
  } else {
    message_alignment = 0;
    add_dsa_buf_bytes = 0;
  }

  /* Allocate temporary buffer for key structure */
  pkey_buf = (unsigned char *) kmalloc((4096+add_dsa_buf_bytes),GFP_KERNEL|GFP_ATOMIC);
  if(pkey_buf == NULL) {
    PRINTK("no memory for key buffer\n");
    return -ENOMEM;
  }
  memset(pkey_buf,0, 4096+add_dsa_buf_bytes);

  pCommandContext = (CommandContext_pt)pkey_buf;
  kcmd = (ubsec_KeyCommandInfo_pt) &pCommandContext[1];
  KeyIOInfo = (ubsec_key_io_pt)&kcmd[1];
  KeyLoc = ((unsigned char *)&KeyIOInfo[1]) + message_alignment;
  
  copy_from_user( KeyIOInfo, pKeyIOInfo, sizeof(*KeyIOInfo));

  /* DSA needs extra indirection setup */

  if ((error = KeyCommandCopyin((kcmd->Command = KeyIOInfo->command),
				&kcmd->Parameters, &KeyIOInfo->key, KeyLoc,&DSAMessageFragList[0])) < 0) {
    PRINTK("KeyCommandCopyin failed.\n");
    KeyIOInfo->result_status = UBSEC_STATUS_INVALID_PARAMETER;
    goto Return;
  }

  /* Set up callback function. */
  kcmd->CompletionCallback = CmdCompleteCallback;
  kcmd->CommandContext=(unsigned long)pkey_buf;

  /* Remember calling process. */
  pCommandContext->pid = current->pid;

  /* Initialize start timer. */
  do_gettimeofday(&pCommandContext->tv_start);
  pCommandContext->CallBackStatus = 0;

#ifndef LINUX2dot2
  init_waitqueue_head(&pCommandContext->WaitQ);
#else
   pCommandContext->WaitQ         = 0; 
#endif

  switch ((KeyIOInfo->result_status = ubsec_KeyCommand(pContext, kcmd, &num_commands))) {
  case UBSEC_STATUS_SUCCESS:
    break;
  case UBSEC_STATUS_TIMEOUT:
    PRINTK("ubsec_Command() Timeout\n");
    error = -ETIMEDOUT;
    goto Return;
    break;
  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK("ubsec_Command:  Invalid parameter\n");
    error = -EINVAL;
    goto Return;
    break;
  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK("ubsec_Command() No key resource. Num Done %d Context %lx\n",
	num_commands, (long unsigned int)pContext);
    error = -ENOBUFS;
    goto Return;
    break;
  case UBSEC_STATUS_DEVICE_FAILED:
    PRINTK("ubsec_Command:  Device Failed.\n");
    error = -EIO;
    goto Return;
    break;
  default:
    PRINTK("ubsec_Command:  Error=%x.\n", KeyIOInfo->result_status);
    error = -EIO;
    goto Return;
    break;
  }

#ifndef GOTOSLEEP      /* We need to poll the device if we are operating in POLL mode. */
  for (delay_total_us=1  ; !pCommandContext->CallBackStatus ; delay_total_us++) {
#ifdef POLL
    ubsec_PollDevice(pContext);
#endif /* POLL */
    if (delay_total_us >= 30000000) {
      PRINTK("pkey timeout in poll wait.\n");
      pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
      break;
    }
    udelay(1);
  }
#else  /* GOTOSLEEP */
  if (!pCommandContext->CallBackStatus) { /* Just in case completed on same thread. */
    timeout = Gotosleep(&pCommandContext->WaitQ);
    if (!pCommandContext->CallBackStatus) {
	      PRINTK("Device timeout\n");
              pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
    }
  }
#endif /* GOTOSLEEP */

  /* Timing information for caller */
  KeyIOInfo->time_us = pCommandContext->tv_start.tv_sec * 1000000 + 
	pCommandContext->tv_start.tv_usec;

  switch ((KeyIOInfo->result_status = pCommandContext->Status)) {
  case UBSEC_STATUS_SUCCESS:
	KeyCommandCopyout(KeyIOInfo->command, &kcmd->Parameters,
		&KeyIOInfo->key, KeyLoc);
	break;
  case UBSEC_STATUS_TIMEOUT:
	error = -ETIMEDOUT;
	ubsec_ResetDevice(pContext);
	break;
  case UBSEC_STATUS_INVALID_PARAMETER:
	PRINTK("pkey FAILURE: UBSEC_STATUS_INVALID_PARAMETER\n");
    	error = -EINVAL;
	break;
  default:
	PRINTK("pkey FAILURE: %x\n", KeyIOInfo->result_status);
	error = -ENOMSG;
  }

Return:

  /*
   * Copy back the result block.
   */
  copy_to_user(pKeyIOInfo, KeyIOInfo, sizeof(*KeyIOInfo));

  if (pkey_buf) kfree(pkey_buf);

  return error;
}

