
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
 * ubssys.h:  ubsec operating system dependencies
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created.
 * 12/02/1999 DWP Added macros to swap bytes for Little and Big endian hosts
 * 12/15/1999 SOR Added bsd include file
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/13/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/16/2001 RJT Added support for BCM5821
 * 10/09/2001 SRM 64 bit port.
 */


#ifndef _UBSSYS_H_
#define _UBSSYS_H_


#if defined(UBSEC_5805)
  #if (defined(UBSEC_5820) || defined(UBSEC_582x))
    ERROR - Multiple driver types simultaneously defined 
  #endif
#elif defined(UBSEC_5820)
  #if (defined(UBSEC_5805) || defined(UBSEC_582x))
    ERROR - Multiple driver types simultaneously defined 
  #endif
#elif defined(UBSEC_582x)
  #if (defined(UBSEC_5805) || defined(UBSEC_5820))
    ERROR - Multiple driver types simultaneously defined 
  #endif
#endif


#ifdef UBS_PLATFORM_H_FILE
  #include UBS_PLATFORM_H_FILE
#else
  #ifdef LINUX_DEVICE
  #define UBS_UINT32 unsigned int
  #include "ubslinux.h"
  #endif
  #ifdef WIN32_DEVICE
  #include "ubsnt.h"
  #endif
  #ifdef BSD_DEVICE
  #include "ubsbsd.h"
  #endif
  #ifdef VXWORKS_DEVICE 
  #include "ubsvxworks.h"
  #endif
  #ifdef SOLARIS_DEVICE
  #include "ubssolaris.h"
  #endif
#endif

#ifndef UBS_IOWR 
#define UBS_IOWR(magic,no,arg) (no)
#endif

#ifndef OS_EnterCriticalSection
  #define OS_EnterCriticalSection(x,y) 0
#endif

#ifndef OS_TestCriticalSection
  #define OS_TestCriticalSection(x,y) 0
#endif

#ifndef OS_SyncToDevice
  #define OS_SyncToDevice(x,y,z)
#endif

#ifndef OS_SyncToCPU
  #define OS_SyncToCPU(x,y,z)
#endif

#ifndef UBS_UINT32
#define UBS_UINT32 unsigned int
#endif

#define BYTESWAPSHORT(sval) ((((sval)&0xff00)>>8)+(((sval)&0xff)<<8))
#define BYTESWAPLONG(lval) (((BYTESWAPSHORT((lval)>>16)))+((BYTESWAPSHORT((lval)&0xffff)<<16)))


#if defined(UBS_ENABLE_SWAP_KEY)
  #undef UBS_ENABLE_SWAP_KEY
#endif

#ifndef VOLATILE
#define VOLATILE volatile
#endif


#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE) 

  #define CPU_TO_CTRL_LONG( lval )  (BYTESWAPLONG( (UBS_UINT32)(lval) ))
  #define CTRL_TO_CPU_LONG( lval )  (BYTESWAPLONG( (UBS_UINT32)(lval) ))
  #define CPU_TO_CTRL_SHORT( sval ) (BYTESWAPSHORT( (unsigned short)(sval) ))
  #define CTRL_TO_CPU_SHORT( sval ) (BYTESWAPSHORT( (unsigned short)(sval) ))

  #define SYS_TO_BE_LONG( lval )  ((UBS_UINT32)(lval))

  #if !defined(UBS_OVERRIDE_LONG_KEY_MODE)
    #define UBS_ENABLE_KEY_SWAP
  #endif

#else /* CPU and CryptoNet device have the same endianess */

  #define CPU_TO_CTRL_LONG( lval )  ((UBS_UINT32)(lval))
  #define CTRL_TO_CPU_LONG( lval )  ((UBS_UINT32)(lval))
  #define CPU_TO_CTRL_SHORT( sval ) ((unsigned short)(sval))
  #define CTRL_TO_CPU_SHORT( sval ) ((unsigned short)(sval))

  #if defined(UBS_OVERRIDE_LONG_KEY_MODE)
    #define UBS_ENABLE_KEY_SWAP
  #endif

  #define SYS_TO_BE_LONG( lval )  (BYTESWAPLONG((UBS_UINT32)(lval)))

#endif /* CPU and CTRLMEM endianess considerations */

#if (UBS_CPU_ATTRIBUTE == UBS_BIG_ENDIAN) 

  /* byteswap for runtime (little endian) CryptoNet register accesses */
  #define CPU_TO_PCI_LONG( lval )  (BYTESWAPLONG( (UBS_UINT32)(lval) ))
  #define PCI_TO_CPU_LONG( lval )  (BYTESWAPLONG( (UBS_UINT32)(lval) ))

#else 

  /* CPU (and CryptoNet registers) are both little endian, no byteswap needed */
  #define CPU_TO_PCI_LONG( lval )  ((UBS_UINT32)(lval))
  #define PCI_TO_CPU_LONG( lval )  ((UBS_UINT32)(lval))

#endif /* CPU and CryptoNet endianess considerations */

#define LOW_BYTE(x) (x&0xff)
#define HIGH_BYTE(x) (((x)&0xff00)>>8)

#define ROUNDUP_TO_32_BIT(n) ((n+31)&(~31)) 

#endif  /* _UBSSYS_H_ */

