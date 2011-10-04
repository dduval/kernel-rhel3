/*
   Copyright 2003 Red Hat, Inc.  All rights reserved.
   Copyright 2003 Jeff Garzik

   The contents of this file are subject to the Open
   Software License version 1.1 that can be found at
   http://www.opensource.org/licenses/osl-1.1.txt and is included herein
   by reference.

   Alternatively, the contents of this file may be used under the terms
   of the GNU General Public License version 2 (the "GPL") as distributed
   in the kernel source COPYING file, in which case the provisions of
   the GPL are applicable instead of the above.  If you wish to allow
   the use of your version of this file only under the terms of the
   GPL and not to allow others to use your version of this file under
   the OSL, indicate your decision by deleting the provisions above and
   replace them with the notice and other provisions required by the GPL.
   If you do not delete the provisions above, a recipient may use your
   version of this file under either the OSL or the GPL.


   TODO:
	ATAPI.
	ATAPI error handling.
	Better ATA error handling.
	request_region for legacy ATA ISA addresses.
	scsi SYNCHRONIZE CACHE and associated mode page
	other minor items, too many to list
	replace THR_*_POLL states with calls to a poll function
	use set-mult-mode/rw mult for faster PIO

 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/blk.h>
#include <scsi/scsi.h>
#include "scsi.h"
#include "hosts.h"
#include <linux/ata.h>
#include <asm/io.h>
#include <asm/semaphore.h>

#define DRV_NAME	"libata"
#define DRV_VERSION	"0.70"	/* must be exactly four chars */

struct ata_scsi_args {
	struct ata_port		*ap;
	struct ata_device	*dev;
	Scsi_Cmnd		*cmd;
	void			(*done)(Scsi_Cmnd *);
};

static void ata_to_sense_error(struct ata_queued_cmd *qc, Scsi_Cmnd *cmd);
static void ata_thread_wake(struct ata_port *ap, unsigned int thr_state);
static void ata_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
			    u8 drv_stat, unsigned int done_late,
			    unsigned int sg_clean);
static void atapi_cdb_send(struct ata_port *ap);
static inline void ata_bad_cdb(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout);
static u8 __ata_dev_select (struct ata_ioports *ioaddr, unsigned int device);
static void ata_qc_push (struct ata_queued_cmd *qc, unsigned int append);
static void ata_dma_start (struct ata_port *ap, struct ata_queued_cmd *qc);

static unsigned int ata_unique_id = 1;
static LIST_HEAD(ata_probe_list);
static spinlock_t ata_module_lock = SPIN_LOCK_UNLOCKED;
static const unsigned int use_software_reset = 0;

MODULE_AUTHOR("Jeff Garzik");
MODULE_DESCRIPTION("Library module for running ATA devices underneath SCSI");
MODULE_LICENSE("GPL");

static const char * thr_state_name[] = {
	"THR_UNKNOWN",
	"THR_CHECKPORT",
	"THR_BUS_RESET",
	"THR_AWAIT_DEATH",
	"THR_IDENTIFY",
	"THR_CONFIG_TIMINGS",
	"THR_CONFIG_DMA",
	"THR_PROBE_FAILED",
	"THR_IDLE",
	"THR_PROBE_SUCCESS",
	"THR_PROBE_START",
	"THR_CONFIG_FORCE_PIO",
	"THR_PIO_POLL",
	"THR_PIO_TMOUT",
	"THR_PIO",
	"THR_PIO_LAST",
	"THR_PIO_LAST_POLL",
	"THR_PIO_ERR",
	"THR_PACKET",
};

/**
 *	ata_thr_state_name - convert thread state enum to string
 *	@thr_state: thread state to be converted to string
 *
 *	Converts the specified thread state id to a constant C string.
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	The THR_xxx-prefixed string naming the specified thread
 *	state id, or the string "<invalid THR_xxx state>".
 */

static const char *ata_thr_state_name(unsigned int thr_state)
{
	if (thr_state < ARRAY_SIZE(thr_state_name))
		return thr_state_name[thr_state];
	return "<invalid THR_xxx state>";
}

/**
 *	msleep - sleep for a number of milliseconds
 *	@msecs: number of milliseconds to sleep
 *
 *	Issues schedule_timeout call for the specified number
 *	of milliseconds.
 *
 *	LOCKING:
 *	None.
 */

static void msleep(unsigned long msecs)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(msecs));
}

/**
 *	ata_tf_load_pio - send taskfile registers to host controller
 *	@ioaddr: set of IO ports to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_load_pio(struct ata_ioports *ioaddr, struct ata_taskfile *tf)
{
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	outb(tf->ctl, ioaddr->ctl_addr);

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		outb(tf->hob_feature, ioaddr->cmd_addr + ATA_REG_FEATURE);
		outb(tf->hob_nsect, ioaddr->cmd_addr + ATA_REG_NSECT);
		outb(tf->hob_lbal, ioaddr->cmd_addr + ATA_REG_LBAL);
		outb(tf->hob_lbam, ioaddr->cmd_addr + ATA_REG_LBAM);
		outb(tf->hob_lbah, ioaddr->cmd_addr + ATA_REG_LBAH);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		outb(tf->feature, ioaddr->cmd_addr + ATA_REG_FEATURE);
		outb(tf->nsect, ioaddr->cmd_addr + ATA_REG_NSECT);
		outb(tf->lbal, ioaddr->cmd_addr + ATA_REG_LBAL);
		outb(tf->lbam, ioaddr->cmd_addr + ATA_REG_LBAM);
		outb(tf->lbah, ioaddr->cmd_addr + ATA_REG_LBAH);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		outb(tf->device, ioaddr->cmd_addr + ATA_REG_DEVICE);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ioaddr);
}

/**
 *	__ata_exec - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *	@bus_state: new BUS_xxx state entered when command is issued
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static inline void __ata_exec(struct ata_port *ap, struct ata_taskfile *tf,
			   unsigned int bus_state)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);
	ap->bus_state = bus_state;
	outb(tf->command, ap->ioaddr.cmd_addr + ATA_REG_CMD);
	ata_pause(&ap->ioaddr);
}

/**
 *	ata_exec - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *	@bus_state: new BUS_xxx state entered when command is issued
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	Obtains host_set lock.
 */

static inline void ata_exec(struct ata_port *ap, struct ata_taskfile *tf,
			   unsigned int bus_state)
{
	unsigned long flags;

	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);
	spin_lock_irqsave(&ap->host_set->lock, flags);
	__ata_exec(ap, tf, bus_state);
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
}

/**
 *	ata_tf_to_host - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *	@bus_state: new BUS_xxx state entered when command is issued
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	via PIO, with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	Obtains host_set lock.
 */

static void ata_tf_to_host(struct ata_port *ap, struct ata_taskfile *tf,
			   unsigned int bus_state)
{
	init_MUTEX_LOCKED(&ap->sem);

	ap->ops->tf_load(&ap->ioaddr, tf);

	ata_exec(ap, tf, bus_state);
}

/**
 *	ata_tf_to_host_nolock - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *	@bus_state: new BUS_xxx state entered when command is issued
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	via PIO, with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_tf_to_host_nolock(struct ata_port *ap, struct ata_taskfile *tf,
			   unsigned int bus_state)
{
	init_MUTEX_LOCKED(&ap->sem);

	ap->ops->tf_load(&ap->ioaddr, tf);

	__ata_exec(ap, tf, bus_state);
}

/**
 *	ata_tf_from_host - input device's ATA taskfile shadow registers
 *	@ioaddr: set of IO ports from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf via PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_from_host(struct ata_ioports *ioaddr,
			     struct ata_taskfile *tf)
{
	if (tf->flags & ATA_TFLAG_DATAREG) {
		u16 data = inw(ioaddr->cmd_addr + ATA_REG_DATA);
		tf->data = data & 0xff;
		tf->hob_data = data >> 8;
	}

	tf->nsect = inb(ioaddr->cmd_addr + ATA_REG_NSECT);
	tf->lbal = inb(ioaddr->cmd_addr + ATA_REG_LBAL);
	tf->lbam = inb(ioaddr->cmd_addr + ATA_REG_LBAM);
	tf->lbah = inb(ioaddr->cmd_addr + ATA_REG_LBAH);
	tf->device = inb(ioaddr->cmd_addr + ATA_REG_DEVICE);

	if (tf->flags & ATA_TFLAG_LBA48) {
		outb(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = inb(ioaddr->cmd_addr + ATA_REG_FEATURE);
		tf->hob_nsect = inb(ioaddr->cmd_addr + ATA_REG_NSECT);
		tf->hob_lbal = inb(ioaddr->cmd_addr + ATA_REG_LBAL);
		tf->hob_lbam = inb(ioaddr->cmd_addr + ATA_REG_LBAM);
		tf->hob_lbah = inb(ioaddr->cmd_addr + ATA_REG_LBAH);
	}
}

static const char * udma_str[] = {
	"UDMA/16",
	"UDMA/25",
	"UDMA/33",
	"UDMA/44",
	"UDMA/66",
	"UDMA/100",
	"UDMA/133",
	"UDMA7",
};

/**
 *	ata_udma_string - convert UDMA bit offset to string
 *	@udma_mask: mask of bits supported; only highest bit counts.
 *
 *	Determine string which represents the highest speed
 *	(highest bit in @udma_mask).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Constant C string representing highest speed listed in
 *	@udma_mask, or the constant C string "<n/a>".
 */

static const char *ata_udma_string(unsigned int udma_mask)
{
	unsigned int i;

	for (i = 7; i >= 0; i--) {
		if (udma_mask & (1 << i))
			return udma_str[i];
	}

	return "<n/a>";
}

/**
 *	ata_dev_devchk -
 *	@ioaddr:
 *	@device:
 *
 *	LOCKING:
 *
 */

static unsigned int ata_dev_devchk(struct ata_ioports *ioaddr,
				   unsigned int device)
{
	u8 nsect, lbal;

	__ata_dev_select(ioaddr, device);

	outb(0x55, ioaddr->cmd_addr + ATA_REG_NSECT);
	outb(0xaa, ioaddr->cmd_addr + ATA_REG_LBAL);

	outb(0xaa, ioaddr->cmd_addr + ATA_REG_NSECT);
	outb(0x55, ioaddr->cmd_addr + ATA_REG_LBAL);

	outb(0x55, ioaddr->cmd_addr + ATA_REG_NSECT);
	outb(0xaa, ioaddr->cmd_addr + ATA_REG_LBAL);

	nsect = inb(ioaddr->cmd_addr + ATA_REG_NSECT);
	lbal = inb(ioaddr->cmd_addr + ATA_REG_LBAL);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_dev_classify - determine device type based on ATA-spec signature
 *	@tf: ATA taskfile register set for device to be identified
 *
 *	Determine from taskfile register contents whether a device is
 *	ATA or ATAPI, as per "Signature and persistence" section
 *	of ATA/PI spec (volume 1, sect 5.14).
 *
 *	LOCKING:
 *	None.
 *
 *	RETURNS:
 *	Device type, %ATA_DEV_ATA, %ATA_DEV_ATAPI, or %ATA_DEV_UNKNOWN
 *	the event of failure.
 */

static unsigned int ata_dev_classify(struct ata_taskfile *tf)
{
	/* Apple's open source Darwin code hints that some devices only
	 * put a proper signature into the LBA mid/high registers,
	 * So, we only check those.  It's sufficient for uniqueness.
	 */

	if (((tf->lbam == 0) && (tf->lbah == 0)) ||
	    ((tf->lbam == 0x3c) && (tf->lbah == 0xc3))) {
		DPRINTK("found ATA device by sig\n");
		return ATA_DEV_ATA;
	}

	if (((tf->lbam == 0x14) && (tf->lbah == 0xeb)) ||
	    ((tf->lbam == 0x69) && (tf->lbah == 0x96))) {
		DPRINTK("found ATAPI device by sig\n");
		return ATA_DEV_ATAPI;
	}

	DPRINTK("unknown device\n");
	return ATA_DEV_UNKNOWN;
}

/**
 *	ata_dev_try_classify -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 *
 */

static u8 ata_dev_try_classify(struct ata_port *ap, unsigned int device,
			       unsigned int maybe_have_dev)
{
	struct ata_device *dev = &ap->device[device];
	struct ata_taskfile tf;
	unsigned int class;
	u8 err;

	__ata_dev_select(&ap->ioaddr, device);

	memset(&tf, 0, sizeof(tf));

	err = inb(ap->ioaddr.cmd_addr + ATA_REG_ERR);
	ata_tf_from_host(&ap->ioaddr, &tf);

	dev->class = ATA_DEV_NONE;

	/* see if device passed diags */
	if (err == 1)
		/* do nothing */ ;
	else if ((device == 0) && (err == 0x81))
		/* do nothing */ ;
	else
		return err;

	/* determine if device if ATA or ATAPI */
	class = ata_dev_classify(&tf);
	if (class == ATA_DEV_UNKNOWN)
		return err;
	if ((class == ATA_DEV_ATA) && (ata_chk_status(&ap->ioaddr) == 0))
		return err;

	dev->class = class;

	return err;
}

/**
 *	ata_dev_id_string -
 *	@dev:
 *	@s:
 *	@ofs:
 *	@len:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static unsigned int ata_dev_id_string(struct ata_device *dev, unsigned char *s,
				      unsigned int ofs, unsigned int len)
{
	unsigned int c, ret = 0;

	while (len > 0) {
		c = dev->id[ofs] >> 8;
		*s = c;
		s++;

		ret = c = dev->id[ofs] & 0xff;
		*s = c;
		s++;

		ofs++;
		len -= 2;
	}

	return ret;
}

/**
 *	ata_dev_parse_strings -
 *	@dev:
 *
 *	LOCKING:
 */

static void ata_dev_parse_strings(struct ata_device *dev)
{
	assert (dev->class == ATA_DEV_ATA);
	memcpy(dev->vendor, "ATA     ", 8);

	ata_dev_id_string(dev, dev->product, ATA_ID_PROD_OFS,
			  sizeof(dev->product));
}

/**
 *	__ata_dev_select -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 *
 */

static u8 __ata_dev_select (struct ata_ioports *ioaddr, unsigned int device)
{
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	outb(tmp, ioaddr->cmd_addr + ATA_REG_DEVICE);
	ata_pause(ioaddr);

	return tmp;
}

/**
 *	ata_dev_select -
 *	@ap:
 *	@device:
 *	@wait:
 *	@can_sleep:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static void ata_dev_select(struct ata_port *ap, unsigned int device,
			   unsigned int wait, unsigned int can_sleep)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	VPRINTK("ENTER, ata%u: device %u, wait %u\n",
		ap->id, device, wait);

	if (wait)
		ata_wait_idle(ioaddr);

	ap->devsel = __ata_dev_select(ioaddr, device);

	if (wait) {
		if (can_sleep && ap->device[device].class == ATA_DEV_ATAPI)
			msleep(150);
		ata_wait_idle(ioaddr);
	}

	assert(ap->bus_state == BUS_IDLE);
}

/**
 *	ata_dump_id -
 *	@dev:
 *
 *	LOCKING:
 */

static inline void ata_dump_id(struct ata_device *dev)
{
	DPRINTK("49==0x%04x  "
		"53==0x%04x  "
		"63==0x%04x  "
		"64==0x%04x  "
		"75==0x%04x  \n",
		dev->id[49],
		dev->id[53],
		dev->id[63],
		dev->id[64],
		dev->id[75]);
	DPRINTK("80==0x%04x  "
		"81==0x%04x  "
		"82==0x%04x  "
		"83==0x%04x  "
		"84==0x%04x  \n",
		dev->id[80],
		dev->id[81],
		dev->id[82],
		dev->id[83],
		dev->id[84]);
	DPRINTK("88==0x%04x  "
		"93==0x%04x\n",
		dev->id[88],
		dev->id[93]);
}

/**
 *	ata_dev_identify - obtain IDENTIFY x DEVICE page
 *	@ap: port on which device we wish to probe resides
 *	@device: device bus address, starting at zero
 *
 *	Following bus reset, we issue the IDENTIFY [PACKET] DEVICE
 *	command, and read back the 512-byte device information page.
 *	The device information page is fed to us via the standard
 *	PIO-IN protocol, but we hand-code it here. (TODO: investigate
 *	using standard PIO-IN paths)
 *
 *	After reading the device information page, we use several
 *	bits of information from it to initialize data structures
 *	that will be used during the lifetime of the ata_device.
 *	Other data from the info page is used to disqualify certain
 *	older ATA devices we do not wish to support.
 *
 *	LOCKING:
 *	Inherited from caller.  Some functions called by this function
 *	obtain the host_set lock.
 */

static void ata_dev_identify(struct ata_port *ap, unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	struct ata_device *dev = &ap->device[device];
	unsigned int i;
	u16 tmp, udma_modes;
	u8 status;
	struct ata_taskfile tf;

	if (!ata_dev_present(dev)) {
		DPRINTK("ENTER/EXIT (host %u, dev %u) -- nodev\n",
			ap->id, device);
		return;
	}

	DPRINTK("ENTER, host %u, dev %u\n", ap->id, device);

	assert (dev->class == ATA_DEV_ATA || dev->class == ATA_DEV_ATAPI ||
		dev->class == ATA_DEV_NONE);

	ata_dev_select(ap, device, 1, 1); /* select device 0/1 */

retry:
	ata_tf_init(ap, &tf, device);
	tf.ctl |= ATA_NIEN;

	if (dev->class == ATA_DEV_ATA)
		tf.command = ATA_CMD_ID_ATA;
	else
		tf.command = ATA_CMD_ID_ATAPI;

	DPRINTK("do identify\n");
	ata_tf_to_host(ap, &tf, BUS_NOINTR);

	/* crazy ATAPI devices... */
	if (dev->class == ATA_DEV_ATAPI)
		msleep(150);

	if (ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT))
		goto err_out;

	status = ata_chk_status(ioaddr);
	if (status & ATA_ERR) {
		/*
		 * arg!  ATA software reset (SRST) correctly places the
		 * the signatures in the taskfile registers... but kills
		 * one of my test devices.  EDD works for all test cases,
		 * but seems to return the ATA signature for some ATAPI
		 * devices.  Until the reason for this is found and fixed,
		 * we fix up the mess here.  If IDENTIFY DEVICE returns
		 * command aborted (as ATAPI devices do), then we
		 * issue an IDENTIFY PACKET DEVICE.
		 */
		if ((!use_software_reset) && (tf.command == ATA_CMD_ID_ATA)) {
			u8 err = inb(ap->ioaddr.cmd_addr + ATA_REG_ERR);
			if (err & ATA_ABORTED) {
				dev->class = ATA_DEV_ATAPI;
				goto retry;
			}
		}
		goto err_out;
	}

	/* make sure we have BSY=0, DRQ=1 */
	if ((status & ATA_DRQ) == 0) {
		printk(KERN_WARNING "ata%u: dev %u (ATA%s?) not returning id page (0x%x)\n",
		       ap->id, device,
		       dev->class == ATA_DEV_ATA ? "" : "PI",
		       status);
		goto err_out;
	}

	/* read IDENTIFY [X] DEVICE page */
	for (i = 0; i < ATA_ID_WORDS; i++)
		dev->id[i] = inw(ap->ioaddr.cmd_addr + ATA_REG_DATA);

	/* wait for host_idle */
	status = ata_wait_idle(&ap->ioaddr);
	if (status & (ATA_BUSY | ATA_DRQ)) {
		printk(KERN_WARNING "ata%u: dev %u (ATA%s?) error after id page (0x%x)\n",
		       ap->id, device,
		       dev->class == ATA_DEV_ATA ? "" : "PI",
		       status);
		goto err_out;
	}

	ata_irq_on(ap);	/* re-enable interrupts */
	ap->bus_state = BUS_IDLE;

	/* print device capabilities */
	printk(KERN_DEBUG "ata%u: dev %u cfg "
	       "49:%04x 82:%04x 83:%04x 84:%04x 85:%04x 86:%04x 87:%04x 88:%04x\n",
	       ap->id, device, dev->id[49],
	       dev->id[82], dev->id[83], dev->id[84],
	       dev->id[85], dev->id[86], dev->id[87],
	       dev->id[88]);

	/*
	 * common ATA, ATAPI feature tests
	 */

	/* we require LBA and DMA support (bits 8 & 9 of word 49) */
	if (!ata_id_has_dma(dev) || !ata_id_has_lba(dev)) {
		printk(KERN_DEBUG "ata%u: no dma/lba\n", ap->id);
		goto err_out_nosup;
	}

	/* we require UDMA support */
	udma_modes =
	tmp = dev->id[ATA_ID_UDMA_MODES];
	if ((tmp & 0xff) == 0) {
		printk(KERN_DEBUG "ata%u: no udma\n", ap->id);
		goto err_out_nosup;
	}

	ata_dump_id(dev);

	/* ATA-specific feature tests */
	if (dev->class == ATA_DEV_ATA) {
		if (!ata_id_is_ata(dev))	/* sanity check */
			goto err_out_nosup;

		tmp = dev->id[ATA_ID_MAJOR_VER];
		for (i = 14; i >= 1; i--)
			if (tmp & (1 << i))
				break;

		/* we require at least ATA-3 */
		if (i < 3) {
			printk(KERN_DEBUG "ata%u: no ATA-3\n", ap->id);
			goto err_out_nosup;
		}

		if (ata_id_has_lba48(dev)) {
			dev->flags |= ATA_DFLAG_LBA48;
			dev->n_sectors = ata_id_u64(dev, 100);
		} else {
			dev->n_sectors = ata_id_u32(dev, 60);
		}

		ata_dev_parse_strings(dev);

		ap->host->max_cmd_len = 16;

		/* print device info to dmesg */
		printk(KERN_INFO "ata%u: dev %u ATA, max %s, %Lu sectors%s\n",
		       ap->id, device,
		       ata_udma_string(udma_modes),
		       dev->n_sectors,
		       dev->flags & ATA_DFLAG_LBA48 ? " (lba48)" : "");
	}

	/* ATAPI-specific feature tests */
	else {
		if (ata_id_is_ata(dev))		/* sanity check */
			goto err_out_nosup;

		/* see if 16-byte commands supported */
		tmp = dev->id[0] & 0x3;
		if (tmp == 1)
			ap->host->max_cmd_len = 16;

		/* print device info to dmesg */
		printk(KERN_INFO "ata%u: dev %u ATAPI, max %s\n",
		       ap->id, device,
		       ata_udma_string(udma_modes));
	}

	DPRINTK("EXIT, drv_stat = 0x%x\n", ata_chk_status(&ap->ioaddr));
	return;

err_out_nosup:
	printk(KERN_WARNING "ata%u: dev %u not supported, ignoring\n",
	       ap->id, device);
err_out:
	ata_irq_on(ap);	/* re-enable interrupts */
	ap->bus_state = BUS_IDLE;
	dev->class++;	/* converts ATA_DEV_xxx into ATA_DEV_xxx_UNSUP */
	DPRINTK("EXIT, err\n");
}

/**
 *	ata_port_probe -
 *	@ap:
 *
 *	LOCKING:
 */

void ata_port_probe(struct ata_port *ap)
{
	ap->flags &= ~ATA_FLAG_PORT_DISABLED;
}

/**
 *	ata_port_disable -
 *	@ap:
 *
 *	LOCKING:
 */

void ata_port_disable(struct ata_port *ap)
{
	ap->device[0].class = ATA_DEV_NONE;
	ap->device[1].class = ATA_DEV_NONE;
	ap->flags |= ATA_FLAG_PORT_DISABLED;
}

/**
 *	ata_busy_sleep - sleep until BSY clears, or timeout
 *	@ap: port containing status register to be polled
 *	@tmout_pat: impatience timeout
 *	@tmout: overall timeout
 *
 *	LOCKING:
 *
 */

static unsigned int ata_busy_sleep (struct ata_port *ap,
				    unsigned long tmout_pat,
			    	    unsigned long tmout)
{
	unsigned long timer_start, timeout;
	u8 status;

	status = ata_busy_wait(&ap->ioaddr, ATA_BUSY, 3);
	timer_start = jiffies;
	timeout = timer_start + tmout_pat;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_busy_wait(&ap->ioaddr, ATA_BUSY, 3);
	}

	if (status & ATA_BUSY)
		printk(KERN_WARNING "ata%u is slow to respond, "
		       "please be patient\n", ap->id);

	timeout = timer_start + tmout;
	while ((status & ATA_BUSY) && (time_before(jiffies, timeout))) {
		msleep(50);
		status = ata_chk_status(&ap->ioaddr);
	}

	if (status & ATA_BUSY) {
		printk(KERN_ERR "ata%u failed to respond (%lu secs)\n",
		       ap->id, tmout / HZ);
		return 1;
	}

	return 0;
}

static void ata_bus_post_reset(struct ata_port *ap, unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	unsigned long timeout;

	/* if device 0 was found in ata_dev_devchk, wait for its
	 * BSY bit to clear
	 */
	if (dev0)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* if device 1 was found in ata_dev_devchk, wait for
	 * register access, then wait for BSY to clear
	 */
	timeout = jiffies + ATA_TMOUT_BOOT;
	while (dev1) {
		u8 nsect, lbal;

		__ata_dev_select(ioaddr, 1);
		nsect = inb(ioaddr->cmd_addr + ATA_REG_NSECT);
		lbal = inb(ioaddr->cmd_addr + ATA_REG_LBAL);
		if ((nsect == 1) && (lbal == 1))
			break;
		if (time_after(jiffies, timeout)) {
			dev1 = 0;
			break;
		}
		msleep(50);	/* give drive a breather */
	}
	if (dev1)
		ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	/* is all this really necessary? */
	__ata_dev_select(ioaddr, 0);
	if (dev1)
		__ata_dev_select(ioaddr, 1);
	if (dev0)
		__ata_dev_select(ioaddr, 0);
}

/**
 *	ata_bus_edd -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static unsigned int ata_bus_edd(struct ata_port *ap)
{
	struct ata_taskfile tf;

	/* set up execute-device-diag (bus reset) taskfile */
	/* also, take interrupts to a known state (disabled) */
	DPRINTK("execute-device-diag\n");
	ata_tf_init(ap, &tf, 0);
	tf.ctl |= ATA_NIEN;
	tf.command = ATA_CMD_EDD;

	/* do bus reset */
	ata_tf_to_host(ap, &tf, BUS_EDD);

	/* spec says at least 2ms.  but who knows with those
	 * crazy ATAPI devices...
	 */
	msleep(150);

	return ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);
}

static unsigned int ata_bus_softreset(struct ata_port *ap,
				      unsigned int devmask)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	/* software reset.  causes dev0 to be selected */
	outb(ap->ctl | ATA_NIEN | ATA_SRST, ioaddr->ctl_addr);
	ata_pause(ioaddr);
	outb(ap->ctl | ATA_NIEN, ioaddr->ctl_addr);
	ata_pause(ioaddr);

	ata_bus_post_reset(ap, devmask);

	return 0;
}

/**
 *	ata_bus_reset - reset host port and associated ATA channel
 *	@ap: port to reset
 *
 *	This is typically the first time we actually start issuing
 *	commands to the ATA channel.  We wait for BSY to clear, then
 *	issue EXECUTE DEVICE DIAGNOSTIC command, polling for its
 *	result.  Determine what devices, if any, are on the channel
 *	by looking at the device 0/1 error register.  Look at the signature
 *	stored in each device's taskfile registers, to determine if
 *	the device is ATA or ATAPI.
 *
 *	LOCKING:
 *	Inherited from caller.  Some functions called by this function
 *	obtain the host_set lock.
 *
 *	SIDE EFFECTS:
 *	Sets ATA_FLAG_PORT_DISABLED if bus reset fails.
 */

static void ata_bus_reset(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	u8 err;
	unsigned int dev0, dev1 = 0, rc, devmask = 0;

	DPRINTK("ENTER, host %u, port %u\n", ap->id, ap->port_no);

	/* set up device control */
	outb(ap->ctl, ioaddr->ctl_addr);

	/* determine if device 0/1 are present */
	dev0 = ata_dev_devchk(ioaddr, 0);
	if (slave_possible)
		dev1 = ata_dev_devchk(ioaddr, 1);

	if (dev0)
		devmask |= (1 << 0);
	if (dev1)
		devmask |= (1 << 1);

	/* select device 0 again */
	__ata_dev_select(ioaddr, 0);

	/* issue bus reset */
	if (use_software_reset)
		rc = ata_bus_softreset(ap, devmask);
	else
		rc = ata_bus_edd(ap);

	ap->bus_state = BUS_IDLE;

	if (rc)
		goto err_out;

	/*
	 * determine by signature whether we have ATA or ATAPI devices
	 */
	err = ata_dev_try_classify(ap, 0, dev0);
	if ((slave_possible) && (err != 0x81))
		ata_dev_try_classify(ap, 1, dev1);

	/* re-enable interrupts */
	ata_irq_on(ap);

	/* is double-select really necessary? */
	if (ap->device[1].class != ATA_DEV_NONE)
		__ata_dev_select(ioaddr, 1);
	if (ap->device[0].class != ATA_DEV_NONE)
		__ata_dev_select(ioaddr, 0);

	/* if no devices were detected, disable this port */
	if ((ap->device[0].class == ATA_DEV_NONE) &&
	    (ap->device[1].class == ATA_DEV_NONE))
		goto err_out;

	DPRINTK("EXIT\n");
	return;

err_out:
	printk(KERN_ERR "ata%u: disabling port\n", ap->id);
	ap->ops->port_disable(ap);

	DPRINTK("EXIT\n");
}

/**
 *	ata_host_set_pio -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_host_set_pio(struct ata_port *ap)
{
	struct ata_device *master, *slave;
	unsigned int pio, i;
	u16 mask;

	master = &ap->device[0];
	slave = &ap->device[1];

	assert (ata_dev_present(master) || ata_dev_present(slave));

	mask = ap->pio_mask;
	if (ata_dev_present(master))
		mask &= (master->id[ATA_ID_PIO_MODES] & 0x03);
	if (ata_dev_present(slave))
		mask &= (slave->id[ATA_ID_PIO_MODES] & 0x03);

	/* require pio mode 3 or 4 support for host and all devices */
	if (mask == 0) {
		printk(KERN_WARNING "ata%u: no PIO3/4 support, ignoring\n",
		       ap->id);
		goto err_out;
	}

	pio = (mask & ATA_ID_PIO4) ? 4 : 3;
	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_present(&ap->device[i])) {
			ap->device[i].pio_mode = (pio == 3) ?
				XFER_PIO_3 : XFER_PIO_4;
			ap->ops->set_piomode(ap, &ap->device[i], pio);
		}

	return;

err_out:
	ap->ops->port_disable(ap);
}

/**
 *	ata_host_set_udma -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_host_set_udma(struct ata_port *ap)
{
	struct ata_device *master, *slave;
	u16 mask;
	unsigned int i, j;
	int udma_mode = -1;

	master = &ap->device[0];
	slave = &ap->device[1];

	assert (ata_dev_present(master) || ata_dev_present(slave));
	assert ((ap->flags & ATA_FLAG_PORT_DISABLED) == 0);

	DPRINTK("udma masks: host 0x%X, master 0x%X, slave 0x%X\n",
		ap->udma_mask,
		(!ata_dev_present(master)) ? 0xff :
			(master->id[ATA_ID_UDMA_MODES] & 0xff),
		(!ata_dev_present(slave)) ? 0xff :
			(slave->id[ATA_ID_UDMA_MODES] & 0xff));

	mask = ap->udma_mask;
	if (ata_dev_present(master))
		mask &= (master->id[ATA_ID_UDMA_MODES] & 0xff);
	if (ata_dev_present(slave))
		mask &= (slave->id[ATA_ID_UDMA_MODES] & 0xff);

	i = XFER_UDMA_7;
	while (i >= XFER_UDMA_0) {
		j = i - XFER_UDMA_0;
		DPRINTK("mask 0x%X i 0x%X j %u\n", mask, i, j);
		if (mask & (1 << j)) {
			udma_mode = i;
			break;
		}

		i--;
	}

	/* require udma for host and all attached devices */
	if (udma_mode < 0) {
		printk(KERN_WARNING "ata%u: no UltraDMA support, ignoring\n",
		       ap->id);
		goto err_out;
	}

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		if (ata_dev_present(&ap->device[i])) {
			ap->device[i].udma_mode = udma_mode;
			ap->ops->set_udmamode(ap, &ap->device[i], udma_mode);
		}

	return;

err_out:
	ap->ops->port_disable(ap);
}

/**
 *	ata_dev_set_xfermode -
 *	@ap:
 *	@dev:
 *
 *	LOCKING:
 */

static void ata_dev_set_xfermode(struct ata_port *ap, struct ata_device *dev)
{
	struct ata_taskfile tf;

	/* set up set-features taskfile */
	DPRINTK("set features - xfer mode\n");
	ata_tf_init(ap, &tf, dev->devno);
	tf.ctl |= ATA_NIEN;
	tf.command = ATA_CMD_SET_FEATURES;
	tf.feature = SETFEATURES_XFER;
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (dev->flags & ATA_DFLAG_PIO)
		tf.nsect = dev->pio_mode;
	else
		tf.nsect = dev->udma_mode;

	/* do bus reset */
	ata_tf_to_host(ap, &tf, BUS_NODATA);

	/* crazy ATAPI devices... */
	if (dev->class == ATA_DEV_ATAPI)
		msleep(150);

	ata_busy_sleep(ap, ATA_TMOUT_BOOT_QUICK, ATA_TMOUT_BOOT);

	ata_irq_on(ap);	/* re-enable interrupts */
	ap->bus_state = BUS_IDLE;

	ata_wait_idle(&ap->ioaddr);

	DPRINTK("EXIT\n");
}

/**
 *	ata_dev_set_udma -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 */

static void ata_dev_set_udma(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];

	if (!ata_dev_present(dev) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		return;

	ata_dev_set_xfermode(ap, dev);

	assert((dev->udma_mode >= XFER_UDMA_0) &&
	       (dev->udma_mode <= XFER_UDMA_7));
	printk(KERN_INFO "ata%u: dev %u configured for %s\n",
	       ap->id, device,
	       udma_str[dev->udma_mode - XFER_UDMA_0]);
}

/**
 *	ata_dev_set_pio -
 *	@ap:
 *	@device:
 *
 *	LOCKING:
 */

static void ata_dev_set_pio(struct ata_port *ap, unsigned int device)
{
	struct ata_device *dev = &ap->device[device];

	if (!ata_dev_present(dev) || (ap->flags & ATA_FLAG_PORT_DISABLED))
		return;

	/* force PIO mode */
	dev->flags |= ATA_DFLAG_PIO;

	ata_dev_set_xfermode(ap, dev);

	assert((dev->pio_mode >= XFER_PIO_3) &&
	       (dev->pio_mode <= XFER_PIO_4));
	printk(KERN_INFO "ata%u: dev %u configured for PIO%c\n",
	       ap->id, device,
	       dev->pio_mode == 3 ? '3' : '4');
}

/**
 *	ata_sg_clean -
 *	@ap:
 *	@qc:
 *
 *	LOCKING:
 */

static void ata_sg_clean(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	Scsi_Cmnd *cmd = qc->scsicmd;
	struct scatterlist *sg;
	unsigned int dma = (qc->flags & ATA_QCFLAG_DMA);

#ifndef ATA_DEBUG
	if (!dma)
		return;
#endif

	if (cmd->use_sg) {
		sg = (struct scatterlist *)qc->scsicmd->request_buffer;
	} else {
		sg = &qc->sgent;
		assert(qc->n_elem == 1);
	}

	if (dma) {
		int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);

		DPRINTK("unmapping %u sg elements\n", qc->n_elem);

		if (cmd->use_sg)
			pci_unmap_sg(ap->host_set->pdev, sg, qc->n_elem, dir);
		else
			pci_unmap_single(ap->host_set->pdev, sg[0].dma_address,
					 sg[0].length, dir);
	}
}

/**
 *	ata_sg_setup_one -
 *	@ap:
 *	@qc:
 *	@cmd:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *
 */

static int ata_sg_setup_one(struct ata_port *ap, struct ata_queued_cmd *qc,
			    Scsi_Cmnd *cmd)
{
	int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
	struct scatterlist *sg = &qc->sgent;
	unsigned int dma = (qc->flags & ATA_QCFLAG_DMA);

	qc->n_elem = 1;
	sg->address = cmd->request_buffer;
	sg->page = virt_to_page(cmd->request_buffer);
	sg->offset = (unsigned long) cmd->request_buffer & ~PAGE_MASK;
	sg->length = cmd->request_bufflen;

	if (!dma)
		return 0;

	sg->dma_address = pci_map_single(ap->host_set->pdev,
					 cmd->request_buffer,
					 cmd->request_bufflen, dir);

	DPRINTK("mapped buffer of %d bytes for %s\n", cmd->request_bufflen,
		qc->flags & ATA_QCFLAG_WRITE ? "write" : "read");

	ap->prd[0].addr = cpu_to_le32(sg->dma_address);
	ap->prd[0].flags_len = cpu_to_le32(sg->length | ATA_PRD_EOT);
	VPRINTK("PRD[0] = (0x%X, 0x%X)\n",
		ap->prd[0].addr, ap->prd[0].flags_len);

	return 0;
}

/**
 *	ata_sg_setup -
 *	@ap:
 *	@qc:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *
 */

static int ata_sg_setup(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	Scsi_Cmnd *cmd = qc->scsicmd;
	struct scatterlist *sg;
	int n_elem;
	unsigned int i;
	unsigned int dma = (qc->flags & ATA_QCFLAG_DMA);

	VPRINTK("ENTER, ata%u, use_sg %d\n", ap->id, cmd->use_sg);
	assert(cmd->use_sg > 0);

	sg = (struct scatterlist *)cmd->request_buffer;
	if (dma) {
		int dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
		n_elem = pci_map_sg(ap->host_set->pdev, sg, cmd->use_sg, dir);
		if (n_elem < 1)
			return -1;
		DPRINTK("%d sg elements mapped\n", n_elem);
	} else {
		n_elem = cmd->use_sg;
	}
	qc->n_elem = n_elem;


#ifndef ATA_DEBUG
	if (!dma)
		return 0;
#endif

	for (i = 0; i < n_elem; i++) {
		ap->prd[i].addr = cpu_to_le32(sg[i].dma_address);
		ap->prd[i].flags_len = cpu_to_le32(sg[i].length);
		VPRINTK("PRD[%u] = (0x%X, 0x%X)\n",
			i, ap->prd[i].addr, ap->prd[i].flags_len);
	}
	ap->prd[n_elem - 1].flags_len |= cpu_to_le32(ATA_PRD_EOT);

#ifdef ATA_DEBUG
	i = n_elem - 1;
	VPRINTK("PRD[%u] = (0x%X, 0x%X)\n",
		i, ap->prd[i].addr, ap->prd[i].flags_len);

	for (i = n_elem; i < ATA_MAX_PRD; i++) {
		ap->prd[i].addr = 0;
		ap->prd[i].flags_len = cpu_to_le32(ATA_PRD_EOT);
	}
#endif

	return 0;
}

/**
 *	ata_pio_poll -
 *	@ap:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static unsigned long ata_pio_poll(struct ata_port *ap)
{
	u8 status;
	unsigned int poll_state = THR_UNKNOWN;
	unsigned int reg_state = THR_UNKNOWN;
	const unsigned int tmout_state = THR_PIO_TMOUT;

	switch (ap->thr_state) {
	case THR_PIO:
	case THR_PIO_POLL:
		poll_state = THR_PIO_POLL;
		reg_state = THR_PIO;
		break;
	case THR_PIO_LAST:
	case THR_PIO_LAST_POLL:
		poll_state = THR_PIO_LAST_POLL;
		reg_state = THR_PIO_LAST;
		break;
	default:
		BUG();
		break;
	}

	status = ata_chk_status(&ap->ioaddr);
	if (status & ATA_BUSY) {
		if (time_after(jiffies, ap->thr_timeout)) {
			ap->thr_state = tmout_state;
			return 0;
		}
		ap->thr_state = poll_state;
		if (current->need_resched)
			return 0;
		return ATA_SHORT_PAUSE;
	}

	ap->thr_state = reg_state;
	return 0;
}

/**
 *	ata_pio_start -
 *	@ap:
 *	@qc:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_pio_start (struct ata_port *ap, struct ata_queued_cmd *qc)
{
	/* FIXME: support TCQ */
	qc->tf.ctl |= ATA_NIEN;	/* disable interrupts */
	ata_tf_to_host_nolock(ap, &qc->tf, BUS_PIO);
	ata_thread_wake(ap, THR_PIO);
}

/**
 *	ata_pio_complete -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_pio_complete (struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	unsigned long flags;
	u8 drv_stat;

	/*
	 * This is purely hueristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, fall back to
	 * THR_PIO_POLL state.
	 */
	drv_stat = ata_busy_wait(&ap->ioaddr, ATA_BUSY | ATA_DRQ, 10);
	if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
		msleep(2);
		drv_stat = ata_busy_wait(&ap->ioaddr, ATA_BUSY | ATA_DRQ, 10);
		if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
			ap->thr_state = THR_PIO_LAST_POLL;
			ap->thr_timeout = jiffies + ATA_TMOUT_PIO;
			return;
		}
	}

	drv_stat = ata_wait_idle(&ap->ioaddr);
	if (drv_stat & (ATA_BUSY | ATA_DRQ)) {
		ap->thr_state = THR_PIO_ERR;
		return;
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);

	ap->thr_state = THR_IDLE;

	spin_lock_irqsave(&ap->host_set->lock, flags);
	assert(ap->bus_state == BUS_PIO);
	ap->bus_state = BUS_IDLE;
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	ata_irq_on(ap);

	ata_qc_complete(ap, qc, drv_stat, 0, 1);
}

/**
 *	ata_pio_sector -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_pio_sector(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	struct scatterlist *sg;
	Scsi_Cmnd *cmd;
	unsigned char *buf;
	u8 status;

	assert(ap->bus_state == BUS_PIO);

	/*
	 * This is purely hueristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, fall back to
	 * THR_PIO_POLL state.
	 */
	status = ata_busy_wait(&ap->ioaddr, ATA_BUSY, 5);
	if (status & ATA_BUSY) {
		msleep(2);
		status = ata_busy_wait(&ap->ioaddr, ATA_BUSY, 10);
		if (status & ATA_BUSY) {
			ap->thr_state = THR_PIO_POLL;
			ap->thr_timeout = jiffies + ATA_TMOUT_PIO;
			return;
		}
	}

	/* handle BSY=0, DRQ=0 as error */
	if ((status & ATA_DRQ) == 0) {
		ap->thr_state = THR_PIO_ERR;
		return;
	}

	qc = ata_qc_from_tag(ap, ap->active_tag);
	cmd = qc->scsicmd;
	if (cmd->use_sg)
		sg = (struct scatterlist *)cmd->request_buffer;
	else
		sg = &qc->sgent;
	if (qc->cursect == (qc->nsect - 1))
		ap->thr_state = THR_PIO_LAST;

	buf = kmap(sg[qc->cursg].page) +
	      sg[qc->cursg].offset + (qc->cursg_ofs * ATA_SECT_SIZE);

	qc->cursect++;
	qc->cursg_ofs++;

	if (cmd->use_sg)
		if ((qc->cursg_ofs * ATA_SECT_SIZE) == sg[qc->cursg].length) {
			qc->cursg++;
			qc->cursg_ofs = 0;
		}

	DPRINTK("data %s, drv_stat 0x%X\n",
		qc->flags & ATA_QCFLAG_WRITE ? "write" : "read",
		status);

	/* do the actual data transfer */
	if (qc->flags & ATA_QCFLAG_WRITE)
		outsl(ap->ioaddr.cmd_addr + ATA_REG_DATA, buf, ATA_SECT_DWORDS);
	else
		insl(ap->ioaddr.cmd_addr + ATA_REG_DATA, buf, ATA_SECT_DWORDS);

	kunmap(sg[qc->cursg].page);
}

/**
 *	ata_eng_schedule - run an iteration of the pio/dma/whatever engine
 *	@ap: port on which activity will occur
 *	@eng: instance of engine
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
static void ata_eng_schedule (struct ata_port *ap, struct ata_engine *eng)
{
	/* FIXME */
}

/**
 *	ata_qc_new -
 *	@ap:
 *	@dev:
 *
 *	LOCKING:
 */

static struct ata_queued_cmd *ata_qc_new(struct ata_port *ap)
{
	struct ata_queued_cmd *qc = NULL;
	unsigned int i;

	for (i = 0; i < ATA_MAX_QUEUE; i++)
		if (!test_and_set_bit(i, &ap->qactive)) {
			qc = ata_qc_from_tag(ap, i);
			break;
		}

	if (qc)
		qc->tag = i;

	return qc;
}

/**
 *	ata_qc_new_init -
 *	@ap:
 *	@cmd:
 *	@done:
 *
 *	LOCKING:
 */

static struct ata_queued_cmd *ata_qc_new_init(struct ata_port *ap,
					      struct ata_device *dev,
		      			      Scsi_Cmnd *cmd,
					      void (*done)(Scsi_Cmnd *))
{
	struct ata_queued_cmd *qc;

	qc = ata_qc_new(ap);
	if (!qc) {
		cmd->result = (DID_OK << 16) | (QUEUE_FULL << 1);
		done(cmd);
	} else {
		ap->active_tag = qc->tag;
		qc->flags = 0;
		qc->scsicmd = cmd;
		qc->scsidone = done;
		qc->ap = ap;
		qc->dev = dev;
		INIT_LIST_HEAD(&qc->node);
		ata_tf_init(ap, &qc->tf, dev->devno);
	}

	return qc;
}

/**
 *	ata_qc_complete -
 *	@qc:
 *	@drv_stat:
 *
 *	LOCKING:
 *
 */

static void ata_qc_complete(struct ata_port *ap, struct ata_queued_cmd *qc,
			    u8 drv_stat, unsigned int done_late,
			    unsigned int sg_clean)
{
	Scsi_Cmnd *cmd = qc->scsicmd;
	unsigned int tag;

	if (sg_clean)
		ata_sg_clean(ap, qc);

	if (drv_stat & ATA_ERR) {
		if (qc->flags & ATA_QCFLAG_ATAPI)
			cmd->result = SAM_STAT_CHECK_CONDITION;
		else
			ata_to_sense_error(qc, cmd);
	} else {
		if (done_late)
			cmd->done_late = 1;
		cmd->result = SAM_STAT_GOOD;
	}

	qc->scsidone(cmd);

	qc->flags &= ~ATA_QCFLAG_ACTIVE;
	tag = qc->tag;
	if (tag < ATA_MAX_QUEUE) {
		if (tag == ap->active_tag)
			ap->active_tag = 0xfafbfcfd;
		qc->tag = 0xfafbfcfd;
		clear_bit(tag, &ap->qactive);
	}
}

/**
 *	ata_qc_push -
 *	@qc:
 *	@append:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
static void ata_qc_push (struct ata_queued_cmd *qc, unsigned int append)
{
	struct ata_port *ap = qc->ap;
	struct ata_engine *eng = &ap->eng;

	if (likely(append))
		list_add_tail(&qc->node, &eng->q);
	else
		list_add(&qc->node, &eng->q);
	
	if (!test_and_set_bit(ATA_EFLG_ACTIVE, &eng->flags))
		ata_eng_schedule(ap, eng);
}

/**
 *	ata_qc_issue -
 *	@qc:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */
static int ata_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	Scsi_Cmnd *cmd = qc->scsicmd;
	unsigned int pio = ((qc->flags & ATA_QCFLAG_DMA) == 0);

	ata_dev_select(ap, qc->dev->devno, 1, 0);

	/* set up SG table */
	if (cmd->use_sg) {
		if (ata_sg_setup(ap, qc))
			goto err_out;
	} else {
		if (ata_sg_setup_one(ap, qc, cmd))
			goto err_out;
	}

	if (pio)
		ata_pio_start(ap, qc);
	else
		ata_dma_start(ap, qc);

	qc->flags |= ATA_QCFLAG_ACTIVE;

	return 0;

err_out:
	return -1;
}

/**
 *	ata_dma_start -
 *	@ap:
 *	@qc:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_dma_start (struct ata_port *ap, struct ata_queued_cmd *qc)
{
	unsigned int rw = (qc->flags & ATA_QCFLAG_WRITE);
	u8 host_stat, dmactl;

	/* load taskfile registers */
	ap->ops->tf_load(&ap->ioaddr, &qc->tf);

	/* load PRD table addr. */
	outl(ap->prd_dma, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);

	/* specify data direction */
	/* FIXME: redundant to later start-dma command? */
	ap->dmactl = rw ? 0 : ATA_DMA_WR;
	outb(ap->dmactl, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* clear interrupt, error bits */
	host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
	     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	/* issue r/w command */
	__ata_exec(ap, &qc->tf, BUS_DMA);

	/* start host DMA transaction */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	outb(dmactl | ATA_DMA_START,
	     ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
}

/**
 *	ata_dma_complete -
 *	@ap:
 *	@host_stat:
 *	@err:
 *
 *	LOCKING:
 */

static void ata_dma_complete(struct ata_port *ap, u8 host_stat,
			     unsigned int done_late)
{
	u8 drv_stat;

	VPRINTK("ENTER\n");

	/* clear start/stop bit */
	outb(0, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* get controller status; clear intr, err bits */
	outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
	     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	inb(ap->ioaddr.ctl_addr);	/* dummy read */

	/* get drive status; clear intr */
	drv_stat = ata_wait_idle(&ap->ioaddr);

	DPRINTK("host %u, host_stat==0x%X, drv_stat==0x%X\n",
		ap->id, (u32) host_stat, (u32) drv_stat);

	if (unlikely((ap->active_tag > ATA_MAX_QUEUE) ||
	         ((ap->qcmd[ap->active_tag].flags & ATA_QCFLAG_ACTIVE) == 0))) {
		printk(KERN_ERR "ata%u: BUG: SCSI cmd not active\n", ap->id);
	} else {
		ata_qc_complete(ap, ata_qc_from_tag(ap, ap->active_tag),
				drv_stat, done_late, 1);
	}
}

/**
 *	ata_host_intr -
 *	@ap:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static inline unsigned int ata_host_intr (struct ata_port *ap)
{
	u8 status, host_stat;
	unsigned int handled = 0;

	switch (ap->bus_state) {
	case BUS_NOINTR:	/* do nothing; (hopefully!) shared irq */
	case BUS_EDD:
	case BUS_IDLE:
	case BUS_PIO:
	case BUS_PACKET:
		ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
		if ((ap->stats.idle_irq % 1000) == 0) {
			handled = 1;
			ata_irq_ack(ap, 0); /* debug trap */
			printk(KERN_WARNING "ata%d: irq trap\n", ap->id);
		}
#endif
		break;

	case BUS_DMA:
		host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
		VPRINTK("BUS_DMA (host_stat 0x%X)\n", host_stat);

		if (!(host_stat & ATA_DMA_INTR)) {
			ap->stats.idle_irq++;
			break;
		}

		ata_dma_complete(ap, host_stat, 0);
		ap->bus_state = BUS_IDLE;
		handled = 1;
		break;

	case BUS_NODATA:	/* command completion, but no data xfer */
		status = ata_irq_ack(ap, 1);
		DPRINTK("BUS_NODATA (drv_stat 0x%X)\n", status);
		ap->bus_state = BUS_IDLE;
		up(&ap->sem);
		handled = 1;
		break;

	case BUS_IDENTIFY:
		status = ata_irq_ack(ap, 0);
		DPRINTK("BUS_IDENTIFY (drv_stat 0x%X)\n", status);
		ap->bus_state = BUS_PIO;
		up(&ap->sem);
		handled = 1;
		break;

	default:
		printk(KERN_DEBUG "ata%u: unhandled bus state %u\n",
		       ap->id, ap->bus_state);
		ap->stats.unhandled_irq++;

#ifdef ATA_IRQ_TRAP
		if ((ap->stats.unhandled_irq % 1000) == 0) {
			handled = 1;
			ata_irq_ack(ap, 0); /* debug trap */
			printk(KERN_WARNING "ata%d: irq trap\n", ap->id);
		}
#endif
		break;
	}

	return handled;
}

/**
 *	ata_interrupt -
 *	@irq:
 *	@dev_instance:
 *	@regs:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static irqreturn_t ata_interrupt (int irq, void *dev_instance,
				  struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned long flags;
	unsigned int i;
	unsigned int handled = 0;

	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_hosts; i++) {
		struct ata_port *ap;

		ap = host_set->hosts[i];
		if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED)))
			handled += ata_host_intr(ap);
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

/**
 *	ata_timer -
 *	@ap:
 *
 *	LOCKING:
 */

static void ata_timer(struct ata_port *ap)
{
	unsigned long flags;
	unsigned int bus_state;
	u8 host_stat;

	DPRINTK("ENTER\n");

	spin_lock_irqsave(&ap->host_set->lock, flags);
	bus_state = ap->bus_state;
	ap->bus_state = BUS_TIMER;
	spin_unlock_irqrestore(&ap->host_set->lock, flags);

	switch (bus_state) {
	case BUS_PIO:
	case BUS_NODATA:
		ata_wait_idle(&ap->ioaddr);
		up(&ap->sem);
		break;

	case BUS_DMA:
		printk(KERN_WARNING "ata%u: DMA timeout\n", ap->id);
		host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
		ata_dma_complete(ap, host_stat, 1);
		break;

	default:
		DPRINTK("unhandled bus state %u\n", bus_state);
		break;
	}

	spin_lock_irqsave(&ap->host_set->lock, flags);
	ap->bus_state = BUS_IDLE;
	spin_unlock_irqrestore(&ap->host_set->lock, flags);
	DPRINTK("EXIT\n");
}

/**
 *	ata_thread_wake -
 *	@ap:
 *	@thr_state:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_thread_wake(struct ata_port *ap, unsigned int thr_state)
{
	assert(ap->thr_state == THR_IDLE);
	ap->thr_state = thr_state;
	up(&ap->thr_sem);
}

/**
 *	ata_thread_timer -
 *	@opaque:
 *
 *	LOCKING:
 */

static void ata_thread_timer(unsigned long opaque)
{
	struct ata_port *ap = (struct ata_port *) opaque;

	up(&ap->thr_sem);
}

/**
 *	ata_thread_iter -
 *	@ap:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static unsigned long ata_thread_iter(struct ata_port *ap)
{
	long timeout = 0;

	DPRINTK("ata%u: thr_state %s\n",
		ap->id, ata_thr_state_name(ap->thr_state));

	switch (ap->thr_state) {
	case THR_UNKNOWN:
		ap->thr_state = THR_CHECKPORT;
		break;

	case THR_PROBE_START:
		down(&ap->sem);
		ap->thr_state = THR_CHECKPORT;
		break;

	case THR_CHECKPORT:
		ap->ops->port_probe(ap);
		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			ap->thr_state = THR_PROBE_FAILED;
		else
			ap->thr_state = THR_BUS_RESET;
		break;

	case THR_BUS_RESET:
		ata_bus_reset(ap);
		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			ap->thr_state = THR_PROBE_FAILED;
		else
			ap->thr_state = THR_IDENTIFY;
		break;

	case THR_IDENTIFY:
		ata_dev_identify(ap, 0);
		ata_dev_identify(ap, 1);

		if (!ata_dev_present(&ap->device[0]) &&
		    !ata_dev_present(&ap->device[1])) {
			ap->ops->port_disable(ap);
			ap->thr_state = THR_PROBE_FAILED;
		} else
			ap->thr_state = THR_CONFIG_TIMINGS;
		break;

	case THR_CONFIG_TIMINGS:
		ata_host_set_pio(ap);
		if ((ap->flags & ATA_FLAG_PORT_DISABLED) == 0)
			ata_host_set_udma(ap);

		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			ap->thr_state = THR_PROBE_FAILED;
		else
#ifdef ATA_FORCE_PIO
			ap->thr_state = THR_CONFIG_FORCE_PIO;
#else
			ap->thr_state = THR_CONFIG_DMA;
#endif
		break;

	case THR_CONFIG_FORCE_PIO:
		ata_dev_set_pio(ap, 0);
		ata_dev_set_pio(ap, 1);

		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			ap->thr_state = THR_PROBE_FAILED;
		else
			ap->thr_state = THR_PROBE_SUCCESS;
		break;

	case THR_CONFIG_DMA:
		ata_dev_set_udma(ap, 0);
		ata_dev_set_udma(ap, 1);

		if (ap->flags & ATA_FLAG_PORT_DISABLED)
			ap->thr_state = THR_PROBE_FAILED;
		else
			ap->thr_state = THR_PROBE_SUCCESS;
		break;

	case THR_PROBE_SUCCESS:
		up(&ap->probe_sem);
		ap->thr_state = THR_IDLE;
		break;

	case THR_PROBE_FAILED:
		up(&ap->probe_sem);
		ap->thr_state = THR_AWAIT_DEATH;
		break;

	case THR_AWAIT_DEATH:
		timeout = -1;
		break;

	case THR_IDLE:
		timeout = 30 * HZ;
		break;

	case THR_PIO:
		ata_pio_sector(ap);
		break;

	case THR_PIO_LAST:
		ata_pio_complete(ap);
		break;

	case THR_PIO_POLL:
	case THR_PIO_LAST_POLL:
		timeout = ata_pio_poll(ap);
		break;

	case THR_PIO_TMOUT:
		printk(KERN_ERR "ata%d: FIXME: THR_PIO_TMOUT\n", /* FIXME */
		       ap->id);
		timeout = 11 * HZ;
		break;

	case THR_PIO_ERR:
		printk(KERN_ERR "ata%d: FIXME: THR_PIO_ERR\n", /* FIXME */
		       ap->id);
		timeout = 11 * HZ;
		break;

	case THR_PACKET:
		atapi_cdb_send(ap);
		break;

	default:
		printk(KERN_DEBUG "ata%u: unknown thr state %s\n",
		       ap->id, ata_thr_state_name(ap->thr_state));
		break;
	}

	DPRINTK("ata%u: new thr_state %s, returning %ld\n",
		ap->id, ata_thr_state_name(ap->thr_state), timeout);
	return timeout;
}

/**
 *	ata_thread -
 *	@data:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int ata_thread (void *data)
{
        struct ata_port *ap = data;
	long timeout;

	daemonize ();
	reparent_to_init();
	spin_lock_irq(&current->sighand->siglock);
	sigemptyset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	sprintf(current->comm, "katad-%u", ap->id);

        while (1) {
		cond_resched();

		timeout = ata_thread_iter(ap);

                if (signal_pending (current)) {
                        spin_lock_irq(&current->sighand->siglock);
                        flush_signals(current);
                        spin_unlock_irq(&current->sighand->siglock);
                }

                if ((timeout < 0) || (ap->time_to_die))
                        break;

 		/* note sleeping for full timeout not guaranteed (that's ok) */
		if (timeout) {
			mod_timer(&ap->thr_timer, jiffies + timeout);
			down_interruptible(&ap->thr_sem);

                	if (signal_pending (current)) {
                        	spin_lock_irq(&current->sighand->siglock);
                        	flush_signals(current);
                        	spin_unlock_irq(&current->sighand->siglock);
                	}

                	if (ap->time_to_die)
                        	break;
		}
        }

	printk(KERN_INFO "ata%u: thread exiting\n", ap->id);
	ap->thr_pid = -1;
        complete_and_exit (&ap->thr_exited, 0);
}

/**
 *	ata_to_sense_error -
 *	@qc:
 *	@cmd:
 *
 *	LOCKING:
 */

static void ata_to_sense_error(struct ata_queued_cmd *qc, Scsi_Cmnd *cmd)
{
	cmd->result = SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = MEDIUM_ERROR;
	cmd->sense_buffer[7] = 14 - 8;	/* addnl. sense len. FIXME: correct? */

	/* additional-sense-code[-qualifier] */
	if ((qc->flags & ATA_QCFLAG_WRITE) == 0) {
		cmd->sense_buffer[12] = 0x11; /* "unrecovered read error" */
		cmd->sense_buffer[13] = 0x04;
	} else {
		cmd->sense_buffer[12] = 0x0C; /* "write error -             */
		cmd->sense_buffer[13] = 0x02; /*  auto-reallocation failed" */
	}
}

/**
 *	ata_scsi_error -
 *	@host:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

int ata_scsi_error(struct Scsi_Host *host)
{
	struct ata_port *ap;
	struct ata_queued_cmd *qc = NULL;

	DPRINTK("ENTER\n");
	ap = (struct ata_port *) &host->hostdata[0];

	ata_timer(ap);

	if (ap->active_tag < ATA_MAX_QUEUE)
		qc = ata_qc_from_tag(ap, ap->active_tag);
	if (qc && (qc->flags & ATA_QCFLAG_ACTIVE)) {
		DPRINTK("cancelling command\n");

		ata_qc_complete(ap, qc, ATA_ERR, 0, 1);
	}

	DPRINTK("EXIT\n");
	return 0;
}

/**
 *	scsi_rw_to_ata -
 *	@qc:
 *	@scsicmd:
 *	@cmd_size:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *
 *	RETURNS:
 *
 */

static unsigned int scsi_rw_to_ata(struct ata_queued_cmd *qc, u8 *scsicmd,
				   unsigned int cmd_size)
{
	struct ata_taskfile *tf = &qc->tf;
	unsigned int lba48 = tf->flags & ATA_TFLAG_LBA48;
	unsigned int pio = ((qc->flags & ATA_QCFLAG_DMA) == 0);

	qc->cursect = qc->cursg = qc->cursg_ofs = 0;
	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->hob_nsect = 0;
	tf->hob_lbal = 0;
	tf->hob_lbam = 0;
	tf->hob_lbah = 0;

	if (scsicmd[0] == READ_10 || scsicmd[0] == READ_6 ||
	    scsicmd[0] == READ_16) {
		if (pio) {
			if (lba48)
				tf->command = ATA_CMD_PIO_READ_EXT;
			else
				tf->command = ATA_CMD_PIO_READ;
		} else {
			if (lba48)
				tf->command = ATA_CMD_READ_EXT;
			else
				tf->command = ATA_CMD_READ;
		}
		qc->flags &= ~ATA_QCFLAG_WRITE;
		VPRINTK("reading\n");
	} else {
		if (pio) {
			if (lba48)
				tf->command = ATA_CMD_PIO_WRITE_EXT;
			else
				tf->command = ATA_CMD_PIO_WRITE;
		} else {
			if (lba48)
				tf->command = ATA_CMD_WRITE_EXT;
			else
				tf->command = ATA_CMD_WRITE;
		}
		qc->flags |= ATA_QCFLAG_WRITE;
		VPRINTK("writing\n");
	}

	if (cmd_size == 10) {
		if (lba48) {
			tf->hob_nsect = scsicmd[7];
			tf->hob_lbal = scsicmd[2];

			qc->nsect = ((unsigned int)scsicmd[7] << 8) |
					scsicmd[8];
		} else {
			/* if we don't support LBA48 addressing, the request
			 * -may- be too large. */
			if ((scsicmd[2] & 0xf0) || scsicmd[7])
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[2];

			qc->nsect = scsicmd[8];
		}
		tf->device |= ATA_LBA;

		tf->nsect = scsicmd[8];
		tf->lbal = scsicmd[5];
		tf->lbam = scsicmd[4];
		tf->lbah = scsicmd[3];

		VPRINTK("ten-byte command\n");
		return 0;
	}

	if (cmd_size == 6) {
		qc->nsect = tf->nsect = scsicmd[4];
		tf->lbal = scsicmd[3];
		tf->lbam = scsicmd[2];
		tf->lbah = scsicmd[1] & 0x1f; /* mask out reserved bits */

		VPRINTK("six-byte command\n");
		return 0;
	}

	if (cmd_size == 16) {
		/* rule out impossible LBAs and sector counts */
		if (scsicmd[2] || scsicmd[3] || scsicmd[10] || scsicmd[11])
			return 1;

		if (lba48) {
			tf->hob_nsect = scsicmd[12];
			tf->hob_lbal = scsicmd[6];
			tf->hob_lbam = scsicmd[5];
			tf->hob_lbah = scsicmd[4];

			qc->nsect = ((unsigned int)scsicmd[12] << 8) |
					scsicmd[13];
		} else {
			/* once again, filter out impossible non-zero values */
			if (scsicmd[4] || scsicmd[5] || scsicmd[12] ||
			    (scsicmd[6] & 0xf0))
				return 1;

			/* stores LBA27:24 in lower 4 bits of device reg */
			tf->device |= scsicmd[2];

			qc->nsect = scsicmd[13];
		}
		tf->device |= ATA_LBA;

		tf->nsect = scsicmd[13];
		tf->lbal = scsicmd[9];
		tf->lbam = scsicmd[8];
		tf->lbah = scsicmd[7];

		VPRINTK("sixteen-byte command\n");
		return 0;
	}

	DPRINTK("no-byte command\n");
	return 1;
}

/**
 *	ata_do_rw -
 *	@ap:
 *	@dev:
 *	@cmd:
 *	@done:
 *	@cmd_size:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_do_rw(struct ata_port *ap, struct ata_device *dev,
		      Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *),
		      unsigned int cmd_size)
{
	struct ata_queued_cmd *qc;
	u8 *scsicmd = cmd->cmnd;

	VPRINTK("ENTER\n");

	qc = ata_qc_new_init(ap, dev, cmd, done);
	if (!qc)
		return;

	if (dev->flags & ATA_DFLAG_LBA48)
		qc->tf.flags |= ATA_TFLAG_LBA48;
	if ((dev->flags & ATA_DFLAG_PIO) == 0)
		qc->flags |= ATA_QCFLAG_DMA;

	if (scsi_rw_to_ata(qc, scsicmd, cmd_size))
		goto err_out;

	/* select device, send command to hardware */
	if (ata_qc_issue(qc))
		goto err_out;

	VPRINTK("EXIT\n");
	return;

err_out:
	ata_bad_cdb(cmd, done);
	DPRINTK("EXIT - badcmd\n");
}

/**
 *	ata_scsi_get_reqbuf -
 *	@cmd:
 *	@buf_out:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 *	FIXME: kmap inside spin_lock_irqsave ok?
 *
 *	RETURNS:
 *
 */

static unsigned int ata_scsi_get_reqbuf(Scsi_Cmnd *cmd, u8 **buf_out)
{
	u8 *buf;
	unsigned int buflen;

	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		buf = kmap(sg->page) + sg->offset;
		buflen = sg->length;
	} else {
		buf = cmd->request_buffer;
		buflen = cmd->request_bufflen;
	}

	memset(buf, 0, buflen);
	*buf_out = buf;
	return buflen;
}

/**
 *	ata_scsi_put_reqbuf -
 *	@cmd:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static inline void ata_scsi_put_reqbuf(Scsi_Cmnd *cmd)
{
	if (cmd->use_sg) {
		struct scatterlist *sg;

		sg = (struct scatterlist *) cmd->request_buffer;
		kunmap(sg->page);
	}
}

/**
 *	ata_scsiop_inq_std -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_inq_std(struct ata_scsi_args *args, u8 *reqbuf,
			       unsigned int buflen)
{
	const u8 hdr[] = {
		TYPE_DISK,
		0,
		0x5,	/* claim SPC-3 version compatibility */
		2,
		96 - 4
	};

	VPRINTK("ENTER\n");

	memcpy(reqbuf, hdr, sizeof(hdr));

	if (buflen > 36) {
		memcpy(&reqbuf[8], args->dev->vendor, 8);
		memcpy(&reqbuf[16], args->dev->product, 16);
		memcpy(&reqbuf[32], DRV_VERSION, 4);
	}

	if (buflen > 63) {
		const u8 versions[] = {
			0x60,	/* SAM-3 (no version claimed) */

			0x03,
			0x20,	/* SBC-2 (no version claimed) */

			0x02,
			0x60	/* SPC-3 (no version claimed) */
		};

		memcpy(reqbuf + 59, versions, sizeof(versions));
	}

	return 0;
}

/**
 *	ata_scsiop_inq_00 -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_inq_00(struct ata_scsi_args *args, u8 *reqbuf,
			      unsigned int buflen)
{
	const u8 pages[] = {
		0x00,	/* page 0x00, this page */
		0x80,	/* page 0x80, unit serial no page */
		0x83	/* page 0x83, device ident page */
	};
	reqbuf[3] = sizeof(pages);	/* number of supported EVPD pages */

	if (buflen > 6)
		memcpy(reqbuf + 4, pages, sizeof(pages));

	return 0;
}

/**
 *	ata_scsiop_inq_80 -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_inq_80(struct ata_scsi_args *args, u8 *reqbuf,
			      unsigned int buflen)
{
	const u8 hdr[] = {
		0,
		0x80,			/* this page code */
		0,
		ATA_SERNO_LEN,		/* page len */
	};
	memcpy(reqbuf, hdr, sizeof(hdr));

	if (buflen > (ATA_SERNO_LEN + 4))
		ata_dev_id_string(args->dev, (unsigned char *) &reqbuf[4],
				  ATA_ID_SERNO_OFS, ATA_SERNO_LEN);

	return 0;
}

static const char *inq_83_str = "Linux ATA-SCSI simulator";

/**
 *	ata_scsiop_inq_83 -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_inq_83(struct ata_scsi_args *args, u8 *reqbuf,
			      unsigned int buflen)
{
	reqbuf[1] = 0x83;			/* this page code */
	reqbuf[3] = 4 + strlen(inq_83_str);	/* page len */

	/* our one and only identification descriptor (vendor-specific) */
	if (buflen > (strlen(inq_83_str) + 4 + 4)) {
		reqbuf[4 + 0] = 2;	/* code set: ASCII */
		reqbuf[4 + 3] = strlen(inq_83_str);
		memcpy(reqbuf + 4 + 4, inq_83_str, strlen(inq_83_str));
	}

	return 0;
}

/**
 *	ata_scsiop_noop -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_noop(struct ata_scsi_args *args, u8 *reqbuf,
			    unsigned int buflen)
{
	VPRINTK("ENTER\n");
	return 0;
}

/**
 *	ata_scsiop_sync_cache -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_sync_cache(struct ata_scsi_args *args, u8 *reqbuf,
				  unsigned int buflen)
{
	VPRINTK("ENTER\n");

	/* FIXME */
	return 1;
}

/**
 *	ata_msense_push
 *	@ptr:
 *	@last:
 *	@buf:
 *	@buflen:
 *
 *	LOCKING:
 *	None.
 */

static void ata_msense_push(u8 **ptr_io, const u8 *last,
			    const u8 *buf, unsigned int buflen)
{
	u8 *ptr = *ptr_io;

	if ((ptr + buflen - 1) > last)
		return;

	memcpy(ptr, buf, buflen);

	ptr += buflen;

	*ptr_io = ptr;
}

/**
 *	ata_msense_caching -
 *	@dev:
 *	@ptr_io:
 *	@last:
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_caching(struct ata_device *dev, u8 **ptr_io,
				       const u8 *last)
{
	u8 page[7] = { 0xf, 0, 0x10, 0, 0x8, 0xa, 0 };
	if (dev->flags & ATA_DFLAG_WCACHE)
		page[6] = 0x4;

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_msense_ctl_mode -
 *	@dev:
 *	@ptr_io:
 *	@last:
 *
 *	LOCKING:
 *	None.
 */

static unsigned int ata_msense_ctl_mode(u8 **ptr_io, const u8 *last)
{
	const u8 page[] = {0xa, 0xa, 2, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 30};

	ata_msense_push(ptr_io, last, page, sizeof(page));
	return sizeof(page);
}

/**
 *	ata_scsiop_mode_sense -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_mode_sense(struct ata_scsi_args *args, u8 *reqbuf,
				  unsigned int buflen)
{
	u8 *scsicmd = args->cmd->cmnd, *p, *last;
	struct ata_device *dev = args->dev;
	unsigned int page_control, six_byte, output_len;

	VPRINTK("ENTER\n");

	six_byte = (scsicmd[0] == MODE_SENSE);

	/* we only support saved and current values (which we treat
	 * in the same manner)
	 */
	page_control = scsicmd[2] >> 6;
	if ((page_control != 0) && (page_control != 3))
		return 1;

	if (six_byte)
		output_len = 4;
	else
		output_len = 8;

	p = reqbuf + output_len;
	last = reqbuf + buflen - 1;

	switch(scsicmd[2] & 0x3f) {
	case 0x08:		/* caching */
		output_len += ata_msense_caching(dev, &p, last);
		break;

	case 0x0a: {		/* control mode */
		output_len += ata_msense_ctl_mode(&p, last);
		break;
		}

	case 0x3f:		/* all pages */
		output_len += ata_msense_caching(dev, &p, last);
		output_len += ata_msense_ctl_mode(&p, last);
		break;

	default:		/* invalid page code */
		return 1;
	}

	if (six_byte) {
		output_len--;
		reqbuf[0] = output_len;
	} else {
		output_len -= 2;
		reqbuf[0] = output_len >> 8;
		reqbuf[1] = output_len;
	}

	return 0;
}

/**
 *	ata_scsiop_read_cap -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_read_cap(struct ata_scsi_args *args, u8 *reqbuf,
			        unsigned int buflen)
{
	u64 n_sectors = args->dev->n_sectors;
	u32 tmp;

	VPRINTK("ENTER\n");

	n_sectors--;		/* one off */

	tmp = n_sectors;	/* note: truncates, if lba48 */
	if (args->cmd->cmnd[0] == READ_CAPACITY) {
		reqbuf[0] = tmp >> (8 * 3);
		reqbuf[1] = tmp >> (8 * 2);
		reqbuf[2] = tmp >> (8 * 1);
		reqbuf[3] = tmp;

		tmp = ATA_SECT_SIZE;
		reqbuf[6] = tmp >> 8;
		reqbuf[7] = tmp;

	} else {
		reqbuf[2] = n_sectors >> (8 * 7);
		reqbuf[3] = n_sectors >> (8 * 6);
		reqbuf[4] = n_sectors >> (8 * 5);
		reqbuf[5] = n_sectors >> (8 * 4);
		reqbuf[6] = tmp >> (8 * 3);
		reqbuf[7] = tmp >> (8 * 2);
		reqbuf[8] = tmp >> (8 * 1);
		reqbuf[9] = tmp;

		tmp = ATA_SECT_SIZE;
		reqbuf[12] = tmp >> 8;
		reqbuf[13] = tmp;
	}

	return 0;
}

/**
 *	ata_scsiop_report_luns -
 *	@args:
 *	@reqbuf:
 *	@buflen:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static unsigned int ata_scsiop_report_luns(struct ata_scsi_args *args, u8 *reqbuf,
				   unsigned int buflen)
{
	VPRINTK("ENTER\n");
	reqbuf[3] = 8;	/* just one lun, LUN 0, size 8 bytes */

	return 0;
}

/**
 *	ata_scsi_reqfill -
 *	@args:
 *	@actor:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_scsi_reqfill(struct ata_scsi_args *args,
			     unsigned int (*actor)
			     		  (struct ata_scsi_args *args,
			     		   u8 *reqbuf, unsigned int buflen))
{
	u8 *reqbuf;
	unsigned int buflen, rc;
	Scsi_Cmnd *cmd = args->cmd;

	buflen = ata_scsi_get_reqbuf(cmd, &reqbuf);
	rc = actor(args, reqbuf, buflen);
	ata_scsi_put_reqbuf(cmd);

	if (rc)
		ata_bad_cdb(cmd, args->done);
	else {
		cmd->result = SAM_STAT_GOOD;
		args->done(cmd);
	}
}

/**
 *	ata_scsi_badcmd -
 *	@cmd:
 *	@done:
 *	@asc:
 *	@ascq:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_scsi_badcmd(Scsi_Cmnd *cmd,
			    void (*done)(Scsi_Cmnd *),
			    u8 asc, u8 ascq)
{
	DPRINTK("ENTER\n");
	cmd->result = SAM_STAT_CHECK_CONDITION;

	cmd->sense_buffer[0] = 0x70;
	cmd->sense_buffer[2] = ILLEGAL_REQUEST;
	cmd->sense_buffer[7] = 14 - 8;	/* addnl. sense len. FIXME: correct? */
	cmd->sense_buffer[12] = asc;
	cmd->sense_buffer[13] = ascq;

	done(cmd);
}

static inline void ata_bad_scsiop(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	ata_scsi_badcmd(cmd, done, 0x20, 0x00);
}

static inline void ata_bad_cdb(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	ata_scsi_badcmd(cmd, done, 0x24, 0x00);
}

/**
 *	atapi_cdb_send -
 *	@ap:
 *
 *	LOCKING:
 *
 */

static void atapi_cdb_send(struct ata_port *ap)
{
	struct ata_queued_cmd *qc;
	u8 status;

	qc = ata_qc_from_tag(ap, ap->active_tag);
	assert(qc->flags & ATA_QCFLAG_ACTIVE);

	/* sleep-wait for BSY to clear */
	DPRINTK("busy wait\n");
	if (ata_busy_sleep(ap, ATA_TMOUT_CDB_QUICK, ATA_TMOUT_CDB))
		goto err_out;

	/* make sure DRQ is set */
	status = ata_chk_status(&ap->ioaddr);
	if ((status & ATA_DRQ) == 0)
		goto err_out;

	/* send SCSI cdb */
	DPRINTK("send cdb\n");
	outsl(ap->ioaddr.cmd_addr + ATA_REG_DATA,
	      qc->scsicmd->cmnd, ap->host->max_cmd_len / 4);

	/* if we are DMA'ing, irq handler takes over from here */
	if (qc->tf.feature == ATAPI_PKT_DMA)
		goto out;

	/* sleep-wait for BSY to clear */
	DPRINTK("busy wait 2\n");
	if (ata_busy_sleep(ap, ATA_TMOUT_CDB_QUICK, ATA_TMOUT_CDB))
		goto err_out;

	/* wait for BSY,DRQ to clear */
	status = ata_wait_idle(&ap->ioaddr);
	if (status & (ATA_BUSY | ATA_DRQ))
		goto err_out;

	/* transaction completed, indicate such to scsi stack */
	/* FIXME: sg_clean probably needed */
	ata_qc_complete(ap, qc, status, 0, 0);
	ata_irq_on(ap);

out:
	ap->thr_state = THR_IDLE;
	return;

err_out:
	/* FIXME: sg_clean probably needed */
	ata_qc_complete(ap, qc, ATA_ERR, 0, 0);
	goto out;
}

/**
 *	atapi_scsi_queuecmd -
 *	@ap:
 *	@dev:
 *	@cmd:
 *	@done:
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void atapi_scsi_queuecmd(struct ata_port *ap, struct ata_device *dev,
			       Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	struct ata_queued_cmd *qc;
	u8 *scsicmd = cmd->cmnd, status;
	unsigned int doing_dma = 0;

	VPRINTK("ENTER, drv_stat = 0x%x\n", ata_chk_status(&ap->ioaddr));

	if (cmd->sc_data_direction == SCSI_DATA_UNKNOWN) {
		DPRINTK("unknown data, scsicmd 0x%x\n", scsicmd[0]);
		ata_bad_cdb(cmd, done);
		return;
	}

	switch(scsicmd[0]) {
	case READ_6:
	case WRITE_6:
	case MODE_SELECT:
	case MODE_SENSE:
		DPRINTK("read6/write6/modesel/modesense trap\n");
		ata_bad_scsiop(cmd, done);
		return;

	default:
		/* do nothing */
		break;
	}

	qc = ata_qc_new_init(ap, dev, cmd, done);
	if (!qc)
		return;

	qc->flags |= ATA_QCFLAG_ATAPI;

	qc->tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	if (cmd->sc_data_direction == SCSI_DATA_WRITE) {
		qc->flags |= ATA_QCFLAG_WRITE;
		DPRINTK("direction: write\n");
	}

	qc->tf.command = ATA_CMD_PACKET;

	/* set up SG table */
	if (cmd->sc_data_direction == SCSI_DATA_NONE) {
		qc->flags |= ATA_QCFLAG_ACTIVE;
		ata_dev_select(ap, dev->devno, 1, 0);

		DPRINTK("direction: none\n");
		qc->tf.ctl |= ATA_NIEN;	/* disable interrupts */
		ata_tf_to_host_nolock(ap, &qc->tf, BUS_PACKET);
	} else {
		doing_dma = 1;

		qc->flags |= ATA_QCFLAG_DMA;
		qc->tf.feature = ATAPI_PKT_DMA;

		/* select device, send command to hardware */
		if (ata_qc_issue(qc))
			goto err_out;
	}

	status = ata_busy_wait(&ap->ioaddr, ATA_BUSY, 1000);
	if (status & ATA_BUSY) {
		ata_thread_wake(ap, THR_PACKET);
		return;
	}
	if ((status & ATA_DRQ) == 0)
		goto err_out;

	DPRINTK("writing cdb\n");
	outsl(ap->ioaddr.cmd_addr + ATA_REG_DATA,
	      scsicmd, ap->host->max_cmd_len / 4);

	if (!doing_dma)
		ata_thread_wake(ap, THR_PACKET);

	VPRINTK("EXIT\n");
	return;

err_out:
	if (!doing_dma)
		ata_irq_on(ap);	/* re-enable interrupts */
	ata_bad_cdb(cmd, done);
	DPRINTK("EXIT - badcmd\n");
}

/**
 *	ata_scsi_queuecmd -
 *	@cmd:
 *	@done:
 *
 *	LOCKING:
 *	Releases scsi-layer-held lock, and obtains host_set lock.
 *
 *	RETURNS:
 *
 */

int ata_scsi_queuecmd(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	u8 *scsicmd = cmd->cmnd;
	struct ata_port *ap;
	struct ata_device *dev;
	struct ata_scsi_args args;
	const unsigned int atapi_support =
#ifdef CONFIG_SCSI_ATA_ATAPI
					   1;
#else
					   0;
#endif

	/* Note: spin_lock_irqsave is held by caller... */
	spin_unlock(&io_request_lock);

	ap = (struct ata_port *) &cmd->host->hostdata[0];

	DPRINTK("CDB (%u:%d,%d,%d) %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		ap->id,
		cmd->channel, cmd->target, cmd->lun,
		scsicmd[0], scsicmd[1], scsicmd[2], scsicmd[3],
		scsicmd[4], scsicmd[5], scsicmd[6], scsicmd[7],
		scsicmd[8]);

	/* skip commands not addressed to targets we care about */
	if ((cmd->channel != 0) || (cmd->lun != 0) ||
	    (cmd->target >= ATA_MAX_DEVICES)) {
		cmd->result = (DID_BAD_TARGET << 16); /* FIXME: correct? */
		done(cmd);
		goto out;
	}

	spin_lock(&ap->host_set->lock);

	dev = &ap->device[cmd->target];

	if (!ata_dev_present(dev)) {
		DPRINTK("no device\n");
		cmd->result = (DID_BAD_TARGET << 16); /* FIXME: correct? */
		done(cmd);
		goto out_unlock;
	}

	if (dev->class == ATA_DEV_ATAPI) {
		if (atapi_support)
			atapi_scsi_queuecmd(ap, dev, cmd, done);
		else {
			cmd->result = (DID_BAD_TARGET << 16); /* correct? */
			done(cmd);
		}
		goto out_unlock;
	}

	/* fast path */
	switch(scsicmd[0]) {
		case READ_6:
		case WRITE_6:
			ata_do_rw(ap, dev, cmd, done, 6);
			goto out_unlock;

		case READ_10:
		case WRITE_10:
			ata_do_rw(ap, dev, cmd, done, 10);
			goto out_unlock;

		case READ_16:
		case WRITE_16:
			ata_do_rw(ap, dev, cmd, done, 16);
			goto out_unlock;

		default:
			/* do nothing */
			break;
	}

	/*
	 * slow path
	 */

	args.ap = ap;
	args.dev = dev;
	args.cmd = cmd;
	args.done = done;

	switch(scsicmd[0]) {
		case TEST_UNIT_READY:		/* FIXME: correct? */
		case FORMAT_UNIT:		/* FIXME: correct? */
		case SEND_DIAGNOSTIC:		/* FIXME: correct? */
			ata_scsi_reqfill(&args, ata_scsiop_noop);
			break;

		case INQUIRY:
			if (scsicmd[1] & 2)	           /* is CmdDt set?  */
				ata_bad_cdb(cmd, done);
			else if ((scsicmd[1] & 1) == 0)    /* is EVPD clear? */
				ata_scsi_reqfill(&args, ata_scsiop_inq_std);
			else if (scsicmd[2] == 0x00)
				ata_scsi_reqfill(&args, ata_scsiop_inq_00);
			else if (scsicmd[2] == 0x80)
				ata_scsi_reqfill(&args, ata_scsiop_inq_80);
			else if (scsicmd[2] == 0x83)
				ata_scsi_reqfill(&args, ata_scsiop_inq_83);
			else
				ata_bad_cdb(cmd, done);
			break;

		case MODE_SENSE:
		case MODE_SENSE_10:
			ata_scsi_reqfill(&args, ata_scsiop_mode_sense);
			break;

		case MODE_SELECT:	/* unconditionally return */
		case MODE_SELECT_10:	/* bad-field-in-cdb */
			ata_bad_cdb(cmd, done);
			break;

		case SYNCHRONIZE_CACHE:
			if ((dev->flags & ATA_DFLAG_WCACHE) == 0)
				ata_bad_scsiop(cmd, done);
			else
				ata_scsi_reqfill(&args, ata_scsiop_sync_cache);
			break;

		case READ_CAPACITY:
			ata_scsi_reqfill(&args, ata_scsiop_read_cap);
			break;

		case SERVICE_ACTION_IN:
			if ((scsicmd[1] & 0x1f) == SAI_READ_CAPACITY_16)
				ata_scsi_reqfill(&args, ata_scsiop_read_cap);
			else
				ata_bad_cdb(cmd, done);
			break;

		case REPORT_LUNS:
			ata_scsi_reqfill(&args, ata_scsiop_report_luns);
			break;

		/* mandantory commands we haven't implemented yet */
		case REQUEST_SENSE:

		/* all other commands */
		default:
			ata_bad_scsiop(cmd, done);
			break;
	}

out_unlock:
	spin_unlock(&ap->host_set->lock);
out:
	spin_lock(&io_request_lock);
	return 0;
}

/**
 *	ata_thread_kill - kill per-port kernel thread
 *	@ap: port those thread is to be killed
 *
 *	LOCKING:
 *
 */

static int ata_thread_kill(struct ata_port *ap)
{
	int ret = 0;

	if (ap->thr_pid >= 0) {
		ap->time_to_die = 1;
		wmb();
		ret = kill_proc(ap->thr_pid, SIGTERM, 1);
		if (ret)
			printk(KERN_ERR "ata%d: unable to kill kernel thread\n",
			       ap->id);
		else
			wait_for_completion(&ap->thr_exited);
	}

	return ret;
}

/**
 *	ata_host_remove -
 *	@ap:
 *	@do_unregister:
 *
 *	LOCKING:
 */

static void ata_host_remove(struct ata_port *ap, unsigned int do_unregister)
{
	struct Scsi_Host *sh = ap->host;

	DPRINTK("ENTER\n");

	if (do_unregister)
		scsi_unregister(sh);

	outl(0, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);
	pci_free_consistent(ap->host_set->pdev, ATA_PRD_TBL_SZ, ap->prd, ap->prd_dma);
}

/**
 *	ata_host_init -
 *	@host:
 *	@ent:
 *	@port_no:
 *
 *	LOCKING:
 *
 */

static void ata_host_init(struct ata_port *ap, struct Scsi_Host *host,
			  struct ata_host_set *host_set,
			  struct ata_probe_ent *ent, unsigned int port_no)
{
	unsigned int i;

	host->max_id = 16;
	host->max_lun = 1;
	host->max_channel = 1;
	host->unique_id = ata_unique_id++;
	host->max_cmd_len = 12;
	host->pci_dev = ent->pdev;

	ap->flags = ATA_FLAG_PORT_DISABLED;
	ap->id = host->unique_id;
	ap->host = host;
	ap->ctl = ATA_DEVCTL_OBS;
	ap->host_set = host_set;
	ap->port_no = port_no;
	ap->pio_mask = ent->pio_mask;
	ap->udma_mask = ent->udma_mask;
	ap->flags |= ent->host_flags;
	ap->ops = ent->host_info;
	ap->bus_state = BUS_UNKNOWN;
	ap->thr_state = THR_PROBE_START;
	ap->cbl = ATA_CBL_NONE;
	ap->device[0].flags = ATA_DFLAG_MASTER;
	ap->active_tag = 0xfafbfcfd;	/* poison value */

	/* ata_engine init */
	ap->eng.flags = 0;
	INIT_LIST_HEAD(&ap->eng.q);

	for (i = 0; i < ATA_MAX_DEVICES; i++)
		ap->device[i].devno = i;

	init_completion(&ap->thr_exited);
	init_MUTEX_LOCKED(&ap->probe_sem);
	init_MUTEX_LOCKED(&ap->sem);
	init_MUTEX_LOCKED(&ap->thr_sem);

	init_timer(&ap->thr_timer);
	ap->thr_timer.function = ata_thread_timer;
	ap->thr_timer.data = (unsigned long) ap;

#ifdef ATA_IRQ_TRAP
	ap->stats.unhandled_irq = 1;
	ap->stats.idle_irq = 1;
#endif

	memcpy(&ap->ioaddr, &ent->port[port_no], sizeof(struct ata_ioports));
}

/**
 *	ata_host_add -
 *	@ent:
 *	@host_set:
 *	@port_no:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static struct ata_port * ata_host_add(struct ata_probe_ent *ent,
				      struct ata_host_set *host_set,
				      unsigned int port_no)
{
	struct pci_dev *pdev = ent->pdev;
	struct Scsi_Host *host;
	struct ata_port *ap;

	DPRINTK("ENTER\n");
	host = scsi_register(ent->sht, sizeof(struct ata_port));
	if (!host)
		return NULL;

	ap = (struct ata_port *) &host->hostdata[0];

	ata_host_init(ap, host, host_set, ent, port_no);

	ap->prd = pci_alloc_consistent(pdev, ATA_PRD_TBL_SZ, &ap->prd_dma);
	if (!ap->prd)
		goto err_out;
	DPRINTK("prd alloc, virt %p, dma %x\n", ap->prd, ap->prd_dma);

	ap->thr_pid = kernel_thread(ata_thread, ap, CLONE_FS | CLONE_FILES);
	if (ap->thr_pid < 0) {
		printk(KERN_ERR "ata%d: unable to start kernel thread\n",
		       ap->id);
		goto err_out_free;
	}

	return ap;

err_out_free:
	pci_free_consistent(ap->host_set->pdev, ATA_PRD_TBL_SZ, ap->prd, ap->prd_dma);
err_out:
	scsi_unregister(host);
	return NULL;
}

/**
 *	ata_device_add -
 *	@ent:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int ata_device_add(struct ata_probe_ent *ent)
{
	unsigned int count = 0, i;
	struct pci_dev *pdev = ent->pdev;
	struct ata_host_set *host_set;

	DPRINTK("ENTER\n");
	/* alloc a container for our list of ATA ports (buses) */
	host_set = kmalloc(sizeof(struct ata_host_set) +
			   (ent->n_ports * sizeof(void *)), GFP_KERNEL);
	if (!host_set)
		return 0;
	memset(host_set, 0, sizeof(struct ata_host_set) + (ent->n_ports * sizeof(void *)));
	spin_lock_init(&host_set->lock);

	host_set->pdev = pdev;
	host_set->n_hosts = ent->n_ports;
	host_set->irq = ent->irq;
	host_set->host_flags = ent->host_flags;

	/* register each port bound to this device */
	for (i = 0; i < ent->n_ports; i++) {
		struct ata_port *ap;

		ap = ata_host_add(ent, host_set, i);
		if (!ap)
			goto err_out;

		host_set->hosts[i] = ap;

		/* print per-port info to dmesg */
		printk(KERN_INFO "ata%u: %cATA max %s cmd 0x%lX ctl 0x%lX "
				 "bmdma 0x%lX irq %lu\n",
			ap->id,
			ap->flags & ATA_FLAG_SATA ? 'S' : 'P',
			ata_udma_string(ent->udma_mask),
	       		ap->ioaddr.cmd_addr,
	       		ap->ioaddr.ctl_addr,
	       		ap->ioaddr.bmdma_addr,
	       		ent->irq);

		count++;
	}

	if (!count) {
		kfree(host_set);
		return 0;
	}

	/* obtain irq, that is shared between channels */
	if (request_irq(ent->irq, ata_interrupt, ent->irq_flags,
			DRV_NAME, host_set))
		goto err_out;

	/* perform each probe synchronously */
	DPRINTK("probe begin\n");
	for (i = 0; i < count; i++) {
		struct ata_port *ap;

		ap = host_set->hosts[i];

		DPRINTK("ata%u: probe begin\n", ap->id);
		up(&ap->sem);		/* start probe */

		DPRINTK("ata%u: probe-wait begin\n", ap->id);
		down(&ap->probe_sem);	/* wait for end */

		DPRINTK("ata%u: probe-wait end\n", ap->id);
	}

	pci_set_drvdata(pdev, host_set);

	VPRINTK("EXIT, returning %u\n", ent->n_ports);
	return ent->n_ports; /* success */

err_out:
	for (i = 0; i < count; i++) {
		ata_host_remove(host_set->hosts[i], 1);
	}
	kfree(host_set);
	VPRINTK("EXIT, returning 0\n");
	return 0;
}

/**
 *	ata_scsi_detect -
 *	@sht:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

int ata_scsi_detect(Scsi_Host_Template *sht)
{
	struct list_head *node;
	struct ata_probe_ent *ent;
	int count = 0;

	VPRINTK("ENTER\n");

	sht->use_new_eh_code = 1;	/* IORL hack, part deux */

	spin_lock(&ata_module_lock);
	while (!list_empty(&ata_probe_list)) {
		node = ata_probe_list.next;
		ent = list_entry(node, struct ata_probe_ent, node);
		list_del(node);

		spin_unlock(&ata_module_lock);

		count += ata_device_add(ent);
		kfree(ent);

		spin_lock(&ata_module_lock);
	}
	spin_unlock(&ata_module_lock);

	VPRINTK("EXIT, returning %d\n", count);
	return count;
}

/**
 *	ata_scsi_release -
 *	@host:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

int ata_scsi_release(struct Scsi_Host *host)
{
	struct ata_port *ap = (struct ata_port *) &host->hostdata[0];

	DPRINTK("ENTER\n");

	ata_thread_kill(ap);	/* FIXME: check return val */

	ap->ops->port_disable(ap);
	ata_host_remove(ap, 0);

	DPRINTK("EXIT\n");
	return 1;
}

/**
 *	ata_pci_init_one -
 *	@pdev:
 *	@boards:
 *	@n_boards:
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

int ata_pci_init_one (struct pci_dev *pdev, struct ata_board **boards,
		      unsigned int n_boards)
{
	struct ata_probe_ent *probe_ent, *probe_ent2 = NULL;
	struct ata_board *board1, *board2;
	u8 tmp8, mask;
	unsigned int legacy_mode = 0;
	int rc;

	DPRINTK("ENTER\n");

	/* TODO: support transitioning to native mode? */
	pci_read_config_byte(pdev, PCI_CLASS_PROG, &tmp8);
	mask = (1 << 2) | (1 << 0);
	if ((tmp8 & mask) != mask)
		legacy_mode = (1 << 3);

	/* FIXME... */
	if ((!legacy_mode) && (n_boards > 1)) {
		printk(KERN_ERR "ata: BUG: native mode, n_boards > 1\n");
		return -EINVAL;
	}

	board1 = boards[0];
	if (n_boards > 1)
		board2 = boards[1];
	else
		board2 = board1;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	if (legacy_mode) {
		if (!request_region(0x1f0, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x1f0;
			res.end = 0x1f0 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 0);
			else
				printk(KERN_WARNING "ata: 0x1f0 IDE port busy\n");
		} else
			legacy_mode |= (1 << 0);

		if (!request_region(0x170, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x170;
			res.end = 0x170 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 1);
			else
				printk(KERN_WARNING "ata: 0x170 IDE port busy\n");
		} else
			legacy_mode |= (1 << 1);
	}

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_legacy_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		rc = -ENOMEM;
		goto err_out_legacy_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	probe_ent->pdev = pdev;
	INIT_LIST_HEAD(&probe_ent->node);

	if (legacy_mode) {
		probe_ent2 = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
		if (!probe_ent2) {
			rc = -ENOMEM;
			goto err_out_free_ent;
		}

		memset(probe_ent2, 0, sizeof(*probe_ent));
		probe_ent2->pdev = pdev;
		INIT_LIST_HEAD(&probe_ent2->node);
	}

	probe_ent->port[0].bmdma_addr = pci_resource_start(pdev, 4);
	probe_ent->sht = board1->sht;
	probe_ent->host_flags = board1->host_flags;
	probe_ent->pio_mask = board1->pio_mask;
	probe_ent->udma_mask = board1->udma_mask;
	probe_ent->host_info = board1->host_info;

	if (legacy_mode) {
		probe_ent->host_flags |= ATA_FLAG_LEGACY;

		probe_ent->port[0].cmd_addr = 0x1f0;
		probe_ent->port[0].ctl_addr = 0x3f6;
		probe_ent->n_ports = 1;
		probe_ent->irq = 14;

		probe_ent2->port[0].cmd_addr = 0x170;
		probe_ent2->port[0].ctl_addr = 0x376;
		probe_ent2->port[0].bmdma_addr = pci_resource_start(pdev, 4)+8;
		probe_ent2->n_ports = 1;
		probe_ent2->irq = 15;

		probe_ent2->sht = board2->sht;
		probe_ent2->host_flags = board2->host_flags | ATA_FLAG_LEGACY;
		probe_ent2->pio_mask = board2->pio_mask;
		probe_ent2->udma_mask = board2->udma_mask;
		probe_ent2->host_info = board2->host_info;
	} else {
		probe_ent->port[0].cmd_addr = pci_resource_start(pdev, 0);
		probe_ent->port[0].ctl_addr =
			pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;

		probe_ent->port[1].cmd_addr = pci_resource_start(pdev, 2);
		probe_ent->port[1].ctl_addr =
			pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
		probe_ent->port[1].bmdma_addr = pci_resource_start(pdev, 4) + 8;

		probe_ent->n_ports = 2;
		probe_ent->irq = pdev->irq;
		probe_ent->irq_flags = SA_SHIRQ;
	}

	pci_set_master(pdev);

	spin_lock(&ata_module_lock);
	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			list_add_tail(&probe_ent->node, &ata_probe_list);
		else
			kfree(probe_ent);
		if (legacy_mode & (1 << 1))
			list_add_tail(&probe_ent2->node, &ata_probe_list);
		else
			kfree(probe_ent2);
	} else {
		list_add_tail(&probe_ent->node, &ata_probe_list);
	}
	spin_unlock(&ata_module_lock);

	return 0;

err_out_free_ent:
	kfree(probe_ent);
err_out_legacy_regions:
	if (legacy_mode & (1 << 0))
		release_region(0x1f0, 8);
	if (legacy_mode & (1 << 1))
		release_region(0x170, 8);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

/**
 *	ata_pci_remove_one -
 *	@pdev:
 *
 *	LOCKING:
 */

void ata_pci_remove_one (struct pci_dev *pdev)
{
	struct ata_host_set *host_set = pci_get_drvdata(pdev);
	struct ata_port *ap;
	Scsi_Host_Template *sht;
	int rc;

	/* FIXME: this unregisters all hosts attached to the
	 * Scsi_Host_Template given.  We _might_ have multiple
	 * templates (though we don't ATM), so this is ok... for now.
	 */
	ap = host_set->hosts[0];
	sht = ap->host->hostt;
	rc = scsi_unregister_module(MODULE_SCSI_HA, sht);
	/* FIXME: handle 'rc' failure? */

	free_irq(host_set->irq, host_set);

	pci_release_regions(pdev);

	if (host_set->host_flags & ATA_FLAG_LEGACY) {
		release_region(0x1f0, 8);
		release_region(0x170, 8);
	}

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	kfree(host_set);
}

/**
 *	ata_init -
 *
 *	LOCKING:
 *
 *	RETURNS:
 *
 */

static int __init ata_init(void)
{
	printk(KERN_DEBUG "libata version " DRV_VERSION " loaded.\n");
	return 0;
}

module_init(ata_init);

/*
 * libata is essentially a library of internal helper functions for
 * low-level ATA host controller drivers.  As such, the API/ABI is
 * likely to change as new drivers are added and updated.
 * Do not depend on ABI/API stability.
 */
EXPORT_SYMBOL_GPL(ata_tf_load_pio);
EXPORT_SYMBOL_GPL(ata_port_probe);
EXPORT_SYMBOL_GPL(ata_port_disable);
EXPORT_SYMBOL_GPL(ata_pci_init_one);
EXPORT_SYMBOL_GPL(ata_pci_remove_one);
EXPORT_SYMBOL_GPL(ata_scsi_detect);
EXPORT_SYMBOL_GPL(ata_scsi_release);
EXPORT_SYMBOL_GPL(ata_scsi_queuecmd);
EXPORT_SYMBOL_GPL(ata_scsi_error);

