/*
   CIPE - encrypted IP over UDP tunneling

   encaps.c - do encryption

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#include "cipe.h"
#include <asm/string.h>
#include <linux/socket.h>

static inline void xorbuf(part *dst, part *src)
{
    int i;
    for (i=0; i<blockSize/sizeof(part); ++i)
	*dst++^=*src++;
}

#ifdef Crypto_IDEA
/* This seems to be the only way to typecast an array.
   Important: GCC is able to swallow the overhead when optimizing. */
static inline void ecb_enc(part *src, part *dst, Idea_Key k)
{
    Idea_Data *s=(Idea_Data *)src;
    Idea_Data *d=(Idea_Data *)dst;
    Idea_Crypt(*s, *d, k);
}
#define ecb_dec(s,d,k) ecb_enc(s,d,k)
#endif

#ifdef Crypto_Blowfish
#define ecb_enc(s,d,k) L_Blowfish_Encrypt(s,d,k)
#define ecb_dec(s,d,k) L_Blowfish_Decrypt(s,d,k)
#endif

#ifdef VER_BACK

/* Encrypt/decrypt message in CBC mode backwards.
   Key must be set up accordingly */
static void cbc_b(unsigned char *msg, int len, Idea_Key *key, int dec)
{
    int i=len/blockSize;
    unsigned char iv[blockSize], iw[blockSize];
    part *p=(part*)(msg+len-blockSize);
    part *q=(part*)iv;
    part *r=(part*)iw;
    part *s;

    memset(iv, 0, blockSize);
    if (dec) {
	while (i-->0) {
	    memcpy(r, p, blockSize);
	    ecb_dec(p, p, key);
	    xorbuf(p, q);
	    s=q; q=r; r=s;
	    p-=blockSize/sizeof(part);
	}
    } else {
	while (i-->0) {
	    xorbuf(p, q);
	    ecb_enc(p, p, key);
	    q=p;
	    p-=blockSize/sizeof(part);
	}
    }
}

#else

/*
  CBC encryption/decryption routines.
  Note: the block to encrypt includes the IV, while decryption swallows
  the IV. Length is always including IV.
*/

#define partinc(p) ((p)+blockSize/sizeof(part))

static void cbc_enc(unsigned char *msg, int len, Key * const key)
{
    part *p=(part *)msg;
    int i=len/blockSize;

#if 0
    dprintk(DEB_CRYPT, (KERN_DEBUG "cbc_enc: %08x %08x ",
			*((__u32*)partinc(msg)), *(__u32*)key));
#endif
    while (--i>0) {
	xorbuf(partinc(p), p);
	p=partinc(p);
	ecb_enc(p, p, *key);
    }
#if 0
    dprintk(DEB_CRYPT, ("%08x\n", *((__u32*)partinc(msg))));
#endif
}

static void cbc_dec(unsigned char *msg, int len, Key * const key)
{
    part *p=(part *)msg;
    int i=len/blockSize;
    part r[blockSize/sizeof(part)];

#if 0
    dprintk(DEB_CRYPT, (KERN_DEBUG "cbc_dec: %08x %08x ",
			*(__u32*)msg, *(__u32*)key));
#endif
    while (--i>0) {
	ecb_dec(partinc(p), r, *key);
	xorbuf(p, r);
	p=partinc(p);
    }
#if 0
    dprintk(DEB_CRYPT, ("%08x\n", *(__u32*)msg));
#endif
}

#endif

#ifndef VER_SHORT
/* Fill a block of length blockSize with strong random numbers.
   Used for generating IVs. */
void cipe_cryptpad(unsigned char *buf)
{
    static int padcnt=MAXBLKS;
    static Key padkey;

    if (++padcnt>MAXBLKS) {
	/* make a new random key */
	UserKey k;
	dprintk(DEB_CRYPT, (KERN_DEBUG "%s: re-keying cryptpad\n", DEVNAME));
	cipe_prnpad((unsigned char*)k, sizeof(UserKey));
	ExpandUserKey(k, padkey);
	padcnt=0;
    }
    *(int *)(buf)=padcnt;
    cipe_prnpad(buf+sizeof(int), blockSize-sizeof(int));
    ecb_enc((part*)buf, (part*)buf, padkey);
}
#endif


void cipe_checkskey(struct cipe *c)
{
    if ((++c->cntskey>MAXBLKS) || (jiffies>c->timeskey)) {
	/* make the control process send an NK_IND */
	cipe_fakenkey(c, NK_REQ);
	c->timeskey=jiffies+c->tmo_keyxchg;
	if (c->cntskey>MAXBLKS)
	    c->cntskey-=1000;
    }
}

void cipe_checkrkey(struct cipe *c)
{
    if ((c->flags&CIPF_HAVE_RKEY) &&
	((++c->cntrkey>MAXBLKS*2) || (jiffies>c->timerkey))) {
	/* make the control process send an NK_REQ */
	cipe_fakenkey(c, NK_RREQ);
	c->flags&=~CIPF_HAVE_RKEY;
	c->timerkey=jiffies+c->tmo_keyxchg;
	if (c->cntrkey>MAXBLKS*2)
	    c->cntrkey-=1000;
    }
}

void cipe_nodynkey(struct cipe *c)
{
    if (jiffies>c->timerkey) {
	/* make the control process send an NK_REQ */
	cipe_fakenkey(c, NK_RREQ);
	c->timerkey=jiffies+c->tmo_keyxchg;
    }
    dprintk(DEB_CRYPT, (KERN_DEBUG "%s: missing dynamic key\n",
	     c->dev->name));
}

#if ProtocolVersion == 3

/* Encryption/decryption version 3 */

void cipe_encrypt(struct cipe *c, unsigned char *buf, int *len, int typ)
{
    unsigned char p=7-(((*len)+4)&7);
    /* merge key flag in IV */
    *buf&=0x7F;
    if (c->flags&CIPF_HAVE_SKEY)
	*buf|=0x80;
    /* pad */
    cipe_prnpad(buf+(*len), p);
    (*len)+=p+5;
    /* set type and crc */
    *(buf+(*len)-5)=typ|(p<<4);
    *((unsigned long *)(buf+(*len)-4))=
	htonl(crc32(buf+blockSize, (*len)-blockSize-4));

    dprintk(DEB_CRYPT, (KERN_DEBUG "%s: encrypt typ %d pad %d len %d\n",
                        c->dev->name, typ, p, *len));
    cbc_enc(buf, *len, c->flags&CIPF_HAVE_SKEY ? &c->skey_e : &c->key_e);
    cipe_checkskey(c);
}

unsigned short cipe_decrypt(struct cipe *c, unsigned char *buf, int *len)
{
    unsigned char p;

    if (((*buf)&0x80) && !(c->flags&CIPF_HAVE_RKEY)) {
	cipe_nodynkey(c);
	return TW_ERROR; /* can't decrypt - no valid key */
    }
    cbc_dec(buf, *len, ((*buf)&0x80) ? &c->rkey_d : &c->key_d);
    (*len)-=blockSize;
    if (((*len) - 4) <= 0) {
	    printk(KERN_ERR, "CIPE BUG, len==%d, orig_len==%d, buf==%p\n",
		   *len, *len + blockSize, buf);
	    return TW_ERROR;
    }
    if (*((unsigned long *)(buf+(*len)-4)) != htonl(crc32(buf, (*len)-4))) {
	dprintk(DEB_CRYPT, (KERN_DEBUG "%s: decrypt CRC error\n",
			    c->dev->name));
	return TW_ERROR;
    }
    p=*(buf+(*len)-5);
    (*len)-=(p>>4)&7;
    cipe_checkrkey(c);
#define CTLBITS 0x06
    dprintk(DEB_CRYPT, (KERN_DEBUG "%s: decrypt len=%d pad=%d typ=%02X\n",
                        c->dev->name, (*len), (p>>4)&7, p&CTLBITS));
    return (p&CTLBITS);
}

#else

/* Encryption/decryption version 1 and 2 */

void cipe_encrypt(struct cipe *c, unsigned char *buf, int *len, int typ)
{
    unsigned short x;
    unsigned char p;

    p=8-((*len)&7);
    cipe_prnpad(buf+(*len), p);
    (*len)+=p;

#ifdef VER_SHORT
    x=((block_crc(buf, *len)&0xFFFE)^((p&7)<<8)^typ)|(c->haveskey);
#else
    x=((block_crc(buf+blockSize, (*len)-blockSize)&0xFFFE)
       ^((p&7)<<8)^typ)|(c->haveskey);
#endif
#ifdef VER_BACK
    cbc_b(buf, *len, c->haveskey ? &c->skey_e : &c->key_e, 0);
#else
    cbc_enc(buf, *len, c->haveskey ? &c->skey_e : &c->key_e);
#endif

    dprintk(DEB_CRYPT, (KERN_DEBUG "%s: encrypt pad %d\n", c->dev->name, p));
    buf[(*len)++]=x>>8;
    buf[(*len)++]=x&255;
    cipe_checkskey(c);
}

unsigned short cipe_decrypt(struct cipe *c, unsigned char *buf, int *len)
{
    unsigned short x=(buf[(*len)-1])+(buf[(*len)-2]<<8);
    unsigned char p;

    if ((x&1) && !(c->haverkey)) {
	cipe_nodynkey(c);
	return TW_ERROR; /* can't decrypt - no valid key */
    }
    (*len)-=2;
    if (*len<9
#ifndef VER_SHORT
	+blockSize
#endif
	)
	return TW_ERROR; /* short packet */

#ifdef VER_BACK
    cbc_b(buf, *len, (x&1) ? &c->rkey_d : &c->key_d, 1);
#else
    cbc_dec(buf, *len, (x&1) ? &c->rkey_d : &c->key_d);
#endif
#ifndef VER_SHORT
    (*len)-=blockSize;
#endif

    x^=block_crc(buf, *len);
    p=(x>>8)&7; if (!p) p=8;
    (*len)-=p;
    cipe_checkrkey(c);

#define CTLBITS 0xF8FE
    dprintk(DEB_CRYPT, (KERN_DEBUG "%s: decrypt pad %d typ %04X\n",
			c->dev->name, (x>>8)&7, x&CTLBITS));
    return (x&CTLBITS); /* delete the control bits */
}

#endif
