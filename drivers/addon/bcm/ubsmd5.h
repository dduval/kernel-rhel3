
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
 * ubsmd5.h:
 */

/*
 * Revision History:
 *
 */

#ifndef _MD5_H_
#define _MD5_H_

#define MD5_BLOCK_LENGTH  64		/* in bytes */
#define MD5_HASH_LENGTH   16 		/* in bytes */


#if 0
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;
#endif


typedef int uint32;
struct MD5Context {
	uint32 buf[4];
	uint32 bits[2];
	unsigned char in[64];
};


#endif /* _MD5_H_ */

