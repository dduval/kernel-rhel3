/*
 * Architecture-specific kernel symbols
 */
#include <linux/config.h>
#include <linux/module.h>

#include <linux/string.h>

EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL_NOVERS(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strpbrk);

#include <linux/irq.h>
EXPORT_SYMBOL(isa_irq_to_vector_map);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(disable_irq_nosync);

#include <linux/interrupt.h>
EXPORT_SYMBOL(probe_irq_mask);

#include <linux/in6.h>
#include <asm/checksum.h>
/* not coded yet?? EXPORT_SYMBOL(csum_ipv6_magic); */
EXPORT_SYMBOL(csum_partial_copy_nocheck);
EXPORT_SYMBOL(csum_tcpudp_magic);
EXPORT_SYMBOL(ip_compute_csum);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_nofold);

#include <asm/io.h>
EXPORT_SYMBOL(__ia64_memcpy_fromio);
EXPORT_SYMBOL(__ia64_memcpy_toio);
EXPORT_SYMBOL(__ia64_memset_c_io);
EXPORT_SYMBOL(io_space);
EXPORT_SYMBOL(screen_info);

#include <asm/semaphore.h>
EXPORT_SYMBOL_NOVERS(__down);
EXPORT_SYMBOL_NOVERS(__down_interruptible);
EXPORT_SYMBOL_NOVERS(__down_trylock);
EXPORT_SYMBOL_NOVERS(__up);

#include <asm/page.h>
EXPORT_SYMBOL(clear_page);

#include <asm/pgtable.h>
EXPORT_SYMBOL(vmalloc_end);
EXPORT_SYMBOL(ia64_page_valid);

#include <asm/processor.h>
# ifndef CONFIG_NUMA
EXPORT_SYMBOL(_cpu_data);
# endif
EXPORT_SYMBOL(kernel_thread);

#include <asm/system.h>
#ifdef CONFIG_IA64_DEBUG_IRQ
EXPORT_SYMBOL(last_cli_ip);
#endif

#include <asm/pgalloc.h>

EXPORT_SYMBOL(flush_tlb_range);

#ifdef CONFIG_SMP

EXPORT_SYMBOL(smp_flush_tlb_all);

#include <asm/current.h>
#include <asm/hardirq.h>
EXPORT_SYMBOL(synchronize_irq);

#include <asm/smp.h>
EXPORT_SYMBOL(dump_smp_call_function);
EXPORT_SYMBOL(smp_call_function);
EXPORT_SYMBOL(smp_call_function_single);
EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(ia64_cpu_to_sapicid);

#include <linux/smp.h>
EXPORT_SYMBOL(smp_num_cpus);

#include <linux/pm.h>
EXPORT_SYMBOL(pm_idle);

#include <asm/smplock.h>
EXPORT_SYMBOL(kernel_flag);

/* #include <asm/system.h> */
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);

#else /* !CONFIG_SMP */

EXPORT_SYMBOL(local_flush_tlb_all);

#endif /* !CONFIG_SMP */

#include <asm/uaccess.h>
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(__do_clear_user);
EXPORT_SYMBOL(__strlen_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(__strnlen_user);

#include <asm/unistd.h>
EXPORT_SYMBOL(__ia64_syscall);

/* from arch/ia64/lib */
extern void __divsi3(void);
extern void __udivsi3(void);
extern void __modsi3(void);
extern void __umodsi3(void);
extern void __divdi3(void);
extern void __udivdi3(void);
extern void __moddi3(void);
extern void __umoddi3(void);

EXPORT_SYMBOL_NOVERS(__divsi3);
EXPORT_SYMBOL_NOVERS(__udivsi3);
EXPORT_SYMBOL_NOVERS(__modsi3);
EXPORT_SYMBOL_NOVERS(__umodsi3);
EXPORT_SYMBOL_NOVERS(__divdi3);
EXPORT_SYMBOL_NOVERS(__udivdi3);
EXPORT_SYMBOL_NOVERS(__moddi3);
EXPORT_SYMBOL_NOVERS(__umoddi3);

extern unsigned long ia64_iobase;
EXPORT_SYMBOL(ia64_iobase);

#include <asm/pal.h>
EXPORT_SYMBOL(ia64_pal_call_phys_stacked);
EXPORT_SYMBOL(ia64_pal_call_phys_static);
EXPORT_SYMBOL(ia64_pal_call_stacked);
EXPORT_SYMBOL(ia64_pal_call_static);
EXPORT_SYMBOL(ia64_load_scratch_fpregs);
EXPORT_SYMBOL(ia64_save_scratch_fpregs);

extern struct efi efi;
EXPORT_SYMBOL(efi);

#include <linux/proc_fs.h>
extern struct proc_dir_entry *efi_dir;
EXPORT_SYMBOL(efi_dir);

#include <asm/machvec.h>
#ifdef CONFIG_IA64_GENERIC
EXPORT_SYMBOL(ia64_mv);
#endif
EXPORT_SYMBOL(machvec_noop);
#ifdef CONFIG_PERFMON
#include <asm/perfmon.h>
EXPORT_SYMBOL(pfm_install_alternate_syswide_subsystem);
EXPORT_SYMBOL(pfm_remove_alternate_syswide_subsystem);
#endif

#include <asm/dma.h>
EXPORT_SYMBOL(MAX_DMA_ADDRESS);

#include <asm/iosapic.h>
EXPORT_SYMBOL_GPL(iosapic_fixup_pci_interrupt);

#include <linux/efi.h>
EXPORT_SYMBOL(efi_mem_type);

#include <linux/acpi.h>
extern acpi_status acpi_hp_csr_space(acpi_handle obj, u64 *csr_base, u64 *csr_length);
EXPORT_SYMBOL(acpi_hp_csr_space);

#include <linux/elf.h>
#include <asm/unwind.h>
extern void ia64_do_copy_regs (struct unw_frame_info *info, void *arg);
EXPORT_SYMBOL(ia64_do_copy_regs);
extern void ia64_freeze_cpu (struct unw_frame_info *info, void *arg);
EXPORT_SYMBOL(ia64_freeze_cpu);
extern void ia64_start_dump(struct unw_frame_info *, void *arg);
EXPORT_SYMBOL(ia64_start_dump);

extern int page_is_ram (unsigned long pagenr);
EXPORT_SYMBOL(page_is_ram);
EXPORT_SYMBOL(unw_init_running);
extern unsigned long next_ram_page(unsigned long);
EXPORT_SYMBOL_GPL(next_ram_page);

extern int init_dump;
EXPORT_SYMBOL(init_dump);

EXPORT_SYMBOL_GPL(show_mem);
EXPORT_SYMBOL_GPL(show_state);
EXPORT_SYMBOL_GPL(show_regs);
EXPORT_SYMBOL(pm_power_off);
