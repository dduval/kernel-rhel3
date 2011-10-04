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
 * $Id: lpfc_sli.c 483 2006-03-22 00:27:31Z sf_support $
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
#include "lpfc_scsi.h"
#include "lpfc_crtn.h"

int lpfc_sli_reset_on_init = 1;

int lpfc_sli_handle_mb_event(lpfcHBA_t *);
int lpfc_sli_handle_ring_event(lpfcHBA_t *, LPFC_SLI_RING_t *, uint32_t);
int lpfc_sli_process_unsol_iocb(lpfcHBA_t * phba,
	LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t *saveq);
int lpfc_sli_process_sol_iocb(lpfcHBA_t * phba,
	LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t *saveq);
int lpfc_sli_ringtx_put(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *);
int lpfc_sli_ringtxcmpl_put(lpfcHBA_t *, LPFC_SLI_RING_t *, LPFC_IOCBQ_t *);
LPFC_IOCBQ_t *lpfc_sli_ringtx_get(lpfcHBA_t *, LPFC_SLI_RING_t *);
LPFC_IOCBQ_t *lpfc_sli_ringtxcmpl_get(lpfcHBA_t *, LPFC_SLI_RING_t *,
	 LPFC_IOCBQ_t *, uint32_t);
LPFC_IOCBQ_t *lpfc_search_txcmpl(LPFC_SLI_RING_t * pring,
		   LPFC_IOCBQ_t * prspiocb);
DMABUF_t *lpfc_sli_ringpostbuf_search(lpfcHBA_t *, LPFC_SLI_RING_t *,
	dma_addr_t, int);


/*
 * Define macro to log: Mailbox command x%x cannot issue Data
 * This allows multiple uses of lpfc_msgBlk0311
 * w/o perturbing log msg utility.
*/
#define LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag) \
			lpfc_printf_log(phba->brd_no,	\
				&lpfc_msgBlk0311,	\
				lpfc_mes0311,		\
				lpfc_msgBlk0311.msgPreambleStr, \
				mb->mbxCommand,		\
				phba->hba_state,	\
				psli->sliinit.sli_flag,	\
				flag);


/* This will save a huge switch to determine if the IOCB cmd
 * is unsolicited or solicited.
 */
#define LPFC_UNKNOWN_IOCB 0
#define LPFC_UNSOL_IOCB   1
#define LPFC_SOL_IOCB     2
#define LPFC_ABORT_IOCB   3
uint8_t lpfc_sli_iocb_cmd_type[CMD_MAX_IOCB_CMD] = {
	LPFC_UNKNOWN_IOCB,	/* 0x00 */
	LPFC_UNSOL_IOCB,	/* CMD_RCV_SEQUENCE_CX     0x01 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_SEQUENCE_CR    0x02 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_SEQUENCE_CX    0x03 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_BCAST_CN       0x04 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_BCAST_CX       0x05 */
	LPFC_UNKNOWN_IOCB,	/* CMD_QUE_RING_BUF_CN     0x06 */
	LPFC_UNKNOWN_IOCB,	/* CMD_QUE_XRI_BUF_CX      0x07 */
	LPFC_UNKNOWN_IOCB,	/* CMD_IOCB_CONTINUE_CN    0x08 */
	LPFC_UNKNOWN_IOCB,	/* CMD_RET_XRI_BUF_CX      0x09 */
	LPFC_SOL_IOCB,		/* CMD_ELS_REQUEST_CR      0x0A */
	LPFC_SOL_IOCB,		/* CMD_ELS_REQUEST_CX      0x0B */
	LPFC_UNKNOWN_IOCB,	/* 0x0C */
	LPFC_UNSOL_IOCB,	/* CMD_RCV_ELS_REQ_CX      0x0D */
	LPFC_ABORT_IOCB,	/* CMD_ABORT_XRI_CN        0x0E */
	LPFC_ABORT_IOCB,	/* CMD_ABORT_XRI_CX        0x0F */
	LPFC_ABORT_IOCB,	/* CMD_CLOSE_XRI_CN        0x10 */
	LPFC_ABORT_IOCB,	/* CMD_CLOSE_XRI_CX        0x11 */
	LPFC_SOL_IOCB,		/* CMD_CREATE_XRI_CR       0x12 */
	LPFC_SOL_IOCB,		/* CMD_CREATE_XRI_CX       0x13 */
	LPFC_SOL_IOCB,		/* CMD_GET_RPI_CN          0x14 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_ELS_RSP_CX     0x15 */
	LPFC_SOL_IOCB,		/* CMD_GET_RPI_CR          0x16 */
	LPFC_ABORT_IOCB,	/* CMD_XRI_ABORTED_CX      0x17 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IWRITE_CR       0x18 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IWRITE_CX       0x19 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IREAD_CR        0x1A */
	LPFC_SOL_IOCB,		/* CMD_FCP_IREAD_CX        0x1B */
	LPFC_SOL_IOCB,		/* CMD_FCP_ICMND_CR        0x1C */
	LPFC_SOL_IOCB,		/* CMD_FCP_ICMND_CX        0x1D */
	LPFC_UNKNOWN_IOCB,	/* 0x1E */
	LPFC_SOL_IOCB,		/* CMD_FCP_TSEND_CX        0x1F */
	LPFC_SOL_IOCB,		/* CMD_ADAPTER_MSG         0x20 */
	LPFC_SOL_IOCB,		/* CMD_FCP_TRECEIVE_CX     0x21 */
	LPFC_SOL_IOCB,		/* CMD_ADAPTER_DUMP        0x22 */
	LPFC_SOL_IOCB,		/* CMD_FCP_TRSP_CX         0x23 */
	/* 0x24 - 0x80 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	/* 0x30 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	/* 0x40 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	/* 0x50 */
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_UNKNOWN_IOCB,
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_UNSOL_IOCB,
	LPFC_UNSOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,

	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	/* 0x60 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	/* 0x70 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	/* 0x80 */
	LPFC_UNKNOWN_IOCB,
	LPFC_UNSOL_IOCB,	/* CMD_RCV_SEQUENCE64_CX   0x81 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_SEQUENCE64_CR  0x82 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_SEQUENCE64_CX  0x83 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_BCAST64_CN     0x84 */
	LPFC_SOL_IOCB,		/* CMD_XMIT_BCAST64_CX     0x85 */
	LPFC_UNKNOWN_IOCB,	/* CMD_QUE_RING_BUF64_CN   0x86 */
	LPFC_UNKNOWN_IOCB,	/* CMD_QUE_XRI_BUF64_CX    0x87 */
	LPFC_UNKNOWN_IOCB,	/* CMD_IOCB_CONTINUE64_CN  0x88 */
	LPFC_UNKNOWN_IOCB,	/* CMD_RET_XRI_BUF64_CX    0x89 */
	LPFC_SOL_IOCB,		/* CMD_ELS_REQUEST64_CR    0x8A */
	LPFC_SOL_IOCB,		/* CMD_ELS_REQUEST64_CX    0x8B */
	LPFC_ABORT_IOCB,	/* CMD_ABORT_MXRI64_CN     0x8C */
	LPFC_UNSOL_IOCB,	/* CMD_RCV_ELS_REQ64_CX    0x8D */
	/* 0x8E - 0x94 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB,
	LPFC_SOL_IOCB,		/* CMD_XMIT_ELS_RSP64_CX   0x95 */
	LPFC_UNKNOWN_IOCB,	/* 0x96 */
	LPFC_UNKNOWN_IOCB,	/* 0x97 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IWRITE64_CR     0x98 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IWRITE64_CX     0x99 */
	LPFC_SOL_IOCB,		/* CMD_FCP_IREAD64_CR      0x9A */
	LPFC_SOL_IOCB,		/* CMD_FCP_IREAD64_CX      0x9B */
	LPFC_SOL_IOCB,		/* CMD_FCP_ICMND64_CR      0x9C */
	LPFC_SOL_IOCB,		/* CMD_FCP_ICMND64_CX      0x9D */
	LPFC_UNKNOWN_IOCB,	/* 0x9E */
	LPFC_SOL_IOCB,		/* CMD_FCP_TSEND64_CX      0x9F */
	LPFC_UNKNOWN_IOCB,	/* 0xA0 */
	LPFC_SOL_IOCB,		/* CMD_FCP_TRECEIVE64_CX   0xA1 */
	LPFC_UNKNOWN_IOCB,	/* 0xA2 */
	LPFC_SOL_IOCB,		/* CMD_FCP_TRSP64_CX       0xA3 */
	/* 0xA4 - 0xC1 */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_SOL_IOCB,		/* CMD_GEN_REQUEST64_CR    0xC2 */
	LPFC_SOL_IOCB,		/* CMD_GEN_REQUEST64_CX    0xC3 */
	/* 0xC4 - 0xCF */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,

	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,		/* CMD_SENDTEXT_CR              0xD1 */
	LPFC_SOL_IOCB,		/* CMD_SENDTEXT_CX              0xD2 */
	LPFC_SOL_IOCB,		/* CMD_RCV_LOGIN                0xD3 */
	LPFC_SOL_IOCB,		/* CMD_ACCEPT_LOGIN             0xD4 */
	LPFC_SOL_IOCB,		/* CMD_REJECT_LOGIN             0xD5 */
	LPFC_UNSOL_IOCB,
	/* 0xD7 - 0xDF */
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB, LPFC_UNKNOWN_IOCB,
	/* 0xE0 */
	LPFC_UNSOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_SOL_IOCB,
	LPFC_UNSOL_IOCB
};

int
lpfc_sli_hba_setup(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *pmb;
	int read_rev_reset, i, rc;
	uint32_t status;
	unsigned long iflag;

	psli = &phba->sli;

	/* Setep SLI interface for HBA register and HBA SLIM access */
	lpfc_setup_slim_access(phba);

	/* Set board state to initialization started */
	phba->hba_state = LPFC_INIT_START;
	read_rev_reset = 0;

	iflag = phba->iflag;
	LPFC_DRVR_UNLOCK(phba, iflag);

	/* On some platforms/OS's, the driver can't rely on the state the
	 * adapter may be in.  For this reason, the driver is allowed to reset
	 * the HBA before initialization.
	 */
	if (lpfc_sli_reset_on_init) {
		phba->hba_state = 0;	/* Don't skip post */
		lpfc_sli_brdreset(phba);
		phba->hba_state = LPFC_INIT_START;
		lpfc_sleep_ms(phba, 2500);
	}

      top:
	/* Read the HBA Host Status Register */
	status = readl(phba->HSregaddr);

	i = 0;			/* counts number of times thru while loop */

	/* Check status register to see what current state is */
	while ((status & (HS_FFRDY | HS_MBRDY)) != (HS_FFRDY | HS_MBRDY)) {

		/* Check every 100ms for 5 retries, then every 500ms for 5, then
		 * every 2.5 sec for 5, then reset board and every 2.5 sec for
		 * 4.
		 */
		if (i++ >= 20) {
			/* Adapter failed to init, timeout, status reg
			   <status> */
			lpfc_printf_log(phba->brd_no,
				&lpfc_msgBlk0436,
				lpfc_mes0436,
				lpfc_msgBlk0436.msgPreambleStr,
				status);
			phba->hba_state = LPFC_HBA_ERROR;
			LPFC_DRVR_LOCK(phba, iflag);
			return (ETIMEDOUT);
		}

		/* Check to see if any errors occurred during init */
		if (status & HS_FFERM) {
			/* ERROR: During chipset initialization */
			/* Adapter failed to init, chipset, status reg
			   <status> */
			lpfc_printf_log(phba->brd_no,
				&lpfc_msgBlk0437,
				lpfc_mes0437,
				lpfc_msgBlk0437.msgPreambleStr,
				status);
			phba->hba_state = LPFC_HBA_ERROR;
			LPFC_DRVR_LOCK(phba, iflag);
			return (EIO);
		}

		if (i <= 5) {
			lpfc_sleep_ms(phba, 100);
		} else if (i <= 10) {
			lpfc_sleep_ms(phba, 500);
		} else {
			lpfc_sleep_ms(phba, 2500);
		}

		if (i == 15) {
			phba->hba_state = 0;	/* Don't skip post */
			lpfc_sli_brdreset(phba);
			phba->hba_state = LPFC_INIT_START;
		}
		/* Read the HBA Host Status Register */
		status = readl(phba->HSregaddr);
	}

	/* Check to see if any errors occurred during init */
	if (status & HS_FFERM) {
		/* ERROR: During chipset initialization */
		/* Adapter failed to init, chipset, status reg <status> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0438,
			lpfc_mes0438,
			lpfc_msgBlk0438.msgPreambleStr,
			status);
		phba->hba_state = LPFC_HBA_ERROR;
		LPFC_DRVR_LOCK(phba, iflag);
		return (EIO);
	}

	/* Clear all interrupt enable conditions */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* setup host attn register */
	writel(0xffffffff, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */

	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	if ((pmb = (LPFC_MBOXQ_t *) lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		phba->hba_state = LPFC_HBA_ERROR;
		LPFC_DRVR_LOCK(phba, iflag);
		return (ENOMEM);
	}

	/* Call pre CONFIG_PORT mailbox command initialization.  A value of 0
	 * means the call was successful.  Any other nonzero value is a failure,
	 * but if ERESTART is returned, the driver may reset the HBA and try
	 * again.
	 */
	if ((rc = lpfc_config_port_prep(phba))) {
		if ((rc == ERESTART) && (read_rev_reset == 0)) {
			lpfc_mbox_free(phba, pmb);
			phba->hba_state = 0;	/* Don't skip post */
			lpfc_sli_brdreset(phba);
			phba->hba_state = LPFC_INIT_START;
			lpfc_sleep_ms(phba, 500);
			read_rev_reset = 1;
			goto top;
		}
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		LPFC_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}

	/* Setup and issue mailbox CONFIG_PORT command */
	phba->hba_state = LPFC_INIT_MBX_CMDS;
	lpfc_config_port(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> CONFIG_PORT,
		   mbxStatus <status> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0442,
			lpfc_mes0442,
			lpfc_msgBlk0442.msgPreambleStr,
			pmb->mb.mbxCommand,
			pmb->mb.mbxStatus,
			0);

		/* This clause gives the config_port call is given multiple
		   chances to succeed. */
		if (read_rev_reset == 0) {
			lpfc_mbox_free(phba, pmb);
			phba->hba_state = 0;	/* Don't skip post */
			lpfc_sli_brdreset(phba);
			phba->hba_state = LPFC_INIT_START;
			lpfc_sleep_ms(phba, 2500);
			read_rev_reset = 1;
			goto top;
		}

		psli->sliinit.sli_flag &= ~LPFC_SLI2_ACTIVE;
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		LPFC_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}

	if ((rc = lpfc_sli_ring_map(phba))) {
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		LPFC_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}
	psli->sliinit.sli_flag |= LPFC_PROCESS_LA;

	/* Call post CONFIG_PORT mailbox command initialization. */
	if ((rc = lpfc_config_port_post(phba))) {
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		LPFC_DRVR_LOCK(phba, iflag);
		return (ENXIO);
	}
	lpfc_mbox_free(phba, pmb);
	LPFC_DRVR_LOCK(phba, iflag);
	return (0);
}

int
lpfc_sli_ring_map(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *pmbox;
	int i;

	psli = &phba->sli;

	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	if ((pmb = (LPFC_MBOXQ_t *) lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		phba->hba_state = LPFC_HBA_ERROR;
		return (ENOMEM);
	}
	pmbox = &pmb->mb;

	/* Initialize the LPFC_SLI_RING_t structure for each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		/* Issue a CONFIG_RING mailbox command for each ring */
		phba->hba_state = LPFC_INIT_MBX_CMDS;
		lpfc_config_ring(phba, i, pmb);
		if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter failed to init, mbxCmd <cmd> CFG_RING,
			   mbxStatus <status>, ring <num> */
			lpfc_printf_log(phba->brd_no,
				&lpfc_msgBlk0446,
				lpfc_mes0446,
				lpfc_msgBlk0446.msgPreambleStr,
				pmbox->mbxCommand,
				pmbox->mbxStatus,
				i);
			phba->hba_state = LPFC_HBA_ERROR;
			lpfc_mbox_free(phba, pmb);
			return (ENXIO);
		}
	}
	lpfc_mbox_free(phba, pmb);
	return (0);
}

int
lpfc_sli_intr(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	uint32_t ha_copy, status;
	int i;

	psli = &phba->sli;
	psli->slistat.sliIntr++;
	ha_copy = lpfc_intr_prep(phba);

	if (!ha_copy) {

		/*
		 * Don't claim that interrupt
		 */
		return (1);
	}

	if (ha_copy & HA_ERATT) {	/* Link / board error */
		psli->slistat.errAttnEvent++;
		/* do what needs to be done, get error from STATUS REGISTER */
		status = readl(phba->HSregaddr);

		/* Clear Chip error bit */
		writel(HA_ERATT, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */
		phba->stopped = 1;
		/* Process the Error Attention */
		lpfc_handle_eratt(phba, status);
		return (0);
	}

	if (ha_copy & HA_MBATT) {	/* Mailbox interrupt */
		lpfc_sli_handle_mb_event(phba);
	}

	if (ha_copy & HA_LATT) {	/* Link Attention interrupt */
		/* Process the Link Attention */
		if (psli->sliinit.sli_flag & LPFC_PROCESS_LA) {
			lpfc_handle_latt(phba);
		}
	}

	/* Now process each ring */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		if ((ha_copy & HA_RXATT)
		    || (pring->flag & LPFC_DEFERRED_RING_EVENT)) {
			if (pring->flag & LPFC_STOP_IOCB_MASK) {
				pring->flag |= LPFC_DEFERRED_RING_EVENT;
			} else {
				lpfc_sli_handle_ring_event(phba, pring,
							   (ha_copy &
							    HA_RXMASK));
				pring->flag &= ~LPFC_DEFERRED_RING_EVENT;
			}
		}
		ha_copy = (ha_copy >> 4);
	}

	return (0);
}

int
lpfc_sli_chk_mbx_command(uint8_t mbxCommand)
{
	uint8_t ret;

	switch (mbxCommand) {
	case MBX_LOAD_SM:
	case MBX_READ_NV:
	case MBX_WRITE_NV:
	case MBX_RUN_BIU_DIAG:
	case MBX_INIT_LINK:
	case MBX_DOWN_LINK:
	case MBX_CONFIG_LINK:
	case MBX_CONFIG_RING:
	case MBX_RESET_RING:
	case MBX_READ_CONFIG:
	case MBX_READ_RCONFIG:
	case MBX_READ_SPARM:
	case MBX_READ_STATUS:
	case MBX_READ_RPI:
	case MBX_READ_XRI:
	case MBX_READ_REV:
	case MBX_READ_LNK_STAT:
	case MBX_REG_LOGIN:
	case MBX_UNREG_LOGIN:
	case MBX_READ_LA:
	case MBX_CLEAR_LA:
	case MBX_DUMP_MEMORY:
	case MBX_DUMP_CONTEXT:
	case MBX_RUN_DIAGS:
	case MBX_RESTART:
	case MBX_UPDATE_CFG:
	case MBX_DOWN_LOAD:
	case MBX_DEL_LD_ENTRY:
	case MBX_RUN_PROGRAM:
	case MBX_SET_MASK:
	case MBX_SET_SLIM:
	case MBX_UNREG_D_ID:
	case MBX_CONFIG_FARP:
	case MBX_LOAD_AREA:
	case MBX_RUN_BIU_DIAG64:
	case MBX_CONFIG_PORT:
	case MBX_READ_SPARM64:
	case MBX_READ_RPI64:
	case MBX_REG_LOGIN64:
	case MBX_READ_LA64:
	case MBX_FLASH_WR_ULA:
	case MBX_SET_DEBUG:
	case MBX_LOAD_EXP_ROM:
		ret = mbxCommand;
		break;
	default:
		ret = MBX_SHUTDOWN;
		break;
	}
	return (ret);
}


/* lpfc_sli_turn_on_ring is only called by lpfc_sli_handle_mb_event below */
static void
lpfc_sli_turn_on_ring(lpfcHBA_t * phba, int ringno)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	PGP *pgp;
	uint32_t status;
	uint32_t portCmdGet, portGetIndex;


	psli = &phba->sli;
	pring = &psli->ring[ringno];
	pgp = (PGP *) & (((MAILBOX_t *)psli->MBhostaddr)->us.s2.port[ringno]);

	/* If the ring is active, flag it */
	if (psli->ring[ringno].cmdringaddr) {
		if (psli->ring[ringno].flag & LPFC_STOP_IOCB_MBX) {
			psli->ring[ringno].flag &= ~LPFC_STOP_IOCB_MBX;
			portGetIndex = lpfc_sli_resume_iocb(phba, pring);
			/* Make sure the host slim pointers are up-to-date
			 * before continuing.  An update is NOT guaranteed on
			 * the first read.
			 */
			status = pgp->cmdGetInx;
			portCmdGet = le32_to_cpu(status);
			if (portGetIndex != portCmdGet) {
				lpfc_sli_resume_iocb(phba, pring);
			}
			/* If this is the FCP ring, the scheduler needs to be
			   restarted. */
			if (pring->ringno == psli->fcp_ring) {
				lpfc_sched_check(phba);
			}
		}
	}
}

int
lpfc_sli_handle_mb_event(lpfcHBA_t * phba)
{
	MAILBOX_t *mbox;
	MAILBOX_t *pmbox;
	LPFC_MBOXQ_t *pmb;
	DMABUF_t   *mp;
	LPFC_SLI_t *psli;
	int i;
	uint32_t process_next;


	psli = &phba->sli;
	/* We should only get here if we are in SLI2 mode */
	if (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {
		return (1);
	}

	psli->slistat.mboxEvent++;

	/* Get a Mailbox buffer to setup mailbox commands for callback */
	if ((pmb = psli->mbox_active)) {
		pmbox = &pmb->mb;
		mbox = (MAILBOX_t *) psli->MBhostaddr;

		/* First check out the status word */
		lpfc_sli_pcimem_bcopy((uint32_t *) mbox, (uint32_t *) pmbox,
				     sizeof (uint32_t));

		/* Sanity check to ensure the host owns the mailbox */
		if (pmbox->mbxOwner != OWN_HOST) {
			/* Lets try for a while */
			for (i = 0; i < 10240; i++) {
				/* First copy command data */
				lpfc_sli_pcimem_bcopy((uint32_t *) mbox,
						     (uint32_t *) pmbox,
						     sizeof (uint32_t));
				if (pmbox->mbxOwner == OWN_HOST)
					goto mbout;
			}
			/* Stray Mailbox Interrupt, mbxCommand <cmd> mbxStatus
			   <status> */
			lpfc_printf_log(phba->brd_no,
				&lpfc_msgBlk0304,
				lpfc_mes0304,
				lpfc_msgBlk0304.msgPreambleStr,
				pmbox->mbxCommand,
				pmbox->mbxStatus);

			psli->sliinit.sli_flag |= LPFC_SLI_MBOX_ACTIVE;
			return (1);
		}

	      mbout:
		if (psli->mbox_tmo.function) {
			lpfc_stop_timer((struct clk_data *)psli->mbox_tmo.data);
		}

		/*
		 * It is a fatal error if unknown mbox command completion.
		 */
		if (lpfc_sli_chk_mbx_command(pmbox->mbxCommand) == 
		    MBX_SHUTDOWN) {

			/* Unknow mailbox command compl */
			lpfc_printf_log(phba->brd_no,
				&lpfc_msgBlk0323,
				lpfc_mes0323,
				lpfc_msgBlk0323.msgPreambleStr,
				pmbox->mbxCommand);
			phba->hba_state = LPFC_HBA_ERROR;
			phba->hba_flag |= FC_STOP_IO;
			lpfc_handle_eratt(phba, HS_FFER3);
			return (0);
		}

		psli->mbox_active = 0;
		if (pmbox->mbxStatus) {
			psli->slistat.mboxStatErr++;
			if (pmbox->mbxStatus == MBXERR_NO_RESOURCES) {
				/* Mbox cmd cmpl error - RETRYing */
				lpfc_printf_log(phba->brd_no,
					&lpfc_msgBlk0305,
					lpfc_mes0305,
					lpfc_msgBlk0305.msgPreambleStr,
					pmbox->mbxCommand,
					pmbox->mbxStatus,
					pmbox->un.varWords[0],
					phba->hba_state);
				pmbox->mbxStatus = 0;
				pmbox->mbxOwner = OWN_HOST;
				psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				if (lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT)
				    == MBX_SUCCESS) {
					return (0);
				}
			}
		}

		/* Mailbox cmd <cmd> Cmpl <cmpl> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0307,	
			lpfc_mes0307,
			lpfc_msgBlk0307.msgPreambleStr,
			pmbox->mbxCommand,
			pmb->mbox_cmpl,
			*((uint32_t *) pmbox),
			pmbox->un.varWords[0],
			pmbox->un.varWords[1],
			pmbox->un.varWords[2],
			pmbox->un.varWords[3],
			pmbox->un.varWords[4],
			pmbox->un.varWords[5],
			pmbox->un.varWords[6],
			pmbox->un.varWords[7]);


		if (pmb->mbox_cmpl) {
			/* Copy entire mbox completion over buffer */
			lpfc_sli_pcimem_bcopy((uint32_t *) mbox,
					     (uint32_t *) pmbox,
					     (sizeof (uint32_t) *
					      (MAILBOX_CMD_WSIZE)));
			/* All mbox cmpls are posted to discovery tasklet */
			lpfc_discq_post_event(phba, (void *)pmb, 0,
				LPFC_EVT_MBOX);
		} else {
			mp = (DMABUF_t *) (pmb->context1);
			if(mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			lpfc_mbox_free(phba, pmb);
		}
	}


	do {
		process_next = 0;	/* by default don't loop */
		psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;

		/* Process next mailbox command if there is one */
		if ((pmb = lpfc_mbox_get(phba))) {
			if (lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT) ==
			    MBX_NOT_FINISHED) {
				mp = (DMABUF_t *) (pmb->context1);
				if(mp) {
					lpfc_mbuf_free(phba, mp->virt,
						mp->phys);
					kfree(mp);
				}
				lpfc_mbox_free(phba, pmb);
				process_next = 1;
				continue;	/* loop back */
			}
		} else {
			/* Turn on IOCB processing */
			for (i = 0; i < psli->sliinit.num_rings; i++) {
				lpfc_sli_turn_on_ring(phba, i);
			}
		}
	} while (process_next);


	return (0);
}

int
lpfc_sli_handle_ring_event(lpfcHBA_t * phba,
			   LPFC_SLI_RING_t * pring, uint32_t mask)
{
	LPFC_SLI_t       * psli;
	IOCB_t           * entry;
	IOCB_t           * irsp;
	LPFC_IOCBQ_t     * rspiocbp;
	LPFC_IOCBQ_t     * cmdiocbp;
	LPFC_IOCBQ_t     * saveq;
	HGP              * hgp;
	PGP              * pgp;
	MAILBOX_t        * mbox;
	struct list_head * curr, * next;
	uint32_t           status, free_saveq;
	uint32_t           portRspPut, portRspMax;
	uint32_t           portCmdGet, portGetIndex;
	int                ringno, loopcnt, rc;
	uint8_t            type;
	void *to_slim;

	psli = &phba->sli;
	ringno = pring->ringno;
	psli->slistat.iocbEvent[ringno]++;
	irsp = 0;
	rc = 1;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	/* portRspMax is the number of rsp ring entries for this specific
	   ring. */
	portRspMax = psli->sliinit.ringinit[ringno].numRiocb;

	rspiocbp = 0;
	loopcnt = 0;

	/* Gather iocb entries off response ring.
	 * rspidx is the IOCB index of the next IOCB that the driver
	 * is going to process.
	 */
	entry = IOCB_ENTRY(pring->rspringaddr, pring->rspidx);
	status = pgp->rspPutInx;
	portRspPut = le32_to_cpu(status);

	if (portRspPut >= portRspMax) {

		/* Ring <ringno> handler: portRspPut <portRspPut> is bigger then
		   rsp ring <portRspMax> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0312,
				lpfc_mes0312,
				lpfc_msgBlk0312.msgPreambleStr,
				ringno, portRspPut, portRspMax);
		/*
		 * Treat it as adapter hardware error.
		 */
		phba->hba_state = LPFC_HBA_ERROR;
		phba->hba_flag |= FC_STOP_IO;
		lpfc_handle_eratt(phba, HS_FFER3);
		return (1);
	}
	
	rmb();

	/* Get the next available response iocb.
	 * rspidx is the IOCB index of the next IOCB that the driver
	 * is going to process.
	 */
	while (pring->rspidx != portRspPut) {
		/* get an iocb buffer to copy entry into */
		if ((rspiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			break;
		}

		lpfc_sli_pcimem_bcopy((uint32_t *) entry,
				      (uint32_t *) & rspiocbp->iocb,
				      sizeof (IOCB_t));
		wmb();
		irsp = &rspiocbp->iocb;

		/* bump iocb available response index */
		if (++pring->rspidx >= portRspMax) {
			pring->rspidx = 0;
		}

		/* Let the HBA know what IOCB slot will be the next one the
		 * driver will read a response from.
		 */
               

		status = (uint32_t) pring->rspidx;
		to_slim = (uint8_t *) phba->MBslimaddr +
		    (SLIMOFF + (ringno * 2) + 1) * 4;
		writel(status, to_slim);
		readl(to_slim); /* flush */
	

		/* chain all iocb entries until LE is set */
		if (list_empty(&(pring->iocb_continueq))) {
			list_add(&rspiocbp->list, &(pring->iocb_continueq));
		} else {
			list_add_tail(&rspiocbp->list,
				      &(pring->iocb_continueq));
		}
		pring->iocb_continueq_cnt++;

		/* when LE is set, entire Command has been received */
		if (irsp->ulpLe) {
			/* get a ptr to first iocb entry in chain and process
			   it */
			free_saveq = 1;
			saveq = list_entry(pring->iocb_continueq.next,
					   LPFC_IOCBQ_t, list);
			irsp = &(saveq->iocb);
			list_del_init(&pring->iocb_continueq);
			pring->iocb_continueq_cnt = 0;

			psli->slistat.iocbRsp[ringno]++;

			if (irsp->ulpStatus) {
				/* Rsp ring <ringno> error: IOCB Data: */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0325,
						lpfc_mes0325,
						lpfc_msgBlk0325.msgPreambleStr,
						ringno,
						irsp->un.ulpWord[0],
						irsp->un.ulpWord[1],
						irsp->un.ulpWord[2],
						irsp->un.ulpWord[3],
						irsp->un.ulpWord[4],
						irsp->un.ulpWord[5],
						*(((uint32_t *) irsp) + 6),
						*(((uint32_t *) irsp) + 7));
			}

			/* Determine if IOCB command is a solicited or
			   unsolicited event */
			type =
			    lpfc_sli_iocb_cmd_type[(irsp->
						    ulpCommand &
						    CMD_IOCB_MASK)];
			if (type == LPFC_SOL_IOCB) {
				rc = lpfc_sli_process_sol_iocb(phba, pring,
					saveq);
				if (pring->ringno == LPFC_ELS_RING)
					free_saveq = 0;
				else
					free_saveq = 1;
			} else if (type == LPFC_UNSOL_IOCB) {
				rc = lpfc_sli_process_unsol_iocb(phba, pring,
					saveq);
				if (pring->ringno == LPFC_ELS_RING)
					free_saveq = 0;
				else
					free_saveq = 1;

			} else if (type == LPFC_ABORT_IOCB) {
				/* Solicited ABORT Responses */
				/* Based on the iotag field, get the cmd IOCB
				   from the txcmplq */
				if ((irsp->ulpCommand == CMD_ABORT_MXRI64_CN) &&
				    ((cmdiocbp =
				      lpfc_sli_ringtxcmpl_get(phba, pring,
							      saveq, 0)))) {
					/* Call the specified completion
					   routine */
					if (cmdiocbp->iocb_cmpl) {
						(cmdiocbp->iocb_cmpl) (phba,
							     cmdiocbp, saveq);
					} else {
						lpfc_iocb_free(phba, cmdiocbp);
					}
				}
			} else if (type == LPFC_UNKNOWN_IOCB) {
				if (irsp->ulpCommand == CMD_ADAPTER_MSG) {

					char adaptermsg[LPFC_MAX_ADPTMSG];

					memset(adaptermsg, 0,
					       LPFC_MAX_ADPTMSG);
					memcpy(&adaptermsg[0], (uint8_t *) irsp,
					       MAX_MSG_DATA);
					printk("lpfc%d: %s", phba->brd_no,
					       adaptermsg);
				} else {
					/* Unknown IOCB command */
					lpfc_printf_log(phba->brd_no,
						&lpfc_msgBlk0321,
						lpfc_mes0321,
						lpfc_msgBlk0321.msgPreambleStr,
						irsp->ulpCommand,
						irsp->ulpStatus,
						irsp->ulpIoTag,
						irsp->ulpContext);
				}
			}

			if(free_saveq) {
				/* Free up iocb buffer chain for command just
			   	processed */

				if (!list_empty(&pring->iocb_continueq)) {
					list_for_each_safe(curr, next,
						   &pring->iocb_continueq) {
					rspiocbp =
					   list_entry(curr, LPFC_IOCBQ_t, list);
						list_del_init(&rspiocbp->list);
					lpfc_iocb_free(phba, rspiocbp);
				}
				}
				lpfc_iocb_free(phba, saveq);
			}
		}

		/* Entire Command has been received */
		entry = IOCB_ENTRY(pring->rspringaddr, pring->rspidx);

		/* If the port response put pointer has not been updated, sync
		 * the pgp->rspPutInx in the MAILBOX_tand fetch the new port
		 * response put pointer.
		 */
		if (pring->rspidx == portRspPut) {
			status = pgp->rspPutInx;
			portRspPut = le32_to_cpu(status);
		}
	}			/* while (pring->rspidx != portRspPut) */

	if ((rspiocbp != 0) && (mask & HA_R0RE_REQ)) {
		/* At least one response entry has been freed */
		psli->slistat.iocbRspFull[ringno]++;
		/* SET RxRE_RSP in Chip Att register */
		status = ((CA_R0ATT | CA_R0RE_RSP) << (ringno * 4));
		wmb();
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */
	}
	if ((mask & HA_R0CE_RSP) && (pring->flag & LPFC_CALL_RING_AVAILABLE)) {
		pring->flag &= ~LPFC_CALL_RING_AVAILABLE;
		psli->slistat.iocbCmdEmpty[ringno]++;
		portGetIndex = lpfc_sli_resume_iocb(phba, pring);

		/* Read the new portGetIndex value twice to ensure it was
		   updated correctly. */
		status = pgp->cmdGetInx;
		portCmdGet = le32_to_cpu(status);
		if (portGetIndex != portCmdGet) {
			lpfc_sli_resume_iocb(phba, pring);
		}
		if ((psli->sliinit.ringinit[ringno].lpfc_sli_cmd_available))
			(psli->sliinit.ringinit[ringno].
			 lpfc_sli_cmd_available) (phba, pring);

		/* Restart the scheduler on the FCP ring. */
		if (pring->ringno == psli->fcp_ring) {
			lpfc_sched_check(phba);
		}
	}
	return (rc);
}

int
lpfc_sli_process_sol_iocb(lpfcHBA_t * phba,
			   LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t *saveq)
{
	LPFC_IOCBQ_t * cmdiocbp;
	int            ringno, rc;

	rc = 1;
	ringno = pring->ringno;
	/* Solicited Responses */
	/* Based on the iotag field, get the cmd IOCB
	   from the txcmplq */
	if ((cmdiocbp =
	     lpfc_sli_ringtxcmpl_get(phba, pring, saveq,
				     0))) {
		/* Call the specified completion
		   routine */
		if (cmdiocbp->iocb_cmpl) {
			/* All iocb cmpls for LPFC_ELS_RING 
			 * are posted to discovery tasklet.
			 */
			if(ringno == LPFC_ELS_RING) {
				lpfc_discq_post_event(phba, (void *)cmdiocbp,
					(void *)saveq,  LPFC_EVT_SOL_IOCB);
			}
			else {
				if (cmdiocbp->iocb_flag & LPFC_IO_POLL) {
					rc = 0;
				}
				(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
			}
		} else {
			lpfc_iocb_free(phba, cmdiocbp);
		}
	} else {
		/* Could not find the initiating command
		 * based of the response iotag.
		 */
		/* Ring <ringno> handler: unexpected
		   completion IoTag <IoTag> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0322,
			lpfc_mes0322,
			lpfc_msgBlk0322.msgPreambleStr,
			ringno,
			saveq->iocb.ulpIoTag,
			saveq->iocb.ulpStatus,
			saveq->iocb.un.ulpWord[4],
			saveq->iocb.ulpCommand,
			saveq->iocb.ulpContext);
	}
	return(rc);
}

int
lpfc_sli_process_unsol_iocb(lpfcHBA_t * phba,
			   LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t *saveq)
{
	LPFC_SLI_t       * psli;
	IOCB_t           * irsp;
	LPFC_RING_INIT_t * pringinit;
	WORD5            * w5p;
	uint32_t           Rctl, Type;
	uint32_t           match, ringno, i;

	psli = &phba->sli;
	match = 0;
	ringno = pring->ringno;
	irsp = &(saveq->iocb);
	if ((irsp->ulpCommand == CMD_RCV_ELS_REQ64_CX)
	    || (irsp->ulpCommand == CMD_RCV_ELS_REQ_CX)) {
		Rctl = FC_ELS_REQ;
		Type = FC_ELS_DATA;
	} else {
		w5p =
		    (WORD5 *) & (saveq->iocb.un.
				 ulpWord[5]);
		Rctl = w5p->hcsw.Rctl;
		Type = w5p->hcsw.Type;
	}
	/* unSolicited Responses */
	pringinit = &psli->sliinit.ringinit[ringno];
	if (pringinit->prt[0].profile) {
		/* If this ring has a profile set, just
		   send it to prt[0] */
		/* All unsol iocbs for LPFC_ELS_RING 
		 * are posted to discovery tasklet.
		 */
		if(ringno == LPFC_ELS_RING) {
			lpfc_discq_post_event(phba, (void *)&pringinit->prt[0],
			(void *)saveq,  LPFC_EVT_UNSOL_IOCB);
		}
		else {
			(pringinit->prt[0].
		 	lpfc_sli_rcv_unsol_event) (phba, pring, saveq);
		}
		match = 1;
	} else {
		/* We must search, based on rctl / type
		   for the right routine */
		for (i = 0; i < pringinit->num_mask;
		     i++) {
			if ((pringinit->prt[i].rctl ==
			     Rctl)
			    && (pringinit->prt[i].
				type == Type)) {
				/* All unsol iocbs for LPFC_ELS_RING 
				 * are posted to discovery tasklet.
				 */
				if(ringno == LPFC_ELS_RING) {
					lpfc_discq_post_event(phba,
					(void *)&pringinit->prt[i],
					(void *)saveq,  LPFC_EVT_UNSOL_IOCB);
				}
				else {
					(pringinit->prt[i].
				 	lpfc_sli_rcv_unsol_event)
				    	(phba, pring, saveq);
				}
				match = 1;
				break;
			}
		}
	}
	if (match == 0) {
		/* Unexpected Rctl / Type received */
		/* Ring <ringno> handler: unexpected
		   Rctl <Rctl> Type <Type> received */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0313,
			lpfc_mes0313,
			lpfc_msgBlk0313.msgPreambleStr,
			ringno,
			Rctl,
			Type);
	}
	return(1);
}

/*! lpfc_mbox_timeout
 * 
 * \pre
 * \post
 * \param hba Pointer to per lpfcHBA_t structure
 * \param l1  Pointer to the driver's mailbox queue.
 * \return 
 *   void
 *
 * \b Description:
 *
 * This routine handles mailbox timeout events at timer interrupt context.
 */
void
lpfc_mbox_timeout(unsigned long ptr)
{
	lpfcHBA_t *phba;
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *pmbox;
	MAILBOX_t *mb;
	DMABUF_t * mp;
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

	pmbox = (LPFC_MBOXQ_t *) clkData->clData1;
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	psli = &phba->sli;
	mb = &pmbox->mb;

	/* Mbox cmd <mbxCommand> timeout */
	lpfc_printf_log(phba->brd_no,
		&lpfc_msgBlk0310,
		lpfc_mes0310,
		lpfc_msgBlk0310.msgPreambleStr,
		mb->mbxCommand,
		phba->hba_state,
		psli->sliinit.sli_flag,
		psli->mbox_active);

	if (psli->mbox_active == pmbox) {
		psli->mbox_active = 0;
		if (pmbox->mbox_cmpl) {
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) (phba, pmbox);
		} else {
			mp = (DMABUF_t *) (pmbox->context1);
			if(mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			lpfc_mbox_free(phba, pmbox);
		}
		psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	}

	lpfc_mbox_abort(phba);
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}

void
lpfc_mbox_abort(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *pmbox;
	DMABUF_t * mp;
	MAILBOX_t *mb;

	psli = &phba->sli;

	if (psli->mbox_active) {
		if (psli->mbox_tmo.function) {
			lpfc_stop_timer((struct clk_data *)psli->mbox_tmo.data);
		}
		pmbox = psli->mbox_active;
		mb = &pmbox->mb;
		psli->mbox_active = 0;
		if (pmbox->mbox_cmpl) {
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) (phba, pmbox);
		} else {
			mp = (DMABUF_t *) (pmbox->context1);
			if(mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			lpfc_mbox_free(phba, pmbox);
		}
		psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	}

	/* Abort all the non active mailbox commands. */
	pmbox = lpfc_mbox_get(phba);
	while (pmbox) {
		mb = &pmbox->mb;
		if (pmbox->mbox_cmpl) {
			mb->mbxStatus = MBX_NOT_FINISHED;
			(pmbox->mbox_cmpl) (phba, pmbox);
		} else {
			mp = (DMABUF_t *) (pmbox->context1);
			if(mp) {
				lpfc_mbuf_free(phba, mp->virt, mp->phys);
				kfree(mp);
			}
			lpfc_mbox_free(phba, pmbox);
		}
		pmbox = lpfc_mbox_get(phba);
	}
	return;
}

int
lpfc_sli_issue_mbox(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmbox, uint32_t flag)
{
	MAILBOX_t *mbox;
	MAILBOX_t *mb;
	LPFC_SLI_t *psli;
	uint32_t status, evtctr;
	uint32_t ha_copy;
	int i;
	unsigned long drvr_flag;
	volatile uint32_t word0, ldata;
	void *to_slim;

	psli = &phba->sli;
	if (flag & MBX_POLL) {
		LPFC_DRVR_LOCK(phba, drvr_flag);
	}

	mb = &pmbox->mb;
	status = MBX_SUCCESS;

	if (psli->sliinit.sli_flag & LPFC_SLI_MBOX_ACTIVE) {
		/* Polling for a mbox command when another one is already active
		 * is not allowed in SLI. Also, the driver must have established
		 * SLI2 mode to queue and process multiple mbox commands.
		 */

		if (flag & MBX_POLL) {
			LPFC_DRVR_UNLOCK(phba, drvr_flag);

			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
			return (MBX_NOT_FINISHED);
		}

		if (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {

			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag)
			return (MBX_NOT_FINISHED);
		}

		/* Handle STOP IOCB processing flag. This is only meaningful
		 * if we are not polling for mbox completion.
		 */
		if (flag & MBX_STOP_IOCB) {
			flag &= ~MBX_STOP_IOCB;
			/* Now flag each ring */
			for (i = 0; i < psli->sliinit.num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |=
					    LPFC_STOP_IOCB_MBX;
				}
			}
		}

		/* Another mailbox command is still being processed, queue this
		 * command to be processed later.
		 */
		lpfc_mbox_put(phba, pmbox);

		/* Mbox cmd issue - BUSY */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0308,
			lpfc_mes0308,
			lpfc_msgBlk0308.msgPreambleStr,
			mb->mbxCommand,
			phba->hba_state,
			psli->sliinit.sli_flag,
			flag);

		psli->slistat.mboxBusy++;
		if (flag == MBX_POLL) {
			LPFC_DRVR_UNLOCK(phba, drvr_flag);
		}
		return (MBX_BUSY);
	}

	/* Handle STOP IOCB processing flag. This is only meaningful
	 * if we are not polling for mbox completion.
	 */
	if (flag & MBX_STOP_IOCB) {
		flag &= ~MBX_STOP_IOCB;
		if (flag == MBX_NOWAIT) {
			/* Now flag each ring */
			for (i = 0; i < psli->sliinit.num_rings; i++) {
				/* If the ring is active, flag it */
				if (psli->ring[i].cmdringaddr) {
					psli->ring[i].flag |=
					    LPFC_STOP_IOCB_MBX;
				}
			}
		}
	}

	psli->sliinit.sli_flag |= LPFC_SLI_MBOX_ACTIVE;

	/* If we are not polling, we MUST be in SLI2 mode */
	if (flag != MBX_POLL) {
		if (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {
			psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;

			/* Mbox command <mbxCommand> cannot issue */
			LOG_MBOX_CANNOT_ISSUE_DATA( phba, mb, psli, flag);
			return (MBX_NOT_FINISHED);
		}
		/* timeout active mbox command */
		if (psli->mbox_tmo.function) {
			mod_timer(&psli->mbox_tmo, LPFC_MBOX_TMO);
		} else {
			lpfc_start_timer(phba, LPFC_MBOX_TMO, &psli->mbox_tmo,
					 lpfc_mbox_timeout,
					 (unsigned long)pmbox,
					 (unsigned long)0);
		}
	}

	/* Mailbox cmd <cmd> issue */
	lpfc_printf_log(phba->brd_no,
		&lpfc_msgBlk0309,
		lpfc_mes0309,
		lpfc_msgBlk0309.msgPreambleStr,
		mb->mbxCommand,
		phba->hba_state,
		psli->sliinit.sli_flag,
		flag);

	psli->slistat.mboxCmd++;
	evtctr = psli->slistat.mboxEvent;

	/* next set own bit for the adapter and copy over command word */
	mb->mbxOwner = OWN_CHIP;

	if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {

		/* First copy command data to host SLIM area */
		mbox = (MAILBOX_t *) psli->MBhostaddr;
		lpfc_sli_pcimem_bcopy((uint32_t *) mb, (uint32_t *) mbox,
				      (sizeof (uint32_t) *
				       (MAILBOX_CMD_WSIZE)));

	} else {
		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* copy command data into host mbox for cmpl */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			lpfc_sli_pcimem_bcopy((uint32_t *) mb,
					      (uint32_t *) mbox,
					      (sizeof (uint32_t) *
					       (MAILBOX_CMD_WSIZE)));
		}

		/* First copy mbox command data to HBA SLIM, skip past first
		   word */
		to_slim = (uint8_t *) phba->MBslimaddr + sizeof (uint32_t);
		lpfc_memcpy_to_slim(to_slim, (void *)&mb->un.varWords[0],
			    (MAILBOX_CMD_WSIZE - 1) * sizeof (uint32_t));

		/* Next copy over first word, with mbxOwner set */
		ldata = *((volatile uint32_t *)mb);
		to_slim = phba->MBslimaddr;
		writel(ldata, to_slim);
		readl(to_slim); /* flush */

		if (mb->mbxCommand == MBX_CONFIG_PORT) {
			/* switch over to host mailbox */
			psli->sliinit.sli_flag |= LPFC_SLI2_ACTIVE;
		}
	}

	wmb();
	/* interrupt board to doit right away */
	writel(CA_MBATT, phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */

	switch (flag) {
	case MBX_NOWAIT:
		/* Don't wait for it to finish, just return */
		psli->mbox_active = pmbox;
		break;

	case MBX_POLL:
		i = 0;
		psli->mbox_active = 0;
		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
			/* First read mbox status word */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			word0 = *((volatile uint32_t *)mbox);
			word0 = le32_to_cpu(word0);
		} else {
			/* First read mbox status word */
			word0 = readl(phba->MBslimaddr);
		}

		/* Read the HBA Host Attention Register */
		ha_copy = readl(phba->HAregaddr);

		/* Wait for command to complete */
		while (((word0 & OWN_CHIP) == OWN_CHIP)
		       || !(ha_copy & HA_MBATT)) {
			if (i++ >= 100) {
				psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
				LPFC_DRVR_UNLOCK(phba, drvr_flag);
				return (MBX_NOT_FINISHED);
			}

			/* Check if we took a mbox interrupt while we were
			   polling */
			if (((word0 & OWN_CHIP) != OWN_CHIP)
			    && (evtctr != psli->slistat.mboxEvent))
				break;

			LPFC_DRVR_UNLOCK(phba, drvr_flag);

			/* If in interrupt context do not sleep */
			lpfc_sleep_ms(phba, i);

			LPFC_DRVR_LOCK(phba, drvr_flag);

			if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
				/* First copy command data */
				mbox = (MAILBOX_t *) psli->MBhostaddr;
				word0 = *((volatile uint32_t *)mbox);
				word0 = le32_to_cpu(word0);
				if (mb->mbxCommand == MBX_CONFIG_PORT) {
					MAILBOX_t *slimmb;
					volatile uint32_t slimword0;
					/* Check real SLIM for any errors */
					slimword0 = readl(phba->MBslimaddr);
					slimmb = (MAILBOX_t *) & slimword0;
					if (((slimword0 & OWN_CHIP) != OWN_CHIP)
					    && slimmb->mbxStatus) {
						psli->sliinit.sli_flag &=
						    ~LPFC_SLI2_ACTIVE;
						word0 = slimword0;
					}
				}
			} else {
				/* First copy command data */
				word0 = readl(phba->MBslimaddr);
			}
			/* Read the HBA Host Attention Register */
			ha_copy = readl(phba->HAregaddr);
		}

		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
			/* First copy command data */
			mbox = (MAILBOX_t *) psli->MBhostaddr;
			/* copy results back to user */
			lpfc_sli_pcimem_bcopy((uint32_t *) mbox,
					      (uint32_t *) mb,
					      (sizeof (uint32_t) *
					       MAILBOX_CMD_WSIZE));
		} else {
			/* First copy command data */
			lpfc_memcpy_from_slim((void *)mb,
				      phba->MBslimaddr,
				      sizeof (uint32_t) * (MAILBOX_CMD_WSIZE));
			if ((mb->mbxCommand == MBX_DUMP_MEMORY) &&
			       pmbox->context2) {
				lpfc_memcpy_from_slim((void *)pmbox->context2,
				      phba->MBslimaddr + DMP_RSP_OFFSET,
				      mb->un.varDmp.word_cnt);
			}
		}

		wmb();
		writel(HA_MBATT, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */

		psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
		status = mb->mbxStatus;
	}

	if (flag == MBX_POLL) {
		LPFC_DRVR_UNLOCK(phba, drvr_flag);
	}
	return (status);
}

int
lpfc_sli_issue_iocb(lpfcHBA_t * phba,
		    LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * piocb,
		    uint32_t flag)
{
	LPFC_SLI_t *psli;
	IOCB_t *iocb;
	IOCB_t *icmd = 0;
	HGP *hgp;
	PGP *pgp;
	MAILBOX_t *mbox;
	LPFC_IOCBQ_t *nextiocb;
	LPFC_IOCBQ_t *lastiocb;
	uint32_t status;
	int ringno, loopcnt;
	uint32_t portCmdGet, portCmdMax;
	void *to_slim;
	void *from_slim;

	psli = &phba->sli;
	ringno = pring->ringno;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	/* portCmdMax is the number of cmd ring entries for this specific
	   ring. */
	portCmdMax = psli->sliinit.ringinit[ringno].numCiocb;

	/* portCmdGet is the IOCB index of the next IOCB that the HBA
	 * is going to process.
	 */
	status = pgp->cmdGetInx;
	portCmdGet = le32_to_cpu(status);

	/* We should never get an IOCB if we are in a < LINK_DOWN state */
	if (phba->hba_state < LPFC_LINK_DOWN) {
		/* If link is not initialized, just return */
		return (IOCB_ERROR);
	}

	/* Check to see if we are blocking IOCB processing because of a
	 * outstanding mbox command.
	 */
	if (pring->flag & LPFC_STOP_IOCB_MBX) {
		/* Queue command to ring xmit queue */
		if (!(flag & SLI_IOCB_RET_IOCB)) {
			lpfc_sli_ringtx_put(phba, pring, piocb);
		}
		psli->slistat.iocbCmdDelay[ringno]++;
		return (IOCB_BUSY);
	}

	if (phba->hba_state == LPFC_LINK_DOWN) {
		icmd = &piocb->iocb;
		if ((icmd->ulpCommand == CMD_QUE_RING_BUF_CN) ||
		    (icmd->ulpCommand == CMD_QUE_RING_BUF64_CN) ||
		    (icmd->ulpCommand == CMD_CLOSE_XRI_CN) ||
		    (icmd->ulpCommand == CMD_ABORT_XRI_CN)) {
			/* For IOCBs, like QUE_RING_BUF, that have no rsp ring 
			 * completion, iocb_cmpl MUST be 0.
			 */
			if (piocb->iocb_cmpl) {
				piocb->iocb_cmpl = 0;
			}
		} else {
			if ((icmd->ulpCommand != CMD_CREATE_XRI_CR)) {
				/* Queue command to ring xmit queue */
				if (!(flag & SLI_IOCB_RET_IOCB)) {
					lpfc_sli_ringtx_put(phba, pring, piocb);
				}

				/* If link is down, just return */
				psli->slistat.iocbCmdDelay[ringno]++;
				return (IOCB_BUSY);
			}
		}
		/* Only CREATE_XRI and QUE_RING_BUF can be issued if the link
		 * is not up.
		 */
	} else {
		/* For FCP commands, we must be in a state where we can process
		 * link attention events.
		 */
		if (!(psli->sliinit.sli_flag & LPFC_PROCESS_LA) &&
		    (pring->ringno == psli->fcp_ring)) {
			/* Queue command to ring xmit queue */
			if (!(flag & SLI_IOCB_RET_IOCB)) {
				lpfc_sli_ringtx_put(phba, pring, piocb);
			}
			psli->slistat.iocbCmdDelay[ringno]++;
			return (IOCB_BUSY);
		}
	}

	/* onetime should only be set for QUE_RING_BUF or CREATE_XRI
	 * iocbs sent with link down.
	 */

	/* Get the next available command iocb.
	 * cmdidx is the IOCB index of the next IOCB that the driver
	 * is going to issue a command with.
	 */
	iocb = IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

	if (portCmdGet >= portCmdMax) {

		/* Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then
		   cmd ring <portCmdMax> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0314,
			lpfc_mes0314,
			lpfc_msgBlk0314.msgPreambleStr,
			ringno,
			portCmdGet,
			portCmdMax);
		/* Queue command to ring xmit queue */
		if (!(flag & SLI_IOCB_RET_IOCB)) {
			lpfc_sli_ringtx_put(phba, pring, piocb);
		}
		psli->slistat.iocbCmdDelay[ringno]++;

		/*
		 * Treat it as adapter hardware error.
		 */
		phba->hba_state = LPFC_HBA_ERROR;
		phba->hba_flag |= FC_STOP_IO;
		lpfc_handle_eratt(phba, HS_FFER3);
		return (IOCB_BUSY);
	}

	/* Bump driver iocb command index to next IOCB */
	if (++pring->cmdidx >= portCmdMax) {
		pring->cmdidx = 0;
	}
	lastiocb = 0;
	loopcnt = 0;

	/* Check to see if this is a high priority
	   command. If so bypass tx queue processing.
	 */

	if (flag & SLI_IOCB_HIGH_PRIORITY) {
		nextiocb = 0;
		goto afterloop;
	}

	/* While IOCB entries are available */
	while (pring->cmdidx != portCmdGet) {
		/* If there is anything on the tx queue, process it before
		   piocb */
		if (((nextiocb = lpfc_sli_ringtx_get(phba, pring)) == 0)
		    && (piocb == 0)) {
		      issueout:
			/* Make sure cmdidx is in sync with the HBA's current
			   value. */
			
			from_slim = (uint8_t *) phba->MBslimaddr +
			    (SLIMOFF + (ringno * 2)) * 4;
			status = readl(from_slim);
			pring->cmdidx = (uint8_t) status;
		

			/* Interrupt the HBA to let it know there is work to do
			 * in ring ringno.
			 */
			status = ((CA_R0ATT) << (ringno * 4));
			wmb();
			writel(status, phba->CAregaddr);
			readl(phba->CAregaddr); /* flush */

			/* If we are waiting for the IOCB to complete before
			   returning */
			if ((flag & SLI_IOCB_POLL) && lastiocb) {
				uint32_t loopcnt, ha_copy;
				int retval;

				/* Wait 10240 loop iterations + 30 seconds
				   before timing out the IOCB. */
				for (loopcnt = 0; loopcnt < (10240 + 30);
				     loopcnt++) {
					ha_copy = lpfc_intr_prep(phba);
					ha_copy = (ha_copy >> (ringno * 4));
					if (ha_copy & HA_RXATT) {
						retval =
						    lpfc_sli_handle_ring_event
						    (phba, pring,
						     (ha_copy & HA_RXMASK));

						/*
						 * The IOCB requires to poll for
						 * completion.  If retval is
						 * identically 0, the iocb has
						 * been handled.  Otherwise,
						 * wait and retry.
						 */
						if (retval == 0) {
							break;
						}
					}
					if (loopcnt > 10240) {
						/* 1 second delay */
						lpfc_sleep_ms(phba, 1000);
					}
				}
				if (loopcnt >= (10240 + 30)) {
					/* Command timed out */
					/* Based on the iotag field, get the cmd
					   IOCB from the txcmplq */
					if ((lastiocb =
					     lpfc_sli_ringtxcmpl_get(phba,
								     pring,
								     lastiocb,
								     0))) {
						/* Call the specified completion
						   routine */
						icmd = &lastiocb->iocb;
						icmd->ulpStatus =
						    IOSTAT_LOCAL_REJECT;
						icmd->un.ulpWord[4] =
						    IOERR_SEQUENCE_TIMEOUT;
						if (lastiocb->iocb_cmpl) {
							(lastiocb->
							 iocb_cmpl) (phba,
								     lastiocb,
								     lastiocb);
						} else {
							lpfc_iocb_free(phba,
							       lastiocb);
						}
					}
				}
			}
			return (IOCB_SUCCESS);
		}

afterloop:

		/* If there is nothing left in the tx queue, now we can send
		   piocb */
		if (nextiocb == 0) {
			nextiocb = piocb;
			piocb = 0;
		}
		icmd = &nextiocb->iocb;

		/* issue iocb command to adapter */
		lpfc_sli_pcimem_bcopy((uint32_t *) icmd, (uint32_t *) iocb,
				      sizeof (IOCB_t));
		wmb();
		psli->slistat.iocbCmd[ringno]++;

		/* If there is no completion routine to call, we can release the
		 * IOCB buffer back right now. For IOCBs, like QUE_RING_BUF,
		 * that have no rsp ring completion, iocb_cmpl MUST be 0.
		 */
		if (nextiocb->iocb_cmpl) {
			lpfc_sli_ringtxcmpl_put(phba, pring, nextiocb);
		} else {
			lpfc_iocb_free(phba, nextiocb);
		}

		/* Let the HBA know what IOCB slot will be the next one the
		 * driver will put a command into.
		 */
		
		
		status = (uint32_t) pring->cmdidx;
		to_slim = (uint8_t *) phba->MBslimaddr
		    + (SLIMOFF + (ringno * 2)) * 4;
		writel(status, to_slim);
		readl(to_slim);
	

		/* Get the next available command iocb.
		 * cmdidx is the IOCB index of the next IOCB that the driver
		 * is going to issue a command with.
		 */
		iocb = IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

		/* Bump driver iocb command index to next IOCB */
		if (++pring->cmdidx >= portCmdMax) {
			pring->cmdidx = 0;
		}

		lastiocb = nextiocb;

		/* Make sure the ring's command index has been updated.  If 
		 * not, sync the slim memory area and refetch the command index.
		 */
		if (pring->cmdidx == portCmdGet) {
			status = pgp->cmdGetInx;
			portCmdGet = le32_to_cpu(status);
		}

	}			/* pring->cmdidx != portCmdGet */

	if (piocb == 0 && !(flag & SLI_IOCB_HIGH_PRIORITY)) {
		goto issueout;
	} else if (piocb == 0) {
		/* Make sure cmdidx is in sync with the HBA's current value. */
		
		from_slim =
		    (uint8_t *) phba->MBslimaddr + (SLIMOFF +
						    (ringno * 2)) * 4;
		status = readl(from_slim);
		pring->cmdidx = (uint8_t) status;
	

		/* Interrupt the HBA to let it know there is work to do
		 * in ring ringno.
		 */
		status = ((CA_R0ATT) << (ringno * 4));
		wmb();
		writel(status, phba->CAregaddr);
		readl(phba->CAregaddr); /* flush */

		return (IOCB_SUCCESS);
	}

	/* This code is executed only if the command ring is full.  Wait for the
	 * HBA to process some entries before handing it more work.
	 */

	/* Make sure cmdidx is in sync with the HBA's current value. */
      	from_slim =
		    (uint8_t *) phba->MBslimaddr + (SLIMOFF + (ringno * 2)) * 4;
	status = readl(from_slim);
	pring->cmdidx = (uint8_t) status;


	pring->flag |= LPFC_CALL_RING_AVAILABLE; /* indicates cmd ring was
						    full */
	/* 
	 * Set ring 'ringno' to SET R0CE_REQ in Chip Att register.
	 * The HBA will tell us when an IOCB entry is available.
	 */
	status = ((CA_R0ATT | CA_R0CE_REQ) << (ringno * 4));
	wmb();
	writel(status, phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */

	psli->slistat.iocbCmdFull[ringno]++;

	/* Queue command to ring xmit queue */
	if ((!(flag & SLI_IOCB_RET_IOCB)) && piocb) {
		lpfc_sli_ringtx_put(phba, pring, piocb);
	}

	return (IOCB_BUSY);
}

int
lpfc_sli_resume_iocb(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring)
{
	LPFC_SLI_t *psli;
	IOCB_t *iocb;
	IOCB_t *icmd = 0;
	HGP *hgp;
	PGP *pgp;
	MAILBOX_t *mbox;
	LPFC_IOCBQ_t *nextiocb;
	uint32_t status;
	int ringno, loopcnt;
	uint32_t portCmdGet, portCmdMax;
	void *to_slim;
	void *from_slim;

	psli = &phba->sli;
	ringno = pring->ringno;

	/* At this point we assume SLI-2 */
	mbox = (MAILBOX_t *) psli->MBhostaddr;
	pgp = (PGP *) & mbox->us.s2.port[ringno];
	hgp = (HGP *) & mbox->us.s2.host[ringno];

	/* portCmdMax is the number of cmd ring entries for this specific
	   ring. */
	portCmdMax = psli->sliinit.ringinit[ringno].numCiocb;

	/* portCmdGet is the IOCB index of the next IOCB that the HBA
	 * is going to process.
	 */
	status = pgp->cmdGetInx;
	portCmdGet = le32_to_cpu(status);

	/* First check to see if there is anything on the txq to send */
	if (pring->txq_cnt == 0) {
		return (portCmdGet);
	}

	if (phba->hba_state <= LPFC_LINK_DOWN) {
		return (portCmdGet);
	}
	/* For FCP commands, we must be in a state where we can process
	 * link attention events.
	 */
	if (!(psli->sliinit.sli_flag & LPFC_PROCESS_LA) &&
	    (pring->ringno == psli->fcp_ring)) {
		return (portCmdGet);
	}

	/* Check to see if we are blocking IOCB processing because of a
	 * outstanding mbox command.
	 */
	if (pring->flag & LPFC_STOP_IOCB_MBX) {
		return (portCmdGet);
	}

	/* Get the next available command iocb.
	 * cmdidx is the IOCB index of the next IOCB that the driver
	 * is going to issue a command with.
	 */
	iocb = IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

	if (portCmdGet >= portCmdMax) {

		/* Ring <ringno> issue: portCmdGet <portCmdGet> is bigger
		   then cmd ring <portCmdMax> */
		lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0315,
			lpfc_mes0315,
			lpfc_msgBlk0315.msgPreambleStr,
			ringno,
			portCmdGet,
			portCmdMax);

		return (portCmdGet);
	}

	/* Bump driver iocb command index to next IOCB */
	if (++pring->cmdidx >= portCmdMax) {
		pring->cmdidx = 0;
	}
	loopcnt = 0;

	/* While IOCB entries are available */
	while (pring->cmdidx != portCmdGet) {
		/* If there is anything on the tx queue, process it */
		if ((nextiocb = lpfc_sli_ringtx_get(phba, pring)) == 0) {
			/* Make sure cmdidx is in sync with the HBA's current
			   value. */
			
			from_slim =
			    (uint8_t *) phba->MBslimaddr + (SLIMOFF +
							    (ringno *
							     2)) * 4;
			status = readl(from_slim);
			pring->cmdidx = (uint8_t) status;
		

			/* Interrupt the HBA to let it know there is work to do
			 * in ring ringno.
			 */
			status = ((CA_R0ATT) << (ringno * 4));
			wmb();
			writel(status, phba->CAregaddr);
			readl(phba->CAregaddr); /* flush */
			return (portCmdGet);
		}
		icmd = &nextiocb->iocb;

		/* issue iocb command to adapter */
		lpfc_sli_pcimem_bcopy((uint32_t *) icmd, (uint32_t *) iocb,
				      sizeof (IOCB_t));
		wmb();
		psli->slistat.iocbCmd[ringno]++;

		/* If there is no completion routine to call, we can release the
		 * IOCB buffer back right now. For IOCBs, like QUE_RING_BUF,
		 * that have no rsp ring completion, iocb_cmpl MUST be 0.
		 */
		if (nextiocb->iocb_cmpl) {
			lpfc_sli_ringtxcmpl_put(phba, pring, nextiocb);
		} else {
			lpfc_iocb_free(phba, nextiocb);
		}

		/* Let the HBA know what IOCB slot will be the next one the
		 * driver will put a command into.
		 */
		

		status = (uint32_t) pring->cmdidx;
		to_slim =
		    (uint8_t *) phba->MBslimaddr + (SLIMOFF +
						    (ringno * 2)) * 4;
		writel(status, to_slim);
		readl(to_slim);
	

		/* Get the next available command iocb.
		 * cmdidx is the IOCB index of the next IOCB that the driver
		 * is going to issue a command with.
		 */
		iocb = IOCB_ENTRY(pring->cmdringaddr, pring->cmdidx);

		/* Bump driver iocb command index to next IOCB */
		if (++pring->cmdidx >= portCmdMax) {
			pring->cmdidx = 0;
		}

		/* Make sure the ring's command index has been updated.  If 
		 * not, sync the slim memory area and refetch the command index.
		 */
		if (pring->cmdidx == portCmdGet) {
			/* sync pgp->cmdGetInx in the MAILBOX_t */
			status = pgp->cmdGetInx;
			portCmdGet = le32_to_cpu(status);
		}
	}

	/* This code is executed only if the command ring is full.  Wait for the
	 * HBA to process some entries before handing it more work.
	 */
	/* Make sure cmdidx is in sync with the HBA's current value. */
   
	from_slim = (uint8_t *) phba->MBslimaddr +
	    (SLIMOFF + (ringno * 2)) * 4;
	status = readl(from_slim);
	pring->cmdidx = (uint8_t) status;


	pring->flag |= LPFC_CALL_RING_AVAILABLE; /* indicates cmd ring was
						    full */
	/* 
	 * Set ring 'ringno' to SET R0CE_REQ in Chip Att register.
	 * The HBA will tell us when an IOCB entry is available.
	 */
	status = ((CA_R0ATT | CA_R0CE_REQ) << (ringno * 4));
	wmb();
	writel(status, phba->CAregaddr);
	readl(phba->CAregaddr); /* flush */

	psli->slistat.iocbCmdFull[ringno]++;;
	return (portCmdGet);
}

#define BARRIER_TEST_PATTERN (0xdeadbeef)

void lpfc_reset_barrier(lpfcHBA_t * phba)
{
	uint32_t * resp_buf;
	uint32_t * mbox_buf;
	volatile uint32_t mbox;
	uint32_t hc_copy;
	int  i;
	uint8_t hdrtype;

	pci_read_config_byte(phba->pcidev, PCI_HEADER_TYPE, &hdrtype);
	if (hdrtype != 0x80 ||
	    (FC_JEDEC_ID(phba->vpd.rev.biuRev) != HELIOS_JEDEC_ID &&
	     FC_JEDEC_ID(phba->vpd.rev.biuRev) != THOR_JEDEC_ID))
		return;

	/*
	 * Tell the other part of the chip to suspend temporarily all
	 * its DMA activity.
	 */
	resp_buf =  (uint32_t *)phba->MBslimaddr;

	/* Disable the error attention */
	hc_copy = readl(phba->HCregaddr);
	writel((hc_copy & ~HC_ERINT_ENA), phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	if (readl(phba->HAregaddr) & HA_ERATT) {
		/* Clear Chip error bit */
		writel(HA_ERATT, phba->HAregaddr);
		phba->stopped = 1;
	}

	mbox = 0;
	((MAILBOX_t *)&mbox)->mbxCommand = MBX_KILL_BOARD;
	((MAILBOX_t *)&mbox)->mbxOwner = OWN_CHIP;

	writel(BARRIER_TEST_PATTERN, (resp_buf + 1));
	mbox_buf = (uint32_t *)phba->MBslimaddr;
	writel(mbox, mbox_buf);

	for (i = 0;
	     readl(resp_buf + 1) != ~(BARRIER_TEST_PATTERN) && i < 50; i++)
		mdelay(1);

	if (readl(resp_buf + 1) != ~(BARRIER_TEST_PATTERN)) {
		if (phba->sli.sliinit.sli_flag & LPFC_SLI2_ACTIVE ||
		    phba->stopped)
			goto restore_hc;
		else
			goto clear_errat;
	}

	((MAILBOX_t *)&mbox)->mbxOwner = OWN_HOST;
	for (i = 0; readl(resp_buf) != mbox &&  i < 500; i++)
		mdelay(1);

clear_errat:

	while (!(readl(phba->HAregaddr) & HA_ERATT) && ++i < 500)
		mdelay(1);

	/* Clear Chip error bit */
	if (readl(phba->HAregaddr) & HA_ERATT) {
		writel(HA_ERATT, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */
		phba->stopped = 1;
	}

restore_hc:
	writel(hc_copy, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */
}

int
lpfc_sli_brdreset(lpfcHBA_t * phba)
{
	MAILBOX_t *swpmb;
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	uint16_t cfg_value, skip_post;
	volatile uint32_t word0;
	int i;
	void *to_slim;
	struct list_head *curr, *next;
	DMABUF_t * mp;

	psli = &phba->sli;

	lpfc_reset_barrier(phba);

	/* A board reset must use REAL SLIM. */
	psli->sliinit.sli_flag &= ~LPFC_SLI2_ACTIVE;

	word0 = 0;
	swpmb = (MAILBOX_t *) & word0;
	swpmb->mbxCommand = MBX_RESTART;
	swpmb->mbxHc = 1;

	to_slim = phba->MBslimaddr;
	writel(*(uint32_t *) swpmb, to_slim);
	readl(to_slim); /* flush */

	/* Only skip post after fc_ffinit is completed */
	if (phba->hba_state) {
		skip_post = 1;
		word0 = 1;	/* This is really setting up word1 */
	} else {
		skip_post = 0;
		word0 = 0;	/* This is really setting up word1 */
	}
	to_slim = (uint8_t *) phba->MBslimaddr + sizeof (uint32_t);
	writel(*(uint32_t *) swpmb, to_slim);
	readl(to_slim); /* flush */

	/* Reset HBA */
	lpfc_printf_log(phba->brd_no,
			&lpfc_msgBlk0326,
			lpfc_mes0326,
			lpfc_msgBlk0326.msgPreambleStr,
			phba->hba_state,
			psli->sliinit.sli_flag);

	/* Turn off SERR, PERR in PCI cmd register */
	phba->hba_state = LPFC_INIT_START;

	/* perform board reset */
	phba->fc_eventTag = 0;
	phba->fc_myDID = 0;
	phba->fc_prevDID = 0;

	/* Turn off parity checking and serr during the physical reset */
	pci_read_config_word(phba->pcidev, PCI_COMMAND, &cfg_value);
	pci_write_config_word(phba->pcidev, PCI_COMMAND,
			      (cfg_value &
			       ~(PCI_COMMAND_PARITY | PCI_COMMAND_SERR)));

	/* Now toggle INITFF bit in the Host Control Register */
	writel(HC_INITFF, phba->HCregaddr);
	mdelay(1);
	readl(phba->HCregaddr); /* flush */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Restore PCI cmd register */

	pci_write_config_word(phba->pcidev, PCI_COMMAND, cfg_value);
	phba->hba_state = LPFC_INIT_START;
	phba->stopped = 0;
	/* Initialize relevant SLI info */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag = 0;
		pring->rspidx = 0;
		pring->cmdidx = 0;
		pring->missbufcnt = 0;
	}

	if (skip_post) {
		mdelay(100);
	} else {
		mdelay(2000);
	}

	/* Now cleanup posted buffers on each ring */
	pring = &psli->ring[LPFC_ELS_RING];
	list_for_each_safe(curr, next, &pring->postbufq) {
		mp = list_entry(curr, DMABUF_t, list);
		list_del(&mp->list);
		pring->postbufq_cnt--;
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}

	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		lpfc_sli_abort_iocb_ring(phba, pring, LPFC_SLI_ABORT_IMED);
	}

	return (0);
}

int
lpfc_sli_queue_setup(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	int i, cnt;

	psli = &phba->sli;
	INIT_LIST_HEAD(&psli->mboxq);
	/* Initialize list headers for txq and txcmplq as double linked lists */
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->ringno = i;
		INIT_LIST_HEAD(&pring->txq);
		INIT_LIST_HEAD(&pring->txcmplq);
		INIT_LIST_HEAD(&pring->iocb_continueq);
		INIT_LIST_HEAD(&pring->postbufq);
		cnt = psli->sliinit.ringinit[i].fast_iotag;
		if (cnt) {
			pring->fast_lookup =
			    kmalloc(cnt * sizeof (LPFC_IOCBQ_t *), GFP_ATOMIC);
			if (pring->fast_lookup == 0) {
				return (0);
			}
			memset((char *)pring->fast_lookup, 0, 
			       cnt*sizeof (LPFC_IOCBQ_t *));
		}
	}
	return (1);
}

int
lpfc_sli_hba_down(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_MBOXQ_t *pmb;
	DMABUF_t * mp;
	LPFC_IOCBQ_t *iocb;
	IOCB_t *icmd = 0;
	int i, cnt;
	unsigned long iflag;
	struct list_head *curr, *next;

	psli = &phba->sli;

	iflag = phba->iflag;

	lpfc_hba_down_prep(phba);

	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->ring[i];
		pring->flag |= LPFC_DEFERRED_RING_EVENT;
		/* Error everything on txq  */
		pring->txq_cnt = 0;

		list_for_each_safe(curr, next, &pring->txq) {
			iocb = list_entry(curr, LPFC_IOCBQ_t, list);
			list_del_init(curr);
			if (iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_DOWN;
				(iocb->iocb_cmpl) (phba, iocb, iocb);
			} else {
				lpfc_iocb_free(phba, iocb);
			}
		}

		INIT_LIST_HEAD(&(pring->txq));

		/* Free any memory allocated for fast lookup */
		cnt = psli->sliinit.ringinit[i].fast_iotag;
		if (pring->fast_lookup) {
			kfree(pring->fast_lookup);
			pring->fast_lookup = 0;
		}
	}

	/* Return any active mbox cmds */
	if (psli->mbox_tmo.function) {
		lpfc_stop_timer((struct clk_data *)psli->mbox_tmo.data);
	}
	if ((psli->mbox_active)) {
		pmb = psli->mbox_active;
		mp = (DMABUF_t *) (pmb->context1);
		if((mp) && 
			(pmb->mbox_cmpl != lpfc_sli_wake_mbox_wait)) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		lpfc_mbox_free(phba, pmb);
	}
	psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	psli->mbox_active = 0;

	/* Return any pending mbox cmds */
	while ((pmb = lpfc_mbox_get(phba))) {
		mp = (DMABUF_t *) (pmb->context1);
		if ((mp) &&
		    (pmb->mbox_cmpl != lpfc_sli_wake_mbox_wait)) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		lpfc_mbox_free(phba, pmb);
	}

	INIT_LIST_HEAD(&psli->mboxq);

	/*
	 * Adapter can not handle any mbox command.
	 * Skip borad reset.
	 */
	if (phba->hba_state != LPFC_HBA_ERROR) {
		phba->hba_state = LPFC_INIT_START;
		lpfc_sli_brdreset(phba);
	}

	return (1);
}

void
lpfc_sli_pcimem_bcopy(uint32_t * src, uint32_t * dest, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src++;
		ldata = le32_to_cpu(ldata);
		*dest++ = ldata;
	}
}

int
lpfc_sli_ringpostbuf_put(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring,
			 DMABUF_t * mp)
{
	/* Stick DMABUF_t at end of postbufq so driver can look it up later */
	list_add_tail(&mp->list, &pring->postbufq);

	pring->postbufq_cnt++;
	return (0);
}

DMABUF_t *
lpfc_sli_ringpostbuf_get(lpfcHBA_t * phba,
			 LPFC_SLI_RING_t * pring, dma_addr_t phys)
{
	return (lpfc_sli_ringpostbuf_search(phba, pring, phys, 1));
}

DMABUF_t *
lpfc_sli_ringpostbuf_search(lpfcHBA_t * phba,
			    LPFC_SLI_RING_t * pring, dma_addr_t phys,
			    int unlink)
{
	DMABUF_t *mp;
	struct list_head *slp;
	int count;
	struct list_head *pos, *tpos;

	slp = &pring->postbufq;

	/* Search postbufq, from the begining, looking for a match on phys */
	count = 0;

	list_for_each_safe(pos, tpos, &pring->postbufq) {
		count++;
		mp = list_entry(pos, DMABUF_t, list);
		if (mp->phys == phys) {
			/* If we find a match, deque it and return it to the
			   caller */
			if (unlink) {
				list_del(&mp->list);

				pring->postbufq_cnt--;
			}
			return (mp);
		}
	}
	/* Cannot find virtual addr for mapped buf on ring <num> */
	lpfc_printf_log(phba->brd_no,
		&lpfc_msgBlk0410,
		lpfc_mes0410,
		lpfc_msgBlk0410.msgPreambleStr,
		pring->ringno,
		phys,
		slp->next,
		slp->prev,
		pring->postbufq_cnt);
	return (0);
}

int
lpfc_sli_ringtx_put(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring,
		    LPFC_IOCBQ_t * piocb)
{
	struct list_head *dlp;
	struct list_head *dlp_end;

	/* Stick IOCBQ_t at end of txq so driver can issue it later */
	dlp = &pring->txq;
	dlp_end = (struct list_head *)dlp->prev;
	list_add(&piocb->list, dlp_end);
	pring->txq_cnt++;
	return (0);
}

int
lpfc_sli_ringtxcmpl_put(lpfcHBA_t * phba,
			LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * piocb)
{
	struct list_head *dlp;
	struct list_head *dlp_end;
	IOCB_t *icmd = 0;
	LPFC_SLI_t *psli;
	uint8_t *ptr;
	uint16_t iotag;

	dlp = &pring->txcmplq;
	dlp_end = dlp->prev;
	list_add(&piocb->list, dlp_end);

	pring->txcmplq_cnt++;
	ptr = (uint8_t *) (pring->fast_lookup);

	if (ptr) {
		/* Setup fast lookup based on iotag for completion */
		psli = &phba->sli;
		icmd = &piocb->iocb;
		iotag = icmd->ulpIoTag;
		if (iotag < psli->sliinit.ringinit[pring->ringno].fast_iotag) {
			ptr += (iotag * sizeof (LPFC_IOCBQ_t *));
			*((LPFC_IOCBQ_t **) ptr) = piocb;
		} else {

			/* Cmd ring <ringno> put: iotag <iotag> greater then
			   configured max <fast_iotag> wd0 <icmd> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0316,
					lpfc_mes0316,
					lpfc_msgBlk0316.msgPreambleStr,
					pring->ringno, iotag, psli->sliinit
					.ringinit[pring->ringno].fast_iotag,
					*(((uint32_t *) icmd) + 7));
		}
	}
	return (0);
}

LPFC_IOCBQ_t *
lpfc_sli_ringtx_get(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring)
{
	struct list_head *dlp;
	LPFC_IOCBQ_t *cmd_iocb;
	LPFC_IOCBQ_t *next_iocb;

	dlp = &pring->txq;
	cmd_iocb = 0;
	next_iocb = (LPFC_IOCBQ_t *) pring->txq.next;
	if (next_iocb != (LPFC_IOCBQ_t *) & pring->txq) {
		/* If the first ptr is not equal to the list header, 
		 * deque the IOCBQ_t and return it.
		 */
		cmd_iocb = next_iocb;
		list_del(&cmd_iocb->list);
		pring->txq_cnt--;
	}
	return (cmd_iocb);
}

LPFC_IOCBQ_t *
lpfc_sli_ringtxcmpl_get(lpfcHBA_t * phba,
			LPFC_SLI_RING_t * pring,
			LPFC_IOCBQ_t * prspiocb, uint32_t srch)
{
	struct list_head *dlp;
	IOCB_t *irsp = 0;
	LPFC_IOCBQ_t *cmd_iocb;
	LPFC_SLI_t *psli;
	uint8_t *ptr;
	uint16_t iotag;


	dlp = &pring->txcmplq;
	ptr = (uint8_t *) (pring->fast_lookup);

	if (ptr && (srch == 0)) {
		/* Use fast lookup based on iotag for completion */
		psli = &phba->sli;
		irsp = &prspiocb->iocb;
		iotag = irsp->ulpIoTag;
		if (iotag < psli->sliinit.ringinit[pring->ringno].fast_iotag) {
			ptr += (iotag * sizeof (LPFC_IOCBQ_t *));
			cmd_iocb = *((LPFC_IOCBQ_t **) ptr);
			*((LPFC_IOCBQ_t **) ptr) = 0;
			if (cmd_iocb == 0) {
				cmd_iocb = lpfc_search_txcmpl(pring, prspiocb);
				return(cmd_iocb);
			}

			list_del(&cmd_iocb->list);
			pring->txcmplq_cnt--;
		} else {

			/* Rsp ring <ringno> get: iotag <iotag> greater then
			   configured max <fast_iotag> wd0 <irsp> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0317,
					lpfc_mes0317,
					lpfc_msgBlk0317.msgPreambleStr,
					pring->ringno, iotag,
					psli->sliinit.ringinit[pring->ringno]
					.fast_iotag,
					*(((uint32_t *) irsp) + 7));

			cmd_iocb = lpfc_search_txcmpl(pring, prspiocb);
			return(cmd_iocb);
		}
	} else {
		cmd_iocb = lpfc_search_txcmpl(pring, prspiocb);
	}

	return (cmd_iocb);
}

LPFC_IOCBQ_t *
lpfc_search_txcmpl(LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * prspiocb)
{
	struct list_head *list, *list_tmp;
	IOCB_t *icmd = 0;
	IOCB_t *irsp = 0;
	LPFC_IOCBQ_t *cmd_iocb;
	LPFC_IOCBQ_t *next_iocb;
	uint16_t iotag;

	irsp = &prspiocb->iocb;
	iotag = irsp->ulpIoTag;
	cmd_iocb = 0;

	/* Search through txcmpl from the begining */
	list_for_each_safe(list, list_tmp, &(pring->txcmplq)) {
		next_iocb = (LPFC_IOCBQ_t *) list;
		icmd = &next_iocb->iocb;
		if (iotag == icmd->ulpIoTag) {
			/* found a match! */
			cmd_iocb = next_iocb;
			list_del(&cmd_iocb->list);
			pring->txcmplq_cnt--;
			break;
		}
	}

	return (cmd_iocb);
}


uint32_t
lpfc_sli_next_iotag(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring)
{
	LPFC_RING_INIT_t *pringinit;
	LPFC_SLI_t *psli;
	uint8_t *ptr;
	int i;

	psli = &phba->sli;
	pringinit = &psli->sliinit.ringinit[pring->ringno];
	for (i = 0; i < pringinit->iotag_max; i++) {
		/* Never give an iotag of 0 back */
		pringinit->iotag_ctr++;
		if (pringinit->iotag_ctr == pringinit->iotag_max) {
			pringinit->iotag_ctr = 1; /* Never use 0 as an iotag */
		}
		/* If fast_iotaging is used, we can ensure that the iotag 
		 * we give back is not already in use.
		 */
		if (pring->fast_lookup) {
			ptr = (uint8_t *) (pring->fast_lookup);
			ptr += (pringinit->iotag_ctr * sizeof (LPFC_IOCBQ_t *));
			if (*((LPFC_IOCBQ_t **) ptr) != 0)
				continue;
		}
		return (pringinit->iotag_ctr);
	}

	/* Outstanding I/O count for ring <ringno> is at max <fast_iotag> */
	lpfc_printf_log(phba->brd_no,
		&lpfc_msgBlk0318,
		lpfc_mes0318,
		lpfc_msgBlk0318.msgPreambleStr,
		pring->ringno,
		psli->sliinit.ringinit[pring->ringno].fast_iotag);
	return (0);
}

void
lpfc_sli_abort_cmpl(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
		    LPFC_IOCBQ_t * rspiocb)
{
	lpfc_iocb_free(phba, cmdiocb);
	return;
}

void
lpfc_sli_abort_elsreq_cmpl(lpfcHBA_t * phba, LPFC_IOCBQ_t * cmdiocb,
			   LPFC_IOCBQ_t * rspiocb)
{
	DMABUF_t *buf_ptr, *buf_ptr1;
	/* Free the resources associated with the ELS_REQUEST64 IOCB the driver
	 * just aborted.
	 * In this case, context2  = cmd,  context2->next = rsp, context3 = bpl 
	 */
	if (cmdiocb->context2) {
		buf_ptr1 = (DMABUF_t *) cmdiocb->context2;

		/* Free the response IOCB before completing the abort
		   command.  */
		if (!list_empty(&buf_ptr1->list)) {

			buf_ptr = list_entry(buf_ptr1->list.next,
					     DMABUF_t, list);

			list_del(&buf_ptr->list);
			lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
			kfree(buf_ptr);
		}
		lpfc_mbuf_free(phba, buf_ptr1->virt, buf_ptr1->phys);
		kfree(buf_ptr1);
	}

	if (cmdiocb->context3) {
		buf_ptr = (DMABUF_t *) cmdiocb->context3;
		lpfc_mbuf_free(phba, buf_ptr->virt, buf_ptr->phys);
		kfree(buf_ptr);
	}
	lpfc_iocb_free(phba, cmdiocb);
	return;
}

int
lpfc_sli_abort_iocb(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring,
		    LPFC_IOCBQ_t * piocb)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *abtsiocbp;
	uint8_t *ptr;
	IOCB_t *abort_cmd = 0, *cmd = 0;
	uint16_t iotag;

	psli = &phba->sli;
	cmd = &piocb->iocb;

	if (piocb->abort_count == 2) {
		/* Clear fast_lookup entry, if any */
		iotag = cmd->ulpIoTag;
		ptr = (uint8_t *) (pring->fast_lookup);
		if (ptr
		    && (iotag <
			psli->sliinit.ringinit[pring->ringno].fast_iotag)) {
			LPFC_IOCBQ_t *cmd_iocb;
			ptr += (iotag * sizeof (LPFC_IOCBQ_t *));
			cmd_iocb = *((LPFC_IOCBQ_t **) ptr);
			*((LPFC_IOCBQ_t **) ptr) = 0;
		}

		/* Dequeue and complete with error */
		list_del(&piocb->list);
		pring->txcmplq_cnt--;

		if (piocb->iocb_cmpl) {
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(piocb->iocb_cmpl) (phba, piocb, piocb);
		} else {
			lpfc_iocb_free(phba, piocb);
		}

		return (1);
	}

	/* issue ABTS for this IOCB based on iotag */

	if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI))
	    == 0) {
		return (0);
	}

	memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
	abort_cmd = &abtsiocbp->iocb;

	abort_cmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	abort_cmd->un.acxri.abortContextTag = cmd->ulpContext;
	abort_cmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	abort_cmd->ulpLe = 1;
	abort_cmd->ulpClass = cmd->ulpClass;
	if (phba->hba_state >= LPFC_LINK_UP) {
		abort_cmd->ulpCommand = CMD_ABORT_XRI_CN;
	} else {
		abort_cmd->ulpCommand = CMD_CLOSE_XRI_CN;
	}

	/* set up an iotag  */
	abort_cmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

	if (lpfc_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
	    == IOCB_ERROR) {
		lpfc_iocb_free(phba, abtsiocbp);
		return (0);
	}

	piocb->abort_count++;
	return (1);
}

int
lpfc_sli_abort_iocb_ring(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring,
			 uint32_t flag)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	uint8_t *ptr;
	IOCB_t *icmd = 0, *cmd = 0;
	int errcnt;
	uint16_t iotag;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error everything on txq and txcmplq 
	 * First do the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		list_del_init(&next_iocb->list);
		iocb = next_iocb;

		if (iocb->iocb_cmpl) {
			icmd = &iocb->iocb;
			icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(iocb->iocb_cmpl) (phba, iocb, iocb);
		} else {
			lpfc_iocb_free(phba, iocb);
		}
	}

	pring->txq_cnt = 0;
	INIT_LIST_HEAD(&(pring->txq));

	/* Next issue ABTS for everything on the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;

		if (flag == LPFC_SLI_ABORT_IMED) {
			/* Imediate abort of IOCB, deque and call compl */
			list_del_init(curr);
		}

		/* issue ABTS for this IOCB based on iotag */

		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		if (flag == LPFC_SLI_ABORT_IMED) {
			/* Clear fast_lookup entry, if any */
			iotag = cmd->ulpIoTag;
			ptr = (uint8_t *) (pring->fast_lookup);
			if (ptr
			    && (iotag <
				psli->sliinit.ringinit[pring->ringno].
				fast_iotag)) {
				ptr += (iotag * sizeof (LPFC_IOCBQ_t *));
				*((LPFC_IOCBQ_t **) ptr) = 0;
			}
			/* Imediate abort of IOCB, deque and call compl */
			if (iocb->iocb_cmpl) {
				cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				(iocb->iocb_cmpl) (phba, iocb, iocb);
			} else {
				lpfc_iocb_free(phba, iocb);
			}
			lpfc_iocb_free(phba, abtsiocbp);
		} else {
			/* set up an iotag  */
			icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

			if (lpfc_sli_issue_iocb
			    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
			    == IOCB_ERROR) {
				lpfc_iocb_free(phba, abtsiocbp);
				errcnt++;
				continue;
			}
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}

	if (flag == LPFC_SLI_ABORT_IMED) {
		INIT_LIST_HEAD(&(pring->txcmplq));
		pring->txcmplq_cnt = 0;
	}

	return (errcnt);
}

int
lpfc_sli_issue_abort_iotag32(lpfcHBA_t * phba,
			     LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * cmdiocb)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0;
	IOCB_t *iabt = 0;
	uint32_t iotag32;

	psli = &phba->sli;

	/* issue ABTS for this IOCB based on iotag */
	if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		return (0);
	}
	memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
	iabt = &abtsiocbp->iocb;

	icmd = &cmdiocb->iocb;
	switch (icmd->ulpCommand) {
	case CMD_ELS_REQUEST64_CR:
		iotag32 = icmd->un.elsreq64.bdl.ulpIoTag32;
		/* Even though we abort the ELS command, the firmware may access
		 * the BPL or other resources before it processes our
		 * ABORT_MXRI64. Thus we must delay reusing the cmdiocb
		 * resources till the actual abort request completes.
		 */
		abtsiocbp->context1 = (void *)((unsigned long)icmd->ulpCommand);
		abtsiocbp->context2 = cmdiocb->context2;
		abtsiocbp->context3 = cmdiocb->context3;
		cmdiocb->context2 = 0;
		cmdiocb->context3 = 0;
		abtsiocbp->iocb_cmpl = lpfc_sli_abort_elsreq_cmpl;
		break;
	default:
		lpfc_iocb_free(phba, abtsiocbp);
		return (0);
	}

	iabt->un.amxri.abortType = ABORT_TYPE_ABTS;
	iabt->un.amxri.iotag32 = iotag32;

	iabt->ulpLe = 1;
	iabt->ulpClass = CLASS3;
	iabt->ulpCommand = CMD_ABORT_MXRI64_CN;

	/* set up an iotag  */
	iabt->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

	if (lpfc_sli_issue_iocb(phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
	    == IOCB_ERROR) {
		lpfc_iocb_free(phba, abtsiocbp);
		return (0);
	}

	return (1);
}

int
lpfc_sli_abort_iocb_ctx(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring, uint32_t ctx)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0, *cmd = 0;
	int errcnt;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;
		if (cmd->ulpContext != ctx) {
			continue;
		}

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

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;
		if (cmd->ulpContext != ctx) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */
		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		/* set up an iotag  */
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

		if (lpfc_sli_issue_iocb
		    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			lpfc_iocb_free(phba, abtsiocbp);
			errcnt++;
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	return (errcnt);
}

int
lpfc_sli_abort_iocb_context1(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring,
			     void *ctx)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0, *cmd = 0;
	int errcnt;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;
		if (iocb->context1 != ctx) {
			continue;
		}

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

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;
		if (iocb->context1 != ctx) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */
		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		/* set up an iotag  */
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

		if (lpfc_sli_issue_iocb
		    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			lpfc_iocb_free(phba, abtsiocbp);
			errcnt++;
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	return (errcnt);
}

int
lpfc_sli_abort_iocb_lun(lpfcHBA_t * phba,
			LPFC_SLI_RING_t * pring,
			uint16_t scsi_target, uint64_t scsi_lun)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0, *cmd = 0;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	int errcnt;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
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
		    (lpfc_cmd->scsi_target != scsi_target) ||
		    (lpfc_cmd->scsi_lun != scsi_lun)) {
			continue;
		}

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

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
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
		    (lpfc_cmd->scsi_target != scsi_target) ||
		    (lpfc_cmd->scsi_lun != scsi_lun)) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */
		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		/* set up an iotag  */
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

		if (lpfc_sli_issue_iocb
		    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			lpfc_iocb_free(phba, abtsiocbp);
			errcnt++;
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	return (errcnt);
}

int
lpfc_sli_abort_iocb_tgt(lpfcHBA_t * phba,
			LPFC_SLI_RING_t * pring, uint16_t scsi_target)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0, *cmd = 0;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	int errcnt;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a LPFC_SCSI_BUF_t */
		lpfc_cmd = (LPFC_SCSI_BUF_t *) (iocb->context1);
		if ((lpfc_cmd == 0) || (lpfc_cmd->scsi_target != scsi_target)) {
			continue;
		}

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

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {

		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a LPFC_SCSI_BUF_t */
		lpfc_cmd = (LPFC_SCSI_BUF_t *) (iocb->context1);
		if ((lpfc_cmd == 0) || (lpfc_cmd->scsi_target != scsi_target)) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */
		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		/* set up an iotag  */
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

		if (lpfc_sli_issue_iocb
		    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			lpfc_iocb_free(phba, abtsiocbp);
			errcnt++;
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	return (errcnt);
}

int
lpfc_sli_abort_iocb_hba(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring)
{
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *iocb, *next_iocb;
	LPFC_IOCBQ_t *abtsiocbp;
	IOCB_t *icmd = 0, *cmd = 0;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	int errcnt;
	struct list_head *curr, *next;

	psli = &phba->sli;
	errcnt = 0;

	/* Error matching iocb on txq or txcmplq 
	 * First check the txq.
	 */
	list_for_each_safe(curr, next, &pring->txq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a LPFC_SCSI_BUF_t */
		lpfc_cmd = (LPFC_SCSI_BUF_t *) (iocb->context1);
		if (lpfc_cmd == 0) {
			continue;
		}

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

	/* Next check the txcmplq */
	list_for_each_safe(curr, next, &pring->txcmplq) {
		next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
		iocb = next_iocb;
		cmd = &iocb->iocb;

		/* Must be a FCP command */
		if ((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
		    (cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
			continue;
		}

		/* context1 MUST be a LPFC_SCSI_BUF_t */
		lpfc_cmd = (LPFC_SCSI_BUF_t *) (iocb->context1);
		if (lpfc_cmd == 0) {
			continue;
		}

		/* issue ABTS for this IOCB based on iotag */
		if ((abtsiocbp = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			errcnt++;
			continue;
		}
		memset(abtsiocbp, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &abtsiocbp->iocb;

		icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
		icmd->un.acxri.abortContextTag = cmd->ulpContext;
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

		icmd->ulpLe = 1;
		icmd->ulpClass = cmd->ulpClass;
		if (phba->hba_state >= LPFC_LINK_UP) {
			icmd->ulpCommand = CMD_ABORT_XRI_CN;
		} else {
			icmd->ulpCommand = CMD_CLOSE_XRI_CN;
		}

		/* set up an iotag  */
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

		if (lpfc_sli_issue_iocb
		    (phba, pring, abtsiocbp, SLI_IOCB_USE_TXQ)
		    == IOCB_ERROR) {
			lpfc_iocb_free(phba, abtsiocbp);
			errcnt++;
			continue;
		}
		/* The rsp ring completion will remove IOCB from txcmplq when 
		 * abort is read by HBA.
		 */
	}
	return (errcnt);
}

void
lpfc_sli_wake_iocb_wait(lpfcHBA_t * phba,
			LPFC_IOCBQ_t * queue1, LPFC_IOCBQ_t * queue2)
{
	wait_queue_head_t *pdone_q;

	queue1->iocb_flag |= LPFC_IO_WAIT;
	if (queue1->context2 && queue2)
		memcpy(queue1->context2, queue2, sizeof (LPFC_IOCBQ_t));
	pdone_q = queue1->context_un.hipri_wait_queue;
	if (pdone_q) {
		wake_up(pdone_q);
	}
	/* if pdone_q/ was NULL, it means the waiter already gave
	   up and returned, so we don't have to do anything */

	return;
}

int
lpfc_sli_issue_iocb_wait(lpfcHBA_t * phba,
			 LPFC_SLI_RING_t * pring,
			 LPFC_IOCBQ_t * piocb,
			 uint32_t flag,
			 LPFC_IOCBQ_t * prspiocbq, uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(done_q);
	DECLARE_WAITQUEUE(wq_entry, current);
	uint32_t timeleft = 0;
	int retval;
	unsigned long iflag = phba->iflag;

	/* The caller must leave context1 empty for the driver. */
	if (piocb->context_un.hipri_wait_queue != 0) {
		return (IOCB_ERROR);
	}
	/* If the caller has provided a response iocbq buffer, then context2 
	 * is NULL or its an error.
	 */
	if (prspiocbq) {
		if (piocb->context2) {
			return (IOCB_ERROR);
		}
		piocb->context2 = prspiocbq;
	}

	/* setup wake call as IOCB callback */
	piocb->iocb_cmpl = lpfc_sli_wake_iocb_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	piocb->context_un.hipri_wait_queue = &done_q;

	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&done_q, &wq_entry);

	/* now issue the command */
	retval = lpfc_sli_issue_iocb(phba, pring, piocb, flag);
	if ((retval == IOCB_SUCCESS) ||
	    ((!(flag & SLI_IOCB_RET_IOCB)) && retval == IOCB_BUSY)) {
		LPFC_DRVR_UNLOCK(phba, iflag);
		timeleft = schedule_timeout(timeout * HZ);
		LPFC_DRVR_LOCK(phba, iflag);
		piocb->context_un.hipri_wait_queue = 0;	/* prevents completion
							   function from
							   signalling */
		piocb->iocb_cmpl = 0;
		if (piocb->context2 == prspiocbq)
			piocb->context2 = 0;

		/* if schedule_timeout returns 0, we timed out and were not
		 * woken up if LPFC_IO_WAIT is not set, we go woken up by a
		 * signal.
		 */
		if ((timeleft == 0) || !(piocb->iocb_flag & LPFC_IO_WAIT)) {
			if (timeleft == 0)
				retval = IOCB_TIMEDOUT;

			if (piocb->list.next && piocb->list.prev)
				list_del((struct list_head *)piocb);
		}
	}
	remove_wait_queue(&done_q, &wq_entry);
	set_current_state(TASK_RUNNING);
	return retval;
}

void
lpfc_sli_wake_iocb_high_priority(lpfcHBA_t * phba,
				 LPFC_IOCBQ_t * queue1, LPFC_IOCBQ_t * queue2)
{
	if (queue1->context2 && queue2)
		memcpy(queue1->context2, queue2, sizeof (LPFC_IOCBQ_t));

	/* The waiter is looking for LPFC_IO_HIPRI bit to be set 
	   as a signal to wake up */
	queue1->iocb_flag |= LPFC_IO_HIPRI;

	return;
}

int
lpfc_sli_issue_iocb_wait_high_priority(lpfcHBA_t * phba,
				       LPFC_SLI_RING_t * pring,
				       LPFC_IOCBQ_t * piocb,
				       uint32_t flag,
				       LPFC_IOCBQ_t * prspiocbq,
				       uint32_t timeout)
{
	int j, delay_time, retval = IOCB_ERROR;
	unsigned long drvr_flag = phba->iflag;
	unsigned long iflag;

	/* The caller must left context1 empty.  */
	if (piocb->context_un.hipri_wait_queue != 0) {
		return (IOCB_ERROR);
	}
	/* If the caller has provided a response iocbq buffer, context2 is NULL
	 * or its an error.
	 */
	if (prspiocbq) {
		if (piocb->context2) {
			return (IOCB_ERROR);
		}
		piocb->context2 = prspiocbq;
	}

	piocb->context3 = 0;
	/* setup wake call as IOCB callback */
	piocb->iocb_cmpl = lpfc_sli_wake_iocb_high_priority;

	/* now issue the command */
	retval =
	    lpfc_sli_issue_iocb(phba, pring, piocb,
				flag | SLI_IOCB_HIGH_PRIORITY);

 	if (retval != IOCB_SUCCESS) {
		piocb->context2 = 0;
		return IOCB_ERROR;
	}


	/*
	 * This high-priority iocb was sent out-of-band.  Poll for its 
	 * completion rather than wait for a signal.  Note that the host_lock
	 * is held by the midlayer and must be released here to allow the 
	 * interrupt handlers to complete the IO and signal this routine via 
	 * the iocb_flag.
	 * Also, the delay_time is computed to be one second longer than
	 * the scsi command timeout to give the FW time to abort on 
	 * timeout rather than the driver just giving up.  Typically,
	 * the midlayer does not specify a time for this command so the
	 * driver is free to enforce its own timeout.
	 */

	retval = IOCB_ERROR;
	delay_time = ((timeout + 1) * 1000) >> 6; 
	for (j = 0; j < 64; j++) {
		LPFC_DRVR_UNLOCK(phba, drvr_flag);
		mdelay(delay_time);
		LPFC_DRVR_LOCK(phba, drvr_flag);

		spin_lock_irqsave(&phba->hiprilock, iflag);
		if (piocb->iocb_flag & LPFC_IO_HIPRI) {
			piocb->iocb_flag &= ~LPFC_IO_HIPRI;
			retval = IOCB_SUCCESS;
			spin_unlock_irqrestore(&phba->hiprilock, iflag);
			break;
		}
		spin_unlock_irqrestore(&phba->hiprilock, iflag);
	}

	piocb->context2 = 0;
	piocb->context3 = 0;

	return retval;
}

void
lpfc_sli_wake_mbox_wait(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmboxq)
{
	wait_queue_head_t *pdone_q;

	pdone_q = (wait_queue_head_t *) pmboxq->context1;
	if (pdone_q)
		wake_up_interruptible(pdone_q);
	/* if pdone_q was NULL, it means the waiter already gave
	   up and returned, so we don't have to do anything */

	return;
}

int
lpfc_sli_issue_mbox_wait(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmboxq,
			 uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(done_q);
	DECLARE_WAITQUEUE(wq_entry, current);
	uint32_t timeleft = 0;
	int retval;
	unsigned long iflag = phba->iflag;

	/* The caller must leave context1 empty. */
	if (pmboxq->context1 != 0) {
		return (MBX_NOT_FINISHED);
	}

	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = lpfc_sli_wake_mbox_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	pmboxq->context1 = &done_q;

	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&done_q, &wq_entry);

	/* now issue the command */
	retval = lpfc_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
	if (retval == MBX_BUSY || retval == MBX_SUCCESS) {
		LPFC_DRVR_UNLOCK(phba, iflag);
		timeleft = schedule_timeout(timeout * HZ);
		LPFC_DRVR_LOCK(phba, iflag);
		pmboxq->context1 = 0;
		/* if schedule_timeout returns 0, we timed out and were not
		   woken up */
		if ((timeleft == 0) || signal_pending(current))
			retval = MBX_TIMEOUT;
		else
			retval = MBX_SUCCESS;
	}


	set_current_state(TASK_RUNNING);
	remove_wait_queue(&done_q, &wq_entry);
	return retval;
}
