/**
 * @file op_model_p4.c
 * P4 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 */

#include <linux/sched.h>
#include <linux/oprofile.h>
#include <linux/smp.h>
#include <asm/msr.h>
#include <asm/ptrace.h>
#include <asm/fixmap.h>
#include <asm/apic.h>
#include <asm/processor.h>

#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_EVENTS 39

#define NUM_COUNTERS_NON_HT 8
#define NUM_ESCRS_NON_HT 45
#define NUM_CCCRS_NON_HT 18
#define NUM_CONTROLS_NON_HT (NUM_ESCRS_NON_HT + NUM_CCCRS_NON_HT)

#define NUM_COUNTERS_HT2 4
#define NUM_ESCRS_HT2 23
#define NUM_CCCRS_HT2 9
#define NUM_CONTROLS_HT2 (NUM_ESCRS_HT2 + NUM_CCCRS_HT2)

static unsigned int num_counters = NUM_COUNTERS_NON_HT;
static unsigned int num_cccrs = NUM_CCCRS_NON_HT;


/* this has to be checked dynamically since the
   hyper-threadedness of a chip is discovered at
   kernel boot-time. */
static inline void setup_num_counters(void)
{
#ifdef CONFIG_SMP
	if (smp_num_siblings == 2) {		
		num_counters = NUM_COUNTERS_HT2;
		num_cccrs = NUM_CCCRS_HT2;
	}
#endif
}

static int inline addr_increment(void)
{
#ifdef CONFIG_SMP
	return smp_num_siblings == 2 ? 2 : 1;
#else
	return 1;
#endif
}


/* tables to simulate simplified hardware view of p4 registers */
struct p4_counter_binding {
	int virt_counter;
	int counter_address;
	int cccr_address;
};

struct p4_event_binding {
	int escr_select;  /* value to put in CCCR */
	int event_select; /* value to put in ESCR */
	struct {
		int virt_counter; /* for this counter... */
		int escr_address; /* use this ESCR       */
	} bindings[2];
};

/* nb: these CTR_* defines are a duplicate of defines in
   libop/op_events.c. */


#define CTR_BPU_0      (1 << 0)
#define CTR_MS_0       (1 << 1)
#define CTR_FLAME_0    (1 << 2)
#define CTR_IQ_4       (1 << 3)
#define CTR_BPU_2      (1 << 4)
#define CTR_MS_2       (1 << 5)
#define CTR_FLAME_2    (1 << 6)
#define CTR_IQ_5       (1 << 7)

static struct p4_counter_binding p4_counters [NUM_COUNTERS_NON_HT] = {
	{ CTR_BPU_0,   MSR_P4_BPU_PERFCTR0,   MSR_P4_BPU_CCCR0 },
	{ CTR_MS_0,    MSR_P4_MS_PERFCTR0,    MSR_P4_MS_CCCR0 },
	{ CTR_FLAME_0, MSR_P4_FLAME_PERFCTR0, MSR_P4_FLAME_CCCR0 },
	{ CTR_IQ_4,    MSR_P4_IQ_PERFCTR4,    MSR_P4_IQ_CCCR4 },
	{ CTR_BPU_2,   MSR_P4_BPU_PERFCTR2,   MSR_P4_BPU_CCCR2 },
	{ CTR_MS_2,    MSR_P4_MS_PERFCTR2,    MSR_P4_MS_CCCR2 },
	{ CTR_FLAME_2, MSR_P4_FLAME_PERFCTR2, MSR_P4_FLAME_CCCR2 },
	{ CTR_IQ_5,    MSR_P4_IQ_PERFCTR5,    MSR_P4_IQ_CCCR5 }
};

#define NUM_UNUSED_CCCRS	NUM_CCCRS_NON_HT - NUM_COUNTERS_NON_HT

/* All cccr we don't use. */
static int p4_unused_cccr[NUM_UNUSED_CCCRS] = {
	MSR_P4_BPU_CCCR1,	MSR_P4_BPU_CCCR3,
	MSR_P4_MS_CCCR1,	MSR_P4_MS_CCCR3,
	MSR_P4_FLAME_CCCR1,	MSR_P4_FLAME_CCCR3,
	MSR_P4_IQ_CCCR0,	MSR_P4_IQ_CCCR1,
	MSR_P4_IQ_CCCR2,	MSR_P4_IQ_CCCR3
};

/* p4 event codes in libop/op_event.h are indices into this table. */

static struct p4_event_binding p4_events[NUM_EVENTS] = {
	
	{ /* BRANCH_RETIRED */
		0x05, 0x06, 
		{ {CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  {CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},
	
	{ /* MISPRED_BRANCH_RETIRED */
		0x04, 0x03, 
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},
	
	{ /* TC_DELIVER_MODE */
		0x01, 0x01,
		{ { CTR_MS_0, MSR_P4_TC_ESCR0},  
		  { CTR_MS_2, MSR_P4_TC_ESCR1} }
	},
	
	{ /* BPU_FETCH_REQUEST */
		0x00, 0x03, 
		{ { CTR_BPU_0, MSR_P4_BPU_ESCR0},
		  { CTR_BPU_2, MSR_P4_BPU_ESCR1} }
	},

	{ /* ITLB_REFERENCE */
		0x03, 0x18,
		{ { CTR_BPU_0, MSR_P4_ITLB_ESCR0},
		  { CTR_BPU_2, MSR_P4_ITLB_ESCR1} }
	},

	{ /* MEMORY_CANCEL */
		0x05, 0x02,
		{ { CTR_FLAME_0, MSR_P4_DAC_ESCR0},
		  { CTR_FLAME_2, MSR_P4_DAC_ESCR1} }
	},

	{ /* MEMORY_COMPLETE */
		0x02, 0x08,
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* LOAD_PORT_REPLAY */
		0x02, 0x04, 
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* STORE_PORT_REPLAY */
		0x02, 0x05,
		{ { CTR_FLAME_0, MSR_P4_SAAT_ESCR0},
		  { CTR_FLAME_2, MSR_P4_SAAT_ESCR1} }
	},

	{ /* MOB_LOAD_REPLAY */
		0x02, 0x03,
		{ { CTR_BPU_0, MSR_P4_MOB_ESCR0},
		  { CTR_BPU_2, MSR_P4_MOB_ESCR1} }
	},

	{ /* PAGE_WALK_TYPE */
		0x04, 0x01,
		{ { CTR_BPU_0, MSR_P4_PMH_ESCR0},
		  { CTR_BPU_2, MSR_P4_PMH_ESCR1} }
	},

	{ /* BSQ_CACHE_REFERENCE */
		0x07, 0x0c, 
		{ { CTR_BPU_0, MSR_P4_BSU_ESCR0},
		  { CTR_BPU_2, MSR_P4_BSU_ESCR1} }
	},

	{ /* IOQ_ALLOCATION */
		0x06, 0x03, 
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  {-1,-1} }
	},

	{ /* IOQ_ACTIVE_ENTRIES */
		0x06, 0x1a, 
		{ { CTR_BPU_2, MSR_P4_FSB_ESCR1},
		  {-1,-1} }
	},

	{ /* FSB_DATA_ACTIVITY */
		0x06, 0x17, 
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  { CTR_BPU_2, MSR_P4_FSB_ESCR1} }
	},

	{ /* BSQ_ALLOCATION */
		0x07, 0x05, 
		{ { CTR_BPU_0, MSR_P4_BSU_ESCR0},
		  {-1,-1} }
	},

	{ /* BSQ_ACTIVE_ENTRIES */
		0x07, 0x06,
		{ { CTR_BPU_2, MSR_P4_BSU_ESCR1 /* guess */},  
		  {-1,-1} }
	},

	{ /* X87_ASSIST */
		0x05, 0x03, 
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* SSE_INPUT_ASSIST */
		0x01, 0x34,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},
  
	{ /* PACKED_SP_UOP */
		0x01, 0x08, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},
  
	{ /* PACKED_DP_UOP */
		0x01, 0x0c, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* SCALAR_SP_UOP */
		0x01, 0x0a, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* SCALAR_DP_UOP */
		0x01, 0x0e,
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* 64BIT_MMX_UOP */
		0x01, 0x02, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},
  
	{ /* 128BIT_MMX_UOP */
		0x01, 0x1a, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},

	{ /* X87_FP_UOP */
		0x01, 0x04, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},
  
	{ /* X87_SIMD_MOVES_UOP */
		0x01, 0x2e, 
		{ { CTR_FLAME_0, MSR_P4_FIRM_ESCR0},
		  { CTR_FLAME_2, MSR_P4_FIRM_ESCR1} }
	},
  
	{ /* MACHINE_CLEAR */
		0x05, 0x02, 
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* GLOBAL_POWER_EVENTS */
		0x06, 0x13 /* manual says 0x05 */, 
		{ { CTR_BPU_0, MSR_P4_FSB_ESCR0},
		  { CTR_BPU_2, MSR_P4_FSB_ESCR1} }
	},
  
	{ /* TC_MS_XFER */
		0x00, 0x05, 
		{ { CTR_MS_0, MSR_P4_MS_ESCR0},
		  { CTR_MS_2, MSR_P4_MS_ESCR1} }
	},

	{ /* UOP_QUEUE_WRITES */
		0x00, 0x09,
		{ { CTR_MS_0, MSR_P4_MS_ESCR0},
		  { CTR_MS_2, MSR_P4_MS_ESCR1} }
	},

	{ /* FRONT_END_EVENT */
		0x05, 0x08,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* EXECUTION_EVENT */
		0x05, 0x0c,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* REPLAY_EVENT */
		0x05, 0x09,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR2},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR3} }
	},

	{ /* INSTR_RETIRED */
		0x04, 0x02, 
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},

	{ /* UOPS_RETIRED */
		0x04, 0x01,
		{ { CTR_IQ_4, MSR_P4_CRU_ESCR0},
		  { CTR_IQ_5, MSR_P4_CRU_ESCR1} }
	},

	{ /* UOP_TYPE */    
		0x02, 0x02, 
		{ { CTR_IQ_4, MSR_P4_RAT_ESCR0},
		  { CTR_IQ_5, MSR_P4_RAT_ESCR1} }
	},

	{ /* RETIRED_MISPRED_BRANCH_TYPE */
		0x02, 0x05, 
		{ { CTR_MS_0, MSR_P4_TBPU_ESCR0},
		  { CTR_MS_2, MSR_P4_TBPU_ESCR1} }
	},

	{ /* RETIRED_BRANCH_TYPE */
		0x02, 0x04,
		{ { CTR_MS_0, MSR_P4_TBPU_ESCR0},
		  { CTR_MS_2, MSR_P4_TBPU_ESCR1} }
	}
};


#define MISC_PMC_ENABLED_P(x) ((x) & 1 << 7)

#define ESCR_RESERVED_BITS 0x80000003
#define ESCR_CLEAR(escr) ((escr) &= ESCR_RESERVED_BITS)
#define ESCR_SET_USR_0(escr, usr) ((escr) |= (((usr) & 1) << 2))
#define ESCR_SET_OS_0(escr, os) ((escr) |= (((os) & 1) << 3))
#define ESCR_SET_USR_1(escr, usr) ((escr) |= (((usr) & 1)))
#define ESCR_SET_OS_1(escr, os) ((escr) |= (((os) & 1) << 1))
#define ESCR_SET_EVENT_SELECT(escr, sel) ((escr) |= (((sel) & 0x3f) << 25))
#define ESCR_SET_EVENT_MASK(escr, mask) ((escr) |= (((mask) & 0xffff) << 9))
#define ESCR_READ(escr,high,ev,i) do {rdmsr(ev->bindings[(i)].escr_address, (escr), (high));} while (0);
#define ESCR_WRITE(escr,high,ev,i) do {wrmsr(ev->bindings[(i)].escr_address, (escr), (high));} while (0);

#define CCCR_RESERVED_BITS 0x38030FFF
#define CCCR_CLEAR(cccr) ((cccr) &= CCCR_RESERVED_BITS)
#define CCCR_SET_REQUIRED_BITS(cccr) ((cccr) |= 0x00030000)
#define CCCR_SET_ESCR_SELECT(cccr, sel) ((cccr) |= (((sel) & 0x07) << 13))
#define CCCR_SET_PMI_OVF_0(cccr) ((cccr) |= (1<<26))
#define CCCR_SET_PMI_OVF_1(cccr) ((cccr) |= (1<<27))
#define CCCR_SET_ENABLE(cccr) ((cccr) |= (1<<12))
#define CCCR_SET_DISABLE(cccr) ((cccr) &= ~(1<<12))
#define CCCR_READ(low, high, i) do {rdmsr (p4_counters[(i)].cccr_address, (low), (high));} while (0);
#define CCCR_WRITE(low, high, i) do {wrmsr (p4_counters[(i)].cccr_address, (low), (high));} while (0);
#define CCCR_OVF_P(cccr) ((cccr) & (1U<<31))
#define CCCR_CLEAR_OVF(cccr) ((cccr) &= (~(1U<<31)))

#define CTR_READ(l,h,i) do {rdmsr(p4_counters[(i)].counter_address, (l), (h));} while (0);
#define CTR_WRITE(l,i) do {wrmsr(p4_counters[(i)].counter_address, -(u32)(l), -1);} while (0);
#define CTR_OVERFLOW_P(ctr) (!((ctr) & 0x80000000))

/* these access the underlying cccrs 1-18, not the subset of 8 bound to "virtual counters" */
#define RAW_CCCR_READ(low, high, i) do {rdmsr (MSR_P4_BPU_CCCR0 + (i), (low), (high));} while (0);
#define RAW_CCCR_WRITE(low, high, i) do {wrmsr (MSR_P4_BPU_CCCR0 + (i), (low), (high));} while (0);


/* this assigns a "stagger" to the current CPU, which is used throughout
   the code in this module as an extra array offset, to select the "even"
   or "odd" part of all the divided resources. */
static inline unsigned int get_stagger(void)
{
#ifdef CONFIG_SMP
	int cpu;
	if (smp_num_siblings > 1) {
		cpu = smp_processor_id();
		return (cpu_sibling_map[cpu] > cpu) ? 0 : 1;
	}
#endif	
	return 0;
}


/* finally, mediate access to a real hardware counter
   by passing a "virtual" counter numer to this macro,
   along with your stagger setting. */
#define VIRT_CTR(stagger, i) ((i) + ((num_counters) * (stagger)))

static unsigned long reset_value[NUM_COUNTERS_NON_HT];


static void p4_fill_in_addresses(struct op_msrs * const msrs)
{
	unsigned int i; 
	unsigned int addr, stag;

	setup_num_counters();
	stag = get_stagger();

	/* the counter registers we pay attention to */
	for (i = 0; i < num_counters; ++i) {
		msrs->counters.addrs[i] = 
			p4_counters[VIRT_CTR(stag, i)].counter_address;
	}

	/* FIXME: bad feeling, we don't save the 10 counters we don't use. */

	/* 18 CCCR registers */
	for (i = 0, addr = MSR_P4_BPU_CCCR0 + stag;
	     addr <= MSR_P4_IQ_CCCR5; ++i, addr += addr_increment()) {
		msrs->controls.addrs[i] = addr;
	}
	
	/* 43 ESCR registers in three discontiguous group */
	for (addr = MSR_P4_BSU_ESCR0 + stag;
	     addr <= MSR_P4_SSU_ESCR0; ++i, addr += addr_increment()) { 
		msrs->controls.addrs[i] = addr;
	}
	
	for (addr = MSR_P4_MS_ESCR0 + stag;
	     addr <= MSR_P4_TC_ESCR1; ++i, addr += addr_increment()) { 
		msrs->controls.addrs[i] = addr;
	}
	
	for (addr = MSR_P4_IX_ESCR0 + stag;
	     addr <= MSR_P4_CRU_ESCR3; ++i, addr += addr_increment()) { 
		msrs->controls.addrs[i] = addr;
	}

	/* there are 2 remaining non-contiguously located ESCRs */

	if (num_counters == NUM_COUNTERS_NON_HT) {		
		/* standard non-HT CPUs handle both remaining ESCRs*/
		msrs->controls.addrs[i++] = MSR_P4_CRU_ESCR5;
		msrs->controls.addrs[i++] = MSR_P4_CRU_ESCR4;

	} else if (stag == 0) {
		/* HT CPUs give the first remainder to the even thread, as
		   the 32nd control register */
		msrs->controls.addrs[i++] = MSR_P4_CRU_ESCR4;

	} else {
		/* and two copies of the second to the odd thread,
		   for the 31st and 32nd control registers */
		msrs->controls.addrs[i++] = MSR_P4_CRU_ESCR5;
		msrs->controls.addrs[i++] = MSR_P4_CRU_ESCR5;
	}
}


static void pmc_setup_one_p4_counter(unsigned int ctr)
{
	int i;
	int const maxbind = 2;
	unsigned int cccr = 0;
	unsigned int escr = 0;
	unsigned int high = 0;
	unsigned int counter_bit;
	struct p4_event_binding * ev = 0;
	unsigned int stag;

	stag = get_stagger();
	
	/* convert from counter *number* to counter *bit* */
	counter_bit = 1 << VIRT_CTR(stag, ctr);
	
	/* find our event binding structure. */
	if (counter_config[ctr].event <= 0 || counter_config[ctr].event > NUM_EVENTS) {
		printk(KERN_ERR 
		       "oprofile: P4 event code 0x%lx out of range\n", 
		       counter_config[ctr].event);
		return;
	}
	
	ev = &(p4_events[counter_config[ctr].event - 1]);
	
	for (i = 0; i < maxbind; i++) {
		if (ev->bindings[i].virt_counter & counter_bit) {
			
			/* modify ESCR */
			ESCR_READ(escr, high, ev, i);
			ESCR_CLEAR(escr);
			if (stag == 0) {
				ESCR_SET_USR_0(escr, counter_config[ctr].user);
				ESCR_SET_OS_0(escr, counter_config[ctr].kernel);
			} else {
				ESCR_SET_USR_1(escr, counter_config[ctr].user);
				ESCR_SET_OS_1(escr, counter_config[ctr].kernel);
			}
			ESCR_SET_EVENT_SELECT(escr, ev->event_select);
			ESCR_SET_EVENT_MASK(escr, counter_config[ctr].unit_mask);			
			ESCR_WRITE(escr, high, ev, i);
		       
			/* modify CCCR */
			CCCR_READ(cccr, high, VIRT_CTR(stag, ctr));
			CCCR_CLEAR(cccr);
			CCCR_SET_REQUIRED_BITS(cccr);
			CCCR_SET_ESCR_SELECT(cccr, ev->escr_select);
			if (stag == 0) {
				CCCR_SET_PMI_OVF_0(cccr);
			} else {
				CCCR_SET_PMI_OVF_1(cccr);
			}
			CCCR_WRITE(cccr, high, VIRT_CTR(stag, ctr));
			return;
		}
	}
}


static void p4_setup_ctrs(struct op_msrs const * const msrs)
{
	unsigned int i;
	unsigned int low, high;
	unsigned int addr;
	unsigned int stag;

	stag = get_stagger();

	rdmsr(MSR_IA32_MISC_ENABLE, low, high);
	if (! MISC_PMC_ENABLED_P(low)) {
		printk(KERN_ERR "oprofile: P4 PMC not available\n");
		return;
	}

	/* clear the cccrs we will use */
	for (i = 0 ; i < num_counters ; i++) {
		rdmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
		CCCR_CLEAR(low);
		CCCR_SET_REQUIRED_BITS(low);
		wrmsr(p4_counters[VIRT_CTR(stag, i)].cccr_address, low, high);
	}

	/* clear cccrs outside our concern */
	for (i = stag ; i < NUM_UNUSED_CCCRS ; i += addr_increment()) {
		rdmsr(p4_unused_cccr[i], low, high);
		CCCR_CLEAR(low);
		CCCR_SET_REQUIRED_BITS(low);
		wrmsr(p4_unused_cccr[i], low, high);
	}

	/* clear all escrs (including those outside our concern) */
	for (addr = MSR_P4_BSU_ESCR0 + stag;
	     addr <= MSR_P4_SSU_ESCR0; addr += addr_increment()) { 
		wrmsr(addr, 0, 0);
	}
	
	for (addr = MSR_P4_MS_ESCR0 + stag;
	     addr <= MSR_P4_TC_ESCR1; addr += addr_increment()){ 
		wrmsr(addr, 0, 0);
	}
	
	for (addr = MSR_P4_IX_ESCR0 + stag;
	     addr <= MSR_P4_CRU_ESCR3; addr += addr_increment()){ 
		wrmsr(addr, 0, 0);
	}

	if (num_counters == NUM_COUNTERS_NON_HT) {		
		wrmsr(MSR_P4_CRU_ESCR4, 0, 0);
		wrmsr(MSR_P4_CRU_ESCR5, 0, 0);
	} else if (stag == 0) {
		wrmsr(MSR_P4_CRU_ESCR4, 0, 0);
	} else {
		wrmsr(MSR_P4_CRU_ESCR5, 0, 0);
	}		
	
	/* setup all counters */
	for (i = 0 ; i < num_counters ; ++i) {
		if (counter_config[i].event) {
			reset_value[i] = counter_config[i].count;
			pmc_setup_one_p4_counter(i);
			CTR_WRITE(counter_config[i].count, VIRT_CTR(stag, i));
		} else {
			reset_value[i] = 0;
		}
	}
}


static int p4_check_ctrs(unsigned int const cpu, 
			  struct op_msrs const * const msrs,
			  struct pt_regs * const regs)
{
	unsigned long ctr, low, high, stag, real;
	int i;
	unsigned long eip = instruction_pointer(regs);
	int is_kernel = !user_mode(regs);

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {
		
		if (!counter_config[i].event) 
			continue;

		/* 
		 * there is some eccentricity in the hardware which
		 * requires that we perform 2 extra corrections:
		 *
		 * - check both the CCCR:OVF flag for overflow and the
		 *   counter high bit for un-flagged overflows.
		 *
		 * - write the counter back twice to ensure it gets
		 *   updated properly.
		 * 
		 * the former seems to be related to extra NMIs happening
		 * during the current NMI; the latter is reported as errata
		 * N15 in intel doc 249199-029, pentium 4 specification
		 * update, though their suggested work-around does not
		 * appear to solve the problem.
		 */
		
		real = VIRT_CTR(stag, i);

		CCCR_READ(low, high, real);
 		CTR_READ(ctr, high, real);
		if (CCCR_OVF_P(low) || CTR_OVERFLOW_P(ctr)) {
			oprofile_add_sample(eip, is_kernel, i, cpu);
 			CTR_WRITE(reset_value[i], real);
			CCCR_CLEAR_OVF(low);
			CCCR_WRITE(low, high, real);
 			CTR_WRITE(reset_value[i], real);
			/* P4 quirk: you have to re-unmask the apic vector */
			apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);
		}
	}

	/* P4 quirk: you have to re-unmask the apic vector */
	apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);

	/* See op_model_ppro.c */
	return 1;
}


static void p4_start(struct op_msrs const * const msrs)
{
	unsigned int low, high, stag;
	int i;

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {
		if (!reset_value[i])
			continue;
		CCCR_READ(low, high, VIRT_CTR(stag, i));
		CCCR_SET_ENABLE(low);
		CCCR_WRITE(low, high, VIRT_CTR(stag, i));
	}
}


static void p4_stop(struct op_msrs const * const msrs)
{
	unsigned int low, high, stag;
	int i;

	stag = get_stagger();

	for (i = 0; i < num_counters; ++i) {
		CCCR_READ(low, high, VIRT_CTR(stag, i));
		CCCR_SET_DISABLE(low);
		CCCR_WRITE(low, high, VIRT_CTR(stag, i));
	}
}


#ifdef CONFIG_SMP
struct op_x86_model_spec const op_p4_ht2_spec = {
	.num_counters = NUM_COUNTERS_HT2,
	.num_controls = NUM_CONTROLS_HT2,
	.fill_in_addresses = &p4_fill_in_addresses,
	.setup_ctrs = &p4_setup_ctrs,
	.check_ctrs = &p4_check_ctrs,
	.start = &p4_start,
	.stop = &p4_stop
};
#endif

struct op_x86_model_spec const op_p4_spec = {
	.num_counters = NUM_COUNTERS_NON_HT,
	.num_controls = NUM_CONTROLS_NON_HT,
	.fill_in_addresses = &p4_fill_in_addresses,
	.setup_ctrs = &p4_setup_ctrs,
	.check_ctrs = &p4_check_ctrs,
	.start = &p4_start,
	.stop = &p4_stop
};
