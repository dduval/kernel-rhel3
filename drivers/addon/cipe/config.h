/* config.h.  Generated automatically by configure.  */
/*
   CIPE - encrypted IP over UDP tunneling

   Copyright 1999 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

/* Config-dependent defines. Always include this first. */
/* @api means the next line is used for determining the version magic */

/* Version of the CIPE package @api */
#define VERSION "1.4.5"

/* Encapsulation protocol version @api */
#define ProtocolVersion 3

/* Cipher algorithm selection @api */
/* #undef Crypto_IDEA */
/* Cipher algorithm selection @api */
#define Crypto_Blowfish 1

/* Assembler module selection */
/* #undef ASM_Idea_Crypt */
#ifdef __i386
#define ASM_BF_Crypt 1
#endif

/* Debug code in kernel */
#define DEBUG 1

/* Use old key parser */
/* #undef BUG_COMPATIBLE */

/* Dynamic device allocation @api */
/* #undef NO_DYNDEV */

/* Syslog facility */
#define LOGFAC LOG_DAEMON

/* Memory management functions (kernel version dependent?) */
#define HAVE_MLOCK 1
#define HAVE_MLOCKALL 1

/* End of autoconf options */

/* This tells the Blowfish module to omit unneeded code */
#define BF_DONTNEED_BE

