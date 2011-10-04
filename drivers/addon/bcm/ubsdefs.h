
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
 * ubsdefs.h:  Compilation definitions
 */

/*
 * Revision History:
 *
 * 09/xx/99 SOR Created.
 * 07/06/2000 DPA Fixes for SMP operation
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 07/16/2001 RJT Added support for BCM5821
 */

#ifndef _UBSDEFS_H_
#define _UBSDEFS_H_

/**** Constants Definitions ****/
#define UBS_LITTLE_ENDIAN 1
#define UBS_BIG_ENDIAN    2
/*******************************/

/* To allow Makefile to override endianess settings with compiler switches */
#if defined(UBS_CPU_LITTLE_ENDIAN)       
  #define UBS_CPU_ATTRIBUTE         UBS_LITTLE_ENDIAN
#elif defined(UBS_CPU_BIG_ENDIAN) 
  #define UBS_CPU_ATTRIBUTE         UBS_BIG_ENDIAN
#else
  #define UBS_CPU_ATTRIBUTE         UBS_LITTLE_ENDIAN  /* Default CPU endianess */
#endif

#if defined(UBS_CRYPTONET_LITTLE_ENDIAN) 
  #define UBS_CRYPTONET_ATTRIBUTE   UBS_LITTLE_ENDIAN
#elif defined(UBS_CRYPTONET_BIG_ENDIAN)
  #define UBS_CRYPTONET_ATTRIBUTE   UBS_BIG_ENDIAN
#else
  #define UBS_CRYPTONET_ATTRIBUTE   UBS_LITTLE_ENDIAN  /* Default CryptoNet endianess */
#endif

/* Use polling for completion instead of irq */
#undef POLL 

/* Block on completion of a single MCR request. */
#undef BLOCK

#ifdef BLOCK
  #ifndef POLL
    #define POLL
  #endif
#endif

/*
 * Operational definitions.
 * UBSEC_5xxx (Chip Type) is defined on the compiler command line (see Makefile)
 */
#if defined(UBSEC_5820)
  #define UBSEC_582x_CLASS_DEVICE
  #define MCR_MAXIMUM_PACKETS 4 /* For key performance */
#elif defined(UBSEC_582x)
  #define UBSEC_582x_CLASS_DEVICE
  #define MCR_MAXIMUM_PACKETS 4 /* For key performance */
#else
  #undef UBSEC_582x_CLASS_DEVICE
  #define MCR_MAXIMUM_PACKETS 8 /* For key performance */
#endif

#define UBSEC_MAX_FRAGMENTS 20 

#ifndef COMPLETE_ON_COMMAND_THREAD /* To allow Makefile to override. */
  #undef COMPLETE_ON_COMMAND_THREAD		/* allows SRL to attempt to complete requests in same thread as command */
#endif

#define UBSEC_STATS  /* Enable/Disable statistical information. */


/* 5820/5821 Feature set */
#ifdef UBSEC_582x_CLASS_DEVICE
  #define UBSEC_HW_NORMALIZE /* Hardware does the normalization. */
  #define UBSEC_SSL_SUPPORT 
#endif

/* 
 *  Hard enable/disable of key support. 
 *  This must be enabled for all key functions.
 */
#define UBSEC_PKEY_SUPPORT	

#ifdef UBSEC_PKEY_SUPPORT
  #define UBSEC_MATH_SUPPORT /* Can be conditional. */
  #define UBSEC_RNG_SUPPORT  /* Can be conditional. */
  #define UBSEC_DH_SUPPORT   /* Can be conditional. */
  #define UBSEC_DSA_SUPPORT  /* Can be conditional. */
  #define UBSEC_RSA_SUPPORT  /* Can be conditional. */
#endif



/*
 * STATIC_F_LIST when defined allocate memory
 * for the fragment lists as part of the MCR.
 * This is useful to minimize fragmentation of memory. However
 * since it increases the size of the MCR large MCR/packet/fragment
 * combinations (see below) can cause the allocation to fail if the
 * system does not support large DMA memory allocations. When undefined
 * separate memory blocks are used in the memory allocation and these
 * allow the memory allocation to succeed.
 */
#undef STATIC_F_LIST

/* Allocate MCRs contiguously */
#undef CONTIG_MCR

/* Cacheline size of target platform.                        */
/* A value of zero disables struct alignment optimizations.  */
/* If enabled (!=0), OS_AllocateDMAMemory() must allocate    */
/* memory blocks aligned to a cacheline boundary.            */
#define SYS_CACHELINE_SIZE 64     

#endif  /* _UBSDEFS_H_ */











