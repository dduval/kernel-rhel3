#ifndef _LINUX_MM_H
#define _LINUX_MM_H
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

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/mmzone.h>
#include <linux/swap.h>
#include <linux/rbtree.h>

extern unsigned long max_mapnr;
extern unsigned long num_physpages;
extern unsigned long num_mappedpages;
extern void * high_memory;
extern int page_cluster;

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/atomic.h>

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* The address space we belong to. */
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
	unsigned long vm_flags;		/* Flags, listed below. */

	rb_node_t vm_rb;

	/*
	 * For areas with an address space and backing store,
	 * one of the address_space->i_mmap{,shared} lists,
	 * for shm areas, the list of attaches, otherwise unused.
	 */
	struct vm_area_struct *vm_next_share;
	struct vm_area_struct **vm_pprev_share;

	/* Function pointers to deal with this struct. */
	struct vm_operations_struct * vm_ops;

	/* Information about our backing store: */
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units, *not* PAGE_CACHE_SIZE */
	struct file * vm_file;		/* File we map to (can be NULL). */
	unsigned long vm_raend;		/* XXX: put full readahead info here. */
	void * vm_private_data;		/* was vm_pte (shared mem) */
};

/*
 * vm_flags..
 */
#define VM_READ		0x00000001	/* currently active flags */
#define VM_WRITE	0x00000002
#define VM_EXEC		0x00000004
#define VM_SHARED	0x00000008

#define VM_MAYREAD	0x00000010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x00000020
#define VM_MAYEXEC	0x00000040
#define VM_MAYSHARE	0x00000080

#define VM_GROWSDOWN	0x00000100	/* general info on the segment */
#define VM_GROWSUP	0x00000200
#define VM_SHM		0x00000400	/* shared memory area, don't swap out */
#define VM_DENYWRITE	0x00000800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x00001000
#define VM_LOCKED	0x00002000
#define VM_IO           0x00004000	/* Memory mapped I/O or similar */

					/* Used by sys_madvise() */
#define VM_SEQ_READ	0x00008000	/* App will access data sequentially */
#define VM_RAND_READ	0x00010000	/* App will not benefit from clustered reads */

#define VM_DONTCOPY	0x00020000      /* Do not copy this vma on fork */
#define VM_DONTEXPAND	0x00040000	/* Cannot expand with mremap() */
#define VM_RESERVED	0x00080000	/* Don't unmap it from swap_out */

#define VM_ACCOUNT	0x00100000	/* Memory is a vm accounted object */
#define VM_HUGETLB	0x00400000	/* Huge TLB Page VM */
#define VM_NO_UNLOCK	0x00800000	/* do not unlock */

/* arches may define VM_STACK_FLAGS for their own purposes */
#ifndef VM_STACK_FLAGS
#ifdef ARCH_STACK_GROWSUP
#define VM_STACK_FLAGS	(VM_DATA_DEFAULT_FLAGS|VM_GROWSUP|VM_ACCOUNT)
#else
#define VM_STACK_FLAGS	(VM_DATA_DEFAULT_FLAGS|VM_GROWSDOWN|VM_ACCOUNT)
#endif
#endif

#define VM_READHINTMASK			(VM_SEQ_READ | VM_RAND_READ)
#define VM_ClearReadHint(v)		(v)->vm_flags &= ~VM_READHINTMASK
#define VM_NormalReadHint(v)		(!((v)->vm_flags & VM_READHINTMASK))
#define VM_SequentialReadHint(v)	((v)->vm_flags & VM_SEQ_READ)
#define VM_RandomReadHint(v)		((v)->vm_flags & VM_RAND_READ)

/* read ahead limits */
extern int vm_min_readahead;
extern int vm_max_readahead;

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];


/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	struct page * (*nopage)(struct vm_area_struct * area, unsigned long address, int unused);
	int (*populate)(struct vm_area_struct * area, unsigned long address, unsigned long len, pgprot_t prot, unsigned long pgoff, int nonblock);
	int (*do_no_page)(struct mm_struct * mm, struct vm_area_struct * vma,
			  unsigned long address, int write_access, 
			  pte_t *page_table, pmd_t *pmd);
};

/* forward declaration; pte_chain is meant to be internal to rmap.c */
struct pte_chain;

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page.
 *
 * Try to keep the most commonly accessed fields in single cache lines
 * here (16 bytes or greater).  This ordering should be particularly
 * beneficial on 32-bit processors.
 *
 * The first line is data used in page cache lookup, the second line
 * is used for linear searches (eg. clock algorithm scans). 
 *
 * TODO: make this structure smaller, it could be as small as 32 bytes.
 */
typedef struct page {
	struct list_head list;		/* ->mapping has some page lists. */
	struct address_space *mapping;	/* The inode (or ...) we belong to.
					 * protected by PG_locked and the
					 * pagecache_lock. Hold one to read,
					 * both to write.
					 */
	unsigned long index;		/* Our offset within mapping. */
	struct page *next_hash;		/* Next page sharing our hash bucket in
					   the pagecache hash table. */
	atomic_t count;			/* Usage count, see below. */
	unsigned long flags;		/* atomic flags, some possibly
					   updated asynchronously */
	struct list_head lru;		/* Pageout list, eg. active_list;
					   protected by the lru lock !! */
	union {
		struct pte_chain *chain;/* Reverse pte mapping pointer.
					 * protected by PG_chainlock */
		pte_addr_t direct;
	} pte;
	unsigned char age;		/* Page aging counter. */
	struct page **pprev_hash;	/* Complement to *next_hash. */
	struct buffer_head * buffers;	/* Buffer maps us to a disk block. */

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* CONFIG_HIGMEM || WANT_PAGE_VIRTUAL */
} mem_map_t;

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for special pages, which can never be swapped
 * out. Some of them might not even exist (eg empty_bad_page)...
 *
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * For the non-reserved pages, page->count denotes a reference count.
 *   page->count == 0 means the page is free.
 *   page->count == 1 means the page is used for exactly one purpose
 *   (e.g. a private data page of one process).
 *
 * A page may be used for kmalloc() or anyone else who does a
 * __get_free_page(). In this case the page->count is at least 1, and
 * all other fields are unused but should be 0 or NULL. The
 * management of this page is the responsibility of the one who uses
 * it.
 *
 * The other pages (we may call them "process pages") are completely
 * managed by the Linux memory manager: I/O, buffers, swapping etc.
 * The following discussion applies only to them.
 *
 * A page may belong to an inode's memory mapping. In this case,
 * page->mapping is the pointer to the inode, and page->index is the
 * file offset of the page, in units of PAGE_CACHE_SIZE.
 *
 * A page may have buffers allocated to it. In this case,
 * page->buffers is a circular list of these buffer heads. Else,
 * page->buffers == NULL.
 *
 * For pages belonging to inodes, the page->count is the number of
 * attaches, plus 1 if buffers are allocated to the page, plus one
 * for the page cache itself.
 *
 * All pages belonging to an inode are in these doubly linked lists:
 * mapping->clean_pages, mapping->dirty_pages and mapping->locked_pages;
 * using the page->list list_head. These fields are also used for
 * freelist managemet (when page->count==0).
 *
 * There is also a hash table mapping (mapping,index) to the page
 * in memory if present. The lists for this hash table use the fields
 * page->next_hash and page->pprev_hash.
 *
 * All process pages can do I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written to disk,
 * - private pages which have been modified may need to be swapped out
 *   to swap space and (later) to be read back into memory.
 * During disk I/O, PG_locked is used. This bit is set before I/O
 * and reset when I/O completes. page_waitqueue(page) is a wait queue of all
 * tasks waiting for the I/O on this page to complete.
 * PG_uptodate tells whether the page's contents is valid.
 * When a read completes, the page becomes uptodate, unless a disk I/O
 * error happened.
 *
 * For choosing which pages to swap out, inode pages carry a
 * PG_referenced bit, which is set any time the system accesses
 * that page through the (mapping,index) hash table. This referenced
 * bit, together with the referenced bit in the page tables, is used
 * to manipulate page->age and move the page across the active,
 * inactive_dirty and inactive_clean lists.
 *
 * Note that the referenced bit, the page->lru list_head and the
 * active, inactive_dirty and inactive_clean lists are protected by
 * the lru lock, and *NOT* by the usual PG_locked bit!
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 *
 * PG_arch_1 is an architecture specific page state bit.  The generic
 * code guarantees that this bit is cleared for a page when it first
 * is entered into the page cache.
 *
 * PG_highmem pages are not permanently mapped into the kernel virtual
 * address space, they need to be kmapped separately for doing IO on
 * the pages. The struct page (these bits with information) are always
 * mapped into kernel address space...
 */
#define PG_locked		 0	/* Page is locked. Don't touch. */
#define PG_error		 1
#define PG_referenced		 2
#define PG_uptodate		 3
#define PG_dirty		 4
#define PG_active_anon		 5
#define PG_direct		 6
#define PG_inactive_dirty	 7
#define PG_inactive_laundry	 8
#define PG_inactive_clean	 9
#define PG_slab			10
#define PG_invalidated		11	/* Page invalidated by direct IO */
#define PG_highmem		12
#define PG_checked		13	/* kill me in 2.5.<early>. */
#define PG_arch_1		14
#define PG_reserved		15
#define PG_launder		16	/* written out by VM pressure.. */
#define PG_chainlock		17	/* lock bit for ->pte_chain */
#define PG_lru			18
#define PG_active_cache		19
#define PG_sync			20
#define PG_fresh_page		21	/* Page freshly read from disk */
#define PG_compound		22	/* Part of a compound page */
#define PG_wired		23	/* wired by ramfs */

/* note: don't make page flags of values 24 or higher! */

/* Make it prettier to test the above... */
#define UnlockPage(page)	unlock_page(page)
#define Page_Uptodate(page)	test_bit(PG_uptodate, &(page)->flags)
#ifndef SetPageUptodate
#define SetPageUptodate(page)	set_bit(PG_uptodate, &(page)->flags)
#endif
#define ClearPageUptodate(page)	clear_bit(PG_uptodate, &(page)->flags)
#define PageDirty(page)		test_bit(PG_dirty, &(page)->flags)
#define SetPageDirty(page)	set_bit(PG_dirty, &(page)->flags)
#define ClearPageDirty(page)	clear_bit(PG_dirty, &(page)->flags)
#define PageLocked(page)	test_bit(PG_locked, &(page)->flags)
#define LockPage(page)		set_bit(PG_locked, &(page)->flags)
#define TryLockPage(page)	test_and_set_bit(PG_locked, &(page)->flags)
#define PageChecked(page)	test_bit(PG_checked, &(page)->flags)
#define SetPageChecked(page)	set_bit(PG_checked, &(page)->flags)
#define ClearPageChecked(page)	clear_bit(PG_checked, &(page)->flags)
#define PageLaunder(page)	test_bit(PG_launder, &(page)->flags)
#define SetPageLaunder(page)	set_bit(PG_launder, &(page)->flags)
#define ClearPageLaunder(page)	clear_bit(PG_launder, &(page)->flags)
#define ClearPageReferenced(page) clear_bit(PG_referenced, &(page)->flags)
#define ClearPageError(page)    clear_bit(PG_error, &(page)->flags)
#define ClearPageArch1(page)    clear_bit(PG_arch_1, &(page)->flags)
#define SetPageWired(page)	set_bit(PG_wired, &(page)->flags)
#define ClearPageWired(page)	clear_bit(PG_wired, &(page)->flags)
#define PageWired(page)		test_bit(PG_wired, &(page)->flags)

/*
 * inlines for acquisition and release of PG_chainlock
 */
static inline void pte_chain_lock(struct page *page)
{
	/*
	 * The preempt patch seems to be popular enough to
	 * warrant this little hack...
	 */
#ifdef CONFIG_PREEMPT
	preempt_disable();
#endif
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
#ifdef CONFIG_SMP
	while (test_and_set_bit(PG_chainlock, &page->flags)) {
		while (test_bit(PG_chainlock, &page->flags)) {
			barrier();
			cpu_relax();
		}
	}
#endif
}

static inline void pte_chain_unlock(struct page *page)
{
#ifdef CONFIG_SMP
	smp_mb__before_clear_bit();
	clear_bit(PG_chainlock, &page->flags);
#endif
	/*
	 * The preempt patch seems to be popular enough to
	 * warrant this little hack...
	 */
#ifdef CONFIG_PREEMPT
	preempt_enable();
#endif
}

/*
 * The zone field is never updated after free_area_init_core()
 * sets it, so none of the operations on it need to be atomic.
 */
#define NODE_SHIFT 4
#define ZONE_SHIFT (BITS_PER_LONG - 8)

struct zone_struct;
extern struct zone_struct *zone_table[];

static inline zone_t *page_zone(struct page *page)
{
	return zone_table[page->flags >> ZONE_SHIFT];
}

static inline void set_page_zone(struct page *page, unsigned long zone_num)
{
	page->flags &= ~(~0UL << ZONE_SHIFT);
	page->flags |= zone_num << ZONE_SHIFT;
}

/*
 * In order to avoid #ifdefs within C code itself, we define
 * set_page_address to a noop for non-highmem machines, where
 * the field isn't useful.
 * The same is true for page_address() in arch-dependent code.
 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)

#define set_page_address(page, address)			\
	do {						\
		(page)->virtual = (address);		\
	} while(0)

#else /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */
#define set_page_address(page, address)  do { } while(0)
#endif /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

/*
 * Permanent address of a page. Obviously must never be
 * called on a highmem page.
 */
#if defined(CONFIG_HIGHMEM) || defined(WANT_PAGE_VIRTUAL)

#define page_address(page) ((page)->virtual)

#else /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

#define page_address(page)						\
	__va( (((page) - page_zone(page)->zone_mem_map) << PAGE_SHIFT)	\
			+ page_zone(page)->zone_start_paddr)

#endif /* CONFIG_HIGHMEM || WANT_PAGE_VIRTUAL */

extern void FASTCALL(set_page_dirty(struct page *));

/*
 * The first mb is necessary to safely close the critical section opened by the
 * TryLockPage(), the second mb is necessary to enforce ordering between
 * the clear_bit and the read of the waitqueue (to avoid SMP races with a
 * parallel wait_on_page).
 */
#define PageDirect(page)	test_bit(PG_direct, &(page)->flags)
#define SetPageDirect(page)	set_bit(PG_direct, &(page)->flags)
#define ClearPageDirect(page)	clear_bit(PG_direct, &(page)->flags)
#define PageError(page)		test_bit(PG_error, &(page)->flags)
#define SetPageError(page)	set_bit(PG_error, &(page)->flags)
#define ClearPageError(page)	clear_bit(PG_error, &(page)->flags)
#define PageReferenced(page)	test_bit(PG_referenced, &(page)->flags)
#define SetPageReferenced(page)	set_bit(PG_referenced, &(page)->flags)
#define ClearPageReferenced(page)	clear_bit(PG_referenced, &(page)->flags)
#define PageTestandClearReferenced(page)	test_and_clear_bit(PG_referenced, &(page)->flags)
#define PageSlab(page)		test_bit(PG_slab, &(page)->flags)
#define PageSetSlab(page)	set_bit(PG_slab, &(page)->flags)
#define PageClearSlab(page)	clear_bit(PG_slab, &(page)->flags)
#define PageReserved(page)	test_bit(PG_reserved, &(page)->flags)

#define PageActiveAnon(page)		test_bit(PG_active_anon, &(page)->flags)
#define SetPageActiveAnon(page)	set_bit(PG_active_anon, &(page)->flags)
#define ClearPageActiveAnon(page)	clear_bit(PG_active_anon, &(page)->flags)
#define TestandSetPageActiveAnon(page)	test_and_set_bit(PG_active_anon, &(page)->flags)
#define TestandClearPageActiveAnon(page)	test_and_clear_bit(PG_active_anon, &(page)->flags)

#define PageActiveCache(page)		test_bit(PG_active_cache, &(page)->flags)
#define SetPageActiveCache(page)	set_bit(PG_active_cache, &(page)->flags)
#define ClearPageActiveCache(page)	clear_bit(PG_active_cache, &(page)->flags)
#define TestandSetPageActiveCache(page)	test_and_set_bit(PG_active_cache, &(page)->flags)
#define TestandClearPageActiveCache(page)	test_and_clear_bit(PG_active_cache, &(page)->flags)

#define PageInactiveLaundry(page)	test_bit(PG_inactive_laundry, &(page)->flags)
#define SetPageInactiveLaundry(page)	set_bit(PG_inactive_laundry, &(page)->flags)
#define ClearPageInactiveLaundry(page)	clear_bit(PG_inactive_laundry, &(page)->flags)

#define PageInactiveDirty(page)	test_bit(PG_inactive_dirty, &(page)->flags)
#define SetPageInactiveDirty(page)	set_bit(PG_inactive_dirty, &(page)->flags)
#define ClearPageInactiveDirty(page)	clear_bit(PG_inactive_dirty, &(page)->flags)

#define PageInactiveClean(page)	test_bit(PG_inactive_clean, &(page)->flags)
#define SetPageInactiveClean(page)	set_bit(PG_inactive_clean, &(page)->flags)
#define ClearPageInactiveClean(page)	clear_bit(PG_inactive_clean, &(page)->flags)

#define PageLRU(page)	 	test_bit(PG_lru, &(page)->flags)
#define SetPageLRU(page)	set_bit(PG_lru, &(page)->flags)
#define ClearPageLRU(page)	clear_bit(PG_lru, &(page)->flags)
#define TestandSetPageLRU(page)	test_and_set_bit(PG_lru, &(page)->flags)

#define PageFresh(page)		test_bit(PG_fresh_page, &(page)->flags)
#define SetPageFresh(page)	set_bit(PG_fresh_page, &(page)->flags)
#define ClearPageFresh(page)	clear_bit(PG_fresh_page, &(page)->flags)

#define SetPageSync(page)	set_bit(PG_sync, &(page)->flags)
#define ClearPageSync(page)	clear_bit(PG_sync, &(page)->flags)
#define TestClearPageSync(page)	test_and_clear_bit(PG_sync, &(page)->flags)

#define SetPageInvalidated(page) set_bit(PG_invalidated, &(page)->flags)
#define ClearPageInvalidated(page) clear_bit(PG_invalidated, &(page)->flags)
#define PageInvalidated(page)   test_bit(PG_invalidated, &(page)->flags)

#ifdef CONFIG_HIGHMEM
#define PageHighMem(page)		test_bit(PG_highmem, &(page)->flags)
#else
#define PageHighMem(page)		0 /* needed to optimize away at compile time */
#endif

#define SetPageReserved(page)		set_bit(PG_reserved, &(page)->flags)
#define ClearPageReserved(page)		clear_bit(PG_reserved, &(page)->flags)

#define PageCompound(page)		test_bit(PG_compound, &(page)->flags)
#define SetPageCompound(page)		set_bit(PG_compound, &(page)->flags)
#define ClearPageCompound(page)		clear_bit(PG_compound, &(page)->flags)

/*
 * There is only one 'core' page-freeing function.
 */
extern void FASTCALL(__free_pages(struct page *page, unsigned int order));
extern void FASTCALL(free_pages_ok(struct page *page, unsigned int order));
extern void FASTCALL(free_pages(unsigned long addr, unsigned int order));

#define __free_page(page) put_page(page)
#define free_page(addr) free_pages((addr),0)

/*
 * Methods to modify the page usage count.
 *
 * What counts for a page usage:
 * - cache mapping   (page->mapping)
 * - disk mapping    (page->buffers)
 * - page mapped in a task's page tables, each mapping
 *   is counted separately
 *
 * Also, many kernel routines increase the page count before a critical
 * routine so they can be sure the page doesn't go away from under them.
 */

extern void show_stack(unsigned long * esp);
struct page;

#ifdef CONFIG_HUGETLB_PAGE


#define get_page(p)							\
do {									\
	struct page *___page = (struct page *)(p);			\
	if (unlikely(PageCompound(___page))) {				\
		___page = (struct page *)___page->lru.next;		\
		BUG_ON(!PageCompound(___page));				\
	}								\
	atomic_inc(&___page->count);					\
} while (0)


#else		/* CONFIG_HUGETLB_PAGE */

#define get_page(p)							\
do {									\
	struct page *___page = (struct page *)(p);			\
	atomic_inc(&___page->count);					\
} while (0)

#endif		/* CONFIG_HUGETLB_PAGE */

#define put_page(p)							\
do {									\
	struct page *___page = (struct page *)(p);			\
 \
 \
 \
 \
	__free_pages(___page, 0);						\
} while (0)

/*
 * Return true if this page is mapped into pagetables.  Subtle: test pte.direct
 * rather than pte.chain.  Because sometimes pte.direct is 64-bit, and .chain
 * is only 32-bit.
 */
static inline int page_mapped(struct page *page)
{
	return page->pte.direct != 0;
}

/*
 * Error return values for the *_nopage functions
 */
#define NOPAGE_SIGBUS	(NULL)
#define NOPAGE_OOM	((struct page *) (-1))

/* The array of struct pages */
extern mem_map_t * mem_map;

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */
extern struct page * FASTCALL(_alloc_pages(unsigned int gfp_mask, unsigned int order));
extern struct page * FASTCALL(__alloc_pages(unsigned int gfp_mask, unsigned int order, zonelist_t *zonelist));
extern struct page * alloc_pages_node(int nid, unsigned int gfp_mask, unsigned int order);

static inline struct page * alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	/*
	 * Gets optimized away by the compiler.
	 */
	if (order >= MAX_ORDER)
		return NULL;
	return _alloc_pages(gfp_mask, order);
}

#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)

extern unsigned long FASTCALL(__get_free_pages(unsigned int gfp_mask, unsigned int order));
extern unsigned long FASTCALL(get_zeroed_page(unsigned int gfp_mask));

#define __get_free_page(gfp_mask) \
		__get_free_pages((gfp_mask),0)

#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA,(order))

/*
 * The old interface name will be removed in 2.5:
 */
#define get_free_page get_zeroed_page

extern void FASTCALL(fixup_freespace(struct zone_struct *, int));
extern void show_free_areas(void);
extern void show_free_areas_node(pg_data_t *pgdat);

extern void clear_page_tables(struct mm_struct *, unsigned long, int);

extern int fail_writepage(struct page *);
struct page * shmem_nopage(struct vm_area_struct * vma, unsigned long address, int unused);
struct file *shmem_file_setup(char * name, loff_t size);
extern int shmem_lock(struct file *, int lock, struct mm_struct **, pid_t *);
extern int shmem_zero_setup(struct vm_area_struct *);

extern void zap_page_range(struct vm_area_struct *vma, unsigned long address, unsigned long size);
extern int copy_page_range(struct mm_struct *dst, struct mm_struct *src, struct vm_area_struct *vma);
extern int remap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long to, unsigned long size, pgprot_t prot);
extern int zeromap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long size, pgprot_t prot);

extern int vmtruncate(struct inode * inode, loff_t offset);
extern pmd_t *FASTCALL(__pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_kernel(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern pte_t *FASTCALL(pte_alloc_map(struct mm_struct *mm, pmd_t *pmd, unsigned long address));
extern int install_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, struct page *page, pgprot_t prot);
extern long sys_remap_file_pages(unsigned long start, unsigned long size, unsigned long prot, unsigned long pgoff, unsigned long nonblock);
extern int handle_mm_fault(struct mm_struct *mm,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);
extern int access_process_vm(struct task_struct *tsk, unsigned long addr, void *buf, int len, int write);

extern struct page * follow_page(struct mm_struct *mm, unsigned long address, int write);
extern struct page * follow_pin_page(struct mm_struct *mm, unsigned long address, int write);
extern int do_no_page(struct mm_struct * mm, struct vm_area_struct * vma,
		      unsigned long address, int write_access, 
		      pte_t *page_table, pmd_t *pmd);

int get_user_pages(struct task_struct *tsk, struct mm_struct *mm, unsigned long start,
		int len, int write, int force, struct page **pages, struct vm_area_struct **vmas);

/*
 * On a two-level page table, this ends up being trivial. Thus the
 * inlining and the symmetry break with pte_alloc() that does all
 * of this out-of-line.
 */
static inline pmd_t *pmd_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address)
{
	if (pgd_none(*pgd))
		return __pmd_alloc(mm, pgd, address);
	return pmd_offset(pgd, address);
}

extern int pgt_cache_water[2];
extern int check_pgt_cache(void);

extern void free_area_init(unsigned long * zones_size);
extern void free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long * zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size);
extern void mem_init(void);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);
extern void swapin_readahead(swp_entry_t);

extern struct address_space swapper_space;
#define PageSwapCache(page) ((page)->mapping == &swapper_space)

#define is_page_cache_freeable(page) (page_count(page) - !!page->buffers == 1)

extern int can_share_swap_page(struct page *);
extern int remove_exclusive_swap_page(struct page *);

/* vm pte ops for accounting. */
extern void FASTCALL(vm_ptep_set_wrprotect(struct mm_struct *mm, pte_t *ptep));
extern void FASTCALL(vm_set_pte(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep, pte_t pte));
extern pte_t FASTCALL(vm_ptep_get_and_clear(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep));
extern void vm_pte_clear(struct vm_area_struct *vma, unsigned long address, pte_t *ptep);
extern void FASTCALL(__free_pte(pte_t pte));
extern void vm_account(struct vm_area_struct *, pte_t, unsigned long, long);


/* mmap.c */
extern void lock_vma_mappings(struct vm_area_struct *);
extern void unlock_vma_mappings(struct vm_area_struct *);
extern int insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void __vma_link_rb(struct mm_struct *, struct vm_area_struct *,
	rb_node_t **, rb_node_t *);
extern void exit_mmap(struct mm_struct *);

extern unsigned long get_unmapped_area(struct file *, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);

extern unsigned long do_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff);

static inline unsigned long do_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	unsigned long ret = -EINVAL;
	if ((offset + PAGE_ALIGN(len)) < offset)
		goto out;
	if (!(offset & ~PAGE_MASK))
		ret = do_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
out:
	return ret;
}

extern int do_munmap(struct mm_struct *, unsigned long, size_t, int acct);

extern unsigned long do_brk(unsigned long, unsigned long);
extern unsigned long do_brk_locked(unsigned long, unsigned long);

#ifdef __i386__
extern void arch_remove_exec_range(struct mm_struct *mm, unsigned long limit);
#else
#define arch_remove_exec_range(mm, limit)  do { } while (0)
#endif


static inline void __vma_unlink(struct mm_struct * mm, struct vm_area_struct * vma, struct vm_area_struct * prev)
{
	prev->vm_next = vma->vm_next;
	rb_erase(&vma->vm_rb, &mm->mm_rb);
	if (mm->mmap_cache == vma)
		mm->mmap_cache = prev;
	arch_remove_exec_range(mm, vma->vm_end);
}

#define VM_SPECIAL (VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED)
#define can_vma_merge(vma, vm_flags) __can_vma_merge(vma, vm_flags, NULL, 0, 0)

/*
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap_pgoff() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index 0xffffffff.
 *
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those.
 */
static inline int __can_vma_merge(struct vm_area_struct * vma, unsigned long vm_flags,
				  struct file * file, unsigned long vm_pgoff, unsigned long offset)
{
	if (unlikely(vma->vm_file != file))
		return 0;
	if (unlikely(vma->vm_flags != vm_flags))
		return 0;
	if (unlikely(vma->vm_flags & VM_SPECIAL))
		return 0;
	if (unlikely(vma->vm_private_data != NULL))
		return 0;
	if (unlikely(vma->vm_ops && vma->vm_ops->close))
		return 0;
	if (file) {
		if (unlikely(vma->vm_pgoff != vm_pgoff + offset))
			return 0;
	}
	return 1;
}

/* mlock can just return an instant EPERM if the caller has no
   permission to do any memory locking. */
static inline int can_do_mlock(void)
{
	if (current->rlim[RLIMIT_MEMLOCK].rlim_cur != 0)
		return 1;
	if (capable(CAP_IPC_LOCK))
		return 1;
	return 0;
}

struct zone_t;
/* filemap.c */
extern void remove_inode_page(struct page *);
extern unsigned long page_unuse(struct page *);
extern void truncate_inode_pages(struct address_space *, loff_t);

/* generic vm_area_ops exported for stackable file systems */
extern int filemap_sync(struct vm_area_struct *, unsigned long,	size_t, unsigned int);
extern struct page *filemap_nopage(struct vm_area_struct *, unsigned long, int);

/*
 * GFP bitmasks..
 */
/* Zone modifiers in GFP_ZONEMASK (see linux/mmzone.h - low four bits) */
#define __GFP_DMA	0x01
#define __GFP_HIGHMEM	0x02

/* Action modifiers - doesn't change the zoning */
#define __GFP_WAIT	0x10	/* Can wait and reschedule? */
#define __GFP_HIGH	0x20	/* Should access emergency pools? */
#define __GFP_IO	0x40	/* Can start low memory physical IO? */
#define __GFP_HIGHIO	0x80	/* Can start high mem physical IO? */
#define __GFP_FS	0x100	/* Can call down to low-level FS? */
#define __GFP_WIRED	0x200   /* Highmem bias and wired */
#define __GFP_NUMA	0x400	/* NUMA allocation */

#define __GFP_BITS_SHIFT 16	/* Room for 16 __GFP_FOO bits */
#define __GFP_BITS_MASK ((1 << __GFP_BITS_SHIFT) - 1)

#define GFP_NOHIGHIO	(__GFP_HIGH | __GFP_WAIT | __GFP_IO)
#define GFP_NOIO	(__GFP_HIGH | __GFP_WAIT)
#define GFP_NOFS	(__GFP_HIGH | __GFP_WAIT | __GFP_IO | __GFP_HIGHIO)
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_USER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_HIGHUSER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS | __GFP_HIGHMEM)
#define GFP_KERNEL	(__GFP_HIGH | __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_NFS		(__GFP_HIGH | __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_KSWAPD	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA

static inline unsigned int pf_gfp_mask(unsigned int gfp_mask)
{
	/* avoid all memory balancing I/O methods if this task cannot block on I/O */
	if (current->flags & PF_NOIO)
		gfp_mask &= ~(__GFP_IO | __GFP_HIGHIO | __GFP_FS);

	return gfp_mask;
}

/* Do stack extension */	
extern  int expand_stack(struct vm_area_struct * vma, unsigned long address);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * FASTCALL(find_vma(struct mm_struct * mm, unsigned long addr));
extern struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
					     struct vm_area_struct **pprev);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

extern struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr);

extern struct page * vmalloc_to_page(void *addr);

/* Page pinning for direct IO.  Copy-on-write effects mean it's unsafe
 * for the VM to unmap a pte if the process has requested direct IO to
 * that page.  So we keep a count of outstanding direct IOs per page.
 *
 * We actually maintain this as a hash into an atomic_t[] array, so
 * depending on the hash size there may be a certain amount of false
 * sharing of page pins. */
extern atomic_t *page_pin_array;
extern unsigned int page_pin_mask;
extern void page_pin_init(unsigned long);
static inline unsigned int page_pin_hash(struct page *p) 
{
	unsigned int addr = (unsigned int)(unsigned long)p;
	/* gcc usually optimises integer divide-by-constant pretty well. */
	addr /= sizeof(struct page);  
	addr ^= (addr >> 8);
	return (addr & page_pin_mask);
}
#define page_pin_counter(p) (page_pin_array + page_pin_hash(p))
#define pin_page_mappings(p) \
	do {atomic_inc(page_pin_counter(p));} while (0)
#define unpin_page_mappings(p) \
	do {atomic_dec(page_pin_counter(p));} while (0)
#define page_mapping_pinned(p) \
	(atomic_read(page_pin_counter(p)) != 0)

#ifdef CONFIG_IA64
int in_gate_area(struct task_struct *task, unsigned long addr);
struct vm_area_struct *get_gate_vma(struct task_struct *tsk);
#endif /*CONFIG_IA64*/
#endif /* __KERNEL__ */

#endif
