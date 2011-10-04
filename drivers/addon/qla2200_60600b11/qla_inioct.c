/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.4.x
 * Copyright (C) 2003 Qlogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

#include "inioct.h"

extern int qla2x00_loopback_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req,
    uint16_t *ret_mb);

int qla2x00_read_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_update_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_write_nvram_word(scsi_qla_host_t *, uint8_t, uint16_t);
int qla2x00_send_loopback(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_read_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_update_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);

int
qla2x00_read_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	char		*ptmp_buf;
	int		ret = 0;

#if defined(ISP2300)
	device_reg_t	*reg = ha->iobase;
#endif
#if defined(ISP2100)
	uint32_t	nvram_size = sizeof(nvram21_t);
#else
	uint32_t	nvram_size = sizeof(nvram22_t);
#endif
	uint16_t	data;
	uint16_t	cnt, base;
 	uint16_t	*wptr;
	uint32_t	transfer_size;

	DEBUG9(printk("qla2x00_read_nvram: entered.\n");)

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&ptmp_buf,
	    nvram_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    nvram_size);)
		return (ret);
	}

	if (pext->ResponseLen < nvram_size)
		transfer_size = pext->ResponseLen / 2;
	else
		transfer_size = nvram_size / 2;

	/* Dump NVRAM. */
#if defined(ISP2300)
	if (ha->device_id == QLA2312_DEVICE_ID) {
		data = RD_REG_WORD(&reg->ctrl_status);
		if ((data >> 14) == 1)
			base = 0x80;
		else
			base = 0;

		data = RD_REG_WORD(&reg->nvram);
		while (data & NV_BUSY) {
			UDELAY(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0x1);
		UDELAY(5);

		data = RD_REG_WORD(&reg->host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			UDELAY(100);
			WRT_REG_WORD(&reg->host_semaphore, 0x1);
			UDELAY(5);
			data = RD_REG_WORD(&reg->host_semaphore);
		}
	} else {
		base = 0;
	}
#else
	base = 0;
#endif

 	wptr = (uint16_t *)ptmp_buf;
 	for (cnt = 0; cnt < transfer_size; cnt++) {
		*wptr = cpu_to_le16(qla2x00_get_nvram_word(ha, (cnt+base)));
		wptr++;
 	}

#if defined(ISP2300)
	if (ha->device_id == QLA2312_DEVICE_ID) {
		/* Unlock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0);
	}
#endif

	ret = copy_to_user((uint8_t *)pext->ResponseAdr, ptmp_buf,
	    transfer_size * 2);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (ret);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_read_nvram: exiting.\n");)

	return (ret);
}

/*
 * qla2x00_update_nvram
 *	Write data to NVRAM.
 *
 * Input:
 *	ha = adapter block pointer.
 *	pext = pointer to driver internal IOCTL structure.
 *
 * Returns:
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_update_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
#if defined(ISP2300)
	device_reg_t	*reg = ha->iobase;
#endif
#if defined(ISP2100)
	nvram21_t	*pnew_nv;
	uint32_t	nvram_size = sizeof(nvram21_t);
#else
	nvram22_t	*pnew_nv;
	uint32_t	nvram_size = sizeof(nvram22_t);
#endif
	uint8_t		chksum = 0;
	uint8_t		*usr_tmp, *kernel_tmp;
	uint16_t	i, cnt, base;
	uint16_t	data;
	uint16_t	*wptr;
	uint32_t	transfer_size;
	int		ret = 0;

	// FIXME: Endianess?
	DEBUG9(printk("qla2x00_update_nvram: entered.\n");)

	if (pext->RequestLen < nvram_size)
		transfer_size = pext->RequestLen;
	else
		transfer_size = nvram_size;

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&pnew_nv,
	    nvram_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    nvram_size);)
		return (ret);
	}

	/* Read from user buffer */
	kernel_tmp = (uint8_t *)pnew_nv;
	usr_tmp = (uint8_t *)pext->RequestAdr;

	ret = verify_area(VERIFY_READ, (void *)usr_tmp, transfer_size);
	if (ret) {
		DEBUG9_10(printk(
		    "qla2x00_update_nvram: ERROR in buffer verify READ. "
		    "RequestAdr=%p\n", pext->RequestAdr);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return ret;
	}

	copy_from_user(kernel_tmp, usr_tmp, transfer_size);

	kernel_tmp = (uint8_t *)pnew_nv;

	/* we need to checksum the nvram */
	for (i = 0; i < nvram_size - 1; i++) {
		chksum += *kernel_tmp;
		kernel_tmp++;
	}

	chksum = ~chksum + 1;

	*kernel_tmp = chksum;

	/* Write to NVRAM */
#if defined(ISP2300)
	if (ha->device_id == QLA2312_DEVICE_ID) {
		data = RD_REG_WORD(&reg->ctrl_status);
		if ((data >> 14) == 1)
			base = 0x80;
		else
			base = 0;

		data = RD_REG_WORD(&reg->nvram);
		while (data & NV_BUSY) {
			UDELAY(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0x1);
		UDELAY(5);

		data = RD_REG_WORD(&reg->host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			UDELAY(100);
			WRT_REG_WORD(&reg->host_semaphore, 0x1);
			UDELAY(5);
			data = RD_REG_WORD(&reg->host_semaphore);
		}
	} else {
		base = 0;
	}
#else
	base = 0;
#endif

	wptr = (uint16_t *)pnew_nv;
	for (cnt = 0; cnt < transfer_size / 2; cnt++) {
		data = cpu_to_le16(*wptr++);
		qla2x00_write_nvram_word(ha, (cnt+base), data);
	}

#if defined(ISP2300)
	if (ha->device_id == QLA2312_DEVICE_ID) {
		/* Unlock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0);
	}
#endif

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_update_nvram: exiting.\n");)

	return 0;
}

int
qla2x00_write_nvram_word(scsi_qla_host_t *ha, uint8_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	device_reg_t *reg = ha->iobase;

	qla2x00_nv_write(ha, NV_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NV_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Erase Location */
	nv_cmd = (addr << 16) | NV_ERASE_OP;
	nv_cmd <<= 5;
	for (count = 0; count < 11; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NV_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for Erase to Finish */
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NV_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NV_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NV_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NV_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);

	DEBUG9(printk("qla2x00_write_nvram_word: exiting.\n");)

	return 0;
}

int
qla2x00_send_loopback(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		status;
	uint16_t	ret_mb[MAILBOX_REGISTER_COUNT];
	INT_LOOPBACK_REQ req;
	INT_LOOPBACK_RSP rsp;

	DEBUG9(printk("qla2x00_send_loopback: entered.\n");)


	if (pext->RequestLen != sizeof(INT_LOOPBACK_REQ)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid RequestLen =%d.\n",
		    pext->RequestLen);)
		return pext->Status;
	}

	if (pext->ResponseLen != sizeof(INT_LOOPBACK_RSP)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid ResponseLen =%d.\n",
		    pext->ResponseLen);)
		return pext->Status;
	}

	status = verify_area(VERIFY_READ, (void *)pext->RequestAdr,
	    pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify read of "
		    "request buffer.\n");)
		return pext->Status;
	}

	copy_from_user((uint8_t *)&req, (uint8_t *)pext->RequestAdr,
	    pext->RequestLen);

	status = verify_area(VERIFY_READ, (void *)pext->ResponseAdr,
	    pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify read of "
		    "response buffer.\n");)
		return pext->Status;
	}

	copy_from_user((uint8_t *)&rsp, (uint8_t *)pext->ResponseAdr,
	    pext->ResponseLen);

	if (req.TransferCount > req.BufferLength ||
	    req.TransferCount > rsp.BufferLength) {

		/* Buffer lengths not large enough. */
		pext->Status = EXT_STATUS_INVALID_PARAM;

		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid TransferCount =%d. "
		    "req BufferLength =%d rspBufferLength =%d.\n",
		    req.TransferCount, req.BufferLength, rsp.BufferLength);)

		return pext->Status;
	}

	status = verify_area(VERIFY_READ, (void *)req.BufferAddress,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify read of "
		    "user loopback data buffer.\n");)
		return pext->Status;
	}

	copy_from_user((uint8_t *)ha->ioctl_mem, (uint8_t *)req.BufferAddress,
	    req.TransferCount);

	DEBUG9(printk("qla2x00_send_loopback: req -- bufadr=%p, buflen=%x, "
	    "xfrcnt=%x, rsp -- bufadr=%p, buflen=%x.\n",
	    req.BufferAddress, req.BufferLength, req.TransferCount,
	    rsp.BufferAddress, rsp.BufferLength);)

	/*
	 * AV - the caller of this IOCTL expects the FW to handle
	 * a loopdown situation and return a good status for the
	 * call function and a LOOPDOWN status for the test operations
	 */
	/*if (ha->loop_state != LOOP_READY || */
	if (
	    (test_bit(CFG_ACTIVE, &ha->cfg_flags)) ||
	    (test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) ||
	    ABORTS_ACTIVE || ha->dpc_active) {

		pext->Status = EXT_STATUS_BUSY;
		DEBUG9_10(printk("qla2x00_send_loopback(%ld): "
		    "loop not ready.\n", ha->host_no);)
		return pext->Status;
	}

	if (ha->current_topology == ISP_CFG_F) {
#if defined(ISP2300)
		status = qla2x00_echo_test(ha, &req, ret_mb);
#else
		pext->Status = EXT_STATUS_INVALID_REQUEST ;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR "
		    "command only supported for QLA23xx.\n");)
		return 0 ;
#endif
	} else {
		status = qla2x00_loopback_test(ha, &req, ret_mb);
	}

	if (status) {
		if (status == QL_STATUS_TIMEOUT ) {
			pext->Status = EXT_STATUS_BUSY;
			DEBUG9_10(printk("qla2x00_send_loopback: ERROR "
			    "command timed out.\n");)
			return 0;
		} else {
			/* EMPTY. Just proceed to copy back mailbox reg
			 * values for users to interpret.
			 */
			DEBUG10(printk("qla2x00_send_loopback: ERROR "
			    "loopback command failed 0x%x.\n", ret_mb[0]);)
		}
	}

	status = verify_area(VERIFY_WRITE, (void *)rsp.BufferAddress,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify "
		    "write of return data buffer.\n");)
		return status ;
	}

	DEBUG9(printk("qla2x00_send_loopback: loopback mbx cmd ok. "
	    "copying data.\n");)

	/* put loopback return data in user buffer */
	copy_to_user((uint8_t *)rsp.BufferAddress,
	    (uint8_t *)ha->ioctl_mem, req.TransferCount);

	rsp.CompletionStatus = ret_mb[0];

	if (ha->current_topology == ISP_CFG_F) {
		rsp.CommandSent = INT_DEF_LB_ECHO_CMD;
	} else {
		if (rsp.CompletionStatus == INT_DEF_LB_COMPLETE ||
		    rsp.CompletionStatus == INT_DEF_LB_CMD_ERROR) {
			rsp.CrcErrorCount = ret_mb[1];
			rsp.DisparityErrorCount = ret_mb[2];
			rsp.FrameLengthErrorCount = ret_mb[3];
			rsp.IterationCountLastError =
			    (ret_mb[19] << 16) | ret_mb[18];
		}
	}

	status = verify_area(VERIFY_WRITE, (void *)pext->ResponseAdr,
	    pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify "
		    "write of response buffer.\n");)
		return pext->Status;
	}

	copy_to_user((uint8_t *)pext->ResponseAdr, (uint8_t *)&rsp,
	    pext->ResponseLen);

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("qla2x00_send_loopback: exiting.\n");)

	return pext->Status;
}

int qla2x00_read_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	uint8_t		*usr_tmp;
	uint32_t	addr;
	uint32_t	midpoint;
	uint32_t	transfer_size;
	uint8_t		data;
	device_reg_t	*reg = ha->iobase;
	unsigned long	cpu_flags;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (pext->ResponseLen != FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		return (1);
	}

	transfer_size = FLASH_IMAGE_SIZE;

	midpoint = FLASH_IMAGE_SIZE / 2;
	usr_tmp = (uint8_t *)pext->ResponseAdr;

	/* Dump FLASH. */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
	qla2x00_flash_enable(ha);
	WRT_REG_WORD(&reg->nvram, 0);
	for (addr = 0; addr < transfer_size; addr++, usr_tmp++) {
		if (addr == midpoint)
			WRT_REG_WORD(&reg->nvram, NV_SELECT);

		data = qla2x00_read_flash_byte(ha, addr);
		__put_user(data, usr_tmp);
	}
	qla2x00_flash_disable(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return (0);
}

int qla2x00_update_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret;
	uint8_t		*usr_tmp;
	uint8_t		*kern_tmp;
	uint16_t	status;
	unsigned long	cpu_flags;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (pext->RequestLen != FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return (1);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	/* Read from user buffer */
	usr_tmp = (uint8_t *)pext->RequestAdr;
	ret = verify_area(VERIFY_READ, (void *)usr_tmp, FLASH_IMAGE_SIZE);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR in buffer verify READ. "
				"RequestAdr=%p\n",
				__func__, pext->RequestAdr);)
		return (ret);
	}

	kern_tmp = (uint8_t *)KMEM_ZALLOC(FLASH_IMAGE_SIZE, 30);
	if (kern_tmp == NULL) {
		pext->Status = EXT_STATUS_COPY_ERR;
		printk(KERN_WARNING
			"%s: ERROR in flash allocation.\n", __func__);
		return (1);
	}
	copy_from_user(kern_tmp, usr_tmp, FLASH_IMAGE_SIZE);

	/* Go with update */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
	status = qla2x00_set_flash_image(ha, kern_tmp);
	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	KMEM_FREE(kern_tmp, FLASH_IMAGE_SIZE);

	if (status) {
		ret = 1;
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR updating flash.\n", __func__);)
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return (ret);
}
