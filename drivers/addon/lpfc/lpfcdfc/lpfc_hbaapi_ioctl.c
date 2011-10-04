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
 * $Id: lpfc_hbaapi_ioctl.c 484 2006-03-27 16:26:51Z sf_support $
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

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_fcp.h"
#include "lpfc_scsi.h"
#include "lpfc_hw.h"
#include "lpfc_diag.h"
#include "lpfc_ioctl.h"
#include "lpfc_diag.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"
#include "hbaapi.h"
#include "lpfc_hbaapi_ioctl.h"

extern lpfcDRVR_t lpfcDRVR;
extern char *lpfc_release_version;

/*
 * For 64 bit platforms that run x86 code we need to thunk the 64 bit versions
 * of the data structures in the driver to the 32 bit versions used by the apps.
 * This only applies to data that has different sizes or packing on the two
 * platforms which is why we don't have 32 and 64 bit versions of all data
 * structures
 */
#ifdef CONFIG_X86_64
#pragma pack(4)
typedef struct HBA_FcpId32 {
	HBA_UINT32 FcId;
	HBA_WWN NodeWWN;
	HBA_WWN PortWWN;
	HBA_UINT64 FcpLun;
} HBA_FCPID32, *PHBA_FCPID32;

typedef struct HBA_FcpScsiEntry32 {
	HBA_SCSIID ScsiId;
	HBA_FCPID32 FcpId;
} HBA_FCPSCSIENTRY32, *PHBA_FCPSCSIENTRY32;

typedef struct HBA_FcpScsiEntryV232 {
	HBA_SCSIID ScsiId;
	HBA_FCPID32 FcpId;
	HBA_LUID LUID;
} HBA_FCPSCSIENTRYV232, *PHBA_FCPSCSIENTRYV232;

typedef struct HBA_FCPTargetMapping32 {
	HBA_UINT32 NumberOfEntries;
	HBA_FCPSCSIENTRY32 entry[1];	/* Variable length array
					 * containing mappings */
} HBA_FCPTARGETMAPPING32, *PHBA_FCPTARGETMAPPING32;

typedef struct HBA_FCPTargetMappingV232 {
	HBA_UINT32 NumberOfEntries;
	HBA_FCPSCSIENTRYV232 entry[1];	/* Variable length array
					 * containing mappings */
} HBA_FCPTARGETMAPPINGV232, *PHBA_FCPTARGETMAPPINGV232;

typedef struct HBA_FCPBindingEntry32 {
	HBA_FCPBINDINGTYPE type;
	HBA_SCSIID ScsiId;
	HBA_FCPID32 FcpId;	/* WWN valid only if type is
				 * to WWN, FcpLun always valid */
	HBA_UINT32 FcId;
} HBA_FCPBINDINGENTRY32, *PHBA_FCPBINDINGENTRY32;

typedef struct HBA_FCPBindingEntry232 {
	HBA_BIND_TYPE type;
	HBA_SCSIID ScsiId;
	HBA_FCPID32 FcpId;
	HBA_LUID LUID;
	HBA_STATUS status;
} HBA_FCPBINDINGENTRY232, *PHBA_FCPBINDINGENTRY232;

typedef struct HBA_FCPBinding32 {
	HBA_UINT32 NumberOfEntries;
	HBA_FCPBINDINGENTRY32 entry[1];	/* Variable length array */
} HBA_FCPBINDING32, *PHBA_FCPBINDING32;

typedef struct HBA_FcpBinding232 {
	HBA_UINT32 NumberOfEntries;
	HBA_FCPBINDINGENTRY232 entry[1];	/* Variable length array */
} HBA_FCPBINDING232, *PHBA_FCPBINDING232;
#pragma pack()
#endif /* CONFIG_X86_64 */

/* Routine Declaration - Local */

int
lpfc_process_ioctl_hbaapi(lpfcHBA_t *phba, LPFCCMDINPUT_t *cip)
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
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1602,	/* ptr to msg structure */
		lpfc_mes1602,			/* ptr to msg */
		lpfc_msgBlk1602.msgPreambleStr,	/* begin varargs */
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

	/* Diagnostic Interface Library Support - hbaapi */
	case LPFC_HBA_ADAPTERATTRIBUTES:
		rc = lpfc_ioctl_hba_adapterattributes(phba, cip, dataout);
		break;

	case LPFC_HBA_PORTATTRIBUTES:
		rc = lpfc_ioctl_hba_portattributes(phba, cip, dataout);
		break;

	case LPFC_HBA_PORTSTATISTICS:
		rc = lpfc_ioctl_hba_portstatistics(phba, cip, dataout);
		break;

	case LPFC_HBA_WWPNPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_wwpnportattributes(phba, cip, dataout);
		break;

	case LPFC_HBA_DISCPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_discportattributes(phba, cip, dataout);
		break;

	case LPFC_HBA_INDEXPORTATTRIBUTES:
		rc = lpfc_ioctl_hba_indexportattributes(phba, cip, dataout);
		break;

	case LPFC_HBA_SETMGMTINFO:
		rc = lpfc_ioctl_hba_setmgmtinfo(phba, cip);
		break;

	case LPFC_HBA_GETMGMTINFO:
		rc = lpfc_ioctl_hba_getmgmtinfo(phba, cip, dataout);
		break;

	case LPFC_HBA_REFRESHINFO:
		rc = lpfc_ioctl_hba_refreshinfo(phba, cip, dataout);
		break;

	case LPFC_HBA_RNID:
		rc = lpfc_ioctl_hba_rnid(phba, cip, dataout);
		break;

	case LPFC_HBA_GETEVENT:
		rc = lpfc_ioctl_hba_getevent(phba, cip, dataout);
		break;

	case LPFC_HBA_FCPTARGETMAPPING:
		rc = lpfc_ioctl_hba_fcptargetmapping(phba, cip, 
						     dataout, total_mem, &do_cp);
		break;

	case LPFC_HBA_FCPBINDING:
		rc = lpfc_ioctl_hba_fcpbinding(phba, cip, dataout, 
					       total_mem, &do_cp);
		break;
	}
	di->fc_refcnt--;

	/* dfc_ioctl exit */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk1603,	/* ptr to msg structure */
		lpfc_mes1603,			/* ptr to msg */
		lpfc_msgBlk1603.msgPreambleStr,	/* begin varargs */
		rc,
		cip->lpfc_outsz,
		(uint32_t) ((ulong) cip->lpfc_dataout));	/* end varargs */


	/* Copy data to user space config method */
	if (rc == 0 || do_cp == 1) {
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
lpfc_ioctl_hba_adapterattributes(lpfcHBA_t * phba,
				 LPFCCMDINPUT_t * cip, void *dataout)
{
	HBA_ADAPTERATTRIBUTES *ha;
	struct pci_dev *pdev;
	char *pNodeSymbolicName;
	char fwrev[32];
	uint32_t incr;
	lpfc_vpd_t *vp;
	int rc = 0;
	int i, j = 0;		/* loop index */

	/* Allocate mboxq structure */
	pNodeSymbolicName = kmalloc(256, GFP_ATOMIC);
	if (!pNodeSymbolicName)
		return(ENOMEM);

	pdev = phba->pcidev;
	vp = &phba->vpd;
	ha = (HBA_ADAPTERATTRIBUTES *) dataout;
	memset(dataout, 0, (sizeof (HBA_ADAPTERATTRIBUTES)));
	ha->NumberOfPorts = 1;
	ha->VendorSpecificID = 
	    ((((uint32_t) pdev->device) << 16) | (uint32_t) (pdev->vendor));
	ha->VendorSpecificID = le32_to_cpu(ha->VendorSpecificID);
	memcpy(ha->DriverVersion, lpfc_release_version, 8);
	lpfc_decode_firmware_rev(phba, fwrev, 1);
	memcpy(ha->FirmwareVersion, fwrev, 32);
	memcpy((uint8_t *) & ha->NodeWWN,
	       (uint8_t *) & phba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy(ha->Manufacturer, "Emulex Corporation", 20);
	memcpy(ha->Model, phba->ModelName, 80);
	memcpy(ha->ModelDescription, phba->ModelDesc, 256);
	memcpy(ha->DriverName, LPFC_DRIVER_NAME, 7);
	memcpy(ha->SerialNumber, phba->SerialNumber, 32);
	memcpy(ha->OptionROMVersion, phba->OptionROMVersion, 32);
	/* Convert JEDEC ID to ascii for hardware version */
	incr = vp->rev.biuRev;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			ha->HardwareVersion[7 - i] =
			    (char)((uint8_t) 0x30 + (uint8_t) j);
		else
			ha->HardwareVersion[7 - i] =
			    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		incr = (incr >> 4);
	}
	ha->HardwareVersion[8] = 0;

	lpfc_get_hba_sym_node_name(phba, (uint8_t *) pNodeSymbolicName);
	memcpy(ha->NodeSymbolicName, pNodeSymbolicName, 256);

	/* Free allocated block of memory */
	if (pNodeSymbolicName)
		kfree(pNodeSymbolicName);

	return (rc);
}

int
lpfc_ioctl_hba_portattributes(lpfcHBA_t * phba,
			      LPFCCMDINPUT_t * cip, void *dataout)
{

	HBA_PORTATTRIBUTES *hp;
	SERV_PARM *hsp;
	HBA_OSDN *osdn;
	lpfc_vpd_t *vp;
	uint32_t cnt;
	lpfcCfgParam_t *clp;
	int rc = 0;

	cnt = 0;
	clp = &phba->config[0];
	vp = &phba->vpd;
	hsp = (SERV_PARM *) (&phba->fc_sparam);
	hp = (HBA_PORTATTRIBUTES *) dataout;

	memset(dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	memcpy((uint8_t *) & hp->NodeWWN,
	       (uint8_t *) & phba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN,
	       (uint8_t *) & phba->fc_sparam.portName, sizeof (HBA_WWN));

	switch(phba->fc_linkspeed) {
	case LA_1GHZ_LINK:
		hp->PortSpeed = HBA_PORTSPEED_1GBIT;
		break;
	case LA_2GHZ_LINK:
		hp->PortSpeed = HBA_PORTSPEED_2GBIT;
		break;
	case LA_4GHZ_LINK:
		hp->PortSpeed = HBA_PORTSPEED_4GBIT;
		break;
	default:
		hp->PortSpeed = HBA_PORTSPEED_UNKNOWN;
		break;
	}

	if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
		hp->PortSupportedSpeed = HBA_PORTSPEED_10GBIT;
	else if ((FC_JEDEC_ID(vp->rev.biuRev) == HELIOS_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == ZEPHYR_JEDEC_ID))
		hp->PortSupportedSpeed = HBA_PORTSPEED_4GBIT;
	else if ((FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == PEGASUS_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == THOR_JEDEC_ID))
		hp->PortSupportedSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSupportedSpeed = HBA_PORTSPEED_1GBIT;

	hp->PortFcId = phba->fc_myDID;
	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (phba->fc_topology == TOPOLOGY_LOOP) {
		if (phba->fc_flag & FC_PUBLIC_LOOP) {
			hp->PortType = HBA_PORTTYPE_NLPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & phba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (phba->fc_flag & FC_FABRIC) {
			hp->PortType = HBA_PORTTYPE_NPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & phba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	if (phba->fc_flag & FC_BYPASSED_MODE) {
		hp->PortState = HBA_PORTSTATE_BYPASSED;
	} else if (phba->fc_flag & FC_OFFLINE_MODE) {
		hp->PortState = HBA_PORTSTATE_DIAGNOSTICS;
	} else {
		switch (phba->hba_state) {
		case LPFC_INIT_START:
		case LPFC_INIT_MBX_CMDS:
			hp->PortState = HBA_PORTSTATE_UNKNOWN;
			break;
		case LPFC_LINK_DOWN:
		case LPFC_LINK_UP:
		case LPFC_LOCAL_CFG_LINK:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
		case LPFC_CLEAR_LA:
			hp->PortState = HBA_PORTSTATE_LINKDOWN;
			break;
		case LPFC_HBA_READY:
			hp->PortState = HBA_PORTSTATE_ONLINE;
			break;
		case LPFC_HBA_ERROR:
		default:
			hp->PortState = HBA_PORTSTATE_ERROR;
			break;
		}
	}
	cnt = phba->fc_map_cnt + phba->fc_unmap_cnt;
	hp->NumberofDiscoveredPorts = cnt;
	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 1 */
	}
	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 2 */
	}
	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 8;	/* bit 3 */
	}
	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	hp->PortSupportedFc4Types.bits[2] = 0x1;
	hp->PortSupportedFc4Types.bits[3] = 0x20;
	hp->PortSupportedFc4Types.bits[7] = 0x1;
	hp->PortActiveFc4Types.bits[2] = 0x1;

	hp->PortActiveFc4Types.bits[7] = 0x1;

	/* OSDeviceName is the device info filled into the HBA_OSDN structure */
	osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
	memcpy(osdn->drvname, LPFC_DRIVER_NAME, 4);
	osdn->instance = phba->brd_no;
	osdn->target = (uint32_t) (-1);
	osdn->lun = (uint32_t) (-1);

	return (rc);
}

int
lpfc_ioctl_hba_portstatistics(lpfcHBA_t * phba,
			      LPFCCMDINPUT_t * cip, void *dataout)
{

	HBA_PORTSTATISTICS *hs;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	unsigned long iflag;
	int rc = 0;
	LPFC_SLI_t *psli = &phba->sli;

	if ((pmboxq = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		return ENOMEM;
	}

	pmb = &pmboxq->mb;

	hs = (HBA_PORTSTATISTICS *) dataout;
	memset(dataout, 0, (sizeof (HBA_PORTSTATISTICS)));
	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = (uint8_t *) 0;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))){
		LPFC_DRVR_UNLOCK(phba, iflag);
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
		LPFC_DRVR_LOCK(phba, iflag);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);
	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT) {
				/*
				 * Let SLI layer to release mboxq if mbox command completed after timeout.
				 */
				pmboxq->mbox_cmpl = 0;
			} else {
				lpfc_mbox_free(phba, pmboxq);
			}
		}
		rc = ENODEV;
		return (rc);
	}
	hs->TxFrames = pmb->un.varRdStatus.xmitFrameCnt;
	hs->RxFrames = pmb->un.varRdStatus.rcvFrameCnt;
	/* Convert KBytes to words */
	hs->TxWords = (pmb->un.varRdStatus.xmitByteCnt * 256);
	hs->RxWords = (pmb->un.varRdStatus.rcvByteCnt * 256);
	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = (uint8_t *) 0;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))){
		LPFC_DRVR_UNLOCK(phba, iflag);
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
		LPFC_DRVR_LOCK(phba, iflag);
	} else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);
	if (rc != MBX_SUCCESS) {
		if (pmboxq) {
			if (rc == MBX_TIMEOUT) {
				/*
				 * Let SLI layer to release mboxq if mbox command completed after timeout.
				 */
				pmboxq->mbox_cmpl = 0;
			} else {
				lpfc_mbox_free(phba, pmboxq);
			}
		}
		rc = ENODEV;
		return (rc);
	}
	hs->LinkFailureCount = pmb->un.varRdLnk.linkFailureCnt;
	hs->LossOfSyncCount = pmb->un.varRdLnk.lossSyncCnt;
	hs->LossOfSignalCount = pmb->un.varRdLnk.lossSignalCnt;
	hs->PrimitiveSeqProtocolErrCount = pmb->un.varRdLnk.primSeqErrCnt;
	hs->InvalidTxWordCount = pmb->un.varRdLnk.invalidXmitWord;
	hs->InvalidCRCCount = pmb->un.varRdLnk.crcCnt;
	hs->ErrorFrames = pmb->un.varRdLnk.crcCnt;

	if (phba->fc_topology == TOPOLOGY_LOOP) {
		hs->LIPCount = (phba->fc_eventTag >> 1);
		hs->NOSCount = -1;
	} else {
		hs->LIPCount = -1;
		hs->NOSCount = (phba->fc_eventTag >> 1);
	}

	hs->DumpedFrames = -1;

	hs->SecondsSinceLastReset = (jiffies - lpfcDRVR.loadtime) / HZ;

	/* Free allocated mboxq memory */
	if (pmboxq) {
		lpfc_mbox_free(phba, pmboxq);
	}

	return (rc);
}

int
lpfc_ioctl_hba_wwpnportattributes(lpfcHBA_t * phba,
				  LPFCCMDINPUT_t * cip, void *dataout)
{
	HBA_WWN findwwn;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos, *listp;
	struct list_head *node_list[2];
	HBA_PORTATTRIBUTES *hp;
	lpfc_vpd_t *vp;
	MAILBOX_t *pmbox;
	int rc = 0;
	unsigned long iflag;
	int i;

	/* Allocate mboxq structure */
	pmbox = kmalloc(sizeof (MAILBOX_t), GFP_ATOMIC);
	if (!pmbox)
		return(ENOMEM);

	hp = (HBA_PORTATTRIBUTES *) dataout;
	vp = &phba->vpd;
	memset(dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & findwwn, (uint8_t *) cip->lpfc_arg1,
			   (ulong) (sizeof (HBA_WWN)))) {
		LPFC_DRVR_LOCK(phba, iflag);
		rc = EIO;
		/* Free allocated mbox memory */
		if (pmbox)
			kfree((void *)pmbox);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);

	/* First Mapped ports, then unMapped ports */
	node_list[0] = &phba->fc_nlpmap_list;
	node_list[1] = &phba->fc_nlpunmap_list;
	for (i = 0; i < 2; i++) {
		listp = node_list[i];
		if (list_empty(listp)) 
			continue;

		list_for_each(pos, listp) {
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);	
			if (lpfc_geportname(&pndl->nlp_portname, 
						(NAME_TYPE *) &findwwn) == 2) {
				/* handle found port */
				rc = lpfc_ioctl_found_port(phba, pndl, dataout, pmbox, hp);
				/* Free allocated mbox memory */
				if (pmbox)
					kfree((void *)pmbox);
				return (rc);
			}
		}
	}

	/* Free allocated mbox memory */
	if (pmbox)
		kfree((void *)pmbox);

	rc = ERANGE;
	return (rc);
}

int
lpfc_ioctl_hba_discportattributes(lpfcHBA_t * phba,
				  LPFCCMDINPUT_t * cip, void *dataout)
{
	HBA_PORTATTRIBUTES *hp;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos, *listp;
	struct list_head *node_list[2];
	lpfc_vpd_t *vp;
	LPFC_SLI_t *psli;
	uint32_t refresh, offset, cnt;
	MAILBOX_t *pmbox;
	int rc = 0;
	int i;

	/* Allocate mboxq structure */
	pmbox = kmalloc(sizeof (MAILBOX_t), GFP_ATOMIC);
	if (!pmbox)
		return (ENOMEM);

	psli = &phba->sli;
	hp = (HBA_PORTATTRIBUTES *) dataout;
	vp = &phba->vpd;
	memset(dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	offset = (ulong) cip->lpfc_arg2;
	refresh = (ulong) cip->lpfc_arg3;
	if (refresh != phba->nport_event_cnt) {
		/* This is an error, need refresh, just return zero'ed out
		 * portattr and FcID as -1.
		 */
		hp->PortFcId = 0xffffffff;
		return (rc);
	}
	cnt = 0;

	/* First Mapped ports, then unMapped ports */
	node_list[0] = &phba->fc_nlpmap_list;
	node_list[1] = &phba->fc_nlpunmap_list;
	for (i = 0; i < 2; i++) {
		listp = node_list[i];
		if (list_empty(listp)) 
			continue;
		list_for_each(pos, listp) {
			pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);	
			if (cnt == offset) {
				/* handle found port */
				rc = lpfc_ioctl_found_port(phba, pndl, dataout, pmbox, hp);

				/* Free allocated mbox memory */
				if (pmbox)
					kfree((void *)pmbox);
				return (rc);
			}
		cnt++;
		}
	}

	rc = ERANGE;

	/* Free allocated mbox memory */
	if (pmbox)
		kfree((void *)pmbox);
	return (rc);
}

int
lpfc_ioctl_hba_indexportattributes(lpfcHBA_t * phba,
				   LPFCCMDINPUT_t * cip, void *dataout)
{
	HBA_PORTATTRIBUTES *hp;
	lpfc_vpd_t *vp;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos;
	uint32_t refresh, offset, cnt;
	MAILBOX_t *pmbox;
	int rc = 0;

	/* Allocate mboxq structure */
	pmbox = kmalloc(sizeof (MAILBOX_t), GFP_ATOMIC);
	if (!pmbox)
		return (ENOMEM);

	vp = &phba->vpd;
	hp = (HBA_PORTATTRIBUTES *) dataout;
	memset(dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	offset = (ulong) cip->lpfc_arg2;
	refresh = (ulong) cip->lpfc_arg3;
	if (refresh != phba->nport_event_cnt) {
		/* This is an error, need refresh, just return zero'ed out
		 * portattr and FcID as -1.
		 */
		hp->PortFcId = 0xffffffff;

		/* Free allocated mbox memory */
		if (pmbox)
			kfree((void *)pmbox);

		return (rc);
	}
	cnt = 0;
	/* Mapped NPorts only */
	list_for_each(pos, &phba->fc_nlpmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (cnt == offset) {
			/* handle found port */
			rc = lpfc_ioctl_found_port(phba, pndl, dataout, pmbox, hp);
			/* Free allocated mbox memory */
			if (pmbox)
				kfree((void *)pmbox);
			return (rc);
		}
		cnt++;
	}

	/* Free allocated mbox memory */
	if (pmbox)
		kfree((void *)pmbox);

	rc = ERANGE;
	return (rc);
}

int
lpfc_ioctl_hba_setmgmtinfo(lpfcHBA_t * phba,
			   LPFCCMDINPUT_t * cip)
{

	HBA_MGMTINFO *mgmtinfo;
	int rc = 0;
	unsigned long iflag;

	mgmtinfo = kmalloc(4096, GFP_ATOMIC);
	if (!mgmtinfo)
		return(ENOMEM);

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user
	    ((uint8_t *) mgmtinfo, (uint8_t *) cip->lpfc_arg1,
	     sizeof (HBA_MGMTINFO))) {
		LPFC_DRVR_LOCK(phba, iflag);
		rc = EIO;
		kfree(mgmtinfo);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);

	/* Can ONLY set UDP port and IP Address */
	phba->ipVersion = mgmtinfo->IPVersion;
	phba->UDPport = mgmtinfo->UDPPort;
	if (phba->ipVersion == RNID_IPV4) {
		memcpy((uint8_t *) & phba->ipAddr[0],
		       (uint8_t *) & mgmtinfo->IPAddress[0], 4);
	} else {
		memcpy((uint8_t *) & phba->ipAddr[0],
		       (uint8_t *) & mgmtinfo->IPAddress[0], 16);
	}

	kfree(mgmtinfo);
	return (rc);
}

int
lpfc_ioctl_hba_getmgmtinfo(lpfcHBA_t * phba,
			   LPFCCMDINPUT_t * cip, void *dataout)
{

	HBA_MGMTINFO *mgmtinfo;
	int rc = 0;

	mgmtinfo = (HBA_MGMTINFO *) dataout;
	memcpy((uint8_t *) & mgmtinfo->wwn, (uint8_t *) & phba->fc_nodename, 8);
	mgmtinfo->unittype = RNID_HBA;
	mgmtinfo->PortId = phba->fc_myDID;
	mgmtinfo->NumberOfAttachedNodes = 0;
	mgmtinfo->TopologyDiscoveryFlags = 0;
	mgmtinfo->IPVersion = phba->ipVersion;
	mgmtinfo->UDPPort = phba->UDPport;
	if (phba->ipVersion == RNID_IPV4) {
		memcpy((void *)&mgmtinfo->IPAddress[0],
		       (void *)&phba->ipAddr[0], 4);
	} else {
		memcpy((void *)&mgmtinfo->IPAddress[0],
		       (void *)&phba->ipAddr[0], 16);
	}

	return (rc);
}

int
lpfc_ioctl_hba_refreshinfo(lpfcHBA_t * phba,
			   LPFCCMDINPUT_t * cip, void *dataout)
{
	uint32_t *lptr;
	int rc = 0;

	lptr = (uint32_t *) dataout;
	*lptr = phba->nport_event_cnt;

	return (rc);
}

int
lpfc_ioctl_hba_rnid(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip, void *dataout)
{

	HBA_WWN idn;
	LPFC_SLI_t *psli;
	LPFC_IOCBQ_t *cmdiocbq = 0;
	LPFC_IOCBQ_t *rspiocbq = 0;
	RNID *prsp;
	uint32_t *pcmd;
	uint32_t *psta;
	IOCB_t *rsp;
	LPFC_SLI_RING_t *pring;
	void *context2;		/* both prep_iocb and iocb_wait use this */
	int i0;
	uint16_t siz;
	unsigned long iflag;
	int rtnbfrsiz;
	LPFC_NODELIST_t *pndl;
	int rc = 0;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];
	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_from_user((uint8_t *) & idn, (uint8_t *) cip->lpfc_arg1,
			   (ulong) (sizeof (HBA_WWN)))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);

	if (cip->lpfc_flag == NODE_WWN) {
		pndl =
		    lpfc_findnode_wwnn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & idn);
	} else {
		pndl =
		    lpfc_findnode_wwpn(phba,
				       NLP_SEARCH_MAPPED | NLP_SEARCH_UNMAPPED,
				       (NAME_TYPE *) & idn);
	}
	if (!pndl) {
		rc = ENODEV;
		goto sndrndqwt;
	}
	for (i0 = 0;
	     i0 < 10 && (pndl->nlp_flag & NLP_ELS_SND_MASK) == NLP_RNID_SND;
	     i0++) {
		iflag = phba->iflag;
		LPFC_DRVR_UNLOCK(phba, iflag);
		mdelay(1000);
		LPFC_DRVR_LOCK(phba, iflag);
	}
	if (i0 == 10) {
		rc = EACCES;
		pndl->nlp_flag &= ~NLP_RNID_SND;
		goto sndrndqwt;
	}

	siz = 2 * sizeof (uint32_t);
	/*  lpfc_prep_els_iocb sets the following: */

	if (!
	    (cmdiocbq =
	     lpfc_prep_els_iocb(phba, 1, siz, 0, pndl, ELS_CMD_RNID))) {
		rc = ENOMEM;
		goto sndrndqwt;
	}
    /************************************************************************/
	/*  context2 is used by prep/free to locate cmd and rsp buffers,   */
	/*  but context2 is also used by iocb_wait to hold a rspiocb ptr, so    */
	/*  the rsp iocbq can be returned from the completion routine for       */
	/*  iocb_wait, so, save the prep/free value locally ... it will be      */
	/*  restored after returning from iocb_wait.                            */
    /************************************************************************/
	context2 = cmdiocbq->context2;	/* needed to use lpfc_els_free_iocb */
	if ((rspiocbq = lpfc_iocb_alloc(phba, MEM_PRI)) == 0) {
		rc = ENOMEM;
		goto sndrndqwt;
	}
	memset((void *)rspiocbq, 0, sizeof (LPFC_IOCBQ_t));
	rsp = &(rspiocbq->iocb);

	pcmd = (uint32_t *) (((DMABUF_t *) cmdiocbq->context2)->virt);
	*pcmd++ = ELS_CMD_RNID;
	memset((void *)pcmd, 0, sizeof (RNID));	/* fill in RNID payload */
	((RNID *) pcmd)->Format = 0;	/* following makes it more interesting */
	((RNID *) pcmd)->Format = RNID_TOPOLOGY_DISC;
	cmdiocbq->context1 = (uint8_t *) 0;
	cmdiocbq->context2 = (uint8_t *) 0;
	cmdiocbq->iocb_flag |= LPFC_IO_LIBDFC;
	for (rc = -1, i0 = 0; i0 < 4 && rc != IOCB_SUCCESS; i0++) {
		pndl->nlp_flag |= NLP_RNID_SND;
		rc = lpfc_sli_issue_iocb_wait(phba, pring, cmdiocbq,
					      SLI_IOCB_USE_TXQ, rspiocbq,
					      (phba->fc_ratov * 2) +
					      LPFC_DRVR_TIMEOUT);
		pndl->nlp_flag &= ~NLP_RNID_SND;
		cmdiocbq->context2 = context2;
		if (rc == IOCB_ERROR) {
			rc = EACCES;
			goto sndrndqwt;
		}
	}
	if (rc != IOCB_SUCCESS) {
		goto sndrndqwt;
	}

	if (rsp->ulpStatus) {
		rc = EACCES;
	} else {
		struct lpfc_dmabuf *buf_ptr1, *buf_ptr;
		buf_ptr1 = (struct lpfc_dmabuf *)(cmdiocbq->context2);
                buf_ptr = list_entry(buf_ptr1->list.next, struct lpfc_dmabuf,
                                                                        list);
                psta = (uint32_t*)buf_ptr->virt;

		prsp = (RNID *) (psta + 1);	/*  then rnid response data */
		if (*psta != ELS_CMD_ACC) {
			rc = EFAULT;
			goto sndrndqwt;
		}
		rtnbfrsiz = prsp->CommonLen + prsp->SpecificLen;
		memcpy((uint8_t *) dataout, (uint8_t *) prsp, rtnbfrsiz);
		LPFC_DRVR_UNLOCK(phba, iflag);
		if (copy_to_user
		    ((uint8_t *) cip->lpfc_arg2, (uint8_t *) & rtnbfrsiz,
		     sizeof (int)))
			rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
	}
      sndrndqwt:
	if (cmdiocbq)
		lpfc_els_free_iocb(phba, cmdiocbq);

	return (rc);
}

int
lpfc_ioctl_hba_getevent(lpfcHBA_t * phba,
			LPFCCMDINPUT_t * cip, void *dataout)
{

	uint32_t outsize, size;
	HBAEVT_t *rec;
	HBAEVT_t *recout;
	int j, rc = 0;
	unsigned long iflag;

	size = (ulong) cip->lpfc_arg1;	/* size is number of event entries */

	recout = (HBAEVT_t *) dataout;
	for (j = 0; j < MAX_HBAEVT; j++) {
		if ((j == (int)size) ||
		    (phba->hba_event_get == phba->hba_event_put))
			break;
		rec = &phba->hbaevt[phba->hba_event_get];
		memcpy((uint8_t *) recout, (uint8_t *) rec, sizeof (HBAEVT_t));
		recout++;
		phba->hba_event_get++;
		if (phba->hba_event_get >= MAX_HBAEVT) {
			phba->hba_event_get = 0;
		}
	}
	outsize = j;

	LPFC_DRVR_UNLOCK(phba, iflag);
	/* copy back size of response */
	if (copy_to_user((uint8_t *) cip->lpfc_arg2, (uint8_t *) & outsize,
			 sizeof (uint32_t))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		return (rc);
	}

	/* copy back number of missed records */
	if (copy_to_user
	    ((uint8_t *) cip->lpfc_arg3, (uint8_t *) & phba->hba_event_missed,
	     sizeof (uint32_t))) {
		rc = EIO;
		LPFC_DRVR_LOCK(phba, iflag);
		return (rc);
	}
	LPFC_DRVR_LOCK(phba, iflag);

	phba->hba_event_missed = 0;
	cip->lpfc_outsz = (uint32_t) (outsize * sizeof (HBA_EVENTINFO));

	return (rc);
}

int
lpfc_ioctl_hba_fcptargetmapping(lpfcHBA_t * phba,
				LPFCCMDINPUT_t * cip,
				void *dataout, int buff_size, int *do_cp)
{

	uint32_t room = (ulong) cip->lpfc_arg1;
	uint32_t total = 0;
	int count = 0;
	int size = 0;
	int minsize;
	int pansid;
	union {
		char* p;
		HBA_FCPSCSIENTRY *se;
#ifdef CONFIG_X86_64
		HBA_FCPSCSIENTRY32 *se32;
#endif /* CONFIG_X86_64 */
	} ep;
	char *appPtr;
	char *drvPtr;
	LPFCSCSILUN_t *plun;
	LPFCSCSITARGET_t *ptarget;
	LPFC_NODELIST_t *pndl;
	struct list_head *pos;
	int rc = 0;
	struct pci_dev *pcidev;
	struct list_head *curr, *next;
	unsigned long iflag;

#ifdef CONFIG_X86_64
	if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
		appPtr = ((char *) cip->lpfc_dataout) + offsetof(HBA_FCPTARGETMAPPING32, entry);
		drvPtr = (char *)&((HBA_FCPTARGETMAPPING32 *) dataout)->entry[0];
		minsize = sizeof(HBA_FCPSCSIENTRY32);
	} else {
#endif /* CONFIG_X86_64 */
		appPtr = ((char *) cip->lpfc_dataout) + offsetof(HBA_FCPTARGETMAPPING, entry);
		drvPtr = (char *)&((HBA_FCPTARGETMAPPING *) dataout)->entry[0];
		minsize = sizeof(HBA_FCPSCSIENTRY);
#ifdef CONFIG_X86_64
	}
#endif /* CONFIG_X86_64 */

	ep.p = drvPtr;

	pcidev = phba->pcidev;

	/* Mapped ports only */
	list_for_each(pos, &phba->fc_nlpmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		pansid = pndl->nlp_sid;
		if (pansid > MAX_FCP_TARGET) {
			continue;
		}
		ptarget = phba->device_queue_hash[pansid];
		if (!ptarget) {
			continue;
		}

		list_for_each_safe(curr, next, &ptarget->lunlist) {
			plun = list_entry(curr, LPFCSCSILUN_t, list);

			if (!(plun->failMask) && !(plun->lunFlag && LUN_BLOCKED)) {
				if (count < room) {
					HBA_OSDN *osdn;
					uint32_t fcpLun[2];

					if ((buff_size - size) < minsize) {
						/*  Not enuough space left; we have to 
						 *  copy what we have now back to 
						 *  application space, before reusing
						 *  this buffer again.
						 */
						LPFC_DRVR_UNLOCK(phba, iflag);
						if ( copy_to_user(appPtr,
								  drvPtr,
								  size)) {
							LPFC_DRVR_LOCK(phba, iflag);
							return(EIO);
						}
						LPFC_DRVR_LOCK(phba, iflag);
						appPtr += size;
						ep.p = drvPtr;
						size = 0;
					}

#ifdef CONFIG_X86_64
					if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
						memset((void *)ep.se32->ScsiId.OSDeviceName,
						       0, 256);
						/* OSDeviceName is device info filled into HBA_OSDN */
						osdn =
							(HBA_OSDN *) & ep.se32->ScsiId.
							OSDeviceName[0];
						memcpy(osdn->drvname, LPFC_DRIVER_NAME,
						       4);
						osdn->instance = phba->brd_no;
						osdn->target = pansid;
						osdn->lun = (uint32_t) (plun->lun_id);
						osdn->flags = 0;
						ep.se32->ScsiId.ScsiTargetNumber = pansid;
						ep.se32->ScsiId.ScsiOSLun =
							(uint32_t) (plun->lun_id);
						ep.se32->ScsiId.ScsiBusNumber = 0;
						ep.se32->FcpId.FcId = pndl->nlp_DID;
						memset((char *)fcpLun, 0,
						       sizeof (HBA_UINT64));
						fcpLun[0] = (plun->lun_id << FC_LUN_SHIFT);
						if (ptarget->addrMode ==
						    VOLUME_SET_ADDRESSING) {
							fcpLun[0] |=
								be32_to_cpu(0x40000000);
						}
						memcpy((char *)&ep.se32->FcpId.FcpLun,
						       (char *)&fcpLun[0],
						       sizeof (HBA_UINT64));
						memcpy((uint8_t *) & ep.se32->FcpId.PortWWN,
						       &pndl->nlp_portname,
						       sizeof (HBA_WWN));
						memcpy((uint8_t *) & ep.se32->FcpId.NodeWWN,
						       &pndl->nlp_nodename,
						       sizeof (HBA_WWN));
						count++;
						ep.se32++;
						size += sizeof(HBA_FCPSCSIENTRY32);
					} else {
#endif /* CONFIG_X86_64 */
						memset((void *)ep.se->ScsiId.OSDeviceName,
						       0, 256);
						/* OSDeviceName is device info filled into HBA_OSDN */
						osdn =
							(HBA_OSDN *) & ep.se->ScsiId.
							OSDeviceName[0];
						memcpy(osdn->drvname, LPFC_DRIVER_NAME,
						       4);
						osdn->instance = phba->brd_no;
						osdn->target = pansid;
						osdn->lun = (uint32_t) (plun->lun_id);
						osdn->flags = 0;
						ep.se->ScsiId.ScsiTargetNumber = pansid;
						ep.se->ScsiId.ScsiOSLun =
							(uint32_t) (plun->lun_id);
						ep.se->ScsiId.ScsiBusNumber = 0;
						ep.se->FcpId.FcId = pndl->nlp_DID;
						memset((char *)fcpLun, 0,
						       sizeof (HBA_UINT64));
						fcpLun[0] = (plun->lun_id << FC_LUN_SHIFT);
						if (ptarget->addrMode ==
						    VOLUME_SET_ADDRESSING) {
							fcpLun[0] |=
								be32_to_cpu(0x40000000);
						}
						memcpy((char *)&ep.se->FcpId.FcpLun,
						       (char *)&fcpLun[0],
						       sizeof (HBA_UINT64));
						memcpy((uint8_t *) & ep.se->FcpId.PortWWN,
						       &pndl->nlp_portname,
						       sizeof (HBA_WWN));
						memcpy((uint8_t *) & ep.se->FcpId.NodeWWN,
						       &pndl->nlp_nodename,
						       sizeof (HBA_WWN));
						count++;
						ep.se++;
						size += sizeof(HBA_FCPSCSIENTRY);
#ifdef CONFIG_X86_64
					}
#endif /* CONFIG_X86_64 */
				}
				total++;
			}
		}
	}

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_to_user(appPtr, drvPtr, size)) {
		LPFC_DRVR_LOCK(phba, iflag);
		return(EIO);
	}
	LPFC_DRVR_LOCK(phba, iflag);

#ifdef CONFIG_X86_64
	if (cip->lpfc_cntl == LPFC_CNTL_X86_APP)
		((HBA_FCPTARGETMAPPING32 *) dataout)->NumberOfEntries = total;
	else
#endif /* CONFIG_X86_64 */
		((HBA_FCPTARGETMAPPING *) dataout)->NumberOfEntries = total;

	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_to_user(cip->lpfc_dataout, &total, sizeof(HBA_UINT32))) {
		LPFC_DRVR_LOCK(phba, iflag);
		return(EIO);
	}
	LPFC_DRVR_LOCK(phba, iflag);
	cip->lpfc_outsz = 0;
	if (total > room) {
		rc = ERANGE;
		*do_cp = 1;
	}

	return (rc);
}

int
lpfc_ioctl_hba_fcpbinding(lpfcHBA_t * phba,
			  LPFCCMDINPUT_t * cip, void *dataout, int size, int *do_cp)
{
	struct list_head *pos;
	uint32_t room;
	uint32_t total = 0;
	uint32_t pansid;
	uint32_t fcpLun[2];
	int memsz = 0;
	int cnt = 0;
	int rc = 0;
	char *appPtr;
	char *drvPtr;
	union {
		char* p;
		HBA_FCPBINDINGENTRY *se;
#ifdef CONFIG_X86_64
		HBA_FCPBINDINGENTRY32 *se32;
#endif /* CONFIG_X86_64 */
	} ep;
	LPFCSCSILUN_t *plun;
	LPFCSCSITARGET_t *ptarget;
	LPFC_BINDLIST_t *pbdl;
	LPFC_NODELIST_t *pndl;
	HBA_OSDN *osdn;
	uint32_t total_mem = size;
	int minsize;
	unsigned long iflag;
	struct pci_dev *pcidev;
	struct list_head *curr, *next;

#ifdef CONFIG_X86_64
	if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
		appPtr = ((char *) cip->lpfc_dataout) + offsetof(HBA_FCPBINDING32, entry);
		drvPtr = (char *)&((HBA_FCPBINDING32 *) dataout)->entry[0];
		minsize = sizeof(HBA_FCPBINDINGENTRY32);
	} else {
#endif /* CONFIG_X86_64 */
		appPtr = ((char *) cip->lpfc_dataout) + offsetof(HBA_FCPBINDING, entry);
		drvPtr = (char *)&((HBA_FCPBINDING *) dataout)->entry[0];
		minsize = sizeof(HBA_FCPBINDINGENTRY);
#ifdef CONFIG_X86_64
	}
#endif /* CONFIG_X86_64 */

	ep.p = drvPtr;

	pcidev = phba->pcidev;

	room = (uint32_t)((ulong)cip->lpfc_arg1);

	/* First Mapped ports */
	list_for_each(pos, &phba->fc_nlpmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (pndl->nlp_flag & NLP_SEED_MASK) {
			pansid = pndl->nlp_sid;
			if (pansid > MAX_FCP_TARGET) {
				continue;
			}
			ptarget = phba->device_queue_hash[pansid];
			if (!ptarget) {
				continue;
			}

			list_for_each_safe(curr, next, &ptarget->lunlist) {
				plun = list_entry(curr, LPFCSCSILUN_t, list);

				if (!(plun->failMask) && !(plun->lunFlag && LUN_BLOCKED)) {
					if (cnt < room) {
						/* if not enuf space left then we have to copy what we 
						 *  have now back to application space, before reusing 
						 *  this buffer again.
						 */
						if (total_mem - memsz < minsize) {
							LPFC_DRVR_UNLOCK(phba, iflag);
							if (copy_to_user
							    ((uint8_t *) appPtr,
							     (uint8_t *) drvPtr,
							     memsz)) {
								LPFC_DRVR_LOCK(phba,
									       iflag);
								return EIO;
							}
							LPFC_DRVR_LOCK(phba, iflag);
							appPtr = appPtr + memsz;
							ep.p = drvPtr;
							memsz = 0;
						}
#ifdef CONFIG_X86_64
						if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
							memset((void *)ep.se32->ScsiId.OSDeviceName,
							       0, 256);
							/* OSDeviceName is device info filled into HBA_OSDN */
							osdn =
								(HBA_OSDN *) & ep.se32->ScsiId.
								OSDeviceName[0];
							memcpy(osdn->drvname, LPFC_DRIVER_NAME,
							       4);
							osdn->instance = phba->brd_no;
							osdn->target = pansid;
							osdn->lun = (uint32_t) (plun->lun_id);
							ep.se32->ScsiId.ScsiTargetNumber =
								pansid;
							ep.se32->ScsiId.ScsiOSLun =
								(uint32_t) (plun->lun_id);
							ep.se32->ScsiId.ScsiBusNumber = 0;
							memset((char *)fcpLun, 0,
							       sizeof (HBA_UINT64));
							fcpLun[0] =
								(plun->lun_id << FC_LUN_SHIFT);
							if (ptarget->addrMode ==
							    VOLUME_SET_ADDRESSING) {
								fcpLun[0] |=
									be32_to_cpu(0x40000000);
							}
							memcpy((char *)&ep.se32->FcpId.FcpLun,
							       (char *)&fcpLun[0],
							       sizeof (HBA_UINT64));

							if (pndl->nlp_flag & NLP_SEED_DID) {
								ep.se32->type = TO_D_ID;
								ep.se32->FcpId.FcId = pndl->nlp_DID;
								ep.se32->FcId = pndl->nlp_DID;
								memset((uint8_t *) & ep.se32->FcpId.
								       PortWWN, 0,
								       sizeof (HBA_WWN));
								memset((uint8_t *) & ep.se32->FcpId.
								       NodeWWN, 0,
								       sizeof (HBA_WWN));
							} else {
								ep.se32->type = TO_WWN;
								ep.se32->FcId = 0;
								ep.se32->FcpId.FcId = 0;
								if (pndl->
								    nlp_flag & NLP_SEED_WWPN) {
									memcpy((uint8_t *) &
									       ep.se32->FcpId.
									       PortWWN,
									       &pndl->
									       nlp_portname,
									       sizeof
									       (HBA_WWN));
								} else {
									memcpy((uint8_t *) &
									       ep.se32->FcpId.
									       NodeWWN,
									       &pndl->
									       nlp_nodename,
									       sizeof
									       (HBA_WWN));
								}
							}
							ep.se32->FcpId.FcId = pndl->nlp_DID;
							memcpy((uint8_t *) & ep.se32->FcpId.PortWWN,
							       &pndl->nlp_portname,
							       sizeof (HBA_WWN));
							memcpy((uint8_t *) & ep.se32->FcpId.NodeWWN,
							       &pndl->nlp_nodename,
							       sizeof (HBA_WWN));
							ep.se32++;
							cnt++;
							memsz += minsize;
						} else {
#endif /* CONFIG_X86_64 */
							memset((void *)ep.se->ScsiId.OSDeviceName,
							       0, 256);
							/* OSDeviceName is device info filled into HBA_OSDN */
							osdn =
								(HBA_OSDN *) & ep.se->ScsiId.
								OSDeviceName[0];
							memcpy(osdn->drvname, LPFC_DRIVER_NAME,
							       4);
							osdn->instance = phba->brd_no;
							osdn->target = pansid;
							osdn->lun = (uint32_t) (plun->lun_id);
							ep.se->ScsiId.ScsiTargetNumber =
								pansid;
							ep.se->ScsiId.ScsiOSLun =
								(uint32_t) (plun->lun_id);
							ep.se->ScsiId.ScsiBusNumber = 0;
							memset((char *)fcpLun, 0,
							       sizeof (HBA_UINT64));
							fcpLun[0] =
								(plun->lun_id << FC_LUN_SHIFT);
							if (ptarget->addrMode ==
							    VOLUME_SET_ADDRESSING) {
								fcpLun[0] |=
									be32_to_cpu(0x40000000);
							}
							memcpy((char *)&ep.se->FcpId.FcpLun,
							       (char *)&fcpLun[0],
							       sizeof (HBA_UINT64));

							if (pndl->nlp_flag & NLP_SEED_DID) {
								ep.se->type = TO_D_ID;
								ep.se->FcpId.FcId = pndl->nlp_DID;
								ep.se->FcId = pndl->nlp_DID;
								memset((uint8_t *) & ep.se->FcpId.
								       PortWWN, 0,
								       sizeof (HBA_WWN));
								memset((uint8_t *) & ep.se->FcpId.
								       NodeWWN, 0,
								       sizeof (HBA_WWN));
							} else {
								ep.se->type = TO_WWN;
								ep.se->FcId = 0;
								ep.se->FcpId.FcId = 0;
								if (pndl->
								    nlp_flag & NLP_SEED_WWPN) {
									memcpy((uint8_t *) &
									       ep.se->FcpId.
									       PortWWN,
									       &pndl->
									       nlp_portname,
									       sizeof
									       (HBA_WWN));
								} else {
									memcpy((uint8_t *) &
									       ep.se->FcpId.
									       NodeWWN,
									       &pndl->
									       nlp_nodename,
									       sizeof
									       (HBA_WWN));
								}
							}
							ep.se->FcpId.FcId = pndl->nlp_DID;
							memcpy((uint8_t *) & ep.se->FcpId.PortWWN,
							       &pndl->nlp_portname,
							       sizeof (HBA_WWN));
							memcpy((uint8_t *) & ep.se->FcpId.NodeWWN,
							       &pndl->nlp_nodename,
							       sizeof (HBA_WWN));
							ep.se++;
							cnt++;
							memsz += minsize;
#ifdef CONFIG_X86_64
						}
#endif /* CONFIG_X86_64 */
					}
					total++;
				}
			}
		}
	}			/* end searching mapped list */

	/* then unmapped ports */
	list_for_each(pos, &phba->fc_nlpunmap_list) {
		pndl = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
		if (pndl->nlp_flag & NLP_SEED_MASK) {
			pansid = pndl->nlp_sid;
			if (pansid > MAX_FCP_TARGET) {
				continue;
			}
			ptarget = phba->device_queue_hash[pansid];
			if (!ptarget) {
				continue;
			}

			list_for_each_safe(curr, next, &ptarget->lunlist) {
				plun = list_entry(curr, LPFCSCSILUN_t , list);

				if (cnt < room) {
					/* if not enuf space left then we have to copy what we 
					 *  have now back to application space, before reusing 
					 *  this buffer again.
					 */
					if (total_mem - memsz < minsize) {
						LPFC_DRVR_UNLOCK(phba, iflag);
						if (copy_to_user
						    ((uint8_t *) appPtr,
						     (uint8_t *) drvPtr,
						     memsz)) {
							LPFC_DRVR_LOCK(phba,
								      iflag);
							return EIO;
						}
						LPFC_DRVR_LOCK(phba, iflag);
						appPtr = appPtr + memsz;
						ep.p = drvPtr;
						memsz = 0;
					}
#ifdef CONFIG_X86_64
					if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
						memset((void *)ep.se32->ScsiId.OSDeviceName,
						       0, 256);
						/* OSDeviceName is device info filled into HBA_OSDN */
						osdn =
							(HBA_OSDN *) & ep.se32->ScsiId.
							OSDeviceName[0];
						memcpy(osdn->drvname, LPFC_DRIVER_NAME,
						       4);
						osdn->instance = phba->brd_no;
						osdn->target = pansid;
						osdn->lun = (uint32_t) (plun->lun_id);
						ep.se32->ScsiId.ScsiTargetNumber =
							pansid;
						ep.se32->ScsiId.ScsiOSLun =
							(uint32_t) (plun->lun_id);
						ep.se32->ScsiId.ScsiBusNumber = 0;
						memset((char *)fcpLun, 0,
						       sizeof (HBA_UINT64));
						fcpLun[0] =
							(plun->lun_id << FC_LUN_SHIFT);
						if (ptarget->addrMode ==
						    VOLUME_SET_ADDRESSING) {
							fcpLun[0] |=
								be32_to_cpu(0x40000000);
						}
						memcpy((char *)&ep.se32->FcpId.FcpLun,
						       (char *)&fcpLun[0],
						       sizeof (HBA_UINT64));
						
						if (pndl->nlp_flag & NLP_SEED_DID) {
							ep.se32->type = TO_D_ID;
							ep.se32->FcpId.FcId = pndl->nlp_DID;
							ep.se32->FcId = pndl->nlp_DID;
							memset((uint8_t *) & ep.se32->FcpId.
							       PortWWN, 0,
							       sizeof (HBA_WWN));
							memset((uint8_t *) & ep.se32->FcpId.
							       NodeWWN, 0,
							       sizeof (HBA_WWN));
						} else {
							ep.se32->type = TO_WWN;
							ep.se32->FcId = 0;
							ep.se32->FcpId.FcId = 0;
							if (pndl->
							    nlp_flag & NLP_SEED_WWPN) {
								memcpy((uint8_t *) &
								       ep.se32->FcpId.
								       PortWWN,
								       &pndl->
								       nlp_portname,
								       sizeof
								       (HBA_WWN));
							} else {
								memcpy((uint8_t *) &
								       ep.se32->FcpId.
								       NodeWWN,
								       &pndl->
								       nlp_nodename,
								       sizeof
								       (HBA_WWN));
							}
						}
						ep.se32->FcpId.FcId = pndl->nlp_DID;
						memcpy((uint8_t *) & ep.se32->FcpId.PortWWN,
						       &pndl->nlp_portname,
						       sizeof (HBA_WWN));
						memcpy((uint8_t *) & ep.se32->FcpId.NodeWWN,
						       &pndl->nlp_nodename,
						       sizeof (HBA_WWN));
						ep.se32++;
						cnt++;
						memsz += minsize;
					} else {
#endif /* CONFIG_X86_64 */
						memset((void *)ep.se->ScsiId.OSDeviceName,
						       0, 256);
						/* OSDeviceName is device info filled into HBA_OSDN */
						osdn =
							(HBA_OSDN *) & ep.se->ScsiId.
							OSDeviceName[0];
						memcpy(osdn->drvname, LPFC_DRIVER_NAME,
						       4);
						osdn->instance = phba->brd_no;
						osdn->target = pansid;
						osdn->lun = (uint32_t) (plun->lun_id);
						ep.se->ScsiId.ScsiTargetNumber =
							pansid;
						ep.se->ScsiId.ScsiOSLun =
							(uint32_t) (plun->lun_id);
						ep.se->ScsiId.ScsiBusNumber = 0;
						memset((char *)fcpLun, 0,
						       sizeof (HBA_UINT64));
						fcpLun[0] =
							(plun->lun_id << FC_LUN_SHIFT);
						if (ptarget->addrMode ==
						    VOLUME_SET_ADDRESSING) {
							fcpLun[0] |=
								be32_to_cpu(0x40000000);
						}
						memcpy((char *)&ep.se->FcpId.FcpLun,
						       (char *)&fcpLun[0],
						       sizeof (HBA_UINT64));
						
						if (pndl->nlp_flag & NLP_SEED_DID) {
							ep.se->type = TO_D_ID;
							ep.se->FcpId.FcId = pndl->nlp_DID;
							ep.se->FcId = pndl->nlp_DID;
							memset((uint8_t *) & ep.se->FcpId.
							       PortWWN, 0,
							       sizeof (HBA_WWN));
							memset((uint8_t *) & ep.se->FcpId.
							       NodeWWN, 0,
							       sizeof (HBA_WWN));
						} else {
							ep.se->type = TO_WWN;
							ep.se->FcId = 0;
							ep.se->FcpId.FcId = 0;
							if (pndl->
							    nlp_flag & NLP_SEED_WWPN) {
								memcpy((uint8_t *) &
								       ep.se->FcpId.
								       PortWWN,
								       &pndl->
								       nlp_portname,
								       sizeof
								       (HBA_WWN));
							} else {
								memcpy((uint8_t *) &
								       ep.se->FcpId.
								       NodeWWN,
								       &pndl->
								       nlp_nodename,
								       sizeof
								       (HBA_WWN));
							}
						}
						ep.se->FcpId.FcId = pndl->nlp_DID;
						memcpy((uint8_t *) & ep.se->FcpId.PortWWN,
						       &pndl->nlp_portname,
						       sizeof (HBA_WWN));
						memcpy((uint8_t *) & ep.se->FcpId.NodeWWN,
						       &pndl->nlp_nodename,
						       sizeof (HBA_WWN));
						ep.se++;
						cnt++;
						memsz += minsize;
#ifdef CONFIG_X86_64
					}
#endif /* CONFIG_X86_64 */
				}
				total++;
			}
		}
	}			/* end searching unmapped list */

	/* search binding list */
	list_for_each(pos, &phba->fc_nlpbind_list) {
		pbdl = list_entry(pos, LPFC_BINDLIST_t, nlp_listp);
		if (pbdl->nlp_bind_type & FCP_SEED_MASK) {
			pansid = pbdl->nlp_sid;
			if (pansid > MAX_FCP_TARGET) {
				continue;
			}
			if (cnt < room) {
				/* if not enough space left then we
				 *  have to copy what we have now back
				 *  to application space, before
				 *  reusing this buffer again.
				 */
				if (total_mem - memsz < minsize) {
					LPFC_DRVR_UNLOCK(phba, iflag);
					if (copy_to_user
					    ((uint8_t *) appPtr,
					     (uint8_t *) drvPtr,
					     memsz)) {
						LPFC_DRVR_LOCK(phba, iflag);
						return EIO;
					}
					LPFC_DRVR_LOCK(phba, iflag);
					appPtr = appPtr + memsz;
					ep.p = drvPtr;
					memsz = 0;
				}
#ifdef CONFIG_X86_64
				if (cip->lpfc_cntl == LPFC_CNTL_X86_APP) {
					memset((void *)ep.se32->ScsiId.OSDeviceName, 0, 256);
					ep.se32->ScsiId.ScsiTargetNumber = pansid;
					ep.se32->ScsiId.ScsiBusNumber = 0;
					memset((char *)fcpLun, 0,
					       sizeof (HBA_UINT64));
					if (pbdl->nlp_bind_type & FCP_SEED_DID) {
						ep.se32->type = TO_D_ID;
						ep.se32->FcpId.FcId = pbdl->nlp_DID;
						ep.se32->FcId = pbdl->nlp_DID;
						memset((uint8_t *) & ep.se32->FcpId.PortWWN,
						       0, sizeof (HBA_WWN));
						memset((uint8_t *) & ep.se32->FcpId.NodeWWN,
						       0, sizeof (HBA_WWN));
					} else {
						ep.se32->type = TO_WWN;
						ep.se32->FcId = 0;
						ep.se32->FcpId.FcId = 0;
						if (pbdl->nlp_bind_type & FCP_SEED_WWPN) {
							memcpy((uint8_t *) & ep.se32->FcpId.
							       PortWWN,
							       &pbdl->nlp_portname,
							       sizeof (HBA_WWN));
						} else {
							memcpy((uint8_t *) & ep.se32->FcpId.
							       NodeWWN,
							       &pbdl->nlp_nodename,
							       sizeof (HBA_WWN));
						}
					}
					ep.se32->FcpId.FcId = pbdl->nlp_DID;
					memcpy((uint8_t *) & ep.se32->FcpId.PortWWN,
					       &pbdl->nlp_portname, sizeof (HBA_WWN));
					memcpy((uint8_t *) & ep.se32->FcpId.NodeWWN,
					       &pbdl->nlp_nodename, sizeof (HBA_WWN));
					ep.se32++;
					cnt++;
					memsz += minsize;
				} else {
#endif /* CONFIG_X86_64 */
					memset((void *)ep.se->ScsiId.OSDeviceName, 0, 256);
					ep.se->ScsiId.ScsiTargetNumber = pansid;
					ep.se->ScsiId.ScsiBusNumber = 0;
					memset((char *)fcpLun, 0,
					       sizeof (HBA_UINT64));
					if (pbdl->nlp_bind_type & FCP_SEED_DID) {
						ep.se->type = TO_D_ID;
						ep.se->FcpId.FcId = pbdl->nlp_DID;
						ep.se->FcId = pbdl->nlp_DID;
						memset((uint8_t *) & ep.se->FcpId.PortWWN,
						       0, sizeof (HBA_WWN));
						memset((uint8_t *) & ep.se->FcpId.NodeWWN,
						       0, sizeof (HBA_WWN));
					} else {
						ep.se->type = TO_WWN;
						ep.se->FcId = 0;
						ep.se->FcpId.FcId = 0;
						if (pbdl->nlp_bind_type & FCP_SEED_WWPN) {
							memcpy((uint8_t *) & ep.se->FcpId.
							       PortWWN,
							       &pbdl->nlp_portname,
							       sizeof (HBA_WWN));
						} else {
							memcpy((uint8_t *) & ep.se->FcpId.
							       NodeWWN,
							       &pbdl->nlp_nodename,
							       sizeof (HBA_WWN));
						}
					}
					ep.se->FcpId.FcId = pbdl->nlp_DID;
					memcpy((uint8_t *) & ep.se->FcpId.PortWWN,
					       &pbdl->nlp_portname, sizeof (HBA_WWN));
					memcpy((uint8_t *) & ep.se->FcpId.NodeWWN,
					       &pbdl->nlp_nodename, sizeof (HBA_WWN));
					ep.se++;
					cnt++;
					memsz += minsize;
#ifdef CONFIG_X86_64
				}
#endif /* CONFIG_X86_64 */
			}
			total++;
		}
	}			/* end searching bindlist */
	LPFC_DRVR_UNLOCK(phba, iflag);
	if (copy_to_user
	    ((uint8_t *) appPtr, (uint8_t *) drvPtr, memsz)) {
		LPFC_DRVR_LOCK(phba, iflag);
		return EIO;
	}

#ifdef CONFIG_X86_64
	if (cip->lpfc_cntl == LPFC_CNTL_X86_APP)
		((HBA_FCPBINDING32 *) dataout)->NumberOfEntries = total;
	else
#endif /* CONFIG_X86_64 */
		((HBA_FCPBINDING *) dataout)->NumberOfEntries = total;

	if (copy_to_user(cip->lpfc_dataout, &total, sizeof (HBA_UINT32))) {
		LPFC_DRVR_LOCK(phba, iflag);
		return EIO;
	}

	LPFC_DRVR_LOCK(phba, iflag);
	cip->lpfc_outsz = 0;	/* no more copy needed */
	if (total > room) {
		rc = ERANGE;
		*do_cp = 1;
	}
	return (rc);
}

int
lpfc_ioctl_port_attrib(lpfcHBA_t * phba, void *dataout)
{
	lpfc_vpd_t *vp;
	SERV_PARM *hsp;
	HBA_PORTATTRIBUTES *hp;
	HBA_OSDN *osdn;
	uint32_t cnt;
	int rc = 0;

	vp = &phba->vpd;
	hsp = (SERV_PARM *) (&phba->fc_sparam);
	hp = (HBA_PORTATTRIBUTES *) dataout;
	memset(dataout, 0, (sizeof (HBA_PORTATTRIBUTES)));
	memcpy((uint8_t *) & hp->NodeWWN,
	       (uint8_t *) & phba->fc_sparam.nodeName, sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN,
	       (uint8_t *) & phba->fc_sparam.portName, sizeof (HBA_WWN));

	if (phba->fc_linkspeed == LA_2GHZ_LINK)
		hp->PortSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSpeed = HBA_PORTSPEED_1GBIT;

	if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
		hp->PortSupportedSpeed = HBA_PORTSPEED_10GBIT;
	else if ((FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == PEGASUS_JEDEC_ID) ||
		 (FC_JEDEC_ID(vp->rev.biuRev) == THOR_JEDEC_ID))
		hp->PortSupportedSpeed = HBA_PORTSPEED_2GBIT;
	else
		hp->PortSupportedSpeed = HBA_PORTSPEED_1GBIT;

	hp->PortFcId = phba->fc_myDID;
	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (phba->fc_topology == TOPOLOGY_LOOP) {
		if (phba->fc_flag & FC_PUBLIC_LOOP) {
			hp->PortType = HBA_PORTTYPE_NLPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & phba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (phba->fc_flag & FC_FABRIC) {
			hp->PortType = HBA_PORTTYPE_NPORT;
			memcpy((uint8_t *) & hp->FabricName,
			       (uint8_t *) & phba->fc_fabparam.nodeName,
			       sizeof (HBA_WWN));
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	if (phba->fc_flag & FC_BYPASSED_MODE) {
		hp->PortState = HBA_PORTSTATE_BYPASSED;
	} else if (phba->fc_flag & FC_OFFLINE_MODE) {
		hp->PortState = HBA_PORTSTATE_DIAGNOSTICS;
	} else {
		switch (phba->hba_state) {
		case LPFC_INIT_START:
		case LPFC_INIT_MBX_CMDS:
			hp->PortState = HBA_PORTSTATE_UNKNOWN;
			break;

		case LPFC_LINK_DOWN:
		case LPFC_LINK_UP:
		case LPFC_LOCAL_CFG_LINK:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
		case LPFC_CLEAR_LA:
			hp->PortState = HBA_PORTSTATE_LINKDOWN;
			break;

		case LPFC_HBA_READY:
			hp->PortState = HBA_PORTSTATE_ONLINE;
			break;

		case LPFC_HBA_ERROR:
		default:
			hp->PortState = HBA_PORTSTATE_ERROR;
			break;
		}
	}

	cnt = phba->fc_map_cnt + phba->fc_unmap_cnt;
	hp->NumberofDiscoveredPorts = cnt;
	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 1 */
	}

	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 2 */
	}

	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 8;	/* bit 3 */
	}

	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	hp->PortSupportedFc4Types.bits[2] = 0x1;
	hp->PortSupportedFc4Types.bits[3] = 0x20;
	hp->PortSupportedFc4Types.bits[7] = 0x1;
	hp->PortActiveFc4Types.bits[2] = 0x1;

	hp->PortActiveFc4Types.bits[7] = 0x1;

	/* OSDeviceName is the device info filled into the HBA_OSDN structure */
	osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
	memcpy(osdn->drvname, LPFC_DRIVER_NAME, 4);
	osdn->instance = phba->brd_no;
	osdn->target = (uint32_t) (-1);
	osdn->lun = (uint32_t) (-1);

	return rc;
}

int
lpfc_ioctl_found_port(lpfcHBA_t * phba,
		      LPFC_NODELIST_t * pndl,
		      void *dataout,
		      MAILBOX_t * pmbox, HBA_PORTATTRIBUTES * hp)
{
	LPFC_SLI_t *psli = &phba->sli;
	SERV_PARM *hsp;
	DMABUF_t *mp;
	HBA_OSDN *osdn;
	LPFC_MBOXQ_t *mboxq;
	unsigned long iflag;
	int mbxstatus;
	int rc = 0;

	/* Check if its the local port */
	if (phba->fc_myDID == pndl->nlp_DID) {
		/* handle localport */
		rc = lpfc_ioctl_port_attrib(phba, dataout);
		return rc;
	}

	memset((void *)pmbox, 0, sizeof (MAILBOX_t));
	pmbox->un.varRdRPI.reqRpi = (volatile uint16_t)pndl->nlp_rpi;
	pmbox->mbxCommand = MBX_READ_RPI64;
	pmbox->mbxOwner = OWN_HOST;

	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);
		return ENOMEM;
	}
	INIT_LIST_HEAD(&mp->list);
	if ((mboxq = lpfc_mbox_alloc(phba, MEM_PRI)) == 0) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		return ENOMEM;
	}

	hsp = (SERV_PARM *) mp->virt;
	if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
		pmbox->un.varRdRPI.un.sp64.addrHigh = putPaddrHigh(mp->phys);
		pmbox->un.varRdRPI.un.sp64.addrLow = putPaddrLow(mp->phys);
		pmbox->un.varRdRPI.un.sp64.tus.f.bdeSize = sizeof (SERV_PARM);
	} else {
		pmbox->un.varRdRPI.un.sp.bdeAddress = putPaddrLow(mp->phys);
		pmbox->un.varRdRPI.un.sp.bdeSize = sizeof (SERV_PARM);
	}

	memset((void *)mboxq, 0, sizeof (LPFC_MBOXQ_t));
	mboxq->mb.mbxCommand = pmbox->mbxCommand;
	mboxq->mb.mbxOwner = pmbox->mbxOwner;
	mboxq->mb.un = pmbox->un;
	mboxq->mb.us = pmbox->us;
	mboxq->context1 = (uint8_t *) 0;

	if ((phba->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE))){
		LPFC_DRVR_UNLOCK(phba, iflag);
		mbxstatus = lpfc_sli_issue_mbox(phba, mboxq, MBX_POLL);
		LPFC_DRVR_LOCK(phba, iflag);
	} else
		mbxstatus =
		    lpfc_sli_issue_mbox_wait(phba, mboxq, phba->fc_ratov * 2);

	if (mbxstatus != MBX_SUCCESS) {
		if (mbxstatus == MBX_TIMEOUT) {
			/*
			 * Let SLI layer to release mboxq if mbox command completed after timeout.
			 */
			mboxq->mbox_cmpl = 0;
		} else {
			lpfc_mbox_free(phba, mboxq);
		}
		return ENODEV;
	}

	pmbox->mbxCommand = mboxq->mb.mbxCommand;
	pmbox->mbxOwner = mboxq->mb.mbxOwner;
	pmbox->un = mboxq->mb.un;
	pmbox->us = mboxq->mb.us;

	if (hsp->cls1.classValid) {
		hp->PortSupportedClassofService |= 2;	/* bit 1 */
	}
	if (hsp->cls2.classValid) {
		hp->PortSupportedClassofService |= 4;	/* bit 2 */
	}
	if (hsp->cls3.classValid) {
		hp->PortSupportedClassofService |= 8;	/* bit 3 */
	}

	hp->PortMaxFrameSize = (((uint32_t) hsp->cmn.bbRcvSizeMsb) << 8) |
	    (uint32_t) hsp->cmn.bbRcvSizeLsb;

	lpfc_mbuf_free(phba, mp->virt, mp->phys);
	kfree(mp);
	lpfc_mbox_free(phba, mboxq);

	memcpy((uint8_t *) & hp->NodeWWN, (uint8_t *) & pndl->nlp_nodename,
	       sizeof (HBA_WWN));
	memcpy((uint8_t *) & hp->PortWWN, (uint8_t *) & pndl->nlp_portname,
	       sizeof (HBA_WWN));
	hp->PortSpeed = 0;
	/* We only know the speed if the device is on the same loop as us */
	if (((phba->fc_myDID & 0xffff00) == (pndl->nlp_DID & 0xffff00)) &&
	    (phba->fc_topology == TOPOLOGY_LOOP)) {
		if (phba->fc_linkspeed == LA_2GHZ_LINK)
			hp->PortSpeed = HBA_PORTSPEED_2GBIT;
		else
			hp->PortSpeed = HBA_PORTSPEED_1GBIT;
	}

	hp->PortFcId = pndl->nlp_DID;
	if ((phba->fc_flag & FC_FABRIC) &&
	    ((phba->fc_myDID & 0xff0000) == (pndl->nlp_DID & 0xff0000))) {
		/* If remote node is in the same domain we are in */
		memcpy((uint8_t *) & hp->FabricName,
		       (uint8_t *) & phba->fc_fabparam.nodeName,
		       sizeof (HBA_WWN));
	}
	hp->PortState = HBA_PORTSTATE_ONLINE;
	if (pndl->nlp_type & NLP_FCP_TARGET) {
		hp->PortActiveFc4Types.bits[2] = 0x1;
	}
	hp->PortActiveFc4Types.bits[7] = 0x1;

	hp->PortType = HBA_PORTTYPE_UNKNOWN;
	if (phba->fc_topology == TOPOLOGY_LOOP) {
		if (phba->fc_flag & FC_PUBLIC_LOOP) {
			/* Check if Fabric port */
			if (lpfc_geportname(&pndl->nlp_nodename,
					    (NAME_TYPE *) & (phba->fc_fabparam.
							     nodeName)) == 2) {
				hp->PortType = HBA_PORTTYPE_FLPORT;
			} else {
				/* Based on DID */
				if ((pndl->nlp_DID & 0xff) == 0) {
					hp->PortType = HBA_PORTTYPE_NPORT;
				} else {
					if ((pndl->nlp_DID & 0xff0000) !=
					    0xff0000) {
						hp->PortType =
						    HBA_PORTTYPE_NLPORT;
					}
				}
			}
		} else {
			hp->PortType = HBA_PORTTYPE_LPORT;
		}
	} else {
		if (phba->fc_flag & FC_FABRIC) {
			/* Check if Fabric port */
			if (lpfc_geportname(&pndl->nlp_nodename,
					    (NAME_TYPE *) & (phba->fc_fabparam.
							     nodeName)) == 2) {
				hp->PortType = HBA_PORTTYPE_FPORT;
			} else {
				/* Based on DID */
				if ((pndl->nlp_DID & 0xff) == 0) {
					hp->PortType = HBA_PORTTYPE_NPORT;
				} else {
					if ((pndl->nlp_DID & 0xff0000) !=
					    0xff0000) {
						hp->PortType =
						    HBA_PORTTYPE_NLPORT;
					}
				}
			}
		} else {
			hp->PortType = HBA_PORTTYPE_PTP;
		}
	}

	/* for mapped devices OSDeviceName is device info filled into HBA_OSDN 
	 * structure */
	if (pndl->nlp_flag & NLP_MAPPED_LIST) {
		osdn = (HBA_OSDN *) & hp->OSDeviceName[0];
		memcpy(osdn->drvname, LPFC_DRIVER_NAME, 4);
		osdn->instance = phba->brd_no;
		osdn->target = pndl->nlp_sid;
		osdn->lun = (uint32_t) (-1);
	}

	return rc;
}
