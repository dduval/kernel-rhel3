/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000, 2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *  Copyright (C) 2000 Intel Corp.
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/cyclone.h>


#define PREFIX			"ACPI: "

asm (".weak iosapic_register_intr");
asm (".weak iosapic_override_isa_irq");
asm (".weak iosapic_register_platform_intr");
asm (".weak iosapic_init");
asm (".weak iosapic_version");

void (*pm_idle) (void);
void (*pm_power_off) (void);

unsigned char acpi_kbd_controller_present = 1;

int acpi_disabled = 0;

const char *
acpi_get_sysname (void)
{
#ifdef CONFIG_IA64_GENERIC
	unsigned long rsdp_phys;
	struct acpi20_table_rsdp *rsdp;
	struct acpi_table_xsdt *xsdt;
	struct acpi_table_header *hdr;

	rsdp_phys = acpi_find_rsdp();
	if (!rsdp_phys) {
		printk("ACPI 2.0 RSDP not found, default to \"dig\"\n");
		return "dig";
	}

	rsdp = (struct acpi20_table_rsdp *) __va(rsdp_phys);
	if (strncmp(rsdp->signature, RSDP_SIG, sizeof(RSDP_SIG) - 1)) {
		printk("ACPI 2.0 RSDP signature incorrect, default to \"dig\"\n");
		return "dig";
	}

	xsdt = (struct acpi_table_xsdt *) __va(rsdp->xsdt_address);
	hdr = &xsdt->header;
	if (strncmp(hdr->signature, XSDT_SIG, sizeof(XSDT_SIG) - 1)) {
		printk("ACPI 2.0 XSDT signature incorrect, default to \"dig\"\n");
		return "dig";
	}

	if (!strcmp(hdr->oem_id, "HP")) {
		return "hpzx1";
	}

	return "dig";
#else
# if defined (CONFIG_IA64_HP_SIM)
	return "hpsim";
# elif defined (CONFIG_IA64_HP_ZX1)
	return "hpzx1";
# elif defined (CONFIG_IA64_SGI_SN1)
	return "sn1";
# elif defined (CONFIG_IA64_SGI_SN2)
	return "sn2";
# elif defined (CONFIG_IA64_DIG)
	return "dig";
# else
#	error Unknown platform.  Fix acpi.c.
# endif
#endif
}

#ifdef CONFIG_ACPI

static struct acpi_resource *
acpi_get_crs_next (struct acpi_buffer *buf, int *offset)
{
	struct acpi_resource *res;

	if (*offset >= buf->length)
		return NULL;

	res = (struct acpi_resource *)((char *) buf->pointer + *offset);
	*offset += res->length;
	return res;
}

static union acpi_resource_data *
acpi_get_crs_type (struct acpi_buffer *buf, int *offset, int type)
{
	for (;;) {
		struct acpi_resource *res = acpi_get_crs_next(buf, offset);
		if (!res)
			return NULL;
		if (res->id == type)
			return &res->data;
	}
}

static void
acpi_get_crs_addr (struct acpi_buffer *buf, int type, u64 *base, u64 *length, u64 *tra)
{
	int offset = 0;
	struct acpi_resource_address16 *addr16;
	struct acpi_resource_address32 *addr32;
	struct acpi_resource_address64 *addr64;

	for (;;) {
		struct acpi_resource *res = acpi_get_crs_next(buf, &offset);
		if (!res)
			return;
		switch (res->id) {
			case ACPI_RSTYPE_ADDRESS16:
				addr16 = (struct acpi_resource_address16 *) &res->data;

				if (type == addr16->resource_type) {
					*base = addr16->min_address_range;
					*length = addr16->address_length;
					*tra = addr16->address_translation_offset;
					return;
				}
				break;
			case ACPI_RSTYPE_ADDRESS32:
				addr32 = (struct acpi_resource_address32 *) &res->data;
				if (type == addr32->resource_type) {
					*base = addr32->min_address_range;
					*length = addr32->address_length;
					*tra = addr32->address_translation_offset;
					return;
				}
				break;
			case ACPI_RSTYPE_ADDRESS64:
				addr64 = (struct acpi_resource_address64 *) &res->data;
				if (type == addr64->resource_type) {
					*base = addr64->min_address_range;
					*length = addr64->address_length;
					*tra = addr64->address_translation_offset;
					return;
				}
				break;
			case ACPI_RSTYPE_END_TAG:
				return;
				break;
		}
	}
}

acpi_status
acpi_get_addr_space(acpi_handle obj, u8 type, u64 *base, u64 *length, u64 *tra)
{
	acpi_status status;
	struct acpi_buffer buf = { .length  = ACPI_ALLOCATE_BUFFER,
			    .pointer = NULL };

	*base = 0;
	*length = 0;
	*tra = 0;

	status = acpi_get_current_resources(obj, &buf);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to get _CRS data on object\n");
		return status;
	}

	acpi_get_crs_addr(&buf, type, base, length, tra);

	acpi_os_free(buf.pointer);

	return AE_OK;
}

typedef struct {
	u8	guid_id;
	u8	guid[16];
	u8	csr_base[8];
	u8	csr_length[8];
} acpi_hp_vendor_long;

#define HP_CCSR_LENGTH 0x21
#define HP_CCSR_TYPE 0x2
#define HP_CCSR_GUID EFI_GUID(0x69e9adf9, 0x924f, 0xab5f, \
			      0xf6, 0x4a, 0x24, 0xd2, 0x01, 0x37, 0x0e, 0xad)

acpi_status
acpi_hp_csr_space(acpi_handle obj, u64 *csr_base, u64 *csr_length)
{
	int i, offset = 0;
	acpi_status status;
	struct acpi_buffer buf = { .length  = ACPI_ALLOCATE_BUFFER,
			    .pointer = NULL };
	struct acpi_resource_vendor *res;
	acpi_hp_vendor_long *hp_res;
	efi_guid_t vendor_guid;

	*csr_base = 0;
	*csr_length = 0;

	status = acpi_get_current_resources(obj, &buf);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Unable to get _CRS data on object\n");
		return status;
	}

	status = AE_NOT_FOUND;
	res = (struct acpi_resource_vendor *)acpi_get_crs_type(&buf, &offset, ACPI_RSTYPE_VENDOR);
	if (!res) {
		printk(KERN_ERR PREFIX "Failed to find config space for device\n");
		goto out;
	}

	status = AE_TYPE; /* Revisit error? */
	hp_res = (acpi_hp_vendor_long *)(res->reserved);

	if (res->length != HP_CCSR_LENGTH || hp_res->guid_id != HP_CCSR_TYPE) {
		printk(KERN_ERR PREFIX "Unknown Vendor data\n");
		goto out;
	}

	memcpy(&vendor_guid, hp_res->guid, sizeof(efi_guid_t));
	if (efi_guidcmp(vendor_guid, HP_CCSR_GUID) != 0) {
		printk(KERN_ERR PREFIX "Vendor GUID does not match\n");
		goto out;
	}

	/* It's probably unaligned, so use memcpy */
	memcpy(csr_base, hp_res->csr_base, 8);
	memcpy(csr_length, hp_res->csr_length, 8);
	status = AE_OK;

 out:
	acpi_os_free(buf.pointer);
	return status;
}

/* Hook from generic ACPI tables.c */
void __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
		 if (!strncmp(oem_id, "IBM", 3) &&
		     (!strncmp(oem_table_id, "SERMOW", 6))){
		 		 /*Start cyclone clock*/
		 		 cyclone_setup(0);
		 }
}

#endif /* CONFIG_ACPI */

#ifdef CONFIG_ACPI_BOOT

#define ACPI_MAX_PLATFORM_INTERRUPTS	256

/* Array to record platform interrupt vectors for generic interrupt routing. */
int platform_intr_list[ACPI_MAX_PLATFORM_INTERRUPTS] = { [0 ... ACPI_MAX_PLATFORM_INTERRUPTS - 1] = -1 };

enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_IOSAPIC;

/*
 * Interrupt routing API for device drivers.  Provides interrupt vector for
 * a generic platform event.  Currently only CPEI is implemented.
 */
int
acpi_request_vector (u32 int_type)
{
	int vector = -1;

	if (int_type < ACPI_MAX_PLATFORM_INTERRUPTS) {
		/* correctable platform error interrupt */
		vector = platform_intr_list[int_type];
	} else
		printk("acpi_request_vector(): invalid interrupt type\n");

	return vector;
}

char *
__acpi_map_table (unsigned long phys_addr, unsigned long size)
{
	return __va(phys_addr);
}

/* --------------------------------------------------------------------------
                            Boot-time Table Parsing
   -------------------------------------------------------------------------- */

static int			total_cpus __initdata;
static int			available_cpus __initdata;
struct acpi_table_madt *	acpi_madt __initdata;
static u8			has_8259;


static int __init
acpi_parse_lapic_addr_ovr (acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic;

	lapic = (struct acpi_table_lapic_addr_ovr *) header;
	if (!lapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic->address) {
		iounmap((void *) ipi_base_addr);
		ipi_base_addr = (unsigned long) ioremap(lapic->address, 0);
	}

	return 0;
}


static int __init
acpi_parse_lsapic (acpi_table_entry_header *header)
{
	struct acpi_table_lsapic *lsapic;
	int phys_id;

	lsapic = (struct acpi_table_lsapic *) header;
	if (!lsapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	phys_id = (lsapic->id << 8) | lsapic->eid;

	if (total_cpus == NR_CPUS) {
		printk(KERN_ERR PREFIX "Ignoring CPU (0x%04x) (NR_CPUS == %d)\n",
			phys_id, NR_CPUS);
		return 0;
	}

	printk("CPU %d (0x%04x)", total_cpus, phys_id);

	if (lsapic->flags.enabled) {
		available_cpus++;
		printk(" enabled");
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = phys_id;
		if (hard_smp_processor_id() == smp_boot_data.cpu_phys_id[total_cpus])
			printk(" (BSP)");
#endif
	}
	else {
		printk(" disabled");
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = -1;
#endif
	}

	printk("\n");

	total_cpus++;
	return 0;
}


static int __init
acpi_parse_lapic_nmi (acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lacpi_nmi;

	lacpi_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lacpi_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support lapic_nmi entries */

	return 0;
}


static int __init
acpi_find_iosapic (unsigned int gsi, u32 *gsi_base, char **iosapic_address)
{
	struct acpi_table_iosapic *iosapic;
	int ver;
	int max_pin;
	char *p;
	char *end;

	if (!gsi_base || !iosapic_address)
		return -ENODEV;

	p = (char *) (acpi_madt + 1);
	end = p + (acpi_madt->header.length - sizeof(struct acpi_table_madt));

	while (p < end) {
		if (*p == ACPI_MADT_IOSAPIC) {
			iosapic = (struct acpi_table_iosapic *) p;

			*gsi_base = iosapic->global_irq_base;
			*iosapic_address = ioremap(iosapic->address, 0);

			ver = iosapic_version(*iosapic_address);
			max_pin = (ver >> 16) & 0xff;

			if ((gsi - *gsi_base) <= max_pin)
				return 0;	/* Found it! */
		}
		p += p[1];
	}
	return -ENODEV;
}


static int __init
acpi_parse_iosapic (acpi_table_entry_header *header)
{
	struct acpi_table_iosapic *iosapic;

	iosapic = (struct acpi_table_iosapic *) header;
	if (!iosapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (iosapic_init) {
#ifndef CONFIG_ITANIUM
		iosapic_init(iosapic->address, iosapic->global_irq_base,
			     has_8259);
#else
		/* Firmware on old Itanium systems is broken */
		iosapic_init(iosapic->address, iosapic->global_irq_base, 1);
#endif
	}
	return 0;
}


static int __init
acpi_parse_plat_int_src (acpi_table_entry_header *header)
{
	struct acpi_table_plat_int_src *plintsrc;
	int vector;
	u32 gsi_base;
	char *iosapic_address;

	plintsrc = (struct acpi_table_plat_int_src *) header;
	if (!plintsrc)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (!iosapic_register_platform_intr) {
		printk(KERN_WARNING PREFIX "No ACPI platform interrupt support\n");
		return -ENODEV;
	}

	if (acpi_find_iosapic(plintsrc->global_irq, &gsi_base, &iosapic_address)) {
		printk(KERN_WARNING PREFIX "IOSAPIC not found\n");
		return -ENODEV;
	}

	/*
	 * Get vector assignment for this interrupt, set attributes,
	 * and program the IOSAPIC routing table.
	 */
	vector = iosapic_register_platform_intr(plintsrc->type,
						plintsrc->global_irq,
						plintsrc->iosapic_vector,
						plintsrc->eid,
						plintsrc->id,
						(plintsrc->flags.polarity == 1) ? 1 : 0,
						(plintsrc->flags.trigger == 1) ? 1 : 0,
						gsi_base,
						iosapic_address);

	platform_intr_list[plintsrc->type] = vector;
	return 0;
}


static int __init
acpi_parse_int_src_ovr (acpi_table_entry_header *header)
{
	struct acpi_table_int_src_ovr *p;

	p = (struct acpi_table_int_src_ovr *) header;
	if (!p)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* Ignore if the platform doesn't support overrides */
	if (!iosapic_override_isa_irq)
		return 0;

	iosapic_override_isa_irq(p->bus_irq, p->global_irq,
				 (p->flags.polarity == 1) ? 1 : 0,
				 (p->flags.trigger == 1) ? 1 : 0);

	return 0;
}


static int __init
acpi_parse_nmi_src (acpi_table_entry_header *header)
{
	struct acpi_table_nmi_src *nmi_src;

	nmi_src = (struct acpi_table_nmi_src*) header;
	if (!nmi_src)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries */

	return 0;
}


static int __init
acpi_parse_madt (unsigned long phys_addr, unsigned long size)
{
	if (!phys_addr || !size)
		return -EINVAL;

	acpi_madt = (struct acpi_table_madt *) __va(phys_addr);

	/* remember the value for reference after free_initmem() */
	has_8259 = acpi_madt->flags.pcat_compat;

	/* Get base address of IPI Message Block */

	if (acpi_madt->lapic_address)
		ipi_base_addr = (unsigned long) ioremap(acpi_madt->lapic_address, 0);

	printk(KERN_INFO PREFIX "Local APIC address 0x%lx\n", ipi_base_addr);

	acpi_madt_oem_check(acpi_madt->header.oem_id,
			acpi_madt->header.oem_table_id);
	return 0;
}


static int __init
acpi_parse_fadt (unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_header *fadt_header;
	struct fadt_descriptor_rev2 *fadt;
	u32 sci_irq, gsi_base;
	char *iosapic_address;

	if (!phys_addr || !size)
		return -EINVAL;

	fadt_header = (struct acpi_table_header *) __va(phys_addr);

	if (fadt_header->revision != 3)
		return -ENODEV;		/* Only deal with ACPI 2.0 FADT */

	fadt = (struct fadt_descriptor_rev2 *) fadt_header;

	if (!(fadt->iapc_boot_arch & BAF_8042_KEYBOARD_CONTROLLER))
		acpi_kbd_controller_present = 0;

	sci_irq = fadt->sci_int;

	if (has_8259 && sci_irq < 16)
		return 0;	/* legacy, no setup required */

	if (!iosapic_register_intr)
		return -ENODEV;

	if (!acpi_find_iosapic(sci_irq, &gsi_base, &iosapic_address))
		iosapic_register_intr(sci_irq, 0, 0, gsi_base, iosapic_address);

	return 0;
}


unsigned long __init
acpi_find_rsdp (void)
{
	unsigned long rsdp_phys = 0;

	if (efi.acpi20)
		rsdp_phys = __pa(efi.acpi20);
	else if (efi.acpi)
		printk(KERN_WARNING PREFIX "v1.0/r0.71 tables no longer supported\n");

	return rsdp_phys;
}


int __init
acpi_boot_init (char *cmdline)
{
	int result;

	/* Initialize the ACPI boot-time table parser */
	result = acpi_table_init();
	if (result)
		return result;

	/*
	 * MADT
	 * ----
	 * Parse the Multiple APIC Description Table (MADT), if exists.
	 * Note that this table provides platform SMP configuration
	 * information -- the successor to MPS tables.
	 */

	if (acpi_table_parse(ACPI_APIC, acpi_parse_madt) < 1) {
		printk(KERN_ERR PREFIX "Can't find MADT\n");
		goto skip_madt;
	}

	/* Local APIC */

	if (acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR,
				  acpi_parse_lapic_addr_ovr) < 0)
		printk(KERN_ERR PREFIX "Error parsing LAPIC address override entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_LSAPIC,
				  acpi_parse_lsapic) < 1)
		printk(KERN_ERR PREFIX "Error parsing MADT - no LAPIC entries\n");

	if (acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI,
				  acpi_parse_lapic_nmi) < 0)
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");

	/* I/O APIC */

	if (acpi_table_parse_madt(ACPI_MADT_IOSAPIC,
				  acpi_parse_iosapic) < 1)
		printk(KERN_ERR PREFIX "Error parsing MADT - no IOAPIC entries\n");

	/* System-Level Interrupt Routing */

	if (acpi_table_parse_madt(ACPI_MADT_PLAT_INT_SRC,
				  acpi_parse_plat_int_src) < 0)
		printk(KERN_ERR PREFIX "Error parsing platform interrupt source entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR,
				  acpi_parse_int_src_ovr) < 0)
		printk(KERN_ERR PREFIX "Error parsing interrupt source overrides entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_NMI_SRC,
				  acpi_parse_nmi_src) < 0)
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
skip_madt:

	/*
	 * The FADT table contains an SCI_INT line, by which the system
	 * gets interrupts such as power and sleep buttons.  If it's not
	 * on a Legacy interrupt, it needs to be setup.
	 */
	if (acpi_table_parse(ACPI_FADT, acpi_parse_fadt) < 1)
		printk(KERN_ERR PREFIX "Can't find FADT\n");

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = total_cpus;
#endif
	/* Make boot-up look pretty */
	printk("%d CPUs available, %d CPUs total\n", available_cpus, total_cpus);
	return 0;
}


/* --------------------------------------------------------------------------
                             PCI Interrupt Routing
   -------------------------------------------------------------------------- */

int __init
acpi_get_prt (struct pci_vector_struct **vectors, int *count)
{
	struct pci_vector_struct *vector;
	struct list_head *node;
	struct acpi_prt_entry *entry;
	int i = 0;

	if (!vectors || !count)
		return -EINVAL;

	*vectors = NULL;
	*count = 0;

	if (acpi_prt.count < 0) {
		printk(KERN_ERR PREFIX "No PCI interrupt routing entries\n");
		return -ENODEV;
	}

	/* Allocate vectors */

	*vectors = kmalloc(sizeof(struct pci_vector_struct) * acpi_prt.count, GFP_KERNEL);
	if (!(*vectors))
		return -ENOMEM;

	/* Convert PRT entries to IOSAPIC PCI vectors */

	vector = *vectors;

	/* 
	 * Make sure all devices described by PCI link device entries
	 * have valid IRQs 
	 */
	if (acpi_pci_link_check()) {
		printk(KERN_ERR PREFIX "Unable to set PCI link device IRQs\n");
		return -ENODEV;
	}

	list_for_each(node, &acpi_prt.entries) {
		entry = (struct acpi_prt_entry *)node;
		vector[i].segment = entry->id.segment;
		vector[i].bus    = entry->id.bus;
		vector[i].pci_id = ((u32) entry->id.device << 16) | 0xffff;
		vector[i].pin    = entry->pin;
		if (!entry->irq && entry->link.handle)
			vector[i].irq = acpi_pci_link_get_irq(entry->link.handle, entry->link.index, NULL, NULL);
		else
			vector[i].irq = entry->link.index;
		i++;
	}
	*count = acpi_prt.count;
	return 0;
}

int
acpi_get_pci_link_irq_params(struct pci_dev *dev, unsigned char pin, int *trig, int *pol)
{
	struct list_head *node;
	struct acpi_prt_entry *entry;

	list_for_each(node, &acpi_prt.entries) {
		entry = (struct acpi_prt_entry *)node;
		
		if ((entry->id.segment == PCI_SEGMENT(dev)) &&
		    (entry->id.bus == dev->bus->number) &&
		    (entry->id.device == PCI_SLOT(dev->devfn)) &&
		    (entry->pin == pin) &&
		    (!entry->irq && entry->link.handle)) {
			acpi_pci_link_get_irq(entry->link.handle, entry->link.index, trig, pol);
			return 1;
		}
	}
	return 0;
}

/* Assume IA64 always use I/O SAPIC */

int __init
acpi_get_interrupt_model (int *type)
{
        if (!type)
                return -EINVAL;

	*type = ACPI_IRQ_MODEL_IOSAPIC;
        return 0;
}

int
acpi_irq_to_vector (u32 irq)
{
	if (has_8259 && irq < 16)
		return isa_irq_to_vector(irq);

	return gsi_to_vector(irq);
}

int
acpi_register_irq (u32 gsi, u32 polarity, u32 mode)
{
	u32 irq_base;
	char *iosapic_address;

	if (has_8259 && gsi < 16)
		return isa_irq_to_vector(gsi);

	if (!iosapic_register_intr)
		return 0;

	if (!acpi_find_iosapic(gsi, &irq_base, &iosapic_address))
		return iosapic_register_intr(gsi,
				polarity == ACPI_ACTIVE_HIGH,
				mode == ACPI_EDGE_SENSITIVE,
				irq_base, iosapic_address);

	return 0;
}

#endif /* CONFIG_ACPI_BOOT */
