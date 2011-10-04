
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
 * rng.c: Character driver interface to RNG routines in the ubsec driver
 *
 * SOR
 * JJT
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 */

#include "cdevincl.h"
#define NORMALIZE 0
/*#define PRG_BUFFER_SIZE 4096*/

#ifdef POLL
#undef GOTOSLEEP
#endif

/* Maximum key size in bytes */
#define MAX_RNG_BYTE_SIZE  (ROUNDUP_TO_32_BIT(MAX_RNG_LENGTH_BITS)/8)

/* Intermediate key copy location offsets */

/* Offset of Key information within kernel buffer. */
#define RNG_RESULT_OFFSET 	0

#define MAX_NUM_RNG_PARAMS 1

static int ubsec_rngsetup(unsigned long command, 
	ubsec_RNGCommandParams_pt pIOparams, ubsec_RNGCommandParams_pt pSRLparams, 	
	unsigned char *RngLoc);

/**************************************************************************
 *
 *  Function:  init_pkeyif
 *   
 *************************************************************************/
int init_rngif(void)
{
 return 0; /* success */
}



/**************************************************************************
 *
 *  Function:  cleanup_module
 *
 *************************************************************************/
void shutdown_rngif(void)
{
 return;
}

/*
 * Rng function  setup function. Builds RngCommandInfo for call to SRL.
 * Returns number of bits to normalize.
 */
static int ubsec_rngsetup(unsigned long command, 
	ubsec_RNGCommandParams_pt pIOparams, ubsec_RNGCommandParams_pt pSRLparams, 	
	unsigned char *RngLoc)
{
  int			NormBits = 0;


  /*
   * Do a brute force copy of the rng command. This will set the
   * lengths etc but we still need to set the pointers.
   */
  *pSRLparams=*pIOparams;

  pSRLparams->Result.KeyValue=(void *)&RngLoc[0];

  /* Validation of the Length */
  CHECK_SIZE(  pSRLparams->Result.KeyLength, MAX_RNG_LENGTH_BITS);

  return NormBits;
}

/*
 *
 */
int
ubsec_rng(ubsec_DeviceContext_t *ubsecContext, ubsec_rng_io_t *pIOInfo)
{
  ubsec_RNGCommandInfo_pt	kcmd;
  ubsec_RNGCommandParams_pt	pRngparams=NULL, 
				pIOparams = NULL;
  unsigned long			delay_total_us;
  unsigned char			*RngLoc;
  unsigned int			num_commands=1;
  ubsec_rng_io_t		RngCommand;
  ubsec_rng_io_pt		pRngIOInfo=&RngCommand;
  int				error = 0;
  CommandContext_pt		pCommandContext;
  unsigned char 		*prng_buf = NULL;
/*
  int i;
*/


  prng_buf = (char *) kmalloc((MAX_RNG_BYTE_SIZE ),GFP_KERNEL|GFP_ATOMIC);
  if( prng_buf == NULL ) {
    PRINTK("no memory for rng buffer\n");
    return -ENOMEM;
  }

  memset(prng_buf,0,MAX_RNG_BYTE_SIZE);

  pCommandContext = (CommandContext_pt)prng_buf;
  kcmd = (ubsec_RNGCommandInfo_pt)&pCommandContext[1];
  RngLoc=(unsigned char *)&kcmd[1];
  pRngparams=&(kcmd->Parameters);

		
		

  copy_from_user( pRngIOInfo, pIOInfo, sizeof(*pRngIOInfo));
  pIOparams=&pRngIOInfo->Rng;

#ifndef LINUX2dot2
  init_waitqueue_head(&pCommandContext->WaitQ);
#else
   pCommandContext->WaitQ         = 0; 
#endif 
	/*
	 * Now we need to format the command for the SRL.
	 * This depends on the type of the key command.
	 */
  switch ((kcmd->Command=pRngIOInfo->command)) {
  case UBSEC_RNG_DIRECT:
  case UBSEC_RNG_SHA1:
    if (ubsec_rngsetup(pRngIOInfo->command,&pRngIOInfo->Rng, pRngparams, RngLoc) < 0){
	error = -EINVAL;
	goto Free_Return;
	}
    break;
  default:
    PRINTK("Invalid Rng Command %lx\n",kcmd->Command);
    error = -EINVAL;
	goto Free_Return;
  }


  kcmd->CompletionCallback = CmdCompleteCallback;
  kcmd->CommandContext=(unsigned long)prng_buf;

	/*
	 *  Let the system do anything it may want/need to do before we begin
	 *  timing.
	 */
  do_gettimeofday(&pCommandContext->tv_start);
  
  pCommandContext->CallBackStatus=0; /* inc'd on callback */
  switch ((pRngIOInfo->result_status=ubsec_RNGCommand(ubsecContext, kcmd,
	&num_commands))) {
  case UBSEC_STATUS_SUCCESS:
    break;
  case UBSEC_STATUS_TIMEOUT:
    PRINTK("ubsec_Command() Timeout\n");
    error = -ETIMEDOUT;
    goto Return;
    break;
  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK("ubsec_Command() Invalid parameter\n");
    error = -EINVAL;
    goto Return;
    break;
  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK("ubsec_Command() No rng resource. Num Done %d\n",num_commands);
    error = -ENOBUFS;
    goto Return;
    break;
  default:
    error = -EIO;
    goto Return;
    break;
  }

#ifndef GOTOSLEEP
  for (delay_total_us=1 ; !(pCommandContext->CallBackStatus); delay_total_us++) {
    ubsec_PollDevice(ubsecContext);
    if (delay_total_us >= 3000000) {
      pCommandContext->Status=-ETIMEDOUT;
      error = -ETIMEDOUT;
      ubsec_ResetDevice(ubsecContext);
      goto Return;
    }
    udelay(1);
  }
#else
  if (!pCommandContext->CallBackStatus) { /* Just in case completed on same thread. */
     Gotosleep(&pCommandContext->WaitQ);
     if (!pCommandContext->CallBackStatus) { /* timed out, never got the interrupt */
       ubsec_PollDevice(ubsecContext);       /* Manually poll just in case */
       if (!pCommandContext->CallBackStatus) { 
	 /* The command truly timed out, return error status */
	 pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
	 error = -ETIMEDOUT;
	 ubsec_ResetDevice(ubsecContext);
	 goto Return;
       }
     }
  }
#endif

  pRngIOInfo->result_status = pCommandContext->Status;
  pRngIOInfo->time_us = pCommandContext->tv_start.tv_sec * 1000000 + 
	pCommandContext->tv_start.tv_usec;

  /*
   * Status gets set above to timeout or in callback to success indicator.
   */
  if (pRngIOInfo->result_status == UBSEC_STATUS_SUCCESS) {
  	/* 
  	 * Now we need to copy out those parameters that were changed
  	 */
    pIOparams->Result.KeyLength = pRngparams->Result.KeyLength;
    copy_to_user(pIOparams->Result.KeyValue, RngLoc, 
	ROUNDUP_TO_32_BIT(pIOparams->Result.KeyLength)/8);


  } else {
#if 1
		PRINTK("rng FAILURE %lx\n", (unsigned long)pCommandContext->Status);
#endif
		error = -ENOMSG;
	}

 Return:
	/*
	 * Copyback the result
	 */
	copy_to_user(pIOInfo, pRngIOInfo, sizeof(*pRngIOInfo));
Free_Return:
  	if (prng_buf != NULL)
		kfree(prng_buf);
	return error;
}
