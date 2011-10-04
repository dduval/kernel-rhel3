/**
 * @file op_model_ia64.c
 * ia64 model-specific MSR operations
 *
 * @remark Copyright 2002-2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 * @author Graydon Hoare
 * @author Will Cohen
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/perfmon.h>
 
#include "op_ia64_model.h"
#include "op_counter.h"

#define NUM_COUNTERS 4
#define NUM_CONTROLS 4

static unsigned long reset_value[NUM_COUNTERS];

static void ia64_fill_in_addresses(struct op_msrs * const msrs)
{
	/* empty */
}

/* ---------------- PMU setup ------------------ */

/* This is kind of artificial.  The proc interface might really want to
 * accept register values directly.  There are other features not exposed 
 * by this limited interface.  Of course that might require all sorts of
 * validity checking??? */
static void
pmc_fill_in(ulong *val, u8 kernel, u8 user, u8 event, u8 um)
{
	/* enable interrupt generation */
	*val |= (1<<5);

	/* setup as a privileged monitor */
	*val |= (1<<6);

	/* McKinley requires pmc4 to have bit 23 set (enable PMU).
	 * It is supposedly ignored in other pmc registers.
	 * Try assuming it's ignored in Itanium, too, and just
	 * set it for everyone.
	 */

	*val |= (1<<23);

	/* enable/disable chosen OS and USR counting */
	(user)   ? (*val |= (1<<3))
		 : (*val &= ~(1<<3));

	(kernel) ? (*val |= (1<<0))
		 : (*val &= ~(1<<0));

	/* what are we counting ? */
	*val &= ~(0x7f << 8);
	*val |= ((event & 0x7f) << 8);
	*val &= ~(0xf << 16);
	*val |= ((um & 0xf) << 16);
}


static void ia64_setup_ctrs(struct op_msrs const * const msrs)
{
	ulong pmc_val;
	int i;
 
	/* clear all counters */
	for (i = 0 ; i < NUM_CONTROLS; ++i) {
		set_pmd(0,i);
	}
	
	/* avoid a false detection of ctr overflows in IRQ handler */
	for (i = 0; i < NUM_COUNTERS; ++i) {
	  /* CTR_WRITE(1, msrs, i); */
	}

	/* Make sure that the pmu  hardware is turn on */
	pmc_val = get_pmc(0);
	pmc_val |= (1<<23);
	set_pmc(pmc_val, 0);

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (counter_config[i].event) {
			pmc_val = 0;

			reset_value[i] = counter_config[i].count;

			set_pmd_neg(reset_value[i], i);

			pmc_fill_in(&pmc_val, counter_config[i].kernel, 
				counter_config[i].user,
				counter_config[i].event, 
				counter_config[i].unit_mask);

			set_pmc(pmc_val, i);
		} else {
			reset_value[i] = 0;
		}
	}

	/* unfreeze PMU */
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}


static int ia64_check_ctrs(unsigned int const cpu, 
			      struct op_msrs const * const msrs, 
			      struct pt_regs * const regs)
{
	int i;
	u64 pmc0;
	unsigned long eip = instruction_pointer(regs);
	int is_kernel = !user_mode(regs);

	pmc0 = ia64_get_pmc(0);

	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (pmd_overflowed(pmc0, i)) {
			oprofile_add_sample(eip, is_kernel, i, cpu);
			set_pmd_neg(reset_value[i], i);
		}
	}
	return 1;
}

 
static void ia64_start(struct op_msrs const * const msrs)
{
	/* turn on profiling */
	PFM_CPUINFO_SET(PFM_CPUINFO_DCR_PP | PFM_CPUINFO_SYST_WIDE);

	/* start monitoring at kernel level */
	__asm__ __volatile__ ("ssm psr.pp;;"::: "memory");

	/* enable dcr pp */
	ia64_set_dcr(ia64_get_dcr()|IA64_DCR_PP);

	ia64_srlz_i();

	/* unfreeze PMU */
	ia64_set_pmc(0, 0);
	ia64_srlz_d();
}


static void ia64_stop(struct op_msrs const * const msrs)
{
	/* freeze PMU */
	ia64_set_pmc(0, 1);
	ia64_srlz_d();

	/* disable the dcr pp */
	ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);

	/* stop in my current state */
	 __asm__ __volatile__ ("rsm psr.pp;;"::: "memory");

	ia64_srlz_i();

	/* turn off profiling */
	PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP | PFM_CPUINFO_SYST_WIDE);
}


struct op_ia64_model_spec const op_ia64_spec = {
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
	.pmd_mask = IA64_2_PMD_MASK_VAL,
	.fill_in_addresses = &ia64_fill_in_addresses,
	.setup_ctrs = &ia64_setup_ctrs,
	.check_ctrs = &ia64_check_ctrs,
	.start = &ia64_start,
	.stop = &ia64_stop,
};


struct op_ia64_model_spec const op_ia64_1_spec = {
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
	.pmd_mask = IA64_1_PMD_MASK_VAL,
	.fill_in_addresses = &ia64_fill_in_addresses,
	.setup_ctrs = &ia64_setup_ctrs,
	.check_ctrs = &ia64_check_ctrs,
	.start = &ia64_start,
	.stop = &ia64_stop
};


struct op_ia64_model_spec const op_ia64_2_spec = {
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
	.pmd_mask = IA64_2_PMD_MASK_VAL,
	.fill_in_addresses = &ia64_fill_in_addresses,
	.setup_ctrs = &ia64_setup_ctrs,
	.check_ctrs = &ia64_check_ctrs,
	.start = &ia64_start,
	.stop = &ia64_stop
};
