/*
 * IA32 helper functions
 *
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2000 Asit K. Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 06/16/00	A. Mallick	added csd/ssd/tssd for ia32 thread context
 * 02/19/01	D. Mosberger	dropped tssd; it's not needed
 * 09/14/01	D. Mosberger	fixed memory management for gdt/tss page
 * 09/29/01	D. Mosberger	added ia32_load_segment_descriptors()
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/sched.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/ia32.h>
#include <asm/uaccess.h>

extern void die_if_kernel (char *str, struct pt_regs *regs, long err);

struct exec_domain ia32_exec_domain;
struct page *ia32_shared_page[(PAGE_ALIGN(IA32_PAGE_SIZE)/PAGE_SIZE) * NR_CPUS];
unsigned long *ia32_boot_gdt;
unsigned long *cpu_gdt_table[NR_CPUS];

static unsigned long
load_desc (u16 selector)
{
	unsigned long *table, limit, index;

	if (!selector)
		return 0;
	if (selector & IA32_SEGSEL_TI) {
		table = (unsigned long *) IA32_LDT_OFFSET;
		limit = IA32_LDT_ENTRIES;
	} else {
		table = cpu_gdt_table[smp_processor_id()];
		limit = IA32_PAGE_SIZE / sizeof(ia32_boot_gdt[0]);
	}
	index = selector >> IA32_SEGSEL_INDEX_SHIFT;
	if (index >= limit)
		return 0;
	return IA32_SEG_UNSCRAMBLE(table[index]);
}

void
ia32_load_segment_descriptors (struct task_struct *task)
{
	struct pt_regs *regs = ia64_task_regs(task);

	/* Setup the segment descriptors */
	regs->r24 = load_desc(regs->r16 >> 16);		/* ESD */
	regs->r27 = load_desc(regs->r16 >>  0);		/* DSD */
	regs->r28 = load_desc(regs->r16 >> 32);		/* FSD */
	regs->r29 = load_desc(regs->r16 >> 48);		/* GSD */
	task->thread.csd = load_desc(regs->r17 >>  0);	/* CSD */
	task->thread.ssd = load_desc(regs->r17 >> 16);	/* SSD */
}

void
ia32_clone_tls(struct task_struct *child, struct pt_regs *childregs)
{
	struct desc_struct *desc;
	struct ia32_user_desc info;
	int idx;

	if (copy_from_user(&info, (void *)(childregs->r14 & 0xffffffff),
			   sizeof(info)))
		return -EFAULT;
	if (LDT_empty(&info))
		return -EINVAL;

	idx = info.entry_number;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = child->tls_array + idx - GDT_ENTRY_TLS_MIN;
	desc->a = LDT_entry_a(&info);
	desc->b = LDT_entry_b(&info);

	/* XXX: can this be done in a cleaner way ? */
	load_TLS(child, smp_processor_id());
	ia32_load_segment_descriptors(child);
	load_TLS(current, smp_processor_id());
}

void
ia32_save_state (struct task_struct *t)
{
	unsigned long eflag, fsr, fcr, fir, fdr, csd, ssd;

	asm ("mov %0=ar.eflag;"
	     "mov %1=ar.fsr;"
	     "mov %2=ar.fcr;"
	     "mov %3=ar.fir;"
	     "mov %4=ar.fdr;"
	     "mov %5=ar.csd;"
	     "mov %6=ar.ssd;"
	     : "=r"(eflag), "=r"(fsr), "=r"(fcr), "=r"(fir), "=r"(fdr), "=r"(csd), "=r"(ssd));
	t->thread.eflag = eflag;
	t->thread.fsr = fsr;
	t->thread.fcr = fcr;
	t->thread.fir = fir;
	t->thread.fdr = fdr;
	t->thread.csd = csd;
	t->thread.ssd = ssd;
	ia64_set_kr(IA64_KR_IO_BASE, t->thread.old_iob);
	ia64_set_kr(IA64_KR_TSSD, t->thread.old_k1);

}

void
ia32_load_state (struct task_struct *t)
{
	unsigned long eflag, fsr, fcr, fir, fdr, csd, ssd, tssd;
	struct pt_regs *regs = ia64_task_regs(t);

	eflag = t->thread.eflag;
	fsr = t->thread.fsr;
	fcr = t->thread.fcr;
	fir = t->thread.fir;
	fdr = t->thread.fdr;
	csd = t->thread.csd;
	ssd = t->thread.ssd;
	tssd = load_desc(_TSS);					/* TSSD */

	asm volatile ("mov ar.eflag=%0;"
		      "mov ar.fsr=%1;"
		      "mov ar.fcr=%2;"
		      "mov ar.fir=%3;"
		      "mov ar.fdr=%4;"
		      "mov ar.csd=%5;"
		      "mov ar.ssd=%6;"
		      :: "r"(eflag), "r"(fsr), "r"(fcr), "r"(fir), "r"(fdr), "r"(csd), "r"(ssd));
	current->thread.old_iob = ia64_get_kr(IA64_KR_IO_BASE);
	current->thread.old_k1 = ia64_get_kr(IA64_KR_TSSD);
	ia64_set_kr(IA64_KR_IO_BASE, IA32_IOBASE);
	ia64_set_kr(IA64_KR_TSSD, tssd);

	regs->r17 = (_TSS << 48) | (_LDT << 32) | (__u32) regs->r17;
	regs->r30 = load_desc(_LDT);				/* LDTD */
	load_TLS(t, smp_processor_id());

}

/*
 * Setup IA32 GDT and TSS
 */
void
ia32_gdt_init (void)
{
	int cpu = smp_processor_id();

	ia32_shared_page[cpu] = alloc_page(GFP_KERNEL);
	cpu_gdt_table[cpu] = page_address(ia32_shared_page[cpu]);

	printk("allocating GDT on cpu: %d\n", cpu);

	/* Copy from the boot cpu's GDT */
	memcpy(cpu_gdt_table[cpu], ia32_boot_gdt, PAGE_SIZE);
}


/*
 * Setup IA32 GDT and TSS
 */
void
ia32_boot_gdt_init (void)
{
	unsigned long ldt_size;

	ia32_shared_page[0] = alloc_page(GFP_KERNEL);
	ia32_boot_gdt = page_address(ia32_shared_page[0]);
	cpu_gdt_table[0] = ia32_boot_gdt;

	/* CS descriptor in IA-32 (scrambled) format */
	ia32_boot_gdt[__USER_CS >> 3] = IA32_SEG_DESCRIPTOR(0, (IA32_PAGE_OFFSET-1) >> IA32_PAGE_SHIFT,
						       0xb, 1, 3, 1, 1, 1, 1);

	/* DS descriptor in IA-32 (scrambled) format */
	ia32_boot_gdt[__USER_DS >> 3] = IA32_SEG_DESCRIPTOR(0, (IA32_PAGE_OFFSET-1) >> IA32_PAGE_SHIFT,
						       0x3, 1, 3, 1, 1, 1, 1);

	ldt_size = PAGE_ALIGN(IA32_LDT_ENTRIES*IA32_LDT_ENTRY_SIZE);
	ia32_boot_gdt[TSS_ENTRY]
		= IA32_SEG_DESCRIPTOR(IA32_TSS_OFFSET, 235,
				      0xb, 0, 3, 1, 1, 1, 0);
	ia32_boot_gdt[LDT_ENTRY]
		= IA32_SEG_DESCRIPTOR(IA32_LDT_OFFSET, ldt_size - 1,
				      0x2, 0, 3, 1, 1, 1, 0);
}

/*
 * Handle bad IA32 interrupt via syscall
 */
void
ia32_bad_interrupt (unsigned long int_num, struct pt_regs *regs)
{
	siginfo_t siginfo;

	die_if_kernel("Bad IA-32 interrupt", regs, int_num);

	siginfo.si_signo = SIGTRAP;
	siginfo.si_errno = int_num;	/* XXX is it OK to abuse si_errno like this? */
	siginfo.si_flags = 0;
	siginfo.si_isr = 0;
	siginfo.si_addr = 0;
	siginfo.si_imm = 0;
	siginfo.si_code = TRAP_BRKPT;
	force_sig_info(SIGTRAP, &siginfo, current);
}

static int __init
ia32_init (void)
{
	ia32_exec_domain.name = "Linux/x86";
	ia32_exec_domain.handler = NULL;
	ia32_exec_domain.pers_low = PER_LINUX32;
	ia32_exec_domain.pers_high = PER_LINUX32;
	ia32_exec_domain.signal_map = default_exec_domain.signal_map;
	ia32_exec_domain.signal_invmap = default_exec_domain.signal_invmap;
	register_exec_domain(&ia32_exec_domain);
	return 0;
}

__initcall(ia32_init);
