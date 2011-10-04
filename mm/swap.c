/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the opereation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * linux/Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swapctl.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/mm_inline.h>

#include <asm/dma.h>
#include <asm/uaccess.h> /* for copy_to/from_user */
#include <asm/pgtable.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

pager_daemon_t pager_daemon = {
	512,	/* base number for calculating the number of tries */
	SWAP_CLUSTER_MAX,	/* minimum number of tries */
	8,	/* do swap I/O in clusters of this size */
};

/**
 * (de)activate_page - move pages from/to active and inactive lists
 * @page: the page we want to move
 *
 * Deactivate_page will move an active page to the right
 * inactive list, while activate_page will move a page back
 * from one of the inactive lists to the active list. If
 * called on a page which is not on any of the lists, the
 * page is left alone.
 */
void deactivate_page_nolock(struct page * page)
{
	/*
	 * Don't touch it if it's not on the active list.
	 * (some pages aren't on any list at all)
	 */
	ClearPageReferenced(page);
	if (PageActiveAnon(page)) {
		del_page_from_active_anon_list(page);
		add_page_to_inactive_dirty_list(page);
	} else if (PageActiveCache(page)) {
		del_page_from_active_cache_list(page);
		add_page_to_inactive_dirty_list(page);
	}
}	

void deactivate_page(struct page * page)
{
	lru_lock(page_zone(page));
	deactivate_page_nolock(page);
	lru_unlock(page_zone(page));
}

/*
 * Move an inactive page to the active list.
 */
void activate_page_nolock(struct page * page)
{
	if (PageInactiveDirty(page)) {
		del_page_from_inactive_dirty_list(page);
		add_page_to_active_list(page, INITIAL_AGE);
	} else if (PageInactiveLaundry(page)) {
		del_page_from_inactive_laundry_list(page);
		add_page_to_active_list(page, INITIAL_AGE);
	} else if (PageInactiveClean(page)) {
		del_page_from_inactive_clean_list(page);
		add_page_to_active_list(page, INITIAL_AGE);
	}
}

void activate_page(struct page * page)
{
	lru_lock(page_zone(page));
	activate_page_nolock(page);
	lru_unlock(page_zone(page));
}

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
void lru_cache_add(struct page * page)
{
	if (!PageLRU(page)) {
		lru_lock(page_zone(page));
		/* pages from a WIRED inode go directly to the wired list */
		if (page->mapping && (page->mapping->gfp_mask & __GFP_WIRED))
			add_page_to_wired_list(page);
		else if (!TestandSetPageLRU(page))
			add_page_to_active_list(page, INITIAL_AGE);
		lru_unlock(page_zone(page));
	}
}

/**
 * __lru_cache_del: remove a page from the page lists
 * @page: the page to add
 *
 * This function is for when the caller already holds
 * the lru lock.
 */
void __lru_cache_del(struct page * page)
{
	if (PageActiveAnon(page)) {
		del_page_from_active_anon_list(page);
	} else if (PageActiveCache(page)) {
		del_page_from_active_cache_list(page);
	} else if (PageInactiveDirty(page)) {
		del_page_from_inactive_dirty_list(page);
	} else if (PageInactiveLaundry(page)) {
		del_page_from_inactive_laundry_list(page);
	} else if (PageInactiveClean(page)) {
		del_page_from_inactive_clean_list(page);
	} else if (PageWired(page)) {
		del_page_from_wired_list(page);
	}
	ClearPageLRU(page);
}

/**
 * lru_cache_del: remove a page from the page lists
 * @page: the page to remove
 */
void lru_cache_del(struct page * page)
{
	lru_lock(page_zone(page));
	__lru_cache_del(page);
	lru_unlock(page_zone(page));
}

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = num_physpages >> (20 - PAGE_SHIFT);

	/* Use a smaller cluster for small-memory machines */
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	/*
	 * Right now other parts of the system means that we
	 * _really_ don't want to cluster much more
	 */
}
