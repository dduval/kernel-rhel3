/*
 *  linux/drivers/net/netconsole.c
 *
 *  Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2002  Red Hat, Inc.
 *
 *  This file contains the implementation of an IRQ-safe, crash-safe
 *  kernel console implementation that outputs kernel messages to the
 *  network.
 *
 * Modification history:
 *
 * 2001-09-17    started by Ingo Molnar.
 * 2002-03-14    simultaneous syslog packet option by Michael K. Johnson
 */

/****************************************************************
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

#include <net/tcp.h>
#include <net/udp.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <asm/pgtable.h>
#if CONFIG_X86_LOCAL_APIC
#include <asm/apic.h>
#endif
#include <linux/console.h>
#include <linux/smp_lock.h>
#include <linux/netdevice.h>
#include <linux/tty_driver.h>
#include <linux/etherdevice.h>
#include <linux/elf.h>
#include <linux/nmi.h>

static struct net_device *netconsole_dev;
static u16 source_port, netdump_target_port, netlog_target_port, syslog_target_port;
static u32 source_ip, netdump_target_ip, netlog_target_ip, syslog_target_ip;
static unsigned char netdump_daddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} ;
static unsigned char netlog_daddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} ;
static unsigned char syslog_daddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} ;

static unsigned int mhz = 1000, idle_timeout;
static unsigned long long mhz_cycles, jiffy_cycles;

#include "netconsole.h"
#include <asm/netdump.h>

#define MAX_UDP_CHUNK 1460
#define MAX_PRINT_CHUNK (MAX_UDP_CHUNK-HEADER_LEN)

#define DEBUG 0
#if DEBUG
# define Dprintk(x...) printk(KERN_INFO x)
#else
# define Dprintk(x...)
#endif
/*
 * We maintain a small pool of fully-sized skbs,
 * to make sure the message gets out even in
 * extreme OOM situations.
 */
#define MAX_NETCONSOLE_SKBS 128

#define MAX_NETCONSOLE_TX_RETRIES 2500

static spinlock_t netconsole_lock = SPIN_LOCK_UNLOCKED;
static int nr_netconsole_skbs;
static struct sk_buff *netconsole_skbs;
/* the following tunes whether we should send printk's via netconsole.  The
 * default is to on.  During a netdump, though, we don't want to send output
 * to the network console, so we disable it for that time.
 */
static int emit_printks = 1;
static asmlinkage void do_netdump(struct pt_regs *regs, void *arg);

#define MAX_SKB_SIZE \
		(MAX_UDP_CHUNK + sizeof(struct udphdr) + \
				sizeof(struct iphdr) + sizeof(struct ethhdr))

static int new_arp = 0;
static unsigned char arp_sha[ETH_ALEN], arp_tha[ETH_ALEN];
static u32 arp_sip, arp_tip;

static void send_netconsole_arp(struct net_device *dev);

static void __refill_netconsole_skbs(void)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&netconsole_lock, flags);
	while (nr_netconsole_skbs < MAX_NETCONSOLE_SKBS) {
		skb = alloc_skb(MAX_SKB_SIZE, GFP_ATOMIC);
		if (!skb)
			break;
		if (netconsole_skbs)
			skb->next = netconsole_skbs;
		else
			skb->next = NULL;
		netconsole_skbs = skb;
		nr_netconsole_skbs++;
	}
	spin_unlock_irqrestore(&netconsole_lock, flags);
}

static struct sk_buff * get_netconsole_skb(void)
{
	struct sk_buff *skb;

	unsigned long flags;

	spin_lock_irqsave(&netconsole_lock, flags);
	skb = netconsole_skbs;
	if (skb) {
		netconsole_skbs = skb->next;
		skb->next = NULL;
		nr_netconsole_skbs--;
	}
	spin_unlock_irqrestore(&netconsole_lock, flags);

	return skb;
}

static unsigned long long t0;

/*
 * Do cleanups:
 * - zap completed output skbs.
 * - send ARPs if requested
 * - reboot the box if inactive for more than N seconds.
 */
static void zap_completion_queue(void)
{
	unsigned long long t1;
	int cpu = smp_processor_id();

	if (softnet_data[cpu].completion_queue) {
		struct sk_buff *clist;
		unsigned long flags;

		local_irq_save(flags);
		clist = softnet_data[cpu].completion_queue;
		softnet_data[cpu].completion_queue = NULL;
		local_irq_restore(flags);

		while (clist != NULL) {
			struct sk_buff *skb = clist;
			clist = clist->next;
			__kfree_skb(skb);
		}
	}

	if (new_arp) {
		Dprintk("got ARP req - sending reply.\n");
		new_arp = 0;
		send_netconsole_arp(netconsole_dev);
	}

	platform_timestamp(t1);

	if (idle_timeout) {
		if (t0) {
			if (((t1 - t0) >> 20) > mhz_cycles * (unsigned long long)idle_timeout) {
				t0 = t1;
				printk("netdump idle timeout - rebooting in 3 seconds.\n");
				mdelay(3000);
				machine_restart(NULL);
			}
		}
	}
	/* maintain jiffies in a polling fashion, based on rdtsc. */
	if (netdump_mode) {
		static unsigned long long prev_tick;

		if (t1 - prev_tick >= jiffy_cycles) {
			prev_tick = t1;
			jiffies++;
		}
	}

	/* update alert counters for this and other cpus */
	touch_nmi_watchdog();
}

static struct sk_buff * alloc_netconsole_skb(struct net_device *dev, int len, int reserve)
{
	int once = 1;
	int count = 0;
	struct sk_buff *skb = NULL;

repeat:
	zap_completion_queue();
	if (nr_netconsole_skbs < MAX_NETCONSOLE_SKBS)
		__refill_netconsole_skbs();

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		skb = get_netconsole_skb();
		if (!skb) {
			count++;
			if (once && (count == 1000000)) {
				printk("possibly FATAL: out of netconsole skbs!!! will keep retrying.\n");
				once = 0;
			}
			Dprintk("alloc skb: polling controller ...\n");
			if (dev->poll_controller)
				dev->poll_controller(dev);
			goto repeat;
		}
	}

	atomic_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static void transmit_raw_skb(struct sk_buff *skb, struct net_device *dev)
{
	int poll_count = 0;

repeat_poll:
	/* drop the packet if we are making no progress */
	if (likely(!netdump_mode) && 
	    poll_count++ > MAX_NETCONSOLE_TX_RETRIES) {
		dev_kfree_skb_any(skb);
		return;
	}

	spin_lock(&dev->xmit_lock);
	dev->xmit_lock_owner = smp_processor_id();

	if (netif_queue_stopped(dev)) {
		dev->xmit_lock_owner = -1;
		spin_unlock(&dev->xmit_lock);

		Dprintk("xmit skb: polling controller ...\n");
		if (dev->poll_controller)
			dev->poll_controller(dev);
		zap_completion_queue();
		udelay(50);
		goto repeat_poll;
	}

	/* tell the hard_start_xmit routine that we got here via netconsole */
	dev->priv_flags |= IFF_NETCONSOLE;

	dev->hard_start_xmit(skb, dev);

	dev->priv_flags &= ~IFF_NETCONSOLE;
	dev->xmit_lock_owner = -1;
	spin_unlock(&dev->xmit_lock);
}

static void transmit_netconsole_skb(struct sk_buff *skb, struct net_device *dev,
	int ip_len, int udp_len,
	u16 source_port, u16 target_port, u32 source_ip, u32 target_ip,
	unsigned char * macdaddr)
{
	struct udphdr *udph;
	struct iphdr *iph;
	struct ethhdr *eth;

	udph = (struct udphdr *) skb_push(skb, sizeof(*udph));
	udph->source = source_port;
	udph->dest = target_port;
	udph->len = htons(udp_len);
	udph->check = 0;

	iph = (struct iphdr *)skb_push(skb, sizeof(*iph));
	skb->nh.iph   = iph;

/*	iph->version  = 4; iph->ihl      = 5; */
        put_unaligned(0x45, (unsigned char *)iph);
	iph->tos      = 0;
	iph->tot_len  = htons(ip_len);
	iph->id       = 0;
	iph->frag_off = 0;
	iph->ttl      = 64;
	iph->protocol = IPPROTO_UDP;
	iph->check    = 0;
        put_unaligned(source_ip, &(iph->saddr));
        put_unaligned(target_ip, &(iph->daddr));
	iph->check    = ip_fast_csum((unsigned char *)iph, 5);

	eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);

	eth->h_proto = htons(ETH_P_IP);
	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, macdaddr, dev->addr_len);

	transmit_raw_skb(skb, dev);
}

static void send_netconsole_arp(struct net_device *dev)
{
	int total_len, arp_len, arp_data_len;
	struct sk_buff *skb;
	unsigned char *arp;
	struct arphdr *arph;
	struct ethhdr *eth;

	arp_data_len = 2*4 + 2*ETH_ALEN;
	arp_len = arp_data_len + sizeof(struct arphdr);
	total_len = arp_len + ETH_HLEN;

	skb = alloc_netconsole_skb(dev, total_len, total_len - arp_data_len);

	arp = skb->data;

	memcpy(arp, dev->dev_addr, ETH_ALEN);
	arp += ETH_ALEN;

	memcpy(arp, &source_ip, 4);
	arp += 4;

	memcpy(arp, arp_sha, ETH_ALEN);
	arp += ETH_ALEN;

	memcpy(arp, &arp_sip, 4);
	arp += 4;

	skb->len += 2*4 + 2*ETH_ALEN;

	arph = (struct arphdr *)skb_push(skb, sizeof(*arph));
	skb->nh.arph = arph;

	arph->ar_hrd = htons(dev->type);
	arph->ar_pro = __constant_htons(ETH_P_IP);
	arph->ar_hln = ETH_ALEN;
	arph->ar_pln = 4;
	arph->ar_op = __constant_htons(ARPOP_REPLY);

	eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);

	eth->h_proto = htons(ETH_P_ARP);
	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest, arp_sha, dev->addr_len);

	transmit_raw_skb(skb, dev);
}

static void send_netdump_skb(struct net_device *dev, const char *msg, unsigned int msg_len, reply_t *reply)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;

	udp_len = msg_len + HEADER_LEN + sizeof(struct udphdr);
	ip_len = udp_len + sizeof(struct iphdr);
	total_len = ip_len + ETH_HLEN;

	skb = alloc_netconsole_skb(dev, total_len, total_len - msg_len - HEADER_LEN);

	skb->data[0] = NETCONSOLE_VERSION;
	put_unaligned(htonl(reply->nr), (u32 *) (skb->data + 1));
	put_unaligned(htonl(reply->code), (u32 *) (skb->data + 5));
	put_unaligned(htonl(reply->info), (u32 *) (skb->data + 9));

	memcpy(skb->data + HEADER_LEN, msg, msg_len);
	skb->len += msg_len + HEADER_LEN;

	transmit_netconsole_skb(skb, dev, ip_len, udp_len,
		source_port, netdump_target_port, source_ip, netdump_target_ip, netdump_daddr);
}

#define SYSLOG_HEADER_LEN 4

static void send_netlog_skb(struct net_device *dev, const char *msg, unsigned int msg_len, reply_t *reply)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;

	udp_len = msg_len + HEADER_LEN + sizeof(struct udphdr);
	ip_len = udp_len + sizeof(struct iphdr);
	total_len = ip_len + ETH_HLEN;

	skb = alloc_netconsole_skb(dev, total_len, total_len - msg_len - HEADER_LEN);

	skb->data[0] = NETCONSOLE_VERSION;
	put_unaligned(htonl(reply->nr), (u32 *) (skb->data + 1));
	put_unaligned(htonl(reply->code), (u32 *) (skb->data + 5));
	put_unaligned(htonl(reply->info), (u32 *) (skb->data + 9));

	memcpy(skb->data + HEADER_LEN, msg, msg_len);
	skb->len += msg_len + HEADER_LEN;

	transmit_netconsole_skb(skb, dev, ip_len, udp_len,
		source_port, netlog_target_port, source_ip, netlog_target_ip, netlog_daddr);
}

#define SYSLOG_HEADER_LEN 4

static void send_syslog_skb(struct net_device *dev, const char *msg, unsigned int msg_len, int pri)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;

	udp_len = msg_len + SYSLOG_HEADER_LEN + sizeof(struct udphdr);
	ip_len = udp_len + sizeof(struct iphdr);
	total_len = ip_len + ETH_HLEN;

	skb = alloc_netconsole_skb(dev, total_len, total_len - msg_len - SYSLOG_HEADER_LEN);

	skb->data[0] = '<';
	skb->data[1] = pri + '0';
	skb->data[2]= '>';
	skb->data[3]= ' ';

	memcpy(skb->data + SYSLOG_HEADER_LEN, msg, msg_len);
	skb->len += msg_len + SYSLOG_HEADER_LEN;

	transmit_netconsole_skb(skb, dev, ip_len, udp_len, source_port,
		syslog_target_port, source_ip, syslog_target_ip, syslog_daddr);
}

#define MAX_SYSLOG_CHARS 1000

static spinlock_t syslog_lock = SPIN_LOCK_UNLOCKED;
static int syslog_chars;
static unsigned char syslog_line [MAX_SYSLOG_CHARS + 10];

/*
 * We feed kernel messages char by char, and send the UDP packet
 * one linefeed. We buffer all characters received.
 */
static inline void feed_syslog_char(struct net_device *dev, const unsigned char c)
{
	if (syslog_chars == MAX_SYSLOG_CHARS)
		syslog_chars--;
	syslog_line[syslog_chars] = c;
	syslog_chars++;
	if (c == '\n') {
		send_syslog_skb(dev, syslog_line, syslog_chars, 5);
		syslog_chars = 0;
	}
}

static spinlock_t sequence_lock = SPIN_LOCK_UNLOCKED;
static unsigned int log_offset;

static void write_netconsole_msg(struct console *con, const char *msg0, unsigned int msg_len)
{
	int len, left, i;
	struct net_device *dev;
	const char *msg = msg0;
	reply_t reply;

	dev = netconsole_dev;
	if (!dev || !emit_printks)
		return;

	if (dev->poll_controller && netif_running(dev)) {
		unsigned long flags;

		__save_flags(flags);
		__cli();
		left = msg_len;
		if (netlog_target_ip) {
			while (left) {
				if (left > MAX_PRINT_CHUNK)
					len = MAX_PRINT_CHUNK;
				else
					len = left;
				reply.code = REPLY_LOG;
				reply.nr = 0;
				spin_lock(&sequence_lock);
				reply.info = log_offset;
				log_offset += len;
				spin_unlock(&sequence_lock);
				send_netlog_skb(dev, msg, len, &reply);
				msg += len;
				left -= len;
			}
		}
		if (syslog_target_ip) {
			spin_lock(&syslog_lock);
			for (i = 0; i < msg_len; i++)
				feed_syslog_char(dev, msg0[i]);
			spin_unlock(&syslog_lock);
		}

		__restore_flags(flags);
	}
}

static unsigned short udp_check(struct udphdr *uh, int len, unsigned long saddr, unsigned long daddr, unsigned long base)
{
	return(csum_tcpudp_magic(saddr, daddr, len, IPPROTO_UDP, base));
}

static int udp_checksum_init(struct sk_buff *skb, struct udphdr *uh,
			     unsigned short ulen, u32 saddr, u32 daddr)
{
	if (uh->check == 0) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (skb->ip_summed == CHECKSUM_HW) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (!udp_check(uh, ulen, saddr, daddr, skb->csum))
			return 0;
		skb->ip_summed = CHECKSUM_NONE;
	}
	if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = csum_tcpudp_nofold(saddr, daddr, ulen, IPPROTO_UDP,
0);
	/* Probably, we should checksum udp header (it should be in cache
	 * in any case) and data in tiny packets (< rx copybreak).
	 */
	return 0;
}

static __inline__ int __udp_checksum_complete(struct sk_buff *skb)
{
	return (unsigned short)csum_fold(skb_checksum(skb, 0, skb->len, skb->csum));
}

static __inline__ int udp_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__udp_checksum_complete(skb);
}

/*
 * NOTE: security depends on the trusted path between the netconsole
 *       server and netconsole client, since none of the packets are
 *       encrypted. The random magic number protects the protocol
 *       against spoofing.
 */
static u64 netconsole_magic;
static u32 magic1, magic2;

static spinlock_t req_lock = SPIN_LOCK_UNLOCKED;
static int nr_req = 0;
static LIST_HEAD(request_list);

static void add_new_req(req_t *req)
{
	unsigned long flags;

	spin_lock_irqsave(&req_lock, flags);
	list_add_tail(&req->list, &request_list);
	nr_req++;
	Dprintk("pending requests: %d.\n", nr_req);
	spin_unlock_irqrestore(&req_lock, flags);

	platform_timestamp(t0);
}

static req_t *get_new_req(void)
{
	req_t *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&req_lock, flags);
	if (nr_req) {
		req = list_entry(request_list.next, req_t, list);
		list_del(&req->list);
		nr_req--;
	}
	spin_unlock_irqrestore(&req_lock, flags);

	return req;
}

static req_t *alloc_req(void)
{
	req_t *req;

	req = (req_t *) kmalloc(sizeof(*req), GFP_ATOMIC);
	return req;
}

int netconsole_rx(struct sk_buff *skb)
{
	int proto;
	struct iphdr *iph;
	struct udphdr *uh;
	__u32 len, saddr, daddr, ulen;
	req_t *__req;
	req_t *req;
	struct net_device *dev;

	if (!netdump_mode)
		return NET_RX_SUCCESS;
#if DEBUG
	{
		static int packet_count;
		Dprintk("        %d\r", ++packet_count);
	}
#endif
	dev = skb->dev;
	if (dev->type != ARPHRD_ETHER)
		goto out;
	proto = ntohs(skb->mac.ethernet->h_proto);
	Dprintk("rx got skb %p (len: %d, users: %d), dev %s, h_proto: %04x.\n", skb, skb->len, atomic_read(&skb->users), dev->name, proto);
	#define D(x) skb->mac.ethernet->h_dest[x]
	Dprintk("... h_dest:   %02X:%02X:%02X:%02X:%02X:%02X.\n", D(0), D(1), D(2), D(3), D(4), D(5));
	#undef D
	#define D(x) skb->mac.ethernet->h_source[x]
	Dprintk("... h_source: %02X:%02X:%02X:%02X:%02X:%02X.\n", D(0), D(1), D(2), D(3), D(4), D(5));
	#undef D
	if (skb->pkt_type == PACKET_OTHERHOST)
		goto out;
	if (skb_shared(skb))
		goto out;
	if (proto == ETH_P_ARP) {
		struct arphdr *arp;
		unsigned char *arp_ptr;

		Dprintk("got arp skb.\n");
		arp = (struct arphdr *)skb->data;
		if (!pskb_may_pull(skb, sizeof(struct arphdr) + 2*4 + 2*ETH_ALEN))
			goto out;
		if (htons(dev->type) != arp->ar_hrd)
			goto out;
		if (arp->ar_pro != __constant_htons(ETH_P_IP))
			goto out;
		if (arp->ar_hln != ETH_ALEN)
			goto out;
		if (arp->ar_pln != 4)
			goto out;
		if (arp->ar_op != __constant_htons(ARPOP_REQUEST))
			goto out;
		/*
		 * ARP header looks ok so far, extract fields:
		 */
		arp_ptr = (unsigned char *)(arp + 1);

		memcpy(arp_sha, arp_ptr, ETH_ALEN);
		arp_ptr += ETH_ALEN;

		memcpy(&arp_sip, arp_ptr, 4);
		arp_ptr += 4;

		memcpy(arp_tha, arp_ptr, ETH_ALEN);
		arp_ptr += ETH_ALEN;

		memcpy(&arp_tip, arp_ptr, 4);

		#define D(x) arp_sha[x]
		Dprintk("... arp_sha:   %02X:%02X:%02X:%02X:%02X:%02X.\n", D(0), D(1), D(2), D(3), D(4), D(5));
		#undef D
		#define D(x) ((unsigned char *)&arp_sip)[x]
		Dprintk("... arp_sip:   %d.%d.%d.%d.\n", D(0), D(1), D(2), D(3));
		#undef D
		#define D(x) arp_tha[x]
		Dprintk("... arp_tha:   %02X:%02X:%02X:%02X:%02X:%02X.\n", D(0), D(1), D(2), D(3), D(4), D(5));
		#undef D
		#define D(x) ((unsigned char *)&arp_tip)[x]
		Dprintk("... arp_tip:   %d.%d.%d.%d.\n", D(0), D(1), D(2), D(3));
		#undef D
		#define D(x) ((unsigned char *)&source_ip)[x]
		Dprintk("... (source_ip):   %d.%d.%d.%d.\n", D(0), D(1), D(2), D(3));
		#undef D

		if (LOOPBACK(arp_tip) || MULTICAST(arp_tip))
			goto out;

		if (arp_tip != source_ip)
			goto out;
		new_arp = 1;
		goto out;
	}
	if (proto != ETH_P_IP)
		goto out;
	/*
	 * IP header correctness testing:
	 */
	iph = (struct iphdr *)skb->data;
	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out;
	Dprintk("... IP ihl*4: %d, version: %d.\n", iph->ihl*4, iph->version);
	if (iph->ihl < 5 || iph->version != 4)
		goto out;
	if (!pskb_may_pull(skb, iph->ihl*4))
		goto out;
	if (ip_fast_csum((u8 *)iph, iph->ihl) != 0)
		goto out;
	len = ntohs(iph->tot_len);
	Dprintk("... IP len: %d.\n", len);
	if (skb->len < len || len < iph->ihl*4)
		goto out;
	saddr = iph->saddr;
	daddr = iph->daddr;
	Dprintk("... IP src: %08x, dst: %08x.\n", saddr, daddr);
	Dprintk("... IP protocol: %d.\n", iph->protocol);
	if (iph->protocol != IPPROTO_UDP)
		goto out;
	Dprintk("... netdump src: %08x, dst: %08x.\n", source_ip, netlog_target_ip);
	if (source_ip != daddr)
		goto out;
	if (netlog_target_ip != saddr)
		goto out;
	len -= iph->ihl*4;
	uh = (struct udphdr *)(((char *)iph) + iph->ihl*4);
	ulen = ntohs(uh->len);
	Dprintk("... UDP len: %d (left %d).\n", ulen, len);

#define MIN_COMM_SIZE (sizeof(*uh) + NETDUMP_REQ_SIZE)
	if (ulen != len || ulen < MIN_COMM_SIZE) {
		Dprintk("... UDP, hm, len not ok.\n");
		goto out;
	}
	if (udp_checksum_init(skb, uh, ulen, saddr, daddr) < 0) {
		Dprintk("... UDP, hm, checksum init not ok.\n");
		goto out;
	}
	if (udp_checksum_complete(skb)) {
		Dprintk("... UDP, hm, checksum complete not ok.\n");
		goto out;
	}
	Dprintk("... UDP packet OK!\n");
	Dprintk("... UDP src port: %d, dst port: %d.\n", uh->source, uh->dest);
	if (source_port != uh->source)
		goto out;
	if (netlog_target_port != uh->dest)
		goto out;
	__req = (req_t *)(uh + 1);
	Dprintk("... UDP netdump packet OK!\n");

	req = alloc_req();
	if (!req) {
		printk("no more RAM to allocate request - dropping it.\n");
		goto out;
	}

	req->magic = ntohl(__req->magic);
	req->command = ntohl(__req->command);
	req->from = ntohl(__req->from);
	req->to = ntohl(__req->to);
	req->nr = ntohl(__req->nr);

	Dprintk("... netdump magic:   %08Lx.\n", req->magic);
	Dprintk("... netdump command: %08x.\n", req->command);
	Dprintk("... netdump from:    %08x.\n", req->from);
	Dprintk("... netdump to:      %08x.\n", req->to);

	add_new_req(req);
out:
	return NET_RX_DROP;
}

int netconsole_receive_skb(struct sk_buff *skb)
{
	int ret;
	unsigned long flags;

	if (!netdump_mode)
		return NET_RX_SUCCESS;

	local_irq_save(flags);
	ret = netconsole_rx(skb);
	local_irq_restore(flags);

	return ret;
}

static void send_netdump_mem (struct net_device *dev, req_t *req)
{
	int i;
	char *kaddr;
	char str[1024];
	struct page *page = NULL;
	unsigned long nr = req->from;
	int nr_chunks = PAGE_SIZE/1024;
	reply_t reply;
	
	reply.nr = req->nr;
	reply.info = 0;
	if (req->from >= platform_max_pfn()) {
		sprintf(str, "page %08lx is bigger than max page # %08lx!\n", 
			nr, platform_max_pfn());
		reply.code = REPLY_ERROR;
		send_netdump_skb(dev, str, strlen(str), &reply);
		return;
	}
        if (platform_page_is_ram(nr)) {
                page = pfn_to_page(nr);
                if (page_to_pfn(page) == nr) {
			kaddr = (char *)kmap_atomic(page, KM_NETDUMP);
			if (!kern_addr_valid((unsigned long)kaddr)) {
				kunmap_atomic(kaddr, KM_NETDUMP);
				page = NULL;
			}
		} else
                        page = NULL;
        }
        if (!page) {
                reply.code = REPLY_RESERVED;
                reply.info = platform_next_available(nr);
                send_netdump_skb(dev, str, 0, &reply);
                return;
	}

	for (i = 0; i < nr_chunks; i++) {
		unsigned int offset = i*1024;
		reply.code = REPLY_MEM;
		reply.info = offset;
		send_netdump_skb(dev, kaddr + offset, 1024, &reply);
	}

	kunmap_atomic(kaddr, KM_NETDUMP);
}

static unsigned char effective_version = NETCONSOLE_VERSION;

/*
 * This function waits for the client to acknowledge the receipt
 * of the netdump startup reply, with the possibility of packets
 * getting lost. We resend the startup packet if no ACK is received,
 * after a 1 second delay.
 *
 * (The client can test the success of the handshake via the HELLO
 * command, and send ACKs until we enter netdump mode.)
 */
static void netdump_startup_handshake(struct net_device *dev)
{
	char tmp[200];
	reply_t reply;
	req_t *req = NULL;
	int i;

	netdump_mode = 1;
	emit_printks = 0;
repeat:
        sprintf(tmp,
            "task_struct:0x%lx page_offset:0x%llx netdump_magic:0x%llx\n",
                (unsigned long)current, (unsigned long long)PAGE_OFFSET,
                (unsigned long long)netconsole_magic);
	reply.code = REPLY_START_NETDUMP;
	reply.nr = platform_machine_type();
	reply.info = NETDUMP_VERSION_MAX;
	send_netdump_skb(dev, tmp, strlen(tmp), &reply);

	for (i = 0; i < 10000; i++) {
		// wait 1 sec.
		udelay(100);
		Dprintk("handshake: polling controller ...\n");
		if (!dev->poll_controller) {
			printk("device %s does not support netconsole!\n",
			       dev->name);
			mdelay(3000);
			machine_restart(NULL);
		}
		dev->poll_controller(dev);
		zap_completion_queue();
		req = get_new_req();
		if (req)
			break;
	}
	if (!req)
		goto repeat;
	if (req->command != COMM_START_NETDUMP_ACK) {
		kfree(req);
		goto repeat;
	}

        /*
         *  Negotiate an effective version that works with the server.
         */
        if ((effective_version = platform_effective_version(req)) == 0) {
                printk(KERN_ERR
                        "netdump: server cannot handle this client -- rebooting.\n");
                mdelay(3000);
                machine_restart(NULL);
        }

	kfree(req);

	printk("NETDUMP START!\n");
}

#if 0

static inline void print_status (req_t *req)
{
	static int count = 0;

	switch (++count & 3) {
		case 0: printk("/\r"); break;
		case 1: printk("|\r"); break;
		case 2: printk("\\\r"); break;
		case 3: printk("-\r"); break;
	}
}

#else

static inline void print_status (req_t *req)
{
	static int count = 0;
	static int prev_jiffies = 0;

	if (jiffies/HZ != prev_jiffies/HZ) {
		prev_jiffies = jiffies;
		count++;
		switch (count & 3) {
			case 0: printk("%d(%ld)/\r", nr_req, jiffies); break;
			case 1: printk("%d(%ld)|\r", nr_req, jiffies); break;
			case 2: printk("%d(%ld)\\\r", nr_req, jiffies); break;
			case 3: printk("%d(%ld)-\r", nr_req, jiffies); break;
		}
	}
}

#endif

#define CLI 1

#ifdef CONFIG_SMP
static char cpus_frozen[NR_CPUS] = { 0 }; 

static void freeze_cpu (void * dummy)
{
	cpus_frozen[smp_processor_id()] = 1;
	platform_freeze_cpu();
}
#endif

static void netconsole_netdump (struct pt_regs *regs)
{
	unsigned long flags;
#ifdef CONFIG_SMP
	int i;
#endif

	__save_flags(flags);
	__cli();
#ifdef CONFIG_SMP
	dump_smp_call_function(freeze_cpu, NULL);
	mdelay(3000);
	for (i = 0; i < NR_CPUS; i++) {
		if (cpus_frozen[i])
			printk("CPU#%d is frozen.\n", i);
		else if (i == smp_processor_id())
			printk("CPU#%d is executing netdump.\n", i);
	}
#else
	mdelay(1000);
#endif /* CONFIG_SMP */

        platform_start_netdump(do_netdump, regs);

	__restore_flags(flags);
}

static char command_tmp[1024];

static asmlinkage void do_netdump(struct pt_regs *regs, void *platform_arg)
{
        reply_t reply;
        char *tmp = command_tmp;
        struct net_device *dev = netconsole_dev;
        struct pt_regs myregs;
	struct sysinfo si;
        req_t *req;

	/*
	 * Just in case we are crashing within the networking code
	 * ... attempt to fix up.
	 */
	spin_lock_init(&dev->xmit_lock);

	platform_fix_regs();

	platform_timestamp(t0);

	printk("< netdump activated - performing handshake with the server. >\n");
	netdump_startup_handshake(dev);

	printk("< handshake completed - listening for dump requests. >\n");

	while (netdump_mode) {
		__cli();
		Dprintk("main netdump loop: polling controller ...\n");
		if (dev->poll_controller)
			dev->poll_controller(dev);
		zap_completion_queue();
#if !CLI
		__sti();
#endif
		req = get_new_req();
		if (!req)
			continue;
		Dprintk("got new req, command %d.\n", req->command);
		print_status(req);
		switch (req->command) {
		case COMM_NONE:
			Dprintk("got NO command.\n");
			break;

		case COMM_SEND_MEM:
			Dprintk("got MEM command.\n");
			// send ->from ->to.
			send_netdump_mem(dev, req);
			break;

		case COMM_EXIT:
			Dprintk("got EXIT command.\n");
			netdump_mode = 0;
			break;

		case COMM_REBOOT:
			Dprintk("got REBOOT command.\n");
			printk("netdump: rebooting in 3 seconds.\n");
			mdelay(3000);
			machine_restart(NULL);
			break;

		case COMM_HELLO:
			sprintf(tmp, "Hello, this is netdump version 0.%02d\n", NETCONSOLE_VERSION);
			reply.code = REPLY_HELLO;
			reply.nr = req->nr;
			reply.info = NETCONSOLE_VERSION;
			send_netdump_skb(dev, tmp, strlen(tmp), &reply);
			break;

		case COMM_GET_PAGE_SIZE:
			sprintf(tmp, "PAGE_SIZE: %ld\n", PAGE_SIZE);
			reply.code = REPLY_PAGE_SIZE;
			reply.nr = req->nr;
			reply.info = PAGE_SIZE;
			send_netdump_skb(dev, tmp, strlen(tmp), &reply);
			break;

		case COMM_GET_REGS:
                        reply.code = REPLY_REGS;
                        reply.nr = req->nr;
			si_meminfo(&si);
                        reply.info = (u32)si.totalram;
			send_netdump_skb(dev, tmp, platform_get_regs(tmp, &myregs), &reply);
			break;

		case COMM_GET_NR_PAGES:
			reply.code = REPLY_NR_PAGES;
			reply.nr = req->nr;
			reply.info = platform_max_pfn();
			sprintf(tmp, "Number of pages: %ld\n", platform_max_pfn());
			send_netdump_skb(dev, tmp, strlen(tmp), &reply);
			break;

		case COMM_SHOW_STATE:
			/* send response first */
			reply.code = REPLY_SHOW_STATE;
			reply.nr = req->nr;
			reply.info = 0;
			send_netdump_skb(dev, tmp, strlen(tmp), &reply);

			emit_printks = 1;
			if (regs)
				show_regs(regs);
			show_state();
			show_mem();
			emit_printks = 0;
			break;

		default:
			reply.code = REPLY_ERROR;
			reply.nr = req->nr;
			reply.info = req->command;
			Dprintk("got UNKNOWN command!\n");
			sprintf(tmp, "Got unknown command code %d!\n", req->command);
			send_netdump_skb(dev, tmp, strlen(tmp), &reply);
			break;
		}
		kfree(req);
		req = NULL;
	}
	sprintf(tmp, "NETDUMP end.\n");
	reply.code = REPLY_END_NETDUMP;
	reply.nr = 0;
	reply.info = 0;
	send_netdump_skb(dev, tmp, strlen(tmp), &reply);
	printk("NETDUMP END!\n");
}

static char *dev;
static int netdump_target_eth_byte0 = 255;
static int netdump_target_eth_byte1 = 255;
static int netdump_target_eth_byte2 = 255;
static int netdump_target_eth_byte3 = 255;
static int netdump_target_eth_byte4 = 255;
static int netdump_target_eth_byte5 = 255;

static int netlog_target_eth_byte0 = 255;
static int netlog_target_eth_byte1 = 255;
static int netlog_target_eth_byte2 = 255;
static int netlog_target_eth_byte3 = 255;
static int netlog_target_eth_byte4 = 255;
static int netlog_target_eth_byte5 = 255;

static int syslog_target_eth_byte0 = 255;
static int syslog_target_eth_byte1 = 255;
static int syslog_target_eth_byte2 = 255;
static int syslog_target_eth_byte3 = 255;
static int syslog_target_eth_byte4 = 255;
static int syslog_target_eth_byte5 = 255;

MODULE_PARM(netdump_target_ip, "i");
MODULE_PARM_DESC(netdump_target_ip,
	"remote netdump IP address as a native (not network) endian integer");
MODULE_PARM(netlog_target_ip, "i");
MODULE_PARM_DESC(netlog_target_ip,
	"remote netlog IP address as a native (not network) endian integer");
MODULE_PARM(syslog_target_ip, "i");
MODULE_PARM_DESC(syslog_target_ip,
	"remote syslog IP address as a native (not network) endian integer");

MODULE_PARM(source_port, "h");
MODULE_PARM_DESC(source_port,
	"local port from which to send netdump packets");

MODULE_PARM(netdump_target_port, "h");
MODULE_PARM_DESC(netdump_target_port,
	"remote port to which to send netdump packets");
MODULE_PARM(netlog_target_port, "h");
MODULE_PARM_DESC(netlog_target_port,
	"remote port to which to send netlog packets");
MODULE_PARM(syslog_target_port, "h");
MODULE_PARM_DESC(syslog_target_port,
	"remote port to which to send syslog packets");

#define ETH_BYTE(name,nr) \
	MODULE_PARM(name##_target_eth_byte##nr, "i"); \
	MODULE_PARM_DESC(name##_target_eth_byte##nr, \
		"byte "#nr" of the netdump server MAC address")

#define ETH_BYTES(name) \
	ETH_BYTE(name, 0); ETH_BYTE(name, 1); ETH_BYTE(name, 2); \
	ETH_BYTE(name, 3); ETH_BYTE(name, 4); ETH_BYTE(name, 5);

ETH_BYTES(netdump);
ETH_BYTES(netlog);
ETH_BYTES(syslog);

MODULE_PARM(magic1, "i");
MODULE_PARM_DESC(magic1,
	"lower 32 bits of magic cookie shared between client and server");
MODULE_PARM(magic2, "i");
MODULE_PARM_DESC(magic2,
	"upper 32 bits of magic cookie shared between client and server");
MODULE_PARM(dev, "s");
MODULE_PARM_DESC(dev,
	"name of the device from which to send netdump and syslog packets");
MODULE_PARM(mhz, "i");
MODULE_PARM_DESC(mhz,
	"one second wall clock time takes this many million CPU cycles");
MODULE_PARM(idle_timeout, "i");
MODULE_PARM_DESC(idle_timeout,
	"reboot system after this many idle seconds");

static struct console netconsole =
	 { flags: CON_ENABLED, write: write_netconsole_msg };

static int init_netconsole(void)
{
	struct net_device *ndev = NULL;
	struct in_device *in_dev;

	printk(KERN_INFO "netlog: using network device <%s>\n", dev);
	// this will be valid once the device goes up.
	if (dev)
		ndev = dev_get_by_name(dev);
	if (!ndev) {
		printk(KERN_ERR "netlog: network device %s does not exist, aborting.\n", dev);
		return -1;
	}
	if (!ndev->poll_controller) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: %s's network driver does not implement netlogging yet, aborting.\n", dev);
		return -1;
	}
	in_dev = in_dev_get(ndev);
	if (!in_dev) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: network device %s is not an IP protocol device, aborting.\n", dev);
		return -1;
	}
	in_dev_put(in_dev);

	if (!magic1 || !magic2) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: magic cookie (magic1,magic2) not specified.\n");
		return -1;
	}
	netconsole_magic = magic1 + (((u64)magic2)<<32);

	source_ip = ntohl(in_dev->ifa_list->ifa_local);
	if (!source_ip) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: network device %s has no local address, aborting.\n", dev);
		return -1;
	}
	printk(KERN_INFO "netlog: using source IP %u.%u.%u.%u\n",
	       HIPQUAD(source_ip));
	source_ip = htonl(source_ip);
	if (!source_port) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: source_port parameter not specified, aborting.\n");
		return -1;
	}
	printk(KERN_INFO "netlog: using source UDP port: %u\n", source_port);
	source_port = htons(source_port);

	if (!netdump_target_ip && !netlog_target_ip && !syslog_target_ip) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: target_ip parameter not specified, aborting.\n");
		return -1;
	}
	if (netdump_target_ip) {
		printk(KERN_INFO "netlog: using netdump target IP %u.%u.%u.%u\n",
		       HIPQUAD(netdump_target_ip));
		netdump_target_ip = htonl(netdump_target_ip);
	}
	if (netlog_target_ip) {
		printk(KERN_INFO "netlog: using netlog target IP %u.%u.%u.%u\n",
		       HIPQUAD(netlog_target_ip));
		netlog_target_ip = htonl(netlog_target_ip);
	}
	if (syslog_target_ip) {
		if (!syslog_target_port)
			syslog_target_port = 514;
		printk("netlog: using syslog target IP %u.%u.%u.%u, port: %d\n",
			HIPQUAD(syslog_target_ip), syslog_target_port);
		syslog_target_ip = htonl(syslog_target_ip);
		syslog_target_port = htons(syslog_target_port);
	}
	if (!netdump_target_port && !netlog_target_port && !syslog_target_port) {
		dev_put(ndev);
		printk(KERN_ERR "netlog: target_port parameter not specified, aborting.\n");
		return -1;
	}
	if (netdump_target_port) {
		printk(KERN_INFO "netlog: using target UDP port: %u\n", netdump_target_port);
		netdump_target_port = htons(netdump_target_port);
	}
	if (netlog_target_port) {
		printk(KERN_INFO "netlog: using target UDP port: %u\n", netlog_target_port);
		netlog_target_port = htons(netlog_target_port);
	}

	netdump_daddr[0] = netdump_target_eth_byte0;
	netdump_daddr[1] = netdump_target_eth_byte1;
	netdump_daddr[2] = netdump_target_eth_byte2;
	netdump_daddr[3] = netdump_target_eth_byte3;
	netdump_daddr[4] = netdump_target_eth_byte4;
	netdump_daddr[5] = netdump_target_eth_byte5;

	if ((netdump_daddr[0] & netdump_daddr[1] & netdump_daddr[2] & netdump_daddr[3] & netdump_daddr[4] & netdump_daddr[5]) == 255)
		printk(KERN_INFO "netlog: using broadcast ethernet frames to send netdump packets.\n");
	else
		printk(KERN_INFO "netlog: using netdump target ethernet address %02x:%02x:%02x:%02x:%02x:%02x.\n",
				netdump_daddr[0], netdump_daddr[1], netdump_daddr[2], netdump_daddr[3], netdump_daddr[4], netdump_daddr[5]);

	netlog_daddr[0] = netlog_target_eth_byte0;
	netlog_daddr[1] = netlog_target_eth_byte1;
	netlog_daddr[2] = netlog_target_eth_byte2;
	netlog_daddr[3] = netlog_target_eth_byte3;
	netlog_daddr[4] = netlog_target_eth_byte4;
	netlog_daddr[5] = netlog_target_eth_byte5;

	if ((netlog_daddr[0] & netlog_daddr[1] & netlog_daddr[2] & netlog_daddr[3] & netlog_daddr[4] & netlog_daddr[5]) == 255)
		printk(KERN_INFO "netlog: using broadcast ethernet frames to send netdump packets.\n");
	else
		printk(KERN_INFO "netlog: using netdump target ethernet address %02x:%02x:%02x:%02x:%02x:%02x.\n",
				netlog_daddr[0], netlog_daddr[1], netlog_daddr[2], netlog_daddr[3], netlog_daddr[4], netlog_daddr[5]);
	syslog_daddr[0] = syslog_target_eth_byte0;
	syslog_daddr[1] = syslog_target_eth_byte1;
	syslog_daddr[2] = syslog_target_eth_byte2;
	syslog_daddr[3] = syslog_target_eth_byte3;
	syslog_daddr[4] = syslog_target_eth_byte4;
	syslog_daddr[5] = syslog_target_eth_byte5;

	if ((syslog_daddr[0] & syslog_daddr[1] & syslog_daddr[2] & syslog_daddr[3] & syslog_daddr[4] & syslog_daddr[5]) == 255)
		printk(KERN_INFO "netlog: using broadcast ethernet frames to send syslog packets.\n");
	else
		printk(KERN_INFO "netlog: using syslog target ethernet address %02x:%02x:%02x:%02x:%02x:%02x.\n",
				syslog_daddr[0], syslog_daddr[1], syslog_daddr[2], syslog_daddr[3], syslog_daddr[4], syslog_daddr[5]);

	platform_cycles(mhz, &jiffy_cycles, &mhz_cycles);

	INIT_LIST_HEAD(&request_list);

	if (netdump_target_ip && platform_supports_netdump) {
		if (netdump_register_hooks(netconsole_rx, 
					      netconsole_receive_skb,
					      netconsole_netdump)) {
			printk("netdump: failed to register hooks.\n");
		}
	}
	netconsole_dev = ndev;
#define STARTUP_MSG "[...network console startup...]\n"
	write_netconsole_msg(NULL, STARTUP_MSG, strlen(STARTUP_MSG));

	register_console(&netconsole);
	printk(KERN_INFO "netlog: network logging started up successfully!\n");
	return 0;
}

static void cleanup_netconsole(void)
{
	printk(KERN_INFO "netlog: network logging shut down.\n");
	unregister_console(&netconsole);

#define SHUTDOWN_MSG "[...network console shutdown...]\n"
	write_netconsole_msg(NULL, SHUTDOWN_MSG, strlen(SHUTDOWN_MSG));
	dev_put(netconsole_dev);
	netconsole_dev = NULL;
	netdump_unregister_hooks();
}

module_init(init_netconsole);
module_exit(cleanup_netconsole);

MODULE_LICENSE("GPL");
