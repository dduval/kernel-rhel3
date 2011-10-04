/*
 *  linux/mm/vmscan.c
 *
 *  The pageout daemon, decides which pages to evict (swap out) and
 *  does the actual work of freeing them.
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 *  O(1) rmap vm, Arjan van de ven <arjanv@redhat.com>
 */

#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/cache_def.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/mm_inline.h>
#include <linux/kernel.h>

#include <asm/pgalloc.h>

static void refill_freelist(void);
static void wakeup_memwaiters(void);

int kscand_work_percent = 100;
/*
 * The "priority" of VM scanning is how much of the queues we
 * will scan in one go. A value of 6 for DEF_PRIORITY implies
 * that we'll scan 1/64th of the queues ("queue_length >> 6")
 * during a normal aging round.
 */
#define DEF_PRIORITY (6)

/*
 * Handling of caches defined in drivers, filesystems, ...
 *
 * The cache definition contains a callback for shrinking 
 * the cache.
 *
 * The [un]register_cache() functions may only be called when
 * the kernel lock is held. The shrink() functions are also
 * called with the kernel lock held.
 */
static DECLARE_MUTEX(other_caches_sem);
static LIST_HEAD(other_caches);

void register_cache(struct cache_definition *cache)
{
	down(&other_caches_sem);
	list_add(&cache->link, &other_caches);
	up(&other_caches_sem);
}

void unregister_cache(struct cache_definition *cache)
{
	down(&other_caches_sem);
	list_del(&cache->link);
	up(&other_caches_sem);
}

int shrink_other_caches(unsigned int priority, int gfp_mask)
{
	struct list_head *p;
	int ret = 0;
	
	if (down_trylock(&other_caches_sem))
		return 0;

	list_for_each_prev(p, &other_caches) {
		struct cache_definition *cache =
			list_entry(p, struct cache_definition, link);

		ret += cache->shrink(priority, gfp_mask);
	}
	up(&other_caches_sem);
	return ret;
}

static inline void age_page_up_nolock(struct page *page, int old_age)
{
	int new_age;
	
	new_age = old_age+4;
	if (new_age < 0)
		new_age = 0;
	if (new_age > MAX_AGE)
		new_age = MAX_AGE;	
		
	if (PageActiveAnon(page)) {
		del_page_from_active_anon_list(page);
		add_page_to_active_anon_list(page, new_age);	
	} else if (PageActiveCache(page)) {
		del_page_from_active_cache_list(page);
		add_page_to_active_cache_list(page, new_age);	
	} else if (PageInactiveDirty(page)) {
		del_page_from_inactive_dirty_list(page);
		add_page_to_active_list(page, new_age);	
	} else if (PageInactiveLaundry(page)) {
		del_page_from_inactive_laundry_list(page);
		add_page_to_active_list(page, new_age);	
	} else if (PageInactiveClean(page)) {
		del_page_from_inactive_clean_list(page);
		add_page_to_active_list(page, new_age);	
	} else return;

}


/* Must be called with page's pte_chain_lock held. */
static inline int page_mapping_inuse(struct page * page)
{
	struct address_space * mapping = page->mapping;

	/* Page is in somebody's page tables. */
	if (page->pte.direct)
		return 1;

	/* XXX: does this happen ? */
	if (!mapping)
		return 0;

	/* File is mmaped by somebody. */
	if (mapping->i_mmap || mapping->i_mmap_shared)
		return 1;

	return 0;
}

/**
 * reclaim_page - reclaims one page from the inactive_clean list
 * @zone: reclaim a page from this zone
 *
 * The pages on the inactive_clean can be instantly reclaimed.
 * The tests look impressive, but most of the time we'll grab
 * the first page of the list and exit successfully.
 */
struct page * reclaim_page(zone_t * zone)
{
	struct page * page = NULL;
	struct list_head * page_lru;
	swp_entry_t entry = {0};
	int maxscan;

	/*
	 * We need to hold the pagecache_lock around all tests to make sure
	 * reclaim_page() doesn't race with other pagecache users
	 */
	lru_lock(zone);
	lock_pagecache();
	maxscan = zone->inactive_clean_pages;
	while (maxscan-- && !list_empty(&zone->inactive_clean_list)) {
		page_lru = zone->inactive_clean_list.prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		BUG_ON(unlikely(!PageInactiveClean(page)));

		/* Page is being freed */
		if (unlikely(page_count(page)) == 0) {
			list_del(page_lru);
			list_add(page_lru, &zone->inactive_clean_list);
			continue;
		}

		/* Page cannot be reclaimed ?  Move to inactive_dirty list. */
		pte_chain_lock(page);
		if (unlikely(page->pte.direct || page->buffers ||
				PageReferenced(page) || PageDirty(page) ||
				page_count(page) > 1 || TryLockPage(page))) {
			del_page_from_inactive_clean_list(page);
			add_page_to_inactive_dirty_list(page);
			pte_chain_unlock(page);
			continue;
		}

		/*
		 * From here until reaching either the bottom of the loop
		 * or found_page: the pte_chain_lock is held.
		 */

		/* OK, remove the page from the caches. */
                if (PageSwapCache(page)) {
			entry.val = page->index;
			__delete_from_swap_cache(page);
			goto found_page;
		}

		if (page->mapping) {
			__remove_inode_page(page);
			goto found_page;
		}

		/* You might think that we should never ever get here.
		 * But you'd be wrong. 
		 * 
		 * The VM can grab temporary page references while
		 * scanning pages.  If the page gets unlinked while such
		 * a reference is held, we end up here with the
		 * page->count == 1 (the temp ref), but no mapping.
		 */
		del_page_from_inactive_clean_list(page);
		add_page_to_inactive_dirty_list(page);
		pte_chain_unlock(page);
		UnlockPage(page);
	}
	unlock_pagecache();
	lru_unlock(zone);
	return NULL;


found_page:
	__lru_cache_del(page);
	pte_chain_unlock(page);
	unlock_pagecache();
	lru_unlock(zone);
	if (entry.val)
		swap_free(entry);
	UnlockPage(page);
	if (page_count(page) != 1)
		printk("VM: reclaim_page, found page with count %d!\n",
				page_count(page));
	return page;
}

int inactive_clean_percent = 30;

/**
 * need_rebalance_dirty - do we need to write inactive stuff to disk?
 * @zone: the zone in question
 *
 * Returns true if the zone in question has too few inactive laundry
 * and inactive clean pages. 
 *
 */
static inline int need_rebalance_dirty(zone_t * zone)
{
	if ((zone->inactive_dirty_pages*inactive_clean_percent/100) > 
		(zone->inactive_laundry_pages + zone->inactive_clean_pages))
		return 1;

	if (zone->inactive_laundry_pages / 2 + zone->inactive_clean_pages +
			zone->free_pages < zone->pages_high)
		return 1;

	return 0;
}

/**
 * need_rebalance_laundry - does the zone have too few inactive_clean pages?
 * @zone: the zone in question
 *
 * Returns true if the zone in question has too few pages in inactive clean pages.
 * 
 */
static inline int need_rebalance_laundry(zone_t * zone)
{
	if (zone->inactive_laundry_pages > zone->inactive_clean_pages)
		return 1;

	return 0;
}

/*
 * returns the active cache ratio relative to the total active list
 * times 100 (eg. 30% cache returns 30)
 */
static inline int cache_ratio(struct zone_struct * zone)
{
        if (!zone->size)
                return 0;
        return 100 * zone->active_cache_pages / (zone->active_cache_pages +
                        zone->active_anon_pages + 1);
}

struct cache_limits cache_limits = {
        .min = 1,
        .borrow = 15,
        .max = 30,
};

static int slab_usable_pages(zone_t * inzone)
{
	pg_data_t *pgdat;
	zonelist_t *zonelist;
	zone_t **zone;

	/* fast path to prevent looking at other zones */
#if defined(CONFIG_IA64) || !defined(CONFIG_HIGHMEM)
	if (inzone->free_pages)
		return 1;
#else
	if (inzone->zone_pgdat->node_zones[ZONE_NORMAL].free_pages)
		return 1;
#endif
	if (inzone - inzone->zone_pgdat->node_zones <= ZONE_NORMAL &&
	    inzone->free_pages)
		return 1;

	/* slow path */
	for_each_pgdat(pgdat) {
		zonelist = pgdat->node_zonelists +
#if defined(CONFIG_IA64)
			ZONE_HIGHMEM;
#else
			ZONE_NORMAL;
#endif
		zone = zonelist->zones;
		if (*zone) {
			for (;;) {
				zone_t *z = *(zone++);
				if (!z)
					break;
				if (z->free_pages)
					return 1;
			}
		}
	}
	return 0;
}

/**
 * launder_page - clean dirty page, move to inactive_laundry list
 * @zone: zone to free pages in
 * @gfp_mask: what operations we are allowed to do
 * @page: the page at hand, must be on the inactive dirty list
 *
 * per-zone lru lock is assumed to be held, but this function can drop
 * it and sleep, so no other locks are allowed to be held.
 *
 * returns 0 for failure; 1 for success
 */
int launder_page(zone_t * zone, int gfp_mask, struct page *page)
{
	int over_rsslimit;

	/*
	 * Page is being freed, don't worry about it, but report progress.
	 */
	if (!atomic_inc_if_nonzero(&page->count))
		return 1;

	BUG_ON(!PageInactiveDirty(page));
	del_page_from_inactive_dirty_list(page);

	/* if pagecache is over max, don't reclaim anonymous pages */	
	if (cache_ratio(zone) > cache_limits.max && page_anon(page) &&
			free_min(zone) < 0) {
		add_page_to_active_list(page, INITIAL_AGE);
		lru_unlock(zone);
		page_cache_release(page);
		lru_lock(zone);
		return 0;
	}

	add_page_to_inactive_laundry_list(page);
	/* store the time we start IO */
	page->age = (jiffies/HZ)&255;
	/*
	 * The page is locked. IO in progress?
	 * If so, move to laundry and report progress
	 * Acquire PG_locked early in order to safely
	 * access page->mapping.
	 */
	if (unlikely(TryLockPage(page))) {
		lru_unlock(zone);
		page_cache_release(page);
		lru_lock(zone);
		return 1;
	}

	if (unlikely(page_count(page)) == 0)
		BUG();

	/*
	 * The page is in active use or really unfreeable. Move to
	 * the active list and adjust the page age if needed.
	 */
	pte_chain_lock(page);
	if (page_referenced(page, &over_rsslimit) && !over_rsslimit &&
			page_mapping_inuse(page)) {
		del_page_from_inactive_laundry_list(page);
		add_page_to_active_list(page, INITIAL_AGE);
		pte_chain_unlock(page);
		UnlockPage(page);
		lru_unlock(zone);
		page_cache_release(page);
		lru_lock(zone);
		return 0;
	}

	/*
	 * Anonymous process memory without backing store. Try to
	 * allocate it some swap space here.
	 *
	 * XXX: implement swap clustering ?
	 */
	if (page->pte.direct && !page->mapping && !page->buffers) {
		pte_chain_unlock(page);
		lru_unlock(zone);
		if (!add_to_swap(page)) {
			activate_page(page);
			UnlockPage(page);
			page_cache_release(page);
			lru_lock(zone);
			return 0;
		}
		lru_lock(zone);
		/* Note: may be on another list ! */
		if (!PageInactiveLaundry(page)) {
			UnlockPage(page);
			lru_unlock(zone);
			page_cache_release(page);
			lru_lock(zone);
			return 1;
		}
		if (unlikely(page_count(page)) == 0) {
			BUG();
			UnlockPage(page);
			lru_unlock(zone);
			page_cache_release(page);
			lru_lock(zone);
			return 1;
		}
		pte_chain_lock(page);
	}

	/*
	 * The page is mapped into the page tables of one or more
	 * processes. Try to unmap it here.
	 */
	if (page->pte.direct && page->mapping) {
		switch (try_to_unmap(page)) {
			case SWAP_ERROR:
			case SWAP_FAIL:
				goto page_active;
			case SWAP_AGAIN:
				pte_chain_unlock(page);
				UnlockPage(page);
				lru_unlock(zone);
				cpu_relax();
				page_cache_release(page);
				lru_lock(zone);
				return 0;
			case SWAP_SUCCESS:
				; /* fall through, try freeing the page below */
			/* fixme: add a SWAP_MLOCK case */
		}
	}
	pte_chain_unlock(page);

	if (PageDirty(page) && page->mapping) {
		/*
		 * The page can be dirtied after we start writing, but
		 * in that case the dirty bit will simply be set again
		 * and we'll need to write it again.
		 */
		int (*writepage)(struct page *);

		writepage = page->mapping->a_ops->writepage;
		if ((gfp_mask & __GFP_FS) && writepage &&
				(page->buffers || slab_usable_pages(zone))) {
			ClearPageDirty(page);
			SetPageLaunder(page);
			lru_unlock(zone);

			writepage(page);

			page_cache_release(page);
			lru_lock(zone);
			return 1;
		} else {
			/* We cannot write, somebody else can. */
			del_page_from_inactive_laundry_list(page);
			add_page_to_inactive_dirty_list(page);
			UnlockPage(page);
			lru_unlock(zone);
			page_cache_release(page);
			lru_lock(zone);
			return 0;
		}
	}

	/*
	 * If the page has buffers, try to free the buffer mappings
	 * associated with this page. If we succeed we try to free
	 * the page as well.
	 */
	if (page->buffers) {
		int ret;
		/* To avoid freeing our page before we're done. */
		lru_unlock(zone);

		ret = try_to_release_page(page, gfp_mask);
		UnlockPage(page);

		/* 
		 * If the buffers were the last user of the page we free
		 * the page here. Because of that we shouldn't hold the
		 * lru lock yet.
		 */
		page_cache_release(page);

		lru_lock(zone);
		return ret;
	}

	/*
	 * If the page is really freeable now, move it to the
	 * inactive_laundry list to keep LRU order.
	 *
	 * We re-test everything since the page could have been
	 * used by somebody else while we waited on IO above.
	 * This test is not safe from races; only the one in
	 * reclaim_page() needs to be.
	 */
	pte_chain_lock(page);
	if (page->mapping && !PageDirty(page) && !page->pte.direct &&
			page_count(page) == (2 + !!page->buffers)) {
		pte_chain_unlock(page);
		UnlockPage(page);
		lru_unlock(zone);
		page_cache_release(page);
		lru_lock(zone);
		return 1;
	} else {
		/*
		 * OK, we don't know what to do with the page.
		 * It's no use keeping it here, so we move it
		 * back to the active list.
		 */
 page_active:
		activate_page_nolock(page);
		pte_chain_unlock(page);
		UnlockPage(page);
		lru_unlock(zone);
		page_cache_release(page);
		lru_lock(zone);
	}
	return 0;
}

/*
 * The aging interval varies from fast to really slow, it is
 * important that we never age too fast and desirable that we
 * keep the pages sorted in order for eviction.
 *
 * Note that while most of the time kscand's recalculating of
 * the per zone aging interval should be good enough, we want
 * the ability to do "emergency wakeups" here since memory zones
 * can suddenly come under VM pressure.
 */
#define MAX_AGING_INTERVAL ((unsigned long)300*HZ)
#define MIN_AGING_INTERVAL ((unsigned long)HZ/2)
static void speedup_aging(struct zone_struct * zone)
{
	zone->need_scan++;
	if (zone->need_scan > 3) {
		unsigned long next_wakeup = jiffies + MIN_AGING_INTERVAL;
		if (time_before(next_wakeup, zone->age_next))
			zone->age_next = next_wakeup;
	}
}

/* Ages down all pages on the active list */
/* assumes the lru lock held */
static inline void kachunk_anon(struct zone_struct * zone)
{
	int k;
	if (!list_empty(&zone->active_anon_list[0]))
		return;
	if (!zone->active_anon_pages)
		return;

	for (k = 0; k < MAX_AGE; k++)  {
		list_splice_init(&zone->active_anon_list[k+1], &zone->active_anon_list[k]);
		zone->active_anon_count[k] = zone->active_anon_count[k+1];
		zone->active_anon_count[k+1] = 0;
	}

	zone->anon_age_bias++;
	speedup_aging(zone);
}

static inline void kachunk_cache(struct zone_struct * zone)
{
	int k;
	if (!list_empty(&zone->active_cache_list[0]))
		return;
	if (!zone->active_cache_pages)
		return;

	for (k = 0; k < MAX_AGE; k++)  {
		list_splice_init(&zone->active_cache_list[k+1], &zone->active_cache_list[k]);
		zone->active_cache_count[k] = zone->active_cache_count[k+1];
		zone->active_cache_count[k+1] = 0;
	}

	zone->cache_age_bias++;
	speedup_aging(zone);
}

#define BATCH_WORK_AMOUNT	64

int skip_mapped_pages = 0;

static inline int check_mapping_inuse(struct page *page)
{
	if (!skip_mapped_pages)
		return 1;
	return page_mapping_inuse(page);
}

/**
 * refill_inactive_zone - scan the active list and find pages to deactivate
 * @priority: how much are we allowed to scan
 *
 * This function will scan a portion of the active list of a zone to find
 * unused pages, those pages will then be moved to the inactive list.
 */
int refill_inactive_zone(struct zone_struct * zone, int priority, int target)
{
	struct list_head * page_lru;
	struct page * page;
	int over_rsslimit;
	int progress = 0;
	int cache_percent, total_active, cache_work, anon_work, cache_goal, maxscan;

	if (target < BATCH_WORK_AMOUNT)
		target = BATCH_WORK_AMOUNT;

	/* Take the lock while messing with the list... */
	lru_lock(zone);

	/* active cache should be at cache_limits.borrow */
	total_active = zone->active_anon_pages + zone->active_cache_pages;
	cache_goal = total_active * cache_limits.borrow/100;
	cache_goal = min(cache_goal, (total_active * cache_limits.max / 100));
	cache_goal = max(cache_goal, (total_active * cache_limits.min / 100));

	/* Calculate number of cache pages to deactivate so it's at cache_limits.borrow.
	 * -If the cache is currently at cache_limits.borrow then deactivate 50% from cache 
	 *  and 50% from anon to keep it that way.
	 * -Otherwise deactivate a number of pages from the cache thats proportional to
	 *  how far it's currently above or below cache_limits.borrow.
	 * -The difference is then deactivated from the anon active lists.
	 * -Finally, we always attempt to deactivate something from both cache and anon.
	 */
	if (zone->active_cache_pages >= cache_goal)
		cache_percent = 50 + ((zone->active_cache_pages-cache_goal)*50/(total_active-cache_goal+1));
	else
		cache_percent = 50 - ((cache_goal-zone->active_cache_pages)*50/cache_goal+1);
	cache_work = cache_percent * target/100;

	/* never below min or above max */
	if (zone->active_cache_pages < (total_active * cache_limits.min / 100))
		cache_work = 0;
	if (zone->active_cache_pages > (total_active * cache_limits.max / 100))
		cache_work = target;

	/* anon pages makes up the rest */
	anon_work = target - cache_work;

	/* anon first */
	maxscan = zone->active_anon_pages;
	while (maxscan-- > 0 && anon_work > 0 && zone->active_anon_pages) {
		if (list_empty(&zone->active_anon_list[0])) {
			kachunk_anon(zone);
			continue;
		}

		page_lru = zone->active_anon_list[0].prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		BUG_ON(unlikely(!PageActiveAnon(page)));
	
		/* Needed to follow page->mapping */
		if (TryLockPage(page)) {
			/* The page is already locked. This for sure means
			 * someone is doing stuff with it which makes it
			 * active by definition ;)
			 */
			del_page_from_active_anon_list(page);
			add_page_to_active_anon_list(page, INITIAL_AGE);
			continue;
		}

		/*
		 * Do aging on the pages.
		 */
		pte_chain_lock(page);
		if (page_referenced(page, &over_rsslimit)
				&& !over_rsslimit
				&& check_mapping_inuse(page)) {
			pte_chain_unlock(page);
			age_page_up_nolock(page, 0);
			UnlockPage(page);
			continue;
		}
		pte_chain_unlock(page);

		deactivate_page_nolock(page);
		anon_work--;
		progress++;
		UnlockPage(page);
	}
	/* then cache */
	maxscan = zone->active_cache_pages;
	while (maxscan-- > 0 && cache_work > 0 && zone->active_cache_pages) {
		if (list_empty(&zone->active_cache_list[0])) {
			kachunk_cache(zone);
			continue;
		}

		page_lru = zone->active_cache_list[0].prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		BUG_ON(unlikely(!PageActiveCache(page)));
	
		/* Needed to follow page->mapping */
		if (TryLockPage(page)) {
			/* The page is already locked. This for sure means
			 * someone is doing stuff with it which makes it
			 * active by definition ;)
			 */
			del_page_from_active_cache_list(page);
			add_page_to_active_cache_list(page, INITIAL_AGE);
			continue;
		}

		/*
		 * Do aging on the pages.
		 */
		pte_chain_lock(page);
		if (page_referenced(page, &over_rsslimit)
				&& !over_rsslimit
				&& check_mapping_inuse(page)) {
			pte_chain_unlock(page);
			age_page_up_nolock(page, 0);
			UnlockPage(page);
			continue;
		}
		pte_chain_unlock(page);

		deactivate_page_nolock(page);
		cache_work--;
		progress++;
		UnlockPage(page);
	}
	lru_unlock(zone);

	return progress;
}

static int need_active_anon_scan(struct zone_struct * zone)
{
	int low = 0, high = 0;
	int k;
	for (k=0; k < MAX_AGE/2; k++)
		low += zone->active_anon_count[k];

	for (k=MAX_AGE/2; k <= MAX_AGE; k++)
		high += zone->active_anon_count[k];

	if (high<low)
		return 1;
	return 0;
}

static int need_active_cache_scan(struct zone_struct * zone)
{
	int low = 0, high = 0;
	int k;
	for (k=0; k < MAX_AGE/2; k++)
		low += zone->active_cache_count[k];

	for (k=MAX_AGE/2; k <= MAX_AGE; k++)
		high += zone->active_cache_count[k];

	if (high<low)
		return 1;
	return 0;
}

static int scan_active_list(struct zone_struct * zone, int age,
		struct list_head * list, int count)
{
	struct list_head *page_lru , *next;
	struct page * page;
	int over_rsslimit;

	count = count * kscand_work_percent / 100;
	/* Take the lock while messing with the list... */
	lru_lock(zone);
	while (count-- > 0 && !list_empty(list)) {
		page = list_entry(list->prev, struct page, lru);
		pte_chain_lock(page);
		if (page_referenced(page, &over_rsslimit)
				&& !over_rsslimit
				&& check_mapping_inuse(page))
			age_page_up_nolock(page, age);
		else {
			list_del(&page->lru);
			list_add(&page->lru, list);
		}
		pte_chain_unlock(page);
	}
	lru_unlock(zone);
	return 0;
}

/*
 * Move max_work pages to the inactive clean list as long as there is a need
 * for this. If gfp_mask allows it, sleep for IO to finish.
 */
int rebalance_laundry_zone(struct zone_struct * zone, int max_work, unsigned int gfp_mask)
{
	struct list_head * page_lru;
	int max_loop;
	int work_done = 0;
	struct page * page;
	unsigned long local_count;

	max_loop = max_work;
	if (max_loop < BATCH_WORK_AMOUNT)
		max_loop = BATCH_WORK_AMOUNT;
	/* Take the lock while messing with the list... */
	lru_lock(zone);
	while (max_loop-- && !list_empty(&zone->inactive_laundry_list)) {
		page_lru = zone->inactive_laundry_list.prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		BUG_ON(unlikely(!PageInactiveLaundry(page)));

		/* TryLock to see if the page IO is done */
		if (TryLockPage(page)) {
			/*
			 * Page is locked (IO in progress?). If we can sleep,
			 * wait for it to finish, except when we've already
			 * done enough work.
			 */
			if ((gfp_mask & __GFP_WAIT) && (work_done < max_work)) {
				int timed_out;

				/* Page is being freed, waiting on lru lock */
				local_count = zone->inactive_laundry_pages;
				if (!atomic_inc_if_nonzero(&page->count)) {
					lru_unlock(zone);
					cpu_relax();
					lru_lock(zone);
					if (zone->inactive_laundry_pages <
					    local_count)
						work_done++;
					continue;
				}
				/* move page to tail so every caller won't wait on it */
				list_del(&page->lru);
				list_add(&page->lru, &zone->inactive_laundry_list);
				lru_unlock(zone);
				run_task_queue(&tq_disk);
				timed_out = wait_on_page_timeout(page, 5 * HZ);
				page_cache_release(page);
				lru_lock(zone);
				if (zone->inactive_laundry_pages < local_count)
					work_done++;
				/*
				 * If we timed out and the page has been in
				 * flight for over 30 seconds, this might not
				 * be the best page to wait on; move it to
				 * the head of the dirty list.
				 */
				if (timed_out && PageInactiveLaundry(page)) {
					unsigned char now;
					now = (jiffies/HZ)&255;
					if ((unsigned char)(now - page->age) > 30) {
						del_page_from_inactive_laundry_list(page);
						add_page_to_inactive_dirty_list(page);
					}
					continue;
				}
				/* We didn't make any progress for our caller,
				 * but we are actively avoiding a livelock
				 * so undo the decrement and wait on this page
				 * some more, until IO finishes or we timeout.
				 */
				max_loop++;
				continue;
			} else
				/* No dice, we can't wait for IO */
				break;
		}

		if (page->buffers) {
			page_cache_get(page);
			local_count = zone->inactive_laundry_pages;
			lru_unlock(zone);
			try_to_release_page(page, 0);
			UnlockPage(page);
			page_cache_release(page);
			lru_lock(zone);
			if (zone->inactive_laundry_pages < local_count)
				work_done++;
			if (unlikely((page->buffers != NULL)) &&
				       	PageInactiveLaundry(page)) {
				del_page_from_inactive_laundry_list(page);
				add_page_to_inactive_dirty_list(page);
				max_loop++;
				/* Eventually IO will complete. Prevent OOM. */
				work_done++;
			}
			continue;
		}

		/* Check if the page is still clean or is accessed. */
		if (unlikely(page->pte.direct || page->buffers ||
				PageReferenced(page) || PageDirty(page) ||
				page_count(page) > 1)) {
			del_page_from_inactive_laundry_list(page);
			add_page_to_inactive_dirty_list(page);
			UnlockPage(page);
			max_loop++;
			continue;
		}
		UnlockPage(page);

		/*
		 * If we get here either the IO on the page is done or
		 * IO never happened because it was clean. Either way
		 * move it to the inactive clean list.
		 */
		del_page_from_inactive_laundry_list(page);
		add_page_to_inactive_clean_list(page);
		work_done++;

		/*
		 * If we've done the minimal batch of work and there's
		 * no longer a need to rebalance, abort now.
		 */
		if ((work_done > BATCH_WORK_AMOUNT) && (!need_rebalance_laundry(zone)))
			break;
	}

	lru_unlock(zone);
	/* The number of pages freed and those still freeable. */
	return work_done + zone->inactive_laundry_pages;
}

/*
 * Move max_work pages from the dirty list as long as there is a need.
 * Start IO if the gfp_mask allows it.
 */
int rebalance_dirty_zone(struct zone_struct * zone, int max_work, unsigned int gfp_mask)
{
	struct list_head * page_lru;
	int max_loop;
	int work_done = 0;
	struct page * page;

	max_loop = zone->inactive_dirty_pages;
	if (max_loop < BATCH_WORK_AMOUNT)
		max_loop = BATCH_WORK_AMOUNT;
	/* Take the lock while messing with the list... */
	lru_lock(zone);
	while (max_loop-- && !list_empty(&zone->inactive_dirty_list)) {
		page_lru = zone->inactive_dirty_list.prev;
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		BUG_ON(unlikely(!PageInactiveDirty(page)));

		/*
		 * Note: launder_page() sleeps so we can't safely look at
		 * the page after this point!
		 *
		 * If we fail (only happens if we can't do IO) we just try
		 * again on another page; launder_page makes sure we won't
		 * see the same page over and over again.
		 */
		if (!launder_page(zone, gfp_mask, page))
			continue;

		if (++work_done > max_work)
			break;
		/*
		 * If we've done the minimal batch of work and there's
		 * no longer any need to rebalance, abort now.
		 */
		if ((work_done > BATCH_WORK_AMOUNT) && (!need_rebalance_dirty(zone)))
			break;
	}
	lru_unlock(zone);

	return work_done;
}

/* goal percentage sets the goal of the laundry+clean+free of the total zone size */
static int rebalance_inactive_zone(struct zone_struct * zone, int max_work, int goal_percentage)
{
	int inactive, total, ret = 0;

	inactive = zone->inactive_dirty_pages + zone->inactive_laundry_pages +
			zone->inactive_clean_pages + zone->free_pages;
	total = zone->active_cache_pages + zone->active_anon_pages + inactive;

	if (inactive * 100 < total * goal_percentage)
		ret = refill_inactive_zone(zone, 0, max_work);

	return ret;
}

int rebalance_inactive(int percentage)
{
	struct zone_struct * zone;
	int max_work;
	int ret = 0;

	max_work = 4 * BATCH_WORK_AMOUNT;
	/* If we're in deeper trouble, do more work */
	if (percentage >= 50)
		max_work = 8 * BATCH_WORK_AMOUNT;

	for_each_zone(zone)
		ret += rebalance_inactive_zone(zone, max_work, percentage);

	return ret;
}

/**
 * background_aging - slow background aging of zones
 * @priority: priority at which to scan
 *
 * When the VM load is low or nonexistant, this function is
 * called once a second to "sort" the pages in the VM. This
 * way we know which pages to evict once a load spike happens.
 * The effects of this function are very slow, the CPU usage
 * should be minimal to nonexistant under most loads.
 */
static inline void background_aging(int priority)
{
	struct zone_struct * zone;

	for_each_zone(zone)
		if (inactive_low(zone) > 0)
			refill_inactive_zone(zone, priority, BATCH_WORK_AMOUNT);
	for_each_zone(zone)
		if (need_rebalance_dirty(zone))
			rebalance_dirty_zone(zone, BATCH_WORK_AMOUNT, GFP_KSWAPD);
}

/*
 * Worker function for kswapd and try_to_free_pages, we get
 * called whenever there is a shortage of free/inactive_clean
 * pages.
 *
 * This function will also move pages to the inactive list,
 * if needed.
 */
static int do_try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 0;
	struct zone_struct * zone;
	pg_data_t *pgdat;

	/*
	 * Eat memory from filesystem page cache, buffer cache,
	 * dentry, inode and filesystem quota caches.
	 *
	 * Because the inactive list might be filled with pages that
	 * are freeable in principle but not freeable at the moment,
	 * make sure to always move some pages to the inactive list.
	 */
	rebalance_inactive(25);
	for_each_zone(zone) {
		if (need_rebalance_dirty(zone))
			ret += rebalance_dirty_zone(zone, BATCH_WORK_AMOUNT,  gfp_mask);

		if (need_rebalance_laundry(zone))
			ret += rebalance_laundry_zone(zone, BATCH_WORK_AMOUNT, gfp_mask);
	}

	ret += shrink_dcache_memory(DEF_PRIORITY, gfp_mask);
	ret += shrink_icache_memory(1, gfp_mask);
#ifdef CONFIG_QUOTA
	ret += shrink_dqcache_memory(DEF_PRIORITY, gfp_mask);
#endif
	ret += shrink_other_caches(DEF_PRIORITY, gfp_mask);

#ifdef CONFIG_HIGHMEM
	/* reclaim bufferheaders on highmem systems with lowmem exhaustion */
	for_each_pgdat(pgdat) {
		zone_t *zone = pgdat->node_zones;

		if (free_low(zone + ZONE_DMA) > 0 || free_low(zone + ZONE_NORMAL) > 0) {
			ret += try_to_reclaim_buffers(DEF_PRIORITY, gfp_mask);
			break;
		}
	}
#endif

	/* 	
	 * Reclaim unused slab cache memory.
	 */
	ret += kmem_cache_reap(gfp_mask);

	/*
	 * Mhwahahhaha! This is the part I really like. Giggle.
	 */
	if (!ret && free_min(ANY_ZONE) > 0 && (gfp_mask & __GFP_FS) &&
	    !((gfp_mask & __GFP_WIRED) &&
	      free_min(&pgdat_list->node_zones[ZONE_NORMAL]) < 0))
		out_of_memory();

	return ret;
}

/*
 * Worker function for kswapd and try_to_free_pages, we get
 * called whenever there is a shortage of free/inactive_clean
 * pages.
 *
 * This function will also move pages to the inactive list,
 * if needed.
 */
static int do_try_to_free_pages_kswapd(unsigned int gfp_mask)
{
	int ret = 0;
	struct zone_struct * zone;
	pg_data_t *pgdat;

	/*
	 * Eat memory from filesystem page cache, buffer cache,
	 * dentry, inode and filesystem quota caches.
	 */
	for_each_zone(zone) {
		int worktodo = max(free_low(zone), BATCH_WORK_AMOUNT);
		if (need_rebalance_laundry(zone))
			ret += rebalance_laundry_zone(zone, worktodo, 0);

		if (need_rebalance_dirty(zone))
			rebalance_dirty_zone(zone, 4 * worktodo,  gfp_mask);

		rebalance_inactive_zone(zone, max(worktodo, 4*BATCH_WORK_AMOUNT), 20);
	}

	for_each_pgdat(pgdat) {
		zone_t *zone = pgdat->node_zones;

		if (free_low(zone + ZONE_DMA) > 0 || free_low(zone + ZONE_NORMAL) > 0) {
			ret += shrink_dcache_memory(DEF_PRIORITY, gfp_mask);
			ret += shrink_icache_memory(DEF_PRIORITY, gfp_mask);
			try_to_reclaim_buffers(DEF_PRIORITY, gfp_mask);
#ifdef CONFIG_QUOTA
			ret += shrink_dqcache_memory(DEF_PRIORITY, gfp_mask);
#endif
			ret += shrink_other_caches(DEF_PRIORITY, gfp_mask);
			ret += kmem_cache_reap(gfp_mask);
			break;
		}
	}

	refill_freelist();

	return ret;
}

/**
 * refill_freelist - move inactive_clean pages to free list if needed
 *
 * Move some pages from the inactive_clean lists to the free
 * lists so atomic allocations have pages to work from. This
 * function really only does something when we don't have a 
 * userspace load on __alloc_pages().
 *
 * We refill the freelist in a bump from pages_min to pages_min * 2
 * in order to give the buddy allocator something to play with.
 */
static void refill_freelist(void)
{
	struct page * page;
	zone_t * zone;

	for_each_zone(zone) {
		if (!zone->size || zone->free_pages >= zone->pages_min)
			continue;

		while (zone->free_pages < zone->pages_min * 2) {
			page = reclaim_page(zone);
			if (!page)
				break;
			__free_page(page);
		}
	}
}

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process. 
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
int kswapd(void *unused)
{
	struct task_struct *tsk = current;

	daemonize();
	strcpy(tsk->comm, "kswapd");
	sigfillset(&tsk->blocked);
	
	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC;

	/*
	 * Kswapd main loop.
	 */
	for (;;) {
		static unsigned long recalc = 0;

		/*
		 * We try to rebalance the VM either when we have a
		 * global shortage of free pages or when one particular
		 * zone is very short on free pages.
		 */
		if (free_high(ALL_ZONES) >= 0 || free_low(ANY_ZONE) > 0)
			do_try_to_free_pages_kswapd(GFP_KSWAPD);

		refill_freelist();

		/* Once a second ... */
		if (time_after(jiffies, recalc + HZ)) {
			recalc = jiffies;

			/* Do background page aging. */
			background_aging(DEF_PRIORITY);
		}

		wakeup_memwaiters();
	}
}

static int kswapd_overloaded;
int kswapd_minfree;	/* initialized in mm/page_alloc.c */
DECLARE_WAIT_QUEUE_HEAD(kswapd_wait);
DECLARE_WAIT_QUEUE_HEAD(kswapd_done);

/**
 * wakeup_kswapd - wake up the pageout daemon
 * gfp_mask: page freeing flags
 *
 * This function wakes up kswapd and can, under heavy VM pressure,
 * put the calling task to sleep temporarily.
 */
void wakeup_kswapd(unsigned int gfp_mask)
{
	DECLARE_WAITQUEUE(wait, current);

	/* If we're in the memory freeing business ourself, don't sleep
	 * but just wake kswapd and go back to businesss.
	 */
	if (current->flags & (PF_MEMALLOC|PF_MEMDIE)) {
		wake_up_interruptible(&kswapd_wait);
		return;
	}

	/* We need all of kswapd's GFP flags, otherwise we can't sleep on it.
	 * We still wake kswapd of course.
	 */
	if ((gfp_mask & GFP_KSWAPD) != GFP_KSWAPD) {
		wake_up_interruptible(&kswapd_wait);
		return;
	}

	add_wait_queue(&kswapd_done, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);

	/* Wake kswapd .... */
	wake_up_interruptible(&kswapd_wait);

	/* ... and check if we need to wait on it */
	if ((free_low(ALL_ZONES) > (kswapd_minfree / 2)) && !kswapd_overloaded)
		schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&kswapd_done, &wait);
}

static void wakeup_memwaiters(void)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&kswapd_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	/* Don't let the processes waiting on memory get stuck, ever. */
	wake_up(&kswapd_done);

	/* Enough free RAM, we can easily keep up with memory demand. */
	if (free_low(ALL_ZONES) <= 0) {
		schedule_timeout(HZ);
		remove_wait_queue(&kswapd_wait, &wait);
		return;
	}
	remove_wait_queue(&kswapd_wait, &wait);

	/* OK, the VM is very loaded. Sleep instead of using all CPU. */
	kswapd_overloaded = 1;
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 40);
	kswapd_overloaded = 0;
	return;
}

/**
 * try_to_free_pages - run the pageout code ourselves
 * gfp_mask: mask of things the pageout code is allowed to do
 *
 * When the load on the system gets higher, it can happen
 * that kswapd no longer manages to keep enough memory
 * free. In those cases user programs allocating memory
 * will call try_to_free_pages() and help the pageout code.
 * This has the effects of freeing memory and slowing down
 * the largest memory hogs a bit.
 */
int try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 0;

	gfp_mask = pf_gfp_mask(gfp_mask);
	if (gfp_mask & __GFP_WAIT) {
		if (!(current->flags & PF_MEMALLOC)) {
			current->flags |= PF_MEMALLOC;
			ret = do_try_to_free_pages(gfp_mask);
			current->flags &= ~PF_MEMALLOC;
		} else if (gfp_mask & (__GFP_IO | __GFP_FS))
			ret = do_try_to_free_pages(gfp_mask & ~(__GFP_IO | __GFP_FS));
	}

	return ret;
}

/**
 * rss_free_pages - run part of the pageout code and slow down a bit
 * @gfp_mask: mask of things the pageout code is allowed to do
 *
 * This function is called when a task is over its RSS limit and
 * has a page fault.  It's goal is to free some memory so non-hogs
 * can run faster and slow down itself when needed so it won't eat
 * the memory non-hogs can use.
 */
void rss_free_pages(unsigned int gfp_mask)
{
	long pause = 0;
	struct zone_struct * zone;

	if (current->flags & PF_MEMALLOC)
		return;

	current->flags |= PF_MEMALLOC;

	do {
		rebalance_inactive(30);
		for_each_zone(zone) {
			if (free_plenty(zone) >= 0) {
				rebalance_laundry_zone(zone, BATCH_WORK_AMOUNT, gfp_mask);
				rebalance_dirty_zone(zone, BATCH_WORK_AMOUNT, gfp_mask);
			}
		}

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(pause);
		set_current_state(TASK_RUNNING);
		pause++;
	} while (free_high(ALL_ZONES) >= 0);

	current->flags &= ~PF_MEMALLOC;
	return;
}

/*
 * The background page scanning daemon, started as a kernel thread
 * from the init process.
 *
 * This is the part that background scans the active list to find
 * pages that are referenced and increases their age score.
 * It is important that this scan rate is not proportional to vm pressure
 * per se otherwise cpu usage becomes unbounded. On the other hand, if there's
 * no VM pressure at all it shouldn't age stuff either otherwise everything
 * ends up at the maximum age.
 */
int kscand(void *unused)
{
	struct task_struct *tsk = current;
	struct zone_struct * zone;
	unsigned long iv;
	int age;

	daemonize();
	strcpy(tsk->comm, "kscand");
	sigfillset(&tsk->blocked);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(MIN_AGING_INTERVAL);
		for_each_zone(zone) {
			if (time_before(jiffies, zone->age_next))
				continue;

			if (need_active_anon_scan(zone)) {
				for (age = MAX_AGE-1; age >= 0; age--)  {
					scan_active_list(zone, age,
						&zone->active_anon_list[age],
						zone->active_anon_count[age]);
					if (current->need_resched)
						schedule();
				}
			}

			if (need_active_cache_scan(zone)) {
				for (age = MAX_AGE-1; age >= 0; age--)  {
					scan_active_list(zone, age,
						&zone->active_cache_list[age],
						zone->active_cache_count[age]);
					if (current->need_resched)
						schedule();
				}
			}

			iv = zone->age_interval;
			/* Check if we've been aging quickly enough ... */
			if (zone->need_scan >= 2)
				iv = max(iv / 2, MIN_AGING_INTERVAL);
			/* ... or too quickly. */
			else if (!zone->need_scan)
				iv = min(iv + (iv / 2), MAX_AGING_INTERVAL);
			zone->need_scan = 0;
			zone->age_interval = iv;
			zone->age_next = jiffies + iv;
		}
	}
}

static int __init kswapd_init(void)
{
	printk("Starting kswapd\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_KERNEL);
	kernel_thread(kscand, NULL, CLONE_KERNEL);
	return 0;
}

module_init(kswapd_init)
