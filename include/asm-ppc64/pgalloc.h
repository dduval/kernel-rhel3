#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/naca.h>
#include <asm/paca.h>

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#define quicklists      get_paca()

#define pgd_quicklist 		(quicklists->pgd_cache)
#define pmd_quicklist 		(quicklists->pmd_cache)
#define pte_quicklist 		(quicklists->pte_cache)
#define pgtable_cache_size 	(quicklists->pgtable_cache_sz)

static inline pgd_t*
pgd_alloc_one_fast (struct mm_struct *mm)
{
	unsigned long *ret = pgd_quicklist;

	if (ret != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	} else
		ret = NULL;
	return (pgd_t *) ret;
}

static inline pgd_t*
pgd_alloc (struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (pgd != NULL)
		clear_page(pgd);
	return pgd;
}

static inline void
pgd_free (pgd_t *pgd)
{
        free_page((unsigned long)pgd);
}

#define pgd_populate(MM, PGD, PMD)	pgd_set(PGD, PMD)

#define pmd_alloc_one_fast(mm, address)		(0)

static inline pmd_t*
pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	pmd_t *pmd = (pmd_t *) __get_free_page(GFP_KERNEL);

	if (pmd != NULL)
		clear_page(pmd);
	return pmd;
}

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte_page) \
        pmd_populate_kernel(mm, pmd, page_address(pte_page))

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
        int count = 0;
        pte_t *pte;

        do {
                pte = (pte_t *)__get_free_page(GFP_KERNEL);
                if (pte)
                        clear_page(pte);
                else {
                        current->state = TASK_UNINTERRUPTIBLE;
                        schedule_timeout(HZ);
                }
        } while (!pte && (count++ < 10));

        return pte;
}

static inline void 
pte_free_kernel(pte_t *pte)
{
        free_page((unsigned long)pte);
}


static inline void
pmd_free (pmd_t *pmd)
{
        free_page((unsigned long)pmd);
}

#define pte_alloc_one_fast(mm, address)		(0)

static inline struct page * 
pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
        pte_t *pte = pte_alloc_one_kernel(mm, address);

        if (pte)
                return virt_to_page(pte);

        return NULL;
}

#define pte_free(pte_page)      pte_free_kernel(page_address(pte_page))

extern int do_check_pgt_cache(int, int);

#define arch_add_exec_range(mm, limit) do { ; } while (0)
#define arch_flush_exec_range(mm)      do { ; } while (0)

#endif /* _PPC64_PGALLOC_H */
