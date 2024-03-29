#ifndef __ASM_SMPBOOT_H
#define __ASM_SMPBOOT_H

/*emum for clustered_apic_mode values*/
enum{
	CLUSTERED_APIC_NONE = 0,
	CLUSTERED_APIC_XAPIC,
	CLUSTERED_APIC_NUMAQ
};

#ifdef CONFIG_X86_CLUSTERED_APIC
extern unsigned int apic_broadcast_id;
extern unsigned char clustered_apic_mode;
extern unsigned char esr_disable;
extern unsigned char int_delivery_mode;
extern unsigned int int_dest_addr_mode;
extern int cyclone_setup(char*);
extern int usb_early_handoff;

static inline void detect_clustered_apic(char* oem, char* prod)
{
	/*
	 * Can't recognize Summit xAPICs at present, so use the OEM ID.
	 */
	if (!strncmp(oem, "IBM ENSW", 8) &&
	    (!strncmp(prod, "VIGIL SMP", 9) ||
	     !strncmp(prod, "EXA", 3) ||
	     !strncmp(prod, "RUTHLESS", 8))){
		clustered_apic_mode = CLUSTERED_APIC_XAPIC;
		apic_broadcast_id = APIC_BROADCAST_ID_XAPIC;
		int_dest_addr_mode = APIC_DEST_PHYSICAL;
		int_delivery_mode = dest_Fixed;
		esr_disable = 1;
		/*Start cyclone clock*/
		cyclone_setup(0);
		usb_early_handoff = 1;
	}
	else if (!strncmp(oem, "IBM ENSW", 8) && !strncmp(prod, "RUTHLESS SMP", 9)){
		clustered_apic_mode = CLUSTERED_APIC_XAPIC;
		apic_broadcast_id = APIC_BROADCAST_ID_XAPIC;
		int_dest_addr_mode = APIC_DEST_PHYSICAL;
		int_delivery_mode = dest_Fixed;
		esr_disable = 1;
		/*Start cyclone clock*/
		cyclone_setup(0);
		usb_early_handoff = 1;
	}
	else if (!strncmp(oem, "IBM NUMA", 8)){
		clustered_apic_mode = CLUSTERED_APIC_NUMAQ;
		apic_broadcast_id = APIC_BROADCAST_ID_APIC;
		int_dest_addr_mode = APIC_DEST_LOGICAL;
		int_delivery_mode = dest_LowestPrio;
		esr_disable = 1;
	}
}
#define	INT_DEST_ADDR_MODE (int_dest_addr_mode)
#define	INT_DELIVERY_MODE (int_delivery_mode)
#else /* CONFIG_X86_CLUSTERED_APIC */
#define apic_broadcast_id (APIC_BROADCAST_ID_APIC)
#define clustered_apic_mode (CLUSTERED_APIC_NONE)
#define esr_disable (0)
#define detect_clustered_apic(x,y)
#define INT_DEST_ADDR_MODE (APIC_DEST_LOGICAL)	/* logical delivery */
#define INT_DELIVERY_MODE (dest_LowestPrio)
#endif /* CONFIG_X86_CLUSTERED_APIC */
#define BAD_APICID 0xFFu

#define TRAMPOLINE_LOW phys_to_virt((clustered_apic_mode == CLUSTERED_APIC_NUMAQ)?0x8:0x467)
#define TRAMPOLINE_HIGH phys_to_virt((clustered_apic_mode == CLUSTERED_APIC_NUMAQ)?0xa:0x469)

#define boot_cpu_apicid ((clustered_apic_mode == CLUSTERED_APIC_NUMAQ)?boot_cpu_logical_apicid:boot_cpu_physical_apicid)

extern unsigned char raw_phys_apicid[NR_CPUS];

/*
 * How to map from the cpu_present_map
 */
static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (clustered_apic_mode == CLUSTERED_APIC_XAPIC)
		return raw_phys_apicid[mps_cpu];
	if(clustered_apic_mode == CLUSTERED_APIC_NUMAQ)
		return (mps_cpu/4)*16 + (1<<(mps_cpu%4));
	return mps_cpu;
}

static inline unsigned long apicid_to_phys_cpu_present(int apicid)
{
	if(clustered_apic_mode)
		return 1UL << (((apicid >> 4) << 2) + (apicid & 0x3));
	return 1UL << apicid;
}

#define physical_to_logical_apicid(phys_apic) ( (1ul << (phys_apic & 0x3)) | (phys_apic & 0xF0u) )

/*
 * Mappings between logical cpu number and logical / physical apicid
 * The first four macros are trivial, but it keeps the abstraction consistent
 */
extern volatile int logical_apicid_2_cpu[];
extern volatile int cpu_2_logical_apicid[];
extern volatile int physical_apicid_2_cpu[];
extern volatile int cpu_2_physical_apicid[];

#define logical_apicid_to_cpu(apicid) logical_apicid_2_cpu[apicid]
#define cpu_to_logical_apicid(cpu) cpu_2_logical_apicid[cpu]
#define physical_apicid_to_cpu(apicid) physical_apicid_2_cpu[apicid]
#define cpu_to_physical_apicid(cpu) cpu_2_physical_apicid[cpu]
#ifdef CONFIG_MULTIQUAD			/* use logical IDs to bootstrap */
#define boot_apicid_to_cpu(apicid) logical_apicid_2_cpu[apicid]
#define cpu_to_boot_apicid(cpu) cpu_2_logical_apicid[cpu]
#else /* !CONFIG_MULTIQUAD */		/* use physical IDs to bootstrap */
#define boot_apicid_to_cpu(apicid) physical_apicid_2_cpu[apicid]
#define cpu_to_boot_apicid(cpu) cpu_2_physical_apicid[cpu]
#endif /* CONFIG_MULTIQUAD */

static inline int cpu_to_apicid (int cpu)
{
	if (clustered_apic_mode)
		return cpu_to_physical_apicid(cpu);
	else
		return cpu_to_logical_apicid(cpu);
}

#ifdef CONFIG_X86_CLUSTERED_APIC
static inline int target_cpus(void)
{
	static int cpu;
	switch(clustered_apic_mode){
		case CLUSTERED_APIC_NUMAQ:
			/* Broadcast intrs to local quad only. */
			return APIC_BROADCAST_ID_APIC;
		case CLUSTERED_APIC_XAPIC:
			/*round robin the interrupts*/
			cpu = (cpu+1)%smp_num_cpus;
			return cpu_to_physical_apicid(cpu);
		default:
			;
	}
	return cpu_online_map;
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline unsigned int cpu_mask_to_apicid (unsigned long cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;	

	if (clustered_apic_mode == CLUSTERED_APIC_NONE)
		return (unsigned int) cpumask;

	num_bits_set = hweight32(cpumask); 
	/* Return id to all */
	if (num_bits_set == 32)
		return (int) 0xFF;
	/* 
	 * The cpus in the mask must all be on the apic cluster.  If are not 
	 * on the same apicid cluster return default value of TARGET_CPUS. 
	 */
	cpu = ffs(cpumask)-1;
	if (clustered_apic_mode == CLUSTERED_APIC_XAPIC)
		return cpu_to_physical_apicid(cpu);
	apicid = cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpumask & (1 << cpu)) {
			int new_apicid = cpu_to_logical_apicid(cpu);
			if (apicid_cluster(apicid) != 
					apicid_cluster(new_apicid)){
				printk ("%s: Not a valid mask!\n",__FUNCTION__);
				return 0xFF;
			}
			apicid = apicid | new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

#else
# define target_cpus() (0xFF)
# define cpu_mask_to_apicid(cpumask) (cpumask)
#endif
#endif
