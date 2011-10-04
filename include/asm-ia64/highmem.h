/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *                   Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with 
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 * Modified for use on IA64.
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

/* undef for production */
#define HIGHMEM_DEBUG 0

/* declarations for highmem.c */
#define PKMAP_BASE (0x0UL)
#define LAST_PKMAP 0
#define LAST_PKMAP_MASK 0
#define PKMAP_NR(virt)  0
#define PKMAP_ADDR(nr)  0
#define kmap_prot PAGE_KERNEL

extern void * FASTCALL(kmap_high(struct page *page, int nonblocking));
extern void FASTCALL(kunmap_high(struct page *page));

extern unsigned long highstart_pfn, highend_pfn;
extern pte_t *kmap_pte;

#define kmap_init(void) do {} while (0)

/*
 * For 64-bit platforms, such as ia64, kmap returns page_address, 
 * since all of physical memory is directly mapped by kernel virtual memory. 
 * kunmap is thus is a no-op.
 */
#define kmap(page) page_address(page)
#define kmap_nonblock(page) page_address(page)
#define kunmap(page) do {} while (0)
#define kmap_atomic(page, type) page_address(page)
#define kunmap_atomic(kvaddr, type) do {} while (0)

#define kmap_atomic_to_page(ptr) virt_to_page(ptr)

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */

