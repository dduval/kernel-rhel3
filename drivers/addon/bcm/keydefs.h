
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
 * keydefs.h: Misc keytype definitions.
 *
 */
/*
 * Revision History:
 *
 * May 2000 SOR/JTT Created.
 */

#ifndef _KEY_DEFS_H
#define _KEY_DEFS_H

/* Maximum key size in bytes */
#define MAX_KEY_BYTE_SIZE 256
/* Convert bit length to byte length */
#define BITSTOBYTES(bitsize) ((bitsize+7)/8)
/* Intermediate key copy location offsets */

/* Offset of Key information within kernel buffer. */
#define DH_Y_OFFSET 	0
#define DH_X_OFFSET 	1
#define DH_K_OFFSET 	2
#define DH_N_OFFSET 	3
#define DH_G_OFFSET 	4
#define DH_USERX_OFFSET	5

#define RSA_OUT_OFFSET 	0
#define RSA_IN_OFFSET 	1
#define RSA_N_OFFSET	3
#define RSA_E_OFFSET	4
#define RSA_P_OFFSET	5
#define RSA_Q_OFFSET	6
#define RSA_EDP_OFFSET	7
#define RSA_EDQ_OFFSET	8
#define RSA_PINV_OFFSET	9

#define DSA_R_OFFSET	1
#define DSA_S_OFFSET	2
#define DSA_Q_OFFSET	3
#define DSA_P_OFFSET	4
#define DSA_G_OFFSET	5
#define DSA_KEY_OFFSET	6
#define DSA_RAND_OFFSET	7
#define DSA_V_OFFSET 	8
#define DSA_IN_OFFSET 	9

#define MAX_NUM_KEY_PARAMS 10

#endif /* KEY_DEFS_H */





