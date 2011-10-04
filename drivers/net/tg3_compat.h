#ifndef __TG3_COMPAT_H__
#define __TG3_COMPAT_H__

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

static inline void *netdev_priv(struct net_device *dev)
{
	return dev->priv;
}

#ifndef WARN_ON
#define WARN_ON(x)	do { } while (0)
#endif

#endif /* __TG3_COMPAT_H__ */
