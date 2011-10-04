#ifndef __TG3_COMPAT_H__
#define __TG3_COMPAT_H__

#define __user
#define __iomem
#define __bitwise
#define __force

#define DMA_64BIT_MASK ((dma_addr_t)0xffffffffffffffffULL)
#define DMA_32BIT_MASK ((dma_addr_t)0xffffffffULL)

#define MODULE_VERSION(ver)

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

/* Driver transmit return codes */
#define NETDEV_TX_OK 0          /* driver took care of packet */
#define NETDEV_TX_BUSY 1        /* driver tx path was busy*/

#define mmiowb() do { } while(0)

#define DMA_40BIT_MASK	0x000000ffffffffffULL

#define skb_header_cloned(skb) 0

#define pci_choose_state(pdev, state) (state)

typedef u32 pm_message_t;

typedef int __bitwise pci_power_t;

#define PCI_D0	((pci_power_t __force) 0)
#define PCI_D1	((pci_power_t __force) 1)
#define PCI_D2	((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)

#define pci_choose_state(pdev, state)	(state)

#ifndef ADVERTISE_PAUSE
#define ADVERTISE_PAUSE_CAP		0x0400
#endif
#ifndef ADVERTISE_PAUSE_ASYM
#define ADVERTISE_PAUSE_ASYM		0x0800
#endif
#ifndef LPA_PAUSE
#define LPA_PAUSE_CAP			0x0400
#endif
#ifndef LPA_PAUSE_ASYM
#define LPA_PAUSE_ASYM			0x0800
#endif

/*
 * Convert jiffies to milliseconds and back.
 *
 * Avoid unnecessary multiplications/divisions in the
 * two most common HZ cases:
 */
static inline unsigned int jiffies_to_msecs(const unsigned long j)
{
#if HZ <= 1000 && !(1000 % HZ)
        return (1000 / HZ) * j;
#elif HZ > 1000 && !(HZ % 1000)
        return (j + (HZ / 1000) - 1)/(HZ / 1000);
#else
        return (j * 1000) / HZ;
#endif
}

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	if (m > jiffies_to_msecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
#if HZ <= 1000 && !(1000 % HZ)
	return (m + (1000 / HZ) - 1) / (1000 / HZ);
#elif HZ > 1000 && !(HZ % 1000)
	return m * (HZ / 1000);
#else
	return (m * HZ + 999) / 1000;
#endif
}

/**
 * msleep - sleep safely even with waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
static inline void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	if (unlikely(crashdump_mode())) {
		while (msecs--) udelay(1000);
		return;
	}

	while (timeout) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
	}
}

/**
 * msleep_interruptible - sleep waiting for waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
static inline unsigned long msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout && !signal_pending(current)) {
		set_current_state(TASK_INTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
	}
	return jiffies_to_msecs(timeout);
}

#define pci_dma_sync_single_for_cpu(pdev, dma_addr, len, dir) \
	pci_dma_sync_single((pdev), (dma_addr), (len), (dir))

#define pci_dma_sync_single_for_device(pdev, dma_addr, len, dir) \
	pci_dma_sync_single((pdev), (dma_addr), (len), (dir))

#define pci_get_device(vendor, device, from) \
	pci_find_device(vendor, device, from)
#define pci_get_slot(bus, devfn) pci_find_slot((bus)->number, devfn)
#define pci_dev_put(pdev)

#define pci_enable_msi(pdev)	(-1)
#define pci_disable_msi(pdev)

#define pci_set_consistent_dma_mask(pdev,mask)	(0)

/**
 * PCI_DEVICE - macro used to describe a specific pci device
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device.  The subvendor and subdevice fields will be set to
 * PCI_ANY_ID.
 */
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * pci_dev_present - Returns 1 if device matching the device list is present, 0 if not.
 * @ids: A pointer to a null terminated list of struct pci_device_id structures
 * that describe the type of PCI device the caller is trying to find.
 *
 * This is a cheap knock-off, just to help in back-porting tg3 from
 * later kernels...beware of changes in usage...
 */
static inline int pci_dev_present(const struct pci_device_id *ids)
{
	const struct pci_device_id *dev;

	for (dev = ids; dev->vendor; dev++) {
		if (pci_find_device(dev->vendor, dev->device, NULL))
			return 1;
	}
	return 0;
}

/* Workqueue / task queue backwards compatibility stuff */

#define work_struct tq_struct
#define INIT_WORK INIT_TQUEUE
#define schedule_work schedule_task

#endif /* __TG3_COMPAT_H__ */
