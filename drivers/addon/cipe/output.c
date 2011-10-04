/*
   CIPE - encrypted IP over UDP tunneling

   output.c - the sending part of the CIPE device

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#include "cipe.h"

#include <net/ip.h>
#include <net/icmp.h>
#include <linux/if_arp.h>
#include <linux/socket.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#include <linux/netfilter_ipv4.h>
#else
#define NF_HOOK(pf, hook, skb, indev, outdev, okfn) (okfn)(skb)
#endif

#ifdef DEBUG

static void cipe_hexdump(const unsigned char *bp, unsigned int len)
{
    static const __u16 p[16]={0,2,5,7,10,12,15,17,21,23,26,28,31,33,36,38};
    static const char h[16]="0123456789abcdef";
    short i=0, j=0;
    static /*?*/ char b[58];

    printk(KERN_DEBUG "(cipe_hexdump %p %x)\n", bp, len);
    if (len>1024)
	len=1024; /* sanity */
    memset(b, ' ', sizeof(b));
    while (len-->0) {
	if (i>15) {
	    b[sizeof(b)-1]=0;
	    printk(KERN_DEBUG " %04x:  %s\n", j, b);
	    memset(b, ' ', sizeof(b));
	    i=0; j+=16;
	}
	b[p[i]]=h[*bp>>4]; b[p[i]+1]=h[*bp&15];
	b[i+42]=(((*bp)&0x60)==0||*bp==127)?'.':*bp;
	++bp; ++i;
    }
    b[sizeof(b)-1]=0;
    printk(KERN_DEBUG " %04x:  %s\n", j, b);
}

void cipe_dump_packet(char *title, struct sk_buff *skb, int dumpskb)
{
    LOCK_PRINTK;
    if (dumpskb) {
	printk(KERN_DEBUG "SKB:\n");
	cipe_hexdump((unsigned char *)skb, sizeof(*skb));
    }
    printk(KERN_DEBUG
#ifdef LINUX_21
           "%s: packet len=%x dev=%s\n",
#else
           "%s: packet len=%lx dev=%s\n",
#endif
           title, skb->len, skb->dev?skb->dev->name:"<none>");
    cipe_hexdump((unsigned char *)skb->data, skb->tail-skb->data);
    UNLOCK_PRINTK;
}
#endif

#ifdef LINUX_21

/* An adapted version of Linux 2.1 net/ipv4/ipip.c output routine. */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,37)
#define ip_select_ident(h,d) (h)->id=htons(ip_id_count++)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,36)
/* this is not exactly the newer kernel's routine, it just does what we need */
struct sk_buff *skb_copy_expand(struct sk_buff *skb, int max_headroom,
				int max_tailroom, int gfp)
{
    struct sk_buff *n=alloc_skb(skb->len+max_headroom+max_tailroom, gfp);
    if (n) {
	skb_reserve(n, max_headroom);
	skb_put(n,skb->len);
	memcpy(n->data,skb->data,skb->len);
	n->priority=skb->priority;
	n->protocol=skb->protocol;
	n->dev=skb->dev;
	n->dst=dst_clone(skb->dst);
	memcpy(n->cb, skb->cb, sizeof(skb->cb));
	n->used=skb->used;
	n->is_clone=0;
	atomic_set(&n->users, 1);
	n->pkt_type=skb->pkt_type;
	n->stamp=skb->stamp;
	n->security=skb->security;
#ifdef CONFIG_IP_FIREWALL
	n->fwmark = skb->fwmark;
#endif
    }
    return n;
}
#endif

int cipe_xmit(struct sk_buff *skb, struct NET_DEVICE *dev)
{
        struct cipe *tunnel = (struct cipe*)(dev->priv);
	struct rtable *rt = NULL;			/* Route to the other host */
	struct NET_DEVICE *tdev;			/* Device to other host */
	struct iphdr  *old_iph = skb->nh.iph;
	u8     	tos = old_iph->tos;
	u16	df = old_iph->frag_off&__constant_htons(IP_DF);
	u8	ttl = tunnel->cttl ? tunnel->cttl : old_iph->ttl;

	struct iphdr  *iph;			/* Our new IP header */
        struct udphdr *udph;
	int    	max_headroom;			/* The extra header space needed */
	int    	max_tailroom;
	u32    	dst = tunnel->peeraddr;
	int    	mtu;
        int     length = ntohs(skb->nh.iph->tot_len);

       	dprintk(DEB_OUT, (KERN_DEBUG "%s: cipe_xmit len=%d(%d)\n", dev->name,
                          length, skb->len));
	if (length!=skb->len) {
	    printk(KERN_ERR "%s: cipe_xmit packet length problem\n",
		   dev->name);
	    goto tx_error_out;
	}
        if (tunnel->magic!=CIPE_MAGIC) {
            printk(KERN_ERR DEVNAME
                   ": cipe_xmit called with wrong struct\n");
            return 0;
        }

        if (tunnel->recursion++) {
        	printk(KERN_ERR "%s: cipe_xmit reentrance\n", dev->name);
		tunnel->stat.collisions++;
		goto tx_error_out;
	}

	if (skb->protocol != __constant_htons(ETH_P_IP))
		goto tx_error_out;

	/* Tell the netfilter framework that this packet is not the
           same as the one before! */
#ifdef CONFIG_NETFILTER
	nf_conntrack_put(skb->nfct);
	skb->nfct = NULL;
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif
#endif

#if 0
        dprintk(DEB_OUT, (KERN_DEBUG "routing dst=%s src=%s tos=%x oif=%d\n",
                          cipe_ntoa(0, dst), cipe_ntoa(1, tunnel->myaddr),
                          RT_TOS(tos), tunnel->sock->bound_dev_if));
#endif
	{
		struct flowi fl = {
			.oif = tunnel->sock->bound_dev_if,
			.nl_u = { .ip4_u =
				  { .daddr = dst,
				    .saddr = tunnel->sock->rcv_saddr,
				    .tos = RT_TOS(tos) } } };

		if (ip_route_output_key(&rt, &fl)) {
			dprintk(DEB_OUT, (KERN_NOTICE "%s: no route\n",
					  dev->name));
			tunnel->stat.tx_carrier_errors++;
			dst_link_failure(skb);
			goto tx_error_out;
		}
	}
        if (rt->rt_src!=tunnel->myaddr) {
            printk(KERN_NOTICE "%s: changing my address: %s\n", dev->name,
                   cipe_ntoa(rt->rt_src));
            tunnel->myaddr=tunnel->sock->saddr=rt->rt_src;
	}

	tdev = rt->u.dst.dev;
        dprintk(DEB_OUT, (KERN_DEBUG "route dev=%s flags=%x type=%x\n",
                          tdev->name, rt->rt_flags, rt->rt_type));

	if (tdev == dev) {
        	printk(KERN_ERR "%s: looped route\n", dev->name);
		tunnel->stat.collisions++;
		goto tx_error;
	}

        mtu = dst_pmtu(&rt->u.dst) - (cipehdrlen+cipefootlen);
        if (tunnel->sockshost)
            mtu -= sizeof(struct sockshdr);

        dprintk(DEB_OUT, (KERN_DEBUG "pmtu=%d dmtu=%d size=%d\n",
                          mtu, tdev->mtu, skb->len));

	if (mtu < 68) {
        	printk(KERN_ERR "%s: MTU too small\n", dev->name);
		tunnel->stat.collisions++;
		goto tx_error;
	}
	if (skb->dst && mtu < dst_pmtu(skb->dst)) {
		skb->dst->ops->update_pmtu(skb->dst, mtu);
        	dprintk(DEB_OUT, (KERN_NOTICE "%s: adjusting PMTU\n", dev->name));
#if 0
                /* TEST: is this OK? */
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
                goto tx_error;
#endif
        }

	if ((old_iph->frag_off&__constant_htons(IP_DF)) && mtu < ntohs(old_iph->tot_len)) {
        	dprintk(DEB_OUT, (KERN_NOTICE "%s: fragmentation needed\n", dev->name));
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
		goto tx_error;
	}

#ifdef DEBUG
        if (cipe_debug&DEB_PKOU)
            cipe_dump_packet("original", skb, 0);
#endif

        max_headroom = ((tdev->hard_header_len+cipehdrlen+
			 ((tunnel->sockshost) ? sizeof(struct sockshdr) : 0)
	                )+16)&(~15);
        max_tailroom = (tunnel->flags&CIPF_HAVE_KEY) ? cipefootlen : 0;
	{
	    struct sk_buff *n= skb_copy_expand(skb, max_headroom,
                                               max_tailroom, GFP_ATOMIC);
            if (!n) {
                printk(KERN_INFO "%s: Out of memory, dropped packet\n",
                       dev->name);
                goto tx_error;
            }
            if (skb->sk)
                    skb_set_owner_w(n, skb->sk);
	    dev_kfree_skb(skb);
            skb = n;
        }
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;
	skb->protocol = htons(ETH_P_IP);

	if (tunnel->flags&CIPF_HAVE_KEY) {
#ifndef VER_SHORT
	    /* Add an IV */
	    cipe_cryptpad(skb_push(skb, blockSize));
	    length+=blockSize;
#endif
            if (!tunnel->flags&CIPF_MAY_STKEY && !tunnel->flags&CIPF_HAVE_SKEY)
                /* Attempt to encrypt data using invalid static key */
                goto tx_error;
	    cipe_encrypt(tunnel, skb->data, &length, TW_DATA);
	    /* This is incorrect - the tail room gets first used and then
	       reserved. Doesn't matter in the current (2.0.29) implementation
	       of skb_put though. Alternative solution would ruin the nice
	       module separation - we don't need to know the real amount
	       of padding here. */
	    if (length-skb->len > skb->end-skb->tail) {
		printk(KERN_ERR "%s: tailroom problem %d %d %d\n",
		       dev->name, length, skb->len, skb->end-skb->tail);
		goto tx_error;
	    }
	    (void) skb_put(skb, length-skb->len);
	} else if (!tunnel->flags&CIPF_MAY_CLEAR) {
	    goto tx_error;
	}

        if (tunnel->sockshost) {
	    /* Install a SOCKS header */
	    struct sockshdr *sh = (struct sockshdr *)
		skb_push(skb, sizeof(struct sockshdr));
	    memset(sh, 0, 4);
	    sh->atyp=1;
	    /* sockshost and socksport contain the real peer's address
	       and the configured/guessed peer is really the socks relayer! */
	    sh->dstaddr=tunnel->sockshost;
	    sh->dstport=tunnel->socksport;
	    length+=sizeof(struct sockshdr);
	}

        /* Install our new headers */
        udph = skb->h.uh = (struct udphdr *) skb_push(skb, sizeof(struct udphdr));
	skb->mac.raw = skb_push(skb, sizeof(struct iphdr));
        iph = skb->nh.iph = (struct iphdr *) skb->mac.raw;

	/*
	 *	Push down and install the CIPE/UDP header.
	 */
	iph->version    =	4;
	iph->ihl        =	sizeof(struct iphdr)>>2;
	iph->tos        =	tos;
	iph->tot_len    =	htons(skb->len);
	ip_select_ident(iph, &rt->u.dst, tunnel->sock);
        iph->frag_off   =	df;
        iph->ttl        =	ttl;
	iph->protocol   =	IPPROTO_UDP;
	iph->saddr      =	rt->rt_src;
	iph->daddr      =	rt->rt_dst;

	ip_send_check(iph);

        udph->source = tunnel->myport;
        udph->dest   = tunnel->peerport;
        udph->len    = htons(length+sizeof(struct udphdr));
        /* Encrypted packets are checksummed already, so we can safely
	   ignore the UDP checksum */
        udph->check  = 0;

	tunnel->stat.tx_bytes += skb->len;
	tunnel->stat.tx_packets++;
        dprintk(DEB_OUT, (KERN_DEBUG
                          "%s: sending %d from %s:%d to %s:%d\n",
                          dev->name, skb->len,
                          cipe_ntoa(iph->saddr), ntohs(udph->source),
                          cipe_ntoa(iph->daddr), ntohs(udph->dest)));
	if (tunnel->sockshost)
	    dprintk(DEB_OUT, (KERN_DEBUG "%s: via SOCKS to %s:%d\n", dev->name,
			      cipe_ntoa(tunnel->sockshost),
			      ntohs(tunnel->socksport)));
#if 0
        dprintk(DEB_OUT, (KERN_DEBUG "dst: (%d,%d) %s %d %d\n",
                          skb->dst->refcnt, skb->dst->use,
                          skb->dst->dev->name, dst_pmtu(skb->dst),
                          skb->dst->error));
#endif
#ifdef DEBUG
        if (cipe_debug&DEB_PKOU)
            cipe_dump_packet("sending", skb, 0);
#endif

	/* Send "new" packet from local host */
	NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, skb, NULL, rt->u.dst.dev,
		dst_output);
        tunnel->recursion--;
	return 0;

 tx_error:
        ip_rt_put(rt);
 tx_error_out:
	tunnel->stat.tx_errors++;
	dev_kfree_skb(skb);
	tunnel->recursion--;
	return 0;
}

#else /* LINUX_21 */

/* An adapted version of Linux 2.0 drivers/net/new_tunnel.c. */

#ifdef SO_BINDTODEVICE
  #define iproute(t,o,d) ip_rt_route(t,o,d)
#else
  #define iproute(t,o,d) ip_rt_route(t,o)
#endif

#if LINUX_VERSION_CODE < 131102  /* < 2.0.30 */
  #include <linux/config.h>
  #ifdef CONFIG_IP_FORWARD
    #define ipforward(s,d,o,t) ip_forward(s,d,o,t)
  #else
    #error "Requires IP forwarding enabled in kernel"
  #endif
#else                            /* >= 2.0.30 */
  #define ipforward(s,d,o,t) (sysctl_ip_forward ? ip_forward(s,d,o,t) : -1)
#endif

int cipe_xmit(struct sk_buff *skb, struct NET_DEVICE *dev)
{
    struct enet_statistics *stats;	/* This device's statistics */
    struct rtable *rt;     		/* Route to the other host */
    struct NET_DEVICE *tdev;		/* Device to other host */
    struct iphdr  *iph;			/* Our new IP header */
    struct udphdr *udph;
    __u32          target;		/* The other host's IP address */
    int      max_headroom;		/* The extra header space needed */
    int      max_tailroom;
    int tos, ttl, length;
    DEVTOCIPE(dev,c,0);

    if (skb == NULL || dev == NULL) {
	dprintk(DEB_OUT, (KERN_DEBUG "%s: nothing to do\n", dev->name));
	return 0;
    }

    /*
     *	Make sure we are not busy (check lock variable)
     */
    stats = &(c->stat);
    if (dev->tbusy != 0)
    {
	printk(KERN_WARNING "%s: device timeout (really possible?)\n",
	       dev->name);
	dev->tbusy=0;
	stats->tx_errors++;
	return(1);
    }

#ifdef DEBUG
    if (cipe_debug&DEB_PKOU)
        cipe_dump_packet("original", skb, 0);
#endif
    /*
     *  First things first.  Look up the destination address in the
     *  routing tables
     */
    target = c->peeraddr;
    if ((!target) || (!c->peerport) || (!c->myport)) {
	/* unconfigured device */
	printk(KERN_DEBUG "%s: unconfigured\n", dev->name);
	goto error;
    }
    if ((rt = iproute(target, 0, skb->sk?skb->sk->bound_device:NULL)) == NULL)
    {
	/* No route to host */
	printk(KERN_INFO "%s: target unreachable\n", dev->name);
	goto error;
    }
    dprintk(DEB_OUT, (KERN_DEBUG
		      "%s: routing to %08lX from %08lX via %08lX dev %s\n",
		      dev->name, ntohl(rt->rt_dst), ntohl(rt->rt_src),
		      ntohl(rt->rt_gateway), rt->rt_dev->name));

    tdev = rt->rt_dev;
    ip_rt_put(rt);

    if (tdev == dev)
    {
	/* Tunnel to ourselves?  -- I don't think so. */
	printk ( KERN_INFO "%s: Packet targetted at myself!\n" , dev->name);
	goto error;
    }

    /*
     * Okay, now see if we can stuff it in the buffer as-is. We can not.
     */
    max_headroom = (((tdev->hard_header_len+15)&~15)+cipehdrlen+
		    ((c->sockshost) ? sizeof(struct sockshdr) : 0));
    max_tailroom = (c->flags&CIPF_HAVE_KEY) ? cipefootlen : 0;
    {
		struct sk_buff *new_skb;

		if ( !(new_skb =
		       dev_alloc_skb(skb->len+max_headroom+max_tailroom)) )
		{
			printk(KERN_INFO "%s: Out of memory, dropped packet\n",
			       dev->name);
  			dev->tbusy = 0;
  			stats->tx_dropped++;
			dev_kfree_skb(skb, FREE_WRITE);
			return 0;
		}
		new_skb->free = 1;

		/*
		 * Reserve space for our header and the lower device header
		 */
		skb_reserve(new_skb, max_headroom);

		/*
		 * Copy the old packet to the new buffer.
		 * Note that new_skb->h.iph will be our (tunnel driver's) header
		 * and new_skb->ip_hdr is the IP header of the old packet.
		 */
		new_skb->ip_hdr = (struct iphdr *) skb_put(new_skb, skb->len);
		new_skb->mac.raw = new_skb->data;
		new_skb->dev = skb->dev;
		memcpy(new_skb->ip_hdr, skb->data, skb->len);
		memset(new_skb->proto_priv, 0, sizeof(skb->proto_priv));

		/* Free the old packet, we no longer need it */
		dev_kfree_skb(skb, FREE_WRITE);
		skb = new_skb;
    }

	tos    = skb->ip_hdr->tos;
	ttl    = skb->ip_hdr->ttl;
        length = skb->len;
	if (c->flags&CIPF_HAVE_KEY) {
#ifndef VER_SHORT
	    /* Add an IV */
	    cipe_cryptpad(skb_push(skb, blockSize));
	    length+=blockSize;
#endif
            if (!c->flags&CIPF_MAY_STKEY && !c->flags&CIPF_HAVE_SKEY)
                /* Attempt to encrypt data using invalid static key */
                goto error;
	    cipe_encrypt(c, skb->data, &length, TW_DATA);
	    /* This is incorrect - the tail room gets first used and then
	       reserved. Doesn't matter in the current (2.0.29) implementation
	       of skb_put though. Alternative solution would ruin the nice
	       module separation - we don't need to know the real amount
	       of padding here. */
	    (void) skb_put(skb, length-skb->len);
	} else if (!c->flags&CIPF_MAY_CLEAR) {
	    goto error;
	}

        if (c->sockshost) {
	    /* Install a SOCKS header */
	    struct sockshdr *sh = (struct sockshdr *)
		skb_push(skb, sizeof(struct sockshdr));
	    memset(sh, 0, 4);
	    sh->atyp=1;
	    /* sockshost and socksport contain the real peer's address
	       and the configured/guessed peer is really the socks relayer! */
	    sh->dstaddr=c->sockshost;
	    sh->dstport=c->socksport;
	    length+=sizeof(struct sockshdr);
	}

        /* Install our new headers */
        udph = (struct udphdr *) skb_push(skb, sizeof(struct udphdr));
        skb->h.iph = (struct iphdr *) skb_push(skb, sizeof(struct iphdr));

	/*
	 *	Push down and install the CIPE/UDP header.
	 */

	iph 			=	skb->h.iph;
	iph->version		= 	4;
	iph->tos		=	tos;

	/* In new_tunnel.c, we use the original packet's TTL here.
	   Setting a new TTL behaves better to the user, and RFC2003
	   recommends it too. But this doesn't fully protect against
	   routing loops. So make it configurable via an argument:
	   "cttl" gives the TTL value; if 0 use the packet's
	   value. Default should be 64, as with the other protocols
	   (ip_statistics.IpDefaultTTL, but this variable is not
	   available for modules). */

	iph->ttl 		=	c->cttl ? c->cttl : ttl;

	iph->frag_off		=	0;
	iph->daddr		=	target;
	iph->saddr		=	c->myaddr; /* tdev->pa_addr; */
	iph->protocol		=	IPPROTO_UDP;
	iph->ihl		=	5;
	iph->tot_len		=	htons(skb->len);
	iph->id			=	htons(ip_id_count++);	/* Race condition here? */
	ip_send_check(iph);

        udph->source = c->myport;
        udph->dest   = c->peerport;
        udph->len    = htons(length+sizeof(struct udphdr));
        /* Encrypted packets are checksummed already, so we can safely
	   ignore the UDP checksum */
        udph->check  = 0;

	skb->ip_hdr 		= skb->h.iph;
	skb->protocol		=	htons(ETH_P_IP);

	/*
	 *	Send the packet on its way!
	 *	Note that dev_queue_xmit() will eventually free the skb.
	 *	If ip_forward() made a copy, it will return 1 so we can free.
	 */

	dprintk(DEB_OUT, (KERN_DEBUG "%s: send to %s via %s\n",
			  dev->name, cipe_ntoa(target), tdev->name));

#ifdef DEBUG
    if (cipe_debug&DEB_PKOU)
        cipe_dump_packet("sending", skb, 0);
#endif
	switch (ipforward(skb, dev, IPFWD_NOTTLDEC, target)) {
	case -1:
	    printk(KERN_DEBUG "%s: forwarding failed\n", dev->name);
	    /* fall thru */
	case 1:
	    dev_kfree_skb(skb, FREE_WRITE);
	    /* Does it really need dev_ here? I think so. */
	    break;
	default:
	    /* nothing to do */
	}

	/*
	 *	Clean up:  We're done with the route and the packet
	 */

	/* Record statistics and return */
	stats->tx_packets++;
	dev->tbusy=0;
	return 0;

    error:
	stats->tx_errors++;
	dev_kfree_skb(skb, FREE_WRITE);
	dev->tbusy=0;
	return 0;
}

#endif /* LINUX_21 */
