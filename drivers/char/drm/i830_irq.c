/* i830_dma.c -- DMA support for the I830 -*- linux-c -*-
 *
 * Copyright 2002 Tungsten Graphics, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Keith Whitwell <keith@tungstengraphics.com>
 *
 */


#include "i830.h"
#include "drmP.h"
#include "drm.h"
#include "i830_drm.h"
#include "i830_drv.h"
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>


void DRM(dma_service)(int irq, void *device, struct pt_regs *regs)
{
	drm_device_t	 *dev = (drm_device_t *)device;
      	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
   	u16 temp;
   
      	temp = I830_READ16(I830REG_INT_IDENTITY_R);
	printk("%s: %x\n", __FUNCTION__, temp);
	
   	if(temp == 0) 
	   return;

	I830_WRITE16(I830REG_INT_IDENTITY_R, temp); 

	if (temp & 2) {
		atomic_inc(&dev_priv->irq_received);
		wake_up_interruptible(&dev_priv->irq_queue); 
	}
}


int i830_emit_irq(drm_device_t *dev)
{
	drm_i830_private_t *dev_priv = dev->dev_private;
	RING_LOCALS;

	DRM_DEBUG("%s\n", __FUNCTION__);

	atomic_inc(&dev_priv->irq_emitted);

   	BEGIN_LP_RING(2);
      	OUT_RING( 0 );
      	OUT_RING( GFX_OP_USER_INTERRUPT );
      	ADVANCE_LP_RING();

	return atomic_read(&dev_priv->irq_emitted);
}


int i830_wait_irq(drm_device_t *dev, int irq_nr)
{
  	drm_i830_private_t *dev_priv = 
	   (drm_i830_private_t *)dev->dev_private;
	DECLARE_WAITQUEUE(entry, current);
	unsigned long end = jiffies + HZ*3;
	int ret = 0;

	DRM_DEBUG("%s\n", __FUNCTION__);

 	if (atomic_read(&dev_priv->irq_received) >= irq_nr)  
 		return 0; 

	dev_priv->sarea_priv->perf_boxes |= I830_BOX_WAIT;

	add_wait_queue(&dev_priv->irq_queue, &entry);

	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
	   	if (atomic_read(&dev_priv->irq_received) >= irq_nr) 
		   break;
		if (time_after(jiffies, end)) {
			DRM_ERROR("timeout iir %x imr %x ier %x hwstam %x\n",
				  I830_READ16( I830REG_INT_IDENTITY_R ),
				  I830_READ16( I830REG_INT_MASK_R ),
				  I830_READ16( I830REG_INT_ENABLE_R ),
				  I830_READ16( I830REG_HWSTAM ));

		   	ret = -EBUSY;	/* Lockup?  Missed irq? */
			break;
		}
	      	schedule_timeout(HZ*3);
	      	if (signal_pending(current)) {
		   	ret = -EINTR;
			break;
		}
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&dev_priv->irq_queue, &entry);
	return ret;
}


/* Needs the lock as it touches the ring.
 */
int i830_irq_emit( struct inode *inode, struct file *filp, unsigned int cmd,
		   unsigned long arg )
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_irq_emit_t emit;
	int result;

	LOCK_TEST_WITH_RETURN(dev);

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if (copy_from_user( &emit, (drm_i830_irq_emit_t *)arg, sizeof(emit) ))
		return -EFAULT;

	result = i830_emit_irq( dev );

	if ( copy_to_user( emit.irq_seq, &result, sizeof(int) ) ) {
		DRM_ERROR( "copy_to_user\n" );
		return -EFAULT;
	}

	return 0;
}


/* Doesn't need the hardware lock.
 */
int i830_irq_wait( struct inode *inode, struct file *filp, unsigned int cmd,
		   unsigned long arg )
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_irq_wait_t irqwait;

	if ( !dev_priv ) {
		DRM_ERROR( "%s called with no initialization\n", __FUNCTION__ );
		return -EINVAL;
	}

	if (copy_from_user( &irqwait, (drm_i830_irq_wait_t *)arg, 
			    sizeof(irqwait) ))
		return -EFAULT;

	return i830_wait_irq( dev, irqwait.irq_seq );
}

