
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
 * param.c: IO parameter manipulation routines
 */
/*
 * Revision History:
 *
 * May 2000 SOR/JTT Created.
 * March 2001 PW Release for Linux 2.4 UP and SMP kernel
 */

#include "cdevincl.h"



extern unsigned long Page_Size;

int
KeyCommandCopyin(unsigned long Command, 
		 ubsec_KeyCommandParams_pt pSRLParams, 
		 ubsec_KeyCommandParams_pt pIOparams,
		 unsigned char *KeyLoc,
		 ubsec_FragmentInfo_pt pDSAMessageFragList)
{

  /*
   * Now we need to format the command for the SRL.
   * This depends on the type of the key command.
   */
  switch (Command) {
  case UBSEC_DH_PUBLIC    :
  case UBSEC_DH_SHARED  :
    if(ubsec_keysetup_Diffie_Hellman(Command, pSRLParams,
					     pIOparams, KeyLoc) != 0)
	return -EINVAL;
    break;

  case UBSEC_RSA_PUBLIC  :
  case UBSEC_RSA_PRIVATE  : 
   if( ubsec_keysetup_RSA(Command, pSRLParams,
				  pIOparams, KeyLoc) != 0)
	return -EINVAL;
    break;

  case UBSEC_DSA_VERIFY: 
  case UBSEC_DSA_SIGN: 
    if (ubsec_keysetup_DSA(Command, pSRLParams,
			   pIOparams, KeyLoc, 
			   pDSAMessageFragList) != 0) {
      return UBSEC_STATUS_NO_RESOURCE;
    }
    break;

  default:
    PRINTK("Invalid Key Command %lx\n",Command);
    return -EINVAL;
  }

return 0 ;
}

/*
 * 
 */
int
KeyCommandCopyout(unsigned long Command, 
		 ubsec_KeyCommandParams_pt pSRLParams, 
		 ubsec_KeyCommandParams_pt pIOparams,
		 unsigned char *KeyLoc)
{
  ubsec_DH_Params_pt pDHSRLparams=NULL,pDHIOparams = NULL;
  ubsec_RSA_Params_pt pRSASRLparams=NULL, pRSAIOparams=NULL;
  ubsec_DSA_Params_pt pDSASRLparams=NULL, pDSAIOparams=NULL;


  /* 
   * Now we need to copyout those parameters that were changed
   */
  switch (Command) {
  case UBSEC_DH_SHARED:
    pDHSRLparams=&pSRLParams->DHParams;
    pDHIOparams=&pIOparams->DHParams;
    pDHIOparams->K.KeyLength = pDHSRLparams->K.KeyLength; 
    copy_to_user(pDHIOparams->K.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DH_K_OFFSET],
		 ROUNDUP_TO_32_BIT(pDHIOparams->K.KeyLength)/8);
    break;

  case UBSEC_DH_PUBLIC:
    pDHSRLparams=&pSRLParams->DHParams;
    pDHIOparams=&pIOparams->DHParams;
    pDHIOparams->Y.KeyLength = pDHSRLparams->Y.KeyLength;
    copy_to_user(pDHIOparams->Y.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DH_Y_OFFSET],
		 ROUNDUP_TO_32_BIT(pDHIOparams->Y.KeyLength)/8);

    pDHIOparams->X.KeyLength = pDHSRLparams->X.KeyLength;
    copy_to_user(pDHIOparams->X.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DH_X_OFFSET],
		 ROUNDUP_TO_32_BIT(pDHIOparams->X.KeyLength)/8);
    break;
  case UBSEC_RSA_PUBLIC  :
  case UBSEC_RSA_PRIVATE  : 
    pRSASRLparams=&pSRLParams->RSAParams;
    pRSAIOparams=&pIOparams->RSAParams;
    pRSAIOparams->OutputKeyInfo.KeyLength =
      pRSASRLparams->OutputKeyInfo.KeyLength;
    copy_to_user(pRSAIOparams->OutputKeyInfo.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_OUT_OFFSET],
		 ROUNDUP_TO_32_BIT(pRSAIOparams->OutputKeyInfo.KeyLength)/8);
    break;

  case UBSEC_DSA_SIGN     : 
    pDSASRLparams=&pSRLParams->DSAParams;
    pDSAIOparams=&pIOparams->DSAParams;
    pDSAIOparams->SigS.KeyLength = pDSASRLparams->SigS.KeyLength;
    pDSAIOparams->SigR.KeyLength = pDSASRLparams->SigR.KeyLength;
    copy_to_user(pDSAIOparams->SigS.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_S_OFFSET],
		 ROUNDUP_TO_32_BIT(pDSAIOparams->SigS.KeyLength)/8);
    copy_to_user(pDSAIOparams->SigR.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_R_OFFSET],
		 ROUNDUP_TO_32_BIT(pDSAIOparams->SigR.KeyLength)/8);
    break;

  case UBSEC_DSA_VERIFY   : 
    pDSAIOparams=&pIOparams->DSAParams;
    pDSASRLparams=&pSRLParams->DSAParams;
    pDSAIOparams->V.KeyLength = pDSASRLparams->V.KeyLength;
    copy_to_user(pDSAIOparams->V.KeyValue,
		 &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_V_OFFSET],
		 ROUNDUP_TO_32_BIT(pDSAIOparams->V.KeyLength)/8);
    break;
  }
return 0 ;
}


/*
 * Diffie Hellman key setup function. Builds KeyCommandInfo for call to SRL.
 */
int
ubsec_keysetup_Diffie_Hellman(unsigned long command, 
			      ubsec_KeyCommandParams_pt pSRLparams, 
			      ubsec_KeyCommandParams_pt pIOparams,
			      unsigned char *KeyLoc)
{
  ubsec_DH_Params_pt 	pDHSRLparams = &pSRLparams->DHParams;
  ubsec_DH_Params_pt 	pDHIOparams = &pIOparams->DHParams;

  /* 
   * Setup key parameter locations and align them. Start with modulus N.
   */

  /* Validation */
 CHECK_SIZE(pDHIOparams->N.KeyLength,MAX_KEY_LENGTH_BITS);
 CHECK_SIZE(pDHIOparams->Y.KeyLength,MAX_KEY_LENGTH_BITS);
  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DH_N_OFFSET],pDHIOparams->N.KeyValue,
	 ROUNDUP_TO_32_BIT(pDHIOparams->N.KeyLength)/8);
  pDHSRLparams->N.KeyValue=(void *)&KeyLoc[MAX_KEY_BYTE_SIZE*DH_N_OFFSET];
  pDHSRLparams->N.KeyLength = pDHIOparams->N.KeyLength;

  /* 
   * Now copy in the specific parameters.
   */
  if ((command & UBSEC_DH_SHARED)==UBSEC_DH_SHARED) {
    /*
     * Computing K=Y**x|N
     */
    /* Validation of the Length */
    CHECK_SIZE(pDHIOparams->K.KeyLength,MAX_KEY_LENGTH_BITS);
    CHECK_SIZE(pDHIOparams->X.KeyLength,MAX_KEY_LENGTH_BITS);
    copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_Y_OFFSET],pDHIOparams->Y.KeyValue,
	   ROUNDUP_TO_32_BIT(pDHIOparams->Y.KeyLength)/8);

    /*
     * Copy in our secret value x. 
     */
    copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_X_OFFSET],pDHIOparams->X.KeyValue,
	   ROUNDUP_TO_32_BIT(pDHIOparams->X.KeyLength)/8);
    pDHSRLparams->X.KeyLength = pDHIOparams->X.KeyLength;
	/*
	 * Output parameter is the shared key. Must represent integral
	 * number of 32 bit words.
	 */
    pDHSRLparams->K.KeyValue=(void *) 
      (&KeyLoc[MAX_KEY_BYTE_SIZE*DH_K_OFFSET]);
    pDHSRLparams->K.KeyLength = pDHIOparams->K.KeyLength;
  } 
  else {
    /*
     * Computing Y=g**x|N
     */
    /* Validation of the Length */
    CHECK_SIZE(pDHIOparams->G.KeyLength,MAX_KEY_LENGTH_BITS);

    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DH_G_OFFSET],pDHIOparams->G.KeyValue,
	   ROUNDUP_TO_32_BIT(pDHIOparams->G.KeyLength)/8);
    pDHSRLparams->G.KeyValue=(void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_G_OFFSET]);
    pDHSRLparams->G.KeyLength = pDHIOparams->G.KeyLength;
    pDHSRLparams->RNGEnable=pDHIOparams->RNGEnable;

    /*
     * X and Y are input/output fragment data and must be physical addresses.
     */
    pDHSRLparams->X.KeyValue=(void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_X_OFFSET]);
 
    if (!pDHSRLparams->RNGEnable) {

      CHECK_SIZE(pDHIOparams->UserX.KeyLength,MAX_KEY_LENGTH_BITS);

      copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_USERX_OFFSET],pDHIOparams->UserX.KeyValue,
	     ROUNDUP_TO_32_BIT(pDHIOparams->UserX.KeyLength)/8);
      pDHSRLparams->UserX.KeyValue=(void *)
	(&KeyLoc[MAX_KEY_BYTE_SIZE*DH_USERX_OFFSET]);
      pDHSRLparams->UserX.KeyLength = pDHIOparams->UserX.KeyLength;
	/*
	 * Set length for return. X will be copied from supplied UserX.
	 */
      pDHIOparams->X.KeyLength = pDHIOparams->UserX.KeyLength;

	/*
	 * User supplied secret value will be copied to the output X.
	 * Output needs to be an integral number of 32-bit words.
	 */
      pDHSRLparams->X.KeyLength=pDHIOparams->UserX.KeyLength;
    }
    else {
      /*
       * Set length for return. X will be the random number
       * generated by the chip. Output needs to be integral 
       * number of 32-bit words.
       */
      CHECK_SIZE(pDHIOparams->X.KeyLength,MAX_KEY_LENGTH_BITS);
      CHECK_SIZE(pDHIOparams->RandomKeyLen,MAX_KEY_LENGTH_BITS);

      pDHIOparams->X.KeyLength = pDHIOparams->RandomKeyLen;
      pDHSRLparams->X.KeyLength = pDHIOparams->X.KeyLength;
      pDHSRLparams->RandomKeyLen=pDHIOparams->RandomKeyLen;
    }
  }

  /*
   * X and Y are input/output fragment data and must be physical addresses.
   */

  pDHSRLparams->X.KeyValue=(void *) (&KeyLoc[MAX_KEY_BYTE_SIZE*DH_X_OFFSET]);
  pDHSRLparams->Y.KeyValue=(void *) (&KeyLoc[MAX_KEY_BYTE_SIZE*DH_Y_OFFSET]);
  pDHSRLparams->Y.KeyLength = pDHIOparams->Y.KeyLength ;

  return 0 ;
}

/*
 * RSA key setup function. Builds KeyCommandInfo for call to SRL.
 */
int
ubsec_keysetup_RSA(unsigned long command, ubsec_KeyCommandParams_pt pSRLparams, 
	ubsec_KeyCommandParams_pt pIOparams,unsigned char *KeyLoc)
{
  ubsec_RSA_Params_pt 	pRSASRLparams=&pSRLparams->RSAParams;
  ubsec_RSA_Params_pt 	pRSAIOparams=&pIOparams->RSAParams;

  /*
   * Message in, message out, same for public or private. Both are
   * fragment data, must be physical address.
   */
  CHECK_SIZE(pRSAIOparams->InputKeyInfo.KeyLength,MAX_KEY_LENGTH_BITS);
  CHECK_SIZE(pRSAIOparams->OutputKeyInfo.KeyLength,MAX_KEY_LENGTH_BITS);

  copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_IN_OFFSET],pRSAIOparams->InputKeyInfo.KeyValue,
	 ROUNDUP_TO_32_BIT(pRSAIOparams->InputKeyInfo.KeyLength)/8);

  /*
   * Output buffer will have to be corrected to be an integral number of 
   * 32-bit words. This will depend on the operation.
   */
  pRSASRLparams->OutputKeyInfo.KeyValue = (void *) 
    (&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_OUT_OFFSET]);

  /*
   * Keys depend on public or private operations.
   */
  if ((command & UBSEC_RSA_PUBLIC)==UBSEC_RSA_PUBLIC) {
    CHECK_SIZE(pRSAIOparams->ModN.KeyLength,MAX_KEY_LENGTH_BITS);
    CHECK_SIZE(pRSAIOparams->ExpE.KeyLength,MAX_KEY_LENGTH_BITS);

    copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_N_OFFSET],pRSAIOparams->ModN.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->ModN.KeyLength)/8);
    pRSASRLparams->ModN.KeyValue=
      (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_N_OFFSET]);
    pRSASRLparams->ModN.KeyLength = pRSAIOparams->ModN.KeyLength;


    pRSASRLparams->OutputKeyInfo.KeyLength=pRSAIOparams->OutputKeyInfo.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_E_OFFSET],pRSAIOparams->ExpE.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->ExpE.KeyLength)/8);
    pRSASRLparams->ExpE.KeyValue= (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_E_OFFSET]);
    pRSASRLparams->ExpE.KeyLength = pRSAIOparams->ExpE.KeyLength;
    pRSASRLparams->InputKeyInfo.KeyLength = pRSAIOparams->InputKeyInfo.KeyLength;
  }
  else { /* Private. */
    CHECK_SIZE(pRSAIOparams->PrimeP.KeyLength,MAX_RSA_PRIVATE_KEY_LENGTH_BITS);
    CHECK_SIZE(pRSAIOparams->PrimeQ.KeyLength,MAX_RSA_PRIVATE_KEY_LENGTH_BITS);
    CHECK_SIZE(pRSAIOparams->PrimeEdp.KeyLength,MAX_RSA_PRIVATE_KEY_LENGTH_BITS);
    CHECK_SIZE(pRSAIOparams->PrimeEdq.KeyLength,MAX_RSA_PRIVATE_KEY_LENGTH_BITS);
    CHECK_SIZE(pRSAIOparams->Pinv.KeyLength,MAX_RSA_PRIVATE_KEY_LENGTH_BITS);
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_P_OFFSET],pRSAIOparams->PrimeP.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->PrimeP.KeyLength)/8);
    pRSASRLparams->PrimeP.KeyValue= (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_P_OFFSET]);
    pRSASRLparams->PrimeP.KeyLength = pRSAIOparams->PrimeP.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_Q_OFFSET],pRSAIOparams->PrimeQ.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->PrimeQ.KeyLength)/8);
    pRSASRLparams->PrimeQ.KeyValue = (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_Q_OFFSET]);
    pRSASRLparams->PrimeQ.KeyLength = pRSAIOparams->PrimeQ.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_EDP_OFFSET],pRSAIOparams->PrimeEdp.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->PrimeEdp.KeyLength)/8);
    pRSASRLparams->PrimeEdp.KeyValue=
      (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_EDP_OFFSET]);
    pRSASRLparams->PrimeEdp.KeyLength = pRSAIOparams->PrimeEdp.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_EDQ_OFFSET],pRSAIOparams->PrimeEdq.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->PrimeEdq.KeyLength)/8);
    pRSASRLparams->PrimeEdq.KeyValue=
      (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_EDQ_OFFSET]);
    pRSASRLparams->PrimeEdq.KeyLength = pRSAIOparams->PrimeEdq.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*RSA_PINV_OFFSET],pRSAIOparams->Pinv.KeyValue,
	   ROUNDUP_TO_32_BIT(pRSAIOparams->Pinv.KeyLength)/8);
    pRSASRLparams->Pinv.KeyValue= (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_PINV_OFFSET]);
    pRSASRLparams->Pinv.KeyLength = pRSAIOparams->Pinv.KeyLength;

    pRSASRLparams->OutputKeyInfo.KeyLength=pRSAIOparams->OutputKeyInfo.KeyLength;
    pRSASRLparams->InputKeyInfo.KeyLength=pRSAIOparams->InputKeyInfo.KeyLength;
  }      

  pRSASRLparams->InputKeyInfo.KeyValue = (void *) 
    (&KeyLoc[MAX_KEY_BYTE_SIZE*RSA_IN_OFFSET]);
  return 0;
}




/*
 * DSA key setup function. Builds KeyCommandInfo for call to SRL.
 */
 int
ubsec_keysetup_DSA(unsigned long command, 
		   ubsec_KeyCommandParams_pt pSRLparams, 
		   ubsec_KeyCommandParams_pt pIOparams,
		   unsigned char *KeyLoc,                    /* Pointer to already malloc'd DMA memory    */
		   ubsec_FragmentInfo_pt pMessageFragList)   /* Pointer to already malloc'd fragment list */
{
  ubsec_DSA_Params_pt 	pDSASRLparams = &pSRLparams->DSAParams; /* points back to SRL parameter structure being built */
  ubsec_DSA_Params_pt 	pDSAIOparams = &pIOparams->DSAParams;   /* points back to IOCTL parameter structure passed in */
  int i; 
  ubsec_FragmentInfo_t IOInputFragment; /* Local (single IOCTL input) fragment for DSA */

  /* Copy single IOCTL fragment info into local copy */
  copy_from_user( &IOInputFragment,pDSAIOparams->InputFragments,sizeof(IOInputFragment));
  pDSAIOparams->InputFragments = &IOInputFragment;   /* points to local (single) fragment descriptor    */
  pDSASRLparams->InputFragments = pMessageFragList;      /* points back to calling fragment list */

  /*
   * Message in, signature in/out, key parameters used for both sign and verify.
   */
  pDSASRLparams->HashEnable = pDSAIOparams->HashEnable;

  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_IN_OFFSET],(void *)pDSAIOparams->InputFragments->FragmentAddress,
		  pDSAIOparams->InputFragments->FragmentLength);
  
  /* Sync the input message DMA memory so that the CryptoNetX device can access it.  */
  /***********************************************************************************/
  /***************************  PLATFORM PORTABILITY ISSUE  **************************/
  /***********************************************************************************/
  /* Strictly speaking, KeyLoc is not the right parameter for the sync macro.        */
  /* It is not an OS_MemHandle_t value returned from a malloc call; it was derived   */
  /* from an OS_MemHandle_t value using pointer math in the virtual address domain.  */
  /* However, OS_MemHandle_t types for Linux-on-a-PC are virtual pointers anyway,    */
  /* so KeyLoc is an acceptable parameter in this case.                              */ 
  /***********************************************************************************/
  /***************************  PLATFORM PORTABILITY ISSUE  **************************/
  /***********************************************************************************/
  OS_SyncToDevice(KeyLoc, MAX_KEY_BYTE_SIZE*DSA_IN_OFFSET, 
		  pDSAIOparams->InputFragments->FragmentLength); /* (MemHandle, offset, bytes) */

  /* The DSA message or message hash is a bytestream. Treat it just like a fragmentable crypto buffer. */
  if ((pDSASRLparams->NumInputFragments = SetupFragmentList(pDSASRLparams->InputFragments,
							    &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_IN_OFFSET],
							    pDSAIOparams->InputFragments->FragmentLength)) == 0) {
    /* Input message for DSA requires too many fragments; return error code */
    return -1;
  }
  for (i=0;i<(pDSASRLparams->NumInputFragments-1);i++) {
    if ((pDSASRLparams->InputFragments[i].FragmentLength)%64)
      /* Input message fragmentation for DSA violates CryptoNet requirements for fragment sizes */
      return -1;
  }
  
  /* If the message has already been hashed, the input hash must be 20 bytes and in a single fragment */
  if ( (!pDSAIOparams->HashEnable) && \
       ((pDSAIOparams->InputFragments->FragmentLength != 20) || (pDSASRLparams->NumInputFragments != 1)) )
    return -1; 
  
  CHECK_SIZE(pDSAIOparams->ModQ.KeyLength,MAX_DSA_MODQ_LENGTH_BITS);
  CHECK_SIZE(pDSAIOparams->ModP.KeyLength,MAX_DSA_KEY_LENGTH_BITS);
  CHECK_SIZE(pDSAIOparams->BaseG.KeyLength,MAX_DSA_KEY_LENGTH_BITS);
  CHECK_SIZE(pDSAIOparams->Key.KeyLength,MAX_DSA_KEY_LENGTH_BITS);

  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_Q_OFFSET],pDSAIOparams->ModQ.KeyValue,
	 ROUNDUP_TO_32_BIT(pDSAIOparams->ModQ.KeyLength)/8);
  pDSASRLparams->ModQ.KeyValue = (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_Q_OFFSET]);
  pDSASRLparams->ModQ.KeyLength = pDSAIOparams->ModQ.KeyLength;

  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_P_OFFSET],pDSAIOparams->ModP.KeyValue,
	 ROUNDUP_TO_32_BIT(pDSAIOparams->ModP.KeyLength)/8);
  pDSASRLparams->ModP.KeyValue = (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_P_OFFSET]);
  pDSASRLparams->ModP.KeyLength = pDSAIOparams->ModP.KeyLength;
  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_G_OFFSET],pDSAIOparams->BaseG.KeyValue,
	 ROUNDUP_TO_32_BIT(pDSAIOparams->BaseG.KeyLength)/8);
  pDSASRLparams->BaseG.KeyValue = (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_G_OFFSET]);
  pDSASRLparams->BaseG.KeyLength = pDSAIOparams->BaseG.KeyLength;

  copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_KEY_OFFSET],pDSAIOparams->Key.KeyValue,
	 ROUNDUP_TO_32_BIT(pDSAIOparams->Key.KeyLength)/8);
  pDSASRLparams->Key.KeyValue = (void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_KEY_OFFSET]);
  pDSASRLparams->Key.KeyLength = pDSAIOparams->Key.KeyLength;

  /*
   * R and S are always input/output fragment data, physical addresses.
   */
  pDSASRLparams->SigS.KeyValue=(void *) (&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_S_OFFSET]);
  pDSASRLparams->SigR.KeyValue=(void *) (&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_R_OFFSET]);

  if ((command & UBSEC_DSA_SIGN) == UBSEC_DSA_SIGN) {
    /*
     * Output will be in R and S, fragment data.
     */
    pDSASRLparams->RNGEnable=pDSAIOparams->RNGEnable;
    if (!pDSASRLparams->RNGEnable) {
      CHECK_SIZE(pDSAIOparams->Random.KeyLength,MAX_DSA_KEY_LENGTH_BITS);
      copy_from_user(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_RAND_OFFSET],pDSAIOparams->Random.KeyValue,
	     ROUNDUP_TO_32_BIT(pDSAIOparams->Random.KeyLength)/8);
      pDSASRLparams->Random.KeyValue = (void *)
	(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_RAND_OFFSET]);
      pDSASRLparams->Random.KeyLength = pDSAIOparams->Random.KeyLength;
    }
    pDSASRLparams->SigS.KeyLength = 160; 
    pDSASRLparams->SigR.KeyLength = 160;     
    /*
     * Set Return lengths
     */
    pDSAIOparams->SigS.KeyLength = pDSAIOparams->ModQ.KeyLength;
    pDSAIOparams->SigR.KeyLength = pDSAIOparams->ModQ.KeyLength;
  } else {
    /*
     * Verify.
     *
     * Input will be in R and S, fragment data.
     */
    CHECK_SIZE(pDSAIOparams->SigS.KeyLength,MAX_DSA_SIG_LENGTH_BITS);
    CHECK_SIZE(pDSAIOparams->SigR.KeyLength,MAX_DSA_SIG_LENGTH_BITS);
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_S_OFFSET],pDSAIOparams->SigS.KeyValue,
	   ROUNDUP_TO_32_BIT(pDSAIOparams->SigS.KeyLength)/8);
    pDSASRLparams->SigS.KeyLength = pDSAIOparams->SigS.KeyLength;
    copy_from_user( &KeyLoc[MAX_KEY_BYTE_SIZE*DSA_R_OFFSET],pDSAIOparams->SigR.KeyValue,
	   ROUNDUP_TO_32_BIT(pDSAIOparams->SigR.KeyLength)/8);
    pDSASRLparams->SigR.KeyLength = pDSAIOparams->SigR.KeyLength;

    /*
     * Output verification value is also fragmented data, must be
     * integral number of 32-bit words.
     */
    pDSASRLparams->V.KeyValue=(void *)(&KeyLoc[MAX_KEY_BYTE_SIZE*DSA_V_OFFSET]);
    pDSASRLparams->V.KeyLength = 160; 
    /*
     * Set value for return.
     */
    pDSAIOparams->V.KeyLength = 160; /*pDSAIOparams->ModQ.KeyLength;*/
  }
	
  return 0;
}



