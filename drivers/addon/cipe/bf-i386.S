/*
	Bruce Schneier's Blowfish in i386 assembler (for linux/gcc)
	Author: Olaf Titz <olaf@bigred.inka.de>

	This code is in the public domain.

	$Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $
*/

#ifdef ASM_BF_Crypt

#ifndef __ASSEMBLY__
#define __ASSEMBLY__
#endif

/* This header just defines ENTRY to make an appropriate global symbol */
#include <linux/linkage.h>

/* Look for CONFIG_X86_BSWAP (defined for 486 and up) */
#include <linux/config.h>

#define PosP0	   0
#define PosP17 	  68
#define PosS0	  72
#define PosS1	1096
#define PosS2	2120
#define PosS3	3144
#define KeyLenL 1042
#define KeyLenB  521

/* This code is optimized for speed rather than size - loops unrolled. etc. */

/*
  Endian-ness is taken care of by (a) the order of shifts in the Round
  macro and (b) the order of shifts below under the ukx label.
  The key tables and user data are stored and processed in the CPU
  byte order.
*/

/* Do one round */
#define Round(lw,rw)				\
	movl	rw, %edx;			\
	shrl	$24, %edx;			\
	movl	PosS0(%edi,%edx,4), %eax;	\
	movl	rw, %edx;			\
	shrl	$16, %edx;			\
	andl	$0xFF, %edx;			\
	addl	PosS1(%edi,%edx,4), %eax;	\
	movl	rw, %edx;			\
	shrl	$8, %edx;			\
	andl	$0xFF, %edx;			\
	xorl	PosS2(%edi,%edx,4), %eax;	\
	movl	rw, %edx;			\
	andl	$0xFF, %edx;			\
	addl	PosS3(%edi,%edx,4), %eax;	\
	xorl	%eax, lw;			\
	lodsl;					\
	xorl	%eax, lw

/* Words in %ebx, %ecx - Key in %edi - P-index in %esi - result swapped */
blowfish:
	lodsl
	xorl	%eax, %ebx
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	Round(%ecx,%ebx); Round(%ebx,%ecx)
	lodsl
	xorl	%eax, %ecx
	ret

/* Endianness swap operation. Is there an easier way to decompose
   %ebx into %bl and %bh within a macro? */
#ifdef CONFIG_X86_BSWAP
#define swab32(x,h,l) bswap x
#else
#define swab32(x,h,l) 		\
	xchgb	h, l;		\
	rorl	$16, x;		\
	xchgb	h, l
#endif


/* Function prototype prologue/epilogue. Sets up registers etc.
   This is all code common to the six encrypt/decrypt functions.
   They have the prototype
   extern void FUNCTION(void *dataIn, void *dataOut, const Blowfish_Key key);
   for the following FUNCTIONs:
   _N_Blowfish_Encrypt, _N_Blowfish_Decrypt  (native-endian)
   B_Blowfish_Encrypt,  B_Blowfish_Decrypt   (big-endian)
   L_Blowfish_Encrypt,  L_Blowfish_Decrypt   (little-endian)
   Of course, in the i386 implementation, native==little-endian.
*/

/* saved regs relative to %esp */
#define sebx	0
#define sebp	4
#define sesi	8
#define sedi	12
#define SAVE	16 /* no. of bytes the saved registers occupy */
/* arguments relative to %esp */
#define dataIn	SAVE+4
#define dataOut	SAVE+8
#define key	SAVE+12

#define PROLOG				\
	/* save registers */		\
	subl	$SAVE, %esp;		\
	movl	%ebx, sebx(%esp);	\
	movl	%ebp, sebp(%esp);	\
	movl	%esi, sesi(%esp);	\
	movl	%edi, sedi(%esp);	\
	/* load data */			\
	movl	dataIn(%esp), %esi;	\
	movl	(%esi), %ebx;		\
	movl	4(%esi), %ecx;		\
	/* load key */			\
	movl	key(%esp), %edi;

#define EPILOG				\
	/* store data */		\
	movl	dataOut(%esp), %edi;	\
	movl	%ebx, 4(%edi);		\
	movl	%ecx, (%edi);		\
	/* restore registers */		\
	movl	sedi(%esp), %edi;	\
	movl	sesi(%esp), %esi;	\
	movl	sebp(%esp), %ebp;	\
	movl	sebx(%esp), %ebx;	\
	addl	$SAVE, %esp;		\
	ret

#define FORWARD				\
	movl	%edi, %esi;		\
	cld

#define BACKWARD			\
	leal	PosP17(%edi), %esi;	\
	std

#define SWAP				\
	swab32(%ebx,%bh,%bl);		\
	swab32(%ecx,%ch,%cl)

/* N.B. In Linux 2.3, the D flag is assumed to be zero all the time.
   Thus after BACKWARD an additional cld is necessary. */

#ifndef BF_DONTNEED_LE
ENTRY(L_Blowfish_Encrypt)
#endif
ENTRY(_N_Blowfish_Encrypt)
	PROLOG
	FORWARD
	call	blowfish
	EPILOG

#ifndef BF_DONTNEED_LE
ENTRY(L_Blowfish_Decrypt)
#endif
ENTRY(_N_Blowfish_Decrypt)
	PROLOG
	BACKWARD
	call	blowfish
	cld
	EPILOG

#ifndef BF_DONTNEED_BE
ENTRY(B_Blowfish_Encrypt)
	PROLOG
	SWAP
	FORWARD
	call	blowfish
	SWAP
	EPILOG

ENTRY(B_Blowfish_Decrypt)
	PROLOG
	SWAP
	BACKWARD
	call	blowfish
	cld
	SWAP
	EPILOG
#endif


/* load byte from key, start over if exhausted */
#define lodsbw(base,len)		\
	lodsb;				\
	decl	%ecx;			\
	cmpl	$0, %ecx;		\
	jg	1f;			\
	movl	base, %esi;		\
	movl	len, %ecx;		\
1:

/*
   void Blowfish_ExpandUserKey(const char *userKey, int userKeyLen,
			       Blowfish_Key key);
*/

ENTRY(Blowfish_ExpandUserKey)
	pushl	%ebx
	pushl	%ebp
	pushl	%esi
	pushl	%edi
#define SAVE	16 /* no. of bytes the saved registers occupy */
/* arguments relative to %esp */
#define	userKey		SAVE+4
#define userKeyLen	SAVE+8
#define key		SAVE+12

	/* Copy the init vector into key */
	leal	SYMBOL_NAME(Blowfish_Init_Key), %esi
	movl	key(%esp), %edi
	movl	$KeyLenL, %ecx
	cld
	rep;	movsl
	/* XOR the user key into the P table */
	movl	key(%esp), %edi
	movl	$18, %ebp
	movl	userKey(%esp), %esi
	movl	userKeyLen(%esp), %ecx
ukx:
	/* process one 32-bit word swapped */
	lodsbw(userKey(%esp), userKeyLen(%esp))
	shll	$8, %eax
	lodsbw(userKey(%esp), userKeyLen(%esp))
	shll	$8, %eax
	lodsbw(userKey(%esp), userKeyLen(%esp))
	shll	$8, %eax
	lodsbw(userKey(%esp), userKeyLen(%esp))
	xorl	%eax, (%edi)
	addl	$4, %edi
	decl	%ebp
	cmpl	$0, %ebp
	jg	ukx

	/* Now do the repeated encryption process */
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	movl	$KeyLenB, %ebp
	movl	key(%esp), %edi
ukb:
	pushl	%edi
	movl	key+4(%esp), %edi
	movl	%edi, %esi
	call	blowfish
	popl	%edi
	xchgl	%ebx, %ecx
	movl	%ebx, (%edi)
	movl	%ecx, 4(%edi)
	addl	$8, %edi
	decl	%ebp
	cmpl	$0, %ebp
	jg	ukb

	popl	%edi
	popl	%esi
	popl	%ebp
	popl	%ebx
	ret
#undef	dataIn
#undef	dataOut
#undef	key

#endif /* ASM_BF_Crypt */
