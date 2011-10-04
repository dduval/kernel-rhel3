/* 
 * X86-64 specific CPU setup.
 * Copyright (C) 1995  Linus Torvalds
 * Copyright 2001, 2002 SuSE Labs / Andi Kleen.
 * See setup.c for older changelog.
 * $Id: setup64.c,v 1.23 2003/05/16 14:22:27 ak Exp $
 */ 
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/pda.h>
#include <asm/pda.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/bitops.h>
#include <asm/atomic.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/mman.h>

char x86_boot_params[2048] __initdata = {0,};

static unsigned long cpu_initialized __initdata = 0;

struct x8664_pda cpu_pda[NR_CPUS] __cacheline_aligned; 

extern void system_call(void); 
extern void ia32_cstar_target(void); 

extern struct desc_ptr cpu_gdt_descr[];
struct desc_ptr idt_descr = { 256 * 16, (unsigned long) idt_table }; 

/* When you change the default make sure the no EFER path below sets the 
   correct flags everywhere. */
unsigned long __supported_pte_mask = ~0UL; 
static int do_not_nx __initdata = 0;  
unsigned long vm_stack_flags = __VM_STACK_FLAGS; 
unsigned long vm_stack_flags32 = __VM_STACK_FLAGS; 
unsigned long vm_data_default_flags = __VM_DATA_DEFAULT_FLAGS; 
unsigned long vm_data_default_flags32 = __VM_DATA_DEFAULT_FLAGS; 
unsigned long vm_force_exec32 = PROT_EXEC; 
 
char boot_cpu_stack[IRQSTACKSIZE] __cacheline_aligned;

/* noexec=on|off

on	Enable
off	Disable
noforce (default) Don't enable by default for heap/stack/data, 
	but allow PROT_EXEC to be effective

*/ 

static int __init nonx_setup(char *str)
{
	if (!strncmp(str, "on",3)) { 
  		__supported_pte_mask |= _PAGE_NX; 
		do_not_nx = 0; 
		vm_data_default_flags &= ~VM_EXEC; 
		vm_stack_flags &= ~VM_EXEC;  
		exec_shield = 2;
	} else if (!strncmp(str, "noforce",7) || !strncmp(str,"off",3)) { 
		do_not_nx = (str[0] == 'o');
		if (do_not_nx)
			__supported_pte_mask &= ~_PAGE_NX; 
		vm_data_default_flags |= VM_EXEC; 
		vm_stack_flags |= VM_EXEC;
		exec_shield = do_not_nx ? 0 : 1;
	}
	return 1;
} 

/* noexec32=opt{,opt} 

Control the no exec default for 32bit processes. Can be also overwritten
per executable using ELF header flags (e.g. needed for the X server)
Requires noexec=on or noexec=noforce to be effective.

Valid options: 
   all,on    Heap,stack,data is non executable. 	
   off       (default) Heap,stack,data is executable
   stack     Stack is non executable, heap/data is.
   force     Don't imply PROT_EXEC for PROT_READ 
   compat    (default) Imply PROT_EXEC for PROT_READ

*/

int exec_shield32 = 1;

static int __init nonx32_setup(char *str)
{
	char *s;
	while ((s = strsep(&str, ",")) != NULL) { 
		if (!strcmp(s, "all") || !strcmp(s,"on")) {
			vm_data_default_flags32 &= ~VM_EXEC; 
			vm_stack_flags32 &= ~VM_EXEC;  
			exec_shield32 = 2;
		} else if (!strcmp(s, "off")) { 
			vm_data_default_flags32 |= VM_EXEC; 
			vm_stack_flags32 |= VM_EXEC;  
			exec_shield32 = 0;
		} else if (!strcmp(s, "stack")) { 
			vm_data_default_flags32 |= VM_EXEC; 
			vm_stack_flags32 &= ~VM_EXEC;  		
			exec_shield32 = 1;
		} else if (!strcmp(s, "force")) { 
			vm_force_exec32 = 0; 
			exec_shield32 = 1;
		} else if (!strcmp(s, "compat")) { 
			vm_force_exec32 = PROT_EXEC;
			exec_shield32 = 1;
		} 
  	} 
  	return 1;
} 

__setup("noexec=", nonx_setup); 
__setup("noexec32=", nonx32_setup);

void pda_init(int cpu)
{ 
        pml4_t *level4;
	
	if (cpu == 0) {
		/* others are initialized in smpboot.c */
		cpu_pda[cpu].pcurrent = &init_task;
		cpu_pda[cpu].irqstackptr = boot_cpu_stack; 
		level4 = init_level4_pgt; 
	} else {
		cpu_pda[cpu].irqstackptr = (char *)
			__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
		if (!cpu_pda[cpu].irqstackptr)
			panic("cannot allocate irqstack for cpu %d\n", cpu); 
		level4 = (pml4_t *)__get_free_pages(GFP_ATOMIC, 0); 
	}   
	if (!level4) 
		panic("Cannot allocate top level page for cpu %d", cpu); 

	cpu_pda[cpu].level4_pgt = (unsigned long *)level4; 
	if (level4 != init_level4_pgt)
		memcpy(level4, &init_level4_pgt, PAGE_SIZE); 
	set_pml4(level4 + 510, 
		 mk_kernel_pml4(__pa_symbol(boot_vmalloc_pgt), KERNPG_TABLE));
	asm volatile("movq %0,%%cr3" :: "r" (__pa(level4))); 

	cpu_pda[cpu].irqstackptr += IRQSTACKSIZE-64;
	cpu_pda[cpu].cpunumber = cpu; 
	cpu_pda[cpu].irqcount = -1;

	asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0)); 
	wrmsrl(MSR_GS_BASE, cpu_pda + cpu);
} 

char boot_exception_stacks[N_EXCEPTION_STACKS*EXCEPTION_STKSZ];

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init.
 */
void __init cpu_init (void)
{
#ifdef CONFIG_SMP
	int cpu = stack_smp_processor_id();
#else
	int cpu = smp_processor_id();
#endif
	struct tss_struct * t = &init_tss[cpu];
	unsigned long v, efer; 	
	char *estack;

	/* CPU 0 is initialised in head64.c */
	if (cpu != 0)
		pda_init(cpu);

	if (test_and_set_bit(cpu, &cpu_initialized))
		panic("CPU#%d already initialized!\n", cpu);

	printk("Initializing CPU#%d\n", cpu);
	
	clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
	if (cpu) {
		memcpy(cpu_gdt_table[cpu], cpu_gdt_table[0], GDT_SIZE);
		cpu_gdt_descr[cpu].size = GDT_SIZE-1;
		cpu_gdt_descr[cpu].address = (unsigned long)cpu_gdt_table[cpu];
	}	

	__asm__ __volatile__("lgdt %0": "=m" (cpu_gdt_descr[cpu]));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	/*
	 * Delete NT
	 */

	asm volatile("pushfq ; popq %%rax ; btr $14,%%rax ; pushq %%rax ; popfq" ::: "eax");

	/* 
	 * LSTAR and STAR live in a bit strange symbiosis.
	 * They both write to the same internal register. STAR allows to set CS/DS
	 * but only a 32bit target. LSTAR sets the 64bit rip. 	 
	 */ 
	wrmsrl(MSR_STAR,  ((u64)__USER32_CS)<<48  | ((u64)__KERNEL_CS)<<32); 
	wrmsrl(MSR_LSTAR, system_call); 

#ifdef CONFIG_IA32_EMULATION   		
	wrmsrl(MSR_CSTAR, ia32_cstar_target); 
#endif

	rdmsrl(MSR_EFER, efer);
	if (!(efer & EFER_NX) || do_not_nx)
		__supported_pte_mask &= ~_PAGE_NX;

	t->io_map_base = INVALID_IO_BITMAP_OFFSET;	
	memset(t->io_bitmap, 0xff, sizeof(t->io_bitmap));

	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK, EF_TF|EF_DF|EF_IE|EF_NT);

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier(); 

	/*
	 * set up and load the per-CPU TSS
	 */
	estack = boot_exception_stacks + EXCEPTION_STKSZ;
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		if (cpu == 0) {
			t->ist[v] = (u64)estack;
			estack += EXCEPTION_STKSZ;
		} else {
			estack = (char *)__get_free_pages(GFP_ATOMIC, EXCEPTION_STK_ORDER);
			if (!estack)
				panic("Can't allocate exception stack %ld for CPU %d\n", v, cpu);
			t->ist[v] = (u64)estack + EXCEPTION_STKSZ;		
		}
	}

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if(current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current, cpu);

	set_tss_desc(cpu, t);
	load_TR_desc();
	load_LDT(&init_mm);

	/*
	 * Clear all 6 debug registers:
	 */

	set_debug(0UL, 0);
	set_debug(0UL, 1);
	set_debug(0UL, 2);
	set_debug(0UL, 3);
	set_debug(0UL, 6);
	set_debug(0UL, 7);

	/*
	 * Force FPU initialization:
	 */
	current->flags &= ~PF_USEDFPU;
	current->used_math = 0;
	stts();
}
