/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 *
 * Simple, low overhead pte-based reverse mapping scheme.
 * This is kept modular because we may want to experiment
 * with object-based reverse mapping schemes. Please try
 * to keep this thing as modular as possible.
 */

/*
 * Locking:
 * - the page->pte.chain is protected by the PG_chainlock bit,
 *   which nests within the the mm->page_table_lock,
 *   which nests within the page lock.
 * - because swapout locking is opposite to the locking order
 *   in the page fault path, the swapout path uses trylocks
 *   on the mm->page_table_lock
 */
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/cache.h>

#include <asm/pgalloc.h>
#include <asm/rmap.h>
#include <asm/tlb.h>

/* #define DEBUG_RMAP */

/*
 * Shared pages have a chain of pte_chain structures, used to locate
 * all the mappings to this page. We only need a pointer to the pte
 * here, the page struct for the page table page contains the process
 * it belongs to and the offset within that process.
 *
 * We use an array of pte pointers in this structure to minimise cache misses
 * while traversing reverse maps.
 */
#define NRPTE ((L1_CACHE_BYTES - sizeof(unsigned long))/sizeof(chain_ptep_t))

/*
 * next_and_idx encodes both the address of the next pte_chain and the
 * offset of the highest-index used pte in ptes[].
 */
struct pte_chain {
	unsigned long next_and_idx;
	chain_ptep_t ptes[NRPTE];
} ____cacheline_aligned;

static kmem_cache_t	*pte_chain_cache;

static inline struct pte_chain *pte_chain_next(struct pte_chain *pte_chain)
{
	return (struct pte_chain *)(pte_chain->next_and_idx & ~NRPTE);
}

static inline struct pte_chain *pte_chain_ptr(unsigned long pte_chain_addr)
{
	return (struct pte_chain *)(pte_chain_addr & ~NRPTE);
}

static inline int pte_chain_idx(struct pte_chain *pte_chain)
{
	return pte_chain->next_and_idx & NRPTE;
}

static inline unsigned long
pte_chain_encode(struct pte_chain *pte_chain, int idx)
{
	return (unsigned long)pte_chain | idx;
}

/*
 * pte_chain list management policy:
 *
 * - If a page has a pte_chain list then it is shared by at least two processes,
 *   because a single sharing uses PageDirect. (Well, this isn't true yet,
 *   coz this code doesn't collapse singletons back to PageDirect on the remove
 *   path).
 * - A pte_chain list has free space only in the head member - all succeeding
 *   members are 100% full.
 * - If the head element has free space, it occurs in its leading slots.
 * - All free space in the pte_chain is at the start of the head member.
 * - Insertion into the pte_chain puts a pte pointer in the last free slot of
 *   the head member.
 * - Removal from a pte chain moves the head pte of the head member onto the
 *   victim pte and frees the head member if it became empty.
 */

/**
 * pte_chain_alloc - allocate a pte_chain struct
 * @gfp_flags: allocation flags
 *
 * Returns a pointer to a fresh pte_chain structure. Allocates new
 * pte_chain structures as required.
 * Caller needs to hold the page's pte_chain_lock.
 */
struct pte_chain * pte_chain_alloc(int gfp_flags)
{
	struct pte_chain * pte_chain;

	pte_chain = kmem_cache_alloc(pte_chain_cache, gfp_flags);
#ifdef DEBUG_RMAP
	{
		int i;
		for (i = 0; i < NRPTE; i++)
			BUG_ON(pte_chain->ptes[i]);
		BUG_ON(pte_chain->next);
	}
#endif
	return pte_chain;
}

/**
 * __pte_chain_free - free pte_chain structure
 * @pte_chain: pte_chain struct to free
 */
void __pte_chain_free(struct pte_chain *pte_chain)
{
	if (pte_chain->next_and_idx)
		pte_chain->next_and_idx = 0;
	kmem_cache_free(pte_chain_cache, pte_chain);
}

/**
 ** VM stuff below this comment
 **/

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 * @rsslimit: place to put whether the page is over RSS limit
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of processes which referenced the page.
 * In addition to this it checks if the processes holding the
 * page are over or under their RSS limit.
 * Caller needs to hold the pte_chain_lock.
 *
 * If the page has a single-entry pte_chain, collapse that back to a PageDirect
 * representation.  This way, it's only done under memory pressure.
 */
int page_referenced(struct page * page, int * rsslimit)
{
	int referenced = 0, over_rsslimit = 0;
	struct mm_struct * mm;
	struct pte_chain * pc;

	if (PageTestandClearReferenced(page))
		referenced++;

	if (PageDirect(page)) {
		pte_t *pte = rmap_ptep_map(page->pte.direct);
		if (pte_young(*pte) && ptep_test_and_clear_young(pte))
			referenced++;

		mm = ptep_to_mm(pte);
		if (mm && mm->rss > mm->rlimit_rss)
			over_rsslimit = 1;
		rmap_ptep_unmap(pte);
	} else if (page->pte.chain) {
		int nr_chains = 0;
		over_rsslimit = 1;

		/* Check all the page tables mapping this page. */
		for (pc = page->pte.chain; pc; pc = pte_chain_next(pc)) {
			int i;

			for (i = NRPTE-1; i >= 0; i--) {
				chain_ptep_t pte_paddr = pc->ptes[i];
				pte_t *pte;

				if (!pte_paddr)
					break;
				pte = rmap_ptep_map(PTE_ADDR_C2D(pte_paddr));
				if (ptep_test_and_clear_young(pte))
					referenced++;
				mm = ptep_to_mm(pte);
				if (mm && mm->rss <= mm->rlimit_rss)
					over_rsslimit = 0;
				rmap_ptep_unmap(pte);
				nr_chains++;
			}
		}
		if (nr_chains == 1) {
			pc = page->pte.chain;
			page->pte.direct = PTE_ADDR_C2D(pc->ptes[NRPTE-1]);
			SetPageDirect(page);
			pc->ptes[NRPTE-1] = 0;
			__pte_chain_free(pc);
		}
	}

	/*
	 * We're only over the RSS limit if all the processes sharing the
	 * page are.
	 */
	*rsslimit = over_rsslimit;

	return referenced;
}

int page_referenced_lock(struct page * page, int * rsslimit)
{
	int ret;

	pte_chain_lock(page);
	ret = page_referenced(page, rsslimit);
	pte_chain_unlock(page);

	return ret;
}

/**
 * page_add_rmap - add reverse mapping entry to a page
 * @page: the page to add the mapping to
 * @ptep: the page table entry mapping this page
 *
 * Add a new pte reverse mapping to a page.
 * The caller needs to hold the mm->page_table_lock.
 */
struct pte_chain *
page_add_rmap(struct page * page, pte_t * ptep, struct pte_chain * pte_chain)
{
	chain_ptep_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain * cur_pte_chain;

#ifdef DEBUG_RMAP
	if (!page || !ptep)
		BUG();
	if (!pte_present(*ptep))
		BUG();
	if (!ptep_to_mm(ptep))
		BUG();
#endif

	/* wired pages don't get pte_chains */
	if (!VALID_PAGE(page) || PageReserved(page) || PageWired(page))
		return pte_chain;

	pte_chain_lock(page);

#ifdef DEBUG_RMAP
	/*
	 * This stuff needs help to get up to highmem speed.
	 */
	{
		struct pte_chain * pc;
		int i;
		if (PageDirect(page)) {
			if (page->pte.direct == PTE_ADDR_C2D(pte_paddr))
				BUG();
		} else {
			for (pc = page->pte.chain; pc; pc = pc->next) {
				for (i = 0; i < NRPTE; i++) {
					chain_ptep_t pte = pc->ptes[i];

					if (pte && pte == pte_paddr)
						BUG();
				}
			}
		}
	}
#endif

	if (page->pte.direct == 0) {
		page->pte.direct = PTE_ADDR_C2D(pte_paddr);
		SetPageDirect(page);
		goto out;
	}

	if (PageDirect(page)) {
		/* Convert a direct pointer into a pte_chain */
		ClearPageDirect(page);
		pte_chain->ptes[NRPTE-1] = PTE_ADDR_D2C(page->pte.direct);
		pte_chain->ptes[NRPTE-2] = pte_paddr;
		pte_chain->next_and_idx = pte_chain_encode(NULL, NRPTE-2);
		page->pte.direct = 0;
		page->pte.chain = pte_chain;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}

	cur_pte_chain = page->pte.chain;
	if (cur_pte_chain->ptes[0]) {	/* It's full */
		pte_chain->next_and_idx = pte_chain_encode(cur_pte_chain,
								NRPTE - 1);
		page->pte.chain = pte_chain;
		pte_chain->ptes[NRPTE-1] = pte_paddr;
		pte_chain = NULL;	/* We consumed it */
		goto out;
	}
	cur_pte_chain->ptes[pte_chain_idx(cur_pte_chain) - 1] = pte_paddr;
	cur_pte_chain->next_and_idx--;
out:
	pte_chain_unlock(page);
	return pte_chain;
}

/**
 * page_remove_rmap - take down reverse mapping to a page
 * @page: page to remove mapping from
 * @ptep: page table entry to remove
 *
 * Removes the reverse mapping from the pte_chain of the page,
 * after that the caller can clear the page table entry and free
 * the page.
 * Caller needs to hold the mm->page_table_lock.
 */
void page_remove_rmap(struct page * page, pte_t * ptep)
{
	chain_ptep_t pte_paddr = ptep_to_paddr(ptep);
	struct pte_chain *pc;

	if (!page || !ptep)
		BUG();
	if (!VALID_PAGE(page))
		return;

	pte_chain_lock(page);

	if (!page_mapped(page))
		goto out;		/* remap_page_range() from a driver? */

	if (PageReserved(page))
		printk("page_remove_rmap: reserved page with rmap...\n");

	if (PageDirect(page)) {
		if (page->pte.direct == PTE_ADDR_C2D(pte_paddr)) {
			page->pte.direct = 0;
			ClearPageDirect(page);
			goto out;
		}
	} else {
		struct pte_chain *start = page->pte.chain;
		struct pte_chain *next;
		int victim_i = -1;

		for (pc = start; pc; pc = next) {
			int i;

			next = pte_chain_next(pc);
			if (next)
				prefetch(next);
			for (i = pte_chain_idx(pc); i < NRPTE; i++) {
				chain_ptep_t pa = pc->ptes[i];

				if (victim_i == -1)
					victim_i = i;
				if (pa != pte_paddr)
					continue;
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				if (victim_i == NRPTE-1) {
					/* Emptied a pte_chain */
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
				} else {
					start->next_and_idx++;
				}
				goto out;
			}
		}
	}
#ifdef DEBUG_RMAP
	/* Not found. This should NEVER happen! */
	printk(KERN_ERR "page_remove_rmap: pte_chain %p not present.\n", ptep);
	printk(KERN_ERR "page_remove_rmap: only found: ");
	if (PageDirect(page)) {
		printk("%llx", (u64)page->pte.direct);
	} else {
		for (pc = page->pte.chain; pc; pc = pc->next) {
			int i;
			for (i = 0; i < NRPTE; i++)
				printk(" %d:%llx", i, (u64)pc->ptes[i]);
		}
	}
	printk("\n");
	printk(KERN_ERR "page_remove_rmap: driver cleared PG_reserved ?\n");
#endif

out:
	pte_chain_unlock(page);
	return;
}

/**
 * try_to_unmap_one - worker function for try_to_unmap
 * @page: page to unmap
 * @ptep: page table entry to unmap from page
 *
 * Internal helper function for try_to_unmap, called for each page
 * table entry mapping a page. Because locking order here is opposite
 * to the locking order used by the page fault path, we use trylocks.
 * Locking:
 *	   lru lock			page_launder()
 *	    page lock			page_launder(), trylock
 *		pte_chain_lock		page_launder()
 *		    mm->page_table_lock	try_to_unmap_one(), trylock
 */
static int FASTCALL(try_to_unmap_one(struct page *, pte_addr_t));
static int try_to_unmap_one(struct page * page, pte_addr_t paddr)
{
	pte_t *ptep = rmap_ptep_map(paddr);
	unsigned long address = ptep_to_address(ptep);
	struct mm_struct * mm = ptep_to_mm(ptep);
	struct vm_area_struct * vma;
	pte_t pte;
	int ret;

	if (!mm)
		BUG();

	/*
	 * We need the page_table_lock to protect us from page faults,
	 * munmap, fork, etc...
	 */
	if (!spin_trylock(&mm->page_table_lock)) {
		rmap_ptep_unmap(ptep);
		return SWAP_AGAIN;
	}

	/* The page is pinned for direct IO, can't be swapped right now. */
	if (unlikely(page_mapping_pinned(page))) {
		/* SWAP_FAIL instead of SWAP_AGAIN: this is a relatively
		 * long-term condition as far as the VM lrus are
		 * concerned. */
		ret = SWAP_FAIL; 
		goto out_unlock;
	}

	/* During mremap, it's possible pages are not in a VMA. */
	vma = find_vma(mm, address);
	if (!vma) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* The page is mlock()d, we cannot swap it out. */
	if (vma->vm_flags & VM_LOCKED) {
		ret = SWAP_FAIL;
		goto out_unlock;
	}

	/* Nuke the page table entry. */
	pte = vm_ptep_get_and_clear(vma, address, ptep);
	flush_tlb_page(vma, address);
	flush_cache_page(vma, address);

	/* Store the swap location in the pte. See handle_pte_fault() ... */
	if (PageSwapCache(page)) {
		swp_entry_t entry = { .val = page->index };
		swap_duplicate(entry);
		vm_set_pte(vma, address, ptep, swp_entry_to_pte(entry));
	}

	/* Move the dirty bit to the physical page now the pte is gone. */
	if (pte_dirty(pte))
		set_page_dirty(page);

	mm->rss--;
	page_cache_release(page);
	ret = SWAP_SUCCESS;

out_unlock:
	rmap_ptep_unmap(ptep);
	spin_unlock(&mm->page_table_lock);
	return ret;
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.  Caller must hold the page lock
 * and its pte chain lock.  Return values are:
 *
 * SWAP_SUCCESS	- we succeeded in removing all mappings
 * SWAP_AGAIN	- we missed a trylock, try again later
 * SWAP_FAIL	- the page is unswappable
 * SWAP_ERROR	- an error occurred
 */
int try_to_unmap(struct page * page)
{
	struct pte_chain *pc, *next_pc, *start;
	int ret = SWAP_SUCCESS;
	int victim_i = -1;

	/* This page should not be on the pageout lists. */
	if (PageReserved(page))
		BUG();
	if (!PageLocked(page))
		BUG();
	/* We need backing store to swap out a page. */
	if (!page->mapping)
		BUG();

	if (PageDirect(page)) {
		ret = try_to_unmap_one(page, page->pte.direct);
		if (ret == SWAP_SUCCESS) {
			page->pte.direct = 0;
			ClearPageDirect(page);
		}
		goto out;
	}		

	start = page->pte.chain;
	for (pc = start; pc; pc = next_pc) {
		int i;

		next_pc = pte_chain_next(pc);
		if (next_pc)
			prefetch(next_pc);
		for (i = pte_chain_idx(pc); i < NRPTE; i++) {
			chain_ptep_t pte_paddr = pc->ptes[i];

			if (!pte_paddr)
				continue;
			if (victim_i == -1) 
				victim_i = i;

			switch (try_to_unmap_one(page,
				PTE_ADDR_C2D(pte_paddr))) {
			case SWAP_SUCCESS:
				/*
				 * Release a slot.  If we're releasing the
				 * first pte in the first pte_chain then
				 * pc->ptes[i] and start->ptes[victim_i] both
				 * refer to the same thing.  It works out.
				 */
				pc->ptes[i] = start->ptes[victim_i];
				start->ptes[victim_i] = 0;
				victim_i++;
				if (victim_i == NRPTE) {
					page->pte.chain = pte_chain_next(start);
					__pte_chain_free(start);
					start = page->pte.chain;
					victim_i = 0;
				} else {
					start->next_and_idx++;
				}
				break;
			case SWAP_AGAIN:
				/* Skip this pte, remembering status. */
				ret = SWAP_AGAIN;
				continue;
			case SWAP_FAIL:
				ret = SWAP_FAIL;
				goto out;
			case SWAP_ERROR:
				ret = SWAP_ERROR;
				goto out;
			}
		}
	}
out:
	return ret;
}

/**
 ** No more VM stuff below this comment, only pte_chain helper
 ** functions.
 **/

static void pte_chain_ctor(void *p, kmem_cache_t *cachep, unsigned long flags)
{
	struct pte_chain *pc = p;

	memset(pc, 0, sizeof(*pc));
}

void __init pte_chain_init(void)
{
	pte_chain_cache = kmem_cache_create(	"pte_chain",
						sizeof(struct pte_chain),
						0,
						SLAB_MUST_HWCACHE_ALIGN,
						pte_chain_ctor,
						NULL);

	if (!pte_chain_cache)
		panic("failed to create pte_chain cache!\n");
}
