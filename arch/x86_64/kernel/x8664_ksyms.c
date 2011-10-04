#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/mca.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/string.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/mmx.h>
#include <asm/nmi.h>
#include <linux/nmi.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/kdebug.h>
#include <asm/pgtable.h>
#include <asm/proto.h>
#define __KERNEL_SYSCALLS__ 1
#include <asm/unistd.h>

extern spinlock_t rtc_lock;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_HD) || defined(CONFIG_BLK_DEV_IDE_MODULE) || defined(CONFIG_BLK_DEV_HD_MODULE)
extern struct drive_info_struct drive_info;
EXPORT_SYMBOL(drive_info);
#endif

/* platform dependent support */
EXPORT_SYMBOL(boot_cpu_data);
//EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);
EXPORT_SYMBOL(probe_irq_mask);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(pm_idle);
EXPORT_SYMBOL(pm_power_off);

#ifdef CONFIG_IO_DEBUG
EXPORT_SYMBOL(__io_virt_debug);
#endif

EXPORT_SYMBOL_NOVERS(__down_failed);
EXPORT_SYMBOL_NOVERS(__down_failed_interruptible);
EXPORT_SYMBOL_NOVERS(__down_failed_trylock);
EXPORT_SYMBOL_NOVERS(__up_wakeup);
/* Networking helper routines. */
EXPORT_SYMBOL(csum_partial_copy_nocheck);
/* Delay loops */
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);

EXPORT_SYMBOL_NOVERS(__get_user_1);
EXPORT_SYMBOL_NOVERS(__get_user_2);
EXPORT_SYMBOL_NOVERS(__get_user_4);
EXPORT_SYMBOL_NOVERS(__get_user_8);
EXPORT_SYMBOL_NOVERS(__put_user_1);
EXPORT_SYMBOL_NOVERS(__put_user_2);
EXPORT_SYMBOL_NOVERS(__put_user_4);
EXPORT_SYMBOL_NOVERS(__put_user_8);

EXPORT_SYMBOL_NOVERS(clear_page);

EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);

EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(strnlen_user);

EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);

EXPORT_SYMBOL(pci_map_sg);
EXPORT_SYMBOL(pci_unmap_sg);

#ifdef CONFIG_PCI
EXPORT_SYMBOL(pcibios_penalize_isa_irq);
EXPORT_SYMBOL(pci_mem_start);
#endif

#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(kernel_flag_cacheline);
EXPORT_SYMBOL(smp_num_cpus);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(smp_num_siblings);
EXPORT_SYMBOL(cpu_sibling_map);
extern void __read_lock_failed(void);
extern void __write_lock_failed(void);
EXPORT_SYMBOL_NOVERS(__write_lock_failed);
EXPORT_SYMBOL_NOVERS(__read_lock_failed);

/* Global SMP irq stuff */
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL(global_irq_holder);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
EXPORT_SYMBOL(dump_smp_call_function);
EXPORT_SYMBOL(smp_call_function);

/* TLB flushing */
EXPORT_SYMBOL(flush_tlb_page);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
EXPORT_SYMBOL_GPL(touch_nmi_watchdog);
#ifdef CONFIG_PM
EXPORT_SYMBOL_GPL(set_nmi_pm_callback);
EXPORT_SYMBOL_GPL(unset_nmi_pm_callback);
#endif
#endif

#ifdef CONFIG_MCA
EXPORT_SYMBOL(machine_id);
#endif

#ifdef CONFIG_VT
EXPORT_SYMBOL(screen_info);
#endif

EXPORT_SYMBOL(get_wchan);

EXPORT_SYMBOL(rtc_lock);

EXPORT_SYMBOL_GPL(set_nmi_callback);
EXPORT_SYMBOL_GPL(unset_nmi_callback);

/* Export string functions. We normally rely on gcc builtin for most of these,
   but gcc sometimes decides not to inline them. */    
#undef memcpy
#undef memset
#undef memmove
#undef strlen
#undef strcpy
#undef strcmp
#undef strncmp
#undef strncpy
#undef strchr	
#undef strcmp 
#undef bcopy
extern void * memcpy(void *,const void *,__kernel_size_t);
extern void * memset(void *,int,__kernel_size_t);
extern __kernel_size_t strlen(const char *);
extern int strcmp(const char *,const char *);
extern char * strcpy(char *,const char *);
extern char * bcopy(const char * src, char * dest, int count);

EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL_NOVERS(__memcpy);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strcpy);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcat);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(memchr);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(bcopy);

EXPORT_SYMBOL(empty_zero_page);

#ifdef CONFIG_HAVE_DEC_LOCK
EXPORT_SYMBOL(atomic_dec_and_lock);
#endif

EXPORT_SYMBOL(die_chain);

extern void do_softirq_thunk(void);
EXPORT_SYMBOL_NOVERS(do_softirq_thunk);

extern unsigned long __supported_pte_mask; 
EXPORT_SYMBOL(__supported_pte_mask);

EXPORT_SYMBOL(init_level4_pgt);

EXPORT_SYMBOL(copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(copy_user_generic);

/* Export kernel syscalls */
EXPORT_SYMBOL(sys_exit);
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(sys_delete_module);
EXPORT_SYMBOL(sys_sync);
EXPORT_SYMBOL(sys_pause);
EXPORT_SYMBOL(sys_setsid);	/* Rather dubious */
extern long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
EXPORT_SYMBOL(sys_ioctl);


EXPORT_SYMBOL(memcpy_fromio);
EXPORT_SYMBOL(memcpy_toio);

EXPORT_SYMBOL(ip_compute_csum);


#ifdef CONFIG_DISCONTIGMEM
EXPORT_SYMBOL(memnode_shift);
EXPORT_SYMBOL(memnodemap);
EXPORT_SYMBOL(plat_node_data);
EXPORT_SYMBOL(fake_node);
#endif

extern void int_ret_from_sys_call(void);
EXPORT_SYMBOL(int_ret_from_sys_call); 
EXPORT_SYMBOL_GPL(execve);

extern int swiotlb;
EXPORT_SYMBOL(swiotlb);

extern int page_is_ram(unsigned long);
EXPORT_SYMBOL_GPL(page_is_ram);
extern int kern_addr_valid(unsigned long addr);
EXPORT_SYMBOL_GPL(kern_addr_valid);
extern unsigned long next_ram_page(unsigned long);
EXPORT_SYMBOL_GPL(next_ram_page);

EXPORT_SYMBOL_GPL(show_mem);
EXPORT_SYMBOL_GPL(show_state);
EXPORT_SYMBOL_GPL(show_regs);
