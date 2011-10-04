/*
 *	Copyright 2003 Red Hat Inc. All Rights Reserved.
 *
 *	Initial experimental implementation of the EDP2 protocol layer
 *
 *	The contents of this file are subject to the Open Software License
 *	version 1.1 that can be found at 
 *		http://www.opensource.org/licenses/osl-1.1.txt and is 
 *	included herein by reference. 
 *   
 *	Alternatively, the contents of this file may be used under the
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/sock.h>
#include <net/edp2.h>
#include <linux/init.h>
#include <linux/module.h>

static void edp2_dev_timeout(unsigned long data);
static struct edp2_device *edp_devices;
spinlock_t edp2_device_database = SPIN_LOCK_UNLOCKED;

EXPORT_SYMBOL_GPL(edp2_device_database);

/**
 *	edp2_dev_find	-	find EDP2 device
 *	@mac: MAC address
 *
 *	Each EDP2 device has a unique MAC address. We key on this
 *	for searches. We don't key on interface because we want to
 *	handle multipathing eventually
 *
 *	The caller must hold the edp2_device_database lock.
 *
 *	Returns:
 *		device pointer, or NULL - not found
 */
  
struct edp2_device *edp2_dev_find(u8 *mac)
{
	struct edp2_device *dev = edp_devices;
	
	while(dev != NULL)
	{
		if(memcmp(dev->mac, mac, 6) == 0)
			return dev;
		dev = dev->next;
	}
	return NULL;
}

EXPORT_SYMBOL_GPL(edp2_dev_find);

/**
 *	edp2_dev_alloc	-	Allocate EDP2 device
 *	@mac: MAC address
 *
 *	Add a new EDP2 device to the database. The basic fields are
 *	filled in and the device is added. The caller must hold the
 *	edp2_device_database_lock
 *
 *	Returns:
 *		device pointer or NULL - out of memory
 */
 
struct edp2_device *edp2_dev_alloc(u8 *mac)
{
	struct edp2_device *dev = kmalloc(sizeof(struct edp2_device), GFP_ATOMIC);
	if(dev == NULL)
		return NULL;
	memset(dev, 0, sizeof(struct edp2_device));
	memcpy(dev->mac, mac, 6);
	init_timer(&dev->timer);
	dev->timer.data = (unsigned long)dev;
	dev->timer.function = edp2_dev_timeout;
	dev->users = 0;
	dev->next = edp_devices;
	edp_devices = dev;
	return dev;
}

EXPORT_SYMBOL_GPL(edp2_dev_alloc);

/**
 *	edp2_dev_free	-	Free an EDP2 device
 *	@dev: device to free
 *
 *	Release an unused EDP2 device. This layer does not
 *	check usage counts, the caller must handle that.
 *	The caller must hold the edp2_device_database_lock
 */
 
void edp2_dev_free(struct edp2_device *dev)
{
	struct edp2_device **p = &edp_devices;
	
	while(*p != NULL)
	{
		if(*p == dev)
		{
			*p = dev->next;
			if(dev->dev)
				dev_put(dev->dev);
			kfree(dev);
			return;
		}
		p = &((*p)->next);
	}
	BUG();
}

EXPORT_SYMBOL_GPL(edp2_dev_free);

/**
 *	edp2_dev_recover -	hunt for a lost device
 *	@dev: The EDP2 device which vanished
 *
 *	Called when an EDP2 device that is in use vanishes. We
 *	will eventually use this to do discovery for it on other
 *	links in case it is multipathed
 */
 
static void edp2_dev_recover(struct edp2_device *dev)
{
}

/**
 *	edp2_dev_flush	-	flush down devices
 *	@dev: network device that is down
 *	
 *	Mark all the devices that are unreachable as dead
 *	for the moment. They aren't neccessarily defunct
 *	as they may be multipathed and if so they will ident
 *	for us later. Unused devices are freed.
 */
 
static void edp2_dev_flush(struct net_device *dev)
{
	struct edp2_device **p = &edp_devices;
	
	while(*p != NULL)
	{
		if((*p)->dev == dev)
		{
			struct edp2_device *d = *p;
			d->dead = 1;
			dev_put(d->dev);
			d->dev = NULL;
			if(d->users == 0)
			{
				*p = d->next;
				kfree(d);
				continue;
			}
			else	/* Live disk vanished, hunt for it */
				edp2_dev_recover(d);
		}
		p = &((*p)->next);
	}
}

/**
 *	edp2_device_recover_complete	-	device returns
 *	@edp: edp device
 *
 *	An EDP device marked as dead has been heard from. At the
 *	moment we do minimal housekeeping here but more is likely
 *	to be added
 */
 
static void edp2_device_recover_complete(struct edp2_device *edp)
{
	edp->dead = 0;
}

/*
 *	EDP2 protocol handlers
 */

/**
 *	edp2_ident_reply	-	EDP ident protocol handler
 *	@edp: EDP2 header
 *	@skb: Buffer
 *
 *	An EDP2 ident message has been sent to us. These occur when
 *	we probe and also when a device is activated. We use the ident
 *	replies to keep the EDP2 device database current.
 */
 
static void edp2_ident_reply(struct edp2_device *dev, struct edp2 *edp, struct sk_buff *skb)
{
	/* Ident indicates volume found or maybe a new volume */
	struct edp2_ata_fid1 *ident;
	
	ident = (struct edp2_ata_fid1 *)skb_pull(skb, sizeof(struct edp2_ata_fid1));
	if(ident == NULL)
		return;
	
	if(dev)
	{
		/* FIXME: can't handle count change on active device */
		dev->queue = ntohl(ident->count);
		dev->revision = ntohl(ident->firmware);
		dev->protocol = ntohs(ident->aoe)&15;
		dev->shelf = ntohs(ident->shad);
		dev->slot = ntohs(ident->slad);
		dev->last_ident = jiffies;
	}
	else
	{
		dev = edp2_dev_alloc(skb->mac.raw+6);
		if(dev != NULL)
		{
			dev_hold(skb->dev);
			dev->dev = skb->dev;
			dev->last_ident = jiffies;
			dev->queue = ntohl(ident->count);
			if(dev->queue > MAX_QUEUED)
				dev->queue = MAX_QUEUED;
			dev->revision = ntohl(ident->firmware);
			dev->protocol = ntohs(ident->aoe)&15;
			dev->shelf = ntohs(ident->shad);
			dev->slot = ntohs(ident->slad);
			/* Report the new device to the disk layer */	
			/* edp2_disk_new(dev, skb); */
		}
	}
}

/**
 *	edp2_callback	-	EDP2 disk functions
 *	@dev: EDP2 device
 *	@edp: EDP2 header
 *	@skb: buffer
 *
 *	An EDP2 response has arrived. There are several kinds
 *	of request and reply frames for our drivers. We use the
 *	tag to find the matching transmit and then the callback
 *	to tidy up.
 */
 
static void edp2_callback(struct edp2_device *dev, struct edp2 *edp, struct sk_buff *skb)
{
	struct sk_buff *txskb;
	struct edp2 *txedp;
	struct edp2_cb *cb;
	int slot = edp->tag & (MAX_QUEUED-1);
	
	/* Use the tag to find the tx frame */
	txskb = dev->skb[slot];
	if(txskb == NULL)
		return;
	/* Check the tag */
	txedp = (struct edp2 *)(skb->data+14);
	if(txedp->tag != edp->tag)
		return;
	/* Clean up before callback in case we free the last slot */
	dev->skb[slot] = NULL;
	dev->count--;
	/* Ok it is our reply */
	cb = (struct edp2_cb *)txskb->cb;
	cb->completion(dev, edp, cb->data, skb);
	kfree_skb(txskb);
	if(dev->count == 0)
		del_timer(&dev->timer);
}

/**
 *	edp2_rcv		-	EDP2 packet handler
 *	@skb: buffer
 *	@dev: device received on
 *	@pt: protocol
 *
 *	Called whenever the networking layer receives an EDP2 type frame.
 *	We perform the basic validation for an EDP initiator and then
 *	hand the frame on for processing.
 */
 
static int edp2_rcv(struct sk_buff *skb, struct net_device *netdev, struct packet_type *pt)
{
	struct edp2 *edp;
	struct edp2_device *dev;
	
	if(skb->pkt_type == PACKET_OTHERHOST)
		goto drop;
	
	edp = (struct edp2 *)skb_pull(skb, 6);
	if(edp == NULL)
		goto drop;
	
	/* We are an initiator */
	if(!(edp->flag_err & EDP_F_RESPONSE))
		goto drop;


	spin_lock(&edp2_device_database);
	dev = edp2_dev_find(skb->mac.raw + 6);
	if(dev != NULL)
	{
		if(dev->dead)
			edp2_device_recover_complete(dev);
		if(dev->dev != skb->dev)
		{
			/* A volume moved interface */
			dev_put(dev->dev);
			dev_hold(skb->dev);
			dev->dev = skb->dev;
		}
	}			
	switch(edp->function)
	{
		case 0:	/* EDP ATA */
		case 3:	/* Claim reply */
			edp2_callback(dev, edp, skb);
			break;
		case 1:	/* EDP ident reply or beacon */
			edp2_ident_reply(dev, edp, skb);
			break;
		case 2: /* Statistics reply */
		default:;
			/* Unknown type */
	}
	spin_unlock(&edp2_device_database);
drop:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 *	edp2_resend_frame	-	edp2 retry helper
 *	@dev: device to talk to
 *	@slot: slot to resend
 *
 *	Retransmit an EDP packet. This is also called to kick off
 *	the initial transmission event. Caller must hold the edp2
 *	protocol lock.
 */
 
static void edp2_resend_frame(struct edp2_device *dev, int slot)
{
	struct sk_buff *skb;
	if(dev->dead)
		return;
	skb = skb_clone(dev->skb[slot], GFP_ATOMIC);
	if(skb == NULL)
		return;
	/* Fill in sending interface */
	memcpy(skb->data, dev->dev->dev_addr, 6);
	/* Fire at will */
	skb->dev = dev->dev;
	skb->nh.raw = skb->data + 14;
	skb->mac.raw = skb->data;
	skb->protocol = htons(ETH_P_EDP2);
	dev_queue_xmit(skb);
}

/**
 *	edp2_resend_timeout	-	EDP2 failed
 *	@dev: Device that timed out
 *	@slot: Slot ID of timed out frame
 *
 *	Clean up after a timeout failure. We call the callback wth
 *	NULL replies to indicate a failure. Possibly we should have two
 *	callback functions ?
 */
 
static void edp2_resend_timeout(struct edp2_device *dev, int slot)
{
	struct sk_buff *skb = dev->skb[slot];
	struct edp2_cb *cb;
	dev->skb[slot] = NULL;
	cb = (struct edp2_cb *)skb->cb;
	cb->completion(dev, NULL, cb->data, NULL);
	kfree_skb(skb);
}

/**
 *	edp2_dev_timeout	-	EDP2 timer event
 *	@data: The EDP2 device that timed out
 *
 *	Walk the EDP2 list for this device and handle retransmits.
 *	The current slot array is just get it going, we need a real
 *	linked chain to walk for this and other purposes.
 *
 *	FIXME: locking
 */
 
static void edp2_dev_timeout(unsigned long data)
{
	struct edp2_device *dev = (struct edp2_device *)data;
	struct edp2_cb *cb;
	unsigned long next = jiffies + 1000*HZ;
	int found = 0;
	int i;
	
	for(i=0; i < MAX_QUEUED; i++)
	{
		if(dev->skb[i])
		{
			cb = (struct edp2_cb *)(dev->skb[i]->cb);
			if(time_before(dev->skb[i], cb->timeout))
			{
				/* Need to do better adaption ! */
				cb->timeout = jiffies + (1<<cb->count)*HZ;
				cb->count++;
				if(cb->count == 10)
					edp2_resend_timeout(dev, i);
				else
				{
					found=1;
					edp2_resend_frame(dev, i);
				}
			}
			else
			{
				found = 1;
				if(time_before(cb->timeout, next))
					next = cb->timeout;
			}
		}
	}
	if(!found)
		return;
	mod_timer(&dev->timer, next);
}
	
/**
 *	edp2_queue_xmit		-	send an EDP2 frame
 *	@dev: device to talk to
 *	@edp: edp header in packet
 *	@skb: packet to send
 *
 *	Send a prepared EDP frame out to a device. The EDP frame
 *	has its timers set and is queued for retransmit in case
 *	of problems. We also use the low tag bits ourselves.
 *
 *	The caller has completed the edp2_cb control block and
 *	must hold the edp2 protocol lock.
 */
 
int edp2_queue_xmit(struct edp2_device *dev, struct edp2 *edp, struct sk_buff *skb)
{
	int next ;
	edp->tag &= ~0xFF;	/* Low byte is reserved for protocol muncher */
	
	/* FIXME: better algorithm given the fact completion is
	   generally ordered */
	for(next = 0; next < MAX_QUEUED; next++)
		if(dev->skb[next] == NULL)
			break;
	if(dev->skb[next])
		BUG();
	dev->skb[next] = skb;
	edp->tag |= next;	/* So we can find the skb fast */
	dev->count++;
	/* FIXME: need to know if the timer is live before adjusting
	   but must do this race free of the expire path */
	mod_timer(&dev->timer, jiffies + 2 * HZ);	
	edp2_resend_frame(dev, next);
	return next;
}

EXPORT_SYMBOL_GPL(edp2_queue_xmit);

/**
 *	edp2_alloc_skb		-	alloc an EDP2 frame
 *	@dev: Device we will talk to
 *	@len: length of frame (in full)
 *	@function: EDP function
 *	@callback: Callback handler on completion/error
 *	@data: data for callback
 *	@timeout: retransmit timeout
 *
 *	Allocate and build the basis of an EDP2 frame
 *	We hand up a frame with everything built below the
 *	application layer.
 *
 *	The caller must hold the edp2 protocol lock
 */
 
struct sk_buff *edp2_alloc_skb(struct edp2_device *dev, int len, int function, 
	int (*callback)(struct edp2_device *, struct edp2 *, unsigned long, struct sk_buff *),
	unsigned long data, unsigned long timeout)
{
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);
	struct edp2_cb *cb = (struct edp2_cb *)skb->cb;
	struct edp2 *edp;
	u8 *hdr;
	if(skb == NULL)
		return NULL;

	/* MAC header */
	hdr = skb_put(skb, 14);
	memcpy(hdr+6, dev->mac, 6);
	hdr[12] = 0x88;
	hdr[13] = 0xA2;
	
	edp = (struct edp2 *)skb_put(skb, sizeof(struct edp2));
	edp->flag_err = 0;
	edp->function = function;
	edp->tag = dev->tag << 8;	/* Kernel tags are 0:devtag:slot */
	dev->tag++;			/* Wraps at 65536 */

	/* So we can handle the completion path nicely */	
	cb->completion = callback;
	cb->data = data;
	cb->timeout = timeout;
	cb->count = 0;
	
	return skb;
}
	
EXPORT_SYMBOL_GPL(edp2_alloc_skb);

/**
 *	edp2_device_event	-	device change notifier
 *	@this: event block
 *	@event: event id
 *	@ptr: data for event
 *
 *	Listen to the network layer events and watch for devices going
 *	down. We need to release any devices that vanish so that the
 *	refcount drops and the device can be flushed.
 */
 
static int edp2_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	unsigned long flags;
	if(event == NETDEV_DOWN)
	{
		spin_lock_irqsave(&edp2_device_database, flags);
		edp2_dev_flush(ptr);
		spin_unlock_irqrestore(&edp2_device_database, flags);
	}
	return NOTIFY_DONE;
}

static struct notifier_block edp2_notifier = {
	notifier_call:	edp2_device_event
};

static struct packet_type edp2_dix_packet_type = {
	type:		__constant_htons(ETH_P_EDP2),
	func:		edp2_rcv,
	data:		(void *) 1,	/* Shared skb is ok */
};

/**
 *	edp2_proto_init	-	set up the EDP2 protocol handlers
 *	
 *	This function sets up the protocol layer for EDP2. It assumes
 *	that the caller is ready to receive packets and events so
 *	requires the EDP block layer data structures are initialised.
 */
 
static __init int edp2_proto_init(void)
{
	printk(KERN_INFO "Coraid EDPv2 Protocol Layer v0.01\n");
	printk(KERN_INFO "   (c) Copyright 2003 Red Hat.\n");
	dev_add_pack(&edp2_dix_packet_type);
	register_netdevice_notifier(&edp2_notifier);
	return 0;
}

/**
 *	edp2_proto_exit	-	cease listening to networking for EDP2
 *
 *	Disconnect EDP2 from the network layer and cease listening to
 *	any device events
 */
 
static void __exit edp2_proto_exit(void)
{
	/* FIXME: module locking, device flushing */
	unregister_netdevice_notifier(&edp2_notifier);
	dev_remove_pack(&edp2_dix_packet_type);
}

module_init(edp2_proto_init);
module_exit(edp2_proto_exit);
