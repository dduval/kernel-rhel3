
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
 * ubsec_norm.c: Normalize (left shift) and UnNormalize (right shift) data 
 *
 *
 * Revision History:
 *
 * May 2000 SOR Created
 * 04/24/01 DPA Allow for unnormalize of D-H random number (x) output for BCM5805
 * 10/09/01 SRM 64 bit port
 */

#include "ubsincl.h"

#ifdef UBSEC_PKEY_SUPPORT
#ifndef UBSEC_HW_NORMALIZE
#define WORD_LENGTH 32


/* 
 * This code assumes that each 32-bit element in the "key" data array is stored
 * in memory in little endian format. This assumption allows one to treat the
 * "key" array as a byte array, with each byte being more significant than the
 * bytes preceeding it and less significant than the bytes that follow it.
 * This "byte array" treatment cannot be applied to arrays of big endian integers.
 */ 

/* The MSB Bit is the most significant bit of a multibyte
   word, in little endian formant. */
#define MSB_BIT_MASK 0x80000000 /* most significant bit */
#define MOVEBITSMASK 0xffffffff /* 32 bits */

/*****************************************************************************
 * ubsec_NormalizeDataTo:
 *
 * Normalize data to the DWORD boundary passed as a parameter.
 * Input: pData,Normalize to len
 *
 * Output: Number of bits to shift
 *
 *****************************************************************************/ 

long ubsec_NormalizeDataTo(ubsec_LongKey_pt pData,int NormalizeLen)
{
  unsigned long ArrayLength;
  unsigned long shift_in_bits;
  UBS_UINT32 *tmpPtr;
  int	  i;
  
  Dbg_Print(DBG_NORM,("\nubsec: pData->KeyLength:%d", pData->KeyLength));
  /* 
   * Set data so there is a bit in the LSb. 
   */
  /* Total length in bits rounded up. */
  ArrayLength = (pData->KeyLength+WORD_LENGTH-1)/WORD_LENGTH;

 /* 
  * Now determine how many bits to shift to align on a DWORD value.
  */
  shift_in_bits = 0;
  tmpPtr = (UBS_UINT32 *)OS_GetVirtualAddress(pData->KeyValue);  
  for (i=(ArrayLength-1) ; (i>0) ; i--, shift_in_bits+=WORD_LENGTH) {
    if (tmpPtr[i]) { /* Bits in this long */ 
      unsigned long y=CPU_TO_CTRL_LONG(tmpPtr[i]); 
      while (!(y & MSB_BIT_MASK)) { /* Find the bit depth */
	shift_in_bits++;
	y=y<<1;
      }
      break;	
    } 
  }

  Dbg_Print(DBG_NORM,("\nubsec: Normalize %08x shift_in_bits-A %d\n",
		      tmpPtr[ArrayLength-1],shift_in_bits));

  NormalizeLen/=32; /* Assume aligned on DWORD Boundary. */
  shift_in_bits+=((NormalizeLen-ArrayLength)*WORD_LENGTH);

  Dbg_Print(DBG_NORM,("\nubsec: Normalize shift_in_bits-B %d\n", shift_in_bits));
  ubsec_ShiftData(pData,shift_in_bits);
  return(shift_in_bits);
}

/*****************************************************************************
 * ubsec_ShiftData():
 * Shift data by the bits indicated as a parameter.
 *
 * Input: pData, ShiftBits
 * Output: pData
 *
 * On input, pData contains a pointer (*KeyValue) to the data to be
 *   unnormalized. It's length(KeyLength) is one of the values 512,768,or 1024
 *   ShiftInBits is the number of bits to be shifted for the pData 
 *
 * On output, pData contains a pointer (*KeyValue) to the data that has been 
 *   unnormalized. It's length (KeyLength) is  the number of bits, excluding
 *   the leading 0's, of the data pointed by KeyValue
 *****************************************************************************/ 

void ubsec_ShiftData(ubsec_LongKey_pt pData, 
                   long    ShiftBits )
{
  unsigned long ArrayLength;
  unsigned long MoveBitsMask;
  unsigned long *tmpPtr,tmpval,tmpval2,tmpval3;
  int i;
  int RightShift;
  int Dest;

  if (!ShiftBits) /* Nothing to do. */
    return;

  if (ShiftBits > 0) 
    RightShift=1;
  else {
    RightShift=0;
    ShiftBits=-ShiftBits;  /* Make it positive */
  }

  Dbg_Print(DBG_NORM,("\nubsec: Normalize shiftbits %d\n", ShiftBits));

  /* Get shift dword length. */
  ArrayLength = (pData->KeyLength+WORD_LENGTH-1)/WORD_LENGTH;
  tmpPtr = (unsigned long *)OS_GetVirtualAddress(pData->KeyValue);
  if (RightShift) {  /* Logical right shift of bits */
    /* Calculate destination location based, on extra
       words to shift */
    i=(ArrayLength-1) ;   /* Point at last location */
    Dest=(i+(ShiftBits/WORD_LENGTH)); /* Point to new dest location. */
    ShiftBits%=WORD_LENGTH; /* Number of bits within a Word to shift.*/
    MoveBitsMask = MOVEBITSMASK << ShiftBits; 
    Dbg_Print(DBG_NORM,("\nubsec: Mod-shiftbits %d Mask %08x\n", ShiftBits,MoveBitsMask));
    tmpval=CTRL_TO_CPU_LONG(tmpPtr[i]);
    if (tmpval & ~(MOVEBITSMASK >> ShiftBits)) {
      i++;
      Dest++;
    }
    for ( ; i>0; i--,Dest-- ) {
      tmpval=CTRL_TO_CPU_LONG(tmpPtr[i]);
      tmpval2=CTRL_TO_CPU_LONG(tmpPtr[i-1]);
      tmpval3=( ((tmpval<< ShiftBits)&MoveBitsMask)|
		((tmpval2 >>(WORD_LENGTH-ShiftBits)) &(~MoveBitsMask)));
      tmpPtr[Dest] = CPU_TO_CTRL_LONG(tmpval3);
    }
      /* last word to shift */
    tmpval=CTRL_TO_CPU_LONG(tmpPtr[0]);
    tmpval2=((tmpval<<ShiftBits) & MoveBitsMask); 
    tmpPtr[Dest]=CPU_TO_CTRL_LONG(tmpval2);

    /* Now clear leading 0s */
    for (i=0; i < Dest; i++)
      tmpPtr[i]=0;
  }
  else {
    /* Calculate destination location based, on extra
       words to shift */
    Dest=0; /* Last location. */
    i=((ShiftBits)/WORD_LENGTH);   /* Point at first location */
    ShiftBits%=WORD_LENGTH; /* Number of bits within a Word to shift.*/
    MoveBitsMask = MOVEBITSMASK >> ShiftBits; 
    Dbg_Print(DBG_NORM,("\nubsec: Mod-shiftbits %d Mask %08x\n", ShiftBits,MoveBitsMask));
    for ( ; Dest<(int)(ArrayLength-1); i++,Dest++ ) {
      tmpval=CTRL_TO_CPU_LONG(tmpPtr[i]);
      tmpval2=CTRL_TO_CPU_LONG(tmpPtr[i+1]);
      tmpval3=( ((tmpval >> ShiftBits)&MoveBitsMask)
	| (tmpval2  << (WORD_LENGTH-ShiftBits) &(~MoveBitsMask)));
      tmpPtr[Dest] = CPU_TO_CTRL_LONG(tmpval3) ;

    }
      /* last word to shift */
    tmpval=CTRL_TO_CPU_LONG(tmpPtr[i]);
    tmpval2=((tmpval>>ShiftBits) & MoveBitsMask);
    tmpPtr[Dest]= CPU_TO_CTRL_LONG(tmpval2); 

    /* Now set trailing 0s */
    for (Dest++; Dest  <= i; Dest++)
      tmpPtr[Dest]=0;
  }

}



#endif /* NORM */

#endif /* Support */




















