/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *  commsup.c
 *
 * Abstract: Contain all routines that are required for FSA host/adapter
 *    communication.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include "scsi.h"
#include "hosts.h"
#include <asm/semaphore.h>

#include "aacraid.h"

/**
 *	fib_map_alloc		-	allocate the fib objects
 *	@dev: Adapter to allocate for
 *
 *	Allocate and map the shared PCI space for the FIB blocks used to
 *	talk to the Adaptec firmware.
 */
 
static int fib_map_alloc(struct aac_dev *dev)
{
	dprintk((KERN_INFO
	  "allocate hardware fibs pci_alloc_consistent(%p, %d * (%d + %d), %p)\n",
	  dev->pdev, dev->max_fib_size, dev->scsi_host_ptr->can_queue,
	  AAC_NUM_FIB - AAC_NUM_IO_FIB, &dev->hw_fib_pa));
	if((dev->hw_fib_va = pci_alloc_consistent(dev->pdev, dev->max_fib_size
	  * (dev->scsi_host_ptr->can_queue + AAC_NUM_FIB - AAC_NUM_IO_FIB),
	  &dev->hw_fib_pa))==NULL)
		return -ENOMEM;
	return 0;
}

/**
 *	fib_map_free		-	free the fib objects
 *	@dev: Adapter to free
 *
 *	Free the PCI mappings and the memory allocated for FIB blocks
 *	on this adapter.
 */

void fib_map_free(struct aac_dev *dev)
{
	pci_free_consistent(dev->pdev, dev->max_fib_size * (dev->scsi_host_ptr->can_queue + AAC_NUM_FIB - AAC_NUM_IO_FIB), dev->hw_fib_va, dev->hw_fib_pa);
}

/**
 *	fib_setup	-	setup the fibs
 *	@dev: Adapter to set up
 *
 *	Allocate the PCI space for the fibs, map it and then intialise the
 *	fib area, the unmapped fib data and also the free list
 */

int fib_setup(struct aac_dev * dev)
{
	struct fib *fibptr;
	struct hw_fib *hw_fib_va;
	dma_addr_t hw_fib_pa;
	int i;
	
	while (((i = fib_map_alloc(dev)) == -ENOMEM)
	 && (dev->scsi_host_ptr->can_queue > 64))
		dev->init->MaxIoCommands = (dev->scsi_host_ptr->can_queue >>= 1);
	if (i<0)
		return -ENOMEM;
		
	hw_fib_va = dev->hw_fib_va;
	hw_fib_pa = dev->hw_fib_pa;
	memset(hw_fib_va, 0, dev->max_fib_size * (dev->scsi_host_ptr->can_queue + AAC_NUM_FIB - AAC_NUM_IO_FIB));
	/*
	 *	Initialise the fibs
	 */
	for (i = 0, fibptr = &dev->fibs[i]; i < (dev->scsi_host_ptr->can_queue + AAC_NUM_FIB - AAC_NUM_IO_FIB); i++, fibptr++) 
	{
		fibptr->dev = dev;
		fibptr->hw_fib = hw_fib_va;
		fibptr->data = (void *) fibptr->hw_fib->data;
		fibptr->next = fibptr+1;	/* Forward chain the fibs */
		init_MUTEX_LOCKED(&fibptr->event_wait);
		spin_lock_init(&fibptr->event_lock);
		hw_fib_va->header.XferState = 0xffffffff;
		hw_fib_va->header.SenderSize = cpu_to_le16(dev->max_fib_size);
		fibptr->hw_fib_pa = hw_fib_pa;
		hw_fib_va = (struct hw_fib *)((unsigned char *)hw_fib_va + dev->max_fib_size);
		hw_fib_pa = hw_fib_pa + dev->max_fib_size;
	}
	/*
	 *	Add the fib chain to the free list
	 */
	dev->fibs[dev->scsi_host_ptr->can_queue + AAC_NUM_FIB - AAC_NUM_IO_FIB - 1].next = NULL;
	/*
	 *	Enable this to debug out of queue space
	 */
	dev->free_fib = &dev->fibs[0];
	return 0;
}

/**
 *	fib_alloc	-	allocate a fib
 *	@dev: Adapter to allocate the fib for
 *
 *	Allocate a fib from the adapter fib pool. If the pool is empty we
 *	return NULL.
 */
 
struct fib * fib_alloc(struct aac_dev *dev)
{
	struct fib * fibptr;
	unsigned long flags;
	spin_lock_irqsave(&dev->fib_lock, flags);
	fibptr = dev->free_fib;	
	if(!fibptr){
		spin_unlock_irqrestore(&dev->fib_lock, flags);
		return fibptr;
	}
	dev->free_fib = fibptr->next;
	spin_unlock_irqrestore(&dev->fib_lock, flags);
	/*
	 *	Set the proper node type code and node byte size
	 */
	fibptr->type = FSAFS_NTC_FIB_CONTEXT;
	fibptr->size = sizeof(struct fib);
	/*
	 *	Null out fields that depend on being zero at the start of
	 *	each I/O
	 */
	fibptr->hw_fib->header.XferState = 0;
	fibptr->callback = NULL;
	fibptr->callback_data = NULL;
	return fibptr;
}

/**
 *	fib_free	-	free a fib
 *	@fibptr: fib to free up
 *
 *	Frees up a fib and places it on the appropriate queue
 *	(either free or timed out)
 */
 
void fib_free(struct fib * fibptr)
{
	unsigned long flags;

	spin_lock_irqsave(&fibptr->dev->fib_lock, flags);
	if (fibptr->flags & FIB_CONTEXT_FLAG_TIMED_OUT) {
		aac_config.fib_timeouts++;
		fibptr->next = fibptr->dev->timeout_fib;
		fibptr->dev->timeout_fib = fibptr;
	} else {
		if (fibptr->hw_fib->header.XferState != 0) {
			printk(KERN_WARNING "fib_free, XferState != 0, fibptr = 0x%p, XferState = 0x%x\n", 
				 (void*)fibptr, le32_to_cpu(fibptr->hw_fib->header.XferState));
		}
		fibptr->next = fibptr->dev->free_fib;
		fibptr->dev->free_fib = fibptr;
	}	
	spin_unlock_irqrestore(&fibptr->dev->fib_lock, flags);
}

/**
 *	fib_init	-	initialise a fib
 *	@fibptr: The fib to initialize
 *	
 *	Set up the generic fib fields ready for use
 */
 
void fib_init(struct fib *fibptr)
{
	struct hw_fib *hw_fib = fibptr->hw_fib;

	hw_fib->header.StructType = FIB_MAGIC;
	hw_fib->header.Size = cpu_to_le16(fibptr->dev->max_fib_size);
	hw_fib->header.XferState = cpu_to_le32(HostOwned | FibInitialized | FibEmpty | FastResponseCapable);
	hw_fib->header.SenderFibAddress = 0; /* Filled in later if needed */
	hw_fib->header.ReceiverFibAddress = cpu_to_le32(fibptr->hw_fib_pa);
	hw_fib->header.SenderSize = cpu_to_le16(fibptr->dev->max_fib_size);
}

/**
 *	fib_deallocate		-	deallocate a fib
 *	@fibptr: fib to deallocate
 *
 *	Will deallocate and return to the free pool the FIB pointed to by the
 *	caller.
 */
 
void fib_dealloc(struct fib * fibptr)
{
	struct hw_fib *hw_fib = fibptr->hw_fib;
	if(hw_fib->header.StructType != FIB_MAGIC) 
		BUG();
	hw_fib->header.XferState = 0;        
}

/*
 *	Commuication primitives define and support the queuing method we use to
 *	support host to adapter commuication. All queue accesses happen through
 *	these routines and are the only routines which have a knowledge of the
 *	 how these queues are implemented.
 */
 
/**
 *	aac_get_entry		-	get a queue entry
 *	@dev: Adapter
 *	@qid: Queue Number
 *	@entry: Entry return
 *	@index: Index return
 *	@nonotify: notification control
 *
 *	With a priority the routine returns a queue entry if the queue has free entries. If the queue
 *	is full(no free entries) than no entry is returned and the function returns 0 otherwise 1 is
 *	returned.
 */
 
static int aac_get_entry (struct aac_dev * dev, u32 qid, struct aac_entry **entry, u32 * index, unsigned long *nonotify)
{
	struct aac_queue * q;
	unsigned long idx;

	/*
	 *	All of the queues wrap when they reach the end, so we check
	 *	to see if they have reached the end and if they have we just
	 *	set the index back to zero. This is a wrap. You could or off
	 *	the high bits in all updates but this is a bit faster I think.
	 */

	q = &dev->queues->queue[qid];
	
	idx = *index = le32_to_cpu(*(q->headers.producer));
	/* Interrupt Moderation, only interrupt for first two entries */
	if (idx != le32_to_cpu(*(q->headers.consumer))) {
		if (--idx == 0) {
			if (qid == AdapHighCmdQueue)
				idx = ADAP_HIGH_CMD_ENTRIES;
			else if (qid == AdapNormCmdQueue)
				idx = ADAP_NORM_CMD_ENTRIES;
			else if (qid == AdapHighRespQueue) 
	        		idx = ADAP_HIGH_RESP_ENTRIES;
			else if (qid == AdapNormRespQueue) 
				idx = ADAP_NORM_RESP_ENTRIES;
		}
		if (idx != le32_to_cpu(*(q->headers.consumer)))
			*nonotify = 1; 
	}

	if (qid == AdapHighCmdQueue) {
	        if (*index >= ADAP_HIGH_CMD_ENTRIES)
        		*index = 0;
	} else if (qid == AdapNormCmdQueue) {
	        if (*index >= ADAP_NORM_CMD_ENTRIES) 
			*index = 0; /* Wrap to front of the Producer Queue. */
	}
	else if (qid == AdapHighRespQueue) 
	{
	        if (*index >= ADAP_HIGH_RESP_ENTRIES)
			*index = 0;
	}
	else if (qid == AdapNormRespQueue) 
	{
		if (*index >= ADAP_NORM_RESP_ENTRIES) 
			*index = 0; /* Wrap to front of the Producer Queue. */
	}
	else {
		printk("aacraid: invalid qid\n");
		BUG();
	}

        if ((*index + 1) == le32_to_cpu(*(q->headers.consumer))) { /* Queue is full */
		printk(KERN_WARNING "Queue %d full, %ld outstanding.\n",
				qid, q->numpending);
		return 0;
	} else {
	        *entry = q->base + *index;
		return 1;
	}
}   

/**
 *	aac_queue_get		-	get the next free QE
 *	@dev: Adapter
 *	@index: Returned index
 *	@priority: Priority of fib
 *	@fib: Fib to associate with the queue entry
 *	@wait: Wait if queue full
 *	@fibptr: Driver fib object to go with fib
 *	@nonotify: Don't notify the adapter
 *
 *	Gets the next free QE off the requested priorty adapter command
 *	queue and associates the Fib with the QE. The QE represented by
 *	index is ready to insert on the queue when this routine returns
 *	success.
 */

static int aac_queue_get(struct aac_dev * dev, u32 * index, u32 qid, struct hw_fib * hw_fib, int wait, struct fib * fibptr, unsigned long *nonotify)
{
	struct aac_entry * entry = NULL;
	int map = 0;
		
	if (qid == AdapHighCmdQueue || qid == AdapNormCmdQueue) 
	{
		/*  if no entries wait for some if caller wants to */
        	while (!aac_get_entry(dev, qid, &entry, index, nonotify)) 
        	{
			printk(KERN_ERR "GetEntries failed\n");
		}
	        /*
	         *	Setup queue entry with a command, status and fib mapped
	         */
	        entry->size = cpu_to_le32(le16_to_cpu(hw_fib->header.Size));
	        map = 1;
	}
	else if (qid == AdapHighRespQueue || qid == AdapNormRespQueue)
	{
	        while(!aac_get_entry(dev, qid, &entry, index, nonotify)) 
	        {
			/* if no entries wait for some if caller wants to */
		}
        	/*
        	 *	Setup queue entry with command, status and fib mapped
        	 */
        	entry->size = cpu_to_le32(le16_to_cpu(hw_fib->header.Size));
        	entry->addr = hw_fib->header.SenderFibAddress;
     			/* Restore adapters pointer to the FIB */
		hw_fib->header.ReceiverFibAddress = hw_fib->header.SenderFibAddress;	/* Let the adapter now where to find its data */
        	map = 0;
	}
	/*
	 *	If MapFib is true than we need to map the Fib and put pointers
	 *	in the queue entry.
	 */
	if (map)
		entry->addr = fibptr->hw_fib_pa;
	return 0;
}

/*
 *	Define the highest level of host to adapter communication routines. 
 *	These routines will support host to adapter FS commuication. These 
 *	routines have no knowledge of the commuication method used. This level
 *	sends and receives FIBs. This level has no knowledge of how these FIBs
 *	get passed back and forth.
 */

/**
 *	fib_send	-	send a fib to the adapter
 *	@command: Command to send
 *	@fibptr: The fib
 *	@size: Size of fib data area
 *	@priority: Priority of Fib
 *	@wait: Async/sync select
 *	@reply: True if a reply is wanted
 *	@callback: Called with reply
 *	@callback_data: Passed to callback
 *
 *	Sends the requested FIB to the adapter and optionally will wait for a
 *	response FIB. If the caller does not wish to wait for a response than
 *	an event to wait on must be supplied. This event will be set when a
 *	response FIB is received from the adapter.
 */

int aac_fib_send(u16 command, struct fib * fibptr, unsigned long size,
		int priority, int wait, int reply, fib_callback callback,
		void * callback_data)
{
	u32 qid;
	struct aac_dev * dev = fibptr->dev;
	struct hw_fib * hw_fib = fibptr->hw_fib;
	struct aac_queue * q;
	unsigned long flags = 0;
	unsigned long qflags;

	if (!(hw_fib->header.XferState & cpu_to_le32(HostOwned)))
		return -EBUSY;
	/*
	 *	There are 5 cases with the wait and reponse requested flags. 
	 *	The only invalid cases are if the caller requests to wait and
	 *	does not request a response and if the caller does not want a
	 *	response and the Fib is not allocated from pool. If a response
	 *	is not requesed the Fib will just be deallocaed by the DPC
	 *	routine when the response comes back from the adapter. No
	 *	further processing will be done besides deleting the Fib. We 
	 *	will have a debug mode where the adapter can notify the host
	 *	it had a problem and the host can log that fact.
	 */
	if (wait && !reply) {
		return -EINVAL;
	} else if (!wait && reply) {
		hw_fib->header.XferState |= cpu_to_le32(Async | ResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.AsyncSent);
	} else if (!wait && !reply) {
		hw_fib->header.XferState |= cpu_to_le32(NoResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.NoResponseSent);
	} else if (wait && reply) {
		hw_fib->header.XferState |= cpu_to_le32(ResponseExpected);
		FIB_COUNTER_INCREMENT(aac_config.NormalSent);
	} 
	/*
	 *	Map the fib into 32bits by using the fib number
	 */

	hw_fib->header.SenderFibAddress = cpu_to_le32(((u32)(fibptr - dev->fibs)) << 2);
	hw_fib->header.SenderData = (u32)(fibptr - dev->fibs);
	/*
	 *	Set FIB state to indicate where it came from and if we want a
	 *	response from the adapter. Also load the command from the
	 *	caller.
	 *
	 *	Map the hw fib pointer as a 32bit value
	 */
	hw_fib->header.Command = cpu_to_le16(command);
	hw_fib->header.XferState |= cpu_to_le32(SentFromHost);
	fibptr->hw_fib->header.Flags = 0;	/* 0 the flags field - internal only*/
	/*
	 *	Set the size of the Fib we want to send to the adapter
	 */
	hw_fib->header.Size = cpu_to_le16(sizeof(struct aac_fibhdr) + size);
	if (le16_to_cpu(hw_fib->header.Size) > le16_to_cpu(hw_fib->header.SenderSize)) {
		return -EMSGSIZE;
	}                
	/*
	 *	Get a queue entry connect the FIB to it and send an notify
	 *	the adapter a command is ready.
	 */
	if (priority == FsaHigh) {
		hw_fib->header.XferState |= cpu_to_le32(HighPriority);
		qid = AdapHighCmdQueue;
	} else {
		hw_fib->header.XferState |= cpu_to_le32(NormalPriority);
		qid = AdapNormCmdQueue;
	}

	/*
	 *	Fill in the Callback and CallbackContext if we are not
	 *	going to wait.
	 */
	if (!wait) {
		fibptr->callback = callback;
		fibptr->callback_data = callback_data;
	}

	fibptr->done = 0;
	fibptr->flags = 0;

	FIB_COUNTER_INCREMENT(aac_config.FibsSent);

	dprintk((KERN_DEBUG "Fib contents:.\n"));
	dprintk((KERN_DEBUG "  Command =               %d.\n", le32_to_cpu(hw_fib->header.Command)));
	dprintk((KERN_DEBUG "  SubCommand =            %d.\n", le32_to_cpu(((struct aac_query_mount *)fib_data(fibptr))->command)));
	dprintk((KERN_DEBUG "  XferState  =            %x.\n", le32_to_cpu(hw_fib->header.XferState)));
	dprintk((KERN_DEBUG "  hw_fib va being sent=%p\n",fibptr->hw_fib));
	dprintk((KERN_DEBUG "  hw_fib pa being sent=%lx\n",(ulong)fibptr->hw_fib_pa));
	dprintk((KERN_DEBUG "  fib being sent=%p\n",fibptr));

	q = &dev->queues->queue[qid];

	if(wait)
		spin_lock_irqsave(&fibptr->event_lock, flags);
	spin_lock_irqsave(q->lock, qflags);
	if (dev->new_comm_interface) {
		unsigned long count = 10000000L; /* 50 seconds */
		list_add_tail(&fibptr->queue, &q->pendingq);
		q->numpending++;
		spin_unlock_irqrestore(q->lock, qflags);
		while (aac_adapter_send(fibptr) != 0) {
			if (--count == 0) {
				if (wait)
					spin_unlock_irqrestore(&fibptr->event_lock, flags);
				spin_lock_irqsave(q->lock, qflags);
				q->numpending--;
				list_del(&fibptr->queue);
				spin_unlock_irqrestore(q->lock, qflags);
				return -ETIMEDOUT;
			}
			udelay(5);
		}
	} else {
		u32 index;
		unsigned long nointr = 0;
		aac_queue_get( dev, &index, qid, hw_fib, 1, fibptr, &nointr);

		list_add_tail(&fibptr->queue, &q->pendingq);
		q->numpending++;
		*(q->headers.producer) = cpu_to_le32(index + 1);
		spin_unlock_irqrestore(q->lock, qflags);
		dprintk((KERN_DEBUG "fib_send: inserting a queue entry at index %d.\n",index));
		if (!(nointr & aac_config.irq_mod))
			aac_adapter_notify(dev, qid);
	}

	/*
	 *	If the caller wanted us to wait for response wait now. 
	 */
    
	if (wait) {
		spin_unlock_irqrestore(&fibptr->event_lock, flags);
		down(&fibptr->event_wait);
		if(fibptr->done == 0)
			BUG();
			
		if((fibptr->flags & FIB_CONTEXT_FLAG_TIMED_OUT)){
			return -ETIMEDOUT;
		} else {
			return 0;
		}
	}
	/*
	 *	If the user does not want a response than return success otherwise
	 *	return pending
	 */
	if (reply)
		return -EINPROGRESS;
	else
		return 0;
}

/** 
 *	aac_consumer_get	-	get the top of the queue
 *	@dev: Adapter
 *	@q: Queue
 *	@entry: Return entry
 *
 *	Will return a pointer to the entry on the top of the queue requested that
 * 	we are a consumer of, and return the address of the queue entry. It does
 *	not change the state of the queue. 
 */

int aac_consumer_get(struct aac_dev * dev, struct aac_queue * q, struct aac_entry **entry)
{
	u32 index;
	int status;
	if (le32_to_cpu(*q->headers.producer) == le32_to_cpu(*q->headers.consumer)) {
		status = 0;
	} else {
		/*
		 *	The consumer index must be wrapped if we have reached
		 *	the end of the queue, else we just use the entry
		 *	pointed to by the header index
		 */
		if (le32_to_cpu(*q->headers.consumer) >= q->entries) 
			index = 0;		
		else
		        index = le32_to_cpu(*q->headers.consumer);
		*entry = q->base + index;
		status = 1;
	}
	return(status);
}

int aac_consumer_avail(struct aac_dev *dev, struct aac_queue * q)
{
	return (le32_to_cpu(*q->headers.producer) != le32_to_cpu(*q->headers.consumer));
}


/**
 *	aac_consumer_free	-	free consumer entry
 *	@dev: Adapter
 *	@q: Queue
 *	@qid: Queue ident
 *
 *	Frees up the current top of the queue we are a consumer of. If the
 *	queue was full notify the producer that the queue is no longer full.
 */

void aac_consumer_free(struct aac_dev * dev, struct aac_queue *q, u32 qid)
{
	int wasfull = 0;
	u32 notify;

	if ((le32_to_cpu(*q->headers.producer)+1) == le32_to_cpu(*q->headers.consumer))
		wasfull = 1;
        
	if (le32_to_cpu(*q->headers.consumer) >= q->entries)
		*q->headers.consumer = cpu_to_le32(1);
	else
		*q->headers.consumer = cpu_to_le32(le32_to_cpu(*q->headers.consumer)+1);
        
	if (wasfull) {
		switch (qid) {

		case HostNormCmdQueue:
			notify = HostNormCmdNotFull;
			break;
		case HostHighCmdQueue:
			notify = HostHighCmdNotFull;
			break;
		case HostNormRespQueue:
			notify = HostNormRespNotFull;
			break;
		case HostHighRespQueue:
			notify = HostHighRespNotFull;
			break;
		default:
			BUG();
			return;
		}
		aac_adapter_notify(dev, notify);
	}
}        

/**
 *	fib_adapter_complete	-	complete adapter issued fib
 *	@fibptr: fib to complete
 *	@size: size of fib
 *
 *	Will do all necessary work to complete a FIB that was sent from
 *	the adapter.
 */

int fib_adapter_complete(struct fib * fibptr, unsigned short size)
{
	struct hw_fib * hw_fib = fibptr->hw_fib;
	struct aac_dev * dev = fibptr->dev;
	struct aac_queue * q;
	unsigned long nointr = 0;
	unsigned long qflags;
	u32 qid;

	if (hw_fib->header.XferState == 0)
        	return 0;
	/*
	 *	If we plan to do anything check the structure type first.
	 */ 
	if ( hw_fib->header.StructType != FIB_MAGIC ) {
        	return -EINVAL;
	}
	/*
	 *	This block handles the case where the adapter had sent us a
	 *	command and we have finished processing the command. We
	 *	call completeFib when we are done processing the command 
	 *	and want to send a response back to the adapter. This will 
	 *	send the completed cdb to the adapter.
	 */
	if (hw_fib->header.XferState & cpu_to_le32(SentFromAdapter)) {
       		u32 index;
	        hw_fib->header.XferState |= cpu_to_le32(HostProcessed);
	        if (hw_fib->header.XferState & cpu_to_le32(HighPriority))
			qid = AdapHighRespQueue;
		else if (hw_fib->header.XferState & cpu_to_le32(NormalPriority))
			qid = AdapNormRespQueue;
		else
			return 0;
		if (size) {
			size += sizeof(struct aac_fibhdr);
			if (size > le16_to_cpu(hw_fib->header.SenderSize)) 
				return -EMSGSIZE;
			hw_fib->header.Size = cpu_to_le16(size);
		}
		q = &dev->queues->queue[qid];
		spin_lock_irqsave(q->lock, qflags);
		aac_queue_get(dev, &index, qid, hw_fib, 1, NULL, &nointr);
		*(q->headers.producer) = cpu_to_le32(index + 1);
		spin_unlock_irqrestore(q->lock, qflags);
		if (!(nointr & (int)aac_config.irq_mod))
			aac_adapter_notify(dev, qid);
	}
	else 
	{
        	printk(KERN_WARNING "fib_adapter_complete: Unknown xferstate detected.\n");
        	BUG();
	}   
	return 0;
}

/**
 *	fib_complete	-	fib completion handler
 *	@fib: FIB to complete
 *
 *	Will do all necessary work to complete a FIB.
 */
 
int fib_complete(struct fib * fibptr)
{
	struct hw_fib * hw_fib = fibptr->hw_fib;

	/*
	 *	Check for a fib which has already been completed
	 */

	if (hw_fib->header.XferState == 0)
        	return 0;
	/*
	 *	If we plan to do anything check the structure type first.
	 */ 

	if (hw_fib->header.StructType != FIB_MAGIC)
	        return -EINVAL;
	/*
	 *	This block completes a cdb which orginated on the host and we 
	 *	just need to deallocate the cdb or reinit it. At this point the
	 *	command is complete that we had sent to the adapter and this
	 *	cdb could be reused.
	 */
	if((hw_fib->header.XferState & cpu_to_le32(SentFromHost)) &&
		(hw_fib->header.XferState & cpu_to_le32(AdapterProcessed)))
	{
		fib_dealloc(fibptr);
	}
	else if(hw_fib->header.XferState & cpu_to_le32(SentFromHost))
	{
		/*
		 *	This handles the case when the host has aborted the I/O
		 *	to the adapter because the adapter is not responding
		 */
		fib_dealloc(fibptr);
	} else if(hw_fib->header.XferState & cpu_to_le32(HostOwned)) {
		fib_dealloc(fibptr);
	} else {
		BUG();
	}   
	return 0;
}

/**
 *	aac_printf	-	handle printf from firmware
 *	@dev: Adapter
 *	@val: Message info
 *
 *	Print a message passed to us by the controller firmware on the
 *	Adaptec board
 */

void aac_printf(struct aac_dev *dev, u32 val)
{
	char *cp = dev->printfbuf;
#if (defined(AAC_PRINTF_ENABLED))
	int length = val & 0xffff;
	int level = (val >> 16) & 0xffff;
	
	/*
	 *	The size of the printfbuf is set in port.c
	 *	There is no variable or define for it
	 */
	if (length > 255)
		length = 255;
	if (cp[length] != 0)
		cp[length] = 0;
	if (level == AAC_LOG_HIGH_ERROR)
		printk(KERN_WARNING "aacraid:%s", cp);
	else
		printk(KERN_INFO "aacraid:%s", cp);
#endif
	memset(cp, 0,  256);
}


/**
 *	aac_handle_aif		-	Handle a message from the firmware
 *	@dev: Which adapter this fib is from
 *	@fibptr: Pointer to fibptr from adapter
 *
 *	This routine handles a driver notify fib from the adapter and
 *	dispatches it to the appropriate routine for handling.
 */

static void aac_handle_aif(struct aac_dev * dev, struct fib * fibptr)
{
	struct hw_fib * hw_fib = fibptr->hw_fib;
	struct aac_aifcmd * aifcmd = (struct aac_aifcmd *)hw_fib->data;
	int busy;
	u32 container;
	struct scsi_device *device;
	enum {
		NOTHING,
		DELETE,
		ADD,
		CHANGE
	} DeviceConfigNeeded;
	extern struct proc_dir_entry * proc_scsi;

	/* Sniff for container changes */
	dprintk ((KERN_INFO "aac_handle_aif: Aif command=%x type=%x\n",
	  le32_to_cpu(aifcmd->command), le32_to_cpu(*(u32 *)aifcmd->data)));

	DeviceConfigNeeded = NOTHING;

	/*
	 *	We have set this up to try and minimize the number of
	 * re-configures that take place. As a result of this when
	 * certain AIF's come in we will set a flag waiting for another
	 * type of AIF before setting the re-config flag.
	 */
	switch (le32_to_cpu(aifcmd->command)) {
	case AifCmdDriverNotify:
		switch (le32_to_cpu(((u32 *)aifcmd->data)[0])) {
		/*
		 *	Morph or Expand complete
		 */
		case AifDenMorphComplete:
		case AifDenVolumeExtendComplete:
			container = le32_to_cpu(((u32 *)aifcmd->data)[1]);
			if (container >= MAXIMUM_NUM_CONTAINERS)
				break;
			dprintk ((KERN_INFO "container=%d(%d,%d,%d,%d)\n",
			  container,
			  (dev && dev->scsi_host_ptr)
			    ? dev->scsi_host_ptr->host_no
			    : -1,
			  CONTAINER_TO_CHANNEL(container),
			  CONTAINER_TO_ID(container),
			  CONTAINER_TO_LUN(container)));

			/*
			 *	Find the Scsi_Device associated with the SCSI
			 * address. Make sure we have the right array, and if
			 * so set the flag to initiate a new re-config once we
			 * see an AifEnConfigChange AIF come through.
			 */

			if ((dev != (struct aac_dev *)NULL)
			 && (dev->scsi_host_ptr != (struct Scsi_Host *)NULL)) {
				for (device = dev->scsi_host_ptr->host_queue;
				  device != (struct scsi_device *)NULL;
				  device = device->next)
				{
					if ((device->channel == CONTAINER_TO_CHANNEL(container))
					 && (device->id == CONTAINER_TO_ID(container))
					 && (device->lun == CONTAINER_TO_LUN(container))) {
						dev->DeviceContainerID = container;
						dev->DeviceConfigNeeded = CHANGE;
						dev->DeviceConfigWaitingOn = AifEnConfigChange;
						break;
					}
				}
			}
		}

		/*
		 *	If we are waiting on something and this happens to be
		 * that thing then set the re-configure flag.
		 */
		if (dev->DeviceConfigWaitingOn == le32_to_cpu(*(u32 *)aifcmd->data)) {
			DeviceConfigNeeded = dev->DeviceConfigNeeded;
		}
		break;

	case AifCmdEventNotify:
		switch (le32_to_cpu(((u32 *)aifcmd->data)[0])) {
		/*
		 *	Add an Array.
		 */
		case AifEnAddContainer:
			container = le32_to_cpu(((u32 *)aifcmd->data)[1]);
			if (container >= MAXIMUM_NUM_CONTAINERS)
				break;
			dev->DeviceContainerID = container;
			dev->DeviceConfigNeeded = ADD;
			dev->DeviceConfigWaitingOn = AifEnConfigChange;
			break;

		/*
		 *	Delete an Array.
		 */
		case AifEnDeleteContainer:
			container = le32_to_cpu(((u32 *)aifcmd->data)[1]);
			if (container >= MAXIMUM_NUM_CONTAINERS)
				break;
			dev->DeviceContainerID = container;
			dev->DeviceConfigNeeded = DELETE;
			dev->DeviceConfigWaitingOn = AifEnConfigChange;
			break;

		/*
		 *	Container change detected. If we currently are not
		 * waiting on something else, setup to wait on a Config Change.
		 */
		case AifEnContainerChange:
			container = le32_to_cpu(((u32 *)aifcmd->data)[1]);
			if (container >= MAXIMUM_NUM_CONTAINERS)
				break;
			dev->DeviceContainerID = container;
			if (dev->DeviceConfigWaitingOn) {
				break;
			}
			dev->DeviceConfigNeeded = CHANGE;
			dev->DeviceConfigWaitingOn = AifEnConfigChange;
			break;
		}

		/*
		 *	If we are waiting on something and this happens to be
		 * that thing then set the re-configure flag.
		 */
		if (dev->DeviceConfigWaitingOn == le32_to_cpu(*(u32 *)aifcmd->data)) {
			DeviceConfigNeeded = dev->DeviceConfigNeeded;
		}
		break;

	case AifCmdJobProgress:
		/*
		 *	These are job progress AIF's. When a Clear is being
		 * done on a container it is initially created then hidden from
		 * the OS. When the clear completes we don't get a config
		 * change so we monitor the job status complete on a clear then
		 * wait for a containe change.
		 */

		if ((((u32 *)aifcmd->data)[1] == cpu_to_le32(AifJobCtrZero))
		 && ((((u32 *)aifcmd->data)[6] == ((u32 *)aifcmd->data)[5])
		  || (((u32 *)aifcmd->data)[4] == cpu_to_le32(AifJobStsSuccess)))) {
			dev->DeviceContainerID = -1;
			dev->DeviceConfigWaitingOn = AifEnContainerChange;
			dev->DeviceConfigNeeded = ADD;
		}
		break;
	}

	if (DeviceConfigNeeded == NOTHING) {
		return;
	}

	/*
	 *	If we decided that a re-configuration needs to be done,
	 * schedule it here on the way out the door, please close the door
	 * behind you.
	 */
	dev->DeviceConfigWaitingOn = 0;
	dev->DeviceConfigNeeded = NOTHING;
	container = dev->DeviceContainerID;
	dev->DeviceContainerID = -1;

	busy = 0;

	dprintk ((KERN_INFO "container=%d(%d,%d,%d,%d)\n",
	  container,
	  (dev && dev->scsi_host_ptr)
	    ? dev->scsi_host_ptr->host_no
	    : -1,
	  CONTAINER_TO_CHANNEL(container),
	  CONTAINER_TO_ID(container),
	  CONTAINER_TO_LUN(container)));

	/*
	 *	Find the Scsi_Device associated with the SCSI address,
	 * and mark it as changed, invalidating the cache. This deals
	 * with changes to existing device IDs.
	 */

	if (!dev || !dev->scsi_host_ptr) {
		return;
	}
	for (device = dev->scsi_host_ptr->host_queue;
	  device != (struct scsi_device *)NULL;
	  device = device->next)
	{
		dprintk((KERN_INFO "aifd: device (%d,%d,%d,%d)?\n",
		  dev->scsi_host_ptr->host_no,
		  device->channel,
		  device->id,
		  device->lun));
		if ((device->channel == CONTAINER_TO_CHANNEL(container))
		 && (device->id == CONTAINER_TO_ID(container))
		 && (device->lun == CONTAINER_TO_LUN(container))) {
			busy |= device->access_count || dev->scsi_host_ptr->in_recovery;
			if (busy == 0) {
				device->removable = 1;
			}
		}
	}
	dprintk (("busy=%d\n", busy));

	/*
	 * if (busy == 0) {
	 *	scan_scsis(dev->scsi_host_ptr, 1,
	 *	  CONTAINER_TO_CHANNEL(container),
	 *	  CONTAINER_TO_ID(container),
	 *	  CONTAINER_TO_LUN(container));
	 * }
	 * is not exported as accessible, so we need to go around it
	 * another way. So, we look for the "proc/scsi/scsi" entry in
	 * the proc filesystem (using proc_scsi as a shortcut) and send
	 * it a message. This deals with new devices that have
	 * appeared. If the device has gone offline, scan_scsis will
	 * also discover this, but we do not want the device to
	 * go away. We need to check the access_count for the
	 * device since we are not wanting the devices to go away.
	 */
	if ((busy == 0) && (DeviceConfigNeeded == CHANGE) && (dev->fsa_dev.valid[container] == 1)) {
		dev->fsa_dev.valid[container] = 2;
	}
	if ((busy == 0)
	 && (proc_scsi != (struct proc_dir_entry *)NULL)) {
		struct proc_dir_entry * entry;

		dprintk((KERN_INFO "proc_scsi=%p ", proc_scsi));
		for (entry = proc_scsi->subdir;
		  entry != (struct proc_dir_entry *)NULL;
		  entry = entry->next) {
			dprintk(("\"%.*s\"[%d]=%x ", entry->namelen,
			  entry->name, entry->namelen, entry->low_ino));
			if ((entry->low_ino != 0)
			 && (entry->namelen == 4)
			 && (memcmp ("scsi", entry->name, 4) == 0)) {
				dprintk(("%p->write_proc=%p ", entry, entry->write_proc));
				if (entry->write_proc != (int (*)(struct file *, const char *, unsigned long, void *))NULL) {
					char buffer[80];
					int length;
					mm_segment_t fs;

					sprintf (buffer,
					  "scsi %s-single-device %d %d %d %d\n",
					  ((DeviceConfigNeeded == DELETE)
					   ? "remove"
					   : "add"),
					  dev->scsi_host_ptr->host_no,
					  CONTAINER_TO_CHANNEL(container),
					  CONTAINER_TO_ID(container),
					  CONTAINER_TO_LUN(container));
					length = strlen (buffer);
					dprintk((KERN_INFO
					  "echo %.*s > /proc/scsi/scsi\n",
					  length-1,
					  buffer));
//printk("echo %.*s > /proc/scsi/scsi\n", length-1, buffer);
					fs = get_fs();
					set_fs(get_ds());
					length = entry->write_proc(
					  NULL, buffer, length, NULL);
					set_fs(fs);
					dprintk((KERN_INFO
					  "returns %d\n", length));
				}
				break;
			}
		}
	}
}

/**
 *	aac_command_thread	-	command processing thread
 *	@dev: Adapter to monitor
 *
 *	Waits on the commandready event in it's queue. When the event gets set
 *	it will pull FIBs off it's queue. It will continue to pull FIBs off
 *	until the queue is empty. When the queue is empty it will wait for
 *	more FIBs.
 */
 
int aac_command_thread(struct aac_dev * dev)
{
	struct hw_fib *hw_fib, *hw_newfib;
	struct fib *fib, *newfib;
	struct aac_queue *q = &dev->queues->queue[HostNormCmdQueue];
	struct aac_fib_context *fibctx;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 *	We can only have one thread per adapter for AIF's.
	 */
	if (dev->aif_thread)
		return -EINVAL;
	/*
	 *	Set up the name that will appear in 'ps'
	 *	stored in  task_struct.comm[16].
	 */
	sprintf(current->comm, "aacraid");
	daemonize();
	/*
	 *	Let the DPC know it has a place to send the AIF's to.
	 */
	dev->aif_thread = 1;
	add_wait_queue(&q->cmdready, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	dprintk ((KERN_INFO "aac_command_thread start\n"));
	while(1) 
	{
		spin_lock_irqsave(q->lock, flags);
		while(!list_empty(&(q->cmdq))) {
			struct list_head *entry;
			struct aac_aifcmd * aifcmd;

			set_current_state(TASK_RUNNING);

			entry = q->cmdq.next;
			list_del(entry);
	
			spin_unlock_irqrestore(q->lock, flags);
			fib = list_entry(entry, struct fib, fiblink);
			/*
			 *	We will process the FIB here or pass it to a 
			 *	worker thread that is TBD. We Really can't 
			 *	do anything at this point since we don't have
			 *	anything defined for this thread to do.
			 */
			hw_fib = fib->hw_fib;

			memset(fib, 0, sizeof(struct fib));
			fib->type = FSAFS_NTC_FIB_CONTEXT;
			fib->size = sizeof( struct fib );
			fib->hw_fib = hw_fib;
			fib->data = hw_fib->data;
			fib->dev = dev;

			/*
			 *	We only handle AifRequest fibs from the adapter.
			 */
			aifcmd = (struct aac_aifcmd *) hw_fib->data;
			if (aifcmd->command == cpu_to_le32(AifCmdDriverNotify)) {
				/* Handle Driver Notify Events */
				aac_handle_aif(dev, fib);
				*(u32 *)hw_fib->data = cpu_to_le32(ST_OK);
				fib_adapter_complete(fib, sizeof(u32));
			} else {
				struct list_head *entry;
				/* The u32 here is important and intended. We are using
				   32bit wrapping time to fit the adapter field */
				   
				u32 time_now, time_last;
				unsigned long flagv;
				unsigned num;
				struct hw_fib ** hw_fib_pool, ** hw_fib_p;
				struct fib ** fib_pool, ** fib_p;
				
				/* Sniff events */
				if ((aifcmd->command == cpu_to_le32(AifCmdEventNotify))
				 || (aifcmd->command == cpu_to_le32(AifCmdJobProgress))) {
					aac_handle_aif(dev, fib);
				}

				time_now = jiffies/HZ;

				/*
				 * Warning: no sleep allowed while
				 * holding spinlock. We take the estimate
				 * and pre-allocate a set of fibs outside the
				 * lock.
				 */
				num = 0;
				spin_lock_irqsave(&dev->fib_lock, flagv);
				entry = dev->fib_list.next;
				while (entry != &dev->fib_list) {
					entry = entry->next;
					++num;
				}
				spin_unlock_irqrestore(&dev->fib_lock, flagv);
				hw_fib_pool = NULL;
				fib_pool = NULL;
				if (num
				 && ((hw_fib_pool = kmalloc(sizeof(struct hw_fib *) * num, GFP_ATOMIC)))
				 && ((fib_pool = kmalloc(sizeof(struct fib *) * num, GFP_ATOMIC)))) {
					hw_fib_p = hw_fib_pool;
					fib_p = fib_pool;
					while (hw_fib_p < &hw_fib_pool[num]) {
						if (!(*(hw_fib_p++) = kmalloc(sizeof(struct hw_fib), GFP_ATOMIC))) {
							--hw_fib_p;
							break;
						}
						if (!(*(fib_p++) = kmalloc(sizeof(struct fib), GFP_ATOMIC))) { 
							kfree(*(--hw_fib_p));
							break;
						}
					}
					if ((num = hw_fib_p - hw_fib_pool) == 0) {
						kfree(fib_pool);
						fib_pool = NULL;
						kfree(hw_fib_pool);
						hw_fib_pool = NULL;
					}
				} else if (hw_fib_pool) {
					kfree(hw_fib_pool);
					hw_fib_pool = NULL;
				}
				spin_lock_irqsave(&dev->fib_lock, flagv);
				entry = dev->fib_list.next;
				/*
				 * For each Context that is on the 
				 * fibctxList, make a copy of the
				 * fib, and then set the event to wake up the
				 * thread that is waiting for it.
				 */
				hw_fib_p = hw_fib_pool;
				fib_p = fib_pool;
				while (entry != &dev->fib_list) {
					/*
					 * Extract the fibctx
					 */
					fibctx = list_entry(entry, struct aac_fib_context, next);
					/*
					 * Check if the queue is getting
					 * backlogged
					 */
					if (fibctx->count > 20)
					{
						time_last = fibctx->jiffies;
						/*
						 * Has it been > 2 minutes 
						 * since the last read off
						 * the queue?
						 */
						if ((time_now - time_last) > 120) {
							entry = entry->next;
							aac_close_fib_context(dev, fibctx);
							continue;
						}
					}
					/*
					 * Warning: no sleep allowed while
					 * holding spinlock
					 */
					if (hw_fib_p < &hw_fib_pool[num]) {
						hw_newfib = *hw_fib_p;
						*(hw_fib_p++) = NULL;
						newfib = *fib_p;
						*(fib_p++) = NULL;
						/*
						 * Make the copy of the FIB
						 */
						memcpy(hw_newfib, hw_fib, sizeof(struct hw_fib));
						memcpy(newfib, fib, sizeof(struct fib));
						newfib->hw_fib = hw_newfib;
						/*
						 * Put the FIB onto the
						 * fibctx's fibs
						 */
						list_add_tail(&newfib->fiblink, &fibctx->fib_list);
						fibctx->count++;
						/* 
						 * Set the event to wake up the
						 * thread that is waiting.
						 */
						up(&fibctx->wait_sem);
					} else
						printk(KERN_WARNING "aifd: didn't allocate NewFib.\n");
					entry = entry->next;
				}
				/*
				 *	Set the status of this FIB
				 */
				*(u32 *)hw_fib->data = cpu_to_le32(ST_OK);
				fib_adapter_complete(fib, sizeof(u32));
				spin_unlock_irqrestore(&dev->fib_lock, flagv);
				/* Free up the remaining resources */
				hw_fib_p = hw_fib_pool;
				fib_p = fib_pool;
				while (hw_fib_p < &hw_fib_pool[num]) {
					if (*hw_fib_p)
						kfree(*hw_fib_p);
					if (*fib_p)
						kfree(*fib_p);
					++fib_p;
					++hw_fib_p;
				}
				if (hw_fib_pool)
					kfree(hw_fib_pool);
				if (fib_pool)
					kfree(fib_pool);
			}
			kfree(fib);
			spin_lock_irqsave(q->lock, flags);
		}
		/*
		 *	There are no more AIF's
		 */
		spin_unlock_irqrestore(q->lock, flags);
		schedule();

		if(signal_pending(current))
			break;
		set_current_state(TASK_INTERRUPTIBLE);
	}
	remove_wait_queue(&q->cmdready, &wait);
	dev->aif_thread = 0;
	complete_and_exit(&dev->aif_completion, 0);
	return 0;
}
