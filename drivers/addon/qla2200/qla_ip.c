/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


static __u8 hwbroadcast_addr[ETH_ALEN] = { [0 ... ETH_ALEN - 1] = 0xFF };

/**
 * qla2x00_ip_initialize() - Initialize RISC IP support.
 * @ha: SCSI driver HA context
 *
 * Prior to RISC IP initialization, this routine, if necessary, will reset all
 * buffers in the receive buffer ring.
 *
 * Returns 1 if the RISC IP initialization succeeds.
 */
static int
qla2x00_ip_initialize(scsi_qla_host_t *ha)
{
	int i;
	int status;
	unsigned long flags;
	device_reg_t *reg;
	static mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct ip_init_cb *ipinit_cb;
	dma_addr_t ipinit_cb_dma;

	DEBUG12(printk("%s: enter\n", __func__));

	status = 0;

	/* Initialize IP data in ha */
	/* Reset/pack buffers owned by RISC in receive buffer ring */
	if (ha->rec_entries_in != ha->rec_entries_out) {
		struct buffer_cb *bcb;
		uint16_t rec_out;
		struct risc_rec_entry *rec_entry;

		bcb = ha->receive_buffers;
		rec_out = ha->rec_entries_out;

		/*
		 * Must locate all RISC owned buffers and pack them in the
		 * buffer ring.
		 */
		/* between IpBufferOut and IpBufferIN */
		for (i = 0; i < ha->max_receive_buffers; i++, bcb++) {
			if (test_bit(BCB_RISC_OWNS_BUFFER, &bcb->state)) {
				/*
				 * Set RISC owned buffer into receive buffer
				 * ring.
				 */
				rec_entry = &ha->risc_rec_q[rec_out];
				rec_entry->handle = bcb->handle;
				rec_entry->data_addr_low =
				    LSD(bcb->skb_data_dma);
				rec_entry->data_addr_high =
				    MSD(bcb->skb_data_dma);
				if (rec_out < IP_BUFFER_QUEUE_DEPTH - 1)
					rec_out++;
				else
					rec_out = 0;
			}
		}

		/* Verify correct number of RISC owned buffers were found */
		if (rec_out != ha->rec_entries_in) {
			/* Incorrect number of RISC owned buffers?? */
			DEBUG12(printk("%s: incorrect number of RISC "
				       "owned buffers, disable IP\n",
				       __func__));
			ha->flags.enable_ip = 0;
			return 0;
		}
	}

	/* Init RISC buffer pointer */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	reg = ha->iobase;
	WRT_REG_WORD(&reg->mailbox8, ha->rec_entries_in);
	PCI_POSTING(&reg->mailbox8);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait for a ready state from the adapter */
	while (!ha->init_done || ha->dpc_active) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	/* Setup IP initialization control block */
	ipinit_cb = pci_alloc_consistent(ha->pdev,
					 sizeof(struct ip_init_cb),
					 &ipinit_cb_dma);
	if (ipinit_cb) {
		memset(ipinit_cb, 0, sizeof(struct ip_init_cb));
		ipinit_cb->version = IPICB_VERSION;
		ipinit_cb->firmware_options =
		    __constant_cpu_to_le16(IPICB_OPTION_OUT_OF_BUFFERS_EVENT |
					   IPICB_OPTION_NO_BROADCAST_FASTPOST |
					   IPICB_OPTION_64BIT_ADDRESSING);
		ipinit_cb->header_size = cpu_to_le16(ha->header_size);
		ipinit_cb->mtu = cpu_to_le16((uint16_t) ha->mtu);
		ipinit_cb->receive_buffer_size =
		    cpu_to_le16((uint16_t) ha->receive_buff_data_size);
		ipinit_cb->receive_queue_size =
		    __constant_cpu_to_le16(IP_BUFFER_QUEUE_DEPTH);
		ipinit_cb->low_water_mark =
		    __constant_cpu_to_le16(IPICB_LOW_WATER_MARK);
		ipinit_cb->receive_queue_addr[0] =
		    cpu_to_le16(LSW(ha->risc_rec_q_dma));
		ipinit_cb->receive_queue_addr[1] =
		    cpu_to_le16(MSW(ha->risc_rec_q_dma));
		ipinit_cb->receive_queue_addr[2] =
		    cpu_to_le16(LSW(MSD(ha->risc_rec_q_dma)));
		ipinit_cb->receive_queue_addr[3] =
		    cpu_to_le16(MSW(MSD(ha->risc_rec_q_dma)));
		ipinit_cb->receive_queue_in = cpu_to_le16(ha->rec_entries_out);
		ipinit_cb->container_count =
		    __constant_cpu_to_le16(IPICB_BUFFER_CONTAINER_COUNT);

		/* Issue mailbox command to initialize IP firmware */
		mcp->mb[0] = MBC_INITIALIZE_IP;
		mcp->mb[2] = MSW(ipinit_cb_dma);
		mcp->mb[3] = LSW(ipinit_cb_dma);
		mcp->mb[6] = MSW(MSD(ipinit_cb_dma));
		mcp->mb[7] = LSW(MSD(ipinit_cb_dma));
		mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
		mcp->in_mb = MBX_0;
		mcp->tov = 30;
		mcp->buf_size = sizeof(struct ip_init_cb);
		mcp->flags = MBX_DMA_OUT;

		status = qla2x00_mailbox_command(ha, mcp);
		if (status == QL_STATUS_SUCCESS) {
			/* IP initialization successful */
			DEBUG12(printk("%s: successful\n", __func__));

			ha->flags.enable_ip = 1;

			/* Force database update */
			set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
			set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
			set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);

			/* qla2x00_loop_resync(ha); */
			if (ha->dpc_wait && !ha->dpc_active) {
				up(ha->dpc_wait);
			}
			status = 1;
		} else {
			DEBUG12(printk("%s: MBC_INITIALIZE_IP "
				       "failed %x MB0 %x\n",
				       __func__, status, mcp->mb[0]));
			status = 0;
		}
		pci_free_consistent(ha->pdev, sizeof(struct ip_init_cb),
				    ipinit_cb, ipinit_cb_dma);

	} else {
		DEBUG12(printk("%s: memory allocation error\n", __func__));
	}

	return status;
}

/**
 * qla2x00_ip_send_complete() - Handle IP send completion.
 * @ha: SCSI driver HA context
 * @handle: handle to completed send_cb
 * @comp_status: Firmware completion status of send_cb
 *
 * Upon cleanup of the internal active-scb queue, the IP driver is notified of
 * the completion.
 */
static void
qla2x00_ip_send_complete(scsi_qla_host_t *ha,
			 uint32_t handle, uint16_t comp_status)
{
	struct send_cb *scb;

	/* Set packet pointer from queue entry handle */
	if (handle < MAX_SEND_PACKETS) {
		scb = ha->active_scb_q[handle];
		if (scb) {
			ha->ipreq_cnt--;
			ha->active_scb_q[handle] = NULL;

			scb->comp_status = comp_status;
			pci_unmap_single(ha->pdev,
					 scb->skb_data_dma,
					 scb->skb->len, PCI_DMA_TODEVICE);

			/* Return send packet to IP driver */
			ha->send_completion_routine(scb);
			return;
		}
	}

	/* Invalid handle from RISC, reset RISC firmware */
	printk(KERN_WARNING
	       "%s: Bad IP send handle %x - aborting ISP\n", __func__, handle);

	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
}

/**
 * qla2x00_ip_receive() - Handle IP receive IOCB.
 * @ha: SCSI driver HA context
 * @pkt: RISC IP receive packet
 *
 * Upon preparation of one or more buffer_cbs, the IP driver is notified of
 * the received packet.
 */
static void
qla2x00_ip_receive(scsi_qla_host_t *ha, struct ip_rec_entry *iprec_entry)
{
	uint32_t handle;
	uint32_t packet_size;
	uint16_t linked_bcb_cnt;
	uint32_t rec_data_size;
	uint16_t comp_status;
	struct buffer_cb *bcb;
	struct buffer_cb *nbcb;

	comp_status = le16_to_cpu(iprec_entry->comp_status);

	/* If split buffer, set header size for 1st buffer */
	if (comp_status & IPREC_STATUS_SPLIT_BUFFER)
		rec_data_size = ha->header_size;
	else
		rec_data_size = ha->receive_buff_data_size;

	handle = iprec_entry->buffer_handles[0];
	if (handle >= ha->max_receive_buffers) {
		/* Invalid handle from RISC, reset RISC firmware */
		printk(KERN_WARNING
		       "%s: Bad IP buffer handle %x (> buffer_count)...Post "
		       "ISP Abort\n", __func__, handle);
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		return;
	}

	bcb = &ha->receive_buffers[handle];

	if (!test_and_clear_bit(BCB_RISC_OWNS_BUFFER, &bcb->state)) {
		/* Invalid handle from RISC, reset RISC firmware */
		printk(KERN_WARNING
		       "%s: Bad IP buffer handle %x (!RISC_owned)...Post "
		       "ISP Abort\n", __func__, handle);
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		return;
	}

	packet_size = le16_to_cpu(iprec_entry->sequence_length);
	bcb->comp_status = comp_status;
	bcb->packet_size = packet_size;
	nbcb = bcb;

	/* Prepare any linked buffers */
	for (linked_bcb_cnt = 1;; linked_bcb_cnt++) {
		if (packet_size > rec_data_size) {
			nbcb->rec_data_size = rec_data_size;
			packet_size -= rec_data_size;

			/*
			 * If split buffer, only use header size on 1st buffer
			 */
			rec_data_size = ha->receive_buff_data_size;

			handle = iprec_entry->buffer_handles[linked_bcb_cnt];
			if (handle >= ha->max_receive_buffers) {
				/*
				 * Invalid handle from RISC reset RISC firmware
				 */
				printk(KERN_WARNING
				       "%s: Bad IP buffer handle %x (> "
				       "buffer_count - PS)...Post ISP Abort\n",
				       __func__, handle);
				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				return;
			}
			nbcb->next_bcb = &ha->receive_buffers[handle];
			nbcb = nbcb->next_bcb;

			if (!test_and_clear_bit(BCB_RISC_OWNS_BUFFER,
						&nbcb->state)) {
				/*
				 * Invalid handle from RISC reset RISC firmware
				 */
				printk(KERN_WARNING
				       "%s: Bad IP buffer handle %x "
				       "(!RISC_owned - PS)...Post ISP Abort\n",
				       __func__, handle);
				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				return;
			}
		} else {
			/* Single buffer_cb */
			nbcb->rec_data_size = packet_size;
			nbcb->next_bcb = NULL;
			break;
		}
	}

	/* Check for incoming ARP packet with matching IP address */
	if (le16_to_cpu(iprec_entry->service_class) == 0) {
		fc_port_t *fcport;
		struct packet_header *packethdr;

		packethdr = (struct packet_header *)bcb->skb_data;

		/* Scan list of IP devices to see if login needed */
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (!memcmp(&fcport->port_name[2],
				    packethdr->networkh.s.na.addr, ETH_ALEN)) {
				break;
			}
		}
	}

	/* Pass received packet to IP driver */
	bcb->linked_bcb_cnt = linked_bcb_cnt;
	ha->receive_packets_routine(ha->receive_packets_context, bcb);

	/* Keep track of RISC buffer pointer (for IP reinit) */
	ha->rec_entries_out += linked_bcb_cnt;
	if (ha->rec_entries_out >= IP_BUFFER_QUEUE_DEPTH)
		ha->rec_entries_out -= IP_BUFFER_QUEUE_DEPTH;
}

/**
 * qla2x00_convert_to_arp() - Convert an IP send packet to an ARP packet
 * @ha: SCSI driver HA context
 * @scb: The send_cb structure to convert
 *
 * Returns 1 if conversion successful.
 */
static int
qla2x00_convert_to_arp(scsi_qla_host_t *ha, struct send_cb *scb)
{
	struct sk_buff *skb;
	struct packet_header *packethdr;
	struct arp_header *arphdr;
	struct ip_header *iphdr;

	DEBUG12(printk("%s: convert packet to ARP\n", __func__));

	skb = scb->skb;
	packethdr = scb->header;
	arphdr = (struct arp_header *)skb->data;
	iphdr = (struct ip_header *)skb->data;

	if (packethdr->snaph.ethertype == __constant_htons(ETH_P_IP)) {
		/* Convert IP packet to ARP packet */
		packethdr->networkh.d.na.naa = NAA_IEEE_MAC_TYPE;
		packethdr->networkh.d.na.unused = 0;
		memcpy(packethdr->networkh.d.na.addr,
		       hwbroadcast_addr, ETH_ALEN);
		packethdr->snaph.ethertype = __constant_htons(ETH_P_ARP);

		arphdr->ar_tip = iphdr->iph.daddr;
		arphdr->ar_sip = iphdr->iph.saddr;
		arphdr->arph.ar_hrd = __constant_htons(ARPHRD_IEEE802);
		arphdr->arph.ar_pro = __constant_htons(ETH_P_IP);
		arphdr->arph.ar_hln = ETH_ALEN;
		arphdr->arph.ar_pln = sizeof(iphdr->iph.daddr);	/* 4 */
		arphdr->arph.ar_op = __constant_htons(ARPOP_REQUEST);
		memcpy(arphdr->ar_sha, packethdr->networkh.s.na.addr, ETH_ALEN);
		memset(arphdr->ar_tha, 0, ETH_ALEN);

		skb->len = sizeof(struct arp_header);

		return 1;
	} else {
		return 0;
	}
}

/**
 * qla2x00_get_ip_loopid() - Retrieve loop id of an IP device.
 * @ha: SCSI driver HA context
 * @packethdr: IP device to remove
 * @loop_id: loop id of discovered device
 *
 * This routine will interrogate the packet header to determine if the sender is
 * in the list of active IP devices.  The first two bytes of the destination
 * address will be modified to match the port name stored in the active IP
 * device list.
 *
 * Returns 1 if a valid loop id is returned.
 */
static int
qla2x00_get_ip_loopid(scsi_qla_host_t *ha,
		      struct packet_header *packethdr, uint16_t * loop_id)
{
	fc_port_t *fcport;

	/* Scan list of logged in IP devices for match */
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (memcmp(&fcport->port_name[2],
			   &(packethdr->networkh.d.fcaddr[2]), ETH_ALEN))
			continue;

		/* Found match, return loop ID  */
		*loop_id = fcport->loop_id;

		/* Update first 2 bytes of port name */
		packethdr->networkh.d.fcaddr[0] = fcport->port_name[0];
		packethdr->networkh.d.fcaddr[1] = fcport->port_name[1];

		return 1;
	}

	/* Check for broadcast or multicast packet */
	if (!memcmp(packethdr->networkh.d.na.addr, hwbroadcast_addr,
	    ETH_ALEN) || (packethdr->networkh.d.na.addr[0] & 0x01)) {
		/* Broadcast packet, return broadcast loop ID  */
		*loop_id = BROADCAST;

		/* Update destination NAA of header */
		packethdr->networkh.d.na.naa = NAA_IEEE_MAC_TYPE;
		packethdr->networkh.d.na.unused = 0;
		return 1;
	}

	/* TODO */
	/* Try sending FARP IOCB to request login */

	DEBUG12(printk("%s: ID not found for "
		       "XX XX %02x %02x %02x %02x %02x %02x\n",
		       __func__,
		       packethdr->networkh.d.na.addr[0],
		       packethdr->networkh.d.na.addr[1],
		       packethdr->networkh.d.na.addr[2],
		       packethdr->networkh.d.na.addr[3],
		       packethdr->networkh.d.na.addr[4],
		       packethdr->networkh.d.na.addr[5]));

	return 0;
}

/**
 * qla2x00_ip_enable() - Create IP-driver/SCSI-driver IP connection.
 * @ha: SCSI driver HA context
 * @enable_data: bd_enable data describing the IP connection
 *
 * This routine is called by the IP driver to enable an IP connection to the
 * SCSI driver and to pass in IP driver parameters.
 *
 * The HA context is propagated with the specified @enable_data and the
 * Firmware is initialized for IP support.
 * 
 * Returns 1 if the IP connection was successfully enabled.
 */
static int
qla2x00_ip_enable(scsi_qla_host_t *ha, struct bd_enable *enable_data)
{
	int status;

	DEBUG12(printk("%s: enable adapter %ld\n", __func__, ha->host_no));

	status = 0;

	/* Verify structure size and version and adapter online */
	if (!(ha->flags.online) ||
	    (enable_data->length != BDE_LENGTH) ||
	    (enable_data->version != BDE_VERSION)) {

		DEBUG12(printk("%s: incompatable structure or offline\n",
		    __func__));
		return status;
	}

	/* Save parameters from IP driver */
	ha->mtu = enable_data->mtu;
	ha->header_size = enable_data->header_size;
	ha->receive_buffers = enable_data->receive_buffers;
	ha->max_receive_buffers = enable_data->max_receive_buffers;
	ha->receive_buff_data_size = enable_data->receive_buff_data_size;
	if (test_bit(BDE_NOTIFY_ROUTINE, &enable_data->options)) {
		ha->notify_routine = enable_data->notify_routine;
		ha->notify_context = enable_data->notify_context;
	}
	ha->send_completion_routine = enable_data->send_completion_routine;
	ha->receive_packets_routine = enable_data->receive_packets_routine;
	ha->receive_packets_context = enable_data->receive_packets_context;

	/* Enable RISC IP support */
	status = qla2x00_ip_initialize(ha);
	if (!status) {
		DEBUG12(printk("%s: IP initialization failed", __func__));
		ha->notify_routine = NULL;
	}
	return status;
}

/**
 * qla2x00_ip_disable() - Remove IP-driver/SCSI-driver IP connection.
 * @ha: SCSI driver HA context
 *
 * This routine is called by the IP driver to disable a previously created IP
 * connection.
 *
 * A Firmware call to disable IP support is issued.
 */
static void
qla2x00_ip_disable(scsi_qla_host_t *ha)
{
	int rval;
	static mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG12(printk("%s: disable adapter %ld\n", __func__, ha->host_no));

	/* Wait for a ready state from the adapter */
	while (!ha->init_done || ha->dpc_active) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	/* Disable IP support */
	ha->flags.enable_ip = 0;

	mcp->mb[0] = MBC_DISABLE_IP;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);
	if (rval == QL_STATUS_SUCCESS) {
		/* IP disabled successful */
		DEBUG12(printk(KERN_INFO "%s: successful\n", __func__));
	} else {
		DEBUG12(printk(KERN_WARNING
			       "%s: MBC_DISABLE_IP failed\n", __func__));
	}

	/* Reset IP parameters */
	ha->rec_entries_in = 0;
	ha->rec_entries_out = 0;
	ha->notify_routine = NULL;
}

/**
 * qla2x00_add_buffers() - Adds buffers to the receive buffer queue.
 * @ha: SCSI driver HA context
 * @rec_count: The number of receive buffers to add to the queue
 * @ha_locked: Flag indicating if the function is called with the hardware lock
 *
 * This routine is called by the IP driver to pass new buffers to the receive
 * buffer queue.
 */
static void
qla2x00_add_buffers(scsi_qla_host_t *ha, uint16_t rec_count, int ha_locked)
{
	int i;
	uint16_t rec_in;
	uint16_t handle;
	unsigned long flags = 0;
	device_reg_t *reg;
	struct risc_rec_entry *risc_rec_q;
	struct buffer_cb *bcbs;

	flags = 0;
	risc_rec_q = ha->risc_rec_q;
	rec_in = ha->rec_entries_in;
	bcbs = ha->receive_buffers;
	/* Set RISC owns buffer flag on new entries */
	for (i = 0; i < rec_count; i++) {
		handle = risc_rec_q[rec_in].handle;
		set_bit(BCB_RISC_OWNS_BUFFER, &(bcbs[handle].state));
		if (rec_in < IP_BUFFER_QUEUE_DEPTH - 1)
			rec_in++;
		else
			rec_in = 0;
	}

	/* Update RISC buffer pointer */
	if (!ha_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	reg = ha->iobase;
	WRT_REG_WORD(&reg->mailbox8, rec_in);
	PCI_POSTING(&reg->mailbox8);
	ha->rec_entries_in = rec_in;

	if (!ha_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla2x00_send_packet() - Transmit a send_cb.
 * @ha: SCSI driver HA context
 * @scb: The send_cb structure to send
 *
 * This routine is called by the IP driver to pass @scb (IP packet) to the ISP
 * for transmission.
 *
 * Returns QL_STATUS_SUCCESS if @scb was sent, QL_STATUS_RESOURCE_ERROR if the
 * RISC was too busy to send, or QL_STATUS_ERROR.
 */
static int
qla2x00_send_packet(scsi_qla_host_t *ha, struct send_cb *scb)
{
	int i;
	uint16_t cnt;
	uint32_t handle;
	unsigned long flags;
	struct ip_cmd_entry *ipcmd_entry;
	struct sk_buff *skb;
	device_reg_t *reg;
	uint16_t loop_id;

	skb = scb->skb;
	reg = ha->iobase;

	/* Check adapter state */
	if (!ha->flags.online) {
		return QL_STATUS_ERROR;
	}

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if (qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA2X00_SUCCESS) {
			printk(KERN_WARNING
			       "%s: Unable to issue marker.\n", __func__);
			return QL_STATUS_ERROR;
		}
		ha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (ha->req_q_cnt < 4) {
		/* Update number of free request entries */
		cnt = qla2x00_debounce_register(&reg->req_q_out);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = REQUEST_ENTRY_CNT -
			    (ha->req_ring_index - cnt);
	}

	if (ha->req_q_cnt >= 4) {
		/* Get tag handle for command */
		handle = ha->current_scb_q_idx;
		for (i = 0; i < MAX_SEND_PACKETS; i++) {
			handle++;
			if (handle == MAX_SEND_PACKETS)
				handle = 0;
			if (ha->active_scb_q[handle] == NULL) {
				ha->current_scb_q_idx = handle;
				goto found_handle;
			}
		}
	}

	/* Low on resources, try again later */
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	printk(KERN_WARNING
	       "%s: Low on resources, try again later...\n", __func__);

	return QL_STATUS_RESOURCE_ERROR;

found_handle:

	/* Build ISP command packet */
	ipcmd_entry = (struct ip_cmd_entry *)ha->request_ring_ptr;

	*((uint32_t *) (&ipcmd_entry->entry_type)) =
	    __constant_cpu_to_le32(ET_IP_COMMAND_64 | (1 << 8));

	ipcmd_entry->handle = handle;

	/* Get destination loop ID for packet */
	if (!qla2x00_get_ip_loopid(ha, scb->header, &loop_id)) {
		/* Failed to get loop ID, convert packet to ARP */
		if (qla2x00_convert_to_arp(ha, scb)) {
			/* Broadcast ARP */
			loop_id = BROADCAST;
		} else {
			/* Return packet */
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			printk(KERN_WARNING
			       "%s: Unable to determine loop id for "
			       "destination.\n", __func__);
			return QL_STATUS_ERROR;
		}
	}

	/* Default five second firmware timeout */
	ipcmd_entry->loop_id = cpu_to_le16(loop_id);
	ipcmd_entry->timeout = __constant_cpu_to_le16(5);
	ipcmd_entry->control_flags = __constant_cpu_to_le16(CF_WRITE);
	ipcmd_entry->reserved_2 = 0;
	ipcmd_entry->service_class = __constant_cpu_to_le16(0);
	ipcmd_entry->data_seg_count = __constant_cpu_to_le16(2);
	scb->skb_data_dma = pci_map_single(ha->pdev, skb->data, skb->len,
					   PCI_DMA_TODEVICE);
	ipcmd_entry->dseg_0_address[0] = cpu_to_le32(LSD(scb->header_dma));
	ipcmd_entry->dseg_0_address[1] = cpu_to_le32(MSD(scb->header_dma));
	ipcmd_entry->dseg_0_length =
	    __constant_cpu_to_le32(sizeof(struct packet_header));
	ipcmd_entry->dseg_1_address[0] = cpu_to_le32(LSD(scb->skb_data_dma));
	ipcmd_entry->dseg_1_address[1] = cpu_to_le32(MSD(scb->skb_data_dma));
	ipcmd_entry->dseg_1_length = cpu_to_le32(skb->len);
	ipcmd_entry->byte_count =
	    cpu_to_le32(skb->len + sizeof(struct packet_header));

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	ha->ipreq_cnt++;
	ha->req_q_cnt--;
	ha->active_scb_q[handle] = scb;
	/* Set chip new ring index. */
	WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
	PCI_POSTING(&reg->req_q_in);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QL_STATUS_SUCCESS;
}

/**
 * qla2x00_tx_timeout() - Handle transmission timeout.
 * @ha: SCSI driver HA context
 *
 * This routine is called by the IP driver to handle packet transmission
 * timeouts.
 *
 * Returns QL_STATUS_SUCCESS if timeout handling completed successfully.
 */
static int
qla2x00_tx_timeout(scsi_qla_host_t *ha)
{
	/* TODO: complete interface */

	/* Reset RISC firmware for basic recovery */
	printk(KERN_WARNING
	       "%s: A transmission timeout occured - aborting ISP\n", __func__);
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);

	return QL_STATUS_SUCCESS;
}

/**
 * qla2x00_ip_inquiry() - Discover IP-capable adapters.
 * @adapter_num: adapter number to check (instance)
 * @inq_data: return bd_inquiry data of the discovered adapter
 *
 * This routine is called by the IP driver to discover adapters that support IP
 * and to get adapter parameters from the SCSI driver.
 *
 * Returns 1 if the specified adapter supports IP.
 */
int
qla2x00_ip_inquiry(uint16_t adapter_num, struct bd_inquiry *inq_data)
{
	scsi_qla_host_t *ha;

	/* Verify structure size and version */
	if ((inq_data->length != BDI_LENGTH) ||
	    (inq_data->version != BDI_VERSION)) {
		DEBUG12(printk("%s: incompatable structure\n", __func__));
		return 0;
	}

	/* Find the specified host adapter */
	for (ha = qla2x00_hostlist;
	     ha && ha->instance != adapter_num; ha = ha->next) ;

	if (!ha)
		return 0;
	if (!ha->flags.online)
		return 0;

	DEBUG12(printk("%s: found adapter %d\n", __func__, adapter_num));

	/* Return inquiry data to backdoor IP driver */
	set_bit(BDI_IP_SUPPORT, &inq_data->options);
	if (ha->flags.enable_64bit_addressing)
		set_bit(BDI_64BIT_ADDRESSING, &inq_data->options);
	inq_data->ha = ha;
	inq_data->risc_rec_q = ha->risc_rec_q;
	inq_data->risc_rec_q_size = IP_BUFFER_QUEUE_DEPTH;
	inq_data->link_speed = ha->current_speed;
	memcpy(inq_data->port_name, ha->ip_port_name, WWN_SIZE);
	inq_data->pdev = ha->pdev;
	inq_data->ip_enable_routine = qla2x00_ip_enable;
	inq_data->ip_disable_routine = qla2x00_ip_disable;
	inq_data->ip_add_buffers_routine = qla2x00_add_buffers;
	inq_data->ip_send_packet_routine = qla2x00_send_packet;
	inq_data->ip_tx_timeout_routine = qla2x00_tx_timeout;

	return 1;
}
