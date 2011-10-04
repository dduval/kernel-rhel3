#ifndef __E1000_COMPAT_H__
#define __E1000_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#define __iomem

#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN    2
#endif

#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

#define DMA_64BIT_MASK  0xffffffffffffffffULL
#define DMA_32BIT_MASK  0x00000000ffffffffULL

/* Driver transmit return codes */
#define NETDEV_TX_OK 0          /* driver took care of packet */
#define NETDEV_TX_BUSY 1        /* driver tx path was busy*/

static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
        return (struct mii_ioctl_data *) &rq->ifr_ifru;
}

static inline unsigned long msecs_to_jiffies(unsigned long msecs)
{
	return ((HZ * msecs + 999) / 1000);
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

static inline void msleep(unsigned long msecs)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

static inline void msleep_interruptible(unsigned long msecs)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

#endif /* __E1000_COMPAT_H__ */
