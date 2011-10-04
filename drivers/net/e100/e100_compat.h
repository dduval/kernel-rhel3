#ifndef __E100_COMPAT_H__
#define __E100_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#define __iomem

#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN    2
#endif

#ifndef SET_NETDEV_DEV
/* 2.6 compatibility */
#define SET_NETDEV_DEV(net, pdev) do { } while (0)
#endif

#define netdev_priv(dev) dev->priv

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

#endif /* __E100_COMPAT_H__ */
