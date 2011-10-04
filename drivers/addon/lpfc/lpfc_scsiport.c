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
 * $Id: lpfc_scsiport.c 328 2005-05-03 15:20:43Z sf_support $
 */
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/blk.h>

#include <scsi.h>
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
#include "lpfc_cfgparm.h"
#include "lpfc_crtn.h"
#include "lpfc_compat.h"

extern int lpfc_inq_pqb_filter;

typedef struct lpfc_xlat_err {
	uint16_t iocb_status;
	uint16_t host_status;
	uint16_t action_flag;
} lpfc_xlat_err_t;

/* Defines for action flags */
#define LPFC_DELAY_IODONE    0x1
#define LPFC_FCPRSP_ERROR    0x2
#define LPFC_IOERR_TABLE     0x4
#define LPFC_STAT_ACTION     0x8

#define LPFC_CMD_BEING_RETRIED  0xFFFF

/* This table is indexed by the IOCB ulpStatus */

lpfc_xlat_err_t lpfc_iostat_tbl[IOSTAT_CNT] = {
/* f/w code            host_status   flag */

	{IOSTAT_SUCCESS, DID_OK, 0},	/* 0x0 */
	{IOSTAT_FCP_RSP_ERROR, DID_OK, LPFC_FCPRSP_ERROR},	/* 0x1 */
	{IOSTAT_REMOTE_STOP, DID_ERROR, 0},	/* 0x2 */
	{IOSTAT_LOCAL_REJECT, DID_ERROR, LPFC_IOERR_TABLE},	/* 0x3 */
	{IOSTAT_NPORT_RJT, DID_ERROR, LPFC_STAT_ACTION},	/* 0x4 */
	{IOSTAT_FABRIC_RJT, DID_ERROR, LPFC_STAT_ACTION},	/* 0x5 */
	{IOSTAT_NPORT_BSY, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x6 */
	{IOSTAT_FABRIC_BSY, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x7 */
	{IOSTAT_INTERMED_RSP, DID_ERROR, 0},	/* 0x8 */
	{IOSTAT_LS_RJT, DID_ERROR, 0},	/* 0x9 */
	{IOSTAT_BA_RJT, DID_ERROR, 0},	/* 0xa */
	{IOSTAT_RSVD1, DID_ERROR, 0},	/* 0xb */
	{IOSTAT_RSVD2, DID_ERROR, 0},	/* 0xc */
	{IOSTAT_RSVD3, DID_ERROR, 0},	/* 0xd */
	{IOSTAT_RSVD4, DID_ERROR, 0},	/* 0xe */
	{IOSTAT_DEFAULT, DID_ERROR, 0},	/* 0xf */
	{IOSTAT_DRIVER_REJECT, DID_ERROR, LPFC_DELAY_IODONE}	/* 0x10 */
};

/* This table is indexed by the IOCB perr.statLocalError */

lpfc_xlat_err_t lpfc_ioerr_tbl[IOERR_CNT] = {

/* f/w code                     host_status     flag */
	{0, DID_ERROR, LPFC_DELAY_IODONE},
	{IOERR_MISSING_CONTINUE, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x1  */
	{IOERR_SEQUENCE_TIMEOUT, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x2  */
	{IOERR_INTERNAL_ERROR, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x3  */
	{IOERR_INVALID_RPI, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x4  */
	{IOERR_NO_XRI, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x5  */
	{IOERR_ILLEGAL_COMMAND, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x6  */
	{IOERR_XCHG_DROPPED, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x7  */
	{IOERR_ILLEGAL_FIELD, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x8  */
	{IOERR_BAD_CONTINUE, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x9  */
	{IOERR_TOO_MANY_BUFFERS, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0xA  */
	{IOERR_RCV_BUFFER_WAITING, DID_ERROR, LPFC_DELAY_IODONE}, /* 0xB  */
	{IOERR_NO_CONNECTION, DID_ERROR, LPFC_DELAY_IODONE},	/* 0xC  */
	{IOERR_TX_DMA_FAILED, DID_ERROR, LPFC_DELAY_IODONE},	/* 0xD  */
	{IOERR_RX_DMA_FAILED, DID_ERROR, LPFC_DELAY_IODONE},	/* 0xE  */
	{IOERR_ILLEGAL_FRAME, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0xF  */
	{IOERR_EXTRA_DATA, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x10 */
	{IOERR_NO_RESOURCES, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x11 */
	{0, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x12 */
	{IOERR_ILLEGAL_LENGTH, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x13 */
	{IOERR_UNSUPPORTED_FEATURE, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x14 */
	{IOERR_ABORT_IN_PROGRESS, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x15 */
	{IOERR_ABORT_REQUESTED, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x16 */
	{IOERR_RECEIVE_BUFFER_TIMEOUT, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x17 */
	{IOERR_LOOP_OPEN_FAILURE, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x18 */
	{IOERR_RING_RESET, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x19 */
	{IOERR_LINK_DOWN, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x1A */
	{IOERR_CORRUPTED_DATA, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x1B */
	{IOERR_CORRUPTED_RPI, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x1C */
	{IOERR_OUT_OF_ORDER_DATA, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x1D */
	{IOERR_OUT_OF_ORDER_ACK, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x1E */
	{IOERR_DUP_FRAME, DID_BUS_BUSY, LPFC_DELAY_IODONE},	/* 0x1F */
	{IOERR_LINK_CONTROL_FRAME, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x20 */
	{IOERR_BAD_HOST_ADDRESS, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x21 */
	{IOERR_RCV_HDRBUF_WAITING, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x22 */
	{IOERR_MISSING_HDR_BUFFER, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x23 */
	{IOERR_MSEQ_CHAIN_CORRUPTED, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x24 */
	{IOERR_ABORTMULT_REQUESTED, DID_ERROR, LPFC_DELAY_IODONE}, /* 0x25 */
	{0, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x26 */
	{0, DID_ERROR, LPFC_DELAY_IODONE},	/* 0x27 */
	{IOERR_BUFFER_SHORTAGE, DID_BUS_BUSY, LPFC_DELAY_IODONE}, /* 0x28 */
	{IOERR_DEFAULT, DID_ERROR, LPFC_DELAY_IODONE}	/* 0x29 */

};

#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)

LPFCSCSITARGET_t *lpfc_find_target(lpfcHBA_t *, uint32_t);
int lpfc_valid_lun(LPFCSCSITARGET_t *, uint64_t);

void lpfc_scsi_add_timer(struct scsi_cmnd *, int);
int lpfc_scsi_delete_timer(struct scsi_cmnd *);
uint32_t lpfc_os_fcp_err_handle(LPFC_SCSI_BUF_t *, lpfc_xlat_err_t *);

/* Functions required by the scsiport module. */

/* This routine allocates a scsi buffer, which contains all the necessary
 * information needed to initiate a SCSI I/O. The non-DMAable region of
 * the buffer contains the area to build the IOCB. The DMAable region contains
 * the memory for the FCP CMND, FCP RSP, and the inital BPL. 
 * In addition to allocating memeory, the FCP CMND and FCP RSP BDEs are setup
 * in the BPL and the BPL BDE is setup in the IOCB.
 */
LPFC_SCSI_BUF_t *
lpfc_get_scsi_buf(lpfcHBA_t * phba)
{
	LPFC_SCSI_BUF_t *psb;
	DMABUF_t *pdma;
	ULP_BDE64 *bpl;
	IOCB_t *cmd;
	uint8_t *ptr;
	dma_addr_t pdma_phys;

	/* Get a SCSI buffer for an I/O */
	if ((psb =
	     (LPFC_SCSI_BUF_t *) kmalloc(sizeof (LPFC_SCSI_BUF_t),
					 GFP_ATOMIC)) == 0) {
		return (0);
	}
	memset(psb, 0, sizeof (LPFC_SCSI_BUF_t));

	/* Get a SCSI DMA extention for an I/O */
	/*
	 * The DMA buffer for FCP_CMND, FCP_RSP and BPL use
	 * lpfc_scsi_dma_ext_pool with size LPFC_SCSI_DMA_EXT_SIZE
	 *
	 *
	 *    The size of FCP_CMND  = 32 bytes.         
	 *    The size of FCP_RSP   = 160 bytes.         
	 *    The size of ULP_BDE64 = 12 bytes and driver can only support
	 *       LPFC_SCSI_INITIAL_BPL_SIZE (3) S/G segments for scsi data.
	 *       One ULP_BDE64 is used for each of the FCP_CMND and FCP_RSP
	 *
	 *    Total usage for each I/O use 32 + 160 + (2 * 12) +
	 *    (3 * 12) = 254 bytes ~256.
	 */
	if ((pdma = (DMABUF_t *) kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) {
		kfree(psb);
		return (0);
	}
	memset(pdma, 0, sizeof (DMABUF_t));

	INIT_LIST_HEAD(&pdma->list);

	pdma->virt = pci_pool_alloc(phba->lpfc_scsi_dma_ext_pool,
				    GFP_ATOMIC, &pdma->phys);
	if (!pdma->virt) {
		kfree(pdma);
		kfree(psb);
		return (0);
	}
	/* Save DMABUF ptr for put routine */
	psb->dma_ext = pdma;

	/* This is used to save extra BPLs that are chained to pdma.
	 * Only used if I/O has more then 65 data segments.
	 */

	/* Save virtual ptrs to FCP Command, Response, and BPL */
	ptr = (uint8_t *) pdma->virt;

	memset(ptr, 0, LPFC_SCSI_DMA_EXT_SIZE);
	psb->fcp_cmnd = (FCP_CMND *) ptr;
	ptr += sizeof (FCP_CMND);
	psb->fcp_rsp = (FCP_RSP *) ptr;
	ptr += (sizeof (FCP_RSP));
	psb->fcp_bpl = (ULP_BDE64 *) ptr;
	psb->scsi_hba = phba;

	/* Since this is for a FCP cmd, the first 2 BDEs in the BPL are always
	 * the FCP CMND and FCP RSP, so lets just set it up right here.
	 */
	bpl = psb->fcp_bpl;
	/* ptr points to physical address of FCP CMD */
	pdma_phys = pdma->phys;
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (FCP_CMND);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	/* Setup FCP RSP */
	pdma_phys += sizeof (FCP_CMND);
	bpl->addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys));
	bpl->addrLow = le32_to_cpu(putPaddrLow(pdma_phys));
	bpl->tus.f.bdeSize = sizeof (FCP_RSP);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	/* Since the IOCB for the FCP I/O is built into the LPFC_SCSI_BUF_t,
	 * lets setup what we can right here.
	 */
	pdma_phys += (sizeof (FCP_RSP));
	cmd = &psb->cur_iocbq.iocb;
	cmd->un.fcpi64.bdl.ulpIoTag32 = 0;
	cmd->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys);
	cmd->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys);
	cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
	cmd->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->ulpBdeCount = 1;
	cmd->ulpClass = CLASS3;

	return (psb);
}

/* This routine frees a scsi buffer, both DMAable and non-DMAable regions */
void
lpfc_free_scsi_buf(LPFC_SCSI_BUF_t * psb)
{
	lpfcHBA_t *phba;
	DMABUF_t *pdma;
	DMABUF_t *pbpl;
	struct list_head *curr, *next;

	if (psb) {
		phba = psb->scsi_hba;
		pdma = psb->dma_ext;
		if (pdma) {
			/* Check to see if there were any extra buffers used to
			   chain BPLs */
			list_for_each_safe(curr, next, &pdma->list) {

				pbpl = list_entry(curr, DMABUF_t, list);
				lpfc_mbuf_free(phba, pbpl->virt, pbpl->phys);
				list_del(&pbpl->list);
				kfree(pbpl);
			}
			pci_pool_free(phba->lpfc_scsi_dma_ext_pool, pdma->virt,
				      pdma->phys);
			kfree(pdma);
		}
		kfree(psb);
	}
	return;
}

LPFCSCSILUN_t *
lpfc_find_lun_device(LPFC_SCSI_BUF_t * lpfc_cmd)
{
	/*
	 * Search through the LUN list to find the LUN that has properties
	 * matching those outlined in this function's parameters. 
	 */
	return lpfc_cmd->scsi_hba->lpfc_tran_find_lun(lpfc_cmd);
}

/*
 * Generic routine used the setup and initiate a SCSI I/O.
 */
int
lpfc_scsi_cmd_start(LPFC_SCSI_BUF_t * lpfc_cmd)
{

	lpfcHBA_t *phba;
	LPFC_SLI_t *psli;
	lpfcCfgParam_t *clp;
	LPFC_IOCBQ_t *piocbq;
	LPFCSCSITARGET_t *targetp;
	IOCB_t *piocb;
	FCP_CMND *fcp_cmnd;
	LPFCSCSILUN_t *lun_device;
	LPFC_NODELIST_t *ndlp;
	int rc = 0;

	lun_device = lpfc_find_lun_device(lpfc_cmd);

	/* 
	 * Make sure the HBA is online (cable plugged) and that this target
	 *is not in an error recovery mode.
	 */
	if (lun_device == 0)
		return -1;

	ndlp = (LPFC_NODELIST_t *) lun_device->pTarget->pcontext;
	phba = lun_device->pHBA;
	clp = &phba->config[0];

	targetp =  lun_device->pTarget;
	if ((targetp->targetFlags & FC_NPR_ACTIVE) ||
	    (targetp->rptLunState == REPORT_LUN_ONGOING)) {
		/* Make sure the target is paused. */
		lpfc_sched_pause_target(lun_device->pTarget);
	} else {
		if ((lun_device->failMask & LPFC_DEV_FATAL_ERROR) &&
		    (clp[LPFC_CFG_HOLDIO].a_current == 0)){
			/* The device is lost.  Just abort the IO and let
			 * queuecommand figure out how to fail this IO.  The
			 * io resources are not mapped yet so don't call
			 * lpfc_os_return_scsi_cmd.
			 */
			return 1;
		}
	}

	/* allocate an iocb command */
	piocbq = &(lpfc_cmd->cur_iocbq);
	piocb = &piocbq->iocb;

	
	psli = &phba->sli;

	lpfc_cmd->pLun = lun_device;

	/* Note: ndlp may be 0 in recovery mode */
	lpfc_cmd->pLun->pnode = ndlp;
	lpfc_cmd->cmd_cmpl = lpfc_os_return_scsi_cmd;

	if (lpfc_os_prep_io(phba, lpfc_cmd)) {
		return 1;
	}

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * The driver provides the timeout mechanism for this command.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = lpfc_cmd->timeout;
	}

	/*
	 * Setup driver timeout, in case the command does not complete
	 * Driver timeout should be greater than ulpTimeout
	 */

	piocbq->drvrTimeout = lpfc_cmd->timeout + LPFC_DRVR_TIMEOUT;

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	putLunHigh(fcp_cmnd->fcpLunMsl, lun_device->lun_id);
	putLunLow(fcp_cmnd->fcpLunLsl, lun_device->lun_id);

	/*
	 * Setup addressing method
	 * The Logical Unit Addressing method is not supported at
	 * this current release.
	 */
	if (lun_device->pTarget->addrMode == VOLUME_SET_ADDRESSING) {
		fcp_cmnd->fcpLunMsl |= be32_to_cpu(0x40000000);
	}

	if (!(piocbq->iocb_flag & LPFC_IO_POLL)) {
		/* Pass the command on down to the SLI layer. */
		rc = lpfc_sched_submit_command(phba, lpfc_cmd);
		if(rc == 0)
			lun_device->qcmdcnt++;
	} else {

		/*
		 * Following statements has been done by the
		 * lpfc_sched_submit_command if LPFC_IO_NOINTR is not set.
		 */
		piocbq->context1 = lpfc_cmd;
		piocbq->iocb_cmpl = lpfc_sched_sli_done;

		/* put the RPI number and NODELIST info in the IOCB command */
		piocbq->iocb.ulpContext = targetp->rpi;
		if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
			piocbq->iocb.ulpFCP2Rcvy = 1;
		}
		piocbq->iocb.ulpClass = (ndlp->nlp_fcp_info & 0x0f);
		/* Get an iotag and finish setup of IOCB  */
		piocbq->iocb.ulpIoTag = lpfc_sli_next_iotag(phba,
						   &psli->ring[psli->fcp_ring]);

		/* Poll for command completion */
		rc = lpfc_sli_issue_iocb(phba, &phba->sli.ring[psli->fcp_ring],
					 piocbq,
					 (SLI_IOCB_RET_IOCB | SLI_IOCB_POLL));
	}
	return (rc);
}

int
lpfc_scsi_prep_task_mgmt_cmd(lpfcHBA_t * phba,
			     LPFC_SCSI_BUF_t * lpfc_cmd, uint8_t task_mgmt_cmd)
{

	LPFC_SLI_t *psli;
	lpfcCfgParam_t *clp;
	LPFC_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	FCP_CMND *fcp_cmnd;
	LPFCSCSILUN_t *lun_device;
	LPFC_NODELIST_t *ndlp;

	lun_device = lpfc_find_lun_device(lpfc_cmd);
	if (lun_device == 0) {
		return 0;
	}

	ndlp = (LPFC_NODELIST_t *) lun_device->pTarget->pcontext;

	if ((lun_device->failMask & LPFC_DEV_FATAL_ERROR) || (ndlp == 0)) {
		return 0;
	}

	/* allocate an iocb command */
	psli = &phba->sli;
	piocbq = &(lpfc_cmd->cur_iocbq);
	piocb = &piocbq->iocb;

	clp = &phba->config[0];

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	putLunHigh(fcp_cmnd->fcpLunMsl, lun_device->lun_id);
	putLunLow(fcp_cmnd->fcpLunLsl, lun_device->lun_id);
	if (lun_device->pTarget->addrMode == VOLUME_SET_ADDRESSING) {
		fcp_cmnd->fcpLunMsl |= be32_to_cpu(0x40000000);
	}
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;

	piocb->ulpIoTag =
	    lpfc_sli_next_iotag(phba, &phba->sli.ring[psli->fcp_ring]);
	piocb->ulpCommand = CMD_FCP_ICMND64_CR;

	piocb->ulpContext = ndlp->nlp_rpi;
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
		piocb->ulpFCP2Rcvy = 1;
	}
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else {
		piocb->ulpTimeout = lpfc_cmd->timeout;
	}

	lun_device->pnode = ndlp;
	lpfc_cmd->pLun = lun_device;

	switch (task_mgmt_cmd) {
	case LUN_RESET:
		/* Issue LUN Reset to TGT <num> LUN <num> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0703,
				lpfc_mes0703, lpfc_msgBlk0703.msgPreambleStr,
				lpfc_cmd->scsi_target, lpfc_cmd->scsi_lun,
				ndlp->nlp_rpi, ndlp->nlp_rflag);

		break;
	case ABORT_TASK_SET:
		/* Issue Abort Task Set to TGT <num> LUN <num> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0701,
				lpfc_mes0701, lpfc_msgBlk0701.msgPreambleStr,
				lpfc_cmd->scsi_target, lpfc_cmd->scsi_lun,
				ndlp->nlp_rpi, ndlp->nlp_rflag);

		break;
	case TARGET_RESET:
		/* Issue Target Reset to TGT <num> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0702,
				lpfc_mes0702, lpfc_msgBlk0702.msgPreambleStr,
				lpfc_cmd->scsi_target, ndlp->nlp_rpi,
				ndlp->nlp_rflag);
		break;
	}

	return (1);
}

/* returns:  0 if we successfully find and abort the command,
	     1 if we couldn't find the command
*/
int
lpfc_scsi_cmd_abort(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{

	/* when this function returns, the command has been aborted and
	   returned to the OS, or it was returned before we could abort
	   it */

	/* tell the scheduler to find this command on LUN queue and remove
	   it.  It's up to the scheduler to remove the command from the SLI
	   layer. */
	if (lpfc_sched_flush_command(phba, lpfc_cmd, LPFC_CMD_STATUS_ABORTED,
				     0)) {
		return 1;
	} else {
		/* couldn't find command - fail */
		return 0;
	}

}

/*
 * Returns: 1 for IOCB_SUCCESS
 */
int
lpfc_scsi_lun_reset(LPFC_SCSI_BUF_t * external_cmd,
		    lpfcHBA_t * phba,
		    uint32_t bus, uint32_t target, uint64_t lun, uint32_t flag)
{
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_IOCBQ_t *piocbq;
	LPFC_SLI_t *psli;
	LPFCSCSILUN_t *plun;
	LPFC_IOCBQ_t *piocbqrsp = 0;
	LPFC_SCSI_BUF_t *internal_cmd = 0;
	int ret = 0;

	/* Allocate command buf if internal command */
	if (!(flag & LPFC_EXTERNAL_RESET)) {
		if ((internal_cmd = lpfc_get_scsi_buf(phba)) == 0) {
			return (FAILURE);
		}

		lpfc_cmd = internal_cmd;
		lpfc_cmd->scsi_hba = phba;
		lpfc_cmd->scsi_bus = bus;
		lpfc_cmd->scsi_target = target;
		lpfc_cmd->scsi_lun = lun;
	} else {
		lpfc_cmd = external_cmd;
	}


	/*
	 * Reset a device with either a LUN reset or an ABORT TASK
	 * reset depending on the caller's flag value.
	 */
	if (flag & LPFC_ISSUE_LUN_RESET) {
		ret = lpfc_scsi_prep_task_mgmt_cmd(phba, lpfc_cmd, LUN_RESET);
	} else {
		if (flag & LPFC_ISSUE_ABORT_TSET) {
			ret =
			    lpfc_scsi_prep_task_mgmt_cmd(phba, lpfc_cmd,
							 ABORT_TASK_SET);
		} else {
			ret = 0;
		}
	}

	if (ret) {
		psli = &phba->sli;
		piocbq = &(lpfc_cmd->cur_iocbq);
		if (flag & LPFC_EXTERNAL_RESET) {

			/* get a buffer for this response IOCB command */
			if ((piocbqrsp = lpfc_iocb_alloc(phba, 0)) == 0) {
				if (internal_cmd) {
					lpfc_free_scsi_buf(internal_cmd);
					internal_cmd = 0;
					lpfc_cmd = 0;
				}
				return (ENOMEM);
			}
			memset(piocbqrsp, 0, sizeof (LPFC_IOCBQ_t));

			piocbq->iocb_flag |= LPFC_IO_POLL;
			piocbq->iocb_cmpl = lpfc_sli_wake_iocb_high_priority;
			ret = lpfc_sli_issue_iocb_wait_high_priority(phba,
					     &phba->sli.ring[psli->fcp_ring],
					     piocbq, SLI_IOCB_USE_TXQ,
					     piocbqrsp, 60);	/* 60 secs */
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;

			lpfc_cmd->result = piocbqrsp->iocb.un.ulpWord[4];
			if ((lpfc_cmd->status = piocbqrsp->iocb.ulpStatus) ==
			    IOSTAT_LOCAL_REJECT) {
				if (lpfc_cmd->result & IOERR_DRVR_MASK) {
					lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
				}
			}

			plun = lpfc_find_lun_device(lpfc_cmd);
			if (plun) {
				/* tell the scheduler to find all commands on
				 * this LUN queue and remove them.  It's up to
				 * the scheduler to remove the command from the
				 * SLI layer.
				 */
				if (!lpfc_sched_flush_lun(phba, plun,
						 LPFC_CMD_STATUS_ABORTED,0)) {

				}

			}
			/* Done with piocbqrsp, return to free list */
			if (piocbqrsp) {
				lpfc_iocb_free(phba, piocbqrsp);
			}

			/* If this was an external lun reset, issue a message
			 * indicating its completion.
			 */
			if (flag & LPFC_ISSUE_LUN_RESET) {
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0748,
						lpfc_mes0748,
						lpfc_msgBlk0748.msgPreambleStr,
						lpfc_cmd->scsi_target,
						lpfc_cmd->scsi_lun, ret,
						lpfc_cmd->status,
						lpfc_cmd->result);
			}
		} else {

			ret =
			    lpfc_sli_issue_iocb(phba,
						&phba->sli.ring[psli->fcp_ring],
						piocbq,
						SLI_IOCB_HIGH_PRIORITY |
						SLI_IOCB_RET_IOCB);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;
		}
	}

	if (internal_cmd) {
		lpfc_free_scsi_buf(internal_cmd);
		internal_cmd = 0;
		lpfc_cmd = 0;
	}

	return (ret);

}


/*
 * Returns: 1 for IOCB_SUCCESS
 */
int
lpfc_scsi_tgt_reset(LPFC_SCSI_BUF_t * external_cmd,
		    lpfcHBA_t * phba,
		    uint32_t bus, uint32_t target, uint32_t flag)
{
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_IOCBQ_t *piocbq;
	LPFC_SLI_t *psli;
	LPFC_SCHED_HBA_t *phbaSched;
	LPFCSCSITARGET_t *ptarget = 0;
	LPFCSCSILUN_t *plun;
	LPFC_IOCBQ_t *piocbqrsp = 0;
	LPFC_SCSI_BUF_t *internal_cmd = 0;
	int ret = 0;

	/* Allocate command buf if internal command */
	if (!(flag & LPFC_EXTERNAL_RESET)) {
		if ((internal_cmd = lpfc_get_scsi_buf(phba)) == 0) {
			return (FAILURE);
		}
		lpfc_cmd = internal_cmd;
		lpfc_cmd->scsi_hba = phba;
		lpfc_cmd->scsi_bus = bus;
		lpfc_cmd->scsi_target = target;
	} else {
		lpfc_cmd = external_cmd;
	}

	/*
	 * target reset a device
	 */
	ret = lpfc_scsi_prep_task_mgmt_cmd(phba, lpfc_cmd, TARGET_RESET);
	if (ret) {
		psli = &phba->sli;
		piocbq = &(lpfc_cmd->cur_iocbq);
		if (flag & LPFC_EXTERNAL_RESET) {

			/* get a buffer for this IOCB command response */
			if ((piocbqrsp = lpfc_iocb_alloc(phba, 0)) == 0) {
				if (internal_cmd) {
					lpfc_free_scsi_buf(internal_cmd);
					internal_cmd = 0;
					lpfc_cmd = 0;
				}
				return (ENOMEM);
			}
			memset(piocbqrsp, 0, sizeof (LPFC_IOCBQ_t));

			piocbq->iocb_flag |= LPFC_IO_POLL;
			piocbq->iocb_cmpl = lpfc_sli_wake_iocb_high_priority;

			if (lpfc_cmd->timeout == 0) {
				/*
				* Allot enough time to abort all outstanding IO
				* in the HBA.  The FW must finish its IO before
				* the scheduler is allowed to abort its IO.
				*/
				lpfc_cmd->timeout = 60;
			}

			ret = lpfc_sli_issue_iocb_wait_high_priority(phba,
				     &phba->sli.ring[psli->fcp_ring],
				     piocbq, SLI_IOCB_HIGH_PRIORITY,
				     piocbqrsp,
				     lpfc_cmd->timeout);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;

			lpfc_cmd->result = piocbqrsp->iocb.un.ulpWord[4];
			if ((lpfc_cmd->status = piocbqrsp->iocb.ulpStatus) ==
			    IOSTAT_LOCAL_REJECT) {
				if (lpfc_cmd->result & IOERR_DRVR_MASK) {
					lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
				}
			}

			plun = lpfc_find_lun_device(lpfc_cmd);

			/* tell the scheduler to find all commands on this Tgt
			 * queue and remove them.  It's up to the scheduler to
			 * remove the command from the SLI layer.
			 */
			if ((plun == 0) || (plun->pTarget == 0)) {
				struct list_head *pos;

				phbaSched = &phba->hbaSched;

				list_for_each(pos, &phbaSched->targetRing) {
					ptarget = list_entry(pos,
							     LPFCSCSITARGET_t,
							     listentry);
					if ((ptarget == 0)
					    || (ptarget->scsi_id == target)) {
						break;
					}
				}
			} else {
				ptarget = plun->pTarget;
			}

			if (ptarget) {
				(void) lpfc_sched_flush_target(phba, ptarget,
						LPFC_CMD_STATUS_ABORTED,0);
			}

			/* Done with piocbqrsp, return to free list */
			if (piocbqrsp) {
				lpfc_iocb_free(phba, piocbqrsp);
			}
		} else {

			ret =
			    lpfc_sli_issue_iocb(phba,
						&phba->sli.ring[psli->fcp_ring],
						piocbq,
						SLI_IOCB_HIGH_PRIORITY |
						SLI_IOCB_RET_IOCB);
			ret = (ret == IOCB_SUCCESS) ? 1 : 0;
		}
	}

	if (internal_cmd) {
		lpfc_free_scsi_buf(internal_cmd);
		internal_cmd = 0;
		lpfc_cmd = 0;
	}

	return (ret);
}

void
lpfc_scsi_lower_lun_qthrottle(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	LPFCSCSILUN_t *plun;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	plun = lpfc_cmd->pLun;

	if (plun->lunSched.maxOutstanding > LPFC_MIN_QFULL) {
		if (plun->lunSched.currentOutstanding > LPFC_MIN_QFULL) {
			/*
			 * knock the current queue throttle down to
			 * (active_io_count - 1)
			 */
			plun->lunSched.maxOutstanding =
			    plun->lunSched.currentOutstanding - 1;

			/*
			 * Delay LPFC_NO_DEVICE_DELAY seconds before sending I/O
			 * this device again.  stop_send_io will be decreament
			 * by 1 in lpfc_qthrottle_up();
			 */
			plun->stop_send_io =
			    clp[LPFC_CFG_NO_DEVICE_DELAY].a_current;

			/*
			 * Kick off the lpfc_qthrottle_up()
			 */
			if (phba->dqfull_clk.function == 0) {
				lpfc_start_timer(phba, 
					clp[LPFC_CFG_DQFULL_THROTTLE_UP_TIME]
						 .a_current,
					&phba->dqfull_clk, lpfc_qthrottle_up, 
					0, 0);
			}
		} else {
			plun->lunSched.maxOutstanding = LPFC_MIN_QFULL;
		}
	}
}

void
lpfc_qthrottle_up(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_NODELIST_t *ndlp;
	struct list_head *pos;
	LPFCSCSITARGET_t *ptarget;
	LPFCSCSILUN_t *plun = 0;
	lpfcCfgParam_t *clp;
	int reset_clock = 0;
	struct clk_data *clkData;
	unsigned long iflag;
	struct list_head *curr, *next;

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
	kfree(clkData);

	clp = &phba->config[0];
	if (clp[LPFC_CFG_DFT_LUN_Q_DEPTH].a_current <= LPFC_MIN_QFULL) {
		LPFC_DRVR_UNLOCK(phba, iflag);
		return;
	}

	if (phba->hba_state != LPFC_HBA_READY) {
		list_for_each(pos, &phba->fc_nlpmap_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ptarget = ndlp->nlp_Target;
			if (ptarget) {
				list_for_each_safe(curr, next,
						   &ptarget->lunlist) {
					plun = list_entry(curr, LPFCSCSILUN_t,
							  list);
					plun->lunSched.maxOutstanding =
					    plun->fcp_lun_queue_depth;
					plun->stop_send_io = 0;
				}
			}
		}
		LPFC_DRVR_UNLOCK(phba, iflag);
		return;
	}

	list_for_each(pos, &phba->fc_nlpmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		ptarget = ndlp->nlp_Target;
		if (ptarget) {
			list_for_each_safe(curr, next, &ptarget->lunlist) {
				plun = list_entry(curr, LPFCSCSILUN_t, list);

				if ((plun->stop_send_io == 0)
				    &&
				    (plun->lunSched.maxOutstanding <
				     plun->fcp_lun_queue_depth)
				    ) {
					/* 
					 * update lun q throttle 
					 */
					plun->lunSched.maxOutstanding +=
					    clp[LPFC_CFG_DQFULL_THROTTLE_UP_INC]
					    .a_current;

					if (plun->lunSched.maxOutstanding >
					    plun->fcp_lun_queue_depth) {
						plun->lunSched.maxOutstanding =
						    plun->fcp_lun_queue_depth;
					}

					reset_clock = 1;
				} else {
					/* 
					 * Try to reset stop_send_io 
					 */
					if (plun->stop_send_io) {
						plun->stop_send_io--;
						reset_clock = 1;
					}
				}
			}
		}
	}

	if (reset_clock) {
		lpfc_start_timer(phba,
				 clp[LPFC_CFG_DQFULL_THROTTLE_UP_TIME].
				 a_current, &phba->dqfull_clk,
				 lpfc_qthrottle_up, (unsigned long)0,
				 (unsigned long)0);
	}

out:
	LPFC_DRVR_UNLOCK(phba, iflag);

	return;
}

void
lpfc_scsi_assign_rpi(lpfcHBA_t * phba, LPFCSCSITARGET_t *targetp, uint16_t rpi)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t * pring;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFCSCSILUN_t *lun;
	struct list_head *cur_h, *next_h;
	struct list_head *cur_l, *next_l;
	struct list_head *curr, *next;
	IOCB_t *cmd = 0;
	LPFC_IOCBQ_t *iocb;

	/* If the rpi has changed for this target, there may be some I/Os
	 * already queued with the WRONG rpi. Bad things will happen if we
	 * don't clean it up here.
	 */
	if(targetp->rpi != rpi) {
		/* Check for initial assignment */
		if(targetp->rpi == 0) {
			targetp->rpi = rpi;
			return;
		}

		/* We can either error the I/O back to the upper layer, and
		 * let it retry, or we can be more sophisticated about it and
		 * change the rpi in the I/Os "on deck".
		 */
		/* First the scheduler queue */

		/* walk the list of LUNs on this target and flush each LUN.  We
		   accomplish this by pulling the first LUN off the head of the
		   queue until there aren't any LUNs left */
		list_for_each_safe(cur_h, next_h, &targetp->targetSched.lunRing) {
			lun = list_entry(cur_h, LPFCSCSILUN_t, listentry);
		
			list_for_each_safe(cur_l, next_l, &lun->lunSched.commandList) {
				LPFC_SCSI_BUF_t *command =
					list_entry(cur_l,
					   LPFC_SCSI_BUF_t,
					   listentry);
				cmd = (IOCB_t *) & (command->cur_iocbq.iocb);
				cmd->ulpContext = rpi;
			}
		}


		/* Next the txq */
		psli = &phba->sli;
		pring = &psli->ring[psli->fcp_ring];

		list_for_each_safe(curr, next, &pring->txq) {
			iocb = list_entry(curr, LPFC_IOCBQ_t, list);
			cmd = &iocb->iocb;

			/* Must be a FCP command */
			if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    	(cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    	(cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
				continue;
			}

			/* context1 MUST be a LPFC_SCSI_BUF_t */
			lpfc_cmd = (LPFC_SCSI_BUF_t *) (iocb->context1);
			if ((lpfc_cmd == 0) ||
				(lpfc_cmd->scsi_target != targetp->scsi_id)) {
				continue;
			}
			cmd->ulpContext = rpi;
		}
		targetp->rpi = rpi;
	}
	return;
}

void
lpfc_npr_timeout(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFCSCSITARGET_t *targetp;
	struct clk_data *nprClkData;
	unsigned long iflag;

	nprClkData = (struct clk_data *)ptr;
	phba = nprClkData->phba;
	LPFC_DRVR_LOCK(phba, iflag);
	if (nprClkData->flags & TM_CANCELED) {
		list_del((struct list_head *)nprClkData);
		kfree(nprClkData);  
		goto out;    
	}


	targetp = (LPFCSCSITARGET_t *) nprClkData->clData1;
	targetp->tmofunc.function = 0;

	/* Expired nodev timer */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0250,
		lpfc_mes0250,
		lpfc_msgBlk0250.msgPreambleStr,
		targetp->un.dev_did, targetp->scsi_id, targetp->rpi);

	list_del((struct list_head *)nprClkData);
	kfree(nprClkData);	
        
	targetp->targetFlags &= ~FC_NPR_ACTIVE;

	if(targetp->pcontext)
		lpfc_disc_state_machine(phba, targetp->pcontext,
			0, NLP_EVT_DEVICE_RM);

	lpfc_sched_flush_target(phba, targetp, IOSTAT_LOCAL_REJECT,
				IOERR_SLI_ABORTED);
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

/*
 * Returns: 1 for SUCCESS
 */
int
lpfc_scsi_hba_reset(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	LPFCSCSITARGET_t *ptarget;
	int ret;
	int i, errcnt = 0;

	lpfc_cmd->scsi_hba = phba;
	lpfc_cmd->scsi_bus = 0;
	lpfc_cmd->scsi_lun = 0;
	for (i = 0; i < MAX_FCP_TARGET; i++) {
		ptarget = phba->device_queue_hash[i];
		if (ptarget) {
			lpfc_cmd->scsi_target = i;
			ret = lpfc_scsi_tgt_reset(lpfc_cmd, phba, 0, i, 
						LPFC_EXTERNAL_RESET);
			if (!ret)
				errcnt++;
		}
	}

	ret = 1;
	if (errcnt)
		ret = 0;
	return ret;
}

LPFC_SCSI_BUF_t *
lpfc_build_scsi_cmd(lpfcHBA_t * phba,
		    LPFC_NODELIST_t * nlp, uint32_t scsi_cmd, uint64_t lun)
{
	LPFC_SLI_t *psli;
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	DMABUF_t *mp;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	LPFC_IOCBQ_t *piocbq;
	IOCB_t *piocb;
	FCP_CMND *fcpCmnd;
	ULP_BDE64 *bpl;
	uint32_t tgt, size;

	tgt = nlp->nlp_sid;
	lunp = lpfc_find_lun(phba, tgt, lun, 1);
	lpfc_cmd = 0;
	/* First see if the SCSI ID has an allocated LPFCSCSITARGET_t */
	if (lunp && lunp->pTarget) {
		targetp = lunp->pTarget;
		psli = &phba->sli;

		/* Get a buffer to hold SCSI data */
		if ((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) {
			return (0);
		}

		INIT_LIST_HEAD(&mp->list);
		/* Get resources to send a SCSI command */
		lpfc_cmd = lpfc_get_scsi_buf(phba);
		if (lpfc_cmd == 0) {
			kfree(mp);
			return (0);
		}
		lpfc_cmd->pLun = lunp;
		lpfc_cmd->scsi_target = tgt;
		lpfc_cmd->scsi_lun = lun;
		lpfc_cmd->timeout = 30 + phba->fcp_timeout_offset;

		/* Finish building BPL with the I/O dma ptrs.
		 * setup FCP CMND, and setup IOCB.
		 */

		fcpCmnd = lpfc_cmd->fcp_cmnd;

		putLunHigh(fcpCmnd->fcpLunMsl, lun);	/* LUN */
		putLunLow(fcpCmnd->fcpLunLsl, lun);	/* LUN */

		switch (scsi_cmd) {
		case FCP_SCSI_REPORT_LUNS:
			size = LPFC_SCSI_PAGE_BUF_SZ;
			fcpCmnd->fcpCdb[0] = scsi_cmd;
			/* 0x1000 = LPFC_SCSI_PAGE_BUF_SZ */
			fcpCmnd->fcpCdb[8] = 0x10;
			fcpCmnd->fcpCdb[9] = 0x00;
			fcpCmnd->fcpCntl3 = READ_DATA;
			fcpCmnd->fcpDl = be32_to_cpu(LPFC_SCSI_PAGE_BUF_SZ);
			/* Get a buffer to hold SCSI data */
			if ((mp->virt = lpfc_page_alloc(phba, MEM_PRI,
			   &(mp->phys))) == 0) {
				if (mp)
					kfree(mp);
				lpfc_free_scsi_buf(lpfc_cmd);
				return (0);
			}
			break;
		case FCP_SCSI_INQUIRY:
			fcpCmnd->fcpCdb[0] = scsi_cmd;	/* SCSI Inquiry
							   Command */
			fcpCmnd->fcpCdb[4] = 0xff;	/* allocation length */
			fcpCmnd->fcpCntl3 = READ_DATA;
			fcpCmnd->fcpDl = be32_to_cpu(LPFC_SCSI_BUF_SZ);
			/* drop thru to get a buffer */
		default:
			size = LPFC_SCSI_BUF_SZ;
			/* Get a buffer to hold SCSI data */
			if ((mp->virt =
			     lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0) {
				if (mp)
					kfree(mp);
				lpfc_free_scsi_buf(lpfc_cmd);
				return (0);
			}
			break;
		}

		bpl = lpfc_cmd->fcp_bpl;
		bpl += 2;	/* Bump past FCP CMND and FCP RSP */

		/* no scatter-gather list case */
		bpl->addrLow = le32_to_cpu(putPaddrLow(mp->phys));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(mp->phys));
		bpl->tus.f.bdeSize = size;
		bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;
		bpl->addrHigh = 0;
		bpl->addrLow = 0;
		bpl->tus.w = 0;

		piocbq = &lpfc_cmd->cur_iocbq;
		piocb = &piocbq->iocb;
		piocb->ulpCommand = CMD_FCP_IREAD64_CR;
		piocb->ulpPU = PARM_READ_CHECK;
		piocb->un.fcpi.fcpi_parm = size;
		piocb->un.fcpi64.bdl.bdeSize += sizeof (ULP_BDE64);
		piocb->ulpBdeCount = 1;
		piocb->ulpLe = 1;	/* Set the LE bit in the iocb */

		/* Get an iotag and finish setup of IOCB  */
		piocb->ulpIoTag = lpfc_sli_next_iotag(phba,
					      &phba->sli.ring[psli->fcp_ring]);
		piocb->ulpContext = nlp->nlp_rpi;
		if (nlp->nlp_fcp_info & NLP_FCP_2_DEVICE) {
			piocb->ulpFCP2Rcvy = 1;
		}
		piocb->ulpClass = (nlp->nlp_fcp_info & 0x0f);

		/* ulpTimeout is only one byte */
		if (lpfc_cmd->timeout > 0xff) {
			/*
			 * Do not timeout the command at the firmware level.
			 * The driver will provide the timeout mechanism.
			 */
			piocb->ulpTimeout = 0;
		} else {
			piocb->ulpTimeout = lpfc_cmd->timeout;
		}

		/*
		 * Setup driver timeout, in case the command does not complete
		 * Driver timeout should be greater than ulpTimeout
		 */

		piocbq->drvrTimeout = lpfc_cmd->timeout + LPFC_DRVR_TIMEOUT;

		/* set up iocb return path by setting the context fields
		 * and the completion function.
		 */
		piocbq->context1 = lpfc_cmd;
		piocbq->context2 = mp;

	}
	return (lpfc_cmd);
}

void
lpfc_scsi_timeout_handler(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_IOCBQ_t *next_iocb;
	LPFC_IOCBQ_t *piocb;
	IOCB_t *cmd = 0;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	uint32_t timeout;
	uint32_t next_timeout;
	unsigned long iflag;
	struct clk_data *scsiClkData;
	struct list_head *curr, *next;

	scsiClkData = (struct clk_data *)ptr;
	phba = scsiClkData->phba;
	LPFC_DRVR_LOCK(phba, iflag);
       	if (scsiClkData->flags & TM_CANCELED) {
		list_del((struct list_head *)scsiClkData);
		kfree(scsiClkData);   
		goto out;  
	}

	timeout = (uint32_t) (unsigned long)(scsiClkData->clData1);
	phba->scsi_tmofunc.function = 0;
	list_del((struct list_head *)scsiClkData);
	kfree(scsiClkData);

	psli = &phba->sli;
	pring = &psli->ring[psli->fcp_ring];
	next_timeout = (phba->fc_ratov << 1) > 5 ? (phba->fc_ratov << 1) : 5;

	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		piocb = next_iocb;
		cmd = &piocb->iocb;
		lpfc_cmd = (LPFC_SCSI_BUF_t *) piocb->context1;

		if (piocb->iocb_flag & (LPFC_IO_LIBDFC | LPFC_IO_POLL)) {
			continue;
		}

		if (piocb->drvrTimeout) {
			if (piocb->drvrTimeout > timeout)
				piocb->drvrTimeout -= timeout;
			else
				piocb->drvrTimeout = 0;

			continue;
		}

		/*
		 * The iocb has timed out; abort it.
		 */

		if (cmd->un.acxri.abortType == ABORT_TYPE_ABTS) {
			/*
			 * If abort times out, simply throw away the iocb
			 */

			list_del(&piocb->list);
			pring->txcmplq_cnt--;
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		} else {
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0754,
					lpfc_mes0754,
					lpfc_msgBlk0754.msgPreambleStr,
					lpfc_cmd->pLun->pTarget->un.dev_did,
					lpfc_cmd->pLun->pTarget->scsi_id,
					lpfc_cmd->fcp_cmnd->fcpCdb[0],
					cmd->ulpIoTag);

			lpfc_sli_abort_iocb(phba, pring, piocb);
		}
	}

	if (!(phba->fc_flag & FC_OFFLINE_MODE))
	{
 
		lpfc_start_timer(phba, next_timeout, &phba->scsi_tmofunc,
			 lpfc_scsi_timeout_handler,
			 (unsigned long)next_timeout, (unsigned long)0);
	}

out:
	LPFC_DRVR_UNLOCK(phba, iflag);
}

int
lpfc_os_prep_io(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	FCP_CMND *fcp_cmnd;
	ULP_BDE64 *topbpl;
	ULP_BDE64 *bpl;
	DMABUF_t *bmp;
	DMABUF_t *head_bmp;
	IOCB_t *cmd;
	struct scsi_cmnd *cmnd;
	struct scatterlist *sgel_p, *sgel_beginp;
	dma_addr_t physaddr;
	uint32_t i;
	uint32_t num_bmps, num_bde, max_bde;
	uint16_t use_sg;
	int datadir;

	bpl = lpfc_cmd->fcp_bpl;
	fcp_cmnd = lpfc_cmd->fcp_cmnd;

	bpl += 2;		/* Bump past FCP CMND and FCP RSP */
	max_bde = LPFC_SCSI_INITIAL_BPL_SIZE - 1;

	cmnd = lpfc_cmd->pCmd;
	cmd = &lpfc_cmd->cur_iocbq.iocb;

	/* These are needed if we chain BPLs */
	head_bmp = lpfc_cmd->dma_ext;
	num_bmps = 1;
	topbpl = 0;

	use_sg = cmnd->use_sg;
	num_bde = 0;
	sgel_p = 0;

	/*
	 * Fill in the FCP CMND
	 */
	memcpy(&fcp_cmnd->fcpCdb[0], cmnd->cmnd, 16);

	if (cmnd->device->tagged_supported) {
		switch (cmnd->tag) {
		case HEAD_OF_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = HEAD_OF_Q;
			break;
		case ORDERED_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = ORDERED_Q;
			break;
		default:
			fcp_cmnd->fcpCntl1 = SIMPLE_Q;
			break;
		}
	} else {
		fcp_cmnd->fcpCntl1 = 0;
	}

	datadir = cmnd->sc_data_direction;
	lpfc_cmd->datadir = cmnd->sc_data_direction;

	if (use_sg) {
		/*
		 * Get a local pointer to the scatter-gather list.  The 
		 * scatter-gather list head must be preserved since
		 * sgel_p is incremented in the loop.  The driver must store
		 * the segment count returned from pci_map_sg for calls to 
		 * pci_unmap_sg later on because the use_sg field in the 
		 * scsi_cmd is a count of physical memory pages, whereas the
		 * seg_cnt is a count of dma-mappings used by the MMIO to
		 * map the use_sg pages.  They are not the same in most
		 * cases for those architectures that implement an MMIO.
		 */
		sgel_p = (struct scatterlist *)cmnd->request_buffer;
		sgel_beginp = sgel_p;
		lpfc_cmd->seg_cnt = pci_map_sg(phba->pcidev, sgel_p, use_sg,
					     scsi_to_pci_dma_dir(datadir));

		/* return error if we cannot map sg list */
		if (lpfc_cmd->seg_cnt == 0)
			return 1;

		/* scatter-gather list case */
		for (i = 0; i < lpfc_cmd->seg_cnt; i++) {
			/* Check to see if current BPL is full of BDEs */
			/* If this is last BDE and there is one left in */
			/* current BPL, use it.                         */
			if (num_bde == max_bde) {
				bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC);
				if (bmp == 0) {
					goto error_out;
				}
				memset(bmp, 0, sizeof (DMABUF_t));
				bmp->virt =
				    lpfc_mbuf_alloc(phba, 0, &bmp->phys);
				if (!bmp->virt) {
					kfree(bmp);
					goto error_out;
				}
				max_bde = ((1024 / sizeof (ULP_BDE64)) - 3);
				/* Fill in continuation entry to next bpl */
				bpl->addrHigh =
				    le32_to_cpu(putPaddrHigh(bmp->phys));
				bpl->addrLow =
				    le32_to_cpu(putPaddrLow(bmp->phys));
				bpl->tus.f.bdeFlags = BPL64_SIZE_WORD;
				num_bde++;
				if (num_bmps == 1) {
					cmd->un.fcpi64.bdl.bdeSize +=
					    (num_bde * sizeof (ULP_BDE64));
				} else {
					topbpl->tus.f.bdeSize =
					    (num_bde * sizeof (ULP_BDE64));
					topbpl->tus.w =
					    le32_to_cpu(topbpl->tus.w);
				}
				topbpl = bpl;
				bpl = (ULP_BDE64 *) bmp->virt;
				list_add(&bmp->list, &head_bmp->list);
				num_bde = 0;
				num_bmps++;
			}

			physaddr = sg_dma_address(sgel_p);

			bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = sg_dma_len(sgel_p);
			if (datadir == SCSI_DATA_WRITE)
			{
				bpl->tus.f.bdeFlags = 0;
			} else {
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;
			}
			bpl->tus.w = le32_to_cpu(bpl->tus.w);
			bpl++;
			sgel_p++;
			num_bde++;
		}		/* end for loop */

		if (datadir == SCSI_DATA_WRITE)
		{
			cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;

			phba->fc4OutputRequests++;
		} else {
			cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			cmd->ulpPU = PARM_READ_CHECK;
			cmd->un.fcpi.fcpi_parm = cmnd->request_bufflen;
			fcp_cmnd->fcpCntl3 = READ_DATA;

			phba->fc4InputRequests++;
		}
	} else {
		if (cmnd->request_buffer && cmnd->request_bufflen) {

			physaddr = pci_map_single(phba->pcidev,
						  cmnd->request_buffer,
						  cmnd->request_bufflen,
						  scsi_to_pci_dma_dir(datadir));

			/* no scatter-gather list case */
			lpfc_cmd->nonsg_phys = physaddr;
			bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = cmnd->request_bufflen;
			if (datadir == SCSI_DATA_WRITE)
			{
				cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
				fcp_cmnd->fcpCntl3 = WRITE_DATA;
				bpl->tus.f.bdeFlags = 0;

				phba->fc4OutputRequests++;
			} else {
				cmd->ulpCommand = CMD_FCP_IREAD64_CR;
				cmd->ulpPU = PARM_READ_CHECK;
				cmd->un.fcpi.fcpi_parm = cmnd->request_bufflen;
				fcp_cmnd->fcpCntl3 = READ_DATA;
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;

				phba->fc4InputRequests++;
			}
			bpl->tus.w = le32_to_cpu(bpl->tus.w);
			num_bde = 1;
			bpl++;
		} else {
			cmd->ulpCommand = CMD_FCP_ICMND64_CR;
			cmd->un.fcpi.fcpi_parm = 0;
			fcp_cmnd->fcpCntl3 = 0;

			phba->fc4ControlRequests++;
		}
	}
	bpl->addrHigh = 0;
	bpl->addrLow = 0;
	bpl->tus.w = 0;
	if (num_bmps == 1) {
		cmd->un.fcpi64.bdl.bdeSize += (num_bde * sizeof (ULP_BDE64));
	} else {
		topbpl->tus.f.bdeSize = (num_bde * sizeof (ULP_BDE64));
		topbpl->tus.w = le32_to_cpu(topbpl->tus.w);
	}
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;		/* Set the LE bit in the iocb */

	/* set the Data Length field in the FCP CMND accordingly */
	fcp_cmnd->fcpDl = be32_to_cpu(cmnd->request_bufflen);

	return (0);

error_out:
	/* Allocation of a chained BPL failed.  Unmap the sg list and return
	 * an error. */
	pci_unmap_sg(phba->pcidev, sgel_beginp, lpfc_cmd->seg_cnt,
		     scsi_to_pci_dma_dir(datadir));
	return 1;
}

int
lpfc_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
{
	lpfcHBA_t *phba;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	int ret;
	void (*old_done) (struct scsi_cmnd *);
	unsigned long iflag;
	LPFCSCSITARGET_t *targetp;
	lpfcCfgParam_t *clp;
	struct Scsi_Host *host;

	host = cmnd->host;

	phba = (lpfcHBA_t *) host->hostdata[0];
	clp = &phba->config[0];

	LPFC_DRVR_LOCK(phba, iflag);

	/* 
	 * If the hba is in blocked state and the command is a retry queue the
	 * command and retry success 
	 */
	if (phba->in_retry) {
		cmnd->scsi_done = done;
		cmnd->reset_chain = phba->cmnd_retry_list;
		phba->cmnd_retry_list = cmnd;
		cmnd->host_scribble = 0;
		atomic_inc(&phba->cmnds_in_flight);
		LPFC_DRVR_UNLOCK(phba, iflag);
		return (0);
	}

	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == 0) {
		if (atomic_read(&phba->cmnds_in_flight) == 0
		    && (host->host_self_blocked == FALSE)) {
			LPFC_DRVR_UNLOCK(phba, iflag);
		} else {
			/* Do not count retry (with cmnds_in_flight) */
			if(cmnd->retries)
				cmnd->retries--;
			LPFC_DRVR_UNLOCK(phba, iflag);
		}
		cmnd->result = ScsiResult(DID_BUS_BUSY, 0);
		done(cmnd);
		return (0);
	}
	

	lpfc_cmd->scsi_bus = cmnd->channel;
	lpfc_cmd->scsi_target = cmnd->target;
	lpfc_cmd->scsi_lun = cmnd->lun;

	if ((targetp = lpfc_find_target(phba, lpfc_cmd->scsi_target))) {
 
		if ((targetp->pcontext == 0)
		    && !(targetp->targetFlags & FC_NPR_ACTIVE)) {
			lpfc_free_scsi_buf(lpfc_cmd);
			LPFC_DRVR_UNLOCK(phba, iflag);
			/* error-out this command */
			cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
			done(cmnd);
			return (0);
		}

	}
	
	/* store our command structure for later */
	lpfc_cmd->pCmd = cmnd;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;

	/* Let the driver time I/Os out, NOT the upper layer */
	lpfc_cmd->scsitmo = lpfc_scsi_delete_timer(cmnd);
	lpfc_cmd->timeout = (uint32_t) (cmnd->timeout_per_command / HZ) +
	    phba->fcp_timeout_offset;
	/* save original done function in case we can not issue this
	   command */
	old_done = cmnd->scsi_done;

	cmnd->scsi_done = done;

	ret = lpfc_scsi_cmd_start(lpfc_cmd);
	if (ret) {

		lpfc_scsi_add_timer(cmnd, cmnd->timeout_per_command);

		lpfc_free_scsi_buf(lpfc_cmd);

		/* restore original done function in command */
		cmnd->scsi_done = old_done;
		if (ret < 0) {
			/* permanent failure -- error out command */
			cmnd->result = ScsiResult(DID_BAD_TARGET, 0);
			LPFC_DRVR_UNLOCK(phba, iflag);
			done(cmnd);
			return (0);
		} else {
			if (atomic_read(&phba->cmnds_in_flight) == 0) {
				/* there are no other commands which will
				   complete to flush the queue, so retry */
				LPFC_DRVR_UNLOCK(phba, iflag);
			} else {
				/* Do not count retry (with cmnds_in_flight) */
				if(cmnd->retries)
					cmnd->retries--;
				LPFC_DRVR_UNLOCK(phba, iflag);
			}
			cmnd->result = ScsiResult(DID_BUS_BUSY, 0);
			done(cmnd);
			return (0);
		}
	}

	atomic_inc(&phba->cmnds_in_flight);
	LPFC_DRVR_UNLOCK(phba, iflag);

	/* Return the error code. */
	return (0);
}

int
lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	lpfcHBA_t *phba;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	unsigned long iflag;
	int rc = 0;

	struct scsi_cmnd *prev_cmnd;

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);
	phba = (lpfcHBA_t *) cmnd->host->hostdata[0];

	LPFC_DRVR_LOCK(phba, iflag);

	lpfc_cmd = (LPFC_SCSI_BUF_t *) cmnd->host_scribble;

	/* 
	   If the command is in retry cahin. delete the command from the
	   list.
	 */
	if (!lpfc_cmd) {

		if (phba->cmnd_retry_list) {
			if (phba->cmnd_retry_list == cmnd) {
				phba->cmnd_retry_list = cmnd->reset_chain;

			} else {
				prev_cmnd = phba->cmnd_retry_list;

				while ((prev_cmnd->reset_chain != 0) &&
				       (prev_cmnd->reset_chain != cmnd))
					prev_cmnd = prev_cmnd->reset_chain;

				if (prev_cmnd->reset_chain)
					prev_cmnd->reset_chain =
					    cmnd->reset_chain;
				else
					rc = 1;		/* return FAILURE */
			}

		} else
			rc = 1;		/* return FAILURE */
		goto exit_abort_handler;
	}

	/* set command timeout to 60 seconds */
	lpfc_cmd->timeout = 60;

	/* SCSI layer issued abort device */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0712,
			lpfc_mes0712,
			lpfc_msgBlk0712.msgPreambleStr,
			lpfc_cmd->scsi_target,
			lpfc_cmd->scsi_lun);

	/* tell low layer to abort it */
	rc = lpfc_scsi_cmd_abort(phba, lpfc_cmd);

	/* SCSI layer issued abort device */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0749,
			lpfc_mes0749,
			lpfc_msgBlk0749.msgPreambleStr,
			lpfc_cmd->scsi_target,
			lpfc_cmd->scsi_lun, rc,
			lpfc_cmd->status, lpfc_cmd->result);

exit_abort_handler:
	LPFC_DRVR_UNLOCK(phba, iflag);

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);

	return ((rc == 0) ? SUCCESS : FAILURE);

}

/* This function is now OS-specific and driver-specific */

int
lpfc_reset_lun_handler(struct scsi_cmnd *cmnd)
{
	lpfcHBA_t *phba;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	unsigned long iflag;
	int rc;

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);
	phba = (lpfcHBA_t *) cmnd->host->hostdata[0];
	LPFC_DRVR_LOCK(phba, iflag);

	/* Get resources to send a SCSI command */
	lpfc_cmd = lpfc_get_scsi_buf(phba);
	if (lpfc_cmd == 0) {
		rc = 0;		/* return FAILURE */
		goto exit_reset_lun_handler;
	}

	lpfc_cmd->timeout = 60;	   /* 60 sec command timeout */
	lpfc_cmd->scsi_hba = phba;
	lpfc_cmd->scsi_bus = cmnd->channel;
	lpfc_cmd->scsi_target = cmnd->target;
	lpfc_cmd->scsi_lun = cmnd->lun;
	lpfc_cmd->pCmd = cmnd;

	/* SCSI layer issued abort device */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0713,
			lpfc_mes0713,
			lpfc_msgBlk0713.msgPreambleStr,
			lpfc_cmd->scsi_target,
			lpfc_cmd->scsi_lun);


	/*
	 * For SCSI device_reset_handler, by default issue LUN Reset.
	 * If the target does not support this, issue Target Reset.
	 * (Alternatively a Abort Task Set can be issued.)
	 */
#ifndef USE_ABORT_TSET
	rc = lpfc_scsi_lun_reset(lpfc_cmd, phba, lpfc_cmd->scsi_bus,
				 lpfc_cmd->scsi_target, lpfc_cmd->scsi_lun,
				 LPFC_EXTERNAL_RESET | LPFC_ISSUE_LUN_RESET);
	/* check if IOCB command was issued and also check reply status */
	if (rc != 1 || lpfc_cmd->status != IOSTAT_SUCCESS) {
		rc = lpfc_scsi_tgt_reset(lpfc_cmd, phba, lpfc_cmd->scsi_bus,
					lpfc_cmd->scsi_target,
					LPFC_EXTERNAL_RESET);
	}

#else /* USE_ABORT_TSET */
	rc = lpfc_scsi_lun_reset(lpfc_cmd, phba, lpfc_cmd->scsi_bus,
				 lpfc_cmd->scsi_target, lpfc_cmd->scsi_lun,
				 LPFC_EXTERNAL_RESET | LPFC_ISSUE_ABORT_TSET);
#endif /* USE_ABORT_TSET */

	/* SCSI layer issued abort device */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0747,
			lpfc_mes0747,
			lpfc_msgBlk0747.msgPreambleStr,
			lpfc_cmd->scsi_target,
			lpfc_cmd->scsi_lun,
			rc, lpfc_cmd->status, lpfc_cmd->result);

	lpfc_free_scsi_buf(lpfc_cmd);

exit_reset_lun_handler:
	LPFC_DRVR_UNLOCK(phba, iflag);

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);

	return ((rc == 1) ? SUCCESS : FAILURE);
}

void
free_lun(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp, *curLun;
	struct list_head *curr, *next;

	lunp = lpfc_cmd->pLun;
	if (lunp == 0) {
		return;
	}

	targetp = lunp->pTarget;
	if (targetp == 0) {
		return;
	}
	lpfc_sched_remove_lun_from_ring(phba, lunp);
	list_for_each_safe(curr, next, &targetp->lunlist) {

		curLun = list_entry(curr, LPFCSCSILUN_t, list);

		if (curLun == lunp) {
			list_del(&lunp->list);
			kfree(lunp);
			return;
		}
	}

}


void
lpfc_os_return_scsi_cmd(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	struct scsi_cmnd *lnx_cmnd = lpfc_cmd->pCmd;
	LPFCSCSILUN_t *lun_device;
	lpfc_xlat_err_t resultdata;
	lpfc_xlat_err_t *presult;
	PARM_ERR *perr;
	uint32_t host_status;
	uint32_t scsi_status;

	FCP_CMND *fcp_cmnd;

	if (lpfc_cmd->status >= IOSTAT_CNT)
		lpfc_cmd->status = IOSTAT_DEFAULT;
	presult = &lpfc_iostat_tbl[lpfc_cmd->status];

	host_status = presult->host_status;
	scsi_status = 0;

	/* Now check if there are any special actions to perform */
	if (presult->action_flag) {
		/* FCP cmd <cmnd> failed <target>/<lun> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0729,
				lpfc_mes0729,
				lpfc_msgBlk0729.msgPreambleStr,
				lnx_cmnd->cmnd[0], lpfc_cmd->scsi_target,
				lpfc_cmd->scsi_lun,
				lpfc_cmd->status,
				lpfc_cmd->result, lpfc_cmd->IOxri,
				lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		if (presult->action_flag & LPFC_FCPRSP_ERROR) {
			presult = &resultdata;
			presult->host_status = DID_OK;
			presult->action_flag = 0;
			/* Call FCP RSP handler to determine result */
			scsi_status = lpfc_os_fcp_err_handle(lpfc_cmd, presult);
			if (scsi_status == LPFC_CMD_BEING_RETRIED) {
				return;
			}
		} else {
			if (presult->action_flag & LPFC_IOERR_TABLE) {

				perr = (PARM_ERR *) & lpfc_cmd->result;
				if (perr->statLocalError >= IOERR_CNT)
					perr->statLocalError = IOERR_DEFAULT;
				presult = &lpfc_ioerr_tbl[perr->statLocalError];
			}
		}
		host_status = presult->host_status;

		if (presult->action_flag & LPFC_STAT_ACTION) {
			perr = (PARM_ERR *) & lpfc_cmd->result;
			if (perr->statAction == RJT_RETRYABLE) {
				host_status = DID_BUS_BUSY;
			}
		}

		lun_device = lpfc_find_lun_device(lpfc_cmd);

		/* Special treatment for targets in the NPort recovery state */
		if (lun_device &&
		    (lun_device->pTarget->targetFlags & FC_NPR_ACTIVE)) {

			/* Make sure command will be retried */
			if (host_status != DID_OK          &&
			    host_status != DID_PASSTHROUGH &&
			    host_status != DID_ERROR       &&
			    host_status != DID_PARITY      &&
			    host_status != DID_BUS_BUSY )
				host_status = DID_SOFT_ERROR;

			if (lnx_cmnd->retries) {
				/* Do not count this retry (in FC_NPR_ACTIVE state) */
				lnx_cmnd->retries--;
			}
		}

		if (presult->action_flag & LPFC_DELAY_IODONE) {
			lnx_cmnd->result = ScsiResult(host_status, scsi_status);
 			lpfc_scsi_delay_iodone(phba, lpfc_cmd);
			return;
		}

	}

	fcp_cmnd = lpfc_cmd->fcp_cmnd;

	/*
	 * If this is a scsi inquiry response, make sure the response is standard
	 * inquiry page data before deciding whether the device exists or not.
	 */
	if ((fcp_cmnd->fcpCdb[0] == FCP_SCSI_INQUIRY) && (fcp_cmnd->fcpCdb[1] == 0) &&
	     (fcp_cmnd->fcpCdb[2] == 0)) {
		unsigned char *buf;
		lpfcCfgParam_t *clp;
		LPFCSCSITARGET_t *targetp;

		buf = (unsigned char *)lnx_cmnd->request_buffer;
		targetp = lpfc_find_target(phba, lpfc_cmd->scsi_target);

		if (buf && ((*buf == 0x7f) || ((*buf & 0xE0) == 0x20))) {
			/*
			 * SLES8 does not handle Peripheral Qualifier bit set 
			 * to 1. If the lpfc_INQ_PQB_filter parameter is enabled
			 * set, change PQB = 1 to PQB = 3.
			 */
			if (lpfc_inq_pqb_filter)
				*buf |= 0x60;

			free_lun(phba, lpfc_cmd);

			/* If a LINUX OS patch to support, LUN skipping / no LUN
			 * 0, is not present, this code will fake out the LINUX
			 * scsi layer to allow it to detect all LUNs if there
			 * are LUN holes on a device.
			 */
			clp = &phba->config[0];
			if ((clp[LPFC_CFG_LUN_SKIP].a_current) &&
			    (targetp) &&
			    (lpfc_cmd->scsi_lun < targetp->max_lun)){
				/* Make lun unassigned and wrong type */
				*buf++ = 0x3;
				*buf++ = 0x0;
				*buf++ = 0x3;
				*buf++ = 0x12;
				*buf++ = 0x4;
				*buf++ = 0x0;
				*buf++ = 0x0;
				*buf++ = 0x2;
			} 
		} else { 
			if ((targetp) && (lpfc_cmd->scsi_lun >  targetp->max_lun))
				targetp->max_lun = lpfc_cmd->scsi_lun;
		}
	}

	lnx_cmnd->result = ScsiResult(host_status, scsi_status);

	lpfc_iodone(phba, lpfc_cmd);

	return;
}

void
lpfc_scsi_add_timer(struct scsi_cmnd *SCset, int timeout)
{

	if (SCset->eh_timeout.function != 0) {
		del_timer(&SCset->eh_timeout);
	}

	if (SCset->eh_timeout.data != (unsigned long)SCset) {
		SCset->eh_timeout.data = (unsigned long)SCset;
		SCset->eh_timeout.function =
		    (void (*)(unsigned long))lpfc_nodev;
	}
	SCset->eh_timeout.expires = jiffies + timeout;

	add_timer(&SCset->eh_timeout);
	return;
}

int
lpfc_scsi_delete_timer(struct scsi_cmnd *SCset)
{
	int rtn;

	rtn = SCset->eh_timeout.expires - jiffies;
	del_timer(&SCset->eh_timeout);
	SCset->eh_timeout.data = 0;
	SCset->eh_timeout.function = 0;
	return (rtn);
}

uint32_t
lpfc_os_fcp_err_handle(LPFC_SCSI_BUF_t * lpfc_cmd, lpfc_xlat_err_t * presult)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	FCP_CMND *fcpcmd;
	FCP_RSP *fcprsp;
	lpfcHBA_t *phba;
	LPFCSCSILUN_t *plun;
	IOCB_t *iocb;
	lpfcCfgParam_t *clp;
	int datadir;
	uint8_t iostat;
	uint32_t scsi_status;
	uint32_t rsplen = 0;

	phba = lpfc_cmd->scsi_hba;
	plun = lpfc_cmd->pLun;
	clp = &phba->config[0];
	iocb = &lpfc_cmd->cur_iocbq.iocb;
	fcpcmd = lpfc_cmd->fcp_cmnd;
	fcprsp = lpfc_cmd->fcp_rsp;
	iostat = (uint8_t) (lpfc_cmd->status);

	/* Make sure presult->host_status is identically DID_OK and scsi_status
	 * is identically 0.  The driver alters this value later on an as-needed
	 * basis.
	 */
	presult->host_status = DID_OK;
	scsi_status = 0;

	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with it.  Return here.
	 */
	if ((cmnd == 0) || (fcpcmd->fcpCntl2)) {
		return (scsi_status);
	}

	/* FCP cmd failed: RSP */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0730,
			lpfc_mes0730,
			lpfc_msgBlk0730.msgPreambleStr,
			fcprsp->rspStatus2, fcprsp->rspStatus3,
			be32_to_cpu(fcprsp->rspResId),
			be32_to_cpu(fcprsp->rspSnsLen),
			be32_to_cpu(fcprsp->rspRspLen), fcprsp->rspInfo3);

	if (fcprsp->rspStatus2 & RSP_LEN_VALID) {
		rsplen = be32_to_cpu(fcprsp->rspRspLen);
		if ((rsplen > 8) || (fcprsp->rspInfo3 != RSP_NO_FAILURE)) {
			presult->host_status = DID_ERROR;
			scsi_status = (uint32_t) (fcprsp->rspStatus3);
			return (scsi_status);
		}
	}

	/* if there's sense data, let's copy it back */
	if ((fcprsp->rspStatus2 & SNS_LEN_VALID) && (fcprsp->rspSnsLen != 0)) {
		uint32_t snsLen;

		snsLen = be32_to_cpu(fcprsp->rspSnsLen);

		/* then we return this sense info in the sense buffer for this
		   cmd */
		if (snsLen > SCSI_SENSE_BUFFERSIZE) {
			snsLen = SCSI_SENSE_BUFFERSIZE;
		}
		memcpy(cmnd->sense_buffer,
		       (((uint8_t *) & fcprsp->rspInfo0) + rsplen), snsLen);
	}

	/*
	 * In the Tape Env., there is an early WARNNING right before EOM without
	 * data xfer error. We should set b_resid to be 0 before we check all
	 * other cases.
	 */

	cmnd->resid = 0;

	if (fcprsp->rspStatus2 & (RESID_UNDER | RESID_OVER)) {
		if (fcprsp->rspStatus2 & RESID_UNDER) {
			cmnd->resid = be32_to_cpu(fcprsp->rspResId);

			/* FCP Read Underrun, expected <len>, residual
			   <resid> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0716,
					lpfc_mes0716,
					lpfc_msgBlk0716.msgPreambleStr,
					be32_to_cpu(fcpcmd->fcpDl),
					cmnd->resid, iocb->un.fcpi.fcpi_parm,
					cmnd->cmnd[0], cmnd->underflow);

			switch (cmnd->cmnd[0]) {
			case TEST_UNIT_READY:
			case REQUEST_SENSE:
			case INQUIRY:
			case RECEIVE_DIAGNOSTIC:
			case READ_CAPACITY:
			case FCP_SCSI_READ_DEFECT_LIST:
			case MDAC_DIRECT_CMD:
				/* return the scsi status */ 
				scsi_status = (uint32_t) (fcprsp->rspStatus3); 
				return (scsi_status); 

			default:
				if (!(fcprsp->rspStatus2 & SNS_LEN_VALID) &&
				    ((cmnd->request_bufflen - cmnd->resid) <
				     cmnd->underflow)) {

					/* FCP command <cmd> residual underrun
					   converted to error */
					lpfc_printf_log(phba->brd_no,
							&lpfc_msgBlk0717,
							lpfc_mes0717,
							lpfc_msgBlk0717.
							msgPreambleStr,
							cmnd->cmnd[0],
							cmnd->request_bufflen,
							cmnd->resid,
							cmnd->underflow);

					presult->host_status = DID_ERROR;
					scsi_status =
					    (uint32_t) (fcprsp->rspStatus3);

					/* Queue Full handled below */
					if (scsi_status != SCSI_STAT_QUE_FULL)
						return (scsi_status);

				}
			}
		}
	} else {
		datadir = lpfc_cmd->datadir;

		if ((datadir == SCSI_DATA_READ) && iocb->un.fcpi.fcpi_parm)
		{
			/* 
			 * This is ALWAYS a readcheck error!! 
			 * Give Check Condition priority over Read Check 
			 */

			if (fcprsp->rspStatus3 != SCSI_STAT_BUSY) {
				if (fcprsp->rspStatus3 != 
				    SCSI_STAT_CHECK_COND) {
					/* FCP Read Check Error */
					lpfc_printf_log(phba->brd_no,
							&lpfc_msgBlk0734,
							lpfc_mes0734,
							lpfc_msgBlk0734.
							msgPreambleStr,
							be32_to_cpu(fcpcmd->
								    fcpDl),
							be32_to_cpu(fcprsp->
								    rspResId),
							iocb->un.fcpi.fcpi_parm,
							cmnd->cmnd[0]);

					presult->host_status = DID_ERROR;
					cmnd->resid = cmnd->request_bufflen;
					scsi_status =
					    (uint32_t) (fcprsp->rspStatus3);
					return (scsi_status);
				}

				/* FCP Read Check Error with Check Condition */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0735,
						lpfc_mes0735,
						lpfc_msgBlk0735.msgPreambleStr,
						be32_to_cpu(fcpcmd->fcpDl),
						be32_to_cpu(fcprsp->rspResId),
						iocb->un.fcpi.fcpi_parm,
						cmnd->cmnd[0]);
			}
		}
	}

	scsi_status = (uint32_t) (fcprsp->rspStatus3);

	switch (scsi_status) {
	case SCSI_STAT_QUE_FULL:
		if (clp[LPFC_CFG_DQFULL_THROTTLE_UP_TIME].a_current) {
			lpfc_scsi_lower_lun_qthrottle(phba, lpfc_cmd);
		}
		presult->host_status = DID_BUS_BUSY;
		presult->action_flag |= LPFC_DELAY_IODONE;
		break;

	case SCSI_STAT_BUSY:
		presult->host_status = DID_BUS_BUSY;
		scsi_status = (uint32_t) (fcprsp->rspStatus3);
		presult->action_flag |= LPFC_DELAY_IODONE;
		break;

	case SCSI_STAT_CHECK_COND:
		{
			uint32_t i;
			uint32_t cc, asc_ascq;
			uint32_t *lp;

			i = be32_to_cpu(fcprsp->rspRspLen);
			lp = (uint32_t *) (((uint8_t *) & fcprsp->rspInfo0) +
					   i);
			cc = be32_to_cpu((lp[3]) & be32_to_cpu(0xFF000000));
			asc_ascq = be32_to_cpu((lp[3]) & be32_to_cpu(0xFFFF0000));

			/* <ASC ASCQ> Check condition received */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0737,
					lpfc_mes0737,
					lpfc_msgBlk0737.msgPreambleStr,
					asc_ascq,
					clp[LPFC_CFG_CHK_COND_ERR].a_current,
					clp[LPFC_CFG_DELAY_RSP_ERR].a_current,
					*lp);

			switch (cc) {
			case 0x0:	/* ASC and ASCQ = 0 */
				break;
			case 0x44000000:	/* Internal Target Failure */
			case 0x25000000:	/* Login Unit not supported */
			case 0x20000000:	/* Invalid cmd operation code */
				/* These will be considered an error if the
				 * command is not a TUR and CHK_COND_ERR is not
				 * set */
				if ((fcpcmd->fcpCdb[0] !=
				     FCP_SCSI_TEST_UNIT_READY)
				    && (clp[LPFC_CFG_CHK_COND_ERR].a_current)) {
					presult->host_status = DID_ERROR;
					scsi_status = 0;
				}
			}
			if (asc_ascq == 0x04010000) {
				presult->action_flag |= LPFC_DELAY_IODONE;
				presult->host_status = DID_ERROR;
				scsi_status = 0;
			}
		}
		break;

	default:
		break;
	}

	return (scsi_status);
}

void
lpfc_iodone(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	LPFCSCSITARGET_t *targetp;
	struct scsi_cmnd *lnx_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel_p = 
			(struct scatterlist *) lnx_cmnd->request_buffer;
	int datadir;

	datadir = lpfc_cmd->datadir;
	if (lnx_cmnd->use_sg) {
		if (lpfc_cmd->seg_cnt == 0) {
			/* This is an error.  Don't allow the caller to unmap a
			 * zero-count sg list.  Note that the sgel_p may or may not
			 * be NULL so treat the printk value as an unsigned long and
			 * don't dereference. it. */
			printk(KERN_ERR "%s: ignoring pci_unmap_sg request on"
				" lpfc_cmd %p sgel_p 0x%lx, seg_cnt %d\n", 
				__FUNCTION__, lpfc_cmd, 
				(unsigned long) sgel_p, lpfc_cmd->seg_cnt);
		} else {
			pci_unmap_sg(phba->pcidev, sgel_p, lpfc_cmd->seg_cnt,
					 scsi_to_pci_dma_dir(datadir));
			lpfc_cmd->seg_cnt = 0;
		}
	} else if ((lnx_cmnd->request_bufflen) && (lpfc_cmd->nonsg_phys)) {
		pci_unmap_single(phba->pcidev, lpfc_cmd->nonsg_phys,
				 lnx_cmnd->request_bufflen,
				 scsi_to_pci_dma_dir(datadir));
	}

	/* Lets check to see if the device timed out during this I/O,
	 * if so, return DID_NO_CONNECT.
	 */
	targetp = lpfc_find_target(phba, lpfc_cmd->scsi_target);
	if (targetp && (targetp->pcontext == 0) &&
	    !(targetp->targetFlags & FC_NPR_ACTIVE)) {
		if(lnx_cmnd->result)
			lnx_cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
	}

	lpfc_free_scsi_buf(lpfc_cmd);
	lpfc_scsi_done(phba, lnx_cmnd);
	return;
}

void
lpfc_delay_done(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_SCSI_BUF_t *lpfc_cmd;
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

	lpfc_cmd = (LPFC_SCSI_BUF_t *) (clkData->clData1);
	list_del(&lpfc_cmd->listentry);
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	lpfc_iodone(phba, lpfc_cmd);
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

int
lpfc_scsi_delay_iodone(lpfcHBA_t * phba, LPFC_SCSI_BUF_t * lpfc_cmd)
{
	lpfcCfgParam_t *clp;
	uint32_t tmout;
	FCP_RSP *fcprsp;
	uint32_t scsi_status;

	clp = &phba->config[0];

	fcprsp = lpfc_cmd->fcp_rsp;
	scsi_status = (uint32_t) (fcprsp->rspStatus3);
	if (scsi_status == SCSI_STAT_QUE_FULL) {
		/* For QUE_FULL, delay should be at least 5 seconds */
		tmout = clp[LPFC_CFG_NO_DEVICE_DELAY].a_current;
		if(tmout < 5)
			tmout = 5;

		list_add(&lpfc_cmd->listentry, &phba->delay_list);

		lpfc_start_timer(phba, tmout, &lpfc_cmd->delayIodoneFunc,
				 lpfc_delay_done, (unsigned long)lpfc_cmd,
				 (unsigned long)0);
		return (0);
	}

	if (clp[LPFC_CFG_NO_DEVICE_DELAY].a_current) {
		/* Set a timer so iodone can be called
		 * for buffer upon expiration.
		 */
		tmout = clp[LPFC_CFG_NO_DEVICE_DELAY].a_current;

		list_add(&lpfc_cmd->listentry, &phba->delay_list);

		lpfc_start_timer(phba, tmout, &lpfc_cmd->delayIodoneFunc,
				 lpfc_delay_done, (unsigned long)lpfc_cmd,
				 (unsigned long)0);
		return (0);
	}

	lpfc_iodone(phba, lpfc_cmd);
	return (0);
}

void
lpfc_block_requests(lpfcHBA_t * phba)
{
	phba->in_retry = 1;
	scsi_block_requests(phba->host);

}

void
lpfc_unblock_requests(lpfcHBA_t * phba)
{

	struct scsi_cmnd *cmnd, *next_cmnd;
	unsigned long iflag;

	cmnd = phba->cmnd_retry_list;
	phba->in_retry = 0;
	while (cmnd) {
		next_cmnd = cmnd->reset_chain;
		cmnd->reset_chain = 0;
		cmnd->result = ScsiResult(DID_RESET, 0);
		lpfc_scsi_done(phba, cmnd);
		cmnd = next_cmnd;
	}
	phba->cmnd_retry_list = 0;
	iflag = phba->iflag;

	LPFC_DRVR_UNLOCK(phba, iflag);
	scsi_unblock_requests(phba->host);
	LPFC_DRVR_LOCK(phba, iflag);
	phba->iflag = iflag;
}

void
lpfc_scsi_done(lpfcHBA_t * phba, struct scsi_cmnd *cmnd)
{
	unsigned long sflag;
	unsigned long iflag;

	LPFC_DRVR_UNLOCK(phba, iflag);

	spin_lock_irqsave(&io_request_lock, sflag);

	cmnd->host_scribble = 0;
	lpfc_scsi_add_timer(cmnd, cmnd->timeout_per_command);
	atomic_dec(&phba->cmnds_in_flight);

	/* Give this command back to the OS */
	cmnd->scsi_done(cmnd);

	spin_unlock_irqrestore(&io_request_lock, sflag);
	LPFC_DRVR_LOCK(phba, iflag);
	return;
}
