#ifndef __3C59X_COMPAT_H__
#define __3C59X_COMPAT_H__

#include <linux/if.h>

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

static inline void *netdev_priv(struct net_device *dev)
{
	return dev->priv;
}

static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}

#ifndef WARN_ON
#define WARN_ON(x)	do { } while (0)
#endif

#endif /* __3C59X_COMPAT_H__ */
