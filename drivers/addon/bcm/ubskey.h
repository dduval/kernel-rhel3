
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
 * ubskey.h: ubsec key specific context definitions.
 *
 * The BCM5805 was previously called the uB5601
 * This file was previously ubs5805.h
 * This file contains all the structure definitions for the key functions
 * of the 58xx chip family
 */

/*
 * Revision History:
 *
 * 09/21/2000 SOR Created from PW-5805.h
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/24/2001 DPA Allow for unnormalize of D-H random number (x) output for BCM5805
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port.
 */


#ifndef _UBSKEY_H_
#define _UBSKEY_H_


/* Context Buffer Operation type */
#if (UBS_CPU_ATTRIBUTE == UBS_CRYPTONET_ATTRIBUTE)
#define OPERATION_DH_PUBLIC    	0x0001
#define OPERATION_DH_SHARED    	0x0002
#define OPERATION_RSA_PUBLIC   	0x0003
#define OPERATION_RSA_PRIVATE  	0x0004
#define OPERATION_DSA_SIGN   	0x0005
#define OPERATION_DSA_VERIFY  	0x0006
#define OPERATION_RNG_DIRECT   	0x0041
#define OPERATION_RNG_SHA1     	0x0042
#define OPERATION_MOD_ADD   	0x0043
#define OPERATION_MOD_SUB     	0x0044
#define OPERATION_MOD_MULT   	0x0045
#define OPERATION_MOD_REDUCT   	0x0046
#define OPERATION_MOD_EXPON   	0x0047
#define OPERATION_MOD_DBLEXP  	0x0049
#else
#define OPERATION_DH_PUBLIC    	0x0100
#define OPERATION_DH_SHARED    	0x0200
#define OPERATION_RSA_PUBLIC   	0x0300
#define OPERATION_RSA_PRIVATE  	0x0400
#define OPERATION_DSA_SIGN   	0x0500
#define OPERATION_DSA_VERIFY  	0x0600
#define OPERATION_RNG_DIRECT   	0x4100
#define OPERATION_RNG_SHA1     	0x4200
#define OPERATION_MOD_ADD   	0x4300
#define OPERATION_MOD_SUB     	0x4400
#define OPERATION_MOD_MULT   	0x4500
#define OPERATION_MOD_REDUCT   	0x4600
#define OPERATION_MOD_EXPON   	0x4700
#define OPERATION_MOD_DBLEXP  	0x4900
#endif

#define NULL_KEY_CONTEXT (KeyContext_pt) 0

/* Context buffer */
/* Different algorithms have different context command buffers */

#ifdef UBSEC_582x_CLASS_DEVICE
#define MAX_KEY_LENGTH_BITS 2048
#else
#define MAX_KEY_LENGTH_BITS 1024
#endif

#define MAX_KEY_LENGTH_BYTES (MAX_KEY_LENGTH_BITS/8)
#define MAX_KEY_LENGTH_WORDS (MAX_KEY_LENGTH_BITS/32)

/* These lengths are defined as longs. */
#define MAX_MODULUS_LENGTH      MAX_KEY_LENGTH_WORDS 	
#define MAX_GENERATOR_LENGTH   	MAX_KEY_LENGTH_WORDS
#define MAX_EXPON_LENGTH        MAX_KEY_LENGTH_WORDS
#define MAX_DP_LENGTH           MAX_KEY_LENGTH_WORDS
#define MAX_DQ_LENGTH           MAX_KEY_LENGTH_WORDS
#define MAX_PRIME_LENGTH        MAX_KEY_LENGTH_WORDS
#define MAX_PRINV_LENGTH        MAX_KEY_LENGTH_WORDS
#define MAX_PRIME_LENGTH        MAX_KEY_LENGTH_WORDS
#define MAX_PRI_KEY_LENGTH      MAX_KEY_LENGTH_WORDS
#define MAX_PUB_KEY_LENGTH      MAX_KEY_LENGTH_WORDS

#define DH_STATIC_SEND_CONTEXT_SIZE (6*sizeof(unsigned short))
/*Diffie-Hellman Send*/
typedef struct DH_Send_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short rng_enable;	       /* Private key x RNG or SW  */ 
  VOLATILE unsigned short private_key_length;  /* Priv key x len in bits   */
  VOLATILE unsigned short generator_length;    /* Generator g len in bits  */
  VOLATILE unsigned short modulus_length;      /* Modulus N Len in bits    */
#else
  VOLATILE unsigned short private_key_length;  /* Priv key x len in bits   */
  VOLATILE unsigned short rng_enable;	       /* Private key x RNG or SW  */ 
  VOLATILE unsigned short modulus_length;      /* Modulus N Len in bits    */
  VOLATILE unsigned short generator_length;    /* Generator g len in bits  */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 Ng[MAX_MODULUS_LENGTH+MAX_GENERATOR_LENGTH];
			 /* Private key is stored in the data buffer */	
} DH_Send_CtxCmdBuf_t, *DH_Send_CtxCmdBuf_pt;

#define DH_STATIC_REC_CONTEXT_SIZE (4*sizeof(unsigned short))
/*Diffie-Hellman Receive*/
typedef struct DH_REC_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short private_key_length;  /* Private key length in bits */
  VOLATILE unsigned short modulus_length;      /* Modulus N Length in bits   */
#else
  VOLATILE unsigned short modulus_length;      /* Modulus N Length in bits   */
  VOLATILE unsigned short private_key_length;  /* Private key length in bits */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 N[MAX_MODULUS_LENGTH];  /* Modulus N  */
} DH_REC_CtxCmdBuf_t, *DH_REC_CtxCmdBuf_pt;

/*Public Key RSA*/
#define RSA_STATIC_PUBLIC_CONTEXT_SIZE (4*sizeof(unsigned short))
typedef struct Pub_RSA_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short exponent_length;   /* Exponent E length in bits */
  VOLATILE unsigned short modulus_length;    /* Modulus N Length in bits  */
#else
  VOLATILE unsigned short modulus_length;    /* Modulus N Length in bits  */
  VOLATILE unsigned short exponent_length;   /* Exponent E length in bits */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 Ng[2*MAX_MODULUS_LENGTH]; /* Modulus N and g fields */
} Pub_RSA_CtxCmdBuf_t, *Pub_RSA_CtxCmdBuf_pt;

/*Public Key RSA*/
#define RSA_STATIC_PRIVATE_CONTEXT_SIZE  (4*sizeof(unsigned short))
  typedef struct Pri_RSA_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short q_length; 	         /* Prime q length in bits */
  VOLATILE unsigned short p_length; 		 /* Prime p Length in bits */
#else
  VOLATILE unsigned short p_length; 		 /* Prime p Length in bits */
  VOLATILE unsigned short q_length; 	         /* Prime q length in bits */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 CtxParams[MAX_PRIME_LENGTH+MAX_PRIME_LENGTH+MAX_DP_LENGTH+MAX_DQ_LENGTH+MAX_PRINV_LENGTH];
} Pri_RSA_CtxCmdBuf_t, *Pri_RSA_CtxCmdBuf_pt;

/*DSA signing */
#define DSA_STATIC_SIGN_CONTEXT_SIZE  (6*sizeof(unsigned short))
typedef struct DSA_Sign_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short sha1_enable;	 /* hash performed by SHA1 or by SW */
  VOLATILE unsigned short Reserved;	 /*                                 */
  VOLATILE unsigned short rng_enable;	 /* Private key x by RNG or SW      */ 
  VOLATILE unsigned short p_length; 	 /* Modulus p length in bits        */
#else
  VOLATILE unsigned short Reserved;	 /*                                 */
  VOLATILE unsigned short sha1_enable;	 /* hash performed by SHA1 or by SW */
  VOLATILE unsigned short p_length; 	 /* Modulus p length in bits        */
  VOLATILE unsigned short rng_enable;	 /* Private key x by RNG or SW      */ 
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 CtxParams[4*MAX_MODULUS_LENGTH]; /* q,p,g,x */
} DSA_Sign_CtxCmdBuf_t, *DSA_Sign_CtxCmdBuf_pt;

/*DSA Verification */
#define DSA_STATIC_VERIFY_CONTEXT_SIZE  (6*sizeof(unsigned short))
typedef struct DSA_Verify_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short sha1_enable;	 /* hash performed by SHA1 or by SW */
  VOLATILE unsigned short Reserved;	 /*                                 */
  VOLATILE unsigned short Reserved2;     /* not used                        */
  VOLATILE unsigned short p_length; 	 /* Modulus p length in bits        */
#else
  VOLATILE unsigned short Reserved;	 /*                                 */
  VOLATILE unsigned short sha1_enable;	 /* hash performed by SHA1 or by SW */
  VOLATILE unsigned short p_length; 	 /* Modulus p length in bits        */
  VOLATILE unsigned short Reserved2;     /* not used                        */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 CtxParams[4*MAX_MODULUS_LENGTH]; /* q,p,g,y */
} DSA_Verify_CtxCmdBuf_t, *DSA_Verify_CtxCmdBuf_pt;

/* RNG Bypass */
#define RNG_STATIC_CONTEXT_SIZE 64
typedef struct RNG_CtxCmdBuf_struct {
  UBS_UINT32 none;
} RNG_CtxCmdBuf_t, *RNG_CtxCmdBuf_pt;


/* Generic math context buffer */
#define MATH_STATIC_CONTEXT_SIZE  (4*sizeof(unsigned short))
typedef struct Math_CtxCmdBuf_struct {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short exponent_length; /* Exponent (N-2) length in bits */
                                           /* 0 when not present            */
  VOLATILE unsigned short modulus_length;  /* Modulus N Length in bits 	    */
#else
  VOLATILE unsigned short modulus_length;  /* Modulus N Length in bits 	    */
  VOLATILE unsigned short exponent_length; /* Exponent (N-2) length in bits */
                                           /* 0 when not present            */
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 NE[2*MAX_MODULUS_LENGTH];  /* Modulus N, N2 (dblmodexp only) */
} Math_CtxCmdBuf_t, *Math_CtxCmdBuf_pt;

typedef union CtxCmdBuf_u {
	DH_Send_CtxCmdBuf_t		DH_Send_CtxCmdBuf;
	DH_REC_CtxCmdBuf_t		DH_REC_CtxCmdBuf;
	Pub_RSA_CtxCmdBuf_t		Pub_RSA_CtxCmdBuf;
	Pri_RSA_CtxCmdBuf_t		Pri_RSA_CtxCmdBuf;
	DSA_Sign_CtxCmdBuf_t		DSA_Sign_CtxCmdBuf;
	DSA_Verify_CtxCmdBuf_t		DSA_Verify_CtxCmdBuf;
        RNG_CtxCmdBuf_t		        RNG_CtxCmdBuf;
	Math_CtxCmdBuf_t		Math_CtxCmdBuf;
    } CtxCmdBuf_t, *CtxCmdBuf_pt ; 

typedef struct KeyContextUnaligned_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short 	  cmd_structure_length;
  VOLATILE unsigned short 	  operation_type; 
#else
  VOLATILE unsigned short 	  operation_type; 
  VOLATILE unsigned short 	  cmd_structure_length;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE CtxCmdBuf_t  CtxCmdBuf;
  VOLATILE UBS_UINT32  	  PhysicalAddress;
  /* The following fields are used as redirection when
     renormailization is required after a key operation. */
  void(*UserCallback)(unsigned long Context,ubsec_Status_t Result);
  unsigned long	UserContext;
  long NormBits;
  ubsec_LongKey_pt ResultKey[2];
  ubsec_LongKey_pt ResultRNG;
} KeyContextUnaligned_t;

#define KEYCONTEXT_ALIGNMENT 64 /* Boundary to which KeyContexts will be aligned. Must be power of 2 */

#if (SYS_CACHELINE_SIZE >= KEYCONTEXT_ALIGNMENT) 
  #define KEYCONTEXT_ALIGNMENT_PAD (KEYCONTEXT_ALIGNMENT - (sizeof(KeyContextUnaligned_t) & (KEYCONTEXT_ALIGNMENT-1)))
#else
  #undef KEYCONTEXT_ALIGNMENT_PAD
#endif

/***********************************************************************/
/* Hardware (DMA) version of above structure that is 'alignably' sized */
/* (an integer multiple of KEYCONTEXT_ALIGNMENT bytes in length).      */
/* Any changes made to either structure must be mirrored in the other  */
/***********************************************************************/

typedef struct KeyContext_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short 	  cmd_structure_length;
  VOLATILE unsigned short 	  operation_type; 
#else
  VOLATILE unsigned short 	  operation_type; 
  VOLATILE unsigned short 	  cmd_structure_length;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE CtxCmdBuf_t  CtxCmdBuf;
  VOLATILE UBS_UINT32  	  PhysicalAddress;
  /* The following fields are used as redirection when
     renormailization is required after a key operation. */
  void(*UserCallback)(unsigned long Context,ubsec_Status_t Result);
  unsigned long	UserContext;
  long NormBits;
  ubsec_LongKey_pt ResultKey[2];
  ubsec_LongKey_pt ResultRNG;
#if (SYS_CACHELINE_SIZE >= KEYCONTEXT_ALIGNMENT) 
  /***********************************************************************/
  /**** If KeyContextUnaligned_t is cacheline sized, the following    ****/
  /**** pad array will have a subscript of zero. Under this condition ****/
  /**** the following line should be commented out.                   ****/ 
  unsigned char pad[KEYCONTEXT_ALIGNMENT_PAD];                        /***/
  /***********************************************************************/
#endif
} KeyContext_t, *KeyContext_pt;

#define NULL_KEY_CONTEXT (KeyContext_pt) 0

#define UBSEC_IS_KEY_DEVICE(pDevice) ((UBSEC_IS_CRYPTO_DEVICEID(pDevice->DeviceID))==0) 

#endif /* _UBSKEY_H_ */
