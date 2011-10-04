#ifndef __E100_COMPAT_H__
#define __E100_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#ifndef SET_NETDEV_DEV
/* 2.6 compatibility */
#define SET_NETDEV_DEV(net, pdev) do { } while (0)
#endif

#define netdev_priv(dev) dev->priv

static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}

#endif /* __E100_COMPAT_H__ */
