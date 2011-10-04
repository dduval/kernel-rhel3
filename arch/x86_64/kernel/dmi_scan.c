
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
//#include <asm/io.h>
#include <linux/pm.h>
#include <asm/keyboard.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <linux/bootmem.h>

#include "pci-x86_64.h"

unsigned long dmi_broken;

struct dmi_header
{
	u8	type;
	u8	length;
	u16	handle;
};

//#define dmi_printk(x)
#define dmi_printk(x) printk x

static char * __init dmi_string(struct dmi_header *dm, u8 s)
{
	u8 *bp=(u8 *)dm;
	bp+=dm->length;
	if(!s)
		return "";
	s--;
	while(s>0 && *bp)
	{
		bp+=strlen(bp);
		bp++;
		s--;
	}
	return bp;
}

/*
 *	We have to be cautious here. We have seen BIOSes with DMI pointers
 *	pointing to completely the wrong place for example
 */
 
static int __init dmi_table(u32 base, int len, int num, void (*decode)(struct dmi_header *))
{
	u8 *buf;
	struct dmi_header *dm;
	u8 *data;
	int i=0;
		
	/* Since pretty much everything is identity-mapped on x86_64,
	 * we shouldn't need to do an ioremap here.  phys_to_virt
	 * should suffice.
	 */
	buf = phys_to_virt(base);

	data = buf;

	/*
 	 *	Stop when we see all the items the table claimed to have
 	 *	OR we run off the end of the table (also happens)
 	 */
 
	while(i<num && data-buf+sizeof(struct dmi_header)<=len)
	{
		dm=(struct dmi_header *)data;
		/*
		 *  We want to know the total length (formated area and strings)
		 *  before decoding to make sure we won't run off the table in
		 *  dmi_decode or dmi_string
		 */
		data+=dm->length;
		while(data-buf<len-1 && (data[0] || data[1]))
			data++;
		if(data-buf<len-1)
			decode(dm);
		data+=2;
		i++;
	}


	return 0;
}


inline static int __init dmi_checksum(u8 *buf)
{
	u8 sum=0;
	int a;
	
	for(a=0; a<15; a++)
		sum+=buf[a];
	return (sum==0);
}

static int __init dmi_iterate(void (*decode)(struct dmi_header *))
{
	u8 buf[15];
	u32 fp=0xF0000;

	while( fp < 0xFFFFF)
	{
		isa_memcpy_fromio(buf, fp, 15);
		if(memcmp(buf, "_DMI_", 5)==0 && dmi_checksum(buf))
		{
			u16 num=buf[13]<<8|buf[12];
			u16 len=buf[7]<<8|buf[6];
			u32 base=buf[11]<<24|buf[10]<<16|buf[9]<<8|buf[8];

			/*
			 * DMI version 0.0 means that the real version is taken from
			 * the SMBIOS version, which we don't know at this point.
			 */
			if(buf[14]!=0)
				dmi_printk((KERN_INFO "DMI %d.%d present.\n",
					buf[14]>>4, buf[14]&0x0F));
			else
				dmi_printk((KERN_INFO "DMI present.\n"));
			dmi_printk((KERN_INFO "%d structures occupying %d bytes.\n",
				num, len));
			dmi_printk((KERN_INFO "DMI table at 0x%08X.\n",
				base));
			if(dmi_table(base,len, num, decode)==0)
				return 0;
		}
		fp+=16;
	}
	return -1;
}


enum
{
	DMI_BIOS_VENDOR,
	DMI_BIOS_VERSION,
	DMI_BIOS_DATE,
	DMI_SYS_VENDOR,
	DMI_PRODUCT_NAME,
	DMI_PRODUCT_VERSION,
	DMI_BOARD_VENDOR,
	DMI_BOARD_NAME,
	DMI_BOARD_VERSION,
	DMI_STRING_MAX
};

static char *dmi_ident[DMI_STRING_MAX];

/*
 *	Save a DMI string
 */
 
static void __init dmi_save_ident(struct dmi_header *dm, int slot, int string)
{
	char *d = (char*)dm;
	char *p = dmi_string(dm, d[string]);
	if(p==NULL || *p == 0)
		return;
	if (dmi_ident[slot])
		return;
	dmi_ident[slot] = alloc_bootmem(strlen(p)+1);
	if(dmi_ident[slot])
		strcpy(dmi_ident[slot], p);
	else
		printk(KERN_ERR "dmi_save_ident: out of memory.\n");
}

/*
 *	DMI callbacks for problem boards
 */

struct dmi_strmatch
{
	u8 slot;
	char *substr;
};

#define NONE	255

struct dmi_blacklist
{
	int (*callback)(struct dmi_blacklist *);
	char *ident;
	struct dmi_strmatch matches[4];
};

#define NO_MATCH	{ NONE, NULL}
#define MATCH(a,b)	{ a, b }

/*
 *	Simple "print if true" callback
 */
 
static __init int print_if_true(struct dmi_blacklist *d)
{
	printk("%s\n", d->ident);
	return 0;
}

/*
 * IBM bladeservers have a USB console switch. The keyboard type is USB
 * and the hardware does not have a console keyboard. We disable the
 * console keyboard so the kernel does not try to initialize one and
 * spew errors. This can be used for all systems without a console
 * keyboard like systems with just a USB or IrDA keyboard.
 */
static __init int disable_console_keyboard(struct dmi_blacklist *d)
{
 	extern int keyboard_controller_present;
	printk(KERN_INFO "*** Hardware has no console keyboard controller.\n");
	printk(KERN_INFO "*** Disabling console keyboard.\n");
 	keyboard_controller_present = 0;
 	return 0;
}


/*
 * The RHEL3 IO APIC initialization does not work correctly on IBM eServer
 * xSeries 260 (also called x3800), 366 (x3850) and 460 (x3950). Therefore
 * disable it.
 *
 * Also, if this is a 64-bit Intel kernel (IA32E) and running on the above
 * mentioned machines, and nmi_watchdog is set to use IO APIC initialization,
 * then set the nmi_watchdog to use Local APIC instead. This is required since
 * the IO APIC intialization is disabled by this function.
 */
static __init int force_acpi_noirq_nmi_probing(struct dmi_blacklist *d)
{
	extern int acpi_noirq;
	printk(KERN_INFO "%s detected. Disabling ACPI IO initialization.\n",
		d->ident);
	acpi_noirq = 1;
#ifdef CONFIG_IA32E
#ifdef CONFIG_X86_LOCAL_APIC
	if (nmi_watchdog == NMI_IO_APIC)
		nmi_watchdog = NMI_LOCAL_APIC;
#endif
#endif
	return 0;
}

/*
 *	Process the DMI blacklists
 */
 

/*
 *	This will be expanded over time to force things like the APM 
 *	interrupt mask settings according to the laptop
 */
 
static __initdata struct dmi_blacklist dmi_blacklist[] = {

	/* IBM Bladeservers */
	{ disable_console_keyboard, "IBM Server Blade", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_BOARD_NAME, "Server Blade"),
			NO_MATCH, NO_MATCH
			} },
	/* IBM eSeries xServer 266, 366, and 460 */
	{ force_acpi_noirq_nmi_probing, "IBM eServer xSeries 260", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "eserver xSeries 260"),
			NO_MATCH, NO_MATCH
			} },
	{ force_acpi_noirq_nmi_probing, "IBM eServer xSeries 366", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "eserver xSeries 366"),
			NO_MATCH, NO_MATCH
			} },
	{ force_acpi_noirq_nmi_probing, "IBM eServer xSeries 460", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "eserver xSeries 460"),
			NO_MATCH, NO_MATCH
			} },
	/* formerly known as x460 */
	{ force_acpi_noirq_nmi_probing, "IBM x3950", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "IBM x3950"),
			NO_MATCH, NO_MATCH
			} },
	/* formerly known as x366 */
	{ force_acpi_noirq_nmi_probing, "IBM x3850", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "IBM x3850"),
			NO_MATCH, NO_MATCH
			} },
	/* formerly known as x266 */
	{ force_acpi_noirq_nmi_probing, "IBM x3800", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_PRODUCT_NAME, "IBM x3800"),
			NO_MATCH, NO_MATCH
			} },

	{ NULL, }
};
	
	
/*
 *	Walk the blacklist table running matching functions until someone 
 *	returns 1 or we hit the end.
 */
 
static __init void dmi_check_blacklist(void)
{
	struct dmi_blacklist *d;
	int i;
		
	d=&dmi_blacklist[0];
	while(d->callback)
	{
		for(i=0;i<4;i++)
		{
			int s = d->matches[i].slot;
			if(s==NONE)
				continue;
			if(dmi_ident[s] && strstr(dmi_ident[s], d->matches[i].substr))
				continue;
			/* No match */
			goto fail;
		}
		if(d->callback(d))
			return;
fail:			
		d++;
	}
}

	

/*
 *	Process a DMI table entry. Right now all we care about are the BIOS
 *	and machine entries. For 2.5 we should pull the smbus controller info
 *	out of here.
 */

static void __init dmi_decode(struct dmi_header *dm)
{
	u8 *data = (u8 *)dm;
	
	switch(dm->type)
	{
		case  0:
			dmi_printk(("BIOS Vendor: %s\n",
				dmi_string(dm, data[4])));
			dmi_save_ident(dm, DMI_BIOS_VENDOR, 4);
			dmi_printk(("BIOS Version: %s\n", 
				dmi_string(dm, data[5])));
			dmi_save_ident(dm, DMI_BIOS_VERSION, 5);
			dmi_printk(("BIOS Release: %s\n",
				dmi_string(dm, data[8])));
			dmi_save_ident(dm, DMI_BIOS_DATE, 8);
			break;
		case 1:
			dmi_printk(("System Vendor: %s\n",
				dmi_string(dm, data[4])));
			dmi_save_ident(dm, DMI_SYS_VENDOR, 4);
			dmi_printk(("Product Name: %s\n",
				dmi_string(dm, data[5])));
			dmi_save_ident(dm, DMI_PRODUCT_NAME, 5);
			dmi_printk(("Version: %s\n",
				dmi_string(dm, data[6])));
			dmi_save_ident(dm, DMI_PRODUCT_VERSION, 6);
			dmi_printk(("Serial Number: %s\n",
				dmi_string(dm, data[7])));
			break;
		case 2:
			dmi_printk(("Board Vendor: %s\n",
				dmi_string(dm, data[4])));
			dmi_save_ident(dm, DMI_BOARD_VENDOR, 4);
			dmi_printk(("Board Name: %s\n",
				dmi_string(dm, data[5])));
			dmi_save_ident(dm, DMI_BOARD_NAME, 5);
			dmi_printk(("Board Version: %s\n",
				dmi_string(dm, data[6])));
			dmi_save_ident(dm, DMI_BOARD_VERSION, 6);
			break;
	}
}

void __init dmi_scan_machine(void)
{
	int err = dmi_iterate(dmi_decode);
	if(err == 0)
		dmi_check_blacklist();
}
