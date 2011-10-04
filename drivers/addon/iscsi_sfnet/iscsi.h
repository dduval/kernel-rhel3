#ifndef ISCSI_H_
#define ISCSI_H_

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
 * $Id: iscsi.h,v 1.23.2.1 2004/08/10 23:04:49 coughlan Exp $ 
 *
 * iscsi.h
 *
 *    include for iSCSI kernel module
 * 
 */

#include "iscsiAuthClient.h"
#include "iscsi-common.h"
#include "iscsi-limits.h"
#include "iscsi-task.h"
#include "iscsi-session.h"

#ifndef MIN
# define MIN(x, y)               ((x) < (y)) ? (x) : (y)
#endif

#ifndef MAX
# define MAX(x, y)               ((x) > (y)) ? (x) : (y)
#endif

typedef struct iscsi_hba {
    struct iscsi_hba *next;
    struct Scsi_Host *host;
    unsigned int host_no;
    unsigned long flags;
    spinlock_t session_lock;
    struct iscsi_session *session_list_head;
    struct iscsi_session *session_list_tail;
    atomic_t num_sessions;
    kmem_cache_t *task_cache;
} iscsi_hba_t;

/* HBA flags */
#define ISCSI_HBA_ACTIVE          0
#define ISCSI_HBA_SHUTTING_DOWN   1
#define ISCSI_HBA_RELEASING       2

/* driver entry points needed by the probing code */
int iscsi_queue(iscsi_session_t * session, Scsi_Cmnd * sc,
		void (*done) (Scsi_Cmnd *));
int iscsi_squash_cmnd(iscsi_session_t * session, Scsi_Cmnd * sc);

/* flags we set on Scsi_Cmnds */
#define COMMAND_TIMEDOUT 1

/* run-time controllable logging */
#define ISCSI_LOG_ERR   1
#define ISCSI_LOG_SENSE 2
#define ISCSI_LOG_INIT  3
#define ISCSI_LOG_QUEUE 4
#define ISCSI_LOG_ALLOC 5
#define ISCSI_LOG_EH    6
#define ISCSI_LOG_FLOW  7
#define ISCSI_LOG_RETRY 8
#define ISCSI_LOG_LOGIN 9
#define ISCSI_LOG_TIMEOUT 10

#define LOG_SET(flag) (1U << (flag))
#define LOG_ENABLED(flag) (iscsi_log_settings & (1U << (flag)))

extern volatile unsigned int iscsi_log_settings;

#ifdef DEBUG
/* compile in all the debug messages and tracing */
# define INCLUDE_DEBUG_ERROR  1
# define INCLUDE_DEBUG_TRACE  1
# define INCLUDE_DEBUG_INIT   1
# define INCLUDE_DEBUG_QUEUE  1
# define INCLUDE_DEBUG_FLOW   1
# define INCLUDE_DEBUG_ALLOC  1
# define INCLUDE_DEBUG_EH     1
# define INCLUDE_DEBUG_RETRY  1
# define INCLUDE_DEBUG_TIMEOUT 1
#else
/* leave out the tracing and most of the debug messages */
# define INCLUDE_DEBUG_ERROR  1
# define INCLUDE_DEBUG_TRACE  0
# define INCLUDE_DEBUG_INIT   0
# define INCLUDE_DEBUG_QUEUE  0
# define INCLUDE_DEBUG_FLOW   0
# define INCLUDE_DEBUG_ALLOC  0
# define INCLUDE_DEBUG_EH     0
# define INCLUDE_DEBUG_RETRY  0
# define INCLUDE_DEBUG_TIMEOUT 0
#endif

#if INCLUDE_DEBUG_INIT
#  define DEBUG_INIT(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_INIT)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_INIT(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_QUEUE
#  define DEBUG_QUEUE(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_QUEUE)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_QUEUE(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_FLOW
#  define DEBUG_FLOW(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_FLOW)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_FLOW(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_ALLOC
#  define DEBUG_ALLOC(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_ALLOC)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_ALLOC(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_RETRY
#  define DEBUG_RETRY(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_RETRY)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_RETRY(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_EH
#  define DEBUG_EH(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_EH)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_EH(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_ERROR
#  define DEBUG_ERR(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_ERR)) printk(fmt , ## args); } while (0)
#else
#  define DEBUG_ERR(fmt, args...) do { } while (0)
#endif

#if INCLUDE_DEBUG_TIMEOUT
#  define DEBUG_TIMEOUT(fmt, args...) \
	do { if (LOG_ENABLED(ISCSI_LOG_TIMEOUT)) printk(fmt , ## args); } \
	while (0)
#else
#  define DEBUG_TIMEOUT(fmt, args...) do { } while (0)
#endif

/* the Scsi_Cmnd's request_bufflen doesn't always match the actual amount of data
 * to be read or written.  Try to compensate by decoding the cdb.
 */
extern unsigned int iscsi_expected_data_length(Scsi_Cmnd * sc);

/* Scsi_cmnd->result */
#define DRIVER_BYTE(byte)   ((byte) << 24)
#define HOST_BYTE(byte)     ((byte) << 16)	/* HBA codes */
#define MSG_BYTE(byte)      ((byte) << 8)
#define STATUS_BYTE(byte)   ((byte))	/* SCSI status */

/* extract parts of the sense data from an (unsigned char *)
 * to the beginning of sense data 
 */
#define SENSE_KEY(sensebuf) ((sensebuf)[2] & 0x0F)
#define ASC(sensebuf)       ((sensebuf)[12])
#define ASCQ(sensebuf)       ((sensebuf)[13])

/* the Linux defines are bit shifted, so we define our own */
#define STATUS_CHECK_CONDITION 0x02
#define STATUS_BUSY 0x08
#define STATUS_QUEUE_FULL 0x28

#endif
