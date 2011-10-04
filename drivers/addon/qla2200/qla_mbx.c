/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


#include "qla_mbx.h"


/*
 *  Local Function Prototypes.
 */

STATIC uint8_t
qla2x00_mailbox_command(scsi_qla_host_t *, mbx_cmd_t *);

STATIC void
qla2x00_mbx_sem_timeout(unsigned long);

STATIC uint8_t
qla2x00_get_mbx_access(scsi_qla_host_t *, uint32_t);

STATIC uint8_t
qla2x00_release_mbx_access(scsi_qla_host_t *, uint32_t);

STATIC uint8_t
qla2x00_mbx_q_add(scsi_qla_host_t *, mbx_cmdq_t **);

STATIC void
qla2x00_mbx_q_get(scsi_qla_host_t *, mbx_cmdq_t **);

STATIC void
qla2x00_mbx_q_memb_alloc(scsi_qla_host_t *, mbx_cmdq_t **);

STATIC void
qla2x00_mbx_q_memb_free(scsi_qla_host_t *, mbx_cmdq_t *);

STATIC int
qla2x00_load_ram(scsi_qla_host_t *, dma_addr_t, uint16_t, uint16_t);

STATIC int
qla2x00_execute_fw(scsi_qla_host_t *, uint32_t);

STATIC int
qla2x00_mbx_reg_test(scsi_qla_host_t *);

STATIC int
qla2x00_verify_checksum(scsi_qla_host_t *, uint32_t );

STATIC int
qla2x00_issue_iocb(scsi_qla_host_t *, void *, dma_addr_t, size_t);

STATIC int
qla2x00_abort_command(scsi_qla_host_t *, srb_t *);

STATIC int
qla24xx_abort_command(scsi_qla_host_t *, srb_t *);

STATIC int
qla2x00_abort_device(scsi_qla_host_t *, uint16_t, uint16_t);

#if USE_ABORT_TGT
STATIC int
qla2x00_abort_target(fc_port_t *fcport);
#endif

STATIC int
qla24xx_abort_target(scsi_qla_host_t *, fc_port_t *);
STATIC int
qla2x00_target_reset(scsi_qla_host_t *, uint16_t, uint16_t);

STATIC int
qla2x00_get_adapter_id(scsi_qla_host_t *, uint16_t *, uint8_t *, uint8_t *,
    uint8_t *, uint16_t *);

STATIC int
qla2x00_get_retry_cnt(scsi_qla_host_t *, uint8_t *, uint8_t *);

#if defined(INTAPI)
int
qla2x00_loopback_test(scsi_qla_host_t *, INT_LOOPBACK_REQ *, uint16_t *);
int
qla2x00_echo_test(scsi_qla_host_t *, INT_LOOPBACK_REQ *, uint16_t *);
#endif

STATIC int
qla2x00_init_firmware(scsi_qla_host_t *, uint16_t);

STATIC int
qla2x00_get_port_database(scsi_qla_host_t *, fc_port_t *, uint8_t);

STATIC int
qla2x00_get_firmware_state(scsi_qla_host_t *, uint16_t *);

STATIC int
qla2x00_get_firmware_options(scsi_qla_host_t *, uint16_t *, uint16_t *,
    uint16_t *, uint16_t *);

#if !defined(ISP2100) && !defined(ISP2200)
STATIC int
qla2x00_set_firmware_options(scsi_qla_host_t *, uint16_t, uint16_t,
    uint16_t, uint16_t, uint16_t, uint16_t *);
#endif

STATIC int
qla2x00_get_port_name(scsi_qla_host_t *, uint16_t, uint8_t *, uint8_t);

STATIC uint8_t
qla2x00_get_link_status(scsi_qla_host_t *, uint16_t, link_stat_t *, uint16_t *);

STATIC int
qla2x00_lip_reset(scsi_qla_host_t *);

STATIC int
qla2x00_send_sns(scsi_qla_host_t *, dma_addr_t, uint16_t, size_t);

STATIC int
qla2x00_login_fabric(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t,
    uint16_t *, uint8_t);

STATIC int
qla2x00_login_local_device(scsi_qla_host_t *, fc_port_t *, uint16_t *, uint8_t);

STATIC int
qla2x00_fabric_logout(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t);

STATIC int
qla2x00_full_login_lip(scsi_qla_host_t *ha);

STATIC int
qla2x00_get_id_list(scsi_qla_host_t *, void *, dma_addr_t, uint16_t *);

#if 0 /* not yet needed */
STATIC int
qla2x00_dump_ram(scsi_qla_host_t *, uint32_t, dma_addr_t, uint32_t);
#endif

int
qla24xx_lun_reset(scsi_qla_host_t *, fc_port_t *, uint16_t);

int
qla2x00_lun_reset(scsi_qla_host_t *, fc_port_t *, uint16_t);

STATIC int
qla2x00_send_rnid_mbx(scsi_qla_host_t *, uint16_t, uint8_t, dma_addr_t,
    size_t, uint16_t *);

STATIC int
qla2x00_set_rnid_params_mbx(scsi_qla_host_t *, dma_addr_t, size_t, uint16_t *);

STATIC int
qla2x00_get_rnid_params_mbx(scsi_qla_host_t *, dma_addr_t, size_t, uint16_t *);

#if defined(QL_DEBUG_LEVEL_3)
STATIC int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map);
#endif

STATIC int
qla2x00_stop_firmware(scsi_qla_host_t *ha);

/***************************/
/* Function implementation */
/***************************/

STATIC void
qla2x00_mbx_sem_timeout(unsigned long data)
{
	struct semaphore	*sem_ptr = (struct semaphore *)data;

	DEBUG11(printk("qla2x00_sem_timeout: entered.\n");)

	if (sem_ptr != NULL) {
		up(sem_ptr);
	}

	DEBUG11(printk("qla2x00_mbx_sem_timeout: exiting.\n");)
}

/*
 *  tov = timeout value in seconds
 */
STATIC uint8_t
qla2x00_get_mbx_access(scsi_qla_host_t *ha, uint32_t tov)
{
	uint8_t		ret;
	int		prev_val = 1;  /* assume no access yet */
	mbx_cmdq_t	*ptmp_mbq;
	struct timer_list	tmp_cmd_timer;
	unsigned long	cpu_flags;


	DEBUG11(printk("qla2x00_get_mbx_access(%ld): entered.\n",
	    ha->host_no);)

	while (1) {
		if (test_bit(MBX_CMD_WANT, &ha->mbx_cmd_flags) == 0 &&
		    test_bit(MBX_UPDATE_FLASH_ACTIVE, 
			    &ha->mbx_cmd_flags) == 0 ) {

			DEBUG11(printk("qla2x00_get_mbx_access(%ld): going "
			    " to test access flags.\n", ha->host_no);)

			/* No one else is waiting. Go ahead and try to
			 * get access.
			 */
			if ((prev_val =  test_and_set_bit(MBX_CMD_ACTIVE,
			    &ha->mbx_cmd_flags)) == 0) {
				break;
			}
		}

		/* wait for previous command to finish */
		DEBUG(printk("qla2x00_get_mbx_access(%ld): access "
		    "flags=%lx. busy. Waiting for access. curr time=0x%lx.\n",
		    ha->host_no, ha->mbx_cmd_flags, jiffies);)

		DEBUG11(printk("qla2x00_get_mbx_access(%ld): access "
		    "flags=%lx. busy. Waiting for access. curr time=0x%lx.\n",
		    ha->host_no, ha->mbx_cmd_flags, jiffies);)

		/*
		 * Init timer and get semaphore from mbx q. After we got valid
		 * semaphore pointer the MBX_CMD_WANT flag would also had
		 * been set.
		 */
		qla2x00_mbx_q_add(ha, &ptmp_mbq);

		if (ptmp_mbq == NULL) {
			/* queue full? problem? can't proceed. */
			DEBUG2_3_11(printk("qla2x00_get_mbx_access(%ld): ERROR "
			    "no more mbx_q allowed. exiting.\n", ha->host_no);)

			break;
		}

		/* init timer and semaphore */
		init_timer(&tmp_cmd_timer);
		tmp_cmd_timer.data = (unsigned long)&ptmp_mbq->cmd_sem;
		tmp_cmd_timer.function =
		    (void (*)(unsigned long))qla2x00_mbx_sem_timeout;
		tmp_cmd_timer.expires = jiffies + tov * HZ;

		DEBUG11(printk("get_mbx_access(%ld): adding timer. "
		    "curr time=0x%lx timeoutval=0x%lx.\n",
		    ha->host_no, jiffies, tmp_cmd_timer.expires);)

			/* wait. */
/*	 	 add_timer(&tmp_cmd_timer);*/
		DEBUG11(printk("get_mbx_access(%ld): going to sleep. "
		    "current time=0x%lx.\n", ha->host_no, jiffies);)

		down_interruptible(&ptmp_mbq->cmd_sem);

		DEBUG11(printk("get_mbx_access(%ld): woke up. current "
		    "time=0x%lx.\n",
		    ha->host_no, jiffies);)

/*		del_timer(&tmp_cmd_timer);*/

		/* try to get lock again. we'll test later to see
		 * if we actually got the lock.
		 */
		prev_val = test_and_set_bit(MBX_CMD_ACTIVE,
		    &ha->mbx_cmd_flags);

		/*
		 * After we tried to get access then we check to see
		 * if we need to clear the MBX_CMD_WANT flag. Don't clear
		 * this flag before trying to get access or else another
		 * new thread might grab it before we did.
		 */
		spin_lock_irqsave(&ha->mbx_q_lock, cpu_flags);
		if (ha->mbx_q_head == NULL) {
			/* We're the last thread in queue. */
			clear_bit(MBX_CMD_WANT, &ha->mbx_cmd_flags);
		}
		qla2x00_mbx_q_memb_free(ha, ptmp_mbq);
		spin_unlock_irqrestore(&ha->mbx_q_lock, cpu_flags);

		break;
	}

	if (prev_val == 0) {
		/* We got the lock */
		DEBUG11(printk("qla2x00_get_mbx_access(%ld): success.\n",
		    ha->host_no);)

		ret = QL_STATUS_SUCCESS;
	} else {
		/* Timeout or resource error. */
		DEBUG2_3_11(printk("qla2x00_get_mbx_access(%ld): timed out.\n",
		    ha->host_no);)

		ret = QL_STATUS_TIMEOUT;
	}

	return ret;
}

STATIC uint8_t
qla2x00_release_mbx_access(scsi_qla_host_t *ha, uint32_t tov)
{
	mbx_cmdq_t	*next_thread;

	DEBUG11(printk("qla2x00_release_mbx_access:(%ld): entered.\n",
	    ha->host_no);)

	clear_bit(MBX_CMD_ACTIVE, &ha->mbx_cmd_flags);

	/* Wake up one pending mailbox cmd thread in queue. */
	qla2x00_mbx_q_get(ha, &next_thread);
	if (next_thread) {
		DEBUG11(printk("qla2x00_release_mbx_access: found pending "
		    "mbx cmd. Waking up sem in %p.\n", &next_thread);)
		up(&next_thread->cmd_sem);
	}

	DEBUG11(printk("qla2x00_release_mbx_access:(%ld): exiting.\n",
	    ha->host_no);)

	return QL_STATUS_SUCCESS;
}

/* Allocates a mbx_cmdq_t struct and add to the mbx_q list. */
STATIC uint8_t
qla2x00_mbx_q_add(scsi_qla_host_t *ha, mbx_cmdq_t **ret_mbq)
{
	uint8_t		ret;
	unsigned long	cpu_flags;
	mbx_cmdq_t	*ptmp = NULL;

	spin_lock_irqsave(&ha->mbx_q_lock, cpu_flags);

	DEBUG11(printk("qla2x00_mbx_q_add: got mbx_q spinlock. "
	    "Inst=%d.\n", apiHBAInstance);)

	qla2x00_mbx_q_memb_alloc(ha, &ptmp);
	if (ptmp == NULL) {
		/* can't add any more threads */
		DEBUG2_3_11(printk("qla2x00_mbx_q_add: ERROR no more "
		    "ioctl threads allowed. Inst=%d.\n", apiHBAInstance);)

		ret = QL_STATUS_RESOURCE_ERROR;
	} else {
		if (ha->mbx_q_tail == NULL) {
			/* First thread to queue. */
			set_bit(IOCTL_WANT, &ha->mbx_cmd_flags);

			ha->mbx_q_head = ptmp;
		} else {
			ha->mbx_q_tail->pnext = ptmp;
		}
		ha->mbx_q_tail = ptmp;

		/* Now init the semaphore */
		init_MUTEX_LOCKED(&ptmp->cmd_sem);
		ret = QL_STATUS_SUCCESS;
	}

	*ret_mbq = ptmp;

	DEBUG11(printk("qla2x00_mbx_q_add: going to release spinlock. "
	    "ret_mbq=%p, ret=%d. Inst=%d.\n", *ret_mbq, ret, apiHBAInstance);)

	spin_unlock_irqrestore(&ha->mbx_q_lock, cpu_flags);

	return ret;
}

/* Just remove and return first member from mbx_cmdq.  Don't free anything. */
STATIC void
qla2x00_mbx_q_get(scsi_qla_host_t *ha, mbx_cmdq_t **ret_mbq)
{
	unsigned long	cpu_flags;

	spin_lock_irqsave(&ha->mbx_q_lock, cpu_flags);

	DEBUG11(printk("qla2x00_mbx_q_get: got mbx_q spinlock. "
	    "Inst=%d.\n", apiHBAInstance);)

	/* Remove from head */
	*ret_mbq = ha->mbx_q_head;
	if (ha->mbx_q_head != NULL) {
		ha->mbx_q_head = ha->mbx_q_head->pnext;
		if (ha->mbx_q_head == NULL) {
			/* That's the last one in queue. */
			ha->mbx_q_tail = NULL;
		}
		(*ret_mbq)->pnext = NULL;
	}

	DEBUG11(printk("qla2x00_mbx_q_remove: return ret_mbq=%p. Going to "
	    "release spinlock. Inst=%d.\n", *ret_mbq, apiHBAInstance);)

	spin_unlock_irqrestore(&ha->mbx_q_lock, cpu_flags);
}

/* Find a free mbx_q member from the array. Must already got the
 * mbx_q_lock spinlock.
 */
STATIC void
qla2x00_mbx_q_memb_alloc(scsi_qla_host_t *ha, mbx_cmdq_t **ret_mbx_q_memb)
{
	mbx_cmdq_t	*ptmp = NULL;

	DEBUG11(printk("qla2x00_mbx_q_memb_alloc: entered. "
	    "Inst=%d.\n", apiHBAInstance);)

	ptmp = ha->mbx_sem_pool_head;
	if (ptmp != NULL) {
		ha->mbx_sem_pool_head = ptmp->pnext;
		ptmp->pnext = NULL;
		if (ha->mbx_sem_pool_head == NULL) {
			ha->mbx_sem_pool_tail = NULL;
		}
	} else {
		/* We ran out of pre-allocated semaphores.  Try to allocate
		 * a new one.
		 */
		ptmp = (void *)KMEM_ZALLOC(sizeof(mbx_cmdq_t), 40);
	}

	*ret_mbx_q_memb = ptmp;

	DEBUG11(printk("qla2x00_mbx_q_memb_alloc: return waitq_memb=%p. "
	    "Inst=%d.\n", *ret_mbx_q_memb, apiHBAInstance);)
}

/* Add the specified mbx_q member back to the free semaphore pool. Must
 * already got the mbx_q_lock spinlock.
 */
STATIC void
qla2x00_mbx_q_memb_free(scsi_qla_host_t *ha, mbx_cmdq_t *pfree_mbx_q_memb)
{
	DEBUG11(printk("qla2x00_mbx_q_memb_free: entered. Inst=%d.\n",
	    apiHBAInstance);)

	if (pfree_mbx_q_memb != NULL) {
		if (ha->mbx_sem_pool_tail != NULL) {
			/* Add to tail */
			ha->mbx_sem_pool_tail->pnext = pfree_mbx_q_memb;
		} else {
			ha->mbx_sem_pool_head = pfree_mbx_q_memb;
		}
		ha->mbx_sem_pool_tail = pfree_mbx_q_memb;
	}

	/* put it back to the free pool. */

	DEBUG11(printk("qla2x00_mbx_q_memb_free: exiting. "
	    "Inst=%d.\n", apiHBAInstance);)
}

/*
 * qla2x00_mailbox_command
 *	Issue mailbox command and waits for completion.
 *
 * Input:
 *	ha = adapter block pointer.
 *	mcp = driver internal mbx struct pointer.
 *
 * Output:
 *	mb[MAX_MAILBOX_REGISTER_COUNT] = returned mailbox data.
 *
 * Returns:
 *	0 : QL_STATUS_SUCCESS = cmd performed success
 *	1 : QL_STATUS_ERROR   (error encountered)
 *	6 : QL_STATUS_TIMEOUT (timeout condition encountered)
 *
 * Context:
 *	Kernel context.
 */
STATIC uint8_t
qla2x00_mailbox_command(scsi_qla_host_t *ha, mbx_cmd_t *mcp)
{
	unsigned long    flags = 0;
	device_reg_t     *reg = NULL;
#if defined(ISP2300)
	struct device_reg_24xx *reg24 = NULL;
#endif
	struct timer_list	tmp_intr_timer;
	uint8_t		abort_active;
	uint8_t		discard;
	uint8_t		io_lock_on = ha->init_done;
	uint8_t		mbx_count;
	uint8_t		status = 0;
	uint8_t		tmp_stat = 0;
	uint16_t	command;
	uint16_t	*iptr, *optr;
	uint32_t	data;
	uint32_t	mboxes;
	unsigned long	mbx_flags = 0, cnt;

	DEBUG11(printk("qla2x00_mailbox_command(%ld): entered.\n",
	    ha->host_no);)

	abort_active = test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
	/*
	 * Wait for active mailbox commands to finish by waiting at most
	 * tov seconds. This is to serialize actual issuing of mailbox cmds
	 * during non ISP abort time.
	 */
	if (!abort_active) {
		tmp_stat = qla2x00_get_mbx_access(ha, mcp->tov);
		if (tmp_stat != QL_STATUS_SUCCESS) {
			/* Timeout occurred. Return error. */
			DEBUG2_3_11(printk("qla2x00_mailbox_command(%ld): cmd "
			    "access timeout. Exiting.\n", ha->host_no);)
			return QL_STATUS_TIMEOUT;
		}
	}

	ha->flags.mbox_busy = TRUE;
	/* Save mailbox command for debug */
	ha->mcp = mcp;

	/* Try to get mailbox register access */
	if (!abort_active)
		QLA_MBX_REG_LOCK(ha);

	DEBUG11(printk("scsi%d: prepare to issue mbox cmd=0x%x.\n",
	    (int)ha->host_no, mcp->mb[0]);)

	ha->mbox_trace = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Load mailbox registers. */
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		reg24 = (struct device_reg_24xx *)ha->iobase;
		optr = (uint16_t *)&reg24->mailbox0;
	} else 
#endif
	{
		reg = ha->iobase;
		optr = (uint16_t *)&reg->mailbox0;
	}
	mbx_count = MAILBOX_REGISTER_COUNT;

	iptr = mcp->mb;
	command = mcp->mb[0];
	mboxes = mcp->out_mb;

	for (cnt = 0; cnt < mbx_count; cnt++) {
#if defined(ISP2200)
		if (cnt == 8) {
			optr = (uint16_t *)&reg->mailbox8;
		}
#endif
		if (mboxes & BIT_0) {
			WRT_REG_WORD(optr, *iptr);
			PCI_POSTING(optr);
		}

		mboxes >>= 1;
		optr++;
		iptr++;
	}

#if defined(QL_DEBUG_LEVEL_1)
	printk("qla2x00_mailbox_command: Loaded MBX registers "
	    "(displayed in bytes) = \n");
	qla2x00_dump_buffer((uint8_t *)mcp->mb, 16);
	printk("\n");
	qla2x00_dump_buffer(((uint8_t *)mcp->mb + 0x10), 16);
	printk("\n");
	qla2x00_dump_buffer(((uint8_t *)mcp->mb + 0x20), 8);
	printk("\n");
	printk("qla2x00_mailbox_command: I/O address = %lx.\n",
	    (u_long)optr);
	qla2x00_dump_regs(ha->host);
#endif

	/* Issue set host interrupt command to send cmd out. */
	ha->flags.mbox_int = FALSE;
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	/* Unlock mbx registers and wait for interrupt */

	DEBUG11(printk("qla2x00_mailbox_command: going to unlock irq & "
	    "waiting for interrupt. jiffies=%lx.\n", jiffies);)

	/* Wait for mbx cmd completion until timeout */

	if (!abort_active && io_lock_on) {
		/* sleep on completion semaphore */
		DEBUG11(printk("qla2x00_mailbox_command(%ld): "
		    "INTERRUPT MODE. Initializing timer.\n",
		    ha->host_no);)

		init_timer(&tmp_intr_timer);
		tmp_intr_timer.data = (unsigned long)&ha->mbx_intr_sem;
		tmp_intr_timer.expires = jiffies + mcp->tov * HZ;
		tmp_intr_timer.function =
		    (void (*)(unsigned long))qla2x00_mbx_sem_timeout;

		DEBUG11(printk("qla2x00_mailbox_command(%ld): "
		    "Adding timer.\n", ha->host_no);)
		add_timer(&tmp_intr_timer);

		DEBUG11(printk("qla2x00_mailbox_command: going to "
		    "unlock & sleep. time=0x%lx.\n", jiffies);)

		MBOX_TRACE(ha,BIT_0);
		set_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);
#if defined(ISP2300)
        	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			WRT_REG_DWORD(&reg24->hccr, HCCRX_SET_HOST_INT);
			PCI_POSTING(&reg24->hccr);
		} else
#endif	
		{
			WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);
			PCI_POSTING(&reg->host_cmd);
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if (!abort_active)
			QLA_MBX_REG_UNLOCK(ha);

		MBOX_TRACE(ha,BIT_1);

		/* Wait for either the timer to expire
		 * or the mbox completion interrupt
		 */
		down_interruptible(&ha->mbx_intr_sem);

		DEBUG11(printk("qla2x00_mailbox_command:"
		    "waking up."
		    "time=0x%lx\n", jiffies);)
		clear_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);

		/* delete the timer */
		del_timer(&tmp_intr_timer);
#if QLA2100_LIPTEST
		if (mbxtimeout) {
			DEBUG(printk("qla2x00_mailbox_command(%ld): "
			    "INTERRUPT MODE - testing timeout handling.\n",
			    ha->host_no);)
			ha->flags.mbox_int= FALSE;
		}
		mbxtimeout= 0;
#endif

	} else {

		DEBUG3_11(printk("qla2x00_mailbox_command(%ld): cmd=%x "
			"POLLING MODE.\n", ha->host_no, command);)

#if defined(ISP2300)
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			WRT_REG_DWORD(&reg24->hccr, HCCRX_SET_HOST_INT);
			PCI_POSTING(&reg24->hccr);
		} else
#endif
		{
			WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);
			PCI_POSTING(&reg->host_cmd);
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		if (!abort_active)
			QLA_MBX_REG_UNLOCK(ha);


		cnt = jiffies + mcp->tov * HZ; /* wait at most tov secs */

		while (!ha->flags.mbox_int) {

			if (time_after(jiffies, cnt))
				break;

			spin_lock_irqsave(&ha->hardware_lock, flags);
			/* Check for pending interrupts. */
#if defined(ISP2300) 
			if (ha->device_id == QLA2312_DEVICE_ID ||
			    ha->device_id == QLA2322_DEVICE_ID ||
			    ha->device_id == QLA6312_DEVICE_ID ||
			    ha->device_id == QLA6322_DEVICE_ID) {
				while ((data =RD_REG_WORD(
				    &reg->istatus)) & RISC_INT) {

					data =RD_REG_WORD(&reg->host_status_lo);
					if((data & HOST_STATUS_INT ) == 0)
						break;
					qla2x00_isr(ha, data, &discard);
				}

			} else {
				if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
					while((data = RD_REG_DWORD(&reg24->host_status)) & HSRX_RISC_INT)
						qla24xx_isr(ha, data, &discard);
				} else {

					while((data = RD_REG_WORD(&reg->host_status_lo))
					    & HOST_STATUS_INT) 
						qla2x00_isr(ha, data, &discard);
				}
			}
#else
			/* QLA2100 or QLA2200 */
			while((data = RD_REG_WORD(&reg->istatus))
			    & RISC_INT) {
				qla2x00_isr(ha, data, &discard);
			}
#endif
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			/* loading RAM is quick, so don't sleep for it. Also
			 * skip sleeping if flag is already set */

			if (command != MBC_LOAD_RAM_A64  &&
			    !ha->flags.mbox_int) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(10);
			}	
		} /* while */
	}

	if (!abort_active)
		QLA_MBX_REG_LOCK(ha);

	if (!abort_active) {
		DEBUG11(printk("qla2x00_mailbox_cmd: checking for additional "
		    "resp interrupt.\n");)

		/* polling mode for non isp_abort commands. */
		/* Go check for any more response interrupts pending. */
		spin_lock_irqsave(&ha->hardware_lock, flags);
#if defined(ISP2300)

		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			while((data = RD_REG_DWORD(&reg24->host_status)) & HSRX_RISC_INT)
				qla24xx_isr(ha, data, &discard);
		} else {
			while (!(ha->flags.in_isr) &&
			    ((data = qla2x00_debounce_register(&reg->host_status_lo)) &
			    HOST_STATUS_INT))
				qla2x00_isr(ha, data, &discard);
		}
#else

		while (!(ha->flags.in_isr) &&
		    ((data = qla2x00_debounce_register(&reg->istatus)) &
		    RISC_INT))
			qla2x00_isr(ha, data,&discard);
#endif

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}
	/* Clean up */
	ha->mcp = NULL;


	/* Check whether we timed out */
	if (ha->flags.mbox_int) {

		DEBUG3_11(printk("qla2x00_mailbox_cmd: cmd %x completed.\n",
		    command);)

		/* Got interrupt. Clear the flag. */
		ha->flags.mbox_int = FALSE;
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		if( ha->mailbox_out[0] != MBS_CMD_CMP ) {
			qla2x00_stats.mboxerr++;
			status = QL_STATUS_ERROR;
		}

		/* Load return mailbox registers. */
		optr = mcp->mb;
		iptr = (uint16_t *)&ha->mailbox_out[0];
		mboxes = mcp->in_mb;
		for (cnt = 0; cnt < mbx_count; cnt++) {

			if (mboxes & BIT_0)
				*optr = *iptr;

			mboxes >>= 1;
			optr++;
			iptr++;
		}
	} else {

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3) || \
		defined(QL_DEBUG_LEVEL_11)	
		uint16_t mb0;
		uint32_t ictrl;
#if defined(ISP2300)
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			mb0 = RD_REG_WORD(&reg24->mailbox0);
			ictrl = RD_REG_DWORD(&reg24->ictrl);
		} else
#endif	
		{
			mb0 = RD_REG_WORD(&reg->mailbox0);
			ictrl = RD_REG_WORD(&reg->ictrl);
		}
		printk(KERN_INFO "qla2x00_mailbox_command(%ld): **** MB"
		    " Command Timeout for cmd %x ****\n", ha->host_no, command);
		printk(KERN_INFO "qla2x00_mailbox_command: icontrol=%x "
		    "jiffies=%lx\n", ictrl, jiffies);
		printk(KERN_INFO "qla2x00_mailbox_command: *** mailbox[0] " 
		    "= 0x%x ***\n",mb0 );
		printk("qla2x00_mailbox_command(%ld): **** MB Command Timeout "
		    "for cmd %x ****\n", ha->host_no, command);
		printk("qla2x00_mailbox_command: icontrol=%x jiffies=%lx\n",
		    ictrl, jiffies);
		printk("qla2x00_mailbox_command: *** mailbox[0] = 0x%x ***\n",
		    mb0);
		qla2x00_dump_regs(ha->host);
#endif

		qla2x00_stats.mboxtout++;
		ha->total_mbx_timeout++;
		status = QL_STATUS_TIMEOUT;
	}

	if (!abort_active)
		QLA_MBX_REG_UNLOCK(ha);

	ha->flags.mbox_busy = FALSE;


	if (status == QL_STATUS_TIMEOUT ) {
		if (!io_lock_on || (mcp->flags & IOCTL_CMD)) {
			/* not in dpc. schedule it for dpc to take over. */
			DEBUG(printk("qla2x00_mailbox_command(%ld): timeout "
			    "schedule isp_abort_needed.\n",
			    ha->host_no);)
			DEBUG2_3_11(printk("qla2x00_mailbox_command(%ld): "
			    "timeout schedule isp_abort_needed.\n",
			    ha->host_no);)
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			if (ha->dpc_wait && !ha->dpc_active) 
				up(ha->dpc_wait);

		} else if (!abort_active) {

			/* call abort directly since we are in the DPC thread */
			DEBUG(printk("qla2x00_mailbox_command(%ld): timeout "
			    "calling abort_isp\n", ha->host_no);)
			DEBUG2_3_11(printk("qla2x00_mailbox_command(%ld): "
			    "timeout calling abort_isp\n", ha->host_no);)

			set_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
			clear_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			if (qla2x00_abort_isp(ha)) {
				/* failed. retry later. */
				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			}
			clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);

			DEBUG(printk("qla2x00_mailbox_command: finished "
			    "abort_isp\n");)
			DEBUG2_3_11(printk("qla2x00_mailbox_command: finished "
			    "abort_isp\n");)
		}
	}

	/* Allow next mbx cmd to come in. */
	if (!abort_active) {
		tmp_stat = qla2x00_release_mbx_access(ha, mcp->tov);

		if (status == 0)
			status = tmp_stat;
	}

	if (status) {
		DEBUG2_3_11(printk("qla2x00_mailbox_command(%ld): **** FAILED. "
		    "mbx0=%x, mbx1=%x, mbx2=%x, cmd=%x ****\n",
		ha->host_no, mcp->mb[0], mcp->mb[1], mcp->mb[2], command);)
	} else {
		DEBUG11(printk("qla2x00_mailbox_command(%ld): done.\n",
		    ha->host_no);)
	}

	DEBUG11(printk("qla2x00_mailbox_command(%ld): exiting.\n",
	    ha->host_no);)

	return status;
}
/*
 * qla2x00_load_ram
 *	Load adapter RAM using DMA.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dptr = DMA memory physical address.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_load_ram(scsi_qla_host_t *ha, dma_addr_t req_dma,
		uint16_t risc_addr, uint16_t risc_code_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t	req_len;
	dma_addr_t	nml_dma;
	uint32_t	nml_len;
	uint32_t	normalized;

	DEBUG11(printk("qla2x00_load_ram(%ld): entered.\n",
	    ha->host_no);)

	req_len = risc_code_size;
	nml_dma = 0;
	nml_len = 0;

	normalized = qla2x00_normalize_dma_addr(
			&req_dma, &req_len,
			&nml_dma, &nml_len);

	/* Load first segment */
	mcp->mb[0] = MBC_LOAD_RAM_A64;
	mcp->mb[1] = risc_addr;
	mcp->mb[2] = MSW(req_dma);
	mcp->mb[3] = LSW(req_dma);
        mcp->mb[4] = (uint16_t)req_len;
	mcp->mb[6] = MSW(MSD(req_dma));
	mcp->mb[7] = LSW(MSD(req_dma));

	mcp->out_mb = MBX_7|MBX_6|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Load second segment - if necessary */
	if (normalized && (rval == QL_STATUS_SUCCESS)) {
		mcp->mb[0] = MBC_LOAD_RAM_A64;
		mcp->mb[1] = risc_addr + (uint16_t)req_len;
		mcp->mb[2] = MSW(nml_dma);
		mcp->mb[3] = LSW(nml_dma);
		mcp->mb[4] = (uint16_t)nml_len;
		mcp->mb[6] = MSW(MSD(nml_dma));
		mcp->mb[7] = LSW(MSD(nml_dma));

		mcp->out_mb = MBX_7|MBX_6|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
		mcp->in_mb = MBX_0;
		mcp->tov = 60;
		mcp->flags = 0;
		rval = (int)qla2x00_mailbox_command(ha, mcp);
	}

	if (rval == QL_STATUS_SUCCESS) {
		/* Empty */
		DEBUG11(printk("qla2x00_load_ram(%ld): done.\n",
		    ha->host_no);)
	} else {
		/* Empty */
		DEBUG2_3_11(printk("qla2x00_load_ram(%ld): failed. rval=%x "
		    "mb[0]=%x.\n",
		    ha->host_no, rval, mcp->mb[0]);)
	}
	return rval;
}

/*
 * qla2x00_load_ram_ext
 *	Load adapter extended RAM using DMA.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_load_ram_ext(scsi_qla_host_t *ha, dma_addr_t req_dma,
    uint32_t risc_addr, uint32_t risc_code_size)
{
	int rval, shift;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t	req_len;
	dma_addr_t	nml_dma;
	uint32_t	nml_len;
	uint32_t	normalized;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	req_len = risc_code_size;
	nml_dma = 0;
	nml_len = 0;

	/* Convert transfer word count to bytes -- 4gb word is 4 bytes. */
	shift = 1;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		shift = 2;
	req_len = risc_code_size << shift;

	normalized = qla2x00_normalize_dma_addr(&req_dma, &req_len, &nml_dma,
	    &nml_len);

	/* Revert to words. */
	req_len >>= shift;
	nml_len >>= shift;

	/* Load first segment */
	mcp->mb[0] = MBC_LOAD_RAM_EXTENDED;
	mcp->mb[1] = LSW(risc_addr);
	mcp->mb[2] = MSW(req_dma);
	mcp->mb[3] = LSW(req_dma);
	mcp->mb[6] = MSW(MSD(req_dma));
	mcp->mb[7] = LSW(MSD(req_dma));
	mcp->mb[8] = MSW(risc_addr);
	mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[4] = MSW(req_len);
		mcp->mb[5] = LSW(req_len);
		mcp->out_mb |= MBX_5|MBX_4;
	} else {
		mcp->mb[4] = LSW(req_len);
		mcp->out_mb |= MBX_4;
	}
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Load second segment - if necessary */
	if (normalized && (rval == QL_STATUS_SUCCESS)) {
		risc_addr += req_len;
		mcp->mb[0] = MBC_LOAD_RAM_EXTENDED;
		mcp->mb[1] = LSW(risc_addr);
		mcp->mb[2] = MSW(nml_dma);
		mcp->mb[3] = LSW(nml_dma);
		mcp->mb[4] = (uint16_t)nml_len;
		mcp->mb[6] = MSW(MSD(nml_dma));
		mcp->mb[7] = LSW(MSD(nml_dma));
		mcp->mb[8] = MSW(risc_addr);
		mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
		mcp->in_mb = MBX_0;
		mcp->tov = 30;
		mcp->flags = 0;
		rval = qla2x00_mailbox_command(ha, mcp);
	}

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n",
		    __func__, ha->host_no, rval, mcp->mb[0]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}


/*
 * qla2x00_execute_fw
 *	Start adapter firmware.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_execute_fw(scsi_qla_host_t *ha, uint32_t risc_addr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_execute_fw(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_EXECUTE_FIRMWARE;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
#if defined(ISP2300) 
	if (ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) {
		mcp->mb[2] = 0; /* FW image has been loaded into memory */
		mcp->out_mb |= MBX_2;
		DEBUG11(printk("%s fwstart=%x \n",__func__,mcp->mb[1]);)
	}
#endif

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		mcp->mb[3] = 0;/* TODO fill me in... */
		mcp->out_mb |= MBX_3|MBX_2;
		mcp->in_mb |= MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
	}
	

	mcp->tov = 60;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	DEBUG11(printk("qla2x00_execute_fw(%ld): done.\n",
	    ha->host_no);)

	return rval;
}


/*
 * qla2x00_get_fw_version
 *	Get firmware version.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	major:		pointer for major number.
 *	minor:		pointer for minor number.
 *	subminor:	pointer for subminor number.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_get_fw_version(scsi_qla_host_t *ha, uint8_t *major, uint8_t *minor,
    uint8_t *subminor, uint16_t *attributes, uint32_t *memory)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GET_FIRMWARE_VERSION;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->flags = 0;
	mcp->tov = 30;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox data. */
	*major = mcp->mb[1];
	*minor = mcp->mb[2];
	*subminor = mcp->mb[3];
	*attributes = mcp->mb[6];
#if defined(ISP2100) || defined(ISP2200)
		*memory = 0x1FFFF;			/* Defaults to 128KB. */
#else	
		*memory = (mcp->mb[5] << 16) | mcp->mb[4];
#endif

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}
}

/*
 * qla2x00_mbx_reg_test
 *	Mailbox register wrap test.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_mbx_reg_test(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_mbx_reg_test(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_MAILBOX_REGISTER_TEST;
	mcp->mb[1] = 0xAAAA;
	mcp->mb[2] = 0x5555;
	mcp->mb[3] = 0xAA55;
	mcp->mb[4] = 0x55AA;
	mcp->mb[5] = 0xA5A5;
	mcp->mb[6] = 0x5A5A;
	mcp->mb[7] = 0x2525;
	mcp->out_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval == QL_STATUS_SUCCESS) {
		if (mcp->mb[1] != 0xAAAA || mcp->mb[2] != 0x5555 ||
		    mcp->mb[3] != 0xAA55 || mcp->mb[4] != 0x55AA)
			rval = QL_STATUS_ERROR;
		if (mcp->mb[5] != 0xA5A5 || mcp->mb[6] != 0x5A5A ||
		    mcp->mb[7] != 0x2525)
			rval = QL_STATUS_ERROR;
	}

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_mbx_reg_test(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_mbx_reg_test(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_verify_checksum
 *	Verify firmware checksum.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_verify_checksum(scsi_qla_host_t *ha, uint32_t risc_addr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	mcp->mb[0] = MBC_VERIFY_CHECKSUM;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		/* FIXME: Where's the checksum reside! 1/2 or 2/3 !!! */
		mcp->out_mb |= MBX_2|MBX_1;
		mcp->in_mb |= MBX_2|MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
		mcp->out_mb |= MBX_1;
		mcp->in_mb |= MBX_1;
	}
	mcp->tov = 60;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", 
				__func__, ha->host_no, rval);)
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}
/*
 * qla2x00_issue_iocb
 *	Issue IOCB using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	phys_addr = physical address of buffer.
 *	size = size of buffer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_issue_iocb(scsi_qla_host_t *ha, void*  buffer, dma_addr_t phys_addr,
    size_t size)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ENTER("qla2x00_issue_iocb: started");

	mcp->mb[0] = MBC_IOCB_EXECUTE_A64;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(phys_addr);
	mcp->mb[3] = LSW(phys_addr);
	mcp->mb[6] = MSW(MSD(phys_addr));
	mcp->mb[7] = LSW(MSD(phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG(printk("qla2x00_issue_iocb(%ld): failed rval 0x%x",
		    ha->host_no,rval);)
		DEBUG2(printk("qla2x00_issue_iocb(%ld): failed rval 0x%x",
		    ha->host_no,rval);)
	} else {
		sts_entry_t *sts_entry = (sts_entry_t *) buffer;

		/* Mask reserved bits. */
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
			sts_entry->entry_status &= RF_MASK_24XX;
		else
			sts_entry->entry_status &= RF_MASK;
		/*EMPTY*/
		LEAVE("qla2x00_issue_iocb: exiting normally");
	}

	return rval;
}

/*
 * qla2x00_abort_command
 *	Abort command aborts a specified IOCB.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sp = SB structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_abort_command(scsi_qla_host_t *ha, srb_t *sp)
{
	unsigned long   flags = 0;
	fc_port_t	*fcport;
	int		rval;
	uint32_t	handle;
	uint16_t	t;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
		 return qla24xx_abort_command(ha, sp);
	DEBUG11(printk("qla2x00_abort_command(%ld): entered.\n",
	    ha->host_no);)

	fcport = sp->fclun->fcport;

	t = SCSI_TCN_32(sp->cmd);

	if (atomic_read(&ha->loop_state) == LOOP_DOWN ||
	    atomic_read(&fcport->state) == FC_DEVICE_LOST) {
		/* v2.19.8 Ignore abort request if port is down */
		return 1;
	}
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < MAX_OUTSTANDING_COMMANDS; handle++) {
		if (ha->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (handle == MAX_OUTSTANDING_COMMANDS) {
		/* command not found */
		return QL_STATUS_ERROR;
	}
	DEBUG2(printk("%s(%ld): handle to abort=%d\n",
		    __func__, ha->host_no, handle);)

	mcp->mb[0] = MBC_ABORT_COMMAND;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = fcport->loop_id;
#else
	mcp->mb[1] = fcport->loop_id << 8;
#endif
	mcp->mb[2] = (uint16_t)handle;
	mcp->mb[3] = (uint16_t)(handle >> 16);
	mcp->mb[6] = (uint16_t)sp->fclun->lun;
	mcp->out_mb = MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;

	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2_3_11(printk("qla2x00_abort_command(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		sp->flags |= SRB_ABORT_PENDING;
		DEBUG11(printk("qla2x00_abort_command(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_abort_device
 *
 * Input:
 *	ha = adapter block pointer.
 *      loop_id  = FC loop ID
 *      lun  = SCSI LUN.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_abort_device(scsi_qla_host_t *ha, uint16_t loop_id, uint16_t lun)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_abort_device(%ld): entered.\n",
			ha->host_no);)

	mcp->mb[0] = MBC_ABORT_DEVICE;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
	mcp->mb[1] = loop_id << 8;
#endif
	mcp->mb[2] = lun;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Issue marker command. */
	qla2x00_marker(ha, loop_id, lun, MK_SYNC_ID_LUN);

	if (rval != QL_STATUS_SUCCESS) {
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);
		DEBUG2_3_11(printk("qla2x00_abort_device(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_abort_device(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

#if USE_ABORT_TGT
/*
 * qla2x00_abort_target
 *	Issue abort target mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	b = Always 0.
 *	t = SCSI ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_abort_target(fc_port_t *fcport)
{
	int        rval;
	uint16_t   loop_id;
	mbx_cmd_t  mc;
	mbx_cmd_t  *mcp = &mc;
	scsi_qla_host_t *ha;	

	DEBUG11(printk("qla2x00_abort_target(%ld): entered.\n",
	    fcport->ha->host_no);)

	if (fcport == NULL) {
		/* no target to abort */
		return 0;
	}
	ha = fcport->ha;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
		return qla24xx_abort_target(ha, fcport);

	loop_id = fcport->loop_id;

	mcp->mb[0] = MBC_ABORT_TARGET;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = fcport->loop_id;
#else
	mcp->mb[1] = loop_id << 8;
#endif
	mcp->mb[2] = ha->loop_reset_delay;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = 0;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Issue marker command. */
	qla2x00_marker(fcport->ha, loop_id, 0, MK_SYNC_ID);

	if (rval != QL_STATUS_SUCCESS) {
/*		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait); */
		DEBUG2_3_11(printk("qla2x00_abort_target(%ld): failed=%x.\n",
		    fcport->ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_abort_target(%ld): done.\n",
		    fcport->ha->host_no);)
	}

	return rval;
}
#endif

/*
 * qla2x00_target_reset
 *	Issue target reset mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_target_reset(scsi_qla_host_t *ha, uint16_t b, uint16_t t)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	os_tgt_t *tgt;

	DEBUG11(printk("qla2x00_target_reset(%ld): entered.\n", ha->host_no);)

	tgt = TGT_Q(ha, t);
	if (tgt->vis_port == NULL) {
		/* no target to abort */
		return 0;
	}
	if (atomic_read(&tgt->vis_port->state) != FC_ONLINE) {
		/* target not online */
		return 0;
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
		return qla24xx_abort_target(ha, tgt->vis_port);

	DEBUG11(printk("qla2x00_target_reset(%ld): target loop_id=(%x).\n",
	    ha->host_no, tgt->vis_port->loop_id);)

	mcp->mb[0] = MBC_TARGET_RESET;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = tgt->vis_port->loop_id;
#else
	mcp->mb[1] = tgt->vis_port->loop_id << 8;
#endif
	mcp->mb[2] = ha->loop_reset_delay;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_target_reset(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_target_reset(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_get_adapter_id
 *	Get adapter ID and topology.
 *
 * Input:
 *	ha = adapter block pointer.
 *	id = pointer for loop ID.
 *	al_pa = pointer for AL_PA.
 *	area = pointer for area.
 *	domain = pointer for domain.
 *	top = pointer for topology.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_adapter_id(scsi_qla_host_t *ha, uint16_t *id, uint8_t *al_pa,
    uint8_t *area, uint8_t *domain, uint16_t *top)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_adapter_id(%ld): entered.\n",
	    ha->host_no);)
	
	mcp->mb[0] = MBC_GET_ADAPTER_LOOP_ID;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Return data. */
	*id = mcp->mb[1];
	*al_pa = LSB(mcp->mb[2]);
	*area = MSB(mcp->mb[2]);
	*domain	= LSB(mcp->mb[3]);
	*top = mcp->mb[6];

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_adapter_id(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		if (mcp->mb[0] == MBS_CMD_ERR) 
		    	rval =  QL_STATUS_MBX_CMD_ERR;
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_adapter_id(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_get_retry_cnt
 *	Get current firmware login retry count and delay.
 *
 * Input:
 *	ha = adapter block pointer.
 *	retry_cnt = pointer to login retry count.
 *	tov = pointer to login timeout value.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_retry_cnt(scsi_qla_host_t *ha, uint8_t *retry_cnt, uint8_t *tov)
{
	int rval;
	uint16_t ratov;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_retry_cnt(%ld): entered.\n",
			ha->host_no);)

	mcp->mb[0] = MBC_GET_RETRY_COUNT;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_retry_cnt(%ld): failed = %x.\n",
		    ha->host_no, mcp->mb[0]);)
	} else {
		/* Convert returned data and check our values. */
		ratov = (mcp->mb[3]/2) / 10;  /* mb[3] value is in 100ms */
		if (mcp->mb[1] * ratov > (*retry_cnt) * (*tov)) {
			/* Update to the larger values */
			*retry_cnt = (uint8_t)mcp->mb[1];
			*tov = ratov;
		}

		DEBUG11(printk("qla2x00_get_retry_cnt(%ld): done. mb3=%d "
		    "ratov=%d.\n", ha->host_no, mcp->mb[3], ratov);)
                DEBUG2(printk(KERN_INFO "qla2x00_get_retry_cnt(%ld): done."
                       " mb3=%d ratov=%d.\n", ha->host_no, mcp->mb[3], ratov);)
	}

	return rval;
}

#if defined(INTAPI)
/*
 * qla2x00_loopback_test
 *	Send out a LOOPBACK mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	retry_cnt = pointer to login retry count.
 *	tov = pointer to login timeout value.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_loopback_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req,
    uint16_t *ret_mb)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	memset(mcp->mb, 0 , sizeof(mcp->mb));

	mcp->mb[0] = MBC_DIAGNOSTIC_LOOP_BACK;
	mcp->mb[1] = req->Options | BIT_6; /* use 64bit DMA addr */
	mcp->mb[10] = LSW(req->TransferCount);
	mcp->mb[11] = MSW(req->TransferCount);

	mcp->mb[14] = LSW(ha->ioctl_mem_phys); /* send data address */
	mcp->mb[15] = MSW(ha->ioctl_mem_phys);
	mcp->mb[20] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[21] = MSW(MSD(ha->ioctl_mem_phys));

	mcp->mb[16] = LSW(ha->ioctl_mem_phys); /* rcv data address */
	mcp->mb[17] = MSW(ha->ioctl_mem_phys);
	mcp->mb[6]  = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[7]  = MSW(MSD(ha->ioctl_mem_phys));

	mcp->mb[18] = LSW(req->IterationCount); /* iteration count lsb */
	mcp->mb[19] = MSW(req->IterationCount); /* iteration count msb */

	mcp->out_mb = MBX_21|MBX_20|MBX_19|MBX_18|MBX_17|MBX_16|MBX_15|
		MBX_14|MBX_13|MBX_12|MBX_11|MBX_10|MBX_7|MBX_6|MBX_1|MBX_0;
	mcp->in_mb = MBX_19|MBX_18|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->buf_size = req->TransferCount;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = 60;

	DEBUG11(printk("qla2x00_send_loopback: req.Options=%x iterations=%x "
	    "MAILBOX_CNT=%d.\n", req->Options, req->IterationCount,
	    MAILBOX_REGISTER_COUNT);)

	rval = qla2x00_mailbox_command(ha, mcp);

	/* Always copy back return mailbox values. */
	memcpy((void *)ret_mb, (void *)mcp->mb, sizeof(mcp->mb));

	if (rval != QL_STATUS_SUCCESS) {
		/* Empty. */
		DEBUG2_3_11(printk(
		    "qla2x00_loopback_test(%ld): mailbox command FAILED=%x.\n",
		    ha->host_no, mcp->mb[0]);)
	} else {
		/* Empty. */
		DEBUG11(printk(
		    "qla2x00_loopback_test(%ld): done.\n", ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_echo_test
 *	Send out a DIAGNOSTIC ECHO mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	retry_cnt = pointer to login retry count.
 *	tov = pointer to login timeout value.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_echo_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req,
    uint16_t *ret_mb)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	uint16_t	tran_cnt;

	/* Sanity check of proper values */
	tran_cnt = req->TransferCount;

	memset(mcp->mb, 0 , sizeof(mcp->mb));

	mcp->mb[0] = MBC_DIAGNOSTIC_ECHO;
	mcp->mb[1] = BIT_6; /* use 64bit DMA addr */
	mcp->mb[10] = tran_cnt;

	mcp->mb[14] = LSW(ha->ioctl_mem_phys); /* send data address */
	mcp->mb[15] = MSW(ha->ioctl_mem_phys);
	mcp->mb[20] = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[21] = MSW(MSD(ha->ioctl_mem_phys));

	mcp->mb[16] = LSW(ha->ioctl_mem_phys); /* rcv data address */
	mcp->mb[17] = MSW(ha->ioctl_mem_phys);
	mcp->mb[6]  = LSW(MSD(ha->ioctl_mem_phys));
	mcp->mb[7]  = MSW(MSD(ha->ioctl_mem_phys));

	mcp->out_mb = MBX_21|MBX_20|MBX_17|MBX_16|MBX_15|
		MBX_14|MBX_10|MBX_7|MBX_6|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->buf_size = tran_cnt;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = 60;

	rval = qla2x00_mailbox_command(ha, mcp);

	/* Always copy back return mailbox values. */
	memcpy((void *)ret_mb, (void *)mcp->mb, sizeof(mcp->mb));

	if (rval != QL_STATUS_SUCCESS) {
		/* Empty. */
		DEBUG2_3_11(printk(
		    "%s(%ld): mailbox command FAILED=%x.\n",
		    __func__, ha->host_no, mcp->mb[0]);)
	} else {
		/* Empty. */
		DEBUG11(printk(
		    "%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}
#endif /* INTAPI */

/*
 * qla2x00_init_firmware
 *	Initialize adapter firmware.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dptr = Initialization control block pointer.
 *	size = size of initialization control block.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_init_firmware(scsi_qla_host_t *ha, uint16_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_init_firmware(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_INITIALIZE_FIRMWARE;
	mcp->mb[2] = MSW(ha->init_cb_dma);
	mcp->mb[3] = LSW(ha->init_cb_dma);
	mcp->mb[4] = 0;
	mcp->mb[5] = 0;
	mcp->mb[6] = MSW(MSD(ha->init_cb_dma));
	mcp->mb[7] = LSW(MSD(ha->init_cb_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_5|MBX_4|MBX_0;
	mcp->buf_size = size;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = 60;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_init_firmware(%ld): failed=%x "
		    "mb0=%x.\n",
		    ha->host_no, rval, mcp->mb[0]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_init_firmware(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}
#if 0
/*
 * qla2x00_get_port_database
 *	Issue normal/enhanced get port database mailbox command
 *	and copy device name as necessary.
 *
 * Input:
 *	ha = adapter state pointer.
 *	dev = structure pointer.
 *	opt = enhanced cmd option byte.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_port_database(scsi_qla_host_t *ha, fc_port_t *fcport, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	port_database_t *pd;
	dma_addr_t phys_address = 0;

	DEBUG11(printk("qla2x00_get_port_database(%ld): entered.\n",
	    ha->host_no);)

	pd = pci_alloc_consistent(ha->pdev, PORT_DATABASE_SIZE, &phys_address);
	if (pd  == NULL) {
		DEBUG2_3_11(printk("qla2x00_get_port_database(%ld): **** "
		    "Mem Alloc Failed ****",
		    ha->host_no);)
		ha->mem_err++;		
		return QL_STATUS_RESOURCE_ERROR;
	}

	memset(pd, 0, PORT_DATABASE_SIZE);

	if (opt != 0)
		mcp->mb[0] = MBC_ENHANCED_GET_PORT_DATABASE;
	else
		mcp->mb[0] = MBC_GET_PORT_DATABASE;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = fcport->loop_id;
#else
        mcp->mb[1] = fcport->loop_id << 8 | opt;
#endif
	mcp->mb[2] = MSW(phys_address);
	mcp->mb[3] = LSW(phys_address);
	mcp->mb[6] = MSW(MSD(phys_address));
	mcp->mb[7] = LSW(MSD(phys_address));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = opt;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_0;
	mcp->buf_size = PORT_DATABASE_SIZE;
	mcp->flags = MBX_DMA_IN;
	/*mcp->tov = ha->retry_count * ha->login_timeout * 2;*/
	/* mcp->tov =  ha->login_timeout * 2; */
	mcp->tov =  (ha->login_timeout * 2) + (ha->login_timeout/2);
	rval = (int)qla2x00_mailbox_command(ha, mcp);
	if (rval != QL_STATUS_SUCCESS)
		goto gpd_error_out;

	/* Check for logged in. */
	if (pd->master_state != PD_STATE_PORT_LOGGED_IN &&
	    pd->slave_state != PD_STATE_PORT_LOGGED_IN) {
		rval = QL_STATUS_ERROR;
		goto gpd_error_out;
	}

	/* Save some data */
	/* Names are big endian. */
	memcpy(fcport->node_name, pd->node_name, WWN_SIZE);
	memcpy(fcport->port_name, pd->port_name, WWN_SIZE);

	/* Get port_id of device. */
	fcport->d_id.b.al_pa = pd->port_id[2];
	fcport->d_id.b.area = pd->port_id[3];
	fcport->d_id.b.domain = pd->port_id[0];
	fcport->d_id.b.rsvd_1 = 0;

	/* If not target must be initiator or unknown type. */
	if ((pd->prli_svc_param_word_3[0] & BIT_4) == 0)
		fcport->port_type = FCT_INITIATOR;
	else
		fcport->port_type = FCT_TARGET;

gpd_error_out:
	pci_free_consistent(ha->pdev, PORT_DATABASE_SIZE, pd, phys_address);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_port_database(%ld): "
		    "failed=%x.\n", ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_port_database(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}
#endif
/*
 * qla2x00_get_firmware_state
 *	Get adapter firmware state.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dptr = pointer for firmware state.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_firmware_state(scsi_qla_host_t *ha, uint16_t *dptr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_firmware_state(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_GET_FIRMWARE_STATE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Return firmware state. */
	*dptr = mcp->mb[1];

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_firmware_state(%ld): "
		    "failed=%x.\n", ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_firmware_state(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_get_firmware_options
 *	Set firmware options.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fwopt = pointer for firmware options.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_firmware_options(scsi_qla_host_t *ha, uint16_t *fwopts1,
    uint16_t *fwopts2, uint16_t *fwopts3, uint16_t *mbx_stat)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	mcp->mb[0] = MBC_GET_FIRMWARE_OPTIONS;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n",
		    __func__, ha->host_no, rval);)
	} else {
		*mbx_stat = mcp->mb[0];
		*fwopts1 = mcp->mb[1];
		*fwopts2 = mcp->mb[2];
		*fwopts3 = mcp->mb[3];

		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}


#if defined(ISP2300) 
/*
 * qla2x00_set_firmware_options
 *	Set firmware options.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fwopt = pointer for firmware options.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_set_firmware_options(scsi_qla_host_t *ha,
    uint16_t fwopts1, uint16_t fwopts2, uint16_t fwopts3, 
    uint16_t fwopts10, uint16_t fwopts11, uint16_t *mbx_stat)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	mcp->mb[0] = MBC_SET_FIRMWARE_OPTIONS;
	mcp->mb[1] = fwopts1;
	mcp->mb[2] = fwopts2;
	mcp->mb[3] = fwopts3;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->in_mb = MBX_1|MBX_0;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	} else {
		/* Notify the F/W its a Fibre Lite Board */
		if (ha->device_id == QLA6312_DEVICE_ID)
			mcp->mb[2] = fwopts2 | BIT_13 ;
		mcp->mb[10] = fwopts10;
		mcp->mb[11] = fwopts11;
		mcp->mb[12] = 0;	/* Undocumented, but used */
		mcp->out_mb = MBX_12|MBX_11|MBX_10|MBX_3|MBX_2|MBX_1|MBX_0;
		mcp->in_mb = MBX_0;
	}
	mcp->tov = 60;
	mcp->flags = 0;

	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n",
		    __func__, ha->host_no, rval);)
	} else {
		/*EMPTY*/
		*mbx_stat = mcp->mb[0];
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}
#endif

int
qla2x00_read_ram_word( scsi_qla_host_t *ha, uint32_t addr, uint32_t *data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_READ_RAM_EXTENDED;
	mcp->mb[1] = LSW(addr);
	mcp->mb[8] = MSW(addr);
	mcp->out_mb = MBX_8|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
		mcp->in_mb |= MBX_3;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
		*data = mcp->mb[2];
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
			*data |= mcp->mb[3] << 16;
	}

	return rval;
}

/*
 * qla2x00_write_ram_word
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_write_ram_word(scsi_qla_host_t *ha, uint32_t addr, uint32_t data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_WRITE_RAM_WORD_EXTENDED;
	mcp->mb[1] = LSW(addr);
	mcp->mb[2] = LSW(data);
	mcp->mb[8] = MSW(addr);
	mcp->out_mb = MBX_8|MBX_2|MBX_1|MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[3] = MSW(data);
		mcp->out_mb |= MBX_3;
	}
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_port_name
 *	Issue get port name mailbox command.
 *	Returned name is in big endian format.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop ID of device.
 *	name = pointer for name.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_port_name(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t *name,
    uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_port_name(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_GET_PORT_NAME;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = loop_id << 8 | opt;
#endif
	mcp->out_mb = MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = opt;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_port_name(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		if (name != NULL) {
			/* This function returns name in big endian. */
			name[0] = LSB(mcp->mb[2]);
			name[1] = MSB(mcp->mb[2]);
			name[2] = LSB(mcp->mb[3]);
			name[3] = MSB(mcp->mb[3]);
			name[4] = LSB(mcp->mb[6]);
			name[5] = MSB(mcp->mb[6]);
			name[6] = LSB(mcp->mb[7]);
			name[7] = MSB(mcp->mb[7]);
		}

		DEBUG11(printk("qla2x00_get_port_name(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_get_link_status
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	ret_buf = pointer to link status return buffer.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = mem alloc error.
 *	BIT_1 = mailbox error.
 */
STATIC uint8_t
qla2x00_get_link_status(scsi_qla_host_t *ha, uint16_t loop_id,
    link_stat_t *ret_buf, uint16_t *status)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	link_stat_t *stat_buf;
	dma_addr_t phys_address = 0;


	DEBUG11(printk("qla2x00_get_link_status(%ld): entered.\n",
	    ha->host_no);)

	stat_buf = pci_alloc_consistent(ha->pdev, sizeof(link_stat_t),
	    &phys_address);
	if (stat_buf == NULL) {
		DEBUG2_3_11(printk("qla2x00_get_link_status(%ld): Failed to "
		    "allocate memory.\n", ha->host_no));
		return BIT_0;
	}

	memset(mcp, 0, sizeof(mbx_cmd_t));
	memset(stat_buf, 0, sizeof(link_stat_t));

	mcp->mb[0] = MBC_GET_LINK_STATUS;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = loop_id << 8;
#endif
	mcp->mb[2] = MSW(phys_address);
	mcp->mb[3] = LSW(phys_address);
	mcp->mb[6] = MSW(MSD(phys_address));
	mcp->mb[7] = LSW(MSD(phys_address));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = 0;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[4] = 0;
		mcp->in_mb |= MBX_1;
		mcp->out_mb |= MBX_4;
	}

	mcp->tov = 60;
	mcp->flags = IOCTL_CMD;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval == QL_STATUS_SUCCESS) {

		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2_3_11(printk("qla2x00_get_link_status(%ld): cmd "
			    "failed. mbx0=%x.\n", ha->host_no, mcp->mb[0]);)
			status[0] = mcp->mb[0];
			rval = BIT_1;
		} else {
			/* copy over data -- firmware data is LE. */
			ret_buf->link_fail_cnt =
			    le32_to_cpu(stat_buf->link_fail_cnt);
			ret_buf->loss_sync_cnt =
			    le32_to_cpu(stat_buf->loss_sync_cnt);
			ret_buf->loss_sig_cnt =
			    le32_to_cpu(stat_buf->loss_sig_cnt);
			ret_buf->prim_seq_err_cnt =
			    le32_to_cpu(stat_buf->prim_seq_err_cnt);
			ret_buf->inval_xmit_word_cnt =
			    le32_to_cpu(stat_buf->inval_xmit_word_cnt);
			ret_buf->inval_crc_cnt =
			    le32_to_cpu(stat_buf->inval_crc_cnt);

			DEBUG(printk("qla2x00_get_link_status(%ld): stat dump: "
			    "fail_cnt=%d loss_sync=%d loss_sig=%d seq_err=%d "
			    "inval_xmt_word=%d inval_crc=%d.\n",
			    ha->host_no, ret_buf->link_fail_cnt,
			    ret_buf->loss_sync_cnt, ret_buf->loss_sig_cnt,
			    ret_buf->prim_seq_err_cnt,
			    ret_buf->inval_xmit_word_cnt,
			    ret_buf->inval_crc_cnt);)
			DEBUG11(printk("qla2x00_get_link_status(%ld): stat "
			    "dump: fail_cnt=%d loss_sync=%d loss_sig=%d "
			    "seq_err=%d inval_xmt_word=%d inval_crc=%d.\n",
			    ha->host_no, ret_buf->link_fail_cnt,
			    ret_buf->loss_sync_cnt, ret_buf->loss_sig_cnt,
			    ret_buf->prim_seq_err_cnt,
			    ret_buf->inval_xmit_word_cnt,
			    ret_buf->inval_crc_cnt);)
		}
	} else {
		/* Failed. */
		DEBUG2_3_11(printk("qla2x00_get_link_status(%ld): failed=%x.\n",
		    ha->host_no, rval);)
		rval = BIT_1;
	}

	pci_free_consistent(ha->pdev, sizeof(link_stat_t), stat_buf,
	    phys_address);

	return rval;
}

#if defined(ISP2300)
uint8_t
qla24xx_get_isp_stats(scsi_qla_host_t *ha, uint32_t *dwbuf, uint32_t dwords,
    uint16_t *status)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t  *siter;
	link_stat_t *stat_buf;
	dma_addr_t sbuf_dma = 0;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)
	stat_buf = pci_alloc_consistent(ha->pdev, sizeof(link_stat_t),
			    &sbuf_dma);
	if (stat_buf == NULL) {
		DEBUG2_3_11(printk("qla2x00_get_link_status(%ld): Failed to "
		    "allocate memory.\n", ha->host_no));
		return BIT_0;
	}

	memset(stat_buf, 0, sizeof(link_stat_t));

	mcp->mb[0] = MBC_GET_LINK_PRIV_STATS;
	mcp->mb[2] = MSW(sbuf_dma);
	mcp->mb[3] = LSW(sbuf_dma);
	mcp->mb[6] = MSW(MSD(sbuf_dma));
	mcp->mb[7] = LSW(MSD(sbuf_dma));
	mcp->mb[8] = dwords;
	mcp->mb[10] = 0;
	mcp->out_mb = MBX_10|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = IOCTL_CMD;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval == QLA2X00_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2_3_11(printk("%s(%ld): cmd failed. mbx0=%x.\n",
			    __func__, ha->host_no, mcp->mb[0]));
			status[0] = mcp->mb[0];
			rval = BIT_1;
		} else {
			/* Copy over data -- firmware data is LE. */
			siter = (uint32_t *)stat_buf;
			while (dwords--)
				*dwbuf++ = le32_to_cpu(*siter++);
		}
	} else {
		/* Failed. */
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
		rval = BIT_1;
	}

	pci_free_consistent(ha->pdev, sizeof(link_stat_t), stat_buf, sbuf_dma);

	return rval;
}
#endif

/*
 * qla2x00_lip_reset
 *	Issue LIP reset mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_lip_reset(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_lip_reset(%ld): entered.\n",
	    ha->host_no);)
	
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[0] = MBC_LIP_FULL_LOGIN;
		mcp->mb[1] = BIT_0;
		mcp->mb[2] = 0xff;
		mcp->mb[3] = 0;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	} else {
		mcp->mb[0] = MBC_LIP_RESET;
#if defined(EXTENDED_IDS)
		mcp->mb[1] = 0x00ff;
#else
		mcp->mb[1] = 0xff00;
#endif
		mcp->mb[2] = ha->loop_reset_delay;
		mcp->mb[3] = 0;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10;
#endif
		mcp->mb[1] = 0xff00;
	}
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_lip_reset(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_lip_reset(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_send_sns
 *	Send SNS command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer for command.
 *	cmd_size = command size in 16-bit words
 *	buf_size = response/command size.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_send_sns(scsi_qla_host_t *ha, dma_addr_t sns_phys_address,
    uint16_t cmd_size, size_t buf_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_send_sns(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_SEND_SNS_COMMAND;
	mcp->mb[1] = cmd_size;
	mcp->mb[2] = MSW(sns_phys_address);
	mcp->mb[3] = LSW(sns_phys_address);
	mcp->mb[6] = MSW(MSD(sns_phys_address));
	mcp->mb[7] = LSW(MSD(sns_phys_address));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0|MBX_1;
	mcp->buf_size = buf_size;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN;
	/*mcp->tov = ha->retry_count * ha->login_timeout * 2;*/
	/* mcp->tov =  ha->login_timeout * 2; */
	mcp->tov =  (ha->login_timeout * 2) + (ha->login_timeout/2);

	DEBUG11(printk("qla2x00_send_sns: retry cnt=%d ratov=%d total "
	    "tov=%d.\n", ha->retry_count, ha->login_timeout, mcp->tov);)

	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG(printk("qla2x00_send_sns(%ld): failed=%x mb[0]=%x "
		    "mb[1]=%x.\n",
		    ha->host_no, rval, mcp->mb[0], mcp->mb[1]);)
		DEBUG2_3_11(printk("qla2x00_send_sns(%ld): failed=%x mb[0]=%x "
		    "mb[1]=%x.\n",
		    ha->host_no, rval, mcp->mb[0], mcp->mb[1]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_send_sns(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}


int
qla24xx_login_device(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *mb, uint8_t opt)
{
	int		rval;

	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;
	uint32_t	iop[2];

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)
	lg = pci_alloc_consistent(ha->pdev, sizeof(struct logio_entry_24xx), 
					&lg_dma);

	if (lg == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Logout IOCB.\n",
		    __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}

	memset(lg, 0, sizeof(struct logio_entry_24xx));

	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags = __constant_cpu_to_le16(LCF_COMMAND_PLOGI);
	if (opt & BIT_0)
		lg->control_flags |= __constant_cpu_to_le16(LCF_COND_PLOGI);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;
	rval = qla2x00_issue_iocb(ha, lg, lg_dma, 0);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Login IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
	} else if (lg->entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    lg->entry_status));
		rval = QLA2X00_FUNCTION_FAILED;
	} else if (lg->comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		iop[0] = le32_to_cpu(lg->io_parameter[0]);
		iop[1] = le32_to_cpu(lg->io_parameter[1]);

		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x)  ioparam=%x/%x.\n", __func__,
		    ha->host_no, le16_to_cpu(lg->comp_status), iop[0],
		    iop[1]));

		switch (iop[0]) {
		case LSC_SCODE_PORTID_USED:
			mb[0] = MBS_PORT_ID_USED;
			mb[1] = LSW(iop[1]);
			break;
		case LSC_SCODE_NPORT_USED:
			mb[0] = MBS_LOOP_ID_USED;
			break;
		case LSC_SCODE_NOLINK:
		case LSC_SCODE_NOIOCB:
		case LSC_SCODE_NOXCB:
		case LSC_SCODE_CMD_FAILED:
		case LSC_SCODE_NOFABRIC:
		case LSC_SCODE_FW_NOT_READY:
		case LSC_SCODE_NOT_LOGGED_IN:
		case LSC_SCODE_NOPCB:
		case LSC_SCODE_ELS_REJECT:
		case LSC_SCODE_CMD_PARAM_ERR:
		case LSC_SCODE_NONPORT:
		case LSC_SCODE_LOGGED_IN:
		case LSC_SCODE_NOFLOGI_ACC:
		default:
			mb[0] = MBS_CMD_ERR;
			break;
		}
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)

		iop[0] = le32_to_cpu(lg->io_parameter[0]);

		mb[0] = MBS_CMD_CMP;
		mb[1] = 0;
		if (iop[0] & BIT_4) {
			if (iop[0] & BIT_8)
				mb[1] |= BIT_1;
		} else
			mb[1] = BIT_0;
	}

	pci_free_consistent(ha->pdev, sizeof(struct logio_entry_24xx), 
				lg, lg_dma);

	return rval;
}

/*
 * qla2x00_login_fabric
 *	Issue login fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	domain = device domain.
 *	area = device area.
 *	al_pa = device AL_PA.
 *	status = pointer for return status.
 *	opt = command options.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_login_fabric(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *status, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_login_fabric(%ld): entered.\n",
	    ha->host_no);)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_login_device(ha, loop_id, domain, area, al_pa,
						status, opt);

	mcp->mb[0] = MBC_LOGIN_FABRIC_PORT;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = (loop_id << 8) | opt;
#endif
	mcp->mb[2] = domain;
	mcp->mb[3] = area << 8 | al_pa;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = opt;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	/*mcp->tov = ha->retry_count * ha->login_timeout * 2;*/
	/* mcp->tov =  ha->login_timeout * 2; */
	mcp->tov =  (ha->login_timeout * 2) + (ha->login_timeout/2);
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox statuses. */
	if (status != NULL) {
		*status++ = mcp->mb[0];
		*status++ = mcp->mb[1];
		*status = mcp->mb[2];
	}

	if (rval != QL_STATUS_SUCCESS) {
		/* RLU tmp code: need to change main mailbox_command function to
		 * return ok even when the mailbox completion value is not
		 * SUCCESS. The caller needs to be responsible to interpret
		 * the return values of this mailbox command if we're not
		 * to change too much of the existing code.
		 */
		if (mcp->mb[0] == 0x4001 || mcp->mb[0] == 0x4002 ||
		    mcp->mb[0] == 0x4003 || mcp->mb[0] == 0x4005 ||
		    mcp->mb[0] == 0x4006)
			rval = QL_STATUS_SUCCESS;

		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_login_fabric(%ld): failed=%x "
		    "mb[0]=%x mb[1]=%x mb[2]=%x.\n",
		    ha->host_no, rval, mcp->mb[0], mcp->mb[1], mcp->mb[2]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_login_fabric(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_login_local_device
 *           Issue login loop port mailbox command.
 *    
 * Input:
 *           ha = adapter block pointer.
 *           loop_id = device loop ID.
 *           opt = command options.
 *          
 * Returns:
 *            Return status code.
 *             
 * Context:
 *            Kernel context.
 *             
 */
STATIC int
qla2x00_login_local_device(scsi_qla_host_t *ha,
		fc_port_t * fcport, uint16_t *mb_ret, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG3(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_login_device(ha, fcport->loop_id, fcport->d_id.b.domain, 
						fcport->d_id.b.area, fcport->d_id.b.al_pa,
						mb_ret, 0);

	mcp->mb[0] = MBC_LOGIN_LOOP_PORT;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = fcport->loop_id;
#else
        mcp->mb[1] = (fcport->loop_id << 8);
#endif
	mcp->mb[2] = opt;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
 	mcp->in_mb = MBX_7|MBX_6|MBX_1|MBX_0;
	/* mcp->tov =  ha->login_timeout * 2; */
	mcp->tov =  (ha->login_timeout * 2) + (ha->login_timeout/2);
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

 	/* Return mailbox statuses. */
 	if (mb_ret != NULL) {
 		mb_ret[0] = mcp->mb[0];
 		mb_ret[1] = mcp->mb[1];
 		mb_ret[6] = mcp->mb[6];
 		mb_ret[7] = mcp->mb[7];
 	}

	if (rval != QL_STATUS_SUCCESS) {
 		/* AV tmp code: need to change main mailbox_command function to
 		 * return ok even when the mailbox completion value is not
 		 * SUCCESS. The caller needs to be responsible to interpret
 		 * the return values of this mailbox command if we're not
 		 * to change too much of the existing code.
 		 */
 		if (mcp->mb[0] == 0x4005 || mcp->mb[0] == 0x4006)
 			rval = QL_STATUS_SUCCESS;

		DEBUG(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x "
		    "mb[6]=%x mb[7]=%x.\n",
		    __func__, ha->host_no, rval, mcp->mb[0], mcp->mb[1],
		    mcp->mb[6], mcp->mb[7]);)
		DEBUG2_3(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x "
		    "mb[6]=%x mb[7]=%x.\n",
		    __func__, ha->host_no, rval, mcp->mb[0], mcp->mb[1],
		    mcp->mb[6], mcp->mb[7]);)
	} else {
		/*EMPTY*/
		DEBUG3(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return (rval);
}
/*
 * qla24xx_fabric_logout
 *	Issue logout fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla24xx_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
{
	int		rval;
	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	lg = pci_alloc_consistent(ha->pdev, sizeof(struct logio_entry_24xx), 
					&lg_dma);

	if (lg == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Logout IOCB.\n",
		    __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}
	memset(lg, 0, sizeof(struct logio_entry_24xx));

	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags = __constant_cpu_to_le16(LCF_COMMAND_LOGO | LCF_EXPL_LOGO);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;

	rval = qla2x00_issue_iocb(ha, lg, lg_dma, 0);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Logout IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
	}  else if (lg->entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    lg->entry_status));
		rval = QLA2X00_FUNCTION_FAILED;
	} else if (lg->comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x)  ioparam=%x/%x.\n", __func__,
		    ha->host_no, le16_to_cpu(lg->comp_status),
		    le32_to_cpu(lg->io_parameter[0]),
		    le32_to_cpu(lg->io_parameter[1]));)
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	pci_free_consistent(ha->pdev, sizeof(struct logio_entry_24xx), 
				lg, lg_dma);
	DEBUG3(printk("%s(%ld) leaving rval=%d\n", __func__, ha->host_no, rval);)
	return rval;
}

/*
 * qla2x00_fabric_logout
 *	Issue logout fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
 {
 	int rval;
 	mbx_cmd_t mc;
 	mbx_cmd_t *mcp = &mc;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_fabric_logout(ha, loop_id, domain, area, al_pa);

	DEBUG11(printk("qla2x00_fabric_logout(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_LOGOUT_FABRIC_PORT;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = loop_id << 8;
#endif
	mcp->out_mb = MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = 0;
        mcp->out_mb |= MBX_10;
#endif

	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_fabric_logout(%ld): failed=%x "
		    "mbx1=%x.\n",
		    ha->host_no, rval, mcp->mb[1]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_fabric_logout(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_full_login_lip
 *	Issue full login LIP mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_full_login_lip(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_full_login_lip(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_LIP_FULL_LOGIN;
	mcp->mb[1] = 0;
	mcp->mb[2] = 0;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_full_login_lip(%ld): failed=%x.\n",
		    ha->instance, rval);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_full_login_lip(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

/*
 * qla2x00_get_id_list
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_id_list(scsi_qla_host_t *ha, void *id_list, dma_addr_t id_list_dma,
    uint16_t *entries)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_id_list(%ld): entered.\n",
	    ha->host_no);)

	if (id_list == NULL)
		return QLA2X00_FUNCTION_FAILED;

	mcp->mb[0] = MBC_GET_ID_LIST;
	mcp->out_mb = MBX_0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		mcp->mb[2] = MSW(id_list_dma);
		mcp->mb[3] = LSW(id_list_dma);
		mcp->mb[6] = MSW(MSD(id_list_dma));
		mcp->mb[7] = LSW(MSD(id_list_dma));
		mcp->mb[8] = MAX_ID_LIST_SIZE;
		mcp->out_mb |= MBX_8|MBX_7|MBX_6|MBX_3|MBX_2;
	} else {
		mcp->mb[1] = MSW(id_list_dma);
		mcp->mb[2] = LSW(id_list_dma);
		mcp->mb[3] = MSW(MSD(id_list_dma));
		mcp->mb[6] = LSW(MSD(id_list_dma));
		mcp->out_mb |= MBX_6|MBX_3|MBX_2|MBX_1;
	}
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_id_list(%ld): failed=%x.\n",
		    ha->host_no, rval);)
	} else {
		*entries = mcp->mb[1];
		DEBUG11(printk("qla2x00_get_id_list(%ld): done.\n",
		    ha->host_no);)
	}

	return rval;
}

#if 0 /* not yet needed */
STATIC int
qla2x00_dump_ram(scsi_qla_host_t *ha, uint32_t risc_address,
    dma_addr_t ispdump_dma, uint32_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	mcp->mb[0] = MBC_DUMP_RAM;
	mcp->mb[1] = risc_address & 0xffff;
	mcp->mb[3] = LSW(ispdump_dma);
	mcp->mb[2] = MSW(ispdump_dma);
	mcp->mb[4] = MSW(MSD(ispdump_dma));
	mcp->mb[6] = LSW(MSD(ispdump_dma));
	mcp->mb[7] = 0;
	mcp->out_mb = MBX_7|MBX_6|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	return rval;
}
#endif

/*
 * qla2x00_lun_reset
 *	Issue lun reset mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *      lun = lun to be reset.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
__qla2x00_lun_reset(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	uint16_t 	loop_id = fcport->loop_id;

	ENTER("qla2x00_lun_reset");

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		return qla24xx_lun_reset(ha, fcport, lun);
	}
	mcp->mb[0] = MBC_LUN_RESET;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = loop_id << 8;
#endif
	mcp->mb[2] = lun;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 60 * 2;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		printk(KERN_WARNING "qla2x00_lun_reset(%d): failed = %d",
		    (int)ha->instance, rval);
	} else {
		printk(KERN_INFO "qla2x00_lun_reset(%d): issue marker 0x%x lun %d",
		    (int)ha->instance, loop_id, lun);
		rval = qla2x00_marker(ha, loop_id, lun, MK_SYNC_ID_LUN);
		if (rval != QLA2X00_SUCCESS) {
		printk(KERN_WARNING "qla2x00_lun_reset(%d): marker failed = %d",
		    (int)ha->instance, rval);
		}
		/*EMPTY*/
		LEAVE("qla2x00_lun_reset: exiting normally");
	}

	return rval;
}

int
qla2x00_lun_reset(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun)
{
	int		retry;
	int		rval;

	retry = 2;
	do {
		rval = 	__qla2x00_lun_reset(ha, fcport, lun);
		if ( rval != QLA2X00_SUCCESS ) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(10);
		}
	} while ( rval != QLA2X00_SUCCESS &&
		retry--);
	return rval;
}

/*
 * qla2x00_send_rnid_mbx
 *	Issue RNID ELS using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	loop_id = loop ID of the target device.
 *	data_fmt = currently supports only 0xDF.
 *	buffer = buffer pointer.
 *	buf_size = size of buffer.
 *	mb_reg = pointer to return mailbox registers.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_send_rnid_mbx(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t data_fmt,
    dma_addr_t buf_phys_addr, size_t buf_size, uint16_t *mb_reg)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("qla2x00_send_rnid_mbx(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_SEND_RNID_ELS;
#if defined(EXTENDED_IDS)
        mcp->mb[1] = loop_id;
#else
        mcp->mb[1] = (loop_id << 8) | data_fmt;
#endif
	mcp->mb[2] = MSW(buf_phys_addr);
	mcp->mb[3] = LSW(buf_phys_addr);
	mcp->mb[6] = MSW(MSD(buf_phys_addr));
	mcp->mb[7] = LSW(MSD(buf_phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
#if defined(EXTENDED_IDS)
        mcp->mb[10] = data_fmt;
        mcp->out_mb |= MBX_10;
#endif
	mcp->in_mb = MBX_1|MBX_0;
	mcp->buf_size = buf_size;
	mcp->flags = MBX_DMA_IN;
	mcp->tov = 60;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QL_STATUS_SUCCESS) {
		memcpy(mb_reg, mcp->mb, 2 * 2); /* 2 status regs */

		DEBUG2_3_11(printk("qla2x00_send_rnid_mbx(%ld): failed=%x "
		    "mb[1]=%x.\n",
		    ha->host_no, mcp->mb[0], mcp->mb[1]);)
	} else {
		/*EMPTY*/
	 	DEBUG11(printk("qla2x00_send_rnid_mbx(%ld): done.\n",
		     ha->host_no);)
	}

	return (rval);
}

/*
 * qla2x00_set_rnid_params_mbx
 *	Set RNID parameters using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	buf_size = size of buffer.
 *	mb_reg = pointer to return mailbox registers.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_set_rnid_params_mbx(scsi_qla_host_t *ha, dma_addr_t buf_phys_addr,
    size_t buf_size, uint16_t *mb_reg)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("qla2x00_set_rnid_params_mbx(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_SET_RNID_PARAMS;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(buf_phys_addr);
	mcp->mb[3] = LSW(buf_phys_addr);
	mcp->mb[6] = MSW(MSD(buf_phys_addr));
	mcp->mb[7] = LSW(MSD(buf_phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->buf_size = buf_size;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = 60;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		memcpy(mb_reg, mcp->mb, 2 * 2); /* 2 status regs */

		DEBUG2_3_11(printk("qla2x00_set_rnid_params_mbx(%ld): "
		    "failed=%x mb[1]=%x.\n",
		    ha->host_no, mcp->mb[0], mcp->mb[1]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_set_rnid_params_mbx(%ld): done.\n",
		    ha->host_no);)
	}

	return (rval);
}

/*
 * qla2x00_get_rnid_params_mbx
 *	Get RNID parameters using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	buf_size = size of buffer.
 *	mb_reg = pointer to return mailbox registers.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_rnid_params_mbx(scsi_qla_host_t *ha, dma_addr_t buf_phys_addr,
    size_t buf_size, uint16_t *mb_reg)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("qla2x00_get_rnid_params_mbx(%ld): entered.\n",
	    ha->host_no);)

	mcp->mb[0] = MBC_GET_RNID_PARAMS;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(buf_phys_addr);
	mcp->mb[3] = LSW(buf_phys_addr);
	mcp->mb[6] = MSW(MSD(buf_phys_addr));
	mcp->mb[7] = LSW(MSD(buf_phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->buf_size = buf_size;
	mcp->flags = MBX_DMA_IN;
	mcp->tov = 60;
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		memcpy(mb_reg, mcp->mb, 2 * 2); /* 2 status regs */

		DEBUG2_3_11(printk("qla2x00_get_rnid_params_mbx(%ld): "
		    "failed=%x mb[1]=%x.\n",
		    ha->host_no, mcp->mb[0], mcp->mb[1]);)
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_rnid_params_mbx(%ld): done.\n",
		    ha->host_no);)
	}

	return (rval);
}

#if defined(QL_DEBUG_LEVEL_3)
/*
 * qla2x00_get_fcal_position_map
 *	Get FCAL (LILP) position map using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	pos_map = buffer pointer (can be NULL).
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	char *pmap;
	dma_addr_t pmap_dma;

	pmap = pci_alloc_consistent(ha->pdev, FCAL_MAP_SIZE, &pmap_dma);
	if (pmap  == NULL) {
		DEBUG2_3_11(printk("%s(%ld): **** Mem Alloc Failed ****",
		    __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}

	memset(pmap, 0, FCAL_MAP_SIZE);

	mcp->mb[0] = MBC_GET_FCAL_MAP;
	mcp->mb[2] = MSW(pmap_dma);
	mcp->mb[3] = LSW(pmap_dma);
	mcp->mb[6] = MSW(MSD(pmap_dma));
	mcp->mb[7] = LSW(MSD(pmap_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->buf_size = FCAL_MAP_SIZE;
	mcp->flags = MBX_DMA_IN;
	/* mcp->tov =  ha->login_timeout * 2; */
	mcp->tov =  (ha->login_timeout * 2) + (ha->login_timeout/2);
	rval = (int)qla2x00_mailbox_command(ha, mcp);

	if (rval == QL_STATUS_SUCCESS) {
		DEBUG11(printk("%s(%ld): (mb0=%x/mb1=%x) FC/AL Position Map "
		    "size (%x)\n",
		    __func__, ha->host_no,
		    mcp->mb[0], mcp->mb[1], (unsigned)pmap[0]));
		DEBUG11(qla2x00_dump_buffer(pmap, pmap[0] + 1));

		if (pos_map)
			memcpy(pos_map, pmap, FCAL_MAP_SIZE);
	}
	pci_free_consistent(ha->pdev, FCAL_MAP_SIZE, pmap, pmap_dma);

	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n",
		    __func__, ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n",
		    __func__, ha->host_no));
	}

	return rval;
}
#endif

// Alternative to MAILBOX command 0x15 (Abort IOCB Command)
//

int
qla24xx_abort_command(scsi_qla_host_t *ha, srb_t *sp)
{
	int		rval;
	fc_port_t	*fcport;
	unsigned long   flags = 0;

	struct abort_entry_24xx *abt;
	dma_addr_t	abt_dma;
	uint32_t	handle;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	fcport = sp->fclun->fcport;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < MAX_OUTSTANDING_COMMANDS; handle++) {
		if (ha->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (handle == MAX_OUTSTANDING_COMMANDS) {
		/* Command not found. */
		return QLA2X00_FUNCTION_FAILED;
	}

	abt = pci_alloc_consistent(ha->pdev, sizeof(struct abort_entry_24xx),
					 &abt_dma);
	if (abt == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Abort IOCB.\n",
		    __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}
	DEBUG2(printk("%s(%ld): handle to abort=%d\n",
		    __func__, ha->host_no, handle);)
	printk(KERN_INFO "%s(%ld): handle to abort=%d\n",
		    __func__, ha->host_no, handle);
	memset(abt, 0, sizeof(struct abort_entry_24xx));

	abt->entry_type = ABORT_IOCB_TYPE;
	abt->entry_count = 1;
	abt->nport_handle = cpu_to_le16(fcport->loop_id);
	abt->handle_to_abort = handle;
	abt->port_id[0] = fcport->d_id.b.al_pa;
	abt->port_id[1] = fcport->d_id.b.area;
	abt->port_id[2] = fcport->d_id.b.domain;
	rval = qla2x00_issue_iocb(ha, abt, abt_dma, 0);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue IOCB (%x).\n",
		    __func__, ha->host_no, rval);)
	} else {
		if (abt->nport_handle != __constant_cpu_to_le16(0)) {
			DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
			    "-- completion status (%x).\n", __func__,
			    ha->host_no, le16_to_cpu(abt->nport_handle));)
			rval = QLA2X00_FUNCTION_FAILED;
		} else {
			DEBUG11(printk("%s(%ld): done.\n", __func__,
			    ha->host_no);)
			sp->flags |= SRB_ABORT_PENDING;
		}
	}

	pci_free_consistent(ha->pdev, sizeof(struct abort_entry_24xx), 
				abt, abt_dma);

#if DEBUG_GET_FW_DUMP
#if defined(ISP2300)
	/* Dump the Firmware to tracH the iocb */
	qla24xx_fw_dump(ha, 0);
	qla24xx_console_fw_dump(ha);
#endif
#endif

	return rval;
}

// NOTE: The driver does not issue an ABORT DEVICE mailbox command

struct tsk_mgmt_cmd {
	union {
		struct tsk_mgmt_entry tsk;
		struct sts_entry_24xx sts;
	} p;
};

// NOTE: The following function handles mailbox command 0x17 and 0x66
int
qla24xx_abort_target(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	int		rval;

	struct tsk_mgmt_cmd *tsk;
	dma_addr_t	tsk_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	tsk = pci_alloc_consistent(ha->pdev, sizeof(struct tsk_mgmt_cmd),
					 &tsk_dma);
	if (tsk == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Task Management "
		    "IOCB.\n", __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}
	memset(tsk, 0, sizeof(struct tsk_mgmt_cmd));

	tsk->p.tsk.entry_type = TSK_MGMT_IOCB_TYPE;
	tsk->p.tsk.entry_count = 1;
	tsk->p.tsk.nport_handle = cpu_to_le16(fcport->loop_id);
	tsk->p.tsk.timeout = __constant_cpu_to_le16(25);
	//FIXME
	//tsk->p.tsk.lun[1] = LSB(lun);
	////tsk->p.tsk.lun[2] = MSB(lun);
	tsk->p.tsk.control_flags = __constant_cpu_to_le32(TCF_TARGET_RESET);
	tsk->p.tsk.port_id[0] = fcport->d_id.b.al_pa;
	tsk->p.tsk.port_id[1] = fcport->d_id.b.area;
	tsk->p.tsk.port_id[2] = fcport->d_id.b.domain;
	rval = qla2x00_issue_iocb(ha, tsk, tsk_dma, 0);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Target Reset IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
		goto atarget_done;
	}

	if (tsk->p.sts.comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x).\n", __func__,
		    ha->host_no, le16_to_cpu(tsk->p.sts.comp_status));)
		rval = QLA2X00_FUNCTION_FAILED;
		goto atarget_done;
	}

	/* Issue marker IOCB. */
	rval = qla2x00_marker(ha, fcport->loop_id, 0, MK_SYNC_ID);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Marker IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

atarget_done:
	pci_free_consistent(ha->pdev, sizeof(struct tsk_mgmt_cmd),
				 tsk, tsk_dma);

	return rval;
}

int
qla24xx_lun_reset(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun)
{
	int		rval;

	struct tsk_mgmt_cmd *tsk;
	dma_addr_t	tsk_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	tsk = pci_alloc_consistent(ha->pdev, sizeof(struct tsk_mgmt_cmd),
					 &tsk_dma);
	if (tsk == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Task Management "
		    "IOCB.\n", __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}
	memset(tsk, 0, sizeof(struct tsk_mgmt_cmd));

	tsk->p.tsk.entry_type = TSK_MGMT_IOCB_TYPE;
	tsk->p.tsk.entry_count = 1;
	tsk->p.tsk.nport_handle = cpu_to_le16(fcport->loop_id);
	tsk->p.tsk.timeout = __constant_cpu_to_le16(25);
	tsk->p.tsk.lun[1] = LSB(lun);
	tsk->p.tsk.lun[2] = MSB(lun);
	host_to_fcp_swap(tsk->p.tsk.lun, sizeof(tsk->p.tsk.lun));
	tsk->p.tsk.control_flags = __constant_cpu_to_le32(TCF_LUN_RESET);
	tsk->p.tsk.port_id[0] = fcport->d_id.b.al_pa;
	tsk->p.tsk.port_id[1] = fcport->d_id.b.area;
	tsk->p.tsk.port_id[2] = fcport->d_id.b.domain;
	rval = qla2x00_issue_iocb(ha, tsk, tsk_dma, 0);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Lun Reset IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
		goto lreset_done;
	}

	if (tsk->p.sts.comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x).\n", __func__,
		    ha->host_no, le16_to_cpu(tsk->p.sts.comp_status));)
		rval = QLA2X00_FUNCTION_FAILED;
		goto lreset_done;
	}

	/* Issue marker IOCB. */
	rval = qla2x00_marker(ha, fcport->loop_id, lun, MK_SYNC_ID_LUN);
	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Marker IOCB "
		    "(%x).\n", __func__, ha->host_no, rval);)
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

lreset_done:
	pci_free_consistent(ha->pdev, sizeof(struct tsk_mgmt_cmd),
				 tsk, tsk_dma);

	return rval;
}

// NOTE: The following function handles mailbox command 0x6c and 0x72
int
qla24xx_full_login_lip(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	mcp->mb[0] = MBC_LIP_FULL_LOGIN;
	mcp->mb[1] = BIT_6;
	mcp->mb[2] = 0;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no);)
	}

	return rval;
}

// MAILBOX command 0x64 (Get Port Database)
//

int
qla2x00_get_port_database(scsi_qla_host_t *ha, fc_port_t *fcport, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	port_database_t *pd;
	struct port_database_24xx *pd24;
	dma_addr_t pd_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	pd24 = NULL;
	pd = (port_database_t *) pci_alloc_consistent(ha->pdev,
			max(PORT_DATABASE_SIZE, PORT_DATABASE_24XX_SIZE),
			&pd_dma);
	if (pd  == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Port Database "
		    "structure.\n", __func__, ha->host_no));
		return QL_STATUS_RESOURCE_ERROR;
	}
	memset(pd, 0, max(PORT_DATABASE_SIZE, PORT_DATABASE_24XX_SIZE));

	mcp->mb[0] = MBC_GET_PORT_DATABASE;
	if (opt != 0 && !check_24xx_or_54xx_device_ids(ha) && 
			!check_25xx_device_ids(ha))
		mcp->mb[0] = MBC_ENHANCED_GET_PORT_DATABASE;
	mcp->mb[2] = MSW(pd_dma);
	mcp->mb[3] = LSW(pd_dma);
	mcp->mb[6] = MSW(MSD(pd_dma));
	mcp->mb[7] = LSW(MSD(pd_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
#if defined(EXTENDED_IDS)
	mcp->mb[1] = fcport->loop_id;
	mcp->mb[10] = opt;
	mcp->out_mb |= MBX_10|MBX_1;
#else
	mcp->mb[1] = fcport->loop_id << 8 | opt;
	mcp->out_mb |= MBX_1;
#endif
	mcp->in_mb = MBX_0;
	mcp->buf_size = (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)
			 ?  PORT_DATABASE_24XX_SIZE : PORT_DATABASE_SIZE);
	mcp->flags = MBX_DMA_IN;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	rval = qla2x00_mailbox_command(ha, mcp);
	if (rval != QLA2X00_SUCCESS)
		goto gpd_error_out;

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		pd24 = (struct port_database_24xx *) pd;

		/* Check for logged in state. */
		if (pd24->current_login_state != PDS_PRLI_COMPLETE &&
		    pd24->last_login_state != PDS_PRLI_COMPLETE) {
			rval = QLA2X00_FUNCTION_FAILED;
			goto gpd_error_out;
		}

		/* Names are little-endian. */
		memcpy(fcport->node_name, pd24->node_name, WWN_SIZE);
		memcpy(fcport->port_name, pd24->port_name, WWN_SIZE);

		/* Get port_id of device. */
		fcport->d_id.b.domain = pd24->port_id[0];
		fcport->d_id.b.area = pd24->port_id[1];
		fcport->d_id.b.al_pa = pd24->port_id[2];
		fcport->d_id.b.rsvd_1 = 0;

		/* If not target must be initiator or unknown type. */
		if ((pd24->prli_svc_param_word_3[0] & BIT_4) == 0)
			fcport->port_type = FCT_INITIATOR;
		else
			fcport->port_type = FCT_TARGET;
	} else {
		/* Check for logged in state. */
		if (pd->master_state != PD_STATE_PORT_LOGGED_IN &&
		    pd->slave_state != PD_STATE_PORT_LOGGED_IN) {
			rval = QLA2X00_FUNCTION_FAILED;
			goto gpd_error_out;
		}

		/* Names are little-endian. */
		memcpy(fcport->node_name, pd->node_name, WWN_SIZE);
		memcpy(fcport->port_name, pd->port_name, WWN_SIZE);

		/* Get port_id of device. */
		fcport->d_id.b.domain = pd->port_id[0];
		fcport->d_id.b.area = pd->port_id[3];
		fcport->d_id.b.al_pa = pd->port_id[2];
		fcport->d_id.b.rsvd_1 = 0;

		/* Check for device require authentication. */
		pd->common_features & BIT_5 ? (fcport->flags |= FC_AUTH_REQ) :
		    (fcport->flags &= ~FC_AUTH_REQ);

		/* If not target must be initiator or unknown type. */
		if ((pd->prli_svc_param_word_3[0] & BIT_4) == 0)
			fcport->port_type = FCT_INITIATOR;
		else
			fcport->port_type = FCT_TARGET;
	}

gpd_error_out:
	pci_free_consistent(ha->pdev, 
		max(PORT_DATABASE_SIZE, PORT_DATABASE_24XX_SIZE), 
		pd, pd_dma);

	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_system_error(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!check_24xx_or_54xx_device_ids(ha) && !check_25xx_device_ids(ha))
		return QLA2X00_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GEN_SYSTEM_ERROR;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 5;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}
/**
 * qla2x00_set_serdes_params() -
 * @ha: HA context
 *
 * Returns
 */
int
qla2x00_set_serdes_params(scsi_qla_host_t *ha, uint16_t sw_em_1g,
    uint16_t sw_em_2g, uint16_t sw_em_4g)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_SERDES_PARAMS;
	mcp->mb[1] = BIT_0;
	mcp->mb[2] = sw_em_1g | BIT_15;
	mcp->mb[3] = sw_em_2g | BIT_15;
	mcp->mb[4] = sw_em_4g | BIT_1;
	mcp->out_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x (%x).\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

//UNKNOWNs

// MAILBOX command 0x22 (Get Retry Counts)
//	Mailbox 1 no longer populated with 'Retry Count'
//
// MAILBOX command 0x69 (Get Firmware State)
//	Firmware state 1 (Waiting for Link) previously wait for AL_PA
//	Firmware state 5 (Error) is missing
//	Firmware state 6 (ReInit) is missing
//


int
qla2x00_stop_firmware(scsi_qla_host_t *ha)
{
	 int rval;
	 mbx_cmd_t mc;
	 mbx_cmd_t *mcp = &mc;
	
	if (!check_24xx_or_54xx_device_ids(ha) && !check_25xx_device_ids(ha))
		 return QLA2X00_FUNCTION_FAILED;
	
	 DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));
	
	 mcp->mb[0] = MBC_STOP_FIRMWARE;
	 mcp->out_mb = MBX_0;
	 mcp->in_mb = MBX_0;
	 mcp->tov = 30;
	 mcp->flags = 0;
	 rval = qla2x00_mailbox_command(ha, mcp);
	
	 if (rval != QLA2X00_SUCCESS) {
	     DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		 ha->host_no, rval));
	 } else {
		     DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	 }
		    
	 return rval;
}

