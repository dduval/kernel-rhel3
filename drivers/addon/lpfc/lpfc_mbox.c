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
 * $Id: lpfc_mbox.c 328 2005-05-03 15:20:43Z sf_support $
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
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"

/**********************************************/

/*                mailbox command             */
/**********************************************/
void
lpfc_dump_mem(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb, uint16_t offset)
{
	MAILBOX_t *mb;
	void *ctx;

	mb = &pmb->mb;
	ctx = pmb->context2;

	/* Setup to dump VPD region */
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_DUMP_MEMORY;
	mb->un.varDmp.cv = 1;
	mb->un.varDmp.type = DMP_NV_PARAMS;
	mb->un.varDmp.entry_index = offset;
	mb->un.varDmp.region_id = DMP_REGION_VPD;
	mb->un.varDmp.word_cnt = (DMP_RSP_SIZE / sizeof (uint32_t));

	mb->un.varDmp.co = 0;
	mb->un.varDmp.resp_offset = 0;
	pmb->context2 = ctx;

	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ NVPARAM        */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_nv(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->mbxCommand = MBX_READ_NV;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_la  Issue a READ LA             */
/*                mailbox command             */
/**********************************************/
int
lpfc_read_la(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	DMABUF_t *mp;
	LPFC_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* Get a buffer to hold the loop map */
	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);
		mb->mbxCommand = MBX_READ_LA64;
		/* READ_LA: no buffers */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0300, lpfc_mes0300,
			       lpfc_msgBlk0300.msgPreambleStr);
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_LA64;
	mb->un.varReadLA.un.lilpBde64.tus.f.bdeSize = 128;
	mb->un.varReadLA.un.lilpBde64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varReadLA.un.lilpBde64.addrLow = putPaddrLow(mp->phys);

	/* Save address for later completion and set the owner to host so that
	 * the FW knows this mailbox is available for processing. 
	 */
	pmb->context1 = (uint8_t *) mp;
	mb->mbxOwner = OWN_HOST;
	return (0);
}

/**********************************************/
/*  lpfc_clear_la  Issue a CLEAR LA           */
/*                 mailbox command            */
/**********************************************/
void
lpfc_clear_la(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varClearLA.eventTag = phba->fc_eventTag;
	mb->mbxCommand = MBX_CLEAR_LA;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**************************************************/
/*  lpfc_config_link  Issue a CONFIG LINK         */
/*                    mailbox command             */
/**************************************************/
void
lpfc_config_link(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* NEW_FEATURE
	 * SLI-2, Coalescing Response Feature. 
	 */
	if (clp[LPFC_CFG_CR_DELAY].a_current) {
		mb->un.varCfgLnk.cr = 1;
		mb->un.varCfgLnk.ci = 1;
		mb->un.varCfgLnk.cr_delay = clp[LPFC_CFG_CR_DELAY].a_current;
		mb->un.varCfgLnk.cr_count = clp[LPFC_CFG_CR_COUNT].a_current;
	}

	mb->un.varCfgLnk.myId = phba->fc_myDID;
	mb->un.varCfgLnk.edtov = phba->fc_edtov;
	mb->un.varCfgLnk.arbtov = phba->fc_arbtov;
	mb->un.varCfgLnk.ratov = phba->fc_ratov;
	mb->un.varCfgLnk.rttov = phba->fc_rttov;
	mb->un.varCfgLnk.altov = phba->fc_altov;
	mb->un.varCfgLnk.crtov = phba->fc_crtov;
	mb->un.varCfgLnk.citov = phba->fc_citov;

	if (clp[LPFC_CFG_ACK0].a_current)
		mb->un.varCfgLnk.ack0_enable = 1;

	mb->mbxCommand = MBX_CONFIG_LINK;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_init_link  Issue an INIT LINK        */
/*                  mailbox command           */
/**********************************************/
void
lpfc_init_link(lpfcHBA_t * phba,
	       LPFC_MBOXQ_t * pmb, uint32_t topology, uint32_t linkspeed)
{
	lpfc_vpd_t *vpd;
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	psli = &phba->sli;
	switch (topology) {
	case FLAGS_TOPOLOGY_MODE_LOOP_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_PT:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		break;
	case FLAGS_TOPOLOGY_MODE_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_LOOP;
		break;
	case FLAGS_TOPOLOGY_MODE_PT_LOOP:
		mb->un.varInitLnk.link_flags = FLAGS_TOPOLOGY_MODE_PT_PT;
		mb->un.varInitLnk.link_flags |= FLAGS_TOPOLOGY_FAILOVER;
		break;
	}

	vpd = &phba->vpd;
	if (vpd->rev.feaLevelHigh >= 0x02) {
		switch(linkspeed) {
		case LINK_SPEED_1G:
		case LINK_SPEED_2G:
		case LINK_SPEED_4G:
			mb->un.varInitLnk.link_flags |= FLAGS_LINK_SPEED;
			mb->un.varInitLnk.link_speed = linkspeed;
			break;
		case LINK_SPEED_AUTO:
		default:
			mb->un.varInitLnk.link_speed = LINK_SPEED_AUTO;
		}
	} else
		mb->un.varInitLnk.link_speed = LINK_SPEED_AUTO;

	mb->mbxCommand = (volatile uint8_t)MBX_INIT_LINK;
	mb->mbxOwner = OWN_HOST;
	mb->un.varInitLnk.fabric_AL_PA = phba->fc_pref_ALPA;
	return;
}

/**********************************************/
/*  lpfc_read_sparam  Issue a READ SPARAM     */
/*                    mailbox command         */
/**********************************************/
int
lpfc_read_sparam(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	DMABUF_t *mp;
	MAILBOX_t *mb;
	LPFC_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold the HBAs Service Parameters */

	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);
		mb->mbxCommand = MBX_READ_SPARM64;
		/* READ_SPARAM: no buffers */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0301,
			       lpfc_mes0301, lpfc_msgBlk0301.msgPreambleStr);
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	mb->mbxCommand = MBX_READ_SPARM64;
	mb->un.varRdSparm.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	mb->un.varRdSparm.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRdSparm.un.sp64.addrLow = putPaddrLow(mp->phys);

	/* save address for completion */
	pmb->context1 = mp;

	return (0);
}

/**********************************************/
/*  lpfc_read_rpi    Issue a READ RPI         */
/*                   mailbox command          */
/**********************************************/
int
lpfc_read_rpi(lpfcHBA_t * phba, uint32_t rpi, LPFC_MBOXQ_t * pmb, uint32_t flag)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRdRPI.reqRpi = (volatile uint16_t)rpi;

	mb->mbxCommand = MBX_READ_RPI64;
	mb->mbxOwner = OWN_HOST;

	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	return (0);
}

/********************************************/
/*  lpfc_unreg_did  Issue a UNREG_DID       */
/*                  mailbox command         */
/********************************************/
void
lpfc_unreg_did(lpfcHBA_t * phba, uint32_t did, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregDID.did = did;

	mb->mbxCommand = MBX_UNREG_D_ID;
	mb->mbxOwner = OWN_HOST;
	return;
}

/***********************************************/

/*                  command to write slim      */
/***********************************************/
void
lpfc_set_slim(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb, uint32_t addr,
	      uint32_t value)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	/* addr = 0x090597 is AUTO ABTS disable for ELS commands */
	/* addr = 0x052198 is DELAYED ABTS enable for ELS commands */

	/*
	 * Always turn on DELAYED ABTS for ELS timeouts 
	 */
	if ((addr == 0x052198) && (value == 0))
		value = 1;

	mb->un.varWords[0] = addr;
	mb->un.varWords[1] = value;

	mb->mbxCommand = MBX_SET_SLIM;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_config_farp  Issue a CONFIG FARP     */
/*                    mailbox command         */
/**********************************************/
void
lpfc_config_farp(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varCfgFarp.filterEnable = 1;
	mb->un.varCfgFarp.portName = 1;
	mb->un.varCfgFarp.nodeName = 1;

	memcpy((uint8_t *) & mb->un.varCfgFarp.portname,
	       (uint8_t *) & phba->fc_portname, sizeof (NAME_TYPE));
	memcpy((uint8_t *) & mb->un.varCfgFarp.nodename,
	       (uint8_t *) & phba->fc_portname, sizeof (NAME_TYPE));
	mb->mbxCommand = MBX_CONFIG_FARP;
	mb->mbxOwner = OWN_HOST;
	return;
}

/**********************************************/
/*  lpfc_read_nv  Issue a READ CONFIG         */
/*                mailbox command             */
/**********************************************/
void
lpfc_read_config(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->mbxCommand = MBX_READ_CONFIG;
	mb->mbxOwner = OWN_HOST;
	return;
}

/********************************************/
/*  lpfc_reg_login  Issue a REG_LOGIN       */
/*                  mailbox command         */
/********************************************/
int
lpfc_reg_login(lpfcHBA_t * phba,
	       uint32_t did, uint8_t * param, LPFC_MBOXQ_t * pmb, uint32_t flag)
{
	uint8_t *sparam;
	DMABUF_t *mp;
	MAILBOX_t *mb;
	LPFC_SLI_t *psli;

	psli = &phba->sli;
	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varRegLogin.rpi = 0;
	mb->un.varRegLogin.did = did;
	mb->un.varWords[30] = flag;	/* Set flag to issue action on cmpl */

	mb->mbxOwner = OWN_HOST;

	/* Get a buffer to hold NPorts Service Parameters */
	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);

		mb->mbxCommand = MBX_REG_LOGIN64;
		/* REG_LOGIN: no buffers */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0302,
			       lpfc_mes0302,
			       lpfc_msgBlk0302.msgPreambleStr,
			       (uint32_t) did, (uint32_t) flag);
		return (1);
	}
	INIT_LIST_HEAD(&mp->list);
	sparam = mp->virt;

	/* Copy param's into a new buffer */
	memcpy(sparam, param, sizeof (SERV_PARM));

	/* save address for completion */
	pmb->context1 = (uint8_t *) mp;

	mb->mbxCommand = MBX_REG_LOGIN64;
	mb->un.varRegLogin.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	mb->un.varRegLogin.un.sp64.addrHigh = putPaddrHigh(mp->phys);
	mb->un.varRegLogin.un.sp64.addrLow = putPaddrLow(mp->phys);

	return (0);
}

/**********************************************/
/*  lpfc_unreg_login  Issue a UNREG_LOGIN     */
/*                    mailbox command         */
/**********************************************/
void
lpfc_unreg_login(lpfcHBA_t * phba, uint32_t rpi, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varUnregLogin.rpi = (uint16_t) rpi;
	mb->un.varUnregLogin.rsvd1 = 0;

	mb->mbxCommand = MBX_UNREG_LOGIN;
	mb->mbxOwner = OWN_HOST;
	return;
}

/***********************************************/
/*  lpfc_config_pcb_setup  Issue a CONFIG_PORT */
/*                   mailbox command           */
/***********************************************/
uint32_t *
lpfc_config_pcb_setup(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	LPFC_RING_INIT_t *pringinit;
	PCB_t *pcbp;
	SLI2_SLIM_t *slim2p_virt;
	dma_addr_t pdma_addr;
	uint32_t offset;
	uint32_t iocbCnt;
	int i;

	psli = &phba->sli;

	slim2p_virt = ((SLI2_SLIM_t *) phba->slim2p.virt);
	pcbp = &slim2p_virt->un.slim.pcb;
	psli->MBhostaddr = (uint32_t *) (&slim2p_virt->un.slim.mbx);

	pcbp->maxRing = (psli->sliinit.num_rings - 1);

	iocbCnt = 0;
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pringinit = &psli->sliinit.ringinit[i];
		pring = &psli->ring[i];
		/* A ring MUST have both cmd and rsp entries defined to be
		   valid */
		if ((pringinit->numCiocb == 0) || (pringinit->numRiocb == 0)) {
			pcbp->rdsc[i].cmdEntries = 0;
			pcbp->rdsc[i].rspEntries = 0;
			pcbp->rdsc[i].cmdAddrHigh = 0;
			pcbp->rdsc[i].rspAddrHigh = 0;
			pcbp->rdsc[i].cmdAddrLow = 0;
			pcbp->rdsc[i].rspAddrLow = 0;
			pring->cmdringaddr = 0;
			pring->rspringaddr = 0;
			continue;
		}
		/* Command ring setup for ring */
		pring->cmdringaddr =
		    (void *)&slim2p_virt->un.slim.IOCBs[iocbCnt];
		pcbp->rdsc[i].cmdEntries = pringinit->numCiocb;

		offset =
		    (uint8_t *) & slim2p_virt->un.slim.IOCBs[iocbCnt] -
		    (uint8_t *) slim2p_virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].cmdAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].cmdAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pringinit->numCiocb;

		/* Response ring setup for ring */
		pring->rspringaddr =
		    (void *) &slim2p_virt->un.slim.IOCBs[iocbCnt];

		pcbp->rdsc[i].rspEntries = pringinit->numRiocb;
		offset =
		    (uint8_t *) & slim2p_virt->un.slim.IOCBs[iocbCnt] -
		    (uint8_t *) slim2p_virt;
		pdma_addr = phba->slim2p.phys + offset;
		pcbp->rdsc[i].rspAddrHigh = putPaddrHigh(pdma_addr);
		pcbp->rdsc[i].rspAddrLow = putPaddrLow(pdma_addr);
		iocbCnt += pringinit->numRiocb;
	}

	/* special handling for LC HBAs */
	if (lpfc_is_LC_HBA(phba->pcidev->device)) {
		lpfc_hba_init(phba);
		return (phba->hbainitEx);
	} else
		return (0);
}


void
lpfc_read_rev(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	MAILBOX_t *mb;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));
	mb->un.varRdRev.cv = 1;
	mb->mbxCommand = MBX_READ_REV;
	mb->mbxOwner = OWN_HOST;
	return;
}

void
lpfc_config_ring(lpfcHBA_t * phba, int ring, LPFC_MBOXQ_t * pmb)
{
	int i;
	MAILBOX_t *mb;
	LPFC_SLI_t *psli;
	LPFC_RING_INIT_t *pring;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	mb->un.varCfgRing.ring = ring;
	mb->un.varCfgRing.maxOrigXchg = 0;
	mb->un.varCfgRing.maxRespXchg = 0;
	mb->un.varCfgRing.recvNotify = 1;

	psli = &phba->sli;
	pring = &psli->sliinit.ringinit[ring];
	mb->un.varCfgRing.numMask = pring->num_mask;
	mb->mbxCommand = MBX_CONFIG_RING;
	mb->mbxOwner = OWN_HOST;

	/* Is this ring configured for a specific profile */
	if (pring->prt[0].profile) {
		mb->un.varCfgRing.profile = pring->prt[0].profile;
		return;
	}

	/* Otherwise we setup specific rctl / type masks for this ring */
	for (i = 0; i < pring->num_mask; i++) {
		mb->un.varCfgRing.rrRegs[i].rval = pring->prt[i].rctl;
		if (mb->un.varCfgRing.rrRegs[i].rval != FC_ELS_REQ)
			mb->un.varCfgRing.rrRegs[i].rmask = 0xff;
		else
			mb->un.varCfgRing.rrRegs[i].rmask = 0xfe;
		mb->un.varCfgRing.rrRegs[i].tval = pring->prt[i].type;
		mb->un.varCfgRing.rrRegs[i].tmask = 0xff;
	}

	return;
}

int
lpfc_config_port(lpfcHBA_t * phba, LPFC_MBOXQ_t * pmb)
{
	LPFC_SLI_t *psli;
	MAILBOX_t *mb;
	uint32_t *hbainit;
	dma_addr_t pdma_addr;
	uint32_t offset;
	HGP hgp;
	void *to_slim;
	uint32_t bar0_config_word, bar1_config_word;

	mb = &pmb->mb;
	memset(pmb, 0, sizeof (LPFC_MBOXQ_t));

	psli = &phba->sli;
	mb->mbxCommand = MBX_CONFIG_PORT;
	mb->mbxOwner = OWN_HOST;

	mb->un.varCfgPort.pcbLen = sizeof (PCB_t);
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb) -
	    (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	mb->un.varCfgPort.pcbLow = putPaddrLow(pdma_addr);
	mb->un.varCfgPort.pcbHigh = putPaddrHigh(pdma_addr);

	/* Now setup pcb */
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.type =
	    TYPE_NATIVE_SLI2;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.feature =
	    FEATURE_INITIAL_SLI2;

	/* Setup Mailbox pointers */
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mailBoxSize =
	    sizeof (MAILBOX_t);
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.mbx) -
	    (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mbAddrHigh =
	    putPaddrHigh(pdma_addr);
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.mbAddrLow =
	    putPaddrLow(pdma_addr);

	
	pci_read_config_dword(phba->pcidev, PCI_BASE_ADDRESS_0, &bar0_config_word);
	pci_read_config_dword(phba->pcidev, PCI_BASE_ADDRESS_1, &bar1_config_word);
	if (bar0_config_word & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		((SLI2_SLIM_t *) phba->slim2p.virt)->
			un.slim.pcb.hgpAddrHigh = bar1_config_word;
	} else {
		((SLI2_SLIM_t *) phba->slim2p.virt)->
			un.slim.pcb.hgpAddrHigh = 0;
	}
	bar0_config_word &= PCI_BASE_ADDRESS_MEM_MASK;

	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.hgpAddrLow = 
		(uint32_t) (bar0_config_word
			    + (SLIMOFF * sizeof (uint32_t)));
	memset(&hgp, 0, sizeof (HGP));

	/* write HGP data to SLIM */
	to_slim = (uint8_t *) phba->MBslimaddr
		+ (SLIMOFF * sizeof (uint32_t));
	lpfc_memcpy_to_slim( to_slim,  &hgp, sizeof (HGP));
	

	/* Setup Port Group ring counters */
	offset =
	    (uint8_t *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.mbx.us.
			 s2.port) - (uint8_t *) phba->slim2p.virt;
	pdma_addr = phba->slim2p.phys + offset;
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.pgpAddrHigh =
	    putPaddrHigh(pdma_addr);
	((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.pcb.pgpAddrLow =
	    putPaddrLow(pdma_addr);

	/* Use callback routine to setp rings in the pcb */
	hbainit = lpfc_config_pcb_setup(phba);
	if (hbainit != 0)
		memcpy(&mb->un.varCfgPort.hbainit, hbainit, 20);

	/* Swap PCB if needed */
	lpfc_sli_pcimem_bcopy((uint32_t
			      *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.
				  pcb),
			     (uint32_t
			      *) (&((SLI2_SLIM_t *) phba->slim2p.virt)->un.slim.
				  pcb), sizeof (PCB_t));

	/* Service Level Interface (SLI) 2 selected */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0405,
		       lpfc_mes0405, lpfc_msgBlk0405.msgPreambleStr);
	return (0);
}

void
lpfc_mbox_put(lpfcHBA_t * phba, LPFC_MBOXQ_t * mbq)
{				
	LPFC_SLI_t *psli;

	psli = &phba->sli;

	list_add_tail(&mbq->list, &psli->mboxq);

	psli->mboxq_cnt++;

	return;
}

LPFC_MBOXQ_t *
lpfc_mbox_get(lpfcHBA_t * phba)
{
	LPFC_MBOXQ_t *mbq;
	LPFC_SLI_t *psli;
	
	mbq = 0;

	psli = &phba->sli;

	if (!list_empty(&psli->mboxq)) {
		mbq = list_entry(psli->mboxq.next, LPFC_MBOXQ_t, list);
		list_del_init(&mbq->list);
		psli->mboxq_cnt--;
	}

	return (mbq);
}
