/* bnx2.c: Broadcom NX2 network driver.
 *
 * Copyright (c) 2004, 2005, 2006 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Michael Chan  (mchan@broadcom.com)
 */

#include "bnx2_compat.h"
#include "bnx2.h"
#include "bnx2_fw.h"

#define DRV_MODULE_NAME		"bnx2"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.4.38"
#define DRV_MODULE_RELDATE	"February 10, 2006"

#define RUN_AT(x) (jiffies + (x))

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (5*HZ)

static char version[] __devinitdata =
	"Broadcom NetXtreme II Gigabit Ethernet Driver " DRV_MODULE_NAME " v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Michael Chan <mchan@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM5706/5708 Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static int disable_msi = 0;

MODULE_PARM(disable_msi, "i");
MODULE_PARM_DESC(disable_msi, "Disable Message Signaled Interrupt (MSI)");

typedef enum {
	BCM5706 = 0,
	NC370T,
	NC370I,
	BCM5706S,
	NC370F,
	BCM5708,
	BCM5708S,
} board_t;

/* indexed by board_t, above */
static struct {
	char *name;
} board_info[] __devinitdata = {
	{ "Broadcom NetXtreme II BCM5706 1000Base-T" },
	{ "HP NC370T Multifunction Gigabit Server Adapter" },
	{ "HP NC370i Multifunction Gigabit Server Adapter" },
	{ "Broadcom NetXtreme II BCM5706 1000Base-SX" },
	{ "HP NC370F Multifunction Gigabit Server Adapter" },
	{ "Broadcom NetXtreme II BCM5708 1000Base-T" },
	{ "Broadcom NetXtreme II BCM5708 1000Base-SX" },
	};

static struct pci_device_id bnx2_pci_tbl[] = {
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_VENDOR_ID_HP, 0x3101, 0, 0, NC370T },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_VENDOR_ID_HP, 0x3106, 0, 0, NC370I },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5706 },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5708,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5708 },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706S,
	  PCI_VENDOR_ID_HP, 0x3102, 0, 0, NC370F },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706S,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5706S },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5708S,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5708S },
	{ 0, }
};

static struct flash_spec flash_table[] =
{
	/* Slow EEPROM */
	{0x00000000, 0x40830380, 0x009f0081, 0xa184a053, 0xaf000400,
	 1, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - slow"},
	/* Expansion entry 0001 */
	{0x08000002, 0x4b808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0001"},
	/* Saifun SA25F010 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x04000001, 0x47808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*2,
	 "Non-buffered flash (128kB)"},
	/* Saifun SA25F020 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x0c000003, 0x4f808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*4,
	 "Non-buffered flash (256kB)"},
	/* Expansion entry 0100 */
	{0x11000000, 0x53808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0100"},
	/* Entry 0101: ST M45PE10 (non-buffered flash, TetonII B0) */
	{0x19000002, 0x5b808201, 0x000500db, 0x03840253, 0xaf020406,        
	 0, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*2,
	 "Entry 0101: ST M45PE10 (128kB non-bufferred)"},
	/* Entry 0110: ST M45PE20 (non-buffered flash)*/
	{0x15000001, 0x57808201, 0x000500db, 0x03840253, 0xaf020406,
	 0, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*4,
	 "Entry 0110: ST M45PE20 (256kB non-bufferred)"},
	/* Saifun SA25F005 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x1d000003, 0x5f808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE,
	 "Non-buffered flash (64kB)"},
	/* Fast EEPROM */
	{0x22000000, 0x62808380, 0x009f0081, 0xa184a053, 0xaf000400,
	 1, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - fast"},
	/* Expansion entry 1001 */
	{0x2a000002, 0x6b808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1001"},
	/* Expansion entry 1010 */
	{0x26000001, 0x67808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1010"},
	/* ATMEL AT45DB011B (buffered flash) */
	{0x2e000003, 0x6e808273, 0x00570081, 0x68848353, 0xaf000400,
	 1, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE,
	 "Buffered flash (128kB)"},
	/* Expansion entry 1100 */
	{0x33000000, 0x73808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1100"},
	/* Expansion entry 1101 */
	{0x3b000002, 0x7b808201, 0x00050081, 0x03840253, 0xaf020406,
	 0, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1101"},
	/* Ateml Expansion entry 1110 */
	{0x37000001, 0x76808273, 0x00570081, 0x68848353, 0xaf000400,
	 1, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1110 (Atmel)"},
	/* ATMEL AT45DB021B (buffered flash) */
	{0x3f000003, 0x7e808273, 0x00570081, 0x68848353, 0xaf000400,
	 1, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE*2,
	 "Buffered flash (256kB)"},
};

MODULE_DEVICE_TABLE(pci, bnx2_pci_tbl);

static inline u32 bnx2_tx_avail(struct bnx2 *bp)
{
	u32 diff = TX_RING_IDX(bp->tx_prod) - TX_RING_IDX(bp->tx_cons);

	if (diff > MAX_TX_DESC_CNT)
		diff = (diff & MAX_TX_DESC_CNT) - 1;
	return (bp->tx_ring_size - diff);
}

static u32
bnx2_reg_rd_ind(struct bnx2 *bp, u32 offset)
{
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW_ADDRESS, offset);
	return (REG_RD(bp, BNX2_PCICFG_REG_WINDOW));
}

static void
bnx2_reg_wr_ind(struct bnx2 *bp, u32 offset, u32 val)
{
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW_ADDRESS, offset);
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW, val);
}

static void
bnx2_ctx_wr(struct bnx2 *bp, u32 cid_addr, u32 offset, u32 val)
{
	offset += cid_addr;
	REG_WR(bp, BNX2_CTX_DATA_ADR, offset);
	REG_WR(bp, BNX2_CTX_DATA, val);
}

static int
bnx2_read_phy(struct bnx2 *bp, u32 reg, u32 *val)
{
	u32 val1;
	int i, ret;

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 &= ~BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	val1 = (bp->phy_addr << 21) | (reg << 16) |
		BNX2_EMAC_MDIO_COMM_COMMAND_READ | BNX2_EMAC_MDIO_COMM_DISEXT |
		BNX2_EMAC_MDIO_COMM_START_BUSY;
	REG_WR(bp, BNX2_EMAC_MDIO_COMM, val1);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
		if (!(val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);

			val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
			val1 &= BNX2_EMAC_MDIO_COMM_DATA;

			break;
		}
	}

	if (val1 & BNX2_EMAC_MDIO_COMM_START_BUSY) {
		*val = 0x0;
		ret = -EBUSY;
	}
	else {
		*val = val1;
		ret = 0;
	}

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 |= BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	return ret;
}

static int
bnx2_write_phy(struct bnx2 *bp, u32 reg, u32 val)
{
	u32 val1;
	int i, ret;

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 &= ~BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	val1 = (bp->phy_addr << 21) | (reg << 16) | val |
		BNX2_EMAC_MDIO_COMM_COMMAND_WRITE |
		BNX2_EMAC_MDIO_COMM_START_BUSY | BNX2_EMAC_MDIO_COMM_DISEXT;
	REG_WR(bp, BNX2_EMAC_MDIO_COMM, val1);
    
	for (i = 0; i < 50; i++) {
		udelay(10);

		val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
		if (!(val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}

	if (val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)
        	ret = -EBUSY;
	else
		ret = 0;

	if (bp->phy_flags & PHY_INT_MODE_AUTO_POLLING_FLAG) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 |= BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	return ret;
}

static void
bnx2_disable_int(struct bnx2 *bp)
{
	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
	       BNX2_PCICFG_INT_ACK_CMD_MASK_INT);
	REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD);
}

static void
bnx2_enable_int(struct bnx2 *bp)
{
	u32 val;

	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
	       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
	       BNX2_PCICFG_INT_ACK_CMD_MASK_INT | bp->last_status_idx);

	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
	       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID | bp->last_status_idx);

	val = REG_RD(bp, BNX2_HC_COMMAND);
	REG_WR(bp, BNX2_HC_COMMAND, val | BNX2_HC_COMMAND_COAL_NOW);
}

static void
bnx2_disable_int_sync(struct bnx2 *bp)
{
	atomic_inc(&bp->intr_sem);
	bnx2_disable_int(bp);
	synchronize_irq();
}

static void
bnx2_netif_stop(struct bnx2 *bp)
{
	bnx2_disable_int_sync(bp);
	if (netif_running(bp->dev)) {
		netif_poll_disable(bp->dev);
		netif_tx_disable(bp->dev);
		bp->dev->trans_start = jiffies;	/* prevent tx timeout */
	}
}

static void
bnx2_netif_start(struct bnx2 *bp)
{
	if (atomic_dec_and_test(&bp->intr_sem)) {
		if (netif_running(bp->dev)) {
			netif_wake_queue(bp->dev);
			netif_poll_enable(bp->dev);
			bnx2_enable_int(bp);
		}
	}
}

static void
bnx2_free_mem(struct bnx2 *bp)
{
	int i;

	if (bp->stats_blk) {
		pci_free_consistent(bp->pdev, sizeof(struct statistics_block),
				    bp->stats_blk, bp->stats_blk_mapping);
		bp->stats_blk = NULL;
	}
	if (bp->status_blk) {
		pci_free_consistent(bp->pdev, sizeof(struct status_block),
				    bp->status_blk, bp->status_blk_mapping);
		bp->status_blk = NULL;
	}
	if (bp->tx_desc_ring) {
		pci_free_consistent(bp->pdev,
				    sizeof(struct tx_bd) * TX_DESC_CNT,
				    bp->tx_desc_ring, bp->tx_desc_mapping);
		bp->tx_desc_ring = NULL;
	}
	kfree(bp->tx_buf_ring);
	bp->tx_buf_ring = NULL;
	for (i = 0; i < bp->rx_max_ring; i++) {
		if (bp->rx_desc_ring[i])
			pci_free_consistent(bp->pdev,
					    sizeof(struct rx_bd) * RX_DESC_CNT,
					    bp->rx_desc_ring[i],
					    bp->rx_desc_mapping[i]);
		bp->rx_desc_ring[i] = NULL;
	}
	vfree(bp->rx_buf_ring);
	bp->rx_buf_ring = NULL;
}

static int
bnx2_alloc_mem(struct bnx2 *bp)
{
	int i;

	bp->tx_buf_ring = kmalloc(sizeof(struct sw_bd) * TX_DESC_CNT,
				     GFP_KERNEL);
	if (bp->tx_buf_ring == NULL)
		return -ENOMEM;

	memset(bp->tx_buf_ring, 0, sizeof(struct sw_bd) * TX_DESC_CNT);
	bp->tx_desc_ring = pci_alloc_consistent(bp->pdev,
					        sizeof(struct tx_bd) *
						TX_DESC_CNT,
						&bp->tx_desc_mapping);
	if (bp->tx_desc_ring == NULL)
		goto alloc_mem_err;

	bp->rx_buf_ring = vmalloc(sizeof(struct sw_bd) * RX_DESC_CNT *
				  bp->rx_max_ring);
	if (bp->rx_buf_ring == NULL)
		goto alloc_mem_err;

	memset(bp->rx_buf_ring, 0, sizeof(struct sw_bd) * RX_DESC_CNT *
				   bp->rx_max_ring);

	for (i = 0; i < bp->rx_max_ring; i++) {
		bp->rx_desc_ring[i] =
			pci_alloc_consistent(bp->pdev,
					     sizeof(struct rx_bd) * RX_DESC_CNT,
					     &bp->rx_desc_mapping[i]);
		if (bp->rx_desc_ring[i] == NULL)
			goto alloc_mem_err;

	}

	bp->status_blk = pci_alloc_consistent(bp->pdev,
					      sizeof(struct status_block),
					      &bp->status_blk_mapping);
	if (bp->status_blk == NULL)
		goto alloc_mem_err;

	memset(bp->status_blk, 0, sizeof(struct status_block));

	bp->stats_blk = pci_alloc_consistent(bp->pdev,
					     sizeof(struct statistics_block),
					     &bp->stats_blk_mapping);
	if (bp->stats_blk == NULL)
		goto alloc_mem_err;

	memset(bp->stats_blk, 0, sizeof(struct statistics_block));

	return 0;

alloc_mem_err:
	bnx2_free_mem(bp);
	return -ENOMEM;
}

static void
bnx2_report_fw_link(struct bnx2 *bp)
{
	u32 fw_link_status = 0;

	if (bp->link_up) {
		u32 bmsr;

		switch (bp->line_speed) {
		case SPEED_10:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_10HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_10FULL;
			break;
		case SPEED_100:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_100HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_100FULL;
			break;
		case SPEED_1000:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_1000HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_1000FULL;
			break;
		case SPEED_2500:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_2500HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_2500FULL;
			break;
		}

		fw_link_status |= BNX2_LINK_STATUS_LINK_UP;

		if (bp->autoneg) {
			fw_link_status |= BNX2_LINK_STATUS_AN_ENABLED;

			bnx2_read_phy(bp, MII_BMSR, &bmsr);
			bnx2_read_phy(bp, MII_BMSR, &bmsr);

			if (!(bmsr & BMSR_ANEGCOMPLETE) ||
			    bp->phy_flags & PHY_PARALLEL_DETECT_FLAG)
				fw_link_status |= BNX2_LINK_STATUS_PARALLEL_DET;
			else
				fw_link_status |= BNX2_LINK_STATUS_AN_COMPLETE;
		}
	}
	else
		fw_link_status = BNX2_LINK_STATUS_LINK_DOWN;

	REG_WR_IND(bp, bp->shmem_base + BNX2_LINK_STATUS, fw_link_status);
}

static void
bnx2_report_link(struct bnx2 *bp)
{
	if (bp->link_up) {
		netif_carrier_on(bp->dev);
		printk(KERN_INFO PFX "%s NIC Link is Up, ", bp->dev->name);

		printk("%d Mbps ", bp->line_speed);

		if (bp->duplex == DUPLEX_FULL)
			printk("full duplex");
		else
			printk("half duplex");

		if (bp->flow_ctrl) {
			if (bp->flow_ctrl & FLOW_CTRL_RX) {
				printk(", receive ");
				if (bp->flow_ctrl & FLOW_CTRL_TX)
					printk("& transmit ");
			}
			else {
				printk(", transmit ");
			}
			printk("flow control ON");
		}
		printk("\n");
	}
	else {
		netif_carrier_off(bp->dev);
		printk(KERN_ERR PFX "%s NIC Link is Down\n", bp->dev->name);
	}

	bnx2_report_fw_link(bp);
}

static void
bnx2_resolve_flow_ctrl(struct bnx2 *bp)
{
	u32 local_adv, remote_adv;

	bp->flow_ctrl = 0;
	if ((bp->autoneg & (AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) != 
		(AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) {

		if (bp->duplex == DUPLEX_FULL) {
			bp->flow_ctrl = bp->req_flow_ctrl;
		}
		return;
	}

	if (bp->duplex != DUPLEX_FULL) {
		return;
	}

	if ((bp->phy_flags & PHY_SERDES_FLAG) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5708)) {
		u32 val;

		bnx2_read_phy(bp, BCM5708S_1000X_STAT1, &val);
		if (val & BCM5708S_1000X_STAT1_TX_PAUSE)
			bp->flow_ctrl |= FLOW_CTRL_TX;
		if (val & BCM5708S_1000X_STAT1_RX_PAUSE)
			bp->flow_ctrl |= FLOW_CTRL_RX;
		return;
	}

	bnx2_read_phy(bp, MII_ADVERTISE, &local_adv);
	bnx2_read_phy(bp, MII_LPA, &remote_adv);

	if (bp->phy_flags & PHY_SERDES_FLAG) {
		u32 new_local_adv = 0;
		u32 new_remote_adv = 0;

		if (local_adv & ADVERTISE_1000XPAUSE)
			new_local_adv |= ADVERTISE_PAUSE_CAP;
		if (local_adv & ADVERTISE_1000XPSE_ASYM)
			new_local_adv |= ADVERTISE_PAUSE_ASYM;
		if (remote_adv & ADVERTISE_1000XPAUSE)
			new_remote_adv |= ADVERTISE_PAUSE_CAP;
		if (remote_adv & ADVERTISE_1000XPSE_ASYM)
			new_remote_adv |= ADVERTISE_PAUSE_ASYM;

		local_adv = new_local_adv;
		remote_adv = new_remote_adv;
	}

	/* See Table 28B-3 of 802.3ab-1999 spec. */
	if (local_adv & ADVERTISE_PAUSE_CAP) {
		if(local_adv & ADVERTISE_PAUSE_ASYM) {
	                if (remote_adv & ADVERTISE_PAUSE_CAP) {
				bp->flow_ctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;
			}
			else if (remote_adv & ADVERTISE_PAUSE_ASYM) {
				bp->flow_ctrl = FLOW_CTRL_RX;
			}
		}
		else {
			if (remote_adv & ADVERTISE_PAUSE_CAP) {
				bp->flow_ctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;
			}
		}
	}
	else if (local_adv & ADVERTISE_PAUSE_ASYM) {
		if ((remote_adv & ADVERTISE_PAUSE_CAP) &&
			(remote_adv & ADVERTISE_PAUSE_ASYM)) {

			bp->flow_ctrl = FLOW_CTRL_TX;
		}
	}
}

static int
bnx2_5708s_linkup(struct bnx2 *bp)
{
	u32 val;

	bp->link_up = 1;
	bnx2_read_phy(bp, BCM5708S_1000X_STAT1, &val);
	switch (val & BCM5708S_1000X_STAT1_SPEED_MASK) {
		case BCM5708S_1000X_STAT1_SPEED_10:
			bp->line_speed = SPEED_10;
			break;
		case BCM5708S_1000X_STAT1_SPEED_100:
			bp->line_speed = SPEED_100;
			break;
		case BCM5708S_1000X_STAT1_SPEED_1G:
			bp->line_speed = SPEED_1000;
			break;
		case BCM5708S_1000X_STAT1_SPEED_2G5:
			bp->line_speed = SPEED_2500;
			break;
	}
	if (val & BCM5708S_1000X_STAT1_FD)
		bp->duplex = DUPLEX_FULL;
	else
		bp->duplex = DUPLEX_HALF;

	return 0;
}

static int
bnx2_5706s_linkup(struct bnx2 *bp)
{
	u32 bmcr, local_adv, remote_adv, common;

	bp->link_up = 1;
	bp->line_speed = SPEED_1000;

	bnx2_read_phy(bp, MII_BMCR, &bmcr);
	if (bmcr & BMCR_FULLDPLX) {
		bp->duplex = DUPLEX_FULL;
	}
	else {
		bp->duplex = DUPLEX_HALF;
	}

	if (!(bmcr & BMCR_ANENABLE)) {
		return 0;
	}

	bnx2_read_phy(bp, MII_ADVERTISE, &local_adv);
	bnx2_read_phy(bp, MII_LPA, &remote_adv);

	common = local_adv & remote_adv;
	if (common & (ADVERTISE_1000XHALF | ADVERTISE_1000XFULL)) {

		if (common & ADVERTISE_1000XFULL) {
			bp->duplex = DUPLEX_FULL;
		}
		else {
			bp->duplex = DUPLEX_HALF;
		}
	}

	return 0;
}

static int
bnx2_copper_linkup(struct bnx2 *bp)
{
	u32 bmcr;

	bnx2_read_phy(bp, MII_BMCR, &bmcr);
	if (bmcr & BMCR_ANENABLE) {
		u32 local_adv, remote_adv, common;

		bnx2_read_phy(bp, MII_CTRL1000, &local_adv);
		bnx2_read_phy(bp, MII_STAT1000, &remote_adv);

		common = local_adv & (remote_adv >> 2);
		if (common & ADVERTISE_1000FULL) {
			bp->line_speed = SPEED_1000;
			bp->duplex = DUPLEX_FULL;
		}
		else if (common & ADVERTISE_1000HALF) {
			bp->line_speed = SPEED_1000;
			bp->duplex = DUPLEX_HALF;
		}
		else {
			bnx2_read_phy(bp, MII_ADVERTISE, &local_adv);
			bnx2_read_phy(bp, MII_LPA, &remote_adv);

			common = local_adv & remote_adv;
			if (common & ADVERTISE_100FULL) {
				bp->line_speed = SPEED_100;
				bp->duplex = DUPLEX_FULL;
			}
			else if (common & ADVERTISE_100HALF) {
				bp->line_speed = SPEED_100;
				bp->duplex = DUPLEX_HALF;
			}
			else if (common & ADVERTISE_10FULL) {
				bp->line_speed = SPEED_10;
				bp->duplex = DUPLEX_FULL;
			}
			else if (common & ADVERTISE_10HALF) {
				bp->line_speed = SPEED_10;
				bp->duplex = DUPLEX_HALF;
			}
			else {
				bp->line_speed = 0;
				bp->link_up = 0;
			}
		}
	}
	else {
		if (bmcr & BMCR_SPEED100) {
			bp->line_speed = SPEED_100;
		}
		else {
			bp->line_speed = SPEED_10;
		}
		if (bmcr & BMCR_FULLDPLX) {
			bp->duplex = DUPLEX_FULL;
		}
		else {
			bp->duplex = DUPLEX_HALF;
		}
	}

	return 0;
}

static int
bnx2_set_mac_link(struct bnx2 *bp)
{
	u32 val;

	REG_WR(bp, BNX2_EMAC_TX_LENGTHS, 0x2620);
	if (bp->link_up && (bp->line_speed == SPEED_1000) &&
		(bp->duplex == DUPLEX_HALF)) {
		REG_WR(bp, BNX2_EMAC_TX_LENGTHS, 0x26ff);
	}

	/* Configure the EMAC mode register. */
	val = REG_RD(bp, BNX2_EMAC_MODE);

	val &= ~(BNX2_EMAC_MODE_PORT | BNX2_EMAC_MODE_HALF_DUPLEX |
		BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK |
		BNX2_EMAC_MODE_25G);

	if (bp->link_up) {
		switch (bp->line_speed) {
			case SPEED_10:
				if (CHIP_NUM(bp) == CHIP_NUM_5708) {
					val |= BNX2_EMAC_MODE_PORT_MII_10;
					break;
				}
				/* fall through */
			case SPEED_100:
				val |= BNX2_EMAC_MODE_PORT_MII;
				break;
			case SPEED_2500:
				val |= BNX2_EMAC_MODE_25G;
				/* fall through */
			case SPEED_1000:
				val |= BNX2_EMAC_MODE_PORT_GMII;
				break;
		}
	}
	else {
		val |= BNX2_EMAC_MODE_PORT_GMII;
	}

	/* Set the MAC to operate in the appropriate duplex mode. */
	if (bp->duplex == DUPLEX_HALF)
		val |= BNX2_EMAC_MODE_HALF_DUPLEX;
	REG_WR(bp, BNX2_EMAC_MODE, val);

	/* Enable/disable rx PAUSE. */
	bp->rx_mode &= ~BNX2_EMAC_RX_MODE_FLOW_EN;

	if (bp->flow_ctrl & FLOW_CTRL_RX)
		bp->rx_mode |= BNX2_EMAC_RX_MODE_FLOW_EN;
	REG_WR(bp, BNX2_EMAC_RX_MODE, bp->rx_mode);

	/* Enable/disable tx PAUSE. */
	val = REG_RD(bp, BNX2_EMAC_TX_MODE);
	val &= ~BNX2_EMAC_TX_MODE_FLOW_EN;

	if (bp->flow_ctrl & FLOW_CTRL_TX)
		val |= BNX2_EMAC_TX_MODE_FLOW_EN;
	REG_WR(bp, BNX2_EMAC_TX_MODE, val);

	/* Acknowledge the interrupt. */
	REG_WR(bp, BNX2_EMAC_STATUS, BNX2_EMAC_STATUS_LINK_CHANGE);

	return 0;
}

static int
bnx2_set_link(struct bnx2 *bp)
{
	u32 bmsr;
	u8 link_up;

	if (bp->loopback == MAC_LOOPBACK) {
		bp->link_up = 1;
		return 0;
	}

	link_up = bp->link_up;

	bnx2_read_phy(bp, MII_BMSR, &bmsr);
	bnx2_read_phy(bp, MII_BMSR, &bmsr);

	if ((bp->phy_flags & PHY_SERDES_FLAG) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5706)) {
		u32 val;

		val = REG_RD(bp, BNX2_EMAC_STATUS);
		if (val & BNX2_EMAC_STATUS_LINK)
			bmsr |= BMSR_LSTATUS;
		else
			bmsr &= ~BMSR_LSTATUS;
	}

	if (bmsr & BMSR_LSTATUS) {
		bp->link_up = 1;

		if (bp->phy_flags & PHY_SERDES_FLAG) {
			if (CHIP_NUM(bp) == CHIP_NUM_5706)
				bnx2_5706s_linkup(bp);
			else if (CHIP_NUM(bp) == CHIP_NUM_5708)
				bnx2_5708s_linkup(bp);
		}
		else {
			bnx2_copper_linkup(bp);
		}
		bnx2_resolve_flow_ctrl(bp);
	}
	else {
		if ((bp->phy_flags & PHY_SERDES_FLAG) &&
			(bp->autoneg & AUTONEG_SPEED)) {

			u32 bmcr;

			bnx2_read_phy(bp, MII_BMCR, &bmcr);
			if (!(bmcr & BMCR_ANENABLE)) {
				bnx2_write_phy(bp, MII_BMCR, bmcr |
					BMCR_ANENABLE);
			}
		}
		bp->phy_flags &= ~PHY_PARALLEL_DETECT_FLAG;
		bp->link_up = 0;
	}

	if (bp->link_up != link_up) {
		bnx2_report_link(bp);
	}

	bnx2_set_mac_link(bp);

	return 0;
}

static int
bnx2_reset_phy(struct bnx2 *bp)
{
	int i;
	u32 reg;

        bnx2_write_phy(bp, MII_BMCR, BMCR_RESET);

#define PHY_RESET_MAX_WAIT 100
	for (i = 0; i < PHY_RESET_MAX_WAIT; i++) {
		udelay(10);

		bnx2_read_phy(bp, MII_BMCR, &reg);
		if (!(reg & BMCR_RESET)) {
			udelay(20);
			break;
		}
	}
	if (i == PHY_RESET_MAX_WAIT) {
		return -EBUSY;
	}
	return 0;
}

static u32
bnx2_phy_get_pause_adv(struct bnx2 *bp)
{
	u32 adv = 0;

	if ((bp->req_flow_ctrl & (FLOW_CTRL_RX | FLOW_CTRL_TX)) ==
		(FLOW_CTRL_RX | FLOW_CTRL_TX)) {

		if (bp->phy_flags & PHY_SERDES_FLAG) {
			adv = ADVERTISE_1000XPAUSE;
		}
		else {
			adv = ADVERTISE_PAUSE_CAP;
		}
	}
	else if (bp->req_flow_ctrl & FLOW_CTRL_TX) {
		if (bp->phy_flags & PHY_SERDES_FLAG) {
			adv = ADVERTISE_1000XPSE_ASYM;
		}
		else {
			adv = ADVERTISE_PAUSE_ASYM;
		}
	}
	else if (bp->req_flow_ctrl & FLOW_CTRL_RX) {
		if (bp->phy_flags & PHY_SERDES_FLAG) {
			adv = ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM;
		}
		else {
			adv = ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
		}
	}
	return adv;
}

static int
bnx2_setup_serdes_phy(struct bnx2 *bp)
{
	u32 adv, bmcr, up1;
	u32 new_adv = 0;

	if (!(bp->autoneg & AUTONEG_SPEED)) {
		u32 new_bmcr;
		int force_link_down = 0;

		if (CHIP_NUM(bp) == CHIP_NUM_5708) {
			bnx2_read_phy(bp, BCM5708S_UP1, &up1);
			if (up1 & BCM5708S_UP1_2G5) {
				up1 &= ~BCM5708S_UP1_2G5;
				bnx2_write_phy(bp, BCM5708S_UP1, up1);
				force_link_down = 1;
			}
		}

		bnx2_read_phy(bp, MII_ADVERTISE, &adv);
		adv &= ~(ADVERTISE_1000XFULL | ADVERTISE_1000XHALF);

		bnx2_read_phy(bp, MII_BMCR, &bmcr);
		new_bmcr = bmcr & ~BMCR_ANENABLE;
		new_bmcr |= BMCR_SPEED1000;
		if (bp->req_duplex == DUPLEX_FULL) {
			adv |= ADVERTISE_1000XFULL;
			new_bmcr |= BMCR_FULLDPLX;
		}
		else {
			adv |= ADVERTISE_1000XHALF;
			new_bmcr &= ~BMCR_FULLDPLX;
		}
		if ((new_bmcr != bmcr) || (force_link_down)) {
			/* Force a link down visible on the other side */
			if (bp->link_up) {
				bnx2_write_phy(bp, MII_ADVERTISE, adv &
					       ~(ADVERTISE_1000XFULL |
						 ADVERTISE_1000XHALF));
				bnx2_write_phy(bp, MII_BMCR, bmcr |
					BMCR_ANRESTART | BMCR_ANENABLE);

				bp->link_up = 0;
				netif_carrier_off(bp->dev);
				bnx2_write_phy(bp, MII_BMCR, new_bmcr);
			}
			bnx2_write_phy(bp, MII_ADVERTISE, adv);
			bnx2_write_phy(bp, MII_BMCR, new_bmcr);
		}
		return 0;
	}

	if (bp->phy_flags & PHY_2_5G_CAPABLE_FLAG) {
		bnx2_read_phy(bp, BCM5708S_UP1, &up1);
		up1 |= BCM5708S_UP1_2G5;
		bnx2_write_phy(bp, BCM5708S_UP1, up1);
	}

	if (bp->advertising & ADVERTISED_1000baseT_Full)
		new_adv |= ADVERTISE_1000XFULL;

	new_adv |= bnx2_phy_get_pause_adv(bp);

	bnx2_read_phy(bp, MII_ADVERTISE, &adv);
	bnx2_read_phy(bp, MII_BMCR, &bmcr);

	bp->serdes_an_pending = 0;
	if ((adv != new_adv) || ((bmcr & BMCR_ANENABLE) == 0)) {
		/* Force a link down visible on the other side */
		if (bp->link_up) {
			int i;

			bnx2_write_phy(bp, MII_BMCR, BMCR_LOOPBACK);
			for (i = 0; i < 110; i++) {
				udelay(100);
			}
		}

		bnx2_write_phy(bp, MII_ADVERTISE, new_adv);
		bnx2_write_phy(bp, MII_BMCR, bmcr | BMCR_ANRESTART |
			BMCR_ANENABLE);
		if (CHIP_NUM(bp) == CHIP_NUM_5706) {
			/* Speed up link-up time when the link partner
			 * does not autonegotiate which is very common
			 * in blade servers. Some blade servers use
			 * IPMI for kerboard input and it's important
			 * to minimize link disruptions. Autoneg. involves
			 * exchanging base pages plus 3 next pages and
			 * normally completes in about 120 msec.
			 */
			bp->current_interval = SERDES_AN_TIMEOUT;
			bp->serdes_an_pending = 1;
			mod_timer(&bp->timer, jiffies + bp->current_interval);
		}
	}

	return 0;
}

#define ETHTOOL_ALL_FIBRE_SPEED						\
	(ADVERTISED_1000baseT_Full)

#define ETHTOOL_ALL_COPPER_SPEED					\
	(ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |		\
	ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |		\
	ADVERTISED_1000baseT_Full)

#define PHY_ALL_10_100_SPEED (ADVERTISE_10HALF | ADVERTISE_10FULL | \
	ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_CSMA)
	
#define PHY_ALL_1000_SPEED (ADVERTISE_1000HALF | ADVERTISE_1000FULL)

static int
bnx2_setup_copper_phy(struct bnx2 *bp)
{
	u32 bmcr;
	u32 new_bmcr;

	bnx2_read_phy(bp, MII_BMCR, &bmcr);

	if (bp->autoneg & AUTONEG_SPEED) {
		u32 adv_reg, adv1000_reg;
		u32 new_adv_reg = 0;
		u32 new_adv1000_reg = 0;

		bnx2_read_phy(bp, MII_ADVERTISE, &adv_reg);
		adv_reg &= (PHY_ALL_10_100_SPEED | ADVERTISE_PAUSE_CAP |
			ADVERTISE_PAUSE_ASYM);

		bnx2_read_phy(bp, MII_CTRL1000, &adv1000_reg);
		adv1000_reg &= PHY_ALL_1000_SPEED;

		if (bp->advertising & ADVERTISED_10baseT_Half)
			new_adv_reg |= ADVERTISE_10HALF;
		if (bp->advertising & ADVERTISED_10baseT_Full)
			new_adv_reg |= ADVERTISE_10FULL;
		if (bp->advertising & ADVERTISED_100baseT_Half)
			new_adv_reg |= ADVERTISE_100HALF;
		if (bp->advertising & ADVERTISED_100baseT_Full)
			new_adv_reg |= ADVERTISE_100FULL;
		if (bp->advertising & ADVERTISED_1000baseT_Full)
			new_adv1000_reg |= ADVERTISE_1000FULL;
		
		new_adv_reg |= ADVERTISE_CSMA;

		new_adv_reg |= bnx2_phy_get_pause_adv(bp);

		if ((adv1000_reg != new_adv1000_reg) ||
			(adv_reg != new_adv_reg) ||
			((bmcr & BMCR_ANENABLE) == 0)) {

			bnx2_write_phy(bp, MII_ADVERTISE, new_adv_reg);
			bnx2_write_phy(bp, MII_CTRL1000, new_adv1000_reg);
			bnx2_write_phy(bp, MII_BMCR, BMCR_ANRESTART |
				BMCR_ANENABLE);
		}
		else if (bp->link_up) {
			/* Flow ctrl may have changed from auto to forced */
			/* or vice-versa. */

			bnx2_resolve_flow_ctrl(bp);
			bnx2_set_mac_link(bp);
		}
		return 0;
	}

	new_bmcr = 0;
	if (bp->req_line_speed == SPEED_100) {
		new_bmcr |= BMCR_SPEED100;
	}
	if (bp->req_duplex == DUPLEX_FULL) {
		new_bmcr |= BMCR_FULLDPLX;
	}
	if (new_bmcr != bmcr) {
		u32 bmsr;
		int i = 0;

		bnx2_read_phy(bp, MII_BMSR, &bmsr);
		bnx2_read_phy(bp, MII_BMSR, &bmsr);
		
		if (bmsr & BMSR_LSTATUS) {
			/* Force link down */
			bnx2_write_phy(bp, MII_BMCR, BMCR_LOOPBACK);
			do {
				udelay(100);
				bnx2_read_phy(bp, MII_BMSR, &bmsr);
				bnx2_read_phy(bp, MII_BMSR, &bmsr);
				i++;
			} while ((bmsr & BMSR_LSTATUS) && (i < 620));
		}

		bnx2_write_phy(bp, MII_BMCR, new_bmcr);

		/* Normally, the new speed is setup after the link has
		 * gone down and up again. In some cases, link will not go
		 * down so we need to set up the new speed here.
		 */
		if (bmsr & BMSR_LSTATUS) {
			bp->line_speed = bp->req_line_speed;
			bp->duplex = bp->req_duplex;
			bnx2_resolve_flow_ctrl(bp);
			bnx2_set_mac_link(bp);
		}
	}
	return 0;
}

static int
bnx2_setup_phy(struct bnx2 *bp)
{
	if (bp->loopback == MAC_LOOPBACK)
		return 0;

	if (bp->phy_flags & PHY_SERDES_FLAG) {
		return (bnx2_setup_serdes_phy(bp));
	}
	else {
		return (bnx2_setup_copper_phy(bp));
	}
}

static int
bnx2_init_5708s_phy(struct bnx2 *bp)
{
	u32 val;

	bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG3);
	bnx2_write_phy(bp, BCM5708S_DIG_3_0, BCM5708S_DIG_3_0_USE_IEEE);
	bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG);

	bnx2_read_phy(bp, BCM5708S_1000X_CTL1, &val);
	val |= BCM5708S_1000X_CTL1_FIBER_MODE | BCM5708S_1000X_CTL1_AUTODET_EN;
	bnx2_write_phy(bp, BCM5708S_1000X_CTL1, val);

	bnx2_read_phy(bp, BCM5708S_1000X_CTL2, &val);
	val |= BCM5708S_1000X_CTL2_PLLEL_DET_EN;
	bnx2_write_phy(bp, BCM5708S_1000X_CTL2, val);

	if (bp->phy_flags & PHY_2_5G_CAPABLE_FLAG) {
		bnx2_read_phy(bp, BCM5708S_UP1, &val);
		val |= BCM5708S_UP1_2G5;
		bnx2_write_phy(bp, BCM5708S_UP1, val);
	}

	if ((CHIP_ID(bp) == CHIP_ID_5708_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B1)) {
		/* increase tx signal amplitude */
		bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
			       BCM5708S_BLK_ADDR_TX_MISC);
		bnx2_read_phy(bp, BCM5708S_TX_ACTL1, &val);
		val &= ~BCM5708S_TX_ACTL1_DRIVER_VCM;
		bnx2_write_phy(bp, BCM5708S_TX_ACTL1, val);
		bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG);
	}

	val = REG_RD_IND(bp, bp->shmem_base + BNX2_PORT_HW_CFG_CONFIG) &
	      BNX2_PORT_HW_CFG_CFG_TXCTL3_MASK;

	if (val) {
		u32 is_backplane;

		is_backplane = REG_RD_IND(bp, bp->shmem_base +
					  BNX2_SHARED_HW_CFG_CONFIG);
		if (is_backplane & BNX2_SHARED_HW_CFG_PHY_BACKPLANE) {
			bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
				       BCM5708S_BLK_ADDR_TX_MISC);
			bnx2_write_phy(bp, BCM5708S_TX_ACTL3, val);
			bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
				       BCM5708S_BLK_ADDR_DIG);
		}
	}
	return 0;
}

static int
bnx2_init_5706s_phy(struct bnx2 *bp)
{
	bp->phy_flags &= ~PHY_PARALLEL_DETECT_FLAG;

	if (CHIP_NUM(bp) == CHIP_NUM_5706) {
        	REG_WR(bp, BNX2_MISC_UNUSED0, 0x300);
	}

	if (bp->dev->mtu > 1500) {
		u32 val;

		/* Set extended packet length bit */
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, (val & 0xfff8) | 0x4000);

		bnx2_write_phy(bp, 0x1c, 0x6c00);
		bnx2_read_phy(bp, 0x1c, &val);
		bnx2_write_phy(bp, 0x1c, (val & 0x3ff) | 0xec02);
	}
	else {
		u32 val;

		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val & ~0x4007);

		bnx2_write_phy(bp, 0x1c, 0x6c00);
		bnx2_read_phy(bp, 0x1c, &val);
		bnx2_write_phy(bp, 0x1c, (val & 0x3fd) | 0xec00);
	}

	return 0;
}

static int
bnx2_init_copper_phy(struct bnx2 *bp)
{
	u32 val;

	bp->phy_flags |= PHY_CRC_FIX_FLAG;

	if (bp->phy_flags & PHY_CRC_FIX_FLAG) {
		bnx2_write_phy(bp, 0x18, 0x0c00);
		bnx2_write_phy(bp, 0x17, 0x000a);
		bnx2_write_phy(bp, 0x15, 0x310b);
		bnx2_write_phy(bp, 0x17, 0x201f);
		bnx2_write_phy(bp, 0x15, 0x9506);
		bnx2_write_phy(bp, 0x17, 0x401f);
		bnx2_write_phy(bp, 0x15, 0x14e2);
		bnx2_write_phy(bp, 0x18, 0x0400);
	}

	if (bp->dev->mtu > 1500) {
		/* Set extended packet length bit */
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val | 0x4000);

		bnx2_read_phy(bp, 0x10, &val);
		bnx2_write_phy(bp, 0x10, val | 0x1);
	}
	else {
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val & ~0x4007);

		bnx2_read_phy(bp, 0x10, &val);
		bnx2_write_phy(bp, 0x10, val & ~0x1);
	}

	/* ethernet@wirespeed */
	bnx2_write_phy(bp, 0x18, 0x7007);
	bnx2_read_phy(bp, 0x18, &val);
	bnx2_write_phy(bp, 0x18, val | (1 << 15) | (1 << 4));
	return 0;
}


static int
bnx2_init_phy(struct bnx2 *bp)
{
	u32 val;
	int rc = 0;

	bp->phy_flags &= ~PHY_INT_MODE_MASK_FLAG;
	bp->phy_flags |= PHY_INT_MODE_LINK_READY_FLAG;

        REG_WR(bp, BNX2_EMAC_ATTENTION_ENA, BNX2_EMAC_ATTENTION_ENA_LINK);

	bnx2_reset_phy(bp);

	bnx2_read_phy(bp, MII_PHYSID1, &val);
	bp->phy_id = val << 16;
	bnx2_read_phy(bp, MII_PHYSID2, &val);
	bp->phy_id |= val & 0xffff;

	if (bp->phy_flags & PHY_SERDES_FLAG) {
		if (CHIP_NUM(bp) == CHIP_NUM_5706)
			rc = bnx2_init_5706s_phy(bp);
		else if (CHIP_NUM(bp) == CHIP_NUM_5708)
			rc = bnx2_init_5708s_phy(bp);
	}
	else {
		rc = bnx2_init_copper_phy(bp);
	}

	bnx2_setup_phy(bp);

	return rc;
}

static int
bnx2_set_mac_loopback(struct bnx2 *bp)
{
	u32 mac_mode;

	mac_mode = REG_RD(bp, BNX2_EMAC_MODE);
	mac_mode &= ~BNX2_EMAC_MODE_PORT;
	mac_mode |= BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK;
	REG_WR(bp, BNX2_EMAC_MODE, mac_mode);
	bp->link_up = 1;
	return 0;
}

static int bnx2_test_link(struct bnx2 *);

static int
bnx2_set_phy_loopback(struct bnx2 *bp)
{
	u32 mac_mode;
	int rc, i;

	spin_lock_bh(&bp->phy_lock);
	rc = bnx2_write_phy(bp, MII_BMCR, BMCR_LOOPBACK | BMCR_FULLDPLX |
			    BMCR_SPEED1000);
	spin_unlock_bh(&bp->phy_lock);
	if (rc)
		return rc;

	for (i = 0; i < 10; i++) {
		if (bnx2_test_link(bp) == 0)
			break;
		udelay(10);
	}

	mac_mode = REG_RD(bp, BNX2_EMAC_MODE);
	mac_mode &= ~(BNX2_EMAC_MODE_PORT | BNX2_EMAC_MODE_HALF_DUPLEX |
		      BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK |
		      BNX2_EMAC_MODE_25G);

	mac_mode |= BNX2_EMAC_MODE_PORT_GMII;
	REG_WR(bp, BNX2_EMAC_MODE, mac_mode);
	bp->link_up = 1;
	return 0;
}

static int
bnx2_fw_sync(struct bnx2 *bp, u32 msg_data, int silent)
{
	int i;
	u32 val;

	bp->fw_wr_seq++;
	msg_data |= bp->fw_wr_seq;

	REG_WR_IND(bp, bp->shmem_base + BNX2_DRV_MB, msg_data);

	/* wait for an acknowledgement. */
	for (i = 0; i < (FW_ACK_TIME_OUT_MS / 10); i++) {
		msleep(10);

		val = REG_RD_IND(bp, bp->shmem_base + BNX2_FW_MB);

		if ((val & BNX2_FW_MSG_ACK) == (msg_data & BNX2_DRV_MSG_SEQ))
			break;
	}
	if ((msg_data & BNX2_DRV_MSG_DATA) == BNX2_DRV_MSG_DATA_WAIT0)
		return 0;

	/* If we timed out, inform the firmware that this is the case. */
	if ((val & BNX2_FW_MSG_ACK) != (msg_data & BNX2_DRV_MSG_SEQ)) {
		if (!silent)
			printk(KERN_ERR PFX "fw sync timeout, reset code = "
					    "%x\n", msg_data);

		msg_data &= ~BNX2_DRV_MSG_CODE;
		msg_data |= BNX2_DRV_MSG_CODE_FW_TIMEOUT;

		REG_WR_IND(bp, bp->shmem_base + BNX2_DRV_MB, msg_data);

		return -EBUSY;
	}

	if ((val & BNX2_FW_MSG_STATUS_MASK) != BNX2_FW_MSG_STATUS_OK)
		return -EIO;

	return 0;
}

static void
bnx2_init_context(struct bnx2 *bp)
{
	u32 vcid;

	vcid = 96;
	while (vcid) {
		u32 vcid_addr, pcid_addr, offset;

		vcid--;

		if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
			u32 new_vcid;

			vcid_addr = GET_PCID_ADDR(vcid);
			if (vcid & 0x8) {
				new_vcid = 0x60 + (vcid & 0xf0) + (vcid & 0x7);
			}
			else {
				new_vcid = vcid;
			}
			pcid_addr = GET_PCID_ADDR(new_vcid);
		}
		else {
	    		vcid_addr = GET_CID_ADDR(vcid);
			pcid_addr = vcid_addr;
		}

		REG_WR(bp, BNX2_CTX_VIRT_ADDR, 0x00);
		REG_WR(bp, BNX2_CTX_PAGE_TBL, pcid_addr);

		/* Zero out the context. */
		for (offset = 0; offset < PHY_CTX_SIZE; offset += 4) {
			CTX_WR(bp, 0x00, offset, 0);
		}

		REG_WR(bp, BNX2_CTX_VIRT_ADDR, vcid_addr);
		REG_WR(bp, BNX2_CTX_PAGE_TBL, pcid_addr);
	}
}

static int
bnx2_alloc_bad_rbuf(struct bnx2 *bp)
{
	u16 *good_mbuf;
	u32 good_mbuf_cnt;
	u32 val;

	good_mbuf = kmalloc(512 * sizeof(u16), GFP_KERNEL);
	if (good_mbuf == NULL) {
		printk(KERN_ERR PFX "Failed to allocate memory in "
				    "bnx2_alloc_bad_rbuf\n");
		return -ENOMEM;
	}

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
		BNX2_MISC_ENABLE_SET_BITS_RX_MBUF_ENABLE);

	good_mbuf_cnt = 0;

	/* Allocate a bunch of mbufs and save the good ones in an array. */
	val = REG_RD_IND(bp, BNX2_RBUF_STATUS1);
	while (val & BNX2_RBUF_STATUS1_FREE_COUNT) {
		REG_WR_IND(bp, BNX2_RBUF_COMMAND, BNX2_RBUF_COMMAND_ALLOC_REQ);

		val = REG_RD_IND(bp, BNX2_RBUF_FW_BUF_ALLOC);

		val &= BNX2_RBUF_FW_BUF_ALLOC_VALUE;

		/* The addresses with Bit 9 set are bad memory blocks. */
		if (!(val & (1 << 9))) {
			good_mbuf[good_mbuf_cnt] = (u16) val;
			good_mbuf_cnt++;
		}

		val = REG_RD_IND(bp, BNX2_RBUF_STATUS1);
	}

	/* Free the good ones back to the mbuf pool thus discarding
	 * all the bad ones. */
	while (good_mbuf_cnt) {
		good_mbuf_cnt--;

		val = good_mbuf[good_mbuf_cnt];
		val = (val << 9) | val | 1;

		REG_WR_IND(bp, BNX2_RBUF_FW_BUF_FREE, val);
	}
	kfree(good_mbuf);
	return 0;
}

static void
bnx2_set_mac_addr(struct bnx2 *bp) 
{
	u32 val;
	u8 *mac_addr = bp->dev->dev_addr;

	val = (mac_addr[0] << 8) | mac_addr[1];

	REG_WR(bp, BNX2_EMAC_MAC_MATCH0, val);

	val = (mac_addr[2] << 24) | (mac_addr[3] << 16) | 
		(mac_addr[4] << 8) | mac_addr[5];

	REG_WR(bp, BNX2_EMAC_MAC_MATCH1, val);
}

static inline int
bnx2_alloc_rx_skb(struct bnx2 *bp, u16 index)
{
	struct sk_buff *skb;
	struct sw_bd *rx_buf = &bp->rx_buf_ring[index];
	dma_addr_t mapping;
	struct rx_bd *rxbd = &bp->rx_desc_ring[RX_RING(index)][RX_IDX(index)];
	unsigned long align;

	skb = dev_alloc_skb(bp->rx_buf_size);
	if (skb == NULL) {
		return -ENOMEM;
	}

	if (unlikely((align = (unsigned long) skb->data & 0x7))) {
		skb_reserve(skb, 8 - align);
	}

	skb->dev = bp->dev;
	mapping = pci_map_single(bp->pdev, skb->data, bp->rx_buf_use_size,
		PCI_DMA_FROMDEVICE);

	rx_buf->skb = skb;
	pci_unmap_addr_set(rx_buf, mapping, mapping);

	rxbd->rx_bd_haddr_hi = (u64) mapping >> 32;
	rxbd->rx_bd_haddr_lo = (u64) mapping & 0xffffffff;

	bp->rx_prod_bseq += bp->rx_buf_use_size;

	return 0;
}

static void
bnx2_phy_int(struct bnx2 *bp)
{
	u32 new_link_state, old_link_state;

	new_link_state = bp->status_blk->status_attn_bits &
		STATUS_ATTN_BITS_LINK_STATE;
	old_link_state = bp->status_blk->status_attn_bits_ack &
		STATUS_ATTN_BITS_LINK_STATE;
	if (new_link_state != old_link_state) {
		if (new_link_state) {
			REG_WR(bp, BNX2_PCICFG_STATUS_BIT_SET_CMD,
				STATUS_ATTN_BITS_LINK_STATE);
		}
		else {
			REG_WR(bp, BNX2_PCICFG_STATUS_BIT_CLEAR_CMD,
				STATUS_ATTN_BITS_LINK_STATE);
		}
		bnx2_set_link(bp);
	}
}

static void
bnx2_tx_int(struct bnx2 *bp)
{
	struct status_block *sblk = bp->status_blk;
	u16 hw_cons, sw_cons, sw_ring_cons;
	int tx_free_bd = 0;

	hw_cons = bp->hw_tx_cons = sblk->status_tx_quick_consumer_index0;
	if ((hw_cons & MAX_TX_DESC_CNT) == MAX_TX_DESC_CNT) {
		hw_cons++;
	}
	sw_cons = bp->tx_cons;

	while (sw_cons != hw_cons) {
		struct sw_bd *tx_buf;
		struct sk_buff *skb;
		int i, last;

		sw_ring_cons = TX_RING_IDX(sw_cons);

		tx_buf = &bp->tx_buf_ring[sw_ring_cons];
		skb = tx_buf->skb;
#ifdef BCM_TSO 
		/* partial BD completions possible with TSO packets */
		if (skb_shinfo(skb)->tso_size) {
			u16 last_idx, last_ring_idx;

			last_idx = sw_cons +
				skb_shinfo(skb)->nr_frags + 1;
			last_ring_idx = sw_ring_cons +
				skb_shinfo(skb)->nr_frags + 1;
			if (unlikely(last_ring_idx >= MAX_TX_DESC_CNT)) {
				last_idx++;
			}
			if (((s16) ((s16) last_idx - (s16) hw_cons)) > 0) {
				break;
			}
		}
#endif
		pci_unmap_single(bp->pdev, pci_unmap_addr(tx_buf, mapping),
			skb_headlen(skb), PCI_DMA_TODEVICE);

		tx_buf->skb = NULL;
		last = skb_shinfo(skb)->nr_frags;

		for (i = 0; i < last; i++) {
			sw_cons = NEXT_TX_BD(sw_cons);

			pci_unmap_page(bp->pdev,
				pci_unmap_addr(
					&bp->tx_buf_ring[TX_RING_IDX(sw_cons)],
				       	mapping),
				skb_shinfo(skb)->frags[i].size,
				PCI_DMA_TODEVICE);
		}

		sw_cons = NEXT_TX_BD(sw_cons);

		tx_free_bd += last + 1;

		dev_kfree_skb_irq(skb);

		hw_cons = bp->hw_tx_cons =
			sblk->status_tx_quick_consumer_index0;

		if ((hw_cons & MAX_TX_DESC_CNT) == MAX_TX_DESC_CNT) {
			hw_cons++;
		}
	}

	bp->tx_cons = sw_cons;

	if (unlikely(netif_queue_stopped(bp->dev))) {
		spin_lock(&bp->tx_lock);
		if ((netif_queue_stopped(bp->dev)) &&
		    (bnx2_tx_avail(bp) > MAX_SKB_FRAGS)) {

			netif_wake_queue(bp->dev);
		}
		spin_unlock(&bp->tx_lock);
	}
}

static inline void
bnx2_reuse_rx_skb(struct bnx2 *bp, struct sk_buff *skb,
	u16 cons, u16 prod)
{
	struct sw_bd *cons_rx_buf, *prod_rx_buf;
	struct rx_bd *cons_bd, *prod_bd;

	cons_rx_buf = &bp->rx_buf_ring[cons];
	prod_rx_buf = &bp->rx_buf_ring[prod];

	pci_dma_sync_single_for_device(bp->pdev,
		pci_unmap_addr(cons_rx_buf, mapping),
		bp->rx_offset + RX_COPY_THRESH, PCI_DMA_FROMDEVICE);

	bp->rx_prod_bseq += bp->rx_buf_use_size;

	prod_rx_buf->skb = skb;

	if (cons == prod)
		return;

	pci_unmap_addr_set(prod_rx_buf, mapping,
			pci_unmap_addr(cons_rx_buf, mapping));

	cons_bd = &bp->rx_desc_ring[RX_RING(cons)][RX_IDX(cons)];
	prod_bd = &bp->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	prod_bd->rx_bd_haddr_hi = cons_bd->rx_bd_haddr_hi;
	prod_bd->rx_bd_haddr_lo = cons_bd->rx_bd_haddr_lo;
}

static int
bnx2_rx_int(struct bnx2 *bp, int budget)
{
	struct status_block *sblk = bp->status_blk;
	u16 hw_cons, sw_cons, sw_ring_cons, sw_prod, sw_ring_prod;
	struct l2_fhdr *rx_hdr;
	int rx_pkt = 0;

	hw_cons = bp->hw_rx_cons = sblk->status_rx_quick_consumer_index0;
	if ((hw_cons & MAX_RX_DESC_CNT) == MAX_RX_DESC_CNT) {
		hw_cons++;
	}
	sw_cons = bp->rx_cons;
	sw_prod = bp->rx_prod;

	/* Memory barrier necessary as speculative reads of the rx
	 * buffer can be ahead of the index in the status block
	 */
	rmb();
	while (sw_cons != hw_cons) {
		unsigned int len;
		u32 status;
		struct sw_bd *rx_buf;
		struct sk_buff *skb;
		dma_addr_t dma_addr;

		sw_ring_cons = RX_RING_IDX(sw_cons);
		sw_ring_prod = RX_RING_IDX(sw_prod);

		rx_buf = &bp->rx_buf_ring[sw_ring_cons];
		skb = rx_buf->skb;

		rx_buf->skb = NULL;

		dma_addr = pci_unmap_addr(rx_buf, mapping);

		pci_dma_sync_single_for_cpu(bp->pdev, dma_addr,
			bp->rx_offset + RX_COPY_THRESH, PCI_DMA_FROMDEVICE);

		rx_hdr = (struct l2_fhdr *) skb->data;
		len = rx_hdr->l2_fhdr_pkt_len - 4;

		if ((status = rx_hdr->l2_fhdr_status) &
			(L2_FHDR_ERRORS_BAD_CRC |
			L2_FHDR_ERRORS_PHY_DECODE |
			L2_FHDR_ERRORS_ALIGNMENT |
			L2_FHDR_ERRORS_TOO_SHORT |
			L2_FHDR_ERRORS_GIANT_FRAME)) {

			goto reuse_rx;
		}

		/* Since we don't have a jumbo ring, copy small packets
		 * if mtu > 1500
		 */
		if ((bp->dev->mtu > 1500) && (len <= RX_COPY_THRESH)) {
			struct sk_buff *new_skb;

			new_skb = dev_alloc_skb(len + 2);
			if (new_skb == NULL)
				goto reuse_rx;

			/* aligned copy */
			memcpy(new_skb->data,
				skb->data + bp->rx_offset - 2,
				len + 2);

			skb_reserve(new_skb, 2);
			skb_put(new_skb, len);
			new_skb->dev = bp->dev;

			bnx2_reuse_rx_skb(bp, skb,
				sw_ring_cons, sw_ring_prod);

			skb = new_skb;
		}
		else if (bnx2_alloc_rx_skb(bp, sw_ring_prod) == 0) {
			pci_unmap_single(bp->pdev, dma_addr,
				bp->rx_buf_use_size, PCI_DMA_FROMDEVICE);

			skb_reserve(skb, bp->rx_offset);
			skb_put(skb, len);
		}
		else {
reuse_rx:
			bnx2_reuse_rx_skb(bp, skb,
				sw_ring_cons, sw_ring_prod);
			goto next_rx;
		}

		skb->protocol = eth_type_trans(skb, bp->dev);

		if ((len > (bp->dev->mtu + ETH_HLEN)) &&
			(htons(skb->protocol) != 0x8100)) {

			dev_kfree_skb_irq(skb);
			goto next_rx;

		}

		skb->ip_summed = CHECKSUM_NONE;
		if (bp->rx_csum &&
			(status & (L2_FHDR_STATUS_TCP_SEGMENT |
			L2_FHDR_STATUS_UDP_DATAGRAM))) {

			if (likely((status & (L2_FHDR_ERRORS_TCP_XSUM |
					      L2_FHDR_ERRORS_UDP_XSUM)) == 0))
				skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

#ifdef BCM_VLAN
		if ((status & L2_FHDR_STATUS_L2_VLAN_TAG) && (bp->vlgrp != 0)) {
			vlan_hwaccel_receive_skb(skb, bp->vlgrp,
				rx_hdr->l2_fhdr_vlan_tag);
		}
		else
#endif
			netif_receive_skb(skb);

		bp->dev->last_rx = jiffies;
		rx_pkt++;

next_rx:
		sw_cons = NEXT_RX_BD(sw_cons);
		sw_prod = NEXT_RX_BD(sw_prod);

		if ((rx_pkt == budget))
			break;

		/* Refresh hw_cons to see if there is new work */
		if (sw_cons == hw_cons) {
			hw_cons = bp->hw_rx_cons =
				sblk->status_rx_quick_consumer_index0;
			if ((hw_cons & MAX_RX_DESC_CNT) == MAX_RX_DESC_CNT)
				hw_cons++;
			rmb();
		}
	}
	bp->rx_cons = sw_cons;
	bp->rx_prod = sw_prod;

	REG_WR16(bp, MB_RX_CID_ADDR + BNX2_L2CTX_HOST_BDIDX, sw_prod);

	REG_WR(bp, MB_RX_CID_ADDR + BNX2_L2CTX_HOST_BSEQ, bp->rx_prod_bseq);

	mmiowb();

	return rx_pkt;

}

/* MSI ISR - The only difference between this and the INTx ISR
 * is that the MSI interrupt is always serviced.
 */
static irqreturn_t
bnx2_msi(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = dev_instance;
	struct bnx2 *bp = netdev_priv(dev);

	prefetch(bp->status_blk);
	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
		BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	/* Return here if interrupt is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	netif_rx_schedule(dev);

	return IRQ_HANDLED;
}

static irqreturn_t
bnx2_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = dev_instance;
	struct bnx2 *bp = netdev_priv(dev);

	/* When using INTx, it is possible for the interrupt to arrive
	 * at the CPU before the status block posted prior to the
	 * interrupt. Reading a register will flush the status block.
	 * When using MSI, the MSI message will always complete after
	 * the status block write.
	 */
	if ((bp->status_blk->status_idx == bp->last_status_idx) &&
	    (REG_RD(bp, BNX2_PCICFG_MISC_STATUS) &
	     BNX2_PCICFG_MISC_STATUS_INTA_VALUE))
		return IRQ_NONE;

	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
		BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	/* Return here if interrupt is shared and is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	netif_rx_schedule(dev);

	return IRQ_HANDLED;
}

static inline int
bnx2_has_work(struct bnx2 *bp)
{
	struct status_block *sblk = bp->status_blk;

	if ((sblk->status_rx_quick_consumer_index0 != bp->hw_rx_cons) ||
	    (sblk->status_tx_quick_consumer_index0 != bp->hw_tx_cons))
		return 1;

	if (((sblk->status_attn_bits & STATUS_ATTN_BITS_LINK_STATE) != 0) !=
	    bp->link_up)
		return 1;

	return 0;
}

static int
bnx2_poll(struct net_device *dev, int *budget)
{
	struct bnx2 *bp = netdev_priv(dev);

	if ((bp->status_blk->status_attn_bits &
		STATUS_ATTN_BITS_LINK_STATE) !=
		(bp->status_blk->status_attn_bits_ack &
		STATUS_ATTN_BITS_LINK_STATE)) {

		spin_lock(&bp->phy_lock);
		bnx2_phy_int(bp);
		spin_unlock(&bp->phy_lock);
	}

	if (bp->status_blk->status_tx_quick_consumer_index0 != bp->hw_tx_cons)
		bnx2_tx_int(bp);

	if (bp->status_blk->status_rx_quick_consumer_index0 != bp->hw_rx_cons) {
		int orig_budget = *budget;
		int work_done;

		if (orig_budget > dev->quota)
			orig_budget = dev->quota;
		
		work_done = bnx2_rx_int(bp, orig_budget);
		*budget -= work_done;
		dev->quota -= work_done;
	}
	
	bp->last_status_idx = bp->status_blk->status_idx;
	rmb();

	if (!bnx2_has_work(bp)) {
		netif_rx_complete(dev);
		if (likely(bp->flags & USING_MSI_FLAG)) {
			REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
			       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
			       bp->last_status_idx);
			return 0;
		}
		REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       BNX2_PCICFG_INT_ACK_CMD_MASK_INT |
		       bp->last_status_idx);

		REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       bp->last_status_idx);
		return 0;
	}

	return 1;
}

/* Called with rtnl_lock from vlan functions and also dev->xmit_lock
 * from set_multicast.
 */
static void
bnx2_set_rx_mode(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	u32 rx_mode, sort_mode;
	int i;

	spin_lock_bh(&bp->phy_lock);

	rx_mode = bp->rx_mode & ~(BNX2_EMAC_RX_MODE_PROMISCUOUS |
				  BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG);
	sort_mode = 1 | BNX2_RPM_SORT_USER0_BC_EN;
#ifdef BCM_VLAN
	if (!bp->vlgrp && !(bp->flags & ASF_ENABLE_FLAG))
		rx_mode |= BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG;
#else
	if (!(bp->flags & ASF_ENABLE_FLAG))
		rx_mode |= BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG;
#endif
	if (dev->flags & IFF_PROMISC) {
		/* Promiscuous mode. */
		rx_mode |= BNX2_EMAC_RX_MODE_PROMISCUOUS;
		sort_mode |= BNX2_RPM_SORT_USER0_PROM_EN;
	}
	else if (dev->flags & IFF_ALLMULTI) {
		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
			       0xffffffff);
        	}
		sort_mode |= BNX2_RPM_SORT_USER0_MC_EN;
	}
	else {
		/* Accept one or more multicast(s). */
		struct dev_mc_list *mclist;
		u32 mc_filter[NUM_MC_HASH_REGISTERS];
		u32 regidx;
		u32 bit;
		u32 crc;

		memset(mc_filter, 0, 4 * NUM_MC_HASH_REGISTERS);

		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {

			crc = ether_crc_le(ETH_ALEN, mclist->dmi_addr);
			bit = crc & 0xff;
			regidx = (bit & 0xe0) >> 5;
			bit &= 0x1f;
			mc_filter[regidx] |= (1 << bit);
		}

		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
			       mc_filter[i]);
		}

		sort_mode |= BNX2_RPM_SORT_USER0_MC_HSH_EN;
	}

	if (rx_mode != bp->rx_mode) {
		bp->rx_mode = rx_mode;
		REG_WR(bp, BNX2_EMAC_RX_MODE, rx_mode);
	}

	REG_WR(bp, BNX2_RPM_SORT_USER0, 0x0);
	REG_WR(bp, BNX2_RPM_SORT_USER0, sort_mode);
	REG_WR(bp, BNX2_RPM_SORT_USER0, sort_mode | BNX2_RPM_SORT_USER0_ENA);

	spin_unlock_bh(&bp->phy_lock);
}

static void
load_rv2p_fw(struct bnx2 *bp, u32 *rv2p_code, u32 rv2p_code_len,
	u32 rv2p_proc)
{
	int i;
	u32 val;


	for (i = 0; i < rv2p_code_len; i += 8) {
		REG_WR(bp, BNX2_RV2P_INSTR_HIGH, *rv2p_code);
		rv2p_code++;
		REG_WR(bp, BNX2_RV2P_INSTR_LOW, *rv2p_code);
		rv2p_code++;

		if (rv2p_proc == RV2P_PROC1) {
			val = (i / 8) | BNX2_RV2P_PROC1_ADDR_CMD_RDWR;
			REG_WR(bp, BNX2_RV2P_PROC1_ADDR_CMD, val);
		}
		else {
			val = (i / 8) | BNX2_RV2P_PROC2_ADDR_CMD_RDWR;
			REG_WR(bp, BNX2_RV2P_PROC2_ADDR_CMD, val);
		}
	}

	/* Reset the processor, un-stall is done later. */
	if (rv2p_proc == RV2P_PROC1) {
		REG_WR(bp, BNX2_RV2P_COMMAND, BNX2_RV2P_COMMAND_PROC1_RESET);
	}
	else {
		REG_WR(bp, BNX2_RV2P_COMMAND, BNX2_RV2P_COMMAND_PROC2_RESET);
	}
}

static void
load_cpu_fw(struct bnx2 *bp, struct cpu_reg *cpu_reg, struct fw_info *fw)
{
	u32 offset;
	u32 val;

	/* Halt the CPU. */
	val = REG_RD_IND(bp, cpu_reg->mode);
	val |= cpu_reg->mode_value_halt;
	REG_WR_IND(bp, cpu_reg->mode, val);
	REG_WR_IND(bp, cpu_reg->state, cpu_reg->state_value_clear);

	/* Load the Text area. */
	offset = cpu_reg->spad_base + (fw->text_addr - cpu_reg->mips_view_base);
	if (fw->text) {
		int j;

		for (j = 0; j < (fw->text_len / 4); j++, offset += 4) {
			REG_WR_IND(bp, offset, fw->text[j]);
	        }
	}

	/* Load the Data area. */
	offset = cpu_reg->spad_base + (fw->data_addr - cpu_reg->mips_view_base);
	if (fw->data) {
		int j;

		for (j = 0; j < (fw->data_len / 4); j++, offset += 4) {
			REG_WR_IND(bp, offset, fw->data[j]);
		}
	}

	/* Load the SBSS area. */
	offset = cpu_reg->spad_base + (fw->sbss_addr - cpu_reg->mips_view_base);
	if (fw->sbss) {
		int j;

		for (j = 0; j < (fw->sbss_len / 4); j++, offset += 4) {
			REG_WR_IND(bp, offset, fw->sbss[j]);
		}
	}

	/* Load the BSS area. */
	offset = cpu_reg->spad_base + (fw->bss_addr - cpu_reg->mips_view_base);
	if (fw->bss) {
		int j;

		for (j = 0; j < (fw->bss_len/4); j++, offset += 4) {
			REG_WR_IND(bp, offset, fw->bss[j]);
		}
	}

	/* Load the Read-Only area. */
	offset = cpu_reg->spad_base +
		(fw->rodata_addr - cpu_reg->mips_view_base);
	if (fw->rodata) {
		int j;

		for (j = 0; j < (fw->rodata_len / 4); j++, offset += 4) {
			REG_WR_IND(bp, offset, fw->rodata[j]);
		}
	}

	/* Clear the pre-fetch instruction. */
	REG_WR_IND(bp, cpu_reg->inst, 0);
	REG_WR_IND(bp, cpu_reg->pc, fw->start_addr);

	/* Start the CPU. */
	val = REG_RD_IND(bp, cpu_reg->mode);
	val &= ~cpu_reg->mode_value_halt;
	REG_WR_IND(bp, cpu_reg->state, cpu_reg->state_value_clear);
	REG_WR_IND(bp, cpu_reg->mode, val);
}

static void
bnx2_init_cpus(struct bnx2 *bp)
{
	struct cpu_reg cpu_reg;
	struct fw_info fw;

	/* Initialize the RV2P processor. */
	load_rv2p_fw(bp, bnx2_rv2p_proc1, sizeof(bnx2_rv2p_proc1), RV2P_PROC1);
	load_rv2p_fw(bp, bnx2_rv2p_proc2, sizeof(bnx2_rv2p_proc2), RV2P_PROC2);

	/* Initialize the RX Processor. */
	cpu_reg.mode = BNX2_RXP_CPU_MODE;
	cpu_reg.mode_value_halt = BNX2_RXP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BNX2_RXP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BNX2_RXP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BNX2_RXP_CPU_REG_FILE;
	cpu_reg.evmask = BNX2_RXP_CPU_EVENT_MASK;
	cpu_reg.pc = BNX2_RXP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BNX2_RXP_CPU_INSTRUCTION;
	cpu_reg.bp = BNX2_RXP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BNX2_RXP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;
    
	fw.ver_major = bnx2_RXP_b06FwReleaseMajor;
	fw.ver_minor = bnx2_RXP_b06FwReleaseMinor;
	fw.ver_fix = bnx2_RXP_b06FwReleaseFix;
	fw.start_addr = bnx2_RXP_b06FwStartAddr;

	fw.text_addr = bnx2_RXP_b06FwTextAddr;
	fw.text_len = bnx2_RXP_b06FwTextLen;
	fw.text_index = 0;
	fw.text = bnx2_RXP_b06FwText;

	fw.data_addr = bnx2_RXP_b06FwDataAddr;
	fw.data_len = bnx2_RXP_b06FwDataLen;
	fw.data_index = 0;
	fw.data = bnx2_RXP_b06FwData;

	fw.sbss_addr = bnx2_RXP_b06FwSbssAddr;
	fw.sbss_len = bnx2_RXP_b06FwSbssLen;
	fw.sbss_index = 0;
	fw.sbss = bnx2_RXP_b06FwSbss;

	fw.bss_addr = bnx2_RXP_b06FwBssAddr;
	fw.bss_len = bnx2_RXP_b06FwBssLen;
	fw.bss_index = 0;
	fw.bss = bnx2_RXP_b06FwBss;

	fw.rodata_addr = bnx2_RXP_b06FwRodataAddr;
	fw.rodata_len = bnx2_RXP_b06FwRodataLen;
	fw.rodata_index = 0;
	fw.rodata = bnx2_RXP_b06FwRodata;

	load_cpu_fw(bp, &cpu_reg, &fw);

	/* Initialize the TX Processor. */
	cpu_reg.mode = BNX2_TXP_CPU_MODE;
	cpu_reg.mode_value_halt = BNX2_TXP_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BNX2_TXP_CPU_MODE_STEP_ENA;
	cpu_reg.state = BNX2_TXP_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BNX2_TXP_CPU_REG_FILE;
	cpu_reg.evmask = BNX2_TXP_CPU_EVENT_MASK;
	cpu_reg.pc = BNX2_TXP_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BNX2_TXP_CPU_INSTRUCTION;
	cpu_reg.bp = BNX2_TXP_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BNX2_TXP_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;
    
	fw.ver_major = bnx2_TXP_b06FwReleaseMajor;
	fw.ver_minor = bnx2_TXP_b06FwReleaseMinor;
	fw.ver_fix = bnx2_TXP_b06FwReleaseFix;
	fw.start_addr = bnx2_TXP_b06FwStartAddr;

	fw.text_addr = bnx2_TXP_b06FwTextAddr;
	fw.text_len = bnx2_TXP_b06FwTextLen;
	fw.text_index = 0;
	fw.text = bnx2_TXP_b06FwText;

	fw.data_addr = bnx2_TXP_b06FwDataAddr;
	fw.data_len = bnx2_TXP_b06FwDataLen;
	fw.data_index = 0;
	fw.data = bnx2_TXP_b06FwData;

	fw.sbss_addr = bnx2_TXP_b06FwSbssAddr;
	fw.sbss_len = bnx2_TXP_b06FwSbssLen;
	fw.sbss_index = 0;
	fw.sbss = bnx2_TXP_b06FwSbss;

	fw.bss_addr = bnx2_TXP_b06FwBssAddr;
	fw.bss_len = bnx2_TXP_b06FwBssLen;
	fw.bss_index = 0;
	fw.bss = bnx2_TXP_b06FwBss;

	fw.rodata_addr = bnx2_TXP_b06FwRodataAddr;
	fw.rodata_len = bnx2_TXP_b06FwRodataLen;
	fw.rodata_index = 0;
	fw.rodata = bnx2_TXP_b06FwRodata;

	load_cpu_fw(bp, &cpu_reg, &fw);

	/* Initialize the TX Patch-up Processor. */
	cpu_reg.mode = BNX2_TPAT_CPU_MODE;
	cpu_reg.mode_value_halt = BNX2_TPAT_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BNX2_TPAT_CPU_MODE_STEP_ENA;
	cpu_reg.state = BNX2_TPAT_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BNX2_TPAT_CPU_REG_FILE;
	cpu_reg.evmask = BNX2_TPAT_CPU_EVENT_MASK;
	cpu_reg.pc = BNX2_TPAT_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BNX2_TPAT_CPU_INSTRUCTION;
	cpu_reg.bp = BNX2_TPAT_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BNX2_TPAT_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;
    
	fw.ver_major = bnx2_TPAT_b06FwReleaseMajor;
	fw.ver_minor = bnx2_TPAT_b06FwReleaseMinor;
	fw.ver_fix = bnx2_TPAT_b06FwReleaseFix;
	fw.start_addr = bnx2_TPAT_b06FwStartAddr;

	fw.text_addr = bnx2_TPAT_b06FwTextAddr;
	fw.text_len = bnx2_TPAT_b06FwTextLen;
	fw.text_index = 0;
	fw.text = bnx2_TPAT_b06FwText;

	fw.data_addr = bnx2_TPAT_b06FwDataAddr;
	fw.data_len = bnx2_TPAT_b06FwDataLen;
	fw.data_index = 0;
	fw.data = bnx2_TPAT_b06FwData;

	fw.sbss_addr = bnx2_TPAT_b06FwSbssAddr;
	fw.sbss_len = bnx2_TPAT_b06FwSbssLen;
	fw.sbss_index = 0;
	fw.sbss = bnx2_TPAT_b06FwSbss;

	fw.bss_addr = bnx2_TPAT_b06FwBssAddr;
	fw.bss_len = bnx2_TPAT_b06FwBssLen;
	fw.bss_index = 0;
	fw.bss = bnx2_TPAT_b06FwBss;

	fw.rodata_addr = bnx2_TPAT_b06FwRodataAddr;
	fw.rodata_len = bnx2_TPAT_b06FwRodataLen;
	fw.rodata_index = 0;
	fw.rodata = bnx2_TPAT_b06FwRodata;

	load_cpu_fw(bp, &cpu_reg, &fw);

	/* Initialize the Completion Processor. */
	cpu_reg.mode = BNX2_COM_CPU_MODE;
	cpu_reg.mode_value_halt = BNX2_COM_CPU_MODE_SOFT_HALT;
	cpu_reg.mode_value_sstep = BNX2_COM_CPU_MODE_STEP_ENA;
	cpu_reg.state = BNX2_COM_CPU_STATE;
	cpu_reg.state_value_clear = 0xffffff;
	cpu_reg.gpr0 = BNX2_COM_CPU_REG_FILE;
	cpu_reg.evmask = BNX2_COM_CPU_EVENT_MASK;
	cpu_reg.pc = BNX2_COM_CPU_PROGRAM_COUNTER;
	cpu_reg.inst = BNX2_COM_CPU_INSTRUCTION;
	cpu_reg.bp = BNX2_COM_CPU_HW_BREAKPOINT;
	cpu_reg.spad_base = BNX2_COM_SCRATCH;
	cpu_reg.mips_view_base = 0x8000000;
    
	fw.ver_major = bnx2_COM_b06FwReleaseMajor;
	fw.ver_minor = bnx2_COM_b06FwReleaseMinor;
	fw.ver_fix = bnx2_COM_b06FwReleaseFix;
	fw.start_addr = bnx2_COM_b06FwStartAddr;

	fw.text_addr = bnx2_COM_b06FwTextAddr;
	fw.text_len = bnx2_COM_b06FwTextLen;
	fw.text_index = 0;
	fw.text = bnx2_COM_b06FwText;

	fw.data_addr = bnx2_COM_b06FwDataAddr;
	fw.data_len = bnx2_COM_b06FwDataLen;
	fw.data_index = 0;
	fw.data = bnx2_COM_b06FwData;

	fw.sbss_addr = bnx2_COM_b06FwSbssAddr;
	fw.sbss_len = bnx2_COM_b06FwSbssLen;
	fw.sbss_index = 0;
	fw.sbss = bnx2_COM_b06FwSbss;

	fw.bss_addr = bnx2_COM_b06FwBssAddr;
	fw.bss_len = bnx2_COM_b06FwBssLen;
	fw.bss_index = 0;
	fw.bss = bnx2_COM_b06FwBss;

	fw.rodata_addr = bnx2_COM_b06FwRodataAddr;
	fw.rodata_len = bnx2_COM_b06FwRodataLen;
	fw.rodata_index = 0;
	fw.rodata = bnx2_COM_b06FwRodata;

	load_cpu_fw(bp, &cpu_reg, &fw);

}

static int
bnx2_set_power_state(struct bnx2 *bp, pci_power_t state)
{
	u16 pmcsr;

	pci_read_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL, &pmcsr);

	switch (state) {
	case PCI_D0: {
		u32 val;

		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
			(pmcsr & ~PCI_PM_CTRL_STATE_MASK) |
			PCI_PM_CTRL_PME_STATUS);

		if (pmcsr & PCI_PM_CTRL_STATE_MASK)
			/* delay required during transition out of D3hot */
			msleep(20);

		val = REG_RD(bp, BNX2_EMAC_MODE);
		val |= BNX2_EMAC_MODE_MPKT_RCVD | BNX2_EMAC_MODE_ACPI_RCVD;
		val &= ~BNX2_EMAC_MODE_MPKT;
		REG_WR(bp, BNX2_EMAC_MODE, val);

		val = REG_RD(bp, BNX2_RPM_CONFIG);
		val &= ~BNX2_RPM_CONFIG_ACPI_ENA;
		REG_WR(bp, BNX2_RPM_CONFIG, val);
		break;
	}
	case PCI_D3hot: {
		int i;
		u32 val, wol_msg;

		if (bp->wol) {
			u32 advertising;
			u8 autoneg;

			autoneg = bp->autoneg;
			advertising = bp->advertising;

			bp->autoneg = AUTONEG_SPEED;
			bp->advertising = ADVERTISED_10baseT_Half |
				ADVERTISED_10baseT_Full |
				ADVERTISED_100baseT_Half |
				ADVERTISED_100baseT_Full |
				ADVERTISED_Autoneg;

			bnx2_setup_copper_phy(bp);

			bp->autoneg = autoneg;
			bp->advertising = advertising;

			bnx2_set_mac_addr(bp);

			val = REG_RD(bp, BNX2_EMAC_MODE);

			/* Enable port mode. */
			val &= ~BNX2_EMAC_MODE_PORT;
			val |= BNX2_EMAC_MODE_PORT_MII |
			       BNX2_EMAC_MODE_MPKT_RCVD |
			       BNX2_EMAC_MODE_ACPI_RCVD |
			       BNX2_EMAC_MODE_MPKT;

			REG_WR(bp, BNX2_EMAC_MODE, val);

			/* receive all multicast */
			for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
				REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
				       0xffffffff);
			}
			REG_WR(bp, BNX2_EMAC_RX_MODE,
			       BNX2_EMAC_RX_MODE_SORT_MODE);

			val = 1 | BNX2_RPM_SORT_USER0_BC_EN |
			      BNX2_RPM_SORT_USER0_MC_EN;
			REG_WR(bp, BNX2_RPM_SORT_USER0, 0x0);
			REG_WR(bp, BNX2_RPM_SORT_USER0, val);
			REG_WR(bp, BNX2_RPM_SORT_USER0, val |
			       BNX2_RPM_SORT_USER0_ENA);

			/* Need to enable EMAC and RPM for WOL. */
			REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
			       BNX2_MISC_ENABLE_SET_BITS_RX_PARSER_MAC_ENABLE |
			       BNX2_MISC_ENABLE_SET_BITS_TX_HEADER_Q_ENABLE |
			       BNX2_MISC_ENABLE_SET_BITS_EMAC_ENABLE);

			val = REG_RD(bp, BNX2_RPM_CONFIG);
			val &= ~BNX2_RPM_CONFIG_ACPI_ENA;
			REG_WR(bp, BNX2_RPM_CONFIG, val);

			wol_msg = BNX2_DRV_MSG_CODE_SUSPEND_WOL;
		}
		else {
			wol_msg = BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL;
		}

		if (!(bp->flags & NO_WOL_FLAG))
			bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT3 | wol_msg, 0);

		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
		    (CHIP_ID(bp) == CHIP_ID_5706_A1)) {

			if (bp->wol)
				pmcsr |= 3;
		}
		else {
			pmcsr |= 3;
		}
		if (bp->wol) {
			pmcsr |= PCI_PM_CTRL_PME_ENABLE;
		}
		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
				      pmcsr);

		/* No more memory access after this point until
		 * device is brought back to D0.
		 */
		udelay(50);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int
bnx2_acquire_nvram_lock(struct bnx2 *bp)
{
	u32 val;
	int j;

	/* Request access to the flash interface. */
	REG_WR(bp, BNX2_NVM_SW_ARB, BNX2_NVM_SW_ARB_ARB_REQ_SET2);
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(bp, BNX2_NVM_SW_ARB);
		if (val & BNX2_NVM_SW_ARB_ARB_ARB2)
			break;

		udelay(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_release_nvram_lock(struct bnx2 *bp)
{
	int j;
	u32 val;

	/* Relinquish nvram interface. */
	REG_WR(bp, BNX2_NVM_SW_ARB, BNX2_NVM_SW_ARB_ARB_REQ_CLR2);

	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(bp, BNX2_NVM_SW_ARB);
		if (!(val & BNX2_NVM_SW_ARB_ARB_ARB2))
			break;

		udelay(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}


static int
bnx2_enable_nvram_write(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, val | BNX2_MISC_CFG_NVM_WR_EN_PCI);

	if (!bp->flash_info->buffered) {
		int j;

		REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);
		REG_WR(bp, BNX2_NVM_COMMAND,
		       BNX2_NVM_COMMAND_WREN | BNX2_NVM_COMMAND_DOIT);

		for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
			udelay(5);

			val = REG_RD(bp, BNX2_NVM_COMMAND);
			if (val & BNX2_NVM_COMMAND_DONE)
				break;
		}

		if (j >= NVRAM_TIMEOUT_COUNT)
			return -EBUSY;
	}
	return 0;
}

static void
bnx2_disable_nvram_write(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, val & ~BNX2_MISC_CFG_NVM_WR_EN);
}


static void
bnx2_enable_nvram_access(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_NVM_ACCESS_ENABLE);
	/* Enable both bits, even on read. */
	REG_WR(bp, BNX2_NVM_ACCESS_ENABLE, 
	       val | BNX2_NVM_ACCESS_ENABLE_EN | BNX2_NVM_ACCESS_ENABLE_WR_EN);
}

static void
bnx2_disable_nvram_access(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_NVM_ACCESS_ENABLE);
	/* Disable both bits, even after read. */
	REG_WR(bp, BNX2_NVM_ACCESS_ENABLE, 
		val & ~(BNX2_NVM_ACCESS_ENABLE_EN |
			BNX2_NVM_ACCESS_ENABLE_WR_EN));
}

static int
bnx2_nvram_erase_page(struct bnx2 *bp, u32 offset)
{
	u32 cmd;
	int j;

	if (bp->flash_info->buffered)
		/* Buffered flash, no erase needed */
		return 0;

	/* Build an erase command */
	cmd = BNX2_NVM_COMMAND_ERASE | BNX2_NVM_COMMAND_WR |
	      BNX2_NVM_COMMAND_DOIT;

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	/* Address of the NVRAM to read from. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue an erase command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		u32 val;

		udelay(5);

		val = REG_RD(bp, BNX2_NVM_COMMAND);
		if (val & BNX2_NVM_COMMAND_DONE)
			break;
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_nvram_read_dword(struct bnx2 *bp, u32 offset, u8 *ret_val, u32 cmd_flags)
{
	u32 cmd;
	int j;

	/* Build the command word. */
	cmd = BNX2_NVM_COMMAND_DOIT | cmd_flags;

	/* Calculate an offset of a buffered flash. */
	if (bp->flash_info->buffered) {
		offset = ((offset / bp->flash_info->page_size) <<
			   bp->flash_info->page_bits) +
			  (offset % bp->flash_info->page_size);
	}

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	/* Address of the NVRAM to read from. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue a read command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		u32 val;

		udelay(5);

		val = REG_RD(bp, BNX2_NVM_COMMAND);
		if (val & BNX2_NVM_COMMAND_DONE) {
			val = REG_RD(bp, BNX2_NVM_READ);

			val = be32_to_cpu(val);
			memcpy(ret_val, &val, 4);
			break;
		}
	}
	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}


static int
bnx2_nvram_write_dword(struct bnx2 *bp, u32 offset, u8 *val, u32 cmd_flags)
{
	u32 cmd, val32;
	int j;

	/* Build the command word. */
	cmd = BNX2_NVM_COMMAND_DOIT | BNX2_NVM_COMMAND_WR | cmd_flags;

	/* Calculate an offset of a buffered flash. */
	if (bp->flash_info->buffered) {
		offset = ((offset / bp->flash_info->page_size) <<
			  bp->flash_info->page_bits) +
			 (offset % bp->flash_info->page_size);
	}

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	memcpy(&val32, val, 4);
	val32 = cpu_to_be32(val32);

	/* Write the data. */
	REG_WR(bp, BNX2_NVM_WRITE, val32);

	/* Address of the NVRAM to write to. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue the write command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		udelay(5);

		if (REG_RD(bp, BNX2_NVM_COMMAND) & BNX2_NVM_COMMAND_DONE)
			break;
	}
	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_init_nvram(struct bnx2 *bp)
{
	u32 val;
	int j, entry_count, rc;
	struct flash_spec *flash;

	/* Determine the selected interface. */
	val = REG_RD(bp, BNX2_NVM_CFG1);

	entry_count = sizeof(flash_table) / sizeof(struct flash_spec);

	rc = 0;
	if (val & 0x40000000) {

		/* Flash interface has been reconfigured */
		for (j = 0, flash = &flash_table[0]; j < entry_count;
		     j++, flash++) {
			if ((val & FLASH_BACKUP_STRAP_MASK) ==
			    (flash->config1 & FLASH_BACKUP_STRAP_MASK)) {
				bp->flash_info = flash;
				break;
			}
		}
	}
	else {
		u32 mask;
		/* Not yet been reconfigured */

		if (val & (1 << 23))
			mask = FLASH_BACKUP_STRAP_MASK;
		else
			mask = FLASH_STRAP_MASK;

		for (j = 0, flash = &flash_table[0]; j < entry_count;
			j++, flash++) {

			if ((val & mask) == (flash->strapping & mask)) {
				bp->flash_info = flash;

				/* Request access to the flash interface. */
				if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
					return rc;

				/* Enable access to flash interface */
				bnx2_enable_nvram_access(bp);

				/* Reconfigure the flash interface */
				REG_WR(bp, BNX2_NVM_CFG1, flash->config1);
				REG_WR(bp, BNX2_NVM_CFG2, flash->config2);
				REG_WR(bp, BNX2_NVM_CFG3, flash->config3);
				REG_WR(bp, BNX2_NVM_WRITE1, flash->write1);

				/* Disable access to flash interface */
				bnx2_disable_nvram_access(bp);
				bnx2_release_nvram_lock(bp);

				break;
			}
		}
	} /* if (val & 0x40000000) */

	if (j == entry_count) {
		bp->flash_info = NULL;
		printk(KERN_ALERT PFX "Unknown flash/EEPROM type.\n");
		return -ENODEV;
	}

	val = REG_RD_IND(bp, bp->shmem_base + BNX2_SHARED_HW_CFG_CONFIG2);
	val &= BNX2_SHARED_HW_CFG2_NVM_SIZE_MASK;
	if (val)
		bp->flash_size = val;
	else
		bp->flash_size = bp->flash_info->total_size;

	return rc;
}

static int
bnx2_nvram_read(struct bnx2 *bp, u32 offset, u8 *ret_buf,
		int buf_size)
{
	int rc = 0;
	u32 cmd_flags, offset32, len32, extra;

	if (buf_size == 0)
		return 0;

	/* Request access to the flash interface. */
	if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
		return rc;

	/* Enable access to flash interface */
	bnx2_enable_nvram_access(bp);

	len32 = buf_size;
	offset32 = offset;
	extra = 0;

	cmd_flags = 0;

	if (offset32 & 3) {
		u8 buf[4];
		u32 pre_len;

		offset32 &= ~3;
		pre_len = 4 - (offset & 3);

		if (pre_len >= len32) {
			pre_len = len32;
			cmd_flags = BNX2_NVM_COMMAND_FIRST |
				    BNX2_NVM_COMMAND_LAST;
		}
		else {
			cmd_flags = BNX2_NVM_COMMAND_FIRST;
		}

		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		if (rc)
			return rc;

		memcpy(ret_buf, buf + (offset & 3), pre_len);

		offset32 += 4;
		ret_buf += pre_len;
		len32 -= pre_len;
	}
	if (len32 & 3) {
		extra = 4 - (len32 & 3);
		len32 = (len32 + 4) & ~3;
	}

	if (len32 == 4) {
		u8 buf[4];

		if (cmd_flags)
			cmd_flags = BNX2_NVM_COMMAND_LAST;
		else
			cmd_flags = BNX2_NVM_COMMAND_FIRST |
				    BNX2_NVM_COMMAND_LAST;

		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	}
	else if (len32 > 0) {
		u8 buf[4];

		/* Read the first word. */
		if (cmd_flags)
			cmd_flags = 0;
		else
			cmd_flags = BNX2_NVM_COMMAND_FIRST;

		rc = bnx2_nvram_read_dword(bp, offset32, ret_buf, cmd_flags);

		/* Advance to the next dword. */
		offset32 += 4;
		ret_buf += 4;
		len32 -= 4;

		while (len32 > 4 && rc == 0) {
			rc = bnx2_nvram_read_dword(bp, offset32, ret_buf, 0);

			/* Advance to the next dword. */
			offset32 += 4;
			ret_buf += 4;
			len32 -= 4;
		}

		if (rc)
			return rc;

		cmd_flags = BNX2_NVM_COMMAND_LAST;
		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	}

	/* Disable access to flash interface */
	bnx2_disable_nvram_access(bp);

	bnx2_release_nvram_lock(bp);

	return rc;
}

static int
bnx2_nvram_write(struct bnx2 *bp, u32 offset, u8 *data_buf,
		int buf_size)
{
	u32 written, offset32, len32;
	u8 *buf, start[4], end[4];
	int rc = 0;
	int align_start, align_end;

	buf = data_buf;
	offset32 = offset;
	len32 = buf_size;
	align_start = align_end = 0;

	if ((align_start = (offset32 & 3))) {
		offset32 &= ~3;
		len32 += align_start;
		if ((rc = bnx2_nvram_read(bp, offset32, start, 4)))
			return rc;
	}

	if (len32 & 3) {
	       	if ((len32 > 4) || !align_start) {
			align_end = 4 - (len32 & 3);
			len32 += align_end;
			if ((rc = bnx2_nvram_read(bp, offset32 + len32 - 4,
				end, 4))) {
				return rc;
			}
		}
	}

	if (align_start || align_end) {
		buf = kmalloc(len32, GFP_KERNEL);
		if (buf == 0)
			return -ENOMEM;
		if (align_start) {
			memcpy(buf, start, 4);
		}
		if (align_end) {
			memcpy(buf + len32 - 4, end, 4);
		}
		memcpy(buf + align_start, data_buf, buf_size);
	}

	written = 0;
	while ((written < len32) && (rc == 0)) {
		u32 page_start, page_end, data_start, data_end;
		u32 addr, cmd_flags;
		int i;
		u8 flash_buffer[264];

	        /* Find the page_start addr */
		page_start = offset32 + written;
		page_start -= (page_start % bp->flash_info->page_size);
		/* Find the page_end addr */
		page_end = page_start + bp->flash_info->page_size;
		/* Find the data_start addr */
		data_start = (written == 0) ? offset32 : page_start;
		/* Find the data_end addr */
		data_end = (page_end > offset32 + len32) ? 
			(offset32 + len32) : page_end;

		/* Request access to the flash interface. */
		if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
			goto nvram_write_end;

		/* Enable access to flash interface */
		bnx2_enable_nvram_access(bp);

		cmd_flags = BNX2_NVM_COMMAND_FIRST;
		if (bp->flash_info->buffered == 0) {
			int j;

			/* Read the whole page into the buffer
			 * (non-buffer flash only) */
			for (j = 0; j < bp->flash_info->page_size; j += 4) {
				if (j == (bp->flash_info->page_size - 4)) {
					cmd_flags |= BNX2_NVM_COMMAND_LAST;
				}
				rc = bnx2_nvram_read_dword(bp,
					page_start + j, 
					&flash_buffer[j], 
					cmd_flags);

				if (rc)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Enable writes to flash interface (unlock write-protect) */
		if ((rc = bnx2_enable_nvram_write(bp)) != 0)
			goto nvram_write_end;

		/* Erase the page */
		if ((rc = bnx2_nvram_erase_page(bp, page_start)) != 0)
			goto nvram_write_end;

		/* Re-enable the write again for the actual write */
		bnx2_enable_nvram_write(bp);

		/* Loop to write back the buffer data from page_start to
		 * data_start */
		i = 0;
		if (bp->flash_info->buffered == 0) {
			for (addr = page_start; addr < data_start;
				addr += 4, i += 4) {
				
				rc = bnx2_nvram_write_dword(bp, addr,
					&flash_buffer[i], cmd_flags);

				if (rc != 0)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Loop to write the new data from data_start to data_end */
		for (addr = data_start; addr < data_end; addr += 4, i++) {
			if ((addr == page_end - 4) ||
				((bp->flash_info->buffered) &&
				 (addr == data_end - 4))) {

				cmd_flags |= BNX2_NVM_COMMAND_LAST;
			}
			rc = bnx2_nvram_write_dword(bp, addr, buf,
				cmd_flags);

			if (rc != 0)
				goto nvram_write_end;

			cmd_flags = 0;
			buf += 4;
		}

		/* Loop to write back the buffer data from data_end
		 * to page_end */
		if (bp->flash_info->buffered == 0) {
			for (addr = data_end; addr < page_end;
				addr += 4, i += 4) {
			
				if (addr == page_end-4) {
					cmd_flags = BNX2_NVM_COMMAND_LAST;
                		}
				rc = bnx2_nvram_write_dword(bp, addr,
					&flash_buffer[i], cmd_flags);

				if (rc != 0)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Disable writes to flash interface (lock write-protect) */
		bnx2_disable_nvram_write(bp);

		/* Disable access to flash interface */
		bnx2_disable_nvram_access(bp);
		bnx2_release_nvram_lock(bp);

		/* Increment written */
		written += data_end - data_start;
	}

nvram_write_end:
	if (align_start || align_end)
		kfree(buf);
	return rc;
}

static int
bnx2_reset_chip(struct bnx2 *bp, u32 reset_code)
{
	u32 val;
	int i, rc = 0;

	/* Wait for the current PCI transaction to complete before
	 * issuing a reset. */
	REG_WR(bp, BNX2_MISC_ENABLE_CLR_BITS,
	       BNX2_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE);
	val = REG_RD(bp, BNX2_MISC_ENABLE_CLR_BITS);
	udelay(5);

	/* Wait for the firmware to tell us it is ok to issue a reset. */
	bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT0 | reset_code, 1);

	/* Deposit a driver reset signature so the firmware knows that
	 * this is a soft reset. */
	REG_WR_IND(bp, bp->shmem_base + BNX2_DRV_RESET_SIGNATURE,
		   BNX2_DRV_RESET_SIGNATURE_MAGIC);

	/* Do a dummy read to force the chip to complete all current transaction
	 * before we issue a reset. */
	val = REG_RD(bp, BNX2_MISC_ID);

	val = BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
	      BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
	      BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP;

	/* Chip reset. */
	REG_WR(bp, BNX2_PCICFG_MISC_CONFIG, val);

	if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5706_A1))
		msleep(15);

	/* Reset takes approximate 30 usec */
	for (i = 0; i < 10; i++) {
		val = REG_RD(bp, BNX2_PCICFG_MISC_CONFIG);
		if ((val & (BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
			    BNX2_PCICFG_MISC_CONFIG_CORE_RST_BSY)) == 0) {
			break;
		}
		udelay(10);
	}

	if (val & (BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
		   BNX2_PCICFG_MISC_CONFIG_CORE_RST_BSY)) {
		printk(KERN_ERR PFX "Chip reset did not complete\n");
		return -EBUSY;
	}

	/* Make sure byte swapping is properly configured. */
	val = REG_RD(bp, BNX2_PCI_SWAP_DIAG0);
	if (val != 0x01020304) {
		printk(KERN_ERR PFX "Chip not in correct endian mode\n");
		return -ENODEV;
	}

	/* Wait for the firmware to finish its initialization. */
	rc = bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT1 | reset_code, 0);
	if (rc)
		return rc;

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		/* Adjust the voltage regular to two steps lower.  The default
		 * of this register is 0x0000000e. */
		REG_WR(bp, BNX2_MISC_VREG_CONTROL, 0x000000fa);

		/* Remove bad rbuf memory from the free pool. */
		rc = bnx2_alloc_bad_rbuf(bp);
	}

	return rc;
}

static int
bnx2_init_chip(struct bnx2 *bp)
{
	u32 val;
	int rc;

	/* Make sure the interrupt is not active. */
	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	val = BNX2_DMA_CONFIG_DATA_BYTE_SWAP |
	      BNX2_DMA_CONFIG_DATA_WORD_SWAP |
#ifdef __BIG_ENDIAN
	      BNX2_DMA_CONFIG_CNTL_BYTE_SWAP | 
#endif
	      BNX2_DMA_CONFIG_CNTL_WORD_SWAP | 
	      DMA_READ_CHANS << 12 |
	      DMA_WRITE_CHANS << 16;

	val |= (0x2 << 20) | (1 << 11);

	if ((bp->flags & PCIX_FLAG) && (bp->bus_speed_mhz == 133))
		val |= (1 << 23);

	if ((CHIP_NUM(bp) == CHIP_NUM_5706) &&
	    (CHIP_ID(bp) != CHIP_ID_5706_A0) && !(bp->flags & PCIX_FLAG))
		val |= BNX2_DMA_CONFIG_CNTL_PING_PONG_DMA;

	REG_WR(bp, BNX2_DMA_CONFIG, val);

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		val = REG_RD(bp, BNX2_TDMA_CONFIG);
		val |= BNX2_TDMA_CONFIG_ONE_DMA;
		REG_WR(bp, BNX2_TDMA_CONFIG, val);
	}

	if (bp->flags & PCIX_FLAG) {
		u16 val16;

		pci_read_config_word(bp->pdev, bp->pcix_cap + PCI_X_CMD,
				     &val16);
		pci_write_config_word(bp->pdev, bp->pcix_cap + PCI_X_CMD,
				      val16 & ~PCI_X_CMD_ERO);
	}

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
	       BNX2_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE |
	       BNX2_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE |
	       BNX2_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE);

	/* Initialize context mapping and zero out the quick contexts.  The
	 * context block must have already been enabled. */
	bnx2_init_context(bp);

	bnx2_init_cpus(bp);
	bnx2_init_nvram(bp);

	bnx2_set_mac_addr(bp);

	val = REG_RD(bp, BNX2_MQ_CONFIG);
	val &= ~BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE;
	val |= BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE_256;
	REG_WR(bp, BNX2_MQ_CONFIG, val);

	val = 0x10000 + (MAX_CID_CNT * MB_KERNEL_CTX_SIZE);
	REG_WR(bp, BNX2_MQ_KNL_BYP_WIND_START, val);
	REG_WR(bp, BNX2_MQ_KNL_WIND_END, val);

	val = (BCM_PAGE_BITS - 8) << 24;
	REG_WR(bp, BNX2_RV2P_CONFIG, val);

	/* Configure page size. */
	val = REG_RD(bp, BNX2_TBDR_CONFIG);
	val &= ~BNX2_TBDR_CONFIG_PAGE_SIZE;
	val |= (BCM_PAGE_BITS - 8) << 24 | 0x40;
	REG_WR(bp, BNX2_TBDR_CONFIG, val);

	val = bp->mac_addr[0] +
	      (bp->mac_addr[1] << 8) +
	      (bp->mac_addr[2] << 16) +
	      bp->mac_addr[3] +
	      (bp->mac_addr[4] << 8) +
	      (bp->mac_addr[5] << 16);
	REG_WR(bp, BNX2_EMAC_BACKOFF_SEED, val);

	/* Program the MTU.  Also include 4 bytes for CRC32. */
	val = bp->dev->mtu + ETH_HLEN + 4;
	if (val > (MAX_ETHERNET_PACKET_SIZE + 4))
		val |= BNX2_EMAC_RX_MTU_SIZE_JUMBO_ENA;
	REG_WR(bp, BNX2_EMAC_RX_MTU_SIZE, val);

	bp->last_status_idx = 0;
	bp->rx_mode = BNX2_EMAC_RX_MODE_SORT_MODE;

	/* Set up how to generate a link change interrupt. */
	REG_WR(bp, BNX2_EMAC_ATTENTION_ENA, BNX2_EMAC_ATTENTION_ENA_LINK);

	REG_WR(bp, BNX2_HC_STATUS_ADDR_L,
	       (u64) bp->status_blk_mapping & 0xffffffff);
	REG_WR(bp, BNX2_HC_STATUS_ADDR_H, (u64) bp->status_blk_mapping >> 32);

	REG_WR(bp, BNX2_HC_STATISTICS_ADDR_L,
	       (u64) bp->stats_blk_mapping & 0xffffffff);
	REG_WR(bp, BNX2_HC_STATISTICS_ADDR_H,
	       (u64) bp->stats_blk_mapping >> 32);

	REG_WR(bp, BNX2_HC_TX_QUICK_CONS_TRIP, 
	       (bp->tx_quick_cons_trip_int << 16) | bp->tx_quick_cons_trip);

	REG_WR(bp, BNX2_HC_RX_QUICK_CONS_TRIP,
	       (bp->rx_quick_cons_trip_int << 16) | bp->rx_quick_cons_trip);

	REG_WR(bp, BNX2_HC_COMP_PROD_TRIP,
	       (bp->comp_prod_trip_int << 16) | bp->comp_prod_trip);

	REG_WR(bp, BNX2_HC_TX_TICKS, (bp->tx_ticks_int << 16) | bp->tx_ticks);

	REG_WR(bp, BNX2_HC_RX_TICKS, (bp->rx_ticks_int << 16) | bp->rx_ticks);

	REG_WR(bp, BNX2_HC_COM_TICKS,
	       (bp->com_ticks_int << 16) | bp->com_ticks);

	REG_WR(bp, BNX2_HC_CMD_TICKS,
	       (bp->cmd_ticks_int << 16) | bp->cmd_ticks);

	REG_WR(bp, BNX2_HC_STATS_TICKS, bp->stats_ticks & 0xffff00);
	REG_WR(bp, BNX2_HC_STAT_COLLECT_TICKS, 0xbb8);  /* 3ms */

	if (CHIP_ID(bp) == CHIP_ID_5706_A1)
		REG_WR(bp, BNX2_HC_CONFIG, BNX2_HC_CONFIG_COLLECT_STATS);
	else {
		REG_WR(bp, BNX2_HC_CONFIG, BNX2_HC_CONFIG_RX_TMR_MODE |
		       BNX2_HC_CONFIG_TX_TMR_MODE |
		       BNX2_HC_CONFIG_COLLECT_STATS);
	}

	/* Clear internal stats counters. */
	REG_WR(bp, BNX2_HC_COMMAND, BNX2_HC_COMMAND_CLR_STAT_NOW);

	REG_WR(bp, BNX2_HC_ATTN_BITS_ENABLE, STATUS_ATTN_BITS_LINK_STATE);

	if (REG_RD_IND(bp, bp->shmem_base + BNX2_PORT_FEATURE) &
	    BNX2_PORT_FEATURE_ASF_ENABLED)
		bp->flags |= ASF_ENABLE_FLAG;

	/* Initialize the receive filter. */
	bnx2_set_rx_mode(bp->dev);

	rc = bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT2 | BNX2_DRV_MSG_CODE_RESET,
			  0);

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS, 0x5ffffff);
	REG_RD(bp, BNX2_MISC_ENABLE_SET_BITS);

	udelay(20);

	return rc;
}


static void
bnx2_init_tx_ring(struct bnx2 *bp)
{
	struct tx_bd *txbd;
	u32 val;

	txbd = &bp->tx_desc_ring[MAX_TX_DESC_CNT];
		
	txbd->tx_bd_haddr_hi = (u64) bp->tx_desc_mapping >> 32;
	txbd->tx_bd_haddr_lo = (u64) bp->tx_desc_mapping & 0xffffffff;

	bp->tx_prod = 0;
	bp->tx_cons = 0;
	bp->hw_tx_cons = 0;
	bp->tx_prod_bseq = 0;
	
	val = BNX2_L2CTX_TYPE_TYPE_L2;
	val |= BNX2_L2CTX_TYPE_SIZE_L2;
	CTX_WR(bp, GET_CID_ADDR(TX_CID), BNX2_L2CTX_TYPE, val);

	val = BNX2_L2CTX_CMD_TYPE_TYPE_L2;
	val |= 8 << 16;
	CTX_WR(bp, GET_CID_ADDR(TX_CID), BNX2_L2CTX_CMD_TYPE, val);

	val = (u64) bp->tx_desc_mapping >> 32;
	CTX_WR(bp, GET_CID_ADDR(TX_CID), BNX2_L2CTX_TBDR_BHADDR_HI, val);

	val = (u64) bp->tx_desc_mapping & 0xffffffff;
	CTX_WR(bp, GET_CID_ADDR(TX_CID), BNX2_L2CTX_TBDR_BHADDR_LO, val);
}

static void
bnx2_init_rx_ring(struct bnx2 *bp)
{
	struct rx_bd *rxbd;
	int i;
	u16 prod, ring_prod; 
	u32 val;

	/* 8 for CRC and VLAN */
	bp->rx_buf_use_size = bp->dev->mtu + ETH_HLEN + bp->rx_offset + 8;
	/* 8 for alignment */
	bp->rx_buf_size = bp->rx_buf_use_size + 8;

	ring_prod = prod = bp->rx_prod = 0;
	bp->rx_cons = 0;
	bp->hw_rx_cons = 0;
	bp->rx_prod_bseq = 0;
		
	for (i = 0; i < bp->rx_max_ring; i++) {
		int j;

		rxbd = &bp->rx_desc_ring[i][0];
		for (j = 0; j < MAX_RX_DESC_CNT; j++, rxbd++) {
			rxbd->rx_bd_len = bp->rx_buf_use_size;
			rxbd->rx_bd_flags = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
		}
		if (i == (bp->rx_max_ring - 1))
			j = 0;
		else
			j = i + 1;
		rxbd->rx_bd_haddr_hi = (u64) bp->rx_desc_mapping[j] >> 32;
		rxbd->rx_bd_haddr_lo = (u64) bp->rx_desc_mapping[j] &
				       0xffffffff;
	}

	val = BNX2_L2CTX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE;
	val |= BNX2_L2CTX_CTX_TYPE_SIZE_L2;
	val |= 0x02 << 8;
	CTX_WR(bp, GET_CID_ADDR(RX_CID), BNX2_L2CTX_CTX_TYPE, val);

	val = (u64) bp->rx_desc_mapping[0] >> 32;
	CTX_WR(bp, GET_CID_ADDR(RX_CID), BNX2_L2CTX_NX_BDHADDR_HI, val);

	val = (u64) bp->rx_desc_mapping[0] & 0xffffffff;
	CTX_WR(bp, GET_CID_ADDR(RX_CID), BNX2_L2CTX_NX_BDHADDR_LO, val);

	for (i = 0; i < bp->rx_ring_size; i++) {
		if (bnx2_alloc_rx_skb(bp, ring_prod) < 0) {
			break;
		}
		prod = NEXT_RX_BD(prod);
		ring_prod = RX_RING_IDX(prod);
	}
	bp->rx_prod = prod;

	REG_WR16(bp, MB_RX_CID_ADDR + BNX2_L2CTX_HOST_BDIDX, prod);

	REG_WR(bp, MB_RX_CID_ADDR + BNX2_L2CTX_HOST_BSEQ, bp->rx_prod_bseq);
}

static void
bnx2_set_rx_ring_size(struct bnx2 *bp, u32 size)
{
	u32 num_rings, max;

	bp->rx_ring_size = size;
	num_rings = 1;
	while (size > MAX_RX_DESC_CNT) {
		size -= MAX_RX_DESC_CNT;
		num_rings++;
	}
	/* round to next power of 2 */
	max = MAX_RX_RINGS;
	while ((max & num_rings) == 0)
		max >>= 1;

	if (num_rings != max)
		max <<= 1;

	bp->rx_max_ring = max;
	bp->rx_max_ring_idx = (bp->rx_max_ring * RX_DESC_CNT) - 1;
}

static void
bnx2_free_tx_skbs(struct bnx2 *bp)
{
	int i;

	if (bp->tx_buf_ring == NULL)
		return;

	for (i = 0; i < TX_DESC_CNT; ) {
		struct sw_bd *tx_buf = &bp->tx_buf_ring[i];
		struct sk_buff *skb = tx_buf->skb;
		int j, last;

		if (skb == NULL) {
			i++;
			continue;
		}

		pci_unmap_single(bp->pdev, pci_unmap_addr(tx_buf, mapping),
			skb_headlen(skb), PCI_DMA_TODEVICE);

		tx_buf->skb = NULL;

		last = skb_shinfo(skb)->nr_frags;
		for (j = 0; j < last; j++) {
			tx_buf = &bp->tx_buf_ring[i + j + 1];
			pci_unmap_page(bp->pdev,
				pci_unmap_addr(tx_buf, mapping),
				skb_shinfo(skb)->frags[j].size,
				PCI_DMA_TODEVICE);
		}
		dev_kfree_skb_any(skb);
		i += j + 1;
	}

}

static void
bnx2_free_rx_skbs(struct bnx2 *bp)
{
	int i;

	if (bp->rx_buf_ring == NULL)
		return;

	for (i = 0; i < bp->rx_max_ring_idx; i++) {
		struct sw_bd *rx_buf = &bp->rx_buf_ring[i];
		struct sk_buff *skb = rx_buf->skb;

		if (skb == NULL)
			continue;

		pci_unmap_single(bp->pdev, pci_unmap_addr(rx_buf, mapping),
			bp->rx_buf_use_size, PCI_DMA_FROMDEVICE);

		rx_buf->skb = NULL;

		dev_kfree_skb_any(skb);
	}
}

static void
bnx2_free_skbs(struct bnx2 *bp)
{
	bnx2_free_tx_skbs(bp);
	bnx2_free_rx_skbs(bp);
}

static int
bnx2_reset_nic(struct bnx2 *bp, u32 reset_code)
{
	int rc;

	rc = bnx2_reset_chip(bp, reset_code);
	bnx2_free_skbs(bp);
	if (rc)
		return rc;

	bnx2_init_chip(bp);
	bnx2_init_tx_ring(bp);
	bnx2_init_rx_ring(bp);
	return 0;
}

static int
bnx2_init_nic(struct bnx2 *bp)
{
	int rc;

	if ((rc = bnx2_reset_nic(bp, BNX2_DRV_MSG_CODE_RESET)) != 0)
		return rc;

	bnx2_init_phy(bp);
	bnx2_set_link(bp);
	return 0;
}

static int
bnx2_test_registers(struct bnx2 *bp)
{
	int ret;
	int i;
	static struct {
		u16   offset;
		u16   flags;
		u32   rw_mask;
		u32   ro_mask;
	} reg_tbl[] = {
		{ 0x006c, 0, 0x00000000, 0x0000003f },
		{ 0x0090, 0, 0xffffffff, 0x00000000 },
		{ 0x0094, 0, 0x00000000, 0x00000000 },

		{ 0x0404, 0, 0x00003f00, 0x00000000 },
		{ 0x0418, 0, 0x00000000, 0xffffffff },
		{ 0x041c, 0, 0x00000000, 0xffffffff },
		{ 0x0420, 0, 0x00000000, 0x80ffffff },
		{ 0x0424, 0, 0x00000000, 0x00000000 },
		{ 0x0428, 0, 0x00000000, 0x00000001 },
		{ 0x0450, 0, 0x00000000, 0x0000ffff },
		{ 0x0454, 0, 0x00000000, 0xffffffff },
		{ 0x0458, 0, 0x00000000, 0xffffffff },

		{ 0x0808, 0, 0x00000000, 0xffffffff },
		{ 0x0854, 0, 0x00000000, 0xffffffff },
		{ 0x0868, 0, 0x00000000, 0x77777777 },
		{ 0x086c, 0, 0x00000000, 0x77777777 },
		{ 0x0870, 0, 0x00000000, 0x77777777 },
		{ 0x0874, 0, 0x00000000, 0x77777777 },

		{ 0x0c00, 0, 0x00000000, 0x00000001 },
		{ 0x0c04, 0, 0x00000000, 0x03ff0001 },
		{ 0x0c08, 0, 0x0f0ff073, 0x00000000 },

		{ 0x1000, 0, 0x00000000, 0x00000001 },
		{ 0x1004, 0, 0x00000000, 0x000f0001 },

		{ 0x1408, 0, 0x01c00800, 0x00000000 },
		{ 0x149c, 0, 0x8000ffff, 0x00000000 },
		{ 0x14a8, 0, 0x00000000, 0x000001ff },
		{ 0x14ac, 0, 0x0fffffff, 0x10000000 },
		{ 0x14b0, 0, 0x00000002, 0x00000001 },
		{ 0x14b8, 0, 0x00000000, 0x00000000 },
		{ 0x14c0, 0, 0x00000000, 0x00000009 },
		{ 0x14c4, 0, 0x00003fff, 0x00000000 },
		{ 0x14cc, 0, 0x00000000, 0x00000001 },
		{ 0x14d0, 0, 0xffffffff, 0x00000000 },

		{ 0x1800, 0, 0x00000000, 0x00000001 },
		{ 0x1804, 0, 0x00000000, 0x00000003 },

		{ 0x2800, 0, 0x00000000, 0x00000001 },
		{ 0x2804, 0, 0x00000000, 0x00003f01 },
		{ 0x2808, 0, 0x0f3f3f03, 0x00000000 },
		{ 0x2810, 0, 0xffff0000, 0x00000000 },
		{ 0x2814, 0, 0xffff0000, 0x00000000 },
		{ 0x2818, 0, 0xffff0000, 0x00000000 },
		{ 0x281c, 0, 0xffff0000, 0x00000000 },
		{ 0x2834, 0, 0xffffffff, 0x00000000 },
		{ 0x2840, 0, 0x00000000, 0xffffffff },
		{ 0x2844, 0, 0x00000000, 0xffffffff },
		{ 0x2848, 0, 0xffffffff, 0x00000000 },
		{ 0x284c, 0, 0xf800f800, 0x07ff07ff },

		{ 0x2c00, 0, 0x00000000, 0x00000011 },
		{ 0x2c04, 0, 0x00000000, 0x00030007 },

		{ 0x3c00, 0, 0x00000000, 0x00000001 },
		{ 0x3c04, 0, 0x00000000, 0x00070000 },
		{ 0x3c08, 0, 0x00007f71, 0x07f00000 },
		{ 0x3c0c, 0, 0x1f3ffffc, 0x00000000 },
		{ 0x3c10, 0, 0xffffffff, 0x00000000 },
		{ 0x3c14, 0, 0x00000000, 0xffffffff },
		{ 0x3c18, 0, 0x00000000, 0xffffffff },
		{ 0x3c1c, 0, 0xfffff000, 0x00000000 },
		{ 0x3c20, 0, 0xffffff00, 0x00000000 },

		{ 0x5004, 0, 0x00000000, 0x0000007f },
		{ 0x5008, 0, 0x0f0007ff, 0x00000000 },
		{ 0x500c, 0, 0xf800f800, 0x07ff07ff },

		{ 0x5c00, 0, 0x00000000, 0x00000001 },
		{ 0x5c04, 0, 0x00000000, 0x0003000f },
		{ 0x5c08, 0, 0x00000003, 0x00000000 },
		{ 0x5c0c, 0, 0x0000fff8, 0x00000000 },
		{ 0x5c10, 0, 0x00000000, 0xffffffff },
		{ 0x5c80, 0, 0x00000000, 0x0f7113f1 },
		{ 0x5c84, 0, 0x00000000, 0x0000f333 },
		{ 0x5c88, 0, 0x00000000, 0x00077373 },
		{ 0x5c8c, 0, 0x00000000, 0x0007f737 },

		{ 0x6808, 0, 0x0000ff7f, 0x00000000 },
		{ 0x680c, 0, 0xffffffff, 0x00000000 },
		{ 0x6810, 0, 0xffffffff, 0x00000000 },
		{ 0x6814, 0, 0xffffffff, 0x00000000 },
		{ 0x6818, 0, 0xffffffff, 0x00000000 },
		{ 0x681c, 0, 0xffffffff, 0x00000000 },
		{ 0x6820, 0, 0x00ff00ff, 0x00000000 },
		{ 0x6824, 0, 0x00ff00ff, 0x00000000 },
		{ 0x6828, 0, 0x00ff00ff, 0x00000000 },
		{ 0x682c, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6830, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6834, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6838, 0, 0x03ff03ff, 0x00000000 },
		{ 0x683c, 0, 0x0000ffff, 0x00000000 },
		{ 0x6840, 0, 0x00000ff0, 0x00000000 },
		{ 0x6844, 0, 0x00ffff00, 0x00000000 },
		{ 0x684c, 0, 0xffffffff, 0x00000000 },
		{ 0x6850, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6854, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6858, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x685c, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6908, 0, 0x00000000, 0x0001ff0f },
		{ 0x690c, 0, 0x00000000, 0x0ffe00f0 },

		{ 0xffff, 0, 0x00000000, 0x00000000 },
	};

	ret = 0;
	for (i = 0; reg_tbl[i].offset != 0xffff; i++) {
		u32 offset, rw_mask, ro_mask, save_val, val;

		offset = (u32) reg_tbl[i].offset;
		rw_mask = reg_tbl[i].rw_mask;
		ro_mask = reg_tbl[i].ro_mask;

		save_val = readl(bp->regview + offset);

		writel(0, bp->regview + offset);

		val = readl(bp->regview + offset);
		if ((val & rw_mask) != 0) {
			goto reg_test_err;
		}

		if ((val & ro_mask) != (save_val & ro_mask)) {
			goto reg_test_err;
		}

		writel(0xffffffff, bp->regview + offset);

		val = readl(bp->regview + offset);
		if ((val & rw_mask) != rw_mask) {
			goto reg_test_err;
		}

		if ((val & ro_mask) != (save_val & ro_mask)) {
			goto reg_test_err;
		}

		writel(save_val, bp->regview + offset);
		continue;

reg_test_err:
		writel(save_val, bp->regview + offset);
		ret = -ENODEV;
		break;
	}
	return ret;
}

static int
bnx2_do_mem_test(struct bnx2 *bp, u32 start, u32 size)
{
	static u32 test_pattern[] = { 0x00000000, 0xffffffff, 0x55555555,
		0xaaaaaaaa , 0xaa55aa55, 0x55aa55aa };
	int i;

	for (i = 0; i < sizeof(test_pattern) / 4; i++) {
		u32 offset;

		for (offset = 0; offset < size; offset += 4) {

			REG_WR_IND(bp, start + offset, test_pattern[i]);

			if (REG_RD_IND(bp, start + offset) !=
				test_pattern[i]) {
				return -ENODEV;
			}
		}
	}
	return 0;
}

static int
bnx2_test_memory(struct bnx2 *bp)
{
	int ret = 0;
	int i;
	static struct {
		u32   offset;
		u32   len;
	} mem_tbl[] = {
		{ 0x60000,  0x4000 },
		{ 0xa0000,  0x3000 },
		{ 0xe0000,  0x4000 },
		{ 0x120000, 0x4000 },
		{ 0x1a0000, 0x4000 },
		{ 0x160000, 0x4000 },
		{ 0xffffffff, 0    },
	};

	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++) {
		if ((ret = bnx2_do_mem_test(bp, mem_tbl[i].offset,
			mem_tbl[i].len)) != 0) {
			return ret;
		}
	}
	
	return ret;
}

#define BNX2_MAC_LOOPBACK	0
#define BNX2_PHY_LOOPBACK	1

static int
bnx2_run_loopback(struct bnx2 *bp, int loopback_mode)
{
	unsigned int pkt_size, num_pkts, i;
	struct sk_buff *skb, *rx_skb;
	unsigned char *packet;
	u16 rx_start_idx, rx_idx;
	u32 val;
	dma_addr_t map;
	struct tx_bd *txbd;
	struct sw_bd *rx_buf;
	struct l2_fhdr *rx_hdr;
	int ret = -ENODEV;

	if (loopback_mode == BNX2_MAC_LOOPBACK) {
		bp->loopback = MAC_LOOPBACK;
		bnx2_set_mac_loopback(bp);
	}
	else if (loopback_mode == BNX2_PHY_LOOPBACK) {
		bp->loopback = 0;
		bnx2_set_phy_loopback(bp);
	}
	else
		return -EINVAL;

	pkt_size = 1514;
	skb = dev_alloc_skb(pkt_size);
	if (!skb)
		return -ENOMEM;
	packet = skb_put(skb, pkt_size);
	memcpy(packet, bp->mac_addr, 6);
	memset(packet + 6, 0x0, 8);
	for (i = 14; i < pkt_size; i++)
		packet[i] = (unsigned char) (i & 0xff);

	map = pci_map_single(bp->pdev, skb->data, pkt_size,
		PCI_DMA_TODEVICE);

	val = REG_RD(bp, BNX2_HC_COMMAND);
	REG_WR(bp, BNX2_HC_COMMAND, val | BNX2_HC_COMMAND_COAL_NOW_WO_INT);
	REG_RD(bp, BNX2_HC_COMMAND);

	udelay(5);
	rx_start_idx = bp->status_blk->status_rx_quick_consumer_index0;

	num_pkts = 0;

	txbd = &bp->tx_desc_ring[TX_RING_IDX(bp->tx_prod)];

	txbd->tx_bd_haddr_hi = (u64) map >> 32;
	txbd->tx_bd_haddr_lo = (u64) map & 0xffffffff;
	txbd->tx_bd_mss_nbytes = pkt_size;
	txbd->tx_bd_vlan_tag_flags = TX_BD_FLAGS_START | TX_BD_FLAGS_END;

	num_pkts++;
	bp->tx_prod = NEXT_TX_BD(bp->tx_prod);
	bp->tx_prod_bseq += pkt_size;

	REG_WR16(bp, MB_TX_CID_ADDR + BNX2_L2CTX_TX_HOST_BIDX, bp->tx_prod);
	REG_WR(bp, MB_TX_CID_ADDR + BNX2_L2CTX_TX_HOST_BSEQ, bp->tx_prod_bseq);

	udelay(100);

	val = REG_RD(bp, BNX2_HC_COMMAND);
	REG_WR(bp, BNX2_HC_COMMAND, val | BNX2_HC_COMMAND_COAL_NOW_WO_INT);
	REG_RD(bp, BNX2_HC_COMMAND);

	udelay(5);

	pci_unmap_single(bp->pdev, map, pkt_size, PCI_DMA_TODEVICE);
	dev_kfree_skb_irq(skb);

	if (bp->status_blk->status_tx_quick_consumer_index0 != bp->tx_prod) {
		goto loopback_test_done;
	}

	rx_idx = bp->status_blk->status_rx_quick_consumer_index0;
	if (rx_idx != rx_start_idx + num_pkts) {
		goto loopback_test_done;
	}

	rx_buf = &bp->rx_buf_ring[rx_start_idx];
	rx_skb = rx_buf->skb;

	rx_hdr = (struct l2_fhdr *) rx_skb->data;
	skb_reserve(rx_skb, bp->rx_offset);

	pci_dma_sync_single_for_cpu(bp->pdev,
		pci_unmap_addr(rx_buf, mapping),
		bp->rx_buf_size, PCI_DMA_FROMDEVICE);

	if (rx_hdr->l2_fhdr_status &
		(L2_FHDR_ERRORS_BAD_CRC |
		L2_FHDR_ERRORS_PHY_DECODE |
		L2_FHDR_ERRORS_ALIGNMENT |
		L2_FHDR_ERRORS_TOO_SHORT |
		L2_FHDR_ERRORS_GIANT_FRAME)) {

		goto loopback_test_done;
	}

	if ((rx_hdr->l2_fhdr_pkt_len - 4) != pkt_size) {
		goto loopback_test_done;
	}

	for (i = 14; i < pkt_size; i++) {
		if (*(rx_skb->data + i) != (unsigned char) (i & 0xff)) {
			goto loopback_test_done;
		}
	}

	ret = 0;

loopback_test_done:
	bp->loopback = 0;
	return ret;
}

#define BNX2_MAC_LOOPBACK_FAILED	1
#define BNX2_PHY_LOOPBACK_FAILED	2
#define BNX2_LOOPBACK_FAILED		(BNX2_MAC_LOOPBACK_FAILED |	\
					 BNX2_PHY_LOOPBACK_FAILED)

static int
bnx2_test_loopback(struct bnx2 *bp)
{
	int rc = 0;

	if (!netif_running(bp->dev))
		return BNX2_LOOPBACK_FAILED;

	bnx2_reset_nic(bp, BNX2_DRV_MSG_CODE_RESET);
	spin_lock_bh(&bp->phy_lock);
	bnx2_init_phy(bp);
	spin_unlock_bh(&bp->phy_lock);
	if (bnx2_run_loopback(bp, BNX2_MAC_LOOPBACK))
		rc |= BNX2_MAC_LOOPBACK_FAILED;
	if (bnx2_run_loopback(bp, BNX2_PHY_LOOPBACK))
		rc |= BNX2_PHY_LOOPBACK_FAILED;
	return rc;
}

#define NVRAM_SIZE 0x200
#define CRC32_RESIDUAL 0xdebb20e3

static int
bnx2_test_nvram(struct bnx2 *bp)
{
	u32 buf[NVRAM_SIZE / 4];
	u8 *data = (u8 *) buf;
	int rc = 0;
	u32 magic, csum;

	if ((rc = bnx2_nvram_read(bp, 0, data, 4)) != 0)
		goto test_nvram_done;

        magic = be32_to_cpu(buf[0]);
	if (magic != 0x669955aa) {
		rc = -ENODEV;
		goto test_nvram_done;
	}

	if ((rc = bnx2_nvram_read(bp, 0x100, data, NVRAM_SIZE)) != 0)
		goto test_nvram_done;

	csum = ether_crc_le(0x100, data);
	if (csum != CRC32_RESIDUAL) {
		rc = -ENODEV;
		goto test_nvram_done;
	}

	csum = ether_crc_le(0x100, data + 0x100);
	if (csum != CRC32_RESIDUAL) {
		rc = -ENODEV;
	}

test_nvram_done:
	return rc;
}

static int
bnx2_test_link(struct bnx2 *bp)
{
	u32 bmsr;

	spin_lock_bh(&bp->phy_lock);
	bnx2_read_phy(bp, MII_BMSR, &bmsr);
	bnx2_read_phy(bp, MII_BMSR, &bmsr);
	spin_unlock_bh(&bp->phy_lock);
		
	if (bmsr & BMSR_LSTATUS) {
		return 0;
	}
	return -ENODEV;
}

static int
bnx2_test_intr(struct bnx2 *bp)
{
	int i;
	u32 val;
	u16 status_idx;

	if (!netif_running(bp->dev))
		return -ENODEV;

	status_idx = REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD) & 0xffff;

	/* This register is not touched during run-time. */
	val = REG_RD(bp, BNX2_HC_COMMAND);
	REG_WR(bp, BNX2_HC_COMMAND, val | BNX2_HC_COMMAND_COAL_NOW);
	REG_RD(bp, BNX2_HC_COMMAND);

	for (i = 0; i < 10; i++) {
		if ((REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD) & 0xffff) !=
			status_idx) {

			break;
		}

		msleep_interruptible(10);
	}
	if (i < 10)
		return 0;

	return -ENODEV;
}

static void
bnx2_timer(unsigned long data)
{
	struct bnx2 *bp = (struct bnx2 *) data;
	u32 msg;

	if (!netif_running(bp->dev))
		return;

	if (atomic_read(&bp->intr_sem) != 0)
		goto bnx2_restart_timer;

	msg = (u32) ++bp->fw_drv_pulse_wr_seq;
	REG_WR_IND(bp, bp->shmem_base + BNX2_DRV_PULSE_MB, msg);

	if ((bp->phy_flags & PHY_SERDES_FLAG) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5706)) {

		spin_lock(&bp->phy_lock);
		if (bp->serdes_an_pending) {
			bp->serdes_an_pending--;
		}
		else if ((bp->link_up == 0) && (bp->autoneg & AUTONEG_SPEED)) {
			u32 bmcr;

			bp->current_interval = bp->timer_interval;

			bnx2_read_phy(bp, MII_BMCR, &bmcr);

			if (bmcr & BMCR_ANENABLE) {
				u32 phy1, phy2;

				bnx2_write_phy(bp, 0x1c, 0x7c00);
				bnx2_read_phy(bp, 0x1c, &phy1);

				bnx2_write_phy(bp, 0x17, 0x0f01);
				bnx2_read_phy(bp, 0x15, &phy2);
				bnx2_write_phy(bp, 0x17, 0x0f01);
				bnx2_read_phy(bp, 0x15, &phy2);

				if ((phy1 & 0x10) &&	/* SIGNAL DETECT */
					!(phy2 & 0x20)) {	/* no CONFIG */

					bmcr &= ~BMCR_ANENABLE;
					bmcr |= BMCR_SPEED1000 |
						BMCR_FULLDPLX;
					bnx2_write_phy(bp, MII_BMCR, bmcr);
					bp->phy_flags |=
						PHY_PARALLEL_DETECT_FLAG;
				}
			}
		}
		else if ((bp->link_up) && (bp->autoneg & AUTONEG_SPEED) &&
			(bp->phy_flags & PHY_PARALLEL_DETECT_FLAG)) {
			u32 phy2;

			bnx2_write_phy(bp, 0x17, 0x0f01);
			bnx2_read_phy(bp, 0x15, &phy2);
			if (phy2 & 0x20) {
				u32 bmcr;

				bnx2_read_phy(bp, MII_BMCR, &bmcr);
				bmcr |= BMCR_ANENABLE;
				bnx2_write_phy(bp, MII_BMCR, bmcr);

				bp->phy_flags &= ~PHY_PARALLEL_DETECT_FLAG;

			}
		}
		else
			bp->current_interval = bp->timer_interval;

		spin_unlock(&bp->phy_lock);
	}

bnx2_restart_timer:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

/* Called with rtnl_lock */
static int
bnx2_open(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	bnx2_set_power_state(bp, PCI_D0);
	bnx2_disable_int(bp);

	rc = bnx2_alloc_mem(bp);
	if (rc)
		return rc;

	if ((CHIP_ID(bp) != CHIP_ID_5706_A0) &&
		(CHIP_ID(bp) != CHIP_ID_5706_A1) &&
		!disable_msi) {

		if (pci_enable_msi(bp->pdev) == 0) {
			bp->flags |= USING_MSI_FLAG;
			rc = request_irq(bp->pdev->irq, bnx2_msi, 0, dev->name,
					dev);
		}
		else {
			rc = request_irq(bp->pdev->irq, bnx2_interrupt,
					SA_SHIRQ, dev->name, dev);
		}
	}
	else {
		rc = request_irq(bp->pdev->irq, bnx2_interrupt, SA_SHIRQ,
				dev->name, dev);
	}
	if (rc) {
		bnx2_free_mem(bp);
		return rc;
	}

	rc = bnx2_init_nic(bp);

	if (rc) {
		free_irq(bp->pdev->irq, dev);
		if (bp->flags & USING_MSI_FLAG) {
			pci_disable_msi(bp->pdev);
			bp->flags &= ~USING_MSI_FLAG;
		}
		bnx2_free_skbs(bp);
		bnx2_free_mem(bp);
		return rc;
	}
	
	mod_timer(&bp->timer, jiffies + bp->current_interval);

	atomic_set(&bp->intr_sem, 0);

	bnx2_enable_int(bp);

	if (bp->flags & USING_MSI_FLAG) {
		/* Test MSI to make sure it is working
		 * If MSI test fails, go back to INTx mode
		 */
		if (bnx2_test_intr(bp) != 0) {
			printk(KERN_WARNING PFX "%s: No interrupt was generated"
			       " using MSI, switching to INTx mode. Please"
			       " report this failure to the PCI maintainer"
			       " and include system chipset information.\n",
			       bp->dev->name);

			bnx2_disable_int(bp);
			free_irq(bp->pdev->irq, dev);
			pci_disable_msi(bp->pdev);
			bp->flags &= ~USING_MSI_FLAG;

			rc = bnx2_init_nic(bp);

			if (!rc) {
				rc = request_irq(bp->pdev->irq, bnx2_interrupt,
					SA_SHIRQ, dev->name, dev);
			}
			if (rc) {
				bnx2_free_skbs(bp);
				bnx2_free_mem(bp);
				del_timer_sync(&bp->timer);
				return rc;
			}
			bnx2_enable_int(bp);
		}
	}
	if (bp->flags & USING_MSI_FLAG) {
		printk(KERN_INFO PFX "%s: using MSI\n", dev->name);
	}

	netif_start_queue(dev);

	return 0;
}

static void
bnx2_reset_task(void *data)
{
	struct bnx2 *bp = data;

	if (!netif_running(bp->dev))
		return;

	bp->in_reset_task = 1;
	bnx2_netif_stop(bp);

	bnx2_init_nic(bp);

	atomic_set(&bp->intr_sem, 1);
	bnx2_netif_start(bp);
	bp->in_reset_task = 0;
}

static void
bnx2_tx_timeout(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	/* This allows the netif to be shutdown gracefully before resetting */
	schedule_work(&bp->reset_task);
}

#ifdef BCM_VLAN
/* Called with rtnl_lock */
static void
bnx2_vlan_rx_register(struct net_device *dev, struct vlan_group *vlgrp)
{
	struct bnx2 *bp = netdev_priv(dev);

	bnx2_netif_stop(bp);

	bp->vlgrp = vlgrp;
	bnx2_set_rx_mode(dev);

	bnx2_netif_start(bp);
}

/* Called with rtnl_lock */
static void
bnx2_vlan_rx_kill_vid(struct net_device *dev, uint16_t vid)
{
	struct bnx2 *bp = netdev_priv(dev);

	bnx2_netif_stop(bp);

	if (bp->vlgrp)
		bp->vlgrp->vlan_devices[vid] = NULL;
	bnx2_set_rx_mode(dev);

	bnx2_netif_start(bp);
}
#endif

/* Called with dev->xmit_lock.
 * hard_start_xmit is pseudo-lockless - a lock is only required when
 * the tx queue is full. This way, we get the benefit of lockless
 * operations most of the time without the complexities to handle
 * netif_stop_queue/wake_queue race conditions.
 */
static int
bnx2_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	dma_addr_t mapping;
	struct tx_bd *txbd;
	struct sw_bd *tx_buf;
	u32 len, vlan_tag_flags, last_frag, mss;
	u16 prod, ring_prod;
	int i;

	if (unlikely(bnx2_tx_avail(bp) < (skb_shinfo(skb)->nr_frags + 1))) {
		netif_stop_queue(dev);
		printk(KERN_ERR PFX "%s: BUG! Tx ring full when queue awake!\n",
			dev->name);

		return NETDEV_TX_BUSY;
	}
	len = skb_headlen(skb);
	prod = bp->tx_prod;
	ring_prod = TX_RING_IDX(prod);

	vlan_tag_flags = 0;
	if (skb->ip_summed == CHECKSUM_HW) {
		vlan_tag_flags |= TX_BD_FLAGS_TCP_UDP_CKSUM;
	}

	if (bp->vlgrp != 0 && vlan_tx_tag_present(skb)) {
		vlan_tag_flags |=
			(TX_BD_FLAGS_VLAN_TAG | (vlan_tx_tag_get(skb) << 16));
	}
#ifdef BCM_TSO 
	if ((mss = skb_shinfo(skb)->tso_size) &&
		(skb->len > (bp->dev->mtu + ETH_HLEN))) {
		u32 tcp_opt_len, ip_tcp_len;

		if (skb_header_cloned(skb) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		tcp_opt_len = ((skb->h.th->doff - 5) * 4);
		vlan_tag_flags |= TX_BD_FLAGS_SW_LSO;

		tcp_opt_len = 0;
		if (skb->h.th->doff > 5) {
			tcp_opt_len = (skb->h.th->doff - 5) << 2;
		}
		ip_tcp_len = (skb->nh.iph->ihl << 2) + sizeof(struct tcphdr);

		skb->nh.iph->check = 0;
		skb->nh.iph->tot_len = ntohs(mss + ip_tcp_len + tcp_opt_len);
		skb->h.th->check =
			~csum_tcpudp_magic(skb->nh.iph->saddr,
					    skb->nh.iph->daddr,
					    0, IPPROTO_TCP, 0);

		if (tcp_opt_len || (skb->nh.iph->ihl > 5)) {
			vlan_tag_flags |= ((skb->nh.iph->ihl - 5) +
				(tcp_opt_len >> 2)) << 8;
		}
	}
	else
#endif
	{
		mss = 0;
	}

	mapping = pci_map_single(bp->pdev, skb->data, len, PCI_DMA_TODEVICE);
	
	tx_buf = &bp->tx_buf_ring[ring_prod];
	tx_buf->skb = skb;
	pci_unmap_addr_set(tx_buf, mapping, mapping);

	txbd = &bp->tx_desc_ring[ring_prod];

	txbd->tx_bd_haddr_hi = (u64) mapping >> 32;
	txbd->tx_bd_haddr_lo = (u64) mapping & 0xffffffff;
	txbd->tx_bd_mss_nbytes = len | (mss << 16);
	txbd->tx_bd_vlan_tag_flags = vlan_tag_flags | TX_BD_FLAGS_START;

	last_frag = skb_shinfo(skb)->nr_frags;

	for (i = 0; i < last_frag; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX_BD(prod);
		ring_prod = TX_RING_IDX(prod);
		txbd = &bp->tx_desc_ring[ring_prod];

		len = frag->size;
		mapping = pci_map_page(bp->pdev, frag->page, frag->page_offset,
			len, PCI_DMA_TODEVICE);
		pci_unmap_addr_set(&bp->tx_buf_ring[ring_prod],
				mapping, mapping);

		txbd->tx_bd_haddr_hi = (u64) mapping >> 32;
		txbd->tx_bd_haddr_lo = (u64) mapping & 0xffffffff;
		txbd->tx_bd_mss_nbytes = len | (mss << 16);
		txbd->tx_bd_vlan_tag_flags = vlan_tag_flags;

	}
	txbd->tx_bd_vlan_tag_flags |= TX_BD_FLAGS_END;

	prod = NEXT_TX_BD(prod);
	bp->tx_prod_bseq += skb->len;

	REG_WR16(bp, MB_TX_CID_ADDR + BNX2_L2CTX_TX_HOST_BIDX, prod);
	REG_WR(bp, MB_TX_CID_ADDR + BNX2_L2CTX_TX_HOST_BSEQ, bp->tx_prod_bseq);

	mmiowb();

	bp->tx_prod = prod;
	dev->trans_start = jiffies;

	if (unlikely(bnx2_tx_avail(bp) <= MAX_SKB_FRAGS)) {
		spin_lock(&bp->tx_lock);
		netif_stop_queue(dev);
		
		if (bnx2_tx_avail(bp) > MAX_SKB_FRAGS)
			netif_wake_queue(dev);
		spin_unlock(&bp->tx_lock);
	}

	return NETDEV_TX_OK;
}

/* Called with rtnl_lock */
static int
bnx2_close(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	u32 reset_code;

	/* Calling flush_scheduled_work() may deadlock because
	 * linkwatch_event() may be on the workqueue and it will try to get
	 * the rtnl_lock which we are holding.
	 */
	while (bp->in_reset_task)
		msleep(1);

	bnx2_netif_stop(bp);
	del_timer_sync(&bp->timer);
	if (bp->flags & NO_WOL_FLAG)
		reset_code = BNX2_DRV_MSG_CODE_UNLOAD;
	else if (bp->wol)
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_WOL;
	else
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL;
	bnx2_reset_chip(bp, reset_code);
	free_irq(bp->pdev->irq, dev);
	if (bp->flags & USING_MSI_FLAG) {
		pci_disable_msi(bp->pdev);
		bp->flags &= ~USING_MSI_FLAG;
	}
	bnx2_free_skbs(bp);
	bnx2_free_mem(bp);
	bp->link_up = 0;
	netif_carrier_off(bp->dev);
	bnx2_set_power_state(bp, PCI_D3hot);
	return 0;
}

#define GET_NET_STATS64(ctr)					\
	(unsigned long) ((unsigned long) (ctr##_hi) << 32) +	\
	(unsigned long) (ctr##_lo)

#define GET_NET_STATS32(ctr)		\
	(ctr##_lo)

#if (BITS_PER_LONG == 64)
#define GET_NET_STATS	GET_NET_STATS64
#else
#define GET_NET_STATS	GET_NET_STATS32
#endif

static struct net_device_stats *
bnx2_get_stats(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	struct statistics_block *stats_blk = bp->stats_blk;
	struct net_device_stats *net_stats = &bp->net_stats;

	if (bp->stats_blk == NULL) {
		return net_stats;
	}
	net_stats->rx_packets =
		GET_NET_STATS(stats_blk->stat_IfHCInUcastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCInMulticastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCInBroadcastPkts);

	net_stats->tx_packets =
		GET_NET_STATS(stats_blk->stat_IfHCOutUcastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCOutMulticastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCOutBroadcastPkts);

	net_stats->rx_bytes =
		GET_NET_STATS(stats_blk->stat_IfHCInOctets);

	net_stats->tx_bytes =
		GET_NET_STATS(stats_blk->stat_IfHCOutOctets);

	net_stats->multicast = 
		GET_NET_STATS(stats_blk->stat_IfHCOutMulticastPkts);

	net_stats->collisions = 
		(unsigned long) stats_blk->stat_EtherStatsCollisions;

	net_stats->rx_length_errors = 
		(unsigned long) (stats_blk->stat_EtherStatsUndersizePkts +
		stats_blk->stat_EtherStatsOverrsizePkts);

	net_stats->rx_over_errors = 
		(unsigned long) stats_blk->stat_IfInMBUFDiscards;

	net_stats->rx_frame_errors = 
		(unsigned long) stats_blk->stat_Dot3StatsAlignmentErrors;

	net_stats->rx_crc_errors = 
		(unsigned long) stats_blk->stat_Dot3StatsFCSErrors;

	net_stats->rx_errors = net_stats->rx_length_errors +
		net_stats->rx_over_errors + net_stats->rx_frame_errors +
		net_stats->rx_crc_errors;

	net_stats->tx_aborted_errors =
    		(unsigned long) (stats_blk->stat_Dot3StatsExcessiveCollisions +
		stats_blk->stat_Dot3StatsLateCollisions);

	if ((CHIP_NUM(bp) == CHIP_NUM_5706) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_A0))
		net_stats->tx_carrier_errors = 0;
	else {
		net_stats->tx_carrier_errors =
			(unsigned long)
			stats_blk->stat_Dot3StatsCarrierSenseErrors;
	}

	net_stats->tx_errors =
    		(unsigned long) 
		stats_blk->stat_emac_tx_stat_dot3statsinternalmactransmiterrors
		+
		net_stats->tx_aborted_errors +
		net_stats->tx_carrier_errors;

	return net_stats;
}

/* All ethtool functions called with rtnl_lock */

static int
bnx2_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2 *bp = netdev_priv(dev);

	cmd->supported = SUPPORTED_Autoneg;
	if (bp->phy_flags & PHY_SERDES_FLAG) {
		cmd->supported |= SUPPORTED_1000baseT_Full |
			SUPPORTED_FIBRE;

		cmd->port = PORT_FIBRE;
	}
	else {
		cmd->supported |= SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half |
			SUPPORTED_100baseT_Full |
			SUPPORTED_1000baseT_Full |
			SUPPORTED_TP;

		cmd->port = PORT_TP;
	}

	cmd->advertising = bp->advertising;

	if (bp->autoneg & AUTONEG_SPEED) {
		cmd->autoneg = AUTONEG_ENABLE;
	}
	else {
		cmd->autoneg = AUTONEG_DISABLE;
	}

	if (netif_carrier_ok(dev)) {
		cmd->speed = bp->line_speed;
		cmd->duplex = bp->duplex;
	}
	else {
		cmd->speed = -1;
		cmd->duplex = -1;
	}

	cmd->transceiver = XCVR_INTERNAL;
	cmd->phy_address = bp->phy_addr;

	return 0;
}
  
static int
bnx2_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2 *bp = netdev_priv(dev);
	u8 autoneg = bp->autoneg;
	u8 req_duplex = bp->req_duplex;
	u16 req_line_speed = bp->req_line_speed;
	u32 advertising = bp->advertising;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		autoneg |= AUTONEG_SPEED;

		cmd->advertising &= ETHTOOL_ALL_COPPER_SPEED; 

		/* allow advertising 1 speed */
		if ((cmd->advertising == ADVERTISED_10baseT_Half) ||
			(cmd->advertising == ADVERTISED_10baseT_Full) ||
			(cmd->advertising == ADVERTISED_100baseT_Half) ||
			(cmd->advertising == ADVERTISED_100baseT_Full)) {

			if (bp->phy_flags & PHY_SERDES_FLAG)
				return -EINVAL;

			advertising = cmd->advertising;

		}
		else if (cmd->advertising == ADVERTISED_1000baseT_Full) {
			advertising = cmd->advertising;
		}
		else if (cmd->advertising == ADVERTISED_1000baseT_Half) {
			return -EINVAL;
		}
		else {
			if (bp->phy_flags & PHY_SERDES_FLAG) {
				advertising = ETHTOOL_ALL_FIBRE_SPEED;
			}
			else {
				advertising = ETHTOOL_ALL_COPPER_SPEED;
			}
		}
		advertising |= ADVERTISED_Autoneg;
	}
	else {
		if (bp->phy_flags & PHY_SERDES_FLAG) {
			if ((cmd->speed != SPEED_1000) ||
				(cmd->duplex != DUPLEX_FULL)) {
				return -EINVAL;
			}
		}
		else if (cmd->speed == SPEED_1000) {
			return -EINVAL;
		}
		autoneg &= ~AUTONEG_SPEED;
		req_line_speed = cmd->speed;
		req_duplex = cmd->duplex;
		advertising = 0;
	}

	bp->autoneg = autoneg;
	bp->advertising = advertising;
	bp->req_line_speed = req_line_speed;
	bp->req_duplex = req_duplex;

	spin_lock_bh(&bp->phy_lock);

	bnx2_setup_phy(bp);

	spin_unlock_bh(&bp->phy_lock);

	return 0;
}

static void
bnx2_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct bnx2 *bp = netdev_priv(dev);

	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
	strcpy(info->bus_info, pci_name(bp->pdev));
	info->fw_version[0] = ((bp->fw_ver & 0xff000000) >> 24) + '0';
	info->fw_version[2] = ((bp->fw_ver & 0xff0000) >> 16) + '0';
	info->fw_version[4] = ((bp->fw_ver & 0xff00) >> 8) + '0';
	info->fw_version[1] = info->fw_version[3] = '.';
	info->fw_version[5] = 0;
}

#define BNX2_REGDUMP_LEN		(32 * 1024)

static int
bnx2_get_regs_len(struct net_device *dev)
{
	return BNX2_REGDUMP_LEN;
}

static void
bnx2_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	u32 *p = _p, i, offset;
	u8 *orig_p = _p;
	struct bnx2 *bp = netdev_priv(dev);
	u32 reg_boundaries[] = { 0x0000, 0x0098, 0x0400, 0x045c,
				 0x0800, 0x0880, 0x0c00, 0x0c10,
				 0x0c30, 0x0d08, 0x1000, 0x101c,
				 0x1040, 0x1048, 0x1080, 0x10a4,
				 0x1400, 0x1490, 0x1498, 0x14f0,
				 0x1500, 0x155c, 0x1580, 0x15dc,
				 0x1600, 0x1658, 0x1680, 0x16d8,
				 0x1800, 0x1820, 0x1840, 0x1854,
				 0x1880, 0x1894, 0x1900, 0x1984,
				 0x1c00, 0x1c0c, 0x1c40, 0x1c54,
				 0x1c80, 0x1c94, 0x1d00, 0x1d84,
				 0x2000, 0x2030, 0x23c0, 0x2400,
				 0x2800, 0x2820, 0x2830, 0x2850,
				 0x2b40, 0x2c10, 0x2fc0, 0x3058,
				 0x3c00, 0x3c94, 0x4000, 0x4010,
				 0x4080, 0x4090, 0x43c0, 0x4458,
				 0x4c00, 0x4c18, 0x4c40, 0x4c54,
				 0x4fc0, 0x5010, 0x53c0, 0x5444,
				 0x5c00, 0x5c18, 0x5c80, 0x5c90,
				 0x5fc0, 0x6000, 0x6400, 0x6428,
				 0x6800, 0x6848, 0x684c, 0x6860,
				 0x6888, 0x6910, 0x8000 };

	regs->version = 0;

	memset(p, 0, BNX2_REGDUMP_LEN);

	if (!netif_running(bp->dev))
		return;

	i = 0;
	offset = reg_boundaries[0];
	p += offset;
	while (offset < BNX2_REGDUMP_LEN) {
		*p++ = REG_RD(bp, offset);
		offset += 4;
		if (offset == reg_boundaries[i + 1]) {
			offset = reg_boundaries[i + 2];
			p = (u32 *) (orig_p + offset);
			i += 2;
		}
	}
}

static void
bnx2_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (bp->flags & NO_WOL_FLAG) {
		wol->supported = 0;
		wol->wolopts = 0;
	}
	else {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
		else
			wol->wolopts = 0;
	}
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int
bnx2_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		if (bp->flags & NO_WOL_FLAG)
			return -EINVAL;

		bp->wol = 1;
	}
	else {
		bp->wol = 0;
	}
	return 0;
}

static int
bnx2_nway_reset(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	u32 bmcr;

	if (!(bp->autoneg & AUTONEG_SPEED)) {
		return -EINVAL;
	}

	spin_lock_bh(&bp->phy_lock);

	/* Force a link down visible on the other side */
	if (bp->phy_flags & PHY_SERDES_FLAG) {
		bnx2_write_phy(bp, MII_BMCR, BMCR_LOOPBACK);
		spin_unlock_bh(&bp->phy_lock);

		msleep(20);

		spin_lock_bh(&bp->phy_lock);
		if (CHIP_NUM(bp) == CHIP_NUM_5706) {
			bp->current_interval = SERDES_AN_TIMEOUT;
			bp->serdes_an_pending = 1;
			mod_timer(&bp->timer, jiffies + bp->current_interval);
		}
	}

	bnx2_read_phy(bp, MII_BMCR, &bmcr);
	bmcr &= ~BMCR_LOOPBACK;
	bnx2_write_phy(bp, MII_BMCR, bmcr | BMCR_ANRESTART | BMCR_ANENABLE);

	spin_unlock_bh(&bp->phy_lock);

	return 0;
}

#if 0 /* Not in RHEL3... */
static int
bnx2_get_eeprom_len(struct net_device *dev)
{
  	struct bnx2 *bp = netdev_priv(dev);

	if (bp->flash_info == NULL)
		return 0;

	return (int) bp->flash_size;
}
#endif

static int
bnx2_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		u8 *eebuf)
{
  	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	if (eeprom->offset > bp->flash_info->total_size)
		return -EINVAL;

	if (eeprom->offset > bp->flash_info->total_size - eeprom->len)
		eeprom->len = bp->flash_info->total_size - eeprom->offset;

	rc = bnx2_nvram_read(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int
bnx2_set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		u8 *eebuf)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	if (eeprom->offset > bp->flash_info->total_size)
		return -EINVAL;

	if (eeprom->offset > bp->flash_info->total_size - eeprom->len)
		eeprom->len = bp->flash_info->total_size - eeprom->offset;

	rc = bnx2_nvram_write(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int
bnx2_get_coalesce(struct net_device *dev, struct ethtool_coalesce *coal)
{
	struct bnx2 *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	coal->rx_coalesce_usecs = bp->rx_ticks;
	coal->rx_max_coalesced_frames = bp->rx_quick_cons_trip;
	coal->rx_coalesce_usecs_irq = bp->rx_ticks_int;
	coal->rx_max_coalesced_frames_irq = bp->rx_quick_cons_trip_int;

	coal->tx_coalesce_usecs = bp->tx_ticks;
	coal->tx_max_coalesced_frames = bp->tx_quick_cons_trip;
	coal->tx_coalesce_usecs_irq = bp->tx_ticks_int;
	coal->tx_max_coalesced_frames_irq = bp->tx_quick_cons_trip_int;

	coal->stats_block_coalesce_usecs = bp->stats_ticks;

	return 0;
}

static int
bnx2_set_coalesce(struct net_device *dev, struct ethtool_coalesce *coal)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->rx_ticks = (u16) coal->rx_coalesce_usecs;
	if (bp->rx_ticks > 0x3ff) bp->rx_ticks = 0x3ff;

	bp->rx_quick_cons_trip = (u16) coal->rx_max_coalesced_frames; 
	if (bp->rx_quick_cons_trip > 0xff) bp->rx_quick_cons_trip = 0xff;

	bp->rx_ticks_int = (u16) coal->rx_coalesce_usecs_irq;
	if (bp->rx_ticks_int > 0x3ff) bp->rx_ticks_int = 0x3ff;

	bp->rx_quick_cons_trip_int = (u16) coal->rx_max_coalesced_frames_irq;
	if (bp->rx_quick_cons_trip_int > 0xff)
		bp->rx_quick_cons_trip_int = 0xff;

	bp->tx_ticks = (u16) coal->tx_coalesce_usecs;
	if (bp->tx_ticks > 0x3ff) bp->tx_ticks = 0x3ff;

	bp->tx_quick_cons_trip = (u16) coal->tx_max_coalesced_frames;
	if (bp->tx_quick_cons_trip > 0xff) bp->tx_quick_cons_trip = 0xff;

	bp->tx_ticks_int = (u16) coal->tx_coalesce_usecs_irq;
	if (bp->tx_ticks_int > 0x3ff) bp->tx_ticks_int = 0x3ff;

	bp->tx_quick_cons_trip_int = (u16) coal->tx_max_coalesced_frames_irq;
	if (bp->tx_quick_cons_trip_int > 0xff) bp->tx_quick_cons_trip_int =
		0xff;

	bp->stats_ticks = coal->stats_block_coalesce_usecs;
	if (bp->stats_ticks > 0xffff00) bp->stats_ticks = 0xffff00;
	bp->stats_ticks &= 0xffff00;

	if (netif_running(bp->dev)) {
		bnx2_netif_stop(bp);
		bnx2_init_nic(bp);
		bnx2_netif_start(bp);
	}

	return 0;
}

static void
bnx2_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct bnx2 *bp = netdev_priv(dev);

	ering->rx_max_pending = MAX_TOTAL_RX_DESC_CNT;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;

	ering->rx_pending = bp->rx_ring_size;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;

	ering->tx_max_pending = MAX_TX_DESC_CNT;
	ering->tx_pending = bp->tx_ring_size;
}

static int
bnx2_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct bnx2 *bp = netdev_priv(dev);

	if ((ering->rx_pending > MAX_TOTAL_RX_DESC_CNT) ||
		(ering->tx_pending > MAX_TX_DESC_CNT) ||
		(ering->tx_pending <= MAX_SKB_FRAGS)) {

		return -EINVAL;
	}
	if (netif_running(bp->dev)) {
		bnx2_netif_stop(bp);
		bnx2_reset_chip(bp, BNX2_DRV_MSG_CODE_RESET);
		bnx2_free_skbs(bp);
		bnx2_free_mem(bp);
	}

	bnx2_set_rx_ring_size(bp, ering->rx_pending);
	bp->tx_ring_size = ering->tx_pending;

	if (netif_running(bp->dev)) {
		int rc;

		rc = bnx2_alloc_mem(bp);
		if (rc)
			return rc;
		bnx2_init_nic(bp);
		bnx2_netif_start(bp);
	}

	return 0;
}

static void
bnx2_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct bnx2 *bp = netdev_priv(dev);

	epause->autoneg = ((bp->autoneg & AUTONEG_FLOW_CTRL) != 0);
	epause->rx_pause = ((bp->flow_ctrl & FLOW_CTRL_RX) != 0);
	epause->tx_pause = ((bp->flow_ctrl & FLOW_CTRL_TX) != 0);
}

static int
bnx2_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->req_flow_ctrl = 0;
	if (epause->rx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_RX;
	if (epause->tx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_TX;

	if (epause->autoneg) {
		bp->autoneg |= AUTONEG_FLOW_CTRL;
	}
	else {
		bp->autoneg &= ~AUTONEG_FLOW_CTRL;
	}

	spin_lock_bh(&bp->phy_lock);

	bnx2_setup_phy(bp);

	spin_unlock_bh(&bp->phy_lock);

	return 0;
}

static u32
bnx2_get_rx_csum(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	return bp->rx_csum;
}

static int
bnx2_set_rx_csum(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->rx_csum = data;
	return 0;
}

#define BNX2_NUM_STATS 45

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2_stats_str_arr[BNX2_NUM_STATS] = {
	{ "rx_bytes" },
	{ "rx_error_bytes" },
	{ "tx_bytes" },
	{ "tx_error_bytes" },
	{ "rx_ucast_packets" },
	{ "rx_mcast_packets" },
	{ "rx_bcast_packets" },
	{ "tx_ucast_packets" },
	{ "tx_mcast_packets" },
	{ "tx_bcast_packets" },
	{ "tx_mac_errors" },
	{ "tx_carrier_errors" },
	{ "rx_crc_errors" },
	{ "rx_align_errors" },
	{ "tx_single_collisions" },
	{ "tx_multi_collisions" },
	{ "tx_deferred" },
	{ "tx_excess_collisions" },
	{ "tx_late_collisions" },
	{ "tx_total_collisions" },
	{ "rx_fragments" },
	{ "rx_jabbers" },
	{ "rx_undersize_packets" },
	{ "rx_oversize_packets" },
	{ "rx_64_byte_packets" },
	{ "rx_65_to_127_byte_packets" },
	{ "rx_128_to_255_byte_packets" },
	{ "rx_256_to_511_byte_packets" },
	{ "rx_512_to_1023_byte_packets" },
	{ "rx_1024_to_1522_byte_packets" },
	{ "rx_1523_to_9022_byte_packets" },
	{ "tx_64_byte_packets" },
	{ "tx_65_to_127_byte_packets" },
	{ "tx_128_to_255_byte_packets" },
	{ "tx_256_to_511_byte_packets" },
	{ "tx_512_to_1023_byte_packets" },
	{ "tx_1024_to_1522_byte_packets" },
	{ "tx_1523_to_9022_byte_packets" },
	{ "rx_xon_frames" },
	{ "rx_xoff_frames" },
	{ "tx_xon_frames" },
	{ "tx_xoff_frames" },
	{ "rx_mac_ctrl_frames" },
	{ "rx_filtered_packets" },
	{ "rx_discards" },
};

#define STATS_OFFSET32(offset_name) (offsetof(struct statistics_block, offset_name) / 4)

static unsigned long bnx2_stats_offset_arr[BNX2_NUM_STATS] = {
    STATS_OFFSET32(stat_IfHCInOctets_hi),
    STATS_OFFSET32(stat_IfHCInBadOctets_hi),
    STATS_OFFSET32(stat_IfHCOutOctets_hi),
    STATS_OFFSET32(stat_IfHCOutBadOctets_hi),
    STATS_OFFSET32(stat_IfHCInUcastPkts_hi),
    STATS_OFFSET32(stat_IfHCInMulticastPkts_hi),
    STATS_OFFSET32(stat_IfHCInBroadcastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutUcastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutMulticastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutBroadcastPkts_hi),
    STATS_OFFSET32(stat_emac_tx_stat_dot3statsinternalmactransmiterrors),
    STATS_OFFSET32(stat_Dot3StatsCarrierSenseErrors),                 
    STATS_OFFSET32(stat_Dot3StatsFCSErrors),                          
    STATS_OFFSET32(stat_Dot3StatsAlignmentErrors),                    
    STATS_OFFSET32(stat_Dot3StatsSingleCollisionFrames),              
    STATS_OFFSET32(stat_Dot3StatsMultipleCollisionFrames),            
    STATS_OFFSET32(stat_Dot3StatsDeferredTransmissions),              
    STATS_OFFSET32(stat_Dot3StatsExcessiveCollisions),                
    STATS_OFFSET32(stat_Dot3StatsLateCollisions),                     
    STATS_OFFSET32(stat_EtherStatsCollisions),                        
    STATS_OFFSET32(stat_EtherStatsFragments),                         
    STATS_OFFSET32(stat_EtherStatsJabbers),                           
    STATS_OFFSET32(stat_EtherStatsUndersizePkts),                     
    STATS_OFFSET32(stat_EtherStatsOverrsizePkts),                     
    STATS_OFFSET32(stat_EtherStatsPktsRx64Octets),                    
    STATS_OFFSET32(stat_EtherStatsPktsRx65Octetsto127Octets),         
    STATS_OFFSET32(stat_EtherStatsPktsRx128Octetsto255Octets),        
    STATS_OFFSET32(stat_EtherStatsPktsRx256Octetsto511Octets),        
    STATS_OFFSET32(stat_EtherStatsPktsRx512Octetsto1023Octets),       
    STATS_OFFSET32(stat_EtherStatsPktsRx1024Octetsto1522Octets),      
    STATS_OFFSET32(stat_EtherStatsPktsRx1523Octetsto9022Octets),      
    STATS_OFFSET32(stat_EtherStatsPktsTx64Octets),                    
    STATS_OFFSET32(stat_EtherStatsPktsTx65Octetsto127Octets),         
    STATS_OFFSET32(stat_EtherStatsPktsTx128Octetsto255Octets),        
    STATS_OFFSET32(stat_EtherStatsPktsTx256Octetsto511Octets),        
    STATS_OFFSET32(stat_EtherStatsPktsTx512Octetsto1023Octets),       
    STATS_OFFSET32(stat_EtherStatsPktsTx1024Octetsto1522Octets),      
    STATS_OFFSET32(stat_EtherStatsPktsTx1523Octetsto9022Octets),      
    STATS_OFFSET32(stat_XonPauseFramesReceived),                      
    STATS_OFFSET32(stat_XoffPauseFramesReceived),                     
    STATS_OFFSET32(stat_OutXonSent),                                  
    STATS_OFFSET32(stat_OutXoffSent),                                 
    STATS_OFFSET32(stat_MacControlFramesReceived),                    
    STATS_OFFSET32(stat_IfInFramesL2FilterDiscards),                  
    STATS_OFFSET32(stat_IfInMBUFDiscards),                            
};

/* stat_IfHCInBadOctets and stat_Dot3StatsCarrierSenseErrors are
 * skipped because of errata.
 */               
static u8 bnx2_5706_stats_len_arr[BNX2_NUM_STATS] = {
	8,0,8,8,8,8,8,8,8,8,
	4,0,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,
};

static u8 bnx2_5708_stats_len_arr[BNX2_NUM_STATS] = {
	8,0,8,8,8,8,8,8,8,8,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,
};

#define BNX2_NUM_TESTS 6

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2_tests_str_arr[BNX2_NUM_TESTS] = {
	{ "register_test (offline)" },
	{ "memory_test (offline)" },
	{ "loopback_test (offline)" },
	{ "nvram_test (online)" },
	{ "interrupt_test (online)" },
	{ "link_test (online)" },
};

static int
bnx2_self_test_count(struct net_device *dev)
{
	return BNX2_NUM_TESTS;
}

static void
bnx2_self_test(struct net_device *dev, struct ethtool_test *etest, u64 *buf)
{
	struct bnx2 *bp = netdev_priv(dev);

	memset(buf, 0, sizeof(u64) * BNX2_NUM_TESTS);
	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		bnx2_netif_stop(bp);
		bnx2_reset_chip(bp, BNX2_DRV_MSG_CODE_DIAG);
		bnx2_free_skbs(bp);

		if (bnx2_test_registers(bp) != 0) {
			buf[0] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if (bnx2_test_memory(bp) != 0) {
			buf[1] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if ((buf[2] = bnx2_test_loopback(bp)) != 0)
			etest->flags |= ETH_TEST_FL_FAILED;

		if (!netif_running(bp->dev)) {
			bnx2_reset_chip(bp, BNX2_DRV_MSG_CODE_RESET);
		}
		else {
			bnx2_init_nic(bp);
			bnx2_netif_start(bp);
		}

		/* wait for link up */
		msleep_interruptible(3000);
		if ((!bp->link_up) && !(bp->phy_flags & PHY_SERDES_FLAG))
			msleep_interruptible(4000);
	}

	if (bnx2_test_nvram(bp) != 0) {
		buf[3] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
	if (bnx2_test_intr(bp) != 0) {
		buf[4] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (bnx2_test_link(bp) != 0) {
		buf[5] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;

	}
}

static void
bnx2_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, bnx2_stats_str_arr,
			sizeof(bnx2_stats_str_arr));
		break;
	case ETH_SS_TEST:
		memcpy(buf, bnx2_tests_str_arr,
			sizeof(bnx2_tests_str_arr));
		break;
	}
}

static int
bnx2_get_stats_count(struct net_device *dev)
{
	return BNX2_NUM_STATS;
}

static void
bnx2_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *buf)
{
	struct bnx2 *bp = netdev_priv(dev);
	int i;
	u32 *hw_stats = (u32 *) bp->stats_blk;
	u8 *stats_len_arr = NULL;

	if (hw_stats == NULL) {
		memset(buf, 0, sizeof(u64) * BNX2_NUM_STATS);
		return;
	}

	if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5706_A1) ||
	    (CHIP_ID(bp) == CHIP_ID_5706_A2) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_A0))
		stats_len_arr = bnx2_5706_stats_len_arr;
	else
		stats_len_arr = bnx2_5708_stats_len_arr;

	for (i = 0; i < BNX2_NUM_STATS; i++) {
		if (stats_len_arr[i] == 0) {
			/* skip this counter */
			buf[i] = 0;
			continue;
		}
		if (stats_len_arr[i] == 4) {
			/* 4-byte counter */
			buf[i] = (u64)
				*(hw_stats + bnx2_stats_offset_arr[i]);
			continue;
		}
		/* 8-byte counter */
		buf[i] = (((u64) *(hw_stats +
					bnx2_stats_offset_arr[i])) << 32) +
				*(hw_stats + bnx2_stats_offset_arr[i] + 1);
	}
}

static int
bnx2_phys_id(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);
	int i;
	u32 save;

	if (data == 0)
		data = 2;

	save = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, BNX2_MISC_CFG_LEDMODE_MAC);

	for (i = 0; i < (data * 2); i++) {
		if ((i % 2) == 0) {
			REG_WR(bp, BNX2_EMAC_LED, BNX2_EMAC_LED_OVERRIDE);
		}
		else {
			REG_WR(bp, BNX2_EMAC_LED, BNX2_EMAC_LED_OVERRIDE |
				BNX2_EMAC_LED_1000MB_OVERRIDE |
				BNX2_EMAC_LED_100MB_OVERRIDE |
				BNX2_EMAC_LED_10MB_OVERRIDE |
				BNX2_EMAC_LED_TRAFFIC_OVERRIDE |
				BNX2_EMAC_LED_TRAFFIC);
		}
		msleep_interruptible(500);
		if (signal_pending(current))
			break;
	}
	REG_WR(bp, BNX2_EMAC_LED, 0);
	REG_WR(bp, BNX2_MISC_CFG, save);
	return 0;
}

static struct ethtool_ops bnx2_ethtool_ops = {
	.get_settings		= bnx2_get_settings,
	.set_settings		= bnx2_set_settings,
	.get_drvinfo		= bnx2_get_drvinfo,
	.get_regs_len		= bnx2_get_regs_len,
	.get_regs		= bnx2_get_regs,
	.get_wol		= bnx2_get_wol,
	.set_wol		= bnx2_set_wol,
	.nway_reset		= bnx2_nway_reset,
	.get_link		= ethtool_op_get_link,
#if 0 /* Not in RHEL3... */
	.get_eeprom_len		= bnx2_get_eeprom_len,
#endif
	.get_eeprom		= bnx2_get_eeprom,
	.set_eeprom		= bnx2_set_eeprom,
	.get_coalesce		= bnx2_get_coalesce,
	.set_coalesce		= bnx2_set_coalesce,
	.get_ringparam		= bnx2_get_ringparam,
	.set_ringparam		= bnx2_set_ringparam,
	.get_pauseparam		= bnx2_get_pauseparam,
	.set_pauseparam		= bnx2_set_pauseparam,
	.get_rx_csum		= bnx2_get_rx_csum,
	.set_rx_csum		= bnx2_set_rx_csum,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= ethtool_op_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
#ifdef BCM_TSO
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
#endif
	.self_test_count	= bnx2_self_test_count,
	.self_test		= bnx2_self_test,
	.get_strings		= bnx2_get_strings,
	.phys_id		= bnx2_phys_id,
	.get_stats_count	= bnx2_get_stats_count,
	.get_ethtool_stats	= bnx2_get_ethtool_stats,
#if 0 /* Not in RHEL3... */
	.get_perm_addr		= ethtool_op_get_perm_addr,
#endif
};

/* Called with rtnl_lock */
static int
bnx2_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct bnx2 *bp = netdev_priv(dev);
	int err;

	switch(cmd) {
	case SIOCGMIIPHY:
		data->phy_id = bp->phy_addr;

		/* fallthru */
	case SIOCGMIIREG: {
		u32 mii_regval;

		spin_lock_bh(&bp->phy_lock);
		err = bnx2_read_phy(bp, data->reg_num & 0x1f, &mii_regval);
		spin_unlock_bh(&bp->phy_lock);

		data->val_out = mii_regval;

		return err;
	}

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&bp->phy_lock);
		err = bnx2_write_phy(bp, data->reg_num & 0x1f, data->val_in);
		spin_unlock_bh(&bp->phy_lock);

		return err;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

/* Called with rtnl_lock */
static int
bnx2_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnx2 *bp = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev))
		bnx2_set_mac_addr(bp);

	return 0;
}

/* Called with rtnl_lock */
static int
bnx2_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (((new_mtu + ETH_HLEN) > MAX_ETHERNET_JUMBO_PACKET_SIZE) ||
		((new_mtu + ETH_HLEN) < MIN_ETHERNET_PACKET_SIZE))
		return -EINVAL;

	dev->mtu = new_mtu;
	if (netif_running(dev)) {
		bnx2_netif_stop(bp);

		bnx2_init_nic(bp);

		bnx2_netif_start(bp);
	}
	return 0;
}

#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
static void
poll_bnx2(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (unlikely(netdump_mode)) {
		int bogus_budget = 64; /* netdump_max_backlog */

		/*
		 *  In the case where the poll routine exhausted its
		 *  quota, it relies on the NAPI infrastructure to reset
		 *  the value.
		 */
		dev->quota = dev->weight;
		bnx2_interrupt(bp->pdev->irq, dev, NULL);
		/*
		 *  Check to see if there is work to do.  Only call the
		 *  poll routine if we put ourselves on the poll list.
		 */
		if (dev->poll_list.prev)
			bnx2_poll(dev, &bogus_budget);
	} else {
		disable_irq(bp->pdev->irq);
		bnx2_interrupt(bp->pdev->irq, dev, NULL);
		enable_irq(bp->pdev->irq);
	}
}
#endif

static int __devinit
bnx2_init_board(struct pci_dev *pdev, struct net_device *dev)
{
	struct bnx2 *bp;
	unsigned long mem_len;
	int rc;
	u32 reg;

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	bp = netdev_priv(dev);

	bp->flags = 0;
	bp->phy_flags = 0;

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device(pdev);
	if (rc) {
		printk(KERN_ERR PFX "Cannot enable PCI device, aborting.");
		goto err_out;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "Cannot find PCI device base address, "
		       "aborting.\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	rc = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (rc) {
		printk(KERN_ERR PFX "Cannot obtain PCI resources, aborting.\n");
		goto err_out_disable;
	}

	pci_set_master(pdev);

	bp->pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (bp->pm_cap == 0) {
		printk(KERN_ERR PFX "Cannot find power management capability, "
			       "aborting.\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->pcix_cap = pci_find_capability(pdev, PCI_CAP_ID_PCIX);
	if (bp->pcix_cap == 0) {
		printk(KERN_ERR PFX "Cannot find PCIX capability, aborting.\n");
		rc = -EIO;
		goto err_out_release;
	}

	if (pci_set_dma_mask(pdev, DMA_64BIT_MASK) == 0) {
		bp->flags |= USING_DAC_FLAG;
		if (pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK) != 0) {
			printk(KERN_ERR PFX "pci_set_consistent_dma_mask "
			       "failed, aborting.\n");
			rc = -EIO;
			goto err_out_release;
		}
	}
	else if (pci_set_dma_mask(pdev, DMA_32BIT_MASK) != 0) {
		printk(KERN_ERR PFX "System does not support DMA, aborting.\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->dev = dev;
	bp->pdev = pdev;

	spin_lock_init(&bp->phy_lock);
	spin_lock_init(&bp->tx_lock);
	INIT_WORK(&bp->reset_task, bnx2_reset_task, bp);

	dev->base_addr = dev->mem_start = pci_resource_start(pdev, 0);
	mem_len = MB_GET_CID_ADDR(17);
	dev->mem_end = dev->mem_start + mem_len;
	dev->irq = pdev->irq;

	bp->regview = ioremap_nocache(dev->base_addr, mem_len);

	if (!bp->regview) {
		printk(KERN_ERR PFX "Cannot map register space, aborting.\n");
		rc = -ENOMEM;
		goto err_out_release;
	}

	/* Configure byte swap and enable write to the reg_window registers.
	 * Rely on CPU to do target byte swapping on big endian systems
	 * The chip's target access swapping will not swap all accesses
	 */
	pci_write_config_dword(bp->pdev, BNX2_PCICFG_MISC_CONFIG,
			       BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
			       BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP);

	bnx2_set_power_state(bp, PCI_D0);

	bp->chip_id = REG_RD(bp, BNX2_MISC_ID);

	/* Get bus information. */
	reg = REG_RD(bp, BNX2_PCICFG_MISC_STATUS);
	if (reg & BNX2_PCICFG_MISC_STATUS_PCIX_DET) {
		u32 clkreg;

		bp->flags |= PCIX_FLAG;

		clkreg = REG_RD(bp, BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS);
		
		clkreg &= BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET;
		switch (clkreg) {
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ:
			bp->bus_speed_mhz = 133;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ:
			bp->bus_speed_mhz = 100;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ:
			bp->bus_speed_mhz = 66;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ:
			bp->bus_speed_mhz = 50;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ:
			bp->bus_speed_mhz = 33;
			break;
		}
	}
	else {
		if (reg & BNX2_PCICFG_MISC_STATUS_M66EN)
			bp->bus_speed_mhz = 66;
		else
			bp->bus_speed_mhz = 33;
	}

	if (reg & BNX2_PCICFG_MISC_STATUS_32BIT_DET)
		bp->flags |= PCI_32BIT_FLAG;

	/* 5706A0 may falsely detect SERR and PERR. */
	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		reg = REG_RD(bp, PCI_COMMAND);
		reg &= ~(PCI_COMMAND_SERR | PCI_COMMAND_PARITY);
		REG_WR(bp, PCI_COMMAND, reg);
	}
	else if ((CHIP_ID(bp) == CHIP_ID_5706_A1) &&
		!(bp->flags & PCIX_FLAG)) {

		printk(KERN_ERR PFX "5706 A1 can only be used in a PCIX bus, "
		       "aborting.\n");
		goto err_out_unmap;
	}

	bnx2_init_nvram(bp);

	reg = REG_RD_IND(bp, BNX2_SHM_HDR_SIGNATURE);

	if ((reg & BNX2_SHM_HDR_SIGNATURE_SIG_MASK) ==
	    BNX2_SHM_HDR_SIGNATURE_SIG)
		bp->shmem_base = REG_RD_IND(bp, BNX2_SHM_HDR_ADDR_0);
	else
		bp->shmem_base = HOST_VIEW_SHMEM_BASE;

	/* Get the permanent MAC address.  First we need to make sure the
	 * firmware is actually running.
	 */
	reg = REG_RD_IND(bp, bp->shmem_base + BNX2_DEV_INFO_SIGNATURE);

	if ((reg & BNX2_DEV_INFO_SIGNATURE_MAGIC_MASK) !=
	    BNX2_DEV_INFO_SIGNATURE_MAGIC) {
		printk(KERN_ERR PFX "Firmware not running, aborting.\n");
		rc = -ENODEV;
		goto err_out_unmap;
	}

	bp->fw_ver = REG_RD_IND(bp, bp->shmem_base + BNX2_DEV_INFO_BC_REV);

	reg = REG_RD_IND(bp, bp->shmem_base + BNX2_PORT_HW_CFG_MAC_UPPER);
	bp->mac_addr[0] = (u8) (reg >> 8);
	bp->mac_addr[1] = (u8) reg;

	reg = REG_RD_IND(bp, bp->shmem_base + BNX2_PORT_HW_CFG_MAC_LOWER);
	bp->mac_addr[2] = (u8) (reg >> 24);
	bp->mac_addr[3] = (u8) (reg >> 16);
	bp->mac_addr[4] = (u8) (reg >> 8);
	bp->mac_addr[5] = (u8) reg;

	bp->tx_ring_size = MAX_TX_DESC_CNT;
	bnx2_set_rx_ring_size(bp, 100);

	bp->rx_csum = 1;

	bp->rx_offset = sizeof(struct l2_fhdr) + 2;

	bp->tx_quick_cons_trip_int = 20;
	bp->tx_quick_cons_trip = 20;
	bp->tx_ticks_int = 80;
	bp->tx_ticks = 80;
		
	bp->rx_quick_cons_trip_int = 6;
	bp->rx_quick_cons_trip = 6;
	bp->rx_ticks_int = 18;
	bp->rx_ticks = 18;

	bp->stats_ticks = 1000000 & 0xffff00;

	bp->timer_interval =  HZ;
	bp->current_interval =  HZ;

	bp->phy_addr = 1;

	/* Disable WOL support if we are running on a SERDES chip. */
	if (CHIP_BOND_ID(bp) & CHIP_BOND_ID_SERDES_BIT) {
		bp->phy_flags |= PHY_SERDES_FLAG;
		bp->flags |= NO_WOL_FLAG;
		if (CHIP_NUM(bp) == CHIP_NUM_5708) {
			bp->phy_addr = 2;
			reg = REG_RD_IND(bp, bp->shmem_base +
					 BNX2_SHARED_HW_CFG_CONFIG);
			if (reg & BNX2_SHARED_HW_CFG_PHY_2_5G)
				bp->phy_flags |= PHY_2_5G_CAPABLE_FLAG;
		}
	}

	if (CHIP_NUM(bp) == CHIP_NUM_5708)
		bp->flags |= NO_WOL_FLAG;

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		bp->tx_quick_cons_trip_int =
			bp->tx_quick_cons_trip;
		bp->tx_ticks_int = bp->tx_ticks;
		bp->rx_quick_cons_trip_int =
			bp->rx_quick_cons_trip;
		bp->rx_ticks_int = bp->rx_ticks;
		bp->comp_prod_trip_int = bp->comp_prod_trip;
		bp->com_ticks_int = bp->com_ticks;
		bp->cmd_ticks_int = bp->cmd_ticks;
	}

	bp->autoneg = AUTONEG_SPEED | AUTONEG_FLOW_CTRL;
	bp->req_line_speed = 0;
	if (bp->phy_flags & PHY_SERDES_FLAG) {
		bp->advertising = ETHTOOL_ALL_FIBRE_SPEED | ADVERTISED_Autoneg;

		reg = REG_RD_IND(bp, bp->shmem_base + BNX2_PORT_HW_CFG_CONFIG);
		reg &= BNX2_PORT_HW_CFG_CFG_DFLT_LINK_MASK;
		if (reg == BNX2_PORT_HW_CFG_CFG_DFLT_LINK_1G) {
			bp->autoneg = 0;
			bp->req_line_speed = bp->line_speed = SPEED_1000;
			bp->req_duplex = DUPLEX_FULL;
		}
	}
	else {
		bp->advertising = ETHTOOL_ALL_COPPER_SPEED | ADVERTISED_Autoneg;
	}

	bp->req_flow_ctrl = FLOW_CTRL_RX | FLOW_CTRL_TX;

	init_timer(&bp->timer);
	bp->timer.expires = RUN_AT(bp->timer_interval);
	bp->timer.data = (unsigned long) bp;
	bp->timer.function = bnx2_timer;

	return 0;

err_out_unmap:
	if (bp->regview) {
		iounmap(bp->regview);
		bp->regview = NULL;
	}

err_out_release:
	pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

err_out:
	return rc;
}

static int __devinit
bnx2_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int version_printed = 0;
	struct net_device *dev = NULL;
	struct bnx2 *bp;
	int rc, i;

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	/* dev zeroed in init_etherdev */
	dev = alloc_etherdev(sizeof(*bp));

	if (!dev)
		return -ENOMEM;

	rc = bnx2_init_board(pdev, dev);
	if (rc < 0) {
		free_netdev(dev);
		return rc;
	}

	dev->open = bnx2_open;
	dev->hard_start_xmit = bnx2_start_xmit;
	dev->stop = bnx2_close;
	dev->get_stats = bnx2_get_stats;
	dev->set_multicast_list = bnx2_set_rx_mode;
	dev->do_ioctl = bnx2_ioctl;
	dev->set_mac_address = bnx2_change_mac_addr;
	dev->change_mtu = bnx2_change_mtu;
	dev->tx_timeout = bnx2_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef BCM_VLAN
	dev->vlan_rx_register = bnx2_vlan_rx_register;
	dev->vlan_rx_kill_vid = bnx2_vlan_rx_kill_vid;
#endif
	dev->poll = bnx2_poll;
	dev->ethtool_ops = &bnx2_ethtool_ops;
	dev->weight = 64;

	bp = netdev_priv(dev);

#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
	dev->poll_controller = poll_bnx2;
#endif

	if ((rc = register_netdev(dev))) {
		printk(KERN_ERR PFX "Cannot register net device\n");
		if (bp->regview)
			iounmap(bp->regview);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		free_netdev(dev);
		return rc;
	}

	pci_set_drvdata(pdev, dev);

	memcpy(dev->dev_addr, bp->mac_addr, 6);
#if 0 /* Not in RHEL3... */
	memcpy(dev->perm_addr, bp->mac_addr, 6);
#endif
	bp->name = board_info[ent->driver_data].name,
	printk(KERN_INFO "%s: %s (%c%d) PCI%s %s %dMHz found at mem %lx, "
		"IRQ %d, ",
		dev->name,
		bp->name,
		((CHIP_ID(bp) & 0xf000) >> 12) + 'A',
		((CHIP_ID(bp) & 0x0ff0) >> 4),
		((bp->flags & PCIX_FLAG) ? "-X" : ""),
		((bp->flags & PCI_32BIT_FLAG) ? "32-bit" : "64-bit"),
		bp->bus_speed_mhz,
		dev->base_addr,
		bp->pdev->irq);

	printk("node addr ");
	for (i = 0; i < 6; i++)
		printk("%2.2x", dev->dev_addr[i]);
	printk("\n");

	dev->features |= NETIF_F_SG;
	if (bp->flags & USING_DAC_FLAG)
		dev->features |= NETIF_F_HIGHDMA;
	dev->features |= NETIF_F_IP_CSUM;
#ifdef BCM_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif
#ifdef BCM_TSO
	dev->features |= NETIF_F_TSO;
#endif

	netif_carrier_off(bp->dev);

	return 0;
}

static void __devexit
bnx2_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	flush_scheduled_work();

	unregister_netdev(dev);

	if (bp->regview)
		iounmap(bp->regview);

	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int
bnx2_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);
	u32 reset_code;

	if (!netif_running(dev))
		return 0;

	flush_scheduled_work();
	bnx2_netif_stop(bp);
	netif_device_detach(dev);
	del_timer_sync(&bp->timer);
	if (bp->flags & NO_WOL_FLAG)
		reset_code = BNX2_DRV_MSG_CODE_UNLOAD;
	else if (bp->wol)
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_WOL;
	else
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL;
	bnx2_reset_chip(bp, reset_code);
	bnx2_free_skbs(bp);
	bnx2_set_power_state(bp, pci_choose_state(pdev, state));
	return 0;
}

static int
bnx2_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	bnx2_set_power_state(bp, PCI_D0);
	netif_device_attach(dev);
	bnx2_init_nic(bp);
	bnx2_netif_start(bp);
	return 0;
}

static struct pci_driver bnx2_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= bnx2_pci_tbl,
	.probe		= bnx2_init_one,
	.remove		= __devexit_p(bnx2_remove_one),
	.suspend	= bnx2_suspend,
	.resume		= bnx2_resume,
};

static int __init bnx2_init(void)
{
	return pci_module_init(&bnx2_pci_driver);
}

static void __exit bnx2_cleanup(void)
{
	pci_unregister_driver(&bnx2_pci_driver);
}

module_init(bnx2_init);
module_exit(bnx2_cleanup);



