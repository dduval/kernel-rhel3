/*
 * iSCSI driver for Linux
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi-devel@lists.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi-probe.c,v 1.7.2.2 2004/09/14 09:31:30 surekhap Exp $
 *
 */

#include <linux/config.h>
#include <linux/version.h>

#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dirent.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/timer.h>

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
# include <asm/semaphore.h>
#else
# include <asm/spinlock.h>
#endif
#include <asm/uaccess.h>
#include <scsi/sg.h>

/* these are from $(TOPDIR)/drivers/scsi, not $(TOPDIR)/include */
#include <scsi.h>
#include <hosts.h>

#include "iscsi-common.h"
#include "iscsi-ioctl.h"
#include "iscsi.h"

/* LUN probing needs to be serialized across all HBA's, to
 * keep a somewhat sane ordering 
 */

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
DECLARE_MUTEX(iscsi_lun_probe_mutex);
#else
struct semaphore iscsi_lun_probe_mutex = MUTEX;
#endif
spinlock_t iscsi_lun_probe_lock = SPIN_LOCK_UNLOCKED;
static iscsi_session_t *iscsi_lun_probe_head = NULL;
static iscsi_session_t *iscsi_lun_probe_tail = NULL;
static iscsi_session_t *iscsi_currently_probing = NULL;
static volatile int iscsi_next_probe = 0;
volatile unsigned long iscsi_lun_probe_start = 0;
#if 0
struct dirent {
    long d_ino;			/* inode number */
    off_t d_off;		/* offset to next dirent */
    unsigned short d_reclen;	/* length of this dirent */
    char d_name[1];		/* file name (null-terminated) */
};
#endif

/* caller must hold iscsi_lun_probe_lock */
static int
enqueue_lun_probe(iscsi_session_t * session)
{
    if (session->probe_next || session->probe_prev) {
	DEBUG_INIT("iSCSI: session %p already queued for LUN probing\n",
		   session);
	return 0;
    }

    if (iscsi_lun_probe_head) {
	if (session->probe_order < iscsi_lun_probe_head->probe_order) {
	    /* insert before the current head */
	    session->probe_prev = NULL;
	    session->probe_next = iscsi_lun_probe_head;
	    iscsi_lun_probe_head->probe_prev = session;
	    iscsi_lun_probe_head = session;
	} else if (session->probe_order >= iscsi_lun_probe_tail->probe_order) {
	    /* insert after the tail */
	    session->probe_next = NULL;
	    session->probe_prev = iscsi_lun_probe_tail;
	    iscsi_lun_probe_tail->probe_next = session;
	    iscsi_lun_probe_tail = session;
	} else {
	    /* insert somewhere in the middle */
	    iscsi_session_t *search = iscsi_lun_probe_head;
	    while (search && search->probe_next) {
		if (session->probe_order < search->probe_next->probe_order) {
		    session->probe_next = search->probe_next;
		    session->probe_prev = search;
		    search->probe_next->probe_prev = session;
		    search->probe_next = session;
		    break;
		}
		search = search->probe_next;
	    }
	}
    } else {
	/* become the only session in the queue */
	session->probe_next = session->probe_prev = NULL;
	iscsi_lun_probe_head = iscsi_lun_probe_tail = session;
    }
    return 1;
}

/* caller must hold iscsi_lun_probe_lock */
static void
dequeue_lun_probe(iscsi_session_t * session)
{
    if (iscsi_currently_probing == session) {
	/* the timer may have tried to start us probing just before we gave up */
	iscsi_currently_probing = NULL;
    } else {
	if (iscsi_lun_probe_head == session) {
	    if ((iscsi_lun_probe_head = iscsi_lun_probe_head->probe_next))
		iscsi_lun_probe_head->probe_prev = NULL;
	    else
		iscsi_lun_probe_tail = NULL;
	} else if (iscsi_lun_probe_tail == session) {
	    iscsi_lun_probe_tail = iscsi_lun_probe_tail->probe_prev;
	    iscsi_lun_probe_tail->probe_next = NULL;
	} else {
	    /* in the middle */
	    if (session->probe_next && session->probe_prev) {
		session->probe_prev->probe_next = session->probe_next;
		session->probe_next->probe_prev = session->probe_prev;
	    } else {
		printk("iSCSI: bug - dequeue_lun_probe %p, prev %p, next %p\n",
		       session, session->probe_prev, session->probe_next);
	    }
	}
    }
}

static int
wait_for_probe_order(iscsi_session_t * session)
{
    spin_lock(&iscsi_lun_probe_lock);
    if ((iscsi_currently_probing == session) || session->probe_next
	|| session->probe_prev) {
	/* we're already probing or queued to be probed,
	 * ignore the 2nd probe request 
	 */
	DEBUG_INIT("iSCSI: session %p to %s ignoring duplicate probe request\n",
		   session, session->log_name);
	spin_unlock(&iscsi_lun_probe_lock);
	return 0;
    } else if ((iscsi_currently_probing == NULL)
	       && (session->probe_order <= iscsi_next_probe)) {
	/* if there's no LUN being probed, and our
	 * probe_order can go now, start probing 
	 */
	DEBUG_INIT("iSCSI: session %p to %s, probe_order %d <= next %d, not "
		   "waiting\n",
		   session, session->log_name, session->probe_order,
		   iscsi_next_probe);
	iscsi_currently_probing = session;

	/* let the timer know another session became ready for LUN probing. */
	iscsi_lun_probe_start = (jiffies + (3 * HZ));
	if (iscsi_lun_probe_start == 0)
	    iscsi_lun_probe_start = 1;
	smp_mb();

	spin_unlock(&iscsi_lun_probe_lock);
	return 1;
    } else if (enqueue_lun_probe(session)) {
	/* otherwise queue up based on our probe order */

	/* tell the timer when to start the LUN probing, to
	 * handle gaps in the probe_order 
	 */
	iscsi_lun_probe_start = (jiffies + (3 * HZ)) ? (jiffies + (3 * HZ)) : 1;
	smp_mb();
	DEBUG_INIT("iSCSI: queued session %p for LUN probing, probe_order %d, "
		   "probe_start at %lu\n",
		   session, session->probe_order, iscsi_lun_probe_start);

	spin_unlock(&iscsi_lun_probe_lock);

	/* and wait for either the timer or the currently
	 * probing session to wake us up 
	 */
	if (down_interruptible(&session->probe_sem)) {
	    printk("iSCSI: session %p to %s interrupted while waiting to probe "
		   "LUNs\n", session, session->log_name);
	    /* give up and take ourselves out of the lun
	     * probing data structures 
	     */
	    spin_lock(&iscsi_lun_probe_lock);
	    dequeue_lun_probe(session);
	    spin_unlock(&iscsi_lun_probe_lock);
	    return 0;
	}

	/* give up if the session is terminating */
	if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    printk("iSCSI: session %p to %s terminated while waiting to "
		   "probe LUNs\n", session, session->log_name);
	    /* give up and take ourselves out of the lun
	     * probing data structures */
	    spin_lock(&iscsi_lun_probe_lock);
	    dequeue_lun_probe(session);
	    spin_unlock(&iscsi_lun_probe_lock);
	    return 0;
	}
#ifdef DEBUG
	/* we should be out of the queue, and in iscsi_currently_probing */
	spin_lock(&iscsi_lun_probe_lock);
	if (iscsi_currently_probing != session)
	    printk("iSCSI: bug - currently probing should be %p, not %p\n",
		   session, iscsi_currently_probing);
	spin_unlock(&iscsi_lun_probe_lock);
#endif
	DEBUG_INIT("iSCSI: wait_for_probe_order %p returning 1\n", session);
	return 1;
    }

    /* silently fail, since the enqueue attempt will have
     * logged any detailed messages needed 
     */
    spin_unlock(&iscsi_lun_probe_lock);
    return 0;
}

/* caller must hold iscsi_lun_probe_lock */
static void
start_next_lun_probe(void)
{
    if (iscsi_currently_probing) {
	printk("iSCSI: bug - start_next_lun_probe called while currently "
	       "probing %p at %lu\n", iscsi_currently_probing, jiffies);
    } else if (iscsi_lun_probe_head) {
	/* pop one off the queue, and tell it to start probing */
	iscsi_currently_probing = iscsi_lun_probe_head;
	if ((iscsi_lun_probe_head = iscsi_currently_probing->probe_next))
	    iscsi_lun_probe_head->probe_prev = NULL;
	else
	    iscsi_lun_probe_tail = NULL;

	/* it's out of the queue now */
	iscsi_currently_probing->probe_next = NULL;
	iscsi_currently_probing->probe_prev = NULL;

	/* skip over any gaps in the probe order */
	if (iscsi_next_probe < iscsi_currently_probing->probe_order) {
	    DEBUG_INIT("iSCSI: LUN probe_order skipping from %d to %d\n",
		       iscsi_next_probe, iscsi_currently_probing->probe_order);
	    iscsi_next_probe = iscsi_currently_probing->probe_order;
	    smp_mb();
	}

	/* wake up the ioctl which is waiting to do a probe */
	DEBUG_INIT("iSCSI: starting LUN probe for session %p to %s\n",
		   iscsi_currently_probing, iscsi_currently_probing->log_name);
	up(&iscsi_currently_probing->probe_sem);
    } else {
	/* if there is nothing else queued, then we don't
	 * need the timer to keep checking, and we want to
	 * reset the probe order so that future LUN probes
	 * get queued, and maintain the proper relative
	 * order amonst themselves, even if the global order
	 * may have been lost.
	 */
	DEBUG_INIT("iSCSI: start_next_lun_probe has nothing to start, "
		   "resetting next LUN probe from %d to 0 at %lu\n",
		   iscsi_next_probe, jiffies);
	iscsi_lun_probe_start = 0;
	iscsi_next_probe = 0;
	smp_mb();
    }
}

void
iscsi_possibly_start_lun_probing(void)
{
    spin_lock(&iscsi_lun_probe_lock);
    if (iscsi_currently_probing == NULL) {
	/* if we're not probing already, make sure we start */
	DEBUG_INIT("iSCSI: timer starting LUN probing at %lu\n", jiffies);
	start_next_lun_probe();
    }
    spin_unlock(&iscsi_lun_probe_lock);
}

static void
iscsi_probe_finished(iscsi_session_t * session)
{
    spin_lock(&iscsi_lun_probe_lock);
    if (iscsi_currently_probing == session) {
	iscsi_currently_probing = NULL;
	DEBUG_INIT("iSCSI: session %p to %s finished probing LUNs at %lu\n",
		   session, session->log_name, jiffies);

	/* continue through the probe order */
	if (iscsi_next_probe == session->probe_order)
	    iscsi_next_probe++;

	/* and possibly start another session probing */
	if (iscsi_lun_probe_head == NULL) {
	    /* nothing is queued, reset LUN probing */
	    DEBUG_INIT("iSCSI: probe_finished has nothing to start, "
		       "resetting next LUN probe from %d to 0 at %lu\n",
		       iscsi_next_probe, jiffies);
	    iscsi_next_probe = 0;
	    iscsi_lun_probe_start = 0;
	    smp_mb();
	} else if ((iscsi_lun_probe_head->probe_order <= iscsi_next_probe) ||
		   (iscsi_lun_probe_start
		    && time_before_eq(iscsi_lun_probe_start, jiffies))) {
	    /* next in order is up, or the timer has expired, start probing */
	    start_next_lun_probe();
	} else {
	    DEBUG_INIT("iSCSI: iscsi_probe_finished can't start_next_lun_probe "
		       "at %lu, next %d, head %p (%d), tail %p (%d), current "
		       "%p, start time %lu\n",
		       jiffies, iscsi_next_probe, iscsi_lun_probe_head,
		       iscsi_lun_probe_head ? iscsi_lun_probe_head->
		       probe_order : -1, iscsi_lun_probe_tail,
		       iscsi_lun_probe_tail ? iscsi_lun_probe_tail->
		       probe_order : -1, iscsi_currently_probing,
		       iscsi_lun_probe_start);
	}
    } else {
	/* should be impossible */
	printk("iSCSI: bug - session %p in iscsi_probe_finished, but currently "
	       "probing %p\n", session, iscsi_currently_probing);
    }
    spin_unlock(&iscsi_lun_probe_lock);
}

/* try to write to /proc/scsi/scsi */
static int
write_proc_scsi_scsi(iscsi_session_t * session, char *str)
{
    struct file *filp = NULL;
    loff_t offset = 0;
    int rc = 0;
    mm_segment_t oldfs = get_fs();

    set_fs(get_ds());

    filp = filp_open("/proc/scsi/scsi", O_WRONLY, 0);
    if (IS_ERR(filp)) {
	printk("iSCSI: session %p couldn't open /proc/scsi/scsi\n", session);
	set_fs(oldfs);
	return -ENOENT;
    }

    rc = filp->f_op->write(filp, str, strlen(str), &offset);
    filp_close(filp, 0);
    set_fs(oldfs);

    if (rc >= 0) {
	/* assume it worked, since the non-negative return
	 * codes aren't set very reliably.  wait for 20 ms
	 * to avoid deadlocks on SMP systems.  FIXME: figure
	 * out why the SMP systems need this wait, and fix
	 * the kernel.
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(20));
	return 1;
    }

    return rc;
}

/* caller must hold the iscsi_lun_probe_mutex */
static int
iscsi_probe_lun(iscsi_session_t * session, int lun)
{
    char str[80];
    int rc;

    if (lun >= ISCSI_MAX_LUN)
	return 0;

    session->lun_being_probed = lun;
    sprintf(str, "scsi add-single-device %d %d %d %d\n",
	    session->host_no, session->channel, session->target_id, lun);
    str[sizeof (str) - 1] = '\0';

    rc = write_proc_scsi_scsi(session, str);
    session->lun_being_probed = -1;

    if (rc < 0) {
	/* clear the newline */
	str[strlen(str) - 1] = '\0';
	printk("iSCSI: session %p error %d writing '%s' to /proc/scsi/scsi\n",
	       session, rc, str);
	return 0;
    }

    return rc;
}

static int
iscsi_remove_lun(iscsi_session_t * session, int lun)
{
    char str[88];
    int rc = 0;

    sprintf(str, "scsi remove-single-device %d %d %d %d\n",
	    session->host_no, session->channel, session->target_id, lun);
    str[sizeof (str) - 1] = '\0';

    rc = write_proc_scsi_scsi(session, str);
    if (rc < 0) {
	/* clear the newline */
	str[strlen(str) - 1] = '\0';
	printk("iSCSI: session %p error %d writing '%s' to /proc/scsi/scsi\n",
	       session, rc, str);
	return 0;
    } else {
	/* removed it */
	clear_bit(lun, session->luns_activated);
	clear_bit(lun, session->luns_detected);
	return 1;
    }

    return rc;
}

void
iscsi_remove_luns(iscsi_session_t * session)
{
    int l;

    /* try to release the kernel's SCSI device structures for every LUN */
    down(&iscsi_lun_probe_mutex);

    for (l = 0; l < ISCSI_MAX_LUN; l++) {
	if (test_bit(l, session->luns_activated)) {
	    /* tell Linux to release the Scsi_Devices */
	    iscsi_remove_lun(session, l);
	}
    }

    up(&iscsi_lun_probe_mutex);
}

/* compute the intersection of the LUNS detected and
 * configured, and probe each LUN 
 */
void
iscsi_probe_luns(iscsi_session_t * session, uint32_t * lun_bitmap)
{
    int l;
    int detected = 0;
    int probed = 0;
    int activated = 0;

    /* try wait for our turn to probe, to keep the device
     * node ordering as repeatable as possible 
     */
    DEBUG_INIT("iSCSI: session %p to %s waiting to probe LUNs at %lu, probe "
	       "order %d\n",
	       session, session->log_name, jiffies, session->probe_order);

    if (!wait_for_probe_order(session)) {
	DEBUG_INIT("iSCSI: session %p to %s couldn't probe LUNs, error waiting "
		   "for probe order\n", session, session->log_name);
	return;
    }

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	printk("iSCSI: session %p to %s terminated while waiting to probe "
	       "LUNs\n", session, session->log_name);
	goto done;
    }
    if (signal_pending(current)) {
	printk("iSCSI: session %p ioctl killed while waiting to probe LUNs\n",
	       session);
	goto done;
    }

    /* make sure we're the only driver process trying to add or remove LUNs */
    if (down_interruptible(&iscsi_lun_probe_mutex)) {
	printk("iSCSI: session %p to %s interrupted while probing LUNs\n",
	       session, session->log_name);
	goto done;
    }

    /* need to set the host's max_channel, max_id, max_lun, since we
     * zero them in iscsi_detect in order to disable the scan that
     * occurs during scsi_register_host.  
     */
    session->hba->host->max_id = ISCSI_MAX_TARGET_IDS_PER_BUS;
    session->hba->host->max_lun = ISCSI_MAX_LUNS_PER_TARGET;
    session->hba->host->max_channel = ISCSI_MAX_CHANNELS_PER_HBA - 1;	/* convert 
									 * from 
									 * count to 
									 * index 
									 */
    smp_mb();

    DEBUG_INIT("iSCSI: probing LUNs for session %p to %s at %lu, "
	       "probe_order %d at %lu\n",
	       session, session->log_name, jiffies, session->probe_order,
	       jiffies);
    for (l = 0; l < ISCSI_MAX_LUN; l++) {
	if (test_bit(SESSION_TERMINATING, &session->control_bits))
	    goto give_up;
	if (signal_pending(current))
	    goto give_up;
	if (test_bit(l, session->luns_detected)) {
	    /* Check if lun has been removed */
	    if (!test_bit(l, session->luns_found))
		    iscsi_remove_lun(session, l);
	    else {
		detected++;

		/* if allowed and not already activated
		 * (successfully probed), probe it 
		 */
		if ((lun_bitmap[l / 32] & (1 << (l % 32)))
		    && !test_bit(l, session->luns_activated)) {
		    DEBUG_FLOW("iSCSI: session %p probing LUN %d at %lu\n",
			       session, l, jiffies);
		    iscsi_probe_lun(session, l);
		    probed++;
		    if (test_bit(l, session->luns_activated))
			activated++;
		}
	    }
	} else
	    if (test_bit(l, session->luns_activated))
		iscsi_remove_lun(session, l);
    }

    if (detected == 0) {
	printk("iSCSI: no LUNs detected for session %p to %s\n", session,
	       session->log_name);

    } else if (LOG_ENABLED(ISCSI_LOG_INIT)) {
	printk("iSCSI: session %p to %s probed %d of %d LUNs detected, %d "
	       "new LUNs activated\n",
	       session, session->log_name, probed, detected, activated);
    }

  give_up:
    up(&iscsi_lun_probe_mutex);

  done:
    /* clean up after wait_for_probe_order, and possibly
     * start the next session probing 
     */
    iscsi_probe_finished(session);
}

typedef struct iscsi_cmnd {
    struct semaphore done_sem;
    Scsi_Cmnd sc;
    unsigned int bufflen;
    uint8_t buffer[1];
} iscsi_cmnd_t;

/* callback function for Scsi_Cmnd's generated by the iSCSI driver itself */
void
iscsi_done(Scsi_Cmnd * sc)
{
    iscsi_cmnd_t *c = (iscsi_cmnd_t *) sc->buffer;

    up(&c->done_sem);
}

static int
iscsi_do_cmnd(iscsi_session_t * session, iscsi_cmnd_t * c,
	      unsigned int attempts_allowed)
{
    Scsi_Cmnd *sc = NULL;
    int queue_attempts = 0;

    if (c->sc.host) {
	DEBUG_FLOW("iSCSI: session %p iscsi_do_cmnd %p to (%u %u %u %u), "
		   "Cmd 0x%02x, %u retries, buffer %p, bufflen %u\n",
		   session, c, c->sc.host->host_no, c->sc.channel, c->sc.target,
		   c->sc.lun, c->sc.cmnd[0], attempts_allowed,
		   c->sc.request_buffer, c->sc.request_bufflen);
    } else {
	printk("iSCSI: session %p iscsi_do_cmnd %p, buffer %p, bufflen "
	       "%u, host %p\n",
	       session, c, c->sc.request_buffer, c->sc.request_bufflen,
	       c->sc.host);
	return 0;
    }
    if (!c->sc.request_buffer)
	return 0;
    if (!c->sc.request_bufflen)
	return 0;

    sc = &(c->sc);
    sc->retries = -1;
    sc->allowed = attempts_allowed;

  retry:
    sc->resid = 0;
    while (++sc->retries < sc->allowed) {
	if (signal_pending(current))
	    return 0;
	if (test_bit(SESSION_TERMINATING, &session->control_bits))
	    return 0;

	sc->result = 0;
	memset(sc->sense_buffer, 0, sizeof (sc->sense_buffer));
	memset(c->buffer, 0, c->bufflen);

	/* try to queue the command */
	queue_attempts = 0;
	for (;;) {
	    sema_init(&c->done_sem, 0);
	    smp_mb();

	    if (signal_pending(current))
		return 0;
	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		return 0;

	    DEBUG_INIT("iSCSI: detect_luns queueing %p to session %p at %lu\n",
		       sc, session, jiffies);

	    /* give up eventually, in case the replacement timeout is in effect.
	     * we don't want to loop forever trying to queue to a session
	     * that may never accept commands.
	     */
	    if (iscsi_queue(session, sc, iscsi_done)) {
		break;
	    } else if (queue_attempts++ >= 500) {
		/* give up after 10 seconds */
		return 0;
	    }

	    /* command not queued, wait a bit and try again */
	    set_current_state(TASK_UNINTERRUPTIBLE);
	    schedule_timeout(MSECS_TO_JIFFIES(20));
	}

	DEBUG_QUEUE("iSCSI: session %p queued iscsi_cmnd %p, buffer %p, "
		    "bufflen %u, scsi_done %p\n",
		    session, c, c->sc.request_buffer, c->sc.request_bufflen,
		    c->sc.scsi_done);

	/* wait til either the command completes, or we get signalled. */
	if (down_interruptible(&c->done_sem)) {
	    /* if we got signalled, squash the command and give up */
	    iscsi_squash_cmnd(session, sc);
	    return 0;
	}

	DEBUG_QUEUE("iSCSI: session %p hba %p host %p woken up by "
		    "iscsi_cmnd %p, buffer %p, bufflen %u\n",
		    session, session->hba, session->hba->host, c,
		    c->sc.request_buffer, c->sc.request_bufflen);

	/* the command completed, check the result and
	 * decide if it needs to be retried. 
	 */
	DEBUG_FLOW("iSCSI: session %p iscsi cmnd %p to (%u %u %u %u), "
		   "Cmd 0x%02x, host byte 0x%x, SCSI status 0x%x, "
		   "residual %u\n", session, c,
		   c->sc.host->host_no, c->sc.channel, c->sc.target,
		   c->sc.lun, c->sc.cmnd[0],
		   (sc->result >> 24) & 0xFF, sc->result & 0xFF, sc->resid);

	/* check the host byte */
	switch (host_byte(sc->result)) {
	case DID_OK:
	    /* no problems so far */
	    break;
	case DID_NO_CONNECT:
	    /* give up, we can't talk to the device */
	    printk("iSCSI: session %p failing iscsi cmnd %p to "
		   "(%u %u %u %u), Cmd 0x%02x, host byte 0x%x, SCSI "
		   "status 0x%x, residual %u\n", session, c,
		   c->sc.host->host_no, c->sc.channel, c->sc.target, c->sc.lun,
		   c->sc.cmnd[0], (sc->result >> 24) & 0xFF, sc->result & 0xFF,
		   sc->resid);
	    return 0;
	case DID_ERROR:
	case DID_SOFT_ERROR:
	case DID_ABORT:
	case DID_BUS_BUSY:
	case DID_PARITY:
	case DID_TIME_OUT:
	case DID_RESET:
	default:
	    if (LOG_ENABLED(ISCSI_LOG_INIT))
		printk("iSCSI: session %p iscsi cmnd %p to (%u %u %u %u), "
		       "Cmd 0x%02x, host byte 0x%x, SCSI status 0x%x, "
		       "residual %u\n", session,
		       c, c->sc.host->host_no, c->sc.channel, c->sc.target,
		       c->sc.lun, c->sc.cmnd[0], (sc->result >> 24) & 0xFF,
		       sc->result & 0xFF, sc->resid);

	    /* some sort of problem, possibly retry */
	    goto retry;
	}

	/* check the SCSI status byte.  Note, Linux values
	 * are right-shifted once compared to the SCSI
	 * spec 
	 */
	switch (status_byte(sc->result)) {
	case GOOD:
	case COMMAND_TERMINATED:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,19)
	    /* make sure we got enough of a response */
	    if (sc->resid
		&& ((iscsi_expected_data_length(sc) - sc->resid) <
		    sc->underflow)) {
		/* try again */
		if (LOG_ENABLED(ISCSI_LOG_INIT))
		    printk("iSCSI: session %p iscsi cmnd %p to (%u %u %u %u), "
			   "Cmd 0x%02x, residual %u, retrying to get %u bytes "
			   "desired\n",
			   session, c, c->sc.host->host_no, c->sc.channel,
			   c->sc.target, c->sc.lun, c->sc.cmnd[0], sc->resid,
			   sc->underflow);
		goto retry;
	    }
#endif
	    /* all done */
	    return 1;
	case BUSY:		/* device is busy, try again later */
	case QUEUE_FULL:	/* tagged queuing device has
				 * a full queue, wait a bit
				 * and try again. 
				 */
	    sc->allowed++;
	    if (sc->allowed > 100) {
		printk("iSCSI: session %p iscsi cmnd %p to (%u %u %u %u), "
		       "Cmd 0x%02x, SCSI status 0x%x, out of retries\n",
		       session, c, c->sc.host->host_no, c->sc.channel,
		       c->sc.target, c->sc.lun, c->sc.cmnd[0],
		       sc->result & 0xFF);
		return 0;
	    }
	    set_current_state(TASK_UNINTERRUPTIBLE);
	    schedule_timeout(MSECS_TO_JIFFIES(20));
	    goto retry;
	case CONDITION_GOOD:
	case INTERMEDIATE_GOOD:
	case INTERMEDIATE_C_GOOD:
	    /* we should never get the linked command return codes */
	case RESERVATION_CONFLICT:
	    /* this is probably never going to happen for
	     * INQUIRY or REPORT_LUNS, but retry if it
	     * does 
	     */
	    printk("iSCSI: session %p iscsi_do_cmnd %p SCSI status 0x%x at "
		   "%lu, retrying\n", session, c, sc->result & 0xFF, jiffies);
	    goto retry;
	case CHECK_CONDITION:
	    /* look at the sense.  If it's illegal request,
	     * don't bother retrying the command 
	     */
	    if ((sc->sense_buffer[0] & 0x70) == 0x70) {
		switch (SENSE_KEY(sc->sense_buffer)) {
		case ILLEGAL_REQUEST:
		    printk("iSCSI: session %p iscsi cmnd %p to (%u %u %u %u), "
			   "Cmd 0x%02x, illegal request\n",
			   session, c, c->sc.host->host_no, c->sc.channel,
			   c->sc.target, c->sc.lun, c->sc.cmnd[0]);
		    return 0;
		default:
		    /* possibly retry */
		    if (LOG_ENABLED(ISCSI_LOG_INIT))
			printk("iSCSI: session %p iscsi cmnd %p to "
			       "(%u %u %u %u), Cmd 0x%02x with sense, "
			       "retrying\n",
			       session, c, c->sc.host->host_no, c->sc.channel,
			       c->sc.target, c->sc.lun, c->sc.cmnd[0]);
		    goto retry;
		}
	    }
	    goto retry;
	default:
	    printk("iSCSI: session %p iscsi_do_cmnd %p unexpected SCSI "
		   "status 0x%x at %lu\n",
		   session, c, sc->result & 0xFF, jiffies);
	    return 0;
	}
    }

    if (LOG_ENABLED(ISCSI_LOG_INIT))
	printk("iSCSI: session %p iscsi_do_cmnd %p SCSI status 0x%x, out "
	       "of retries at %lu\n", session, c, sc->result & 0xFF, jiffies);

    return 0;
}

static void
make_report_luns(Scsi_Cmnd * sc, uint32_t max_entries)
{
    uint32_t length = 8 + (max_entries * 8);	/* 8 byte header plus 
						 * 8 bytes per LUN 
						 */

    sc->cmd_len = 10;
    sc->request_bufflen = length;
    sc->underflow = 8;		/* need at least the length */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,19)
    sc->resid = 0;
#endif

    /* CDB */
    memset(sc->cmnd, 0, sizeof (sc->cmnd));
    sc->cmnd[0] = REPORT_LUNS;
    sc->cmnd[1] = 0;
    sc->cmnd[2] = 0;		/* either reserved or select report in 
				 * various versions of SCSI-3 
				 */
    sc->cmnd[3] = 0;
    sc->cmnd[4] = 0;
    sc->cmnd[5] = 0;
    sc->cmnd[6] = (length >> 24) & 0xFF;
    sc->cmnd[7] = (length >> 16) & 0xFF;
    sc->cmnd[8] = (length >> 8) & 0xFF;
    sc->cmnd[9] = (length) & 0xFF;
    sc->sc_data_direction = SCSI_DATA_READ;
}

static void
make_inquiry(Scsi_Cmnd * sc, int lun0_scsi_level)
{
    sc->cmd_len = 6;
    sc->request_bufflen = 255;
    if (sc->lun == 0)
	sc->underflow = 3;	/* we need at least the peripheral code 
				 * and SCSI version 
				 */
    else
	sc->underflow = 1;	/* we need at least the peripheral code */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,19)
    sc->resid = 0;
#endif

    memset(sc->cmnd, 0, sizeof (sc->cmnd));
    sc->cmnd[0] = INQUIRY;
    if (lun0_scsi_level >= 0x3)
	sc->cmnd[1] = 0;	/* reserved in SCSI-3 and higher */
    else
	sc->cmnd[1] = (sc->lun << 5) & 0xe0;

    sc->cmnd[2] = 0;
    sc->cmnd[3] = 0;
    sc->cmnd[4] = 255;		/* length */
    sc->cmnd[5] = 0;
    sc->sc_data_direction = SCSI_DATA_READ;
}

/* scan for LUNs */
void
iscsi_detect_luns(iscsi_session_t * session)
{
    int l;
    iscsi_cmnd_t *c = NULL;
    Scsi_Cmnd *sc = NULL;
    int lun0_scsi_level = 0;
    size_t cmd_size = sizeof (iscsi_cmnd_t);
    unsigned int bufflen = 0;
    uint32_t last_luns = 0;
    uint32_t luns = 32;		/* start small to avoid bugs in 
				 * REPORT_LUNS handling 
				 */
    int report_luns_failed = 0;

    memset(session->luns_found, 0, sizeof (session->luns_found));

    /* need enough buffer space for replies to INQUIRY and REPORT_LUNS */
    if ((8 + (ISCSI_MAX_LUN * 8)) < 255)
	bufflen = 255;
    else
	bufflen = (ISCSI_MAX_LUN * 8) + 8;

    cmd_size += bufflen;

    c = kmalloc(cmd_size, GFP_KERNEL);
    if (!c) {
	printk("iSCSI: session %p iscsi_detect_luns couldn't allocate "
	       "a Scsi_Cmnd\n", session);
	return;
    }

    /* initialize */
    memset(c, 0, cmd_size);
    sema_init(&c->done_sem, 0);
    c->bufflen = bufflen;
    DEBUG_ALLOC("iSCSI: session %p hba %p host %p allocated iscsi cmnd %p, "
		"size %d, buffer %p, bufflen %u, end %p\n",
		session, session->hba, session->hba->host, c, cmd_size,
		c->buffer, c->bufflen, c->buffer + c->bufflen);

    /* fill in the basic required info in the Scsi_Cmnd */
    sc = &(c->sc);
    sc->host = session->hba->host;
    sc->channel = session->channel;
    sc->target = session->target_id;
    sc->lun = 0;
    sc->use_sg = 0;
    sc->request_buffer = c->buffer;
    sc->request_bufflen = c->bufflen;
    sc->scsi_done = iscsi_done;
    sc->timeout_per_command = 30 * HZ;
    init_timer(&sc->eh_timeout);
    /* save a pointer to the iscsi_cmnd in the Scsi_Cmnd, so
     * that iscsi_done can use it 
     */
    sc->buffer = (void *) c;

    do {
	if (signal_pending(current)) {
	    DEBUG_INIT("iSCSI: session %p detect LUNs aborted by signal\n",
		       session);
	    goto done;
	}
	if (test_bit(SESSION_TERMINATING, &session->control_bits))
	    goto done;

	/* send a REPORT_LUNS to LUN 0.  If it works, we know the LUNs. */
	last_luns = luns;
	make_report_luns(sc, luns);
	smp_mb();
	if (iscsi_do_cmnd(session, c, 6)) {
	    uint8_t *lun_list = c->buffer + 8;
	    int luns_listed;
	    uint32_t length = 0;

	    /* get the list length the target has */
	    length = c->buffer[0] << 24;
	    length |= c->buffer[1] << 16;
	    length |= c->buffer[2] << 8;
	    length |= c->buffer[3];

	    if (length < 8) {
		/* odd, assume REPORT_LUNS is broken, fall
		 * back to doing INQUIRY */
		DEBUG_INIT("iSCSI: session %p REPORT_LUNS length 0, "
			   "falling back to INQUIRY\n", session);
		report_luns_failed = 1;
		break;
	    }

	    /* figure out how many luns we were told about this time */
	    if ((length / 8U) < luns)
		luns_listed = length / 8U;
	    else
		luns_listed = luns;

	    /* loop until we run out of data, or out of buffer */
	    for (l = 0; l < luns_listed; l++) {
		int address_method = (lun_list[0] & 0xc0) >> 6;
		int lun;

		if (LOG_ENABLED(ISCSI_LOG_LOGIN) || LOG_ENABLED(ISCSI_LOG_INIT))
		    printk("iSCSI: session %p (%u %u %u *) REPORT_LUNS[%d] = "
			   "%02x %02x %02x %02x %02x %02x %02x %02x\n",
			   session, session->host_no, session->channel,
			   session->target_id, l, lun_list[0], lun_list[1],
			   lun_list[2], lun_list[3], lun_list[4], lun_list[5],
			   lun_list[6], lun_list[7]);

		switch (address_method) {
		case 0x0:{
			/* single-level LUN if bus id is 0,
			 * else peripheral device
			 * addressing 
			 */
			lun = lun_list[1];
			set_bit(lun, session->luns_detected);
			/* This is useful while checking for deleted luns */
			set_bit(lun, session->luns_found);
			break;
		    }
		case 0x1:{
			/* flat-space addressing */
			lun = lun_list[1];
			set_bit(lun, session->luns_detected);
			/* This is useful while checking for deleted luns */
			set_bit(lun, session->luns_found);
			break;
		    }
		case 0x2:{
			/* logical unit addressing method */
			lun = lun_list[1] & 0x1F;
			set_bit(lun, session->luns_detected);
			/* This is useful while checking for deleted luns */
			set_bit(lun, session->luns_found);
			break;
		    }
		case 0x3:{
			/* extended logical unit addressing
			 * method is too complicated for us
			 * to want to deal with 
			 */
			printk("iSCSI: session %p (%u %u %u *) REPORT_LUNS[%d] "
			       "with extended LU address method 0x%x ignored\n",
			       session, session->host_no, session->channel,
			       session->target_id, l, address_method);
			break;
		    }
		default:
		    printk("iSCSI: session %p (%u %u %u *) REPORT_LUNS[%d] "
			   "with unknown address method 0x%x ignored\n",
			   session, session->host_no, session->channel,
			   session->target_id, l, address_method);
		    break;
		}

		/* next LUN in the list */
		lun_list += 8;
	    }

	    /* decide how many luns to ask for on the next
	     * iteration, if there is one 
	     */
	    luns = length / 8U;
	    if (luns > ISCSI_MAX_LUN) {
		/* we only have buffer space for so many LUNs */
		luns = ISCSI_MAX_LUN;
		printk("iSCSI: session %p REPORT_LUNS length %u "
		       "(%u entries) truncated to %u (%u entries)\n",
		       session, length, (length / 8) - 1, (luns + 1) * 8U,
		       luns);
	    }

	} else {
	    /* REPORT_LUNS failed, fall back to doing INQUIRY */
	    DEBUG_INIT("iSCSI: session %p REPORT_LUNS failed, "
		       "falling back to INQUIRY\n", session);
	    report_luns_failed = 1;
	    break;
	}

    } while (luns > last_luns);

    if (signal_pending(current)) {
	DEBUG_INIT("iSCSI: session %p detect LUNs aborted by signal\n",
		   session);
	goto done;
    }

    if (report_luns_failed) {
	/* if REPORT_LUNS failed, then either it's a SCSI-2 device
	 * that doesn't understand the command, or it's a SCSI-3
	 * device that only has one LUN and decided not to implement
	 * REPORT_LUNS.  In either case, we're safe just probing LUNs
	 * 0-7 with INQUIRY, since SCSI-2 can't have more than 8 LUNs,
	 * and SCSI-3 should do REPORT_LUNS if it has more than 1 LUN.
	 */
	for (l = 0; l < 8; l++) {
	    sc->lun = l;
	    sc->request_buffer = c->buffer;
	    make_inquiry(sc, lun0_scsi_level);

	    /* we'll make a note of the LUN when the rx
	     * thread receives the response.  No need to do
	     * it again here.
	     */
	    if (iscsi_do_cmnd(session, c, 6)) {
		/* we do need to record the SCSI level so we
		 * can build inquiries properly though 
		 */
		if (l == 0) {
		    lun0_scsi_level = c->buffer[2] & 0x07;
		    if (LOG_ENABLED(ISCSI_LOG_INIT))
			printk("iSCSI: session %p (%u %u %u %u) is SCSI "
			       "level %d\n",
			       session, sc->host->host_no, sc->channel,
			       sc->target, sc->lun, lun0_scsi_level);
		}
	    } else {
		/* just assume there's no LUN */
	    }

	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		break;
	    if (signal_pending(current))
		break;
	}
    }

  done:
    DEBUG_ALLOC("iSCSI: session %p hba %p host %p kfree iscsi cmnd %p, "
		"bufflen %u\n",
		session, session->hba, session->hba->host, c, c->bufflen);
    kfree(c);
}

int
iscsi_reset_lun_probing(void)
{
    int ret = 0;

    spin_lock(&iscsi_lun_probe_lock);
    if ((iscsi_currently_probing == NULL) && (iscsi_lun_probe_head == NULL)) {
	/* if we're not currently probing, reset */
	DEBUG_INIT("iSCSI: reset LUN probing at %lu\n", jiffies);
	iscsi_next_probe = 0;
	iscsi_lun_probe_start = 0;
	smp_mb();
	ret = 1;
    } else {
	DEBUG_INIT("iSCSI: failed to reset LUN probing at %lu, currently "
		   "probing %p, queue head %p\n",
		   jiffies, iscsi_currently_probing, iscsi_lun_probe_head);
    }
    spin_unlock(&iscsi_lun_probe_lock);

    return ret;
}
