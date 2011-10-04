#ifndef _LINUX_MM_INLINE_H
#define _LINUX_MM_INLINE_H

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/brlock.h>


/*
 * Copyright (c) 2002. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: 
 *	Linus Torvalds
 *	Stephen Tweedie
 *	Andrea Arcangeli
 *	Rik van Riel
 *	Arjan van de Ven
 *	and others
 */

GPL_HEADER()

/*
 * internal page count APIs - use at your own risk!
 * they are hugetlb-unaware.
 */
#define put_page_testzero(p) 	atomic_dec_and_test(&(p)->count)
#define page_count(p)		atomic_read(&(p)->count)
#define set_page_count(p,v) 	atomic_set(&(p)->count, v)

/*
 * These inline functions tend to need bits and pieces of all the
 * other VM include files, meaning they cannot be defined inside
 * one of the other VM include files.
 * 
 */
 
/**
 * page_dirty - do we need to write the data out to disk
 * @page: page to test
 *
 * Returns true if the page contains data which needs to
 * be written to disk.  Doesn't test the page tables (yet?).
 */
static inline int page_dirty(struct page *page)
{
	struct buffer_head *tmp, *bh;

	if (PageDirty(page))
		return 1;

	if (page->mapping && !page->buffers)
		return 0;

	tmp = bh = page->buffers;

	do {
		if (tmp->b_state & ((1<<BH_Dirty) | (1<<BH_Lock)))
			return 1;
		tmp = tmp->b_this_page;
	} while (tmp != bh);

	return 0;
}

/**
 * page_anon - is this page ram/swap backed ?
 * @page - page to test
 *
 * Returns 1 if the page is backed by ram/swap, 0 if the page is
 * backed by a file in a filesystem on permanent storage.
 */
extern int shmem_writepage(struct page *);
static inline int page_anon(struct page * page)
{
	if (!page->mapping && !page->buffers)
		return 1;

	if (PageSwapCache(page))
		return 1;

	/*
	 * Files on tmpfs that are are shared writable mmapped are often
	 * database shared memory segments, which should receive the same
	 * priority as anonymous memory.  Yes this is ugly.
	 */
	if (page->mapping && page->mapping->i_mmap_shared &&
			page->mapping->a_ops->writepage == shmem_writepage)
		return 1;

	/* TODO: ramfs and ramdisk */

	return 0;
}



static inline void add_page_to_active_anon_list(struct page * page, int age)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageActiveAnon(page);
	BUG_ON(PageCompound(page));
	list_add(&page->lru, &zone->active_anon_list[age]);
	page->age = age + zone->anon_age_bias;
	zone->active_anon_count[age]++;
	zone->active_anon_pages++;
}

static inline void add_page_to_active_cache_list(struct page * page, int age)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageActiveCache(page);
	BUG_ON(PageCompound(page));
	list_add(&page->lru, &zone->active_cache_list[age]);
	page->age = age + zone->cache_age_bias;
	zone->active_cache_count[age]++;
	zone->active_cache_pages++;
}

static inline void add_page_to_active_list(struct page * page, int age)
{
	if (page_anon(page))
		add_page_to_active_anon_list(page, age);
	else
		add_page_to_active_cache_list(page, age);
}

static inline void add_page_to_inactive_dirty_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageInactiveDirty(page);
	BUG_ON(PageCompound(page));
	list_add(&page->lru, &zone->inactive_dirty_list);
	zone->inactive_dirty_pages++;
}

static inline void add_page_to_inactive_laundry_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageInactiveLaundry(page);
	BUG_ON(PageCompound(page));
	list_add(&page->lru, &zone->inactive_laundry_list);
	zone->inactive_laundry_pages++;
}

static inline void add_page_to_inactive_clean_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	DEBUG_LRU_PAGE(page);
	SetPageInactiveClean(page);
	BUG_ON(PageCompound(page));
	list_add(&page->lru, &zone->inactive_clean_list);
	zone->inactive_clean_pages++;
}

static inline void add_page_to_wired_list(struct page * page)
{
        struct zone_struct * zone = page_zone(page);
        DEBUG_LRU_PAGE(page);
        SetPageWired(page);
        BUG_ON(PageCompound(page));
        list_add(&page->lru, &zone_wired[page->flags>>ZONE_SHIFT].wired_list);
        zone_wired[page->flags>>ZONE_SHIFT].wired_pages++;
}

static inline void del_page_from_active_anon_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	unsigned char age;

	BUG_ON(PageCompound(page));
	list_del(&page->lru);
	ClearPageActiveAnon(page);
	zone->active_anon_pages--;
	age = page->age - zone->anon_age_bias;
	if (age<=MAX_AGE)
		zone->active_anon_count[age]--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_active_cache_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);
	unsigned char age;

	BUG_ON(PageCompound(page));
	list_del(&page->lru);
	ClearPageActiveCache(page);
	zone->active_cache_pages--;
	age = page->age - zone->cache_age_bias;
	if (age<=MAX_AGE)
		zone->active_cache_count[age]--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_inactive_dirty_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);

	BUG_ON(PageCompound(page));
	list_del(&page->lru);
	ClearPageInactiveDirty(page);
	zone->inactive_dirty_pages--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_inactive_laundry_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);

	BUG_ON(PageCompound(page));
	list_del(&page->lru);
	ClearPageInactiveLaundry(page);
	zone->inactive_laundry_pages--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_inactive_clean_list(struct page * page)
{
	struct zone_struct * zone = page_zone(page);

	BUG_ON(PageCompound(page));
	list_del(&page->lru);
	ClearPageInactiveClean(page);
	zone->inactive_clean_pages--;
	DEBUG_LRU_PAGE(page);
}

static inline void del_page_from_wired_list(struct page * page)
{
        struct zone_struct * zone = page_zone(page);

        BUG_ON(PageCompound(page));
        list_del(&page->lru);
        ClearPageWired(page);
        zone_wired[page->flags>>ZONE_SHIFT].wired_pages--;
        DEBUG_LRU_PAGE(page);
}

/*
 * Inline functions to control some balancing in the VM.
 *
 * Note that we do both global and per-zone balancing, with
 * most of the balancing done globally.
 */
#define	PLENTY_FACTOR	2
#define	ALL_ZONES	NULL
#define	ANY_ZONE	(struct zone_struct *)(~0UL)
#define INACTIVE_FACTOR	5

#define	VM_MIN	0
#define	VM_LOW	1
#define	VM_HIGH	2
#define VM_PLENTY 3
static inline int zone_free_limit(struct zone_struct * zone, int limit)
{
	int free, target, delta;

	/* This is really nasty, but GCC should completely optimise it away. */
	if (limit == VM_MIN)
		target = zone->pages_min;
	else if (limit == VM_LOW)
		target = zone->pages_low;
	else if (limit == VM_HIGH)
		target = zone->pages_high;
	else
		target = zone->pages_high * PLENTY_FACTOR;

	free = zone->free_pages + zone->inactive_clean_pages;
	delta = target - free;

	return delta;
}

static inline int free_limit(struct zone_struct * zone, int limit)
{
	int shortage = 0, local;

	if (zone == ALL_ZONES) {
		for_each_zone(zone)
			shortage += zone_free_limit(zone, limit);
	} else if (zone == ANY_ZONE) {
		for_each_zone(zone) {
			local = zone_free_limit(zone, limit);
			shortage += max(local, 0);
		}
	} else {
		shortage = zone_free_limit(zone, limit);
	}

	return shortage;
}

/**
 * free_min - test for critically low amount of free pages
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if we have a serious shortage of free and
 * clean pages, zero or negative if there is no serious shortage.
 */
static inline int free_min(struct zone_struct * zone)
{
	return free_limit(zone, VM_MIN);
}

/**
 * free_low - test for low amount of free pages
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if we have a shortage of free and
 * clean pages, zero or negative if there is no shortage.
 */
static inline int free_low(struct zone_struct * zone)
{
	return free_limit(zone, VM_LOW);
}

/**
 * free_high - test if amount of free pages is less than ideal
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if the number of free and clean
 * pages is below kswapd's target, zero or negative if we
 * have more than enough free and clean pages.
 */
static inline int free_high(struct zone_struct * zone)
{
	return free_limit(zone, VM_HIGH);
}

/**
 * free_plenty - test if enough pages are freed
 * @zone: zone to test, ALL_ZONES to test memory globally
 *
 * Returns a positive value if the number of free + clean pages
 * in a zone is not yet excessive and kswapd is still allowed to
 * free pages here, a negative value if kswapd should leave the
 * zone alone.
 */
static inline int free_plenty(struct zone_struct * zone)
{
	return free_limit(zone, VM_PLENTY);
}

/*
 * The inactive page target is the free target + 20% of (active + inactive)
 * pages. 
 */
static inline int zone_inactive_limit(struct zone_struct * zone, int limit)
{
	int inactive, target, inactive_base;

	inactive_base = zone->active_anon_pages + zone->active_cache_pages;
	inactive_base /= INACTIVE_FACTOR;

	/* GCC should optimise this away completely. */
	if (limit == VM_MIN)
		target = zone->pages_high + inactive_base / 2;
	else if (limit == VM_LOW)
		target = zone->pages_high + inactive_base;
	else
		target = zone->pages_high + inactive_base * 2;

	inactive = zone->free_pages + zone->inactive_clean_pages
		+ zone->inactive_dirty_pages + zone->inactive_laundry_pages;

	return target - inactive;
}

static inline int inactive_limit(struct zone_struct * zone, int limit)
{
	int shortage = 0, local;

	if (zone == ALL_ZONES) {
		for_each_zone(zone)
			shortage += zone_inactive_limit(zone, limit);
	} else if (zone == ANY_ZONE) {
		for_each_zone(zone) {
			local = zone_inactive_limit(zone, limit);
			shortage += max(local, 0);
		}
	} else {
		shortage = zone_inactive_limit(zone, limit);
	}

	return shortage;
}

/**
 * inactive_min - test for serious shortage of (free + inactive clean) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have no serious shortage of (free + inactive clean) pages
 */
static inline int inactive_min(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_MIN);
}

/**
 * inactive_low - test for shortage of (free + inactive clean) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have no shortage of (free + inactive clean) pages
 */
static inline int inactive_low(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_LOW);
}

/**
 * inactive_high - less than ideal amount of (free + inactive) pages
 * @zone: zone to test, ALL_ZONES for global testing
 *
 * Returns the shortage as a positive number, a negative number
 * if we have more than enough (free + inactive) pages
 */
static inline int inactive_high(struct zone_struct * zone)
{
	return inactive_limit(zone, VM_HIGH);
}

/*
 * inactive_target - number of inactive pages we ought to have.
 */
static inline int inactive_target(void)
{
	int target;

	target = nr_active_anon_pages() + nr_active_cache_pages()
			+ nr_inactive_dirty_pages() + nr_inactive_clean_pages()
			+ nr_inactive_laundry_pages();

	target /= INACTIVE_FACTOR;

	return target;
}

static inline void lru_lock(struct zone_struct *zone)
{
	if (zone) {
		br_read_lock(BR_LRU_LOCK);
		spin_lock(&zone->lru_lock);
	} else {
		br_write_lock(BR_LRU_LOCK);
	}
}

static inline void lru_unlock(struct zone_struct *zone)
{
	if (zone) {
		spin_unlock(&zone->lru_lock);
		br_read_unlock(BR_LRU_LOCK);
	} else {
		br_write_unlock(BR_LRU_LOCK);
	}
}

#endif /* _LINUX_MM_INLINE_H */
