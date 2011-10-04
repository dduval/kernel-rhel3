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
 * $Id: lpfc_ct.c 437 2005-11-01 17:11:04Z sf_support $
 *
 * Fibre Channel SCSI LAN Device Driver CT support
 */


#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/utsname.h>
#include <linux/pci.h>
#include <linux/utsname.h>


#include <linux/blk.h>

#include "lpfc_version.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"

#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_cfgparm.h"
#include "lpfc_dfc.h"
#include "hbaapi.h"

#define FOURBYTES	4


extern char *lpfc_release_version;

DMABUF_t *lpfc_alloc_ct_rsp(lpfcHBA_t *, int, ULP_BDE64 *, uint32_t, int *);



/*
 * lpfc_ct_unsol_event
 */
void
lpfc_ct_unsol_event(lpfcHBA_t * phba,
		    LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * piocbq)
{

	LPFC_SLI_t *psli;
	DMABUF_t *p_mbuf = 0;
	DMABUF_t *matp;
	uint32_t ctx;
	uint32_t count;
	IOCB_t *icmd;
	int i;
	int status;
	int go_exit = 0;
	struct list_head head, *curr, *next;

	psli = &phba->sli;
	icmd = &piocbq->iocb;
	if (icmd->ulpStatus) {
		goto exit_unsol_event;
	}

	ctx = 0;
	count = 0;
	list_add_tail(&head, &piocbq->list);
	list_for_each_safe(curr, next, &head) {
		piocbq = list_entry(curr, LPFC_IOCBQ_t, list);
		piocbq = (LPFC_IOCBQ_t *) curr;
		icmd = &piocbq->iocb;
		if (ctx == 0)
			ctx = (uint32_t) (icmd->ulpContext);
		if (icmd->ulpStatus) {
			if ((icmd->ulpStatus == IOSTAT_LOCAL_REJECT) &&
				((icmd->un.ulpWord[4] & 0xff)
				 == IOERR_RCV_BUFFER_WAITING)) {
				phba->fc_stat.NoRcvBuf++;

				phba->fc_flag |= FC_NO_RCV_BUF;
				lpfc_post_buffer(phba, pring, 0, 1);
			}

			go_exit = 1;
			break;
		}

		if (icmd->ulpBdeCount == 0) {
			continue;
		}

		for (i = 0; i < (int)icmd->ulpBdeCount; i++) {
			matp = lpfc_sli_ringpostbuf_get(phba, pring,
							getPaddr(icmd->un.
								 cont64[i].
								 addrHigh,
								 icmd->un.
								 cont64[i].
								 addrLow));
			if (matp == 0) {
				/* Insert lpfc log message here */
				go_exit = 1;
				break;
			}

			/* Typically for Unsolicited CT requests */
			if (!p_mbuf) {
				p_mbuf = matp;
				INIT_LIST_HEAD(&p_mbuf->list);
			} else
				list_add_tail(&matp->list, &p_mbuf->list);

			count += icmd->un.cont64[i].tus.f.bdeSize;
		}

		/* check for early exit from above for loop */
		if (go_exit != 0)
			break;

		lpfc_post_buffer(phba, pring, i, 1);
		icmd->ulpBdeCount = 0;
	}

	list_del(&head);

	/*
	 * if not early-exiting and there is p_mbuf,
	 * then do  FC_REG_CT_EVENT for HBAAPI libdfc event handling
	 */
	if (go_exit == 0  &&  p_mbuf != 0) {
		status = lpfc_put_event(phba, FC_REG_CT_EVENT, ctx,
				       p_mbuf,
				       (void *) (unsigned long)count);

		if ( status) {
			/* Need to free IOCB buffer ? */
			return;
		}
	}

exit_unsol_event:
	if (p_mbuf) {
		list_for_each_safe(curr, next, &p_mbuf->list) {
			matp = list_entry(curr, DMABUF_t, list);
			lpfc_mbuf_free(phba, matp->virt, matp->phys);
			list_del(&matp->list);
			kfree(matp);
		}
		lpfc_mbuf_free(phba, p_mbuf->virt, p_mbuf->phys);
		kfree(p_mbuf);
	}
	return;
}

/*
 * lpfc_ns_cmd
 * Description:
 *    Issue Cmd to NameServer
 *       SLI_CTNS_GID_FT
 *       LI_CTNS_RFT_ID
 */
int
lpfc_ns_cmd(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, int cmdcode)
{
	lpfcCfgParam_t *clp;
	DMABUF_t *mp, *bmp;
	SLI_CT_REQUEST *CtReq;
	ULP_BDE64 *bpl;
	void (*cmpl) (struct lpfcHBA *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);
	uint32_t rsp_size;

	clp = &phba->config[0];

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if ((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) {
		return (1);
	}
	
	INIT_LIST_HEAD(&mp->list);
	if ((mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys))) == 0) {
		kfree(mp);
		return (1);
	}

	/* Allocate buffer for Buffer ptr list */
	if ((bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		return (1);
	}

	INIT_LIST_HEAD(&bmp->list);
	if ((bmp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(bmp->phys))) == 0) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		kfree(bmp);
		return (1);
	}

	bpl = (ULP_BDE64 *) bmp->virt;
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	if (cmdcode == SLI_CTNS_GID_FT)
		bpl->tus.f.bdeSize = GID_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RFT_ID)
		bpl->tus.f.bdeSize = RFT_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RNN_ID)
		bpl->tus.f.bdeSize = RNN_REQUEST_SZ;
	else if (cmdcode == SLI_CTNS_RSNN_NN)
		bpl->tus.f.bdeSize = RSNN_REQUEST_SZ;
	else
		bpl->tus.f.bdeSize = 0;

	bpl->tus.w = le32_to_cpu(bpl->tus.w);
	CtReq = (SLI_CT_REQUEST *) mp->virt;

	/* NameServer Req */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0236, lpfc_mes0236,
			lpfc_msgBlk0236.msgPreambleStr, cmdcode, phba->fc_flag,
			phba->fc_rscn_id_cnt);

	memset(CtReq, 0, sizeof (SLI_CT_REQUEST));
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_DIRECTORY_SERVICE;
	CtReq->FsSubType = SLI_CT_DIRECTORY_NAME_SERVER;
	CtReq->CommandResponse.bits.Size = 0;

	cmpl = 0;
	rsp_size = 1024;
	switch (cmdcode) {
	case SLI_CTNS_GID_FT:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_GID_FT);
		CtReq->un.gid.Fc4Type = SLI_CTPT_FCP;
		if (phba->hba_state < LPFC_HBA_READY) {
			phba->hba_state = LPFC_NS_QRY;
		}
		lpfc_set_disctmo(phba);
		cmpl = lpfc_cmpl_ct_cmd_gid_ft;
		rsp_size = FC_MAX_NS_RSP;
		break;

	case SLI_CTNS_RFT_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RFT_ID);
		CtReq->un.rft.PortId = be32_to_cpu(phba->fc_myDID);
		CtReq->un.rft.fcpReg = 1;

		cmpl = lpfc_cmpl_ct_cmd_rft_id;
		break;

	case SLI_CTNS_RNN_ID:
		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RNN_ID);
		CtReq->un.rnn.PortId = be32_to_cpu(phba->fc_myDID);
		memcpy(CtReq->un.rnn.wwnn, (uint8_t *) & phba->fc_nodename,
		       sizeof (NAME_TYPE));
		cmpl = lpfc_cmpl_ct_cmd_rnn_id;
		break;

	case SLI_CTNS_RSNN_NN:

		CtReq->CommandResponse.bits.CmdRsp =
		    be16_to_cpu(SLI_CTNS_RSNN_NN);
		memcpy(CtReq->un.rsnn.wwnn, (uint8_t *) & phba->fc_nodename,
		       sizeof (NAME_TYPE));
		lpfc_get_hba_sym_node_name(phba,
					  (uint8_t *) CtReq->un.rsnn.symbname);
		CtReq->un.rsnn.len = strlen((char *)CtReq->un.rsnn.symbname);
		cmpl = lpfc_cmpl_ct_cmd_rsnn_nn;
		break;
	}

	if (lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl, rsp_size)) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(mp);
		kfree(bmp);
		return (1);
	}

	return (0);
}

int
lpfc_ct_cmd(lpfcHBA_t * phba,
	    DMABUF_t * inmp,
	    DMABUF_t * bmp,
	    LPFC_NODELIST_t *ndlp,
	    void (*cmpl) (struct lpfcHBA *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *),
	    uint32_t rsp_size)
{
	ULP_BDE64 *bpl;
	DMABUF_t *outmp;
	int cnt;
	int cmdcode;

	bpl = (ULP_BDE64 *) bmp->virt;
	bpl++;			/* Skip past ct request */

	cnt = 0;

	cmdcode = ((SLI_CT_REQUEST *) inmp->virt)->CommandResponse.bits.CmdRsp;

	/* Put buffer(s) for ct rsp in bpl */
	if ((outmp = lpfc_alloc_ct_rsp(phba, cmdcode, bpl, rsp_size, &cnt))
	    == 0) {
		return (ENOMEM);
	}

	if ((lpfc_gen_req
	     (phba, bmp, inmp, outmp, cmpl, ndlp, 0, (cnt + 1),
	      0))) {
		lpfc_free_ct_rsp(phba, outmp);
		return (ENOMEM);
	}
	return (0);
}

int
lpfc_free_ct_rsp(lpfcHBA_t * phba, DMABUF_t * mlist)
{
	DMABUF_t *mlast;
	struct list_head *curr, *next;

	list_for_each_safe(curr, next, &mlist->list) {
		mlast = list_entry(curr, DMABUF_t, list);
		lpfc_mbuf_free(phba, mlast->virt, mlast->phys);
		list_del(&mlast->list);
		kfree(mlast);
	}

	lpfc_mbuf_free(phba, mlist->virt, mlist->phys);
	kfree(mlist);

	return (0);
}

DMABUF_t *
lpfc_alloc_ct_rsp(lpfcHBA_t * phba, int cmdcode, ULP_BDE64 * bpl, uint32_t size,
		  int *entries)
{
	DMABUF_t *mlist;
	DMABUF_t *mp;
	int cnt, i;

	mlist = 0;
	i = 0;

	while (size) {

		/* We get chucks of FCELSSIZE */
		if (size > FCELSSIZE)
			cnt = FCELSSIZE;
		else
			cnt = size;

		/* Allocate buffer for rsp payload */
		if ((mp = kmalloc(sizeof(DMABUF_t), GFP_ATOMIC)) == 0) {
			lpfc_free_ct_rsp(phba, mlist);
			return (0);
		}
		
		INIT_LIST_HEAD(&mp->list);		

		if (cmdcode == be16_to_cpu(SLI_CTNS_GID_FT))
			mp->virt = lpfc_mbuf_alloc(phba, MEM_PRI, &(mp->phys));

		else
			mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys));

		if (mp->virt == 0) {
				kfree(mp);
			lpfc_free_ct_rsp(phba, mlist);
			return (0);
		}
				
		/* Queue it to a linked list */
		if (mlist)
			list_add_tail(&mp->list, &mlist->list); 
		else 
			mlist = mp;

		bpl->tus.f.bdeFlags = BUFF_USE_RCV;

		/* build buffer ptr list for IOCB */
		bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
		bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
		bpl->tus.f.bdeSize = (uint16_t) cnt;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;

		i++;
		size -= cnt;
	}

	*entries = i;
	return (mlist);
}

int
lpfc_ns_rsp(lpfcHBA_t * phba, DMABUF_t * mp, uint32_t Size)
{
	SLI_CT_REQUEST *Response;
	LPFC_NODELIST_t *ndlp, *new_ndlp;
	struct list_head *listp, *pos, *pos_tmp;
	struct list_head *node_list[4];
	DMABUF_t *mlast, *mhead;
	uint32_t *ctptr;
	uint32_t Did;
	uint32_t CTentry;
	int Cnt, new_node, i;
	struct list_head head, *curr, *next;

	ndlp = 0;

	lpfc_set_disctmo(phba);

	Response = (SLI_CT_REQUEST *) mp->virt;
	ctptr = (uint32_t *) & Response->un.gid.PortType;

	mhead = mp;
	list_add_tail(&head, &mp->list);

	list_for_each_safe(curr, next, &head) {
		mp = list_entry(curr, DMABUF_t, list);
		mlast = mp;

		if (Size > FCELSSIZE)
			Cnt = FCELSSIZE;
		else
			Cnt = Size;

		Size -= Cnt;

		if (ctptr == 0)
			ctptr = (uint32_t *) mlast->virt;
		else
			Cnt -= 16;	/* subtract length of CT header */

		/* Loop through entire NameServer list of DIDs */
		while (Cnt) {

			/* Get next DID from NameServer List */
			CTentry = *ctptr++;
			Did = ((be32_to_cpu(CTentry)) & Mask_DID);

			/* If we are processing an RSCN, check to ensure the Did
			 * falls under the juristiction of the RSCN payload.
			 */
			if (phba->hba_state == LPFC_HBA_READY) {
				Did = lpfc_rscn_payload_check(phba, Did);
				/* Did = 0 indicates Not part of RSCN, ignore
				   this entry */
			}

			ndlp = 0;
			if ((Did) && (Did != phba->fc_myDID)) {
				new_node = 0;
				/* Skip if the node is already in the plogi /
				   adisc list */
				if ((ndlp =
				     lpfc_findnode_did(phba, (NLP_SEARCH_PLOGI |
							      NLP_SEARCH_ADISC),
						       Did)))
					{
						goto nsout0;
					}
				ndlp =
				    lpfc_findnode_did(phba, NLP_SEARCH_ALL,
						      Did);
				if (ndlp) {
					/*
					 * Event NLP_EVT_DEVICE_ADD will trigger ADISC to be
					 * sent to the N_port after receiving a LOGO from it.
					 * Don't call state machine if NLP_LOGO_ACC is set
					 * Let lpfc_cmpl_els_logo_acc() to handle this node
					 */
					if (!(ndlp->nlp_flag & NLP_LOGO_ACC) &&
					    !(ndlp->nlp_flag & NLP_PRLI_SND)) {
						lpfc_disc_state_machine(phba, ndlp, (void *)0,
									NLP_EVT_DEVICE_ADD);
					}
				} else {
					new_node = 1;
					if ((ndlp = (LPFC_NODELIST_t *)
					     lpfc_nlp_alloc(phba, 0))) {
						memset(ndlp, 0,
						       sizeof
						       (LPFC_NODELIST_t));
						ndlp->nlp_state =
						    NLP_STE_UNUSED_NODE;
						ndlp->nlp_DID = Did;
						lpfc_disc_state_machine(phba,
							ndlp, 0,
							NLP_EVT_DEVICE_ADD);
					}
				}
			}
		      nsout0:
			/* Mark all node table entries that are in the
			   Nameserver */
			if (ndlp) {
				ndlp->nlp_flag |= NLP_NS_NODE;
				/* NameServer Rsp */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0238,
						lpfc_mes0238,
						lpfc_msgBlk0238.msgPreambleStr,
						Did, ndlp->nlp_flag,
						phba->fc_flag,
						phba->fc_rscn_id_cnt);
			} else {
				/* NameServer Rsp */
				lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0239,
						lpfc_mes0239,
						lpfc_msgBlk0239.msgPreambleStr,
						Did, Size, phba->fc_flag,
						phba->fc_rscn_id_cnt);
			}

			if (CTentry & (be32_to_cpu(SLI_CT_LAST_ENTRY)))
				goto nsout1;
			Cnt -= sizeof (uint32_t);
		}
		ctptr = 0;

	}

      nsout1:
	list_del(&head);
	/* Take out all node table entries that are not in the NameServer.  To
	 * begin, start with a populated list.
	 */
	node_list[0] = &phba->fc_plogi_list;
	node_list[1] = &phba->fc_adisc_list;
	node_list[2] = &phba->fc_nlpunmap_list;
	node_list[3] = &phba->fc_nlpmap_list;
	for (i = 0; i < 4; i++) {
		listp = node_list[i];
		if (list_empty(listp)) 
			continue;

		list_for_each_safe(pos, pos_tmp, listp) {
			new_ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			ndlp     = new_ndlp;
			if ((ndlp->nlp_DID == phba->fc_myDID) ||
			    (ndlp->nlp_DID == NameServer_DID) ||
			    (ndlp->nlp_DID == FDMI_DID)       ||
			    (ndlp->nlp_type & NLP_FABRIC) ||
			    (ndlp->nlp_flag & NLP_NS_NODE)) {
				if (ndlp->nlp_flag & NLP_NS_NODE) {
					ndlp->nlp_flag &= ~NLP_NS_NODE;
				}
				continue;
			}

			/* If the driver is processing an RSCN, check to ensure
			 * the Did falls under the juristiction of the RSCN
			 * payload.
			 */
			if ((phba->hba_state == LPFC_HBA_READY) &&
			    (!(lpfc_rscn_payload_check(phba, ndlp->nlp_DID)))) {
				/* Not part of RSCN, ignore this entry */
				continue;	
			}

			lpfc_disc_state_machine(phba, ndlp, 0,
						NLP_EVT_DEVICE_RM);

		}
	}

	if (phba->hba_state == LPFC_HBA_READY) {
		lpfc_els_flush_rscn(phba);
		phba->fc_flag |= FC_RSCN_MODE;
	}
	return (0);
}

int
lpfc_issue_ct_rsp(lpfcHBA_t * phba,
		  uint32_t tag, DMABUF_t * bmp, DMABUFEXT_t * inp)
{
	LPFC_SLI_t *psli;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *ctiocb;
	LPFC_SLI_RING_t *pring;
	uint32_t num_entry;
	int rc = 0;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	num_entry = (uint32_t) inp->flag;
	inp->flag = 0;

	/* Allocate buffer for  command iocb */
	if ((ctiocb = lpfc_iocb_alloc(phba, 0)) == 0) {
		return (ENOMEM);
	}
	memset(ctiocb, 0, sizeof (LPFC_IOCBQ_t));
	icmd = &ctiocb->iocb;

	icmd->un.xseq64.bdl.ulpIoTag32 = 0;
	icmd->un.xseq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.xseq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.xseq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.xseq64.bdl.bdeSize = (num_entry * sizeof (ULP_BDE64));

	icmd->un.xseq64.w5.hcsw.Fctl = (LS | LA);
	icmd->un.xseq64.w5.hcsw.Dfctl = 0;
	icmd->un.xseq64.w5.hcsw.Rctl = FC_SOL_CTL;
	icmd->un.xseq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

	/* Fill in rest of iocb */
	icmd->ulpCommand = CMD_XMIT_SEQUENCE64_CX;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = (ushort) tag;
	/* Xmit CT response on exchange <xid> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0118,
			lpfc_mes0118, lpfc_msgBlk0118.msgPreambleStr,
			icmd->ulpContext, icmd->ulpIoTag, phba->hba_state);

	ctiocb->iocb_cmpl = 0;
	ctiocb->iocb_flag |= LPFC_IO_LIBDFC;

	rc = lpfc_sli_issue_iocb_wait(phba, pring, ctiocb, SLI_IOCB_USE_TXQ, 0,
				     phba->fc_ratov * 2 + LPFC_DRVR_TIMEOUT);
	lpfc_iocb_free(phba, ctiocb);
	return (rc);
}				/* lpfc_issue_ct_rsp */

int
lpfc_gen_req(lpfcHBA_t * phba,
	     DMABUF_t * bmp,
	     DMABUF_t * inp,
	     DMABUF_t * outp,
	     void (*cmpl) (struct lpfcHBA *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *),
	     LPFC_NODELIST_t *ndlp, uint32_t usr_flg, uint32_t num_entry,
	     uint32_t tmo)
{

	LPFC_SLI_t *psli;
	LPFC_SLI_RING_t *pring;
	IOCB_t *icmd;
	LPFC_IOCBQ_t *geniocb;

	psli = &phba->sli;
	pring = &psli->ring[LPFC_ELS_RING];	/* ELS ring */
	/* Allocate buffer for  command iocb */
	if ((geniocb = lpfc_iocb_alloc(phba, 0)) == 0) {
		return (1);
	}
	memset(geniocb, 0, sizeof (LPFC_IOCBQ_t));
	icmd = &geniocb->iocb;

	icmd->un.genreq64.bdl.ulpIoTag32 = 0;
	icmd->un.genreq64.bdl.addrHigh = putPaddrHigh(bmp->phys);
	icmd->un.genreq64.bdl.addrLow = putPaddrLow(bmp->phys);
	icmd->un.genreq64.bdl.bdeFlags = BUFF_TYPE_BDL;
	icmd->un.genreq64.bdl.bdeSize = (num_entry * sizeof (ULP_BDE64));

	if (usr_flg)
		geniocb->context3 = 0;
	else
		geniocb->context3 = (uint8_t *) bmp;

	/* Save for completion so we can release these resources */
	geniocb->context1 = (uint8_t *) inp;
	geniocb->context2 = (uint8_t *) outp;

	/* Fill in payload, bp points to frame payload */
	icmd->ulpCommand = CMD_GEN_REQUEST64_CR;

	icmd->ulpIoTag = lpfc_sli_next_iotag(phba, pring);

	/* Fill in rest of iocb */
	icmd->un.genreq64.w5.hcsw.Fctl = (SI | LA);
	icmd->un.genreq64.w5.hcsw.Dfctl = 0;
	icmd->un.genreq64.w5.hcsw.Rctl = FC_UNSOL_CTL;
	icmd->un.genreq64.w5.hcsw.Type = FC_COMMON_TRANSPORT_ULP;

	if (tmo == 0)
		tmo = (2 * phba->fc_ratov) + 1;
	icmd->ulpTimeout = tmo;
	icmd->ulpBdeCount = 1;
	icmd->ulpLe = 1;
	icmd->ulpClass = CLASS3;
	icmd->ulpContext = ndlp->nlp_rpi;

	/* Issue GEN REQ IOCB for NPORT <did> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0119,
			lpfc_mes0119, lpfc_msgBlk0119.msgPreambleStr,
			icmd->un.ulpWord[5], icmd->ulpIoTag, phba->hba_state);
	geniocb->iocb_cmpl = cmpl;
	geniocb->drvrTimeout = icmd->ulpTimeout + LPFC_DRVR_TIMEOUT;
	if (lpfc_sli_issue_iocb(phba, pring, geniocb, SLI_IOCB_USE_TXQ) ==
	    IOCB_ERROR) {
		lpfc_iocb_free(phba, geniocb);
		return (1);
	}

	return (0);
}

void
lpfc_cmpl_ct_cmd_gid_ft(lpfcHBA_t * phba,
			LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	IOCB_t *irsp;
	LPFC_SLI_t *psli;
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	LPFC_NODELIST_t *ndlp;
	SLI_CT_REQUEST *CTrsp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb; 

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;

	irsp = &rspiocb->iocb;

	/*
	 * If the iocb is aborted by the driver do not retry it.
	 */
        if ((irsp->ulpStatus ) &&
            ((irsp->un.ulpWord[4] == IOERR_SLI_DOWN)||
	     (irsp->un.ulpWord[4] == IOERR_SLI_ABORTED)))
                goto out;

	if (irsp->ulpStatus) {
		/* Check for retry */
		if (phba->fc_ns_retry < LPFC_MAX_NS_RETRY) {
			phba->fc_ns_retry++;
			/* CT command is being retried */
			ndlp =
			    lpfc_findnode_did(phba, NLP_SEARCH_UNMAPPED,
					      NameServer_DID);
			if (ndlp) {
				if (lpfc_ns_cmd(phba, ndlp, SLI_CTNS_GID_FT) ==
				    0) {
					goto out;
				}
			}
		}
	} else {
		/* Good status, continue checking */
		CTrsp = (SLI_CT_REQUEST *) outp->virt;
		if (CTrsp->CommandResponse.bits.CmdRsp ==
		    be16_to_cpu(SLI_CT_RESPONSE_FS_ACC)) {
			lpfc_ns_rsp(phba, outp,
				    (uint32_t) (irsp->un.genreq64.bdl.bdeSize));
		} else if (CTrsp->CommandResponse.bits.CmdRsp ==
			   be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
			/* NameServer Rsp Error */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0240,
					lpfc_mes0240,
					lpfc_msgBlk0240.msgPreambleStr,
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					phba->fc_flag);
		} else {
			/* NameServer Rsp Error */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0241,
					lpfc_mes0241,
					lpfc_msgBlk0241.msgPreambleStr,
					CTrsp->CommandResponse.bits.CmdRsp,
					(uint32_t) CTrsp->ReasonCode,
					(uint32_t) CTrsp->Explanation,
					phba->fc_flag);
		}
	}
	/* Link up / RSCN discovery */
	lpfc_disc_start(phba);
      out:
	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	lpfc_iocb_free(phba, cmdiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rft_id(lpfcHBA_t * phba,
			LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	LPFC_SLI_t *psli;
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	IOCB_t *irsp;
	SLI_CT_REQUEST *CTrsp;

	psli = &phba->sli;
	/* we pass cmdiocb to state machine which needs rspiocb as well */
	cmdiocb->context_un.rsp_iocb = rspiocb; 

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;
	irsp = &rspiocb->iocb;

	CTrsp = (SLI_CT_REQUEST *) outp->virt;

	/* RFT request completes status <ulpStatus> CmdRsp <CmdRsp> */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0209,
			lpfc_mes0209, lpfc_msgBlk0209.msgPreambleStr,
			irsp->ulpStatus, CTrsp->CommandResponse.bits.CmdRsp);

	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	lpfc_iocb_free(phba, cmdiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rnn_id(lpfcHBA_t * phba,
			LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

void
lpfc_cmpl_ct_cmd_rsnn_nn(lpfcHBA_t * phba,
			 LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	lpfc_cmpl_ct_cmd_rft_id(phba, cmdiocb, rspiocb);
	return;
}

void
lpfc_get_os_nameversion(int cmd, char *osversion)
{

	memset(osversion, 0, 256);

	switch (cmd) {
	case GET_OS_VERSION:
		sprintf(osversion, "%s %s %s",
			system_utsname.sysname, system_utsname.release,
			system_utsname.version);
		break;
	case GET_HOST_NAME:
		sprintf(osversion, "%s", system_utsname.nodename);
		break;
	}
	return ;
}

int
lpfc_fdmi_cmd(lpfcHBA_t * phba, LPFC_NODELIST_t * ndlp, int cmdcode)
{
	lpfcCfgParam_t *clp;
	DMABUF_t *mp, *bmp;
	SLI_CT_REQUEST *CtReq;
	ULP_BDE64 *bpl;
	uint32_t size;
	PREG_HBA rh;
	PPORT_ENTRY pe;
	PREG_PORT_ATTRIBUTE pab;
	PATTRIBUTE_BLOCK ab;
	PATTRIBUTE_ENTRY ae;
	void (*cmpl) (struct lpfcHBA *, LPFC_IOCBQ_t *, LPFC_IOCBQ_t *);

	clp = &phba->config[0];

	/* fill in BDEs for command */
	/* Allocate buffer for command payload */
	if (((mp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((mp->virt = lpfc_mbuf_alloc(phba, 0, &(mp->phys))) == 0)) {
		if (mp)
			kfree(mp);
		/* Issue FDMI request failed */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0219,
				lpfc_mes0219, lpfc_msgBlk0219.msgPreambleStr,
				cmdcode);
		return (1);
	}

	/* Allocate buffer for Buffer ptr list */
	if (((bmp = kmalloc(sizeof (DMABUF_t), GFP_ATOMIC)) == 0) ||
	    ((bmp->virt = lpfc_mbuf_alloc(phba, 0, &(bmp->phys))) == 0)) {
		if (bmp)
			kfree(bmp);
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		kfree(mp);
		/* Issue FDMI request failed */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0243,
			       lpfc_mes0243, lpfc_msgBlk0243.msgPreambleStr,
			       cmdcode);
		return (1);
	}

	INIT_LIST_HEAD(&mp->list);
	INIT_LIST_HEAD(&bmp->list);
	/* FDMI request */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0218,
		       lpfc_mes0218, lpfc_msgBlk0218.msgPreambleStr,
		       phba->fc_flag, phba->hba_state, cmdcode);

	CtReq = (SLI_CT_REQUEST *) mp->virt;

/*
   memset((void *)CtReq, 0, sizeof(SLI_CT_REQUEST));
*/
	memset(CtReq, 0, 1024);
	CtReq->RevisionId.bits.Revision = SLI_CT_REVISION;
	CtReq->RevisionId.bits.InId = 0;

	CtReq->FsType = SLI_CT_MANAGEMENT_SERVICE;
	CtReq->FsSubType = SLI_CT_FDMI_Subtypes;
	size = 0;

#define FOURBYTES	4

	switch (cmdcode) {
	case SLI_MGMT_RHBA:
		{
			lpfc_vpd_t *vp;
			char str[256];
			char lpfc_fwrevision[32];
			uint32_t i, j, incr;
			int len;
			uint8_t HWrev[8];

			vp = &phba->vpd;

			CtReq->CommandResponse.bits.CmdRsp =
			    be16_to_cpu(SLI_MGMT_RHBA);
			CtReq->CommandResponse.bits.Size = 0;
			rh = (REG_HBA *) & CtReq->un.PortID;
			memcpy((uint8_t *) & rh->hi.PortName,
			       (uint8_t *) & phba->fc_sparam.portName,
			       sizeof (NAME_TYPE));
			/* One entry (port) per adapter */
			rh->rpl.EntryCnt = be32_to_cpu(1);
			memcpy((uint8_t *) & rh->rpl.pe,
			       (uint8_t *) & phba->fc_sparam.portName,
			       sizeof (NAME_TYPE));

			/* point to the HBA attribute block */
			size =
			    sizeof (NAME_TYPE) + FOURBYTES + sizeof (NAME_TYPE);
			ab = (ATTRIBUTE_BLOCK *) ((uint8_t *) rh + size);
			ab->EntryCnt = 0;

			/* Point to the beginning of the first HBA attribute
			   entry */
			/* #1 HBA attribute entry */
			size += FOURBYTES;
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(NODE_NAME);
			ae->ad.bits.AttrLen =
			    be16_to_cpu(FOURBYTES + sizeof (NAME_TYPE));
			memcpy((uint8_t *) & ae->un.NodeName,
			       (uint8_t *) & phba->fc_sparam.nodeName,
			       sizeof (NAME_TYPE));
			ab->EntryCnt++;
			size += FOURBYTES + sizeof (NAME_TYPE);

			/* #2 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MANUFACTURER);
			strcpy((char *)ae->un.Manufacturer,
			       "Emulex Corporation");
			len = strlen((char *)ae->un.Manufacturer);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #3 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(SERIAL_NUMBER);
			strcpy((char *)ae->un.SerialNumber, phba->SerialNumber);
			len = strlen((char *)ae->un.SerialNumber);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #4 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MODEL);
			strcpy((char *)ae->un.Model, phba->ModelName);
			len = strlen((char *)ae->un.Model);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #5 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MODEL_DESCRIPTION);
			strcpy((char *)ae->un.ModelDescription, phba->ModelDesc);
			len = strlen((char *)ae->un.ModelDescription);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #6 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(HARDWARE_VERSION);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 8);
			/* Convert JEDEC ID to ascii for hardware version */
			incr = vp->rev.biuRev;
			for (i = 0; i < 8; i++) {
				j = (incr & 0xf);
				if (j <= 9)
					HWrev[7 - i] =
					    (char)((uint8_t) 0x30 +
						   (uint8_t) j);
				else
					HWrev[7 - i] =
					    (char)((uint8_t) 0x61 +
						   (uint8_t) (j - 10));
				incr = (incr >> 4);
			}
			memcpy(ae->un.HardwareVersion, (uint8_t *) HWrev, 8);
			ab->EntryCnt++;
			size += FOURBYTES + 8;

			/* #7 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(DRIVER_VERSION);
			strcpy((char *)ae->un.DriverVersion,
			       (char *)lpfc_release_version);
			len = strlen((char *)ae->un.DriverVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #8 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(OPTION_ROM_VERSION);
			strcpy((char *)ae->un.OptionROMVersion,
			       (char *)phba->OptionROMVersion);
			len = strlen((char *)ae->un.OptionROMVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #9 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(FIRMWARE_VERSION);
			lpfc_decode_firmware_rev(phba, lpfc_fwrevision, 1);
			strcpy((char *)ae->un.FirmwareVersion,
			       (char *)lpfc_fwrevision);
			len = strlen((char *)ae->un.FirmwareVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #10 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(OS_NAME_VERSION);
			lpfc_get_os_nameversion(GET_OS_VERSION, str);
			strcpy((char *)ae->un.OsNameVersion, (char *)str);
			len = strlen((char *)ae->un.OsNameVersion);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			ab->EntryCnt++;
			size += FOURBYTES + len;

			/* #11 HBA attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) rh + size);
			ae->ad.bits.AttrType = be16_to_cpu(MAX_CT_PAYLOAD_LEN);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			ae->un.MaxCTPayloadLen = (65 * 4096);
			ab->EntryCnt++;
			size += FOURBYTES + 4;

			ab->EntryCnt = be32_to_cpu(ab->EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_RPA:
		{
			lpfc_vpd_t *vp;
			SERV_PARM *hsp;
			char str[256];
			int len;

			vp = &phba->vpd;

			CtReq->CommandResponse.bits.CmdRsp =
			    be16_to_cpu(SLI_MGMT_RPA);
			CtReq->CommandResponse.bits.Size = 0;
			pab = (REG_PORT_ATTRIBUTE *) & CtReq->un.PortID;
			size = sizeof (NAME_TYPE) + FOURBYTES;
			memcpy((uint8_t *) & pab->PortName,
			       (uint8_t *) & phba->fc_sparam.portName,
			       sizeof (NAME_TYPE));
			pab->ab.EntryCnt = 0;

			/* #1 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(SUPPORTED_FC4_TYPES);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 32);
			ae->un.SupportFC4Types[2] = 1;
			ae->un.SupportFC4Types[7] = 1;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 32;

			/* #2 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(SUPPORTED_SPEED);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			if (FC_JEDEC_ID(vp->rev.biuRev) == VIPER_JEDEC_ID)
				ae->un.SupportSpeed = HBA_PORTSPEED_10GBIT;
			else if ((FC_JEDEC_ID(vp->rev.biuRev) ==
				  HELIOS_JEDEC_ID)
				 || (FC_JEDEC_ID(vp->rev.biuRev) ==
				     ZEPHYR_JEDEC_ID))
				ae->un.SupportSpeed = HBA_PORTSPEED_4GBIT;
			else if ((FC_JEDEC_ID(vp->rev.biuRev) ==
				  CENTAUR_2G_JEDEC_ID)
				 || (FC_JEDEC_ID(vp->rev.biuRev) ==
				     PEGASUS_JEDEC_ID)
				 || (FC_JEDEC_ID(vp->rev.biuRev) ==
				     THOR_JEDEC_ID))
				ae->un.SupportSpeed = HBA_PORTSPEED_2GBIT;
			else
				ae->un.SupportSpeed = HBA_PORTSPEED_1GBIT;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #3 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(PORT_SPEED);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			switch(phba->fc_linkspeed) {
				case LA_1GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_1GBIT;
				break;
				case LA_2GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_2GBIT;
				break;
				case LA_4GHZ_LINK:
					ae->un.PortSpeed = HBA_PORTSPEED_4GBIT;
				break;
				default:
					ae->un.PortSpeed = HBA_PORTSPEED_UNKNOWN;
				break;
			}
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #4 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(MAX_FRAME_SIZE);
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + 4);
			hsp = (SERV_PARM *) & phba->fc_sparam;
			ae->un.MaxFrameSize =
			    (((uint32_t) hsp->cmn.
			      bbRcvSizeMsb) << 8) | (uint32_t) hsp->cmn.
			    bbRcvSizeLsb;
			pab->ab.EntryCnt++;
			size += FOURBYTES + 4;

			/* #5 Port attribute entry */
			ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab + size);
			ae->ad.bits.AttrType = be16_to_cpu(OS_DEVICE_NAME);
			strcpy((char *)ae->un.OsDeviceName, LPFC_DRIVER_NAME);
			len = strlen((char *)ae->un.OsDeviceName);
			len += (len & 3) ? (4 - (len & 3)) : 4;
			ae->ad.bits.AttrLen = be16_to_cpu(FOURBYTES + len);
			pab->ab.EntryCnt++;
			size += FOURBYTES + len;

			if (clp[LPFC_CFG_FDMI_ON].a_current == 2) {
				/* #6 Port attribute entry */
				ae = (ATTRIBUTE_ENTRY *) ((uint8_t *) pab +
							  size);
				ae->ad.bits.AttrType = be16_to_cpu(HOST_NAME);
				lpfc_get_os_nameversion(GET_HOST_NAME, str);
				strcpy((char *)ae->un.HostName, (char *)str);
				len = strlen((char *)ae->un.HostName);
				len += (len & 3) ? (4 - (len & 3)) : 4;
				ae->ad.bits.AttrLen =
				    be16_to_cpu(FOURBYTES + len);
				pab->ab.EntryCnt++;
				size += FOURBYTES + len;
			}

			pab->ab.EntryCnt = be32_to_cpu(pab->ab.EntryCnt);
			/* Total size */
			size = GID_REQUEST_SZ - 4 + size;
		}
		break;

	case SLI_MGMT_DHBA:
		CtReq->CommandResponse.bits.CmdRsp = be16_to_cpu(SLI_MGMT_DHBA);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & phba->fc_sparam.portName,
		       sizeof (NAME_TYPE));
		size = GID_REQUEST_SZ - 4 + sizeof (NAME_TYPE);
		break;

	case SLI_MGMT_DPRT:
		CtReq->CommandResponse.bits.CmdRsp = be16_to_cpu(SLI_MGMT_DPRT);
		CtReq->CommandResponse.bits.Size = 0;
		pe = (PORT_ENTRY *) & CtReq->un.PortID;
		memcpy((uint8_t *) & pe->PortName,
		       (uint8_t *) & phba->fc_sparam.portName,
		       sizeof (NAME_TYPE));
		size = GID_REQUEST_SZ - 4 + sizeof (NAME_TYPE);
		break;
	}

	bpl = (ULP_BDE64 *) bmp->virt;
	bpl->addrHigh = le32_to_cpu( putPaddrHigh(mp->phys) );
	bpl->addrLow = le32_to_cpu( putPaddrLow(mp->phys) );
	bpl->tus.f.bdeFlags = 0;
	bpl->tus.f.bdeSize = size;
	bpl->tus.w = le32_to_cpu(bpl->tus.w);

	cmpl = lpfc_cmpl_ct_cmd_fdmi;

	if (lpfc_ct_cmd(phba, mp, bmp, ndlp, cmpl, FC_MAX_NS_RSP)) {
		lpfc_mbuf_free(phba, mp->virt, mp->phys);
		lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
		kfree(mp);
		kfree(bmp);
		/* Issue FDMI request failed */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0244,
			       lpfc_mes0244, lpfc_msgBlk0244.msgPreambleStr,
			       cmdcode);
		return (1);
	}
	return (0);
}

void
lpfc_cmpl_ct_cmd_fdmi(lpfcHBA_t * phba,
		      LPFC_IOCBQ_t * cmdiocb, LPFC_IOCBQ_t * rspiocb)
{
	DMABUF_t *bmp;
	DMABUF_t *inp;
	DMABUF_t *outp;
	SLI_CT_REQUEST *CTrsp;
	SLI_CT_REQUEST *CTcmd;
	LPFC_NODELIST_t *ndlp;
	uint16_t fdmi_cmd;
	uint16_t fdmi_rsp;

	inp = (DMABUF_t *) cmdiocb->context1;
	outp = (DMABUF_t *) cmdiocb->context2;
	bmp = (DMABUF_t *) cmdiocb->context3;

	CTcmd = (SLI_CT_REQUEST *) inp->virt;
	CTrsp = (SLI_CT_REQUEST *) outp->virt;

	ndlp = lpfc_findnode_did(phba, NLP_SEARCH_ALL, FDMI_DID);

	fdmi_rsp = CTrsp->CommandResponse.bits.CmdRsp;
	fdmi_cmd = CTcmd->CommandResponse.bits.CmdRsp;

	if (fdmi_rsp == be16_to_cpu(SLI_CT_RESPONSE_FS_RJT)) {
		/* FDMI rsp failed */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0220,
			       lpfc_mes0220, lpfc_msgBlk0220.msgPreambleStr,
			       be16_to_cpu(fdmi_cmd));
	}

	switch (be16_to_cpu(fdmi_cmd)) {
	case SLI_MGMT_RHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RPA);
		break;

	case SLI_MGMT_RPA:
		break;

	case SLI_MGMT_DHBA:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DPRT);
		break;

	case SLI_MGMT_DPRT:
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_RHBA);
		break;
	}

	lpfc_free_ct_rsp(phba, outp);
	lpfc_mbuf_free(phba, inp->virt, inp->phys);
	lpfc_mbuf_free(phba, bmp->virt, bmp->phys);
	kfree(inp);
	kfree(bmp);
	lpfc_iocb_free(phba, cmdiocb);
	return;
}

void
lpfc_fdmi_tmo(unsigned long ptr)
{
	lpfcHBA_t     *phba;
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


	ndlp = (LPFC_NODELIST_t *)clkData->clData1;
	clkData->timeObj->function = 0;
	list_del((struct list_head *)clkData);
	kfree(clkData);

	if (system_utsname.nodename[0] == '\0') {
		lpfc_start_timer(phba, 60, &phba->fc_fdmitmo, lpfc_fdmi_tmo, 
			(unsigned long)ndlp, (unsigned long)0);
	} else {
		phba->fc_fdmitmo.function = 0;
		lpfc_fdmi_cmd(phba, ndlp, SLI_MGMT_DHBA);
	}

out:
	LPFC_DRVR_UNLOCK(phba, iflag);
	return;
}


void
lpfc_decode_firmware_rev(lpfcHBA_t * phba, char *fwrevision, int flag)
{
	LPFC_SLI_t *psli;
	lpfc_vpd_t *vp;
	uint32_t b1, b2, b3, b4, ldata;
	char c;
	uint32_t i, rev;
	uint32_t *ptr, str[4];

	psli = &phba->sli;
	vp = &phba->vpd;
	if (vp->rev.rBit) {
		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE)
			rev = vp->rev.sli2FwRev;
		else
			rev = vp->rev.sli1FwRev;

		b1 = (rev & 0x0000f000) >> 12;
		b2 = (rev & 0x00000f00) >> 8;
		b3 = (rev & 0x000000c0) >> 6;
		b4 = (rev & 0x00000030) >> 4;

		switch (b4) {
		case 0:
			c = 'N';
			break;
		case 1:
			c = 'A';
			break;
		case 2:
			c = 'B';
			break;
		case 3:
		default:
			c = 0;
			break;
		}
		b4 = (rev & 0x0000000f);

		if (psli->sliinit.sli_flag & LPFC_SLI2_ACTIVE) {
			for (i = 0; i < 16; i++) {
				if (vp->rev.sli2FwName[i] == 0x20) {
					vp->rev.sli2FwName[i] = 0;
				}
			}
			ptr = (uint32_t *) vp->rev.sli2FwName;
		} else {
			for (i = 0; i < 16; i++) {
				if (vp->rev.sli1FwName[i] == 0x20) {
					vp->rev.sli1FwName[i] = 0;
				}
			}
			ptr = (uint32_t *) vp->rev.sli1FwName;
		}
		for (i = 0; i < 3; i++) {
			ldata = *ptr++;
			ldata = be32_to_cpu(ldata);
			str[i] = ldata;
		}

		if (c == 0) {
			if (flag)
				sprintf(fwrevision, "%d.%d%d (%s)",
					(int)b1, (int)b2, (int)b3, (char *)str);
			else
				sprintf(fwrevision, "%d.%d%d", (int)b1,
					(int)b2, (int)b3);
		} else {
			if (flag)
				sprintf(fwrevision, "%d.%d%d%c%d (%s)",
					(int)b1, (int)b2, (int)b3, c,
					(int)b4, (char *)str);
			else
				sprintf(fwrevision, "%d.%d%d%c%d",
					(int)b1, (int)b2, (int)b3, c, (int)b4);
		}
	} else {
		rev = vp->rev.smFwRev;

		b1 = (rev & 0xff000000) >> 24;
		b2 = (rev & 0x00f00000) >> 20;
		b3 = (rev & 0x000f0000) >> 16;
		c = (char)((rev & 0x0000ff00) >> 8);
		b4 = (rev & 0x000000ff);

		if (flag)
			sprintf(fwrevision, "%d.%d%d%c%d ", (int)b1,
				(int)b2, (int)b3, c, (int)b4);
		else
			sprintf(fwrevision, "%d.%d%d%c%d ", (int)b1,
				(int)b2, (int)b3, c, (int)b4);
	}
	return;
}

void
lpfc_get_hba_model_desc(lpfcHBA_t * phba, uint8_t * mdp, uint8_t * descp)
{
	lpfc_vpd_t *vp;
	uint16_t dev_id;
	uint16_t dev_subid;
	uint8_t hdrtype;
	char *model_str = "";  
	char *descr_str = "";

	vp = &phba->vpd;
	pci_read_config_word(phba->pcidev, PCI_DEVICE_ID, &dev_id);
	pci_read_config_byte(phba->pcidev, PCI_HEADER_TYPE, &hdrtype);

	switch (dev_id) {
	case PCI_DEVICE_ID_FIREFLY:
		model_str = "LP6000";
		descr_str = "Emulex LP6000 1Gb PCI Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_SUPERFLY:
		if (vp->rev.biuRev >= 1 && vp->rev.biuRev <= 3) {
			model_str = "LP7000";
			descr_str = "Emulex LP7000 1Gb PCI Fibre Channel Adapter";
		}
		else {
			model_str = "LP7000E";
			descr_str = "Emulex LP7000E 1Gb PCI Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_DRAGONFLY:
		model_str = "LP8000";
		descr_str = "Emulex LP8000 1Gb PCI Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_CENTAUR:
		if (FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID) {
			model_str = "LP9002";
			descr_str = "Emulex LP9002 2Gb PCI Fibre Channel Adapter";
		}
		else {
			model_str = "LP9000";
			descr_str = "Emulex LP9000 1Gb PCI Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_RFLY:
		model_str = "LP952";
		descr_str = "Emulex LP952 2Gb PCI Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_PEGASUS:
		model_str = "LP9802";
		descr_str = "Emulex LP9802 2Gb PCI-X Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_THOR:
		if (hdrtype == 0x80) {
			model_str = "LP10000DC";
			descr_str = "Emulex LP10000DC 2Gb 2-port PCI-X Fibre Channel Adapter";
		}
		else {
			model_str = "LP10000";
			descr_str = "Emulex LP10000 2Gb PCI-X Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_VIPER:
		model_str = "LPX1000";
		descr_str = "Emulex LPX1000 10Gb PCI-X Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_PFLY:
		model_str = "LP982";
		descr_str = "Emulex LP982 2Gb PCI-X Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_TFLY:
		if (hdrtype == 0x80) {
			model_str = "LP1050DC";
			descr_str = "Emulex LP1050DC 2Gb 2-port PCI-X Fibre Channel Adapter";
		}
		else {
			model_str = "LP1050";
			descr_str = "Emulex LP1050 2Gb PCI-X Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_HELIOS:
		if (hdrtype == 0x80) {
			model_str = "LP11002";
			descr_str = "Emulex LP11002 4Gb 2-port PCI-X2 Fibre Channel Adapter";
		}
		else {
			model_str = "LP11000";
			descr_str = "Emulex LP11000 4Gb PCI-X2 Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_HELIOS_SCSP:
		model_str = "LP11000-SP";
		descr_str = "Emulex LP11000-SP 4Gb PCI-X2 Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_HELIOS_DCSP:
		model_str = "LP11002-SP";
		descr_str = "Emulex LP11002-SP 4Gb 2-port PCI-X2 Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_NEPTUNE:
		if (hdrtype == 0x80) {
			model_str = "LPe1002";
			descr_str = "Emulex LPe1002 4Gb 2-port PCIe Fibre Channel Adapter";
		}
		else {
			model_str = "LPe1000";
			descr_str = "Emulex LPe1000 4Gb PCIe Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_NEPTUNE_SCSP:
		model_str = "LPe1000-SP";
		descr_str = "Emulex LPe1000-SP 4Gb PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_NEPTUNE_DCSP:
		model_str = "LPe1002-SP";
		descr_str = "Emulex LPe1002-SP 4Gb 2-port PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_BMID:
		model_str = "LP1150";
		descr_str = "Emulex LP1150 4Gb PCI-X2 Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_BSMB:
		model_str = "LP111";
		descr_str = "Emulex LP111 4Gb PCI-X2 Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_ZEPHYR:
		if (hdrtype == 0x80) {
			model_str = "LPe11002";
			descr_str = "Emulex LPe11002 4Gb 2-port PCIe Fibre Channel Adapter";
		}
		else {
			model_str = "LPe11000";
			descr_str = "Emulex LPe11000 4Gb PCIe Fibre Channel Adapter";
		}
		break;
	case PCI_DEVICE_ID_ZEPHYR_SCSP:
		model_str = "LPe11000-SP";
		descr_str = "Emulex LPe11000-SP 4Gb PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_ZEPHYR_DCSP:
		model_str = "LPe11002-SP";
		descr_str = "Emulex LPe11002-SP 4Gb 2-port PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_ZMID:
		model_str = "LPe1150";
		descr_str = "Emulex LPe1150 4Gb PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_ZSMB:
		model_str = "LPe111";
		descr_str = "Emulex LPe111 4Gb PCIe Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_LP101:
		model_str = "LP101";
		descr_str = "Emulex LP101 2Gb PCI-X Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_LP10000S:
		model_str = "LP10000-S";
		descr_str = "Emulex LP10000-S 2Gb PCI Fibre Channel Adapter";
		break;
	case PCI_DEVICE_ID_LP11000S:
	case PCI_DEVICE_ID_LPE11000S:
		pci_read_config_word(phba->pcidev, PCI_SUBSYSTEM_ID, &dev_subid);
		switch (dev_subid) {
		case PCI_SUBSYSTEM_ID_LP11000S:
			model_str = "LP11000-S";
			descr_str = "Emulex LP11000-S 4Gb PCI-X2 Fibre Channel Adapter";
			break;
		case PCI_SUBSYSTEM_ID_LP11002S:
			model_str = "LP11002-S";
			descr_str = "Emulex LP11002-S 4Gb 2-port PCI-X2 Fibre Channel Adapter";
			break;
		case PCI_SUBSYSTEM_ID_LPE11000S:
			model_str = "LPe11000-S";
			descr_str = "Emulex LPe11000-S 4Gb PCIe Fibre Channel Adapter";
			break;
		case PCI_SUBSYSTEM_ID_LPE11002S:
			model_str = "LPe11002-S";
			descr_str = "Emulex LPe11002-S 4Gb 2-port PCIe Fibre Channel Adapter";
			break;
		case PCI_SUBSYSTEM_ID_LPE11010S:
			model_str = "LPe11010-S";
			descr_str = "Emulex LPe11010-S 4Gb 10-port PCIe Fibre Channel Adapter";
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (mdp)
		sprintf(mdp, "%s", model_str);
	if (descp)
		sprintf(descp, "%s", descr_str);
}

void
lpfc_get_hba_sym_node_name(lpfcHBA_t * phba, uint8_t * symbp)
{
	char fwrev[16];

	lpfc_decode_firmware_rev(phba, fwrev, 0);
	sprintf(symbp, "Emulex %s FV%s DV%s", phba->ModelName, fwrev, lpfc_release_version);
}
