
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
 * ubssha1.h: 
 * 10/09/2001 SRM 64 bit port.
 */

/*
 * Revision History:
 *
 */

#ifndef _SHA_H_
#define _SHA_H_

/* define the following line to get FIPS 180-1 enhancements */
#define SHA_UPDATE

#define SHA_BLOCK_LENGTH  64		/* in bytes */
#define SHA_HASH_LENGTH   20		/* in bytes */

typedef unsigned char BYTE;


typedef struct {
    UBS_UINT32 Numbytes;
    UBS_UINT32 Numblocks[2];  /* each block contains 64 bytes */
    UBS_UINT32 Mblock[16];
    UBS_UINT32 buffer[5];
} SHA_CTX;


#endif /*_SHA_H_*/

