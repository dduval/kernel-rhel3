#ifndef __LIBATA_COMPAT_H__
#define __LIBATA_COMPAT_H__

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>

extern void ssleep(unsigned long secs);

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)


typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#define __iomem
#define __user

static inline struct page *nth_page(struct page *page, int n)
{
	return page + n;
}

#if HZ <= 1000 && !(1000 % HZ)
#  define MAX_MSEC_OFFSET \
	(ULONG_MAX - (1000 / HZ) + 1)
#elif HZ > 1000 && !(HZ % 1000)
#  define MAX_MSEC_OFFSET \
	(ULONG_MAX / (HZ / 1000))
#else
#  define MAX_MSEC_OFFSET \
	((ULONG_MAX - 999) / HZ)
#endif

static inline unsigned long msecs_to_jiffies(const unsigned int m)
{
	if (MAX_MSEC_OFFSET < UINT_MAX && m > (unsigned int)MAX_MSEC_OFFSET)
		return MAX_JIFFY_OFFSET;
#if HZ <= 1000 && !(1000 % HZ)
	return ((unsigned long)m + (1000 / HZ) - 1) / (1000 / HZ);
#elif HZ > 1000 && !(HZ % 1000)
	return (unsigned long)m * (HZ / 1000);
#else
	return ((unsigned long)m * HZ + 999) / 1000;
#endif
}

static inline void msleep(unsigned long msecs)
{
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(msecs_to_jiffies(msecs) + 1);
}

typedef u32 __le32;
typedef u64 __le64;

#define DMA_64BIT_MASK 0xffffffffffffffffULL
#define DMA_32BIT_MASK 0x00000000ffffffffULL

/* These definitions mirror those in pci.h, so they can be used
 * interchangeably with their PCI_ counterparts */
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

#define offset_in_page(p)	((unsigned long)(p) & ~PAGE_MASK)

#define MODULE_VERSION(ver_str)

struct device {
	struct pci_dev pdev;
};

static inline struct pci_dev *to_pci_dev(struct device *dev)
{
	return (struct pci_dev *) dev;
}

#define pdev_printk(lvl, pdev, fmt, args...)			\
	do {							\
		printk("%s%s(%s): ", lvl,			\
			(pdev)->driver && (pdev)->driver->name ? \
				(pdev)->driver->name : "PCI",	\
			pci_name(pdev));			\
		printk(fmt, ## args);				\
	} while (0)

static inline int pci_enable_msi(struct pci_dev *dev) { return -1; }
static inline void pci_disable_msi(struct pci_dev *dev) {}

static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
	if (mask == (u64)dev->dma_mask)
		return 0;
	return -EIO;
}

/* NOTE: dangerous! we ignore the 'gfp' argument */
#define dma_alloc_coherent(dev,sz,dma,gfp) \
	pci_alloc_consistent(to_pci_dev(dev),(sz),(dma))
#define dma_free_coherent(dev,sz,addr,dma_addr) \
	pci_free_consistent(to_pci_dev(dev),(sz),(addr),(dma_addr))

#define dma_map_sg(dev,a,b,c) \
	pci_map_sg(to_pci_dev(dev),(a),(b),(c))
#define dma_unmap_sg(dev,a,b,c) \
	pci_unmap_sg(to_pci_dev(dev),(a),(b),(c))

#define dma_map_single(dev,a,b,c) \
	pci_map_single(to_pci_dev(dev),(a),(b),(c))
#define dma_unmap_single(dev,a,b,c) \
	pci_unmap_single(to_pci_dev(dev),(a),(b),(c))

#define dma_mapping_error(addr) (0)

#define dev_get_drvdata(dev) \
	pci_get_drvdata(to_pci_dev(dev))
#define dev_set_drvdata(dev,ptr) \
	pci_set_drvdata(to_pci_dev(dev),(ptr))

static inline void *kcalloc(size_t nmemb, size_t size, int flags)
{
	size_t total = nmemb * size;
	void *mem = kmalloc(total, flags);
	if (mem)
		memset(mem, 0, total);
	return mem;
}

static inline void *kzalloc(size_t size, int flags)
{
	return kcalloc(1, size, flags);
}

static inline void pci_iounmap(struct pci_dev *pdev, void *mem)
{
	iounmap(mem);
}

/**
 * pci_intx - enables/disables PCI INTx for device dev
 * @pdev: the PCI device to operate on
 * @enable: boolean: whether to enable or disable PCI INTx
 *
 * Enables/disables PCI INTx for device dev
 */
static inline void
pci_intx(struct pci_dev *pdev, int enable)
{
	u16 pci_command, new;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);

	if (enable) {
		new = pci_command & ~PCI_COMMAND_INTX_DISABLE;
	} else {
		new = pci_command | PCI_COMMAND_INTX_DISABLE;
	}

	if (new != pci_command) {
		pci_write_config_word(pdev, PCI_COMMAND, new);
	}
}

static inline void __iomem *
pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	unsigned long start = pci_resource_start(dev, bar);
	unsigned long len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len || !start)
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (flags & IORESOURCE_IO) {
		BUG();
	}
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);
		return ioremap_nocache(start, len);
	}
	/* What? */
	return NULL;
}

static inline void sg_set_buf(struct scatterlist *sg, void *buf,
			      unsigned int buflen)
{
	sg->page = virt_to_page(buf);
	sg->offset = offset_in_page(buf);
	sg->length = buflen;
}

static inline void sg_init_one(struct scatterlist *sg, void *buf,
			       unsigned int buflen)
{
	memset(sg, 0, sizeof(*sg));
	sg_set_buf(sg, buf, buflen);
}

#endif /* __LIBATA_COMPAT_H__ */
