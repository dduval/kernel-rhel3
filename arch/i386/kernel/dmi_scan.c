#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/apm_bios.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/pm.h>
#include <asm/keyboard.h>
#include <asm/system.h>
#include <linux/bootmem.h>

#include "pci-i386.h"

unsigned long dmi_broken;
int is_sony_vaio_laptop;

struct dmi_header
{
	u8	type;
	u8	length;
	u16	handle;
};

#define dmi_printk(x)
//#define dmi_printk(x) printk x

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
		
	buf = bt_ioremap(base, len);
	if(buf==NULL)
		return -1;

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
	bt_iounmap(buf, len);
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
 *	We have problems with IDE DMA on some platforms. In paticular the
 *	KT7 series. On these it seems the newer BIOS has fixed them. The
 *	rule needs to be improved to match specific BIOS revisions with
 *	corruption problems
 
static __init int disable_ide_dma(struct dmi_blacklist *d)
{
#ifdef CONFIG_BLK_DEV_IDE
	extern int noautodma;
	if(noautodma == 0)
	{
		noautodma = 1;
		printk(KERN_INFO "%s series board detected. Disabling IDE DMA.\n", d->ident);
	}
#endif	
	return 0;
}
 */ 

/* 
 * Reboot options and system auto-detection code provided by
 * Dell Computer Corporation so their systems "just work". :-)
 */

/* 
 * Some machines require the "reboot=b"  commandline option, this quirk makes that automatic.
 */
static __init int set_bios_reboot(struct dmi_blacklist *d)
{
	extern int reboot_thru_bios;
	if (reboot_thru_bios == 0)
	{
		reboot_thru_bios = 1;
		printk(KERN_INFO "%s series board detected. Selecting BIOS-method for reboots.\n", d->ident);
	}
	return 0;
}

/*
 * Some machines require the "reboot=s"  commandline option, this quirk makes that automatic.
 */
static __init int set_smp_reboot(struct dmi_blacklist *d)
{
#ifdef CONFIG_SMP
	extern int reboot_smp;
	if (reboot_smp == 0)
	{
		reboot_smp = 1;
		printk(KERN_INFO "%s series board detected. Selecting SMP-method for reboots.\n", d->ident);
	}
#endif
	return 0;
}

/*
 * Some machines require the "reboot=b,s"  commandline option, this quirk makes that automatic.
 */
static __init int set_smp_bios_reboot(struct dmi_blacklist *d)
{
	set_smp_reboot(d);
	set_bios_reboot(d);
	return 0;
}

/*
 * Some bioses have a broken protected mode poweroff and need to use realmode
 */

static __init int set_realmode_power_off(struct dmi_blacklist *d)
{
       if (apm_info.realmode_power_off == 0)
       {
               apm_info.realmode_power_off = 1;
               printk(KERN_INFO "%s bios detected. Using realmode poweroff only.\n", d->ident);
       }
       return 0;
}


/* 
 * Some laptops require interrupts to be enabled during APM calls 
 */

static __init int set_apm_ints(struct dmi_blacklist *d)
{
	if (apm_info.allow_ints == 0)
	{
		apm_info.allow_ints = 1;
		printk(KERN_INFO "%s machine detected. Enabling interrupts during APM calls.\n", d->ident);
	}
	return 0;
}

/* 
 * Some APM bioses corrupt memory or just plain do not work
 */

static __init int apm_is_horked(struct dmi_blacklist *d)
{
	if (apm_info.disabled == 0)
	{
		apm_info.disabled = 1;
		printk(KERN_INFO "%s machine detected. Disabling APM.\n", d->ident);
	}
	return 0;
}

static __init int apm_is_horked_d850md(struct dmi_blacklist *d)
{
	if (apm_info.disabled == 0)
	{
		apm_info.disabled = 1;
		printk(KERN_ERR "%s machine detected. Disabling APM.\n", d->ident);
		printk(KERN_ERR "This bug is fixed in bios P15 which is available for \n");
		printk(KERN_ERR "download from support.intel.com \n");
	}
	return 0;
}

/* 
 * Some APM bioses hang on APM idle calls
 */

static __init int apm_likes_to_melt(struct dmi_blacklist *d)
{
	if (apm_info.forbid_idle == 0)
	{
		apm_info.forbid_idle = 1;
		printk(KERN_INFO "%s machine detected. Disabling APM idle calls.\n", d->ident);
	}
	return 0;
}

/*
 * Some machines, usually laptops, can't handle an enabled local APIC.
 * The symptoms include hangs or reboots when suspending or resuming,
 * attaching or detaching the power cord, or entering BIOS setup screens
 * through magic key sequences.
 */
static int __init local_apic_kills_bios(struct dmi_blacklist *d)
{
#ifdef CONFIG_X86_LOCAL_APIC
	extern int dont_enable_local_apic;
	if (!dont_enable_local_apic) {
		dont_enable_local_apic = 1;
		printk(KERN_WARNING "%s with broken BIOS detected. "
		       "Refusing to enable the local APIC.\n",
		       d->ident);
	}
#endif
	return 0;
}

/*
 * The Microstar 6163-2 (a.k.a Pro) mainboard will hang shortly after
 * resumes, and also at what appears to be asynchronous APM events,
 * if the local APIC is enabled.
 */
static int __init apm_kills_local_apic(struct dmi_blacklist *d)
{
#ifdef CONFIG_X86_LOCAL_APIC
	extern int dont_enable_local_apic;
	if (apm_info.bios.version && !dont_enable_local_apic) {
		dont_enable_local_apic = 1;
		printk(KERN_WARNING "%s with broken BIOS detected. "
		       "Refusing to enable the local APIC.\n",
		       d->ident);
	}
#endif
	return 0;
}

/*
 * The Intel AL440LX mainboard will hang randomly if the local APIC
 * timer is running and the APM BIOS hasn't been disabled.
 */
static int __init apm_kills_local_apic_timer(struct dmi_blacklist *d)
{
#ifdef CONFIG_X86_LOCAL_APIC
	extern int dont_use_local_apic_timer;
	if (apm_info.bios.version && !dont_use_local_apic_timer) {
		dont_use_local_apic_timer = 1;
		printk(KERN_WARNING "%s with broken BIOS detected. "
		       "The local APIC timer will not be used.\n",
		       d->ident);
	}
#endif
	return 0;
}

/*
 *  Check for clue free BIOS implementations who use
 *  the following QA technique
 *
 *      [ Write BIOS Code ]<------
 *               |                ^
 *      < Does it Compile >----N--
 *               |Y               ^
 *	< Does it Boot Win98 >-N--
 *               |Y
 *           [Ship It]
 *
 *	Phoenix A04  08/24/2000 is known bad (Dell Inspiron 5000e)
 *	Phoenix A07  09/29/2000 is known good (Dell Inspiron 5000)
 */

static __init int broken_apm_power(struct dmi_blacklist *d)
{
	apm_info.get_power_status_broken = 1;
	printk(KERN_WARNING "BIOS strings suggest APM bugs, disabling power status reporting.\n");
	return 0;
}		

/*
 * Check for a Sony Vaio system
 *
 * On a Sony system we want to enable the use of the sonypi
 * driver for Sony-specific goodies like the camera and jogdial.
 * We also want to avoid using certain functions of the PnP BIOS.
 */

static __init int sony_vaio_laptop(struct dmi_blacklist *d)
{
	if (is_sony_vaio_laptop == 0)
	{
		is_sony_vaio_laptop = 1;
		printk(KERN_INFO "%s laptop detected.\n", d->ident);
	}
	return 0;
}

/*
 * This bios swaps the APM minute reporting bytes over (Many sony laptops
 * have this problem).
 */
 
static __init int swab_apm_power_in_minutes(struct dmi_blacklist *d)
{
	apm_info.get_power_status_swabinminutes = 1;
	printk(KERN_WARNING "BIOS strings suggest APM reports battery life in minutes and wrong byte order.\n");
	return 0;
}

/*
 * The Intel 440GX hall of shame. 
 *
 * On many (all we have checked) of these boxes the $PIRQ table is wrong.
 * The MP1.4 table is right however and so SMP kernels tend to work. 
 */
 
extern int skip_ioapic_setup;
#ifdef CONFIG_PCI
extern int broken_440gx_bios;
extern unsigned int pci_probe;
#endif
static __init int broken_pirq(struct dmi_blacklist *d)
{
	printk(KERN_ERR " *** Possibly defective BIOS detected (irqtable)\n");
	printk(KERN_ERR " *** Many BIOSes matching this signature have incorrect IRQ routing tables.\n");
	printk(KERN_ERR " *** If you see IRQ problems, in particular SCSI resets and hangs at boot\n");
	printk(KERN_ERR " *** contact your hardware vendor and ask about updates.\n");
	printk(KERN_INFO " *** Building an SMP kernel may evade the bug some of the time.\n");
#ifdef CONFIG_X86_IO_APIC
	skip_ioapic_setup = 0;
#endif
#ifdef CONFIG_PCI
	broken_440gx_bios = 1;
	pci_probe |= PCI_BIOS_IRQ_SCAN;
#endif
	
	return 0;
}

/*
 * Toshiba fails to preserve interrupts over S1
 */

static __init int init_ints_after_s1(struct dmi_blacklist *d)
{
	printk(KERN_WARNING "Toshiba with broken S1 detected.\n");
	dmi_broken |= BROKEN_INIT_AFTER_S1;
	return 0;
}

/*
 * Some Bioses enable the PS/2 mouse (touchpad) at resume, even if it
 * was disabled before the suspend. Linux gets terribly confused by that.
 */

typedef void (pm_kbd_func) (void);

static __init int broken_ps2_resume(struct dmi_blacklist *d)
{
#ifdef CONFIG_VT
	if (pm_kbd_request_override == NULL)
	{
		pm_kbd_request_override = pckbd_pm_resume;
		printk(KERN_INFO "%s machine detected. Mousepad Resume Bug workaround enabled.\n", d->ident);
	}
#endif
	return 0;
}

/*
 * Work around broken HP Pavilion Notebooks which assign USB to
 * IRQ 9 even though it is actually wired to IRQ 11
 */
static __init int fix_broken_hp_bios_irq9(struct dmi_blacklist *d)
{
#ifdef CONFIG_PCI
	extern int broken_hp_bios_irq9;
	if (broken_hp_bios_irq9 == 0)
	{
		broken_hp_bios_irq9 = 1;
		printk(KERN_INFO "%s detected - fixing broken IRQ routing\n", d->ident);
	}
#endif
	return 0;
}

/*
 *	Exploding PnPBIOS. Don't yet know if its the BIOS or us for
 *	some entries
 */

static __init int exploding_pnp_bios(struct dmi_blacklist *d)
{
	printk(KERN_WARNING "%s detected. Disabling PnPBIOS\n", d->ident);
	dmi_broken |= BROKEN_PNP_BIOS;
	return 0;
}

/*
 *	Simple "print if true" callback
 */
 
static __init int print_if_true(struct dmi_blacklist *d)
{
	printk("%s\n", d->ident);
	return 0;
}

/*
 * Dell Inspiron 8000 APM BIOS fails to correctly save and restore the
 * config space of some PCI devices.
 */

static __init int broken_apm_pci_restore(struct dmi_blacklist *d)
{
#ifdef CONFIG_PCI
	extern int pci_bridge_force_restore;

	printk(KERN_WARNING "%s detected. Forcing restore of PCI configuration space on APM resume.\n", d->ident);
	pci_bridge_force_restore = 1;
#endif
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

/* Sun X4100 and X4200 servers need this to prevent PCI-X devices from being scanned twice */
/* PCI-X cards will not work in this machine otherwise */
static __init int pci_no_peer_tricks(struct dmi_blacklist *d)
{
 	extern int pcibios_no_peer_tricks;
	printk(KERN_INFO "Sun X4100/X4200 detected.. disabling pcibios_peer_tricks.\n");
 	pcibios_no_peer_tricks = 1;
 	return 0;
}
/*
 *	Process the DMI blacklists
 */
 

/*
 *	This will be expanded over time to force things like the APM 
 *	interrupt mask settings according to the laptop
 */
 
static __initdata struct dmi_blacklist dmi_blacklist[]={
#if 0
	{ disable_ide_dma, "KT7", {	/* Overbroad right now - kill DMA on problem KT7 boards */
			MATCH(DMI_PRODUCT_NAME, "KT7-RAID"),
			NO_MATCH, NO_MATCH, NO_MATCH
			} },
	{ disable_ide_dma, "Dell Inspiron 8100", {	/* Kill DMA on Dell Inspiron 8100 laptops */
			MATCH(DMI_PRODUCT_NAME, "Inspiron 8100"),
			MATCH(DMI_SYS_VENDOR,"Dell Computer Corporation"), NO_MATCH, NO_MATCH
			} },

#endif			
	/* Dell Laptop hall of shame */
	{ broken_ps2_resume, "Dell Latitude C600", {	/* Handle problems with APM on the C600 */
		        MATCH(DMI_SYS_VENDOR, "Dell"),
			MATCH(DMI_PRODUCT_NAME, "Latitude C600"),
			NO_MATCH, NO_MATCH
	                } },
	{ apm_is_horked, "Dell Inspiron 2500", { /* APM crashes */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron 2500"),
			MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION,"A11")
			} },
	{ apm_is_horked, "Dell Dimension 4100", { /* APM crashes */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "XPS-Z"),
			MATCH(DMI_BIOS_VENDOR,"Intel Corp."),
			MATCH(DMI_BIOS_VERSION,"A11")
			} },
	{ set_apm_ints, "Dell Inspiron", {	/* Allow interrupts during suspend on Dell Inspiron laptops*/
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron 4000"),
			NO_MATCH, NO_MATCH
			} },
	{ set_apm_ints, "Dell Latitude", {	/* Allow interrupts during suspend on Dell Latitude laptops*/
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Latitude C510"),
			NO_MATCH, NO_MATCH
			} },
	{ broken_apm_power, "Dell Inspiron 5000e", {	/* Handle problems with APM on Inspiron 5000e */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "A04"),
			MATCH(DMI_BIOS_DATE, "08/24/2000"), NO_MATCH
			} },

	/* other items */	
	{ broken_apm_power, "Dell Inspiron 2500", {	/* Handle problems with APM on Inspiron 2500 */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "A12"),
			MATCH(DMI_BIOS_DATE, "02/04/2002"), NO_MATCH
			} },
	{ set_realmode_power_off, "Award Software v4.60 PGMA", {	/* broken PM poweroff bios */
			MATCH(DMI_BIOS_VENDOR, "Award Software International, Inc."),
			MATCH(DMI_BIOS_VERSION, "4.60 PGMA"),
			MATCH(DMI_BIOS_DATE, "134526184"), NO_MATCH
			} },
	{ set_smp_bios_reboot, "Dell PowerEdge 1300", {	/* Handle problems with rebooting on Dell 1300's */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PowerEdge 1300/"),
			NO_MATCH, NO_MATCH
			} },
	{ set_bios_reboot, "Dell PowerEdge 300", {	/* Handle problems with rebooting on Dell 1300's */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PowerEdge 300/"),
			NO_MATCH, NO_MATCH
			} },
	{ set_bios_reboot, "Dell PowerEdge 2400", {  /* Handle problems with rebooting on Dell 2400's */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PowerEdge 2400"),
			NO_MATCH, NO_MATCH
			} },
	{ set_bios_reboot, "Dell PowerEdge 6450", {  /* Handle problems with rebooting on Dell 6450's */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PowerEdge 6450"),
			NO_MATCH, NO_MATCH
			} },
	{ set_bios_reboot, "Dell PowerEdge 6400", {  /* Handle problems with rebooting on Dell 6400's */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PowerEdge 6400"),
			NO_MATCH, NO_MATCH
			} },
	{ set_apm_ints, "Compaq 12XL125", {	/* Allow interrupts during suspend on Compaq Laptops*/
			MATCH(DMI_SYS_VENDOR, "Compaq"),
			MATCH(DMI_PRODUCT_NAME, "Compaq PC"),
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION,"4.06")
			} },
	{ set_apm_ints, "ASUSTeK", {   /* Allow interrupts during APM or the clock goes slow */
			MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
			MATCH(DMI_PRODUCT_NAME, "L8400K series Notebook PC"),
			NO_MATCH, NO_MATCH
			} },					
	{ apm_is_horked, "Trigem Delhi3", { /* APM crashes */
			MATCH(DMI_SYS_VENDOR, "TriGem Computer, Inc"),
			MATCH(DMI_PRODUCT_NAME, "Delhi3"),
			NO_MATCH, NO_MATCH,
			} },
	{ apm_is_horked, "Fujitsu-Siemens", { /* APM crashes */
			MATCH(DMI_BIOS_VENDOR, "hoenix/FUJITSU SIEMENS"),
			MATCH(DMI_BIOS_VERSION, "Version1.01"),
			NO_MATCH, NO_MATCH,
			} },
	{ apm_is_horked_d850md, "Intel D850MD", { /* APM crashes */
			MATCH(DMI_BIOS_VENDOR, "Intel Corp."),
			MATCH(DMI_BIOS_VERSION, "MV85010A.86A.0016.P07.0201251536"),
			NO_MATCH, NO_MATCH,
			} },
	{ apm_is_horked, "Intel D810EMO", { /* APM crashes */
			MATCH(DMI_BIOS_VENDOR, "Intel Corp."),
			MATCH(DMI_BIOS_VERSION, "MO81010A.86A.0008.P04.0004170800"),
			NO_MATCH, NO_MATCH,
			} },
	{ apm_is_horked, "Dell XPS-Z", { /* APM crashes */
			MATCH(DMI_BIOS_VENDOR, "Intel Corp."),
			MATCH(DMI_BIOS_VERSION, "A11"),
			MATCH(DMI_PRODUCT_NAME, "XPS-Z"),
			NO_MATCH,
			} },
	{ apm_is_horked, "Sharp PC-PJ/AX", { /* APM crashes */
			MATCH(DMI_SYS_VENDOR, "SHARP"),
			MATCH(DMI_PRODUCT_NAME, "PC-PJ/AX"),
			MATCH(DMI_BIOS_VENDOR,"SystemSoft"),
			MATCH(DMI_BIOS_VERSION,"Version R2.08")
			} },
	{ apm_is_horked, "Dell Inspiron 2500", { /* APM crashes */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron 2500"),
			MATCH(DMI_BIOS_VENDOR,"Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION,"A11")
			} },
	{ apm_likes_to_melt, "Jabil AMD", { /* APM idle hangs */
			MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
			MATCH(DMI_BIOS_VERSION, "0AASNP06"),
			NO_MATCH, NO_MATCH,
			} },
	{ apm_likes_to_melt, "AMI Bios", { /* APM idle hangs */
			MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
			MATCH(DMI_BIOS_VERSION, "0AASNP05"), 
			NO_MATCH, NO_MATCH,
			} },

	{ sony_vaio_laptop, "Sony Vaio", { /* This is a Sony Vaio laptop */
			MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			MATCH(DMI_PRODUCT_NAME, "PCG-"),
			NO_MATCH, NO_MATCH,
			} },
	{ swab_apm_power_in_minutes, "Sony VAIO", { /* Handle problems with APM on Sony Vaio PCG-N505X(DE) */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0206H"),
			MATCH(DMI_BIOS_DATE, "08/23/99"), NO_MATCH
	} },

	{ swab_apm_power_in_minutes, "Sony VAIO", { /* Handle problems with APM on Sony Vaio PCG-N505VX */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "W2K06H0"),
			MATCH(DMI_BIOS_DATE, "02/03/00"), NO_MATCH
			} },
			
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-XG29 */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0117A0"),
			MATCH(DMI_BIOS_DATE, "04/25/00"), NO_MATCH
			} },

	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z600NE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0121Z1"),
			MATCH(DMI_BIOS_DATE, "05/11/00"), NO_MATCH
			} },

	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z600NE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "WME01Z1"),
			MATCH(DMI_BIOS_DATE, "08/11/00"), NO_MATCH
			} },

	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z600LEK(DE) */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0206Z3"),
			MATCH(DMI_BIOS_DATE, "12/25/00"), NO_MATCH
			} },

	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z505LS */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0203D0"),
			MATCH(DMI_BIOS_DATE, "05/12/00"), NO_MATCH
			} },

	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z505LS */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0203Z3"),
			MATCH(DMI_BIOS_DATE, "08/25/00"), NO_MATCH
			} },
	
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z505LS (with updated BIOS) */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0209Z3"),
			MATCH(DMI_BIOS_DATE, "05/12/01"), NO_MATCH
			} },
	
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z505LS (with updated BIOS) */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "WXP01Z3"),
			MATCH(DMI_BIOS_DATE, "10/26/01"), NO_MATCH
			} },
	
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-F104K */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0204K2"),
			MATCH(DMI_BIOS_DATE, "08/28/00"), NO_MATCH
			} },
	
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-C1VN/C1VE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0208P1"),
			MATCH(DMI_BIOS_DATE, "11/09/00"), NO_MATCH
			} },
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-C1VE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0204P1"),
			MATCH(DMI_BIOS_DATE, "09/12/00"), NO_MATCH
			} },
			
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z600RE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "R0116Z1"),
			NO_MATCH, NO_MATCH
			} },
	
	{ swab_apm_power_in_minutes, "Sony VAIO", {	/* Handle problems with APM on Sony Vaio PCG-Z600RE */
			MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			MATCH(DMI_BIOS_VERSION, "RK116Z1"),
			NO_MATCH, NO_MATCH
			} },

	{ exploding_pnp_bios, "Higraded P14H", {	/* BIOSPnP problem */
			MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
			MATCH(DMI_BIOS_VERSION, "07.00T"),
			MATCH(DMI_SYS_VENDOR, "Higraded"),
			MATCH(DMI_PRODUCT_NAME, "P14H")
			} },

	/* Machines which have problems handling enabled local APICs */

	{ local_apic_kills_bios, "Dell Inspiron", {
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron"),
			NO_MATCH, NO_MATCH
			} },

	{ local_apic_kills_bios, "Dell Latitude", {
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Latitude"),
			NO_MATCH, NO_MATCH
			} },

	{ local_apic_kills_bios, "IBM Thinkpad T20", {
			MATCH(DMI_BOARD_VENDOR, "IBM"),
			MATCH(DMI_BOARD_NAME, "264741U"),
			NO_MATCH, NO_MATCH
			} },

	{ apm_kills_local_apic, "Microstar 6163", {
			MATCH(DMI_BOARD_VENDOR, "MICRO-STAR INTERNATIONAL CO., LTD"),
			MATCH(DMI_BOARD_NAME, "MS-6163"),
			NO_MATCH, NO_MATCH } },

	{ apm_kills_local_apic_timer, "Intel AL440LX", {
			MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			MATCH(DMI_BOARD_NAME, "AL440LX"),
			NO_MATCH, NO_MATCH } },

	/* Problem Intel 440GX bioses */

	{ broken_pirq, "SABR1 Bios", {			/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"SABR1"),
			NO_MATCH, NO_MATCH
			} },
	{ broken_pirq, "l44GX Bios", {        		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0.86B.0066.P07"),
			NO_MATCH, NO_MATCH
                        } },
	{ broken_pirq, "IBM xseries 370", {        		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "IBM"),
			MATCH(DMI_BIOS_VERSION,"MMKT33AUS"),
			NO_MATCH, NO_MATCH
                        } },
	{ broken_pirq, "l44GX Bios", {        		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0.86B.0094.P10"),
			NO_MATCH, NO_MATCH
                        } },
	{ broken_pirq, "l44GX Bios", {        		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0.86B.0115.P12"),
			NO_MATCH, NO_MATCH
                        } },
	{ broken_pirq, "l44GX Bios", {        		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0.86B.0120.P12"),
			NO_MATCH, NO_MATCH
                        } },
	{ broken_pirq, "l44GX Bios", {		/* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0.86B.0125.P13"),
			NO_MATCH, NO_MATCH
			} },
	{ broken_pirq, "l44GX Bios", {          /* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"L440GX0"),
			NO_MATCH, NO_MATCH
			} },
	{ broken_pirq, "l44GX Bios", {          /* Bad $PIR */
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION,"C440GX0"),
			NO_MATCH, NO_MATCH
			} },
		
                        
	/* Intel in disgiuse - In this case they can't hide and they don't run
	   too well either... */
	{ broken_pirq, "Dell PowerEdge 8450", {		/* Bad $PIR */
			MATCH(DMI_PRODUCT_NAME, "Dell PowerEdge 8450"),
			NO_MATCH, NO_MATCH, NO_MATCH
			} },

	/* Possibly another Intel board issue, bz89593 */
	{ broken_pirq, "Intel NA440BX DP", {
			MATCH(DMI_BIOS_VENDOR, "Intel Corporation"),
			MATCH(DMI_BIOS_VERSION, "NIGHTS0.86B.0095.P14.0001211034"),
			NO_MATCH, NO_MATCH
			} },
			
	{ init_ints_after_s1, "Toshiba Satellite 4030cdt", { /* Reinitialization of 8259 is needed after S1 resume */
			MATCH(DMI_PRODUCT_NAME, "S4030CDT/4.3"),
			NO_MATCH, NO_MATCH, NO_MATCH
			} },

	{ print_if_true, KERN_WARNING "IBM T23 - BIOS 1.03b+ and controller firmware 1.02+ may be needed for Linux APM.", {
			MATCH(DMI_SYS_VENDOR, "IBM"),
			MATCH(DMI_BIOS_VERSION, "1AET38WW (1.01b)"),
			NO_MATCH, NO_MATCH
			} },
	 
	{ fix_broken_hp_bios_irq9, "HP Pavilion N5400 Series Laptop", {
			MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			MATCH(DMI_BIOS_VERSION, "GE.M1.03"),
			MATCH(DMI_PRODUCT_VERSION, "HP Pavilion Notebook Model GE"),
			MATCH(DMI_BOARD_VERSION, "OmniBook N32N-736")
			} },

	/*
	 *	Generic per vendor APM settings
	 */
	 
	{ set_apm_ints, "IBM", {	/* Allow interrupts during suspend on IBM laptops */
			MATCH(DMI_SYS_VENDOR, "IBM"),
			NO_MATCH, NO_MATCH, NO_MATCH
			} },
	{ broken_apm_pci_restore, "Dell Inspiron 8000", {	/* Work around broken Dell BIOS */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron 8000"),
			NO_MATCH, NO_MATCH
			} },
	{ broken_apm_pci_restore, "Dell Inspiron 8100", {	/* Work around broken Dell BIOS */
			MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			MATCH(DMI_PRODUCT_NAME, "Inspiron 8100"),
			NO_MATCH, NO_MATCH
			} },

	/* IBM Bladeservers */
	{ disable_console_keyboard, "IBM Server Blade", {
			MATCH(DMI_SYS_VENDOR,"IBM"),
			MATCH(DMI_BOARD_NAME, "Server Blade"),
			NO_MATCH, NO_MATCH
			} },
	/* SUN */
	/* x4100 */
	{ pci_no_peer_tricks, "Sun X4100", {
			MATCH(DMI_SYS_VENDOR,"Sun Microsystems"),
			MATCH(DMI_PRODUCT_NAME, "Sun Fire X4100 Server"),
			NO_MATCH, NO_MATCH
			} },
	/* x4200 */
	{ pci_no_peer_tricks, "Sun X4200", {
			MATCH(DMI_SYS_VENDOR,"Sun Microsystems"),
			MATCH(DMI_PRODUCT_NAME, "Sun Fire X4200 Server"),
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
