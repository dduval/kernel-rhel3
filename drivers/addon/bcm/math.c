
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
 * math.c: Character driver interface to MATH routines in the ubsec driver
 *
 * SOR
 * JJT
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 * 10/09/2001 SRM 64 bit port
 */

#include "cdevincl.h"

/* Maximum key size in bytes */
#define MAX_KEY_BYTE_SIZE 256

/* Intermediate key copy location offsets */

/* Offset of Key information within kernel buffer. */
#define MATH_MODN_OFFSET 	0
#define MATH_MODN2_OFFSET 	1
#define MATH_PARAMA_OFFSET 	2
#define MATH_PARAMB_OFFSET 	3
#define MATH_PARAMC_OFFSET 	4
#define MATH_PARAMD_OFFSET 	5
#define MATH_RESULT_OFFSET 	6
#define MATH_RESULT2_OFFSET 	7

#define MAX_NUM_MATH_PARAMS 8

#ifdef POLL
#undef GOTOSLEEP
#endif

static int ubsec_mathsetup(unsigned long command, 
	ubsec_MathCommandParams_pt pIOparams, ubsec_MathCommandParams_pt pSRLparams, 	
	unsigned char *MathLoc);

void ubsec_pkey(ubsec_io_t *at);

/**************************************************************************
 *
 *  Function:  init_pkeyif
 *   
 *************************************************************************/
int init_mathif(void)
{
 return 0; /* success */
}



/**************************************************************************
 *
 *  Function:  cleanup_module
 *
 *************************************************************************/
void shutdown_mathif(void)
{
  return;
}

/*
 * Math function  setup function. Builds MathCommandInfo for call to SRL.
 * Returns number of bits to normalize.
 */
static int ubsec_mathsetup(unsigned long command, 
			   ubsec_MathCommandParams_pt pSRLparams, 	
			   ubsec_MathCommandParams_pt pIOparams, 
			   unsigned char *MathLoc)
{
  /*
   * Do a brute force copy of the math command. This will set the
   * lengths etc but we still need to set the pointers.
   */
  *pSRLparams=*pIOparams;


  /* Validation of the Length */
  if (command == UBSEC_MATH_DBLMODEXP) { 
    CHECK_SIZE(pIOparams->ModN.KeyLength , 512);
    CHECK_SIZE(pIOparams->ModN2.KeyLength , 512);
  }
  else {
    CHECK_SIZE(pIOparams->ModN.KeyLength , MAX_MATH_LENGTH_BITS);
  }

  /* 
   * Setup Math parameter locations and align them. Start with modulus N.
   */
  copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_MODN_OFFSET],pIOparams->ModN.KeyValue,
	 ROUNDUP_TO_32_BIT(pIOparams->ModN.KeyLength)/8);
  /*  Modulus is a virtual address. */
  pSRLparams->ModN.KeyValue=(void *)&MathLoc[MAX_KEY_BYTE_SIZE*MATH_MODN_OFFSET];

  /* Always copy in paramA */
   /* Validation of the Length */
   CHECK_SIZE(pIOparams->ParamA.KeyLength , MAX_MATH_LENGTH_BITS);
  copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMA_OFFSET],pIOparams->ParamA.KeyValue,
	 ROUNDUP_TO_32_BIT(pIOparams->ParamA.KeyLength)/8);
  pSRLparams->ParamA.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMA_OFFSET]);

  /* Optionally copy in paramB */
  if (command!=UBSEC_MATH_MODREM) {
   /* Validation of the Length */
   CHECK_SIZE(pIOparams->ParamB.KeyLength , MAX_MATH_LENGTH_BITS);
    copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMB_OFFSET],pIOparams->ParamB.KeyValue,
	 ROUNDUP_TO_32_BIT(pIOparams->ParamB.KeyLength)/8);
    pSRLparams->ParamB.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMB_OFFSET]);
  }
   
  pSRLparams->Result.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_RESULT_OFFSET]);
  pSRLparams->Result.KeyLength = pIOparams->ModN.KeyLength;
 
  /* Optionally copy in the Double ModExp params */
  if (command == UBSEC_MATH_DBLMODEXP) {

    /* Second modulus N2 */
    copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_MODN2_OFFSET],pIOparams->ModN2.KeyValue,
		    ROUNDUP_TO_32_BIT(pIOparams->ModN2.KeyLength)/8);
    /*  Modulus is a virtual address. */
    pSRLparams->ModN2.KeyValue=(void *)&MathLoc[MAX_KEY_BYTE_SIZE*MATH_MODN2_OFFSET];

    /* Parameter C */
    /* Validation of the Length */
    CHECK_SIZE(pIOparams->ParamC.KeyLength , MAX_MATH_LENGTH_BITS);
    copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMC_OFFSET],pIOparams->ParamC.KeyValue,
		    ROUNDUP_TO_32_BIT(pIOparams->ParamC.KeyLength)/8);
    pSRLparams->ParamC.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMC_OFFSET]);

    /* Parameter D */
    /* Validation of the Length */
    CHECK_SIZE(pIOparams->ParamD.KeyLength , MAX_MATH_LENGTH_BITS);
    copy_from_user( &MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMD_OFFSET],pIOparams->ParamD.KeyValue,
		    ROUNDUP_TO_32_BIT(pIOparams->ParamD.KeyLength)/8);
    pSRLparams->ParamD.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_PARAMD_OFFSET]);

    /* Second result Result2 */
    pSRLparams->Result2.KeyValue=(void *) (&MathLoc[MAX_KEY_BYTE_SIZE*MATH_RESULT2_OFFSET]);
    pSRLparams->Result2.KeyLength = pIOparams->ModN2.KeyLength;
  }

  return 0;
}

/*
 *
 */
int
ubsec_math(ubsec_DeviceContext_t pContext,
	   ubsec_math_io_t *pIOInfo)
{
  ubsec_MathCommandInfo_pt	kcmd;
  ubsec_MathCommandParams_pt	pMathparams=NULL, 
				pIOparams = NULL;
#ifndef GOTOSLEEP
  unsigned long			delay_total_us;
#endif
  unsigned char			*MathLoc;
  unsigned int			num_commands=1;
  ubsec_math_io_t		MathCommand;
  ubsec_math_io_pt		pMathIOInfo=&MathCommand;
  int				error = 0;
  CommandContext_pt		pCommandContext;
  char				*pmath_buf=NULL;

  pmath_buf = (char *) kmalloc((4096),GFP_KERNEL|GFP_ATOMIC);
  if( pmath_buf == NULL ) {
    PRINTK("no memory for math buffer\n");
    return -ENOMEM;
  }
  memset(pmath_buf,0,4096);

  copy_from_user( pMathIOInfo, pIOInfo, sizeof(*pMathIOInfo));
  pIOparams=&pMathIOInfo->Math;

#if 0
#ifdef GOTOSLEEP
  WaitQ= (struct wait_queue *)pmath_buf;
  kcmd=&WaitQ[1];
#else
  kcmd = (ubsec_MathCommandInfo_pt)pmath_buf;
#endif
  tv_start =(struct timeval *)(&kcmd[1]);
  pStatus = (long *)(&tv_start[1]);
  pCallbackStatus = (int *)(&pStatus[1]);

	/* Set up starting key location. */
  MathLoc=(unsigned char *)&pCallbackStatus[1];
#endif

  pCommandContext = (CommandContext_pt)pmath_buf;
  kcmd = (ubsec_MathCommandInfo_pt)&pCommandContext[1];
  pMathparams=&kcmd->Parameters;
  MathLoc=(unsigned char *)&kcmd[1];

#ifndef LINUX2dot2
  init_waitqueue_head(&pCommandContext->WaitQ);
#else
   pCommandContext->WaitQ         = 0; 
#endif
    
	/*
	 * Now we need to format the command for the SRL.
	 * This depends on the type of the key command.
	 */
  switch (kcmd->Command=pMathIOInfo->command) {
  case UBSEC_MATH_MODADD :
  case UBSEC_MATH_MODSUB :
  case UBSEC_MATH_MODMUL :
  case UBSEC_MATH_MODEXP :
  case UBSEC_MATH_MODREM :
  case UBSEC_MATH_DBLMODEXP:
    	if(ubsec_mathsetup(pMathIOInfo->command, &kcmd->Parameters,
			       &pMathIOInfo->Math, MathLoc) < 0) {
	error = -EINVAL;
	goto Free_Return;
	}
    break;

  default:
    PRINTK("Invalid Math Command %lx\n",kcmd->Command);
    return EINVAL;
  }



  kcmd->CompletionCallback = CmdCompleteCallback;
  kcmd->CommandContext=(unsigned long)pmath_buf;

	/*
	 *  Let the system do anything it may want/need to do before we begin
	 *  timing.
	 */
  do_gettimeofday(&pCommandContext->tv_start);
  
  pCommandContext->CallBackStatus=0; /* inc'd on callback */
  switch (pMathIOInfo->result_status=ubsec_MathCommand(pContext,kcmd,&num_commands) ) {
  case UBSEC_STATUS_SUCCESS:
    break;
  case UBSEC_STATUS_TIMEOUT:
    PRINTK("ubsec_Command() Timeout\n");
    ubsec_ResetDevice(pContext);
    error = -ETIMEDOUT;
    goto Return;
    break;

  case UBSEC_STATUS_INVALID_PARAMETER:
    PRINTK("ubsec_Command() Invalid parameter\n");
    error = -EINVAL;
    goto Return;
    break;

  case UBSEC_STATUS_NO_RESOURCE:
    PRINTK(" ubsec_Command() No math resource. Num Done %d\n",num_commands);
    error = -ENOBUFS;
  default:
    error = -EIO;
    goto Return;
    break;
  }

#ifndef GOTOSLEEP      /* We need to poll the device if we are operating in POLL mode. */
  for (delay_total_us=1  ; !pCommandContext->CallBackStatus ; delay_total_us++) {
#ifdef POLL
    ubsec_PollDevice(pContext);
#endif
    if (delay_total_us >= 30000000) {
      PRINTK("Command timeout\n");
      pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
      error = ETIMEDOUT;
      ubsec_ResetDevice(pContext);
      goto Return;
    }
    udelay(1);
  }
#else
  if (!pCommandContext->CallBackStatus) { /* Just in case completed on same thread. */
    Gotosleep(&pCommandContext->WaitQ);
    if (!pCommandContext->CallBackStatus) {
              pCommandContext->Status=UBSEC_STATUS_TIMEOUT;
      	      ubsec_ResetDevice(pContext);
	      error = ETIMEDOUT;
	      goto Return;
    }                          
  }

#endif

  pMathIOInfo->result_status = pCommandContext->Status;
  pMathIOInfo->time_us = pCommandContext->tv_start.tv_sec * 1000000 + 
	pCommandContext->tv_start.tv_usec;

  /*
   * Status gets set above to timeout or in callback to success indicator.
   */
  if (pMathIOInfo->result_status == UBSEC_STATUS_SUCCESS) {
  	/* 
  	 * Now we need to copyout those parameters that were changed
  	 */
    pIOparams->Result.KeyLength = pMathparams->Result.KeyLength;
    copy_to_user(pIOparams->Result.KeyValue,
		 &MathLoc[MAX_KEY_BYTE_SIZE*MATH_RESULT_OFFSET],
		 ROUNDUP_TO_32_BIT(pIOparams->Result.KeyLength)/8);
    /* Optionally copy out the Double ModExp params */
    if (pMathIOInfo->command == UBSEC_MATH_DBLMODEXP) {
      pIOparams->Result2.KeyLength = pMathparams->Result2.KeyLength;
      copy_to_user(pIOparams->Result2.KeyValue,
		   &MathLoc[MAX_KEY_BYTE_SIZE*MATH_RESULT2_OFFSET],
		   ROUNDUP_TO_32_BIT(pIOparams->Result2.KeyLength)/8);
    }

  } else {
		error = -ENOMSG;
	}

 Return:
	/*
	 * Copyback the result
	 */
	copy_to_user(pIOInfo, pMathIOInfo, sizeof(*pMathIOInfo));
 Free_Return:
  	if (pmath_buf) kfree(pmath_buf);
	return error;
}

