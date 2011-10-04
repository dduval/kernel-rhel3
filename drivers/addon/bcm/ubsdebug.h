
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
 * ubsdebug.h: Ubsec debug helper routines
 */

/*
 * Revision History:
 *
 * 09/xx/99 SOR Created.
 */

/*
 * Debug.h: Macros associated with debug.
 */

#ifndef _UBSDEBUG_H_
#define _UBSDEBUG_H_

#ifdef  UBSDBG

#ifndef Dbg_Test

#define Dbg_Test(class)           \
	((Dbg_PrintEnabled & (class))==class)
#endif /* Dbg_Test */

#ifndef Dbg_Print

#define  Dbg_Print(class, x)                    \
  do  {                                         \
    if ((Dbg_PrintEnabled & (class))==class) 			\
      DbgPrint (x);                               \
  } while (0)
#endif /* Dbg_Print */

#ifndef Dbg_Call
#define  Dbg_Call(class, function)        		 \
  {                                             \
    if ((Dbg_PrintEnabled & (class))==class) 			\
      function;                                 \
  }
#endif /* Dbg_call */

#ifndef ASSERT
#define  ASSERT(x)  \
      DbgPrint(DBG_FATAL,("Assert \"%s\" failed, file %s, line %d.\n",  \
               #x, __FILE__, __LINE__));  

#endif /* ASSERT */
#else /* UBSDBG */

#ifndef Dbg_Test
#define Dbg_Test(class) 0
#endif /* Dbg_Test */

#ifndef Dbg_Print
#define  Dbg_Print(class, x)
#endif /* Dbg_Print  */

#ifndef Dbg_Call
#define	 Dbg_Call(class, function)
#endif /*  Dbg_Call */

#ifndef ASSERT
#define  ASSERT(x)
#endif /* ASSERT */

#endif

/*
 * Classes for the Dbg_Print macro.
 */
#define DBG_ALL	   	0xffffffff
#define	DBG_INIT   	0x00000001 /* debug initialization stuff */
#define	DBG_CMD_FAIL   	0x00000002 /* Debug command failures */
#define	DBG_MD5   	0x00000004 /* Debug MD5 Code */
#define	DBG_SHA1   	0x00000008 /* Debug SHA1 Code */
#define	DBG_FATAL   	0x00000010 /* Debug FATAL conditions */
#define	DBG_VERSION   	0x00000020 /* Debug Version control */
#define DBG_CMD        (0x00000040+DBG_CMD_FAIL) /* Command failure */
#define	DBG_IRQ   	0x00000080 /* Debug IRQ */
#define	DBG_DHKEY   	0x00000200 /* Debug DH  KEY */
#define DBG_PACKET      0x00000400 /* Debug packet processing. */
#define DBG_NORM        0x00000800 /* Debug Normalization routines. */
#define	DBG_RSAKEY   	0x00001000 /* Debug RSA  KEY */
#define	DBG_DSAKEY   	0x00002000 /* Debug DSA  KEY */
#define	DBG_TEST   	0x00004000 /* Debug SelfTest  KEY */
#define DBG_MATH        0x00008000 /* MATH function debug. */
#define DBG_RNG         0x00010000 /* Random Number function debug. */
#define DBG_RESET       0x00020000 /* Indicate Device Reset event */
#define DBG_LOG         0x00040000 /* Log messages */
#define DBG_INITD       0x00080000 /* Init device */
#define	DBG_INITS_LIST 	0x00100000 /* debug initialization list structure  stuff */
#define DBG_MCR_SYNC    0x00200000 /* CPU <-> DMA MCR synchronization events */
#define DBG_CNTXT_SYNC  0x00400000 /* Context synchronization events */
#define DBG_FRAG_SYNC   0x00800000 /* Frag descr synchronization events */
#define DBG_SYNC        (DBG_MCR_SYNC | DBG_CNTXT_SYNC | DBG_FRAG_SYNC)
#define	DBG_INITS 	(DBG_INITD | DBG_INITS_LIST)
#define	DBG_KEY   	(DBG_DHKEY | DBG_RSAKEY | DBG_DSAKEY)
#define DBG_LEVEL  	(DBG_FATAL | DBG_CMD_FAIL)  

#ifndef Dbg_PrintEnabled 
#define Dbg_PrintEnabled DBG_LEVEL
#endif /* Dbg_PrintEnabled */


/* When defined, enables the UBSEC_HW_PROFILE_MARKER macro (in ubsctl.h) */
#undef UBSEC_HW_PROFILE_MARKER_ENABLE

#endif /* _UBSDEBUG_H_ */

