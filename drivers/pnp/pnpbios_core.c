/*
 * PnP BIOS services
 * 
 * Originally (C) 1998 Christian Schmidt <schmidt@digadd.de>
 * Modifications (c) 1998 Tom Lees <tom@lpsg.demon.co.uk>
 * Minor reorganizations by David Hinds <dahinds@users.sourceforge.net>
 * Modifications (c) 2001,2002 by Thomas Hood <jdthood@mail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * References:
 *   Compaq Computer Corporation, Phoenix Technologies Ltd., Intel Corporation
 *   Plug and Play BIOS Specification, Version 1.0A, May 5, 1994
 *   Plug and Play BIOS Clarification Paper, October 6, 1994
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/pnpbios.h>
#include <asm/page.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <asm/desc.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/kmod.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <asm/system.h>


/*
 *
 * PnP BIOS INTERFACE
 *
 */

/* PnP BIOS signature: "$PnP" */
#define PNP_SIGNATURE   (('$' << 0) + ('P' << 8) + ('n' << 16) + ('P' << 24))

#pragma pack(1)
union pnp_bios_expansion_header {
	struct {
		u32 signature;    /* "$PnP" */
		u8 version;	  /* in BCD */
		u8 length;	  /* length in bytes, currently 21h */
		u16 control;	  /* system capabilities */
		u8 checksum;	  /* all bytes must add up to 0 */

		u32 eventflag;    /* phys. address of the event flag */
		u16 rmoffset;     /* real mode entry point */
		u16 rmcseg;
		u16 pm16offset;   /* 16 bit protected mode entry */
		u32 pm16cseg;
		u32 deviceID;	  /* EISA encoded system ID or 0 */
		u16 rmdseg;	  /* real mode data segment */
		u32 pm16dseg;	  /* 16 bit pm data segment base */
	} fields;
	char chars[0x21];	  /* To calculate the checksum */
};
#pragma pack()

static struct {
	u16	offset;
	u16	segment;
} pnp_bios_callpoint;

static union pnp_bios_expansion_header * pnp_bios_hdr = NULL;

/* The PnP BIOS entries in the GDT */
#define PNP_GDT    (0x0060)
#define PNP_CS32   (PNP_GDT+0x00)	/* segment for calling fn */
#define PNP_CS16   (PNP_GDT+0x08)	/* code segment for BIOS */
#define PNP_DS     (PNP_GDT+0x10)	/* data segment for BIOS */
#define PNP_TS1    (PNP_GDT+0x18)	/* transfer data segment */
#define PNP_TS2    (PNP_GDT+0x20)	/* another data segment */

/* 
 * These are some opcodes for a "static asmlinkage"
 * As this code is *not* executed inside the linux kernel segment, but in a
 * alias at offset 0, we need a far return that can not be compiled by
 * default (please, prove me wrong! this is *really* ugly!) 
 * This is the only way to get the bios to return into the kernel code,
 * because the bios code runs in 16 bit protected mode and therefore can only
 * return to the caller if the call is within the first 64kB, and the linux
 * kernel begins at offset 3GB...
 */

asmlinkage void pnp_bios_callfunc(void);

__asm__(
	".text			\n"
	__ALIGN_STR "\n"
	SYMBOL_NAME_STR(pnp_bios_callfunc) ":\n"
	"	pushl %edx	\n"
	"	pushl %ecx	\n"
	"	pushl %ebx	\n"
	"	pushl %eax	\n"
	"	lcallw " SYMBOL_NAME_STR(pnp_bios_callpoint) "\n"
	"	addl $16, %esp	\n"
	"	lret		\n"
	".previous		\n"
);

#define Q_SET_SEL(selname, address, size) \
set_base (gdt [(selname) >> 3], __va((u32)(address))); \
set_limit (gdt [(selname) >> 3], size)

#define Q2_SET_SEL(selname, address, size) \
set_base (gdt [(selname) >> 3], (u32)(address)); \
set_limit (gdt [(selname) >> 3], size)

/*
 * At some point we want to use this stack frame pointer to unwind
 * after PnP BIOS oopses. 
 */
 
u32 pnp_bios_fault_esp;
u32 pnp_bios_fault_eip;
u32 pnp_bios_is_utter_crap = 0;

static spinlock_t pnp_bios_lock;

static inline u16 call_pnp_bios(u16 func, u16 arg1, u16 arg2, u16 arg3,
                                u16 arg4, u16 arg5, u16 arg6, u16 arg7,
                                void *ts1_base, u32 ts1_size,
                                void *ts2_base, u32 ts2_size)
{
	unsigned long flags;
	u16 status;

	/*
	 * PnP BIOSes are generally not terribly re-entrant.
	 * Also, don't rely on them to save everything correctly.
	 */
	if(pnp_bios_is_utter_crap)
		return PNP_FUNCTION_NOT_SUPPORTED;

	/* On some boxes IRQ's during PnP BIOS calls are deadly.  */
	spin_lock_irqsave(&pnp_bios_lock, flags);

	if (ts1_size)
		Q2_SET_SEL(PNP_TS1, ts1_base, ts1_size);
	if (ts2_size)
		Q2_SET_SEL(PNP_TS2, ts2_base, ts2_size);

	__asm__ __volatile__(
	        "pushl %%ebp\n\t"
		"pushl %%edi\n\t"
		"pushl %%esi\n\t"
		"pushl %%ds\n\t"
		"pushl %%es\n\t"
		"pushl %%fs\n\t"
		"pushl %%gs\n\t"
		"pushfl\n\t"
		"movl %%esp, pnp_bios_fault_esp\n\t"
		"movl $1f, pnp_bios_fault_eip\n\t"
		"lcall %5,%6\n\t"
		"1:popfl\n\t"
		"popl %%gs\n\t"
		"popl %%fs\n\t"
		"popl %%es\n\t"
		"popl %%ds\n\t"
	        "popl %%esi\n\t"
		"popl %%edi\n\t"
		"popl %%ebp\n\t"
		: "=a" (status)
		: "0" ((func) | (((u32)arg1) << 16)),
		  "b" ((arg2) | (((u32)arg3) << 16)),
		  "c" ((arg4) | (((u32)arg5) << 16)),
		  "d" ((arg6) | (((u32)arg7) << 16)),
		  "i" (PNP_CS32),
		  "i" (0)
		: "memory"
	);
	spin_unlock_irqrestore(&pnp_bios_lock, flags);
	
	/* If we get here and this is set then the PnP BIOS faulted on us. */
	if(pnp_bios_is_utter_crap)
	{
		printk(KERN_ERR "PnPBIOS: Warning! Your PnP BIOS caused a fatal error. Attempting to continue.\n");
		printk(KERN_ERR "PnPBIOS: You may need to reboot with the \"pnpbios=off\" option to operate stably.\n");
		printk(KERN_ERR "PnPBIOS: Check with your vendor for an updated BIOS.\n");
	}

	return status;
}


/*
 *
 * UTILITY FUNCTIONS
 *
 */

static void pnpbios_warn_unexpected_status(const char * module, u16 status)
{
	printk(KERN_ERR "PnPBIOS: %s: Unexpected status 0x%x\n", module, status);
}

void *pnpbios_kmalloc(size_t size, int f)
{
	void *p = kmalloc( size, f );
	if ( p == NULL )
		printk(KERN_ERR "PnPBIOS: kmalloc() failed\n");
	return p;
}

/*
 * Call this only after init time
 */
static inline int pnp_bios_present(void)
{
	return (pnp_bios_hdr != NULL);
}

/* Forward declaration */
static void update_devlist( u8 nodenum, struct pnp_bios_node *data );


/*
 *
 * PnP BIOS ACCESS FUNCTIONS
 *
 */

#define PNP_GET_NUM_SYS_DEV_NODES       0x00
#define PNP_GET_SYS_DEV_NODE            0x01
#define PNP_SET_SYS_DEV_NODE            0x02
#define PNP_GET_EVENT                   0x03
#define PNP_SEND_MESSAGE                0x04
#define PNP_GET_DOCKING_STATION_INFORMATION 0x05
#define PNP_SET_STATIC_ALLOCED_RES_INFO 0x09
#define PNP_GET_STATIC_ALLOCED_RES_INFO 0x0a
#define PNP_GET_APM_ID_TABLE            0x0b
#define PNP_GET_PNP_ISA_CONFIG_STRUC    0x40
#define PNP_GET_ESCD_INFO               0x41
#define PNP_READ_ESCD                   0x42
#define PNP_WRITE_ESCD                  0x43

/*
 * Call PnP BIOS with function 0x00, "get number of system device nodes"
 */
static int __pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_NUM_SYS_DEV_NODES, 0, PNP_TS1, 2, PNP_TS1, PNP_DS, 0, 0,
			       data, sizeof(struct pnp_dev_node_info), 0, 0);
	data->no_nodes &= 0xff;
	return status;
}

int pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	int status = __pnp_bios_dev_node_info( data );
	if ( status )
		pnpbios_warn_unexpected_status( "dev_node_info", status );
	return status;
}

/*
 * Note that some PnP BIOSes (e.g., on Sony Vaio laptops) die a horrible
 * death if they are asked to access the "current" configuration.
 * Therefore, if it's a matter of indifference, it's better to call
 * get_dev_node() and set_dev_node() with boot=1 rather than with boot=0.
 */

/* 
 * Call PnP BIOS with function 0x01, "get system device node"
 * Input: *nodenum = desired node, 
 *        boot = whether to get nonvolatile boot (!=0)
 *               or volatile current (0) config
 * Output: *nodenum=next node or 0xff if no more nodes
 */
static int __pnp_bios_get_dev_node(u8 *nodenum, char boot, struct pnp_bios_node *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if ( !boot & pnpbios_dont_use_current_config )
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_SYS_DEV_NODE, 0, PNP_TS1, 0, PNP_TS2, boot ? 2 : 1, PNP_DS, 0,
			       nodenum, sizeof(char), data, 65536);
	return status;
}

int pnp_bios_get_dev_node(u8 *nodenum, char boot, struct pnp_bios_node *data)
{
	int status;
	status =  __pnp_bios_get_dev_node( nodenum, boot, data );
	if ( status )
		pnpbios_warn_unexpected_status( "get_dev_node", status );
	return status;
}


/*
 * Call PnP BIOS with function 0x02, "set system device node"
 * Input: *nodenum = desired node, 
 *        boot = whether to set nonvolatile boot (!=0)
 *               or volatile current (0) config
 */
static int __pnp_bios_set_dev_node(u8 nodenum, char boot, struct pnp_bios_node *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if ( !boot & pnpbios_dont_use_current_config )
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SET_SYS_DEV_NODE, nodenum, 0, PNP_TS1, boot ? 2 : 1, PNP_DS, 0, 0,
			       data, 65536, 0, 0);
	return status;
}

int pnp_bios_set_dev_node(u8 nodenum, char boot, struct pnp_bios_node *data)
{
	int status;
	status =  __pnp_bios_set_dev_node( nodenum, boot, data );
	if ( status ) {
		pnpbios_warn_unexpected_status( "set_dev_node", status );
		return status;
	}
	if ( !boot ) { /* Update devlist */
		u8 thisnodenum = nodenum;
		status =  pnp_bios_get_dev_node( &nodenum, boot, data );
		if ( status )
			return status;
		update_devlist( thisnodenum, data );
	}
	return status;
}

#if needed
/*
 * Call PnP BIOS with function 0x03, "get event"
 */
static int pnp_bios_get_event(u16 *event)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_EVENT, 0, PNP_TS1, PNP_DS, 0, 0 ,0 ,0,
			       event, sizeof(u16), 0, 0);
	return status;
}
#endif

#if needed
/* 
 * Call PnP BIOS with function 0x04, "send message"
 */
static int pnp_bios_send_message(u16 message)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SEND_MESSAGE, message, PNP_DS, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	return status;
}
#endif

#ifdef CONFIG_HOTPLUG
/*
 * Call PnP BIOS with function 0x05, "get docking station information"
 */
static int pnp_bios_dock_station_info(struct pnp_docking_station_info *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_DOCKING_STATION_INFORMATION, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       data, sizeof(struct pnp_docking_station_info), 0, 0);
	return status;
}
#endif

#if needed
/*
 * Call PnP BIOS with function 0x09, "set statically allocated resource
 * information"
 */
static int pnp_bios_set_stat_res(char *info)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SET_STATIC_ALLOCED_RES_INFO, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       info, *((u16 *) info), 0, 0);
	return status;
}
#endif

/*
 * Call PnP BIOS with function 0x0a, "get statically allocated resource
 * information"
 */
static int __pnp_bios_get_stat_res(char *info)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_STATIC_ALLOCED_RES_INFO, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       info, 65536, 0, 0);
	return status;
}

int pnp_bios_get_stat_res(char *info)
{
	int status;
	status = __pnp_bios_get_stat_res( info );
	if ( status )
		pnpbios_warn_unexpected_status( "get_stat_res", status );
	return status;
}

#if needed
/*
 * Call PnP BIOS with function 0x0b, "get APM id table"
 */
static int pnp_bios_apm_id_table(char *table, u16 *size)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_APM_ID_TABLE, 0, PNP_TS2, 0, PNP_TS1, PNP_DS, 0, 0,
			       table, *size, size, sizeof(u16));
	return status;
}
#endif

/*
 * Call PnP BIOS with function 0x40, "get isa pnp configuration structure"
 */
static int __pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	u16 status;
	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_PNP_ISA_CONFIG_STRUC, 0, PNP_TS1, PNP_DS, 0, 0, 0, 0,
			       data, sizeof(struct pnp_isa_config_struc), 0, 0);
	return status;
}

int pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	int status;
	status = __pnp_bios_isapnp_config( data );
	if ( status )
		pnpbios_warn_unexpected_status( "isapnp_config", status );
	return status;
}

/*
 * Call PnP BIOS with function 0x41, "get ESCD info"
 */
static int __pnp_bios_escd_info(struct escd_info_struc *data)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_ESCD_INFO, 0, PNP_TS1, 2, PNP_TS1, 4, PNP_TS1, PNP_DS,
			       data, sizeof(struct escd_info_struc), 0, 0);
	return status;
}

int pnp_bios_escd_info(struct escd_info_struc *data)
{
	int status;
	status = __pnp_bios_escd_info( data );
	if ( status )
		pnpbios_warn_unexpected_status( "escd_info", status );
	return status;
}

/*
 * Call PnP BIOS function 0x42, "read ESCD"
 * nvram_base is determined by calling escd_info
 */
static int __pnp_bios_read_escd(char *data, u32 nvram_base)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_READ_ESCD, 0, PNP_TS1, PNP_TS2, PNP_DS, 0, 0, 0,
			       data, 65536, (void *)nvram_base, 65536);
	return status;
}

int pnp_bios_read_escd(char *data, u32 nvram_base)
{
	int status;
	status = __pnp_bios_read_escd( data, nvram_base );
	if ( status )
		pnpbios_warn_unexpected_status( "read_escd", status );
	return status;
}

#if needed
/*
 * Call PnP BIOS function 0x43, "write ESCD"
 */
static int pnp_bios_write_escd(char *data, u32 nvram_base)
{
	u16 status;
	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_WRITE_ESCD, 0, PNP_TS1, PNP_TS2, PNP_DS, 0, 0, 0,
			       data, 65536, nvram_base, 65536);
	return status;
}
#endif


/*
 *
 * DOCKING FUNCTIONS
 *
 */

#ifdef CONFIG_HOTPLUG

static int unloading = 0;
static struct completion unload_sem;

/*
 * (Much of this belongs in a shared routine somewhere)
 */
 
static int pnp_dock_event(int dock, struct pnp_docking_station_info *info)
{
	char *argv [3], **envp, *buf, *scratch;
	int i = 0, value;

	if (!hotplug_path [0])
		return -ENOENT;
	if (!current->fs->root) {
		return -EAGAIN;
	}
	if (!(envp = (char **) pnpbios_kmalloc (20 * sizeof (char *), GFP_KERNEL))) {
		return -ENOMEM;
	}
	if (!(buf = pnpbios_kmalloc (256, GFP_KERNEL))) {
		kfree (envp);
		return -ENOMEM;
	}

	/* only one standardized param to hotplug command: type */
	argv [0] = hotplug_path;
	argv [1] = "dock";
	argv [2] = 0;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

#ifdef	DEBUG
	/* hint that policy agent should enter no-stdout debug mode */
	envp [i++] = "DEBUG=kernel";
#endif
	/* extensible set of named bus-specific parameters,
	 * supporting multiple driver selection algorithms.
	 */
	scratch = buf;

	/* action:  add, remove */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "ACTION=%s", dock?"add":"remove") + 1;

	/* Report the ident for the dock */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "DOCK=%x/%x/%x",
		info->location_id, info->serial, info->capabilities);
	envp[i] = 0;
	
	value = call_usermodehelper (argv [0], argv, envp);
	kfree (buf);
	kfree (envp);
	return 0;
}

/*
 * Poll the PnP docking at regular intervals
 */
static int pnp_dock_thread(void * unused)
{
	static struct pnp_docking_station_info now;
	int docked = -1, d = 0;
	daemonize();
	reparent_to_init();
	strcpy(current->comm, "kpnpbiosd");
	while(!unloading && !signal_pending(current))
	{
		int status;
		
		/*
		 * Poll every 2 seconds
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ*2);
		if(signal_pending(current))
			break;

		status = pnp_bios_dock_station_info(&now);

		switch(status)
		{
			/*
			 * No dock to manage
			 */
			case PNP_FUNCTION_NOT_SUPPORTED:
				complete_and_exit(&unload_sem, 0);
			case PNP_SYSTEM_NOT_DOCKED:
				d = 0;
				break;
			case PNP_SUCCESS:
				d = 1;
				break;
			default:
				pnpbios_warn_unexpected_status( "pnp_dock_thread", status );
				continue;
		}
		if(d != docked)
		{
			if(pnp_dock_event(d, &now)==0)
			{
				docked = d;
#if 0
				printk(KERN_INFO "PnPBIOS: Docking station %stached\n", docked?"at":"de");
#endif
			}
		}
	}	
	complete_and_exit(&unload_sem, 0);
}

#endif   /* CONFIG_HOTPLUG */


/*
 *
 * NODE DATA PARSING FUNCTIONS
 *
 */

static void add_irqresource(struct pci_dev *dev, int irq)
{
	int i = 0;
	while (!(dev->irq_resource[i].flags & IORESOURCE_UNSET) && i < DEVICE_COUNT_IRQ) i++;
	if (i < DEVICE_COUNT_IRQ) {
		dev->irq_resource[i].start = (unsigned long) irq;
		dev->irq_resource[i].flags = IORESOURCE_IRQ;  // Also clears _UNSET flag
	}
}

static void add_dmaresource(struct pci_dev *dev, int dma)
{
	int i = 0;
	while (!(dev->dma_resource[i].flags & IORESOURCE_UNSET) && i < DEVICE_COUNT_DMA) i++;
	if (i < DEVICE_COUNT_DMA) {
		dev->dma_resource[i].start = (unsigned long) dma;
		dev->dma_resource[i].flags = IORESOURCE_DMA;  // Also clears _UNSET flag
	}
}

static void add_ioresource(struct pci_dev *dev, int io, int len)
{
	int i = 0;
	while (!(dev->resource[i].flags & IORESOURCE_UNSET) && i < DEVICE_COUNT_RESOURCE) i++;
	if (i < DEVICE_COUNT_RESOURCE) {
		dev->resource[i].start = (unsigned long) io;
		dev->resource[i].end = (unsigned long)(io + len - 1);
		dev->resource[i].flags = IORESOURCE_IO;  // Also clears _UNSET flag
	}
}

static void add_memresource(struct pci_dev *dev, int mem, int len)
{
	int i = 0;
	while (!(dev->resource[i].flags & IORESOURCE_UNSET) && i < DEVICE_COUNT_RESOURCE) i++;
	if (i < DEVICE_COUNT_RESOURCE) {
		dev->resource[i].start = (unsigned long) mem;
		dev->resource[i].end = (unsigned long)(mem + len - 1);
		dev->resource[i].flags = IORESOURCE_MEM;  // Also clears _UNSET flag
	}
}

static void node_resource_data_to_dev(struct pnp_bios_node *node, struct pci_dev *dev)
{
	unsigned char *p = node->data, *lastp=NULL;
	int i;

	/*
	 * First, set resource info to default values
	 */
	for (i=0;i<DEVICE_COUNT_RESOURCE;i++) {
		dev->resource[i].start = 0;  // "disabled"
		dev->resource[i].flags = IORESOURCE_UNSET;
	}
	for (i=0;i<DEVICE_COUNT_IRQ;i++) {
		dev->irq_resource[i].start = (unsigned long)-1;  // "disabled"
		dev->irq_resource[i].flags = IORESOURCE_UNSET;
	}
	for (i=0;i<DEVICE_COUNT_DMA;i++) {
		dev->dma_resource[i].start = (unsigned long)-1;  // "disabled"
		dev->dma_resource[i].flags = IORESOURCE_UNSET;
	}

	/*
	 * Fill in dev resource info
	 */
        while ( (char *)p < ((char *)node->data + node->size )) {
        	if(p==lastp) break;

                if( p[0] & 0x80 ) {// large item
			switch (p[0] & 0x7f) {
			case 0x01: // memory
			{
				int io = *(short *) &p[4];
				int len = *(short *) &p[10];
				add_memresource(dev, io, len);
				break;
			}
			case 0x02: // device name
			{
				int len = *(short *) &p[1];
				memcpy(dev->name, p + 3, len >= 80 ? 79 : len);
				break;
			}
			case 0x05: // 32-bit memory
			{
				int io = *(int *) &p[4];
				int len = *(int *) &p[16];
				add_memresource(dev, io, len);
				break;
			}
			case 0x06: // fixed location 32-bit memory
			{
				int io = *(int *) &p[4];
				int len = *(int *) &p[8];
				add_memresource(dev, io, len);
				break;
			}
			} /* switch */
                        lastp = p+3;
                        p = p + p[1] + p[2]*256 + 3;
                        continue;
                }
                if ((p[0]>>3) == 0x0f) // end tag
                        break;
                switch (p[0]>>3) {
                case 0x04: // irq
                {
                        int i, mask, irq = -1;
                        mask= p[1] + p[2]*256;
                        for (i=0;i<16;i++, mask=mask>>1)
                                if(mask & 0x01) irq=i;
			add_irqresource(dev, irq);
                        break;
                }
                case 0x05: // dma
                {
                        int i, mask, dma = -1;
                        mask = p[1];
                        for (i=0;i<8;i++, mask = mask>>1)
                                if(mask & 0x01) dma=i;
			add_dmaresource(dev, dma);
                        break;
                }
                case 0x08: // io
                {
			int io= p[2] + p[3] *256;
			int len = p[7];
			add_ioresource(dev, io, len);
                        break;
                }
		case 0x09: // fixed location io
		{
			int io = p[1] + p[2] * 256;
			int len = p[3];
			add_ioresource(dev, io, len);
			break;
		}
                } /* switch */
                lastp=p+1;
                p = p + (p[0] & 0x07) + 1;

        } /* while */

        return;
}


/*
 *
 * DEVICE LIST MANAGEMENT FUNCTIONS
 *
 *
 * Some of these are exported to give public access
 *
 * Question: Why maintain a device list when the PnP BIOS can 
 * list devices for us?  Answer: Some PnP BIOSes can't report
 * the current configuration, only the boot configuration.
 * The boot configuration can be changed, so we need to keep
 * a record of what the configuration was when we booted;
 * presumably it continues to describe the current config.
 * For those BIOSes that can change the current config, we
 * keep the information in the devlist up to date.
 *
 * Note that it is currently assumed that the list does not
 * grow or shrink in size after init time, and slot_name
 * never changes.  The list is protected by a spinlock.
 */

static LIST_HEAD(pnpbios_devices);

static spinlock_t pnpbios_devices_lock;

static int inline insert_device(struct pci_dev *dev)
{

	/*
	 * FIXME: Check for re-add of existing node;
	 * return -1 if node already present
	 */

	/* We don't lock because we only do this at init time */
	list_add_tail(&dev->global_list, &pnpbios_devices);

	return 0;
}

#define HEX(id,a) hex[((id)>>a) & 15]
#define CHAR(id,a) (0x40 + (((id)>>a) & 31))
//
static void inline pnpid32_to_pnpid(u32 id, char *str)
{
	const char *hex = "0123456789abcdef";

	id = be32_to_cpu(id);
	str[0] = CHAR(id, 26);
	str[1] = CHAR(id, 21);
	str[2] = CHAR(id,16);
	str[3] = HEX(id, 12);
	str[4] = HEX(id, 8);
	str[5] = HEX(id, 4);
	str[6] = HEX(id, 0);
	str[7] = '\0';

	return;
}                                              
//
#undef CHAR
#undef HEX 

/*
 * Build a linked list of pci_devs in order of ascending node number
 * Called only at init time.
 */
static void __init build_devlist(void)
{
	u8 nodenum;
	unsigned int nodes_got = 0;
	unsigned int devs = 0;
	struct pnp_bios_node *node;
	struct pnp_dev_node_info node_info;
	struct pci_dev *dev;
	
	if (!pnp_bios_present())
		return;

	if (pnp_bios_dev_node_info(&node_info) != 0)
		return;

	node = pnpbios_kmalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return;

	for(nodenum=0; nodenum<0xff; ) {
		u8 thisnodenum = nodenum;
		/* We build the list from the "boot" config because
		 * asking for the "current" config causes some
		 * BIOSes to crash.
		 */
		if (pnp_bios_get_dev_node(&nodenum, (char )1 , node))
			break;
		nodes_got++;
		dev =  pnpbios_kmalloc(sizeof (struct pci_dev), GFP_KERNEL);
		if (!dev)
			break;
		memset(dev,0,sizeof(struct pci_dev));
		dev->devfn = thisnodenum;
		memcpy(dev->name,"PNPBIOS",8);
		pnpid32_to_pnpid(node->eisa_id,dev->slot_name);
		node_resource_data_to_dev(node,dev);
		if(insert_device(dev)<0)
			kfree(dev);
		else
			devs++;
		if (nodenum <= thisnodenum) {
			printk(KERN_ERR "PnPBIOS: build_devlist: Node number 0x%x is out of sequence following node 0x%x. Aborting.\n", (unsigned int)nodenum, (unsigned int)thisnodenum);
			break;
		}
	}
	kfree(node);

	printk(KERN_INFO "PnPBIOS: %i node%s reported by PnP BIOS; %i recorded by driver\n",
		nodes_got, nodes_got != 1 ? "s" : "", devs);
}

static struct pci_dev *find_device_by_nodenum( u8 nodenum )
{
	struct pci_dev *dev;

	pnpbios_for_each_dev(dev) {
		if(dev->devfn == nodenum)
			return dev;
	}

	return NULL;
}

static void update_devlist( u8 nodenum, struct pnp_bios_node *data )
{
	unsigned long flags;
	struct pci_dev *dev;

	spin_lock_irqsave(&pnpbios_devices_lock, flags);
	dev = find_device_by_nodenum( nodenum );
	if ( dev ) {
		node_resource_data_to_dev(data,dev);
	}
	spin_unlock_irqrestore(&pnpbios_devices_lock, flags);

	return;
}


/*
 *
 * DRIVER REGISTRATION FUNCTIONS
 *
 *
 * Exported to give public access
 *
 */

static LIST_HEAD(pnpbios_drivers);

static const struct pnpbios_device_id *
match_device(const struct pnpbios_device_id *ids, const struct pci_dev *dev)
{
	while (*ids->id)
	{
		if(memcmp(ids->id, dev->slot_name, 7)==0)
			return ids;
		ids++;
	}
	return NULL;
}

static int announce_device(struct pnpbios_driver *drv, struct pci_dev *dev)
{
	const struct pnpbios_device_id *id;
	struct pci_dev tmpdev;
	int ret;

	if (drv->id_table) {
		id = match_device(drv->id_table, dev);
		if (!id)
			return 0;
	} else
		id = NULL;

	memcpy( &tmpdev, dev, sizeof(struct pci_dev));
	tmpdev.global_list.prev = NULL;
	tmpdev.global_list.next = NULL;

	dev_probe_lock();
	/* Obviously, probe() should not call any pnpbios functions */
	ret = drv->probe(&tmpdev, id);
	dev_probe_unlock();
	if (ret < 1)
		return 0;

	dev->driver = (void *)drv;

	return 1;
}

/**
 * pnpbios_register_driver - register a new pci driver
 * @drv: the driver structure to register
 * 
 * Adds the driver structure to the list of registered drivers
 *
 * For each device in the pnpbios device list that matches one of
 * the ids in drv->id_table, calls the driver's "probe" function with
 * arguments (1) a pointer to a *temporary* struct pci_dev containing
 * resource info for the device, and (2) a pointer to the id string
 * of the device.  Expects the probe function to return 1 if the
 * driver claims the device (otherwise 0) in which case, marks the
 * device as having this driver.
 * 
 * Returns the number of pci devices which were claimed by the driver
 * during registration.  The driver remains registered even if the
 * return value is zero.
 */
int pnpbios_register_driver(struct pnpbios_driver *drv)
{
	struct pci_dev *dev;
	unsigned long flags;
	int count = 0;

	list_add_tail(&drv->node, &pnpbios_drivers);
	spin_lock_irqsave(&pnpbios_devices_lock, flags);
	pnpbios_for_each_dev(dev) {
		if (!pnpbios_dev_driver(dev))
			count += announce_device(drv, dev);
	}
	spin_unlock_irqrestore(&pnpbios_devices_lock, flags);
	return count;
}

EXPORT_SYMBOL(pnpbios_register_driver);

/**
 * pnpbios_unregister_driver - unregister a pci driver
 * @drv: the driver structure to unregister
 * 
 * Deletes the driver structure from the list of registered PnPBIOS
 * drivers, gives it a chance to clean up by calling its "remove"
 * function for each device it was responsible for, and marks those
 * devices as driverless.
 */
void pnpbios_unregister_driver(struct pnpbios_driver *drv)
{
	unsigned long flags;
	struct pci_dev *dev;

	list_del(&drv->node);
	spin_lock_irqsave(&pnpbios_devices_lock, flags);
	pnpbios_for_each_dev(dev) {
		if (dev->driver == (void *)drv) {
			if (drv->remove)
				drv->remove(dev);
			dev->driver = NULL;
		}
	}
	spin_unlock_irqrestore(&pnpbios_devices_lock, flags);
}

EXPORT_SYMBOL(pnpbios_unregister_driver);


/*
 *
 * RESOURCE RESERVATION FUNCTIONS
 *
 *
 * Used only at init time
 *
 */

static void __init reserve_ioport_range(char *pnpid, int start, int end)
{
	struct resource *res;
	char *regionid;

#if 0
	/*
	 * TEMPORARY hack to work around the fact that the
	 * floppy driver inappropriately reserves ioports 0x3f0 and 0x3f1
	 * Remove this once the floppy driver is fixed.
	 */
	if (
		(0x3f0 >= start && 0x3f0 <= end)
		|| (0x3f1 >= start && 0x3f1 <= end)
	) {
		printk(KERN_INFO
			"PnPBIOS: %s: ioport range 0x%x-0x%x NOT reserved\n",
			pnpid, start, end
		);
		return;
	}
#endif

	regionid = pnpbios_kmalloc(16, GFP_KERNEL);
	if ( regionid == NULL )
		return;
	snprintf(regionid, 16, "PnPBIOS %s", pnpid);
	res = request_region(start,end-start+1,regionid);
	if ( res == NULL )
		kfree( regionid );
	else
		res->flags &= ~IORESOURCE_BUSY;
	/*
	 * Failures at this point are usually harmless. pci quirks for
	 * example do reserve stuff they know about too, so we may well
	 * have double reservations.
	 */
	printk(KERN_INFO
		"PnPBIOS: %s: ioport range 0x%x-0x%x %s reserved\n",
		pnpid, start, end,
		NULL != res ? "has been" : "could not be"
	);

	return;
}

static void __init reserve_resources_of_dev( struct pci_dev *dev )
{
	int i;

	for (i=0;i<DEVICE_COUNT_RESOURCE;i++) {
		if ( dev->resource[i].flags & IORESOURCE_UNSET )
			/* end of resources */
			break;
		if (dev->resource[i].flags & IORESOURCE_IO) {
			/* ioport */
			if ( dev->resource[i].start == 0 )
				/* disabled */
				/* Do nothing */
				continue;
			if ( dev->resource[i].start < 0x100 )
				/*
				 * Below 0x100 is only standard PC hardware
				 * (pics, kbd, timer, dma, ...)
				 * We should not get resource conflicts there,
				 * and the kernel reserves these anyway
				 * (see arch/i386/kernel/setup.c).
				 * So, do nothing
				 */
				continue;
			if ( dev->resource[i].end < dev->resource[i].start )
				/* invalid endpoint */
				/* Do nothing */
				continue;
			reserve_ioport_range(
				dev->slot_name,
				dev->resource[i].start,
				dev->resource[i].end
			);
		} else if (dev->resource[i].flags & IORESOURCE_MEM) {
			/* iomem */
			/* For now do nothing */
			continue;
		} else {
			/* Neither ioport nor iomem */
			/* Do nothing */
			continue;
		}
	}

	return;
}

static void __init reserve_resources( void )
{
	struct pci_dev *dev;

	pnpbios_for_each_dev(dev) {
		if (
			0 != strcmp(dev->slot_name,"PNP0c01") &&  /* memory controller */
			0 != strcmp(dev->slot_name,"PNP0c02")     /* system peripheral: other */
		) {
			continue;
		}  
		reserve_resources_of_dev(dev);
	}

	return;
}


/* 
 *
 * INIT AND EXIT
 *
 */

extern int is_sony_vaio_laptop;

static int pnpbios_disabled; /* = 0 */
static int dont_reserve_resources; /* = 0 */
int pnpbios_dont_use_current_config; /* = 0 */

#ifndef MODULE
static int __init pnpbios_setup(char *str)
{
	int invert;

	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			pnpbios_disabled=1;
		if (strncmp(str, "on", 2) == 0)
			pnpbios_disabled=0;
		invert = (strncmp(str, "no-", 3) == 0);
		if (invert)
			str += 3;
		if (strncmp(str, "curr", 4) == 0)
			pnpbios_dont_use_current_config = invert;
		if (strncmp(str, "res", 3) == 0)
			dont_reserve_resources = invert;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}

	return 1;
}

__setup("pnpbios=", pnpbios_setup);
#endif

int __init pnpbios_init(void)
{
	union pnp_bios_expansion_header *check;
	u8 sum;
	int i, length, r;

	spin_lock_init(&pnp_bios_lock);
	spin_lock_init(&pnpbios_devices_lock);

	if(pnpbios_disabled || (dmi_broken & BROKEN_PNP_BIOS) ) {
		printk(KERN_INFO "PnPBIOS: Disabled\n");
		return -ENODEV;
	}

	if ( is_sony_vaio_laptop )
		pnpbios_dont_use_current_config = 1;

	/*
 	 * Search the defined area (0xf0000-0xffff0) for a valid PnP BIOS
	 * structure and, if one is found, sets up the selectors and
	 * entry points
	 */
	for (check = (union pnp_bios_expansion_header *) __va(0xf0000);
	     check < (union pnp_bios_expansion_header *) __va(0xffff0);
	     ((void *) (check)) += 16) {
		if (check->fields.signature != PNP_SIGNATURE)
			continue;
		length = check->fields.length;
		if (!length)
			continue;
		for (sum = 0, i = 0; i < length; i++)
			sum += check->chars[i];
		if (sum)
			continue;
		if (check->fields.version < 0x10) {
			printk(KERN_WARNING "PnPBIOS: PnP BIOS version %d.%d is not supported\n",
			       check->fields.version >> 4,
			       check->fields.version & 15);
			continue;
		}
		printk(KERN_INFO "PnPBIOS: Found PnP BIOS installation structure at 0x%p\n", check);
		printk(KERN_INFO "PnPBIOS: PnP BIOS version %d.%d, entry 0x%x:0x%x, dseg 0x%x\n",
                       check->fields.version >> 4, check->fields.version & 15,
		       check->fields.pm16cseg, check->fields.pm16offset,
		       check->fields.pm16dseg);
		Q2_SET_SEL(PNP_CS32, &pnp_bios_callfunc, 64 * 1024);
		Q_SET_SEL(PNP_CS16, check->fields.pm16cseg, 64 * 1024);
		Q_SET_SEL(PNP_DS, check->fields.pm16dseg, 64 * 1024);
		pnp_bios_callpoint.offset = check->fields.pm16offset;
		pnp_bios_callpoint.segment = PNP_CS16;
		pnp_bios_hdr = check;
		break;
	}
	if (!pnp_bios_present())
		return -ENODEV;
	build_devlist();
	if ( ! dont_reserve_resources )
		reserve_resources();
#ifdef CONFIG_PROC_FS
	r = pnpbios_proc_init();
	if (r)
		return r;
#endif
	return 0;
}

static int pnpbios_thread_init(void)
{
#ifdef CONFIG_HOTPLUG	
	init_completion(&unload_sem);
	if(kernel_thread(pnp_dock_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL)>0)
		unloading = 0;
#endif		
	return 0;
}

#ifndef MODULE

/* init/main.c calls pnpbios_init early */

/* Start the kernel thread later: */
module_init(pnpbios_thread_init);

#else

/*
 * N.B.: Building pnpbios as a module hasn't been fully implemented
 */

MODULE_LICENSE("GPL");

static int pnpbios_init_all(void)
{
	int r;
	r = pnpbios_init();
	if (r)
		return r;
	r = pnpbios_thread_init();
	if (r)
		return r;
	return 0;
}

static void __exit pnpbios_exit(void)
{
#ifdef CONFIG_HOTPLUG
	unloading = 1;
	wait_for_completion(&unload_sem);
#endif
	pnpbios_proc_exit();
	/* We ought to free resources here */
	return;
}

module_init(pnpbios_init_all);
module_exit(pnpbios_exit);

#endif
