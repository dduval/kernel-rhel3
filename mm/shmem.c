/*
 * Resizable virtual memory filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000-2001 Christoph Rohland
 *		 2000-2001 SAP AG
 *		 2002 Red Hat Inc.
 * TODO:
 *	Accounting needs verification
 *	We need to verify all the security fixes from generic_file_write are
 *		now there
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * This virtual memory filesystem is heavily based on the ramfs. It
 * extends ramfs by the ability to use swap and honor resource limits
 * which makes it a completely usable filesystem.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/* This magic number is used in glibc for posix shared memory */
#define TMPFS_MAGIC	0x01021994

#define ENTRIES_PER_PAGE (PAGE_CACHE_SIZE/sizeof(unsigned long))
#define BLOCKS_PER_PAGE  (PAGE_CACHE_SIZE/512)

#define SHMEM_MAX_INDEX  (SHMEM_NR_DIRECT + ENTRIES_PER_PAGE * (ENTRIES_PER_PAGE/2) * (ENTRIES_PER_PAGE+1))
#define SHMEM_MAX_BYTES  ((unsigned long long)SHMEM_MAX_INDEX << PAGE_CACHE_SHIFT)
#define VM_ACCT(size)    (((size) + PAGE_CACHE_SIZE - 1) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

#define SHMEM_SB(sb) (&sb->u.shmem_sb)

static struct super_operations shmem_ops;
static struct address_space_operations shmem_aops;
static struct file_operations shmem_file_operations;
static struct inode_operations shmem_inode_operations;
static struct inode_operations shmem_dir_inode_operations;
static struct vm_operations_struct shmem_vm_ops;

LIST_HEAD (shmem_inodes);
static spinlock_t shmem_ilock = SPIN_LOCK_UNLOCKED;
atomic_t shmem_nrpages = ATOMIC_INIT(0);

static struct page *shmem_getpage_locked(struct shmem_inode_info *, struct inode *, unsigned long);

static void shmem_removepage(struct page *page)
{
	struct inode * inode;
	struct shmem_sb_info * sbinfo;

	atomic_dec(&shmem_nrpages);
	if (PageLaunder(page))
		return;

	inode = page->mapping->host;
	sbinfo = SHMEM_SB(inode->i_sb);
	inode->i_blocks -= BLOCKS_PER_PAGE;
	spin_lock (&sbinfo->stat_lock);
	sbinfo->free_blocks++;
	spin_unlock (&sbinfo->stat_lock);
}

/*
 * shmem_swp_entry - find the swap vector position in the info structure
 *
 * @info:  info structure for the inode
 * @index: index of the page to find
 * @page:  optional page to add to the structure. Has to be preset to
 *         all zeros
 *
 * If there is no space allocated yet it will return -ENOMEM when
 * page == 0 else it will use the page for the needed block.
 *
 * returns -EFBIG if the index is too big.
 *
 *
 * The swap vector is organized the following way:
 *
 * There are SHMEM_NR_DIRECT entries directly stored in the
 * shmem_inode_info structure. So small files do not need an addional
 * allocation.
 *
 * For pages with index > SHMEM_NR_DIRECT there is the pointer
 * i_indirect which points to a page which holds in the first half
 * doubly indirect blocks, in the second half triple indirect blocks:
 *
 * For an artificial ENTRIES_PER_PAGE = 4 this would lead to the
 * following layout (for SHMEM_NR_DIRECT == 16):
 *
 * i_indirect -> dir --> 16-19
 * 	      |	     +-> 20-23
 * 	      |
 * 	      +-->dir2 --> 24-27
 * 	      |	       +-> 28-31
 * 	      |	       +-> 32-35
 * 	      |	       +-> 36-39
 * 	      |
 * 	      +-->dir3 --> 40-43
 * 	       	       +-> 44-47
 * 	      	       +-> 48-51
 * 	      	       +-> 52-55
 */
static swp_entry_t * shmem_swp_entry (struct shmem_inode_info *info, unsigned long index, unsigned long page) 
{
	unsigned long offset;
	void **dir;

	if (index < SHMEM_NR_DIRECT)
		return info->i_direct+index;

	index -= SHMEM_NR_DIRECT;
	offset = index % ENTRIES_PER_PAGE;
	index /= ENTRIES_PER_PAGE;

	if (!info->i_indirect) {
		info->i_indirect = (void *) page;
		return ERR_PTR(-ENOMEM);
	}

	dir = info->i_indirect + index;
	if (index >= ENTRIES_PER_PAGE/2) {
		index -= ENTRIES_PER_PAGE/2;
		dir = info->i_indirect + ENTRIES_PER_PAGE/2 
			+ index/ENTRIES_PER_PAGE;
		index %= ENTRIES_PER_PAGE;

		if(!*dir) {
			*dir = (void *) page;
			/* We return since we will need another page
                           in the next step */
			return ERR_PTR(-ENOMEM);
		}
		dir = ((void **)*dir) + index;
	}
	if (!*dir) {
		if (!page)
			return ERR_PTR(-ENOMEM);
		*dir = (void *)page;
	}
	return ((swp_entry_t *)*dir) + offset;
}

/*
 * shmem_alloc_entry - get the position of the swap entry for the
 *                     page. If it does not exist allocate the entry
 *
 * @info:	info structure for the inode
 * @index:	index of the page to find
 */
static inline swp_entry_t * shmem_alloc_entry (struct shmem_inode_info *info, unsigned long index)
{
	unsigned long page = 0;
	swp_entry_t * res;

	if (index >= SHMEM_MAX_INDEX)
		return ERR_PTR(-EFBIG);

	if (info->next_index <= index)
		info->next_index = index + 1;

	while ((res = shmem_swp_entry(info,index,page)) == ERR_PTR(-ENOMEM)) {
		page = get_zeroed_page(GFP_USER);
		if (!page)
			break;
	}
	return res;
}

/*
 * shmem_free_swp - free some swap entries in a directory
 *
 * @dir:   pointer to the directory
 * @count: number of entries to scan
 */
static int shmem_free_swp(swp_entry_t *dir, unsigned int count)
{
	swp_entry_t *ptr, entry;
	int freed = 0;

	for (ptr = dir; ptr < dir + count; ptr++) {
		if (!ptr->val)
			continue;
		entry = *ptr;
		*ptr = (swp_entry_t){0};
		freed++;
		free_swap_and_cache(entry);
	}
	return freed;
}

/*
 * shmem_truncate_direct - free the swap entries of a whole doubly
 *                         indirect block
 *
 * @dir:	pointer to the pointer to the block
 * @start:	offset to start from (in pages)
 * @len:	how many pages are stored in this block
 *
 * Returns the number of freed swap entries.
 */

static inline unsigned long 
shmem_truncate_direct(swp_entry_t *** dir, unsigned long start, unsigned long len) {
	swp_entry_t **last, **ptr;
	unsigned long off, freed = 0;
 
	if (!*dir)
		return 0;

	last = *dir + (len + ENTRIES_PER_PAGE-1) / ENTRIES_PER_PAGE;
	off = start % ENTRIES_PER_PAGE;

	for (ptr = *dir + start/ENTRIES_PER_PAGE; ptr < last; ptr++) {
		if (!*ptr) {
			off = 0;
			continue;
		}

		if (!off) {
			freed += shmem_free_swp(*ptr, ENTRIES_PER_PAGE);
			free_page ((unsigned long) *ptr);
			*ptr = 0;
		} else {
			freed += shmem_free_swp(*ptr+off,ENTRIES_PER_PAGE-off);
			off = 0;
		}
	}
	
	if (!start) {
		free_page((unsigned long) *dir);
		*dir = 0;
	}
	return freed;
}

/*
 * shmem_truncate_indirect - truncate an inode
 *
 * @info:  the info structure of the inode
 * @index: the index to truncate
 *
 * This function locates the last doubly indirect block and calls
 * then shmem_truncate_direct to do the real work
 */
static inline unsigned long
shmem_truncate_indirect(struct shmem_inode_info *info, unsigned long index)
{
	swp_entry_t ***base;
	unsigned long baseidx, len, start;
	unsigned long max = info->next_index-1;

	if (max < SHMEM_NR_DIRECT) {
		info->next_index = index;
		return shmem_free_swp(info->i_direct + index,
				      SHMEM_NR_DIRECT - index);
	}

	if (max < ENTRIES_PER_PAGE * ENTRIES_PER_PAGE/2 + SHMEM_NR_DIRECT) {
		max -= SHMEM_NR_DIRECT;
		base = (swp_entry_t ***) &info->i_indirect;
		baseidx = SHMEM_NR_DIRECT;
		len = max+1;
	} else {
		max -= ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2+SHMEM_NR_DIRECT;
		if (max >= ENTRIES_PER_PAGE*ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2)
			BUG();

		baseidx = max & ~(ENTRIES_PER_PAGE*ENTRIES_PER_PAGE-1);
		base = (swp_entry_t ***) info->i_indirect + ENTRIES_PER_PAGE/2 + baseidx/ENTRIES_PER_PAGE/ENTRIES_PER_PAGE ;
		len = max - baseidx + 1;
		baseidx += ENTRIES_PER_PAGE*ENTRIES_PER_PAGE/2+SHMEM_NR_DIRECT;
	}

	if (index > baseidx) {
		info->next_index = index;
		start = index - baseidx;
	} else {
		info->next_index = baseidx;
		start = 0;
	}
	return shmem_truncate_direct(base, start, len);
}

static void shmem_truncate (struct inode * inode)
{
	unsigned long index;
	unsigned long partial;
	unsigned long freed = 0;
	struct shmem_inode_info * info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	down(&info->sem);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	spin_lock (&info->lock);
	index = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	partial = inode->i_size & ~PAGE_CACHE_MASK;

	if (partial) {
		swp_entry_t *entry = shmem_swp_entry(info, index-1, 0);
		struct page *page;
		/*
		 * This check is racy: it's faintly possible that page
		 * was assigned to swap during truncate_inode_pages,
		 * and now assigned to file; but better than nothing.
		 */
		if (!IS_ERR(entry) && entry->val) {
			spin_unlock(&info->lock);
			page = shmem_getpage_locked(info, inode, index-1);
			if (!IS_ERR(page)) {
				memclear_highpage_flush(page, partial,
					PAGE_CACHE_SIZE - partial);
				UnlockPage(page);
				page_cache_release(page);
			}
			spin_lock(&info->lock);
		}
	}

	while (index < info->next_index) 
		freed += shmem_truncate_indirect(info, index);

	info->swapped -= freed;
	inode->i_blocks -= freed*BLOCKS_PER_PAGE;
	spin_unlock (&info->lock);
	spin_lock (&sbinfo->stat_lock);
	sbinfo->free_blocks += freed;
	spin_unlock (&sbinfo->stat_lock);
	up(&info->sem);
}

static int shmem_notify_change(struct dentry * dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	long change = 0;
	int error;

	if ((attr->ia_valid & ATTR_SIZE) && (attr->ia_size <= SHMEM_MAX_BYTES)) {
		/*
	 	 * Account swap file usage based on new file size,
		 * but just let vmtruncate fail on out-of-range sizes.
	 	 */
		change = VM_ACCT(attr->ia_size) - VM_ACCT(inode->i_size);
		if (change > 0) {
			if (!vm_enough_memory(change))
				return -ENOMEM;
		} else
			vm_unacct_memory(-change);

	}
	error = inode_change_ok(inode, attr);
	if (!error)
		error = inode_setattr(inode, attr);
	if (error && change)
		vm_unacct_memory(change);
	return error;
}

static void shmem_delete_inode(struct inode * inode)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	if (inode->i_op->truncate == shmem_truncate) {
		spin_lock (&shmem_ilock);
		list_del (&SHMEM_I(inode)->list);
		spin_unlock (&shmem_ilock);
		vm_unacct_memory(VM_ACCT(inode->i_size));
		inode->i_size = 0;
		shmem_truncate (inode);
	}
	spin_lock (&sbinfo->stat_lock);
	sbinfo->free_inodes++;
	spin_unlock (&sbinfo->stat_lock);
	clear_inode(inode);
}

static inline int shmem_find_swp(swp_entry_t entry, swp_entry_t *ptr, swp_entry_t *eptr)
{
	swp_entry_t *test;

	for (test = ptr; test < eptr; test++) {
		if (test->val == entry.val)
			return test - ptr;
	}
	return -1;
}

static int shmem_unuse_inode(struct shmem_inode_info *info, swp_entry_t entry, struct page *page)
{
	swp_entry_t *ptr;
	unsigned long idx;
	int offset;

	idx = 0;
	ptr = info->i_direct;
	spin_lock (&info->lock);
	offset = info->next_index;
	if (offset > SHMEM_NR_DIRECT)
		offset = SHMEM_NR_DIRECT;
	offset = shmem_find_swp(entry, ptr, ptr + offset);
	if (offset >= 0)
		goto found;

	for (idx = SHMEM_NR_DIRECT; idx < info->next_index; 
	     idx += ENTRIES_PER_PAGE) {
		ptr = shmem_swp_entry(info, idx, 0);
		if (IS_ERR(ptr))
			continue;
		offset = info->next_index - idx;
		if (offset > ENTRIES_PER_PAGE)
			offset = ENTRIES_PER_PAGE;
		offset = shmem_find_swp(entry, ptr, ptr + offset);
		if (offset >= 0)
			goto found;
	}
	spin_unlock (&info->lock);
	return 0;
found:
	swap_free(entry);
	ptr[offset] = (swp_entry_t) {0};
	delete_from_swap_cache(page);
	atomic_inc(&shmem_nrpages);
	add_to_page_cache(page, info->inode->i_mapping, offset + idx);
	SetPageDirty(page);
	SetPageUptodate(page);
	info->swapped--;
	spin_unlock(&info->lock);
	return 1;
}

/*
 * shmem_unuse() search for an eventually swapped out shmem page.
 */
void shmem_unuse(swp_entry_t entry, struct page *page)
{
	struct list_head *p;
	struct shmem_inode_info * info;

	spin_lock (&shmem_ilock);
	list_for_each(p, &shmem_inodes) {
		info = list_entry(p, struct shmem_inode_info, list);

		if (info->swapped && shmem_unuse_inode(info, entry, page)) {
			/* move head to start search for next from here */
			list_del(&shmem_inodes);
			list_add_tail(&shmem_inodes, p);
			break;
		}
	}
	spin_unlock (&shmem_ilock);
}

/*
 * Move the page from the page cache to the swap cache.
 *
 * The page lock prevents multiple occurences of shmem_writepage at
 * once.  We still need to guard against racing with
 * shmem_getpage_locked().  
 */
int shmem_writepage(struct page * page)
{
	struct shmem_inode_info *info;
	swp_entry_t *entry, swap;
	struct address_space *mapping;
	unsigned long index;
	struct inode *inode;

	if (!PageLocked(page))
		BUG();
	if (!PageLaunder(page))
		return fail_writepage(page);

	mapping = page->mapping;
	index = page->index;
	inode = mapping->host;
	info = SHMEM_I(inode);
	if (info->locked)
		return fail_writepage(page);
getswap:
	swap = get_swap_page();
	if (!swap.val)
		return fail_writepage(page);

	spin_lock(&info->lock);
	entry = shmem_swp_entry(info, index, 0);
	if (IS_ERR(entry))	/* this had been allocated on page allocation */
		BUG();
	if (entry->val)
		BUG();

	/* Remove it from the page cache */
	remove_inode_page(page);
	page_cache_release(page);

	/* Add it to the swap cache */
	if (add_to_swap_cache(page, swap) != 0) {
		/*
		 * Raced with "speculative" read_swap_cache_async.
		 * Add page back to page cache, unref swap, try again.
		 */
		atomic_inc(&shmem_nrpages);
		add_to_page_cache_locked(page, mapping, index);
		spin_unlock(&info->lock);
		swap_free(swap);
		goto getswap;
	}

	*entry = swap;
	info->swapped++;
	spin_unlock(&info->lock);
	SetPageUptodate(page);
	set_page_dirty(page);
	UnlockPage(page);
	return 0;
}

/*
 * shmem_getpage_locked - either get the page from swap or allocate a new one
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 *
 * Called with the inode locked, so it cannot race with itself, but we
 * still need to guard against racing with shm_writepage(), which might
 * be trying to move the page to the swap cache as we run.
 */
static struct page * shmem_getpage_locked(struct shmem_inode_info *info, struct inode * inode, unsigned long idx)
{
	struct address_space * mapping = inode->i_mapping;
	struct shmem_sb_info *sbinfo;
	struct page * page;
	swp_entry_t *entry;

repeat:
	page = find_lock_page(mapping, idx);
	if (page)
		return page;

	entry = shmem_alloc_entry (info, idx);
	if (IS_ERR(entry))
		return (void *)entry;

	spin_lock (&info->lock);
	
	/* The shmem_alloc_entry() call may have blocked, and
	 * shmem_writepage may have been moving a page between the page
	 * cache and swap cache.  We need to recheck the page cache
	 * under the protection of the info->lock spinlock. */

	page = find_get_page(mapping, idx);
	if (page) {
		if (TryLockPage(page))
			goto wait_retry;
		spin_unlock (&info->lock);
		return page;
	}
	
	if (entry->val) {
		/* Look it up and read it in.. */
		page = lookup_swap_cache(*entry);
		if (!page) {
			swp_entry_t swap = *entry;
			spin_unlock (&info->lock);
			swapin_readahead(*entry);
			page = read_swap_cache_async(*entry);
			if (!page) {
				if (entry->val != swap.val)
					goto repeat;
				return ERR_PTR(-ENOMEM);
			}
			wait_on_page(page);
			if (!Page_Uptodate(page) && entry->val == swap.val) {
				page_cache_release(page);
				return ERR_PTR(-EIO);
			}
			
			/* Too bad we can't trust this page, because we
			 * dropped the info->lock spinlock */
			page_cache_release(page);
			goto repeat;
		}

		/* We have to this with page locked to prevent races */
		if (TryLockPage(page)) 
			goto wait_retry;

		swap_free(*entry);
		*entry = (swp_entry_t) {0};
		delete_from_swap_cache(page);

		ClearPageUptodate(page);
		ClearPageError(page);
		ClearPageReferenced(page);
		ClearPageArch1(page);
		SetPageDirty(page);
		add_to_page_cache_locked(page, mapping, idx);
		info->swapped--;
		spin_unlock (&info->lock);
	} else {
		sbinfo = SHMEM_SB(inode->i_sb);
		spin_unlock (&info->lock);
		spin_lock (&sbinfo->stat_lock);
		if (sbinfo->free_blocks == 0)
			goto no_space;
		sbinfo->free_blocks--;
		spin_unlock (&sbinfo->stat_lock);

		/* Ok, get a new page.  We don't have to worry about the
		 * info->lock spinlock here: we cannot race against
		 * shm_writepage because we have already verified that
		 * there is no page present either in memory or in the
		 * swap cache, so we are guaranteed to be populating a
		 * new shm entry.  The inode semaphore we already hold
		 * is enough to make this atomic. */
		page = page_cache_alloc(mapping);
		if (!page)
			return ERR_PTR(-ENOMEM);
		clear_highpage(page);
		flush_dcache_page(page);
		inode->i_blocks += BLOCKS_PER_PAGE;
		add_to_page_cache (page, mapping, idx);
	}

	/* We have the page */
	SetPageUptodate(page);
	atomic_inc(&shmem_nrpages);
	return page;
no_space:
	spin_unlock (&sbinfo->stat_lock);
	return ERR_PTR(-ENOSPC);

wait_retry:
	spin_unlock (&info->lock);
	wait_on_page(page);
	page_cache_release(page);
	goto repeat;
}

static int shmem_getpage(struct inode * inode, unsigned long idx, struct page **ptr)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;

	down (&info->sem);
	*ptr = ERR_PTR(-EFAULT);
	if (inode->i_size <= (loff_t) idx * PAGE_CACHE_SIZE)
		goto failed;

	*ptr = shmem_getpage_locked(info, inode, idx);
	if (IS_ERR (*ptr))
		goto failed;

	UnlockPage(*ptr);
	up (&info->sem);
	return 0;
failed:
	up (&info->sem);
	error = PTR_ERR(*ptr);
	*ptr = NOPAGE_SIGBUS;
	if (error == -ENOMEM)
		*ptr = NOPAGE_OOM;
	return error;
}

struct page * shmem_nopage(struct vm_area_struct * vma, unsigned long address, int unused)
{
	struct page * page;
	unsigned int idx;
	struct inode * inode = vma->vm_file->f_dentry->d_inode;

	idx = (address - vma->vm_start) >> PAGE_CACHE_SHIFT;
	idx += vma->vm_pgoff;

	if (shmem_getpage(inode, idx, &page))
		return page;

	flush_page_to_ram(page);
	return(page);
}

static int shmem_populate(struct vm_area_struct *vma,
	unsigned long addr, unsigned long len,
	pgprot_t prot, unsigned long pgoff, int nonblock)
{
	struct inode *inode = vma->vm_file->f_dentry->d_inode;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long size;

	size = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (pgoff >= size || pgoff + (len >> PAGE_SHIFT) > size)
		return -EINVAL;
	
	vma->vm_flags |= VM_NO_UNLOCK;

	while ((long) len > 0) {
		struct page *page = NULL;
		int err;
		/*
		 * Will need changing if PAGE_CACHE_SIZE != PAGE_SIZE
		 */
		err = shmem_getpage(inode, pgoff, &page);
		if (err)
			return err;
		if (page) {
			mark_page_accessed(page);
			err = install_page(mm, vma, addr, page, prot);
			if (err) {
				page_cache_release(page);
				return err;
			}
		}
		len -= PAGE_SIZE;
		addr += PAGE_SIZE;
		pgoff++;
	}
	return 0;
}

int shmem_lock(struct file *file, int lock,
		struct mm_struct **locker_mm_p, pid_t *locker_pid_p)
{
	struct inode * inode = file->f_dentry->d_inode;
	struct shmem_inode_info * info = SHMEM_I(inode);
	struct mm_struct *mm = current->mm;
	unsigned long lock_limit, locked;
	int retval = -ENOMEM;

	spin_lock(&info->lock);
	if (lock && !info->locked) {
		locked = inode->i_size >> PAGE_SHIFT;
		locked += mm->locked_vm;
		lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
		lock_limit >>= PAGE_SHIFT;
		if (locked > num_physpages/10*9)
			goto out_nomem;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			goto out_nomem;
		mm->locked_vm = locked;
		*locker_mm_p = mm;
		*locker_pid_p = current->tgid;
	}
	if (!lock && info->locked && mm) {
		locked = inode->i_size >> PAGE_SHIFT;
		if (mm == *locker_mm_p &&
		    current->tgid == *locker_pid_p &&
		    locked <= mm->locked_vm) {
			mm->locked_vm -= locked;
			*locker_mm_p = NULL;
			*locker_pid_p = 0;
		}
	}
	info->locked = lock;
	retval = 0;
out_nomem:
	spin_unlock(&info->lock);
	return retval;
}

static int shmem_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct vm_operations_struct * ops;
	struct inode *inode = file->f_dentry->d_inode;

	ops = &shmem_vm_ops;
	if (!inode->i_sb || !S_ISREG(inode->i_mode))
		return -EACCES;
	UPDATE_ATIME(inode);
	vma->vm_ops = ops;
	return 0;
}

struct inode *shmem_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode * inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	spin_lock (&sbinfo->stat_lock);
	if (!sbinfo->free_inodes) {
		spin_unlock (&sbinfo->stat_lock);
		return NULL;
	}
	sbinfo->free_inodes--;
	spin_unlock (&sbinfo->stat_lock);

	inode = new_inode(sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		info = SHMEM_I(inode);
		info->inode = inode;
		spin_lock_init (&info->lock);
		sema_init (&info->sem, 1);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			spin_lock (&shmem_ilock);
			list_add_tail(&info->list, &shmem_inodes);
			spin_unlock (&shmem_ilock);
			break;
		case S_IFDIR:
			inode->i_nlink++;
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * BOGO_DIRENT_SIZE;
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &dcache_dir_ops;
			break;
		case S_IFLNK:
			break;
		}
	}
	return inode;
}

static int shmem_set_size(struct shmem_sb_info *info,
			  unsigned long max_blocks, unsigned long max_inodes)
{
	int error;
	unsigned long blocks, inodes;

	spin_lock(&info->stat_lock);
	blocks = info->max_blocks - info->free_blocks;
	inodes = info->max_inodes - info->free_inodes;
	error = -EINVAL;
	if (max_blocks < blocks)
		goto out;
	if (max_inodes < inodes)
		goto out;
	error = 0;
	info->max_blocks  = max_blocks;
	info->free_blocks = max_blocks - blocks;
	info->max_inodes  = max_inodes;
	info->free_inodes = max_inodes - inodes;
out:
	spin_unlock(&info->stat_lock);
	return error;
}

#ifdef CONFIG_TMPFS

static struct inode_operations shmem_symlink_inode_operations;
static struct inode_operations shmem_symlink_inline_operations;

/*
 *	This is a copy of generic_file_write slightly modified. It would
 *	help no end if it were kept remotely up to date with the
 *	generic_file_write changes. I don't alas see a good way to merge
 *	it back and use the generic one -- Alan
 */
 
static ssize_t
shmem_file_write(struct file *file,const char *buf,size_t count,loff_t *ppos)
{
	struct inode	*inode = file->f_dentry->d_inode; 
	struct shmem_inode_info *info;
	unsigned long	limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	loff_t		pos;
	struct page	*page;
	unsigned long	written;
	long		status;
	ssize_t		err;
	loff_t		maxpos;

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	down(&inode->i_sem);

	pos = *ppos;
	err = -EINVAL;
	if (pos < 0)
		goto out_nc;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out_nc;
	}

	written = 0;

	if (file->f_flags & O_APPEND)
		pos = inode->i_size;

	maxpos = inode->i_size;

	if (pos + count > inode->i_size) {
		maxpos = pos + count;
		if (maxpos > SHMEM_MAX_BYTES)
			maxpos = SHMEM_MAX_BYTES;
		if (!vm_enough_memory(VM_ACCT(maxpos) - VM_ACCT(inode->i_size))) {
			err = -ENOMEM;
			goto out_nc;
		}
	}

	/*
	 * Check whether we've reached the file size limit.
	 */
	err = -EFBIG;
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (pos > 0xFFFFFFFFULL || count > limit - (u32)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			count = limit - (u32)pos;
		}
	}

	/*
	 *	LFS rule 
	 */
	if ( pos + count > MAX_NON_LFS && !(file->f_flags&O_LARGEFILE)) {
		if (pos >= MAX_NON_LFS) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > MAX_NON_LFS - (u32)pos) {
			/* send_sig(SIGXFSZ, current, 0); */
			count = MAX_NON_LFS - (u32)pos;
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
			if (count || pos > inode->i_sb->s_maxbytes) {
				send_sig(SIGXFSZ, current, 0);
				err = -EFBIG;
				goto out;
			}
			/* zero-length writes at ->s_maxbytes are OK */
		}

		if (pos + count > inode->i_sb->s_maxbytes)
			count = inode->i_sb->s_maxbytes - pos;
	} else {
		if (is_read_only(inode->i_rdev)) {
			err = -EPERM;
			goto out;
		}
		if (pos >= inode->i_size) {
			if (count || pos > inode->i_size) {
				err = -ENOSPC;
				goto out;
			}
		}

		if (pos + count > inode->i_size)
			count = inode->i_size - pos;
	}

	err = 0;
	if (count == 0)
		goto out;
	status	= 0;

	remove_suid(inode);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;

	while (count) {
		unsigned long bytes, index, offset;
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

		info = SHMEM_I(inode);
		down (&info->sem);
		page = shmem_getpage_locked(info, inode, index);
		up (&info->sem);

		status = PTR_ERR(page);
		if (IS_ERR(page))
			break;

		/* We have exclusive IO access to the page.. */
		if (!PageLocked(page)) {
			PAGE_BUG(page);
		}

		kaddr = kmap(page);
		status = __copy_from_user(kaddr+offset, buf, bytes);
		kunmap(page);
		if (status)
			goto fail_write;

		flush_dcache_page(page);
		if (bytes > 0) {
			SetPageDirty(page);
			written += bytes;
			count -= bytes;
			pos += bytes;
			buf += bytes;
			if (pos > inode->i_size) 
				inode->i_size = pos;
		}
unlock:
		/* Mark it unlocked again and drop the page.. */
		SetPageReferenced(page);
		UnlockPage(page);
#if USING_RMAP		
		if (deactivate)
			deactivate_page(page);
		else
			mark_page_accessed(page);
#endif			
		page_cache_release(page);

		if (status < 0)
			break;
	}
	*ppos = pos;

	err = written ? written : status;
out:
	/* Short writes give back address space */
	if (inode->i_size != maxpos)
		vm_unacct_memory(VM_ACCT(maxpos) - VM_ACCT(inode->i_size));
out_nc:
	up(&inode->i_sem);
	return err;
fail_write:
	status = -EFAULT;
	ClearPageUptodate(page);
	goto unlock;
}

static void do_shmem_file_read(struct file * filp, loff_t *ppos, read_descriptor_t * desc)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index, offset;
	loff_t pos = *ppos;

	if (unlikely(pos < 0))
		return;

	index = pos >> PAGE_CACHE_SHIFT;
	offset = pos & ~PAGE_CACHE_MASK;

	for (;;) {
		struct page *page;
		unsigned long end_index;
		int nr, ret;

		end_index = inode->i_size >> PAGE_CACHE_SHIFT;
		if (index > end_index)
			break;
		nr = PAGE_CACHE_SIZE;
		if (index == end_index) {
			nr = inode->i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset)
				break;
		}
		nr -= offset;

		desc->error = shmem_getpage(inode, index, &page);
		if (desc->error)
			break;

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
		ret = file_read_actor(desc, page, offset, nr);
		offset += ret;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;
	
		page_cache_release(page);
		if (nr != ret || !desc->count)
			break;
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	UPDATE_ATIME(inode);
}

static ssize_t shmem_file_read(struct file * filp, char * buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = -EFAULT;
	if (access_ok(VERIFY_WRITE, buf, count)) {
		retval = 0;

		if (count) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.count = count;
			desc.buf = buf;
			desc.error = 0;
			do_shmem_file_read(filp, ppos, &desc);

			retval = desc.written;
			if (!retval)
				retval = desc.error;
		}
	}
	return retval;
}

static int shmem_statfs(struct super_block *sb, struct statfs *buf)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	buf->f_type = TMPFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	spin_lock (&sbinfo->stat_lock);
	buf->f_blocks = sbinfo->max_blocks;
	buf->f_bavail = buf->f_bfree = sbinfo->free_blocks;
	buf->f_files = sbinfo->max_inodes;
	buf->f_ffree = sbinfo->free_inodes;
	spin_unlock (&sbinfo->stat_lock);
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.
 */
static struct dentry * shmem_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int shmem_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode * inode = shmem_get_inode(dir->i_sb, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		dir->i_size += BOGO_DIRENT_SIZE;
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int shmem_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	int error;

	if ((error = shmem_mknod(dir, dentry, mode | S_IFDIR, 0)))
		return error;
	dir->i_nlink++;
	return 0;
}

static int shmem_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode * dir, struct dentry * dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	dir->i_size += BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inode->i_nlink++;
	atomic_inc(&inode->i_count);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
	return 0;
}

static inline int shmem_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

/*
 * Check that a directory is empty (this works
 * for regular files too, they'll just always be
 * considered empty..).
 *
 * Note that an empty directory can still have
 * children, they just all have to be negative..
 */
static int shmem_empty(struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);
	list = dentry->d_subdirs.next;

	while (list != &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);

		if (shmem_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
		list = list->next;
	}
	spin_unlock(&dcache_lock);
	return 1;
}

static int shmem_unlink(struct inode * dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inode->i_nlink--;
	dput(dentry);	/* Undo the count from "create" - this does all the work */
	return 0;
}

static int shmem_rmdir(struct inode * dir, struct dentry *dentry)
{
	if (!shmem_empty(dentry))
		return -ENOTEMPTY;

	dir->i_nlink--;
	return shmem_unlink(dir, dentry);
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename(struct inode * old_dir, struct dentry *old_dentry, struct inode * new_dir,struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (!shmem_empty(new_dentry)) 
		return -ENOTEMPTY;

	if (new_dentry->d_inode) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs)
			old_dir->i_nlink--;
	} else if (they_are_dirs) {
		old_dir->i_nlink--;
		new_dir->i_nlink++;
	}

	old_dir->i_size -= BOGO_DIRENT_SIZE;
	new_dir->i_size += BOGO_DIRENT_SIZE;
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	inode->i_ctime = CURRENT_TIME;
	return 0;
}

static int shmem_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	int len;
	struct inode *inode;
	struct page *page;
	char *kaddr;
	struct shmem_inode_info * info;

	len = strlen(symname) + 1;
	if (len > PAGE_CACHE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(dir->i_sb, S_IFLNK|S_IRWXUGO, 0);
	if (!inode)
		return -ENOSPC;

	info = SHMEM_I(inode);
	inode->i_size = len-1;
	if (len <= sizeof(struct shmem_inode_info)) {
		/* do it inline */
		memcpy(info, symname, len);
		inode->i_op = &shmem_symlink_inline_operations;
	} else {
		if (!vm_enough_memory(VM_ACCT(1))) {
			iput(inode);
			return -ENOMEM;
		}
		down(&info->sem);
		page = shmem_getpage_locked(info, inode, 0);
		if (IS_ERR(page)) {
			up(&info->sem);
			vm_unacct_memory(VM_ACCT(1));
			iput(inode);
			return PTR_ERR(page);
		}
		inode->i_op = &shmem_symlink_inode_operations;
		spin_lock (&shmem_ilock);
		list_add_tail(&info->list, &shmem_inodes);
		spin_unlock (&shmem_ilock);
		kaddr = kmap(page);
		memcpy(kaddr, symname, len);
		kunmap(page);
		SetPageDirty(page);
		UnlockPage(page);
		page_cache_release(page);
		up(&info->sem);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
}

static int shmem_readlink_inline(struct dentry *dentry, char *buffer, int buflen)
{
	return vfs_readlink(dentry,buffer,buflen, (const char *)SHMEM_I(dentry->d_inode));
}

static int shmem_follow_link_inline(struct dentry *dentry, struct nameidata *nd)
{
	return vfs_follow_link(nd, (const char *)SHMEM_I(dentry->d_inode));
}

static int shmem_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct page * page;
	int res = shmem_getpage(dentry->d_inode, 0, &page);

	if (res)
		return res;

	res = vfs_readlink(dentry,buffer,buflen, kmap(page));
	kunmap(page);
	page_cache_release(page);
	return res;
}

static int shmem_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct page * page;
	int res = shmem_getpage(dentry->d_inode, 0, &page);
	if (res)
		return res;

	res = vfs_follow_link(nd, kmap(page));
	kunmap(page);
	page_cache_release(page);
	return res;
}

static struct inode_operations shmem_symlink_inline_operations = {
	readlink:	shmem_readlink_inline,
	follow_link:	shmem_follow_link_inline,
};

static struct inode_operations shmem_symlink_inode_operations = {
	truncate:	shmem_truncate,
	readlink:	shmem_readlink,
	follow_link:	shmem_follow_link,
};

static int shmem_parse_options(char *options, int *mode, uid_t *uid, gid_t *gid, unsigned long * blocks, unsigned long *inodes)
{
	char *this_char, *value, *rest;

	this_char = NULL;
	if ( options )
		this_char = strtok(options,",");
	for ( ; this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL) {
			*value++ = 0;
		} else {
			printk(KERN_ERR 
			    "tmpfs: No value for mount option '%s'\n", 
			    this_char);
			return 1;
		}

		if (!strcmp(this_char,"size")) {
			unsigned long long size;
			size = memparse(value,&rest);
			if (*rest)
				goto bad_val;
			*blocks = size >> PAGE_CACHE_SHIFT;
		} else if (!strcmp(this_char,"nr_blocks")) {
			*blocks = memparse(value,&rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"nr_inodes")) {
			*inodes = memparse(value,&rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"mode")) {
			if (!mode)
				continue;
			*mode = simple_strtoul(value,&rest,8);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"uid")) {
			if (!uid)
				continue;
			*uid = simple_strtoul(value,&rest,0);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"gid")) {
			if (!gid)
				continue;
			*gid = simple_strtoul(value,&rest,0);
			if (*rest)
				goto bad_val;
		} else {
			printk(KERN_ERR "tmpfs: Bad mount option %s\n",
			       this_char);
			return 1;
		}
	}
	return 0;

bad_val:
	printk(KERN_ERR "tmpfs: Bad value '%s' for mount option '%s'\n", 
	       value, this_char);
	return 1;

}

static int shmem_remount_fs (struct super_block *sb, int *flags, char *data)
{
	struct shmem_sb_info *sbinfo = &sb->u.shmem_sb;
	unsigned long max_blocks = sbinfo->max_blocks;
	unsigned long max_inodes = sbinfo->max_inodes;

	if (shmem_parse_options (data, NULL, NULL, NULL, &max_blocks, &max_inodes))
		return -EINVAL;
	return shmem_set_size(sbinfo, max_blocks, max_inodes);
}

int shmem_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}
#endif

static struct super_block *shmem_read_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;
	unsigned long blocks, inodes;
	int mode   = S_IRWXUGO | S_ISVTX;
	uid_t uid = current->fsuid;
	gid_t gid = current->fsgid;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	struct sysinfo si;

	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance
	 */
	si_meminfo(&si);
	blocks = inodes = si.totalram / 2;

#ifdef CONFIG_TMPFS
	if (shmem_parse_options (data, &mode, &uid, &gid, &blocks, &inodes))
		return NULL;
#endif

	spin_lock_init (&sbinfo->stat_lock);
	sbinfo->max_blocks = blocks;
	sbinfo->free_blocks = blocks;
	sbinfo->max_inodes = inodes;
	sbinfo->free_inodes = inodes;
	sb->s_maxbytes = SHMEM_MAX_BYTES;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = TMPFS_MAGIC;
	sb->s_op = &shmem_ops;
	inode = shmem_get_inode(sb, S_IFDIR | mode, 0);
	if (!inode)
		return NULL;

	inode->i_uid = uid;
	inode->i_gid = gid;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	return sb;
}



static struct address_space_operations shmem_aops = {
	removepage:	shmem_removepage,
	writepage:	shmem_writepage,
};

static struct file_operations shmem_file_operations = {
	mmap:	shmem_mmap,
#ifdef CONFIG_TMPFS
	read:	shmem_file_read,
	write:	shmem_file_write,
	fsync:	shmem_sync_file,
#endif
};

static struct inode_operations shmem_inode_operations = {
	truncate:	shmem_truncate,
	setattr:	shmem_notify_change,
};

static struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	create:		shmem_create,
	lookup:		shmem_lookup,
	link:		shmem_link,
	unlink:		shmem_unlink,
	symlink:	shmem_symlink,
	mkdir:		shmem_mkdir,
	rmdir:		shmem_rmdir,
	mknod:		shmem_mknod,
	rename:		shmem_rename,
#endif
};

static struct super_operations shmem_ops = {
#ifdef CONFIG_TMPFS
	statfs:		shmem_statfs,
	remount_fs:	shmem_remount_fs,
#endif
	delete_inode:	shmem_delete_inode,
	put_inode:	force_delete,	
};

static struct vm_operations_struct shmem_vm_ops = {
	nopage:	shmem_nopage,
	populate: shmem_populate,
};

#ifdef CONFIG_TMPFS
/* type "shm" will be tagged obsolete in 2.5 */
static DECLARE_FSTYPE(shmem_fs_type, "shm", shmem_read_super, FS_LITTER);
static DECLARE_FSTYPE(tmpfs_fs_type, "tmpfs", shmem_read_super, FS_LITTER);
#else
static DECLARE_FSTYPE(tmpfs_fs_type, "tmpfs", shmem_read_super, FS_LITTER|FS_NOMOUNT);
#endif
static struct vfsmount *shm_mnt;

static int __init init_shmem_fs(void)
{
	int error;
	struct vfsmount * res;

	if ((error = register_filesystem(&tmpfs_fs_type))) {
		printk (KERN_ERR "Could not register tmpfs\n");
		return error;
	}
#ifdef CONFIG_TMPFS
	if ((error = register_filesystem(&shmem_fs_type))) {
		printk (KERN_ERR "Could not register shm fs\n");
		return error;
	}
	devfs_mk_dir (NULL, "shm", NULL);
#endif
	res = kern_mount(&tmpfs_fs_type);
	if (IS_ERR (res)) {
		printk (KERN_ERR "could not kern_mount tmpfs\n");
		unregister_filesystem(&tmpfs_fs_type);
		return PTR_ERR(res);
	}
	shm_mnt = res;

	/* The internal instance should not do size checking */
	if ((error = shmem_set_size(SHMEM_SB(res->mnt_sb), ULONG_MAX, ULONG_MAX)))
		printk (KERN_ERR "could not set limits on internal tmpfs\n");

	return 0;
}

static void __exit exit_shmem_fs(void)
{
#ifdef CONFIG_TMPFS
	unregister_filesystem(&shmem_fs_type);
#endif
	unregister_filesystem(&tmpfs_fs_type);
	mntput(shm_mnt);
}

module_init(init_shmem_fs)
module_exit(exit_shmem_fs)

/*
 * shmem_file_setup - get an unlinked file living in shmem fs
 *
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 *
 */
struct file *shmem_file_setup(char * name, loff_t size)
{
	int error;
	struct file *file;
	struct inode * inode;
	struct dentry *dentry, *root;
	struct qstr this;

	if (size > SHMEM_MAX_BYTES)
		return ERR_PTR(-EINVAL);

	if (!vm_enough_memory(VM_ACCT(size)))
		return ERR_PTR(-ENOMEM);

	error = -ENOMEM;
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	root = shm_mnt->mnt_root;
	dentry = d_alloc(root, &this);
	if (!dentry)
		goto put_memory;

	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto put_dentry;

	error = -ENOSPC;
	inode = shmem_get_inode(root->d_sb, S_IFREG | S_IRWXUGO, 0);
	if (!inode) 
		goto close_file;

	d_instantiate(dentry, inode);
	inode->i_size = size;
	inode->i_nlink = 0;	/* It is unlinked */
	file->f_vfsmnt = mntget(shm_mnt);
	file->f_dentry = dentry;
	file->f_op = &shmem_file_operations;
	file->f_mode = FMODE_WRITE | FMODE_READ;
	return(file);

close_file:
	put_filp(file);
put_dentry:
	dput (dentry);
put_memory:
	vm_unacct_memory(VM_ACCT(size));
	return ERR_PTR(error);	
}

/*
 * shmem_zero_setup - setup a shared anonymous mapping
 *
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;
	
	file = shmem_file_setup("dev/zero", size);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput (vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;
	return 0;
}

EXPORT_SYMBOL(shmem_file_setup);
