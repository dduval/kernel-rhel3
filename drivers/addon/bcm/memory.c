
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/*
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/*
 * Broadcom Corporation uBSec SDK
 */
/*
 * Revision History:
 *
 * March 2001 PW Created, Release for Linux 2.4 UP and SMP kernel
 */                                         

#include "cdevincl.h"

#ifndef LINUX2dot2
struct pci_dev *globalpDev;
#endif 

void *LinuxAllocateMemory(unsigned long size) ;
void LinuxFreeMemory(void *virtual);
#if 0
void *LinuxAllocateMemory(unsigned long size) {
    dma_addr_t dma_handle;
    struct list_node *temp;
    void *virtual;
    
    temp = my_get_free_q();
    if (temp == NULL) {

        printk("<0> Unable to allocate memory\n");
        while(1);
    }
    virtual = pci_alloc_consistent(globalpDev, size, &dma_handle);

    my_put_busy_q(temp, virtual, size, dma_handle);

    return virtual;
}

void
LinuxFreeMemory(void *virtual)
{
    struct list_node *temp;
    struct pci_dev *pDev;


    if ((temp = my_get_busy_q(virtual)) == NULL) {

        printk("<0> Unable to find virtual %x\n",virtual);
        return;
    }
    pci_free_consistent(globalpDev, temp->size, virtual, temp->dma_handle);
    my_put_free_q(temp);    

}
#else

void *LinuxAllocateMemory(unsigned long size)
{
   return kmalloc(size, GFP_KERNEL|GFP_ATOMIC);
}

void
LinuxFreeMemory(void *virtual)
{
    kfree(virtual);
}

void *LinuxAllocateDMAMemory(unsigned long size) 
{
    return kmalloc(size, GFP_KERNEL| GFP_ATOMIC);
}

void
LinuxFreeDMAMemory(void *virtual)
{
    kfree(virtual);
}
#endif

unsigned long
LinuxGetPhysicalAddress(void *virtual)
{
   return virt_to_bus(virtual);
}

unsigned long
LinuxGetVirtualAddress(void *virtual)
{
   return (unsigned long) virtual ;
}

void *LinuxMapPhysToIO(unsigned long Physical_Address, int size)
{
	return ioremap(Physical_Address,size );
}

void LinuxUnMapIO( unsigned long ioaddr)
{
	iounmap((void *)ioaddr);
}

/* Made the access functions generic as viewed by SRL(OS_AllocateDMA ... */
void * Linux_AllocateDMAMemory(ubsec_DeviceContext_t * context, int size)
{
	return LinuxAllocateMemory(size);
}
void  Linux_FreeDMAMemory(void * virtual , int size)
{
	LinuxFreeMemory(virtual);
	return;
}
