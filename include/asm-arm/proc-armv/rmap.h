#ifndef _ARMV_RMAP_H
#define _ARMV_RMAP_H
/*
 * linux/include/asm-arm/proc-armv/rmap.h
 *
 * Architecture dependant parts of the reverse mapping code,
 *
 * We use the struct page of the page table page to find a pointer
 * to an array of two 'struct arm_rmap_info's, one for each of the
 * two page tables in each page.
 * 
 * - rmi->mm points to the process' mm_struct
 * - rmi->index has the high bits of the address
 * - the lower bits of the address are calculated from the
 *   offset of the page table entry within the page table page
 */
#include <linux/mm.h>

struct arm_rmap_info {
	struct mm_struct *mm;
	unsigned long index;
};

static inline void pgtable_add_rmap(pte_t * ptep, struct mm_struct * mm, unsigned long address)
{
	struct page * page = virt_to_page(ptep);
	struct arm_rmap_info *rmi = (void *)page->mapping;

	if (((unsigned long)ptep)&2048)
		rmi++;

	rmi->mm = mm;
	rmi->index = address & ~((PTRS_PER_PTE * PAGE_SIZE) - 1);
}

static inline void pgtable_remove_rmap(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	struct arm_rmap_info *rmi = (void *)page->mapping;

	if (((unsigned long)ptep)&2048)
		rmi++;

	rmi->mm = NULL;
	rmi->index = 0;
}

static inline struct mm_struct * ptep_to_mm(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	struct arm_rmap_info *rmi = (void *)page->mapping;

	if (((unsigned long)ptep)&2048)
		rmi++;

	return rmi->mm;
}

static inline unsigned long ptep_to_address(pte_t * ptep)
{
	struct page * page = virt_to_page(ptep);
	struct arm_rmap_info *rmi = (void *)page->mapping;
	unsigned long low_bits;

	if (((unsigned long)ptep)&2048)
		rmi++;

	low_bits = ((unsigned long)ptep & ~PAGE_MASK) * PTRS_PER_PTE;
	return rmi->index + low_bits;
}

#endif /* _ARMV_RMAP_H */
