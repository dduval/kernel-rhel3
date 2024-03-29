/*
 * linux/drivers/ide/pci/alim15x3.c		Version 0.17	2003/03/05
 *
 *  Copyright (C) 1998-2000 Michel Aubry, Maintainer
 *  Copyright (C) 1998-2000 Andrzej Krzysztofowicz, Maintainer
 *  Copyright (C) 1999-2000 CJ, cjtsai@ali.com.tw, Maintainer
 *
 *  Copyright (C) 1998-2000 Andre Hedrick (andre@linux-ide.org)
 *  May be copied or modified under the terms of the GNU General Public License
 *  Copyright (C) 2002 Alan Cox <alan@redhat.com>
 *
 *  Changes for ALi 156x ported from RH 8.0 driver changes by
 *			Clear Zang
 *
 *  (U)DMA capable version of ali 1533/1543(C), 1535(D)
 *
 **********************************************************************
 *  9/7/99 --Parts from the above author are included and need to be
 *  converted into standard interface, once I finish the thought.
 *
 *  Recent changes
 *	Don't use LBA48 mode on ALi <= 0xC4
 *	Don't poke 0x79 with a non ALi northbridge
 *	Don't flip undefined bits on newer chipsets (fix Fujitsu laptop hang)
 *
 *  Documentation
 *	Chipset documentation available under NDA only
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"
#include "alim15x3.h"

/*
 *	ALi devices are not plug in. Otherwise these static values would
 *	need to go. They ought to go away anyway
 */
 
static u8 m5229_revision;
static u8 chip_is_1543c_e;
static struct pci_dev *isa_dev;

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 ali_proc = 0;

static struct pci_dev *bmide_dev;

static char *fifo[4] = {
	"FIFO Off",
	"FIFO On ",
	"DMA mode",
	"PIO mode" };

static char *udmaT[8] = {
	"1.5T",
	"  2T",
	"2.5T",
	"  3T",
	"3.5T",
	"  4T",
	"  6T",
	"  8T"
};

static char *channel_status[8] = {
	"OK            ",
	"busy          ",
	"DRQ           ",
	"DRQ busy      ",
	"error         ",
	"error busy    ",
	"error DRQ     ",
	"error DRQ busy"
};

/**
 *	ali_get_info		-	generate proc file for ALi IDE
 *	@buffer: buffer to fill
 *	@addr: address of user start in buffer
 *	@offset: offset into 'file'
 *	@count: buffer count
 *
 *	Walks the Ali devices and outputs summary data on the tuning and
 *	anything else that will help with debugging
 */
 
static int ali_get_info (char *buffer, char **addr, off_t offset, int count)
{
	unsigned long bibma;
	u8 reg53h, reg5xh, reg5yh, reg5xh1, reg5yh1, c0, c1, rev, tmp;
	char *q, *p = buffer;

#ifndef CONFIG_SMALL
	/* fetch rev. */
	pci_read_config_byte(bmide_dev, 0x08, &rev);
	if (rev >= 0xc1)	/* M1543C or newer */
		udmaT[7] = " ???";
	else
		fifo[3]  = "   ???  ";

	/* first fetch bibma: */
	
	bibma = pci_resource_start(bmide_dev, 4);

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte
	 * registers to investigate:
	 */
	c0 = inb(bibma + 0x02);
	c1 = inb(bibma + 0x0a);

	p += sprintf(p,
		"\n                                Ali M15x3 Chipset.\n");
	p += sprintf(p,
		"                                ------------------\n");
	pci_read_config_byte(bmide_dev, 0x78, &reg53h);
	p += sprintf(p, "PCI Clock: %d.\n", reg53h);

	pci_read_config_byte(bmide_dev, 0x53, &reg53h);
	p += sprintf(p,
		"CD_ROM FIFO:%s, CD_ROM DMA:%s\n",
		(reg53h & 0x02) ? "Yes" : "No ",
		(reg53h & 0x01) ? "Yes" : "No " );
	pci_read_config_byte(bmide_dev, 0x74, &reg53h);
	p += sprintf(p,
		"FIFO Status: contains %d Words, runs%s%s\n\n",
		(reg53h & 0x3f),
		(reg53h & 0x40) ? " OVERWR" : "",
		(reg53h & 0x80) ? " OVERRD." : "." );

	p += sprintf(p,
		"-------------------primary channel"
		"-------------------secondary channel"
		"---------\n\n");

	pci_read_config_byte(bmide_dev, 0x09, &reg53h);
	p += sprintf(p,
		"channel status:       %s"
		"                               %s\n",
		(reg53h & 0x20) ? "On " : "Off",
		(reg53h & 0x10) ? "On " : "Off" );

	p += sprintf(p,
		"both channels togth:  %s"
		"                               %s\n",
		(c0&0x80) ? "No " : "Yes",
		(c1&0x80) ? "No " : "Yes" );

	pci_read_config_byte(bmide_dev, 0x76, &reg53h);
	p += sprintf(p,
		"Channel state:        %s                    %s\n",
		channel_status[reg53h & 0x07],
		channel_status[(reg53h & 0x70) >> 4] );

	pci_read_config_byte(bmide_dev, 0x58, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5c, &reg5yh);
	p += sprintf(p,
		"Add. Setup Timing:    %dT"
		"                                %dT\n",
		(reg5xh & 0x07) ? (reg5xh & 0x07) : 8,
		(reg5yh & 0x07) ? (reg5yh & 0x07) : 8 );

	pci_read_config_byte(bmide_dev, 0x59, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5d, &reg5yh);
	p += sprintf(p,
		"Command Act. Count:   %dT"
		"                                %dT\n"
		"Command Rec. Count:   %dT"
		"                               %dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8, 
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16 );

	p += sprintf(p,
		"----------------drive0-----------drive1"
		"------------drive0-----------drive1------\n\n");
	p += sprintf(p,
		"DMA enabled:      %s              %s"
		"               %s              %s\n",
		(c0&0x20) ? "Yes" : "No ",
		(c0&0x40) ? "Yes" : "No ",
		(c1&0x20) ? "Yes" : "No ",
		(c1&0x40) ? "Yes" : "No " );

	pci_read_config_byte(bmide_dev, 0x54, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x55, &reg5yh);
	q = "FIFO threshold:   %2d Words         %2d Words"
		"          %2d Words         %2d Words\n";
	if (rev < 0xc1) {
		if ((rev == 0x20) &&
		    (pci_read_config_byte(bmide_dev, 0x4f, &tmp), (tmp &= 0x20))) {
			p += sprintf(p, q, 8, 8, 8, 8);
		} else {
			p += sprintf(p, q,
				(reg5xh & 0x03) + 12,
				((reg5xh & 0x30)>>4) + 12,
				(reg5yh & 0x03) + 12,
				((reg5yh & 0x30)>>4) + 12 );
		}
	} else {
		int t1 = (tmp = (reg5xh & 0x03)) ? (tmp << 3) : 4;
		int t2 = (tmp = ((reg5xh & 0x30)>>4)) ? (tmp << 3) : 4;
		int t3 = (tmp = (reg5yh & 0x03)) ? (tmp << 3) : 4;
		int t4 = (tmp = ((reg5yh & 0x30)>>4)) ? (tmp << 3) : 4;
		p += sprintf(p, q, t1, t2, t3, t4);
	}

#if 0
	p += sprintf(p, 
		"FIFO threshold:   %2d Words         %2d Words"
		"          %2d Words         %2d Words\n",
		(reg5xh & 0x03) + 12,
		((reg5xh & 0x30)>>4) + 12,
		(reg5yh & 0x03) + 12,
		((reg5yh & 0x30)>>4) + 12 );
#endif

	p += sprintf(p,
		"FIFO mode:        %s         %s          %s         %s\n",
		fifo[((reg5xh & 0x0c) >> 2)],
		fifo[((reg5xh & 0xc0) >> 6)],
		fifo[((reg5yh & 0x0c) >> 2)],
		fifo[((reg5yh & 0xc0) >> 6)] );

	pci_read_config_byte(bmide_dev, 0x5a, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5b, &reg5xh1);
	pci_read_config_byte(bmide_dev, 0x5e, &reg5yh);
	pci_read_config_byte(bmide_dev, 0x5f, &reg5yh1);

	p += sprintf(p,/*
		"------------------drive0-----------drive1"
		"------------drive0-----------drive1------\n")*/
		"Dt RW act. Cnt    %2dT              %2dT"
		"               %2dT              %2dT\n"
		"Dt RW rec. Cnt    %2dT              %2dT"
		"               %2dT              %2dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5xh1 & 0x70) ? ((reg5xh1 & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8,
		(reg5yh1 & 0x70) ? ((reg5yh1 & 0x70) >> 4) : 8,
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5xh1 & 0x0f) ? (reg5xh1 & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16,
		(reg5yh1 & 0x0f) ? (reg5yh1 & 0x0f) : 16 );

	p += sprintf(p,
		"-----------------------------------UDMA Timings"
		"--------------------------------\n\n");

	pci_read_config_byte(bmide_dev, 0x56, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x57, &reg5yh);
	p += sprintf(p,
		"UDMA:             %s               %s"
		"                %s               %s\n"
		"UDMA timings:     %s             %s"
		"              %s             %s\n\n",
		(reg5xh & 0x08) ? "OK" : "No",
		(reg5xh & 0x80) ? "OK" : "No",
		(reg5yh & 0x08) ? "OK" : "No",
		(reg5yh & 0x80) ? "OK" : "No",
		udmaT[(reg5xh & 0x07)],
		udmaT[(reg5xh & 0x70) >> 4],
		udmaT[reg5yh & 0x07],
		udmaT[(reg5yh & 0x70) >> 4] );

#endif
	return p-buffer; /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

/**
 *	ali15x3_tune_drive	-	set up a drive
 *	@drive: drive to tune
 *	@pio: unused
 *
 *	Select the best PIO timing for the drive in question. Then
 *	program the controller for this drive set up
 */
 
static void ali15x3_tune_drive (ide_drive_t *drive, u8 pio)
{
	ide_pio_data_t d;
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	int s_time, a_time, c_time;
	u8 s_clc, a_clc, r_clc;
	unsigned long flags;
	int bus_speed = system_bus_clock();
	int port = hwif->channel ? 0x5c : 0x58;
	int portFIFO = hwif->channel ? 0x55 : 0x54;
	u8 cd_dma_fifo = 0;
	int unit = drive->select.b.unit & 1;

	pio = ide_get_best_pio_mode(drive, pio, 5, &d);
	s_time = ide_pio_timings[pio].setup_time;
	a_time = ide_pio_timings[pio].active_time;
	if ((s_clc = (s_time * bus_speed + 999) / 1000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * bus_speed + 999) / 1000) >= 8)
		a_clc = 0;
	c_time = ide_pio_timings[pio].cycle_time;

#if 0
	if ((r_clc = ((c_time - s_time - a_time) * bus_speed + 999) / 1000) >= 16)
		r_clc = 0;
#endif

	if (!(r_clc = (c_time * bus_speed + 999) / 1000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
			r_clc = 0;
	}
	local_irq_save(flags);
	
	/* 
	 * PIO mode => ATA FIFO on, ATAPI FIFO off
	 */
	pci_read_config_byte(dev, portFIFO, &cd_dma_fifo);
	if (drive->media==ide_disk) {
		if (unit) {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0x0F) | 0x50);
		} else {
			pci_write_config_byte(dev, portFIFO, (cd_dma_fifo & 0xF0) | 0x05);
		}
	} else {
		if (unit) {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0x0F);
		} else {
			pci_write_config_byte(dev, portFIFO, cd_dma_fifo & 0xF0);
		}
	}
	
	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);
	local_irq_restore(flags);

	/*
	 * setup   active  rec
	 * { 70,   165,    365 },   PIO Mode 0
	 * { 50,   125,    208 },   PIO Mode 1
	 * { 30,   100,    110 },   PIO Mode 2
	 * { 30,   80,     70  },   PIO Mode 3 with IORDY
	 * { 25,   70,     25  },   PIO Mode 4 with IORDY  ns
	 * { 20,   50,     30  }    PIO Mode 5 with IORDY (nonstandard)
	 */

}

/**
 *	ali15x3_can_ultra	-	check for ultra DMA support
 *	@drive: drive to do the check
 *
 *	Check the drive and controller revisions. Return 0 if UDMA is
 *	not available, or 1 if UDMA can be used. The actual rules for
 *	the ALi are
 *		No UDMA on revisions <= 0xC1
 *		Disk only for revisions < 0xC2
 *		Not WDC drives for revisions < 0xC2
 *
 *	FIXME: WDC ifdef needs to die
 */
 
static u8 ali15x3_can_ultra (ide_drive_t *drive)
{
#ifndef CONFIG_WDC_ALI15X3
	struct hd_driveid *id	= drive->id;
#endif /* CONFIG_WDC_ALI15X3 */

	if (m5229_revision < 0xC1) {	/* According to ALi */
		return 0;
	} else if ((m5229_revision < 0xC2) &&
#ifndef CONFIG_WDC_ALI15X3
		   ((chip_is_1543c_e && strstr(id->model, "WDC ")) ||
		    (drive->media!=ide_disk))) {
#else /* CONFIG_WDC_ALI15X3 */
		   (drive->media!=ide_disk)) {
#endif /* CONFIG_WDC_ALI15X3 */
		return 0;
	} else {
		return 1;
	}
}

/**
 *	ali15x3_ratemask	-	generate DMA mode list
 *	@drive: drive to compute against
 *
 *	Generate a list of the available DMA modes for the drive. 
 *	FIXME: this function contains lots of bogus masking we can dump
 *
 *	Return the highest available mode (UDMA33, UDMA66, UDMA100,..)
 */
 
static u8 ali15x3_ratemask (ide_drive_t *drive)
{
	u8 mode = 0, can_ultra	= ali15x3_can_ultra(drive);

	if (m5229_revision >= 0xC4 && can_ultra) {
		mode = 4;
	} else if (m5229_revision >= 0xC2 && can_ultra) {
		mode = 2;
	} else if (can_ultra) {
		return 1;
	} else {
		return 0;
	}

	/*
	 *	If the drive sees no suitable cable then UDMA 33
	 *	is the highest permitted mode
	 */
	 
	if (!eighty_ninty_three(drive))
		mode = min(mode, (u8)1);
	return mode;
}

/**
 *	ali15x3_tune_chipset	-	set up chiset for new speed
 *	@drive: drive to configure for
 *	@xferspeed: desired speed
 *
 *	Configure the hardware for the desired IDE transfer mode.
 *	We also do the needed drive configuration through helpers
 */
 
static int ali15x3_tune_chipset (ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 speed		= ide_rate_filter(ali15x3_ratemask(drive), xferspeed);
	u8 speed1		= speed;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 tmpbyte		= 0x00;
	int m5229_udma		= (hwif->channel) ? 0x57 : 0x56;

	if (speed == XFER_UDMA_6)
	{
		speed1 = 0x47;
		if(m5229_revision == 0xC4)
			speed1 --;
	}
	if (speed < XFER_UDMA_0) {
		u8 ultra_enable	= (unit) ? 0x7f : 0xf7;
		/*
		 * clear "ultra enable" bit
		 */
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= ultra_enable;
		pci_write_config_byte(dev, m5229_udma, tmpbyte);

		if (speed < XFER_SW_DMA_0)
			ali15x3_tune_drive(drive, speed);
	} else {
		pci_read_config_byte(dev, m5229_udma, &tmpbyte);
		tmpbyte &= (0x0f << ((1-unit) << 2));
		/*
		 * enable ultra dma and set timing
		 */
		tmpbyte |= ((0x08 | ((4-speed1)&0x07)) << (unit << 2));
		pci_write_config_byte(dev, m5229_udma, tmpbyte);
		if (speed >= XFER_UDMA_3) {
			pci_read_config_byte(dev, 0x4b, &tmpbyte);
			tmpbyte |= 1;
			pci_write_config_byte(dev, 0x4b, tmpbyte);
		}
	}
	return (ide_config_drive_speed(drive, speed));
}


/**
 *	config_chipset_for_dma	-	set up DMA mode
 *	@drive: drive to configure for
 *
 *	Place a drive into DMA mode and tune the chipset for
 *	the selected speed.
 *
 *	Returns true if DMA mode can be used
 */
 
static int config_chipset_for_dma (ide_drive_t *drive)
{
	u8 speed = ide_dma_speed(drive, ali15x3_ratemask(drive));

	if (!(speed))
		return 0;

	(void) ali15x3_tune_chipset(drive, speed);
	return ide_dma_enable(drive);
}

/**
 *	ali15x3_config_drive_for_dma	-	configure for DMA
 *	@drive: drive to configure
 *
 *	Configure a drive for DMA operation. If DMA is not possible we
 *	drop the drive into PIO mode instead.
 *
 *	FIXME: exactly what are we trying to return here
 */
 
static int ali15x3_config_drive_for_dma(ide_drive_t *drive)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct hd_driveid *id	= drive->id;

	if ((m5229_revision<=0x20) && (drive->media!=ide_disk))
		return hwif->ide_dma_off_quietly(drive);

	drive->init_speed = 0;

	if ((id->capability & 1) != 0 && drive->autodma) {
		/* Consult the list of known "bad" drives */
		if (hwif->ide_dma_bad_drive(drive))
			goto ata_pio;
		if ((id->field_valid & 4) && (m5229_revision >= 0xC2)) {
			if (id->dma_ultra & hwif->ultra_mask) {
				/* Force if Capable UltraDMA */
				int dma = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) && !dma)
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & hwif->mwdma_mask) ||
			    (id->dma_1word & hwif->swdma_mask)) {
				/* Force if Capable regular DMA modes */
				if (!config_chipset_for_dma(drive))
					goto no_dma_set;
			}
		} else if (hwif->ide_dma_good_drive(drive) &&
			   (id->eide_dma_time < 150)) {
			/* Consult the list of known "good" drives */
			if (!config_chipset_for_dma(drive))
				goto no_dma_set;
		} else {
			goto ata_pio;
		}
	} else {
ata_pio:
		hwif->tuneproc(drive, 255);
no_dma_set:
		return hwif->ide_dma_off_quietly(drive);
	}
	return hwif->ide_dma_on(drive);
}

/**
 *	ali15x3_dma_write	-	do a DMA IDE write
 *	@drive:	drive to issue write for
 *
 *	Returns 1 if the DMA write cannot be performed, zero on 
 *	success.
 */
 
static int ali15x3_dma_write (ide_drive_t *drive)
{
	if ((m5229_revision < 0xC2) && (drive->media != ide_disk))
		return 1;	/* try PIO instead of DMA */
	return __ide_dma_write(drive);
}

/**
 *	init_chipset_ali15x3	-	Initialise an ALi IDE controller
 *	@dev: PCI device
 *	@name: Name of the controller
 *
 *	This function initializes the ALI IDE controller and where 
 *	appropriate also sets up the 1533 southbridge.
 */
  
static unsigned int __init init_chipset_ali15x3 (struct pci_dev *dev, const char *name)
{
	unsigned long flags;
	u8 tmpbyte;
	struct pci_dev *north = pci_find_slot(0, PCI_DEVFN(0,0));

	pci_read_config_byte(dev, PCI_REVISION_ID, &m5229_revision);

	isa_dev = pci_find_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!ali_proc) {
		ali_proc = 1;
		bmide_dev = dev;
		ide_pci_register_host_proc(&ali_procs[0]);
	}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

	local_irq_save(flags);

	if (m5229_revision < 0xC2) {
		/*
		 * revision 0x20 (1543-E, 1543-F)
		 * revision 0xC0, 0xC1 (1543C-C, 1543C-D, 1543C-E)
		 * clear CD-ROM DMA write bit, m5229, 0x4b, bit 7
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		/*
		 * clear bit 7
		 */
		pci_write_config_byte(dev, 0x4b, tmpbyte & 0x7F);
		local_irq_restore(flags);
		return 0;
	}

	/*
	 * 1543C-B?, 1535, 1535D, 1553
	 * Note 1: not all "motherboard" support this detection
	 * Note 2: if no udma 66 device, the detection may "error".
	 *         but in this case, we will not set the device to
	 *         ultra 66, the detection result is not important
	 */

	/*
	 * enable "Cable Detection", m5229, 0x4b, bit3
	 */
	pci_read_config_byte(dev, 0x4b, &tmpbyte);
	pci_write_config_byte(dev, 0x4b, tmpbyte | 0x08);

	/*
	 * We should only tune the 1533 enable if we are using an ALi
	 * North bridge. We might have no north found on some zany
	 * box without a device at 0:0.0. The ALi bridge will be at
	 * 0:0.0 so if we didn't find one we know what is cooking.
	 */
	if (north && north->vendor != PCI_VENDOR_ID_AL) {
		local_irq_restore(flags);
	        return 0;
	}

	if (m5229_revision < 0xC5 && isa_dev)
	{	
		/*
		 * set south-bridge's enable bit, m1533, 0x79
		 */

		pci_read_config_byte(isa_dev, 0x79, &tmpbyte);
		if (m5229_revision == 0xC2) {
			/*
			 * 1543C-B0 (m1533, 0x79, bit 2)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x04);
		} else if (m5229_revision >= 0xC3) {
			/*
			 * 1553/1535 (m1533, 0x79, bit 1)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x02);
		}
	}
	local_irq_restore(flags);
	return 0;
}

/**
 *	ata66_ali15x3	-	check for UDMA 66 support
 *	@hwif: IDE interface
 *
 *	This checks if the controller and the cable are capable
 *	of UDMA66 transfers. It doesn't check the drives.
 */

static unsigned int __init ata66_ali15x3 (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int ata66	= 0;
	u8 cable_80_pin[2]	= { 0, 0 };

	unsigned long flags;
	u8 tmpbyte;

	/* Revision C5 and higher we must rely on drive side */
	if (m5229_revision >= 0xC5)
		return 1;
		
	local_irq_save(flags);
	
	if (m5229_revision >= 0xC2) {
		/*
		 * Ultra66 cable detection (from Host View)
		 * m5229, 0x4a, bit0: primary, bit1: secondary 80 pin
		 */
		pci_read_config_byte(dev, 0x4a, &tmpbyte);
		/*
		 * 0x4a, bit0 is 0 => primary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x01)) cable_80_pin[0] = 1;
		/*
		 * 0x4a, bit1 is 0 => secondary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x02)) cable_80_pin[1] = 1;
		/*
		 * Allow ata66 if cable of current channel has 80 pins
		 */
		ata66 = (hwif->channel)?cable_80_pin[1]:cable_80_pin[0];
	} else {
		/*
		 * check m1533, 0x5e, bit 1~4 == 1001 => & 00011110 = 00010010
		 */
		pci_read_config_byte(isa_dev, 0x5e, &tmpbyte);
		chip_is_1543c_e = ((tmpbyte & 0x1e) == 0x12) ? 1: 0;
	}

	/*
	 * CD_ROM DMA on (m5229, 0x53, bit0)
	 *      Enable this bit even if we want to use PIO
	 * PIO FIFO off (m5229, 0x53, bit1)
	 *      The hardware will use 0x54h and 0x55h to control PIO FIFO
	 *	(Not on later devices it seems)
	 *
	 *	0x53 changes meaning on later revs - we must no touch
	 *	bit 1 on them. Need to check if 0x20 is the right break
	 */
	 
	pci_read_config_byte(dev, 0x53, &tmpbyte);
	
	if(m5229_revision <= 0x20)
		tmpbyte = (tmpbyte & (~0x02)) | 0x01;
	else
		tmpbyte |= 0x01;

	pci_write_config_byte(dev, 0x53, tmpbyte);

	local_irq_restore(flags);

	return(ata66);
}

/**
 *	init_hwif_common_ali15x3	-	Set up ALI IDE hardware
 *	@hwif: IDE interface
 *
 *	Initialize the IDE structure side of the ALi 15x3 driver.
 */
 
static void __init init_hwif_common_ali15x3 (ide_hwif_t *hwif)
{
	hwif->autodma = 0;
	hwif->tuneproc = &ali15x3_tune_drive;
	hwif->speedproc = &ali15x3_tune_chipset;

	/* Don't use LBA48 on ALi devices before rev 0xC5 */
	hwif->addressing = (m5229_revision <= 0xC4) ? 1 : 0;

	if (!hwif->dma_base) {
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}

	hwif->atapi_dma = 1;

	if (m5229_revision > 0x20)
		hwif->ultra_mask = 0x3f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

        if (m5229_revision >= 0x20) {
                /*
                 * M1543C or newer for DMAing
                 */
                hwif->ide_dma_check = &ali15x3_config_drive_for_dma;
                hwif->ide_dma_write = &ali15x3_dma_write;
		if (!noautodma)
			hwif->autodma = 1;
		if (!(hwif->udma_four))
			hwif->udma_four = ata66_ali15x3(hwif);
	}
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

/**
 *	init_hwif_ali15x3	-	Initialize the ALI IDE x86 stuff
 *	@hwif: interface to configure
 *
 *	Obtain the IRQ tables for an ALi based IDE solution on the PC
 *	class platforms. This part of the code isn't applicable to the
 *	Sparc systems
 */

static void __init init_hwif_ali15x3 (ide_hwif_t *hwif)
{
	u8 ideic, inmir;
	s8 irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };
	int irq = -1;
	
	hwif->irq = hwif->channel ? 15 : 14;

	if (isa_dev) {
		/*
		 * read IDE interface control
		 */
		pci_read_config_byte(isa_dev, 0x58, &ideic);

		/* bit0, bit1 */
		ideic = ideic & 0x03;

		/* get IRQ for IDE Controller */
		if ((hwif->channel && ideic == 0x03) ||
		    (!hwif->channel && !ideic)) {
			/*
			 * get SIRQ1 routing table
			 */
			pci_read_config_byte(isa_dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			irq = irq_routing_table[inmir];
		} else if (hwif->channel && !(ideic & 0x01)) {
			/*
			 * get SIRQ2 routing table
			 */
			pci_read_config_byte(isa_dev, 0x75, &inmir);
			inmir = inmir & 0x0f;
			irq = irq_routing_table[inmir];
		}
		if(irq >= 0)
			hwif->irq = irq;
	}

	init_hwif_common_ali15x3(hwif);
}

/**
 *	init_dma_ali15x3	-	set up DMA on ALi15x3
 *	@hwif: IDE interface
 *	@dmabase: DMA interface base PCI address
 *
 *	Set up the DMA functionality on the ALi 15x3. For the ALi
 *	controllers this is generic so we can let the generic code do
 *	the actual work.
 */

static void __init init_dma_ali15x3 (ide_hwif_t *hwif, unsigned long dmabase)
{
	if (m5229_revision < 0x20)
		return;
	if (!(hwif->channel))
		hwif->OUTB(hwif->INB(dmabase+2) & 0x60, dmabase+2);
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);


/**
 *	alim15x3_init_one	-	set up an ALi15x3 IDE controller
 *	@dev: PCI device to set up
 *
 *	Perform the actual set up for an ALi15x3 that has been found by the
 *	hot plug layer.
 */
 
static int __devinit alim15x3_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &ali15x3_chipsets[id->driver_data];
	
	if(pci_find_device(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_RADEON_IGP, NULL))
		printk(KERN_ERR "Warning: ATI Radeon IGP Northbridge is not yet fully tested.\n");

#if defined(CONFIG_SPARC64)
	d->init_hwif = init_hwif_common_ali15x3;
#endif /* CONFIG_SPARC64 */
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}


static struct pci_device_id alim15x3_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5229, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "ALI15x3 IDE",
	.id_table	= alim15x3_pci_tbl,
	.probe		= alim15x3_init_one,
};

static int ali15x3_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void ali15x3_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(ali15x3_ide_init);
module_exit(ali15x3_ide_exit);

MODULE_AUTHOR("Michael Aubry, Andrzej Krzysztofowicz, CJ, Andre Hedrick, Alan Cox");
MODULE_DESCRIPTION("PCI driver module for ALi 15x3 IDE");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
