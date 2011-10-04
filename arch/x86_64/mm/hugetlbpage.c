/*
 * X86_64 Huge TLB Page Support for Kernel, based on IA-32 work.
 *
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

#include <linux/sysctl.h>

static long    htlbpagemem;
int	htlbpool_max;
int     htlbpage_max;
static long    htlbzone_pages;

static LIST_HEAD(htlbpage_freelist);
static spinlock_t htlbpage_lock = SPIN_LOCK_UNLOCKED;

void free_huge_page(struct page *page);

#define CHECK_LINK(p) BUG_ON((p) != (struct page *)(p)->lru.next)

static void check_huge_page(struct page *page0)
{
	int i;
	struct page *tmp;

	BUG_ON(page_count(page0) != 1);
	CHECK_LINK(page0);

	BUG_ON(page0->flags & (1 << PG_reserved));
	tmp = page0;
	for (i = 0; i < (HPAGE_SIZE / PAGE_SIZE); i++) {
		if (i && (tmp->flags & (1 << PG_locked | 1 << PG_reserved))) {
			printk("hm, tmp: %p (%d), ->flags: %08lx\n", tmp, i, tmp->flags);
			BUG();
		}
		if (i && page_count(tmp)) {
			printk("hm, tmp: %p (%d), page_count(): %d\n", tmp, i, page_count(tmp));
			BUG();
		}
		if (tmp->mapping) {
			printk("hm, tmp: %p (%d), ->mapping: %p\n", tmp, i, tmp->mapping);
			BUG();
		}
		tmp++;
	}
}

static struct page *alloc_hugetlb_page(void)
{
	int i;
	struct page *page;

	spin_lock(&htlbpage_lock);
	if (list_empty(&htlbpage_freelist)) {
		spin_unlock(&htlbpage_lock);
		return NULL;
	}

	page = list_entry(htlbpage_freelist.next, struct page, list);
	list_del(&page->list);
	htlbpagemem--;
	spin_unlock(&htlbpage_lock);
	check_huge_page(page);
	page->lru.prev = (void *)free_huge_page;
	for (i = 0; i < (HPAGE_SIZE/PAGE_SIZE); ++i)
		clear_highpage(&page[i]);

	return page;
}

static pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	pmd = pmd_alloc(mm, pgd, addr);
	return (pte_t *) pmd;
}

static pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	pmd = pmd_offset(pgd, addr);
	return (pte_t *) pmd;
}

static void set_huge_pte(struct mm_struct *mm, struct vm_area_struct *vma, struct page *page, pte_t * page_table, int write_access)
{
	pte_t entry;

	mm->rss += (HPAGE_SIZE / PAGE_SIZE);
	if (write_access) {
		entry =
		    pte_mkwrite(pte_mkdirty(mk_pte(page, vma->vm_page_prot)));
	} else
		entry = pte_wrprotect(mk_pte(page, vma->vm_page_prot));
	entry = pte_mkyoung(entry);
	mk_pte_huge(entry);
	vm_set_pte(vma, vma->vm_start, page_table, entry);
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}

int copy_hugetlb_page_range(struct mm_struct *dst, struct mm_struct *src,
			struct vm_area_struct *vma)
{
	pte_t *src_pte, *dst_pte, entry;
	struct page *ptepage;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;

	while (addr < end) {
		dst_pte = huge_pte_alloc(dst, addr);
		if (!dst_pte)
			goto nomem;
		src_pte = huge_pte_offset(src, addr);
		entry = *src_pte;
		ptepage = pte_page(entry);
		get_page(ptepage);
		vm_set_pte(vma, vma->vm_start, dst_pte, entry);
		dst->rss += (HPAGE_SIZE / PAGE_SIZE);
		addr += HPAGE_SIZE;
	}
	return 0;

nomem:
	return -ENOMEM;
}

int
follow_pin_hugetlb_page(struct mm_struct *mm, struct vm_area_struct *vma,
		    struct page **pages, struct vm_area_struct **vmas,
		    unsigned long *position, int *length, int i)
{
	unsigned long vpfn, vaddr = *position;
	int remainder = *length;

	BUG_ON(!is_vm_hugetlb_page(vma));

	vpfn = vaddr/PAGE_SIZE;
	while (vaddr < vma->vm_end && remainder) {

		if (pages) {
			pte_t *pte;
			struct page *page;

			pte = huge_pte_offset(mm, vaddr);

			/* hugetlb should be locked, and hence, prefaulted */
			BUG_ON(!pte || pte_none(*pte));

			page = &pte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];

			BUG_ON(!PageCompound(page));

			get_page(page);
			pages[i] = page;
		}

		if (vmas)
			vmas[i] = vma;

		vaddr += PAGE_SIZE;
		++vpfn;
		--remainder;
		++i;
	}

	*length = remainder;
	*position = vaddr;

	return i;
}

struct page *
follow_huge_addr(struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long address, int write)
{
	return NULL;
}

struct page *
follow_pin_huge_addr(struct mm_struct *mm,
	struct vm_area_struct *vma, unsigned long address, int write)
{
	return NULL;
}

struct vm_area_struct *hugepage_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

int pmd_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PSE);
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pmd);
	if (page)
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);

	return page;
}

struct page *
follow_pin_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pmd);
	if (page) {
		page += ((address & ~HPAGE_MASK) >> PAGE_SHIFT);
		get_page(page);
	}

	return page;
}


void free_huge_page(struct page *page0)
{
	BUG_ON(page_count(page0));
	set_page_count(page0, 1);

	check_huge_page(page0);

	INIT_LIST_HEAD(&page0->list);

	spin_lock(&htlbpage_lock);
	list_add(&page0->list, &htlbpage_freelist);
	htlbpagemem++;
	spin_unlock(&htlbpage_lock);
}

void huge_page_release(struct page *page)
{
	CHECK_LINK(page);

	if (!put_page_testzero(page))
		return;

	free_huge_page(page);
}

void unmap_hugepage_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long address;
	pte_t *pte;
	struct page *page;

	BUG_ON(start & (HPAGE_SIZE - 1));
	BUG_ON(end & (HPAGE_SIZE - 1));

	for (address = start; address < end; address += HPAGE_SIZE) {
		pte = huge_pte_offset(mm, address);
		if (pte_none(*pte))
			continue;
		page = pte_page(*pte);	
		vm_pte_clear(vma, address, pte);
		flush_tlb_range(vma, address, address + HPAGE_SIZE);
		huge_page_release(page);
	}
	mm->rss -= (end - start) >> PAGE_SHIFT;
}

void
zap_hugepage_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long length)
{
	struct mm_struct *mm = vma->vm_mm;
	spin_lock(&mm->page_table_lock);
	unmap_hugepage_range(vma, start, start + length);
	spin_unlock(&mm->page_table_lock);
}

int zap_one_hugepage(struct vm_area_struct *vma, unsigned long address, 
		     unsigned long size)
{
	struct page *page;
	pte_t *pte;

	BUG_ON(is_aligned_hugepage_range(address, size) < 0);

	pte = huge_pte_offset(vma->vm_mm, address);
	if (pte_none(*pte))
		BUG();
	page = pte_page(*pte);	
	vm_pte_clear(vma, address, pte);
	flush_tlb_range(vma, address, address + HPAGE_SIZE);
	huge_page_release(page);

	return HPAGE_SIZE >> PAGE_SHIFT;
}


int hugetlb_prefault(struct address_space *mapping, struct vm_area_struct *vma)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret = 0;
	extern void free_one_pmd(pmd_t *);

	BUG_ON(vma->vm_start & ~HPAGE_MASK);
	BUG_ON(vma->vm_end & ~HPAGE_MASK);

	spin_lock(&mm->page_table_lock);
	for (addr = vma->vm_start; addr < vma->vm_end; addr += HPAGE_SIZE) {
		unsigned long idx;
		pte_t *pte = huge_pte_alloc(mm, addr);
		struct page *page;

		if (!pte) {
			ret = -ENOMEM;
			goto out;
		}
		/* if a ptepage already exists from a previous mapping, get rid of it */
		if (!pte_none(*pte))
			free_one_pmd((pmd_t *)pte);

		idx = ((addr - vma->vm_start) >> HPAGE_SHIFT)
			+ (vma->vm_pgoff >> (HPAGE_SHIFT - PAGE_SHIFT));
		page = find_get_page(mapping, idx);
		if (!page) {
			page = alloc_hugetlb_page();
			if (!page) {
				ret = -ENOMEM;
				goto out;
			}
			ret = add_to_page_cache_unique_nolru(page, mapping, idx, page_hash(mapping, idx));
			unlock_page(page);
			if (ret) {
				free_huge_page(page);
				goto out;
			}
		}
		CHECK_LINK(page);
		set_huge_pte(mm, vma, page, pte, vma->vm_flags & VM_WRITE);
	}
out:
	spin_unlock(&mm->page_table_lock);
	return ret;
}

void update_and_free_page(struct page *page)
{
	int j;
	struct page *map;

	map = page;
	htlbzone_pages--;
	for (j = 0; j < (HPAGE_SIZE / PAGE_SIZE); j++) {
		map->flags &= ~(1 << PG_locked | 1 << PG_error | 1 << PG_referenced | 1 << PG_dirty | 1 << PG_reserved);
		set_page_count(map, 0);
		map++;
	}
	set_page_count(page, 1);
	free_pages_ok(page, HUGETLB_PAGE_ORDER);
}

static int try_to_free_low(int count)
{
	struct list_head *p;
	struct page *page, *map;

	map = NULL;
	spin_lock(&htlbpage_lock);
	list_for_each(p, &htlbpage_freelist) {
		if (map) {
			list_del(&map->list);
			update_and_free_page(map);
			htlbpagemem--;
			map = NULL;
			if (++count == 0)
				break;
		}
		page = list_entry(p, struct page, list);
		if (!PageHighMem(page))
			map = page;
	}
	if (map) {
		list_del(&map->list);
		update_and_free_page(map);
		htlbpagemem--;
		count++;
	}
	spin_unlock(&htlbpage_lock);
	return count;
}

int set_hugetlb_mem_size(int count)
{
	int lcount;
	struct page *page;

	if (count < 0)
		lcount = count;
	else
		lcount = count - htlbzone_pages;

	if (lcount == 0)
		return (int)htlbzone_pages;
	if (lcount > 0) {	/* Increase the mem size. */
		while (lcount--) {
			page = alloc_pages(__GFP_HIGHMEM, HUGETLB_PAGE_ORDER);
			if (page == NULL)
				break;
			spin_lock(&htlbpage_lock);
			list_add(&page->list, &htlbpage_freelist);
			htlbpagemem++;
			htlbzone_pages++;
			spin_unlock(&htlbpage_lock);
		}
		return (int) htlbzone_pages;
	}
	/* Shrink the memory size. */
	lcount = try_to_free_low(lcount);
	while (lcount++) {
		page = alloc_hugetlb_page();
		if (page == NULL)
			break;
		spin_lock(&htlbpage_lock);
		update_and_free_page(page);
		spin_unlock(&htlbpage_lock);
	}
	return (int) htlbzone_pages;
}

#define HPAGE_FACTOR (HPAGE_SIZE / 1024 / 1024)

int hugetlb_sysctl_handler(ctl_table *table, int write,
		struct file *file, void *buffer, size_t *length)
{
	int ret = proc_dointvec(table, write, file, buffer, length);
	/*
	 * htlbpool_max is in units of MB.
	 * htlbpage_max is in units of hugepages.
	 *
	 * Be careful about 32-bit overflows:
	 */
	if (write) {
		htlbpage_max = htlbpool_max / HPAGE_FACTOR;
		htlbpage_max = set_hugetlb_mem_size(htlbpage_max);
		htlbpool_max = htlbpage_max * HPAGE_FACTOR;
	}
	return ret;
}

static int __init hugetlb_setup(char *s)
{
	if (sscanf(s, "%d", &htlbpage_max) <= 0)
		htlbpage_max = 0;
	htlbpool_max = htlbpage_max * HPAGE_FACTOR;
	return 1;
}
__setup("hugepages=", hugetlb_setup);

static int __init hugetlbpool_setup(char *s)
{
	if (sscanf(s, "%d", &htlbpool_max) <= 0)
		htlbpool_max = 0;
	htlbpage_max = htlbpool_max / HPAGE_FACTOR;
	return 1;
}
__setup("hugetlbpool=", hugetlbpool_setup);


static int __init hugetlb_init(void)
{
	int i;
	struct page *page;

	for (i = 0; i < htlbpage_max; ++i) {
		page = alloc_pages(__GFP_HIGHMEM, HUGETLB_PAGE_ORDER);
		if (!page)
			break;
		spin_lock(&htlbpage_lock);
		list_add(&page->list, &htlbpage_freelist);
		spin_unlock(&htlbpage_lock);
	}
	htlbpage_max = htlbpagemem = htlbzone_pages = i;
	printk("Total HugeTLB memory allocated, %ld\n", htlbpagemem);
	return 0;
}
module_init(hugetlb_init);

int hugetlb_report_meminfo(char *buf)
{
	return sprintf(buf,
			"HugePages_Total: %5lu\n"
			"HugePages_Free:  %5lu\n"
			"Hugepagesize:    %5lu kB\n",
			htlbzone_pages,
			htlbpagemem,
			HPAGE_SIZE/1024);
}

int is_hugepage_mem_enough(size_t size)
{
	return (size + ~HPAGE_MASK)/HPAGE_SIZE <= htlbpagemem;
}

/*
 * We cannot handle pagefaults against hugetlb pages at all.  They cause
 * handle_mm_fault() to try to instantiate regular-sized pages in the
 * hugegpage VMA.  do_page_fault() is supposed to trap this, so BUG is we get
 * this far.
 */
static struct page *hugetlb_nopage(struct vm_area_struct *vma,
				unsigned long address, int unused)
{
	BUG();
	return NULL;
}

struct vm_operations_struct hugetlb_vm_ops = {
	.nopage = hugetlb_nopage,
};
