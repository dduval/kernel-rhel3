#ifndef _ASM_I386_MODULE_H
#define _ASM_I386_MODULE_H
/*
 * This file contains the i386 architecture specific module code.
 */

#define module_map(x)		__vmalloc((x), GFP_KERNEL | __GFP_HIGHMEM, \
					  PAGE_KERNEL_EXEC)
#define module_unmap(x)		vfree(x)
#define module_arch_init(x)	(0)
#define arch_init_modules(x)	do { } while (0)

#endif /* _ASM_I386_MODULE_H */
