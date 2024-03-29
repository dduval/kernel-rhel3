/*
 * IP Payload Compression Protocol (IPComp) - RFC3173.
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * Todo:
 *   - Tunable compression parameters.
 *   - Compression stats.
 *   - Adaptive compression.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>
#include <linux/pfkeyv2.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>
#include <net/ipcomp.h>

static int ipcomp_decompress(struct xfrm_state *x, struct sk_buff *skb)
{
	int err, plen, dlen;
	struct iphdr *iph;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch = ipcd->scratch;
	
	plen = skb->len;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data;

	err = crypto_comp_decompress(ipcd->tfm, start, plen, scratch, &dlen);
	if (err)
		goto out;

	if (dlen < (plen + sizeof(struct ip_comp_hdr))) {
		err = -EINVAL;
		goto out;
	}

	err = pskb_expand_head(skb, 0, dlen - plen, GFP_ATOMIC);
	if (err)
		goto out;
		
	skb_put(skb, dlen - plen);
	memcpy(skb->data, scratch, dlen);
	iph = skb->nh.iph;
	iph->tot_len = htons(dlen + iph->ihl * 4);
out:	
	return err;
}

static int ipcomp_input(struct xfrm_state *x,
                        struct xfrm_decap_state *decap, struct sk_buff *skb)
{
	u8 nexthdr;
	int err = 0;
	struct iphdr *iph;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;


	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
	    skb_linearize(skb, GFP_ATOMIC) != 0) {
	    	err = -ENOMEM;
	    	goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	/* Remove ipcomp header and decompress original payload */	
	iph = skb->nh.iph;
	memcpy(&tmp_iph, iph, iph->ihl * 4);
	nexthdr = *(u8 *)skb->data;
	skb_pull(skb, sizeof(struct ip_comp_hdr));
	skb->nh.raw += sizeof(struct ip_comp_hdr);
	memcpy(skb->nh.raw, &tmp_iph, tmp_iph.iph.ihl * 4);
	iph = skb->nh.iph;
	iph->tot_len = htons(ntohs(iph->tot_len) - sizeof(struct ip_comp_hdr));
	iph->protocol = nexthdr;
	skb->h.raw = skb->data;
	err = ipcomp_decompress(x, skb);

out:	
	return err;
}

static int ipcomp_compress(struct xfrm_state *x, struct sk_buff *skb)
{
	int err, plen, dlen, ihlen;
	struct iphdr *iph = skb->nh.iph;
	struct ipcomp_data *ipcd = x->data;
	u8 *start, *scratch = ipcd->scratch;
	
	ihlen = iph->ihl * 4;
	plen = skb->len - ihlen;
	dlen = IPCOMP_SCRATCH_SIZE;
	start = skb->data + ihlen;

	err = crypto_comp_compress(ipcd->tfm, start, plen, scratch, &dlen);
	if (err)
		goto out;

	if ((dlen + sizeof(struct ip_comp_hdr)) >= plen) {
		err = -EMSGSIZE;
		goto out;
	}
	
	memcpy(start, scratch, dlen);
	pskb_trim(skb, ihlen + dlen);
	
out:	
	return err;
}

static void ipcomp_tunnel_encap(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct iphdr *iph, *top_iph;

	iph = skb->nh.iph;
	top_iph = (struct iphdr *)skb_push(skb, sizeof(struct iphdr));
	top_iph->ihl = 5;
	top_iph->version = 4;
	top_iph->tos = iph->tos;
	top_iph->tot_len = htons(skb->len);
	if (!(iph->frag_off&htons(IP_DF))) {
#ifdef NETIF_F_TSO
		__ip_select_ident(top_iph, dst, 0);
#else
		__ip_select_ident(top_iph, dst);
#endif
	}
	top_iph->ttl = iph->ttl;
	top_iph->check = 0;
	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	top_iph->frag_off = iph->frag_off&~htons(IP_MF|IP_OFFSET);
	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	skb->nh.raw = skb->data;
}

static int ipcomp_output(struct sk_buff *skb)
{
	int err;
	struct dst_entry *dst = skb->dst;
	struct xfrm_state *x = dst->xfrm;
	struct iphdr *iph, *top_iph;
	struct ip_comp_hdr *ipch;
	struct ipcomp_data *ipcd = x->data;
	union {
		struct iphdr	iph;
		char 		buf[60];
	} tmp_iph;
	int hdr_len = 0;

	if (skb->ip_summed == CHECKSUM_HW && skb_checksum_help(skb) == NULL) {
		err = -EINVAL;
		goto error_nolock;
	}

	spin_lock_bh(&x->lock);
	err = xfrm_check_output(x, skb, AF_INET);
	if (err)
		goto error;

	/* Don't bother compressing */
	if (!x->props.mode) {
		iph = skb->nh.iph;
		hdr_len = iph->ihl * 4;
	}
	if ((skb->len - hdr_len) < ipcd->threshold) {
		if (x->props.mode) {
			ipcomp_tunnel_encap(x, skb);
			iph = skb->nh.iph;
			iph->protocol = IPPROTO_IPIP;
			ip_send_check(iph);
		}
		goto out_ok;
	}

	if (x->props.mode) 
		ipcomp_tunnel_encap(x, skb);

	if ((skb_is_nonlinear(skb) || skb_cloned(skb)) &&
	    skb_linearize(skb, GFP_ATOMIC) != 0) {
	    	err = -ENOMEM;
	    	goto error;
	}
	
	err = ipcomp_compress(x, skb);
	if (err) {
		if (err == -EMSGSIZE) {
			if (x->props.mode) {
				iph = skb->nh.iph;
				iph->protocol = IPPROTO_IPIP;
				ip_send_check(iph);
			}
			goto out_ok;
		}
		goto error;
	}

	/* Install ipcomp header, convert into ipcomp datagram. */
	iph = skb->nh.iph;
	memcpy(&tmp_iph, iph, iph->ihl * 4);
	top_iph = (struct iphdr *)skb_push(skb, sizeof(struct ip_comp_hdr));
	memcpy(top_iph, &tmp_iph, iph->ihl * 4);
	iph = top_iph;
	if (x->props.mode && (x->props.flags & XFRM_STATE_NOECN))
		IP_ECN_clear(iph);
	iph->tot_len = htons(skb->len);
	iph->protocol = IPPROTO_COMP;
	iph->check = 0;
	ipch = (struct ip_comp_hdr *)((char *)iph + iph->ihl * 4);
	ipch->nexthdr = x->props.mode ? IPPROTO_IPIP : tmp_iph.iph.protocol;
	ipch->flags = 0;
	ipch->cpi = htons((u16 )ntohl(x->id.spi));
	ip_send_check(iph);
	skb->nh.raw = skb->data;

out_ok:
	x->curlft.bytes += skb->len;
	x->curlft.packets++;
	spin_unlock_bh(&x->lock);
	
	if ((skb->dst = dst_pop(dst)) == NULL) {
		err = -EHOSTUNREACH;
		goto error_nolock;
	}
	err = NET_XMIT_BYPASS;

out_exit:
	return err;
error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
	goto out_exit;
}

static void ipcomp4_err(struct sk_buff *skb, u32 info)
{
	u32 spi;
	struct iphdr *iph = (struct iphdr *)skb->data;
	struct ip_comp_hdr *ipch = (struct ip_comp_hdr *)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	if (skb->h.icmph->type != ICMP_DEST_UNREACH ||
	    skb->h.icmph->code != ICMP_FRAG_NEEDED)
		return;

	spi = ntohl(ntohs(ipch->cpi));
	x = xfrm_state_lookup((xfrm_address_t *)&iph->daddr,
	                      spi, IPPROTO_COMP, AF_INET);
	if (!x)
		return;
	printk(KERN_DEBUG "pmtu discovery on SA IPCOMP/%08x/%u.%u.%u.%u\n",
	       spi, NIPQUAD(iph->daddr));
	xfrm_state_put(x);
}

/* We always hold one tunnel user reference to indicate a tunnel */ 
static struct xfrm_state *ipcomp_tunnel_create(struct xfrm_state *x)
{
	struct xfrm_state *t;
	
	t = xfrm_state_alloc();
	if (t == NULL)
		goto out;

	t->id.proto = IPPROTO_IPIP;
	t->id.spi = x->props.saddr.a4;
	t->id.daddr.a4 = x->id.daddr.a4;
	memcpy(&t->sel, &x->sel, sizeof(t->sel));
	t->props.family = AF_INET;
	t->props.mode = 1;
	t->props.saddr.a4 = x->props.saddr.a4;
	t->props.flags = x->props.flags;
	
	t->type = xfrm_get_type(IPPROTO_IPIP, t->props.family);
	if (t->type == NULL)
		goto error;
		
	if (t->type->init_state(t, NULL))
		goto error;

	t->km.state = XFRM_STATE_VALID;
	atomic_set(&t->tunnel_users, 1);
out:
	return t;

error:
	xfrm_state_put(t);
	t = NULL;
	goto out;
}

/*
 * Must be protected by xfrm_cfg_sem.  State and tunnel user references are
 * always incremented on success.
 */
static int ipcomp_tunnel_attach(struct xfrm_state *x)
{
	int err = 0;
	struct xfrm_state *t;

	t = xfrm_state_lookup((xfrm_address_t *)&x->id.daddr.a4,
	                      x->props.saddr.a4, IPPROTO_IPIP, AF_INET);
	if (!t) {
		t = ipcomp_tunnel_create(x);
		if (!t) {
			err = -EINVAL;
			goto out;
		}
		xfrm_state_insert(t);
		xfrm_state_hold(t);
	}
	x->tunnel = t;
	atomic_inc(&t->tunnel_users);
out:
	return err;
}

static void ipcomp_free_data(struct ipcomp_data *ipcd)
{
	if (ipcd->tfm)
		crypto_free_tfm(ipcd->tfm);
	if (ipcd->scratch)
		kfree(ipcd->scratch);	
}

static void ipcomp_destroy(struct xfrm_state *x)
{
	struct ipcomp_data *ipcd = x->data;
	if (!ipcd)
		return;
	xfrm_state_delete_tunnel(x);
	ipcomp_free_data(ipcd);
	kfree(ipcd);
}

static int ipcomp_init_state(struct xfrm_state *x, void *args)
{
	int err = -ENOMEM;
	struct ipcomp_data *ipcd;
	struct xfrm_algo_desc *calg_desc;

	ipcd = kmalloc(sizeof(*ipcd), GFP_KERNEL);
	if (!ipcd)
		goto error;

	memset(ipcd, 0, sizeof(*ipcd));
	x->props.header_len = sizeof(struct ip_comp_hdr);
	if (x->props.mode)
		x->props.header_len += sizeof(struct iphdr);

	ipcd->scratch = kmalloc(IPCOMP_SCRATCH_SIZE, GFP_KERNEL);
	if (!ipcd->scratch)
		goto error;
	
	ipcd->tfm = crypto_alloc_tfm(x->calg->alg_name, 0);
	if (!ipcd->tfm)
		goto error;

	if (x->props.mode) {
		err = ipcomp_tunnel_attach(x);
		if (err)
			goto error;
	}

	calg_desc = xfrm_calg_get_byname(x->calg->alg_name);
	BUG_ON(!calg_desc);
	ipcd->threshold = calg_desc->uinfo.comp.threshold;
	x->data = ipcd;
	err = 0;
out:
	return err;

error:
	if (ipcd) {
		ipcomp_free_data(ipcd);
		kfree(ipcd);
	}
	goto out;
}

static struct xfrm_type ipcomp_type =
{
	.description	= "IPCOMP4",
	.proto	     	= IPPROTO_COMP,
	.init_state	= ipcomp_init_state,
	.destructor	= ipcomp_destroy,
	.input		= ipcomp_input,
	.output		= ipcomp_output
};

static struct inet_protocol ipcomp4_protocol = {
	.handler	=	xfrm4_rcv,
	.err_handler	=	ipcomp4_err,
	.no_policy	=	1,
};

static int __init ipcomp4_init(void)
{
	SET_MODULE_OWNER(&ipcomp_type);
	if (xfrm_register_type(&ipcomp_type, AF_INET) < 0) {
		printk(KERN_INFO "ipcomp init: can't add xfrm type\n");
		return -EAGAIN;
	}
	if (inet_add_protocol(&ipcomp4_protocol, IPPROTO_COMP) < 0) {
		printk(KERN_INFO "ipcomp init: can't add protocol\n");
		xfrm_unregister_type(&ipcomp_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit ipcomp4_fini(void)
{
	if (inet_del_protocol(&ipcomp4_protocol, IPPROTO_COMP) < 0)
		printk(KERN_INFO "ip ipcomp close: can't remove protocol\n");
	if (xfrm_unregister_type(&ipcomp_type, AF_INET) < 0)
		printk(KERN_INFO "ip ipcomp close: can't remove xfrm type\n");
}

module_init(ipcomp4_init);
module_exit(ipcomp4_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IP Payload Compression Protocol (IPComp) - RFC3173");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");

