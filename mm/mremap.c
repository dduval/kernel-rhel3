/*
 *	linux/mm/remap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 *
 *	Address space accounting code	<alan@redhat.com>
 *	(c) Copyright 2002 Red Hat Inc, All Rights Reserved
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

#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>

static inline pte_t *get_one_pte_map_nested(struct mm_struct *mm, unsigned long addr)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		goto end;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		goto end;
	}

	pmd = pmd_offset(pgd, addr);
	if (pmd_none(*pmd))
		goto end;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto end;
	}

	pte = pte_offset_map_nested(pmd, addr);
	if (pte_none(*pte)) {
		pte_unmap_nested(pte);
		pte = NULL;
	}
end:
	return pte;
}

#ifdef CONFIG_HIGHPTE	/* Save a few cycles on the sane machines */
static inline int page_table_present(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return 0;
	pmd = pmd_offset(pgd, addr);
	return pmd_present(*pmd);
}
#else
#define page_table_present(mm, addr)	(1)
#endif

static inline pte_t *alloc_one_pte_map(struct mm_struct *mm, unsigned long addr)
{
	pmd_t * pmd;
	pte_t * pte = NULL;

	pmd = pmd_alloc(mm, pgd_offset(mm, addr), addr);
	if (pmd)
		pte = pte_alloc_map(mm, pmd, addr);
	return pte;
}

static int copy_one_pte(struct vm_area_struct *vma, pte_t * src, pte_t * dst,
			unsigned long old_addr, unsigned long new_addr,
			struct pte_chain ** pte_chainp)
{
	int error = 0;
	pte_t pte;
	struct page * page = NULL;

	if (pte_present(*src))
		page = pte_page(*src);

	if (!pte_none(*src)) {
		if (page)
			page_remove_rmap(page, src);
		pte = vm_ptep_get_and_clear(vma, old_addr, src);
		if (!dst) {
			/* No dest?  We must put it back. */
			dst = src;
			error++;
		}
		vm_set_pte(vma, new_addr, dst, pte);
		if (page)
			*pte_chainp = page_add_rmap(page, dst, *pte_chainp);
	}
	return error;
}

static int move_one_page(struct vm_area_struct *vma, unsigned long old_addr, unsigned long new_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct pte_chain * pte_chain;
	int error = 0;
	pte_t *src, *dst;

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain)
		return -1;
	spin_lock(&mm->page_table_lock);
	src = get_one_pte_map_nested(mm, old_addr);
	if (src) {
		/*
		 * Look to see whether alloc_one_pte_map needs to perform a
		 * memory allocation.  If it does then we need to drop the
		 * atomic kmap
		 */
		if (!page_table_present(mm, new_addr)) {
			pte_unmap_nested(src);
			src = NULL;
		}
		dst = alloc_one_pte_map(mm, new_addr);
		if (src == NULL)
			src = get_one_pte_map_nested(mm, old_addr);
		if (src) {
			error = copy_one_pte(vma, src, dst, old_addr, new_addr, &pte_chain);
			pte_unmap_nested(src);
		}
		pte_unmap(dst);
	}
	flush_tlb_page(vma, old_addr);
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
	return error;
}

static int move_page_tables(struct vm_area_struct *vma,
	unsigned long new_addr, unsigned long old_addr, unsigned long len)
{
	unsigned long offset = len;

	flush_cache_range(vma, old_addr, old_addr + len);

	/*
	 * This is not the clever way to do this, but we're taking the
	 * easy way out on the assumption that most remappings will be
	 * only a few pages.. This also makes error recovery easier.
	 */
	while (offset) {
		offset -= PAGE_SIZE;
		if (move_one_page(vma, old_addr + offset, new_addr + offset))
			goto oops_we_failed;
	}
	return 0;

	/*
	 * Ok, the move failed because we didn't have enough pages for
	 * the new page table tree. This is unlikely, but we have to
	 * take the possibility into account. In that case we just move
	 * all the pages back (this will work, because we still have
	 * the old page tables)
	 */
oops_we_failed:
	flush_cache_range(vma, new_addr, new_addr + len);
	while ((offset += PAGE_SIZE) < len)
		move_one_page(vma, new_addr + offset, old_addr + offset);
	zap_page_range(vma, new_addr, len);
	return -1;
}

static unsigned long move_vma(struct vm_area_struct * vma,
	unsigned long addr, unsigned long old_len, unsigned long new_len,
	unsigned long new_addr)
{
	struct mm_struct * mm = vma->vm_mm;
	struct vm_area_struct * new_vma, * next, * prev;
	int allocated_vma;


	new_vma = NULL;
	next = find_vma_prev(mm, new_addr, &prev);
	if (next) {
		if (prev && prev->vm_end == new_addr &&
		    can_vma_merge(prev, vma->vm_flags) && !vma->vm_file && !(vma->vm_flags & VM_SHARED)) {
			spin_lock(&mm->page_table_lock);
			prev->vm_end = new_addr + new_len;
			spin_unlock(&mm->page_table_lock);
			new_vma = prev;
			if (next != prev->vm_next)
				BUG();
			if (prev->vm_end == next->vm_start && can_vma_merge(next, prev->vm_flags)) {
				spin_lock(&mm->page_table_lock);
				prev->vm_end = next->vm_end;
				__vma_unlink(mm, next, prev);
				spin_unlock(&mm->page_table_lock);
				if (vma == next)
					vma = prev;
				mm->map_count--;
				kmem_cache_free(vm_area_cachep, next);
			}
		} else if (next->vm_start == new_addr + new_len &&
			   can_vma_merge(next, vma->vm_flags) && !vma->vm_file && !(vma->vm_flags & VM_SHARED)) {
			spin_lock(&mm->page_table_lock);
			next->vm_start = new_addr;
			spin_unlock(&mm->page_table_lock);
			new_vma = next;
		}
	} else {
		prev = find_vma(mm, new_addr-1);
		if (prev && prev->vm_end == new_addr &&
		    can_vma_merge(prev, vma->vm_flags) && !vma->vm_file && !(vma->vm_flags & VM_SHARED)) {
			spin_lock(&mm->page_table_lock);
			prev->vm_end = new_addr + new_len;
			spin_unlock(&mm->page_table_lock);
			new_vma = prev;
		}
	}

	allocated_vma = 0;
	if (!new_vma) {
		new_vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (!new_vma)
			goto out;
		allocated_vma = 1;
	}

	if (!move_page_tables(vma, new_addr, addr, old_len)) {
		if (allocated_vma) {
			*new_vma = *vma;
			new_vma->vm_start = new_addr;
			new_vma->vm_end = new_addr+new_len;
			new_vma->vm_pgoff += (addr - vma->vm_start) >> PAGE_SHIFT;
			new_vma->vm_raend = 0;
			if (new_vma->vm_file)
				get_file(new_vma->vm_file);
			if (new_vma->vm_ops && new_vma->vm_ops->open)
				new_vma->vm_ops->open(new_vma);
			insert_vm_struct(current->mm, new_vma);
		}
		/* The old VMA has been accounted for, don't double account */
		do_munmap(current->mm, addr, old_len, 0);
		current->mm->total_vm += new_len >> PAGE_SHIFT;
		if (new_vma->vm_flags & VM_LOCKED) {
			current->mm->locked_vm += new_len >> PAGE_SHIFT;
			make_pages_present(new_vma->vm_start,
					   new_vma->vm_end);
		}
		return new_addr;
	}
	if (allocated_vma)
		kmem_cache_free(vm_area_cachep, new_vma);
 out:
	return -ENOMEM;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * MREMAP_FIXED option added 5-Dec-1999 by Benjamin LaHaise
 * This option implies MREMAP_MAYMOVE.
 */
unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	unsigned long charged = 0;

	if (flags & ~(MREMAP_FIXED | MREMAP_MAYMOVE))
		goto out;

	if (addr & ~PAGE_MASK)
		goto out;

	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);

	/* new_addr is only valid if MREMAP_FIXED is specified */
	if (flags & MREMAP_FIXED) {
		if (new_addr & ~PAGE_MASK)
			goto out;
		if (!(flags & MREMAP_MAYMOVE))
			goto out;

		if (new_len > TASK_SIZE || new_addr > TASK_SIZE - new_len)
			goto out;
		/*
		 * Allow new_len == 0 only if new_addr == addr
		 * to preserve truncation in place (that was working
		 * safe and some app may depend on it).
		 */
		if (unlikely(!new_len && new_addr != addr))
			goto out;

		/* Check if the location we're moving into overlaps the
		 * old location at all, and fail if it does.
		 */
		if ((new_addr <= addr) && (new_addr+new_len) > addr)
			goto out;

		if ((addr <= new_addr) && (addr+old_len) > new_addr)
			goto out;

		/* Ensure a non-privileged process is not trying to map
		 * lower pages.
		 */
		if (new_addr < mmap_min_addr && !capable(CAP_SYS_RAWIO))
			return -EPERM;
		
		ret = do_munmap(current->mm, new_addr, new_len, 1);
		if (ret && new_len)
			goto out;
	}

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 * do_munmap does all the needed commit accounting
	 */
	if (old_len >= new_len) {
		ret = do_munmap(current->mm, addr+new_len, old_len-new_len, 1);
		if (ret && old_len != new_len)
			goto out;
		ret = addr;
		if (!(flags & MREMAP_FIXED) || (new_addr == addr))
			goto out;
		old_len = new_len;
	}

	/*
	 * Ok, we need to grow..  or relocate.
	 */
	ret = -EFAULT;
	vma = find_vma(current->mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	if (is_vm_hugetlb_page(vma)) {
		ret = -EINVAL;
		goto out;
	}

	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		goto out;
	if (vma->vm_flags & VM_DONTEXPAND) {
		if (new_len > old_len)
			goto out;
	}
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked, lock_limit;
		locked = current->mm->locked_vm << PAGE_SHIFT;
		lock_limit = current->rlim[RLIMIT_MEMLOCK].rlim_cur;
		locked += new_len - old_len;
		ret = -EAGAIN;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			goto out;
	}
	ret = -ENOMEM;
	if ((current->mm->total_vm << PAGE_SHIFT) + (new_len - old_len)
	    > current->rlim[RLIMIT_AS].rlim_cur)
		goto out;

	if (vma->vm_flags & VM_ACCOUNT) {
		charged = (new_len - old_len) >> PAGE_SHIFT;
		if(!vm_enough_memory(charged))
			goto out_nc;
	}

	/* old_len exactly to the end of the area..
	 * And we're not relocating the area.
	 */
	if (old_len == vma->vm_end - addr &&
	    !((flags & MREMAP_FIXED) && (addr != new_addr)) &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;
			spin_lock(&vma->vm_mm->page_table_lock);
			vma->vm_end = addr + new_len;
			spin_unlock(&vma->vm_mm->page_table_lock);
			current->mm->total_vm += pages;
			if (vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += pages;
				make_pages_present(addr + old_len,
						   addr + new_len);
			}
			ret = addr;
			vm_validate_enough("mremap path1");
			goto out;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	ret = -ENOMEM;
	if (flags & MREMAP_MAYMOVE) {
		if (!(flags & MREMAP_FIXED)) {
			unsigned long map_flags = 0;
			if (vma->vm_flags & VM_SHARED)
				map_flags |= MAP_SHARED;

			new_addr = get_unmapped_area(vma->vm_file, 0, new_len, vma->vm_pgoff, map_flags, vma->vm_flags & VM_EXEC);
			ret = new_addr;
			if (new_addr & ~PAGE_MASK)
				goto out;
		}
		ret = move_vma(vma, addr, old_len, new_len, new_addr);
	}
out:
	if(ret & ~PAGE_MASK)
	{
		vm_unacct_memory(charged);
		vm_validate_enough("mremap error path");
	}
out_nc:
	return ret;
}

asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	unsigned long ret;

	vm_validate_enough("entry to mremap");
	down_write(&current->mm->mmap_sem);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	up_write(&current->mm->mmap_sem);
	vm_validate_enough("exit from mremap");
	return ret;
}
