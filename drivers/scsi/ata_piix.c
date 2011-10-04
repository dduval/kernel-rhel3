/*
	Copyright 2003 Red Hat Inc
	Copyright 2003 Jeff Garzik


	Copyright header from piix.c:

    Copyright (C) 1998-1999 Andrzej Krzysztofowicz, Author and Maintainer
    Copyright (C) 1998-2000 Andre Hedrick <andre@linux-ide.org>
    Copyright (C) 2003 Red Hat Inc <alan@redhat.com>

    May be copied or modified under the terms of the GNU General Public License

	TODO:
	* check traditional port enable/disable bits in pata port_probe

 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/ata.h>

#define DRV_NAME	"ata_piix"
#define DRV_VERSION	"0.93"

enum {
	ICH5_PCS		= 0x92,	/* port control and status */

	PIIX_FLAG_COMBINED	= (1 << 30), /* combined mode possible */

	PIIX_COMB_PRI		= (1 << 0), /* combined mode, PATA primary */
	PIIX_COMB_SEC		= (1 << 1), /* combined mode, PATA secondary */

	ich5_pata		= 0,
	ich5_sata		= 1,
	piix4_pata		= 2,
};

static int __devinit piix_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent);
static void piix_port_probe(struct ata_port *ap);
static void piix_port_disable(struct ata_port *ap);
static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio);
static void piix_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			       unsigned int udma);

static unsigned int in_module_init = 1;

static struct pci_device_id piix_pci_tbl[] __devinitdata = {
#ifdef CONFIG_SCSI_ATA_PATA
	{ 0x8086, 0x7111, PCI_ANY_ID, PCI_ANY_ID, 0, 0, piix4_pata },
	{ 0x8086, 0x24db, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
	{ 0x8086, 0x25a2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_pata },
#endif

	{ 0x8086, 0x24d1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x24df, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x25a3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },
	{ 0x8086, 0x25b0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, ich5_sata },

	{ }	/* terminate list */
};

struct pci_driver piix_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= piix_pci_tbl,
	.probe			= piix_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template piix_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.detect			= ata_scsi_detect,
	.release		= ata_scsi_release,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= ATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.use_new_eh_code	= ATA_SHT_NEW_EH_CODE,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
};

struct ata_host_info piix_pata_ops = {
	.port_probe		= ata_port_probe,
	.port_disable		= ata_port_disable,
	.set_piomode		= piix_set_piomode,
	.set_udmamode		= piix_set_udmamode,
	.tf_load		= ata_tf_load_pio,
};

struct ata_host_info piix_sata_ops = {
	.port_probe		= piix_port_probe,
	.port_disable		= piix_port_disable,
	.set_piomode		= piix_set_piomode,
	.set_udmamode		= piix_set_udmamode,
	.tf_load		= ata_tf_load_pio,
};

struct ata_board ata_board_tbl[] __devinitdata = {
	/* ich5_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x03,	/* pio3-4 */
		.udma_mask	= 0x07,	/* udma0-2 ; FIXME */
		.host_info	= &piix_pata_ops,
	},

	/* ich5_sata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SATA | PIIX_FLAG_COMBINED,
		.pio_mask	= 0x03,	/* pio3-4 */
		.udma_mask	= 0x7f,	/* udma0-6 ; FIXME */
		.host_info	= &piix_sata_ops,
	},

	/* piix4_pata */
	{
		.sht		= &piix_sht,
		.host_flags	= ATA_FLAG_SLAVE_POSS,
		.pio_mask	= 0x03, /* pio3-4 */
		.udma_mask	= 0x07,	/* udma0-2 ; FIXME */
		.host_info	= &piix_pata_ops,
	},
};

MODULE_AUTHOR("Andre Hedrick, Alan Cox, Andrzej Krzysztofowicz, Jeff Garzik");
MODULE_DESCRIPTION("SCSI low-level driver for Intel PIIX/ICH ATA controllers");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, piix_pci_tbl);

/**
 *	piix_port_probe -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static void piix_port_probe(struct ata_port *ap)
{
	u16 pcs;
	struct pci_dev *pdev = ap->host_set->pdev;

	pci_read_config_word(pdev, ICH5_PCS, &pcs);

	/* if port not enabled, exit */
	if ((pcs & (1 << ap->port_no)) == 0) {
		ata_port_disable(ap);
		DPRINTK("port %d disabled, ignoring\n", ap->port_no);
		return;
	}

	/* if port enabled but no device, disable port and exit */
	if ((pcs & (1 << (ap->port_no + 4))) == 0) {
		pcs &= ~(1 << ap->port_no);
		pci_write_config_word(pdev, ICH5_PCS, pcs);

		ata_port_disable(ap);
		DPRINTK("port %d has no dev, disabling\n", ap->port_no);
		return;
	}

	ata_port_probe(ap);
}

/**
 *	piix_port_disable - 
 *	@ap:
 *
 *	LOCKING:
 *
 */

static void piix_port_disable(struct ata_port *ap)
{
	struct pci_dev *pdev = ap->host_set->pdev;
	u16 pcs;

	ata_port_disable(ap);

	pci_read_config_word(pdev, ICH5_PCS, &pcs);

	if (pcs & (1 << ap->port_no)) {
		pcs &= ~(1 << ap->port_no);
		pci_write_config_word(pdev, ICH5_PCS, pcs);
	}
}

/**
 *	piix_set_piomode - 
 *	@ap:
 *	@adev:
 *	@pio:
 *
 *	LOCKING:
 *
 */

static void piix_set_piomode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int pio)
{
	struct pci_dev *dev	= ap->host_set->pdev;
	unsigned int is_slave	= (adev->flags & ATA_DFLAG_MASTER) ? 0 : 1;
	unsigned int master_port= ap->port_no ? 0x42 : 0x40;
	unsigned int slave_port	= 0x44;
	u16 master_data;
	u8 slave_data;

	static const	 /* ISP  RTC */
	u8 timings[][2]	= { { 0, 0 },
			    { 0, 0 },
			    { 1, 0 },
			    { 2, 1 },
			    { 2, 3 }, };

	pci_read_config_word(dev, master_port, &master_data);
	if (is_slave) {
		master_data |= 0x4000;
		/* enable PPE, IE and TIME */
		master_data |= 0x0070;
		pci_read_config_byte(dev, slave_port, &slave_data);
		slave_data &= (ap->port_no ? 0x0f : 0xf0);
		slave_data |=
			(timings[pio][0] << 2) |
			(timings[pio][1] << (ap->port_no ? 4 : 0));
	} else {
		master_data &= 0xccf8;
		/* enable PPE, IE and TIME */
		master_data |= 0x0007;
		master_data |=
			(timings[pio][0] << 12) |
			(timings[pio][1] << 8);
	}
	pci_write_config_word(dev, master_port, master_data);
	if (is_slave)
		pci_write_config_byte(dev, slave_port, slave_data);
}

/**
 *	piix_set_udmamode - 
 *	@ap:
 *	@adev:
 *	@udma:
 *
 *	LOCKING:
 *
 */

static void piix_set_udmamode (struct ata_port *ap, struct ata_device *adev,
			      unsigned int udma)
{
	struct pci_dev *dev	= ap->host_set->pdev;
	u8 maslave		= ap->port_no ? 0x42 : 0x40;
	u8 speed		= udma;
	unsigned int drive_dn	= (ap->port_no ? 2 : 0) + adev->devno;
	int a_speed		= 3 << (drive_dn * 4);
	int u_flag		= 1 << drive_dn;
	int v_flag		= 0x01 << drive_dn;
	int w_flag		= 0x10 << drive_dn;
	int u_speed		= 0;
	int			sitre;
	u16			reg4042, reg44, reg48, reg4a, reg54;
	u8			reg55;

	pci_read_config_word(dev, maslave, &reg4042);
	DPRINTK("reg4042 = 0x%04x\n", reg4042);
	sitre = (reg4042 & 0x4000) ? 1 : 0;
	pci_read_config_word(dev, 0x44, &reg44);
	pci_read_config_word(dev, 0x48, &reg48);
	pci_read_config_word(dev, 0x4a, &reg4a);
	pci_read_config_word(dev, 0x54, &reg54);
	pci_read_config_byte(dev, 0x55, &reg55);

	switch(speed) {
		case XFER_UDMA_4:
		case XFER_UDMA_2:	u_speed = 2 << (drive_dn * 4); break;
		case XFER_UDMA_6:
		case XFER_UDMA_5:
		case XFER_UDMA_3:
		case XFER_UDMA_1:	u_speed = 1 << (drive_dn * 4); break;
		case XFER_UDMA_0:	u_speed = 0 << (drive_dn * 4); break;
		default:
			BUG();
			return;
	}

	if (!(reg48 & u_flag))
		pci_write_config_word(dev, 0x48, reg48|u_flag);
	if (speed == XFER_UDMA_5) {
		pci_write_config_byte(dev, 0x55, (u8) reg55|w_flag);
	} else {
		pci_write_config_byte(dev, 0x55, (u8) reg55 & ~w_flag);
	}
	if (!(reg4a & u_speed)) {
		pci_write_config_word(dev, 0x4a, reg4a & ~a_speed);
		pci_write_config_word(dev, 0x4a, reg4a|u_speed);
	}
	if (speed > XFER_UDMA_2) {
		if (!(reg54 & v_flag)) {
			pci_write_config_word(dev, 0x54, reg54|v_flag);
		}
	} else {
		pci_write_config_word(dev, 0x54, reg54 & ~v_flag);
	}
}

/**
 *	piix_probe_combined - 
 *
 *	LOCKING:
 */
static void piix_probe_combined (struct pci_dev *pdev, unsigned int *mask)
{
	u8 tmp;

	pci_read_config_byte(pdev, 0x90, &tmp); /* combined mode reg */
	tmp &= 0x6; 	/* interesting bits 2:1, PATA primary/secondary */

	/* backwards from what one might expect */
	if (tmp == 0x4)	/* bits 10x */
		*mask |= PIIX_COMB_SEC;
	if (tmp == 0x6)	/* bits 11x */
		*mask |= PIIX_COMB_PRI;
}

/**
 *	piix_init_one - 
 *	@pdev:
 *	@ent:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __devinit piix_init_one (struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	static int printed_version;
	struct ata_board *boards[2];
	unsigned int combined = 0, n_boards = 1;
	unsigned int pata_comb = 0, sata_comb = 0;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	/* no hotplugging support (FIXME) */
	if (!in_module_init)
		return -ENODEV;

	boards[0] = &ata_board_tbl[ent->driver_data];
	boards[1] = NULL;
	if (boards[0]->host_flags & PIIX_FLAG_COMBINED)
		piix_probe_combined(pdev, &combined);

	if (combined & PIIX_COMB_PRI)
		sata_comb = 1;
	else if (combined & PIIX_COMB_SEC)
		pata_comb = 1;

	if (pata_comb || sata_comb) {
		boards[sata_comb] = &ata_board_tbl[ent->driver_data];
		boards[sata_comb]->host_flags |= ATA_FLAG_SLAVE_POSS; /* sigh */
		boards[pata_comb] = &ata_board_tbl[ich5_pata]; /*ich5-specific*/
		n_boards++;

		printk(KERN_WARNING DRV_NAME ": combined mode detected\n");
	}

	return ata_pci_init_one(pdev, boards, n_boards);
}

/**
 *	piix_init - 
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __init piix_init(void)
{
	int rc;

	DPRINTK("pci_module_init\n");
	rc = pci_module_init(&piix_pci_driver);
	if (rc)
		return rc;

	in_module_init = 0;

	DPRINTK("scsi_register_host\n");
	rc = scsi_register_host(&piix_sht);
	if (rc) {
		rc = -ENODEV;
		goto err_out;
	}

	DPRINTK("done\n");
	return 0;

err_out:
	pci_unregister_driver(&piix_pci_driver);
	return rc;
}

/**
 *	piix_exit - 
 *
 *	LOCKING:
 *
 */

static void __exit piix_exit(void)
{
	scsi_unregister_host(&piix_sht);
	pci_unregister_driver(&piix_pci_driver);
}

module_init(piix_init);
module_exit(piix_exit);

