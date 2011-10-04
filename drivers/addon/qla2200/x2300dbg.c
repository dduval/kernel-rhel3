/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.4.x
 * Copyright (C) 2003 QLogic Corporation
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
void
qla2300_dump_isp(scsi_qla_host_t *ha)
{
	int rval;
	uint32_t     i;
	uint32_t     cnt, timer;
	uint32_t     risc_address;
	uint16_t     risc_code_size;
	uint16_t     mb0, mb2;

	uint16_t	data, stat;
	device_reg_t	*reg;
	uint16_t	*dmp_reg;
	unsigned long flags;


	reg = ha->iobase;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	printk("\n\n[==>BEG]\n");

	printk("HCCR Register:\n");
	printk("%04x\n\n", RD_REG_WORD(&reg->host_cmd));

	WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC); 
	SYS_DELAY(100);

	printk("PBIU Registers:\n");
	dmp_reg = (uint16_t *)(reg + 0);
	for (i = 0; i < 8; i++) 
		printk("%04x ", RD_REG_WORD(dmp_reg++));

	printk("\n\n");

	printk("ReqQ-RspQ-Risc2Host Status registers:\n");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x10);
	for (i = 0; i < 8; i++) 
		printk("%04x ", RD_REG_WORD(dmp_reg++));

	printk("\n\n");

	printk("Mailbox Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x40);
	for (i = 0; i < 32; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x40);
	printk("Auto Request Response DMA Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 32; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x50); 
	printk("DMA Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 48; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x00); 
	printk("RISC Hardware Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0xA0);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2000); 
	printk("RISC GP0 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2200); 
	printk("RISC GP1 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2400); 
	printk("RISC GP2 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2600); 
	printk("RISC GP3 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2800); 
	printk("RISC GP4 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2A00); 
	printk("RISC GP5 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2C00); 
	printk("RISC GP6 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->pcr, 0x2E00); 
	printk("RISC GP7 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x10); 
	printk("Frame Buffer Hardware Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 64; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x20); 
	printk("FPM B0 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 64; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, 0x30); 
	printk("FPM B1 Registers:");
	dmp_reg = (uint16_t *)((uint8_t *)reg + 0x80);
	for (i = 0; i < 64; i++) {
		if ((i % 8) == 0)
			printk("\n");
		printk("%04x ", RD_REG_WORD(dmp_reg++));
	}

	printk("\n\n");

	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);

	data = RD_REG_WORD(&reg->ctrl_status);
	for (i = 6000000; i && data & CSR_ISP_SOFT_RESET; i--) {
		SYS_DELAY(5);
		data = RD_REG_WORD(&reg->ctrl_status);
	}
	if (
	    #if defined(ISP200)
	    (ha->device_id == QLA6312_DEVICE_ID ||
	     ha->device_id == QLA6322_DEVICE_ID)  
	    #else	
	    (ha->device_id == QLA2312_DEVICE_ID ||
	     ha->device_id == QLA2322_DEVICE_ID)
	    #endif	    
	   ) { 	    
		SYS_DELAY(10);
	} else {
		data = RD_REG_WORD(&reg->mailbox0);
		for (i = 6000000; i && data == MBS_BUSY; i--) {
			SYS_DELAY(5);
			data = RD_REG_WORD(&reg->mailbox0);
		}
	}

	rval = QLA2X00_SUCCESS;
	mb0 = mb2 = 0;
	printk("Code RAM Dump:");
	risc_address      = 0x800;
	risc_code_size    = 0xffff - 0x800 + 1;
	WRT_REG_WORD(&reg->mailbox0, MBC_READ_RAM_WORD);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	for (cnt = 0; cnt < risc_code_size && rval == QLA2X00_SUCCESS; cnt++) {
		if ((cnt % 8) == 0)
			printk("\n%04x: ", cnt + 0x0800);
		WRT_REG_WORD(&reg->mailbox1, (uint16_t)risc_address++);
		WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);

		for (timer = 6000000; timer != 0; timer--) {
			/* Check for pending interrupts. */
			if ((stat = RD_REG_WORD(&reg->host_status_lo)) & HOST_STATUS_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					/* Release mailbox registers. */
					WRT_REG_WORD(&reg->semaphore, 0);
					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				} else if (stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				}

				/* clear this intr; it wasn't a mailbox intr */
				WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			}

			mdelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			printk("%04x ", mb2);
		} else {
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	printk("\n\n");

	mb0 = mb2 = 0;
	printk("Stack RAM Dump:");
	risc_address      = 0x10000;
	risc_code_size    = 0x107ff - 0x10000 + 1;
	WRT_REG_WORD(&reg->mailbox0, 0xf /* MBC_READ_RAM_EXTENDED */);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	for (cnt = 0; cnt < risc_code_size && rval == QLA2X00_SUCCESS; cnt++) {
		if ((cnt % 8) == 0)
			printk("\n%05x: ", cnt + 0x10000);
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address++));
		WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);

		for (timer = 6000000; timer != 0; timer--) {
			/* Check for pending interrupts. */
			if ((stat = RD_REG_WORD(&reg->host_status_lo)) & HOST_STATUS_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					/* Release mailbox registers. */
					WRT_REG_WORD(&reg->semaphore, 0);
					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				} else if (stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				}

				/* clear this intr; it wasn't a mailbox intr */
				WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			}

			mdelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			printk("%04x ", mb2);
		} else {
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	printk("\n\n");

	mb0 = mb2 = 0;
	printk("Data RAM Dump:");
	risc_address      = 0x10800;
	risc_code_size    = 0x1ffff - 0x10800 + 1;
	WRT_REG_WORD(&reg->mailbox0, 0xf /* MBC_READ_RAM_EXTENDED */);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	for (cnt = 0; cnt < risc_code_size && rval == QLA2X00_SUCCESS; cnt++) {
		if ((cnt % 8) == 0)
			printk("\n%05x: ", cnt + 0x10800);
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address++));
		WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);

		for (timer = 6000000; timer != 0; timer--) {
			/* Check for pending interrupts. */
			if ((stat = RD_REG_WORD(&reg->host_status_lo)) & HOST_STATUS_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					/* Release mailbox registers. */
					WRT_REG_WORD(&reg->semaphore, 0);
					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				} else if (stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

					mb0 = RD_REG_WORD(&reg->mailbox0);
					mb2 = RD_REG_WORD(&reg->mailbox2);

					WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
					break;
				}

				/* clear this intr; it wasn't a mailbox intr */
				WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			}

			mdelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			printk("%04x ", mb2);
		} else {
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	printk("\n[<==END] ISP DEBUG DUMP\n");

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static int
qla_uprintf(char **uiter, char *fmt, ...)
{
	int	iter, len;
	char	buf[128];
	va_list	args;
 
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	for (iter = 0; iter < len; iter++, *uiter += 1)
		*uiter[0] = buf[iter];

	return (len);
}


void
qla24xx_fw_dump(scsi_qla_host_t *ha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt, timer;
	uint32_t	risc_address;
	uint16_t	mb[4];

	uint32_t	stat;
	struct device_reg_24xx *reg;
	uint32_t   *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t   *mbx_reg;
	unsigned long	flags;
	struct qla24xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;

	reg = (struct device_reg_24xx *)ha->iobase;
	risc_address = ext_mem_cnt = 0;
	memset(mb, 0, sizeof(mb));
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump24) {
		printk(KERN_WARNING
		    "No buffer available for dump!!!\n");
		goto qla24xx_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		printk(KERN_WARNING
		    "%s(%ld)Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", __func__, ha->host_no, ha->fw_dump24);
		goto qla24xx_fw_dump_failed;
	}
	fw = (struct qla24xx_fw_dump *) ha->fw_dump24;

	rval = QLA2X00_SUCCESS;
	fw->host_status = RD_REG_DWORD(&reg->host_status);

	/* Pause RISC. */
	if ((RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0) {

		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET |
		    HCCRX_CLR_HOST_INT);
		RD_REG_DWORD(&reg->hccr);		/* PCI Posting. */
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_PAUSE);
		for (cnt = 30000;
		    (RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0 &&
		    rval == QLA2X00_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QL_STATUS_TIMEOUT;
		}
	}

	if (rval == QLA2X00_SUCCESS) {
		/* Host interface registers. */
		dmp_reg = (uint32_t *)(reg + 0);
		for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++)
			fw->host_reg[cnt] = RD_REG_DWORD(dmp_reg++);
		/* Disable interrupts. */
		WRT_REG_DWORD(&reg->ictrl, 0);
		RD_REG_DWORD(&reg->ictrl);

		/* Shadow registers. */
		WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
		RD_REG_DWORD(&reg->iobase_addr);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0000000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[0] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0100000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[1] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0200000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[2] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0300000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[3] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0400000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[4] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0500000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[5] = RD_REG_DWORD(dmp_reg);

		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xF0);
		WRT_REG_DWORD(dmp_reg, 0xB0600000);
		dmp_reg = (uint32_t __iomem *)((uint8_t __iomem *)reg + 0xFC);
		fw->shadow_reg[6] = RD_REG_DWORD(dmp_reg);

		/* Mailbox registers. */
		mbx_reg = (uint16_t *)((uint8_t *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
			fw->mailbox_reg[cnt] = RD_REG_WORD(mbx_reg++);

		/* Transfer sequence registers. */
		iter_reg = fw->xseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xBF00);
		dmp_reg = (uint32_t *)((uint8_t *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF10);
		dmp_reg = (uint32_t *)((uint8_t *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF20);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF30);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF40);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF50);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF60);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF70);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFE0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->xseq_0_reg) / 4; cnt++)
			fw->xseq_0_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFF0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->xseq_1_reg) / 4; cnt++)
			fw->xseq_1_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		/* Receive sequence registers. */
		iter_reg = fw->rseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xFF00);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF10);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF20);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF30);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF40);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF50);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF60);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF70);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFD0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->rseq_0_reg) / 4; cnt++)
			fw->rseq_0_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFE0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->rseq_1_reg) / 4; cnt++)
			fw->rseq_1_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFF0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->rseq_2_reg) / 4; cnt++)
			fw->rseq_2_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		/* Command DMA registers. */
		WRT_REG_DWORD(&reg->iobase_addr, 0x7100);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->cmd_dma_reg) / 4; cnt++)
			fw->cmd_dma_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		/* Queues. */
		iter_reg = fw->req0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7200);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xE4);
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->resp0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7300);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xE4);
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->req1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7400);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xE4);
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		/* Transmit DMA registers. */
		iter_reg = fw->xmt0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7600);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7610);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->xmt1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7620);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7630);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->xmt2_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7640);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7650);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->xmt3_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7660);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7670);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->xmt4_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7680);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7690);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x76A0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < sizeof(fw->xmt_data_dma_reg) / 4; cnt++)
			fw->xmt_data_dma_reg[cnt] = RD_REG_DWORD(dmp_reg++);

		/* Receive DMA registers. */
		iter_reg = fw->rcvt0_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7700);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7710);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		iter_reg = fw->rcvt1_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7720);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x7730);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		/* RISC registers. */
		iter_reg = fw->risc_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x0F00);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F10);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F20);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F30);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F40);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F50);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F60);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);


		/* Local memory controller registers. */
		iter_reg = fw->lmc_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x3000);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3010);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3020);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3030);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3040);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3050);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x3060);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		/* Fibre Protocol Module registers. */
		iter_reg = fw->fpm_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x4000);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4010);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4020);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4030);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4040);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4050);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4060);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4070);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4080);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x4090);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x40A0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x40B0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		/* Frame Buffer registers. */
		iter_reg = fw->fb_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x6000);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6010);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6020);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6030);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6040);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6100);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6130);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6150);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6170);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x6190);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		WRT_REG_DWORD(&reg->iobase_addr, 0x61B0);
		dmp_reg = (uint32_t   *)((uint8_t   *)reg + 0xC0);
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = RD_REG_DWORD(dmp_reg++);

		/* Reset RISC. */
		WRT_REG_DWORD(&reg->ctrl_status,
		    CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_DWORD(&reg->ctrl_status) &
			    CSRX_DMA_ACTIVE) == 0)
				break;

			udelay(10);
		}

		WRT_REG_DWORD(&reg->ctrl_status,
		    CSRX_ISP_SOFT_RESET|CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
		/*
		 * It is necessary to delay here since the card doesn't respond
		 * to PCI reads during a reset. On some architectures this will
		 * result in an MCA.
		*/
		udelay(100);

		/* Wait for firmware to complete NVRAM accesses. */
		mb[0] = (uint32_t) RD_REG_WORD(&reg->mailbox0);
		for (cnt = 10000 ; cnt && mb[0]; cnt--) {
			udelay(5);
			mb[0] = (uint32_t) RD_REG_WORD(&reg->mailbox0);
			barrier();
		}

		/* Wait for soft-reset to complete. */
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_DWORD(&reg->ctrl_status) &
			    CSRX_ISP_SOFT_RESET) == 0)
				break;

			udelay(10);
		}
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_RESET);
		RD_REG_DWORD(&reg->hccr);             /* PCI Posting. */
	}

	for (cnt = 30000; RD_REG_WORD(&reg->mailbox0) != 0 &&
	    rval == QLA2X00_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QL_STATUS_TIMEOUT;
	}

	/* Memory. */
	if (rval == QLA2X00_SUCCESS) {
		/* Code RAM. */
		risc_address = 0x20000;
		WRT_REG_WORD(&reg->mailbox0, MBC_READ_RAM_EXTENDED);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < sizeof(fw->code_ram) / 4 && rval == QLA2X00_SUCCESS;
	    cnt++, risc_address++) {
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address));
		RD_REG_WORD(&reg->mailbox8);
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_HOST_INT);

		for (timer = 6000000; timer; timer--) {
			/* Check for pending interrupts. */
			stat = RD_REG_DWORD(&reg->host_status);
			if (stat & HSRX_RISC_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2 ||
				    stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb[0] = RD_REG_WORD(&reg->mailbox0);
					mb[2] = RD_REG_WORD(&reg->mailbox2);
					mb[3] = RD_REG_WORD(&reg->mailbox3);

					WRT_REG_DWORD(&reg->hccr,
					    HCCRX_CLR_RISC_INT);
					RD_REG_DWORD(&reg->hccr);
					break;
				}

				/* Clear this intr; it wasn't a mailbox intr */
				WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
				RD_REG_DWORD(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb[0] & MBS_MASK;
			fw->code_ram[cnt] = (mb[3] << 16) | mb[2];
		} else {
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	if (rval == QLA2X00_SUCCESS) {
		/* External Memory. */
		risc_address = 0x100000;
		ext_mem_cnt = ha->fw_memory_size - 0x100000 + 1;
		WRT_REG_WORD(&reg->mailbox0, MBC_READ_RAM_EXTENDED);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < ext_mem_cnt && rval == QLA2X00_SUCCESS;
	    cnt++, risc_address++) {
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address));
		RD_REG_WORD(&reg->mailbox8);
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_HOST_INT);

		for (timer = 6000000; timer; timer--) {
			/* Check for pending interrupts. */
			stat = RD_REG_DWORD(&reg->host_status);
			if (stat & HSRX_RISC_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2 ||
				    stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb[0] = RD_REG_WORD(&reg->mailbox0);
					mb[2] = RD_REG_WORD(&reg->mailbox2);
					mb[3] = RD_REG_WORD(&reg->mailbox3);

					WRT_REG_DWORD(&reg->hccr,
					    HCCRX_CLR_RISC_INT);
					RD_REG_DWORD(&reg->hccr);
					break;
				}

				/* Clear this intr; it wasn't a mailbox intr */
				WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
				RD_REG_DWORD(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb[0] & MBS_MASK;
			fw->ext_mem[cnt] = (mb[3] << 16) | mb[2];
		} else {
			rval = QLA2X00_FUNCTION_FAILED;
		}
	}

	if (rval != QLA2X00_SUCCESS) {
		printk(KERN_WARNING
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		printk(KERN_INFO 
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump24);
		ha->fw_dumped = 1;
	}

qla24xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

#if 0
void
qla24xx_ascii_fw_dump(scsi_qla_host_t *ha)
{
	uint32_t cnt;
	char *uiter;
	struct qla24xx_fw_dump *fw;
	uint32_t ext_mem_cnt;

	uiter = ha->fw_dump_buffer;
	fw = ha->fw_dump24;
	qla_uprintf(&uiter, "ISP FW Version %d.%02d.%02d Attributes %04x\n",
	    ha->fw_major_version, ha->fw_minor_version,
	    ha->fw_subminor_version, ha->fw_attributes);

	qla_uprintf(&uiter, "\nR2H Status Register\n%04x\n", fw->host_status);


	qla_uprintf(&uiter, "\nHost Interface Registers");
	for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->host_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nShadow Registers");
	for (cnt = 0; cnt < sizeof(fw->shadow_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->shadow_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nMailbox Registers");
	for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->mailbox_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXSEQ GP Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xseq_gp_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXSEQ-0 Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_0_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xseq_0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXSEQ-1 Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_1_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xseq_1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRSEQ GP Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rseq_gp_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRSEQ-0 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_0_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rseq_0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRSEQ-1 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_1_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rseq_1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRSEQ-2 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_2_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rseq_2_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nCommand DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->cmd_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->cmd_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRequest0 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->req0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->req0_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nResponse0 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->resp0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->resp0_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRequest1 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->req1_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->req1_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT0 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt0_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT1 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt1_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt1_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT2 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt2_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt2_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT3 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt3_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt3_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT4 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt4_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt4_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nXMT Data DMA Common Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->xmt_data_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRCV Thread 0 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->rcvt0_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rcvt0_data_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRCV Thread 1 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->rcvt1_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->rcvt1_data_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP Registers");
	for (cnt = 0; cnt < sizeof(fw->risc_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->risc_gp_reg[cnt]);
	}


	qla_uprintf(&uiter, "\n\nLMC Registers");
	for (cnt = 0; cnt < sizeof(fw->lmc_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->lmc_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFPM Hardware Registers");
	for (cnt = 0; cnt < sizeof(fw->fpm_hdw_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->fpm_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFB Hardware Registers");
	for (cnt = 0; cnt < sizeof(fw->fb_hdw_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			qla_uprintf(&uiter, "\n");

		qla_uprintf(&uiter, "%08x ", fw->fb_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nCode RAM");
	for (cnt = 0; cnt < sizeof (fw->code_ram) / 4; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%08x: ", cnt + 0x20000);
		}
		qla_uprintf(&uiter, "%08x ", fw->code_ram[cnt]);
	}

	qla_uprintf(&uiter, "\n\nExternal Memory");
	ext_mem_cnt = ha->fw_memory_size - 0x100000 + 1;
	for (cnt = 0; cnt < ext_mem_cnt; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%08x: ", cnt + 0x100000);
		}
		qla_uprintf(&uiter, "%08x ", fw->ext_mem[cnt]);
	}

	qla_uprintf(&uiter, "\n[<==END] ISP Debug Dump");
}
#endif

void
qla24xx_console_fw_dump(scsi_qla_host_t *ha)
{
	uint32_t cnt;
#if 0
	char *uiter = ha->fw_dump_buffer;
#endif
	struct qla24xx_fw_dump *fw;
	uint32_t ext_mem_cnt;

	if (!ha->fw_dump24) {
		printk(KERN_WARNING
		    "%s No buffer available for dump!!!\n", __func__);
		return;
	}
	fw = ha->fw_dump24;
#if 0
	printk("ISP FW Version %d.%02d.%02d Attributes %04x\n",
	    ha->fw_major_version, ha->fw_minor_version,
	    ha->fw_subminor_version, ha->fw_attributes);
#endif

	printk("\nR2H Status Register\n%04x\n", fw->host_status);


	printk("\nHost Interface Registers");
	for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->host_reg[cnt]);
	}

	printk("\n\nShadow Registers");
	for (cnt = 0; cnt < sizeof(fw->shadow_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->shadow_reg[cnt]);
	}

	printk("\n\nMailbox Registers");
	for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->mailbox_reg[cnt]);
	}

	printk("\n\nXSEQ GP Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xseq_gp_reg[cnt]);
	}

	printk("\n\nXSEQ-0 Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_0_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xseq_0_reg[cnt]);
	}

	printk("\n\nXSEQ-1 Registers");
	for (cnt = 0; cnt < sizeof(fw->xseq_1_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xseq_1_reg[cnt]);
	}

	printk("\n\nRSEQ GP Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rseq_gp_reg[cnt]);
	}

	printk("\n\nRSEQ-0 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_0_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rseq_0_reg[cnt]);
	}

	printk("\n\nRSEQ-1 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_1_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rseq_1_reg[cnt]);
	}

	printk("\n\nRSEQ-2 Registers");
	for (cnt = 0; cnt < sizeof(fw->rseq_2_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rseq_2_reg[cnt]);
	}

	printk("\n\nCommand DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->cmd_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->cmd_dma_reg[cnt]);
	}

	printk("\n\nRequest0 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->req0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->req0_dma_reg[cnt]);
	}

	printk("\n\nResponse0 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->resp0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->resp0_dma_reg[cnt]);
	}

	printk("\n\nRequest1 Queue DMA Channel Registers");
	for (cnt = 0; cnt < sizeof(fw->req1_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->req1_dma_reg[cnt]);
	}

	printk("\n\nXMT0 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt0_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt0_dma_reg[cnt]);
	}

	printk("\n\nXMT1 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt1_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt1_dma_reg[cnt]);
	}

	printk("\n\nXMT2 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt2_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt2_dma_reg[cnt]);
	}

	printk("\n\nXMT3 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt3_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt3_dma_reg[cnt]);
	}

	printk("\n\nXMT4 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt4_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt4_dma_reg[cnt]);
	}

	printk("\n\nXMT Data DMA Common Registers");
	for (cnt = 0; cnt < sizeof(fw->xmt_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->xmt_data_dma_reg[cnt]);
	}

	printk("\n\nRCV Thread 0 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->rcvt0_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rcvt0_data_dma_reg[cnt]);
	}

	printk("\n\nRCV Thread 1 Data DMA Registers");
	for (cnt = 0; cnt < sizeof(fw->rcvt1_data_dma_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->rcvt1_data_dma_reg[cnt]);
	}

	printk("\n\nRISC GP Registers");
	for (cnt = 0; cnt < sizeof(fw->risc_gp_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->risc_gp_reg[cnt]);
	}


	printk("\n\nLMC Registers");
	for (cnt = 0; cnt < sizeof(fw->lmc_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->lmc_reg[cnt]);
	}

	printk("\n\nFPM Hardware Registers");
	for (cnt = 0; cnt < sizeof(fw->fpm_hdw_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->fpm_hdw_reg[cnt]);
	}

	printk("\n\nFB Hardware Registers");
	for (cnt = 0; cnt < sizeof(fw->fb_hdw_reg) / 4; cnt++) {
		if (cnt % 8 == 0)
			printk("\n");

		printk("%08x ", fw->fb_hdw_reg[cnt]);
	}

	printk("\n\nCode RAM");
	for (cnt = 0; cnt < sizeof (fw->code_ram) / 4; cnt++) {
		if (cnt % 8 == 0) {
			printk("\n%08x: ", cnt + 0x20000);
		}
		printk("%08x ", fw->code_ram[cnt]);
	}

	printk("\n\nExternal Memory");
	ext_mem_cnt = ha->fw_memory_size - 0x100000 + 1;
	for (cnt = 0; cnt < ext_mem_cnt; cnt++) {
		if (cnt % 8 == 0) {
			printk("\n%08x: ", cnt + 0x100000);
		}
		printk("%08x ", fw->ext_mem[cnt]);
	}

	printk("\n[<==END] ISP Debug Dump");
}
