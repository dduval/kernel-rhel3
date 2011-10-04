/*
 * syscall-ia64.c
 *
 * ia64 specific system call information for the audit daemon.
 *
 * Copyright (C) 2004 Hewlett-Packard Co
 *	Ray Lanza <ray.lanza@hp.com>
 *
 * Derived from syscall-x86_64.c
 *
 * Copyright (C) 2003 Max Asbock, IBM Corp.
 *
 * Derived from syscall-i386.c, syscall-x86_64.c and syscall.c:
 * Copyright (C) 2003 SuSE Linux AG
 * Written by okir@suse.de, based on ideas from systrace, by
 * Niels Provos (OpenBSD) and ported to Linux by Marius Aamodt Eriksen.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/utime.h>
#include <linux/sysctl.h>

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/shm.h>

#include <asm/ipc.h>

#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/semaphore.h>
#include <asm/unistd.h>
#include <asm/ia32_unistd.h>

#include <linux/audit.h>
#include "audit-private.h"

extern struct sysent	linux_sysent[];
extern int		audit_get_socketargs(struct aud_syscall_data *);
extern int		audit_get_ipcargs(struct aud_syscall_data *);
extern int		audit_get_ioctlargs(struct aud_syscall_data *);

int			audit_arch = AUDIT_ARCH_IA64;


/* copied from syscall.c, need to consolidate */
#define T_void          { AUDIT_ARG_END }
#define T_immediate(T)  { AUDIT_ARG_IMMEDIATE, sizeof(T) }
#define T_signedimm(T)  { AUDIT_ARG_IMMEDIATE, sizeof(T), .sa_flags = AUD_ARG_SIGNED }
#define T_pointer(T)    { AUDIT_ARG_POINTER, sizeof(T) }
#define T_string        { AUDIT_ARG_STRING }
#define T_path          { AUDIT_ARG_PATH }
#define T_path_parent   { AUDIT_ARG_PATH, .sa_flags = AUD_ARG_DIRNAME }
#define T_filedesc      { AUDIT_ARG_FILEDESC }
#define T_int           T_signedimm(int)
#define T_uint          T_immediate(int)
#define T_long          T_signedimm(long)
#define T_ulong         T_immediate(long)
#define T_off_t         T_signedimm(off_t)
#define T_loff_t        T_signedimm(loff_t)
#define T_mode_t        T_immediate(mode_t)
#define T_size_t        T_immediate(size_t)
#define T_dev_t         T_immediate(dev_t)
#define T_pid_t         T_immediate(pid_t)
#define T_uid_t         T_immediate(uid_t)
#define T_gid_t         T_immediate(gid_t)
#define T_u16_t         { AUDIT_ARG_IMMEDIATE, 2 }
#define T_u32_t         { AUDIT_ARG_IMMEDIATE, 4 }
#define T_u64_t         { AUDIT_ARG_IMMEDIATE, 8 }
#define T_any_ptr       { AUDIT_ARG_POINTER, 0 }
#define T_timeval_t     T_pointer(struct timeval)
#define T_timezone_t    T_pointer(struct timezone)
#define T_timex_t       T_pointer(struct timex)
#define T_caphdr_t      T_pointer(struct __user_cap_header_struct)
#define T_capdata_t     T_pointer(struct __user_cap_data_struct)
#define T_sysctl_t      T_pointer(struct __sysctl_args)
#define T_rlimit_t      T_pointer(struct rlimit)
#define T_socklen_t     T_immediate(socklen_t)
#define T_sigset_t      T_pointer(sigset_t)
#define socklen_t       int     /* socklen_t is a user land thing */
#define T_array(itype, index, max) \
                        { AUDIT_ARG_ARRAY, sizeof(itype), index, max }
#define T_opaque_t(idx) { AUDIT_ARG_ARRAY, 1, idx+1, 256 }

/* ia64 supports 64-bit and 32-bit syscalls. They use different codes
 * therefore we need a second table
 */
static struct sysent linux_sysent_ia32[IA32_NR_syscalls] = {
[__NR_ia32_fork]	= { 0, { T_void } },
[__NR_ia32_vfork]	= { 0, { T_void } },
[__NR_ia32_clone]	= { 1, { T_uint } },
[__NR_ia32_execve]	= { 3, { T_path, T_any_ptr, T_any_ptr } },
[__NR_ia32_exit]	= { 1, { T_int } },
[__NR_ia32_ptrace]	= { 4, { T_uint, T_pid_t, T_any_ptr, T_any_ptr } },
[__NR_ia32_uselib]	= { 1, { T_path } },
[__NR_ia32_kill]	= { 2, { T_pid_t, T_int } },

[__NR_ia32_setuid]	= { 1, { T_u16_t } },
[__NR_ia32_setgid]	= { 1, { T_u16_t } },
[__NR_ia32_setreuid]	= { 2, { T_u16_t, T_u16_t } },
[__NR_ia32_setregid]	= { 2, { T_u16_t, T_u16_t } },
[__NR_ia32_setresuid]	= { 3, { T_u16_t, T_u16_t, T_u16_t } },
[__NR_ia32_setresgid]	= { 3, { T_u16_t, T_u16_t, T_u16_t } },
[__NR_ia32_setfsuid]	= { 1, { T_u16_t } },
[__NR_ia32_setfsgid]	= { 1, { T_u16_t } },
[__NR_ia32_setgroups]	= { 2, { T_size_t, T_array(__u16, 0, NGROUPS) } },
[__NR_ia32_setuid32]	= { 1, { T_uid_t } },
[__NR_ia32_setgid32]	= { 1, { T_gid_t } },
[__NR_ia32_setreuid32]	= { 2, { T_uid_t, T_uid_t } },
[__NR_ia32_setregid32]	= { 2, { T_gid_t, T_gid_t } },
[__NR_ia32_setresuid32]	= { 3, { T_uid_t, T_uid_t, T_uid_t } },
[__NR_ia32_setresgid32]	= { 3, { T_gid_t, T_gid_t, T_gid_t } },
[__NR_ia32_setfsuid32]	= { 1, { T_uid_t } },
[__NR_ia32_setfsgid32]	= { 1, { T_uid_t } },
[__NR_ia32_setgroups32]	= { 2, { T_size_t, T_array(gid_t, 0, NGROUPS) } },
[__NR_ia32_capset]	= { 2, { T_caphdr_t, T_capdata_t } },
[__NR_ia32_umask]		= { 1, { T_mode_t } },
[__NR_ia32_chroot]		= { 1, { T_path } },
[__NR_ia32_chdir]		= { 1, { T_path } },
[__NR_ia32_fchdir]		= { 1, { T_filedesc } },
[__NR_ia32_setrlimit]		= { 2, { T_int, T_rlimit_t } },
[__NR_ia32_setpriority]		= { 2, { T_int, T_int } },
[__NR_ia32_sched_setaffinity]	= { 3, { T_pid_t, T_int, T_pointer(long) } },
[__NR_ia32_sched_setparam]	= { 2, { T_pid_t, T_pointer(struct sched_param) } },
[__NR_ia32_sched_setscheduler]	= { 3, { T_pid_t, T_int, T_pointer(struct sched_param) } },
[__NR_ia32_brk]			= { 1, { T_any_ptr } },
[__NR_ia32_signal]		= { 2, { T_int, T_any_ptr } },
[__NR_ia32_rt_sigreturn]	= { 1, { T_long } },
[__NR_ia32_rt_sigaction]	= { 3, { T_int, T_pointer(struct sigaction), T_any_ptr } },
[__NR_ia32_rt_sigprocmask]	= { 3, { T_int, T_sigset_t, T_any_ptr } },
[__NR_ia32_rt_sigpending]	= { 1, { T_any_ptr } },
[__NR_ia32_rt_sigtimedwait]	= { 3, { T_sigset_t, T_any_ptr, T_pointer(struct timespec) } },
[__NR_ia32_rt_sigqueueinfo]	= { 1, { T_void } },
[__NR_ia32_rt_sigsuspend]	= { 1, { T_pointer(sigset_t) } },
[__NR_ia32_setitimer]		= { 3, { T_int, T_any_ptr, T_any_ptr } },
[__NR_ia32_setpgid]		= { 2, { T_pid_t, T_pid_t } },
[__NR_ia32_setsid]		= { 1, { T_void } },
[__NR_ia32_settimeofday]	= { 2, { T_timeval_t, T_timezone_t } },

[__NR_ia32__sysctl]		= { 1, { T_sysctl_t } },
[__NR_ia32_sethostname]		= { 2, { T_array(char, 1, 256), T_size_t } },
[__NR_ia32_setdomainname]	= { 2, { T_array(char, 1, __NEW_UTS_LEN), T_size_t } },
[__NR_ia32_reboot]		= { 4, { T_int, T_int, T_int, T_any_ptr } },

[__NR_ia32_mount]		= { 5, { T_string, T_path, T_string, T_long, T_any_ptr } },
[__NR_ia32_umount]		= { 1, { T_path } },
[__NR_ia32_umount2]		= { 2, { T_path, T_int } },
[__NR_ia32_swapon]		= { 2, { T_path, T_int } },
[__NR_ia32_swapoff]		= { 1, { T_path } },
[__NR_ia32_ioperm]		= { 3, { T_long, T_long, T_int } },
[__NR_ia32_iopl]		= { 1, { T_int } },
[__NR_ia32_syslog]		= { 3, { T_int, T_any_ptr, T_int } },
[__NR_ia32_open]		= { 3, { T_path, T_int, T_mode_t } },
[__NR_ia32_read]		= { 3, { T_filedesc, T_any_ptr, T_size_t } },
[__NR_ia32_write]		= { 3, { T_filedesc, T_any_ptr, T_size_t } },
[__NR_ia32_close]		= { 1, { T_filedesc } },
[__NR_ia32_readv]		= { 3, { T_filedesc, T_any_ptr, T_size_t } },
[__NR_ia32_writev]		= { 3, { T_filedesc, T_any_ptr, T_size_t } },
[__NR_ia32_readdir]		= { 3, { T_filedesc, T_any_ptr, T_size_t } },
[__NR_ia32_sendfile]		= { 4, { T_filedesc, T_filedesc, T_off_t, T_size_t } },
[__NR_ia32_access]		= { 2, { T_path, T_int } },
[__NR_ia32_creat]		= { 2, { T_path, T_mode_t } },
[__NR_ia32_mkdir]		= { 2, { T_path_parent, T_mode_t } },
[__NR_ia32_mknod]		= { 3, { T_path_parent, T_mode_t, T_dev_t } },
[__NR_ia32_link]		= { 2, { T_path, T_path_parent } },
[__NR_ia32_symlink]		= { 2, { T_string, T_path_parent } },
[__NR_ia32_rename]		= { 2, { T_path, T_path_parent } },
[__NR_ia32_unlink]		= { 1, { T_path } },
[__NR_ia32_rmdir]		= { 1, { T_path } },
[__NR_ia32_utime]		= { 2, { T_path, T_pointer(struct utimbuf) } },
[__NR_ia32_chmod]		= { 2, { T_path, T_mode_t } },
[__NR_ia32_chown]		= { 3, { T_path, T_u16_t, T_u16_t } },
[__NR_ia32_chown32]		= { 3, { T_path, T_uid_t, T_gid_t } },
[__NR_ia32_lchown]		= { 3, { T_path_parent, T_u16_t, T_u16_t } },
[__NR_ia32_lchown32]		= { 3, { T_path_parent, T_uid_t, T_gid_t } },
[__NR_ia32_fchown]		= { 3, { T_filedesc, T_u16_t, T_u16_t } },
[__NR_ia32_fchown32]		= { 3, { T_filedesc, T_uid_t, T_gid_t } },
[__NR_ia32_fchmod]		= { 2, { T_filedesc, T_mode_t } },
[__NR_ia32_truncate]		= { 2, { T_path, T_size_t } },
[__NR_ia32_truncate64]		= { 2, { T_path, T_u64_t } },
[__NR_ia32_ftruncate]		= { 2, { T_filedesc, T_size_t } },
[__NR_ia32_ftruncate64]		= { 2, { T_filedesc, T_u64_t } },

[__NR_ia32_socketcall]	= { 2, { T_int, T_any_ptr } },
[__NR_ia32_ipc]		= { 6, { T_int, T_long, T_long, T_long, T_long, T_long } },
[__NR_ia32_ioctl]	= { 3, { T_filedesc, T_int, T_any_ptr } },

};

#define MAX_SYSCALLS 1300

/*
 * Given the set of registers, extract the syscall code and
 * arguments array
 */
int
audit_get_args(struct pt_regs *regs, struct aud_syscall_data *sc)
{
	struct sysent	*entry;
	int		code, i;
	int		ia32_thread;
	int 		nr_syscalls;

	unsigned long 	psr = regs->cr_ipsr;	/* process status word */
	unsigned long	*bsp = sc->bsp;		/* backing store pointer */
	
	sc->entry = NULL;
	sc->major = 0;
	sc->minor = 0;

	if (regs == NULL)
		return -EPERM;

	if (psr & IA64_PSR_IS) {
		if (audit_disable_32bit)
			audit_kill_process(-ENOSYS);

		code = regs->r1;
		sc->arch = AUDIT_ARCH_I386;
		nr_syscalls = NR_syscalls;
		ia32_thread = 1;
	} else {
		code = regs->r15;
		sc->arch = AUDIT_ARCH_IA64;
		nr_syscalls = MAX_SYSCALLS;
		ia32_thread = 0;
	}

	if (code < 0 || code >= nr_syscalls)
		return -ENOSYS;

	if (audit_policy_ignore(code))
		return 0;

	if( ia32_thread )
		entry = &linux_sysent_ia32[code];
	else
		entry = &linux_sysent[code];

	/*
	 * On ia64 the syscall arguments are on the register
	 * stack. Fetch the pointer and flush the register
	 * stack to insure the backing store contains
	 * the arguments.
	 */

	asm volatile (";;flushrs;;");

	switch (8) {
	case 8: sc->raw_args[7] = bsp[7];
	case 7: sc->raw_args[6] = bsp[6];
	case 6:	sc->raw_args[5] = bsp[5];
	case 5:	sc->raw_args[4] = bsp[4];
	case 4: sc->raw_args[3] = bsp[3];
	case 3: sc->raw_args[2] = bsp[2];
	case 2: sc->raw_args[1] = bsp[1];
	case 1: sc->raw_args[0] = bsp[0];
	case 0: break;
	default:
		printk("audit: invalid argument count?!\n");
		BUG();
	}

	sc->major = code;
	sc->entry = entry;

	/* Special treatment for special functions */

	if (code == __NR_ioctl || code == __NR_ia32_ioctl)
		return audit_get_ioctlargs(sc);
	return 0;
}

/*
 * Update system call register
 */
int
audit_update_arg(struct aud_syscall_data *sc, unsigned int n, unsigned long newval)
{
	struct pt_regs	*regs = sc->regs;
	int		code = sc->major;
	unsigned long	*bsp, rsc;

	if (n > 8)
		return -EINVAL;

	bsp = sc->bsp;
	sc->raw_args[n] = newval;

	switch (n) {
	case 7: bsp[7] = newval; break;
	case 6: bsp[6] = newval; break;
	case 5: bsp[5] = newval; break;
	case 4: bsp[4] = newval; break;
	case 3: bsp[3] = newval; break;
	case 2: bsp[2] = newval; break;
	case 1: bsp[1] = newval; break;
	case 0: bsp[0] = newval; break;
	}

	/*
	 * make sure the arguments are filled on the way out
	 */
	asm volatile ("mov %0=ar.rsc" : "=r"(rsc));
	asm volatile ("mov ar.rsc=0;;loadrs;;mov ar.rsc=%0" : "=r"(rsc));

	return 0;
}

/*
 * Get the return value of a system call
 *
 * The ia64 calling convention is different than other platforms: 
 *
 *     r10 contains an error indicator (0:success, -1:error).
 *
 *     r8 contains either the return value, or (if r10 indicates error)
 *     the errno value as a POSITIVE integer.
 *
 * Audit expects that (-errno) is returned in fail cases.
 */
long
audit_get_result(struct pt_regs *regs)
{
	if (regs->r10 == 0)
		return regs->r8;
	else
		return -regs->r8;
}
