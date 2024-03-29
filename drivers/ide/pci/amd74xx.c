/*
 * Version 2.10
 *
 * AMD 755/756/766/8111 and nVidia nForce IDE driver for Linux.
 *
 * Copyright (c) 2000-2002 Vojtech Pavlik
 *
 * Based on the work of:
 *      Andre Hedrick
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>

#include "ide-timing.h"
#include "amd74xx.h"

#define AMD_IDE_ENABLE		(0x00 + amd_config->base)
#define AMD_IDE_CONFIG		(0x01 + amd_config->base)
#define AMD_CABLE_DETECT	(0x02 + amd_config->base)
#define AMD_DRIVE_TIMING	(0x08 + amd_config->base)
#define AMD_8BIT_TIMING		(0x0e + amd_config->base)
#define AMD_ADDRESS_SETUP	(0x0c + amd_config->base)
#define AMD_UDMA_TIMING		(0x10 + amd_config->base)

#define AMD_UDMA		0x07
#define AMD_UDMA_33		0x01
#define AMD_UDMA_66		0x02
#define AMD_UDMA_100		0x03
#define AMD_UDMA_133		0x04
#define AMD_CHECK_SWDMA		0x08
#define AMD_BAD_SWDMA		0x10
#define AMD_BAD_FIFO		0x20

/*
 * AMD SouthBridge chips.
 */

static struct amd_ide_chip {
	unsigned short id;
	unsigned char rev;
	unsigned long base;
	unsigned char flags;
} amd_ide_chips[] = {
	{ PCI_DEVICE_ID_AMD_COBRA_7401, 0x00, 0x40, AMD_UDMA_33 | AMD_BAD_SWDMA },	/* AMD-755 Cobra */
	{ PCI_DEVICE_ID_AMD_VIPER_7409, 0x00, 0x40, AMD_UDMA_66 | AMD_CHECK_SWDMA },	/* AMD-756 Viper */
	{ PCI_DEVICE_ID_AMD_VIPER_7411, 0x00, 0x40, AMD_UDMA_100 | AMD_BAD_FIFO },	/* AMD-766 Viper */
	{ PCI_DEVICE_ID_AMD_OPUS_7441, 0x00, 0x40, AMD_UDMA_100 },			/* AMD-768 Opus */
	{ PCI_DEVICE_ID_AMD_8111_IDE,  0x00, 0x40, AMD_UDMA_100 },			/* AMD-8111 */
        { PCI_DEVICE_ID_NVIDIA_NFORCE_IDE, 0x00, 0x50, AMD_UDMA_100 },                  /* nVidia nForce */
        { PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE, 0x00, 0x50, AMD_UDMA_100 },                 /* nVidia nForce */
        { PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE, 0x00, 0x50, AMD_UDMA_100 },                /* nVidia nForce2s */
        { PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA, 0x00, 0x50, AMD_UDMA_100 },               /* nVidia nForce2s SATA */
        { PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE, 0x00, 0x50, AMD_UDMA_100 },                 /* NVIDIA nForce3 */
        { PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE, 0x00, 0x50, AMD_UDMA_100 },                /* NVIDIA nForce3s */
        { PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA, 0x00, 0x50, AMD_UDMA_100 },               /* NVIDIA nForce3s SATA */
        { PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2, 0x00, 0x50, AMD_UDMA_100 },              /* NVIDIA nForce3s SATA2 */
	{ PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE, 0x00, 0x50, AMD_UDMA_133 },
	/* NVIDIA CK804 */

	{ 0 }
};

static struct amd_ide_chip *amd_config;
static unsigned char amd_enabled;
static unsigned int amd_80w;
static unsigned int amd_clock;

static unsigned char amd_cyc2udma[] = { 6, 6, 5, 4, 0, 1, 1, 2, 2, 3, 3 };
static unsigned char amd_udma2cyc[] = { 4, 6, 8, 10, 3, 2, 1, 1 };
static char *amd_dma[] = { "MWDMA16", "UDMA33", "UDMA66", "UDMA100", "UDMA133" };

/*
 * AMD /proc entry.
 */

#ifdef CONFIG_PROC_FS

#include <linux/stat.h>
#include <linux/proc_fs.h>

static unsigned long amd_base;
static struct pci_dev *bmide_dev;
extern int (*amd74xx_display_info)(char *, char **, off_t, int); /* ide-proc.c */

#define amd_print(format, arg...) p += sprintf(p, format "\n" , ## arg)
#define amd_print_drive(name, format, arg...)\
	p += sprintf(p, name); for (i = 0; i < 4; i++) p += sprintf(p, format, ## arg); p += sprintf(p, "\n");

static int amd74xx_get_info(char *buffer, char **addr, off_t offset, int count)
{
	int speed[4], cycle[4], setup[4], active[4], recover[4], den[4],
		 uen[4], udma[4], active8b[4], recover8b[4];
	struct pci_dev *dev = bmide_dev;
	unsigned int v, u, i;
	unsigned short c, w;
	unsigned char t;
	char *p = buffer;
	int len = 0;
#ifndef CONFIG_SMALL
	amd_print("----------AMD BusMastering IDE Configuration----------------");

	amd_print("Driver Version:                     2.10");
	amd_print("South Bridge:                       %s", bmide_dev->name);

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	amd_print("Revision:                           IDE %#x", t);
	amd_print("Highest DMA rate:                   %s", amd_dma[amd_config->flags & AMD_UDMA]);

	amd_print("BM-DMA base:                        %#lx", amd_base);
	amd_print("PCI clock:                          %d.%dMHz", amd_clock / 1000, amd_clock / 100 % 10);
	
	amd_print("-----------------------Primary IDE-------Secondary IDE------");

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	amd_print("Prefetch Buffer:       %10s%20s", (t & 0x80) ? "yes" : "no", (t & 0x20) ? "yes" : "no");
	amd_print("Post Write Buffer:     %10s%20s", (t & 0x40) ? "yes" : "no", (t & 0x10) ? "yes" : "no");

	pci_read_config_byte(dev, AMD_IDE_ENABLE, &t);
	amd_print("Enabled:               %10s%20s", (t & 0x02) ? "yes" : "no", (t & 0x01) ? "yes" : "no");

	c = inb(amd_base + 0x02) | (inb(amd_base + 0x0a) << 8);
	amd_print("Simplex only:          %10s%20s", (c & 0x80) ? "yes" : "no", (c & 0x8000) ? "yes" : "no");

	amd_print("Cable Type:            %10s%20s", (amd_80w & 1) ? "80w" : "40w", (amd_80w & 2) ? "80w" : "40w");

	if (!amd_clock)
                return p - buffer;

	amd_print("-------------------drive0----drive1----drive2----drive3-----");

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	pci_read_config_dword(dev, AMD_DRIVE_TIMING, &v);
	pci_read_config_word(dev, AMD_8BIT_TIMING, &w);
	pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);

	for (i = 0; i < 4; i++) {
		setup[i]     = ((t >> ((3 - i) << 1)) & 0x3) + 1;
		recover8b[i] = ((w >> ((1 - (i >> 1)) << 3)) & 0xf) + 1;
		active8b[i]  = ((w >> (((1 - (i >> 1)) << 3) + 4)) & 0xf) + 1;
		active[i]    = ((v >> (((3 - i) << 3) + 4)) & 0xf) + 1;
		recover[i]   = ((v >> ((3 - i) << 3)) & 0xf) + 1;

		udma[i] = amd_udma2cyc[((u >> ((3 - i) << 3)) & 0x7)];
		uen[i]  = ((u >> ((3 - i) << 3)) & 0x40) ? 1 : 0;
		den[i]  = (c & ((i & 1) ? 0x40 : 0x20) << ((i & 2) << 2));

		if (den[i] && uen[i] && udma[i] == 1) {
			speed[i] = amd_clock * 3;
			cycle[i] = 666666 / amd_clock;
			continue;
		}

		speed[i] = 4 * amd_clock / ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2);
		cycle[i] = 1000000 * ((den[i] && uen[i]) ? udma[i] : (active[i] + recover[i]) * 2) / amd_clock / 2;
	}

	amd_print_drive("Transfer Mode: ", "%10s", den[i] ? (uen[i] ? "UDMA" : "DMA") : "PIO");

	amd_print_drive("Address Setup: ", "%8dns", 1000000 * setup[i] / amd_clock);
	amd_print_drive("Cmd Active:    ", "%8dns", 1000000 * active8b[i] / amd_clock);
	amd_print_drive("Cmd Recovery:  ", "%8dns", 1000000 * recover8b[i] / amd_clock);
	amd_print_drive("Data Active:   ", "%8dns", 1000000 * active[i] / amd_clock);
	amd_print_drive("Data Recovery: ", "%8dns", 1000000 * recover[i] / amd_clock);
	amd_print_drive("Cycle Time:    ", "%8dns", cycle[i]);
	amd_print_drive("Transfer Rate: ", "%4d.%dMB/s", speed[i] / 1000, speed[i] / 100 % 10);

	/* hoping p - buffer is less than 4K... */
	len = (p - buffer) - offset;
	*addr = buffer + offset;
#endif	
	return len > count ? count : len;
}

#endif

/*
 * amd_set_speed() writes timing values to the chipset registers
 */

static void amd_set_speed(struct pci_dev *dev, unsigned char dn, struct ide_timing *timing)
{
	unsigned char t;

	pci_read_config_byte(dev, AMD_ADDRESS_SETUP, &t);
	t = (t & ~(3 << ((3 - dn) << 1))) | ((FIT(timing->setup, 1, 4) - 1) << ((3 - dn) << 1));
	pci_write_config_byte(dev, AMD_ADDRESS_SETUP, t);

	pci_write_config_byte(dev, AMD_8BIT_TIMING + (1 - (dn >> 1)),
		((FIT(timing->act8b, 1, 16) - 1) << 4) | (FIT(timing->rec8b, 1, 16) - 1));

	pci_write_config_byte(dev, AMD_DRIVE_TIMING + (3 - dn),
		((FIT(timing->active, 1, 16) - 1) << 4) | (FIT(timing->recover, 1, 16) - 1));

	switch (amd_config->flags & AMD_UDMA) {
		case AMD_UDMA_33:  t = timing->udma ? (0xc0 | (FIT(timing->udma, 2, 5) - 2)) : 0x03; break;
		case AMD_UDMA_66:  t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 2, 10)]) : 0x03; break;
		case AMD_UDMA_100: t = timing->udma ? (0xc0 | amd_cyc2udma[FIT(timing->udma, 1, 10)]) : 0x03; break;
		default: return;
	}

	pci_write_config_byte(dev, AMD_UDMA_TIMING + (3 - dn), t);
}

/*
 * amd_set_drive() computes timing values configures the drive and
 * the chipset to a desired transfer mode. It also can be called
 * by upper layers.
 */

static int amd_set_drive(ide_drive_t *drive, u8 speed)
{
	ide_drive_t *peer = HWIF(drive)->drives + (~drive->dn & 1);
	struct ide_timing t, p;
	int T, UT;

	if (speed != XFER_PIO_SLOW && speed != drive->current_speed)
		if (ide_config_drive_speed(drive, speed))
			printk(KERN_WARNING "ide%d: Drive %d didn't accept speed setting. Oh, well.\n",
				drive->dn >> 1, drive->dn & 1);

	T = 1000000000 / amd_clock;
	UT = T / min_t(int, max_t(int, amd_config->flags & AMD_UDMA, 1), 2);

	ide_timing_compute(drive, speed, &t, T, UT);

	if (peer->present) {
		ide_timing_compute(peer, peer->current_speed, &p, T, UT);
		ide_timing_merge(&p, &t, &t, IDE_TIMING_8BIT);
	}

	if (speed == XFER_UDMA_5 && amd_clock <= 33333) t.udma = 1;

	amd_set_speed(HWIF(drive)->pci_dev, drive->dn, &t);

	if (!drive->init_speed)	
		drive->init_speed = speed;
	drive->current_speed = speed;

	return 0;
}

/*
 * amd74xx_tune_drive() is a callback from upper layers for
 * PIO-only tuning.
 */

static void amd74xx_tune_drive(ide_drive_t *drive, u8 pio)
{
	if (!((amd_enabled >> HWIF(drive)->channel) & 1))
		return;

	if (pio == 255) {
		amd_set_drive(drive, ide_find_best_mode(drive, XFER_PIO | XFER_EPIO));
		return;
	}

	amd_set_drive(drive, XFER_PIO_0 + min_t(byte, pio, 5));
}

/*
 * amd74xx_dmaproc() is a callback from upper layers that can do
 * a lot, but we use it for DMA/PIO tuning only, delegating everything
 * else to the default ide_dmaproc().
 */

static int amd74xx_ide_dma_check(ide_drive_t *drive)
{
	int w80 = HWIF(drive)->udma_four;

	u8 speed = ide_find_best_mode(drive,
		XFER_PIO | XFER_EPIO | XFER_MWDMA | XFER_UDMA |
		((amd_config->flags & AMD_BAD_SWDMA) ? 0 : XFER_SWDMA) |
		(w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_66 ? XFER_UDMA_66 : 0) |
		(w80 && (amd_config->flags & AMD_UDMA) >= AMD_UDMA_100 ? XFER_UDMA_100 : 0));

	amd_set_drive(drive, speed);

	if (drive->autodma && (speed & XFER_MODE) != XFER_PIO)
		return HWIF(drive)->ide_dma_on(drive);
	return HWIF(drive)->ide_dma_off_quietly(drive);
}

/*
 * The initialization callback. Here we determine the IDE chip type
 * and initialize its drive independent registers.
 */

static unsigned int __init init_chipset_amd74xx(struct pci_dev *dev, const char *name)
{
	unsigned char t;
	unsigned int u;
	int i;

/*
 * Check for bad SWDMA.
 */

	if (amd_config->flags & AMD_CHECK_SWDMA) {
		pci_read_config_byte(dev, PCI_REVISION_ID, &t);
		if (t <= 7)
			amd_config->flags |= AMD_BAD_SWDMA;
	}

/*
 * Check 80-wire cable presence.
 */

	switch (amd_config->flags & AMD_UDMA) {

		case AMD_UDMA_100:
			pci_read_config_byte(dev, AMD_CABLE_DETECT, &t);
			pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);
			amd_80w = ((t & 0x3) ? 1 : 0) | ((t & 0xc) ? 2 : 0);
			for (i = 24; i >= 0; i -= 8)
				if (((u >> i) & 4) && !(amd_80w & (1 << (1 - (i >> 4))))) {
					printk(KERN_WARNING "AMD_IDE: Bios didn't set cable bits corectly. Enabling workaround.\n");
					amd_80w |= (1 << (1 - (i >> 4)));
				}
			break;

		case AMD_UDMA_66:
			pci_read_config_dword(dev, AMD_UDMA_TIMING, &u);
			for (i = 24; i >= 0; i -= 8)
				if ((u >> i) & 4)
					amd_80w |= (1 << (1 - (i >> 4)));
			break;
	}

	pci_read_config_dword(dev, AMD_IDE_ENABLE, &u);
	amd_enabled = ((u & 1) ? 2 : 0) | ((u & 2) ? 1 : 0);

/*
 * Take care of prefetch & postwrite.
 */

	pci_read_config_byte(dev, AMD_IDE_CONFIG, &t);
	pci_write_config_byte(dev, AMD_IDE_CONFIG,
		(amd_config->flags & AMD_BAD_FIFO) ? (t & 0x0f) : (t | 0xf0));

/*
 * Determine the system bus clock.
 */

	amd_clock = system_bus_clock() * 1000;

	switch (amd_clock) {
		case 33000: amd_clock = 33333; break;
		case 37000: amd_clock = 37500; break;
		case 41000: amd_clock = 41666; break;
	}

	if (amd_clock < 20000 || amd_clock > 50000) {
		printk(KERN_WARNING "AMD_IDE: User given PCI clock speed impossible (%d), using 33 MHz instead.\n", amd_clock);
		printk(KERN_WARNING "AMD_IDE: Use ide0=ata66 if you want to assume 80-wire cable\n");
		amd_clock = 33333;
	}

/*
 * Print the boot message.
 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &t);
	printk(KERN_INFO "AMD_IDE: %s (rev %02x) %s controller on pci%s\n",
		dev->name, t, amd_dma[amd_config->flags & AMD_UDMA], dev->slot_name);

/*
 * Register /proc/ide/amd74xx entry
 */

#if defined(DISPLAY_AMD_TIMINGS) && defined(CONFIG_PROC_FS)
        if (!amd74xx_proc) {
                amd_base = pci_resource_start(dev, 4);
                bmide_dev = dev;
                ide_pci_register_host_proc(&amd74xx_procs[0]);
                amd74xx_proc = 1;
        }
#endif /* DISPLAY_AMD_TIMINGS && CONFIG_PROC_FS */


	return 0;
}

static unsigned int __init ata66_amd74xx(ide_hwif_t *hwif)
{
	return ((amd_enabled & amd_80w) >> hwif->channel) & 1;
}

static void __init init_hwif_amd74xx(ide_hwif_t *hwif)
{
	int i;

	hwif->autodma = 0;

	hwif->tuneproc = &amd74xx_tune_drive;
	hwif->speedproc = &amd_set_drive;

	for (i = 0; i < 2; i++) {
		hwif->drives[i].io_32bit = 1;
		hwif->drives[i].unmask = 1;
		hwif->drives[i].autotune = 1;
		hwif->drives[i].dn = hwif->channel * 2 + i;
	}

	if (!hwif->dma_base)
		return;

        hwif->atapi_dma = 1;
        hwif->ultra_mask = 0x7f;
        hwif->mwdma_mask = 0x07;
        hwif->swdma_mask = 0x07;

        if (!(hwif->udma_four))
                hwif->udma_four = ((amd_enabled & amd_80w) >> hwif->channel) & 1;
        hwif->ide_dma_check = &amd74xx_ide_dma_check;
        if (!noautodma)
                hwif->autodma = 1;
        hwif->drives[0].autodma = hwif->autodma;
        hwif->drives[1].autodma = hwif->autodma;
}

/*
 * We allow the BM-DMA driver only work on enabled interfaces.
 */

static void __init init_dma_amd74xx(ide_hwif_t *hwif, unsigned long dmabase)
{
	if ((amd_enabled >> hwif->channel) & 1)
		ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static int __devinit amd74xx_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = amd74xx_chipsets + id->driver_data;
	amd_config = amd_ide_chips + id->driver_data;
	if (dev->device != d->device) BUG();
	if (dev->device != amd_config->id) BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id amd74xx_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_COBRA_7401,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7409,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7411,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_OPUS_7441,	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8111_IDE, 	PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 9},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12},
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 13},

	{ 0, },
};

static struct pci_driver driver = {
	.name		= "AMD IDE",
	.id_table	= amd74xx_pci_tbl,
	.probe		= amd74xx_probe,
};

static int amd74xx_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void amd74xx_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(amd74xx_ide_init);
module_exit(amd74xx_ide_exit);

MODULE_AUTHOR("Vojtech Pavlik");
MODULE_DESCRIPTION("AMD PCI IDE driver");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
