/*
 * eeh.c
 * Copyright (C) 2001 Dave Engebretsen & Todd Inglett IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Change Activity:
 * 2001/10/27 : engebret : Created.
 * End Change Activity 
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/processor.h>
#include "pci.h"

#undef DEBUG

/** Overview:
 *  EEH, or "Extended Error Handling" is a PCI bridge technology for
 *  dealing with PCI bus errors that can't be dealt with within the
 *  usual PCI framework, except by check-stopping the CPU.  Systems
 *  that are designed for high-availability/reliability cannot afford
 *  to crash due to a "mere" PCI error, thus the need for EEH.
 *  An EEH-capable bridge operates by converting a detected error
 *  into a "slot freeze", taking the PCI adapter off-line, making
 *  the slot behave, from the OS'es point of view, as if the slot
 *  were "empty": all reads return 0xff's and all writes are silently
 *  ignored.  EEH slot isolation events can be triggered by parity
 *  errors on the address or data busses (e.g. during posted writes),
 *  which in turn might be caused by dust, vibration, humidity,
 *  radioactivity or plain-old failed hardware.
 *
 *  Note, however, that one of the leading causes of EEH slot
 *  freeze events are buggy device drivers, buggy device microcode,
 *  or buggy device hardware.  This is because any attempt by the
 *  device to bus-master data to a memory address that is not
 *  assigned to the device will trigger a slot freeze.   (The idea
 *  is to prevent devices-gone-wild from corrupting system memory).
 *  Buggy hardware/drivers will have a miserable time co-existing
 *  with EEH.
 *
 *  Ideally, a PCI device driver, when suspecting that an isolation
 *  event has occured (e.g. by reading 0xff's), will then ask EEH
 *  whether this is the case, and then take appropriate steps to
 *  reset the PCI slot, the PCI device, and then resume operations.
 *  However, until that day,  the checking is done here, with the
 *  eeh_check_failure() routine embedded in the MMIO macros.  If
 *  the slot is found to be isolated, an "EEH Event" is synthesized
 *  and sent out for processing.
 */

#define BUID_HI(buid) ((buid) >> 32)
#define BUID_LO(buid) ((buid) & 0xffffffff)
#define CONFIG_ADDR(busno, devfn) (((((busno) & 0xff) << 8) | ((devfn) & 0xf8)) << 8)

unsigned long eeh_total_mmio_ffs;
unsigned long eeh_false_positives;
unsigned long eeh_ignored_failures;
unsigned long eeh_device_not_found;
unsigned long eeh_node_not_found;
unsigned long eeh_config_not_found;
unsigned long eeh_no_check;

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;
static int ibm_slot_error_detail;

/* Buffer for reporting slot-error-detail rtas calls */
static unsigned char slot_errbuf[RTAS_ERROR_LOG_MAX];
static spinlock_t slot_errbuf_lock = SPIN_LOCK_UNLOCKED;
static int eeh_error_buf_size;

static int eeh_subsystem_enabled;
#define EEH_MAX_OPTS 4096
static char *eeh_opts;
static int eeh_opts_last;

pte_t *find_linux_pte(pgd_t *pgdir, unsigned long va);	/* from htab.c */
static int eeh_check_opts_config(struct device_node *dn,
				 int class_code, int vendor_id, int device_id,
				 int default_state);

/* ------------------------------------------------------------- */
/**
 * The pci address cache subsystem.  This subsystem places
 * PCI device address resources into a red-black tree, sorted
 * according to the address range, so that given only an i/o
 * address, the corresponding PCI device can be **quickly**
 * found. It is safe to perform an address lookup in an interrupt
 * context; this ability is an important feature.
 *
 * Currently, the only customer of this code is the EEH subsystem;
 * thus, this code has been somewhat tailored to suit EEH better.
 * In particular, the cache does *not* hold the addresses of devices
 * for which EEH is not enabled.
 *
 * (Implementation Note: The RB tree seems to be better/faster
 * than any hash algo I could think of for this problem, even
 * with the penalty of slow pointer chases for d-cache misses).
 */
struct pci_io_addr_range
{
	struct rb_node_s rb_node;
	unsigned long addr_lo;
	unsigned long addr_hi;
	struct pci_dev *pcidev;
	unsigned int flags;
};

static struct pci_io_addr_cache
{
	struct rb_root_s rb_root;
	spinlock_t piar_lock;
} pci_io_addr_cache_root;

static inline struct pci_dev *__pci_get_device_by_addr(unsigned long addr)
{
	struct rb_node_s *n = pci_io_addr_cache_root.rb_root.rb_node;

	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (addr < piar->addr_lo) {
			n = n->rb_left;
		} else {
			if (addr > piar->addr_hi) {
				n = n->rb_right;
			} else {
				return piar->pcidev;
			}
		}
	}

	return NULL;
}

/**
 * pci_get_device_by_addr - Get device, given only address
 * @addr: mmio (PIO) phys address or i/o port number
 *
 * Given an mmio phys address, or a port number, find a pci device
 * that implements this address.  Be sure to pci_dev_put the device
 * when finished.  I/O port numbers are assumed to be offset
 * from zero (that is, they do *not* have pci_io_addr added in).
 * It is safe to call this function within an interrupt.
 */
static struct pci_dev *pci_get_device_by_addr(unsigned long addr)
{
	struct pci_dev *dev;
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	dev = __pci_get_device_by_addr(addr);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
	return dev;
}

#ifdef DEBUG
/*
 * Handy-dandy debug print routine, does nothing more
 * than print out the contents of our addr cache.
 */
static void pci_addr_cache_print(struct pci_io_addr_cache *cache)
{
	struct rb_node_s *n;
	int cnt = 0;

	n = rb_first(&cache->rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);
		printk(KERN_INFO "PCI: %s addr range %d [%lx-%lx]: %s %s\n",
		       (piar->flags & IORESOURCE_IO) ? "i/o" : "mem", cnt,
		       piar->addr_lo, piar->addr_hi, pci_name(piar->pcidev),
		       piar->pcidev->name);
		cnt++;
		n = rb_next(n);
	}
}
#endif

/* Insert address range into the rb tree. */
static struct pci_io_addr_range *
pci_addr_cache_insert(struct pci_dev *dev, unsigned long alo,
		      unsigned long ahi, unsigned int flags)
{
	struct rb_node_s **p = &pci_io_addr_cache_root.rb_root.rb_node;
	struct rb_node_s *parent = NULL;
	struct pci_io_addr_range *piar;

#ifdef DEBUG
	printk(KERN_INFO "PCI: add %s %s [%lx:%lx] to addr cache\n",
		       pci_name(dev), dev->name, alo, ahi);
#endif
	/* Walk tree, find a place to insert into tree */
	while (*p) {
		parent = *p;
		piar = rb_entry(parent, struct pci_io_addr_range, rb_node);
		if (alo < piar->addr_lo) {
			p = &parent->rb_left;
		} else if (ahi > piar->addr_hi) {
			p = &parent->rb_right;
		} else {
			if (dev != piar->pcidev ||
			    alo != piar->addr_lo || ahi != piar->addr_hi) {
				printk(KERN_WARNING "PIAR: overlapping address range\n");
			}
			return piar;
		}
	}
	piar = (struct pci_io_addr_range *)kmalloc(sizeof(struct pci_io_addr_range), GFP_ATOMIC);
	if (!piar)
		return NULL;

	piar->addr_lo = alo;
	piar->addr_hi = ahi;
	piar->pcidev = dev;
	piar->flags = flags;

	rb_link_node(&piar->rb_node, parent, p);
	rb_insert_color(&piar->rb_node, &pci_io_addr_cache_root.rb_root);

	return piar;
}

static void __pci_addr_cache_insert_device(struct pci_dev *dev)
{
	struct device_node *dn;
	int i;

	dn = pci_device_to_OF_node(dev);
	if (!dn) {
		printk(KERN_WARNING "PCI: no pci dn found for dev=%s\n",
			pci_name(dev));
		return;
	}
#ifdef DEBUG
	printk(KERN_INFO "PCI: adding %s %s to address cache\n",
		       pci_name(dev), dev->name);
#endif

	/* Skip any devices for which EEH is not enabled. */
	if (!(dn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    dn->eeh_mode & EEH_MODE_NOCHECK) {
#ifdef DEBUG
		printk(KERN_INFO "PCI: skip building address cache for=%s %s\n",
		       pci_name(dev), dev->name);
#endif
		return;
	}

	/* Walk resources on this device, poke them into the tree */
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		unsigned long start = pci_resource_start(dev,i);
		unsigned long end = pci_resource_end(dev,i);
		unsigned int flags = pci_resource_flags(dev,i);

		/* We are interested only bus addresses, not dma or other stuff */
		if (0 == (flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if (start == 0 || ~start == 0 || end == 0 || ~end == 0)
			 continue;
		pci_addr_cache_insert(dev, start, end, flags);
	}
}

/**
 * pci_addr_cache_insert_device - Add a device to the address cache
 * @dev: PCI device whose I/O addresses we are interested in.
 *
 * In order to support the fast lookup of devices based on addresses,
 * we maintain a cache of devices that can be quickly searched.
 * This routine adds a device to that cache.
 */
void pci_addr_cache_insert_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_insert_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}


static inline void __pci_addr_cache_remove_device(struct pci_dev *dev)
{
	struct rb_node_s *n;

restart:
	n = rb_first(&pci_io_addr_cache_root.rb_root);
	while (n) {
		struct pci_io_addr_range *piar;
		piar = rb_entry(n, struct pci_io_addr_range, rb_node);

		if (piar->pcidev == dev) {
			rb_erase(n, &pci_io_addr_cache_root.rb_root);
			kfree(piar);
			goto restart;
		}
		n = rb_next(n);
	}
}

/**
 * pci_addr_cache_remove_device - remove pci device from addr cache
 * @dev: device to remove
 *
 * Remove a device from the addr-cache tree.
 * This is potentially expensive, since it will walk
 * the tree multiple times (once per resource).
 * But so what; device removal doesn't need to be that fast.
 */
void pci_addr_cache_remove_device(struct pci_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_io_addr_cache_root.piar_lock, flags);
	__pci_addr_cache_remove_device(dev);
	spin_unlock_irqrestore(&pci_io_addr_cache_root.piar_lock, flags);
}

/**
 * pci_addr_cache_build - Build a cache of I/O addresses
 *
 * Build a cache of pci i/o addresses.  This cache will be used to
 * find the pci device that corresponds to a given address.
 * This routine scans all pci busses to build the cache.
 * Must be run late in boot process, after the pci controllers
 * have been scaned for devices (after all device resources are known).
 */
void __init pci_addr_cache_build(void)
{
	struct pci_dev *dev = NULL;

	spin_lock_init(&pci_io_addr_cache_root.piar_lock);

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		/* Ignore PCI bridges ( XXX why ??) */
		if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE) {
			continue;
		}
		pci_addr_cache_insert_device(dev);
	}

#ifdef DEBUG
	/* Verify tree built up above, echo back the list of addrs. */
	pci_addr_cache_print(&pci_io_addr_cache_root);
#endif
}

/* ------------------------------------------------------------- */

unsigned long eeh_token_to_phys(unsigned long token)
{
	if (REGION_ID(token) == EEH_REGION_ID) {
		unsigned long vaddr = IO_TOKEN_TO_ADDR(token);
		pte_t *ptep = find_linux_pte(ioremap_mm.pgd, vaddr);
		unsigned long pa = pte_pagenr(*ptep) << PAGE_SHIFT;
		return pa | (vaddr & (PAGE_SIZE-1));
	} else
		return token;
}

/**
 * eeh_panic - call panic() for an eeh event that cannot be handled.
 * The philosophy of this routine is that it is better to panic and
 * halt the OS than it is to risk possible data corruption by
 * oblivious device drivers that don't know better.
 *
 * @dev pci device that had an eeh event
 * @reset_state current reset state of the device slot
 */
static void eeh_panic(struct device_node *dn, int reset_state)
{
	/*
	 * XXX We should create a seperate sysctl for this.
	 *
	 * Since the panic_on_oops sysctl is used to halt the system
	 * in light of potential corruption, we can use it here.
	 */
	if (panic_on_oops)
		panic("EEH: MMIO failure (%d) on device:%s %s\n",
		      reset_state, dn->name, dn->full_name);
	else {
		eeh_ignored_failures++;
		printk(KERN_INFO "EEH: Ignored MMIO failure (%d) on device:%s %s\n",
		      reset_state, dn->name, dn->full_name);
	}
}

/**
 * eeh_dn_check_failure - check if all 1's data is due to EEH slot freeze
 * @dn device node
 * @dev pci device, if known
 *
 * Check for an EEH failure for the given device node.  Call this
 * routine if the result of a read was all 0xff's and you want to
 * find out if this is due to an EEH slot freeze event.  This routine
 * will query firmware for the EEH status.
 *
 * Returns 0 if there has not been an EEH error; otherwise returns
 * an error code.
 *
 * Note this routine is safe to call in an interrupt context.
 */
int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev)
{
	int ret;
	unsigned long rets[2];

	eeh_total_mmio_ffs++;

	if (!eeh_subsystem_enabled)
		return 0;

	if (!dn) {
		eeh_node_not_found++;
		return 0;
	}

	/* Access to IO BARs might get this far and still not want checking. */
	if (!(dn->eeh_mode & EEH_MODE_SUPPORTED) ||
	    dn->eeh_mode & EEH_MODE_NOCHECK) {
		eeh_no_check++;
		return 0;
	}

	if (!dn->eeh_config_addr) {
		eeh_config_not_found++;
		return 0;
	}

	/*
	 * Now test for an EEH failure.  This is VERY expensive.
	 * Note that the eeh_config_addr may be a parent device
	 * in the case of a device behind a bridge, or it may be
	 * function zero of a multi-function device.
	 * In any case they must share a common PHB.
	 */
	ret = rtas_call(ibm_read_slot_reset_state, 3, 3, rets,
			dn->eeh_config_addr, BUID_HI(dn->phb->buid),
			BUID_LO(dn->phb->buid));

	if (ret == 0 && rets[1] == 1 && rets[0] >= 2) {
		int reset_state = rets[0];
		unsigned long flags;
		int rc;

		spin_lock_irqsave(&slot_errbuf_lock, flags);
		memset(slot_errbuf, 0, eeh_error_buf_size);

		rc = rtas_call(ibm_slot_error_detail,
		                      8, 1, NULL, dn->eeh_config_addr,
		                      BUID_HI(dn->phb->buid),
		                      BUID_LO(dn->phb->buid), NULL, 0,
		                      virt_to_phys(slot_errbuf),
		                      eeh_error_buf_size,
		                      1 /* Temporary Error */);

		if (rc == 0)
			log_error(slot_errbuf, ERR_TYPE_RTAS_LOG, 0);
		spin_unlock_irqrestore(&slot_errbuf_lock, flags);

		/* Prevent repeated reports of this failure */
		dn->eeh_mode |= EEH_MODE_NOCHECK;
		
		/* For non-recoverable errors, we panic now.  This
		 * prevents the device driver from getting tangled
		 * in its own shorts.  */
		eeh_panic(dn, reset_state);
		return -EIO;
	} else {
		eeh_false_positives++;
	}

	return 0;
}

/**
 * eeh_check_failure - check if all 1's data is due to EEH slot freeze
 * @token i/o token, should be address in the form 0xA.... (or 0xE.... for
 *        mappings set up before mem init i.e. PHB I/O space)
 * @val value, should be all 1's (XXX why do we need this arg??)
 *
 * Check for an eeh failure at the given token address.
 * Check for an EEH failure at the given token address.  Call this
 * routine if the result of a read was all 0xff's and you want to
 * find out if this is due to an EEH slot freeze event.  This routine
 * will query firmware for the EEH status.
 *
 * Note this routine is safe to call in an interrupt context.
 */
unsigned long eeh_check_failure(void *token, unsigned long val)
{
	unsigned long addr;
	struct pci_dev *dev;
	struct device_node *dn;

	/* If token is an I/O address, we know it was passed to us
	 * by a port I/O function (inb, inw, or inl), so subtracting
	 * pci_io_base gets the physical address for us.
	 */
	if (REGION_ID((unsigned long)token) == IO_REGION_ID)
		addr = (unsigned long)token - pci_io_base;
	else
		addr = eeh_token_to_phys((unsigned long)token);

	/* Finding the phys addr + pci device; this is pretty quick. */
	dev = pci_get_device_by_addr(addr);
	if (!dev) {
		eeh_device_not_found++;
		return val;
	}

	dn = pci_device_to_OF_node(dev);
	eeh_dn_check_failure (dn, dev);
	
	return val;
}

struct eeh_early_enable_info {
	unsigned int buid_hi;
	unsigned int buid_lo;
	int adapters_enabled;
};

/* Enable eeh for the given device node. */
static void *early_enable_eeh(struct device_node *dn, void *data)
{
	struct eeh_early_enable_info *info = data;
	long ret;
	char *status = get_property(dn, "status", 0);
	u32 *class_code = (u32 *)get_property(dn, "class-code", 0);
	u32 *vendor_id =(u32 *) get_property(dn, "vendor-id", 0);
	u32 *device_id = (u32 *)get_property(dn, "device-id", 0);
	u32 *regs;
	int enable;

	dn->eeh_mode = 0;

	if (status && strcmp(status, "ok") != 0)
		return NULL;	/* ignore devices with bad status */

	/* Ignore bad nodes. */
	if (!class_code || !vendor_id || !device_id)
		return NULL;

	/* There is nothing to check on PCI to ISA bridges */
	if (dn->type && !strcmp(dn->type, "isa")) {
		dn->eeh_mode |= EEH_MODE_NOCHECK;
		return NULL;
	}

	/* Now decide if we are going to "Disable" EEH checking
	 * for this device.  We still run with the EEH hardware active,
	 * but we won't be checking for ff's.  This means a driver
	 * could return bad data (very bad!), an interrupt handler could
	 * hang waiting on status bits that won't change, etc.
	 * But there are a few cases like display devices that make sense.
	 */
	enable = 1;	/* i.e. we will do checking */
	if ((*class_code >> 16) == PCI_BASE_CLASS_DISPLAY)
		enable = 0;

	if (!eeh_check_opts_config(dn, *class_code, *vendor_id, *device_id, enable)) {
		if (enable) {
			printk(KERN_INFO "EEH: %s user requested to run without EEH.\n", dn->full_name);
			enable = 0;
		}
	}

	if (!enable)
		dn->eeh_mode |= EEH_MODE_NOCHECK;

	/* Ok..see if this device supports EEH.  Some do, some don't,
	 * and the only way to find out is to check each and every one. */
	regs = (u32 *)get_property(dn, "reg", 0);
	if (regs) {
		/* First register entry is addr (00BBSS00)  */
		/* Try to enable eeh */
		ret = rtas_call(ibm_set_eeh_option, 4, 1, NULL,
				regs[0], info->buid_hi, info->buid_lo,
				EEH_ENABLE);
		if (ret == 0) {
			info->adapters_enabled++;
			dn->eeh_mode |= EEH_MODE_SUPPORTED;
			dn->eeh_config_addr = regs[0];
		} else {
			/* This device doesn't support EEH, but it may have an
			 * EEH parent, in which case we mark it as supported. */
			if (dn->parent && (dn->parent->eeh_mode & EEH_MODE_SUPPORTED)) {
				/* Parent supports EEH. */
				dn->eeh_mode |= EEH_MODE_SUPPORTED;
				dn->eeh_config_addr = dn->parent->eeh_config_addr;
				return NULL;
			}
		}
	} else {
		printk(KERN_WARNING "EEH: %s: unable to get reg property.\n",
		       dn->full_name);
	}
	return NULL; 
}

/*
 * Initialize eeh by trying to enable it for all of the adapters in the system.
 * As a side effect we can determine here if eeh is supported at all.
 * Note that we leave EEH on so failed config cycles won't cause a machine
 * check.  If a user turns off EEH for a particular adapter they are really
 * telling Linux to ignore errors.
 *
 * We should probably distinguish between "ignore errors" and "turn EEH off"
 * but for now disabling EEH for adapters is mostly to work around drivers that
 * directly access mmio space (without using the macros).
 *
 * The eeh-force-off/on option does literally what it says, so if Linux must
 * avoid enabling EEH this must be done.
 */
void eeh_init(void)
{
	struct device_node *phb;
	struct eeh_early_enable_info info;

	extern char cmd_line[];	/* Very early cmd line parse.  Cheap, but works. */
	char *eeh_force_off = strstr(cmd_line, "eeh-force-off");
	char *eeh_force_on = strstr(cmd_line, "eeh-force-on");

	ibm_set_eeh_option = rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset = rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state = rtas_token("ibm,read-slot-reset-state");
	ibm_slot_error_detail = rtas_token("ibm,slot-error-detail");

	eeh_error_buf_size = rtas_token("rtas-error-log-max");
	if (eeh_error_buf_size == RTAS_UNKNOWN_SERVICE)
		eeh_error_buf_size = RTAS_ERROR_LOG_MAX;
	if (eeh_error_buf_size > RTAS_ERROR_LOG_MAX) {
		printk(KERN_WARNING "EEH: rtas-error-log-max is bigger than "
		       "allocated buffer! (%d vs %d)", eeh_error_buf_size,
		       RTAS_ERROR_LOG_MAX);
		eeh_error_buf_size = RTAS_ERROR_LOG_MAX;
	}

	/* Allow user to force eeh mode on or off -- even if the hardware
	 * doesn't exist.  This allows driver writers to at least test use
	 * of I/O macros even if we can't actually test for EEH failure.
	 */
	if (eeh_force_on > eeh_force_off)
		eeh_subsystem_enabled = 1;
	else if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE)
		return;

	if (eeh_force_off > eeh_force_on) {
		/* User is forcing EEH off.  Be noisy if it is implemented. */
		if (eeh_subsystem_enabled)
			printk(KERN_WARNING "EEH: WARNING: PCI Enhanced I/O Error Handling is user disabled\n");
		eeh_subsystem_enabled = 0;
		return;
	}


	/* Enable EEH for all adapters.  Note that eeh requires buid's */
	init_pci_config_tokens();
	info.adapters_enabled = 0;
	for (phb = find_devices("pci"); phb; phb = phb->next) {
		unsigned long buid;
		buid = get_phb_buid(phb);
		if (buid == 0)
			continue;

		info.buid_lo = BUID_LO(buid);
		info.buid_hi = BUID_HI(buid);
		traverse_pci_devices(phb, early_enable_eeh, NULL, &info);
	}
	if (info.adapters_enabled) {
		printk(KERN_INFO "EEH: PCI Enhanced I/O Error Handling Enabled\n");
		eeh_subsystem_enabled = 1;
	}
}


int eeh_set_option(struct pci_dev *dev, int option)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_controller *phb = PCI_GET_PHB_PTR(dev);

	if (dn == NULL || phb == NULL || phb->buid == 0 || !eeh_subsystem_enabled)
		return -2;

	return rtas_call(ibm_set_eeh_option, 4, 1, NULL,
			 CONFIG_ADDR(dn->busno, dn->devfn),
			 BUID_HI(phb->buid), BUID_LO(phb->buid), option);
}


/* If EEH is implemented, find the PCI device using given phys addr
 * and check to see if eeh failure checking is disabled.
 * Remap the addr (trivially) to the EEH region if not.
 * For addresses not known to PCI the vaddr is simply returned unchanged.
 */
void *eeh_ioremap(unsigned long addr, void *vaddr)
{
	struct pci_dev *dev;
	struct device_node *dn;

	if (!eeh_subsystem_enabled)
		return vaddr;
	dev = pci_find_dev_by_addr(addr);
	if (!dev)
		return vaddr;
	dn = pci_device_to_OF_node(dev);
	if (!dn)
		return vaddr;
	if (dn->eeh_mode & EEH_MODE_NOCHECK)
		return vaddr;

	return (void *)IO_ADDR_TO_TOKEN(vaddr);
}

static int eeh_proc_falsepositive_read(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;
	len = sprintf(page,
		      "eeh_device_not_found=%ld\n"
		      "eeh_node_not_found=%ld\n"
		      "eeh_config_not_found=%ld\n"
		      "eeh_no_check=%ld\n"
		      "eeh_false_positives=%ld\n"
		      "eeh_total_mmio_ffs=%ld\n"
		      "eeh_ignored_failures=%ld\n",
		      eeh_device_not_found,
		      eeh_node_not_found,
		      eeh_config_not_found,
		      eeh_no_check,
		      eeh_false_positives,
		      eeh_total_mmio_ffs, eeh_ignored_failures);
	return len;
}

/* Implementation of /proc/ppc64/eeh
 * For now it is one file showing false positives.
 */
static int __init eeh_init_proc(void)
{
	struct proc_dir_entry *ent = create_proc_entry("ppc64/eeh", S_IRUGO, 0);
	if (ent) {
		ent->nlink = 1;
		ent->data = NULL;
		ent->read_proc = (void *)eeh_proc_falsepositive_read;
	}
	return 0;
}

/*
 * Test if "dev" should be configured on or off.
 * This processes the options literally from left to right.
 * This lets the user specify stupid combinations of options,
 * but at least the result should be very predictable.
 */
static int eeh_check_opts_config(struct device_node *dn,
				 int class_code, int vendor_id, int device_id,
				 int default_state)
{
	char devname[32], classname[32];
	char *strs[8], *s;
	int nstrs, i;
	int ret = default_state;

	/* Build list of strings to match */
	nstrs = 0;
	s = (char *)get_property(dn, "ibm,loc-code", 0);
	if (s)
		strs[nstrs++] = s;
	sprintf(devname, "dev%04x:%04x", vendor_id, device_id);
	strs[nstrs++] = devname;
	sprintf(classname, "class%04x", class_code);
	strs[nstrs++] = classname;
	strs[nstrs++] = "";	/* yes, this matches the empty string */

	/* Now see if any string matches the eeh_opts list.
	 * The eeh_opts list entries start with + or -.
	 */
	for (s = eeh_opts; s && (s < (eeh_opts + eeh_opts_last)); s += strlen(s)+1) {
		for (i = 0; i < nstrs; i++) {
			if (strcasecmp(strs[i], s+1) == 0) {
				ret = (strs[i][0] == '+') ? 1 : 0;
			}
		}
	}
	return ret;
}

/* Handle kernel eeh-on & eeh-off cmd line options for eeh.
 *
 * We support:
 *	eeh-off=loc1,loc2,loc3...
 *
 * and this option can be repeated so
 *      eeh-off=loc1,loc2 eeh-off=loc3
 * is the same as eeh-off=loc1,loc2,loc3
 *
 * loc is an IBM location code that can be found in a manual or
 * via openfirmware (or the Hardware Management Console).
 *
 * We also support these additional "loc" values:
 *
 *	dev#:#    vendor:device id in hex (e.g. dev1022:2000)
 *	class#    class id in hex (e.g. class0200)
 *
 * If no location code is specified all devices are assumed
 * so eeh-off means eeh by default is off.
 */

/* This is implemented as a null separated list of strings.
 * Each string looks like this:  "+X" or "-X"
 * where X is a loc code, vendor:device, class (as shown above)
 * or empty which is used to indicate all.
 *
 * We interpret this option string list so that it will literally
 * behave left-to-right even if some combinations don't make sense.
 */

static int __init eeh_parm(char *str, int state)
{
	char *s, *cur, *curend;
	if (!eeh_opts) {
		eeh_opts = alloc_bootmem(EEH_MAX_OPTS);
		eeh_opts[eeh_opts_last++] = '+'; /* default */
		eeh_opts[eeh_opts_last++] = '\0';
	}
	if (*str == '\0') {
		eeh_opts[eeh_opts_last++] = state ? '+' : '-';
		eeh_opts[eeh_opts_last++] = '\0';
		return 1;
	}
	if (*str == '=')
		str++;
	for (s = str; s && *s != '\0'; s = curend) {
		cur = s;
		while (*cur == ',')
			cur++;	/* ignore empties.  Don't treat as "all-on" or "all-off" */
		curend = strchr(cur, ',');
		if (!curend)
			curend = cur + strlen(cur);
		if (*cur) {
			int curlen = curend-cur;
			if (eeh_opts_last + curlen > EEH_MAX_OPTS-2) {
				printk(KERN_INFO "EEH: sorry...too many eeh cmd line options\n");
				return 1;
			}
			eeh_opts[eeh_opts_last++] = state ? '+' : '-';
			strncpy(eeh_opts+eeh_opts_last, cur, curlen);
			eeh_opts_last += curlen;
			eeh_opts[eeh_opts_last++] = '\0';
		}
	}
	return 1;
}

static int __init eehoff_parm(char *str)
{
	return eeh_parm(str, 0);
}

static int __init eehon_parm(char *str)
{
	return eeh_parm(str, 1);
}

__initcall(eeh_init_proc);
__setup("eeh-off", eehoff_parm);
__setup("eeh-on", eehon_parm);
