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
 * $Id: lpfc_nportdisc.c 502 2006-04-04 17:11:23Z sf_support $
 */

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/pci.h>


#include <linux/blk.h>
#include <scsi.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc_cfgparm.h"
#include "lpfc_hw.h"
#include "lpfc_crtn.h"

/* This next section defines the NPort Discovery State Machine */

/* There are 4 different double linked lists nodelist entries can reside on.
 * The plogi list and adisc list are used when Link Up discovery or RSCN 
 * processing is needed. Each list holds the nodes that we will send PLOGI
 * or ADISC on. These lists will keep track of what nodes will be effected
 * by an RSCN, or a Link Up (Typically, all nodes are effected on Link Up).
 * The unmapped_list will contain all nodes that we have successfully logged
 * into at the Fibre Channel level. The mapped_list will contain all nodes
 * that are mapped FCP targets.
 */
/*
 * The bind list is a list of undiscovered (potentially non-existent) nodes
 * that we have saved binding information on. This information is used when
 * nodes transition from the unmapped to the mapped list.
 */
/* For UNUSED_NODE state, the node has just been allocated .
 * For PLOGI_ISSUE and REG_LOGIN_ISSUE, the node is on
 * the PLOGI list. For REG_LOGIN_COMPL, the node is taken off the PLOGI list
 * and put on the unmapped list. For ADISC processing, the node is taken off 
 * the ADISC list and placed on either the mapped or unmapped list (depending
 * on its previous state). Once on the unmapped list, a PRLI is issued and the
 * state changed to PRLI_ISSUE. When the PRLI completion occurs, the state is
 * changed to PRLI_COMPL. If the completion indicates a mapped
 * node, the node is taken off the unmapped list. The binding list is checked
 * for a valid binding, or a binding is automatically assigned. If binding
 * assignment is unsuccessful, the node is left on the unmapped list. If
 * binding assignment is successful, the associated binding list entry (if
 * any) is removed, and the node is placed on the mapped list. 
 */
/*
 * For a Link Down, all nodes on the ADISC, PLOGI, unmapped or mapped
 * lists will receive a DEVICE_UNK event. If the linkdown or nodev timers
 * expire, all effected nodes will receive a DEVICE_RM event.
 */
/*
 * For a Link Up or RSCN, all nodes will move from the mapped / unmapped lists
 * to either the ADISC or PLOGI list.  After a Nameserver query or ALPA loopmap
 * check, additional nodes may be added (DEVICE_ADD) or removed (DEVICE_RM) to /
 * from the PLOGI or ADISC lists. Once the PLOGI and ADISC lists are populated,
 * we will first process the ADISC list.  32 entries are processed initially and
 * ADISC is initited for each one.  Completions / Events for each node are
 * funnelled thru the state machine.  As each node finishes ADISC processing, it
 * starts ADISC for any nodes waiting for ADISC processing. If no nodes are
 * waiting, and the ADISC list count is identically 0, then we are done. For
 * Link Up discovery, since all nodes on the PLOGI list are UNREG_LOGIN'ed, we
 * can issue a CLEAR_LA and reenable Link Events. Next we will process the PLOGI
 * list.  32 entries are processed initially and PLOGI is initited for each one.
 * Completions / Events for each node are funnelled thru the state machine.  As
 * each node finishes PLOGI processing, it starts PLOGI for any nodes waiting
 * for PLOGI processing. If no nodes are waiting, and the PLOGI list count is
 * indentically 0, then we are done. We have now completed discovery / RSCN
 * handling. Upon completion, ALL nodes should be on either the mapped or
 * unmapped lists.
 */

void *lpfc_disc_action[NLP_STE_MAX_STATE * NLP_EVT_MAX_EVENT] = {
	/* Action routine                          Event       Current State  */
	(void *)lpfc_rcv_plogi_unused_node,	/* RCV_PLOGI   UNUSED_NODE    */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_unused_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_unused_node,	/* RCV_PRLO        */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_els_unused_node,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_unused_node,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_unused_node,	/* DEVICE_RM       */
	(void *)lpfc_device_add_unused_node,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_unused_node,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_plogi_issue,	/* RCV_PLOGI   PLOGI_ISSUE    */
	(void *)lpfc_rcv_prli_plogi_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_plogi_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_els_plogi_issue,	/* RCV_PRLO        */
	(void *)lpfc_cmpl_plogi_plogi_issue,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_prli_plogi_issue,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_plogi_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_plogi_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_plogi_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_plogi_issue,	/* DEVICE_RM       */
	(void *)lpfc_disc_nodev,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_plogi_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_reglogin_issue,	/* RCV_PLOGI  REG_LOGIN_ISSUE */
	(void *)lpfc_rcv_prli_reglogin_issue,	/* RCV_PLOGI       */
	(void *)lpfc_rcv_logo_reglogin_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_reglogin_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_reglogin_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_reglogin_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_reglogin_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_reglogin_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_reglogin_issue,	/* DEVICE_RM       */
	(void *)lpfc_disc_nodev,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_reglogin_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_prli_issue,	/* RCV_PLOGI   PRLI_ISSUE     */
	(void *)lpfc_rcv_prli_prli_issue,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_prli_issue,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_prli_issue,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_prli_issue,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_cmpl_prli_prli_issue,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_prli_issue,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_prli_issue,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_prli_issue,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_prli_issue,	/* DEVICE_RM       */
	(void *)lpfc_device_add_prli_issue,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_prli_issue,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_prli_compl,	/* RCV_PLOGI   PRLI_COMPL     */
	(void *)lpfc_rcv_prli_prli_compl,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_prli_compl,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_prli_compl,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_prli_compl,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_prli_compl,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_prli_compl,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_prli_compl,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_prli_compl,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_prli_compl,	/* DEVICE_RM       */
	(void *)lpfc_device_add_prli_compl,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_prli_compl,	/* DEVICE_UNK      */
	(void *)lpfc_rcv_plogi_mapped_node,	/* RCV_PLOGI   MAPPED_NODE    */
	(void *)lpfc_rcv_prli_mapped_node,	/* RCV_PRLI        */
	(void *)lpfc_rcv_logo_mapped_node,	/* RCV_LOGO        */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_ADISC       */
	(void *)lpfc_rcv_padisc_mapped_node,	/* RCV_PDISC       */
	(void *)lpfc_rcv_prlo_mapped_node,	/* RCV_PRLO        */
	(void *)lpfc_disc_neverdev,	/* CMPL_PLOGI      */
	(void *)lpfc_disc_neverdev,	/* CMPL_PRLI       */
	(void *)lpfc_cmpl_logo_mapped_node,	/* CMPL_LOGO       */
	(void *)lpfc_cmpl_adisc_mapped_node,	/* CMPL_ADISC      */
	(void *)lpfc_cmpl_reglogin_mapped_node,	/* CMPL_REG_LOGIN  */
	(void *)lpfc_device_rm_mapped_node,	/* DEVICE_RM       */
	(void *)lpfc_device_add_mapped_node,	/* DEVICE_ADD      */
	(void *)lpfc_device_unk_mapped_node,	/* DEVICE_UNK      */
};

extern uint8_t lpfcAlpaArray[];

int lpfc_check_adisc(lpfcHBA_t *, LPFC_NODELIST_t *, NAME_TYPE *, NAME_TYPE *);
int lpfc_geportname(NAME_TYPE *, NAME_TYPE *);
LPFC_BINDLIST_t *lpfc_assign_scsid(lpfcHBA_t *, LPFC_NODELIST_t *, int);

int
lpfc_disc_state_machine(lpfcHBA_t * phba,
			LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	uint32_t cur_state, rc;
	uint32_t(*func) (lpfcHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);

	ndlp->nlp_disc_refcnt++;
	cur_state = ndlp->nlp_state;

	/* DSM in event <evt> on NPort <nlp_DID> in state <cur_state> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0211,
		       lpfc_mes0211,
		       lpfc_msgBlk0211.msgPreambleStr,
		       evt, ndlp->nlp_DID, cur_state, ndlp->nlp_flag);

	func = (uint32_t(*)(lpfcHBA_t *, LPFC_NODELIST_t *, void *, uint32_t))
	    lpfc_disc_action[(cur_state * NLP_EVT_MAX_EVENT) + evt];
	rc = (func) (phba, ndlp, arg, evt);

	/* DSM out state <rc> on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0212,
		       lpfc_mes0212,
		       lpfc_msgBlk0212.msgPreambleStr,
		       rc, ndlp->nlp_DID, ndlp->nlp_flag);

	ndlp->nlp_disc_refcnt--;

	/* Check to see if ndlp removal is deferred */
	if((ndlp->nlp_disc_refcnt == 0) && (ndlp->nlp_rflag & NLP_DELAY_REMOVE)) {
		ndlp->nlp_rflag &= ~NLP_DELAY_REMOVE;
		lpfc_nlp_remove(phba, ndlp);
		return (NLP_STE_FREED_NODE);
	}
	if (rc == NLP_STE_FREED_NODE)
		return (NLP_STE_FREED_NODE);
	ndlp->nlp_state = rc;
	return (rc);
}

uint32_t
lpfc_rcv_plogi(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, LPFC_IOCBQ_t *cmdiocb)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	SERV_PARM *sp;
	LPFC_MBOXQ_t *mbox;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
	    (sp->cls2.classValid)) {
		ndlp->nlp_fcp_info |= CLASS2;
	} else {
		ndlp->nlp_fcp_info |= CLASS3;
	}

	if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		return (ndlp->nlp_state);
	}
	if ((phba->fc_flag & FC_PT2PT)
	    && !(phba->fc_flag & FC_PT2PT_PLOGI)) {
		/* The rcv'ed PLOGI determines what our NPortId will
		   be */
		phba->fc_myDID = icmd->un.rcvels.parmRo;
		lpfc_config_link(phba, mbox);
		if (lpfc_sli_issue_mbox
		    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mbox);
			return (ndlp->nlp_state);
		}
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
			return (ndlp->nlp_state);
		}
	}
	if (lpfc_reg_login(phba, icmd->un.rcvels.remoteID,
			   (uint8_t *) sp, mbox, 0) == 0) {
		mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
		mbox->context2 = ndlp;
		ndlp->nlp_state = NLP_STE_REG_LOGIN_ISSUE;
		if (lpfc_sli_issue_mbox
		    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
		    != MBX_NOT_FINISHED) {
			lpfc_nlp_plogi(phba, ndlp);
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb,
					 ndlp, 0, 0);
			return (ndlp->nlp_state);	/* HAPPY PATH */
		}
		/* NOTE: we should have messages for unsuccessful
		   reglogin */
		lpfc_mbox_free(phba, mbox);
	} else {
		lpfc_mbox_free(phba, mbox);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_plogi_unused_node(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	lpfcCfgParam_t *clp;
	LS_RJT stat;

	clp = &phba->config[0];
	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= LPFC_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Before responding to PLOGI, check for pt2pt mode.
		 * If we are pt2pt, with an outstanding FLOGI, abort
		 * the FLOGI and resend it first.
		 */
		if (phba->fc_flag & FC_PT2PT) {
			lpfc_els_abort_flogi(phba);
		        if(!(phba->fc_flag & FC_PT2PT_PLOGI)) {
				/* If the other side is supposed to initiate
				 * the PLOGI anyway, just ACC it now and
				 * move on with discovery.
				 */
	    			lpfc_check_sparm(phba, ndlp, sp, CLASS3);
				phba->fc_edtov = FF_DEF_EDTOV;
				phba->fc_ratov = FF_DEF_RATOV;
				phba->fcp_timeout_offset = 2 * phba->fc_ratov +
				    clp[LPFC_CFG_EXTRA_IO_TMO].a_current;
				/* Start discovery - this should just do
				   CLEAR_LA */
				lpfc_disc_start(phba);
			}
			else {
				lpfc_initial_flogi(phba);
			}
			goto good_path;
		}
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		if (phba->hba_state <= LPFC_FLOGI) {
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		} else {
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
good_path:
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0114,
				lpfc_mes0114,
				lpfc_msgBlk0114.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				ndlp->nlp_rpi);

		ndlp->nlp_state = lpfc_rcv_plogi(phba, ndlp, cmdiocb);
		if(ndlp->nlp_state == NLP_STE_REG_LOGIN_ISSUE)
			return (ndlp->nlp_state);
	}
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_els_unused_node(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_issue_els_logo(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_unused_node(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_els_unused_node(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_reglogin_unused_node(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_rm_unused_node(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_add_unused_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	}
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	lpfc_nlp_plogi(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_unused_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_plogi_plogi_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	SERV_PARM *sp;
	LPFC_MBOXQ_t *mbox;
	lpfcCfgParam_t *clp;
	LS_RJT stat;
	int port_cmp;

	clp = &phba->config[0];
	cmdiocb = (LPFC_IOCBQ_t *) arg;
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	/* For a PLOGI, we only accept if our portname is less
	 * than the remote portname. 
	 */
	phba->fc_stat.elsLogiCol++;
	port_cmp = lpfc_geportname((NAME_TYPE *) & phba->fc_portname,
				   (NAME_TYPE *) & sp->portName);

	if (!port_cmp) {
		if (lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0) {
			/* Reject this request because invalid parameters */
			stat.un.b.lsRjtRsvd0 = 0;
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
			stat.un.b.vendorUnique = 0;
			lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb,
					    ndlp);
		} else {
			/* PLOGI chkparm OK */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0120,
					lpfc_mes0120,
					lpfc_msgBlk0120.msgPreambleStr,
					ndlp->nlp_DID, ndlp->nlp_state,
					ndlp->nlp_flag,
					((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

			if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nlp_fcp_info |= CLASS2;
			} else {
				ndlp->nlp_fcp_info |= CLASS3;
			}

			if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
				if (lpfc_reg_login
				    (phba, icmd->un.rcvels.remoteID,
				     (uint8_t *) sp, mbox, 0) == 0) {
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_reg_login;
					mbox->context2 = ndlp;
					/* Issue Reg Login after successful
					   ACC */
					ndlp->nlp_flag |= NLP_ACC_REGLOGIN;

					if (port_cmp != 2) {
						/* Abort outstanding PLOGI */
						lpfc_driver_abort(phba, ndlp);
					}
					lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI,
							 cmdiocb, ndlp, mbox,
							 0);
					return (ndlp->nlp_state);

				} else {
					lpfc_mbox_free(phba, mbox);
				}
			}
		}		/* if valid sparm */
	} /* if our portname was less */
	else {
		/* Reject this request because the remote node will accept
		   ours */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CMD_IN_PROGRESS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_plogi_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* software abort outstanding plogi, then send logout */
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
		lpfc_issue_els_logo(phba, ndlp, 0);
	} else {
		if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
			lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
			return(ndlp->nlp_state);
		}

		lpfc_driver_abort(phba, ndlp);
		lpfc_issue_els_logo(phba, ndlp, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_plogi_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	lpfcCfgParam_t *clp;
	int           issue_abort;

	clp = &phba->config[0];
	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* software abort outstanding plogi before sending acc */
	issue_abort = 0;
	if (!ndlp->nlp_tmofunc.function) {
		issue_abort = 1;
	}
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);

	/* resend plogi after 1 sec delay */
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	} 
	ndlp->nlp_flag |= NLP_DELAY_TMO;
	ndlp->nlp_retry = 0;

	lpfc_start_timer(phba, 1, &ndlp->nlp_tmofunc, lpfc_els_retry_delay, 
		(unsigned long)ndlp->nlp_DID, (unsigned long)ELS_CMD_PLOGI);
	lpfc_nlp_plogi(phba, ndlp);

	if(issue_abort) {
		lpfc_driver_abort(phba, ndlp);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_els_plogi_issue(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding plogi, then send logout */
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	} else {
		lpfc_driver_abort(phba, ndlp);
	}
	lpfc_issue_els_logo(phba, ndlp, 0);
	return (ndlp->nlp_state);
}

/*! lpfc_cmpl_plogi_plogi_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *     This routine is envoked when we rcv a PLOGI completion from a node we
  *     tried to log into. We check the CSPs, and the ulpStatus. If successful
  *     change the state to REG_LOGIN_ISSUE and issue a REG_LOGIN. For failure,
  *     we free the nodelist entry.
  */

uint32_t
lpfc_cmpl_plogi_plogi_issue(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	SERV_PARM *sp;
	LPFC_MBOXQ_t *mbox;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	cmdiocb = (LPFC_IOCBQ_t *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;

	if (ndlp->nlp_flag & NLP_ACC_REGLOGIN) {
		return (ndlp->nlp_state);
	}

	irsp = &rspiocb->iocb;

	if (irsp->ulpStatus == 0) {
		pCmd = (DMABUF_t *) cmdiocb->context2;
		
		pRsp = (DMABUF_t *) pCmd->list.next;
		lp = (uint32_t *) pRsp->virt;

		sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));
		if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
			/* PLOGI chkparm OK */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0121,
					lpfc_mes0121,
					lpfc_msgBlk0121.msgPreambleStr,
					ndlp->nlp_DID, ndlp->nlp_state,
					ndlp->nlp_flag,
					((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

			if ((clp[LPFC_CFG_FCP_CLASS].a_current == CLASS2) &&
			    (sp->cls2.classValid)) {
				ndlp->nlp_fcp_info |= CLASS2;
			} else {
				ndlp->nlp_fcp_info |= CLASS3;
			}

			if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
				if (lpfc_reg_login
				    (phba, irsp->un.elsreq64.remoteID,
				     (uint8_t *) sp, mbox, 0) == 0) {
					/* set_slim mailbox command needs to
					 * execute first, queue this command to
					 * be processed later.
					 */
					if (ndlp->nlp_DID == NameServer_DID) {
						mbox->mbox_cmpl =
						    lpfc_mbx_cmpl_ns_reg_login;
					} else if (ndlp->nlp_DID == FDMI_DID) {
						mbox->mbox_cmpl =
						   lpfc_mbx_cmpl_fdmi_reg_login;
					} else {
						mbox->mbox_cmpl =
						    lpfc_mbx_cmpl_reg_login;
					}
					mbox->context2 = ndlp;
					ndlp->nlp_state =
					    NLP_STE_REG_LOGIN_ISSUE;
					if (lpfc_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    != MBX_NOT_FINISHED) {
						return (ndlp->nlp_state);
					}
					lpfc_mbox_free(phba, mbox);
				} else {
					lpfc_mbox_free(phba, mbox);
				}
			}
		}
	}

	/* If we are in the middle of discovery,
	 * take necessary actions to finish up.
	 */
	if (ndlp->nlp_DID == NameServer_DID) {
		/* Link up / RSCN discovery */
		lpfc_disc_start(phba);
	}

	/* If this is a driver initiated abort, to a FCP target,
	 * still honor the timer to relogin after a second.
	 */
	if((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
	  (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED) &&
	  (ndlp->nlp_tmofunc.function) && (ndlp->nlp_flag & NLP_DELAY_TMO)) {
		return (ndlp->nlp_state);
	}
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;

	/* Free this node since the driver cannot login or has the wrong
	   sparm */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_prli_plogi_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{

	/* First ensure ndlp is on the plogi list */
	lpfc_nlp_plogi(phba, ndlp);

	/* If a PLOGI is not already pending, issue one */
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		ndlp->nlp_flag |= NLP_DISC_NODE;
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_plogi_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {
		lpfc_nlp_plogi(phba, ndlp);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_adisc_plogi_issue(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{

	/* First ensure ndlp is on the plogi list */
	lpfc_nlp_plogi(phba, ndlp);

	/* If a PLOGI is not already pending, issue one */
	if (!(ndlp->nlp_flag & NLP_PLOGI_SND)) {
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		ndlp->nlp_flag |= NLP_DISC_NODE;
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_plogi_issue(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (le32_to_cpu(ldata) & 0xFFFF);

	/* first unreg node's rpi */
	if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (lpfc_sli_issue_mbox(phba, mbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mbox);
		}
	}

	/* software abort outstanding plogi */
	lpfc_driver_abort(phba, ndlp);
	/* send a new plogi */
	lpfc_nlp_plogi(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_issue_els_plogi(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_plogi_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *     This routine is envoked when we a request to remove a nport we are in
  *     the process of PLOGIing. We should issue a software abort on the
  *     outstanding PLOGI request, then issue a LOGO request. Change node state
  *     to UNUSED_NODE so it can be freed when LOGO completes.
  *
  */

uint32_t
lpfc_device_rm_plogi_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{

	/* software abort outstanding plogi, before sending LOGO */
	lpfc_driver_abort(phba, ndlp);

	/* If discovery processing causes us to remove a device, it is important
	 * that nothing gets sent to the device (soft zoning issues).
	 */
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_unk_plogi_issue(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding plogi */
	lpfc_driver_abort(phba, ndlp);

	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_plogi_reglogin_issue(lpfcHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0122,
				lpfc_mes0122,
				lpfc_msgBlk0122.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_reglogin_issue(lpfcHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_reglogin_issue(lpfcHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);

	/* resend plogi */
	lpfc_nlp_plogi(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
	lpfc_issue_els_plogi(phba, ndlp, 0);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_reglogin_issue(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0123,
				lpfc_mes0123,
				lpfc_msgBlk0123.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_reglogin_issue(lpfcHBA_t * phba,
			     LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, 0, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_reglogin_issue(lpfcHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* 
	 * don't really want to do anything since reglogin has not finished,
	 * and we won't let any els happen until the mb is finished. 
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_adisc_reglogin_issue(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{

	/* First ensure ndlp is on the plogi list */
	lpfc_nlp_plogi(phba, ndlp);

	return (ndlp->nlp_state);
}

/*! lpfc_cmpl_reglogin_reglogin_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *     This routine is envoked when the REG_LOGIN completes. If unsuccessful,
  *     we should send a LOGO ELS request and free the node entry. If
  *     successful, save the RPI assigned, then issue a PRLI request. The
  *     nodelist entry should be moved to the unmapped list.  If the NPortID
  *     indicates a Fabric entity, don't issue PRLI, just go straight into
  *     PRLI_COMPL.  PRLI_COMPL - for fabric entity
  */
uint32_t
lpfc_cmpl_reglogin_reglogin_issue(lpfcHBA_t * phba,
				  LPFC_NODELIST_t * ndlp,
				  void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	LPFC_NODELIST_t *plogi_ndlp;
	uint32_t did;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	did = mb->un.varWords[1];
	if (mb->mbxStatus ||
	    ((plogi_ndlp = lpfc_findnode_did(phba,
					     (NLP_SEARCH_PLOGI |
					      NLP_SEARCH_DEQUE), did)) == 0)
	    || (ndlp != plogi_ndlp)) {
		/* RegLogin failed */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0246,
				lpfc_mes0246,
				lpfc_msgBlk0246.msgPreambleStr,
				did, mb->mbxStatus, phba->hba_state);

		lpfc_dequenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_remove(phba, ndlp);
		return (NLP_STE_FREED_NODE);
	}

	ndlp->nlp_rpi = mb->un.varWords[0];

	/* This is a brand new rpi, so there should be NO pending IOCBs queued to
	 * the SLI layer using this rpi.
	 */
	lpfc_new_rpi(phba, ndlp->nlp_rpi);
	lpfc_nlp_unmapped(phba, ndlp);

	/* Only if we are not a fabric nport do we issue PRLI */
	if (!(ndlp->nlp_type & NLP_FABRIC)) {
		lpfc_issue_els_prli(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PRLI_ISSUE;
	} else {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_reglogin_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *     This routine is envoked when we a request to remove a nport we are in
  *     the process of REG_LOGINing. We should issue a UNREG_LOGIN by did, then
  *     issue a LOGO request. Change node state to NODE_UNUSED, so it will be
  *     freed when LOGO completes.
  *
  */

uint32_t
lpfc_device_rm_reglogin_issue(lpfcHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *mbox;

	if (ndlp->nlp_rpi) {
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
			}
		}

		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;
	}

	/* If discovery processing causes us to remove a device, it is important
	 * that nothing gets sent to the device (soft zoning issues).
	 */
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

/* DEVICE_ADD for REG_LOGIN_ISSUE is nodev */

uint32_t
lpfc_device_unk_reglogin_issue(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_plogi_prli_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0)) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0124,
				lpfc_mes0124,
				lpfc_msgBlk0124.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, 0);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_prli_issue(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_prli_issue(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* Software abort outstanding prli before sending acc */
	lpfc_driver_abort(phba, ndlp);

	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
	}

	/* The driver has to wait until the ACC completes before it continues
	 * processing the LOGO.  The action will resume in
	 * lpfc_cmpl_els_logo_acc routine. Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_prli_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp,
					 0, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

/* This routine is envoked when we rcv a PRLO request from a nport
 * we are logged into.  We should send back a PRLO rsp setting the
 * appropriate bits.
 * NEXT STATE = PRLI_ISSUE
 */
uint32_t
lpfc_rcv_prlo_prli_issue(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, 0, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_prli_prli_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	PRLI *npr;
	LPFC_BINDLIST_t *blp;
	LPFCSCSILUN_t *lunp;
	LPFCSCSITARGET_t *targetp;
	struct list_head *curr, *next;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;
	irsp = &rspiocb->iocb;
	if (irsp->ulpStatus) {
		/* If a local error occured go back to plogi state and retry */
		if(((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		   (irsp->un.ulpWord[4] == IOERR_SEQUENCE_TIMEOUT)) ||
		   ((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		    (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED))) {
			lpfc_nlp_plogi(phba, ndlp);
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
		} else {
			ndlp->nlp_state = NLP_STE_PRLI_COMPL;
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
					  LPFC_CLR_BITMASK);
		}
		return (ndlp->nlp_state);
	}
	pCmd = (DMABUF_t *) cmdiocb->context2;
	
	pRsp = (DMABUF_t *) pCmd->list.next;
	lp = (uint32_t *) pRsp->virt;

	npr = (PRLI *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out PRLI rsp */
	if ((npr->acceptRspCode != PRLI_REQ_EXECUTED) ||
	    (npr->prliType != PRLI_FCP_TYPE) || (npr->targetFunc != 1)) {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
				  LPFC_CLR_BITMASK);
		return (ndlp->nlp_state);
	}
	if (npr->Retry == 1) {
		ndlp->nlp_fcp_info |= NLP_FCP_2_DEVICE;
	}

	/* Can we assign a SCSI Id to this NPort */
	if ((blp = lpfc_assign_scsid(phba, ndlp, 0))) {
		lpfc_nlp_mapped(phba, ndlp, blp);
		targetp = ndlp->nlp_Target;
		ndlp->nlp_failMask = 0;
		if (targetp) {
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->failMask = 0;
			}
		}
		else {
			/* new target to driver, allocate space to target <sid>
			   lun 0 */
			if (blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target =
				    phba->device_queue_hash[blp->nlp_sid];
			}
			targetp = blp->nlp_Target;
			ndlp->nlp_Target = targetp;
			targetp->pcontext = ndlp;
			lpfc_scsi_assign_rpi(phba, targetp, ndlp->nlp_rpi);
			targetp->un.dev_did = ndlp->nlp_DID;
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = (LPFC_NODELIST_t *) ndlp;
			}
		}
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
				  LPFC_SET_BITMASK);

		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		lpfc_disc_issue_rptlun(phba, ndlp);
	} else {
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
				  LPFC_CLR_BITMASK);
	}
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_prli_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	/* software abort outstanding prli, then send logout */
	lpfc_driver_abort(phba, ndlp);
	lpfc_issue_els_logo(phba, ndlp, 0);
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_adisc_prli_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_BINDLIST_t  *blp;
	LPFCSCSILUN_t    *lunp;
	LPFCSCSITARGET_t *targetp;
	struct list_head *curr, *next;

	/* Can we reassign a SCSI Id to this NPort */
	if ((blp = lpfc_assign_scsid(phba, ndlp, 1))) {
		lpfc_nlp_mapped(phba, ndlp, blp);
		/* NOTE: can't we have this in lpfc_nlp_mapped */
		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		targetp = ndlp->nlp_Target;
		if(!targetp) {
			/* new target to driver, allocate target <sid> lun 0 */
			if(blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target = phba->device_queue_hash[blp->nlp_sid];
			}
			targetp = blp->nlp_Target;
			ndlp->nlp_Target = targetp;
			targetp->pcontext = ndlp;
			lpfc_scsi_assign_rpi(phba, targetp, ndlp->nlp_rpi);
			targetp->un.dev_did = ndlp->nlp_DID;
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = (LPFC_NODELIST_t *) ndlp;
			}
		}
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_ALL_BITS, LPFC_CLR_BITMASK);
	}
	else {
		lpfc_nlp_unmapped(phba, ndlp);
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN, LPFC_CLR_BITMASK);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_prli_issue(lpfcHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (le32_to_cpu(ldata) & 0xFFFF);

	if (ndlp->nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
			}
		}

		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (lpfc_sli_issue_mbox(phba, mbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mbox);
		}

		/* software abort outstanding prli */
		lpfc_driver_abort(phba, ndlp);

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_prli_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    This routine is envoked when we a request to remove a nport we are in the
  *    process of PRLIing. We should software abort outstanding prli, unreg
  *    login, send a logout. We will change node state to UNUSED_NODE, put it
  *    on plogi list so it can be freed when LOGO completes. 
  *
  */
uint32_t
lpfc_device_rm_prli_issue(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{

	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	ndlp->nlp_state = NLP_STE_UNUSED_NODE;

	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_add_prli_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	}
	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

/*! lpfc_device_unk_prli_issue
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    The routine is envoked when the state of a device is unknown, like
  *    during a link down. We should remove the nodelist entry from the
  *    unmapped list, issue a UNREG_LOGIN, do a software abort of the
  *    outstanding PRLI command, then free the node entry.
  */
uint32_t
lpfc_device_unk_prli_issue(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* software abort outstanding prli */
	lpfc_driver_abort(phba, ndlp);

	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_plogi_prli_compl(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= LPFC_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		if (phba->hba_state <= LPFC_FLOGI) {
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		} else {
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0125,
				lpfc_mes0125,
				lpfc_msgBlk0125.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, 0);

		if ((phba->fc_flag & FC_PT2PT)
		    && !(phba->fc_flag & FC_PT2PT_PLOGI)) {
			ndlp->nlp_state = lpfc_rcv_plogi(phba, ndlp, cmdiocb);
		}
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_prli_compl(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_prli_compl(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* software abort outstanding adisc before sending acc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		lpfc_driver_abort(phba, ndlp);
	}
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
	}

	/* The driver has to wait until the ACC completes before we can continue
	 * processing the LOGO, the action will resume in
	 * lpfc_cmpl_els_logo_acc.  Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_prli_compl(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp,
					 0, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_prli_compl(lpfcHBA_t * phba,
			 LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, 0, 0);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_prli_compl(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		/* software abort outstanding adisc */
		lpfc_driver_abort(phba, ndlp);
	}

	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_adisc_prli_compl(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	LPFC_BINDLIST_t * blp;
	LPFCSCSILUN_t    * lunp;
	LPFCSCSITARGET_t * targetp;
	ADISC *ap;
	struct list_head * curr, * next;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;
	irsp = &rspiocb->iocb;

	/* First remove the ndlp from any list */
	lpfc_dequenode(phba, ndlp);

	if (irsp->ulpStatus) {
		lpfc_freenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}

	pCmd = (DMABUF_t *) cmdiocb->context2;
	
	pRsp = (DMABUF_t *) pCmd->list.next;
	lp = (uint32_t *) pRsp->virt;

	ap = (ADISC *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out ADISC rsp */
	if ((lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName) == 0)) {
		lpfc_freenode(phba, ndlp);
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}
	if ((blp = lpfc_assign_scsid(phba, ndlp, 1))) {
		lpfc_nlp_mapped(phba, ndlp, blp);
		/* NOTE: can't we have this in lpfc_nlp_mapped */
		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		targetp = ndlp->nlp_Target;
		if(!targetp) {
			/* new target to driver, allocate space to target <sid> lun 0 */
			if(blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target = phba->device_queue_hash[blp->nlp_sid];
			}
			targetp = blp->nlp_Target;
			ndlp->nlp_Target = targetp;
			targetp->pcontext = ndlp;
			lpfc_scsi_assign_rpi(phba, targetp, ndlp->nlp_rpi);
			targetp->un.dev_did = ndlp->nlp_DID;
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = (LPFC_NODELIST_t *) ndlp;
			}
		}
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_ALL_BITS, LPFC_CLR_BITMASK);
	}
	else {
		lpfc_nlp_unmapped(phba, ndlp);
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN, LPFC_CLR_BITMASK);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_prli_compl(lpfcHBA_t * phba,
			      LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (le32_to_cpu(ldata) & 0xFFFF);

	if (ndlp->nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
			}
		}

		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (lpfc_sli_issue_mbox(phba, mbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mbox);
		}

		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			lpfc_nlp_plogi(phba, ndlp);
			/* software abort outstanding adisc */
			lpfc_driver_abort(phba, ndlp);
		}

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_prli_compl
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    This routine is envoked when we a request to remove a nport.
  *    It could be called when linkdown or nodev timer expires.
  *    If nodev timer is still running, we just want to exit.
  *    If this node timed out, we want to abort outstanding ADISC,
  *    unreg login, send logout, change state to UNUSED_NODE and
  *    place node on plogi list so it can be freed when LOGO completes.
  *
  */
uint32_t
lpfc_device_rm_prli_compl(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
			  LPFC_SET_BITMASK);
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		/* software abort outstanding adisc */
		lpfc_driver_abort(phba, ndlp);
	}

	/* If discovery processing causes us to remove a device, it is
	 * important that nothing gets sent to the device (soft zoning
	 * issues).
	 */
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_add_prli_compl(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	}
	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_prli_compl(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_rcv_plogi_mapped_node(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	uint32_t *lp;
	SERV_PARM *sp;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

	if ((phba->hba_state <= LPFC_FLOGI) ||
	    ((lpfc_check_sparm(phba, ndlp, sp, CLASS3) == 0))) {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		if (phba->hba_state <= LPFC_FLOGI) {
			stat.un.b.lsRjtRsnCode = LSRJT_LOGICAL_BSY;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		} else {
			stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
			stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		}
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	} else {
		/* PLOGI chkparm OK */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0126,
				lpfc_mes0126,
				lpfc_msgBlk0126.msgPreambleStr,
				ndlp->nlp_DID, ndlp->nlp_state, ndlp->nlp_flag,
				((LPFC_NODELIST_t*) ndlp)->nlp_rpi);

		lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, 0);

		if ((phba->fc_flag & FC_PT2PT)
		    && !(phba->fc_flag & FC_PT2PT_PLOGI)) {
			ndlp->nlp_state = lpfc_rcv_plogi(phba, ndlp, cmdiocb);
		}
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prli_mapped_node(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	lpfc_els_rsp_prli_acc(phba, cmdiocb, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_logo_mapped_node(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* software abort outstanding adisc before sending acc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		lpfc_driver_abort(phba, ndlp);
	}
	/* Only call LOGO ACC for first LOGO, this avoids sending unnecessary
	 * PLOGIs during LOGO storms from a device.
	 */
	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
		ndlp->nlp_flag |= NLP_LOGO_ACC;
	} else {
		ndlp->nlp_flag |= NLP_LOGO_ACC;
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);
	}

	/* The driver has to wait until the ACC completes before we can continue
	 * processing the LOGO, the action will resume in
	 * lpfc_cmpl_els_logo_acc.  Since part of processing includes an
	 * unreg_login, the driver waits so the ACC does not get aborted.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_padisc_mapped_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;
	DMABUF_t *pCmd;
	SERV_PARM *sp;		/* used for PDISC */
	ADISC *ap;		/* used for ADISC */
	uint32_t *lp;
	uint32_t cmd;
	NAME_TYPE *pnn, *ppn;
	LS_RJT stat;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;
	cmd = *lp++;

	if (cmd == ELS_CMD_ADISC) {
		ap = (ADISC *) lp;
		pnn = (NAME_TYPE *) & ap->nodeName;
		ppn = (NAME_TYPE *) & ap->portName;
	} else {
		sp = (SERV_PARM *) lp;
		pnn = (NAME_TYPE *) & sp->nodeName;
		ppn = (NAME_TYPE *) & sp->portName;
	}

	if (lpfc_check_adisc(phba, ndlp, pnn, ppn)) {
		if (cmd == ELS_CMD_ADISC) {
			lpfc_els_rsp_adisc_acc(phba, cmdiocb, ndlp);
		} else {
			lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp,
					 0, 0);
		}
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_rcv_prlo_mapped_node(lpfcHBA_t * phba,
			  LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb;

	cmdiocb = (LPFC_IOCBQ_t *) arg;

	/* Force discovery to rePLOGI as well as PRLI */
	ndlp->nlp_flag |= NLP_LOGO_ACC;
	lpfc_els_rsp_acc(phba, ELS_CMD_PRLO, cmdiocb, ndlp, 0, 0);

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_logo_mapped_node(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* save binding on binding list */
	if (ndlp->nlp_listp_bind) {
		lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);
		ndlp->nlp_listp_bind = 0;
		ndlp->nlp_sid = 0;
		ndlp->nlp_flag &= ~NLP_SEED_MASK;
	}

	/* dequeue, cancel timeout, unreg login */
	lpfc_freenode(phba, ndlp);

	/* software abort outstanding adisc */
	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		lpfc_driver_abort(phba, ndlp);
	}
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_cmpl_adisc_mapped_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_IOCBQ_t *cmdiocb, *rspiocb;
	DMABUF_t *pCmd, *pRsp;
	uint32_t *lp;
	IOCB_t *irsp;
	LPFC_BINDLIST_t *blp;
	LPFCSCSILUN_t *lunp;
	LPFCSCSITARGET_t *targetp;
	struct list_head *curr, *next;
	ADISC *ap;

	cmdiocb = (LPFC_IOCBQ_t *) arg;
	rspiocb = cmdiocb->context_un.rsp_iocb;
	irsp = &rspiocb->iocb;

	/* First remove the ndlp from any list */
	lpfc_dequenode(phba, ndlp);

	if (irsp->ulpStatus) {
		/* If this is not a driver aborted ADISC, handle the recovery
		   here */
		if ((irsp->ulpStatus != IOSTAT_LOCAL_REJECT) ||
			!(irsp->un.ulpWord[4] & IOERR_DRVR_MASK)) {
			lpfc_freenode(phba, ndlp);
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			lpfc_nlp_plogi(phba, ndlp);
		}
		return (ndlp->nlp_state);
	}

	pCmd = (DMABUF_t *) cmdiocb->context2;
	
	pRsp = (DMABUF_t *) pCmd->list.next;
	lp = (uint32_t *) pRsp->virt;

	ap = (ADISC *) ((uint8_t *) lp + sizeof (uint32_t));

	/* Check out ADISC rsp */
	if ((lpfc_check_adisc(phba, ndlp, &ap->nodeName, &ap->portName) == 0)) {
		/* This is not a driver aborted ADISC, so handle the recovery
		   here */
		ndlp->nlp_state = NLP_STE_UNUSED_NODE;
		lpfc_freenode(phba, ndlp);
		lpfc_nlp_plogi(phba, ndlp);
		return (ndlp->nlp_state);
	}

	/* Can we reassign a SCSI Id to this NPort */
	if ((blp = lpfc_assign_scsid(phba, ndlp, 0))) {
		lpfc_nlp_mapped(phba, ndlp, blp);

		ndlp->nlp_state = NLP_STE_MAPPED_NODE;
		targetp = ndlp->nlp_Target;
		if (!targetp) {
			/* new target to driver, allocate space to target <sid>
			   lun 0 */
			if (blp->nlp_Target == 0) {
				lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
				blp->nlp_Target =
				    phba->device_queue_hash[blp->nlp_sid];
			}
			targetp = blp->nlp_Target;
			ndlp->nlp_Target = targetp;
			targetp->pcontext = ndlp;
			lpfc_scsi_assign_rpi(phba, targetp, ndlp->nlp_rpi);
			targetp->un.dev_did = ndlp->nlp_DID;
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = (LPFC_NODELIST_t *) ndlp;
			}
		}
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_ALL_BITS,
				  LPFC_CLR_BITMASK);
	} else {
		lpfc_nlp_unmapped(phba, ndlp);
		ndlp->nlp_state = NLP_STE_PRLI_COMPL;
		ndlp->nlp_flag |= NLP_TGT_NO_SCSIID;
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
				  LPFC_CLR_BITMASK);
	}

	return (ndlp->nlp_state);
}

uint32_t
lpfc_cmpl_reglogin_mapped_node(lpfcHBA_t * phba,
			       LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	LPFC_MBOXQ_t *pmb, *mbox;
	MAILBOX_t *mb;
	uint32_t ldata;
	uint16_t rpi;

	if (ndlp->nlp_flag & NLP_LOGO_SND) {
		return (ndlp->nlp_state);
	}

	pmb = (LPFC_MBOXQ_t *) arg;
	mb = &pmb->mb;
	ldata = mb->un.varWords[0];	/* rpi */
	rpi = (uint16_t) (le32_to_cpu(ldata) & 0xFFFF);

	if (ndlp->nlp_rpi != rpi) {
		/* first unreg node's rpi */
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			lpfc_unreg_login(phba, ndlp->nlp_rpi, mbox);
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
			}
		}

		lpfc_no_rpi(phba, ndlp);
		ndlp->nlp_rpi = 0;

		/* now unreg rpi just got back from reg_login */
		lpfc_unreg_login(phba, rpi, mbox);
		if (lpfc_sli_issue_mbox(phba, mbox,
					(MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mbox);
		}

		/* save binding on binding list */
		if (ndlp->nlp_listp_bind) {
			lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);
			ndlp->nlp_listp_bind = 0;
			ndlp->nlp_sid = 0;
			ndlp->nlp_flag &= ~NLP_SEED_MASK;
		}


		/* software abort outstanding adisc */
		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			lpfc_nlp_plogi(phba, ndlp);
			lpfc_driver_abort(phba, ndlp);
		}

		/* send logout and put this node on plogi list */
		lpfc_issue_els_logo(phba, ndlp, 0);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
	}

	return (ndlp->nlp_state);
}

/*! lpfc_device_rm_mapped_node
  * 
  * \pre
  * \post
  * \param   phba
  * \param   ndlp
  * \param   arg
  * \param   evt
  * \return  uint32_t
  *
  * \b Description:
  *    This routine is envoked when we a request to remove a nport.
  *    It could be called when linkdown or nodev timer expires.
  *    If nodev timer is still running, we just want to exit.
  *    If this node timed out, we want to abort outstanding ADISC,
  *    save its binding, unreg login, send logout, change state to 
  *    UNUSED_NODE and place node on plogi list so it can be freed 
  *    when LOGO completes.
  *
  */
uint32_t
lpfc_device_rm_mapped_node(lpfcHBA_t * phba,
			   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
			  LPFC_SET_BITMASK);

	if (ndlp->nlp_flag & NLP_ADISC_SND) {
		lpfc_nlp_plogi(phba, ndlp);
		/* software abort outstanding adisc */
		lpfc_driver_abort(phba, ndlp);
	}

	/* save binding info */
	if (ndlp->nlp_listp_bind) {
		lpfc_nlp_bind(phba, ndlp->nlp_listp_bind);
		ndlp->nlp_listp_bind = 0;
		ndlp->nlp_sid = 0;
		ndlp->nlp_flag &= ~NLP_SEED_MASK;
	}


	/* If discovery processing causes us to remove a device, it is
	 * important that nothing gets sent to the device (soft zoning
	 * issues).
	 */
	ndlp->nlp_state = NLP_STE_UNUSED_NODE;
	/* dequeue, cancel timeout, unreg login */
	lpfc_nlp_remove(phba, ndlp);
	return (NLP_STE_FREED_NODE);
}

uint32_t
lpfc_device_add_mapped_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	if (ndlp->nlp_tmofunc.function) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
	}
	lpfc_nlp_adisc(phba, ndlp);
	return (ndlp->nlp_state);
}

uint32_t
lpfc_device_unk_mapped_node(lpfcHBA_t * phba,
			    LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* No need to do anything till the link comes back up or
	 * npr timer expires.
	 */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_disc_nodev(lpfcHBA_t * phba,
		LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

uint32_t
lpfc_disc_neverdev(lpfcHBA_t * phba,
		   LPFC_NODELIST_t * ndlp, void *arg, uint32_t evt)
{
	/* This routine does nothing, just return the current state */
	return (ndlp->nlp_state);
}

int
lpfc_geportname(NAME_TYPE * pn1, NAME_TYPE * pn2)
{
	int i;
	uint8_t *cp1, *cp2;

	i = sizeof (NAME_TYPE);
	cp1 = (uint8_t *) pn1;
	cp2 = (uint8_t *) pn2;
	while (i--) {
		if (*cp1 < *cp2) {
			return (0);
		}
		if (*cp1 > *cp2) {
			return (1);
		}
		cp1++;
		cp2++;
	}

	return (2);		/* equal */
}

int
lpfc_check_sparm(lpfcHBA_t * phba,
		 LPFC_NODELIST_t * ndlp, SERV_PARM * sp, uint32_t class)
{
	volatile SERV_PARM *hsp;

	hsp = &phba->fc_sparam;
	/* First check for supported version */

	/* Next check for class validity */
	if (sp->cls1.classValid) {

		if (sp->cls1.rcvDataSizeMsb > hsp->cls1.rcvDataSizeMsb)
			sp->cls1.rcvDataSizeMsb = hsp->cls1.rcvDataSizeMsb;
		if (sp->cls1.rcvDataSizeLsb > hsp->cls1.rcvDataSizeLsb)
			sp->cls1.rcvDataSizeLsb = hsp->cls1.rcvDataSizeLsb;
	} else if (class == CLASS1) {
		return (0);
	}

	if (sp->cls2.classValid) {

		if (sp->cls2.rcvDataSizeMsb > hsp->cls2.rcvDataSizeMsb)
			sp->cls2.rcvDataSizeMsb = hsp->cls2.rcvDataSizeMsb;
		if (sp->cls2.rcvDataSizeLsb > hsp->cls2.rcvDataSizeLsb)
			sp->cls2.rcvDataSizeLsb = hsp->cls2.rcvDataSizeLsb;
	} else if (class == CLASS2) {
		return (0);
	}

	if (sp->cls3.classValid) {

		if (sp->cls3.rcvDataSizeMsb > hsp->cls3.rcvDataSizeMsb)
			sp->cls3.rcvDataSizeMsb = hsp->cls3.rcvDataSizeMsb;
		if (sp->cls3.rcvDataSizeLsb > hsp->cls3.rcvDataSizeLsb)
			sp->cls3.rcvDataSizeLsb = hsp->cls3.rcvDataSizeLsb;
	} else if (class == CLASS3) {
		return (0);
	}

	if (sp->cmn.bbRcvSizeMsb > hsp->cmn.bbRcvSizeMsb)
		sp->cmn.bbRcvSizeMsb = hsp->cmn.bbRcvSizeMsb;
	if (sp->cmn.bbRcvSizeLsb > hsp->cmn.bbRcvSizeLsb)
		sp->cmn.bbRcvSizeLsb = hsp->cmn.bbRcvSizeLsb;

	/* If check is good, copy wwpn wwnn into ndlp */
	memcpy(&ndlp->nlp_nodename, &sp->nodeName, sizeof (NAME_TYPE));
	memcpy(&ndlp->nlp_portname, &sp->portName, sizeof (NAME_TYPE));
	return (1);
}

int
lpfc_check_adisc(lpfcHBA_t * phba,
		 LPFC_NODELIST_t * ndlp, NAME_TYPE * nn, NAME_TYPE * pn)
{
	if (lpfc_geportname((NAME_TYPE *) nn, &ndlp->nlp_nodename) != 2) {
		return (0);
	}

	if (lpfc_geportname((NAME_TYPE *) pn, &ndlp->nlp_portname) != 2) {
		return (0);
	}

	return (1);
}

int
lpfc_binding_found(LPFC_BINDLIST_t * blp, LPFC_NODELIST_t * ndlp)
{
	uint16_t bindtype;

	bindtype = blp->nlp_bind_type;
	if ((bindtype & FCP_SEED_DID) && (ndlp->nlp_DID == blp->nlp_DID)) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWPN) &&
		   (lpfc_geportname(&ndlp->nlp_portname, &blp->nlp_portname) ==
		    2)) {
		return (1);
	} else if ((bindtype & FCP_SEED_WWNN) &&
		   (lpfc_geportname(&ndlp->nlp_nodename, &blp->nlp_nodename) ==
		    2)) {
		return (1);
	}
	return (0);
}

int
lpfc_binding_useid(lpfcHBA_t * phba, uint32_t sid)
{
	LPFC_BINDLIST_t *blp;
	struct list_head *pos;

	list_for_each(pos, &phba->fc_nlpbind_list) {
		blp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);

		if (blp->nlp_sid == sid) {
			return (1);
		}
	}
	return (0);
}

int
lpfc_mapping_useid(lpfcHBA_t * phba, uint32_t sid)
{
	LPFC_NODELIST_t *mapnode;
	LPFC_BINDLIST_t *blp;
	struct list_head *pos;

	list_for_each(pos, &phba->fc_nlpmap_list) {
		mapnode = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		blp = mapnode->nlp_listp_bind;
		if (blp->nlp_sid == sid) {
			return (1);
		}
	}
	return (0);
}

LPFC_BINDLIST_t *
lpfc_create_binding(lpfcHBA_t * phba,
		    LPFC_NODELIST_t * ndlp, uint16_t index, uint16_t bindtype)
{
	LPFC_BINDLIST_t *blp;

	if ((blp = lpfc_bind_alloc(phba, 0))) {
		memset(blp, 0, sizeof (LPFC_BINDLIST_t));
		switch (bindtype) {
		case FCP_SEED_WWPN:
			blp->nlp_bind_type = FCP_SEED_WWPN;
			break;
		case FCP_SEED_WWNN:
			blp->nlp_bind_type = FCP_SEED_WWNN;
			break;
		case FCP_SEED_DID:
			blp->nlp_bind_type = FCP_SEED_DID;
			break;
		}
		blp->nlp_sid = index;
		blp->nlp_DID = ndlp->nlp_DID;
		blp->nlp_Target = phba->device_queue_hash[index];
		memcpy(&blp->nlp_nodename, &ndlp->nlp_nodename,
		       sizeof (NAME_TYPE));
		memcpy(&blp->nlp_portname, &ndlp->nlp_portname,
		       sizeof (NAME_TYPE));

		return (blp);
	}

	return (0);
}

uint32_t
lpfc_add_bind(lpfcHBA_t * phba, uint8_t bind_type,	/* NN/PN/DID */
	      void *bind_id,	/* pointer to the bind id value */
	      uint32_t scsi_id)
{
	LPFC_NODELIST_t *ndlp;
	LPFC_BINDLIST_t *blp;
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	struct list_head *pos;
	struct list_head *curr, *next;

	/* Check if the SCSI ID is currently mapped */
	ndlp = lpfc_findnode_scsiid(phba, scsi_id);
	if (ndlp && (ndlp != &phba->fc_fcpnodev)) {
		return ENOENT;
	}
	/* Check if the SCSI ID is currently in the bind list. */
	list_for_each(pos, &phba->fc_nlpbind_list) {
		blp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);
	
		if (blp->nlp_sid == scsi_id) {
			return ENOENT;
		}
		switch (bind_type) {
		case FCP_SEED_WWPN:
			if ((blp->nlp_bind_type & FCP_SEED_WWPN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_portname) ==
			     2)) {
				return EBUSY;
			}
			break;
		case FCP_SEED_WWNN:
			if ((blp->nlp_bind_type & FCP_SEED_WWNN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_nodename) ==
			     2)) {
				return EBUSY;
			}
			break;
		case FCP_SEED_DID:
			if ((blp->nlp_bind_type & FCP_SEED_DID) &&
			    (*((uint32_t *) bind_id) == blp->nlp_DID)) {
				return EBUSY;
			}
			break;
		}
	}
	if (phba->fcp_mapping != bind_type) {
		return EINVAL;
	}
	switch (bind_type) {
	case FCP_SEED_WWNN:
		{
			/* Check if the node name present in the mapped list */
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			if (ndlp) {
				return EBUSY;
			}
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_UNMAPPED,
					       bind_id);
			break;
		}
	case FCP_SEED_WWPN:
		{
			/* Check if the port name present in the mapped list */
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			if (ndlp)
				return EBUSY;
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_UNMAPPED,
					       bind_id);
			break;
		}
	case FCP_SEED_DID:
		{
			/* Check if the DID present in the mapped list */
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_MAPPED,
					      *((uint32_t *) bind_id));
			if (ndlp)
				return EBUSY;
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
					      *((uint32_t *) bind_id));
			break;
		}
	}

	/* Add to the bind list */
	if ((blp = (LPFC_BINDLIST_t *) lpfc_bind_alloc(phba, 0)) == 0) {
		return EIO;
	}
	memset(blp, 0, sizeof (LPFC_BINDLIST_t));
	blp->nlp_bind_type = bind_type;
	blp->nlp_sid = (scsi_id & 0xff);

	switch (bind_type) {
	case FCP_SEED_WWNN:
		memcpy(&blp->nlp_nodename, (uint8_t *) bind_id,
		       sizeof (NAME_TYPE));
		break;

	case FCP_SEED_WWPN:
		memcpy(&blp->nlp_portname, (uint8_t *) bind_id,
		       sizeof (NAME_TYPE));
		break;

	case FCP_SEED_DID:
		blp->nlp_DID = *((uint32_t *) bind_id);
		break;

	}

	lpfc_nlp_bind(phba, blp);
	/* 
	   If the newly added node is in the unmapped list, assign a
	   SCSI ID to the node.
	 */

	if (ndlp) {
		if ((blp = lpfc_assign_scsid(phba, ndlp, 0))) {
			lpfc_nlp_mapped(phba, ndlp, blp);
			targetp = ndlp->nlp_Target;
			ndlp->nlp_failMask = 0;
			if (targetp) {
				list_for_each_safe(curr, next,
						   &targetp->lunlist) {
					lunp = list_entry(curr, LPFCSCSILUN_t,
							  list);
					lunp->failMask = 0;
				}
			} else {
				/* new target to driver, allocate space to
				   target <sid> lun 0 */
				if (blp->nlp_Target == 0) {
					lpfc_find_lun(phba, blp->nlp_sid, 0, 1);
					blp->nlp_Target =
					    phba->device_queue_hash[blp->
								    nlp_sid];
				}
				targetp = blp->nlp_Target;
				ndlp->nlp_Target = targetp;
				targetp->pcontext = ndlp;
				lpfc_scsi_assign_rpi(phba, targetp, ndlp->nlp_rpi);
				targetp->un.dev_did = ndlp->nlp_DID;
				list_for_each_safe(curr, next, &targetp->lunlist) {
					lunp = list_entry(curr, LPFCSCSILUN_t, list);
					lunp->pnode = (LPFC_NODELIST_t *) ndlp;
				}
			}
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
					  LPFC_SET_BITMASK);
			ndlp->nlp_state = NLP_STE_MAPPED_NODE;
			lpfc_disc_issue_rptlun(phba, ndlp);
		}
	}
	return (0);
}

uint32_t
lpfc_del_bind(lpfcHBA_t * phba, uint8_t bind_type,	/* NN/PN/DID */
	      void *bind_id,	/* pointer to the bind id value */
	      uint32_t scsi_id)
{
	LPFC_BINDLIST_t *blp = 0;
	uint32_t found = 0;
	LPFC_NODELIST_t *ndlp = 0;
	struct list_head *pos;

	/* Search the mapped list for the bind_id */
	if (!bind_id) {
		ndlp = lpfc_findnode_scsiid(phba, scsi_id);
		if ((ndlp == &phba->fc_fcpnodev) ||
		    (ndlp && (!(ndlp->nlp_flag & NLP_MAPPED_LIST))))
			ndlp = 0;
	} else {

		if (bind_type != phba->fcp_mapping)
			return EINVAL;

		switch (bind_type) {
		case FCP_SEED_WWNN:
			ndlp =
			    lpfc_findnode_wwnn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			break;

		case FCP_SEED_WWPN:
			ndlp =
			    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED,
					       bind_id);
			break;

		case FCP_SEED_DID:
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_MAPPED,
					      *((uint32_t *) bind_id));
			break;
		}
	}

	/* If there is a mapped target for this bing unmap it */
	if (ndlp) {
		return EBUSY;
	}

	/* check binding list */
	list_for_each(pos, &phba->fc_nlpbind_list) {
		blp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);

		if (!bind_id) {
			/* Search binding based on SCSI ID */
			if (blp->nlp_sid == scsi_id) {
				found = 1;
				break;
			} else {
				continue;
			}
		}

		switch (bind_type) {
		case FCP_SEED_WWPN:
			if ((blp->nlp_bind_type & FCP_SEED_WWPN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_portname) ==
			     2)) {
				found = 1;
			}
			break;
		case FCP_SEED_WWNN:
			if ((blp->nlp_bind_type & FCP_SEED_WWNN) &&
			    (lpfc_geportname(bind_id, &blp->nlp_nodename) ==
			     2)) {
				found = 1;
			}
			break;
		case FCP_SEED_DID:
			if ((blp->nlp_bind_type & FCP_SEED_DID) &&
			    (*((uint32_t *) bind_id) == blp->nlp_DID)) {
				found = 1;
			}
			break;
		}
		if (found)
			break;
	}

	if (found) {
		/* take it off the bind list */
		phba->fc_bind_cnt--;
		list_del(&blp->nlp_listp);
		return 0;
	}

	return ENOENT;
}

LPFC_BINDLIST_t *
lpfc_assign_scsid(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp,
		  int prev_flag)
{
	LPFC_BINDLIST_t *blp;
	lpfcCfgParam_t *clp;
	uint16_t index;
	struct list_head *pos, *pos_tmp;

	clp = &phba->config[0];

	/* check binding list */
	list_for_each_safe(pos, pos_tmp, &phba->fc_nlpbind_list) {
		blp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);

		if (lpfc_binding_found(blp, ndlp)) {
			ndlp->nlp_sid = blp->nlp_sid;
			ndlp->nlp_Target = blp->nlp_Target;
			ndlp->nlp_flag &= ~NLP_SEED_MASK;
			switch ((blp->nlp_bind_type & FCP_SEED_MASK)) {
			case FCP_SEED_WWPN:
				ndlp->nlp_flag |= NLP_SEED_WWPN;
				break;
			case FCP_SEED_WWNN:
				ndlp->nlp_flag |= NLP_SEED_WWNN;
				break;
			case FCP_SEED_DID:
				ndlp->nlp_flag |= NLP_SEED_DID;
				break;
			}
			if (blp->nlp_bind_type & FCP_SEED_AUTO) {
				ndlp->nlp_flag |= NLP_AUTOMAP;
			}

			/* take it off the binding list */
			phba->fc_bind_cnt--;
			list_del_init(pos);

			/* Reassign scsi id <sid> to NPort <nlp_DID> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0213,
					lpfc_mes0213,
					lpfc_msgBlk0213.msgPreambleStr,
					blp->nlp_sid, ndlp->nlp_DID,
					blp->nlp_bind_type, ndlp->nlp_flag,
					ndlp->nlp_state, ndlp->nlp_rpi);

			return (blp);
		}
	}

	if(prev_flag)
		return(0);

	/* NOTE: if scan-down = 2 and we have private loop, then we use
	 * AlpaArray to determine sid.
	 */
	if ((clp[LPFC_CFG_BINDMETHOD].a_current == 4) &&
	    ((phba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) ||
	     (phba->fc_topology != TOPOLOGY_LOOP))) {
		/* Log message: ALPA based binding used on a non loop
		   topology */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0245,
				lpfc_mes0245,
				lpfc_msgBlk0245.msgPreambleStr,
				phba->fc_topology);
	}

	if ((clp[LPFC_CFG_BINDMETHOD].a_current == 4) &&
	    !(phba->fc_flag & (FC_PUBLIC_LOOP | FC_FABRIC)) &&
	    (phba->fc_topology == TOPOLOGY_LOOP)) {
		for (index = 0; index < FC_MAXLOOP; index++) {
			if (ndlp->nlp_DID == (uint32_t) lpfcAlpaArray[index]) {
				if ((blp =
				     lpfc_create_binding(phba, ndlp, index,
							 FCP_SEED_DID))) {

					ndlp->nlp_sid = index;
					ndlp->nlp_Target =
					    phba->device_queue_hash[index];
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
					ndlp->nlp_flag |= NLP_SEED_DID;
					ndlp->nlp_flag |= NLP_SEED_ALPA;

					/* Assign scandown scsi id <sid> to
					   NPort <nlp_DID> */
					lpfc_printf_log(phba->brd_no,
						&lpfc_msgBlk0216,
						lpfc_mes0216,
						lpfc_msgBlk0216.msgPreambleStr,
						blp->nlp_sid, ndlp->nlp_DID,
						blp->nlp_bind_type,
						ndlp->nlp_flag, ndlp->nlp_state,
						ndlp->nlp_rpi);

					return (blp);
				}
				goto errid;
			}
		}
	}

	if (clp[LPFC_CFG_AUTOMAP].a_current) {
		while (1) {
			if ((lpfc_binding_useid(phba, phba->sid_cnt))
			     || (lpfc_mapping_useid (phba, phba->sid_cnt))) {

				phba->sid_cnt++;
			} else {
				if ((blp =
				     lpfc_create_binding(phba, ndlp,
							 phba->sid_cnt,
							 phba->fcp_mapping))) {
					ndlp->nlp_sid = blp->nlp_sid;
					ndlp->nlp_Target = blp->nlp_Target;
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
					switch ((blp->
						 nlp_bind_type & FCP_SEED_MASK))
					{
					case FCP_SEED_WWPN:
						ndlp->nlp_flag |= NLP_SEED_WWPN;
						break;
					case FCP_SEED_WWNN:
						ndlp->nlp_flag |= NLP_SEED_WWNN;
						break;
					case FCP_SEED_DID:
						ndlp->nlp_flag |= NLP_SEED_DID;
						break;
					}
					blp->nlp_bind_type |= FCP_SEED_AUTO;
					ndlp->nlp_flag |= NLP_AUTOMAP;

					phba->sid_cnt++;

					/* Assign scsi id <sid> to NPort
					   <nlp_DID> */
					lpfc_printf_log(phba->brd_no,
						&lpfc_msgBlk0229,
						lpfc_mes0229,
						lpfc_msgBlk0229.msgPreambleStr,
						blp->nlp_sid, ndlp->nlp_DID,
						blp->nlp_bind_type,
						ndlp->nlp_flag, ndlp->nlp_state,
						ndlp->nlp_rpi);

					return (blp);
				}
				goto errid;
			}
		}
	}
	/* if automap on */
      errid:
	/* Cannot assign scsi id on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0230,
			lpfc_mes0230,
			lpfc_msgBlk0230.msgPreambleStr,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	return (0);
}
