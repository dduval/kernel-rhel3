#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

#define DOUBLEFAULT_STACKSIZE (1024)
static unsigned long doublefault_stack[DOUBLEFAULT_STACKSIZE];
#define STACK_START (unsigned long)(doublefault_stack+DOUBLEFAULT_STACKSIZE)

static void doublefault_fn(void)
{
	struct Xgt_desc_struct gdt_desc = {0, 0};
	unsigned long gdt, tss;
	struct tss_struct *t;

	__asm__ __volatile__("sgdt %0": "=m" (gdt_desc): :"memory");
	gdt = gdt_desc.address;

	printk("double fault, gdt at %08lx [%d bytes]\n", gdt, gdt_desc.size);

	gdt += GDT_ENTRY_TSS << 3;
	tss = *(u16 *)(gdt+2);
	tss += *(u8 *)(gdt+4) << 16;
	tss += *(u8 *)(gdt+7) << 24;
	printk("double fault, tss at %08lx\n", tss);

	t = (struct tss_struct *)tss;

	printk("eip = %08lx, esp = %08lx, esp0 = %08lx\n",
		t->eip, t->esp, t->esp0);
	printk("cr3 = %08lx\n", t->__cr3);
	printk("eax = %08lx, ebx = %08lx, ecx = %08lx, edx = %08lx\n",
		t->eax, t->ebx, t->ecx, t->edx);
	printk("esi = %08lx, edi = %08lx\n", t->esi, t->edi);
	for (;;) /* nothing */;
}

struct tss_struct doublefault_tss __cacheline_aligned = {
	.esp0		= STACK_START,
	.ss0		= __KERNEL_DS,
	.ldt		= 0,
	.bitmap		= INVALID_IO_BITMAP_OFFSET,
	.io_bitmap	= { [0 ... IO_BITMAP_SIZE ] = ~0 },

	.eip		= (unsigned long) doublefault_fn,
	.eflags		= 0x00000082,
	.esp		= STACK_START,
	.es		= __USER_DS,
	.cs		= __KERNEL_CS,
	.ss		= __KERNEL_DS,
	.ds		= __USER_DS,

	.__cr3		= __pa(swapper_pg_dir)
};

