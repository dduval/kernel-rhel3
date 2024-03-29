#ifndef _ASM_GENAPIC_H
#define _ASM_GENAPIC_H 1

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch data struct.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */

struct genapic {
	char *name;
	u32 int_delivery_mode;
	u32 int_dest_mode;
	u32 int_delivery_dest;	/* for quick IPIs */
	int (*apic_id_registered)(void);
	u8 (*target_cpus)(void);
	void (*init_apic_ldr)(void);
	/* ipi */
	void (*send_IPI_mask)(unsigned long mask, int vector);
	void (*send_IPI_allbutself)(int vector);
	void (*send_IPI_all)(int vector);
	/* */
	unsigned int (*cpu_mask_to_apicid)(unsigned long cpumask);
};


extern struct genapic *genapic;

#endif
