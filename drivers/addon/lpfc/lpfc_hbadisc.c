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
 * $Id: lpfc_hbadisc.c 369 2005-07-08 23:29:48Z sf_support $
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
#include "lpfc_fcp.h"
#include "lpfc_scsi.h"
#include "lpfc_cfgparm.h"
#include "hbaapi.h"
#include "lpfc_dfc.h"
#include "lpfc_crtn.h"

int lpfc_matchdid(lpfcHBA_t *, LPFC_NODELIST_t *, uint32_t);
void lpfc_free_tx(lpfcHBA_t *, LPFC_NODELIST_t *);
void lpfc_put_buf(unsigned long);
void lpfc_disc_retry_rptlun(unsigned long);

/* Could be put in lpfc.conf; For now defined here */
int lpfc_qfull_retry_count = 5;

/* AlpaArray for assignment of scsid for scan-down and bind_method */
uint8_t lpfcAlpaArray[] = {
	0xEF, 0xE8, 0xE4, 0xE2, 0xE1, 0xE0, 0xDC, 0xDA, 0xD9, 0xD6,
	0xD5, 0xD4, 0xD3, 0xD2, 0xD1, 0xCE, 0xCD, 0xCC, 0xCB, 0xCA,
	0xC9, 0xC7, 0xC6, 0xC5, 0xC3, 0xBC, 0xBA, 0xB9, 0xB6, 0xB5,
	0xB4, 0xB3, 0xB2, 0xB1, 0xAE, 0xAD, 0xAC, 0xAB, 0xAA, 0xA9,
	0xA7, 0xA6, 0xA5, 0xA3, 0x9F, 0x9E, 0x9D, 0x9B, 0x98, 0x97,
	0x90, 0x8F, 0x88, 0x84, 0x82, 0x81, 0x80, 0x7C, 0x7A, 0x79,
	0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x6E, 0x6D, 0x6C, 0x6B,
	0x6A, 0x69, 0x67, 0x66, 0x65, 0x63, 0x5C, 0x5A, 0x59, 0x56,
	0x55, 0x54, 0x53, 0x52, 0x51, 0x4E, 0x4D, 0x4C, 0x4B, 0x4A,
	0x49, 0x47, 0x46, 0x45, 0x43, 0x3C, 0x3A, 0x39, 0x36, 0x35,
	0x34, 0x33, 0x32, 0x31, 0x2E, 0x2D, 0x2C, 0x2B, 0x2A, 0x29,
	0x27, 0x26, 0x25, 0x23, 0x1F, 0x1E, 0x1D, 0x1B, 0x18, 0x17,
	0x10, 0x0F, 0x08, 0x04, 0x02, 0x01
};

int
lpfc_linkdown(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	struct list_head *pos, *next, *listp;
	struct list_head *node_list[4];
	LPFCSCSITARGET_t *targetp;
	LPFC_MBOXQ_t *mb;
	lpfcCfgParam_t *clp;
	int rc, i;

	clp = &phba->config[0];
	psli = &phba->sli;
	phba->hba_state = LPFC_LINK_DOWN;
	phba->fc_flag |= FC_LNK_DOWN;

	lpfc_put_event(phba, FC_REG_LINK_EVENT, 0, 0, 0);
	phba->nport_event_cnt++;

	lpfc_hba_put_event(phba, HBA_EVENT_LINK_DOWN, phba->fc_myDID, 0, 0, 0);

	/* Clean up any firmware default rpi's */
	if ((mb = lpfc_mbox_alloc(phba, 0))) {
		lpfc_unreg_did(phba, 0xffffffff, mb);
		if (lpfc_sli_issue_mbox(phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, mb);
		}
	}


	/* Cleanup any outstanding RSCN activity */
	lpfc_els_flush_rscn(phba);

	/* Cleanup any outstanding ELS commands */
	lpfc_els_flush_cmd(phba);

	/* Flush all the ELS completion in the tasklet queue */
	lpfc_flush_disc_evtq(phba);

	/* Handle linkdown timer logic.   */
	if (!(phba->fc_flag & FC_LD_TIMER)) {
		/* Should we start the link down watchdog timer */
		if ((clp[LPFC_CFG_LINKDOWN_TMO].a_current == 0) ||
		    clp[LPFC_CFG_HOLDIO].a_current) {
			phba->fc_flag |= (FC_LD_TIMER | FC_LD_TIMEOUT);
			phba->hba_flag |= FC_LFR_ACTIVE;
		} else {
			phba->fc_flag |= FC_LD_TIMER;
			phba->hba_flag |= FC_LFR_ACTIVE;
			if (phba->fc_linkdown.function) {
				unsigned long new_tmo;
				new_tmo = jiffies + HZ *
				    (clp[LPFC_CFG_LINKDOWN_TMO].a_current);
				mod_timer(&phba->fc_linkdown, new_tmo);
			} else {
				if (clp[LPFC_CFG_HOLDIO].a_current == 0) {
					lpfc_start_timer(phba,
						clp[LPFC_CFG_LINKDOWN_TMO]
							 .a_current,
						&phba->fc_linkdown,
						lpfc_linkdown_timeout, 0, 0);
				}
			}
		}
	}

	/* Issue a LINK DOWN event to all nodes */
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

			/* Fabric nodes are not handled thru state machine for
			   link down */
			if (!(ndlp->nlp_type & NLP_FABRIC)) {
				lpfc_set_failmask(phba, ndlp,
						  LPFC_DEV_LINK_DOWN,
						  LPFC_SET_BITMASK);
			}

			targetp = ndlp->nlp_Target;
			if(targetp)
				lpfc_set_npr_tmo(phba, targetp, ndlp);

			rc = lpfc_disc_state_machine(phba, ndlp, 0,
						     NLP_EVT_DEVICE_UNK);
		}
	}

	/* Setup myDID for link up if we are in pt2pt mode */
	if (phba->fc_flag & FC_PT2PT) {
		phba->fc_myDID = 0;
		if ((mb = lpfc_mbox_alloc(phba, 0))) {
			lpfc_config_link(phba, mb);
			if (lpfc_sli_issue_mbox
			    (phba, mb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mb);
			}
		}
		phba->fc_flag &= ~(FC_PT2PT | FC_PT2PT_PLOGI);
	}
	phba->fc_flag &= ~FC_LBIT;

	/* Turn off discovery timer if its running */
	lpfc_can_disctmo(phba);


	/* Must process IOCBs on all rings to handle ABORTed I/Os */
	return (0);
}

int
lpfc_linkup(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp, *new_ndlp;
	struct list_head *pos, *next, *listp;
	struct list_head *node_list[4];
	lpfcCfgParam_t *clp;
	int i;

	clp = &phba->config[0];
	phba->hba_state = LPFC_LINK_UP;
	phba->hba_flag |= FC_NDISC_ACTIVE;
	phba->fc_flag &= ~(FC_LNK_DOWN | FC_PT2PT | FC_PT2PT_PLOGI |
			   FC_RSCN_MODE | FC_NLP_MORE | FC_DELAY_DISC |
			   FC_RSCN_DISC_TMR | FC_RSCN_DISCOVERY | FC_LD_TIMER |
			   FC_LD_TIMEOUT);
	phba->fc_ns_retry = 0;

	lpfc_put_event(phba, FC_REG_LINK_EVENT, 0, 0, 0);
	phba->nport_event_cnt++;

	lpfc_hba_put_event(phba, HBA_EVENT_LINK_UP, phba->fc_myDID,
			  phba->fc_topology, 0, phba->fc_linkspeed);

	if (phba->fc_linkdown.function) {
		lpfc_stop_timer((struct clk_data *)phba->fc_linkdown.data);
	}

	/*
	 * Clean up old Fabric, NameServer and other NLP_FABRIC logins.
	 */
	list_for_each_safe(pos, next, &phba->fc_nlpunmap_list) {
		new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		ndlp = new_ndlp;
		if (ndlp->nlp_type & NLP_FABRIC) {
			ndlp->nlp_flag &= ~(NLP_UNMAPPED_LIST |
					    NLP_TGT_NO_SCSIID);
			lpfc_nlp_remove(phba, ndlp);
		}
	}

	/* Mark all nodes for LINK UP */
	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_nlpmap_list;
	for (i = 0; i < 4; i++) {
		listp = node_list[i];
		if (list_empty(listp))
			continue;

		list_for_each(pos, listp) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
					  LPFC_SET_BITMASK);
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_LINK_DOWN,
					  LPFC_CLR_BITMASK);
		}
	}
	/* Setup for first FLOGI */
	phba->fc_ratov = LPFC_DISC_FLOGI_TMO;

	return (0);
}

/*
 * This routine handles processing a READ_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_read_la(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	DMABUF_t *mp;
	LPFC_SLI_t *psli;
	READ_LA_VAR *la;
	LPFC_MBOXQ_t *mbox;
	MAILBOX_t *mb;
	lpfcCfgParam_t *clp;
	uint32_t control;
	int i;

	clp = &phba->config[0];
	psli = &phba->sli;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1307,
				lpfc_mes1307,
				lpfc_msgBlk1307.msgPreambleStr,
				mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;

		/* turn on Link Attention interrupts */
		psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
		control = readl(phba->HCregaddr);
		control |= HC_LAINT_ENA;
		writel(control, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		mp = (DMABUF_t *) (pmb->context1);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		return;
	}
	la = (READ_LA_VAR *) & pmb->mb.un.varReadLA;

	mp = (DMABUF_t *) (pmb->context1);

	/* Get Loop Map information */
	if (mp) {
		memcpy(&phba->alpa_map[0], mp->virt, 128);
	} else {
		memset(&phba->alpa_map[0], 0, 128);
	}

	if (la->pb)
		phba->fc_flag |= FC_BYPASSED_MODE;
	else
		phba->fc_flag &= ~FC_BYPASSED_MODE;

	if (((phba->fc_eventTag + 1) < la->eventTag) ||
	    (phba->fc_eventTag == la->eventTag)) {
		phba->fc_stat.LinkMultiEvent++;
		if (la->attType == AT_LINK_UP) {
			if (phba->fc_eventTag != 0) {

				lpfc_linkdown(phba);
			}
		}
	}

	phba->fc_eventTag = la->eventTag;

	if (la->attType == AT_LINK_UP) {
		phba->fc_stat.LinkUp++;
		/* Link Up Event <eventTag> received */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1303,
				lpfc_mes1303,
				lpfc_msgBlk1303.msgPreambleStr,
				la->eventTag, phba->fc_eventTag,
				la->granted_AL_PA, la->UlnkSpeed,
				phba->alpa_map[0]);

		switch(la->UlnkSpeed) {
			case LA_1GHZ_LINK:
				phba->fc_linkspeed = LA_1GHZ_LINK;
			break;
			case LA_2GHZ_LINK:
				phba->fc_linkspeed = LA_2GHZ_LINK;
			break;
			case LA_4GHZ_LINK:
				phba->fc_linkspeed = LA_4GHZ_LINK;
			break;
			default:
				phba->fc_linkspeed = LA_UNKNW_LINK;
			break;
		}

		if ((phba->fc_topology = la->topology) == TOPOLOGY_LOOP) {

			if (la->il) {
				phba->fc_flag |= FC_LBIT;
			}

			phba->fc_myDID = la->granted_AL_PA;

			i = la->un.lilpBde64.tus.f.bdeSize;
			if (i == 0) {
				phba->alpa_map[0] = 0;
			} else {
				if (clp[LPFC_CFG_LOG_VERBOSE].
				    a_current & LOG_LINK_EVENT) {
					int numalpa, j, k;
					union {
						uint8_t pamap[16];
						struct {
							uint32_t wd1;
							uint32_t wd2;
							uint32_t wd3;
							uint32_t wd4;
						} pa;
					} un;

					numalpa = phba->alpa_map[0];
					j = 0;
					while (j < numalpa) {
						memset(un.pamap, 0, 16);
						for (k = 1; j < numalpa; k++) {
							un.pamap[k - 1] =
							    phba->alpa_map[j +
									   1];
							j++;
							if (k == 16)
								break;
						}
						/* Link Up Event ALPA map */
						lpfc_printf_log(phba->brd_no,
							&lpfc_msgBlk1304,
							lpfc_mes1304,
							lpfc_msgBlk1304
								.msgPreambleStr,
							un.pa.wd1, un.pa.wd2,
							un.pa.wd3, un.pa.wd4);
					}
				}
			}
		} else {
			phba->fc_myDID = phba->fc_pref_DID;
			phba->fc_flag |= FC_LBIT;
		}

		lpfc_linkup(phba);
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			lpfc_read_sparam(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_read_sparam;
			lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
		}

		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			phba->hba_state = LPFC_LOCAL_CFG_LINK;
			lpfc_config_link(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_config_link;
			lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB));
		}
	} else {
		phba->fc_stat.LinkDown++;
		/* Link Down Event <eventTag> received */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1305,
				lpfc_mes1305,
				lpfc_msgBlk1305.msgPreambleStr,
				la->eventTag, phba->fc_eventTag,
				phba->hba_state, phba->fc_flag);

		lpfc_linkdown(phba);

		/* turn on Link Attention interrupts - no CLEAR_LA needed */
		psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
		control = readl(phba->HCregaddr);
		control |= HC_LAINT_ENA;
		writel(control, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	pmb->context1 = 0;
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);
	return;
}

void
lpfc_mbx_cmpl_config_link(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;

	psli = &phba->sli;
	mb = &pmb->mb;
	/* Check for error */
	if (mb->mbxStatus) {
		/* CONFIG_LINK mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0306,
				lpfc_mes0306,
				lpfc_msgBlk0306.msgPreambleStr,
				mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {

		/* Start discovery by sending a FLOGI hba_state is identically
		 * LPFC_FLOGI while waiting for FLOGI cmpl (same on FAN)
		 */
		phba->hba_state = LPFC_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		lpfc_mbox_free(phba, pmb);
		return;
	}

      out:
	/* CONFIG_LINK bad hba state <hba_state> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0200,
			lpfc_mes0200,
			lpfc_msgBlk0200.msgPreambleStr, phba->hba_state);

	if (phba->hba_state != LPFC_CLEAR_LA) {
		lpfc_clear_la(phba, pmb);
		pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
		if (lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
		    == MBX_NOT_FINISHED) {
			lpfc_mbox_free(phba, pmb);
			lpfc_disc_flush_list(phba);
			psli->ring[(psli->ip_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->fcp_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			psli->ring[(psli->next_ring)].flag &=
				~LPFC_STOP_IOCB_EVENT;
			phba->hba_state = LPFC_HBA_READY;
		}
	} else {
		lpfc_mbox_free(phba, pmb);
	}
	return;
}

void
lpfc_mbx_cmpl_read_sparam(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;

	psli = &phba->sli;
	mb = &pmb->mb;

	/* Check for error */
	if (mb->mbxStatus) {
		/* READ_SPARAM mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0319,
				lpfc_mes0319,
				lpfc_msgBlk0319.msgPreambleStr,
				mb->mbxStatus, phba->hba_state);

		lpfc_linkdown(phba);
		phba->hba_state = LPFC_HBA_ERROR;
		mp = (DMABUF_t *) pmb->context1;
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		if (phba->hba_state != LPFC_CLEAR_LA) {
			lpfc_clear_la(phba, pmb);
			pmb->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			if (lpfc_sli_issue_mbox(phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
				== MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, pmb);
				lpfc_disc_flush_list(phba);
				psli->ring[(psli->ip_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->fcp_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->next_ring)].flag &=
					~LPFC_STOP_IOCB_EVENT;
				phba->hba_state = LPFC_HBA_READY;
			}
		} else {
			lpfc_mbox_free(phba, pmb);
		}
		return;
	}

	mp = (DMABUF_t *) pmb->context1;

	memcpy((uint8_t *) & phba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (SERV_PARM));
	memcpy((uint8_t *) & phba->fc_nodename,
	       (uint8_t *) & phba->fc_sparam.nodeName, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & phba->fc_portname,
	       (uint8_t *) & phba->fc_sparam.portName, sizeof (NAME_TYPE));
	memcpy(phba->phys_addr, phba->fc_portname.IEEE, 6);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);
	return;
}

/*
 * This routine handles processing a CLEAR_LA mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_clear_la(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	lpfcCfgParam_t *clp;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos;
	MAILBOX_t *mb;
	uint32_t control;

	psli = &phba->sli;
	clp = &phba->config[0];
	mb = &pmb->mb;
	/* Since we don't do discovery right now, turn these off here */
	psli->ring[psli->ip_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->fcp_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	psli->ring[psli->next_ring].flag &= ~LPFC_STOP_IOCB_EVENT;
	/* Check for error */
	if ((mb->mbxStatus) && (mb->mbxStatus != 0x1601)) {
		/* CLEAR_LA mbox error <mbxStatus> state <hba_state> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0320,
				lpfc_mes0320,
				lpfc_msgBlk0320.msgPreambleStr,
				mb->mbxStatus, phba->hba_state);

		phba->hba_state = LPFC_HBA_ERROR;
		goto out;
	}

	phba->num_disc_nodes = 0;
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
	}
	if (phba->num_disc_nodes == 0) {
		phba->hba_flag &= ~FC_NDISC_ACTIVE;
	}
	phba->hba_state = LPFC_HBA_READY;

      out:
	/* Device Discovery completes */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0225,
			lpfc_mes0225, lpfc_msgBlk0225.msgPreambleStr);

	phba->hba_flag &= ~FC_LFR_ACTIVE;

	lpfc_mbox_free(phba, pmb);
	if (phba->fc_flag & FC_ESTABLISH_LINK) {
		phba->fc_flag &= ~FC_ESTABLISH_LINK;
	}
	if (phba->fc_estabtmo.function) {
		lpfc_stop_timer((struct clk_data *)phba->fc_estabtmo.data);
	}
	lpfc_can_disctmo(phba);

	/* turn on Link Attention interrupts */
	psli->sliinit.sli_flag |= LPFC_PROCESS_LA;
	control = readl(phba->HCregaddr);
	control |= HC_LAINT_ENA;
	writel(control, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* If there are mapped FCP nodes still running, restart the scheduler 
	 * to get any pending IOCBs out.
	 */
	if (phba->fc_map_cnt) {
		lpfc_sched_check(phba);
	}
	return;
}

/*
 * This routine handles processing a REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_reg_login(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	pmb->context1 = 0;

	/* Good status, call state machine */
	lpfc_disc_state_machine(phba, ndlp, pmb, NLP_EVT_CMPL_REG_LOGIN);
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);

	return;
}

/*
 * This routine handles processing a Fabric REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fabric_reg_login(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *ndlp_fdmi;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	pmb->context1 = 0;

	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		/* This NPort has been assigned an NPort_ID by the fabric as a
		 * result of the completed fabric login.  Issue a State Change
		 * Registration (SCR) ELS request to the fabric controller
		 * (SCR_DID) so that this NPort gets RSCN events from the
		 * fabric.
		 */
		lpfc_issue_els_scr(phba, SCR_DID, 0);

		/* Allocate a new node instance.  If the pool is empty, just
		 * start the discovery process and skip the Nameserver login
		 * process.  This is attempted again later on.  Otherwise, issue
		 * a Port Login (PLOGI) to the NameServer
		 */
		if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
			lpfc_disc_start(phba);
		} else {
			memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_type |= NLP_FABRIC;
			ndlp->nlp_DID = NameServer_DID;
			ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
			lpfc_issue_els_plogi(phba, ndlp, 0);
			if (clp[LPFC_CFG_FDMI_ON].a_current) {
				if ((ndlp_fdmi = lpfc_nlp_alloc(phba, 0))) {
					memset(ndlp_fdmi, 0,
					       sizeof (LPFC_NODELIST_t));
					ndlp_fdmi->nlp_type |= NLP_FABRIC;
					ndlp_fdmi->nlp_DID = FDMI_DID;
					ndlp_fdmi->nlp_state =
					    NLP_STE_PLOGI_ISSUE;
					lpfc_issue_els_plogi(phba, ndlp_fdmi,
							     0);
				}
			}
		}
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);

	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_ns_reg_login(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;

	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	pmb->context1 = 0;
	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	if (phba->hba_state < LPFC_HBA_READY) {
		/* Link up discovery requires Fabrib registration. */
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RNN_ID);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RSNN_NN);
		lpfc_ns_cmd(phba, ndlp, SLI_CTNS_RFT_ID);
	}

	phba->fc_ns_retry = 0;
	/* Good status, issue CT Request to NameServer */
	if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT)) {
		/* Cannot issue NameServer Query, so finish up discovery */
		lpfc_disc_start(phba);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);

	return;
}

/*
 * Start / ReStart npr timer for Discovery / RSCN handling
 */
void
lpfc_set_npr_tmo(lpfcHBA_t * phba, LPFCSCSITARGET_t *targetp,
		LPFC_NODELIST_t * nlp)
{
	uint32_t tmo;
	lpfcCfgParam_t *clp;

	if(targetp->targetFlags & FC_NPR_ACTIVE)
		return;

	clp = &phba->config[0];
	targetp->targetFlags |= FC_NPR_ACTIVE;
	if(clp[LPFC_CFG_HOLDIO].a_current == 0){
		tmo = clp[LPFC_CFG_NODEV_TMO].a_current;

		if (( phba->fc_flag & FC_LNK_DOWN) &&
		   (clp[LPFC_CFG_NODEV_TMO].a_current <
		    clp[LPFC_CFG_LINKDOWN_TMO].a_current)) {
			tmo = clp[LPFC_CFG_LINKDOWN_TMO].a_current;
		}
		lpfc_start_timer(phba, tmo, &targetp->tmofunc,
			 lpfc_npr_timeout, (unsigned long)targetp, 0);

 		/* Start nodev timer */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0256,
				lpfc_mes0256,
				lpfc_msgBlk0256.msgPreambleStr,
				nlp->nlp_DID, nlp->nlp_flag, nlp->nlp_state,
				nlp->nlp_sid);
	}
	return;
}

/*
 * Cancel npr timer for Discovery / RSCN handling
 */
int
lpfc_can_npr_tmo(lpfcHBA_t * phba, LPFCSCSITARGET_t *targetp,
		LPFC_NODELIST_t * nlp)
{
	int rc;

	rc = 0;

	targetp->targetFlags &= ~FC_NPR_ACTIVE;
	if(targetp->tmofunc.function) {
		lpfc_stop_timer((struct clk_data *) targetp->tmofunc.data);
		rc = 1;
	}

	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0260,
			lpfc_mes0260,
			lpfc_msgBlk0260.msgPreambleStr,
			nlp->nlp_DID, nlp->nlp_flag, nlp->nlp_state,
			nlp->nlp_sid);
	return (rc);
}

/* Put blp on the bind list */
int
lpfc_nlp_bind(lpfcHBA_t * phba, LPFC_BINDLIST_t * blp)
{
	LPFCSCSITARGET_t *targetp;

	/* Put it at the end of the bind list */
	list_add_tail(&blp->nlp_listp, &phba->fc_nlpbind_list);
	phba->fc_bind_cnt++;
	targetp = phba->device_queue_hash[blp->nlp_sid];

	/* Add scsiid <sid> to BIND list */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0903,
			lpfc_mes0903,
			lpfc_msgBlk0903.msgPreambleStr,
			blp->nlp_sid, phba->fc_bind_cnt, blp->nlp_DID,
			blp->nlp_bind_type, (unsigned long)blp);

	return (0);
}

/* Put blp on the plogi list */
int
lpfc_nlp_plogi(lpfcHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	LPFC_BINDLIST_t *blp;
	LPFC_SLI_t *psli;

	psli = &phba->sli;
	blp = 0;

	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			phba->fc_map_cnt--;
			list_del(&nlp->nlp_listp);
			phba->nport_event_cnt++;

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, LPFC_DEV_DISCONNECTED,
					  LPFC_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
				nlp->nlp_listp_bind = 0;
				nlp->nlp_sid = 0;
				nlp->nlp_flag &= ~NLP_SEED_MASK;
			}
			if(nlp->nlp_Target)
				lpfc_set_npr_tmo(phba, nlp->nlp_Target, nlp);

		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			phba->fc_unmap_cnt--;
			list_del(&nlp->nlp_listp);
			phba->nport_event_cnt++;

		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			return (0);	/* Already on plogi list */
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			phba->fc_adisc_cnt--;
			list_del(&nlp->nlp_listp);
		}
	}

	/* Put it at the end of the plogi list */
	list_add_tail(&nlp->nlp_listp, &phba->fc_plogi_list);
	phba->fc_plogi_cnt++;
	nlp->nlp_flag |= NLP_PLOGI_LIST;

	/* Add NPort <did> to PLOGI list */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0904,
			lpfc_mes0904,
			lpfc_msgBlk0904.msgPreambleStr,
			nlp->nlp_DID, phba->fc_plogi_cnt, (unsigned long)blp);

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}
	return (0);
}

/* Put nlp on the adisc list */
int
lpfc_nlp_adisc(lpfcHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	LPFC_BINDLIST_t *blp;
	LPFC_SLI_t *psli;
	LPFCSCSITARGET_t *targetp;
	lpfcCfgParam_t *clp;

	blp = 0;
	psli = &phba->sli;
	clp = &phba->config[0];
	targetp = nlp->nlp_Target;

	/* Check to see if this node exist on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			phba->fc_map_cnt--;
			list_del(&nlp->nlp_listp);
			phba->nport_event_cnt++;

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, LPFC_DEV_DISAPPEARED,
					  LPFC_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
				nlp->nlp_listp_bind = 0;
				nlp->nlp_flag &= ~NLP_SEED_MASK;
			}
			if(nlp->nlp_Target)
				lpfc_set_npr_tmo(phba, nlp->nlp_Target, nlp);

		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			phba->fc_unmap_cnt--;
			list_del(&nlp->nlp_listp);
			phba->nport_event_cnt++;


		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			phba->fc_plogi_cnt--;
			list_del(&nlp->nlp_listp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			return (0);	/* Already on adisc list */
		}
	}

	/* Put it at the end of the adisc list */
	list_add_tail(&nlp->nlp_listp, &phba->fc_adisc_list);
	phba->fc_adisc_cnt++;
	nlp->nlp_flag |= NLP_ADISC_LIST;

	/* Add NPort <did> to ADISC list */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0905,
			lpfc_mes0905,
			lpfc_msgBlk0905.msgPreambleStr,
			nlp->nlp_DID, phba->fc_adisc_cnt, (unsigned long)blp);

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}

	return (0);
}

/*
 * Put nlp on the unmapped list 
 * NOTE: - update nlp_type to NLP_FC_NODE
 */
int
lpfc_nlp_unmapped(lpfcHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	LPFC_BINDLIST_t *blp;

	blp = 0;

	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			nlp->nlp_flag &= ~NLP_MAPPED_LIST;
			phba->fc_map_cnt--;
			list_del(&nlp->nlp_listp);

			/* Must call before binding is removed */
			lpfc_set_failmask(phba, nlp, LPFC_DEV_DISAPPEARED,
					  LPFC_SET_BITMASK);

			blp = nlp->nlp_listp_bind;
			if (blp) {
				blp->nlp_Target = nlp->nlp_Target;
				nlp->nlp_listp_bind = 0;
				nlp->nlp_sid = 0;
				nlp->nlp_flag &= ~NLP_SEED_MASK;
			}
			if(nlp->nlp_Target)
				lpfc_set_npr_tmo(phba, nlp->nlp_Target, nlp);

		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			return (0);	/* Already on unmapped list */
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			phba->fc_plogi_cnt--;
			list_del(&nlp->nlp_listp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			phba->fc_adisc_cnt--;
			list_del(&nlp->nlp_listp);
		}
	}

	/* Put it at the end of the unmapped list */
	list_add_tail(&nlp->nlp_listp, &phba->fc_nlpunmap_list);
	phba->nport_event_cnt++;
	phba->fc_unmap_cnt++;
	nlp->nlp_type |= NLP_FC_NODE;
	nlp->nlp_flag |= NLP_UNMAPPED_LIST;

	/* Add NPort <did> to UNMAP list */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0906,
			lpfc_mes0906,
			lpfc_msgBlk0906.msgPreambleStr,
			nlp->nlp_DID, phba->fc_unmap_cnt, (unsigned long)blp);

	if (blp) {
		lpfc_nlp_bind(phba, blp);
	}
	return (0);
}

/*
 * Put nlp on the mapped list 
 * NOTE: - update nlp_type to NLP_FCP_TARGET
 *       - attach binding entry to context2 
 */
int
lpfc_nlp_mapped(lpfcHBA_t * phba, LPFC_NODELIST_t * nlp, LPFC_BINDLIST_t * blp)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	struct list_head *curr, *next;

	/* Check to see if this node exists on any other list */
	if (nlp->nlp_flag & NLP_LIST_MASK) {
		if (nlp->nlp_flag & NLP_MAPPED_LIST) {
			return (0);	/* Already on mapped list */
		} else if (nlp->nlp_flag & NLP_UNMAPPED_LIST) {
			nlp->nlp_flag &=
			    ~(NLP_UNMAPPED_LIST | NLP_TGT_NO_SCSIID);
			phba->fc_unmap_cnt--;
			list_del(&nlp->nlp_listp);
		} else if (nlp->nlp_flag & NLP_PLOGI_LIST) {
			nlp->nlp_flag &= ~NLP_PLOGI_LIST;
			phba->fc_plogi_cnt--;
			list_del(&nlp->nlp_listp);
		} else if (nlp->nlp_flag & NLP_ADISC_LIST) {
			nlp->nlp_flag &= ~NLP_ADISC_LIST;
			phba->fc_adisc_cnt--;
			list_del(&nlp->nlp_listp);
		}
	}

	/* Put it at the end of the mapped list */
	list_add_tail(&nlp->nlp_listp, &phba->fc_nlpmap_list);
	phba->nport_event_cnt++;
	phba->fc_map_cnt++;
	nlp->nlp_flag |= NLP_MAPPED_LIST;
	nlp->nlp_type |= NLP_FCP_TARGET;
	nlp->nlp_sid = blp->nlp_sid;
	nlp->nlp_listp_bind = blp;
	targetp = phba->device_queue_hash[nlp->nlp_sid];

	/* Add NPort <did> to MAPPED list scsiid <sid> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0907,
			lpfc_mes0907,
			lpfc_msgBlk0907.msgPreambleStr,
			nlp->nlp_DID, nlp->nlp_sid, phba->fc_map_cnt,
			(unsigned long)blp);

	if(nlp->nlp_tmofunc.function) {
		nlp->nlp_flag &= ~NLP_DELAY_TMO;
		lpfc_stop_timer((struct clk_data *)nlp->nlp_tmofunc.data);
	}
	if (targetp) {
		lpfc_can_npr_tmo(phba, targetp, nlp);
		blp->nlp_Target = targetp;
		nlp->nlp_Target = targetp;
		targetp->pcontext = nlp;
		lpfc_scsi_assign_rpi(phba, targetp, nlp->nlp_rpi);
		targetp->un.dev_did = nlp->nlp_DID;
		list_for_each_safe(curr, next, &targetp->lunlist) {
			lunp = list_entry(curr, LPFCSCSILUN_t, list);
			lunp->pnode = (LPFC_NODELIST_t *) nlp;
		}
	}

	return (0);
}

/*
 * Start / ReStart rescue timer for Discovery / RSCN handling
 */
void
lpfc_set_disctmo(lpfcHBA_t * phba)
{
	uint32_t tmo;

	/* lpfc_prep_els_iocb adds LPFC_DRVR_TIMEOUT, so we must here as well */
	tmo = (phba->fc_ratov << 1) + LPFC_DRVR_TIMEOUT + 1;

	/* Turn off discovery timer if its running */
	if (phba->fc_disctmo.function) {
		lpfc_stop_timer((struct clk_data *)phba->fc_disctmo.data);
	}
	lpfc_start_timer(phba, tmo, &phba->fc_disctmo, lpfc_disc_timeout,
			 (unsigned long)0, (unsigned long)0);

	/* Start Discovery Timer state <hba_state> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0247,
			lpfc_mes0247,
			lpfc_msgBlk0247.msgPreambleStr,
			phba->hba_state, tmo, (unsigned long)&phba->fc_disctmo,
			phba->fc_plogi_cnt, phba->fc_adisc_cnt);

	return;
}

/*
 * Cancel rescue timer for Discovery / RSCN handling
 */
int
lpfc_can_disctmo(lpfcHBA_t * phba)
{
	int rc;

	rc = 0;

	/* Turn off discovery timer if its running */
	if (phba->fc_disctmo.function) {
		lpfc_stop_timer((struct clk_data *)phba->fc_disctmo.data);
		rc = 1;
	}

	/* Cancel Discovery Timer state <hba_state> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0248,
			lpfc_mes0248,
			lpfc_msgBlk0248.msgPreambleStr,
			phba->hba_state, phba->fc_flag, rc, phba->fc_plogi_cnt,
			phba->fc_adisc_cnt);

	return (rc);
}

/*
 * Check specified ring for outstanding IOCB on the SLI queue
 * Return true if iocb matches the specified nport
 */
int
lpfc_check_sli_ndlp(lpfcHBA_t * phba,
		    LPFC_SLI_RING_t * pring,
		    LPFC_IOCBQ_t * iocb, LPFC_NODELIST_t * ndlp)
{
	LPFC_SLI_t *psli;
	IOCB_t *icmd;

	psli = &phba->sli;
	icmd = &iocb->iocb;
	if (pring->ringno == LPFC_ELS_RING) {
		switch (icmd->ulpCommand) {
		case CMD_GEN_REQUEST64_CR:
			if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi)
				return (1);
		case CMD_ELS_REQUEST64_CR:
		case CMD_XMIT_ELS_RSP64_CX:
			if (iocb->context1 == (uint8_t *) ndlp)
				return (1);
		}
	} else if (pring->ringno == psli->ip_ring) {

	} else if (pring->ringno == psli->fcp_ring) {
		/* Skip match check if waiting to relogin to FCP target */
	  	if((ndlp->nlp_type & NLP_FCP_TARGET) &&
	  	  (ndlp->nlp_tmofunc.function) &&
		  (ndlp->nlp_flag & NLP_DELAY_TMO)) {
			return (0);
		}
		if (icmd->ulpContext == (volatile ushort)ndlp->nlp_rpi) {
			return (1);
		}
	} else if (pring->ringno == psli->next_ring) {

	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with nlp_rpi in the LPFC_NODELIST entry.
 */
int
lpfc_no_rpi(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	uint32_t rpi, i;
	struct list_head *curr, *next;

	psli = &phba->sli;
	rpi = ndlp->nlp_rpi;
	if (rpi) {
		/* Now process each ring */
		for (i = 0; i < psli->sliinit.num_rings; i++) {
			pring = &psli->ring[i];

			list_for_each_safe(curr, next, &pring->txq) {
				next_iocb = list_entry(curr, LPFC_IOCBQ_t,
						       list);
				iocb = next_iocb;
				/* Check to see if iocb matches the nport we are
				   looking for */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl
					   with an error */
					list_del(&iocb->list);
					pring->txq_cnt--;
					if (iocb->iocb_cmpl) {
						icmd = &iocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						(iocb->iocb_cmpl) (phba,
								   iocb, iocb);
					} else {
						lpfc_iocb_free(phba, iocb);
					}
				}
			}
			/* Everything that matches on txcmplq will be returned
			 * by firmware with a no rpi error.
			 */
		}
	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with a brand new rpi.
 */
int
lpfc_new_rpi(lpfcHBA_t * phba, uint16_t rpi)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	struct list_head *curr, *next;

	if (rpi) {
		psli = &phba->sli;
		pring = &psli->ring[psli->fcp_ring];

		list_for_each_safe(curr, next, &pring->txq) {
			next_iocb = list_entry(curr, LPFC_IOCBQ_t,
						       list);
			iocb = next_iocb;
			icmd = &iocb->iocb;
			if (icmd->ulpContext == (volatile ushort)rpi) {
				/* It matches, so deque and call compl
				   with an error */
				list_del(&iocb->list);
				pring->txq_cnt--;
				if (iocb->iocb_cmpl) {
					icmd->ulpStatus =
					    IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] =
					    IOERR_SLI_ABORTED;
					(iocb->iocb_cmpl) (phba,
							   iocb, iocb);
				} else {
					lpfc_iocb_free(phba, iocb);
				}
			}
		}
	}
	return (0);
}

/*
 * Free resources / clean up outstanding I/Os
 * associated with a LPFC_NODELIST entry. This
 * routine effectively results in a "software abort".
 */
int
lpfc_driver_abort(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	struct clk_data *clkData;
	uint32_t i, cmd;
	struct list_head *curr, *next;

	/* Abort outstanding I/O on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0201,
			lpfc_mes0201,
			lpfc_msgBlk0201.msgPreambleStr,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	psli = &phba->sli;
	/* Now process each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];

		/* First check the txq */
		list_for_each_safe(curr, next, &pring->txq) {
			next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
			iocb = next_iocb;
			/* Check to see if iocb matches the nport we are looking
			   for */
			if ((lpfc_check_sli_ndlp(phba, pring, iocb, ndlp))) {
				/* It matches, so deque and call compl with an
				   error */
				list_del(&iocb->list);
				pring->txq_cnt--;
				if (iocb->iocb_cmpl) {
					icmd = &iocb->iocb;
					icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
					icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
					(iocb->iocb_cmpl) (phba, iocb, iocb);
				} else {
					lpfc_iocb_free(phba, iocb);
				}
			}
		}
		/* Everything on txcmplq will be returned by firmware with a no
		 * rpi / linkdown / abort error.  For ring 0, ELS discovery, we
		 * want to get rid of it right here.
		 */
		if (pring->ringno == LPFC_ELS_RING) {
			/* Next check the txcmplq */
			list_for_each_safe(curr, next, &pring->txcmplq) {
				next_iocb =
				    list_entry(curr, LPFC_IOCBQ_t, list);
				iocb = next_iocb;
				/* Check to see if iocb matches the nport we are
				   looking for */
				if ((lpfc_check_sli_ndlp
				     (phba, pring, iocb, ndlp))) {
					/* It matches, so deque and call compl
					   with an error */
					list_del(&iocb->list);
					pring->txcmplq_cnt--;

					icmd = &iocb->iocb;
					/* If the driver is completing an ELS
					 * command early, flush it out of the
					 * firmware.
					 */
					if ((icmd->ulpCommand ==
					     CMD_ELS_REQUEST64_CR)
					    && (icmd->un.elsreq64.bdl.
						ulpIoTag32)) {
						lpfc_sli_issue_abort_iotag32
						    (phba, pring, iocb);
					}
					if (iocb->iocb_cmpl) {
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SLI_ABORTED;
						(iocb->iocb_cmpl) (phba,
								   iocb, iocb);
					} else {
						lpfc_iocb_free(phba, iocb);
					}
				}
			}
		}
	}

	/* If we are delaying issuing an ELS command, cancel it */
	if ((ndlp->nlp_tmofunc.function) && (ndlp->nlp_flag & NLP_DELAY_TMO)) {
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		clkData = (struct clk_data *)(ndlp->nlp_tmofunc.data);
		cmd = (uint32_t) (unsigned long)clkData->clData2;
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);

		/* Allocate an IOCB and indicate an error completion */
		/* Allocate a buffer for the command iocb */
		if ((iocb = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			return (0);
		}
		memset(iocb, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &iocb->iocb;
		icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
		icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
		iocb->context1 = ndlp;

		switch (cmd) {
		case ELS_CMD_FLOGI:
			iocb->iocb_cmpl = lpfc_cmpl_els_flogi;
			break;
		case ELS_CMD_PLOGI:
			iocb->iocb_cmpl = lpfc_cmpl_els_plogi;
			break;
		case ELS_CMD_ADISC:
			iocb->iocb_cmpl = lpfc_cmpl_els_adisc;
			break;
		case ELS_CMD_PRLI:
			iocb->iocb_cmpl = lpfc_cmpl_els_prli;
			break;
		case ELS_CMD_LOGO:
			iocb->iocb_cmpl = lpfc_cmpl_els_logo;
			break;
		default:
			iocb->iocb_cmpl = lpfc_cmpl_els_cmd;
			break;
		}
		(iocb->iocb_cmpl) (phba, iocb, iocb);
	}
	return (0);
}

void
lpfc_dequenode(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFC_BINDLIST_t *blp;

	if (ndlp->nlp_flag & NLP_LIST_MASK) {
		if (ndlp->nlp_flag & NLP_MAPPED_LIST) {
			ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
			phba->fc_map_cnt--;
			list_del(&ndlp->nlp_listp);
			phba->nport_event_cnt++;
			blp = ndlp->nlp_listp_bind;
			ndlp->nlp_listp_bind = 0;
			if (blp) {
				blp->nlp_Target = ndlp->nlp_Target;
				lpfc_nlp_bind(phba, blp);
			}
			ndlp->nlp_flag &= ~NLP_SEED_MASK;
		} else if (ndlp->nlp_flag & NLP_UNMAPPED_LIST) {
			ndlp->nlp_flag &= ~NLP_UNMAPPED_LIST;
			phba->fc_unmap_cnt--;
			list_del(&ndlp->nlp_listp);
			phba->nport_event_cnt++;
		} else if (ndlp->nlp_flag & NLP_PLOGI_LIST) {
			ndlp->nlp_flag &= ~NLP_PLOGI_LIST;
			phba->fc_plogi_cnt--;
			list_del(&ndlp->nlp_listp);
		} else if (ndlp->nlp_flag & NLP_ADISC_LIST) {
			ndlp->nlp_flag &= ~NLP_ADISC_LIST;
			phba->fc_adisc_cnt--;
			list_del(&ndlp->nlp_listp);
		}
	}
	return;
}

/*
 * Free resources associated with LPFC_NODELIST entry
 * so it can be freed.
 */
int
lpfc_freenode(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFC_MBOXQ_t *mbox;
	LPFC_SLI_t *psli;

	/* The psli variable gets rid of the long pointer deference. */
	psli = &phba->sli;

	/* Cleanup node for NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0900,
			lpfc_mes0900,
			lpfc_msgBlk0900.msgPreambleStr,
			ndlp->nlp_DID, ndlp->nlp_flag, ndlp->nlp_state,
			ndlp->nlp_rpi);

	lpfc_dequenode(phba, ndlp);

	if (ndlp->nlp_tmofunc.function) {
		lpfc_stop_timer((struct clk_data *)ndlp->nlp_tmofunc.data);
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
	}

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
		lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
				  LPFC_SET_BITMASK);
	}
	return (0);
}

/*
 * Check to see if we can free the nlp back to the freelist.
 * If we are in the middle of using the nlp in the discovery state
 * machine, defer the free till we reach the end of the state machine.
 */
int
lpfc_nlp_remove(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t    *lunp;
	struct list_head *curr, *next;

	if(ndlp->nlp_disc_refcnt) {
		ndlp->nlp_rflag |= NLP_DELAY_REMOVE;
	}
	else {
		/* Since the ndlp is being freed, disassociate it
		 * from the target / lun structures.
		 */
		targetp = ndlp->nlp_Target;
		if(targetp) {
			lpfc_sched_flush_target(phba, targetp, IOSTAT_LOCAL_REJECT,
				IOERR_SLI_ABORTED);

			targetp->pcontext = 0;
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = 0;
			}
		}
		lpfc_freenode(phba, ndlp);
		lpfc_nlp_free(phba, ndlp);
	}
	return(0);
}

int
lpfc_matchdid(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, uint32_t did)
{
	D_ID mydid;
	D_ID ndlpdid;
	D_ID matchdid;
	int zero_did;

	if (did == Bcast_DID)
		return (0);

	zero_did = 0;
	if (ndlp->nlp_DID == 0) {
		return (0);
	}

	/* First check for Direct match */
	if (ndlp->nlp_DID == did)
		return (1);

	/* Next check for area/domain identically equals 0 match */
	mydid.un.word = phba->fc_myDID;
	if ((mydid.un.b.domain == 0) && (mydid.un.b.area == 0)) {
		goto out;
	}

	matchdid.un.word = did;
	ndlpdid.un.word = ndlp->nlp_DID;
	if (matchdid.un.b.id == ndlpdid.un.b.id) {
		if ((mydid.un.b.domain == matchdid.un.b.domain) &&
		    (mydid.un.b.area == matchdid.un.b.area)) {
			if ((ndlpdid.un.b.domain == 0) &&
			    (ndlpdid.un.b.area == 0)) {
				if (ndlpdid.un.b.id)
					return (1);
			}
			goto out;
		}

		matchdid.un.word = ndlp->nlp_DID;
		if ((mydid.un.b.domain == ndlpdid.un.b.domain) &&
		    (mydid.un.b.area == ndlpdid.un.b.area)) {
			if ((matchdid.un.b.domain == 0) &&
			    (matchdid.un.b.area == 0)) {
				if (matchdid.un.b.id)
					return (1);
			}
		}
	}
      out:
	if (zero_did)
		ndlp->nlp_DID = 0;
	return (0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_scsiid(lpfcHBA_t * phba, uint32_t scsid)
{
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos;
	LPFCSCSITARGET_t *targetp;

	targetp = phba->device_queue_hash[scsid];
	/* First see if the SCSI ID has an allocated LPFCSCSITARGET_t */
	if (targetp) {
		if (targetp->pcontext) {
			return ((LPFC_NODELIST_t *) targetp->pcontext);
		}
	}

	/* Now try the hard way */
	list_for_each(pos, &phba->fc_nlpmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (scsid == ndlp->nlp_sid) {
			return (ndlp);
		}
	}

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_wwnn(lpfcHBA_t * phba, uint32_t order, NAME_TYPE * wwnn)
{
	LPFC_NODELIST_t *ndlp;
	uint32_t data1;
	LPFC_BINDLIST_t *blp;
	struct list_head *pos, *tpos;

	blp = 0;
	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_safe(pos, tpos, &phba->fc_nlpunmap_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (lpfc_geportname(&ndlp->nlp_nodename, wwnn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0910,
						lpfc_mes0910,
						lpfc_msgBlk0910.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					phba->fc_unmap_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;
				}
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_safe(pos, tpos, &phba->fc_nlpmap_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (lpfc_geportname(&ndlp->nlp_nodename, wwnn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node did mapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0902,
						lpfc_mes0902,
						lpfc_msgBlk0902.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					phba->fc_map_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;

					/* Must call before binding is
					   removed */
					lpfc_set_failmask(phba, ndlp,
							  LPFC_DEV_DISAPPEARED,
							  LPFC_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target and sid since
					 * LPFC_DEV_DISAPPEARED is a
					 * non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}
				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
		}
	}

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_wwpn(lpfcHBA_t * phba, uint32_t order, NAME_TYPE * wwpn)
{
	LPFC_NODELIST_t *ndlp, *new_ndlp;
	uint32_t data1;
	LPFC_BINDLIST_t *blp;
	struct list_head *pos, *next;

	blp = 0;
	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_safe(pos, next, &phba->fc_nlpunmap_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			if (lpfc_geportname(&ndlp->nlp_portname, wwpn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0911,
						lpfc_mes0911,
						lpfc_msgBlk0911.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					phba->fc_unmap_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;
				}
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_safe(pos, next, &phba->fc_nlpmap_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			if (lpfc_geportname(&ndlp->nlp_portname, wwpn) == 2) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID mapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0901,
						lpfc_mes0901,
						lpfc_msgBlk0901.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					phba->fc_map_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;

					/* Must call before binding is
					   removed */
					lpfc_set_failmask(phba, ndlp,
							  LPFC_DEV_DISAPPEARED,
							  LPFC_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target and sid since
					 * LPFC_DEV_DISAPPEARED is a
					 * non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}
				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
		}
	}

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Search for a nodelist entry on a specific list */
LPFC_NODELIST_t *
lpfc_findnode_did(lpfcHBA_t * phba, uint32_t order, uint32_t did)
{
	LPFC_NODELIST_t *ndlp, *new_ndlp;
	uint32_t data1;
	LPFC_BINDLIST_t *blp;
	struct list_head *pos, *next;

	blp = 0;
	if (order & NLP_SEARCH_UNMAPPED) {
		list_for_each_safe(pos, next, &phba->fc_nlpunmap_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			if (lpfc_matchdid(phba, ndlp, did)) {
				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID unmapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0929,
						lpfc_mes0929,
						lpfc_msgBlk0929.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &=
					    ~(NLP_UNMAPPED_LIST |
					      NLP_TGT_NO_SCSIID);
					phba->fc_unmap_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;
				}

				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_MAPPED) {
		list_for_each_safe(pos, next, &phba->fc_nlpmap_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;

			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* FIND node DID mapped */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0930,
						lpfc_mes0930,
						lpfc_msgBlk0930.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_MAPPED_LIST;
					phba->fc_map_cnt--;
					list_del(&ndlp->nlp_listp);
					phba->nport_event_cnt++;

					/* Must call before binding is
					   removed */
					lpfc_set_failmask(phba, ndlp,
							  LPFC_DEV_DISAPPEARED,
							  LPFC_SET_BITMASK);

					blp = ndlp->nlp_listp_bind;
					ndlp->nlp_listp_bind = 0;
					if (blp) {
						blp->nlp_Target =
						    ndlp->nlp_Target;
					}
					/* Keep Target and sid since
					 * LPFC_DEV_DISAPPEARED is a
					 * non-fatal error
					 */
					ndlp->nlp_flag &= ~NLP_SEED_MASK;
				}

				if (blp) {
					lpfc_nlp_bind(phba, blp);
				}
				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_PLOGI) {
		list_for_each_safe(pos, next, &phba->fc_plogi_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to PLOGI */
				/* FIND node DID bind */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0908,
						lpfc_mes0908,
						lpfc_msgBlk0908.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_PLOGI_LIST;
					phba->fc_plogi_cnt--;
					list_del(&ndlp->nlp_listp);
				}

				return (ndlp);
			}
		}
	}

	if (order & NLP_SEARCH_ADISC) {
		list_for_each_safe(pos, next, &phba->fc_adisc_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			if (lpfc_matchdid(phba, ndlp, did)) {

				data1 = (((uint32_t) ndlp->nlp_state << 24) |
					 ((uint32_t) ndlp->nlp_xri << 16) |
					 ((uint32_t) ndlp->nlp_type << 8) |
					 ((uint32_t) ndlp->nlp_rpi & 0xff));
				/* LOG change to ADISC */
				/* FIND node DID bind */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0931,
						lpfc_mes0931,
						lpfc_msgBlk0931.msgPreambleStr,
						(ulong) ndlp, ndlp->nlp_DID,
						ndlp->nlp_flag, data1);
				if (order & NLP_SEARCH_DEQUE) {
					ndlp->nlp_flag &= ~NLP_ADISC_LIST;
					phba->fc_adisc_cnt--;
					list_del(&ndlp->nlp_listp);
				}

				return (ndlp);
			}
		}
	}

	/* FIND node did <did> NOT FOUND */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0932,
			lpfc_mes0932,
			lpfc_msgBlk0932.msgPreambleStr, did, order);

	/* no match found */
	return ((LPFC_NODELIST_t *) 0);
}

/* Build a list of nodes to discover based on the loopmap */
void
lpfc_disc_list_loopmap(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;
	int j;
	uint32_t alpa, index;

	clp = &phba->config[0];

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->fc_topology != TOPOLOGY_LOOP) {
		return;
	}

	/* Check for loop map present or not */
	if (phba->alpa_map[0]) {
		for (j = 1; j <= phba->alpa_map[0]; j++) {
			alpa = phba->alpa_map[j];

			if (((phba->fc_myDID & 0xff) == alpa) || (alpa == 0)) {
				continue;
			}
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_MAPPED |
						       NLP_SEARCH_UNMAPPED |
						       NLP_SEARCH_DEQUE),
						      alpa))) {
				/* Mark node for address authentication */
				lpfc_disc_state_machine(phba, ndlp, 0,
							NLP_EVT_DEVICE_ADD);
				continue;
			}
			/* Skip if the node is already in the plogi / adisc
			   list */
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_PLOGI |
						       NLP_SEARCH_ADISC),
						      alpa))) {
				continue;
			}
			/* Cannot find existing Fabric ndlp, so allocate a new
			   one */
			if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
				continue;
			}
			memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			ndlp->nlp_DID = alpa;
			/* Mark node for address discovery */
			lpfc_disc_state_machine(phba, ndlp, 0,
						NLP_EVT_DEVICE_ADD);
		}
	} else {
		/* No alpamap, so try all alpa's */
		for (j = 0; j < FC_MAXLOOP; j++) {
			if (clp[LPFC_CFG_SCAN_DOWN].a_current)
				index = FC_MAXLOOP - j - 1;
			else
				index = j;
			alpa = lpfcAlpaArray[index];
			if ((phba->fc_myDID & 0xff) == alpa) {
				continue;
			}

			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_MAPPED |
						       NLP_SEARCH_UNMAPPED |
						       NLP_SEARCH_DEQUE),
						      alpa))) {
				/* Mark node for address authentication */
				lpfc_disc_state_machine(phba, ndlp, 0,
							NLP_EVT_DEVICE_ADD);
				continue;
			}
			/* Skip if the node is already in the plogi / adisc
			   list */
			if ((ndlp = lpfc_findnode_did(phba,
						      (NLP_SEARCH_PLOGI |
						       NLP_SEARCH_ADISC),
						      alpa))) {
				continue;
			}
			/* Cannot find existing ndlp, so allocate a new one */
			if ((ndlp = lpfc_nlp_alloc(phba, 0)) == 0) {
				continue;
			}
			memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
			ndlp->nlp_state = NLP_STE_UNUSED_NODE;
			ndlp->nlp_DID = alpa;
			/* Mark node for address discovery */
			lpfc_disc_state_machine(phba, ndlp, 0,
						NLP_EVT_DEVICE_ADD);
		}
	}
	return;
}

/* Start Link up / RSCN discovery on ADISC or PLOGI lists */
void
lpfc_disc_start(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *mbox;
	LPFC_NODELIST_t *ndlp;
	LPFC_NODELIST_t *new_ndlp;
	struct list_head *pos, *next;
	uint32_t did_changed;
	lpfcCfgParam_t *clp;
	uint32_t clear_la_pending;

	clp = &phba->config[0];
	psli = &phba->sli;

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return;
	}
	if (phba->hba_state == LPFC_CLEAR_LA)
		clear_la_pending = 1;
	else
		clear_la_pending = 0;

	if (phba->hba_state < LPFC_HBA_READY) {
		phba->hba_state = LPFC_DISC_AUTH;
	}
	lpfc_set_disctmo(phba);

	if (phba->fc_prevDID == phba->fc_myDID) {
		did_changed = 0;
	} else {
		did_changed = 1;
	}
	phba->fc_prevDID = phba->fc_myDID;

	/* Start Discovery state <hba_state> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0202,
			lpfc_mes0202,
			lpfc_msgBlk0202.msgPreambleStr,
			phba->hba_state, phba->fc_flag, phba->fc_plogi_cnt,
			phba->fc_adisc_cnt);

	/* First do ADISC for authentication */
	if (phba->fc_adisc_cnt) {
		if (did_changed == 0) {
			phba->num_disc_nodes = 0;
			/* go thru ADISC list and issue ELS ADISCs */
			list_for_each(pos, &phba->fc_adisc_list) {
				ndlp = list_entry(pos, LPFC_NODELIST_t,
						  nlp_listp);
				lpfc_issue_els_adisc(phba, ndlp, 0);
				ndlp->nlp_flag |= NLP_DISC_NODE;
				phba->num_disc_nodes++;
				if (phba->num_disc_nodes >=
				    clp[LPFC_CFG_DISC_THREADS].a_current) {
					if (phba->fc_adisc_cnt >
					    phba->num_disc_nodes)
						phba->fc_flag |= FC_NLP_MORE;
					break;
				}
			}
			return;
		}
		/* If the did changed, force PLOGI discovery on all NPorts scheduled for ADISC */
		if (!list_empty(&phba->fc_adisc_list)) {
			list_for_each_safe(pos, next, &phba->fc_adisc_list) {
				new_ndlp = list_entry(pos, LPFC_NODELIST_t,
						      nlp_listp);
				ndlp = new_ndlp;
				lpfc_freenode(phba, ndlp);
				ndlp->nlp_state = NLP_STE_UNUSED_NODE;
				lpfc_nlp_plogi(phba, ndlp);
			}
		}
	}

	if ((phba->hba_state < LPFC_HBA_READY) && (!clear_la_pending)) {
		/* If we get here, there is nothing to ADISC */
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			phba->hba_state = LPFC_CLEAR_LA;
			lpfc_clear_la(phba, mbox);
			mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
			if (lpfc_sli_issue_mbox
			    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
			    == MBX_NOT_FINISHED) {
				lpfc_mbox_free(phba, mbox);
				lpfc_disc_flush_list(phba);
				psli->ring[(psli->ip_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->fcp_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				psli->ring[(psli->next_ring)].flag &=
				    ~LPFC_STOP_IOCB_EVENT;
				phba->hba_state = LPFC_HBA_READY;
			}
		}
	} else {
		/* go thru PLOGI list and issue ELS PLOGIs */
		phba->num_disc_nodes = 0;
		if (phba->fc_plogi_cnt) {
			list_for_each(pos, &phba->fc_plogi_list) {
				ndlp = list_entry(pos, LPFC_NODELIST_t,
						  nlp_listp);
				if (ndlp->nlp_state == NLP_STE_UNUSED_NODE) {
					ndlp->nlp_state = NLP_STE_PLOGI_ISSUE;
					lpfc_issue_els_plogi(phba, ndlp, 0);
					ndlp->nlp_flag |= NLP_DISC_NODE;
					phba->num_disc_nodes++;
					if (phba->num_disc_nodes >=
					    clp[LPFC_CFG_DISC_THREADS].
					    a_current) {
						if (phba->fc_plogi_cnt >
						    phba->num_disc_nodes)
							phba->fc_flag |=
							    FC_NLP_MORE;
						break;
					}
				}
			}
		} else {
			if (phba->fc_flag & FC_RSCN_MODE) {
				/* Check to see if more RSCNs came in while we
				 * were processing this one.
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
	return;
}

void
lpfc_disc_flush_list(lpfcHBA_t * phba)
{
	LPFC_NODELIST_t *ndlp, *new_ndlp;
	struct list_head *pos, *next;

	if (phba->fc_plogi_cnt) {
		list_for_each_safe(pos, next, &phba->fc_plogi_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
					  LPFC_SET_BITMASK);
			lpfc_free_tx(phba, ndlp);
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	if (phba->fc_adisc_cnt) {
		list_for_each_safe(pos, next, &phba->fc_adisc_list) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp = new_ndlp;
			lpfc_set_failmask(phba, ndlp, LPFC_DEV_DISCONNECTED,
					  LPFC_SET_BITMASK);
			lpfc_free_tx(phba, ndlp);
			lpfc_nlp_remove(phba, ndlp);
		}
	}
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_timeout
 *
 * FUNCTION: Fibre Channel driver discovery timeout routine.
 *
 * EXECUTION ENVIRONMENT: interrupt only
 *
 * CALLED FROM:
 *      Timer function
 *
 * RETURNS:  
 *      none
 */
/*****************************************************************************/
void
lpfc_disc_timeout(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_SLI_t *psli;
	LPFC_NODELIST_t *ndlp;
	LPFC_MBOXQ_t *mbox;
	lpfcCfgParam_t *clp;
	struct clk_data *clkData;
	unsigned long iflag;

	clkData = (struct clk_data *)ptr;
	phba = clkData->phba;
	if (!phba) {
		kfree(clkData);
		return;
	}

	LPFC_DRVR_LOCK(phba, iflag);
       	if (clkData->flags & TM_CANCELED) {
		list_del((struct list_head *)clkData);
		kfree(clkData);	
		goto out;
 	}

	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	clp = &phba->config[0];
	psli = &phba->sli;
	phba->fc_disctmo.function = 0;	/* timer expired */

	/* hba_state is identically LPFC_LOCAL_CFG_LINK while waiting for FAN */
	if (phba->hba_state == LPFC_LOCAL_CFG_LINK) {
		/* FAN timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0221,
				lpfc_mes0221, lpfc_msgBlk0221.msgPreambleStr);

		/* Forget about FAN, Start discovery by sending a FLOGI
		 * hba_state is identically LPFC_FLOGI while waiting for FLOGI
		 * cmpl
		 */
		phba->hba_state = LPFC_FLOGI;
		lpfc_set_disctmo(phba);
		lpfc_initial_flogi(phba);
		goto out;
	}

	/* hba_state is identically LPFC_FLOGI while waiting for FLOGI cmpl */
	if (phba->hba_state == LPFC_FLOGI) {
		/* Initial FLOGI timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0222,
				lpfc_mes0222, lpfc_msgBlk0222.msgPreambleStr);

		/* Assume no Fabric and go on with discovery.
		 * Check for outstanding ELS FLOGI to abort.
		 */

		/* FLOGI failed, so just use loop map to make discovery list */
		lpfc_disc_list_loopmap(phba);

		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* hba_state is identically LPFC_FABRIC_CFG_LINK while waiting for
	   NameServer login */
	if (phba->hba_state == LPFC_FABRIC_CFG_LINK) {
		/* Timeout while waiting for NameServer login */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0223,
				lpfc_mes0223, lpfc_msgBlk0223.msgPreambleStr);

		/* Next look for NameServer ndlp */
		if ((ndlp =
		     lpfc_findnode_did(phba,
				       (NLP_SEARCH_ALL | NLP_SEARCH_DEQUE),
				       NameServer_DID))) {
			lpfc_nlp_remove(phba, ndlp);
		}
		/* Start discovery */
		lpfc_disc_start(phba);
		goto out;
	}

	/* Check for wait for NameServer Rsp timeout */
	if (phba->hba_state == LPFC_NS_QRY) {
		/* NameServer Query timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0224,
				lpfc_mes0224,
				lpfc_msgBlk0224.msgPreambleStr,
				phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

		if ((ndlp =
		     lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
				       NameServer_DID))) {
			if (phba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
				/* Try it one more time */
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
			phba->fc_ns_retry = 0;
		}

		/* Nothing to authenticate, so CLEAR_LA right now */
		if (phba->hba_state != LPFC_CLEAR_LA) {
			if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
				phba->hba_state = LPFC_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					lpfc_mbox_free(phba, mbox);
					goto clrlaerr;
				}
			} else {
				/* Device Discovery completion error */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0226,
						lpfc_mes0226,
						lpfc_msgBlk0226.msgPreambleStr);
				phba->hba_state = LPFC_HBA_ERROR;
			}
		}
		if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
			/* Setup and issue mailbox INITIALIZE LINK command */
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
		}
		goto out;
	}

	if (phba->hba_state == LPFC_DISC_AUTH) {
		/* Node Authentication timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0227,
				lpfc_mes0227, lpfc_msgBlk0227.msgPreambleStr);
		lpfc_disc_flush_list(phba);
		if (phba->hba_state != LPFC_CLEAR_LA) {
			if ((mbox = lpfc_mbox_alloc(phba, MEM_PRI))) {
				phba->hba_state = LPFC_CLEAR_LA;
				lpfc_clear_la(phba, mbox);
				mbox->mbox_cmpl = lpfc_mbx_cmpl_clear_la;
				if (lpfc_sli_issue_mbox
				    (phba, mbox, (MBX_NOWAIT | MBX_STOP_IOCB))
				    == MBX_NOT_FINISHED) {
					lpfc_mbox_free(phba, mbox);
					goto clrlaerr;
				}
			}
		}
		goto out;
	}

	if (phba->hba_state == LPFC_CLEAR_LA) {
		/* CLEAR LA timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0228,
				lpfc_mes0228, lpfc_msgBlk0228.msgPreambleStr);
	      clrlaerr:
		lpfc_disc_flush_list(phba);
		psli->ring[(psli->ip_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->fcp_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		psli->ring[(psli->next_ring)].flag &= ~LPFC_STOP_IOCB_EVENT;
		phba->hba_state = LPFC_HBA_READY;
		goto out;
	}

	if ((phba->hba_state == LPFC_HBA_READY) &&
	    (phba->fc_flag & FC_RSCN_MODE)) {
		/* RSCN timeout */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0231,
				lpfc_mes0231,
				lpfc_msgBlk0231.msgPreambleStr,
				phba->fc_ns_retry, LPFC_MAX_NS_RETRY);

		/* Cleanup any outstanding ELS commands */
		lpfc_els_flush_cmd(phba);

		lpfc_els_flush_rscn(phba);
		lpfc_disc_flush_list(phba);
		goto out;
	}

      out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_linkdown_timeout
 *
 * FUNCTION: Fibre Channel driver linkdown timeout routine.
 *
 * EXECUTION ENVIRONMENT: interrupt only
 *
 * CALLED FROM:
 *      Timer function
 *
 * RETURNS:  
 *      none
 */
/*****************************************************************************/
void
lpfc_linkdown_timeout(unsigned long ptr)
{
	lpfcHBA_t *phba;
	struct clk_data *clkData;
	unsigned long iflag;

	clkData = (struct clk_data *)ptr;
	phba = clkData->phba;
	if (!phba) {
		kfree(clkData);
		return;
	}

	LPFC_DRVR_LOCK(phba, iflag);
       	if (clkData->flags & TM_CANCELED) {
		list_del((struct list_head *)clkData);
		kfree(clkData);	
		goto out;
 	}
	
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	/* Link Down timeout */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1306,
			lpfc_mes1306,
			lpfc_msgBlk1306.msgPreambleStr,
			phba->hba_state, phba->fc_flag, phba->fc_ns_retry);

	phba->fc_linkdown.function = 0;	/* timer expired */
	phba->fc_flag |= (FC_LD_TIMER | FC_LD_TIMEOUT);	/* indicate timeout */
	phba->hba_flag &= ~FC_LFR_ACTIVE;
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

LPFCSCSITARGET_t *
lpfc_find_target(lpfcHBA_t * phba, uint32_t tgt)
{
	LPFCSCSITARGET_t *targetp;

	targetp = phba->device_queue_hash[tgt];
	return (targetp);
}

/*****************************************************************************/
/*
 * NAME:     lpfc_find_lun
 *
 * FUNCTION: Fibre Channel bus/target/LUN to LPFCSCSILUN_t lookup
 *
 * EXECUTION ENVIRONMENT: 
 *
 * RETURNS:  
 *      ptr to desired LPFCSCSILUN_t
 */
/*****************************************************************************/
LPFCSCSILUN_t *
lpfc_find_lun(lpfcHBA_t * phba, uint32_t tgt, uint64_t lun, int create_flag)
{
	LPFC_NODELIST_t *nlp;
	LPFC_BINDLIST_t *blp;
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	lpfcCfgParam_t *clp;
	struct list_head *curr, *next;

	clp = &phba->config[0];
	targetp = phba->device_queue_hash[tgt];

	/* First see if the SCSI ID has an allocated LPFCSCSITARGET_t */
	if (targetp) {
		list_for_each_safe(curr, next, &targetp->lunlist) {
			lunp = list_entry(curr, LPFCSCSILUN_t, list);
			/* Finally see if the LUN ID has an allocated
			   LPFCSCSILUN_t */
			if (lunp->lun_id == lun) {
				return (lunp);
			}
		}
		if (create_flag) {
			goto lun_create;
		}
	} else {
		if (create_flag) {
			nlp = lpfc_findnode_scsiid(phba, tgt);
			if (nlp == 0) {
				return 0;
			}

			targetp = kmalloc(sizeof (LPFCSCSITARGET_t),
					  GFP_ATOMIC);
			if (targetp == 0) {
				return (0);
			}

			memset(targetp, 0, sizeof (LPFCSCSITARGET_t));
			INIT_LIST_HEAD(&targetp->lunlist);
			targetp->scsi_id = tgt;
			targetp->max_lun = clp[LPFC_CFG_MAX_LUN].a_current;
			targetp->pHba = phba;
			phba->device_queue_hash[tgt] = targetp;
			targetp->pcontext = nlp;
			if (nlp) {

				/* Create SCSI Target <tgt> */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0204,
						lpfc_mes0204,
						lpfc_msgBlk0204.msgPreambleStr,
						tgt);

				nlp->nlp_Target = targetp;
				if ((blp = nlp->nlp_listp_bind)) {
					blp->nlp_Target = targetp;
				}
			}
			if (clp[LPFC_CFG_DFT_TGT_Q_DEPTH].a_current) {
				lpfc_sched_target_init(targetp, (uint16_t)
						clp[LPFC_CFG_DFT_TGT_Q_DEPTH]
						       .a_current);
			} else {
				lpfc_sched_target_init(targetp, (uint16_t)
						clp[LPFC_CFG_DFT_HBA_Q_DEPTH]
						       .a_current - 10);
			}

lun_create:
			lunp = kmalloc(sizeof (LPFCSCSILUN_t), GFP_ATOMIC);
			if (lunp == 0) {
				return (0);
			}

			/* Create SCSI LUN <lun> on Target <tgt> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0205,
					lpfc_mes0205,
					lpfc_msgBlk0205.msgPreambleStr,
					(uint32_t) lun, tgt);

			memset(lunp, 0, sizeof (LPFCSCSILUN_t));
			lunp->lun_id = lun;
			/* For Schedular to retry */
			lunp->qfull_retries = lpfc_qfull_retry_count;
			lunp->pTarget = targetp;
			lunp->pHBA = phba;

			list_add_tail(&lunp->list, &targetp->lunlist);
			lpfc_sched_lun_init(lunp, (uint16_t)
					    clp[LPFC_CFG_DFT_LUN_Q_DEPTH].
					    a_current);
			return (lunp);
		}
	}
	return (0);
}

void
lpfc_disc_cmpl_rptlun(lpfcHBA_t * phba,
		      LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	DMABUF_t *mp;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;
	FCP_RSP *fcprsp;
	IOCB_t *iocb;
	uint8_t *datap;
	uint32_t *datap32;
	uint32_t rptLunLen;
	uint32_t max, lun, i;
	struct list_head *curr, *next;

	lpfc_cmd = cmdiocb->context1;
	mp = cmdiocb->context2;
	targetp = lpfc_cmd->pLun->pTarget;
	ndlp = (LPFC_NODELIST_t *) targetp->pcontext;
	iocb = &lpfc_cmd->cur_iocbq.iocb;
	fcprsp = lpfc_cmd->fcp_rsp;
	clp = &phba->config[0];

	if (ndlp == 0) {
		targetp->rptLunState = REPORT_LUN_ERRORED;
		targetp->targetFlags &= ~FC_RETRY_RPTLUN;
		if (targetp->tmofunc.function) {
			lpfc_stop_timer((struct clk_data *)
					targetp->tmofunc.data);
		}
		lpfc_page_free(phba, mp->virt, mp->phys);
		kfree(mp);
		lpfc_free_scsi_buf(lpfc_cmd);
		return;
	}

	/* Report Lun completes on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0206,
			lpfc_mes0206,
			lpfc_msgBlk0206.msgPreambleStr,
			ndlp->nlp_DID, iocb->ulpStatus, fcprsp->rspStatus2,
			fcprsp->rspStatus3, ndlp->nlp_failMask);

	if (targetp) {

		targetp->max_lun = clp[LPFC_CFG_MAX_LUN].a_current;

		if (((iocb->ulpStatus == IOSTAT_SUCCESS) &&
		     (fcprsp->rspStatus3 == SCSI_STAT_GOOD)) ||
		    ((iocb->ulpStatus == IOSTAT_FCP_RSP_ERROR) &&
		     (fcprsp->rspStatus2 & RESID_UNDER) &&
		     (fcprsp->rspStatus3 == SCSI_STAT_GOOD))) {

			datap = (uint8_t *) mp->virt;
			/*
			 * Assume all LUNs use same addressing mode as LUN0
			 */
			i = (uint32_t) ((datap[8] & 0xc0) >> 6);
			switch (i) {
			case PERIPHERAL_DEVICE_ADDRESSING:
				targetp->addrMode =
				    PERIPHERAL_DEVICE_ADDRESSING;
				break;
			case VOLUME_SET_ADDRESSING:
				targetp->addrMode = VOLUME_SET_ADDRESSING;
				break;
			case LOGICAL_UNIT_ADDRESSING:	/* Not supported */
			default:
				/* Unsupported Addressing Mode <i> on NPort
				   <nlp_DID> Tgt <sid> */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0249,
						lpfc_mes0249,
						lpfc_msgBlk0249.msgPreambleStr,
						i, ndlp->nlp_DID,
						ndlp->nlp_sid);
				targetp->addrMode =
				    PERIPHERAL_DEVICE_ADDRESSING;
				break;
			}

			i = 0;
			datap32 = (uint32_t *) mp->virt;
			rptLunLen = *datap32;
			rptLunLen = be32_to_cpu(rptLunLen);
			/* search for the max lun */
			max = 0;
			for (i = 0;
			     ((i < rptLunLen) && (i < (8 * LPFC_MAX_LUN)));
			     i += 8) {
				datap32 += 2;
				lun = (((*datap32) >> FC_LUN_SHIFT) & 0xff);
				if (lun > max)
					max = lun;
			}
			if (i) {
				targetp->max_lun = max + 1;
			} else {
				targetp->max_lun = 0;
			}

			targetp->rptLunState = REPORT_LUN_COMPLETE;
			targetp->targetFlags &= ~FC_RETRY_RPTLUN;
			if (targetp->tmofunc.function) {
				lpfc_stop_timer((struct clk_data *)
						targetp->tmofunc.data);
			}

			/* The lpfc_issue_rptlun function does not re-use the
			 * buffer pointed to by targetp->RptLunData.  It always
			 * allocates a new one and frees the old buffer.
			 */
			if (targetp->RptLunData) {
				lpfc_page_free(phba,
					       targetp->RptLunData->virt,
					       targetp->RptLunData->phys);
				kfree(targetp->RptLunData);
			}
			targetp->RptLunData = mp;

			lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
					  LPFC_CLR_BITMASK);
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				lunp->pnode = (LPFC_NODELIST_t *) ndlp;
			}
		} else {
			/* Retry RPTLUN */
			if (ndlp
			    && (!(ndlp->nlp_failMask & LPFC_DEV_FATAL_ERROR))
			    && (!(targetp->targetFlags & FC_RETRY_RPTLUN))) {
				targetp->targetFlags |= FC_RETRY_RPTLUN;
				lpfc_start_timer(phba, 1, &targetp->rptlunfunc,
						 lpfc_disc_retry_rptlun,
						 (unsigned long)targetp, 0);
			} else {
				targetp->rptLunState = REPORT_LUN_ERRORED;

				/* If ReportLun failed, then we allow only lun 0
				 * on this target.  This way, the driver won't
				 * create Processor devices when JBOD failed
				 * ReportLun and lun-skip is turned ON.
				 */
				targetp->max_lun = 1;

				targetp->targetFlags &= ~FC_RETRY_RPTLUN;
				if (targetp->tmofunc.function) {
					lpfc_stop_timer((struct clk_data *)
							targetp->tmofunc.data);
				}
				lpfc_set_failmask(phba, ndlp, LPFC_DEV_RPTLUN,
						  LPFC_CLR_BITMASK);

				list_for_each_safe(curr, next,
						   &targetp->lunlist) {
					lunp = list_entry(curr, LPFCSCSILUN_t,
							  list);
					lunp->pnode = (LPFC_NODELIST_t *) ndlp;
				}
			}
		}
	}

	/* We cannot free RptLunData buffer if we already save it in 
	 * the target structure */
	if (mp != targetp->RptLunData) {
		lpfc_page_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
	lpfc_free_scsi_buf(lpfc_cmd);
	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_retry_rptlun
 *
 * FUNCTION: Try to send report lun again.  Note that NODELIST could have
 *           changed from the last failed repotlun cmd.  That's why we have
 *           to get the latest ndlp before calling lpfc_disc_issue_rptlun. 
 *
 * EXECUTION ENVIRONMENT: 
 *           During device discovery
 *
 */
/*****************************************************************************/
void
lpfc_disc_retry_rptlun(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFCSCSITARGET_t *targetp;
	LPFC_NODELIST_t *ndlp;
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

	targetp = (LPFCSCSITARGET_t *) clkData->clData1;
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	ndlp = (LPFC_NODELIST_t *) targetp->pcontext;
	if (ndlp) {
		lpfc_disc_issue_rptlun(phba, ndlp);
	}
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
}

/*****************************************************************************/
/*
 * NAME:     lpfc_disc_issue_rptlun
 *
 * FUNCTION: Issue a RPTLUN SCSI command to a newly mapped FCP device
 *           to determine LUN addressing mode
 *
 * EXECUTION ENVIRONMENT: 
 *           During device discovery
 *
 */
/*****************************************************************************/
int
lpfc_disc_issue_rptlun(lpfcHBA_t * phba, LPFC_NODELIST_t * nlp)
{
	LPFC_SLI_t *psli;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_IOCBQ_t *piocbq;

	/* Issue Report LUN on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0207,
			lpfc_mes0207,
			lpfc_msgBlk0207.msgPreambleStr,
			nlp->nlp_DID, nlp->nlp_failMask, nlp->nlp_state,
			nlp->nlp_rpi);

	psli = &phba->sli;
	lpfc_cmd = lpfc_build_scsi_cmd(phba, nlp, FCP_SCSI_REPORT_LUNS, 0);
	if (lpfc_cmd) {
		piocbq = &lpfc_cmd->cur_iocbq;
		piocbq->iocb_cmpl = lpfc_disc_cmpl_rptlun;

		if (lpfc_sli_issue_iocb(phba, &psli->ring[psli->fcp_ring],
					piocbq,
					SLI_IOCB_USE_TXQ) == IOCB_ERROR) {
			lpfc_page_free(phba,
				       ((DMABUF_t *) piocbq->context2)->virt,
				       ((DMABUF_t *) piocbq->context2)->phys);
			kfree(piocbq->context2);
			lpfc_free_scsi_buf(lpfc_cmd);
			return (1);
		}
		if (lpfc_cmd->pLun->pTarget) {
			lpfc_cmd->pLun->pTarget->rptLunState =
			    REPORT_LUN_ONGOING;
		}
	}
	return (0);
}

/*
 *   lpfc_set_failmask
 *   Set, or clear, failMask bits in LPFC_NODELIST_t
 */
void
lpfc_set_failmask(lpfcHBA_t * phba,
		  LPFC_NODELIST_t * ndlp, uint32_t bitmask, uint32_t flag)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	uint32_t oldmask;
	uint32_t changed;
	struct list_head *curr, *next;

	/* Failmask change on NPort <nlp_DID> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0208,
			lpfc_mes0208,
			lpfc_msgBlk0208.msgPreambleStr,
			ndlp->nlp_DID, ndlp->nlp_failMask, bitmask, flag);

	targetp = ndlp->nlp_Target;
	if (flag == LPFC_SET_BITMASK) {
		oldmask = ndlp->nlp_failMask;
		/* Set failMask event */
		ndlp->nlp_failMask |= bitmask;
		if (oldmask != ndlp->nlp_failMask) {
			changed = 1;
		} else {
			changed = 0;
		}

		if (oldmask == 0) {

			/* Pause the scheduler if this is a FCP node */
			if (targetp) {
				lpfc_sched_pause_target(targetp);
			}
		}
	} else {
		/* Clear failMask event */
		ndlp->nlp_failMask &= ~bitmask;
		changed = 1;
	}

	/* If mask has changed, there may be more to do */
	if (changed) {
		/* If the map was / is a mapped target, probagate change to 
		 * all LPFCSCSILUN_t's
		 */
		if (targetp) {
			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t, list);
				if (flag == LPFC_SET_BITMASK) {
					/* Set failMask event */
					lunp->failMask |= bitmask;
				} else {
					/* Clear failMask event */
					lunp->failMask &= ~bitmask;
				}
			}

			/* If the failMask changes to 0, resume the scheduler */
			if (ndlp->nlp_failMask == 0) {
				lpfc_sched_continue_target(targetp);
			}
		}
	}
	return;
}

/*
 *  Ignore completion for all IOCBs on tx and txcmpl queue for ELS 
 *  ring the match the sppecified nodelist.
 */
void
lpfc_free_tx(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	IOCB_t *icmd;
	LPFC_SLI_RING_t *pring;
	struct list_head *curr, *next;
	struct lpfc_dmabuf *pCmd, *pRsp;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			list_del(&iocb->list);
			pring->txq_cnt--;
			lpfc_els_free_iocb(phba, iocb);
		}
	}

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		if (iocb->context1 != ndlp) {
			continue;
		}
		icmd = &iocb->iocb;
		if ((icmd->ulpCommand == CMD_ELS_REQUEST64_CR) ||
		    (icmd->ulpCommand == CMD_XMIT_ELS_RSP64_CX)) {

			iocb->iocb_cmpl = 0;
			/* context2 = cmd, context2->next = rsp, context3 =
			   bpl */
			pCmd = (struct lpfc_dmabuf *)iocb->context2;
			if (pCmd) {
				/* Free the response IOCB before handling the
				   command. */
				pRsp = list_entry(pCmd->list.next, DMABUF_t, list); 
				if (pRsp) {
					/* Delay before releasing rsp buffer to
					 * give UNREG mbox a chance to take
					 * effect.
					 */
					list_add(&pRsp->list, &phba->free_buf_list);
					lpfc_start_timer(phba, 1,
							 &phba->buf_tmo,
							 lpfc_put_buf,
							 (unsigned
							  long)pRsp, 0);
				}
				lpfc_mbuf_free(phba,
					       pCmd->virt, 
					       pCmd->phys);
				kfree(pCmd);
			}

			if (iocb->context3) {
				lpfc_mbuf_free(phba,
					       ((DMABUF_t *) iocb->context3)->
					       virt,
					       ((DMABUF_t *) iocb->context3)->
					       phys);
				kfree(iocb->context3);
			}
		}
	}

	return;
}

/*****************************************************************************/
/*
 * NAME:     lpfc_put_buf
 *
 * FUNCTION: Fibre Channel driver delayed buffer release routine.
 *
 * EXECUTION ENVIRONMENT: interrupt only
 *
 * CALLED FROM:
 *      Timer function
 *
 * RETURNS:  
 *      none
 */
/*****************************************************************************/
void
lpfc_put_buf(unsigned long ptr)
{
	lpfcHBA_t *phba;
	struct clk_data *clkData;
	unsigned long iflag;
	struct lpfc_dmabuf *some_buf;

	clkData = (struct clk_data *)ptr;
	phba = clkData->phba;
	LPFC_DRVR_LOCK(phba, iflag);
       	if (clkData->flags & TM_CANCELED) {
		list_del((struct list_head *)clkData);
		kfree(clkData);	
		goto out;
        }

	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);

	some_buf = (struct lpfc_dmabuf *)clkData->clData1;
	list_del(&some_buf->list);
	lpfc_mbuf_free(phba, some_buf->virt,
		       some_buf->phys);
	kfree((void *)some_buf);
	kfree(clkData);
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

/*
 * This routine handles processing a NameServer REG_LOGIN mailbox
 * command upon completion. It is setup in the LPFC_MBOXQ
 * as the completion routine when the command is
 * handed off to the SLI layer.
 */
void
lpfc_mbx_cmpl_fdmi_reg_login(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_NODELIST_t *ndlp;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	psli = &phba->sli;
	mb = &pmb->mb;

	ndlp = (LPFC_NODELIST_t *) pmb->context2;
	mp = (DMABUF_t *) (pmb->context1);

	pmb->context1 = 0;
	ndlp->nlp_rpi = mb->un.varWords[0];
	ndlp->nlp_type |= NLP_FABRIC;
	lpfc_nlp_unmapped(phba, ndlp);
	ndlp->nlp_state = NLP_STE_PRLI_COMPL;

	/* Start issuing Fabric-Device Management Interface (FDMI)
	 * command to 0xfffffa (FDMI well known port)
	 */
	if (clp[LPFC_CFG_FDMI_ON].a_current == 1) {
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	} else {
		/*
		 * Delay issuing FDMI command if fdmi-on=2
		 * (supporting RPA/hostnmae)
		 */
		lpfc_start_timer(phba, 60, &phba->fc_fdmitmo, lpfc_fdmi_tmo,
				 (unsigned long)ndlp, (unsigned long)0);
	}

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, pmb);

	return;
}

/*
 * This routine finds a node by sequentially searching each of the node lists
 * maintained in the hba structure for that node that contains the caller's
 * rpi.  If found, the node list pointer is returned.  Otherwise NULL is
 * returned.
 */
LPFC_NODELIST_t *
lpfc_findnode_rpi(lpfcHBA_t * phba, uint16_t rpi)
{
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos, *next;

	/*
	 * The lpfc_els module calls this routine for GEN_REQUEST iocbs. 
	 * Start the sequential search with the plogi list. 
	 */
	list_for_each_safe(pos, next, &phba->fc_plogi_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (ndlp->nlp_rpi == rpi)
			return ndlp;
	}

	list_for_each_safe(pos, next, &phba->fc_adisc_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (ndlp->nlp_rpi == rpi)
			return ndlp;
	}

	list_for_each_safe(pos, next, &phba->fc_nlpunmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (ndlp->nlp_rpi == rpi)
			return ndlp;
	}

	list_for_each_safe(pos, next, &phba->fc_nlpmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (ndlp->nlp_rpi == rpi)
			return ndlp;
	}

	/* No match found */
	return ((LPFC_NODELIST_t *) 0);
}
