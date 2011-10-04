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
 * $Id: iscsi.c,v 1.1.2.4 2004/09/22 09:23:28 krishmnc Exp $ 
 *
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
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
#include <linux/in.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/timer.h>

#include <linux/init.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <scsi/sg.h>
#include <linux/inetdevice.h>
#include <linux/route.h>
#include <net/route.h>
/* 
 * These header files are required for Shutdown Notification routines
 */
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

/* these are from $(TOPDIR)/drivers/scsi, not $(TOPDIR)/include */
#include "scsi.h"
#include "hosts.h"

/* if non-zero, do a TCP Abort when a session drops, instead
 * of (attempting) a graceful TCP Close 
 */
#define TCP_ABORT_ON_DROP 0

#define RETRIES_BLOCK_DEVICES 1

/* some targets, such as the Intel Labs target on SourceForge, make
 * invalid assumptions about the relateive ordering of command and
 * data PDUs, but still advertise a CmdSN window greater than one
 * command.  When this is non-zero, we try to break ourselves in such
 * a way that the target's bogus assumptions are met.  No promises
 * though, since we may send Nops or task mgmt PDUs at any time, 
 * which the broken target may still choke on.
 */
#define INVALID_ORDERING_ASSUMPTIONS 0

/* periodically stall reading data to test data arriving
 * after aborts have started 
 */
#define TEST_DELAYED_DATA 0

/* fake sense indicating ILLEGAL_REQUEST for all REPORT_LUNS commands */
#define FAKE_NO_REPORT_LUNS 0

/* fake check conditions on the first 2 attempts for each probe command */
#define FAKE_PROBE_CHECK_CONDITIONS 0

/* fake underflows on the first 4 attempts for each probe command */
#define FAKE_PROBE_UNDERFLOW 0

/* Timeout for commands during configuration */
#define CFG_TIMEOUT   30

#include "iscsi-common.h"
#include "iscsi-protocol.h"
#include "iscsi-ioctl.h"
#include "iscsi-io.h"
#include "iscsi-login.h"
#include "iscsi-trace.h"
#include "iscsi.h"
#include "iscsi-session.h"
#include "iscsi-version.h"
#include "iscsi-probe.h"
#include "iscsi-crc.h"

/*
 *  IMPORTANT NOTE: to prevent deadlock, when holding multiple locks,
 *  the following locking order must be followed at all times:
 *
 *  hba_list_lock             - access to collection of HBA instances
 *  session->portal_lock      - access to a session's portal info
 *  session->task_lock        - access to a session's collections of tasks
 *  hba->session_lock         - access to an HBA's collection of sessions   
 *  session->scsi_cmnd_lock   - access to a session's list of Scsi_Cmnds 
 *                              (IRQSAVE)
 *  io_request_lock/host_lock - mid-layer acquires before calling queuecommand, 
 *                              eh_*, we must acquire before done() callback  
 *                              (IRQSAVE)
 *  iscsi_trace_lock          - for the tracing code     (IRQSAVE)
 *
 *  Note: callers not in interrupt context must locally disable/restore 
 *        interrupts when holding locks marked (IRQSAVE)
 */

#ifdef MODULE
MODULE_AUTHOR("Cisco Systems, Inc.");
MODULE_DESCRIPTION("iSCSI initiator");
#  if defined(MODULE_LICENSE)
MODULE_LICENSE("GPL");
#  endif
#endif

static int iscsi_system_is_rebooting;
static int this_is_iscsi_boot;
static sapiNBP_t iscsi_inbp_info;
static char inbp_interface_name[IFNAMSIZ];

/* Force tagged command queueing for all devices, regardless
 * of whether they say they support it 
 */
static int force_tcq = 0;
MODULE_PARM(force_tcq, "i");
MODULE_PARM_DESC(force_tcq,
		 "when non-zero, force tagged command queueing for all devices");

/* Queue depth for devices that don't support tagged command queueing.
 * The driver used to use ISCSI_CMDS_PER_LUN, which was probably a bug.
 * Default to 1 now, but let people who want to the old behavior set it higher.
 */
static int untagged_queue_depth = 1;
MODULE_PARM(untagged_queue_depth, "i");
MODULE_PARM_DESC(untagged_queue_depth,
		 "queue depth to use for devices that don't support tagged "
		 "command queueing");

static int translate_deferred_sense = 1;
MODULE_PARM(translate_deferred_sense, "i");
MODULE_PARM_DESC(translate_deferred_sense,
		 "translate deferred sense data to current sense data in "
		 "disk command responses");

static int iscsi_reap_tasks = 0;
MODULE_PARM(iscsi_reap_tasks, "i");
MODULE_PARM_DESC(iscsi_reap_task,
		 "when non-zero, the OS is allowed to reap pages from the "
		 "iSCSI task cache");

#ifndef UINT32_MAX
# define UINT32_MAX 0xFFFFFFFFU
#endif

/* We need it here for probing luns on lun change async event */
#define MAX_SCSI_DISKS 128
#define MAX_SCSI_DISK_PARTITIONS 15
#define MAX_SCSI_TAPES 32
#define MAX_SCSI_GENERICS 256
#define MAX_SCSI_CDROMS 256

#define WILD_CARD ~0

static int ctl_open(struct inode *inode, struct file *file);
static int ctl_close(struct inode *inode, struct file *file);
static int ctl_ioctl(struct inode *inode,
		     struct file *file, unsigned int cmd, unsigned long arg);
static int iscsi_inet_aton(char *asciiz,
			   unsigned char *ip_address, int *ip_length);

static int control_major;
static const char *control_name = "iscsictl";

static struct file_operations control_fops = {
  owner:THIS_MODULE,
  ioctl:ctl_ioctl,		/* ioctl */
  open:ctl_open,		/* open */
  release:ctl_close,		/* release */
};

spinlock_t iscsi_hba_list_lock = SPIN_LOCK_UNLOCKED;
static iscsi_hba_t *iscsi_hba_list = NULL;

static volatile unsigned long init_module_complete = 0;
static volatile unsigned long iscsi_timer_running = 0;
static volatile pid_t iscsi_timer_pid = 0;

volatile unsigned int iscsi_log_settings =
LOG_SET(ISCSI_LOG_ERR) | LOG_SET(ISCSI_LOG_RETRY) | LOG_SET(ISCSI_LOG_TIMEOUT);

#define is_digit(c)	(((c) >= '0') && ((c) <= '9'))
#define is_hex_lower(c) (((c) >= 'a') && ((c) <= 'f'))
#define is_hex_upper(c) (((c) >= 'A') && ((c) <= 'F'))
#define is_space(c)	((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\0')

#if DEBUG_TRACE
spinlock_t
    iscsi_trace_lock = SPIN_LOCK_UNLOCKED;
static
    iscsi_trace_entry_t
    trace_table[ISCSI_TRACE_COUNT];
static int
    trace_index = 0;

# define ISCSI_TRACE(P_TYPE, P_CMND, P_TASK, P_DATA1, P_DATA2) \
           iscsi_fill_trace((P_TYPE), (P_CMND), (P_TASK), (P_DATA1), (P_DATA2))
#else
# define ISCSI_TRACE(P_TYPE, P_CMND, P_TASK, P_DATA1, P_DATA2)
#endif

#define MAX_PORTALS 32		/* 
				 * 32 is the sizeof(unsigned int).
				 * If max portals to a target exceeds 32 then
				 * we need to change the preferred_portal_bitmap,
				 * preferred_subnet_bitmap from unsigned int to
				 * an array of unsigned int's.
				 */

/* Returns 0 on success, non zero on error */
static int
iscsi_add_route(void)
{
    struct rtentry rt;
    struct sockaddr_in *dst = (struct sockaddr_in *) &rt.rt_dst;
    struct sockaddr_in *gw = (struct sockaddr_in *) &rt.rt_gateway;
    char dev[IFNAMSIZ];
    char ip[16];
    int ret;

    memset((char *) &rt, 0, sizeof (struct rtentry));
    memset(ip, 0, 16);
    memset(dev, 0, IFNAMSIZ);

    dst->sin_family = AF_INET;
    dst->sin_addr.s_addr = INADDR_ANY;

    strcpy(ip, (char *) (&iscsi_inbp_info.ripaddr));
    strcpy(dev, inbp_interface_name);

    gw->sin_family = AF_INET;
    memcpy((char *) (&(gw->sin_addr.s_addr)), (char *) ip, 4);

    rt.rt_flags = (RTF_UP | RTF_GATEWAY);
    rt.rt_dev = dev;

    DEBUG_INIT("iSCSI: Setting gateway ip as: 0x%2x %2x %2x %2x\n", ip[0],
	       ip[1], ip[2], ip[3]);

    ret = ip_rt_ioctl(SIOCADDRT, &rt);

    if (ret != 0) {
	printk("iSCSI: ERROR: ip_rt_ioctl returned with value: %d\n", ret);
    }

    return ret;
}

/* Returns 1 on success 0 on failure */
/* Needs to be called between set_fs() ... */
static int
iscsi_ifdown(void)
{
    struct ifreq ifr;
    int dev_ret = 0;

    memset(&ifr, 0, sizeof (struct ifreq));

    printk("\niSCSI: iscsi_ifdown: Bringing down network interface %s\n",
	   inbp_interface_name);

    strcpy(ifr.ifr_name, inbp_interface_name);

    /* 
     * Check if the interface is already up or not, set_fs should have already 
     * been done before calling this function
     */
    if (dev_ioctl(SIOCGIFFLAGS, &ifr) == 0) {
	if ((ifr.ifr_flags & IFF_UP) != 0) {
	    DEBUG_INIT("\niSCSI: Interface %s has IFF_UP flag set, will "
		       "bring it down ...\n", ifr.ifr_name);
	    /* fall through and bring down the interface */
	} else {
	    DEBUG_INIT("\niSCSI: Interface %s does not have IFF_UP flag set\n",
		       ifr.ifr_name);
	    return 1;
	}
    } else {
	printk("\niSCSI: ERROR in getting interface flags for interface %s\n",
	       ifr.ifr_name);
	return 0;
    }

    ifr.ifr_flags &= ~(IFF_UP);

    if ((dev_ret = devinet_ioctl(SIOCSIFFLAGS, (void *) &ifr)) != 0) {
	printk("\niSCSI: ERROR in bringing down interface %s, return "
	       "value %d\n", ifr.ifr_name, dev_ret);
	return 0;
    }

    return 1;
}

/* Returns 1 on success 0 on failure */
/* Needs to be called between set_fs() ... */
static int
iscsi_set_if_addr(void)
{
    struct ifreq ifr;
    struct sockaddr sa;
    struct sockaddr_in *sin = (struct sockaddr_in *) &sa;
    int dev_ret = 0;

    memset(&ifr, 0, sizeof (struct ifreq));
    memset(&sa, 0, sizeof (struct sockaddr));

    printk("\niSCSI: iscsi_set_if_addr: Bringing up network interface\n");

    if (iscsi_inbp_info.myipaddr != 0) {
	DEBUG_INIT("\nSetting ip from inbp 0x%x\n", iscsi_inbp_info.myipaddr);
	memcpy((char *) (&sin->sin_addr), (char *) (&iscsi_inbp_info.myipaddr),
	       4);
    } else {
	DEBUG_INIT("\nERROR !!! Not setting ip from inbp !!!\n");
	return 0;
    }
    if (inbp_interface_name != NULL) {
	DEBUG_INIT("\nSetting interface from inbp %s\n", inbp_interface_name);
	strcpy(ifr.ifr_name, inbp_interface_name);
    } else {
	DEBUG_INIT("\nERROR !!! Not setting interface from inbp !!!\n");
	return 0;
    }

    /* Check if the interface is already up or not */
    if (dev_ioctl(SIOCGIFFLAGS, &ifr) == 0) {
	if ((ifr.ifr_flags & IFF_UP) != 0) {
	    DEBUG_INIT("\nInterface %s has IFF_UP flag already set\n",
		       ifr.ifr_name);
	    return 1;
	} else {
	    DEBUG_INIT("\nInterface %s does not have IFF_UP flag set\n",
		       ifr.ifr_name);
	    /* fall through and bring up the interface */
	}
    } else {
	printk("\niSCSI: ERROR in getting interface FLAGS for interface %s\n",
	       ifr.ifr_name);
	return 0;
    }

    memset(&ifr, 0, sizeof (struct ifreq));
    /* If we came this far then inbp_interface_name should be valid */
    strcpy(ifr.ifr_name, inbp_interface_name);

    sin->sin_family = AF_INET;
    sin->sin_port = 0;

    memcpy((char *) &ifr.ifr_addr, (char *) &sa, sizeof (struct sockaddr));

    /* Bring up networking, set_fs has already been done */
    if ((dev_ret = devinet_ioctl(SIOCSIFADDR, (void *) &ifr)) != 0) {
	printk("\niSCSI: ERROR in setting ip address for interface %s\n",
	       ifr.ifr_name);
	return 0;
    }

    DEBUG_INIT("\niSCSI: addr_dev_ret = 0x%x\n", dev_ret);

    memset(&ifr, 0, sizeof (struct ifreq));

    if (inbp_interface_name != NULL) {
	DEBUG_INIT("\nSetting interface from inbp %s\n", inbp_interface_name);
	strcpy(ifr.ifr_name, inbp_interface_name);
    } else {
	DEBUG_INIT("\nERROR !!! Not setting interface from inbp !!!\n");
	return 0;
    }

    ifr.ifr_flags |= IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;

    if ((dev_ret = devinet_ioctl(SIOCSIFFLAGS, (void *) &ifr)) != 0) {
	printk("\niSCSI: ERROR in setting flags for interface %s\n",
	       ifr.ifr_name);
	return 0;
    }

    DEBUG_INIT("\niSCSI: flag_dev_ret = 0x%x\n", dev_ret);

    memset(&ifr, 0, sizeof (struct ifreq));

    /* If we get this far we assume name and mask in inbp structure are valid */

    strcpy(ifr.ifr_name, inbp_interface_name);
    memcpy((char *) (&sin->sin_addr), (char *) (&iscsi_inbp_info.myipmask), 4);
    memcpy((char *) &ifr.ifr_netmask, (char *) &sa, sizeof (struct sockaddr));

    if ((dev_ret = devinet_ioctl(SIOCSIFNETMASK, (void *) &ifr)) != 0) {
	printk("\niSCSI: ERROR in setting network mask for interface %s\n",
	       ifr.ifr_name);
	return 0;
    }

    DEBUG_INIT("\niSCSI: mask_dev_ret = 0x%x\n", dev_ret);

    while (iscsi_add_route()) {
	printk("\niSCSI: set_inbp_info: iscsi_add_route failed\n");
	schedule_timeout(10 * HZ);
    }

    return 1;
}

/* become a daemon kernel thread.  Some kernels provide this functionality
 * already, and some even do it correctly
 */
void
iscsi_daemonize(void)
{
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,44) )
    /* use the kernel's daemonize */
    daemonize();

    /* Reparent to init now done by daemonize */

    /* FIXME: do we really need to bump up the thread priority? */
# if defined(HAS_SET_USER_NICE) || defined(set_user_nice)
    {
	struct task_struct *this_task = current;
	set_user_nice(this_task, -20);
    }
# endif

#else
    struct task_struct *this_task = current;

    /* use the kernel's daemonize */
    daemonize();

# if defined(HAS_REPARENT_TO_INIT) || defined(reparent_to_init)
    /* Reparent to init */
    reparent_to_init();
# endif

    /* increase priority like the md driver does for it's kernel threads */
    this_task->policy = SCHED_OTHER;
# if defined(HAS_SET_USER_NICE) || defined(set_user_nice)
    set_user_nice(this_task, -20);
# else
    this_task->nice = -20;
# endif
    smp_mb();
#endif
}

#ifdef HAS_NEW_DEVICE_LISTS

static void
target_reset_occured(iscsi_session_t * session)
{
    Scsi_Device *device = NULL;

    list_for_each_entry(device, &session->hba->host->my_devices, siblings) {
	if ((device->channel == session->channel) &&
	    (device->id == session->target_id)) {
	    device->was_reset = 1;
	    device->expecting_cc_ua = 1;
	}
    }
}

static void
lun_reset_occured(iscsi_session_t * session, unsigned int lun)
{
    Scsi_Device *device = NULL;

    list_for_each_entry(device, &session->hba->host->my_devices, siblings) {
	if ((device->channel == session->channel) &&
	    (device->id == session->target_id) && (device->lun == lun)) {
	    device->was_reset = 1;
	    device->expecting_cc_ua = 1;
	}
    }
}

#else

static void
target_reset_occured(iscsi_session_t * session)
{
    Scsi_Device *device = NULL;

    /* FIXME: locking? */
    for (device = session->hba->host->host_queue; device; device = device->next) {
	if ((device->channel == session->channel) &&
	    (device->id == session->target_id)) {
	    device->was_reset = 1;
	    device->expecting_cc_ua = 1;
	}
    }
}

static void
lun_reset_occured(iscsi_session_t * session, unsigned int lun)
{
    Scsi_Device *device = NULL;

    for (device = session->hba->host->host_queue; device; device = device->next) {
	if ((device->channel == session->channel) &&
	    (device->id == session->target_id) && (device->lun == lun)) {
	    device->was_reset = 1;
	    device->expecting_cc_ua = 1;
	}
    }
}

#endif

/* determine whether a command is eligible to be retried internally. */
static inline int
internally_retryable(Scsi_Cmnd * sc)
{
    if (sc->device && (sc->device->type == TYPE_DISK)) {
	switch (sc->cmnd[0]) {
	case INQUIRY:
	case REPORT_LUNS:
	case TEST_UNIT_READY:
	case READ_CAPACITY:
	case START_STOP:
	case MODE_SENSE:
	    return 0;
	default:
	    return 1;
	}
    }

    return 0;
}

/* newer kernels require long alignment for bitops.
 * we assume pointers have at least as much alignment as longs,
 * and use an unused pointer field to store bit flags.
 */
static inline unsigned long *
device_flags(Scsi_Device * sd)
{
    unsigned long *flags = (unsigned long *) &(sd->hostdata);

    return flags;
}

/* device flags */
#define DEVICE_LOG_TERMINATING          0
#define DEVICE_LOG_REPLACEMENT_TIMEDOUT 1
#define DEVICE_LOG_NO_SESSION           2

/* newer kernels require long alignment for bitops.
 * we assume pointers have at least as much alignment as longs,
 * and use an unused pointer field to store bit flags.
 */
static inline unsigned long *
command_flags(Scsi_Cmnd * sc)
{
    unsigned long *flags = (unsigned long *) &(sc->SCp.buffer);

    return flags;
}

static void
useless_timeout_function(unsigned long arg)
{

}

static inline void
add_completion_timer(Scsi_Cmnd * sc)
{
    if (sc->scsi_done != iscsi_done) {
	sc->eh_timeout.data = (unsigned long) sc;
	sc->eh_timeout.expires = jiffies + sc->timeout_per_command;
	sc->eh_timeout.function = useless_timeout_function;
	add_timer(&sc->eh_timeout);
    }
}

static void
add_command_timer(iscsi_session_t * session, Scsi_Cmnd * sc,
		  void (*fn) (unsigned long))
{
    unsigned long now = jiffies;
    unsigned long expires;

    if (sc->eh_timeout.function != NULL) {
	DEBUG_QUEUE("iSCSI: add_command_timer %p %p deleting existing "
		    "timer at %lu\n", sc, fn, jiffies);

	del_timer_sync(&sc->eh_timeout);
	sc->eh_timeout.function = NULL;
    }

    /* default is based on the number of retries remaining
     * and the timeout for each 
     */
    if ((sc->allowed > 1) && (sc->retries < sc->allowed))
	if (session->lun_being_probed != sc->lun)
	    sc->eh_timeout.expires =
		now + ((sc->allowed - sc->retries) * sc->timeout_per_command);
	else
	    /* Since device configuration is serial, long timeouts are
	     * undesirable during that period.
	     */
	    sc->eh_timeout.expires = now + CFG_TIMEOUT * HZ;
    else
	sc->eh_timeout.expires = now + sc->timeout_per_command;

    if (sc->eh_timeout.expires == 0)
	sc->eh_timeout.expires = 1;

    /* but each session may override that timeout value for
     * certain disk commands 
     */
    if (sc->device && (sc->device->type == TYPE_DISK)) {
	expires = now + session->disk_command_timeout * HZ;
	if (expires > sc->eh_timeout.expires) {
	    /* we only increase timeouts on commands
	     * that are internally retryable, since some commands
	     * must be completed in a reasonable amount of time
	     * in order for the upper layers to behave properly.
	     */
	    if ((session->lun_being_probed != sc->device->lun)
		&& internally_retryable(sc)) {
		/*
		 * increase the timeout.
		 */
		sc->eh_timeout.expires = expires;
	    }
	} else {
	    if (session->disk_command_timeout) {
		/* Reduction in timeout is always allowed.
		 * This will keep multipath drivers happy.
		 */
		sc->eh_timeout.expires = expires;
	    } else if ((session->lun_being_probed != sc->device->lun)
		       && internally_retryable(sc))
		/* No timeout. Infinite retries. */
		sc->eh_timeout.expires = 0;
	}
    }

    if (sc->eh_timeout.expires) {
	DEBUG_QUEUE("iSCSI: add_command_timer %p %p adding timer at %lu, "
		    "expires %lu, timeout %u, retries %u, allowed %u\n",
		    sc, fn, jiffies, sc->eh_timeout.expires,
		    sc->timeout_per_command, sc->retries, sc->allowed);
	sc->eh_timeout.data = (unsigned long) sc;
	sc->eh_timeout.function = fn;
	add_timer(&sc->eh_timeout);
    }
}

static void
add_task_timer(iscsi_task_t * task, void (*fn) (unsigned long))
{
    if (task->timer.function != NULL) {
	DEBUG_QUEUE("iSCSI: add_task_timer %p %p deleting existing "
		    "timer at %lu\n", task, fn, jiffies);

	del_timer_sync(&task->timer);
    }
    task->timer.data = (unsigned long) task;
    task->timer.expires = jiffies + task->scsi_cmnd->timeout_per_command;
    task->timer.function = fn;

    DEBUG_QUEUE("iSCSI: add_task_timer %p %p adding timer at %lu, expires "
		"%lu, timeout %u\n",
		task, fn, jiffies, task->timer.expires,
		task->scsi_cmnd->timeout_per_command);
    add_timer(&task->timer);
}

static int
del_command_timer(Scsi_Cmnd * sc)
{
    int ret;

    DEBUG_QUEUE("iSCSI: del_command_timer %p deleting timer at %lu\n", sc,
		jiffies);

    ret = del_timer_sync(&sc->eh_timeout);

    sc->eh_timeout.expires = 0;
    sc->eh_timeout.data = (unsigned long) NULL;
    sc->eh_timeout.function = NULL;
    return ret;
}

static int
del_task_timer(iscsi_task_t * task)
{
    int ret;

    DEBUG_QUEUE("iSCSI: del_task_timer %p deleting timer at %lu\n", task,
		jiffies);

    ret = del_timer_sync(&task->timer);

    task->timer.expires = 0;
    task->timer.data = (unsigned long) NULL;
    task->timer.function = NULL;
    return ret;
}

static void
iscsi_command_times_out(unsigned long arg)
{
    Scsi_Cmnd *sc = (Scsi_Cmnd *) arg;
    iscsi_session_t *session = (iscsi_session_t *) sc->SCp.ptr;

    /* we can safely use the session pointer, since during a session termination
     * the rx thread will make sure all commands have been completed before it
     * drops the session refcount.
     */

    if (session == NULL)
	return;

    DEBUG_EH("iSCSI: session %p timer for command %p expired at %lu, "
	     "retries %d, allowed %d\n",
	     session, sc, jiffies, sc->retries, sc->allowed);

    /* tell the tx thread that a command has timed out */
    set_bit(COMMAND_TIMEDOUT, command_flags(sc));
    smp_wmb();

    set_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits);
    smp_wmb();

    /* wake up the tx thread to deal with the timeout */
    set_bit(TX_WAKE, &session->control_bits);
    smp_mb();
    /* we can't know which wait_q the tx thread is in (if
     * any), so wake them both 
     */
    wake_up(&session->tx_wait_q);
    wake_up(&session->login_wait_q);
}

static void
iscsi_task_times_out(unsigned long arg)
{
    iscsi_task_t *task = (iscsi_task_t *) arg;
    iscsi_session_t *session = task->session;

    /* we can safely use the session pointer, since during a session termination
     * the rx thread will make sure all tasks have been completed before it
     * drops the session refcount.
     */
    if (session == NULL)
	return;

    DEBUG_TIMEOUT("iSCSI: session %p timer for task %p expired at %lu\n",
		  session, task, jiffies);

    /* stop new tasks from being sent to this LUN (force error recovery) */
    set_bit(task->lun, session->luns_timing_out);
    smp_wmb();

    /* tell the tx thread that a task has timed out */
    set_bit(0, &task->timedout);
    smp_wmb();

    set_bit(SESSION_TASK_TIMEDOUT, &session->control_bits);
    smp_wmb();

    /* wake up the tx thread to deal with the timeout and
     * possible error recovery 
     */
    set_bit(TX_WAKE, &session->control_bits);
    smp_mb();

    /* we can't know which wait_q the tx thread is in (if
     * any), so wake them both 
     */
    wake_up(&session->tx_wait_q);
    wake_up(&session->login_wait_q);
}

/* wake up the tx_thread without ever losing the wakeup event */
static void
wake_tx_thread(int control_bit, iscsi_session_t * session)
{
    /* tell the tx thread what to do when it wakes up. */
    set_bit(control_bit, &session->control_bits);
    smp_wmb();

    /* We make a condition variable out of a wait queue and atomic test&clear.
     * May get spurious wake-ups, but no wakeups will be lost.
     * this is cv_signal().  wait_event_interruptible is cv_wait().
     */
    set_bit(TX_WAKE, &session->control_bits);
    smp_mb();

    wake_up(&session->tx_wait_q);
}

/* drop an iscsi session */
static void
iscsi_drop_session(iscsi_session_t * session)
{
    pid_t pid;

    DEBUG_INIT("iSCSI: iscsi_drop_session %p, rx %d, tx %d at %lu\n",
	       session, session->rx_pid, session->tx_pid, jiffies);

    set_bit(SESSION_DROPPED, &session->control_bits);	/* so we know whether
							 * to abort the
							 * connection 
							 */
    session->session_drop_time = jiffies ? jiffies : 1;	/* for replacement 
							 * timeouts 
							 */
    smp_wmb();
    if (test_and_clear_bit(SESSION_ESTABLISHED, &session->control_bits)) {
    	smp_mb();
	if ((pid = session->tx_pid))
		kill_proc(pid, SIGHUP, 1);
    	if ((pid = session->rx_pid))
		kill_proc(pid, SIGHUP, 1);
    	session->session_alive = 0;
    }
}

/* caller must hold session->task_lock */
static void
iscsi_request_logout(iscsi_session_t * session, int logout, int logout_response)
{
    if (atomic_read(&session->num_active_tasks) == 0) {
	DEBUG_INIT("iSCSI: session %p currently has no active tasks, "
		   "queueing logout at %lu\n", session, jiffies);
	session->logout_response_deadline = jiffies + (logout_response * HZ);
	if (session->logout_response_deadline == 0)
	    session->logout_response_deadline = 1;
	smp_mb();
	set_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits);
	smp_mb();
	wake_tx_thread(TX_LOGOUT, session);
    } else {
	session->logout_deadline = jiffies + (logout * HZ);
	if (session->logout_deadline == 0)
	    session->logout_deadline = 1;
	session->logout_response_deadline =
	    session->logout_deadline + (logout_response * HZ);
	if (session->logout_response_deadline == 0)
	    session->logout_response_deadline = 1;
	smp_mb();
	set_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits);
	smp_mb();
    }
}

/* Note: may acquire the task_lock */
static void
iscsi_terminate_session(iscsi_session_t * session)
{
    pid_t pid;

    if ((test_and_set_bit(SESSION_TERMINATING, &session->control_bits) == 0) &&
	test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	DEBUG_INIT("iSCSI: iscsi_terminate_session %p, requesting logout "
		   "at %lu\n", session, jiffies);

	/* on the first terminate request while the session
	 * is up, request a logout in the next 3 seconds 
	 */
	spin_lock(&session->task_lock);
	iscsi_request_logout(session, 3, session->active_timeout);
	spin_unlock(&session->task_lock);
    } else {
	/* either we've already tried to terminate once, or
	 * the the session is down.  just kill
	 * everything. 
	 */
	clear_bit(SESSION_ESTABLISHED, &session->control_bits);
	session->session_drop_time = jiffies ? jiffies : 1;
	smp_mb();

	DEBUG_INIT("iSCSI: iscsi_terminate_session %p, killing rx "
		   "%d, tx %d at %lu\n",
		   session, session->rx_pid, session->tx_pid, jiffies);

	/* kill the session's threads */
	if ((pid = session->tx_pid))
	    kill_proc(pid, SIGKILL, 1);
	if ((pid = session->rx_pid))
	    kill_proc(pid, SIGKILL, 1);
    }
    session->session_alive = 0;
}

/* if a signal is pending, deal with it, and return 1.
 * Otherwise, return 0.
 */
static int
iscsi_handle_signals(iscsi_session_t * session)
{
    pid_t pid;
    int ret = 0, sigkill, sighup;

    /* if we got SIGHUP, try to establish a replacement session.
     * if we got SIGKILL, terminate this session.
     */
    if (signal_pending(current)) {
	LOCK_SIGNALS();
	sigkill = SIGNAL_IS_PENDING(SIGKILL);
	sighup = SIGNAL_IS_PENDING(SIGHUP);
	flush_signals(current);
	UNLOCK_SIGNALS();

	/* iscsi_drop_session and iscsi_terminate_session signal both
	 * threads, but someone logged in as root may not.  So, we
	 * make sure whichever process gets signalled first propagates
	 * the signal when it looks like only one thread got
	 * signalled.
	 */

	/* on SIGKILL, terminate the session */
	if (sigkill) {
	    /*
	     * FIXME: We don't terminate the sessions if "/" is iSCSI disk
	     * Need to fix this for other iSCSI targets.
	     */

	    if (!session->this_is_root_disk || iscsi_system_is_rebooting) {
		if (!test_and_set_bit
		    (SESSION_TERMINATING, &session->control_bits)) {
		    if ((pid = session->tx_pid) && (pid != current->pid)) {
			printk("iSCSI: rx thread %d received SIGKILL, "
			       "killing tx thread %d\n", current->pid, pid);
			kill_proc(pid, SIGKILL, 1);
		    }
		    if ((pid = session->rx_pid) && (pid != current->pid)) {
			printk("iSCSI: tx thread %d received SIGKILL, "
			       "killing rx thread %d\n", current->pid, pid);
			kill_proc(pid, SIGKILL, 1);
		    }
		}
		ret = 1;
	    }
	}
	/* on SIGHUP, drop the session, and try to establish
	 * a replacement session 
	 */
	if (sighup) {
	    if (test_and_clear_bit(SESSION_ESTABLISHED, &session->control_bits)) {
		if ((pid = session->tx_pid) && (pid != current->pid)) {
		    printk("iSCSI: rx thread %d received SIGHUP, "
			   "signaling tx thread %d\n", current->pid, pid);
		    kill_proc(pid, SIGHUP, 1);
		}
		if ((pid = session->rx_pid) && (pid != current->pid)) {
		    printk("iSCSI: tx thread %d received SIGHUP, "
			   "signaling rx thread %d\n", current->pid, pid);
		    kill_proc(pid, SIGHUP, 1);
		}
	    }
	    ret = 1;
	}
    }
    if (ret && !session->session_drop_time) {
	session->session_drop_time = jiffies ? jiffies : 1;
	session->session_alive = 0;
    }

    return ret;
}

/* caller must hold the session's task_lock */
static void
trigger_error_recovery(iscsi_session_t * session, unsigned int lun)
{
    iscsi_task_t *t;

    /* stop new tasks from being sent to this LUN */
    set_bit(lun, session->luns_timing_out);
    smp_wmb();

    /* fake timeouts for all tasks to the specified LUN in
     * order to trigger error recovery. 
     */
    DEBUG_EH("iSCSI: session %p faking task timeouts to trigger error "
	     "recovery for LUN %u at %lu\n", session, lun, jiffies);

    for (t = session->arrival_order.head; t; t = t->order_next) {
	if ((t->lun == lun) && t->scsi_cmnd && !test_bit(0, &t->timedout)) {
	    DEBUG_EH("iSCSI: session %p faking timeout of itt %u, task "
		     "%p, LUN %u, sc %p at %lu\n",
		     session, t->itt, t, t->lun, t->scsi_cmnd, jiffies);

	    /* make the command look like it timedout */
	    del_task_timer(t);
	    set_bit(0, &t->timedout);
	    /* ensure nothing will be completed until error recovery finishes */
	    set_bit(lun, session->luns_doing_recovery);
	}
    }
    smp_mb();
    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
}

unsigned int
iscsi_expected_data_length(Scsi_Cmnd * sc)
{
    unsigned int length = 0;

    if (sc == NULL)
	return 0;

    switch (sc->cmnd[0]) {
    case INQUIRY:
    case REQUEST_SENSE:
	length = sc->cmnd[4];
	return length;
    case REPORT_LUNS:
	length |= sc->cmnd[6] << 24;
	length |= sc->cmnd[7] << 16;
	length |= sc->cmnd[8] << 8;
	length |= sc->cmnd[9];
	return length;
    default:
	return sc->request_bufflen;
    }
}

/* compare against 2^31 */
#define SNA32_CHECK 2147483648UL

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
static int
sna_lt(uint32_t n1, uint32_t n2)
{
    return ((n1 != n2) && (((n1 < n2) && ((n2 - n1) < SNA32_CHECK))
			   || ((n1 > n2) && ((n2 - n1) < SNA32_CHECK))));
}

/* Serial Number Arithmetic, 32 bits, less than, RFC1982 */
static int
sna_lte(uint32_t n1, uint32_t n2)
{
    return ((n1 == n2) || (((n1 < n2) && ((n2 - n1) < SNA32_CHECK))
			   || ((n1 > n2) && ((n2 - n1) < SNA32_CHECK))));
}

/* difference isn't really a defined operation in SNA, but we'd like it so that
 * we can determine how many commands can be queued to a session.
 */
static int
cmdsn_window_size(uint32_t expected, uint32_t max)
{
    if ((expected <= max) && ((max - expected) < SNA32_CHECK)) {
	return (max - expected + 1);
    } else if ((expected > max) && ((expected - max) < SNA32_CHECK)) {
	/* window wraps around */
	return ((UINT32_MAX - expected) + 1 + max + 1);
    } else {
	/* window closed, or numbers bogus */
	return 0;
    }
}

/* remember old peak cmdsn window size, and report the largest */
static int
max_tasks_for_session(iscsi_session_t * session)
{
    if (session->ExpCmdSn == session->MaxCmdSn + 1)
	/* if the window is closed, report nothing,
	 * regardless of what it was in the past 
	 */
	return 0;
    else if (session->last_peak_window_size < session->current_peak_window_size)
	/* window increasing, so report the current peak size */
	return MIN(session->current_peak_window_size,
		   ISCSI_CMDS_PER_LUN * session->num_luns);
    else
	/* window decreasing.  report the previous peak size, in case it's
	 * a temporary decrease caused by the commands we're sending.
	 * we want to keep the right number of commands queued in the driver,
	 * ready to go as soon as they can.
	 */
	return MIN(session->last_peak_window_size,
		   ISCSI_CMDS_PER_LUN * session->num_luns);
}

/* possibly update the ExpCmdSN and MaxCmdSN, and peak window sizes */
static void
updateSN(iscsi_session_t * session, UINT32 expcmdsn, UINT32 maxcmdsn)
{
    int window_size;

    /* standard specifies this check for when to update
     * expected and max sequence numbers 
     */
    if (!sna_lt(maxcmdsn, expcmdsn - 1)) {
	if ((expcmdsn != session->ExpCmdSn)
	    && !sna_lt(expcmdsn, session->ExpCmdSn)) {
	    session->ExpCmdSn = expcmdsn;
	}
	if ((maxcmdsn != session->MaxCmdSn)
	    && !sna_lt(maxcmdsn, session->MaxCmdSn)) {

	    session->MaxCmdSn = maxcmdsn;

	    /* look for the peak window size */
	    window_size = cmdsn_window_size(expcmdsn, maxcmdsn);
	    if (window_size > session->current_peak_window_size)
		session->current_peak_window_size = window_size;

	    /* age peak window size info */
	    if (time_before(session->window_peak_check + (15 * HZ), jiffies)) {
		session->last_peak_window_size =
		    session->current_peak_window_size;
		session->current_peak_window_size = window_size;
		session->window_peak_check = jiffies;
	    }

	    /* memory barrier for all of that */
	    smp_mb();

	    /* wake the tx thread to try sending more commands */
	    wake_tx_thread(TX_SCSI_COMMAND, session);
	}

	/* record whether or not the command window for this session has closed,
	 * so that we can ping the target periodically to ensure we eventually
	 * find out that the window has re-opened.  
	 */
	if (maxcmdsn == expcmdsn - 1) {
	    /* record how many times this happens, to see
	     * how often we're getting throttled 
	     */
	    session->window_closed++;
	    /* prepare to poll the target to see if the window has reopened */
	    session->current_peak_window_size = 0;
	    session->last_window_check = jiffies;
	    smp_wmb();
	    set_bit(SESSION_WINDOW_CLOSED, &session->control_bits);
	    smp_mb();
	    DEBUG_QUEUE("iSCSI: session %p command window closed, ExpCmdSN "
			"%u, MaxCmdSN %u at %lu\n",
			session, session->ExpCmdSn, session->MaxCmdSn, jiffies);
	} else if (test_bit(SESSION_WINDOW_CLOSED, &session->control_bits)) {
	    DEBUG_QUEUE("iSCSI: session %p command window opened, ExpCmdSN "
			"%u, MaxCmdSN %u at %lu\n",
			session, session->ExpCmdSn, session->MaxCmdSn, jiffies);
	    clear_bit(SESSION_WINDOW_CLOSED, &session->control_bits);
	    smp_mb();
	} else {
	    DEBUG_FLOW("iSCSI: session %p - ExpCmdSN %u, MaxCmdSN %u at %lu\n",
		       session, session->ExpCmdSn, session->MaxCmdSn, jiffies);
	}
    }
}

/* add a session to some iSCSI HBA's collection of sessions. */
static int
add_session(iscsi_session_t * session)
{
    iscsi_session_t *prior, *next;
    iscsi_hba_t *hba;
    int hba_number;
    int channel_number;
    int ret = 0;
    int p;
    DECLARE_NOQUEUE_FLAGS;

    /* find the HBA that has the desired iSCSI bus */
    hba_number = session->iscsi_bus / ISCSI_MAX_CHANNELS_PER_HBA;
    channel_number = session->iscsi_bus % ISCSI_MAX_CHANNELS_PER_HBA;

    spin_lock(&iscsi_hba_list_lock);
    hba = iscsi_hba_list;
    while (hba && (hba_number-- > 0)) {
	hba = hba->next;
    }

    if (!hba) {
	printk("iSCSI: couldn't find HBA with iSCSI bus %d\n",
	       session->iscsi_bus);
	spin_unlock(&iscsi_hba_list_lock);
	return 0;
    }
    if (!test_bit(ISCSI_HBA_ACTIVE, &hba->flags)) {
	printk("iSCSI: HBA %p is not active, can't add session %p\n", hba,
	       session);
	spin_unlock(&iscsi_hba_list_lock);
	return 0;
    }
    if (!hba->host) {
	printk("iSCSI: HBA %p has no host, can't add session %p\n", hba,
	       session);
	spin_unlock(&iscsi_hba_list_lock);
	return 0;
    }
    if (test_bit(ISCSI_HBA_RELEASING, &hba->flags)) {
	printk("iSCSI: releasing HBA %p, can't add session %p\n", hba, session);
	spin_unlock(&iscsi_hba_list_lock);
	return 0;
    }
    if (test_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags)) {
	printk("iSCSI: HBA %p is shutting down, can't add session %p\n", hba,
	       session);
	spin_unlock(&iscsi_hba_list_lock);
	return 0;
    }

    SPIN_LOCK_NOQUEUE(&hba->session_lock);

    prior = NULL;
    next = hba->session_list_head;
    /* skip earlier channels */
    while (next && (next->channel < session->channel)) {
	prior = next;
	next = prior->next;
    }
    /* skip earlier targets on the same channel */
    while (next && (next->channel == session->channel)
	   && (next->target_id < session->target_id)) {
	prior = next;
	next = prior->next;
    }

    /* same Linux SCSI address? */
    if (next && (next->channel == session->channel)
	&& (next->target_id == session->target_id)) {
	if (strcmp(next->TargetName, session->TargetName)) {
	    /* warn that some other target has it */
	    printk("iSCSI: bus %d target %d is already claimed for %s, "
		   "can't claim for %s\n",
		   session->iscsi_bus, next->target_id, session->TargetName,
		   next->TargetName);
	}
	ret = 0;
    } else {
	/* insert the session into the list */
	if ((session->next = next))
	    next->prev = session;
	else
	    hba->session_list_tail = session;

	if ((session->prev = prior))
	    prior->next = session;
	else
	    hba->session_list_head = session;

	session->hba = hba;
	session->host_no = hba->host->host_no;
	atomic_inc(&hba->num_sessions);

	/* log the session's bus, target id, TargetName, and all of
	 * the portals, so that the user has a record of what targets
	 * the kernel module was given.  We do this with locks held so
	 * that no other session's info will get interleaved while
	 * we're printing this one's.
	 */
	printk("iSCSI: bus %d target %d = %s\n", session->iscsi_bus,
	       session->target_id, session->TargetName);
	for (p = 0; p < session->num_portals; p++) {
	    /* FIXME: IPv6 */
	    printk("iSCSI: bus %d target %d portal %u = address %u.%u.%u.%u "
		   "port %d group %d\n",
		   session->iscsi_bus, session->target_id, p,
		   session->portals[p].ip_address[0],
		   session->portals[p].ip_address[1],
		   session->portals[p].ip_address[2],
		   session->portals[p].ip_address[3], session->portals[p].port,
		   session->portals[p].tag);
	}

	ret = 1;
    }

    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
    spin_unlock(&iscsi_hba_list_lock);

    return ret;
}

/* remove a session from an HBA's collection of sessions.
 * caller must hold the HBA's session lock.
 */
static int
remove_session(iscsi_hba_t * hba, iscsi_session_t * session)
{
    if (session->hba && (hba != session->hba)) {
	printk("iSCSI: tried to remove session %p from hba %p, "
	       "but session->hba is %p\n", session, hba, session->hba);
	return 0;
    }

    /* remove the session from the HBA */
    if (session == hba->session_list_head) {
	if ((hba->session_list_head = session->next))
	    hba->session_list_head->prev = NULL;
	else
	    hba->session_list_tail = NULL;
    } else if (session == hba->session_list_tail) {
	hba->session_list_tail = session->prev;
	hba->session_list_tail->next = NULL;
    } else {
	/* we should always be in the middle, 
	 * but check pointers to make sure we don't crash the kernel 
	 * if the function is called for a session not on the hba.
	 */
	if (session->next && session->prev) {
	    session->next->prev = session->prev;
	    session->prev->next = session->next;
	} else {
	    printk("iSCSI: failed to remove session %p from hba %p\n",
		   session, hba);
	    return 0;
	}
    }
    session->prev = NULL;
    session->next = NULL;

    return 1;
}

static iscsi_session_t *
find_session_for_cmnd(Scsi_Cmnd * sc)
{
    iscsi_session_t *session = NULL;
    iscsi_hba_t *hba;
    DECLARE_NOQUEUE_FLAGS;

    if (!sc->host)
	return NULL;

    if (!sc->host->hostdata)
	return NULL;

    hba = (iscsi_hba_t *) sc->host->hostdata;

    /* find the session for this command */
    /* FIXME: may want to cache the last session we looked
     * for, since we'll often get burst of requests for the
     * same session when multiple commands are queued. Would
     * need to invalidate the cache when a session is
     * removed from the HBA.
     */
    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    session = hba->session_list_head;
    while (session
	   && (session->channel != sc->channel
	       || session->target_id != sc->target))
	session = session->next;
    if (session)
	atomic_inc(&session->refcount);	/* caller must use drop_reference 
					 * when it's done with the session 
					 */
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    return session;
}

#if 0
static iscsi_session_t *
find_session_by_channel(unsigned int host_no, unsigned int channel,
			unsigned int target_id)
{
    iscsi_session_t *session = NULL;
    iscsi_hba_t *hba;
    DECLARE_NOQUEUE_FLAGS;

    spin_lock(&iscsi_hba_list_lock);

    hba = iscsi_hba_list;
    while (hba && (hba->host_no != host_no)) {
	hba = hba->next;
    }

    /* find the session for this command */
    if (hba) {
	SPIN_LOCK_NOQUEUE(&hba->session_lock);
	session = hba->session_list_head;
	while (session
	       && (session->channel != channel
		   || session->target_id != target_id))
	    session = session->next;
	if (session)
	    atomic_inc(&session->refcount);	/* caller must use 
						 * drop_reference when 
						 * it's done with the session 
						 */
	SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
    }

    spin_unlock(&iscsi_hba_list_lock);

    return session;
}
#endif

static iscsi_session_t *
find_session_by_bus(int iscsi_bus, int target_id)
{
    iscsi_session_t *session = NULL;
    iscsi_hba_t *hba;
    unsigned int hba_index;
    unsigned int channel;
    DECLARE_NOQUEUE_FLAGS;

    /* compute the appropriate HBA and channel numbers */
    hba_index = iscsi_bus / ISCSI_MAX_CHANNELS_PER_HBA;
    channel = iscsi_bus % ISCSI_MAX_CHANNELS_PER_HBA;

    spin_lock(&iscsi_hba_list_lock);

    hba = iscsi_hba_list;
    while (hba && (hba_index-- > 0)) {
	hba = hba->next;
    }

    /* find the session for this command */
    if (hba) {
	SPIN_LOCK_NOQUEUE(&hba->session_lock);
	session = hba->session_list_head;
	while (session
	       && (session->channel != channel
		   || session->target_id != target_id))
	    session = session->next;
	if (session)
	    atomic_inc(&session->refcount);	/* caller must use 
						 * drop_reference when 
						 * it's done with the session 
						 */
	SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
    }

    spin_unlock(&iscsi_hba_list_lock);

    return session;
}

static void
iscsi_task_ctor(void *obj, kmem_cache_t * cache, unsigned long flags)
{
    iscsi_task_t *task = (iscsi_task_t *) obj;

    memset(task, 0, sizeof (*task));
    task->flags = 0;
    task->itt = RSVD_TASK_TAG;
    task->ttt = RSVD_TASK_TAG;
    task->mgmt_itt = RSVD_TASK_TAG;
    task->next = task->prev = NULL;
    task->order_next = task->order_prev = NULL;
    init_timer(&task->timer);
    atomic_set(&task->refcount, 0);
}

static void
delete_session(iscsi_session_t * session)
{
    unsigned int host, channel, target;

    host = session->host_no;
    channel = session->channel;
    target = session->target_id;

    if (session->preallocated_task) {
	DEBUG_ALLOC("iSCSI: session %p for (%u %u %u *) freeing preallocated "
		    "task %p to cache %p prior to deleting session\n",
		    session, host, channel, target, session->preallocated_task,
		    session->hba->task_cache);
	iscsi_task_ctor(session->preallocated_task, NULL, 0);
	kmem_cache_free(session->hba->task_cache, session->preallocated_task);
	session->preallocated_task = NULL;
    }

    /* free the auth structures */
    if (session->auth_client_block)
	kfree(session->auth_client_block);
    if (session->auth_recv_string_block)
	kfree(session->auth_recv_string_block);
    if (session->auth_send_string_block)
	kfree(session->auth_send_string_block);
    if (session->auth_recv_binary_block)
	kfree(session->auth_recv_binary_block);
    if (session->auth_send_binary_block)
	kfree(session->auth_send_binary_block);

    if (session->username) {
	memset(session->username, 0, strlen(session->username));
	kfree(session->username);
	session->username = NULL;
    }
    if (session->password) {
	memset(session->password, 0, session->password_length);
	kfree(session->password);
	session->password = NULL;
    }
    if (session->username_in) {
	memset(session->username_in, 0, strlen(session->username_in));
	kfree(session->username_in);
	session->username_in = NULL;
    }
    if (session->password_in) {
	memset(session->password_in, 0, session->password_length_in);
	kfree(session->password_in);
	session->password_in = NULL;
    }
    if (session->portals) {
	kfree(session->portals);
	session->portals = NULL;
    }
    if (session->InitiatorName) {
	kfree(session->InitiatorName);
	session->InitiatorName = NULL;
    }
    if (session->InitiatorAlias) {
	kfree(session->InitiatorAlias);
	session->InitiatorAlias = NULL;
    }

    memset(session, 0, sizeof (*session));
    kfree(session);
}

/* decrement the session refcount, and remove it and free it
 * if the refcount hit zero */
static void
drop_reference(iscsi_session_t * session)
{
    iscsi_hba_t *hba;
    DECLARE_NOQUEUE_FLAGS;

    if (!session) {
	printk("iSCSI: bug - drop_reference(NULL)\n");
	return;
    }

    if ((hba = session->hba)) {
	/* may need to remove it from the HBA's session list */
	SPIN_LOCK_NOQUEUE(&hba->session_lock);
	if (atomic_dec_and_test(&session->refcount)) {
	    if (remove_session(hba, session)) {
		delete_session(session);
		atomic_dec(&hba->num_sessions);
		DEBUG_INIT("iSCSI: terminated and deleted session %p for "
			   "(%u %u %u *)\n",
			   session, session->host_no, session->channel,
			   session->target_id);
	    } else {
		printk("iSCSI: bug - failed to remove unreferenced "
		       "session %p\n", session);
	    }
	}
	SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
    } else {
	/* session isn't in an HBA's list at the moment, so just check
	 * the refcount, and possibly free it.  
	 */
	if (atomic_dec_and_test(&session->refcount)) {
	    delete_session(session);
	    DEBUG_INIT("iSCSI: terminated and deleted session %p for "
		       "(%u %u %u *)\n",
		       session, session->host_no, session->channel,
		       session->target_id);
	}
    }
}

/* must hold the task_lock to call this */
static iscsi_task_t *
find_session_task(iscsi_session_t * session, uint32_t itt)
{
    iscsi_task_t *task = session->arrival_order.head;

    while (task) {
	if (task->itt == itt) {
	    DEBUG_FLOW("iSCSI: found itt %u, task %p, refcount %d\n", itt, task,
		       atomic_read(&task->refcount));
	    return task;
	}
	task = task->order_next;
    }

    return NULL;
}

/* must hold the task_lock to call this */
static iscsi_task_t *
find_session_mgmt_task(iscsi_session_t * session, uint32_t mgmt_itt)
{
    iscsi_task_t *task = session->arrival_order.head;

    while (task) {
	if (task->mgmt_itt == mgmt_itt) {
	    DEBUG_FLOW("iSCSI: found mgmt_itt %u, task %p, refcount %d\n",
		       mgmt_itt, task, atomic_read(&task->refcount));
	    return task;
	}
	task = task->order_next;
    }

    return NULL;
}

#if 0

/* must hold the task_lock to call this */
static iscsi_task_t *
find_task(iscsi_task_collection_t * collection, uint32_t itt)
{
    iscsi_task_t *task = collection->head;

    while (task) {
	if (task->itt == itt) {
	    DEBUG_FLOW("iSCSI: found itt %u, task %p, refcount %d\n", itt, task,
		       atomic_read(&task->refcount));
	    return task;
	}
	task = task->next;
    }

    return NULL;
}

/* don't actually use this at the moment */
/* must hold the task_lock to call this */
static iscsi_task_t *
find_mgmt_task(iscsi_task_collection_t * collection, uint32_t mgmt_itt)
{
    iscsi_task_t *task = collection->head;

    while (task) {
	if (task->mgmt_itt == mgmt_itt) {
	    DEBUG_FLOW("iSCSI: found mgmt_itt %u, task %p\n", mgmt_itt, task);
	    return task;
	}
	task = task->next;
    }

    return NULL;
}

#endif

/* don't actually need this at the moment */
/* must hold the task_lock to call this */
static iscsi_task_t *
find_task_for_cmnd(iscsi_session_t * session, Scsi_Cmnd * sc)
{
    iscsi_task_t *task = session->arrival_order.head;

    while (task) {
	if (task->scsi_cmnd == sc) {
	    DEBUG_FLOW("iSCSI: found itt %u, task %p for cmnd %p\n", task->itt,
		       task, sc);
	    return task;
	}
	task = task->order_next;
    }

    return NULL;
}

/* add a task to the collection.  Must hold the task_lock to do this. */
static void
add_task(iscsi_task_collection_t * collection, iscsi_task_t * task)
{
    if (task->prev || task->next)
	printk("iSCSI: bug - adding task %p, prev %p, next %p, to "
	       "collection %p\n", task, task->prev, task->next, collection);

    if (collection->head) {
	task->next = NULL;
	task->prev = collection->tail;
	collection->tail->next = task;
	collection->tail = task;
    } else {
	task->prev = task->next = NULL;
	collection->head = collection->tail = task;
    }
}

/* must hold the task_lock when calling this */
static iscsi_task_t *
pop_task(iscsi_task_collection_t * collection)
{
    iscsi_task_t *task = NULL;

    if ((task = collection->head)) {
	/* pop the head */
	if ((collection->head = task->next))
	    collection->head->prev = NULL;
	else
	    collection->tail = NULL;

	/* and return it */
	task->prev = NULL;
	task->next = NULL;

	return task;
    }

    return NULL;
}

static void
unlink_task(iscsi_task_collection_t * collection, iscsi_task_t * task)
{
    /* unlink the task from the collection */
    if (task == collection->head) {
	if ((collection->head = task->next))
	    collection->head->prev = NULL;
	else
	    collection->tail = NULL;
    } else if (task == collection->tail) {
	collection->tail = task->prev;
	collection->tail->next = NULL;
    } else {
	task->next->prev = task->prev;
	task->prev->next = task->next;
    }
    task->next = NULL;
    task->prev = NULL;
}

/* if the task for the itt is found in the collection, remove it, and return it.
 * otherwise, return NULL.  Must hold the task_lock to call this.
 */
static iscsi_task_t *
remove_task(iscsi_task_collection_t * collection, uint32_t itt)
{
    iscsi_task_t *task = NULL;
    iscsi_task_t *search = collection->head;

    while (search) {
	if (search->itt == itt) {
	    task = search;
	    unlink_task(collection, task);
	    return task;
	}
	search = search->next;
    }

    return NULL;
}

/* if the task for the itt is found in the collection, remove it, and return it.
 * otherwise, return NULL.  Must hold the task_lock to call this.
 */
static iscsi_task_t *
remove_task_for_cmnd(iscsi_task_collection_t * collection, Scsi_Cmnd * sc)
{
    iscsi_task_t *task = NULL;
    iscsi_task_t *search = collection->head;

    while (search) {
	if (search->scsi_cmnd == sc) {
	    task = search;
	    unlink_task(collection, task);
	    return task;
	}
	search = search->next;
    }

    return NULL;
}

/* caller must hold the session's scsi_cmnd_lock */
static void
print_session_cmnds(iscsi_session_t * session)
{
    Scsi_Cmnd *search = session->retry_cmnd_head;
    printk("iSCSI: session %p retry cmnd queue: head %p, tail %p, "
	   "num %u at %lu\n",
	   session, session->retry_cmnd_head, session->retry_cmnd_tail,
	   atomic_read(&session->num_retry_cmnds), jiffies);
    while (search) {
	printk("iSCSI: session %p retry cmnd %p: cdb 0x%x to (%u %u %u %u) "
	       "flags 0x%01lx expires %lu\n",
	       session, search, search->cmnd[0], search->host->host_no,
	       search->channel, search->target, search->lun,
	       (unsigned long) *command_flags(search),
	       search->eh_timeout.expires);
	search = (Scsi_Cmnd *) search->host_scribble;
    }

    printk("iSCSI: session %p deferred cmnd queue: head %p, tail %p, num "
	   "%u at %lu\n",
	   session, session->deferred_cmnd_head, session->deferred_cmnd_tail,
	   session->num_deferred_cmnds, jiffies);
    search = session->deferred_cmnd_head;
    while (search) {
	printk("iSCSI: session %p deferred cmnd %p: cdb 0x%x to (%u %u %u %u) "
	       "flags 0x%01lx expires %lu\n",
	       session, search, search->cmnd[0], search->host->host_no,
	       search->channel, search->target, search->lun,
	       (unsigned long) *command_flags(search),
	       search->eh_timeout.expires);
	search = (Scsi_Cmnd *) search->host_scribble;
    }

    printk("iSCSI: session %p normal cmnd queue: head %p, tail %p, num %u "
	   "at %lu\n",
	   session, session->scsi_cmnd_head, session->scsi_cmnd_tail,
	   atomic_read(&session->num_cmnds), jiffies);
    search = session->scsi_cmnd_head;
    while (search) {
	printk("iSCSI: session %p normal cmnd %p: cdb 0x%x to (%u %u %u %u) "
	       "flags 0x%01lx expires %lu\n",
	       session, search, search->cmnd[0], search->host->host_no,
	       search->channel, search->target, search->lun,
	       (unsigned long) *command_flags(search),
	       search->eh_timeout.expires);
	search = (Scsi_Cmnd *) search->host_scribble;
    }
}

/* caller must hold the session's task_lock */
static void
print_session_tasks(iscsi_session_t * session)
{
    iscsi_task_t *task = NULL;
    Scsi_Cmnd *cmnd = NULL;

    printk("iSCSI: session %p task queue: head %p, tail %p, num %u at %lu\n",
	   session, session->arrival_order.head, session->arrival_order.tail,
	   atomic_read(&session->num_active_tasks), jiffies);

    task = session->arrival_order.head;
    while (task) {
	if ((cmnd = task->scsi_cmnd))
	    printk("iSCSI: session %p task %p: itt %u flags 0x%04lx expires "
		   "%lu %c, cmnd %p cdb 0x%x to (%u %u %u %u) flags "
		   "0x%01lx expires %lu\n",
		   session, task, task->itt, task->flags, task->timer.expires,
		   test_bit(0, &task->timedout) ? 'T' : ' ', cmnd,
		   cmnd->cmnd[0], cmnd->host->host_no, cmnd->channel,
		   cmnd->target, cmnd->lun,
		   (unsigned long) *command_flags(cmnd),
		   cmnd->eh_timeout.expires);
	else
	    printk("iSCSI: session %p task %p: itt %u flags 0x%04lx expires "
		   "%lu timedout %u, cmnd NULL, LUN %u\n",
		   session, task, task->itt, task->flags, task->timer.expires,
		   test_bit(0, &task->timedout) ? 1 : 0, task->lun);

	task = task->order_next;
    }
}

/* caller must hold the session's task lock */
static iscsi_task_t *
alloc_task(iscsi_session_t * session)
{
    iscsi_task_t *task = NULL;
    iscsi_hba_t *hba = session->hba;

    if (hba == NULL) {
	printk("iSCSI: session %p alloc_task failed - NULL HBA\n", session);
	return NULL;
    } else if (hba->task_cache == NULL) {
	printk("iSCSI: session %p alloc_task failed - NULL HBA task cache\n",
	       session);
	return NULL;
    }

    if ((task = kmem_cache_alloc(hba->task_cache, SLAB_ATOMIC))) {
	session->tasks_allocated++;
	task->session = session;
	DEBUG_ALLOC("iSCSI: session %p allocated task %p from cache at %lu\n",
		    session, task, jiffies);
    } else if (session->preallocated_task) {
	/* if the task cache is empty, we fall back to the
	 * session's preallocated task, which guarantees us
	 * at least some forward progress on every session.
	 */
	task = session->preallocated_task;
	session->preallocated_task = NULL;
	task->session = session;
	/* don't log by default.  We're more concerned with when a
	 * task alloc fails than when we use the preallocated task. 
	 */
	if (LOG_ENABLED(ISCSI_LOG_ALLOC))
	    printk("iSCSI: session %p to (%u %u %u *) task cache empty, using "
		   "preallocated task %p at %lu\n",
		   session, session->host_no, session->channel,
		   session->target_id, task, jiffies);
    } else {
	/* better luck later */
	task = NULL;
    }

    return task;
}

/* caller must hold the session's task lock */
static void
free_task(iscsi_session_t * session, iscsi_task_t * task)
{
    iscsi_hba_t *hba;

    if (!task) {
	printk("iSCSI: free_task couldn't free NULL task\n");
	return;
    }
    if (!session) {
	printk("iSCSI: free_task couldn't find session for task %p\n", task);
	return;
    }
    hba = session->hba;
    if (!hba) {
	printk("iSCSI: free_task couldn't find HBA for task %p\n", task);
	return;
    }

    if (task->next || task->prev || task->order_next || task->order_prev) {
	/* this is a memory leak, which is better than memory corruption */
	printk("iSCSI: bug - tried to free task %p with prev %p, next %p, "
	       "order_prev %p, order_next %p\n",
	       task, task->prev, task->next, task->order_prev,
	       task->order_next);
	return;
    }

    DEBUG_QUEUE("iSCSI: session %p free_task %p, itt %u\n", task->session, task,
		task->itt);

    if (test_bit(TASK_PREALLOCATED, &task->flags)) {
	if (session->preallocated_task) {
	    printk("iSCSI: bug - session %p has preallocated task %p, really "
		   "freeing %p itt %u flags 0x%0lx at %lu\n",
		   session, session->preallocated_task, task, task->itt,
		   task->flags, jiffies);

	    /* reinitialize the task for later use */
	    iscsi_task_ctor(task, NULL, 0);

	    kmem_cache_free(hba->task_cache, task);
	} else {
	    /* reinitialize the task for later use */
	    iscsi_task_ctor(task, NULL, 0);
	    __set_bit(TASK_PREALLOCATED, &task->flags);

	    /* save it for the next memory emergency */
	    session->preallocated_task = task;

	    /* wake up the tx thread, since it may have been forced to
	     * stop sending tasks once the prealloacte task was in use.
	     * Now that the preallocated task is back, we can guarantee
	     * this session can allocate at least one more task.  Too many
	     * wakeups is better than too few.  
	     */
	    printk("iSCSI: session %p to (%u %u %u *) done using preallocated "
		   "task %p at %lu\n",
		   session, session->host_no, session->channel,
		   session->target_id, task, jiffies);
	    wake_tx_thread(TX_SCSI_COMMAND, session);
	}
    } else {
	session->tasks_freed++;

	/* reinitialize the task for later use */
	iscsi_task_ctor(task, NULL, 0);

	/* return it to the cache */
	kmem_cache_free(hba->task_cache, task);
    }
}

/* As long as the tx thread is the only caller, no locking
 * is required.  If any other thread also needs to call this,
 * then all callers must be changed to agree on some locking
 * protocol.  Currently, some but not all caller's are holding
 * the session->task_lock.
 */
static inline uint32_t
allocate_itt(iscsi_session_t * session)
{
    uint32_t itt = 0;

    if (session) {
	itt = session->itt++;
	/* iSCSI reserves 0xFFFFFFFF, this driver reserves 0 */
	if (session->itt == RSVD_TASK_TAG)
	    session->itt = 1;
    }
    return itt;
}

/* Caller must hold the session's task_lock.  Associating a task with
 * a session causes it to be completed on a session drop or target
 * reset, along with all other session tasks, in the order they were
 * added to the session.  Preserving the ordering is required by the
 * Linux SCSI architecture.  Tasks that should not be completed to the
 * Linux SCSI layer (because the eh_abort_handler has or will return
 * SUCCESS for it) get removed from the session, though they may still
 * be in various task collections so that PDUs relating to them can be
 * sent or received.
 */
static void
add_session_task(iscsi_session_t * session, iscsi_task_t * task)
{
    if (atomic_read(&session->num_active_tasks) == 0) {
	/* session going from idle to active, pretend we just
	 * received something, so that the idle period before this doesn't
	 * cause an immediate timeout.
	 */
	session->last_rx = jiffies;
	smp_mb();
    }
    atomic_inc(&session->num_active_tasks);

    /* set task info */
    task->session = session;
    task->itt = allocate_itt(session);

    DEBUG_QUEUE("iSCSI: task %p allocated itt %u for command %p, session "
		"%p to %s\n",
		task, task->itt, task->scsi_cmnd, session, session->log_name);

    /* add it to the session task ordering list */
    if (session->arrival_order.head) {
	task->order_prev = session->arrival_order.tail;
	task->order_next = NULL;
	session->arrival_order.tail->order_next = task;
	session->arrival_order.tail = task;
    } else {
	task->order_prev = NULL;
	task->order_next = NULL;
	session->arrival_order.head = session->arrival_order.tail = task;
    }

    DEBUG_FLOW("iSCSI: task %p, itt %u, added to session %p to %s\n", task,
	       task->itt, session, session->log_name);
}

static int
remove_session_task(iscsi_session_t * session, iscsi_task_t * task)
{
    /* remove the task from the session's arrival_order collection */
    if (task == session->arrival_order.head) {
	if ((session->arrival_order.head = task->order_next))
	    session->arrival_order.head->order_prev = NULL;
	else
	    session->arrival_order.tail = NULL;
    } else if (task == session->arrival_order.tail) {
	session->arrival_order.tail = task->order_prev;
	session->arrival_order.tail->order_next = NULL;
    } else {
	/* we should always be in the middle, 
	 * but check pointers to make sure we don't crash the kernel 
	 * if the function is called for a task not in the session.
	 */
	if (task->order_next && task->order_prev) {
	    task->order_next->order_prev = task->order_prev;
	    task->order_prev->order_next = task->order_next;
	} else {
	    printk("iSCSI: failed to remove itt %u, task %p from session "
		   "%p to %s\n", task->itt, task, session, session->log_name);
	    return 0;
	}
    }
    task->order_prev = NULL;
    task->order_next = NULL;

    if (atomic_dec_and_test(&session->num_active_tasks)) {
	/* no active tasks, ready to logout */
	if (test_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p now has no active tasks, "
		       "queueing logout at %lu\n", session, jiffies);
	    wake_tx_thread(TX_LOGOUT, session);
	}
    }

    return 1;
}

static inline void
set_not_ready(Scsi_Cmnd * sc)
{
    sc->sense_buffer[0] = 0x70;
    sc->sense_buffer[2] = NOT_READY;
    sc->sense_buffer[7] = 0x0;
}

/* mark a Scsi_Cmnd as having a LUN communication failure */
static inline void
set_lun_comm_failure(Scsi_Cmnd * sc)
{
    sc->sense_buffer[0] = 0x70;
    sc->sense_buffer[2] = NOT_READY;
    sc->sense_buffer[7] = 0x6;
    sc->sense_buffer[12] = 0x08;
    sc->sense_buffer[13] = 0x00;
}

/* decode common network errno values into more useful strings.
 * strerror would be nice right about now.
 */
static char *
iscsi_strerror(int errno)
{
    switch (errno) {
    case EIO:
	return "I/O error";
    case EINTR:
	return "Interrupted system call";
    case ENXIO:
	return "No such device or address";
    case EFAULT:
	return "Bad address";
    case EBUSY:
	return "Device or resource busy";
    case EINVAL:
	return "Invalid argument";
    case EPIPE:
	return "Broken pipe";
    case ENONET:
	return "Machine is not on the network";
    case ECOMM:
	return "Communication error on send";
    case EPROTO:
	return "Protocol error";
    case ENOTUNIQ:
	return "Name not unique on network";
    case ENOTSOCK:
	return "Socket operation on non-socket";
    case ENETDOWN:
	return "Network is down";
    case ENETUNREACH:
	return "Network is unreachable";
    case ENETRESET:
	return "Network dropped connection because of reset";
    case ECONNABORTED:
	return "Software caused connection abort";
    case ECONNRESET:
	return "Connection reset by peer";
    case ESHUTDOWN:
	return "Cannot send after shutdown";
    case ETIMEDOUT:
	return "Connection timed out";
    case ECONNREFUSED:
	return "Connection refused";
    case EHOSTDOWN:
	return "Host is down";
    case EHOSTUNREACH:
	return "No route to host";
    default:
	return "";
    }
}

static int
iscsi_recvmsg(iscsi_session_t * session, struct msghdr *msg, int len)
{
    int rc = 0;
    mm_segment_t oldfs;

    if (session->socket) {
	oldfs = get_fs();
	set_fs(get_ds());

	/* Try to avoid memory allocation deadlocks by using GFP_ATOMIC. */
	session->socket->sk->allocation = GFP_ATOMIC;

	rc = sock_recvmsg(session->socket, msg, len, MSG_WAITALL);

	set_fs(oldfs);
    }

    return rc;
}

static int
iscsi_sendmsg(iscsi_session_t * session, struct msghdr *msg, int len)
{
    int rc = 0;
    mm_segment_t oldfs;

    if (session->socket) {
	oldfs = get_fs();
	set_fs(get_ds());

	/* Try to avoid resource acquisition deadlocks by using GFP_ATOMIC. */
	session->socket->sk->allocation = GFP_ATOMIC;

	/* FIXME: ought to loop handling short writes, unless a signal occurs */
	rc = sock_sendmsg(session->socket, msg, len);

	set_fs(oldfs);
    }

    return rc;
}

/* create and connect a new socket for this session */
int
iscsi_connect(iscsi_session_t * session)
{
    mm_segment_t oldfs;
    struct socket *socket = NULL;
    struct sockaddr_in addr, laddr;
    int window_size;
    int arg = 1, arglen = 0;
    int rc = 0, ret = 0;

    if (session->socket) {
	printk("iSCSI: session %p already has socket %p\n", session,
	       session->socket);
	return 0;
    }

    oldfs = get_fs();
    set_fs(get_ds());

    /* FIXME: sock_create may (indirectly) call the slab
     * allocator with SLAB_KERNEL, which can fail if the
     * cache needs to allocate another page.  Should we
     * preallocate a socket before starting the session, so
     * that we have another to use if the first one drops?
     * VM livelock can occur if the VM can't write to iSCSI
     * disks when it needs to clean pages.  To be useful it
     * would have to work more than once, which means
     * finding some way to safely allocate another socket
     * for the next low-memory problem.  A better solution
     * would be to find a way to avoid freeing the current
     * socket.  If we abort the connection instead of close
     * it, can we reuse the existing socket instead of
     * allocating a new one?
     */
    rc = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &socket);
    if (rc < 0) {
	printk("iSCSI: session %p failed to create socket, rc %d\n", session,
	       rc);
	set_fs(oldfs);
	return 0;
    }

    /* no delay in sending */
    rc = socket->ops->setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
				 (char *) &arg, sizeof (arg));
    if (rc < 0) {
	printk("iSCSI: session %p failed to setsockopt TCP_NODELAY, rc %d\n",
	       session, rc);
	goto done;
    }

    /* try to ensure a reasonably sized TCP window */
    arglen = sizeof (window_size);
    if (sock_getsockopt
	(socket, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, &arglen) >= 0) {
	DEBUG_FLOW("iSCSI: session %p TCP recv window size %u\n", session,
		   window_size);

	if (session->tcp_window_size
	    && (window_size < session->tcp_window_size)) {
	    window_size = session->tcp_window_size;
	    rc = sock_setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
				 (char *) &window_size, sizeof (window_size));
	    if (rc < 0) {
		printk("iSCSI: session %p failed to set TCP recv window "
		       "size to %u, rc %d\n", session, window_size, rc);
	    } else
		if (sock_getsockopt
		    (socket, SOL_SOCKET, SO_RCVBUF, (char *) &window_size,
		     &arglen) >= 0) {
		DEBUG_INIT("iSCSI: session %p set TCP recv window size "
			   "to %u, actually got %u\n",
			   session, session->tcp_window_size, window_size);
	    }
	}
    } else {
	printk("iSCSI: session %p getsockopt RCVBUF %p failed\n", session,
	       socket);
    }

    if (sock_getsockopt
	(socket, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, &arglen) >= 0) {
	DEBUG_FLOW("iSCSI: session %p TCP send window size %u\n", session,
		   window_size);

	if (session->tcp_window_size
	    && (window_size < session->tcp_window_size)) {
	    window_size = session->tcp_window_size;
	    rc = sock_setsockopt(socket, SOL_SOCKET, SO_SNDBUF,
				 (char *) &window_size, sizeof (window_size));
	    if (rc < 0) {
		printk("iSCSI: session %p failed to set TCP send window "
		       "size to %u, rc %d\n", session, window_size, rc);
	    } else
		if (sock_getsockopt
		    (socket, SOL_SOCKET, SO_SNDBUF, (char *) &window_size,
		     &arglen) >= 0) {
		DEBUG_INIT("iSCSI: session %p set TCP send window "
			   "size to %u, actually got %u\n",
			   session, session->tcp_window_size, window_size);
	    }
	}
    } else {
	printk("iSCSI: session %p getsockopt SNDBUF %p failed\n", session,
	       socket);
    }
    /* In case of iSCSI boot, bring up the network interface, used for iSCSI 
     * boot, if it is down.
     */
    if (this_is_iscsi_boot) {
	if (session->this_is_root_disk) {
	    if (!iscsi_set_if_addr()) {
		printk("\niSCSI: iscsi_set_if_addr failed !!!\n");
	    }
	}
    }
    if (session->local_ip_length) {
	memset((char *) &laddr, 0, sizeof (laddr));
	laddr.sin_family = AF_INET;
	/*Address to accept any incoming messages */
	memcpy(&laddr.sin_addr.s_addr, session->local_ip_address,
	       session->local_ip_length);
	if (socket->ops->
	    bind(socket, (struct sockaddr *) &laddr, sizeof (laddr))
	    < 0) {
	    printk("iSCSI:session %p unable to bind to local address "
		   "%u.%u.%u.%u\n",
		   session, session->local_ip_address[0],
		   session->local_ip_address[1], session->local_ip_address[2],
		   session->local_ip_address[3]);
	    goto done;
	}
    }

    /* connect to the target */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(session->port);
    if (session->ip_length == 4) {
	memcpy(&addr.sin_addr.s_addr, session->ip_address,
	       MIN(sizeof (addr.sin_addr.s_addr), session->ip_length));
    } else {
	/* FIXME: IPv6 */
	printk("iSCSI: session %p unable to handle IPv6 address, length "
	       "%u, addr %u.%u.%u.%u\n",
	       session, session->ip_length, session->ip_address[0],
	       session->ip_address[1], session->ip_address[2],
	       session->ip_address[3]);
	goto done;
    }
    rc = socket->ops->connect(socket, (struct sockaddr *) &addr, sizeof (addr),
			      0);

    if (signal_pending(current))
	goto done;

    if (rc < 0) {
	char *error = iscsi_strerror(-rc);
	if (error && error[0] != '\0') {
	    printk("iSCSI: session %p to %s failed to connect, rc %d, %s\n",
		   session, session->log_name, rc, error);
	} else {
	    printk("iSCSI: session %p to %s failed to connect, rc %d\n",
		   session, session->log_name, rc);
	}
    } else {
	if (LOG_ENABLED(ISCSI_LOG_LOGIN))
	    printk("iSCSI: session %p to %s connected at %lu\n", session,
		   session->log_name, jiffies);
	ret = 1;
    }

  done:
    if (ret) {
	/* save the socket pointer for later */
	session->socket = socket;
    } else {
	/* close the socket */
	sock_release(socket);
	session->socket = NULL;
    }
    smp_mb();
    set_fs(oldfs);
    return ret;
}

void
iscsi_disconnect(iscsi_session_t * session)
{
    if (session->socket) {
#if TCP_ABORT_ON_DROP
	if (test_and_clear_bit(SESSION_DROPPED, &session->control_bits) &&
	    !test_bit(SESSION_LOGGED_OUT, &session->control_bits)) {
	    /* setting linger on and lingertime to 0 before closing
	     * the socket will trigger a TCP abort (abort all sends
	     * and receives, possibly send RST, connection to CLOSED),
	     * which is probably what we want if we're dropping and
	     * restarting a session.  A TCP Abort will discard TCP
	     * data, which is probably a bunch of commands and data
	     * we'll resend on a new session anyway.  This frees up
	     * skbuffs, and makes the VM livelock less likely.  When
	     * we relogin again to the target with the same ISID, the
	     * target will kill off the old connections on it's side,
	     * so the FIN handshake should be unnecessary, and there
	     * are cases where network failures may prevent the FIN
	     * handshake from completing, so the connection wouldn't
	     * get cleaned up unless the TCP stack has timeouts for
	     * some of the TCP states.
	     */
	    struct linger ling;
	    mm_segment_t oldfs;

	    memset(&ling, 0, sizeof (ling));
	    ling.l_onoff = 1;
	    ling.l_linger = 0;

	    /* we could adjust the socket linger values
	     * directly, but using the sockopt call is less
	     * likely to break if someone overhauls the
	     * socket structure.
	     */
	    oldfs = get_fs();
	    set_fs(get_ds());

	    if (sock_setsockopt
		(session->socket, IPPROTO_TCP, SO_LINGER, (char *) &ling,
		 sizeof (ling)) < 0) {
		printk("iSCSI: session %p couldn't set lingertime to zero "
		       "after session drop\n", session);
	    } else {
		DEBUG_INIT("iSCSI: session %p set lingertime to zero "
			   "because of session drop\n", session);
	    }

	    set_fs(oldfs);
	}
#endif

	/* close the socket, triggering either a TCP close or a TCP abort */
	sock_release(session->socket);

	session->socket = NULL;
	smp_mb();
    }
}

int
iscsi_send_pdu(iscsi_session_t * session, struct IscsiHdr *pdu, char *data,
	       int timeout)
{
    struct msghdr msg;
    struct iovec iov[3];
    char padding[4];
    int pad = 0;
    int rc;
    int pdu_length = 0;
    int data_length;

    if (pdu == NULL) {
	printk("iSCSI: session %p, pdu NULL, can't send PDU header\n", session);
	return 0;
    }

    memset(iov, 0, sizeof (iov));
    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    /* pdu header */
    iov[0].iov_base = pdu;
    iov[0].iov_len = sizeof (*pdu);
    pdu_length = sizeof (*pdu);

    /* pdu data */
    data_length = ntoh24(pdu->dlength);
    if (data) {
	iov[msg.msg_iovlen].iov_base = data;
	iov[msg.msg_iovlen].iov_len = data_length;
	msg.msg_iovlen++;
	pdu_length += ntoh24(pdu->dlength);
    } else if (data_length) {
	printk("iSCSI: session %p pdu %p with dlength %d, but data NULL\n",
	       session, pdu, data_length);
	return 0;
    }

    /* add any padding needed */
    if (pdu_length % PAD_WORD_LEN) {
	memset(padding, 0x0, sizeof (padding));
	pad = PAD_WORD_LEN - (pdu_length % PAD_WORD_LEN);
    }
    if (pad) {
	iov[msg.msg_iovlen].iov_base = padding;
	iov[msg.msg_iovlen].iov_len = pad;
	msg.msg_iovlen++;
	pdu_length += pad;
    }

    /* set a timer, though we shouldn't really need one */
    if (timeout) {
	session->login_phase_timer = jiffies + (timeout * HZ);
	smp_mb();
    }

    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
	char *text = data;
	char *end = text + ntoh24(pdu->dlength);
	int show_text = 0;

	if ((pdu->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGIN_CMD) {
	    struct IscsiLoginHdr *login_pdu = (struct IscsiLoginHdr *) pdu;
	    /* show the login phases and tbit */
	    printk("iSCSI: session %p sending login pdu with current "
		   "phase %d, next %d, transit 0x%x, dlength %d at %lu, "
		   "timeout at %lu (%d seconds)\n",
		   session, ISCSI_LOGIN_CURRENT_STAGE(login_pdu->flags),
		   ISCSI_LOGIN_NEXT_STAGE(login_pdu->flags),
		   login_pdu->flags & ISCSI_FLAG_LOGIN_TRANSIT,
		   ntoh24(pdu->dlength), jiffies, session->login_phase_timer,
		   session->login_timeout);
	    show_text = 1;
	} else if ((pdu->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_TEXT_CMD) {
	    printk("iSCSI: session %p sending text pdu, dlength %d at %lu, "
		   "timeout at %lu (%d seconds)\n",
		   session, ntoh24(pdu->dlength), jiffies,
		   session->login_phase_timer, session->login_timeout);
	    show_text = 1;
	} else {
	    printk("iSCSI: session %p sending pdu with opcode 0x%x, dlength "
		   "%d at %lu, timeout at %lu (%d seconds)\n",
		   session, pdu->opcode, ntoh24(pdu->dlength), jiffies,
		   session->login_phase_timer, session->login_timeout);
	}

	/* show all the text that we're sending */
	while (show_text && (text < end)) {
	    printk("iSCSI: session %p login text: %s\n", session, text);
	    text += strlen(text);
	    while ((text < end) && (*text == '\0'))
		text++;
	}
    }

    rc = iscsi_sendmsg(session, &msg, pdu_length);

    /* clear the timer */
    session->login_phase_timer = 0;
    smp_mb();

    if (rc != pdu_length) {
	char *error;
	if ((rc < 0) && (error = iscsi_strerror(-rc)) && (error[0] != '\0'))
	    printk("iSCSI: session %p failed to send login PDU, rc %d, %s\n",
		   session, rc, iscsi_strerror(-rc));
	else
	    printk("iSCSI: session %p failed to send login PDU, rc %d\n",
		   session, rc);

	return 0;
    }

    DEBUG_FLOW("iSCSI: session %p sent login pdu %p at %lu, length %d, "
	       "dlength %d\n",
	       session, pdu, jiffies, pdu_length, ntoh24(pdu->dlength));

    return 1;
}

/* try to read an entire login PDU into the buffer, timing
 * out after timeout seconds */
int
iscsi_recv_pdu(iscsi_session_t * session, struct IscsiHdr *header,
	       int max_header_length, char *data, int max_data_length,
	       int timeout)
{
    struct msghdr msg;
    struct iovec iov[2];
    char padding[PAD_WORD_LEN];
    int rc = 0;
    int data_length;
    int ret = 0;

    if (header == NULL) {
	printk("iSCSI: session %p, can't receive PDU header into NULL\n",
	       session);
	return 0;
    }

    if (max_header_length < sizeof (*header)) {
	printk("iSCSI: session %p, can't receive %Zu PDU header bytes "
	       "into %d byte buffer\n",
	       session, sizeof (*header), max_header_length);
	return 0;
    }

    /* set the timer to implement the timeout requested */
    if (timeout)
	session->login_phase_timer = jiffies + (timeout * HZ);
    else
	session->login_phase_timer = 0;
    smp_mb();
    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
	printk("iSCSI: session %p trying to recv login pdu at %lu, timeout "
	       "at %lu (%d seconds)\n",
	       session, jiffies, session->login_phase_timer, timeout);
    }

    /* read the PDU header */
    memset(iov, 0, sizeof (iov));
    iov[0].iov_base = (void *) header;
    iov[0].iov_len = sizeof (*header);
    memset(&msg, 0, sizeof (struct msghdr));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    rc = iscsi_recvmsg(session, &msg, sizeof (*header));

    /* FIXME: check for additional header segments */

    if (signal_pending(current)) {
	printk("iSCSI: session %p recv_login_pdu timed out at %lu\n", session,
	       jiffies);
	goto done;
    }

    if (rc != sizeof (*header)) {
	if (rc < 0) {
	    char *error = iscsi_strerror(-rc);
	    if (error && error[0] != '\0') {
		printk("iSCSI: session %p recv_login_pdu failed to recv %d "
		       "login PDU bytes, rc %d, %s\n",
		       session, (int) iov[0].iov_len, rc, iscsi_strerror(-rc));
	    } else {
		printk("iSCSI: session %p recv_login_pdu failed to recv %d "
		       "login PDU bytes, rc %d\n", session, (int) iov[0].iov_len, rc);
	    }
	} else if (rc == 0) {
	    printk("iSCSI: session %p recv_login_pdu: connection closed\n",
		   session);
	} else {
	    /* short reads should be impossible unless a signal occured,
	     * which we already checked for.
	     */
	    printk("iSCSI: bug - session %p recv_login_pdu, short read %d "
		   "of %Zu\n", session, rc, sizeof (*header));
	}
	goto done;
    }
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    /* possibly read PDU data */
    data_length = ntoh24(header->dlength);
    if (data_length) {
	/* check for buffer overflow */
	if (data_length > max_data_length) {
	    printk("iSCSI: session %p recv_login_pdu can't read %d bytes "
		   "of login PDU data, only %d bytes of buffer available\n",
		   session, data_length, max_data_length);
	    goto done;
	}

	/* read the PDU's text data payload */
	memset(&msg, 0, sizeof (struct msghdr));
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	memset(iov, 0, sizeof (iov));
	iov[0].iov_base = data;
	iov[0].iov_len = data_length;

	/* handle PDU padding */
	if (data_length % PAD_WORD_LEN) {
	    int pad = PAD_WORD_LEN - (data_length % PAD_WORD_LEN);

	    iov[1].iov_base = padding;
	    iov[1].iov_len = pad;
	    msg.msg_iovlen = 2;
	    data_length += pad;
	}

	rc = iscsi_recvmsg(session, &msg, data_length);

	if (signal_pending(current)) {
	    printk("iSCSI: session %p recv_login_pdu timed out at %lu\n",
		   session, jiffies);
	    goto done;
	}

	if (rc != data_length) {
	    if (rc < 0) {
		char *error = iscsi_strerror(-rc);
		if (error && error[0] != '\0') {
		    printk("iSCSI: session %p recv_login_pdu failed to "
			   "recv %d login data PDU bytes, rc %d, %s\n",
			   session, data_length, rc, iscsi_strerror(-rc));
		} else {
		    printk("iSCSI: session %p recv_login_pdu failed to "
			   "recv %d login data PDU bytes, rc %d\n",
			   session, data_length, rc);
		}
		ret = rc;
	    } else if (rc == 0) {
		printk("iSCSI: session %p recv_login_pdu: connection closed\n",
		       session);
	    } else {
		/* short reads should be impossible unless a signal occured,
		 * which we already checked for.
		 */
		printk("iSCSI: bug - session %p recv_login_pdu, short read %d "
		       "of %d\n", session, rc, data_length);
	    }
	    goto done;
	}

	/* assume a PDU round-trip, connection is ok */
	session->last_rx = jiffies;
	smp_mb();
    }

    if (LOG_ENABLED(ISCSI_LOG_LOGIN)) {
	char *text = data;
	char *end = text + ntoh24(header->dlength);
	int show_text = 0;

	if (header->opcode == ISCSI_OP_LOGIN_RSP) {
	    struct IscsiLoginRspHdr *login_pdu =
		(struct IscsiLoginRspHdr *) header;
	    /* show the login phases and transit bit */
	    printk("iSCSI: session %p received login pdu response at %lu "
		   "with current stage %d, next %d, transit 0x%x, dlength %d\n",
		   session, jiffies,
		   ISCSI_LOGIN_CURRENT_STAGE(login_pdu->flags),
		   ISCSI_LOGIN_NEXT_STAGE(login_pdu->flags),
		   login_pdu->flags & ISCSI_FLAG_LOGIN_TRANSIT,
		   ntoh24(header->dlength));
	    show_text = 1;
	} else if (header->opcode == ISCSI_OP_TEXT_RSP) {
	    printk("iSCSI: session %p received text pdu response with "
		   "dlength %d at %lu\n",
		   session, ntoh24(header->dlength), jiffies);
	    show_text = 1;
	} else {
	    printk("iSCSI: session %p received pdu with opcode 0x%x, "
		   "dlength %d at %lu\n",
		   session, header->opcode, ntoh24(header->dlength), jiffies);
	}

	/* show all the text that we're sending */
	while (show_text && (text < end)) {
	    printk("iSCSI: session %p login resp text: %s\n", session, text);
	    text += strlen(text);
	    while ((text < end) && (*text == '\0'))
		text++;
	}
    }

    ret = 1;

  done:
    /* clear the timer */
    session->login_phase_timer = 0;
    smp_mb();
    iscsi_handle_signals(session);

    return ret;
}

#if DEBUG_TRACE
static void
iscsi_fill_trace(unsigned char type, Scsi_Cmnd * sc, iscsi_task_t * task,
		 unsigned long data1, unsigned long data2)
{
    iscsi_trace_entry_t *te;
    cpu_flags_t flags;

    spin_lock_irqsave(&iscsi_trace_lock, flags);

    te = &trace_table[trace_index];
    trace_index++;
    if (trace_index >= ISCSI_TRACE_COUNT) {
	trace_index = 0;
    }
    memset(te, 0x0, sizeof (*te));

    te->type = type;
    if (task) {
	iscsi_session_t *session = task->session;

	te->host = session->host_no;
	te->channel = session->channel;
	te->target = session->target_id;
	te->lun = task->lun;
	te->itt = task->itt;
    }
    if (sc) {
	te->cmd = sc->cmnd[0];
	te->host = sc->host->host_no;
	te->channel = sc->channel;
	te->target = sc->target;
	te->lun = sc->lun;
    }
    te->data1 = data1;
    te->data2 = data2;
    te->jiffies = jiffies;

    spin_unlock_irqrestore(&iscsi_trace_lock, flags);
}
#endif

/* caller must either hold the task, or keep the task
 * refcount non-zero while calling this 
 */
static void
iscsi_set_direction(iscsi_task_t * task)
{
    if (task && task->scsi_cmnd) {
	switch (task->scsi_cmnd->sc_data_direction) {
	case SCSI_DATA_WRITE:
	    __set_bit(TASK_WRITE, &task->flags);
	    break;
	case SCSI_DATA_READ:
	    __set_bit(TASK_READ, &task->flags);
	    break;
	case SCSI_DATA_NONE:
	case SCSI_DATA_UNKNOWN:
	    break;
	}
    }
}

/* tagged queueing */
static inline unsigned int
iscsi_command_attr(Scsi_Cmnd * cmd)
{
    if (cmd->device && cmd->device->tagged_queue) {
	switch (cmd->tag) {
	case HEAD_OF_QUEUE_TAG:
	    return ISCSI_ATTR_HEAD_OF_QUEUE;
	case ORDERED_QUEUE_TAG:
	    return ISCSI_ATTR_ORDERED;
	default:
	    return ISCSI_ATTR_SIMPLE;
	}
    }

    return ISCSI_ATTR_UNTAGGED;
}

static void
print_cmnd(Scsi_Cmnd * sc)
{
#ifdef HAS_CMND_REQUEST_STRUCT
    struct request *req = &sc->request;
    struct buffer_head *bh = NULL;
#endif

    printk("iSCSI: Scsi_Cmnd %p to (%u %u %u %u), Cmd 0x%x\n"
	   "iSCSI:   done %p, scsi_done %p, host_scribble %p\n"
	   "iSCSI:   reqbuf %p, req_len %u\n"
	   "iSCSI:   buffer %p, bufflen %u\n"
	   "iSCSI:   use_sg %u, old_use_sg %u, sglist_len %u\n"
	   "iSCSI:   owner 0x%x, state  0x%x, eh_state 0x%x\n"
	   "iSCSI:   cmd_len %u, old_cmd_len %u\n",
	   sc, sc->host->host_no, sc->channel, sc->target, sc->lun, sc->cmnd[0],
	   sc->done, sc->scsi_done, sc->host_scribble,
	   sc->request_buffer, sc->request_bufflen, sc->buffer, sc->bufflen,
	   sc->use_sg, sc->old_use_sg, sc->sglist_len,
	   sc->owner, sc->state, sc->eh_state, sc->cmd_len, sc->old_cmd_len);

    if (sc->cmd_len >= 12)
	printk
	    ("iSCSI:   cdb %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
	     sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3], sc->cmnd[4],
	     sc->cmnd[5], sc->cmnd[6], sc->cmnd[7], sc->cmnd[8], sc->cmnd[9],
	     sc->cmnd[10], sc->cmnd[11]);
    else if (sc->cmd_len >= 10)
	printk("iSCSI:   cdb %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
	       sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
	       sc->cmnd[4], sc->cmnd[5], sc->cmnd[6], sc->cmnd[7],
	       sc->cmnd[8], sc->cmnd[9]);
    else if (sc->cmd_len >= 8)
	printk("iSCSI:   cdb %02x%02x%02x%02x %02x%02x%02x%02x\n",
	       sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
	       sc->cmnd[4], sc->cmnd[5], sc->cmnd[6], sc->cmnd[7]);
    else if (sc->cmd_len >= 6)
	printk("iSCSI:   cdb %02x%02x%02x%02x %02x%02x\n",
	       sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3],
	       sc->cmnd[4], sc->cmnd[5]);
    else if (sc->cmd_len >= 4)
	printk("iSCSI:   cdb %02x%02x%02x%02x\n",
	       sc->cmnd[0], sc->cmnd[1], sc->cmnd[2], sc->cmnd[3]);
    else if (sc->cmd_len >= 2)
	printk("iSCSI:   cdb %02x%02x\n", sc->cmnd[0], sc->cmnd[1]);

    if (sc->use_sg && sc->request_buffer) {
	struct scatterlist *sglist = (struct scatterlist *) sc->request_buffer;
	int i;

	for (i = 0; i < sc->use_sg; i++) {
#if (HAS_SCATTERLIST_PAGE && HAS_SCATTERLIST_ADDRESS)
	    printk("iSCSI:   sglist %p index %02d = addr %p, page %p, "
		   "offset %u, len %u\n",
		   sglist, i, sglist->address, sglist->page, sglist->offset,
		   sglist->length);
#elif HAS_SCATTERLIST_PAGE
	    printk("iSCSI:   sglist %p index %02d = page %p, offset %u, "
		   "len %u\n",
		   sglist, i, sglist->page, sglist->offset, sglist->length);
#else
	    printk("iSCSI:   sglist %p index %02d = addr %p, len %u\n",
		   sglist, i, sglist->address, sglist->length);
#endif
	    sglist++;
	}
    }
#ifdef HAS_CMND_REQUEST_STRUCT
    /* and log the struct request so we can check consistency */
    printk("iSCSI:   request status 0x%x, sector %lu, nr_sectors %lu, "
	   "hard_sector %lu, hard_nr_sectors %lu\n"
	   "iSCSI:   nr_segments %u, hard_nr_segments %u, current_nr_sectors %lu\n"
	   "iSCSI:   special %p, buffer %p, bh %p, bhtail %p\n", req->rq_status,
	   req->sector, req->nr_sectors, req->hard_sector, req->hard_nr_sectors,
	   req->nr_segments, req->nr_hw_segments, req->current_nr_sectors,
	   req->special, req->buffer, req->bh, req->bhtail);

    for (bh = req->bh; bh; bh = bh->b_reqnext) {
	printk("iSCSI:   bh %p = rsector %lu, blocknr %lu, size %u, list %u, "
	       "state 0x%lx, data %p, page %p\n",
	       bh, bh->b_rsector, bh->b_blocknr, bh->b_size, bh->b_list,
	       bh->b_state, bh->b_data, bh->b_page);
    }
#endif

    /* and log the scsi_request so we can check consistency */
    if (sc->sc_request) {
	printk("iSCSI:   Scsi_Request %p = sr_magic 0x%x, sr_bufflen %u, "
	       "sr_buffer %p, sr_allowed %u, sr_cmd_len %u\n"
	       "iSCSI:                     sr_use_sg %u, sr_sglist_len "
	       "%u, sr_underflow %u\n",
	       sc->sc_request, sc->sc_request->sr_magic,
	       sc->sc_request->sr_bufflen, sc->sc_request->sr_buffer,
	       sc->sc_request->sr_allowed, sc->sc_request->sr_cmd_len,
	       sc->sc_request->sr_use_sg, sc->sc_request->sr_sglist_len,
	       sc->sc_request->sr_underflow);

    }
}

static inline int
add_cmnd(Scsi_Cmnd * sc, Scsi_Cmnd ** head, Scsi_Cmnd ** tail)
{
    sc->host_scribble = NULL;

    if (*head) {
	(*tail)->host_scribble = (void *) sc;
	*tail = sc;
    } else {
	*tail = *head = sc;
    }

    return 1;
}

static void
request_command_retries(unsigned long arg)
{
    iscsi_session_t *session = (iscsi_session_t *) arg;

    DEBUG_RETRY("iSCSI: session %p retry timer expired at %lu\n", session,
		jiffies);
    session->retry_timer.expires = 0;
    smp_mb();
    wake_tx_thread(SESSION_RETRY_COMMANDS, session);
}

/* try to queue one command retry for each LUN that needs one */
static void
iscsi_retry_commands(iscsi_session_t * session)
{
    Scsi_Cmnd *prior = NULL, *sc;
    Scsi_Cmnd *retry_head = NULL, *retry_tail = NULL;
    iscsi_task_t *task;
    int num_retries = 0;
    int l;
    DECLARE_NOQUEUE_FLAGS;

    spin_lock(&session->task_lock);
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

    /* record which LUNs we're going to check for retries */
    memset(session->luns_checked, 0, sizeof (session->luns_checked));
    for (l = 0; l < ISCSI_MAX_LUN; l++) {
	if (test_bit(l, session->luns_delaying_commands)
	    && !test_bit(l, session->luns_timing_out))
	    __set_bit(l, session->luns_checked);
    }

    /* skip LUNs that already have outstanding tasks */
    for (task = session->arrival_order.head; task; task = task->next) {
	__clear_bit(task->lun, session->luns_checked);
    }

    /* skip LUNs that already have a retry queued */
    for (sc = session->retry_cmnd_head; sc;
	 sc = (Scsi_Cmnd *) sc->host_scribble) {
	__clear_bit(sc->lun, session->luns_checked);
    }

    /* find the oldest deferred command to each of the LUNs
     * we want to queue a retry to 
     */
    while ((sc = session->deferred_cmnd_head)) {
	if (test_bit(sc->lun, session->luns_checked)) {
	    /* pop this command off the head of the deferred queue */
	    session->deferred_cmnd_head = (Scsi_Cmnd *) sc->host_scribble;
	    if (session->deferred_cmnd_head == NULL)
		session->deferred_cmnd_tail = NULL;
	    session->num_deferred_cmnds--;

	    /* queue it for retry */
	    if (retry_head) {
		retry_tail->host_scribble = (unsigned char *) sc;
		retry_tail = sc;
	    } else {
		retry_head = retry_tail = sc;
	    }
	    sc->host_scribble = NULL;
	    num_retries++;
	    if (LOG_ENABLED(ISCSI_LOG_RETRY))
		printk("iSCSI: session %p queuing command %p cdb "
		       "0x%02x to (%u %u %u %u) for retry at %lu\n",
		       session, sc, sc->cmnd[0], session->host_no,
		       session->channel, session->target_id, sc->lun, jiffies);

	    /* and don't take any more commands for this LUN */
	    __clear_bit(sc->lun, session->luns_checked);
	} else {
	    prior = sc;
	    break;
	}
    }
    while (prior && (sc = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(sc->lun, session->luns_checked)) {
	    /* remove this command from the deferred queue */
	    prior->host_scribble = sc->host_scribble;
	    if (session->deferred_cmnd_tail == sc)
		session->deferred_cmnd_tail = prior;
	    session->num_deferred_cmnds--;

	    /* queue it for retry */
	    if (retry_head) {
		retry_tail->host_scribble = (unsigned char *) sc;
		retry_tail = sc;
	    } else {
		retry_head = retry_tail = sc;
	    }
	    sc->host_scribble = NULL;
	    num_retries++;
	    if (LOG_ENABLED(ISCSI_LOG_RETRY))
		printk("iSCSI: session %p queuing command %p cdb 0x%02x "
		       "to (%u %u %u %u) for retry at %lu\n",
		       session, sc, sc->cmnd[0], session->host_no,
		       session->channel, session->target_id, sc->lun, jiffies);

	    /* and don't take any more commands for this LUN */
	    __clear_bit(sc->lun, session->luns_checked);
	} else {
	    prior = sc;
	}
    }

    if (num_retries) {
	/* append to the retry_cmnd queue */
	if (session->retry_cmnd_head)
	    session->retry_cmnd_tail->host_scribble = (void *) retry_head;
	else
	    session->retry_cmnd_head = retry_head;

	session->retry_cmnd_tail = retry_tail;
	atomic_add(num_retries, &session->num_retry_cmnds);
	set_bit(TX_WAKE, &session->control_bits);
	set_bit(TX_SCSI_COMMAND, &session->control_bits);
    }

    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    if (session->num_luns_delaying_commands
	&& (session->retry_timer.expires == 0)) {
	/* as long as at least one LUN is delaying commands,
	 * we need to reset the timer 
	 */
	session->retry_timer.function = request_command_retries;
	session->retry_timer.data = (unsigned long) session;
	session->retry_timer.expires = jiffies + HZ;
	add_timer(&session->retry_timer);
    }

    spin_unlock(&session->task_lock);
}

static void
requeue_deferred_commands(iscsi_session_t * session, unsigned int lun)
{
    Scsi_Cmnd *cmnd, *prior, *requeue_head = NULL, *requeue_tail = NULL;
    int num_requeued = 0;

    DEBUG_RETRY("iSCSI: session %p requeuing deferred commands for "
		"(%u %u %u %u) at %lu\n",
		session, session->host_no, session->channel, session->target_id,
		lun, jiffies);

    prior = NULL;
    while ((cmnd = session->deferred_cmnd_head)) {
	if (cmnd->lun == lun) {
	    /* remove it from the deferred queue */
	    session->deferred_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->deferred_cmnd_head == NULL)
		session->deferred_cmnd_tail = NULL;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    DEBUG_RETRY("iSCSI: session %p requeueing deferred command %p "
			"cdb 0x%02x to (%u %u %u %u) at %lu\n",
			session, cmnd, cmnd->cmnd[0], session->host_no,
			session->channel, session->target_id, cmnd->lun,
			jiffies);
	    add_cmnd(cmnd, &requeue_head, &requeue_tail);
	    num_requeued++;
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (cmnd->lun == lun) {
	    /* remove it from the deferred queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->deferred_cmnd_tail == cmnd)
		session->deferred_cmnd_tail = prior;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    DEBUG_RETRY("iSCSI: session %p requeueing deferred command "
			"%p cdb 0x%02x to (%u %u %u %u) at %lu\n",
			session, cmnd, cmnd->cmnd[0], session->host_no,
			session->channel, session->target_id, cmnd->lun,
			jiffies);
	    add_cmnd(cmnd, &requeue_head, &requeue_tail);
	    num_requeued++;
	} else {
	    prior = cmnd;
	}
    }

    if (requeue_head) {
	requeue_tail->host_scribble = (void *) session->scsi_cmnd_head;
	session->scsi_cmnd_head = requeue_head;
	if (session->scsi_cmnd_tail == NULL)
	    session->scsi_cmnd_tail = requeue_tail;
	atomic_add(num_requeued, &session->num_cmnds);
	wake_tx_thread(TX_SCSI_COMMAND, session);
	DEBUG_RETRY("iSCSI: session %p requeued %d deferred commands "
		    "and woke tx thread at %lu\n",
		    session, num_requeued, jiffies);
    }
}

/* caller must hold the task lock */
static void
process_task_response(iscsi_session_t * session, iscsi_task_t * task,
		      struct IscsiScsiRspHdr *stsrh, unsigned char *sense_data,
		      int senselen)
{
    Scsi_Cmnd *sc = task->scsi_cmnd;
    int needs_retry = 0;
    int slow_retry = 0;
    unsigned int expected = 0;

    DEBUG_FLOW("iSCSI: session %p recv_cmd - itt %u, task %p, cmnd %p, "
	       "cdb 0x%x, cmd_len %d, rsp dlength %d, senselen %d\n",
	       session, task->itt, task, sc, sc->cmnd[0], sc->cmd_len,
	       ntoh24(stsrh->dlength), senselen);

    /* default to just passing along the SCSI status.  We
     * may change this later 
     */
    sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(stsrh->cmd_status);

    /* grab any sense data that came with the command.  It could be
     * argued that we should only do this if the SCSI status is check
     * condition.  It could also be argued that the target should only
     * send sense if the SCSI status is check condition.  If the
     * target bothered to send sense, we pass it along, since it
     * may indicate a problem, and it's safer to report a possible
     * problem than it is to assume everything is fine.
     */
    if (senselen) {
	/* fill in the Scsi_Cmnd's sense data */
	memset(sc->sense_buffer, 0, sizeof (sc->sense_buffer));
	memcpy(sc->sense_buffer, sense_data,
	       MIN(senselen, sizeof (sc->sense_buffer)));

	/* if sense data logging is enabled, or it's deferred
	 * sense that we're going to do something special with, 
	 * or if it's an unexpected unit attention, which Linux doesn't
	 * handle well, log the sense data.
	 */
	if ((LOG_ENABLED(ISCSI_LOG_SENSE)) ||
	    (((sense_data[0] == 0x71) || (sense_data[0] == 0xF1))
	     && translate_deferred_sense)
	    || ((SENSE_KEY(sense_data) == UNIT_ATTENTION)
		&& (test_bit(SESSION_RESETTING, &session->control_bits) == 0))) {
	    if (senselen >= 26) {
		printk("iSCSI: session %p recv_cmd %p, cdb 0x%x, status "
		       "0x%x, response 0x%x, senselen %d, "
		       "key %02x, ASC/ASCQ %02X/%02X, itt %u task %p to "
		       "(%u %u %u %u), %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n", session,
		       sc, sc->cmnd[0], stsrh->cmd_status, stsrh->response,
		       senselen, SENSE_KEY(sense_data), ASC(sense_data),
		       ASCQ(sense_data), task->itt, task, sc->host->host_no,
		       sc->channel, sc->target, sc->lun, session->log_name,
		       sense_data[0], sense_data[1], sense_data[2],
		       sense_data[3], sense_data[4], sense_data[5],
		       sense_data[6], sense_data[7], sense_data[8],
		       sense_data[9], sense_data[10], sense_data[11],
		       sense_data[12], sense_data[13], sense_data[14],
		       sense_data[15], sense_data[16], sense_data[17],
		       sense_data[18], sense_data[19], sense_data[20],
		       sense_data[21], sense_data[22], sense_data[23],
		       sense_data[24], sense_data[25]);
	    } else if (senselen >= 18) {
		printk("iSCSI: session %p recv_cmd %p, cdb 0x%x, status "
		       "0x%x, response 0x%x, senselen %d, "
		       "key %02x, ASC/ASCQ %02X/%02X, itt %u task %p to "
		       "(%u %u %u %u), %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
		       session, sc, sc->cmnd[0], stsrh->cmd_status,
		       stsrh->response, senselen, SENSE_KEY(sense_data),
		       ASC(sense_data), ASCQ(sense_data), task->itt, task,
		       sc->host->host_no, sc->channel, sc->target, sc->lun,
		       session->log_name, sense_data[0], sense_data[1],
		       sense_data[2], sense_data[3], sense_data[4],
		       sense_data[5], sense_data[6], sense_data[7],
		       sense_data[8], sense_data[9], sense_data[10],
		       sense_data[11], sense_data[12], sense_data[13],
		       sense_data[14], sense_data[15], sense_data[16],
		       sense_data[17]);
	    } else {
		printk("iSCSI: session %p recv_cmd %p, cdb 0x%x, status 0x%x, "
		       "response 0x%x, senselen %d, key %02x, "
		       "itt %u task %p to (%u %u %u %u), %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x\n",
		       session, sc, sc->cmnd[0], stsrh->cmd_status,
		       stsrh->response, senselen, SENSE_KEY(sense_data),
		       task->itt, task, sc->host->host_no, sc->channel,
		       sc->target, sc->lun, session->log_name, sense_data[0],
		       sense_data[1], sense_data[2], sense_data[3],
		       sense_data[4], sense_data[5], sense_data[6],
		       sense_data[7]);
	    }
	}
    } else if ((stsrh->cmd_status == STATUS_CHECK_CONDITION) && (senselen == 0)) {
	/* check condition with no sense.  We need to avoid
	 * this, since the Linux SCSI code could put the
	 * command in SCSI_STATE_FAILED, which it's error
	 * recovery doesn't appear to handle correctly, and
	 * even if it does, we're trying to bypass all of
	 * the Linux error recovery code to avoid blocking
	 * all I/O to the HBA.  Fake some sense data that
	 * gets a retry from Linux.
	 */
	printk("iSCSI: session %p recv_cmd %p generating sense for "
	       "itt %u, task %p, status 0x%x, response 0x%x, senselen "
	       "%d, cdb 0x%x to (%u %u %u %u) at %lu\n",
	       session, sc, task->itt, task, stsrh->cmd_status, stsrh->response,
	       senselen, sc->cmnd[0], sc->host->host_no, sc->channel,
	       sc->target, sc->lun, jiffies);

	/* report a complete underflow */
	stsrh->residual_count = htonl(iscsi_expected_data_length(sc));
	stsrh->flags |= ISCSI_FLAG_CMD_UNDERFLOW;

	memset(sc->sense_buffer, 0, sizeof (sc->sense_buffer));
	sc->sense_buffer[0] = 0x70;
	sc->sense_buffer[2] = ABORTED_COMMAND;	/* so that scsi_check_sense 
						 * always returns NEEDS_RETRY 
						 * to scsi_decide_dispostion 
						 */
	sc->sense_buffer[7] = 0x6;
	sc->sense_buffer[12] = 0x04;	/* ASC/ASCQ 04/01 appears to always 
					 * get a retry from scsi_io_completion, 
					 * so we use that 
					 */
	sc->sense_buffer[13] = 0x01;
    }
#if FAKE_NO_REPORT_LUNS
    else if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc
	     && (senselen == 0) && (sc->cmnd[0] == REPORT_LUNS)
	     && (stsrh->cmd_status == 0) && (stsrh->response == 0)) {
	printk("iSCSI: session %p faking failed REPORT_LUNS itt "
	       "%u, CmdSN %u, task %p, sc %p, cdb 0x%x to (%u %u %u %u)\n",
	       session, itt, task->cmdsn, task, sc, sc->cmnd[0],
	       sc->host->host_no, sc->channel, sc->target, sc->lun);

	/* fake an illegal request check condition for this command.
	 */
	sc->sense_buffer[0] = 0x70;
	sc->sense_buffer[2] = ILLEGAL_REQUEST;
	sc->sense_buffer[7] = 0x6;
	sc->sense_buffer[12] = 0x20;	/* INVALID COMMAND OPERATION CODE */
	sc->sense_buffer[13] = 0x00;
	sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(0x02);
	stsrh->cmd_status = 0x2;
	stsrh->residual_count = htonl(iscsi_expected_data_length(sc));
	stsrh->flags |= ISCSI_FLAG_CMD_UNDERFLOW;
    }
#endif
#if FAKE_PROBE_CHECK_CONDITIONS
    else if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc
	     && (senselen == 0) && (sc->scsi_done == iscsi_done)
	     && (sc->retries <= 1) && (stsrh->cmd_status == 0)
	     && (stsrh->response == 0)) {
	printk("iSCSI: session %p faking failed probe itt %u, CmdSN %u, "
	       "task %p, sc %p, cdb 0x%x to (%u %u %u %u)\n",
	       session, itt, task->cmdsn, task, sc, sc->cmnd[0],
	       sc->host->host_no, sc->channel, sc->target, sc->lun);

	/* fake an command aborted check condition to test
	 * the recovery of probe commands 
	 */
	sc->sense_buffer[0] = 0x70;
	sc->sense_buffer[2] = NOT_READY;
	sc->sense_buffer[7] = 0x6;
	sc->sense_buffer[12] = 0x08;
	sc->sense_buffer[13] = 0x00;
	stsrh->cmd_status = 0x2;
	stsrh->residual_count = htonl(iscsi_expected_data_length(sc));
	stsrh->flags |= ISCSI_FLAG_CMD_UNDERFLOW;
	sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(0x02);
    }
#endif
#if FAKE_PROBE_UNDERFLOW
    else if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc
	     && (senselen == 0) && (sc->scsi_done == iscsi_done)
	     && (sc->retries <= 3) && (stsrh->cmd_status == 0)
	     && (stsrh->response == 0)) {
	printk("iSCSI: session %p faking probe underflow for itt %u, "
	       "CmdSN %u, task %p, sc %p, cdb 0x%x to (%u %u %u %u)\n",
	       session, itt, task->cmdsn, task, sc, sc->cmnd[0],
	       sc->host->host_no, sc->channel, sc->target, sc->lun);

	stsrh->residual_count = htonl(iscsi_expected_data_length(sc));
	stsrh->flags |= ISCSI_FLAG_CMD_UNDERFLOW;
	sc->resid = iscsi_expected_data_length(sc);
	sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(0x0);
    }
#endif

    /* record the (possibly fake) status in the trace */
    ISCSI_TRACE(ISCSI_TRACE_RxCmdStatus, sc, task, stsrh->cmd_status,
		stsrh->response);

    if (senselen && ((sense_data[0] == 0x71) || (sense_data[0] == 0xF1)) &&
	sc->device && (sc->device->type == TYPE_DISK)
	&& translate_deferred_sense) {
	printk("iSCSI: session %p recv_cmd %p translating deferred sense "
	       "to current sense for itt %u\n", session, sc, task->itt);
	sc->sense_buffer[0] &= 0xFE;
    }

    /* check for underflow and overflow */
    expected = iscsi_expected_data_length(sc);
    if ((stsrh->flags & ISCSI_FLAG_CMD_OVERFLOW)
	|| (stsrh->flags & ISCSI_FLAG_CMD_UNDERFLOW)
	|| ((test_bit(TASK_READ, &task->flags)) && (task->rxdata < expected))) {
	if (LOG_ENABLED(ISCSI_LOG_QUEUE) || LOG_ENABLED(ISCSI_LOG_FLOW)
	    || (senselen && (SENSE_KEY(sense_data) == UNIT_ATTENTION))) {
	    /* for debugging, always log this for UNIT ATTENTION */
	    /* FIXME: bidi flags as well someday */
	    printk("iSCSI: session %p recv_cmd %p, itt %u, task %p to "
		   "(%u %u %u %u), cdb 0x%x, %c%c %s, received %u, "
		   "residual %u, expected %u\n",
		   session, sc, task->itt, task, sc->host->host_no, sc->channel,
		   sc->target, sc->lun, sc->cmnd[0],
		   (stsrh->flags & ISCSI_FLAG_CMD_OVERFLOW) ? 'O' : ' ',
		   (stsrh->flags & ISCSI_FLAG_CMD_UNDERFLOW) ? 'U' : ' ',
		   (stsrh->flags & ISCSI_FLAG_CMD_OVERFLOW) ?
		   "overflow" : "underflow",
		   task->rxdata, ntohl(stsrh->residual_count), expected);
	}
#ifdef DEBUG
	/* FIXME: fake a bad driver or SCSI status if there is a
	 * residual for certain commands?  The Linux high-level
	 * drivers appear to ignore the resid field.  This may cause
	 * data corruption if a device returns a residual for a read
	 * or write command, but a good SCSI status and iSCSI
	 * response.  The problem is that for some commands an
	 * underflow is normal, such as INQUIRY.  We have to check the cdb
	 * to determine if an underflow should be translated to an error.
	 * For now, just log about it, so we can see if the problem 
	 * is ever occuring.
	 */
	switch (sc->cmnd[0]) {
	case READ_6:
	case READ_10:
	case READ_12:
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	    if (sc->device && (sc->device->type == TYPE_DISK)
		&& stsrh->residual_count && (stsrh->response == 0)
		&& (stsrh->cmd_status == 0)) {
		/* log if we get an underflow with a good status
		 * and response for data transfer commands, since
		 * Linux appears to ignore the residual field of
		 * Scsi_Cmnds, and only consider the data invalid
		 * if the driver of SCSI status was bad.  
		 */
		printk("iSCSI: session %p task %p itt %u to (%u %u %u %u), "
		       "cdb 0x%x, received %u, residual %u, expected %u, but "
		       "normal status\n",
		       session, task, task->itt, sc->host->host_no, sc->channel,
		       sc->target, sc->lun, sc->cmnd[0], task->rxdata,
		       ntohl(stsrh->residual_count), expected);
	    }
	    break;
	}
#endif

	if (stsrh->flags & ISCSI_FLAG_CMD_UNDERFLOW) {
	    ISCSI_TRACE(ISCSI_TRACE_RxUnderflow, sc, task,
			ntohl(stsrh->residual_count), expected);
	    sc->resid = ntohl(stsrh->residual_count);
	} else if (stsrh->flags & ISCSI_FLAG_CMD_OVERFLOW) {
	    ISCSI_TRACE(ISCSI_TRACE_RxOverflow, sc, task,
			ntohl(stsrh->residual_count), expected);
	    sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(stsrh->cmd_status);
	    sc->resid = expected;
	} else if (task->rxdata < expected) {
	    /* All the read data did not arrive.  This can
	     * happen without an underflow indication from
	     * the target if the data is discarded by the
	     * driver, due to failed sanity checks on the
	     * PDU or digest errors.
	     */
	    ISCSI_TRACE(ISCSI_TRACE_HostUnderflow, sc, task, task->rxdata,
			expected);
	    sc->resid = expected - task->rxdata;
	}
    }

    if (stsrh->response) {
	needs_retry = 1;
	slow_retry = 1;

	/* log when we transition from no transport errors to transport errors */
	if (__test_and_set_bit(sc->lun, session->luns_unreachable) == 0) {
	    printk("iSCSI: session %p recv_cmd %p, status 0x%x, iSCSI "
		   "transport response 0x%x, itt %u, task %p to (%u %u %u %u) "
		   "at %lu\n",
		   session, sc, stsrh->cmd_status, stsrh->response, task->itt,
		   task, sc->host->host_no, sc->channel, sc->target, sc->lun,
		   jiffies);
	}
    } else {
	/* log when we transition from transport errors to no transport errors */
	if (__test_and_clear_bit(sc->lun, session->luns_unreachable)) {
	    printk("iSCSI: session %p recv_cmd %p, status 0x%x, iSCSI "
		   "transport response 0x%x, itt %u, task %p to (%u %u %u %u) "
		   "at %lu\n",
		   session, sc, stsrh->cmd_status, stsrh->response, task->itt,
		   task, sc->host->host_no, sc->channel, sc->target, sc->lun,
		   jiffies);
	}

	/* now we basically duplicate what
	 * scsi_decide_disposition and scsi_check_sense
	 * would have done if we completed the command, but
	 * we do it ourselves so that we can requeue
	 * internally.
	 */
	if ((stsrh->cmd_status == STATUS_BUSY)
	    || (stsrh->cmd_status == STATUS_QUEUE_FULL)) {
	    /* slow retries, at least until a command completes */
	    needs_retry = 1;
	    slow_retry = 1;
	} else if (stsrh->cmd_status == STATUS_CHECK_CONDITION) {
	    /* check conditions can only be retried if the
	     * command allows retries.  Tapes for example,
	     * can't retry, since the tape head may have
	     * moved.
	     */
	    /* FIXME: possible interactions with ACA. Do we
	     * need to complete the command back to the SCSI
	     * layer when ACA is enabled?
	     */
	    if (sc->allowed > 1) {
		if (senselen == 0) {
		    /* for check conditions with no sense,
		     * fast retry when possible 
		     */
		    needs_retry = 1;
		} else if ((sc->sense_buffer[0] & 0x70) == 0) {
		    /* check conditions with invalid sense */
		    needs_retry = 1;
		} else if (sc->sense_buffer[2] & 0xe0) {
		    /* can't retry internally */
		    /* FIXME: why not? what are these bits? */
		} else if ((SENSE_KEY(sc->sense_buffer) == ABORTED_COMMAND)) {
		    needs_retry = 1;
		} else if ((SENSE_KEY(sc->sense_buffer) == MEDIUM_ERROR)) {
		    needs_retry = 1;
		} else if ((SENSE_KEY(sc->sense_buffer) == NOT_READY)) {
		    if ((ASC(sc->sense_buffer) == 0x04)
			&& (ASCQ(sc->sense_buffer) == 0x01)) {
			/* LUN in the process of becoming ready */
			needs_retry = 1;
			slow_retry = 1;
		    }
		}

		/* switch to slow retries if the fast
		 * retries don't seem to be working 
		 */
		if (needs_retry && (sc->SCp.sent_command > 10))
		    slow_retry = 1;
	    }
	}
    }

    if (needs_retry && internally_retryable(sc)) {
	/* need to requeue this command for a retry later.
	 * Philsophically we ought to complete the command and let the
	 * midlayer or high-level driver deal with retries.  Since the
	 * way the midlayer does retries is undesirable, we instead
	 * keep the command in the driver, but requeue it for the same
	 * cases the midlayer checks for retries.  This lets us ignore
	 * the command's retry count, and do retries until the command
	 * timer expires.  
	 */
	sc->result = 0;
	sc->resid = 0;
	sc->SCp.sent_command++;	/* count how many internal retries we've done */
	memset(sc->sense_buffer, 0, sizeof (sc->sense_buffer));
	__set_bit(TASK_NEEDS_RETRY, &task->flags);

	if (slow_retry) {
	    /* delay commands for slower retries */
	    if (__test_and_set_bit(task->lun, session->luns_delaying_commands)
		== 0) {
		/* FIXME: we don't want to log this if a QUEUE_FULL
		 * puts us in slow retries for a fraction of a second.
		 * Where can we record a per-Scsi_Device timestamp to
		 * use when deciding whether or not to log?  In 2.5
		 * we can put a pointer in Scsi_Device->hostdata, but
		 * 2.4 doesn't appear to give us any good hooks for
		 * deallocating that memory.  There's no slave_destroy
		 * or slave_detach.
		 */
		DEBUG_RETRY("iSCSI: session %p starting to delay commands "
			    "to (%u %u %u %u) at %lu\n",
			    session, session->host_no, session->channel,
			    session->target_id, sc->lun, jiffies);
		if (session->num_luns_delaying_commands == 0) {
		    session->retry_timer.data = (unsigned long) session;
		    session->retry_timer.expires = jiffies + HZ;
		    session->retry_timer.function = request_command_retries;
		    add_timer(&session->retry_timer);
		    DEBUG_RETRY("iSCSI: session %p starting retry timer "
				"at %lu\n", session, jiffies);
		}
		session->num_luns_delaying_commands++;
	    }
	}
#if RETRIES_BLOCK_DEVICES
	/* try to stop the mid-layer from queueing any more commands to this LUN
	 * until a command completes, by setting sc->device->device_blocked.
	 */
	/* FIXME: locking? */
	if (sc->device) {
# ifdef SCSI_DEFAULT_DEVICE_BLOCKED
	    sc->device->device_blocked = sc->device->max_device_blocked;
# else
	    sc->device->device_blocked = TRUE;
# endif
	    smp_mb();
	}
#endif

	/* FIXME: warn if the command's tag is ORDERED or
	 * HEAD_OF_QUEUE, since we're reordering commands by
	 * requeuing to the tail of the scsi_cmnd queue,
	 * rather than retrying this task and all younger
	 * tasks to this LUN.  We emulate what the SCSI
	 * midlayer would do, even though what it does is
	 * probably broken if the command is ORDERED or
	 * HEAD_OF_QUEUE.  We probably need something like
	 * ACA to make this work right, and it doesn't look
	 * like the midlayer uses ACA, but rather it just
	 * assumes everything is untagged or simple, so
	 * command reordering doesn't matter.  If the
	 * midlayer ever changes, we'll need to make similar
	 * changes, or go back to actually completing the
	 * command back to the midlayer and letting it
	 * figure out how to retry.
	 */
	if (sc->tag == ORDERED_QUEUE_TAG)
	    printk("iSCSI: session %p retrying ORDERED command %p, "
		   "possible reordering hazard at %lu\n", session, sc, jiffies);
	else if (sc->tag == HEAD_OF_QUEUE_TAG)
	    printk("iSCSI: session %p retrying HEAD_OF_QUEUE command %p, "
		   "possible reordering hazard at %lu\n", session, sc, jiffies);

	smp_mb();
    } else {
	/* if we're not retrying this command, we go back to full
	 * speed unless command timeouts have triggered or will
	 * trigger error recovery.  
	 */
	if (test_bit(task->lun, session->luns_delaying_commands)) {
	    __clear_bit(task->lun, session->luns_delaying_commands);
	    session->num_luns_delaying_commands--;
	    DEBUG_RETRY("iSCSI: session %p no longer delaying commands "
			"to (%u %u %u %u) at %lu\n",
			session, session->host_no, session->channel,
			session->target_id, sc->lun, jiffies);
	    if (session->num_luns_delaying_commands == 0) {
		del_timer_sync(&session->retry_timer);
		clear_bit(SESSION_RETRY_COMMANDS, &session->control_bits);
		DEBUG_RETRY("iSCSI: session %p stopping retry timer at %lu\n",
			    session, jiffies);
	    }
	    if (!test_bit(task->lun, session->luns_timing_out)) {
		DECLARE_NOQUEUE_FLAGS;

		SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		requeue_deferred_commands(session, task->lun);
		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	    }
	    smp_mb();
	}
    }

    ISCSI_TRACE(ISCSI_TRACE_RxCmd, sc, task, task->rxdata, expected);
}

/* 
 * complete a task in the session's completing queue, and return a pointer to it,
 * or NULL if the task could not be completed.  Caller must hold the task_lock,
 * but the lock is always released before returning.
 */
static void
complete_task(iscsi_session_t * session, uint32_t itt)
{
    iscsi_task_t *task;
    unsigned long last_log = 0;
    int refcount;
    DECLARE_MIDLAYER_FLAGS;

    while (!signal_pending(current)) {
	DEBUG_QUEUE("iSCSI: session %p attempting to complete itt %u\n",
		    session, itt);

	if ((task = find_session_task(session, itt))) {
	    Scsi_Cmnd *sc = task->scsi_cmnd;

	    if (test_bit(SESSION_RESETTING, &session->control_bits)) {
		/* we don't trust the target to give us
		 * correct responses once we've issued a
		 * reset.  Ensure that none of the
		 * outstanding tasks complete.
		 */
		spin_unlock(&session->task_lock);
		DEBUG_EH("iSCSI: session %p can't complete itt %u, task %p, "
			 "cmnd %p, reset in progress at %lu\n",
			 session, itt, task, sc, jiffies);
		return;
	    } else if (test_bit(task->lun, session->luns_doing_recovery)) {
		/* don't complete any tasks once a LUN has
		 * started doing error recovery.  Leave the
		 * recovery state as it is, since we may
		 * have an outstanding task mgmt PDU for
		 * this task.
		 */
		spin_unlock(&session->task_lock);
		DEBUG_EH("iSCSI: session %p can't complete itt %u, task "
			 "%p, cmnd %p, LUN %u doing error recovery at %lu\n",
			 session, itt, task, sc, task->lun, jiffies);
		return;
	    }

	    /* no need to do error recovery for this task */
	    task->flags &= ~TASK_RECOVERY_MASK;

	    /* it's possible the tx thread is using the task right now.
	     * the task's refcount can't increase while it's in the completing
	     * collection, so wait for the refcount to hit zero, or the task
	     * to leave the completing collection, whichever happens first.
	     */
	    if ((refcount = atomic_read(&task->refcount)) == 0) {
		/* this is the expected case */
#if INCLUDE_DEBUG_EH
		if (LOG_ENABLED(ISCSI_LOG_EH) && sc
		    && (sc->cmnd[0] == TEST_UNIT_READY)) {
		    printk("iSCSI: completing TUR at %lu, itt %u, task %p, "
			   "command %p, (%u %u %u %u), cdb 0x%x, result 0x%x\n",
			   jiffies, itt, task, sc, sc->host->host_no,
			   sc->channel, sc->target, sc->lun, sc->cmnd[0],
			   sc->result);
		} else
#endif
		{
#if INCLUDE_DEBUG_QUEUE
		    if (LOG_ENABLED(ISCSI_LOG_QUEUE)) {
			if (sc)
			    printk("iSCSI: completing itt %u, task %p, "
				   "command %p, (%u %u %u %u), cdb 0x%x, "
				   "done %p, result 0x%x\n",
				   itt, task, sc, sc->host->host_no,
				   sc->channel, sc->target, sc->lun,
				   sc->cmnd[0], sc->scsi_done, sc->result);
			else
			    printk("iSCSI: completing itt %u, task %p, command "
				   "NULL, (%u %u %u %u)\n",
				   itt, task, session->host_no,
				   session->channel, session->target_id,
				   task->lun);
		    }
#endif
		}

		/* remove the task from the session, to ensure a
		 * session drop won't try to complete the task again.
		 */
		if (remove_session_task(session, task)) {
		    DEBUG_QUEUE("iSCSI: removed itt %u, task %p from "
				"session %p to %s\n",
				task->itt, task, session, session->log_name);
		}

		if (test_bit(task->lun, session->luns_timing_out)) {
		    /* this task may be the last thing delaying error recovery.
		     * make sure the tx thread scans tasks again.
		     */
		    DEBUG_EH("iSCSI: session %p completing itt %u, task "
			     "%p while LUN %u is timing out at %lu\n",
			     session, itt, task, task->lun, jiffies);
		    set_bit(SESSION_TASK_TIMEDOUT, &session->control_bits);
		    smp_mb();
		}

		/* this task no longer has a Scsi_Cmnd associated with it */
		task->scsi_cmnd = NULL;
		if (sc)
		    sc->host_scribble = NULL;

		if (sc == NULL) {
		    /* already completed, nothing to do */
		    printk("iSCSI: session %p already completed itt %u, task "
			   "%p, (%u %u %u %u)\n",
			   session, itt, task, session->host_no,
			   session->channel, session->target_id, task->lun);

		    free_task(session, task);
		} else if (test_bit(TASK_NEEDS_RETRY, &task->flags)) {
		    DECLARE_NOQUEUE_FLAGS;

		    /* done with this task */
		    free_task(session, task);

		    /* just requeue the task back to the
		     * scsi_cmnd queue so that it gets
		     * retried 
		     */
		    DEBUG_RETRY("iSCSI: session %p requeueing itt %u task "
				"%p command %p cdb 0x%x for retry to "
				"(%u %u %u %u)\n",
				session, itt, task, sc, sc->cmnd[0],
				sc->host->host_no, sc->channel, sc->target,
				sc->lun);

		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    add_cmnd(sc, &session->scsi_cmnd_head,
			     &session->scsi_cmnd_tail);
		    atomic_inc(&session->num_cmnds);

#if RETRIES_BLOCK_DEVICES
		    /* try to prevent the midlayer from
		     * issuing more commands to this device
		     * until we complete a command for this
		     * device back to the midlayer.  This
		     * hopefully keeps the midlayer queueing
		     * commands to other LUNs, rather than
		     * filling up the driver's limit of 64
		     * with commands that we can't complete,
		     * which would effectively block other
		     * LUNs that are still working from
		     * getting any commands.
		     */
		    /* FIXME: locking? */
		    if (sc->device) {
# ifdef SCSI_DEFAULT_DEVICE_BLOCKED
			sc->device->device_blocked =
			    sc->device->max_device_blocked;
# else
			sc->device->device_blocked = TRUE;
# endif
			smp_mb();
		    }
#endif
		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    wake_tx_thread(TX_SCSI_COMMAND, session);
		} else {
		    /* delete our command timer */
		    del_command_timer(sc);

		    /* we're completing it out of the driver */
		    ISCSI_TRACE(ISCSI_TRACE_CmdDone, sc, task, sc->result, 0);

		    /* done with this task */
		    free_task(session, task);

		    /* FIXME: if we want to get lots of
		     * retries for cases we don't retry
		     * internally, we'll need to
		     * conditionally alter sc->retries
		     * before completing the command.
		     */

		    if (sc->scsi_done == NULL) {
			printk("iSCSI: no completion callback for command %p\n",
			       sc);
		    } else if (sc->scsi_done == iscsi_done) {
			/* it came from iscsi-probe.c, and
			 * doesn't need a timer added or
			 * lock held 
			 */
			sc->scsi_done(sc);
		    } else {
			/* add a useless timer for the midlayer to delete */
			add_completion_timer(sc);

			/* tell the SCSI midlayer that the command is done */
			LOCK_MIDLAYER_LOCK(session->hba->host);
			sc->scsi_done(sc);
			UNLOCK_MIDLAYER_LOCK(session->hba->host);
		    }

		    DEBUG_QUEUE("iSCSI: session %p completed itt %u, task %p, "
				"command %p, (%u %u %u %u), cdb 0x%x, result "
				"0x%x\n",
				session, itt, task, sc, sc->host->host_no,
				sc->channel, sc->target, sc->lun, sc->cmnd[0],
				sc->result);
		}

		spin_unlock(&session->task_lock);

		return;
	    } else {
		/* task is still in use, can't complete it yet.  Since
		 * this only happens when a command is aborted by the
		 * target unexpectedly, this error case can be slow.
		 * Just keep polling for the refcount to hit zero.  If
		 * the tx thread is blocked while using a task, the
		 * timer thread will eventually send a signal to both
		 * the rx thread and tx thread, so this loop will
		 * terminate one way or another.  
		 */
		if ((last_log == 0) || time_before_eq(last_log + HZ, jiffies)) {
		    DEBUG_QUEUE("iSCSI: waiting to complete itt %u, task %p, "
				"cmnd %p, refcount %d\n",
				itt, task, sc, refcount);
		}

		spin_unlock(&session->task_lock);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(MSECS_TO_JIFFIES(10));

		spin_lock(&session->task_lock);
	    }
	} else {
	    /* not a valid task */
	    DEBUG_QUEUE("iSCSI: can't complete itt %u, task not found\n", itt);
	    spin_unlock(&session->task_lock);
	    return;
	}
    }

    printk("iSCSI: session %p complete_task %u failed at %lu\n", session, itt,
	   jiffies);
    spin_unlock(&session->task_lock);
}

static int
iscsi_xmit_task_mgmt(iscsi_session_t * session, uint8_t func_type,
		     iscsi_task_t * task, uint32_t mgmt_itt)
{
    struct msghdr msg;
    struct iovec iov[2];
    int rc, wlen;
    struct IscsiScsiTaskMgtHdr ststmh;
    uint32_t crc32c;

    memset(&ststmh, 0, sizeof (ststmh));
    ststmh.opcode = ISCSI_OP_SCSI_TASK_MGT_MSG | ISCSI_OP_IMMEDIATE;
    ststmh.flags =
	ISCSI_FLAG_FINAL | (func_type & ISCSI_FLAG_TASK_MGMT_FUNCTION_MASK);
    ststmh.rtt = RSVD_TASK_TAG;
    ststmh.itt = htonl(mgmt_itt);
    ststmh.cmdsn = htonl(session->CmdSn);	/* CmdSN not incremented
						 * after imm cmd 
						 */
    ststmh.expstatsn = htonl(session->ExpStatSn);

    switch (func_type) {
    case ISCSI_TM_FUNC_ABORT_TASK:
	/* need a task for this */
	if (task) {
	    ststmh.refcmdsn = htonl(task->cmdsn);
	    ststmh.rtt = htonl(task->itt);
	    ststmh.lun[1] = task->lun;
	    ISCSI_TRACE(ISCSI_TRACE_TxAbort, task->scsi_cmnd, task,
			task->mgmt_itt, 0);
	} else {
	    printk("iSCSI: session %p failed to send abort, task unknown\n",
		   session);
	    return 0;
	}
	break;
    case ISCSI_TM_FUNC_ABORT_TASK_SET:
	/* need a LUN for this */
	if (task) {
	    ststmh.lun[1] = task->lun;
	    ISCSI_TRACE(ISCSI_TRACE_TxAbortTaskSet, task->scsi_cmnd, task,
			task->mgmt_itt, 0);
	} else {
	    printk("iSCSI: session %p failed to send abort task set, "
		   "LUN unknown\n", session);
	    return 0;
	}
	break;
    case ISCSI_TM_FUNC_LOGICAL_UNIT_RESET:
	/* need a LUN for this */
	if (task) {
	    ststmh.lun[1] = task->lun;
	    ISCSI_TRACE(ISCSI_TRACE_TxLunReset, task->scsi_cmnd, task,
			task->mgmt_itt, 0);
	} else {
	    printk("iSCSI: session %p failed to send logical unit reset, "
		   "no task\n", session);
	    return 0;
	}
	break;
    case ISCSI_TM_FUNC_TARGET_WARM_RESET:
	ISCSI_TRACE(ISCSI_TRACE_TxWarmReset, task ? task->scsi_cmnd : NULL,
		    task, mgmt_itt, 0);
	break;
    case ISCSI_TM_FUNC_TARGET_COLD_RESET:
	ISCSI_TRACE(ISCSI_TRACE_TxColdReset, task ? task->scsi_cmnd : NULL,
		    task, mgmt_itt, 0);
	break;
    default:
	printk("iSCSI: unknown task mgmt function type %u for session %p "
	       "to %s\n", func_type, session, session->log_name);
	return 0;
	break;
    }

    iov[0].iov_base = &ststmh;
    iov[0].iov_len = sizeof (ststmh);
    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    wlen = sizeof (ststmh);

    /* HeaderDigests */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	crc32c = iscsi_crc32c(&ststmh, sizeof (ststmh));
	iov[msg.msg_iovlen].iov_base = &crc32c;
	iov[msg.msg_iovlen].iov_len = sizeof (crc32c);
	msg.msg_iovlen++;
	wlen += sizeof (crc32c);
    }

    rc = iscsi_sendmsg(session, &msg, wlen);
    if (rc != wlen) {
	printk("iSCSI: session %p xmit_task_mgmt failed, rc %d\n", session, rc);
	iscsi_drop_session(session);
	return 0;
    }

    return 1;
}

static void
recheck_busy_commands(unsigned long arg)
{
    iscsi_session_t *session = (iscsi_session_t *) arg;

    session->busy_command_timer.expires = 0;
    smp_mb();
    wake_tx_thread(SESSION_COMMAND_TIMEDOUT, session);
}

static int
process_timedout_commands(iscsi_session_t * session)
{
    iscsi_task_t *task, *next;
    Scsi_Cmnd *fatal_head = NULL, *fatal_tail = NULL, *cmnd = NULL, *prior =
	NULL;
    int busy = 0;
    DECLARE_NOQUEUE_FLAGS;

    spin_lock(&session->task_lock);
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

    DEBUG_TIMEOUT("iSCSI: session %p processing timedout commands at %lu\n",
		  session, jiffies);
    clear_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits);

    /* by default, we can fail commands to any LUN */
    memset(session->luns_checked, 0xFF, sizeof (session->luns_checked));

    DEBUG_TIMEOUT("iSCSI: session %p checking %d tasks for command timeouts "
		  "at %lu\n",
		  session, atomic_read(&session->num_active_tasks), jiffies);
    task = session->arrival_order.head;
    while (task) {
	next = task->order_next;

	if (task->scsi_cmnd
	    && test_bit(COMMAND_TIMEDOUT, command_flags(task->scsi_cmnd))) {
	    if (atomic_read(&task->refcount) == 0) {
		cmnd = task->scsi_cmnd;
		task->scsi_cmnd = NULL;

		if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		    printk("iSCSI: session %p failing itt %u task %p cmnd "
			   "%p cdb 0x%02x to (%u %u %u %u) at %lu, retries "
			   "%d, allowed %d\n",
			   session, task->itt, task, cmnd, cmnd->cmnd[0],
			   cmnd->host->host_no, cmnd->channel, cmnd->target,
			   cmnd->lun, jiffies, cmnd->retries, cmnd->allowed);
		add_cmnd(cmnd, &fatal_head, &fatal_tail);
	    } else {
		/* can't fail this command now, something
		 * may be using it's buffers.  delay failing
		 * this command and all younger commands to
		 * this LUN.
		 */
		DEBUG_TIMEOUT("iSCSI: session %p itt %u task %p cmnd %p "
			      "is timedout but busy at %lu\n",
			      session, task->itt, task, task->scsi_cmnd,
			      jiffies);
		__clear_bit(task->lun, session->luns_checked);
		busy = 1;
		break;
	    }
	}

	task = next;
    }

    if (busy) {
	/* schedule another scan in the near future */
	if ((session->busy_command_timer.expires == 0)
	    && !test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    session->busy_command_timer.expires =
		jiffies + MSECS_TO_JIFFIES(40);
	    session->busy_command_timer.data = (unsigned long) session;
	    session->busy_command_timer.function = recheck_busy_commands;
	    DEBUG_TIMEOUT("iSCSI: session %p scheduling busy command scan "
			  "for %lu at %lu\n",
			  session, session->busy_command_timer.expires,
			  jiffies);
	    del_timer_sync(&session->busy_command_timer);	/* make sure 
								 * it's not 
								 * running now 
								 */
	    add_timer(&session->busy_command_timer);
	}
    }

    /* if any commands in the retry queue have TIMEDOUT,
     * dequeue and fail them. 
     */
    DEBUG_TIMEOUT("iSCSI: session %p checking %d retry commands for timeouts "
		  "at %lu\n",
		  session, atomic_read(&session->num_retry_cmnds), jiffies);
    prior = NULL;
    while ((cmnd = session->retry_cmnd_head)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd))) {
	    /* remove it from the deferred queue */
	    session->retry_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->retry_cmnd_head == NULL)
		session->retry_cmnd_tail = NULL;
	    atomic_dec(&session->num_retry_cmnds);
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing retryable command %p "
		       "cdb 0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd))) {
	    /* remove it from the deferred queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->retry_cmnd_tail == cmnd)
		session->retry_cmnd_tail = prior;
	    atomic_dec(&session->num_retry_cmnds);
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing retryable command %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	}
    }

    /* if any commands in the deferred queue have TIMEDOUT,
     * dequeue and fail them 
     */
    DEBUG_TIMEOUT("iSCSI: session %p checking %d deferred commands for "
		  "timeouts at %lu\n",
		  session, session->num_deferred_cmnds, jiffies);
    prior = NULL;
    while ((cmnd = session->deferred_cmnd_head)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd))) {
	    /* remove it from the deferred queue */
	    session->deferred_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->deferred_cmnd_head == NULL)
		session->deferred_cmnd_tail = NULL;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing deferred command %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd))) {
	    /* remove it from the deferred queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->deferred_cmnd_tail == cmnd)
		session->deferred_cmnd_tail = prior;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing deferred command %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	}
    }

    /* if any commands in the normal queue have TIMEDOUT,
     * dequeue and fail them 
     */
    DEBUG_TIMEOUT("iSCSI: session %p checking %d normal commands for "
		  "timeouts at %lu\n",
		  session, atomic_read(&session->num_cmnds), jiffies);
    prior = NULL;
    while ((cmnd = session->scsi_cmnd_head)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd))) {
	    /* remove it from the scsi_cmnd queue */
	    session->scsi_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->scsi_cmnd_head == NULL)
		session->scsi_cmnd_tail = NULL;
	    atomic_dec(&session->num_cmnds);
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing normal command %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);

	    /* and arrange for it to be completed with a fatal error */
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(COMMAND_TIMEDOUT, command_flags(cmnd)) == 0) {
	    /* remove it from the scsi_cmnd queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->scsi_cmnd_tail == cmnd)
		session->scsi_cmnd_tail = prior;
	    atomic_dec(&session->num_cmnds);
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_TIMEOUT))
		printk("iSCSI: session %p failing normal command %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu, retries %d, "
		       "allowed %d\n",
		       session, cmnd, cmnd->cmnd[0], cmnd->host->host_no,
		       cmnd->channel, cmnd->target, cmnd->lun, jiffies,
		       cmnd->retries, cmnd->allowed);

	    /* and arrange for it to be completed with a fatal error */
	    add_cmnd(cmnd, &fatal_head, &fatal_tail);
	} else {
	    prior = cmnd;
	}
    }

    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
    spin_unlock(&session->task_lock);

    /* if we have commands to fail back to the high-level
     * driver with a fatal error, do so now 
     */
    if (fatal_head) {
	DECLARE_MIDLAYER_FLAGS;

	DEBUG_TIMEOUT("iSCSI: session %p completing timedout commands at %lu\n",
		      session, jiffies);

	LOCK_MIDLAYER_LOCK(session->hba->host);
	while ((cmnd = fatal_head)) {
	    fatal_head = (Scsi_Cmnd *) cmnd->host_scribble;

	    cmnd->result = HOST_BYTE(DID_NO_CONNECT);
	    cmnd->resid = iscsi_expected_data_length(cmnd);
	    if (cmnd->allowed > 1)	/* we've exhausted all retries */
		cmnd->retries = cmnd->allowed;

	    set_not_ready(cmnd);	/* fail the whole
					 * command now,
					 * rather than just
					 * 1 buffer head */

	    /* FIXME: if it's a disk write, take the device
	     * offline? We don't want the buffer cache data
	     * loss to occur silently, but offlining the
	     * device will break multipath drivers, and
	     * cause problems for future kernels that have
	     * the cache problem fixed.
	     */
	    if (cmnd->scsi_done) {
		del_command_timer(cmnd);	/* must have already 
						 * started running, but 
						 * may not have finished yet 
						 */
		add_completion_timer(cmnd);
		cmnd->scsi_done(cmnd);
	    }
	}
	UNLOCK_MIDLAYER_LOCK(session->hba->host);
    }

    return 0;
}

static void
recheck_busy_tasks(unsigned long arg)
{
    iscsi_session_t *session = (iscsi_session_t *) arg;

    session->busy_task_timer.expires = 0;
    smp_mb();
    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
}

static int
process_timedout_tasks(iscsi_session_t * session)
{
    iscsi_task_t *task, *next, *t;
    Scsi_Cmnd *requeue_head = NULL, *requeue_tail = NULL, *defer_head =
	NULL, *defer_tail = NULL;
    Scsi_Cmnd *cmnd = NULL, *prior = NULL;
    int luns_checked, luns_recovering, tasks_recovering = 0;
    int num_requeue_cmnds = 0, num_deferred_cmnds = 0;
    int l, busy = 0;
    DECLARE_NOQUEUE_FLAGS;

    spin_lock(&session->task_lock);
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

    if (LOG_ENABLED(ISCSI_LOG_EH))
	printk("iSCSI: session %p processing timedout tasks at %lu\n", session,
	       jiffies);

    do {
	if (signal_pending(current))
	    break;		/* the session drop will
				 * take care of
				 * everything 
				 */
	if (test_bit(SESSION_TERMINATING, &session->control_bits))
	    break;		/* the session termination
				 * will take care of
				 * everything 
				 */
	/* calculate the state of each LUN based on the tasks, so that
	 * we know how to deal with the tasks and commands
	 * later on.
	 */

	clear_bit(SESSION_TASK_TIMEDOUT, &session->control_bits);
	/* we could use per-LUN data structures instead of bitmaps for these */
	memset(session->luns_checked, 0, sizeof (session->luns_checked));
	memset(session->luns_needing_recovery, 0,
	       sizeof (session->luns_needing_recovery));
	memset(session->luns_delaying_recovery, 0,
	       sizeof (session->luns_delaying_recovery));
	luns_checked = 0;
	luns_recovering = 0;
	tasks_recovering = 0;

	if (test_bit(SESSION_RESETTING, &session->control_bits)) {
	    DEBUG_EH("iSCSI: session %p resetting, task timeout processing "
		     "checking all LUNs\n", session);

	    memset(session->luns_checked, 0xFF, sizeof (session->luns_checked));
	    luns_checked += ISCSI_MAX_LUN;
	} else {
	    /* record which LUNS currently are timing out,
	     * so that we know which ones we've checked for recovery.
	     */
	    for (l = 0; l < ISCSI_MAX_LUN; l++) {
		if (test_bit(l, session->luns_timing_out)) {
		    DEBUG_EH("iSCSI: session %p task timeout processing "
			     "checking LUN %u\n", session, l);
		    __set_bit(l, session->luns_checked);
		    luns_checked++;
		}
	    }
	}

	/* scan all outstanding tasks to determine which
	 * LUNs need error recovery, and whether recovery
	 * must be delayed.
	 */
	for (task = session->arrival_order.head; task; task = task->order_next) {
	    if (test_bit(task->lun, session->luns_checked)) {
		if (TASK_NEEDS_RECOVERY(task)) {
		    /* we must do error recovery for this LUN */
		    tasks_recovering++;
		    if (__test_and_set_bit
			(task->lun, session->luns_needing_recovery))
			luns_recovering++;

		    DEBUG_EH("iSCSI: session %p itt %u task %p sc %p LUN "
			     "%u needs error recovery\n",
			     session, task->itt, task, task->scsi_cmnd,
			     task->lun);
		}

		if (!test_bit(0, &task->timedout)) {
		    /* don't do error recovery for this LUN
		     * while outstanding tasks have not yet
		     * completed or timed out.
		     */
		    __set_bit(task->lun, session->luns_delaying_recovery);
		    DEBUG_EH("iSCSI: session %p itt %u task %p sc %p has not "
			     "timed out, delaying recovery for LUN %u\n",
			     session, task->itt, task, task->scsi_cmnd,
			     task->lun);
		} else if (atomic_read(&task->refcount)) {
		    /* the task refcount may be non-zero if we're in
		     * the middle of sending or receiving data for
		     * this task.  Make sure that we don't try to
		     * finish recovery and complete the task when it's
		     * in use.
		     */
		    /* FIXME: we only want to delay
		     * finishing recovery for this LUN.  we
		     * don't have to delay sending task mgmt
		     * PDUs for this task, though we
		     * currently do.
		     */
		    __set_bit(task->lun, session->luns_needing_recovery);
		    __set_bit(task->lun, session->luns_delaying_recovery);
		    DEBUG_EH("iSCSI: session %p itt %u task %p sc %p has "
			     "timed out but is busy, delaying recovery "
			     "for LUN %u\n",
			     session, task->itt, task, task->scsi_cmnd,
			     task->lun);
		    busy = 1;
		} else {
		    DEBUG_EH("iSCSI: session %p itt %u task %p sc %p has "
			     "timed out\n",
			     session, task->itt, task, task->scsi_cmnd);
		}

		/* Note: draft 16 - 9.5.1 says we MUST keep
		 * responding to valid target transfer tags, though we
		 * can terminate them early with the F-bit, and that
		 * the target must wait for all outstanding target
		 * transfer tags to complete before doing an abort
		 * task set.  For simplicity's sake, we currently
		 * always continue responding to ttts, and send
		 * the actual data if we still have the command,
		 * or empty data PDUs if the command has already
		 * been completed out of the driver.
		 */
	    }
	}

	smp_mb();

    } while (test_bit(SESSION_TASK_TIMEDOUT, &session->control_bits));

    if (busy) {
	/* either xmit_data invoked us with a task refcount held high,
	 * or the rx thread is in the middle of receiving data for
	 * a task.
	 */
	if ((session->busy_task_timer.expires == 0)
	    && !test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    session->busy_task_timer.expires = jiffies + MSECS_TO_JIFFIES(40);
	    session->busy_task_timer.data = (unsigned long) session;
	    session->busy_task_timer.function = recheck_busy_tasks;
	    DEBUG_EH("iSCSI: session %p scheduling busy task scan for "
		     "%lu at %lu\n",
		     session, session->busy_task_timer.expires, jiffies);
	    del_timer_sync(&session->busy_task_timer);	/* make sure it's 
							 * not running now 
							 */
	    add_timer(&session->busy_task_timer);
	}
    }

    if (test_bit(SESSION_RESETTING, &session->control_bits)) {
	if (!test_bit(SESSION_RESET, &session->control_bits)) {
	    /* don't complete anything if a reset is in
	     * progress but has not yet occured 
	     */
	    DEBUG_EH("iSCSI: session %p reset in progress at %lu, "
		     "deferring recovery for all LUNs\n", session, jiffies);
	    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	    /* we may need to escalate a timedout reset though */
	    goto error_recovery;
	} else if (busy) {
	    /* reset has finished, but a task is busy,
	     * complete everything later 
	     */
	    DEBUG_EH("iSCSI: session %p reset complete but tasks busy "
		     "at %lu, deferring recovery for all LUNs\n",
		     session, jiffies);
	    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	    /* we may need to escalate a timedout reset though */
	    goto error_recovery;
	} else {
	    /* go ahead and recovery everything */
	    DEBUG_EH("iSCSI: session %p reset complete at %lu, recovering "
		     "tasks for all LUNs\n", session, jiffies);
	}
    }

    /* if we've cleared a LUN's problems, we need to requeue
     * tasks and commands to that LUN 
     */

    /* process the tasks */
    DEBUG_EH("iSCSI: session %p checking %d tasks for recovery at %lu\n",
	     session, atomic_read(&session->num_active_tasks), jiffies);
    task = session->arrival_order.head;
    while (task) {
	next = task->order_next;

	if (test_bit(task->lun, session->luns_checked)
	    && !test_bit(task->lun, session->luns_needing_recovery)) {
	    /* we're done with this task */
	    if (remove_task(&session->tx_tasks, task->itt)) {
		DEBUG_EH("iSCSI: session %p task %p data transmit "
			 "cancelled to LUN %u\n", session, task, task->lun);
		task->ttt = RSVD_TASK_TAG;
	    }
	    remove_session_task(session, task);
	    del_task_timer(task);

	    /* and this task's command */
	    if ((cmnd = task->scsi_cmnd)) {
		/* clear any Scsi_Cmnd fields that may have been modified */
		memset(cmnd->sense_buffer, 0, sizeof (cmnd->sense_buffer));
		cmnd->result = 0;
		cmnd->resid = 0;
		cmnd->host_scribble = NULL;
		/* prepare to requeue it someplace appropriate */
		if (test_bit(task->lun, session->luns_delaying_commands)) {
		    /* we need to defer this task's command and any commands
		     * for this LUN in the retry queue (since the tasks should
		     * be retried first).
		     */
		    if (LOG_ENABLED(ISCSI_LOG_EH))
			printk("iSCSI: session %p deferring itt %u task "
			       "%p cmnd %p cdb 0x%02x to (%u %u %u %u) "
			       "at %lu\n",
			       session, task->itt, task, cmnd, cmnd->cmnd[0],
			       session->host_no, session->channel,
			       session->target_id, cmnd->lun, jiffies);
		    add_cmnd(cmnd, &defer_head, &defer_tail);
		    num_deferred_cmnds++;
		} else {
		    /* requeue all tasks and retry commands
		     * back to the scsi_cmd queue.  There
		     * may be a command retry queued even
		     * when the LUN isn't failing delivery,
		     * in cases where a command completion
		     * arrived during error recovery and
		     * cleared the failing_delivery bit.
		     */
		    if (LOG_ENABLED(ISCSI_LOG_EH))
			printk("iSCSI: session %p requeueing itt %u task "
			       "%p cmnd %p cdb 0x%02x to (%u %u %u %u) "
			       "at %lu\n",
			       session, task->itt, task, cmnd, cmnd->cmnd[0],
			       session->host_no, session->channel,
			       session->target_id, cmnd->lun, jiffies);
		    add_cmnd(cmnd, &requeue_head, &requeue_tail);
		    num_requeue_cmnds++;
		}
	    }

	    /* the task has a refcount of zero and has already been
	     * removed from the session, so we can safely free it
	     * now.
	     */
	    free_task(session, task);
	}

	task = next;
    }

    /* anything in the retry queue needs to get requeued along with the tasks,
     * to avoid reordering commands.
     */
    DEBUG_EH("iSCSI: session %p checking %d retry queue commands following "
	     "task timeouts at %lu\n",
	     session, atomic_read(&session->num_retry_cmnds), jiffies);
    prior = NULL;
    while ((cmnd = session->retry_cmnd_head)) {
	if (test_bit(cmnd->lun, session->luns_checked)
	    && !test_bit(cmnd->lun, session->luns_needing_recovery)) {
	    /* remove it from the retry_cmnd queue */
	    session->retry_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->retry_cmnd_head == NULL)
		session->retry_cmnd_tail = NULL;
	    atomic_dec(&session->num_retry_cmnds);
	    cmnd->host_scribble = NULL;

	    if (test_bit(task->lun, session->luns_delaying_commands)) {
		if (LOG_ENABLED(ISCSI_LOG_EH))
		    printk("iSCSI: session %p deferring retryable cmnd "
			   "%p cdb 0x%02x to (%u %u %u %u) at %lu\n",
			   session, cmnd, cmnd->cmnd[0], session->host_no,
			   session->channel, session->target_id, cmnd->lun,
			   jiffies);
		add_cmnd(cmnd, &defer_head, &defer_tail);
		num_deferred_cmnds++;
	    } else {
		if (LOG_ENABLED(ISCSI_LOG_EH))
		    printk("iSCSI: session %p requeueing retryable cmnd "
			   "%p cdb 0x%02x to (%u %u %u %u) at %lu\n",
			   session, cmnd, cmnd->cmnd[0], session->host_no,
			   session->channel, session->target_id, cmnd->lun,
			   jiffies);
		add_cmnd(cmnd, &requeue_head, &requeue_tail);
		num_requeue_cmnds++;
	    }
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(cmnd->lun, session->luns_checked)
	    && !test_bit(cmnd->lun, session->luns_needing_recovery)) {
	    /* remove it from the retry_cmnd queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->retry_cmnd_tail == cmnd)
		session->retry_cmnd_tail = prior;
	    atomic_dec(&session->num_retry_cmnds);
	    cmnd->host_scribble = NULL;

	    if (test_bit(task->lun, session->luns_delaying_commands)) {
		if (LOG_ENABLED(ISCSI_LOG_EH))
		    printk("iSCSI: session %p deferring retryable cmnd "
			   "%p cdb 0x%02x to (%u %u %u %u) at %lu\n",
			   session, cmnd, cmnd->cmnd[0], session->host_no,
			   session->channel, session->target_id, cmnd->lun,
			   jiffies);
		add_cmnd(cmnd, &defer_head, &defer_tail);
		num_deferred_cmnds++;
	    } else {
		if (LOG_ENABLED(ISCSI_LOG_EH))
		    printk("iSCSI: session %p requeueing retryable cmnd %p "
			   "cdb 0x%02x to (%u %u %u %u) at %lu\n",
			   session, cmnd, cmnd->cmnd[0], session->host_no,
			   session->channel, session->target_id, cmnd->lun,
			   jiffies);
		add_cmnd(cmnd, &requeue_head, &requeue_tail);
		num_requeue_cmnds++;
	    }
	} else {
	    prior = cmnd;
	}
    }

    /* scan the deferred queue, moving commands to the requeue list unless
     * the LUN is currently delaying commands.
     */
    DEBUG_EH("iSCSI: session %p checking %d deferred queue commands "
	     "following task timeouts at %lu\n",
	     session, session->num_deferred_cmnds, jiffies);
    prior = NULL;
    while ((cmnd = session->deferred_cmnd_head)) {
	if (test_bit(cmnd->lun, session->luns_checked) &&
	    !test_bit(cmnd->lun, session->luns_needing_recovery) &&
	    !test_bit(cmnd->lun, session->luns_delaying_commands)) {
	    /* remove it from the deferred_cmnd queue */
	    session->deferred_cmnd_head = (Scsi_Cmnd *) cmnd->host_scribble;
	    if (session->deferred_cmnd_head == NULL)
		session->deferred_cmnd_tail = NULL;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    /* and requeue it to be sent */
	    if (LOG_ENABLED(ISCSI_LOG_EH))
		printk("iSCSI: session %p requeueing deferred cmnd %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu\n",
		       session, cmnd, cmnd->cmnd[0], session->host_no,
		       session->channel, session->target_id, cmnd->lun,
		       jiffies);
	    add_cmnd(cmnd, &requeue_head, &requeue_tail);
	    num_requeue_cmnds++;
	} else {
	    prior = cmnd;
	    break;
	}
    }
    while (prior && (cmnd = (Scsi_Cmnd *) prior->host_scribble)) {
	if (test_bit(cmnd->lun, session->luns_checked) &&
	    !test_bit(cmnd->lun, session->luns_needing_recovery) &&
	    !test_bit(cmnd->lun, session->luns_delaying_commands)) {
	    /* remove it from the deferred_cmnd queue */
	    prior->host_scribble = cmnd->host_scribble;
	    if (session->deferred_cmnd_tail == cmnd)
		session->deferred_cmnd_tail = prior;
	    session->num_deferred_cmnds--;
	    cmnd->host_scribble = NULL;

	    if (LOG_ENABLED(ISCSI_LOG_EH))
		printk("iSCSI: session %p requeueing deferred cmnd %p cdb "
		       "0x%02x to (%u %u %u %u) at %lu\n",
		       session, cmnd, cmnd->cmnd[0], session->host_no,
		       session->channel, session->target_id, cmnd->lun,
		       jiffies);
	    add_cmnd(cmnd, &requeue_head, &requeue_tail);
	    num_requeue_cmnds++;
	} else {
	    prior = cmnd;
	}
    }

    if (requeue_head) {
	/* requeue to the head of the scsi_cmnd queue */
	DEBUG_EH("iSCSI: session %p requeueing %d commands at %lu\n", session,
		 num_requeue_cmnds, jiffies);
	requeue_tail->host_scribble = (void *) session->scsi_cmnd_head;
	session->scsi_cmnd_head = requeue_head;
	if (session->scsi_cmnd_tail == NULL)
	    session->scsi_cmnd_tail = requeue_tail;
	atomic_add(num_requeue_cmnds, &session->num_cmnds);
    }

    if (defer_head) {
	/* requeue to the head of the deferred_cmnd queue */
	DEBUG_EH("iSCSI: session %p deferring %d commands at %lu\n", session,
		 num_deferred_cmnds, jiffies);
	defer_tail->host_scribble = (void *) session->deferred_cmnd_head;
	session->deferred_cmnd_head = defer_head;
	if (session->deferred_cmnd_tail == NULL)
	    session->deferred_cmnd_tail = defer_tail;
	session->num_deferred_cmnds += num_deferred_cmnds;
    }

    /* we no longer need the scsi_cmnd lock */
    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    /* clear bits and let I/O to these LUNs restart */
    if (test_and_clear_bit(SESSION_RESET, &session->control_bits)) {
	printk("iSCSI: session %p (%u %u %u *) finished reset at %lu\n",
	       session, session->host_no, session->channel, session->target_id,
	       jiffies);
	for (l = 0; l < ISCSI_MAX_LUN; l++) {
	    clear_bit(l, session->luns_doing_recovery);	/* allow completion 
							 * again 
							 */
	    clear_bit(l, session->luns_timing_out);	/* allow new tasks 
							 * again 
							 */
	}
	clear_bit(SESSION_RESETTING, &session->control_bits);
	set_bit(TX_SCSI_COMMAND, &session->control_bits);
	set_bit(TX_WAKE, &session->control_bits);
    } else {
	for (l = 0; l < ISCSI_MAX_LUN; l++) {
	    if (test_bit(l, session->luns_checked)
		&& !test_bit(l, session->luns_needing_recovery)) {
		printk("iSCSI: session %p (%u %u %u %u) finished error "
		       "recovery at %lu\n",
		       session, session->host_no, session->channel,
		       session->target_id, l, jiffies);
		clear_bit(l, session->luns_doing_recovery);	/* allow
								 * completion
								 * again 
								 */
		clear_bit(l, session->luns_timing_out);	/* allow new tasks 
							 * again 
							 */
		set_bit(TX_SCSI_COMMAND, &session->control_bits);
		set_bit(TX_WAKE, &session->control_bits);
	    }
	}
    }
    smp_mb();

  error_recovery:
    if (signal_pending(current)) {
	DEBUG_EH("iSCSI: session %p signalled during timeout processing, "
		 "skipping error recovery\n", session);
	spin_unlock(&session->task_lock);
	return 0;		/* the session drop will
				 * take care of
				 * everything 
				 */
    }

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	DEBUG_EH("iSCSI: session %p terminating, skipping error recovery\n",
		 session);
	spin_unlock(&session->task_lock);
	return 0;		/* the session termination will take care 
				 * of everything 
				 */
    }

    if (test_and_clear_bit(SESSION_TASK_MGMT_TIMEDOUT, &session->control_bits)
	&& ((task = find_session_mgmt_task(session, session->mgmt_itt)))) {
	/* a timeout has occured, escalate the task's
	 * recovery method, and quit waiting for a
	 * response 
	 */
	if (__test_and_clear_bit(TASK_TRY_ABORT, &task->flags))
	    __set_bit(TASK_TRY_ABORT_TASK_SET, &task->flags);
	else if (__test_and_clear_bit(TASK_TRY_ABORT_TASK_SET, &task->flags))
	    __set_bit(TASK_TRY_LUN_RESET, &task->flags);
	else if (__test_and_clear_bit(TASK_TRY_LUN_RESET, &task->flags))
	    __set_bit(TASK_TRY_WARM_RESET, &task->flags);
	else if (__test_and_clear_bit(TASK_TRY_WARM_RESET, &task->flags))
	    __set_bit(TASK_TRY_COLD_RESET, &task->flags);
	else {
	    printk("iSCSI: session %p cold reset timed out, dropping "
		   "session at %lu\n", session, jiffies);
	    spin_unlock(&session->task_lock);
	    iscsi_drop_session(session);
	    return 0;
	}

	session->mgmt_itt = task->mgmt_itt = RSVD_TASK_TAG;
    }

    /* if tasks need recovery and we don't have an
     * oustanding task mgmt PDU, send one 
     */
    if (tasks_recovering && (session->mgmt_itt == RSVD_TASK_TAG)) {

	DEBUG_EH("iSCSI: session %p doing error recovery at %lu, %d "
		 "tasks need recovery\n", session, jiffies, tasks_recovering);

	/* send a PDU for the oldest TIMEDOUT task needing recovery 
	 * for a LUN that needs error recovery and isn't delaying it.
	 */
	for (task = session->arrival_order.head; task; task = task->order_next) {
	    DEBUG_EH("iSCSI: session %p error recovery checking itt %u "
		     "task %p LUN %u flags 0x%04lx\n",
		     session, task->itt, task, task->lun, task->flags);

	    if (TASK_NEEDS_RECOVERY(task) &&
		test_bit(task->lun, session->luns_needing_recovery) &&
		!test_bit(task->lun, session->luns_delaying_recovery)) {
		break;
	    }
	}

	if (task) {
	    /* prevent any command completions once we start error
	     * recovery for a LUN.  We want to hang on to all of the
	     * tasks, so that we can complete them in order once error
	     * recovery finishes.
	     */
	    set_bit(task->lun, session->luns_doing_recovery);

	    if (test_bit(TASK_TRY_ABORT, &task->flags)) {
		session->mgmt_itt = task->mgmt_itt = allocate_itt(session);
		if (session->abort_timeout) {
		    session->task_mgmt_response_deadline =
			jiffies + (session->abort_timeout * HZ);
		    if (session->task_mgmt_response_deadline == 0)
			session->task_mgmt_response_deadline = 1;
		}
		atomic_inc(&task->refcount);

		if (task->scsi_cmnd)
		    printk("iSCSI: session %p sending mgmt %u abort for "
			   "itt %u task %p cmnd %p cdb 0x%02x to (%u %u %u %u) "
			   "at %lu\n",
			   session, task->mgmt_itt, task->itt, task,
			   task->scsi_cmnd, task->scsi_cmnd->cmnd[0],
			   session->host_no, session->channel,
			   session->target_id, task->lun, jiffies);
		else
		    printk("iSCSI: session %p sending mgmt %u abort for "
			   "itt %u task %p to (%u %u %u %u) at %lu\n",
			   session, task->mgmt_itt, task->itt, task,
			   session->host_no, session->channel,
			   session->target_id, task->lun, jiffies);

		spin_unlock(&session->task_lock);

		iscsi_xmit_task_mgmt(session, ISCSI_TM_FUNC_ABORT_TASK, task,
				     task->mgmt_itt);
		atomic_dec(&task->refcount);
	    } else if (test_bit(TASK_TRY_ABORT_TASK_SET, &task->flags)) {
		session->mgmt_itt = task->mgmt_itt = allocate_itt(session);
		if (session->abort_timeout) {
		    session->task_mgmt_response_deadline =
			jiffies + (session->abort_timeout * HZ);
		    if (session->task_mgmt_response_deadline == 0)
			session->task_mgmt_response_deadline = 1;
		}
		atomic_inc(&task->refcount);
		spin_unlock(&session->task_lock);

		printk("iSCSI: session %p sending mgmt %u abort task "
		       "set to (%u %u %u %u) at %lu\n",
		       session, task->mgmt_itt, session->host_no,
		       session->channel, session->target_id, task->lun,
		       jiffies);
		iscsi_xmit_task_mgmt(session, ISCSI_TM_FUNC_ABORT_TASK_SET,
				     task, task->mgmt_itt);
		atomic_dec(&task->refcount);
	    } else if (test_bit(TASK_TRY_LUN_RESET, &task->flags)) {
		session->mgmt_itt = task->mgmt_itt = allocate_itt(session);
		if (session->reset_timeout) {
		    session->task_mgmt_response_deadline =
			jiffies + (session->reset_timeout * HZ);
		    if (session->task_mgmt_response_deadline == 0)
			session->task_mgmt_response_deadline = 1;
		}
		atomic_inc(&task->refcount);
		spin_unlock(&session->task_lock);

		printk("iSCSI: session %p sending mgmt %u LUN reset to "
		       "(%u %u %u %u) at %lu\n",
		       session, task->mgmt_itt, session->host_no,
		       session->channel, session->target_id, task->lun,
		       jiffies);
		iscsi_xmit_task_mgmt(session, ISCSI_TM_FUNC_LOGICAL_UNIT_RESET,
				     task, task->mgmt_itt);
		atomic_dec(&task->refcount);
	    } else if (test_bit(TASK_TRY_WARM_RESET, &task->flags)) {
		/* block any new tasks from starting and
		 * existing tasks from completing 
		 */
		set_bit(SESSION_RESETTING, &session->control_bits);

		for (t = session->arrival_order.head; t; t = t->order_next) {
		    DEBUG_EH("iSCSI: session %p warm target reset causing "
			     "problems for LUN %u\n", session, t->lun);
		    set_bit(t->lun, session->luns_timing_out);
		    /* the task scans above assume that all
		     * tasks TIMEDOUT before error recovery
		     * could have killed the tasks.  Make it
		     * look like all tasks have TIMEDOUT, so
		     * that the LUNs affected by the target
		     * reset can be recovered in the same
		     * way as usual.
		     */
		    del_task_timer(t);
		    set_bit(0, &t->timedout);
		    /* the task mgmt response will set
		     * SESSION_TASK_TIMEDOUT and ensure
		     * these get processed later 
		     */
		}

		session->mgmt_itt = task->mgmt_itt = allocate_itt(session);
		if (session->reset_timeout) {
		    session->task_mgmt_response_deadline =
			jiffies + (session->reset_timeout * HZ);
		    if (session->task_mgmt_response_deadline == 0)
			session->task_mgmt_response_deadline = 1;
		}
		atomic_inc(&task->refcount);
		spin_unlock(&session->task_lock);

		printk("iSCSI: session %p sending mgmt %u warm target "
		       "reset to (%u %u %u *) at %lu\n",
		       session, task->mgmt_itt, session->host_no,
		       session->channel, session->target_id, jiffies);
		iscsi_xmit_task_mgmt(session, ISCSI_TM_FUNC_TARGET_WARM_RESET,
				     task, task->mgmt_itt);
		atomic_dec(&task->refcount);
	    } else if (test_bit(TASK_TRY_COLD_RESET, &task->flags)) {

		/* block any new tasks from starting and
		 * existing tasks from completing 
		 */
		set_bit(SESSION_RESETTING, &session->control_bits);

		for (t = session->arrival_order.head; t; t = t->order_next) {
		    DEBUG_EH("iSCSI: session %p cold target reset causing "
			     "problems for LUN %u\n", session, t->lun);
		    set_bit(t->lun, session->luns_timing_out);
		    /* the task scans above assume that all
		     * tasks TIMEDOUT before error recovery
		     * could have killed the tasks.  Make it
		     * look like all tasks have TIMEDOUT, so
		     * that the LUNs affected by the target
		     * reset can be recovered in the same
		     * way as usual.
		     */
		    del_task_timer(t);
		    set_bit(0, &t->timedout);
		    /* the task mgmt response will set
		     * SESSION_TASK_TIMEDOUT and ensure
		     * these get processed later 
		     */
		}

		/* tell all devices attached to this target
		 * that a reset occured we do this now,
		 * since a cold reset should cause the
		 * target to drop the session, and we
		 * probably won't get a task mgmt response
		 * for a cold reset.  FIXME: better to do
		 * this when the session actually drops?
		 */
		target_reset_occured(session);

		/* this is our last resort, so force a 10 second deadline */
		session->task_mgmt_response_deadline = jiffies + (10 * HZ);
		if (session->task_mgmt_response_deadline == 0)
		    session->task_mgmt_response_deadline = 1;
		atomic_inc(&task->refcount);
		spin_unlock(&session->task_lock);

		printk("iSCSI: session %p sending mgmt %u cold target "
		       "reset to (%u %u %u *) at %lu\n",
		       session, task->mgmt_itt, session->host_no,
		       session->channel, session->target_id, jiffies);
		iscsi_xmit_task_mgmt(session, ISCSI_TM_FUNC_TARGET_COLD_RESET,
				     task, task->mgmt_itt);
		atomic_dec(&task->refcount);
	    }
	} else {
	    spin_unlock(&session->task_lock);
	    DEBUG_EH("iSCSI: session %p couldn't find a task ready for "
		     "error recovery at %lu\n", session, jiffies);
	}
    } else {
	/* Either don't need or can't do any recovery right now. */
	spin_unlock(&session->task_lock);
    }

    DEBUG_EH("iSCSI: session %p finished processing timedout commands at %lu\n",
	     session, jiffies);
    return 0;
}

static int
start_timer(iscsi_session_t * session)
{
    iscsi_task_t *task;
    Scsi_Cmnd *cmnd;
    DECLARE_NOQUEUE_FLAGS;

    spin_lock(&session->task_lock);
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

    DEBUG_INIT("iSCSI: session iscsi bus %d target id %d starting timer "
	       "for tasks at %lu\n",
	       session->iscsi_bus, session->target_id, jiffies);

    if (test_bit(SESSION_TERMINATING, &session->control_bits))
	return 0;

    for (task = session->arrival_order.head; task; task = task->order_next) {
	cmnd = task->scsi_cmnd;
	if (cmnd) {
	    if (cmnd->eh_timeout.function == NULL)
		add_command_timer(session, cmnd, iscsi_command_times_out);
	}
    }

    for (cmnd = session->retry_cmnd_head; cmnd;
	 cmnd = (Scsi_Cmnd *) cmnd->host_scribble) {
	if (cmnd->eh_timeout.function == NULL)
	    add_command_timer(session, cmnd, iscsi_command_times_out);
    }

    for (cmnd = session->scsi_cmnd_head; cmnd;
	 cmnd = (Scsi_Cmnd *) cmnd->host_scribble) {
	if (cmnd->eh_timeout.function == NULL)
	    add_command_timer(session, cmnd, iscsi_command_times_out);
    }

    for (cmnd = session->deferred_cmnd_head; cmnd;
	 cmnd = (Scsi_Cmnd *) cmnd->host_scribble) {
	if (cmnd->eh_timeout.function == NULL)
	    add_command_timer(session, cmnd, iscsi_command_times_out);
    }

    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
    spin_unlock(&session->task_lock);

    return 1;
}

static inline void *kmap_sg(struct scatterlist *sg);
static inline void *sg_virtual_address(struct scatterlist *sg);
static inline void kunmap_sg(struct scatterlist *sg);

static int
iscsi_xmit_task(iscsi_task_t * task)
{
    struct msghdr msg;
    struct iovec iov[ISCSI_MAX_SG+4]; /* header, digest, data, pad, digest */
    struct IscsiScsiCmdHdr stsch;
    int rc, wlen;
    int remain;
    iscsi_session_t *session = task->session;
    Scsi_Cmnd *sc = task->scsi_cmnd;
    unsigned int segment_offset = 0, index = 0;
    uint32_t data_offset = 0;
    int xfrlen = 0;
    struct scatterlist *sglist = NULL, *sg, *first_sg = NULL, *last_sg = NULL;
    int iovn = 0, first_data_iovn = 0;
    int bytes_to_fill, bytes_from_segment;
    int pad_bytes = 0;
    char padding[4];
    uint32_t header_crc32c, data_crc32c;

    if (!task) {
	printk("iSCSI: xmit_task NULL\n");
	return 0;
    }

    if (!sc) {
	printk("iSCSI: xmit_task %p, cmnd NULL\n", task);
	return 0;
    }

    DEBUG_FLOW("iSCSI: xmit_task %p, itt %u to (%u %u %u %u), cdb 0x%x, "
	       "cmd_len %u, bufflen %u\n",
	       task, task->itt, sc->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0], sc->cmd_len, sc->request_bufflen);

    wlen = sizeof (stsch);
    memset(&stsch, 0, sizeof (stsch));

    if (test_bit(TASK_READ, &task->flags)) {
	/* read */
	stsch.flags |= ISCSI_FLAG_CMD_READ;
	stsch.data_length = htonl(iscsi_expected_data_length(sc));
    }
    if (test_bit(TASK_WRITE, &task->flags)) {
	/* write */
	stsch.flags |= ISCSI_FLAG_CMD_WRITE;
	stsch.data_length = htonl(iscsi_expected_data_length(sc));
    }

    /* tagged command queueing */
    stsch.flags |= (iscsi_command_attr(sc) & ISCSI_FLAG_CMD_ATTR_MASK);

    /* FIXME: if it's an untagged command, and we've already sent
     * an untagged command to the LUN, don't send a 2nd untagged command.
     * Leave it queued up and send it after the other command completes.
     * We also don't want to block commands for other LUNs.  Basically,
     * we need a per-LUN command queue.  For now, deal with it by
     * setting the Scsi_Device queue_depth to 1 without TCQ.  We can
     * reduce latency by keeping multiple commands per LUN queued to
     * the HBA, but only sending one.  That takes a more code though.
     */

    stsch.opcode = ISCSI_OP_SCSI_CMD;
    stsch.itt = htonl(task->itt);
    task->cmdsn = session->CmdSn;
    stsch.cmdsn = htonl(session->CmdSn);
    stsch.expstatsn = htonl(session->ExpStatSn);

    /* set the final bit when there are no unsolicited
     * Data-out PDUs following the command PDU 
     */
    if (!test_bit(TASK_INITIAL_R2T, &task->flags))
	stsch.flags |= ISCSI_FLAG_FINAL;
    /* FIXME: why does clearing the flags crash the kernel? */

    /* single level LUN format puts LUN in byte 1, 0 everywhere else */
    stsch.lun[1] = sc->lun;

    memcpy(stsch.scb, sc->cmnd, MIN(sizeof (stsch.scb), sc->cmd_len));

    ISCSI_TRACE(ISCSI_TRACE_TxCmd, sc, task, session->CmdSn,
		ntohl(stsch.data_length));

    /* FIXME: Sending ImmediateData along with the cmd PDU */

    /* PDU header */
    iov[0].iov_base = &stsch;
    iov[0].iov_len = sizeof (stsch);
    iovn = 1;
    wlen = sizeof (stsch);

    /* HeaderDigests */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	iov[1].iov_base = &header_crc32c;
	iov[1].iov_len = sizeof (header_crc32c);
	iovn = 2;
	wlen += sizeof (header_crc32c);
    }

    /* For ImmediateData, we need to compute the DataDigest also
     */
    if (session->ImmediateData && (sc->sc_data_direction == SCSI_DATA_WRITE)) {
	/* make sure we have data to send when we expect to */
	if (sc && (iscsi_expected_data_length(sc) == 0)
	    && ((sc->request_bufflen == 0) || (sc->request_buffer == NULL))) {
	    printk("iSCSI: xmit_task for itt %u, task %p, sc %p, expected "
		   "%u, no data in buffer\n"
		   "             request_buffer %p len %u, buffer %p len %u\n",
		   task->itt, task, sc, iscsi_expected_data_length(sc),
		   sc->request_buffer, sc->request_bufflen, sc->buffer,
		   sc->bufflen);
	    print_cmnd(sc);
	    return 0;
	}
	remain = 0;
	/* Find the segment and offset within the segment to
	 * start writing from. 
	 */
	if (sc && sc->use_sg) {
	    sg = sglist = (struct scatterlist *) sc->request_buffer;
	    segment_offset = data_offset;
	    for (index = 0; index < sc->use_sg; index++) {
		if (segment_offset < sglist[index].length)
		    break;
		else
		    segment_offset -= sglist[index].length;
	    }
	    if (index >= sc->use_sg) {
		/* didn't find the offset, command will eventually timeout */
		printk("iSCSI: session %p xmit_data for itt %u couldn't "
		       "find offset %u in sglist %p, sc %p, bufflen %u, "
		       "use_sg %u\n",
		       session, task->itt, data_offset, sglist, sc,
		       sc->request_bufflen, sc->use_sg);
		print_cmnd(sc);
		ISCSI_TRACE(ISCSI_TRACE_OutOfData, sc, task, index, sc->use_sg);
		return 0;
	    }
	}

	first_data_iovn = iovn;
	if (session->FirstBurstLength) {
	    bytes_to_fill =
		MIN(session->FirstBurstLength,
		    session->MaxXmitDataSegmentLength);
	} else {
	    bytes_to_fill = session->MaxXmitDataSegmentLength;
	}
	bytes_to_fill = MIN(bytes_to_fill, sc->request_bufflen);

	/*
	 * Check to see if we need to set the F-bit.
	 */
	if (session->FirstBurstLength == bytes_to_fill) {
	    stsch.flags |= ISCSI_FLAG_FINAL;
	}

	/* check if we need to pad the PDU */
	if (bytes_to_fill % PAD_WORD_LEN) {
	    pad_bytes = PAD_WORD_LEN - (bytes_to_fill % PAD_WORD_LEN);
	    memset(padding, 0x0, sizeof (padding));
	} else {
	    pad_bytes = 0;
	}

	if (sc) {
	    /* find all the PDU data */
	    if (sc->use_sg) {
		/* while there is more data and we want to send more data */
		while (bytes_to_fill > 0) {
		    if (index >= sc->use_sg) {
			printk("iSCSI: session %p xmit_data index %d "
			       "exceeds sc->use_sg %d, bytes_to_fill %d, "
			       "out of buffers\n",
			       session, index, sc->use_sg, bytes_to_fill);
			/* the command will eventually timeout */
			print_cmnd(sc);
			ISCSI_TRACE(ISCSI_TRACE_OutOfData, sc, task, index,
				    sc->use_sg);
			goto done;
		    }
		    sg = &sglist[index];
		    /* make sure the segment is mapped */
		    if (!kmap_sg(sg)) {
			printk("iSCSI: session %p xmit_data couldn't map "
			       "segment %p\n", session, sg);
			goto done;
		    } else if (first_sg == NULL) {
			first_sg = sg;
		    }
		    last_sg = sg;
		    /* sanity check the sglist segment length */
		    if (sg->length <= segment_offset) {
			/* the sglist is corrupt */
			printk("iSCSI: session %p xmit_data index %d, "
			       "length %u too small for offset %u, "
			       "bytes_to_fill %d, sglist has been corrupted\n",
			       session, index, sg->length, segment_offset,
			       bytes_to_fill);
			/* the command will eventually timeout */
			print_cmnd(sc);
			ISCSI_TRACE(ISCSI_TRACE_BadTxSeg, sc, task, sg->length,
				    segment_offset);
			goto done;
		    }
		    bytes_from_segment = sg->length - segment_offset;
		    if (bytes_from_segment > bytes_to_fill) {
			/* only need part of this segment */
			iov[iovn].iov_base = sg->address + segment_offset;
			iov[iovn].iov_len = bytes_to_fill;
			xfrlen += bytes_to_fill;
			DEBUG_FLOW("iSCSI: session %p xmit_data xfrlen %d, "
				   "to_fill %d, from_segment %d, iov[%2d] = "
				   "partial sg[%2d]\n",
				   session, xfrlen, bytes_to_fill,
				   bytes_from_segment, iovn, index);
			iovn++;
			segment_offset += bytes_to_fill;
			break;
		    } else {
			/* need all of this segment, and
			 * possibly more from the next 
			 */
			iov[iovn].iov_base =
			    sg_virtual_address(sg) + segment_offset;
			iov[iovn].iov_len = bytes_from_segment;
			xfrlen += bytes_from_segment;
			DEBUG_FLOW("iSCSI: session %p xmit_data xfrlen %d, "
				   "to_fill %d, from_segment %d, "
				   "iov[%2d] = sg[%2d]\n",
				   session, xfrlen, bytes_to_fill,
				   bytes_from_segment, iovn, index);
			bytes_to_fill -= bytes_from_segment;
			iovn++;
			/* any remaining data starts at
			 * offset 0 of the next segment 
			 */
			index++;
			segment_offset = 0;
		    }
		}
	    } else {
		/* no scatter-gather */
		if ((sc->request_buffer + data_offset + bytes_to_fill) <=
		    (sc->request_buffer + sc->request_bufflen)) {
		    /* send all the data */
		    iov[iovn].iov_base = sc->request_buffer + data_offset;
		    iov[iovn].iov_len = xfrlen = bytes_to_fill;
		    iovn++;
		} else if ((sc->request_buffer + data_offset) <
			   (sc->request_buffer + sc->request_bufflen)) {
		    /* send some data, but can't send all requested */
		    xfrlen = sc->request_bufflen - data_offset;
		    printk("iSCSI: xmit_data ran out of data, buffer "
			   "%p len %u but offset %d length %d, sending "
			   "final %d bytes\n",
			   sc->request_buffer, sc->request_bufflen, data_offset,
			   bytes_to_fill, xfrlen);
		    iov[iovn].iov_base = sc->request_buffer + data_offset;
		    iov[iovn].iov_len = xfrlen;
		    iovn++;
		    remain = xfrlen;
		} else {
		    /* can't send any data */
		    printk("iSCSI: xmit_data ran out of data, buffer "
			   "%p len %u but offset %d length %d, sending "
			   "no more data\n",
			   sc->request_buffer, sc->request_bufflen, data_offset,
			   bytes_to_fill);
		    goto done;
		}
	    }

	    if (pad_bytes) {
		iov[iovn].iov_base = padding;
		iov[iovn].iov_len = pad_bytes;
		iovn++;
		wlen += pad_bytes;
	    }
	}

	/* put the data length in the PDU header */
	hton24(stsch.dlength, xfrlen);
	stsch.data_length = htonl(sc->request_bufflen);
	wlen += xfrlen;
    }

    /* header complete, we can finally calculate the HeaderDigest */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	header_crc32c = iscsi_crc32c(&stsch, sizeof (stsch));
	/* FIXME: this may not be SMP safe, but it's only
	 * for testing anyway, so it probably doesn't need
	 * to be 
	 */
	if (session->fake_write_header_mismatch > 0) {
	    session->fake_write_header_mismatch--;
	    smp_mb();
	    printk("iSCSI: session %p faking HeaderDigest mismatch for "
		   "itt %u, task %p\n", session, task->itt, task);
	    header_crc32c = 0x01020304;
	}
    }
    /* DataDigest */
    if (xfrlen && (session->DataDigest == ISCSI_DIGEST_CRC32C)) {
	int i;

	data_crc32c =
	    iscsi_crc32c(iov[first_data_iovn].iov_base,
			 iov[first_data_iovn].iov_len);
	for (i = first_data_iovn + 1; i < iovn; i++) {
	    data_crc32c =
		iscsi_crc32c_continued(iov[i].iov_base, iov[i].iov_len,
				       data_crc32c);
	}

	/* FIXME: this may not be SMP safe, but it's only
	 * for testing anyway, so it probably doesn't need
	 * to be 
	 */
	if (session->fake_write_data_mismatch > 0) {
	    session->fake_write_data_mismatch--;
	    smp_mb();
	    printk("iSCSI: session %p faking DataDigest mismatch for "
		   "itt %u, task %p\n", session, task->itt, task);
	    data_crc32c = 0x01020304;
	}
	iov[iovn].iov_base = &data_crc32c;
	iov[iovn].iov_len = sizeof (data_crc32c);
	iovn++;
	wlen += sizeof (data_crc32c);
    }

    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = &iov[0];
    msg.msg_iovlen = iovn;

    ISCSI_TRACE(ISCSI_TRACE_TxDataPDU, sc, task, data_offset, xfrlen);

    rc = iscsi_sendmsg(session, &msg, wlen);
    if (rc != wlen) {
	printk("iSCSI: session %p xmit_data failed to send %d bytes, rc %d\n",
	       session, wlen, rc);
	iscsi_drop_session(session);
	goto done;
    }

    session->CmdSn++;

    return 1;

  done:
    if (first_sg) {
	/* undo any temporary mappings */
	for (sg = first_sg; sg <= last_sg; sg++) {
	    kunmap_sg(sg);
	}
    }
    return 0;
}

static int
fake_task_completion(iscsi_session_t * session, iscsi_task_t * task)
{
    struct IscsiScsiRspHdr stsrh;
    Scsi_Cmnd *sc = task->scsi_cmnd;
    unsigned char sense_buffer[32];
    int senselen = 0;
    uint32_t itt = task->itt;

    /* For testing, fake a completion with various status
     * codes when requested, without ever sending the task or
     * any data to the target, so that data corruption
     * problems will occur if the retry isn't handled
     * correctly.  
     */

    memset(&stsrh, 0, sizeof (stsrh));
    stsrh.itt = htonl(itt);

    if (session->fake_status_unreachable) {
	session->fake_status_unreachable--;
	stsrh.response = 0x82;
	printk("iSCSI: session %p faking iSCSI response 0x82 for itt "
	       "%u task %p command %p to LUN %u at %lu\n",
	       session, task->itt, task, sc, task->lun, jiffies);
    } else if (session->fake_status_busy) {
	session->fake_status_busy--;
	stsrh.cmd_status = STATUS_BUSY;
	printk("iSCSI: session %p faking SCSI status BUSY for itt %u "
	       "task %p command %p to LUN %u at %lu\n",
	       session, task->itt, task, sc, task->lun, jiffies);
    } else if (session->fake_status_queue_full) {
	session->fake_status_queue_full--;
	stsrh.cmd_status = STATUS_QUEUE_FULL;
	printk("iSCSI: session %p faking SCSI status QUEUE_FULL for itt "
	       "%u task %p command %p to LUN %u at %lu\n",
	       session, task->itt, task, sc, task->lun, jiffies);
    } else if (session->fake_status_aborted) {
	session->fake_status_aborted--;
	stsrh.cmd_status = STATUS_CHECK_CONDITION;
	stsrh.residual_count = htonl(iscsi_expected_data_length(sc));
	stsrh.flags |= ISCSI_FLAG_CMD_UNDERFLOW;
	sense_buffer[0] = 0x70;
	sense_buffer[2] = ABORTED_COMMAND;
	senselen = 8;
	printk("iSCSI: session %p faking SCSI status CHECK_CONDITION key "
	       "ABORTED_COMMAND for itt %u task %p command %p to LUN "
	       "%u at %lu\n", session, task->itt, task, sc, task->lun, jiffies);
    } else {
	/* nothing left to fake */
	session->fake_status_lun = -2;
	return 0;
    }

    /* determine command result based on the iSCSI response, status, and sense */
    process_task_response(session, task, &stsrh, sense_buffer, senselen);

    /* try to complete the command */
    complete_task(session, itt);
    /* Note: we lose the task_lock by calling complete_task */

    return 1;
}

static void
iscsi_xmit_queued_cmnds(iscsi_session_t * session)
{
    Scsi_Cmnd *sc;
    iscsi_task_t *task = NULL;
    DECLARE_NOQUEUE_FLAGS;
    uint32_t imm_data_length = 0;

    if (!session) {
	printk("iSCSI: can't xmit queued commands, no session\n");
	return;
    }

    for (;;) {

	if (signal_pending(current)) {
	    DEBUG_QUEUE("iSCSI: session %p can't start tasks now, "
			"signal pending\n", session);
	    break;
	}

	if ((atomic_read(&session->num_cmnds) == 0)
	    && (atomic_read(&session->num_retry_cmnds) == 0)) {
	    DEBUG_QUEUE("iSCSI: no SCSI cmnds queued for session %p to %s\n",
			session, session->log_name);
	    break;
	}

	if (!sna_lte(session->CmdSn, session->MaxCmdSn)) {
	    DEBUG_QUEUE("iSCSI: session %p can't start %u tasks now, "
			"ExpCmdSN %u, CmdSn %u, MaxCmdSN %u\n",
			session, atomic_read(&session->num_cmnds),
			session->ExpCmdSn, session->CmdSn, session->MaxCmdSn);
	    if (test_bit(SESSION_WINDOW_CLOSED, &session->control_bits) == 0) {
		/* window is open, but not large enough for
		 * us to send everything we have queued.
		 * record how many times we hit this
		 * situation, to see how often we're getting
		 * throttled.
		 */
		session->window_full++;
		smp_mb();
	    }
	    break;
	}

	if (test_bit(SESSION_RESETTING, &session->control_bits)) {
	    DEBUG_EH("iSCSI: session %p resetting, can't start tasks at %lu\n",
		     session, jiffies);
	    break;
	}

	DEBUG_QUEUE("iSCSI: session %p xmit_queued_cmnds, CmdSN %u, "
		    "MaxCmdSN %u\n",
		    session, session->CmdSn, session->MaxCmdSn);

	spin_lock(&session->task_lock);

	if (task == NULL) {
	    /* allocate a task */
	    task = alloc_task(session);
	    if (task == NULL) {
		printk("iSCSI: session %p to (%u %u %u *) couldn't allocate "
		       "task at %lu\n",
		       session, session->host_no, session->channel,
		       session->target_id, jiffies);
		spin_unlock(&session->task_lock);
		/* to prevent a stall of the driver, free_task must wakeup
		 * the tx thread later.
		 */
		return;
	    }
	}

	/* Don't start any new tasks if a Logout has been requested.  */
	if (test_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits)) {
	    spin_unlock(&session->task_lock);
	    DEBUG_QUEUE("iSCSI: session %p logout requested, can't start "
			"tasks now\n", session);
	    break;
	}

	SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

	if ((sc = session->retry_cmnd_head)) {
	    /* remove the command from the retry_cmnd queue */
	    session->retry_cmnd_head = (Scsi_Cmnd *) sc->host_scribble;
	    sc->host_scribble = NULL;
	    if (session->retry_cmnd_head == NULL)
		session->retry_cmnd_tail = NULL;

	    /* FIXME: we could stop using an atomic counter,
	     * if we're willing to acquire the session's
	     * scsi_cmnd_lock every time the TX_SCSI_COMMAND
	     * bit is set.  For now, we use atomic counters
	     * so that we can skip the lock acquisition if
	     * there are no commands queued.
	     */
	    atomic_dec(&session->num_retry_cmnds);

	    /* commands in the retry queue are sent even
	     * when the LUN is delaying commands, since this
	     * is how we detect that they no longer need to
	     * be delayed.
	     */

	    /* if error recovery has started or will start,
	     * don't start any new tasks 
	     */
	    if (test_bit(sc->lun, session->luns_timing_out)) {
		/* defer the command until later */
		DEBUG_EH("iSCSI: session %p deferring command %p retry "
			 "to (%u %u %u %u) at %lu\n",
			 session, sc, sc->host->host_no, sc->channel,
			 sc->target, sc->lun, jiffies);

		/* these go back on the head of the deferred queue, not the tail,
		 * to preserve ordering of commands to each LUN.
		 */
		sc->host_scribble = (void *) session->deferred_cmnd_head;
		if (session->deferred_cmnd_head == NULL)
		    session->deferred_cmnd_tail = sc;
		session->deferred_cmnd_head = sc;
		session->num_deferred_cmnds++;

		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		spin_unlock(&session->task_lock);

		/* there may be commands for other LUNs that we can send */
		continue;
	    }
	} else if ((sc = session->scsi_cmnd_head)) {
	    /* remove the command from the scsi_cmnd queue */
	    session->scsi_cmnd_head = (Scsi_Cmnd *) sc->host_scribble;
	    sc->host_scribble = NULL;
	    if (session->scsi_cmnd_head == NULL)
		session->scsi_cmnd_tail = NULL;

	    /* FIXME: we could stop using an atomic counter,
	     * if we're willing to acquire the session's
	     * scsi_cmnd_lock every time the TX_SCSI_COMMAND
	     * bit is set.  For now, we use atomic counters
	     * so that we can skip the lock acquisition if
	     * there are no commands queued.
	     */
	    atomic_dec(&session->num_cmnds);

	    /* FIXME: should we check delaying_commands
	     * first, or timing_out first?  Does it
	     * matter? 
	     */
	    if (test_bit(sc->lun, session->luns_delaying_commands)) {
		/* defer the command until later */
		DEBUG_RETRY("iSCSI: session %p deferring command "
			    "%p to (%u %u %u %u) at %lu\n",
			    session, sc, sc->host->host_no, sc->channel,
			    sc->target, sc->lun, jiffies);

		/* append it to the tail of the deferred queue */
		add_cmnd(sc, &session->deferred_cmnd_head,
			 &session->deferred_cmnd_tail);
		session->num_deferred_cmnds++;

		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		spin_unlock(&session->task_lock);

		/* there may be commands for other LUNs that we can send */
		continue;
	    }

	    if (test_bit(sc->lun, session->luns_timing_out)) {
		/* defer the command until later */
		DEBUG_EH("iSCSI: session %p deferring command %p to "
			 "(%u %u %u %u) at %lu\n",
			 session, sc, sc->host->host_no, sc->channel,
			 sc->target, sc->lun, jiffies);

		/* append it to the tail of the deferred queue */
		add_cmnd(sc, &session->deferred_cmnd_head,
			 &session->deferred_cmnd_tail);
		session->num_deferred_cmnds++;

		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		spin_unlock(&session->task_lock);

		/* there may be commands for other LUNs that we can send */
		continue;
	    }
	} else {
	    /* this should never happen if the command counts are accurate */
	    printk("iSCSI: bug - no SCSI cmnds queued at %lu for session "
		   "%p, num_cmnds %u, head %p, tail %p, num_retry %u, "
		   "head %p, tail %p\n",
		   jiffies, session, atomic_read(&session->num_cmnds),
		   session->scsi_cmnd_head, session->scsi_cmnd_tail,
		   atomic_read(&session->num_retry_cmnds),
		   session->retry_cmnd_head, session->retry_cmnd_tail);

	    atomic_set(&session->num_cmnds, 0);
	    atomic_set(&session->num_retry_cmnds, 0);

	    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	    spin_unlock(&session->task_lock);
	    break;
	}

	/* prepare to start a new task */
	__set_bit(TASK_TRY_ABORT, &task->flags);
	task->lun = sc->lun;
	task->scsi_cmnd = sc;
	iscsi_set_direction(task);
	add_session_task(session, task);

	DEBUG_QUEUE("iSCSI: cmnd %p became task %p itt %u at %lu for "
		    "session %p, num_cmnds %u, head %p, tail %p\n",
		    sc, task, task->itt, jiffies, session,
		    atomic_read(&session->num_cmnds), session->scsi_cmnd_head,
		    session->scsi_cmnd_tail);

	SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

	if ((session->fake_status_lun >= -1) &&
	    ((session->fake_status_lun == -1)
	     || (session->fake_status_lun == sc->lun))) {
	    if (fake_task_completion(session, task)) {
		/* the task has been completed, and we've lost the task_lock */
		task = NULL;
		continue;
	    } else {
		/* nothing left to fake, still have the task_lock */
		session->fake_status_lun = -2;
	    }
	}

	/* start a timer, queue up any unsolicited data, and send the task */
	add_task_timer(task, iscsi_task_times_out);
	atomic_inc(&task->refcount);
	smp_mb();

	/* possibly queue up unsolicited data PDUs.
	 * With ImmediateData, we may or may not have to send
	 * additional Data PDUs, depending on the amount of data, and
	 * the Max PDU Length, and the FirstBurstLength.
	 */
	if (test_bit(TASK_WRITE, &task->flags) && sc->request_buffer
	    && sc->request_bufflen && iscsi_expected_data_length(sc)) {

	    /* queue up unsolicited data PDUs.  the implied
	     * initial R2T doesn't count against the
	     * MaxOutstandingR2T, so we can't use the normal
	     * R2T fields of the task for the implied
	     * initial R2T.  Use a special flag for the
	     * implied initial R2T, and let the rx thread
	     * update tasks in the tx_tasks collection if an
	     * R2T comes in before the implied initial R2T
	     * has been processed.
	     */
	    if (session->ImmediateData) {
		imm_data_length = session->MaxXmitDataSegmentLength;
		imm_data_length =
		    MIN((int) imm_data_length, session->FirstBurstLength);
		imm_data_length = MIN(imm_data_length, sc->request_bufflen);

		/*
		 * Only queue unsolicited data out PDUs if there is
		 * more data in the request, and the FirstBurstLength
		 * hasn't already been satisfied with the ImmediateData that
		 * will be sent below via iscsi_xmit_task().
		 */
		if ((sc->request_bufflen > imm_data_length) &&
		    (imm_data_length < session->FirstBurstLength)) {
		    if (!session->InitialR2T) {
			__set_bit(TASK_INITIAL_R2T, &task->flags);
			/* queue up an implied R2T data transfer for later */
			add_task(&session->tx_tasks, task);
			set_bit(TX_DATA, &session->control_bits);
			set_bit(TX_WAKE, &session->control_bits);
		    }
		}
	    } else {
		if (!session->InitialR2T) {
		    __set_bit(TASK_INITIAL_R2T, &task->flags);
		    /* queue up an implied R2T data transfer for later */
		    add_task(&session->tx_tasks, task);
		    set_bit(TX_DATA, &session->control_bits);
		    set_bit(TX_WAKE, &session->control_bits);
		}
	    }
	}

	spin_unlock(&session->task_lock);

	DEBUG_FLOW("iSCSI: sending itt %u on session %p as CmdSN %u, "
		   "MaxCmdSn %u\n",
		   task->itt, session, session->CmdSn, session->MaxCmdSn);

	/* we don't bother to check if the xmit works, since if it
	 * fails, the session will drop, and all tasks and cmnds
	 * will be completed by the drop.
	 */
	iscsi_xmit_task(task);

#if INVALID_ORDERING_ASSUMPTIONS
	/* some broken targets choke if a command PDU is
	 * followed by anything other than the data for that
	 * command, but still advertise a CmdSN window of
	 * more than 1 command.  For such broken targets, we
	 * stop the loop after the first write, to try and
	 * give them the data they want.  The target may
	 * still choke if it doesn't get us an R2T before we
	 * send the next command.  Nothing we can do about
	 * that, other than log bugs against the broken
	 * targets.
	 */
	if (test_bit(TASK_WRITE, &task->flags)) {
	    atomic_dec(&task->refcount);
	    set_bit(TX_SCSI_COMMAND, &session->control_bits);
	    set_bit(TX_WAKE, &session->control_bits);
	    return;
	}
#endif

	atomic_dec(&task->refcount);
	DEBUG_FLOW("iSCSI: after sending itt %u, task %p now has refcount %d\n",
		   task->itt, task, atomic_read(&task->refcount));
	task = NULL;
    }

    /* we still have a task we never used.  free it before returning */
    if (task) {
	spin_lock(&session->task_lock);
	free_task(session, task);
	spin_unlock(&session->task_lock);
    }
}

static inline void *
sg_virtual_address(struct scatterlist *sg)
{
#if (HAS_SCATTERLIST_PAGE && HAS_SCATTERLIST_ADDRESS)
    /* page may or may not be mapped */
    if (sg->address) {
	return sg->address;
    } else if (sg->page) {
	return page_address(sg->page) + sg->offset;
    }
    return NULL;

#elif HAS_SCATTERLIST_PAGE
    /* should have already mapped the page */
    if (sg->page) {
	return page_address(sg->page) + sg->offset;
    }
    return NULL;
#else
    return sg->address;
#endif

}

/*
 * NOTE: Since the driver does not set highmem_io in the template, there is
 * no possibility that we will use highmem, and need to map the page via
 * kmap.  Thus, kmap() is never called for any linux-iscsi code that runs
 * on the 2.4 kernel (e.g. 3.4.x versions or 3.6.x versions), e.g. the
 * below code always uses "sg->address", since it's always valid for normal
 * memory (< 896MB).
 *
 * In order to use highmem_io, the "multiple kmap can deadlock" problem
 * must be solved, that is, a single thread cannot kmap() multiple times
 * without risking a deadlock.  This will be addressed in the 4-0 branch
 * most likely via the sendpage() mechanism and backported to the 3-6 branch.
 */
static inline void *
kmap_sg(struct scatterlist *sg)
{
#if (HAS_SCATTERLIST_PAGE && HAS_SCATTERLIST_ADDRESS)
    /* page may or may not be mapped if HIGHMEM is in use */
    if (sg->address) {
	DEBUG_FLOW("iSCSI: kmap sg %p to address %p\n", sg, sg->address);
	return sg->address;
    } else if (sg->page) {
	void *addr = kmap(sg->page);
	DEBUG_FLOW("iSCSI: kmap sg %p page %p to addr %p\n", sg, sg->page,
		   addr);
	return addr;
    }
    return NULL;

#elif HAS_SCATTERLIST_PAGE
    /* there is no address, must kmap the page */
    if (sg->page) {
	return kmap(sg->page);
    }
    return NULL;

#else
    /* just use the address */
    DEBUG_FLOW("iSCSI: kmap sg %p to address %p\n", sg, sg->address);
    return sg->address;
#endif
}

/*
 * NOTE: sg->address is always valid, so we never call kunmap().  See
 * comment in kmap_sg() above for more info.
 */
static inline void
kunmap_sg(struct scatterlist *sg)
{
#if (HAS_SCATTERLIST_PAGE && HAS_SCATTERLIST_ADDRESS)
    if (!sg->address && sg->page)
	kunmap(sg->page);
#elif HAS_SCATTERLIST_PAGE
    if (sg->page)
	kunmap(sg->page);
#endif
    return;
}

static void
iscsi_xmit_data(iscsi_task_t * task, uint32_t ttt, uint32_t data_offset,
		uint32_t data_length)
{
    struct msghdr msg;
    struct IscsiDataHdr stdh;
    Scsi_Cmnd *sc = NULL;
    iscsi_session_t *session = task->session;
    struct scatterlist *sglist = NULL, *sg, *first_sg = NULL, *last_sg = NULL;
    int wlen, rc, iovn = 0, first_data_iovn = 0;
    unsigned int segment_offset = 0, index = 0;
    int remain, xfrlen;
    uint32_t data_sn = 0;
    int bytes_to_fill, bytes_from_segment;
    char padding[4];
    int pad_bytes;
    uint32_t header_crc32c;
    uint32_t data_crc32c;

    sc = task->scsi_cmnd;
    /* make sure we have data to send when we expect to */
    if (sc && (iscsi_expected_data_length(sc) == 0)
	&& ((sc->request_bufflen == 0) || (sc->request_buffer == NULL))) {
	printk("iSCSI: xmit_data for itt %u, task %p, sc %p, dlength %u, "
	       "expected %u, no data in buffer\n"
	       "       request_buffer %p len %u, buffer %p len %u\n", task->itt,
	       task, sc, data_length, iscsi_expected_data_length(sc),
	       sc->request_buffer, sc->request_bufflen, sc->buffer,
	       sc->bufflen);
	print_cmnd(sc);
	return;
    }

    remain = data_length;
    if (sc == NULL)
	remain = 0;

    memset(&stdh, 0, sizeof (stdh));
    stdh.opcode = ISCSI_OP_SCSI_DATA;
    stdh.itt = htonl(task->itt);
    stdh.ttt = ttt;
    stdh.offset = htonl(data_offset);

    /* PDU header */
    session->tx_iov[0].iov_base = &stdh;
    session->tx_iov[0].iov_len = sizeof (stdh);

    DEBUG_FLOW("iSCSI: xmit_data for itt %u, task %p, credit %d @ %u\n"
	       "       request_buffer %p len %u, buffer %p len %u\n",
	       task->itt, task, remain, data_offset,
	       sc->request_buffer, sc->request_bufflen, sc->buffer,
	       sc->bufflen);

    /* Find the segment and offset within the segment to start writing from.  */
    if (sc && sc->use_sg) {
	sg = sglist = (struct scatterlist *) sc->request_buffer;

	segment_offset = data_offset;

	for (index = 0; index < sc->use_sg; index++) {
	    if (segment_offset < sglist[index].length)
		break;
	    else
		segment_offset -= sglist[index].length;
	}

	if (index >= sc->use_sg) {
	    /* didn't find the offset, command will eventually timeout */
	    printk("iSCSI: session %p xmit_data for itt %u couldn't find "
		   "offset %u in sglist %p, sc %p, bufflen %u, use_sg %u\n",
		   session, task->itt, data_offset, sglist, sc,
		   sc->request_bufflen, sc->use_sg);
	    print_cmnd(sc);
	    ISCSI_TRACE(ISCSI_TRACE_OutOfData, sc, task, index, sc->use_sg);
	    return;
	}
    }

    ISCSI_TRACE(ISCSI_TRACE_TxData, sc, task, data_offset, data_length);

    do {
	if (signal_pending(current))
	    break;

#if (INVALID_ORDERING_ASSUMPTIONS == 0)
	/* since this loop may take a while, check for
	 * TIMEDOUT tasks and commands 
	 */
	/* Note: this means a task may have a non-zero
	 * refcount during timeout processing 
	 */
	if (test_bit(SESSION_TASK_TIMEDOUT, &session->control_bits)) {
	    process_timedout_tasks(session);
	}
	if (test_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits)) {
	    process_timedout_commands(session);
	}

	/* also queue up command retries */
	if (test_and_clear_bit(SESSION_RETRY_COMMANDS, &session->control_bits)) {
	    /* try to queue up delayed commands for retries */
	    iscsi_retry_commands(session);
	}

	/* if command PDUs are small (no immediate data),
	 * start commands as soon as possible, so that we can
	 * overlap the R2T latency with the time it takes to
	 * send data for commands already issued.  This increases 
	 * throughput without significantly increasing the completion 
	 * time of commands already issued.  Some broken targets
	 * such as the one by Intel Labs will choke if they receive
	 * another command before they get all of the data for preceding
	 * commands, so this can be conditionally compiled out.
	 */
	if (!session->ImmediateData) {
	    DEBUG_FLOW("iSCSI: checking for new commands before sending "
		       "data to %s\n", session->log_name);
	    iscsi_xmit_queued_cmnds(session);
	}
#endif

	iovn = 1;
	wlen = sizeof (stdh);
	if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	    /* we'll need to send a digest, but can't compute it yet */
	    session->tx_iov[1].iov_base = &header_crc32c;
	    session->tx_iov[1].iov_len = sizeof (header_crc32c);
	    iovn = 2;
	    wlen += sizeof (header_crc32c);
	}

	first_data_iovn = iovn;

	stdh.datasn = htonl(data_sn++);
	stdh.offset = htonl(data_offset);
	stdh.expstatsn = htonl(session->ExpStatSn);

	if (session->MaxXmitDataSegmentLength
	    && (remain > session->MaxXmitDataSegmentLength)) {
	    /* enforce the target's data segment limit */
	    bytes_to_fill = session->MaxXmitDataSegmentLength;
	} else {
	    /* final PDU of a data burst */
	    bytes_to_fill = remain;
	    stdh.flags = ISCSI_FLAG_FINAL;
	}

	/* check if we need to pad the PDU */
	if (bytes_to_fill % PAD_WORD_LEN) {
	    pad_bytes = PAD_WORD_LEN - (bytes_to_fill % PAD_WORD_LEN);
	    memset(padding, 0x0, sizeof (padding));
	} else {
	    pad_bytes = 0;
	}

	DEBUG_FLOW("iSCSI: remain %d, bytes_to_fill %d, sc->use_sg %u, "
		   "MaxRecvDataSegmentLength %d\n",
		   remain, bytes_to_fill, sc->use_sg,
		   session->MaxRecvDataSegmentLength);

	xfrlen = 0;

	if (sc) {
	    /* find all the PDU data */
	    if (sc->use_sg) {
		/* while there is more data and we want to send more data */
		while (bytes_to_fill > 0) {

		    if (index >= sc->use_sg) {
			printk("iSCSI: session %p xmit_data index %d "
			       "exceeds sc->use_sg %d, bytes_to_fill %d, "
			       "out of buffers\n",
			       session, index, sc->use_sg, bytes_to_fill);
			/* the command will eventually timeout */
			print_cmnd(sc);
			ISCSI_TRACE(ISCSI_TRACE_OutOfData, sc, task, index,
				    sc->use_sg);
			goto done;
		    }
		    if (signal_pending(current)) {
			DEBUG_FLOW("iSCSI: session %p signal pending, "
				   "returning from xmit_data\n", session);
			goto done;
		    }

		    sg = &sglist[index];

		    /* make sure the segment is mapped */
		    if (!kmap_sg(sg)) {
			printk("iSCSI: session %p xmit_data couldn't map "
			       "segment %p\n", session, sg);
			goto done;
		    } else if (first_sg == NULL) {
			first_sg = sg;
		    }
		    last_sg = sg;

		    /* sanity check the sglist segment length */
		    if (sg->length <= segment_offset) {
			/* the sglist is corrupt */
			printk("iSCSI: session %p xmit_data index %d, length "
			       "%u too small for offset %u, bytes_to_fill %d, "
			       "sglist has been corrupted\n",
			       session, index, sg->length, segment_offset,
			       bytes_to_fill);
			/* the command will eventually timeout */
			print_cmnd(sc);
			ISCSI_TRACE(ISCSI_TRACE_BadTxSeg, sc, task, sg->length,
				    segment_offset);
			goto done;
		    }

		    bytes_from_segment = sg->length - segment_offset;
		    if (bytes_from_segment > bytes_to_fill) {
			/* only need part of this segment */
			session->tx_iov[iovn].iov_base =
			    sg_virtual_address(sg) + segment_offset;
			session->tx_iov[iovn].iov_len = bytes_to_fill;
			xfrlen += bytes_to_fill;
			DEBUG_FLOW("iSCSI: session %p xmit_data xfrlen %d, "
				   "to_fill %d, from_segment %d, iov[%2d] = "
				   "partial sg[%2d]\n",
				   session, xfrlen, bytes_to_fill,
				   bytes_from_segment, iovn, index);
			iovn++;
			segment_offset += bytes_to_fill;
			break;
		    } else {
			/* need all of this segment, and
			 * possibly more from the next 
			 */
			session->tx_iov[iovn].iov_base =
			    sg_virtual_address(sg) + segment_offset;
			session->tx_iov[iovn].iov_len = bytes_from_segment;
			xfrlen += bytes_from_segment;
			DEBUG_FLOW("iSCSI: session %p xmit_data xfrlen %d, "
				   "to_fill %d, from_segment %d, iov[%2d] = "
				   "sg[%2d]\n",
				   session, xfrlen, bytes_to_fill,
				   bytes_from_segment, iovn, index);
			bytes_to_fill -= bytes_from_segment;
			iovn++;
			/* any remaining data starts at
			 * offset 0 of the next segment 
			 */
			index++;
			segment_offset = 0;
		    }
		}

		if (xfrlen <= 0) {
		    printk("iSCSI: session %p xmit_data picked xfrlen "
			   "of 0, sc->use_sg %d, bytes_to_fill %d\n",
			   session, sc->use_sg, bytes_to_fill);
		    iscsi_drop_session(session);
		    goto done;
		}
	    } else {
		/* no scatter-gather */
		if ((sc->request_buffer + data_offset + bytes_to_fill) <=
		    (sc->request_buffer + sc->request_bufflen)) {
		    /* send all the data */
		    session->tx_iov[iovn].iov_base =
			sc->request_buffer + data_offset;
		    session->tx_iov[iovn].iov_len = xfrlen = bytes_to_fill;
		    iovn++;
		} else if ((sc->request_buffer + data_offset) <
			   (sc->request_buffer + sc->request_bufflen)) {
		    /* send some data, but can't send all requested */
		    xfrlen = sc->request_bufflen - data_offset;
		    printk("iSCSI: xmit_data ran out of data, buffer "
			   "%p len %u but offset %d length %d, sending "
			   "final %d bytes\n",
			   sc->request_buffer, sc->request_bufflen, data_offset,
			   bytes_to_fill, xfrlen);
		    session->tx_iov[iovn].iov_base =
			sc->request_buffer + data_offset;
		    session->tx_iov[iovn].iov_len = xfrlen;
		    iovn++;
		    stdh.flags = ISCSI_FLAG_FINAL;
		    remain = xfrlen;
		} else {
		    /* can't send any data */
		    printk("iSCSI: xmit_data ran out of data, buffer %p "
			   "len %u but offset %d length %d, sending no "
			   "more data\n",
			   sc->request_buffer, sc->request_bufflen, data_offset,
			   bytes_to_fill);
		    goto done;
		}
	    }

	    if (pad_bytes) {
		session->tx_iov[iovn].iov_base = padding;
		session->tx_iov[iovn].iov_len = pad_bytes;
		iovn++;
		wlen += pad_bytes;
	    }
	}

	/* put the data length in the PDU header */
	hton24(stdh.dlength, xfrlen);
	wlen += xfrlen;

	/* header complete, we can finally calculate the HeaderDigest */
	if (session->HeaderDigest == ISCSI_DIGEST_CRC32C)
	    header_crc32c = iscsi_crc32c(&stdh, sizeof (stdh));

	/* DataDigest */
	if (xfrlen && (session->DataDigest == ISCSI_DIGEST_CRC32C)) {
	    int i;

	    data_crc32c =
		iscsi_crc32c(session->tx_iov[first_data_iovn].iov_base,
			     session->tx_iov[first_data_iovn].iov_len);
	    for (i = first_data_iovn + 1; i < iovn; i++) {
		data_crc32c =
		    iscsi_crc32c_continued(session->tx_iov[i].iov_base,
					   session->tx_iov[i].iov_len,
					   data_crc32c);
	    }

	    /* FIXME: this may not be SMP safe, but it's
	     * only for testing anyway, so it probably
	     * doesn't need to be 
	     */
	    if (session->fake_write_data_mismatch > 0) {
		session->fake_write_data_mismatch--;
		smp_mb();
		printk("iSCSI: session %p faking DataDigest mismatch for "
		       "itt %u, task %p\n", session, task->itt, task);
		data_crc32c = 0x01020304;
	    }

	    session->tx_iov[iovn].iov_base = &data_crc32c;
	    session->tx_iov[iovn].iov_len = sizeof (data_crc32c);
	    iovn++;
	    wlen += sizeof (data_crc32c);
	}

	memset(&msg, 0, sizeof (msg));
	msg.msg_iov = &session->tx_iov[0];
	msg.msg_iovlen = iovn;

	ISCSI_TRACE(ISCSI_TRACE_TxDataPDU, sc, task, data_offset, xfrlen);

	rc = iscsi_sendmsg(session, &msg, wlen);
	if (rc != wlen) {
	    printk("iSCSI: session %p xmit_data failed to send %d bytes, "
		   "rc %d\n", session, wlen, rc);
	    iscsi_drop_session(session);
	    goto done;
	}

	remain -= xfrlen;

	DEBUG_FLOW("iSCSI: xmit_data sent %d @ %u for itt %u, remaining %d, "
		   "final %d\n",
		   xfrlen, data_offset, task->itt, remain,
		   stdh.flags & ISCSI_FLAG_FINAL);

	data_offset += xfrlen;

	if (first_sg) {
	    /* undo any temporary mappings */
	    for (sg = first_sg; sg <= last_sg; sg++) {
		kunmap_sg(sg);
	    }
	    first_sg = last_sg = NULL;
	}

    } while (remain);

  done:
    if (first_sg) {
	/* undo any temporary mappings */
	for (sg = first_sg; sg <= last_sg; sg++) {
	    kunmap_sg(sg);
	}
    }
}

static void
iscsi_xmit_r2t_data(iscsi_session_t * session)
{
    iscsi_task_t *task;
    uint32_t itt;
    uint32_t ttt;
    uint32_t offset;
    uint32_t length;
    int initial_r2t = 0;
    uint32_t implied_length = 0;
    uint32_t imm_data_length = 0;

    spin_lock(&session->task_lock);
    while ((task = pop_task(&session->tx_tasks))) {
	itt = task->itt;

	if ((initial_r2t =
	     __test_and_clear_bit(TASK_INITIAL_R2T, &task->flags))) {
	    if (session->FirstBurstLength)
		implied_length =
		    MIN(session->FirstBurstLength,
			iscsi_expected_data_length(task->scsi_cmnd));
	    else
		/* FirstBurstLength 0 means no limit */
		implied_length = iscsi_expected_data_length(task->scsi_cmnd);

	    /* For ImmediateData, we'll have to subtract it off as well */
	    if (session->ImmediateData) {
		imm_data_length = session->MaxXmitDataSegmentLength;
		imm_data_length =
		    MIN(imm_data_length, session->FirstBurstLength);
		imm_data_length =
		    MIN(imm_data_length,
			iscsi_expected_data_length(task->scsi_cmnd));
		implied_length -= imm_data_length;
	    }

	    if (implied_length == 0)
		printk("iSCSI: session %p sending empty Data PDU for "
		       "implied R2T of itt %u, task %p, cmnd NULL at %lu\n",
		       session, task->itt, task, jiffies);
	}

	/* save the values that get set when we receive an R2T from the target,
	 * so that we can receive another one while we're sending data.
	 */
	ttt = task->ttt;
	offset = task->data_offset;
	length = task->data_length;
	task->ttt = RSVD_TASK_TAG;
	if (task->scsi_cmnd == NULL) {
	    printk("iSCSI: session %p sending empty Data PDU for R2T "
		   "(%u @ %u), itt %u, ttt %u, task %p, cmnd NULL at %lu\n",
		   session, offset, length, task->itt, ntohl(ttt), task,
		   jiffies);
	    length = 0;
	}

	atomic_inc(&task->refcount);
	spin_unlock(&session->task_lock);

	/* implied initial R2T */
	if (initial_r2t) {
	    DEBUG_FLOW("iSCSI: session %p sending implied initial R2T "
		       "data (%u @ 0) for itt %u, task %p to %s\n",
		       session, implied_length, itt, task, session->log_name);

	    /* we now send an empty PDU if the implied length is zero,
	     * to handle cases where a task's command is removed and
	     * completed while the task is still queued to have data
	     * sent.  We could trigger error recovery at this point,
	     * or send an ABORT_TASK to try to quiet error message on
	     * the target about 0 length data PDUs.  If we end up
	     * trying ABORT_TASK_SET, we're required to continue
	     * responding to all outstanding ttts, though we can send
	     * empty Data PDUs with the F-bit set (like we do here).
	     */
	    iscsi_xmit_data(task, RSVD_TASK_TAG, imm_data_length,
			    implied_length);
	}

	if (signal_pending(current)) {
	    atomic_dec(&task->refcount);
	    return;
	}

	/* normal R2T from the target */
	if (ttt != RSVD_TASK_TAG) {
	    DEBUG_FLOW("iSCSI: session %p sending R2T data (%u @ %u) for "
		       "itt %u, ttt %u, task %p to %s\n",
		       session, length, offset, itt, ntohl(ttt), task,
		       session->log_name);

	    iscsi_xmit_data(task, ttt, offset, length);
	}

	atomic_dec(&task->refcount);

	if (signal_pending(current))
	    return;

	/* relock before checking loop condition */
	spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}

/* send a reply to a nop that requested one */
static void
iscsi_xmit_nop_reply(iscsi_session_t * session, iscsi_nop_info_t * nop_info)
{
    struct IscsiNopOutHdr stnoh;
    struct msghdr msg;
    struct iovec iov[5];
    int rc;
    int pad[4];
    uint32_t header_crc32c, data_crc32c;
    int length, iovn, first_data_iovn, i;

    memset(&stnoh, 0, sizeof (stnoh));
    stnoh.opcode = ISCSI_OP_NOOP_OUT | ISCSI_OP_IMMEDIATE;
    stnoh.itt = RSVD_TASK_TAG;
    stnoh.ttt = nop_info->ttt;
    stnoh.flags = ISCSI_FLAG_FINAL;
    memcpy(stnoh.lun, nop_info->lun, sizeof (stnoh.lun));
    hton24(stnoh.dlength, nop_info->dlength);
    stnoh.cmdsn = htonl(session->CmdSn);	/* don't increment 
						 * after immediate cmds 
						 */
    stnoh.expstatsn = htonl(session->ExpStatSn);

    /* PDU header */
    iov[0].iov_base = &stnoh;
    iov[0].iov_len = sizeof (stnoh);
    length = sizeof (stnoh);
    iovn = 1;

    /* HeaderDigest */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	iov[iovn].iov_base = &header_crc32c;
	iov[iovn].iov_len = sizeof (header_crc32c);
	iovn++;
	length += sizeof (header_crc32c);
    }

    first_data_iovn = iovn;

    if (nop_info->dlength) {
	/* data */
	iov[iovn].iov_base = nop_info->data;
	iov[iovn].iov_len = nop_info->dlength;
	length += nop_info->dlength;
	iovn++;

	/* pad */
	if (nop_info->dlength % PAD_WORD_LEN) {
	    memset(pad, 0, sizeof (pad));
	    iov[iovn].iov_base = pad;
	    iov[iovn].iov_len =
		PAD_WORD_LEN - (nop_info->dlength % PAD_WORD_LEN);
	    length += iov[iovn].iov_len;
	    iovn++;
	}

	/* DataDigest */
	if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	    data_crc32c =
		iscsi_crc32c(iov[first_data_iovn].iov_base,
			     iov[first_data_iovn].iov_len);

	    for (i = first_data_iovn + 1; i < iovn; i++) {
		data_crc32c =
		    iscsi_crc32c_continued(iov[i].iov_base, iov[i].iov_len,
					   data_crc32c);
	    }

	    iov[iovn].iov_base = &data_crc32c;
	    iov[iovn].iov_len = sizeof (data_crc32c);
	    length += sizeof (data_crc32c);
	    iovn++;
	}
    }

    /* HeaderDigest */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C)
	header_crc32c = iscsi_crc32c(&stnoh, sizeof (stnoh));

    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iovn;

    rc = iscsi_sendmsg(session, &msg, length);
    if (rc != length) {
	printk("iSCSI: xmit_nop %d failed, rc %d\n", length, rc);
	iscsi_drop_session(session);
    }

    ISCSI_TRACE(ISCSI_TRACE_TxNopReply, NULL, NULL, nop_info->ttt,
		nop_info->dlength);
}

/* send replies for NopIns that requested them */
static void
iscsi_xmit_nop_replys(iscsi_session_t * session)
{
    iscsi_nop_info_t *nop_info;

    /* these aren't really tasks, but it's not worth having
     * a separate lock for them 
     */
    spin_lock(&session->task_lock);

    /* space for one data-less reply is preallocated in the session itself */
    if (session->nop_reply.ttt != RSVD_TASK_TAG) {
	spin_unlock(&session->task_lock);

	iscsi_xmit_nop_reply(session, &session->nop_reply);
	session->nop_reply.ttt = RSVD_TASK_TAG;

	spin_lock(&session->task_lock);
    }

    /* if we get multiple reply requests, or they have data,
     * they'll get queued up 
     */
    while ((nop_info = session->nop_reply_head)) {
	session->nop_reply_head = nop_info->next;
	if (!session->nop_reply_head)
	    session->nop_reply_tail = NULL;
	spin_unlock(&session->task_lock);

	iscsi_xmit_nop_reply(session, nop_info);
	kfree(nop_info);
	DEBUG_ALLOC("iSCSI: kfree nop_info %p after sending nop reply\n",
		    nop_info);

	if (signal_pending(current))
	    return;

	/* relock before checking loop condition */
	spin_lock(&session->task_lock);
    }
    spin_unlock(&session->task_lock);
}

static void
iscsi_xmit_logout(iscsi_session_t * session, uint32_t itt, int reason)
{
    struct IscsiLogoutHdr stlh;
    struct msghdr msg;
    struct iovec iov[2];
    uint32_t crc32c;
    int rc, wlen;

    memset(&stlh, 0, sizeof (stlh));
    stlh.opcode = ISCSI_OP_LOGOUT_CMD | ISCSI_OP_IMMEDIATE;
    stlh.flags = ISCSI_FLAG_FINAL | (reason & ISCSI_FLAG_LOGOUT_REASON_MASK);
    stlh.itt = htonl(itt);
    stlh.cmdsn = htonl(session->CmdSn);
    stlh.expstatsn = htonl(session->ExpStatSn);

    memset(iov, 0, sizeof (iov));
    iov[0].iov_base = &stlh;
    iov[0].iov_len = sizeof (stlh);
    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    wlen = sizeof (stlh);

    /* HeaderDigests */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	crc32c = iscsi_crc32c(&stlh, sizeof (stlh));
	iov[msg.msg_iovlen].iov_base = &crc32c;
	iov[msg.msg_iovlen].iov_len = sizeof (crc32c);
	msg.msg_iovlen++;
	wlen += sizeof (crc32c);
    }

    rc = iscsi_sendmsg(session, &msg, wlen);
    if (rc != wlen) {
	printk("iSCSI: session %p xmit_logout error, rc %d, wlen %d\n", session,
	       rc, wlen);
	iscsi_drop_session(session);
    }
}

static void
iscsi_xmit_ping(iscsi_session_t * session, uint32_t itt, unsigned char *data,
		int length)
{
    struct IscsiNopOutHdr stph;
    struct msghdr msg;
    struct iovec iov[5];
    unsigned char pad[4];
    uint32_t header_crc32c, data_crc32c;
    int rc, wlen, iovn = 0, first_data_iovn, i;

    memset(&stph, 0, sizeof (stph));
    stph.opcode = ISCSI_OP_NOOP_OUT | ISCSI_OP_IMMEDIATE;
    stph.flags = ISCSI_FLAG_FINAL;
    stph.itt = htonl(itt);	/* reply request */
    stph.ttt = RSVD_TASK_TAG;
    stph.cmdsn = htonl(session->CmdSn);
    stph.expstatsn = htonl(session->ExpStatSn);

    memset(iov, 0, sizeof (iov));
    iov[0].iov_base = &stph;
    iov[0].iov_len = sizeof (stph);
    iovn = 1;
    wlen = sizeof (stph);

    /* HeaderDigests */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
	iov[iovn].iov_base = &header_crc32c;
	iov[iovn].iov_len = sizeof (header_crc32c);
	iovn++;
	wlen += sizeof (header_crc32c);
    }

    first_data_iovn = iovn;

    if (data && length) {
	hton24(stph.dlength, length);

	/* add the data */
	iov[iovn].iov_base = data;
	iov[iovn].iov_len = length;
	iovn++;
	wlen += length;

	/* may need to pad as well */
	if (length % PAD_WORD_LEN) {
	    memset(pad, 0, sizeof (pad));
	    iov[iovn].iov_base = pad;
	    iov[iovn].iov_len = PAD_WORD_LEN - (length % PAD_WORD_LEN);
	    wlen += iov[iovn].iov_len;
	    iovn++;
	}

	/* DataDigest */
	if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	    data_crc32c =
		iscsi_crc32c(iov[first_data_iovn].iov_base,
			     iov[first_data_iovn].iov_len);

	    for (i = first_data_iovn + 1; i < iovn; i++) {
		data_crc32c =
		    iscsi_crc32c_continued(iov[i].iov_base, iov[i].iov_len,
					   data_crc32c);
	    }

	    iov[iovn].iov_base = &data_crc32c;
	    iov[iovn].iov_len = sizeof (data_crc32c);
	    wlen += sizeof (data_crc32c);
	    iovn++;
	}

	DEBUG_FLOW("iSCSI: session %p tx Nop/data itt %u, lengths %d, %d, %d\n",
		   session, itt, iov[0].iov_len, iov[1].iov_len,
		   iov[2].iov_len);
    } else {
	DEBUG_FLOW("iSCSI: session %p tx Nop/data itt %u, lengths %d, %d, %d\n",
		   session, itt, iov[0].iov_len, iov[1].iov_len,
		   iov[2].iov_len);
    }

    /* can't calculate the HeaderDigest until after we've
     * filled in the dlength 
     */
    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C)
	header_crc32c = iscsi_crc32c(&stph, sizeof (stph));

    ISCSI_TRACE(ISCSI_TRACE_TxPing, NULL, NULL, itt, length);

    memset(&msg, 0, sizeof (msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iovn;

    rc = iscsi_sendmsg(session, &msg, wlen);

    if (rc != wlen) {
	printk("iSCSI: session %p xmit_ping error, rc %d, wlen %d\n", session,
	       rc, wlen);
	iscsi_drop_session(session);
    }
}

/* called by the /proc code, so we can block */
static void
iscsi_ping_test_session(iscsi_session_t * session, int total_data_length)
{
    unsigned char *rx_buffer;
    unsigned char *tx_buffer;

    /* assume that we can run the test, and allocate the memory we'll need.
     * draft 8 only allows 4K of ping data per Nop.
     */
    rx_buffer = kmalloc(4096, GFP_ATOMIC);
    tx_buffer = kmalloc(4096, GFP_ATOMIC);

    if (rx_buffer && tx_buffer) {
	unsigned char *data;
	unsigned int value = 0;

	/* put a simple pattern in the data */
	for (data = tx_buffer; data < tx_buffer + 4096; data++) {
	    *data = value & 0xFF;
	    value++;
	}

	spin_lock(&session->task_lock);
	if (session->ping_test_start == 0) {
	    /* start a ping test */
	    session->ping_test_start = jiffies;
	    session->ping_test_data_length = total_data_length;
	    session->ping_test_rx_length = total_data_length;
	    session->ping_test_rx_start = 0;
	    session->ping_test_tx_buffer = tx_buffer;

	    smp_mb();
	    wake_tx_thread(TX_PING_DATA, session);
	    printk("iSCSI: session %p starting Nop data test with total "
		   "length %u at %lu\n", session, total_data_length, jiffies);
	} else {
	    printk("iSCSI: session %p can't start Nop data test, test "
		   "started at %lu still in progress at %lu\n",
		   session, session->ping_test_start, jiffies);
	}
	spin_unlock(&session->task_lock);

	/* the tx and rx thread will free the buffers when
	 * they're done with them, so that we can just
	 * return.
	 */
    } else {
	printk("iSCSI: session %p can't start Nop data test, couldn't "
	       "allocate buffers at %lu\n", session, jiffies);
	if (rx_buffer)
	    kfree(rx_buffer);
	if (tx_buffer)
	    kfree(tx_buffer);
    }
}

/* the writer thread */
static int
iscsi_tx_thread(void *vtaskp)
{
    iscsi_session_t *session;

    if (!vtaskp) {
	printk("iSCSI: tx thread task parameter NULL\n");
	return 0;
    }

    session = (iscsi_session_t *) vtaskp;
    /* whoever created the thread already incremented the
     * session's refcount for us 
     */

    DEBUG_INIT("iSCSI: tx thread %d for session %p about to daemonize "
	       "on cpu%d\n", current->pid, session, smp_processor_id());

    /* become a daemon kernel thread, and abandon any user space resources */
    sprintf(current->comm, "iscsi-tx");
    iscsi_daemonize();
    session->tx_pid = current->pid;
    current->flags |= PF_MEMALLOC;
    smp_mb();

    /* check to see if iscsi_terminate_session was called before we
     * started running, since we can't get a signal from it until
     * until we set session->tx_pid.  
     */
    if (test_bit(SESSION_TERMINATING, &session->control_bits))
	goto ThreadExit;

    /* Block all signals except SIGHUP and SIGKILL */
    LOCK_SIGNALS();
    siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGHUP));
    RECALC_PENDING_SIGNALS;
    UNLOCK_SIGNALS();

    DEBUG_INIT("iSCSI: tx thread %d for session %p starting on cpu%d\n",
	       current->pid, session, smp_processor_id());

    while (!test_bit(SESSION_TERMINATING, &session->control_bits)) {
	wait_queue_t waitq;
	int timedout = 0;

	DEBUG_INIT("iSCSI: tx thread %d for session %p waiting for new "
		   "session to be established at %lu\n",
		   current->pid, session, jiffies);

	/* add ourselves to the login wait q, so that the rx
	 * thread can wake us up 
	 */
	init_waitqueue_entry(&waitq, current);
	add_wait_queue(&session->login_wait_q, &waitq);
	smp_mb();

	for (;;) {
	    int replacement_timeout;
	    unsigned long now;
	    long sleep_jiffies = 0;

	    /* tell the rx thread that we're blocked, and that it can
	     * safely call iscsi_sendmsg now as part of the Login
	     * phase, since we're guaranteed not to be doing any IO
	     * until the session is up.  
	     */
	    set_current_state(TASK_INTERRUPTIBLE);
	    set_bit(TX_THREAD_BLOCKED, &session->control_bits);
	    smp_mb();
	    wake_up(&session->tx_blocked_wait_q);

	    /* if the session is up, our wait is over */
	    if (test_bit(SESSION_ESTABLISHED, &session->control_bits))
		break;

	    now = jiffies;
	    replacement_timeout = session->replacement_timeout;

	    /* check for a session replacement timeout */
	    if (!timedout && replacement_timeout && session->session_drop_time
		&& time_before_eq(session->session_drop_time +
				  (replacement_timeout * HZ), now)) {
		Scsi_Cmnd *sc;
		DECLARE_NOQUEUE_FLAGS;
		DECLARE_MIDLAYER_FLAGS;

		printk("iSCSI: session %p replacement timed after %d seconds, "
		       "drop %lu, now %lu, failing all commands\n",
		       session, replacement_timeout, session->session_drop_time,
		       jiffies);

		SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		LOCK_MIDLAYER_LOCK(session->hba->host);

		/* make sure any future attempts to queue a
		 * command fail immediately 
		 */
		set_bit(SESSION_REPLACEMENT_TIMEDOUT, &session->control_bits);

		/* don't need to do this again, since we
		 * just put a barrier blocking any more
		 * commands from being queued 
		 */
		timedout = 1;

		/* we're failing all commands, so any
		 * outstanding command timeouts are also
		 * handled 
		 */
		clear_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits);

		/* complete all commands currently in the
		 * driver.  Note: this assumes that the
		 * completion callback will not call
		 * iscsi_queuecommand, since we're holding
		 * the scsi_cmnd_lock, and would deadlock
		 * with ourselves if queuecommand was
		 * called.
		 */
		while ((sc = session->scsi_cmnd_head)) {
		    session->scsi_cmnd_head = (Scsi_Cmnd *) sc->host_scribble;

                    if (session->scsi_cmnd_head == NULL)
                        session->scsi_cmnd_tail = NULL;

		    atomic_dec(&session->num_cmnds);
		    sc->result = HOST_BYTE(DID_NO_CONNECT);
		    sc->resid = iscsi_expected_data_length(sc);

		    set_lun_comm_failure(sc);

		    /* FIXME: if this is the last retry of a disk
		     * write, log a warning about possible data loss
		     * from the buffer cache?
		     */

		    if (sc->scsi_done) {
			del_command_timer(sc); /* DiskCommandTimeout's timer
						* might be running.
						*/
			add_completion_timer(sc);
			DEBUG_EH("iSCSI: session %p replacement timeout "
				 "completing %p at %lu\n",
				 session, sc, jiffies);
			sc->scsi_done(sc);
		    }
		}

		UNLOCK_MIDLAYER_LOCK(session->hba->host);
		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	    }

	    /* process any command timeouts */
	    if (test_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits)) {
		DEBUG_INIT("iSCSI: session %p processing command timeouts "
			   "while not established at %lu\n", session, jiffies);
		process_timedout_commands(session);
	    }

	    /* wait for either: 
	     *   the rx thread to tell us the session is up
	     *   the session replacement timeout to expire
	     *   a command timeout to expire for the last time
	     */
	    if (!timedout && replacement_timeout && session->session_drop_time) {
		unsigned long timeout = 0;

		/* calculate how long til the replacement timer expires */
		now = jiffies;
		if (session->session_drop_time)
		    timeout =
			session->session_drop_time + (HZ * replacement_timeout);
		else
		    timeout = now + (HZ * replacement_timeout);

		/* handle wrap-around */
		if (now <= timeout)
		    sleep_jiffies = timeout - now;
		else
		    sleep_jiffies = ULONG_MAX - now + timeout;

		DEBUG_INIT("iSCSI: session %p tx thread %d blocking at %lu, "
			   "timeout at %lu\n",
			   session, current->pid, jiffies, timeout);
		schedule_timeout(sleep_jiffies);
	    } else {
		DEBUG_INIT("iSCSI: session %p tx thread %d blocking at %lu, "
			   "timedout %d, replacement %d, drop time %lu\n",
			   session, current->pid, jiffies, timedout,
			   replacement_timeout, session->session_drop_time);
		schedule();
	    }

	    if (iscsi_handle_signals(session)) {
		DEBUG_INIT("iSCSI: session %p tx thread %d signalled at %lu "
			   "while waiting for session establishment\n",
			   session, current->pid, jiffies);
	    }

	    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
		/* we're all done */
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&session->login_wait_q, &waitq);
		goto ThreadExit;
	    }
	}

	/* remove ourselves from the login wait q */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&session->login_wait_q, &waitq);

	/* we're up and running with a new session */
	clear_bit(TX_THREAD_BLOCKED, &session->control_bits);
	DEBUG_INIT("iSCSI: tx thread %d for session %p starting to process "
		   "new session with socket %p at %lu\n",
		   current->pid, session, session->socket, jiffies);

	/* make sure we start sending commands again */
	set_bit(TX_PING, &session->control_bits);
	set_bit(TX_SCSI_COMMAND, &session->control_bits);
	set_bit(TX_WAKE, &session->control_bits);

	/* don't start any new commands if we're still trying to do a reset */
	if (test_bit(SESSION_RESET_REQUESTED, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p still has a warm reset requested "
		       "at %lu\n", session, jiffies);
	    set_bit(SESSION_RESETTING, &session->control_bits);
	}

	/* process tx requests for this session, until the session drops */
	while (!signal_pending(current)) {

	    DEBUG_FLOW("iSCSI: tx thread %d for session %p waiting at %lu\n",
		       session->tx_pid, session, jiffies);
	    wait_event_interruptible(session->tx_wait_q,
				     test_and_clear_bit(TX_WAKE,
							&session->
							control_bits));

	    DEBUG_FLOW("iSCSI: tx thread %d for session %p is awake at %lu\n",
		       session->tx_pid, session, jiffies);

	    if (signal_pending(current))
		break;

	    if (test_bit(SESSION_TASK_TIMEDOUT, &session->control_bits)) {
		process_timedout_tasks(session);
	    }

	    if (signal_pending(current))
		break;

	    if (test_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits)) {
		process_timedout_commands(session);
	    }

	    if (signal_pending(current))
		break;

	    /* See if we should send a ping (Nop with reply requested) */
	    if (test_and_clear_bit(TX_PING, &session->control_bits)) {
		uint32_t itt;

		DEBUG_FLOW("iSCSI: sending Nop/poll on session %p\n", session);
		/* may need locking someday.  see allocate_itt comment */
		itt = allocate_itt(session);
		iscsi_xmit_ping(session, itt, NULL, 0);
	    }

	    if (signal_pending(current))
		break;

	    /* See if we should send a ping (Nop with reply
	     * requested) containing test data 
	     */
	    if (test_and_clear_bit(TX_PING_DATA, &session->control_bits)) {
		int data_length, length;
		unsigned char *buffer;
		unsigned long tx_start, tx_stop;

		/* grab the total data length and buffer to use */
		spin_lock(&session->task_lock);
		length = data_length = session->ping_test_data_length;
		buffer = session->ping_test_tx_buffer;
		session->ping_test_tx_buffer = NULL;
		spin_unlock(&session->task_lock);

		tx_start = jiffies;
		while ((length > 0) && buffer) {
		    /* may need locking someday.  see allocate_itt comment */
		    uint32_t itt = allocate_itt(session);

		    DEBUG_FLOW("iSCSI: sending Nop/poll with data on "
			       "session %p\n", session);
		    iscsi_xmit_ping(session, itt, buffer, MIN(4096, length));

		    if (signal_pending(current)) {
			printk("iSCSI: session %p Nop data tx failed at %lu\n",
			       session, jiffies);
			break;
		    }

		    length -= MIN(4096, length);
		}
		tx_stop = jiffies;

		if (buffer)
		    kfree(buffer);

		printk("iSCSI: session %p tx Nop data test - tx %d of %d "
		       "bytes, start %lu, stop %lu, jiffies %lu, HZ %u\n",
		       session, data_length - length, data_length, tx_start,
		       tx_stop, tx_stop - tx_start, HZ);
	    }

	    if (signal_pending(current))
		break;

	    /* See if we should send one or more Nops
	     * (replies requested by the target) 
	     */
	    if (test_and_clear_bit(TX_NOP_REPLY, &session->control_bits)) {
		DEBUG_FLOW("iSCSI: sending Nop replies on session %p\n",
			   session);
		iscsi_xmit_nop_replys(session);
	    }

	    if (signal_pending(current))
		break;

	    /* See if we should warm reset the target */
	    if (test_bit(SESSION_RESET_REQUESTED, &session->control_bits)
		&& (session->warm_reset_itt == RSVD_TASK_TAG)) {
		if (test_bit(SESSION_RESETTING, &session->control_bits)) {
		    /* error recovery is already doing a
		     * reset, so we don't need to 
		     */
		    printk("iSCSI: session %p ignoring target reset request "
			   "for (%u %u %u *), reset already in progress "
			   "at %lu\n",
			   session, session->host_no, session->channel,
			   session->target_id, jiffies);
		    clear_bit(SESSION_RESET_REQUESTED, &session->control_bits);
		} else {
		    uint32_t itt;
		    iscsi_task_t *task;

		    spin_lock(&session->task_lock);

		    session->warm_reset_itt = itt = allocate_itt(session);
		    session->reset_response_deadline =
			jiffies + (session->reset_timeout * HZ);
		    if (session->reset_response_deadline == 0)
			session->reset_response_deadline = 1;

		    printk("iSCSI: session %p requested target reset for "
			   "(%u %u %u *), warm reset itt %u at %lu\n",
			   session, session->host_no, session->channel,
			   session->target_id, itt, jiffies);
		    /* prevent any new tasks from starting
		     * or existing tasks from completing 
		     */
		    set_bit(SESSION_RESETTING, &session->control_bits);
		    for (task = session->arrival_order.head; task;
			 task = task->order_next) {
			DEBUG_EH("iSCSI: session %p warm target reset "
				 "causing problems for LUN %u\n",
				 session, task->lun);
			set_bit(task->lun, session->luns_timing_out);
			del_task_timer(task);
			set_bit(0, &task->timedout);
			/* the task mgmt response will set
			 * SESSION_TASK_TIMEDOUT and ensure
			 * these get processed later 
			 */
		    }
		    spin_unlock(&session->task_lock);

		    iscsi_xmit_task_mgmt(session,
					 ISCSI_TM_FUNC_TARGET_WARM_RESET, NULL,
					 itt);
		}
	    }

	    if (signal_pending(current))
		break;

	    if (test_and_clear_bit
		(SESSION_RETRY_COMMANDS, &session->control_bits)) {
		/* try to queue up delayed commands for retries */
		iscsi_retry_commands(session);
	    }

	    if (signal_pending(current))
		break;

	    /* New SCSI command received, or MaxCmdSN
	     * incremented, or task freed 
	     */
	    if (test_and_clear_bit(TX_SCSI_COMMAND, &session->control_bits)) {
		/* if possible, issue new commands */
		iscsi_xmit_queued_cmnds(session);
	    }

	    if (signal_pending(current))
		break;

	    /* See if we need to send more data */
	    if (test_and_clear_bit(TX_DATA, &session->control_bits)) {
		/* NOTE: this may call iscsi_xmit_queued_cmnds 
		 * under some conditions 
		 */
		iscsi_xmit_r2t_data(session);
	    }

	    if (signal_pending(current))
		break;

	    if (test_and_clear_bit(TX_LOGOUT, &session->control_bits)) {
		uint32_t itt;

		DEBUG_INIT("iSCSI: session %p sending Logout at %lu\n", session,
			   jiffies);
		/* may need locking someday.  see allocate_itt comment */
		itt = allocate_itt(session);
		session->logout_itt = itt;
		smp_mb();
		iscsi_xmit_logout(session, itt,
				  ISCSI_LOGOUT_REASON_CLOSE_SESSION);
	    }
	}

	/* handle any signals that may have occured */
	iscsi_handle_signals(session);
    }

  ThreadExit:
    DEBUG_INIT("iSCSI: tx thread %d for session %p exiting\n", session->tx_pid,
	       session);

    /* the rx thread may be waiting for the tx thread to block.  make it happy */
    set_bit(TX_THREAD_BLOCKED, &session->control_bits);
    wake_up(&session->tx_blocked_wait_q);

    /* we're done */
    set_current_state(TASK_RUNNING);
    session->tx_pid = 0;
    smp_mb();
    drop_reference(session);

    return 0;
}

/* update LUN info for /proc/scsi/iscsi 
 * Watch the inquiry data rather than TEST_UNIT_READY response,
 * since the TURs come from the target driver (sd, st, etc), and
 * that may not happen until after a SCSI device node is opened
 * and the target driver is loaded.  We get INQUIRY commands when
 * the HBA registers, and when our driver's LUN probing forces
 * them.  This is more reliable than waiting for TURs.
 */
static void
process_inquiry_data(iscsi_session_t * session, Scsi_Cmnd * sc, uint8_t * data)
{
    if (data && sc) {
	/* look at the peripheral qualifier (bits 5,6, and 7
	 * of the first byte), SPC-3 7.4.2 
	 */
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: session %p cmd %p INQUIRY peripheral 0x%02x "
		   "from (%u %u %u %u), %s\n",
		   session, sc, *data, sc->host->host_no, sc->channel,
		   sc->target, sc->lun, session->log_name);

	switch ((*data & 0xE0) >> 5) {
	case 0:		/* possibly a LUN */
	    if (sc->lun < ISCSI_MAX_LUN) {
		/* detected a LUN */
		set_bit(sc->lun, session->luns_detected);
		set_bit(sc->lun, session->luns_found);

		/* unless it's one of our driver's LUN
		 * probes, the SCSI layer will activate this
		 * LUN 
		 */
		if ((sc->scsi_done != iscsi_done) &&
		    !test_and_set_bit(sc->lun, session->luns_activated)) {
		    /* assume we found a useable LUN */
		    session->num_luns++;
		    smp_mb();
		}
	    }
	    break;
	case 1:		/* capable of supporting a physical
				 * device at this LUN, but not
				 * currently connected 
				 */
	    /* as of 2002may09, the available Linux kernels
	     * treat qualifier 1 the same as qualifier 0,
	     * even though that's not really appropriate,
	     * and fills the log with a bunch of messages
	     * about unknown device types.  Map it to
	     * qualifier 3, which gets silently ignored.
	     */
	    *data = 0x7F;	/* Linux only ignores a 0x7F */
	    /* fall-through */
	case 2:		/* reserved */
	case 3:		/* not capable of supporting a physical device 
				 * at this LUN
				 */
	default:
	    if (sc->lun < ISCSI_MAX_LUN) {
		clear_bit(sc->lun, session->luns_detected);

		if ((sc->scsi_done != iscsi_done) &&
		    test_and_clear_bit(sc->lun, session->luns_activated)) {
		    /* there's not really a useable LUN */
		    session->num_luns--;
		    smp_mb();
		}
	    }
	    break;
	}
    } else {
	printk("iSCSI: failed to process inquiry data for session %p, "
	       "sc %p, data %p\n", session, sc, data);
    }
}

static void
iscsi_recv_logout(iscsi_session_t * session, struct IscsiLogoutRspHdr *stlh)
{
    updateSN(session, ntohl(stlh->expcmdsn), ntohl(stlh->maxcmdsn));

    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    session->logout_itt = RSVD_TASK_TAG;
    session->logout_response_deadline = 0;
    smp_mb();

    if (test_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits)) {
	switch (stlh->response) {
	case ISCSI_LOGOUT_SUCCESS:
	    /* set session's time2wait to zero?  use DefaultTime2Wait? */
	    session->time2wait = 0;
	    printk("iSCSI: session %p to %s logged out at %lu\n", session,
		   session->log_name, jiffies);
	    set_bit(SESSION_LOGGED_OUT, &session->control_bits);
	    smp_mb();
	    iscsi_drop_session(session);
	    break;
	case ISCSI_LOGOUT_CID_NOT_FOUND:
	    printk("iSCSI: session %p logout failed, cid not found\n", session);
	    iscsi_drop_session(session);
	    break;
	case ISCSI_LOGOUT_RECOVERY_UNSUPPORTED:
	    printk("iSCSI: session %p logout failed, connection recovery "
		   "not supported\n", session);
	    iscsi_drop_session(session);
	    break;
	case ISCSI_LOGOUT_CLEANUP_FAILED:
	    printk("iSCSI: session %p logout failed, cleanup failed\n",
		   session);
	    iscsi_drop_session(session);
	    break;
	default:
	    printk("iSCSI: session %p logout failed, response 0x%x\n", session,
		   stlh->response);
	    iscsi_drop_session(session);
	    break;
	}
    } else {
	printk("iSCSI: session %p received logout response at %lu, but "
	       "never sent a login request\n", session, jiffies);
	iscsi_drop_session(session);
    }
}

static int
iscsi_recv_nop_data(iscsi_session_t * session, unsigned char *buffer,
		    int data_length)
{
    /* read the nop data into the nop_info struct, and throw
     * any pad bytes away 
     */
    struct msghdr msg;
    int bytes_read = 0, rc = 0;
    int num_bytes = data_length;
    int iovn = 1;
    int pad =
	(data_length % PAD_WORD_LEN) ? (PAD_WORD_LEN -
					(data_length % PAD_WORD_LEN)) : 0;
    uint32_t received_crc32c, calculated_crc32c;

    while (bytes_read < num_bytes) {

	/* data */
	session->rx_iov[0].iov_base = buffer + bytes_read;
	session->rx_iov[0].iov_len = data_length - bytes_read;
	num_bytes = data_length - bytes_read;
	iovn = 1;

	if (pad) {
	    session->rx_iov[1].iov_base = session->rx_buffer;
	    session->rx_iov[1].iov_len = pad;
	    num_bytes += pad;
	    iovn++;
	}

	if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	    session->rx_iov[1].iov_base = &received_crc32c;
	    session->rx_iov[1].iov_len = sizeof (received_crc32c);
	    num_bytes += sizeof (received_crc32c);
	    iovn++;
	}

	memset(&msg, 0, sizeof (struct msghdr));
	msg.msg_iov = session->rx_iov;
	msg.msg_iovlen = iovn;

	rc = iscsi_recvmsg(session, &msg, num_bytes);
	if (rc <= 0) {
	    printk("iSCSI: session %p recv_nop_data failed to recv %d bytes, "
		   "rc %d\n", session, num_bytes, rc);
	    iscsi_drop_session(session);
	    return bytes_read;
	}
	if (signal_pending(current)) {
	    return bytes_read;
	}

	bytes_read += rc;
    }

    DEBUG_FLOW("iSCSI: session %p recv_nop_data read %d bytes at %lu\n",
	       session, num_bytes, jiffies);

    if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	calculated_crc32c = iscsi_crc32c(buffer, data_length + pad);
	if (calculated_crc32c != received_crc32c) {
	    printk("iSCSI: session %p recv_nop_data DataDigest mismatch, "
		   "received 0x%08x, calculated 0x%08x\n",
		   session, received_crc32c, calculated_crc32c);
	    /* we're not required to do anything if Nop data
	     * has a digest error 
	     */
	}
    }

    return data_length;
}

static void
iscsi_recv_nop(iscsi_session_t * session, struct IscsiNopInHdr *stnih)
{
    int dlength = ntoh24(stnih->dlength);

    DEBUG_FLOW("iSCSI: recv_nop for session %p from %s\n", session,
	       session->log_name);

    if (stnih->itt != RSVD_TASK_TAG) {
	/* FIXME: check StatSN */
	session->ExpStatSn = ntohl(stnih->statsn) + 1;
	updateSN(session, ntohl(stnih->expcmdsn), ntohl(stnih->maxcmdsn));
	/* it's a reply to one of our Nop-outs, so there was
	 * a PDU round-trip, and the connection is ok 
	 */
	session->last_rx = jiffies;
	smp_mb();

	ISCSI_TRACE(ISCSI_TRACE_RxPingReply, NULL, NULL, ntohl(stnih->itt),
		    dlength);

	/* if there is ping data in the reply, check to see
	 * if it matches what we expect 
	 */
	if (dlength) {
	    unsigned long rx_start, rx_stop;

	    /* FIXME: make sure the dlength won't overflow the buffer */
	    rx_start = jiffies;
	    if (iscsi_recv_nop_data(session, session->rx_buffer, dlength) !=
		dlength) {
		return;
	    }
	    rx_stop = jiffies;

	    spin_lock(&session->task_lock);
	    if (session->ping_test_rx_start == 0)
		session->ping_test_rx_start = rx_start;

	    session->ping_test_rx_length -= dlength;

	    if (session->ping_test_rx_length <= 0) {
		/* all done */
		printk("iSCSI: session %p rx Nop data test - rx %d of %d "
		       "bytes, start %lu, stop %lu, jiffies %lu, HZ %u\n",
		       session,
		       session->ping_test_data_length -
		       session->ping_test_rx_length,
		       session->ping_test_data_length,
		       session->ping_test_rx_start, rx_stop,
		       rx_stop - session->ping_test_rx_start, HZ);
		printk("iSCSI: session %p Nop data test %d bytes, start %lu, "
		       "stop %lu, jiffies %lu, HZ %u\n",
		       session, session->ping_test_data_length,
		       session->ping_test_start, rx_stop,
		       rx_stop - session->ping_test_start, HZ);
		session->ping_test_start = 0;
		session->ping_test_rx_start = 0;
	    }
	    spin_unlock(&session->task_lock);
	}
    } else {
	/* FIXME: check StatSN, but don't advance it */
	updateSN(session, ntohl(stnih->expcmdsn), ntohl(stnih->maxcmdsn));
    }

    /* check the ttt to decide whether to reply with a Nop-out */
    if (stnih->ttt != RSVD_TASK_TAG) {
	iscsi_nop_info_t *nop_info;

	ISCSI_TRACE(ISCSI_TRACE_RxNop, NULL, NULL, ntohl(stnih->itt),
		    stnih->ttt);

	if (dlength == 0) {
	    /* we preallocate space for one data-less nop reply in the
	     * session structure, to avoid having to invoke the kernel
	     * memory allocator in the common case where the target
	     * has at most one outstanding data-less nop reply
	     * requested at any given time.
	     */
	    spin_lock(&session->task_lock);
	    if ((session->nop_reply.ttt == RSVD_TASK_TAG)
		&& (session->nop_reply_head == NULL)) {
		session->nop_reply.ttt = stnih->ttt;
		memcpy(session->nop_reply.lun, stnih->lun,
		       sizeof (session->nop_reply.lun));
		spin_unlock(&session->task_lock);
		DEBUG_FLOW("iSCSI: preallocated nop reply for ttt %u, "
			   "dlength %d\n", ntohl(stnih->ttt), dlength);
		wake_tx_thread(TX_NOP_REPLY, session);
		return;
	    }
	    spin_unlock(&session->task_lock);
	}

	/* otherwise, try to allocate a nop_info struct and queue it up */
	nop_info = kmalloc(sizeof (iscsi_nop_info_t) + dlength, GFP_ATOMIC);
	if (nop_info) {
	    DEBUG_ALLOC("iSCSI: allocated nop_info %p, %u bytes\n", nop_info,
			sizeof (iscsi_nop_info_t) + dlength);
	    nop_info->next = NULL;
	    nop_info->ttt = stnih->ttt;
	    memcpy(nop_info->lun, stnih->lun, sizeof (nop_info->lun));
	    nop_info->dlength = dlength;

	    /* try to save any data from the nop for the reply */
	    if (dlength) {
		if (iscsi_recv_nop_data(session, nop_info->data, dlength) !=
		    dlength) {
		    kfree(nop_info);
		    return;
		}
	    }

	    /* queue it up */
	    spin_lock(&session->task_lock);
	    if (session->nop_reply_head) {
		session->nop_reply_tail->next = nop_info;
		session->nop_reply_tail = nop_info;
	    } else {
		session->nop_reply_head = session->nop_reply_tail = nop_info;
	    }
	    spin_unlock(&session->task_lock);

	    DEBUG_FLOW("iSCSI: queued nop reply for ttt %u, dlength %d\n",
		       ntohl(stnih->ttt), dlength);
	    wake_tx_thread(TX_NOP_REPLY, session);
	} else {
	    printk("iSCSI: session %p couldn't queue nop reply for ttt %u\n",
		   session, ntohl(stnih->ttt));
	}
    }
}

static void
iscsi_recv_cmd(iscsi_session_t * session, struct IscsiScsiRspHdr *stsrh,
	       unsigned char *sense_data)
{
    iscsi_task_t *task;
    Scsi_Cmnd *sc = NULL;
    unsigned int senselen = 0;
    uint32_t itt = ntohl(stsrh->itt);

    /* FIXME: check StatSN */
    session->ExpStatSn = ntohl(stsrh->statsn) + 1;
    updateSN(session, ntohl(stsrh->expcmdsn), ntohl(stsrh->maxcmdsn));
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    /* find the task for the itt we received */
    spin_lock(&session->task_lock);
    if ((task = find_session_task(session, itt))) {
	/* task was waiting for this command response */
	__set_bit(TASK_COMPLETED, &task->flags);
	sc = task->scsi_cmnd;

	/* for testing, we may want to ignore this command completion */
	if (session->ignore_completions && ((session->ignore_lun == -1)
					    || (session->ignore_lun ==
						task->lun))) {
	    /* for testing, the driver can be told to ignore
	     * command completion 
	     */
	    printk("iSCSI: session %p recv_cmd ignoring completion of "
		   "itt %u, task %p, LUN %u, sc %p, cdb 0x%x to "
		   "(%u %u %u %u) at %lu\n",
		   session, itt, task, task->lun, sc, sc->cmnd[0],
		   sc->host->host_no, sc->channel, sc->target, sc->lun,
		   jiffies);
	    session->ignore_completions--;
	    spin_unlock(&session->task_lock);
	    return;
	}

	del_task_timer(task);

	if (sc == NULL) {
	    printk("iSCSI: session %p recv_cmd itt %u, task %p, refcount %d, "
		   "no SCSI command at %lu\n",
		   session, itt, task, atomic_read(&task->refcount), jiffies);
	    /* this will just wait for the refcount to drop
	     * and then free the task 
	     */
	    complete_task(session, itt);
	    return;
	}

	DEBUG_QUEUE("iSCSI: session %p recv_cmd %p, itt %u, task %p, "
		    "refcount %d\n",
		    session, sc, itt, task, atomic_read(&task->refcount));
    } else {
	DEBUG_INIT("iSCSI: session %p recv_cmd - response for itt %u, "
		   "but no such task\n", session, itt);
	spin_unlock(&session->task_lock);
	return;
    }

    /* check for sense data */
    if ((ntoh24(stsrh->dlength) > 1) && sense_data) {
	/* Sense data format per draft-08, 3.4.6.  2-byte
	 * sense length, then sense data, then iSCSI
	 * response data 
	 */
	senselen = (sense_data[0] << 8) | sense_data[1];
	if (senselen > (ntoh24(stsrh->dlength) - 2))
	    senselen = (ntoh24(stsrh->dlength) - 2);
	sense_data += 2;
    }

    /* determine command result based on the iSCSI response, status, and sense */
    process_task_response(session, task, stsrh, sense_data, senselen);

#if TEST_PROBE_RECOVERY
    if (test_bit(SESSION_ESTABLISHED, &session->control_bits) && sc &&
	((sc->cmnd[0] == REPORT_LUNS) || (sc->cmnd[0] == INQUIRY)) &&
	(stsrh->cmd_status == 0) &&
	(task->cmdsn >= ABORT_FREQUENCY) &&
	((task->cmdsn % ABORT_FREQUENCY) >= 0)
	&& ((task->cmdsn % ABORT_FREQUENCY) < ABORT_COUNT)) {
	/* don't complete this command, so that we can test
	 * the probe error handling code. 
	 */
	if ((task = remove_task(&session->completing_tasks, itt))) {
	    add_task(&session->rx_tasks, task);
	    printk("iSCSI: ignoring completion of itt %u, CmdSN %u, task %p, "
		   "sc %p, cdb 0x%x to (%u %u %u %u)\n",
		   itt, task->cmdsn, task, sc, sc->cmnd[0], sc->host->host_no,
		   sc->channel, sc->target, sc->lun);
	}
	atomic_dec(&task->refcount);
	spin_unlock(&session->task_lock);
	return;
    }
#endif

    /* now that we're done with it, try to complete it.  */
    DEBUG_FLOW("iSCSI: session %p recv_cmd attempting to complete itt %u\n",
	       session, itt);
    complete_task(session, itt);
    /* Note: the task_lock will be unlocked by complete_task */
}

static void
iscsi_recv_r2t(iscsi_session_t * session, struct IscsiRttHdr *strh)
{
    iscsi_task_t *task = NULL;
    uint32_t itt = ntohl(strh->itt);

    updateSN(session, ntohl(strh->expcmdsn), ntohl(strh->maxcmdsn));
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    spin_lock(&session->task_lock);
    if ((task = find_session_task(session, itt))) {
	if (!test_bit(TASK_WRITE, &task->flags)) {
	    /* bug in the target.  the command isn't a
	     * write, so we have no data to send 
	     */
	    printk("iSCSI: session %p ignoring unexpected R2T for task %p, "
		   "itt %u, %u bytes @ offset %u, ttt %u, not a write command\n",
		   session, task, ntohl(strh->itt), ntohl(strh->data_length),
		   ntohl(strh->data_offset), ntohl(strh->ttt));
	    iscsi_drop_session(session);
	} else if (task->scsi_cmnd == NULL) {
	    printk("iSCSI: session %p ignoring R2T for task %p, itt %u, %u "
		   "bytes @ offset %u, ttt %u, no SCSI command\n",
		   session, task, ntohl(strh->itt), ntohl(strh->data_length),
		   ntohl(strh->data_offset), ntohl(strh->ttt));
	} else if (task->ttt != RSVD_TASK_TAG) {
	    /* bug in the target.  MaxOutsandingR2T == 1
	     * should have prevented this from occuring 
	     */
	    printk("iSCSI: session %p ignoring R2T for task %p, itt %u, %u "
		   "bytes @ offset %u, ttt %u, "
		   "already have R2T for %u @ %u, ttt %u\n", session, task,
		   ntohl(strh->itt), ntohl(strh->data_length),
		   ntohl(strh->data_offset), ntohl(strh->ttt),
		   task->data_length, task->data_offset, ntohl(task->ttt));
	} else {
	    /* record the R2T */
	    task->ttt = strh->ttt;
	    task->data_length = ntohl(strh->data_length);
	    task->data_offset = ntohl(strh->data_offset);
	    ISCSI_TRACE(ISCSI_TRACE_R2T, task->scsi_cmnd, task,
			task->data_offset, task->data_length);
	    DEBUG_FLOW("iSCSI: session %p R2T for task %p itt %u, %u bytes "
		       "@ offset %u\n",
		       session, task, ntohl(strh->itt),
		       ntohl(strh->data_length), ntohl(strh->data_offset));

	    /* even if we've issued an abort task set, we're required
	     * to respond to R2Ts for this task, though we can
	     * apparently set the F-bit and terminate the data burst
	     * early.  Rather than hope targets handle that correctly,
	     * we just send the data requested as usual.
	     */
	    add_task(&session->tx_tasks, task);
	    wake_tx_thread(TX_DATA, session);
	}
    } else {
	/* the task no longer exists */
	DEBUG_FLOW("iSCSI: session %p ignoring R2T for itt %u, %u bytes "
		   "@ offset %u\n",
		   session, ntohl(strh->itt), ntohl(strh->data_length),
		   ntohl(strh->data_offset));
    }
    spin_unlock(&session->task_lock);
}

static void
iscsi_recv_data(iscsi_session_t * session, struct IscsiDataRspHdr *stdrh)
{
    iscsi_task_t *task = NULL;
    Scsi_Cmnd *sc = NULL;
    struct scatterlist *sglist = NULL, *sg, *first_sg = NULL, *last_sg = NULL;
    int length, dlength, remaining, rc, i;
    int bytes_read = 0;
    uint32_t offset, expected_offset = 0;
    unsigned int iovn = 0, pad = 0;
    unsigned int segment_offset = 0;
    struct msghdr msg;
    uint32_t itt = ntohl(stdrh->itt);
    uint8_t *peripheral = NULL;
    uint32_t received_crc32c;
    int fake_data_mismatch = 0;
    int ignore_completion = 0;

    if (stdrh->flags & ISCSI_FLAG_DATA_STATUS) {
	/* FIXME: check StatSN */
	session->ExpStatSn = ntohl(stdrh->statsn) + 1;
    }
    updateSN(session, ntohl(stdrh->expcmdsn), ntohl(stdrh->maxcmdsn));
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    length = dlength = ntoh24(stdrh->dlength);
    offset = ntohl(stdrh->offset);

    /* Compute padding bytes that follow the data */
    pad = dlength % PAD_WORD_LEN;
    if (pad) {
	pad = PAD_WORD_LEN - pad;
    }

    spin_lock(&session->task_lock);

    task = find_session_task(session, itt);
    if (task == NULL) {
	printk("iSCSI: session %p recv_data, no task for itt %u (next itt %u), "
	       "discarding received data, offset %u len %u\n",
	       session, ntohl(stdrh->itt), session->itt, offset, dlength);
    } else if (!test_bit(TASK_READ, &task->flags)) {
	/* we shouldn't be getting Data-in unless it's a read */
	if (task->scsi_cmnd)
	    printk("iSCSI: session %p recv_data itt %u, task %p, command %p "
		   "cdb 0x%02x, dropping session due to unexpected Data-in "
		   "from (%u %u %u %u)\n",
		   session, itt, task, task->scsi_cmnd,
		   task->scsi_cmnd->cmnd[0], session->host_no, session->channel,
		   session->target_id, task->lun);
	else
	    printk("iSCSI: session %p recv_data itt %u, task %p, command NULL, "
		   "dropping session due to unexpected Data-in from "
		   "(%u %u %u %u)\n",
		   session, itt, task, session->host_no, session->channel,
		   session->target_id, task->lun);

	/* print the entire PDU header */
	printk("iSCSI: bogus Data-in PDU header: itt 0x%0x ttt 0x%0x, "
	       "hlength %u dlength %u lun %u, statsn 0x%0x expcmdsn "
	       "0x%0x maxcmdsn 0x%0x datasn 0x%0x, offset %u residual %u\n",
	       ntohl(stdrh->itt), ntohl(stdrh->ttt), stdrh->hlength,
	       ntoh24(stdrh->dlength), stdrh->lun[1], ntohl(stdrh->statsn),
	       ntohl(stdrh->expcmdsn), ntohl(stdrh->maxcmdsn),
	       ntohl(stdrh->datasn), ntohl(stdrh->offset),
	       ntohl(stdrh->residual_count));

	iscsi_drop_session(session);
	task = NULL;
	spin_unlock(&session->task_lock);
	return;
    } else {
	/* accept all of the data for this task */
	sc = task->scsi_cmnd;
	expected_offset = task->rxdata;

	if (sc) {
	    /* either we'll read it all, or we'll drop the
	     * session and requeue the command, so it's safe
	     * to increment the received data count before
	     * we actually read the data, while we still
	     * have the task_lock.
	     */
	    task->rxdata += dlength;

	    /* ensure the task's command won't be completed
	     * while we're using it 
	     */
	    atomic_inc(&task->refcount);

	    DEBUG_FLOW("iSCSI: session %p recv_data itt %u, task %p, sc %p, "
		       "datasn %u, offset %u, dlength %u\n",
		       session, itt, task, sc, ntohl(stdrh->datasn), offset,
		       dlength);
	} else {
	    /* command has already been completed (by a timeout) */
	    printk("iSCSI: session %p recv_data itt %u, task %p, no SCSI "
		   "command at %lu\n", session, itt, task, jiffies);
	}

	/* if there is piggybacked status, ensure that we're
	 * not delaying commands to this LUN 
	 */
	if (stdrh->flags & ISCSI_FLAG_DATA_STATUS) {
	    /* mark the task completed */
	    __set_bit(TASK_COMPLETED, &task->flags);

	    if (sc && session->ignore_completions && ((session->ignore_lun < 0)
						      || (session->ignore_lun ==
							  task->lun))) {
		/* for testing, the driver can be told to
		 * ignore command completion 
		 */
		printk("iSCSI: session %p ignoring completion of itt %u, "
		       "task %p, cmnd %p, cdb 0x%x to (%u %u %u %u) at %lu\n",
		       session, itt, task, sc, sc->cmnd[0], session->host_no,
		       session->channel, session->target_id, task->lun,
		       jiffies);

		session->ignore_completions--;
		ignore_completion = 1;
	    } else {
		del_task_timer(task);

		/* piggybacked status is always good */
		if (test_bit(task->lun, session->luns_delaying_commands)) {
		    __clear_bit(task->lun, session->luns_delaying_commands);
		    session->num_luns_delaying_commands--;
		    DEBUG_RETRY("iSCSI: session %p no longer delaying commands "
				"to (%u %u %u %u) at %lu\n",
				session, session->host_no, session->channel,
				session->target_id, task->lun, jiffies);
		    if (session->num_luns_delaying_commands == 0) {
			del_timer_sync(&session->retry_timer);
			clear_bit(SESSION_RETRY_COMMANDS,
				  &session->control_bits);
			DEBUG_RETRY("iSCSI: session %p stopping retry timer "
				    "at %lu\n", session, jiffies);
		    }
		    if (!test_bit(task->lun, session->luns_timing_out)) {
			DECLARE_NOQUEUE_FLAGS;

			SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
			requeue_deferred_commands(session, task->lun);
			SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    }
		    smp_mb();
		}
	    }
	}

	/* if there is no command, we don't increment the
	 * task refcount, so we can't keep using it 
	 */
	if (sc == NULL)
	    task = NULL;

	/* for testing, possibly fake a digest mismatch */
	if (session->fake_read_data_mismatch > 0) {
	    session->fake_read_data_mismatch--;
	    fake_data_mismatch = 1;
	}
    }
#if TEST_DELAYED_DATA
    if (task && dlength && ((task->cmdsn % 500) == 0)) {
	printk("iSCSI: testing delayed data for task %p, itt %u, cmdsn %u, "
	       "dlength %u at %lu\n",
	       task, task->itt, task->cmdsn, dlength, jiffies);
	atomic_dec(&task->refcount);
	task = NULL;
	sc = NULL;
	spin_unlock(&session->task_lock);
	session->last_rx = jiffies + (45 * HZ);
	smp_mb();
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(37 * HZ);
	spin_lock(&session->task_lock);
	printk("iSCSI: test of delayed data continuing at %lu\n", jiffies);
    }
#endif
    spin_unlock(&session->task_lock);

    if (sc == NULL)
	goto toss_data;

    /* sanity check the PDU against the command */
    if ((offset + dlength) > sc->request_bufflen) {
	/* buffer overflow, often because of a corrupt PDU header */
	printk("iSCSI: session %p recv_data for itt %u, task %p, cmnd %p, "
	       "bufflen %u, Data PDU with offset %u len %u overflows command "
	       "buffer, dropping session\n",
	       session, itt, task, sc, sc->request_bufflen, offset, dlength);

	if (task)
	    atomic_dec(&task->refcount);
	iscsi_drop_session(session);
	return;
    } else if (expected_offset != offset) {
	/* if the data arrives out-of-order, it becomes much harder
	 * for us to correctly calculate the residual if we don't get
	 * enough data and also don't get an underflow from the
	 * target.  This can happen if we discard Data PDUs due to
	 * bogus offsets/lengths.  Since we always negotiate for 
	 * Data PDUs in-order, this should never happen, but check
	 * for it anyway.
	 */
	/* buffer overflow, often because of a corrupt PDU header */
	printk("iSCSI: session %p recv_data for itt %u, task %p, cmnd %p, "
	       "bufflen %u, offset %u does not match expected offset %u, "
	       "dropping session\n",
	       session, itt, task, sc, sc->request_bufflen, offset,
	       expected_offset);

	if (task)
	    atomic_dec(&task->refcount);
	iscsi_drop_session(session);
	return;
    }

    /* configure for receiving the data */
    if (sc->use_sg) {
	int index;

	/* scatter-gather */
	sg = sglist = (struct scatterlist *) sc->request_buffer;
	segment_offset = offset;

	for (index = 0; index < sc->use_sg; index++) {
	    if (segment_offset < sglist[index].length)
		break;
	    else
		segment_offset -= sglist[index].length;
	}

	if (index >= sc->use_sg) {
	    /* didn't find the offset, toss the data and let
	     * the command underflow 
	     */
	    printk("iSCSI: session %p recv_data for itt %u couldn't find "
		   "offset %u in sglist %p, sc %p, bufflen %u, use_sg %u, "
		   "dropping session\n",
		   session, task->itt, offset, sglist, sc, sc->request_bufflen,
		   sc->use_sg);
	    print_cmnd(sc);
	    ISCSI_TRACE(ISCSI_TRACE_BadOffset, sc, task, offset,
			sc->request_bufflen);
	    /* FIXME: discard the data, or drop the session? */
	    atomic_dec(&task->refcount);
	    iscsi_drop_session(session);
	    return;
	} else {
	    remaining = dlength;

	    /* setup all the data buffers */
	    while (sc && (remaining > 0) && (index < sc->use_sg)) {
		sg = &sglist[index];

		if (!kmap_sg(sg)) {
		    printk("iSCSI: session %p recv_data itt %u task %p "
			   "failed to map sg %p\n", session, task->itt, task,
			   sg);
		    print_cmnd(sc);
		    /* FIXME: discard the data, or drop the session? */
		    atomic_dec(&task->refcount);
		    iscsi_drop_session(session);
		    return;
		} else if (first_sg == NULL) {
		    first_sg = sg;
		}

		last_sg = sg;

		/* sanity check the sglist segment length */
		if (sg->length <= segment_offset) {
		    /* the sglist is corrupt */
		    printk("iSCSI: session %p recv_data index %d, length %u "
			   "too small for offset %u, remaining %d, sglist has "
			   "been corrupted\n",
			   session, index, sg->length, segment_offset,
			   remaining);
		    print_cmnd(sc);
		    ISCSI_TRACE(ISCSI_TRACE_BadRxSeg, sc, task, sg->length,
				segment_offset);
		    /* FIXME: discard the data, or drop the session? */
		    atomic_dec(&task->refcount);
		    iscsi_drop_session(session);
		    return;
		}

		session->rx_iov[iovn].iov_base =
		    sg_virtual_address(sg) + segment_offset;
		session->rx_iov[iovn].iov_len =
		    MIN(remaining, sg->length - segment_offset);
		remaining -= session->rx_iov[iovn].iov_len;

		DEBUG_FLOW("iSCSI: recv_data itt %u, iov[%2d] = sg[%2d] = "
			   "%p, %u of %u bytes, remaining %u\n",
			   itt, iovn, sg - sglist,
			   session->rx_iov[iovn].iov_base,
			   session->rx_iov[iovn].iov_len, sg->length,
			   remaining);
		index++;
		iovn++;
		segment_offset = 0;
	    }

	    if (remaining != 0) {
		/* we ran out of buffer space with more data remaining.
		 * this should never happen if the Scsi_Cmnd's bufflen
		 * matches the combined length of the sglist segments.
		 */
		printk("iSCSI: session %p recv_data for cmnd %p, bufflen %u, "
		       "offset %u len %u, remaining data %u, dropping "
		       "session\n",
		       session, sc, sc->request_bufflen, offset, dlength,
		       remaining);
		print_cmnd(sc);
		/* FIXME: discard the data, or drop the session? */
		atomic_dec(&task->refcount);
		iscsi_drop_session(session);
		return;
	    }
	}
    } else {
	/* no scatter-gather, just read it into the buffer */
	session->rx_iov[0].iov_base = sc->request_buffer + offset;
	session->rx_iov[0].iov_len = dlength;
	iovn = 1;
    }

    if (pad) {
	session->rx_iov[iovn].iov_base = session->rx_buffer;
	session->rx_iov[iovn].iov_len = pad;
	iovn++;
	length += pad;
    }

    if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	/* If we're calculating a data digest, we need to save the pointer
	 * and length values in the iovecs before the recvmsg modifies
	 * them (or walk through the sglist again and recalculate
	 * them later, which seems inefficient).
	 */
	for (i = 0; i < iovn; i++) {
	    session->crc_rx_iov[i].iov_base = session->rx_iov[i].iov_base;
	    session->crc_rx_iov[i].iov_len = session->rx_iov[i].iov_len;
	}

	/* and we need to receive the target's digest */
	session->rx_iov[iovn].iov_base = &received_crc32c;
	session->rx_iov[iovn].iov_len = sizeof (received_crc32c);
	iovn++;
	length += sizeof (received_crc32c);
    }

    /* save the address of the first byte of INQUIRY data */
    if ((sc->cmnd[0] == INQUIRY) && (offset == 0) && (dlength > 0))
	peripheral = session->rx_iov[0].iov_base;

    /* accept the data */
    memset(&msg, 0, sizeof (struct msghdr));
    msg.msg_iov = session->rx_iov;
    msg.msg_iovlen = iovn;

    DEBUG_FLOW("iSCSI: recv_data itt %u calling recvmsg %d bytes, iovn %u, "
	       "rx_iov[0].base = %p\n",
	       itt, dlength + pad, iovn, session->rx_iov[0].iov_base);
    rc = iscsi_recvmsg(session, &msg, length);

    if (rc == length) {
	/* assume a PDU round-trip, connection is ok */
	session->last_rx = jiffies;
	smp_mb();

	if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	    uint32_t calculated_crc32c =
		iscsi_crc32c(session->crc_rx_iov[0].iov_base,
			     session->crc_rx_iov[0].iov_len);

	    /* add in all other segments, except for the digest itself */
	    for (i = 1; i < iovn - 1; i++) {
		calculated_crc32c =
		    iscsi_crc32c_continued(session->crc_rx_iov[i].iov_base,
					   session->crc_rx_iov[i].iov_len,
					   calculated_crc32c);
	    }

	    if (fake_data_mismatch) {
		printk("iSCSI: session %p faking read DataDigest mismatch "
		       "for itt %u, task %p\n", session, task->itt, task);
		calculated_crc32c = 0x01020304;
	    }

	    if (calculated_crc32c != received_crc32c) {
		unsigned int lun = task->lun;
		printk("iSCSI: session %p recv_data for itt %u, task %p, "
		       "cmnd %p DataDigest mismatch, received 0x%08x, "
		       "calculated 0x%08x, triggering error recovery "
		       "for LUN %u\n",
		       session, itt, task, sc, received_crc32c,
		       calculated_crc32c, lun);
		if (first_sg) {
		    /* undo any temporary mappings */
		    for (sg = first_sg; sg <= last_sg; sg++) {
			kunmap_sg(sg);
		    }
		    first_sg = NULL;
		}
		/* we MUST abort this task.  To avoid reordering, we
		 * trigger recovery for all tasks to this LUN.  
		 */
		spin_lock(&session->task_lock);
		task->rxdata = 0;
		atomic_dec(&task->refcount);
		trigger_error_recovery(session, lun);
		spin_unlock(&session->task_lock);
		return;
	    }
	}
    } else {
	printk("iSCSI: session %p recv_data for itt %u, task %p, cmnd %p "
	       "failed to recv %d data PDU bytes, rc %d\n",
	       session, task->itt, task, sc, length, rc);
	atomic_dec(&task->refcount);
	iscsi_drop_session(session);
	return;
    }

    /* update LUN info based on the INQUIRY data, since
     * we've got it mapped now 
     */
    if (peripheral)
	process_inquiry_data(session, task->scsi_cmnd, peripheral);

    /* done with the data buffers */
    if (first_sg) {
	/* undo any temporary mappings */
	for (sg = first_sg; sg <= last_sg; sg++) {
	    kunmap_sg(sg);
	}
    }

    ISCSI_TRACE(ISCSI_TRACE_RxData, sc, task, offset, dlength);

    if ((stdrh->flags & ISCSI_FLAG_DATA_STATUS) && !ignore_completion) {
	unsigned int expected = iscsi_expected_data_length(sc);

	/* we got status, meaning the command completed in a way that
	 * doesn't give us any sense data, and the command must be
	 * completed now, since we won't get a command response PDU.
	 */
	DEBUG_FLOW("iSCSI: Data-in with status 0x%x for itt %u, task %p, "
		   "sc %p\n",
		   stdrh->cmd_status, ntohl(stdrh->itt), task, task->scsi_cmnd);
	ISCSI_TRACE(ISCSI_TRACE_RxDataCmdStatus, sc, task, stdrh->cmd_status,
		    0);
	sc->result = HOST_BYTE(DID_OK) | STATUS_BYTE(stdrh->cmd_status);

	spin_lock(&session->task_lock);

	if ((stdrh->flags & ISCSI_FLAG_DATA_OVERFLOW)
	    || (stdrh->flags & ISCSI_FLAG_DATA_UNDERFLOW)
	    || ((test_bit(TASK_READ, &task->flags))
		&& (task->rxdata != expected))) {
	    if (LOG_ENABLED(ISCSI_LOG_QUEUE) || LOG_ENABLED(ISCSI_LOG_FLOW)) {
		printk("iSCSI: session %p task %p itt %u to (%u %u %u %u), "
		       "cdb 0x%x, %c%c %s, received %u, residual %u, expected "
		       "%u\n",
		       session, task, task->itt, sc->host->host_no, sc->channel,
		       sc->target, sc->lun, sc->cmnd[0],
		       (stdrh->flags & ISCSI_FLAG_DATA_OVERFLOW) ? 'O' : ' ',
		       (stdrh->flags & ISCSI_FLAG_DATA_UNDERFLOW) ? 'U' : ' ',
		       (stdrh->flags & ISCSI_FLAG_DATA_OVERFLOW) ? "overflow" :
		       "underflow", task->rxdata, ntohl(stdrh->residual_count),
		       expected);
	    }

	    if (stdrh->flags & ISCSI_FLAG_DATA_UNDERFLOW) {
		ISCSI_TRACE(ISCSI_TRACE_RxUnderflow, sc, task,
			    ntohl(stdrh->residual_count), expected);
		sc->resid = ntohl(stdrh->residual_count);
	    } else if (stdrh->flags & ISCSI_FLAG_DATA_OVERFLOW) {
		/* FIXME: not sure how to tell the SCSI
		 * layer of an overflow, so just give it an
		 * error 
		 */
		ISCSI_TRACE(ISCSI_TRACE_RxOverflow, sc, task,
			    ntohl(stdrh->residual_count), expected);
		sc->result =
		    HOST_BYTE(DID_ERROR) | STATUS_BYTE(stdrh->cmd_status);
	    } else {
		/* All the read data did not arrive */
		ISCSI_TRACE(ISCSI_TRACE_HostUnderflow, sc, task, task->rxdata,
			    expected);
		/* we don't know which parts of the buffer
		 * didn't get data, so report the whole
		 * buffer missing 
		 */
		sc->resid = expected;
	    }
	}

	/* done using the command's data buffers and structure fields */
	atomic_dec(&task->refcount);

	/* try to complete the task.  complete_task expects
	 * the task_lock held, but returns with it
	 * unlocked 
	 */
	complete_task(session, itt);
    } else {
	/* done modifying the command and task */
	atomic_dec(&task->refcount);
    }

    return;

  toss_data:
    /* just throw away the PDU */
    if (first_sg) {
	/* undo any temporary mappings */
	for (sg = first_sg; sg <= last_sg; sg++) {
	    kunmap_sg(sg);
	}
    }

    bytes_read = 0;
    length = dlength + pad;
    if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
	printk("iSCSI: session %p recv_data discarding %d data PDU bytes, "
	       "%d pad bytes, %Zu digest bytes\n",
	       session, dlength, pad, sizeof (received_crc32c));
	length += sizeof (received_crc32c);
    } else {
	printk("iSCSI: session %p recv_data discarding %d data PDU bytes, "
	       "%d pad bytes\n", session, dlength, pad);
    }

    while (!signal_pending(current) && (bytes_read < length)) {
	int num_bytes = MIN(length - bytes_read, sizeof (session->rx_buffer));

	/* FIXME: can we use the same rx_buffer in all the
	 * iovecs, since we're discarding the data anyway? 
	 * That would reduce the number of recvmsg calls we
	 * have to make.
	 */
	session->rx_iov[0].iov_base = session->rx_buffer;
	session->rx_iov[0].iov_len = sizeof (session->rx_buffer);
	memset(&msg, 0, sizeof (struct msghdr));
	msg.msg_iov = &session->rx_iov[0];
	msg.msg_iovlen = 1;
	rc = iscsi_recvmsg(session, &msg, num_bytes);
	if (rc <= 0) {
	    printk("iSCSI: session %p recv_data failed to recv and discard "
		   "%d data PDU bytes, rc %d, bytes_read %d\n",
		   session, length, rc, bytes_read);
	    iscsi_drop_session(session);
	} else {
	    /* assume a PDU round-trip, connection is ok */
	    bytes_read += rc;
	    DEBUG_FLOW("iSCSI: session %p recv_data discarded %d bytes, "
		       "tossed %d of %d bytes at %lu\n",
		       session, rc, bytes_read, length, jiffies);
	    session->last_rx = jiffies;
	    smp_mb();
	}
    }

    /* We don't bother checking the CRC, since we couldn't
     * retry the command anyway 
     */
    if (task) {
	atomic_dec(&task->refcount);
	task = NULL;
    }

    if (stdrh->flags & ISCSI_FLAG_DATA_STATUS) {
	spin_lock(&session->task_lock);
	complete_task(session, itt);
	/* complete_task will release the lock */
    }
}

static void
iscsi_recv_task_mgmt(iscsi_session_t * session,
		     struct IscsiScsiTaskMgtRspHdr *ststmrh)
{
    iscsi_task_t *task = NULL;
    uint32_t mgmt_itt = ntohl(ststmrh->itt);
    int ignored = 0;

    /* FIXME: check StatSN */
    session->ExpStatSn = ntohl(ststmrh->statsn) + 1;
    updateSN(session, ntohl(ststmrh->expcmdsn), ntohl(ststmrh->maxcmdsn));
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    spin_lock(&session->task_lock);

    /* we should always find the task, since we don't allow them to leave
     * the driver once we've started error recovery, and we shouldn't
     * receive a task mgmt response until we've started error recovery.
     */
    if ((task = find_session_mgmt_task(session, mgmt_itt))) {
	/* we save the recovery state in the session when we send task mgmt PDUs,
	 * since a command completion that arrives after we start recovery may
	 * change the task's state after we send the task mgmt PDU.  We want
	 * to remember what we sent and act accordingly.
	 */
	if (test_bit(TASK_TRY_ABORT, &task->flags)) {
	    ISCSI_TRACE(ISCSI_TRACE_RxAbort, task->scsi_cmnd, task, mgmt_itt,
			ststmrh->response);
	    if (session->ignore_aborts && ((session->ignore_lun < 0)
					   || (session->ignore_lun ==
					       task->lun))) {
		session->ignore_aborts--;
		ignored = 1;
		if (task->scsi_cmnd)
		    printk("iSCSI: session %p ignoring abort response 0x%x "
			   "for mgmt %u, itt %u, task %p, cmnd %p, cdb "
			   "0x%x at %lu\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task, task->scsi_cmnd,
			   task->scsi_cmnd->cmnd[0], jiffies);
		else
		    printk("iSCSI: session %p ignoring abort response 0x%x "
			   "for mgmt %u, itt %u, task %p, cmnd NULL at %lu\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task, jiffies);
	    } else if (session->reject_aborts && ((session->reject_lun < 0)
						  || (session->reject_lun ==
						      task->lun))) {
		session->reject_aborts--;
		if (task->scsi_cmnd)
		    printk("iSCSI: session %p treating abort response 0x%x "
			   "as reject for mgmt %u, itt %u, task %p, cmnd %p, "
			   "cdb 0x%x\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task, task->scsi_cmnd,
			   task->scsi_cmnd->cmnd[0]);
		else
		    printk("iSCSI: session %p treating abort response 0x%x as "
			   "reject for mgmt %u, itt %u, task %p, cmnd NULL\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task);

		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_ABORT_TASK_SET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (ststmrh->response == 0) {
		if (task->scsi_cmnd)
		    printk("iSCSI: session %p abort success for mgmt %u, itt "
			   "%u, task %p, cmnd %p, cdb 0x%x\n",
			   session, ntohl(ststmrh->itt), task->itt, task,
			   task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
		else
		    printk("iSCSI: session %p abort success for mgmt %u, itt "
			   "%u, task %p, cmnd NULL\n",
			   session, ntohl(ststmrh->itt), task->itt, task);
		task->flags &= ~TASK_RECOVERY_MASK;
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (test_bit(TASK_COMPLETED, &task->flags)) {
		/* we received a command completion before the abort response, 
		 * so the task mgmt abort doesn't need to succeed.
		 */
		if (task->scsi_cmnd)
		    printk("iSCSI: session %p abort success for mgmt %u due "
			   "to completion of itt %u, task %p, cmnd %p, "
			   "cdb 0x%x\n",
			   session, ntohl(ststmrh->itt), task->itt, task,
			   task->scsi_cmnd, task->scsi_cmnd->cmnd[0]);
		else
		    printk("iSCSI: session %p abort success for mgmt %u "
			   "due to completion of itt %u, task %p, "
			   "cmnd NULL\n",
			   session, ntohl(ststmrh->itt), task->itt, task);

		task->flags &= ~TASK_RECOVERY_MASK;
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else {
		if (task->scsi_cmnd)
		    printk("iSCSI: session %p abort rejected (0x%x) for "
			   "mgmt %u, itt %u, task %p, cmnd %p, cdb 0x%x\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task, task->scsi_cmnd,
			   task->scsi_cmnd->cmnd[0]);
		else
		    printk("iSCSI: session %p abort rejected (0x%x) for mgmt "
			   "%u, itt %u, task %p, cmnd NULL\n",
			   session, ststmrh->response, ntohl(ststmrh->itt),
			   task->itt, task);

		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_ABORT_TASK_SET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    }
	} else if (test_bit(TASK_TRY_ABORT_TASK_SET, &task->flags)) {
	    ISCSI_TRACE(ISCSI_TRACE_RxAbortTaskSet,
			task ? task->scsi_cmnd : NULL, task, mgmt_itt,
			ststmrh->response);
	    if (session->ignore_abort_task_sets && ((session->ignore_lun < 0)
						    || (session->ignore_lun ==
							task->lun))) {
		session->ignore_abort_task_sets--;
		printk("iSCSI: session %p ignoring abort task set response "
		       "0x%x for mgmt %u, itt %u, task %p, cmnd %p, at %lu\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd, jiffies);
		ignored = 1;
	    } else if (session->reject_abort_task_sets
		       && ((session->reject_lun < 0)
			   || (session->reject_lun == task->lun))) {
		session->reject_abort_task_sets--;
		printk("iSCSI: session %p treating abort task set response "
		       "0x%x as reject for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_LUN_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (ststmrh->response == 0) {
		iscsi_task_t *t;
		printk("iSCSI: session %p abort task set success for "
		       "mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ntohl(ststmrh->itt), task->itt, task,
		       task->scsi_cmnd);
		/* all tasks to this LUN have been recovered */
		for (t = session->arrival_order.head; t; t = t->order_next) {
		    if (task->lun == t->lun)
			t->flags &= ~TASK_RECOVERY_MASK;
		}
		task->flags &= ~TASK_RECOVERY_MASK;
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else {
		printk("iSCSI: session %p abort task set rejected (0x%x) "
		       "for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_LUN_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    }
	} else if (test_bit(TASK_TRY_LUN_RESET, &task->flags)) {
	    ISCSI_TRACE(ISCSI_TRACE_RxLunReset, task ? task->scsi_cmnd : NULL,
			task, mgmt_itt, ststmrh->response);
	    if (session->ignore_lun_resets && ((session->ignore_lun < 0)
					       || (session->ignore_lun ==
						   task->lun))) {
		session->ignore_lun_resets--;
		ignored = 1;
		printk("iSCSI: session %p ignoring LUN reset response 0x%x "
		       "for mgmt %u, itt %u, task %p, cmnd %p at %lu\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd, jiffies);
	    } else if (session->reject_lun_resets && ((session->reject_lun < 0)
						      || (session->reject_lun ==
							  task->lun))) {
		session->reject_lun_resets--;
		printk("iSCSI: session %p treating LUN reset response 0x%x "
		       "as reject for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_WARM_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (ststmrh->response == 0) {
		iscsi_task_t *t;
		printk("iSCSI: session %p LUN reset success for mgmt %u, "
		       "itt %u, task %p, cmnd %p\n",
		       session, ntohl(ststmrh->itt), task->itt, task,
		       task->scsi_cmnd);

		/* tell all devices attached to this LUN that a reset occured */
		lun_reset_occured(session, task->lun);

		/* all tasks to this LUN have been recovered */
		for (t = session->arrival_order.head; t; t = t->order_next) {
		    if (task->lun == t->lun) {
			printk("iSCSI: session %p LUN reset success recovering "
			       "itt %u, task %p, cmnd %p\n",
			       session, t->itt, t, t->scsi_cmnd);
			t->flags &= ~TASK_RECOVERY_MASK;
		    }
		}
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else {
		printk("iSCSI: session %p LUN reset rejected (0x%x) for mgmt "
		       "%u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_WARM_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    }
	} else if (test_bit(TASK_TRY_WARM_RESET, &task->flags)) {
	    ISCSI_TRACE(ISCSI_TRACE_RxWarmReset, task ? task->scsi_cmnd : NULL,
			task, mgmt_itt, ststmrh->response);
	    if (session->ignore_warm_resets && ((session->ignore_lun < 0)
						|| (session->ignore_lun ==
						    task->lun))) {
		session->ignore_warm_resets--;
		printk("iSCSI: session %p ignoring warm reset response 0x%x "
		       "for mgmt %u, itt %u, task %p, cmnd %p at %lu\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd, jiffies);
		ignored = 1;
	    } else if (session->reject_warm_resets && ((session->reject_lun < 0)
						       || (session->
							   reject_lun ==
							   task->lun))) {
		session->reject_warm_resets--;
		printk("iSCSI: session %p treating warm reset response 0x%x "
		       "as reject for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_COLD_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (ststmrh->response == 0) {
		iscsi_task_t *t;
		printk("iSCSI: session %p warm target reset success for mgmt "
		       "%u, itt %u, task %p, cmnd %p\n",
		       session, ntohl(ststmrh->itt), task->itt, task,
		       task->scsi_cmnd);

		/* tell all devices attached to this target
		 * that a reset occured 
		 */
		target_reset_occured(session);

		/* mark all tasks recovered */
		for (t = session->arrival_order.head; t; t = t->order_next) {
		    printk("iSCSI: session %p warm target reset success "
			   "recovering itt %u, task %p, cmnd %p\n",
			   session, t->itt, t, t->scsi_cmnd);
		    t->flags &= ~TASK_RECOVERY_MASK;
		}

		/* and recover them */
		set_bit(SESSION_RESET, &session->control_bits);
		smp_mb();
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else {
		printk("iSCSI: session %p warm target reset rejected (0x%x) "
		       "for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_COLD_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    }
	} else if (test_bit(TASK_TRY_COLD_RESET, &task->flags)) {
	    /* we probably won't ever get a task mgmt
	     * response for a cold reset that works, since
	     * the target should drop the session as part of
	     * the reset.
	     */
	    ISCSI_TRACE(ISCSI_TRACE_RxColdReset, task ? task->scsi_cmnd : NULL,
			task, mgmt_itt, ststmrh->response);
	    task->flags &= ~TASK_RECOVERY_MASK;
	    if (session->ignore_cold_resets && ((session->ignore_lun < 0)
						|| (session->ignore_lun ==
						    task->lun))) {
		session->ignore_cold_resets--;
		printk("iSCSI: session %p ignoring cold reset response "
		       "0x%x for mgmt %u, itt %u, task %p, cmnd %p at %lu\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd, jiffies);
		ignored = 1;
	    } else if (session->reject_cold_resets && ((session->reject_lun < 0)
						       || (session->
							   reject_lun ==
							   task->lun))) {
		session->reject_cold_resets--;
		printk("iSCSI: session %p treating cold reset response 0x%x "
		       "as reject for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		task->flags &= ~TASK_RECOVERY_MASK;
		__set_bit(TASK_TRY_COLD_RESET, &task->flags);
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else if (ststmrh->response == 0) {
		iscsi_task_t *t;

		printk("iSCSI: session %p cold target reset success for "
		       "mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ntohl(ststmrh->itt), task->itt, task,
		       task->scsi_cmnd);

		/* mark all tasks recovered */
		for (t = session->arrival_order.head; t; t = t->order_next) {
		    printk("iSCSI: session %p cold target reset success "
			   "recovering itt %u, task %p, cmnd %p\n",
			   session, t->itt, t, t->scsi_cmnd);
		    t->flags &= ~TASK_RECOVERY_MASK;
		}

		/* clear any requested reset, since we just did one */
		session->warm_reset_itt = RSVD_TASK_TAG;
		clear_bit(SESSION_RESET_REQUESTED, &session->control_bits);
		/* and recover all the tasks */
		set_bit(SESSION_RESET, &session->control_bits);
		smp_mb();
		wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	    } else {
		printk("iSCSI: session %p cold target reset rejected (0x%x) "
		       "for mgmt %u, itt %u, task %p, cmnd %p\n",
		       session, ststmrh->response, ntohl(ststmrh->itt),
		       task->itt, task, task->scsi_cmnd);
		/* nothing left to try, just drop the
		 * session and hope the target clears the
		 * problem 
		 */
		iscsi_drop_session(session);
	    }
	}
    } else if (mgmt_itt == session->warm_reset_itt) {
	/* response to a requested reset */
	if (session->ignore_warm_resets && ((session->ignore_lun < 0)
					    || (session->ignore_lun ==
						task->lun))) {
	    session->ignore_warm_resets--;
	    printk("iSCSI: session %p ignoring warm reset response 0x%x "
		   "for mgmt %u at %lu\n",
		   session, ststmrh->response, mgmt_itt, jiffies);
	    ignored = 1;
	} else if (session->reject_warm_resets && ((session->reject_lun < 0)
						   || (session->reject_lun ==
						       task->lun))) {
	    session->reject_warm_resets--;
	    printk("iSCSI: session %p ignoring warm reset response 0x%x for "
		   "mgmt %u at %lu\n",
		   session, ststmrh->response, mgmt_itt, jiffies);

	    session->warm_reset_itt = RSVD_TASK_TAG;
	    clear_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	    smp_mb();
	} else if (ststmrh->response == 0) {
	    iscsi_task_t *t;
	    printk("iSCSI: session %p warm target reset success for mgmt %u "
		   "at %lu\n", session, mgmt_itt, jiffies);

	    session->warm_reset_itt = RSVD_TASK_TAG;
	    clear_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	    smp_mb();

	    /* tell all devices attached to this target that a reset occured */
	    target_reset_occured(session);

	    /* mark all tasks recovered */
	    for (t = session->arrival_order.head; t; t = t->order_next) {
		printk("iSCSI: session %p warm target reset killed itt %u, "
		       "task %p, cmnd %p\n", session, t->itt, t, t->scsi_cmnd);
		t->flags &= ~TASK_RECOVERY_MASK;
	    }

	    /* and recovery them */
	    set_bit(SESSION_RESET, &session->control_bits);
	    smp_mb();
	    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
	} else {
	    /* didn't work.  just give up */
	    session->warm_reset_itt = RSVD_TASK_TAG;
	    clear_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	    smp_mb();

	    printk("iSCSI: session %p warm target reset rejected (0x%x) for "
		   "mgmt %u at %lu\n",
		   session, ststmrh->response, mgmt_itt, jiffies);
	}
    } else {
	printk("iSCSI: session %p mgmt response 0x%x for unknown itt %u, "
	       "rtt %u\n",
	       session, ststmrh->response, ntohl(ststmrh->itt),
	       ntohl(ststmrh->rtt));
    }

    if (!ignored && (session->mgmt_itt == mgmt_itt)) {
	/* we got the expected response, allow the tx thread
	 * to send another task mgmt PDU whenever it wants
	 * to 
	 */
	session->mgmt_itt = RSVD_TASK_TAG;
	session->task_mgmt_response_deadline = 0;
	smp_mb();
    }

    spin_unlock(&session->task_lock);
}

void
retry_immediate_mgmt_pdus(unsigned long arg)
{
    iscsi_session_t *session = (iscsi_session_t *) arg;

    session->immediate_reject_timer.expires = 0;
    smp_mb();
    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
}

static void
iscsi_recv_reject(iscsi_session_t * session, struct IscsiRejectRspHdr *reject,
		  unsigned char *xbuf)
{
    int dlength = ntoh24(reject->dlength);
    uint32_t itt = 0;
    iscsi_task_t *task = NULL;
    struct IscsiHdr pdu;

    /* FIXME: check StatSN */
    session->ExpStatSn = ntohl(reject->statsn) + 1;
    updateSN(session, ntohl(reject->expcmdsn), ntohl(reject->maxcmdsn));
    /* assume a PDU round-trip, connection is ok */
    session->last_rx = jiffies;
    smp_mb();

    if (reject->reason == REJECT_REASON_DATA_DIGEST_ERROR) {
	/* we don't need to do anything about these, timers
	 * or other PDUs will handle the problem 
	 */
	if (dlength >= sizeof (pdu)) {
	    memcpy(&pdu, xbuf, sizeof (pdu));
	    itt = ntohl(pdu.itt);
	    printk("iSCSI: session %p itt %u (opcode 0x%x) rejected because "
		   "of a DataDigest error at %lu\n",
		   session, itt, pdu.opcode, jiffies);
	} else {
	    printk("iSCSI: session %p target rejected a PDU because of a "
		   "DataDigest error at %lu\n", session, jiffies);
	}
    } else if (reject->reason == REJECT_REASON_IMM_CMD_REJECT) {
	if (dlength >= sizeof (pdu)) {
	    /* look at the rejected PDU */
	    memcpy(&pdu, xbuf, sizeof (pdu));
	    itt = ntohl(pdu.itt);

	    /* try to find the task corresponding to this
	     * itt, and wake up any process waiting on it 
	     */
	    spin_lock(&session->task_lock);

	    if (session->mgmt_itt == itt)
		session->mgmt_itt = RSVD_TASK_TAG;

	    if ((task = find_session_mgmt_task(session, itt))) {
		if (task->scsi_cmnd)
		    DEBUG_EH("iSCSI: session %p task mgmt PDU rejected, "
			     "mgmt %u, task %p, itt %u, cmnd %p, cdb 0x%x\n",
			     session, itt, task, task->itt, task->scsi_cmnd,
			     task->scsi_cmnd->cmnd[0]);
		else
		    DEBUG_EH("iSCSI: session %p task mgmt PDU rejected, "
			     "mgmt %u, task %p, itt %u, cmnd NULL\n",
			     session, itt, task, task->itt);

		if (session->immediate_reject_timer.expires == 0) {
		    session->immediate_reject_timer.expires =
			jiffies + MSECS_TO_JIFFIES(40);
		    session->immediate_reject_timer.data =
			(unsigned long) session;
		    session->immediate_reject_timer.function =
			retry_immediate_mgmt_pdus;
		    DEBUG_EH("iSCSI: session %p scheduling task mgmt %u "
			     "retry for %lu at %lu\n",
			     session, itt, session->busy_task_timer.expires,
			     jiffies);
		    del_timer_sync(&session->busy_task_timer);	/* make sure 
								 * it's not 
								 * running now 
								 */
		    add_timer(&session->immediate_reject_timer);
		}
	    } else if ((pdu.opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGOUT_CMD) {
		/* our Logout was rejected.  just let the
		 * logout response timer drop the session 
		 */
		printk("iSCSI: session %p logout PDU rejected, itt %u\n",
		       session, itt);
		session->logout_itt = RSVD_TASK_TAG;
		smp_mb();
	    } else {
		printk("iSCSI: session %p, itt %u immediate command rejected "
		       "at %lu\n", session, itt, jiffies);
	    }
	    spin_unlock(&session->task_lock);
	} else {
	    printk("iSCSI: session %p, immediate command rejected at %lu, "
		   "dlength %u\n", session, jiffies, dlength);
	}
    } else {
	if (dlength >= sizeof (pdu)) {
	    /* look at the rejected PDU */
	    memcpy(&pdu, xbuf, sizeof (pdu));
	    itt = ntohl(pdu.itt);
	    printk("iSCSI: dropping session %p because target rejected a PDU, "
		   "reason 0x%x, dlength %d, rejected itt %u, opcode 0x%x\n",
		   session, reject->reason, dlength, itt, pdu.opcode);
	} else {
	    printk("iSCSI: dropping session %p because target rejected a PDU, "
		   "reason 0x%x, dlength %u\n", session, reject->reason,
		   dlength);
	}
	iscsi_drop_session(session);
    }
}

static int
iscsi_lun_thread(void *vtaskp)
{
    iscsi_session_t *session;
    int rc = -1;
    int lun = 0;

    session = (iscsi_session_t *) vtaskp;

    printk("iSCSI: session %p lun thread %d about to daemonize on cpu%d\n",
	   session, current->pid, smp_processor_id());

    /* become a daemon kernel thread */
    sprintf(current->comm, "iscsi-lun-thr");
    iscsi_daemonize();
    current->flags |= PF_MEMALLOC;
    smp_mb();

    /* Block all signals except SIGHUP and SIGKILL */
    LOCK_SIGNALS();
    siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGHUP));
    RECALC_PENDING_SIGNALS;
    UNLOCK_SIGNALS();

    printk("iSCSI: session %p lun thread %d starting on cpu%d\n", session,
	   current->pid, smp_processor_id());

    if (test_and_set_bit(SESSION_PROBING_LUNS, &session->control_bits)) {
	printk("iSCSI: session %p already has a process probing or waiting to "
	       "probe LUNs for bus %d, target %d\n",
	       session, session->iscsi_bus, session->target_id);
	rc = -EBUSY;
	goto done;
    }
    iscsi_detect_luns(session);
    for (lun = 0; lun < ISCSI_MAX_LUN; lun++) {
	if (test_bit(lun, session->luns_detected)) {
	    /* These are the original luns present */
	    if (!test_bit(lun, session->luns_found)) {
		/* the lun seems to have changed */
		iscsi_remove_lun(session, lun);
	    }
	}
    }

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	printk("iSCSI: session %p terminating, returning at %lu\n", session,
	       jiffies);
	clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
	smp_mb();
	goto done;
    } else if (signal_pending(current)) {
	iscsi_terminate_session(session);
	printk("iSCSI: session %p ioctl terminated, returning at %lu\n",
	       session, jiffies);
	clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
	smp_mb();
	goto done;
    }

    iscsi_probe_luns(session, session->luns_allowed);

    /* and then we're done */
    clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
    smp_mb();
    rc = 0;

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	printk("iSCSI: session %p terminating, ioctl returning at %lu\n",
	       session, jiffies);
    } else if (signal_pending(current)) {
	iscsi_terminate_session(session);
	printk("iSCSI: session %p ioctl terminated, returning at %lu\n",
	       session, jiffies);
    }

  done:			/* lun change event finished */
    DEBUG_INIT("iSCSI: lun thread leaving kernel at %lu\n", jiffies);
    set_current_state(TASK_RUNNING);
    drop_reference(session);
    smp_mb();
    return rc;
}

static void
iscsi_recv_async_event(iscsi_session_t * session,
		       struct IscsiAsyncEvtHdr *staeh, unsigned char *xbuf)
{
    unsigned int senselen; 

    /* FIXME: check StatSN */
    session->ExpStatSn = ntohl(staeh->statsn) + 1;
    updateSN(session, ntohl(staeh->expcmdsn), ntohl(staeh->maxcmdsn));

    ISCSI_TRACE(ISCSI_TRACE_RxAsyncEvent, NULL, NULL, staeh->async_event,
		staeh->async_vcode);

    switch (staeh->async_event) {
    case ASYNC_EVENT_SCSI_EVENT:
	senselen = (xbuf[0] << 8) | xbuf[1];
	xbuf += 2;
	printk(" iSCSI: SCSI Async event ASC=%0x2x, ASCQ=%0x2x received on "
	       "session %p for target %s\n", ASC(xbuf), ASCQ(xbuf), session,
	       session->log_name);

	if (ASC(xbuf) == 0x3f && ASCQ(xbuf) == 0x0e) {
	    atomic_inc(&session->refcount);
	    /* Lun change event has occured for a target */
	    if (kernel_thread(iscsi_lun_thread, (void *) session, 0) < 0) {
		printk("iSCSI: failed to start the thread \n");
		atomic_dec(&session->refcount);
	    }
	}

	/* no way to pass this up to the SCSI layer, since
	 * there is no command associated with it 
	 */
	if (LOG_ENABLED(ISCSI_LOG_SENSE)) {
	    if (senselen >= 26) {
		printk("iSCSI: SCSI Async event, senselen %d, key %02x, "
		       "ASC/ASCQ %02X/%02X, session %p to %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n", senselen,
		       SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf), session,
		       session->log_name, xbuf[0], xbuf[1], xbuf[2], xbuf[3],
		       xbuf[4], xbuf[5], xbuf[6], xbuf[7], xbuf[8], xbuf[9],
		       xbuf[10], xbuf[11], xbuf[12], xbuf[13], xbuf[14],
		       xbuf[15], xbuf[16], xbuf[17], xbuf[18], xbuf[19],
		       xbuf[20], xbuf[21], xbuf[22], xbuf[23], xbuf[24],
		       xbuf[25]);
	    } else if (senselen >= 18) {
		printk("iSCSI: SCSI Async event, senselen %d, key %02x, "
		       "ASC/ASCQ %02X/%02X, session %p to %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x\n",
		       senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf),
		       session, session->log_name, xbuf[0], xbuf[1], xbuf[2],
		       xbuf[3], xbuf[4], xbuf[5], xbuf[6], xbuf[7], xbuf[8],
		       xbuf[9], xbuf[10], xbuf[11], xbuf[12], xbuf[13],
		       xbuf[14], xbuf[15], xbuf[16], xbuf[17]);
	    } else if (senselen >= 14) {
		printk("iSCSI: SCSI Async event, senselen %d, key %02x, "
		       "ASC/ASCQ %02X/%02X, session %p to %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x "
		       "%02x%02x%02x%02x %02x%02x\n",
		       senselen, SENSE_KEY(xbuf), ASC(xbuf), ASCQ(xbuf),
		       session, session->log_name, xbuf[0], xbuf[1], xbuf[2],
		       xbuf[3], xbuf[4], xbuf[5], xbuf[6], xbuf[7], xbuf[8],
		       xbuf[9], xbuf[10], xbuf[11], xbuf[12], xbuf[13]);
	    } else {
		printk("iSCSI: SCSI Async event, senselen %d, key %02x, "
		       "session %p to %s\n"
		       "iSCSI: Sense %02x%02x%02x%02x %02x%02x%02x%02x\n",
		       senselen, SENSE_KEY(xbuf), session, session->log_name,
		       xbuf[0], xbuf[1], xbuf[2], xbuf[3], xbuf[4], xbuf[5],
		       xbuf[6], xbuf[7]);
	    }
	}
	break;
    case ASYNC_EVENT_REQUEST_LOGOUT:
	printk("iSCSI: target requests logout within %u seconds for session "
	       "to %s\n", ntohs(staeh->param3), session->log_name);
	/* FIXME: this is really a request to drop a
	 * connection, not the whole session, but we
	 * currently only have one connection per session,
	 * so there's no difference at the moment.
	 */

	/* we need to get the task lock to make sure the TX
	 * thread isn't in the middle of adding another task
	 * to the session.
	 */
	spin_lock(&session->task_lock);
	iscsi_request_logout(session, ntohs(staeh->param3) - (HZ / 10),
			     session->active_timeout);
	spin_unlock(&session->task_lock);
	break;
    case ASYNC_EVENT_DROPPING_CONNECTION:
	printk("iSCSI: session %p target dropping connection %u, reconnect "
	       "min %u max %u\n",
	       session, ntohs(staeh->param1), ntohs(staeh->param2),
	       ntohs(staeh->param3));
	session->time2wait = (long) ntohs(staeh->param2) & 0x0000FFFFFL;
	smp_mb();
	break;
    case ASYNC_EVENT_DROPPING_ALL_CONNECTIONS:
	printk("iSCSI: session %p target dropping all connections, reconnect "
	       "min %u max %u\n",
	       session->log_name, ntohs(staeh->param2), ntohs(staeh->param3));
	session->time2wait = (long) ntohs(staeh->param2) & 0x0000FFFFFL;
	smp_mb();
	break;
    case ASYNC_EVENT_VENDOR_SPECIFIC:
	printk("iSCSI: session %p ignoring vendor-specific async event, "
	       "vcode 0x%x\n", session, staeh->async_vcode);
	break;
    case ASYNC_EVENT_PARAM_NEGOTIATION:
	printk("iSCSI: session %p received async event param negotiation, "
	       "dropping session\n", session);
	iscsi_drop_session(session);
	break;
    default:
	printk("iSCSI: session %p received unknown async event 0x%x at %lu\n",
	       session, staeh->async_event, jiffies);
	break;
    }
    if (staeh->async_event == ASYNC_EVENT_DROPPING_CONNECTION ||
	staeh->async_event == ASYNC_EVENT_DROPPING_ALL_CONNECTIONS ||
	staeh->async_event == ASYNC_EVENT_REQUEST_LOGOUT) {
	spin_lock(&session->portal_lock);
	session->ip_length =
	    session->portals[session->current_portal].ip_length;
	memcpy(session->ip_address,
	       session->portals[session->current_portal].ip_address,
	       session->portals[session->current_portal].ip_length);

	spin_unlock(&session->portal_lock);
    }
}

/* wait for the tx thread to block or exit, ignoring signals.
 * the rx thread needs to know that the tx thread is not running before
 * it can safely close the socket and start a new login phase on a new socket,
 * Also, tasks still in use by the tx thread can't safely be completed on
 * a session drop.
 */
static int
wait_for_tx_blocked(iscsi_session_t * session)
{
    while (session->tx_pid) {
	DEBUG_INIT("iSCSI: session %p thread %d waiting for tx thread %d to "
		   "block\n", session, current->pid, session->tx_pid);

	wait_event_interruptible(session->tx_blocked_wait_q,
				 test_bit(TX_THREAD_BLOCKED,
					  &session->control_bits));

	if (iscsi_handle_signals(session)) {
	    DEBUG_INIT("iSCSI: session %p wait_for_tx_blocked signalled "
		       "at %lu while waiting for tx %d\n",
		       session, jiffies, session->tx_pid);
	}
	/* if the session is terminating, the tx thread will
	 * exit, waking us up in the process we don't want
	 * to return until the tx thread is blocked, since
	 * there's not much the rx thread can do until the
	 * tx thread is guaranteed not to be doing anything.
	 */
	if (test_bit(TX_THREAD_BLOCKED, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p rx thread %d found tx thread "
		       "%d blocked\n", session, current->pid, session->tx_pid);
	    return 1;
	}
    }

    /* dead and blocked are fairly similar, really */
    DEBUG_INIT("iSCSI: session %p rx thread %d found tx thread %d exited\n",
	       session, current->pid, session->tx_pid);
    return 1;
}

/* Wait for a session to be established.  
 * Returns 1 if the session is established, zero if the timeout expires
 * or the session is terminating/has already terminated.
 */
static int
wait_for_session(iscsi_session_t * session, int use_timeout)
{
    int ret = 0;
    wait_queue_t waitq;

    if (test_bit(SESSION_ESTABLISHED, &session->control_bits))
	return 1;

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	printk("iSCSI: session %p terminating, wait_for_session failed\n",
	       session);
	return 0;
    }

    init_waitqueue_entry(&waitq, current);
    add_wait_queue(&session->login_wait_q, &waitq);
    smp_mb();

    DEBUG_INIT("iSCSI: pid %d waiting for session %p at %lu\n", current->pid,
	       session, jiffies);

    for (;;) {
	set_current_state(TASK_INTERRUPTIBLE);

	if (test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	    ret = 1;
	    goto done;
	}

	if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    ret = 0;
	    goto done;
	}

	if (signal_pending(current)) {
	    ret = 0;
	    goto done;
	}

	if (use_timeout && session->replacement_timeout) {
	    unsigned long timeout, now;
	    long sleep_jiffies = 0;

	    if (test_bit(SESSION_REPLACEMENT_TIMEDOUT, &session->control_bits)) {
		ret = 0;
		goto done;
	    }

	    if (session->session_drop_time)
		timeout =
		    session->session_drop_time +
		    (HZ * session->replacement_timeout);
	    else
		timeout = jiffies + (HZ * session->replacement_timeout);

	    if (time_before_eq(timeout, jiffies)) {
		printk("iSCSI: pid %d timed out in wait_for_session %p\n",
		       current->pid, session);
		ret = 0;
		goto done;
	    }

	    /* handle wrap-around */
	    now = jiffies;
	    if (now < timeout)
		sleep_jiffies = timeout - now;
	    else
		sleep_jiffies = ULONG_MAX - now + timeout;

	    schedule_timeout(sleep_jiffies);
	} else
	    schedule();
    }

  done:
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&session->login_wait_q, &waitq);

    if (ret == 0)
	printk("iSCSI: wait_for_session %p failed\n", session);

    return ret;
}

/* caller must hold the session's portal_lock */
static unsigned int
find_portal(iscsi_session_t * session, unsigned char *ip_address, int ip_length,
	    int port)
{
    iscsi_portal_info_t *portals = session->portals;
    unsigned int p;

    for (p = 0; p < session->num_portals; p++) {
	if (portals[p].ip_length == 0)
	    continue;

	if (portals[p].ip_length != ip_length)
	    continue;

	if (portals[p].port != port)
	    continue;

	if (memcmp(portals[p].ip_address, ip_address, ip_length))
	    continue;

	DEBUG_INIT("iSCSI: session %p found portal %u\n", session, p);
	break;
    }

    if (p < session->num_portals)
	return p;

    return UINT_MAX;
}

static void
set_portal_config(iscsi_session_t * session, unsigned int p)
{
    /* Set the session timeouts and iSCSI op params based on
     * the portal's settings.  Don't change the address,
     * since a termporary redirect may have already changed
     * the address, and we want to use the redirected
     * address rather than the portal's address.
     */
    session->login_timeout = session->portals[p].login_timeout;
    session->auth_timeout = session->portals[p].auth_timeout;
    session->active_timeout = session->portals[p].active_timeout;
    session->idle_timeout = session->portals[p].idle_timeout;
    session->ping_timeout = session->portals[p].ping_timeout;
    session->abort_timeout = session->portals[p].abort_timeout;
    session->reset_timeout = session->portals[p].reset_timeout;
    session->replacement_timeout = session->portals[p].replacement_timeout;

    /* FIXME: get the scsi_cmnd_lock when setting these? */
    session->disk_command_timeout = session->portals[p].disk_command_timeout;

    session->InitialR2T = session->portals[p].InitialR2T;
    session->ImmediateData = session->portals[p].ImmediateData;
    session->MaxRecvDataSegmentLength =
	session->portals[p].MaxRecvDataSegmentLength;
    session->FirstBurstLength = session->portals[p].FirstBurstLength;
    session->MaxBurstLength = session->portals[p].MaxBurstLength;
    session->DefaultTime2Wait = session->portals[p].DefaultTime2Wait;
    session->DefaultTime2Retain = session->portals[p].DefaultTime2Retain;

    session->HeaderDigest = session->portals[p].HeaderDigest;
    session->DataDigest = session->portals[p].DataDigest;

    session->portal_group_tag = session->portals[p].tag;

    /* TCP options */
    session->tcp_window_size = session->portals[p].tcp_window_size;
    /* FIXME: type_of_service */
}

/* caller must hold the session's portal_lock */
static int
set_portal(iscsi_session_t * session, unsigned int p)
{
    iscsi_portal_info_t *portals = session->portals;

    if (portals == NULL) {
	printk("iSCSI: session %p has no portal info, can't set portal %d\n",
	       session, p);
	return 0;
    }

    if (p >= session->num_portals) {
	printk("iSCSI: session %p has only %d portals, can't set portal %d\n",
	       session, session->num_portals, p);
	return 0;
    }

    session->current_portal = p;

    /* address */
    session->ip_length = portals[p].ip_length;
    memcpy(session->ip_address, portals[p].ip_address, portals[p].ip_length);
    session->port = portals[p].port;

    /* timeouts, operational params, other settings */
    set_portal_config(session, p);

    DEBUG_INIT("iSCSI: session %p set to portal %d, group %d\n",
	       session, session->current_portal, session->portal_group_tag);

    return 1;
}

static void
set_preferred_subnet_bitmap(iscsi_session_t * session)
{
    unsigned int bitmap = 0;
    iscsi_portal_info_t *portals = session->portals;
    unsigned char ip[16];
    int ip_length = 4;
    unsigned int p;
    uint32_t a1, a2;

    if (portals == NULL) {
	printk("iSCSI: session %p has no portal info, therefore no "
	       "preferred subnet bitmap\n", session);
	return;
    }

    iscsi_inet_aton(session->preferred_subnet, ip, &ip_length);

    a1 = ip[0] << 24;
    a1 |= ip[1] << 16;
    a1 |= ip[2] << 8;
    a1 |= ip[3];
    a1 &= session->preferred_subnet_mask;

    for (p = 0; p < session->num_portals; p++) {
	a2 = portals[p].ip_address[0] << 24;
	a2 |= portals[p].ip_address[1] << 16;
	a2 |= portals[p].ip_address[2] << 8;
	a2 |= portals[p].ip_address[3];
	a2 &= session->preferred_subnet_mask;

	if (a1 == a2)
	    bitmap = bitmap | (1 << (p % MAX_PORTALS));
    }
    session->preferred_subnet_bitmap = bitmap;
}

static void
set_preferred_portal_bitmap(iscsi_session_t * session)
{
    unsigned int bitmap = 0;
    iscsi_portal_info_t *portals = session->portals;
    unsigned char ip[16];
    int ip_length = 4;
    unsigned int p;

    if (portals == NULL) {
	printk("iSCSI: session %p has no portal info, therefore no "
	       "preferred portal bitmap\n", session);
	return;
    }

    iscsi_inet_aton(session->preferred_portal, ip, &ip_length);

    for (p = 0; p < session->num_portals; p++) {
	if (memcmp(ip, portals[p].ip_address, portals[p].ip_length) == 0) {
	    bitmap = bitmap | (1 << (p % MAX_PORTALS));
	    break;
	}
    }
    session->preferred_portal_bitmap = bitmap;
}

static int
get_appropriate_portal(iscsi_session_t * session)
{
    unsigned int p;
    int pp = -1;
    unsigned int portal_bitmap = session->preferred_portal_bitmap;
    unsigned int subnet_bitmap = session->preferred_subnet_bitmap;

    if (!portal_bitmap && !subnet_bitmap)
	return -1;

    for (p = 0; p < session->num_portals; p++) {
	if (portal_bitmap & (1 << (p % MAX_PORTALS))) {
	    pp = p;
	    break;
	}
    }

    if (pp < 0) {
	for (p = 0; p < session->num_portals; p++) {
	    if (subnet_bitmap & (1 << (p % MAX_PORTALS))) {
		pp = p;
		break;
	    }
	}
    }
    return pp;
}

/* caller must hold the session's portal_lock */
static void
next_portal(iscsi_session_t * session)
{
    unsigned int desired_portal = UINT_MAX;
    int allow_any_tag = 1;
    int current_tag = session->portal_group_tag;

    if (!allow_any_tag && (session->portal_group_tag < 0)) {
	printk("iSCSI: session %p current portal %u group tag unknown, "
	       "can't switch portals\n", session, session->current_portal);
	set_portal(session, session->current_portal);
	return;
    }

    /* requested portals and fallbacks after requested
     * portals are handled similarly 
     */
    if (session->requested_portal != UINT_MAX) {
	DEBUG_INIT("iSCSI: session %p requested to switch to portal %u\n",
		   session, session->requested_portal);
	desired_portal = session->requested_portal;
	session->requested_portal = UINT_MAX;
    } else if (session->fallback_portal != UINT_MAX) {
	DEBUG_INIT("iSCSI: session %p falling back to portal %u\n", session,
		   session->fallback_portal);
	desired_portal = session->fallback_portal;
	session->fallback_portal = UINT_MAX;
    }

    if (desired_portal != UINT_MAX) {
	/* a particular portal has been requested */
	if (desired_portal >= session->num_portals) {
	    /* the portal doesn't exist */
	    printk("iSCSI: session %p desired portal %u does not exist, "
		   "staying with portal %u\n",
		   session, desired_portal, session->current_portal);
	    /* don't reset the address, so that we stay
	     * wherever we are if we can't switch portals 
	     */
	    set_portal_config(session, session->current_portal);
	} else if (session->portals[desired_portal].ip_length == 0) {
	    /* the requested portal is dead (probably killed
	     * by a permanent redirect) 
	     */
	    printk("iSCSI: session %p desireed portal %u is dead, "
		   "staying with portal %u\n",
		   session, desired_portal, session->current_portal);
	    /* don't reset the address, so that we stay
	     * wherever we are if we can't switch portals 
	     */
	    set_portal_config(session, session->current_portal);
	} else if (!allow_any_tag
		   && (session->portals[desired_portal].tag !=
		       session->portal_group_tag)) {
	    /* the requested portal is in the wrong portal group */
	    printk("iSCSI: session %p desired portal %u is in portal "
		   "group %u, but portal group %u is required, staying "
		   "with portal %u\n",
		   session, desired_portal,
		   session->portals[desired_portal].tag,
		   session->portal_group_tag, session->current_portal);
	    /* don't reset the address, so that we stay
	     * wherever we are if we can't switch portals 
	     */
	    set_portal_config(session, session->current_portal);
	} else {
	    /* try the requested portal */
	    session->current_portal = desired_portal;
	    set_portal(session, session->current_portal);
	}
    } else if (session->portal_failover) {
	unsigned int p;
	int failed = 1;
	unsigned int bitmap = 0;
	unsigned int num_portals = session->num_portals;

	/* Look for the preferred portal */
	bitmap = session->preferred_portal_bitmap;
	if (bitmap) {
	    for (p = 0; p < num_portals; p++) {
		if (bitmap & (1 << (p % MAX_PORTALS))) {
		    if (!(session->tried_portal_bitmap &
			  (1 << (p % MAX_PORTALS)))) {
			if (session->portals[p].ip_length == 0) {
			    /* this portal is dead (probably
			     * killed by a permanent
			     * redirect) 
			     */
			    DEBUG_INIT("iSCSI: session %p skipping dead "
				       "portal %u\n", session, p);
			} else if (allow_any_tag) {
			    /* we can use any portal group,
			     * so a tag mismatch isn't a
			     * problem 
			     */
			    session->current_portal = p;
			    session->tried_portal_bitmap |=
				(1 << (p % MAX_PORTALS));
			    failed = 0;
			    break;
			} else if (session->portals[p].tag < 0) {
			    DEBUG_INIT("iSCSI: session %p skipping portal %u "
				       "group unknown, must login to group %u\n",
				       session, p, current_tag);
			} else if (session->portals[p].tag == current_tag) {
			    /* tag allowed, go ahead and try it */
			    session->current_portal = p;
			    session->tried_portal_bitmap |=
				(1 << (p % MAX_PORTALS));
			    failed = 0;
			    break;
			}
		    }
		}
	    }
	}

	if (failed) {
	    /* Look for the portal in the preferred subnet */
	    bitmap = session->preferred_subnet_bitmap;
	    if (bitmap) {
		for (p = 0; p < num_portals; p++) {
		    if (bitmap & (1 << (p % MAX_PORTALS))) {
			if (!(session->tried_portal_bitmap &
			      (1 << (p % MAX_PORTALS)))) {
			    if (session->portals[p].ip_length == 0) {
				/* this portal is dead
				 * (probably killed by a
				 * permanent redirect) 
				 */
				DEBUG_INIT("iSCSI: session %p skipping dead "
					   "portal %u\n", session, p);
			    } else if (allow_any_tag) {
				/* we can use any portal
				 * group, so a tag mismatch
				 * isn't a problem 
				 */
				session->current_portal = p;
				session->tried_portal_bitmap |=
				    (1 << (p % MAX_PORTALS));
				failed = 0;
				break;
			    } else if (session->portals[p].tag < 0) {
				DEBUG_INIT("iSCSI: session %p skipping portal "
					   "%u group unknown, must login to "
					   "group %u\n",
					   session, p, current_tag);
			    } else if (session->portals[p].tag == current_tag) {
				/* tag allowed, go ahead and try it */
				session->current_portal = p;
				session->tried_portal_bitmap |=
				    (1 << (p % MAX_PORTALS));
				failed = 0;
				break;
			    }
			}
		    }
		}
	    }
	}

	if (failed) {
	    /* Now, look for portal in the rest of the available portals */
	  retry:for (p = 0; p < num_portals; p++) {
		if (!(session->tried_portal_bitmap & (1 << (p % MAX_PORTALS)))) {
		    if (session->portals[p].ip_length == 0) {
			/* this portal is dead (probably
			 * killed by a permanent redirect) 
			 */
			DEBUG_INIT("iSCSI: session %p skipping dead "
				   "portal %u\n", session, p);
		    } else if (allow_any_tag) {
			/* we can use any portal group, so a
			 * tag mismatch isn't a problem 
			 */
			session->current_portal = p;
			session->tried_portal_bitmap |=
			    (1 << (p % MAX_PORTALS));
			failed = 0;
			break;
		    } else if (session->portals[p].tag < 0) {
			DEBUG_INIT("iSCSI: session %p skipping portal %u "
				   "group unknown, must login to group %u\n",
				   session, p, current_tag);
		    } else if (session->portals[p].tag == current_tag) {
			/* tag allowed, go ahead and try it */
			session->current_portal = p;
			session->tried_portal_bitmap |=
			    (1 << (p % MAX_PORTALS));
			failed = 0;
			break;
		    }
		}
	    }
	    if (failed) {
		/* We have exhausted all portals, will traverse the list
		 * again, and try if we can get any active portal to connect.
		 */
		session->tried_portal_bitmap = 0;
		logmsg(AS_NOTICE,
		       "iSCSI: session %p retrying all the portals again, "
		       "since the portal list got exhausted\n", session);
		goto retry;
	    }
	}

	/* set the portal, even if it hasn't changed, so that we
	 * replace the session's address and undo any temporary
	 * redirects.
	 */
	set_portal(session, session->current_portal);
    }
}

static int
iscsi_establish_session(iscsi_session_t * session)
{
    int ret = -1;
    uint8_t status_class;
    uint8_t status_detail;
    iscsi_login_status_t login_status = 0;

    spin_lock(&session->portal_lock);
    if (session->requested_portal != UINT_MAX) {
	/* request to change to a specific portal */
	next_portal(session);
    } else {
	/* Set almost everything based on the portal's
	 * settings.  Don't change the address, since a
	 * temporary redirect may have already changed the
	 * address, and we want to use the redirected
	 * address rather than the portal's address.
	 */
	set_portal_config(session, session->current_portal);
    }
    spin_unlock(&session->portal_lock);

    if (LOG_ENABLED(ISCSI_LOG_LOGIN) || LOG_ENABLED(ISCSI_LOG_INIT))
	printk("iSCSI: bus %d target %d trying to establish session %p "
	       "to portal %u, address %u.%u.%u.%u port %d group %d, rx %d, "
	       "tx %d at %lu\n",
	       session->iscsi_bus, session->target_id, session,
	       session->current_portal, session->ip_address[0],
	       session->ip_address[1], session->ip_address[2],
	       session->ip_address[3], session->port, session->portal_group_tag,
	       session->rx_pid, session->tx_pid, jiffies);
    else
	printk("iSCSI: bus %d target %d trying to establish session %p to "
	       "portal %u, address %u.%u.%u.%u port %d group %d\n",
	       session->iscsi_bus, session->target_id, session,
	       session->current_portal, session->ip_address[0],
	       session->ip_address[1], session->ip_address[2],
	       session->ip_address[3], session->port,
	       session->portal_group_tag);

    /* set a timer on the connect */
    if (session->login_timeout) {
	session->login_phase_timer = jiffies + (session->login_timeout * HZ);
	smp_mb();
    }
    if (LOG_ENABLED(ISCSI_LOG_LOGIN))
	printk("iSCSI: session %p attempting to connect at %lu, timeout at "
	       "%lu (%d seconds)\n",
	       session, jiffies, session->login_phase_timer,
	       session->login_timeout);

    if (!iscsi_connect(session)) {
	if (signal_pending(current))
	    printk("iSCSI: session %p connect timed out at %lu\n", session,
		   jiffies);
	else
	    printk("iSCSI: session %p connect failed at %lu\n", session,
		   jiffies);
	/* switch to the next portal */
	spin_lock(&session->portal_lock);
	next_portal(session);
	spin_unlock(&session->portal_lock);
	goto done;
    }

    /* We need to grab the config_mutex before we start trying to
     * login, to ensure update_session doesn't try to change the
     * per-session settings while the login code is using them.  Any
     * config updates will be deferred until after the login
     * completes.  We grab the mutex now, so that the connect timeout
     * will break us out if we can't get the mutex for some reason.
     */
    if (down_interruptible(&session->config_mutex)) {
	printk("iSCSI: session %p failed to acquire mutex before "
	       "login at %lu\n", session, jiffies);
	goto done;
    }

    /* make sure we have auth buffers for the login library to use */
    if (session->bidirectional_auth || session->username || session->password) {
	/* make sure we've allocated everything we need */
	if (session->auth_client_block == NULL) {
	    session->auth_client_block =
		kmalloc(sizeof (*session->auth_client_block), GFP_KERNEL);
	    if (session->auth_client_block)
		DEBUG_INIT("iSCSI: session %p allocated auth_client_block "
			   "%p (size %Zu) while establishing session\n",
			   session, session->auth_client_block,
			   sizeof (*session->auth_client_block));
	}
	if (session->auth_recv_string_block == NULL) {
	    session->auth_recv_string_block =
		kmalloc(sizeof (*session->auth_recv_string_block), GFP_KERNEL);
	    if (session->auth_recv_string_block)
		DEBUG_INIT("iSCSI: session %p allocated auth_recv_string_block "
			   "%p (size %Zu) while establishing session\n",
			   session, session->auth_recv_string_block,
			   sizeof (*session->auth_recv_string_block));
	}
	if (session->auth_send_string_block == NULL) {
	    session->auth_send_string_block =
		kmalloc(sizeof (*session->auth_send_string_block), GFP_KERNEL);
	    if (session->auth_send_string_block)
		DEBUG_INIT("iSCSI: session %p allocated auth_send_string_block "
			   "%p (size %Zu) while establishing session\n",
			   session, session->auth_send_string_block,
			   sizeof (*session->auth_send_string_block));
	}
	if (session->auth_recv_binary_block == NULL) {
	    session->auth_recv_binary_block =
		kmalloc(sizeof (*session->auth_recv_binary_block), GFP_KERNEL);
	    if (session->auth_recv_binary_block)
		DEBUG_INIT("iSCSI: session %p allocated auth_recv_binary_block "
			   "%p (size %Zu) while establishing session\n",
			   session, session->auth_recv_binary_block,
			   sizeof (*session->auth_recv_binary_block));
	}
	if (session->auth_send_binary_block == NULL) {
	    session->auth_send_binary_block =
		kmalloc(sizeof (*session->auth_send_binary_block), GFP_KERNEL);
	    if (session->auth_send_binary_block)
		DEBUG_INIT("iSCSI: session %p allocated auth_send_binary_block "
			   "%p (size %Zu) while establishing session\n",
			   session, session->auth_send_binary_block,
			   sizeof (*session->auth_send_binary_block));
	}

	/* if we have everything we need, setup the auth
	 * buffer descriptors for the login library 
	 */
	session->num_auth_buffers = 0;
	memset(&session->auth_buffers, 0, sizeof (session->auth_buffers));
	if (session->auth_client_block && session->auth_recv_string_block
	    && session->auth_send_string_block
	    && session->auth_recv_binary_block
	    && session->auth_send_binary_block) {
	    session->auth_buffers[0].address = session->auth_client_block;
	    session->auth_buffers[0].length =
		sizeof (*session->auth_client_block);

	    session->auth_buffers[1].address = session->auth_recv_string_block;
	    session->auth_buffers[1].length =
		sizeof (*session->auth_recv_string_block);

	    session->auth_buffers[2].address = session->auth_send_string_block;
	    session->auth_buffers[2].length =
		sizeof (*session->auth_send_string_block);

	    session->auth_buffers[3].address = session->auth_recv_binary_block;
	    session->auth_buffers[3].length =
		sizeof (*session->auth_recv_binary_block);

	    session->auth_buffers[4].address = session->auth_send_binary_block;
	    session->auth_buffers[4].length =
		sizeof (*session->auth_send_binary_block);

	    session->num_auth_buffers = 5;
	} else if (session->bidirectional_auth) {
	    /* we must authenticate, but can't. error out */
	    printk("iSCSI: session %p requires birectional authentication, "
		   "but couldn't allocate authentication stuctures\n", session);
	    ret = -1;		/* retry */
	    up(&session->config_mutex);
	    goto done;
	} else {
	    /* try to login without auth structures, and see if the target
	     * will let us in anyway.  If we get rejected, retry, and hope
	     * we can allocate auth structures next time.
	     */
	    DEBUG_INIT("iSCSI: session %p authentication configured, "
		       "but couldn't allocate authentication structures\n",
		       session);
	}
    }

    /* clear the connect timer */
    session->login_phase_timer = 0;
    smp_mb();
    iscsi_handle_signals(session);

    /* try to make sure other timeouts don't go off as soon
     * as the session is established 
     */
    session->last_rx = jiffies;
    session->last_ping = jiffies - 1;

    /* initialize session fields for the iscsi-login code */
    session->type = ISCSI_SESSION_TYPE_NORMAL;
    /* iSCSI default, unless declared otherwise by the target during login */
    session->MaxXmitDataSegmentLength = DEFAULT_MAX_RECV_DATA_SEGMENT_LENGTH;
    session->vendor_specific_keys = 1;
    smp_mb();

    /* use the session's rx_buffer for a login PDU buffer, since it is
     * currently unused.  We can't afford to dynamically allocate
     * memory right now, since it's possible we're reconnecting, and
     * the VM system is already blocked trying to write dirty pages to
     * the iSCSI device we're trying to reconnect.  The session's
     * rx_buffer was sized to have enough space for us to handle the login
     * phase.  
     */
    login_status =
	iscsi_login(session, session->rx_buffer, sizeof (session->rx_buffer),
		    &status_class, &status_detail);

    /* release the lock on the per-session settings used by the login code */
    up(&session->config_mutex);

    switch (login_status) {
    case LOGIN_OK:
	/* check the status class and detail */
	break;

    case LOGIN_IO_ERROR:
    case LOGIN_WRONG_PORTAL_GROUP:
    case LOGIN_REDIRECTION_FAILED:
	/* these may indicate problems with just the current
	 * portal.  Try a different one 
	 */
	iscsi_disconnect(session);
	spin_lock(&session->portal_lock);
	next_portal(session);
	printk("iSCSI: session %p retrying login to portal %u at %lu\n",
	       session, session->current_portal, jiffies);
	spin_unlock(&session->portal_lock);
	ret = -1;
	goto done;

    default:
    case LOGIN_FAILED:
    case LOGIN_NEGOTIATION_FAILED:
    case LOGIN_AUTHENTICATION_FAILED:
    case LOGIN_VERSION_MISMATCH:
    case LOGIN_INVALID_PDU:
	/* these are problems that will probably occur with
	 * any portal of this target. 
	 */
	if (session->ever_established && session->num_luns
	    && session->commands_queued) {
	    /* the session has found LUNs and been used before, so
	     * applications or the buffer cache may be expecting
	     * it to continue working.  Keep trying to login even
	     * though clearing the error may require
	     * reconfiguration on the target.
	     */
	    iscsi_disconnect(session);
	    spin_lock(&session->portal_lock);
	    next_portal(session);
	    printk("iSCSI: session %p may be in use, retrying login to "
		   "portal %u at %lu\n",
		   session, session->current_portal, jiffies);
	    spin_unlock(&session->portal_lock);
	    ret = -1;
	} else {
	    printk("iSCSI: session %p giving up on login attempts at %lu\n",
		   session, jiffies);
	    iscsi_disconnect(session);
	    ret = 0;
	}
	goto done;
    }

    /* check the login status */
    switch (status_class) {
    case STATUS_CLASS_SUCCESS:
	session->auth_failures = 0;
	ret = 1;
	break;
    case STATUS_CLASS_REDIRECT:
	switch (status_detail) {
	case ISCSI_LOGIN_STATUS_TGT_MOVED_TEMP:{
		unsigned int portal;

		/* the session IP address was changed by the login
		 * library, sp just try again with this portal
		 * config but the new address.
		 */
		session->auth_failures = 0;
		smp_mb();
		ret = 1;	/* not really success, but
				 * we want to retry
				 * immediately, with no
				 * delay 
				 */
		spin_lock(&session->portal_lock);
		portal =
		    find_portal(session, session->ip_address,
				session->ip_length, session->port);
		if (portal != UINT_MAX) {
		    /* FIXME: IPv6 */
		    printk("iSCSI: session %p login to portal %u temporarily "
			   "redirected to portal %u = %u.%u.%u.%u port %d\n",
			   session, session->current_portal, portal,
			   session->ip_address[0], session->ip_address[1],
			   session->ip_address[2], session->ip_address[3],
			   session->port);

		    /* try to switch to the portal we've
		     * been redirected to.  if that fails,
		     * try to come back to the portal we
		     * were redirected away from.  if that
		     * fails, try any other portals.
		     */
		    session->requested_portal = portal;
		    session->fallback_portal = session->current_portal;
		} else {
		    /* FIXME: IPv6 */
		    printk("iSCSI: session %p login to portal %u temporarily "
			   "redirected to %u.%u.%u.%u port %d\n",
			   session, session->current_portal,
			   session->ip_address[0], session->ip_address[1],
			   session->ip_address[2], session->ip_address[3],
			   session->port);

		    /* we'll connect to the session's
		     * address next time.  If that fails,
		     * we'll fallback to the current portal
		     * automatically.
		     */
		}
		spin_unlock(&session->portal_lock);
		goto done;
	    }
	case ISCSI_LOGIN_STATUS_TGT_MOVED_PERM:{
		unsigned int portal;

		/* for a permanent redirect, we need to
		 * update the portal address, and then try
		 * again. 
		 */
		session->auth_failures = 0;
		smp_mb();
		ret = 1;	/* not really success, but
				 * we want to retry
				 * immediately, with no
				 * delay 
				 */
		spin_lock(&session->portal_lock);
		portal =
		    find_portal(session, session->ip_address,
				session->ip_length, session->port);
		if (portal != UINT_MAX) {
		    /* FIXME: IPv6 */
		    printk("iSCSI: session %p login to portal %u permanently "
			   "redirected to portal %u = %u.%u.%u.%u port %d\n",
			   session, session->current_portal, portal,
			   session->ip_address[0], session->ip_address[1],
			   session->ip_address[2], session->ip_address[3],
			   session->port);

		    /* We want to forget about the current portal.
		     * Mark this portal dead, and switch to the new portal.
		     */
		    session->portals[session->current_portal].ip_length = 0;

		    /* and switch to the other portal */
		    set_portal(session, portal);
		} else {
		    printk("iSCSI: session %p login to portal %u permanently "
			   "redirected to %u.%u.%u.%u port %d\n",
			   session, session->current_portal,
			   session->ip_address[0], session->ip_address[1],
			   session->ip_address[2], session->ip_address[3],
			   session->port);

		    /* reset the address in the current portal info */
		    session->portals[session->current_portal].ip_length =
			session->ip_length;
		    memcpy(session->portals[session->current_portal].ip_address,
			   session->ip_address, session->ip_length);
		    session->portals[session->current_portal].port =
			session->port;

		    /* and just try logging in again with
		     * the current portal's config.  It'd be
		     * nice for Subnet entries in the
		     * iscsi.conf file to take effect, but
		     * arranging for that means exporting
		     * them all into the kernel module.
		     */
		}

		spin_unlock(&session->portal_lock);
		goto done;
	    }
	default:
	    ret = -1;
	    session->auth_failures = 0;
	    smp_mb();
	    printk("iSCSI: session %p login rejected: redirection type "
		   "0x%x not supported\n", session, status_detail);
	    break;
	}
	iscsi_disconnect(session);
	goto done;
    case STATUS_CLASS_INITIATOR_ERR:
	switch (status_detail) {
	case ISCSI_LOGIN_STATUS_AUTH_FAILED:
	    printk("iSCSI: session %p login rejected: initiator failed "
		   "authentication with target %s\n",
		   session, session->TargetName);
	    iscsi_disconnect(session);
	    spin_lock(&session->portal_lock);
	    if ((session->num_auth_buffers < 5) &&
		(session->username || session->password_length
		 || session->bidirectional_auth)) {
		/* retry, and hope we can allocate the auth
		 * structures next time 
		 */
		DEBUG_INIT("iSCSI: session %p retrying the same portal, "
			   "no authentication structures allocated\n", session);
		ret = -1;
	    } else if ((!session->ever_established)
		       && (session->auth_failures >= session->num_portals)) {
		/* give up, since we've tried every portal,
		 * and have never established a session 
		 */
		printk("iSCSI: session %p terminating login attempts, "
		       "%d of %d portals failed authentication or "
		       "authorization\n",
		       session, session->auth_failures, session->num_portals);
		ret = 0;
	    } else if (session->portal_failover) {
		/* try a different portal */
		session->auth_failures++;
		next_portal(session);
		ret = -1;
	    } else {
		session->auth_failures = 0;
		ret = 0;
	    }
	    spin_unlock(&session->portal_lock);
	    goto done;
	case ISCSI_LOGIN_STATUS_TGT_FORBIDDEN:
	    printk("iSCSI: session %p login rejected: initiator failed "
		   "authorization with target %s\n",
		   session, session->TargetName);
	    iscsi_disconnect(session);
	    spin_lock(&session->portal_lock);
	    session->auth_failures++;
	    if ((!session->ever_established)
		&& (session->auth_failures >= session->num_portals)) {
		/* give up, since we've tried every portal,
		 * and have never established a session 
		 */
		printk("iSCSI: session %p terminating login attempts, "
		       "%d of %d portals failed authentication or "
		       "authorization\n",
		       session, session->auth_failures, session->num_portals);
		ret = 0;
	    } else if (session->portal_failover) {
		/* try a different portal */
		next_portal(session);
		ret = -1;
	    } else {
		session->auth_failures = 0;
		ret = 0;
	    }
	    spin_unlock(&session->portal_lock);
	    goto done;
	case ISCSI_LOGIN_STATUS_TGT_NOT_FOUND:
	    printk("iSCSI: session %p login rejected: initiator error - target "
		   "not found (%02x/%02x)\n",
		   session, status_class, status_detail);
	    session->auth_failures = 0;
	    iscsi_disconnect(session);
	    ret = 0;
	    goto done;
	case ISCSI_LOGIN_STATUS_NO_VERSION:
	    /* FIXME: if we handle multiple protocol
	     * versions, before we log an error, try the
	     * other supported versions. 
	     */
	    printk("iSCSI: session %p login rejected: incompatible version "
		   "(%02x/%02x), non-retryable, giving up\n",
		   session, status_class, status_detail);
	    session->auth_failures = 0;
	    iscsi_disconnect(session);
	    ret = 0;
	    goto done;
	default:
	    printk("iSCSI: session %p login rejected: initiator error "
		   "(%02x/%02x), non-retryable, giving up\n",
		   session, status_class, status_detail);
	    session->auth_failures = 0;
	    iscsi_disconnect(session);
	    ret = 0;
	    goto done;
	}
    case STATUS_CLASS_TARGET_ERR:
	printk("iSCSI: session %p login rejected: target error (%02x/%02x)\n",
	       session, status_class, status_detail);
	session->auth_failures = 0;
	iscsi_disconnect(session);
	/* Try a different portal for the retry.  We have no idea
	 * what the problem is, but maybe a different portal will
	 * work better. (for single portal setups we try again in
	 * a couple of seconds on the same portal)
	 */
	spin_lock(&session->portal_lock);
	ret = -1;
	if (session->portal_failover)
	    next_portal(session);
	spin_unlock(&session->portal_lock);
	goto done;
    default:
	printk("iSCSI: session %p login response with unknown status class "
	       "0x%x, detail 0x%x\n", session, status_class, status_detail);
	session->auth_failures = 0;
	iscsi_disconnect(session);
	ret = 0;
	goto done;
    }

    /* logged in, get the new session ready */
    clear_bit(SESSION_LOGGED_OUT, &session->control_bits);
    session->fallback_portal = UINT_MAX;
    session->tried_portal_bitmap = 0;
    session->ever_established = 1;
    session->session_alive = 1;
    session->generation++;
    session->auth_failures = 0;
    session->last_rx = jiffies;
    session->last_ping = jiffies - 1;
    session->last_window_check = jiffies;
    session->last_peak_window_size = 0;
    session->last_kill = 0;
    session->window_closed = 0;
    session->window_full = 0;
    session->current_peak_window_size = max_tasks_for_session(session);
    session->window_peak_check = jiffies;
    session->warm_reset_itt = RSVD_TASK_TAG;
    session->cold_reset_itt = RSVD_TASK_TAG;
    session->nop_reply.ttt = RSVD_TASK_TAG;
    session->nop_reply_head = session->nop_reply_tail = NULL;
    session->session_established_time = jiffies;	/* used to detect 
							 * sessions that die as 
							 * soon as we hit FFP 
							 */
    session->session_drop_time = 0;	/* used to detect sessions that aren't 
					 * coming back up 
					 */
    session->login_phase_timer = 0;
    if (session->TargetAlias[0])
	session->log_name = session->TargetAlias;
    smp_mb();

    /* announce it */
    if (session->TargetAlias[0] != '\0')
	printk("iSCSI: bus %d target %d established session %p #%lu to "
	       "portal %u, address %u.%u.%u.%u port %d group %d, alias %s\n",
	       session->iscsi_bus, session->target_id, session,
	       session->generation, session->current_portal,
	       session->ip_address[0], session->ip_address[1],
	       session->ip_address[2], session->ip_address[3], session->port,
	       session->portal_group_tag, session->TargetAlias);
    else
	printk("iSCSI: bus %d target %d established session %p #%lu, portal "
	       "%u, address %u.%u.%u.%u port %d group %d\n",
	       session->iscsi_bus, session->target_id, session,
	       session->generation, session->current_portal,
	       session->ip_address[0], session->ip_address[1],
	       session->ip_address[2], session->ip_address[3], session->port,
	       session->portal_group_tag);

    if (LOG_ENABLED(ISCSI_LOG_INIT) || LOG_ENABLED(ISCSI_LOG_EH)) {
	printk("iSCSI: session %p #%lu established at %lu, isid "
	       "0x%02x%02x%02x%02x%02x%02x, tsih %u, %u normal cmnds, "
	       "%u deferred cmnds, %u tasks, bits 0x%08lx\n",
	       session, session->generation, jiffies, session->isid[0],
	       session->isid[1], session->isid[2], session->isid[3],
	       session->isid[4], session->isid[5], session->tsih,
	       atomic_read(&session->num_cmnds), session->num_deferred_cmnds,
	       atomic_read(&session->num_active_tasks), session->control_bits);
    }

    /* mark the session as up and accepting commands again */
    clear_bit(SESSION_REPLACEMENT_TIMEDOUT, &session->control_bits);
    smp_wmb();
    set_bit(SESSION_ESTABLISHED, &session->control_bits);
    smp_mb();

    /* wake up everyone waiting for the session to be established */
    wake_up(&session->login_wait_q);

    /* make sure we start sending commands again */
    wake_tx_thread(TX_SCSI_COMMAND, session);

  done:
    /* clear any timer that may have been left running */
    session->login_phase_timer = 0;
    smp_mb();
    /* cleanup after a possible timeout expiration */
    if (iscsi_handle_signals(session)) {
	if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p terminating, giving up on login "
		       "attempts\n", session);
	    return 0;
	} else {
	    DEBUG_INIT("iSCSI: session %p received signal during login, "
		       "retrying\n", session);
	    return -1;
	}
    }

    return ret;
}

static inline void
append_queue(Scsi_Cmnd ** to_head, Scsi_Cmnd ** to_tail, Scsi_Cmnd ** from_head,
	     Scsi_Cmnd ** from_tail)
{
    if (*to_head && *from_head) {
	/* both non-empty, append 'from' to 'to' */
	(*to_tail)->host_scribble = (void *) *from_head;
	*to_tail = *from_tail;
	*from_head = NULL;
	*from_tail = NULL;
    } else if (*from_head) {
	/* 'from' becomes 'to' */
	*to_head = *from_head;
	*to_tail = *from_tail;
	*from_head = NULL;
	*from_tail = NULL;
    }
}

/* caller must hold the task_lock */
static void
requeue_or_fail_commands(iscsi_session_t * session)
{
    Scsi_Cmnd *fatal_head = NULL, *fatal_tail = NULL;
    Scsi_Cmnd *requeue_head = NULL, *requeue_tail = NULL;
    Scsi_Cmnd *sc = NULL;
    iscsi_task_t *task = NULL;
    int fail_all = 0;
    int num_failed = 0;
    int num_tasks = 0;
    DECLARE_MIDLAYER_FLAGS;
    DECLARE_NOQUEUE_FLAGS;

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	/* no point in retrying anything */
	if (test_bit(ISCSI_HBA_SHUTTING_DOWN, &session->hba->flags))
	    DEBUG_INIT("iSCSI: session %p terminating, failing all SCSI "
		       "commands\n", session);
	else
	    printk("iSCSI: session %p terminating, failing all SCSI commands\n",
		   session);
	fail_all = 1;
    } else {
	DEBUG_INIT("iSCSI: session %p requeue_or_fail_commands at %lu\n",
		   session, jiffies);
    }

    /* grab all the tasks for this connection */
    while ((task = session->arrival_order.head)) {
	session->arrival_order.head = task->order_next;

	del_task_timer(task);

	if (atomic_read(&task->refcount) == 0) {
	    ISCSI_TRACE(ISCSI_TRACE_TaskAborted, sc, task, 0, 0);

	    task->next = task->prev = task->order_next = task->order_prev =
		NULL;
	    sc = task->scsi_cmnd;
	    task->scsi_cmnd = NULL;

	    if (sc)
		add_cmnd(sc, &requeue_head, &requeue_tail);

	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		DEBUG_ALLOC("iSCSI: session %p requeue_or_fail freeing "
			    "task %p at %lu\n", session, task, jiffies);

	    num_tasks++;
	    free_task(session, task);
	} else {
	    /* This should never happen, which is good,
	     * since we don't really have any good options
	     * here.  Leak the task memory, and fail to
	     * complete the cmnd, which may leave apps
	     * blocked forever in the kernel.
	     */
	    printk("iSCSI: bug - session %p can't complete itt %u task %p, "
		   "refcount %u, command %p, leaking task memory\n",
		   session, task->itt, task, atomic_read(&task->refcount),
		   task->scsi_cmnd);
	}
    }

    if (test_bit(SESSION_TERMINATING, &session->control_bits)
	&& LOG_ENABLED(ISCSI_LOG_ALLOC))
	printk("iSCSI: session %p for (%u %u %u *) requeue_or_fail freed %d "
	       "tasks at %lu, alloc %u freed %u\n",
	       session, session->host_no, session->channel, session->target_id,
	       num_tasks, jiffies, session->tasks_allocated,
	       session->tasks_freed);

    session->arrival_order.head = session->arrival_order.tail = NULL;
    atomic_set(&session->num_active_tasks, 0);
    /* clear out the task collections */
    session->tx_tasks.head = session->tx_tasks.tail = NULL;
    session->warm_reset_itt = RSVD_TASK_TAG;
    session->cold_reset_itt = RSVD_TASK_TAG;

    /* grab the retry, deferred, and normal queues in that order */
    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
    append_queue(&requeue_head, &requeue_tail, &session->retry_cmnd_head,
		 &session->retry_cmnd_tail);
    atomic_set(&session->num_retry_cmnds, 0);
    append_queue(&requeue_head, &requeue_tail, &session->deferred_cmnd_head,
		 &session->deferred_cmnd_tail);
    session->num_deferred_cmnds = 0;
    append_queue(&requeue_head, &requeue_tail, &session->scsi_cmnd_head,
		 &session->scsi_cmnd_tail);
    atomic_set(&session->num_cmnds, 0);

    while ((sc = requeue_head)) {
	requeue_head = (Scsi_Cmnd *) sc->host_scribble;

	if (fail_all || (sc->allowed <= 1)) {
	    /* fail it */
	    add_cmnd(sc, &fatal_head, &fatal_tail);
	    num_failed++;
	} else {
	    /* requeue it */
	    add_cmnd(sc, &session->scsi_cmnd_head, &session->scsi_cmnd_tail);
	    atomic_inc(&session->num_cmnds);
	}
    }

    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    /* fail any commands that can't be retried */
    LOCK_MIDLAYER_LOCK(session->hba->host);
    while ((sc = fatal_head)) {
	fatal_head = (Scsi_Cmnd *) sc->host_scribble;

	del_command_timer(sc);
	sc->host_scribble = NULL;
	sc->resid = iscsi_expected_data_length(sc);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	if (sc->allowed > 1)
	    sc->retries = sc->allowed - 1;

	set_not_ready(sc);

	/* FIXME: always log these?  sometimes log these? */
	printk("iSCSI: session %p failing command %p cdb 0x%02x to "
	       "(%u %u %u %u) at %lu\n",
	       session, sc, sc->cmnd[0], sc->host->host_no, sc->channel,
	       sc->target, sc->lun, jiffies);

	if (sc->scsi_done) {
	    add_completion_timer(sc);
	    sc->scsi_done(sc);
	}
    }

    UNLOCK_MIDLAYER_LOCK(session->hba->host);
}

static int
iscsi_rx_thread(void *vtaskp)
{
    iscsi_session_t *session;
    iscsi_hba_t *hba;
    int rc = -EPIPE, length, xlen;
    struct msghdr msg;
    struct iovec iov[2];
    struct IscsiHdr sth;
    uint32_t crc32c;
    unsigned char *rxbuf;
    long login_delay = 0;
    int pad;
    unsigned long session_failures = 0;

    if (vtaskp == NULL) {
	printk("iSCSI: rx thread task parameter NULL\n");
	return 0;
    }

    session = (iscsi_session_t *) vtaskp;
    /* whoever created the thread already incremented the
     * session's refcount for us 
     */

    hba = session->hba;

    DEBUG_INIT("iSCSI: session %p rx thread %d about to daemonize on cpu%d\n",
	       session, current->pid, smp_processor_id());

    /* become a daemon kernel thread, and abandon any user space resources */
    sprintf(current->comm, "iscsi-rx");
    iscsi_daemonize();
    session->rx_pid = current->pid;
    current->flags |= PF_MEMALLOC;
    smp_mb();

    /* check to see if iscsi_terminate_session was called before we
     * started running, since we can't get a signal from it until
     * until we set session->rx_pid.
     */
    if (test_bit(SESSION_TERMINATING, &session->control_bits))
	goto ThreadExit;

    /* Block all signals except SIGHUP and SIGKILL */
    LOCK_SIGNALS();
    siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGHUP));
    RECALC_PENDING_SIGNALS;
    UNLOCK_SIGNALS();

    DEBUG_INIT("iSCSI: session %p rx thread %d starting on cpu%d\n", session,
	       current->pid, smp_processor_id());

    while (!test_bit(SESSION_TERMINATING, &session->control_bits)) {
	unsigned long login_failures = 0;

	/* we need a session for the rx and tx threads to use */
	while (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	    if (login_delay) {
		printk("iSCSI: session %p to %s waiting %ld seconds before "
		       "next login attempt\n",
		       session, session->log_name, login_delay);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(login_delay * HZ);
	    }

	    /* ensure we can write to the socket without interference */
	    DEBUG_INIT("iSCSI: session %p rx thread %d waiting for tx blocked "
		       "for at %lu\n", session, current->pid, jiffies);
	    wait_for_tx_blocked(session);
	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		goto ThreadExit;

	    /* now that the tx thread is idle, it's safe to
	     * clean up the old session, if there was one 
	     */
	    iscsi_disconnect(session);
	    /* FIXME: should clearing these bits move to
	     * iscsi_establish_session? 
	     */
	    clear_bit(SESSION_DROPPED, &session->control_bits);
	    clear_bit(SESSION_TASK_ALLOC_FAILED, &session->control_bits);
	    clear_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits);
	    clear_bit(SESSION_WINDOW_CLOSED, &session->control_bits);
	    clear_bit(SESSION_RESETTING, &session->control_bits);
	    clear_bit(SESSION_RESET, &session->control_bits);
	    clear_bit(SESSION_TASK_MGMT_TIMEDOUT, &session->control_bits);
	    smp_mb();

	    /* try to get a new session */
	    rc = iscsi_establish_session(session);
	    if (rc > 0) {
		/* established or redirected */
		login_failures = 0;
		session->session_alive = 1;
	    } else if (rc < 0) {
		/* failed, retry */
		login_failures++;
	    } else {
		/* failed, give up */
		printk("iSCSI: session %p giving up at %lu\n", session,
		       jiffies);
		iscsi_terminate_session(session);
		goto ThreadExit;
	    }

	    /* slowly back off the frequency of login attempts */
	    if (login_failures == 0)
		login_delay = 0;
	    else if (login_failures < 30)
		login_delay = 1;	/* 30 seconds at 1 sec each */
	    else if (login_failures < 48)
		login_delay = 5;	/* another 90 seconds at 5 sec each */
	    else if (session->replacement_timeout &&
		     time_before_eq(session->session_drop_time +
				    (HZ * session->replacement_timeout),
				    jiffies)) {
		login_delay = 10;	/* every 10 seconds */
	    } else {
		/* we've already failed all commands out of the
		 * driver, but if we can bring the session back up, we
		 * can stop failing new commands in queuecommand.
		 */
		login_delay = 60;
	    }
	}

	DEBUG_INIT("iSCSI: session %p established by rx thread %d at %lu\n",
		   session, current->pid, jiffies);

	/* handle rx for this session */
	while (!signal_pending(current)) {
	    /* check for anything to read on socket */
	    iov[0].iov_base = &sth;
	    iov[0].iov_len = length = sizeof (sth);
	    memset(&msg, 0, sizeof (msg));
	    msg.msg_iov = iov;
	    msg.msg_iovlen = 1;
	    if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
		iov[1].iov_base = &crc32c;
		iov[1].iov_len = sizeof (crc32c);
		msg.msg_iovlen = 2;
		length += sizeof (crc32c);
	    }

	    DEBUG_FLOW("iSCSI: session %p rx thread %d waiting to receive "
		       "%d header bytes\n", session, session->rx_pid, length);

	    rc = iscsi_recvmsg(session, &msg, length);
	    if (signal_pending(current)) {
		DEBUG_FLOW("iSCSI: session %p rx thread %d received signal\n",
			   session, session->rx_pid);
		goto EndSession;
	    }
	    if (rc == length) {
		DEBUG_FLOW("iSCSI: session %p rx thread %d received "
			   "%d header bytes, opcode 0x%x\n",
			   session, session->rx_pid, length, sth.opcode);
		/* HeaderDigests */
		if (session->HeaderDigest == ISCSI_DIGEST_CRC32C) {
		    uint32_t calculated_crc32c =
			iscsi_crc32c(&sth, sizeof (sth));

		    if (session->fake_read_header_mismatch > 0) {
			session->fake_read_header_mismatch--;
			smp_mb();
			printk("iSCSI: session %p faking HeaderDigest "
			       "mismatch for itt %u\n", session,
			       ntohl(sth.itt));
			calculated_crc32c = 0x01020304;
		    }

		    if (calculated_crc32c != crc32c) {
			printk("iSCSI: session %p HeaderDigest mismatch, "
			       "received 0x%08x, calculated 0x%08x, dropping "
			       "session at %lu\n",
			       session, crc32c, calculated_crc32c, jiffies);
			iscsi_drop_session(session);
			goto EndSession;
		    }
		}

		/* received something */
		xlen = ntoh24(sth.dlength);

		if (sth.hlength) {
		    /* FIXME: read any additional header
		     * segments.  For now, drop the session
		     * if one is received, since we can't
		     * handle them.
		     */
		    printk("iSCSI: session %p received opcode %x, ahs "
			   "length %d, dlength %d, itt %u at %lu\n",
			   session, sth.opcode, sth.hlength, xlen,
			   ntohl(sth.itt), jiffies);
		    printk("iSCSI: session %p dropping, additional header "
			   "segments not supported by this driver version.\n",
			   session);
		    iscsi_drop_session(session);
		    goto EndSession;
		}

		/* If there are padding bytes, read them as well */
		pad = xlen % PAD_WORD_LEN;
		if (pad) {
		    pad = PAD_WORD_LEN - pad;
		    xlen += pad;
		}

		DEBUG_FLOW("iSCSI: session %p rx PDU, opcode 0x%x, dlength %d "
			   "at %lu\n", session, sth.opcode, xlen, jiffies);

		if (xlen && (sth.opcode != ISCSI_OP_SCSI_DATA_RSP)
		    && (sth.opcode != ISCSI_OP_NOOP_IN)) {
		    /* unless it's got a (possibly large)
		     * data payload, read the whole PDU into
		     * memory beforehand 
		     */
		    if (xlen > ISCSI_RXCTRL_SIZE) {
			printk("iSCSI: session %p PDU data length too large, "
			       "opcode %x, dlen %d\n", session, sth.opcode,
			       xlen);
			iscsi_drop_session(session);
			goto EndSession;
		    }
		    rxbuf = session->rx_buffer;
		    iov[0].iov_base = rxbuf;
		    iov[0].iov_len = xlen;
		    memset(&msg, 0, sizeof (struct msghdr));
		    msg.msg_iov = iov;
		    msg.msg_iovlen = 1;
		    length = xlen;

		    if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
			iov[1].iov_base = &crc32c;
			iov[1].iov_len = sizeof (crc32c);
			msg.msg_iovlen = 2;
			length += sizeof (crc32c);
		    }

		    rc = iscsi_recvmsg(session, &msg, length);
		    if (rc != length) {
			printk("iSCSI: session %p PDU opcode 0x%x, recvmsg %d "
			       "failed, rc %d\n",
			       session, sth.opcode, length, rc);
			iscsi_drop_session(session);
			goto EndSession;
		    }

		    if (session->DataDigest == ISCSI_DIGEST_CRC32C) {
			uint32_t calculated_crc32c = iscsi_crc32c(rxbuf, xlen);

			if (calculated_crc32c != crc32c) {
			    /* FIXME: if it's a command response, we MUST 
			     * do a Logout and drop the session.  it's
			     * not a command response or data, we're 
			     * allowed to just ignore the PDU.  It 
			     * must have been Async with sense, or a
			     * Reject, or Nop-in with data, and other 
			     * timers should handle those.  For now,
			     * ignore the spec, and just drop the 
			     * session unconditionally.
			     */
			    printk("iSCSI: session %p DataDigest mismatch, "
				   "opcode 0x%x, received 0x%08x, calculated "
				   "0x%08x, dropping session at %lu\n",
				   session, sth.opcode, crc32c,
				   calculated_crc32c, jiffies);
			    iscsi_drop_session(session);
			    goto EndSession;
			}
		    }
		} else {
		    rxbuf = NULL;
		}

		switch (sth.opcode) {
		case ISCSI_OP_NOOP_IN | 0xc0:	/* work-around a bug in the 
						 * Intel Nov05 target 
						 */
		case ISCSI_OP_NOOP_IN:
		    iscsi_recv_nop(session, (struct IscsiNopInHdr *) &sth);
		    break;
		case ISCSI_OP_SCSI_RSP:
		    iscsi_recv_cmd(session, (struct IscsiScsiRspHdr *) &sth,
				   rxbuf);
		    break;
		case ISCSI_OP_SCSI_TASK_MGT_RSP:
		    iscsi_recv_task_mgmt(session,
					 (struct IscsiScsiTaskMgtRspHdr *)
					 &sth);
		    break;
		case ISCSI_OP_RTT_RSP:
		    iscsi_recv_r2t(session, (struct IscsiRttHdr *) &sth);
		    break;
		case ISCSI_OP_SCSI_DATA_RSP:
		    iscsi_recv_data(session, (struct IscsiDataRspHdr *) &sth);
		    break;
		case ISCSI_OP_ASYNC_EVENT:
		    iscsi_recv_async_event(session,
					   (struct IscsiAsyncEvtHdr *) &sth,
					   rxbuf);
		    break;
		case ISCSI_OP_REJECT_MSG:
		    iscsi_recv_reject(session,
				      (struct IscsiRejectRspHdr *) &sth, rxbuf);
		    break;
		case ISCSI_OP_LOGOUT_RSP:
		    iscsi_recv_logout(session,
				      (struct IscsiLogoutRspHdr *) &sth);
		    break;
		default:
		    printk("iSCSI: session %p dropping after receiving "
			   "unexpected opcode 0x%x\n", session, sth.opcode);
		    session->time2wait = 2;	/* don't spin if the target 
						 * always sends illegal 
						 * opcodes 
						 */
		    iscsi_drop_session(session);
		    goto EndSession;
		}
	    } else {
		if (rc != -EAGAIN) {
		    if (rc == 0) {
			printk("iSCSI: session %p closed by target %s at %lu\n",
			       session, session->log_name, jiffies);
		    } else if (rc == -ECONNRESET) {
			printk("iSCSI: session %p to %s received connection "
			       "reset at %lu\n",
			       session, session->log_name, jiffies);
		    } else if (rc == -ERESTARTSYS) {
			printk("iSCSI: session %p to %s received signal "
			       "at %lu\n", session, session->log_name, jiffies);
		    } else {
			printk("iSCSI: session %p to %s short PDU header read, "
			       "%d of %d at %lu\n",
			       session, session->log_name, rc, length, jiffies);
		    }
		    iscsi_drop_session(session);
		    goto EndSession;
		}
	    }
	}

      EndSession:
	DEBUG_INIT("iSCSI: session %p going down at %lu\n", session, jiffies);

	/* calculate how long to wait before logging in again */
	if (session->time2wait >= 0) {
	    /* the target gave us a specific Time2Wait */
	    login_delay = session->time2wait;
	    session->time2wait = -1;
	    DEBUG_INIT("iSCSI: session %p Time2Wait %ld\n", session,
		       login_delay);
	} else {
	    /* use the default */
	    login_delay = session->DefaultTime2Wait;
	    DEBUG_INIT("iSCSI: session %p DefaultTime2Wait %ld\n", session,
		       login_delay);
	}

	if (time_before_eq
	    (session->session_drop_time,
	     session->session_established_time + (2 * HZ))) {
	    /* if the session dies really quicky after we reach
	     * full-feature phase, we may not be interoperable due to
	     * bugs in the target (or this driver) that send illegal
	     * opcodes, or disagreements about how to do CRC
	     * calculations.  To avoid spinning, we track sessions
	     * with really short lifetimes, and decrease the login
	     * frequency if we keep getting session failures, like we
	     * do for login failures.
	     */
	    session_failures++;

	    if (session_failures < 30)
		login_delay = MAX(login_delay, 1);	/* 30 seconds at 1 sec 
							 * each 
							 */
	    else if (session_failures < 48)
		login_delay = MAX(login_delay, 5);	/* another 90 seconds 
							 * at 5 sec each 
							 */
	    else if (session->replacement_timeout &&
		     time_before_eq(session->session_drop_time +
				    (HZ * session->replacement_timeout),
				    jiffies)) {
		login_delay = MAX(login_delay, 10);	/* every 10 seconds */
	    } else {
		/* after the replacement timeout has expired, the
		 * device will probably be offline, so we probably
		 * don't need a session anymore, but it's possible the
		 * device isn't offline yet because of all the
		 * hard-coded sleeps in the SCSI midlayer after resets
		 * occur, and in any case it might be useful to know
		 * if we ever get a session back for debugging
		 * purposes, so we'll keep trying occasionally.
		 */
		login_delay = MAX(login_delay, 60);
	    }

	    printk("iSCSI: session %p has ended quickly %lu times, login "
		   "delay %ld seconds\n", session, session_failures,
		   login_delay);
	} else {
	    /* session lived long enough that the target is probably ok */
	    session_failures = 0;
	}

	/* handle any signals that may have occured, which
	 * may kill the tx thread 
	 */
	iscsi_handle_signals(session);

	/* we need to wait for the tx thread to block before
	 * trying to complete commands, since it may be
	 * using a task at the moment, which means we can't
	 * complete it yet.  even if the session is
	 * terminating, we must wait for the tx thread.
	 */
	wait_for_tx_blocked(session);

	spin_lock(&session->task_lock);

	if (session->warm_reset_itt != RSVD_TASK_TAG) {
	    printk("iSCSI: session %p dropped during warm target reset, "
		   "assuming SCSI commands completed by reset\n", session);
	    session->warm_reset_itt = RSVD_TASK_TAG;
	    smp_mb();

	    /* FIXME: complete everything with DID_RESET? */
	    requeue_or_fail_commands(session);
	} else if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    requeue_or_fail_commands(session);
	} else if (test_bit(SESSION_LOGGED_OUT, &session->control_bits)) {
	    /* the session has logged out, so there
	     * shouldn't be any tasks, but check anyway 
	     */
	    requeue_or_fail_commands(session);
	} else {
	    /* session dropped unexpectedly, often due to network problems */
	    printk("iSCSI: session %p to %s dropped\n", session,
		   session->log_name);

	    /* fail all commands that don't allow retries,
	     * and requeue everything else 
	     */
	    requeue_or_fail_commands(session);
	}

	/* can't send any nop replies now */
	session->nop_reply.ttt = RSVD_TASK_TAG;
	while (session->nop_reply_head) {
	    iscsi_nop_info_t *nop_info = session->nop_reply_head;
	    session->nop_reply_head = nop_info->next;
	    DEBUG_ALLOC("iSCSI: kfree nop_info %p\n", nop_info);
	    kfree(nop_info);
	}
	session->nop_reply_tail = NULL;
	/* a ping test also fails on a connection drop */
	session->ping_test_start = 0;
	session->ping_test_rx_length = 0;
	session->ping_test_data_length = 0;
	if (session->ping_test_tx_buffer) {
	    kfree(session->ping_test_tx_buffer);
	    session->ping_test_tx_buffer = NULL;
	}

	/* no point trying to logout now */
	session->logout_deadline = 0;
	session->logout_response_deadline = 0;

	/* terminate error recovery and command retries */
	session->mgmt_itt = RSVD_TASK_TAG;
	session->task_mgmt_response_deadline = 0;
	del_timer_sync(&session->busy_task_timer);
	del_timer_sync(&session->busy_command_timer);
	del_timer_sync(&session->immediate_reject_timer);
	del_timer_sync(&session->retry_timer);
	memset(session->luns_timing_out, 0, sizeof (session->luns_timing_out));
	memset(session->luns_doing_recovery, 0,
	       sizeof (session->luns_doing_recovery));
	memset(session->luns_delaying_commands, 0,
	       sizeof (session->luns_delaying_commands));
	session->num_luns_delaying_commands = 0;

	/* we'll never get a reset reply now */
	session->warm_reset_itt = RSVD_TASK_TAG;
	session->reset_response_deadline = 0;

	/* cancel any testing */
	session->ignore_lun = -2;
	session->ignore_completions = 0;
	session->ignore_aborts = 0;
	session->ignore_abort_task_sets = 0;
	session->ignore_lun_resets = 0;
	session->ignore_warm_resets = 0;
	session->ignore_cold_resets = 0;
	session->reject_lun = -2;
	session->reject_aborts = 0;
	session->reject_abort_task_sets = 0;
	session->reject_lun_resets = 0;
	session->reject_warm_resets = 0;
	session->fake_status_lun = -2;
	session->fake_status_unreachable = 0;
	session->fake_status_busy = 0;
	session->fake_status_queue_full = 0;

	spin_unlock(&session->task_lock);
    }

  ThreadExit:
    DEBUG_INIT("iSCSI: session %p for (%u %u %u *) rx thread %d exiting\n",
	       session, session->host_no, session->channel, session->target_id,
	       session->rx_pid);
    /* indicate that we're already going down, so that we don't get killed */
    session->rx_pid = 0;
    smp_mb();

    /* this will fail all commands, since the SESSION_TERMINATING bit is set */
    requeue_or_fail_commands(session);

    spin_lock(&session->task_lock);
    /* no point trying to logout now */
    session->logout_deadline = 0;
    session->logout_response_deadline = 0;
    /* terminate error recovery */
    session->mgmt_itt = RSVD_TASK_TAG;
    session->task_mgmt_response_deadline = 0;
    session->reset_response_deadline = 0;
    /* ensure the timers have been deleted before we free their memory */
    del_timer_sync(&session->busy_task_timer);
    del_timer_sync(&session->busy_command_timer);
    del_timer_sync(&session->immediate_reject_timer);
    del_timer_sync(&session->retry_timer);
    memset(session->luns_timing_out, 0, sizeof (session->luns_timing_out));
    memset(session->luns_doing_recovery, 0,
	   sizeof (session->luns_doing_recovery));
    memset(session->luns_delaying_commands, 0,
	   sizeof (session->luns_delaying_commands));
    if (session->preallocated_task) {
	iscsi_task_ctor(session->preallocated_task, NULL, 0);
	kmem_cache_free(session->hba->task_cache, session->preallocated_task);
	session->preallocated_task = NULL;
    } else {
	printk("iSCSI: session %p for (%u %u %u *) terminating, but has "
	       "no preallocated task to free at %lu\n",
	       session, session->host_no, session->channel, session->target_id,
	       jiffies);
    }
    spin_unlock(&session->task_lock);

    /* cleanup the socket */
    if (session->socket) {
	/* wait for the tx thread to exit */
	while (session->tx_pid) {
	    DEBUG_INIT("iSCSI: session %p rx thread %d waiting for tx "
		       "thread %d to exit\n",
		       session, current->pid, session->tx_pid);
	    set_current_state(TASK_INTERRUPTIBLE);
	    schedule_timeout(MSECS_TO_JIFFIES(10));
	}

	/* drop the connection */
	iscsi_disconnect(session);
    }

    set_bit(SESSION_TERMINATED, &session->control_bits);

    /* wake up any ioctls sleeping on the session */
    wake_up(&session->login_wait_q);
    up(&session->probe_sem);

    /* iscsi_remove_luns(session); */
    if (!session->this_is_root_disk) {
	iscsi_remove_luns(session);
    }

    drop_reference(session);

    return 0;
}

static int
iscsi_inet_aton(char *asciiz, unsigned char *ip_address, int *ip_length)
{
    char *c = asciiz;
    unsigned char *ip = ip_address;
    uint32_t base = 10, value = 0;
    int empty;

    if ((asciiz == NULL) || (*asciiz == '\0') || (ip_address == NULL)
	|| (ip_length == NULL))
	return 0;

    /* FIXME: IPv6 */

    /* look for an IPv4 dotted-decimal IP address */
    while (*c) {
	value = 0;
	base = 10;
	empty = 1;

	/* each part must start with a digit */
	if (!is_digit(*c))
	    return 0;

	/* figure out the base for this part */
	if (*c == '0') {
	    empty = 0;
	    base = 8;
	    c++;
	    if (*c == 'x') {
		base = 16;
		c++;
	    }
	}

	/* get the value of this part */
	while (*c && (*c != '.')) {
	    if ((base == 16) && (is_hex_lower(*c))) {
		value = (value * base) + (*c - 'a') + 10;
		c++;
		empty = 0;
	    } else if ((base == 16) && (is_hex_upper(*c))) {
		value = (value * base) + (*c - 'A') + 10;
		c++;
		empty = 0;
	    } else if (is_digit(*c)) {
		value = (value * base) + (*c - '0');
		c++;
		empty = 0;
	    } else
		return 0;
	}

	/* reached the end of the part? */
	if (empty) {
	    return 0;
	} else if (*c == '.') {
	    c++;
	    if (value <= 0xFF) {
		*ip++ = value & 0xFF;
		value = 0;
	    } else
		return 0;
	}
    }

    if (*c == '\0') {
	/* end of the ascii address */
	switch (ip - ip_address) {
	default:
	    return 0;
	case 0:
	    /* a = 32 */
	    *ip++ = (value >> 24) & 0xFF;
	    *ip++ = (value >> 16) & 0xFF;
	    *ip++ = (value >> 8) & 0xFF;
	    *ip++ = (value >> 0) & 0xFF;
	    if (ip_length)
		*ip_length = 4;
	    return 1;
	case 1:
	    /* a.b = 8,24 */
	    if (value <= 0x00FFFFFF) {
		*ip++ = (value >> 16) & 0xFF;
		*ip++ = (value >> 8) & 0xFF;
		*ip++ = (value >> 0) & 0xFF;
		if (ip_length)
		    *ip_length = 4;
		return 1;
	    } else
		return 0;
	case 2:
	    /* a.b.c = 8,8,16 */
	    if (value <= 0x0000FFFF) {
		*ip++ = (value >> 8) & 0xFF;
		*ip++ = (value >> 0) & 0xFF;
		if (ip_length)
		    *ip_length = 4;
		return 1;
	    } else
		return 0;
	case 3:
	    /* a.b.c.d = 8,8,8,8 */
	    if (value <= 0x000000FF) {
		*ip++ = (value >> 0) & 0xFF;
		if (ip_length)
		    *ip_length = 4;
		return 1;
	    } else
		return 0;
	}
    }

    return 0;
}

static int
update_address(iscsi_session_t * session, char *address)
{
    char *tag;
    char *port;
    int ret = 0;
    unsigned char ip[16];
    int ip_length = 4;

    memset(ip, 0, sizeof (ip));

    if ((tag = iscsi_strrchr(address, ','))) {
	*tag = '\0';
	tag++;
    }
    if ((port = iscsi_strrchr(address, ':'))) {
	*port = '\0';
	port++;
    }

    /* update the session's IP address and port, based on the
     * TargetAddress passed to us.  For now, we can't resolve DNS
     * names, since that would require us to pass the request up to a
     * user-mode process, and use the NSS system.  We have our own
     * equivalent of inet_aton, and fail to change the address if we
     * get a DNS name instead of an IP address.
     */
    if (iscsi_inet_aton(address, ip, &ip_length)) {
	memcpy(session->ip_address, ip, sizeof (session->ip_address));
	session->ip_length = ip_length;
	if (port)
	    session->port = iscsi_strtoul(port, NULL, 0);
	else
	    session->port = ISCSI_LISTEN_PORT;

	ret = 1;
    }

    /* restore the original strings */
    if (tag) {
	--tag;
	*tag = ',';
    }
    if (port) {
	--port;
	*port = ':';
    }

    smp_mb();
    return ret;
}

static int
same_network_portal(iscsi_portal_info_t * p1, iscsi_portal_info_t * p2)
{
    if (p1->port != p2->port)
	return 0;

    if (p1->tag != p2->tag)
	return 0;

    if (p1->ip_length != p2->ip_length)
	return 0;

    if (memcmp(p1->ip_address, p2->ip_address, p1->ip_length))
	return 0;

    return 1;
}

static int
update_session(iscsi_session_t * session, iscsi_session_ioctl_t * ioctld,
	       iscsi_portal_info_t * portals)
{
    iscsi_portal_info_t *old_portals;
    iscsi_portal_info_t *old_portal;
    iscsi_portal_info_t *new_portal = NULL;
    iscsi_portal_info_t *q = NULL;
    char *username = NULL;
    unsigned char *password = NULL;
    int password_length = 0;
    char *username_in = NULL;
    unsigned char *password_in = NULL;
    int password_length_in = 0;
    int bidirectional = 0;
    int auth_update_failed = 0;
    int p;
    int relogin = 0;
    size_t length;
    char *str;
    unsigned int requested_portal = UINT_MAX, portal = 0;
    unsigned int need_to_start_timer = 0;
    int ret = 0, found = 0;

    if (down_interruptible(&session->config_mutex)) {
	/* signalled before we got the mutex */
	printk("iSCSI: session %p configuration update aborted by signal "
	       "at %lu\n", session, jiffies);
	return 0;
    }
    if (ioctld->update && (ioctld->config_number < session->config_number)) {
	/* this update is obsolete, ignore it */
	DEBUG_INIT("iSCSI: session %p ignoring obsolete update #%u, "
		   "currently on config #%u\n",
		   session, ioctld->config_number, session->config_number);
	return 0;
    }
    session->config_number = ioctld->config_number;

    printk("iSCSI: bus %d target %d updating configuration of session "
	   "%p to %s\n",
	   ioctld->iscsi_bus, ioctld->target_id, session, session->log_name);

    /* once we have the mutex, we're guaranteed that the session is
     * initialized and not logging in at the moment, so we can safely
     * change the per-session settings stored in the session itself:
     * isid, InitiatorName, InitiatorAlias, username, password
     */

    if (memcmp(session->isid, ioctld->isid, sizeof (session->isid))) {
	/* FIXME: the explicit logout better work, since
	 * there won't be an implicit logout 
	 */
	memcpy(session->isid, ioctld->isid, sizeof (session->isid));
	relogin = 1;
    }

    if ((session->InitiatorName == NULL)
	|| strcmp(ioctld->InitiatorName, session->InitiatorName)) {
	length = strlen(ioctld->InitiatorName);
	if ((str = kmalloc(length + 1, GFP_ATOMIC))) {
	    if (session->InitiatorName)
		kfree(session->InitiatorName);
	    session->InitiatorName = str;
	    strcpy(session->InitiatorName, ioctld->InitiatorName);
	    relogin = 1;
	    DEBUG_INIT("iSCSI: session %p updated InitiatorName at %lu\n",
		       session, jiffies);
	} else {
	    printk("iSCSI: session %p failed to change InitiatorName from "
		   "%s to %s\n",
		   session, session->InitiatorName, ioctld->InitiatorName);
	    up(&session->config_mutex);
	    return 0;
	}
    }

    if ((session->InitiatorAlias == NULL)
	|| strcmp(ioctld->InitiatorAlias, session->InitiatorAlias)) {
	length = strlen(ioctld->InitiatorAlias);
	if ((str = kmalloc(length + 1, GFP_ATOMIC))) {
	    if (session->InitiatorAlias)
		kfree(session->InitiatorAlias);
	    session->InitiatorAlias = str;
	    strcpy(session->InitiatorAlias, ioctld->InitiatorAlias);
	    relogin = 1;
	    DEBUG_INIT("iSCSI: session %p updated InitiatorAlias at %lu\n",
		       session, jiffies);
	} else {
	    printk("iSCSI: session %p failed to change InitiatorAlias from "
		   "%s to %s\n",
		   session, session->InitiatorAlias, ioctld->InitiatorAlias);
	    up(&session->config_mutex);
	    return 0;
	}
    }

    /* transactional (all-or-nothing) update of the auth config */
    if (ioctld->username_in[0] || ioctld->password_length_in)
	bidirectional = 1;

    /* cases:
     * 1) no new or current value (unchanged), NULL, NULL
     * 2) new but no current value (transactional update)  NULL, *
     * 3) no new but current value (transactional delete) *, NULL
     * 4) new and current value are different (transactional update) *,*
     * 5) new and current value are the same (unchanged) * == *
     */
    if (ioctld->username[0]) {
	if ((session->username == NULL)
	    || strcmp(ioctld->username, session->username)) {
	    /* update the username */
	    length = strlen(ioctld->username);
	    if ((username = kmalloc(length + 1, GFP_ATOMIC))) {
		strncpy(username, ioctld->username, length);
		username[length] = '\0';
	    } else {
		printk("iSCSI: session %p failed to change outgoing username\n",
		       session);
		ret = -ENOMEM;
		auth_update_failed = 1;
	    }
	} else {
	    /* they're the same, just keep the current one */
	    username = session->username;
	}
    }

    if (ioctld->password_length) {
	if ((session->password == NULL)
	    || (session->password_length != ioctld->password_length)
	    || memcmp(ioctld->password, session->password,
		      session->password_length)) {
	    /* update the existing password */
	    if ((password = kmalloc(ioctld->password_length + 1, GFP_ATOMIC))) {
		password_length = ioctld->password_length;
		memcpy(password, ioctld->password, password_length);
		password[password_length] = '\0';
	    } else {
		printk("iSCSI: session %p failed to change outgoing password\n",
		       session);
		password_length = 0;
		ret = -ENOMEM;
		auth_update_failed = 1;
	    }
	} else {
	    /* they're the same, just keep the current one */
	    password = session->password;
	}
    }

    if (ioctld->username_in[0]) {
	if ((session->username_in == NULL)
	    || strcmp(ioctld->username_in, session->username_in)) {
	    /* update the username */
	    length = strlen(ioctld->username_in);
	    if ((username_in = kmalloc(length + 1, GFP_ATOMIC))) {
		strncpy(username_in, ioctld->username_in, length);
		username_in[length] = '\0';
	    } else {
		printk("iSCSI: session %p failed to change incoming username\n",
		       session);
		ret = -ENOMEM;
		auth_update_failed = 1;
	    }
	} else {
	    /* they're the same, just keep the current one */
	    username_in = session->username_in;
	}
    }

    if (ioctld->password_length_in) {
	if ((session->password_in == NULL)
	    || (session->password_length_in != ioctld->password_length_in)
	    || memcmp(ioctld->password_in, session->password_in,
		      session->password_length_in)) {
	    /* update the existing password */
	    if ((password_in =
		 kmalloc(ioctld->password_length_in + 1, GFP_ATOMIC))) {
		password_length = ioctld->password_length_in;
		memcpy(password_in, ioctld->password_in, password_length_in);
		password[password_length_in] = '\0';
	    } else {
		printk("iSCSI: session %p failed to change incoming password\n",
		       session);
		password_length_in = 0;
		ret = -ENOMEM;
		auth_update_failed = 1;
	    }
	} else {
	    /* they're the same, just keep the current one */
	    password_in = session->password_in;
	}
    }

    if (!auth_update_failed) {
	/* update to the new auth config */
	session->bidirectional_auth = bidirectional;

	if (username != session->username) {
	    /* update current */
	    if (session->username) {
		memset(session->username, 0, strlen(session->username));
		kfree(session->username);
	    }
	    session->username = username;
	    if (username)
		DEBUG_INIT("iSCSI: session %p updated outgoing username to "
			   "%s at %lu\n", session, session->username, jiffies);
	}

	if (password != session->password) {
	    /* update current */
	    if (session->password) {
		memset(session->password, 0, session->password_length);
		kfree(session->password);
	    }
	    session->password = password;
	    session->password_length = password_length;
	    if (password)
		DEBUG_INIT("iSCSI: session %p updated outgoing password at "
			   "%lu\n", session, jiffies);
	}

	if (username_in != session->username_in) {
	    /* update current */
	    if (session->username_in) {
		memset(session->username_in, 0, strlen(session->username_in));
		kfree(session->username_in);
	    }
	    session->username_in = username_in;
	    if (username_in)
		DEBUG_INIT("iSCSI: session %p updated incoming username to %s "
			   "at %lu\n", session, session->username_in, jiffies);
	}

	if (password_in != session->password_in) {
	    /* update current */
	    if (session->password) {
		memset(session->password, 0, session->password_length);
		kfree(session->password);
	    }
	    session->password_in = password_in;
	    session->password_length_in = password_length_in;
	    if (password_in)
		DEBUG_INIT("iSCSI: session %p updated incoming password at "
			   "%lu\n", session, jiffies);
	}
    } else {
	/* update failed, free anything we allocated */
	if (username)
	    kfree(username);
	if (password)
	    kfree(password);
	if (username_in)
	    kfree(username_in);
	if (password_in)
	    kfree(password_in);
    }

    /* iscsi_establish_session will ensure we have auth structures,
     * or error out if bidi auth is required and we can't do authentication.
     */

    up(&session->config_mutex);

    /* the portals are guarded by a spinlock instead of the config
     * mutex, so that we can request portal changes while a login is
     * occuring.
     */
    spin_lock(&session->portal_lock);

    /* replace the portals */
    old_portals = session->portals;
    session->portals = portals;
    session->num_portals = ioctld->num_portals;
    session->requested_portal = UINT_MAX;	/* cancel any request, 
						 * since the portals may
						 * have changed 
						 */
    session->fallback_portal = UINT_MAX;	/* cancel any fallback, 
						 * since the portals may 
						 * have changed 
						 */
    session->portal_failover = ioctld->portal_failover;
    memset(session->preferred_portal, 0, sizeof (session->preferred_portal));
    memset(session->preferred_subnet, 0, sizeof (session->preferred_subnet));
    session->preferred_portal_bitmap = 0;
    session->preferred_subnet_bitmap = 0;
    session->tried_portal_bitmap = 0;

    if (ioctld->preferred_portal && strlen(ioctld->preferred_portal)) {
	memcpy(session->preferred_portal, ioctld->preferred_portal,
	       strlen(ioctld->preferred_portal));
	set_preferred_portal_bitmap(session);
    }

    if (ioctld->preferred_subnet && strlen(ioctld->preferred_subnet)) {
	memcpy(session->preferred_subnet, ioctld->preferred_subnet,
	       strlen(ioctld->preferred_subnet));
	session->preferred_subnet_mask = ioctld->preferred_subnet_mask;
	set_preferred_subnet_bitmap(session);
    }

    printk("iSCSI: bus %d target %d = %s\n", ioctld->iscsi_bus,
	   ioctld->target_id, ioctld->TargetName);
    for (p = 0; p < session->num_portals; p++) {
	/* FIXME: IPv6 */
	printk("iSCSI: bus %d target %d portal %u = address %u.%u.%u.%u "
	       "port %d group %d\n",
	       ioctld->iscsi_bus, ioctld->target_id, p,
	       portals[p].ip_address[0], portals[p].ip_address[1],
	       portals[p].ip_address[2], portals[p].ip_address[3],
	       portals[p].port, portals[p].tag);
	if (portals[p].disk_command_timeout > 0)
	    need_to_start_timer = 1;
    }

    old_portal = &old_portals[session->current_portal];

    /* figure out which new portal (if any) we're currently
     * connected/connecting to 
     */
    for (p = 0; p < ioctld->num_portals; p++) {
	if (same_network_portal(&portals[p], old_portal)) {
	    new_portal = &portals[p];

	    if (session->current_portal == p) {
		DEBUG_INIT("iSCSI: bus %d target %d staying with portal %u\n",
			   session->iscsi_bus, session->target_id, p);
	    } else if (session->portal_failover) {
		printk("iSCSI: bus %d target %d portals have changed, "
		       "old portal %u is new portal %u\n",
		       session->iscsi_bus, session->target_id,
		       session->current_portal, p);
		/* request the new portal if we decide we need to relogin */
		requested_portal = p;
		/* but reset the current portal in case we
		 * don't need to relogin 
		 */
		session->current_portal = p;
	    }
	}
    }

    if ((new_portal == NULL) && session->portal_failover) {
	/* no matching new portal. try to find a portal in
	 * the same portal group 
	 */
	for (p = 0; p < ioctld->num_portals; p++) {
	    if (portals[p].tag == old_portal->tag) {
		printk("iSCSI: bus %d target %d portals have changed, "
		       "session %p switching to portal %u in group %u\n",
		       session->iscsi_bus, session->target_id, session, p,
		       portals[p].tag);
		requested_portal = p;
		new_portal = &portals[p];
		relogin = 1;
	    }
	}
    }

    if ((new_portal == NULL) && session->portal_failover) {
	/* we couldn't find a portal in the same portal group.
	 * if we can do a clean logout, we can login to any portal.
	 * if we can't logout, we risk command reordering if we login 
	 * to a different group.
	 */
	new_portal = &portals[0];
	requested_portal = 0;
	printk("iSCSI: bus %d target %d portals have changed, failed to "
	       "find a new portal in portal group %u, session %p trying "
	       "portal 0 group %u\n",
	       session->iscsi_bus, session->target_id, old_portal->tag, session,
	       new_portal->tag);
	/* FIXME: if the logout fails, we'll need to error out somehow. */
	relogin = 1;
    }

    if (session->portal_failover) {
	/* the driver timeouts can change on the fly, with no relogin */
	if (new_portal->login_timeout != old_portal->login_timeout)
	    session->login_timeout = new_portal->login_timeout;

	if (new_portal->auth_timeout != old_portal->auth_timeout)
	    session->auth_timeout = new_portal->auth_timeout;

	if (new_portal->active_timeout != old_portal->active_timeout)
	    session->active_timeout = new_portal->active_timeout;

	if (new_portal->idle_timeout != old_portal->idle_timeout)
	    session->idle_timeout = new_portal->idle_timeout;

	if (new_portal->ping_timeout != old_portal->ping_timeout) {
	    session->ping_timeout = new_portal->ping_timeout;
	    relogin = 1;	/* because we ask the target to use 
				 * it as well with com.cisco.PingTimeout 
				 */
	}

	if (new_portal->abort_timeout != old_portal->abort_timeout)
	    session->abort_timeout = new_portal->abort_timeout;

	if (new_portal->reset_timeout != old_portal->reset_timeout)
	    session->reset_timeout = new_portal->reset_timeout;

	/* FIXME: this should probably be per-session rather than per-portal */
	if (new_portal->replacement_timeout != old_portal->replacement_timeout)
	    session->replacement_timeout = new_portal->replacement_timeout;

	/* FIXME: get the scsi_cmnd_lock when setting these? */
	if (new_portal->disk_command_timeout !=
	    old_portal->disk_command_timeout)
	    session->disk_command_timeout = new_portal->disk_command_timeout;

	/* the iSCSI op params need a relogin to change */
	if (new_portal->InitialR2T != old_portal->InitialR2T)
	    relogin = 1;

	if (new_portal->ImmediateData != old_portal->ImmediateData)
	    relogin = 1;

	if (new_portal->MaxRecvDataSegmentLength !=
	    old_portal->MaxRecvDataSegmentLength)
	    relogin = 1;

	if (new_portal->FirstBurstLength != old_portal->FirstBurstLength)
	    relogin = 1;

	if (new_portal->MaxBurstLength != old_portal->MaxBurstLength)
	    relogin = 1;

	if (new_portal->DefaultTime2Wait != old_portal->DefaultTime2Wait)
	    relogin = 1;

	if (new_portal->DefaultTime2Retain != old_portal->DefaultTime2Retain)
	    relogin = 1;

	if (new_portal->HeaderDigest != old_portal->HeaderDigest)
	    relogin = 1;

	if (new_portal->DataDigest != old_portal->DataDigest)
	    relogin = 1;

	/* the TCP connection settings need a relogin */
	if (new_portal->tcp_window_size != old_portal->tcp_window_size)
	    relogin = 1;

	/* FIXME: TCP type_of_service */

    } else {
	relogin = 0;
	q = session->portals;
	for (portal = 0; portal < session->num_portals; portal++) {
	    if (memcmp
		(q[portal].ip_address, session->ip_address,
		 session->ip_length) == 0) {
		found = 1;
		break;
	    }
	}
	if (!found) {
	    iscsi_terminate_session(session);
	    drop_reference(session);
	    return 1;
	}
    }

    if (relogin) {
	/* if we have to relogin, place any portal request decided on earlier */
	session->requested_portal = requested_portal;
	session->fallback_portal = UINT_MAX;
    }

    spin_unlock(&session->portal_lock);

    kfree(old_portals);

    smp_mb();

    if (relogin) {
	if (test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	    spin_lock(&session->task_lock);
	    printk("iSCSI: bus %d target %d configuration updated at %lu, "
		   "session %p to %s must logout\n",
		   session->iscsi_bus, session->target_id, jiffies, session,
		   session->log_name);
	    iscsi_request_logout(session, 3, session->active_timeout);
	    spin_unlock(&session->task_lock);
	} else {
	    printk("iSCSI: bus %d target %d configuration updated at %lu "
		   "while session %p to %s is not established\n",
		   session->iscsi_bus, session->target_id, jiffies, session,
		   session->log_name);
	}
    } else {
	printk("iSCSI: bus %d target %d configuration updated at %lu, "
	       "session %p to %s does not need to logout\n",
	       session->iscsi_bus, session->target_id, jiffies, session,
	       session->log_name);
    }

    if (need_to_start_timer)
	start_timer(session);

    return 1;
}

static iscsi_session_t *
allocate_session(iscsi_session_ioctl_t * ioctld, iscsi_portal_info_t * portals)
{
    iscsi_session_t *session =
	(iscsi_session_t *) kmalloc(sizeof (*session), GFP_KERNEL);
    size_t length;
    int pp;

    if (session == NULL) {
	printk("iSCSI: bus %d target %d cannot allocate new session "
	       "(size %Zu) at %lu\n",
	       ioctld->iscsi_bus, ioctld->target_id, sizeof (*session),
	       jiffies);
	return NULL;
    }

    memset(session, 0, sizeof (*session));
    atomic_set(&session->refcount, 1);
    DEBUG_INIT("iSCSI: bus %d target %d allocated session %p (size %Zu) "
	       "at %lu\n",
	       ioctld->iscsi_bus, ioctld->target_id, session, sizeof (*session),
	       jiffies);

    /* an InitiatorName is required */
    length = strlen(ioctld->InitiatorName);
    if (length) {
	if ((session->InitiatorName = kmalloc(length + 1, GFP_KERNEL))) {
	    strncpy(session->InitiatorName, ioctld->InitiatorName, length);
	    session->InitiatorName[length] = '\0';
	} else {
	    printk("iSCSI: bus %d target %d cannot allocate InitiatorName "
		   "at %lu\n", ioctld->iscsi_bus, ioctld->target_id, jiffies);
	    delete_session(session);
	    return NULL;
	}
    } else {
	printk("iSCSI: bus %d target %d has no InitiatorName at %lu\n",
	       ioctld->iscsi_bus, ioctld->target_id, jiffies);
	delete_session(session);
	return NULL;
    }

    /* an InitiatorAlias is optional */
    length = strlen(ioctld->InitiatorAlias);
    if (length && (session->InitiatorAlias = kmalloc(length + 1, GFP_KERNEL))) {
	strncpy(session->InitiatorAlias, ioctld->InitiatorAlias, length);
	session->InitiatorAlias[length] = '\0';
    }

    memcpy(session->isid, ioctld->isid, sizeof (session->isid));

    if (this_is_iscsi_boot) {
	if (strcmp(iscsi_inbp_info.targetstring, ioctld->TargetName)) {
	    session->this_is_root_disk = 0;
	    DEBUG_INIT("\nMaking session->this_is_root_disk = 0 for %s\n",
		       ioctld->TargetName);
	} else {
	    session->this_is_root_disk = 1;
	    DEBUG_INIT("\nMaking session->this_is_root_disk = 1 for %s\n",
		       ioctld->TargetName);
	}
    }

    strncpy(session->TargetName, ioctld->TargetName,
	    sizeof (session->TargetName));
    session->TargetName[sizeof (session->TargetName) - 1] = '\0';
    session->log_name = session->TargetName;
    session->TargetAlias[0] = '\0';	/* none unless declared by the target */

    session->num_auth_buffers = 0;
    session->auth_client_block = NULL;
    session->auth_recv_string_block = NULL;
    session->auth_send_string_block = NULL;
    session->auth_recv_binary_block = NULL;
    session->auth_send_binary_block = NULL;

    /* allocate authentication info */
    if (ioctld->username_in[0] || ioctld->password_length_in) {
	/* we must authenticate the target or refuse to login */
	session->bidirectional_auth = 1;
    } else {
	session->bidirectional_auth = 0;	/* authentication is optional */
    }

    /* FIXME: should we fail the ioctl if the allocation fails? */
    if ((length = strlen(ioctld->username))) {
	if ((session->username = kmalloc(length + 1, GFP_KERNEL))) {
	    strncpy(session->username, ioctld->username, length);
	    session->username[length] = '\0';
	} else {
	    printk("iSCSI: bus %d target %d failed to allocate outgoing "
		   "username at %lu\n",
		   ioctld->iscsi_bus, ioctld->target_id, jiffies);
	    delete_session(session);
	    return NULL;
	}
    }

    if (ioctld->password_length) {
	if ((session->password =
	     kmalloc(ioctld->password_length + 1, GFP_KERNEL))) {
	    memcpy(session->password, ioctld->password,
		   ioctld->password_length);
	    session->password_length = ioctld->password_length;
	    session->password[session->password_length] = '\0';
	} else {
	    printk("iSCSI: bus %d target %d failed to allocate outgoing "
		   "password at %lu\n",
		   ioctld->iscsi_bus, ioctld->target_id, jiffies);
	    delete_session(session);
	    return NULL;
	}
    }

    if ((length = strlen(ioctld->username_in))) {
	if ((session->username_in = kmalloc(length + 1, GFP_KERNEL))) {
	    strncpy(session->username_in, ioctld->username_in, length);
	    session->username_in[length] = '\0';
	} else {
	    printk("iSCSI: bus %d target %d failed to allocate incoming "
		   "username at %lu\n",
		   ioctld->iscsi_bus, ioctld->target_id, jiffies);
	    delete_session(session);
	    return NULL;
	}
    }

    if (ioctld->password_length_in) {
	if ((session->password_in =
	     kmalloc(ioctld->password_length_in + 1, GFP_KERNEL))) {
	    memcpy(session->password_in, ioctld->password_in,
		   ioctld->password_length_in);
	    session->password_length_in = ioctld->password_length_in;
	    session->password_in[session->password_length_in] = '\0';
	} else {
	    printk("iSCSI: bus %d target %d failed to allocate incoming "
		   "password at %lu\n",
		   ioctld->iscsi_bus, ioctld->target_id, jiffies);
	    delete_session(session);
	    return NULL;
	}
    }

    /* the auth structures are allocated in iscsi_establish_session, so that
     * any allocation failures are retried automatically.
     */

#if 0
    if (session->username)
	printk("iSCSI: session %p username %s\n", session, session->username);

    if (session->password)
	printk("iSCSI: session %p password %s\n", session, session->password);

    if (session->username_in)
	printk("iSCSI: session %p username_in %s\n", session,
	       session->username_in);

    if (session->password_in)
	printk("iSCSI: session %p password_in %s\n", session,
	       session->password_in);
#endif

    /* initialize the session structure */
    session->socket = NULL;

    session->config_number = ioctld->config_number;

    spin_lock_init(&session->portal_lock);
    session->num_portals = 0;
    session->portals = NULL;
    session->auth_failures = 0;
    session->portal_failover = 1;
    session->current_portal = 0;
    session->requested_portal = UINT_MAX;
    session->fallback_portal = UINT_MAX;
    session->portal_group_tag = -1;
    memset(session->preferred_portal, 0, sizeof (session->preferred_portal));
    memset(session->preferred_subnet, 0, sizeof (session->preferred_subnet));
    session->preferred_portal_bitmap = 0;
    session->preferred_subnet_bitmap = 0;
    session->tried_portal_bitmap = 0;

    spin_lock_init(&session->scsi_cmnd_lock);
    session->retry_cmnd_head = session->retry_cmnd_tail = NULL;
    atomic_set(&session->num_retry_cmnds, 0);
    session->scsi_cmnd_head = session->scsi_cmnd_tail = NULL;
    atomic_set(&session->num_cmnds, 0);
    session->deferred_cmnd_head = session->deferred_cmnd_tail = NULL;
    session->num_deferred_cmnds = 0;

    sema_init(&session->probe_sem, 0);	/* the first down should block */
    session->probe_order = ioctld->probe_order;
    memcpy(session->luns_allowed, ioctld->lun_bitmap,
	   sizeof (session->luns_allowed));

    sema_init(&session->config_mutex, 0);	/* the first down should block */

    spin_lock_init(&session->task_lock);
    session->arrival_order.head = session->arrival_order.tail = NULL;
    session->tx_tasks.head = session->tx_tasks.tail = NULL;
    atomic_set(&session->num_active_tasks, 0);
    session->preallocated_task = NULL;

    init_waitqueue_head(&session->tx_wait_q);
    init_waitqueue_head(&session->tx_blocked_wait_q);
    init_waitqueue_head(&session->login_wait_q);

    init_timer(&session->busy_task_timer);
    init_timer(&session->busy_command_timer);
    init_timer(&session->immediate_reject_timer);
    init_timer(&session->retry_timer);

    /* save the portal info, and pick which portal to start with */
    session->update_address = &update_address;
    session->portals = portals;
    session->num_portals = ioctld->num_portals;
    session->portal_failover = ioctld->portal_failover;

    if (ioctld->preferred_portal && strlen(ioctld->preferred_portal)) {
	memcpy(session->preferred_portal, ioctld->preferred_portal,
	       strlen(ioctld->preferred_portal));
	set_preferred_portal_bitmap(session);
    }

    if (ioctld->preferred_subnet && strlen(ioctld->preferred_subnet)) {
	memcpy(session->preferred_subnet, ioctld->preferred_subnet,
	       strlen(ioctld->preferred_subnet));
	session->preferred_subnet_mask = ioctld->preferred_subnet_mask;
	set_preferred_subnet_bitmap(session);
    }

    pp = get_appropriate_portal(session);
    if (pp < 0)
	set_portal(session, 0U);
    else
	set_portal(session, pp);

    session->channel = ioctld->iscsi_bus % ISCSI_MAX_CHANNELS_PER_HBA;
    session->iscsi_bus = ioctld->iscsi_bus;
    session->target_id = ioctld->target_id;

    session->itt = ioctld->target_id;	/* to make reading login traces easier */
    session->generation = 0;
    session->ever_established = 0;
    session->session_alive = 0;
    session->time2wait = -1;
    session->logout_itt = RSVD_TASK_TAG;
    session->mgmt_itt = RSVD_TASK_TAG;

    session->ignore_lun = -2;
    session->reject_lun = -2;
    session->fake_status_lun = -2;
    session->lun_being_probed = -1;

    /* in case the session never comes up */
    session->session_drop_time = jiffies;

    smp_mb();

    return session;
}

int
start_session_threads(iscsi_session_t * session)
{
    pid_t rx_pid, tx_pid;

    /* start a tx thread */
    DEBUG_INIT("iSCSI: session %p about to start tx and rx threads at %lu\n",
	       session, jiffies);
    atomic_inc(&session->refcount);
    tx_pid =
	kernel_thread(iscsi_tx_thread, (void *) session,
		      CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
    if (tx_pid > 0) {
	DEBUG_INIT("iSCSI: session %p started tx thread %u at %lu\n", session,
		   tx_pid, jiffies);
    } else {
	printk("iSCSI: session %p failed to start tx thread, terminating "
	       "session\n", session);
	atomic_dec(&session->refcount);	/* the thread isn't actually using it */
	iscsi_terminate_session(session);
	drop_reference(session);
	return -EAGAIN;
    }

    /* start an rx thread */
    atomic_inc(&session->refcount);
    rx_pid =
	kernel_thread(iscsi_rx_thread, (void *) session,
		      CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
    if (rx_pid > 0) {
	DEBUG_INIT("iSCSI: session %p started rx thread %u at %lu\n", session,
		   rx_pid, jiffies);
    } else {
	printk("iSCSI: session %p failed to start rx thread, terminating "
	       "session\n", session);
	atomic_dec(&session->refcount);	/* the thread isn't actually using it */
	iscsi_terminate_session(session);
	drop_reference(session);
	return -EAGAIN;
    }

    DEBUG_INIT("iSCSI: session %p waiting for rx %d and tx %d at %lu\n",
	       session, rx_pid, tx_pid, jiffies);

    /* wait for the threads to start */
    while ((session->tx_pid == 0) || (session->rx_pid == 0)) {
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(10));
	if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	    printk("iSCSI: session %p terminating, failed to start threads "
		   "at %lu\n", session, jiffies);
	    return -EAGAIN;
	} else if (signal_pending(current)) {
	    iscsi_terminate_session(session);
	    printk("iSCSI: session %p thread start terminated, returning "
		   "at %lu\n", session, jiffies);
	    return -EAGAIN;
	}
	smp_mb();
    }

    DEBUG_INIT("iSCSI: session %p started tx_pid %d, rx_pid %d\n", session,
	       session->tx_pid, session->rx_pid);

    if (signal_pending(current))
	return -EINTR;
    else
	return 0;
}

/* do timer processing for one session, and return the length of time
 * (in jiffies) til this session needs to be checked again.
 */
static unsigned long
check_session_timeouts(iscsi_session_t * session)
{
    unsigned long timeout;
    unsigned long session_timeout = 0;
    pid_t pid;

    if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	/* check login phase timeouts */
	if (session->login_phase_timer) {
	    session_timeout = session->login_phase_timer;
	    if (time_before_eq(session_timeout, jiffies)) {
		printk("iSCSI: login phase for session %p (rx %d, tx %d) "
		       "timed out at %lu, timeout was set for %lu\n",
		       session, session->rx_pid, session->tx_pid, jiffies,
		       session_timeout);
		session->login_phase_timer = 0;
		smp_mb();
		session_timeout = 0;
		if ((pid = session->rx_pid))
			kill_proc(pid, SIGHUP, 1);
	    }
	}
    } else {
	/* check full-feature phase timeouts. */
	if (atomic_read(&session->num_active_tasks))
	    timeout = session->active_timeout;
	else
	    timeout = session->idle_timeout;

	if (timeout) {
	    if (session->ping_timeout &&
		time_before_eq(session->last_rx + (timeout * HZ) +
			       (session->ping_timeout * HZ), jiffies)) {
		/* should have received something by now, kill the connection */
		if ((session->last_kill == 0)
		    || time_before_eq(session->last_kill + HZ, jiffies)) {

		    session->last_kill = jiffies;

		    printk("iSCSI: %lu second timeout expired for session "
			   "%p, rx %lu, ping %lu, now %lu\n",
			   timeout + session->ping_timeout, session,
			   session->last_rx, session->last_ping, jiffies);

		    iscsi_drop_session(session);

		    session_timeout = jiffies + HZ;
		} else
		    session_timeout = 0;
	    } else
		if (time_before_eq(session->last_rx + (timeout * HZ), jiffies))
	    {

		if (time_before_eq(session->last_ping, session->last_rx)) {
		    /* send a ping to try to provoke some traffic */
		    DEBUG_FLOW("iSCSI: timer queuing ping for session %p, "
			       "rx %lu, ping %lu, now %lu\n",
			       session, session->last_rx, session->last_ping,
			       jiffies);
		    session->last_ping = jiffies - 1;

		    wake_tx_thread(TX_PING, session);
		}
		session_timeout =
		    session->last_rx + (timeout * HZ) +
		    (session->ping_timeout * HZ);
	    } else {
		if (atomic_read(&session->num_active_tasks)) {
		    session_timeout =
			session->last_rx + (session->active_timeout * HZ);
		} else {
		    unsigned long active_timeout, idle_timeout;

		    /* session is idle, but may become
		     * active without the timer being
		     * notified, so use smaller of (now +
		     * active_timeout, last_rx +
		     * idle_timeout)
		     */
		    idle_timeout =
			session->last_rx + (session->idle_timeout * HZ);
		    active_timeout = jiffies + (session->active_timeout * HZ);
		    if (time_before_eq(idle_timeout, active_timeout)) {
			session_timeout = idle_timeout;
		    } else {
			session_timeout = active_timeout;
		    }
		}
	    }
	}

	/* we limit how long we'll wait for a task mgmt
	 * response, to avoid blocking forever in error
	 * recovery.
	 */
	if (session->task_mgmt_response_deadline &&
	    time_before_eq(session->task_mgmt_response_deadline, jiffies)) {
	    printk("iSCSI: session %p task mgmt %u response timeout at %lu\n",
		   session, session->mgmt_itt, jiffies);
	    session->task_mgmt_response_deadline = 0;
	    smp_mb();

	    /* tell the tx thread that the task mgmt PDU
	     * timed out, and have it escalate error
	     * recovery 
	     */
	    set_bit(SESSION_TASK_MGMT_TIMEDOUT, &session->control_bits);
	    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);

	    if ((session_timeout == 0)
		|| time_before(session->task_mgmt_response_deadline,
			       session_timeout))
		session_timeout = session->task_mgmt_response_deadline;
	}

	/* there's a separate deadline for responses to
	 * requested resets, so that error recovery and
	 * reset requests don't get in each other's way.
	 */
	if (session->reset_response_deadline &&
	    time_before_eq(session->reset_response_deadline, jiffies)) {
	    session->reset_response_deadline = 0;
	    smp_mb();

	    if (test_and_clear_bit
		(SESSION_RESET_REQUESTED, &session->control_bits)) {
		/* FIXME: what should we do when a requested reset times out?
		 * for now, just give up on the reset and drop the session.  The
		 * target should always reply, so we probably have a network
		 * problem.
		 */
		printk("iSCSI: session %p timed out waiting for mgmt %u "
		       "reset response, dropping session at %lu\n",
		       session, session->warm_reset_itt, jiffies);
		iscsi_drop_session(session);
		return 0;
	    }

	    if ((session_timeout == 0)
		|| time_before(session->task_mgmt_response_deadline,
			       session_timeout))
		session_timeout = session->task_mgmt_response_deadline;
	}

	if (test_bit(SESSION_LOGOUT_REQUESTED, &session->control_bits)) {
	    /* we're waiting for tasks to complete before
	     * logging out.  no need to check the CmdSN
	     * window, since we won't be starting any more
	     * tasks.
	     */
	    if (time_before_eq(session->logout_response_deadline, jiffies)) {
		/* passed the deadline for a logout
		 * response, just drop the session 
		 */
		printk("iSCSI: session %p logout response timeout at %lu, "
		       "dropping session\n", session, jiffies);
		session->logout_response_deadline = 0;
		session->logout_deadline = 0;
		smp_mb();
		iscsi_drop_session(session);
	    } else if (time_before_eq(session->logout_deadline, jiffies)) {
		/* send a logout */
		DEBUG_INIT("iSCSI: session %p logout deadline reached at %lu\n",
			   session, jiffies);
		session->logout_deadline = 0;
		smp_mb();
		wake_tx_thread(TX_LOGOUT, session);
		if ((session_timeout == 0)
		    || time_before(session->logout_response_deadline,
				   session_timeout))
		    session_timeout = session->logout_response_deadline;
	    } else {
		if ((session_timeout == 0)
		    || time_before(session->logout_deadline, session_timeout))
		    session_timeout = session->logout_deadline;
	    }
	} else if (test_bit(SESSION_WINDOW_CLOSED, &session->control_bits) &&
		   time_before_eq(session->last_window_check + HZ, jiffies)) {
	    /* command window closed, ping once a second to ensure we find out
	     * when it re-opens.  Target should send us an update when it does,
	     * but we're not very trusting of target correctness.
	     */
	    session->last_window_check = jiffies;
	    DEBUG_QUEUE("iSCSI: session %p command window closed, ExpCmdSN %u, "
			"MaxCmdSN %u, polling target at %lu\n",
			session, session->ExpCmdSn, session->MaxCmdSn, jiffies);

	    /* request a window update from the target with Nops */
	    wake_tx_thread(TX_PING, session);

	    if ((session_timeout == 0)
		|| time_before(session->last_window_check + HZ,
			       session_timeout))
		session_timeout = session->last_window_check + HZ;
	}
    }

    return session_timeout;
}

/*
 *  FIXME: it'd probably be cleaner to move the timeout logic to the rx thread.
 *         The only danger is if the rx thread somehow blocks indefinately.
 *         Doing timeouts here makes sure the timeouts get checked, at the
 *         cost of having this code constantly loop.
 */
static int
iscsi_timer_thread(void *vtaskp)
{
    iscsi_session_t *session;
    iscsi_hba_t *hba;

    /* become a child of init, and abandon any user space resources */
    sprintf(current->comm, "iscsi-timer");
    iscsi_daemonize();

    iscsi_timer_pid = current->pid;
    smp_mb();
    DEBUG_INIT("iSCSI: timer pid %d starting at %lu\n", iscsi_timer_pid,
	       jiffies);

    LOCK_SIGNALS();
    /* Block all signals except SIGKILL */
    siginitsetinv(&current->blocked, sigmask(SIGKILL));
    RECALC_PENDING_SIGNALS;
    UNLOCK_SIGNALS();

    /* wait for the module to initialize */
    while (test_bit(0, &init_module_complete) == 0) {
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(10));
	if (signal_pending(current)) {
	    iscsi_timer_running = 0;
	    smp_mb();
	    return 0;
	}
    }

    DEBUG_INIT("iSCSI: timer waiting for HBA at %lu\n", jiffies);
    while (!signal_pending(current)) {
	spin_lock(&iscsi_hba_list_lock);
	hba = iscsi_hba_list;
	spin_unlock(&iscsi_hba_list_lock);

	if (hba)
	    break;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(10));
    }

    DEBUG_INIT("iSCSI: timer looping over HBAs at %lu\n", jiffies);

  continue_timer_thread:

    while (!signal_pending(current)) {
	unsigned long next_timeout = jiffies + (5 * HZ);

	spin_lock(&iscsi_hba_list_lock);
	hba = iscsi_hba_list;
	while (hba) {
	    DECLARE_NOQUEUE_FLAGS;

	    SPIN_LOCK_NOQUEUE(&hba->session_lock);
	    session = hba->session_list_head;
	    while (session) {
		unsigned long session_timeout = 0;

		if (LOG_ENABLED(ISCSI_LOG_ALLOC))
		    printk("iSCSI: session %p, rx %5u, tx %5u, %u luns, %3u r, "
			   "%3u d, %3u n, %3u t, bits 0x%08lx at %lu\n",
			   session, session->rx_pid, session->tx_pid,
			   session->num_luns,
			   atomic_read(&session->num_retry_cmnds),
			   session->num_deferred_cmnds,
			   atomic_read(&session->num_cmnds),
			   atomic_read(&session->num_active_tasks),
			   session->control_bits, jiffies);

		session_timeout = check_session_timeouts(session);

		/* find the earliest timeout that might
		 * occur, so that we know how long to
		 * sleep 
		 */
		if (session_timeout && time_before_eq(session_timeout, jiffies))
		    printk("iSCSI: ignoring session timeout %lu at %lu, "
			   "last rx %lu, for session %p\n",
			   session_timeout, jiffies, session->last_rx, session);
		else if (session_timeout
			 && time_before(session_timeout, next_timeout))
		    next_timeout = session_timeout;

		session = session->next;
	    }
	    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

	    if (LOG_ENABLED(ISCSI_LOG_ALLOC))
		printk("iSCSI: timer - host %d can_queue %d at %lu\n",
		       hba->host->host_no, hba->host->can_queue, jiffies);

	    hba = hba->next;
	}
	spin_unlock(&iscsi_hba_list_lock);

	/* possibly start LUN probing */
	if (iscsi_lun_probe_start) {
	    if (time_before_eq(iscsi_lun_probe_start, jiffies)) {
		iscsi_possibly_start_lun_probing();
	    } else if (time_before_eq(iscsi_lun_probe_start, next_timeout)) {
		next_timeout = iscsi_lun_probe_start;
	    }
	}

	/* sleep for a while */
	if (time_before(jiffies, next_timeout)) {
	    unsigned long sleep;

	    /* sleep til the next time a timeout might
	     * occur, and handle jiffies wrapping 
	     */
	    if (next_timeout < jiffies)
		sleep = (ULONG_MAX - jiffies + next_timeout);
	    else
		sleep = (next_timeout - jiffies);
	    DEBUG_FLOW("iSCSI: timer sleeping for %lu jiffies, now %lu, "
		       "next %lu, HZ %u\n", sleep, jiffies, next_timeout, HZ);

	    set_current_state(TASK_INTERRUPTIBLE);
	    schedule_timeout(sleep);
	    if (signal_pending(current))
		goto finished;
	} else {
	    /* this should never happen, but make sure we
	     * block for at least a little while if it does
	     * somehow, otherwise it'll lock up the machine
	     * and be impossible to debug what went wrong.
	     */
	    DEBUG_FLOW("iSCSI: timer forced to sleep, now %lu, next %lu, "
		       "HZ %u\n", jiffies, next_timeout, HZ);
	    set_current_state(TASK_INTERRUPTIBLE);
	    schedule_timeout(1);
	    if (signal_pending(current))
		goto finished;
	}
    }

  finished:
    /* timer finished */

    if ((this_is_iscsi_boot) && (!iscsi_system_is_rebooting)) {
	printk("\niSCSI: timer_thread got signalled\n");
	flush_signals(current);
	goto continue_timer_thread;
    }

    DEBUG_INIT("iSCSI: timer leaving kernel at %lu\n", jiffies);

    set_current_state(TASK_RUNNING);

    iscsi_timer_running = 0;
    iscsi_timer_pid = 0;
    smp_mb();

    return 0;
}

/* shutdown every session on the HBA */
static int
iscsi_shutdown_hba(iscsi_hba_t * hba)
{
    int num_sessions = 0;
    iscsi_session_t *session;
    DECLARE_NOQUEUE_FLAGS;

    /* FIXME: we lose info on LUNs probed when this happens.  After
     * this, the kernel module must be reloaded in order for another
     * LUN probe to work correctly.  Just restarting the daemon causes
     * LUN probe attempts, but the kernel's scsi.c will detect that
     * the device is already on the HBA's device list and error out 
     * the add-single-device.  
     */

    /* ensure no more sessions get added to the HBA while
     * we're trying to shut it down 
     */
    set_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags);

    do {
	num_sessions = 0;

	SPIN_LOCK_NOQUEUE(&hba->session_lock);
	for (session = hba->session_list_head; session; session = session->next) {
	    num_sessions++;
	    set_bit(SESSION_TERMINATING, &session->control_bits);
	    if ((session->last_kill == 0)
		|| time_before_eq(session->last_kill + (5 * HZ), jiffies)) {
		session->last_kill = jiffies;
		iscsi_drop_session(session);
	    }
	}
	SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(20));

    } while (num_sessions);

    /* let sessions get added again */
    clear_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags);
    smp_mb();

    return 1;
}

static int
iscsi_shutdown(void)
{
    iscsi_hba_t *hba;
    iscsi_session_t *session;
    int num_sessions = 0;
    pid_t pid;
    DECLARE_NOQUEUE_FLAGS;

    /* terminate every session on every HBA */
    if (this_is_iscsi_boot && !iscsi_system_is_rebooting)
	printk("iSCSI: driver shutdown killing all sessions, except "
	       "session to ROOT disk\n");
    else
	printk("iSCSI: driver shutdown killing all sessions\n");

    do {
	num_sessions = 0;

	spin_lock(&iscsi_hba_list_lock);
	for (hba = iscsi_hba_list; hba; hba = hba->next) {
	    set_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags);
	    SPIN_LOCK_NOQUEUE(&hba->session_lock);
	    for (session = hba->session_list_head; session;
		 session = session->next) {
		if (!session->this_is_root_disk || iscsi_system_is_rebooting) {
		    num_sessions++;
		    set_bit(SESSION_TERMINATING, &session->control_bits);
		    if (session->last_kill == 0) {
			DEBUG_INIT("iSCSI: shutdown killing session %p with "
				   "refcount %u\n",
				   session, atomic_read(&session->refcount));
			session->last_kill = jiffies;
			/* FIXME: should we try to cleanly
			 * terminate the session the first
			 * time? May have locking issues
			 * with that 
			 */
			iscsi_drop_session(session);
		    } else
			if (time_before_eq
			    (session->last_kill + (5 * HZ), jiffies)) {
			printk("iSCSI: shutdown killing session %p with "
			       "refcount %u\n",
			       session, atomic_read(&session->refcount));
			session->last_kill = jiffies;
			iscsi_drop_session(session);
		    }
		}
	    }
	    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
	}
	spin_unlock(&iscsi_hba_list_lock);

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(20));
    } while (num_sessions);

    /* kill the timer */
    if (!this_is_iscsi_boot || iscsi_system_is_rebooting) {
	if ((pid = iscsi_timer_pid)) {
	    printk("iSCSI: driver shutdown killing timer %d\n", pid);
	    kill_proc(pid, SIGKILL, 1);
	}

	printk("iSCSI: driver shutdown waiting for timer to terminate\n");
	while (test_bit(0, &iscsi_timer_running)) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    schedule_timeout(MSECS_TO_JIFFIES(10));
	    if (signal_pending(current))
		return 0;
	}
    }

    /* reset LUN probing */
    iscsi_reset_lun_probing();

    /* let sessions get added again later */
    spin_lock(&iscsi_hba_list_lock);
    for (hba = iscsi_hba_list; hba; hba = hba->next) {
	clear_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags);
    }
    spin_unlock(&iscsi_hba_list_lock);

    if (!this_is_iscsi_boot || iscsi_system_is_rebooting)
	printk("iSCSI: driver shutdown complete at %lu\n", jiffies);

    return 1;
}

static int
iscsi_reboot_notifier_function(struct notifier_block *this, unsigned long code,
			       void *unused)
{
    mm_segment_t oldfs;

    if (code == SYS_DOWN) {
	DEBUG_INIT("\niSCSI: iscsi_reboot_notifier_function called with "
		   "code SYS_DOWN = 0x%lu\n", code);
    } else if (code == SYS_RESTART) {
	DEBUG_INIT("\niSCSI: iscsi_reboot_notifier_function called with "
		   "code SYS_RESTART = 0x%lu\n", code);
    } else if (code == SYS_HALT) {
	DEBUG_INIT("\niSCSI: iscsi_reboot_notifier_function called with "
		   "code SYS_HALT = 0x%lu\n", code);
    } else if (code == SYS_POWER_OFF) {
	DEBUG_INIT("\niSCSI: iscsi_reboot_notifier_function called with "
		   "code SYS_POWER_OFF = 0x%lu\n", code);
    } else {
	printk("\niSCSI: iscsi_reboot_notifier_function called with unknown "
	       "code = 0x%lu !!!\n", code);
    }

    oldfs = get_fs();
    set_fs(get_ds());
    /* 
     * We don't do this in while loop as someone can do CTL-ALT-DEL after a 
     * 'halt' which will cause this to fail and retried again and again
     */
    if (!iscsi_set_if_addr()) {
	printk("\niSCSI: iscsi_set_if_addr failed !!!\n");
	schedule_timeout(10 * HZ);
    }
    set_fs(oldfs);

    DEBUG_INIT("\niSCSI: Setting iscsi_system_is_rebooting, current->pid = %d "
	       "this->next = 0x%p iscsi_reboot_notifier_function = 0x%p\n",
	       current->pid, this->next, &iscsi_reboot_notifier_function);

    iscsi_system_is_rebooting = 1;

    while (!iscsi_shutdown()) {
	printk("iSCSI: driver shutdown failed\n");
    }
    printk("\niSCSI: iscsi_reboot_notifier_function: driver shutdown "
	   "succeeded\n");

    oldfs = get_fs();
    set_fs(get_ds());
    /* 
     * We don't do this in while loop as someone can do CTL-ALT-DEL after a 
     * 'halt' which will cause this to fail and retried again and again
     */
    if (!iscsi_ifdown()) {
	printk("\niSCSI: iscsi_set_if_addr failed !!!\n");
	schedule_timeout(10 * HZ);
    }
    set_fs(oldfs);

    return NOTIFY_DONE;
}

static struct notifier_block iscsi_reboot_notifier = {
  notifier_call:iscsi_reboot_notifier_function,
  next:NULL,
  priority:255			/* priority, might need to have a 
				 * relook at the value 
				 */
};

/*
 * This is called as part of ISCSI_SET_INBP_INFO ioctl which gets called
 * only from iscsi-network-boot.c from initrd. iscsid will never call
 * ISCSI_SET_INBP_INFO.
 */
/* For now always returns 0 */
static int
set_inbp_info(void)
{
    int tmp_index;
    int rv;
    struct ifreq req;
    int second_index;
    mm_segment_t oldfs;

    this_is_iscsi_boot = iscsi_inbp_info.root_disk;
    printk("\nSetting this_is_iscsi_boot in the kernel\n");

    printk("\n############################################################"
	   "##################\n");
    printk("iscsi_inbp_info.myethaddr = %2x %2x %2x %2x %2x %2x\n",
	   iscsi_inbp_info.myethaddr[0], iscsi_inbp_info.myethaddr[1],
	   iscsi_inbp_info.myethaddr[2], iscsi_inbp_info.myethaddr[3],
	   iscsi_inbp_info.myethaddr[4], iscsi_inbp_info.myethaddr[5]);

    printk("iscsi_inbp_info.targetstring = %s\n", iscsi_inbp_info.targetstring);
    printk("iscsi_inbp_info.myipaddr = 0x%x\n", iscsi_inbp_info.myipaddr);
    printk("##############################################################"
	   "################\n");

    oldfs = get_fs();
    set_fs(get_ds());

    for (tmp_index = 1; tmp_index < NETDEV_BOOT_SETUP_MAX; tmp_index++) {
	memset(&req, 0, sizeof (req));
	req.ifr_ifindex = tmp_index;
	rv = dev_ioctl(SIOCGIFNAME, &req);
	DEBUG_INIT("\nifrn_name = %s : hw_addr \n", req.ifr_name);
	req.ifr_ifindex = 0;
	rv = dev_ioctl(SIOCGIFHWADDR, &req);
	for (second_index = 0; second_index < IFHWADDRLEN; second_index++) {
	    DEBUG_INIT("\nifr_hwaddr[%d] = 0x%2x ", second_index,
		       req.ifr_hwaddr.sa_data[second_index]);
	}

	if (memcmp
	    (iscsi_inbp_info.myethaddr, req.ifr_hwaddr.sa_data, IFHWADDRLEN)) {
	    DEBUG_INIT("\nInterface %s does not correspond to the mac address "
		       "in inbp structure : %2x %2x %2x %2x %2x %2x\n",
		       req.ifr_name, req.ifr_hwaddr.sa_data[0],
		       req.ifr_hwaddr.sa_data[1], req.ifr_hwaddr.sa_data[2],
		       req.ifr_hwaddr.sa_data[3], req.ifr_hwaddr.sa_data[4],
		       req.ifr_hwaddr.sa_data[5]);
	} else {
	    printk("\nInterface %s corresponds to mac address in inbp "
		   "structure : %2x %2x %2x %2x %2x %2x\n",
		   req.ifr_name, iscsi_inbp_info.myethaddr[0],
		   iscsi_inbp_info.myethaddr[1], iscsi_inbp_info.myethaddr[2],
		   iscsi_inbp_info.myethaddr[3], iscsi_inbp_info.myethaddr[4],
		   iscsi_inbp_info.myethaddr[5]);

	    strcpy(inbp_interface_name, req.ifr_name);
	    DEBUG_INIT("\ninbp_interface_name resolved as %s\n",
		       inbp_interface_name);
	}
    }

    while (!iscsi_set_if_addr()) {
	printk("\niSCSI: set_inbp_info: iscsi_set_if_addr failed\n");
	schedule_timeout(10 * HZ);
    }

    set_fs(oldfs);
    /* 
     * We will register reboot notifier only in case of iSCSI boot (not under 
     * usual driver runs) so we should never need to unregister it.
     */
    if (this_is_iscsi_boot) {
	if (register_reboot_notifier(&iscsi_reboot_notifier)) {
	    /* FIXME: return error */
	    DEBUG_INIT("\niSCSI: register_reboot_notifier failed\n");
	} else {
	    DEBUG_INIT("\niSCSI: register_reboot_notifier succeeded\n");
	}
    }
    return 0;
}

#if defined(HAS_SLAVE_CONFIGURE)

int
iscsi_slave_alloc(Scsi_Device * dev)
{
    return 0;
}

int
iscsi_slave_configure(Scsi_Device * dev)
{
    unsigned char depth;

    /* select queue depth and tcq for this device */
    if (dev->tagged_supported) {
	depth = ISCSI_CMDS_PER_LUN;
	scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: enabled tagged command queueing for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    } else if (force_tcq) {
	depth = ISCSI_CMDS_PER_LUN;
	scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: forced tagged command queueing for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    } else {
	depth = untagged_queue_depth;
	scsi_adjust_queue_depth(dev, 0, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: tagged command queueing not supported for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    }

    return 0;
}

void
iscsi_slave_destroy(Scsi_Device * dev)
{
}

#elif defined(HAS_NEW_SLAVE_ATTACH)

int
iscsi_slave_attach(Scsi_Device * dev)
{
    unsigned char depth;

    /* select queue depth and tcq for this device */
    if (dev->tagged_supported) {
	depth = ISCSI_CMDS_PER_LUN;
	scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: enabled tagged command queueing for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    } else if (force_tcq) {
	depth = ISCSI_CMDS_PER_LUN;
	scsi_adjust_queue_depth(dev, MSG_SIMPLE_TAG, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: forced tagged command queueing for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    } else {
	depth = untagged_queue_depth;
	scsi_adjust_queue_depth(dev, 0, depth);
	if (LOG_ENABLED(ISCSI_LOG_INIT))
	    printk("iSCSI: tagged command queueing not supported for device %p "
		   "(%u %u %u %u), type 0x%x, depth %u\n",
		   dev, dev->host->host_no, dev->channel, dev->id, dev->lun,
		   dev->type, depth);
    }

    return 0;
}

void
iscsi_slave_detach(Scsi_Device * dev)
{
}

#elif defined(HAS_SELECT_QUEUE_DEPTHS)

static void
iscsi_select_queue_depths(struct Scsi_Host *host, Scsi_Device * device_list)
{
    Scsi_Device *dev;

    DEBUG_INIT("iSCSI: selecting queue depths for host #%u\n", host->host_no);

    for (dev = device_list; dev; dev = dev->next) {
	if (dev->host != host)
	    continue;

	if (force_tcq) {
	    dev->queue_depth = ISCSI_CMDS_PER_LUN;
	    if (dev->tagged_supported) {
		if (dev->tagged_queue == 0)
		    printk("iSCSI: enabled tagged command queueing for "
			   "(%u %u %u %u), type 0x%x, depth %d\n",
			   host->host_no, dev->channel, dev->id, dev->lun,
			   dev->type, dev->queue_depth);
		else
		    DEBUG_INIT("iSCSI: enabled tagged command queueing for "
			       "(%u %u %u %u), type 0x%x, depth %d\n",
			       host->host_no, dev->channel, dev->id, dev->lun,
			       dev->type, dev->queue_depth);
	    } else {
		if (dev->tagged_queue == 0)
		    printk("iSCSI: forced tagged command queueing for "
			   "(%u %u %u %u), type 0x%x, depth %d\n",
			   host->host_no, dev->channel, dev->id, dev->lun,
			   dev->type, dev->queue_depth);
		else
		    DEBUG_INIT("iSCSI: forced tagged command queueing for "
			       "(%u %u %u %u), type 0x%x, depth %d\n",
			       host->host_no, dev->channel, dev->id, dev->lun,
			       dev->type, dev->queue_depth);
	    }
	    dev->tagged_queue = 1;
	} else if (dev->tagged_supported) {
	    dev->tagged_queue = 1;
	    dev->queue_depth = ISCSI_CMDS_PER_LUN;
	    DEBUG_INIT("iSCSI: enabled tagged command queueing for "
		       "(%u %u %u %u), type 0x%x, depth %d\n",
		       host->host_no, dev->channel, dev->id, dev->lun,
		       dev->type, dev->queue_depth);
	} else {
	    dev->tagged_queue = 0;
	    dev->queue_depth = untagged_queue_depth;
	    if (LOG_ENABLED(ISCSI_LOG_INIT))
		printk("iSCSI: tagged command queueing not supported for "
		       "(%u %u %u %u), type 0x%x, depth %d\n",
		       host->host_no, dev->channel, dev->id, dev->lun,
		       dev->type, dev->queue_depth);
	}
    }
}

#endif

int
iscsi_detect(Scsi_Host_Template * sht)
{
    struct Scsi_Host *sh;
    iscsi_hba_t *hba;
    unsigned char cache_name[20];

    sht->proc_name = "iscsi";

    sh = scsi_register(sht, sizeof (iscsi_hba_t));
    if (!sh) {
	printk("iSCSI: Unable to register iSCSI HBA\n");
	return 0;
    }

    /* zero these now to disable the scan done during scsi_register_host.
     * iscsi_probe_luns will set them later.
     */
    sh->max_id = 0;
    sh->max_lun = 0;
    sh->max_channel = 0;

#if defined(HAS_SELECT_QUEUE_DEPTHS)
    sh->select_queue_depths = iscsi_select_queue_depths;
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,0)
    /* indicate the maximum CDB length we can accept */
    sh->max_cmd_len = ISCSI_MAX_CMD_LEN;
#endif

    hba = (iscsi_hba_t *) sh->hostdata;
    memset(hba, 0, sizeof (iscsi_hba_t));

    hba->next = NULL;
    hba->host = sh;

    /* list of sessions on this HBA */
    spin_lock_init(&hba->session_lock);
    hba->session_list_head = NULL;
    hba->session_list_tail = NULL;
    atomic_set(&hba->num_sessions, 0);

    /* pool of iscsi tasks */
    /* Note: we uniqify the cache name, since the kernel
     * bugchecks if you try to create a name that already
     * exists.  Since kmem_cache_destroy may fail, a unique
     * name keeps the kernel from panicing if the module is
     * unloaded and reloaded.
     */
    sprintf(cache_name, "iscsi_%.10u", (unsigned int) (jiffies & 0xFFFFFFFFU));
    if (iscsi_reap_tasks) {
	printk("iSCSI: allocating task cache %s with reaping enabled\n",
	       cache_name);
	hba->task_cache =
	    kmem_cache_create(cache_name, sizeof (iscsi_task_t), 0, 0,
			      iscsi_task_ctor, NULL);
    } else {
	printk("iSCSI: allocating task cache %s with reaping disabled\n",
	       cache_name);
	hba->task_cache =
	    kmem_cache_create(cache_name, sizeof (iscsi_task_t), 0,
			      SLAB_NO_REAP, iscsi_task_ctor, NULL);
    }
    if (hba->task_cache) {
	iscsi_task_t *head = NULL, *task;
	int n;

	/* try to provoke some slab allocation while we can safely block.
	 * this probably won't accomplish much without SLAB_NO_REAP,
	 * but it won't hurt in that case either, so we always do it.
	 */
	/* FIXME: is there some way to do this on all
	 * processors, so that we prime the CPU cache for
	 * each processor on SMP machines?
	 * smp_call_function() says the function shouldn't
	 * block, which means we couldn't use SLAB_KERNEL.
	 */
	for (n = 0; n < ISCSI_PREALLOCATED_TASKS; n++) {
	    task = kmem_cache_alloc(hba->task_cache, SLAB_KERNEL);
	    if (task) {
		task->next = head;
		head = task;
	    }
	}
	while (head) {
	    task = head;
	    head = task->next;
	    task->next = NULL;
	    kmem_cache_free(hba->task_cache, task);
	}
    } else {
	/* FIXME: do we need to undo the scsi_register, or
	 * will iscsi_release get called? 
	 */
	printk("iSCSI: kmem_cache_create failed at %lu\n", jiffies);
	return 0;
    }

    set_bit(ISCSI_HBA_ACTIVE, &hba->flags);
    clear_bit(ISCSI_HBA_SHUTTING_DOWN, &hba->flags);

    hba->host_no = sh->host_no;

    /* for now, there's just one iSCSI HBA */
    smp_mb();
    iscsi_hba_list = hba;
    smp_mb();
    printk("iSCSI: detected HBA %p, host #%d\n", hba, sh->host_no);
    return 1;
}

/* cleanup before unloading the module */
int
iscsi_release(struct Scsi_Host *sh)
{
    iscsi_hba_t *hba;

    hba = (iscsi_hba_t *) sh->hostdata;
    if (!hba) {
	return FALSE;
    }

    printk("iSCSI: releasing HBA %p, host #%d\n", hba, hba->host->host_no);
    set_bit(ISCSI_HBA_RELEASING, &hba->flags);
    smp_mb();

    /* remove all sessions on this HBA, and prevent any from being added */
    if (!iscsi_shutdown_hba(hba)) {
	printk("iSCSI: can't release HBA %p, host #%u failed to shutdown\n",
	       hba, sh->host_no);
	return FALSE;
    }

    /* remove from the iSCSI HBA list */
    spin_lock(&iscsi_hba_list_lock);
    if (hba == iscsi_hba_list) {
	iscsi_hba_list = iscsi_hba_list->next;
    } else {
	iscsi_hba_t *prior = iscsi_hba_list;

	while (prior && prior->next != hba)
	    prior = prior->next;
	if (prior && prior->next == hba)
	    prior->next = hba->next;
    }
    spin_unlock(&iscsi_hba_list_lock);

    /* free this HBA's tasks */
    if (hba->task_cache) {
	DEBUG_INIT("iSCSI: HBA %p destroying task cache %p at %lu\n", hba,
		   hba->task_cache, jiffies);
	if (kmem_cache_destroy(hba->task_cache)) {
	    printk("iSCSI: HBA %p failed to destroy task cache %p at %lu\n",
		   hba, hba->task_cache, jiffies);

	    set_current_state(TASK_INTERRUPTIBLE);
	    schedule_timeout(HZ);

	    printk("iSCSI: HBA %p destroying task cache %p again at %lu\n", hba,
		   hba->task_cache, jiffies);
	    if (kmem_cache_destroy(hba->task_cache)) {
		printk("iSCSI: HBA %p failed to destroy task cache %p "
		       "again, giving up at %lu\n",
		       hba, hba->task_cache, jiffies);
	    }
	}

	hba->task_cache = NULL;
    }

    scsi_unregister(sh);

    printk("iSCSI: released HBA %p\n", hba);
    return TRUE;
}

/* remove a Scsi_Cmnd from a singly linked list joined by
 * the host_scribble pointers. 
*/
static int
remove_cmnd(Scsi_Cmnd * sc, Scsi_Cmnd ** head, Scsi_Cmnd ** tail)
{
    if (!sc || !head || !tail) {
	printk("iSCSI: bug - remove_cmnd %p, head %p, tail %p\n", sc, head,
	       tail);
	return 0;
    }

    if (sc == *head) {
	/* it's the head, remove it */
	*head = (Scsi_Cmnd *) sc->host_scribble;	/* next */
	if (*head == NULL)
	    *tail = NULL;
	sc->host_scribble = NULL;
	return 1;
    } else if (*head) {
	Scsi_Cmnd *prior, *next;

	/* try find the command prior to sc */
	prior = *head;
	next = (Scsi_Cmnd *) prior->host_scribble;
	while (next && (next != sc)) {
	    prior = next;
	    next = (Scsi_Cmnd *) prior->host_scribble;	/* next command */
	}
	if (prior && (next == sc)) {
	    /* remove the command */
	    prior->host_scribble = sc->host_scribble;
	    if (*tail == sc)
		*tail = prior;
	    sc->host_scribble = NULL;
	    return 1;
	}
    }

    return 0;
}

/* unconditionally remove the cmnd from all driver data structures 
 * The probing code uses this when cmnds time out or the probe is killed.
 * It aborts the command on our side, but doesn't inform the target.
 * Since the cmnd is either INQUIRY or REPORT_LUNs, the target should
 * always complete the command, and we just discard the response if
 * it's already been removed from our data structures.
 */
int
iscsi_squash_cmnd(iscsi_session_t * session, Scsi_Cmnd * sc)
{
    iscsi_task_t *task;
    int ret = 0;
    DECLARE_NOQUEUE_FLAGS;

    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
    if (remove_cmnd(sc, &session->retry_cmnd_head, &session->retry_cmnd_tail)) {
	del_command_timer(sc);
	ret = 1;
    } else
	if (remove_cmnd(sc, &session->scsi_cmnd_head, &session->scsi_cmnd_tail))
    {
	del_command_timer(sc);
	ret = 1;
    } else
	if (remove_cmnd
	    (sc, &session->deferred_cmnd_head, &session->deferred_cmnd_tail)) {
	del_command_timer(sc);
	ret = 1;
    }
    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    if (ret)
	return ret;

    /* remove any task for this cmnd */
    spin_lock(&session->task_lock);
    if ((task = remove_task_for_cmnd(&session->tx_tasks, sc))) {
	/* it's received an R2T, and is queued to have data sent */
	atomic_inc(&task->refcount);
    } else if ((task = find_task_for_cmnd(session, sc))) {
	atomic_inc(&task->refcount);
    }

    if (task) {
	DEBUG_EH("iSCSI: session %p squashing task %p, itt %u\n", session, task,
		 task->itt);
	remove_session_task(session, task);

	while (atomic_read(&task->refcount) > 1) {
	    /* wait for the driver to quit using the task */
	    set_current_state(TASK_UNINTERRUPTIBLE);
	    schedule_timeout(MSECS_TO_JIFFIES(10));
	}

	/* delete the timers */
	del_task_timer(task);
	del_command_timer(sc);

	/* free the task */
	free_task(session, task);

	ret = 1;
	DEBUG_EH("iSCSI: session %p squashed task %p, itt %u\n", session, task,
		 task->itt);
    }
    spin_unlock(&session->task_lock);

    if (ret == 0) {
	printk("iSCSI: session %p couldn't squash cmnd %p\n", session, sc);
    }
    return ret;
}

/*
 * All the docs say we're supposed to reset the device and complete
 * all commands for it back to the SCSI layer.  However, the SCSI
 * layer doesn't actually count how many commands are completed back
 * to it after a device reset, but rather just assumes only 1 command,
 * with a comment saying it should be fixed to handle the case where
 * there are multiple commands.  
 *
 * If there are multiple commands, the SCSI layer will blindly
 * continue on to the next stage of error recovery, even if we
 * complete all the failed commands back to it after a device reset.
 * Hopefully the Linux SCSI layer will be fixed to handle this
 * corectly someday.  In the meantime, we do the right thing here, and
 * make sure the other reset handlers can deal with the case where
 * they get called with a command that has already been completed back
 * to the SCSI layer by a device reset.
 *   
 */
int
iscsi_eh_device_reset(Scsi_Cmnd * sc)
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_session_t *session = NULL;
    int ret = FAILED;

    if (!sc) {
	printk("iSCSI: device reset, no SCSI command\n");
	return FAILED;
    }
    host = sc->host;
    if (!host) {
	printk("iSCSI: device reset, no host for SCSI command %p\n", sc);
	return FAILED;
    }
    hba = (iscsi_hba_t *) host->hostdata;
    if (!hba) {
	printk("iSCSI: device reset, no iSCSI HBA associated with SCSI "
	       "command %p\n", sc);
	return FAILED;
    }

    RELEASE_MIDLAYER_LOCK(host);

    /* find the appropriate session for the command */
    session = find_session_for_cmnd(sc);
    if (session) {
	set_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	printk("iSCSI: session %p eh_device_reset at %lu for command %p to "
	       "(%u %u %u %u), cdb 0x%x\n",
	       session, jiffies, sc, sc->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	drop_reference(session);
	ret = SUCCESS;
    } else {
	printk("iSCSI: session %p eh_device_reset failed at %lu, no session "
	       "for command %p to (%u %u %u %u), cdb 0x%x\n",
	       session, jiffies, sc, sc->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	ret = FAILED;
    }

    REACQUIRE_MIDLAYER_LOCK(host);
    return ret;
}

/* NOTE: due to bugs in the linux SCSI layer (scsi_unjam_host), it's
 * possible for this handler to be called even if the device_reset
 * handler completed all the failed commands back to the SCSI layer
 * with DID_RESET and returned SUCCESS.  To compensate for this, we
 * must ensure that this reset handler doesn't actually care whether
 * the command is still in the driver.  Just find the session
 * associated with the command, and reset it.  
 */
int
iscsi_eh_bus_reset(Scsi_Cmnd * sc)
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_session_t *session;
    DECLARE_NOQUEUE_FLAGS;

    if (!sc) {
	return FAILED;
    }
    host = sc->host;
    if (!host) {
	printk("iSCSI: bus reset, no host for SCSI command %p\n", sc);
	return FAILED;
    }
    hba = (iscsi_hba_t *) host->hostdata;
    if (!hba) {
	printk("iSCSI: bus reset, no iSCSI HBA associated with SCSI "
	       "command %p\n", sc);
	return FAILED;
    }

    RELEASE_MIDLAYER_LOCK(host);

    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    for (session = hba->session_list_head; session; session = session->next) {
	if (session->channel == sc->channel) {
	    set_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	    printk("iSCSI: session %p eh_bus_reset at %lu for command %p to "
		   "(%u %u %u %u), cdb 0x%x\n",
		   session, jiffies, sc, sc->host->host_no, sc->channel,
		   sc->target, sc->lun, sc->cmnd[0]);
	}
    }
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    REACQUIRE_MIDLAYER_LOCK(host);
    return SUCCESS;
}

int
iscsi_eh_host_reset(Scsi_Cmnd * sc)
{
    struct Scsi_Host *host = NULL;
    iscsi_hba_t *hba = NULL;
    iscsi_session_t *session;
    DECLARE_NOQUEUE_FLAGS;

    if (!sc) {
	return FAILED;
    }
    host = sc->host;
    if (!host) {
	printk("iSCSI: host reset, no host for SCSI command %p\n", sc);
	return FAILED;
    }
    hba = (iscsi_hba_t *) host->hostdata;
    if (!hba) {
	printk("iSCSI: host reset, no iSCSI HBA associated with SCSI "
	       "command %p\n", sc);
	return FAILED;
    }

    RELEASE_MIDLAYER_LOCK(host);

    SPIN_LOCK_NOQUEUE(&hba->session_lock);
    for (session = hba->session_list_head; session; session = session->next) {
	set_bit(SESSION_RESET_REQUESTED, &session->control_bits);
	printk("iSCSI: session %p eh_bus_reset at %lu for command "
	       "%p to (%u %u %u %u), cdb 0x%x\n",
	       session, jiffies, sc, sc->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
    }
    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);

    REACQUIRE_MIDLAYER_LOCK(host);
    return SUCCESS;
}

/* try to queue a command to the session, returning a
 * boolean indicating success or failure 
*/
int
iscsi_queue(iscsi_session_t * session, Scsi_Cmnd * sc,
	    void (*done) (Scsi_Cmnd *))
{
    DECLARE_NOQUEUE_FLAGS;

    if (session == NULL)
	return 0;

    /* make sure we can complete it properly later */
    sc->scsi_done = done;
    sc->result = 0;

    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);

    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
	if ((sc->device == NULL) || LOG_ENABLED(ISCSI_LOG_QUEUE) ||
	    (test_bit(DEVICE_LOG_TERMINATING, device_flags(sc->device)) == 0)) {
	    /* by default, log this only once per
	     * Scsi_Device, to avoid flooding the log 
	     */
	    printk("iSCSI: session %p terminating, failing to queue %p "
		   "cdb 0x%x and any following commands to (%u %u %u %u), %s\n",
		   session, sc, sc->cmnd[0], session->host_no, sc->channel,
		   sc->target, sc->lun, session->log_name);
	    if (sc->device)
		set_bit(DEVICE_LOG_TERMINATING, device_flags(sc->device));
	}
	SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	return 0;
    }

    if (test_bit(SESSION_REPLACEMENT_TIMEDOUT, &session->control_bits)) {
	if ((sc->device == NULL) || LOG_ENABLED(ISCSI_LOG_QUEUE) ||
	    (test_bit(DEVICE_LOG_REPLACEMENT_TIMEDOUT, device_flags(sc->device))
	     == 0)) {
	    /* by default, log this only once per
	     * Scsi_Device, to avoid flooding the log 
	     */
	    printk("iSCSI: session %p replacement timed out, failing to "
		   "queue %p cdb 0x%x and any following commands to "
		   "(%u %u %u %u), %s\n",
		   session, sc, sc->cmnd[0], session->host_no, sc->channel,
		   sc->target, sc->lun, session->log_name);
	    if (sc->device)
		set_bit(DEVICE_LOG_REPLACEMENT_TIMEDOUT,
			device_flags(sc->device));
	}
	SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
	return 0;
    }
#ifdef DEBUG
    /* make sure the command hasn't already been queued */
    {
	Scsi_Cmnd *search = session->scsi_cmnd_head;
	while (search) {
	    if (search == sc) {
		printk("iSCSI: bug - cmnd %p, state %x, eh_state %x, scribble "
		       "%p is already queued to session %p\n",
		       sc, sc->state, sc->eh_state, sc->host_scribble, session);
		print_session_cmnds(session);
		SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries,
			    sc->allowed);
		return 0;
	    }
	    search = (Scsi_Cmnd *) search->host_scribble;
	}
    }
#endif

    /* initialize Scsi_Pointer fields that we might use later */
    memset(&sc->SCp, 0, sizeof (sc->SCp));
    sc->SCp.ptr = (char *) session;
    sc->host_scribble = NULL;

    if (session->print_cmnds > 0) {
	session->print_cmnds--;
	printk("iSCSI: session %p iscsi_queue printing command at %lu\n",
	       session, jiffies);
	print_cmnd(sc);
    }

    /* add a command timer that tells us to fail the command back to the OS */
    DEBUG_QUEUE("iSCSI: session %p adding timer to command %p at %lu\n",
		session, sc, jiffies);
    add_command_timer(session, sc, iscsi_command_times_out);

    /* add it to the session's command queue so the tx thread will send it */
    if (session->scsi_cmnd_head) {
	/* append at the tail */
	session->scsi_cmnd_tail->host_scribble = (unsigned char *) sc;
	session->scsi_cmnd_tail = sc;
    } else {
	/* make it the head */
	session->scsi_cmnd_head = session->scsi_cmnd_tail = sc;
    }
    atomic_inc(&session->num_cmnds);

    DEBUG_QUEUE("iSCSI: queued %p to session %p at %lu, %u cmnds, head %p, "
		"tail %p\n",
		sc, session, jiffies, atomic_read(&session->num_cmnds),
		session->scsi_cmnd_head, session->scsi_cmnd_tail);

    ISCSI_TRACE(ISCSI_TRACE_Qd, sc, NULL, sc->retries, sc->timeout_per_command);
    wake_tx_thread(TX_SCSI_COMMAND, session);

    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);

    return 1;
}

int
iscsi_queuecommand(Scsi_Cmnd * sc, void (*done) (Scsi_Cmnd *))
{
    iscsi_hba_t *hba;
    iscsi_session_t *session = NULL;
    struct Scsi_Host *host;
    int queued = 0;
    int fake_transport_error = 0;

    host = sc->host;
    if (host == NULL) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: queuecommand but no Scsi_Host\n");
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }

    hba = (iscsi_hba_t *) sc->host->hostdata;
    if ((!hba) || (!test_bit(ISCSI_HBA_ACTIVE, &hba->flags))) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: queuecommand but no HBA\n");
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }

    if (!iscsi_timer_running) {
	/* iSCSI coming up or going down, fail the command */
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	DEBUG_QUEUE("iSCSI: no timer, failing to queue %p to (%u %u %u %u), "
		    "cdb 0x%x\n",
		    sc, hba->host->host_no, sc->channel, sc->target, sc->lun,
		    sc->cmnd[0]);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	done(sc);
	return 0;
    }

    if (sc->target >= ISCSI_MAX_TARGET_IDS_PER_BUS) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: invalid target id %u, failing to queue %p to "
	       "(%u %u %u %u), cdb 0x%x\n",
	       sc->target, sc, hba->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }
    if (sc->lun >= ISCSI_MAX_LUNS_PER_TARGET) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: invalid LUN %u, failing to queue %p to (%u %u %u %u), "
	       "cdb 0x%x\n",
	       sc->lun, sc, hba->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }
    /* CDBs larger than 16 bytes require additional header
     * segments, not yet implemented 
     */
    if (sc->cmd_len > ISCSI_MAX_CMD_LEN) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: cmd_len %u too large, failing to queue %p to "
	       "(%u %u %u %u), cdb 0x%x\n",
	       sc->cmd_len, sc, hba->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }
    /* make sure our SG_TABLESIZE limit was respected */
    if (sc->use_sg > ISCSI_MAX_SG) {
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	printk("iSCSI: use_sg %u too large, failing to queue %p to "
	       "(%u %u %u %u), cdb 0x%x\n",
	       sc->use_sg, sc, hba->host->host_no, sc->channel, sc->target,
	       sc->lun, sc->cmnd[0]);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	set_lun_comm_failure(sc);
	done(sc);
	return 0;
    }
#ifdef DEBUG
    if (sc->use_sg) {
	int index;
	struct scatterlist *sglist = (struct scatterlist *) sc->request_buffer;
	unsigned int length = 0;
	int bogus = 0;

	/* sanity check the sglist, to make sure the
	 * segments have at least bufflen 
	 */
	for (index = 0; index < sc->use_sg; index++) {
	    length += sglist[index].length;
	    if (sglist[index].length == 0)
		bogus = 1;
	}

	if (bogus || (length < sc->request_bufflen)) {
	    printk("iSCSI: attempted to queue %p at %lu to (%u %u %u %u), "
		   "cdb 0x%x, corrupt sglist, sg length %u, buflen %u\n",
		   sc, jiffies, hba->host->host_no, sc->channel, sc->target,
		   sc->lun, sc->cmnd[0], length, sc->request_bufflen);
	    print_cmnd(sc);
	    ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries,
			sc->timeout_per_command);
	    sc->result = HOST_BYTE(DID_NO_CONNECT);
	    set_lun_comm_failure(sc);
	    done(sc);
	    return 0;
	}
    }
#endif

    RELEASE_MIDLAYER_LOCK(host);

    DEBUG_QUEUE("iSCSI: queueing %p to (%u %u %u %u) at %lu, cdb 0x%x, cpu%d\n",
		sc, hba->host->host_no, sc->channel, sc->target, sc->lun,
		jiffies, sc->cmnd[0], smp_processor_id());

    if (hba) {
	session = find_session_for_cmnd(sc);

	if (session) {
	    DEBUG_QUEUE("iSCSI: session %p queuecommand %p at %lu, retries %d, "
			"allowed %d, timeout %u\n",
			session, sc, jiffies, sc->retries, sc->allowed,
			sc->timeout_per_command);

	    /* record whether I/O commands have been ever
	     * been sent on this session, to help us decide
	     * when we need the session and should retry
	     * logins regardless of the login status. Ignore
	     * all the commands sent by default as part of
	     * the LUN being scanned or a device being
	     * opened, so that sessions that have always
	     * been idle can be dropped.  Of course, this is
	     * always true for disks, since Linux will do
	     * reads looking for a partition table.
	     */
	    switch (sc->cmnd[0]) {
	    case INQUIRY:
	    case REPORT_LUNS:
	    case TEST_UNIT_READY:
	    case READ_CAPACITY:
	    case START_STOP:
	    case MODE_SENSE:
		break;
	    default:
		session->commands_queued = 1;
		smp_mb();
		break;
	    }

	    /* For testing, possibly fake transport errors for some commands */
	    if (session->fake_not_ready > 0) {
		session->fake_not_ready--;	/* not atomic to avoid 
						 * overhead, and miscounts 
						 * won't matter much 
						 */
		smp_mb();
		fake_transport_error = 1;
	    } else {
		/* delete the existing command timer before 
		 * iscsi_queue adds ours 
		 */
		del_command_timer(sc);

		queued = iscsi_queue(session, sc, done);

		if (!queued)
		    add_completion_timer(sc);	/* need a timer for the 
						 * midlayer to delete 
						 */
	    }

	    drop_reference(session);
	} else {
	    /* couldn't find a session */
	    if ((sc->device == NULL) || LOG_ENABLED(ISCSI_LOG_QUEUE) ||
		(test_bit(DEVICE_LOG_NO_SESSION, device_flags(sc->device)) ==
		 0)) {
		printk("iSCSI: queuecommand %p failed to find a session "
		       "for HBA %p, (%u %u %u %u)\n",
		       sc, hba, hba->host->host_no, sc->channel, sc->target,
		       sc->lun);
		if (sc->device)
		    set_bit(DEVICE_LOG_NO_SESSION, device_flags(sc->device));
	    }
	}
    }

    REACQUIRE_MIDLAYER_LOCK(host);

    if (fake_transport_error) {
	printk("iSCSI: session %p faking transport failure for command %p "
	       "to (%u %u %u %u) at %lu\n",
	       session, sc, sc->host->host_no, sc->channel, sc->target, sc->lun,
	       jiffies);
	/* act as if recv_cmd() received a non-zero iSCSI response */
	memset(sc->sense_buffer, 0, sizeof (sc->sense_buffer));
	set_lun_comm_failure(sc);
	sc->result = HOST_BYTE(DID_ERROR) | STATUS_BYTE(STATUS_CHECK_CONDITION);
	sc->resid = iscsi_expected_data_length(sc);
	done(sc);
    } else if (!queued) {
	DEBUG_QUEUE("iSCSI: queuecommand completing %p with DID_NO_CONNECT "
		    "at %lu\n", sc, jiffies);
	ISCSI_TRACE(ISCSI_TRACE_QFailed, sc, NULL, sc->retries, sc->allowed);
	sc->result = HOST_BYTE(DID_NO_CONNECT);
	sc->resid = iscsi_expected_data_length(sc);
	set_lun_comm_failure(sc);
	done(sc);
	/* "queued" successfully, and already completed
	 * (with a fatal error), so we still return 0 
	 */
    }

    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,45))
int
iscsi_biosparam(struct scsi_device *sdev, struct block_device *n,
		sector_t capacity, int geom[])
{
    /* FIXME: should we use 255h,63s if there are more than 1024 cylinders? */
    geom[0] = 64;		/* heads */
    geom[1] = 32;		/* sectors */
    geom[2] = (unsigned long) capacity / (64 * 32);	/* cylinders */
    return 1;
}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,44))
# include "sd.h"
int
iscsi_biosparam(Disk * disk, struct block_device *bdev, int geom[])
{
    /* FIXME: should we use 255h,63s if there are more than 1024 cylinders? */
    geom[0] = 64;		/* heads */
    geom[1] = 32;		/* sectors */
    geom[2] = disk->capacity / (64 * 32);	/* cylinders */
    return 1;
}
#else
# include "sd.h"
int
iscsi_biosparam(Disk * disk, kdev_t dev, int geom[])
{
    /* FIXME: should we use 255h,63s if there are more than 1024 cylinders? */
    geom[0] = 64;		/* heads */
    geom[1] = 32;		/* sectors */
    geom[2] = disk->capacity / (64 * 32);	/* cylinders */
    DEBUG_INIT("iSCSI: biosparam = %d cylinders, %d heads, %d sectors\n",
	       geom[2], geom[0], geom[1]);

    return 1;
}
#endif

const char *
iscsi_info(struct Scsi_Host *sh)
{
    iscsi_hba_t *hba;
    static char buffer[256];
    char *build_str = BUILD_STR;

    DEBUG_INIT("iSCSI: Info\n");
    hba = (iscsi_hba_t *) sh->hostdata;
    if (!hba) {
	return NULL;
    }

    memset(buffer, 0, sizeof (buffer));

    if (build_str) {
	/* developer-built variant of a 4-digit internal release */
	sprintf(buffer, "iSCSI %d.%d.%d.%d%s variant (%s)",
		DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
		DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION,
		DRIVER_EXTRAVERSION, ISCSI_DATE);
    } else if (DRIVER_INTERNAL_VERSION > 0) {
	/* 4-digit release */
	sprintf(buffer, "iSCSI %d.%d.%d.%d%s (%s)",
		DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
		DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION,
		DRIVER_EXTRAVERSION, ISCSI_DATE);
    } else {
	/* 3-digit release */
	sprintf(buffer, "iSCSI %d.%d.%d%s (%s)",
		DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
		DRIVER_PATCH_VERSION, DRIVER_EXTRAVERSION, ISCSI_DATE);
    }

    return buffer;
}

static void
right_justify(char *buffer, int fieldsize)
{
    char *src = buffer;
    int length = strlen(buffer);
    int shift;
    char *dst;

    if (length > fieldsize) {
	printk("iSCSI: can't right justify, length %d, fieldsize %d\n", length,
	       fieldsize);
	return;
    }

    shift = (fieldsize - length);

    if ((length > 0) && (shift > 0)) {
	/* memmove it to the right, assuming the buffer is
	 * at least one byte longer than fieldsize 
	 */
	dst = buffer + fieldsize;
	src = buffer + length;

	/* have to copy 1 byte at a time from the end, to
	 * avoid clobbering the source 
	 */
	while (src >= buffer) {
	    *dst-- = *src--;
	}

	/* fill spaces on the left */
	for (dst = buffer; shift; shift--)
	    *dst++ = ' ';
    }
}

/* if current_offset is in [start..finish), add str to the buffer */
static int
add_proc_buffer(char *str, off_t start, off_t finish, off_t * current_offset,
		char **buffer)
{
    int length = strlen(str);
    int leading = 0;

    if (*current_offset + length <= start) {
	/* don't need any of it */
	*current_offset += length;
	return 1;
    } else if (*current_offset + length <= finish) {
	/* need everything at or above start */
	if (*current_offset < start)
	    leading = start - *current_offset;

	strcpy(*buffer, str + leading);
	*buffer += (length - leading);
	*current_offset += length;
	return 1;
    } else {
	/* need everything at or above start, and before finish */
	if (*current_offset < start) {
	    leading = start - *current_offset;
	    strncpy(*buffer, str + leading, finish - start);
	    *buffer += (finish - start);
	    *current_offset = finish;
	}
	return 0;		/* no more */
    }
}

/* Each LUN line has:
 * 2 spaces + ((3-digit field + space) * 4) + 4 spaces + 
 * 15 char IP address + 2 spaces + 5-digit port + 2 spaces 
 * 2 + 16 + 4 + 15 + 2 + 5 + 2 = fixed length 46 + TargetName + newline
 * the TargetName ends the line, and has a variable length.
 */
static int
add_lun_line(iscsi_session_t * session, int lun,
	     off_t start, off_t finish, off_t * current_offset, char **buffer)
{
    char str[32];

    DEBUG_FLOW("iSCSI: add_lun_line %p, %d, %lu, %lu, %lu, %p\n",
	       session, lun, start, finish, *current_offset, *buffer);

    /* 2 spaces + (4 * (3-digit field + space)) + 4 spaces = 22 chars */
    if (lun >= 0)
	sprintf(str, "  %3.1u %3.1u %3.1u    ",
		session->iscsi_bus, session->target_id, lun);
    else
	sprintf(str, "  %3.1u %3.1u   ?    ",
		session->iscsi_bus, session->target_id);

    if (!add_proc_buffer(str, start, finish, current_offset, buffer))
	return 0;

    if (test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	/* up to 15 char IP address + 2 spaces = 17 chars */
	sprintf(str, "%u.%u.%u.%u  ",
		session->ip_address[0], session->ip_address[1],
		session->ip_address[2], session->ip_address[3]);
	right_justify(str, 17);

	if (!add_proc_buffer(str, start, finish, current_offset, buffer))
	    return 0;

	/* 5-digit port + 2 spaces = 7 chars */
	sprintf(str, "%d  ", session->port);
	right_justify(str, 7);

	if (!add_proc_buffer(str, start, finish, current_offset, buffer))
	    return 0;
    } else {
	/* fill in '?' for each empty field, so that /proc/scsi/iscsi can easily 
	 * be processed by tools such as awk and perl.
	 */
	sprintf(str, "              ?      ?  ");
	if (!add_proc_buffer(str, start, finish, current_offset, buffer))
	    return 0;
    }

    if (!add_proc_buffer
	(session->TargetName, start, finish, current_offset, buffer))
	return 0;

    /* and a newline */
    sprintf(str, "\n");
    if (!add_proc_buffer(str, start, finish, current_offset, buffer))
	return 0;

    /* keep going */
    return 1;
}

/* Show LUNs for every session on the HBA.  The parameters tell us
 * what part of the /proc "file" we're supposed to put in the buffer.
 * This is somewhat broken, since the data may change in between the
 * multiple calls to this function, since we can't keep holding the
 * locks.  Implemented by throwing away bytes that we would have
 * written to the buffer until we reach <start>, and then putting
 * everything before <finish> in the buffer.  Return when we run
 * out of data, or out of buffer.  We avoid oddities in the output,
 * we must ensure that the size of the output doesn't vary while /proc
 * is being read.  We can easily make each line a fixed size, but
 * if the number of LUNs or sessions varies during a /proc read,
 * the user loses.
 */
static int
show_session_luns(iscsi_session_t * session,
		  off_t start, off_t finish, off_t * current_offset,
		  char **buffer)
{
    /* if we've already found LUNs, show them all */
    int lfound = 0;
    int l;

    /* FIXME: IPv6 */
    if (session->ip_length != 4)
	return 1;

    for (l = 0; l < ISCSI_MAX_LUN; l++) {
	if (test_bit(l, session->luns_activated)) {
	    lfound += 1;

	    if (!add_lun_line
		(session, l, start, finish, current_offset, buffer)) {
		DEBUG_FLOW("iSCSI: show session luns returning 0 with "
			   "current offset %lu, buffer %p\n",
			   *current_offset, buffer);
		return 0;
	    }
	}
    }

    /* if we haven't found any LUNs, use ? for a LUN number */
    if (!lfound) {
	if (!add_lun_line(session, -1, start, finish, current_offset, buffer)) {
	    DEBUG_FLOW("iSCSI: show session luns returning 0 with current "
		       "offset %lu, buffer %p\n", *current_offset, buffer);
	    return 0;
	}
    } else {
	DEBUG_FLOW("iSCSI: show session luns returning 1 with current "
		   "offset %lu, buffer %p\n", *current_offset, buffer);
    }

    return 1;
}

/* returns number of bytes matched */
static int
find_keyword(char *start, char *end, char *key)
{
    char *ptr = start;
    int key_len = strlen(key);

    /* skip leading whitespace */
    while ((ptr < end) && is_space(*ptr))
	ptr++;

    /* compare */
    if (((end - ptr) == key_len) && !memcmp(key, ptr, key_len)) {
	return (ptr - start) + key_len;
    } else if (((end - ptr) > key_len) && !memcmp(key, ptr, key_len)
	       && is_space(ptr[key_len])) {
	return (ptr - start) + key_len;
    } else {
	return 0;
    }
}

static int
find_number(char *start, char *end, int *number)
{
    char *ptr = start;
    int found = 0;
    int acc = 0;

    /* skip leading whitespace */
    while ((ptr < end) && is_space(*ptr))
	ptr++;

    while (ptr < end) {
	if (is_space(*ptr)) {
	    break;
	} else if (is_digit(*ptr)) {
	    found = 1;
	    acc = (acc * 10) + (*ptr - '0');
	} else {
	    /* something bogus */
	    return 0;
	}
	ptr++;
    }

    if (found) {
	if (number)
	    *number = acc;
	return (ptr - start);
    } else
	return 0;
}

static int
find_value(char *start, char *end, int *value)
{
    const char *ptr = start;
    unsigned int found = 0;
    int acc = 0;

    /* skip leading whitespace */
    while ((ptr < end) && is_space(*ptr))
	ptr++;
    while (ptr < end) {
	if (is_space(*ptr)) {
	    break;
	} else if (is_digit(*ptr)) {
	    found = 1;
	    acc = (acc * 10) + (*ptr - '0');
	} else if (*ptr == '-') {
	    /* If we have already seen a number, this '-' is a wild card
	     * for next field.
	     */
	    if (found == 1)
		break;
	    found = 1;
	    *value = WILD_CARD;
	    ptr++;
	    return (ptr - start);
	} else {
	    /* Something bogus */
	    return 0;
	}
	ptr++;
    }

    if (found) {
	*value = acc;
	return (ptr - start);
    } else
	return 0;
}

static int
find_ip(char *start, char *end, char *addr)
{
    char *ptr = start;
    char *ptr1;
    int ip_length = 0;

    /* skip leading whitespace */
    while ((ptr < end) && is_space(*ptr))
	ptr++;

    ptr1 = ptr;
    while ((ptr1 < end) && !is_space(*ptr1)) {
	ptr1++;
	ip_length++;
    }
    if (ip_length) {
	memcpy(addr, ptr, ip_length);
	return (ptr1 - start);
    } else
	return 0;
}

/*
 * *buffer: I/O buffer
 * **start: for user reads, driver can report where valid data starts in 
 *          the buffer
 * offset: current offset into a /proc/scsi/iscsi/[0-9]* file
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * write: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written to a
 * /proc/scsi/iscsi/[0-9]* file.
 */

int
iscsi_proc_info(char *buffer,
		char **start, off_t offset, int length, int hostno, int write)
{
    char *bp = buffer;
    iscsi_hba_t *hba;
    iscsi_session_t *session;
    unsigned int bus = 0, target = 0, lun = 0;
    unsigned int completions = 0;
    unsigned int aborts = 0;
    unsigned int abort_task_sets = 0;
    unsigned int lun_resets = 0;
    unsigned int warm_resets = 0;
    unsigned int cold_resets = 0;
    DECLARE_NOQUEUE_FLAGS;

    if (!buffer)
	return -EINVAL;

    if (write) {
	int cmd_len;
	char *end = buffer + length;
	DECLARE_NOQUEUE_FLAGS;

	if ((cmd_len = find_keyword(bp, end, "log"))) {
	    unsigned int log_setting = 0;

	    bp += cmd_len;

	    if ((cmd_len = find_keyword(bp, end, "all")) != 0) {
		iscsi_log_settings = 0xFFFFFFFF;
		printk("iSCSI: all logging enabled\n");
	    } else if ((cmd_len = find_keyword(bp, end, "none")) != 0) {
		iscsi_log_settings = 0;
		printk("iSCSI: all logging disabled\n");
	    } else if ((cmd_len = find_keyword(bp, end, "sense")) != 0) {
		log_setting = ISCSI_LOG_SENSE;
		bp += cmd_len;

		if ((cmd_len = find_keyword(bp, end, "always")) != 0) {
		    iscsi_log_settings |= LOG_SET(log_setting);
		    printk("iSCSI: log sense always\n");
		} else if ((cmd_len = find_keyword(bp, end, "on")) != 0) {
		    iscsi_log_settings |= LOG_SET(log_setting);
		    printk("iSCSI: log sense yes\n");
		} else if ((cmd_len = find_keyword(bp, end, "yes")) != 0) {
		    iscsi_log_settings |= LOG_SET(log_setting);
		    printk("iSCSI: log sense yes\n");
		} else if ((cmd_len = find_keyword(bp, end, "1")) != 0) {
		    iscsi_log_settings |= LOG_SET(log_setting);
		    printk("iSCSI: log sense 1\n");
		} else if ((cmd_len = find_keyword(bp, end, "minimal")) != 0) {
		    iscsi_log_settings &= ~LOG_SET(log_setting);
		    printk("iSCSI: log sense off\n");
		} else if ((cmd_len = find_keyword(bp, end, "off")) != 0) {
		    iscsi_log_settings &= ~LOG_SET(log_setting);
		    printk("iSCSI: log sense off\n");
		} else if ((cmd_len = find_keyword(bp, end, "no")) != 0) {
		    iscsi_log_settings &= ~LOG_SET(log_setting);
		    printk("iSCSI: log sense no\n");
		} else if ((cmd_len = find_keyword(bp, end, "0")) != 0) {
		    iscsi_log_settings &= ~LOG_SET(log_setting);
		    printk("iSCSI: log sense 0\n");
		}
	    } else {
		if ((cmd_len = find_keyword(bp, end, "login")) != 0) {
		    log_setting = ISCSI_LOG_LOGIN;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "init")) != 0) {
		    log_setting = ISCSI_LOG_INIT;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "queue")) != 0) {
		    log_setting = ISCSI_LOG_QUEUE;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "alloc")) != 0) {
		    log_setting = ISCSI_LOG_ALLOC;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "flow")) != 0) {
		    log_setting = ISCSI_LOG_FLOW;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "error")) != 0) {
		    log_setting = ISCSI_LOG_ERR;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "eh")) != 0) {
		    log_setting = ISCSI_LOG_EH;
		    bp += cmd_len;
		} else if ((cmd_len = find_keyword(bp, end, "retry")) != 0) {
		    log_setting = ISCSI_LOG_RETRY;
		    bp += cmd_len;
		}

		if (log_setting) {
		    if ((cmd_len = find_keyword(bp, end, "on")) != 0) {
			iscsi_log_settings |= LOG_SET(log_setting);
		    } else if ((cmd_len = find_keyword(bp, end, "yes")) != 0) {
			iscsi_log_settings |= LOG_SET(log_setting);
		    } else if ((cmd_len = find_keyword(bp, end, "1")) != 0) {
			iscsi_log_settings |= LOG_SET(log_setting);
		    } else if ((cmd_len = find_keyword(bp, end, "off")) != 0) {
			iscsi_log_settings &= ~LOG_SET(log_setting);
		    } else if ((cmd_len = find_keyword(bp, end, "no")) != 0) {
			iscsi_log_settings &= ~LOG_SET(log_setting);
		    } else if ((cmd_len = find_keyword(bp, end, "0")) != 0) {
			iscsi_log_settings &= ~LOG_SET(log_setting);
		    }
		}
	    }

	    printk("iSCSI: log settings %8x\n", iscsi_log_settings);
	    smp_mb();
	} else if ((cmd_len = find_keyword(bp, end, "connfailtimeout"))) {
	    int bus, id, timeout;
	    int len;
	    struct iscsi_session *session;
	    struct iscsi_portal_info *p = NULL;
	    unsigned int portal;

	    bp += cmd_len;
	    if ((len = find_value(bp, end, &bus)) == 0) {
		printk("iSCSI: Invalid bus specified %s \n", bp);
		return length;
	    }
	    bp += len;
	    if ((len = find_value(bp, end, &id)) == 0) {
		printk("iSCSI: Invalid target id specified %s\n", bp);
		return length;
	    }
	    bp += len;
	    if ((len = find_number((char *) bp, (char *) end, &timeout) == 0)) {
		printk("iSCSI : Invalid timeout specified %s\n", bp);
		return length;
	    }
	    if ((bus != WILD_CARD) && (id != WILD_CARD)) {
		session = find_session_by_bus(bus, id);
		if (!session) {
		    printk("iSCSI: session corresponding to bus %d target %d "
			   "does not exist", bus, id);
		    return length;
		}
		session->replacement_timeout = timeout;
		spin_lock(&session->portal_lock);
		p = session->portals;
		for (portal = 0; portal < session->num_portals; portal++) {
		    p[portal].replacement_timeout = timeout;
		}
		spin_unlock(&session->portal_lock);

		/* we can't know which wait_q the tx thread is in
		 * (if any), so wake them both
		 */
		wake_up(&session->tx_wait_q);
		wake_up(&session->login_wait_q);
		drop_reference(session);
	    } else if (iscsi_hba_list) {
		spin_lock(&iscsi_hba_list_lock);
		for (hba = iscsi_hba_list; hba; hba = hba->next) {
		    SPIN_LOCK_NOQUEUE(&hba->session_lock);
		    for (session = hba->session_list_head; session;
			 session = session->next) {
			if ((bus == WILD_CARD) && (id != WILD_CARD)) {
			    if (session->target_id == id) {
				session->replacement_timeout = timeout;
				spin_lock(&session->portal_lock);
				p = session->portals;
				for (portal = 0; portal < session->num_portals;
				     portal++) {
				    p[portal].replacement_timeout = timeout;
				}
				spin_unlock(&session->portal_lock);
				/* we can't know which wait_q the 
				 * tx_thread is in (if any), so 
				 * wake them both 
				 */
				wake_up(&session->tx_wait_q);
				wake_up(&session->login_wait_q);
			    }
			} else if ((id == WILD_CARD) && (bus != WILD_CARD)) {
			    if (session->channel == bus) {
				session->replacement_timeout = timeout;
				spin_lock(&session->portal_lock);
				p = session->portals;
				for (portal = 0; portal < session->num_portals;
				     portal++) {
				    p[portal].replacement_timeout = timeout;
				}
				spin_unlock(&session->portal_lock);
				/* we can't know which wait_q the 
				 * tx_thread is in (if any), so 
				 * wake them both 
				 */
				wake_up(&session->tx_wait_q);
				wake_up(&session->login_wait_q);
			    }
			} else {
			    /*Apply to all sessions */
			    session->replacement_timeout = timeout;
			    spin_lock(&session->portal_lock);
			    p = session->portals;
			    for (portal = 0; portal < session->num_portals;
				 portal++) {
				p[portal].replacement_timeout = timeout;
			    }
			    spin_unlock(&session->portal_lock);
			    /* we can't know which wait_q the 
			     * tx_thread is in (if any), so 
			     * wake them both 
			     */
			    wake_up(&session->tx_wait_q);
			    wake_up(&session->login_wait_q);
			}
		    }
		    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
		}
		spin_unlock(&iscsi_hba_list_lock);
	    }
	} else if ((cmd_len = find_keyword(bp, end, "diskcommandtimeout"))) {
	    int bus, id, timeout;
	    int len;
	    struct iscsi_session *session;
	    struct iscsi_portal_info *p = NULL;
	    unsigned int portal;

	    bp += cmd_len;
	    if ((len = find_value(bp, end, &bus)) == 0) {
		printk("iSCSI: Invalid bus specified %s \n", bp);
		return length;
	    }
	    bp += len;
	    if ((len = find_value(bp, end, &id)) == 0) {
		printk("iSCSI: Invalid target id specified %s\n", bp);
		return length;
	    }
	    bp += len;
	    if ((len = find_number((char *) bp, (char *) end, &timeout) == 0)) {
		printk("iSCSI : Invalid timeout specified %s\n", bp);
		return length;
	    }
	    if ((bus != WILD_CARD) && (id != WILD_CARD)) {
		session = find_session_by_bus(bus, id);
		if (!session) {
		    printk("iSCSI: session corresponding to bus %d target %d "
			   "does not exist", bus, id);
		    return length;
		}
		session->disk_command_timeout = timeout;
		spin_lock(&session->portal_lock);
		p = session->portals;
		for (portal = 0; portal < session->num_portals; portal++) {
		    p[portal].disk_command_timeout = timeout;
		}
		spin_unlock(&session->portal_lock);
		start_timer(session);
		drop_reference(session);
	    } else if (iscsi_hba_list) {
		spin_lock(&iscsi_hba_list_lock);
		for (hba = iscsi_hba_list; hba; hba = hba->next) {
		    SPIN_LOCK_NOQUEUE(&hba->session_lock);
		    for (session = hba->session_list_head; session;
			 session = session->next) {
			if ((bus == WILD_CARD) && (id != WILD_CARD)) {
			    if (session->target_id == id) {
				session->disk_command_timeout = timeout;
				spin_lock(&session->portal_lock);
				p = session->portals;
				for (portal = 0; portal < session->num_portals;
				     portal++) {
				    p[portal].disk_command_timeout = timeout;
				}
				spin_unlock(&session->portal_lock);
				start_timer(session);
			    }
			} else if ((id == WILD_CARD) && (bus != WILD_CARD)) {
			    if (session->channel == bus) {
				session->disk_command_timeout = timeout;
				spin_lock(&session->portal_lock);
				p = session->portals;
				for (portal = 0; portal < session->num_portals;
				     portal++) {
				    p[portal].disk_command_timeout = timeout;
				}
				spin_unlock(&session->portal_lock);
				start_timer(session);
			    }
			} else {
			    /*Apply to all sessions */
			    session->disk_command_timeout = timeout;
			    spin_lock(&session->portal_lock);
			    p = session->portals;
			    for (portal = 0; portal < session->num_portals;
				 portal++) {
				p[portal].disk_command_timeout = timeout;
			    }
			    spin_unlock(&session->portal_lock);
			    start_timer(session);
			}
		    }
		    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
		}
		spin_unlock(&iscsi_hba_list_lock);
	    }
	} else if ((cmd_len = find_keyword(bp, end, "shutdown"))) {
	    /* try to shutdown the driver */
	    if (!iscsi_shutdown()) {
		printk("iSCSI: driver shutdown failed\n");
	    }
	} else if ((cmd_len = find_keyword(bp, end, "lun"))) {
	    bp += cmd_len;

	    if ((cmd_len = find_number(bp, end, &bus)) == 0) {
		printk("iSCSI: /proc/scsi/iscsi couldn't determine bus number "
		       "of session\n");
		return length;
	    }
	    bp += cmd_len;

	    if ((cmd_len = find_number(bp, end, &target)) == 0) {
		printk("iSCSI: /proc/scsi/iscsi couldn't determine target id "
		       "number of session\n");
		return length;
	    }
	    bp += cmd_len;

	    if ((cmd_len = find_number(bp, end, &lun)) == 0) {
		printk("iSCSI: /proc/scsi/iscsi couldn't determine logical "
		       "unit number\n");
		return length;
	    }
	    bp += cmd_len;

	    session = find_session_by_bus(bus, target);
	    if (session) {

		if ((cmd_len = find_keyword(bp, end, "ignore"))) {
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &completions);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &aborts);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &abort_task_sets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &lun_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &warm_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &cold_resets);

		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u LUN %u at %lu, ignore %u completions, "
			   "%u aborts, %u abort task sets, %u LUN resets, "
			   "%u warm target resets, %u cold target resets\n",
			   session, bus, target, lun, jiffies, completions,
			   aborts, abort_task_sets, lun_resets, warm_resets,
			   cold_resets);

		    spin_lock(&session->task_lock);
		    session->ignore_lun = lun;
		    session->ignore_completions = completions;
		    session->ignore_aborts = aborts;
		    session->ignore_abort_task_sets = abort_task_sets;
		    session->ignore_lun_resets = lun_resets;
		    session->ignore_warm_resets = warm_resets;
		    session->ignore_cold_resets = cold_resets;
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "reject"))) {
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &aborts);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &abort_task_sets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &lun_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &warm_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &cold_resets);

		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u LUN %u at %lu, reject %u aborts, "
			   "%u abort task sets, %u LUN resets, %u warm "
			   "target resets, %u cold target resets\n",
			   session, bus, target, lun, jiffies, aborts,
			   abort_task_sets, lun_resets, warm_resets,
			   cold_resets);

		    spin_lock(&session->task_lock);
		    session->reject_lun = lun;
		    session->reject_aborts = aborts;
		    session->reject_abort_task_sets = abort_task_sets;
		    session->reject_lun_resets = lun_resets;
		    session->reject_warm_resets = warm_resets;
		    session->reject_cold_resets = cold_resets;
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "unreachable"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = lun;
		    session->fake_status_unreachable = count;
		    printk("iSCSI: session %p will fake %u iSCSI transport "
			   "errors from LUN %u at %lu\n",
			   session, count, lun, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "busy"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = lun;
		    session->fake_status_busy = count;
		    printk("iSCSI: session %p will fake %u SCSI status "
			   "BUSY responses from LUN %u at %lu\n",
			   session, count, lun, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "queuefull"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = lun;
		    session->fake_status_queue_full = count;
		    printk("iSCSI: session %p will fake %u SCSI status "
			   "QUEUE_FULL responses from LUN %u at %lu\n",
			   session, count, lun, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "aborted"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = lun;
		    session->fake_status_aborted = count;
		    printk("iSCSI: session %p will fake %u target command "
			   "aborts from LUN %u at %lu\n",
			   session, count, lun, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "tasktimeouts"))) {
		    iscsi_task_t *t;
		    unsigned int count = 0xFFFFFFFF;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);
		    printk("iSCSI: session %p faking up to %u task "
			   "timeouts for LUN %u at %lu\n",
			   session, count, lun, jiffies);

		    spin_lock(&session->task_lock);
		    /* fake task timeouts, to try to test
		     * the error recovery code 
		     */
		    for (t = session->arrival_order.head; t; t = t->order_next) {
			if (count == 0)
			    break;

			if ((t->lun == lun) && !test_bit(0, &t->timedout)) {
			    printk("iSCSI: session %p faking task timeout "
				   "of itt %u, task %p, LUN %u, sc %p at %lu\n",
				   session, t->itt, t, t->lun, t->scsi_cmnd,
				   jiffies);

			    /* make the task look like it timedout */
			    del_task_timer(t);
			    set_bit(t->lun, session->luns_timing_out);
			    smp_wmb();
			    set_bit(0, &t->timedout);
			    smp_mb();

			    count--;
			}
		    }
		    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "commandtimeouts"))) {
		    iscsi_task_t *t;
		    Scsi_Cmnd *sc;
		    unsigned int count = 0xFFFFFFFF;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);
		    printk("iSCSI: session %p faking up to %u command "
			   "timeouts for LUN %u at %lu\n",
			   session, count, lun, jiffies);

		    spin_lock(&session->task_lock);
		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    /* fake command timeouts for all tasks and queued commands */
		    for (t = session->arrival_order.head; t; t = t->order_next) {
			if (count == 0)
			    goto finished_lun;

			if ((t->lun == lun) && t->scsi_cmnd
			    && !test_bit(COMMAND_TIMEDOUT,
					 command_flags(t->scsi_cmnd))) {
			    printk("iSCSI: session %p faking command timeout "
				   "of itt %u, task %p, LUN %u, cmnd %p "
				   "at %lu\n",
				   session, t->itt, t, t->lun, t->scsi_cmnd,
				   jiffies);

			    /* make the task look like it timedout */
			    del_command_timer(t->scsi_cmnd);
			    set_bit(COMMAND_TIMEDOUT,
				    command_flags(t->scsi_cmnd));
			    count--;
			}
		    }
		    for (sc = session->retry_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished_lun;

			if (sc->lun == lun) {
			    printk("iSCSI: session %p faking command timeout "
				   "of retry cmnd %p, LUN %u, at %lu\n",
				   session, sc, sc->lun, jiffies);
			    del_command_timer(sc);
			    set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			    count--;
			}
		    }
		    for (sc = session->deferred_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished_lun;

			if (sc->lun == lun) {
			    printk("iSCSI: session %p faking command timeout "
				   "of deferred cmnd %p, LUN %u, at %lu\n",
				   session, sc, sc->lun, jiffies);
			    del_command_timer(sc);
			    set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			    count--;
			}
		    }
		    for (sc = session->scsi_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished_lun;

			if (sc->lun == lun) {
			    printk("iSCSI: session %p faking command timeout "
				   "of normal cmnd %p, LUN %u, at %lu\n",
				   session, sc, sc->lun, jiffies);
			    del_command_timer(sc);
			    set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			    count--;
			}
		    }

		  finished_lun:
		    smp_wmb();
		    set_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits);
		    smp_wmb();

		    /* wake up the tx thread to deal with the timeout */
		    set_bit(TX_WAKE, &session->control_bits);
		    smp_mb();
		    /* we can't know which wait_q the tx
		     * thread is in (if any), so wake them
		     * both 
		     */
		    wake_up(&session->tx_wait_q);
		    wake_up(&session->login_wait_q);

		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "status"))) {
		    spin_lock(&session->task_lock);
		    printk("iSCSI: session %p LUN %u detected=%s, "
			   "activated=%s, timing out=%s, doing recovery=%s, "
			   "delaying commands=%s, unreachable=%s\n",
			   session, lun, test_bit(lun,
						  session->
						  luns_detected) ? "yes" : "no",
			   test_bit(lun,
				    session->luns_activated) ? "yes" : "no",
			   test_bit(lun,
				    session->luns_timing_out) ? "yes" : "no",
			   test_bit(lun,
				    session->
				    luns_doing_recovery) ? "yes" : "no",
			   test_bit(lun,
				    session->
				    luns_delaying_commands) ? "yes" : "no",
			   test_bit(lun,
				    session->luns_unreachable) ? "yes" : "no");

		    spin_unlock(&session->task_lock);
		}

		/* done with the session */
		drop_reference(session);
	    } else {
		printk("iSCSI: /proc/scsi/iscsi failed to find session "
		       "bsu %u target %u LUN %u\n", bus, target, lun);
	    }
	} else if ((cmd_len = find_keyword(bp, end, "target"))) {
	    bp += cmd_len;

	    if ((cmd_len = find_number(bp, end, &bus)) == 0) {
		printk("iSCSI: /proc/scsi/iscsi couldn't determine host "
		       "number of session\n");
		return length;
	    }
	    bp += cmd_len;

	    if ((cmd_len = find_number(bp, end, &target)) == 0) {
		printk("iSCSI: /proc/scsi/iscsi couldn't determine target "
		       "id number of session\n");
		return length;
	    }
	    bp += cmd_len;

	    session = find_session_by_bus(bus, target);
	    if (session) {
		if ((cmd_len = find_keyword(bp, end, "nop"))) {
		    unsigned int data_length = 0;

		    bp += cmd_len;
		    if ((cmd_len = find_number(bp, end, &data_length)) == 0) {
			printk("iSCSI: session %p Nop test couldn't determine "
			       "data length\n", session);
			return length;
		    }
		    bp += cmd_len;

		    if (data_length) {
			printk("iSCSI: session %p for bus %u target %u, %d "
			       "byte Nop data test requested at %lu\n",
			       session, bus, target, data_length, jiffies);
			iscsi_ping_test_session(session, data_length);
		    } else {
			printk("iSCSI: session %p for bus %u target %u, 0 "
			       "byte Nop data test ignored at %lu\n",
			       session, bus, target, jiffies);
		    }
		} else if ((cmd_len = find_keyword(bp, end, "portal"))) {
		    if (session->portal_failover) {
			unsigned int portal = 0;

			bp += cmd_len;
			if ((cmd_len = find_number(bp, end, &portal)) == 0) {
			    printk("iSCSI: /proc/scsi/iscsi session %p for "
				   "bus %u target %u, no portal specified\n",
				   session, bus, target);
			    return length;
			}
			bp += cmd_len;

			spin_lock(&session->portal_lock);
#ifndef DEBUG
			if (portal < session->num_portals) {
#endif
			    session->requested_portal = portal;
			    session->fallback_portal = session->current_portal;

			    printk("iSCSI: /proc/scsi/iscsi session %p for "
				   "bus %u target %u requesting switch to "
				   "portal %u at %lu\n",
				   session, bus, target, portal, jiffies);

			    /* request a logout for the current session */
			    spin_lock(&session->task_lock);
			    iscsi_request_logout(session,
						 session->active_timeout,
						 session->active_timeout);
			    spin_unlock(&session->task_lock);
#ifndef DEBUG
			} else {
			    printk("iSCSI: /proc/scsi/iscsi session %p for "
				   "bus %u target %u can't switch to "
				   "portal %u, only %d portals\n",
				   session, bus, target, portal,
				   session->num_portals);
			}
#endif
			spin_unlock(&session->portal_lock);
		    } else
			printk("iSCSI: /proc/scsi/iscsi session %p for "
			       "bus %u target %u can't switch to "
			       "requested portal, because portal failover "
			       "is disabled.\n", session, bus, target);
		} else if ((cmd_len = find_keyword(bp, end, "address"))) {
		    if (session->portal_failover) {
			char ip[16];
			char address[17];
			int ip_length = 4;
			iscsi_portal_info_t *p = NULL;
			unsigned int portal = 0;

			bp += cmd_len;
			memset(address, 0, sizeof (address));
			if ((cmd_len = find_ip(bp, end, address)) == 0) {
			    printk("iSCSI: /proc/scsi/iscsi session %p for "
				   "bus %u target %u, no ip address specified\n",
				   session, bus, target);
			} else {
			    bp += cmd_len;
			    p = session->portals;
			    for (portal = 0; portal < session->num_portals;
				 portal++) {
				iscsi_inet_aton(address, ip, &ip_length);
				if (memcmp
				    (p[portal].ip_address, ip,
				     p[portal].ip_length) == 0) {
				    break;
				}
			    }

			    spin_lock(&session->portal_lock);
			    if (memcmp
				(session->ip_address, ip,
				 p[portal].ip_length) == 0) {
				printk("iSCSI: Requested address %s is "
				       "already the current portal, failover "
				       "not needed \n", address);
			    } else if (portal < session->num_portals) {
				session->requested_portal = portal;
				session->fallback_portal =
				    session->current_portal;

				printk("iSCSI: /proc/scsi/iscsi session %p "
				       "for bus %u target %u requesting "
				       "switch to ip %s at %lu\n",
				       session, bus, target, address, jiffies);
				/* request a logout for the current session */

				spin_lock(&session->task_lock);
				iscsi_request_logout(session,
						     session->active_timeout,
						     session->active_timeout);
				spin_unlock(&session->task_lock);
			    } else {
				address[16] = '\0';
				printk("iSCSI: /proc/scsi/iscsi session %p "
				       "for bus %u target %u can't switch "
				       "to ip %s\n",
				       session, bus, target, address);
			    }
			    spin_unlock(&session->portal_lock);
			}
		    } else
			printk("iSCSI: /proc/scsi/iscsi session %p for bus "
			       "%u target %u can't switch to requested ip, "
			       "because portal failover is disabled\n",
			       session, bus, target);
		} else if ((cmd_len = find_keyword(bp, end, "logout"))) {
		    unsigned int deadline = 0;
		    unsigned int response_deadline = 0;

		    bp += cmd_len;
		    if ((cmd_len = find_number(bp, end, &deadline)) == 0) {
			deadline = session->active_timeout;
		    }
		    bp += cmd_len;

		    if ((cmd_len =
			 find_number(bp, end, &response_deadline)) == 0) {
			response_deadline = session->active_timeout;
		    }
		    bp += cmd_len;

		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u requesting logout at %lu, "
			   "logout deadline %u seconds, response "
			   "deadline %u seconds\n",
			   session, bus, target, jiffies, deadline,
			   response_deadline);

		    /* request a logout for the current session */
		    spin_lock(&session->task_lock);
		    iscsi_request_logout(session, session->active_timeout,
					 session->active_timeout);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "drop"))) {
		    unsigned int time2wait = 0;

		    bp += cmd_len;
		    if ((cmd_len = find_number(bp, end, &time2wait))) {
			session->time2wait = time2wait;
			smp_mb();
			printk("iSCSI: /proc/scsi/iscsi dropping session "
			       "%p for bus %u target %u at %lu, time2wait %u\n",
			       session, bus, target, jiffies, time2wait);
		    } else {
			printk("iSCSI: /proc/scsi/iscsi dropping session %p "
			       "for bus %u target %u at %lu\n",
			       session, bus, target, jiffies);
		    }
		    bp += cmd_len;

		    iscsi_drop_session(session);
		} else if ((cmd_len = find_keyword(bp, end, "terminate"))) {
		    printk("iSCSI: /proc/scsi/iscsi terminating session %p "
			   "for bus %u target %u at %lu\n",
			   session, bus, target, jiffies);
		    iscsi_terminate_session(session);
		} else if ((cmd_len = find_keyword(bp, end, "queues"))) {
		    /* show all of the session's queues */
		    spin_lock(&session->task_lock);
		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    printk("iSCSI: session %p to %s, bits 0x%08lx, next "
			   "itt %u at %lu\n",
			   session, session->log_name, session->control_bits,
			   session->itt, jiffies);
		    printk("iSCSI: session %p ExpCmdSN %08u, next CmdSN %08u, "
			   "MaxCmdSN %08u, window closed %lu times, full %lu "
			   "times\n",
			   session, session->ExpCmdSn, session->CmdSn,
			   session->MaxCmdSn, session->window_closed,
			   session->window_full);
		    print_session_tasks(session);
		    print_session_cmnds(session);
		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "notready"))) {
		    unsigned int notready = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &notready);

		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    session->fake_not_ready = notready;
		    printk("iSCSI: session %p will fake %u NOT_READY errors "
			   "at %lu\n", session, notready, jiffies);
		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		} else if ((cmd_len = find_keyword(bp, end, "printcommand"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    session->print_cmnds = count;
		    printk("iSCSI: session %p will print %u commands at %lu\n",
			   session, count, jiffies);
		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		} else if ((cmd_len = find_keyword(bp, end, "unreachable"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = -1;
		    session->fake_status_unreachable = count;
		    printk("iSCSI: session %p will fake %u iSCSI transport "
			   "errors at %lu\n", session, count, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "busy"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = -1;
		    session->fake_status_busy = count;
		    printk("iSCSI: session %p will fake %u SCSI status BUSY "
			   "responses at %lu\n", session, count, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "queuefull"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = -1;
		    session->fake_status_queue_full = count;
		    printk("iSCSI: session %p will fake %u SCSI status "
			   "QUEUE_FULL responses at %lu\n",
			   session, count, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "aborted"))) {
		    unsigned int count = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);

		    spin_lock(&session->task_lock);
		    session->fake_status_lun = -1;
		    session->fake_status_aborted = count;
		    printk("iSCSI: session %p will fake %u target command "
			   "aborted responses at %lu\n",
			   session, count, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "tasktimeouts"))) {
		    iscsi_task_t *t;
		    unsigned int count = 0xFFFFFFFF;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);
		    printk("iSCSI: session %p faking up to %u task timeouts "
			   "at %lu\n", session, count, jiffies);

		    spin_lock(&session->task_lock);
		    /* fake task timeouts, to try to test
		     * the error recovery code 
		     */
		    for (t = session->arrival_order.head; t; t = t->order_next) {
			if (count == 0)
			    break;

			if (!test_bit(0, &t->timedout)) {
			    printk("iSCSI: session %p faking task timeout "
				   "of itt %u, task %p, LUN %u, sc %p at %lu\n",
				   session, t->itt, t, t->lun, t->scsi_cmnd,
				   jiffies);

			    /* make the task look like it timedout */
			    del_task_timer(t);
			    set_bit(t->lun, session->luns_timing_out);
			    smp_wmb();
			    set_bit(0, &t->timedout);
			    smp_mb();
			    count--;
			}
		    }
		    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "commandtimeouts"))) {
		    iscsi_task_t *t;
		    Scsi_Cmnd *sc;
		    unsigned int count = 0xFFFFFFFF;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &count);
		    printk("iSCSI: session %p faking up to %u command timeouts "
			   "at %lu\n", session, count, jiffies);

		    spin_lock(&session->task_lock);
		    SPIN_LOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    /* fake command timeouts for all tasks and queued commands */
		    for (t = session->arrival_order.head; t; t = t->order_next) {
			if (count == 0)
			    goto finished;

			if (t->scsi_cmnd
			    && !test_bit(COMMAND_TIMEDOUT,
					 command_flags(t->scsi_cmnd))) {
			    printk("iSCSI: session %p faking command timeout "
				   "of itt %u, task %p, LUN %u, cmnd %p "
				   "at %lu\n",
				   session, t->itt, t, t->lun, t->scsi_cmnd,
				   jiffies);

			    /* make the task look like it timedout */
			    del_command_timer(t->scsi_cmnd);
			    set_bit(COMMAND_TIMEDOUT,
				    command_flags(t->scsi_cmnd));
			    count--;
			}
		    }
		    for (sc = session->retry_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished;

			printk("iSCSI: session %p faking command timeout "
			       "of retry cmnd %p, LUN %u, at %lu\n",
			       session, sc, sc->lun, jiffies);
			del_command_timer(sc);
			set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			count--;
		    }
		    for (sc = session->deferred_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished;

			printk("iSCSI: session %p faking command timeout "
			       "of deferred cmnd %p, LUN %u, at %lu\n",
			       session, sc, sc->lun, jiffies);
			del_command_timer(sc);
			set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			count--;
		    }
		    for (sc = session->scsi_cmnd_head; sc;
			 sc = (Scsi_Cmnd *) sc->host_scribble) {
			if (count == 0)
			    goto finished;

			printk("iSCSI: session %p faking command timeout "
			       "of normal cmnd %p, LUN %u, at %lu\n",
			       session, sc, sc->lun, jiffies);
			del_command_timer(sc);
			set_bit(COMMAND_TIMEDOUT, command_flags(sc));
			count--;
		    }

		  finished:
		    smp_wmb();
		    set_bit(SESSION_COMMAND_TIMEDOUT, &session->control_bits);
		    smp_wmb();

		    /* wake up the tx thread to deal with the timeout */
		    set_bit(TX_WAKE, &session->control_bits);
		    smp_mb();
		    /* we can't know which wait_q the tx
		     * thread is in (if any), so wake them
		     * both 
		     */
		    wake_up(&session->tx_wait_q);
		    wake_up(&session->login_wait_q);

		    SPIN_UNLOCK_NOQUEUE(&session->scsi_cmnd_lock);
		    spin_unlock(&session->task_lock);
		} else
		    if ((cmd_len =
			 find_keyword(bp, end, "writeheadermismatch"))) {
		    unsigned int mismatch = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &mismatch);

		    spin_lock(&session->task_lock);
		    session->fake_write_header_mismatch = mismatch;
		    printk("iSCSI: session %p will fake %u write HeaderDigest "
			   "mismatches at %lu\n", session, mismatch, jiffies);
		    spin_unlock(&session->task_lock);
		} else
		    if ((cmd_len = find_keyword(bp, end, "readdatamismatch"))) {
		    unsigned int mismatch = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &mismatch);

		    spin_lock(&session->task_lock);
		    session->fake_read_data_mismatch = mismatch;
		    printk("iSCSI: session %p will fake %u read DataDigest "
			   "mismatches at %lu\n", session, mismatch, jiffies);
		    spin_unlock(&session->task_lock);
		} else
		    if ((cmd_len =
			 find_keyword(bp, end, "writedatamismatch"))) {
		    unsigned int mismatch = 1;

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &mismatch);

		    spin_lock(&session->task_lock);
		    session->fake_write_data_mismatch = mismatch;
		    printk("iSCSI: session %p will fake %u write DataDigest "
			   "mismatches at %lu\n", session, mismatch, jiffies);
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "ignore"))) {

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &completions);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &aborts);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &abort_task_sets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &lun_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &warm_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &cold_resets);

		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u at %lu, ignore %u completions, %u "
			   "aborts, %u abort task sets, %u LUN resets, %u "
			   "warm target resets, %u cold target resets\n",
			   session, bus, target, jiffies, completions, aborts,
			   abort_task_sets, lun_resets, warm_resets,
			   cold_resets);

		    spin_lock(&session->task_lock);
		    session->ignore_lun = -1;
		    session->ignore_completions = completions;
		    session->ignore_aborts = aborts;
		    session->ignore_abort_task_sets = abort_task_sets;
		    session->ignore_lun_resets = lun_resets;
		    session->ignore_warm_resets = warm_resets;
		    session->ignore_cold_resets = cold_resets;
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "reject"))) {

		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &aborts);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &abort_task_sets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &lun_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &warm_resets);
		    bp += cmd_len;
		    cmd_len = find_number(bp, end, &cold_resets);

		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u at %lu, reject %u aborts, %u abort "
			   "task sets, %u LUN resets, %u warm target resets, "
			   "%u cold target resets\n",
			   session, bus, target, jiffies, aborts,
			   abort_task_sets, lun_resets, warm_resets,
			   cold_resets);

		    spin_lock(&session->task_lock);
		    session->reject_lun = -1;
		    session->reject_aborts = aborts;
		    session->reject_abort_task_sets = abort_task_sets;
		    session->reject_lun_resets = lun_resets;
		    session->reject_warm_resets = warm_resets;
		    session->reject_cold_resets = cold_resets;
		    spin_unlock(&session->task_lock);
		} else if ((cmd_len = find_keyword(bp, end, "reset"))) {
		    printk("iSCSI: /proc/scsi/iscsi session %p warm target "
			   "reset requested at %lu\n", session, jiffies);
		    wake_tx_thread(SESSION_RESET_REQUESTED, session);
		} else if ((cmd_len = find_keyword(bp, end, "commandtimedout"))) {
		    printk("iSCSI: /proc/scsi/iscsi session %p waking tx "
			   "thread SESSION_COMMAND_TIMEDOUT at %lu\n",
			   session, jiffies);
		    wake_tx_thread(SESSION_COMMAND_TIMEDOUT, session);
		} else if ((cmd_len = find_keyword(bp, end, "tasktimedout"))) {
		    printk("iSCSI: /proc/scsi/iscsi session %p waking tx "
			   "thread SESSION_TASK_TIMEDOUT at %lu\n",
			   session, jiffies);
		    wake_tx_thread(SESSION_TASK_TIMEDOUT, session);
		} else if ((cmd_len = find_keyword(bp, end, "retry"))) {
		    printk("iSCSI: /proc/scsi/iscsi session %p waking tx "
			   "thread SESSION_RETRY_COMMANDS at %lu\n",
			   session, jiffies);
		    wake_tx_thread(SESSION_RETRY_COMMANDS, session);
		} else if ((cmd_len = find_keyword(bp, end, "txcommand"))) {
		    printk("iSCSI: /proc/scsi/iscsi session %p waking tx "
			   "thread TX_SCSI_COMMAND at %lu\n", session, jiffies);
		    wake_tx_thread(TX_SCSI_COMMAND, session);
		} else {
		    printk("iSCSI: /proc/scsi/iscsi session %p for bus %u "
			   "target %u, unknown command\n",
			   session, bus, target);
		}

		/* done with the session */
		drop_reference(session);
	    } else {
		printk("iSCSI: /proc/scsi/iscsi failed to find session for "
		       "bus %u target %u\n", bus, target);
	    }
	}

	/* FIXME: some SCSI read and write tests would be useful as
	 * well.  Allow user to specify read6, write6, read10,
	 * write10, specify the command queue size (max outstanding),
	 * total number of commands, command buffer size, starting
	 * block offset, and block increment per command.  This should
	 * let us do sequential or fixed offset I/O tests, and try to
	 * determine throughput without having do worry about what the
	 * SCSI layer or applications above us are capable of.  Also,
	 * consider a flag that controls writing/validating a data
	 * pattern.  We don't always want to do it, but it may be
	 * useful sometimes.
	 */
	return length;
    } else {
	/* it's a read */
	char *build_str = BUILD_STR;
	off_t current_offset = 0;
	off_t finish = offset + length;

	DEBUG_FLOW("iSCSI: /proc read, buffer %p, start %p, offset %lu, "
		   "length %d, hostno %d\n",
		   buffer, start, offset, length, hostno);

	/* comment header with version number */
	/* FIXME: we assume the buffer length is always large enough for 
	 * our header 
	 */
	if (build_str) {
	    /* developer-built variant of a 4-digit internal release */
	    bp +=
		sprintf(bp, "# iSCSI driver version: %d.%d.%d.%d%s variant "
			"(%s)\n#\n",
			DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
			DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION,
			DRIVER_EXTRAVERSION, ISCSI_DATE);
	} else if (DRIVER_INTERNAL_VERSION > 0) {
	    /* 4-digit release */
	    bp += sprintf(bp, "# iSCSI driver version: %d.%d.%d.%d%s (%s)\n#\n",
			  DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
			  DRIVER_PATCH_VERSION, DRIVER_INTERNAL_VERSION,
			  DRIVER_EXTRAVERSION, ISCSI_DATE);
	} else {
	    /* 3-digit release */
	    bp += sprintf(bp, "# iSCSI driver version: %d.%d.%d%s (%s)\n#\n",
			  DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION,
			  DRIVER_PATCH_VERSION, DRIVER_EXTRAVERSION,
			  ISCSI_DATE);
	}
	bp += sprintf(bp, "# SCSI:               iSCSI:\n");
	bp +=
	    sprintf(bp,
		    "# Bus Tgt LUN         IP address   Port  TargetName\n");

	*start = buffer;
	current_offset = bp - buffer;

	if (offset >= current_offset) {
	    bp = buffer;	/* don't need any of that header, toss it all */
	    DEBUG_FLOW("iSCSI: /proc skipping header, current offset %lu, "
		       "buffer %p\n", current_offset, bp);
	} else if (offset != 0) {
	    /* need only some of the header */
	    char *src = buffer + offset;
	    char *dst = buffer;

	    /* memmove what we need to the beginning of the
	     * buffer, since we may have to return the whole
	     * buffer length to avoid prematurely indicating
	     * EOF.
	     */
	    while ((*dst++ = *src++)) ;

	    bp = dst;

	    DEBUG_FLOW("iSCSI: /proc partial header, current offset %lu, "
		       "buffer %p\n", current_offset, bp);
	} else {
	    DEBUG_FLOW("iSCSI: /proc full header, current offset %lu, "
		       "buffer %p\n", current_offset, bp);
	}

	/* find the HBA corresponding to hostno */
	spin_lock(&iscsi_hba_list_lock);
	hba = iscsi_hba_list;
	while (hba && hba->host->host_no != hostno)
	    hba = hba->next;
	spin_unlock(&iscsi_hba_list_lock);

	if (hba) {
	    SPIN_LOCK_NOQUEUE(&hba->session_lock);
	    session = hba->session_list_head;
	    while (session) {
		if (!show_session_luns
		    (session, offset, finish, &current_offset, &bp))
		    break;
		session = session->next;
	    }
	    SPIN_UNLOCK_NOQUEUE(&hba->session_lock);
	} else {
	    printk("iSCSI: /proc read couldn't find HBA #%d\n", hostno);
	}

	/* tell the caller about the output */
	if (current_offset <= offset) {
	    /* return no valid data if the desired offset is beyond the 
	     * total "file" length 
	     */
	    DEBUG_FLOW("iSCSI: /proc read returning 0 of %ld (EOF), start %p\n",
		       finish - offset, *start);
	    return 0;
	}

	if ((current_offset - offset) > length)
	    return length;

	/* return how much valid data is in the buffer */
	DEBUG_FLOW("iSCSI: /proc read returning %ld of %ld, start %p\n",
		   current_offset - offset, finish - offset, *start);
	return (current_offset - offset);
    }
}

/*
 * We cannot include scsi_module.c because the daemon has not got a connection
 * up yet.
 */
static int
ctl_open(struct inode *inode, struct file *file)
{
    MOD_INC_USE_COUNT;
    return 0;
}

static int
ctl_close(struct inode *inode, struct file *file)
{
    MOD_DEC_USE_COUNT;
    return 0;
}

static int
ctl_ioctl(struct inode *inode,
	  struct file *file, unsigned int cmd, unsigned long arg)
{
    int rc = 0;

    if (cmd == ISCSI_ESTABLISH_SESSION) {
	iscsi_session_ioctl_t *ioctld = kmalloc(sizeof (*ioctld), GFP_KERNEL);
	iscsi_session_t *session = NULL;
	iscsi_portal_info_t *portals = NULL;
	int probe_luns;

	if (!ioctld) {
	    printk("iSCSI: couldn't allocate space for session ioctl data\n");
	    return -ENOMEM;
	}
	if (copy_from_user(ioctld, (void *) arg, sizeof (*ioctld))) {
	    printk("iSCSI: Cannot copy session ioctl data\n");
	    kfree(ioctld);
	    return -EFAULT;
	}

	DEBUG_INIT("iSCSI: ioctl establish session to %s at %lu\n",
		   ioctld->TargetName, jiffies);

	if (ioctld->ioctl_size != sizeof (iscsi_session_ioctl_t)) {
	    printk("iSCSI: ioctl size %u incorrect, expecting %Zu\n",
		   ioctld->ioctl_size, sizeof (*ioctld));
	    kfree(ioctld);
	    return -EINVAL;
	}
	if (ioctld->ioctl_version != ISCSI_SESSION_IOCTL_VERSION) {
	    printk("iSCSI: ioctl version %u incorrect, expecting %u\n",
		   ioctld->ioctl_version, ISCSI_SESSION_IOCTL_VERSION);
	    kfree(ioctld);
	    return -EINVAL;
	}
	if (ioctld->portal_info_size != sizeof (iscsi_portal_info_t)) {
	    printk("iSCSI: ioctl portal info size %u incorrect, "
		   "expecting %Zu\n",
		   ioctld->portal_info_size, sizeof (*portals));
	    kfree(ioctld);
	    return -EINVAL;
	}
	if (ioctld->num_portals == 0) {
	    printk("iSCSI: ioctl has no portals\n");
	    kfree(ioctld);
	    return -EINVAL;
	}

	/* allocate the portals */
	if (ioctld->num_portals <= 0) {
	    printk("iSCSI: bus %d target %d has no portals in session ioctl\n",
		   ioctld->iscsi_bus, ioctld->target_id);
	    kfree(ioctld);
	    return -EINVAL;
	}
	portals =
	    (iscsi_portal_info_t *) kmalloc(ioctld->num_portals *
					    sizeof (*portals), GFP_KERNEL);
	if (portals == NULL) {
	    printk("iSCSI: bus %d target %d cannot allocate %d portals "
		   "for session ioctl\n",
		   ioctld->iscsi_bus, ioctld->target_id, ioctld->num_portals);
	    kfree(ioctld);
	    return -ENOMEM;
	}
	DEBUG_INIT("iSCSI: bus %d target %d allocated portals %p "
		   "(size %u) at %lu\n",
		   ioctld->iscsi_bus, ioctld->target_id, portals,
		   ioctld->num_portals * sizeof (*portals), jiffies);
	memset(portals, 0, ioctld->num_portals * sizeof (*portals));

	/* copy the portal info from the user ioctl structure */
	if (copy_from_user
	    (portals,
	     (void *) arg + offsetof(struct iscsi_session_ioctl, portals),
	     ioctld->num_portals * sizeof (*portals))) {
	    printk("iSCSI: bus %d target %d cannot copy portal info, "
		   "ioctl %p, size %Zu, portals %p\n",
		   ioctld->iscsi_bus, ioctld->target_id, ioctld,
		   sizeof (*portals),
		   (void *) arg + offsetof(struct iscsi_session_ioctl,
					   portals));
	    kfree(ioctld);
	    kfree(portals);
	    return -EFAULT;
	} else {
	    DEBUG_ALLOC("iSCSI: copied %u bytes of portal info from %p to %p\n",
			ioctld->num_portals * sizeof (*portals),
			(void *) arg + offsetof(struct iscsi_session_ioctl,
						portals), portals);
	}

	/* if this is the daemon's ioctl for a new session
	 * process, then we need to wait for the session to be
	 * established and probe LUNs, regardless of whether or not
	 * the session already exists, and regardless of the config
	 * number of any existing session.  This is because the config
	 * is guaranteed to be the newest, regardless of the config
	 * number, and because the daemon needs to know if the session
	 * failed to start in order for the session process to exit
	 * with the appropriate exit value.
	 *
	 * if this is an update and there is no existing session, then
	 * we need to create a session, wait for it to be established,
	 * and probe LUNs for the new session.
	 *
	 * if this is an update and there is an existing session, then
	 * we need to update the config if the ioctl config number is
	 * greater than the existing session's config number.
	 * Regardless of whether or not the config update was
	 * accepted, LUN probing must occur if requested.  If LUN
	 * probing is already in progress, return -EBUSY so that the
	 * daemon tries again later.  
	 */
	probe_luns = ioctld->probe_luns;

	/* create or update a session */
	do {
	    if ((session =
		 find_session_by_bus(ioctld->iscsi_bus, ioctld->target_id))) {
		if (strcmp(ioctld->TargetName, session->TargetName) == 0) {
		    rc = update_session(session, ioctld, portals);
		    if (rc < 0) {
			/* error out */
			goto done;
		    }
		} else {
		    /* otherwise error out */
		    printk("iSCSI: bus %d target %d already bound to %s\n",
			   ioctld->iscsi_bus, ioctld->target_id,
			   session->TargetName);
		    drop_reference(session);
		    kfree(ioctld);
		    kfree(portals);
		    return 0;
		}
	    } else if ((session = allocate_session(ioctld, portals))) {
		/* the config_mutex is initialized to locked, so that
		 * any calls to update_session block if they see the
		 * session we're about to add before we've had a chance 
		 * to clear old LUNs and start the session threads.
		 */
		if (add_session(session)) {
		    /* preallocate a task for this session to use in
		     * case the HBA's task_cache ever becomes empty,
		     * since we can use SLAB_KERNEL now, but would
		     * have to use SLAB_ATOMIC later.  We have to do
		     * this after adding the session to the HBA, so
		     * that a driver shutdown can see this session and
		     * wait for it to terminate.  Otherwise we'd fail
		     * to detroy the task cache because this session
		     * still had a task allocated, but wasn't visible
		     * to the shutdown code.
		     */
		    if ((session->preallocated_task =
			 kmem_cache_alloc(session->hba->task_cache,
					  SLAB_KERNEL))) {
			DEBUG_ALLOC("iSCSI: session %p preallocated task %p "
				    "at %lu\n",
				    session, session->preallocated_task,
				    jiffies);
			__set_bit(TASK_PREALLOCATED,
				  &session->preallocated_task->flags);
		    } else {
			printk("iSCSI: session %p couldn't preallocate task "
			       "at %lu\n", session, jiffies);
			drop_reference(session);
			kfree(ioctld);
			return -ENOMEM;
		    }

		    /* We now own the bus/target id.  Clear
		     * away any old luns.
		     */
		    iscsi_remove_luns(session);
		    
		    /* give the caller the host and channel
		     * numbers we just claimed 
		     */
		    ioctld->host_number = session->host_no;
		    ioctld->channel = session->channel;

		    /* unless we already have one, start a timer thread */
		    if (!test_and_set_bit(0, &iscsi_timer_running)) {
			DEBUG_INIT("iSCSI: starting timer thread at %lu\n",
				   jiffies);
			if (kernel_thread(iscsi_timer_thread, NULL, 0) < 0) {
			    printk("iSCSI: failed to start timer thread "
				   "at %lu\n", jiffies);
			    drop_reference(session);
			    kfree(ioctld);
			    up(&session->config_mutex);
			    return -ENOMEM;
			}
		    }

		    /* try to start the threads for this session */
		    rc = start_session_threads(session);

		    /* unlock the mutex so that any waiting
		     * or future update_session() calls can
		     * proceed 
		     */
		    up(&session->config_mutex);

		    if (rc < 0)
			goto done;	/* we failed to start the 
					 * session threads 
					 */

		    /* always probe LUNs when we create a new session */
		    probe_luns = 1;
		} else {
		    /* some session claimed this bus/target id while
		     * we were allocating a session.  Loop, so that we
		     * either update the existing session or error
		     * out.
		     */
		    drop_reference(session);
		    session = NULL;
		}
	    } else {
		/* couldn't allocate a new session. error
		 * out and let the daemonr etry the ioctl 
		 */
		kfree(ioctld);
		kfree(portals);
		return -ENOMEM;
	    }
	} while (session == NULL);

	if (probe_luns || !ioctld->update) {
	    /* wait for the session login to complete */
	    DEBUG_INIT("iSCSI: ioctl waiting for session %p at %lu\n", session,
		       jiffies);
	    wait_for_session(session, FALSE);
	    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
		printk("iSCSI: session %p terminating, ioctl returning "
		       "at %lu\n", session, jiffies);
		goto done;
	    } else if (signal_pending(current)) {
		iscsi_terminate_session(session);
		printk("iSCSI: session %p ioctl terminated, returning at %lu\n",
		       session, jiffies);
		goto done;
	    }
	}

	if (probe_luns) {
	    /* if another ioctl is already trying to probe
	     * LUNs, must wait for it to finish 
	     */
	    if (test_and_set_bit(SESSION_PROBING_LUNS, &session->control_bits)) {
		DEBUG_INIT("iSCSI: session %p already has an ioctl probing "
			   "or waiting to probe LUNs for bus %d, target %d\n",
			   session, ioctld->iscsi_bus, ioctld->target_id);
		rc = -EBUSY;
		goto done;
	    }
	    /* first figure out what LUNs actually exist */
	    iscsi_detect_luns(session);
	    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
		printk("iSCSI: session %p terminating, ioctl returning "
		       "at %lu\n", session, jiffies);
		clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
		smp_mb();
		goto done;
	    } else if (signal_pending(current)) {
		iscsi_terminate_session(session);
		printk("iSCSI: session %p ioctl terminated, returning at %lu\n",
		       session, jiffies);
		clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
		smp_mb();
		goto done;
	    }

	    /* and then try to probe the intersection of the
	     * allowed and detected LUNs 
	     */
	    iscsi_probe_luns(session, session->luns_allowed);

	    /* and then we're done */
	    clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
	    smp_mb();

	    if (test_bit(SESSION_TERMINATING, &session->control_bits)) {
		printk("iSCSI: session %p terminating, ioctl returning "
		       "at %lu\n", session, jiffies);
		goto done;
	    } else if (signal_pending(current)) {
		iscsi_terminate_session(session);
		printk("iSCSI: session %p ioctl terminated, returning at %lu\n",
		       session, jiffies);
		goto done;
	    }
	}

	rc = 1;

      done:
	/* pass back the TargetAlias to the caller */
	memcpy(ioctld->TargetAlias, session->TargetAlias,
	       MIN(sizeof (ioctld->TargetAlias),
		   sizeof (session->TargetAlias)));
	ioctld->TargetAlias[sizeof (ioctld->TargetAlias) - 1] = '\0';
	if (copy_to_user((void *) arg, ioctld, sizeof (*ioctld))) {
	    printk("iSCSI: failed to copy ioctl data back to user mode "
		   "for session %p\n", session);
	}
	kfree(ioctld);

	drop_reference(session);

	if (signal_pending(current))
	    return -EINTR;
	else
	    return rc;
    } else if (cmd == ISCSI_SET_INBP_INFO) {
	if (copy_from_user(&iscsi_inbp_info, (void *) arg,
			   sizeof (iscsi_inbp_info))) {
	    printk("iSCSI: Cannot copy set_inbp_info ioctl data\n");
	    return -EFAULT;
	}
	return (set_inbp_info());
    } else if (cmd == ISCSI_CHECK_INBP_BOOT) {
	if (copy_to_user((int *) arg, &this_is_iscsi_boot,
			 sizeof (this_is_iscsi_boot))) {
	    printk("iSCSI: Cannot copy out this_is_iscsi_boot variable\n");
	    return -EFAULT;
	}
	return 0;
    } else if (cmd == ISCSI_SHUTDOWN) {
	return iscsi_shutdown();
    } else if (cmd == ISCSI_RESET_PROBING) {
	return iscsi_reset_lun_probing();
    } else if (cmd == ISCSI_PROBE_LUNS) {
	iscsi_session_t *session = NULL;
	iscsi_probe_luns_ioctl_t *ioctld =
	    kmalloc(sizeof (*ioctld), GFP_KERNEL);

	if (!ioctld) {
	    printk("iSCSI: couldn't allocate space for probe ioctl data\n");
	    return -ENOMEM;
	}
	if (copy_from_user(ioctld, (void *) arg, sizeof (*ioctld))) {
	    printk("iSCSI: Cannot copy session ioctl data\n");
	    kfree(ioctld);
	    return -EFAULT;
	}
	if (ioctld->ioctl_size != sizeof (*ioctld)) {
	    printk("iSCSI: ioctl size %u incorrect, expecting %u\n",
		   ioctld->ioctl_size, (unsigned int) sizeof (*ioctld));
	    kfree(ioctld);
	    return -EINVAL;
	}
	if (ioctld->ioctl_version != ISCSI_PROBE_LUNS_IOCTL_VERSION) {
	    printk("iSCSI: ioctl version %u incorrect, expecting %u\n",
		   ioctld->ioctl_version, ISCSI_PROBE_LUNS_IOCTL_VERSION);
	    kfree(ioctld);
	    return -EINVAL;
	}

	rc = 0;

	/* find the session */
	session = find_session_by_bus(ioctld->iscsi_bus, ioctld->target_id);
	if (session == NULL) {
	    printk("iSCSI: ioctl probe LUNs (bus %d, target %d) failed, no "
		   "session\n", ioctld->iscsi_bus, ioctld->target_id);
	    goto done_probing;
	}

	/* if another ioctl is already trying to probe LUNs,
	 * we don't need a 2nd 
	 */
	if (test_and_set_bit(SESSION_PROBING_LUNS, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p already has an ioctl probing or "
		       "waiting to probe LUNs for bus %d, target %d\n",
		       session, ioctld->iscsi_bus, ioctld->target_id);
	    rc = 1;
	    goto done_probing;
	}

	if (signal_pending(current))
	    goto done_probing;
	if (test_bit(SESSION_TERMINATING, &session->control_bits))
	    goto done_probing;

	/* if the session has been established before, but isn't established now,
	 * try to wait for it, at least until we get signalled or the session
	 * replacement timeout expires.
	 */
	if (!test_bit(SESSION_ESTABLISHED, &session->control_bits)) {
	    DEBUG_INIT("iSCSI: session %p LUN probe ioctl for bus %d, "
		       "target %d waiting for session to be established "
		       "at %lu\n",
		       session, ioctld->iscsi_bus, ioctld->target_id, jiffies);
	}
	if (wait_for_session(session, TRUE)) {
	    DEBUG_INIT("iSCSI: session %p ioctl triggering LUN probe for "
		       "bus %d, target %d at %lu\n",
		       session, ioctld->iscsi_bus, ioctld->target_id, jiffies);

	    iscsi_detect_luns(session);

	    if (signal_pending(current))
		goto done_probing;
	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		goto done_probing;

	    iscsi_probe_luns(session, session->luns_allowed);
	    if (signal_pending(current))
		goto done_probing;
	    if (test_bit(SESSION_TERMINATING, &session->control_bits))
		goto done_probing;

	    rc = 1;
	} else {
	    /* we got signalled or the session replacement timer expired */
	    DEBUG_INIT("iSCSI: session %p LUN probe ioctl for bus %d, "
		       "target %d failed\n",
		       session, ioctld->iscsi_bus, ioctld->target_id);

	    rc = 0;
	}

      done_probing:
	if (session) {
	    clear_bit(SESSION_PROBING_LUNS, &session->control_bits);
	    smp_mb();
	    drop_reference(session);
	}
	kfree(ioctld);
	return rc;
    } else if (cmd == ISCSI_TERMINATE_SESSION) {
	iscsi_terminate_session_ioctl_t ioctld;
	iscsi_session_t *session = NULL;

	if (copy_from_user(&ioctld, (void *) arg, sizeof (ioctld))) {
	    printk("iSCSI: Cannot copy session ioctl data\n");
	    return -EFAULT;
	}
	if (ioctld.ioctl_size != sizeof (ioctld)) {
	    printk("iSCSI: terminate session ioctl size %u incorrect, "
		   "expecting %Zu\n", ioctld.ioctl_size, sizeof (ioctld));
	    return -EINVAL;
	}
	if (ioctld.ioctl_version != ISCSI_TERMINATE_SESSION_IOCTL_VERSION) {
	    printk("iSCSI: terminate session ioctl version %u incorrect, "
		   "expecting %u\n",
		   ioctld.ioctl_version, ISCSI_TERMINATE_SESSION_IOCTL_VERSION);
	    return -EINVAL;
	}

	/* find the session */
	session = find_session_by_bus(ioctld.iscsi_bus, ioctld.target_id);
	if (session) {
	    if (!session->this_is_root_disk) {
		printk("iSCSI: bus %d target %d session %p terminated by "
		       "ioctl\n", ioctld.iscsi_bus, ioctld.target_id, session);
		iscsi_terminate_session(session);
		drop_reference(session);
		return 1;
	    } else {
		printk("iSCSI: bus %d target %d session %p NOT terminated "
		       "by ioctl because this session belongs to ROOT disk\n",
		       ioctld.iscsi_bus, ioctld.target_id, session);
		return 1;
	    }
	} else {
	    printk("iSCSI: terminate session ioctl for bus %d target %d "
		   "failed, no session\n", ioctld.iscsi_bus, ioctld.target_id);
	    return 0;
	}
    } else if (cmd == ISCSI_GETTRACE) {
#if DEBUG_TRACE
	iscsi_trace_dump_t dump;
	iscsi_trace_dump_t *user_dump;
	int rc;
	DECLARE_NOQUEUE_FLAGS;

	user_dump = (iscsi_trace_dump_t *) arg;
	if (copy_from_user(&dump, user_dump, sizeof (dump))) {
	    printk("iSCSI: trace copy_from_user %p, %p, %Zu failed\n",
		   &dump, user_dump, sizeof (dump));
	    return -EFAULT;
	}

	if (dump.dump_ioctl_size != sizeof (iscsi_trace_dump_t)) {
	    printk("iSCSI: trace dump ioctl size is %Zu, but caller uses %u\n",
		   sizeof (iscsi_trace_dump_t), dump.dump_ioctl_size);
	    return -EINVAL;
	}

	if (dump.dump_version != TRACE_DUMP_VERSION) {
	    printk("iSCSI: trace dump version is %u, but caller uses %u\n",
		   TRACE_DUMP_VERSION, dump.dump_version);
	    return -EINVAL;
	}

	if (dump.trace_entry_size != sizeof (iscsi_trace_entry_t)) {
	    printk("iSCSI: trace dump ioctl size is %Zu, but caller uses %u\n",
		   sizeof (iscsi_trace_dump_t), dump.dump_ioctl_size);
	    return -EINVAL;
	}

	if (dump.num_entries < ISCSI_TRACE_COUNT) {
	    /* tell the caller to use a bigger buffer */
	    dump.num_entries = ISCSI_TRACE_COUNT;
	    if (copy_to_user(user_dump, &dump, sizeof (dump)))
		return -EFAULT;
	    else
		return -E2BIG;
	}

	/* the caller is responsible for zeroing the buffer
	 * before the ioctl, so if the caller asks for too
	 * many entries, it should be able to tell which
	 * ones actually have data.
	 */

	/* only send what we've got */
	dump.num_entries = ISCSI_TRACE_COUNT;
	if (copy_to_user(user_dump, &dump, sizeof (dump))) {
	    printk("iSCSI: trace copy_to_user %p, %p, %Zu failed\n",
		   user_dump, &dump, sizeof (dump));
	    return -EFAULT;
	}

	SPIN_LOCK_NOQUEUE(&iscsi_trace_lock);
	/* FIXME: copy_to_user may sleep, but we're holding
	 * a spin_lock with interrupts off 
	 */
	if (copy_to_user
	    (user_dump->trace, &trace_table[0],
	     dump.num_entries * sizeof (iscsi_trace_entry_t))) {
	    printk("iSCSI: trace copy_to_user %p, %p, %u failed\n",
		   user_dump->trace, &trace_table[0], dump.num_entries);
	    SPIN_UNLOCK_NOQUEUE(&iscsi_trace_lock);
	    return -EFAULT;
	}
	rc = trace_index;
	printk("iSCSI: copied %d trace entries to %p at %lu\n",
	       dump.num_entries, user_dump->trace, jiffies);
	SPIN_UNLOCK_NOQUEUE(&iscsi_trace_lock);

	return rc;
#else
	printk("iSCSI: iSCSI kernel module does not implement tracing\n");
	return -ENXIO;
#endif
    } else if (cmd == ISCSI_LS_TARGET_INFO) {
	int target_index, bus_index, rc = 0;
	target_info_t tmp_buf;
	iscsi_hba_t *hba;
	iscsi_session_t *session;

	if (copy_from_user(&tmp_buf, (void *) arg, sizeof (target_info_t))) {
	    printk("iSCSI: Cannot copy user-level data\n");
	    return -EFAULT;
	}
	target_index = tmp_buf.target_id;
	bus_index = tmp_buf.channel;
	spin_lock(&iscsi_hba_list_lock);
	for (hba = iscsi_hba_list; hba; hba = hba->next) {
	    spin_lock(&hba->session_lock);
	    for (session = hba->session_list_head; session;
		 session = session->next) {
		if ((session->target_id == target_index)
		    && (session->channel == bus_index)) {
		    tmp_buf.host_no = session->host_no;
		    tmp_buf.channel = session->channel;
		    strcpy(tmp_buf.target_name, session->TargetName);
		    strcpy(tmp_buf.target_alias, session->TargetAlias);
		    tmp_buf.num_portals = session->num_portals;
		    memcpy(tmp_buf.session_data.isid, session->isid,
			   sizeof (session->isid));
		    tmp_buf.session_data.tsih = session->tsih;
		    tmp_buf.session_data.addr[0] = session->ip_address[0];
		    tmp_buf.session_data.addr[1] = session->ip_address[1];
		    tmp_buf.session_data.addr[2] = session->ip_address[2];
		    tmp_buf.session_data.addr[3] = session->ip_address[3];
		    tmp_buf.session_data.port = session->port;

		    tmp_buf.session_data.establishment_time =
			(jiffies - session->session_established_time) / HZ;
		    tmp_buf.session_data.session_drop_time =
			(jiffies - session->session_drop_time) / HZ;
		    tmp_buf.session_data.ever_established =
			session->ever_established;
		    tmp_buf.session_data.session_alive = session->session_alive;
		    tmp_buf.session_data.InitialR2T = session->InitialR2T;
		    tmp_buf.session_data.ImmediateData = session->ImmediateData;
		    tmp_buf.session_data.HeaderDigest = session->HeaderDigest;
		    tmp_buf.session_data.DataDigest = session->DataDigest;
		    tmp_buf.session_data.FirstBurstLength =
			session->FirstBurstLength;
		    tmp_buf.session_data.MaxBurstLength =
			session->MaxBurstLength;
		    tmp_buf.session_data.MaxRecvDataSegmentLength =
			session->MaxRecvDataSegmentLength;
		    tmp_buf.session_data.MaxXmitDataSegmentLength =
			session->MaxXmitDataSegmentLength;
		    tmp_buf.session_data.login_timeout = session->login_timeout;
		    tmp_buf.session_data.auth_timeout = session->auth_timeout;
		    tmp_buf.session_data.active_timeout =
			session->active_timeout;
		    tmp_buf.session_data.idle_timeout = session->idle_timeout;
		    tmp_buf.session_data.ping_timeout = session->ping_timeout;
		}
	    }
	    spin_unlock(&hba->session_lock);
	}
	spin_unlock(&iscsi_hba_list_lock);
	if (copy_to_user((void *) arg, &tmp_buf, sizeof (target_info_t))) {
	    printk("iSCSI: failed to copy target-specific data back to "
		   "user mode\n");
	    return -EFAULT;
	}
	return rc;
    } else if (cmd == ISCSI_LS_PORTAL_INFO) {
	iscsi_hba_t *hba;
	iscsi_session_t *session;
	portal_list_t portalp;
	int flag = 0;
	int num_portals = 0;
	iscsi_portal_info_t *portal_copy = NULL;

	if (copy_from_user(&portalp, (void *) arg, sizeof (portalp))) {
	    printk("iSCSI: Cannot copy user-level data\n");
	    return -EFAULT;
	}

	spin_lock(&iscsi_hba_list_lock);
	for (hba = iscsi_hba_list; hba; hba = hba->next) {
	    spin_lock(&hba->session_lock);
	    for (session = hba->session_list_head; session;
		 session = session->next) {
		if ((session->target_id == portalp.target_id)
		    && (session->channel == portalp.bus_id)) {
		    portal_copy =
			(iscsi_portal_info_t *) kmalloc(session->num_portals *
							sizeof
							(iscsi_portal_info_t),
							GFP_ATOMIC);
		    memcpy(portal_copy, session->portals,
			   session->num_portals * sizeof (iscsi_portal_info_t));
		    num_portals = session->num_portals;
		    flag = 1;
		    break;
		}
	    }
	    spin_unlock(&hba->session_lock);
	    if (flag == 1)
		break;
	}
	spin_unlock(&iscsi_hba_list_lock);
	if (flag == 1) {
	    if (copy_to_user
		(portalp.portals, portal_copy,
		 num_portals * sizeof (iscsi_portal_info_t))) {
		printk("iSCSI: failed to copy target-specific data back to "
		       "user mode\n");
		return -EFAULT;
	    }
	    kfree(portal_copy);
	}
	return 0;

    }

    printk("iSCSI: Requested ioctl not found\n");
    return -EINVAL;
}

Scsi_Host_Template iscsi_driver_template = {
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,5,44) )
  next:NULL,
  module:NULL,
  proc_dir:NULL,
#endif
  proc_info:iscsi_proc_info,
  name:NULL,
  detect:iscsi_detect,
  release:iscsi_release,
  info:iscsi_info,
  ioctl:NULL,
  command:NULL,
  queuecommand:iscsi_queuecommand,
  eh_strategy_handler:NULL,
  eh_abort_handler:NULL,
  eh_device_reset_handler:iscsi_eh_device_reset,
  eh_bus_reset_handler:iscsi_eh_bus_reset,
  eh_host_reset_handler:iscsi_eh_host_reset,
  abort:NULL,
  reset:NULL,
#if defined(HAS_SLAVE_CONFIGURE)
  slave_alloc:iscsi_slave_alloc,
  slave_configure:iscsi_slave_configure,
  slave_destroy:iscsi_slave_destroy,
#elif defined(HAS_NEW_SLAVE_ATTACH)
  slave_attach:iscsi_slave_attach,
  slave_detach:iscsi_slave_detach,
#else
  slave_attach:NULL,
#endif
  bios_param:iscsi_biosparam,
  this_id:-1,
  can_queue:ISCSI_CANQUEUE,
  sg_tablesize:ISCSI_MAX_SG,
  cmd_per_lun:ISCSI_CMDS_PER_LUN,
  present:0,
  unchecked_isa_dma:0,
  use_clustering:ENABLE_CLUSTERING,
  max_sectors:256,
#if ( LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0) )
  use_new_eh_code:1,
#endif
  emulated:1
};

#ifdef MODULE
EXPORT_NO_SYMBOLS;

static int __init
iscsi_init(void)
{
    char *build_str = BUILD_STR;
    int ret = -ENODEV;

    DEBUG_INIT("iSCSI: init module\n");

    if (build_str) {
	/* developer-built variant of a 4-digit internal release */
	printk("iSCSI: %d.%d.%d.%d%s variant (%s) built for Linux %s\n"
	       "iSCSI: %s\n",
	       DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION,
	       DRIVER_INTERNAL_VERSION, DRIVER_EXTRAVERSION, ISCSI_DATE,
	       UTS_RELEASE, build_str);
    } else if (DRIVER_INTERNAL_VERSION > 0) {
	/* 4-digit release */
	printk("iSCSI: %d.%d.%d.%d%s (%s) built for Linux %s\n",
	       DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION,
	       DRIVER_INTERNAL_VERSION, DRIVER_EXTRAVERSION, ISCSI_DATE,
	       UTS_RELEASE);
    } else {
	/* 3-digit release */
	printk("iSCSI: %d.%d.%d%s (%s) built for Linux %s\n",
	       DRIVER_MAJOR_VERSION, DRIVER_MINOR_VERSION, DRIVER_PATCH_VERSION,
	       DRIVER_EXTRAVERSION, ISCSI_DATE, UTS_RELEASE);
    }

    /* log any module param settings the user has changed */
    if (translate_deferred_sense)
	printk("iSCSI: will translate deferred sense to current sense on "
	       "disk command responses\n");

    if (force_tcq)
	printk("iSCSI: will force tagged command queueing for all devices\n");

    if (untagged_queue_depth != 1)
	printk("iSCSI: untagged queue depth %d\n", untagged_queue_depth);

    control_major = register_chrdev(0, control_name, &control_fops);
    if (control_major < 0) {
	printk("iSCSI: failed to register the control device\n");
	return control_major;
    }
    printk("iSCSI: control device major number %d\n", control_major);

    iscsi_driver_template.module = THIS_MODULE;

    REGISTER_SCSI_HOST(&iscsi_driver_template);
    if (iscsi_driver_template.present) {
	ret = 0;
    } else {
	printk("iSCSI: failed to register SCSI HBA driver\n");
	unregister_chrdev(control_major, control_name);
	UNREGISTER_SCSI_HOST(&iscsi_driver_template);
    }

    set_bit(0, &init_module_complete);
    return ret;
}

static void __exit
iscsi_cleanup(void)
{
    pid_t pid = 0;
    int rc;

    DEBUG_INIT("iSCSI: cleanup module\n");
    if (control_major > 0) {
	rc = unregister_chrdev(control_major, control_name);
	if (rc) {
	    printk("iSCSI: error trying to unregister control device\n");
	} else {
	    control_major = 0;
	}
    }

    if (iscsi_driver_template.present) {
	DEBUG_INIT("iSCSI: SCSI template present\n");
	/* this will cause the SCSI layer to call our iscsi_release function */
	UNREGISTER_SCSI_HOST(&iscsi_driver_template);
	iscsi_driver_template.present = 0;
    }

    /* kill the timer */
    if ((pid = iscsi_timer_pid))
	kill_proc(pid, SIGKILL, 1);

    /* wait for the timer to exit */
    while (test_bit(0, &iscsi_timer_running)) {
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(MSECS_TO_JIFFIES(10));
    }

    DEBUG_INIT("iSCSI: cleanup module complete\n");
    return;
}

module_init(iscsi_init);
module_exit(iscsi_cleanup);

#endif
