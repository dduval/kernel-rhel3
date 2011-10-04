/*
 *  scsi_queue.c Copyright (C) 1997 Eric Youngdale
 *
 *  generic mid-level SCSI queueing.
 *
 *  The point of this is that we need to track when hosts are unable to
 *  accept a command because they are busy.  In addition, we track devices
 *  that cannot accept a command because of a QUEUE_FULL condition.  In both
 *  of these cases, we enter the command in the queue.  At some later point,
 *  we attempt to remove commands from the queue and retry them.
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp_lock.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"

/*
 * TODO:
 *      1) Prevent multiple traversals of list to look for commands to
 *         queue.
 *      2) Protect against multiple insertions of list at the same time.
 * DONE:
 *      1) Set state of scsi command to a new state value for ml queue.
 *      2) Insert into queue when host rejects command.
 *      3) Make sure status code is properly passed from low-level queue func
 *         so that internal_cmnd properly returns the right value.
 *      4) Insert into queue when QUEUE_FULL.
 *      5) Cull queue in bottom half handler.
 *      6) Check usage count prior to queue insertion.  Requeue if usage
 *         count is 0.
 *      7) Don't send down any more commands if the host/device is busy.
 */

static const char RCSid[] = "$Header: /mnt/ide/home/eric/CVSROOT/linux/drivers/scsi/scsi_queue.c,v 1.1 1997/10/21 11:16:38 eric Exp $";

/*
 * Function:	scsi_timeout_block()
 *
 * Purpose:	Unblock a host or device after a time interval has passed
 *
 * Arguments:	cmd    - command that provides reference to either the host
 *			 or device to be unblocked.
 *
 * Lock Status: No locks held (called by core timer expiration code)
 *
 * Returns:	Nothing
 *
 * Note:	We enter here for either a blocked host or device.  We simply
 *		unblock both and then call the request queue for the
 *		device passed in.  That will get things going again.  If
 *		it was the host that was blocked, then we'll be nice and
 *		call request_fn for all the device queues.
 */
void scsi_timeout_block(Scsi_Cmnd * cmd)
{
	int host_was_blocked = cmd->host->host_blocked;
	struct scsi_device *device = cmd->device;
	struct request_queue *q = &device->request_queue;
	struct request *req;
	unsigned long flags;

	device->device_blocked = 0;
	device->unblock_timer_active = 0;
	spin_lock_irqsave(cmd->host->host_lock, flags);
	cmd->host->host_blocked = 0;
	for (device = cmd->host->host_queue; device; device = device->next)
		if (device->unblock_timer_active)
			break;
	if (!device)
		cmd->host->unblock_timer_active = 0;
	spin_unlock_irqrestore(cmd->host->host_lock, flags);

	if (cmd->host->eh_wait != NULL && cmd->host->in_recovery) {
		/*
		 * We don't goose any queues if we are in recovery.
		 * Instead we wait until all timers that are already set
		 * have fired and host_busy == host_failed.  If all the
		 * commands have already finished, then we have to start
		 * the eh thread (it's normally started at the end of
		 * command completion processing, but it won't get started
		 * if one of these timers is active).
		 */
		if (cmd->host->unblock_timer_active == 0 &&
		    atomic_read(&cmd->host->host_busy) == cmd->host->host_failed)
			up(cmd->host->eh_wait);
	} else if (!host_was_blocked) {
		spin_lock_irq(q->queue_lock);
		q->request_fn(q);
		spin_unlock_irq(q->queue_lock);
	} else {
		/*
		 * OK, the host was blocked.  Walk the device queue and kick
		 * any devices that aren't blocked.
		 */
		for (device = cmd->host->host_queue;
		     device;
		     device = device->next) {
			if (device->device_blocked)
				continue;
			q = &device->request_queue;
			spin_lock_irq(q->queue_lock);
			q->request_fn(q);
			spin_unlock_irq(q->queue_lock);
		}
	}
}
		

/*
 * Function:    scsi_mlqueue_insert()
 *
 * Purpose:     Insert a command in the midlevel queue.
 *
 * Arguments:   cmd    - command that we are adding to queue.
 *              reason - why we are inserting command to queue.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing.
 *
 * Notes:       We do this for one of two cases.  Either the host is busy
 *              and it cannot accept any more commands for the time being,
 *              or the device returned QUEUE_FULL and can accept no more
 *              commands.
 * Notes:       This could be called either from an interrupt context or a
 *              normal process context.
 */
int scsi_mlqueue_insert(Scsi_Cmnd * cmd, int reason)
{
	struct Scsi_Host *host;
	struct scsi_device *device;
	struct request_queue *q;
	unsigned long flags;

	SCSI_LOG_MLQUEUE(1, printk("Inserting command %p into mlqueue\n", cmd));

	host = cmd->host;
	device = cmd->device;
	q = &device->request_queue;

	/*
	 * Decrement the counters, since these commands are no longer
	 * active on the host/device.
	 */
	atomic_dec(&host->host_busy);
	atomic_dec(&device->device_busy);

	/* Clear any bad state info from the last try before putting back
	 * on the queue.
	 */
	scsi_setup_cmd_retry(cmd);
	memset((void *)&cmd->sense_buffer, 0, sizeof cmd->sense_buffer);

	/*
	 * Register the fact that we own the thing for now.
	 */
	cmd->state = SCSI_STATE_MLQUEUE;
	cmd->owner = SCSI_OWNER_MIDLEVEL;

	if (host->eh_wait != NULL && host->in_recovery) {
		/*
		 * We don't goose any queues if we are in recovery.
		 * We don't need to start the eh thread either,
		 * scsi_softirq_handler will do so if we are ready.
		 */
		spin_lock_irqsave(q->queue_lock, flags);
		list_add(&cmd->sc_list, &device->sdev_retry_q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	} else if (reason == SCSI_MLQUEUE_HOST_BUSY) {
		/*
		 * Next, set the appropriate busy bit for the device/host.
		 */
		spin_lock_irqsave(host->host_lock, flags);
		host->host_blocked = TRUE;
		if (atomic_read(&host->host_busy) == 0 &&
		    !host->unblock_timer_active) {
			scsi_add_timer(cmd, HZ/5, scsi_timeout_block);
			host->unblock_timer_active = 1;
		}
		spin_unlock(host->host_lock);
		spin_lock(q->queue_lock);
		list_add(&cmd->sc_list, &device->sdev_retry_q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	} else {
		spin_lock_irqsave(q->queue_lock, flags);
		device->device_blocked = TRUE;
		list_add(&cmd->sc_list, &device->sdev_retry_q);
		if (atomic_read(&device->device_busy) == 0) {
			scsi_add_timer(cmd, HZ/5, scsi_timeout_block);
			device->unblock_timer_active = 1;
			spin_unlock(q->queue_lock);
			spin_lock(host->host_lock);
			host->unblock_timer_active = 1;
			spin_unlock_irqrestore(host->host_lock, flags);
		} else
			spin_unlock_irqrestore(q->queue_lock, flags);
	}

	return 0;
}
