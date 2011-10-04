#include <linux/pci.h>
#include "emu_wrapper.h" 

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3)
int pci_set_dma_mask(struct pci_dev *dev, dma_addr_t mask)
{
	/*
	* we fall back to GFP_DMA when the mask isn't all 1s,
	* so we can't guarantee allocations that must be
	* within a tighter range than GFP_DMA..
	*/
	if(mask < 0x00ffffff)
		return -EIO;

	dev->dma_mask = mask;

	return 0;
}
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,8)
loff_t no_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}
#endif
