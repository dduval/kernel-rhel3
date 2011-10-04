/*
   CIPE - encrypted IP over UDP tunneling

   sock.c - socket/input interface

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#include "cipe.h"

#include <linux/config.h>
#include <linux/sched.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/udp.h>
#ifdef LINUX_21
#include <asm/uaccess.h>
#else
typedef unsigned short mm_segment_t;
#endif

#ifdef LINUX_21
#define kfreeskb(s,t) kfree_skb(s)
#define saddr(skb) ((skb)->nh.iph->saddr)
#define daddr(skb) ((skb)->nh.iph->daddr)
#else
#define kfreeskb(s,t) kfree_skb(s,t)
#define saddr(skb) ((skb)->saddr)
#define daddr(skb) ((skb)->daddr)
#endif

/* Rewire generic socket operations to our device-specific ones.
   We have new routines for close, sendmsg, recvmsg. */

/* init struct cipe *c based on struct sock *sk */
#ifdef LINUX_21

#define SOCKTOC(nam,sk,c,r) \
    struct cipe *c=(struct cipe *)sk->user_data; \
    if ((!c) || (c->magic!=CIPE_MAGIC)) { \
	 printk(KERN_ERR "Ouch: SOCKTOC" nam "\n"); return r; } \
    else { \
         dprintk(DEB_CALL, (KERN_DEBUG "%s: " nam "\n", c->dev->name)); }

#else

#define SOCKTOC(nam,sk,c,r) \
    struct cipe *c=(struct cipe *)sk->protinfo.af_packet.bound_dev; \
    if ((!c) || (c->magic!=CIPE_MAGIC)) { \
	 printk(KERN_ERR "Ouch: SOCKTOC" nam "\n"); return r; } \
    else { \
         dprintk(DEB_CALL, (KERN_DEBUG "%s: " nam "\n", c->dev->name)); }

#endif

/* Close the socket */
void cipe_sock_close(struct sock *sock, timeout_t timeout)
{
    SOCKTOC("sock_close",sock,c,);
    c->sock=NULL;

    /* Put back the old protocol block and let it do the close */
    sock->prot=c->udp_prot;
    sock->prot->close(sock, timeout);
    if (c->dev->flags&IFF_UP) {
	rtnl_LOCK();
	dev_close(c->dev);
	rtnl_UNLOCK();
    } else {
	cipe_close(c);
    }
}

/* Anything we send to this socket is in fact a key exchange block.
   Encode and encrypt it accordingly.
*/
int cipe_sendmsg(struct sock *sock, struct msghdr *msg, int len
#ifndef LINUX_21
                 , int nonblock, int flags
#endif
    )
{
    struct msghdr mymsg;
    struct iovec myio[2];
    struct sockaddr_in sa;
    struct sockshdr sh;
    int e, n=0;
    unsigned char buf[KEYXCHGBLKMAX+blockSize];
    SOCKTOC("cipe_sendmsg",sock,c,-ENOSYS);

    if (len>KEYXCHGBLKMAX)
	return -EMSGSIZE;
    cipe_prnpad(buf, sizeof(buf));
#ifdef VER_SHORT
    memcpy_fromiovec(buf, msg->msg_iov, len);
#else
    memcpy_fromiovec(buf+blockSize, msg->msg_iov, len);
    len+=blockSize;
#endif
    if (buf[0
#ifndef VER_SHORT
             +blockSize
#endif
                       ]>=CT_DUMMY) {
        if (!c->flags&CIPF_HAVE_KEY)
            return -EINVAL;
        buf[KEYXCHGTSPOS-1]='\0';
    } else {
        if (len<KEYXCHGBLKMIN)
            return -EINVAL;
    }
    (*(__u32 *)(buf+KEYXCHGTSPOS))=htonl(CURRENT_TIME); /* timestamp */
    len=KEYXCHGBLKMIN+buf[sizeof(buf)-1]; /* random */
    cipe_encrypt(c, buf, &len, TW_NEWKEY);

    sa.sin_family=AF_INET;
    sa.sin_addr.s_addr=c->peeraddr;
    sa.sin_port=c->peerport;

    if (c->sockshost) {
	/* Prepend a socks header. */
	memset(&sh, 0, sizeof(sh));
	sh.atyp=1;
	sh.dstaddr=c->sockshost;
	sh.dstport=c->socksport;
	myio[n].iov_base=&sh;
	myio[n].iov_len=sizeof(sh);
	++n;
    }
    myio[n].iov_base=&buf;
    myio[n].iov_len=len;
    /* mymsg=*msg; */
    mymsg.msg_name=&sa;
    mymsg.msg_namelen=sizeof(sa);
    mymsg.msg_iov=myio;
    mymsg.msg_iovlen=n+1;
    /* just to be sure */
    mymsg.msg_control=NULL;
    mymsg.msg_controllen=0;
    mymsg.msg_flags=0;

    /* Call the real thing. Pretend this is user space segment. */
    {
        mm_segment_t fs=get_fs();
        set_fs(get_ds());
        if (c->sockshost)
            len+=sizeof(struct sockshdr);
        dprintk(DEB_KXC, (KERN_DEBUG "%s: real sendmsg len %d text=%04x...\n",
                          c->dev->name, len,
                          ntohs(*((unsigned short *)(myio[n].iov_base)))));
        e=c->udp_prot->sendmsg(sock, &mymsg, len
#ifndef LINUX_21
                               , nonblock, flags
#endif
            );
        set_fs(fs);
    }
    return e;
}

/* Check if we have a new peer */
static void checkpeer(struct cipe *c, __u32 saddr, __u16 sport)
{
    if (c->sockshost) {
	if (c->sockshost==saddr && c->socksport==sport)
	    return;
	/* sockshost and socksport contain the real peer's address
	   and the configured/guessed peer is really the socks relayer! */
	c->sockshost=saddr;
	c->socksport=sport;
    } else {
	if (c->peeraddr==saddr && c->peerport==sport)
	    return;
	c->peeraddr=saddr;
	c->peerport=sport;
    }
    printk(KERN_NOTICE "%s: new peer %s:%d\n",
	   c->dev->name, cipe_ntoa(saddr), ntohs(sport));
}

/* Decrypt a received packet. Requeue it or return kxc block. */
/* On entry the packet starts with the original UDP header,
   ip_hdr and h.uh are set to the IP and UDP headers. */
struct sk_buff *cipe_decrypt_skb(struct cipe *c, struct sock *sk,
				 struct sk_buff *skb, int *offset,
                                 int *msgflag)
{
    struct sk_buff *n=NULL;
    int length;
    __u32 rsaddr;
    __u16 rsport;

#ifdef DEBUG
    if (cipe_debug&DEB_PKIN)
        cipe_dump_packet("received", skb, 1);
#endif
    length = skb->len - sizeof(struct udphdr);
    n=alloc_skb(skb->len, GFP_KERNEL);
    if (!n) {
	printk(KERN_WARNING "%s: cipe_decrypt_skb: out of memory\n",
	       c->dev->name);
	++c->stat.rx_dropped;
	return NULL;
    }
#if 0 /*def LINUX_21*/
    if (skb->sk)
	skb_set_owner_r(n, skb->sk);
#endif
#ifndef LINUX_21
    n->free=1;
#endif
    n->dev=c->dev;

    /* Copy the datagram into new buffer, skip IP header */
    /* We must copy here because raw sockets get only a clone of the
       skbuff, so they would receive the plaintext */
    dprintk(DEB_INP, (KERN_DEBUG DEVNAME
                      ": sa=%s da=%s us=%d ud=%d ul=%d\n",
                      cipe_ntoa(saddr(skb)), cipe_ntoa(daddr(skb)),
                      ntohs(skb->h.uh->source), ntohs(skb->h.uh->dest),
                      ntohs(skb->h.uh->len)));

    /* why did this get swapped?? */
#ifdef LINUX_21
    rsaddr=saddr(skb);
#else
    rsaddr=daddr(skb);
#endif
    rsport=skb->h.uh->source;
    skb_put(n,skb->len);
#ifdef MAX_SKB_FRAGS
    /* Zerocopy version. -DaveM */
    skb_copy_bits(skb, (skb->h.raw - skb->data), n->data, skb->len);
#else
    memcpy(n->data, skb->h.raw, skb->len);
#endif
    n->h.uh=(struct udphdr *)n->data;

    if (c->sockshost) {
	/* Pull out the SOCKS header and correct the peer's address. */
	struct sockshdr *sh;
	sh=(struct sockshdr *)skb_pull(n, sizeof(struct udphdr));
	dprintk(DEB_INP, (KERN_DEBUG "socks: fr=%d at=%d da=%s dp=%d\n",
                          sh->frag, sh->atyp,
                          cipe_ntoa(sh->dstaddr), ntohs(sh->dstport)));
	if (sh->frag || (sh->atyp!=1))
	    goto error;
	/* Pull out UDP header */
#ifdef LINUX_21
        n->nh.iph=(struct iphdr *)skb_pull(n, sizeof(struct sockshdr));
#else
	n->ip_hdr=(struct iphdr *)skb_pull(n, sizeof(struct sockshdr));
#endif
	/***saddr(n)=sh->dstaddr;*/
        rsaddr=sh->dstaddr;
	rsport=n->h.uh->source=sh->dstport;
	length-=sizeof(struct sockshdr);
	*offset=sizeof(struct sockshdr)+sizeof(struct udphdr);
    } else {
	/* Pull out UDP header */
#ifdef LINUX_21
	n->nh.iph=(struct iphdr *) skb_pull(n, sizeof(struct udphdr));
#else
	n->ip_hdr=(struct iphdr *) skb_pull(n, sizeof(struct udphdr));
#endif
	/***saddr(n)=rsaddr;*/
	n->h.uh->source=rsport;
	*offset=0;
    }
    n->mac.raw=n->data; /* no hardware header */

    if (c->flags&CIPF_HAVE_KEY) {
	if (length%blockSize) {
	    printk(KERN_DEBUG "%s: got bogus length=%d\n", c->dev->name,
		   length);
	    goto error;
	}
        switch (cipe_decrypt(c, n->data, &length)) {
	case TW_DATA:
            if (!c->flags&CIPF_MAY_STKEY && !c->flags&CIPF_HAVE_RKEY) {
                printk(KERN_ERR "%s: got data using invalid static key\n",
                       c->dev->name);
                goto error;
            }
	    break;
	case TW_CTRL:
            /* return it as a control block - out of band datagram */
            *msgflag|=MSG_OOB;
            /* fall thru */
	case TW_NEWKEY:
	    /* return it as key exchange block - proper UDP datagram */
	    dprintk(DEB_INP, (KERN_DEBUG "TW_NEWKEY data=%p len=%d length=%d\n", n->data, n->len, length));
#ifdef LINUX_21
	    do_gettimeofday(&n->stamp);
#endif
	    skb_trim(n, length);
	    checkpeer(c, rsaddr, rsport);
#if 0
	    n->saddr=c->myaddr;
	    n->daddr=c->peeraddr;
#endif
	    n->h.uh->check=0;
	    return n;
	default:
	    /* Bogus packet etc. */
	    ++c->stat.rx_crc_errors;
	    goto error;
	}
    } else if (!c->flags&CIPF_MAY_CLEAR) {
	goto error;
    }

    dprintk(DEB_INP, (KERN_DEBUG "TW_DATA data=%p len=%d length=%d\n", n->data, n->len, length));
    skb_trim(n, length);
    checkpeer(c, rsaddr, rsport);
    /* adjust pointers */
#ifdef LINUX_21
    n->nh.iph=(struct iphdr *)n->data;
    memset(&(IPCB(n)->opt), 0, sizeof(IPCB(n)->opt));
#else
    n->h.iph=(struct iphdr *)n->data;
    memset(n->proto_priv, 0, sizeof(struct options));
#endif
    if (length<cipehdrlen+(c->sockshost?sizeof(struct sockshdr):0)) {
        printk(KERN_INFO "%s: got short packet from %s\n", c->dev->name,
               cipe_ntoa(saddr(skb)));
	goto framerr;
    }

    n->protocol = __constant_htons(ETH_P_IP);
    n->ip_summed = 0;
    n->pkt_type = PACKET_HOST;
    dprintk(DEB_INP, (KERN_DEBUG "%s: real src %s dst %s len %d\n",
                      n->dev->name, cipe_ntoa(saddr(n)),
                      cipe_ntoa(daddr(n)), length));

#ifdef DEBUG
    if (cipe_debug&DEB_PKIN)
        cipe_dump_packet("decrypted", n, 1);
#ifdef LINUX_23
    dprintk(DEB_INP, (KERN_DEBUG "%s state=%ld refcnt=%d\n", n->dev->name, n->dev->state,
		      atomic_read(&n->dev->refcnt)));
#endif
#endif

    /* requeue */
    netif_rx(n);
#ifdef LINUX_21
    c->stat.rx_bytes+=skb->len; /* count carrier-level bytes */
#endif
    c->stat.rx_packets++;
    return NULL;

 framerr:
    ++c->stat.rx_frame_errors; /* slightly abuse this */
 error:
    ++c->stat.rx_errors;
    if (n)
	kfreeskb(n, FREE_READ);
    return NULL;
}

/* Receive message. If encrypted, put it through the mill.
   If decrypted, return it as key exchange block.
   This is mostly from net/ipv4/udp.c, but cipe_decrypt_skb pulls the
   UDP header.
*/

int cipe_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		 int noblock, int flags,int *addr_len)
{
  	int copied;
  	struct sk_buff *skb, *skn;
  	int er=0, of;
  	struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;
	SOCKTOC("cipe_recvmsg",sk,c,-ENOSYS);

	/*
	 *	Check any passed addresses
	 */

  	if (addr_len)
  		*addr_len=sizeof(*sin);

	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */

	do {
	    /* CIPE: look if the packet is encrypted, repeat until
	       a decrypted one is found */
	    skn=NULL;
	    skb=skb_recv_datagram(sk,flags,noblock,&er);
	    if(skb==NULL) {
		dprintk(DEB_KXC, (KERN_DEBUG "%s: skb_recv_datagram: %d\n",
                                  c->dev->name, er));
                return er;
	    }

	    if (!skb->h.uh->source) {
		/* Synthetic KXC packets are marked by source port 0.
		   Correct this - we know the truth from our structure.
		   Perhaps it would be better to not correct it so the
		   user level daemon can spot the difference? */
		skb->h.uh->source=c->peerport;
		of=skb->data-skb->head+sizeof(struct udphdr);
		break;
	    }
	    skn=cipe_decrypt_skb(c, sk, skb, &of, &(msg->msg_flags));
	    skb_free_datagram(sk, skb);
	    skb=skn;
	} while (skb==NULL);

	dprintk(DEB_INP, (KERN_DEBUG "%s: returning %d flags %04x\n",
			  c->dev->name, skb->len, msg->msg_flags));
  	copied = skb->len-of;
	if (copied > len)
	{
		copied = len;
#ifdef LINUX_21
		msg->msg_flags |= MSG_TRUNC;
#endif
	}
	if (copied<=0) {
	    printk(KERN_ERR "cipe_recvmsg: bogus length %d\n", copied);
	    goto out_free;
	}

  	/*
  	 *	FIXME : should use udp header size info value
  	 */
#ifdef LINUX_21
	er = skb_copy_datagram_iovec(skb,of,msg->msg_iov,copied);
	if (er<0)
		goto out_free;
#else
        skb_copy_datagram_iovec(skb,of,msg->msg_iov,copied);
#endif
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin
#ifdef LINUX_21
            && skb->nh.iph   /* Fake KXC has no address */
#endif
	) {
		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
                sin->sin_addr.s_addr = daddr(skb);
	}
        er=copied;

 out_free:
	if (skb==skn)
	    /* the buffer was a copy made by decryption */
	    kfreeskb(skb, FREE_READ);
	else
	    skb_free_datagram(sk, skb);

  	return(er);
}

#ifdef LINUX_21

#include <linux/file.h>

int cipe_attach(struct NET_DEVICE *dev, struct siocsifcipatt *parm)
{
    struct file *file;
    struct inode *inode;
    struct socket *sock;
    struct sock *sk;
#if 0
    struct in_device *indev;
#endif
    struct cipe *c0;
    DEVTOCIPE(dev,c,-ENODEV);

    if (c->sock)
        return -EBUSY;
    if (!(file=fget(parm->fd)))
        return(-EBADF);
    inode = file->f_dentry->d_inode;
    if (!inode || !inode->i_sock || !(sock=&inode->u.socket_i)) {
        fput(file);
        return(-ENOTSOCK);
    }
    if (sock->file != file) {
        printk(KERN_ERR DEVNAME ": socki_lookup: socket file changed!\n");
        sock->file = file;
    }

    fput(file);
    if (sock->type!=SOCK_DGRAM)
	return(-ESOCKTNOSUPPORT);
    if (sock->ops->family!=AF_INET)
	return(-EAFNOSUPPORT);

    sk=sock->sk;
    if (sk->state!=TCP_ESTABLISHED)
	return(-ENOTCONN);
    if (((c0=(struct cipe *)sk->user_data)) &&
	(c0->magic==CIPE_MAGIC))
	return(-EBUSY); /* socket is already attached */

    cipe_use_module();
    c->owner=current->pid;
    c->sock=sk;
    c->peeraddr=sk->daddr;
    c->peerport=sk->dport;
    c->myaddr=sk->saddr;
    if (c->flags&CIPF_MAY_DYNIP)
        sk->rcv_saddr=0;
    c->myport=htons(sk->num);
    /* Disconnect socket, we might receive from somewhere else */
    sk->daddr=0;
    sk->dport=0;

    /* Fill an otherwise unused field in the sock struct with this info.
       This field is conveniently named and the kernel uses it only for RPC. */
    sk->user_data=c;
    sk->no_check=UDP_CSUM_NOXMIT; /* our packets are checksummed internally */

    /* Set up new socket operations */
    c->udp_prot=sk->prot;
    c->cipe_proto=*sk->prot;
    c->cipe_proto.close=cipe_sock_close;
    c->cipe_proto.sendmsg=cipe_sendmsg;
    c->cipe_proto.recvmsg=cipe_recvmsg;
    sk->prot=&c->cipe_proto;

#if 0
    /* (Try to) Set our dummy hardware address from the IP address. */
    /* This does not work, because the IP address is set
       _after_ the attach... */
    if ((indev=dev->ip_ptr)) {
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	for (ifap=&indev->ifa_list; (ifa=*ifap) != NULL; ifap=&ifa->ifa_next)
	    if (strcmp(dev->name, ifa->ifa_label) == 0)
		break;
	if (ifa) {
	    char *x=(char *)&ifa->ifa_local;
	    dev->dev_addr[3]=x[1]|0x80;
	    dev->dev_addr[4]=x[2];
	    dev->dev_addr[5]=x[3];
	}
    }
#endif

    return(0);
}


#else /* LINUX_21 */

#define sreturn(x) {sti(); return((x));}

int cipe_attach(struct NET_DEVICE *dev, struct siocsifcipatt *parm)
{
    struct file *file;
    struct inode *inode;
    struct socket *sock;
    struct sock *sk;
    int fd=parm->fd;
    struct cipe *c0;
    DEVTOCIPE(dev,c,-ENODEV);
    cli();
    if (c->sock)
	sreturn(-EBUSY);

    /* Find the socket (from net/socket.c) */
    if (fd < 0 || fd >= NR_OPEN || !(file = current->files->fd[fd]))
	sreturn(-EBADF);
    inode = file->f_inode;
    if (!inode || !inode->i_sock)
	sreturn(-ENOTSOCK);
    sock=&inode->u.socket_i;
    if (sock->type!=SOCK_DGRAM)
	sreturn(-ESOCKTNOSUPPORT);
    if (sock->ops->family!=AF_INET)
	sreturn(-EAFNOSUPPORT);
    sk=(struct sock *)sock->data;
    if (sk->state!=TCP_ESTABLISHED)
	sreturn(-ENOTCONN);
    if (((c0=(struct cipe *)sk->protinfo.af_packet.bound_dev)) &&
	(c0->magic==CIPE_MAGIC))
	sreturn(-EBUSY); /* socket is already attached */

    cipe_use_module();
    c->owner=current->pid;
    c->sock=sk;
    c->peeraddr=sk->daddr;
    c->peerport=sk->dummy_th.dest;
    c->myaddr=sk->saddr;
    if (c->flags&CIPF_MAY_DYNIP)
        sk->rcv_saddr=0;
    c->myport=sk->dummy_th.source;
    /* Disconnect socket, we might receive from somewhere else */
    sk->daddr=0;
    sk->dummy_th.dest=0;

    /* Set up new socket operations */
    c->udp_prot=sk->prot;
    c->cipe_proto=*sk->prot;
    c->cipe_proto.close=cipe_sock_close;
    c->cipe_proto.sendmsg=cipe_sendmsg;
    c->cipe_proto.recvmsg=cipe_recvmsg;
    sk->prot=&c->cipe_proto;

    /* Fill an otherwise unused field in the sock struct with this info.
       Actually, this is very similar to a packet socket!
       The ugly cast saves us one deref in the actual ops */
    sk->protinfo.af_packet.bound_dev=(struct NET_DEVICE *)c;
    sk->no_check=1; /* our packets are checksummed internally */

    sti();
    return 0;
}

#endif


/* Build and enqueue a fake UDP packet to receive.
   Note that these are neither encrypted nor SOCKSed.
*/
void cipe_fakenkey(struct cipe *c, char typ)
{
    int len=sizeof(struct udphdr)+1;
    struct sk_buff *skb=alloc_skb(len, GFP_ATOMIC);

    if (!skb) {
        printk(KERN_WARNING "%s: cipe_fakenkey: out of memory\n",
               c->dev->name);
        return; /* not much we can do */
    }

    dprintk(DEB_KXC, (KERN_DEBUG "%s: fake kxc block typ=%d\n",
                      c->dev->name, typ));

    skb->sk=NULL;
    skb->dev=c->dev;

    skb->h.uh=(struct udphdr *)skb->data;
    skb->len=len;
#ifdef LINUX_21
    skb->nh.iph=NULL;
#else
    saddr(skb)=c->myaddr;
    daddr(skb)=c->peeraddr;
    skb->free=1; /* Discard after use */
    skb->ip_hdr=NULL;
#endif
    skb->h.uh->source=0; /* mark as KXC packet */
    skb->h.uh->dest=c->myport;
    len-=sizeof(struct udphdr);
    skb->h.uh->len=htons(len);
    skb->h.uh->check=0;
    skb->mac.raw=skb->data; /* no hardware header */

    /* All those contortions for just one byte of payload data.
       Since we generate only NK_RREQ and NK_REQ it's effectively
       one _bit_... */
    skb->data[sizeof(struct udphdr)]=typ;

    if (sock_queue_rcv_skb(c->sock, skb)<0) {
        printk(KERN_WARNING "%s: cipe_fakenkey: enqueuing failed\n",
               c->dev->name);
        kfreeskb(skb, FREE_WRITE);
    }
}
