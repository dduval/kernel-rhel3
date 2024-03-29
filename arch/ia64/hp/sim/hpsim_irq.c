/*
 * Platform dependent support for HP simulator.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 * Copyright (C) 1998-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/irq.h>

static unsigned int
hpsim_irq_startup (unsigned int irq)
{
	return 0;
}

static void
hpsim_irq_noop (unsigned int irq)
{
}

static struct hw_interrupt_type irq_type_hp_sim = {
	typename:	"hpsim",
	startup:	hpsim_irq_startup,
	shutdown:	hpsim_irq_noop,
	enable:		hpsim_irq_noop,
	disable:	hpsim_irq_noop,
	ack:		hpsim_irq_noop,
	end:		hpsim_irq_noop,
	set_affinity:	(void (*)(unsigned int, unsigned long)) hpsim_irq_noop,
};

void __init
hpsim_irq_init (void)
{
	irq_desc_t *idesc;
	int i;

	for (i = 0; i < NR_IRQS; ++i) {
		idesc = ia64_irq_desc(i);
		if (idesc->handler == &no_irq_type)
			idesc->handler = &irq_type_hp_sim;
	}
}
