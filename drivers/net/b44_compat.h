#ifndef __B44_COMPAT_H__
#define __B44_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#define __user
#define __iomem

#define MODULE_VERSION(dummy)

static inline void *netdev_priv(struct net_device *dev)
{
	return dev->priv;
}

static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}

#endif /* __B44_COMPAT_H__ */
