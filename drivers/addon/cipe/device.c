/*
   CIPE - encrypted IP over UDP tunneling

   device.c - the net device driver

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#include "cipe.h"
#include "version.h"
#include <stddef.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/config.h>

#ifdef LINUX_21
#include <asm/uaccess.h>
#include <linux/rtnetlink.h>
#else
#define register_netdevice register_netdev
#define unregister_netdevice unregister_netdev
#endif

/*** Globals ***/

static const char driver_version[]=VERSION;

struct cipe_ctrl **cipe_ctrls = NULL;
#ifdef NO_DYNDEV
int cipe_maxdev = 4;            /* changeable via insmod */
#else
int cipe_maxdev = 100;          /* changeable via insmod */
#endif
#ifdef DEBUG
int cipe_debug = DEB_CALL;      /* changeable via insmod */
#endif

/* clear all potentially sensitive info and stats */
static void cipe_zero_c(struct cipe *c)
{
    memset(&(c->peeraddr), 0,
           offsetof(struct cipe, udp_prot)-offsetof(struct cipe, peeraddr));
    /* reset these to sensible values */
    c->tmo_keyxchg = 10*HZ;
    c->tmo_keylife = 10*60*HZ;
}

/* weak but fast PRNG, used for padding only */
static __u32 prnseed;
void cipe_prnpad(unsigned char *buf, int len)
{
    while (len>0) {
	prnseed=prnseed*0x01001001+1;
	if (len>=2) {
	    *(__u16 *)buf=prnseed>>16;
	    len-=2; buf+=2;
	} else {
	    *buf=(prnseed>>24)^jiffies; return;
	}
    }
}

#ifdef DO_LOCK_PRINTK
spinlock_t cipe_printk_lock = SPIN_LOCK_UNLOCKED;
#endif

/* inet_ntoa() for multiple use. */
#ifdef CONFIG_SMP
#define NTOABUFS	16
#else
#define NTOABUFS	4
#endif
static char ntoabuf[NTOABUFS][16];
static int ntoaptr=0;
#ifdef LINUX_21
spinlock_t cipe_ntoa_lock=SPIN_LOCK_UNLOCKED;
#endif

const char *cipe_ntoa(const __u32 addr)
{
    const unsigned char *x=(const unsigned char *)&addr;
    char *p;
    int b, i;
#ifdef LINUX_21
    unsigned long flags;
    spin_lock_irqsave(&cipe_ntoa_lock, flags);
#endif
    b=ntoaptr;
    if (++b>=NTOABUFS)
	b=0;
    ntoaptr=b;
#ifdef LINUX_21
    spin_unlock_irqrestore(&cipe_ntoa_lock, flags);
#endif

    p=ntoabuf[b];
    for (i=0; i<4; ++i) {
        int k=x[i]/100;
        int l=(x[i]/10)%10;
        if (k)
            *p++=k+'0';
        if (k || l)
            *p++=l+'0';
        *p++=(x[i]%10)+'0';
        if (i<3)
            *p++='.';
    }
    *p='\0';
    return ntoabuf[b];
}

/*** IOCTL handlers ***/

#ifdef SIOCGIFCIPPAR
static int cipe_getpar(struct NET_DEVICE *dev, struct siocgifcippar *parm)
{
    DEVTOCIPE(dev,c,-ENODEV);

    parm->sockshost=c->sockshost;
    parm->socksport=c->socksport;
    parm->tmo_keyxchg=c->tmo_keyxchg/HZ;
    parm->tmo_keylife=c->tmo_keylife/HZ;
    parm->flags=c->flags;
    parm->cttl=c->cttl;
    return 0;
}
#endif

static int cipe_setpar(struct NET_DEVICE *dev, struct siocsifcippar *parm)
{
    DEVTOCIPE(dev,c,-ENODEV);

    if (parm->sockshost)
	c->sockshost=parm->sockshost;
    if (parm->socksport)
	c->socksport=parm->socksport;
    if (parm->tmo_keyxchg>10*60*HZ)
	return -EINVAL;
    if (parm->tmo_keyxchg)
	c->tmo_keyxchg=parm->tmo_keyxchg*HZ;
    if (parm->tmo_keylife>24*60*60*HZ)
	return -EINVAL;
    if (parm->tmo_keylife)
	c->tmo_keylife=parm->tmo_keylife*HZ;
    c->flags=(parm->flags&CIPF_MASK_EXT)|(c->flags&CIPF_MASK_INT);
    c->cttl=parm->cttl;
    dprintk(DEB_CALL, (KERN_DEBUG "%s: setpar %s:%d %ld %ld %04x %d\n",
                       dev->name,
                       cipe_ntoa(c->sockshost), ntohs(c->socksport),
                       c->tmo_keyxchg, c->tmo_keylife,
                       c->flags, c->cttl));
    return 0;
}

static int cipe_setkey(struct NET_DEVICE *dev, struct siocsifcipkey *parm)
{
    DEVTOCIPE(dev,c,-ENODEV);

    dprintk(DEB_KXC, (KERN_DEBUG "%s: setkey %d\n", dev->name, parm->which));
    switch (parm->which) {
    case KEY_STATIC:
	ExpandUserKey(parm->thekey, c->key_e);
#if 0
	dprintk(DEB_CRYPT, (KERN_DEBUG "ExpandUserKey: %08x\n",
			    *(__u32*)(c->key_e)));
#endif
	InvertKey(c->key_e, c->key_d);
	c->flags|=CIPF_HAVE_KEY;
	break;
    case KEY_SEND:
	ExpandUserKey(parm->thekey, c->skey_e);
	c->timeskey=jiffies+c->tmo_keylife;
	c->cntskey=0;
	c->flags|=CIPF_HAVE_SKEY;
	break;
    case KEY_RECV:
	ExpandUserKey(parm->thekey, c->rkey_d);
	InvertKey(c->rkey_d, c->rkey_d);
	c->timerkey=jiffies+2*c->tmo_keylife; /* allow for fuzz */
	c->cntrkey=0;
	c->flags|=CIPF_HAVE_RKEY;
	break;
    case KEY_STATIC+KEY_INVAL:
        c->flags&=~(CIPF_HAVE_KEY|CIPF_HAVE_SKEY|CIPF_HAVE_RKEY);
	memset(&(c->key_e), 0, sizeof(c->key_e));
	memset(&(c->key_d), 0, sizeof(c->key_d));
	break;
    case KEY_SEND+KEY_INVAL:
        c->flags&=~CIPF_HAVE_SKEY;
	memset(&(c->skey_e), 0, sizeof(c->skey_e));
	c->timeskey=jiffies+c->tmo_keyxchg;
	break;
    case KEY_RECV+KEY_INVAL:
        c->flags&=~CIPF_HAVE_RKEY;
	memset(&(c->rkey_d), 0, sizeof(c->rkey_d));
	c->timerkey=jiffies+c->tmo_keyxchg;
	break;
    default:
	return -EINVAL;
    }
    return 0;
}

static int cipe_alloc_dev(int n);
static void cipe_unalloc_dev(int n);

static int cipe_isowned(struct cipe *c)
{
    struct task_struct *p;
    pid_t pid=c->owner;
    if (!pid) return 0;
    tasklist_LOCK();
    p=current;
    do {
	if (p->pid==pid) {
	    tasklist_UNLOCK();
	    return 1;
	}
	p=next_task(p);
    } while (p!=current);
    tasklist_UNLOCK();
    return 0;
}

#define cipe_hasowner(n) cipe_isowned(&cipe_ctrls[(n)]->cipe)

#ifdef LINUX_21
/* In 2.1 the ioctl operations are run under lock. Beware of deadlocks. */
#define cipe_alloc_LOCK()       0 /* nop */
#define cipe_alloc_UNLOCK()	  /* nop */
#else
static struct semaphore cipe_alloc_sem=MUTEX;
#define cipe_alloc_LOCK()       down_interruptible(&cipe_alloc_sem)
#define cipe_alloc_UNLOCK()     up(&cipe_alloc_sem)
#endif

static int cipe_alloc(struct NET_DEVICE *dev, struct siocsifcipall *parm)
{
#ifdef NO_DYNDEV
    return -ENOSYS;
#else
    int n=parm->num;
    int e;
    if (n>=cipe_maxdev)
        return -EINVAL;
    if ((e=cipe_alloc_LOCK()))
        return e;
    if (n>=0) {
        if (cipe_ctrls[n]) {
            if (cipe_ctrls[n]->cipe.sock || cipe_hasowner(n))
                e=-EBUSY;
            else
                cipe_ctrls[n]->cipe.owner=current->pid;
        } else {
            e=cipe_alloc_dev(n);
        }
    } else {
        e=-EMFILE;
        for (n=0; n<cipe_maxdev; ++n) {
            if (!cipe_ctrls[n]) {
                e=cipe_alloc_dev(n);
                break;
            }
            if (!cipe_hasowner(n)) {
                cipe_ctrls[n]->cipe.owner=current->pid;
                e=0;
                break;
            }
        }
    }
    if (!e) {
        parm->num=n;
        strncpy(parm->name, cipe_ctrls[n]->dev.name, sizeof(parm->name)-1);
        parm->name[sizeof(parm->name)-1]='\0';
    }
    cipe_alloc_UNLOCK();
    return e;
#endif
}

static int cipe_unalloc(struct NET_DEVICE *dev, struct siocsifcipall *parm)
{
#ifdef NO_DYNDEV
    return -ENOSYS;
#else
    int e;
    if (parm->num<0 || parm->num>=cipe_maxdev)
        return -EINVAL;
    if ((e=cipe_alloc_LOCK()))
        return e;
    if (cipe_ctrls[parm->num]->cipe.sock) {
        e=-EBUSY;
    } else {
        if (parm->num>0)
            cipe_unalloc_dev(parm->num);
    }
    cipe_alloc_UNLOCK();
    return e;
#endif
}


/*** Device operation handlers ***/

int cipe_dev_ioctl(struct NET_DEVICE *dev, struct ifreq *ifr, int cmd)
{
    int e=-EINVAL;

#ifdef LINUX_21

    if (!capable(CAP_NET_ADMIN))
	return -EPERM;

#define doioctl(nam,fun,str) {                                          \
    struct str parm;                                                    \
    dprintk(DEB_CALL, (KERN_DEBUG "%s: " nam "\n", dev->name));          \
    if ((e=copy_from_user((void*)&parm,(void*)ifr->ifr_data,            \
                          sizeof(parm)))<0)                             \
        goto out;                                                       \
    if (parm.magic!=VERSION_MAGIC) {                                    \
        printk(KERN_WARNING "%s: ciped version mismatch %lx -> %x\n", dev->name,parm.magic,VERSION_MAGIC); \
        e=-EINVAL; goto out; }                                          \
    if ((e=fun(dev, &parm))<0)                                          \
        goto out;                                                       \
    e=copy_to_user((void*)ifr->ifr_data, (void*)&parm, sizeof(parm));   \
    goto out;                                                           \
  }

#else

    if (!suser())
	return -EPERM;

#define doioctl(nam,fun,str) {                                              \
    struct str parm;                                                        \
    dprintk(DEB_CALL, (KERN_DEBUG "%s: " nam "\n", dev->name));              \
    if ((e=verify_area(VERIFY_READ, ifr->ifr_data, sizeof(parm)))<0)        \
        goto out;                                                           \
    memcpy_fromfs((void*)&parm, (void*)ifr->ifr_data, sizeof(parm));        \
    if (parm.magic!=VERSION_MAGIC) {                                        \
        printk(KERN_WARNING "%s: ciped version mismatch\n", dev->name);     \
        e=-EINVAL; goto out; }                                              \
    if ((e=fun(dev, &parm))<0)                                              \
        goto out;                                                           \
    if ((e=verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(parm)))<0)       \
        goto out;                                                           \
    memcpy_tofs((void*)ifr->ifr_data, (void*)&parm, sizeof(parm));          \
    goto out;                                                               \
  }

#endif

    cipe_use_module();
    switch (cmd) {
#ifdef SIOCGIFCIPPAR
    case SIOCGIFCIPPAR:
	doioctl("getpar", cipe_getpar, siocgifcippar);
#endif
    case SIOCSIFCIPPAR:
	doioctl("setpar", cipe_setpar, siocsifcippar);
    case SIOCSIFCIPKEY:
	doioctl("setkey", cipe_setkey, siocsifcipkey);
    case SIOCSIFCIPATT:
	doioctl("attach", cipe_attach, siocsifcipatt);
    case SIOCSIFCIPALL:
	doioctl("alloc", cipe_alloc, siocsifcipall);
    case SIOCSIFCIPUNA:
	doioctl("unalloc", cipe_unalloc, siocsifcipall);
    /* default: e=-EINVAL; */
    }

 out:
    cipe_unuse_module();
    return e;

#undef doioctl
}

int cipe_dev_open(struct NET_DEVICE *dev)
{
    DEVTOCIPE(dev,c,-ENODEV);
    if (!c->sock)
	return -ENXIO;
    dprintk(DEB_CALL, (KERN_DEBUG "%s: opened\n", dev->name));
    return 0;
}

void cipe_close(struct cipe *c)
{
    cipe_zero_c(c);
    dprintk(DEB_CALL, (KERN_DEBUG "%s: closed\n", c->dev->name));
    cipe_unuse_module();
}

int cipe_dev_close(struct NET_DEVICE *dev)
{
    struct cipe *c = (struct cipe*)(dev->priv);
    if ((!c) || (c->magic!=CIPE_MAGIC)) {
	printk(KERN_WARNING "%s: cipe_dev_close: no valid struct\n",
               dev->name);
	return 0;
    }
    if (c->sock) {
	dprintk(DEB_CALL, (KERN_DEBUG "%s: closing\n", c->dev->name));
	/* Tell the attached socket we're going down */
	c->sock->shutdown=SHUTDOWN_MASK;
	c->sock->zapped=1;
	c->sock->err=ENXIO;
	c->sock->error_report(c->sock);
#ifdef LINUX_21
	if (!cipe_isowned(c)) {
	    /* SHOULD NOT HAPPEN. Socket is probably left orphaned */
	    printk(KERN_ERR "cipe_dev_close: not owned??\n");
	    cipe_close(c);
	}
#endif
    } else {
	cipe_close(c);
    }
    return 0;
}

struct DEV_STATS *cipe_get_stats(struct NET_DEVICE *dev)
{
    DEVTOCIPE(dev,c,NULL);
    return &(c->stat);
}

int cipe_set_mac(struct NET_DEVICE *dev, void *p)
{
    struct sockaddr *addr=p;
    memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
    return 0;
}


/*** Initialization and finalization stuff ***/

#ifndef LINUX_21
static inline void dev_init_buffers(struct NET_DEVICE *dev)
{
    int i;
    for (i = 0; i < DEV_NUMBUFFS; i++)  {
        skb_queue_head_init(&dev->buffs[i]);
    }
}
#endif

static int cipe_init_dev(struct NET_DEVICE *dev)
{
    struct cipe *c = (struct cipe*)(dev->priv);
    if (!c)
	return -ENODEV;

    memset(c, 0, sizeof(struct cipe)); /* zero the device struct along */
    c->magic       = CIPE_MAGIC;
    c->dev         = dev;
    cipe_zero_c(c);

    /* Device parameters. */
    /* Procedural */
    dev->open                   = cipe_dev_open;
    dev->stop                   = cipe_dev_close;
    dev->hard_start_xmit        = cipe_xmit;
    /*dev->hard_header          = NULL;*/
    /*dev->rebuild_header       = NULL;*/
    dev->set_mac_address	= cipe_set_mac;
    dev->do_ioctl               = cipe_dev_ioctl;
    dev->get_stats              = cipe_get_stats;

    /* "Hardware" */
    dev->type		        = ARPHRD_TUNNEL;
    dev->hard_header_len        = 0; /* we copy anyway to expand */
    dev->mtu		        = ETH_DATA_LEN
                                     -sizeof(struct sockshdr)
                                     -cipehdrlen
                                     -cipefootlen;

    dev->tx_queue_len	        = 100; /* matches ethernet */

#ifdef LINUX_21
    dev->iflink         = -1;
#else
    dev->family		= AF_INET;
    dev->pa_alen	= 4;
    dev->metric         = 1;
#endif
    dev_init_buffers(dev);

    /* New-style flags */
    dev->flags		= IFF_POINTOPOINT|IFF_NOTRAILERS|IFF_NOARP;

    return 0;
}

static int cipe_alloc_dev(int n)
{
    int e=0;
    struct cipe_ctrl *cc;

    dprintk(DEB_CALL, (KERN_DEBUG DEVNAME ": cipe_alloc_dev %d\n", n));
    if (!(cc=kmalloc(sizeof(struct cipe_ctrl), GFP_KERNEL))) {
        cipe_ctrls[n]=NULL;
        return -ENOMEM;
    }

    memset((void *)cc, 0, sizeof(*cc));
/* If this doesn't compile, define or undefine HAVE_DEVNAME_ARRAY
   in cipe.h accordingly. */
#ifdef HAVE_DEVNAME_ARRAY
    sprintf(cc->dev.name, DEVNAME "%d", n);
#else
    sprintf(cc->name, DEVNAME "%d", n);
    cc->dev.name      = cc->name;
#endif
    cc->dev.base_addr = n; /* dummy */
    cc->dev.priv      = (void*)&(cc->cipe);
    cc->dev.next      = NULL;
    cc->dev.init      = cipe_init_dev; /* called by register_netdevice */

    /* Generate a dummy MAC address. This code seems to be in accordance
       to the address assignments as of RFC1700, pp.172f.
       We use 00-00-5E-8v-xx-nn with v=Protocol version, xx=crypto
       designator, nn=device number.
    */
    cc->dev.dev_addr[2]=0x5E;
    cc->dev.dev_addr[3]=0x80+ProtocolVersion;
    cc->dev.dev_addr[4]=CRNAMEC;
    cc->dev.dev_addr[5]=n;
    cc->dev.addr_len=6;

    e=register_netdevice(&(cc->dev));
    if (e<0) {
	kfree(cc);
	printk(KERN_ERR
	       "%s: register_netdevice() failed\n", cc->dev.name);
        cc=NULL;
    } else {
        cc->cipe.owner=current->pid;
    }
    cipe_ctrls[n]=cc;
    return e;
}

static void cipe_unalloc_dev(int n)
{
    struct cipe_ctrl *cc=cipe_ctrls[n];
    if (!cc)
	return;
    dprintk(DEB_CALL, (KERN_DEBUG DEVNAME ": cipe_unalloc_dev %d\n", n));
    if (cc->cipe.magic!=CIPE_MAGIC) {
        printk(KERN_WARNING DEVNAME ": Ouch: cipe_unalloc_dev() wrong struct\n");
        return;
    }
    unregister_netdevice(&(cc->dev));
    cipe_ctrls[n]=NULL;
    kfree(cc);
}

static int cipe_init(void)
{
    int e=cipe_check_kernel();
    if (e<0)
	return e;

    /* sanity check on insmod-provided data */
    if (cipe_maxdev<1)  cipe_maxdev=1;
    if (cipe_maxdev>255) 
    {
	    cipe_maxdev=255;
	    printk(KERN_WARNING ": Too many channels requested, setting cipe_maxdev to 255\n");
    }

#ifdef DEBUG
    printk(KERN_INFO
	   DEVNAME ": CIPE driver vers %s (c) Olaf Titz 1996-2000, %d channels, debug=%d\n",
	   driver_version, cipe_maxdev, cipe_debug);
#else
    printk(KERN_INFO
	   DEVNAME ": CIPE driver vers %s (c) Olaf Titz 1996-2000, %d channels\n",
	   driver_version, cipe_maxdev);
#endif

    prnseed=(~jiffies)^CURRENT_TIME;
    cipe_ctrls = (struct cipe_ctrl **) kmalloc(sizeof(void*)*cipe_maxdev,
					       GFP_KERNEL);
    if (!cipe_ctrls) {
	printk(KERN_ERR
	       DEVNAME ": failed to allocate master control structure\n");
	return -ENOMEM;
    }
    memset(cipe_ctrls, 0, sizeof(void*)*cipe_maxdev);
#ifdef NO_DYNDEV
    {
        int i;
	rtnl_LOCK();
        for (i=0; i<cipe_maxdev; ++i)
            if ((e=cipe_alloc_dev(i))) {
		rtnl_UNLOCK();
                return e;
	    }
	rtnl_UNLOCK();
        return 0;
    }
#else
    rtnl_LOCK();
    e=cipe_alloc_dev(0);
    rtnl_UNLOCK();
    return e;
#endif
}

static void cipe_cleanup(void)
{
    int i;
    rtnl_LOCK();
    for (i=0; i<cipe_maxdev; ++i)
	cipe_unalloc_dev(i);
    rtnl_UNLOCK();
    kfree(cipe_ctrls);
}

module_init(cipe_init);
module_exit(cipe_cleanup);
