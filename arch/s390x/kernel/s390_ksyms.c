/*
 *  arch/s390/kernel/s390_ksyms.c
 *
 *  S390 version
 */
#include <linux/config.h>
#include <linux/highuid.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <asm/checksum.h>
#include <asm/delay.h>
#include <asm/pgalloc.h>
#include <asm/setup.h>
#include <asm/softirq.h>
#include <asm/ctrlchar.h>
#if CONFIG_IP_MULTICAST
#include <net/arp.h>
#endif

/*
 * memory management
 */
EXPORT_SYMBOL_NOVERS(_oi_bitmap);
EXPORT_SYMBOL_NOVERS(_ni_bitmap);
EXPORT_SYMBOL_NOVERS(_zb_findmap);
EXPORT_SYMBOL_NOVERS(_sb_findmap);
EXPORT_SYMBOL_NOVERS(__copy_from_user_asm);
EXPORT_SYMBOL_NOVERS(__copy_to_user_asm);
EXPORT_SYMBOL_NOVERS(__clear_user_asm);

/*
 * semaphore ops
 */
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);
EXPORT_SYMBOL(__down_trylock);

/*
 * string functions
 */
EXPORT_SYMBOL_NOVERS(memcmp);
EXPORT_SYMBOL_NOVERS(memset);
EXPORT_SYMBOL_NOVERS(memmove);
EXPORT_SYMBOL_NOVERS(memscan);
EXPORT_SYMBOL_NOVERS(strlen);
EXPORT_SYMBOL_NOVERS(strchr);
EXPORT_SYMBOL_NOVERS(strcmp);
EXPORT_SYMBOL_NOVERS(strncat);
EXPORT_SYMBOL_NOVERS(strncmp);
EXPORT_SYMBOL_NOVERS(strncpy);
EXPORT_SYMBOL_NOVERS(strnlen);
EXPORT_SYMBOL_NOVERS(strrchr);
EXPORT_SYMBOL_NOVERS(strstr);
EXPORT_SYMBOL_NOVERS(strtok);
EXPORT_SYMBOL_NOVERS(strpbrk);

/*
 * binfmt_elf loader 
 */
extern int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(overflowuid);
EXPORT_SYMBOL(overflowgid);

#ifdef CONFIG_S390_SUPPORT
/*
 * Dynamically add/remove 31 bit ioctl conversion functions.
 */
extern int register_ioctl32_conversion(unsigned int cmd,
				       int (*handler)(unsigned int, 
						      unsigned int,
						      unsigned long,
						      struct file *));
int unregister_ioctl32_conversion(unsigned int cmd);
EXPORT_SYMBOL(register_ioctl32_conversion);
EXPORT_SYMBOL(unregister_ioctl32_conversion);
#endif

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);
EXPORT_SYMBOL(sys_ioctl);

/*
 * misc.
 */
EXPORT_SYMBOL(machine_flags);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(console_mode);
EXPORT_SYMBOL(console_device);
EXPORT_SYMBOL_NOVERS(do_call_softirq);
EXPORT_SYMBOL(ctrlchar_init);
EXPORT_SYMBOL(ctrlchar_handle);
EXPORT_SYMBOL(show_trace);

/*
 * More
 */
#ifdef CONFIG_SMP
EXPORT_SYMBOL(cpu_online_map);
#endif
