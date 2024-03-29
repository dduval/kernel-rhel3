/*
 *  linux/arch/i386/mm/pgtable.c
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm_inline.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/tlb.h>
//#include <asm/tlbflush.h>

void show_mem(void)
{
	int total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_size; ++i) {
			page = pgdat->node_mem_map + i;
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
	}
	printk("%d pages of RAM\n", total);
	printk("%d pages of HIGHMEM\n",highmem);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

/*
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */ 
static void set_pte_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pgd, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	/* <pfn,flags> stored as-is, to permit clearing entries */
	set_pte(pte, pfn_pte(pfn, flags));

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/*
 * Associate a large virtual page frame with a given physical page frame 
 * and protection flags for that frame. pfn is for the base of the page,
 * vaddr is what the page gets mapped to - both must be properly aligned. 
 * The pmd must already be instantiated. Assumes PAE mode.
 */ 
void set_pmd_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pmd_t *pmd;

	if (vaddr & (PMD_SIZE-1)) {		/* vaddr is misaligned */
		printk ("set_pmd_pfn: vaddr misaligned\n");
		return; /* BUG(); */
	}
	if (pfn & (PTRS_PER_PTE-1)) {		/* pfn is misaligned */
		printk ("set_pmd_pfn: pfn misaligned\n");
		return; /* BUG(); */
	}
	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	if (pgd_none(*pgd)) {
		printk ("set_pmd_pfn: pgd_none\n");
		return; /* BUG(); */
	}
	pmd = pmd_offset(pgd, vaddr);
	set_pmd(pmd, pfn_pmd(pfn, flags));
	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

void __set_fixmap (enum fixed_addresses idx, unsigned long phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_pfn(address, phys >> PAGE_SHIFT, flags);
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	int count = 0;
	pte_t *pte;
   
   	do {
		pte = (pte_t *) __get_free_page(GFP_KERNEL);
		if (pte)
			clear_page(pte);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));
	return pte;
}

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	int count = 0;
	struct page *pte;
   
   	do {
#if CONFIG_HIGHPTE
		pte = alloc_pages(GFP_KERNEL | __GFP_HIGHMEM | __GFP_WIRED, 0);
#else
		pte = alloc_pages(GFP_KERNEL, 0);
#endif
		if (pte)
			clear_highpage(pte);
		else {
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(HZ);
		}
	} while (!pte && (count++ < 10));
	return pte;
}

#if CONFIG_X86_PAE

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	int i;
	pgd_t *pgd = kmem_cache_alloc(pae_pgd_cachep, GFP_KERNEL);

	if (pgd) {
		unsigned long pmd;
#if CONFIG_X86_4G_VM_LAYOUT
		pmd_t *pmd0, *kernel_pmd0;
#endif

		for (i = 0; i < USER_PTRS_PER_PGD; i++) {
			pmd = __get_free_page(GFP_KERNEL);
			if (!pmd)
				goto out_oom;
			clear_page(pmd);
			set_pgd(pgd + i, __pgd(1 + __pa(pmd)));
		}
#if CONFIG_X86_4G_VM_LAYOUT
		/*
		 * In the 4G userspace case alias the top 16 MB virtual
		 * memory range into the user mappings as well (these
		 * include the trampoline and CPU data structures).
		 */
		pmd0 = (pmd_t *)pmd;
		kernel_pmd0 = (pmd_t *)pgd_page(swapper_pg_dir[PTRS_PER_PGD-1]);
		memcpy(pmd0 + PTRS_PER_PMD - NR_SHARED_PMDS, kernel_pmd0 + PTRS_PER_PMD - NR_SHARED_PMDS, sizeof(pmd_t) * NR_SHARED_PMDS);
#else
		memcpy(pgd + USER_PTRS_PER_PGD,
			swapper_pg_dir + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
#endif
	}
	return pgd;
out_oom:
	for (i--; i >= 0; i--)
		free_page((unsigned long)__va(pgd_val(pgd[i])-1));
	kmem_cache_free(pae_pgd_cachep, pgd);
	return NULL;
}

void pgd_free(pgd_t *pgd)
{
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		free_page((unsigned long)__va(pgd_val(pgd[i])-1));
	kmem_cache_free(pae_pgd_cachep, pgd);
}

#else

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (pgd) {
#if CONFIG_X86_4G_VM_LAYOUT
		memset(pgd, 0, (PTRS_PER_PGD-2) * sizeof(pgd_t));
		pgd[PTRS_PER_PGD-2] = swapper_pg_dir[PTRS_PER_PGD-2];
		pgd[PTRS_PER_PGD-1] = swapper_pg_dir[PTRS_PER_PGD-1];
#else
 		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
 		memcpy(pgd + USER_PTRS_PER_PGD,
 			swapper_pg_dir + USER_PTRS_PER_PGD,
 			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
#endif
	}
	return pgd;
}

void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#endif /* CONFIG_X86_PAE */

