
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
 * ubssha1.c: SHA1 key manipulation functions.
 */

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created from little endian c-model
 * 12/02/1999 DWP modified to handle Big Endian architectures
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 10/09/2001 SRM 64 bit port
 */

#include "ubsincl.h"
                                         
static void SHAUpdate(SHA_CTX *ctx, unsigned char *HashBlock, int len);

static  void bytnxor( unsigned char *dest, unsigned char *src1, unsigned char *src2, unsigned char len)
{
  while (len-- > 0) *dest++ = *src1++ ^ *src2++; 
}

/*
 * This code will only work on a little endian machine. Constants and boolean functions
 * are out of Schneier's "Applied Cryptography". Also, SHAUpdate() must only be called
 * once, followed by a call to SHAFinal() -- the length field is not being properly
 * carried over within SHAUpdate().
 *
 * This implementation does not handle large block sizes per the spec.
 * It is meant for packets of less than 2**28 bytes each.
*/

/* Boolean functions for internal rounds */
#define F(x,y,z) ( ((x) & (y)) | ((~(x)) & (z)) )
#define G(x,y,z) ( (x) ^ (y) ^ (z) )
#define H(x,y,z) ( ((x)&(y)) | ((x)&(z)) | ((y)&(z)) )

/* The 4 magic constants */
static UBS_UINT32 SHA1_K[] = {
  0x5a827999, 0x6ed9eba1, 
  0x8f1bbcdc, 0xca62c1d6
  };

static unsigned char ipad[64] = {0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,
        0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36};
                                         
static unsigned char opad[64] = {0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,
        0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c,0x5c};

void 
InitSHA1State(ubsec_HMAC_State_pt HMAC_State,unsigned char *HashKey)
{
  SHA_CTX ctx;
  unsigned char pad[64];

  /* First prepare the inner block. */
  bytnxor((unsigned char *)pad,(unsigned char *)HashKey,ipad, 64);

	/* Init the context, the initial values in memory are 01 23 45 ... */
  RTL_MemZero(&ctx,sizeof(SHA_CTX));
  ctx.buffer[0] = 0x67452301;
  ctx.buffer[1] = 0xefcdab89;
  ctx.buffer[2] = 0x98badcfe;
  ctx.buffer[3] = 0x10325476;
  ctx.buffer[4] = 0xc3d2e1f0;
  SHAUpdate(&ctx,pad,64);

	/* ctx comes out as an array of long ints. The byte order of ctx
is dependent on the CPU endianess. The byte order of the memory destination 
is dependent on the CryptoNet memory endianess. Based on our SHA1 algorithm's
CPU endianess assumptions, the net result is that we do a straight copy if
the CPU and CryptoNet memory are of the same endianess. If the CPU and 
CryptoNet memory are of opposite endianess, we'll do 32-bit byteswapping
during the copy, taken care of by the copywords() routine. */

#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE)
  copywords((UBS_UINT32 *)&HMAC_State->InnerState[0],&ctx.buffer[0], SHA_HASH_LENGTH/4); 
#else
  RTL_Memcpy(&HMAC_State->InnerState[0],&ctx.buffer[0], SHA_HASH_LENGTH); 
#endif /* UBS_CPU_ATTRIBUTE */

  /* Do do the same for the outer block */
  bytnxor((unsigned char *)pad,(unsigned char *)HashKey, opad, 64);
  RTL_MemZero(&ctx,sizeof(SHA_CTX));
  ctx.buffer[0] = 0x67452301;
  ctx.buffer[1] = 0xefcdab89;
  ctx.buffer[2] = 0x98badcfe;
  ctx.buffer[3] = 0x10325476;
  ctx.buffer[4] = 0xc3d2e1f0;

  SHAUpdate(&ctx, pad,64);

	/* ctx comes out as an array of long ints. The byte order of ctx
is dependent on the CPU endianess. The byte order of the memory destination 
is dependent on the CryptoNet memory endianess. Based on our SHA1 algorithm's
CPU endianess assumptions, the net result is that we do a straight copy if
the CPU and CryptoNet memory are of the same endianess. If the CPU and 
CryptoNet memory are of opposite endianess, we'll do 32-bit byteswapping
during the copy, taken care of by the copywords() routine. */

#if (UBS_CPU_ATTRIBUTE != UBS_CRYPTONET_ATTRIBUTE)
  copywords((UBS_UINT32 *)&HMAC_State->OuterState[0],&ctx.buffer[0], SHA_HASH_LENGTH/4); 
#else
  RTL_Memcpy(&HMAC_State->OuterState[0],&ctx.buffer[0], SHA_HASH_LENGTH); 
#endif /* UBS_CPU_ATTRIBUTE */

}


static void
SHAUpdate(SHA_CTX *ctx, unsigned char *HashBlock, int len)
{
  UBS_UINT32 a, b, c, d, e;
  int block, i;

	/* Process as many blocks as possible */
  for (block = 0; len >= 64; len -= 64, ++block) {
    UBS_UINT32 m[16];

    /* A 16-byte buffer is sufficient -- we build needed words beyond the first 16 on the fly */
    RTL_Memcpy(m, HashBlock + 64*block, 64); /* Get one block of data */

    /* At this point m[] is built as a char array. However, m[] will
       be operated on from here on out as an array of unsigned longs.
       This algorithm assumes m[] is arranged in big-endian byte order, so 
       we'll endian-adjust the byte array if we have a little endian CPU */

#if (UBS_CPU_ATTRIBUTE == UBS_LITTLE_ENDIAN)
    for (i = 0; i < 16; ++i) 
        m[i] = BYTESWAPLONG(m[i]); /* View data as big endian */
#endif

    a = ctx->buffer[0];
    b = ctx->buffer[1];
    c = ctx->buffer[2];
    d = ctx->buffer[3];
    e = ctx->buffer[4];

    Dbg_Print(DBG_SHA1,("pre round 0: 0x%x 0x%x 0x%x 0x%x 0x%x\n", a, b, c, d, e));

		/* Four sets of 20 rounds each */
    for (i = 0; i < 80; ++i) {
      unsigned long fn = 0, temp, K = 0;
      switch (i / 20) {
      case 0: K = SHA1_K[0]; fn = F(b,c,d); break;
      case 1: K = SHA1_K[1]; fn = G(b,c,d); break;
      case 2: K = SHA1_K[2]; fn = H(b,c,d); break;
      case 3: K = SHA1_K[3]; fn = G(b,c,d); break;
      default:;
      }

			/* Build needed words beyond original 16 on the fly */
      if (i >= 16) m[i % 16] = rol(m[(i-3) % 16] ^ m[(i-8) % 16] ^ m[(i-14) % 16] ^ m[(i-16) % 16], 1);
      temp = rol(a, 5) + fn + e + m[i%16] + K;
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;

     Dbg_Print(DBG_SHA1,("post round %d: 0x%x 0x%x 0x%x 0x%x 0x%x\n", i, a, b, c, d, e));
    }

    ctx->buffer[0] += a;
    ctx->buffer[1] += b;
    ctx->buffer[2] += c;
    ctx->buffer[3] += d;
    ctx->buffer[4] += e;
  }

}













