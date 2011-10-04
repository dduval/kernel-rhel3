#ifndef _GENERIC_RMAP_H
#define _GENERIC_RMAP_H
/*
 * linux/include/asm-generic/rmap.h
 *
 * Architecture dependant parts of the reverse mapping code,
 * this version should work for most architectures with a
 * 'normal' page table layout.
 *
 * We use the struct page of the page table page to find out
 * the process and full address of a page table entry:
 * - page->mapping points to the process' mm_struct
 * - page->index has the high bits of the address
 * - the lower bits of the address are calculated from the
 *   offset of the page table entry within the page table page
 *
 * For CONFIG_HIGHPTE, we need to represent the address of a pte in a
 * scalar pte_addr_t.  The pfn of the pte's page is shifted left by PAGE_SIZE
 * bits and is then ORed with the byte offset of the pte within its page.
 *
 * For CONFIG_HIGHMEM4G, the pte_addr_t is 32 bits.  20 for the pfn, 12 for
 * the offset.
 *
 * For CONFIG_HIGHMEM64G, the pte_addr_t is 64 bits.  52 for the pfn, 12 for
 * the offset.
 */
#include <linux/mm.h>

/*
 * The pte_addr_t is the type of the pte.direct field of the page
 * structure, whereas the chain_ptep_t is the type of the ptes[]
 * array elements in the pte_chain structure.  The PTE_ADDR_C2D()
 * macro converts from a chain_ptep_t to a pte_addr_t, and the
 * PTE_ADDR_D2C() macro converts from a pte_addr_t to a chain_ptep_t.
 *
 * Typically, these mappings are no-ops.  But for certain x86
 * configurations, the pte_addr_t is 64 bits and the chain_ptep_t
 * is 32 bits, thus more than doubling the number of pte addresses
 * that can be stored in a pte_chain structure.  In this case, the
 * definitions for CHAIN_PTEP_T, PTE_ADDR_C2D(), and PTE_ADDR_D2C()
 * must occur in an architecture-dependent header file before this
 * header file is included.
 */
#ifdef CHAIN_PTEP_T
typedef CHAIN_PTEP_T	chain_ptep_t;
#else
typedef pte_addr_t	chain_ptep_t;
#define PTE_ADDR_C2D(x) ((pte_addr_t)(x))
#define PTE_ADDR_D2C(x) ((chain_ptep_t)(x))
#endif

static inline void pgtable_add_rmap(struct page * page, struct mm_struct * mm, unsigned long address)
{
#ifdef BROKEN_PPC_PTE_ALLOC_ONE
	/* OK, so PPC calls pte_alloc() before mem_map[] is setup ... ;( */
	extern int mem_init_done;

	if (!mem_init_done)
		return;
#endif
	page->mapping = (void *)mm;
	page->index = address & ~((PTRS_PER_PTE * PAGE_SIZE) - 1);
}

static inline void pgtable_remove_rmap(struct page * page)
{
	page->mapping = NULL;
	page->index = 0;
}

static inline struct mm_struct * ptep_to_mm(pte_t * ptep)
{
	struct page * page = kmap_atomic_to_page(ptep);

	return (struct mm_struct *) page->mapping;
}

static inline unsigned long ptep_to_address(pte_t * ptep)
{
	struct page * page = kmap_atomic_to_page(ptep);
	unsigned long low_bits;

	low_bits = ((unsigned long)ptep & ~PAGE_MASK) * PTRS_PER_PTE;
	return page->index + low_bits;
}

#if CONFIG_HIGHPTE
static inline chain_ptep_t ptep_to_paddr(pte_t *ptep)
{
	u64 paddr;
	paddr = (u64)page_to_pfn(kmap_atomic_to_page(ptep)) << PAGE_SHIFT;
	return PTE_ADDR_D2C(paddr + ((u64)(unsigned long)ptep & ~PAGE_MASK));
}
#else
static inline chain_ptep_t ptep_to_paddr(pte_t *ptep)
{
	return (chain_ptep_t)ptep;
}
#endif

#ifndef CONFIG_HIGHPTE
static inline pte_t *rmap_ptep_map(pte_addr_t pte_paddr)
{
	return (pte_t *)pte_paddr;
}

static inline void rmap_ptep_unmap(pte_t *pte)
{
	return;
}
#endif

#endif /* _GENERIC_RMAP_H */
