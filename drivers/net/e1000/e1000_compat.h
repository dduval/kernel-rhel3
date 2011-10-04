#ifndef __E1000_COMPAT_H__
#define __E1000_COMPAT_H__

#define PMSG_SUSPEND 3

#define skb_header_cloned(skb) 0

#define __iomem
#define __bitwise
#define __force

typedef u32 pm_message_t;

typedef int __bitwise pci_power_t;

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

#define netdev_priv(dev) dev->priv

#ifndef WARN_ON
#define WARN_ON(x)	do { } while (0)
#endif

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
