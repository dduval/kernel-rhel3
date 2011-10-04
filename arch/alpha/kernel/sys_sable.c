/*
 *	linux/arch/alpha/kernel/sys_sable.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the Sable and Sable-Gamma systems.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_t2.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"

spinlock_t sable_irq_lock = SPIN_LOCK_UNLOCKED;

/*
 *   For SABLE, which is really baroque, we manage 40 IRQ's, but the
 *   hardware really only supports 24, not via normal ISA PIC,
 *   but cascaded custom 8259's, etc.
 *	 0-7  (char at 536)
 *	 8-15 (char at 53a)
 *	16-23 (char at 53c)
 *
 * Summary Registers (536/53a/53c):
 *
 * Bit      Meaning               Kernel IRQ
 *------------------------------------------
 * 0        PCI slot 0			34
 * 1        NCR810 (builtin)		33
 * 2        TULIP (builtin)		32
 * 3        mouse			12
 * 4        PCI slot 1			35
 * 5        PCI slot 2			36
 * 6        keyboard			1
 * 7        floppy			6
 * 8        COM2			3
 * 9        parallel port		7
 *10        EISA irq 3			-
 *11        EISA irq 4			-
 *12        EISA irq 5			5
 *13        EISA irq 6			-
 *14        EISA irq 7			-
 *15        COM1			4
 *16        EISA irq 9			9
 *17        EISA irq 10			10
 *18        EISA irq 11			11
 *19        EISA irq 12			-
 *20        EISA irq 13			-
 *21        EISA irq 14			14
 *22        NC				15
 *23        IIC				-
 */

static struct 
{
	char irq_to_mask[40];
	char mask_to_irq[40];

	/* Note mask bit is true for DISABLED irqs.  */
	unsigned long shadow_mask;
} sable_irq_swizzle = {
	{
		-1,  6, -1,  8, 15, 12,  7,  9,	/* pseudo PIC  0-7  */
		-1, 16, 17, 18,  3, -1, 21, 22,	/* pseudo PIC  8-15 */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 0-7  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 8-15 */
		 2,  1,  0,  4,  5, -1, -1, -1,	/* pseudo PCI */
	},
	{
		34, 33, 32, 12, 35, 36,  1,  6,	/* mask 0-7  */
		 3,  7, -1, -1,  5, -1, -1,  4,	/* mask 8-15  */
		 9, 10, 11, -1, -1, 14, 15, -1,	/* mask 16-23  */
	},
	-1
};

static inline void
sable_update_irq_hw(unsigned long bit, unsigned long mask)
{
	int port = 0x537;

	if (bit >= 16) {
		port = 0x53d;
		mask >>= 16;
	} else if (bit >= 8) {
		port = 0x53b;
		mask >>= 8;
	}

	outb(mask, port);
}

static inline void
sable_ack_irq_hw(unsigned long bit)
{
	int port, val1, val2;

	if (bit >= 16) {
		port = 0x53c;
		val1 = 0xE0 | (bit - 16);
		val2 = 0xE0 | 4;
	} else if (bit >= 8) {
		port = 0x53a;
		val1 = 0xE0 | (bit - 8);
		val2 = 0xE0 | 3;
	} else {
		port = 0x536;
		val1 = 0xE0 | (bit - 0);
		val2 = 0xE0 | 1;
	}

	outb(val1, port);	/* ack the slave */
	outb(val2, 0x534);	/* ack the master */
}

static inline void
sable_enable_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_irq_swizzle.irq_to_mask[irq];
	spin_lock(&sable_irq_lock);
	mask = sable_irq_swizzle.shadow_mask &= ~(1UL << bit);
	sable_update_irq_hw(bit, mask);
	spin_unlock(&sable_irq_lock);
}

static void
sable_disable_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_irq_swizzle.irq_to_mask[irq];
	spin_lock(&sable_irq_lock);
	mask = sable_irq_swizzle.shadow_mask |= 1UL << bit;
	sable_update_irq_hw(bit, mask);
	spin_unlock(&sable_irq_lock);
}

static unsigned int
sable_startup_irq(unsigned int irq)
{
	sable_enable_irq(irq);
	return 0;
}

static void
sable_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		sable_enable_irq(irq);
}

static void
sable_mask_and_ack_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_irq_swizzle.irq_to_mask[irq];
	spin_lock(&sable_irq_lock);
	mask = sable_irq_swizzle.shadow_mask |= 1UL << bit;
	sable_update_irq_hw(bit, mask);
	sable_ack_irq_hw(bit);
	spin_unlock(&sable_irq_lock);
}

static struct hw_interrupt_type sable_irq_type = {
	typename:	"SABLE",
	startup:	sable_startup_irq,
	shutdown:	sable_disable_irq,
	enable:		sable_enable_irq,
	disable:	sable_disable_irq,
	ack:		sable_mask_and_ack_irq,
	end:		sable_end_irq,
};

static void 
sable_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	/* Note that the vector reported by the SRM PALcode corresponds
	   to the interrupt mask bits, but we have to manage via more
	   normal IRQs.  */

	int bit, irq;

	bit = (vector - 0x800) >> 4;
	irq = sable_irq_swizzle.mask_to_irq[bit];
	handle_irq(irq, regs);
}

static void __init
sable_init_irq(void)
{
	long i;

	outb(-1, 0x537);	/* slave 0 */
	outb(-1, 0x53b);	/* slave 1 */
	outb(-1, 0x53d);	/* slave 2 */
	outb(0x44, 0x535);	/* enable cascades in master */

	for (i = 0; i < 40; ++i) {
		irq_desc[i].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i].handler = &sable_irq_type;
	}
	
	common_init_isa_dma();
}


/*
 * PCI Fixup configuration for ALPHA SABLE (2100) - 2100A is different ??
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  0       TULIP
 *  1       SCSI
 *  2       PCI-EISA bridge
 *  3       none
 *  4       none
 *  5       none
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
/*
 * NOTE: the IRQ assignments below are arbitrary, but need to be consistent
 * with the values in the irq swizzling tables above.
 */

static int __init
sable_map_irq(struct pci_dev *dev, u8 slota, u8 pin)
{
	int slot=slota;
        static char irq_tab[9][5] __initdata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 32+0,  32+0,  32+0,  32+0,  32+0},  /* IdSel 0,  TULIP  */
		{ 32+1,  32+1,  32+1,  32+1,  32+1},  /* IdSel 1,  SCSI   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 2,  SIO   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 3,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 4,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 5,  none   */
		{ 32+2,  32+2,  32+2,  32+2,  32+2},  /* IdSel 6,  slot 0 */
		{ 32+3,  32+3,  32+3,  32+3,  32+3},  /* IdSel 7,  slot 1 */
		{ 32+4,  32+4,  32+4,  32+4,  32+4},  /* IdSel 8,  slot 2 */
        };
	const long min_idsel = 0, max_idsel = 8, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}


/*
 * The System Vectors
 *
 * In order that T2_HAE_ADDRESS should be a constant, we play
 * these games with GAMMA_BIAS.
 */

#if defined(CONFIG_ALPHA_GENERIC) || !defined(CONFIG_ALPHA_GAMMA)
#undef GAMMA_BIAS
#define GAMMA_BIAS 0
struct alpha_machine_vector sable_mv __initmv = {
	vector_name:		"Sable",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	DO_T2_BUS,
	machine_check:		t2_machine_check,
	max_dma_address:	ALPHA_SABLE_MAX_DMA_ADDRESS,
	min_io_address:		EISA_DEFAULT_IO_BASE,
	min_mem_address:	T2_DEFAULT_MEM_BASE,

	nr_irqs:		40,
	device_interrupt:	sable_srm_device_interrupt,

	init_arch:		t2_init_arch,
	init_irq:		sable_init_irq,
	init_rtc:		common_init_rtc,
	init_pci:		common_init_pci,
	kill_arch:		NULL,
	pci_map_irq:		sable_map_irq,
	pci_swizzle:		common_swizzle,

	sys: { t2: {
	    gamma_bias:		0
	} }
};
ALIAS_MV(sable)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_GAMMA)
#undef GAMMA_BIAS
#define GAMMA_BIAS _GAMMA_BIAS
struct alpha_machine_vector sable_gamma_mv __initmv = {
	vector_name:		"Sable-Gamma",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	DO_T2_BUS,
	machine_check:		t2_machine_check,
	max_dma_address:	ALPHA_SABLE_MAX_DMA_ADDRESS,
	min_io_address:		EISA_DEFAULT_IO_BASE,
	min_mem_address:	T2_DEFAULT_MEM_BASE,

	nr_irqs:		40,
	device_interrupt:	sable_srm_device_interrupt,

	init_arch:		t2_init_arch,
	init_irq:		sable_init_irq,
	init_rtc:		common_init_rtc,
	init_pci:		common_init_pci,
	pci_map_irq:		sable_map_irq,
	pci_swizzle:		common_swizzle,

	sys: { t2: {
	    gamma_bias:		_GAMMA_BIAS
	} }
};
ALIAS_MV(sable_gamma)
#endif
