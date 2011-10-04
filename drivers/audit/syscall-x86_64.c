/*
 * syscall-x86_64.c
 *
 * x86_64 specific system call information for the audit daemon.
 *
 * Copyright (C) 2003 Max Asbock, IBM Corp.
 *
 * Derived from syscall-i386.c and syscall.c:
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

#include <linux/net.h>
#include <asm-i386/ipc.h>

#include <linux/audit.h>
#include "audit-private.h"

extern struct sysent	linux_sysent[];
extern int		audit_get_socketargs(struct aud_syscall_data *);
extern int		audit_get_ipcargs(struct aud_syscall_data *);
extern int		audit_get_ioctlargs(struct aud_syscall_data *);

int			audit_arch = AUDIT_ARCH_X86_64;

/* System call filtering is fundamentally based on the 64bit native
 * x86_64 syscall numbers. We also need auditing for the ia32 emulation
 * mode syscalls, and these need to use the same filter tables. As
 * a crude hack, convert the ia32 syscall numbers to native ones.
 */
static int syscall_conv_ia32_to_x86_64[IA32_NR_syscalls] = {
[__NR_ia32_fork] = __NR_fork,
[__NR_ia32_vfork] = __NR_vfork,
[__NR_ia32_clone] = __NR_clone,
[__NR_ia32_execve] = __NR_execve,
[__NR_ia32_exit] = __NR_exit,
[__NR_ia32_ptrace] = __NR_ptrace,
[__NR_ia32_uselib] = __NR_uselib,
[__NR_ia32_kill] = __NR_kill,
[__NR_ia32_tkill] = __NR_tkill,
[__NR_ia32_setuid] = __NR_setuid,
[__NR_ia32_setgid] = __NR_setgid,
[__NR_ia32_setreuid] = __NR_setreuid,
[__NR_ia32_setregid] = __NR_setregid,
[__NR_ia32_setresuid] = __NR_setresuid,
[__NR_ia32_setresgid] = __NR_setresgid,
[__NR_ia32_setfsuid] = __NR_setfsuid,
[__NR_ia32_setfsgid] = __NR_setfsgid,
[__NR_ia32_setgroups] = __NR_setgroups,
[__NR_ia32_setuid32] = __NR_setuid,
[__NR_ia32_setgid32] = __NR_setgid,
[__NR_ia32_setreuid32] = __NR_setreuid,
[__NR_ia32_setregid32] = __NR_setregid,
[__NR_ia32_setresuid32] = __NR_setresuid,
[__NR_ia32_setresgid32] = __NR_setresgid,
[__NR_ia32_setfsuid32] = __NR_setfsuid,
[__NR_ia32_setfsgid32] = __NR_setfsgid,
[__NR_ia32_setgroups32] = __NR_setgroups,
[__NR_ia32_capset] = __NR_capset,
[__NR_ia32_umask] = __NR_umask,
[__NR_ia32_chroot] = __NR_chroot,
[__NR_ia32_chdir] = __NR_chdir,
[__NR_ia32_fchdir] = __NR_fchdir,
[__NR_ia32_setrlimit] = __NR_setrlimit,
[__NR_ia32_setpriority] = __NR_setpriority,
[__NR_ia32_sched_setaffinity] = __NR_sched_setaffinity,
[__NR_ia32_sched_setparam] = __NR_sched_setparam,
[__NR_ia32_sched_setscheduler] = __NR_sched_setscheduler,
[__NR_ia32_brk] = __NR_brk,
[__NR_ia32_signal] = __NR_rt_sigaction,
[__NR_ia32_rt_sigreturn] = __NR_rt_sigreturn,
[__NR_ia32_rt_sigaction] = __NR_rt_sigaction,
[__NR_ia32_rt_sigprocmask] = __NR_rt_sigprocmask,
[__NR_ia32_rt_sigpending] = __NR_rt_sigpending,
[__NR_ia32_rt_sigtimedwait] = __NR_rt_sigtimedwait,
[__NR_ia32_rt_sigqueueinfo] = __NR_rt_sigqueueinfo,
[__NR_ia32_rt_sigsuspend] = __NR_rt_sigsuspend,
[__NR_ia32_setitimer] = __NR_setitimer,
[__NR_ia32_setpgid] = __NR_setpgid,
[__NR_ia32_setsid] = __NR_setsid,
[__NR_ia32_settimeofday] = __NR_settimeofday,
[__NR_ia32_adjtimex] = __NR_adjtimex,
[__NR_ia32_stime] = __NR_settimeofday,
[__NR_ia32__sysctl] = __NR__sysctl,
[__NR_ia32_sethostname] = __NR_sethostname,
[__NR_ia32_setdomainname] = __NR_setdomainname,
[__NR_ia32_reboot] = __NR_reboot,
[__NR_ia32_create_module] = __NR_create_module,
[__NR_ia32_init_module] = __NR_init_module,
[__NR_ia32_query_module] = __NR_query_module,
[__NR_ia32_delete_module] = __NR_delete_module,
[__NR_ia32_mount] = __NR_mount,
[__NR_ia32_umount] = __NR_umount2,
[__NR_ia32_umount2] = __NR_umount2,
[__NR_ia32_swapon] = __NR_swapon,
[__NR_ia32_swapoff] = __NR_swapoff,
[__NR_ia32_ioperm] = __NR_ioperm,
[__NR_ia32_iopl] = __NR_iopl,
[__NR_ia32_syslog] = __NR_syslog,
[__NR_ia32_open] = __NR_open,
[__NR_ia32_read] = __NR_read,
[__NR_ia32_write] = __NR_write,
[__NR_ia32_close] = __NR_close,
[__NR_ia32_readv] = __NR_readv,
[__NR_ia32_writev] = __NR_writev,
[__NR_ia32_readdir] = __NR_getdents,
[__NR_ia32_sendfile] = __NR_sendfile,
[__NR_ia32_access] = __NR_access,
[__NR_ia32_creat] = __NR_creat,
[__NR_ia32_mkdir] = __NR_mkdir,
[__NR_ia32_mknod] = __NR_mknod,
[__NR_ia32_link] = __NR_link,
[__NR_ia32_symlink] = __NR_symlink,
[__NR_ia32_rename] = __NR_rename,
[__NR_ia32_unlink] = __NR_unlink,
[__NR_ia32_rmdir] = __NR_rmdir,
[__NR_ia32_utime] = __NR_utime,
[__NR_ia32_chmod] = __NR_chmod,
[__NR_ia32_chown] = __NR_chown,
[__NR_ia32_chown32] = __NR_chown,
[__NR_ia32_lchown] = __NR_lchown,
[__NR_ia32_lchown32] = __NR_lchown,
[__NR_ia32_fchown] = __NR_fchown,
[__NR_ia32_fchown32] = __NR_fchown,
[__NR_ia32_fchmod] = __NR_fchmod,
[__NR_ia32_truncate] = __NR_truncate,
[__NR_ia32_truncate64] = __NR_truncate,
[__NR_ia32_ftruncate] = __NR_ftruncate,
[__NR_ia32_ftruncate64] = __NR_ftruncate,
[__NR_ia32_setxattr] = __NR_setxattr,
[__NR_ia32_lsetxattr] = __NR_lsetxattr,
[__NR_ia32_fsetxattr] = __NR_fsetxattr,
[__NR_ia32_getxattr] = __NR_getxattr,
[__NR_ia32_lgetxattr] = __NR_lgetxattr,
[__NR_ia32_fgetxattr] = __NR_fgetxattr,
[__NR_ia32_listxattr] = __NR_listxattr,
[__NR_ia32_llistxattr] = __NR_llistxattr,
[__NR_ia32_flistxattr] = __NR_flistxattr,
[__NR_ia32_removexattr] = __NR_removexattr,
[__NR_ia32_lremovexattr] = __NR_lremovexattr,
[__NR_ia32_fremovexattr] = __NR_fremovexattr,
[__NR_ia32_socketcall] = __NR_socket, /* fixed below */
[__NR_ia32_ipc] = __NR_semop, /* fixed below */
[__NR_ia32_ioctl] = __NR_ioctl,
};

/*
 * Given the set of registers, extract the syscall code and
 * arguments array
 */
int
audit_get_args(struct pt_regs *regs, struct aud_syscall_data *sc)
{
	struct sysent	*entry;
	int		code_64, code_raw;
	int		ia32_thread = 0;
	int 		nr_syscalls = NR_syscalls;
	int		ret = -EINVAL;

	if (current->thread.flags & THREAD_IA32) {
		ia32_thread = 1;
		nr_syscalls = IA32_NR_syscalls;
	}

	sc->entry = NULL;
	sc->major = 0;
	sc->minor = 0;

	if (regs == NULL)
		return -EPERM;

	code_raw = regs->orig_rax;

	/* XXX should we try to log calls to invalid syscalls? */
	if (code_raw < 0 || code_raw >= nr_syscalls)
		return -ENOSYS;

	if (ia32_thread) {
		/* convert ia32 syscall number to native x86_64 numbering scheme */
		code_64 = syscall_conv_ia32_to_x86_64[code_raw];

		/* ugly special cases, ia32 uses multiplexed calls via socketcall()
                 * and ipc(), which x86_64 does not have. Do manual mapping.
                 */
		if (code_64 == __NR_socket) {
			switch(regs->rbx) {
			case SYS_SOCKET: code_64=__NR_socket; break;
			case SYS_BIND: code_64=__NR_bind; break; 
			case SYS_CONNECT: code_64=__NR_connect; break;
			case SYS_LISTEN: code_64=__NR_listen; break;
			case SYS_ACCEPT: code_64=__NR_accept; break;
			case SYS_GETSOCKNAME: code_64=__NR_getsockname; break;
			case SYS_GETPEERNAME: code_64=__NR_getpeername; break;
			case SYS_SOCKETPAIR: code_64=__NR_socketpair; break;
			case SYS_SEND: code_64=__NR_sendto; break; 
			case SYS_RECV: code_64=__NR_recvfrom; break; 
			case SYS_SENDTO: code_64=__NR_sendto; break;
			case SYS_RECVFROM: code_64=__NR_recvfrom; break;
			case SYS_SHUTDOWN: code_64=__NR_shutdown; break;
			case SYS_SETSOCKOPT: code_64=__NR_setsockopt; break;
			case SYS_GETSOCKOPT: code_64=__NR_getsockopt; break;
			case SYS_SENDMSG: code_64=__NR_sendmsg; break;
			case SYS_RECVMSG: code_64=__NR_recvmsg; break;
			}
		} else if (code_64 == __NR_semop) {
			switch(regs->rbx) {
			case SEMOP: code_64=__NR_semop; break;
			case SEMGET: code_64=__NR_semget; break;
			case SEMCTL: code_64=__NR_semctl; break;
			case SEMTIMEDOP: code_64=__NR_semtimedop; break;
			case MSGSND: code_64=__NR_msgsnd; break;
			case MSGRCV: code_64=__NR_msgrcv; break;
			case MSGGET: code_64=__NR_msgget; break;
			case MSGCTL: code_64=__NR_msgctl; break;
			case SHMAT: code_64=__NR_shmat; break;
			case SHMDT: code_64=__NR_shmdt; break;
			case SHMGET: code_64=__NR_shmget; break;
			case SHMCTL: code_64=__NR_shmctl; break;
			}
		}
	} else {
		code_64 = code_raw;
	}

	DPRINTF("code=%d raw=%d/%s\n", code_64, code_raw, ia32_thread ? "32" : "64");

	if (audit_policy_ignore(code_64)) {
		ret = 0;
		goto exit;
	}

	sc->arch = AUDIT_ARCH_X86_64;
	entry = &linux_sysent[code_64];

	if (ia32_thread) {
		/* always copy all registers, due to ipc(2) renumbering */
		sc->raw_args[5] = regs->rbp;
		sc->raw_args[4] = regs->rdi;
		sc->raw_args[3] = regs->rsi;
		sc->raw_args[2] = regs->rdx;
		sc->raw_args[1] = regs->rcx;
		sc->raw_args[0] = regs->rbx;
	} else {
		switch (entry->sy_narg) {
		case 6:	sc->raw_args[5] = regs->r9;  /* correct? */
		case 5:	sc->raw_args[4] = regs->r8;
		case 4: sc->raw_args[3] = regs->r10;
		case 3: sc->raw_args[2] = regs->rdx;
		case 2: sc->raw_args[1] = regs->rsi;
		case 1: sc->raw_args[0] = regs->rdi;
		case 0: break;
		default:
			printk("audit: invalid argument count?!\n");
			BUG();
		}
	}

	sc->major = code_64;
	sc->entry = entry;

	/* Special treatment for special functions */

	if (ia32_thread) {
		switch (code_raw) {
		case __NR_ia32_truncate64:
		case __NR_ia32_ftruncate64:
			/* 64bit values are actually passed as two 32bit
		 	* registers, lower one first */
			sc->raw_args[1] |= ((u_int64_t) regs->rdx) << 32;
			break;
		case __NR_ia32_socketcall:
			ret = audit_get_socketargs(sc);
			sc->minor = 0;
			goto exit;
		case __NR_ia32_ipc:
			ret = audit_get_ipcargs(sc);
			sc->minor = 0;
			goto exit;
		}
	}

	if (code_64 == __NR_ioctl) {
		ret = audit_get_ioctlargs(sc);
		goto exit;
	}

	ret = 0;
exit:
	return ret;
}

/*
 * Update system call register
 */
int
audit_update_arg(struct aud_syscall_data *sc, unsigned int n, unsigned long newval)
{
	struct pt_regs	*regs = sc->regs;
	int		code = sc->major;

	if (current->thread.flags & THREAD_IA32) {
		if ( code == __NR_ia32_ioctl 
		     ||  code == __NR_ia32_socketcall
		     ||  code == __NR_ia32_ipc)
			return -EINVAL;
	} else {
		if ( code == __NR_ioctl )
			return -EINVAL;
	}

	if (n > 5)
		return -EINVAL;

	sc->raw_args[n] = newval;

	if (current->thread.flags & THREAD_IA32) {
		switch (n) {
		case 5: regs->rbp = newval; break;
		case 4: regs->rdi = newval; break;
		case 3: regs->rsi = newval; break;
		case 2: regs->rdx = newval; break;
		case 1: regs->rcx = newval; break;
		case 0: regs->rbx = newval; break;
		}
	} else {
		switch (n) {
		case 5: regs->r9  = newval; break;
		case 4: regs->r8  = newval; break;
		case 3: regs->r10 = newval; break;
		case 2: regs->rdx = newval; break;
		case 1: regs->rsi = newval; break;
		case 0: regs->rdi = newval; break;
		}
	}

	return 0;
}

/*
 * Get the return value of a system call
 */
long
audit_get_result(struct pt_regs *regs)
{
	return regs->rax;
}
