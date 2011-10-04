/*
	Bruce Schneier's Blowfish.
	Author: Olaf Titz <olaf@bigred.inka.de>

	This code is in the public domain.

	$Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $
*/

#ifndef _BF_H_
#define _BF_H_

#include <asm/types.h>       	/* gives __u32 as an unsigned 32bit integer */
/* PORTABILITY: under non-Linux,
   omit this include and insert an appropriate typedef
*/

#ifdef __KERNEL__
#include <asm/byteorder.h>
#endif
/* PORTABILITY: under non-Linux, omit this include.
   Generic, endian-neutral, slower C routines will be used instead of
   the assembler versions found in the kernel includes.
*/

/* This is ugly, but seems the easiest way to find an endianness test
   which works both in kernel and user mode.
   This is only an optimization - everything works even if none of the
   tests are defined.
*/
#ifdef __BYTE_ORDER
#if __BYTE_ORDER == __BIG_ENDIAN
#define BF_NATIVE_BE
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define BF_NATIVE_LE
#endif
#else
#ifdef __BIG_ENDIAN
#define BF_NATIVE_BE
#endif
#ifdef __LITTLE_ENDIAN
#define BF_NATIVE_LE
#endif
#endif

/* The data block processed by the encryption algorithm - 64 bits */
typedef __u32 Blowfish_Data[2];
/* The key as entered by the user - size may vary */
typedef char Blowfish_UserKey[16];
/* The expanded key for internal use - 18+4*256 words*/
typedef __u32 Blowfish_Key[1042];

/* Byteorder-dependent handling of data encryption: Blowfish is by
   definition big-endian. However, there are broken implementations on
   little-endian machines which treat the data as little-endian.
   This module provides both variants.
 */

/* Native byte order. For internal use ONLY. */
extern void _N_Blowfish_Encrypt(void *dataIn, void *dataOut,
				const Blowfish_Key key);
extern void _N_Blowfish_Decrypt(void *dataIn, void *dataOut,
				const Blowfish_Key key);

#ifndef BF_DONTNEED_BE
/* Big endian. This is the "real" Blowfish. */
#ifdef BF_NATIVE_BE
#define B_Blowfish_Encrypt _N_Blowfish_Encrypt
#define B_Blowfish_Decrypt _N_Blowfish_Decrypt
#else
extern void B_Blowfish_Encrypt(void *dataIn, void *dataOut,
			       const Blowfish_Key key);
extern void B_Blowfish_Decrypt(void *dataIn, void *dataOut,
			       const Blowfish_Key key);
#endif
#endif

#ifndef BF_DONTNEED_LE
/* Little endian. To be compatible with other LE implementations. */
#ifdef BF_NATIVE_LE
#define L_Blowfish_Encrypt _N_Blowfish_Encrypt
#define L_Blowfish_Decrypt _N_Blowfish_Decrypt
#else
extern void L_Blowfish_Encrypt(void *dataIn, void *dataOut,
			       const Blowfish_Key key);
extern void L_Blowfish_Decrypt(void *dataIn, void *dataOut,
			       const Blowfish_Key key);
#endif
#endif

/* User key expansion. This is not byteorder dependent as all common
   implementations get it right (i.e. big-endian). */

extern void Blowfish_ExpandUserKey(const char *userKey, int userKeyLen,
				   Blowfish_Key key);

extern const Blowfish_Key Blowfish_Init_Key;

#endif
