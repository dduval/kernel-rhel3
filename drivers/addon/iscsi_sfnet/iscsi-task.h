#ifndef ISCSI_TASK_H_
#define ISCSI_TASK_H_

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
 * $Id: iscsi-task.h,v 1.9.2.1 2004/08/10 23:04:49 coughlan Exp $
 *
 * iscsi-task.h
 *
 *   define the iSCSI task structure needed by the kernel module
 * 
 */

#include "iscsi-kernel.h"

struct iscsi_session;

/* task flags */

#define TASK_CONTROL       1
#define TASK_WRITE         2
#define TASK_READ          3

/* internal driver state for the task */
#define TASK_INITIAL_R2T   4
#define TASK_PREALLOCATED  5	/* preallocated by a
				 * session, never freed to
				 * the task cache 
				 */
#define TASK_NEEDS_RETRY   6
#define TASK_COMPLETED     7

/* what type of task mgmt function to try next */
#define TASK_TRY_ABORT          8
#define TASK_TRY_ABORT_TASK_SET 9
#define TASK_TRY_LUN_RESET      10
#define TASK_TRY_WARM_RESET     11

#define TASK_TRY_COLD_RESET     12

/* we need to check and sometimes clear all of the TRY_ bits at once */
#define TASK_RECOVERY_MASK      0x1F00UL
#define TASK_NEEDS_RECOVERY(task) ((task)->flags & TASK_RECOVERY_MASK)

typedef struct iscsi_task {
    struct iscsi_task *order_next;
    struct iscsi_task *order_prev;
    struct iscsi_task *next;
    struct iscsi_task *prev;
    Scsi_Cmnd *scsi_cmnd;
    struct iscsi_session *session;
    atomic_t refcount;
    uint32_t rxdata;
    unsigned long flags;	/* guarded by session->task_lock */
    uint32_t cmdsn;		/* need to record so that
				 * aborts can set RefCmdSN
				 * properly 
				 */
    uint32_t itt;
    uint32_t ttt;
    uint32_t mgmt_itt;		/* itt of a task mgmt command for this task */
    unsigned int data_offset;	/* explicit R2T */
    int data_length;		/* explicit R2T */
    unsigned int lun;
    unsigned long timedout;	/* separate from flags so
				 * that the flags don't need
				 * to be atomically
				 * updated 
				 */
    struct timer_list timer;	/* task timer used to trigger error recovery */
} iscsi_task_t;

typedef struct iscsi_task_collection {
    struct iscsi_task *volatile head;
    struct iscsi_task *volatile tail;
} iscsi_task_collection_t;

#endif
