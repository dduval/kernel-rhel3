#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>      /* for module_init/exit */
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>

#include <asm/e820.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>


/* this file, present in the -BOOT kernel, informs anaconda about which 
   kernels this system needs. While not impossible to do in userspace,
   the kernel has the authorative list of defective bioses (440GX etc) that
   have special needs.
 */

#define cpu_has_cmov    (test_bit(X86_FEATURE_CMOV, boot_cpu_data.x86_capability))


extern char rpmarchitecture[];
extern char rpmkerneltype[];
extern int skip_ioapic_setup;

extern struct e820map e820;
extern struct cpuinfo_x86 boot_cpu_data;

static inline int needbigmem()
{
        int i;
        
        /* no pae no bigmem */
        if ( (!cpu_has_pae) || (!cpu_has_xmm) )
        	return 0;

        for (i = 0; i < e820.nr_map; i++) {
                switch (e820.map[i].type) {
                case E820_RAM:  
                		if (e820.map[i].addr > 0xffffffff)
                			return 1;
                		if (e820.map[i].addr+e820.map[i].size > 0xffffffff)
                			return 1;
                                break;
                default:        
                                break;
                }
        }
	return 0;
}

static inline void cputype(void)
{
	/* i386 works always */
	sprintf(rpmarchitecture,"i386");
	/* test for i586 and up */
	if (boot_cpu_data.x86_model<5)
		return;
	sprintf(rpmarchitecture,"i586 i386");
	/* i686 and above needs cmov support */
	if (!cpu_has_cmov)
		return;
	sprintf(rpmarchitecture,"i686 i586 i386");
	
	/* athlon */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) && 
	    (boot_cpu_data.x86>=6) )
		sprintf(rpmarchitecture,"athlon i686 i586 i386");
	
}

int __init rpmhelper_init(void)
{        
	int ent=0,smp=0;
	/* > 4Gb ram addressable -> Enterprise kernel */
	ent = needbigmem();
	/* 440GX or similarly broken bios ? -> smp kernel */
	#if CONFIG_X86_LOCAL_APIC
	if (skip_ioapic_setup==0)
		smp = 1;
	#endif
	
	if (ent && smp)
		sprintf(rpmkerneltype,"bigmem smp");
	else
		if (smp)
			sprintf(rpmkerneltype,"smp");
	
	cputype();
	
	return 0;
}


void rpmhelper_exit(void)
{
}

module_init(rpmhelper_init);
module_exit(rpmhelper_exit);

