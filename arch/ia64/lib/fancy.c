/* pci mapping routines 
 * 
 * these routines assume that the memory we are mapping is
 * already accessible by the device. This means that no bouncing
 * or re-mapping needs to take place. We can basically just return
 * the physical address of the memory. This is ensured by the
 * 'upper' layers in the kernel, ie, the bounce buffering code.
 */
 
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/scatterlist.h>


#define flush_write_buffers() do {} while (0)

	
/* Map a single buffer of the indicated size for DMA in streaming mode.
 * The 32-bit bus address to use is returned.

 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_dma_sync_single is performed.
 */
dma_addr_t fancy_pci_map_single(struct pci_dev *hwdev, void *ptr,
                                       size_t size, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       flush_write_buffers();
       return virt_to_bus(ptr);
}

/* Unmap a single streaming mode DMA translation.  The dma_addr and size
 * must match what was provided for in a previous pci_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guarenteed to see
 * whatever the device wrote there.
 */
void fancy_pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
                                   size_t size, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       /* Nothing to do */
}

/*
 * pci_{map,unmap}_single_page maps a kernel page to a dma_addr_t. identical
 * to pci_map_single, but takes a struct page instead of a virtual address
*/
dma_addr_t fancy_pci_map_page(struct pci_dev *hwdev, struct page *page,
                                     unsigned long offset, size_t size, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();

       return (page - mem_map) * PAGE_SIZE + offset;
}

void fancy_pci_unmap_page(struct pci_dev *hwdev, dma_addr_t dma_address,
                                 size_t size, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       /* Nothing to do */
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int fancy_pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
                            int nents, int direction)
{
       int i;

       if (direction == PCI_DMA_NONE)
               BUG();
 
       /*
        * temporary 2.4 hack
        */
       for (i = 0; i < nents; i++ ) {
               if (sg[i].address && sg[i].page)
                       BUG();
               else if (!sg[i].address && !sg[i].page)
                       BUG();
 
               if (sg[i].address)
                       sg[i].dma_address = virt_to_bus(sg[i].address);
               else
                       sg[i].dma_address = page_to_bus(sg[i].page) + sg[i].offset;
	       sg[i].dma_length = sg[i].length;
       }
 
       flush_write_buffers();
       return nents;
}

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void fancy_pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg,
                               int nents, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       /* Nothing to do */
}

/* Make physical memory consistent for a single
 * streaming mode DMA translation after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, the
 * device again owns the buffer.
 */
void fancy_pci_dma_sync_single(struct pci_dev *hwdev,
                                      dma_addr_t dma_handle,
                                      size_t size, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       flush_write_buffers();
}

/* Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list,
 * same rules and usage.
 */
void fancy_pci_dma_sync_sg(struct pci_dev *hwdev,
                                  struct scatterlist *sg,
                                  int nelems, int direction)
{
       if (direction == PCI_DMA_NONE)
               BUG();
       flush_write_buffers();
}

#define PCI_DMA_BUS_IS_PHYS (1)
#define pci_dac_dma_supported(pci_dev, mask)      (0)

EXPORT_SYMBOL(fancy_pci_map_single);
EXPORT_SYMBOL(fancy_pci_unmap_single);
EXPORT_SYMBOL(fancy_pci_map_sg);
EXPORT_SYMBOL(fancy_pci_unmap_sg);
EXPORT_SYMBOL(fancy_pci_dma_sync_single);
EXPORT_SYMBOL(fancy_pci_dma_sync_sg);




