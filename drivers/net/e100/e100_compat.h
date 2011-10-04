#ifndef __E100_COMPAT_H__
#define __E100_COMPAT_H__

#define __iomem
#define __force

typedef u32 pm_message_t;

typedef int pci_power_t;

#define PCI_D0	((pci_power_t __force) 0)
#define PCI_D1	((pci_power_t __force) 1)
#define PCI_D2	((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)

#define pci_choose_state(pdev, state)	(state)

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN    2
#endif

#ifndef SET_NETDEV_DEV
/* 2.6 compatibility */
#define SET_NETDEV_DEV(net, pdev) do { } while (0)
#endif

#define netdev_priv(dev) dev->priv

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

/* Test for pci_map_single or pci_map_page having generated an error.  */

static inline int
pci_dma_mapping_error(dma_addr_t dma_addr)
{
	return dma_addr == 0;
}

#endif /* __E100_COMPAT_H__ */
