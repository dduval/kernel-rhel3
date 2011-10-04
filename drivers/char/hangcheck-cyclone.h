/* hangcheck-cyclone.h: 
 *
 * Code to access the x440's cyclone counter
 *
 * Copyright (C) 2003 IBM. 
 *  
 * Author: John Stultz <johnstul@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#ifndef X440_CYCLONE_H
#define X440_CYCLONE_H

#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ (u64)100000000
#define CYCLONE_TIMER_MASK (((u64)1<<40)-1) /*40 bit mask*/

static u32* volatile cyclone_timer;	/* Cyclone MPMC0 register */

/* locates and maps in the cyclone counter*/
static inline int init_cyclone(void)
{
	u32 base;
	u32* reg;	
	int i;

#ifdef CONFIG_SMP
	/* Borrowed from apm code. We need to insure we're on node0
	 * before trying to map in the CBAR address, otherwise we 
	 * may get the second CEC's CBAR which would point to the
	 * wrong cyclone counter
	 * 	-johnstul@us.ibm.com
	 */
	if (cpu_number_map(smp_processor_id()) != 0) {
		set_cpus_allowed(current,1);
		schedule();
		if (unlikely(cpu_number_map(smp_processor_id()) != 0)){
			return -ENODEV;		
		}
	}
#endif	

	/* find base address */
	reg = (u32*)__ioremap(CYCLONE_CBAR_ADDR, sizeof(u32), _PAGE_PCD);
	if(!reg){
		return -ENODEV;
	}
	/*save baseaddr*/
	base = *reg;
	iounmap(reg);
	if(!base){
		return -ENODEV;
	}
	
	/* map in cyclone_timer */
	cyclone_timer = (u32*)__ioremap(base + CYCLONE_MPMC_OFFSET,
			sizeof(u64), _PAGE_PCD);
	if(!cyclone_timer){
		return -ENODEV;
	}

	/*quick test to make sure its ticking*/
	for(i=0; i<3; i++){
		u32 old = cyclone_timer[0];
		int stall = 100;
		while(stall--) barrier();
		if(cyclone_timer[0] == old){
			cyclone_timer = 0;
			return -ENODEV;
		}
	}

	/* Everything looks good! */
	return 0;
}

/* reads the cyclone counter */
static inline unsigned long long get_cyclone(void)
{
	u32 low, high;
	
	do {
		high = cyclone_timer[1];
		low = cyclone_timer[0];
	} while(high != cyclone_timer[1]);
	
	return ((unsigned long long)high<<32)|low;
}
#endif
