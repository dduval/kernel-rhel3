#ifndef ISCSI_KERNEL_H_
#define ISCSI_KERNEL_H_

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
 * $Id: iscsi-kernel.h,v 1.1.2.1 2004/08/10 23:04:48 coughlan Exp $
 *
 * iscsi-kernel.h
 *
 *   hide variations in various Linux kernel versions   
 * 
 */

/* useful 2.4-ism */
#ifndef set_current_state
#  define set_current_state(state_value) \
	do { current->state = state_value; mb(); } while(0)
#endif

/* the interface to the SCSI code varies between kernels */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,53) )
#  define HAS_NEW_SCSI_DEPTH 1
#  define HAS_NEW_DEVICE_LISTS 1
#  define HAS_SLAVE_CONFIGURE 1
#elif ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,44) )
#  define HAS_NEW_SLAVE_ATTACH 1
#  define HAS_CMND_REQUEST_STRUCT 1
#else
#  define HAS_SELECT_QUEUE_DEPTHS 1
#  define HAS_CMND_REQUEST_STRUCT 1
#endif

/* scatterlists have changed for HIGHMEM support.  
 * Later 2.4 kernels may have unmapped segments, and
 * 2.5 kernels remove the address altogether.
 */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) )
#  include <linux/highmem.h>
#  define HAS_SCATTERLIST_PAGE    1
#  define HAS_SCATTERLIST_ADDRESS 0
#elif ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13) )
#  include <linux/highmem.h>
#  define HAS_SCATTERLIST_PAGE    1
#  define HAS_SCATTERLIST_ADDRESS 1
#elif defined (SCSI_HAS_HOST_LOCK)
/* Redhat Advanced Server calls itself 2.4.9, but has much
 * newer patches in it.  FIXME: find a better way to detect
 * whether scatterlists have page pointers or not.
 */
#  include <linux/highmem.h>
#  define HAS_SCATTERLIST_PAGE    1
#  define HAS_SCATTERLIST_ADDRESS 1
#else
#  define HAS_SCATTERLIST_PAGES   0
#  define HAS_SCATTERLIST_ADDRESS 0
#endif

/* hide the wait queue differences */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18) )
typedef struct wait_queue *wait_queue_head_t;
typedef struct wait_queue wait_queue_t;
# define init_waitqueue_head(q_head_ptr) \
	do { *(q_head_ptr) = NULL; mb(); } while (0)
# define init_waitqueue_entry(q_ptr, tsk) \
	do { (q_ptr)->task = (tsk); mb(); } while (0)
#endif

/* The scheduling policy name has been changed from SCHED_OTHER to
 * SCHED_NORMAL in linux kernel version 2.5.39
 */

#if defined (SCHED_NORMAL)
#define SCHED_OTHER SCHED_NORMAL
#endif

/* the lock we need to hold while checking pending signals */

/* Linux kernel version 2.5.60 onwards and Redhat 9.0 kernel 2.4.20-8
 * onwards implements NPTL ( Native Posix Thread Library ) which has
 * introduced some changes to signal lock members of task structure in
 * "sched.h". These changes have been taken care at few places below
 * through the introduction of INIT_SIGHAND variable.
 */

#if defined(INIT_SIGHAND)
#  define LOCK_SIGNALS()    spin_lock_irq(&current->sighand->siglock)
#  define UNLOCK_SIGNALS()  spin_unlock_irq(&current->sighand->siglock)
#elif ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
#  define LOCK_SIGNALS()    spin_lock_irq(&current->sig->siglock)
#  define UNLOCK_SIGNALS()  spin_unlock_irq(&current->sig->siglock)
#else
#  define LOCK_SIGNALS()  spin_lock_irq(&current->sigmask_lock)
#  define UNLOCK_SIGNALS()  spin_unlock_irq(&current->sigmask_lock)
#endif

/* determine if a particular signal is pending or not */
# if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
#  define SIGNAL_IS_PENDING(SIG) sigismember(&current->pending.signal, (SIG))
# else
#  define SIGNAL_IS_PENDING(SIG) sigismember(&current->signal, (SIG))
# endif

# if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0) || defined(INIT_SIGHAND) )
#  define RECALC_PENDING_SIGNALS recalc_sigpending()
# else
#  define RECALC_PENDING_SIGNALS recalc_sigpending(current)
# endif

/* we don't have to worry about ordering I/O and memory, just memory,
 * so we can use the smp_ memory barriers.  Older kernels don't have them,
 * so map them to the non-SMP barriers if need be.
 */
#ifndef smp_mb
# if defined(CONFIG_SMP) || defined(__SMP__)
#   define smp_mb()  mb()
# else
#   define smp_mb()  barrier()
# endif
#endif

#ifndef smp_wmb
# if defined(CONFIG_SMP) || defined(__SMP__)
#   define smp_wmb()  wmb()
# else
#   define smp_wmb()  barrier()
# endif
#endif

#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0) ) && \
	!defined (SCSI_HAS_HOST_LOCK) && !defined(__clear_bit)
#  define __clear_bit clear_bit
#endif

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
typedef unsigned long cpu_flags_t;
#else
typedef unsigned int cpu_flags_t;
#endif

/* kernels 2.2.16 through 2.5.21 call driver entry points with a lock
 * held and interrupts off, but the lock varies.  Hide the
 * differences, and give ourselves ways of releasing the lock in our
 * entry points, since we may need to call the scheduler, and can't do
 * that with a spinlock held and interrupts off, so we need to release
 * the lock and reenable interrupts, and then reacquire the lock
 * before returning from the entry point.
 */

/* for releasing the lock when we don't want it, but have it */
# define RELEASE_MIDLAYER_LOCK(host)  spin_unlock_irq((host)->host_lock)
# define REACQUIRE_MIDLAYER_LOCK(host) spin_lock_irq((host)->host_lock)
/* for getting the lock when we need it to call done(), but don't have it */
# define DECLARE_MIDLAYER_FLAGS cpu_flags_t midlayer_flags_
# define LOCK_MIDLAYER_LOCK(host) \
	spin_lock_irqsave((host)->host_lock, midlayer_flags_);
# define UNLOCK_MIDLAYER_LOCK(host) \
	spin_unlock_irqrestore((host)->host_lock, midlayer_flags_);

/* register as a SCSI HBA with the kernel */
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,2) )
#  define REGISTER_SCSI_HOST(template) scsi_register_host((template))
#  define UNREGISTER_SCSI_HOST(template) scsi_unregister_host((template))
#else
#  define REGISTER_SCSI_HOST(template) \
	scsi_register_module(MODULE_SCSI_HA, (template))
#  define UNREGISTER_SCSI_HOST(template) \
	scsi_unregister_module(MODULE_SCSI_HA, (template))
#endif

/* we need to ensure the SCSI midlayer won't call the queuecommand()
 * entry point from a bottom-half handler while a thread holding locks
 * that queuecommand() will need to acquire is suspended by an interrupt.
 * we don't use spin_lock_bh() on 2.4 kernels, because spin_unlock_bh()
 * will run bottom-half handlers, which is bad if interrupts are turned off
 * and the io_request_lock is held, since the SCSI bottom-half handler will
 * try to acquire the io_request_lock again and deadlock.
 */
#define DECLARE_NOQUEUE_FLAGS cpu_flags_t noqueue_flags_
#define SPIN_LOCK_NOQUEUE(lock) spin_lock_irqsave((lock), noqueue_flags_)
#define SPIN_UNLOCK_NOQUEUE(lock) spin_unlock_irqrestore((lock), noqueue_flags_)

/* Linux doesn't define the SCSI opcode REPORT_LUNS yet, but
 * we will, since we use it 
*/
#ifndef REPORT_LUNS
#  define REPORT_LUNS 0xa0
#endif

#define MSECS_TO_JIFFIES(ms) (((ms)*HZ+999)/1000)

#endif
