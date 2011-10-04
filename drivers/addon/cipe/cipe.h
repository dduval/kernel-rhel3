/*
   CIPE - encrypted IP over UDP tunneling

   cipe.h - contains definitions, includes etc. common to all modules

   Copyright 1996-2000 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#ifndef _CIPE_H_
#define _CIPE_H_

#include "config.h"
#include "crypto.h"
#ifdef __KERNEL__
#include <linux/if.h>
#else
#include <net/if.h>
#endif
#include <linux/if_cipe.h>

/*** Key exchange related definitions ***/

/* Minimum kxc block. */
#define KEYXCHGBLKMIN   64
/* Maximum kxc block, padded with random bytes */
#define KEYXCHGBLKMAX   (KEYXCHGBLKMIN+256)
/* Position of the timestamp */
#define KEYXCHGTSPOS    56
/* Type words. Only 4 are possible. */
#define TW_DATA         0
#define TW_NEWKEY       2
#define TW_CTRL         4
#define TW_RSVD2        6
/* error indication, no valid type word */
#define TW_ERROR        1

/* NEWKEY (key exchange mode 1) subtypes. */
#define NK_RREQ         0 /* not used in protocol */
#define NK_REQ          1 /* send me your new key */
#define NK_IND          2 /* this is my new key   */
#define NK_ACK          3 /* i have your new key  */

/* CTRL subtypes. By now sent in a TW_NEWKEY packet. */
#define CT_DUMMY     0x70 /* ignore */
#define CT_DEBUG     0x71 /* log */
#define CT_PING      0x72 /* send PONG */
#define CT_PONG      0x73
#define CT_KILL      0x74 /* exit */

/*** Kernel-module internal stuff ***/

#ifdef __KERNEL__

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/if_ether.h>
#include <linux/net.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/sock.h>
#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,0)
#define LINUX_21
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
#define LINUX_23
#endif

#ifdef LINUX_21
#ifndef SPIN_LOCK_UNLOCKED /* 2.2/2.4 incompat */
#include <asm/spinlock.h>
#endif
#endif

#ifdef LINUX_23
#define tasklist_LOCK()		read_lock(&tasklist_lock)
#define tasklist_UNLOCK()	read_unlock(&tasklist_lock)
#else
#define tasklist_LOCK()		/* nop */
#define tasklist_UNLOCK()	/* nop */
#endif

#ifdef LINUX_23
#define rtnl_LOCK()	rtnl_lock()
#define rtnl_UNLOCK()	rtnl_unlock()
#else
#define rtnl_LOCK()	/* nop */
#define rtnl_UNLOCK()	/* nop */
#endif

#ifdef LINUX_23
#define NET_DEVICE net_device
#define DEV_STATS  net_device_stats
#else
#define NET_DEVICE device
#define DEV_STATS  enet_statistics
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,127)
#define timeout_t unsigned long
#else
#define timeout_t long
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,99)
#define HAVE_DEVNAME_ARRAY
#endif

/* The header we add to each packet */
#ifdef VER_SHORT
#define cipehdrlen (sizeof(struct iphdr)+sizeof(struct udphdr))
#else
#define cipehdrlen (sizeof(struct iphdr)+sizeof(struct udphdr)+blockSize)
#endif
/* ...plus a real hardware header (common case) */
#define cipexhdrl  (cipehdrlen+((ETH_HLEN+15)&~15))
/* max. padding at the end */
#if ProtocolVersion == 3
#define cipefootlen 12 /* 7 bytes pad, 1 byte type, 4 bytes CRC */
#else
#define cipefootlen 10 /* 8 bytes pad, 2 bytes CRC */
#endif

/* A CIPE device's parameter block */

#define CIPE_MAGIC  (htonl(0x43495045))
struct cipe {
    __u32               magic;
    struct NET_DEVICE   *dev;
     /* Set by user process */
    __u32               peeraddr;
    __u32               myaddr;
    __u16               peerport;
    __u16               myport;
    __u32               sockshost;
    __u16               socksport;
    short	        cttl;
#ifdef Crypto_IDEA
    Key                 key_e, key_d, skey_e, rkey_d;
#endif
#ifdef Crypto_Blowfish
    Key                 key, skey, rkey;
    #define key_e       key
    #define key_d       key
    #define skey_e      skey
    #define rkey_d      rkey
#endif
    unsigned long       tmo_keyxchg;
    unsigned long       tmo_keylife;
     /* Internal */
    unsigned long       timekx;
    unsigned long       timeskey;
    unsigned long       timerkey;
    int                 cntskey;
    int                 cntrkey;
    struct sock         *sock;
    int                 flags;
#ifdef LINUX_21
    char                recursion;
#endif
    pid_t               owner;
    /* Statistics */
#ifdef LINUX_21
    struct net_device_stats stat;
#else
    struct enet_statistics stat;
#endif
    /* Socket interface stuff */
    struct proto        *udp_prot;
    struct proto        cipe_proto;
};

/* Flag values (internally used) */
#define CIPF_HAVE_KEY           0x0001
#define CIPF_HAVE_SKEY          0x0002
#define CIPF_HAVE_RKEY          0x0004
#define CIPF_MASK_INT           0x00FF
#define CIPF_MASK_EXT           0xFF00

#define MAXBLKS         32767  /* max # blocks to encrypt using one key */

/* Define, init and check a struct cipe * variable. */
#define DEVTOCIPE(dev,c,err) \
    struct cipe *c = (struct cipe*)(dev->priv); \
    if (!c || c->magic!=CIPE_MAGIC) return err;

/* Master control struct */
struct cipe_ctrl {
#ifndef HAVE_DEVNAME_ARRAY
    char                name[IFNAMSIZ];
#endif
    struct cipe         cipe;
    struct NET_DEVICE 	dev;
};

extern struct cipe_ctrl **cipe_ctrls;
extern int cipe_maxdev;

/* SOCKS5 encapsulation header */
struct sockshdr {
    char                rsv[2];
    char                frag;
    char                atyp;
    __u32               dstaddr __attribute__((packed));
    __u16               dstport __attribute__((packed));
};

#ifdef DEBUG
extern int cipe_debug;

#if 0
/* Lock around our printks, to avoid mixing up dumps. NOT for regular use. */
extern spinlock_t cipe_printk_lock;
#define LOCK_PRINTK unsigned long flags; spin_lock_irqsave(&cipe_printk_lock, flags)
#define UNLOCK_PRINTK spin_unlock_irqrestore(&cipe_printk_lock, flags)
#else
#define LOCK_PRINTK	/* nop */
#define UNLOCK_PRINTK	/* nop */
#endif

#define DEB_CALL        1
#define DEB_INP         2
#define DEB_OUT         4
#define DEB_CRYPT       8
#define DEB_KXC         16
#define DEB_PKIN        32
#define DEB_PKOU        64
#define DEB_CHKP	128

#define dprintk(l,p)	if(cipe_debug&l){LOCK_PRINTK; printk p; UNLOCK_PRINTK;}

#else
#define dprintk(l,p)	/* nop */

#endif /* DEBUG */

#if defined(DEBUG) && defined(LINUX_23)
#define __CHECKPOINT(F,L) printk(KERN_DEBUG "CHECKPOINT " F ":%d\n", L)
#define CHECKPOINT if (cipe_debug&DEB_CHKP){\
    LOCK_PRINTK; __CHECKPOINT(__FILE__,__LINE__); UNLOCK_PRINTK;\
    current->state=TASK_INTERRUPTIBLE; schedule_timeout(HZ/20); }
#else
#define CHECKPOINT	/* nop */
#endif

/* internal routines */
/* module.c */
extern void cipe_use_module(void);
extern void cipe_unuse_module(void);
extern int cipe_check_kernel(void);
/* device.c */
extern void cipe_prnpad(unsigned char *buf, int len);
extern void cipe_close(struct cipe *c);
extern const char *cipe_ntoa(__u32 addr);
/* sock.c */
extern int cipe_attach(struct NET_DEVICE *dev, struct siocsifcipatt *parm);
extern void cipe_fakenkey(struct cipe *c, char typ);
/* output.c */
#ifdef DEBUG
extern void cipe_dump_packet(char *title, struct sk_buff *skb, int dumpskb);
#endif
extern int cipe_xmit(struct sk_buff *skb, struct NET_DEVICE *dev);
/* encaps.c */
extern void cipe_encrypt(struct cipe *c, unsigned char *buf,
			 int *len, int typcode);
extern unsigned short cipe_decrypt(struct cipe *c, unsigned char *buf,
				   int *len);
#ifndef VER_SHORT
extern void cipe_cryptpad(unsigned char *buf);
#endif

#endif /* __KERNEL__ */

#ifdef VER_CRC32
/* crc32.c */
extern unsigned long crc32(const unsigned char *s, unsigned int len);
#else
/* crc.c */
extern unsigned short block_crc(unsigned char *d, int len);
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))

#ifndef DEVNAME
#define DEVNAME "cip" VERNAME CRNAME
#endif

#endif /* _CIPE_H_ */
