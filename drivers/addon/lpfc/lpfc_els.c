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
 * $Id: lpfc_els.c 502 2006-04-04 17:11:23Z sf_support $
 */
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/pci.h>


#include <linux/blk.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "hbaapi.h"
#include "lpfc_dfc.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"

int lpfc_els_rcv_rscn(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_els_rcv_flogi(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_els_rcv_rrq(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_rnid(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_farp(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_farpr(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rcv_fan(lpfcHBA_t *, LPFC_IOCBQ_t *, LPFC_NODELIST_t *);

int lpfc_max_els_tries = 3;

int
lpfc_initial_flogi(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp;

	/* First look for Fabric ndlp on the unmapped list */

	if ((ndlp =
	     lpfc_findnode_did(phba, (NLP_SEARCH_UNMAPPED | NLP_SEARCH_DEQUE),
			       Fabric_DID)) == 0) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
			return (0);
		}
		memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = Fabric_DID;
	}
	if (lpfc_issue_els_flogi(phba, ndlp, 0)) {
		lpfc_nlp_free(phba, ndlp);
	}
	return (1);
}

int
lpfc_issue_els_flogi(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	SERV_PARM *sp;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (SERV_PARM));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_FLOGI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For FLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_FLOGI;
	pCmd += sizeof (uint32_t);
	memcpy(pCmd, &phba->fc_sparam, sizeof (SERV_PARM));
	sp = (SERV_PARM *) pCmd;

	/* Setup CSPs accordingly for Fabric */
	sp->cmn.e_d_tov = 0;
	sp->cmn.w2.r_a_tov = 0;
	sp->cls1.classValid = 0;
	sp->cls2.seqDelivery = 1;
	sp->cls3.seqDelivery = 1;
	if (sp->cmn.fcphLow < FC_PH3)
		sp->cmn.fcphLow = FC_PH3;
	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	lpfc_set_disctmo(phba);

	phba->fc_stat.elsXmitFLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_flogi;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_flogi(lpfcHBA_t * phba,
		    LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	DMABUF_t *pCmd, *pRsp;
	SERV_PARM *sp;
	uint32_t *lp;
	LPFC_MBOXQ_t *mbox;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;
	uint32_t rc;

	psli = &phba->sli;
	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	pCmd = (DMABUF_t *) cmdiocb->context2;

	clp = &phba->config[0];

	/* Return to default values since first FLOGI has completed */
	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	phba->fcp_timeout_offset = 2 * phba->fc_ratov +
		clp[LPFC_CFG_EXTRA_IO_TMO].a_current;

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, rspiocb))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* FLOGI failed, so there is no fabric */
		phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);

		/* If private loop, then allow max outstandting els to be
		 * LPFC_MAX_DISC_THREADS (32). Scanning in the case of no 
		 * alpa map would take too long otherwise. 
		 */
		if (phba->alpa_map[0] == 0) {
			clp[LPFC_CFG_DISC_THREADS].a_current =
			    LPFC_MAX_DISC_THREADS;
		}

		/* FLOGI failure */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0100,
				lpfc_mes0100,
				lpfc_msgBlk0100.msgPreambleStr,
				irsp->ulpStatus, irsp->un.ulpWord[4]);
	} else {
		/* The FLogI succeeded.  Sync the data for the CPU before
		 * accessing it. 
		 */
		pRsp = (DMABUF_t *) pCmd->list.next;
		lp = (uint32_t *) pRsp->virt;

		sp = (SERV_PARM *) ((uint8_t *) lp + sizeof (uint32_t));

		/* FLOGI completes successfully */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0101,
				lpfc_mes0101, lpfc_msgBlk0101.msgPreambleStr,
				irsp->un.ulpWord[4], sp->cmn.e_d_tov,
				sp->cmn.w2.r_a_tov, sp->cmn.edtovResolution);

		if (phba->hba_state == LPFC_FLOGI) {
			/* If Common Service Parameters indicate Nport
			 * we are point to point, if Fport we are Fabric.
			 */
			if (sp->cmn.fPort) {
				phba->fc_flag |= FC_FABRIC;
				if (sp->cmn.edtovResolution) {
					/* E_D_TOV ticks are in nanoseconds */
					phba->fc_edtov =
					    (be32_to_cpu(sp->cmn.e_d_tov) +
					     999999) / 1000000;
				} else {
					/* E_D_TOV ticks are in milliseconds */
					phba->fc_edtov =
					    be32_to_cpu(sp->cmn.e_d_tov);
				}
				phba->fc_ratov =
				    (be32_to_cpu(sp->cmn.w2.r_a_tov) +
				     999) / 1000;
				phba->fcp_timeout_offset =
				    2 * phba->fc_ratov +
				    clp[LPFC_CFG_EXTRA_IO_TMO].a_current;

				if (phba->fc_topology == TOPOLOGY_LOOP) {
					phba->fc_flag |= FC_PUBLIC_LOOP;
				} else {
					/* If we are a N-port connected to a
					 * Fabric, fixup sparam's so logins to
					 * devices on remote loops work.
					 */
					phba->fc_sparam.cmn.altBbCredit = 1;
				}

				phba->fc_myDID = irsp->un.ulpWord[4] & Mask_DID;

				memcpy(&ndlp->nlp_portname,
				       &sp->portName, sizeof (NAME_TYPE));
				memcpy(&ndlp->nlp_nodename,
				       &sp->nodeName, sizeof (NAME_TYPE));
				memcpy(&phba->fc_fabparam, sp,
				       sizeof (SERV_PARM));
				if ((mbox = lpfc_mbox_alloc(phba, 0)) == 0) {
					goto flogifail;
				}
				phba->hba_state = LPFC_FABRIC_CFG_LINK;
				lpfc_config_link(phba, mbox);
				if (lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					lpfc_mbox_free(phba, mbox);
					goto flogifail;
				}

				if ((mbox = lpfc_mbox_alloc(phba, 0)) == 0) {
					goto flogifail;
				}
				if (lpfc_reg_login(phba, Fabric_DID,
						   (uint8_t *) sp, mbox,
						   0) == 0) {
					/* set_slim mailbox command needs to
					 * execute first, queue this command to
					 * be processed later.
					 */
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_fabric_reg_login;
					mbox->context2 = ndlp;
					if (lpfc_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						DMABUF_t *mp;
						mp = (DMABUF_t *)(mbox->context1);
						lpfc_mbuf_free(phba, mp->virt, mp->phys);
						kfree(mp);
						lpfc_mbox_free(phba, mbox);
						goto flogifail;
					}
				} else {
					lpfc_mbox_free(phba, mbox);
					goto flogifail;
				}
			} else {
				/* We FLOGIed into an NPort, initiate pt2pt
				   protocol */
				phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
				phba->fc_edtov = FF_DEF_EDTOV;
				phba->fc_ratov = FF_DEF_RATOV;
				phba->fcp_timeout_offset = 2 * phba->fc_ratov +
				    clp[LPFC_CFG_EXTRA_IO_TMO].a_current;
				if ((rc =
				     lpfc_geportname((NAME_TYPE *) & phba->
						     fc_portname,
						     (NAME_TYPE *) & sp->
						     portName))) {
					/* This side will initiate the PLOGI */
					phba->fc_flag |= FC_PT2PT_PLOGI;

					/* N_Port ID cannot be 0, set our to
					 * LocalID the other side will be
					 * RemoteID.
					 */

					/* not equal */
					if (rc == 1)
						phba->fc_myDID = PT2PT_LocalID;
					rc = 0;

					if ((mbox = lpfc_mbox_alloc(phba, 0))
					    == 0) {
						goto flogifail;
					}
					lpfc_config_link(phba, mbox);
					if (lpfc_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						lpfc_mbox_free(phba, mbox);
						goto flogifail;
					}
					lpfc_nlp_free(phba, ndlp);

					if ((ndlp =
					     lpfc_findnode_did(phba,
							       NLP_SEARCH_ALL,
							       PT2PT_RemoteID))
					    == 0) {
						/* Cannot find existing Fabric
						   ndlp, so allocate a new
						   one */
						if ((ndlp = (LPFC_NODELIST_t *)
						     lpfc_nlp_alloc(phba,
								    0)) == 0) {
							goto flogifail;
						}
						memset(ndlp, 0,
						       sizeof
						       (LPFC_NODELIST_t));
						ndlp->nlp_DID = PT2PT_RemoteID;
					}
					memcpy(&ndlp->nlp_portname,
					       &sp->portName,
					       sizeof (NAME_TYPE));
					memcpy(&ndlp->nlp_nodename,
					       &sp->nodeName,
					       sizeof (NAME_TYPE));
					lpfc_nlp_plogi(phba, ndlp);
				} else {
					/* This side will wait for the PLOGI */
					lpfc_nlp_free(phba, ndlp);
				}

				phba->fc_flag |= FC_PT2PT;
				lpfc_set_disctmo(phba);

				/* Start discovery - this should just do
				   CLEAR_LA */
				lpfc_disc_start(phba);
			}
			goto out;
		}
	}

      flogifail:
	lpfc_nlp_remove(phba, ndlp);

	/* Do not start discovery on a driver initialed FLOGI abort */
	if((irsp->ulpStatus != IOSTAT_LOCAL_REJECT) ||
		(irsp->un.ulpWord[4] != IOERR_SLI_ABORTED)) {

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
	}

      out:
	/* if myDID ndlp exists, remove it */
	if ((ndlp = lpfc_findnode_did(phba,
	     NLP_SEARCH_ALL, phba->fc_myDID))) {
		lpfc_nlp_remove(phba, ndlp);
	}

	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_els_abort_flogi(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_NODELIST_t *ndlp;
	IOCB_t *icmd;
	struct list_head *curr, *next;

	/* Abort outstanding I/O to the Fabric */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0242,
			lpfc_mes0242,
			lpfc_msgBlk0242.msgPreambleStr,
			Fabric_DID);

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		/* Check to see if iocb matches the nport we are
		   looking for */
		icmd = &iocb->iocb;
		if (icmd->ulpCommand == CMD_ELS_REQUEST64_CR) {
			ndlp = (LPFC_NODELIST_t *)(iocb->context1);
			if(ndlp && (ndlp->nlp_DID == Fabric_DID)) {
				/* It matches, so deque and call compl
				   with an error */
				list_del(&iocb->list);
				pring->txcmplq_cnt--;

				if ((icmd->un.elsreq64.bdl.ulpIoTag32)) {
					lpfc_sli_issue_abort_iotag32
					    (phba, pring, iocb);
				}
				if (iocb->iocb_cmpl) {
					icmd->ulpStatus =
					    IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] =
					    IOERR_SLI_ABORTED;
					(iocb->iocb_cmpl) (phba, iocb, iocb);
				} else {
					lpfc_iocb_free(phba, iocb);
				}
			}
		}
	}
	return (0);
}

int
lpfc_issue_els_plogi(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	SERV_PARM *sp;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (SERV_PARM));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_PLOGI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For PLOGI request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_PLOGI;
	pCmd += sizeof (uint32_t);
	memcpy(pCmd, &phba->fc_sparam, sizeof (SERV_PARM));
	sp = (SERV_PARM *) pCmd;

	if (sp->cmn.fcphLow < FC_PH_4_3)
		sp->cmn.fcphLow = FC_PH_4_3;

	if (sp->cmn.fcphHigh < FC_PH3)
		sp->cmn.fcphHigh = FC_PH3;

	phba->fc_stat.elsXmitPLOGI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_plogi;
	ndlp->nlp_flag |= NLP_PLOGI_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ)
	    == IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_PLOGI_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_plogi(lpfcHBA_t * phba,
		    LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	int disc;

	psli = &phba->sli;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_PLOGI_SND;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_DISC_NODE);
	ndlp->nlp_flag &= ~NLP_DISC_NODE;

	/* PLOGI completes to NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0102,
			lpfc_mes0102, lpfc_msgBlk0102.msgPreambleStr,
			ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			disc, phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, rspiocb))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				ndlp->nlp_flag |= NLP_DISC_NODE;
			}
			goto out;
		}

		/* PLOGI failed */
		if (!((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		      (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED)))
			lpfc_disc_state_machine(phba, ndlp, cmdiocb,
						NLP_EVT_CMPL_PLOGI);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_PLOGI);
	}

	if (disc && phba->num_disc_nodes) {
		/* Check to see if there are more PLOGIs to be sent */
		lpfc_more_plogi(phba);
	}

	if (phba->num_disc_nodes == 0) {
		if (disc) {
			phba->hba_flag &= ~FC_NDISC_ACTIVE;
		}
		lpfc_can_disctmo(phba);
		if (phba->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we were
			 * processing this one.
			 */
			if ((phba->fc_rscn_id_cnt == 0) &&
			    (!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
				lpfc_els_flush_rscn(phba);
			} else {
				lpfc_els_handle_rscn(phba);
			}
		}
	}

      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_prli(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	PRLI *npr;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (PRLI));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_PRLI)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For PRLI request, remainder of payload is service parameters */
	memset(pCmd, 0, (sizeof (PRLI) + sizeof (uint32_t)));
	*((uint32_t *) (pCmd)) = ELS_CMD_PRLI;
	pCmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	npr = (PRLI *) pCmd;
	/*
	 * If our firmware version is 3.20 or later, 
	 * set the following bits for FC-TAPE support.
	 */
	if (phba->vpd.rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;

	/* For FCP support */
	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	phba->fc_stat.elsXmitPRLI++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_prli;
	ndlp->nlp_flag |= NLP_PRLI_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_PRLI_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	phba->fc_prli_sent++;
	return (0);
}

void
lpfc_cmpl_els_prli(lpfcHBA_t * phba,
		   LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_PRLI_SND;

	/* PRLI completes to NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0103,
			lpfc_mes0103, lpfc_msgBlk0103.msgPreambleStr,
			ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			phba->num_disc_nodes);

	phba->fc_prli_sent--;
	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, rspiocb))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* PRLI failed */
		if ((!((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		       (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED))) ||
		    (ndlp->nlp_state == NLP_STE_PRLI_ISSUE))
			lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_PRLI);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_PRLI);
	}

      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_adisc(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	ADISC *ap;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (ADISC));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_ADISC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	/* For ADISC request, remainder of payload is service parameters */
	*((uint32_t *) (pCmd)) = ELS_CMD_ADISC;
	pCmd += sizeof (uint32_t);

	/* Fill in ADISC payload */
	ap = (ADISC *) pCmd;
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &phba->fc_portname, sizeof (NAME_TYPE));
	memcpy(&ap->nodeName, &phba->fc_nodename, sizeof (NAME_TYPE));
	ap->DID = be32_to_cpu(phba->fc_myDID);

	phba->fc_stat.elsXmitADISC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_adisc;
	ndlp->nlp_flag |= NLP_ADISC_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_ADISC_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

/* lpfc_rscn_disc is only called by lpfc_cmpl_els_adisc below */
static void
lpfc_rscn_disc(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;
	struct list_head *pos;

	clp = &phba->config[0];

	/* RSCN discovery */
	/* go thru PLOGI list and issue ELS PLOGIs */
	if (phba->fc_plogi_cnt) {
		list_for_each(pos, &phba->fc_plogi_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
				ndlp->nlp_flag |= NLP_DISC_NODE;
				phba->num_disc_nodes++;
				if (phba->num_disc_nodes >=
				    clp[LPFC_CFG_DISC_THREADS].a_current) {
					if (phba->fc_plogi_cnt >
					    phba->num_disc_nodes)
						phba->fc_flag |= FC_NLP_MORE;
					break;
				}
			}
		}
	} else {
		if (phba->fc_flag & FC_RSCN_MODE) {
			/* Check to see if more RSCNs came in while we were
			 * processing this one.
			 */
			if ((phba->fc_rscn_id_cnt == 0) &&
			    (!(phba->fc_flag & FC_RSCN_DISCOVERY))) {
				lpfc_els_flush_rscn(phba);
			} else {
				lpfc_els_handle_rscn(phba);
			}
		}
	}
}

void
lpfc_cmpl_els_adisc(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		    LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFC_MBOXQ_t *mbox;
	int disc;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	psli = &phba->sli;

	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_ADISC_SND;

	if ((irsp->ulpStatus ) &&
	    (irsp->un.ulpWord[4] == IOERR_SLI_DOWN))
		goto out;

	/* Since ndlp can be freed in the disc state machine, note if this node
	 * is being used during discovery.
	 */
	disc = (ndlp->nlp_flag & NLP_DISC_NODE);
	ndlp->nlp_flag &= ~NLP_DISC_NODE;

	/* ADISC completes to NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0104,
			lpfc_mes0104, lpfc_msgBlk0104.msgPreambleStr,
			ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			disc, phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, rspiocb))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			if (disc) {
				ndlp->nlp_flag |= NLP_DISC_NODE;
				lpfc_set_disctmo(phba);
			}
			goto out;
		}
		/* ADISC failed */
		if (!((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		      (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED))) {
			lpfc_disc_state_machine(phba, ndlp, cmdiocb,
						NLP_EVT_CMPL_ADISC);
		}
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb,
					NLP_EVT_CMPL_ADISC);
	}

	if (disc && phba->num_disc_nodes) {
		/* Check to see if there are more ADISCs to be sent */
		lpfc_more_adisc(phba);

		/* Check to see if we are done with ADISC authentication */
		if (phba->num_disc_nodes == 0) {
			/* If we get here, there is nothing left to wait for */
			if ((phba->hba_state < LPFC_HBA_READY) &&
			    (phba->hba_state != LPFC_CLEAR_LA)) {
				/* Link up discovery */
				if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
					phba->hba_state = LPFC_CLEAR_LA;
					lpfc_clear_la(phba, mbox);
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_clear_la;
					if (lpfc_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						lpfc_mbox_free(phba, mbox);
						lpfc_disc_flush_list(phba);
						psli->ring[(psli->ip_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						psli->ring[(psli->fcp_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						psli->ring[(psli->next_ring)].
						    flag &=
						    ~LPFC_STOP_IOCB_EVENT;
						phba->hba_state =
						    LPFC_HBA_READY;
					}
				}
			} else {
				lpfc_rscn_disc(phba);
			}
		}
	}
      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_logo(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint8_t retry)
{
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	cmdsize = 2 * (sizeof (uint32_t) + sizeof (NAME_TYPE));
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_LOGO)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
	*((uint32_t *) (pCmd)) = ELS_CMD_LOGO;
	pCmd += sizeof (uint32_t);

	/* Fill in LOGO payload */
	*((uint32_t *) (pCmd)) = be32_to_cpu(phba->fc_myDID);
	pCmd += sizeof (uint32_t);
	memcpy(pCmd, &phba->fc_portname, sizeof (NAME_TYPE));

	phba->fc_stat.elsXmitLOGO++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_logo;
	ndlp->nlp_flag |= NLP_LOGO_SND;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		ndlp->nlp_flag &= ~NLP_LOGO_SND;
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_els_logo(lpfcHBA_t * phba,
		   LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb;

	irsp = &(rspiocb->iocb);
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	ndlp->nlp_flag &= ~NLP_LOGO_SND;

	/* LOGO completes to NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0105,
			lpfc_mes0105, lpfc_msgBlk0105.msgPreambleStr,
			ndlp->nlp_DID, irsp->ulpStatus, irsp->un.ulpWord[4],
			phba->num_disc_nodes);

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, rspiocb))
		goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (lpfc_els_retry(phba, cmdiocb, rspiocb)) {
			/* ELS command is being retried */
			goto out;
		}
		/* LOGO failed */
		if (!((irsp->ulpStatus == IOSTAT_LOCAL_REJECT) &&
		      (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED)))
			lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_LOGO);
	} else {
		/* Good status, call state machine */
		lpfc_disc_state_machine(phba, ndlp, cmdiocb, NLP_EVT_CMPL_LOGO);
	}

      out:
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_issue_els_scr(lpfcHBA_t * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	cmdsize = (sizeof (uint32_t) + sizeof (SCR));
	if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
		return (1);
	}

	memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = nportid;

	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_SCR)) == 0) {
		lpfc_nlp_free(phba, ndlp);
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_SCR;
	pCmd += sizeof (uint32_t);

	/* For SCR, remainder of payload is SCR parameter page */
	memset(pCmd, 0, sizeof (SCR));
	((SCR *) pCmd)->Function = SCR_FUNC_FULL;

	phba->fc_stat.elsXmitSCR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_nlp_free(phba, ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	lpfc_nlp_free(phba, ndlp);
	return (0);
}

int
lpfc_issue_els_farp(lpfcHBA_t * phba, uint8_t * arg,
		    LPFC_FARP_ADDR_TYPE argFlag)
{
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	FARP *fp;
	uint8_t *pCmd;
	uint32_t *lp;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = (sizeof (uint32_t) + sizeof (FARP));
	if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
		return (1);
	}
	memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = Bcast_DID;
	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, 0,
					  ndlp, ELS_CMD_RNID)) == 0) {
		lpfc_nlp_free(phba, ndlp);
		return (1);
	}

	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
	*((uint32_t *) (pCmd)) = ELS_CMD_FARP;
	pCmd += sizeof (uint32_t);

	/* Provide a timeout value, function, and context.  If the IP node on
	 * far end never responds, this FARP and all IP bufs must be timed out.
	 */
	icmd = &elsiocb->iocb;
	icmd->ulpTimeout = phba->fc_ipfarp_timeout;
	icmd->ulpContext = (uint16_t) ELS_CMD_FARP;

	/* Fill in FARP payload */

	fp = (FARP *) (pCmd);
	memset(fp, 0, sizeof (FARP));
	lp = (uint32_t *) pCmd;
	*lp++ = be32_to_cpu(phba->fc_myDID);
	fp->Mflags = FARP_MATCH_PORT;
	fp->Rflags = FARP_REQUEST_PLOGI;
	memcpy(&fp->OportName, &phba->fc_portname, sizeof (NAME_TYPE));
	memcpy(&fp->OnodeName, &phba->fc_nodename, sizeof (NAME_TYPE));
	switch (argFlag) {
	case LPFC_FARP_BY_IEEE:
		fp->Mflags = FARP_MATCH_PORT;
		fp->RportName.nameType = NAME_IEEE;	/* IEEE name */
		fp->RportName.IEEEextMsn = 0;
		fp->RportName.IEEEextLsb = 0;
		memcpy(fp->RportName.IEEE, arg, 6);
		fp->RnodeName.nameType = NAME_IEEE;	/* IEEE name */
		fp->RnodeName.IEEEextMsn = 0;
		fp->RnodeName.IEEEextLsb = 0;
		memcpy(fp->RnodeName.IEEE, arg, 6);
		break;
	case LPFC_FARP_BY_WWPN:
		fp->Mflags = FARP_MATCH_PORT;
		memcpy(&fp->RportName, arg, sizeof (NAME_TYPE));
		break;
	case LPFC_FARP_BY_WWNN:
		fp->Mflags = FARP_MATCH_NODE;
		memcpy(&fp->RnodeName, arg, sizeof (NAME_TYPE));
		break;
	}

	phba->fc_stat.elsXmitFARP++;

	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_nlp_free(phba, ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}

	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0610,
			lpfc_mes0610,
			lpfc_msgBlk0610.msgPreambleStr,
			phba->fc_nodename.IEEE[0], phba->fc_nodename.IEEE[1],
			phba->fc_nodename.IEEE[2], phba->fc_nodename.IEEE[3],
			phba->fc_nodename.IEEE[4], phba->fc_nodename.IEEE[5]);
	return (0);
}

int
lpfc_issue_els_farpr(lpfcHBA_t * phba, uint32_t nportid, uint8_t retry)
{
	IOCB_t *icmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	FARP *fp;
	uint8_t *pCmd;
	uint32_t *lp;
	uint16_t cmdsize;
	LPFC_NODELIST_t *ondlp;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	cmdsize = (sizeof (uint32_t) + sizeof (FARP));
	if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
		return (1);
	}
	memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
	ndlp->nlp_DID = nportid;

	if ((elsiocb = lpfc_prep_els_iocb(phba, TRUE, cmdsize, retry,
					  ndlp, ELS_CMD_RNID)) == 0) {
		lpfc_nlp_free(phba, ndlp);
		return (1);
	}

	icmd = &elsiocb->iocb;
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_FARPR;
	pCmd += sizeof (uint32_t);

	/* Fill in FARPR payload */
	fp = (FARP *) (pCmd);
	memset(fp, 0, sizeof (FARP));
	lp = (uint32_t *) pCmd;
	*lp++ = be32_to_cpu(nportid);
	*lp++ = be32_to_cpu(phba->fc_myDID);
	fp->Rflags = 0;
	fp->Mflags = (FARP_MATCH_PORT | FARP_MATCH_NODE);

	memcpy(&fp->RportName, &phba->fc_portname, sizeof (NAME_TYPE));
	memcpy(&fp->RnodeName, &phba->fc_nodename, sizeof (NAME_TYPE));
	if ((ondlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, nportid))) {
		memcpy(&fp->OportName, &ondlp->nlp_portname,
		       sizeof (NAME_TYPE));
		memcpy(&fp->OnodeName, &ondlp->nlp_nodename,
		       sizeof (NAME_TYPE));
	}

	phba->fc_stat.elsXmitFARPR++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_cmd;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_nlp_free(phba, ndlp);
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	lpfc_nlp_free(phba, ndlp);
	return (0);
}

void
lpfc_cmpl_els_cmd(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		  LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;

	irsp = &rspiocb->iocb;

	/* ELS cmd tag <ulpIoTag> completes */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0106,
			lpfc_mes0106,
			lpfc_msgBlk0106.msgPreambleStr,
			irsp->ulpIoTag, irsp->ulpStatus, irsp->un.ulpWord[4]);

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

void
lpfc_els_retry_delay(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_NODELIST_t *ndlp;
	uint32_t cmd;
	uint32_t did;
	uint8_t retry;
	struct clk_data *clkData;
	unsigned long iflag;

	clkData = (struct clk_data *)ptr;
	phba = clkData->phba;
	LPFC_DRVR_LOCK(phba, iflag);
       	if (clkData->flags & TM_CANCELED) {
		list_del((struct list_head *)clkData);
		kfree(clkData);	
		goto out;
	}

	did = (uint32_t) (unsigned long)(clkData->clData1);
	cmd = (uint32_t) (unsigned long)(clkData->clData2);
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did)) == 0) {
		if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
			LPFC_DRVR_UNLOCK(phba, iflag);
			return;
		}
		memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = did;
	}

	ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	ndlp->nlp_tmofunc.function = 0;
	retry = ndlp->nlp_retry;

	switch (cmd) {
	case ELS_CMD_FLOGI:
		lpfc_issue_els_flogi(phba, ndlp, retry);
		break;
	case ELS_CMD_PLOGI:
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_nlp_plogi(phba, ndlp);
		lpfc_issue_els_plogi(phba, ndlp, retry);
		break;
	case ELS_CMD_ADISC:
		lpfc_issue_els_adisc(phba, ndlp, retry);
		break;
	case ELS_CMD_PRLI:
		lpfc_issue_els_prli(phba, ndlp, retry);
		break;
	case ELS_CMD_LOGO:
		lpfc_issue_els_logo(phba, ndlp, retry);
		break;
	}
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

int
lpfc_els_retry(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	DMABUF_t *pCmd;
	LPFC_NODELIST_t *ndlp;
	uint32_t *elscmd;
	lpfcCfgParam_t *clp;
	LS_RJT stat;
	int retry, maxretry;
	int delay;
	uint32_t cmd;

	clp = &phba->config[0];
	retry = 0;
	delay = 0;
	maxretry = lpfc_max_els_tries;
	irsp = &rspiocb->iocb;
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;

	pCmd = (DMABUF_t *) cmdiocb->context2;
	cmd = 0;
	/* Note: context2 may be 0 for internal driver abort 
	 * of delays ELS command.
	 */

	if (pCmd && pCmd->virt) {
		elscmd = (uint32_t *) (pCmd->virt);
		cmd = *elscmd++;
	}

	switch (irsp->ulpStatus) {
	case IOSTAT_FCP_RSP_ERROR:
	case IOSTAT_REMOTE_STOP:
		break;

	case IOSTAT_LOCAL_REJECT:
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_LINK_DOWN)
			break;

		if (irsp->un.ulpWord[4] == IOERR_SLI_DOWN)
			break;

		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_LOOP_OPEN_FAILURE) {
			if (cmd == ELS_CMD_PLOGI) {
				if (cmdiocb->retry == 0) {
					delay = 1;
				}
			}
			retry = 1;
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_SEQUENCE_TIMEOUT) {
			retry = 1;
			if ((cmd == ELS_CMD_FLOGI)
			    && (phba->fc_topology != TOPOLOGY_LOOP)) {
				delay = 1;
				maxretry = 48;
			}
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_NO_RESOURCES) {
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
			}
			retry = 1;
			break;
		}
		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_INVALID_RPI) {
			retry = 1;
			break;
		}

		if ((irsp->un.ulpWord[4] & 0xff) == IOERR_ABORT_REQUESTED) {
			if (cmd == ELS_CMD_PRLI) {
				if (cmdiocb->retry == 0) {
					delay = 1;
				}
			}
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_RJT:
	case IOSTAT_FABRIC_RJT:
		if (irsp->un.ulpWord[4] & RJT_UNAVAIL_TEMP) {
			retry = 1;
			break;
		}
		break;

	case IOSTAT_NPORT_BSY:
	case IOSTAT_FABRIC_BSY:
		retry = 1;
		break;

	case IOSTAT_LS_RJT:
		stat.un.lsRjtError = be32_to_cpu(irsp->un.ulpWord[4]);
		/* Added for Vendor specifc support
		 * Just keep retrying for these Rsn / Exp codes
		 */
		switch (stat.un.b.lsRjtRsnCode) {
		case LSRJT_UNABLE_TPC:
			if (stat.un.b.lsRjtRsnCodeExp ==
			    LSEXP_CMD_IN_PROGRESS) {
				if (cmd == ELS_CMD_PLOGI) {
					delay = 1;
					maxretry = 48;
				}
				retry = 1;
				break;
			}
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = lpfc_max_els_tries + 1;
				retry = 1;
				break;
			}
			break;

		case LSRJT_LOGICAL_BSY:
			if (cmd == ELS_CMD_PLOGI) {
				delay = 1;
				maxretry = 48;
			}
			retry = 1;
			break;
		}
		break;

	case IOSTAT_INTERMED_RSP:
	case IOSTAT_BA_RJT:
		break;

	default:
		break;
	}

	if (ndlp->nlp_DID == FDMI_DID) {
		retry = 1;
	}

	if ((++cmdiocb->retry) >= maxretry) {
		phba->fc_stat.elsRetryExceeded++;
		retry = 0;
	}

	if (retry) {

		/* Retry ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0107,
				lpfc_mes0107,
				lpfc_msgBlk0107.msgPreambleStr,
				cmd, ndlp->nlp_DID, cmdiocb->retry, delay);

		if ((cmd == ELS_CMD_PLOGI) || (cmd == ELS_CMD_ADISC)) {
			/* If discovery / RSCN timer is running, reset it */
			if ((phba->fc_disctmo.function)
			    || (phba->fc_flag & FC_RSCN_MODE)) {
				lpfc_set_disctmo(phba);
			}
		}

		phba->fc_stat.elsXmitRetry++;
		if (delay) {
			phba->fc_stat.elsDelayRetry++;
			if (ndlp->nlp_tmofunc.function) {
				ndlp->nlp_flag &= ~NLP_DELAY_TMO;
				lpfc_stop_timer((struct clk_data *)
						ndlp->nlp_tmofunc.data);
			}
			if (cmd == ELS_CMD_PLOGI) {
				lpfc_nlp_plogi(phba, ndlp);
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			}
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			ndlp->nlp_retry = cmdiocb->retry;

			lpfc_start_timer(phba, 1, &ndlp->nlp_tmofunc,
					 lpfc_els_retry_delay,
					 (unsigned long)ndlp->nlp_DID,
					 (unsigned long)cmd);

			return (1);
		}
		switch (cmd) {
		case ELS_CMD_FLOGI:
			lpfc_issue_els_flogi(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_PLOGI:
			lpfc_issue_els_plogi(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_ADISC:
			lpfc_issue_els_adisc(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_PRLI:
			lpfc_issue_els_prli(phba, ndlp, cmdiocb->retry);
			return (1);
		case ELS_CMD_LOGO:
			lpfc_issue_els_logo(phba, ndlp, cmdiocb->retry);
			return (1);
		}
	}

	/* No retry ELS command <elsCmd> to remote NPORT <did> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0108,
			lpfc_mes0108,
			lpfc_msgBlk0108.msgPreambleStr,
			cmd, ndlp->nlp_DID, cmdiocb->retry, ndlp->nlp_flag);

	return (0);
}

LPFC_IOCBQ_t *
lpfc_prep_els_iocb(lpfcHBA_t * phba,
		   uint8_t expectRsp,
		   uint16_t cmdSize,
		   uint8_t retry, LPFC_NODELIST_t * ndlp, uint32_t elscmd)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *elsiocb;
	DMABUF_t *pCmd, *pRsp, *pBufList;
	ULP_BDE64 *bpl;
	IOCB_t *icmd;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	if (phba->hba_state < LPFC_LINK_UP) {
		return (0);
	}

	/* Allocate buffer for  command iocb */
	if ((elsiocb = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		return (0);
	}
	memset(elsiocb, 0, sizeof (LPFC_IOCBQ_t));
	icmd = &elsiocb->iocb;

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if (((pCmd = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((pCmd->virt = lpfc_mbuf_alloc(phba,
					   MEM_PRI, &(pCmd->phys))) == 0)) {
		if (pCmd)
			kfree(pCmd);
		lpfc_iocb_free(phba, elsiocb);
		return (0);
	}

	INIT_LIST_HEAD(&pCmd->list);

	/* Allocate buffer for response payload */
	if (expectRsp) {
		if (((pRsp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
		    ((pRsp->virt = lpfc_mbuf_alloc(phba,
						   MEM_PRI,
						   &(pRsp->phys))) == 0)) {
			if (pRsp)
				kfree(pRsp);
			lpfc_mbuf_free(phba, pCmd->virt, pCmd->phys);
			kfree(pCmd);
			lpfc_iocb_free(phba, elsiocb);
			return (0);
		}
		INIT_LIST_HEAD(&pRsp->list);
	} else {
		pRsp = 0;
	}

	/* Allocate buffer for Buffer ptr list */
	if (((pBufList = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((pBufList->virt = lpfc_mbuf_alloc(phba,
					       MEM_PRI,
					       &(pBufList->phys))) == 0)) {
		lpfc_iocb_free(phba, elsiocb);
		lpfc_mbuf_free(phba, pCmd->virt, pCmd->phys);
		lpfc_mbuf_free(phba, pRsp->virt, pRsp->phys);
		kfree(pCmd);
		kfree(pRsp);
		if (pBufList)
			kfree(pBufList);
		return (0);
	}

	INIT_LIST_HEAD(&pBufList->list);

	icmd->un.elsreq64.bdl.addrHigh = putPaddrHigh(pBufList->phys);
	icmd->un.elsreq64.bdl.addrLow = putPaddrLow(pBufList->phys);
	icmd->un.elsreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	if (expectRsp) {
		icmd->un.elsreq64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
		icmd->un.elsreq64.remoteID = ndlp->nlp_DID;	/* DID */
		icmd->ulpCommand = CMD_ELS_REQUEST64_CR;
	} else {
		icmd->un.elsreq64.bdl.bdeSize = sizeof (ULP_BDE64);
		icmd->ulpCommand = CMD_XMIT_ELS_RSP64_CX;
	}

	/* NOTE: we don't use ulpIoTag0 because it is a t2 structure */
	icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
	icmd->un.elsreq64.bdl.ulpIoTag32 = (uint32_t) icmd->ulpIoTag;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;

	bpl = (ULP_BDE64 *) pBufList->virt;
	bpl->addrLow = le32_to_cpu(putPaddrLow(pCmd->phys));
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pCmd->phys));
	bpl->tus.f.bdeSize = cmdSize;
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	if (expectRsp) {
		bpl++;
		bpl->addrLow = le32_to_cpu(putPaddrLow(pRsp->phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(pRsp->phys));
		bpl->tus.f.bdeSize = FCELSSIZE;
		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
	}

	/* Save for completion so we can release these resources */
	elsiocb->context1 = (uint8_t *) ndlp;
	elsiocb->context2 = (uint8_t *) pCmd;
	elsiocb->context3 = (uint8_t *) pBufList;
	elsiocb->retry = retry;
	elsiocb->drvrTimeout = (phba->fc_ratov << 1) + LPFC_DRVR_TIMEOUT;

	if (pRsp) {
		list_add(&pRsp->list, &pCmd->list);
	}

	if (expectRsp) {
		/* Xmit ELS command <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0116, lpfc_mes0116,
				lpfc_msgBlk0116.msgPreambleStr, elscmd,
				ndlp->nlp_DID, icmd->ulpIoTag, phba->hba_state);
	} else {
		/* Xmit ELS response <elsCmd> to remote NPORT <did> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0117, lpfc_mes0117,
				lpfc_msgBlk0117.msgPreambleStr, elscmd,
				ndlp->nlp_DID, icmd->ulpIoTag, cmdSize);
	}

	return (elsiocb);
}

int
lpfc_els_free_iocb(lpfcHBA_t * phba, LPFC_IOCBQ_t * elsiocb)
{
	DMABUF_t *buf_ptr, *buf_ptr1;

	/* context2  = cmd,  context2->next = rsp, context3 = bpl */
	if (elsiocb->context2) {
		buf_ptr1 = (DMABUF_t *) elsiocb->context2;
		/* Free the response before processing the command.  */
		if (!list_empty(&buf_ptr1->list)) {
			buf_ptr = list_entry(buf_ptr1->list.next,
					     DMABUF_t, list);
			lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
			kfree(buf_ptr);
		}
		lpfc_mbuf_free(phba, buf_ptr1->virt, buf_ptr1->phys);
		kfree(buf_ptr1);
	}

	if (elsiocb->context3) {
		buf_ptr = (DMABUF_t *) elsiocb->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
	}

	lpfc_iocb_free(phba, elsiocb);
	return 0;
}

void
lpfc_cmpl_els_logo_acc(lpfcHBA_t * phba,
		       LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;
	int delay;

	clp = &phba->config[0];
	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;

	/* ACC to LOGO completes to NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0109,
			lpfc_mes0109,
			lpfc_msgBlk0109.msgPreambleStr,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	delay = 1;
	switch (ndlp->nlp_state) {
	case NLP_STE_UNUSED_NODE:	/* node is just allocated */
	case NLP_STE_PLOGI_ISSUE:	/* PLOGI was sent to NL_PORT */
	case NLP_STE_REG_LOGIN_ISSUE:	/* REG_LOGIN was issued for NL_PORT */
		break;
	case NLP_STE_PRLI_ISSUE:	/* PRLI was sent to NL_PORT */
		/* dequeue, cancel timeout, unreg login */
		lpfc_freenode(phba, ndlp);

		/* put back on plogi list and send a new plogi */
		lpfc_nlp_plogi(phba, ndlp);
		ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
		lpfc_issue_els_plogi(phba, ndlp, 0);
		break;

	case NLP_STE_PRLI_COMPL:	/* PRLI completed from NL_PORT */
		delay = 0;
		if((ndlp->nlp_DID & Fabric_DID_MASK) == Fabric_DID_MASK) {
			lpfc_nlp_remove(phba, ndlp);
			ndlp = 0;
			break;
		}

	case NLP_STE_MAPPED_NODE:	/* Identified as a FCP Target */

		lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
				  LPFC_SET_BITMASK);
		if (ndlp->nlp_flag & NLP_ADISC_SND) {
			/* dequeue, cancel timeout, unreg login */
			lpfc_freenode(phba, ndlp);

			/* put back on plogi list and send a new plogi */
			lpfc_nlp_plogi(phba, ndlp);
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
		} else {
			/* dequeue, cancel timeout, unreg login */
			lpfc_freenode(phba, ndlp);

			if (ndlp->nlp_tmofunc.function) {
				ndlp->nlp_flag &= ~NLP_DELAY_TMO;
				lpfc_stop_timer((struct clk_data *)
						ndlp->nlp_tmofunc.data);
			}
			lpfc_nlp_plogi(phba, ndlp);
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			ndlp->nlp_flag |= NLP_DELAY_TMO;
			ndlp->nlp_retry = 0;

			lpfc_start_timer(phba, delay, &ndlp->nlp_tmofunc,
					 lpfc_els_retry_delay,
					 (unsigned long)ndlp->nlp_DID,
					 (unsigned long)ELS_CMD_PLOGI);

		}
		break;
	}
	if(ndlp)
		ndlp->nlp_flag &= ~NLP_LOGO_ACC;
	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

void
lpfc_cmpl_els_acc(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		  LPFC_IOCBQ_t * rspiocb)
{
	LPFC_NODELIST_t *ndlp;
	LPFC_MBOXQ_t *mbox = 0;

	ndlp = (LPFC_NODELIST_t *) cmdiocb->context1;
	if (!ndlp) {

		/* Check to see if link went down during discovery */
		lpfc_els_chk_latt(phba, rspiocb);

		lpfc_els_free_iocb(phba, cmdiocb);
		return;
	}

	/* ELS response tag <ulpIoTag> completes */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0110,
			lpfc_mes0110,
			lpfc_msgBlk0110.msgPreambleStr,
			cmdiocb->iocb.ulpIoTag, rspiocb->iocb.ulpStatus,
			rspiocb->iocb.un.ulpWord[4], ndlp->nlp_DID,
			ndlp->nlp_flag, ndlp->nlp_state, ndlp->nlp_rpi);

	if (cmdiocb->context_un.mbox)
		mbox = cmdiocb->context_un.mbox;

	if (mbox) {
		if ((rspiocb->iocb.ulpStatus == 0)
		    && (ndlp->nlp_flag & NLP_ACC_REGLOGIN)) {
			/* set_slim mailbox command needs to execute first,
			 * queue this command to be processed later.
			 */
			mbox->mbox_cmpl = lpfc_mbx_cmpl_reg_login;
			mbox->context2 = ndlp;
			ndlp->nlp_state = NLP_STE_REG_LOGIN_ISSUE;
			if (lpfc_sli_issue_mbox(phba, mbox,
						(MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				goto out;
			}
			else {
				/* NOTE: we should have messages for unsuccessful
				   reglogin */
				DMABUF_t *mp;
				mp = (DMABUF_t *)(mbox->context1);
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
				lpfc_mbox_free(phba, mbox);
			}
		} else {
			lpfc_mbox_free(phba, mbox);
		}
	}

      out:
	ndlp->nlp_flag &= ~NLP_ACC_REGLOGIN;

	/* Check to see if link went down during discovery */
	lpfc_els_chk_latt(phba, rspiocb);

	lpfc_els_free_iocb(phba, cmdiocb);
	return;
}

int
lpfc_els_rsp_acc(lpfcHBA_t * phba,
		 uint32_t flag,
		 LPFC_IOCBQ_t * oldiocb,
		 LPFC_NODELIST_t * ndlp, LPFC_MBOXQ_t * mbox, uint8_t newnode)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	oldcmd = &oldiocb->iocb;

	switch (flag) {
	case ELS_CMD_ACC:
		cmdsize = sizeof (uint32_t);
		if ((elsiocb =
		     lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					ndlp, ELS_CMD_ACC)) == 0) {
			return (1);
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
		*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
		pCmd += sizeof (uint32_t);
		break;
	case ELS_CMD_PLOGI:
		cmdsize = (sizeof (SERV_PARM) + sizeof (uint32_t));
		if ((elsiocb =
		     lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					ndlp, ELS_CMD_ACC)) == 0) {
			return (1);
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
		pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

		if (mbox)
			elsiocb->context_un.mbox = mbox;

		*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
		pCmd += sizeof (uint32_t);
		memcpy(pCmd, &phba->fc_sparam, sizeof (SERV_PARM));
		break;
	case ELS_CMD_PRLO:
		cmdsize = (sizeof (uint32_t) + sizeof (PRLO));
		if ((elsiocb =
			lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
				ndlp, ELS_CMD_PRLO)) == 0) {
				return(1);
		}
		icmd = &elsiocb->iocb;
		icmd->ulpContext = oldcmd->ulpContext;
		pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);
		memcpy(pCmd, ((DMABUF_t *) oldiocb->context2)->virt, 
			sizeof (uint32_t) + sizeof (PRLO));
		*((uint32_t *) (pCmd)) = ELS_CMD_PRLO_ACC;
		break;
	default:
		return (1);
	}

	if (newnode)
		elsiocb->context1 = 0;

	if (ndlp->nlp_flag & NLP_LOGO_ACC) {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_logo_acc;
	} else {
		elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;
	}

	phba->fc_stat.elsXmitACC++;
	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_reject(lpfcHBA_t * phba, uint32_t rejectError,
		    LPFC_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = 2 * sizeof (uint32_t);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_LS_RJT)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_LS_RJT;
	pCmd += sizeof (uint32_t);
	*((uint32_t *) (pCmd)) = rejectError;

	phba->fc_stat.elsXmitLSRJT++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_adisc_acc(lpfcHBA_t * phba,
		       LPFC_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	ADISC *ap;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = sizeof (uint32_t) + sizeof (ADISC);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_ACC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
	pCmd += sizeof (uint32_t);

	ap = (ADISC *) (pCmd);
	ap->hardAL_PA = phba->fc_pref_ALPA;
	memcpy(&ap->portName, &phba->fc_portname, sizeof (NAME_TYPE));
	memcpy(&ap->nodeName, &phba->fc_nodename, sizeof (NAME_TYPE));
	ap->DID = be32_to_cpu(phba->fc_myDID);

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_prli_acc(lpfcHBA_t * phba,
		      LPFC_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	PRLI *npr;
	lpfc_vpd_t *vpd;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */

	cmdsize = sizeof (uint32_t) + sizeof (PRLI);
	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp,
					  (ELS_CMD_ACC |
					   (ELS_CMD_PRLI & ~ELS_RSP_MASK)))) ==
	    0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = (ELS_CMD_ACC | (ELS_CMD_PRLI & ~ELS_RSP_MASK));
	pCmd += sizeof (uint32_t);

	/* For PRLI, remainder of payload is PRLI parameter page */
	memset(pCmd, 0, sizeof (PRLI));

	npr = (PRLI *) pCmd;
	vpd = &phba->vpd;
	/*
	 * If our firmware version is 3.20 or later, 
	 * set the following bits for FC-TAPE support.
	 */
	if (vpd->rev.feaLevelHigh >= 0x02) {
		npr->ConfmComplAllowed = 1;
		npr->Retry = 1;
		npr->TaskRetryIdReq = 1;
	}

	npr->acceptRspCode = PRLI_REQ_EXECUTED;
	npr->estabImagePair = 1;
	npr->readXferRdyDis = 1;
	npr->ConfmComplAllowed = 1;

	npr->prliType = PRLI_FCP_TYPE;
	npr->initiatorFunc = 1;

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

int
lpfc_els_rsp_rnid_acc(lpfcHBA_t * phba,
		      uint8_t format,
		      LPFC_IOCBQ_t * oldiocb, LPFC_NODELIST_t * ndlp)
{
	RNID *rn;
	IOCB_t *icmd;
	IOCB_t *oldcmd;
	LPFC_IOCBQ_t *elsiocb;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	uint8_t *pCmd;
	uint16_t cmdsize;
	lpfcCfgParam_t *clp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	clp = &phba->config[0];

	cmdsize =
	    sizeof (uint32_t) + sizeof (uint32_t) + (2 * sizeof (NAME_TYPE));
	if (format)
		cmdsize += sizeof (RNID_TOP_DISC);

	if ((elsiocb = lpfc_prep_els_iocb(phba, FALSE, cmdsize, oldiocb->retry,
					  ndlp, ELS_CMD_ACC)) == 0) {
		return (1);
	}

	icmd = &elsiocb->iocb;
	oldcmd = &oldiocb->iocb;
	icmd->ulpContext = oldcmd->ulpContext;	/* Xri */
	pCmd = (uint8_t *) (((DMABUF_t *) elsiocb->context2)->virt);

	*((uint32_t *) (pCmd)) = ELS_CMD_ACC;
	pCmd += sizeof (uint32_t);

	memset(pCmd, 0, sizeof (RNID));
	rn = (RNID *) (pCmd);
	rn->Format = format;
	rn->CommonLen = (2 * sizeof (NAME_TYPE));
	memcpy(&rn->portName, &phba->fc_portname, sizeof (NAME_TYPE));
	memcpy(&rn->nodeName, &phba->fc_nodename, sizeof (NAME_TYPE));
	switch (format) {
	case 0:
		rn->SpecificLen = 0;
		break;
	case RNID_TOPOLOGY_DISC:
		rn->SpecificLen = sizeof (RNID_TOP_DISC);
		memcpy(&rn->un.topologyDisc.portName,
		       &phba->fc_portname, sizeof (NAME_TYPE));
		rn->un.topologyDisc.unitType = RNID_HBA;
		rn->un.topologyDisc.physPort = 0;
		rn->un.topologyDisc.attachedNodes = 0;
		break;
	default:
		rn->CommonLen = 0;
		rn->SpecificLen = 0;
		break;
	}

	phba->fc_stat.elsXmitACC++;
	elsiocb->iocb_cmpl = lpfc_cmpl_els_acc;

	if (lpfc_sli_issue_iocb(phba, pring, elsiocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return (1);
	}
	return (0);
}

void
lpfc_els_unsol_event(lpfcHBA_t * phba,
		     LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * elsiocb)
{
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	DMABUF_t *mp;
	uint32_t *lp;
	IOCB_t *icmd;
	LS_RJT stat;
	uint32_t cmd;
	uint32_t did;
	uint32_t newnode;
	uint32_t drop_cmd = 0;	/* by default do NOT drop received cmd */

	psli = &phba->sli;
	icmd = &elsiocb->iocb;

	/* type of ELS cmd is first 32bit word in packet */
	mp = lpfc_sli_ringpostbuf_get(phba, pring, getPaddr(icmd->un.
							    cont64[0].
							    addrHigh,
							    icmd->un.
							    cont64[0].addrLow));
	if (mp == 0) {
		drop_cmd = 1;
		goto dropit;
	}

	newnode = 0;
	lp = (uint32_t *) mp->virt;
	cmd = *lp++;
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], 1, 1);

	if (icmd->ulpStatus) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		drop_cmd = 1;
		goto dropit;
	}

	/* Check to see if link went down during discovery */
	if (lpfc_els_chk_latt(phba, elsiocb)) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		drop_cmd = 1;
		goto dropit;
	}

	did = icmd->un.rcvels.remoteID;
	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did)) == 0) {
		/* Cannot find existing Fabric ndlp, so allocate a new one */
		if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
			drop_cmd = 1;
			goto dropit;
		}

		newnode = 1;
		memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = did;
		if ((did & Fabric_DID_MASK) == Fabric_DID_MASK) {
			ndlp->nlp_type |= NLP_FABRIC;
		}
	}

	phba->fc_stat.elsRcvFrame++;
	elsiocb->context1 = ndlp;
	elsiocb->context2 = mp;

	if ((cmd & ELS_CMD_MASK) == ELS_CMD_RSCN) {
		cmd &= ELS_CMD_MASK;
	}
	/* ELS command <elsCmd> received from NPORT <did> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0112,
			lpfc_mes0112,
			lpfc_msgBlk0112.msgPreambleStr,
			cmd, did, phba->hba_state);

	switch (cmd) {
	case ELS_CMD_PLOGI:
		phba->fc_stat.elsRcvPLOGI++;
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PLOGI);
		break;
	case ELS_CMD_FLOGI:
		phba->fc_stat.elsRcvFLOGI++;
		lpfc_els_rcv_flogi(phba, elsiocb, ndlp, newnode);
		if (newnode) {
			lpfc_nlp_free(phba, ndlp);
		}
		break;
	case ELS_CMD_LOGO:
		phba->fc_stat.elsRcvLOGO++;
		if(ndlp->nlp_Target)
			lpfc_set_npr_tmo(phba, ndlp->nlp_Target, ndlp);
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_LOGO);
		break;
	case ELS_CMD_PRLO:
		phba->fc_stat.elsRcvPRLO++;
		if (ndlp->nlp_Target)
			lpfc_set_npr_tmo(phba, ndlp->nlp_Target, ndlp);
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PRLO);
		break;
	case ELS_CMD_RSCN:
		phba->fc_stat.elsRcvRSCN++;
		lpfc_els_rcv_rscn(phba, elsiocb, ndlp, newnode);
		if (newnode) {
			lpfc_nlp_free(phba, ndlp);
		}
		break;
	case ELS_CMD_ADISC:
		phba->fc_stat.elsRcvADISC++;
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_ADISC);
		break;
	case ELS_CMD_PDISC:
		phba->fc_stat.elsRcvPDISC++;
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PDISC);
		break;
	case ELS_CMD_FARPR:
		phba->fc_stat.elsRcvFARPR++;
		lpfc_els_rcv_farpr(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FARP:
		phba->fc_stat.elsRcvFARP++;
		lpfc_els_rcv_farp(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_FAN:
		phba->fc_stat.elsRcvFAN++;
		lpfc_els_rcv_fan(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_RRQ:
		phba->fc_stat.elsRcvRRQ++;
		lpfc_els_rcv_rrq(phba, elsiocb, ndlp);
		break;
	case ELS_CMD_PRLI:
		phba->fc_stat.elsRcvPRLI++;
		lpfc_disc_state_machine(phba, ndlp, elsiocb, NLP_EVT_RCV_PRLI);
		break;
	case ELS_CMD_RNID:
		phba->fc_stat.elsRcvRNID++;
		lpfc_els_rcv_rnid(phba, elsiocb, ndlp);
		break;
	default:
		/* Unsupported ELS command, reject */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_CMD_UNSUPPORTED;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_NOTHING_MORE;
		stat.un.b.vendorUnique = 0;

		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, elsiocb, ndlp);

		/* Unknown ELS command <elsCmd> received from NPORT <did> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0115,
				lpfc_mes0115,
				lpfc_msgBlk0115.msgPreambleStr, cmd, did);
		if (newnode) {
			lpfc_nlp_free(phba, ndlp);
		}
		break;
	}
	if (elsiocb->context2) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}

      dropit:
	/* check if need to drop received ELS cmd */
	if (drop_cmd == 1) {
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0111,
				lpfc_mes0111,
				lpfc_msgBlk0111.msgPreambleStr,
				icmd->ulpStatus, icmd->un.ulpWord[4]);
		phba->fc_stat.elsRcvDrop++;
	}

	return;
}

void
lpfc_more_adisc(lpfcHBA_t * phba)
{
	int sentadisc;
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos;

	if (phba->num_disc_nodes)
		phba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> ADISCs to go */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0210,
			lpfc_mes0210,
			lpfc_msgBlk0210.msgPreambleStr,
			phba->num_disc_nodes, phba->fc_adisc_cnt, phba->fc_flag,
			phba->hba_state);

	/* Check to see if there are more ADISCs to be sent */
	if (phba->fc_flag & FC_NLP_MORE) {
		sentadisc = 0;
		lpfc_set_disctmo(phba);

		/* go thru ADISC list and issue any remaining ELS ADISCs */
		list_for_each(pos, &phba->fc_adisc_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (!(ndlp->nlp_flag & NLP_ADISC_SND)) {
				/* If we haven't already sent an ADISC for this
				 * node, send it.
				 */
				lpfc_issue_els_adisc(phba, ndlp, 0);
				phba->num_disc_nodes++;
				ndlp->nlp_flag |= NLP_DISC_NODE;
				sentadisc = 1;
				break;
			}
		}

		if (sentadisc == 0) {
			phba->fc_flag &= ~FC_NLP_MORE;
		}
	}
	return;
}

void
lpfc_more_plogi(lpfcHBA_t * phba)
{
	int sentplogi;
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos;

	if (phba->num_disc_nodes)
		phba->num_disc_nodes--;

	/* Continue discovery with <num_disc_nodes> PLOGIs to go */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0232,
			lpfc_mes0232,
			lpfc_msgBlk0232.msgPreambleStr,
			phba->num_disc_nodes, phba->fc_plogi_cnt, phba->fc_flag,
			phba->hba_state);

	/* Check to see if there are more PLOGIs to be sent */
	if (phba->fc_flag & FC_NLP_MORE) {
		sentplogi = 0;

		/* go thru PLOGI list and issue any remaining ELS PLOGIs */
		list_for_each(pos, &phba->fc_plogi_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if ((!(ndlp->nlp_flag & NLP_PLOGI_SND)) &&
			    (ndlp->nlp_state == NLP_STE_UNUSED_NODE)) {
				/* If we haven't already sent an PLOGI for this
				 * node, send it.
				 */
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
				phba->num_disc_nodes++;
				ndlp->nlp_flag |= NLP_DISC_NODE;
				sentplogi = 1;
				break;
			}
		}
		if (sentplogi == 0) {
			phba->fc_flag &= ~FC_NLP_MORE;
		}
	}
	return;
}

int
lpfc_els_flush_rscn(lpfcHBA_t * phba)
{
	DMABUF_t *mp;
	int i;

	for (i = 0; i < phba->fc_rscn_id_cnt; i++) {
		mp = phba->fc_rscn_id_list[i];
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		phba->fc_rscn_id_list[i] = 0;
	}
	phba->fc_rscn_id_cnt = 0;
	phba->fc_flag &= ~(FC_RSCN_MODE | FC_RSCN_DISCOVERY);
	lpfc_can_disctmo(phba);
	return (0);
}

int
lpfc_rscn_payload_check(lpfcHBA_t * phba, uint32_t did)
{
	D_ID ns_did;
	D_ID rscn_did;
	DMABUF_t *mp;
	uint32_t *lp;
	uint32_t payload_len, cmd, i, match;

	ns_did.un.word = did;
	match = 0;

	/* If we are doing a FULL RSCN rediscovery, match everything */
	if (phba->fc_flag & FC_RSCN_DISCOVERY) {
		return (did);
	}

	for (i = 0; i < phba->fc_rscn_id_cnt; i++) {
		mp = phba->fc_rscn_id_list[i];
		lp = (uint32_t *) mp->virt;
		cmd = *lp++;
		payload_len = be32_to_cpu(cmd) & 0xffff; /* payload length */
		payload_len -= sizeof (uint32_t);	/* take off word 0 */
		while (payload_len) {
			rscn_did.un.word = *lp++;
			rscn_did.un.word = be32_to_cpu(rscn_did.un.word);
			payload_len -= sizeof (uint32_t);
			switch (rscn_did.un.b.resv) {
			case 0:	/* Single N_Port ID effected */
				if (ns_did.un.word == rscn_did.un.word) {
					match = did;
				}
				break;
			case 1:	/* Whole N_Port Area effected */
				if ((ns_did.un.b.domain == rscn_did.un.b.domain)
				    && (ns_did.un.b.area == rscn_did.un.b.area))
					{
						match = did;
					}
				break;
			case 2:	/* Whole N_Port Domain effected */
				if (ns_did.un.b.domain == rscn_did.un.b.domain)
					{
						match = did;
					}
				break;
			case 3:	/* Whole Fabric effected */
				match = did;
				break;
			default:
				/* Unknown Identifier in RSCN list */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0217,
						lpfc_mes0217,
						lpfc_msgBlk0217.msgPreambleStr,
						rscn_did.un.word);
				break;
			}
			if (match) {
				break;
			}
		}
	}
	return (match);
}


static int
lpfc_rscn_recovery_check(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp = NULL, *new_ndlp;
	struct list_head *pos, *next, *listp;
	struct list_head *node_list[4];
	LPFCSCSITARGET_t *targetp;
	int i;

	/* Make all effected nodes LPFC_DEV_DISCONNECTED */
	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_nlpmap_list;
	for (i = 0; i < 4; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each_safe(pos, next, listp) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;

			if((lpfc_rscn_payload_check(phba, ndlp->nlp_DID))) {
				lpfc_set_failmask(phba, ndlp,
						  LPFC_DEV_DISCONNECTED,
						  LPFC_SET_BITMASK);

				targetp = ndlp->nlp_Target;
				if(targetp)
					lpfc_set_npr_tmo(phba, targetp, ndlp);
			}
		}
	}
	return (0);
}

int
lpfc_els_rcv_rscn(lpfcHBA_t * phba,
		  LPFC_IOCBQ_t * cmdiocb,
		  LPFC_NODELIST_t * ndlp, uint8_t newnode)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t payload_len, cmd;

	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	payload_len = be32_to_cpu(cmd) & 0xffff;	/* payload length */
	payload_len -= sizeof (uint32_t);	/* take off word 0 */
	cmd &= ELS_CMD_MASK;

	/* RSCN received */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0214,
			lpfc_mes0214,
			lpfc_msgBlk0214.msgPreambleStr,
			phba->fc_flag, payload_len, *lp, phba->fc_rscn_id_cnt);

	/* if we are already processing an RSCN, save the received
	 * RSCN payload buffer, cmdiocb->context2 to process later.
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	if (phba->fc_flag & FC_RSCN_MODE) {
		if ((phba->fc_rscn_id_cnt < FC_MAX_HOLD_RSCN) &&
		    !(phba->fc_flag & FC_RSCN_DISCOVERY)) {
			phba->fc_rscn_id_list[phba->fc_rscn_id_cnt++] = pCmd;
			cmdiocb->context2 = 0;
			/* Deferred RSCN */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0235,
					lpfc_mes0235,
					lpfc_msgBlk0235.msgPreambleStr,
					phba->fc_rscn_id_cnt, phba->fc_flag,
					phba->hba_state);
		} else {
			phba->fc_flag |= FC_RSCN_DISCOVERY;
			/* ReDiscovery RSCN */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0234,
					lpfc_mes0234,
					lpfc_msgBlk0234.msgPreambleStr,
					phba->fc_rscn_id_cnt, phba->fc_flag,
					phba->hba_state);
		}
		/* Send back ACC */
		lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, newnode);
		lpfc_rscn_recovery_check(phba);
		return (0);
	}

	phba->fc_flag |= FC_RSCN_MODE;
	phba->fc_rscn_id_list[phba->fc_rscn_id_cnt++] = pCmd;
	/*
	 * If we zero, cmdiocb->context2, the calling routine will
	 * not try to free it.
	 */
	cmdiocb->context2 = 0;

	lpfc_set_disctmo(phba);

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, newnode);
	lpfc_rscn_recovery_check(phba);

	return (lpfc_els_handle_rscn(phba));
}

int
lpfc_els_handle_rscn(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp;

	lpfc_hba_put_event(phba, HBA_EVENT_RSCN, phba->fc_myDID,
			  phba->fc_myDID, 0, 0);
	lpfc_put_event(phba, FC_REG_RSCN_EVENT, phba->fc_myDID, 0, 0);

	/* RSCN processed */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0215,
			lpfc_mes0215,
			lpfc_msgBlk0215.msgPreambleStr,
			phba->fc_flag, 0, phba->fc_rscn_id_cnt,
			phba->hba_state);

	/* To process RSCN, first compare RSCN data with NameServer */
	phba->fc_ns_retry = 0;
	if ((ndlp = lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
				      NameServer_DID))) {
		/* Good ndlp, issue CT Request to NameServer */
		if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) == 0) {
			/* Wait for NameServer query cmpl before we can
			   continue */
			return (1);
		}
	} else {
		/* If login to NameServer does not exist, issue one */
		/* Good status, issue PLOGI to NameServer */
		if ((ndlp =
		     lpfc_findnode_did(phba, NLP_SEARCH_ALL, NameServer_DID))) {
			/* Wait for NameServer login cmpl before we can
			   continue */
			return (1);
		}
		if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
			lpfc_els_flush_rscn(phba);
			return (0);
		} else {
			memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_type |= NLP_FABRIC;
			ndlp->nlp_DID = NameServer_DID;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
			/* Wait for NameServer login cmpl before we can
			   continue */
			return (1);
		}
	}

	lpfc_els_flush_rscn(phba);
	return (0);
}

int
lpfc_els_rcv_flogi(lpfcHBA_t * phba,
		   LPFC_IOCBQ_t * cmdiocb,
		   LPFC_NODELIST_t * ndlp, uint8_t newnode)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	SERV_PARM *sp;
	LPFC_MBOXQ_t *mbox;
	LPFC_SLI_t *psli;
	lpfcCfgParam_t *clp;
	LS_RJT stat;
	uint32_t cmd, did;

	psli = &phba->sli;
	clp = &phba->config[0];
	icmd = &cmdiocb->iocb;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	sp = (SERV_PARM *) lp;

	/* FLOGI received */

	lpfc_set_disctmo(phba);

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		/* We should never receive a FLOGI in loop mode, ignore it */
		did = icmd->un.elsreq64.remoteID;

		/* An FLOGI ELS command <elsCmd> was received from DID <did> in
		   Loop Mode */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0113,
				lpfc_mes0113,
				lpfc_msgBlk0113.msgPreambleStr, cmd, did);
		return (1);
	}

	did = Fabric_DID;

	if ((lpfc_check_sparm(phba, ndlp, sp, CLASS3))) {
		/* For a FLOGI we accept, then if our portname is greater
		 * then the remote portname we initiate Nport login. 
		 */
		int rc;

		rc = lpfc_geportname((NAME_TYPE *) & phba->fc_portname,
				     (NAME_TYPE *) & sp->portName);

		if (rc == 2) {
			if ((mbox = lpfc_mbox_alloc(phba, 0)) == 0) {
				return (1);
			}
			lpfc_linkdown(phba);
			lpfc_init_link(phba, mbox,
				       clp[LPFC_CFG_TOPOLOGY].a_current,
				       clp[LPFC_CFG_LINK_SPEED].a_current);
			mbox->mb.un.varInitLnk.lipsr_AL_PA = 0;
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
			}
			return (1);
		}

		if (rc == 1) {	/* greater than */
			phba->fc_flag |= FC_PT2PT_PLOGI;
		}
		phba->fc_flag |= FC_PT2PT;
		phba->fc_flag &= ~(FC_FABRIC | FC_PUBLIC_LOOP);
	} else {
		/* Reject this request because invalid parameters */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_SPARM_OPTIONS;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
		return (1);
	}

	/* Send back ACC */
	lpfc_els_rsp_acc(phba, ELS_CMD_PLOGI, cmdiocb, ndlp, 0, newnode);

	return (0);
}

int
lpfc_els_rcv_rnid(lpfcHBA_t * phba,
		  LPFC_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	RNID *rn;
	LS_RJT stat;
	uint32_t cmd, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	rn = (RNID *) lp;

	/* RNID received */

	switch (rn->Format) {
	case 0:
	case RNID_TOPOLOGY_DISC:
		/* Send back ACC */
		lpfc_els_rsp_rnid_acc(phba, rn->Format, cmdiocb, ndlp);
		break;
	default:
		/* Reject this request because format not supported */
		stat.un.b.lsRjtRsvd0 = 0;
		stat.un.b.lsRjtRsnCode = LSRJT_UNABLE_TPC;
		stat.un.b.lsRjtRsnCodeExp = LSEXP_CANT_GIVE_DATA;
		stat.un.b.vendorUnique = 0;
		lpfc_els_rsp_reject(phba, stat.un.lsRjtError, cmdiocb, ndlp);
	}
	return (0);
}

int
lpfc_els_rcv_rrq(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		 LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	RRQ *rrq;
	uint32_t cmd, did;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_FCP_RING];
	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	rrq = (RRQ *) lp;

	/* RRQ received */
	/* Get oxid / rxid from payload and abort it */
	if ((rrq->SID == be32_to_cpu(phba->fc_myDID))) {
		lpfc_sli_abort_iocb_ctx(phba, pring, rrq->Oxid);
	} else {
		lpfc_sli_abort_iocb_ctx(phba, pring, rrq->Rxid);
	}
	/* ACCEPT the rrq request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);

	return (0);
}

int
lpfc_els_rcv_farp(lpfcHBA_t * phba,
		  LPFC_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	FARP *fp;
	uint32_t cmd, cnt, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	fp = (FARP *) lp;

	/* FARP-REQ received from DID <did> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0601,
			lpfc_mes0601, lpfc_msgBlk0601.msgPreambleStr, did);

	/* We will only support match on WWPN or WWNN */
	if (fp->Mflags & ~(FARP_MATCH_NODE | FARP_MATCH_PORT)) {
		return (0);
	}

	cnt = 0;
	/* If this FARP command is searching for my portname */
	if (fp->Mflags & FARP_MATCH_PORT) {
		if (lpfc_geportname(&fp->RportName, &phba->fc_portname) == 2)
			cnt = 1;
	}

	/* If this FARP command is searching for my nodename */
	if (fp->Mflags & FARP_MATCH_NODE) {
		if (lpfc_geportname(&fp->RnodeName, &phba->fc_nodename) == 2)
			cnt = 1;
	}

	if (cnt) {
		if ((ndlp->nlp_failMask == 0) &&
		    (!(ndlp->nlp_flag & NLP_ELS_SND_MASK))) {
			/* Log back into the node before sending the FARP. */
			if (fp->Rflags & FARP_REQUEST_PLOGI) {
				lpfc_nlp_plogi(phba, ndlp);
				ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_issue_els_plogi(phba, ndlp, 0);
			}

			/* Send a FARP response to that node */
			if (fp->Rflags & FARP_REQUEST_FARPR) {
				lpfc_issue_els_farpr(phba, did, 0);
			}
		}
	}
	return (0);
}

int
lpfc_els_rcv_farpr(lpfcHBA_t * phba,
		   LPFC_IOCBQ_t * cmdiocb, LPFC_NODELIST_t * ndlp)
{
	DMABUF_t *pCmd;
	uint32_t *lp;
	IOCB_t *icmd;
	uint32_t cmd, did;

	icmd = &cmdiocb->iocb;
	did = icmd->un.elsreq64.remoteID;
	pCmd = (DMABUF_t *) cmdiocb->context2;
	lp = (uint32_t *) pCmd->virt;

	cmd = *lp++;
	/* FARP-RSP received from DID <did> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0600,
			lpfc_mes0600, lpfc_msgBlk0600.msgPreambleStr, did);

	/* ACCEPT the Farp resp request */
	lpfc_els_rsp_acc(phba, ELS_CMD_ACC, cmdiocb, ndlp, 0, 0);

	return (0);
}

int
lpfc_els_rcv_fan(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		 LPFC_NODELIST_t * ndlp)
{
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0261,
			lpfc_mes0261,
			lpfc_msgBlk0261.msgPreambleStr);
	return (0);
}

int
lpfc_els_chk_latt(lpfcHBA_t * phba, LPFC_IOCBQ_t * rspiocb)
{
	LPFC_SLI_t *psli;
	IOCB_t *irsp;
	LPFC_MBOXQ_t *mbox;
	uint32_t ha_copy;

	psli = &phba->sli;

	if ((phba->hba_state < LPFC_HBA_READY) &&
		(phba->hba_state != LPFC_LINK_DOWN)) {
		uint32_t tag, stat, wd4;

		/* Read the HBA Host Attention Register */
		ha_copy = readl(phba->HAregaddr);

		if (ha_copy & HA_LATT) {	/* Link Attention interrupt */
			if (rspiocb) {
				irsp = &(rspiocb->iocb);
				tag = irsp->ulpIoTag;
				stat = irsp->ulpStatus;
				wd4 = irsp->un.ulpWord[4];
				irsp->ulpStatus = IOSTAT_LOCAL_REJECT;
				irsp->un.ulpWord[4] = IOERR_SLI_ABORTED;
			} else {
				tag = 0;
				stat = 0;
				wd4 = 0;
			}
			/* Pending Link Event during Discovery */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0237,
					lpfc_mes0237,
					lpfc_msgBlk0237.msgPreambleStr,
					phba->hba_state, tag, stat, wd4);

			lpfc_linkdown(phba);

			if (phba->hba_state != LPFC_CLEAR_LA) {
				if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
					phba->hba_state = LPFC_CLEAR_LA;
					lpfc_clear_la(phba, mbox);
					mbox->mbox_cmpl =
					    lpfc_mbx_cmpl_clear_la;
					if (lpfc_sli_issue_mbox
					    (phba, mbox,
					     (MBX_NOWAIT | MBX_STOP_IOCB))
					    == MBX_NOT_FINISHED) {
						lpfc_mbox_free(phba, mbox);
						phba->hba_state =
						    LPFC_HBA_ERROR;
					}
				}
			}
			return (1);
		}
	}

	return (0);
}

void
lpfc_els_timeout_handler(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *next_iocb;
	LPFC_IOCBQ_t *piocb;
	IOCB_t *cmd = 0;
	DMABUF_t *pCmd;
	struct list_head *dlp;
	uint32_t *elscmd;
	uint32_t els_command;
	uint32_t timeout;
	uint32_t next_timeout;
	uint32_t remote_ID;
	unsigned long iflag;
	struct clk_data *elsClkData;
	struct list_head *curr, *next;

	elsClkData = (struct clk_data *)ptr;
	phba = elsClkData->phba;
	LPFC_DRVR_LOCK(phba, iflag);
       	if (elsClkData->flags & TM_CANCELED) {
		list_del((struct list_head *)elsClkData);
		kfree(elsClkData);	
		goto out;
	}

	timeout = (uint32_t) (unsigned long)(elsClkData->clData1);
	phba->els_tmofunc.function = 0;
	list_del((struct list_head *)elsClkData);
	kfree(elsClkData);

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	dlp = &pring->txcmplq;
	next_timeout = phba->fc_ratov << 1;

	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		piocb = next_iocb;
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}
		pCmd = (DMABUF_t *) piocb->context2;
		elscmd = (uint32_t *) (pCmd->virt);
		els_command = *elscmd;

		if ((els_command == ELS_CMD_FARP)
		    || (els_command == ELS_CMD_FARPR)) {
			continue;
		}

		if (piocb->drvrTimeout > 0) {
			if (piocb->drvrTimeout >= timeout) {
				piocb->drvrTimeout -= timeout;
			} else {
				piocb->drvrTimeout = 0;
			}
			continue;
		}

		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			LPFC_NODELIST_t *ndlp;

			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			remote_ID = ndlp->nlp_DID;
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0127, lpfc_mes0127,
				lpfc_msgBlk0127.msgPreambleStr, els_command,
				remote_ID, cmd->ulpCommand, cmd->ulpIoTag);

		/*
		 * The iocb has timed out; driver abort it.
		 */
		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		} else {
			lpfc_iocb_free(phba, piocb);
		}
	}

	lpfc_start_timer(phba, next_timeout, &phba->els_tmofunc,
			 lpfc_els_timeout_handler,
			 (unsigned long)next_timeout, (unsigned long)0);
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
}

void
lpfc_els_flush_cmd(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *next_iocb;
	LPFC_IOCBQ_t *piocb;
	IOCB_t *cmd = 0;
	DMABUF_t *pCmd;
	uint32_t *elscmd;
	uint32_t els_command;
	uint32_t remote_ID;
	struct list_head *curr, *next;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		piocb = next_iocb;
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}

		/* Do not flush out the QUE_RING and ABORT/CLOSE iocbs */
		if ((cmd->ulpCommand == CMD_QUE_RING_BUF_CN) ||
		    (cmd->ulpCommand == CMD_QUE_RING_BUF64_CN) ||
		    (cmd->ulpCommand == CMD_CLOSE_XRI_CN) ||
		    (cmd->ulpCommand == CMD_ABORT_XRI_CN)) {
			continue;
		}

		pCmd = (DMABUF_t *) piocb->context2;
		elscmd = (uint32_t *) (pCmd->virt);
		els_command = *elscmd;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			LPFC_NODELIST_t *ndlp;

			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			remote_ID = ndlp->nlp_DID;
			if (phba->hba_state == LPFC_HBA_READY) {
				continue;
			}
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		list_del(&piocb->list);
		pring->txq_cnt--;

		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		} else {
			lpfc_iocb_free(phba, piocb);
		}
	}

	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		piocb = next_iocb;
		cmd = &piocb->iocb;

		if (piocb->iocb_flag & LPFC_IO_LIBDFC) {
			continue;
		}
		pCmd = (DMABUF_t *) piocb->context2;
		elscmd = (uint32_t *) (pCmd->virt);
		els_command = *elscmd;

		if (cmd->ulpCommand == CMD_GEN_REQUEST64_CR) {
			LPFC_NODELIST_t *ndlp;

			ndlp = lpfc_findnode_rpi(phba, cmd->ulpContext);
			remote_ID = ndlp->nlp_DID;
			if (phba->hba_state == LPFC_HBA_READY) {
				continue;
			}
		} else {
			remote_ID = cmd->un.elsreq64.remoteID;
		}

		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;

		if (piocb->iocb_cmpl) {
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		} else {
			lpfc_iocb_free(phba, piocb);
		}
	}
	return;
}
