/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
 */

/*
 * This file handles the generic file mmap semantics used by
 * most "normal" filesystems (but you don't /have/ to use this:
 * the NFS filesystem used to do this differently, for example)
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/locks.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/swapctl.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/iobuf.h>
#include <linux/bootmem.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/mman.h>

#include <linux/highmem.h>
#include <linux/worktodo.h>
#include <linux/iobuf.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 *
 * Shared mappings now work. 15.8.1995  Bruno.
 *
 * finished 'unifying' the page and buffer cache and SMP-threaded the
 * page-cache, 21.05.1999, Ingo Molnar <mingo@redhat.com>
 *
 * SMP-threaded pagemap-LRU 1999, Andrea Arcangeli <andrea@suse.de>
 */

atomic_t page_cache_size = ATOMIC_INIT(0);
unsigned int page_hash_bits;
struct page **page_hash_table;

int vm_max_readahead = 31;
int vm_min_readahead = 3;
EXPORT_SYMBOL(vm_max_readahead);
EXPORT_SYMBOL(vm_min_readahead);


spinlock_cacheline_t pagecache_lock_cacheline  = {SPIN_LOCK_UNLOCKED};
/*
 * NOTE: to avoid deadlocking you must never acquire the lru lock 
 *	with the pagecache_lock held.
 *
 * Ordering:
 *	swap_lock ->
 *		   lru lock ->
 *			pagecache_lock
 */

#define CLUSTER_PAGES		(1 << page_cluster)
#define CLUSTER_OFFSET(x)	(((x) >> page_cluster) << page_cluster)

static void FASTCALL(add_page_to_hash_queue(struct page * page, struct page **p));
static void add_page_to_hash_queue(struct page * page, struct page **p)
{
	struct page *next = *p;

	*p = page;
	page->next_hash = next;
	page->pprev_hash = p;
	if (next)
		next->pprev_hash = &page->next_hash;
	if (page->buffers)
		PAGE_BUG(page);
	atomic_inc(&page_cache_size);
}

static inline void add_page_to_inode_queue(struct address_space *mapping, struct page * page)
{
	struct list_head *head = &mapping->clean_pages;

	mapping->nrpages++;
	list_add(&page->list, head);
	page->mapping = mapping;
}

static inline void remove_page_from_inode_queue(struct page * page)
{
	struct address_space * mapping = page->mapping;

	if (mapping->a_ops->removepage)
		mapping->a_ops->removepage(page);
	
	list_del(&page->list);
	page->mapping = NULL;
	wmb();
	mapping->nrpages--;
	if (!mapping->nrpages) 
		refile_inode(mapping->host);
}

static inline void remove_page_from_hash_queue(struct page * page)
{
	struct page *next = page->next_hash;
	struct page **pprev = page->pprev_hash;

	if (next)
		next->pprev_hash = pprev;
	*pprev = next;
	page->pprev_hash = NULL;
	atomic_dec(&page_cache_size);
}

/*
 * Remove a page from the page cache and free it. Caller has to make
 * sure the page is locked and that nobody else uses it - or that usage
 * is safe.
 */
void __remove_inode_page(struct page *page)
{
	remove_page_from_inode_queue(page);
	remove_page_from_hash_queue(page);
}

void remove_inode_page(struct page *page)
{
	if (!PageLocked(page))
		PAGE_BUG(page);

	lock_pagecache();
	__remove_inode_page(page);
	unlock_pagecache();
}

static inline int sync_page(struct page *page)
{
	struct address_space *mapping = page->mapping;

	if (mapping && mapping->a_ops && mapping->a_ops->sync_page)
		return mapping->a_ops->sync_page(page);
	return 0;
}

/*
 * Add a page to the dirty page list.
 */
void set_page_dirty(struct page *page)
{
	if (!test_and_set_bit(PG_dirty, &page->flags)) {
		struct address_space *mapping = page->mapping;

		if (mapping) {
			lock_pagecache();
			mapping = page->mapping;
			if (mapping) {	/* may have been truncated */
				list_del(&page->list);
				list_add(&page->list, &mapping->dirty_pages);
			}
			unlock_pagecache();

			if (mapping && mapping->host)
				mark_inode_dirty_pages(mapping->host);
		}
	}
}

/**
 * invalidate_inode_pages - Invalidate all the unlocked pages of one inode
 * @inode: the inode which pages we want to invalidate
 *
 * This function only removes the unlocked pages, if you want to
 * remove all the pages of one inode, you must call truncate_inode_pages.
 */

void invalidate_inode_pages(struct inode * inode)
{
	struct list_head *head, *curr;
	struct page * page;

	head = &inode->i_mapping->clean_pages;

	lru_lock(ALL_ZONES);
	lock_pagecache();
	curr = head->next;

	while (curr != head) {
		page = list_entry(curr, struct page, list);
		curr = curr->next;

		/* We cannot invalidate something in dirty.. */
		if (PageDirty(page))
			continue;

		/* ..or locked */
		if (TryLockPage(page))
			continue;

		if (page->buffers && !try_to_free_buffers(page, 0))
			goto unlock;

		if (page_count(page) != 1)
			goto unlock;

		/* Manual lru del to avoid lock ordering problems */
		__lru_cache_del(page);
		__remove_inode_page(page);
		UnlockPage(page);
		page_cache_release(page);
		continue;
unlock:
		UnlockPage(page);
		continue;
	}

	unlock_pagecache();
	lru_unlock(ALL_ZONES);
}

static int do_flushpage(struct page *page, unsigned long offset)
{
	int (*flushpage) (struct page *, unsigned long);
	flushpage = page->mapping->a_ops->flushpage;
	if (flushpage)
		return (*flushpage)(page, offset);
	return block_flushpage(page, offset);
}

static inline void truncate_partial_page(struct page *page, unsigned partial)
{
	memclear_highpage_flush(page, partial, PAGE_CACHE_SIZE-partial);
	if (page->buffers)
		do_flushpage(page, partial);
}

static void truncate_complete_page(struct page *page)
{
	/*
	 * Leave it on the LRU if it gets converted into anonymous buffers
	 * or anonymous process memory.
	 */
	if ((!page->buffers || do_flushpage(page, 0)) && !page->pte.direct)
		lru_cache_del(page);

	/*
	 * We remove the page from the page cache _after_ we have
	 * destroyed all buffer-cache references to it. Otherwise some
	 * other process might think this inode page is not in the
	 * page cache and creates a buffer-cache alias to it causing
	 * all sorts of fun problems ...  
	 */
	ClearPageDirty(page);
	ClearPageUptodate(page);
	remove_inode_page(page);
	page_cache_release(page);
}

static int FASTCALL(truncate_list_pages(struct list_head *, unsigned long, unsigned *));
static int truncate_list_pages(struct list_head *head, unsigned long start, unsigned *partial)
{
	struct list_head *curr;
	struct page * page;
	int unlocked = 0;

 restart:
	curr = head->prev;
	while (curr != head) {
		unsigned long offset;

		page = list_entry(curr, struct page, list);
		offset = page->index;

		/* Is one of the pages to truncate? */
		if ((offset >= start) || (*partial && (offset + 1) == start)) {
			int failed;

			page_cache_get(page);
			failed = TryLockPage(page);

			list_del(head);
			if (!failed)
				/* Restart after this page */
				list_add_tail(head, curr);
			else
				/* Restart on this page */
				list_add(head, curr);

			unlock_pagecache();
			unlocked = 1;

 			if (!failed) {
				if (*partial && (offset + 1) == start) {
					truncate_partial_page(page, *partial);
					*partial = 0;
				} else 
					truncate_complete_page(page);

				UnlockPage(page);
			} else
 				wait_on_page(page);

			page_cache_release(page);

			if (current->need_resched) {
				__set_current_state(TASK_RUNNING);
				schedule();
			}

			lock_pagecache();
			goto restart;
		}
		curr = curr->prev;
	}
	return unlocked;
}


/**
 * truncate_inode_pages - truncate *all* the pages from an offset
 * @mapping: mapping to truncate
 * @lstart: offset from with to truncate
 *
 * Truncate the page cache at a set offset, removing the pages
 * that are beyond that offset (and zeroing out partial pages).
 * If any page is locked we wait for it to become unlocked.
 */
void truncate_inode_pages(struct address_space * mapping, loff_t lstart) 
{
	unsigned long start = (lstart + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	unsigned partial = lstart & (PAGE_CACHE_SIZE - 1);
	int unlocked;

	lock_pagecache();
	do {
		unlocked = truncate_list_pages(&mapping->clean_pages, start, &partial);
		unlocked |= truncate_list_pages(&mapping->dirty_pages, start, &partial);
		unlocked |= truncate_list_pages(&mapping->locked_pages, start, &partial);
	} while (unlocked);
	/* Traversed all three lists without dropping the lock */
	unlock_pagecache();
}

static inline int invalidate_this_page2(struct page * page,
					struct list_head * curr,
					struct list_head * head)
{
	int unlocked = 1;

	/*
	 * The page is locked and we hold the pagecache_lock as well
	 * so both page_count(page) and page->buffers stays constant here.
	 */
	SetPageInvalidated(page);
	if (page_count(page) == 1 + !!page->buffers) {
		/* Restart after this page */
		list_del(head);
		list_add_tail(head, curr);

		page_cache_get(page);
		unlock_pagecache();
		truncate_complete_page(page);
	} else {
		if (page->buffers) {
			/* Restart after this page */
			list_del(head);
			list_add_tail(head, curr);

			page_cache_get(page);
			unlock_pagecache();
			block_invalidate_page(page);
		} else
			unlocked = 0;

		ClearPageDirty(page);
		ClearPageUptodate(page);
	}

	return unlocked;
}

static int FASTCALL(invalidate_list_pages2(struct list_head *));
static int invalidate_list_pages2(struct list_head *head)
{
	struct list_head *curr;
	struct page * page;
	int unlocked = 0;

 restart:
	curr = head->prev;
	while (curr != head) {
		page = list_entry(curr, struct page, list);

		if (!TryLockPage(page)) {
			int __unlocked;

			__unlocked = invalidate_this_page2(page, curr, head);
			UnlockPage(page);
			unlocked |= __unlocked;
			if (!__unlocked) {
				curr = curr->prev;
				continue;
			}
		} else {
			/* Restart on this page */
			list_del(head);
			list_add(head, curr);

			page_cache_get(page);
			unlock_pagecache();
			unlocked = 1;
			wait_on_page(page);
		}

		page_cache_release(page);
		if (current->need_resched) {
			__set_current_state(TASK_RUNNING);
			schedule();
		}

		lock_pagecache();
		goto restart;
	}
	return unlocked;
}

/**
 * invalidate_inode_pages2 - Clear all the dirty bits around if it can't
 * free the pages because they're mapped.
 * @mapping: the address_space which pages we want to invalidate
 */
void invalidate_inode_pages2(struct address_space * mapping)
{
	int unlocked;

	lock_pagecache();
	do {
		unlocked = invalidate_list_pages2(&mapping->clean_pages);
		unlocked |= invalidate_list_pages2(&mapping->dirty_pages);
		unlocked |= invalidate_list_pages2(&mapping->locked_pages);
	} while (unlocked);
	unlock_pagecache();
}

static inline struct page * __find_page_nolock(struct address_space *mapping, unsigned long offset, struct page *page)
{
	goto inside;

	for (;;) {
		page = page->next_hash;
inside:
		if (!page)
			goto not_found;
		prefetch(page->next_hash);
		if (page->mapping != mapping)
			continue;
		if (page->index == offset)
			break;
	}

not_found:
	return page;
}

static int do_buffer_fdatasync(struct list_head *head, unsigned long start, unsigned long end, int (*fn)(struct page *))
{
	struct list_head *curr;
	struct page *page;
	int retval = 0;

	lock_pagecache();
	curr = head->next;
	while (curr != head) {
		page = list_entry(curr, struct page, list);
		curr = curr->next;
		if (!page->buffers)
			continue;
		if (page->index >= end)
			continue;
		if (page->index < start)
			continue;

		page_cache_get(page);
		unlock_pagecache();
		lock_page(page);

		/* The buffers could have been free'd while we waited for the page lock */
		if (page->buffers)
			retval |= fn(page);

		UnlockPage(page);
		lock_pagecache();
		curr = page->list.next;
		page_cache_release(page);
	}
	unlock_pagecache();

	return retval;
}

/*
 * Two-stage data sync: first start the IO, then go back and
 * collect the information..
 */
int generic_buffer_fdatasync(struct inode *inode, unsigned long start_idx, unsigned long end_idx)
{
	int retval;

	/* writeout dirty buffers on pages from both clean and dirty lists */
	retval = do_buffer_fdatasync(&inode->i_mapping->dirty_pages, start_idx, end_idx, writeout_one_page);
	retval |= do_buffer_fdatasync(&inode->i_mapping->clean_pages, start_idx, end_idx, writeout_one_page);
	retval |= do_buffer_fdatasync(&inode->i_mapping->locked_pages, start_idx, end_idx, writeout_one_page);

	/* now wait for locked buffers on pages from both clean and dirty lists */
	retval |= do_buffer_fdatasync(&inode->i_mapping->dirty_pages, start_idx, end_idx, waitfor_one_page);
	retval |= do_buffer_fdatasync(&inode->i_mapping->clean_pages, start_idx, end_idx, waitfor_one_page);
	retval |= do_buffer_fdatasync(&inode->i_mapping->locked_pages, start_idx, end_idx, waitfor_one_page);

	return retval;
}

/*
 * In-memory filesystems have to fail their
 * writepage function - and this has to be
 * worked around in the VM layer..
 *
 * We
 *  - mark the page dirty again (but do NOT
 *    add it back to the inode dirty list, as
 *    that would livelock in fdatasync)
 *  - activate the page so that the page stealer
 *    doesn't try to write it out over and over
 *    again.
 */
int fail_writepage(struct page *page)
{
	/* Only activate on memory-pressure, not fsync.. */
	if (PageLaunder(page)) {
		activate_page(page);
		SetPageReferenced(page);
	}

	/* Set the page dirty again, unlock */
	SetPageDirty(page);
	UnlockPage(page);
	return 0;
}

EXPORT_SYMBOL(fail_writepage);

/**
 *      filemap_fdatasync - walk the list of dirty pages of the given address space
 *     	and writepage() all of them.
 * 
 *      @mapping: address space structure to write
 *
 */
int filemap_fdatasync(struct address_space * mapping)
{
	int ret = 0;
	int (*writepage)(struct page *) = mapping->a_ops->writepage;

	lock_pagecache();

        while (!list_empty(&mapping->dirty_pages)) {
		struct page *page = list_entry(mapping->dirty_pages.prev, struct page, list);

		list_del(&page->list);
		list_add(&page->list, &mapping->locked_pages);

		if (!PageDirty(page))
			continue;

		page_cache_get(page);
		unlock_pagecache();

		lock_page(page);

		if (PageDirty(page)) {
			int err;
			ClearPageDirty(page);
			err = writepage(page);
			if (err && !ret)
				ret = err;
		} else
			UnlockPage(page);

		page_cache_release(page);
		lock_pagecache();
	}
	unlock_pagecache();
	return ret;
}

/**
 *      filemap_fdatawait - walk the list of locked pages of the given address space
 *     	and wait for all of them.
 * 
 *      @mapping: address space structure to wait for
 *
 */
int filemap_fdatawait(struct address_space * mapping)
{
	int err, ret = 0;

	lock_pagecache();

        while (!list_empty(&mapping->locked_pages)) {
		struct page *page = list_entry(mapping->locked_pages.next, struct page, list);

		list_del(&page->list);
		if (PageDirty(page))
			list_add(&page->list, &mapping->dirty_pages);
		else
			list_add(&page->list, &mapping->clean_pages);

		if (!PageLocked(page)) {
			if (PageError(page))
				ret = -EIO;
			continue;
		}

		page_cache_get(page);
		unlock_pagecache();

		___wait_on_page(page);
		if (PageError(page))
			ret = -EIO;

		page_cache_release(page);
		lock_pagecache();
	}
	unlock_pagecache();

	err = mapping_get_error(mapping);
	if (!ret)
		ret = err;

	return ret;
}

/*
 * Add a page to the inode page cache.
 *
 * The caller must have locked the page and 
 * set all the page flags correctly..
 */
void add_to_page_cache_locked(struct page * page, struct address_space *mapping, unsigned long index)
{
	if (!PageLocked(page))
		BUG();

	page->index = index;
	page_cache_get(page);
	lock_pagecache();
	add_page_to_inode_queue(mapping, page);
	add_page_to_hash_queue(page, page_hash(mapping, index));
	unlock_pagecache();

	lru_cache_add(page);
}

/*
 * This adds a page to the page cache, starting out as locked,
 * owned by us, but unreferenced, not uptodate and with no errors.
 */
static inline void __add_to_page_cache(struct page * page,
	struct address_space *mapping, unsigned long offset,
	struct page **hash)
{
	ClearPageUptodate(page);
	ClearPageError(page);
	ClearPageDirty(page);
	ClearPageReferenced(page);
	ClearPageArch1(page);
	ClearPageChecked(page);
	LockPage(page);
	page_cache_get(page);
	page->index = offset;
	add_page_to_inode_queue(mapping, page);
	add_page_to_hash_queue(page, hash);
}

void add_to_page_cache(struct page * page, struct address_space * mapping, unsigned long offset)
{
	lock_pagecache();
	__add_to_page_cache(page, mapping, offset, page_hash(mapping, offset));
	unlock_pagecache();
	lru_cache_add(page);
}

int add_to_page_cache_unique(struct page * page,
	struct address_space *mapping, unsigned long offset,
	struct page **hash)
{
	int err;
	struct page *alias;

	lock_pagecache();
	alias = __find_page_nolock(mapping, offset, *hash);

	err = 1;
	if (!alias) {
		__add_to_page_cache(page,mapping,offset,hash);
		err = 0;
	}

	unlock_pagecache();
	if (!err)
		lru_cache_add(page);
	return err;
}

int add_to_page_cache_unique_nolru(struct page * page,
	struct address_space *mapping, unsigned long offset,
	struct page **hash)
{
	int err;
	struct page *alias;

	lock_pagecache();
	alias = __find_page_nolock(mapping, offset, *hash);

	err = 1;
	if (!alias) {
		__add_to_page_cache(page,mapping,offset,hash);
		err = 0;
	}
	unlock_pagecache();

	return err;
}

/*
 * This adds the requested page to the page cache if it isn't already there,
 * and schedules an I/O to read in its contents from disk.
 */
static int FASTCALL(page_cache_read(struct file * file, unsigned long offset));
static int page_cache_read(struct file * file, unsigned long offset)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct page **hash = page_hash(mapping, offset);
	struct page *page; 

	lock_pagecache_readonly();
	page = __find_page_nolock(mapping, offset, *hash);
	unlock_pagecache_readonly();
	if (page)
		return 0;

	page = page_cache_alloc(mapping);
	if (!page)
		return -ENOMEM;

	if (!add_to_page_cache_unique(page, mapping, offset, hash)) {
		int error = mapping->a_ops->readpage(file, page);
		SetPageFresh(page);
		page_cache_release(page);
		return error;
	}
	/*
	 * We arrive here in the unlikely event that someone 
	 * raced with us and added our page to the cache first.
	 */
	page_cache_release(page);
	return 0;
}

/*
 * Read in an entire cluster at once.  A cluster is usually a 64k-
 * aligned block that includes the page requested in "offset."
 */
static int FASTCALL(read_cluster_nonblocking(struct file * file, unsigned long offset,
					     unsigned long filesize));
static int read_cluster_nonblocking(struct file * file, unsigned long offset,
	unsigned long filesize)
{
	unsigned long pages = CLUSTER_PAGES;

	offset = CLUSTER_OFFSET(offset);
	while ((pages-- > 0) && (offset < filesize)) {
		int error = page_cache_read(file, offset);
		if (error < 0)
			return error;
		offset ++;
	}

	return 0;
}

/*
 * Knuth recommends primes in approximately golden ratio to the maximum
 * integer representable by a machine word for multiplicative hashing.
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * These primes are chosen to be bit-sparse, that is operations on
 * them can use shifts and additions instead of multiplications for
 * machines where multiplications are slow.
 */
#if BITS_PER_LONG == 32
/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e370001UL
#elif BITS_PER_LONG == 64
/*  2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define GOLDEN_RATIO_PRIME 0x9e37fffffffc0001UL
#else
#error Define GOLDEN_RATIO_PRIME for your wordsize.
#endif

/*
 * In order to wait for pages to become available there must be
 * waitqueues associated with pages. By using a hash table of
 * waitqueues where the bucket discipline is to maintain all
 * waiters on the same queue and wake all when any of the pages
 * become available, and for the woken contexts to check to be
 * sure the appropriate page became available, this saves space
 * at a cost of "thundering herd" phenomena during rare hash
 * collisions.
 */
inline wait_queue_head_t *page_waitqueue(struct page *page)
{
	const zone_t *zone = page_zone(page);
	wait_queue_head_t *wait = zone->wait_table;
	unsigned long hash = (unsigned long)page;

#if BITS_PER_LONG == 64
	/*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
	unsigned long n = hash;
	n <<= 18;
	hash -= n;
	n <<= 33;
	hash -= n;
	n <<= 3;
	hash += n;
	n <<= 3;
	hash -= n;
	n <<= 4;
	hash += n;
	n <<= 2;
	hash += n;
#else
	/* On some cpus multiply is faster, on others gcc will do shifts */
	hash *= GOLDEN_RATIO_PRIME;
#endif
	hash >>= zone->wait_table_shift;

	return &wait[hash];
}

/*
 * This must be called after every submit_bh with end_io
 * callbacks that would result into the blkdev layer waking
 * up the page after a queue unplug.
 */
void wakeup_page_waiters(struct page * page)
{
	wait_queue_head_t * head;

	head = page_waitqueue(page);
	if (waitqueue_active(head))
		wake_up_filtered(head, page);
}
  

/* 
 * Wait for a page to get unlocked.
 *
 * This must be called with the caller "holding" the page,
 * ie with increased "page->count" so that the page won't
 * go away during the wait..
 *
 * The waiting strategy is to get on a waitqueue determined
 * by hashing. Waiters will then collide, and the newly woken
 * task must then determine whether it was woken for the page
 * it really wanted, and go back to sleep on the waitqueue if
 * that wasn't it. With the waitqueue semantics, it never leaves
 * the waitqueue unless it calls, so the loop moves forward one
 * iteration every time there is
 * (1) a collision 
 * and
 * (2) one of the colliding pages is woken
 *
 * This is the thundering herd problem, but it is expected to
 * be very rare due to the few pages that are actually being
 * waited on at any given time and the quality of the hash function.
 */
void ___wait_on_page(struct page *page)
{
	wait_queue_head_t *waitqueue = page_waitqueue(page);
	struct task_struct *tsk = current;
	DEFINE_FILTERED_WAIT(wait, page);

	add_wait_queue(waitqueue, &wait.wait);
	do {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!PageLocked(page))
			break;
		sync_page(page);
		io_schedule();
	} while (PageLocked(page));
	__set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(waitqueue, &wait.wait);
}

/*
 * unlock_page() is the other half of the story just above
 * __wait_on_page(). Here a couple of quick checks are done
 * and a couple of flags are set on the page, and then all
 * of the waiters for all of the pages in the appropriate
 * wait queue are woken.
 */
void unlock_page(struct page *page)
{
	wait_queue_head_t *waitqueue = page_waitqueue(page);
	ClearPageLaunder(page);
	smp_mb__before_clear_bit();
	if (!test_and_clear_bit(PG_locked, &(page)->flags))
		BUG();
	smp_mb__after_clear_bit(); 

	/*
	 * Although the default semantics of wake_up() are
	 * to wake all, here the specific function is used
	 * to make it even more explicit that a number of
	 * pages are being waited on here.
	 */
	if (waitqueue_active(waitqueue))
		wake_up_filtered(waitqueue, page);
}


/* like wait_on_page but with a timeout (in jiffies).
 * returns 1 on timeout 
 */
int wait_on_page_timeout(struct page *page, int timeout)
{
	wait_queue_head_t *waitqueue = page_waitqueue(page);
	struct task_struct *tsk = current;
	DEFINE_FILTERED_WAIT(wait, page);
	
	if (!PageLocked(page))
		return 0;

	add_wait_queue(waitqueue, &wait.wait);
	do {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (!PageLocked(page))
			break;
		sync_page(page);
		timeout = schedule_timeout(timeout);
	} while (PageLocked(page) && timeout);
	__set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(waitqueue, &wait.wait);
	return PageLocked(page);
}

/*
 * Get a lock on the page, assuming we need to sleep
 * to get it..
 */
static void __lock_page(struct page *page)
{
	wait_queue_head_t *waitqueue = page_waitqueue(page);
	struct task_struct *tsk = current;
	DEFINE_FILTERED_WAIT(wait, page);

	add_wait_queue_exclusive(waitqueue, &wait.wait);
	for (;;) {
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (PageLocked(page)) {
			sync_page(page);
			io_schedule();
		}
		if (!TryLockPage(page))
			break;
	}
	__set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(waitqueue, &wait.wait);
}

/*
 * Get an exclusive lock on the page, optimistically
 * assuming it's not locked..
 */
void lock_page(struct page *page)
{
	if (TryLockPage(page))
		__lock_page(page);
}

/*
 * a rather lightweight function, finding and getting a reference to a
 * hashed page atomically.
 */
struct page * __find_get_page(struct address_space *mapping,
			      unsigned long offset, struct page **hash)
{
	struct page *page;

	/*
	 * We scan the hash list read-only. Addition to and removal from
	 * the hash-list needs a held write-lock.
	 */
	lock_pagecache_readonly();
	page = __find_page_nolock(mapping, offset, *hash);
	if (page)
		page_cache_get(page);
	unlock_pagecache_readonly();
	return page;
}

/*
 * Same as above, but trylock it instead of incrementing the count.
 */
struct page *find_trylock_page(struct address_space *mapping, unsigned long offset)
{
	struct page *page;
	struct page **hash = page_hash(mapping, offset);

	lock_pagecache_readonly();
	page = __find_page_nolock(mapping, offset, *hash);
	if (page) {
		if (TryLockPage(page))
			page = NULL;
	}
	unlock_pagecache_readonly();
	return page;
}
EXPORT_SYMBOL_GPL(find_trylock_page);

/*
 * Must be called with the pagecache lock held,
 * will return with it held (but it may be dropped
 * during blocking operations..
 */
static struct page * FASTCALL(__find_lock_page_helper(struct address_space *, unsigned long, struct page *));
static struct page * __find_lock_page_helper(struct address_space *mapping,
					unsigned long offset, struct page *hash)
{
	struct page *page;

	/*
	 * We scan the hash list read-only. Addition to and removal from
	 * the hash-list needs a held write-lock.
	 */
repeat:
	page = __find_page_nolock(mapping, offset, hash);
	if (page) {
		page_cache_get(page);
		if (TryLockPage(page)) {
			unlock_pagecache();
			lock_page(page);
			lock_pagecache();

			/* Has the page been re-allocated while we slept? */
			if (page->mapping != mapping || page->index != offset) {
				UnlockPage(page);
				page_cache_release(page);
				goto repeat;
			}
		}
	}
	return page;
}
static struct page * FASTCALL(__find_lock_page_helper_readonly(struct address_space *, unsigned long, struct page *));
static struct page * __find_lock_page_helper_readonly(struct address_space *mapping,
					unsigned long offset, struct page *hash)
{
	struct page *page;

	/*
	 * We scan the hash list read-only. Addition to and removal from
	 * the hash-list needs a held write-lock.
	 */
repeat:
	page = __find_page_nolock(mapping, offset, hash);
	if (page) {
		page_cache_get(page);
		if (TryLockPage(page)) {
			unlock_pagecache_readonly();
			lock_page(page);
			lock_pagecache_readonly();

			/* Has the page been re-allocated while we slept? */
			if (page->mapping != mapping || page->index != offset) {
				UnlockPage(page);
				page_cache_release(page);
				goto repeat;
			}
		}
	}
	return page;
}


/*
 * Same as the above, but lock the page too, verifying that
 * it's still valid once we own it.
 */
struct page * __find_lock_page (struct address_space *mapping,
				unsigned long offset, struct page **hash)
{
	struct page *page;

	lock_pagecache_readonly();
	page = __find_lock_page_helper_readonly(mapping, offset, *hash);
	unlock_pagecache_readonly();
	return page;
}

/*
 * Same as above, but create the page if required..
 */
struct page * find_or_create_page(struct address_space *mapping, unsigned long index, unsigned int gfp_mask)
{
	struct page *page;
	struct page **hash = page_hash(mapping, index);

	lock_pagecache_readonly();
	page = __find_lock_page_helper_readonly(mapping, index, *hash);
	unlock_pagecache_readonly();
	if (!page) {
		struct page *newpage = alloc_page(gfp_mask);
		if (newpage) {
			lock_pagecache();
			page = __find_lock_page_helper(mapping, index, *hash);
			if (likely(!page)) {
				page = newpage;
				__add_to_page_cache(page, mapping, index, hash);
				newpage = NULL;
			}
			unlock_pagecache();
			if (newpage == NULL)
				lru_cache_add(page);
			else 
				page_cache_release(newpage);
		}
	}
	return page;	
}

/*
 * Same as grab_cache_page, but do not wait if the page is unavailable.
 * This is intended for speculative data generators, where the data can
 * be regenerated if the page couldn't be grabbed.  This routine should
 * be safe to call while holding the lock for another page.
 */
struct page *grab_cache_page_nowait(struct address_space *mapping, unsigned long index)
{
	struct page *page, **hash;

	hash = page_hash(mapping, index);
	page = __find_get_page(mapping, index, hash);

	if ( page ) {
		if ( !TryLockPage(page) ) {
			/* Page found and locked */
			/* This test is overly paranoid, but what the heck... */
			if ( unlikely(page->mapping != mapping || page->index != index) ) {
				/* Someone reallocated this page under us. */
				UnlockPage(page);
				page_cache_release(page);
				return NULL;
			} else {
				return page;
			}
		} else {
			/* Page locked by someone else */
			page_cache_release(page);
			return NULL;
		}
	}

	page = page_cache_alloc(mapping);
	if ( unlikely(!page) )
		return NULL;	/* Failed to allocate a page */

	if ( unlikely(add_to_page_cache_unique(page, mapping, index, hash)) ) {
		/* Someone else grabbed the page already. */
		page_cache_release(page);
		return NULL;
	}

	return page;
}

#if 0
#define PROFILE_READAHEAD
#define DEBUG_READAHEAD
#endif

/*
 * Read-ahead profiling information
 * --------------------------------
 * Every PROFILE_MAXREADCOUNT, the following information is written 
 * to the syslog:
 *   Percentage of asynchronous read-ahead.
 *   Average of read-ahead fields context value.
 * If DEBUG_READAHEAD is defined, a snapshot of these fields is written 
 * to the syslog.
 */

#ifdef PROFILE_READAHEAD

#define PROFILE_MAXREADCOUNT 1000

static unsigned long total_reada;
static unsigned long total_async;
static unsigned long total_ramax;
static unsigned long total_ralen;
static unsigned long total_rawin;

static void profile_readahead(int async, struct file *filp)
{
	unsigned long flags;

	++total_reada;
	if (async)
		++total_async;

	total_ramax	+= filp->f_ramax;
	total_ralen	+= filp->f_ralen;
	total_rawin	+= filp->f_rawin;

	if (total_reada > PROFILE_MAXREADCOUNT) {
		save_flags(flags);
		cli();
		if (!(total_reada > PROFILE_MAXREADCOUNT)) {
			restore_flags(flags);
			return;
		}

		printk("Readahead average:  max=%ld, len=%ld, win=%ld, async=%ld%%\n",
			total_ramax/total_reada,
			total_ralen/total_reada,
			total_rawin/total_reada,
			(total_async*100)/total_reada);
#ifdef DEBUG_READAHEAD
		printk("Readahead snapshot: max=%ld, len=%ld, win=%ld, raend=%Ld\n",
			filp->f_ramax, filp->f_ralen, filp->f_rawin, filp->f_raend);
#endif

		total_reada	= 0;
		total_async	= 0;
		total_ramax	= 0;
		total_ralen	= 0;
		total_rawin	= 0;

		restore_flags(flags);
	}
}
#endif  /* defined PROFILE_READAHEAD */

/*
 * Read-ahead context:
 * -------------------
 * The read ahead context fields of the "struct file" are the following:
 * - f_raend : position of the first byte after the last page we tried to
 *	       read ahead.
 * - f_ramax : current read-ahead maximum size.
 * - f_ralen : length of the current IO read block we tried to read-ahead.
 * - f_rawin : length of the current read-ahead window.
 *		if last read-ahead was synchronous then
 *			f_rawin = f_ralen
 *		otherwise (was asynchronous)
 *			f_rawin = previous value of f_ralen + f_ralen
 *
 * Read-ahead limits:
 * ------------------
 * MIN_READAHEAD   : minimum read-ahead size when read-ahead.
 * MAX_READAHEAD   : maximum read-ahead size when read-ahead.
 *
 * Synchronous read-ahead benefits:
 * --------------------------------
 * Using reasonable IO xfer length from peripheral devices increase system 
 * performances.
 * Reasonable means, in this context, not too large but not too small.
 * The actual maximum value is:
 *	MAX_READAHEAD + PAGE_CACHE_SIZE = 76k is CONFIG_READA_SMALL is undefined
 *      and 32K if defined (4K page size assumed).
 *
 * Asynchronous read-ahead benefits:
 * ---------------------------------
 * Overlapping next read request and user process execution increase system 
 * performance.
 *
 * Read-ahead risks:
 * -----------------
 * We have to guess which further data are needed by the user process.
 * If these data are often not really needed, it's bad for system 
 * performances.
 * However, we know that files are often accessed sequentially by 
 * application programs and it seems that it is possible to have some good 
 * strategy in that guessing.
 * We only try to read-ahead files that seems to be read sequentially.
 *
 * Asynchronous read-ahead risks:
 * ------------------------------
 * In order to maximize overlapping, we must start some asynchronous read 
 * request from the device, as soon as possible.
 * We must be very careful about:
 * - The number of effective pending IO read requests.
 *   ONE seems to be the only reasonable value.
 * - The total memory pool usage for the file access stream.
 *   This maximum memory usage is implicitly 2 IO read chunks:
 *   2*(MAX_READAHEAD + PAGE_CACHE_SIZE) = 156K if CONFIG_READA_SMALL is undefined,
 *   64k if defined (4K page size assumed).
 */

static inline int get_max_readahead(struct inode * inode)
{
	if (!inode->i_dev || !max_readahead[MAJOR(inode->i_dev)])
		return vm_max_readahead;
	return max_readahead[MAJOR(inode->i_dev)][MINOR(inode->i_dev)];
}

static void generic_file_readahead(int reada_ok,
	struct file * filp, struct inode * inode,
	struct page * page, int flags)
{
	unsigned long end_index;
	unsigned long index = page->index;
	unsigned long max_ahead, ahead;
	unsigned long raend;
	int max_readahead = get_max_readahead(inode);

	/* Readahead might block. */
	if (flags & F_ATOMIC)
		return;

	end_index = inode->i_size >> PAGE_CACHE_SHIFT;

	raend = filp->f_raend;
	max_ahead = 0;

/*
 * The current page is locked.
 * If the current position is inside the previous read IO request, do not
 * try to reread previously read ahead pages.
 * Otherwise decide or not to read ahead some pages synchronously.
 * If we are not going to read ahead, set the read ahead context for this 
 * page only.
 */
	if (PageLocked(page)) {
		if (!filp->f_ralen || index >= raend || index + filp->f_rawin < raend) {
			raend = index;
			if (raend < end_index)
				max_ahead = filp->f_ramax;
			filp->f_rawin = 0;
			filp->f_ralen = 1;
			if (!max_ahead) {
				filp->f_raend  = index + filp->f_ralen;
				filp->f_rawin += filp->f_ralen;
			}
		}
	}
/*
 * The current page is not locked.
 * If we were reading ahead and,
 * if the current max read ahead size is not zero and,
 * if the current position is inside the last read-ahead IO request,
 *   it is the moment to try to read ahead asynchronously.
 * We will later force unplug device in order to force asynchronous read IO.
 */
	else if (reada_ok && filp->f_ramax && raend >= 1 &&
		 index <= raend && index + filp->f_ralen >= raend) {
/*
 * Add ONE page to max_ahead in order to try to have about the same IO max size
 * as synchronous read-ahead (MAX_READAHEAD + 1)*PAGE_CACHE_SIZE.
 * Compute the position of the last page we have tried to read in order to 
 * begin to read ahead just at the next page.
 */
		raend -= 1;
		if (raend < end_index)
			max_ahead = filp->f_ramax + 1;

		if (max_ahead) {
			filp->f_rawin = filp->f_ralen;
			filp->f_ralen = 0;
			reada_ok      = 2;
		}
	}
/*
 * Try to read ahead pages.
 * We hope that ll_rw_blk() plug/unplug, coalescence, requests sort and the
 * scheduler, will work enough for us to avoid too bad actuals IO requests.
 */
	ahead = 0;
	while (ahead < max_ahead) {
		ahead ++;
		if ((raend + ahead) >= end_index)
			break;
		if (page_cache_read(filp, raend + ahead) < 0)
			break;
	}
/*
 * If we tried to read ahead some pages,
 * If we tried to read ahead asynchronously,
 *   Try to force unplug of the device in order to start an asynchronous
 *   read IO request.
 * Update the read-ahead context.
 * Store the length of the current read-ahead window.
 * Double the current max read ahead size.
 *   That heuristic avoid to do some large IO for files that are not really
 *   accessed sequentially.
 */
	if (ahead) {
		filp->f_ralen += ahead;
		filp->f_rawin += filp->f_ralen;
		filp->f_raend = raend + ahead + 1;

		filp->f_ramax += filp->f_ramax;

		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;

#ifdef PROFILE_READAHEAD
		profile_readahead((reada_ok == 2), filp);
#endif
	}

	return;
}

/*
 * Mark a page as having seen activity.
 *
 * We immediately reclaim the inactive clean pages because those are
 * counted as freeable.  We don't modify the inactive dirty ones because
 * we're never sure if those are freeable anyway.
 */
void mark_page_accessed(struct page *page)
{
	/* Mark the page referenced, AFTER checking for previous usage.. */
	SetPageReferenced(page);

	if (unlikely(PageInactiveClean(page) || PageInactiveLaundry(page))) {
		struct zone_struct *zone = page_zone(page);
		int free = zone->free_pages + zone->inactive_clean_pages;

		activate_page(page);
		if (free <= zone->pages_min)
			wakeup_kswapd(GFP_NOIO);
	}
}

EXPORT_SYMBOL(mark_page_accessed);

/*
 * This is a generic file read routine, and uses the
 * inode->i_op->readpage() function for the actual low-level
 * stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 */
void do_generic_file_read(struct file * filp, loff_t *ppos, read_descriptor_t * desc, read_actor_t actor, int flags)
{
	struct address_space *mapping = filp->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;
	unsigned long index, offset;
	struct page *cached_page;
	int reada_ok;
	int error;
	int max_readahead = get_max_readahead(inode);
	loff_t pos;
	int dummy;

	pos = *ppos;
	if (unlikely(pos < 0)) {
		desc->error = -EINVAL;
		return;
	}

	cached_page = NULL;
	index = pos >> PAGE_CACHE_SHIFT;
	offset = pos & ~PAGE_CACHE_MASK;

/*
 * If the current position is outside the previous read-ahead window, 
 * we reset the current read-ahead context and set read ahead max to zero
 * (will be set to just needed value later),
 * otherwise, we assume that the file accesses are sequential enough to
 * continue read-ahead.
 */
	if (index > filp->f_raend || index + filp->f_rawin < filp->f_raend) {
		reada_ok = 0;
		filp->f_raend = 0;
		filp->f_ralen = 0;
		filp->f_ramax = 0;
		filp->f_rawin = 0;
	} else {
		reada_ok = 1;
	}
/*
 * Adjust the current value of read-ahead max.
 * If the read operation stay in the first half page, force no readahead.
 * Otherwise try to increase read ahead max just enough to do the read request.
 * Then, at least MIN_READAHEAD if read ahead is ok,
 * and at most MAX_READAHEAD in all cases.
 */
	if (!index && offset + desc->count <= (PAGE_CACHE_SIZE >> 1)) {
		filp->f_ramax = 0;
	} else {
		unsigned long needed;

		needed = ((offset + desc->count) >> PAGE_CACHE_SHIFT) + 1;

		if (filp->f_ramax < needed)
			filp->f_ramax = needed;

		if (reada_ok && filp->f_ramax < vm_min_readahead)
				filp->f_ramax = vm_min_readahead;
		if (filp->f_ramax > max_readahead)
			filp->f_ramax = max_readahead;
	}

	for (;;) {
		struct page *page, **hash, **next_hash;
		unsigned long end_index, nr, ret;

		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
			
		if (index > end_index) {
			desc->error = 0;
			break;
		}
		nr = PAGE_CACHE_SIZE;
		if (index == end_index) {
			nr = inode->i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset) {
				desc->error = 0;
				break;
			}
		}

		nr = nr - offset;

		/*
		 * Try to find the data in the page cache..
		 */
		hash = page_hash(mapping, index);
		prefetch(*hash);
		if (likely(hash + 1 != page_hash_table + PAGE_HASH_SIZE))
			prefetch(*(hash + 1));

		lock_pagecache_readonly();
		page = __find_page_nolock(mapping, index, *hash);
		if (!page)
			goto no_cached_page;
		page_cache_get(page);
		unlock_pagecache_readonly();
found_page:

		if (!Page_Uptodate(page))
			goto page_not_up_to_date;
		generic_file_readahead(reada_ok, filp, inode, page, flags);
page_ok:
		if (!(filp->f_mode & FMODE_WRITE) &&
		    page->buffers &&
		    !PageDirty(page) &&
		    !TryLockPage(page)) {
			if (page->buffers)
				try_to_release_page(page, 0);
			UnlockPage(page);
		}

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping->i_mmap_shared != NULL)
			flush_dcache_page(page);

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 *
		 * The actor routine returns how many bytes were actually used..
		 * NOTE! This may not be the same as how much of a user buffer
		 * we filled up (we may be padding etc), so we can only update
		 * "pos" here (the actor routine has to update the user buffer
		 * pointers and the remaining count).
		 */
		ret = actor(desc, page, offset, nr);
		offset += ret;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;

		if (PageFresh(page) && page->age <= INITIAL_AGE && !page_referenced_lock(page, &dummy) ) {
			ClearPageFresh(page);
			deactivate_page(page);
		} else {
			/*
			 * Mark the page accessed if we read the beginning or
			 * we just did an lseek, in order to prevent a series
			 * of small reads marking the page multiple times.
			 */
			if (!offset || !filp->f_reada)
				mark_page_accessed(page);
		}

		page_cache_release(page);
		if (ret == nr && desc->count)
			continue;
		break;

/*
 * Ok, the page was not immediately readable, so let's try to read ahead while we're at it..
 */
page_not_up_to_date:
		generic_file_readahead(reada_ok, filp, inode, page, flags);

		if (Page_Uptodate(page))
			goto page_ok;

		/* Get exclusive access to the page ... */
		if (flags & F_ATOMIC) {
			if (TryLockPage(page)) {
				if (Page_Uptodate(page))
					goto page_ok;
				desc->error = -EWOULDBLOCKIO;
				page_cache_release(page);
				break;
			}
			printk("page_not_up_to_date: atomic trylock succeeded\n");
		} else
			lock_page(page);

		/* Did it get unhashed before we got the lock? */
		if (!page->mapping) {
			UnlockPage(page);
			page_cache_release(page);
			continue;
		}

		/* Did somebody else fill it already? */
		if (Page_Uptodate(page)) {
			UnlockPage(page);
			goto page_ok;
		}
		/* Get rid of any stale I/O errors */
		ClearPageError(page);

readpage:
		/* ... and start the actual read. The read will unlock the page. */
		ClearPageInvalidated(page);
		error = mapping->a_ops->readpage(filp, page);

		if (!error) {
			if (Page_Uptodate(page))
				goto page_ok;

			/* Again, try some read-ahead while waiting for the page to finish.. */
			generic_file_readahead(reada_ok, filp, inode, page, flags);
			if (!(flags & F_ATOMIC))
				wait_on_page(page);
			if (Page_Uptodate(page) ||
			    (PageInvalidated(page) && !PageError(page)))
				goto page_ok;
			error = (flags & F_ATOMIC) ? -EWOULDBLOCKIO : -EIO;
			/* Ugly --- if we're racing against another
			 * instance of generic_file_read(), the
			 * PageInvalidated flag can have been cleared by
			 * now.  We can avoid that race by taking the
			 * page lock again on this rare path and testing
			 * once more. */
			if (error == -EIO) {
				lock_page(page);
				if (Page_Uptodate(page) ||
				    (PageInvalidated(page) && !PageError(page))) {
					unlock_page(page);
					goto page_ok;
				}
				unlock_page(page);
			}
		}

		/* UHHUH! A synchronous read error occurred. Report it */
		desc->error = error;
		page_cache_release(page);
		break;

no_cached_page:
		if (flags & F_ATOMIC) {
			unlock_pagecache_readonly();
			desc->error = -EWOULDBLOCKIO;
			break;
		}
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 *
		 * We get here with the page cache lock held.
		 */
		if (!cached_page) {
			unlock_pagecache_readonly();
			cached_page = page_cache_alloc(mapping);
			if (!cached_page) {
				desc->error = -ENOMEM;
				break;
			}
		} else
			unlock_pagecache_readonly();

		/*
		 * Somebody may have added the page while we
		 * dropped the page cache lock. Check for that.
		 */
		lock_pagecache();
		page = __find_page_nolock(mapping, index, *hash);
		if (page) {
			page_cache_get(page);
			unlock_pagecache();
			goto found_page;
		}

		/*
		 * Ok, add the new page to the hash-queues...
		 */
		page = cached_page;
		__add_to_page_cache(page, mapping, index, hash);
		SetPageFresh(page);
		unlock_pagecache();
		lru_cache_add(page);		
		cached_page = NULL;

		goto readpage;
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	filp->f_reada = 1;
	if (cached_page)
		page_cache_release(cached_page);
	UPDATE_ATIME(inode);
}

/* Only files on a varyio-capable block device are allowed to do
 * sector-aligned direct IO, and then only if the direct_sector_IO
 * method is present.  Otherwise we need to constrain the IO to fs
 * blocksize. */

static inline int get_min_io_size(struct inode *inode)
{
	kdev_t dev = inode->i_dev;
	if (inode->i_mapping->a_ops->direct_sector_IO != NULL &&
	    get_blkdev_varyio(dev))
		return get_hardsect_size(dev);
	return 1 << inode->i_blkbits;
}

/*
 * i_sem is already held on entry
 * i_alloc_sem is taken during the function (exclusive for extends, shared for
 * reads/overwrites)
 * i_sem is dropped once we've mapped the new IO.
 * i_alloc_sem is kept until the IO completes. 
 */

static ssize_t generic_file_direct_IO(int rw, struct file * filp, char * buf, size_t *d_count, loff_t offset, int *ret_errno)
{
	ssize_t retval, progress;
	int new_iobuf, chunk_size, blocksize_mask, blocksize, blocksize_bits, iosize;
	struct kiobuf * iobuf;
	struct address_space * mapping = filp->f_dentry->d_inode->i_mapping;
	struct inode * inode = mapping->host;
	loff_t size = inode->i_size;
	int sectsize = get_min_io_size(inode), good_sectsize = 0;
	int sectsize_bits;
	size_t count, count_orig = 0;

	count = *d_count;
	new_iobuf = 0;
	iobuf = filp->f_iobuf;
	if (test_and_set_bit(0, &filp->f_iobuf_lock)) {
		/*
		 * A parallel read/write is using the preallocated iobuf
		 * so just run slow and allocate a new one.
		 */
		retval = alloc_kiovec(1, &iobuf);
		if (retval)
			goto out;
		new_iobuf = 1;
	}

	blocksize = 1 << inode->i_blkbits;
	blocksize_bits = inode->i_blkbits;
	blocksize_mask = blocksize - 1;
	chunk_size = KIO_MAX_ATOMIC_IO << 10;

	retval = -EINVAL;

	/* Work out the highest usable sector size that is compatible
	 * with the input argument alignments.  If even the min sector
	 * size doesn't work, we just fail with EINVAL. */
	while (sectsize <= blocksize) {
		int sectsize_mask = sectsize - 1;
		
		if ((offset & sectsize_mask) ||
		    (count & sectsize_mask) ||
		    ((unsigned long) buf & sectsize_mask))
			break;
		good_sectsize = sectsize;
		sectsize *= 2;
	}
	if (!good_sectsize)
		goto out_free;
	sectsize = good_sectsize;
	sectsize_bits = 9;
	while (good_sectsize > 512)
		good_sectsize >>= 1, sectsize_bits++;

	/* If the last block in the file is a partial block, do I/O for a
	 * full sector, and adjust the return value below. */
	if ((rw == READ) && (offset + count > size)) {
		int sectsize_mask = sectsize - 1;

		count = size - offset;
		if (count & sectsize_mask) {
			count_orig = count;
			count = (count + sectsize) & ~sectsize_mask;
		}
	}

	/*
	 * Flush to disk exclusively the _data_, metadata must remain
	 * completly asynchronous or performance will go to /dev/null.
	 */
	retval = filemap_fdatasync(mapping);
	if (retval == 0)
		retval = fsync_inode_data_buffers(inode);
	if (retval == 0)
		retval = filemap_fdatawait(mapping);
	if (retval < 0)
		goto out_free;

	progress = retval = 0;
	while (count > 0) {
		iosize = count;
		if (iosize > chunk_size)
			iosize = chunk_size;

		retval = map_user_kiobuf(rw, iobuf, (unsigned long) buf, iosize);
		if (retval)
			break;

		if (mapping->a_ops->direct_sector_IO != NULL) 
			retval = mapping->a_ops->direct_sector_IO
				(rw, filp, iobuf,
				 (offset+progress) >> blocksize_bits, 
				 blocksize, sectsize, 
				 ((offset+progress) & (blocksize_mask)) >> sectsize_bits);
		else {
			BUG_ON(blocksize != sectsize);
			retval = mapping->a_ops->direct_IO
				(rw, filp, iobuf,
				 (offset+progress) >> blocksize_bits, 
				 blocksize);
		}
		if (rw == READ && retval > 0)
			mark_dirty_kiobuf(iobuf, retval);
		
		if (retval >= 0) {
			count -= retval;
			buf += retval;
			/* warning: weird semantics here, we're reporting a read behind the end of the file */
			progress += retval;
		} else {
			*ret_errno = retval;
		}

		unmap_kiobuf(iobuf);

		if (retval != iosize)
			break;
	}
	*d_count -= progress;

	if (progress)
		retval = progress;

	/* truncate the read to the end of the file */
	if (unlikely(count_orig && retval > count_orig && retval > 0))
		retval = count_orig;

 out_free:
	if (!new_iobuf)
		clear_bit(0, &filp->f_iobuf_lock);
	else
		free_kiovec(1, &iobuf);
 out:	
	return retval;
}

int file_read_actor(read_descriptor_t * desc, struct page *page, unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long left, count = desc->count;

	if (size > count)
		size = count;

	kaddr = kmap(page);
	left = __copy_to_user(desc->buf, kaddr + offset, size);
	kunmap(page);
	
	if (left) {
		size -= left;
		desc->error = -EFAULT;
	}
	desc->count = count - size;
	desc->written += size;
	desc->buf += size;
	return size;
}

static ssize_t generic_file_new_read(struct file * filp, char * buf, size_t count, loff_t *ppos, int flags, int *ret_errno)
{
	ssize_t retval;

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (filp->f_flags & O_DIRECT)
		goto o_direct;

	retval = -EFAULT;
	if (access_ok(VERIFY_WRITE, buf, count)) {
		retval = 0;

		if (count) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.count = count;
			desc.buf = buf;
			desc.error = 0;
			do_generic_file_read(filp, ppos, &desc, file_read_actor, flags);

			retval = desc.written;
			if (!retval)
				retval = desc.error;
		}
	}
 out:
	return retval;

 o_direct:
	{
		loff_t pos = *ppos, size;
		struct address_space *mapping = filp->f_dentry->d_inode->i_mapping;
		struct inode *inode = mapping->host;
		size_t d_count;

		retval = 0;
		if (!count)
			goto out; /* skip atime */
		d_count = count;
		down_read(&inode->i_alloc_sem);
		down(&inode->i_sem);
		size = inode->i_size;
		if (pos < size) {
			retval = generic_file_direct_IO(READ, filp, buf, &d_count, pos, ret_errno);
			if (retval > 0)
				*ppos = pos + retval;
		}
		up(&inode->i_sem);
		up_read(&inode->i_alloc_sem);
		UPDATE_ATIME(filp->f_dentry->d_inode);
		goto out;
	}
}

/*
 * This is the "read()" routine for all filesystems
 * that can use the page cache directly.
 */
ssize_t generic_file_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
	int ret_errno = 0;
	return generic_file_new_read(filp, buf, count, ppos, 0, &ret_errno);
}

int file_send_actor(read_descriptor_t * desc, struct page *page, unsigned long offset , unsigned long size)
{
	ssize_t written;
	unsigned long count = desc->count;
	struct file *file = (struct file *) desc->buf;

	if (size > count)
		size = count;

 	if (file->f_op->sendpage) {
 		written = file->f_op->sendpage(file, page, offset,
					       size, &file->f_pos, size<count);
	} else {
		char *kaddr;
		mm_segment_t old_fs;

		old_fs = get_fs();
		set_fs(KERNEL_DS);

		kaddr = kmap(page);
		written = file->f_op->write(file, kaddr + offset, size, &file->f_pos);
		kunmap(page);

		set_fs(old_fs);
	}
	if (written < 0) {
		desc->error = written;
		written = 0;
	}
	desc->count = count - written;
	desc->written += written;
	return written;
}

static ssize_t common_sendfile(int out_fd, int in_fd, loff_t *offset, size_t count)
{
	ssize_t retval;
	struct file * in_file, * out_file;
	struct inode * in_inode, * out_inode;

	/*
	 * Get input file, and verify that it is ok..
	 */
	retval = -EBADF;
	in_file = fget(in_fd);
	if (!in_file)
		goto out;
	if (!(in_file->f_mode & FMODE_READ))
		goto fput_in;
	retval = -EINVAL;
	in_inode = in_file->f_dentry->d_inode;
	if (!in_inode)
		goto fput_in;
	if (!in_inode->i_mapping->a_ops->readpage)
		goto fput_in;
	retval = locks_verify_area(FLOCK_VERIFY_READ, in_inode, in_file, in_file->f_pos, count);
	if (retval)
		goto fput_in;

	/*
	 * Get output file, and verify that it is ok..
	 */
	retval = -EBADF;
	out_file = fget(out_fd);
	if (!out_file)
		goto fput_in;
	if (!(out_file->f_mode & FMODE_WRITE))
		goto fput_out;
	retval = -EINVAL;
	if (!out_file->f_op || !out_file->f_op->write)
		goto fput_out;
	out_inode = out_file->f_dentry->d_inode;
	retval = locks_verify_area(FLOCK_VERIFY_WRITE, out_inode, out_file, out_file->f_pos, count);
	if (retval)
		goto fput_out;

	retval = 0;
	if (count) {
		if (!offset)
			offset = &in_file->f_pos;

		if (IS_SENDFILE_FOP(in_inode) && in_file->f_op && ((struct file_operations_ext *)in_file->f_op)->sendfile)
			retval = ((struct file_operations_ext *)in_file->f_op)->sendfile(in_file, out_file, count, offset);
		else {
			read_descriptor_t desc;

			desc.written = 0;
			desc.count = count;
			desc.buf = (char *) out_file;
			desc.error = 0;
			do_generic_file_read(in_file, offset, &desc, file_send_actor, 0);

			retval = desc.written;
			if (!retval)
				retval = desc.error;
		}
	}

fput_out:
	fput(out_file);
fput_in:
	fput(in_file);
out:
	return retval;
}

asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	loff_t pos, *ppos = NULL;
	ssize_t ret;
	if (offset) {
		off_t off;
		if (unlikely(get_user(off, offset)))
			return -EFAULT;
		pos = off;
		ppos = &pos;
	}
	ret = common_sendfile(out_fd, in_fd, ppos, count);
	if (offset)
		put_user((off_t)pos, offset);
	return ret;
}

asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, loff_t *offset, size_t count)
{
	loff_t pos, *ppos = NULL;
	ssize_t ret;
	if (offset) {
		if (unlikely(copy_from_user(&pos, offset, sizeof(loff_t))))
			return -EFAULT;
		ppos = &pos;
	}
	ret = common_sendfile(out_fd, in_fd, ppos, count);
	if (offset)
		put_user(pos, offset);
	return ret;
}

static ssize_t do_readahead(struct file *file, unsigned long index, unsigned long nr)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	unsigned long max;

	if (!mapping || !mapping->a_ops || !mapping->a_ops->readpage)
		return -EINVAL;

	/* Limit it to the size of the file.. */
	max = (mapping->host->i_size + ~PAGE_CACHE_MASK) >> PAGE_CACHE_SHIFT;
	if (index > max)
		return 0;
	max -= index;
	if (nr > max)
		nr = max;

	/* And limit it to a sane percentage of the inactive list.. */
	max = (nr_inactive_clean_pages() + nr_inactive_laundry_pages()) / 2;
	if (nr > max)
		nr = max;

	while (nr) {
		page_cache_read(file, index);
		index++;
		nr--;
	}
	return 0;
}

asmlinkage ssize_t sys_readahead(int fd, loff_t offset, size_t count)
{
	ssize_t ret;
	struct file *file;

	ret = -EBADF;
	file = fget(fd);
	if (file) {
		if (file->f_mode & FMODE_READ) {
			unsigned long start = offset >> PAGE_CACHE_SHIFT;
			unsigned long len = (count + ((long)offset & ~PAGE_CACHE_MASK)) >> PAGE_CACHE_SHIFT;
			ret = do_readahead(file, start, len);
		}
		fput(file);
	}
	return ret;
}

/*
 * Read-ahead and flush behind for MADV_SEQUENTIAL areas.  Since we are
 * sure this is sequential access, we don't need a flexible read-ahead
 * window size -- we can always use a large fixed size window.
 */
static void nopage_sequential_readahead(struct vm_area_struct * vma,
	unsigned long pgoff, unsigned long filesize)
{
	unsigned long ra_window;

	ra_window = get_max_readahead(vma->vm_file->f_dentry->d_inode);
	ra_window = CLUSTER_OFFSET(ra_window + CLUSTER_PAGES - 1);

	/* vm_raend is zero if we haven't read ahead in this area yet.  */
	if (vma->vm_raend == 0)
		vma->vm_raend = vma->vm_pgoff + ra_window;

	/*
	 * If we've just faulted the page half-way through our window,
	 * then schedule reads for the next window, and release the
	 * pages in the previous window.
	 */
	if ((pgoff + (ra_window >> 1)) == vma->vm_raend) {
		unsigned long start = vma->vm_pgoff + vma->vm_raend;
		unsigned long end = start + ra_window;

		if (end > ((vma->vm_end >> PAGE_SHIFT) + vma->vm_pgoff))
			end = (vma->vm_end >> PAGE_SHIFT) + vma->vm_pgoff;
		if (start > end)
			return;

		while ((start < end) && (start < filesize)) {
			if (read_cluster_nonblocking(vma->vm_file,
							start, filesize) < 0)
				break;
			start += CLUSTER_PAGES;
		}
		run_task_queue(&tq_disk);

		/* if we're far enough past the beginning of this area,
		   recycle pages that are in the previous window. */
		if (vma->vm_raend > (vma->vm_pgoff + ra_window + ra_window)) {
			unsigned long window = ra_window << PAGE_SHIFT;

			end = vma->vm_start + ((vma->vm_raend - vma->vm_pgoff) << PAGE_SHIFT);
			end -= window + window;
			filemap_sync(vma, end - window, window, MS_INVALIDATE);
		}

		vma->vm_raend += ra_window;
	}

	return;
}

/*
 * filemap_nopage() is invoked via the vma operations vector for a
 * mapped memory region to read in file data during a page fault.
 *
 * The goto's are kind of ugly, but this streamlines the normal case of having
 * it in the page cache, and handles the special cases reasonably without
 * having a lot of duplicated code.
 */
struct page * filemap_nopage(struct vm_area_struct * area, unsigned long address, int unused)
{
	int error;
	struct file *file = area->vm_file;
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;
	struct page *page, **hash;
	unsigned long size, pgoff, endoff;

	pgoff = ((address - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;
	endoff = ((area->vm_end - area->vm_start) >> PAGE_CACHE_SHIFT) + area->vm_pgoff;

retry_all:
	/*
	 * An external ptracer can access pages that normally aren't
	 * accessible..
	 */
	size = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	if ((pgoff >= size) && (area->vm_mm == current->mm))
		return NULL;

	/* The "size" of the file, as far as mmap is concerned, isn't bigger than the mapping */
	if (size > endoff)
		size = endoff;

	/*
	 * Do we have something in the page cache already?
	 */
	hash = page_hash(mapping, pgoff);
retry_find:
	page = __find_get_page(mapping, pgoff, hash);
	if (!page)
		goto no_cached_page;

	/*
	 * Ok, found a page in the page cache, now we need to check
	 * that it's up-to-date.
	 */
	if (!Page_Uptodate(page))
		goto page_not_uptodate;

success:
	if (!(file->f_mode & FMODE_WRITE) &&
	    page->buffers &&
	    !PageDirty(page) &&
	    !TryLockPage(page)) {
		if (page->buffers)
			try_to_release_page(page, 0);
		UnlockPage(page);
	}
 	/*
	 * Try read-ahead for sequential areas.
	 */
	if (VM_SequentialReadHint(area))
		nopage_sequential_readahead(area, pgoff, size);

	/*
	 * Found the page and have a reference on it, need to check sharing
	 * and possibly copy it over to another page..
	 */
	mark_page_accessed(page);
	flush_page_to_ram(page);
	return page;

no_cached_page:
	/*
	 * If the requested offset is within our file, try to read a whole 
	 * cluster of pages at once.
	 *
	 * Otherwise, we're off the end of a privately mapped file,
	 * so we need to map a zero page.
	 */
	if ((pgoff < size) && !VM_RandomReadHint(area))
		error = read_cluster_nonblocking(file, pgoff, size);
	else
		error = page_cache_read(file, pgoff);

	/*
	 * The page we want has now been added to the page cache.
	 * In the unlikely event that someone removed it in the
	 * meantime, we'll just come back here and read it again.
	 */
	if (error >= 0)
		goto retry_find;

	/*
	 * An error return from page_cache_read can result if the
	 * system is low on memory, or a problem occurs while trying
	 * to schedule I/O.
	 */
	if (error == -ENOMEM)
		return NOPAGE_OOM;
	return NULL;

page_not_uptodate:
	lock_page(page);

	/* Did it get unhashed while we waited for it? */
	if (!page->mapping) {
		UnlockPage(page);
		page_cache_release(page);
		goto retry_all;
	}

	/* Did somebody else get it up-to-date? */
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto success;
	}

	if (!mapping->a_ops->readpage(file, page)) {
		wait_on_page(page);
		if (Page_Uptodate(page))
			goto success;
	}

	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	lock_page(page);

	/* Somebody truncated the page on us? */
	if (!page->mapping) {
		UnlockPage(page);
		page_cache_release(page);
		goto retry_all;
	}

	/* Somebody else successfully read it in? */
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto success;
	}
	ClearPageError(page);
	if (!mapping->a_ops->readpage(file, page)) {
		wait_on_page(page);
		if (Page_Uptodate(page))
			goto success;
	}

	/*
	 * Things didn't work out. Return zero to tell the
	 * mm layer so, possibly freeing the page cache page first.
	 */
	page_cache_release(page);
	return NULL;
}

/* Called with mm->page_table_lock held to protect against other
 * threads/the swapper from ripping pte's out from under us.
 */
static inline int filemap_sync_pte(pte_t * ptep, struct vm_area_struct *vma,
	unsigned long address, unsigned int flags)
{
	pte_t pte = *ptep;

	if (pte_present(pte)) {
		struct page *page = pte_page(pte);
		if (VALID_PAGE(page) && !PageReserved(page) && ptep_test_and_clear_dirty(ptep)) {
			flush_tlb_page(vma, address);
			set_page_dirty(page);	/* This actually does not sleep */
			return 0;
		}
	}
	return 0;
}

static inline int filemap_sync_pte_range(pmd_t * pmd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned long offset, unsigned int flags)
{
	pte_t *pte, *mapping;
	unsigned long end;
	int error;

	if (pmd_none(*pmd))
		return 0;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return 0;
	}
	mapping = pte = pte_offset_map(pmd, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte(pte, vma, address + offset, flags);
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	pte_unmap(mapping);
	return error;
}

static inline int filemap_sync_pmd_range(pgd_t * pgd,
	unsigned long address, unsigned long size, 
	struct vm_area_struct *vma, unsigned int flags)
{
	pmd_t * pmd;
	unsigned long offset, end;
	int error;

	if (pgd_none(*pgd))
		return 0;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		return 0;
	}
	pmd = pmd_offset(pgd, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	error = 0;
	do {
		error |= filemap_sync_pte_range(pmd, address, end - address, vma, offset, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return error;
}

int filemap_sync(struct vm_area_struct * vma, unsigned long address,
	size_t size, unsigned int flags)
{
	pgd_t * dir;
	unsigned long end = address + size;
	int error = 0;

	if ((vma->vm_flags & VM_HUGETLB) || address < vma->vm_start || end > vma->vm_end)
		return error;

	/* Aquire the lock early; it may be possible to avoid dropping
	 * and reaquiring it repeatedly.
	 */
	spin_lock(&vma->vm_mm->page_table_lock);

	dir = pgd_offset(vma->vm_mm, address);
	flush_cache_range(vma, end - size, end);
	if (address >= end)
		BUG();
	do {
		error |= filemap_sync_pmd_range(dir, address, end - address, vma, flags);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_range(vma, end - size, end);

	spin_unlock(&vma->vm_mm->page_table_lock);

	return error;
}

static struct vm_operations_struct generic_file_vm_ops = {
	nopage:		filemap_nopage,
};

/* This is used for a general mmap of a disk file */

int generic_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;

	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		if (!mapping->a_ops->writepage)
			return -EINVAL;
	}
	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	UPDATE_ATIME(inode);
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

/*
 * The msync() system call.
 */

/*
 * MS_SYNC syncs the entire file - including mappings.
 *
 * MS_ASYNC initiates writeout of just the dirty mapped data.
 * This provides no guarantee of file integrity - things like indirect
 * blocks may not have started writeout.  MS_ASYNC is primarily useful
 * where the application knows that it has finished with the data and
 * wishes to intelligently schedule its own I/O traffic.
 */
static int msync_interval(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int flags)
{
	int ret = 0;
	struct file * file = vma->vm_file;

	if ( (flags & MS_INVALIDATE) && (vma->vm_flags & VM_LOCKED) )
		return -EBUSY;

	if (file && (vma->vm_flags & VM_SHARED)) {
		ret = filemap_sync(vma, start, end-start, flags);

		if (!ret && (flags & (MS_SYNC|MS_ASYNC))) {
			struct inode * inode = file->f_dentry->d_inode;

			down(&inode->i_sem);
			ret = filemap_fdatasync(inode->i_mapping);
			if (flags & MS_SYNC) {
				int err;

				if (file->f_op && file->f_op->fsync) {
					err = file->f_op->fsync(file, file->f_dentry, 1);
					if (err && !ret)
						ret = err;
				}
				err = filemap_fdatawait(inode->i_mapping);
				if (err && !ret)
					ret = err;
			}
			up(&inode->i_sem);
		}
	}
	return ret;
}

asmlinkage long sys_msync(unsigned long start, size_t len, int flags)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error, error = -EINVAL;

	down_read(&current->mm->mmap_sem);
	if (start & ~PAGE_MASK)
		goto out;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		goto out;
	if ((flags & MS_ASYNC) && (flags & MS_SYNC))
		goto out;

	error = 0;
	if (end == start)
		goto out;
	/*
	 * If the interval [start,end) covers some unmapped address ranges,
	 * just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	unmapped_error = 0;
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;
		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}
		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = msync_interval(vma, start, end, flags);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}
		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = msync_interval(vma, start, vma->vm_end, flags);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}
out:
	up_read(&current->mm->mmap_sem);
	return error;
}

static inline void setup_read_behavior(struct vm_area_struct * vma,
	int behavior)
{
	VM_ClearReadHint(vma);
	switch(behavior) {
		case MADV_SEQUENTIAL:
			vma->vm_flags |= VM_SEQ_READ;
			break;
		case MADV_RANDOM:
			vma->vm_flags |= VM_RAND_READ;
			break;
		default:
			break;
	}
	return;
}

static long madvise_fixup_start(struct vm_area_struct * vma,
	unsigned long end, int behavior)
{
	struct vm_area_struct * n;
	struct mm_struct * mm = vma->vm_mm;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_end = end;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	vma->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_start = end;
	__insert_vm_struct(mm, n);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

static long madvise_fixup_end(struct vm_area_struct * vma,
	unsigned long start, int behavior)
{
	struct vm_area_struct * n;
	struct mm_struct * mm = vma->vm_mm;

	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -EAGAIN;
	*n = *vma;
	n->vm_start = start;
	n->vm_pgoff += (n->vm_start - vma->vm_start) >> PAGE_SHIFT;
	setup_read_behavior(n, behavior);
	n->vm_raend = 0;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_end = start;
	__insert_vm_struct(mm, n);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

static long madvise_fixup_middle(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int behavior)
{
	struct vm_area_struct * left, * right;
	struct mm_struct * mm = vma->vm_mm;

	left = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!left)
		return -EAGAIN;
	right = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!right) {
		kmem_cache_free(vm_area_cachep, left);
		return -EAGAIN;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	right->vm_start = end;
	right->vm_pgoff += (right->vm_start - left->vm_start) >> PAGE_SHIFT;
	left->vm_raend = 0;
	right->vm_raend = 0;
	if (vma->vm_file)
		atomic_add(2, &vma->vm_file->f_count);

	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	vma->vm_pgoff += (start - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_raend = 0;
	lock_vma_mappings(vma);
	spin_lock(&mm->page_table_lock);
	vma->vm_start = start;
	vma->vm_end = end;
	setup_read_behavior(vma, behavior);
	__insert_vm_struct(mm, left);
	__insert_vm_struct(mm, right);
	spin_unlock(&mm->page_table_lock);
	unlock_vma_mappings(vma);
	return 0;
}

/*
 * We can potentially split a vm area into separate
 * areas, each area with its own behavior.
 */
static long madvise_behavior(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, int behavior)
{
	int error = 0;

	/* This caps the number of vma's this process can own */
	if (vma->vm_mm->map_count > max_map_count)
		return -ENOMEM;

	if (start == vma->vm_start) {
		if (end == vma->vm_end) {
			setup_read_behavior(vma, behavior);
			vma->vm_raend = 0;
		} else
			error = madvise_fixup_start(vma, end, behavior);
	} else {
		if (end == vma->vm_end)
			error = madvise_fixup_end(vma, start, behavior);
		else
			error = madvise_fixup_middle(vma, start, end, behavior);
	}

	return error;
}

/*
 * Schedule all required I/O operations, then run the disk queue
 * to make sure they are started.  Do not wait for completion.
 */
static long madvise_willneed(struct vm_area_struct * vma,
	unsigned long start, unsigned long end)
{
	long error = -EBADF;
	struct file * file;
	unsigned long size, rlim_rss;

	/* Doesn't work if there's no mapped file. */
	if (!vma->vm_file)
		return error;
	file = vma->vm_file;
	size = (file->f_dentry->d_inode->i_size + PAGE_CACHE_SIZE - 1) >>
							PAGE_CACHE_SHIFT;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	/* Make sure this doesn't exceed the process's max rss. */
	error = -EIO;
	rlim_rss = current->rlim ?  current->rlim[RLIMIT_RSS].rlim_cur :
				LONG_MAX; /* default: see resource.h */
	if ((vma->vm_mm->rss + (end - start)) > rlim_rss)
		return error;

	/* round to cluster boundaries if this isn't a "random" area. */
	if (!VM_RandomReadHint(vma)) {
		start = CLUSTER_OFFSET(start);
		end = CLUSTER_OFFSET(end + CLUSTER_PAGES - 1);

		while ((start < end) && (start < size)) {
			error = read_cluster_nonblocking(file, start, size);
			start += CLUSTER_PAGES;
			if (error < 0)
				break;
		}
	} else {
		while ((start < end) && (start < size)) {
			error = page_cache_read(file, start);
			start++;
			if (error < 0)
				break;
		}
	}

	/* Don't wait for someone else to push these requests. */
	run_task_queue(&tq_disk);

	return error;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range call sets things up for refill_inactive to actually free
 * these pages later if no one else has touched them in the meantime,
 * although we could add these pages to a global reuse list for
 * refill_inactive to pick up before reclaiming other pages.
 *
 * NB: This interface discards data rather than pushes it out to swap,
 * as some implementations do.  This has performance implications for
 * applications like large transactional databases which want to discard
 * pages in anonymous maps after committing to backing store the data
 * that was kept in them.  There is no reason to write this data out to
 * the swap area if the application is discarding it.
 *
 * An interface that causes the system to free clean pages and flush
 * dirty pages is already available as msync(MS_INVALIDATE).
 */
static long madvise_dontneed(struct vm_area_struct * vma,
	unsigned long start, unsigned long end)
{
	if (vma->vm_flags & VM_LOCKED)
		return -EINVAL;

	zap_page_range(vma, start, end - start);
	return 0;
}

static long madvise_vma(struct vm_area_struct * vma, unsigned long start,
	unsigned long end, int behavior)
{
	long error = -EBADF;

	switch (behavior) {
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
		error = madvise_behavior(vma, start, end, behavior);
		break;

	case MADV_WILLNEED:
		error = madvise_willneed(vma, start, end);
		break;

	case MADV_DONTNEED:
		error = madvise_dontneed(vma, start, end);
		break;

	default:
		error = -EINVAL;
		break;
	}
		
	return error;
}

/*
 * The madvise(2) system call.
 *
 * Applications can use madvise() to advise the kernel how it should
 * handle paging I/O in this VM area.  The idea is to help the kernel
 * use appropriate read-ahead and caching techniques.  The information
 * provided is advisory only, and can be safely disregarded by the
 * kernel without affecting the correct operation of the application.
 *
 * behavior values:
 *  MADV_NORMAL - the default behavior is to read clusters.  This
 *		results in some read-ahead and read-behind.
 *  MADV_RANDOM - the system should read the minimum amount of data
 *		on any access, since it is unlikely that the appli-
 *		cation will need more than what it asks for.
 *  MADV_SEQUENTIAL - pages in the given range will probably be accessed
 *		once, so they can be aggressively read ahead, and
 *		can be freed soon after they are accessed.
 *  MADV_WILLNEED - the application is notifying the system to read
 *		some pages ahead.
 *  MADV_DONTNEED - the application is finished with the given range,
 *		so the kernel can free resources associated with it.
 *
 * return values:
 *  zero    - success
 *  -EINVAL - start + len < 0, start is not page-aligned,
 *		"behavior" is not a valid value, or application
 *		is attempting to release locked or shared pages.
 *  -ENOMEM - addresses in the specified range are not currently
 *		mapped, or are outside the AS of the process.
 *  -EIO    - an I/O error occurred while paging in data.
 *  -EBADF  - map exists, but area maps something that isn't a file.
 *  -EAGAIN - a kernel resource was temporarily unavailable.
 */
asmlinkage long sys_madvise(unsigned long start, size_t len, int behavior)
{
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error = 0;
	int error = -EINVAL;

	down_write(&current->mm->mmap_sem);

	if (start & ~PAGE_MASK)
		goto out;
	len = (len + ~PAGE_MASK) & PAGE_MASK;
	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = madvise_vma(vma, start, end,
							behavior);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = madvise_vma(vma, start, vma->vm_end, behavior);
		if (error)
			goto out;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

out:
	up_write(&current->mm->mmap_sem);
	return error;
}

/*
 * Later we can get more picky about what "in core" means precisely.
 * For now, simply check to see if the page is in the page cache,
 * and is up to date; i.e. that no page-in operation would be required
 * at this time if an application were to map and access this page.
 */
static unsigned char mincore_page(struct vm_area_struct * vma,
	unsigned long pgoff)
{
	unsigned char present = 0;
	struct address_space * as = vma->vm_file->f_dentry->d_inode->i_mapping;
	struct page * page, ** hash = page_hash(as, pgoff);

	lock_pagecache_readonly();
	page = __find_page_nolock(as, pgoff, *hash);
	if ((page) && (Page_Uptodate(page)))
		present = 1;
	unlock_pagecache_readonly();

	return present;
}

static long mincore_vma(struct vm_area_struct * vma,
	unsigned long start, unsigned long end, unsigned char * vec)
{
	long error, i, remaining;
	unsigned char * tmp;

	error = -ENOMEM;
	if (!vma->vm_file)
		return error;

	start = ((start - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (end > vma->vm_end)
		end = vma->vm_end;
	end = ((end - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;

	error = -EAGAIN;
	tmp = (unsigned char *) __get_free_page(GFP_KERNEL);
	if (!tmp)
		return error;

	/* (end - start) is # of pages, and also # of bytes in "vec */
	remaining = (end - start),

	error = 0;
	for (i = 0; remaining > 0; remaining -= PAGE_SIZE, i++) {
		int j = 0;
		long thispiece = (remaining < PAGE_SIZE) ?
						remaining : PAGE_SIZE;

		while (j < thispiece)
			tmp[j++] = mincore_page(vma, start++);

		if (copy_to_user(vec + PAGE_SIZE * i, tmp, thispiece)) {
			error = -EFAULT;
			break;
		}
	}

	free_page((unsigned long) tmp);
	return error;
}

/*
 * The mincore(2) system call.
 *
 * mincore() returns the memory residency status of the pages in the
 * current process's address space specified by [addr, addr + len).
 * The status is returned in a vector of bytes.  The least significant
 * bit of each byte is 1 if the referenced page is in memory, otherwise
 * it is zero.
 *
 * Because the status of a page can change after mincore() checks it
 * but before it returns to the application, the returned vector may
 * contain stale information.  Only locked pages are guaranteed to
 * remain in memory.
 *
 * return values:
 *  zero    - success
 *  -EFAULT - vec points to an illegal address
 *  -EINVAL - addr is not a multiple of PAGE_CACHE_SIZE,
 *		or len has a nonpositive value
 *  -ENOMEM - Addresses in the range [addr, addr + len] are
 *		invalid for the address space of this process, or
 *		specify one or more pages which are not currently
 *		mapped
 *  -EAGAIN - A kernel resource was temporarily unavailable.
 */
asmlinkage long sys_mincore(unsigned long start, size_t len,
	unsigned char * vec)
{
	int index = 0;
	unsigned long end;
	struct vm_area_struct * vma;
	int unmapped_error = 0;
	long error = -EINVAL;

	down_read(&current->mm->mmap_sem);

	if (start & ~PAGE_CACHE_MASK)
		goto out;
	len = (len + ~PAGE_CACHE_MASK) & PAGE_CACHE_MASK;
	end = start + len;
	if (end < start)
		goto out;

	error = 0;
	if (end == start)
		goto out;

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 */
	vma = find_vma(current->mm, start);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < vma->vm_end. */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
		}

		/* Here vma->vm_start <= start < vma->vm_end. */
		if (end <= vma->vm_end) {
			if (start < end) {
				error = mincore_vma(vma, start, end,
							&vec[index]);
				if (error)
					goto out;
			}
			error = unmapped_error;
			goto out;
		}

		/* Here vma->vm_start <= start < vma->vm_end < end. */
		error = mincore_vma(vma, start, vma->vm_end, &vec[index]);
		if (error)
			goto out;
		index += (vma->vm_end - start) >> PAGE_CACHE_SHIFT;
		start = vma->vm_end;
		vma = vma->vm_next;
	}

out:
	up_read(&current->mm->mmap_sem);
	return error;
}

static inline
struct page *__read_cache_page(struct address_space *mapping,
				unsigned long index,
				int (*filler)(void *,struct page*),
				void *data)
{
	struct page **hash = page_hash(mapping, index);
	struct page *page, *cached_page = NULL;
	int err;
repeat:
	page = __find_get_page(mapping, index, hash);
	if (!page) {
		if (!cached_page) {
			cached_page = page_cache_alloc(mapping);
			if (!cached_page)
				return ERR_PTR(-ENOMEM);
		}
		page = cached_page;
		if (add_to_page_cache_unique(page, mapping, index, hash))
			goto repeat;
		cached_page = NULL;
		err = filler(data, page);
		if (err < 0) {
			page_cache_release(page);
			page = ERR_PTR(err);
		}
	}
	if (cached_page)
		page_cache_release(cached_page);
	return page;
}

/*
 * Read into the page cache. If a page already exists,
 * and Page_Uptodate() is not set, try to fill the page.
 */
struct page *read_cache_page(struct address_space *mapping,
				unsigned long index,
				int (*filler)(void *,struct page*),
				void *data)
{
	struct page *page;
	int err;

retry:
	page = __read_cache_page(mapping, index, filler, data);
	if (IS_ERR(page))
		goto out;
	mark_page_accessed(page);
	if (Page_Uptodate(page))
		goto out;

	lock_page(page);
	if (!page->mapping) {
		UnlockPage(page);
		page_cache_release(page);
		goto retry;
	}
	if (Page_Uptodate(page)) {
		UnlockPage(page);
		goto out;
	}
	err = filler(data, page);
	if (err < 0) {
		page_cache_release(page);
		page = ERR_PTR(err);
	}
 out:
	return page;
}

static inline struct page * __grab_cache_page(struct address_space *mapping,
				unsigned long index, struct page **cached_page)
{
	struct page *page, **hash = page_hash(mapping, index);
repeat:
	page = __find_lock_page(mapping, index, hash);
	if (!page) {
		if (!*cached_page) {
			*cached_page = page_cache_alloc(mapping);
			if (!*cached_page)
				return NULL;
		}
		page = *cached_page;
		if (add_to_page_cache_unique(page, mapping, index, hash))
			goto repeat;
		*cached_page = NULL;
	}
	return page;
}

inline void remove_suid(struct inode *inode)
{
	unsigned int mode;

	/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
	mode = (inode->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

	/* was any of the uid bits set? */
	mode &= inode->i_mode;
	if (mode && !capable(CAP_FSETID)) {
		inode->i_mode &= ~mode;
		mark_inode_dirty(inode);
	}
}

/*
 * precheck_file_write():
 * Check the conditions on a file descriptor prior to beginning a write
 * on it.  Contains the common precheck code for both buffered and direct
 * IO.
 */
static int precheck_file_write(struct file *file, struct inode *inode,
			       size_t *count, loff_t *ppos)
{
	ssize_t		err;
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	loff_t		pos = *ppos;
	
	err = -EINVAL;
	if (pos < 0)
		goto out;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out;
	}

	/* FIXME: this is for backwards compatibility with 2.4 */
	if (!S_ISBLK(inode->i_mode) && file->f_flags & O_APPEND)
		*ppos = pos = inode->i_size;

	/*
	 * Check whether we've reached the file size limit.
	 */
	err = -EFBIG;

	if (!S_ISBLK(inode->i_mode) && limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (*count > limit - (unsigned long)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			*count = limit - (unsigned long)pos;
		}
	}

	/*
	 *	LFS rule 
	 */
	if ( pos + *count > MAX_NON_LFS && !(file->f_flags&O_LARGEFILE)) {
		if (pos >= MAX_NON_LFS) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (*count > MAX_NON_LFS - (unsigned long)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			*count = MAX_NON_LFS - (unsigned long)pos;
		}
	}

	/*
	 *	Are we about to exceed the fs block limit ?
	 *
	 *	If we have written data it becomes a short write
	 *	If we have exceeded without writing data we send
	 *	a signal and give them an EFBIG.
	 *
	 *	Linus frestrict idea will clean these up nicely..
	 */
	 
	if (!S_ISBLK(inode->i_mode)) {
		if (pos >= inode->i_sb->s_maxbytes)
		{
			if (*count || pos > inode->i_sb->s_maxbytes) {
				send_sig(SIGXFSZ, current, 0);
				err = -EFBIG;
				goto out;
			}
			/* zero-length writes at ->s_maxbytes are OK */
		}

		if (pos + *count > inode->i_sb->s_maxbytes)
			*count = inode->i_sb->s_maxbytes - pos;
	} else {
		if (is_read_only(inode->i_rdev)) {
			err = -EPERM;
			goto out;
		}
		if (pos >= inode->i_size) {
			if (*count || pos > inode->i_size) {
				err = -ENOSPC;
				goto out;
			}
		}

		if (pos + *count > inode->i_size)
			*count = inode->i_size - pos;
	}

	err = 0;
	if (*count == 0)
		goto out;

	remove_suid(inode);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty_sync(inode);
	
out:
	return err;
}

/*
 * Write to a file through the page cache. 
 *
 * We currently put everything into the page cache prior to writing it.
 * This is not a problem when writing full pages. With partial pages,
 * however, we first have to read the data into the cache, then
 * dirty the page, and finally schedule it for writing. Alternatively, we
 * could write-through just the portion of data that would go into that
 * page, but that would kill performance for applications that write data
 * line by line, and it's prone to race conditions.
 *
 * Note that this routine doesn't try to keep track of dirty pages. Each
 * file system has to do this all by itself, unfortunately.
 *							okir@monad.swb.de
 */
ssize_t
do_generic_file_write(struct file *file,const char *buf,size_t count, loff_t *ppos)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode	*inode = mapping->host;
	loff_t		pos;
	struct page	*page, *cached_page;
	ssize_t		written;
	long		status = 0;
	ssize_t		err;
	unsigned	bytes;

	cached_page = NULL;
	pos = *ppos;
	written = 0;
	
	err = precheck_file_write(file, inode, &count, &pos);
	if (err != 0 || count == 0)
		goto out;
	
	do {
		unsigned long index, offset;
		long page_fault;
		char *kaddr;
		int deactivate = 1;

		/*
		 * Try to find the page in the cache. If it isn't there,
		 * allocate a free page.
		 */
		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count) {
			bytes = count;
			deactivate = 0;
		}

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 */
		{ volatile unsigned char dummy;
			__get_user(dummy, buf);
			__get_user(dummy, buf+bytes-1);
		}

		status = -ENOMEM;	/* we'll assign it later anyway */
		page = __grab_cache_page(mapping, index, &cached_page);
		if (!page)
			break;

		/* We have exclusive IO access to the page.. */
		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		status = mapping->a_ops->prepare_write(file, page, offset, offset+bytes);
		if (status)
			goto sync_failure;
		kaddr = kmap(page);
		page_fault = __copy_from_user(kaddr+offset, buf, bytes);
		kunmap(page);
		flush_dcache_page(page);
		status = mapping->a_ops->commit_write(file, page, offset, offset+bytes);
		if (page_fault)
			goto fail_write;
		if (!status)
			status = bytes;

		if (status >= 0) {
			written += status;
			count -= status;
			pos += status;
			buf += status;
		}
unlock:
		/* Mark it unlocked again and drop the page.. */
		UnlockPage(page);
		if (deactivate)
			deactivate_page(page);
		else
			mark_page_accessed(page);
		page_cache_release(page);

		if (status < 0)
			break;
	} while (count);
done:
	*ppos = pos;

	if (cached_page)
		page_cache_release(cached_page);

	/* For now, when the user asks for O_SYNC, we'll actually
	 * provide O_DSYNC. */
	if (status >= 0) {
		if ((file->f_flags & O_SYNC) || IS_SYNC(inode)) {
			status = generic_osync_inode(inode, OSYNC_METADATA|OSYNC_DATA);
			/* If that failed, we don't know _where_ it failed so
			 * we really need to fail the whole IO. */
			if (status)
				written = 0;
		}
	}
	
	err = written ? written : status;
out:

	return err;
fail_write:
	status = -EFAULT;
	goto unlock;

sync_failure:
	/*
	 * If blocksize < pagesize, prepare_write() may have instantiated a
	 * few blocks outside i_size.  Trim these off again.
	 */
	UnlockPage(page);
	page_cache_release(page);
	if (pos + bytes > inode->i_size)
		vmtruncate(inode, inode->i_size);
	goto done;
}

ssize_t
static do_generic_direct_write(struct file *file, const char *buf, size_t *d_count, loff_t *ppos, int *ret_errno)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode	*inode = mapping->host;
	loff_t		pos;
	ssize_t		written;
	long		status = 0;
	ssize_t		err;

	pos = *ppos;
	written = 0;
	
	err = precheck_file_write(file, inode, d_count, &pos);

	if (err != 0 || *d_count == 0)
		goto out;
	
	if (!file->f_flags & O_DIRECT)
		BUG();

	written = generic_file_direct_IO(WRITE, file, (char *) buf, d_count, pos, ret_errno);

	if (written > 0) {
		loff_t end = pos + written;
		if (end > inode->i_size && !S_ISBLK(inode->i_mode)) {
			inode->i_size = end;
			mark_inode_dirty(inode);
		}
		*ppos = end;
		invalidate_inode_pages2(mapping);
	}

	/*
	 * Sync the fs metadata but not the minor inode changes and
	 * of course not the data as we did direct DMA for the IO.
	 */
	if (written >= 0 && file->f_flags & O_SYNC)
		status = generic_osync_inode(inode, OSYNC_METADATA);

	err = written ? written : status;
out:
	return err;
}

static ssize_t do_odirect_fallback(struct file *file, struct inode *inode,
				   const char *buf, size_t count, loff_t *ppos)
{
	int err;
	ssize_t ret;

	down(&inode->i_sem);
	ret = do_generic_file_write(file, buf, count, ppos);
	if (ret > 0) {
		err = do_fdatasync(file);
		if (err)
			ret = err;
	}
	up(&inode->i_sem);
	return ret;
}

ssize_t
generic_file_write(struct file *file,const char *buf,size_t count, loff_t *ppos)
{
	struct inode	*inode = file->f_dentry->d_inode->i_mapping->host;
	ssize_t		err;
	int		ret_errno = 0;
	size_t		d_count;

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	if (file->f_flags & O_DIRECT) {
		d_count = count;
		/* do_generic_direct_write may drop i_sem during the
		   actual IO */
		down_read(&inode->i_alloc_sem);
		down(&inode->i_sem);
		err = do_generic_direct_write(file, buf, &d_count, ppos, &ret_errno);
		up(&inode->i_sem);
		up_read(&inode->i_alloc_sem);
		if (unlikely(err == -ENOTBLK || ret_errno == -ENOTBLK))  {
			err = do_odirect_fallback(file, inode,
				buf + (count - d_count), d_count, ppos);
			if (err > 0)
				err = (count - d_count) + err;
			else if (d_count < count)
				err = (count - d_count);
			/* if dcount == count, then nothing has been written,
			   so just return the error */
		}
	} else {
		down(&inode->i_sem);
		err = do_generic_file_write(file, buf, count, ppos);
		up(&inode->i_sem);
	}

	return err;
}

void __init page_cache_init(unsigned long mempages)
{
	page_hash_table = alloc_large_system_hash("Page-cache",	
						  sizeof(struct page *),
						  12,
						  1,
						  &page_hash_bits,
						  NULL);
}

/* address_space_map
 *	Maps a series of pages from the page cache into the given array.
 */
static int address_space_map(struct address_space *as, unsigned long index,
		int nr, struct page **pages,
		int *nr_newp, struct page **new_pages)
{
	struct page *cached_page = NULL;
	int nr_new = 0;
	int ret;

	if (unlikely(nr <= 0)) {
		*nr_newp = nr_new;
		return 0;
	}

	ret = 0;

	lock_pagecache();

	while (nr > 0) {
		struct page **hash = page_hash(as, index);
		struct page *page;

		page = __find_page_nolock(as, index, *hash);
		if (page) {
			page_cache_get(page);
got_page:
			pages[ret++] = page;
			index++;
			nr--;
			continue;
		}

		if (cached_page) {
			__add_to_page_cache(cached_page, as, index, hash);
			unlock_pagecache();
			lru_cache_add(cached_page);
			lock_pagecache();
			nr_new++;
			*new_pages++ = page = cached_page;
			cached_page = NULL;
			goto got_page;
		}
		unlock_pagecache();

		cached_page = page_cache_alloc(as);
		if (!cached_page)
			goto out;

		/* Okay, we now have an allocated page.  Retry
		 * the search and add. */
		lock_pagecache();
	}

	unlock_pagecache();

out:
	if (cached_page)
		page_cache_release(cached_page);

	*nr_newp = nr_new;
	return ret ? ret : -ENOMEM;
}

struct iodesc {
	struct worktodo	wtd;

	struct page	*good_page;	/* the highest Uptodate page */
	int		good_idx;
	int		err;
	int		did_read;
	int		rw;
	loff_t		pos;

	struct page	**pages;
	struct page	**new_pages;
	struct page	**cur_pagep;
	int		nr_pages;
	int		nr_new_pages;

	struct address_space *as;
	struct file	*file;
	kvec_cb_t	cb;

	size_t		size;
	unsigned long	transferred;
	unsigned	offset;
	struct kveclet	*veclet;

	struct kvec_dst	src;

	int		sync;
	unsigned long	rlimit_fsize;

#define READDESC_NR_DEF	3
	struct page *def_pages[READDESC_NR_DEF];
	struct page *def_new_pages[READDESC_NR_DEF];
};

static void __iodesc_free(struct iodesc *io, int unlock)
{
	kvec_cb_t cb;
	ssize_t res;

	if (unlock) {
		unsigned i;
		for (i=0; i<io->nr_pages; i++) {
			struct page *page = io->pages[i];
			UnlockPage(page);
			page_cache_release(page);
		}
	} else {
		unsigned i;
		for (i=0; i<io->nr_pages; i++)
			page_cache_release(io->pages[i]);
	}

	if (io->new_pages != io->def_new_pages)
		kfree(io->new_pages);
	if (io->pages != io->def_pages)
		kfree(io->pages);

	cb = io->cb;
	res = io->transferred ? io->transferred : io->err;
	kfree(io);

	cb.fn(cb.data, cb.vec, res);
}

/* By the time this function is called, all of the pages prior to
 * the current good_idx have been released appropriately.  The remaining
 * duties are to release any remaining pages and to honour O_SYNC.
 */
static void __iodesc_finish_write(struct iodesc *io)
{
	pr_debug("__iodesc_finish_write(%p)\n", io);

	__iodesc_free(io, WRITE == io->rw);
}

/* This is mostly ripped from generic_file_write */
static int __iodesc_write_page(struct iodesc *io, struct page *page)
{
	char *kaddr = kmap(page);
	unsigned long bytes;
	unsigned long offset;
	long status;
	int done = 0;

	offset = io->offset;
	kaddr += offset;

	bytes = PAGE_CACHE_SIZE - offset;
	if (io->size < bytes)
		bytes = io->size;

	pr_debug("__iodesc_write_page(%p (%lu), %lu %lu %lu)\n", page, page->index, offset, bytes);

	if (!page->mapping) {
		io->err = -EIO;
		printk("iodesc_write_page: page was truncated\n");
	} else
		io->err = io->as->a_ops->prepare_write(io->file, page,
							offset, offset + bytes);

	if (unlikely(io->err)) {
		pr_debug("prepare_write: %d\n", io->err);
		kunmap(page);
		return 1;
	}

	kvec_dst_map(&io->src);
	memcpy_from_kvec_dst(kaddr, &io->src, bytes);
	kvec_dst_unmap(&io->src);	/* commit_write may block */

	flush_dcache_page(page);
	status = io->as->a_ops->commit_write(io->file, page,
						offset, offset+bytes);

	/* We don't handle short writes */
	if (status > 0 && status != bytes)
		done = 1;

	if (!status)
		status = bytes;

	if (likely(status > 0)) {
		io->transferred += status;
		io->size -= status;
		io->offset = (offset + status) & (PAGE_CACHE_SIZE - 1);

		if (io->offset)
			done = 1;
	} else {
		io->err = status;
		done = 1;
	}

	kunmap(page);
	return done;
}

void __iodesc_sync_wait_page(void *data)
{
	struct iodesc *io = data;

	do {
		struct buffer_head *bh, *head = io->pages[io->good_idx]->buffers;

		if (!head)
			continue;

		bh = head;
		do {
			if (buffer_locked(bh)) {
				pr_debug("waiting on bh=%pi io=%p\n", bh, io);
				if (!wtd_wait_on_buffer(&io->wtd, bh))
					return;
			}
			if (buffer_req(bh) && !buffer_uptodate(bh)) {
				pr_debug("io err bh=%p (%p)\n", bh, io);
				io->err = -EIO;
				break;
			}
		} while ((bh = bh->b_this_page) != head);
	} while (!io->err && ++io->good_idx < io->nr_pages) ;

	pr_debug("finish_write(%p)\n", io);
	__iodesc_finish_write(io);
}

static void __iodesc_do_write(void *data)
{
	struct iodesc *io = data;
	unsigned i;

	for (i=0; i<io->nr_pages; i++) {
		if (__iodesc_write_page(io, io->pages[i]))
			break;
	}

	up(&io->file->f_dentry->d_inode->i_sem);

	if (io->sync) {
		io->good_idx = 0;

		pr_debug("writing out pages(%p)\n", io);
		for (i=0; i<io->nr_pages; i++) {
			if (io->pages[i]->buffers)
				writeout_one_page(io->pages[i]);
		}

		pr_debug("calling __iodesc_sync_wait_page(%p)\n", io);
		wtd_set_action(&io->wtd, __iodesc_sync_wait_page, io);
		__iodesc_sync_wait_page(io);
		return;
	}

	__iodesc_finish_write(io);
}

static void __iodesc_write_lock_next_page(void *data)
{
	struct iodesc *io = data;
	pr_debug("__iodesc_write_next_page(%p)\n", io);

	while (io->good_idx < io->nr_pages) {
		io->good_page = io->pages[io->good_idx++];
		if (io->good_page == *io->cur_pagep)
			io->cur_pagep++;
		else {
			if (!wtd_lock_page(&io->wtd, io->good_page))
				return;
		}
	}

	//Is this faster? __iodesc_do_write(io);
	wtd_set_action(&io->wtd, __iodesc_do_write, io);
	wtd_queue(&io->wtd);
}

static void __generic_file_write_iodesc(struct iodesc *io)
{
	struct inode *inode = io->file->f_dentry->d_inode;
	time_t now = CURRENT_TIME;

	remove_suid(inode);
	if (inode->i_ctime != now || inode->i_mtime != now) {
		inode->i_ctime = inode->i_mtime = now;
		mark_inode_dirty_sync(inode);
	}

	wtd_set_action(&io->wtd, __iodesc_write_lock_next_page, io);
	io->sync = !!(io->file->f_flags & O_SYNC);
	io->good_idx = 0;
	io->cur_pagep = io->new_pages;
	__iodesc_write_lock_next_page(io);
}

static void __iodesc_read_finish(struct iodesc *io)
{
	struct page **src_pagep;
	char *dst_addr, *src_addr;
	int src_off;
	size_t size;
	size_t valid;

	struct kveclet *veclet = io->veclet;
	struct page *dst_page = veclet->page;
	int dst_len = veclet->length;
	int dst_off = veclet->offset;


	pr_debug("__iodesc_read_finish: good_idx = %d\n", io->good_idx);
	if (io->good_idx <= 0)
		goto no_data;

	size = io->size;
	src_off = io->offset;
	src_pagep = io->pages;
	src_addr = kmap(*src_pagep);

	valid = (size_t)io->good_idx << PAGE_CACHE_SHIFT;
	valid -= src_off;
	pr_debug("size=%d valid=%d src_off=%d\n", size, valid, src_off);

	if (valid < size)
		size = valid;

	dst_addr = kmap(veclet->page);

	while (size > 0) {
		int this = PAGE_CACHE_SIZE - src_off;
		if ((PAGE_SIZE - dst_off) < this)
			this = PAGE_SIZE - dst_off;
		if (size < this)
			this = size;
		pr_debug("this=%d src_off=%d dst_off=%d dst_len=%d\n",
			this, src_off, dst_off, dst_len);
		memcpy(dst_addr + dst_off, src_addr + src_off, this);

		src_off += this;
		dst_off += this;
		dst_len -= this;
		size -= this;
		io->transferred += this;
		pr_debug("read_finish: this=%d transferred=%d\n",
			 this, io->transferred);

		if (size <= 0)
			break;

		if (dst_len <= 0) {
			kunmap(dst_page);
			veclet++;
			dst_page = veclet->page;
			dst_off = veclet->offset;
			dst_len = veclet->length;
			dst_addr = kmap(dst_page);
		}

		if (src_off >= PAGE_SIZE) { /* FIXME: PAGE_CACHE_SIZE */
			kunmap(*src_pagep);
			pr_debug("page(%lu)->count = %d\n",
				 (*src_pagep)->index,
				 atomic_read(&(*src_pagep)->count));
			src_pagep++;
			src_addr = kmap(*src_pagep);
			src_off = 0;
		}
	}
	kunmap(dst_page);
	kunmap(*src_pagep);
no_data:
	__iodesc_free(io, 0);
}

static void __iodesc_make_uptodate(void *data)
{
	struct iodesc *io = data;
	struct page *page = io->good_page;
	int locked = 1;

	pr_debug("__iodesc_make_uptodate: io=%p index=%lu\n", io, page->index);
again:
	while (Page_Uptodate(page)) {
		pr_debug("page index %lu uptodate\n", page->index);
		if (locked) {
			UnlockPage(page);
			locked = 0;
		}
		io->did_read = 0;
		io->good_idx++;
		if (io->good_idx >= io->nr_pages) {
			__iodesc_read_finish(io);
			return;
		}
		page = io->good_page = io->pages[io->good_idx];
		pr_debug("__iodesc_make_uptodate: index=%lu\n", page->index);
	}

	if (!locked) {
		if (!wtd_lock_page(&io->wtd, page))
			return;
		locked = 1;
	}

	if (!io->did_read) {
		/* We haven't tried reading this page before, give it a go. */
		pr_debug("attempting to read %lu\n", page->index);
		io->did_read = 1;
		if (!page->mapping) {
			printk("page not mapped.  erroring out.\n");
			io->err = 0;
		} else {
			locked = 0;
			io->err = page->mapping->a_ops->readpage(io->file, page);
			if (!io->err) {
				if (Page_Uptodate(page))
					goto again;
				if (wtd_lock_page(&io->wtd, page)) {
					locked = 1;
					goto again;
				}
				return;
			}
		}
	}

	if (locked)
		UnlockPage(page);

	/* We've already read this page before.  Set err to EIO and quite */
	if (!io->err)
		io->err = -EIO;
	__iodesc_read_finish(io);
}

static void __wtdgeneric_file_read_iodesc(void *data);

static void __generic_file_read_iodesc(struct iodesc *io, int mayblock)
{
	int (*readpage)(struct file *, struct page *);
	int i;

	/* First things first: if we have newly instantiated page cache 
	 * pages, go them and submit for io since they won't be uptodate.
	 */
	wtd_set_action(&io->wtd, __iodesc_make_uptodate, io);
	readpage = io->as->a_ops->readpage;
	for (i=0; i<io->nr_new_pages; i++) {
		int ret;
		if (!mayblock) {
			wtd_set_action(&io->wtd, __wtdgeneric_file_read_iodesc, io);
			wtd_queue(&io->wtd);
			return;
		}
		ret = readpage(io->file, io->new_pages[i]);
		if (ret)
			printk(KERN_DEBUG "__generic_file_read_kiovec: readpage(%lu) = %d\n", io->new_pages[i]->index, ret);
	}

	/* We have now issued readpage() on all new pages.  Don't 
	 * consider them to be special anymore. 
	 */
	io->nr_new_pages = 0;

	for (i=0; i<io->nr_pages; i++) {
		struct page *page = io->pages[i];
		if (Page_Uptodate(page)) {
			pr_debug("__generic_file_read_iodesc: %lu is uptodate\n", page->index);
			continue;
		}

		if (!mayblock) {
			wtd_set_action(&io->wtd, __wtdgeneric_file_read_iodesc, io);
			wtd_queue(&io->wtd);
			return;
		}
	       	/* Lock the page and if it is still mapped in the file,
		 * attempt to read it in.
		 */
		if (!TryLockPage(page)) {
			if (page->mapping) {
				int ret = readpage(io->file, page);
				if (ret)
					printk(KERN_DEBUG "__generic_file_read_iodesc: readpage(%lu): %d\n", page->index, ret);
			} else { /* not mapped anymore -- probably truncated */
				printk("page not mapped.  truncated!\n");
				UnlockPage(page);
			}
		}

		if (!Page_Uptodate(page) && io->good_idx == -1) {
			pr_debug("first good_idx=%d (%lu)\n", i, page->index);
			io->good_idx = i;
			io->good_page = page;
		}
	}

	/* Whee, all the pages are uptodate! */
	if (!io->good_page) {
		pr_debug("all pages uptodate!\n");
		io->good_idx = io->nr_pages;
		__iodesc_read_finish(io);
		return;
	}

	pr_debug("locking good_page\n");
	if (wtd_lock_page(&io->wtd, io->good_page))
		__iodesc_make_uptodate(io);
	return;
}

static void __wtdgeneric_file_read_iodesc(void *data)
{
	struct iodesc *io = data;
	__generic_file_read_iodesc(io, 1);
}

static int generic_file_rw_kvec(struct file *file, int rw, kvec_cb_t cb,
			 size_t size, loff_t pos);

int generic_file_kvec_read(struct file *file, kvec_cb_t cb, size_t size, loff_t pos)
{
	return generic_file_rw_kvec(file, READ, cb, size, pos);
}

int generic_file_kvec_write(struct file *file, kvec_cb_t cb, size_t size, loff_t pos)
{
	return generic_file_rw_kvec(file, WRITE, cb, size, pos);
}

void wtd_rw_kvec_core(void *);
int rw_kvec_core(struct iodesc *io);

int generic_file_rw_kvec(struct file *file, int rw, kvec_cb_t cb,
			 size_t size, loff_t pos)
{
	struct inode *inode = file->f_dentry->d_inode;
	int append = file->f_flags & O_APPEND;
	struct iodesc *io = NULL;
	int ret;

	ret = -EINVAL;
	if (unlikely(rw != READ && rw != WRITE))
		goto out;

	/* Don't check pos when appending, but otherwise do santity 
	 * checks before allocating memory.  -'ve offsets are invalid.
	 */
	if (unlikely(!append && pos < 0))
		goto out;

	ret = -ENOMEM;
	io = kmalloc(sizeof(*io), GFP_KERNEL);
	if (!io)
		goto out;

	memset(io, 0, sizeof(*io));
	io->file = file;
	io->rw = rw;
	io->cb = cb;
	io->size = size;
	io->pos = pos;
	io->rlimit_fsize = current->rlim[RLIMIT_FSIZE].rlim_cur;
	wtd_set_action(&io->wtd, wtd_rw_kvec_core, io);

	if ((rw == READ) || (0 == wtd_down(&io->wtd, &inode->i_sem)))
		return rw_kvec_core(io);

	return 0;

out:
	if (!ret)
		cb.fn(cb.data, cb.vec, ret);
	return ret;
}

void wtd_rw_kvec_core(void *data)
{
	struct iodesc *io = data;
	kvec_cb_t cb = io->cb;
	int ret = rw_kvec_core(io);
	if (ret)
		cb.fn(cb.data, cb.vec, ret);
}

int rw_kvec_core(struct iodesc *io)
{
	int append = io->file->f_flags & O_APPEND;
	struct inode *inode = io->file->f_dentry->d_inode;
	struct address_space *as = inode->i_mapping;
	unsigned long index;
	unsigned long eindex;
	unsigned long nr_pages;
	int ret;

	/* Use the mapping host inode to get block and CODA right */
	inode = as->host;

	if (io->rw == WRITE) {
		unsigned long long tmp;
		loff_t limit;

		/* We've already down'd the inode semaphore */
		if (append)
			io->pos = inode->i_size;

		limit = io->rlimit_fsize;
		if (likely(RLIM_INFINITY == limit))
			limit = OFFSET_MAX;

		/* Filesystem limits take precedence over user limits */
		if (likely(inode->i_sb->s_maxbytes < limit))
			limit = inode->i_sb->s_maxbytes;

	        if (unlikely(io->pos >= limit)) {
			pr_debug("maxbytes: %Ld\n", limit);
			ret = 0;
			if (io->size || io->pos > limit)
				ret = -EFBIG;
			goto out_io;
		}

		/* Clamp writes straddling limit. */
		tmp = io->pos + io->size;
		if (unlikely(tmp > (unsigned long long)limit))
			io->size = limit - io->pos;
	}

	if (READ == io->rw) {
		pr_debug("io->pos=%Ld i_size=%Ld\n", io->pos, inode->i_size);

		if (io->pos > inode->i_size)
			io->size = 0;
		else if ((io->pos + io->size) > inode->i_size) {
			size_t size = inode->i_size - io->pos;
			if (size < io->size)
				io->size = size;
		}

		pr_debug("io->size=%d\n", io->size);
	}

	ret = 0;
	if (unlikely(!io->size))
		goto out_io;

	index = io->pos >> PAGE_CACHE_SHIFT;
	eindex = (io->pos + io->size - 1) >> PAGE_CACHE_SHIFT;
	nr_pages = eindex - index + 1;

	pr_debug("nr_pages: %lu\n", nr_pages);

	io->good_idx = -1;
	io->good_page = NULL;
	io->did_read = 0;
	io->err = 0;
	io->as = as;
	io->offset = (unsigned long)io->pos & (PAGE_CACHE_SIZE - 1);
	kvec_dst_init(&io->src, KM_USER0);
	kvec_dst_set(&io->src, io->cb.vec->veclet);
	io->veclet = io->cb.vec->veclet;
	if (nr_pages < READDESC_NR_DEF) {
		io->pages = io->def_pages;
		io->new_pages = io->def_new_pages;
	} else {
		io->pages = kmalloc(sizeof(*io->pages) * (nr_pages + 1), GFP_KERNEL);
		if (!io->pages)
			goto out_io;

		io->new_pages = kmalloc(sizeof(*io->new_pages) * (nr_pages + 1), GFP_KERNEL);
		if (!io->new_pages)
			goto out_pages;
	}

	ret = address_space_map(as, index, nr_pages, io->pages,
			&io->nr_new_pages, io->new_pages);
	pr_debug("as_map: %d (%d new)\n", ret, io->nr_new_pages);
	if (ret <= 0)
		goto out_new_pages;

	io->nr_pages = ret;
	io->pages[io->nr_pages] = NULL;
	io->new_pages[io->nr_new_pages] = NULL;

	if (io->rw == READ)
		__generic_file_read_iodesc(io, 0);
	else if (io->rw == WRITE)
		__generic_file_write_iodesc(io);

	return 0;

out_new_pages:
	if (io->new_pages != io->def_new_pages)
		kfree(io->new_pages);
out_pages:
	if (io->pages != io->def_pages)
		kfree(io->pages);
out_io:
	if (io->rw == WRITE)
		up(&inode->i_sem);
	if (!ret)
		io->cb.fn(io->cb.data, io->cb.vec, ret);
	kfree(io);
	return ret;
}
