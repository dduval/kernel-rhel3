/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_sched.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef         LPFC_SCHED_H
#define         LPFC_SCHED_H

/*

There are two key data structures

A LUN Ring which is just a circular list of all LUNs with pending commands.

A command list.. a linked list of all pending commands for a particular LUN.
The Comand List is a FIFO used by the scheduler although there are routines
to search and remove a command from the lists.

The HBA structure a pointer to the LUN Ring along with a pointer to the
the next LUN to be checked for scheduling. The pointer moves around the ring
as the scheduler checks for commands to release. The HBA has an pointer to the
ring and a count of the number of LUNs on the ring. If the count is zero then
the pointers are undefined.

Pointers to each of the command lists pointers are stored in the LUN table
along with a count of the number of commands on the list.  The list is
singularly linked the LUN structure as a pointer to the first and last
command on the list. If a count is zero, the pointers are undefined.

Each Target Structure has a count of the number of LUNs in the target that
currently have pended commands. As LUNs enter and exit the LUN Ring, the count
is incremented and decremented respectively.

The HBA, Targets and LUNs have queue depth limits. There are two counters, the
number of outstanding commands sent to the SLI layer and the max. number of
commands that should be sent. Associated with each HBA, Target and LUN is a
status word that has two states OKAYTOSEND or Paused. If a paused, then no
pending commands form that object will be scheduled. If OKAYTOSEND then pending
commands will be released to the SLI layer as the queue depth window opens.


*/

/* **************************************************************************
**
**   The Pending Scheduler (called SCH here) requires a data structures in
**   each HBA, Target, LUN and SCSI_BUF. This interface defines the following
**      conventions for calling these structures :
**               LPFC_type_SCHED_t 
**                   where type is HBA, LUN, TARGET or SCSI_BUF.
**
**   This header should be included BEFORE the actual declarations of the
**   base structure are defined so the SCH structures can be inline.
**
**
** NOTE:  HBA and LUN SCH structures have a "count" field.
**        It represents the number of LUNs on the LUN RING and the
**        number of SCSI_BUFs on the command QUEUEs.  If the "count" is
**        identically 0, then the value of all pointers in the SCH structure
**        is NOT DEFINED.  Check the "count" field before attempting to
**        deference any pointers.
**
**   Locking consists of a lock in the HBA Scheduler Data
**   Considerations for working with LUNs or Targets:
**   1   Pause the LUN, Target, or HBA   LPFC_SCHED_PAUSE_<LUN,TARGET,HBA>
**   2   LPFC_SCHED_DEQUEUE_LUN each LUN for commands until 0 is returned
**   3   Make sure no more commands are sent down with that LUN
**
**   Once a LUN pending queue is empty, the Scheduler will have any references
**   to that LUN structure.
**
** ************************************************************************ */

/* ***************************************************************************
**
**   Forward declarations needed by the SCHED type structures.  Because
**   the actual structures will contain these structures and the SCH
**   structures contains pointers to the containers, the code forward declares
**   the structure names.
**
** ************************************************************************* */

struct lpfcHBA;
struct lpfcScsiTarget;
struct lpfcScsiLun;
struct lpfc_scsi_buf;
struct lpfcIocbq;

typedef enum {
	LPFC_SCHED_STATUS_OKAYTOSEND = 1,	/* Okay to search this list  */
	LPFC_SCHED_STATUS_PAUSED = 2,	/* Do not schedule this list */
} LPFC_SCHED_STATUS_t;

typedef struct lpfcSchedHBA {
	struct list_head highPriorityCmdList; /* High priority command queue */
	uint16_t q_cnt;
	uint16_t targetCount;	/* Number of elements on Ring */
	struct list_head targetRing;

	uint16_t maxOutstanding;	/* These 2 words implement queue */
	uint16_t currentOutstanding;	/* Depth on the HBA. */
	LPFC_SCHED_STATUS_t status;	/* Status word for stopping any
					   scheduling of the HBA */
        struct lpfcScsiTarget *nextTargetToCheck; 
} LPFC_SCHED_HBA_t;		/*   layer without queueing */

/* ****************************************************************************
**
**    Scheduler Data for Target
**
**   lunCount lunList and nextLunToCheck are used to
**   manage a list of LUNs that have pending commands
**   We keep here the queue depth for the Target, Target Queue Status
**
** ************************************************************************** */

typedef struct lpfcSchedTarget {
	struct list_head lunRing;

	int16_t lunCount;	/* Number of elements on Ring */
	int16_t maxOutstanding;	/* Max # of commands that can be outstanding */
	int16_t currentOutstanding; /* # of commands that currently
				       outstanding. */
	LPFC_SCHED_STATUS_t status;	/* Pended entries can be scheduled or */
	/*   or not at this time. */
	struct lpfcScsiLun *nextLunToCheck;
} LPFC_SCHED_TARGET_t;

/* ****************************************************************************
**
**    Scheduler Data for a LUN
**
**   SCSI_BUFs that are pending are keeped on a singly link list.
**   Each LUN_SCH has a pointer to the first and last entry.
**   In most cases 99.9999% all commands are released FIFO
**   exceptions are ABORT, LUN and Target Resets (and internal
**   conditions that have the same effect).
**
**  The "count" field is the number of commands on list.  If count is
**  identically 0 the values of firstCommand and lastCommand are undefined.
**
**  LUNS with pending commands are doubly linked into a ring.
**  nextLun and previousLun point to other LUNs on the ring.
**  (HBA_SCH contains a pointer to the LUN RING.)
**  If count is identically 0 then there are no commands and therefore
**  the LUN is NOT on the ring and therefore the
**  nextLUN and previousLun are undefined when
**  count is identically 0.
**
**  Status is used to signal whether commands on this LUN
**  can be scheduled.
**
**  maxOutstanding and currentOutstanding are used for queue depth
**  management for this LUN
**
**  Only Statistic variable is maxCount which is the max number of commands
**  ever queued by this LUN.
** ************************************************************************** */

typedef struct {
	struct list_head commandList;	/* Next command to be scheduled */
	uint16_t q_cnt; /* commandList queue count */
	LPFC_SCHED_STATUS_t status;	/*Status of this LUN's queue   */
	uint16_t maxOutstanding;	/*Queue Depth for the LUN      */
	uint16_t currentOutstanding;
} LPFC_SCHED_LUN_t;

/*    Scheduler Data for each Command   */

typedef struct {
	struct lpfc_scsi_buf *nextCommand;
} LPFC_SCHED_SCSI_BUF_t;

/* ********************** FUNCTION DECLARATIONS   **************************
**
**   The next 3 routines initialize the SCH structures. Basically
**   clear the block and set the queue depth.  No Lock is taken
**
** ********************************************************************** */

void lpfc_sched_hba_init(struct lpfcHBA *hba, uint16_t maxOutstanding);
void lpfc_sched_target_init(struct lpfcScsiTarget *target,
			   uint16_t maxOutstanding);
void lpfc_sched_lun_init(struct lpfcScsiLun *lun, uint16_t maxOutstanding);

/* **********************************************************************
**
**   Pause and Continue the scheduler
**
** ********************************************************************* */

void lpfc_sched_pause_target(struct lpfcScsiTarget *target);
void lpfc_sched_pause_hba(struct lpfcHBA *hba);
void lpfc_sched_continue_target(struct lpfcScsiTarget *target);
void lpfc_sched_continue_hba(struct lpfcHBA *hba);

/* ********************************************************************
**   Used to schedule a SCSI IO (including Task Management) IOCBs
**   to be passed to the SLI layer.  HBA -> to the HBA structure
**   command pts to the SCSI_BUF to be submitted.
** ********************************************************************** */

void lpfc_sched_service_high_priority_queue(struct lpfcHBA *pHba);

/*      
 * When a command is returned from the SLI layer, pass it back through
 * the scheduler so the queue depths can be updated.  Scheduling is NOT.
 * performed as part of this command.  This approach allows multiple 
 * completions before insertions and reduces recursion issues.
 */

void lpfc_sched_sli_done(struct lpfcHBA *hba, struct lpfcIocbq *iocbqin,
			struct lpfcIocbq *iocbqout);

/* **************************************************************************
**
**   Use this command to kick of a search of pended SCSI_BUFs to be sent to the
**   SLI layer.
**
** ****************************************************************************/

void lpfc_sched_check(struct lpfcHBA *hba);

/* **************************************************************************
**
**   Check to see if a command is currently in the scheduler and removes
**   it if it is.  Returns 0 if not found or the pointer if it was found
**
**   DEQUEUE_LUN just gets the first command on the LUN pending queue or 0
**   if there are none.  (You might want to pause the LUN before this call).
**
** ****************************************************************************/

struct lpfc_scsi_buf *lpfc_sched_dequeue(struct lpfcHBA *hba,
				       struct lpfc_scsi_buf *command);

uint32_t lpfc_sched_flush_hba(struct lpfcHBA *hba, uint8_t status,
			     uint32_t word4);
uint32_t lpfc_sched_flush_target(struct lpfcHBA *hba,
				struct lpfcScsiTarget *target, uint8_t status,
				uint32_t word4);
uint32_t lpfc_sched_flush_lun(struct lpfcHBA *hba, struct lpfcScsiLun *lun,
			     uint8_t status, uint32_t word4);
uint32_t lpfc_sched_flush_command(struct lpfcHBA *hba,
				 struct lpfc_scsi_buf *command, uint8_t status,
				 uint32_t word4);

#endif
