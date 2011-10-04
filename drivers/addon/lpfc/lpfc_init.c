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
 * $Id: lpfc_init.c 485 2006-03-28 16:18:51Z sf_support $
 */

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/pci.h>


#include <linux/blk.h>
#include <linux/ctype.h>

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc_cfgparm.h"
#include "lpfc_crtn.h"

extern lpfcDRVR_t lpfcDRVR;

int lpfc_parse_vpd(lpfcHBA_t *, uint8_t *);
int lpfc_post_rcv_buf(lpfcHBA_t *);
void lpfc_establish_link_tmo(unsigned long ptr);
int lpfc_check_for_vpd = 1;
int lpfc_rdrev_wd30 = 0;


/************************************************************************/
/*                                                                      */
/*   lpfc_swap_bcopy                                                    */
/*                                                                      */
/************************************************************************/
void
lpfc_swap_bcopy(uint32_t * src, uint32_t * dest, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src++;
		ldata = cpu_to_be32(ldata);
		*dest++ = ldata;
	}
}

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_prep                                             */
/*    This routine will do LPFC initialization prior to the             */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success or ERESTART if it wants         */
/*    the SLI layer to reset the HBA and try again. Any                 */
/*    other return value indicates an error.                            */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_prep(lpfcHBA_t * phba)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	lpfc_vpd_t *vp;
	char licensed[56] =
	    "key unlock for use with gnu public licensed code only\0";
	uint32_t *pText = (uint32_t *) licensed;

	vp = &phba->vpd;

	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	if ((pmb = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		phba->hba_state = LPFC_HBA_ERROR;
		return (ENOMEM);
	}
	mb = &pmb->mb;

	/* special handling for LC HBAs */
	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		/* Setup and issue mailbox READ NVPARAMS command */
		phba->hba_state = LPFC_INIT_MBX_CMDS;
		lpfc_read_nv(phba, pmb);
		memset((void *)mb->un.varRDnvp.rsvd3, 0,
		       sizeof (mb->un.varRDnvp.rsvd3));
		lpfc_swap_bcopy(pText, pText, 56);
		memcpy((void *)mb->un.varRDnvp.rsvd3, licensed,
		       sizeof (licensed));
		if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
			/* Adapter initialization error, mbxCmd <cmd>
			   READ_NVPARM, mbxStatus <status> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0324,
				       lpfc_mes0324,
				       lpfc_msgBlk0324.msgPreambleStr,
				       mb->mbxCommand, mb->mbxStatus);
			return (ERESTART);
		}
		memcpy((uint8_t *) phba->wwnn,
		       (uint8_t *) mb->un.varRDnvp.nodename,
		       sizeof (mb->un.varRDnvp.nodename));
	}

	/* Setup and issue mailbox READ REV command */
	phba->hba_state = LPFC_INIT_MBX_CMDS;
	lpfc_read_rev(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <mbxCmd> READ_REV, mbxStatus
		   <status> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0439,
				lpfc_mes0439,
				lpfc_msgBlk0439.msgPreambleStr,
				mb->mbxCommand, mb->mbxStatus);
		lpfc_mbox_free(phba, pmb);
		return (ERESTART);
	}

	/* The HBA's current state is provided by the ProgType and rr fields.
	 * Read and check the value of these fields before continuing to config
	 * this port.
	 */
	if (mb->un.varRdRev.rr == 0) {
		/* Old firmware */
		vp->rev.rBit = 0;
		/* Adapter failed to init, mbxCmd <cmd> READ_REV detected
		   outdated firmware */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0440,
				lpfc_mes0440,
				lpfc_msgBlk0440.msgPreambleStr,
				mb->mbxCommand, 0);

		lpfc_mbox_free(phba, pmb);
		return (ERESTART);
	} else {
		if (mb->un.varRdRev.un.b.ProgType != 2) {
			lpfc_mbox_free(phba, pmb);
			return (ERESTART);
		}
		vp->rev.rBit = 1;
		vp->rev.sli1FwRev = mb->un.varRdRev.sli1FwRev;
		memcpy((uint8_t *) vp->rev.sli1FwName,
		       (uint8_t *) mb->un.varRdRev.sli1FwName, 16);
		vp->rev.sli2FwRev = mb->un.varRdRev.sli2FwRev;
		memcpy((uint8_t *) vp->rev.sli2FwName,
		       (uint8_t *) mb->un.varRdRev.sli2FwName, 16);
	}

	/* Save information as VPD data */
	vp->rev.biuRev = mb->un.varRdRev.biuRev;
	vp->rev.smRev = mb->un.varRdRev.smRev;
	vp->rev.smFwRev = mb->un.varRdRev.un.smFwRev;
	vp->rev.endecRev = mb->un.varRdRev.endecRev;
	vp->rev.fcphHigh = mb->un.varRdRev.fcphHigh;
	vp->rev.fcphLow = mb->un.varRdRev.fcphLow;
	vp->rev.feaLevelHigh = mb->un.varRdRev.feaLevelHigh;
	vp->rev.feaLevelLow = mb->un.varRdRev.feaLevelLow;
	vp->rev.postKernRev = mb->un.varRdRev.postKernRev;
	vp->rev.opFwRev = mb->un.varRdRev.opFwRev;
	lpfc_rdrev_wd30 = mb->un.varWords[30];

	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		memcpy((uint8_t *) phba->RandomData,
		       (uint8_t *) & mb->un.varWords[24],
		       sizeof (phba->RandomData));
	}

	/* Get the default values for Model Name and Description */
	lpfc_get_hba_model_desc(phba, phba->ModelName, phba->ModelDesc);

	if (lpfc_check_for_vpd) {
		uint32_t *lpfc_vpd_data = 0;
		uint16_t offset = 0;

		/* Get adapter VPD information */
		lpfc_vpd_data = kmalloc(DMP_VPD_SIZE, GFP_ATOMIC);
		pmb->context2 = kmalloc(DMP_RSP_SIZE, GFP_ATOMIC);

		do {
			lpfc_dump_mem(phba, pmb, offset);

			if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
				/*
				 * Let it go through even if failed.
				 */
				/* Adapter failed to init, mbxCmd <cmd> DUMP VPD,
				   mbxStatus <status> */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0441,
						lpfc_mes0441,
						lpfc_msgBlk0441.msgPreambleStr,
						mb->mbxCommand, mb->mbxStatus);
				kfree(lpfc_vpd_data);
				lpfc_vpd_data = 0;
				break;
			}

			lpfc_sli_pcimem_bcopy((uint32_t *)pmb->context2,
					      (uint32_t*)((uint8_t*)lpfc_vpd_data + offset),
					      mb->un.varDmp.word_cnt);

			offset += mb->un.varDmp.word_cnt;
		} while (mb->un.varDmp.word_cnt);

		lpfc_parse_vpd(phba, (uint8_t *)lpfc_vpd_data);

		if (pmb->context2)
			kfree(pmb->context2);
		if (lpfc_vpd_data)
			kfree(lpfc_vpd_data);
		pmb->context2 = 0;
	}
	lpfc_mbox_free(phba, pmb);
	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_config_port_post                                             */
/*    This routine will do LPFC initialization after the                */
/*    CONFIG_PORT mailbox command. This will be initialized             */
/*    as a SLI layer callback routine.                                  */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_config_port_post(lpfcHBA_t * phba)
{
	LPFC_MBOXQ_t *pmb;
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_SLI_t *psli;
	lpfcCfgParam_t *clp;
	uint32_t status;
	int i, j, flogi_sent;
	unsigned long iflag, isr_cnt, clk_cnt;
	uint32_t timeout;

	psli = &phba->sli;
	clp = &phba->config[0];

	/* Get a Mailbox buffer to setup mailbox commands for HBA
	   initialization */
	if ((pmb = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		phba->hba_state = LPFC_HBA_ERROR;
		return (ENOMEM);
	}
	mb = &pmb->mb;

	/* Setup link timers */
	lpfc_config_link(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> CONFIG_LINK mbxStatus
		   <status> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0447,
				lpfc_mes0447,
				lpfc_msgBlk0447.msgPreambleStr,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		return (EIO);
	}

	/* Get login parameters for NID.  */
	lpfc_read_sparam(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> READ_SPARM mbxStatus
		   <status> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0448,
				lpfc_mes0448,
				lpfc_msgBlk0448.msgPreambleStr,
				mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		return (EIO);
	}

	mp = (DMABUF_t *) pmb->context1;

	memcpy((uint8_t *) & phba->fc_sparam, (uint8_t *) mp->virt,
	       sizeof (SERV_PARM));
	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	pmb->context1 = 0;

	memcpy((uint8_t *) & phba->fc_nodename,
	       (uint8_t *) & phba->fc_sparam.nodeName, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & phba->fc_portname,
	       (uint8_t *) & phba->fc_sparam.portName, sizeof (NAME_TYPE));
	memcpy(phba->phys_addr, phba->fc_portname.IEEE, 6);
	/* If no serial number in VPD data, use low 6 bytes of WWNN */
	if (phba->SerialNumber[0] == 0) {
		uint8_t *outptr;

		outptr = (uint8_t *) & phba->fc_nodename.IEEE[0];
		for (i = 0; i < 12; i++) {
			status = *outptr++;
			j = ((status & 0xf0) >> 4);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
			i++;
			j = (status & 0xf);
			if (j <= 9)
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x30 + (uint8_t) j);
			else
				phba->SerialNumber[i] =
				    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		}
	}

	/* This should turn on DELAYED ABTS for ELS timeouts */
	lpfc_set_slim(phba, pmb, 0x052198, 0x1);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		return (EIO);
	}


	lpfc_read_config(phba, pmb);
	if (lpfc_sli_issue_mbox(phba, pmb, MBX_POLL) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> READ_CONFIG, mbxStatus
		   <status> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0453,
			       lpfc_mes0453,
			       lpfc_msgBlk0453.msgPreambleStr,
			       mb->mbxCommand, mb->mbxStatus);
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		return (EIO);
	}

	if (clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current > (mb->un.varRdConfig.max_xri+1)) {
		/* Reset the DFT_HBA_Q_DEPTH to the max xri  */
		clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current = mb->un.varRdConfig.max_xri + 1;
	}
	phba->lmt = mb->un.varRdConfig.lmt;

	/* HBA is not 4GB capable, or HBA is not 2GB capable, 
	don't let link speed ask for it */
	if ((((phba->lmt & LMT_4250_10bit) != LMT_4250_10bit) &&
		(clp[LPFC_CFG_LINK_SPEED].a_current > LINK_SPEED_2G)) || 
		(((phba->lmt & LMT_2125_10bit) != LMT_2125_10bit) &&
		 (clp[LPFC_CFG_LINK_SPEED].a_current > LINK_SPEED_1G))) {
			/* Reset link speed to auto. 1G/2GB HBA cfg'd for 4G */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1302,
					lpfc_mes1302,
					lpfc_msgBlk1302.msgPreambleStr,
					clp[LPFC_CFG_LINK_SPEED].a_current);
			clp[LPFC_CFG_LINK_SPEED].a_current = LINK_SPEED_AUTO;
	}
	
	if (phba->intr_inited != 1) {
		/* Add our interrupt routine to kernel's interrupt chain &
		   enable it */

		if (request_irq(phba->pcidev->irq,
				lpfc_intr_handler,
				SA_SHIRQ,
				LPFC_DRIVER_NAME,
				phba) != 0) {
			/* Enable interrupt handler failed */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0451,
					lpfc_mes0451,
					lpfc_msgBlk0451.msgPreambleStr);
			phba->hba_state = LPFC_HBA_ERROR;
			lpfc_mbox_free(phba, pmb);
			return (EIO);
		}
		phba->intr_inited = 1;
	}

	phba->hba_state = LPFC_LINK_DOWN;
	phba->fc_flag |= FC_LNK_DOWN;

	/* Only process IOCBs on ring 0 till hba_state is READY */
	if (psli->ring[psli->ip_ring].cmdringaddr)
		psli->ring[psli->ip_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->fcp_ring].cmdringaddr)
		psli->ring[psli->fcp_ring].flag |= LPFC_STOP_IOCB_EVENT;
	if (psli->ring[psli->next_ring].cmdringaddr)
		psli->ring[psli->next_ring].flag |= LPFC_STOP_IOCB_EVENT;

	/* Post receive buffers for desired rings */
	lpfc_post_rcv_buf(phba);

	/* Enable appropriate host interrupts */
	status = readl(phba->HCregaddr);
	status |= (uint32_t) (HC_MBINT_ENA | HC_ERINT_ENA | HC_LAINT_ENA);
	if (psli->sliinit.num_rings > 0)
		status |= HC_R0INT_ENA;
	if (psli->sliinit.num_rings > 1)
		status |= HC_R1INT_ENA;
	if (psli->sliinit.num_rings > 2)
		status |= HC_R2INT_ENA;
	if (psli->sliinit.num_rings > 3)
		status |= HC_R3INT_ENA;

	writel(status, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	/* Setup and issue mailbox INITIALIZE LINK command */
	lpfc_init_link(phba, pmb, clp[LPFC_CFG_TOPOLOGY].a_current,
		       clp[LPFC_CFG_LINK_SPEED].a_current);

	isr_cnt = psli->slistat.sliIntr;
	clk_cnt = jiffies;

	if (lpfc_sli_issue_mbox(phba, pmb, MBX_NOWAIT) != MBX_SUCCESS) {
		/* Adapter failed to init, mbxCmd <cmd> INIT_LINK, mbxStatus
		   <status> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0454,
				lpfc_mes0454,
				lpfc_msgBlk0454.msgPreambleStr,
				mb->mbxCommand, mb->mbxStatus);

		/* Clear all interrupt enable conditions */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		/* Clear all pending interrupts */
		writel(0xffffffff, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */

		free_irq(phba->pcidev->irq, phba);
		phba->intr_inited = 0;
		phba->hba_state = LPFC_HBA_ERROR;
		lpfc_mbox_free(phba, pmb);
		return (EIO);
	}
	/* MBOX buffer will be freed in mbox compl */

	/*
	 * Setup the ring 0 (els)  timeout handler
	 */
	timeout = phba->fc_ratov << 1;
	lpfc_start_timer(phba, timeout, &phba->els_tmofunc, 
		lpfc_els_timeout_handler, 
		(unsigned long)timeout, (unsigned long)0);

	phba->fc_prevDID = Mask_DID;
	flogi_sent = 0;
	i = 0;
	while ((phba->hba_state != LPFC_HBA_READY) ||
	       (phba->num_disc_nodes) || (phba->fc_prli_sent) ||
	       ((phba->fc_map_cnt == 0) && (i<2)) ||
	       (psli->sliinit.sli_flag & LPFC_SLI_MBOX_ACTIVE)) {
		/* Check every second for 45 retries. */
		i++;
		if (i > 45) {
			break;
		}
		if ((i >= 15) && (phba->hba_state <= LPFC_LINK_DOWN)) {
			/* The link is down.  Set linkdown timeout */

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
						(clp[LPFC_CFG_LINKDOWN_TMO]
						 .a_current);
					mod_timer(&phba->fc_linkdown, new_tmo);
				} else {
					if (clp[LPFC_CFG_HOLDIO].a_current
					    == 0) {
						lpfc_start_timer(phba,
						 clp[LPFC_CFG_LINKDOWN_TMO]
								 .a_current,
						 &phba->fc_linkdown,
						 lpfc_linkdown_timeout, 0, 0);
					}
				}
			}
			break;
		}

		/* 20 * 50ms is identically 1sec */
		for (j = 0; j < 20; j++) {
			lpfc_sleep_ms(phba, 50);
			/* On some systems hardware interrupts cannot interrupt
			 * the attach / detect routine. If this is the case,
			 * manually call the ISR every 50 ms to service any
			 * potential interrupt.
			 */
			LPFC_DRVR_LOCK(phba, iflag);
			if (isr_cnt == psli->slistat.sliIntr) {
				lpfc_sli_intr(phba);
				isr_cnt = psli->slistat.sliIntr;
			}
			LPFC_DRVR_UNLOCK(phba, iflag);
		}
		isr_cnt = psli->slistat.sliIntr;

		/* On some systems clock interrupts cannot interrupt the attach
		 * / detect routine. If this is the case, manually call the
		 * clock routine every sec to service any potential timeouts.
		 */
		if (clk_cnt == jiffies) {
			/* REMOVE: IF THIS HAPPENS, SYSTEM CLOCK IS NOT RUNNING.
			 * WE HAVE TO MANUALLY CALL OUR TIMEOUT ROUTINES.
			 */
			clk_cnt = jiffies;
		}
	}

	/* Since num_disc_nodes keys off of PLOGI, delay a bit to let
	 * any potential PRLIs to flush thru the SLI sub-system.
	 */
	lpfc_sleep_ms(phba, 50);
	LPFC_DRVR_LOCK(phba, iflag);
	if (isr_cnt == psli->slistat.sliIntr) {
		lpfc_sli_intr(phba);
	}

	LPFC_DRVR_UNLOCK(phba, iflag);

	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_hba_down_prep                                                */
/*    This routine will do LPFC uninitialization before the             */
/*    HBA is reset when bringing down the SLI Layer. This will be       */
/*    initialized as a SLI layer callback routine.                      */
/*    This routine returns 0 on success. Any other return value         */
/*    indicates an error.                                               */
/*                                                                      */
/************************************************************************/
int
lpfc_hba_down_prep(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;

	psli = &phba->sli;
	/* Disable interrupts */
	writel(0, phba->HCregaddr);
	readl(phba->HCregaddr); /* flush */

	lpfc_flush_disc_evtq(phba);
	lpfc_els_flush_rscn(phba);

	return (0);
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_eratt                                                 */
/*    This routine will handle processing a Host Attention              */
/*    Error Status event. This will be initialized                      */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_eratt(lpfcHBA_t * phba, uint32_t status)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t  * pring;
	LPFC_IOCBQ_t     * iocb, * next_iocb;
	IOCB_t          * icmd = NULL, * cmd = NULL;
	LPFC_SCSI_BUF_t  * lpfc_cmd;
	volatile uint32_t status1, status2;
	struct list_head *curr, *next;
	void *from_slim;

	psli = &phba->sli;
	from_slim = ((uint8_t *)phba->MBslimaddr + 0xa8);
	status1 = readl( from_slim);
	from_slim =  ((uint8_t *)phba->MBslimaddr + 0xac);
	status2 = readl( from_slim);

	if (status & HS_FFER6) {
		/* Re-establishing Link */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1301,
			       lpfc_mes1301,
			       lpfc_msgBlk1301.msgPreambleStr,
			       status, status1, status2);
		phba->fc_flag |= FC_ESTABLISH_LINK;

		/* 
		* Firmware stops when it triggled erratt with HS_FFER6.
		* That could cause the I/Os dropped by the firmware.
		* Error iocb (I/O) on txcmplq and let the SCSI layer 
		* retry it after re-establishing link. 
		*/
		pring = &psli->ring[psli->fcp_ring];

		list_for_each_safe(curr, next, &pring->txcmplq) {
			next_iocb = list_entry(curr, LPFC_IOCBQ_t, list);
			iocb = next_iocb;
			cmd = &iocb->iocb;

			/* Must be a FCP command */
			if((cmd->ulpCommand != CMD_FCP_ICMND64_CR) &&
				(cmd->ulpCommand != CMD_FCP_IWRITE64_CR) &&
				(cmd->ulpCommand != CMD_FCP_IREAD64_CR)) {
				continue;
				}

			/* context1 MUST be a LPFC_SCSI_BUF_t */
			lpfc_cmd = (LPFC_SCSI_BUF_t *)(iocb->context1);
			if(lpfc_cmd == 0) {
				continue;
			}

			list_del(&iocb->list);
			pring->txcmplq_cnt--;

			if(iocb->iocb_cmpl) {
				icmd = &iocb->iocb;
				icmd->ulpStatus = IOSTAT_LOCAL_REJECT;
				icmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
				(iocb->iocb_cmpl)(phba, iocb, iocb);
			} else {
				lpfc_iocb_free(phba, iocb);
			}
		}

		lpfc_offline(phba);
		if (lpfc_online(phba) == 0) {	/* Initialize the HBA */
			if (phba->fc_estabtmo.function) {
				lpfc_stop_timer((struct clk_data *)
						phba->fc_estabtmo.data);
			}
			lpfc_start_timer(phba, 60, &phba->fc_estabtmo,
					 lpfc_establish_link_tmo, 0, 0);
			return;
		}
	}
	/* Adapter Hardware Error */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0457,
			lpfc_mes0457,
			lpfc_msgBlk0457.msgPreambleStr,
			status, status1, status2);

	lpfc_offline(phba);
	lpfc_unblock_requests(phba);
	return;
}

/************************************************************************/
/*                                                                      */
/*    lpfc_handle_latt                                                  */
/*    This routine will handle processing a Host Attention              */
/*    Link Status event. This will be initialized                       */
/*    as a SLI layer callback routine.                                  */
/*                                                                      */
/************************************************************************/
void
lpfc_handle_latt(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_MBOXQ_t *pmb;
	volatile uint32_t control;

	/* called from host_interrupt, to process LATT */
	psli = &phba->sli;

	psli->slistat.linkEvent++;

	/* Get a buffer which will be used for mailbox commands */
	if ((pmb = (LPFC_MBOXQ_t *) lpfc_mbox_alloc(phba, MEM_PRI))) {
		if (lpfc_read_la(phba, pmb) == 0) {
			pmb->mbox_cmpl = lpfc_mbx_cmpl_read_la;
			if (lpfc_sli_issue_mbox
			    (phba, pmb, (MBX_NOWAIT | MBX_STOP_IOCB))
			    != MBX_NOT_FINISHED) {
				/* Turn off Link Attention interrupts until
				   CLEAR_LA done */
				psli->sliinit.sli_flag &= ~LPFC_PROCESS_LA;
				control = readl(phba->HCregaddr);
				control &= ~HC_LAINT_ENA;
				writel(control, phba->HCregaddr);
				readl(phba->HCregaddr); /* flush */

				/* Clear Link Attention in HA REG */
				writel(HA_LATT, phba->HAregaddr);
				readl(phba->HAregaddr); /* flush */
				return;
			} else {
				lpfc_mbox_free(phba, pmb);
			}
		} else {
			lpfc_mbox_free(phba, pmb);
		}
	}

	/* Clear Link Attention in HA REG */
	writel(HA_LATT, phba->HAregaddr);
	readl(phba->HAregaddr); /* flush */
	lpfc_linkdown(phba);
	phba->hba_state = LPFC_HBA_ERROR;
	return;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_parse_vpd                                                     */
/*   This routine will parse the VPD data                               */
/*                                                                      */
/************************************************************************/
int
lpfc_parse_vpd(lpfcHBA_t * phba, uint8_t * vpd)
{
	uint8_t lenlo, lenhi;
	uint32_t Length;
	int i, j;
	int finished = 0;
	int index = 0;

	if (!vpd)
		return (0);

	/* Vital Product */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0455,
			lpfc_mes0455,
			lpfc_msgBlk0455.msgPreambleStr,
			(uint32_t) vpd[0], (uint32_t) vpd[1], (uint32_t) vpd[2],
			(uint32_t) vpd[3]);
	do {
		switch (vpd[index]) {
		case 0x82:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			i = ((((unsigned short)lenhi) << 8) + lenlo);
			index += i;
			break;
		case 0x90:
			index += 1;
			lenlo = vpd[index];
			index += 1;
			lenhi = vpd[index];
			index += 1;
			Length = ((((unsigned short)lenhi) << 8) + lenlo);

			while (Length > 0) {
			/* Look for Serial Number */
			if ((vpd[index] == 'S') && (vpd[index+1] == 'N')) {
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->SerialNumber[j++] = vpd[index++];
					if(j == 31)
						break;
				}
				phba->SerialNumber[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '1')) {
				phba->vpd_flag |= VPD_MODEL_DESC;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ModelDesc[j++] = vpd[index++];
					if(j == 255)
						break;
				}
				phba->ModelDesc[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '2')) {
				phba->vpd_flag |= VPD_MODEL_NAME;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ModelName[j++] = vpd[index++];
					if(j == 79)
						break;
				}
				phba->ModelName[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '3')) {
				phba->vpd_flag |= VPD_PROGRAM_TYPE;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
					phba->ProgramType[j++] = vpd[index++];
					if(j == 255)
						break;
				}
				phba->ProgramType[j] = 0;
				continue;
			}
			else if ((vpd[index] == 'V') && (vpd[index+1] == '4')) {
				phba->vpd_flag |= VPD_PORT;
				index += 2;
				i = vpd[index];
				index += 1;
				j = 0;
				Length -= (3+i);
				while(i--) {
				phba->Port[j++] = vpd[index++];
				if(j == 19)
					break;
				}
				phba->Port[j] = 0;
				continue;
			}
			else {
				index += 2;
				i = vpd[index];
				index += 1;
				index += i;
				Length -= (3 + i);
			}
			}
			finished = 0;
			break;
		case 0x78:
			finished = 1;
			break;
		default:
			index++;
			break;
		}
	} while (!finished && (index < 108));

	return (1);
}

/**************************************************/
/*   lpfc_post_buffer                             */
/*                                                */
/*   This routine will post count buffers to the  */
/*   ring with the QUE_RING_BUF_CN command. This  */
/*   allows 3 buffers / command to be posted.     */
/*   Returns the number of buffers NOT posted.    */
/**************************************************/
int
lpfc_post_buffer(lpfcHBA_t * phba, LPFC_SLI_RING_t * pring, int cnt, int type)
{
	IOCB_t *icmd;
	LPFC_IOCBQ_t *iocb;
	DMABUF_t *mp1, *mp2;

	cnt += pring->missbufcnt;

	/* While there are buffers to post */
	while (cnt > 0) {
		/* Allocate buffer for  command iocb */
		if ((iocb = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
			pring->missbufcnt = cnt;
			return (cnt);
		}
		memset(iocb, 0, sizeof (LPFC_IOCBQ_t));
		icmd = &iocb->iocb;

		/* 2 buffers can be posted per command */
		/* Allocate buffer to post */
		if (((mp1 = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
		    ((mp1->virt = lpfc_mbuf_alloc(phba,
						  MEM_PRI,
						  &(mp1->phys))) == 0)) {
			if (mp1)
				kfree(mp1);

			lpfc_iocb_free(phba, iocb);
			pring->missbufcnt = cnt;
			return (cnt);
		}
	
		INIT_LIST_HEAD(&mp1->list);
		/* Allocate buffer to post */
		if (cnt > 1) {
			if (((mp2 =
			      kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0)
			    ||
			    ((mp2->virt =
			      lpfc_mbuf_alloc(phba, MEM_PRI,
					      &(mp2->phys))) == 0)) {
				if (mp2)
					kfree(mp2);
				lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
				kfree(mp1);
				lpfc_iocb_free(phba, iocb);
				pring->missbufcnt = cnt;
				return (cnt);
			}

			INIT_LIST_HEAD(&mp2->list);
		} else {
			mp2 = 0;
		}

		icmd->un.cont64[0].addrHigh = putPaddrHigh(mp1->phys);
		icmd->un.cont64[0].addrLow = putPaddrLow(mp1->phys);
		icmd->un.cont64[0].tus.f.bdeSize = FCELSSIZE;
		icmd->ulpBdeCount = 1;
		cnt--;
		if (mp2) {
			icmd->un.cont64[1].addrHigh = putPaddrHigh(mp2->phys);
			icmd->un.cont64[1].addrLow = putPaddrLow(mp2->phys);
			icmd->un.cont64[1].tus.f.bdeSize = FCELSSIZE;
			cnt--;
			icmd->ulpBdeCount = 2;
		}

		icmd->ulpCommand = CMD_QUE_RING_BUF64_CN;
		icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
		icmd->ulpLe = 1;

		if (lpfc_sli_issue_iocb(phba, pring, iocb, SLI_IOCB_USE_TXQ) ==
		    IOCB_ERROR) {
			lpfc_mbuf_free(phba, mp1->virt, mp1->phys);
			kfree(mp1);
			if (mp2) {
				lpfc_mbuf_free(phba, mp2->virt, mp2->phys);
				kfree(mp2);
			}
			lpfc_iocb_free(phba, iocb);
			pring->missbufcnt = cnt;
			return (cnt);
		}
		lpfc_sli_ringpostbuf_put(phba, pring, mp1);
		if (mp2) {
			lpfc_sli_ringpostbuf_put(phba, pring, mp2);
		}
	}
	pring->missbufcnt = 0;
	return (0);
}

/************************************************************************/
/*                                                                      */
/*   lpfc_post_rcv_buf                                                  */
/*   This routine post initial rcv buffers to the configured rings      */
/*                                                                      */
/************************************************************************/
int
lpfc_post_rcv_buf(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	lpfcCfgParam_t *clp;

	psli = &phba->sli;
	clp = &phba->config[0];

	/* Ring 0, ELS / CT buffers */
	lpfc_post_buffer(phba, &psli->ring[LPFC_ELS_RING], LPFC_BUF_RING0, 1);


	/* Ring 2 - FCP no buffers needed */

	return (0);
}

#define S(N,V) (((V)<<(N))|((V)>>(32-(N))))

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_init                                                      */
/*                                                                      */
/************************************************************************/
void
lpfc_sha_init(uint32_t * HashResultPointer)
{
	HashResultPointer[0] = 0x67452301;
	HashResultPointer[1] = 0xEFCDAB89;
	HashResultPointer[2] = 0x98BADCFE;
	HashResultPointer[3] = 0x10325476;
	HashResultPointer[4] = 0xC3D2E1F0;
}

/************************************************************************/
/*                                                                      */
/*   lpfc_sha_iterate                                                   */
/*                                                                      */
/************************************************************************/
void
lpfc_sha_iterate(uint32_t * HashResultPointer, uint32_t * HashWorkingPointer)
{
	int t;
	uint32_t TEMP;
	uint32_t A, B, C, D, E;
	t = 16;
	do {
		HashWorkingPointer[t] =
		    S(1,
		      HashWorkingPointer[t - 3] ^ HashWorkingPointer[t -
								     8] ^
		      HashWorkingPointer[t - 14] ^ HashWorkingPointer[t - 16]);
	} while (++t <= 79);
	t = 0;
	A = HashResultPointer[0];
	B = HashResultPointer[1];
	C = HashResultPointer[2];
	D = HashResultPointer[3];
	E = HashResultPointer[4];

	do {
		if (t < 20) {
			TEMP = ((B & C) | ((~B) & D)) + 0x5A827999;
		} else if (t < 40) {
			TEMP = (B ^ C ^ D) + 0x6ED9EBA1;
		} else if (t < 60) {
			TEMP = ((B & C) | (B & D) | (C & D)) + 0x8F1BBCDC;
		} else {
			TEMP = (B ^ C ^ D) + 0xCA62C1D6;
		}
		TEMP += S(5, A) + E + HashWorkingPointer[t];
		E = D;
		D = C;
		C = S(30, B);
		B = A;
		A = TEMP;
	} while (++t <= 79);

	HashResultPointer[0] += A;
	HashResultPointer[1] += B;
	HashResultPointer[2] += C;
	HashResultPointer[3] += D;
	HashResultPointer[4] += E;

}

/************************************************************************/
/*                                                                      */
/*   lpfc_challenge_key                                                 */
/*                                                                      */
/************************************************************************/
void
lpfc_challenge_key(uint32_t * RandomChallenge, uint32_t * HashWorking)
{
	*HashWorking = (*RandomChallenge ^ *HashWorking);
}

/************************************************************************/
/*                                                                      */
/*   lpfc_hba_init                                                      */
/*                                                                      */
/************************************************************************/
void
lpfc_hba_init(lpfcHBA_t * phba)
{
	int t;
	uint32_t HashWorking[80];
	uint32_t *pwwnn;

	pwwnn = phba->wwnn;
	memset(HashWorking, 0, sizeof (HashWorking));
	HashWorking[0] = HashWorking[78] = *pwwnn++;
	HashWorking[1] = HashWorking[79] = *pwwnn;
	for (t = 0; t < 7; t++) {
		lpfc_challenge_key(phba->RandomData + t, HashWorking + t);
	}
	lpfc_sha_init(phba->hbainitEx);
	lpfc_sha_iterate(phba->hbainitEx, HashWorking);
}

void
lpfc_cleanup(lpfcHBA_t * phba, uint32_t save_bind)
{
	LPFC_NODELIST_t *ndlp;
	LPFC_BINDLIST_t *bdlp;
	struct list_head *pos, *next;

	/* clean up phba - lpfc specific */
	lpfc_can_disctmo(phba);
	list_for_each_safe(pos, next, &phba->fc_nlpunmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_safe(pos, next, &phba->fc_nlpmap_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_safe(pos, next, &phba->fc_plogi_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		lpfc_nlp_remove(phba, ndlp);
	}

	list_for_each_safe(pos, next, &phba->fc_adisc_list) {
		ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		lpfc_nlp_remove(phba, ndlp);
	}

	if (save_bind == 0) {
		list_for_each_safe(pos, next, &phba->fc_nlpbind_list) {
			bdlp = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);
			list_del(pos);
			lpfc_bind_free(phba, bdlp);
		}

		phba->fc_bind_cnt = 0;
	}

	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	
	phba->fc_map_cnt   = 0;
	phba->fc_unmap_cnt = 0;
	phba->fc_plogi_cnt = 0;
	phba->fc_adisc_cnt = 0;
	return;
}

void
lpfc_establish_link_tmo(unsigned long ptr)
{
	lpfcHBA_t     *phba;
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

	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	/* Re-establishing Link, timer expired */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1300,
		       lpfc_mes1300,
		       lpfc_msgBlk1300.msgPreambleStr,
		       phba->fc_flag, phba->hba_state);
	phba->fc_flag &= ~FC_ESTABLISH_LINK;
out:
	LPFC_DRVR_UNLOCK(phba, iflag);
}

int
lpfc_online(lpfcHBA_t * phba)
{
	uint32_t timeout;

	if (phba) {
		if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
			return (0);
		}
		phba->reset_pending = 1;
		phba->no_timer = 0;

		/* Bring Adapter online */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0458,
			       lpfc_mes0458,
			       lpfc_msgBlk0458.msgPreambleStr);

		if (!lpfc_sli_queue_setup(phba)) {
			phba->reset_pending = 0;
			return (1);
		}
		if (lpfc_sli_hba_setup(phba)) {	/* Initialize the HBA */
			phba->reset_pending = 0;
			return (1);
		}

   		phba->fc_flag &= ~FC_OFFLINE_MODE;
		
		timeout = (phba->fc_ratov << 1) > 5 ? (phba->fc_ratov << 1) : 5;
		lpfc_start_timer(phba, timeout, &phba->scsi_tmofunc, 
			lpfc_scsi_timeout_handler, (unsigned long)timeout, 
			(unsigned long)0);

		
		phba->reset_pending = 0;
		lpfc_unblock_requests(phba);
	}
	return (0);
}

int
lpfc_offline(lpfcHBA_t * phba)
{
	LPFC_SLI_RING_t *pring;
	LPFC_SLI_t *psli;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	unsigned long iflag;
	int i;
	struct clk_data *clkData;
	struct list_head *curr, *next;
	struct lpfc_dmabuf *cur_buf;
	struct timer_list *ptimer;
	LPFCSCSITARGET_t *targetp;

	if (phba) {
		if (phba->fc_flag & FC_OFFLINE_MODE) {
			return (0);
		}

		phba->reset_pending = 1;
		psli = &phba->sli;
		pring = &psli->ring[psli->fcp_ring];

		lpfc_block_requests(phba);

		lpfc_linkdown(phba);

		phba->no_timer = 1;
		list_for_each_safe(curr, next, &phba->timerList) {
			clkData = list_entry(curr, struct clk_data, listLink);
			if (clkData) {
				ptimer = clkData->timeObj;
				if (timer_pending(ptimer)) {
					lpfc_stop_timer(clkData);	
				}
			}
		}

        	for (i = 0; i < MAX_FCP_TARGET; i++) {
                	targetp = phba->device_queue_hash[i];
                	if (targetp) {
                        	targetp->targetFlags &= ~FC_NPR_ACTIVE;
                        	targetp->tmofunc.function = 0;
                                                                                
                        	if(targetp->pcontext)
                                	lpfc_disc_state_machine(phba, 
					targetp->pcontext, 
					0, NLP_EVT_DEVICE_RM);
                                                                                
                        	lpfc_sched_flush_target(phba, 
					targetp, IOSTAT_LOCAL_REJECT,
                                	IOERR_SLI_ABORTED);
                	}
        	}

		/* If lpfc_offline is called from the interrupt, there is a
           	   FW trap . Do not expect iocb completions here */
		if (!in_interrupt()) {
			i = 0;
			while (pring->txcmplq_cnt) {
				LPFC_DRVR_UNLOCK(phba, iflag);
				mdelay(10);
				LPFC_DRVR_LOCK(phba, iflag);
				if (i++ > 3000)	/* 30 secs */
					break;
			}
		}

		LPFC_DRVR_UNLOCK(phba, iflag);
		while (!list_empty(&phba->timerList)) {
		}
		LPFC_DRVR_LOCK(phba, iflag);

		/* Bring Adapter offline */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0460,
			       lpfc_mes0460,
			       lpfc_msgBlk0460.msgPreambleStr);

		lpfc_sli_hba_down(phba);	/* Bring down the SLI Layer */

		phba->fc_flag |= FC_OFFLINE_MODE;
 
		lpfc_cleanup(phba, 1);	/* Save bindings */

		while(!list_empty(&phba->delay_list)) {
			lpfc_cmd = list_entry(phba->delay_list.next, LPFC_SCSI_BUF_t, listentry);
			list_del(&lpfc_cmd->listentry);
			lpfc_iodone(phba, lpfc_cmd);
		}

		while(!list_empty(&phba->free_buf_list)) {
			cur_buf = list_entry(phba->free_buf_list.next, DMABUF_t, list);
			list_del(&cur_buf->list);
			lpfc_mbuf_free(phba, cur_buf->virt, cur_buf->phys);
			kfree((void *)cur_buf);
		}

		phba->reset_pending = 0;
	}
	return (0);
}

/******************************************************************************
* Function name : lpfc_scsi_free
*
* Description   : Called from fc_detach to free scsi tgt / lun resources
* 
******************************************************************************/
int
lpfc_scsi_free(lpfcHBA_t * phba)
{
	LPFCSCSITARGET_t *targetp;
	LPFCSCSILUN_t *lunp;
	int i;
	struct list_head *curr, *next;

	for (i = 0; i < MAX_FCP_TARGET; i++) {
		targetp = phba->device_queue_hash[i];
		if (targetp) {

			list_for_each_safe(curr, next, &targetp->lunlist) {
				lunp = list_entry(curr, LPFCSCSILUN_t , list);
				list_del(&lunp->list);
				kfree(lunp);
			}

			if (targetp->RptLunData) {
				lpfc_page_free(phba,
					       targetp->RptLunData->virt,
					       targetp->RptLunData->phys);
				kfree(targetp->RptLunData);
			}

			kfree(targetp);
			phba->device_queue_hash[i] = 0;
		}
	}
	return (0);
}


/******************************************************************************
* Function name : lpfc_parse_binding_entry
*
* Description   : Parse binding entry for WWNN & WWPN
*
* ASCII Input string example: 2000123456789abc:lpfc1t0
* 
* Return        :  0              = Success
*                  Greater than 0 = Binding entry syntax error. SEE defs
*                                   LPFC_SYNTAX_ERR_XXXXXX.
******************************************************************************/
int
lpfc_parse_binding_entry(lpfcHBA_t * phba,
			 uint8_t * inbuf,
			 uint8_t * outbuf,
			 int in_size,
			 int out_size,
			 int bind_type,
			 unsigned int *sum, int entry, int *lpfc_num)
{
	int c1, sumtmp;
	uint8_t hexval;
	char val[3] = {0};

	char ds_lpfc[] = LPFC_DRIVER_NAME;

	*lpfc_num = -1;
	if (bind_type == LPFC_BIND_DID) {
		outbuf++;
	}

	/* Sanity check the input parameters */
	if (in_size < 1) {
		/* Convert ASC to hex. Input byte cnt < 1. */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1210,
			       lpfc_mes1210,
			       lpfc_msgBlk1210.msgPreambleStr);
		return (LPFC_SYNTAX_ERR_ASC_CONVERT);
	}
	if ((out_size * 2) < in_size) {
		/* Convert ASC to hex. Output buffer to small. */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1212,
			       lpfc_mes1212,
			       lpfc_msgBlk1212.msgPreambleStr);
		return (LPFC_SYNTAX_ERR_ASC_CONVERT);
	}

	/* Parse 16 digit ASC hex address */
	while (in_size > 1) {
		if (sscanf((char *)inbuf, "%1c", val) != 1) {
			/* Convert ASC to hex. Input char seq not ASC hex. */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1213,
				       lpfc_mes1213,
				       lpfc_msgBlk1213.msgPreambleStr);
			return (LPFC_SYNTAX_ERR_ASC_CONVERT);
		}
		hexval = (uint8_t)simple_strtoul(val, NULL, 16);
		*(char *)outbuf++ = hexval;
		inbuf += 2;
		in_size -=2;
	}

	/* Parse colon */
	if (*inbuf++ != ':')
		return (LPFC_SYNTAX_ERR_EXP_COLON);

	/* Parse lpfc */
	if (strncmp(inbuf, ds_lpfc, strlen(ds_lpfc)))
		return (LPFC_SYNTAX_ERR_EXP_LPFC);
	inbuf += strlen(ds_lpfc);

	/* Parse lpfc number */
	/* Get 1st lpfc digit */
	c1 = *inbuf++;
	if (!isdigit(c1))
		goto err_lpfc_num;
	sumtmp = c1 - 0x30;

	/* Get 2nd lpfc digit */
	c1 = *inbuf;
	if (!isdigit(c1))
		goto convert_instance;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;

	/* Get 3rd lpfc digit */
	c1 = *inbuf;
	if (!isdigit(c1))
		goto convert_instance;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;
	if (sumtmp < 0)
		goto err_lpfc_num;

	goto convert_instance;

      err_lpfc_num:

	return (LPFC_SYNTAX_ERR_INV_LPFC_NUM);

	/* Convert from ddi instance number to adapter number */
      convert_instance:

	/* Check to see if this is the right board */
	if(phba->brd_no != sumtmp) {
		/* Skip this entry */
		return(LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD);
	}

	/* Parse 't' */
	if (*inbuf++ != 't')
		return (LPFC_SYNTAX_ERR_EXP_T);

	/* Parse target number */
	/* Get 1st target digit */
	c1 = *inbuf++;
	if (!isdigit(c1))
		goto err_target_num;
	sumtmp = c1 - 0x30;

	/* Get 2nd target digit */
	c1 = *inbuf;
	if (!isdigit(c1))
		goto check_for_term;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;

	/* Get 3nd target digit */
	c1 = *inbuf;
	if (!isdigit(c1))
		goto check_for_term;
	inbuf++;
	sumtmp = (sumtmp * 10) + c1 - 0x30;
	goto check_for_term;

      err_target_num:
	return (LPFC_SYNTAX_ERR_INV_TARGET_NUM);

      check_for_term:

	if (*inbuf != 0)
		return (LPFC_SYNTAX_ERR_EXP_NULL_TERM);

	*sum = sumtmp;
	return (LPFC_SYNTAX_OK);	/* Success */
}
