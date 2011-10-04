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
 * $Id: lpfc_sched.c 328 2005-05-03 15:20:43Z sf_support $
 */

#include <linux/version.h>
#include <linux/spinlock.h>


#include <linux/blk.h>
#include <scsi.h>

#include "lpfc_hw.h"
#include "lpfc_mem.h"
#include "lpfc_sli.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_fcp.h"
#include "lpfc_scsi.h"
#include "lpfc_cfgparm.h"
#include "lpfc_crtn.h"

struct lpfcHBA;

/* *********************************************************************
**
**    Forward declaration of internal routines to SCHED
**
** ******************************************************************** */

static void lpfc_sched_internal_check(lpfcHBA_t * hba);

/* ***************************************************************
**
**  Initialize HBA, TARGET and LUN SCHED structures
**  Basically clear them, set MaxQueue Depth
** and mark them ready to go
**
** **************************************************************/

void
lpfc_sched_init_hba(lpfcHBA_t * hba, uint16_t maxOutstanding)
{
	memset(&hba->hbaSched, 0, sizeof (hba->hbaSched));
	hba->hbaSched.maxOutstanding = maxOutstanding;
	hba->hbaSched.status = LPFC_SCHED_STATUS_OKAYTOSEND;
	INIT_LIST_HEAD(&hba->hbaSched.highPriorityCmdList);
	INIT_LIST_HEAD(&hba->hbaSched.targetRing);
}

void
lpfc_sched_target_init(LPFCSCSITARGET_t * target, uint16_t maxOutstanding)
{
	memset(&target->targetSched, 0, sizeof (target->targetSched));
	target->targetSched.maxOutstanding = maxOutstanding;
	target->targetSched.status = LPFC_SCHED_STATUS_OKAYTOSEND;
	/* initlize the queue */
	INIT_LIST_HEAD(&target->listentry);
	INIT_LIST_HEAD(&target->targetSched.lunRing);
	return;
}

void
lpfc_sched_lun_init(LPFCSCSILUN_t * lun, uint16_t maxOutstanding)
{
	memset(&lun->lunSched, 0, sizeof (lun->lunSched));
	lun->lunSched.maxOutstanding = maxOutstanding;
	lun->fcp_lun_queue_depth = lun->lunSched.maxOutstanding;
	lun->lunSched.status = LPFC_SCHED_STATUS_OKAYTOSEND;
	/* initialize list */
	INIT_LIST_HEAD(&lun->lunSched.commandList);
	INIT_LIST_HEAD(&lun->listentry);

	return;
}

void
lpfc_sched_pause_target(LPFCSCSITARGET_t * target)
{
	target->targetSched.status = LPFC_SCHED_STATUS_PAUSED;

	return;
}

void
lpfc_sched_pause_hba(lpfcHBA_t * hba)
{
	hba->hbaSched.status = LPFC_SCHED_STATUS_PAUSED;

	return;
}

void
lpfc_sched_continue_target(LPFCSCSITARGET_t * target)
{
	target->targetSched.status = LPFC_SCHED_STATUS_OKAYTOSEND;
	/* Make target the next LPFCSCSITARGET_t to process */
	lpfc_sched_internal_check(target->pHba);
	return;
}

void
lpfc_sched_continue_hba(lpfcHBA_t * hba)
{
	hba->hbaSched.status = LPFC_SCHED_STATUS_OKAYTOSEND;
	lpfc_sched_internal_check(hba);
	return;
}

void
lpfc_sched_sli_done(lpfcHBA_t * pHba,
		    LPFC_IOCBQ_t * pIocbIn, LPFC_IOCBQ_t * pIocbOut)
{
	LPFC_SCSI_BUF_t *pCommand = (LPFC_SCSI_BUF_t *) pIocbIn->context1;
	LPFCSCSILUN_t *plun = pCommand->pLun;
	static int doNotCheck = 0;
	lpfcCfgParam_t *clp;
	FCP_RSP *fcprsp;

	plun->lunSched.currentOutstanding--;
	plun->pTarget->targetSched.currentOutstanding--;

	pCommand->result = pIocbOut->iocb.un.ulpWord[4];
	if ((pCommand->status = pIocbOut->iocb.ulpStatus) ==
	    IOSTAT_LOCAL_REJECT) {
		if(pCommand->result & IOERR_DRVR_MASK) {
			pCommand->status = IOSTAT_DRIVER_REJECT;
		}
	}
	pCommand->IOxri = pIocbOut->iocb.ulpContext;
	if (pCommand->status) {
		plun->errorcnt++;
	}
	plun->iodonecnt++;

	pHba->hbaSched.currentOutstanding--;

	fcprsp = pCommand->fcp_rsp;
	if ((pCommand->status == IOSTAT_FCP_RSP_ERROR) &&
	    (fcprsp->rspStatus3 == SCSI_STAT_QUE_FULL)) {

		/* Scheduler received Queue Full status from FCP device (tgt>
		   <lun> */
		lpfc_printf_log(pHba->brd_no, &lpfc_msgBlk0738,
				lpfc_mes0738,
				lpfc_msgBlk0738.msgPreambleStr,
				pCommand->scsi_target, pCommand->scsi_lun,
				pCommand->qfull_retry_count,
				plun->qfull_retries,
				plun->lunSched.currentOutstanding,
				plun->lunSched.maxOutstanding);

		if (((plun->qfull_retries > 0) &&
		     (pCommand->qfull_retry_count < plun->qfull_retries)) || 
		    (plun->lunSched.currentOutstanding + plun->lunSched.q_cnt == 0)){
			clp = &pHba->config[0];
			if (clp[LPFC_CFG_DQFULL_THROTTLE_UP_TIME].a_current) {
				lpfc_scsi_lower_lun_qthrottle(pHba, pCommand);
			}
			lpfc_sched_queue_command(pHba, pCommand);
			plun->qcmdcnt++;
			pCommand->qfull_retry_count++;
			goto skipcmpl;
		}
	}

	(pCommand->cmd_cmpl) (pHba, pCommand);

 skipcmpl:

	if (!doNotCheck) {
		doNotCheck = 1;
		lpfc_sched_internal_check(pHba);
		doNotCheck = 0;
	}
	return;
}

void
lpfc_sched_check(lpfcHBA_t * hba)
{
	lpfc_sched_internal_check(hba);

	return;
}


static void    
lpfc_sched_internal_check(lpfcHBA_t  *hba) 
{
	LPFC_SCHED_HBA_t  * hbaSched           = &hba->hbaSched;
	LPFC_SLI_t        * psli;
	LPFC_NODELIST_t   * ndlp;
	int                numberOfFailedTargetChecks = 0;
	int                didSuccessSubmit   = 0;    /* SLI optimization for Port signals */
	int                stopSched          = 0;    /* used if SLI rejects on interloop */

	/* get the elx_sli struct from phba */
	psli = &hba->sli;
   
	/* Service the High Priority Queue first */
	if (hba->hbaSched.q_cnt)
		lpfc_sched_service_high_priority_queue(hba);
   
	/* If targetCount is identically 0 then there are no Targets on the ring therefore
	   no pending commands on any LUN           
	*/
	if ( (hbaSched->targetCount == 0) ||
	     (hbaSched->status == LPFC_SCHED_STATUS_PAUSED) )   
		return;
   
	/* We are going to cycle through the Targets
	   on a round robin basis until we make a pass through
	   with nothing to schedule. 
	*/

	while ( (stopSched == 0)                                            &&
		(hbaSched->currentOutstanding < hbaSched->maxOutstanding) &&
		(numberOfFailedTargetChecks < hbaSched->targetCount) ) {
		LPFCSCSITARGET_t      *target      = hbaSched->nextTargetToCheck;
		LPFC_SCHED_TARGET_t   *targetSched = &target->targetSched;
		LPFCSCSITARGET_t      *newNext     = list_entry(target->listentry.next,
								LPFCSCSITARGET_t,
								listentry);
		int                   numberOfFailedLunChecks = 0;
      
		if (target->listentry.next == &hbaSched->targetRing) {
			newNext = list_entry( hbaSched->targetRing.next,
					      LPFCSCSITARGET_t,
					      listentry);
		}

		if (( targetSched->currentOutstanding  < targetSched->maxOutstanding) &&
		    ( targetSched->status != LPFC_SCHED_STATUS_PAUSED)) {
			while ( numberOfFailedLunChecks < targetSched->lunCount ) {
				LPFCSCSILUN_t      *lun        = target->targetSched.nextLunToCheck;
				LPFC_SCHED_LUN_t   *lunSched   = &lun->lunSched;
				LPFCSCSILUN_t      *newNextLun = list_entry(lun->listentry.next,
									    LPFCSCSILUN_t,
									    listentry);
			   
				if ( lun->listentry.next == &target->targetSched.lunRing ) {
					newNextLun = list_entry(target->targetSched.lunRing.next,
								LPFCSCSILUN_t,
								listentry);
				}

				if (( lunSched->currentOutstanding < lunSched->maxOutstanding ) &&
				    ( !(list_empty(&lunSched->commandList))) &&
				    ( lunSched->status != LPFC_SCHED_STATUS_PAUSED)) {
					LPFC_SCSI_BUF_t   *command;
					int               sliStatus;
					LPFC_IOCBQ_t      *pIocbq;
					struct list_head *head;

					head = lunSched->commandList.next;
					command = list_entry(head,
							     LPFC_SCSI_BUF_t,
							     listentry);
					list_del(head);
					--lunSched->q_cnt;
				   
					if (!command) {
						numberOfFailedLunChecks++;
						targetSched->nextLunToCheck  = newNextLun;
						continue;
					}
				   
					ndlp = command->pLun->pnode;
					if(ndlp == 0) {
						numberOfFailedLunChecks++;
						lpfc_sched_queue_command(hba,command);
						targetSched->nextLunToCheck  = newNextLun;
						continue;
					}
	       
					pIocbq = &command->cur_iocbq;
				   
					pIocbq->context1  = command;
					pIocbq->iocb_cmpl = lpfc_sched_sli_done;
				   
					/* put the RPI number and NODELIST info in the IOCB command */
					pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
					if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
						pIocbq->iocb.ulpFCP2Rcvy = 1;
					}
					pIocbq->iocb.ulpClass = (ndlp->nlp_fcp_info & 0x0f);
				   
					/* Get an iotag and finish setup of IOCB  */
					pIocbq->iocb.ulpIoTag = lpfc_sli_next_iotag( hba,
										     &psli->ring[ psli->fcp_ring] );
					if(pIocbq->iocb.ulpIoTag == 0) {
						stopSched = 1;
						list_add(&command->listentry,
							 &lunSched->commandList);
						++lunSched->q_cnt;
						break;
					}
				   
					sliStatus = lpfc_sli_issue_iocb(hba,
									&psli->ring[ psli->fcp_ring],
									pIocbq, SLI_IOCB_RET_IOCB);
	       
				   
					switch (sliStatus) {
					case  IOCB_ERROR:   
					case  IOCB_BUSY: 
						stopSched = 1;
						list_add(&command->listentry,
							 &lunSched->commandList);
						++lunSched->q_cnt;
						break;
		     
					case  IOCB_SUCCESS:
						didSuccessSubmit = 1;
						lunSched->currentOutstanding++;
						targetSched->currentOutstanding++;
						hbaSched->currentOutstanding++;
						targetSched->nextLunToCheck = newNextLun;
						break;
		     
					default:
						break;
					}      /* End of Switch */				   

					/* 
					 * Check if there is any pending command on the lun. If not 
					 * remove the lun. If this is the last lun in the target, the
					 * target also will get removed from the scheduler ring.
					 */
					if (list_empty(&lunSched->commandList))		   
						lpfc_sched_remove_lun_from_ring(hba,lun);

					/* Either we shipped or SLI refused the operation. In any chase
					 * the driver is done with this LUN/Target!. 
					 */
					break;

					/* This brace ends LUN window open */
				}   
				else {
					numberOfFailedLunChecks++;
					targetSched->nextLunToCheck = newNextLun;
				}
				/* This brace ends While looping through LUNs on a Target */
			}
	 
			if ( numberOfFailedLunChecks >= targetSched->lunCount )  
				numberOfFailedTargetChecks++;
			else 
				numberOfFailedTargetChecks = 0;
		}   /* if Target isn't pended */
		else 
			numberOfFailedTargetChecks++;
      
		hbaSched->nextTargetToCheck = newNext;
	}   /* While looping through Targets on HBA */
  
	return;
}


void
lpfc_sched_service_high_priority_queue(struct lpfcHBA *hba)
{
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;

	LPFC_IOCBQ_t *pIocbq;
	LPFC_SCSI_BUF_t *command;
	int sliStatus;
	struct list_head *pos, *pos_tmp;

	psli = &hba->sli;

	/* 
	 * Iterate through highprioritycmdlist if any cmds waiting on it
	 * dequeue first cmd from highPriorityCmdList
	 * 
	 */
	list_for_each_safe(pos, pos_tmp, &hba->hbaSched.highPriorityCmdList) {
		command =
			list_entry(pos,
				   LPFC_SCSI_BUF_t,
				   listentry);
		list_del(pos);
		--hba->hbaSched.q_cnt;

		if (!command) {
			continue;
		}

		if ((command->pLun) && (command->pLun->pnode)) {

			ndlp = command->pLun->pnode;
			if (ndlp == 0) {

			} else {
				/* put the RPI number and NODELIST info in the
				   IOCB command */
				pIocbq = &command->cur_iocbq;
				pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
				if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
					pIocbq->iocb.ulpFCP2Rcvy = 1;
				}
				pIocbq->iocb.ulpClass =
					(ndlp->nlp_fcp_info & 0x0f);
			}
		}

		pIocbq = &command->cur_iocbq;
		/*  Current assumption is let SLI queue it until it busy us */

		pIocbq->context1 = command;

		/* Fill in iocb completion callback  */
		pIocbq->iocb_cmpl = lpfc_sli_wake_iocb_high_priority;

		/* Fill in iotag if we don't have one yet */
		if (pIocbq->iocb.ulpIoTag == 0) {
			pIocbq->iocb.ulpIoTag =
				lpfc_sli_next_iotag(hba,
						    &psli->ring[psli->fcp_ring]);
		}

		sliStatus = lpfc_sli_issue_iocb(hba,
						&psli->ring[psli->fcp_ring],
						pIocbq,
						SLI_IOCB_HIGH_PRIORITY |
						SLI_IOCB_RET_IOCB);

		switch (sliStatus) {
		case IOCB_ERROR:
		case IOCB_BUSY:
			/* We'll put it back to the head of the q and try
			   again */
			list_add(&command->listentry,
				 &hba->hbaSched.highPriorityCmdList);
			++hba->hbaSched.q_cnt;
			break;

		case IOCB_SUCCESS:
			hba->hbaSched.currentOutstanding++;
			break;

		default:

			break;
		}

		break;
	}

	return;
}

LPFC_SCSI_BUF_t *
lpfc_sched_dequeue(lpfcHBA_t * hba, LPFC_SCSI_BUF_t * ourCommand)
{
	LPFC_SCSI_BUF_t *currentCommand=0;
	LPFC_SCHED_LUN_t *pLunSched;
	struct list_head *cur, *next;

	pLunSched = &ourCommand->pLun->lunSched;
	list_for_each_safe (cur, next, &pLunSched->commandList) {
		currentCommand = list_entry(cur, LPFC_SCSI_BUF_t, listentry);

		if (currentCommand == ourCommand) {	/* found it */
			/* remove this entry from queue */
			list_del(cur);

			--pLunSched->q_cnt;

			if (!pLunSched->q_cnt){
				/* queue is empty */
				lpfc_sched_remove_lun_from_ring(hba,
								ourCommand->pLun);
			}
			break;
		}
	}

	return (currentCommand);;
}

uint32_t
lpfc_sched_flush_command(lpfcHBA_t * pHba,
			 LPFC_SCSI_BUF_t * command,
			 uint8_t iocbStatus, uint32_t word4)
{
	LPFC_SCSI_BUF_t *foundCommand = lpfc_sched_dequeue(pHba, command);
	uint32_t found = 0;

	if (foundCommand) {
		IOCB_t *pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
		found++;
		pIOCB->ulpStatus = iocbStatus;
		foundCommand->status = iocbStatus;
		if (word4) {
			pIOCB->un.ulpWord[4] = word4;
			foundCommand->result = word4;
		}

		if (foundCommand->status) {
			foundCommand->pLun->errorcnt++;
		}
		foundCommand->pLun->iodonecnt++;

		(command->cmd_cmpl) (pHba, command);
	} else {
		/* if we couldn't find this command is not in the scheduler,
		   look for it in the SLI layer */
		if (lpfc_sli_abort_iocb_context1
		    (pHba, &pHba->sli.ring[pHba->sli.fcp_ring], command) == 0) {
			found++;
		}
	}

	return found;
}

uint32_t
lpfc_sched_flush_lun(lpfcHBA_t * pHba,
		     LPFCSCSILUN_t * lun, uint8_t iocbStatus, uint32_t word4)
{
	struct list_head *cur, *next;

	int numberFlushed = 0;

	list_for_each_safe(cur, next, &lun->lunSched.commandList) {
		IOCB_t *pIOCB;
		LPFC_SCSI_BUF_t *command =
			list_entry(cur,
				   LPFC_SCSI_BUF_t,
				   listentry);
		list_del(cur);
		--lun->lunSched.q_cnt;

		pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
		pIOCB->ulpStatus = iocbStatus;
		command->status = iocbStatus;
		if (word4) {
			pIOCB->un.ulpWord[4] = word4;
			command->result = word4;
		}

		if (command->status) {
			lun->errorcnt++;
		}
		lun->iodonecnt++;

		(command->cmd_cmpl) (pHba, command);

		numberFlushed++;
	}
	lpfc_sched_remove_lun_from_ring(pHba, lun);

	/* flush the SLI layer also */
	lpfc_sli_abort_iocb_lun(pHba, &pHba->sli.ring[pHba->sli.fcp_ring],
				lun->pTarget->scsi_id, lun->lun_id);
	return (numberFlushed);
}

uint32_t
lpfc_sched_flush_target(lpfcHBA_t * pHba,
			LPFCSCSITARGET_t * target,
			uint8_t iocbStatus, uint32_t word4)
{
	LPFCSCSILUN_t *lun;
	int numberFlushed = 0;
	struct list_head *cur_h, *next_h;
	struct list_head *cur_l, *next_l;

	if (target->rptlunfunc.function) {
		lpfc_stop_timer((struct clk_data *) target->rptlunfunc.data);
		target->targetFlags &= ~FC_RETRY_RPTLUN;
	}

	/* walk the list of LUNs on this target and flush each LUN.  We
	   accomplish this by pulling the first LUN off the head of the
	   queue until there aren't any LUNs left */
	list_for_each_safe(cur_h, next_h, &target->targetSched.lunRing) {
		lun = list_entry(cur_h,
				 LPFCSCSILUN_t,
				 listentry);
		
		list_for_each_safe(cur_l, next_l, &lun->lunSched.commandList) {
			IOCB_t *pIOCB;
			LPFC_SCSI_BUF_t *command =
				list_entry(cur_l,
					   LPFC_SCSI_BUF_t,
					   listentry);
			list_del(cur_l);
			--lun->lunSched.q_cnt;

			pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
			pIOCB->ulpStatus = iocbStatus;
			command->status = iocbStatus;
			if (word4) {
				pIOCB->un.ulpWord[4] = word4;
				command->result = word4;
			}

			if (command->status) {
				lun->errorcnt++;
			}
			lun->iodonecnt++;

			(command->cmd_cmpl) (pHba, command);

			numberFlushed++;
		}

		lpfc_sched_remove_lun_from_ring(pHba, lun);
	}
	lpfc_sched_remove_target_from_ring(pHba, target);

	/* flush the SLI layer also */
	lpfc_sli_abort_iocb_tgt(pHba, &pHba->sli.ring[pHba->sli.fcp_ring],
				target->scsi_id);
	return (numberFlushed);
}

uint32_t
lpfc_sched_flush_hba(lpfcHBA_t * pHba, uint8_t iocbStatus, uint32_t word4)
{
	int numberFlushed = 0;
	LPFCSCSITARGET_t *target;
	LPFCSCSILUN_t *lun;
	struct list_head *cur_h, *next_h;
	struct list_head *cur_l, *next_l;
	struct list_head *cur_c, *next_c;

	list_for_each_safe(cur_h, next_h, &pHba->hbaSched.targetRing) {
		target = list_entry(cur_h,
				    LPFCSCSITARGET_t,
				    listentry);
		list_for_each_safe(cur_l, next_l, &target->targetSched.lunRing)
			{
				lun = list_entry(cur_l, LPFCSCSILUN_t, listentry);
				list_for_each_safe(cur_c, next_c,
						   &lun->lunSched.commandList) {
					IOCB_t *pIOCB;
					LPFC_SCSI_BUF_t *command =
						list_entry(cur_c,
							   LPFC_SCSI_BUF_t,
							   listentry);
					list_del(cur_c);
					--lun->lunSched.q_cnt;

					pIOCB = (IOCB_t *) & (command->cur_iocbq.iocb);
					pIOCB->ulpStatus = iocbStatus;
					command->status = iocbStatus;
					if (word4) {
						pIOCB->un.ulpWord[4] = word4;
						command->result = word4;
					}

					if (command->status) {
						lun->errorcnt++;
					}
					lun->iodonecnt++;

					(command->cmd_cmpl) (pHba, command);

					numberFlushed++;
				}

				lpfc_sched_remove_lun_from_ring(pHba, lun);
			}
		lpfc_sched_remove_target_from_ring(pHba, target);
	}

	/* flush the SLI layer also */
	lpfc_sli_abort_iocb_hba(pHba, &pHba->sli.ring[pHba->sli.fcp_ring]);
	return (numberFlushed);
}

int
lpfc_sched_submit_command(lpfcHBA_t * hba, LPFC_SCSI_BUF_t * command)
{
	LPFC_NODELIST_t *ndlp;
	uint16_t okayToSchedule = 1;

	/* If we have a command see if we can cut through */
	if (command != 0) {

		/* Just some short cuts */
		LPFC_SCHED_HBA_t *hbaSched = &hba->hbaSched;
		LPFC_SCHED_LUN_t *lunSched = &command->pLun->lunSched;
		LPFC_SCHED_TARGET_t *targetSched =
			&command->pLun->pTarget->targetSched;
		LPFC_IOCBQ_t *pIocbq = &command->cur_iocbq;
		LPFC_SLI_t *psli = &hba->sli;

		/*    Set it up so SLI calls us when it is done       */

		ndlp = command->pLun->pnode;
		if (ndlp == 0) {
			if(!(command->pLun->pTarget->targetFlags &
				FC_NPR_ACTIVE)) {
				return (1);
			}
			/* To be filled in later */
			pIocbq->iocb.ulpContext = 0;
			pIocbq->iocb.ulpFCP2Rcvy = 0;
			pIocbq->iocb.ulpClass = CLASS3;
		}
		else {
			/* put RPI number and NODELIST info in IOCB command */
			pIocbq->iocb.ulpContext = ndlp->nlp_rpi;
			if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
				pIocbq->iocb.ulpFCP2Rcvy = 1;
			}
			pIocbq->iocb.ulpClass = (ndlp->nlp_fcp_info & 0x0f);
		}

		pIocbq->context1 = command;
		pIocbq->iocb_cmpl = lpfc_sched_sli_done;

		/* Get an iotag and finish setup of IOCB  */
		pIocbq->iocb.ulpIoTag = lpfc_sli_next_iotag(hba,
							    &psli->ring[psli->fcp_ring]);

		if ((pIocbq->iocb.ulpIoTag != 0) && ndlp &&
		    (hbaSched->currentOutstanding < hbaSched->maxOutstanding) &&
		    (hbaSched->status & LPFC_SCHED_STATUS_OKAYTOSEND) &&
		    (targetSched->lunCount == 0) &&
		    (targetSched->currentOutstanding <
		     targetSched->maxOutstanding)
		    && (targetSched->status & LPFC_SCHED_STATUS_OKAYTOSEND)
		    && (lunSched->currentOutstanding < lunSched->maxOutstanding)
		    && (lunSched->status & LPFC_SCHED_STATUS_OKAYTOSEND)
		    ) {

			/* The scheduler, target and lun are all in a position
			 * to accept a send operation.  Call the SLI layer and
			 * issue the IOCB.
			 */

			int sliStatus;

			
			sliStatus =
				lpfc_sli_issue_iocb(hba,
						    &psli->ring[psli->fcp_ring],
						    pIocbq, SLI_IOCB_RET_IOCB);

			switch (sliStatus) {
			case IOCB_ERROR:
			case IOCB_BUSY:
				okayToSchedule = 0;
				lpfc_sched_queue_command(hba, command);
				break;
			case IOCB_SUCCESS:
				lunSched->currentOutstanding++;
				targetSched->currentOutstanding++;
				hbaSched->currentOutstanding++;
				break;
			default:

				break;
			}

			/* Remove this state to cause a scan of queues if submit
			   worked. */
			okayToSchedule = 0;
		} else {
			/* This clause is execute only if there are outstanding
			 * commands in the scheduler.
			 */
			lpfc_sched_queue_command(hba, command);
		}
	}

	/* if(command) */
	/* We either queued something or someone called us to schedule
	   so now go schedule. */
	if (okayToSchedule)
		lpfc_sched_internal_check(hba);
	return (0);
}

void
lpfc_sched_queue_command(lpfcHBA_t * hba, LPFC_SCSI_BUF_t * command)
{
	LPFCSCSILUN_t *lun = command->pLun;
	LPFC_SCHED_LUN_t *lunSched = &lun->lunSched;
	struct list_head *head;
	
	head = (struct list_head *)&lunSched->commandList;
	
	list_add_tail(&command->listentry,
		      head);
	lunSched->q_cnt++;

	lpfc_sched_add_lun_to_ring(hba, lun);

	return;
}

void
lpfc_sched_add_target_to_ring(lpfcHBA_t * hba, LPFCSCSITARGET_t * target)
{
	LPFC_SCHED_TARGET_t *targetSched = &target->targetSched;
	LPFC_SCHED_HBA_t *hbaSched = &hba->hbaSched;

	if (!list_empty(&target->listentry) ||	/* Already on list */
	    (targetSched->lunCount == 0))	/* nothing to schedule */
		return;

	list_add_tail(&target->listentry, &hbaSched->targetRing);
	if ( hbaSched->targetCount == 0 ) {
		hbaSched->nextTargetToCheck = target;
	}
	hbaSched->targetCount++;
	return;
}

void
lpfc_sched_add_lun_to_ring(lpfcHBA_t * hba, LPFCSCSILUN_t * lun)
{
	LPFC_SCHED_LUN_t *lunSched = &lun->lunSched;
	LPFCSCSITARGET_t *target = lun->pTarget;
	LPFC_SCHED_TARGET_t *targetSched = &target->targetSched;

	if (!list_empty(&lun->listentry) ||	/* Already on list */
	    (lunSched->q_cnt == 0))	/* nothing to schedule */
		return;

	list_add_tail(&lun->listentry, &targetSched->lunRing);
	if ( targetSched->lunCount == 0 ) {
		targetSched->nextLunToCheck = lun;
	}

	targetSched->lunCount++;
	lpfc_sched_add_target_to_ring(hba, target);
	return;
}

void
lpfc_sched_remove_target_from_ring(lpfcHBA_t * hba, LPFCSCSITARGET_t * target)
{

	LPFC_SCHED_HBA_t *hbaSched = &hba->hbaSched;

	if (list_empty(&target->listentry))
		return;		/* Not on Ring */
	hbaSched->targetCount--;

	if ( hbaSched->targetCount ) {
		if ( hbaSched->nextTargetToCheck == target ) {
			if (target->listentry.next == &hbaSched->targetRing) {
				hbaSched->nextTargetToCheck  = list_entry( hbaSched->targetRing.next,
									   LPFCSCSITARGET_t,
									   listentry);
			} else {
				hbaSched->nextTargetToCheck = list_entry ( target->listentry.next,
									   LPFCSCSITARGET_t,
									   listentry);
			}
		}
	} else {
		hbaSched->nextTargetToCheck = 0;
	}

	list_del_init(&target->listentry);
	return;
}

void
lpfc_sched_remove_lun_from_ring(lpfcHBA_t * hba, LPFCSCSILUN_t * lun)
{
	LPFCSCSITARGET_t *target = lun->pTarget;
	LPFC_SCHED_TARGET_t *targetSched = &target->targetSched;

	if (list_empty(&lun->listentry))
		return;		/* Not on Ring  */

	targetSched->lunCount--;

	if ( targetSched->lunCount ) {     /*  Delink the LUN from the Ring */
		
		if ( targetSched->nextLunToCheck == lun ) {

			if ( lun->listentry.next == &target->targetSched.lunRing ) {
				targetSched->nextLunToCheck = 
					list_entry(target->targetSched.lunRing.next,
						   LPFCSCSILUN_t,
						   listentry);
			} else {
				targetSched->nextLunToCheck = 
					list_entry(lun->listentry.next,
						   LPFCSCSILUN_t,
						   listentry);

			}

		}
	} else
		targetSched->nextLunToCheck = 0; /*   Ring is empty */
	
	list_del_init(&lun->listentry);

	if (!targetSched->lunCount)
		lpfc_sched_remove_target_from_ring(hba, target);

	return;
}
