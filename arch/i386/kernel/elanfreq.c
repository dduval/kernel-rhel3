/*
 * 	elanfreq: 	cpufreq driver for the AMD ELAN family
 *
 *	(c) Copyright 2002 Robert Schwebel <r.schwebel@pengutronix.de>
 *
 *	Parts of this code are (c) Sven Geggus <sven@geggus.net> 
 *
 *      All Rights Reserved. 
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version. 
 *
 *	2002-02-13: - initial revision for 2.4.18-pre9 by Robert Schwebel
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/cpufreq.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>

#define REG_CSCIR 0x22 		/* Chip Setup and Control Index Register    */
#define REG_CSCDR 0x23		/* Chip Setup and Control Data  Register    */

static struct cpufreq_driver *elanfreq_driver;

/* Module parameter */
static int max_freq;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robert Schwebel <r.schwebel@pengutronix.de>, Sven Geggus <sven@geggus.net>");
MODULE_DESCRIPTION("cpufreq driver for AMD's Elan CPUs");

struct s_elan_multiplier {
	int clock;		/* frequency in kHz                         */
	int val40h;		/* PMU Force Mode register                  */
	int val80h;		/* CPU Clock Speed Register                 */
};

/*
 * It is important that the frequencies 
 * are listed in ascending order here!
 */
struct s_elan_multiplier elan_multiplier[] = {
	{1000,	0x02,	0x18},
	{2000,	0x02,	0x10},
	{4000,	0x02,	0x08},
	{8000,	0x00,	0x00},
	{16000,	0x00,	0x02},
	{33000,	0x00,	0x04},
	{66000,	0x01,	0x04},
	{99000,	0x01,	0x05}
};

static struct cpufreq_frequency_table elanfreq_table[] = {
	{0,	1000},
	{1,	2000},
	{2,	4000},
	{3,	8000},
	{4,	16000},
	{5,	33000},
	{6,	66000},
	{7,	99000},
	{0,	CPUFREQ_TABLE_END},
};


/**
 *	elanfreq_get_cpu_frequency: determine current cpu speed
 *
 *	Finds out at which frequency the CPU of the Elan SOC runs
 *	at the moment. Frequencies from 1 to 33 MHz are generated 
 *	the normal way, 66 and 99 MHz are called "Hyperspeed Mode"
 *	and have the rest of the chip running with 33 MHz. 
 */

static unsigned int elanfreq_get_cpu_frequency(void)
{
        u8 clockspeed_reg;    /* Clock Speed Register */
	
	local_irq_disable();
        outb_p(0x80,REG_CSCIR);
        clockspeed_reg = inb_p(REG_CSCDR);
	local_irq_enable();

        if ((clockspeed_reg & 0xE0) == 0xE0) { return 0; }

        /* Are we in CPU clock multiplied mode (66/99 MHz)? */
        if ((clockspeed_reg & 0xE0) == 0xC0) {
                if ((clockspeed_reg & 0x01) == 0) {
			return 66000;
		} else {
			return 99000;             
		}
        }

	/* 33 MHz is not 32 MHz... */
	if ((clockspeed_reg & 0xE0)==0xA0)
		return 33000;

        return ((1<<((clockspeed_reg & 0xE0) >> 5)) * 1000);
}


/**
 *      elanfreq_set_cpu_frequency: Change the CPU core frequency
 * 	@cpu: cpu number
 *	@freq: frequency in kHz
 *
 *      This function takes a frequency value and changes the CPU frequency 
 *	according to this. Note that the frequency has to be checked by
 *	elanfreq_validatespeed() for correctness!
 *	
 *	There is no return value. 
 */

static void elanfreq_set_cpu_state (unsigned int state) {

	struct cpufreq_freqs    freqs;

	if (!elanfreq_driver) {
		printk(KERN_ERR "cpufreq: initialization problem or invalid target frequency\n");
		return;
	}

	freqs.old = elanfreq_get_cpu_frequency();
	freqs.new = elan_multiplier[state].clock;
	freqs.cpu = 0; /* elanfreq.c is UP only driver */
	
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	printk(KERN_INFO "elanfreq: attempting to set frequency to %i kHz\n",elan_multiplier[state].clock);


	/* 
	 * Access to the Elan's internal registers is indexed via    
	 * 0x22: Chip Setup & Control Register Index Register (CSCI) 
	 * 0x23: Chip Setup & Control Register Data  Register (CSCD) 
	 *
	 */

	/* 
	 * 0x40 is the Power Management Unit's Force Mode Register. 
	 * Bit 6 enables Hyperspeed Mode (66/100 MHz core frequency)
	 */

	local_irq_disable();
	outb_p(0x40,REG_CSCIR); 	/* Disable hyperspeed mode          */
	outb_p(0x00,REG_CSCDR);
	local_irq_enable();		/* wait till internal pipelines and */
	udelay(1000);			/* buffers have cleaned up          */

	local_irq_disable();

	/* now, set the CPU clock speed register (0x80) */
	outb_p(0x80,REG_CSCIR);
	outb_p(elan_multiplier[state].val80h,REG_CSCDR);

	/* now, the hyperspeed bit in PMU Force Mode Register (0x40) */
	outb_p(0x40,REG_CSCIR);
	outb_p(elan_multiplier[state].val40h,REG_CSCDR);
	udelay(10000);
	local_irq_enable();

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
};


/**
 *	elanfreq_validatespeed: test if frequency range is valid 
 *
 *	This function checks if a given frequency range in kHz is valid 
 *      for the hardware supported by the driver. 
 */

static int elanfreq_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &elanfreq_table[0]);
}

static int elanfreq_setpolicy (struct cpufreq_policy *policy)
{
	unsigned int    newstate = 0;

	if (cpufreq_frequency_table_setpolicy(policy, &elanfreq_table[0], &newstate))
		return -EINVAL;

	elanfreq_set_cpu_state(newstate);

	return 0;
}


/*
 *	Module init and exit code
 */

#ifndef MODULE
/**
 * elanfreq_setup - elanfreq command line parameter parsing
 *
 * elanfreq command line parameter.  Use:
 *  elanfreq=66000
 * to set the maximum CPU frequency to 66 MHz. Note that in
 * case you do not give this boot parameter, the maximum
 * frequency will fall back to _current_ CPU frequency which
 * might be lower. If you build this as a module, use the
 * max_freq module parameter instead.
 */
static int __init elanfreq_setup(char *str)
{
	max_freq = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("elanfreq=", elanfreq_setup);
#endif

static int __init elanfreq_init(void) 
{	
	struct cpuinfo_x86 *c = cpu_data;
	struct cpufreq_driver *driver;
	int ret, i;

	/* Test if we have the right hardware */
	if ((c->x86_vendor != X86_VENDOR_AMD) ||
		(c->x86 != 4) || (c->x86_model!=10))
	{
		printk(KERN_INFO "elanfreq: error: no Elan processor found!\n");
                return -ENODEV;
	}
	
	driver = kmalloc(sizeof(struct cpufreq_driver) +
			 NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->policy = (struct cpufreq_policy *) (driver + 1);

	if (!max_freq)
		max_freq = elanfreq_get_cpu_frequency();

	/* table init */
 	for (i=0; (elanfreq_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (elanfreq_table[i].frequency > max_freq)
			elanfreq_table[i].frequency = CPUFREQ_ENTRY_INVALID;
	}

#ifdef CONFIG_CPU_FREQ_24_API
	driver->cpu_cur_freq[0] = elanfreq_get_cpu_frequency();
#endif

	driver->verify        = &elanfreq_verify;
	driver->setpolicy     = &elanfreq_setpolicy;

	driver->policy[0].cpu    = 0;
	ret = cpufreq_frequency_table_cpuinfo(&driver->policy[0], &elanfreq_table[0]);
	if (ret) {
		kfree(driver);
		return ret;
	}
	driver->policy[0].policy = CPUFREQ_POLICY_PERFORMANCE;
	driver->policy[0].cpuinfo.transition_latency = CPUFREQ_ETERNAL;

	elanfreq_driver = driver;

	ret = cpufreq_register(driver);
	if (ret) {
		elanfreq_driver = NULL;
		kfree(driver);
	}

	return ret;
}


static void __exit elanfreq_exit(void) 
{
	if (elanfreq_driver) {
		cpufreq_unregister();
		kfree(elanfreq_driver);
	}
}

module_init(elanfreq_init);
module_exit(elanfreq_exit);

MODULE_PARM (max_freq, "i");

