/*
   CIPE - encrypted IP over UDP tunneling

   crypto.h - configuration of the crypto algorithm

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#ifndef _CRYPTO_H_
#define _CRYPTO_H_

typedef unsigned long part;
/* the longest integer so that sizeof(part) divides blockSize.
   Used only for optimizing block-copy and block-XOR operations. */

#if     ProtocolVersion == 1

#ifdef  OLDNAMES
#define VERNAME "1"
#else
#define VERNAME "a"
#endif
#define VER_BACK                /* encryption progress backwards */
#define VER_SHORT               /* no IV in packet */

#elif   ProtocolVersion == 2

#ifdef  OLDNAMES
#define VERNAME "2"
#else
#define VERNAME "b"
#endif

#elif   ProtocolVersion == 3

#ifdef  OLDNAMES
#define VERNAME "3"
#else
#define VERNAME "c"
#endif
#define VER_CRC32               /* checksums are 32bit */

#else
#error  "Must specify correct ProtocolVersion"
#endif


#ifdef  Crypto_IDEA
#define CRYPTO                  "IDEA"
#define CRNAME			"i"
#define CRNAMEC			'i'
#include "idea0.h"
#define Key                     Idea_Key
#define keySize                 Idea_keySize
#define UserKey                 Idea_UserKey
#define userKeySize             Idea_userKeySize
#define ExpandUserKey           Idea_ExpandUserKey
#define InvertKey               Idea_InvertKey
#define blockSize               Idea_dataSize

#else
#ifdef  Crypto_Blowfish
#define CRYPTO                  "Blowfish"
#define CRNAME			"b"
#define CRNAMEC			'b'
#include "bf.h"
#define Key                     Blowfish_Key
#define keySize                 sizeof(Blowfish_Key)
#define UserKey                 Blowfish_UserKey
#define userKeySize             16 /* arbitrary, but matches IDEA */
#define ExpandUserKey(u,k)      Blowfish_ExpandUserKey(u,userKeySize,k)
#define InvertKey(x,y)          /* noop */
#define blockSize               sizeof(Blowfish_Data)

#else
#error  "Must specify Crypto_IDEA or Crypto_Blowfish"
#endif
#endif

#endif
