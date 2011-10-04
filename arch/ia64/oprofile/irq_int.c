/**
 * @file irq_int.c
 *
 * @remark Copyright 2002-2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Will Cohen
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/pm.h>
#include <asm/ptrace.h>
#include <asm/bitops.h>
#include <asm/processor.h>
 
#include "op_counter.h"
#include "op_ia64_model.h"

unsigned long op_pmd_mask = IA64_2_PMD_MASK_VAL;
 
static volatile struct op_ia64_model_spec const * model;
static struct op_msrs cpu_msrs[NR_CPUS];
 
static int irq_start(void);
static void irq_stop(void);

spinlock_t oprofilefs_lock = SPIN_LOCK_UNLOCKED;
 
static void
irq_pmu_interrupt_handler(int irq, void *arg, struct pt_regs *regs)
{
	u64 pmc0 = ia64_get_pmc(0);

	if (IA64_PMU_FREEZE(pmc0)) {
		uint cpu = smp_processor_id();

		(model->check_ctrs(cpu, &cpu_msrs[cpu], regs));
		/* unfreeze PMU */
		ia64_set_pmc(0, 0);
		ia64_srlz_d();
	}
}


static pfm_intr_handler_desc_t irq_handler={
	handler: irq_pmu_interrupt_handler
};

 
static void irq_save_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr_group * counters = &msrs->counters;
	struct op_msr_group * controls = &msrs->controls;
	int i;

	for (i = 0; i < nr_ctrs; ++i) {
		counters->saved[i].low = get_pmd(i);
	}
 
	for (i = 0; i < nr_ctrls; ++i) {
		controls->saved[i].low = get_pmc(i);
	}
}

 
static void irq_cpu_setup(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	model->fill_in_addresses(msrs);
	irq_save_registers(msrs);
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
}
 

static int irq_setup(void)
{
	int ret;

	/*
	 * first we must reserve all the CPUs for our full system-wide
	 * session.
	 */
	ret = pfm_install_alternate_syswide_subsystem(&irq_handler);
	if (ret) {
		printk(KERN_INFO "cannot reserve alternate system wide session: %d\n",
		       ret);
		return 1;
	}
	printk(KERN_INFO "succesfully allocated all PMUs for system wide session, handler redirected\n");
	/*
	 * upon return, you are guaranteed:
	 * 	- that no perfmon context was alive 
	 * 	- no new perfmon context will be created until you unreserve
	 * 	- any new overflow interrupt will go to our handler
	 *
	 * This call only does software reservation, the PMU is not touched
	 * at all. 
	 *
	 * For 2.4 kernels, the PMU is guaranteed frozen on all CPUs at this point.
	 */
	printk(KERN_INFO "succesfully install new PMU overflow handler\n");

	/* local work with the PMU can begin here */

	smp_call_function(irq_cpu_setup, NULL, 0, 1);
	irq_cpu_setup(0);
	return 0;
}


static void irq_restore_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr_group * counters = &msrs->counters;
	struct op_msr_group * controls = &msrs->controls;
	int i;

	for (i = 0; i < nr_ctrls; ++i) {
		set_pmc(controls->saved[i].low, i);
	}
 
	for (i = 0; i < nr_ctrs; ++i) {
		set_pmd(counters->saved[i].low, i);
	}
}
 

static void irq_cpu_shutdown(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
 
	irq_restore_registers(msrs);
}

 
static void irq_shutdown(void)
{
	printk(KERN_INFO "entering irq_shutdown()\n");
	/*
	 * this call will:
	 * 	- remove our local PMU interrupt handler
	 * 	- release our system wide session on all CPU indicated by the provided cpu_mask
	 *
	 * The caller must leave the PMU as follows (i.e. as it was when this got started):
	 * 	- frozen on all CPUs
	 * 	- local_cpu_data->pfm_dcr_pp = 0 and local_cpu_data->pfm_syst_wide = 0
	 */
	smp_call_function(irq_cpu_shutdown, NULL, 0, 1);
	irq_cpu_shutdown(0);
	pfm_remove_alternate_syswide_subsystem(&irq_handler);
}

 
static void irq_cpu_start(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->start(msrs);
}
 

static int irq_start(void)
{
	smp_call_function(irq_cpu_start, NULL, 0, 1);
	irq_cpu_start(0);
	return 0;
}
 
 
static void irq_cpu_stop(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->stop(msrs);
}
 
 
static void irq_stop(void)
{
	smp_call_function(irq_cpu_stop, NULL, 0, 1);
	irq_cpu_stop(0);
}


struct op_counter_config counter_config[OP_MAX_COUNTER];

static int irq_create_files(struct super_block * sb, struct dentry * root)
{
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry * dir;
		char buf[2];
 
		snprintf(buf, 2, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled); 
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event); 
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count); 
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask); 
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel); 
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user); 
	}

	return 0;
}
 
 
struct oprofile_operations irq_ops = {
	.create_files 	= irq_create_files,
	.setup 		= irq_setup,
	.shutdown	= irq_shutdown,
	.start		= irq_start,
	.stop		= irq_stop
};
 
 
int __init irq_init(struct oprofile_operations ** ops)
{
	__u8 family = local_cpu_data->family;


	/* FIXME: There should be a bit more checking here. */
	switch (family) {
	case 0x07: /* Itanium */
		irq_ops.cpu_type = "ia64/itanium";
		model = &op_ia64_1_spec;
		break;
	case 0x1f: /* Itanium 2 */
		irq_ops.cpu_type = "ia64/itanium2";
		model = &op_ia64_2_spec;
		break;
	default:
		irq_ops.cpu_type = "ia64/ia64";
		model = &op_ia64_spec;
		break;
	}

	*ops = &irq_ops;
	op_pmd_mask = model->pmd_mask;
	printk(KERN_INFO "oprofile: using IRQ interrupt.\n");
	return 1;
}
