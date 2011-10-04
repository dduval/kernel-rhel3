/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2006 Emulex.  All rights reserved.           *
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
 * $Id: lpfc_util_ioctl.c 466 2006-01-05 19:16:29Z sf_support $
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
#include "lpfc_dfc.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"
#include "hbaapi.h"
#include "lpfc_util_ioctl.h"

#define LPFC_MAX_EVENT 4 /* Default events we can queue before dropping them */

extern lpfcDRVR_t lpfcDRVR;
struct dfc dfc;

uint32_t lpfc_diag_state = DDI_ONDI;

int
lpfc_initpci(struct dfc_info *di, lpfcHBA_t * phba)
{
	struct pci_dev *pdev;
	char lpfc_fwrevision[32];
	extern char* lpfc_release_version;

	pdev = phba->pcidev;
	/*
	   must have the pci struct
	 */
	if (!pdev)
		return (1);

	di->fc_ba.a_onmask = (ONDI_MBOX | ONDI_RMEM | ONDI_RPCI | ONDI_RCTLREG |
			      ONDI_IOINFO | ONDI_LNKINFO | ONDI_NODEINFO |
			      ONDI_CFGPARAM | ONDI_CT | ONDI_HBAAPI);
	di->fc_ba.a_offmask =
	    (OFFDI_MBOX | OFFDI_RMEM | OFFDI_WMEM | OFFDI_RPCI | OFFDI_WPCI |
	     OFFDI_RCTLREG | OFFDI_WCTLREG);

	if (lpfc_diag_state == DDI_ONDI)
		di->fc_ba.a_onmask |= ONDI_SLI2;
	else
		di->fc_ba.a_onmask |= ONDI_SLI1;

	/* set endianness of driver diagnotic interface */
#if __BIG_ENDIAN
	di->fc_ba.a_onmask |= ONDI_BIG_ENDIAN;
#else	/*  __LITTLE_ENDIAN */
	di->fc_ba.a_onmask |= ONDI_LTL_ENDIAN;
#endif

	di->fc_ba.a_pci =
	    ((((uint32_t) pdev->device) << 16) | (uint32_t) (pdev->vendor));
	di->fc_ba.a_pci = le32_to_cpu(di->fc_ba.a_pci);
	di->fc_ba.a_ddi = phba->brd_no;

	if (pdev->bus)
		di->fc_ba.a_busid = (uint32_t) (pdev->bus->number);
	else
		di->fc_ba.a_busid = 0;
	di->fc_ba.a_devid = (uint32_t) (pdev->devfn);

	memcpy(di->fc_ba.a_drvrid, lpfc_release_version, 8);
	lpfc_decode_firmware_rev(phba, lpfc_fwrevision, 1);
	memcpy(di->fc_ba.a_fwname, lpfc_fwrevision, 32);

	return (0);
}
/* Routine Declaration - Local */

int
lpfc_process_ioctl_util(lpfcHBA_t *phba, LPFCCMDINPUT_t *cip)
{
	int rc = -1;
	int do_cp = 0; 
	uint32_t outshift;
	uint32_t total_mem;
	struct dfc_info *di;
	void   *dataout;
	unsigned long iflag;

	extern struct dfc dfc;

	di = &dfc.dfc_info[cip->lpfc_brd];
	/* dfc_ioctl entry */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1606,	/* ptr to msg structure */
		lpfc_mes1606,			/* ptr to msg */
		lpfc_msgBlk1606.msgPreambleStr,	/* begin varargs */
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

	/* Diagnostic Interface Library Support - util */
	case LPFC_WRITE_PCI:
		rc = lpfc_ioctl_write_pci(phba, cip);
		break;

	case LPFC_READ_PCI:
		rc = lpfc_ioctl_read_pci(phba, cip, dataout);
		break;

	case LPFC_WRITE_MEM:
		rc = lpfc_ioctl_write_mem(phba, cip);
		break;

	case LPFC_READ_MEM:
		rc = lpfc_ioctl_read_mem(phba, cip, dataout);
		break;

	case LPFC_WRITE_CTLREG:
		rc = lpfc_ioctl_write_ctlreg(phba, cip);
		break;

	case LPFC_READ_CTLREG:
		rc = lpfc_ioctl_read_ctlreg(phba, cip, dataout);
		break;

	case LPFC_GET_DFC_REV:

		((DfcRevInfo *) dataout)->a_Major = DFC_MAJOR_REV;
		((DfcRevInfo *) dataout)->a_Minor = DFC_MINOR_REV;
		cip->lpfc_outsz = sizeof (DfcRevInfo);
		rc = 0;
		break;

	case LPFC_INITBRDS:
		di = &dfc.dfc_info[cip->lpfc_brd];
		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & di->fc_ba, (uint8_t *) cip->lpfc_dataout,
		     sizeof (brdinfo))) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			break;
		}
		LPFC_DRVR_LOCK(phba, iflag);
		if (lpfc_initpci(di, phba)) {
			rc = EIO;
			break;
		}
		if (phba->fc_flag & FC_OFFLINE_MODE)
			di->fc_ba.a_offmask |= OFFDI_OFFLINE;

		memcpy(dataout, (uint8_t *) & di->fc_ba,
		       sizeof (brdinfo));
		cip->lpfc_outsz = sizeof (brdinfo);
		rc = 0;
		break;

	case LPFC_SETDIAG:
		rc = lpfc_ioctl_setdiag(phba, cip, dataout);
		break;

	case LPFC_HBA_SEND_SCSI:
	case LPFC_HBA_SEND_FCP:
		rc = lpfc_ioctl_send_scsi_fcp(phba, cip);
		break;

	case LPFC_HBA_SEND_MGMT_RSP:
		rc = lpfc_ioctl_send_mgmt_rsp(phba, cip);
		break;

	case LPFC_HBA_SEND_MGMT_CMD:
	case LPFC_CT:
		rc = lpfc_ioctl_send_mgmt_cmd(phba, cip, dataout);
		break;

	case LPFC_MBOX:
		rc = lpfc_ioctl_mbox(phba, cip, dataout);
		break;

	case LPFC_LINKINFO:
		rc = lpfc_ioctl_linkinfo(phba, cip, dataout);
		break;

	case LPFC_IOINFO:
		rc = lpfc_ioctl_ioinfo(phba, cip, dataout);
		break;

	case LPFC_NODEINFO:
		rc = lpfc_ioctl_nodeinfo(phba, cip, dataout, total_mem);
		break;

	case LPFC_GETCFG:
		rc = lpfc_ioctl_getcfg(phba, cip, dataout);
		break;

	case LPFC_SETCFG:
		rc = lpfc_ioctl_setcfg(phba, cip);
		break;

	case LPFC_HBA_GET_EVENT:
		rc = lpfc_ioctl_hba_get_event(phba, cip, dataout, total_mem);
		break;

	case LPFC_HBA_SET_EVENT:
		rc = lpfc_ioctl_hba_set_event(phba, cip);
		break;

	case LPFC_ADD_BIND:
		rc = lpfc_ioctl_add_bind(phba, cip);
		break;

	case LPFC_DEL_BIND:
		rc = lpfc_ioctl_del_bind(phba, cip);
		break;

	case LPFC_LIST_BIND:
		rc = lpfc_ioctl_list_bind(phba, cip, dataout, &do_cp);
		break;

	case LPFC_GET_VPD:
		rc = lpfc_ioctl_get_vpd(phba, cip, dataout, &do_cp);
		break;
	}
	di->fc_refcnt--;

	/* dfc_ioctl exit */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1607,	/* ptr to msg structure */
		lpfc_mes1607,			/* ptr to msg */
		lpfc_msgBlk1607.msgPreambleStr,	/* begin varargs */
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
lpfc_ioctl_write_pci(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{
	uint32_t offset, cnt;
	int i, rc = 0;
	unsigned long iflag;
	uint32_t *buffer;

	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		rc = EPERM;
		return (rc);
	}

	if ((cnt + offset) > 256) {
		rc = ERANGE;
		return (rc);
	}

	buffer = kmalloc(4096, GFP_ATOMIC);
	if (!buffer) {
		return (ENOMEM);
	}

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user(buffer, cip->lpfc_dataout,
			   cnt)) {
		LPFC_DRVR_LOCK(phba, iflag);
		rc = EIO;
		kfree(buffer);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);
	
	for (i = offset; i < (offset + cnt); i += 4) {
		pci_write_config_dword(phba->pcidev, i, *buffer);
		buffer++;
	}

	kfree(buffer);
	return (rc);
}

int
lpfc_ioctl_read_pci(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t offset, cnt;
	uint32_t *destp;
	int rc = 0;
	int i;

	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;
	destp = (uint32_t *) dataout;

	if ((cnt + offset) > 256) {
		rc = ERANGE;
		return (rc);
	}

	for (i = offset; i < (offset + cnt); i += 4) {
		pci_read_config_dword(phba->pcidev, i, destp);
		destp++;
	}

	return (rc);
}

int
lpfc_ioctl_write_mem(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{
	uint32_t offset, cnt;
	LPFC_SLI_t *psli;
	int rc = 0;
	unsigned long iflag;
	uint8_t *buffer;

	psli = &phba->sli;
	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		if (offset != 256) {
			rc = EPERM;
			return (rc);
		}
		/* Allow writing of first 128 bytes after mailbox in online mode */
		if (cnt > 128) {
			rc = EPERM;
			return (rc);
		}
	}
	if (offset >= 4096) {
		rc = ERANGE;
		return (rc);
	}
	cnt = (ulong) cip->lpfc_arg2;
	if ((cnt + offset) > 4096) {
		rc = ERANGE;
		return (rc);
	}

	buffer =  kmalloc(4096, GFP_ATOMIC);
	if (!buffer)
		return(ENOMEM);

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) buffer, (uint8_t *) cip->lpfc_dataout,
			   (ulong) cnt)) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		kfree(buffer);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);

	if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
		/* copy into SLIM2 */
		lpfc_sli_pcimem_bcopy((uint32_t *) buffer,
				     ((uint32_t *) phba->slim2p.virt + offset),
				     cnt >> 2);
	} else {
		/* First copy command data */
		lpfc_memcpy_to_slim( phba->MBslimaddr, (void *)buffer, cnt);
	}
	kfree(buffer);
	return (rc);
}

int
lpfc_ioctl_read_mem(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t offset, cnt;
	LPFC_SLI_t *psli;
	int i, rc = 0;

	psli = &phba->sli;
	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;

	if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
		/* The SLIM2 size is stored in the next field */
		i = phba->slim_size;
	} else {
		i = 4096;
	}

	if (offset >= i) {
		rc = ERANGE;
		return (rc);
	}

	if ((cnt + offset) > i) {
		/* Adjust cnt instead of error ret */
		cnt = (i - offset);
	}

	if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
		/* copy results back to user */
		lpfc_sli_pcimem_bcopy((uint32_t *) psli->MBhostaddr,
				     (uint32_t *) dataout, cnt);
	} else {
		/* First copy command data from SLIM */
		lpfc_memcpy_from_slim( dataout,
			       phba->MBslimaddr,
			       sizeof (uint32_t) * (MAILBOX_CMD_WSIZE) );		
	}
	return (rc);
}

int
lpfc_ioctl_write_ctlreg(lpfcHBA_t * phba,
			LPFCCMDINPUT_t * cip)
{
	uint32_t offset, incr;
	LPFC_SLI_t *psli;
	int rc = 0;

	psli = &phba->sli;
	offset = (ulong) cip->lpfc_arg1;
	incr = (ulong) cip->lpfc_arg2;

	if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
		rc = EPERM;
		return (rc);
	}

	if (offset > 255) {
		rc = ERANGE;
		return (rc);
	}

	if (offset % 4) {
		rc = EINVAL;
		return (rc);
	}

	writel(incr, (phba->ctrl_regs_memmap_p) + offset);

	return (rc);
}

int
lpfc_ioctl_read_ctlreg(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t offset, incr;
	int rc = 0;

	offset = (ulong) cip->lpfc_arg1;

	if (offset > 255) {
		rc = ERANGE;
		return (rc);
	}

	if (offset % 4) {
		rc = EINVAL;
		return (rc);
	}

	incr = readl((phba->ctrl_regs_memmap_p) + offset);
	*((uint32_t *) dataout) = incr;

	return (rc);
}

int
lpfc_ioctl_setdiag(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t offset;
	int rc = 0;

	offset = (ulong) cip->lpfc_arg1;

	switch (offset) {
	case DDI_ONDI:
		rc = ENXIO;
		break;

	case DDI_OFFDI:
		rc = ENXIO;
		break;

	case DDI_SHOW:
		rc = ENXIO;
		break;

	case DDI_BRD_ONDI:
		if (phba->fc_flag & FC_OFFLINE_MODE) {
			lpfc_online(phba);
		}
		*((uint32_t *) (dataout)) = DDI_ONDI;
		break;

	case DDI_BRD_OFFDI:
		if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
			lpfc_offline(phba);
		}
		*((uint32_t *) (dataout)) = DDI_OFFDI;
		break;

	case DDI_BRD_SHOW:
		if (phba->fc_flag & FC_OFFLINE_MODE) {
			*((uint32_t *) (dataout)) = DDI_OFFDI;
		} else {
			*((uint32_t *) (dataout)) = DDI_ONDI;
		}
		break;

	default:
		rc = ERANGE;
		break;
	}

	return (rc);
}

int
lpfc_ioctl_send_scsi_fcp(lpfcHBA_t * phba,
			 LPFCCMDINPUT_t * cip)
{

	LPFC_SLI_t *psli = &phba->sli;
	lpfcCfgParam_t *clp;
	int reqbfrcnt;
	int snsbfrcnt;
	int j = 0;
	HBA_WWN wwpn;
	FCP_CMND *fcpcmd;
	FCP_RSP *fcprsp;
	ULP_BDE64 *bpl;
	LPFC_NODELIST_t *pndl;
	LPFC_SLI_RING_t *pring = &psli->ring[LPFC_FCP_RING];
	LPFC_IOCBQ_t *cmdiocbq = 0;
	LPFC_IOCBQ_t *rspiocbq = 0;
	DMABUFEXT_t *outdmp = 0;
	IOCB_t *cmd = 0;
	IOCB_t *rsp = 0;
	DMABUF_t *mp = 0;
	DMABUF_t *bmp = 0;
	int i0;
	char *outdta;
	uint32_t clear_count;
	int rc = 0;
	unsigned long iflag;
	uint32_t iocb_wait_timeout = cip->lpfc_arg5;
	uint32_t iocb_retries;

	struct {
		/* this rspcnt is really data buffer size */
		uint32_t rspcnt;
		/* this is sense count in case of LPFC_HBA_SEND_SCSI.
		 * It is fcp response size in case of LPFC_HBA_SEND_FCP
		 */
		uint32_t snscnt;
	} count;

	clp = &phba->config[0];

    /************************************************************************/

    /************************************************************************/
	reqbfrcnt = cip->lpfc_arg4;
	snsbfrcnt = cip->lpfc_flag;
	if ((reqbfrcnt + cip->lpfc_outsz) > (80 * 4096)) {
		/* lpfc_ioctl:error <idx> */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1604,	/* ptr to msg structure */
			       lpfc_mes1604,	/* ptr to msg */
			       lpfc_msgBlk1604.msgPreambleStr,	/* begin varargs */
			       0);	/* end varargs */
		rc = ERANGE;
		goto sndsczout;
	}

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & wwpn, (uint8_t *) cip->lpfc_arg3,
			   (ulong) (sizeof (HBA_WWN)))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		goto sndsczout;
	}
	LPFC_DRVR_LOCK(phba, iflag);

	pndl =
	    lpfc_findnode_wwpn(phba, NLP_SEARCH_MAPPED, (NAME_TYPE *) & wwpn);
	if (!pndl) {
		if (!(pndl = lpfc_findnode_wwpn(phba, NLP_SEARCH_UNMAPPED,
						(NAME_TYPE *) & wwpn))
		    || !(pndl->nlp_flag & NLP_TGT_NO_SCSIID)) {
			pndl = (LPFC_NODELIST_t *) 0;
		}
	}
	if (!pndl || !(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {
		rc = EACCES;
		goto sndsczout;
	}
	if (pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = ENODEV;
		goto sndsczout;
	}
	/* Allocate buffer for command iocb */
	if ((cmdiocbq = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	memset((void *)cmdiocbq, 0, sizeof (LPFC_IOCBQ_t));
	cmd = &(cmdiocbq->iocb);

	/* Allocate buffer for response iocb */
	if ((rspiocbq = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}
	memset((void *)rspiocbq, 0, sizeof (LPFC_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	/* Allocate buffer for Buffer ptr list */
	if (((bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys))) == 0)) {
		if (bmp)
			kfree(bmp);
		bmp = NULL;
		rc = ENOMEM;
		goto sndsczout;
	}
	INIT_LIST_HEAD(&bmp->list);
	bpl = (ULP_BDE64 *) bmp->virt;

	/* Allocate buffer for FCP CMND / FCP RSP */
	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);
		mp = NULL;
		rc = ENOMEM;
		goto sndsczout;
	}
	INIT_LIST_HEAD(&mp->list);
	fcpcmd = (FCP_CMND *) mp->virt;
	fcprsp = (FCP_RSP *) ((uint8_t *) mp->virt + sizeof (FCP_CMND));
	memset((void *)fcpcmd, 0, sizeof (FCP_CMND) + sizeof (FCP_RSP));

	/* Setup FCP CMND and FCP RSP */
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
	bpl->tus.f.bdeSize = sizeof (FCP_CMND);
	bpl->tus.f.bdeFlags = BUFF_USE_CMND;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys + sizeof (FCP_CMND)) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys + sizeof (FCP_CMND)) );
	bpl->tus.f.bdeSize = sizeof (FCP_RSP);
	bpl->tus.f.bdeFlags = (BUFF_USE_CMND | BUFF_USE_RCV);
	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	bpl++;

	/* Copy user data into fcpcmd buffer at this point to see if its a read
	   or a write.  */
	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) fcpcmd, (uint8_t *) cip->lpfc_arg1,
			   (ulong) (reqbfrcnt))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		goto sndsczout;
	}
	LPFC_DRVR_LOCK(phba, iflag);

	outdta = (fcpcmd->fcpCntl3 == WRITE_DATA ? cip->lpfc_dataout : 0);

	/* Allocate data buffer, and fill it if its a write */
	if (cip->lpfc_outsz == 0) {
		outdmp = dfc_cmd_data_alloc(phba, outdta, bpl, 512);
	} else {
		outdmp = dfc_cmd_data_alloc(phba, outdta, bpl, cip->lpfc_outsz);
	}
	if (outdmp == 0) {
		rc = ENOMEM;
		goto sndsczout;
	}

	cmd->un.fcpi64.bdl.ulpIoTag32 = 0;
	cmd->un.fcpi64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.fcpi64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.fcpi64.bdl.bdeSize = (3 * sizeof (ULP_BDE64));
	cmd->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->ulpBdeCount = 1;
	cmd->ulpContext = pndl->nlp_rpi;
	cmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
	cmd->ulpClass = pndl->nlp_fcp_info & 0x0f;
	cmd->ulpOwner = OWN_CHIP;
	cmd->ulpTimeout =
	    clp[LPFC_CFG_SCSI_REQ_TMO].a_current + phba->fcp_timeout_offset;
	cmd->ulpLe = 1;
	if (pndl->nlp_fcp_info & NLP_FCP_2_DEVICE) {
		cmd->ulpFCP2Rcvy = 1;
	}
	switch (fcpcmd->fcpCntl3) {
	case READ_DATA:	/* Set up for SCSI read */
		cmd->ulpCommand = CMD_FCP_IREAD64_CR;
		cmd->ulpPU = PARM_READ_CHECK;
		cmd->un.fcpi.fcpi_parm = cip->lpfc_outsz;
		cmd->un.fcpi64.bdl.bdeSize =
		    ((outdmp->flag + 2) * sizeof (ULP_BDE64));
		break;
	case WRITE_DATA:	/* Set up for SCSI write */
		cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
		cmd->un.fcpi64.bdl.bdeSize =
		    ((outdmp->flag + 2) * sizeof (ULP_BDE64));
		break;
	default:		/* Set up for SCSI command */
		cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof (ULP_BDE64));
		break;
	}
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	/* Set up the timeout value for the iocb wait command. */
	if( iocb_wait_timeout == 0 ) {
	        iocb_wait_timeout = clp[LPFC_CFG_SCSI_REQ_TMO].a_current +
		   phba->fcp_timeout_offset + LPFC_DRVR_TIMEOUT;
		/* Retry three times on getting IOCB_BUSY or
		   IOCB_TIMEOUT from issue_iocb. */
		iocb_retries = 4;
	} else {
	        /* Don't retry the iocb on failure. */
	        iocb_retries = 1;
	}

	/* send scsi command, retry 3 times on getting IOCB_BUSY, or
	   IOCB_TIMEOUT frm issue_iocb  */
	for (rc = -1, i0 = 0; i0 < iocb_retries && rc != IOCB_SUCCESS; i0++) {
		rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     iocb_wait_timeout);
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			break;
		}
	}

	if (rc != IOCB_SUCCESS) {
		rc = EACCES;
		goto sndsczout;
	}

	/* For LPFC_HBA_SEND_FCP, just return FCP_RSP unless we got
	 * an IOSTAT_LOCAL_REJECT.
	 *
	 * For SEND_FCP case, snscnt is really FCP_RSP length. In the
	 * switch statement below, the snscnt should not get destroyed.
	 */
	if (cmd->ulpCommand == CMD_FCP_IWRITE64_CX) {
		clear_count = (rsp->ulpStatus == IOSTAT_SUCCESS ? 1 : 0);
	} else {
		clear_count = cmd->un.fcpi.fcpi_parm;
	}
	if ((cip->lpfc_cmd == LPFC_HBA_SEND_FCP) &&
	    (rsp->ulpStatus != IOSTAT_LOCAL_REJECT)) {
		if (snsbfrcnt < sizeof (FCP_RSP)) {
			count.snscnt = snsbfrcnt;
		} else {
			count.snscnt = sizeof (FCP_RSP);
		}
		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user((uint8_t *) cip->lpfc_arg2, (uint8_t *) fcprsp,
				 count.snscnt)) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			goto sndsczout;
		}
		LPFC_DRVR_LOCK(phba, iflag);
	}
	switch (rsp->ulpStatus) {
	case IOSTAT_SUCCESS:
	      cpdata:
		if (cip->lpfc_outsz < clear_count) {
			cip->lpfc_outsz = 0;
			rc = ERANGE;
			break;
		}
		cip->lpfc_outsz = clear_count;
		if (cip->lpfc_cmd == LPFC_HBA_SEND_SCSI) {
			count.rspcnt = cip->lpfc_outsz;
			count.snscnt = 0;
		} else {	/* For LPFC_HBA_SEND_FCP, snscnt is already set */
			count.rspcnt = cip->lpfc_outsz;
		}
		LPFC_DRVR_UNLOCK(phba, iflag);
		/* Return data length */
		if (copy_to_user((uint8_t *) cip->lpfc_arg3, (uint8_t *) & count,
				 (2 * sizeof (uint32_t)))) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			break;
		}
		LPFC_DRVR_LOCK(phba, iflag);
		cip->lpfc_outsz = 0;
		if (count.rspcnt) {
			if (dfc_rsp_data_copy
			    (phba, (uint8_t *) cip->lpfc_dataout, outdmp,
			     count.rspcnt)) {
				rc = EIO;
				break;
			}
		}
		break;
	case IOSTAT_LOCAL_REJECT:
		cip->lpfc_outsz = 0;
		if (rsp->un.grsp.perr.statLocalError == IOERR_SEQUENCE_TIMEOUT) {
			rc = ETIMEDOUT;
			break;
		}
		rc = EFAULT;
		goto sndsczout;	/* count.rspcnt and count.snscnt is already 0 */
	case IOSTAT_FCP_RSP_ERROR:
		/* at this point, clear_count is the residual count. 
		 * We are changing it to the amount actually xfered.
		 */
		if (fcpcmd->fcpCntl3 == READ_DATA) {
			if ((fcprsp->rspStatus2 & RESID_UNDER)
			    && (fcprsp->rspStatus3 == SCSI_STAT_GOOD)) {
				goto cpdata;
			}
		} else {
			clear_count = 0;
		}
		count.rspcnt = (uint32_t) clear_count;
		cip->lpfc_outsz = 0;
		if (fcprsp->rspStatus2 & RSP_LEN_VALID) {
			j = be32_to_cpu(fcprsp->rspRspLen);
		}
		if (fcprsp->rspStatus2 & SNS_LEN_VALID) {
			if (cip->lpfc_cmd == LPFC_HBA_SEND_SCSI) {
				if (snsbfrcnt < be32_to_cpu(fcprsp->rspSnsLen))
					count.snscnt = snsbfrcnt;
				else
					count.snscnt =
					    be32_to_cpu(fcprsp->rspSnsLen);
				/* Return sense info from rsp packet */
				LPFC_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->lpfc_arg2,
				     ((uint8_t *) & fcprsp->rspInfo0) + j,
				     count.snscnt)) {
					rc = EIO;
					LPFC_DRVR_LOCK(phba, iflag);
					break;
				}
				LPFC_DRVR_LOCK(phba, iflag);
			}
		} else {
			rc = EFAULT;
			break;
		}
		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user(	/* return data length */
					(uint8_t *) cip->lpfc_arg3,
					(uint8_t *) & count,
					(2 * sizeof (uint32_t)))) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			break;
		}
		LPFC_DRVR_LOCK(phba, iflag);
		if (count.rspcnt) {	/* return data for read */
			if (dfc_rsp_data_copy
			    (phba, (uint8_t *) cip->lpfc_dataout, outdmp,
			     count.rspcnt)) {
				rc = EIO;
				break;
			}
		}
		break;
	default:
		cip->lpfc_outsz = 0;
		rc = EFAULT;
		break;
	}
      sndsczout:
	dfc_cmd_data_free(phba, outdmp);
	if (mp) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
	if (bmp) {
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
	}
	if (cmdiocbq)
		lpfc_iocb_free(phba, cmdiocbq);
	if (rspiocbq)
		lpfc_iocb_free(phba, rspiocbq);
	return (rc);
}

int
lpfc_ioctl_send_mgmt_rsp(lpfcHBA_t * phba,
			 LPFCCMDINPUT_t * cip)
{
	ULP_BDE64 *bpl;
	DMABUF_t *bmp;
	DMABUFEXT_t *indmp;
	uint32_t tag;
	int reqbfrcnt;
	int rc = 0;

	tag = (uint32_t) cip->lpfc_flag;	/* XRI for XMIT_SEQUENCE */
	reqbfrcnt = (ulong) cip->lpfc_arg2;

	if ((reqbfrcnt == 0) || (reqbfrcnt > (80 * 4096))) {
		rc = ERANGE;
		return (rc);
	}

	/* Allocate buffer for Buffer ptr list */
	if (((bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys))) == 0)) {
		if (bmp)
			kfree(bmp);
		rc = ENOMEM;
		return (rc);
	}
	INIT_LIST_HEAD(&bmp->list);
	bpl = (ULP_BDE64 *) bmp->virt;

	if ((indmp =
	     dfc_cmd_data_alloc(phba, (char *)cip->lpfc_arg1, bpl,
				reqbfrcnt)) == 0) {
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
		rc = ENOMEM;
		return (rc);
	}

	/* flag contains total number of BPLs for xmit */
	if ((rc = lpfc_issue_ct_rsp(phba, tag, bmp, indmp))) {
		if (rc == IOCB_TIMEDOUT)
			rc = ETIMEDOUT;
		else if (rc == IOCB_ERROR)
			rc = EACCES;
	}

	dfc_cmd_data_free(phba, indmp);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(bmp);

	return (rc);
}

int
lpfc_ioctl_send_mgmt_cmd(lpfcHBA_t * phba,
			 LPFCCMDINPUT_t * cip, void *dataout)
{
	LPFC_NODELIST_t *pndl;
	ULP_BDE64 *bpl;
	HBA_WWN findwwn;
	uint32_t finddid;
	LPFC_IOCBQ_t *cmdiocbq = 0;	/* Initialize the command iocb queue to a 0 default. */
	LPFC_IOCBQ_t *rspiocbq = 0;	/* Initialize the response iocb queue to a 0 default. */
	DMABUFEXT_t *indmp = 0;
	DMABUFEXT_t *outdmp = 0;
	IOCB_t *cmd = 0;
	IOCB_t *rsp = 0;
	DMABUF_t *mp = 0;
	DMABUF_t *bmp = 0;
	LPFC_SLI_t *psli = &phba->sli;
	LPFC_SLI_RING_t *pring = &psli->ring[LPFC_ELS_RING];	/* els ring */
	int i0 = 0, rc = 0;
	int reqbfrcnt;
	int snsbfrcnt;
	uint32_t timeout;
	unsigned long iflag;

	reqbfrcnt = cip->lpfc_arg4;
	snsbfrcnt = cip->lpfc_arg5;

	if (!(reqbfrcnt) || !(snsbfrcnt)
	    || (reqbfrcnt + snsbfrcnt) > (80 * 4096)) {
		rc = ERANGE;
		goto sndmgtqwt;
	}

	if (cip->lpfc_cmd == LPFC_HBA_SEND_MGMT_CMD) {

		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & findwwn, (uint8_t *) cip->lpfc_arg3,
		     (ulong) (sizeof (HBA_WWN)))) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			goto sndmgtqwt;
		}
		LPFC_DRVR_LOCK(phba, iflag);

		pndl =
		    lpfc_findnode_wwpn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & findwwn);
		if (!pndl) {
			rc = ENODEV;
			goto sndmgtqwt;
		}
	} else {
		finddid = (uint32_t)((unsigned long)cip->lpfc_arg3);
		if (!(pndl = lpfc_findnode_did(phba, 
					       NLP_SEARCH_MAPPED |
					       NLP_SEARCH_UNMAPPED, finddid))) {
			if (phba->fc_flag & FC_FABRIC) {
				if ((pndl = lpfc_nlp_alloc(phba, 0)) == 0) {
					rc = ENODEV;
					goto sndmgtqwt;
				}

				memset(pndl, 0, sizeof (LPFC_NODELIST_t));
				pndl->nlp_DID = finddid;

				if (lpfc_issue_els_plogi(phba, pndl, 0)) {
					lpfc_nlp_free(phba, pndl);
					rc = ENODEV;
					goto sndmgtqwt;
				}

				pndl->nlp_state = NLP_STE_PLOGI_ISSUE;
				lpfc_nlp_plogi(phba, pndl);

				/* Allow the node to complete discovery */
				while ((i0++ < 4) &&
				       ! (pndl = lpfc_findnode_did(phba,
								   NLP_SEARCH_MAPPED |
								   NLP_SEARCH_UNMAPPED, finddid))) {
					LPFC_DRVR_UNLOCK(phba, iflag);
					lpfc_sleep_ms(phba, 500);
					LPFC_DRVR_LOCK(phba, iflag);
				}

				if (i0 == 4) {
					rc = ENODEV;
					goto sndmgtqwt;
				}
			}
			else {
				rc = ENODEV;
				goto sndmgtqwt;
			}
		}
	}

	if (!pndl || !(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)) {
		rc = EACCES;
		goto sndmgtqwt;
	}
	if (pndl->nlp_flag & NLP_ELS_SND_MASK) {
		rc = ENODEV;
		goto sndmgtqwt;
	}
	if ((cmdiocbq = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	memset((void *)cmdiocbq, 0, sizeof (LPFC_IOCBQ_t));
	cmd = &(cmdiocbq->iocb);

	if ((rspiocbq = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	memset((void *)rspiocbq, 0, sizeof (LPFC_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	if (((bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys))) == 0)) {
		if (bmp)
			kfree(bmp);
		bmp = NULL;
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	INIT_LIST_HEAD(&bmp->list);

	bpl = (ULP_BDE64 *) bmp->virt;
	if ((indmp = dfc_cmd_data_alloc(phba, (char *)cip->lpfc_arg1, bpl,
					reqbfrcnt)) == 0) {
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
		bmp = NULL;
		rc = ENOMEM;
		goto sndmgtqwt;
	}
	bpl += indmp->flag;	/* flag contains total number of BPLs for xmit */
	if ((outdmp = dfc_cmd_data_alloc(phba, 0, bpl, snsbfrcnt)) == 0) {
		dfc_cmd_data_free(phba, indmp);
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
		bmp = NULL;
		rc = ENOMEM;
		goto sndmgtqwt;
	}

	cmd->un.genreq64.bdl.ulpIoTag32 = 0;
	cmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	cmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	cmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	cmd->un.genreq64.bdl.bdeSize =
	    (outdmp->flag + indmp->flag) * sizeof (ULP_BDE64);
	cmd->ulpCommand = CMD_GEN_REQUEST64_CR;
	cmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	cmd->un.genreq64.w5.hcsw.Dfctl = 0;
	cmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	cmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;
	cmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);
	cmd->ulpTimeout = cip->lpfc_flag;
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;
	cmd->ulpClass = CLASS3;
	cmd->ulpContext = pndl->nlp_rpi;
	cmd->ulpOwner = OWN_CHIP;
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->context2 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;

	if (cip->lpfc_flag < (phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT)) {
		timeout = phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT;
	} else {
		timeout = cip->lpfc_flag;
	}

	for (rc = -1, i0 = 0; i0 < 4 && rc != IOCB_SUCCESS; i0++) {
		rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					     SLI_IOCB_USE_TXQ, rspiocbq,
					     timeout);
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			goto sndmgtqwt;
		}
	}

	if (rc != IOCB_SUCCESS) {
		goto sndmgtqwt;
	}
	if (rsp->ulpStatus) {
		if (rsp->ulpStatus == IOSTAT_LOCAL_REJECT) {
			switch (rsp->un.ulpWord[4] & 0xff) {
			case IOERR_SEQUENCE_TIMEOUT:
				rc = ETIMEDOUT;
				break;
			case IOERR_INVALID_RPI:
				rc = EFAULT;
				break;
			default:
				rc = EACCES;
				break;
			}

			goto sndmgtqwt;
		}
	} else {
		outdmp->flag = rsp->un.genreq64.bdl.bdeSize;
	}
	if (outdmp->flag > snsbfrcnt) {	/* copy back response data */
		rc = ERANGE;	/* C_CT Request error */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1208,	/* ptr to msg structure */
			       lpfc_mes1208,	/* ptr to msg */
			       lpfc_msgBlk1208.msgPreambleStr,	/* begin varargs */
			       outdmp->flag, 4096);	/* end varargs */
		goto sndmgtqwt;
	}
	/* copy back size of response, and response itself */
	memcpy(dataout, (char *)&outdmp->flag, sizeof (int));
	if (dfc_rsp_data_copy
	    (phba, (uint8_t *) cip->lpfc_arg2, outdmp, outdmp->flag)) {
		rc = EIO;
		goto sndmgtqwt;
	}
      sndmgtqwt:
	dfc_cmd_data_free(phba, indmp);
	dfc_cmd_data_free(phba, outdmp);
	if (mp) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
	}
	if (bmp) {
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(bmp);
	}
	if (cmdiocbq)
		lpfc_iocb_free(phba, cmdiocbq);
	if (rspiocbq)
		lpfc_iocb_free(phba, rspiocbq);

	return (rc);
}

int
lpfc_ioctl_mbox(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	MAILBOX_t *pmbox;
	uint32_t size;
	dma_addr_t lptr;
	struct dfc_info *di;
	LPFC_MBOXQ_t *pmboxq;
	DMABUF_t *pbfrnfo;
	unsigned long iflag;
	int count = 0;
	int rc = 0;
	int mbxstatus = 0;

	/* Allocate mbox structure */
	if ((pmbox = (MAILBOX_t *) lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		return ENOMEM;
	}

	/* Allocate mboxq structure */
	if ((pmboxq = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		lpfc_mbox_free(phba, (LPFC_MBOXQ_t *) pmbox);
		return ENOMEM;
	}

	/* Allocate mbuf structure */
	if (((pbfrnfo = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((pbfrnfo->virt = lpfc_mbuf_alloc(phba,
					      0, &(pbfrnfo->phys))) == 0)) {
		if (pbfrnfo)
			kfree(pbfrnfo);
		lpfc_mbox_free(phba, (LPFC_MBOXQ_t *) pmbox);
		lpfc_mbox_free(phba, pmboxq);
		return ENOMEM;
	}
	INIT_LIST_HEAD(&pbfrnfo->list);
	di = &dfc.dfc_info[cip->lpfc_brd];

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) pmbox, (uint8_t *) cip->lpfc_arg1,
			   MAILBOX_CMD_WSIZE * sizeof (uint32_t))) {
		LPFC_DRVR_LOCK(phba, iflag);
		lpfc_mbox_free(phba, (LPFC_MBOXQ_t *) pmbox);
		lpfc_mbox_free(phba, pmboxq);
		lpfc_mbuf_free(phba, pbfrnfo->virt, pbfrnfo->phys);
		kfree(pbfrnfo);
		return EIO;
	}
	LPFC_DRVR_LOCK(phba, iflag);

	while (di->fc_flag & DFC_MBOX_ACTIVE) {
		LPFC_DRVR_UNLOCK(phba, iflag);
		lpfc_sleep_ms(phba, 5);
		LPFC_DRVR_LOCK(phba, iflag);
		if (count++ == 200)
			break;
	}

	if (count >= 200) {
		pmbox->mbxStatus = MBXERR_ERROR;
		rc = EAGAIN;
		goto mbout_err;
	} else {
#ifdef _LP64
		if ((pmbox->mbxCommand == MBX_READ_SPARM) ||
		    (pmbox->mbxCommand == MBX_READ_RPI) ||
		    (pmbox->mbxCommand == MBX_REG_LOGIN) ||
		    (pmbox->mbxCommand == MBX_READ_LA)) {
			/* Must use 64 bit versions of these mbox cmds */
			pmbox->mbxStatus = MBXERR_ERROR;
			rc = ENODEV;
			goto mbout_err;
		}
#endif
		di->fc_flag |= DFC_MBOX_ACTIVE;
		lptr = 0;
		size = 0;
		switch (pmbox->mbxCommand) {
			/* Offline only */
		case MBX_WRITE_NV:
		case MBX_INIT_LINK:
		case MBX_DOWN_LINK:
		case MBX_CONFIG_LINK:
		case MBX_CONFIG_RING:
		case MBX_RESET_RING:
		case MBX_UNREG_LOGIN:
		case MBX_CLEAR_LA:
		case MBX_DUMP_CONTEXT:
		case MBX_RUN_DIAGS:
		case MBX_RESTART:
		case MBX_FLASH_WR_ULA:
		case MBX_SET_MASK:
		case MBX_SET_SLIM:
		case MBX_SET_DEBUG:
			if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
				pmbox->mbxStatus = MBXERR_ERROR;
				di->fc_flag &= ~DFC_MBOX_ACTIVE;
				goto mbout_err;
			}
			break;

			/* Online / Offline */
		case MBX_LOAD_SM:
		case MBX_READ_NV:
		case MBX_READ_CONFIG:
		case MBX_READ_RCONFIG:
		case MBX_READ_STATUS:
		case MBX_READ_XRI:
		case MBX_READ_REV:
		case MBX_READ_LNK_STAT:
		case MBX_DUMP_MEMORY:
		case MBX_DOWN_LOAD:
		case MBX_UPDATE_CFG:
		case MBX_LOAD_AREA:
		case MBX_LOAD_EXP_ROM:
			break;

			/* Online / Offline - with DMA */
		case MBX_READ_SPARM64:
			lptr = getPaddr(pmbox->un.varRdSparm.un.sp64.addrHigh,
					pmbox->un.varRdSparm.un.sp64.addrLow);
			size = (int)pmbox->un.varRdSparm.un.sp64.tus.f.bdeSize;
			if (lptr) {
				pmbox->un.varRdSparm.un.sp64.addrHigh =
				    putPaddrHigh(pbfrnfo->phys);
				pmbox->un.varRdSparm.un.sp64.addrLow =
				    putPaddrLow(pbfrnfo->phys);
			}
			break;

		case MBX_READ_RPI64:
			/* This is only allowed when online is SLI2 mode */
			lptr = getPaddr(pmbox->un.varRdRPI.un.sp64.addrHigh,
					pmbox->un.varRdRPI.un.sp64.addrLow);
			size = (int)pmbox->un.varRdRPI.un.sp64.tus.f.bdeSize;
			if (lptr) {
				pmbox->un.varRdRPI.un.sp64.addrHigh =
				    putPaddrHigh(pbfrnfo->phys);
				pmbox->un.varRdRPI.un.sp64.addrLow =
				    putPaddrLow(pbfrnfo->phys);
			}
			break;

		case MBX_READ_LA:
		case MBX_READ_LA64:
		case MBX_REG_LOGIN:
		case MBX_REG_LOGIN64:
		case MBX_CONFIG_PORT:
		case MBX_RUN_BIU_DIAG:
			/* Do not allow SLI-2 commands */
			pmbox->mbxStatus = MBXERR_ERROR;
			di->fc_flag &= ~DFC_MBOX_ACTIVE;
			goto mbout_err;

		default:
			/* Offline only
			 * Let firmware return error for unsupported commands
			 */
			if (!(phba->fc_flag & FC_OFFLINE_MODE)) {
				pmbox->mbxStatus = MBXERR_ERROR;
				di->fc_flag &= ~DFC_MBOX_ACTIVE;
				goto mbout_err;
			}
			break;
		}		/* switch pmbox->command */

		{
			MAILBOX_t *pmb = &pmboxq->mb;
			LPFC_SLI_t *psli = &phba->sli;

			memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
			pmb->mbxCommand = pmbox->mbxCommand;
			pmb->mbxOwner = pmbox->mbxOwner;
			pmb->un = pmbox->un;
			pmb->us = pmbox->us;
			pmboxq->context1 = (uint8_t *) 0;
			if ((phba->fc_flag & FC_OFFLINE_MODE) ||
			    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))){
				LPFC_DRVR_UNLOCK(phba, iflag);
				mbxstatus =
				    lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
				LPFC_DRVR_LOCK(phba, iflag);
			} else
				mbxstatus =
				    lpfc_sli_issue_mbox_wait(phba, pmboxq,
							    LPFC_MBOX_TMO);
			di->fc_flag &= ~DFC_MBOX_ACTIVE;

			if (mbxstatus == MBX_TIMEOUT) {
				rc = EBUSY;
				goto mbout;
			} else if (mbxstatus != MBX_SUCCESS) {
				rc = ENODEV;
				/* Not successful */
				goto mbout;
			}
		}

		if (lptr) {
			LPFC_DRVR_UNLOCK(phba, iflag);
			if ((copy_to_user
			     ((uint8_t *) & lptr, (uint8_t *) pbfrnfo->virt,
			      (ulong) size))) {
				rc = EIO;
			}
			LPFC_DRVR_LOCK(phba, iflag);
		}
	}

      mbout:
	{
		MAILBOX_t *pmb = &pmboxq->mb;

		memcpy(dataout, (char *)pmb,
		       MAILBOX_CMD_WSIZE * sizeof (uint32_t));
	}

	goto mbout_freemem;

      mbout_err:
	{
		/* Jump here only if there is an error and copy the status */
		memcpy(dataout, (char *)pmbox,
		       MAILBOX_CMD_WSIZE * sizeof (uint32_t));
	}

      mbout_freemem:
	/* Free allocated mbox memory */
	if (pmbox)
		lpfc_mbox_free(phba, (LPFC_MBOXQ_t *) pmbox);

	/* Free allocated mboxq memory */
	if (pmboxq) {
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			pmboxq->mbox_cmpl = 0;
		} else {
			lpfc_mbox_free(phba, pmboxq);
		}
	}

	/* Free allocated mbuf memory */
	if (pbfrnfo) {
		lpfc_mbuf_free(phba, pbfrnfo->virt, pbfrnfo->phys);
		kfree(pbfrnfo);
	}

	return (rc);
}
int
lpfc_ioctl_linkinfo(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{
	LinkInfo *linkinfo;
	int rc = 0;

	linkinfo = (LinkInfo *) dataout;
	linkinfo->a_linkEventTag = phba->fc_eventTag;
	linkinfo->a_linkUp = phba->fc_stat.LinkUp;
	linkinfo->a_linkDown = phba->fc_stat.LinkDown;
	linkinfo->a_linkMulti = phba->fc_stat.LinkMultiEvent;
	linkinfo->a_DID = phba->fc_myDID;
	if (phba->fc_topology == TOPOLOGY_LOOP) {
		if (phba->fc_flag & FC_PUBLIC_LOOP) {
			linkinfo->a_topology = LNK_PUBLIC_LOOP;
			memcpy((uint8_t *) linkinfo->a_alpaMap,
			       (uint8_t *) phba->alpa_map, 128);
			linkinfo->a_alpaCnt = phba->alpa_map[0];
		} else {
			linkinfo->a_topology = LNK_LOOP;
			memcpy((uint8_t *) linkinfo->a_alpaMap,
			       (uint8_t *) phba->alpa_map, 128);
			linkinfo->a_alpaCnt = phba->alpa_map[0];
		}
	} else {
		memset((uint8_t *) linkinfo->a_alpaMap, 0, 128);
		linkinfo->a_alpaCnt = 0;
		if (phba->fc_flag & FC_FABRIC) {
			linkinfo->a_topology = LNK_FABRIC;
		} else {
			linkinfo->a_topology = LNK_PT2PT;
		}
	}
	linkinfo->a_linkState = 0;
	switch (phba->hba_state) {
	case LPFC_INIT_START:

	case LPFC_LINK_DOWN:
		linkinfo->a_linkState = LNK_DOWN;
		memset((uint8_t *) linkinfo->a_alpaMap, 0, 128);
		linkinfo->a_alpaCnt = 0;
		break;
	case LPFC_LINK_UP:

	case LPFC_LOCAL_CFG_LINK:
		linkinfo->a_linkState = LNK_UP;
		break;
	case LPFC_FLOGI:
		linkinfo->a_linkState = LNK_FLOGI;
		break;
	case LPFC_DISC_AUTH:
	case LPFC_FABRIC_CFG_LINK:
	case LPFC_NS_REG:
	case LPFC_NS_QRY:

	case LPFC_CLEAR_LA:
		linkinfo->a_linkState = LNK_DISCOVERY;
		break;
	case LPFC_HBA_READY:
		linkinfo->a_linkState = LNK_READY;
		break;
	}
	linkinfo->a_alpa = (uint8_t) (phba->fc_myDID & 0xff);
	memcpy((uint8_t *) linkinfo->a_wwpName,
	       (uint8_t *) & phba->fc_portname, 8);
	memcpy((uint8_t *) linkinfo->a_wwnName,
	       (uint8_t *) & phba->fc_nodename, 8);

	return (rc);
}

int
lpfc_ioctl_ioinfo(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{

	IOinfo *ioinfo;
	LPFC_SLI_t *psli;
	int rc = 0;

	psli = &phba->sli;

	ioinfo = (IOinfo *) dataout;
	ioinfo->a_mbxCmd = psli->slistat.mboxCmd;
	ioinfo->a_mboxCmpl = psli->slistat.mboxEvent;
	ioinfo->a_mboxErr = psli->slistat.mboxStatErr;
	ioinfo->a_iocbCmd = psli->slistat.iocbCmd[cip->lpfc_ring];
	ioinfo->a_iocbRsp = psli->slistat.iocbRsp[cip->lpfc_ring];
	ioinfo->a_adapterIntr = (psli->slistat.linkEvent +
				 psli->slistat.iocbRsp[cip->lpfc_ring] +
				 psli->slistat.mboxEvent);
	ioinfo->a_fcpCmd = phba->fc_stat.fcpCmd;
	ioinfo->a_fcpCmpl = phba->fc_stat.fcpCmpl;
	ioinfo->a_fcpErr = phba->fc_stat.fcpRspErr +
	    phba->fc_stat.fcpRemoteStop + phba->fc_stat.fcpPortRjt +
	    phba->fc_stat.fcpPortBusy + phba->fc_stat.fcpError +
	    phba->fc_stat.fcpLocalErr;
	ioinfo->a_bcastRcv = phba->fc_stat.frameRcvBcast;
	ioinfo->a_RSCNRcv = phba->fc_stat.elsRcvRSCN;
	ioinfo->a_cnt1 = 0;
	ioinfo->a_cnt2 = 0;
	ioinfo->a_cnt3 = 0;
	ioinfo->a_cnt4 = 0;

	return (rc);
}

int
lpfc_ioctl_nodeinfo(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout, int size)
{
	NodeInfo *np;
	LPFC_NODELIST_t *pndl;
	LPFC_BINDLIST_t *pbdl;
	uint32_t cnt;
	int rc = 0;
	uint32_t total_mem = size;
	struct list_head *pos, *listp;
	struct list_head *node_list[4];
	int i;

	np = (NodeInfo *) dataout;
	cnt = 0;

	/* Since the size of bind & others are different,
	   get the node list of bind first
	 */
	total_mem -= sizeof (LPFC_BINDLIST_t);

	list_for_each(pos, &phba->fc_nlpbind_list) {
		if (total_mem <= 0)
			break;
		pbdl = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);
		memset((uint8_t *) np, 0, sizeof (LPFC_BINDLIST_t));
		if (pbdl->nlp_bind_type & FCP_SEED_WWPN)
			np->a_flag |= NODE_SEED_WWPN;
		if (pbdl->nlp_bind_type & FCP_SEED_WWNN)
			np->a_flag |= NODE_SEED_WWNN;
		if (pbdl->nlp_bind_type & FCP_SEED_DID)
			np->a_flag |= NODE_SEED_DID;
		if (pbdl->nlp_bind_type & FCP_SEED_AUTO)
			np->a_flag |= NODE_AUTOMAP;
		np->a_state = NODE_SEED;
		np->a_did = pbdl->nlp_DID;
		np->a_targetid = pbdl->nlp_sid;
		memcpy(np->a_wwpn, &pbdl->nlp_portname, 8);
		memcpy(np->a_wwnn, &pbdl->nlp_nodename, 8);
		total_mem -= sizeof (LPFC_BINDLIST_t);
		np++;
		cnt++;
	}

	/* Get the node list of unmap, map, plogi and adisc
	 */
	total_mem -= sizeof (LPFC_NODELIST_t);

	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_nlpmap_list;
	for (i = 0; i < 4; i++) {
		listp = node_list[i];
		if (list_empty(listp)) 
			continue;	

		list_for_each(pos, listp) {
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (total_mem <= 0)
				break;
			memset((uint8_t *) np, 0, sizeof (LPFC_NODELIST_t));
			if (pndl->nlp_flag & NLP_ADISC_LIST) {
				np->a_flag |= NODE_ADDR_AUTH;
				np->a_state = NODE_LIMBO;
			}
			if (pndl->nlp_flag & NLP_PLOGI_LIST) {
				np->a_state = NODE_PLOGI;
			}
			if (pndl->nlp_flag & NLP_MAPPED_LIST) {
				np->a_state = NODE_ALLOC;
			}
			if (pndl->nlp_flag & NLP_UNMAPPED_LIST) {
				np->a_state = NODE_PRLI;
			}
			if (pndl->nlp_type & NLP_FABRIC)
				np->a_flag |= NODE_FABRIC;
			if (pndl->nlp_type & NLP_FCP_TARGET)
				np->a_flag |= NODE_FCP_TARGET;
			if (pndl->nlp_flag & NLP_ELS_SND_MASK)	/* Sent ELS mask  -- Check this */
				np->a_flag |= NODE_REQ_SND;
			if (pndl->nlp_flag & NLP_FARP_SND)
				np->a_flag |= NODE_FARP_SND;
			if (pndl->nlp_flag & NLP_SEED_WWPN)
				np->a_flag |= NODE_SEED_WWPN;
			if (pndl->nlp_flag & NLP_SEED_WWNN)
				np->a_flag |= NODE_SEED_WWNN;
			if (pndl->nlp_flag & NLP_SEED_DID)
				np->a_flag |= NODE_SEED_DID;
			if (pndl->nlp_flag & NLP_AUTOMAP)
				np->a_flag |= NODE_AUTOMAP;
			np->a_did = pndl->nlp_DID;
			np->a_targetid = pndl->nlp_sid;
			memcpy(np->a_wwpn, &pndl->nlp_portname, 8);
			memcpy(np->a_wwnn, &pndl->nlp_nodename, 8);
			total_mem -= sizeof (LPFC_NODELIST_t);
			np++;
			cnt++;
		}
	}
	cip->lpfc_outsz = (uint32_t) (cnt * sizeof (NodeInfo));

	return (rc);
}

int
lpfc_ioctl_getcfg(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{

	CfgParam *cp;
	iCfgParam *icp;
	uint32_t cnt;
	lpfcCfgParam_t *clp;
	int i, rc = 0, astringi;

	clp = &phba->config[0];
	/* First uint32_t word will be count */
	cp = (CfgParam *) dataout;
	cnt = 0;
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		icp = (iCfgParam *) & clp[i];
		if (!(icp->a_flag & CFG_EXPORT))
			continue;
		cp->a_low = icp->a_low;
		cp->a_hi = icp->a_hi;
		cp->a_flag = icp->a_flag;
		cp->a_default = icp->a_default;
		if (i == LPFC_CFG_FCP_CLASS) {
			switch (icp->a_current) {
			case CLASS1:
				cp->a_current = 1;
				break;
			case CLASS2:
				cp->a_current = 2;
				break;
			case CLASS3:
				cp->a_current = 3;
				break;
			}
		} else {
			cp->a_current = icp->a_current;
		}
		cp->a_changestate = icp->a_changestate;
		memcpy(cp->a_string, icp->a_string, 32);

		/* Translate all "_" to "-" to preserve backwards compatibility
		with older drivers that used "_" */
		astringi=0;
		while(cp->a_string[astringi++])
			if(cp->a_string[astringi] == '_')
				cp->a_string[astringi] = '-';

		memcpy(cp->a_help, icp->a_help, 80);
		cp++;
		cnt++;
	}
	if (cnt) {
		cip->lpfc_outsz = (uint32_t) (cnt * sizeof (CfgParam));
	}

	return (rc);
}

int
lpfc_ioctl_setcfg(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{

	iCfgParam *icp;
	uint32_t offset, cnt;
	lpfcCfgParam_t *clp;
	LPFC_SLI_t *psli;
	int rc = 0;
	int i, j;

	psli = &phba->sli;
	clp = &phba->config[0];
	offset = (ulong) cip->lpfc_arg1;
	cnt = (ulong) cip->lpfc_arg2;
	if (offset >= LPFC_TOTAL_NUM_OF_CFG_PARAM) {
		rc = ERANGE;
		return (rc);
	}
	j = offset;
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		icp = (iCfgParam *) & clp[i];
		if (!(icp->a_flag & CFG_EXPORT))
			continue;
		if (j == 0)
			break;
		j--;
	}
	if (icp->a_changestate != CFG_DYNAMIC) {
		rc = EPERM;
		return (rc);
	}
	if (((icp->a_low != 0) && (cnt < icp->a_low)) || (cnt > icp->a_hi)) {
		rc = ERANGE;
		return (rc);
	}
	if (!(icp->a_flag & CFG_EXPORT)) {
		rc = EPERM;
		return (rc);
	}
	switch (offset) {
	case LPFC_CFG_FCP_CLASS:
		switch (cnt) {
		case 1:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS1;
			break;
		case 2:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS2;
			break;
		case 3:
			clp[LPFC_CFG_FCP_CLASS].a_current = CLASS3;
			break;
		}
		icp->a_current = cnt;
		break;

	case LPFC_CFG_LINKDOWN_TMO:
		icp->a_current = cnt;
		break;

	default:
		icp->a_current = cnt;
	}

	return (rc);
}

int
lpfc_ioctl_hba_get_event(lpfcHBA_t * phba,
			 LPFCCMDINPUT_t * cip, 
			 void *dataout, int data_size)
{
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp;
	uint8_t *cp;
	void *type;
	uint32_t offset, incr, size, cnt, i, gstype;
	DMABUF_t *mm;
	int no_more;
	int rc = 0;
	uint32_t total_mem = data_size;
	unsigned long iflag;
	struct list_head head, *pos, *tmp_pos;

	no_more = 1;

	offset = ((ulong) cip->lpfc_arg3 &	/* event mask */
		  FC_REG_EVENT_MASK);	/* event mask */
	incr = (uint32_t) cip->lpfc_flag;	/* event id   */
	size = (uint32_t) cip->lpfc_iocb;	/* process requesting evt  */

	type = 0;
	switch (offset) {
	case FC_REG_CT_EVENT:
		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_from_user
		    ((uint8_t *) & gstype, (uint8_t *) cip->lpfc_arg2,
		     (ulong) (sizeof (uint32_t)))) {
			rc = EIO;
			LPFC_DRVR_LOCK(phba, iflag);
			return (rc);
		}
		LPFC_DRVR_LOCK(phba, iflag);
		type = (void *)(ulong) gstype;
		break;
	}

	ehp = (fcEVTHDR_t *) phba->fc_evt_head;

	while (ehp) {
		if ((ehp->e_mask == offset) && (ehp->e_type == type))
			break;
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		rc = ENOENT;
		return (rc);
	}

	ep = ehp->e_head;
	oep = 0;
	while (ep) {
		/* Find an event that matches the event mask */
		if (ep->evt_sleep == 0) {
			/* dequeue event from event list */
			if (oep == 0) {
				ehp->e_head = ep->evt_next;
			} else {
				oep->evt_next = ep->evt_next;
			}
			if (ehp->e_tail == ep)
				ehp->e_tail = oep;

			switch (offset) {
			case FC_REG_LINK_EVENT:
				break;
			case FC_REG_RSCN_EVENT:
				/* Return data length */
				cnt = sizeof (uint32_t);
				LPFC_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->lpfc_arg1,
				     (uint8_t *) & cnt, sizeof (uint32_t))) {
					rc = EIO;
				}
				LPFC_DRVR_LOCK(phba, iflag);
				memcpy(dataout, (char *)&ep->evt_data0,
				       cnt);
				cip->lpfc_outsz = (uint32_t) cnt;
				break;
			case FC_REG_CT_EVENT:
				/* Return data length */
				cnt = (ulong) (ep->evt_data2);
				LPFC_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->lpfc_arg1,
				     (uint8_t *) & cnt, sizeof (uint32_t))) {
					rc = EIO;
				} else {
					if (copy_to_user
					    ((uint8_t *) cip->lpfc_arg2,
					     (uint8_t *) & ep->evt_data0,
					     sizeof (uint32_t))) {
						rc = EIO;
					}
				}
				LPFC_DRVR_LOCK(phba, iflag);

				cip->lpfc_outsz = (uint32_t) cnt;
				i = cnt;
				mm = (DMABUF_t *) ep->evt_data1;
				cp = (uint8_t *) dataout;
				list_add_tail(&head, &mm->list);
				list_for_each_safe(pos, tmp_pos, &head) {
					mm = list_entry(pos, DMABUF_t, list);

					if (cnt > FCELSSIZE)
						i = FCELSSIZE;
					else
						i = cnt;

					if (total_mem > 0) {
						memcpy(cp, (char *)mm->virt, i);
						total_mem -= i;
					}

					cp += i;
					lpfc_mbuf_free(phba, mm->virt,
						       mm->phys);
					list_del(pos);
					kfree(mm);
				}
				list_del(&head);
				break;
			}

			if ((offset == FC_REG_CT_EVENT) && (ep->evt_next) &&
			    (((fcEVT_t *) (ep->evt_next))->evt_sleep == 0)) {
				ep->evt_data0 |= 0x80000000;	/* More event r waiting */
				LPFC_DRVR_UNLOCK(phba, iflag);
				if (copy_to_user
				    ((uint8_t *) cip->lpfc_arg2,
				     (uint8_t *) & ep->evt_data0,
				     sizeof (uint32_t))) {
					rc = EIO;
				}
				LPFC_DRVR_LOCK(phba, iflag);
				no_more = 0;
			}

			/* Requeue event entry */
			ep->evt_next = 0;
			ep->evt_data0 = 0;
			ep->evt_data1 = 0;
			ep->evt_data2 = 0;
			ep->evt_sleep = 1;
			ep->evt_flags = 0;

			if (ehp->e_head == 0) {
				ehp->e_head = ep;
				ehp->e_tail = ep;
			} else {
				ehp->e_tail->evt_next = ep;
				ehp->e_tail = ep;
			}

			if (offset == FC_REG_LINK_EVENT) {
				ehp->e_flag &= ~E_GET_EVENT_ACTIVE;
				rc = lpfc_ioctl_linkinfo(phba, cip, dataout);
				return (rc);
			}

			if (no_more)
				ehp->e_flag &= ~E_GET_EVENT_ACTIVE;
			return (rc);
			/*
			   break;
			 */
		}
		oep = ep;
		ep = ep->evt_next;
	}
	if (ep == 0) {
		/* No event found */
		rc = ENOENT;
	}

	return (rc);
}

int
lpfc_sleep_event(lpfcHBA_t * phba, fcEVTHDR_t * ep)
{

	ep->e_mode |= E_SLEEPING_MODE;
	switch (ep->e_mask) {
	case FC_REG_LINK_EVENT:
		return (lpfc_sleep(phba, &phba->linkevtwq, 0));
	case FC_REG_RSCN_EVENT:
		return (lpfc_sleep(phba, &phba->rscnevtwq, 0));
	case FC_REG_CT_EVENT:
		return (lpfc_sleep(phba, &phba->ctevtwq, 0));
	}
	return (0);
}

int
lpfc_ioctl_hba_set_event(lpfcHBA_t * phba,
			 LPFCCMDINPUT_t * cip)
{
	fcEVT_t *evp;
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp;
	fcEVTHDR_t *oehp;
	int found;
	void *type;
	uint32_t offset, incr;
	int rc = 0;

	offset = (((ulong) cip->lpfc_arg3) &	/* event mask */
		  FC_REG_EVENT_MASK);
	incr = (uint32_t) cip->lpfc_flag;	/* event id   */
	switch (offset) {
	case FC_REG_CT_EVENT:
		type = cip->lpfc_arg2;
		found = LPFC_MAX_EVENT;	/* Number of events we can queue up + 1, before
					 * dropping events for this event id.  */
		break;
	case FC_REG_RSCN_EVENT:
		type = (void *)0;
		found = LPFC_MAX_EVENT;	/* Number of events we can queue up + 1, before
					 * dropping events for this event id.  */
		break;
	case FC_REG_LINK_EVENT:
		type = (void *)0;
		found = 2;	/* Number of events we can queue up + 1, before
				 * dropping events for this event id.  */
		break;
	default:
		found = 0;
		rc = EINTR;
		return (rc);
	}

	/*
	 * find the fcEVT_t header for this Event, allocate a header
	 * if not found.
	 */
	oehp = 0;
	ehp = (fcEVTHDR_t *) phba->fc_evt_head;
	while (ehp) {
		if ((ehp->e_mask == offset) && (ehp->e_type == type)) {
			found = 0;
			break;
		}
		oehp = ehp;
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		ehp = kmalloc (sizeof (fcEVTHDR_t),
			       GFP_ATOMIC);
		if (ehp == 0 ) {
			rc = EINTR;
			return (rc);
		}
		memset((char *)ehp, 0, sizeof (fcEVTHDR_t));
		if (phba->fc_evt_head == 0) {
			phba->fc_evt_head = ehp;
			phba->fc_evt_tail = ehp;
		} else {
			((fcEVTHDR_t *) (phba->fc_evt_tail))->e_next_header =
			    ehp;
			phba->fc_evt_tail = (void *)ehp;
		}
		ehp->e_handle = incr;
		ehp->e_mask = offset;
		ehp->e_type = type;
		ehp->e_refcnt++;
	} else {
		ehp->e_refcnt++;
	}

	while (found) {
		/* Save event id for C_GET_EVENT */
		oep = kmalloc (sizeof (fcEVT_t),
			       GFP_ATOMIC);
		if ( oep ==  0) {
			rc = EINTR;
			break;
		}
		memset((char *)oep, 0, sizeof (fcEVT_t));

		oep->evt_sleep = 1;
		oep->evt_handle = incr;
		oep->evt_mask = offset;
		oep->evt_type = type;

		if (ehp->e_head == 0) {
			ehp->e_head = oep;
			ehp->e_tail = oep;
		} else {
			ehp->e_tail->evt_next = (void *)oep;
			ehp->e_tail = oep;
		}
		oep->evt_next = 0;
		found--;
	}

	switch (offset) {
	case FC_REG_CT_EVENT:
	case FC_REG_RSCN_EVENT:
	case FC_REG_LINK_EVENT:

		if (rc || lpfc_sleep_event(phba, ehp)) {
			rc = EINTR;
			ehp->e_mode &= ~E_SLEEPING_MODE;
			ehp->e_refcnt--;
			if (ehp->e_refcnt) {
				goto setout;
			}
			/* Remove all eventIds from queue */
			ep = ehp->e_head;
			oep = 0;
			found = 0;
			while (ep) {
				if (ep->evt_handle == incr) {
					/* dequeue event from event list */
					if (oep == 0) {
						ehp->e_head = ep->evt_next;
					} else {
						oep->evt_next = ep->evt_next;
					}
					if (ehp->e_tail == ep)
						ehp->e_tail = oep;
					evp = ep;
					ep = ep->evt_next;
					kfree(evp);
				} else {
					oep = ep;
					ep = ep->evt_next;
				}
			}

			/*
			 * No more fcEVT_t pointer under this fcEVTHDR_t
			 * Free the fcEVTHDR_t
			 */
			if (ehp->e_head == 0) {
				oehp = 0;
				ehp = (fcEVTHDR_t *) phba->fc_evt_head;
				while (ehp) {
					if ((ehp->e_mask == offset) &&
					    (ehp->e_type == type)) {
						found = 0;
						break;
					}
					oehp = ehp;
					ehp = (fcEVTHDR_t *) ehp->e_next_header;
				}
				if (oehp == 0) {
					phba->fc_evt_head = ehp->e_next_header;
				} else {
					oehp->e_next_header =
					    ehp->e_next_header;
				}
				if (phba->fc_evt_tail == ehp)
					phba->fc_evt_tail = oehp;

				kfree(ehp);
			}
			goto setout;
		}
		ehp->e_refcnt--;
		break;
	}
      setout:
	return (rc);
}

int
lpfc_ioctl_add_bind(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{

	bind_ctl_t bind_ctl;
	void *bind_id = 0;
	uint8_t bind_type = FCP_SEED_WWNN;
	int rc = 0;
	unsigned long iflag;

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & bind_ctl, (uint8_t *) cip->lpfc_arg1,
			   (ulong) (sizeof (bind_ctl)))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		return rc;
	}
	LPFC_DRVR_LOCK(phba, iflag);

	switch (bind_ctl.bind_type) {
	case LPFC_WWNN_BIND:
		bind_type = FCP_SEED_WWNN;
		bind_id = &bind_ctl.wwnn[0];
		break;
	case LPFC_WWPN_BIND:
		bind_type = FCP_SEED_WWPN;
		bind_id = &bind_ctl.wwpn[0];
		break;
	case LPFC_DID_BIND:
		bind_type = FCP_SEED_DID;
		bind_id = &bind_ctl.did;
		break;
	default:
		rc = EIO;
		break;
	}

	if (rc)
		return rc;

	rc = lpfc_add_bind(phba, bind_type, bind_id, bind_ctl.scsi_id);
	return rc;
}

int
lpfc_ioctl_del_bind(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip)
{

	bind_ctl_t bind_ctl;
	void *bind_id = 0;
	uint8_t bind_type = FCP_SEED_WWNN;
	int rc = 0;
	unsigned long iflag;

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & bind_ctl, (uint8_t *) cip->lpfc_arg1,
			   (ulong) (sizeof (bind_ctl)))) {
		LPFC_DRVR_LOCK(phba, iflag);
		rc = EIO;
		return rc;
	}
	LPFC_DRVR_LOCK(phba, iflag);

	switch (bind_ctl.bind_type) {

	case LPFC_WWNN_BIND:
		bind_type = FCP_SEED_WWNN;
		bind_id = &bind_ctl.wwnn[0];
		break;

	case LPFC_WWPN_BIND:
		bind_type = FCP_SEED_WWPN;
		bind_id = &bind_ctl.wwpn[0];
		break;

	case LPFC_DID_BIND:
		bind_type = FCP_SEED_DID;
		bind_id = &bind_ctl.did;
		break;

	case LPFC_SCSI_ID:
		bind_id = 0;
		break;

	default:
		rc = EIO;
		break;
	}

	if (rc)
		return rc;

	rc = lpfc_del_bind(phba, bind_type, bind_id, bind_ctl.scsi_id);

	return rc;
}

int
lpfc_ioctl_list_bind(lpfcHBA_t * phba,
		     LPFCCMDINPUT_t * cip, void *dataout, int *do_cp)
{

	unsigned long next_index = 0;
	unsigned long max_index = (unsigned long)cip->lpfc_arg1;
	HBA_BIND_LIST *bind_list;
	HBA_BIND_ENTRY *bind_array;
	LPFC_BINDLIST_t *pbdl;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos;
	int rc;

	bind_list = (HBA_BIND_LIST *) dataout;
	bind_array = &bind_list->entry[0];

	/* Iterate through the mapped list */
	list_for_each(pos, &phba->fc_nlpmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}

		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].scsi_id = pndl->nlp_sid;
		bind_array[next_index].did = pndl->nlp_DID;
		memcpy(&bind_array[next_index].wwpn, &pndl->nlp_portname,
		       sizeof (HBA_WWN));
		memcpy(&bind_array[next_index].wwnn, &pndl->nlp_nodename,
		       sizeof (HBA_WWN));
		if (pndl->nlp_flag & NLP_AUTOMAP)
			bind_array[next_index].flags |= HBA_BIND_AUTOMAP;
		if (pndl->nlp_flag & NLP_SEED_WWNN)
			bind_array[next_index].bind_type = BIND_WWNN;
		if (pndl->nlp_flag & NLP_SEED_WWPN)
			bind_array[next_index].bind_type = BIND_WWPN;
		if (pndl->nlp_flag & NLP_SEED_ALPA)
			bind_array[next_index].bind_type = BIND_ALPA;
		else if (pndl->nlp_flag & NLP_SEED_DID)
			bind_array[next_index].bind_type = BIND_DID;
		bind_array[next_index].flags |= HBA_BIND_MAPPED;
		if (pndl && pndl->nlp_Target) {
		    if(pndl->nlp_Target->targetFlags & FC_NPR_ACTIVE)
			bind_array[next_index].flags |= HBA_BIND_NODEVTMO;
		    if(pndl->nlp_Target->rptLunState == REPORT_LUN_COMPLETE)
			bind_array[next_index].flags |= HBA_BIND_RPTLUNST;
		}
		next_index++;
	}

	/* Iterate through the unmapped list */
	list_for_each(pos, &phba->fc_nlpunmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}

		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].did = pndl->nlp_DID;
		memcpy(&bind_array[next_index].wwpn, &pndl->nlp_portname,
		       sizeof (HBA_WWN));
		memcpy(&bind_array[next_index].wwnn, &pndl->nlp_nodename,
		       sizeof (HBA_WWN));
		bind_array[next_index].flags |= HBA_BIND_UNMAPPED;
		if (pndl->nlp_flag & NLP_TGT_NO_SCSIID)
			bind_array[next_index].flags |= HBA_BIND_NOSCSIID;
		if (pndl && pndl->nlp_Target) {
		    if(pndl->nlp_Target->targetFlags & FC_NPR_ACTIVE)
			bind_array[next_index].flags |= HBA_BIND_NODEVTMO;
		    if(pndl->nlp_Target->rptLunState == REPORT_LUN_COMPLETE)
			bind_array[next_index].flags |= HBA_BIND_RPTLUNST;
		}
		next_index++;
	}

	/* Iterate through the bind list */
	list_for_each(pos, &phba->fc_nlpbind_list) {
		pbdl = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);
	
		if (next_index >= max_index) {
			rc = ERANGE;
			*do_cp = 0;
			return (rc);
		}
		memset(&bind_array[next_index], 0, sizeof (HBA_BIND_ENTRY));
		bind_array[next_index].scsi_id = pbdl->nlp_sid;

		if (pbdl->nlp_bind_type & FCP_SEED_DID) {
			bind_array[next_index].bind_type = BIND_DID;
			bind_array[next_index].did = pbdl->nlp_DID;

		}

		if (pbdl->nlp_bind_type & FCP_SEED_WWPN) {
			bind_array[next_index].bind_type = BIND_WWPN;
			memcpy((uint8_t *) & bind_array[next_index].wwpn,
			       &pbdl->nlp_portname, sizeof (HBA_WWN));
		}

		if (pbdl->nlp_bind_type & FCP_SEED_WWNN) {
			bind_array[next_index].bind_type = BIND_WWNN;
			memcpy((uint8_t *) & bind_array[next_index].wwnn,
			       &pbdl->nlp_nodename, sizeof (HBA_WWN));
		}
		bind_array[next_index].flags |= HBA_BIND_BINDLIST;
		
		next_index++;
	}
	bind_list->NumberOfEntries = next_index;
	return 0;
}

int
lpfc_ioctl_get_vpd(lpfcHBA_t * phba,
		   LPFCCMDINPUT_t * cip, void *dataout, int *do_cp)
{
	struct vpd *dp;
	int rc = 0;

	dp = (struct vpd *) dataout;

	if (cip->lpfc_arg4 != VPD_VERSION1) {
		rc = EINVAL;
		*do_cp = 1;
	}

	dp->version = VPD_VERSION1;

	memset(dp->ModelDescription, 0, 256);
	memset(dp->Model, 0, 80);
	memset(dp->ProgramType, 0, 256);
	memset(dp->PortNum, 0, 20);

	if (phba->vpd_flag & VPD_MASK) {
		if (phba->vpd_flag & VPD_MODEL_DESC) {
			memcpy(dp->ModelDescription, phba->ModelDesc, 256);
		}
		if (phba->vpd_flag & VPD_MODEL_NAME) {
			memcpy(dp->Model, phba->ModelName, 80);
		}
		if (phba->vpd_flag & VPD_PROGRAM_TYPE) {
			memcpy(dp->ProgramType, phba->ProgramType, 256);
		}
		if (phba->vpd_flag & VPD_PORT) {
			memcpy(dp->PortNum, phba->Port, 20);
		}
	}

	return rc;
}

int
dfc_rsp_data_copy(lpfcHBA_t * phba,
		  uint8_t * outdataptr, DMABUFEXT_t * mlist, uint32_t size)
{
	DMABUFEXT_t *mlast = 0;
	int cnt, offset = 0;
	unsigned long iflag;
	struct list_head head, *curr, *next;

	if (!mlist)
		return(0);

	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, DMABUFEXT_t , dma.list);
		if (!size)
			break;

		/* We copy chucks of 4K */
		if (size > 4096)
			cnt = 4096;
		else
			cnt = size;

		if (outdataptr) {
			pci_dma_sync_single(phba->pcidev, mlast->dma.phys,
						LPFC_BPL_SIZE, PCI_DMA_TODEVICE);
			/* Copy data to user space */
			LPFC_DRVR_UNLOCK(phba, iflag);
			if (copy_to_user
			    ((uint8_t *) (outdataptr + offset),
			     (uint8_t *) mlast->dma.virt, (ulong) cnt)) {
				LPFC_DRVR_LOCK(phba, iflag);
				return (1);
			}
			LPFC_DRVR_LOCK(phba, iflag);
		}
		offset += cnt;
		size -= cnt;
	}
	list_del(&head);
	return (0);
}

DMABUFEXT_t *
dfc_cmd_data_alloc(lpfcHBA_t * phba,
		   char *indataptr, ULP_BDE64 * bpl, uint32_t size)
{
	DMABUFEXT_t *mlist = 0;
	DMABUFEXT_t *dmp;
	int cnt, offset = 0, i = 0;
	unsigned long iflag;
	struct pci_dev *pcidev;

	pcidev = phba->pcidev;

	while (size) {
		/* We get chucks of 4K */
		if (size > 4096)
			cnt = 4096;
		else
			cnt = size;

		/* allocate DMABUFEXT_t buffer header */
		dmp = kmalloc(sizeof (DMABUFEXT_t), GFP_ATOMIC);
		if ( dmp == 0 ) {
			goto out;
		}

		INIT_LIST_HEAD(&dmp->dma.list);

		/* Queue it to a linked list */
		if (mlist)
			list_add_tail(&dmp->dma.list, &mlist->dma.list);
		else
			mlist = dmp;

		/* allocate buffer */
		dmp->dma.virt = pci_alloc_consistent(pcidev, 
						     cnt, 
						     &(dmp->dma.phys));

		if (dmp->dma.virt == 0) {
			goto out;
		}
		dmp->size = cnt;

		if (indataptr) {
			/* Copy data from user space in */
			LPFC_DRVR_UNLOCK(phba, iflag);
			if (copy_from_user
			    ((uint8_t *) dmp->dma.virt,
			     (uint8_t *) (indataptr + offset), (ulong) cnt)) {
				LPFC_DRVR_LOCK(phba, iflag);
				goto out;
			}
			LPFC_DRVR_LOCK(phba, iflag);
			bpl->tus.f.bdeFlags = 0;

			pci_dma_sync_single(phba->pcidev, dmp->dma.phys, 
						LPFC_BPL_SIZE, PCI_DMA_TODEVICE);
		} else {
			bpl->tus.f.bdeFlags = BUFF_USE_RCV;
		}

		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu( putPaddrLow(dmp->dma.phys) );
		bpl->addrHigh = le32_to_cpu( putPaddrHigh(dmp->dma.phys) );
		bpl->tus.f.bdeSize = (ushort) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		offset += cnt;
		size -= cnt;
	}

	mlist->flag = i;
	return (mlist);
      out:
	dfc_cmd_data_free(phba, mlist);
	return (0);
}

int
dfc_cmd_data_free(lpfcHBA_t * phba, DMABUFEXT_t * mlist)
{
	DMABUFEXT_t *mlast;
	struct pci_dev *pcidev;
	struct list_head head, *curr, *next;

	if (!mlist)
		return(0);

	pcidev = phba->pcidev;
	list_add_tail(&head, &mlist->dma.list);

	list_for_each_safe(curr, next, &head) {
		mlast = list_entry(curr, DMABUFEXT_t , dma.list);
		if (mlast->dma.virt) {

			pci_free_consistent(pcidev, 
					    mlast->size, 
					    mlast->dma.virt, 
					    mlast->dma.phys);

		}
		kfree(mlast);
	}
	return (0);
}
