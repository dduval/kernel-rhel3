#ifndef __TG3_COMPAT_H__
#define __TG3_COMPAT_H__

#define __iomem

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

/* Driver transmit return codes */
#define NETDEV_TX_OK 0          /* driver took care of packet */
#define NETDEV_TX_BUSY 1        /* driver tx path was busy*/

#endif /* __TG3_COMPAT_H__ */
