/*
 *  linux/arch/i386/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm_inline.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/tlb.h>
#include <asm/desc.h>

mmu_gather_t mmu_gathers[NR_CPUS];
unsigned long highstart_pfn, highend_pfn;
static unsigned long totalram_pages;
static unsigned long totalhigh_pages;

int do_check_pgt_cache(int low, int high)
{
	return 0;	/* FIXME! */
#if 0
	int freed = 0;
	if(pgtable_cache_size > high) {
		do {
			if (pgd_quicklist) {
				free_pgd_slow(get_pgd_fast());
				freed++;
			}
			if (pmd_quicklist) {
				pmd_free_slow(pmd_alloc_one_fast(NULL, 0));
				freed++;
			}
			if (pte_quicklist) {
				pte_free_slow(pte_alloc_one_fast(NULL, 0));
				freed++;
			}
		} while(pgtable_cache_size > low);
	}
	return freed;
#endif
}

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

pte_t *kmap_pte;

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset_kernel(pmd_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr))

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);
}

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

static __init void prepare_pagetables(pgd_t *pgd_base, unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_base + __pgd_offset(address);
	pmd = pmd_offset(pgd, address);
	if (!pmd_present(*pmd)) {
		pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)));
	}
}


static void __init fixrange_init (unsigned long start, unsigned long end, pgd_t *pgd_base)
{
	unsigned long vaddr;

	for (vaddr = start; vaddr != end; vaddr += PAGE_SIZE)
		prepare_pagetables(pgd_base, vaddr);
}

extern char _stext, __init_end;

/*
 * Is there any kernel text byte between addr and addr+size?
 */
static inline int is_kernel_text(unsigned long addr, unsigned long size)
{
	unsigned long text_start = (unsigned long)&_stext;
	unsigned long text_end = (unsigned long)&__init_end;

	if (addr < 1024 * 1024)
		return 1;
	if (text_end < addr)
		return 0;
	if (addr + size <= text_start)
		return 0;

	return 1;
}

void setup_identity_mappings(pgd_t *pgd_base, unsigned long start, unsigned long end)
{
	unsigned long vaddr;
	pgd_t *pgd;
	int i, j, k;
	pmd_t *pmd;
	pte_t *pte, *pte_base;

	pgd = pgd_base;

	for (i = 0; i < PTRS_PER_PGD; pgd++, i++) {
		vaddr = i*PGDIR_SIZE;
		if (end && (vaddr >= end))
			break;
		pmd = pmd_offset(pgd, 0);
		for (j = 0; j < PTRS_PER_PMD; pmd++, j++) {
			vaddr = i*PGDIR_SIZE + j*PMD_SIZE;
			if (end && (vaddr >= end))
				break;
			if (vaddr < start)
				continue;
			if (cpu_has_pse) {
				unsigned long long __pe;

				set_in_cr4(X86_CR4_PSE);
				boot_cpu_data.wp_works_ok = 1;
				__pe = _KERNPG_TABLE + _PAGE_PSE + vaddr - start;
				if (use_nx && !is_kernel_text(vaddr, PMD_SIZE))
					__pe += _PAGE_NX;

				/* Make it "global" too if supported */
				if (cpu_has_pge) {
					set_in_cr4(X86_CR4_PGE);
#if !CONFIG_X86_SWITCH_PAGETABLES
					__pe += _PAGE_GLOBAL;
					__PAGE_KERNEL |= _PAGE_GLOBAL;
					__PAGE_KERNEL_EXEC |= _PAGE_GLOBAL;
#endif
				}
				set_pmd(pmd, __pmd(__pe));
				continue;
			}

			if (!pmd_present(*pmd))
				pte_base = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
			else
				pte_base = (pte_t *) pmd_page_kernel(*pmd);
			pte = pte_base;
			for (k = 0; k < PTRS_PER_PTE; pte++, k++) {
				vaddr = i*PGDIR_SIZE + j*PMD_SIZE + k*PAGE_SIZE;
				if (end && (vaddr >= end))
					break;
				if (vaddr < start)
					continue;
				if (is_kernel_text(vaddr, PAGE_SIZE)) 
					*pte = mk_pte_phys(vaddr-start, PAGE_KERNEL_EXEC);
				else
					*pte = mk_pte_phys(vaddr-start, PAGE_KERNEL);
			}
			set_pmd(pmd, __pmd(_KERNPG_TABLE + __pa(pte_base)));
		}
	}
}

/*
 * Clear kernel pagetables in a PMD_SIZE-aligned range.
 */
static void clear_mappings(pgd_t *pgd_base, unsigned long start, unsigned long end)
{
	unsigned long vaddr;
	pgd_t *pgd;
	pmd_t *pmd;
	int i, j;

	pgd = pgd_base;

	for (i = 0; i < PTRS_PER_PGD; pgd++, i++) {
		vaddr = i*PGDIR_SIZE;
		if (end && (vaddr >= end))
			break;
		pmd = pmd_offset(pgd, 0);
		for (j = 0; j < PTRS_PER_PMD; pmd++, j++) {
			vaddr = i*PGDIR_SIZE + j*PMD_SIZE;
			if (end && (vaddr >= end))
				break;
			if (vaddr < start)
				continue;
			pmd_clear(pmd);
		}
	}
	flush_tlb_all();
}

static void __init pagetable_init (void)
{
	unsigned long vaddr, end;
	pgd_t *pgd_base;
	int i;

	/*
	 * This can be zero as well - no problem, in that case we exit
	 * the loops anyway due to the PTRS_PER_* conditions.
	 */
	end = (unsigned long)__va(max_low_pfn*PAGE_SIZE);

	pgd_base = swapper_pg_dir;
#if CONFIG_X86_PAE
	/*
	 * It causes too many problems if there's no proper pmd set up
	 * for all 4 entries of the PGD - so we allocate all of them.
	 * PAE systems will not miss this extra 4-8K anyway ...
	 */
	for (i = 0; i < PTRS_PER_PGD; i++) {
		pmd_t *pmd = (pmd_t *) alloc_bootmem_low_pages(PAGE_SIZE);
		set_pgd(pgd_base + i, __pgd(__pa(pmd) + 0x1));
	}
#endif
	/*
	 * Set up lowmem-sized identity mappings at PAGE_OFFSET:
	 */
	setup_identity_mappings(pgd_base, PAGE_OFFSET, end);

	/*
	 * Add flat-mode identity-mappings - SMP needs it when
	 * starting up on an AP from real-mode. (In the non-PAE
	 * case we already have these mappings through head.S.)
	 * All user-space mappings are explicitly cleared after
	 * SMP startup.
	 */
#if CONFIG_SMP && CONFIG_X86_PAE
	setup_identity_mappings(pgd_base, 0, 16*1024*1024);
#endif

	/*
	 * Fixed mappings, only the page table structure has to be
	 * created - mappings will be set by set_fixmap():
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, 0, pgd_base);

#if CONFIG_HIGHMEM
	{
		pgd_t *pgd;
		pmd_t *pmd;
		pte_t *pte;

		/*
		 * Permanent kmaps:
		 */
		vaddr = PKMAP_BASE;
		fixrange_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, pgd_base);

		pgd = swapper_pg_dir + __pgd_offset(vaddr);
		pmd = pmd_offset(pgd, vaddr);
		pte = pte_offset_kernel(pmd, vaddr);
		pkmap_page_table = pte;
	}
#endif
}

void __init zap_low_mappings(void)
{
	printk("zapping low mappings.\n");
	/*
	 * Zap initial low-memory mappings.
	 */
	clear_mappings(swapper_pg_dir, 0, 16*1024*1024);
}


unsigned long long __PAGE_KERNEL = _PAGE_KERNEL;
unsigned long long __PAGE_KERNEL_EXEC = _PAGE_KERNEL_EXEC;

#if CONFIG_HIGHMEM64G
#define MIN_ZONE_DMA_PAGES 2048
#else
#define MIN_ZONE_DMA_PAGES 4096
#endif

static void __init zone_sizes_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned int max_dma, ratio_maxdma, high, low;

	max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	low = max_low_pfn;
	high = highend_pfn;
	
	/*
	 * Make the DMA zone 5% of memory, with a minimum of 8Mb
	 * and a maximum of 16Mb.
	 */
	#if MAX_ORDER != 11
	#error fix this
	#endif
	ratio_maxdma = (highend_pfn / 20) & ~((1 << MAX_ORDER)-1); 
	if (ratio_maxdma > max_dma)
		ratio_maxdma = max_dma;
	if (ratio_maxdma < MIN_ZONE_DMA_PAGES)
		ratio_maxdma = MIN_ZONE_DMA_PAGES;
		
	max_dma = ratio_maxdma;

	if (low < max_dma)
		zones_size[ZONE_DMA] = low;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = low - max_dma;
#ifdef CONFIG_HIGHMEM
		zones_size[ZONE_HIGHMEM] = high - low;
#endif
	}
	free_area_init(zones_size);
}

/*
 * paging_init() sets up the page tables - note that the first 8MB are
 * already mapped by head.S.
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
#ifdef CONFIG_X86_PAE
	if (cpu_has_pae) {
 		if (cpuid_eax(0x80000000) > 0x80000001) {
 			u32 v[4];
 			u32 l,h;
 			cpuid(0x80000001, &v[0], &v[1], &v[2], &v[3]);
 			if ((v[3] & (1 << 20))) {
 				rdmsr(MSR_EFER, l, h);
 				l |= EFER_NX;
 				wrmsr(MSR_EFER, l, h);
 				printk("NX (Execute Disable) protection: active\n");
				use_nx = 1;
 				__supported_pte_mask |= _PAGE_NX;
 			}
 		}
 	}
#endif
	if (!use_nx && exec_shield)
		printk("NX protection not present; using segment protection\n");
	pagetable_init();

	load_cr3(swapper_pg_dir);	

#if CONFIG_X86_PAE
	/*
	 * We will bail out later - printk doesn't work right now so
	 * the user would just see a hanging kernel.
	 */
	if (cpu_has_pae)
		set_in_cr4(X86_CR4_PAE);
#endif

	__flush_tlb_all();

	kmap_init();
	zone_sizes_init();
}

/*
 * Test if the WP bit works in supervisor mode. It isn't supported on 386's
 * and also on some strange 486's (NexGen etc.). All 586+'s are OK. The jumps
 * before and after the test are here to work-around some nasty CPU bugs.
 */

/*
 * This function cannot be __init, since exceptions don't work in that
 * section.
 */
static int do_test_wp_bit(unsigned long vaddr);

void __init test_wp_bit(void)
{
/*
 * Ok, all PSE-capable CPUs are definitely handling the WP bit right.
 */
	const unsigned long vaddr = PAGE_OFFSET;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte, old_pte;

	printk("Checking if this processor honours the WP bit even in supervisor mode... ");

	pgd = swapper_pg_dir + __pgd_offset(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	old_pte = *pte;
	*pte = mk_pte_phys(0, PAGE_READONLY);
	local_flush_tlb();

	boot_cpu_data.wp_works_ok = do_test_wp_bit(vaddr);

	*pte = old_pte;
	local_flush_tlb();

	if (!boot_cpu_data.wp_works_ok) {
		printk("No.\n");
#ifdef CONFIG_X86_WP_WORKS_OK
		panic("This kernel doesn't support CPU's with broken WP. Recompile it for a 386!");
#endif
	} else {
		printk("Ok.\n");
	}
}

int page_is_ram (unsigned long pagenr)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long addr, end;

		if (e820.map[i].type != E820_RAM)	/* not usable memory */
			continue;
		/*
		 *	!!!FIXME!!! Some BIOSen report areas as RAM that
		 *	are not. Notably the 640->1Mb area. We need a sanity
		 *	check here.
		 */
		addr = (e820.map[i].addr+PAGE_SIZE-1) >> PAGE_SHIFT;
		end = (e820.map[i].addr+e820.map[i].size) >> PAGE_SHIFT;
		if  ((pagenr >= addr) && (pagenr < end))
			return 1;
	}
	return 0;
}

unsigned long next_ram_page(unsigned long pagenr)
{
	int i;
	unsigned long addr, end;
	unsigned long min_pageno = ULONG_MAX;

	pagenr++;

	for (i = 0; i < e820.nr_map; i++) {

		if (e820.map[i].type != E820_RAM)	/* not usable memory */
			continue;

		addr = (e820.map[i].addr+PAGE_SIZE-1) >> PAGE_SHIFT;
		end = (e820.map[i].addr+e820.map[i].size) >> PAGE_SHIFT;
		if  ((pagenr >= addr) && (pagenr < end))
			return pagenr;
		if ((pagenr < addr) && (addr < min_pageno))
			min_pageno = addr;
	}
	return min_pageno;
}

static inline int page_kills_ppro(unsigned long pagenr)
{
	if(pagenr >= 0x70000 && pagenr <= 0x7003F)
		return 1;
	return 0;
}

#ifdef CONFIG_HIGHMEM
void __init one_highpage_init(struct page *page, int pfn, int bad_ppro)
{
	if (!page_is_ram(pfn)) {
		SetPageReserved(page);
		return;
	}
	
	if (bad_ppro && page_kills_ppro(pfn)) {
		SetPageReserved(page);
		return;
	}
	
	ClearPageReserved(page);
	set_bit(PG_highmem, &page->flags);
	atomic_set(&page->count, 1);
	__free_page(page);
	totalhigh_pages++;
}
#endif /* CONFIG_HIGHMEM */

static void __init set_max_mapnr_init(void)
{
#ifdef CONFIG_HIGHMEM
        highmem_start_page = mem_map + highstart_pfn;
        max_mapnr = num_physpages = highend_pfn;
        num_mappedpages = max_low_pfn;
#else
        max_mapnr = num_mappedpages = num_physpages = max_low_pfn;
#endif
}

static int __init free_pages_init(void)
{
	extern int ppro_with_ram_bug(void);
	int bad_ppro, reservedpages, pfn;

	bad_ppro = ppro_with_ram_bug();
	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

	reservedpages = 0;
	for (pfn = 0; pfn < max_low_pfn; pfn++) {
		/*
		 * Only count reserved RAM pages
		 */
		if (page_is_ram(pfn) && PageReserved(mem_map+pfn))
			reservedpages++;
	}
#ifdef CONFIG_HIGHMEM
	for (pfn = highend_pfn-1; pfn >= highstart_pfn; pfn--)
		one_highpage_init((struct page *) (mem_map + pfn), pfn, bad_ppro);
	reset_highmem_zone(totalhigh_pages);
	totalram_pages += totalhigh_pages;
#endif
	return reservedpages;
}

extern void fixup_sort_exception_table(void);

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;

	fixup_sort_exception_table();
	if (!mem_map)
		BUG();
#ifdef CONFIG_HIGHMEM
	/* check that fixmap and pkmap do not overlap */
	if (PKMAP_BASE+LAST_PKMAP*PAGE_SIZE >= FIXADDR_START) {
		printk(KERN_ERR "fixmap and kmap areas overlap - this will crash\n");
		printk(KERN_ERR "pkstart: %lxh pkend: %lxh fixstart %lxh\n",
				PKMAP_BASE, PKMAP_BASE+LAST_PKMAP*PAGE_SIZE, FIXADDR_START);
		BUG();
	}
#endif
	set_max_mapnr_init();

	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	reservedpages = free_pages_init();

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk(KERN_INFO "Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init, %ldk highmem)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10,
		(unsigned long) (totalhigh_pages << (PAGE_SHIFT-10))
	       );

#if CONFIG_X86_PAE
	if (!cpu_has_pae)
		panic("cannot execute a PAE-enabled kernel on a PAE-less CPU!");
#endif
	if (boot_cpu_data.wp_works_ok < 0)
		test_wp_bit();

	/*
	 * Subtle. SMP is doing it's boot stuff late (because it has to
	 * fork idle threads) - but it also needs low mappings for the
	 * protected-mode entry to work. We zap these entries only after
	 * the WP-bit has been tested.
	 */
#ifndef CONFIG_SMP
	zap_low_mappings();
#endif
	entry_trampoline_setup();
	default_ldt_page = virt_to_page(default_ldt);
	load_LDT(&init_mm.context);
}

/* Put this after the callers, so that it cannot be inlined */
static int do_test_wp_bit(unsigned long vaddr)
{
	char tmp_reg;
	int flag;

	__asm__ __volatile__(
		"	movb %0,%1	\n"
		"1:	movb %1,%0	\n"
		"	xorl %2,%2	\n"
		"2:			\n"
		".section __ex_table,\"a\"\n"
		"	.align 4	\n"
		"	.long 1b,2b	\n"
		".previous		\n"
		:"=m" (*(char *) vaddr),
		 "=q" (tmp_reg),
		 "=r" (flag)
		:"2" (1)
		:"memory");
	
	return flag;
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		totalram_pages++;
	}
	printk (KERN_INFO "Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		printk (KERN_INFO "Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = 0;
	val->freeram = nr_free_pages();
	val->bufferram = atomic_read(&buffermem_pages);
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
	return;
}

#if defined(CONFIG_X86_PAE)
struct kmem_cache_s *pae_pgd_cachep;
void __init pgtable_cache_init(void)
{
	/*
	 * PAE pgds must be 16-byte aligned:
	 */
	pae_pgd_cachep = kmem_cache_create("pae_pgd", 32, 0,
		SLAB_HWCACHE_ALIGN | SLAB_MUST_HWCACHE_ALIGN, NULL, NULL);
	if (!pae_pgd_cachep)
		panic("init_pae(): Cannot alloc pae_pgd SLAB cache");
}
#endif /* CONFIG_X86_PAE */
