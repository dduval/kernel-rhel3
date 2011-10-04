/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


#define QLA_INIT_FDMI_CTIU_HDR(ha, ct_hdr)		\
    ct_hdr.revision = GS4_REVISION;			\
    ct_hdr.gs_type = GS_TYPE_MGMT_SERVER;		\
    ct_hdr.gs_subtype = GS_SUBTYPE_FDMI_HBA;

struct ct_info {
	uint16_t	ct_cmd;
	void		*pct_buf;
	dma_addr_t	ct_buf_dma_addr;
};


/* Local functions */
static __inline__ void
qla2x00_init_ms_mbx_iocb(scsi_qla_host_t *, uint32_t, uint32_t, uint16_t,
    uint16_t, uint16_t);

static __inline__ ms_iocb_entry_t *
qla2x00_alloc_ms_mbx_iocb(scsi_qla_host_t *);

static __inline__ void
qla2x00_free_ms_mbx_iocb(scsi_qla_host_t *);

static __inline__ int
qla2x00_fdmi_ctiu_mem_alloc(scsi_qla_host_t *, size_t);

static __inline__ void
qla2x00_fdmi_ctiu_mem_free(scsi_qla_host_t *, size_t);

static __inline__ void
qla2x00_fdmi_setup_hbaattr(scsi_qla_host_t *, hba_attr_t *);

static __inline__ void
qla2x00_fdmi_setup_rhbainfo(scsi_qla_host_t *, ct_iu_rhba_t *);

static __inline__ void
qla2x00_fdmi_setup_rhatinfo(scsi_qla_host_t *, ct_iu_rhat_t *);

static __inline__ void
qla2x00_fdmi_setup_rpainfo(scsi_qla_host_t *, ct_iu_rpa_t *);

static __inline__ void
qla2x00_fdmi_srb_init(scsi_qla_host_t *, srb_t *, uint16_t, uint16_t);

static __inline__ void
qla2x00_init_req_q_ms_iocb(scsi_qla_host_t *, ms_iocb_entry_t *,
    dma_addr_t, size_t, size_t, uint8_t, uint16_t, uint16_t);

static __inline__ int
qla2x00_fdmi_srb_tmpmem_alloc(scsi_qla_host_t *ha, srb_t *sp);

static __inline__ int
qla2x00_fdmi_sc_request_dev_bufs_alloc(scsi_qla_host_t *, Scsi_Cmnd *);

static __inline__ void
qla2x00_fdmi_sc_request_dev_bufs_free(Scsi_Cmnd *);

STATIC int
qla2x00_fdmi_cmnd_srb_alloc(scsi_qla_host_t *, srb_t **);

STATIC void
qla2x00_fdmi_cmnd_srb_free(scsi_qla_host_t *, srb_t *);

STATIC int
qla2x00_fdmi_rhba(scsi_qla_host_t *, uint8_t *);

STATIC int
qla2x00_fdmi_ghat(scsi_qla_host_t *, ct_iu_ghat_rsp_t *, uint8_t *);

STATIC int
qla2x00_fdmi_rpa(scsi_qla_host_t *, uint8_t *);

STATIC int
qla2x00_fdmi_dhba(scsi_qla_host_t *, uint8_t *);

STATIC int
qla2x00_fdmi_rhba_intr(scsi_qla_host_t *);

STATIC int
qla2x00_fdmi_rhat_intr(scsi_qla_host_t *, Scsi_Cmnd *, void *, dma_addr_t);

STATIC int
qla2x00_fdmi_rpa_intr(scsi_qla_host_t *, Scsi_Cmnd *, void *, dma_addr_t);

STATIC void
qla2x00_fdmi_done(Scsi_Cmnd *);

/* functions to export */
int
qla2x00_mgmt_svr_login(scsi_qla_host_t *ha);

void
qla2x00_fdmi_srb_tmpmem_free(srb_t *sp);

void
qla2x00_fdmi_register(scsi_qla_host_t *);

void
qla2x00_fdmi_register_intr(scsi_qla_host_t *);


/*
 * qla2x00_mgmt_svr_login
 *	Login management server.
 *
 * Input:
 *	ha:	adapter state pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_mgmt_svr_login(scsi_qla_host_t *ha)
{
	int		tmp_rval = 0;
	uint16_t	mb[MAILBOX_REGISTER_COUNT];

	DEBUG13(printk("%s(%ld): entered\n",
	    __func__, ha->host_no);)

	/* check on management server login status */
	if (ha->flags.management_server_logged_in == 0) {
		/* login to management server device */

		tmp_rval = qla2x00_login_fabric(ha, MANAGEMENT_SERVER,
		    0xff, 0xff, 0xfa, &mb[0], BIT_1);

		if (tmp_rval != 0 || mb[0] != 0x4000) {

	 		DEBUG2_13(printk(
			    "%s(%ld): inst=%ld ERROR login to MS.\n",
			    __func__, ha->host_no, ha->instance);)

			return (QL_STATUS_ERROR);
		}

		ha->flags.management_server_logged_in = 1;
		DEBUG13(printk("%s(%ld): success login to MS.\n",
		    __func__, ha->host_no);)
	}

	DEBUG13(printk("%s(%ld): exiting.\n",
	    __func__, ha->host_no);)

	return (QL_STATUS_SUCCESS);
}

/*
 * qla2x00_fdmi_register
 *	Uses execute iocb mbx command to perform fdmi registration
 *	functions.  If functions cannot be performed or returned
 *	error, just return without error, since this is not a critical
 *	function that must succeed.
 *	It is assumed the driver has already login to management svr.
 *
 * Input:
 *	ha:	adapter state pointer.
 *
 * Returns:
 *	void
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_fdmi_register(scsi_qla_host_t *ha)
{
	int		rval;
	uint8_t		fdmi_stat = FDMI_STAT_OK;
	ms_iocb_entry_t *ms_pkt;
	ct_iu_ghat_rsp_t tmp_hat_buf;
	ct_iu_rhba_t	hba_buf;

	DEBUG13(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	/* allocate MSIOCB */
	ms_pkt = qla2x00_alloc_ms_mbx_iocb(ha);

	if (ms_pkt == NULL) {
		/* error cannot perform register functions */
		DEBUG2_13(printk("%s(%ld): MSIOCB alloc failed.\n",
		    __func__, ha->host_no);)
		return;
	}

	/* register HBA */
	rval = qla2x00_fdmi_rhba(ha, &fdmi_stat);

	/* if already registered, get and compare HBA attributes */
	if (rval != QL_STATUS_SUCCESS) {
		if (fdmi_stat == FDMI_STAT_ALREADY_REGISTERED) {
			DEBUG2_13(printk("%s(%ld): HBA already registered. "
			    "Get/compare attributes.\n",
			    __func__, ha->host_no);)

			if (qla2x00_fdmi_ghat(ha, &tmp_hat_buf, &fdmi_stat)) {
				/* error */
				DEBUG2_13(printk("%s(%ld): GHAT failed. "
				    "De-registering.\n",
				    __func__, ha->host_no);)
				/* deregister and return */
				qla2x00_fdmi_dhba(ha, &fdmi_stat);
				return;
			}
		} else {
			/* error */
			DEBUG2_13(printk("%s(%ld): RHBA failed. exiting.\n",
			    __func__, ha->host_no);)
			return;
		}

		DEBUG13(printk("%s(%ld): ghat rsp buf dump:\n",
		    __func__, ha->host_no);)
		DEBUG13(qla2x00_dump_buffer((uint8_t*)&tmp_hat_buf.plist,
		    sizeof(reg_port_list_t) + sizeof(hba_attr_t));)

		/* rebuild hba values locally and compare; if different
		 * attribute values, de-register and register the hba.
		 */
		memset(&hba_buf, 0, sizeof(ct_iu_rhba_t));
		qla2x00_fdmi_setup_rhbainfo(ha, &hba_buf);

		DEBUG13(printk("%s(%ld): compare hba buf dump:\n",
		    __func__, ha->host_no);)
		DEBUG13(qla2x00_dump_buffer((uint8_t*)&hba_buf.plist,
		    sizeof(reg_port_list_t) + sizeof(hba_attr_t));)

		if (memcmp(&hba_buf.plist, &tmp_hat_buf.plist,
		    sizeof(reg_port_list_t) + sizeof(hba_attr_t)) != 0) {
			/* deregister and re-register */
			DEBUG13(printk("%s(%ld): different attributes already "
			    "registered. deregistering...\n",
			    __func__, ha->host_no);)

			rval = qla2x00_fdmi_dhba(ha, &fdmi_stat);
			if (rval != QL_STATUS_SUCCESS) {
				/* error */
				DEBUG2_13(printk("%s(%ld): DHBA failed.\n",
				    __func__, ha->host_no);)
				return;
			}

			DEBUG13(printk("%s(%ld): deregister HBA success. "
			    "re-registering...\n",
			    __func__, ha->host_no);)

			/* try again */
			rval = qla2x00_fdmi_rhba(ha, &fdmi_stat);
			if (rval != QL_STATUS_SUCCESS ||
			    fdmi_stat != FDMI_STAT_OK) {
				/* error */
				DEBUG2_13(printk(
				    "%s(%ld): RHBA failed again-exiting.\n",
				    __func__, ha->host_no);)
				return;
			}
		}
	}

	/* register port attributes.  This call should always succeed
	 * if the command is supported.
	 */
	qla2x00_fdmi_rpa(ha, &fdmi_stat);

	/* free MSIOCB */
	qla2x00_free_ms_mbx_iocb(ha);

	DEBUG13(printk("%s(%ld): exiting\n", __func__, ha->host_no);)
}

static __inline__ int
qla2x00_fdmi_srb_tmpmem_alloc(scsi_qla_host_t *ha, srb_t *sp)
{
	if (sp == NULL) {
		return (QL_STATUS_ERROR);
	}

	/* initialize for proper handling of error case */
	sp->tgt_queue = NULL;
	sp->lun_queue = NULL;
	sp->fclun = NULL;

	sp->tgt_queue = KMEM_ZALLOC(sizeof(os_tgt_t), 60);
	if (sp->tgt_queue == NULL) {
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in tmp tgt queue allocation.\n",
		    __func__, ha->host_no);)
		qla2x00_fdmi_srb_tmpmem_free(sp);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	sp->lun_queue = KMEM_ZALLOC(sizeof(os_lun_t), 61);
	if (sp->lun_queue == NULL) {
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in tmp lun queue allocation.\n",
		    __func__, ha->host_no);)
		qla2x00_fdmi_srb_tmpmem_free(sp);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	sp->fclun = KMEM_ZALLOC(sizeof(fc_lun_t), 62);
	if (sp->fclun == NULL) {
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in tmp fclun queue allocation.\n",
		    __func__, ha->host_no);)
		qla2x00_fdmi_srb_tmpmem_free(sp);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	sp->fclun->fcport = KMEM_ZALLOC(sizeof(fc_port_t), 63);
	if (sp->fclun->fcport == NULL) {
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in tmp fcport queue allocation.\n",
		    __func__, ha->host_no);)
		qla2x00_fdmi_srb_tmpmem_free(sp);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	return (QL_STATUS_SUCCESS);
}

void
qla2x00_fdmi_srb_tmpmem_free(srb_t *sp)
{
	if (sp->fclun != NULL) {
		if (sp->fclun->fcport != NULL) {
			KMEM_FREE(sp->fclun->fcport, sizeof(fc_port_t));
		}

		KMEM_FREE(sp->fclun, sizeof(fc_lun_t));
	}

	if (sp->lun_queue != NULL) {
		KMEM_FREE(sp->lun_queue, sizeof(os_lun_t));
	}

	if (sp->tgt_queue != NULL) {
		KMEM_FREE(sp->tgt_queue, sizeof(os_tgt_t));
	}

}

static __inline__ int
qla2x00_fdmi_sc_request_dev_bufs_alloc(scsi_qla_host_t *ha, Scsi_Cmnd *pcmd)
{

	pcmd->device = KMEM_ZALLOC(sizeof(Scsi_Device), 66);
	if (pcmd->device == NULL) {
		/* error */
		return (QL_STATUS_RESOURCE_ERROR);
	}

	pcmd->sc_request = KMEM_ZALLOC(sizeof(Scsi_Request), 67);
	if (pcmd->sc_request == NULL) {
		/* error */
		qla2x00_fdmi_sc_request_dev_bufs_free(pcmd);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	pcmd->sc_request->sr_buffer = KMEM_ZALLOC(sizeof(struct ct_info), 68);
	if (pcmd->sc_request->sr_buffer == NULL) {
		/* error */
		qla2x00_fdmi_sc_request_dev_bufs_free(pcmd);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	return (QL_STATUS_SUCCESS);
}

static __inline__ void
qla2x00_fdmi_sc_request_dev_bufs_free(Scsi_Cmnd *pcmd)
{
	if (pcmd->sc_request != NULL) {
		if (pcmd->sc_request->sr_buffer != NULL) {
			KMEM_FREE(pcmd->sc_request->sr_buffer,
			    sizeof(struct ct_info));
			pcmd->sc_request->sr_buffer = NULL;
		}

		KMEM_FREE(pcmd->sc_request, sizeof(Scsi_Request));
		pcmd->sc_request = NULL;
	}

	if (pcmd->device != NULL) {
		KMEM_FREE(pcmd->device, sizeof(Scsi_Device));
		pcmd->device = NULL;
	}
}

STATIC int
qla2x00_fdmi_cmnd_srb_alloc(scsi_qla_host_t *ha, srb_t **sp)
{
	struct ct_info	*pdata;
	void		*pctbuf;
	dma_addr_t	ctbuf_dma_addr;

	/* Allocate SRB block. */
	if ((*sp = qla2x00_get_new_sp(ha)) == NULL) {

		DEBUG2_13(printk("%s(%ld): ERROR cannot alloc sp.\n",
		    __func__, ha->host_no);)

		return (QL_STATUS_RESOURCE_ERROR);
	}

	if (qla2x00_fdmi_srb_tmpmem_alloc(ha, *sp)) {
		/* error */
		atomic_set(&(*sp)->ref_count, 0);
		add_to_free_queue(ha, *sp);
		*sp = NULL;

		return (QL_STATUS_RESOURCE_ERROR);
	}

	(*sp)->cmd = KMEM_ZALLOC(sizeof(Scsi_Cmnd), 64);
	if ((*sp)->cmd == NULL) {
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in scsi_cmnd mem allocation.\n",
		    __func__, ha->host_no);)
		qla2x00_fdmi_srb_tmpmem_free(*sp);
		return (QL_STATUS_RESOURCE_ERROR);
	}

	/* These buffers are used and freed during callback time */
	if (qla2x00_fdmi_sc_request_dev_bufs_alloc(ha, (*sp)->cmd)) {
		/* error */

		qla2x00_fdmi_srb_tmpmem_free(*sp);
		KMEM_FREE((*sp)->cmd, sizeof(Scsi_Cmnd));
		(*sp)->cmd = NULL;
		atomic_set(&(*sp)->ref_count, 0);
		add_to_free_queue(ha, *sp);
		*sp = NULL;

		return (QL_STATUS_RESOURCE_ERROR);
	}

	pctbuf = pci_alloc_consistent(ha->pdev, sizeof(ct_fdmi_pkt_t),
	    &ctbuf_dma_addr);
	if (pctbuf == NULL) {
		/* error */
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ERROR in ctiu mem allocation.\n",
		    __func__, ha->host_no);)

		qla2x00_fdmi_sc_request_dev_bufs_free((*sp)->cmd);
		qla2x00_fdmi_srb_tmpmem_free(*sp);
		KMEM_FREE((*sp)->cmd, sizeof(Scsi_Cmnd));
		(*sp)->cmd = NULL;
		atomic_set(&(*sp)->ref_count, 0);
		add_to_free_queue(ha, *sp);
		*sp = NULL;

		return (QL_STATUS_RESOURCE_ERROR);
	}
	/* sr_buffer is used to save some data for callback time */
	pdata = (*sp)->cmd->sc_request->sr_buffer;
	pdata->pct_buf = pctbuf;
	pdata->ct_buf_dma_addr = ctbuf_dma_addr;

	return (QL_STATUS_SUCCESS);
}

STATIC void
qla2x00_fdmi_cmnd_srb_free(scsi_qla_host_t *ha, srb_t *sp)
{
	struct ct_info	*pdata;

	if (sp == NULL) 
		return;

	if (sp->cmd->sc_request != NULL &&
	    (pdata = sp->cmd->sc_request->sr_buffer) != NULL) {
		if (pdata->pct_buf != NULL) {
			pci_free_consistent(ha->pdev, sizeof(ct_fdmi_pkt_t),
			    pdata->pct_buf, pdata->ct_buf_dma_addr);
		}
	}

	qla2x00_fdmi_sc_request_dev_bufs_free(sp->cmd);

	if (sp->cmd != NULL) {
		KMEM_FREE(sp->cmd, sizeof(Scsi_Cmnd));
		sp->cmd = NULL;
	}

	qla2x00_fdmi_srb_tmpmem_free(sp);

	atomic_set(&sp->ref_count, 0);
	add_to_free_queue(ha, sp);
}

STATIC void
qla2x00_fdmi_done(Scsi_Cmnd *pscsi_cmd)
{
	uint8_t			free_mem = TRUE;
	uint16_t		cmd_code;
	struct Scsi_Host	*host;
	scsi_qla_host_t 	*ha;
	struct ct_info		*pdata;
	ct_iu_preamble_t	*pct;

	host = pscsi_cmd->host;
	if (host == NULL) {
		/* error */
		DEBUG2_13(printk("%s: entered. no host found.\n", __func__);)
		return;
	}

	ha = (scsi_qla_host_t *) host->hostdata;

	DEBUG13(printk("%s(%ld): entered.\n", __func__ ,ha->host_no);)

	/* read data and free memory */
	if (pscsi_cmd->sc_request == NULL) {
		/* error */
		return;
	}
	if ((pdata = (struct ct_info *)pscsi_cmd->sc_request->sr_buffer) ==
	    NULL) {
		/* error */
		return;
	}

	pct = pdata->pct_buf;

	cmd_code = be16_to_cpu(pdata->ct_cmd);
	DEBUG13(printk("%s(%ld): got cmd %x, result=%x. rsp dump:\n",
	    __func__ ,ha->host_no, cmd_code, CMD_RESULT(pscsi_cmd));)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)pct, sizeof(ct_iu_preamble_t));)

	switch (cmd_code) {
	case FDMI_CC_RHBA:
		if (pct->cmd_rsp_code !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_13(printk("%s(%ld): RHBA failed, rejected "
			    "request, rhba_rsp:\n", __func__, ha->host_no);)
			DEBUG2_13(qla2x00_dump_buffer((uint8_t *)pct,
			    sizeof(ct_iu_preamble_t));)

			if ((pct->reason == FDMI_REASON_CANNOT_PERFORM) &&
			    (pct->explanation ==
			    FDMI_EXPL_HBA_ALREADY_REGISTERED)) {

				DEBUG13(printk(
				    "%s(%ld): RHBA already registered. "
				    "calling RHAT.\n",
				    __func__, ha->host_no);)

				qla2x00_fdmi_rhat_intr(ha, pscsi_cmd,
				    pdata->pct_buf, pdata->ct_buf_dma_addr);
				free_mem = FALSE;
			} else {
				/* error. just free mem and exit */
				DEBUG2_13(printk("%s(%ld): RHBA failed. "
				    "going to free memory.\n",
				    __func__, ha->host_no);)
			}
		} else {
			/* command completed. */
			DEBUG13(printk(
			    "%s(%ld): RHBA finished ok. going to call RPA.\n",
			    __func__, ha->host_no);)
			qla2x00_fdmi_rpa_intr(ha, pscsi_cmd, pdata->pct_buf,
			    pdata->ct_buf_dma_addr);
			free_mem = FALSE;
		}

		break;

	case FDMI_CC_RHAT:
		/* Just go ahead and issue next command */
		DEBUG13(printk("%s(%ld): RHAT rspcode=%x. going to call RPA.\n",
		    __func__, ha->host_no, be16_to_cpu(pct->cmd_rsp_code));)
		qla2x00_fdmi_rpa_intr(ha, pscsi_cmd, pdata->pct_buf,
		    pdata->ct_buf_dma_addr);
		free_mem = FALSE;
		break;

	case FDMI_CC_RPA:
		DEBUG13(printk("%s(%ld): got RPA rspcode=%x.\n",
		    __func__ ,ha->host_no, be16_to_cpu(pct->cmd_rsp_code));)

		/* This is assumed to be last command issued in this
		 * chain. Proceed to free memory.
		 */
		break;

	default:
		DEBUG13(printk("%s(%ld): cmd_code=%x not processed.\n",
		    __func__ ,ha->host_no, cmd_code);)
		break;
	}

	if (free_mem) {
		DEBUG13(printk("%s(%ld): going to free mem.\n",
		    __func__ ,ha->host_no);)
		if (pdata->pct_buf != NULL) {
			pci_free_consistent(ha->pdev, sizeof(ct_fdmi_pkt_t),
			    pdata->pct_buf, pdata->ct_buf_dma_addr);
		}
		qla2x00_fdmi_sc_request_dev_bufs_free(pscsi_cmd);
		KMEM_FREE(pscsi_cmd, sizeof(Scsi_Cmnd));
	}

	DEBUG13(printk("%s(%ld): exiting.\n", __func__ ,ha->host_no);)
}

static __inline__ void
qla2x00_fdmi_srb_init(scsi_qla_host_t *ha, srb_t *sp, uint16_t tov,
    uint16_t ct_cmd_code)
{
	struct ct_info	*pdata;

	/* setup sp for this command */
	sp->ha = ha;
	sp->flags = SRB_FDMI_CMD;
	sp->fclun->lun = 0;
	sp->fclun->flags = 0;
	sp->lun_queue->fclun = sp->fclun;
	sp->lun_queue->fclun->fcport->ha = ha;
	sp->lun_queue->q_state = LUN_STATE_READY;
	sp->lun_queue->q_lock = SPIN_LOCK_UNLOCKED;
	sp->tgt_queue->ha = ha;
	sp->tgt_queue->vis_port = sp->fclun->fcport;

	/* init scsi_cmd */
	CMD_SP(sp->cmd) = (void *)sp;
	sp->cmd->host = ha->host;
	sp->cmd->scsi_done = qla2x00_fdmi_done;
	CMD_TIMEOUT(sp->cmd) = tov;
	/* sr_buffer is used to save some data for callback time */
	pdata = (struct ct_info *)sp->cmd->sc_request->sr_buffer;
	pdata->ct_cmd = ct_cmd_code;
}

static __inline__ void
qla2x00_init_req_q_ms_iocb(scsi_qla_host_t *ha, ms_iocb_entry_t *ms_pkt,
    dma_addr_t ct_buf_dma, size_t req_buf_size, size_t rsp_buf_size,
    uint8_t loop_id, uint16_t ctrl_flags, uint16_t tov)
{
	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ct_buf_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ct_buf_dma));

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ct_buf_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ct_buf_dma));

#if defined(EXTENDED_IDS)
	ms_pkt->loop_id = __constant_cpu_to_le16(loop_id);
#else
	ms_pkt->loop_id = loop_id;
#endif
	ms_pkt->control_flags = __constant_cpu_to_le16(ctrl_flags);
	ms_pkt->timeout = __constant_cpu_to_le16(tov);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_buf_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_buf_size);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;
}

/*
 * qla2x00_fdmi_register_intr
 *	Uses request queue iocb to perform fdmi registration
 *	functions.  If functions cannot be performed or returned
 *	error, just return without error, since this is not a critical
 *	function that must succeed.
 *	It is assumed the driver has already login to management svr.
 *
 * Input:
 *	ha:	adapter state pointer.
 *
 * Returns:
 *	void
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_fdmi_register_intr(scsi_qla_host_t *ha)
{
	DEBUG13(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	/* start the chain of fdmi calls with RHBA */
	qla2x00_fdmi_rhba_intr(ha);

	DEBUG13(printk("%s(%ld): exiting\n", __func__, ha->host_no);)
}

static __inline__ int
qla2x00_fdmi_ctiu_mem_alloc(scsi_qla_host_t *ha, size_t buf_size)
{
	int	rval = QL_STATUS_SUCCESS;

	/* Get consistent memory allocated for CT commands */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    buf_size, &ha->ct_iu_dma);
	}

	if (ha->ct_iu == NULL) {
		 /* error */
		DEBUG2_13(printk(KERN_WARNING
		    "%s(%ld): ct_iu Memory Allocation failed.\n",
		    __func__, ha->host_no);)
		return (QL_STATUS_ERROR);
	}

	memset(ha->ct_iu, 0, buf_size);

	return (rval);
}

static __inline__ void
qla2x00_fdmi_ctiu_mem_free(scsi_qla_host_t *ha, size_t buf_size)
{
	if (ha->ct_iu != NULL) {
		pci_free_consistent(ha->pdev,
		    buf_size, ha->ct_iu, ha->ct_iu_dma);
		ha->ct_iu = NULL;
	}
}

static __inline__ ms_iocb_entry_t *
qla2x00_alloc_ms_mbx_iocb(scsi_qla_host_t *ha)
{
	ms_iocb_entry_t *ms_pkt;

	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "%s(%ld): msiocb Memory Allocation failed.\n",
		    __func__, ha->host_no);
		return (NULL);
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT commands */
	if (qla2x00_fdmi_ctiu_mem_alloc(ha, sizeof(ct_fdmi_pkt_t)) !=
	    QL_STATUS_SUCCESS) {
		printk(KERN_WARNING
		    "%s(%ld): ct_iu Memory Allocation failed.\n",
		    __func__, ha->host_no);
		qla2x00_free_ms_mbx_iocb(ha);
		return (NULL);
	}

	/* Initialize some common fields */
	ms_pkt = ha->ms_iocb;

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));

	return (ms_pkt);
}

static __inline__ void
qla2x00_free_ms_mbx_iocb(scsi_qla_host_t *ha)
{
	if (ha->ms_iocb != NULL){
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		ha->ms_iocb = NULL;
	}

	qla2x00_fdmi_ctiu_mem_free(ha, sizeof(ct_fdmi_pkt_t));

}

static __inline__ void
qla2x00_init_ms_mbx_iocb(scsi_qla_host_t *ha, uint32_t req_size,
    uint32_t rsp_size, uint16_t loop_id, uint16_t ctrl_flags, uint16_t tov)
{
	ms_iocb_entry_t *ms_pkt;

	ms_pkt = ha->ms_iocb;

#if defined(EXTENDED_IDS)
	ms_pkt->loop_id = __constant_cpu_to_le16(loop_id);
#else
	ms_pkt->loop_id = loop_id;
#endif
	ms_pkt->control_flags = __constant_cpu_to_le16(ctrl_flags);
	ms_pkt->timeout = __constant_cpu_to_le16(tov);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_size);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

}

static __inline__ void
qla2x00_fdmi_setup_hbaattr(scsi_qla_host_t *ha, hba_attr_t *attr)
{
	char		tmp_str[80];
	uint32_t        tmp_sn;
	qla_boards_t	*bdp;

	attr->count = __constant_cpu_to_be32(HBA_ATTR_COUNT);

	/* node name */
	attr->nn.type = __constant_cpu_to_be16(T_NODE_NAME);
	attr->nn.len = cpu_to_be16(sizeof(hba_nn_attr_t));
	memcpy(attr->nn.value, ha->node_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): NODENAME=%02x%02x%02x%02x%02x"
	    "%02x%02x%02x.\n",
	    __func__, ha->host_no, attr->nn.value[0],
	    attr->nn.value[1], attr->nn.value[2], attr->nn.value[3],
	    attr->nn.value[4], attr->nn.value[5], attr->nn.value[6],
	    attr->nn.value[7]);)

	/* company name */
	attr->man.type = __constant_cpu_to_be16(T_MANUFACTURER);
	attr->man.len = cpu_to_be16(sizeof(hba_man_attr_t));
	sprintf((char *)attr->man.value, QLOGIC_COMPANY_NAME);

	DEBUG13(printk("%s(%ld): MANUFACTURER=%s.\n",
	    __func__, ha->host_no, attr->man.value);)

	/* serial number */
	attr->sn.type = __constant_cpu_to_be16(T_SERIAL_NUMBER);
	attr->sn.len = cpu_to_be16(sizeof(hba_sn_attr_t));
	tmp_sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | 
	    ha->serial1;
	sprintf((char *)attr->sn.value, "%c%05d",
	    ('A' + tmp_sn/100000), (tmp_sn%100000));

	DEBUG13(printk("%s(%ld): SERIALNO=%s.\n",
	    __func__, ha->host_no, attr->sn.value);)

	/* model name */
	attr->mod.type = __constant_cpu_to_be16(T_MODEL);
	attr->mod.len = cpu_to_be16(sizeof(hba_mod_attr_t));
	strncpy((char *)attr->mod.value, ha->model_number, NVRAM_MODEL_SIZE);

	DEBUG13(printk("%s(%ld): MODEL_NAME=%s.\n",
	    __func__, ha->host_no, attr->mod.value);)

	/* model description */
	attr->mod_desc.type = __constant_cpu_to_be16(T_MODEL_DESCRIPTION);
	attr->mod_desc.len = cpu_to_be16(sizeof(hba_mod_desc_attr_t));
	strncpy((char *)attr->mod_desc.value, ha->model_desc, 80);

	DEBUG13(printk("%s(%ld): MODEL_DESC=%s.\n",
	    __func__, ha->host_no, attr->mod_desc.value);)

	/* hardware version */
	attr->hv.type = __constant_cpu_to_be16(T_HARDWARE_VERSION);
	attr->hv.len = cpu_to_be16(sizeof(hba_hv_attr_t));
	/* hw_id_version contains either valid hw_id for 2312 and later NVRAM
	 * or NULLs.
	 */
	strncpy((char *)attr->hv.value, ha->hw_id_version, NVRAM_HW_ID_SIZE);

	DEBUG13(printk("%s(%ld): HARDWAREVER=%s.\n",
	    __func__, ha->host_no, attr->hv.value);)

	/* driver version */
	attr->dv.type = __constant_cpu_to_be16(T_DRIVER_VERSION);
	attr->dv.len = cpu_to_be16(sizeof(hba_dv_attr_t));
	strncpy((char *)attr->dv.value, qla2x00_version_str,
	    QLA_DRVR_VERSION_LEN);

	DEBUG13(printk("%s(%ld): DRIVERVER=%s.\n",
	    __func__, ha->host_no, qla2x00_version_str);)

	/* opt rom version */
	attr->or.type = __constant_cpu_to_be16(T_OPTION_ROM_VERSION);
	attr->or.len = cpu_to_be16(sizeof(hba_or_attr_t));
	sprintf((char *)attr->or.value, "%d.%d", ha->bios_revision[1],
	    ha->bios_revision[0]);

	DEBUG13(printk("%s(%ld): OPTROMVER=%s.\n",
	    __func__, ha->host_no, attr->or.value);)

	/* firmware version */
	attr->fw.type = __constant_cpu_to_be16(T_FIRMWARE_VERSION);
	attr->fw.len = cpu_to_be16(sizeof(hba_fw_attr_t));
	bdp = &QLBoardTbl_fc[ha->devnum];
	sprintf((char *)attr->fw.value, "%2d.%02d.%02d", bdp->fwver[0],
	    bdp->fwver[1], bdp->fwver[2]);

	DEBUG13(printk("%s(%ld): FIRMWAREVER=%s.\n",
	    __func__, ha->host_no, attr->fw.value);)

	/* OS name/version */
	attr->os.type = __constant_cpu_to_be16(T_OS_NAME_AND_VERSION);
	attr->os.len = cpu_to_be16(sizeof(hba_os_attr_t));
	sprintf(tmp_str, "%s %s", system_utsname.sysname,
	    system_utsname.release);
	strncpy((char *)attr->os.value, tmp_str, 16);

	DEBUG13(printk("%s(%ld): OSNAME=%s.\n",
	    __func__, ha->host_no, tmp_str);)
}

static __inline__ void
qla2x00_fdmi_setup_rhbainfo(scsi_qla_host_t *ha, ct_iu_rhba_t *ct)
{
	memcpy(ct->hba_identifier, ha->port_name, WWN_SIZE);
	ct->plist.num_ports = __constant_cpu_to_be32(1);
	memcpy(ct->plist.port_entry, ha->port_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): RHBA identifier=%02x%02x%02x%02x%02x"
	    "%02x%02x%02x.\n",
	    __func__, ha->host_no, ct->hba_identifier[0],
	    ct->hba_identifier[1], ct->hba_identifier[2], ct->hba_identifier[3],
	    ct->hba_identifier[4], ct->hba_identifier[5], ct->hba_identifier[6],
	    ct->hba_identifier[7]);)

	qla2x00_fdmi_setup_hbaattr(ha, &ct->attr);
}

static __inline__ void
qla2x00_fdmi_setup_rhatinfo(scsi_qla_host_t *ha, ct_iu_rhat_t *ct)
{
	memcpy(ct->hba_identifier, ha->port_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): RHAT identifier=%02x%02x%02x%02x%02x"
	    "%02x%02x%02x.\n",
	    __func__, ha->host_no, ct->hba_identifier[0],
	    ct->hba_identifier[1], ct->hba_identifier[2], ct->hba_identifier[3],
	    ct->hba_identifier[4], ct->hba_identifier[5], ct->hba_identifier[6],
	    ct->hba_identifier[7]);)

	qla2x00_fdmi_setup_hbaattr(ha, &ct->attr);
}

/*
 * qla2x00_fdmi_rhba
 *	FDMI register HBA via execute IOCB mbx cmd.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	pret_stat:	local fdmi return status pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fdmi_rhba(scsi_qla_host_t *ha, uint8_t *pret_stat)
{
	int		rval;
	ct_iu_rhba_t	*ct;


	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* init */
	*pret_stat = FDMI_STAT_OK;

	/* Prepare common MS IOCB- Request/Response size adjusted. tov same
	 * as mailbox tov.
	 */
	qla2x00_init_ms_mbx_iocb(ha, sizeof(ct_iu_rhba_t),
	    sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    (CF_READ | CF_HEAD_TAG), 60 - 1);

	DEBUG13(printk("%s(%ld): done msiocb init.\n", __func__, ha->host_no);)

	/* Prepare CT request */
	memset(ha->ct_iu, 0, sizeof(ct_fdmi_pkt_t));
	ct = (ct_iu_rhba_t *)ha->ct_iu;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ct->hdr);
	ct->hdr.cmd_rsp_code = __constant_cpu_to_be16(FDMI_CC_RHBA);

	/* Setup register hba payload. */
	qla2x00_fdmi_setup_rhbainfo(ha, ct);

	DEBUG13(printk("%s(%ld): done ct init. ct buf dump:\n",
	    __func__, ha->host_no);)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ct,
	    sizeof(ct_iu_rhba_t));)
	DEBUG13(printk("msiocb buf dump:.\n");)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ha->ms_iocb,
	    sizeof(ms_iocb_entry_t));)

	/* Go issue command and wait for completion. */
	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_13(printk("%s(%ld): RHBA issue IOCB failed (%d).\n",
		    __func__, ha->host_no, rval);)
		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	} else if (ct->hdr.cmd_rsp_code !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_13(printk("%s(%ld): RHBA failed, rejected "
		    "request, rhba_rsp:\n", __func__, ha->host_no);)
		DEBUG2_13(qla2x00_dump_buffer((uint8_t *)&ct->hdr,
		    sizeof(ct_iu_preamble_t));)

		if ((ct->hdr.reason == FDMI_REASON_CANNOT_PERFORM) &&
		    (ct->hdr.explanation == FDMI_EXPL_HBA_ALREADY_REGISTERED)) {
			*pret_stat = FDMI_STAT_ALREADY_REGISTERED;
			DEBUG2_13(printk("%s(%ld): HBA already registered.\n",
			    __func__, ha->host_no);)
		}

		rval = QL_STATUS_ERROR;
	}

	DEBUG13(printk("%s(%ld): exiting.\n", __func__ ,ha->host_no);)

	return (rval);
}

/*
 * qla2x00_fdmi_ghat
 *	FDMI get HBA attributes via execute IOCB mbx cmd.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	prsp_buf:	local ghat response buffer pointer.
 *	pret_stat:	local fdmi return status pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fdmi_ghat(scsi_qla_host_t *ha, ct_iu_ghat_rsp_t *prsp_buf,
    uint8_t *pret_stat)
{
	int			rval;
	ct_iu_ghat_req_t	*ct_req;
	ct_iu_ghat_rsp_t	*ct_rsp;
	ct_fdmi_pkt_t		*ctbuf;

	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* init */
	*pret_stat = FDMI_STAT_OK;

	/* Prepare common MS IOCB- Request/Response size adjusted. tov
	 * same as mailbox tov.
	 */
	qla2x00_init_ms_mbx_iocb(ha, sizeof(ct_iu_ghat_req_t),
	    sizeof(ct_iu_ghat_rsp_t), MANAGEMENT_SERVER,
	    (CF_READ | CF_HEAD_TAG), 60 - 1);

	DEBUG13(printk("%s(%ld): done msiocb init.\n", __func__, ha->host_no);)

	/* Prepare CT request */
	memset(ha->ct_iu, 0, sizeof(ct_fdmi_pkt_t));
	ctbuf = (ct_fdmi_pkt_t *)ha->ct_iu;
	ct_req = (ct_iu_ghat_req_t *)ha->ct_iu;
	ct_rsp = (ct_iu_ghat_rsp_t *)ha->ct_iu;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ct_req->hdr);
	ct_req->hdr.cmd_rsp_code = __constant_cpu_to_be16(FDMI_CC_GHAT);

	/* Setup get hba attrib payload. */
	memcpy(ct_req->hba_identifier, ha->port_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): done ct init. ct buf dump:\n",
	    __func__, ha->host_no);)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ct_req,
	    sizeof(ct_fdmi_pkt_t));)
	DEBUG13(printk("msiocb buf dump:.\n");)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ha->ms_iocb,
	    sizeof(ms_iocb_entry_t));)

	/* Go issue command and wait for completion. */
	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QL_STATUS_SUCCESS || ct_rsp->hdr.cmd_rsp_code !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_13(printk("%s(%ld): GHAT IOCB failed=%d. rspcode=%x, "
		    "ct rspbuf:\n",
		    __func__, ha->host_no, rval,
		    be16_to_cpu(ct_rsp->hdr.cmd_rsp_code));)
		DEBUG2_13(qla2x00_dump_buffer((uint8_t *)ct_rsp,
		    sizeof(ct_iu_ghat_rsp_t));)

		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	} else {
		/* copy response */
		memcpy(prsp_buf, ct_rsp, sizeof(ct_iu_ghat_rsp_t));
	}

	DEBUG13(printk("%s(%ld): exiting.\n", __func__ ,ha->host_no);)

	return (rval);
}

static __inline__ void
qla2x00_fdmi_setup_rpainfo(scsi_qla_host_t *ha, ct_iu_rpa_t *ct)
{
	/* Setup register port payload. */
	memcpy(ct->portname, ha->port_name, WWN_SIZE);

	ct->attr.count = __constant_cpu_to_be32(PORT_ATTR_COUNT);

	/* FC4 types */
	ct->attr.fc4_types.type = __constant_cpu_to_be16(T_FC4_TYPES);
	ct->attr.fc4_types.len = cpu_to_be16(sizeof(port_fc4_attr_t));
#if defined(ISP2300)
	if (ha->flags.enable_ip)
		ct->attr.fc4_types.value[3] = 0x20; /* type 5 for IP */
#endif
	ct->attr.fc4_types.value[2] = 0x01;	/* SCSI - FCP */

	DEBUG13(printk("%s(%ld): register fc4types=%02x %02x.\n",
	    __func__, ha->host_no, ct->attr.fc4_types.value[3],
	    ct->attr.fc4_types.value[2]);)

	/* Supported speed */
	ct->attr.sup_speed.type = __constant_cpu_to_be16(T_SUPPORT_SPEED);
	ct->attr.sup_speed.len = cpu_to_be16(sizeof(port_speed_attr_t));
#if defined(ISP2100) || defined (ISP2200)
	ct->attr.sup_speed.value = __constant_cpu_to_be32(1);	/* 1 Gig */
#elif defined(ISP2300)
	if (check_25xx_device_ids(ha)) {
		ct->attr.sup_speed.value = __constant_cpu_to_be32(8);	/* 8 Gig */
	} else if (check_24xx_or_54xx_device_ids(ha)) { 
		ct->attr.sup_speed.value = __constant_cpu_to_be32(4);	/* 4 Gig */
	} else {
		ct->attr.sup_speed.value = __constant_cpu_to_be32(2);	/* 2 Gig */
	}
#endif

	DEBUG13(printk("%s(%ld): register SUPPSPEED=%x.\n",
	    __func__, ha->host_no, ct->attr.sup_speed.value);)

	/* Current speed */
	ct->attr.cur_speed.type = __constant_cpu_to_be16(T_CURRENT_SPEED);
	ct->attr.cur_speed.len = cpu_to_be16(sizeof(port_speed_attr_t));
	switch (ha->current_speed) {
	case EXT_DEF_PORTSPEED_1GBIT:
		ct->attr.cur_speed.value = __constant_cpu_to_be32(1);
		break;
	case EXT_DEF_PORTSPEED_2GBIT:
		ct->attr.cur_speed.value = __constant_cpu_to_be32(2);
		break;
	case EXT_DEF_PORTSPEED_4GBIT:
		ct->attr.cur_speed.value = __constant_cpu_to_be32(4);
		break;
	case EXT_DEF_PORTSPEED_8GBIT:
		ct->attr.cur_speed.value = __constant_cpu_to_be32(8);
		break;
	}

	DEBUG13(printk("%s(%ld): register CURRSPEED=%x.\n",
	    __func__, ha->host_no, ct->attr.cur_speed.value);)

	/* Max frame size */
	ct->attr.max_fsize.type = __constant_cpu_to_be16(T_MAX_FRAME_SIZE);
	ct->attr.max_fsize.len = cpu_to_be16(sizeof(port_frame_attr_t));
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		struct init_cb_24xx *icb = (struct init_cb_24xx *)ha->init_cb;
		ct->attr.max_fsize.value =
			    cpu_to_be32((uint32_t)icb->frame_payload_size);
	} else {	
		init_cb_t *icb = (init_cb_t *)ha->init_cb;
		ct->attr.max_fsize.value =
			    cpu_to_be32((uint32_t)icb->frame_length);
	}
#else
	init_cb_t *icb = (init_cb_t *)ha->init_cb;
	ct->attr.max_fsize.value =
	    cpu_to_be32((uint32_t)icb->frame_length);
#endif

	DEBUG13(printk("%s(%ld): register MAXFSIZE=%d.\n",
	    __func__, ha->host_no, ct->attr.max_fsize.value);)

	/* OS device name */
	ct->attr.os_dev_name.type = __constant_cpu_to_be16(T_OS_DEVICE_NAME);
	ct->attr.os_dev_name.len = cpu_to_be16(sizeof(port_os_attr_t));
	/* register same string used/returned by SNIA HBA API */
#if defined(ISP2100)
	sprintf((char *)ct->attr.os_dev_name.value, "/proc/scsi/qla2100/%ld",
	    ha->host_no);
#elif defined(ISP2200)
	sprintf((char *)ct->attr.os_dev_name.value, "/proc/scsi/qla2200/%ld",
	    ha->host_no);
#elif defined(ISP2300)
	sprintf((char *)ct->attr.os_dev_name.value, "/proc/scsi/qla2300/%ld",
	    ha->host_no);
#endif
	DEBUG13(printk("%s(%ld): register OSDEVNAME=%s.\n",
	    __func__, ha->host_no, ct->attr.os_dev_name.value);)

	/* Host name */
	ct->attr.host_name.type = __constant_cpu_to_be16(T_HOST_NAME);
	ct->attr.host_name.len = cpu_to_be16(sizeof(port_host_name_attr_t));
	strcpy((char *)ct->attr.host_name.value, system_utsname.nodename);

	DEBUG13(printk("%s(%ld): register HOSTNAME=%s.\n",
	    __func__, ha->host_no, ct->attr.host_name.value);)
}

/*
 * qla2x00_fdmi_rpa
 *	FDMI register port attributes via execute IOCB mbx cmd.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	pret_stat:	local fdmi return status pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fdmi_rpa(scsi_qla_host_t *ha, uint8_t *pret_stat)
{
	int		rval;
	ct_iu_rpa_t	*ct;

	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* init */
	*pret_stat = FDMI_STAT_OK;

	/* Prepare common MS IOCB- Request/Response size adjusted */
	/* tov same as mbx tov */
	qla2x00_init_ms_mbx_iocb(ha, sizeof(ct_iu_rpa_t),
	    sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    (CF_READ | CF_HEAD_TAG), 60 - 1);

	DEBUG13(printk("%s(%ld): done msiocb init.\n", __func__, ha->host_no);)

	/* Prepare CT request */
	memset(ha->ct_iu, 0, sizeof(ct_fdmi_pkt_t));
	ct = (ct_iu_rpa_t *)ha->ct_iu;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ct->hdr);
	ct->hdr.cmd_rsp_code = __constant_cpu_to_be16(FDMI_CC_RPA);

	/* Setup register port attribute payload. */
	qla2x00_fdmi_setup_rpainfo(ha, ct);

	DEBUG13(printk("%s(%ld): done ct init. ct buf dump:\n",
	    __func__, ha->host_no);)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ct,
	    sizeof(ct_iu_rpa_t));)
	DEBUG13(printk("msiocb buf dump:.\n");)
	DEBUG13(qla2x00_dump_buffer((uint8_t *)ha->ms_iocb,
	    sizeof(ms_iocb_entry_t));)

	/* Go issue command and wait for completion. */
	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2_13(printk("%s(%ld): RPA issue IOCB failed (%d).\n",
		    __func__, ha->host_no, rval);)
		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	} else if (ct->hdr.cmd_rsp_code !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {

		DEBUG2_13(printk("%s(%ld): RPA failed, rejected "
		    "request, rhba_rsp:\n", __func__, ha->host_no);)
		DEBUG2_13(qla2x00_dump_buffer((uint8_t *)&ct->hdr,
		    sizeof(ct_iu_preamble_t));)
		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	}

	DEBUG13(printk("%s(%ld): exiting.\n", __func__ ,ha->host_no);)

	return (rval);
}

/*
 * qla2x00_fdmi_dhba
 *	FDMI de-register HBA.
 *
 * Input:
 *	ha:		adapter state pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fdmi_dhba(scsi_qla_host_t *ha, uint8_t *pret_stat)
{
	int		rval;
	ct_iu_dhba_t	*ct;


	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* init */
	*pret_stat = FDMI_STAT_OK;

	/* Prepare common MS IOCB- Request/Response size adjusted. tov
	 * same as mbx tov.
	 */
	qla2x00_init_ms_mbx_iocb(ha, sizeof(ct_iu_dhba_t),
	    sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    (CF_HEAD_TAG), 60 - 1);

	DEBUG13(printk("%s(%ld): done msiocb init.\n", __func__, ha->host_no);)

	/* Prepare CT request */
	memset(ha->ct_iu, 0, sizeof(ct_fdmi_pkt_t));
	ct = (ct_iu_dhba_t *)ha->ct_iu;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ct->hdr);
	ct->hdr.cmd_rsp_code = __constant_cpu_to_be16(FDMI_CC_DHBA);

	/* Setup deregister hba payload. */
	memcpy(ct->hba_portname, ha->port_name, WWN_SIZE);

	DEBUG13(printk("%s(%ld): done ct init.\n", __func__, ha->host_no);)

	/* Go issue command and wait for completion. */
	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2_13(printk("%s(%ld): DHBA issue IOCB failed (%d).\n",
		    __func__, ha->host_no, rval);)
		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	} else if (ct->hdr.cmd_rsp_code !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {

		DEBUG2_13(printk("%s(%ld): DHBA failed, rejected "
		    "request, dhba_rsp:\n", __func__, ha->host_no);)
		DEBUG2_13(qla2x00_dump_buffer((uint8_t *)&ct->hdr,
		    sizeof(ct_iu_preamble_t));)
		*pret_stat = FDMI_STAT_ERR;
		rval = QL_STATUS_ERROR;
	}

	DEBUG13(printk("%s(%ld): exiting.\n", __func__ ,ha->host_no);)

	return (rval);
}

/*
 * qla2x00_fdmi_rhba_intr
 *	FDMI register HBA sent via regular request queue.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		device context pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fdmi_rhba_intr(scsi_qla_host_t *ha)
{
	int			rval = QL_STATUS_SUCCESS;
	uint16_t		tov;
	unsigned long		cpu_flags = 0;
	struct ct_info		*pdata;
	ct_iu_rhba_t		*ct;
	ms_iocb_entry_t		*pkt;
	srb_t			*sp;

	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	if ((rval = qla2x00_fdmi_cmnd_srb_alloc(ha, &sp))){
		DEBUG2_13(printk("%s(%ld): cmd_srb_alloc failed.\n",
		    __func__, ha->host_no);)
		return (rval);
	}

	tov = ha->login_timeout*2;

	DEBUG13(printk("%s(%ld): going to srb_init\n", __func__, ha->host_no);)

	qla2x00_fdmi_srb_init(ha, sp, tov,
	    __constant_cpu_to_be16(FDMI_CC_RHBA));

	DEBUG13(printk("%s(%ld): done srb_init\n", __func__, ha->host_no);)

	pdata = (struct ct_info *)sp->cmd->sc_request->sr_buffer;
	ct = (ct_iu_rhba_t *)pdata->pct_buf;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ct->hdr);
	ct->hdr.cmd_rsp_code = pdata->ct_cmd;

	/* Setup register hba payload. */
	qla2x00_fdmi_setup_rhbainfo(ha, ct);

	DEBUG13(printk("%s(%ld): done setup_rhbainfo\n", __func__, ha->host_no);)

	/* get spin lock for this operation */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);

	/* Get MS request IOCB from request queue. */
	pkt = (ms_iocb_entry_t *)qla2x00_ms_req_pkt(ha, sp);
	if (pkt == NULL) {
		/* release spin lock and return error. */
		DEBUG2_13(printk("%s(%ld): no pkt. going to unlock "
		    "and free mem.\n", __func__, ha->host_no);)

		spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

		qla2x00_fdmi_cmnd_srb_free(ha, sp);

		DEBUG2_13(printk("%s(%ld): MSIOCB - could not get "
		    "Request Packet.\n", __func__, ha->host_no);)

		return (QL_STATUS_RESOURCE_ERROR);
	}

	DEBUG13(printk(KERN_INFO "%s(%ld): going to init_req_q_msiocb\n",
	    __func__, ha->host_no);)
	qla2x00_init_req_q_ms_iocb(ha, pkt, pdata->ct_buf_dma_addr,
	    sizeof(ct_iu_rhba_t), sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    0, tov);

	/* Issue command to ISP */
	DEBUG13(printk("%s(%ld): going to call isp_cmd.\n",
	    __func__, ha->host_no);)

	qla2x00_isp_cmd(ha);

	DEBUG13(printk("%s(%ld): going to add timer.\n",
	    __func__, ha->host_no);)

	qla2x00_add_timer_to_cmd(sp, tov + 2);

	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	DEBUG13(printk("%s(%ld): exiting\n", __func__, ha->host_no);)

	return (rval);
}

STATIC int
qla2x00_fdmi_rhat_intr(scsi_qla_host_t *ha, Scsi_Cmnd *pscsi_cmd, void *pct_buf,
    dma_addr_t ct_buf_dma_addr)
{
	int			rval = QL_STATUS_SUCCESS;
	uint16_t		tov;
	unsigned long		cpu_flags = 0;
	struct ct_info		*pdata;
	ct_iu_rhat_t		*cth;
	ms_iocb_entry_t		*pkt;
	srb_t			*sp;

	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* Allocate SRB block. */
	if ((sp = qla2x00_get_new_sp(ha)) == NULL) {

		DEBUG2_13(printk("%s(%ld): ERROR cannot alloc sp.\n",
		    __func__, ha->host_no);)

		return (QL_STATUS_RESOURCE_ERROR);
	}

	DEBUG13(printk("%s(%ld): got sp\n", __func__, ha->host_no);)

	if (qla2x00_fdmi_srb_tmpmem_alloc(ha, sp)) {
		/* error */
		atomic_set(&(sp)->ref_count, 0);
		add_to_free_queue(ha, sp);
		sp = NULL;

		return (QL_STATUS_RESOURCE_ERROR);
	}

	sp->cmd = pscsi_cmd;

	tov = ha->login_timeout*2;
	qla2x00_fdmi_srb_init(ha, sp, tov,
	    __constant_cpu_to_be16(FDMI_CC_RHAT));

	DEBUG13(printk("%s(%ld): done srb_init\n", __func__, ha->host_no);)

	pdata = (struct ct_info *)sp->cmd->sc_request->sr_buffer;
	cth = (ct_iu_rhat_t *)pdata->pct_buf;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, cth->hdr);
	cth->hdr.cmd_rsp_code = pdata->ct_cmd;

	/* Setup register hba payload. */
	qla2x00_fdmi_setup_rhatinfo(ha, cth);

	DEBUG13(printk("%s(%ld): done setup_rhatinfo\n", __func__, ha->host_no);)

	/* get spin lock for this operation */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);

	/* Get MS request IOCB from request queue. */
	pkt = (ms_iocb_entry_t *)qla2x00_ms_req_pkt(ha, sp);
	if (pkt == NULL) {
		/* release spin lock and return error. */
		spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

		qla2x00_fdmi_cmnd_srb_free(ha, sp);

		DEBUG2_13(printk("%s(%ld): inst=%ld MSIOCB - could not get "
		    "Request Packet.\n", __func__, ha->host_no, ha->instance);)
		return (QL_STATUS_RESOURCE_ERROR);
	}

	qla2x00_init_req_q_ms_iocb(ha, pkt, pdata->ct_buf_dma_addr,
	    sizeof(ct_iu_rhat_t), sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    0, tov);

	DEBUG13(printk("%s(%ld): call isp_cmd.\n",
	    __func__, ha->host_no);)

	qla2x00_isp_cmd(ha);

	DEBUG13(printk("%s(%ld): going to add timer.\n",
	    __func__, ha->host_no);)

	qla2x00_add_timer_to_cmd(sp, tov + 2);

	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	DEBUG13(printk("%s(%ld): exiting\n", __func__, ha->host_no);)

	return (rval);
}

STATIC int
qla2x00_fdmi_rpa_intr(scsi_qla_host_t *ha, Scsi_Cmnd *pscsi_cmd, void *pct_buf,
    dma_addr_t ct_buf_dma_addr)
{
	int			rval = QL_STATUS_SUCCESS;
	uint16_t		tov;
	unsigned long		cpu_flags = 0;
	struct ct_info		*pdata;
	ct_iu_rpa_t		*ctp;
	ms_iocb_entry_t		*pkt;
	srb_t			*sp;

	DEBUG13(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* Allocate SRB block. */
	if ((sp = qla2x00_get_new_sp(ha)) == NULL) {

		DEBUG2_13(printk("%s(%ld): ERROR cannot alloc sp.\n",
		    __func__, ha->host_no);)

		return (QL_STATUS_RESOURCE_ERROR);
	}

	DEBUG13(printk("%s(%ld): got sp\n", __func__, ha->host_no);)

	if (qla2x00_fdmi_srb_tmpmem_alloc(ha, sp)) {
		/* error */
		atomic_set(&(sp)->ref_count, 0);
		add_to_free_queue(ha, sp);
		sp = NULL;

		return (QL_STATUS_RESOURCE_ERROR);
	}

	sp->cmd = pscsi_cmd;

	tov = ha->login_timeout*2;
	qla2x00_fdmi_srb_init(ha, sp, tov, __constant_cpu_to_be16(FDMI_CC_RPA));

	DEBUG13(printk("%s(%ld): done srb_init\n", __func__, ha->host_no);)

	pdata = (struct ct_info *)sp->cmd->sc_request->sr_buffer;
	ctp = (ct_iu_rpa_t *)pdata->pct_buf;

	/* Setup CT-IU Basic preamble. */
	QLA_INIT_FDMI_CTIU_HDR(ha, ctp->hdr);
	ctp->hdr.cmd_rsp_code = pdata->ct_cmd;

	/* Setup register port attribute payload. */
	qla2x00_fdmi_setup_rpainfo(ha, ctp);

	DEBUG13(printk("%s(%ld): done setup_rpainfo\n", __func__, ha->host_no);)

	/* get spin lock for this operation */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);

	/* Get MS request IOCB from request queue. */
	pkt = (ms_iocb_entry_t *)qla2x00_ms_req_pkt(ha, sp);
	if (pkt == NULL) {
		/* release spin lock and return error. */
		spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

		qla2x00_fdmi_cmnd_srb_free(ha, sp);

		DEBUG2_13(printk("%s(%ld): inst=%ld MSIOCB - could not get "
		    "Request Packet.\n", __func__, ha->host_no, ha->instance);)
		return (QL_STATUS_RESOURCE_ERROR);
	}

	qla2x00_init_req_q_ms_iocb(ha, pkt, pdata->ct_buf_dma_addr,
	    sizeof(ct_iu_rpa_t), sizeof(ct_iu_preamble_t), MANAGEMENT_SERVER,
	    0, tov);
	DEBUG13(printk("%s(%ld): calling isp_cmd.\n",
	    __func__, ha->host_no);)

	qla2x00_isp_cmd(ha);

	DEBUG13(printk("%s(%ld): going to add timer.\n",
	    __func__, ha->host_no);)

	qla2x00_add_timer_to_cmd(sp, tov + 2);

	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	DEBUG13(printk("%s(%ld): exiting\n", __func__, ha->host_no);)

	return (rval);
}


