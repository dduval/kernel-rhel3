#ifndef __EMU_WRAPPER_H
#define __EMU_WRAPPER_H

#include <linux/version.h>

#define UP_INODE_SEM(a)
#define DOWN_INODE_SEM(a)

#define GET_INODE_STRUCT()

#define vma_get_pgoff(v)	((v)->vm_pgoff)

#define compat_request_region request_region

#undef MOD_INC_USE_COUNT
#define MOD_INC_USE_COUNT

#undef MOD_DEC_USE_COUNT
#define MOD_DEC_USE_COUNT

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,16)
#define __devexit_p
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,9)
#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })

#define MODULE_LICENSE(foo)
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,8)
extern loff_t no_llseek(struct file *file, loff_t offset, int origin);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3)
int pci_set_dma_mask(struct pci_dev *dev, dma_addr_t mask);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
static inline void *pci_get_drvdata (struct pci_dev *pdev)
{
        return pdev->driver_data;
}

static inline void pci_set_drvdata (struct pci_dev *pdev, void *data)
{
        pdev->driver_data = data;
}
#endif

#ifndef DSP_CAP_MULTI
#       define DSP_CAP_MULTI            0x00004000      /* support multiple open */
#endif

#ifndef DSP_CAP_BIND
#	define DSP_CAP_BIND             0x00008000      /* channel binding to front/rear/cneter/lfe */
#endif


#endif  /* __EMU_WRAPPER_H */
