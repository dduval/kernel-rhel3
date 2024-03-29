/*
 * pci.c - Low-Level PCI Access in IA-64
 *
 * Derived from bios32.c of i386 tree.
 */
#include <linux/config.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/acpi.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include <asm/sal.h>


#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif
#include <asm/irq.h>


#undef DEBUG
#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

#ifdef CONFIG_IA64_MCA
extern void ia64_mca_check_errors( void );
#endif

static unsigned int acpi_root_bridges;

struct pci_fixup pcibios_fixups[1];

struct pci_ops *pci_root_ops;

int (*pci_config_read)(int seg, int bus, int dev, int fn, int reg, int len, u32 *value);
int (*pci_config_write)(int seg, int bus, int dev, int fn, int reg, int len, u32 value);


/*
 * Low-level SAL-based PCI configuration access functions. Note that SAL
 * calls are already serialized (via sal_lock), so we don't need another
 * synchronization mechanism here.
 */

#define PCI_SAL_ADDRESS(seg, bus, dev, fn, reg) \
	((u64)(seg << 24) | (u64)(bus << 16) | \
	 (u64)(dev << 11) | (u64)(fn << 8) | (u64)(reg))

static int
pci_sal_read (int seg, int bus, int dev, int fn, int reg, int len, u32 *value)
{
	int result = 0;
	u64 data = 0;

	if (!value || (seg > 255) || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	result = ia64_sal_pci_config_read(PCI_SAL_ADDRESS(seg, bus, dev, fn, reg), len, &data);

	*value = (u32) data;

	return result;
}

static int
pci_sal_write (int seg, int bus, int dev, int fn, int reg, int len, u32 value)
{
	if ((seg > 255) || (bus > 255) || (dev > 31) || (fn > 7) || (reg > 255))
		return -EINVAL;

	return ia64_sal_pci_config_write(PCI_SAL_ADDRESS(seg, bus, dev, fn, reg), len, value);
}


static int
pci_sal_read_config_byte (struct pci_dev *dev, int where, u8 *value)
{
	int result = 0;
	u32 data = 0;

	if (!value)
		return -EINVAL;

	result = pci_sal_read(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			      PCI_FUNC(dev->devfn), where, 1, &data);

	*value = (u8) data;

	return result;
}

static int
pci_sal_read_config_word (struct pci_dev *dev, int where, u16 *value)
{
	int result = 0;
	u32 data = 0;

	if (!value)
		return -EINVAL;

	result = pci_sal_read(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			      PCI_FUNC(dev->devfn), where, 2, &data);

	*value = (u16) data;

	return result;
}

static int
pci_sal_read_config_dword (struct pci_dev *dev, int where, u32 *value)
{
	if (!value)
		return -EINVAL;

	return pci_sal_read(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			    PCI_FUNC(dev->devfn), where, 4, value);
}

static int
pci_sal_write_config_byte (struct pci_dev *dev, int where, u8 value)
{
	return pci_sal_write(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 1, value);
}

static int
pci_sal_write_config_word (struct pci_dev *dev, int where, u16 value)
{
	return pci_sal_write(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 2, value);
}

static int
pci_sal_write_config_dword (struct pci_dev *dev, int where, u32 value)
{
	return pci_sal_write(PCI_SEGMENT(dev), dev->bus->number, PCI_SLOT(dev->devfn),
			     PCI_FUNC(dev->devfn), where, 4, value);
}

struct pci_ops pci_sal_ops = {
	pci_sal_read_config_byte,
	pci_sal_read_config_word,
	pci_sal_read_config_dword,
	pci_sal_write_config_byte,
	pci_sal_write_config_word,
	pci_sal_write_config_dword
};


/*
 * Initialization. Uses the SAL interface
 */

static struct pci_controller *
alloc_pci_controller(int seg)
{
	struct pci_controller *controller;

	controller = kmalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return NULL;

	memset(controller, 0, sizeof(*controller));
	controller->segment = seg;
	return controller;
}

static u64
add_io_space (struct acpi_resource_address64 *addr)
{
	u64 offset;
	int sparse = 0;
	int i;

	if (addr->address_translation_offset == 0)
		return IO_SPACE_BASE(0);	/* part of legacy IO space */

	if (addr->attribute.io.translation_attribute == ACPI_SPARSE_TRANSLATION)
		sparse = 1;

	offset = (u64) ioremap(addr->address_translation_offset, 0);
	for (i = 0; i < num_io_spaces; i++)
		if (io_space[i].mmio_base == offset &&
		    io_space[i].sparse == sparse)
			return IO_SPACE_BASE(i);

	if (num_io_spaces == MAX_IO_SPACES) {
		printk("Too many IO port spaces\n");
		return ~0;
	}

	i = num_io_spaces++;
	io_space[i].mmio_base = offset;
	io_space[i].sparse = sparse;

	return IO_SPACE_BASE(i);
}

static acpi_status
count_window (struct acpi_resource *resource, void *data)
{
	unsigned int *windows = (unsigned int *) data;
	struct acpi_resource_address64 addr;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &addr);
	if (ACPI_SUCCESS(status))
		if (addr.resource_type == ACPI_MEMORY_RANGE ||
		    addr.resource_type == ACPI_IO_RANGE)
			(*windows)++;

	return AE_OK;
}

struct pci_root_info {
	struct pci_controller *controller;
	char *name;
};

static acpi_status
add_window (struct acpi_resource *res, void *data)
{
	struct pci_root_info *info = (struct pci_root_info *) data;
	struct pci_window *window;
	struct acpi_resource_address64 addr;
	acpi_status status;
	unsigned long flags, offset = 0;

	status = acpi_resource_to_address64(res, &addr);
	if (ACPI_SUCCESS(status)) {
		if (!addr.address_length)
			return AE_OK;

		if (addr.resource_type == ACPI_MEMORY_RANGE) {
			flags = IORESOURCE_MEM;
			offset = addr.address_translation_offset;
		} else if (addr.resource_type == ACPI_IO_RANGE) {
			flags = IORESOURCE_IO;
			offset = add_io_space(&addr);
			if (offset == ~0)
				return AE_OK;
		} else
			return AE_OK;

		window = &info->controller->window[info->controller->windows++];
		window->resource.flags |= flags;
		window->resource.start  = addr.min_address_range;
		window->resource.end    = addr.max_address_range;
		window->offset		= offset;
	}

	return AE_OK;
}

static struct pci_bus *
scan_root_bus(int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b;

	/*
	 * We know this is a new root bus we haven't seen before, so
	 * scan it, even if we've seen the same bus number in a different
	 * segment.
	 */
	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (!b)
		return NULL;

	memset(b, 0, sizeof(*b));
	INIT_LIST_HEAD(&b->children);
	INIT_LIST_HEAD(&b->devices);

	list_add_tail(&b->node, &pci_root_buses);

	b->number = b->secondary = bus;
	b->resource[0] = &ioport_resource;
	b->resource[1] = &iomem_resource;

	b->sysdata = sysdata;
	b->ops = ops;
	b->subordinate = pci_do_scan_bus(b);

	return b;
}

struct pci_bus *
pcibios_scan_root(void *handle, int seg, int bus)
{
	struct pci_root_info info;
	struct pci_controller *controller;
	unsigned int windows = 0;
	char *name;

	acpi_root_bridges++;

	controller = alloc_pci_controller(seg);
	if (!controller)
		return NULL;

	controller->acpi_handle = handle;

	acpi_walk_resources(handle, METHOD_NAME__CRS, count_window, &windows);
	controller->window = kmalloc(sizeof(*controller->window) * windows, GFP_KERNEL);
	if (!controller->window)
		goto out2;

	name = kmalloc(16, GFP_KERNEL);
	if (!name)
		goto out3;

	sprintf(name, "PCI Bus %04x:%02x", seg, bus);
	info.controller = controller;
	info.name = name;
	acpi_walk_resources(handle, METHOD_NAME__CRS, add_window, &info);

	return scan_root_bus(bus, pci_root_ops, controller);

out3:
	kfree(controller->window);
out2:
	kfree(controller);
	return NULL;
}

void __init
pcibios_config_init (void)
{
	if (pci_root_ops)
		return;

	printk("PCI: Using SAL to access configuration space\n");

	pci_root_ops = &pci_sal_ops;
	pci_config_read = pci_sal_read;
	pci_config_write = pci_sal_write;

	return;
}

void __init
pcibios_init (void)
{
#	define PCI_BUSES_TO_SCAN 256
	int i = 0;
	struct pci_controller *controller;

#ifdef CONFIG_IA64_MCA
	ia64_mca_check_errors();    /* For post-failure MCA error logging */
#endif

	pcibios_config_init();

	platform_pci_fixup(0);	/* phase 0 fixups (before buses scanned) */

	/* Only probe blindly if ACPI didn't tell us about root bridges */
	if (!acpi_root_bridges) {
		printk("PCI: Probing PCI hardware\n");
		controller = alloc_pci_controller(0);
		if (controller)
			for (i = 0; i < PCI_BUSES_TO_SCAN; i++)
				pci_scan_bus(i, pci_root_ops, controller);
	}

	platform_pci_fixup(1);	/* phase 1 fixups (after buses scanned) */

	return;
}

void __init
pcibios_fixup_device_resources(struct pci_dev *dev, struct pci_bus *bus)
{
	struct pci_controller *controller = PCI_CONTROLLER(dev);
	struct pci_window *window;
	int i, j;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;

#define contains(win, res)	((res)->start >= (win)->start && \
				 (res)->end   <= (win)->end)

		for (j = 0; j < controller->windows; j++) {
			window = &controller->window[j];
			if (((dev->resource[i].flags & IORESOURCE_MEM &&
			      window->resource.flags & IORESOURCE_MEM) ||
			     (dev->resource[i].flags & IORESOURCE_IO &&
			      window->resource.flags & IORESOURCE_IO)) &&
			    contains(&window->resource, &dev->resource[i])) {
				dev->resource[i].start += window->offset;
				dev->resource[i].end   += window->offset;
			}
		}
	}
}

/*
 *  Called after each bus is probed, but before its children are examined.
 */
void __devinit
pcibios_fixup_bus (struct pci_bus *b)
{
	struct list_head *ln;

	for (ln = b->devices.next; ln != &b->devices; ln = ln->next)
		pcibios_fixup_device_resources(pci_dev_b(ln), b);
}

void __devinit
pcibios_update_resource (struct pci_dev *dev, struct resource *root,
			 struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __devinit
pcibios_update_irq (struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __devinit
pcibios_fixup_pbus_ranges (struct pci_bus * bus, struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

static inline int
pcibios_enable_resources (struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if (!dev)
		return -EINVAL;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		/* Only set up the desired resources.  */
		if (!(mask & (1 << idx)))
			continue;

		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because of resource collisions\n",
			       dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int
pcibios_enable_device (struct pci_dev *dev, int mask)
{
	int ret;

	ret = pcibios_enable_resources(dev, mask);
	if (ret < 0)
		return ret;

 	platform_pci_enable_device(dev);

	printk(KERN_INFO "PCI: Found IRQ %d for device %s\n", dev->irq, dev->slot_name);

	return 0;
}

void
pcibios_align_resource (void *data, struct resource *res,
		        unsigned long size, unsigned long align)
{
}

/*
 * PCI BIOS setup, always defaults to SAL interface
 */
char * __init
pcibios_setup (char *str)
{
	return NULL;
}

int
pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state, int write_combine)
{
	/*
	 * I/O space cannot be accessed via normal processor loads and stores on this
	 * platform.
	 */
	if (mmap_state == pci_mmap_io)
		/*
		 * XXX we could relax this for I/O spaces for which ACPI indicates that
		 * the space is 1-to-1 mapped.  But at the moment, we don't support
		 * multiple PCI address spaces and the legacy I/O space is not 1-to-1
		 * mapped, so this is moot.
		 */
		return -EINVAL;

	/*
	 * Leave vm_pgoff as-is, the PCI space address is the physical address on this
	 * platform.
	 */
	vma->vm_flags |= (VM_SHM | VM_LOCKED | VM_IO);

	if (write_combine)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_page_range(vma, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			     vma->vm_end - vma->vm_start,
			     vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
