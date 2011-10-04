/*
 *  linux/mm/page_alloc.c
 *
 *  Manages the free list, the system allocates free pages here.
 *  Note that kmalloc() lives in slab.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 *  Per-CPU page pool, Ingo Molnar, Red Hat, 2001, 2002
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mm_inline.h>
#include <linux/smp.h>

int nr_swap_pages;
pg_data_t *pgdat_list;

int vm_defragment;

static unsigned long __initdata heuristic_lowmem_pages;
static unsigned long __initdata heuristic_all_pages;

/*
 *
 * The zone_table array is used to look up the address of the
 * struct zone corresponding to a given zone number (ZONE_DMA,
 * ZONE_NORMAL, or ZONE_HIGHMEM).
 */
zone_t *zone_table[MAX_NR_ZONES*MAX_NR_NODES];
EXPORT_SYMBOL(zone_table);

zone_wired_t zone_wired[MAX_NR_ZONES*MAX_NR_NODES];

static char *zone_names[MAX_NR_ZONES] = { "DMA", "Normal", "HighMem" };
#if defined(CONFIG_HIGHMEM64G) || defined(CONFIG_X86_64)
static int zone_balance_ratio[MAX_NR_ZONES] __initdata = { 4097, 128, 128, };
static int zone_balance_min[MAX_NR_ZONES] __initdata = { 0 , 20, 20, };
static int zone_balance_max[MAX_NR_ZONES] __initdata = { 0 , 255, 255, };
static int zone_extrafree_ratio[MAX_NR_ZONES] __initdata = { 4097, 512, 0, };
static int zone_extrafree_max[MAX_NR_ZONES] __initdata = { 0 , 1024, 0, };
#else
static int zone_balance_ratio[MAX_NR_ZONES] __initdata = { 128, 128, 128, };
static int zone_balance_min[MAX_NR_ZONES] __initdata = { 20 , 20, 20, };
static int zone_balance_max[MAX_NR_ZONES] __initdata = { 255 , 255, 255, };
static int zone_extrafree_ratio[MAX_NR_ZONES] __initdata = { 128, 512, 0, };
static int zone_extrafree_max[MAX_NR_ZONES] __initdata = { 1024 , 1024, 0, };
#endif

/*
 * Temporary debugging check.
 */
#define BAD_RANGE(zone, page)						\
(									\
	(((page) - mem_map) >= ((zone)->zone_start_mapnr+(zone)->size))	\
	|| (((page) - mem_map) < (zone)->zone_start_mapnr)		\
	|| ((zone) != page_zone(page))					\
)

static void FASTCALL(__free_pages_ok (struct page *page, unsigned int order));

static spinlock_t free_pages_ok_no_irq_lock = SPIN_LOCK_UNLOCKED;
struct page *free_pages_ok_no_irq_head;

static void do_free_pages_ok_no_irq(void * arg)
{
	struct page * page, * __page;

	spin_lock_irq(&free_pages_ok_no_irq_lock);

	page = free_pages_ok_no_irq_head;
	free_pages_ok_no_irq_head = NULL;

	spin_unlock_irq(&free_pages_ok_no_irq_lock);

	while (page) {
		__page = page;
		page = page->next_hash;
		__free_pages_ok(__page, __page->index);
	}
}

static struct tq_struct free_pages_ok_no_irq_task = {
	.routine	= do_free_pages_ok_no_irq,
};

#ifndef CONFIG_HUGETLB_PAGE
#define prep_compound_page(page, order) do { } while (0)
#define destroy_compound_page(page, order) do { } while (0)
#else
/*
 * Higher-order pages are called "compound pages".  They are structured thusly:
 *
 * The first PAGE_SIZE page is called the "head page".
 *
 * The remaining PAGE_SIZE pages are called "tail pages".
 *
 * All pages have PG_compound set.  All pages have their lru.next pointing at
 * the head page (even the head page has this).
 *
 * The head page's lru.prev, if non-zero, holds the address of the compound
 * page's put_page() function.
 *
 * The order of the allocation is stored in the first tail page's lru.prev.
 * This is only for debug at present.  This usage means that zero-order pages
 * may not be compound.
 */
static void prep_compound_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;

	BUG_ON(page_count(page) != 1);
	page->lru.prev = NULL;
	page[1].lru.prev = (void *)order;
	for (i = 0; i < nr_pages; i++) {
		struct page *p = page + i;

		SetPageCompound(p);
		p->lru.next = (void *)page;
		if (unlikely(i && page_count(p))) {
			printk("prep_compound_page(): incorrect sub-page count %08x, of page %016Lx(%08lx).\n", page_count(p), (unsigned long long)(p-mem_map)*PAGE_SIZE, p->flags);
			set_page_count(p, 0);
		}
		BUG_ON(p->mapping);
	}
}

static void destroy_compound_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;

	BUG_ON(page[1].lru.prev != (void *)order);

	for (i = 0; i < nr_pages; i++) {
		struct page *p = page + i;

		BUG_ON(!PageCompound(p));
		BUG_ON(page_count(p));
		if (p->lru.next != (void *)page) {
			printk("ugh - idx %d, page %p, order %ld.\n",
					i, page, order);
			printk("p->lru.next (%p) != page (%p)\n",
					p->lru.next, page);
			BUG();
		}
		BUG_ON(p->mapping);
		ClearPageCompound(p);
	}
}
#endif		/* CONFIG_HUGETLB_PAGE */

/*
 * Freeing function for a buddy system allocator.
 * Contrary to prior comments, this is *NOT* hairy, and there
 * is no reason for anyone not to understand it.
 *
 * The concept of a buddy system is to maintain direct-mapped tables
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep one bit for each pair of blocks, which
 * is set to 1 iff only one of the pair is allocated.  So when we
 * are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were   
 * free, the remainder of the region must be split into blocks.   
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.            
 *
 * -- wli
 */

static void __free_pages_ok (struct page *page, unsigned int order)
{
	unsigned long index, page_idx, mask, flags;
	free_area_t *area;
	struct page *base;
	per_cpu_t *per_cpu;
	zone_t *zone;

	/*
	 * Yes, think what happens when other parts of the kernel take 
	 * a reference to a page in order to pin it for io. -ben
	 */
	if (PageLRU(page)) {
		if (unlikely(in_interrupt())) {
			unsigned long flags;

			spin_lock_irqsave(&free_pages_ok_no_irq_lock, flags);

			page->next_hash = free_pages_ok_no_irq_head;
			free_pages_ok_no_irq_head = page;
			page->index = order;

			spin_unlock_irqrestore(&free_pages_ok_no_irq_lock, flags);

			schedule_task(&free_pages_ok_no_irq_task);
			return;
		}
		lru_cache_del(page);
	}

	if (page->buffers)
		BUG();
	if (page->mapping) {
		printk(KERN_CRIT "Page has mapping still set. This is a serious situation. However if you \n");
		printk(KERN_CRIT "are using the NVidia binary only module please report this bug to \n");
		printk(KERN_CRIT "NVidia and not to the linux kernel mailinglist.\n");
		BUG();
	}
	if (!VALID_PAGE(page))
		BUG();
	if (PageLocked(page))
		BUG();
	if (PageActiveAnon(page))
		BUG();
	if (PageActiveCache(page))
		BUG();
	if (PageInactiveDirty(page))
		BUG();
	if (PageInactiveLaundry(page))
		BUG();
	if (PageInactiveClean(page))
		BUG();
	if (page->pte.direct)
		BUG();
	if (page_count(page)) {
		static int once = 1;
		printk("free_pages_ok(): incorrect sub-page count %08x, of page %016Lx(%08lx).\n", page_count(page), (unsigned long long)(page-mem_map)*PAGE_SIZE, page->flags);
		if (once) {
			once = 0;
			show_stack(NULL);
		}

	}
	ClearPageReferenced(page);
	ClearPageDirty(page);
	ClearPageFresh(page);
	
	zone = page_zone(page);

	mask = (~0UL) << order;
	base = zone->zone_mem_map;
	page_idx = page - base;
	if (page_idx & ~mask)
		BUG();
	index = page_idx >> (1 + order);

	area = zone->free_area + order;

	per_cpu = zone->cpu_pages + smp_processor_id();

	__save_flags(flags);
	__cli();
	if (!order && (per_cpu->nr_pages < per_cpu->max_nr_pages) && (free_high(zone) <= 0)) {
		list_add(&page->list, &per_cpu->head);
		per_cpu->nr_pages++;
		__restore_flags(flags);
		return;
	}

	spin_lock(&zone->lock);
	if (order)
		destroy_compound_page(page, order);
	zone->free_pages -= mask;

	while (mask + (1 << (MAX_ORDER-1))) {
		struct page *buddy1, *buddy2;

		if (area >= zone->free_area + MAX_ORDER)
			BUG();
		if (!__test_and_change_bit(index, area->map))
			/*
			 * the buddy page is still allocated.
			 */
			break;
		/*
		 * Move the buddy up one level.
		 * This code is taking advantage of the identity:
		 * 	-mask = 1+~mask
		 */
		buddy1 = base + (page_idx ^ -mask);
		buddy2 = base + page_idx;
		if (BAD_RANGE(zone,buddy1))
			BUG();
		if (BAD_RANGE(zone,buddy2))
			BUG();

		list_del(&buddy1->list);
		mask <<= 1;
		area++;
		index >>= 1;
		page_idx &= mask;
	}
	list_add(&(base + page_idx)->list, &area->free_list);

	spin_unlock_irqrestore(&zone->lock, flags);
}

#define MARK_USED(index, order, area) \
	__change_bit((index) >> (1+(order)), (area)->map)

static inline struct page * expand (zone_t *zone, struct page *page,
	 unsigned long index, int low, int high, free_area_t * area)
{
	unsigned long size = 1 << high;

	while (high > low) {
		if (BAD_RANGE(zone,page))
			BUG();
		area--;
		high--;
		size >>= 1;
		list_add(&(page)->list, &(area)->free_list);
		MARK_USED(index, high, area);
		index += size;
		page += size;
	}
	if (BAD_RANGE(zone,page))
		BUG();
	return page;
}

static FASTCALL(struct page * rmqueue(zone_t *zone, unsigned int order));
static struct page * rmqueue(zone_t *zone, unsigned int order)
{
	per_cpu_t *per_cpu = zone->cpu_pages + smp_processor_id();
	free_area_t * area = zone->free_area + order;
	unsigned int curr_order = order;
	struct list_head *head, *curr;
	unsigned long flags;
	struct page *page;
	int threshold = 0;

	if (!(current->flags & PF_MEMALLOC))
		 threshold = (per_cpu->max_nr_pages / 8);
	__save_flags(flags);
	__cli();

	if (!order && (per_cpu->nr_pages>threshold)) {
		if (unlikely(list_empty(&per_cpu->head)))
			BUG();
		page = list_entry(per_cpu->head.next, struct page, list);
		list_del(&page->list);
		per_cpu->nr_pages--;
		__restore_flags(flags);
 
		set_page_count(page, 1);
		return page;
	}
 
 	spin_lock(&zone->lock);
	do {
		head = &area->free_list;
		curr = head->next;

		if (curr != head) {
			unsigned int index;

			page = list_entry(curr, struct page, list);
			if (BAD_RANGE(zone,page))
				BUG();
			list_del(curr);
			index = page - zone->zone_mem_map;
			if (curr_order != MAX_ORDER-1)
				MARK_USED(index, curr_order, area);
			zone->free_pages -= 1UL << order;

			page = expand(zone, page, index, order, curr_order, area);
			spin_unlock_irqrestore(&zone->lock, flags);

			set_page_count(page, 1);
			if (BAD_RANGE(zone,page))
				BUG();
			DEBUG_LRU_PAGE(page);
			if (order)
				prep_compound_page(page, order);
			return page;	
		}
		curr_order++;
		area++;
	} while (curr_order < MAX_ORDER);
	spin_unlock_irqrestore(&zone->lock, flags);

	return NULL;
}

#ifndef CONFIG_DISCONTIGMEM
struct page *_alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	return __alloc_pages(gfp_mask, order,
		contig_page_data.node_zonelists+(gfp_mask & GFP_ZONEMASK));
}
#endif

/*
 * If we are able to directly reclaim pages, we move pages from the
 * inactive_clean list onto the free list until the zone has enough
 * free pages or until the inactive_clean pages are exhausted.
 */
void FASTCALL(fixup_freespace(zone_t * zone, int direct_reclaim));
void fixup_freespace(zone_t * zone, int direct_reclaim)
{
	if (direct_reclaim) {
		struct page * page;
		int worktodo = max_t(int, 64, zone->pages_min-zone->free_pages);
		do {
			if ((page = reclaim_page(zone))) {
				if (page_count(page) != 1)
					printk("fixup_freespace(): incorrect sub-page count %08x, of page %016Lx(%08lx).\n", page_count(page), (unsigned long long)(page-mem_map)*PAGE_SIZE, page->flags);
				set_page_count(page, 0);
				__free_pages_ok(page, 0);
			}
		} while (page && worktodo-- > 0);
	}
}

#define PAGES_KERNEL	0
#define PAGES_MIN	1
#define PAGES_LOW	2
#define PAGES_HIGH	3

static int enough_freeable_memory(zone_t *z)
{
	int pages;

	pages = z->free_pages + z->inactive_clean_pages +
		z->inactive_laundry_pages + z->inactive_dirty_pages +
		z->active_cache_pages + z->active_anon_pages;

	if (pages > z->size * 10 / 100)
		return 1;
	else
		return 0;
}

/*
 * This function does the dirty work for __alloc_pages
 * and is separated out to keep the code size smaller.
 * (suggested by Davem at 1:30 AM, typed by Rik at 6 AM)
 */
static struct page * __alloc_pages_limit(zonelist_t *zonelist,
			unsigned long order, int limit, int direct_reclaim, int wired)
{
	zone_t **zone = zonelist->zones;
	unsigned long water_mark = 0;

	for (;;) {
		zone_t *z = *(zone++);

		if (!z)
			break;
		if (!z->size)
			continue;

		/*
		 * We allocate if the number of (free + inactive_clean)
		 * pages is above the watermark.
		 */
		switch (limit) {
			case PAGES_KERNEL:
				water_mark = z->pages_min / 2;
				break;
			case PAGES_MIN:
				water_mark = z->pages_min;
				break;
			case PAGES_LOW:
				water_mark = z->pages_low;
				break;
			default:
			case PAGES_HIGH:
				water_mark = z->pages_high;
		}

		if (z->free_pages + z->inactive_clean_pages >= water_mark) {
			struct page *page = NULL;

			page = rmqueue(z, order);
			/* Fall back to direct reclaim if possible */
			if (!page && direct_reclaim)
				page = reclaim_page(z);
			if (page)
				return page;
		}

		if (wired && enough_freeable_memory(z))
			break;
	}

	/* Found nothing. */
	return NULL;
}

/*
 * This is the 'heart' of the zoned buddy allocator:
 */
struct page * __alloc_pages(unsigned int gfp_mask, unsigned int order, zonelist_t *zonelist)
{
	zone_t **zone;
	int min, direct_reclaim = 0;
	struct page * page;
	int wired = gfp_mask & __GFP_WIRED;

	/*
	 * (If anyone calls gfp from interrupts nonatomically then it
	 * will sooner or later tripped up by a schedule().)
	 *
	 * We fall back to lower-level zones if allocation
	 * in a higher zone fails.
	 */

	/*
	 * Clear any possible error bits that might have been propagated
	 * from the "gfp_mask" field of the "address_space" structure via
	 * page_cache_alloc() or grab_cache_page().
	 */
	gfp_mask &= __GFP_BITS_MASK;

	/*
	 * Can we take pages directly from the inactive_clean
	 * list?
	 */
	if (order == 0 && (gfp_mask & __GFP_WAIT))
		direct_reclaim = 1;

try_again:
	/*
	 * First, refill the free list to at least the minimum watermark.
	 * This needs to be done in order to fulfill allocations which
	 * can't do direct reclaim, as well as higher order allocations.
	 */
	zone = zonelist->zones;
	if (!*zone)
		return NULL;
	if (direct_reclaim) {
		for (;;) {
			zone_t *z = *(zone++);
			if (!z)
				break;
			if (!z->size)
				continue;
			if (z->free_pages < z->pages_min)
				fixup_freespace(z, direct_reclaim);
		}
	}

	/*
	 * Next, try to allocate a page from a zone with a HIGH
	 * amount of (free + inactive_clean) pages.
	 *
	 * If there is a lot of activity, inactive_target
	 * will be high and we'll have a good chance of
	 * finding a page using the HIGH limit.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_HIGH, direct_reclaim, wired);
	if (page)
		return page;

	/*
	 * Then try to allocate a page from a zone with more
	 * than zone->pages_low of (free + inactive_clean) pages.
	 *
	 * When the working set is very large and VM activity
	 * is low, we're most likely to have our allocation
	 * succeed here.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_LOW, direct_reclaim, wired);
	if (page)
		return page;

	/*
	 * OK, none of the zones on our zonelist has lots
	 * of pages free.
	 *
	 * We wake up kswapd, in the hope that kswapd will
	 * resolve this situation before memory gets tight.
	 *
	 * We'll also help a bit trying to free pages, this
	 * way statistics will make sure really fast allocators
	 * are slowed down more than slow allocators and other
	 * programs in the system shouldn't be impacted as much
	 * by the hogs.
	 */
	wakeup_kswapd(gfp_mask);

	/* skip to the next node below pages_low */
	if (gfp_mask & __GFP_NUMA)
		return NULL;

	/*
	 * After waking up kswapd, we try to allocate a page
	 * from any zone which isn't critical yet.
	 *
	 * Kswapd should, in most situations, bring the situation
	 * back to normal in no time.
	 */
	page = __alloc_pages_limit(zonelist, order, PAGES_MIN, direct_reclaim, wired);
	if (page)
		return page;

	/*
	 * Kernel allocations can eat a few emergency pages.
	 * We should be able to run without this, find out why
	 * the SCSI layer isn't happy ...
	 */
	if (gfp_mask & __GFP_HIGH) {
		page = __alloc_pages_limit(zonelist, order, PAGES_KERNEL, direct_reclaim, wired);
		if (page)
			return page;
	}

	/*
	 * Oh well, we didn't succeed.
	 */
	if (!(current->flags & (PF_MEMALLOC|PF_MEMDIE))) {
		/*
		 * Are we dealing with a higher order allocation?
		 *
		 * If so, try to defragment some memory.
		 */
		if (order > 0 && (gfp_mask & __GFP_WAIT))
			goto defragment;

		/*
		 * If we arrive here, we are really tight on memory.
		 * Since kswapd didn't succeed in freeing pages for us,
		 * we need to help it.
		 *
		 * Single page allocs loop until the allocation succeeds.
		 * Multi-page allocs can fail due to memory fragmentation;
		 * in that case we bail out to prevent infinite loops and
		 * hanging device drivers ...
		 *
		 * Another issue are GFP_NOFS allocations; because they
		 * do not have __GFP_FS set it's possible we cannot make
		 * any progress freeing pages, in that case it's better
		 * to give up than to deadlock the kernel looping here.
		 *
		 * NFS: we must yield the CPU (to rpciod) to avoid deadlock.
		 */
		if (gfp_mask & __GFP_WAIT) {
			yield();
			if (!order || free_high(ALL_ZONES) >= 0) {
				int progress = try_to_free_pages(gfp_mask);
				if (progress || (gfp_mask & __GFP_FS)) {
					wired = 0;
					goto try_again;
				}
				/*
				 * Fail if no progress was made and the
				 * allocation may not be able to block on IO.
				 */
				return NULL;
			}
		}
	}

	/*
	 * Final phase: allocate anything we can!
	 *
	 * Higher order allocations, GFP_ATOMIC allocations and
	 * recursive allocations (PF_MEMALLOC) end up here.
	 *
	 * Only recursive allocations can use the very last pages
	 * in the system, otherwise it would be just too easy to
	 * deadlock the system...
	 */
	zone = zonelist->zones;
	min = 1UL << order;
	for (;;) {
		zone_t *z = *(zone++);
		struct page * page = NULL;
		if (!z)
			break;

		/*
		 * SUBTLE: direct_reclaim is only possible if the task
		 * becomes PF_MEMALLOC while looping above. This will
		 * happen when the OOM killer selects this task for
		 * death.
		 */
		if (direct_reclaim) {
			page = reclaim_page(z);
			if (page)
				return page;
		}

		/* XXX: is pages_min/4 a good amount to reserve for this? */
		min += z->pages_min / 4;
		if (z->free_pages > min || ((current->flags & PF_MEMALLOC) && !in_interrupt())) {
			page = rmqueue(z, order);
			if (page)
				return page;
		}
		if (order > 0 && vm_defragment) {
			int try_harder = vm_defragment * 64;
			while (z->inactive_clean_pages && try_harder-- > 0) {
				struct page *page;
				/* Move one page to the free list. */
				page = reclaim_page(z);
				if (!page)
					break;
				__free_page(page);
				/* Try if the allocation succeeds. */
				page = rmqueue(z, order);
				if (page)
					return page;
			}
		}
	}
	goto out_failed;


	/*
	 * Naive "defragmentation" for higher-order allocations. First we
	 * free the inactive_clean pages to see if we can allocate our
	 * allocation, then we call page_launder() to clean some dirty
	 * pages, and last we try once more.
	 *
	 * We might want to turn this into something which defragments
	 * memory based on physical page, simply by looking for unmapped
	 * pages next to pages on the free list...
	 */
defragment:
	{
		int try_harder = 0;
		unsigned int mask = 0;
		int numpages, freed;
defragment_again:
		zone = zonelist->zones;
		freed = 0;
		for (;;) {
			zone_t *z = *(zone++);
			if (!z)
				break;
			if (!z->size)
				continue;

			/*
			 * Try to free the zone's inactive laundry pages.
			 * Nonblocking in the first pass; blocking in the
			 * second pass, but never on very new IO.
			 */
			numpages = z->inactive_laundry_pages;
			if (try_harder) {
				numpages = try_harder * 64;
				rebalance_inactive(20);
				freed += rebalance_dirty_zone(z, numpages, mask);
			}

			current->flags |= PF_MEMALLOC;
			freed += rebalance_laundry_zone(z, numpages, mask);
			current->flags &= ~PF_MEMALLOC;

			while (z->inactive_clean_pages) {
				struct page * page;
				/* Move one page to the free list. */
				page = reclaim_page(z);
				if (!page)
					break;
				__free_page(page);
				/* Try if the allocation succeeds. */
				page = rmqueue(z, order);
				if (page)
					return page;
			}
			/* retry the allocation with no inactive_clean_pages */
			page = rmqueue(z, order);
			if (page)
				return page;
		}

		/* If we can wait for IO to complete, we wait... */
		if (gfp_mask & __GFP_FS) {
			if (!try_harder) {
				mask = gfp_mask;
				try_harder = 1;
				goto defragment_again;
			}
			/* Try smaller allocations indefinitely. */
			if (order <= 2 && freed)
				goto defragment_again;
			/* Try medium allocations a tunable number of times. */
			if (order < 5 && freed && try_harder <= vm_defragment) {
				try_harder++;
				goto defragment_again;
			}
		}
	}

out_failed:
	/* No luck.. */
//	printk(KERN_ERR "__alloc_pages: %lu-order allocation failed.\n", order);
	return NULL;
}

/*
 * Common helper functions.
 */
unsigned long __get_free_pages(unsigned int gfp_mask, unsigned int order)
{
	struct page * page;

	page = alloc_pages(gfp_mask, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);
}

unsigned long get_zeroed_page(unsigned int gfp_mask)
{
	struct page * page;

	page = alloc_pages(gfp_mask, 0);
	if (page) {
		void *address = page_address(page);
		clear_page(address);
		return (unsigned long) address;
	}
	return 0;
}

void __free_pages(struct page *page, unsigned int order)
{
	if (PageCompound(page)) {
		page = (struct page *)page->lru.next;
		BUG_ON(!PageCompound(page));

		if (put_page_testzero(page)) {
			if (page->lru.prev)	/* destructor? */
				(*(void(*)(struct page *))page->lru.prev)(page);
			else
				__free_pages_ok(page, order);
		}
		return;
	}
	if (!PageReserved(page) && put_page_testzero(page))
		__free_pages_ok(page, order);
}

void free_pages_ok(struct page *page, unsigned int order)
{
	if (!PageReserved(page) && put_page_testzero(page))
		__free_pages_ok(page, order);
}

void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0)
		__free_pages(virt_to_page(addr), order);
}

/*
 * These statistics are held in per-zone counters, so we need to loop
 * over each zone and read the statistics.  We use this silly macro
 * so we don't need to duplicate the code for every statistic.
 * If you have a better idea on how to implement this (cut'n'paste
 * isn't considered better), please let me know - Rik
 */
#define NR_FOO_PAGES(__function_name, __stat)	\
	unsigned int __function_name (void)	\
	{					\
		unsigned int sum = 0;		\
		zone_t *zone;			\
						\
		for_each_zone(zone)		\
			sum += zone->__stat;	\
		return sum;			\
	}

NR_FOO_PAGES(nr_free_pages, free_pages)
NR_FOO_PAGES(nr_active_anon_pages, active_anon_pages)
NR_FOO_PAGES(nr_active_cache_pages, active_cache_pages)
NR_FOO_PAGES(nr_inactive_dirty_pages, inactive_dirty_pages)
NR_FOO_PAGES(nr_inactive_laundry_pages, inactive_laundry_pages)
NR_FOO_PAGES(nr_inactive_clean_pages, inactive_clean_pages)

/*
 * Amount of free RAM allocatable as buffer memory:
 */
unsigned int nr_free_buffer_pages (void)
{
	pg_data_t *pgdat;
	unsigned int sum = 0;

	for_each_pgdat(pgdat) {
		zonelist_t *zonelist = pgdat->node_zonelists + (GFP_USER & GFP_ZONEMASK);
		zone_t **zonep = zonelist->zones;
		zone_t *zone;

		for (zone = *zonep++; zone; zone = *zonep++) {
			sum += zone->free_pages;
			sum += zone->inactive_clean_pages;
			sum += zone->inactive_laundry_pages;
			sum += zone->inactive_dirty_pages;
		}
	}

	return sum;
}

#if CONFIG_HIGHMEM
unsigned int nr_free_highpages (void)
{
	pg_data_t *pgdat;
	unsigned int pages = 0;

	for_each_pgdat(pgdat)
		pages += pgdat->node_zones[ZONE_HIGHMEM].free_pages;

	return pages;
}

unsigned int freeable_lowmem(void)
{
	unsigned int pages = 0;
	pg_data_t *pgdat;

	for_each_pgdat(pgdat) {
		pages += pgdat->node_zones[ZONE_DMA].free_pages;
		pages += pgdat->node_zones[ZONE_DMA].inactive_clean_pages;
		pages += pgdat->node_zones[ZONE_DMA].inactive_laundry_pages;
		pages += pgdat->node_zones[ZONE_DMA].inactive_dirty_pages;
		pages += pgdat->node_zones[ZONE_DMA].active_cache_pages;
		pages += pgdat->node_zones[ZONE_DMA].active_anon_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].free_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].inactive_clean_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].inactive_laundry_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].inactive_dirty_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].active_cache_pages;
		pages += pgdat->node_zones[ZONE_NORMAL].active_anon_pages;
	}

	return pages;
}
#endif

#define K(x) ((x) << (PAGE_SHIFT-10))

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas_core(pg_data_t *pgdat)
{
 	unsigned int order;
	unsigned type;
	pg_data_t *tmpdat = pgdat;

	while (tmpdat) {
		zone_t *zone;
		for (zone = tmpdat->node_zones;
			       	zone < tmpdat->node_zones + MAX_NR_ZONES; zone++)
			printk("Zone:%s freepages:%6lu min:%6lu low:%6lu " 
				       "high:%6lu\n", 
					zone->name,
					zone->free_pages,
					zone->pages_min,
					zone->pages_low,
					zone->pages_high);
			
		tmpdat = tmpdat->node_next;
	}

	printk("Free pages:      %6d (%6d HighMem)\n",
		nr_free_pages(),
		nr_free_highpages());

	printk("( Active: %d/%d, inactive_laundry: %d, inactive_clean: %d, free: %d )\n",
		nr_active_anon_pages() + nr_active_cache_pages(),
		nr_inactive_dirty_pages(),
		nr_inactive_laundry_pages(),
		nr_inactive_clean_pages(),
		nr_free_pages());

	tmpdat = pgdat; 
	while (tmpdat) {
		zone_t *zone;
		for (zone = tmpdat->node_zones;
		     zone < tmpdat->node_zones + MAX_NR_ZONES; zone++)
			printk("  aa:%ld ac:%ld id:%ld il:%ld ic:%ld fr:%ld\n",
				zone->active_anon_pages,
				zone->active_cache_pages,
				zone->inactive_dirty_pages,
				zone->inactive_laundry_pages,
				zone->inactive_clean_pages,
				zone->free_pages);

		tmpdat = tmpdat->node_next;
	}

	for (type = 0; type < MAX_NR_ZONES; type++) {
		struct list_head *head, *curr;
		zone_t *zone = pgdat->node_zones + type;
		unsigned long total, flags;
		unsigned long nr[MAX_ORDER];

		total = 0;
		if (zone->size) {
			local_irq_save(flags);
			if (!spin_trylock(&zone->lock)) {
				printk("[%s zone locked]\n", zone->name);
				local_irq_restore(flags);
				continue;
			}
		 	for (order = 0; order < MAX_ORDER; order++) {
				head = &(zone->free_area + order)->free_list;
				curr = head;
				nr[order] = 0;
				for (;;) {
					if ((curr = curr->next) == head)
						break;
					nr[order]++;
				}
				total += nr[order] * (1 << order);
			}
			spin_unlock(&zone->lock);
			local_irq_restore(flags);
			/*
			 * Move the printk out of the irq-spinlock range
			 * to avoid nmi_watchdog timer kicks off dump.
			 */
		 	for (order = 0; order < MAX_ORDER; order++) {
				printk("%lu*%lukB ", nr[order], K(1UL) << order);
			}
		}
		if (zone->size)
			printk("= %lukB)\n", K(total));
	}

#ifdef SWAP_CACHE_INFO
	show_swap_cache_info();
#endif	
}

extern int slabpages;
extern int nr_threads;
extern atomic_t lowmem_pagetables, highmem_pagetables;
#ifdef CONFIG_HIGHMEM
extern atomic_t bouncepages;
extern int nr_emergency_pages;
#endif

void show_free_areas(void)
{
	show_free_areas_core(pgdat_list);
	printk("%d pages of slabcache\n", slabpages);
	printk("%d pages of kernel stacks\n", nr_threads * 2);
	printk("%d lowmem pagetables, %d highmem pagetables\n", 
		atomic_read(&lowmem_pagetables),
		atomic_read(&highmem_pagetables));
#ifdef CONFIG_HIGHMEM
	printk("%d bounce buffer pages, %d are on the emergency list\n",
		atomic_read(&bouncepages), nr_emergency_pages);
#endif
}

/*
 * Builds allocation fallback zone lists.
 */
static inline void build_zonelists(pg_data_t *pgdat)
{
	int i, j, k;

	for (i = 0; i <= GFP_ZONEMASK; i++) {
		zonelist_t *zonelist;
		zone_t *zone;

		zonelist = pgdat->node_zonelists + i;
		memset(zonelist, 0, sizeof(*zonelist));

		j = 0;
		k = ZONE_NORMAL;
		if (i & __GFP_HIGHMEM)
			k = ZONE_HIGHMEM;
		if (i & __GFP_DMA)
			k = ZONE_DMA;

		switch (k) {
			default:
				BUG();
			/*
			 * fallthrough:
			 */
			case ZONE_HIGHMEM:
				zone = pgdat->node_zones + ZONE_HIGHMEM;
				if (zone->size) {
#ifndef CONFIG_HIGHMEM
					BUG();
#endif
					zonelist->zones[j++] = zone;
				}
			case ZONE_NORMAL:
				zone = pgdat->node_zones + ZONE_NORMAL;
				if (zone->size)
					zonelist->zones[j++] = zone;
#if defined(CONFIG_HIGHMEM64G) || defined(CONFIG_X86_64)
				break;
#endif				
			case ZONE_DMA:
				zone = pgdat->node_zones + ZONE_DMA;
				if (zone->size)
					zonelist->zones[j++] = zone;
		}
		zonelist->zones[j++] = NULL;
	} 
}

/*
 * Helper functions to size the waitqueue hash table.
 * Essentially these want to choose hash table sizes sufficiently
 * large so that collisions trying to wait on pages are rare.
 * But in fact, the number of active page waitqueues on typical
 * systems is ridiculously low, less than 200. So this is even
 * conservative, even though it seems large.
 *
 * The constant PAGES_PER_WAITQUEUE specifies the ratio of pages to
 * waitqueues, i.e. the size of the waitq table given the number of pages.
 */
#define PAGES_PER_WAITQUEUE	256

static inline unsigned long wait_table_size(unsigned long pages)
{
	unsigned long size = 1;

	pages /= PAGES_PER_WAITQUEUE;

	while (size < pages)
		size <<= 1;

	/*
	 * Once we have dozens or even hundreds of threads sleeping
	 * on IO we've got bigger problems than wait queue collision.
	 * Limit the size of the wait table to a reasonable size.
	 */
	size = min(size, 4096UL);

	return size;
}

/*
 * This is an integer logarithm so that shifts can be used later
 * to extract the more random high bits from the multiplicative
 * hash function before the remainder is taken.
 */
static inline unsigned long wait_table_bits(unsigned long size)
{
	return ffz(~size);
}

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

static unsigned long memmap_init(struct page *start, struct page *end,
	int zone, unsigned long start_paddr, int highmem) 
{
	struct page *page;

	for (page = start; page < end; page++) {
		set_page_zone(page, zone);
		set_page_count(page, 0);
		SetPageReserved(page);
		INIT_LIST_HEAD(&page->list);
		if (!highmem)
			set_page_address(page, __va(start_paddr));
		start_paddr += PAGE_SIZE;
	}
	return start_paddr;
}

#ifdef HAVE_ARCH_MEMMAP_INIT
#define MEMMAP_INIT(start, end, zone, paddr, highmem) \
	arch_memmap_init(memmap_init, start, end, zone, paddr, highmem)
#else
#define MEMMAP_INIT(start, end, zone, paddr, highmem) \
	memmap_init(start, end, zone, paddr, highmem)
#endif

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */
extern int kswapd_minfree;
void __init free_area_init_core(int nid, pg_data_t *pgdat, struct page **gmap,
	unsigned long *zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size, struct page *lmem_map)
{
	unsigned long i, j;
	unsigned long map_size;
	unsigned long totalpages, offset, realtotalpages;
	const unsigned long zone_required_alignment = 1UL << (MAX_ORDER-1);

	if (zone_start_paddr & ~PAGE_MASK)
		BUG();

	totalpages = realtotalpages = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		unsigned long size = zones_size[i];
		unsigned long realsize = size;
		if (zholes_size)
			realsize -= zholes_size[i];

		totalpages += size;
		realtotalpages += realsize;

		if (i == ZONE_DMA || i == ZONE_NORMAL)
			heuristic_lowmem_pages += realsize;
		heuristic_all_pages += realsize;
	}

	printk("On node %d totalpages: %lu\n", nid, realtotalpages);

	/*
	 * Some architectures (with lots of mem and discontinous memory
	 * maps) have to search for a good mem_map area:
	 * For discontigmem, the conceptual mem map array starts from 
	 * PAGE_OFFSET, we need to align the actual array onto a mem map 
	 * boundary, so that MAP_NR works.
	 */
	map_size = (totalpages + 1)*sizeof(struct page);
	if (lmem_map == (struct page *)0) {
		lmem_map = (struct page *) alloc_bootmem_node(pgdat, map_size);
		lmem_map = (struct page *)(PAGE_OFFSET + 
			MAP_ALIGN((unsigned long)lmem_map - PAGE_OFFSET));
	}
	*gmap = pgdat->node_mem_map = lmem_map;
	pgdat->node_size = totalpages;
	pgdat->node_start_paddr = zone_start_paddr;
	pgdat->node_start_mapnr = (lmem_map - mem_map);
	pgdat->nr_zones = 0;

	offset = lmem_map - mem_map;	
	for (j = 0; j < MAX_NR_ZONES; j++) {
		int k;
		zone_t *zone = pgdat->node_zones + j;
		unsigned long mask, extrafree = 0;
		unsigned long size, realsize;

		zone_table[nid * MAX_NR_ZONES + j] = zone;
		realsize = size = zones_size[j];
		if (zholes_size)
			realsize -= zholes_size[j];

		printk("zone(%lu): %lu pages.\n", j, size);
		zone->size = size;
		zone->name = zone_names[j];

		for (k = 0; k < NR_CPUS; k++) {
			per_cpu_t *per_cpu = zone->cpu_pages + k;

			INIT_LIST_HEAD(&per_cpu->head);
			per_cpu->nr_pages = 0;
			per_cpu->max_nr_pages = realsize / smp_num_cpus / 128;
			if (per_cpu->max_nr_pages > MAX_PER_CPU_PAGES)
				per_cpu->max_nr_pages = MAX_PER_CPU_PAGES;
			else if (!per_cpu->max_nr_pages)
				per_cpu->max_nr_pages = 1;
		}
		zone->lock = SPIN_LOCK_UNLOCKED;
		zone->zone_pgdat = pgdat;
		zone->free_pages = 0;
		zone->active_anon_pages = 0;
		zone->active_cache_pages = 0;
		zone->inactive_clean_pages = 0;
		zone->inactive_laundry_pages = 0;
		zone->inactive_dirty_pages = 0;
		zone->need_balance = 0;
		zone->need_scan = 0;
		zone->age_interval = HZ;
		zone->age_next = jiffies;
		for (k = 0; k <= MAX_AGE ; k++) {
			INIT_LIST_HEAD(&zone->active_anon_list[k]);
			zone->active_anon_count[k] = 0;
		}
		for (k = 0; k <= MAX_AGE ; k++) {
			INIT_LIST_HEAD(&zone->active_cache_list[k]);
			zone->active_cache_count[k] = 0;
		}
		zone->cache_age_bias = 0;
		zone->anon_age_bias = 0;
		INIT_LIST_HEAD(&zone->inactive_dirty_list);
		INIT_LIST_HEAD(&zone->inactive_laundry_list);
		INIT_LIST_HEAD(&zone->inactive_clean_list);
		INIT_LIST_HEAD(&zone_wired[nid * MAX_NR_ZONES + j].wired_list);
		zone_wired[nid * MAX_NR_ZONES + j].wired_pages = 0;
		spin_lock_init(&zone->lru_lock);

		if (!size)
			continue;

		/*
		 * The per-page waitqueue mechanism uses hashed waitqueues
		 * per zone.
		 */
		zone->wait_table_size = wait_table_size(size);
		zone->wait_table_shift =
			BITS_PER_LONG - wait_table_bits(zone->wait_table_size);
		zone->wait_table = (wait_queue_head_t *)
			alloc_bootmem_node(pgdat, zone->wait_table_size
						* sizeof(wait_queue_head_t));

		for(i = 0; i < zone->wait_table_size; ++i)
			init_waitqueue_head(zone->wait_table + i);

		pgdat->nr_zones = j+1;

		/*
		 * On large memory machines we keep extra memory
		 * free for kernel allocations.
		 */
		if (zone_extrafree_ratio[j])
			extrafree = min_t(int, (realtotalpages / zone_extrafree_ratio[j]), zone_extrafree_max[j]);
		if (extrafree < zone_balance_max[j])
			extrafree = 0;

		mask = (realsize / zone_balance_ratio[j]);
		if (mask < zone_balance_min[j])
			mask = zone_balance_min[j];
		zone->pages_min = extrafree + min(mask, (unsigned long)zone_balance_max[j]);
		zone->pages_low = extrafree + mask*2;
		zone->pages_high = extrafree + mask*3;
		zone->pages_plenty = extrafree + mask*6;
		zone->zone_mem_map = mem_map + offset;
		zone->zone_start_mapnr = offset;
		zone->zone_start_paddr = zone_start_paddr;

		if ((zone_start_paddr >> PAGE_SHIFT) & (zone_required_alignment-1))
			printk("BUG: wrong zone alignment, it will crash\n");

		kswapd_minfree += zone->pages_min;

		/*
		 * Initially all pages are reserved - free ones are freed
		 * up by free_all_bootmem() once the early boot process is
		 * done. Non-atomic initialization, single-pass.
		 */
		zone_start_paddr = MEMMAP_INIT(mem_map + offset,
				mem_map + offset + size,
				nid * MAX_NR_ZONES + j, zone_start_paddr,
				(j == ZONE_HIGHMEM ? 1 : 0));

		offset += size;
		for (i = 0; ; i++) {
			unsigned long bitmap_size;

			INIT_LIST_HEAD(&zone->free_area[i].free_list);
			if (i == MAX_ORDER-1) {
				zone->free_area[i].map = NULL;
				break;
			}

			/*
			 * Page buddy system uses "index >> (i+1)",
			 * where "index" is at most "size-1".
			 *
			 * The extra "+3" is to round down to byte
			 * size (8 bits per byte assumption). Thus
			 * we get "(size-1) >> (i+4)" as the last byte
			 * we can access.
			 *
			 * The "+1" is because we want to round the
			 * byte allocation up rather than down. So
			 * we should have had a "+7" before we shifted
			 * down by three. Also, we have to add one as
			 * we actually _use_ the last bit (it's [0,n]
			 * inclusive, not [0,n[).
			 *
			 * So we actually had +7+1 before we shift
			 * down by 3. But (n+8) >> 3 == (n >> 3) + 1
			 * (modulo overflows, which we do not have).
			 *
			 * Finally, we LONG_ALIGN because all bitmap
			 * operations are on longs.
			 */
			bitmap_size = (size-1) >> (i+4);
			bitmap_size = LONG_ALIGN(bitmap_size+1);
			bitmap_size *= 2;
			zone->free_area[i].map = 
			  (unsigned long *) alloc_bootmem_node(pgdat, bitmap_size);
		}
	}
	build_zonelists(pgdat);
}

void __init free_area_init(unsigned long *zones_size)
{
	free_area_init_core(0, &contig_page_data, &mem_map, zones_size, 0, 0, 0);
}

static int __init setup_mem_frac(char *str)
{
	int j = 0;

	while (get_option(&str, &zone_balance_ratio[j++]) == 2);
	printk("setup_mem_frac: ");
	for (j = 0; j < MAX_NR_ZONES; j++) printk("%d  ", zone_balance_ratio[j]);
	printk("\n");
	return 1;
}

__setup("memfrac=", setup_mem_frac);

#ifdef CONFIG_HIGHMEM
void __init reset_highmem_zone(int highmempages)
{

	pg_data_t	*pgdat;
	int		sum;

	sum = 0;
	pgdat = pgdat_list;

	/* sum up the highpages */
	while (pgdat) {
		sum += (pgdat->node_zones+ZONE_HIGHMEM)->pages_high;
		pgdat = pgdat->node_next;
	}

	pgdat = pgdat_list;
	/* zero the watermarks and the free count if there's no at least high pages */
	if (highmempages <= sum) {

		while (pgdat) {
			(pgdat->node_zones+ZONE_HIGHMEM)->size = 0;
			(pgdat->node_zones+ZONE_HIGHMEM)->pages_min = 0;
			(pgdat->node_zones+ZONE_HIGHMEM)->pages_low = 0;
			(pgdat->node_zones+ZONE_HIGHMEM)->pages_high = 0;
			(pgdat->node_zones+ZONE_HIGHMEM)->pages_plenty = 0;
			(pgdat->node_zones+ZONE_HIGHMEM)->free_pages = 0;
			pgdat = pgdat->node_next;
		}
	}

}
#endif

static inline int long_log2(unsigned long x) __attribute__((pure));
static inline int long_log2(unsigned long x)
{
	int r = 0;
	for (x >>= 1; x > 0; x >>= 1)
		r++;
	return r;
}

/*
 * allocate a large system hash table from bootmem
 * - it is assumed that the hash table must contain an exact power-of-2
 *   quantity of entries
 */
void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     int scale,
				     int consider_highmem,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask)
{
	unsigned long estimate, mem, max, log2qty, size;
	void *table;

	/* determine applicable memory size, rounded up to nearest megabyte */
	mem = consider_highmem ? heuristic_all_pages : heuristic_lowmem_pages;
	mem += (1UL << (20 - PAGE_SHIFT)) - 1;
	mem >>= 20 - PAGE_SHIFT;
	mem <<= 20 - PAGE_SHIFT;

	/* estimate the number of buckets by requesting 1 bucket per 2^scale
	 * bytes of memory (rounded up to nearest power of 2 in size) */
	if (scale > PAGE_SHIFT)
		estimate = mem >> (scale - PAGE_SHIFT);
	else
		estimate = mem << (PAGE_SHIFT - scale);

	estimate = 1UL << (long_log2(estimate - 1UL) + 1);

	/* limit the allocation size */
	max = (1UL << (PAGE_SHIFT + MAX_SYS_HASH_TABLE_ORDER)) / bucketsize;
	if (estimate > max)
		estimate = max;

	log2qty = long_log2(estimate);

	do {
		size = bucketsize << log2qty;

		table = alloc_bootmem(size);

	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	printk("%s hash table entries: %u (order: %d, %lu KB)\n",
	       tablename,
	       (1U << log2qty),
	       long_log2(size) - PAGE_SHIFT,
	       size / 1024);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}
