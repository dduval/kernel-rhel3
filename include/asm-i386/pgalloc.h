#ifndef _I386_PGALLOC_H
#define _I386_PGALLOC_H

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/fixmap.h>
#include <asm/desc.h>
#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist (current_cpu_data.pmd_quick)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE +
		((unsigned long long)page_to_pfn(pte) <<
			(unsigned long long) PAGE_SHIFT)));
}
/*
 * Allocate and free page tables.
 */

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern struct page *pte_alloc_one(struct mm_struct *, unsigned long);

#define pte_alloc_one_fast(mm, address)		(0)
#define pmd_alloc_one_fast(mm, address)		(0)

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}


#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 * (In the PAE case we free the pmds as part of the pgd.)
 */

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

extern int do_check_pgt_cache(int, int);

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */

#ifndef CONFIG_SMP

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb_all()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
#ifndef CONFIG_X86_SWITCH_PAGETABLES
	if (mm == current->active_mm)
		__flush_tlb();
#endif
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
#ifndef CONFIG_X86_SWITCH_PAGETABLES
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
#endif
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
#ifndef CONFIG_X86_SWITCH_PAGETABLES
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
#endif
}

#else

#include <asm/smp.h>

#define local_flush_tlb() \
	__flush_tlb()

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);

#define flush_tlb()	flush_tlb_all()

static inline void flush_tlb_range(struct vm_area_struct * vma, unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

struct tlb_state
{
	struct mm_struct *active_mm;
	int state;
} ____cacheline_aligned;
extern struct tlb_state cpu_tlbstate[NR_CPUS];

#endif /* CONFIG_SMP */

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	flush_tlb_mm(mm);
}

static inline void set_user_cs(struct desc_struct *desc, unsigned long limit)
{
	limit = (limit - 1) / PAGE_SIZE;
	desc->a = limit & 0xffff;
	desc->b = (limit & 0xf0000) | 0x00c0fb00;
}

#define load_user_cs_desc(cpu, mm) \
    	cpu_gdt_table[(cpu)][GDT_ENTRY_DEFAULT_USER_CS] = (mm)->context.user_cs

extern void arch_add_exec_range(struct mm_struct *mm, unsigned long limit);
extern void arch_remove_exec_range(struct mm_struct *mm, unsigned long limit);
extern void arch_flush_exec_range(struct mm_struct *mm);

#define HAVE_ARCH_UNMAPPED_AREA 1

#endif /* _I386_PGALLOC_H */
