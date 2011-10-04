/**
 * @file op_ia64_model.h
 * interface to ia64 model-specific MSR operations
 *
 * @remark Copyright 2002-2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 * @author Will Cohen
 */

#ifndef OP_IA64_MODEL_H
#define OP_IA64_MODEL_H

#include <asm/perfmon.h>

/* the Pentium IV has quite a lot of control registers */
#define MAX_MSR 63

/* Valid bits in PMD registers */
#define IA64_1_PMD_MASK_VAL	((1UL << 32) - 1)
#define IA64_2_PMD_MASK_VAL	((1UL << 47) - 1)

#define IA64_PMU_FREEZE(v)	((v) & (~0x1UL))

/* performance counters are in pairs: pmcN and pmdN.  The pmc register acts
 * as the event selection; the pmd register is the counter. */
#define perf_reg(c)	((c)+4)

#define pmd_overflowed(r,c) ((r) & (1 << perf_reg(c)))
#define set_pmd_neg(v,c) do { \
	ia64_set_pmd(perf_reg(c), -(ulong)(v) & op_pmd_mask); \
	ia64_srlz_d(); } while (0)
#define set_pmd(v,c) do { \
	ia64_set_pmd(perf_reg(c), (v) & op_pmd_mask); \
	ia64_srlz_d(); } while (0)
#define set_pmc(v,c) do { ia64_set_pmc(perf_reg(c), (v)); ia64_srlz_d(); } while (0)
#define get_pmd(c) ia64_get_pmd(perf_reg(c))
#define get_pmc(c) ia64_get_pmc(perf_reg(c))
 
struct op_saved_msr {
	unsigned int high;
	unsigned int low;
};

struct op_msr_group {
	unsigned int addrs[MAX_MSR];
	struct op_saved_msr saved[MAX_MSR];
};

struct op_msrs {
	struct op_msr_group counters;
	struct op_msr_group controls;
};

struct pt_regs;

/* The model vtable abstracts the differences between
 * various ia64 CPU model's perfctr support.
 */
struct op_ia64_model_spec {
	unsigned int const num_counters;
	unsigned int const num_controls;
	unsigned long const pmd_mask;
	void (*fill_in_addresses)(struct op_msrs * const msrs);
	void (*setup_ctrs)(struct op_msrs const * const msrs);
	int (*check_ctrs)(unsigned int const cpu, 
		struct op_msrs const * const msrs,
		struct pt_regs * const regs);
	void (*start)(struct op_msrs const * const msrs);
	void (*stop)(struct op_msrs const * const msrs);
};

extern unsigned long op_pmd_mask;

extern struct op_ia64_model_spec const op_ia64_spec;
extern struct op_ia64_model_spec const op_ia64_1_spec;
extern struct op_ia64_model_spec const op_ia64_2_spec;

#endif /* OP_IA64_MODEL_H */
