/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with 
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/kmap_types.h>
#include <asm/pgtable.h>

#include <asm/atomic_kmap.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *pkmap_page_table;

extern void kmap_init(void) __init;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#define PKMAP_BASE (0xff000000UL)
#define NR_SHARED_PMDS ((0xffffffff-PKMAP_BASE+1)/PMD_SIZE)
#ifdef CONFIG_X86_PAE
#define LAST_PKMAP 512
#else
#define LAST_PKMAP 1024
#endif
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void * FASTCALL(kmap_high(struct page *page, int nonblocking));
extern void FASTCALL(kunmap_high(struct page *page));

#define kmap(page) __kmap(page, 0)
#define kmap_nonblock(page) __kmap(page, 1)

static inline void *__kmap(struct page *page, int nonblocking)
{
	if (in_interrupt())
		out_of_line_bug();
	if (page < highmem_start_page)
		return page_address(page);
	return kmap_high(page, nonblocking);
}

static inline void kunmap(struct page *page)
{
	if (in_interrupt())
		out_of_line_bug();
	if (page < highmem_start_page)
		return;
	kunmap_high(page);
}

static inline void *kmap_atomic(struct page *page, enum km_type type)
{
/*
 * The same code, just different probability:
 */
#if CONFIG_X86_4G_VM_LAYOUT
	if (unlikely(page < highmem_start_page))
		return page_address(page);
#else
	if (page < highmem_start_page)
		return page_address(page);
#endif
	return __kmap_atomic(page, type);
}

static inline void kunmap_atomic(void *kvaddr, enum km_type type)
{
#if HIGHMEM_DEBUG
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;

	if (vaddr < FIXADDR_START) // FIXME
		return;
	__kunmap_atomic(kvaddr, type);
#endif
}

static inline struct page *kmap_atomic_to_page(void *ptr)
{
	unsigned long idx, vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	idx = virt_to_fix(vaddr);
	pte = kmap_pte - (idx - FIX_KMAP_BEGIN);
	return pte_page(*pte);
}

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
