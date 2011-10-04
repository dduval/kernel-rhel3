#ifndef __BNX2_COMPAT_H__
#define __BNX2_COMPAT_H__

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/netdevice.h>

#define __iomem
#define __bitwise
#define __force

#define MODULE_VERSION(ver)

typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

/* Driver transmit return codes */
#define NETDEV_TX_OK 0          /* driver took care of packet */
#define NETDEV_TX_BUSY 1        /* driver tx path was busy*/

#define mmiowb() do { } while(0)

#define skb_header_cloned(skb) 0

typedef u32 pm_message_t;

typedef int __bitwise pci_power_t;

#define PCI_D0	((pci_power_t __force) 0)
#define PCI_D1	((pci_power_t __force) 1)
#define PCI_D2	((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)

#define pci_choose_state(pdev, state)	(state)

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

#define pci_enable_msi(pdev)	(-1)
#define pci_disable_msi(pdev)

#define DMA_64BIT_MASK	0xffffffffffffffffULL
#define DMA_32BIT_MASK	0xffffffffULL

#define pci_set_consistent_dma_mask(pdev,mask)	(0)

/* Workqueue / task queue backwards compatibility stuff */

#define work_struct tq_struct
#define INIT_WORK INIT_TQUEUE
#define schedule_work schedule_task
#define flush_scheduled_work flush_scheduled_tasks

static inline void *netdev_priv(struct net_device *dev)
{
	return dev->priv;
}

#endif /* __BNX2_COMPAT_H__ */
