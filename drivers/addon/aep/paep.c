/*
 * $Revision: 1.1 $
 * $Source: /home/master/uaep/driver/linux/paep.c,v $
 * $Date: 2002/02/21 17:00:41 $
 *
 * $CN{$ 
 * 
 * Copyright (c) 1999-2001 Accelerated Encryption Processing Ltd. (AEP)
 * Bray Business Park, Southern Cross Route, Bray, Co. Wicklow, Ireland.
 * All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of AEP Ltd. nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * 
 * $}CN$
 */
/*
 * AEP driver
 * For a Uniprocessor:
 * Compile with "gcc -O -DMODULE -D__KERNEL__ -c paep.c"
 * For an SMP machine:
 * Compile with "gcc -O -DMODULE -D__KERNEL__ -D__SMP__ -c paep.c"
 * Install with "root /sbin/insmod paep.o"
 * See also "lsmod" and "rmmod"
 * Get the major number from /var/log/messages
 * Use via "mknod /dev/aep c <major#> <minor#>"
 *
 *
 * History:
 *
 * - implemented proc_entry_owner to open and close /proc/paep entry only once for all AEP devices 
 * - kernel style indent, changed write_dma_msg and make_dma_msg to static inline 
 */



#include <linux/version.h>

/* 
* Removed this file for compatiblility across 
*  2.4.* kernels 
*/
/*#include <linux/modversions.h>*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/errno.h>         /* for -EBUSY */
#include <linux/proc_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/poll.h>


#include <asm/uaccess.h>         /* for copy_to_user */
#include <asm/io.h> 

#include "paep.h"

#ifndef DRIVER_VERSION
#define DRIVER_VERSION "UNLABELLED"
#endif

static char const _RCSId[] =  "$Id: paep.c,v 1.1 2002/02/21 17:00:41 tmaher Exp $"
                              "$Source: /home/master/uaep/driver/linux/paep.c,v $";

static char aep_version_str[100] = "AEP Device Driver. Version: ";

/*  add more vendor/id pairs in the future when aep gets its own id's */
static struct pci_device_id aep_pci_tbl[] __initdata = {
        { AEP_PCI_VENDOR, AEP_PCI_ID, AEP_SUBSYS_VENDOR, AEP_SUBSYS_ID, },
        { 0, } ,
};

static struct file_operations aep_fops = {
        owner:     THIS_MODULE,  
        llseek:    NULL,        
        read:      read_aep,
        write:     write_aep,
        readdir:   NULL, 
        poll:      poll_aep, 
        ioctl:     NULL, 
        mmap:      NULL,
        open:      open_aep,
        flush:     NULL, 
        release:   release_aep,
        fsync:     NULL, 
        fasync:    NULL, 
        lock:      NULL,
        readv:     NULL,
        writev:    NULL
};
MODULE_DEVICE_TABLE (pci, aep_pci_tbl);

static struct pci_driver aep_driver = {
        name:      AEP_DRIVER_NAME,
        id_table:  aep_pci_tbl,
        probe:     aep_init_one_device,
        remove:    aep_remove_one_device,
        suspend:   NULL,
        resume:    NULL
};

MODULE_AUTHOR("AEP software development team");
MODULE_DESCRIPTION("AEP (tm) 1000 PCI cryptographic accelerator driver");
MODULE_LICENSE("Dual BSD/GPL");

/* globals for this driver - Not visible to the kernel */

static struct pci_dev **dev_list_pp       = NULL;  /* list of AEP devices */
static struct pci_dev *proc_entry_owner_p = NULL;  /* /proc/paep entry is common to all devices, only open once */

static int nb_cards       = 0;  /* used to generate the names in /dev/paep0, /dev/paep1, /dev/paep<nb_cards> */
static int dma_buff_order = 0;  /* order of the buffer used to dma data */

static void inline write_dma_msg(const DMA_msg_t *msg_p, paep_unit_info_t *dev_param_p, int target_address)
{
        assert(msg_p && dev_param_p && target_address);
    
        DPRINTK("writing message to 0x%08X\n", target_address);
        DPRINTK_MSG(msg_p);
    
        writel(msg_p->command, dev_param_p->mem_space + target_address);
        writel(msg_p->address, dev_param_p->mem_space + target_address + sizeof(u32));
        writel(msg_p->length, dev_param_p->mem_space + target_address + 2*sizeof(u32));
        return;
}

static void inline make_dma_msg(DMA_msg_t *msg_p, int command, int address, int length)
{
        assert(msg_p);
    
        msg_p->command = command;
        msg_p->address = address;
        msg_p->length = length;
}

int aep_read_procmem(char *buf_p, char **start_pp, off_t offset,
		     int len, int *unused_p, void* also_unused_p)

{
        int              count;
        struct pci_dev   *dev_p;
        paep_unit_info_t *dev_param_p;

        assert(buf_p && start_pp);

        len = 0;

        for (count = 0; count < AEP_MAXCARDS_NB; count++) {

                if (dev_list_pp[count]) {

                        dev_p = dev_list_pp[count];
                        dev_param_p = dev_p->driver_data;
            
                        len += sprintf(buf_p+len, "\nDevice %i:\n",count);

                            /* flags */
                        len += sprintf(buf_p+len, "    Flags = 0x%08X ", dev_param_p->flags);

                        if (dev_param_p->flags) {
                                len += sprintf(buf_p+len, "( ");
                                if (dev_param_p->flags & DEV_OPEN) len += sprintf(buf_p+len, "DEV_OPEN");
                                if (dev_param_p->flags & DMA_DEV_BUSY) len += sprintf(buf_p+len, " | DMA_DEV_BUSY");
                                if (dev_param_p->flags & WAITING_FOR_READ_DATA) len += sprintf(buf_p+len, " | WAITING_FOR_READ_DATA");
                                if (dev_param_p->flags & READ_DATA_AVAILABLE) len += sprintf(buf_p+len, " | READ_DATA_AVAILABLE");
                                len += sprintf(buf_p+len, " )\n");
                        } else {
                                len += sprintf(buf_p+len, "\n");
                        }

                        if (len > LIMIT) return len;

                            /* read state */
                        len += sprintf(buf_p+len, "    Read State = 0x%08X ", dev_param_p->read_state);

                        if (dev_param_p->read_state == RS_IDLE) {
                                len += sprintf(buf_p+len, "( IDLE )\n");
                        } else if (dev_param_p->read_state == RS_DMA_ACTIVE) {
                                len += sprintf(buf_p+len, "( DMA_ACTIVE )\n");
                        } else if (dev_param_p->read_state == RS_DMA_COMPLETE) {
                                len += sprintf(buf_p+len, "( DMA_COMPLETE)\n");
                        } else if (dev_param_p->read_state == RS_DEAD) {
                                len += sprintf(buf_p+len, "( DEAD)\n");
                        } else {
                                len += sprintf(buf_p+len, "(***Unknown***)\n");
                        }
                        if (len > LIMIT) return len;

                            /* write state */
                        len += sprintf(buf_p+len, "    Write State = 0x%08X ", dev_param_p->write_state);

                        if (dev_param_p->write_state == WS_IDLE) {
                                len += sprintf(buf_p+len, "( IDLE )\n");
                        } else if (dev_param_p->write_state == WS_DMA_ACTIVE) {
                                len += sprintf(buf_p+len, "( DMA_ACTIVE )\n");
                        } else if (dev_param_p->write_state == WS_DMA_COMPLETE) {
                                len += sprintf(buf_p+len, "( DMA_COMPLETE)\n");
                        } else if (dev_param_p->write_state == WS_DEAD) {
                                len += sprintf(buf_p+len, "( DEAD)\n");
                        } else {
                                len += sprintf(buf_p+len, "(***Unknown***)\n");
                        }
                        if (len > LIMIT) return len;

#ifdef INSTRUMENT
                        len += sprintf(buf_p+len, "    Open Count = %ld\n", dev_param_p->open_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Busy on Open Count = %ld\n", dev_param_p->open_busy_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Close Count = %ld\n", dev_param_p->close_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Read Count = %ld\n", dev_param_p->read_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Read DMA Count = %ld\n", dev_param_p->read_dma_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Busy on Read Count = %ld\n", dev_param_p->read_busy_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Read Byte Count = %ld\n", dev_param_p->read_byte_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Write Count = %ld\n", dev_param_p->write_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Write DMA Count = %ld\n", dev_param_p->write_dma_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Busy on Write Count = %ld\n", dev_param_p->write_busy_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Busy on Write Count 2 = %ld\n", dev_param_p->write_busy_cnt2);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Write Byte Count = %ld\n", dev_param_p->write_byte_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "    Interrupt Count = %ld\n", dev_param_p->interrupt_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        SIGNAL_DRIVER: %ld\n", dev_param_p->SIGNAL_DRIVER_int_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        DMA_WRITE_COMPLETE: %ld\n", dev_param_p->DMA_WRITE_COMPLETE_int_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        DMA_WRITE_NO_SPACE: %ld\n", dev_param_p->DMA_WRITE_NO_SPACE_int_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        DMA_WRITE_ERROR: %ld\n", dev_param_p->DMA_WRITE_ERROR_int_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        DMA_READ_COMPLETE: %ld\n", dev_param_p->DMA_READ_COMPLETE_int_cnt);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        DMA_READ_ERROR: %ld\n", dev_param_p->DMA_READ_ERROR_int_cnt);
                        if (len > LIMIT) return len;
#endif/*INSTRUMENT*/
            
#ifndef DONT_PRINT_DEBUG_PROC
                        len += sprintf(buf_p+len, "        dev_p = 0x%p\n", dev_p);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        config_space = 0x%p\n", dev_param_p->config_space);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        mem_space = 0x%p\n", dev_param_p->mem_space);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        read_dma_buf_p = 0x%p\n", dev_param_p->read_dma_buf_p);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        write_dma_buf_p = 0x%p\n", dev_param_p->write_dma_buf_p);
                        if (len > LIMIT) return len;

                        len += sprintf(buf_p+len, "        int_inuse = 0x%08x\n", dev_param_p->int_inuse);
                        if (len > LIMIT) return len;
#endif/* DONT_PRINT_DEBUG_PROC */
                }
        }
        return len;
}


/* Read function*/
static ssize_t read_aep(struct file *file_p, char *buf_p, size_t count, loff_t *ppos_p)
{
	paep_unit_info_t  *dev_param_p = file_p->private_data;
	DMA_msg_t         msg;
	unsigned int      targ_msg = 0; 
	wait_queue_t      local_wq;

	assert(file_p && buf_p);

	BANNER();

	if((file_p->f_flags & O_NONBLOCK) && (!(dev_param_p->flags & READ_DATA_AVAILABLE))){
		return -EAGAIN;
	}

	DPRINTK("file_p = 0x%p, buf_p = 0x%p, count = 0x%08X, ppos_p = 0x%p\n",
			file_p, buf_p, count, ppos_p);

	init_waitqueue_entry(&local_wq, current); 

#ifdef INSTRUMENT
	++dev_param_p->read_cnt;
#endif /* INSTRUMENT */

	/* Wait until there is data available before
		 * we proceed with the read. 
	*/
	down_interruptible(&dev_param_p->read_sem);

	down(&dev_param_p->aep_sem);

	dev_param_p->flags &= ~READ_DATA_AVAILABLE;

	/*  Set up the DMA and initiate it */
	make_dma_msg(&msg, DMA_DEVICE_READ, virt_to_bus(dev_param_p->read_dma_buf_p), count);
	targ_msg = get_msg_ptr(dev_param_p);
	exit_if_invalid_msg_ptr(targ_msg, dev_param_p);

		/*
		 * set up device and go!
		 *
		 * Note: we don't need to acquire the MUTEX here for two reasons:
		 * 1) we are the only top half routine that touches this stuff, and
		 * the mutex & conditional variable stuff surrounding DMA_DEV_BUSY
		 * art the start of this routine single threads us, therefore 2) only
		 * the interrupt routine can interrupt this stuff.
		 */

		/* Write the message we just made to the device */
	write_dma_msg(&msg, dev_param_p, targ_msg);

		/*
		 *  Put this mesage on the InBound Post list.  When the inbound post list is not
		 *  empty the target gets interrupted and takes the next address from the list and
		 *  processes the message
		 */

	DPRINTK("writing request\n");

	add_wait_queue(&dev_param_p->rq, &local_wq);
	current->state = TASK_INTERRUPTIBLE;

	post_msg(targ_msg, dev_param_p);

		/* Suspend the process with a timeout in case our device goes to lunch */
	schedule_timeout(IO_TIMEOUT * HZ);
	remove_wait_queue(&dev_param_p->rq, &local_wq);

	up(&dev_param_p->aep_sem);

		/*
		 *  Determine whether we completed, or timed out. Note that
		 *  there is a small chance here that we both completed and
		 *  timed out because the completion and timeout happened
		 *  at about the same time. Therfore, we can't use information
		 *  about the process (timeout) to tell what
		 *  happened.
		 */

	if (dev_param_p->read_state == RS_DMA_COMPLETE) 
	{
				/* we did complete */
#ifdef INSTRUMENT
			++dev_param_p->read_dma_cnt;
			dev_param_p->read_byte_cnt += count - dev_param_p->read_residual;
#endif /* INSTRUMENT */
			dev_param_p->read_state = RS_IDLE;

			DPRINTK("copying 0x%08X bytes from 0x%p to 0x%p\n",
					count, dev_param_p->read_dma_buf_p, buf_p);

			copy_to_user(buf_p, dev_param_p->read_dma_buf_p, count - dev_param_p->read_residual); 
 
	} 
	else 
	{
				/* timeout or error */
			dev_param_p->read_state = RS_IDLE;
			return -ETIME;
	}
		/* Now that we're really done, */

	return (count - dev_param_p->read_residual);
}

/*
 *  Write function
 *
 *  Since there is no supported way in Linux to DMA from a user space buffer
 *  to a PCI device, we have to copy the user data to a buffer and DMA from
 *  there. The best we can do is to allocate a buffer for this purpose and
 *  repeatedly copy and DMA through that buffer until all the user's data
 *  has been sent to the device. The size of the buffer may be set at module
 *  load time through the variable aep_dma_buff_order.
 */
static ssize_t write_aep(struct file *file_p, const char *buf_p, size_t size, loff_t *ppos_p)
{
        paep_unit_info_t *dev_param_p = file_p->private_data;
        DMA_msg_t msg;
        unsigned int targ_msg; /* address of message */
        wait_queue_t local_wq;

        assert(file_p && buf_p);
    
        BANNER();

        DPRINTK("file_p = %p, buf_p = %p, size = %d, ppos_p = %p\n", file_p, buf_p, size, ppos_p);

        init_waitqueue_entry(&local_wq, current);

        dev_param_p->write_fail_reason = 0;
    
#ifdef INSTRUMENT
        ++dev_param_p->write_cnt;
#endif /* INSTRUMENT */

        down(&dev_param_p->aep_sem);

            /*
             *  Copy the user data into the buffer up to the buffer size
             *  This may cause the calling process to suspend if it's
             *  data is not paged in
             */
        copy_from_user(dev_param_p->write_dma_buf_p, buf_p, size); /* may suspend */
       
        DPRINTK("setting up DMA\n");
            /* Set up the DMA and initiate it */
    
        make_dma_msg(&msg, DMA_DEVICE_WRITE, virt_to_bus(dev_param_p->write_dma_buf_p), size);
        targ_msg = get_msg_ptr(dev_param_p);
        exit_if_invalid_msg_ptr(targ_msg, dev_param_p);

            /* Write the message we just made to the device */
        write_dma_msg(&msg, dev_param_p, targ_msg);
    
        add_wait_queue(&dev_param_p->wq, &local_wq);
        current->state=TASK_INTERRUPTIBLE;
    
        post_msg(targ_msg, dev_param_p);
    
            /* Suspend the process with a timeout in case our device goes to lunch */
        schedule_timeout(IO_TIMEOUT * HZ);
        remove_wait_queue(&dev_param_p->wq, &local_wq);

            /*
             *  Determine whether we completed, or timed out. Note that
             *  there is a small chance here that we both completed and
             *  timed out because the completion and timeout happened
             *  at about the same time. Therfore, we can't use information
             *  about the process (timeout) to tell what
             *  happened.
             */

        if (dev_param_p->write_state == WS_DMA_COMPLETE) 
		{

                DPRINTK("DMA completed normally\n");
                dev_param_p->write_state = WS_IDLE;
                up(&dev_param_p->aep_sem);
#ifdef INSTRUMENT
                ++dev_param_p->write_dma_cnt;
                dev_param_p->write_byte_cnt += this_size;
#endif /* INSTRUMENT */
        } 
		else if (dev_param_p->write_fail_reason == DMA_WRITE_ERROR) 
		{
        
                dev_param_p->write_state = WS_IDLE;
                up(&dev_param_p->aep_sem);
                ERR_PRINTK("Write error (returning -EIO)\n");
                return -EIO;
        
        } 
		else if (dev_param_p->write_fail_reason == DMA_WRITE_NO_SPACE) 
		{
	  
                    /* don't print this for now, as it saturates /var/log/messages with the vsa / sar
                     * Not a driver issue. VF 
                     *  WARN_PRINTK("Write completed with no space on device (returning -EAGAIN)\n");
                     */ 
        
                dev_param_p->write_state = WS_IDLE; 
                up(&dev_param_p->aep_sem);
                return -EAGAIN;
        } else 
		{
        
                WARN_PRINTK("Write completed with timeout (returning -ETIME)\n");
                dev_param_p->write_state = WS_IDLE;
                up(&dev_param_p->aep_sem);
                return -ETIME;
        }
    
        return (size);
}


static size_t poll_aep(struct file *file_p, poll_table *wait)
{
	paep_unit_info_t *dev_param_p = file_p->private_data;
	unsigned int	mask = 0;

	poll_wait(file_p, &dev_param_p->select_wq, wait);
	
	if(dev_param_p->flags & READ_DATA_AVAILABLE){
		mask = POLLIN | POLLRDNORM;
	}
	else{
		mask = 0;
	}

	return(mask);
}

/*
 *  Open function
 Note that we do not request the IRQ here because of an errata that exists on the IXP1200's PCI
 interface.  The errata effectively means that the host has no way to turn off device interrupts
 so rather than request and return the irq in the open and release methods, we just grab an irq
 at init.  This problem manifested itself when a process using the device was killed, the
 release method waould be called and an interrupt would come in just after the irq was returned,
 resulting in a kernel crash
 */
static int open_aep(struct inode *inode_p, struct file *file_p)
{
        int retval = 0; /* success */
        int unit_no;
        struct pci_dev   *dev_p = NULL;
        paep_unit_info_t *dev_param_p = NULL;

        BANNER();

        assert(inode_p && file_p);
    
        unit_no = MINOR(inode_p->i_rdev);
        exit_if_too_many_cards(unit_no);
        
        if (unit_no >= nb_cards)
        	return -ENODEV;
    
        dev_p = dev_list_pp[unit_no];
        dev_param_p = dev_p->driver_data;
    
    
        DPRINTK("unit %d\n", unit_no);

        if (dev_p == NULL)
                return -ENODEV;

        assert(dev_param_p);
    
        file_p->private_data = dev_param_p;

        down(&dev_param_p->aep_sem);
        DPRINTK("entered mutex\n");

            /* this is strictly an internal sanity check */
        if ((dev_param_p->read_state != RS_IDLE) || (dev_param_p->write_state != WS_IDLE)) 
		{

                DPRINTK("invalid state(s) (failing)\n");
                retval = -EBUSY;
                goto open_release_mutex_and_exit;
        }

            /* only allow a single open of the device */
        if (dev_param_p->flags & DEV_OPEN) 
		{

                DPRINTK("busy (failing)\n");
#ifdef INSTRUMENT
                ++dev_param_p->open_busy_cnt;
#endif/*INSTRUMENT*/
                retval = -EBUSY;
                goto open_release_mutex_and_exit;
        } else {
                    /* Claim all the per-card resources we need 
                       interrupt - getting it here allows other cards to use it */


                DPRINTK("allocating read buffer of order %d\n", dma_buff_order);
                dev_param_p->read_dma_buf_p = (char *)__get_dma_pages(GFP_KERNEL, dma_buff_order);

                if (!dev_param_p->read_dma_buf_p) 
				{
            
                        ERR_PRINTK("Couldn't allocate memory for dma read buffer (returning -ENOMEM)\n");
                        retval = -ENOMEM;
                        goto open_release_mutex_and_exit;
                }
        
                DPRINTK("allocated read buffer at 0x%p\n", dev_param_p->read_dma_buf_p);
                DPRINTK("allocating write buffer of order %d\n", dma_buff_order);
        
                dev_param_p->write_dma_buf_p = (char *)__get_dma_pages(GFP_KERNEL, dma_buff_order);

                if (!dev_param_p->write_dma_buf_p) 
				{
            
                        ERR_PRINTK("Couldn't allocate memory for dma write buffer (returning -ENOMEM)\n");
                        retval = -ENOMEM;
                        goto open_release_mutex_and_exit;
                }
        
                DPRINTK("allocated write buffer at 0x%p\n", dev_param_p->write_dma_buf_p);
        }

#ifdef INSTRUMENT
        ++dev_param_p->open_cnt;
#endif/*INSTRUMENT*/
        MOD_INC_USE_COUNT;

        dev_param_p->flags |= DEV_OPEN;
            /*
             * If the device was last closed while active, then we may have missed an interrupt,
             * and there may be data on the device. We go ahead and set READ_DATA_AVAILABLE.
             * If there isn't data on the device, the first read simply completes with
             * zero bytes read
             */
		dev_param_p->flags |= READ_DATA_AVAILABLE;
		up(&dev_param_p->read_sem);

  open_release_mutex_and_exit:
        up(&dev_param_p->aep_sem);
        DPRINTK("left mutex and returning %d\n", retval);
        return retval;
}

/*
 *  Release function
 */
static int release_aep(struct inode *inode_p, struct file *file_p)
{
        int unit_no;
        int retval = 0;

        struct pci_dev   *dev_p;
        paep_unit_info_t *dev_param_p;

        assert(inode_p && file_p);
    
        BANNER();

        unit_no = MINOR(inode_p->i_rdev);
        exit_if_too_many_cards(unit_no);
        
        dev_p = dev_list_pp[unit_no];
        dev_param_p = dev_p->driver_data;
    
        DPRINTK("unit %d\n", unit_no);

            /* validity checks */
        if (dev_param_p == 0)
                return -EINVAL;
    
        if (!(dev_param_p->flags & DEV_OPEN))
                return -ENODEV;

            /* enter the mutex for this device */
        down(&dev_param_p->aep_sem);

        dev_param_p->flags &= ~DEV_OPEN;              /* make the device available again */
        dev_param_p->flags &= ~WAITING_FOR_READ_DATA; /* and reset this flag, too */

            /*
             * If the user process was interrupted, we may have to reset state.
             * If the device is DEAD, the first operation on the next open will
             * fail, and reset the state.
             */
    
        dev_param_p->write_state = WS_IDLE;
        dev_param_p->read_state = RS_IDLE;

        if (dev_param_p->read_dma_buf_p) {
        
                DPRINTK("freeing read buffer 0x%p\n", dev_param_p->read_dma_buf_p);
                free_pages((unsigned long) dev_param_p->read_dma_buf_p, dma_buff_order);
                dev_param_p->read_dma_buf_p = 0;
        }
        if (dev_param_p->write_dma_buf_p) {
        
                DPRINTK("freeing write buffer 0x%p\n", dev_param_p->write_dma_buf_p);
                free_pages((unsigned long) dev_param_p->write_dma_buf_p, dma_buff_order);
                dev_param_p->write_dma_buf_p = 0;
        }

#ifdef INSTRUMENT
        ++dev_param_p->close_cnt;
#endif/*INSTRUMENT*/
    
        MOD_DEC_USE_COUNT;
        dev_param_p->flags &= ~DEV_OPEN;
        up(&dev_param_p->aep_sem);    
        DPRINTK("returning %d\n", retval);
        return retval;
}

/*
 *  Interrupt handler routine
 *
 *  Called when:
 *  1) a DMA transfer has completed successfully
 *  2) a DMA transfer has completed unsuccessfully due to a bad transfer
 *  3) a DMA transfer has completed unsuccessfully due to lack of space
 *  4) the target has data to be read
 *
 */
void aep_interrupt_handler(int irq, void *dev_id_p, struct pt_regs *regs_p)
{
        register struct pci_dev   *dev_p = (struct pci_dev*) dev_id_p;
        register paep_unit_info_t *dev_param_p = dev_p->driver_data;

        u32 status;

        assert(dev_id_p && regs_p);
    
        BANNER();

#ifdef INSTRUMENT
        ++dev_param_p->interrupt_cnt;
#endif /* INSTRUMENT */

        status = read_pci_status(dev_p); 

            /* if it's anything but the doorbell int, then it's not ours */
        if (!(status & PCI_DOORBELL_INT))
                return;

            /* figure out which doorbell interrupt woke us */
        status = read_doorbell_status(dev_p);

            /* -spc- We handle every possible interrupt here, so that if more
             * than one cause has asserted we don't leave this handler without
             * clearing it by setting the relevant doorbell bits 
             */

        if (status & SIGNAL_DRIVER) {

#ifdef INSTRUMENT
                ++dev_param_p->SIGNAL_DRIVER_int_cnt;
#endif/*INSTRUMENT*/
        
                acknowledge_irq(SIGNAL_DRIVER, dev_p);

		dev_param_p->flags |= READ_DATA_AVAILABLE;

  		/*Post the semaphore controlling reads to the device*/
		up(&dev_param_p->read_sem);
		wake_up_interruptible(&dev_param_p->select_wq);

        }

        if (status & DMA_WRITE_COMPLETE) {

                acknowledge_irq(DMA_WRITE_COMPLETE, dev_p);
        
#ifdef INSTRUMENT
                ++dev_param_p->DMA_WRITE_COMPLETE_int_cnt;
#endif/*INSTRUMENT*/
        
                dev_param_p->write_state = WS_DMA_COMPLETE;
                wake_up_interruptible(&dev_param_p->wq);
        }

        if (status & DMA_WRITE_NO_SPACE) {

                acknowledge_irq(DMA_WRITE_NO_SPACE, dev_p);
        
#ifdef INSTRUMENT
                ++dev_param_p->DMA_WRITE_NO_SPACE_int_cnt;
#endif/*INSTRUMENT*/
                dev_param_p->write_fail_reason = DMA_WRITE_NO_SPACE;
                wake_up_interruptible(&dev_param_p->wq);
        }

        if (status & DMA_WRITE_ERROR) {

                acknowledge_irq(DMA_WRITE_ERROR, dev_p);

#ifdef INSTRUMENT
                ++dev_param_p->DMA_WRITE_ERROR_int_cnt;
#endif/*INSTRUMENT*/
                dev_param_p->write_fail_reason = DMA_WRITE_ERROR;
                wake_up_interruptible(&dev_param_p->wq);     
        }

        if (status & DMA_READ_COMPLETE) {

                acknowledge_irq(DMA_READ_COMPLETE, dev_p);

#ifdef INSTRUMENT
                ++dev_param_p->DMA_READ_COMPLETE_int_cnt;
#endif/*INSTRUMENT*/

                    /* save how many didn't get transferred */
                dev_param_p->read_residual = read_mailbox_status(dev_p);
                dev_param_p->read_state = RS_DMA_COMPLETE;
                wake_up_interruptible(&dev_param_p->rq);       
        }

        if (status & DMA_READ_ERROR) {

                acknowledge_irq(DMA_READ_ERROR, dev_p);

#ifdef INSTRUMENT
                ++dev_param_p->DMA_READ_ERROR_int_cnt;
#endif/* INSTRUMENT */

                wake_up_interruptible(&dev_param_p->rq);      
        }
        return;
}


/*
 *  Module Initialization (find it on the PCI bus)
 */

static int __init aep_init_module(void) 

{
        int count;

        BANNER();

        printk("%s%s%s%s\n", aep_version_str, DRIVER_VERSION, " Build Date: ", __DATE__);
        DPRINTK("AEP_MAXCARDS_NB = %d\n", AEP_MAXCARDS_NB);

        dev_list_pp = NULL;
        dma_buff_order = get_order(AEP_DMA_BUFFER_SIZE);
   
        DPRINTK("dma_buff_order = %d\n", dma_buff_order);
        DPRINTK("allocating pointer list\n");
    
        dev_list_pp = (struct pci_dev **) kmalloc(AEP_MAXCARDS_NB * sizeof(struct pci_dev*), GFP_KERNEL);
        DPRINTK("dev_list_pp = 0x%p\n", dev_list_pp);

        if (!dev_list_pp) {
        
                ERR_PRINTK("Pointer list allocation failed (returning -ENOMEM)\n");
                return -ENOMEM;
        }    

        for (count = 0; count < AEP_MAXCARDS_NB; count++){

                DPRINTK("zeroing pointer for card %d\n", count);
                dev_list_pp[count] = NULL;
        }

        if (pci_present()){

                if (pci_register_driver(&aep_driver) <= 0) {

                        ERR_PRINTK("no devices (returning -ENODEV)\n");
                        pci_unregister_driver(&aep_driver);
                        return -ENODEV;
                }
        }
    
        DPRINTK("returning\n");
        return 0;
}

static void __exit aep_cleanup_module(void) 


{
        DPRINTK("unregistering driver\n");
        pci_unregister_driver(&aep_driver);    

        nb_cards = 0;

        DPRINTK("freeing list of pointers 0x%p\n", dev_list_pp);
        kfree(dev_list_pp);
        dev_list_pp = NULL;
}

    
/* setup data for one AEP card,
   NOTE: pci_device_id can be used in the future if different
   initialisation methods are required for different chipsets */
static int __devinit aep_init_one_device (struct pci_dev *dev_p,
                                          const struct pci_device_id *unused_p)
{
        paep_unit_info_t *dev_param_p;
    
        unsigned long bar1;
        unsigned long bar3;
        int major,result;   

        assert(dev_p);
    
        BANNER();
    
        exit_if_too_many_cards(nb_cards);

        DPRINTK("allocating unit info struct\n");
        dev_param_p = (paep_unit_info_t *) kmalloc(sizeof(paep_unit_info_t), GFP_KERNEL);

        if (!dev_param_p) {

                ERR_PRINTK("Couldn't allocate memory for device information (returning -ENOMEM)\n");
                return -ENOMEM;
        }
    
        DPRINTK("dev_param_p = 0x%p\n", dev_param_p);

        dev_p->driver_data = dev_param_p;    
        dev_param_p->major = 0;
    
        dev_param_p->int_inuse = 0;
        dev_param_p->flags = 0;
        dev_param_p->config_space = 0;
        dev_param_p->mem_space = 0;
        dev_param_p->read_dma_buf_p = 0;
        dev_param_p->write_dma_buf_p = 0;
        init_waitqueue_head(&dev_param_p->rq);
        init_waitqueue_head(&dev_param_p->drq);
        init_waitqueue_head(&dev_param_p->wq);
	init_waitqueue_head(&dev_param_p->select_wq);
        dev_param_p->read_state = RS_IDLE;
        dev_param_p->write_state = WS_IDLE;
        dev_param_p->read_residual = 0;
        dev_param_p->last_read_dma = 0;
        dev_param_p->last_write_dma = 0;

#ifdef INSTRUMENT

        dev_param_p->open_cnt = 0;
        dev_param_p->open_busy_cnt = 0;
        dev_param_p->close_cnt = 0;

        dev_param_p->read_cnt = 0;
        dev_param_p->read_dma_cnt = 0;
        dev_param_p->read_busy_cnt = 0;
        dev_param_p->read_byte_cnt = 0;
        dev_param_p->write_cnt = 0;
        dev_param_p->write_dma_cnt = 0;
        dev_param_p->write_busy_cnt = 0;
        dev_param_p->write_busy_cnt2 = 0;
        dev_param_p->write_byte_cnt = 0;

        dev_param_p->interrupt_cnt = 0;
        dev_param_p->SIGNAL_DRIVER_int_cnt = 0;
        dev_param_p->DMA_WRITE_COMPLETE_int_cnt = 0;
        dev_param_p->DMA_WRITE_NO_SPACE_int_cnt = 0;
        dev_param_p->DMA_WRITE_ERROR_int_cnt = 0;
        dev_param_p->DMA_READ_COMPLETE_int_cnt = 0;
        dev_param_p->DMA_READ_ERROR_int_cnt = 0;

#endif /* INSTRUMENT */

            /* set up mapping to PCI device addresses */
        DPRINTK("mapping PCI\n");

        bar1 = pci_resource_start(dev_p, 0);
        DPRINTK("bar1 = 0x%08lX\n", bar1);
        dev_param_p->config_space = ioremap(bar1, AEP_CONFIG_SIZE);
        DPRINTK("config_space = 0x%p\n", dev_param_p->config_space);
    
        if (!dev_param_p->config_space) {

                ERR_PRINTK("Unable to map PCI config space (returning -ENOMEM)\n");
                kfree(dev_param_p);
                return -ENOMEM;
        }

        bar3 = pci_resource_start(dev_p, 2);
        DPRINTK("bar3 = 0x%08lX\n", bar3);
        dev_param_p->mem_space = ioremap(bar3, AEP_MEM_SIZE);
        DPRINTK("mem_space = 0x%p\n", dev_param_p->mem_space);

        if (!dev_param_p->mem_space) {

                ERR_PRINTK("Unable to map PCI memory space (returning -ENOMEM)\n");
                iounmap(dev_param_p->config_space);
                kfree(dev_param_p);
                return -ENOMEM;
        }

        init_MUTEX(&(dev_param_p->aep_sem));

     	init_MUTEX(&(dev_param_p->read_sem));
 
    
            /* create and register _PROPERLY_ /dev entry in device filesystem
             * NOTE: unlike the driver for the 2.2 kernel, each card has a different major number
             */
        sprintf(dev_p->name, "%s%d", AEP_DEVICE_NAME, nb_cards);
        major = devfs_register_chrdev(dev_param_p->major, dev_p->name, &aep_fops);
    
        if (major < 0) {
        
                ERR_PRINTK("Unable to get major for aep device (returning %d)\n", major);
                return major;
        }

        dev_param_p->dev_fs_handle = devfs_register(NULL, dev_p->name, DEVFS_FL_DEFAULT,
                                                    dev_param_p->major, nb_cards, S_IFCHR | S_IRUGO | S_IWUSR,
                                                    &aep_fops, NULL);
    
        if (dev_param_p->major == 0)
                dev_param_p->major = major; 

				DPRINTK("successfully registered devfs entry %s (major=%d)\n",
					dev_p->name, dev_param_p->major);                    

				/*See the comment for the open routine for an explanation for why we're
				requesting the irq here*/
                DPRINTK("requesting irq %d\n", dev_p->irq);
                result = request_irq(dev_p->irq, aep_interrupt_handler,
                                     SA_INTERRUPT | SA_SHIRQ | SA_SAMPLE_RANDOM,
                                     AEP_DEVICE_NAME, dev_p);
                if (result) {
            
                        ERR_PRINTK("Can't get assigned irq %d (returning -EBUSY)\n", dev_p->irq);
#ifdef INSTRUMEN
                        ++dev_param_p->open_busy_cnt;
#endif/*INSTRUMENT*/
                        return( -EBUSY);
                }
        
                dev_param_p->int_inuse = 1;

#ifdef AEP_HAS_PROC_ENTRY

        if (proc_entry_owner_p == NULL) {
        
                if ((dev_p->procent = create_proc_entry(AEP_DEVICE_NAME, S_IRUGO | S_IWUSR, NULL)) != NULL) {

                            /* NOTE: I think there is no need to protect this section by a mutex,
                             * as the driver registers each device sequentially one after another.
                             * The first device will become the owner of the /proc entry. VF
                             */
                        dev_p->procent->read_proc = aep_read_procmem;
                        proc_entry_owner_p = dev_p;
                        DPRINTK("created proc entry %s successfully\n", AEP_DEVICE_NAME);   
                } else
        
                        WARN_PRINTK("Failed to create proc entry %s\n", AEP_DEVICE_NAME);
        }
    
#endif /* AEP_HAS_PROC_ENTRY */

        dev_list_pp[nb_cards] = dev_p;
    
        nb_cards++;
    
        DPRINTK("returning\n");
        return 0;
}


/* unmap bar1 and bar3, free corresponding PCI resources */
static void __devexit aep_remove_one_device (struct pci_dev *rem_dev_p)
{
        int dev_num = 0;
        int removed = 0;
    
        struct pci_dev   *dev_p = NULL;
        paep_unit_info_t *dev_param_p = NULL;
    
        assert(rem_dev_p);
    
        BANNER();

        if (dev_list_pp)
        
                for (dev_num = 0; dev_num < AEP_MAXCARDS_NB; dev_num++) {

                        dev_p = dev_list_pp[dev_num];

                        if (dev_p)
                
                                if (dev_p == rem_dev_p) {

                                        removed = dev_num;
                                        dev_param_p = dev_p->driver_data;
                    
                                        DPRINTK("cleaning up card %d\n", dev_num);

            /* clean up resources */
        if (dev_param_p->int_inuse) {
        
                DPRINTK("freeing irq\n");
                free_irq(dev_p->irq, dev_p);
                dev_param_p->int_inuse = 0;
        
        } else 
        
                WARN_PRINTK("IRQ not in use, won't free\n");


#ifdef AEP_HAS_PROC_ENTRY
                                        if (dev_p == proc_entry_owner_p) {

                                                DPRINTK("removing proc entry %s\n", dev_p->procent->name);
                                                remove_proc_entry(dev_p->procent->name, NULL);
                                        }
#endif /* AEP_HAS_PROC_ENTRY */   
                                        devfs_unregister_chrdev(dev_param_p->major, dev_p->name);
                                        devfs_unregister(dev_param_p->dev_fs_handle);

                                        DPRINTK("successfully un-registered devfs entry %s (major=%d)\n",
                                                dev_p->name, dev_param_p->major);                    

                                            /* release all resources associated with each card */
                                        if (dev_param_p->config_space) {
                                                DPRINTK("     unmapping config space 0x%p\n", dev_param_p->config_space);
                                                iounmap(dev_param_p->config_space);
                                        }

                                        if (dev_param_p->mem_space) {
                                                DPRINTK("     unmapping register space 0x%p\n", dev_param_p->mem_space);
                                                iounmap(dev_param_p->mem_space);
                                        }

                                        if (dev_param_p) {
                                                DPRINTK("     freeing card info struct 0x%p\n", dev_param_p);
                                                kfree(dev_param_p);
                                                dev_param_p = NULL;
                                        }
                    
                                        dev_list_pp[removed] = NULL;
                                        nb_cards--;
                                } 
                }   
}


module_init(aep_init_module); 
module_exit(aep_cleanup_module);
