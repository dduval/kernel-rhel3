/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Clustered APIC subarch code.  Up to 255 CPUs, physical delivery.
 * (A more realistic maximum is around 230 CPUs.)
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


/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void cluster_init_apic_ldr(void)
{
	unsigned long val, id;
	long i, count;
	u8 lid;
	u8 my_id = hard_smp_processor_id();
	u8 my_cluster = APIC_CLUSTER(my_id);

	/* Create logical APIC IDs by counting CPUs already in cluster. */
	for (count = 0, i = NR_CPUS; --i >= 0; ) {
		lid = x86_cpu_to_log_apicid[i];
		if (lid != BAD_APICID && APIC_CLUSTER(lid) == my_cluster)
			++count;
	}
	/*
	 * We only have a 4 wide bitmap in cluster mode.  There's no way
	 * to get above 60 CPUs and still give each one it's own bit.
	 * But, we're using physical IRQ delivery, so we don't care.
	 * Use bit 3 for the 4th through Nth CPU in each cluster.
	 */
	if (count >= XAPIC_DEST_CPUS_SHIFT)
		count = 3;
	id = my_cluster | (1UL << count);
	x86_cpu_to_log_apicid[smp_processor_id()] = id;
	apic_write_around(APIC_DFR, APIC_DFR_CLUSTER);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write_around(APIC_LDR, val);
}


/* Mapping from cpu number to logical apicid */
static int cluster_cpu_to_logical_apicid(int cpu)
{
       if ((unsigned)cpu >= NR_CPUS)
	       return BAD_APICID;
	return x86_cpu_to_log_apicid[cpu];
}

static int cluster_cpu_present_to_apicid(int mps_cpu)
{
	if ((unsigned)mps_cpu < NR_CPUS)
		return (int)bios_cpu_apicid[mps_cpu];
	else
		return BAD_APICID;
}

/* Distribute IRQ load with round-robin allocation */

static u8 cluster_target_cpus(void)
{
	unsigned long	i;
	static unsigned long	last_cpu = 0;

	i = last_cpu;
	do {
		if (++i >= NR_CPUS)
			i = 0;
	} while (x86_cpu_to_apicid[i] == BAD_APICID);
	last_cpu = i;

	return x86_cpu_to_apicid[i];
}

static void cluster_send_IPI_mask(unsigned long mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static void cluster_send_IPI_allbutself(int vector)
{
	unsigned long	mask;

	mask = ~(1ul << smp_processor_id()) & cpu_online_map;
	if (mask != 0)
		cluster_send_IPI_mask(mask, vector);
}

static void cluster_send_IPI_all(int vector)
{
	cluster_send_IPI_mask(cpu_online_map, vector);
}

static int cluster_apic_id_registered(void)
{
	return 1;
}

static unsigned int cluster_cpu_mask_to_apicid(unsigned long cpumask)
{
	long cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = ffs(cpumask) - 1;
	if (cpu >= 0 && cpu < NR_CPUS)
		return x86_cpu_to_apicid[cpu];
	else
		return x86_cpu_to_apicid[0];
}


struct genapic apic_cluster = {
	.name = "clustered",
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.int_delivery_dest = APIC_DEST_PHYSICAL | APIC_DM_FIXED,
	.target_cpus = cluster_target_cpus,
	.apic_id_registered = cluster_apic_id_registered,
	.init_apic_ldr = cluster_init_apic_ldr,
	.send_IPI_all = cluster_send_IPI_all,
	.send_IPI_allbutself = cluster_send_IPI_allbutself,
	.send_IPI_mask = cluster_send_IPI_mask,
	.cpu_mask_to_apicid = cluster_cpu_mask_to_apicid,
};
