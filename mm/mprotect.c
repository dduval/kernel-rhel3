/*
 *	linux/mm/mprotect.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 *
 *  Address space accounting code	<alan@redhat.com>
 *  (c) Copyright 2002 Red Hat Inc, All Rights Reserved
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
#include <linux/highmem.h>
#include <linux/hugetlb.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

static inline void change_pte_range(struct vm_area_struct *vma,
	pmd_t * pmd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pte_t *pte, *mapping;
	unsigned long end, start;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return;
	}
	mapping = pte = pte_offset_map(pmd, address);
	start = address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		if (pte_present(*pte)) {
			pte_t entry;

			/* Avoid an SMP race with hardware updated dirty/clean
			 * bits by wiping the pte and then setting the new pte
			 * into place.
			 */
			entry = vm_ptep_get_and_clear(vma, address + start, pte);
			vm_set_pte(vma, address + start,
				   pte, pte_modify(entry, newprot));
			update_mmu_cache(vma, address + start, entry);
		}
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	pte_unmap(mapping);
}

static inline void change_pmd_range(struct vm_area_struct *vma,
	pgd_t * pgd, unsigned long address,
	unsigned long size, pgprot_t newprot)
{
	pmd_t * pmd;
	unsigned long end, start;

	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		return;
	}
	pmd = pmd_offset(pgd, address);
	start = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		change_pte_range(vma, pmd, start + address, end - address, newprot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
}

static void change_protection(struct vm_area_struct *vma, unsigned long start, unsigned long end, pgprot_t newprot)
{
	pgd_t *dir;
	unsigned long beg = start;

	dir = pgd_offset(current->mm, start);
	flush_cache_range(vma, beg, end);
	if (start >= end)
		BUG();
	spin_lock(&current->mm->page_table_lock);
	do {
		change_pmd_range(vma, dir, start, end - start, newprot);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (start && (start < end));
	spin_unlock(&current->mm->page_table_lock);
	flush_tlb_range(vma, beg, end);
}

static inline int mprotect_fixup_all(struct vm_area_struct ** pvma, struct vm_area_struct ** pprev,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * prev = *pprev;
	struct vm_area_struct * vma = *pvma;
	struct mm_struct * mm = vma->vm_mm;
	int oldflags;

	if (prev && prev->vm_end == vma->vm_start && can_vma_merge(prev, newflags) &&
	    !vma->vm_file && !(vma->vm_flags & VM_SHARED)) {
		spin_lock(&mm->page_table_lock);
		prev->vm_end = vma->vm_end;
		__vma_unlink(mm, vma, prev);
		spin_unlock(&mm->page_table_lock);

		kmem_cache_free(vm_area_cachep, vma);
		mm->map_count--;
		*pvma = prev;

		return 0;
	}

	spin_lock(&mm->page_table_lock);
	oldflags = vma->vm_flags;
	vma->vm_flags = newflags;
	vma->vm_page_prot = prot;
	if (oldflags & VM_EXEC)
		arch_remove_exec_range(current->mm, vma->vm_end);
	spin_unlock(&mm->page_table_lock);

	*pprev = vma;

	return 0;
}

static inline int mprotect_fixup_start(struct vm_area_struct * vma, struct vm_area_struct ** pprev,
	unsigned long end,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * n, * prev = *pprev;
	unsigned long prev_end;

	*pprev = vma;

	if (prev && prev->vm_end == vma->vm_start && can_vma_merge(prev, newflags) &&
	    !vma->vm_file && !(vma->vm_flags & VM_SHARED)) {
		spin_lock(&vma->vm_mm->page_table_lock);
		prev_end = prev->vm_end;
		prev->vm_end = end;
		if (prev->vm_flags & VM_EXEC)
			arch_remove_exec_range(current->mm, prev_end);
		vma->vm_start = end;
		spin_unlock(&vma->vm_mm->page_table_lock);

		return 0;
	}
	n = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!n)
		return -ENOMEM;
	*n = *vma;
	n->vm_end = end;
	n->vm_flags = newflags;
	n->vm_raend = 0;
	n->vm_page_prot = prot;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	vma->vm_pgoff += (end - vma->vm_start) >> PAGE_SHIFT;
	lock_vma_mappings(vma);
	spin_lock(&vma->vm_mm->page_table_lock);
	vma->vm_start = end;
	__insert_vm_struct(current->mm, n);
	spin_unlock(&vma->vm_mm->page_table_lock);
	unlock_vma_mappings(vma);

	return 0;
}

static inline int mprotect_fixup_end(struct vm_area_struct * vma, struct vm_area_struct ** pprev,
	unsigned long start,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * n;

	n = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	*n = *vma;
	n->vm_start = start;
	n->vm_pgoff += (n->vm_start - vma->vm_start) >> PAGE_SHIFT;
	n->vm_flags = newflags;
	n->vm_raend = 0;
	n->vm_page_prot = prot;
	if (n->vm_file)
		get_file(n->vm_file);
	if (n->vm_ops && n->vm_ops->open)
		n->vm_ops->open(n);
	lock_vma_mappings(vma);
	spin_lock(&vma->vm_mm->page_table_lock);
	vma->vm_end = start;
	__insert_vm_struct(current->mm, n);
	spin_unlock(&vma->vm_mm->page_table_lock);
	unlock_vma_mappings(vma);

	*pprev = n;

	return 0;
}

static inline int mprotect_fixup_middle(struct vm_area_struct * vma, struct vm_area_struct ** pprev,
	unsigned long start, unsigned long end,
	int newflags, pgprot_t prot)
{
	struct vm_area_struct * left, * right;
	unsigned long prev_end;
	int oldflags;

	left = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!left)
		return -ENOMEM;
	right = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!right) {
		kmem_cache_free(vm_area_cachep, left);
		return -ENOMEM;
	}
	*left = *vma;
	*right = *vma;
	left->vm_end = start;
	right->vm_start = end;
	right->vm_pgoff += (right->vm_start - left->vm_start) >> PAGE_SHIFT;
	left->vm_raend = 0;
	right->vm_raend = 0;
	if (vma->vm_file)
		atomic_add(2,&vma->vm_file->f_count);
	if (vma->vm_ops && vma->vm_ops->open) {
		vma->vm_ops->open(left);
		vma->vm_ops->open(right);
	}
	vma->vm_pgoff += (start - vma->vm_start) >> PAGE_SHIFT;
	vma->vm_raend = 0;
	vma->vm_page_prot = prot;
	lock_vma_mappings(vma);
	spin_lock(&vma->vm_mm->page_table_lock);
	oldflags = vma->vm_flags;
	vma->vm_flags = newflags;
	vma->vm_start = start;
	prev_end = vma->vm_end;
	vma->vm_end = end;
	if (!(newflags & VM_EXEC))
		arch_remove_exec_range(current->mm, prev_end);
	__insert_vm_struct(current->mm, left);
	__insert_vm_struct(current->mm, right);
	spin_unlock(&vma->vm_mm->page_table_lock);
	unlock_vma_mappings(vma);

	*pprev = right;

	return 0;
}

static int mprotect_fixup(struct vm_area_struct * vma, struct vm_area_struct ** pprev,
	unsigned long start, unsigned long end, unsigned int newflags)
{
	pgprot_t newprot;
	int error;
	unsigned long charged = 0;

	if (newflags == vma->vm_flags) {
		*pprev = vma;
		return 0;
	}

	/*
	 * If we make a private mapping writable we increase our commit;
	 * but (without finer accounting) cannot reduce our commit if we
	 * make it unwritable again.
	 *
	 * FIXME? We haven't defined a VM_NORESERVE flag, so mprotecting
	 * a MAP_NORESERVE private mapping to writable will now reserve.
	 */
	if ((newflags & VM_WRITE) &&
	    !(vma->vm_flags & (VM_ACCOUNT|VM_WRITE|VM_SHARED))) {
		charged = (end - start) >> PAGE_SHIFT;
		if (!vm_enough_memory(charged))
			return -ENOMEM;
		newflags |= VM_ACCOUNT;
	}
	newprot = protection_map[newflags & 0xf];
	if (start == vma->vm_start) {
		if (end == vma->vm_end)
			error = mprotect_fixup_all(&vma, pprev, newflags, newprot);
		else
			error = mprotect_fixup_start(vma, pprev, end, newflags, newprot);
	} else if (end == vma->vm_end)
		error = mprotect_fixup_end(vma, pprev, start, newflags, newprot);
	else
		error = mprotect_fixup_middle(vma, pprev, start, end, newflags, newprot);
	if (error) {
		vm_unacct_memory(charged);
		return error;
	}
	change_protection(vma, start, end, newprot);
	return 0;
}

#ifndef PROT_GROWSDOWN
#define PROT_GROWSDOWN	0x01000000
#define PROT_GROWSUP	0x02000000
#endif

asmlinkage long sys_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	unsigned long nstart, end, tmp;
	struct vm_area_struct * vma, * next, * prev;
	int error = -EINVAL;
	const int grows = prot & (PROT_GROWSDOWN|PROT_GROWSUP);
	prot &= ~(PROT_GROWSDOWN|PROT_GROWSUP);
	if (grows == (PROT_GROWSDOWN|PROT_GROWSUP)) /* can't be both */
		return -EINVAL;

	vm_validate_enough("entering mprotect");

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = PAGE_ALIGN(len);
	end = start + len;
	if (end < start)
		return -ENOMEM;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	if (end == start)
		return 0;

	down_write(&current->mm->mmap_sem);

	vma = find_vma_prev(current->mm, start, &prev);
	error = -ENOMEM;
	if (!vma)
		goto out;
	if (unlikely (grows & PROT_GROWSDOWN)) {
		if (vma->vm_start >= end)
			goto out;
		start = vma->vm_start;
		error = -EINVAL;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto out;
	}
	else {
		if (vma->vm_start > start)
			goto out;
		if (unlikely(grows & PROT_GROWSUP)) {
			end = vma->vm_end;
			error = -EINVAL;
			if (!(vma->vm_flags & VM_GROWSUP))
				goto out;
		}
	}

	for (nstart = start ; ; ) {
		unsigned int newflags;
		int last = 0;

		if (is_vm_hugetlb_page(vma)) {
			error = -EACCES;
			goto out;
		}

		/* Here we know that  vma->vm_start <= nstart < vma->vm_end. */

		newflags = prot | (vma->vm_flags & ~(PROT_READ | PROT_WRITE | PROT_EXEC));
		/*
		 * If the application expects PROT_READ to imply PROT_EXEC, and
		 * the mapping isn't for a region whose permissions disallow
		 * execute access, then enable PROT_EXEC.
		 */
		if ((prot & (PROT_EXEC | PROT_READ)) == PROT_READ &&
		    !(current->flags & PF_RELOCEXEC) &&
		    (vma->vm_flags & VM_MAYEXEC))
			newflags |= PROT_EXEC;

		if ((newflags & ~(newflags >> 4)) & 0xf) {
			error = -EACCES;
			goto out;
		}

		if (vma->vm_end > end) {
			error = mprotect_fixup(vma, &prev, nstart, end, newflags);
			goto out;
		}
		if (vma->vm_end == end)
			last = 1;

		tmp = vma->vm_end;
		next = vma->vm_next;
		error = mprotect_fixup(vma, &prev, nstart, tmp, newflags);
		if (error)
			goto out;
		if (last)
			break;
		nstart = tmp;
		vma = next;
		if (!vma || vma->vm_start != nstart) {
			error = -ENOMEM;
			goto out;
		}
	}
	if (next && prev->vm_end == next->vm_start && can_vma_merge(next, prev->vm_flags) &&
	    !prev->vm_file && !(prev->vm_flags & VM_SHARED)) {
		spin_lock(&prev->vm_mm->page_table_lock);
		prev->vm_end = next->vm_end;
		__vma_unlink(prev->vm_mm, next, prev);
		spin_unlock(&prev->vm_mm->page_table_lock);

		kmem_cache_free(vm_area_cachep, next);
		prev->vm_mm->map_count--;
	}
out:
	up_write(&current->mm->mmap_sem);
	vm_validate_enough("exiting mprotect");
	return error;
}
