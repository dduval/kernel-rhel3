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

 */

#ifndef __LINUX_ATA_H__
#define __LINUX_ATA_H__

#include <linux/delay.h>
#include <asm/io.h>

/*
 * compile-time options
 */
#undef ATA_FORCE_PIO		/* do not configure or use DMA */
#undef ATA_DEBUG		/* debugging output */
#undef ATA_VERBOSE_DEBUG	/* yet more debugging output */
#undef ATA_IRQ_TRAP		/* define to ack screaming irqs */
#undef ATA_NDEBUG		/* define to disable quick runtime checks */


/* note: prints function name for you */
#ifdef ATA_DEBUG
#define DPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#ifdef ATA_VERBOSE_DEBUG
#define VPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define VPRINTK(fmt, args...)
#endif	/* ATA_VERBOSE_DEBUG */
#else
#define DPRINTK(fmt, args...)
#define VPRINTK(fmt, args...)
#endif	/* ATA_DEBUG */

#ifdef ATA_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {                                   \
        printk(KERN_ERR "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif

enum {
	/* various global constants */
	ATA_MAX_PORTS		= 2,
	ATA_MAX_DEVICES		= 2,	/* per bus/port */
	ATA_DEF_QUEUE		= 1,
	ATA_MAX_QUEUE		= 1,
	ATA_MAX_PRD		= 256,	/* we could make these 256/256 */
	ATA_MAX_SECTORS		= 200,	/* FIXME */
	ATA_MAX_BUS		= 2,
	ATA_SECT_SIZE		= 512,
	ATA_SECT_SIZE_MASK	= (ATA_SECT_SIZE - 1),
	ATA_SECT_DWORDS		= ATA_SECT_SIZE / sizeof(u32),
	ATA_ID_WORDS		= 256,
	ATA_ID_PROD_OFS		= 27,
	ATA_ID_SERNO_OFS	= 10,
	ATA_ID_MAJOR_VER	= 80,
	ATA_ID_PIO_MODES	= 64,
	ATA_ID_UDMA_MODES	= 88,
	ATA_ID_PIO4		= (1 << 1),
	ATA_DEF_BUSY_WAIT	= 10000,
	ATA_PCI_CTL_OFS		= 2,
	ATA_SHORT_PAUSE		= (HZ >> 6) + 1,
	ATA_SERNO_LEN		= 20,

	ATA_SHT_EMULATED	= 1,
	ATA_SHT_NEW_EH_CODE	= 1,
	ATA_SHT_CMD_PER_LUN	= 1,
	ATA_SHT_THIS_ID		= -1,
	ATA_SHT_USE_CLUSTERING	= 0,	/* FIXME: which is best, 0 or 1?  */

	/* DMA-related */
	ATA_PRD_SZ		= 8,
	ATA_PRD_TBL_SZ		= (ATA_MAX_PRD * ATA_PRD_SZ),
	ATA_PRD_EOT		= (1 << 31),	/* end-of-table flag */

	ATA_DMA_MASK		= 0xffffffff,
	ATA_DMA_TABLE_OFS	= 4,
	ATA_DMA_STATUS		= 2,
	ATA_DMA_CMD		= 0,
	ATA_DMA_WR		= (1 << 3),
	ATA_DMA_START		= (1 << 0),
	ATA_DMA_INTR		= (1 << 2),
	ATA_DMA_ERR		= (1 << 1),
	ATA_DMA_ACTIVE		= (1 << 0),

	/* bits in ATA command block registers */
	ATA_HOB			= (1 << 7),	/* LBA48 selector */
	ATA_NIEN		= (1 << 1),	/* disable-irq flag */
	ATA_LBA			= (1 << 6),	/* LBA28 selector */
	ATA_DEV1		= (1 << 4),	/* Select Device 1 (slave) */
	ATA_BUSY		= (1 << 7),	/* BSY status bit */
	ATA_DEVICE_OBS		= (1 << 7) | (1 << 5), /* obs bits in dev reg */
	ATA_DEVCTL_OBS		= (1 << 3),	/* obsolete bit in devctl reg */
	ATA_DRQ			= (1 << 3),	/* data request i/o */
	ATA_ERR			= (1 << 0),	/* have an error */
	ATA_SRST		= (1 << 2),	/* software reset */
	ATA_ABORTED		= (1 << 2),	/* command aborted */

	/* ATA command block registers */
	ATA_REG_DATA		= 0x00,
	ATA_REG_ERR		= 0x01,
	ATA_REG_NSECT		= 0x02,
	ATA_REG_LBAL		= 0x03,
	ATA_REG_LBAM		= 0x04,
	ATA_REG_LBAH		= 0x05,
	ATA_REG_DEVICE		= 0x06,
	ATA_REG_STATUS		= 0x07,

	ATA_REG_FEATURE		= ATA_REG_ERR, /* and their aliases */
	ATA_REG_CMD		= ATA_REG_STATUS,
	ATA_REG_BYTEL		= ATA_REG_LBAM,
	ATA_REG_BYTEH		= ATA_REG_LBAH,
	ATA_REG_DEVSEL		= ATA_REG_DEVICE,
	ATA_REG_IRQ		= ATA_REG_NSECT,

	/* struct ata_device stuff */
	ATA_DFLAG_LBA48		= (1 << 0), /* device supports LBA48 */
	ATA_DFLAG_PIO		= (1 << 1), /* device currently in PIO mode */
	ATA_DFLAG_MASTER	= (1 << 2), /* is device 0? */
	ATA_DFLAG_WCACHE	= (1 << 3), /* has write cache we can
					     * (hopefully) flush? */

	ATA_DEV_UNKNOWN		= 0,	/* unknown device */
	ATA_DEV_ATA		= 1,	/* ATA device */
	ATA_DEV_ATA_UNSUP	= 2,	/* ATA device (unsupported) */
	ATA_DEV_ATAPI		= 3,	/* ATAPI device */
	ATA_DEV_ATAPI_UNSUP	= 4,	/* ATAPI device (unsupported) */
	ATA_DEV_NONE		= 5,	/* no device */

	/* struct ata_port flags */
	ATA_FLAG_SLAVE_POSS	= (1 << 1), /* host supports slave dev */
					    /* (doesn't imply presence) */
	ATA_FLAG_PORT_DISABLED	= (1 << 2), /* port is disabled, ignore it */
	ATA_FLAG_SATA		= (1 << 3),
	ATA_FLAG_LEGACY		= (1 << 11),

	/* struct ata_taskfile flags */
	ATA_TFLAG_LBA48		= (1 << 0),
	ATA_TFLAG_DATAREG	= (1 << 1),
	ATA_TFLAG_ISADDR	= (1 << 2), /* enable r/w to nsect/lba regs */
	ATA_TFLAG_DEVICE	= (1 << 4), /* enable r/w to device reg */

	ATA_QCFLAG_WRITE	= (1 << 0), /* read==0, write==1 */
	ATA_QCFLAG_ACTIVE	= (1 << 1), /* cmd not yet ack'd to scsi lyer */
	ATA_QCFLAG_DMA		= (1 << 2), /* data delivered via DMA */
	ATA_QCFLAG_ATAPI	= (1 << 3), /* is ATAPI packet command? */

	/* struct ata_engine atomic flags (use test_bit, etc.) */
	ATA_EFLG_ACTIVE		= 0,	/* engine is active */

	/* ATA device commands */
	ATA_CMD_EDD		= 0x90,	/* execute device diagnostic */
	ATA_CMD_ID_ATA		= 0xEC,
	ATA_CMD_ID_ATAPI	= 0xA1,
	ATA_CMD_READ		= 0xC8,
	ATA_CMD_READ_EXT	= 0x25,
	ATA_CMD_WRITE		= 0xCA,
	ATA_CMD_WRITE_EXT	= 0x35,
	ATA_CMD_PIO_READ	= 0x20,
	ATA_CMD_PIO_READ_EXT	= 0x24,
	ATA_CMD_PIO_WRITE	= 0x30,
	ATA_CMD_PIO_WRITE_EXT	= 0x34,
	ATA_CMD_SET_FEATURES	= 0xEF,
	ATA_CMD_PACKET		= 0xA0,

	/* various lengths of time */
	ATA_TMOUT_EDD		= 5 * HZ,	/* hueristic */
	ATA_TMOUT_PIO		= 30 * HZ,
	ATA_TMOUT_BOOT		= 30 * HZ,	/* hueristic */
	ATA_TMOUT_BOOT_QUICK	= 7 * HZ,	/* hueristic */
	ATA_TMOUT_CDB		= 30 * HZ,
	ATA_TMOUT_CDB_QUICK	= 5 * HZ,

	/* ATA bus states */
	BUS_UNKNOWN		= 0,
	BUS_DMA			= 1,
	BUS_IDLE		= 2,
	BUS_NOINTR		= 3,
	BUS_NODATA		= 4,
	BUS_TIMER		= 5,
	BUS_PIO			= 6,
	BUS_EDD			= 7,
	BUS_IDENTIFY		= 8,
	BUS_PACKET		= 9,

	/* thread states */
	THR_UNKNOWN		= 0,
	THR_CHECKPORT		= 1,
	THR_BUS_RESET		= 2,
	THR_AWAIT_DEATH		= 3,
	THR_IDENTIFY		= 4,
	THR_CONFIG_TIMINGS	= 5,
	THR_CONFIG_DMA		= 6,
	THR_PROBE_FAILED	= 7,
	THR_IDLE		= 8,
	THR_PROBE_SUCCESS	= 9,
	THR_PROBE_START		= 10,
	THR_CONFIG_FORCE_PIO	= 11,
	THR_PIO_POLL		= 12,
	THR_PIO_TMOUT		= 13,
	THR_PIO			= 14,
	THR_PIO_LAST		= 15,
	THR_PIO_LAST_POLL	= 16,
	THR_PIO_ERR		= 17,
	THR_PACKET		= 18,

	/* SATA port states */
	PORT_UNKNOWN		= 0,
	PORT_ENABLED		= 1,
	PORT_DISABLED		= 2,

	/* SETFEATURES stuff */
	SETFEATURES_XFER	= 0x03,
	XFER_UDMA_7		= 0x47,
	XFER_UDMA_6		= 0x46,
	XFER_UDMA_5		= 0x45,
	XFER_UDMA_4		= 0x44,
	XFER_UDMA_3		= 0x43,
	XFER_UDMA_2		= 0x42,
	XFER_UDMA_1		= 0x41,
	XFER_UDMA_0		= 0x40,
	XFER_PIO_4		= 0x0C,
	XFER_PIO_3		= 0x0B,

	/* ATAPI stuff */
	ATAPI_PKT_DMA		= (1 << 0),

	/* cable types */
	ATA_CBL_NONE		= 0,
	ATA_CBL_PATA40		= 1,
	ATA_CBL_PATA80		= 2,
	ATA_CBL_SATA		= 3,

	/* ata_qc_cb_t flags - note uses above ATA_QCFLAG_xxx namespace,
	 * but not numberspace
	 */
	ATA_QCFLAG_TIMEOUT	= (1 << 0),
};

/* forward declarations */
struct ata_host_info;
struct ata_port;
struct ata_queued_cmd;

/* typedefs */
typedef void (*ata_qc_cb_t) (struct ata_queued_cmd *qc, unsigned int flags);

/* core structures */
struct ata_prd {
	u32			addr;
	u32			flags_len;
} __attribute__((packed));

struct ata_ioports {
	unsigned long		cmd_addr;
	unsigned long		ctl_addr;
	unsigned long		bmdma_addr;
};

struct ata_probe_ent {
	struct list_head	node;
	struct pci_dev		*pdev;
	struct ata_host_info	*host_info;
	Scsi_Host_Template	*sht;
	struct ata_ioports	port[ATA_MAX_PORTS];
	unsigned int		n_ports;
	unsigned int		pio_mask;
	unsigned int		udma_mask;
	unsigned int		legacy_mode;
	unsigned long		irq;
	unsigned int		irq_flags;
	unsigned long		host_flags;
};

struct ata_host_set {
	spinlock_t		lock;
	struct pci_dev		*pdev;
	unsigned long		irq;
	unsigned int		n_hosts;
	struct ata_port *	hosts[0];
	unsigned long		host_flags;
};

struct ata_taskfile {
	unsigned long		flags;		/* ATA_TFLAG_xxx */

	u8			data;		/* command registers */
	u8			feature;
	u8			nsect;
	u8			lbal;
	u8			lbam;
	u8			lbah;
	u8			device;

	u8			command;	/* IO operation */

	u8			hob_feature;	/* additional data */
	u8			hob_nsect;	/* to support LBA48 */
	u8			hob_lbal;
	u8			hob_lbam;
	u8			hob_lbah;
	u8			hob_data;

	u8			ctl;		/* control reg/altstatus */
};

struct ata_queued_cmd {
	struct list_head	node;
	unsigned long		flags;		/* ATA_QCFLAG_xxx */
	unsigned int		tag;
	unsigned int		n_elem;
	unsigned int		nsect;
	unsigned int		cursect;
	unsigned int		cursg;
	unsigned int		cursg_ofs;
	Scsi_Cmnd		*scsicmd;
	void			(*scsidone)(Scsi_Cmnd *);
	struct ata_taskfile	tf;
	struct scatterlist	sgent;
	ata_qc_cb_t		callback;
	struct ata_port		*ap;
	struct ata_device	*dev;
};

struct ata_host_stats {
	unsigned long		unhandled_irq;
	unsigned long		idle_irq;
	unsigned long		rw_reqbuf;
};

struct ata_device {
	u64			n_sectors;	/* size of device, if ATA */
	unsigned long		flags;		/* ATA_DFLAG_xxx */
	unsigned int		class;		/* ATA_DEV_xxx */
	unsigned int		devno;		/* 0 or 1 */
	u16			id[ATA_ID_WORDS]; /* IDENTIFY xxx DEVICE data */
	unsigned int		pio_mode;
	unsigned int		udma_mode;

	unsigned char		vendor[8];	/* space-padded, not ASCIIZ */
	unsigned char		product[16];
};

struct ata_engine {
	unsigned long		flags;
	struct list_head	q;
};

struct ata_port {
	struct Scsi_Host	*host;	/* our co-allocated scsi host */
	struct ata_host_info	*ops;
	unsigned long		flags;	/* ATA_FLAG_xxx */
	unsigned int		id;	/* unique id req'd by scsi midlyr */
	unsigned int		port_no; /* unique port #; from zero */

	struct ata_prd		*prd;	 /* our SG list */
	dma_addr_t		prd_dma; /* and its DMA mapping */

	struct ata_ioports	ioaddr;	/* ATA cmd/ctl/dma register blocks */

	u8			ctl;	/* cache of ATA control register */
	u8			dmactl; /* cache of DMA control register */
	u8			devsel;	/* cache of Device Select reg */
	unsigned int		bus_state;
	unsigned int		port_state;
	unsigned int		pio_mask;
	unsigned int		udma_mask;
	unsigned int		cbl;	/* cable type; ATA_CBL_xxx */

	struct ata_engine	eng;

	struct ata_device	device[ATA_MAX_DEVICES];

	struct ata_queued_cmd	qcmd[ATA_MAX_QUEUE];
	unsigned long		qactive;
	unsigned int		active_tag;

	struct ata_host_stats	stats;
	struct ata_host_set	*host_set;

	struct semaphore	sem;
	struct semaphore	probe_sem;

	unsigned int		thr_state;
	int			time_to_die;
	pid_t			thr_pid;
	struct completion	thr_exited;
	struct semaphore	thr_sem;
	struct timer_list	thr_timer;
	unsigned long		thr_timeout;
};

struct ata_host_info {
	void (*port_probe) (struct ata_port *);
	void (*port_disable) (struct ata_port *);

	void (*set_piomode) (struct ata_port *, struct ata_device *,
			     unsigned int);
	void (*set_udmamode) (struct ata_port *, struct ata_device *,
			     unsigned int);

	void (*tf_load) (struct ata_ioports *ioaddr, struct ata_taskfile *tf);
};

struct ata_board {
	Scsi_Host_Template	*sht;
	unsigned long		host_flags;
	unsigned long		pio_mask;
	unsigned long		udma_mask;
	struct ata_host_info	*host_info;
};

#define ata_id_is_ata(dev)	(((dev)->id[0] & (1 << 15)) == 0)
#define ata_id_has_lba48(dev)	((dev)->id[83] & (1 << 10))
#define ata_id_has_lba(dev)	((dev)->id[49] & (1 << 8))
#define ata_id_has_dma(dev)	((dev)->id[49] & (1 << 9))
#define ata_id_u32(dev,n)	\
	(((u32) (dev)->id[(n) + 1] << 16) | ((u32) (dev)->id[(n)]))
#define ata_id_u64(dev,n)	\
	( ((u64) dev->id[(n) + 3] << 48) |	\
	  ((u64) dev->id[(n) + 2] << 32) |	\
	  ((u64) dev->id[(n) + 1] << 16) |	\
	  ((u64) dev->id[(n) + 0]) )  

extern void ata_port_probe(struct ata_port *);
extern void ata_port_disable(struct ata_port *);
extern int ata_pci_init_one (struct pci_dev *pdev, struct ata_board **boards,
			     unsigned int n_boards);
extern void ata_pci_remove_one (struct pci_dev *pdev);
extern int ata_scsi_detect(Scsi_Host_Template *sht);
extern int ata_scsi_release(struct Scsi_Host *host);
extern int ata_scsi_queuecmd(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *));
extern int ata_scsi_error(struct Scsi_Host *host);
extern void ata_tf_load_pio(struct ata_ioports *ioaddr,struct ata_taskfile *tf);

static inline unsigned long msecs_to_jiffies(unsigned long msecs)
{
	return ((HZ * msecs + 999) / 1000);
}

static inline unsigned int ata_dev_present(struct ata_device *dev)
{
	return ((dev->class == ATA_DEV_ATA) ||
		(dev->class == ATA_DEV_ATAPI));
}

static inline u8 ata_chk_status(struct ata_ioports *ioaddr)
{
	return inb(ioaddr->cmd_addr + ATA_REG_STATUS);
}

static inline u8 ata_altstatus(struct ata_ioports *ioaddr)
{
	return inb(ioaddr->ctl_addr);
}

static inline void ata_pause(struct ata_ioports *ioaddr)
{
	ata_altstatus(ioaddr);
	ndelay(400);
}

static inline u8 ata_busy_wait(struct ata_ioports *ioaddrs, unsigned int bits,
			       unsigned int max)
{
	u8 status;

	do {
		udelay(10);
		status = ata_chk_status(ioaddrs);
		max--;
	} while ((status & bits) && (max > 0));

	return status;
}

static inline u8 ata_wait_idle(struct ata_ioports *ioaddrs)
{
	u8 status = ata_busy_wait(ioaddrs, ATA_BUSY | ATA_DRQ, 1000);

	if (status & (ATA_BUSY | ATA_DRQ)) {
		unsigned long l = ioaddrs->cmd_addr + ATA_REG_STATUS;
		printk(KERN_WARNING
		       "ATA: abnormal status 0x%X on port 0x%lX\n",
		       status, l);
	}

	return status;
}

static inline struct ata_queued_cmd *ata_qc_from_tag (struct ata_port *ap,
						      unsigned int tag)
{
	return &ap->qcmd[tag];
}

static inline void ata_tf_init(struct ata_port *ap, struct ata_taskfile *tf, unsigned int device)
{
	memset(tf, 0, sizeof(*tf));

	tf->ctl = ap->ctl;
	if (device == 0)
		tf->device = ATA_DEVICE_OBS;
	else
		tf->device = ATA_DEVICE_OBS | ATA_DEV1;
}

static inline u8 ata_irq_on(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	ap->ctl &= ~ATA_NIEN;
	outb(ap->ctl, ioaddr->ctl_addr);

	return ata_wait_idle(ioaddr);
}

static inline u8 ata_irq_ack(struct ata_port *ap, unsigned int chk_drq)
{
	unsigned int bits = chk_drq ? ATA_BUSY | ATA_DRQ : ATA_BUSY;
	u8 host_stat, status;

	status = ata_busy_wait(&ap->ioaddr, bits, 1000);
	if (status & bits)
		DPRINTK("abnormal status 0x%X\n", status);

	/* get controller status; clear intr, err bits */
	host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	outb(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
	     ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	VPRINTK("irq ack: host_stat 0x%X, new host_stat 0x%X, drv_stat 0x%X\n",
		host_stat, inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS),
		status);

	return status;
}

/*
 * 2.5 compat.
 */

static inline int scsi_register_host(Scsi_Host_Template *t)
{
	return scsi_register_module(MODULE_SCSI_HA, t);
}

static inline void scsi_unregister_host(Scsi_Host_Template *t)
{
	scsi_unregister_module(MODULE_SCSI_HA, t);
}

typedef void irqreturn_t;

#define IRQ_RETVAL(x)

#define REPORT_LUNS		0xa0
#define READ_16			0x88
#define WRITE_16		0x8a
#define SERVICE_ACTION_IN	0x9e
/* values for service action in */
#define SAI_READ_CAPACITY_16	0x10

#define SAM_STAT_GOOD		0x00
#define SAM_STAT_CHECK_CONDITION 0x02

#endif /* __LINUX_ATA_H__ */
