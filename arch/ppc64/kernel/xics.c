/* 
 * arch/ppc64/kernel/xics.c
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/naca.h>
#include <asm/rtas.h>
#include "i8259.h"
#include "xics.h"
#include <asm/ppcdebug.h>

static unsigned int xics_startup(unsigned int irq);
static void xics_enable_irq(unsigned int irq);
static void xics_disable_irq(unsigned int irq);
static void xics_mask_and_ack_irq(unsigned int irq);
static void xics_end_irq(unsigned int irq);
static void xics_set_affinity(unsigned int irq_nr, unsigned long cpumask);

struct hw_interrupt_type xics_pic = {
	.typename = " XICS     ",
	.startup = xics_startup,
	.enable = xics_enable_irq,
	.disable = xics_disable_irq,
	.ack = xics_mask_and_ack_irq,
	.end = xics_end_irq,
	.set_affinity = xics_set_affinity
};

struct hw_interrupt_type xics_8259_pic = {
	.typename = " XICS/8259",
	.ack = xics_mask_and_ack_irq,
};

#define XICS_IPI		2
#define XICS_IRQ_SPURIOUS	0

/* Want a priority other than 0.  Various HW issues require this. */
#define	DEFAULT_PRIORITY	5

struct xics_ipl {
	union {
		u32	word;
		u8	bytes[4];
	} xirr_poll;
	union {
		u32 word;
		u8	bytes[4];
	} xirr;
	u32	dummy;
	union {
		u32	word;
		u8	bytes[4];
	} qirr;
};

static struct xics_ipl *xics_per_cpu[NR_CPUS];

static int xics_irq_8259_cascade = 0;
static int xics_irq_8259_cascade_real = 0;
static unsigned int default_server = 0xFF;
unsigned int default_distrib_server = 0;

static inline u32 physmask(u32);
extern unsigned int irq_affinity[];

/* RTAS service tokens */
int ibm_get_xive;
int ibm_set_xive;
int ibm_int_on;
int ibm_int_off;

typedef struct {
	int (*xirr_info_get)(int cpu);
	void (*xirr_info_set)(int cpu, int val);
	void (*cppr_info)(int cpu, u8 val);
	void (*qirr_info)(int cpu, u8 val);
} xics_ops;


static int pSeries_xirr_info_get(int n_cpu)
{
	return xics_per_cpu[n_cpu]->xirr.word;
}

static void pSeries_xirr_info_set(int n_cpu, int value)
{
	xics_per_cpu[n_cpu]->xirr.word = value;
}

static void pSeries_cppr_info(int n_cpu, u8 value)
{
	xics_per_cpu[n_cpu]->xirr.bytes[0] = value;
}

static void pSeries_qirr_info(int n_cpu, u8 value)
{
	xics_per_cpu[n_cpu]->qirr.bytes[0] = value;
}

static xics_ops pSeries_ops = {
	pSeries_xirr_info_get,
	pSeries_xirr_info_set,
	pSeries_cppr_info,
	pSeries_qirr_info
};

static xics_ops *ops = &pSeries_ops;
extern xics_ops pSeriesLP_ops;

/*
 * Stuff for mapping real irq numbers to virtual relatively quickly.
 * We use a radix tree of degree 64.
 */
#define RADIX_BITS	6
#define RADIX_DEGREE	(1U << RADIX_BITS)
#define RADIX_MASK	(RADIX_DEGREE - 1)

struct radix_node {
	void *ptrs[RADIX_DEGREE];
};

static struct radix_node *radix_root;
static unsigned int radix_depth;
static unsigned int radix_max = 1;

static int radix_tree_insert(unsigned int key, void *ptr)
{
	struct radix_node *p, *q;
	unsigned int i, l;

	while (key >= radix_max || radix_depth == 0) {
		/* add another level... */
		p = kmalloc(sizeof(struct radix_node), GFP_ATOMIC);
		if (p == NULL)
			return -ENOMEM;
		memset(p, 0, sizeof(struct radix_node));
		p->ptrs[0] = radix_root;
		radix_root = p;
		++radix_depth;
		radix_max <<= RADIX_BITS;
	}

	p = radix_root;
	/* note radix_depth > 0 */
	for (l = radix_depth; l > 1; --l) {
		i = (key >> ((l - 1) * RADIX_BITS)) & RADIX_MASK;
		q = p;
		p = p->ptrs[i];
		if (p != NULL)
			continue;
		p = kmalloc(sizeof(struct radix_node), GFP_ATOMIC);
		if (p == NULL)
			return -ENOMEM;
		memset(p, 0, sizeof(struct radix_node));
		q->ptrs[i] = p;
	}

	p->ptrs[key & RADIX_MASK] = ptr;
	return 0;
}

static void *radix_tree_lookup(unsigned int key)
{
	struct radix_node *p;
	unsigned int i, l;

	p = radix_root;
	for (l = radix_depth; l > 0 && p != NULL; --l) {
		i = (key >> ((l - 1) * RADIX_BITS)) & RADIX_MASK;
		p = p->ptrs[i];
	}
	return (void *) p;
}

static unsigned int xics_startup(unsigned int virq)
{
	virq = irq_offset_down(virq);
	if (radix_tree_insert(virt_irq_to_real(virq),
			      &virt_irq_to_real_map[virq]) == -ENOMEM)
		printk(KERN_CRIT "Out of memory creating real -> virtual"
		       " IRQ mapping for irq %u (real 0x%x)\n",
		       virq, virt_irq_to_real(virq));
	return 0;	/* return value is ignored */
}

static unsigned int real_irq_to_virt(unsigned int real_irq)
{
	unsigned int *ptr;
	unsigned int virq;

	ptr = radix_tree_lookup(real_irq);
	if (ptr == NULL)
		return NO_IRQ;
	virq = ptr - virt_irq_to_real_map;
	if (virq >= NR_IRQS)
		return NO_IRQ;		/* something has gone badly wrong */
	return virq;
}

/*
 * Find first logical cpu and return its physical cpu number
 */
static inline u32 physmask(u32 cpumask)
{
	int i;

	for (i = 0; i < smp_num_cpus; ++i, cpumask >>= 1) {
		if (cpumask & 1)
			return get_hard_smp_processor_id(i);
	}

	printk(KERN_ERR "xics_set_affinity: invalid irq mask\n");

	return default_distrib_server;
}

static unsigned int get_irq_server(unsigned int irq)
{
	unsigned int server;
	
#ifdef CONFIG_IRQ_ALL_CPUS
	if ((smp_num_cpus == systemcfg->processorCount) &&
	    (smp_threads_ready)) {
		/* Retain the affinity setting specified */
		if (irq_affinity[irq] == 0xffffffff)
			server = default_distrib_server;
		else
			server = physmask(irq_affinity[irq]);
	} else
#endif
		server = default_server;
	return server;

}

static void xics_enable_irq(unsigned int virq)
{
	u_int		irq;
	long	        call_status;
	unsigned int    interrupt_server;

	irq = virt_irq_to_real(irq_offset_down(virq));
	if (irq == XICS_IPI)
		return;

	interrupt_server = get_irq_server(virq);
	call_status = rtas_call(ibm_set_xive, 3, 1, NULL,
				irq, interrupt_server, DEFAULT_PRIORITY);

	if (call_status != 0) {
		printk(KERN_ERR "xics_enable_irq: irq=0x%x: "
		       "ibm_set_xive returned %lx\n", irq, call_status);
		return;
	}

	/* Now unmask the interrupt (often a no-op) */
	call_status = rtas_call(ibm_int_on, 1, 1, NULL, irq);
	if( call_status != 0 ) {
		printk(KERN_ERR "xics_enable_irq: irq=%x: ibm_int_on "
		       "returned %lx\n", irq, call_status);
		return;
	}
}

static void xics_disable_real_irq(unsigned int irq)
{
	long call_status;
	unsigned int server;

	call_status = rtas_call(ibm_int_off, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_real_irq: irq=%x: "
		       "ibm_int_off returned %lx\n", irq, call_status);
		return;
	}

	/* Have to set XIVE to 0xff to be able to remove a slot */
	call_status = rtas_call(ibm_set_xive, 3, 1, NULL, irq,
				default_server, 0xff);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_irq: irq=%x: ibm_set_xive(0xff)"
		       " returned %lx\n", irq, call_status);
	}
}

static void xics_disable_irq(unsigned int virq)
{
	unsigned int irq;

	irq = virt_irq_to_real(irq_offset_down(virq));
	xics_disable_real_irq(irq);
}

static void xics_end_irq(unsigned int irq)
{
	int cpu = smp_processor_id();

	ops->cppr_info(cpu, 0); /* actually the value overwritten by ack */
	iosync();
	ops->xirr_info_set(cpu, ((0xff<<24) |
				 (virt_irq_to_real(irq_offset_down(irq)))));
	iosync();
}

static void xics_mask_and_ack_irq(unsigned int irq)
{
	int cpu = smp_processor_id();

	if (irq < NUM_ISA_INTERRUPTS) {
		i8259_pic.ack(irq);
		iosync();
		ops->xirr_info_set(cpu, ((0xff<<24) |
					 xics_irq_8259_cascade_real));
		iosync();
	}
}

int xics_get_irq(struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();
	unsigned int vec;
	int irq;

	vec = ops->xirr_info_get(cpu);
	/*  (vec >> 24) == old priority */
	vec &= 0x00ffffff;
	/* for sanity, this had better be < NR_IRQS - 16 */
	if (vec == xics_irq_8259_cascade_real) {
		irq = i8259_irq(cpu);
		if (irq == -1) {
			/* Spurious cascaded interrupt.  Still must ack xics */
			xics_end_irq(irq_offset_up(xics_irq_8259_cascade));

			irq = -1;
		}
	} else if (vec == XICS_IRQ_SPURIOUS) {
		irq = -1;
	} else {
		irq = real_irq_to_virt(vec);
		if (irq == NO_IRQ)
			irq = real_irq_to_virt_slow(vec);
		if (irq == NO_IRQ) {
			printk(KERN_ERR "Interrupt 0x%x (real) is invalid,"
			       " disabling it.\n", vec);
			xics_disable_real_irq(vec);
		} else
			irq = irq_offset_up(irq);
	}
	return irq;
}


#ifdef CONFIG_SMP
void xics_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	extern volatile unsigned long xics_ipi_message[];
	int cpu = smp_processor_id();

	ops->qirr_info(cpu, 0xff);
	while (xics_ipi_message[cpu]) {
		if (test_and_clear_bit(PPC_MSG_CALL_FUNCTION, &xics_ipi_message[cpu])) {
			mb();
			smp_message_recv(PPC_MSG_CALL_FUNCTION, regs);
		}
		if (test_and_clear_bit(PPC_MSG_RESCHEDULE, &xics_ipi_message[cpu])) {
			mb();
			smp_message_recv(PPC_MSG_RESCHEDULE, regs);
		}
#ifdef CONFIG_XMON
		if (test_and_clear_bit(PPC_MSG_XMON_BREAK, &xics_ipi_message[cpu])) {
			mb();
			smp_message_recv(PPC_MSG_XMON_BREAK, regs);
		}
#endif
	}
}

void xics_cause_IPI(int cpu)
{
	ops->qirr_info(cpu,0) ;
}

void xics_setup_cpu(void)
{
	int cpu = smp_processor_id();

	ops->cppr_info(cpu, 0xff);
	iosync();
}

void xics_request_IPI(void)
{
	if (request_irq(irq_offset_up(XICS_IPI), xics_ipi_action, 0, "IPI", 0))
		printk(KERN_CRIT "Unable to get XICS irq 2 for IPI\n");
}
#endif /* CONFIG_SMP */

void xics_init_IRQ(void)
{
	int i;
	unsigned long intr_size = 0;
	struct device_node *np;
	uint *ireg, ilen, indx = 0;
	unsigned long intr_base = 0;
	struct xics_interrupt_node {
		unsigned long addr;
		unsigned long size;
	} inodes[NR_CPUS]; 

	ppc64_boot_msg(0x20, "XICS Init");

	ibm_get_xive = rtas_token("ibm,get-xive");
	ibm_set_xive = rtas_token("ibm,set-xive");
	ibm_int_on  = rtas_token("ibm,int-on");
	ibm_int_off = rtas_token("ibm,int-off");

	np = find_type_devices("PowerPC-External-Interrupt-Presentation");
	if (!np) {
		printk(KERN_WARNING "Can't find Interrupt Presentation\n");
		udbg_printf("Can't find Interrupt Presentation\n");
		while (1);
	}
nextnode:
	ireg = (uint *)get_property(np, "ibm,interrupt-server-ranges", 0);
	if (ireg) {
		/*
		 * set node starting index for this node
		 */
		indx = *ireg;
	}

	ireg = (uint *)get_property(np, "reg", &ilen);
	if (!ireg) {
		printk(KERN_WARNING "Can't find Interrupt Reg Property\n");
		udbg_printf("Can't find Interrupt Reg Property\n");
		while (1);
	}
	
	while (ilen) {
		inodes[indx].addr = (unsigned long long)*ireg++ << 32;
		ilen -= sizeof(uint);
		inodes[indx].addr |= *ireg++;
		ilen -= sizeof(uint);
		inodes[indx].size = (unsigned long long)*ireg++ << 32;
		ilen -= sizeof(uint);
		inodes[indx].size |= *ireg++;
		ilen -= sizeof(uint);
		indx++;
		if (indx >= NR_CPUS) break;
	}

	np = np->next;
	if ((indx < NR_CPUS) && np) goto nextnode;

	/* Find the server numbers for the boot cpu. */
	for (np = find_type_devices("cpu"); np; np = np->next) {
		ireg = (uint *)get_property(np, "reg", &ilen);
		if (ireg && ireg[0] == hard_smp_processor_id()) {
			ireg = (uint *)get_property(np, "ibm,ppc-interrupt-gserver#s", &ilen);
			i = ilen / sizeof(int);
			if (ireg && i > 0) {
				default_server = ireg[0];
				default_distrib_server = ireg[i-1]; /* take last element */
			}
			break;
		}
	}

	intr_base = inodes[0].addr;
	intr_size = (ulong)inodes[0].size;

	np = find_type_devices("interrupt-controller");
	if (!np) {
		printk(KERN_WARNING "xics:  no ISA Interrupt Controller\n");
		xics_irq_8259_cascade_real = -1;
		xics_irq_8259_cascade = -1;
	} else {
		ireg = (uint *) get_property(np, "interrupts", 0);
		if (!ireg) {
			printk(KERN_WARNING "Can't find ISA Interrupts Property\n");
			udbg_printf("Can't find ISA Interrupts Property\n");
			while (1);
		}
		xics_irq_8259_cascade_real = *ireg;
		xics_irq_8259_cascade
			= virt_irq_create_mapping(xics_irq_8259_cascade_real);
	}

	if (systemcfg->platform == PLATFORM_PSERIES) {
#ifdef CONFIG_SMP
		for (i = 0; i < systemcfg->processorCount; ++i) {
			xics_per_cpu[i] =
			  __ioremap((ulong)inodes[get_hard_smp_processor_id(i)].addr, 
				  (ulong)inodes[get_hard_smp_processor_id(i)].size, _PAGE_NO_CACHE);
		}
#else
		xics_per_cpu[0] = __ioremap((ulong)intr_base, intr_size, _PAGE_NO_CACHE);
#endif /* CONFIG_SMP */
#ifdef CONFIG_PPC_PSERIES
	/* actually iSeries does not use any of xics...but it has link dependencies
	 * for now, except this new one...
	 */
	} else if (systemcfg->platform == PLATFORM_PSERIES_LPAR) {
		ops = &pSeriesLP_ops;
#endif
	}

	xics_8259_pic.enable = i8259_pic.enable;
	xics_8259_pic.disable = i8259_pic.disable;
	for (i = 0; i < 16; ++i)
		irq_desc[i].handler = &xics_8259_pic;
	for (; i < NR_IRQS; ++i)
		irq_desc[i].handler = &xics_pic;

	ops->cppr_info(0, 0xff);
	iosync();

#ifdef CONFIG_SMP
	virt_irq_to_real_map[XICS_IPI] = XICS_IPI;
	irq_desc[irq_offset_up(XICS_IPI)].status |= IRQ_PER_CPU;
#endif
	ppc64_boot_msg(0x21, "XICS Done");
}

void xics_isa_init(void)
{
	if (request_irq(irq_offset_up(xics_irq_8259_cascade), no_action,
			0, "8259 cascade", 0))
		printk(KERN_ERR "xics_init_IRQ: couldn't get 8259 cascade\n");
	i8259_init();
}

static void xics_set_affinity(unsigned int virq, unsigned long cpumask)
{
        irq_desc_t *desc = irq_desc + virq;
	unsigned int irq;
	unsigned long flags;
	long status;
	unsigned long xics_status[2];
	u32 newmask;

	irq = virt_irq_to_real(irq_offset_down(virq));
	if (irq == XICS_IPI)
		return;

        spin_lock_irqsave(&desc->lock, flags);

	status = rtas_call(ibm_get_xive, 1, 3, (void *)&xics_status, irq);

	if (status) {
		printk("xics_set_affinity: irq=%d ibm,get-xive returns %ld\n",
			irq, status);
		goto out;
	}

	/* For the moment only implement delivery to all cpus or one cpu */
	if (cpumask == 0xffffffff)
		newmask = default_distrib_server;
	else
		newmask = physmask(cpumask);

	status = rtas_call(ibm_set_xive, 3, 1, NULL,
				irq, newmask, xics_status[1]);

	if (status) {
		printk("xics_set_affinity irq=%d ibm,set-xive returns %ld\n",
			irq, status);
		goto out;
	}

out:
        spin_unlock_irqrestore(&desc->lock, flags);
}
