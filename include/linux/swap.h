#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <linux/spinlock.h>
#include <asm/page.h>
#include <linux/brlock.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

#define MAX_SWAPFILES 32

/*
 * Magic header for a swap area. The first part of the union is
 * what the swap magic looks like for the old (limited to 128MB)
 * swap area format, the second part of the union adds - in the
 * old reserved area - some extra information. Note that the first
 * kilobyte is reserved for boot loader or disk label stuff...
 *
 * Having the magic at the end of the PAGE_SIZE makes detecting swap
 * areas somewhat tricky on machines that support multiple page sizes.
 * For 2.5 we'll probably want to move the magic to just beyond the
 * bootbits...
 */
union swap_header {
	struct 
	{
		char reserved[PAGE_SIZE - 10];
		char magic[10];			/* SWAP-SPACE or SWAPSPACE2 */
	} magic;
	struct 
	{
		char	     bootbits[1024];	/* Space for disklabel etc. */
		unsigned int version;
		unsigned int last_page;
		unsigned int nr_badpages;
		unsigned int padding[125];
		unsigned int badpages[1];
	} info;
};

#ifdef __KERNEL__

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

#include <asm/atomic.h>

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

/*
 * The in-memory structure used to track swap areas.
 */
struct swap_info_struct {
	unsigned int flags;
	kdev_t swap_device;
	spinlock_t sdev_lock;
	struct dentry * swap_file;
	struct vfsmount *swap_vfsmnt;
	unsigned short * swap_map;
	unsigned int lowest_bit;
	unsigned int highest_bit;
	unsigned int cluster_next;
	unsigned int cluster_nr;
	int prio;			/* swap priority */
	int pages;
	unsigned long max;
	int next;			/* next entry on swap list */
};

extern int nr_swap_pages;

/* Swap 50% full? Release swapcache more aggressively.. */
#define vm_swap_full() (nr_swap_pages*2 < total_swap_pages)

extern unsigned int nr_free_pages(void);
extern unsigned int nr_free_buffer_pages(void);
extern unsigned int nr_active_anon_pages(void);
extern unsigned int nr_active_cache_pages(void);
extern unsigned int nr_inactive_dirty_pages(void);
extern unsigned int nr_inactive_laundry_pages(void);
extern unsigned int nr_inactive_clean_pages(void);
extern unsigned int freeable_lowmem(void);
extern atomic_t page_cache_size;
extern atomic_t buffermem_pages;

#if 1

static inline void
	lock_pagecache(void) { br_write_lock(BR_PAGECACHE_LOCK); }
static inline void
	unlock_pagecache(void) { br_write_unlock(BR_PAGECACHE_LOCK); }
static inline void
	lock_pagecache_readonly(void) { br_read_lock(BR_PAGECACHE_LOCK); }
static inline void
	unlock_pagecache_readonly(void) { br_read_unlock(BR_PAGECACHE_LOCK); }

#else

extern spinlock_cacheline_t pagecache_lock_cacheline;
#define __pagecache_lock (pagecache_lock_cacheline.lock)

static inline void
	lock_pagecache(void) { spin_lock(&__pagecache_lock); }
static inline void
	unlock_pagecache(void) { spin_unlock(&__pagecache_lock); }
static inline void
	lock_pagecache_readonly(void) { spin_lock(&__pagecache_lock); }
static inline void
	unlock_pagecache_readonly(void) { spin_unlock(&__pagecache_lock); }

#endif

extern void __remove_inode_page(struct page *);

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;

struct zone_t;

/* linux/mm/rmap.c */
struct pte_chain;
extern int FASTCALL(page_referenced(struct page *, int *));
extern int FASTCALL(page_referenced_lock(struct page *, int *));
extern struct pte_chain * FASTCALL(page_add_rmap(struct page *, pte_t *,
					struct pte_chain *));
extern void FASTCALL(page_remove_rmap(struct page *, pte_t *));
extern int FASTCALL(try_to_unmap(struct page *));
struct pte_chain * pte_chain_alloc(int);
void __pte_chain_free(struct pte_chain *);

static inline void pte_chain_free(struct pte_chain * pte_chain)
{
	if (pte_chain)
		__pte_chain_free(pte_chain);
}

/* return values of try_to_unmap */
#define	SWAP_SUCCESS	0
#define	SWAP_AGAIN	1
#define	SWAP_FAIL	2
#define	SWAP_ERROR	3

/* linux/mm/swap.c */
extern void FASTCALL(lru_cache_add(struct page *));
extern void FASTCALL(lru_cache_add_dirty(struct page *));
extern void FASTCALL(__lru_cache_del(struct page *));
extern void FASTCALL(lru_cache_del(struct page *));

extern void FASTCALL(activate_page(struct page *));
extern void FASTCALL(activate_page_nolock(struct page *));
extern void FASTCALL(deactivate_page(struct page *));
extern void FASTCALL(deactivate_page_nolock(struct page *));
extern void FASTCALL(drop_page(struct page *));

extern void swap_setup(void);

/* linux/mm/vmscan.c */
extern struct page * FASTCALL(reclaim_page(zone_t *));
extern wait_queue_head_t kswapd_wait;
extern int FASTCALL(try_to_free_pages(unsigned int gfp_mask));
extern int rebalance_laundry_zone(struct zone_struct *, int, unsigned int);
extern int rebalance_dirty_zone(struct zone_struct *, int, unsigned int);
extern int rebalance_inactive(int);
extern void wakeup_kswapd(unsigned int);
extern void rss_free_pages(unsigned int);

/*
 * Limits, in percent, on how large the cache can be and how to do
 * page reclaiming.  If the cache is more than borrow% in size, we
 * reclaim pages from the cache and won't swap out application pages.
 * Check mm/vmscan.c for implementation details.
 */
struct cache_limits {
	int min;
	int borrow;
	int max;
};
extern struct cache_limits cache_limits;

/* linux/mm/page_io.c */
extern void rw_swap_page(int, struct page *);
extern void rw_swap_page_nolock(int, swp_entry_t, char *);

/* linux/mm/page_alloc.c */

/* linux/mm/swap_state.c */
#define SWAP_CACHE_INFO
#ifdef SWAP_CACHE_INFO
extern void show_swap_cache_info(void);
#endif
extern int add_to_swap_cache(struct page *, swp_entry_t);
extern int add_to_swap(struct page *);
extern void __delete_from_swap_cache(struct page *page);
extern void delete_from_swap_cache(struct page *page);
extern void free_page_and_swap_cache(struct page *page);
extern struct page * lookup_swap_cache(swp_entry_t);
extern struct page * read_swap_cache_async(swp_entry_t);

/* linux/mm/oom_kill.c */
extern void out_of_memory(void);

/* linux/mm/swapfile.c */
extern int total_swap_pages;
extern unsigned int nr_swapfiles;
extern struct swap_info_struct swap_info[];
extern int is_swap_partition(kdev_t);
extern void si_swapinfo(struct sysinfo *);
extern swp_entry_t get_swap_page(void);
extern void get_swaphandle_info(swp_entry_t, unsigned long *, kdev_t *, 
					struct inode **);
extern int swap_duplicate(swp_entry_t);
extern int valid_swaphandles(swp_entry_t, unsigned long *);
extern void swap_free(swp_entry_t);
extern void free_swap_and_cache(swp_entry_t);
struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};
extern struct swap_list_t swap_list;
asmlinkage long sys_swapoff(const char *);
asmlinkage long sys_swapon(const char *, int);


extern void FASTCALL(mark_page_accessed(struct page *));

/*
 * Page aging defines. These seem to work great in FreeBSD,
 * no need to reinvent the wheel.
 */
#define PAGE_AGE_START INITIAL_AGE
#define PAGE_AGE_ADV 3
#define PAGE_AGE_DECL 1
#define PAGE_AGE_MAX 64

/*
 * List add/del helper macros. These must be called
 * with the lru lock held!
 */
#define DEBUG_LRU_PAGE(page)			\
do {						\
	if (PageActiveAnon(page))		\
		BUG();				\
	if (PageActiveCache(page))		\
		BUG();				\
	if (PageInactiveDirty(page))		\
		BUG();				\
	if (PageInactiveLaundry(page))		\
		BUG();				\
	if (PageInactiveClean(page))		\
		BUG();				\
} while (0)

extern spinlock_t swaplock;

#define swap_list_lock()	spin_lock(&swaplock)
#define swap_list_unlock()	spin_unlock(&swaplock)
#define swap_device_lock(p)	spin_lock(&p->sdev_lock)
#define swap_device_unlock(p)	spin_unlock(&p->sdev_lock)

extern void shmem_unuse(swp_entry_t entry, struct page *page);

#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
