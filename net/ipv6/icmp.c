/*
 *	Internet Control Message Protocol (ICMPv6)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: icmp.c,v 1.37 2001/09/18 22:29:10 davem Exp $
 *
 *	Based on net/ipv4/icmp.c
 *
 *	RFC 1885
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Andi Kleen		:	exception handling
 *	Andi Kleen			add rate limits. never reply to a icmp.
 *					add more length checks and other fixes.
 *	yoshfuji		:	ensure to sent parameter problem for
 *					fragments.
 *	YOSHIFUJI Hideaki @USAGI:	added sysctl for icmp rate limit.
 *	Kazunori MIYAZAWA @USAGI:       change output process to use ip6_append_data
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>

#include <net/ip.h>
#include <net/sock.h>

#include <net/ipv6.h>
#include <net/checksum.h>
#include <net/protocol.h>
#include <net/raw.h>
#include <net/rawv6.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>

#include <asm/uaccess.h>
#include <asm/system.h>

struct icmpv6_mib icmpv6_statistics[NR_CPUS*2];

/*
 *	ICMP socket(s) for flow control.
 */

static struct socket *__icmpv6_socket[NR_CPUS];
#define icmpv6_socket	__icmpv6_socket[smp_processor_id()]
#define icmpv6_socket_cpu(X) __icmpv6_socket[(X)]

static int icmpv6_rcv(struct sk_buff **pskb, unsigned int *nhoffp);

static struct inet6_protocol icmpv6_protocol = {
	.handler	=	icmpv6_rcv,
	.flags		=	INET6_PROTO_FINAL,
};

static int icmpv6_xmit_lock(void)
{
	local_bh_disable();
	if (unlikely(!spin_trylock(&icmpv6_socket->sk->lock.slock))) {
		/* This can happen if the output path (f.e. SIT or
		 * ip6ip6 tunnel) signals dst_link_failure() for an
		 * outgoing ICMP6 packet.
		 */
		local_bh_enable();
		return 1;
	}
	return 0;
}

static void icmpv6_xmit_unlock(void)
{
	spin_unlock_bh(&icmpv6_socket->sk->lock.slock);
}

/* 
 * Slightly more convenient version of icmpv6_send.
 */
void icmpv6_param_prob(struct sk_buff *skb, int code, int pos)
{
	icmpv6_send(skb, ICMPV6_PARAMPROB, code, pos, skb->dev);
	kfree_skb(skb);
}

/*
 * Figure out, may we reply to this packet with icmp error.
 *
 * We do not reply, if:
 *	- it was icmp error message.
 *	- it is truncated, so that it is known, that protocol is ICMPV6
 *	  (i.e. in the middle of some exthdr)
 *
 *	--ANK (980726)
 */

static int is_ineligible(struct sk_buff *skb)
{
	int ptr = (u8*)(skb->nh.ipv6h+1) - skb->data;
	int len = skb->len - ptr;
	__u8 nexthdr = skb->nh.ipv6h->nexthdr;

	if (len < 0)
		return 1;

	ptr = ipv6_skip_exthdr(skb, ptr, &nexthdr, len);
	if (ptr < 0)
		return 0;
	if (nexthdr == IPPROTO_ICMPV6) {
		u8 type;
		if (skb_copy_bits(skb, ptr+offsetof(struct icmp6hdr, icmp6_type),
				  &type, 1)
		    || !(type & ICMPV6_INFOMSG_MASK))
			return 1;
	}
	return 0;
}

int sysctl_icmpv6_time = 1*HZ; 

/* 
 * Check the ICMP output rate limit 
 */
static inline int icmpv6_xrlim_allow(struct sock *sk, int type,
				     struct flowi *fl)
{
	struct dst_entry *dst;
	int res = 0;

	/* Informational messages are not limited. */
	if (type & ICMPV6_INFOMSG_MASK)
		return 1;

	/* Do not limit pmtu discovery, it would break it. */
	if (type == ICMPV6_PKT_TOOBIG)
		return 1;

	/* 
	 * Look up the output route.
	 * XXX: perhaps the expire for routing entries cloned by
	 * this lookup should be more aggressive (not longer than timeout).
	 */
	dst = ip6_route_output(sk, fl);
	if (dst->error) {
		IP6_INC_STATS(Ip6OutNoRoutes);
	} else if (dst->dev && (dst->dev->flags&IFF_LOOPBACK)) {
		res = 1;
	} else {
		struct rt6_info *rt = (struct rt6_info *)dst;
		int tmo = sysctl_icmpv6_time;

		/* Give more bandwidth to wider prefixes. */
		if (rt->rt6i_dst.plen < 128)
			tmo >>= ((128 - rt->rt6i_dst.plen)>>5);

		res = xrlim_allow(dst, tmo);
	}
	dst_release(dst);
	return res;
}

/*
 *	an inline helper for the "simple" if statement below
 *	checks if parameter problem report is caused by an
 *	unrecognized IPv6 option that has the Option Type 
 *	highest-order two bits set to 10
 */

static __inline__ int opt_unrec(struct sk_buff *skb, __u32 offset)
{
	u8 optval;

	offset += skb->nh.raw - skb->data;
	if (skb_copy_bits(skb, offset, &optval, 1))
		return 1;
	return (optval&0xC0) == 0x80;
}

int icmpv6_push_pending_frames(struct sock *sk, struct flowi *fl, struct icmp6hdr *thdr, int len)
{
	struct sk_buff *skb;
	struct icmp6hdr *icmp6h;
	int err = 0;

	if ((skb = skb_peek(&sk->write_queue)) == NULL)
		goto out;

	icmp6h = (struct icmp6hdr*) skb->h.raw;
	memcpy(icmp6h, thdr, sizeof(struct icmp6hdr));
	icmp6h->icmp6_cksum = 0;

	if (skb_queue_len(&sk->write_queue) == 1) {
		skb->csum = csum_partial((char *)icmp6h,
					sizeof(struct icmp6hdr), skb->csum);
		icmp6h->icmp6_cksum = csum_ipv6_magic(&fl->fl6_src,
						      &fl->fl6_dst,
						      len, fl->proto,
						      skb->csum);
	} else {
		u32 tmp_csum = 0;

		skb_queue_walk(&sk->write_queue, skb) {
			tmp_csum = csum_add(tmp_csum, skb->csum);
		}

		tmp_csum = csum_partial((char *)icmp6h,
					sizeof(struct icmp6hdr), tmp_csum);
		tmp_csum = csum_ipv6_magic(&fl->fl6_src,
					   &fl->fl6_dst,
					   len, fl->proto, tmp_csum);
		icmp6h->icmp6_cksum = tmp_csum;
	}
	if (icmp6h->icmp6_cksum == 0)
		icmp6h->icmp6_cksum = -1;
	ip6_push_pending_frames(sk);
out:
	return err;
}

struct icmpv6_msg {
	struct sk_buff	*skb;
	int		offset;
};

static int icmpv6_getfrag(void *from, char *to, int offset, int len, int odd, struct sk_buff *skb)
{
	struct icmpv6_msg *msg = (struct icmpv6_msg *) from;
	struct sk_buff *org_skb = msg->skb;
	__u32 csum = 0;

	csum = skb_copy_and_csum_bits(org_skb, msg->offset + offset,
				      to, len, csum);
	skb->csum = csum_block_add(skb->csum, csum, odd);
	return 0;
}

/*
 *	Send an ICMP message in response to a packet in error
 */
void icmpv6_send(struct sk_buff *skb, int type, int code, __u32 info, 
		 struct net_device *dev)
{
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct sock *sk = icmpv6_socket->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *saddr = NULL;
	struct dst_entry *dst;
	struct icmp6hdr tmp_hdr;
	struct flowi fl;
	struct icmpv6_msg msg;
	int iif = 0;
	int addr_type = 0;
	int len;
	int hlimit = -1;
	int err = 0;

	if ((u8*)hdr < skb->head || (u8*)(hdr+1) > skb->tail)
		return;

	/*
	 *	Make sure we respect the rules 
	 *	i.e. RFC 1885 2.4(e)
	 *	Rule (e.1) is enforced by not using icmpv6_send
	 *	in any code that processes icmp errors.
	 */
	addr_type = ipv6_addr_type(&hdr->daddr);

	if (ipv6_chk_addr(&hdr->daddr, skb->dev))
		saddr = &hdr->daddr;

	/*
	 *	Dest addr check
	 */

	if ((addr_type & IPV6_ADDR_MULTICAST || skb->pkt_type != PACKET_HOST)) {
		if (type != ICMPV6_PKT_TOOBIG &&
		    !(type == ICMPV6_PARAMPROB && 
		      code == ICMPV6_UNK_OPTION && 
		      (opt_unrec(skb, info))))
			return;

		saddr = NULL;
	}

	addr_type = ipv6_addr_type(&hdr->saddr);

	/*
	 *	Source addr check
	 */

	if (addr_type & IPV6_ADDR_LINKLOCAL)
		iif = skb->dev->ifindex;

	/*
	 *	Must not send if we know that source is Anycast also.
	 *	for now we don't know that.
	 */
	if ((addr_type == IPV6_ADDR_ANY) || (addr_type & IPV6_ADDR_MULTICAST)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "icmpv6_send: addr_any/mcast source\n");
		return;
	}

	/* 
	 *	Never answer to a ICMP packet.
	 */
	if (is_ineligible(skb)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "icmpv6_send: no reply to icmp error\n"); 
		return;
	}

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_dst, &hdr->saddr);
	if (saddr)
		ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.oif = iif;
	fl.fl_icmp_type = type;
	fl.fl_icmp_code = code;

	if (icmpv6_xmit_lock())
		return;

	if (!icmpv6_xrlim_allow(sk, type, &fl))
		goto out;

	tmp_hdr.icmp6_type = type;
	tmp_hdr.icmp6_code = code;
	tmp_hdr.icmp6_cksum = 0;
	tmp_hdr.icmp6_pointer = htonl(info);

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst))
		fl.oif = np->mcast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;

	if (hlimit < 0) {
		if (ipv6_addr_is_multicast(&fl.fl6_dst))
			hlimit = np->mcast_hops;
		else
			hlimit = np->hop_limit;
		if (hlimit < 0)
			hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	}

	msg.skb = skb;
	msg.offset = skb->nh.raw - skb->data;

	len = skb->len - msg.offset;
	len = min_t(unsigned int, len, IPV6_MIN_MTU - sizeof(struct ipv6hdr) -sizeof(struct icmp6hdr));
	if (len < 0) {
		if (net_ratelimit())
			printk(KERN_DEBUG "icmp: len problem\n");
		goto out_dst_release;
	}

	err = ip6_append_data(sk, icmpv6_getfrag, &msg,
			      len + sizeof(struct icmp6hdr),
			      sizeof(struct icmp6hdr),
			      hlimit, NULL, &fl, (struct rt6_info*)dst,
			      MSG_DONTWAIT);
	if (err) {
		ip6_flush_pending_frames(sk);
		goto out_dst_release;
	}
	err = icmpv6_push_pending_frames(sk, &fl, &tmp_hdr, len + sizeof(struct icmp6hdr));

	if (type >= ICMPV6_DEST_UNREACH && type <= ICMPV6_PARAMPROB)
		(&(icmpv6_statistics[smp_processor_id()*2].Icmp6OutDestUnreachs))[type-1]++;
	ICMP6_INC_STATS_BH(Icmp6OutMsgs);
out_dst_release:
	dst_release(dst);
out:
	icmpv6_xmit_unlock();
}

static void icmpv6_echo_reply(struct sk_buff *skb)
{
	struct sock *sk = icmpv6_socket->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *saddr = NULL;
	struct icmp6hdr *icmph = (struct icmp6hdr *) skb->h.raw;
	struct icmp6hdr tmp_hdr;
	struct flowi fl;
	struct icmpv6_msg msg;
	struct dst_entry *dst;
	int err = 0;
	int hlimit = -1;

	saddr = &skb->nh.ipv6h->daddr;

	if (!ipv6_unicast_destination(skb))
		saddr = NULL;

	memcpy(&tmp_hdr, icmph, sizeof(tmp_hdr));
	tmp_hdr.icmp6_type = ICMPV6_ECHO_REPLY;

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_dst, &skb->nh.ipv6h->saddr);
	if (saddr)
		ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.oif = skb->dev->ifindex;
	fl.fl_icmp_type = ICMPV6_ECHO_REPLY;

	if (icmpv6_xmit_lock())
		return;

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst))
		fl.oif = np->mcast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;

	if (hlimit < 0) {
		if (ipv6_addr_is_multicast(&fl.fl6_dst))
			hlimit = np->mcast_hops;
		else
			hlimit = np->hop_limit;
		if (hlimit < 0)
			hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	}

	msg.skb = skb;
	msg.offset = 0;

	err = ip6_append_data(sk, icmpv6_getfrag, &msg, skb->len + sizeof(struct icmp6hdr),
				sizeof(struct icmp6hdr), hlimit, NULL, &fl,
				(struct rt6_info*)dst, MSG_DONTWAIT);
  
	if (err) {
		ip6_flush_pending_frames(sk);
		goto out_dst_release;
	}
	err = icmpv6_push_pending_frames(sk, &fl, &tmp_hdr, skb->len + sizeof(struct icmp6hdr));

        ICMP6_INC_STATS_BH(Icmp6OutEchoReplies);
        ICMP6_INC_STATS_BH(Icmp6OutMsgs);

out_dst_release:
	dst_release(dst);
out: 
	icmpv6_xmit_unlock();
}

static void icmpv6_notify(struct sk_buff *skb, int type, int code, u32 info)
{
	struct in6_addr *saddr, *daddr;
	struct inet6_protocol *ipprot;
	struct sock *sk;
	int inner_offset;
	int hash;
	u8 nexthdr;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		return;

	nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
	if (ipv6_ext_hdr(nexthdr)) {
		/* now skip over extension headers */
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr, skb->len - sizeof(struct ipv6hdr));
		if (inner_offset<0)
			return;
	} else {
		inner_offset = sizeof(struct ipv6hdr);
	}

	/* Checkin header including 8 bytes of inner protocol header. */
	if (!pskb_may_pull(skb, inner_offset+8))
		return;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;

	/* BUGGG_FUTURE: we should try to parse exthdrs in this packet.
	   Without this we will not able f.e. to make source routed
	   pmtu discovery.
	   Corresponding argument (opt) to notifiers is already added.
	   --ANK (980726)
	 */

	hash = nexthdr & (MAX_INET_PROTOS - 1);

	ipprot = inet6_protos[hash];
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, NULL, type, code, inner_offset, info);

	read_lock(&raw_v6_lock);
	if ((sk = raw_v6_htable[hash]) != NULL) {
		while((sk = __raw_v6_lookup(sk, nexthdr, daddr, saddr))) {
			rawv6_err(sk, skb, NULL, type, code, inner_offset, info);
			sk = sk->next;
		}
	}
	read_unlock(&raw_v6_lock);
}
  
/*
 *	Handle icmp messages
 */

static int icmpv6_rcv(struct sk_buff **pskb, unsigned int *nhoffp)
{
	struct sk_buff *skb = *pskb;
	struct net_device *dev = skb->dev;
	struct in6_addr *saddr, *daddr;
	struct ipv6hdr *orig_hdr;
	struct icmp6hdr *hdr;
	int type;

	ICMP6_INC_STATS_BH(Icmp6InMsgs);

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;

	/* Perform checksum. */
	if (skb->ip_summed == CHECKSUM_HW) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (csum_ipv6_magic(saddr, daddr, skb->len, IPPROTO_ICMPV6,
				    skb->csum)) {
			if (net_ratelimit())
				printk(KERN_DEBUG "ICMPv6 hw checksum failed\n");
			skb->ip_summed = CHECKSUM_NONE;
		}
	}
	if (skb->ip_summed == CHECKSUM_NONE) {
		if (csum_ipv6_magic(saddr, daddr, skb->len, IPPROTO_ICMPV6,
				    skb_checksum(skb, 0, skb->len, 0))) {
			if (net_ratelimit())
				printk(KERN_DEBUG "ICMPv6 checksum failed [%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x > %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]\n",
				       ntohs(saddr->s6_addr16[0]),
				       ntohs(saddr->s6_addr16[1]),
				       ntohs(saddr->s6_addr16[2]),
				       ntohs(saddr->s6_addr16[3]),
				       ntohs(saddr->s6_addr16[4]),
				       ntohs(saddr->s6_addr16[5]),
				       ntohs(saddr->s6_addr16[6]),
				       ntohs(saddr->s6_addr16[7]),
				       ntohs(daddr->s6_addr16[0]),
				       ntohs(daddr->s6_addr16[1]),
				       ntohs(daddr->s6_addr16[2]),
				       ntohs(daddr->s6_addr16[3]),
				       ntohs(daddr->s6_addr16[4]),
				       ntohs(daddr->s6_addr16[5]),
				       ntohs(daddr->s6_addr16[6]),
				       ntohs(daddr->s6_addr16[7]));
			goto discard_it;
		}
	}

	if (!pskb_pull(skb, sizeof(struct icmp6hdr)))
		goto discard_it;

	hdr = (struct icmp6hdr *) skb->h.raw;

	type = hdr->icmp6_type;

	if (type >= ICMPV6_DEST_UNREACH && type <= ICMPV6_PARAMPROB)
		(&icmpv6_statistics[smp_processor_id()*2].Icmp6InDestUnreachs)[type-ICMPV6_DEST_UNREACH]++;
	else if (type >= ICMPV6_ECHO_REQUEST && type <= NDISC_REDIRECT)
		(&icmpv6_statistics[smp_processor_id()*2].Icmp6InEchos)[type-ICMPV6_ECHO_REQUEST]++;

	switch (type) {
	case ICMPV6_ECHO_REQUEST:
		icmpv6_echo_reply(skb);
		break;

	case ICMPV6_ECHO_REPLY:
		/* we coulnd't care less */
		break;

	case ICMPV6_PKT_TOOBIG:
		/* BUGGG_FUTURE: if packet contains rthdr, we cannot update
		   standard destination cache. Seems, only "advanced"
		   destination cache will allow to solve this problem
		   --ANK (980726)
		 */
		if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
			goto discard_it;
		hdr = (struct icmp6hdr *) skb->h.raw;
		orig_hdr = (struct ipv6hdr *) (hdr + 1);
		rt6_pmtu_discovery(&orig_hdr->daddr, &orig_hdr->saddr, dev,
				   ntohl(hdr->icmp6_mtu));

		/*
		 *	Drop through to notify
		 */

	case ICMPV6_DEST_UNREACH:
	case ICMPV6_TIME_EXCEED:
	case ICMPV6_PARAMPROB:
		icmpv6_notify(skb, type, hdr->icmp6_code, hdr->icmp6_mtu);
		break;

	case NDISC_ROUTER_SOLICITATION:
	case NDISC_ROUTER_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
	case NDISC_REDIRECT:
		if (skb_is_nonlinear(skb) &&
		    skb_linearize(skb, GFP_ATOMIC) != 0) {
			kfree_skb(skb);
			return 0;
		}

		ndisc_rcv(skb);
		break;

	case ICMPV6_MGM_QUERY:
		igmp6_event_query(skb);
		break;

	case ICMPV6_MGM_REPORT:
		igmp6_event_report(skb);
		break;

	case ICMPV6_MGM_REDUCTION:
		break;

	default:
		if (net_ratelimit())
			printk(KERN_DEBUG "icmpv6: msg of unkown type\n");

		/* informational */
		if (type & ICMPV6_INFOMSG_MASK)
			break;

		/* 
		 * error of unkown type. 
		 * must pass to upper level 
		 */

		icmpv6_notify(skb, type, hdr->icmp6_code, hdr->icmp6_mtu);
	};
	kfree_skb(skb);
	return 0;

discard_it:
	ICMP6_INC_STATS_BH(Icmp6InErrors);
	kfree_skb(skb);
	return 0;
}

int __init icmpv6_init(struct net_proto_family *ops)
{
	struct sock *sk;
	int err, i, j;

	for (i = 0; i < NR_CPUS; i++) {
		icmpv6_socket_cpu(i) = sock_alloc();
		if (icmpv6_socket_cpu(i) == NULL) {
			printk(KERN_ERR
			       "Failed to create the ICMP6 control socket.\n");
			err = -1;
			goto fail;
		}
		icmpv6_socket_cpu(i)->inode->i_uid = 0;
		icmpv6_socket_cpu(i)->inode->i_gid = 0;
		icmpv6_socket_cpu(i)->type = SOCK_RAW;

		if ((err = ops->create(icmpv6_socket_cpu(i), IPPROTO_ICMPV6)) < 0) {
			printk(KERN_ERR
			       "Failed to initialize the ICMP6 control socket "
			       "(err %d).\n",
			       err);
			goto fail;
		}

		sk = icmpv6_socket_cpu(i)->sk;
		sk->allocation = GFP_ATOMIC;
		sk->sndbuf = SK_WMEM_MAX*2;
		sk->prot->unhash(sk);
	}

	if (inet6_add_protocol(&icmpv6_protocol, IPPROTO_ICMPV6) < 0) {
		printk(KERN_ERR "Failed to register ICMP6 protocol\n");
		sock_release(icmpv6_socket);
		icmpv6_socket = NULL;
		return -EAGAIN;
	}

	return 0;
fail:
	for (j = 0; j < i; j++) {
		sock_release(icmpv6_socket_cpu(j));
		icmpv6_socket_cpu(j) = NULL;
	}
	return err;
}

void icmpv6_cleanup(void)
{
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		sock_release(icmpv6_socket_cpu(i));
		icmpv6_socket_cpu(i) = NULL;
	}
	inet6_del_protocol(&icmpv6_protocol, IPPROTO_ICMPV6);
}

static struct icmp6_err {
	int err;
	int fatal;
} tab_unreach[] = {
	{ ENETUNREACH,	0},	/* NOROUTE		*/
	{ EACCES,	1},	/* ADM_PROHIBITED	*/
	{ EHOSTUNREACH,	0},	/* Was NOT_NEIGHBOUR, now reserved */
	{ EHOSTUNREACH,	0},	/* ADDR_UNREACH		*/
	{ ECONNREFUSED,	1},	/* PORT_UNREACH		*/
};

int icmpv6_err_convert(int type, int code, int *err)
{
	int fatal = 0;

	*err = EPROTO;

	switch (type) {
	case ICMPV6_DEST_UNREACH:
		fatal = 1;
		if (code <= ICMPV6_PORT_UNREACH) {
			*err  = tab_unreach[code].err;
			fatal = tab_unreach[code].fatal;
		}
		break;

	case ICMPV6_PKT_TOOBIG:
		*err = EMSGSIZE;
		break;
		
	case ICMPV6_PARAMPROB:
		*err = EPROTO;
		fatal = 1;
		break;

	case ICMPV6_TIME_EXCEED:
		*err = EHOSTUNREACH;
		break;
	};

	return fatal;
}

#ifdef CONFIG_SYSCTL
ctl_table ipv6_icmp_table[] = {
	{NET_IPV6_ICMP_RATELIMIT, "ratelimit",
	&sysctl_icmpv6_time, sizeof(int), 0644, NULL, &proc_dointvec},
	{0},
};
#endif

