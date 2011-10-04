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
 * $Id: lpfc_debug_ioctl.c 328 2005-05-03 15:20:43Z sf_support $
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/unistd.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <sd.h>			/* From drivers/scsi */
#include <hosts.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_fcp.h"
#include "lpfc_scsi.h"
#include "lpfc_diag.h"
#include "lpfc_ioctl.h"
#include "lpfc_diag.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"
#include "lpfc_debug_ioctl.h"

extern lpfcDRVR_t lpfcDRVR;

int
lpfc_process_ioctl_dfc(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{

	int rc = -1;
	uint32_t outshift;
	uint32_t total_mem;
	struct dfc_info *di;
	void   *dataout;
	unsigned long iflag;
        
	extern struct dfc dfc;

	di = &dfc.dfc_info[cip->lpfc_brd];
	/* dfc_ioctl entry */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1600,	/* ptr to msg structure */
		lpfc_mes1600,			/* ptr to msg */
		lpfc_msgBlk1600.msgPreambleStr,	/* begin varargs */
		cip->lpfc_cmd,
		(ulong) cip->lpfc_arg1,
		(ulong) cip->lpfc_arg2,
		cip->lpfc_outsz);		/* end varargs */

	outshift = 0;
	if (cip->lpfc_outsz >= 4096) {

		/* Allocate memory for ioctl data. If buffer is bigger than 64k, then we
		 * allocate 64k and re-use that buffer over and over to xfer the whole 
		 * block. This is because Linux kernel has a problem allocating more than
		 * 120k of kernel space memory. Saw problem with GET_FCPTARGETMAPPING...
		 */
		if (cip->lpfc_outsz <= (64 * 1024))
			total_mem = cip->lpfc_outsz;
		else
			total_mem = 64 * 1024;		
	} else {
		/* Allocate memory for ioctl data */
		total_mem = 4096;
	}


	dataout = kmalloc(total_mem, GFP_ATOMIC);
	if (!dataout)
		return (ENOMEM);

	di->fc_refcnt++;
	switch (cip->lpfc_cmd) {

	/* Debug Interface Support - dfc */
	case LPFC_LIP:
		rc = lpfc_ioctl_lip(phba, cip, dataout);
		break;

	case LPFC_RESET_QDEPTH:
		rc = lpfc_reset_dev_q_depth(phba);
		break;

	case LPFC_OUTFCPIO:
		rc = lpfc_ioctl_outfcpio(phba, cip, dataout);
		break;

	case LPFC_SEND_ELS:
		rc = lpfc_ioctl_send_els(phba, cip);
		break;

	case LPFC_INST:
		rc = lpfc_ioctl_inst(phba, cip, dataout);
		break;

	case LPFC_READ_BPLIST:
		rc = lpfc_ioctl_read_bplist(phba, cip, dataout, total_mem);
		break;

	case LPFC_LISTN:
		rc = lpfc_ioctl_listn(phba, cip, dataout, total_mem);
		break;

	case LPFC_RESET:
		rc = lpfc_ioctl_reset(phba, cip);
		break;

	case LPFC_READ_HBA:
		rc = lpfc_ioctl_read_hba(phba, cip, dataout, total_mem);
		break;

	case LPFC_STAT:
		rc = lpfc_ioctl_stat(phba, cip, dataout);
		break;

	case LPFC_DEVP:
		rc = lpfc_ioctl_devp(phba, cip, dataout);
		break;
	}

	di->fc_refcnt--;

	/* dfc_ioctl exit */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1601,	/* ptr to msg structure */
		lpfc_mes1601,			/* ptr to msg */
		lpfc_msgBlk1601.msgPreambleStr,	/* begin varargs */
		rc,
		cip->lpfc_outsz,
		(uint32_t) ((ulong) cip->lpfc_dataout));	/* end varargs */


	/* Copy data to user space config method */
	if (rc == 0) {
		if (cip->lpfc_outsz) {
			LPFC_DRVR_UNLOCK(phba, iflag);
			if (copy_to_user
			    ((uint8_t *) cip->lpfc_dataout,
			     (uint8_t *) dataout, (int)cip->lpfc_outsz)) {
				rc = EIO;
			}
			LPFC_DRVR_LOCK(phba, iflag);
		}
	}

	kfree(dataout);
	return(rc);
}

int
lpfc_ioctl_lip(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	lpfcCfgParam_t *clp;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus;
	int i, rc;
	unsigned long iflag;

	clp = &phba->config[0];
	psli = &phba->sli;

	rc = 0;

	mbxstatus = MBXERR_ERROR;
	if (phba->hba_state == LPFC_HBA_READY) {

		if ((pmboxq = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
			return ENOMEM;
		}

		/* The HBA is reporting ready.  Pause the scheduler so that
		 * all outstanding I/Os complete before LIPing.
		 */
		lpfc_sched_pause_hba(phba);

		i = 0;
		pring = &psli->ring[psli->fcp_ring];
		while (pring->txcmplq_cnt) {
			if (i++ > 500) {	/* wait up to 5 seconds */
				break;
			}

			LPFC_DRVR_UNLOCK(phba, iflag);
			mdelay(10);
			LPFC_DRVR_LOCK(phba, iflag);
		}
		memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
		lpfc_init_link(phba, pmboxq, clp[LPFC_CFG_TOPOLOGY].a_current,
			       clp[LPFC_CFG_LINK_SPEED].a_current);

		mbxstatus =
		    lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			pmboxq->mbox_cmpl = 0;
		} else {
			lpfc_mbox_free(phba, pmboxq);
		}

		lpfc_sched_continue_hba(phba);
	}

	memcpy(dataout, (char *)&mbxstatus, sizeof (uint16_t));

	return (rc);
}

int
copy_sli_info(dfcsli_t * pdfcsli, LPFC_SLI_t * psli)
{
	int i, j;

	for (i = 0; i < LPFC_MAX_RING; ++i) {
		for (j = 0; j < LPFC_MAX_RING_MASK; ++j) {
			pdfcsli->sliinit.ringinit[i].prt[j].rctl =
				psli->sliinit.ringinit[i].prt[j].rctl;
			pdfcsli->sliinit.ringinit[i].prt[j].type =
				psli->sliinit.ringinit[i].prt[j].type;
		}
		pdfcsli->sliinit.ringinit[i].num_mask =
			psli->sliinit.ringinit[i].num_mask;
		pdfcsli->sliinit.ringinit[i].iotag_ctr =
			psli->sliinit.ringinit[i].iotag_ctr;
		pdfcsli->sliinit.ringinit[i].numCiocb =
			psli->sliinit.ringinit[i].numCiocb;
		pdfcsli->sliinit.ringinit[i].numRiocb =
			psli->sliinit.ringinit[i].numRiocb;
	}
	pdfcsli->sliinit.num_rings = psli->sliinit.num_rings;
	pdfcsli->sliinit.sli_flag = psli->sliinit.sli_flag;
	pdfcsli->MBhostaddr.addrlo =
		(uint64_t)((unsigned long)psli->MBhostaddr) & 0xffffffff;
	pdfcsli->MBhostaddr.addrhi =
		(uint64_t)((unsigned long)psli->MBhostaddr) >> 32;
	for (i = 0; i < LPFC_MAX_RING; ++i) {
		pdfcsli->ring[i].rspidx = psli->ring[i].rspidx;
		pdfcsli->ring[i].cmdidx = psli->ring[i].cmdidx;
		pdfcsli->ring[i].txq_cnt = psli->ring[i].txq_cnt;
		pdfcsli->ring[i].txq_max = psli->ring[i].txq_max;
		pdfcsli->ring[i].txcmplq_cnt = psli->ring[i].txcmplq_cnt;
		pdfcsli->ring[i].txcmplq_max = psli->ring[i].txcmplq_max;
		pdfcsli->ring[i].cmdringaddr.addrlo =
			(uint64_t)((unsigned long)psli->ring[i].cmdringaddr)
			& 0xffffffff;
		pdfcsli->ring[i].cmdringaddr.addrhi =
			(uint64_t)((unsigned long)psli->ring[i].cmdringaddr)
			>> 32;
		pdfcsli->ring[i].rspringaddr.addrlo =
			(uint64_t)((unsigned long)psli->ring[i].rspringaddr)
			& 0xffffffff;
		pdfcsli->ring[i].rspringaddr.addrhi =
			(uint64_t)((unsigned long)psli->ring[i].rspringaddr)
			>> 32;
		pdfcsli->ring[i].missbufcnt = psli->ring[i].missbufcnt;
		pdfcsli->ring[i].postbufq_cnt = psli->ring[i].postbufq_cnt;
		pdfcsli->ring[i].postbufq_max = psli->ring[i].postbufq_max;
	}
	pdfcsli->mboxq_cnt = psli->mboxq_cnt;
	pdfcsli->mboxq_max = psli->mboxq_max;
	for (i = 0; i < LPFC_MAX_RING; ++i) {
		pdfcsli->slistat.iocbEvent[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbEvent[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbEvent[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbEvent[i])
			>> 32;
		pdfcsli->slistat.iocbCmd[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbCmd[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbCmd[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbCmd[i])
			>> 32;
		pdfcsli->slistat.iocbRsp[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbRsp[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbRsp[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbRsp[i])
			>> 32;
		pdfcsli->slistat.iocbCmdFull[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbCmdFull[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbCmdFull[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbCmdFull[i])
			>> 32;
		pdfcsli->slistat.iocbCmdEmpty[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbCmdEmpty[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbCmdEmpty[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbCmdEmpty[i])
			>> 32;
		pdfcsli->slistat.iocbRspFull[i].lo =
			(uint64_t)((unsigned long)psli->slistat.iocbRspFull[i])
			& 0xffffffff;
		pdfcsli->slistat.iocbRspFull[i].hi =
			(uint64_t)((unsigned long)psli->slistat.iocbRspFull[i])
			>> 32;
	}
	pdfcsli->slistat.mboxStatErr.lo =
		(uint64_t)((unsigned long)psli->slistat.mboxStatErr)
		& 0xffffffff;
	pdfcsli->slistat.mboxStatErr.hi =
		(uint64_t)((unsigned long)psli->slistat.mboxStatErr)
		>> 32;
	pdfcsli->slistat.mboxCmd.lo =
		(uint64_t)((unsigned long)psli->slistat.mboxCmd)
		& 0xffffffff;
	pdfcsli->slistat.mboxCmd.hi =
		(uint64_t)((unsigned long)psli->slistat.mboxCmd)
		>> 32;
	pdfcsli->slistat.sliIntr.lo =
		(uint64_t)((unsigned long)psli->slistat.sliIntr)
		& 0xffffffff;
	pdfcsli->slistat.sliIntr.hi =
		(uint64_t)((unsigned long)psli->slistat.sliIntr)
		>> 32;
	pdfcsli->slistat.errAttnEvent = psli->slistat.errAttnEvent;
	pdfcsli->slistat.linkEvent = psli->slistat.linkEvent;
	pdfcsli->fcp_ring = (uint32_t)psli->fcp_ring;
	return (0);
}

int
lpfc_ioctl_outfcpio(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	LPFCSCSILUN_t *lunp;
	LPFCSCSITARGET_t *targetp;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *iocb;
	LPFC_IOCBQ_t *next_iocb;
	IOCB_t *cmd;
	uint32_t tgt, lun;
	struct out_fcp_devp *dp;
	int max;
	LPFC_SLI_t *psli;
	dfcsli_t *pdfcsli;
	int rc = 0;
	uint64_t count = 0;
	struct list_head *curr, *next;
	struct list_head *curr_lun, *next_lun;
	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];

	pdfcsli = (dfcsli_t*) dataout;
	copy_sli_info(pdfcsli, psli);

	dp = (struct out_fcp_devp *)(pdfcsli + 1);
	max = cip->lpfc_outsz - sizeof (LPFC_SLI_t);
	max = (max / sizeof (struct out_fcp_devp));

	for (tgt = 0; tgt < MAX_FCP_TARGET; tgt++) {
		if ((targetp = phba->device_queue_hash[tgt])) {
			list_for_each_safe(curr_lun, next_lun, &targetp->lunlist) {
				lunp = list_entry(curr_lun, LPFCSCSILUN_t , list);
				lun = lunp->lun_id;
				if (count++ >= max)
					goto outio;

				dp->target = tgt;
				dp->lun = (ushort) lun;

				dp->qcmdcnt = lunp->qcmdcnt;
				dp->iodonecnt = lunp->iodonecnt;
				dp->errorcnt = lunp->errorcnt;

				dp->tx_count = 0;
				dp->txcmpl_count = 0;
				dp->delay_count = 0;

				dp->sched_count =
				    lunp->lunSched.q_cnt;
				dp->lun_qdepth = lunp->lunSched.maxOutstanding;
				dp->current_qdepth =
				    lunp->lunSched.currentOutstanding;

				/* Error matching iocb on txq or txcmplq 
				 * First check the txq.
				 */
				list_for_each_safe(curr, next, &pring->txq) {
					next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
					iocb = next_iocb;
					cmd = &iocb->iocb;

					/* Must be a FCP command */
					if ((cmd->ulpCommand !=
					     CMD_FCP_ICMND64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IWRITE64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IREAD64_CR)) {
						continue;
					}

					/* context1 MUST be a LPFC_SCSI_BUF_t */
					lpfc_cmd =
					    (LPFC_SCSI_BUF_t *) (iocb->context1);
					if ((lpfc_cmd == 0)
					    || (lpfc_cmd->scsi_target != tgt)
					    || (lpfc_cmd->scsi_lun != lun)) {
						continue;
					}
					dp->tx_count++;
				}

				/* Next check the txcmplq */
				list_for_each_safe(curr, next, &pring->txcmplq) {
					next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
					iocb = next_iocb;
					cmd = &iocb->iocb;

					/* Must be a FCP command */
					if ((cmd->ulpCommand !=
					     CMD_FCP_ICMND64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IWRITE64_CR)
					    && (cmd->ulpCommand !=
						CMD_FCP_IREAD64_CR)) {
						continue;
					}

					/* context1 MUST be a LPFC_SCSI_BUF_t */
					lpfc_cmd =
					    (LPFC_SCSI_BUF_t *) (iocb->context1);
					if ((lpfc_cmd == 0)
					    || (lpfc_cmd->scsi_target != tgt)
					    || (lpfc_cmd->scsi_lun != lun)) {
						continue;
					}

					dp->txcmpl_count++;
				}
				dp++;

			}
		}
	}
      outio:
	/* Use sliIntr to count number of out_fcp_devp entries */
	pdfcsli->slistat.sliIntr.lo = count & 0xffffffff;
	pdfcsli->slistat.sliIntr.hi = count >> 32;

	cip->lpfc_outsz = (sizeof (LPFC_SLI_t) +
			  (count * sizeof (struct out_fcp_devp)));

	return (rc);
}

int
lpfc_ioctl_send_els(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{

	uint32_t did;
	uint32_t opcode;
	LPFC_BINDLIST_t *blp;
	LPFC_NODELIST_t *pndl;
	int rc = 0;

	did = (ulong) cip->lpfc_arg1;
	opcode = (ulong) cip->lpfc_arg2;
	did = (did & Mask_DID);

	if (((pndl = lpfc_findnode_did(phba, NLP_SEARCH_ALL, did))) == 0) {
		if ((pndl = lpfc_nlp_alloc(phba, 0))) {
			memset((void *)pndl, 0, sizeof (LPFC_NODELIST_t));
			pndl->nlp_DID = did;
			pndl->nlp_state = NLP_STE_UNUSED_NODE;
			blp = pndl->nlp_listp_bind;
			if (blp) {
				lpfc_nlp_bind(phba, blp);
			}
		} else {
			rc = ENOMEM;
			return (rc);
		}
	}

	switch (opcode) {
	case ELS_CMD_PLOGI:
		lpfc_issue_els_plogi(phba, pndl, 0);
		break;
	case ELS_CMD_LOGO:
		lpfc_issue_els_logo(phba, pndl, 0);
		break;
	case ELS_CMD_ADISC:
		lpfc_issue_els_adisc(phba, pndl, 0);
		break;
	default:
		break;
	}

	return (rc);
}

int
lpfc_ioctl_inst(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	struct list_head *pos;
	int *p_int;
	int rc = 0;

	p_int = dataout;

	/* Store the number of devices */
	*p_int++ = (int)lpfcDRVR.num_devs;
	/* Store instance number of each device */
	list_for_each(pos, &lpfcDRVR.hba_list_head) {
		phba = list_entry(pos, lpfcHBA_t, hba_list);
		*p_int++ = phba->brd_no;
	}


	return (rc);
}

int
copy_node_list(dfcnodelist_t *pdfcndl, LPFC_NODELIST_t *pndl)
{
	pdfcndl->nlp_failMask = pndl->nlp_failMask;
	pdfcndl->nlp_type = pndl->nlp_type;
	pdfcndl->nlp_rpi = pndl->nlp_rpi;
	pdfcndl->nlp_state = pndl->nlp_state;
	pdfcndl->nlp_xri = pndl->nlp_xri;
	pdfcndl->nlp_flag = pndl->nlp_flag;
	pdfcndl->nlp_DID = pndl->nlp_DID;
	pdfcndl->nlp_oldDID = pndl->nlp_oldDID;
	memcpy(pdfcndl->nlp_portname,
	       (uint8_t *)&(pndl->nlp_portname),
	       sizeof(pdfcndl->nlp_portname));
	memcpy(pdfcndl->nlp_nodename,
	       (uint8_t *)&(pndl->nlp_nodename),
	       sizeof(pdfcndl->nlp_nodename));
	pdfcndl->nlp_sid = pndl->nlp_sid;

	return (sizeof (dfcnodelist_t));
}

int
lpfc_ioctl_listn(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip,  void *dataout, int size)
{

	dfcbindlist_t   *bpp;
	LPFC_BINDLIST_t *blp;
	dfcnodelist_t   *npp;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos;
	uint32_t offset;
	uint32_t lcnt;
	uint32_t *lcntp;
	int rc = 0;
	uint32_t total_mem = size;

	offset = (ulong) cip->lpfc_arg1;
	/* If the value of offset is 1, the driver is handling
	 * the bindlist.  Correct the total memory to account for the 
	 * bindlist's different size 
	 */
	if (offset == 1) {
		total_mem -= sizeof (LPFC_BINDLIST_t);
	} else {
		total_mem -= sizeof (LPFC_NODELIST_t);
	}

	lcnt = 0;
	switch (offset) {
	case 1:		/* bind */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		bpp =
		    (dfcbindlist_t *) ((uint8_t *) (dataout) +
					 sizeof (uint32_t));

		list_for_each(pos, &phba->fc_nlpbind_list) {
			if (total_mem <= 0)
				break;
			blp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);

			memcpy(bpp->nlp_portname,
			       (uint8_t *)&(blp->nlp_portname),
			       sizeof(bpp->nlp_portname));
			memcpy(bpp->nlp_nodename,
			       (uint8_t *)&(blp->nlp_nodename),
			       sizeof(bpp->nlp_nodename));
			bpp->nlp_bind_type = blp->nlp_bind_type;
			bpp->nlp_sid = blp->nlp_sid;
			bpp->nlp_DID = blp->nlp_DID;
			total_mem -= sizeof (dfcbindlist_t);
			bpp++;
			lcnt++;
		}
		*lcntp = lcnt;
		break;
	case 2:		/* unmap */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		npp = (dfcnodelist_t *) ((uint8_t *) (dataout) + sizeof (uint32_t));
		list_for_each(pos, &phba->fc_nlpunmap_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}
		*lcntp = lcnt;
		break;
	case 3:		/* map */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		npp = (dfcnodelist_t *) ((uint8_t *) (dataout) + sizeof (uint32_t));
		list_for_each(pos, &phba->fc_nlpmap_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);		
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;   
		}
		*lcntp = lcnt;
		break;
	case 4:		/* plogi */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		npp = (dfcnodelist_t *) ((uint8_t *) (dataout) + sizeof (uint32_t));
		
		list_for_each(pos, &phba->fc_plogi_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}
		*lcntp = lcnt;
		break;
	case 5:		/* adisc */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		npp = (dfcnodelist_t *) ((uint8_t *) (dataout) + sizeof (uint32_t));
		
		list_for_each(pos, &phba->fc_adisc_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);		
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}
		*lcntp = lcnt;
		break;
	case 6:		/* all except bind list */
		lcntp = dataout;
		memcpy(dataout, (uint8_t *) & lcnt, sizeof (uint32_t));
		npp =
		    (dfcnodelist_t *) ((uint8_t *) (dataout) +
					 sizeof (uint32_t));

		list_for_each(pos, &phba->fc_plogi_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}

		list_for_each(pos, &phba->fc_adisc_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}

		list_for_each(pos, &phba->fc_nlpunmap_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}

		list_for_each(pos, &phba->fc_nlpmap_list) {
			if (total_mem <= 0)
				break;
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			total_mem -= copy_node_list(npp, pndl);
			npp++;
			lcnt++;
		}
		*lcntp = lcnt;
		break;
	default:
		rc = ERANGE;
		break;
	}
	cip->lpfc_outsz = (sizeof (uint32_t) + (lcnt * sizeof (LPFC_NODELIST_t)));

	return (rc);
}

int
lpfc_ioctl_read_bplist(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, 
		       void *dataout, int size)
{
	LPFC_SLI_RING_t *rp;
	struct list_head *dlp;
	DMABUF_t *mm;
	uint32_t *lptr;
	LPFC_SLI_t *psli;
	int rc = 0;
	struct list_head *pos;
	uint32_t total_mem = size;

	psli = &phba->sli;
	rp = &psli->ring[LPFC_ELS_RING];	/* RING 0 */
	dlp = &rp->postbufq;
	lptr = (uint32_t *) dataout;
	total_mem -= (3 * sizeof (uint32_t));

	list_for_each(pos, &rp->postbufq) {
		if (total_mem <= 0)
			break;
		mm = list_entry(pos, DMABUF_t, list);
		if ((cip->lpfc_ring == LPFC_ELS_RING)
		    || (cip->lpfc_ring == LPFC_FCP_NEXT_RING)) {
			*lptr++ = (uint32_t) ((ulong) mm);
			*lptr++ = (uint32_t) ((ulong) mm->virt);
			*lptr++ = (uint32_t) ((ulong) mm->phys);
		}
		total_mem -= (3 * sizeof (uint32_t));
	}
	*lptr++ = 0;

	cip->lpfc_outsz = ((uint8_t *) lptr - (uint8_t *) (dataout));

	return (rc);
}

int
lpfc_ioctl_reset(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{
	uint32_t status;
	uint32_t offset;
	LPFC_SLI_t *psli;
	int rc = 0;

	psli = &phba->sli;
	offset = (ulong) cip->lpfc_arg1;
	switch (offset) {
	case 1:		/* hba */
		phba->hba_state = 0;	/* Don't skip post */
		lpfc_sli_brdreset(phba);
		phba->hba_state = LPFC_INIT_START;
		mdelay(2500);
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
		break;

	case 3:		/* target */
		lpfc_fcp_abort(phba, TARGET_RESET, (long)cip->lpfc_arg2, -1);
		break;
	case 4:		/* lun */
		lpfc_fcp_abort(phba, LUN_RESET, (long)cip->lpfc_arg2,
			       (long)cip->lpfc_arg3);
		break;
	case 5:		/* task set */
		lpfc_fcp_abort(phba, ABORT_TASK_SET,
			       (long)cip->lpfc_arg2, (long)cip->lpfc_arg3);
		break;
	case 6:		/* bus */
		lpfc_fcp_abort(phba, BUS_RESET, -1, -1);
		break;

	default:
		rc = ERANGE;
		break;
	}
	return (rc);
}

int
copy_hba_info(void *dataout, lpfcHBA_t * phba)
{
	dfchba_t * pdfchba;

	pdfchba = (dfchba_t*)dataout;

	pdfchba->hba_state = phba->hba_state;
	pdfchba->cmnds_in_flight = atomic_read(&(phba->cmnds_in_flight));
	pdfchba->fc_busflag = phba->fc_busflag;
	pdfchba->hbaSched.targetCount = phba->hbaSched.targetCount;
	pdfchba->hbaSched.maxOutstanding = phba->hbaSched.maxOutstanding;
	pdfchba->hbaSched.currentOutstanding = phba->hbaSched.currentOutstanding;
	pdfchba->hbaSched.status = phba->hbaSched.status;
	copy_sli_info(&pdfchba->sli, &phba->sli);
	return (0);
}

int
copy_stat_info(void *dataout, lpfcHBA_t * phba)
{
	dfcstats_t * pdfcstat;
	pdfcstat = (dfcstats_t*)dataout;

	pdfcstat->elsRetryExceeded = phba->fc_stat.elsRetryExceeded;
	pdfcstat->elsXmitRetry = phba->fc_stat.elsXmitRetry;
	pdfcstat->elsRcvDrop = phba->fc_stat.elsRcvDrop;
	pdfcstat->elsRcvFrame = phba->fc_stat.elsRcvFrame;
	pdfcstat->elsRcvRSCN = phba->fc_stat.elsRcvRSCN;
	pdfcstat->elsRcvRNID = phba->fc_stat.elsRcvRNID;
	pdfcstat->elsRcvFARP = phba->fc_stat.elsRcvFARP;
	pdfcstat->elsRcvFARPR = phba->fc_stat.elsRcvFARPR;
	pdfcstat->elsRcvFLOGI = phba->fc_stat.elsRcvFLOGI;
	pdfcstat->elsRcvPLOGI = phba->fc_stat.elsRcvPLOGI;
	pdfcstat->elsRcvADISC = phba->fc_stat.elsRcvADISC;
	pdfcstat->elsRcvPDISC = phba->fc_stat.elsRcvPDISC;
	pdfcstat->elsRcvFAN = phba->fc_stat.elsRcvFAN;
	pdfcstat->elsRcvLOGO = phba->fc_stat.elsRcvLOGO;
	pdfcstat->elsRcvPRLO = phba->fc_stat.elsRcvPRLO;
	pdfcstat->elsRcvPRLI = phba->fc_stat.elsRcvPRLI;
	pdfcstat->elsRcvRRQ = phba->fc_stat.elsRcvRRQ;
	pdfcstat->frameRcvBcast = phba->fc_stat.frameRcvBcast;
	pdfcstat->frameRcvMulti = phba->fc_stat.frameRcvMulti;
	pdfcstat->strayXmitCmpl = phba->fc_stat.strayXmitCmpl;
	pdfcstat->frameXmitDelay = phba->fc_stat.frameXmitDelay;
	pdfcstat->xriCmdCmpl = phba->fc_stat.xriCmdCmpl;
	pdfcstat->xriStatErr = phba->fc_stat.xriStatErr;
	pdfcstat->LinkUp = phba->fc_stat.LinkUp;
	pdfcstat->LinkDown = phba->fc_stat.LinkDown;
	pdfcstat->LinkMultiEvent = phba->fc_stat.LinkMultiEvent;
	pdfcstat->NoRcvBuf = phba->fc_stat.NoRcvBuf;
	pdfcstat->fcpCmd = phba->fc_stat.fcpCmd;
	pdfcstat->fcpCmpl = phba->fc_stat.fcpCmpl;
	pdfcstat->fcpRspErr = phba->fc_stat.fcpRspErr;
	pdfcstat->fcpRemoteStop = phba->fc_stat.fcpRemoteStop;
	pdfcstat->fcpPortRjt = phba->fc_stat.fcpPortRjt;
	pdfcstat->fcpPortBusy = phba->fc_stat.fcpPortBusy;
	pdfcstat->fcpError = phba->fc_stat.fcpError;
	return(0);
}

int
lpfc_ioctl_read_hba(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout, int size)
{

	LPFC_SLI_t *psli;
	int rc = 0;
	int cnt = 0;
	unsigned long iflag;
	void* psavbuf = 0;

	psli = &phba->sli;
	if (cip->lpfc_arg1) {

		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {

			/* The SLIM2 size is stored in the next field.  We cannot exceed
			 * the size of the dataout buffer so if it's not big enough we need
			 * to allocate a temp buffer.
			 */
			cnt = phba->slim_size;
			if (cnt > size) {
				psavbuf = dataout;
				dataout = kmalloc(cnt, GFP_ATOMIC);
				if (!dataout)
					return (ENOMEM);
			}
		} else {
			cnt = 4096;
		}

		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
			/* copy results back to user */
			lpfc_sli_pcimem_bcopy((uint32_t *) psli->MBhostaddr,
					     (uint32_t *) dataout, cnt);
		} else {
			/* First copy command data */
			lpfc_memcpy_from_slim( dataout, phba->MBslimaddr, cnt);
		}

		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user
		    ((uint8_t *) cip->lpfc_arg1, (uint8_t *) dataout,
		     cnt)) {
			rc = EIO;
		}
		LPFC_DRVR_LOCK(phba, iflag);
		if (psavbuf) {
			kfree(dataout);
			dataout = psavbuf;
			psavbuf = 0;
		}
		if (rc)
			return (rc);
	}
	copy_hba_info(dataout, phba);
	return (rc);
}

int
lpfc_ioctl_stat(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	int rc = 0;

	if ((ulong) cip->lpfc_arg1 == 1) {
		copy_hba_info(dataout, phba);
	}

	/* Copy LPFC_STAT_t */
	if ((ulong) cip->lpfc_arg1 == 2) {
		copy_stat_info(dataout, phba);
	}

	return (rc);
}

int
lpfc_ioctl_devp(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t offset, cnt;
	LPFCSCSILUN_t *dev_ptr;
	LPFC_NODELIST_t *pndl;
	LPFCSCSITARGET_t *node_ptr;
	int rc = 0;
	int i;
	struct list_head *curr, *next;

	cnt = 0;
	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;
	if ((offset >= (MAX_FCP_TARGET)) || (cnt >= 128)) {
		rc = ERANGE;
		return (rc);
	}
	node_ptr = 0;
	dev_ptr = 0;
	pndl = 0;
	memset(dataout, 0,
	       (sizeof (LPFCSCSITARGET_t) + sizeof (LPFCSCSILUN_t) +
		sizeof (LPFC_NODELIST_t)));
	rc = ENODEV;
	node_ptr = phba->device_queue_hash[offset];
	if (node_ptr) {
		dfcscsitarget_t dfc_node;

		rc = 0;
		dfc_node.context.addrlo = (uint64_t)((unsigned long)node_ptr->pcontext) & 0xffffffff;
		dfc_node.context.addrhi = (uint64_t)((unsigned long)node_ptr->pcontext) >> 32;
		dfc_node.targetSched.lunCount = node_ptr->targetSched.lunCount;
		dfc_node.targetSched.maxOutstanding = node_ptr->targetSched.maxOutstanding;
		dfc_node.targetSched.currentOutstanding = node_ptr->targetSched.currentOutstanding;
		dfc_node.targetSched.status = node_ptr->targetSched.status;
		dfc_node.max_lun = node_ptr->max_lun;
		dfc_node.scsi_id = node_ptr->scsi_id;
		dfc_node.targetFlags = node_ptr->targetFlags;
		dfc_node.addrMode = node_ptr->addrMode;
		dfc_node.rptLunState = node_ptr->rptLunState;

		cip->lpfc_outsz = sizeof (dfcscsitarget_t);
		memcpy((uint8_t *) dataout, (uint8_t *) &dfc_node,
		       (sizeof(dfcscsitarget_t)));

		list_for_each_safe(curr, next, &node_ptr->lunlist) {
			dev_ptr = list_entry(curr, LPFCSCSILUN_t, list);
			if (dev_ptr->lun_id == (uint64_t) cnt)
				break;
		}
		if (dev_ptr) {
			dfcscsilun_t dfc_dev;

			dfc_dev.lun_id.lo =
				(uint64_t)((unsigned long)dev_ptr->lun_id)
				& 0xffffffff;
			dfc_dev.lun_id.hi =
				(uint64_t)((unsigned long)dev_ptr->lun_id)
				>> 32;
			dfc_dev.lunSched.maxOutstanding = dev_ptr->lunSched.maxOutstanding;
			dfc_dev.lunSched.currentOutstanding = dev_ptr->lunSched.currentOutstanding;
			dfc_dev.lunSched.status = dev_ptr->lunSched.status;
			dfc_dev.lunFlag = dev_ptr->lunFlag;
			dfc_dev.failMask = dev_ptr->failMask;
			for (i=0; i<LPFC_INQSN_SZ; i++)
				dfc_dev.InquirySN[i] = dev_ptr->InquirySN[i];
			for (i=0; i<sizeof(dfc_dev.Vendor); i++)
				dfc_dev.Vendor[i] = dev_ptr->Vendor[i];
			for (i=0; i<sizeof(dfc_dev.Product); i++)
				dfc_dev.Product[i] = dev_ptr->Product[i];
			for (i=0; i<sizeof(dfc_dev.Rev); i++)
				dfc_dev.Rev[i] = dev_ptr->Rev[i];
			dfc_dev.sizeSN = dev_ptr->sizeSN;

			cip->lpfc_outsz += sizeof (dfcscsilun_t);
			memcpy(((uint8_t *) dataout +
				sizeof (dfcscsitarget_t)), (uint8_t *) &dfc_dev,
			       (sizeof (dfcscsilun_t)));
			pndl = (LPFC_NODELIST_t *) node_ptr->pcontext;
			if (pndl) {
				cip->lpfc_outsz += copy_node_list(
					(dfcnodelist_t*)((uint8_t *) dataout +
					 sizeof (dfcscsilun_t) +
					 sizeof (dfcscsitarget_t)), pndl);
			}
		}
	}
	return (rc);
}

int
lpfc_reset_dev_q_depth(lpfcHBA_t * phba)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *dev_ptr;
	int i;
	lpfcCfgParam_t *clp = &phba->config[0];
	struct list_head *curr, *next;

	/*
	 * Find the target and set it to default. 
	 */

	clp = &phba->config[0];
	for (i = 0; i < MAX_FCP_TARGET; ++i) {
		targetp = phba->device_queue_hash[i];
		if (targetp) {
			list_for_each_safe(curr, next, &targetp->lunlist) {
				dev_ptr = list_entry(curr, LPFCSCSILUN_t , list);
				
				dev_ptr->lunSched.maxOutstanding =
				    (ushort) clp[LPFC_DFT_LUN_Q_DEPTH].
				    a_current;
			}
		}
	}
	return (0);
}

int
lpfc_fcp_abort(lpfcHBA_t * phba, int cmd, int target, int lun)
{
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_NODELIST_t *pndl;
	uint32_t flag;
	int ret = 0;
	int i = 0;

	flag = LPFC_EXTERNAL_RESET;
	switch (cmd) {
	case BUS_RESET:

		{
			for (i = 0; i < MAX_FCP_TARGET; i++) {
				pndl = lpfc_findnode_scsiid(phba, i);
				if (pndl) {
					lpfc_cmd = lpfc_get_scsi_buf(phba);
					if (lpfc_cmd) {

						lpfc_cmd->scsi_hba = phba;
						lpfc_cmd->scsi_bus = 0;
						lpfc_cmd->scsi_target = i;
						lpfc_cmd->scsi_lun = 0;
						ret =
						    lpfc_scsi_tgt_reset(lpfc_cmd,
								        phba, 0,
								        i, flag);
						lpfc_free_scsi_buf(lpfc_cmd);
						lpfc_cmd = 0;
					}
				}
			}
		}
		break;
	case TARGET_RESET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				lpfc_cmd = lpfc_get_scsi_buf(phba);
				if (lpfc_cmd) {

					lpfc_cmd->scsi_hba = phba;
					lpfc_cmd->scsi_bus = 0;
					lpfc_cmd->scsi_target = target;
					lpfc_cmd->scsi_lun = 0;

					ret =
					    lpfc_scsi_tgt_reset(lpfc_cmd, phba, 0,
							        target, flag);
					lpfc_free_scsi_buf(lpfc_cmd);
					lpfc_cmd = 0;
				}
			}
		}
		break;
	case LUN_RESET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				lpfc_cmd = lpfc_get_scsi_buf(phba);
				if (lpfc_cmd) {
					lpfc_cmd->scsi_hba = phba;
					lpfc_cmd->scsi_bus = 0;
					lpfc_cmd->scsi_target = target;
					lpfc_cmd->scsi_lun = lun;
					ret =
					    lpfc_scsi_lun_reset(lpfc_cmd, phba, 0,
							        target, lun,
							        (flag |
								 LPFC_ISSUE_LUN_RESET));
					lpfc_free_scsi_buf(lpfc_cmd);
					lpfc_cmd = 0;
				}
			}
		}
		break;
	case ABORT_TASK_SET:
		{
			/* Obtain node ptr */
			pndl = lpfc_findnode_scsiid(phba, target);
			if (pndl) {
				lpfc_cmd = lpfc_get_scsi_buf(phba);
				if (lpfc_cmd) {
					lpfc_cmd->scsi_hba = phba;
					lpfc_cmd->scsi_bus = 0;
					lpfc_cmd->scsi_target = target;
					lpfc_cmd->scsi_lun = lun;
					ret =
					    lpfc_scsi_lun_reset(lpfc_cmd, phba, 0,
							        target, lun,
							        (flag |
								 LPFC_ISSUE_ABORT_TSET));
					lpfc_free_scsi_buf(lpfc_cmd);
					lpfc_cmd = 0;
				}
			}
		}
		break;
	}

	return (ret);
}
