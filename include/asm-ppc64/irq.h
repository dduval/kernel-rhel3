#ifdef __KERNEL__
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/atomic.h>

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * this is the maximum number of virtual irqs we will use.
 */
#define NR_IRQS			512

/* Token to indicate an unassigned or invalid IRQ number */
#define NO_IRQ			(-1)

/* Interrupt numbers are virtual in case they are sparsely
 * distributed by the hardware.
 */
extern unsigned int virt_irq_to_real_map[NR_IRQS];

/* Create a mapping for a real_irq if it doesn't already exist.
 * Returns the virtual irq.
 */
int virt_irq_create_mapping(unsigned int real_irq);

/* This maps virtual irqs to real */
static inline unsigned int virt_irq_to_real(int virt_irq)
{
	return virt_irq_to_real_map[virt_irq];
}

/* This maps real irqs to virtual by looking in the virt_irq_to_real_map. */
extern int real_irq_to_virt_slow(unsigned int real_irq);

/*
 * This gets called from serial.c, which is now used on
 * powermacs as well as prep/chrp boxes.
 * Prep and chrp both have cascaded 8259 PICs.
 */
static __inline__ int irq_cannonicalize(int irq)
{
	return irq;
}

/*
 * Because many systems have two overlapping names spaces for
 * interrupts (ISA and XICS for example), and the ISA interrupts
 * have historically not been easy to renumber, we allow ISA
 * interrupts to take values 0 - 15, and shift up the remaining
 * interrupts by 0x10.
 *
 * This would be nice to remove at some point as it adds confusion
 * and adds a nasty end case if any platform native interrupts have
 * values within 0x10 of the end of that namespace.
 */

#define NUM_ISA_INTERRUPTS	0x10

extern inline int irq_offset_up(int irq)
{
	return(irq + NUM_ISA_INTERRUPTS);
}

extern inline int irq_offset_down(int irq)
{
	return(irq - NUM_ISA_INTERRUPTS);
}

extern inline int irq_offset_value(void)
{
	return NUM_ISA_INTERRUPTS;
}

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
