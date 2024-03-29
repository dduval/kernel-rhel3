/*
 *  sx8.c: Driver for Promise SATA SX8 looks-like-I2O hardware
 *
 *  Copyright 2004 Red Hat, Inc.
 *
 *  Author/maintainer:  Jeff Garzik <jgarzik@pobox.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file "COPYING" in the main directory of this archive
 *  for more details.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/hdreg.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "sx8_compat.h"

MODULE_AUTHOR("Jeff Garzik");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Promise SATA SX8 block driver");

#if 0
#define CARM_DEBUG
#define CARM_VERBOSE_DEBUG
#else
#undef CARM_DEBUG
#undef CARM_VERBOSE_DEBUG
#endif
#undef CARM_NDEBUG

#define DRV_NAME "sx8"
#define DRV_VERSION "0.8-24.1"
#define PFX DRV_NAME ": "

#define NEXT_RESP(idx)	((idx + 1) % RMSG_Q_LEN)

/* 0xf is just arbitrary, non-zero noise; this is sorta like poisoning */
#define TAG_ENCODE(tag)	(((tag) << 16) | 0xf)
#define TAG_DECODE(tag)	(((tag) >> 16) & 0x1f)
#define TAG_VALID(tag)	((((tag) & 0xf) == 0xf) && (TAG_DECODE(tag) < 32))

/* note: prints function name for you */
#ifdef CARM_DEBUG
#define DPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#ifdef CARM_VERBOSE_DEBUG
#define VPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define VPRINTK(fmt, args...)
#endif	/* CARM_VERBOSE_DEBUG */
#else
#define DPRINTK(fmt, args...)
#define VPRINTK(fmt, args...)
#endif	/* CARM_DEBUG */

#ifdef CARM_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(unlikely(!(expr))) {                                   \
        printk(KERN_ERR "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif

/* defines only for the constants which don't work well as enums */
struct carm_host;

enum {
	/* adapter-wide limits */
	CARM_MAX_PORTS		= 8,
	CARM_SHM_SIZE		= (4096 << 7),
	CARM_PART_SHIFT		= 5,
	CARM_MINORS_PER_MAJOR	= (1 << CARM_PART_SHIFT),
	CARM_MAX_WAIT_Q		= CARM_MAX_PORTS + 1,

	/* command message queue limits */
	CARM_MAX_REQ		= 64,	       /* max command msgs per host */
	CARM_MAX_Q		= 1,		   /* one command at a time */
	CARM_MSG_LOW_WATER	= (CARM_MAX_REQ / 4),	     /* refill mark */

	/* S/G limits, host-wide and per-request */
	CARM_MAX_REQ_SG		= 32,	     /* max s/g entries per request */
	CARM_SG_BOUNDARY	= 0xffffUL,	    /* s/g segment boundary */
	CARM_MAX_HOST_SG	= 600,		/* max s/g entries per host */
	CARM_SG_LOW_WATER	= (CARM_MAX_HOST_SG / 4),   /* re-fill mark */

	/* hardware registers */
	CARM_IHQP		= 0x1c,
	CARM_INT_STAT		= 0x10, /* interrupt status */
	CARM_INT_MASK		= 0x14, /* interrupt mask */
	CARM_HMUC		= 0x18, /* host message unit control */
	RBUF_ADDR_LO		= 0x20, /* response msg DMA buf low 32 bits */
	RBUF_ADDR_HI		= 0x24, /* response msg DMA buf high 32 bits */
	RBUF_BYTE_SZ		= 0x28,
	CARM_RESP_IDX		= 0x2c,
	CARM_CMS0		= 0x30, /* command message size reg 0 */
	CARM_LMUC		= 0x48,
	CARM_HMPHA		= 0x6c,
	CARM_INITC		= 0xb5,

	/* bits in CARM_INT_{STAT,MASK} */
	INT_RESERVED		= 0xfffffff0,
	INT_WATCHDOG		= (1 << 3),	/* watchdog timer */
	INT_Q_OVERFLOW		= (1 << 2),	/* cmd msg q overflow */
	INT_Q_AVAILABLE		= (1 << 1),	/* cmd msg q has free space */
	INT_RESPONSE		= (1 << 0),	/* response msg available */
	INT_ACK_MASK		= INT_WATCHDOG | INT_Q_OVERFLOW,
	INT_DEF_MASK		= INT_RESERVED | INT_Q_OVERFLOW |
				  INT_RESPONSE,

	/* command messages, and related register bits */
	CARM_HAVE_RESP		= 0x01,
	CARM_MSG_READ		= 1,
	CARM_MSG_WRITE		= 2,
	CARM_MSG_VERIFY		= 3,
	CARM_MSG_GET_CAPACITY	= 4,
	CARM_MSG_FLUSH		= 5,
	CARM_MSG_IOCTL		= 6,
	CARM_MSG_ARRAY		= 8,
	CARM_MSG_MISC		= 9,
	CARM_CME		= (1 << 2),
	CARM_RME		= (1 << 1),
	CARM_WZBC		= (1 << 0),
	CARM_RMI		= (1 << 0),
	CARM_Q_FULL		= (1 << 3),
	CARM_MSG_SIZE		= 288,
	CARM_Q_LEN		= 48,

	/* CARM_MSG_IOCTL messages */
	CARM_IOC_SCAN_CHAN	= 5,	/* scan channels for devices */
	CARM_IOC_GET_TCQ	= 13,	/* get tcq/ncq depth */
	CARM_IOC_SET_TCQ	= 14,	/* set tcq/ncq depth */

	IOC_SCAN_CHAN_NODEV	= 0x1f,
	IOC_SCAN_CHAN_OFFSET	= 0x40,

	/* CARM_MSG_ARRAY messages */
	CARM_ARRAY_INFO		= 0,

	ARRAY_NO_EXIST		= (1 << 31),

	/* response messages */
	RMSG_SZ			= 8,	/* sizeof(struct carm_response) */
	RMSG_Q_LEN		= 48,	/* resp. msg list length */
	RMSG_OK			= 1,	/* bit indicating msg was successful */
					/* length of entire resp. msg buffer */
	RBUF_LEN		= RMSG_SZ * RMSG_Q_LEN,

	PDC_SHM_SIZE		= (4096 << 7), /* length of entire h/w buffer */

	/* CARM_MSG_MISC messages */
	MISC_GET_FW_VER		= 2,
	MISC_ALLOC_MEM		= 3,
	MISC_SET_TIME		= 5,

	/* MISC_GET_FW_VER feature bits */
	FW_VER_4PORT		= (1 << 2), /* 1=4 ports, 0=8 ports */
	FW_VER_NON_RAID		= (1 << 1), /* 1=non-RAID firmware, 0=RAID */
	FW_VER_ZCR		= (1 << 0), /* zero channel RAID (whatever that is) */

	/* carm_host flags */
	FL_NON_RAID		= FW_VER_NON_RAID,
	FL_4PORT		= FW_VER_4PORT,
	FL_FW_VER_MASK		= (FW_VER_NON_RAID | FW_VER_4PORT),
	FL_DAC			= (1 << 16),
	FL_DYN_MAJOR		= (1 << 17),
};

enum carm_magic_numbers {
	CARM_MAGIC_HOST		= 0xdeadbeefUL,
	CARM_MAGIC_PORT		= 0xbedac0edUL,
};

enum scatter_gather_types {
	SGT_32BIT		= 0,
	SGT_64BIT		= 1,
};

enum host_states {
	HST_INVALID,		/* invalid state; never used */
	HST_ALLOC_BUF,		/* setting up master SHM area */
	HST_ERROR,		/* we never leave here */
	HST_PORT_SCAN,		/* start dev scan */
	HST_DEV_SCAN_START,	/* start per-device probe */
	HST_DEV_SCAN,		/* continue per-device probe */
	HST_DEV_ACTIVATE,	/* activate devices we found */
	HST_PROBE_FINISHED,	/* probe is complete */
	HST_PROBE_START,	/* initiate probe */
	HST_SYNC_TIME,		/* tell firmware what time it is */
	HST_GET_FW_VER,		/* get firmware version, adapter port cnt */
};

#ifdef CARM_DEBUG
static const char *state_name[] = {
	"HST_INVALID",
	"HST_ALLOC_BUF",
	"HST_ERROR",
	"HST_PORT_SCAN",
	"HST_DEV_SCAN_START",
	"HST_DEV_SCAN",
	"HST_DEV_ACTIVATE",
	"HST_PROBE_FINISHED",
	"HST_PROBE_START",
	"HST_SYNC_TIME",
	"HST_GET_FW_VER",
};
#endif

struct carm_port {
	unsigned long			magic;
	unsigned int			port_no;
	unsigned int			n_queued;
	struct carm_host		*host;
	struct tasklet_struct		tasklet;
	request_queue_t			q;

	/* attached device characteristics */
	u64				capacity;
	char				name[41];
	u16				dev_geom_head;
	u16				dev_geom_sect;
	u16				dev_geom_cyl;
};

struct carm_request {
	unsigned int			tag;
	int				n_elem;
	unsigned int			msg_type;
	unsigned int			msg_subtype;
	unsigned int			msg_bucket;
	struct request			*rq;
	struct carm_port		*port;
	struct request			special_rq;
	struct scatterlist		sg[CARM_MAX_REQ_SG];
};

struct carm_host {
	unsigned long			magic;
	unsigned long			flags;
	void				*mmio;
	void				*shm;
	dma_addr_t			shm_dma;

	int				major;
	int				id;
	char				name[32];

	struct pci_dev			*pdev;
	unsigned int			state;
	u32				fw_ver;

	request_queue_t			oob_q;
	unsigned int			n_oob;
	struct tasklet_struct		oob_tasklet;

	unsigned int			hw_sg_used;

	unsigned int			resp_idx;

	unsigned int			wait_q_prod;
	unsigned int			wait_q_cons;
	request_queue_t			*wait_q[CARM_MAX_WAIT_Q];

	unsigned int			n_msgs;
	u64				msg_alloc;
	struct carm_request		req[CARM_MAX_REQ];
	void				*msg_base;
	dma_addr_t			msg_dma;

	int				cur_scan_dev;
	unsigned long			dev_active;
	unsigned long			dev_present;
	struct carm_port		port[CARM_MAX_PORTS];

	struct tq_struct		fsm_task;

	struct semaphore		probe_sem;

	struct gendisk			gendisk;
	struct hd_struct		gendisk_hd[256];
	int				blk_sizes[256];
	int				blk_block_sizes[256];
	int				blk_sect_sizes[256];

	struct list_head		host_list_node;
};

struct carm_response {
	u32 ret_handle;
	u32 status;
}  __attribute__((packed));

struct carm_msg_sg {
	u32 start;
	u32 len;
}  __attribute__((packed));

struct carm_msg_rw {
	u8 type;
	u8 id;
	u8 sg_count;
	u8 sg_type;
	u32 handle;
	u32 lba;
	u16 lba_count;
	u16 lba_high;
	struct carm_msg_sg sg[32];
}  __attribute__((packed));

struct carm_msg_allocbuf {
	u8 type;
	u8 subtype;
	u8 n_sg;
	u8 sg_type;
	u32 handle;
	u32 addr;
	u32 len;
	u32 evt_pool;
	u32 n_evt;
	u32 rbuf_pool;
	u32 n_rbuf;
	u32 msg_pool;
	u32 n_msg;
	struct carm_msg_sg sg[8];
}  __attribute__((packed));

struct carm_msg_ioctl {
	u8 type;
	u8 subtype;
	u8 array_id;
	u8 reserved1;
	u32 handle;
	u32 data_addr;
	u32 reserved2;
}  __attribute__((packed));

struct carm_msg_sync_time {
	u8 type;
	u8 subtype;
	u16 reserved1;
	u32 handle;
	u32 reserved2;
	u32 timestamp;
}  __attribute__((packed));

struct carm_msg_get_fw_ver {
	u8 type;
	u8 subtype;
	u16 reserved1;
	u32 handle;
	u32 data_addr;
	u32 reserved2;
}  __attribute__((packed));

struct carm_fw_ver {
	u32 version;
	u8 features;
	u8 reserved1;
	u16 reserved2;
}  __attribute__((packed));

struct carm_array_info {
	u32 size;

	u16 size_hi;
	u16 stripe_size;

	u32 mode;

	u16 stripe_blk_sz;
	u16 reserved1;

	u16 cyl;
	u16 head;

	u16 sect;
	u8 array_id;
	u8 reserved2;

	char name[40];

	u32 array_status;

	/* device list continues beyond this point? */
}  __attribute__((packed));

static int carm_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
static void carm_remove_one (struct pci_dev *pdev);
static int carm_bdev_ioctl(struct inode *ino, struct file *fil,
			   unsigned int cmd, unsigned long arg);
static request_queue_t *carm_find_queue(kdev_t device);
static int carm_revalidate_disk(kdev_t dev);
static void carm_unregister_proc(void);
static void carm_register_proc(void);

static struct pci_device_id carm_pci_tbl[] = {
	{ PCI_VENDOR_ID_PROMISE, 0x8000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ PCI_VENDOR_ID_PROMISE, 0x8002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, },
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, carm_pci_tbl);

static struct pci_driver carm_driver = {
	.name		= DRV_NAME,
	.id_table	= carm_pci_tbl,
	.probe		= carm_init_one,
	.remove		= carm_remove_one,
};

static struct block_device_operations carm_bd_ops = {
	.owner		= THIS_MODULE,
	.ioctl		= carm_bdev_ioctl,
};

static unsigned int carm_host_id;
static unsigned long carm_major_alloc;
static LIST_HEAD(carm_host_list);


static struct carm_host *carm_from_dev(kdev_t dev, struct carm_port **port_out)
{
	struct carm_host *host;
	struct carm_port *port;
	request_queue_t *q;

	q = carm_find_queue(dev);
	if (!q || !q->queuedata) {
		printk(KERN_ERR PFX "queue not found for major %d minor %d\n",
			MAJOR(dev), MINOR(dev));
		return NULL;
	}

	port = q->queuedata;
	if (unlikely(port->magic != CARM_MAGIC_PORT)) {
		printk(KERN_ERR PFX "bad port magic number for major %d minor %d\n",
			MAJOR(dev), MINOR(dev));
		return NULL;
	}

	host = port->host;
	if (unlikely(host->magic != CARM_MAGIC_HOST)) {
		printk(KERN_ERR PFX "bad host magic number for major %d minor %d\n",
			MAJOR(dev), MINOR(dev));
		return NULL;
	}

	if (port_out)
		*port_out = port;
	return host;
}

static int carm_bdev_ioctl(struct inode *ino, struct file *fil,
			   unsigned int cmd, unsigned long arg)
{
	void *usermem = (void *) arg;
	struct carm_port *port = NULL;
	struct carm_host *host;

	host = carm_from_dev(ino->i_rdev, &port);
	if (!host)
		return -EINVAL;

	switch (cmd) {
	case HDIO_GETGEO: {
		struct hd_geometry geom;

		if (!usermem)
			return -EINVAL;

		if (port->dev_geom_cyl) {
			geom.heads = port->dev_geom_head;
			geom.sectors = port->dev_geom_sect;
			geom.cylinders = port->dev_geom_cyl;
		} else {
			u32 tmp = ((u32)port->capacity) / (0xff * 0x3f);
			geom.heads = 0xff;
			geom.sectors = 0x3f;
			if (tmp > 65536)
				geom.cylinders = 0xffff;
			else
				geom.cylinders = tmp;
		}
		geom.start = host->gendisk_hd[MINOR(ino->i_rdev)].start_sect;

		if (copy_to_user(usermem, &geom, sizeof(geom)))
			return -EFAULT;
		return 0;
	}

	case HDIO_GETGEO_BIG: {
		struct hd_big_geometry geom;

		if (!usermem)
			return -EINVAL;

		if (port->dev_geom_cyl) {
			geom.heads = port->dev_geom_head;
			geom.sectors = port->dev_geom_sect;
			geom.cylinders = port->dev_geom_cyl;
		} else {
			geom.heads = 0xff;
			geom.sectors = 0x3f;
			geom.cylinders = ((u32)port->capacity) / (0xff * 0x3f);
		}
		geom.start = host->gendisk_hd[MINOR(ino->i_rdev)].start_sect;

		if (copy_to_user(usermem, &geom, sizeof(geom)))
			return -EFAULT;
		return 0;
	}

	case BLKRRPART:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return carm_revalidate_disk(ino->i_rdev);

	case BLKGETSIZE:
	case BLKGETSIZE64:
	case BLKFLSBUF:
	case BLKBSZSET:
	case BLKBSZGET:
	case BLKROSET:
	case BLKROGET:
	case BLKRASET:
	case BLKRAGET:
	case BLKPG:
	case BLKELVGET:
	case BLKELVSET:
		return blk_ioctl(ino->i_rdev, cmd, arg);

	default:
		break;
	}

	return -EOPNOTSUPP;
}

static inline unsigned long msecs_to_jiffies(unsigned long msecs)
{
	return ((HZ * msecs + 999) / 1000);
}

static void msleep(unsigned long msecs)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

static const u32 msg_sizes[] = { 32, 64, 128, CARM_MSG_SIZE };

static inline int carm_lookup_bucket(u32 msg_size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(msg_sizes); i++)
		if (msg_size <= msg_sizes[i])
			return i;
	
	return -ENOENT;
}

static void carm_init_buckets(void *mmio)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(msg_sizes); i++)
		writel(msg_sizes[i], mmio + CARM_CMS0 + (4 * i));
}

static inline void *carm_ref_msg(struct carm_host *host,
				 unsigned int msg_idx)
{
	return host->msg_base + (msg_idx * CARM_MSG_SIZE);
}

static inline dma_addr_t carm_ref_msg_dma(struct carm_host *host,
					  unsigned int msg_idx)
{
	return host->msg_dma + (msg_idx * CARM_MSG_SIZE);
}

static int carm_send_msg(struct carm_host *host,
			 struct carm_request *crq)
{
	void *mmio = host->mmio;
	u32 msg = (u32) carm_ref_msg_dma(host, crq->tag);
	u32 cm_bucket = crq->msg_bucket;
	u32 tmp;
	int rc = 0;

	VPRINTK("ENTER\n");

	tmp = readl(mmio + CARM_HMUC);
	if (tmp & CARM_Q_FULL) {
#if 0
		tmp = readl(mmio + CARM_INT_MASK);
		tmp |= INT_Q_AVAILABLE;
		writel(tmp, mmio + CARM_INT_MASK);
		readl(mmio + CARM_INT_MASK);	/* flush */
#endif
		DPRINTK("host msg queue full\n");
		rc = -EBUSY;
	} else {
		writel(msg | (cm_bucket << 1), mmio + CARM_IHQP);
		readl(mmio + CARM_IHQP);	/* flush */
	}

	return rc;
}

static struct carm_request *carm_get_request(struct carm_host *host)
{
	unsigned int i;

	/* obey global hardware limit on S/G entries */
	if (host->hw_sg_used >= (CARM_MAX_HOST_SG - CARM_MAX_REQ_SG))
		return NULL;

	for (i = 0; i < CARM_MAX_Q; i++)
		if ((host->msg_alloc & (1ULL << i)) == 0) {
			struct carm_request *crq = &host->req[i];
			crq->port = NULL;
			crq->n_elem = 0;

			host->msg_alloc |= (1ULL << i);
			host->n_msgs++;

			assert(host->n_msgs <= CARM_MAX_REQ);
			return crq;
		}
	
	DPRINTK("no request available, returning NULL\n");
	return NULL;
}

static int carm_put_request(struct carm_host *host, struct carm_request *crq)
{
	assert(crq->tag < CARM_MAX_Q);

	if (unlikely((host->msg_alloc & (1ULL << crq->tag)) == 0))
		return -EINVAL; /* tried to clear a tag that was not active */

	assert(host->hw_sg_used >= crq->n_elem);

	host->msg_alloc &= ~(1ULL << crq->tag);
	host->hw_sg_used -= crq->n_elem;
	host->n_msgs--;

	return 0;
}

static void carm_insert_special(request_queue_t *q, struct request *rq,
				void *data, int at_head)
{
	unsigned long flags;

	rq->cmd = SPECIAL;
	rq->special = data;
	rq->q = NULL;
	rq->nr_segments = 0;
	rq->elevator_sequence = 0;

	spin_lock_irqsave(&io_request_lock, flags);
	if (at_head)
		list_add(&rq->queue, &q->queue_head);
	else
		list_add_tail(&rq->queue, &q->queue_head);
	q->request_fn(q);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

static struct carm_request *carm_get_special(struct carm_host *host)
{
	unsigned long flags;
	struct carm_request *crq = NULL;
	int tries = 5000;

	while (tries-- > 0) {
		spin_lock_irqsave(&io_request_lock, flags);
		crq = carm_get_request(host);
		spin_unlock_irqrestore(&io_request_lock, flags);

		if (crq)
			break;
		msleep(10);
	}

	if (!crq)
		return NULL;

	crq->rq = &crq->special_rq;
	return crq;
}

static int carm_array_info (struct carm_host *host, unsigned int array_idx)
{
	struct carm_msg_ioctl *ioc;
	unsigned int idx;
	u32 msg_data;
	dma_addr_t msg_dma;
	struct carm_request *crq;
	int rc;
	unsigned long flags;

	crq = carm_get_special(host);
	if (!crq) {
		rc = -ENOMEM;
		goto err_out;
	}

	idx = crq->tag;

	ioc = carm_ref_msg(host, idx);
	msg_dma = carm_ref_msg_dma(host, idx);
	msg_data = (u32) (msg_dma + sizeof(struct carm_array_info));

	crq->msg_type = CARM_MSG_ARRAY;
	crq->msg_subtype = CARM_ARRAY_INFO;
	rc = carm_lookup_bucket(sizeof(struct carm_msg_ioctl) +
				sizeof(struct carm_array_info));
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_ARRAY;
	ioc->subtype	= CARM_ARRAY_INFO;
	ioc->array_id	= (u8) array_idx;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	assert(host->state == HST_DEV_SCAN_START ||
	       host->state == HST_DEV_SCAN);

	DPRINTK("blk_insert_request, tag == %u\n", idx);
	carm_insert_special(&host->oob_q, crq->rq, crq, 1);

	return 0;

err_out:
	spin_lock_irqsave(&io_request_lock, flags);
	host->state = HST_ERROR;
	spin_unlock_irqrestore(&io_request_lock, flags);
	return rc;
}

typedef unsigned int (*carm_sspc_t)(struct carm_host *, unsigned int, void *);

static int carm_send_special (struct carm_host *host, carm_sspc_t func)
{
	struct carm_request *crq;
	struct carm_msg_ioctl *ioc;
	void *mem;
	unsigned int idx, msg_size;
	int rc;

	crq = carm_get_special(host);
	if (!crq)
		return -ENOMEM;

	idx = crq->tag;

	mem = carm_ref_msg(host, idx);

	msg_size = func(host, idx, mem);

	ioc = mem;
	crq->msg_type = ioc->type;
	crq->msg_subtype = ioc->subtype;
	rc = carm_lookup_bucket(msg_size);
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	DPRINTK("blk_insert_request, tag == %u\n", idx);
	carm_insert_special(&host->oob_q, crq->rq, crq, 1);

	return 0;
}

static unsigned int carm_fill_sync_time(struct carm_host *host,
					unsigned int idx, void *mem)
{
	struct timeval tv;
	struct carm_msg_sync_time *st = mem;

	do_gettimeofday(&tv);

	memset(st, 0, sizeof(*st));
	st->type	= CARM_MSG_MISC;
	st->subtype	= MISC_SET_TIME;
	st->handle	= cpu_to_le32(TAG_ENCODE(idx));
	st->timestamp	= cpu_to_le32(tv.tv_sec);

	return sizeof(struct carm_msg_sync_time);
}

static unsigned int carm_fill_alloc_buf(struct carm_host *host,
					unsigned int idx, void *mem)
{
	struct carm_msg_allocbuf *ab = mem;

	memset(ab, 0, sizeof(*ab));
	ab->type	= CARM_MSG_MISC;
	ab->subtype	= MISC_ALLOC_MEM;
	ab->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ab->n_sg	= 1;
	ab->sg_type	= SGT_32BIT;
	ab->addr	= cpu_to_le32(host->shm_dma + (PDC_SHM_SIZE >> 1));
	ab->len		= cpu_to_le32(PDC_SHM_SIZE >> 1);
	ab->evt_pool	= cpu_to_le32(host->shm_dma + (16 * 1024));
	ab->n_evt	= cpu_to_le32(1024);
	ab->rbuf_pool	= cpu_to_le32(host->shm_dma);
	ab->n_rbuf	= cpu_to_le32(RMSG_Q_LEN);
	ab->msg_pool	= cpu_to_le32(host->shm_dma + RBUF_LEN);
	ab->n_msg	= cpu_to_le32(CARM_Q_LEN);
	ab->sg[0].start	= cpu_to_le32(host->shm_dma + (PDC_SHM_SIZE >> 1));
	ab->sg[0].len	= cpu_to_le32(65536);

	return sizeof(struct carm_msg_allocbuf);
}

static unsigned int carm_fill_scan_channels(struct carm_host *host,
					    unsigned int idx, void *mem)
{
	struct carm_msg_ioctl *ioc = mem;
	u32 msg_data = (u32) (carm_ref_msg_dma(host, idx) +
			      IOC_SCAN_CHAN_OFFSET);

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_IOCTL;
	ioc->subtype	= CARM_IOC_SCAN_CHAN;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	/* fill output data area with "no device" default values */
	mem += IOC_SCAN_CHAN_OFFSET;
	memset(mem, IOC_SCAN_CHAN_NODEV, CARM_MAX_PORTS);

	return IOC_SCAN_CHAN_OFFSET + CARM_MAX_PORTS;
}

static unsigned int carm_fill_get_fw_ver(struct carm_host *host,
					 unsigned int idx, void *mem)
{
	struct carm_msg_get_fw_ver *ioc = mem;
	u32 msg_data = (u32) (carm_ref_msg_dma(host, idx) + sizeof(*ioc));

	memset(ioc, 0, sizeof(*ioc));
	ioc->type	= CARM_MSG_MISC;
	ioc->subtype	= MISC_GET_FW_VER;
	ioc->handle	= cpu_to_le32(TAG_ENCODE(idx));
	ioc->data_addr	= cpu_to_le32(msg_data);

	return sizeof(struct carm_msg_get_fw_ver) +
	       sizeof(struct carm_fw_ver);
}

static void carm_activate_disk(struct carm_host *host,
			       struct carm_port *port)
{
	int minor_start = port->port_no << CARM_PART_SHIFT;
	int start, end, i;

	host->gendisk_hd[minor_start].nr_sects = port->capacity;
	host->blk_sizes[minor_start] = port->capacity;

	start = minor_start;
	end = minor_start + CARM_MINORS_PER_MAJOR;
	for (i = start; i < end; i++) {
		invalidate_device(MKDEV(host->major, i), 1);
		host->gendisk.part[i].start_sect = 0;
		host->gendisk.part[i].nr_sects = 0;
		host->blk_block_sizes[i] = 512;
		host->blk_sect_sizes[i] = 512;
	}

	grok_partitions(&host->gendisk, port->port_no, 
			CARM_MINORS_PER_MAJOR,
			port->capacity);
}

static int carm_revalidate_disk(kdev_t dev)
{
	struct carm_host *host;
	struct carm_port *port = NULL;

	host = carm_from_dev(dev, &port);
	if (!host)
		return -EINVAL;

	carm_activate_disk(host, port);

	return 0;
}

static inline void complete_buffers(struct buffer_head *bh, int status)
{
	struct buffer_head *xbh;
	
	while (bh) {
		xbh = bh->b_reqnext; 
		bh->b_reqnext = NULL; 
		blk_finished_io(bh->b_size >> 9);
		bh->b_end_io(bh, status);
		bh = xbh;
	}
} 

static inline void carm_end_request_queued(struct carm_host *host,
					   struct carm_request *crq,
					   int uptodate)
{
	struct request *req = crq->rq;
	int rc;

	complete_buffers(req->bh, uptodate);
	end_that_request_last(req);

	rc = carm_put_request(host, crq);
	assert(rc == 0);
}

static inline void carm_push_q (struct carm_host *host, request_queue_t *q)
{
	unsigned int idx = host->wait_q_prod % CARM_MAX_WAIT_Q;

	VPRINTK("STOPPED QUEUE %p\n", q);

	host->wait_q[idx] = q;
	host->wait_q_prod++;
	BUG_ON(host->wait_q_prod == host->wait_q_cons); /* overrun */
}

static inline request_queue_t *carm_pop_q(struct carm_host *host)
{
	unsigned int idx;

	if (host->wait_q_prod == host->wait_q_cons)
		return NULL;

	idx = host->wait_q_cons % CARM_MAX_WAIT_Q;
	host->wait_q_cons++;

	return host->wait_q[idx];
}

static inline void carm_round_robin(struct carm_host *host)
{
	request_queue_t *q = carm_pop_q(host);
	if (q) {
		struct tasklet_struct *tasklet;
		if (q == &host->oob_q)
			tasklet = &host->oob_tasklet;
		else {
			struct carm_port *port = q->queuedata;
			tasklet = &port->tasklet;
		}
		tasklet_schedule(tasklet);
		VPRINTK("STARTED QUEUE %p\n", q);
	}
}

static inline void carm_end_rq(struct carm_host *host, struct carm_request *crq,
			int is_ok)
{
	carm_end_request_queued(host, crq, is_ok);
	if (CARM_MAX_Q == 1)
		carm_round_robin(host);
	else if ((host->n_msgs <= CARM_MSG_LOW_WATER) &&
		 (host->hw_sg_used <= CARM_SG_LOW_WATER)) {
		carm_round_robin(host);
	}
}

static inline int carm_new_segment(request_queue_t *q, struct request *rq)
{
        if (rq->nr_segments < CARM_MAX_REQ_SG) {
                rq->nr_segments++;
                return 1;
        }
        return 0;
}

static int carm_back_merge_fn(request_queue_t *q, struct request *rq,
                             struct buffer_head *bh, int max_segments)
{
	if (blk_seg_merge_ok(rq->bhtail, bh))	
                return 1;
        return carm_new_segment(q, rq);
}

static int carm_front_merge_fn(request_queue_t *q, struct request *rq,
                             struct buffer_head *bh, int max_segments)
{
	if (blk_seg_merge_ok(bh, rq->bh))
                return 1;
        return carm_new_segment(q, rq);
}

static int carm_merge_requests_fn(request_queue_t *q, struct request *rq,
                                 struct request *nxt, int max_segments)
{
        int total_segments = rq->nr_segments + nxt->nr_segments;

	if (blk_seg_merge_ok(rq->bhtail, nxt->bh))
                total_segments--;

        if (total_segments > CARM_MAX_REQ_SG)
                return 0;

        rq->nr_segments = total_segments;
        return 1;
}

static void carm_oob_rq_fn(request_queue_t *q)
{
	struct carm_host *host = q->queuedata;

	tasklet_schedule(&host->oob_tasklet);
}

static void carm_rq_fn(request_queue_t *q)
{
	struct carm_port *port = q->queuedata;

	tasklet_schedule(&port->tasklet);
}

static void carm_oob_tasklet(unsigned long _data)
{
	struct carm_host *host = (void *) _data;
	request_queue_t *q = &host->oob_q;
	struct carm_request *crq;
	struct request *rq;
	int rc, have_work = 1;
	struct list_head *queue_head = &q->queue_head;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	if (q->plugged || list_empty(queue_head))
		have_work = 0;

	if (!have_work)
		goto out;

	while (1) {
		DPRINTK("get req\n");
		if (list_empty(queue_head))
			break;

		rq = blkdev_entry_next_request(queue_head);

		crq = rq->special;
		assert(crq != NULL);
		assert(crq->rq == rq);

		crq->n_elem = 0;

		DPRINTK("send req\n");
		rc = carm_send_msg(host, crq);
		if (rc) {
			carm_push_q(host, q);
			break;		/* call us again later, eventually */
		} else
			blkdev_dequeue_request(rq);
	}

out:
	spin_unlock_irqrestore(&io_request_lock, flags);
}

static int blk_rq_map_sg(request_queue_t *q, struct request *rq,
			 struct scatterlist *sg)
{
	int n_elem = 0;
	struct buffer_head *bh = rq->bh;
	u64 last_phys = ~0ULL;

	while (bh) {
		if (bh_phys(bh) == last_phys) {
			sg[n_elem - 1].length += bh->b_size;
			last_phys += bh->b_size;
		} else {
			if (unlikely(n_elem == CARM_MAX_REQ_SG))
				BUG();
			sg[n_elem].page = bh->b_page;
			sg[n_elem].length = bh->b_size;
			sg[n_elem].offset = bh_offset(bh);
			last_phys = bh_phys(bh) + bh->b_size;
			n_elem++;
		}

		bh = bh->b_reqnext;
	}

	return n_elem;
}

static void carm_rw_tasklet(unsigned long _data)
{
	struct carm_port *port = (void *) _data;
	struct carm_host *host = port->host;
	request_queue_t *q = &port->q;
	struct carm_msg_rw *msg;
	struct carm_request *crq;
	struct request *rq;
	struct scatterlist *sg;
	int writing = 0, pci_dir, i, n_elem, rc, have_work = 1;
	u32 tmp;
	unsigned int msg_size;
	unsigned long flags;
	struct list_head *queue_head = &q->queue_head;
	unsigned long start_sector;

	spin_lock_irqsave(&io_request_lock, flags);
	if (q->plugged || list_empty(queue_head))
		have_work = 0;

	if (!have_work)
		goto out;

queue_one_request:
	VPRINTK("get req\n");
	if (list_empty(queue_head))
		goto out;

	rq = blkdev_entry_next_request(queue_head);

	crq = carm_get_request(host);
	if (!crq) {
		carm_push_q(host, q);
		goto out;	/* call us again later, eventually */
	}
	crq->rq = rq;

	if (rq_data_dir(rq) == WRITE) {
		writing = 1;
		pci_dir = PCI_DMA_TODEVICE;
	} else {
		pci_dir = PCI_DMA_FROMDEVICE;
	}

	/* get scatterlist from block layer */
	sg = &crq->sg[0];
	n_elem = blk_rq_map_sg(q, rq, sg);
	if (n_elem <= 0) {
		carm_end_rq(host, crq, 0);
		goto out;	/* request with no s/g entries? */
	}

	/* map scatterlist to PCI bus addresses */
	n_elem = pci_map_sg(host->pdev, sg, n_elem, pci_dir);
	if (n_elem <= 0) {
		carm_end_rq(host, crq, 0);
		goto out;	/* request with no s/g entries? */
	}
	crq->n_elem = n_elem;
	crq->port = port;
	host->hw_sg_used += n_elem;

	/*
	 * build read/write message
	 */

	VPRINTK("build msg\n");
	msg = (struct carm_msg_rw *) carm_ref_msg(host, crq->tag);

	if (writing) {
		msg->type = CARM_MSG_WRITE;
		crq->msg_type = CARM_MSG_WRITE;
	} else {
		msg->type = CARM_MSG_READ;
		crq->msg_type = CARM_MSG_READ;
	}

	start_sector = rq->sector;
	start_sector += host->gendisk_hd[MINOR(rq->rq_dev)].start_sect;

	msg->id		= port->port_no;
	msg->sg_count	= n_elem;
	msg->sg_type	= SGT_32BIT;
	msg->handle	= cpu_to_le32(TAG_ENCODE(crq->tag));
	msg->lba	= cpu_to_le32(start_sector & 0xffffffff);
	tmp		= (start_sector >> 16) >> 16;
	msg->lba_high	= cpu_to_le16( (u16) tmp );
	msg->lba_count	= cpu_to_le16(rq->nr_sectors);

	msg_size = sizeof(struct carm_msg_rw) - sizeof(msg->sg);
	for (i = 0; i < n_elem; i++) {
		struct carm_msg_sg *carm_sg = &msg->sg[i];
		carm_sg->start = cpu_to_le32(sg_dma_address(&crq->sg[i]));
		carm_sg->len = cpu_to_le32(sg_dma_len(&crq->sg[i]));
		msg_size += sizeof(struct carm_msg_sg);
	}

	rc = carm_lookup_bucket(msg_size);
	BUG_ON(rc < 0);
	crq->msg_bucket = (u32) rc;

	/*
	 * queue read/write message to hardware
	 */

	VPRINTK("send msg, tag == %u\n", crq->tag);
	rc = carm_send_msg(host, crq);
	if (rc) {
		carm_put_request(host, crq);
		carm_push_q(host, q);
		goto out;	/* call us again later, eventually */
	} else
		blkdev_dequeue_request(rq);

	goto queue_one_request;

out:
	spin_unlock_irqrestore(&io_request_lock, flags);
}

static void carm_handle_array_info(struct carm_host *host,
				   struct carm_request *crq, u8 *mem,
				   int is_ok)
{
	struct carm_port *port;
	u8 *msg_data = mem + sizeof(struct carm_array_info);
	struct carm_array_info *desc = (struct carm_array_info *) msg_data;
	u64 lo, hi;
	int cur_port;
	size_t slen;

	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, is_ok);

	if (!is_ok)
		goto out;
	if (le32_to_cpu(desc->array_status) & ARRAY_NO_EXIST)
		goto out;

	cur_port = host->cur_scan_dev;

	/* should never occur */
	if ((cur_port < 0) || (cur_port >= CARM_MAX_PORTS)) {
		printk(KERN_ERR PFX "BUG: cur_scan_dev==%d, array_id==%d\n",
		       cur_port, (int) desc->array_id);
		goto out;
	}

	port = &host->port[cur_port];

	lo = (u64) le32_to_cpu(desc->size);
	hi = (u64) le32_to_cpu(desc->size_hi);

	port->capacity = lo | (hi << 32);
	port->dev_geom_head = le16_to_cpu(desc->head);
	port->dev_geom_sect = le16_to_cpu(desc->sect);
	port->dev_geom_cyl = le16_to_cpu(desc->cyl);

	host->dev_active |= (1 << cur_port);

	strncpy(port->name, desc->name, sizeof(port->name));
	port->name[sizeof(port->name) - 1] = 0;
	slen = strlen(port->name);
	while (slen && (port->name[slen - 1] == ' ')) {
		port->name[slen - 1] = 0;
		slen--;
	}

	printk(KERN_INFO DRV_NAME "(%s): port %u device %Lu sectors\n",
	       pci_name(host->pdev), port->port_no, port->capacity);
	printk(KERN_INFO DRV_NAME "(%s): port %u device \"%s\"\n",
	       pci_name(host->pdev), port->port_no, port->name);

out:
	assert(host->state == HST_DEV_SCAN);
	schedule_task(&host->fsm_task);
}

static void carm_handle_scan_chan(struct carm_host *host,
				  struct carm_request *crq, u8 *mem,
				  int is_ok)
{
	u8 *msg_data = mem + IOC_SCAN_CHAN_OFFSET;
	unsigned int i, dev_count = 0;
	int new_state = HST_DEV_SCAN_START;

	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, is_ok);

	if (!is_ok) {
		new_state = HST_ERROR;
		goto out;
	}

	/* TODO: scan and support non-disk devices */
	for (i = 0; i < 8; i++)
		if (msg_data[i] == 0) { /* direct-access device (disk) */
			host->dev_present |= (1 << i);
			dev_count++;
		}

	printk(KERN_INFO DRV_NAME "(%s): found %u interesting devices\n",
	       pci_name(host->pdev), dev_count);

out:
	assert(host->state == HST_PORT_SCAN);
	host->state = new_state;
	schedule_task(&host->fsm_task);
}

static void carm_handle_generic(struct carm_host *host,
				struct carm_request *crq, int is_ok,
				int cur_state, int next_state)
{
	DPRINTK("ENTER\n");

	carm_end_rq(host, crq, is_ok);

	assert(host->state == cur_state);
	if (is_ok)
		host->state = next_state;
	else
		host->state = HST_ERROR;
	schedule_task(&host->fsm_task);
}

static inline void carm_handle_rw(struct carm_host *host,
				  struct carm_request *crq, int is_ok)
{
	int pci_dir;

	VPRINTK("ENTER\n");

	if (rq_data_dir(crq->rq) == WRITE)
		pci_dir = PCI_DMA_TODEVICE;
	else
		pci_dir = PCI_DMA_FROMDEVICE;

	pci_unmap_sg(host->pdev, &crq->sg[0], crq->n_elem, pci_dir);

	carm_end_rq(host, crq, is_ok);
}

static inline void carm_handle_resp(struct carm_host *host,
				    u32 ret_handle_le, u32 status)
{
	u32 handle = le32_to_cpu(ret_handle_le);
	unsigned int msg_idx;
	struct carm_request *crq;
	int is_ok = (status == RMSG_OK);
	u8 *mem;

	VPRINTK("ENTER, handle == 0x%x\n", handle);

	if (unlikely(!TAG_VALID(handle))) {
		printk(KERN_ERR DRV_NAME "(%s): BUG: invalid tag 0x%x\n",
		       pci_name(host->pdev), handle);
		return;
	}

	msg_idx = TAG_DECODE(handle);
	VPRINTK("tag == %u\n", msg_idx);

	crq = &host->req[msg_idx];

	/* fast path */
	if (likely(crq->msg_type == CARM_MSG_READ ||
		   crq->msg_type == CARM_MSG_WRITE)) {
		carm_handle_rw(host, crq, is_ok);
		return;
	}

	mem = carm_ref_msg(host, msg_idx);

	switch (crq->msg_type) {
	case CARM_MSG_IOCTL: {
		switch (crq->msg_subtype) {
		case CARM_IOC_SCAN_CHAN:
			carm_handle_scan_chan(host, crq, mem, is_ok);
			break;
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	case CARM_MSG_MISC: {
		switch (crq->msg_subtype) {
		case MISC_ALLOC_MEM:
			carm_handle_generic(host, crq, is_ok,
					    HST_ALLOC_BUF, HST_SYNC_TIME);
			break;
		case MISC_SET_TIME:
			carm_handle_generic(host, crq, is_ok,
					    HST_SYNC_TIME, HST_GET_FW_VER);
			break;
		case MISC_GET_FW_VER: {
			struct carm_fw_ver *ver = (struct carm_fw_ver *)
				mem + sizeof(struct carm_msg_get_fw_ver);
			if (is_ok) {
				host->fw_ver = le32_to_cpu(ver->version);
				host->flags |= (ver->features & FL_FW_VER_MASK);
			}
			carm_handle_generic(host, crq, is_ok,
					    HST_GET_FW_VER, HST_PORT_SCAN);
			break;
		}
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	case CARM_MSG_ARRAY: {
		switch (crq->msg_subtype) {
		case CARM_ARRAY_INFO:
			carm_handle_array_info(host, crq, mem, is_ok);
			break;
		default:
			/* unknown / invalid response */
			goto err_out;
		}
		break;
	}

	default:
		/* unknown / invalid response */
		goto err_out;
	}

	return;

err_out:
	printk(KERN_WARNING DRV_NAME "(%s): BUG: unhandled message type %d/%d\n",
	       pci_name(host->pdev), crq->msg_type, crq->msg_subtype);
	carm_end_rq(host, crq, 0);
}

static inline void carm_handle_responses(struct carm_host *host)
{
	void *mmio = host->mmio;
	struct carm_response *resp = (struct carm_response *) host->shm;
	unsigned int work = 0;
	unsigned int idx = host->resp_idx % RMSG_Q_LEN;

	while (1) {
		u32 status = le32_to_cpu(resp[idx].status);

		if (status == 0xffffffff) {
			VPRINTK("ending response on index %u\n", idx);
			writel(idx << 3, mmio + CARM_RESP_IDX);
			break;
		}

		/* response to a message we sent */
		else if ((status & (1 << 31)) == 0) {
			VPRINTK("handling msg response on index %u\n", idx);
			carm_handle_resp(host, resp[idx].ret_handle, status);
			resp[idx].status = 0xffffffff;
		}

		/* asynchronous events the hardware throws our way */
		else if ((status & 0xff000000) == (1 << 31)) {
			u8 *evt_type_ptr = (u8 *) &resp[idx];
			u8 evt_type = *evt_type_ptr;
			printk(KERN_WARNING DRV_NAME "(%s): unhandled event type %d\n",
			       pci_name(host->pdev), (int) evt_type);
			resp[idx].status = 0xffffffff;
		}

		idx = NEXT_RESP(idx);
		work++;
	}

	VPRINTK("EXIT, work==%u\n", work);
	host->resp_idx += work;
}

static irqreturn_t carm_interrupt(int irq, void *__host, struct pt_regs *regs)
{
	struct carm_host *host = __host;
	void *mmio;
	u32 mask;
	int handled = 0;
	unsigned long flags;

	if (!host) {
		VPRINTK("no host\n");
		return IRQ_NONE;
	}

	spin_lock_irqsave(&io_request_lock, flags);

	mmio = host->mmio;

	/* reading should also clear interrupts */
	mask = readl(mmio + CARM_INT_STAT);

	if (mask == 0 || mask == 0xffffffff) {
		VPRINTK("no work, mask == 0x%x\n", mask);
		goto out;
	}

	if (mask & INT_ACK_MASK)
		writel(mask, mmio + CARM_INT_STAT);

	if (unlikely(host->state == HST_INVALID)) {
		VPRINTK("not initialized yet, mask = 0x%x\n", mask);
		goto out;
	}

	if (mask & CARM_HAVE_RESP) {
		handled = 1;
		carm_handle_responses(host);
	}

out:
	spin_unlock_irqrestore(&io_request_lock, flags);
	VPRINTK("EXIT\n");
	return IRQ_RETVAL(handled);
}

static void carm_fsm_task (void *_data)
{
	struct carm_host *host = _data;
	unsigned long flags;
	unsigned int state;
	int rc, i, next_dev;
	int reschedule = 0;
	int new_state = HST_INVALID;

	spin_lock_irqsave(&io_request_lock, flags);
	state = host->state;
	spin_unlock_irqrestore(&io_request_lock, flags);

	DPRINTK("ENTER, state == %s\n", state_name[state]);

	switch (state) {
	case HST_PROBE_START:
		new_state = HST_ALLOC_BUF;
		reschedule = 1;
		break;

	case HST_ALLOC_BUF:
		rc = carm_send_special(host, carm_fill_alloc_buf);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_SYNC_TIME:
		rc = carm_send_special(host, carm_fill_sync_time);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_GET_FW_VER:
		rc = carm_send_special(host, carm_fill_get_fw_ver);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_PORT_SCAN:
		rc = carm_send_special(host, carm_fill_scan_channels);
		if (rc) {
			new_state = HST_ERROR;
			reschedule = 1;
		}
		break;

	case HST_DEV_SCAN_START:
		host->cur_scan_dev = -1;
		new_state = HST_DEV_SCAN;
		reschedule = 1;
		break;

	case HST_DEV_SCAN:
		next_dev = -1;
		for (i = host->cur_scan_dev + 1; i < CARM_MAX_PORTS; i++)
			if (host->dev_present & (1 << i)) {
				next_dev = i;
				break;
			}

		if (next_dev >= 0) {
			host->cur_scan_dev = next_dev;
			rc = carm_array_info(host, next_dev);
			if (rc) {
				new_state = HST_ERROR;
				reschedule = 1;
			}
		} else {
			new_state = HST_DEV_ACTIVATE;
			reschedule = 1;
		}
		break;

	case HST_DEV_ACTIVATE: {
		int activated = 0;
		for (i = 0; i < CARM_MAX_PORTS; i++)
			if (host->dev_active & (1 << i)) {
				carm_activate_disk(host, &host->port[i]);
				activated++;
			}

		printk(KERN_INFO DRV_NAME "(%s): %d ports activated\n",
		       pci_name(host->pdev), activated);

		new_state = HST_PROBE_FINISHED;
		reschedule = 1;
		break;
	}

	case HST_PROBE_FINISHED:
		up(&host->probe_sem);
		break;

	case HST_ERROR:
		/* FIXME: TODO */
		break;

	default:
		/* should never occur */
		printk(KERN_ERR PFX "BUG: unknown state %d\n", state);
		assert(0);
		break;
	}

	if (new_state != HST_INVALID) {
		spin_lock_irqsave(&io_request_lock, flags);
		host->state = new_state;
		spin_unlock_irqrestore(&io_request_lock, flags);
	}
	if (reschedule)
		schedule_task(&host->fsm_task);
}

static int carm_init_wait(void *mmio, u32 bits, unsigned int test_bit)
{
	unsigned int i;

	for (i = 0; i < 50000; i++) {
		u32 tmp = readl(mmio + CARM_LMUC);
		udelay(100);

		if (test_bit) {
			if ((tmp & bits) == bits)
				return 0;
		} else {
			if ((tmp & bits) == 0)
				return 0;
		}

		cond_resched();
	}

	printk(KERN_ERR PFX "carm_init_wait timeout, bits == 0x%x, test_bit == %s\n",
	       bits, test_bit ? "yes" : "no");
	return -EBUSY;
}

static void carm_init_responses(struct carm_host *host)
{
	void *mmio = host->mmio;
	unsigned int i;
	struct carm_response *resp = (struct carm_response *) host->shm;

	for (i = 0; i < RMSG_Q_LEN; i++)
		resp[i].status = 0xffffffff;

	writel(0, mmio + CARM_RESP_IDX);
}

static int carm_init_host(struct carm_host *host)
{
	void *mmio = host->mmio;
	u32 tmp;
	u8 tmp8;
	int rc;
	unsigned long flags;

	DPRINTK("ENTER\n");

	writel(0, mmio + CARM_INT_MASK);

	tmp8 = readb(mmio + CARM_INITC);
	if (tmp8 & 0x01) {
		tmp8 &= ~0x01;
		writeb(tmp8, CARM_INITC);
		readb(mmio + CARM_INITC);	/* flush */

		DPRINTK("snooze...\n");
		msleep(5000);
	}

	tmp = readl(mmio + CARM_HMUC);
	if (tmp & CARM_CME) {
		DPRINTK("CME bit present, waiting\n");
		rc = carm_init_wait(mmio, CARM_CME, 1);
		if (rc) {
			DPRINTK("EXIT, carm_init_wait 1 failed\n");
			return rc;
		}
	}
	if (tmp & CARM_RME) {
		DPRINTK("RME bit present, waiting\n");
		rc = carm_init_wait(mmio, CARM_RME, 1);
		if (rc) {
			DPRINTK("EXIT, carm_init_wait 2 failed\n");
			return rc;
		}
	}

	tmp &= ~(CARM_RME | CARM_CME);
	writel(tmp, mmio + CARM_HMUC);
	readl(mmio + CARM_HMUC);	/* flush */

	rc = carm_init_wait(mmio, CARM_RME | CARM_CME, 0);
	if (rc) {
		DPRINTK("EXIT, carm_init_wait 3 failed\n");
		return rc;
	}

	carm_init_buckets(mmio);

	writel(host->shm_dma & 0xffffffff, mmio + RBUF_ADDR_LO);
	writel((host->shm_dma >> 16) >> 16, mmio + RBUF_ADDR_HI);
	writel(RBUF_LEN, mmio + RBUF_BYTE_SZ);

	tmp = readl(mmio + CARM_HMUC);
	tmp |= (CARM_RME | CARM_CME | CARM_WZBC);
	writel(tmp, mmio + CARM_HMUC);
	readl(mmio + CARM_HMUC);	/* flush */

	rc = carm_init_wait(mmio, CARM_RME | CARM_CME, 1);
	if (rc) {
		DPRINTK("EXIT, carm_init_wait 4 failed\n");
		return rc;
	}

	writel(0, mmio + CARM_HMPHA);
	writel(INT_DEF_MASK, mmio + CARM_INT_MASK);

	carm_init_responses(host);

	/* start initialization, probing state machine */
	spin_lock_irqsave(&io_request_lock, flags);
	assert(host->state == HST_INVALID);
	host->state = HST_PROBE_START;
	spin_unlock_irqrestore(&io_request_lock, flags);

	schedule_task(&host->fsm_task);

	DPRINTK("EXIT\n");
	return 0;
}

static void carm_init_ports (struct carm_host *host)
{
	struct carm_port *port;
	request_queue_t *q;
	unsigned int i;

	for (i = 0; i < CARM_MAX_PORTS; i++) {
		port = &host->port[i];
		port->magic = CARM_MAGIC_PORT;
		port->host = host;
		port->port_no = i;
		tasklet_init(&port->tasklet, carm_rw_tasklet,
			     (unsigned long) port);

		q = &port->q;

		blk_init_queue(q, carm_rq_fn);
		q->queuedata = port;
		blk_queue_bounce_limit(q, host->pdev->dma_mask);
		blk_queue_headactive(q, 0);

		q->back_merge_fn = carm_back_merge_fn;
		q->front_merge_fn = carm_front_merge_fn;
		q->merge_requests_fn = carm_merge_requests_fn;
	}
}

static request_queue_t *carm_find_queue(kdev_t device)
{
	struct carm_host *host;

	host = blk_dev[MAJOR(device)].data;
	if (!host)
		return NULL;
	if (host->magic != CARM_MAGIC_HOST)
		return NULL;

	DPRINTK("match: major %d, minor %d\n",
		MAJOR(device), MINOR(device));
	return &host->port[MINOR(device) >> CARM_PART_SHIFT].q;
}

static int carm_init_disks(struct carm_host *host)
{
	host->gendisk.major = host->major;
	host->gendisk.major_name = host->name;
	host->gendisk.minor_shift = CARM_PART_SHIFT;
	host->gendisk.max_p = CARM_MINORS_PER_MAJOR;
	host->gendisk.part = host->gendisk_hd;
	host->gendisk.sizes = host->blk_sizes;
	host->gendisk.nr_real = CARM_MAX_PORTS;
	host->gendisk.fops = &carm_bd_ops;

	blk_dev[host->major].queue = carm_find_queue;
	blk_dev[host->major].data = host;
	blk_size[host->major] = host->blk_sizes;
	blksize_size[host->major] = host->blk_block_sizes;
	hardsect_size[host->major] = host->blk_sect_sizes;

	add_gendisk(&host->gendisk);

	return 0;
}

static void carm_free_disks(struct carm_host *host)
{
	unsigned int i;

	del_gendisk(&host->gendisk);

	for (i = 0; i < CARM_MAX_PORTS; i++) {
		struct carm_port *port = &host->port[i];

		blk_cleanup_queue(&port->q);
	}

	blk_dev[host->major].queue = NULL;
	blk_dev[host->major].data = NULL;
	blk_size[host->major] = NULL;
	blksize_size[host->major] = NULL;
	hardsect_size[host->major] = NULL;
}

static void carm_stop_tasklets(struct carm_host *host)
{
	unsigned int i;

	tasklet_kill(&host->oob_tasklet);

	for (i = 0; i < CARM_MAX_PORTS; i++) {
		struct carm_port *port = &host->port[i];
		tasklet_kill(&port->tasklet);
	}
}

static int carm_init_shm(struct carm_host *host)
{
	host->shm = pci_alloc_consistent(host->pdev, CARM_SHM_SIZE,
					 &host->shm_dma);
	if (!host->shm)
		return -ENOMEM;

	host->msg_base = host->shm + RBUF_LEN;
	host->msg_dma = host->shm_dma + RBUF_LEN;

	memset(host->shm, 0xff, RBUF_LEN);
	memset(host->msg_base, 0, PDC_SHM_SIZE - RBUF_LEN);

	return 0;
}

static int carm_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static unsigned int printed_version;
	struct carm_host *host;
	unsigned int pci_dac;
	int rc;
	unsigned int i;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

#if IF_64BIT_DMA_IS_POSSIBLE /* grrrr... */
	rc = pci_set_dma_mask(pdev, 0xffffffffffffffffULL);
	if (!rc) {
		rc = pci_set_consistent_dma_mask(pdev, 0xffffffffffffffffULL);
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): consistent DMA mask failure\n",
				pci_name(pdev));
			goto err_out_regions;
		}
		pci_dac = 1;
	} else {
#endif
		rc = pci_set_dma_mask(pdev, 0xffffffffULL);
		if (rc) {
			printk(KERN_ERR DRV_NAME "(%s): DMA mask failure\n",
				pci_name(pdev));
			goto err_out_regions;
		}
		pci_dac = 0;
#if IF_64BIT_DMA_IS_POSSIBLE /* grrrr... */
	}
#endif

	host = kmalloc(sizeof(*host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR DRV_NAME "(%s): memory alloc failure\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(host, 0, sizeof(*host));
	host->magic = CARM_MAGIC_HOST;
	host->pdev = pdev;
	host->flags = pci_dac ? FL_DAC : 0;
	INIT_TQUEUE(&host->fsm_task, carm_fsm_task, host);
	INIT_LIST_HEAD(&host->host_list_node);
	init_MUTEX_LOCKED(&host->probe_sem);
	tasklet_init(&host->oob_tasklet, carm_oob_tasklet,
		     (unsigned long) host);
	carm_init_ports(host);

	for (i = 0; i < ARRAY_SIZE(host->req); i++)
		host->req[i].tag = i;

	host->mmio = ioremap(pci_resource_start(pdev, 0),
			     pci_resource_len(pdev, 0));
	if (!host->mmio) {
		printk(KERN_ERR DRV_NAME "(%s): MMIO alloc failure\n",
		       pci_name(pdev));
		rc = -ENOMEM;
		goto err_out_kfree;
	}

	rc = carm_init_shm(host);
	if (rc) {
		printk(KERN_ERR DRV_NAME "(%s): DMA SHM alloc failure\n",
		       pci_name(pdev));
		goto err_out_iounmap;
	}

	blk_init_queue(&host->oob_q, carm_oob_rq_fn);
	host->oob_q.queuedata = host;
	blk_queue_bounce_limit(&host->oob_q, pdev->dma_mask);
	blk_queue_headactive(&host->oob_q, 0);

	/*
	 * Figure out which major to use: 160, 161, or dynamic
	 */
	if (!test_and_set_bit(0, &carm_major_alloc))
		host->major = 160;
	else if (!test_and_set_bit(1, &carm_major_alloc))
		host->major = 161;
	else
		host->flags |= FL_DYN_MAJOR;

	host->id = carm_host_id;
	sprintf(host->name, DRV_NAME "/%d", carm_host_id);

	rc = register_blkdev(host->major, host->name, &carm_bd_ops);
	if (rc < 0)
		goto err_out_free_majors;
	if (host->flags & FL_DYN_MAJOR)
		host->major = rc;

	rc = carm_init_disks(host);
	if (rc)
		goto err_out_blkdev_disks;

	pci_set_master(pdev);

	rc = request_irq(pdev->irq, carm_interrupt, SA_SHIRQ, DRV_NAME, host);
	if (rc) {
		printk(KERN_ERR DRV_NAME "(%s): irq alloc failure\n",
		       pci_name(pdev));
		goto err_out_blkdev_disks;
	}

	rc = carm_init_host(host);
	if (rc)
		goto err_out_free_irq;

	DPRINTK("waiting for probe_sem\n");
	down(&host->probe_sem);

	printk(KERN_INFO "%s: pci %s, ports %d, io %lx, irq %u, major %d\n",
	       host->name, pci_name(pdev), (int) CARM_MAX_PORTS,
	       pci_resource_start(pdev, 0), pdev->irq, host->major);

	carm_host_id++;
	pci_set_drvdata(pdev, host);
	list_add_tail(&host->host_list_node, &carm_host_list);
	return 0;

err_out_free_irq:
	free_irq(pdev->irq, host);
err_out_blkdev_disks:
	carm_free_disks(host);
	unregister_blkdev(host->major, host->name);
err_out_free_majors:
	if (host->major == 160)
		clear_bit(0, &carm_major_alloc);
	else if (host->major == 161)
		clear_bit(1, &carm_major_alloc);
	blk_cleanup_queue(&host->oob_q);
	pci_free_consistent(pdev, CARM_SHM_SIZE, host->shm, host->shm_dma);
err_out_iounmap:
	iounmap(host->mmio);
err_out_kfree:
	kfree(host);
err_out_regions:
	pci_release_regions(pdev);
err_out:
	pci_disable_device(pdev);
	return rc;
}

static void carm_remove_one (struct pci_dev *pdev)
{
	struct carm_host *host = pci_get_drvdata(pdev);

	if (!host) {
		printk(KERN_ERR PFX "BUG: no host data for PCI(%s)\n",
		       pci_name(pdev));
		return;
	}

	free_irq(pdev->irq, host);
	carm_stop_tasklets(host);
	carm_free_disks(host);
	unregister_blkdev(host->major, host->name);
	if (host->major == 160)
		clear_bit(0, &carm_major_alloc);
	else if (host->major == 161)
		clear_bit(1, &carm_major_alloc);
	blk_cleanup_queue(&host->oob_q);
	pci_free_consistent(pdev, CARM_SHM_SIZE, host->shm, host->shm_dma);
	iounmap(host->mmio);
	list_del(&host->host_list_node);
	kfree(host);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int __init carm_init(void)
{
	int rc;
	carm_register_proc();
	rc = pci_module_init(&carm_driver);
	if (rc)
		carm_unregister_proc();
	return rc;
}

static void __exit carm_exit(void)
{
	pci_unregister_driver(&carm_driver);
	carm_unregister_proc();
}

module_init(carm_init);
module_exit(carm_exit);

#ifdef CONFIG_PROC_FS

#define CARM_PROC_FN "sx8"

static struct proc_dir_entry *carm_proc;

static void carm_seq_show1(struct seq_file *f, struct carm_host *host,
			   unsigned int port_idx)
{
	struct carm_port *port = &host->port[port_idx];

	seq_printf(f, "%s_%u: %Lu sectors\n", host->name, port_idx,
		   (unsigned long long) port->capacity);
}

static void *carm_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos == 0) {
		return (void *) 1;
	} else {
		return NULL;
	}
}

static void *carm_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void carm_seq_stop(struct seq_file *seq, void *v)
{
	/* do nothing */
}

#define carm_host_entry(list) \
	list_entry((list), struct carm_host, host_list_node)

static int carm_seq_show(struct seq_file *seq, void *v)
{
	struct list_head *p;
	unsigned int i;

	list_for_each(p, &carm_host_list) {
		struct carm_host *host;

		host = carm_host_entry(p);
		for (i = 0; i < CARM_MAX_PORTS; i++)
			if (host->dev_active & (1 << i))
				carm_seq_show1(seq, host, i);
	}

	return 0;
}

static struct seq_operations carm_seq_ops = {
	.start	= carm_seq_start,
	.stop	= carm_seq_stop,
	.next	= carm_seq_next,
	.show	= carm_seq_show,
};

static int carm_proc_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct proc_dir_entry *proc;
	int rc;

	rc = seq_open(file, &carm_seq_ops);
	if (!rc) {
		seq = file->private_data;
		proc = PDE(inode);
		seq->private = proc->data;
	}

	return rc;
}

static struct file_operations carm_proc_fops = {
	.owner	= THIS_MODULE,
	.open	= carm_proc_open,
	.read	= seq_read,
	.llseek	= seq_lseek,
	.release= seq_release,
};

static void carm_register_proc(void)
{
	carm_proc = create_proc_entry(CARM_PROC_FN, S_IFREG, proc_root_driver);
	if (carm_proc) {
		carm_proc->proc_fops = &carm_proc_fops;
		SET_MODULE_OWNER(carm_proc);
	}
}

static void carm_unregister_proc(void)
{
	if (carm_proc)
		remove_proc_entry(CARM_PROC_FN, proc_root_driver);
}

#endif /* CONFIG_PROC_FS */

