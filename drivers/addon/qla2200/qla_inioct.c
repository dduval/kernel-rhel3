/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


#include <linux/vmalloc.h>
#include "qla2x00.h"
#include "inioct.h"

extern int qla2x00_loopback_test(scsi_qla_host_t *ha, INT_LOOPBACK_REQ *req,
    uint16_t *ret_mb);

int qla24xx_read_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_read_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_update_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_write_nvram_word(scsi_qla_host_t *, uint8_t, uint16_t);
int qla2x00_send_loopback(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_read_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_read_option_rom_ext(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_update_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_update_option_rom_ext(scsi_qla_host_t *, EXT_IOCTL *, int);
int qla2x00_get_option_rom_layout(scsi_qla_host_t *, EXT_IOCTL *, int);
static void qla2x00_get_option_rom_table(scsi_qla_host_t *,
    INT_OPT_ROM_REGION **, unsigned long * );

/* Option ROM definitions. */
INT_OPT_ROM_REGION OptionRomTable2312[] = 
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHBIOS_FCODE_EFI_CFW, INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

INT_OPT_ROM_REGION OptionRomTable6312[] = // 128k x20000
{
    {INT_OPT_ROM_REGION_ALL,    INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHBIOS_CFW, INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

INT_OPT_ROM_REGION OptionRomTableHp[] = // 128k x20000
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_PHEFI_PHECFW_PHVPD, INT_OPT_ROM_SIZE_2312,
	    0, INT_OPT_ROM_SIZE_2312-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

INT_OPT_ROM_REGION  OptionRomTable2322[] = // 1 M x100000
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2322,
	    0, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_PHBIOS_PHFCODE_PHEFI_FW, INT_OPT_ROM_SIZE_2322,
	    0, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

INT_OPT_ROM_REGION  OptionRomTable6322[] = // 1 M x100000
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2322,
	    0, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_PHBIOS_FW, INT_OPT_ROM_SIZE_2322,
	    0, INT_OPT_ROM_SIZE_2322-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

INT_OPT_ROM_REGION OptionRomTable2422[] = // 1 M x100000
{
    {INT_OPT_ROM_REGION_ALL, INT_OPT_ROM_SIZE_2422,
	    0, INT_OPT_ROM_SIZE_2422-1},
    {INT_OPT_ROM_REGION_PHBIOS_PHFCODE_PHEFI, 0x40000,
	    0, 0x40000-1 },
    {INT_OPT_ROM_REGION_FW, 0x80000,
	    0x80000, INT_OPT_ROM_SIZE_2422-1},
    {INT_OPT_ROM_REGION_NONE, 0, 0, 0 }
};

#if defined(ISP2300)
/*****************************************************************************/
/* Flash Manipulation Routines                                               */
/*****************************************************************************/
static inline uint32_t
flash_data_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_DATA | faddr;
}

static inline uint32_t
flash_conf_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_CONF | faddr;
}

static inline uint32_t
nvram_conf_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_CONF | naddr;
}

static inline uint32_t
nvram_data_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_DATA | naddr;
}

int
qla24xx_write_flash_dword(scsi_qla_host_t *ha, uint32_t addr, uint32_t data)
{
	int rval;
	uint32_t cnt;
	struct device_reg_24xx *reg = (struct device_reg_24xx *)ha->iobase;

	WRT_REG_DWORD(&reg->flash_data, data);
	RD_REG_DWORD(&reg->flash_data);         /* PCI Posting. */
	WRT_REG_DWORD(&reg->flash_addr, addr | FARX_DATA_FLAG);
	/* Wait for Write cycle to complete. */
	rval = QLA2X00_SUCCESS;
	for (cnt = 500000; (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) &&
	    rval == QLA2X00_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA2X00_FUNCTION_FAILED;
	}
	return rval;
}

int
qla24xx_read_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int     ret = 0;
	char    *ptmp_buf;
	uint32_t transfer_size;
	unsigned long flags;
	uint32_t i;
	uint32_t *dwptr, naddr;
	struct device_reg_24xx *reg;

	DEBUG9(printk("%s: entered.\n",__func__);)

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&ptmp_buf,
	    ha->nvram_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    ha->nvram_size);)
		return (ret);
	}

	transfer_size = ha->nvram_size;
	if (pext->ResponseLen < ha->nvram_size)
		transfer_size = pext->ResponseLen;

	reg = (struct device_reg_24xx *)ha->iobase;

	/* Dump NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Dword reads to flash. */
	naddr = ha->nvram_base;
	dwptr = (uint32_t *)ptmp_buf;
	for (i = 0; i < ha->nvram_size >> 2; i++, naddr++)
		dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    nvram_data_to_access_addr(naddr)));

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	printk("%s(%ld): transfer_size=%d.\n",
	    __func__, ha->host_no, transfer_size);
	DEBUG9(qla2x00_dump_buffer((uint8_t *)ptmp_buf, transfer_size);)

	ret = copy_to_user(pext->ResponseAdr, ptmp_buf, transfer_size);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance);)
			qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("%s: exiting.\n",__func__);)

	return (ret);
}
#endif

int
qla2x00_read_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	char		*ptmp_buf;
	int		ret = 0;

#if defined(ISP2300)
	device_reg_t	*reg = ha->iobase;
	uint16_t	data;
	uint32_t	wait_cnt;
#endif
#if defined(ISP2100)
	uint32_t	nvram_size = sizeof(nvram21_t);
#else
	uint32_t	nvram_size = sizeof(nvram22_t);
#endif
	uint16_t	cnt, base;
 	uint16_t	*wptr;
	uint32_t	transfer_size;

	DEBUG9(printk("qla2x00_read_nvram: entered.\n");)

#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_read_nvram(ha, pext, mode);
#endif
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
	if (ha->device_id == QLA2312_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) { 	    
		data = RD_REG_WORD(&reg->ctrl_status);
		if ((data >> 14) == 1)
			base = 0x80;
		else
			base = 0;

		wait_cnt = 20000;
		data = RD_REG_WORD(&reg->nvram);
		while (data & NV_BUSY) {
			wait_cnt--;
			if(wait_cnt == 0) { 
				DEBUG9_10(printk("%s(%ld) NVRAM Busy\n",
						__func__,ha->host_no);)
				return ret;
			}
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
	if (ha->device_id == QLA2312_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) { 	    
		/* Unlock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0);
		PCI_POSTING(&reg->host_semaphore);
	}
#endif

	ret = copy_to_user(pext->ResponseAdr, ptmp_buf, transfer_size * 2);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_read_nvram: exiting.\n");)

	return (ret);
}

#if defined(ISP2300)
/*
 * qla24xx_update_nvram
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
qla24xx_update_nvram(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	uint8_t cnt;
	uint8_t *usr_tmp, *kernel_tmp;
	struct nvram_24xx *pnew_nv;
	uint32_t transfer_size;
	int ret = 0;
	unsigned long flags;
	uint32_t *iter;
	uint32_t chksum;
	uint32_t i;
	uint32_t *dwptr, naddr;
	struct device_reg_24xx *reg = (struct device_reg_24xx *)ha->iobase;


	DEBUG9(printk("qla2x00_update_nvram: entered.\n");)

	if (pext->RequestLen < ha->nvram_size)
		transfer_size = pext->RequestLen;
	else
		transfer_size = ha->nvram_size;

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&pnew_nv,
	    ha->nvram_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance, ha->nvram_size));
		return (ret);
	}

	/* Read from user buffer */
	kernel_tmp = (uint8_t *)pnew_nv;
	usr_tmp = (uint8_t *)pext->RequestAdr;

	ret = copy_from_user(kernel_tmp, usr_tmp, transfer_size);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk(
		    "qla2x00_update_nvram: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n", pext->RequestAdr);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	/* Checksum NVRAM. */

	iter = (uint32_t *)pnew_nv;
	chksum = 0;
	for (cnt = 0; cnt < ((ha->nvram_size >> 2) - 1); cnt++)
		chksum += le32_to_cpu(*iter++);
	chksum = ~chksum + 1;
	*iter = cpu_to_le32(chksum);

	ret = QLA2X00_SUCCESS;
	/* Write NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101), 0);
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101), 0);

	/* Dword writes to flash. */
	dwptr = (uint32_t *)pnew_nv;
	naddr = ha->nvram_base;
	for (i = 0; i < transfer_size >> 2; i++, naddr++, dwptr++) {
		if (qla24xx_write_flash_dword(ha,
		    nvram_data_to_access_addr(naddr), cpu_to_le32(*dwptr)) !=
		    QLA2X00_SUCCESS) {
			DEBUG9(printk("%s(%ld) Unable to program "
			    "nvram address=%x data=%x.\n", __func__,
			    ha->host_no, naddr, *dwptr));
			ret = QLA2X00_FUNCTION_FAILED;
			break;
		}
	}

	/* Enable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0x8c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	/* Schedule DPC to restart the RISC */
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	up(ha->dpc_wait);

	if (qla2x00_wait_for_hba_online(ha) != QLA2X00_SUCCESS) {
		pext->Status = EXT_STATUS_ERR;
	}

	DEBUG9(printk("qla2x00_update_nvram: exiting.\n");)
	return ret;
}
#endif

static void
qla2x00_set_nvram_protection(scsi_qla_host_t *ha, int stat)
{
	device_reg_t *reg;
	uint32_t word;
	reg = ha->iobase;
	uint32_t	wait_cnt;

	if (stat != QLA2X00_SUCCESS)
		return;

	/* Set NVRAM write protection. */
	/* Write enable. */
	qla2x00_nv_write(ha, NV_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NV_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
	qla2x00_nv_write(ha, NV_PR_ENABLE);
	qla2x00_nv_write(ha, NV_PR_ENABLE);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NV_DATA_OUT | NV_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
	qla2x00_nv_write(ha, NV_PR_ENABLE);
	qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NV_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready. */
	wait_cnt = 20000;
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	PCI_POSTING(&reg->nvram);
	do {
		wait_cnt--;
		if(wait_cnt == 0) { 
			DEBUG9_10(printk(
			    "%s(%ld) Wait for NVRAM Ready Over\n",
			    __func__,ha->host_no);)
			return;
		}
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NV_DATA_IN) == 0);
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
	uint32_t	word;
	uint16_t	wprot, wprot_old;
	uint32_t	wait_cnt;
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
	uint8_t		stat = 0;

	// FIXME: Endianess?
	DEBUG9(printk("qla2x00_update_nvram: entered.\n");)

#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_update_nvram(ha, pext, mode);
#endif

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

	ret = copy_from_user(kernel_tmp, usr_tmp, transfer_size);
	if (ret) {
		DEBUG9_10(printk(
		    "qla2x00_update_nvram: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n", pext->RequestAdr);)
		pext->Status = EXT_STATUS_COPY_ERR;
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

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
	if (ha->device_id == QLA2312_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) { 	    
		data = RD_REG_WORD(&reg->ctrl_status);
		if ((data >> 14) == 1)
			base = 0x80;
		else
			base = 0;

		wait_cnt = 20000;
		data = RD_REG_WORD(&reg->nvram);
		while (data & NV_BUSY) {
			wait_cnt--;
			if(wait_cnt == 0) { 
				DEBUG9_10(printk("%s(%ld) NVRAM Busy\n",
						__func__,ha->host_no);)
				return ret;
			}
			UDELAY(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0x1);
		UDELAY(5);

		wait_cnt = 20000;
		data = RD_REG_WORD(&reg->host_semaphore);
		while ((data & BIT_0) == 0) {
			wait_cnt--;
			if(wait_cnt == 0) { 
				DEBUG9_10(printk(
				    "%s(%ld) Unable to set sem_lock\n",
				    __func__,ha->host_no);)
				return ret;
			}
			/* Lock failed */
			UDELAY(100);
			WRT_REG_WORD(&reg->host_semaphore, 0x1);
			UDELAY(5);
			data = RD_REG_WORD(&reg->host_semaphore);
		}
	} else {
		base = 0;
	}

	/* Clear write protection, if necessary. */
	wprot_old = cpu_to_le16(qla2x00_get_nvram_word(ha, base));
	stat = qla2x00_write_nvram_word(ha, base, 
					__constant_cpu_to_le16(0x1234));
	wprot = cpu_to_le16(qla2x00_get_nvram_word(ha, base));
	if (stat != QLA2X00_SUCCESS || wprot != 
				__constant_cpu_to_le16(0x1234)) {
		/* write enable */
		qla2x00_nv_write(ha, NV_DATA_OUT);
		qla2x00_nv_write(ha, 0);
		qla2x00_nv_write(ha, 0);

		for (word = 0; word < 8; word++) {
			qla2x00_nv_write(ha, NV_DATA_OUT);
		}

		qla2x00_nv_deselect(ha);

		/* enable protection register */
		qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
		qla2x00_nv_write(ha, NV_PR_ENABLE);
		qla2x00_nv_write(ha, NV_PR_ENABLE);

		for (word = 0; word < 8; word++) {
			qla2x00_nv_write(ha, NV_DATA_OUT | NV_PR_ENABLE);
		}

		qla2x00_nv_deselect(ha);

		/* clear protection register (ffff is cleared) */
		qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
		qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);
		qla2x00_nv_write(ha, NV_PR_ENABLE | NV_DATA_OUT);

		for (word = 0; word < 8; word++) {
			qla2x00_nv_write(ha, NV_DATA_OUT | NV_PR_ENABLE);
		}
		qla2x00_nv_deselect(ha);

		/* Wait for NVRAM to become ready */
		wait_cnt = 20000;
		WRT_REG_WORD(&reg->nvram, NV_SELECT);
		PCI_POSTING(&reg->nvram);
		do {
			wait_cnt--;
			if(wait_cnt == 0) { 
				DEBUG9_10(printk(
				    "%s(%ld) Wait for NVRAM Ready Over\n",
				    __func__,ha->host_no);)
				return ret;
			}
			NVRAM_DELAY();
			word = RD_REG_WORD(&reg->nvram);
		} while ((word & NV_DATA_IN) == 0);
	} else
		qla2x00_write_nvram_word(ha, base, wprot_old);
#else
	base = 0;
#endif

	wptr = (uint16_t *)pnew_nv;
	for (cnt = 0; cnt < transfer_size / 2; cnt++) {
		data = cpu_to_le16(*wptr++);
		qla2x00_write_nvram_word(ha, (cnt+base), data);
	}
	/* Enable NVRAM write-protection if cleared. */
	qla2x00_set_nvram_protection(ha, stat);

#if defined(ISP2300)
	if (ha->device_id == QLA2312_DEVICE_ID ||
	    ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6312_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) { 	    
		/* Unlock resource */
		WRT_REG_WORD(&reg->host_semaphore, 0);
		PCI_POSTING(&reg->host_semaphore);
	}
#endif

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("qla2x00_update_nvram: exiting.\n");)

	/* Schedule DPC to restart the RISC */
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	up(ha->dpc_wait);

	ret = qla2x00_wait_for_hba_online(ha);
	return ret;
}

int
qla2x00_write_nvram_word(scsi_qla_host_t *ha, uint8_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	device_reg_t *reg = ha->iobase;
	uint32_t	wait_cnt;

	DEBUG9(printk("%s entered\n",__func__);)

	qla2x00_nv_write(ha, NV_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NV_DATA_OUT);

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
	wait_cnt = 20000;
	WRT_REG_WORD(&reg->nvram, NV_SELECT);
	PCI_POSTING(&reg->nvram);
	do {
		wait_cnt--;
		if(wait_cnt == 0) { 
			DEBUG9_10(printk("%s(%ld) Wait for NVRAM Ready Over\n",
					__func__,ha->host_no);)
			return 1;
		}
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
		return 0;
	}

	if (pext->ResponseLen != sizeof(INT_LOOPBACK_RSP)) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid ResponseLen =%d.\n",
		    pext->ResponseLen);)
		return 0;
	}

	status = copy_from_user(&req, pext->RequestAdr, pext->RequestLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy read of "
		    "request buffer.\n");)
		return (-EFAULT);
	}

	status = copy_from_user(&rsp, pext->ResponseAdr, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify read of "
		    "response buffer.\n");)
		return (-EFAULT);
	}

	if (req.TransferCount > req.BufferLength ||
	    req.TransferCount > rsp.BufferLength) {

		/* Buffer lengths not large enough. */
		pext->Status = EXT_STATUS_INVALID_PARAM;

		DEBUG9_10(printk(
		    "qla2x00_send_loopback: invalid TransferCount =%d. "
		    "req BufferLength =%d rspBufferLength =%d.\n",
		    req.TransferCount, req.BufferLength, rsp.BufferLength);)

		return 0;
	}

	status = copy_from_user(ha->ioctl_mem, req.BufferAddress,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy read of "
		    "user loopback data buffer.\n");)
		return (-EFAULT);
	}

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
		return 0;
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

	DEBUG9(printk("qla2x00_send_loopback: loopback mbx cmd ok. "
	    "copying data.\n");)

	/* put loopback return data in user buffer */
	status = copy_to_user(rsp.BufferAddress, ha->ioctl_mem,
	    req.TransferCount);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR verify "
		    "write of return data buffer.\n");)
		return (-EFAULT);
	}

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

	status = copy_to_user(pext->ResponseAdr, &rsp, pext->ResponseLen);
	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("qla2x00_send_loopback: ERROR copy "
		    "write of response buffer.\n");)
		return (-EFAULT);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("qla2x00_send_loopback: exiting.\n");)

	return 0;
}

int
qla2x00_read_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int	rval = 0;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (pext->SubCode)
		return qla2x00_read_option_rom_ext(ha, pext, mode);

	/* These interfaces are not valid for 24xx and 25xx chips. */
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		pext->Status = EXT_STATUS_INVALID_REQUEST;
		return (rval);
        }

	if (pext->ResponseLen < FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		return (rval);
	}

	/* Dump FLASH. */
 	qla2x00_update_or_read_flash(ha, (uint8_t *)pext->ResponseAdr, 0, 
	    FLASH_IMAGE_SIZE, QLA2X00_READ); 

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return (rval);
}

int
qla2x00_read_option_rom_ext(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		iter, found;
	int		rval = 0;
	uint8_t		*image_ptr;
	uint32_t	saddr, length;


	DEBUG9(printk("%s: entered.\n", __func__);)

	found = 0;
	saddr = length = 0;

	/* Retrieve region or raw starting address. */
	if (pext->SubCode == 0xFFFF) {
		saddr = pext->Reserved1;
		length = pext->RequestLen;
		found++;
	} else {
		INT_OPT_ROM_REGION *OptionRomTable = NULL;
		unsigned long  OptionRomTableSize;

		/* Pick the right OptionRom table based on device id */
		qla2x00_get_option_rom_table(ha, &OptionRomTable, 
		    &OptionRomTableSize);
		for (iter = 0; OptionRomTable != NULL && iter <
		    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION));
		    iter++) {
			if (OptionRomTable[iter].Region == pext->SubCode) {
				saddr = OptionRomTable[iter].Beg;
				length = OptionRomTable[iter].Size;
				found++;
				break;
			}
		}
	}
	if (!found) {
		pext->Status = EXT_STATUS_ERR;
		return (rval);
	}
	if (pext->ResponseLen < length) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		return (rval);
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		image_ptr = vmalloc(length);
		if (image_ptr == NULL) {
			pext->Status = EXT_STATUS_NO_MEMORY;
			printk(KERN_WARNING
			    "%s: ERROR in flash allocation.\n", __func__);
			return (-ENOMEM);
		}
	} else {
		image_ptr = (uint8_t *)pext->ResponseAdr;
	}

	/* Dump FLASH. */
 	qla2x00_update_or_read_flash(ha, image_ptr, saddr, length,
	    QLA2X00_READ); 

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		rval = copy_to_user(pext->ResponseAdr, image_ptr, length);
		if (rval) {
			pext->Status = EXT_STATUS_COPY_ERR;
			DEBUG9_10(printk(
			"%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
			__func__, ha->host_no, ha->instance));
			vfree(image_ptr);
			return (-EFAULT);
		}

		vfree(image_ptr);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return rval;
}

int
qla2x00_update_option_rom(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret = 0;
	uint8_t		*usr_tmp;
	uint8_t		*kern_tmp;
	uint16_t	status;

	DEBUG9(printk("%s: entered.\n", __func__);)

	if (pext->SubCode)
		return qla2x00_update_option_rom_ext(ha, pext, mode);

	/* These interfaces are not valid for 24xx and 25xx chips. */
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                pext->Status = EXT_STATUS_INVALID_REQUEST;
                return (ret);
        }

	if (pext->RequestLen != FLASH_IMAGE_SIZE) {
		pext->Status = EXT_STATUS_INVALID_PARAM;
		return (ret);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	/* Read from user buffer */
	usr_tmp = (uint8_t *)pext->RequestAdr;

	kern_tmp = (uint8_t *)vmalloc(FLASH_IMAGE_SIZE);
	if (kern_tmp == NULL) {
		pext->Status = EXT_STATUS_NO_MEMORY;
		printk(KERN_WARNING
		    "%s: ERROR in flash allocation.\n", __func__);
		return (-ENOMEM);
	}

	ret = copy_from_user(kern_tmp, usr_tmp, FLASH_IMAGE_SIZE);
	if (ret) {
		KMEM_FREE(kern_tmp, FLASH_IMAGE_SIZE);
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n",
		    __func__, pext->RequestAdr);)
		vfree(kern_tmp);
		return (-EFAULT);
	}

	status = qla2x00_update_or_read_flash(ha, kern_tmp, 0, 
	    FLASH_IMAGE_SIZE, QLA2X00_WRITE); 

	vfree(kern_tmp);

	if (status) {
		pext->Status = EXT_STATUS_COPY_ERR;
		ret = (-EFAULT);
		DEBUG9_10(printk("%s: ERROR updating flash.\n", __func__);)
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)

	return (ret);
}

int
qla2x00_update_option_rom_ext(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		iter, found;
	int		ret;
	uint8_t		*usr_tmp;
	uint8_t		*kern_tmp;
	uint16_t	status;
	uint32_t	saddr, length;

	DEBUG9(printk("%s: entered.\n", __func__);)

	found = 0;
	saddr = length = 0;
	/* Retrieve region or raw starting address. */
	if (pext->SubCode == 0xFFFF) {
		saddr = pext->Reserved1;
		length = pext->RequestLen;
		found++;
	} else {
		INT_OPT_ROM_REGION *OptionRomTable = NULL;
		unsigned long  OptionRomTableSize;

		/* Pick the right OptionRom table based on device id */
		qla2x00_get_option_rom_table(ha, &OptionRomTable, 
		    &OptionRomTableSize);
		for (iter = 0; OptionRomTable != NULL && iter <
		    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION));
		    iter++) {
			if (OptionRomTable[iter].Region == pext->SubCode) {
				saddr = OptionRomTable[iter].Beg;
				length = OptionRomTable[iter].Size;
				found++;
				break;
			}
		}
	}

	if (!found) {
		pext->Status = EXT_STATUS_ERR;
		return 0;
	}

	if (pext->RequestLen < length) {
		pext->Status = EXT_STATUS_COPY_ERR;
		return (-EFAULT);
	}

	pext->Status = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	/* Read from user buffer */
	usr_tmp = (uint8_t *)pext->RequestAdr;

	kern_tmp = (uint8_t *)vmalloc(length);
	if (kern_tmp == NULL) {
		pext->Status = EXT_STATUS_NO_MEMORY;
		printk(KERN_WARNING
		    "%s: ERROR in flash allocation.\n", __func__);
		return (-ENOMEM);
	}

	ret = copy_from_user(kern_tmp, usr_tmp, length);
	if (ret) {
		vfree(kern_tmp);
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR in buffer copy READ. "
		    "RequestAdr=%p\n", __func__, pext->RequestAdr));
		return (-EFAULT);
	}

	/* Go with update */
	status = qla2x00_update_or_read_flash(ha, kern_tmp, saddr, 
	    length, QLA2X00_WRITE);

	vfree(kern_tmp);

	if (status) {
		ret = -EFAULT;
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s: ERROR updating flash.\n", __func__);)
#if defined(ISP2300)
	} else {
		uint8_t		*ptmp_mem = NULL;

		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			if (qla2x00_get_ioctl_scrap_mem(ha,
			    (void **)&ptmp_mem, PAGE_SIZE)) {
				/* not enough memory */
				pext->Status = EXT_STATUS_NO_MEMORY;
				DEBUG9_10(printk("%s(%ld): inst=%ld scrap not "
				    "big enough. size requested=%ld.\n",
				    __func__, ha->host_no,
				    ha->instance, PAGE_SIZE));
			} else if (qla24xx_refresh_flash_version(ha, ptmp_mem)){
				pext->Status = EXT_STATUS_ERR;
				DEBUG9_10(printk( "%s: ERROR reading updated "
				    "flash versions.\n",
				    __func__);)
			}

			qla2x00_free_ioctl_scrap_mem(ha);
		}
#endif
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)
	return (ret);
}

int
qla2x00_get_option_rom_layout(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int			ret, iter;
	INT_OPT_ROM_REGION	*OptionRomTable = NULL;
	INT_OPT_ROM_LAYOUT	*optrom_layout;	
	unsigned long		OptionRomTableSize; 

	DEBUG9(printk("%s: entered.\n", __func__);)

	/* Pick the right OptionRom table based on device id */
	qla2x00_get_option_rom_table(ha, &OptionRomTable, &OptionRomTableSize);

	if (OptionRomTable == NULL) {
		pext->Status = EXT_STATUS_DEV_NOT_FOUND;
		DEBUG9_10(printk("%s(%ld) Option Rom Table for device_id=0x%x "
		    "not defined\n", __func__,ha->host_no,ha->device_id));
		return 0;
	}

	if (pext->ResponseLen < OptionRomTableSize) {
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		DEBUG9_10(printk("%s(%ld) buffer too small: response_len = %d "
		    "optrom_table_len=%ld.\n", __func__, ha->host_no,
		    pext->ResponseLen,OptionRomTableSize));
		return 0;
	}
	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&optrom_layout,
	    OptionRomTableSize)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%ld.\n", __func__, ha->host_no,
		    ha->instance, OptionRomTableSize));
		return 0;
	}

	// Dont Count the NULL Entry.
	optrom_layout->NoOfRegions =
	    (OptionRomTableSize / sizeof(INT_OPT_ROM_REGION) - 1);

	for (iter = 0; iter < optrom_layout->NoOfRegions; iter++) {
		optrom_layout->Region[iter].Region =
		    OptionRomTable[iter].Region;
		optrom_layout->Region[iter].Size =
		    OptionRomTable[iter].Size;

		if (OptionRomTable[iter].Region == INT_OPT_ROM_REGION_ALL)
			optrom_layout->Size = OptionRomTable[iter].Size;
	}

	ret = copy_to_user(pext->ResponseAdr, optrom_layout,
	    OptionRomTableSize);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance));
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("%s: exiting.\n", __func__));
	return ret;
}


/*
* qla2x00_get_option_rom_table
* 	This function returns the OptionRom Table for matching device id.
*/
static void
qla2x00_get_option_rom_table(scsi_qla_host_t *ha,
    INT_OPT_ROM_REGION **pOptionRomTable, unsigned long  *OptionRomTableSize)
{
	DEBUG9(printk("%s: entered.\n", __func__));

	switch (ha->device_id) {
	case QLA6312_DEVICE_ID:
		*pOptionRomTable = OptionRomTable6312;
		*OptionRomTableSize = sizeof(OptionRomTable6312);
		break;
	case QLA2312_DEVICE_ID:
		/* HBA Model 6826A - is 2312 V3 Chip */
		if (ha->subsystem_vendor == 0x103C &&
		    ha->subsystem_device == 0x12BA){
			*pOptionRomTable = OptionRomTableHp;
			*OptionRomTableSize = sizeof(OptionRomTableHp);
		} else {
			*pOptionRomTable = OptionRomTable2312;
			*OptionRomTableSize = sizeof(OptionRomTable2312);
		}
		break;
	case QLA2322_DEVICE_ID:
		*pOptionRomTable = OptionRomTable2322;
		*OptionRomTableSize = sizeof(OptionRomTable2322);
		break;
	case QLA6322_DEVICE_ID:
		*pOptionRomTable = OptionRomTable6322;
		*OptionRomTableSize = sizeof(OptionRomTable6322);
		break;
	case QLA2422_DEVICE_ID:
	case QLA2432_DEVICE_ID:
	case QLA5422_DEVICE_ID:
	case QLA5432_DEVICE_ID:
		*pOptionRomTable = OptionRomTable2422;
		*OptionRomTableSize = sizeof(OptionRomTable2422);
		break;
	default:
		DEBUG9_10(printk("%s(%ld) Option Rom Table for device_id=0x%x "
		    "not defined\n", __func__, ha->host_no,ha->device_id));
		break;
	}

	DEBUG9(printk("%s: exiting.\n", __func__);)
}

#if !defined(ISP2100) && !defined(ISP2200)

#if 0
void
qla24xx_read_nvram_data(scsi_qla_host_t *ha, uint8_t *pbuf, uint32_t naddr,
    uint32_t size)
{
	uint32_t	i;
	uint32_t	*dwptr;

	/* Dword reads to flash. */
	dwptr = (uint32_t *)pbuf;
	for (i = 0; i < size >> 2; i++, naddr++) {
		 dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    nvram_data_to_access_addr(naddr)));
	}
}
#endif

int
qla24xx_write_nvram_data(scsi_qla_host_t *ha, uint8_t *pnew_nv, uint32_t naddr,
    uint32_t transfer_size)
{
	int			ret = 0;
	uint32_t		i;
	uint32_t		*dwptr;
	struct device_reg_24xx	*reg = (struct device_reg_24xx *)ha->iobase;

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101), 0);
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101), 0);

	/* Dword writes to flash. */
	dwptr = (uint32_t *)pnew_nv;
	for (i = 0; i < transfer_size >> 2; i++, naddr++, dwptr++) {

		if (qla24xx_write_flash_dword(ha,
		    nvram_data_to_access_addr(naddr), *dwptr) != 
		    QLA2X00_SUCCESS) {
			DEBUG9(printk("%s(%ld) Unable to program "
			    "nvram address=%x data=%x.\n", __func__,
			    ha->host_no, naddr, *dwptr));
			ret = QLA2X00_FUNCTION_FAILED;
			break;
		}
	}

	/* Enable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101), 0x8c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return (ret);
}

int
qla2x00_get_vpd(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret = 0;
	uint8_t		*ptmp_buf;
	uint32_t	data_offset;
	uint32_t	tmp_data_offset;
	uint32_t	transfer_size;
	unsigned long	flags;


	if (!check_24xx_or_54xx_device_ids(ha) && !check_25xx_device_ids(ha)) {
		pext->Status = EXT_STATUS_INVALID_REQUEST;
		DEBUG9_10(printk(
		    "%s(%ld): inst=%ld not 24xx or 25xx. exiting.\n",
		    __func__, ha->host_no, ha->instance));
		return (ret);
	}

	DEBUG9(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	transfer_size = FA_NVRAM_VPD_SIZE * 4; /* byte count */
	if (pext->ResponseLen < transfer_size) {
		pext->ResponseLen = transfer_size;
		pext->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		DEBUG9_10(printk(
		    "%s(%ld): inst=%ld Response buffer too small.\n",
		    __func__, ha->host_no, ha->instance));
		return (ret);
	}

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&ptmp_buf,
	    transfer_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance,
		    ha->nvram_size);)
		return (ret);
	}

	if (PCI_FUNC(ha->pdev->devfn))
		data_offset = FA_NVRAM_VPD1_ADDR;
	else
		data_offset = FA_NVRAM_VPD0_ADDR;

	tmp_data_offset = data_offset;

	/* Dump VPD region in NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla24xx_read_nvram_data(ha, (uint32_t *)ptmp_buf, data_offset, transfer_size);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG2(printk(
	    "%s(%ld): using data_offset=0x%x transfer_size=0x%x.\n",
	    __func__, ha->host_no, data_offset, transfer_size);)

	printk("BUFFER in Get VPD offset: 0x%x\n", data_offset);
	qla2x00_dump_buffer((uint8_t *)ptmp_buf, transfer_size);

	DEBUG2(qla2x00_dump_buffer((uint8_t *)ptmp_buf, transfer_size);)
	ret = copy_to_user((void *)(pext->ResponseAdr), ptmp_buf,
	    transfer_size);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk("%s(%ld): inst=%ld ERROR copy rsp buffer.\n",
		    __func__, ha->host_no, ha->instance);)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("%s(%ld): exiting.\n", __func__, ha->host_no);)

	return (ret);
}

int
qla2x00_update_vpd(scsi_qla_host_t *ha, EXT_IOCTL *pext, int mode)
{
	int		ret = 0;
	uint8_t		*usr_tmp, *kernel_tmp, *pnew_nv;
	uint32_t	data_offset;
	uint32_t	transfer_size;
	unsigned long	flags;


	if (!check_24xx_or_54xx_device_ids(ha) && !check_25xx_device_ids(ha)) {
		pext->Status = EXT_STATUS_INVALID_REQUEST;
		DEBUG9_10(printk(
		    "%s(%ld): inst=%ld not 24xx or 25xx. exiting.\n",
		    __func__, ha->host_no, ha->instance));
		return (ret);
	}

	DEBUG9(printk("%s(%ld): entered DEBUG.\n", __func__, ha->host_no);)

	transfer_size = FA_NVRAM_VPD_SIZE * 4; /* byte count */
	if (pext->RequestLen < transfer_size)
		transfer_size = pext->RequestLen;

	if (qla2x00_get_ioctl_scrap_mem(ha, (void **)&pnew_nv, transfer_size)) {
		/* not enough memory */
		pext->Status = EXT_STATUS_NO_MEMORY;
		DEBUG9_10(printk("%s(%ld): inst=%ld scrap not big enough. "
		    "size requested=%d.\n",
		    __func__, ha->host_no, ha->instance, transfer_size));
		return (ret);
	}

	/* Read from user buffer */
	kernel_tmp = (uint8_t *)pnew_nv;
	usr_tmp = (void *)(pext->RequestAdr);

	ret = copy_from_user(kernel_tmp, usr_tmp, transfer_size);
	if (ret) {
		pext->Status = EXT_STATUS_COPY_ERR;
		DEBUG9_10(printk(
		    "%s(%ld): ERROR in buffer copy READ. RequestAdr=%p\n",
		    __func__, ha->host_no, (void *)(pext->RequestAdr));)
		qla2x00_free_ioctl_scrap_mem(ha);
		return (-EFAULT);
	}

	if (PCI_FUNC(ha->pdev->devfn))
		data_offset = FA_NVRAM_VPD1_ADDR;
	else
		data_offset = FA_NVRAM_VPD0_ADDR;


	qla2x00_dump_buffer((uint8_t *)pnew_nv, transfer_size);

	DEBUG9(printk(
	    "%s(%ld): using data_offset=0x%x transfer_size=0x%x.\n",
	    __func__, ha->host_no, data_offset, transfer_size);)


	DEBUG2(qla2x00_dump_buffer((uint8_t *)pnew_nv, transfer_size);)

	/* Write NVRAM. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla24xx_write_nvram_data(ha, pnew_nv, data_offset, transfer_size);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	pext->Status       = EXT_STATUS_OK;
	pext->DetailStatus = EXT_STATUS_OK;

	qla2x00_free_ioctl_scrap_mem(ha);

	DEBUG9(printk("%s(%ld): exiting.\n", __func__, ha->host_no);)

	/* No need to reset the 24xx. */
	return ret;
}
#endif

