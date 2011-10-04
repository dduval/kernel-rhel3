#ifndef _I386_PAGE_H
#define _I386_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#include <linux/config.h>

#ifndef __ASSEMBLY__

#ifdef CONFIG_X86_USE_3DNOW

#include <asm/mmx.h>

#define clear_page(page)	mmx_clear_page((void *)(page))
#define copy_page(to,from)	mmx_copy_page(to,from)

#else

/*
 *	On older X86 processors its not a win to use MMX here it seems.
 *	Maybe the K6-III ?
 */
 
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#endif

#define clear_user_page(page, vaddr)	clear_page(page)
#define copy_user_page(to, from, vaddr)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
#if CONFIG_X86_PAE
extern unsigned long long __supported_pte_mask;
typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pmd; } pmd_t;
typedef struct { unsigned long long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#define pte_val(x)	((x).pte_low | ((unsigned long long)(x).pte_high << 32))
#define HPAGE_SHIFT	21
#else
typedef struct { unsigned long pte_low; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#define pte_val(x)	((x).pte_low)
#define HPAGE_SHIFT	22
#endif
#define PTE_MASK	PAGE_MASK

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE	((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#endif

#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

/* Materialize both the hard and soft NX bits for a given PTE */
#ifdef CONFIG_X86_PAE
#define __PAGE_BIT_NX		9
#define __PAGE_BIT_NX_PTE	63
static inline unsigned long long pgprot_nx(pgprot_t x)
{
	unsigned long long 	p;

	p = ((unsigned long long)(x).pgprot & __supported_pte_mask) & (1ULL << __PAGE_BIT_NX);

	return p | (p << (__PAGE_BIT_NX_PTE - __PAGE_BIT_NX));
}
#else
#define pgprot_nx(x)	(0ULL)
#endif

#define __pte(x) ((pte_t) { (x) } )
#define __pmd(x) ((pmd_t) { (x) } )
#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*
 * This handles the memory map.. We could make this a config
 * option, but too many people screw it up, and too few need
 * it.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB. 
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 *
 * Note: on PAE the kernel must never go below 32 MB, we use the
 * first 8 entries of the 2-level boot pgd for PAE magic.
 */

#if CONFIG_X86_4G_VM_LAYOUT
# define __PAGE_OFFSET		(0x02000000)
# define __PAGE_OFFSET_USER	(0xff000000)
#else
# if defined(CONFIG_3GB)
#  define __PAGE_OFFSET		(0xc0000000)
#  define __PAGE_OFFSET_USER	(0xc0000000)
# elif defined(CONFIG_2GB)
#  define __PAGE_OFFSET		(0x80000000)
#  define __PAGE_OFFSET_USER	(0x80000000)
# elif defined(CONFIG_1GB)
#  define __PAGE_OFFSET		(0x40000000)
#  define __PAGE_OFFSET_USER	(0x40000000)
# endif
#endif

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
#define __VMALLOC_RESERVE	(128 << 20)

#ifndef __ASSEMBLY__

/*
 * Tell the user there is some problem. Beep too, so we can
 * see^H^H^Hhear bugs in early bootup as well!
 * The offending file and line are encoded after the "officially
 * undefined" opcode for parsing in the trap handler.
 */

#if 1	/* Set to zero for a slightly smaller kernel */
#define BUG()				\
 __asm__ __volatile__(	"ud2\n"		\
			"\t.word %c0\n"	\
			"\t.long %c1\n"	\
			 : : "i" (__LINE__), "i" (__FILE__))
#else
#define BUG() __asm__ __volatile__("ud2\n")
#endif

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

/* Pure 2^n version of get_order */
static __inline__ int get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#endif /* __ASSEMBLY__ */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)
#define PAGE_OFFSET_USER	((unsigned long)__PAGE_OFFSET_USER)
#define VMALLOC_RESERVE		((unsigned long)__VMALLOC_RESERVE)
#define __MAXMEM		(-__PAGE_OFFSET-__VMALLOC_RESERVE)
#define MAXMEM			((unsigned long)(-PAGE_OFFSET-VMALLOC_RESERVE))
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#ifndef CONFIG_DISCONTIGMEM
#define pfn_to_page(pfn)	(mem_map + (pfn))
#define page_to_pfn(page)	((unsigned long)((page) - mem_map))
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif /* !CONFIG_DISCONTIGMEM */
#define virt_to_page(kaddr)     pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)

#define VM_DATA_DEFAULT_FLAGS \
		(VM_READ | VM_WRITE | \
			((current->flags & PF_RELOCEXEC) ? 0 : VM_EXEC) | \
				VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif /* _I386_PAGE_H */
