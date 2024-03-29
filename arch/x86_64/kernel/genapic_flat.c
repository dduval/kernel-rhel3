/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Flat APIC subarch code.  Maximum 8 CPUs, logical delivery.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/smp.h>
#include <asm/ipi.h>


static u8 flat_target_cpus(void)
{
	return cpu_online_map;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void flat_init_apic_ldr(void)
{
	unsigned long val;
	unsigned long num, id;

	num = smp_processor_id();
	id = 1UL << num;
	x86_cpu_to_log_apicid[num] = id;
	apic_write_around(APIC_DFR, APIC_DFR_FLAT);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write_around(APIC_LDR, val);
}

static void flat_send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then
	 * we get an APIC send error if we try to broadcast.
	 * thus we have to avoid sending IPIs in this case.
	 */
//	if (~(1ul << smp_processor_id()) & cpu_online_map)
	if (smp_num_cpus > 1)
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector, APIC_DEST_LOGICAL);
}

static void flat_send_IPI_all(int vector)
{
	__send_IPI_shortcut(APIC_DEST_ALLINC, vector, APIC_DEST_LOGICAL);
}

static void flat_send_IPI_mask(unsigned long cpumask, int vector)
{
	unsigned long mask = cpumask;
	unsigned long cfg;
	unsigned long flags;

	__save_flags(flags);
	__cli();

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	apic_write_around(APIC_ICR2, cfg);

	/*
	 * program the ICR
	 */
	cfg = __prepare_ICR(0, vector, APIC_DEST_LOGICAL);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
	__restore_flags(flags);
}

static int flat_apic_id_registered(void)
{
	return test_bit(GET_APIC_ID(apic_read(APIC_ID)), &phys_cpu_present_map);
}

static unsigned int flat_cpu_mask_to_apicid(unsigned long cpumask)
{
	cpumask &= APIC_ALL_CPUS;
	if (cpumask == 0)
		cpumask = 0x01UL;
	return cpumask;
}


struct genapic apic_flat =  {
	.name = "flat",
	.int_delivery_mode = dest_LowestPrio,
	.int_dest_mode = (APIC_DEST_LOGICAL != 0),
	.int_delivery_dest = APIC_DEST_LOGICAL | APIC_DM_LOWEST,
	.target_cpus = flat_target_cpus,
	.apic_id_registered = flat_apic_id_registered,
	.init_apic_ldr = flat_init_apic_ldr,
	.send_IPI_all = flat_send_IPI_all,
	.send_IPI_allbutself = flat_send_IPI_allbutself,
	.send_IPI_mask = flat_send_IPI_mask,
	.cpu_mask_to_apicid = flat_cpu_mask_to_apicid,
};
