/*
 * linux/arch/i386/kernel/entry_trampoline.c
 *
 * (C) Copyright 2003 Ingo Molnar
 *
 * This file contains the needed support code for 4GB userspace
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/highmem.h>
#include <asm/desc.h>
#include <asm/atomic_kmap.h>

void *return_path_start, *return_path_end;

extern char return_path_start_marker, return_path_end_marker;

void __init init_entry_mappings(void)
{
#if CONFIG_X86_HIGH_ENTRY

	void *tramp;

	/*
	 * We need a high IDT and GDT for the 4G/4G split:
	 */
	trap_init_virtual_IDT();

	__set_fixmap(FIX_ENTRY_TRAMPOLINE, __pa((unsigned long)&entry_tramp_start), PAGE_KERNEL_EXEC);
	tramp = (void *)fix_to_virt(FIX_ENTRY_TRAMPOLINE);

	printk("mapped 4G/4G trampoline to %p.\n", tramp);
	/*
	 * Virtual kernel stack:
	 */
	BUG_ON(__kmap_atomic_vaddr(KM_VSTACK0) & 8191);
	BUG_ON(sizeof(struct desc_struct)*NR_CPUS*GDT_ENTRIES > 2*PAGE_SIZE);
	BUG_ON((unsigned int)&entry_tramp_end - (unsigned int)&entry_tramp_start > PAGE_SIZE);

	/*
	 * set up the initial thread's virtual stack related
	 * fields:
	 */
	current->thread.stack_page0 = virt_to_page((char *)current);
	current->thread.stack_page1 = virt_to_page((char *)current + PAGE_SIZE);
	current->virtual_stack = (void *)__kmap_atomic_vaddr(KM_VSTACK0);

	__kunmap_atomic_type(KM_VSTACK0);
	__kunmap_atomic_type(KM_VSTACK1);
        __kmap_atomic(current->thread.stack_page0, KM_VSTACK0);
        __kmap_atomic(current->thread.stack_page1, KM_VSTACK1);

	return_path_start = ENTRY_TRAMP_ADDR(&return_path_start_marker);
	return_path_end = ENTRY_TRAMP_ADDR(&return_path_end_marker);
#endif
	current->real_stack = (void *)current;
	current->user_pgd = NULL;
	current->thread.esp0 = (unsigned long)current->real_stack + THREAD_SIZE;

}



void __init entry_trampoline_setup(void)
{
	/*
	 * old IRQ entries set up by the boot code will still hang
	 * around - they are a sign of hw trouble anyway, now they'll
	 * produce a double fault message.
	 */
	trap_init_virtual_GDT();
}
