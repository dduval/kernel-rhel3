/*
 * AGPGART module version 0.99
 * Copyright (C) 1999 Jeff Hartmann
 * Copyright (C) 1999 Precision Insight, Inc.
 * Copyright (C) 1999 Xi Graphics, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * JEFF HARTMANN, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * Intel(R) 855GM/852GM and 865G support, added by
 * David Dawes <dawes@tungstengraphics.com>.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>

#include <linux/agp_backend.h>
#include "agp.h"

MODULE_AUTHOR("Jeff Hartmann <jhartmann@precisioninsight.com>");
MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
EXPORT_SYMBOL(agp_free_memory);
EXPORT_SYMBOL(agp_allocate_memory);
EXPORT_SYMBOL(agp_copy_info);
EXPORT_SYMBOL(agp_bind_memory);
EXPORT_SYMBOL(agp_unbind_memory);
EXPORT_SYMBOL(agp_enable);
EXPORT_SYMBOL(agp_backend_acquire);
EXPORT_SYMBOL(agp_backend_release);

static void flush_cache(void);
static int agp_init_one(struct pci_dev *dev);

static struct agp_bridge_data agp_bridge;
static int agp_try_unsupported __initdata = 0;

int agp_memory_reserved;
__u32 *agp_gatt_table;

static inline void flush_cache(void)
{
#if defined(__i386__) || defined(__x86_64__)
	asm volatile ("wbinvd":::"memory");
#elif defined(__alpha__) || defined(__ia64__) || defined(__sparc__)
	/* ??? I wonder if we'll really need to flush caches, or if the
	   core logic can manage to keep the system coherent.  The ARM
	   speaks only of using `cflush' to get things in memory in
	   preparation for power failure.

	   If we do need to call `cflush', we'll need a target page,
	   as we can only flush one page at a time.

	   Ditto for IA-64. --davidm 00/08/07 */
	mb();
#else
#error "Please define flush_cache."
#endif
}

#ifdef CONFIG_SMP
static atomic_t cpus_waiting;

static void ipi_handler(void *null)
{
	flush_cache();
	atomic_dec(&cpus_waiting);
	while (atomic_read(&cpus_waiting) > 0)
		barrier();
}

static void smp_flush_cache(void)
{
	atomic_set(&cpus_waiting, smp_num_cpus - 1);
	if (smp_call_function(ipi_handler, NULL, 1, 0) != 0)
		panic(PFX "timed out waiting for the other CPUs!\n");
	flush_cache();
	while (atomic_read(&cpus_waiting) > 0)
		barrier();
}
#define global_cache_flush smp_flush_cache
#else				/* CONFIG_SMP */
#define global_cache_flush flush_cache
#endif				/* CONFIG_SMP */

int agp_backend_acquire(void)
{
	if (agp_bridge.type == NOT_SUPPORTED) {
		return -EINVAL;
	}
	atomic_inc(&agp_bridge.agp_in_use);

	if (atomic_read(&agp_bridge.agp_in_use) != 1) {
		atomic_dec(&agp_bridge.agp_in_use);
		return -EBUSY;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

void agp_backend_release(void)
{
	if (agp_bridge.type == NOT_SUPPORTED) {
		return;
	}
	atomic_dec(&agp_bridge.agp_in_use);
	MOD_DEC_USE_COUNT;
}

/* 
 * Generic routines for handling agp_memory structures -
 * They use the basic page allocation routines to do the
 * brunt of the work.
 */


static void agp_free_key(int key)
{

	if (key < 0) {
		return;
	}
	if (key < MAXKEY) {
		clear_bit(key, agp_bridge.key_list);
	}
}

static int agp_get_key(void)
{
	int bit;

	bit = find_first_zero_bit(agp_bridge.key_list, MAXKEY);
	if (bit < MAXKEY) {
		set_bit(bit, agp_bridge.key_list);
		return bit;
	}
	return -1;
}

static agp_memory *agp_create_memory(int scratch_pages)
{
	agp_memory *new;

	new = kmalloc(sizeof(agp_memory), GFP_KERNEL);

	if (new == NULL) {
		return NULL;
	}
	memset(new, 0, sizeof(agp_memory));
	new->key = agp_get_key();

	if (new->key < 0) {
		kfree(new);
		return NULL;
	}
	new->memory = vmalloc(PAGE_SIZE * scratch_pages);

	if (new->memory == NULL) {
		agp_free_key(new->key);
		kfree(new);
		return NULL;
	}
	new->num_scratch_pages = scratch_pages;
	return new;
}

void agp_free_memory(agp_memory * curr)
{
	int i;

	if ((agp_bridge.type == NOT_SUPPORTED) || (curr == NULL)) {
		return;
	}
	if (curr->is_bound == TRUE) {
		agp_unbind_memory(curr);
	}
	if (curr->type != 0) {
		agp_bridge.free_by_type(curr);
		return;
	}
	if (curr->page_count != 0) {
		for (i = 0; i < curr->page_count; i++) {
			agp_bridge.agp_destroy_page((unsigned long)
					 phys_to_virt(curr->memory[i]));
		}
	}
	agp_free_key(curr->key);
	vfree(curr->memory);
	kfree(curr);
	MOD_DEC_USE_COUNT;
}

#define ENTRIES_PER_PAGE		(PAGE_SIZE / sizeof(unsigned long))

agp_memory *agp_allocate_memory(size_t page_count, u32 type)
{
	int scratch_pages;
	agp_memory *new;
	int i;

	if (agp_bridge.type == NOT_SUPPORTED) {
		return NULL;
	}
	if ((atomic_read(&agp_bridge.current_memory_agp) + page_count) >
	    agp_bridge.max_memory_agp) {
		return NULL;
	}

	if (type != 0) {
		new = agp_bridge.alloc_by_type(page_count, type);
		return new;
	}
      	/* We always increase the module count, since free auto-decrements
	 * it
	 */

      	MOD_INC_USE_COUNT;

	scratch_pages = (page_count + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;

	new = agp_create_memory(scratch_pages);

	if (new == NULL) {
	      	MOD_DEC_USE_COUNT;
		return NULL;
	}
	for (i = 0; i < page_count; i++) {
		new->memory[i] = agp_bridge.agp_alloc_page();

		if (new->memory[i] == 0) {
			/* Free this structure */
			agp_free_memory(new);
			return NULL;
		}
		new->memory[i] = virt_to_phys((void *) new->memory[i]);
		new->page_count++;
	}

	return new;
}

/* End - Generic routines for handling agp_memory structures */

static int agp_return_size(void)
{
	int current_size;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		current_size = A_SIZE_8(temp)->size;
		break;
	case U16_APER_SIZE:
		current_size = A_SIZE_16(temp)->size;
		break;
	case U32_APER_SIZE:
		current_size = A_SIZE_32(temp)->size;
		break;
	case LVL2_APER_SIZE:
		current_size = A_SIZE_LVL2(temp)->size;
		break;
	case FIXED_APER_SIZE:
		current_size = A_SIZE_FIX(temp)->size;
		break;
	default:
		current_size = 0;
		break;
	}

	current_size -= (agp_memory_reserved / (1024*1024));

	return current_size;
}

/* Routine to copy over information structure */

int agp_copy_info(agp_kern_info * info)
{
	memset(info, 0, sizeof(agp_kern_info));
	if (agp_bridge.type == NOT_SUPPORTED) {
		info->chipset = agp_bridge.type;
		return -1;
	}
	info->version.major = agp_bridge.version->major;
	info->version.minor = agp_bridge.version->minor;
	info->device = agp_bridge.dev;
	info->chipset = agp_bridge.type;
	info->mode = agp_bridge.mode;
	info->aper_base = agp_bridge.gart_bus_addr;
	info->aper_size = agp_return_size();
	info->max_memory = agp_bridge.max_memory_agp;
	info->current_memory = atomic_read(&agp_bridge.current_memory_agp);
	info->cant_use_aperture = agp_bridge.cant_use_aperture;
	info->page_mask = ~0UL;
	return 0;
}

/* End - Routine to copy over information structure */

/*
 * Routines for handling swapping of agp_memory into the GATT -
 * These routines take agp_memory and insert them into the GATT.
 * They call device specific routines to actually write to the GATT.
 */

int agp_bind_memory(agp_memory * curr, off_t pg_start)
{
	int ret_val;

	if ((agp_bridge.type == NOT_SUPPORTED) ||
	    (curr == NULL) || (curr->is_bound == TRUE)) {
		return -EINVAL;
	}
	if (curr->is_flushed == FALSE) {
		CACHE_FLUSH();
		curr->is_flushed = TRUE;
	}
	ret_val = agp_bridge.insert_memory(curr, pg_start, curr->type);

	if (ret_val != 0) {
		return ret_val;
	}
	curr->is_bound = TRUE;
	curr->pg_start = pg_start;
	return 0;
}

int agp_unbind_memory(agp_memory * curr)
{
	int ret_val;

	if ((agp_bridge.type == NOT_SUPPORTED) || (curr == NULL)) {
		return -EINVAL;
	}
	if (curr->is_bound != TRUE) {
		return -EINVAL;
	}
	ret_val = agp_bridge.remove_memory(curr, curr->pg_start, curr->type);

	if (ret_val != 0) {
		return ret_val;
	}
	curr->is_bound = FALSE;
	curr->pg_start = 0;
	return 0;
}

/* End - Routines for handling swapping of agp_memory into the GATT */

/* 
 * Driver routines - start
 * Currently this module supports the following chipsets:
 * i810, i815, 440lx, 440bx, 440gx, i830, i840, i845, i850, i860, via vp3,
 * via mvp3, via kx133, via kt133, amd irongate, amd 761, amd 762, ALi M1541,
 * and generic support for the SiS chipsets.
 */

/* Generic Agp routines - Start */

static void agp_generic_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch;
	u8 cap_ptr;

	pci_read_config_dword(agp_bridge.dev,
			      agp_bridge.capndx + 4,
			      &command);

	/*
	 * PASS1: go throu all devices that claim to be
	 *        AGP devices and collect their data.
	 */


	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			pci_read_config_dword(device, cap_ptr + 4, &scratch);

			/* adjust RQ depth */
			command =
			    ((command & ~0xff000000) |
			     min_t(u32, (mode & 0xff000000),
				 min_t(u32, (command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x00000200) &&
			      (scratch & 0x00000200) &&
			      (mode & 0x00000200)))
				command &= ~0x00000200;

			/* disable FW if it's not supported */
			if (!((command & 0x00000010) &&
			      (scratch & 0x00000010) &&
			      (mode & 0x00000010)))
				command &= ~0x00000010;

			if (!((command & 4) &&
			      (scratch & 4) &&
			      (mode & 4)))
				command &= ~0x00000004;

			if (!((command & 2) &&
			      (scratch & 2) &&
			      (mode & 2)))
				command &= ~0x00000002;

			if (!((command & 1) &&
			      (scratch & 1) &&
			      (mode & 1)))
				command &= ~0x00000001;
		}
	}
	/*
	 * PASS2: Figure out the 4X/2X/1X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 4) {
		command &= ~3;	/* 4X */
	}
	if (command & 2) {
		command &= ~5;	/* 2X */
	}
	if (command & 1) {
		command &= ~6;	/* 1X */
	}
	command |= 0x00000100;

	pci_write_config_dword(agp_bridge.dev,
			       agp_bridge.capndx + 8,
			       command);

	/*
	 * PASS3: Go throu all AGP devices and update the
	 *        command registers.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
}

/*
 * Generic routines for AGP 3.0 compliant bridges.
 */

/* Generic AGP 3.0 enabling routines */

struct agp_3_0_dev {
	struct list_head list;
	u8 capndx;
	u32 maxbw;
	struct pci_dev *dev;
};

static void agp_3_0_dev_list_insert(struct list_head *head, struct list_head *new)
{
	struct agp_3_0_dev *cur, *n = list_entry(new, struct agp_3_0_dev, list);
	struct list_head *pos;

	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_0_dev, list);
		if(cur->maxbw > n->maxbw)
			break;
	}
	list_add_tail(new, pos);
}

static void agp_3_0_dev_list_sort(struct agp_3_0_dev *list, unsigned int ndevs)
{
	struct agp_3_0_dev *cur;
	struct pci_dev *dev;
	struct list_head *pos, *tmp, *head = &list->list, *start = head->next;
	u32 nistat;

	INIT_LIST_HEAD(head);

	for(pos = start; pos != head;) {
		cur = list_entry(pos, struct agp_3_0_dev, list);
		dev = cur->dev;

		pci_read_config_dword(dev, cur->capndx + 0x0c, &nistat);
		cur->maxbw = (nistat >> 16) & 0xff;

		tmp = pos;
		pos = pos->next;
		agp_3_0_dev_list_insert(head, tmp);
	}
}

/* 
 * Initialize all isochronous transfer parameters for an AGP 3.0 
 * node (i.e. a host bridge in combination with the adapters 
 * lying behind it...)
 */

static int agp_3_0_isochronous_node_enable(struct agp_bridge_data *bridge,
		struct agp_3_0_dev *dev_list, unsigned int ndevs)
{
	/*
	 * Convenience structure to make the calculations clearer
	 * here.  The field names come straight from the AGP 3.0 spec.
	 */
	struct isoch_data {
		u32 maxbw;
		u32 n;
		u32 y;
		u32 l;
		u32 rq;
		struct agp_3_0_dev *dev;
	};

	struct pci_dev *td = bridge->dev, *dev;
	struct list_head *head = &dev_list->list, *pos;
	struct agp_3_0_dev *cur;
	struct isoch_data *master, target;
	unsigned int cdev = 0;
	u32 mnistat, tnistat, tstatus, mcmd;
	u16 tnicmd, mnicmd;
	u8 mcapndx;
	u32 tot_bw = 0, tot_n = 0, tot_rq = 0, y_max, rq_isoch, rq_async;
	u32 step, rem, rem_isoch, rem_async;
	int ret = 0;

	/*
	 * We'll work with an array of isoch_data's (one for each
	 * device in dev_list) throughout this function.
	 */
	if((master = kmalloc(ndevs * sizeof(*master), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto get_out;
	}

	/*
	 * Sort the device list by maxbw.  We need to do this because the
	 * spec suggests that the devices with the smallest requirements
	 * have their resources allocated first, with all remaining resources
	 * falling to the device with the largest requirement.
	 *
	 * We don't exactly do this, we divide target resources by ndevs
	 * and split them amongst the AGP 3.0 devices.  The remainder of such
	 * division operations are dropped on the last device, sort of like
	 * the spec mentions it should be done.
	 *
	 * We can't do this sort when we initially construct the dev_list
	 * because we don't know until this function whether isochronous
	 * transfers are enabled and consequently whether maxbw will mean
	 * anything.
	 */
	agp_3_0_dev_list_sort(dev_list, ndevs);

	pci_read_config_dword(td, bridge->capndx + 0x0c, &tnistat);
	pci_read_config_dword(td, bridge->capndx+0x4, &tstatus);

	/* Extract power-on defaults from the target */
	target.maxbw = (tnistat >> 16) & 0xff;
	target.n     = (tnistat >> 8)  & 0xff;
	target.y     = (tnistat >> 6)  & 0x3;
	target.l     = (tnistat >> 3)  & 0x7;
	target.rq    = (tstatus >> 24) & 0xff;

	y_max = target.y;

	/*
	 * Extract power-on defaults for each device in dev_list.  Along
	 * the way, calculate the total isochronous bandwidth required
	 * by these devices and the largest requested payload size.
	 */
	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_0_dev, list);
		dev = cur->dev;

		mcapndx = cur->capndx;

		pci_read_config_dword(dev, cur->capndx + 0x0c, &mnistat);

		master[cdev].maxbw = (mnistat >> 16) & 0xff;
		master[cdev].n     = (mnistat >> 8)  & 0xff;
		master[cdev].y     = (mnistat >> 6)  & 0x3;
		master[cdev].dev   = cur;

		tot_bw += master[cdev].maxbw;
		y_max = max(y_max, master[cdev].y);

		cdev++;
	}

	/* Check if this configuration has any chance of working */
	if(tot_bw > target.maxbw) {
		printk(KERN_ERR PFX "isochronous bandwidth required "
			"by AGP 3.0 devices exceeds that which is supported by "
			"the AGP 3.0 bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	target.y = y_max;

	/*
	 * Write the calculated payload size into the target's NICMD
	 * register.  Doing this directly effects the ISOCH_N value
	 * in the target's NISTAT register, so we need to do this now
	 * to get an accurate value for ISOCH_N later.
	 */
	pci_read_config_word(td, bridge->capndx + 0x20, &tnicmd);
	tnicmd &= ~(0x3 << 6);
	tnicmd |= target.y << 6;
	pci_write_config_word(td, bridge->capndx + 0x20, tnicmd);

	/* Reread the target's ISOCH_N */
	pci_read_config_dword(td, bridge->capndx + 0x0c, &tnistat);
	target.n = (tnistat >> 8) & 0xff;

	/* Calculate the minimum ISOCH_N needed by each master */
	for(cdev = 0; cdev < ndevs; cdev++) {
		master[cdev].y = target.y;
		master[cdev].n = master[cdev].maxbw / (master[cdev].y + 1);

		tot_n += master[cdev].n;
	}

	/* Exit if the minimal ISOCH_N allocation among the masters is more
	 * than the target can handle. */
	if(tot_n > target.n) {
		printk(KERN_ERR PFX "number of isochronous "
			"transactions per period required by AGP 3.0 devices "
			"exceeds that which is supported by the AGP 3.0 "
			"bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	/* Calculate left over ISOCH_N capability in the target.  We'll give
	 * this to the hungriest device (as per the spec) */
	rem  = target.n - tot_n;

	/* 
	 * Calculate the minimum isochronous RQ depth needed by each master.
	 * Along the way, distribute the extra ISOCH_N capability calculated
	 * above.
	 */
	for(cdev = 0; cdev < ndevs; cdev++) {
		/*
		 * This is a little subtle.  If ISOCH_Y > 64B, then ISOCH_Y
		 * byte isochronous writes will be broken into 64B pieces.
		 * This means we need to budget more RQ depth to account for
		 * these kind of writes (each isochronous write is actually
		 * many writes on the AGP bus).
		 */
		master[cdev].rq = master[cdev].n;
		if(master[cdev].y > 0x1) {
			master[cdev].rq *= (1 << (master[cdev].y - 1));
		}

		tot_rq += master[cdev].rq;

		if(cdev == ndevs - 1)
			master[cdev].n += rem;
	}

	/* Figure the number of isochronous and asynchronous RQ slots the
	 * target is providing. */
	rq_isoch = (target.y > 0x1) ? target.n * (1 << (target.y - 1)) : target.n;
	rq_async = target.rq - rq_isoch;

	/* Exit if the minimal RQ needs of the masters exceeds what the target
	 * can provide. */
	if(tot_rq > rq_isoch) {
		printk(KERN_ERR PFX "number of request queue slots "
			"required by the isochronous bandwidth requested by "
			"AGP 3.0 devices exceeds the number provided by the "
			"AGP 3.0 bridge!\n");
		ret = -ENODEV;
		goto free_and_exit;
	}

	/* Calculate asynchronous RQ capability in the target (per master) as
	 * well as the total number of leftover isochronous RQ slots. */
	step      = rq_async / ndevs;
	rem_async = step + (rq_async % ndevs);
	rem_isoch = rq_isoch - tot_rq;

	/* Distribute the extra RQ slots calculated above and write our
	 * isochronous settings out to the actual devices. */
	for(cdev = 0; cdev < ndevs; cdev++) {
		cur = master[cdev].dev;
		dev = cur->dev;

		mcapndx = cur->capndx;

		master[cdev].rq += (cdev == ndevs - 1)
		              ? (rem_async + rem_isoch) : step;

		pci_read_config_word(dev, cur->capndx + 0x20, &mnicmd);
		pci_read_config_dword(dev, cur->capndx + 0x8, &mcmd);

		mnicmd &= ~(0xff << 8);
		mnicmd &= ~(0x3  << 6);
		mcmd   &= ~(0xff << 24);

		mnicmd |= master[cdev].n  << 8;
		mnicmd |= master[cdev].y  << 6;
		mcmd   |= master[cdev].rq << 24;

		pci_write_config_dword(dev, cur->capndx + 0x8, mcmd);
		pci_write_config_word(dev, cur->capndx + 0x20, mnicmd);
	}

free_and_exit:
	kfree(master);

get_out:
	return ret;
}

/*
 * This function basically allocates request queue slots among the
 * AGP 3.0 systems in nonisochronous nodes.  The algorithm is
 * pretty stupid, divide the total number of RQ slots provided by the
 * target by ndevs.  Distribute this many slots to each AGP 3.0 device,
 * giving any left over slots to the last device in dev_list.
 */
static void agp_3_0_nonisochronous_node_enable(struct agp_bridge_data *bridge,
		struct agp_3_0_dev *dev_list, unsigned int ndevs)
{
	struct agp_3_0_dev *cur;
	struct list_head *head = &dev_list->list, *pos;
	u32 tstatus, mcmd;
	u32 trq, mrq, rem;
	unsigned int cdev = 0;

	pci_read_config_dword(bridge->dev, bridge->capndx + 0x04, &tstatus);

	trq = (tstatus >> 24) & 0xff;
	mrq = trq / ndevs;

	rem = mrq + (trq % ndevs);

	for(pos = head->next; cdev < ndevs; cdev++, pos = pos->next) {
		cur = list_entry(pos, struct agp_3_0_dev, list);

		pci_read_config_dword(cur->dev, cur->capndx + 0x8, &mcmd);
		mcmd &= ~(0xff << 24);
		mcmd |= ((cdev == ndevs - 1) ? rem : mrq) << 24;
		pci_write_config_dword(cur->dev, cur->capndx + 0x8, mcmd);
	}
}

/*
 * Fully configure and enable an AGP 3.0 host bridge and all the devices
 * lying behind it.
 */
int agp_3_0_node_enable(struct agp_bridge_data *bridge, u32 mode, u32 minor)
{
	struct pci_dev *td = bridge->dev, *dev;
	u8 mcapndx;
	u32 isoch, arqsz, cal_cycle, tmp, rate;
	u32 tstatus, tcmd, mcmd, mstatus, ncapid;
	u32 mmajor, mminor;
	u16 mpstat;
	struct agp_3_0_dev *dev_list, *cur;
	struct list_head *head, *pos;
	unsigned int ndevs = 0;
	int ret = 0;

	/* 
	 * Allocate a head for our AGP 3.0 device list (multiple AGP 3.0
	 * devices are allowed behind a single bridge). 
	 */
	if((dev_list = kmalloc(sizeof(*dev_list), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto get_out;
	}
	head = &dev_list->list;
	INIT_LIST_HEAD(head);

	/* Find all AGP devices, and add them to dev_list. */
	pci_for_each_dev(dev) { 
		mcapndx = pci_find_capability(dev, PCI_CAP_ID_AGP);
		switch ((dev->class >>8) & 0xff00) {
			case 0x0600:    /* Bridge */
				/* Skip bridges. We should call this function for each one. */
				continue;

			case 0x0001:    /* Unclassified device */
				/* Don't know what this is, but log it for investigation. */
				if (mcapndx != 0) {
					printk (KERN_INFO PFX "Wacky, found unclassified AGP device. %x:%x\n",
						dev->vendor, dev->device);
				}
				continue;

			case 0x0300:    /* Display controller */
			case 0x0400:    /* Multimedia controller */
				if (mcapndx == 0)
					continue;

				if((cur = kmalloc(sizeof(*cur), GFP_KERNEL)) == NULL) {
					ret = -ENOMEM;
					goto free_and_exit;
				}
				cur->dev = dev;

				pos = &cur->list;
				list_add(pos, head);
				ndevs++;
				continue;

			default:
				continue;
		}
	}

	/* Extract some power-on defaults from the target */
	pci_read_config_dword(td, bridge->capndx + 0x04, &tstatus);
	isoch     = (tstatus >> 17) & 0x1;
	arqsz     = (tstatus >> 13) & 0x7;
	cal_cycle = (tstatus >> 10) & 0x7;
	rate      = tstatus & 0x7;

	/*
	 * Take an initial pass through the devices lying behind our host
	 * bridge.  Make sure each one is actually an AGP 3.0 device, otherwise
	 * exit with an error message.  Along the way store the AGP 3.0
	 * cap_ptr for each device, the minimum supported cal_cycle, and the
	 * minimum supported data rate.
	 */
	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_0_dev, list);
		dev = cur->dev;
		
		pci_read_config_word(dev, PCI_STATUS, &mpstat);
		if((mpstat & PCI_STATUS_CAP_LIST) == 0)
			continue;

		pci_read_config_byte(dev, PCI_CAPABILITY_LIST, &mcapndx);
		if (mcapndx != 0x00) {
			do {
				pci_read_config_dword(dev, mcapndx, &ncapid);
				if ((ncapid & 0xff) != 0x02)
					mcapndx = (ncapid >> 8) & 0xff;
			}
			while (((ncapid & 0xff) != 0x02) && (mcapndx != 0x00));
		}

		if(mcapndx == 0) {
			printk(KERN_ERR PFX "woah!  Non-AGP device "
				"found on the secondary bus of an AGP 3.0 bridge!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}

		mmajor = (ncapid >> 20) & 0xf;
		mminor = (ncapid >> 16) & 0xf;

		if(mmajor < 3) {
			printk(KERN_ERR PFX "woah!  AGP 2.0 device "
				"found on the secondary bus of an AGP 3.0 "
				"bridge operating with AGP 3.0 electricals!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}

		cur->capndx = mcapndx;

		pci_read_config_dword(dev, cur->capndx + 0x04, &mstatus);

		if(((mstatus >> 3) & 0x1) == 0) {
			printk(KERN_ERR PFX "woah!  AGP 3.0 device "
				"not operating in AGP 3.0 mode found on the "
				"secondary bus of an AGP 3.0 bridge operating "
				"with AGP 3.0 electricals!\n");
			ret = -ENODEV;
			goto free_and_exit;
		}

		tmp = (mstatus >> 10) & 0x7;
		cal_cycle = min(cal_cycle, tmp);

		/* figure the lesser rate */
		tmp = mstatus & 0x7;
		if(tmp < rate) 
			rate = tmp;
			
	}		

	/* Turn rate into something we can actually write out to AGPCMD */
	switch(rate) {
	case 0x1:
	case 0x2:
		break;
	case 0x3:
		rate = 0x2;
		break;
	default:
		printk(KERN_ERR PFX "woah!  Bogus AGP rate (%d) "
			"value found advertised behind an AGP 3.0 bridge!\n", rate);
		ret = -ENODEV;
		goto free_and_exit;
	}

	/*
	 * Call functions to divide target resources amongst the AGP 3.0
	 * masters.  This process is dramatically different depending on
	 * whether isochronous transfers are supported.
	 */
	if (isoch) {
		ret = agp_3_0_isochronous_node_enable(bridge, dev_list, ndevs);
		if (ret) {
			printk(KERN_INFO PFX "Something bad happened setting "
			       "up isochronous xfers.  Falling back to "
			       "non-isochronous xfer mode.\n");
		}
	}
	agp_3_0_nonisochronous_node_enable(bridge, dev_list, ndevs);

	/*
	 * Set the calculated minimum supported cal_cycle and minimum
	 * supported transfer rate in the target's AGPCMD register.
	 * Also set the AGP_ENABLE bit, effectively 'turning on' the
	 * target (this has to be done _before_ turning on the masters).
	 */
	pci_read_config_dword(td, bridge->capndx + 0x8, &tcmd);

	tcmd &= ~(0x7 << 10);
	tcmd &= ~0x7;

	tcmd |= cal_cycle << 10;
	tcmd |= 0x1 << 8;
	tcmd |= rate;

	pci_write_config_dword(td, bridge->capndx + 0x8, tcmd);

	/*
	 * Set the target's advertised arqsz value, the minimum supported
	 * transfer rate, and the AGP_ENABLE bit in each master's AGPCMD
	 * register.
	 */
	list_for_each(pos, head) {
		cur = list_entry(pos, struct agp_3_0_dev, list);
		dev = cur->dev;

		mcapndx = cur->capndx;

		pci_read_config_dword(dev, cur->capndx + 0x8, &mcmd);

		mcmd &= ~(0x7 << 13);
		mcmd &= ~0x7;

		mcmd |= arqsz << 13;
		mcmd |= (1 << 8);
		mcmd |= rate;

		pci_write_config_dword(dev, cur->capndx + 0x8, mcmd);
	}

free_and_exit:
	/* Be sure to free the dev_list */
	for(pos = head->next; pos != head;) {
		cur = list_entry(pos, struct agp_3_0_dev, list);

		pos = pos->next;
		kfree(cur);
	}
	kfree(dev_list);

get_out:
	return ret;
}

void agp_generic_agp_3_0_enable(u32 mode)
{
	u32 ncapid, major, minor, agp_3_0;

	pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx, &ncapid);

	major = (ncapid >> 20) & 0xf;
	minor = (ncapid >> 16) & 0xf;

	printk(KERN_INFO PFX "Found an AGP %d.%d compliant device. \n",
			  major, minor);
	if(major >= 3) {
		pci_read_config_dword(agp_bridge.dev, 
				      agp_bridge.capndx + 0x4, &agp_3_0);
		/* 
		 * Check to see if we are operating in 3.0 mode 
		 */
		if((agp_3_0 >> 3) & 0x1) {
			agp_3_0_node_enable(&agp_bridge, mode, minor);
			return;
		}
	}
	agp_generic_agp_enable(mode);
}

static int agp_generic_create_gatt_table(void)
{
	char *table;
	char *table_end;
	int size;
	int page_order;
	int num_entries;
	int i;
	void *temp;
	struct page *page;
	int err = 0;

	/* The generic routines can't handle 2 level gatt's */
	if (agp_bridge.size_type == LVL2_APER_SIZE) {
		return -EINVAL;
	}

	table = NULL;
	i = agp_bridge.aperture_size_idx;
	temp = agp_bridge.current_size;
	size = page_order = num_entries = 0;

	if (agp_bridge.size_type != FIXED_APER_SIZE) {
		do {
			switch (agp_bridge.size_type) {
			case U8_APER_SIZE:
				size = A_SIZE_8(temp)->size;
				page_order =
				    A_SIZE_8(temp)->page_order;
				num_entries =
				    A_SIZE_8(temp)->num_entries;
				break;
			case U16_APER_SIZE:
				size = A_SIZE_16(temp)->size;
				page_order = A_SIZE_16(temp)->page_order;
				num_entries = A_SIZE_16(temp)->num_entries;
				break;
			case U32_APER_SIZE:
				size = A_SIZE_32(temp)->size;
				page_order = A_SIZE_32(temp)->page_order;
				num_entries = A_SIZE_32(temp)->num_entries;
				break;
				/* This case will never really happen. */
			case FIXED_APER_SIZE:
			case LVL2_APER_SIZE:
			default:
				size = page_order = num_entries = 0;
				break;
			}

			table = (char *) __get_free_pages(GFP_KERNEL,
							  page_order);

			if (table == NULL) {
				i++;
				switch (agp_bridge.size_type) {
				case U8_APER_SIZE:
					agp_bridge.current_size = A_IDX8();
					break;
				case U16_APER_SIZE:
					agp_bridge.current_size = A_IDX16();
					break;
				case U32_APER_SIZE:
					agp_bridge.current_size = A_IDX32();
					break;
					/* This case will never really 
					 * happen. 
					 */
				case FIXED_APER_SIZE:
				case LVL2_APER_SIZE:
				default:
					agp_bridge.current_size =
					    agp_bridge.current_size;
					break;
				}
				temp = agp_bridge.current_size;
			} else {
				agp_bridge.aperture_size_idx = i;
			}
		} while ((table == NULL) &&
			 (i < agp_bridge.num_aperture_sizes));
	} else {
		size = ((aper_size_info_fixed *) temp)->size;
		page_order = ((aper_size_info_fixed *) temp)->page_order;
		num_entries = ((aper_size_info_fixed *) temp)->num_entries;
		table = (char *) __get_free_pages(GFP_KERNEL, page_order);
	}

	if (table == NULL) {
		return -ENOMEM;
	}
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		SetPageReserved(page);

	agp_bridge.gatt_table_real = (u32 *) table;
	agp_gatt_table = (void *)table;
#ifdef CONFIG_X86
	err = change_page_attr(virt_to_page(table), 1<<page_order, PAGE_KERNEL_NOCACHE);
#endif
	if (!err) 
	agp_bridge.gatt_table = ioremap_nocache(virt_to_phys(table),
					(PAGE_SIZE * (1 << page_order)));
	CACHE_FLUSH();

	if (agp_bridge.gatt_table == NULL || err) {
		for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
			ClearPageReserved(page);

		free_pages((unsigned long) table, page_order);

		return -ENOMEM;
	}
	agp_bridge.gatt_bus_addr = virt_to_phys(agp_bridge.gatt_table_real);

	for (i = 0; i < num_entries; i++) {
		agp_bridge.gatt_table[i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	return 0;
}

static int agp_generic_suspend(void)
{
	return 0;
}

static void agp_generic_resume(void)
{
	return;
}

static int agp_generic_free_gatt_table(void)
{
	int page_order;
	char *table, *table_end;
	void *temp;
	struct page *page;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		page_order = A_SIZE_8(temp)->page_order;
		break;
	case U16_APER_SIZE:
		page_order = A_SIZE_16(temp)->page_order;
		break;
	case U32_APER_SIZE:
		page_order = A_SIZE_32(temp)->page_order;
		break;
	case FIXED_APER_SIZE:
		page_order = A_SIZE_FIX(temp)->page_order;
		break;
	case LVL2_APER_SIZE:
		/* The generic routines can't deal with 2 level gatt's */
		return -EINVAL;
		break;
	default:
		page_order = 0;
		break;
	}

	/* Do not worry about freeing memory, because if this is
	 * called, then all agp memory is deallocated and removed
	 * from the table.
	 */

#ifdef CONFIG_X86
	change_page_attr(virt_to_page(agp_bridge.gatt_table_real), 1<<page_order, 
			 PAGE_KERNEL);
#endif
	iounmap(agp_bridge.gatt_table);
	table = (char *) agp_bridge.gatt_table_real;
	table_end = table + ((PAGE_SIZE * (1 << page_order)) - 1);

	for (page = virt_to_page(table); page <= virt_to_page(table_end); page++)
		ClearPageReserved(page);

	free_pages((unsigned long) agp_bridge.gatt_table_real, page_order);
	return 0;
}

static int agp_generic_insert_memory(agp_memory * mem,
				     off_t pg_start, int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;

	switch (agp_bridge.size_type) {
	case U8_APER_SIZE:
		num_entries = A_SIZE_8(temp)->num_entries;
		break;
	case U16_APER_SIZE:
		num_entries = A_SIZE_16(temp)->num_entries;
		break;
	case U32_APER_SIZE:
		num_entries = A_SIZE_32(temp)->num_entries;
		break;
	case FIXED_APER_SIZE:
		num_entries = A_SIZE_FIX(temp)->num_entries;
		break;
	case LVL2_APER_SIZE:
		/* The generic routines can't deal with 2 level gatt's */
		return -EINVAL;
		break;
	default:
		num_entries = 0;
		break;
	}

	num_entries -= agp_memory_reserved/PAGE_SIZE;
	if (num_entries < 0) num_entries = 0;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}
	j = pg_start;

	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		agp_bridge.gatt_table[j] =
			agp_bridge.mask_memory(mem->memory[i], mem->type);
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int agp_generic_remove_memory(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	if (type != 0 || mem->type != 0) {
		/* The generic routines know nothing of memory types */
		return -EINVAL;
	}
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge.gatt_table[i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static agp_memory *agp_generic_alloc_by_type(size_t page_count, int type)
{
	return NULL;
}

static void agp_generic_free_by_type(agp_memory * curr)
{
	if (curr->memory != NULL) {
		vfree(curr->memory);
	}
	agp_free_key(curr->key);
	kfree(curr);
}

/* 
 * Basic Page Allocation Routines -
 * These routines handle page allocation
 * and by default they reserve the allocated 
 * memory.  They also handle incrementing the
 * current_memory_agp value, Which is checked
 * against a maximum value.
 */

static unsigned long agp_generic_alloc_page(void)
{
	struct page * page;
	
	page = alloc_page(GFP_KERNEL);
	if (page == NULL) {
		return 0;
	}
#ifdef CONFIG_X86
	if (change_page_attr(page, 1, PAGE_KERNEL_NOCACHE) < 0) {
		__free_page(page); 
		return 0;
	}
#endif
	get_page(page);
	LockPage(page);
	atomic_inc(&agp_bridge.current_memory_agp);
	return (unsigned long)page_address(page);
}

static void agp_generic_destroy_page(unsigned long addr)
{
	void *pt = (void *) addr;
	struct page *page;

	if (pt == NULL) {
		return;
	}
	
	page = virt_to_page(pt);
#ifdef CONFIG_X86
	change_page_attr(page, 1, PAGE_KERNEL); 
#endif	
	put_page(page);
	UnlockPage(page);
	free_page((unsigned long) pt);
	atomic_dec(&agp_bridge.current_memory_agp);
}

/* End Basic Page Allocation Routines */

void agp_enable(u32 mode)
{
	if (agp_bridge.type == NOT_SUPPORTED) return;
	agp_bridge.agp_enable(mode);
}

/* End - Generic Agp routines */

#ifdef CONFIG_AGP_I810
static aper_size_info_fixed intel_i810_sizes[] =
{
	{64, 16384, 4},
     /* The 32M mode still requires a 64k gatt */
	{32, 8192, 4}
};

#define AGP_DCACHE_MEMORY 1
#define AGP_PHYS_MEMORY   2

static gatt_mask intel_i810_masks[] =
{
	{I810_PTE_VALID, 0},
	{(I810_PTE_VALID | I810_PTE_LOCAL), AGP_DCACHE_MEMORY},
	{I810_PTE_VALID, 0}
};

static struct _intel_i810_private {
	struct pci_dev *i810_dev;	/* device one */
	volatile u8 *registers;
	int num_dcache_entries;
} intel_i810_private;

static int intel_i810_fetch_size(void)
{
	u32 smram_miscc;
	aper_size_info_fixed *values;

	pci_read_config_dword(agp_bridge.dev, I810_SMRAM_MISCC, &smram_miscc);
	values = A_SIZE_FIX(agp_bridge.aperture_sizes);

	if ((smram_miscc & I810_GMS) == I810_GMS_DISABLE) {
		printk(KERN_WARNING PFX "i810 is disabled\n");
		return 0;
	}
	if ((smram_miscc & I810_GFX_MEM_WIN_SIZE) == I810_GFX_MEM_WIN_32M) {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values + 1);
		agp_bridge.aperture_size_idx = 1;
		return values[1].size;
	} else {
		agp_bridge.previous_size =
		    agp_bridge.current_size = (void *) (values);
		agp_bridge.aperture_size_idx = 0;
		return values[0].size;
	}

	return 0;
}

static int intel_i810_configure(void)
{
	aper_size_info_fixed *current_size;
	u32 temp;
	int i;

	current_size = A_SIZE_FIX(agp_bridge.current_size);

	pci_read_config_dword(intel_i810_private.i810_dev, I810_MMADDR, &temp);
	temp &= 0xfff80000;

	intel_i810_private.registers =
	    (volatile u8 *) ioremap(temp, 128 * 4096);

	if ((INREG32(intel_i810_private.registers, I810_DRAM_CTL)
	     & I810_DRAM_ROW_0) == I810_DRAM_ROW_0_SDRAM) {
		/* This will need to be dynamically assigned */
		printk(KERN_INFO PFX "detected 4MB dedicated video ram.\n");
		intel_i810_private.num_dcache_entries = 1024;
	}
	pci_read_config_dword(intel_i810_private.i810_dev, I810_GMADDR, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL,
		 agp_bridge.gatt_bus_addr | I810_PGETBL_ENABLED);
	CACHE_FLUSH();

	if (agp_bridge.needs_scratch_page == TRUE) {
		for (i = 0; i < current_size->num_entries; i++) {
			OUTREG32(intel_i810_private.registers,
				 I810_PTE_BASE + (i * 4),
				 agp_bridge.scratch_page);
		}
	}
	return 0;
}

static void intel_i810_cleanup(void)
{
	OUTREG32(intel_i810_private.registers, I810_PGETBL_CTL, 0);
	iounmap((void *) intel_i810_private.registers);
}

static void intel_i810_tlbflush(agp_memory * mem)
{
	return;
}

static void intel_i810_agp_enable(u32 mode)
{
	return;
}

static int intel_i810_insert_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i, j, num_entries;
	void *temp;

	temp = agp_bridge.current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}
	for (j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j])) {
			return -EBUSY;
		}
	}

	if (type != 0 || mem->type != 0) {
		if ((type == AGP_DCACHE_MEMORY) &&
		    (mem->type == AGP_DCACHE_MEMORY)) {
			/* special insert */
			CACHE_FLUSH();
			for (i = pg_start;
			     i < (pg_start + mem->page_count); i++) {
				OUTREG32(intel_i810_private.registers,
					 I810_PTE_BASE + (i * 4),
					 (i * 4096) | I810_PTE_LOCAL |
					 I810_PTE_VALID);
			}
			CACHE_FLUSH();
			agp_bridge.tlb_flush(mem);
			return 0;
		}
	        if((type == AGP_PHYS_MEMORY) &&
		   (mem->type == AGP_PHYS_MEMORY)) {
		   goto insert;
		}
		return -EINVAL;
	}

insert:
   	CACHE_FLUSH();
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (j * 4),
			 agp_bridge.mask_memory(mem->memory[i], mem->type));
	}
	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int intel_i810_remove_entries(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		OUTREG32(intel_i810_private.registers,
			 I810_PTE_BASE + (i * 4),
			 agp_bridge.scratch_page);
	}

	CACHE_FLUSH();
	agp_bridge.tlb_flush(mem);
	return 0;
}

static agp_memory *intel_i810_alloc_by_type(size_t pg_count, int type)
{
	agp_memory *new;

	if (type == AGP_DCACHE_MEMORY) {
		if (pg_count != intel_i810_private.num_dcache_entries) {
			return NULL;
		}
		new = agp_create_memory(1);

		if (new == NULL) {
			return NULL;
		}
		new->type = AGP_DCACHE_MEMORY;
		new->page_count = pg_count;
		new->num_scratch_pages = 0;
		vfree(new->memory);
	   	MOD_INC_USE_COUNT;
		return new;
	}
	if(type == AGP_PHYS_MEMORY) {
		/* The I810 requires a physical address to program
		 * it's mouse pointer into hardware.  However the
		 * Xserver still writes to it through the agp
		 * aperture
		 */
	   	if (pg_count != 1) {
		   	return NULL;
		}
	   	new = agp_create_memory(1);

		if (new == NULL) {
			return NULL;
		}
	   	MOD_INC_USE_COUNT;
		new->memory[0] = agp_bridge.agp_alloc_page();

		if (new->memory[0] == 0) {
			/* Free this structure */
			agp_free_memory(new);
			return NULL;
		}
		new->memory[0] = virt_to_phys((void *) new->memory[0]);
		new->page_count = 1;
	   	new->num_scratch_pages = 1;
	   	new->type = AGP_PHYS_MEMORY;
	        new->physical = virt_to_phys((void *) new->memory[0]);
	   	return new;
	}
   
	return NULL;
}

static void intel_i810_free_by_type(agp_memory * curr)
{
	agp_free_key(curr->key);
   	if(curr->type == AGP_PHYS_MEMORY) {
	   	agp_bridge.agp_destroy_page((unsigned long)
				 phys_to_virt(curr->memory[0]));
		vfree(curr->memory);
	}
	kfree(curr);
   	MOD_DEC_USE_COUNT;
}

static unsigned long intel_i810_mask_memory(unsigned long addr, int type)
{
	/* Type checking must be done elsewhere */
	return addr | agp_bridge.masks[type].mask;
}

static int __init intel_i810_setup(struct pci_dev *i810_dev)
{
	intel_i810_private.i810_dev = i810_dev;

	agp_bridge.masks = intel_i810_masks;
	agp_bridge.aperture_sizes = (void *) intel_i810_sizes;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;
	agp_bridge.dev_private_data = (void *) &intel_i810_private;
	agp_bridge.needs_scratch_page = TRUE;
	agp_bridge.configure = intel_i810_configure;
	agp_bridge.fetch_size = intel_i810_fetch_size;
	agp_bridge.cleanup = intel_i810_cleanup;
	agp_bridge.tlb_flush = intel_i810_tlbflush;
	agp_bridge.mask_memory = intel_i810_mask_memory;
	agp_bridge.agp_enable = intel_i810_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = intel_i810_insert_entries;
	agp_bridge.remove_memory = intel_i810_remove_entries;
	agp_bridge.alloc_by_type = intel_i810_alloc_by_type;
	agp_bridge.free_by_type = intel_i810_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
}

static aper_size_info_fixed intel_i830_sizes[] =
{
	{128, 32768, 5},
	/* The 64M mode still requires a 128k gatt */
	{64, 16384, 5}
};

static struct _intel_i830_private {
	struct pci_dev *i830_dev;   /* device one */
	volatile u8 *registers;
	int gtt_entries;
} intel_i830_private;

static void intel_i830_init_gtt_entries(void)
{
	u16 gmch_ctrl;
	int gtt_entries;
	u8 rdct;
	int local = 0;
	static const int ddt[4] = { 0, 16, 32, 64 };

	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);

	if (agp_bridge.dev->device != PCI_DEVICE_ID_INTEL_830_M_0 &&
	    agp_bridge.dev->device != PCI_DEVICE_ID_INTEL_845_G_0) {
		switch (gmch_ctrl & I855_GMCH_GMS_MASK) {
		case I855_GMCH_GMS_STOLEN_1M:
			gtt_entries = MB(1) - KB(132);
			break;
		case I855_GMCH_GMS_STOLEN_4M:
			gtt_entries = MB(4) - KB(132);
			break;
		case I855_GMCH_GMS_STOLEN_8M:
			gtt_entries = MB(8) - KB(132);
			break;
		case I855_GMCH_GMS_STOLEN_16M:
			gtt_entries = MB(16) - KB(132);
			break;
		case I855_GMCH_GMS_STOLEN_32M:
			gtt_entries = MB(32) - KB(132);
			break;
		default:
			gtt_entries = 0;
			break;
		}
	} else
	{
		switch (gmch_ctrl & I830_GMCH_GMS_MASK) {
		case I830_GMCH_GMS_STOLEN_512:
			gtt_entries = KB(512) - KB(132);
			break;
		case I830_GMCH_GMS_STOLEN_1024:
			gtt_entries = MB(1) - KB(132);
			break;
		case I830_GMCH_GMS_STOLEN_8192:
			gtt_entries = MB(8) - KB(132);
			break;
		case I830_GMCH_GMS_LOCAL:
			rdct = INREG8(intel_i830_private.registers,
				      I830_RDRAM_CHANNEL_TYPE);
			gtt_entries = (I830_RDRAM_ND(rdct) + 1) *
				      MB(ddt[I830_RDRAM_DDT(rdct)]);
			local = 1;
			break;
		default:
			gtt_entries = 0;
			break;
		}
	}

	if (gtt_entries > 0)
		printk(KERN_INFO PFX "Detected %dK %s memory.\n",
		       gtt_entries / KB(1), local ? "local" : "stolen");
	else
		printk(KERN_INFO PFX
		       "No pre-allocated video memory detected.\n");
	gtt_entries /= KB(4);

	intel_i830_private.gtt_entries = gtt_entries;
}

/* The intel i830 automatically initializes the agp aperture during POST.
 * Use the memory already set aside for in the GTT.
 */
static int intel_i830_create_gatt_table(void)
{
	int page_order;
	aper_size_info_fixed *size;
	int num_entries;
	u32 temp;

	size = agp_bridge.current_size;
	page_order = size->page_order;
	num_entries = size->num_entries;
	agp_bridge.gatt_table_real = 0;

	pci_read_config_dword(intel_i830_private.i830_dev,I810_MMADDR,&temp);
	temp &= 0xfff80000;

	intel_i830_private.registers = (volatile u8 *) ioremap(temp,128 * 4096);
	if (!intel_i830_private.registers) return (-ENOMEM);

	temp = INREG32(intel_i830_private.registers,I810_PGETBL_CTL) & 0xfffff000;
	CACHE_FLUSH();

	/* we have to call this as early as possible after the MMIO base address is known */
	intel_i830_init_gtt_entries();

	agp_bridge.gatt_table = NULL;

	agp_bridge.gatt_bus_addr = temp;

	return(0);
}

/* Return the gatt table to a sane state. Use the top of stolen
 * memory for the GTT.
 */
static int intel_i830_free_gatt_table(void)
{
	return(0);
}

static int intel_i830_fetch_size(void)
{
	u16 gmch_ctrl;
	aper_size_info_fixed *values;

	values = A_SIZE_FIX(agp_bridge.aperture_sizes);

	if (agp_bridge.dev->device != PCI_DEVICE_ID_INTEL_830_M_0 &&
	    agp_bridge.dev->device != PCI_DEVICE_ID_INTEL_845_G_0) {
		agp_bridge.previous_size = agp_bridge.current_size = (void *) values;
		agp_bridge.aperture_size_idx = 0;
		return(values[0].size);
	}
		
	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);
	if ((gmch_ctrl & I830_GMCH_MEM_MASK) == I830_GMCH_MEM_128M) {
		agp_bridge.previous_size = agp_bridge.current_size = (void *) values;
		agp_bridge.aperture_size_idx = 0;
		return(values[0].size);
	} else {
		agp_bridge.previous_size = agp_bridge.current_size = (void *) values;
		agp_bridge.aperture_size_idx = 1;
		return(values[1].size);
	}

	return(0);
}

static int intel_i830_configure(void)
{
	aper_size_info_fixed *current_size;
	u32 temp;
	u16 gmch_ctrl;
	int i;

	current_size = A_SIZE_FIX(agp_bridge.current_size);

	pci_read_config_dword(intel_i830_private.i830_dev,I810_GMADDR,&temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	pci_read_config_word(agp_bridge.dev,I830_GMCH_CTRL,&gmch_ctrl);
	gmch_ctrl |= I830_GMCH_ENABLED;
	pci_write_config_word(agp_bridge.dev,I830_GMCH_CTRL,gmch_ctrl);

	OUTREG32(intel_i830_private.registers,I810_PGETBL_CTL,agp_bridge.gatt_bus_addr | I810_PGETBL_ENABLED);
	CACHE_FLUSH();

	if (agp_bridge.needs_scratch_page == TRUE)
		for (i = intel_i830_private.gtt_entries; i < current_size->num_entries; i++)
			OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (i * 4),agp_bridge.scratch_page);

	return (0);
}

static void intel_i830_cleanup(void)
{
	iounmap((void *) intel_i830_private.registers);
}

static int intel_i830_insert_entries(agp_memory *mem,off_t pg_start,int type)
{
	int i,j,num_entries;
	void *temp;

	temp = agp_bridge.current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;

	if (pg_start < intel_i830_private.gtt_entries) {
		printk (KERN_DEBUG "pg_start == 0x%.8lx,intel_i830_private.gtt_entries == 0x%.8x\n",
				pg_start,intel_i830_private.gtt_entries);

		printk ("Trying to insert into local/stolen memory\n");
		return (-EINVAL);
	}

	if ((pg_start + mem->page_count) > num_entries)
		return (-EINVAL);

	/* The i830 can't check the GTT for entries since its read only,
	 * depend on the caller to make the correct offset decisions.
	 */

	if ((type != 0 && type != AGP_PHYS_MEMORY) ||
		(mem->type != 0 && mem->type != AGP_PHYS_MEMORY))
		return (-EINVAL);

	CACHE_FLUSH();

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++)
		OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (j * 4),
			 agp_bridge.mask_memory(mem->memory[i], mem->type));

	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);

	return(0);
}

static int intel_i830_remove_entries(agp_memory *mem,off_t pg_start,int type)
{
	int i;

	CACHE_FLUSH ();

	if (pg_start < intel_i830_private.gtt_entries) {
		printk ("Trying to disable local/stolen memory\n");
		return (-EINVAL);
	}

	for (i = pg_start; i < (mem->page_count + pg_start); i++)
		OUTREG32(intel_i830_private.registers,I810_PTE_BASE + (i * 4),agp_bridge.scratch_page);

	CACHE_FLUSH();

	agp_bridge.tlb_flush(mem);

	return (0);
}

static agp_memory *intel_i830_alloc_by_type(size_t pg_count,int type)
{
	agp_memory *nw;

	/* always return NULL for now */
	if (type == AGP_DCACHE_MEMORY) return(NULL);

	if (type == AGP_PHYS_MEMORY) {
		unsigned long physical;

		/* The i830 requires a physical address to program
		 * it's mouse pointer into hardware. However the
		 * Xserver still writes to it through the agp
		 * aperture
		 */

		if (pg_count != 1) return(NULL);

		nw = agp_create_memory(1);

		if (nw == NULL) return(NULL);

		MOD_INC_USE_COUNT;
		nw->memory[0] = agp_bridge.agp_alloc_page();
		physical = nw->memory[0];
		if (nw->memory[0] == 0) {
			/* free this structure */
			agp_free_memory(nw);
			return(NULL);
		}

		nw->memory[0] = virt_to_phys((void *) nw->memory[0]);
		nw->page_count = 1;
		nw->num_scratch_pages = 1;
		nw->type = AGP_PHYS_MEMORY;
		nw->physical = virt_to_phys((void *) physical);
		return(nw);
	}

	return(NULL);
}

static int __init intel_i830_setup(struct pci_dev *i830_dev)
{
	intel_i830_private.i830_dev = i830_dev;

	agp_bridge.masks = intel_i810_masks;
	agp_bridge.aperture_sizes = (void *) intel_i830_sizes;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;

	agp_bridge.dev_private_data = (void *) &intel_i830_private;
	agp_bridge.needs_scratch_page = TRUE;

	agp_bridge.configure = intel_i830_configure;
	agp_bridge.fetch_size = intel_i830_fetch_size;
	agp_bridge.cleanup = intel_i830_cleanup;
	agp_bridge.tlb_flush = intel_i810_tlbflush;
	agp_bridge.mask_memory = intel_i810_mask_memory;
	agp_bridge.agp_enable = intel_i810_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;

	agp_bridge.create_gatt_table = intel_i830_create_gatt_table;
	agp_bridge.free_gatt_table = intel_i830_free_gatt_table;

	agp_bridge.insert_memory = intel_i830_insert_entries;
	agp_bridge.remove_memory = intel_i830_remove_entries;
	agp_bridge.alloc_by_type = intel_i830_alloc_by_type;
	agp_bridge.free_by_type = intel_i810_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;

	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return(0);
}

#endif /* CONFIG_AGP_I810 */
 
 #ifdef CONFIG_AGP_INTEL

#endif /* CONFIG_AGP_I810 */

#ifdef CONFIG_AGP_INTEL

static int intel_fetch_size(void)
{
	int i;
	u16 temp;
	aper_size_info_16 *values;

	pci_read_config_word(agp_bridge.dev, INTEL_APSIZE, &temp);
	values = A_SIZE_16(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}


static int intel_8xx_fetch_size(void)
{
	int i;
	u8 temp;
	aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge.dev, INTEL_APSIZE, &temp);

        /* Intel 815 chipsets have a _weird_ APSIZE register with only
         * one non-reserved bit, so mask the others out ... */
        if (agp_bridge.type == INTEL_I815) 
		temp &= (1 << 3);
        
	values = A_SIZE_8(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void intel_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2200);
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);
}


static void intel_8xx_tlbflush(agp_memory * mem)
{
  u32 temp;
  pci_read_config_dword(agp_bridge.dev, INTEL_AGPCTRL, &temp);
  pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, temp & ~(1 << 7));
  pci_read_config_dword(agp_bridge.dev, INTEL_AGPCTRL, &temp);
  pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, temp | (1 << 7));
}


static void intel_cleanup(void)
{
	u16 temp;
	aper_size_info_16 *previous_size;

	previous_size = A_SIZE_16(agp_bridge.previous_size);
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static void intel_8xx_cleanup(void)
{
	u16 temp;
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG, temp & ~(1 << 9));
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static int intel_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_16 *current_size;

	current_size = A_SIZE_16(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_word(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x2280);

	/* paccfg/nbxcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG,
			      (temp2 & ~(1 << 10)) | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_byte(agp_bridge.dev, INTEL_ERRSTS + 1, 7);
	return 0;
}

static int intel_815_configure(void)
{
	u32 temp, addr;
	u8 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
        /* the Intel 815 chipset spec. says that bits 29-31 in the
         * ATTBASE register are reserved -> try not to write them */
        if (agp_bridge.gatt_bus_addr &  INTEL_815_ATTBASE_MASK)
		panic("gatt bus addr too high");
	pci_read_config_dword(agp_bridge.dev, INTEL_ATTBASE, &addr);
	addr &= INTEL_815_ATTBASE_MASK;
	addr |= agp_bridge.gatt_bus_addr;
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE, addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* apcont */
	pci_read_config_byte(agp_bridge.dev, INTEL_815_APCONT, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_815_APCONT,
			      temp2 | (1 << 1));
	/* clear any possible error conditions */
        /* Oddness : this chipset seems to have no ERRSTS register ! */
	return 0;
}

static void intel_820_tlbflush(agp_memory * mem)
{
  return;
}

static void intel_820_cleanup(void)
{
	u8 temp;
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_read_config_byte(agp_bridge.dev, INTEL_I820_RDCR, &temp);
	pci_write_config_byte(agp_bridge.dev, INTEL_I820_RDCR, 
			      temp & ~(1 << 1));
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      previous_size->size_value);
}


static int intel_820_configure(void)
{
	u32 temp;
 	u8 temp2; 
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* global enable aperture access */
	/* This flag is not accessed through MCHCFG register as in */
	/* i850 chipset. */
	pci_read_config_byte(agp_bridge.dev, INTEL_I820_RDCR, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_I820_RDCR, 
			      temp2 | (1 << 1));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I820_ERRSTS, 0x001c); 
	return 0;
}

static int intel_830mp_configure(void)
{
       u32 temp;
       u16 temp2;
       aper_size_info_8 *current_size;

       current_size = A_SIZE_8(agp_bridge.current_size);

       /* aperture size */
       pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
                             current_size->size_value);

       /* address to map to */
       pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
       agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

       /* attbase - aperture base */
       pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
                              agp_bridge.gatt_bus_addr);

       /* agpctrl */
       pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000);

       /* gmch */
       pci_read_config_word(agp_bridge.dev, INTEL_NBXCFG, &temp2);
       pci_write_config_word(agp_bridge.dev, INTEL_NBXCFG,
                             temp2 | (1 << 9));
       /* clear any possible AGP-related error conditions */
       pci_write_config_word(agp_bridge.dev, INTEL_I830_ERRSTS, 0x1c);
       return 0;
}
       


static int intel_840_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I840_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I840_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I840_ERRSTS, 0xc000); 
	return 0;
}

static int intel_845_configure(void)
{
	u32 temp;
	u8 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* agpm */
	pci_read_config_byte(agp_bridge.dev, INTEL_I845_AGPM, &temp2);
	pci_write_config_byte(agp_bridge.dev, INTEL_I845_AGPM,
			      temp2 | (1 << 1));
	/* clear any possible error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I845_ERRSTS, 0x001c); 
	return 0;
}

static void intel_845_resume(void)
{
   intel_845_configure();
}


static int intel_850_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value); 

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr); 

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000); 

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I850_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I850_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I850_ERRSTS, 0x001c); 
	return 0;
}

static int intel_860_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000);

	/* mcgcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_I860_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_I860_MCHCFG,
			      temp2 | (1 << 9));
	/* clear any possible AGP-related error conditions */
	pci_write_config_word(agp_bridge.dev, INTEL_I860_ERRSTS, 0xf700);
	return 0;
}

static int intel_7x05_configure(void)
{
	u32 temp;
	u16 temp2;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, INTEL_APSIZE,
			      current_size->size_value);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, INTEL_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* attbase - aperture base */
	pci_write_config_dword(agp_bridge.dev, INTEL_ATTBASE,
			       agp_bridge.gatt_bus_addr);

	/* agpctrl */
	pci_write_config_dword(agp_bridge.dev, INTEL_AGPCTRL, 0x0000);

	/* mchcfg */
	pci_read_config_word(agp_bridge.dev, INTEL_7505_MCHCFG, &temp2);
	pci_write_config_word(agp_bridge.dev, INTEL_7505_MCHCFG,
			      temp2 | (1 << 9));
	return 0;
}



static unsigned long intel_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static void intel_resume(void)
{
	intel_configure();
}

/* Setup function */
static gatt_mask intel_generic_masks[] =
{
	{0x00000017, 0}
};

static aper_size_info_8 intel_815_sizes[2] =
{
	{64, 16384, 4, 0},
	{32, 8192, 3, 8},
};

static aper_size_info_8 intel_8xx_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static aper_size_info_16 intel_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 32},
	{64, 16384, 4, 48},
	{32, 8192, 3, 56},
	{16, 4096, 2, 60},
	{8, 2048, 1, 62},
	{4, 1024, 0, 63}
};

static aper_size_info_8 intel_830mp_sizes[4] = 
{
  {256, 65536, 6, 0},
  {128, 32768, 5, 32},
  {64, 16384, 4, 48},
  {32, 8192, 3, 56}
};

static int __init intel_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_generic_sizes;
	agp_bridge.size_type = U16_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_configure;
	agp_bridge.fetch_size = intel_fetch_size;
	agp_bridge.cleanup = intel_cleanup;
	agp_bridge.tlb_flush = intel_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = intel_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

static int __init intel_815_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_815_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 2;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_815_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

static int __init intel_820_setup (struct pci_dev *pdev)
{
       agp_bridge.masks = intel_generic_masks;
       agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
       agp_bridge.size_type = U8_APER_SIZE;
       agp_bridge.num_aperture_sizes = 7;
       agp_bridge.dev_private_data = NULL;
       agp_bridge.needs_scratch_page = FALSE;
       agp_bridge.configure = intel_820_configure;
       agp_bridge.fetch_size = intel_8xx_fetch_size;
       agp_bridge.cleanup = intel_820_cleanup;
       agp_bridge.tlb_flush = intel_820_tlbflush;
       agp_bridge.mask_memory = intel_mask_memory;
       agp_bridge.agp_enable = agp_generic_agp_enable;
       agp_bridge.cache_flush = global_cache_flush;
       agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
       agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
       agp_bridge.insert_memory = agp_generic_insert_memory;
       agp_bridge.remove_memory = agp_generic_remove_memory;
       agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
       agp_bridge.free_by_type = agp_generic_free_by_type;
       agp_bridge.agp_alloc_page = agp_generic_alloc_page;
       agp_bridge.agp_destroy_page = agp_generic_destroy_page;
       agp_bridge.suspend = agp_generic_suspend;
       agp_bridge.resume = agp_generic_resume;
       agp_bridge.cant_use_aperture = 0;

       return 0;

       (void) pdev; /* unused */
}

static int __init intel_830mp_setup (struct pci_dev *pdev)
{
       agp_bridge.masks = intel_generic_masks;
       agp_bridge.aperture_sizes = (void *) intel_830mp_sizes;
       agp_bridge.size_type = U8_APER_SIZE;
       agp_bridge.num_aperture_sizes = 4;
       agp_bridge.dev_private_data = NULL;
       agp_bridge.needs_scratch_page = FALSE;
       agp_bridge.configure = intel_830mp_configure;
       agp_bridge.fetch_size = intel_8xx_fetch_size;
       agp_bridge.cleanup = intel_8xx_cleanup;
       agp_bridge.tlb_flush = intel_8xx_tlbflush;
       agp_bridge.mask_memory = intel_mask_memory;
       agp_bridge.agp_enable = agp_generic_agp_enable;
       agp_bridge.cache_flush = global_cache_flush;
       agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
       agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
       agp_bridge.insert_memory = agp_generic_insert_memory;
       agp_bridge.remove_memory = agp_generic_remove_memory;
       agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
       agp_bridge.free_by_type = agp_generic_free_by_type;
       agp_bridge.agp_alloc_page = agp_generic_alloc_page;
       agp_bridge.agp_destroy_page = agp_generic_destroy_page;
       agp_bridge.suspend = agp_generic_suspend;
       agp_bridge.resume = agp_generic_resume;
       agp_bridge.cant_use_aperture = 0;
       return 0;

       (void) pdev; /* unused */
}

static int __init intel_840_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_840_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

static int __init intel_845_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_845_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = intel_845_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

static int __init intel_850_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_850_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

static int __init intel_860_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_860_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;

	(void) pdev; /* unused */
}
static int __init intel_875_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_845_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_3_0_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;

	(void) pdev; /* unused */
}

static int __init intel_7x05_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = intel_generic_masks;
	agp_bridge.aperture_sizes = (void *) intel_8xx_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = intel_7x05_configure;
	agp_bridge.fetch_size = intel_8xx_fetch_size;
	agp_bridge.cleanup = intel_8xx_cleanup;
	agp_bridge.tlb_flush = intel_8xx_tlbflush;
	agp_bridge.mask_memory = intel_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_3_0_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_INTEL */

#ifdef CONFIG_AGP_VIA

static int via_fetch_size(void)
{
	int i;
	u8 temp;
	aper_size_info_8 *values;

	values = A_SIZE_8(agp_bridge.aperture_sizes);
	pci_read_config_byte(agp_bridge.dev, VIA_APSIZE, &temp);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int via_configure(void)
{
	u32 temp;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);
	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      current_size->size_value);
	/* address to map too */
	pci_read_config_dword(agp_bridge.dev, VIA_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* GART control register */
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);

	/* attbase - aperture GATT base */
	pci_write_config_dword(agp_bridge.dev, VIA_ATTBASE,
			    (agp_bridge.gatt_bus_addr & 0xfffff000) | 3);
	return 0;
}

static void via_cleanup(void)
{
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_byte(agp_bridge.dev, VIA_APSIZE,
			      previous_size->size_value);
	/* Do not disable by writing 0 to VIA_ATTBASE, it screws things up
	 * during reinitialization.
	 */
}

static void via_tlbflush(agp_memory * mem)
{
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000008f);
	pci_write_config_dword(agp_bridge.dev, VIA_GARTCTRL, 0x0000000f);
}

static unsigned long via_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static aper_size_info_8 via_generic_sizes[7] =
{
	{256, 65536, 6, 0},
	{128, 32768, 5, 128},
	{64, 16384, 4, 192},
	{32, 8192, 3, 224},
	{16, 4096, 2, 240},
	{8, 2048, 1, 248},
	{4, 1024, 0, 252}
};

static gatt_mask via_generic_masks[] =
{
	{0x00000000, 0}
};

static int __init via_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = via_generic_masks;
	agp_bridge.aperture_sizes = (void *) via_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = via_configure;
	agp_bridge.fetch_size = via_fetch_size;
	agp_bridge.cleanup = via_cleanup;
	agp_bridge.tlb_flush = via_tlbflush;
	agp_bridge.mask_memory = via_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_VIA */

#ifdef CONFIG_AGP_SIS

static int sis_fetch_size(void)
{
	u8 temp_size;
	int i;
	aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge.dev, SIS_APSIZE, &temp_size);
	values = A_SIZE_8(agp_bridge.aperture_sizes);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if ((temp_size == values[i].size_value) ||
		    ((temp_size & ~(0x03)) ==
		     (values[i].size_value & ~(0x03)))) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);

			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}


static void sis_tlbflush(agp_memory * mem)
{
	pci_write_config_byte(agp_bridge.dev, SIS_TLBFLUSH, 0x02);
}

static int sis_configure(void)
{
	u32 temp;
	aper_size_info_8 *current_size;

	current_size = A_SIZE_8(agp_bridge.current_size);
	pci_write_config_byte(agp_bridge.dev, SIS_TLBCNTRL, 0x05);
	pci_read_config_dword(agp_bridge.dev, SIS_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	pci_write_config_dword(agp_bridge.dev, SIS_ATTBASE,
			       agp_bridge.gatt_bus_addr);
	pci_write_config_byte(agp_bridge.dev, SIS_APSIZE,
			      current_size->size_value);
	return 0;
}

static void sis_cleanup(void)
{
	aper_size_info_8 *previous_size;

	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_byte(agp_bridge.dev, SIS_APSIZE,
			      (previous_size->size_value & ~(0x03)));
}

static unsigned long sis_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static aper_size_info_8 sis_generic_sizes[7] =
{
	{256, 65536, 6, 99},
	{128, 32768, 5, 83},
	{64, 16384, 4, 67},
	{32, 8192, 3, 51},
	{16, 4096, 2, 35},
	{8, 2048, 1, 19},
	{4, 1024, 0, 3}
};

static gatt_mask sis_generic_masks[] =
{
	{0x00000000, 0}
};

static int __init sis_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = sis_generic_masks;
	agp_bridge.aperture_sizes = (void *) sis_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = sis_configure;
	agp_bridge.fetch_size = sis_fetch_size;
	agp_bridge.cleanup = sis_cleanup;
	agp_bridge.tlb_flush = sis_tlbflush;
	agp_bridge.mask_memory = sis_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
}

#endif /* CONFIG_AGP_SIS */

#ifdef CONFIG_AGP_AMD

typedef struct _amd_page_map {
	unsigned long *real;
	unsigned long *remapped;
} amd_page_map;

static struct _amd_irongate_private {
	volatile u8 *registers;
	amd_page_map **gatt_pages;
	int num_tables;
} amd_irongate_private;

static int amd_create_page_map(amd_page_map *page_map)
{
	int i;
	int err = 0;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL) {
		return -ENOMEM;
	}
	SetPageReserved(virt_to_page(page_map->real));
	CACHE_FLUSH();
#ifdef CONFIG_X86
	err = change_page_attr(virt_to_page(page_map->real), 1, PAGE_KERNEL_NOCACHE);
#endif
	if (!err) 
	page_map->remapped = ioremap_nocache(virt_to_phys(page_map->real), 
					    PAGE_SIZE);
	if (page_map->remapped == NULL || err) {
		ClearPageReserved(virt_to_page(page_map->real));
		free_page((unsigned long) page_map->real);
		page_map->real = NULL;
		return -ENOMEM;
	}
	CACHE_FLUSH();

	for(i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++) {
		page_map->remapped[i] = agp_bridge.scratch_page;
	}

	return 0;
}

static void amd_free_page_map(amd_page_map *page_map)
{
	iounmap(page_map->remapped);
#ifdef CONFIG_X86
	change_page_attr(virt_to_page(page_map->real), 1, PAGE_KERNEL);
#endif
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void amd_free_gatt_pages(void)
{
	int i;
	amd_page_map **tables;
	amd_page_map *entry;

	tables = amd_irongate_private.gatt_pages;
	for(i = 0; i < amd_irongate_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL) {
				amd_free_page_map(entry);
			}
			kfree(entry);
		}
	}
	kfree(tables);
}

static int amd_create_gatt_pages(int nr_tables)
{
	amd_page_map **tables;
	amd_page_map *entry;
	int retval = 0;
	int i;

	tables = kmalloc((nr_tables + 1) * sizeof(amd_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL) {
		return -ENOMEM;
	}
	memset(tables, 0, sizeof(amd_page_map *) * (nr_tables + 1));
	for (i = 0; i < nr_tables; i++) {
		entry = kmalloc(sizeof(amd_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		memset(entry, 0, sizeof(amd_page_map));
		tables[i] = entry;
		retval = amd_create_page_map(entry);
		if (retval != 0) break;
	}
	amd_irongate_private.num_tables = nr_tables;
	amd_irongate_private.gatt_pages = tables;

	if (retval != 0) amd_free_gatt_pages();

	return retval;
}

/* Since we don't need contigious memory we just try
 * to get the gatt table once
 */

#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge.gart_bus_addr))
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12) 
#define GET_GATT(addr) (amd_irongate_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

static int amd_create_gatt_table(void)
{
	aper_size_info_lvl2 *value;
	amd_page_map page_dir;
	unsigned long addr;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge.current_size);
	retval = amd_create_page_map(&page_dir);
	if (retval != 0) {
		return retval;
	}

	retval = amd_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		amd_free_page_map(&page_dir);
		return retval;
	}

	agp_bridge.gatt_table_real = (u32 *)page_dir.real;
	agp_bridge.gatt_table = (u32 *)page_dir.remapped;
	agp_bridge.gatt_bus_addr = virt_to_bus(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge.dev, AMD_APBASE, &temp);
	addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	agp_bridge.gart_bus_addr = addr;

	/* Calculate the agp offset */
	for(i = 0; i < value->num_entries / 1024; i++, addr += 0x00400000) {
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] =
			virt_to_bus(amd_irongate_private.gatt_pages[i]->real);
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] |= 0x00000001;
	}

	return 0;
}

static int amd_free_gatt_table(void)
{
	amd_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge.gatt_table_real;
	page_dir.remapped = (unsigned long *)agp_bridge.gatt_table;

	amd_free_gatt_pages();
	amd_free_page_map(&page_dir);
	return 0;
}

static int amd_irongate_fetch_size(void)
{
	int i;
	u32 temp;
	aper_size_info_lvl2 *values;

	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = (temp & 0x0000000e);
	values = A_SIZE_LVL2(agp_bridge.aperture_sizes);
	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);

			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int amd_irongate_configure(void)
{
	aper_size_info_lvl2 *current_size;
	u32 temp;
	u16 enable_reg;

	current_size = A_SIZE_LVL2(agp_bridge.current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge.dev, AMD_MMBASE, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	amd_irongate_private.registers = (volatile u8 *) ioremap(temp, 4096);

	/* Write out the address of the gatt table */
	OUTREG32(amd_irongate_private.registers, AMD_ATTBASE,
		 agp_bridge.gatt_bus_addr);

	/* Write the Sync register */
	pci_write_config_byte(agp_bridge.dev, AMD_MODECNTL, 0x80);
   
   	/* Set indexing mode */
   	pci_write_config_byte(agp_bridge.dev, AMD_MODECNTL2, 0x00);

	/* Write the enable register */
	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg | 0x0004);
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write out the size register */
	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = (((temp & ~(0x0000000e)) | current_size->size_value)
		| 0x00000001);
	pci_write_config_dword(agp_bridge.dev, AMD_APSIZE, temp);

	/* Flush the tlb */
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);

	return 0;
}

static void amd_irongate_cleanup(void)
{
	aper_size_info_lvl2 *previous_size;
	u32 temp;
	u16 enable_reg;

	previous_size = A_SIZE_LVL2(agp_bridge.previous_size);

	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg & ~(0x0004));
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write back the previous size and disable gart translation */
	pci_read_config_dword(agp_bridge.dev, AMD_APSIZE, &temp);
	temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
	pci_write_config_dword(agp_bridge.dev, AMD_APSIZE, temp);
	iounmap((void *) amd_irongate_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void amd_irongate_tlbflush(agp_memory * temp)
{
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);
}

static unsigned long amd_irongate_mask_memory(unsigned long addr, int type)
{
	/* Only type 0 is supported by the irongate */

	return addr | agp_bridge.masks[0].mask;
}

static int amd_insert_memory(agp_memory * mem,
			     off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge.current_size)->num_entries;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		if (!PGE_EMPTY(cur_gatt[GET_GATT_OFF(addr)])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] =
			agp_bridge.mask_memory(mem->memory[i], mem->type);
	}
	agp_bridge.tlb_flush(mem);
	return 0;
}

static int amd_remove_memory(agp_memory * mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] = 
			(unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static aper_size_info_lvl2 amd_irongate_sizes[7] =
{
	{2048, 524288, 0x0000000c},
	{1024, 262144, 0x0000000a},
	{512, 131072, 0x00000008},
	{256, 65536, 0x00000006},
	{128, 32768, 0x00000004},
	{64, 16384, 0x00000002},
	{32, 8192, 0x00000000}
};

static gatt_mask amd_irongate_masks[] =
{
	{0x00000001, 0}
};

static int __init amd_irongate_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = amd_irongate_masks;
	agp_bridge.aperture_sizes = (void *) amd_irongate_sizes;
	agp_bridge.size_type = LVL2_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = (void *) &amd_irongate_private;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = amd_irongate_configure;
	agp_bridge.fetch_size = amd_irongate_fetch_size;
	agp_bridge.cleanup = amd_irongate_cleanup;
	agp_bridge.tlb_flush = amd_irongate_tlbflush;
	agp_bridge.mask_memory = amd_irongate_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = amd_create_gatt_table;
	agp_bridge.free_gatt_table = amd_free_gatt_table;
	agp_bridge.insert_memory = amd_insert_memory;
	agp_bridge.remove_memory = amd_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_AMD */

#ifdef CONFIG_AGP_AMD_8151

/* Begin AMD-8151 support */

static u_int64_t pci_read64 (struct pci_dev *dev, int reg)
{
	union {
		u64 full;
		struct {
			u32 high;
			u32 low;
		} split;
	} tmp;
	pci_read_config_dword(dev, reg, &tmp.split.high);
	pci_read_config_dword(dev, reg+4, &tmp.split.low);
	return tmp.full;
}

static void pci_write64 (struct pci_dev *dev, int reg, u64 value)
{
	union {
		u64 full;
		struct {
			u32 high;
			u32 low;
		} split;
	} tmp;
	tmp.full = value;
	pci_write_config_dword(dev, reg, tmp.split.high);
	pci_write_config_dword(dev, reg+4, tmp.split.low);
}


static int x86_64_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	void *temp;
	long tmp;
	u32 pte;
	u64 addr;

	temp = agp_bridge.current_size;

	num_entries = A_SIZE_32(temp)->num_entries;

	num_entries -= agp_memory_reserved>>PAGE_SHIFT;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	/* Make sure we can fit the range in the gatt table. */
	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;

	/* gatt table should be empty. */
	while (j < (pg_start + mem->page_count)) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[j]))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = agp_bridge.mask_memory(mem->memory[i], mem->type);

		tmp = addr;
		BUG_ON(tmp & 0xffffff0000000ffc);
		pte = (tmp & 0x000000ff00000000) >> 28;
		pte |=(tmp & 0x00000000fffff000);
		pte |= 1<<1|1<<0;

		agp_bridge.gatt_table[j] = pte;
	}
	agp_bridge.tlb_flush(mem);
	return 0;
}

/*
 * This hack alters the order element according
 * to the size of a long. It sucks. I totally disown this, even
 * though it does appear to work for the most part.
 */
static aper_size_info_32 x86_64_aperture_sizes[7] =
{
	{32,   8192,   3+(sizeof(long)/8), 0 },
	{64,   16384,  4+(sizeof(long)/8), 1<<1 },
	{128,  32768,  5+(sizeof(long)/8), 1<<2 },
	{256,  65536,  6+(sizeof(long)/8), 1<<1 | 1<<2 },
	{512,  131072, 7+(sizeof(long)/8), 1<<3 },
	{1024, 262144, 8+(sizeof(long)/8), 1<<1 | 1<<3},
	{2048, 524288, 9+(sizeof(long)/8), 1<<2 | 1<<3}
};


/*
 * Get the current Aperture size from the x86-64.
 * Note, that there may be multiple x86-64's, but we just return
 * the value from the first one we find. The set_size functions
 * keep the rest coherent anyway. Or at least should do.
 */
static int amd_x86_64_fetch_size(void)
{
	struct pci_dev *dev;
	int i;
	u32 temp;
	aper_size_info_32 *values;

	pci_for_each_dev(dev) {
		if (dev->bus->number==0 &&
			PCI_FUNC(dev->devfn)==3 &&
			PCI_SLOT(dev->devfn)>=24 && PCI_SLOT(dev->devfn)<=31) {

			pci_read_config_dword(dev, AMD_X86_64_GARTAPERTURECTL, &temp);
			temp = (temp & 0xe);
			values = A_SIZE_32(x86_64_aperture_sizes);

			for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
				if (temp == values[i].size_value) {
					agp_bridge.previous_size =
					    agp_bridge.current_size = (void *) (values + i);

					agp_bridge.aperture_size_idx = i;
					return values[i].size;
				}
			}
		}
	}
	/* erk, couldn't find an x86-64 ? */
	return 0;
}


static void inline flush_x86_64_tlb(struct pci_dev *dev)
{
	u32 tmp;

	pci_read_config_dword (dev, AMD_X86_64_GARTCACHECTL, &tmp);
	tmp |= 1<<0;
	pci_write_config_dword (dev, AMD_X86_64_GARTCACHECTL, tmp);
}


void amd_x86_64_tlbflush(agp_memory * temp)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    PCI_SLOT(dev->devfn) >=24 && PCI_SLOT(dev->devfn) <=31) {
			flush_x86_64_tlb (dev);
		}
	}
}


/*
 * In a multiprocessor x86-64 system, this function gets
 * called once for each CPU.
 */
u64 amd_x86_64_configure (struct pci_dev *hammer, u64 gatt_table)
{
	u64 aperturebase;
	u32 tmp;
	u64 addr, aper_base;

	/* Address to map to */
	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &tmp);
	aperturebase = tmp << 25;
	aper_base = (aperturebase & PCI_BASE_ADDRESS_MEM_MASK);

	/* address of the mappings table */
	addr = (u64) gatt_table;
	addr >>= 12;
	tmp = (u32) addr<<4;
	tmp &= ~0xf;
	pci_write_config_dword (hammer, AMD_X86_64_GARTTABLEBASE, tmp);

	/* Enable GART translation for this hammer. */
	pci_read_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, &tmp);
	tmp &= 0x3f;
	tmp |= 1<<0;
	pci_write_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, tmp);

	/* keep CPU's coherent. */
	flush_x86_64_tlb (hammer);
	
	return aper_base;
}


static aper_size_info_32 amd_8151_sizes[7] =
{
	{2048, 524288, 9, 0x00000000 },	/* 0 0 0 0 0 0 */
	{1024, 262144, 8, 0x00000400 },	/* 1 0 0 0 0 0 */
	{512,  131072, 7, 0x00000600 },	/* 1 1 0 0 0 0 */
	{256,  65536,  6, 0x00000700 },	/* 1 1 1 0 0 0 */
	{128,  32768,  5, 0x00000720 },	/* 1 1 1 1 0 0 */
	{64,   16384,  4, 0x00000730 },	/* 1 1 1 1 1 0 */
	{32,   8192,   3, 0x00000738 } 	/* 1 1 1 1 1 1 */
};

static int amd_8151_configure(void)
{
	struct pci_dev *dev, *hammer=NULL;
	int current_size;
	int tmp, tmp2, i;
	u64 aperbar;
	unsigned long gatt_bus = virt_to_phys(agp_bridge.gatt_table_real);

	/* Configure AGP regs in each x86-64 host bridge. */
	pci_for_each_dev(dev) {
		if (dev->bus->number==0 &&
			PCI_FUNC(dev->devfn)==3 &&
			PCI_SLOT(dev->devfn)>=24 && PCI_SLOT(dev->devfn)<=31) {
			agp_bridge.gart_bus_addr = amd_x86_64_configure(dev,gatt_bus);
			hammer = dev;

			/*
			 * TODO: Cache pci_dev's of x86-64's in private struct to save us
			 * having to scan the pci list each time.
			 */
		}
	}

	if (hammer == NULL) {
		return -ENODEV;
	}

	/* Shadow x86-64 registers into 8151 registers. */

	dev = agp_bridge.dev;
	if (!dev) 
		return -ENODEV;

	current_size = amd_x86_64_fetch_size();

	pci_read_config_dword(dev, AMD_8151_APERTURESIZE, &tmp);
	tmp &= ~(0xfff);

	/* translate x86-64 size bits to 8151 size bits*/
	for (i=0 ; i<7; i++) {
		if (amd_8151_sizes[i].size == current_size)
			tmp |= (amd_8151_sizes[i].size_value) << 3;
	}
	pci_write_config_dword(dev, AMD_8151_APERTURESIZE, tmp);

	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &tmp);
	aperbar = pci_read64 (dev, AMD_8151_VMAPERTURE);
	aperbar |= (tmp & 0x7fff) <<25;
	aperbar &= 0x000000ffffffffff;
	aperbar |= 1<<2;	/* This address is a 64bit ptr FIXME: Make conditional in 32bit mode */
	pci_write64 (dev, AMD_8151_VMAPERTURE, aperbar);

	pci_read_config_dword(dev, AMD_8151_AGP_CTL , &tmp);
	tmp &= ~(AMD_8151_GTLBEN | AMD_8151_APEREN);
	
	pci_read_config_dword(hammer, AMD_X86_64_GARTAPERTURECTL, &tmp2);
	if (tmp2 & AMD_X86_64_GARTEN)
		tmp |= AMD_8151_APEREN;
	// FIXME: bit 7 of AMD_8151_AGP_CTL (GTLBEN) must be copied if set.
	// But where is it set ?
	pci_write_config_dword(dev, AMD_8151_AGP_CTL, tmp);

	return 0;
}


static void amd_8151_cleanup(void)
{
	struct pci_dev *dev;
	u32 tmp;

	pci_for_each_dev(dev) {
		/* disable gart translation */
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    (PCI_SLOT(dev->devfn) >=24) && (PCI_SLOT(dev->devfn) <=31)) {

			pci_read_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, &tmp);
			tmp &= ~(AMD_X86_64_GARTEN);
			pci_write_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, tmp);
		}

		/* Now shadow the disable in the 8151 */
		if (dev->vendor == PCI_VENDOR_ID_AMD &&
			dev->device == PCI_DEVICE_ID_AMD_8151_0) {

			pci_read_config_dword (dev, AMD_8151_AGP_CTL, &tmp);
			tmp &= ~(AMD_8151_APEREN);	
			pci_write_config_dword (dev, AMD_8151_AGP_CTL, tmp);
		}
	}
}



static unsigned long amd_8151_mask_memory(unsigned long addr, int type)
{
	return addr | agp_bridge.masks[0].mask;
}


static gatt_mask amd_8151_masks[] =
{
	{0x00000001, 0}
};


/*
 * Try to configure an AGP v3 capable setup.
 * If we fail (typically because we don't have an AGP v3
 * card in the system) we fall back to the generic AGP v2
 * routines.
 */
static void agp_x86_64_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch; 
	u8 cap_ptr;
	u8 agp_v3;
	u8 v3_devs=0;

	/* FIXME: If 'mode' is x1/x2/x4 should we call the AGPv2 routines directly ?
	 * Messy, as some AGPv3 cards can only do x4 as a minimum.
	 */

	/* PASS1: Count # of devs capable of AGPv3 mode. */
	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			pci_read_config_dword(device, cap_ptr, &scratch);
			scratch &= (1<<20|1<<21|1<<22|1<<23);
			scratch = scratch>>20;
			/* AGP v3 capable ? */
			if (scratch>=3) {
				v3_devs++;
				printk (KERN_INFO "AGP: Found AGPv3 capable device at %d:%d:%d\n",
					device->bus->number, PCI_FUNC(device->devfn), PCI_SLOT(device->devfn));
			} else {
				printk (KERN_INFO "AGP: Meh. version %x AGP device found.\n", scratch);
			}
		}
	}
	/* If not enough, go to AGP v2 setup */
	if (v3_devs<2) {
		printk (KERN_INFO "AGP: Only %d devices found, not enough, trying AGPv2\n", v3_devs);
		return agp_generic_agp_enable(mode);
	} else {
		printk (KERN_INFO "AGP: Enough AGPv3 devices found, setting up...\n");
	}


	pci_read_config_dword(agp_bridge.dev, agp_bridge.capndx + 4, &command);

	/*
	 * PASS2: go through all devices that claim to be
	 *        AGP devices and collect their data.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			printk (KERN_INFO "AGP: Setting up AGPv3 capable device at %d:%d:%d\n",
					device->bus->number, PCI_FUNC(device->devfn), PCI_SLOT(device->devfn));
			pci_read_config_dword(device, cap_ptr + 4, &scratch);
			agp_v3 = (scratch & (1<<3) ) >>3;

			/* adjust RQ depth */
			command =
			    ((command & ~0xff000000) |
			     min_t(u32, (mode & 0xff000000),
				 min_t(u32, (command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x200) && (scratch & 0x200) && (mode & 0x200)))
				command &= ~0x200;

			/* disable FW if it's not supported */
			if (!((command & 0x10) && (scratch & 0x10) && (mode & 0x10)))
				command &= ~0x10;

			if (!((command & 2) && (scratch & 2) && (mode & 2))) {
				command &= ~2;		/* 8x */
				printk (KERN_INFO "AGP: Putting device into 8x mode\n");
			}

			if (!((command & 1) && (scratch & 1) && (mode & 1))) {
				command &= ~1;		/* 4x */
				printk (KERN_INFO "AGP: Putting device into 4x mode\n");
			}
		}
	}
	/*
	 * PASS3: Figure out the 8X/4X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 2)
		command &= ~5;	/* 8X */

	if (command & 1)
		command &= ~6;	/* 4X */

	command |= 0x100;

	pci_write_config_dword(agp_bridge.dev, agp_bridge.capndx + 8, command);

	/*
	 * PASS4: Go through all AGP devices and update the
	 *        command registers.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
}


static int __init amd_8151_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = amd_8151_masks;
	agp_bridge.aperture_sizes = (void *) amd_8151_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = amd_8151_configure;
	agp_bridge.fetch_size = amd_x86_64_fetch_size;
	agp_bridge.cleanup = amd_8151_cleanup;
	agp_bridge.tlb_flush = amd_x86_64_tlbflush;
	agp_bridge.mask_memory = amd_8151_mask_memory;
	agp_bridge.agp_enable = agp_x86_64_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = x86_64_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

/* NVIDIA x86-64 chipset support */
 

static struct _nvidia_x86_64_private {
	struct pci_dev *dev_1;
} nvidia_x86_64_private;


static aper_size_info_32 nvidia_x86_64_sizes[5] =
{
	{512,  131072, 7, 0x00000000 },
	{256,  65536,  6, 0x00000008 },
	{128,  32768,  5, 0x0000000C },
	{64,   16384,  4, 0x0000000E },
	{32,   8192,   3, 0x0000000F }
};


static int nvidia_x86_64_configure(void)
{
	struct pci_dev *dev, *hammer=NULL;
	int i, current_size;
	u32 tmp, apbase, apbar, aplimit;
	unsigned long gatt_bus = virt_to_phys(agp_bridge.gatt_table_real);

	if (!agp_bridge.dev) 
		return -ENODEV;

	/* configure AGP regs in each x86-64 host bridge */
	pci_for_each_dev(dev) {
		if (dev->bus->number==0 &&
			PCI_FUNC(dev->devfn)==3 &&
			PCI_SLOT(dev->devfn)>=24 && PCI_SLOT(dev->devfn)<=31) {
			agp_bridge.gart_bus_addr = amd_x86_64_configure(dev,gatt_bus);
			hammer = dev;
		}
	}
	if (hammer == NULL)
		return -ENODEV;

	/* translate x86-64 aperture size to NVIDIA aperture size */
	current_size = amd_x86_64_fetch_size();
	for (i = 0 ; i < agp_bridge.num_aperture_sizes; i++) {
		if (nvidia_x86_64_sizes[i].size == current_size)
			break;
	}
	/* if x86-64 size does not match any NVIDIA size, exit here */
	if (i == agp_bridge.num_aperture_sizes)
		return -ENODEV;
	pci_read_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APSIZE, &tmp);
	tmp &= ~(0xf);
	tmp |= nvidia_x86_64_sizes[i].size_value;
	pci_write_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APSIZE, tmp);

	/* shadow x86-64 registers into NVIDIA registers */
	pci_read_config_dword (hammer, AMD_X86_64_GARTAPERTUREBASE, &apbase);
	/* if x86-64 aperture base is beyond 4G, exit here */
	if ( (apbase & 0x7fff) >> (32 - 25) )
		 return -ENODEV;
	apbase = (apbase & 0x7fff) << 25;

	/* AK: most likely the shadow into the primary device is not needed */

	pci_read_config_dword(agp_bridge.dev, NVIDIA_X86_64_0_APBASE, &apbar);
	apbar &= ~PCI_BASE_ADDRESS_MEM_MASK;
	apbar |= apbase;
	pci_write_config_dword(agp_bridge.dev, NVIDIA_X86_64_0_APBASE, apbar);

	/* Shadow into secondary device looks dubious, but we keep it for now.
	   If these two could be dropped then the NForce3 code path could
	   be just folded into the generic functions above. */

	aplimit = apbase + (current_size * 1024 * 1024) - 1;
	pci_write_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APBASE1, apbase);
	pci_write_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APLIMIT1, aplimit);
	pci_write_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APBASE2, apbase);
	pci_write_config_dword(nvidia_x86_64_private.dev_1, NVIDIA_X86_64_1_APLIMIT2, aplimit);

	/* Original driver updated the IORR here, but AMD documentation
	   explicitely discourages this for something already covered by the GART. */
	
	return 0;
}


static void nvidia_x86_64_cleanup(void)
{
	struct pci_dev *dev;
	u32 tmp;

	pci_for_each_dev(dev) {
		/* disable gart translation */
		if (dev->bus->number==0 && PCI_FUNC(dev->devfn)==3 &&
		    (PCI_SLOT(dev->devfn) >=24) && (PCI_SLOT(dev->devfn) <=31)) {

			pci_read_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, &tmp);
			tmp &= ~(AMD_X86_64_GARTEN);
			pci_write_config_dword (dev, AMD_X86_64_GARTAPERTURECTL, tmp);
		}
	}
}


static unsigned long nvidia_x86_64_mask_memory(unsigned long addr, int type)
{
	return addr | agp_bridge.masks[0].mask;
}


static gatt_mask nvidia_x86_64_masks[] =
{
	{0x00000001, 0}
};


static int __init nvidia_x86_64_setup (struct pci_dev *pdev)
{
	nvidia_x86_64_private.dev_1 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(11, 0));

	if (nvidia_x86_64_private.dev_1 == NULL) {
		printk(KERN_INFO PFX "agpgart: Detected an NVIDIA "
			"nForce3 chipset, but could not find "
			"the secondary device.\n");
		agp_bridge.type = NOT_SUPPORTED;
		return -ENODEV;
	}

	agp_bridge.masks = nvidia_x86_64_masks;
	agp_bridge.aperture_sizes = (void *) nvidia_x86_64_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 5;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = nvidia_x86_64_configure;
	agp_bridge.fetch_size = amd_x86_64_fetch_size;
	agp_bridge.cleanup = nvidia_x86_64_cleanup;
	agp_bridge.tlb_flush = amd_x86_64_tlbflush;
	agp_bridge.mask_memory = nvidia_x86_64_mask_memory;
	agp_bridge.agp_enable = agp_x86_64_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = x86_64_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_AMD_8151 */

#ifdef CONFIG_AGP_ALI

static int ali_fetch_size(void)
{
	int i;
	u32 temp;
	aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge.dev, ALI_ATTBASE, &temp);
	temp &= ~(0xfffffff0);
	values = A_SIZE_32(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void ali_tlbflush(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge.dev, ALI_TAGCTRL,
			((temp & 0xfffffff0) | 0x00000001|0x00000002));
}

static void ali_cleanup(void)
{
	aper_size_info_32 *previous_size;
	u32 temp;

	previous_size = A_SIZE_32(agp_bridge.previous_size);

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge.dev, ALI_TAGCTRL,
			((temp & 0xffffff00) | 0x00000001|0x00000002));

	pci_read_config_dword(agp_bridge.dev,  ALI_ATTBASE, &temp);
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE,
			((temp & 0x00000ff0) | previous_size->size_value));
}

static int ali_configure(void)
{
	u32 temp;
	aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge.current_size);

	/* aperture size and gatt addr */
	pci_read_config_dword(agp_bridge.dev, ALI_ATTBASE, &temp);
	temp = (((temp & 0x00000ff0) | (agp_bridge.gatt_bus_addr & 0xfffff000))
			| (current_size->size_value & 0xf));
	pci_write_config_dword(agp_bridge.dev, ALI_ATTBASE, temp);

	/* tlb control */

	/*
	 *	Question: Jeff, ALi's patch deletes this:
	 *
	 *	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	 *	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL,
	 *			       ((temp & 0xffffff00) | 0x00000010));
	 *
	 *	and replaces it with the following, which seems to duplicate the
	 *	next couple of lines below it. I suspect this was an oversight,
	 *	but you might want to check up on this?
	 */
	
	pci_read_config_dword(agp_bridge.dev, ALI_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* address to map to */
	pci_read_config_dword(agp_bridge.dev, ALI_APBASE, &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

#if 0
	if (agp_bridge.type == ALI_M1541) {
		u32 nlvm_addr = 0;

		switch (current_size->size_value) {
			case 0:  break;
			case 1:  nlvm_addr = 0x100000;break;
			case 2:  nlvm_addr = 0x200000;break;
			case 3:  nlvm_addr = 0x400000;break;
			case 4:  nlvm_addr = 0x800000;break;
			case 6:  nlvm_addr = 0x1000000;break;
			case 7:  nlvm_addr = 0x2000000;break;
			case 8:  nlvm_addr = 0x4000000;break;
			case 9:  nlvm_addr = 0x8000000;break;
			case 10: nlvm_addr = 0x10000000;break;
			default: break;
		}
		nlvm_addr--;
		nlvm_addr&=0xfff00000;

		nlvm_addr+= agp_bridge.gart_bus_addr;
		nlvm_addr|=(agp_bridge.gart_bus_addr>>12);
		printk(KERN_INFO PFX "nlvm top &base = %8x\n",nlvm_addr);
	}
#endif

	pci_read_config_dword(agp_bridge.dev, ALI_TLBCTRL, &temp);
	temp &= 0xffffff7f;		//enable TLB
	pci_write_config_dword(agp_bridge.dev, ALI_TLBCTRL, temp);

	return 0;
}

static unsigned long ali_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static void ali_cache_flush(void)
{
	global_cache_flush();

	if (agp_bridge.type == ALI_M1541) {
		int i, page_count;
		u32 temp;

		page_count = 1 << A_SIZE_32(agp_bridge.current_size)->page_order;
		for (i = 0; i < PAGE_SIZE * page_count; i += PAGE_SIZE) {
			pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
			pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
					(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
					  (agp_bridge.gatt_bus_addr + i)) |
					    ALI_CACHE_FLUSH_EN));
		}
	}
}


static unsigned long ali_alloc_page(void)
{
	unsigned long p = agp_generic_alloc_page(); 
	if (!p) 
		return 0;

	/* probably not needed anymore */
	global_cache_flush();

	if (agp_bridge.type == ALI_M1541) {
		u32 temp;
		pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys((void *)p)) |
				    ALI_CACHE_FLUSH_EN ));
	}
	return p;
}

static void ali_destroy_page(unsigned long addr)
{
	u32 temp;
	void *pt = (void *) addr;

	if (pt == NULL)
		return;

	global_cache_flush();

	if (agp_bridge.type == ALI_M1541) {
		pci_read_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge.dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys(pt)) |
				    ALI_CACHE_FLUSH_EN));
	}

	agp_generic_destroy_page(addr);
}

/* Setup function */
static gatt_mask ali_generic_masks[] =
{
	{0x00000000, 0}
};

static aper_size_info_32 ali_generic_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};

static int __init ali_generic_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = ali_generic_masks;
	agp_bridge.aperture_sizes = (void *) ali_generic_sizes;
	agp_bridge.size_type = U32_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = ali_configure;
	agp_bridge.fetch_size = ali_fetch_size;
	agp_bridge.cleanup = ali_cleanup;
	agp_bridge.tlb_flush = ali_tlbflush;
	agp_bridge.mask_memory = ali_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = ali_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = agp_generic_insert_memory;
	agp_bridge.remove_memory = agp_generic_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = ali_alloc_page;
	agp_bridge.agp_destroy_page = ali_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
	
	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_ALI */

#ifdef CONFIG_AGP_SWORKS
typedef struct _serverworks_page_map {
	unsigned long *real;
	unsigned long *remapped;
} serverworks_page_map;

static struct _serverworks_private {
	struct pci_dev *svrwrks_dev;	/* device one */
	volatile u8 *registers;
	serverworks_page_map **gatt_pages;
	int num_tables;
	serverworks_page_map scratch_dir;

	int gart_addr_ofs;
	int mm_addr_ofs;
} serverworks_private;

static int serverworks_create_page_map(serverworks_page_map *page_map)
{
	int i;
	int err = 0;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL) {
		return -ENOMEM;
	}
	SetPageReserved(virt_to_page(page_map->real));
#ifdef CONFIG_X86
	err = change_page_attr(virt_to_page(page_map->real), 1, PAGE_KERNEL_NOCACHE);
#endif
	CACHE_FLUSH();
	if (!err) 
	page_map->remapped = ioremap_nocache(virt_to_phys(page_map->real), 
					    PAGE_SIZE);
	if (page_map->remapped == NULL || err) {
		ClearPageReserved(virt_to_page(page_map->real));
		free_page((unsigned long) page_map->real);
		page_map->real = NULL;
		return -ENOMEM;
	}
	CACHE_FLUSH();

	for(i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++) {
		page_map->remapped[i] = agp_bridge.scratch_page;
	}

	return 0;
}

static void serverworks_free_page_map(serverworks_page_map *page_map)
{
#ifdef CONFIG_X86
	change_page_attr(virt_to_page(page_map->real),1,PAGE_KERNEL); 
#endif
	iounmap(page_map->remapped);
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void serverworks_free_gatt_pages(void)
{
	int i;
	serverworks_page_map **tables;
	serverworks_page_map *entry;

	tables = serverworks_private.gatt_pages;
	for(i = 0; i < serverworks_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL) {
				serverworks_free_page_map(entry);
			}
			kfree(entry);
		}
	}
	kfree(tables);
}

static int serverworks_create_gatt_pages(int nr_tables)
{
	serverworks_page_map **tables;
	serverworks_page_map *entry;
	int retval = 0;
	int i;

	tables = kmalloc((nr_tables + 1) * sizeof(serverworks_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL) {
		return -ENOMEM;
	}
	memset(tables, 0, sizeof(serverworks_page_map *) * (nr_tables + 1));
	for (i = 0; i < nr_tables; i++) {
		entry = kmalloc(sizeof(serverworks_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		memset(entry, 0, sizeof(serverworks_page_map));
		tables[i] = entry;
		retval = serverworks_create_page_map(entry);
		if (retval != 0) break;
	}
	serverworks_private.num_tables = nr_tables;
	serverworks_private.gatt_pages = tables;

	if (retval != 0) serverworks_free_gatt_pages();

	return retval;
}

#define SVRWRKS_GET_GATT(addr) (serverworks_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

#ifndef GET_PAGE_DIR_OFF
#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#endif

#ifndef GET_PAGE_DIR_IDX
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge.gart_bus_addr))
#endif

#ifndef GET_GATT_OFF
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12)
#endif

static int serverworks_create_gatt_table(void)
{
	aper_size_info_lvl2 *value;
	serverworks_page_map page_dir;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge.current_size);
	retval = serverworks_create_page_map(&page_dir);
	if (retval != 0) {
		return retval;
	}
	retval = serverworks_create_page_map(&serverworks_private.scratch_dir);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		return retval;
	}
	/* Create a fake scratch directory */
	for(i = 0; i < 1024; i++) {
		serverworks_private.scratch_dir.remapped[i] = (unsigned long) agp_bridge.scratch_page;
		page_dir.remapped[i] =
			virt_to_bus(serverworks_private.scratch_dir.real);
		page_dir.remapped[i] |= 0x00000001;
	}

	retval = serverworks_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		serverworks_free_page_map(&page_dir);
		serverworks_free_page_map(&serverworks_private.scratch_dir);
		return retval;
	}

	agp_bridge.gatt_table_real = (u32 *)page_dir.real;
	agp_bridge.gatt_table = (u32 *)page_dir.remapped;
	agp_bridge.gatt_bus_addr = virt_to_bus(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge.dev,
			      serverworks_private.gart_addr_ofs,
			      &temp);
	agp_bridge.gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

	/* Calculate the agp offset */	

	for(i = 0; i < value->num_entries / 1024; i++) {
		page_dir.remapped[i] =
			virt_to_bus(serverworks_private.gatt_pages[i]->real);
		page_dir.remapped[i] |= 0x00000001;
	}

	return 0;
}

static int serverworks_free_gatt_table(void)
{
	serverworks_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge.gatt_table_real;
	page_dir.remapped = (unsigned long *)agp_bridge.gatt_table;

	serverworks_free_gatt_pages();
	serverworks_free_page_map(&page_dir);
	serverworks_free_page_map(&serverworks_private.scratch_dir);
	return 0;
}

static int serverworks_fetch_size(void)
{
	int i;
	u32 temp;
	u32 temp2;
	aper_size_info_lvl2 *values;

	values = A_SIZE_LVL2(agp_bridge.aperture_sizes);
	pci_read_config_dword(agp_bridge.dev,
			      serverworks_private.gart_addr_ofs,
			      &temp);
	pci_write_config_dword(agp_bridge.dev,
			       serverworks_private.gart_addr_ofs,
			       SVWRKS_SIZE_MASK);
	pci_read_config_dword(agp_bridge.dev,
			      serverworks_private.gart_addr_ofs,
			      &temp2);
	pci_write_config_dword(agp_bridge.dev,
			       serverworks_private.gart_addr_ofs,
			       temp);
	temp2 &= SVWRKS_SIZE_MASK;

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (temp2 == values[i].size_value) {
			agp_bridge.previous_size =
			    agp_bridge.current_size = (void *) (values + i);

			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int serverworks_configure(void)
{
	aper_size_info_lvl2 *current_size;
	u32 temp;
	u8 enable_reg;
	u8 cap_ptr;
	u32 cap_id;
	u16 cap_reg;

	current_size = A_SIZE_LVL2(agp_bridge.current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge.dev,
			      serverworks_private.mm_addr_ofs,
			      &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	serverworks_private.registers = (volatile u8 *) ioremap(temp, 4096);

	OUTREG8(serverworks_private.registers, SVWRKS_GART_CACHE, 0x0a);

	OUTREG32(serverworks_private.registers, SVWRKS_GATTBASE, 
		 agp_bridge.gatt_bus_addr);

	cap_reg = INREG16(serverworks_private.registers, SVWRKS_COMMAND);
	cap_reg &= ~0x0007;
	cap_reg |= 0x4;
	OUTREG16(serverworks_private.registers, SVWRKS_COMMAND, cap_reg);

	pci_read_config_byte(serverworks_private.svrwrks_dev,
			     SVWRKS_AGP_ENABLE, &enable_reg);
	enable_reg |= 0x1; /* Agp Enable bit */
	pci_write_config_byte(serverworks_private.svrwrks_dev,
			      SVWRKS_AGP_ENABLE, enable_reg);
	agp_bridge.tlb_flush(NULL);

	pci_read_config_byte(serverworks_private.svrwrks_dev, 0x34, &cap_ptr);
	if (cap_ptr != 0x00) {
		do {
			pci_read_config_dword(serverworks_private.svrwrks_dev,
					      cap_ptr, &cap_id);

			if ((cap_id & 0xff) != 0x02)
				cap_ptr = (cap_id >> 8) & 0xff;
		}
		while (((cap_id & 0xff) != 0x02) && (cap_ptr != 0x00));
	}
	agp_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      agp_bridge.capndx + 4,
			      &agp_bridge.mode);

	pci_read_config_byte(agp_bridge.dev,
			     SVWRKS_CACHING,
			     &enable_reg);
	enable_reg &= ~0x3;
	pci_write_config_byte(agp_bridge.dev,
			      SVWRKS_CACHING,
			      enable_reg);

	pci_read_config_byte(agp_bridge.dev,
			     SVWRKS_FEATURE,
			     &enable_reg);
	enable_reg |= (1<<6);
	pci_write_config_byte(agp_bridge.dev,
			      SVWRKS_FEATURE,
			      enable_reg);

	return 0;
}

static void serverworks_cleanup(void)
{
	iounmap((void *) serverworks_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void serverworks_tlbflush(agp_memory * temp)
{
	unsigned long end;

	OUTREG8(serverworks_private.registers, SVWRKS_POSTFLUSH, 0x01);
	end = jiffies + 3*HZ;
	while(INREG8(serverworks_private.registers, 
		     SVWRKS_POSTFLUSH) == 0x01) {
		if((signed)(end - jiffies) <= 0) {
			printk(KERN_ERR "Posted write buffer flush took more"
			       "then 3 seconds\n");
		}
	}
	OUTREG32(serverworks_private.registers, SVWRKS_DIRFLUSH, 0x00000001);
	end = jiffies + 3*HZ;
	while(INREG32(serverworks_private.registers, 
		     SVWRKS_DIRFLUSH) == 0x00000001) {
		if((signed)(end - jiffies) <= 0) {
			printk(KERN_ERR "TLB flush took more"
			       "then 3 seconds\n");
		}
	}
}

static unsigned long serverworks_mask_memory(unsigned long addr, int type)
{
	/* Only type 0 is supported by the serverworks chipsets */

	return addr | agp_bridge.masks[0].mask;
}

static int serverworks_insert_memory(agp_memory * mem,
			     off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge.current_size)->num_entries;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}
	if ((pg_start + mem->page_count) > num_entries) {
		return -EINVAL;
	}

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		if (!PGE_EMPTY(cur_gatt[GET_GATT_OFF(addr)])) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] =
			agp_bridge.mask_memory(mem->memory[i], mem->type);
	}
	agp_bridge.tlb_flush(mem);
	return 0;
}

static int serverworks_remove_memory(agp_memory * mem, off_t pg_start,
			     int type)
{
	int i;
	unsigned long *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	CACHE_FLUSH();
	agp_bridge.tlb_flush(mem);

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge.gart_bus_addr;
		cur_gatt = SVRWRKS_GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] = 
			(unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static gatt_mask serverworks_masks[] =
{
	{0x00000001, 0}
};

static aper_size_info_lvl2 serverworks_sizes[7] =
{
	{2048, 524288, 0x80000000},
	{1024, 262144, 0xc0000000},
	{512, 131072, 0xe0000000},
	{256, 65536, 0xf0000000},
	{128, 32768, 0xf8000000},
	{64, 16384, 0xfc000000},
	{32, 8192, 0xfe000000}
};

static void serverworks_agp_enable(u32 mode)
{
	struct pci_dev *device = NULL;
	u32 command, scratch, cap_id;
	u8 cap_ptr;

	pci_read_config_dword(serverworks_private.svrwrks_dev,
			      agp_bridge.capndx + 4,
			      &command);

	/*
	 * PASS1: go throu all devices that claim to be
	 *        AGP devices and collect their data.
	 */


	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00) {
			do {
				pci_read_config_dword(device,
						      cap_ptr, &cap_id);

				if ((cap_id & 0xff) != 0x02)
					cap_ptr = (cap_id >> 8) & 0xff;
			}
			while (((cap_id & 0xff) != 0x02) && (cap_ptr != 0x00));
		}
		if (cap_ptr != 0x00) {
			/*
			 * Ok, here we have a AGP device. Disable impossible 
			 * settings, and adjust the readqueue to the minimum.
			 */

			pci_read_config_dword(device, cap_ptr + 4, &scratch);

			/* adjust RQ depth */
			command =
			    ((command & ~0xff000000) |
			     min_t(u32, (mode & 0xff000000),
				 min_t(u32, (command & 0xff000000),
				     (scratch & 0xff000000))));

			/* disable SBA if it's not supported */
			if (!((command & 0x00000200) &&
			      (scratch & 0x00000200) &&
			      (mode & 0x00000200)))
				command &= ~0x00000200;

			/* disable FW */
			command &= ~0x00000010;

			command &= ~0x00000008;

			if (!((command & 4) &&
			      (scratch & 4) &&
			      (mode & 4)))
				command &= ~0x00000004;

			if (!((command & 2) &&
			      (scratch & 2) &&
			      (mode & 2)))
				command &= ~0x00000002;

			if (!((command & 1) &&
			      (scratch & 1) &&
			      (mode & 1)))
				command &= ~0x00000001;
		}
	}
	/*
	 * PASS2: Figure out the 4X/2X/1X setting and enable the
	 *        target (our motherboard chipset).
	 */

	if (command & 4) {
		command &= ~3;	/* 4X */
	}
	if (command & 2) {
		command &= ~5;	/* 2X */
	}
	if (command & 1) {
		command &= ~6;	/* 1X */
	}
	command |= 0x00000100;

	pci_write_config_dword(serverworks_private.svrwrks_dev,
			       agp_bridge.capndx + 8,
			       command);

	/*
	 * PASS3: Go throu all AGP devices and update the
	 *        command registers.
	 */

	pci_for_each_dev(device) {
		cap_ptr = pci_find_capability(device, PCI_CAP_ID_AGP);
		if (cap_ptr != 0x00)
			pci_write_config_dword(device, cap_ptr + 8, command);
	}
}

static int __init serverworks_setup (struct pci_dev *pdev)
{
	u32 temp;
	u32 temp2;

	serverworks_private.svrwrks_dev = pdev;

	agp_bridge.masks = serverworks_masks;
	agp_bridge.aperture_sizes = (void *) serverworks_sizes;
	agp_bridge.size_type = LVL2_APER_SIZE;
	agp_bridge.num_aperture_sizes = 7;
	agp_bridge.dev_private_data = (void *) &serverworks_private;
	agp_bridge.needs_scratch_page = TRUE;
	agp_bridge.configure = serverworks_configure;
	agp_bridge.fetch_size = serverworks_fetch_size;
	agp_bridge.cleanup = serverworks_cleanup;
	agp_bridge.tlb_flush = serverworks_tlbflush;
	agp_bridge.mask_memory = serverworks_mask_memory;
	agp_bridge.agp_enable = serverworks_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = serverworks_create_gatt_table;
	agp_bridge.free_gatt_table = serverworks_free_gatt_table;
	agp_bridge.insert_memory = serverworks_insert_memory;
	agp_bridge.remove_memory = serverworks_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	pci_read_config_dword(agp_bridge.dev,
			      SVWRKS_APSIZE,
			      &temp);

	serverworks_private.gart_addr_ofs = 0x10;

	if(temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(agp_bridge.dev,
				      SVWRKS_APSIZE + 4,
				      &temp2);
		if(temp2 != 0) {
			printk("Detected 64 bit aperture address, but top "
			       "bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
		serverworks_private.mm_addr_ofs = 0x18;
	} else {
		serverworks_private.mm_addr_ofs = 0x14;
	}

	pci_read_config_dword(agp_bridge.dev,
			      serverworks_private.mm_addr_ofs,
			      &temp);
	if(temp & PCI_BASE_ADDRESS_MEM_TYPE_64) {
		pci_read_config_dword(agp_bridge.dev,
				      serverworks_private.mm_addr_ofs + 4,
				      &temp2);
		if(temp2 != 0) {
			printk("Detected 64 bit MMIO address, but top "
			       "bits are not zero.  Disabling agp\n");
			return -ENODEV;
		}
	}

	return 0;
}

#endif /* CONFIG_AGP_SWORKS */

#ifdef CONFIG_AGP_NVIDIA

static struct _nvidia_private {
	struct pci_dev *dev_1;
	struct pci_dev *dev_2;
	struct pci_dev *dev_3;
	volatile u32 *aperture;
	int num_active_entries;
	off_t pg_offset;
} nvidia_private;

static int nvidia_fetch_size(void)
{
	int i;
	u8 size_value;
	aper_size_info_8 *values;

	pci_read_config_byte(agp_bridge.dev, NVIDIA_0_APSIZE, &size_value);
	size_value &= 0x0f;
	values = A_SIZE_8(agp_bridge.aperture_sizes);

	for (i = 0; i < agp_bridge.num_aperture_sizes; i++) {
		if (size_value == values[i].size_value) {
			agp_bridge.previous_size =
				agp_bridge.current_size = (void *) (values + i);
			agp_bridge.aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

#define SYSCFG          0xC0010010
#define IORR_BASE0      0xC0010016
#define IORR_MASK0      0xC0010017
#define AMD_K7_NUM_IORR 2

static int nvidia_init_iorr(u32 base, u32 size)
{
	u32 base_hi, base_lo;
	u32 mask_hi, mask_lo;
	u32 sys_hi, sys_lo;
	u32 iorr_addr, free_iorr_addr;

	/* Find the iorr that is already used for the base */
	/* If not found, determine the uppermost available iorr */
	free_iorr_addr = AMD_K7_NUM_IORR;
	for(iorr_addr = 0; iorr_addr < AMD_K7_NUM_IORR; iorr_addr++) {
		rdmsr(IORR_BASE0 + 2 * iorr_addr, base_lo, base_hi);
		rdmsr(IORR_MASK0 + 2 * iorr_addr, mask_lo, mask_hi);

		if ((base_lo & 0xfffff000) == (base & 0xfffff000))
			break;

		if ((mask_lo & 0x00000800) == 0)
			free_iorr_addr = iorr_addr;
	}
	
	if (iorr_addr >= AMD_K7_NUM_IORR) {
		iorr_addr = free_iorr_addr;
		if (iorr_addr >= AMD_K7_NUM_IORR)
			return -EINVAL;
	}

    base_hi = 0x0;
    base_lo = (base & ~0xfff) | 0x18;
    mask_hi = 0xf;
    mask_lo = ((~(size - 1)) & 0xfffff000) | 0x800;
    wrmsr(IORR_BASE0 + 2 * iorr_addr, base_lo, base_hi);
    wrmsr(IORR_MASK0 + 2 * iorr_addr, mask_lo, mask_hi);

    rdmsr(SYSCFG, sys_lo, sys_hi);
    sys_lo |= 0x00100000;
    wrmsr(SYSCFG, sys_lo, sys_hi);

	return 0;
}

static int nvidia_configure(void)
{
	int i, rc, num_dirs;
	u32 apbase, aplimit;
	aper_size_info_8 *current_size;
	u32 temp;

	current_size = A_SIZE_8(agp_bridge.current_size);

	/* aperture size */
	pci_write_config_byte(agp_bridge.dev, NVIDIA_0_APSIZE,
		current_size->size_value);

    /* address to map to */
	pci_read_config_dword(agp_bridge.dev, NVIDIA_0_APBASE, &apbase);
	apbase &= PCI_BASE_ADDRESS_MEM_MASK;
	agp_bridge.gart_bus_addr = apbase;
	aplimit = apbase + (current_size->size * 1024 * 1024) - 1;
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_APLIMIT, aplimit);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APBASE, apbase);
	pci_write_config_dword(nvidia_private.dev_3, NVIDIA_3_APLIMIT, aplimit);
	if (0 != (rc = nvidia_init_iorr(apbase, current_size->size * 1024 * 1024)))
		return rc;

	/* directory size is 64k */
	num_dirs = current_size->size / 64;
	nvidia_private.num_active_entries = current_size->num_entries;
	nvidia_private.pg_offset = 0;
	if (num_dirs == 0) {
		num_dirs = 1;
		nvidia_private.num_active_entries /= (64 / current_size->size);
		nvidia_private.pg_offset = (apbase & (64 * 1024 * 1024 - 1) &
			~(current_size->size * 1024 * 1024 - 1)) / PAGE_SIZE;
	}

	/* attbase */
	for(i = 0; i < 8; i++) {
		pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_ATTBASE(i),
			(agp_bridge.gatt_bus_addr + (i % num_dirs) * 64 * 1024) | 1);
	}

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL,
		temp | 0x11);

	/* gart control */
	pci_read_config_dword(agp_bridge.dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge.dev, NVIDIA_0_APSIZE,
		temp | 0x100);

	/* map aperture */
	nvidia_private.aperture =
		(volatile u32 *) ioremap(apbase, 33 * PAGE_SIZE);

	return 0;
}

static void nvidia_cleanup(void)
{
	aper_size_info_8 *previous_size;
	u32 temp;

	/* gart control */
	pci_read_config_dword(agp_bridge.dev, NVIDIA_0_APSIZE, &temp);
	pci_write_config_dword(agp_bridge.dev, NVIDIA_0_APSIZE,
		temp & ~(0x100));

	/* gtlb control */
	pci_read_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL, &temp);
	pci_write_config_dword(nvidia_private.dev_2, NVIDIA_2_GARTCTRL,
		temp & ~(0x11));

	/* unmap aperture */
	iounmap((void *) nvidia_private.aperture);

	/* restore previous aperture size */
	previous_size = A_SIZE_8(agp_bridge.previous_size);
	pci_write_config_byte(agp_bridge.dev, NVIDIA_0_APSIZE,
		previous_size->size_value);

	/* restore iorr for previous aperture size */
	nvidia_init_iorr(agp_bridge.gart_bus_addr,
		previous_size->size * 1024 * 1024);
}

static void nvidia_tlbflush(agp_memory * mem)
{
	int i;
	unsigned long end;
	u32 wbc_reg, wbc_mask, temp;

	/* flush chipset */
	switch(agp_bridge.type) {
	case NVIDIA_NFORCE:
		wbc_mask = 0x00010000;
		break;
	case NVIDIA_NFORCE2:
		wbc_mask = 0x80000000;
		break;
	default:
		wbc_mask = 0;
		break;
	}

	if (wbc_mask) {
		pci_read_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, &wbc_reg);
		wbc_reg |= wbc_mask;
		pci_write_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, wbc_reg);

		end = jiffies + 3*HZ;
		do {
			pci_read_config_dword(nvidia_private.dev_1, NVIDIA_1_WBC, &wbc_reg);
			if ((signed)(end - jiffies) <= 0) {
				printk(KERN_ERR "TLB flush took more than 3 seconds.\n");
			}
		} while (wbc_reg & wbc_mask);
	}

	/* flush TLB entries */
	for(i = 0; i < 32 + 1; i++)
		temp = nvidia_private.aperture[i * PAGE_SIZE / sizeof(u32)];
	for(i = 0; i < 32 + 1; i++)
		temp = nvidia_private.aperture[i * PAGE_SIZE / sizeof(u32)];
}

static unsigned long nvidia_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge.masks[0].mask;
}

static int nvidia_insert_memory(agp_memory * mem,
				     off_t pg_start, int type)
{
	int i, j;
	
	if ((type != 0) || (mem->type != 0))
		return -EINVAL;
	
	if ((pg_start + mem->page_count) >
		(nvidia_private.num_active_entries - agp_memory_reserved/PAGE_SIZE))
		return -EINVAL;
	
	for(j = pg_start; j < (pg_start + mem->page_count); j++) {
		if (!PGE_EMPTY(agp_bridge.gatt_table[nvidia_private.pg_offset + j])) {
			return -EBUSY;
		}
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}
	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		agp_bridge.gatt_table[nvidia_private.pg_offset + j] = mem->memory[i];
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int nvidia_remove_memory(agp_memory * mem, off_t pg_start,
				     int type)
{
	int i;

	if ((type != 0) || (mem->type != 0))
		return -EINVAL;
	
	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		agp_bridge.gatt_table[nvidia_private.pg_offset + i] =
		    (unsigned long) agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static aper_size_info_8 nvidia_generic_sizes[5] =
{
	{512, 131072, 7, 0},
	{256, 65536, 6, 8},
	{128, 32768, 5, 12},
	{64, 16384, 4, 14},
	/* The 32M mode still requires a 64k gatt */
	{32, 16384, 4, 15}
};

static gatt_mask nvidia_generic_masks[] =
{
	{0x00000001, 0}
};

static int __init nvidia_generic_setup (struct pci_dev *pdev)
{
	nvidia_private.dev_1 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 1));
	nvidia_private.dev_2 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(0, 2));
	nvidia_private.dev_3 =
		pci_find_slot((unsigned int)pdev->bus->number, PCI_DEVFN(30, 0));
	
	if((nvidia_private.dev_1 == NULL) ||
		(nvidia_private.dev_2 == NULL) ||
		(nvidia_private.dev_3 == NULL)) {
		printk(KERN_INFO PFX "agpgart: Detected an NVIDIA "
			"nForce/nForce2 chipset, but could not find "
			"the secondary devices.\n");
		agp_bridge.type = NOT_SUPPORTED;
		return -ENODEV;
	}

	agp_bridge.masks = nvidia_generic_masks;
	agp_bridge.aperture_sizes = (void *) nvidia_generic_sizes;
	agp_bridge.size_type = U8_APER_SIZE;
	agp_bridge.num_aperture_sizes = 5;
	agp_bridge.dev_private_data = (void *) &nvidia_private;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = nvidia_configure;
	agp_bridge.fetch_size = nvidia_fetch_size;
	agp_bridge.cleanup = nvidia_cleanup;
	agp_bridge.tlb_flush = nvidia_tlbflush;
	agp_bridge.mask_memory = nvidia_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = agp_generic_create_gatt_table;
	agp_bridge.free_gatt_table = agp_generic_free_gatt_table;
	agp_bridge.insert_memory = nvidia_insert_memory;
	agp_bridge.remove_memory = nvidia_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.suspend = agp_generic_suspend;
	agp_bridge.resume = agp_generic_resume;
	agp_bridge.cant_use_aperture = 0;

	return 0;
}

#endif /* CONFIG_AGP_NVIDIA */

#ifdef CONFIG_AGP_HP_ZX1

#ifndef log2
#define log2(x)		ffz(~(x))
#endif

#define HP_ZX1_IOVA_BASE	GB(1UL)
#define HP_ZX1_IOVA_SIZE	GB(1UL)
#define HP_ZX1_GART_SIZE	(HP_ZX1_IOVA_SIZE / 2)
#define HP_ZX1_SBA_IOMMU_COOKIE	0x0000badbadc0ffeeUL

#define HP_ZX1_PDIR_VALID_BIT	0x8000000000000000UL
#define HP_ZX1_IOVA_TO_PDIR(va)	((va - hp_private.iova_base) >> \
					hp_private.io_tlb_shift)

#define PCI_DEVICE_ID_HP_ZX1_QS_LBA	0x12b4

static aper_size_info_fixed hp_zx1_sizes[] =
{
	{0, 0, 0},		/* filled in by hp_zx1_fetch_size() */
};

static gatt_mask hp_zx1_masks[] =
{
	{HP_ZX1_PDIR_VALID_BIT, 0}
};

static struct _hp_private {
	struct pci_dev *ioc;
	volatile u8 *registers;
	u64 *io_pdir;		// PDIR for entire IOVA
	u64 *gatt;		// PDIR just for GART (subset of above)
	u64 gatt_entries;
	u64 iova_base;
	u64 gart_base;
	u64 gart_size;
	u64 io_pdir_size;
	int io_pdir_owner;	// do we own it, or share it with sba_iommu?
	int io_page_size;
	int io_tlb_shift;
	int io_tlb_ps;		// IOC ps config
	int io_pages_per_kpage;
} hp_private;

static int __init hp_zx1_ioc_shared(void)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR shared with sba_iommu\n");

	/*
	 * IOC already configured by sba_iommu module; just use
	 * its setup.  We assume:
	 * 	- IOVA space is 1Gb in size
	 * 	- first 512Mb is IOMMU, second 512Mb is GART
	 */
	hp->io_tlb_ps = INREG64(hp->registers, HP_ZX1_TCNFG);
	switch (hp->io_tlb_ps) {
		case 0: hp->io_tlb_shift = 12; break;
		case 1: hp->io_tlb_shift = 13; break;
		case 2: hp->io_tlb_shift = 14; break;
		case 3: hp->io_tlb_shift = 16; break;
		default:
			printk(KERN_ERR PFX "Invalid IOTLB page size "
			       "configuration 0x%x\n", hp->io_tlb_ps);
			hp->gatt = 0;
			hp->gatt_entries = 0;
			return -ENODEV;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = INREG64(hp->registers, HP_ZX1_IBASE) & ~0x1;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - HP_ZX1_GART_SIZE;

	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gatt_entries = hp->gart_size / hp->io_page_size;

	hp->io_pdir = phys_to_virt(INREG64(hp->registers, HP_ZX1_PDIR_BASE));
	hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];

	if (hp->gatt[0] != HP_ZX1_SBA_IOMMU_COOKIE) {
	    	hp->gatt = 0;
		hp->gatt_entries = 0;
		printk(KERN_ERR PFX "No reserved IO PDIR entry found; "
		       "GART disabled\n");
		return -ENODEV;
	}

	return 0;
}

static int __init hp_zx1_ioc_owner(u8 ioc_rev)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR dedicated to GART\n");

	/*
	 * Select an IOV page size no larger than system page size.
	 */
	if (PAGE_SIZE >= KB(64)) {
		hp->io_tlb_shift = 16;
		hp->io_tlb_ps = 3;
	} else if (PAGE_SIZE >= KB(16)) {
		hp->io_tlb_shift = 14;
		hp->io_tlb_ps = 2;
	} else if (PAGE_SIZE >= KB(8)) {
		hp->io_tlb_shift = 13;
		hp->io_tlb_ps = 1;
	} else {
		hp->io_tlb_shift = 12;
		hp->io_tlb_ps = 0;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = HP_ZX1_IOVA_BASE;
	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - hp->gart_size;

	hp->gatt_entries = hp->gart_size / hp->io_page_size;
	hp->io_pdir_size = (HP_ZX1_IOVA_SIZE / hp->io_page_size) * sizeof(u64);

	return 0;
}

static int __init hp_zx1_ioc_init(void)
{
	struct _hp_private *hp = &hp_private;
	struct pci_dev *ioc;
	int i;
	u8 ioc_rev;

	ioc = pci_find_device(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_ZX1_IOC, NULL);
	if (!ioc) {
		printk(KERN_ERR PFX "Detected HP ZX1 AGP bridge but no IOC\n");
		return -ENODEV;
	}
	hp->ioc = ioc;

	pci_read_config_byte(ioc, PCI_REVISION_ID, &ioc_rev);

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (pci_resource_flags(ioc, i) == IORESOURCE_MEM) {
			hp->registers = (u8 *) ioremap(pci_resource_start(ioc,
									    i),
						    pci_resource_len(ioc, i));
			break;
		}
	}
	if (!hp->registers) {
		printk(KERN_ERR PFX "Detected HP ZX1 AGP bridge but no CSRs\n");

		return -ENODEV;
	}

	/*
	 * If the IOTLB is currently disabled, we can take it over.
	 * Otherwise, we have to share with sba_iommu.
	 */
	hp->io_pdir_owner = (INREG64(hp->registers, HP_ZX1_IBASE) & 0x1) == 0;

	if (hp->io_pdir_owner)
		return hp_zx1_ioc_owner(ioc_rev);

	return hp_zx1_ioc_shared();
}

static int hp_zx1_fetch_size(void)
{
	int size;

	size = hp_private.gart_size / MB(1);
	hp_zx1_sizes[0].size = size;
	agp_bridge.current_size = (void *) &hp_zx1_sizes[0];
	return size;
}

static int hp_zx1_configure(void)
{
	struct _hp_private *hp = &hp_private;

	agp_bridge.gart_bus_addr = hp->gart_base;
	agp_bridge.capndx = pci_find_capability(agp_bridge.dev, PCI_CAP_ID_AGP);
	pci_read_config_dword(agp_bridge.dev,
		agp_bridge.capndx + PCI_AGP_STATUS, &agp_bridge.mode);

	if (hp->io_pdir_owner) {
		OUTREG64(hp->registers, HP_ZX1_PDIR_BASE,
			virt_to_phys(hp->io_pdir));
		OUTREG64(hp->registers, HP_ZX1_TCNFG, hp->io_tlb_ps);
		OUTREG64(hp->registers, HP_ZX1_IMASK, ~(HP_ZX1_IOVA_SIZE - 1));
		OUTREG64(hp->registers, HP_ZX1_IBASE, hp->iova_base | 0x1);
		OUTREG64(hp->registers, HP_ZX1_PCOM,
			hp->iova_base | log2(HP_ZX1_IOVA_SIZE));
		INREG64(hp->registers, HP_ZX1_PCOM);
	}

	return 0;
}

static void hp_zx1_cleanup(void)
{
	struct _hp_private *hp = &hp_private;

	if (hp->io_pdir_owner)
		OUTREG64(hp->registers, HP_ZX1_IBASE, 0);
	iounmap((void *) hp->registers);
}

static void hp_zx1_tlbflush(agp_memory * mem)
{
	struct _hp_private *hp = &hp_private;

	OUTREG64(hp->registers, HP_ZX1_PCOM, 
		hp->gart_base | log2(hp->gart_size));
	INREG64(hp->registers, HP_ZX1_PCOM);
}

static int hp_zx1_create_gatt_table(void)
{
	struct _hp_private *hp = &hp_private;
	int i;

	if (hp->io_pdir_owner) {
		hp->io_pdir = (u64 *) __get_free_pages(GFP_KERNEL,
						get_order(hp->io_pdir_size));
		if (!hp->io_pdir) {
			printk(KERN_ERR PFX "Couldn't allocate contiguous "
				"memory for I/O PDIR\n");
			hp->gatt = 0;
			hp->gatt_entries = 0;
			return -ENOMEM;
		}
		memset(hp->io_pdir, 0, hp->io_pdir_size);

		hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];
	}

	for (i = 0; i < hp->gatt_entries; i++) {
		hp->gatt[i] = (unsigned long) agp_bridge.scratch_page;
	}

	return 0;
}

static int hp_zx1_free_gatt_table(void)
{
	struct _hp_private *hp = &hp_private;
	
	if (hp->io_pdir_owner)
		free_pages((unsigned long) hp->io_pdir,
			    get_order(hp->io_pdir_size));
	else
		hp->gatt[0] = HP_ZX1_SBA_IOMMU_COOKIE;
	return 0;
}

static int hp_zx1_insert_memory(agp_memory * mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, k;
	off_t j, io_pg_start;
	int io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	if ((io_pg_start + io_pg_count) > hp->gatt_entries) {
		return -EINVAL;
	}

	j = io_pg_start;
	while (j < (io_pg_start + io_pg_count)) {
		if (hp->gatt[j]) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		CACHE_FLUSH();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = io_pg_start; i < mem->page_count; i++) {
		unsigned long paddr;

		paddr = mem->memory[i];
		for (k = 0;
		     k < hp->io_pages_per_kpage;
		     k++, j++, paddr += hp->io_page_size) {
			hp->gatt[j] = agp_bridge.mask_memory(paddr, type);
		}
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static int hp_zx1_remove_memory(agp_memory * mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, io_pg_start, io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	for (i = io_pg_start; i < io_pg_count + io_pg_start; i++) {
		hp->gatt[i] = agp_bridge.scratch_page;
	}

	agp_bridge.tlb_flush(mem);
	return 0;
}

static unsigned long hp_zx1_mask_memory(unsigned long addr, int type)
{
	return HP_ZX1_PDIR_VALID_BIT | addr;
}

static int __init hp_zx1_setup (struct pci_dev *pdev)
{
	agp_bridge.masks = hp_zx1_masks;
	agp_bridge.dev_private_data = NULL;
	agp_bridge.size_type = FIXED_APER_SIZE;
	agp_bridge.needs_scratch_page = FALSE;
	agp_bridge.configure = hp_zx1_configure;
	agp_bridge.fetch_size = hp_zx1_fetch_size;
	agp_bridge.cleanup = hp_zx1_cleanup;
	agp_bridge.tlb_flush = hp_zx1_tlbflush;
	agp_bridge.mask_memory = hp_zx1_mask_memory;
	agp_bridge.agp_enable = agp_generic_agp_enable;
	agp_bridge.cache_flush = global_cache_flush;
	agp_bridge.create_gatt_table = hp_zx1_create_gatt_table;
	agp_bridge.free_gatt_table = hp_zx1_free_gatt_table;
	agp_bridge.insert_memory = hp_zx1_insert_memory;
	agp_bridge.remove_memory = hp_zx1_remove_memory;
	agp_bridge.alloc_by_type = agp_generic_alloc_by_type;
	agp_bridge.free_by_type = agp_generic_free_by_type;
	agp_bridge.agp_alloc_page = agp_generic_alloc_page;
	agp_bridge.agp_destroy_page = agp_generic_destroy_page;
	agp_bridge.cant_use_aperture = 1;

	return hp_zx1_ioc_init();

	(void) pdev; /* unused */
}

#endif /* CONFIG_AGP_HP_ZX1 */

/* per-chipset initialization data.
 * note -- all chipsets for a single vendor MUST be grouped together
 */
static struct {
	unsigned short device_id; /* first, to make table easier to read */
	unsigned short vendor_id;
	enum chipset_type chipset;
	const char *vendor_name;
	const char *chipset_name;
	int (*chipset_setup) (struct pci_dev *pdev);
} agp_bridge_info[] __initdata = {

#ifdef CONFIG_AGP_ALI
	{ PCI_DEVICE_ID_AL_M1541_0,
		PCI_VENDOR_ID_AL,
		ALI_M1541,
		"Ali",
		"M1541",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1621_0,  
		PCI_VENDOR_ID_AL,
		ALI_M1621,
		"Ali",
		"M1621",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1631_0,
		PCI_VENDOR_ID_AL,
		ALI_M1631,
		"Ali",
		"M1631",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1632_0,
		PCI_VENDOR_ID_AL,
		ALI_M1632,
		"Ali",
		"M1632",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1641_0,
		PCI_VENDOR_ID_AL,
		ALI_M1641,
		"Ali",
		"M1641",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1644_0,
		PCI_VENDOR_ID_AL,
		ALI_M1644,
		"Ali",
		"M1644",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1647_0,
		PCI_VENDOR_ID_AL,
		ALI_M1647,
		"Ali",
		"M1647",
		ali_generic_setup },
	{ PCI_DEVICE_ID_AL_M1651_0,
		PCI_VENDOR_ID_AL,
		ALI_M1651,
		"Ali",
		"M1651",
		ali_generic_setup },  
	{ PCI_DEVICE_ID_AL_M1671_0,
		PCI_VENDOR_ID_AL,
		ALI_M1671,
		"Ali",
		"M1671",
		ali_generic_setup },  
	{ 0,
		PCI_VENDOR_ID_AL,
		ALI_GENERIC,
		"Ali",
		"Generic",
		ali_generic_setup },
#endif /* CONFIG_AGP_ALI */

#ifdef CONFIG_AGP_AMD
	{ PCI_DEVICE_ID_AMD_IRONGATE_0,
		PCI_VENDOR_ID_AMD,
		AMD_IRONGATE,
		"AMD",
		"Irongate",
		amd_irongate_setup },
	{ PCI_DEVICE_ID_AMD_762_0,
		PCI_VENDOR_ID_AMD,
		AMD_IRONGATE,
		"AMD",
		"760MP",
		amd_irongate_setup },
	{ PCI_DEVICE_ID_AMD_761_0,
		PCI_VENDOR_ID_AMD,
		AMD_761,
		"AMD",
		"761",
		amd_irongate_setup },
	{ 0,
		PCI_VENDOR_ID_AMD,
		AMD_GENERIC,
		"AMD",
		"Generic",
		amd_irongate_setup },
#endif /* CONFIG_AGP_AMD */

#ifdef CONFIG_AGP_AMD_8151
	{ PCI_DEVICE_ID_AMD_8151_0,
		PCI_VENDOR_ID_AMD,
		AMD_8151,
		"AMD",
		"8151",
		amd_8151_setup },
#endif /* CONFIG_AGP_AMD */

#ifdef CONFIG_AGP_INTEL
	{ PCI_DEVICE_ID_INTEL_82443LX_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_LX,
		"Intel",
		"440LX",
		intel_generic_setup },
	{ PCI_DEVICE_ID_INTEL_82443BX_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_BX,
		"Intel",
		"440BX",
		intel_generic_setup },
	{ PCI_DEVICE_ID_INTEL_82443GX_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_GX,
		"Intel",
		"440GX",
		intel_generic_setup },
	{ PCI_DEVICE_ID_INTEL_815_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I815,
		"Intel",
		"i815",
		intel_815_setup },
	{ PCI_DEVICE_ID_INTEL_820_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I820,
		"Intel",
		"i820",
		intel_820_setup },
        { PCI_DEVICE_ID_INTEL_820_UP_0,
                PCI_VENDOR_ID_INTEL,
                INTEL_I820,
                "Intel",
                 "i820",
                 intel_820_setup },
	{ PCI_DEVICE_ID_INTEL_830_M_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I830_M,
		"Intel(R)",
		"830M",
		intel_830mp_setup },
	
	{ PCI_DEVICE_ID_INTEL_845_G_0,
		 PCI_VENDOR_ID_INTEL,
		 INTEL_I845_G,
		 "Intel(R)",
		 "845G",
		 intel_845_setup },

	{ PCI_DEVICE_ID_INTEL_855_GM_0,
		 PCI_VENDOR_ID_INTEL,
		 INTEL_I855_PM,
		 "Intel(R)",
		 "855PM",
		 intel_845_setup },

	{ PCI_DEVICE_ID_INTEL_855_PM_0,
		 PCI_VENDOR_ID_INTEL,
		 INTEL_I855_PM,
		 "Intel(R)",
		 "855PM",
		 intel_845_setup },

	{ PCI_DEVICE_ID_INTEL_865_G_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I865_G,
		"Intel(R)",
		"865G",
		 intel_845_setup },

	{ PCI_DEVICE_ID_INTEL_840_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I840,
		"Intel",
		"i840",
		intel_840_setup },
	{ PCI_DEVICE_ID_INTEL_845_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I845,
		"Intel",
		"i845",
		intel_845_setup },
	{ PCI_DEVICE_ID_INTEL_850_0,
	        PCI_VENDOR_ID_INTEL,
	        INTEL_I850,
	        "Intel",
	        "i850",
	        intel_850_setup },
	{ PCI_DEVICE_ID_INTEL_860_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I860,
		"Intel",
		"i860",
		intel_860_setup },
	{ PCI_DEVICE_ID_INTEL_7505,
		PCI_VENDOR_ID_INTEL,
		INTEL_7505,
		"Intel",
		"7505",
		intel_7x05_setup },
	{ PCI_DEVICE_ID_INTEL_7205,
		PCI_VENDOR_ID_INTEL,
		INTEL_7205,
		"Intel",
		"7205",
		intel_7x05_setup },
	{ PCI_DEVICE_ID_INTEL_875_0,
		PCI_VENDOR_ID_INTEL,
		INTEL_I875,
		"Intel",
		"i875",
		intel_875_setup },
	{ 0,
		PCI_VENDOR_ID_INTEL,
		INTEL_GENERIC,
		"Intel",
		"Generic",
		intel_generic_setup },

#endif /* CONFIG_AGP_INTEL */

#ifdef CONFIG_AGP_SIS
	{ PCI_DEVICE_ID_SI_740,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"740",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_651,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"651",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_650,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"650",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_651,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"651",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_645,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"645",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_646,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"646",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_648,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"648",
		sis_generic_setup },		
	{ PCI_DEVICE_ID_SI_735,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"735",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_745,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"745",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_746,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"746",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_730,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"730",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_630,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"630",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_540,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"540",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_620,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"620",
		sis_generic_setup },
	{ PCI_DEVICE_ID_SI_530,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"530",
		sis_generic_setup },
        { PCI_DEVICE_ID_SI_550,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
                "550",
		sis_generic_setup },
	{ 0,
		PCI_VENDOR_ID_SI,
		SIS_GENERIC,
		"SiS",
		"Generic",
		sis_generic_setup },
#endif /* CONFIG_AGP_SIS */

#ifdef CONFIG_AGP_VIA
	{ PCI_DEVICE_ID_VIA_8501_0,
		PCI_VENDOR_ID_VIA,
		VIA_MVP4,
		"Via",
		"MVP4",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_82C597_0,
		PCI_VENDOR_ID_VIA,
		VIA_VP3,
		"Via",
		"VP3",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_82C598_0,
		PCI_VENDOR_ID_VIA,
		VIA_MVP3,
		"Via",
		"MVP3",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8601_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_PLE133,
		"Via",
		"Apollo PLE133",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_82C691_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_PRO,
		"Via",
		"Apollo Pro",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8371_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KX133,
		"Via",
		"Apollo Pro KX133",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8363_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KT133,
		"Via",
		"Apollo Pro KT133",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8367_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KT133,
		"Via",
		"Apollo Pro KT266",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8375,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KM266,
		"Via",
		"Apollo Pro KM266 / KL266",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_8377_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KT400,
		"Via",
		"Apollo Pro KT400",
		via_generic_setup },
        { PCI_DEVICE_ID_VIA_8377_0,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_KT400,
		"Via",
		"Apollo Pro KT400",
		via_generic_setup },
	{ PCI_DEVICE_ID_VIA_P4M266,
		PCI_VENDOR_ID_VIA,
		VIA_APOLLO_P4M266,
		"Via",
		"Apollo P4M266",
		via_generic_setup },
	{ 0,
		PCI_VENDOR_ID_VIA,
		VIA_GENERIC,
		"Via",
		"Generic",
		via_generic_setup },
#endif /* CONFIG_AGP_VIA */

#ifdef CONFIG_AGP_NVIDIA
	{ PCI_DEVICE_ID_NVIDIA_NFORCE,
		PCI_VENDOR_ID_NVIDIA,
		NVIDIA_NFORCE,
		"NVIDIA",
		"nForce",
		nvidia_generic_setup },
	{ PCI_DEVICE_ID_NVIDIA_NFORCE2,
		PCI_VENDOR_ID_NVIDIA,
		NVIDIA_NFORCE2,
		"NVIDIA",
		"nForce2",
		nvidia_generic_setup },
#endif
#ifdef CONFIG_AGP_AMD_8151
	{ PCI_DEVICE_ID_NVIDIA_NFORCE3,
		PCI_VENDOR_ID_NVIDIA,
		NVIDIA_NFORCE3,
		"NVIDIA",
		"nForce3/K8 On-CPU GART",
		nvidia_x86_64_setup },
#endif
#ifdef CONFIG_AGP_NVIDIA
	{ 0,
		PCI_VENDOR_ID_NVIDIA,
		NVIDIA_GENERIC,
		"NVIDIA",
		"Generic",
		nvidia_generic_setup },
#endif /* CONFIG_AGP_NVIDIA */

#ifdef CONFIG_AGP_HP_ZX1
	{ PCI_DEVICE_ID_HP_ZX1_QS_LBA,
		PCI_VENDOR_ID_HP,
		HP_ZX1,
		"HP",
		"ZX1",
		hp_zx1_setup },
	{ PCI_DEVICE_ID_HP_ZX1_LBA,
		PCI_VENDOR_ID_HP,
		HP_ZX1,
		"HP",
		"ZX1",
		hp_zx1_setup },
#endif

	{ 0, }, /* dummy final entry, always present */
};


/* scan table above for supported devices */
static int __init agp_lookup_host_bridge (struct pci_dev *pdev)
{
	int i;
	
	for (i = 0; i < ARRAY_SIZE (agp_bridge_info); i++)
		if (pdev->vendor == agp_bridge_info[i].vendor_id)
			break;

	if (i >= ARRAY_SIZE (agp_bridge_info)) {
		printk (KERN_DEBUG PFX "unsupported bridge\n");
		return -ENODEV;
	}

	while ((i < ARRAY_SIZE (agp_bridge_info)) &&
	       (agp_bridge_info[i].vendor_id == pdev->vendor)) {
		if (pdev->device == agp_bridge_info[i].device_id) {
#ifdef CONFIG_AGP_ALI
			if (pdev->device == PCI_DEVICE_ID_AL_M1621_0) {
				u8 hidden_1621_id;

				pci_read_config_byte(pdev, 0xFB, &hidden_1621_id);
				switch (hidden_1621_id) {
				case 0x31:
					agp_bridge_info[i].chipset_name="M1631";
					break;
				case 0x32:
					agp_bridge_info[i].chipset_name="M1632";
					break;
				case 0x41:
					agp_bridge_info[i].chipset_name="M1641";
					break;
				case 0x43:
					break;
				case 0x47:
					agp_bridge_info[i].chipset_name="M1647";
					break;
				case 0x51:
					agp_bridge_info[i].chipset_name="M1651";
					break;
				default:
					break;
				}
			}
#endif

			printk (KERN_INFO PFX "Detected %s %s chipset\n",
				agp_bridge_info[i].vendor_name,
				agp_bridge_info[i].chipset_name);
			agp_bridge.type = agp_bridge_info[i].chipset;
			return agp_bridge_info[i].chipset_setup (pdev);
		}
		
		i++;
	}

	i--; /* point to vendor generic entry (device_id == 0) */

	/* try init anyway, if user requests it AND
	 * there is a 'generic' bridge entry for this vendor */
	if (agp_try_unsupported && agp_bridge_info[i].device_id == 0) {
		printk(KERN_WARNING PFX "Trying generic %s routines"
		       " for device id: %04x\n",
		       agp_bridge_info[i].vendor_name, pdev->device);
		agp_bridge.type = agp_bridge_info[i].chipset;
		return agp_bridge_info[i].chipset_setup (pdev);
	}

	printk(KERN_ERR PFX "Unsupported %s chipset (device id: %04x),"
	       " you might want to try agp_try_unsupported=1.\n",
	       agp_bridge_info[i].vendor_name, pdev->device);
	return -ENODEV;
}

/* Supported Device Scanning routine */

static int __init agp_find_supported_device(void)
{
	struct pci_dev *dev = NULL;
	int ret = -ENODEV;

	while ((dev = pci_find_class(PCI_CLASS_BRIDGE_HOST << 8, dev)) != NULL) {
		ret = agp_init_one(dev);
		if (ret != -ENODEV)
			goto out;
	}

	/* ZX1 QS LBA appears as PCI bridge, not host bridge */
	while ((dev = pci_find_class(PCI_CLASS_BRIDGE_PCI << 8, dev)) != NULL) {
		ret = agp_init_one(dev);
		if (ret != -ENODEV)
			break;
	}
out:
	if (agp_bridge.dev == NULL)
		agp_bridge.dev = dev;

	return ret;
}

static int __init agp_init_one(struct pci_dev *dev)
{
	u8 cap_ptr = 0x00;

#ifdef CONFIG_AGP_AMD_8151
	/* If there is any K8 northbridge in the system always use the K8 driver */
	if (agp_try_unsupported
	    && pci_find_device(PCI_VENDOR_ID_AMD, 0x1103, NULL)
	    && !pci_find_device(PCI_VENDOR_ID_NVIDIA, 
				PCI_DEVICE_ID_NVIDIA_NFORCE3,
				NULL)) { 

		/* find capndx */
		cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
		if (cap_ptr == 0x00)
			return -ENODEV;
		agp_bridge.dev = dev;
		agp_bridge.capndx = cap_ptr;
		
		/* Fill in the mode register */
		pci_read_config_dword(agp_bridge.dev,
				      agp_bridge.capndx + 4,
				      &agp_bridge.mode);
		
		printk(KERN_INFO PFX "Detected GART in AMD K8 Northbridge\n"); 
		agp_bridge.type = AMD_8151; 
		return amd_8151_setup(dev);
	}					
#endif


	/* Need to test for I810 here */
#ifdef CONFIG_AGP_I810
	if (dev->vendor == PCI_VENDOR_ID_INTEL) {
		struct pci_dev *i810_dev;

		switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_810_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					       PCI_DEVICE_ID_INTEL_810_1,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810,"
				       " but could not find the secondary"
				       " device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel "
			       "i810 Chipset.\n");
			agp_bridge.dev = dev;
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup (i810_dev);

		case PCI_DEVICE_ID_INTEL_810_DC100_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					 PCI_DEVICE_ID_INTEL_810_DC100_1,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810 "
				       "DC100, but could not find the "
				       "secondary device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel i810 "
			       "DC100 Chipset.\n");
			agp_bridge.dev = dev;
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		case PCI_DEVICE_ID_INTEL_810_E_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					     PCI_DEVICE_ID_INTEL_810_E_1,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "Detected an Intel i810 E"
				    ", but could not find the secondary "
				       "device.\n");
				return -ENODEV;
			}
			printk(KERN_INFO PFX "Detected an Intel i810 E "
			       "Chipset.\n");
			agp_bridge.dev = dev;
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		 case PCI_DEVICE_ID_INTEL_815_0:
		   /* The i815 can operate either as an i810 style
		    * integrated device, or as an AGP4X motherboard.
		    *
		    * This only addresses the first mode:
		    */
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
						   PCI_DEVICE_ID_INTEL_815_1,
						   NULL);
			if (i810_dev == NULL) {
				printk(KERN_ERR PFX "agpgart: Detected an "
				       "Intel i815, but could not find the"
				       " secondary device. Assuming a "
				       "non-integrated video card.\n");
				break;
			}
			printk(KERN_INFO PFX "agpgart: Detected an Intel i815 "
			       "Chipset.\n");
			agp_bridge.dev = dev;
			agp_bridge.type = INTEL_I810;
			return intel_i810_setup(i810_dev);

		case PCI_DEVICE_ID_INTEL_845_G_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_845_G_1, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_845_G_1, i810_dev);
			}

			agp_bridge.dev = dev;
			if (i810_dev == NULL) {
                                /* 
                                 * We probably have a I845 G chipset
                                 * with an external graphics
                                 * card. It will be initialized later 
                                 */
				printk(KERN_ERR PFX "Detected an "
				       "Intel(R) 845G, but could not find the"
				       " secondary device. Assuming a "
				       "non-integrated video card.\n");
				agp_bridge.type = INTEL_I845_G;
				break;
			}
			printk(KERN_INFO PFX "Detected an Intel(R) "
				   "845G Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		   
		case PCI_DEVICE_ID_INTEL_830_M_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_830_M_1, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_830_M_1, i810_dev);
			}

			agp_bridge.dev = dev;
			if (i810_dev == NULL) {
                                /* 
                                 * We probably have a I830MP chipset
                                 * with an external graphics
                                 * card. It will be initialized later 
                                 */
				agp_bridge.type = INTEL_I830_M;
				break;
			}
			printk(KERN_INFO PFX "Detected an Intel(R) "
				   "830M Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		case PCI_DEVICE_ID_INTEL_855_GM_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_855_GM_1, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_855_GM_1, i810_dev);
			}
			agp_bridge.dev = dev;
			if (i810_dev == NULL) {
                                /* 
                                 * We probably have an 855PM chipset
                                 * with an external graphics
                                 * card. It will be initialized later.
                                 */
				agp_bridge.type = INTEL_I855_PM;
				break;
			}
			{
				u32 capval = 0;
				const char *name = "855GM/852GM";

				pci_read_config_dword(dev, I85X_CAPID, &capval);
				switch ((capval >> I85X_VARIANT_SHIFT) &
					I85X_VARIANT_MASK) {
				case I855_GME:
					name = "855GME";
					break;
				case I855_GM:
					name = "855GM";
					break;
				case I852_GME:
					name = "852GME";
					break;
				case I852_GM:
					name = "852GM";
					break;
				}
				printk(KERN_INFO PFX "Detected an Intel(R) "
					"%s Chipset.\n", name);
			}
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		case PCI_DEVICE_ID_INTEL_855_PM_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_855_PM_1, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_855_PM_1, i810_dev);
			}
			agp_bridge.dev = dev;
			if (i810_dev == NULL) {
                                /* 
                                 * We probably have an 855PM chipset
                                 * with an external graphics
                                 * card. It will be initialized later.
                                 */
				agp_bridge.type = INTEL_I855_PM;
				break;
			}
			{
				u32 capval = 0;
				const char *name = "855PM/852PM";

				pci_read_config_dword(dev, I85X_CAPID, &capval);
				switch ((capval >> I85X_VARIANT_SHIFT) &
					I85X_VARIANT_MASK) {
				case I855_PME:
					name = "855PME";
					break;
				case I855_PM:
					name = "855PM";
					break;
				case I852_PME:
					name = "852PME";
					break;
				case I852_PM:
					name = "852PM";
					break;
				}
				printk(KERN_INFO PFX "Detected an Intel(R) "
					"%s Chipset.\n", name);
			}
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		case PCI_DEVICE_ID_INTEL_865_G_0:
			i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_865_G_1, NULL);
			if(i810_dev && PCI_FUNC(i810_dev->devfn) != 0) {
				i810_dev = pci_find_device(PCI_VENDOR_ID_INTEL,
					PCI_DEVICE_ID_INTEL_865_G_1, i810_dev);
			}

			agp_bridge.dev = dev;
			if (i810_dev == NULL) {
                                /* 
                                 * We probably have a 865G chipset
                                 * with an external graphics
                                 * card. It will be initialized later 
                                 */
				printk(KERN_ERR PFX "Detected an "
				       "Intel(R) 865G, but could not"
				       " find the"
				       " secondary device. Assuming a "
				       "non-integrated video card.\n");
				agp_bridge.type = INTEL_I865_G;
				break;
			}
			printk(KERN_INFO PFX "Detected an Intel(R) "
				   "865G Chipset.\n");
			agp_bridge.type = INTEL_I810;
			return intel_i830_setup(i810_dev);
		default:
			break;
		}
	}
#endif /* CONFIG_AGP_I810 */

#ifdef CONFIG_AGP_SWORKS
	/* Everything is on func 1 here so we are hardcoding function one */
	if (dev->vendor == PCI_VENDOR_ID_SERVERWORKS) {
		struct pci_dev *bridge_dev;

		bridge_dev = pci_find_slot ((unsigned int)dev->bus->number, 
					    PCI_DEVFN(0, 1));
		if(bridge_dev == NULL) {
			printk(KERN_INFO PFX "agpgart: Detected a Serverworks "
			       "Chipset, but could not find the secondary "
			       "device.\n");
			return -ENODEV;
		}

		switch (dev->device) {
		case PCI_DEVICE_ID_SERVERWORKS_HE:
			agp_bridge.dev = dev;
			agp_bridge.type = SVWRKS_HE;
			return serverworks_setup(bridge_dev);

		case PCI_DEVICE_ID_SERVERWORKS_LE:
		case 0x0007:
			agp_bridge.dev = dev;
			agp_bridge.type = SVWRKS_LE;
			return serverworks_setup(bridge_dev);

		default:
			if(agp_try_unsupported) {
				agp_bridge.dev = dev;
				agp_bridge.type = SVWRKS_GENERIC;
				return serverworks_setup(bridge_dev);
			}
			break;
		}
	}

#endif	/* CONFIG_AGP_SWORKS */

#ifdef CONFIG_AGP_HP_ZX1
	if (dev->vendor == PCI_VENDOR_ID_HP) {
		do {
			/* ZX1 LBAs can be either PCI or AGP bridges */
			if (pci_find_capability(dev, PCI_CAP_ID_AGP)) {
				printk(KERN_INFO PFX "Detected HP ZX1 AGP "
				       "chipset at %s\n", dev->slot_name);
				agp_bridge.type = HP_ZX1;
				agp_bridge.dev = dev;
				return hp_zx1_setup(dev);
			}
			dev = pci_find_class(PCI_CLASS_BRIDGE_HOST << 8, dev);
		} while (dev);

		/* ZX1 QS LBA appears as PCI bridge, not host bridge */
		dev = pci_find_class(PCI_CLASS_BRIDGE_PCI << 8, NULL);
		while (dev) {
			/* ZX1 LBAs can be either PCI or AGP bridges */
			if (pci_find_capability(dev, PCI_CAP_ID_AGP)) {
				printk(KERN_INFO PFX "Detected HP ZX1 AGP "
				       "chipset at %s\n", dev->slot_name);
				agp_bridge.type = HP_ZX1;
				agp_bridge.dev = dev;
				return hp_zx1_setup(dev);
			}
			dev = pci_find_class(PCI_CLASS_BRIDGE_PCI << 8, dev);
		} 
		return -ENODEV;
	}
#endif	/* CONFIG_AGP_HP_ZX1 */

	/* find capndx */
	cap_ptr = pci_find_capability(dev, PCI_CAP_ID_AGP);
	if (cap_ptr == 0x00)
		return -ENODEV;
	agp_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(dev,
			      agp_bridge.capndx + 4,
			      &agp_bridge.mode);

	/* probe for known chipsets */
	return agp_lookup_host_bridge (dev);
}

struct agp_max_table {
	int mem;
	int agp;
};

static struct agp_max_table maxes_table[9] __initdata =
{
	{0, 0},
	{32, 4},
	{64, 28},
	{128, 96},
	{256, 204},
	{512, 440},
	{1024, 942},
	{2048, 1920},
	{4096, 3932}
};

static int __init agp_find_max (void)
{
	long memory, index, result;

	memory = (num_physpages << PAGE_SHIFT) >> 20;
	index = 1;

	while ((memory > maxes_table[index].mem) &&
	       (index < 8)) {
		index++;
	}

	result = maxes_table[index - 1].agp +
	   ( (memory - maxes_table[index - 1].mem)  *
	     (maxes_table[index].agp - maxes_table[index - 1].agp)) /
	   (maxes_table[index].mem - maxes_table[index - 1].mem);

	printk(KERN_INFO PFX "Maximum main memory to use "
	       "for agp memory: %ldM\n", result);
	result = result << (20 - PAGE_SHIFT);
        return result;
}

#define AGPGART_VERSION_MAJOR 0
#define AGPGART_VERSION_MINOR 99

static agp_version agp_current_version =
{
	AGPGART_VERSION_MAJOR,
	AGPGART_VERSION_MINOR
};

static int __init agp_backend_initialize(void)
{
	int size_value, rc, got_gatt=0, got_keylist=0;

	memset(&agp_bridge, 0, sizeof(struct agp_bridge_data));
	agp_bridge.type = NOT_SUPPORTED;
	agp_bridge.max_memory_agp = agp_find_max();
	agp_bridge.version = &agp_current_version;

	rc = agp_find_supported_device();
	if (rc) {
		/* not KERN_ERR because error msg should have already printed */
		printk(KERN_DEBUG PFX "no supported devices found.\n");
		return rc;
	}

	if (agp_bridge.needs_scratch_page == TRUE) {
		unsigned long addr;
		addr = agp_bridge.agp_alloc_page();

		if (addr == 0) {
			printk(KERN_ERR PFX "unable to get memory for "
			       "scratch page.\n");
			return -ENOMEM;
		}
		agp_bridge.scratch_page_real = virt_to_phys((void *) addr);
		agp_bridge.scratch_page =
		    agp_bridge.mask_memory(agp_bridge.scratch_page_real, 0);
	}

	size_value = agp_bridge.fetch_size();

	if (size_value == 0) {
		printk(KERN_ERR PFX "unable to determine aperture size.\n");
		rc = -EINVAL;
		goto err_out;
	}
	if (agp_bridge.create_gatt_table()) {
		printk(KERN_ERR PFX "unable to get memory for graphics "
		       "translation table.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_gatt = 1;
	
	agp_bridge.key_list = vmalloc(PAGE_SIZE * 4);
	if (agp_bridge.key_list == NULL) {
		printk(KERN_ERR PFX "error allocating memory for key lists.\n");
		rc = -ENOMEM;
		goto err_out;
	}
	got_keylist = 1;
	
	/* FIXME vmalloc'd memory not guaranteed contiguous */
	memset(agp_bridge.key_list, 0, PAGE_SIZE * 4);

	if (agp_bridge.configure()) {
		printk(KERN_ERR PFX "error configuring host chipset.\n");
		rc = -EINVAL;
		goto err_out;
	}

	printk(KERN_INFO PFX "AGP aperture is %dM @ 0x%lx\n",
	       size_value, agp_bridge.gart_bus_addr);

	return 0;

err_out:
	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.agp_destroy_page((unsigned long)
				 phys_to_virt(agp_bridge.scratch_page_real));
	}
	if (got_gatt)
		agp_bridge.free_gatt_table();
	if (got_keylist)
		vfree(agp_bridge.key_list);
	return rc;
}


/* cannot be __exit b/c as it could be called from __init code */
static void agp_backend_cleanup(void)
{
	agp_bridge.cleanup();
	agp_bridge.free_gatt_table();
	vfree(agp_bridge.key_list);

	if (agp_bridge.needs_scratch_page == TRUE) {
		agp_bridge.agp_destroy_page((unsigned long)
				 phys_to_virt(agp_bridge.scratch_page_real));
	}
}

static int agp_power(struct pm_dev *dev, pm_request_t rq, void *data)
{
	switch(rq)
	{
		case PM_SUSPEND:
			return agp_bridge.suspend();
		case PM_RESUME:
			agp_bridge.resume();
			return 0;
	}		
	return 0;
}

extern int agp_frontend_initialize(void);
extern void agp_frontend_cleanup(void);

static const drm_agp_t drm_agp = {
	&agp_free_memory,
	&agp_allocate_memory,
	&agp_bind_memory,
	&agp_unbind_memory,
	&agp_enable,
	&agp_backend_acquire,
	&agp_backend_release,
	&agp_copy_info
};

int __init agp_init(void)
{
	int ret_val;

	printk(KERN_INFO "Linux agpgart interface v%d.%d (c) Jeff Hartmann\n",
	       AGPGART_VERSION_MAJOR, AGPGART_VERSION_MINOR);

	ret_val = agp_backend_initialize();
	if (ret_val) {
		agp_bridge.type = NOT_SUPPORTED;
		return ret_val;
	}
	ret_val = agp_frontend_initialize();
	if (ret_val) {
		agp_bridge.type = NOT_SUPPORTED;
		agp_backend_cleanup();
		return ret_val;
	}

	inter_module_register("drm_agp", THIS_MODULE, &drm_agp);
	
	pm_register(PM_PCI_DEV, PM_PCI_ID(agp_bridge.dev), agp_power);
	return 0;
}

static void __exit agp_cleanup(void)
{
	pm_unregister_all(agp_power);
	agp_frontend_cleanup();
	agp_backend_cleanup();
	inter_module_unregister("drm_agp");
}

#ifndef CONFIG_GART_IOMMU
module_init(agp_init);
module_exit(agp_cleanup);
#endif
