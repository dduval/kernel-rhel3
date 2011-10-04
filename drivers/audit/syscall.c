/*
 * syscall.c
 *
 * Common system call information for the audit daemon.
 *
 * Copyright (C) 2003 SuSE Linux AG
 * Written by okir@suse.de.
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
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>
#include <linux/route.h>

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <asm/ipc.h>

#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/semaphore.h>
#include <asm/unistd.h>

#include <linux/audit.h>
#include "audit-private.h"

#define MAX_SOCKETCALL	20
#define MAX_IPCCALL	24

/*
 * Maximum of bytes we copy when logging ioctl's third argument
 */
#define MAX_IOCTL_COPY	256

/*
 * Note - using [__NR_xxx] to initialize the array makes us more
 * hardware independent. Platform specific syscalls can easily
 * be included in #ifdef __NR_foobar/#endif.
 * The only remaining problem is platforms with more than one
 * exec domain by default.
 */

#define f(name, args...) [__NR_##name] = { 0, { args , { AUDIT_ARG_END } } }

#define T_void		{ AUDIT_ARG_END }
#define T_immediate(T)	{ AUDIT_ARG_IMMEDIATE, sizeof(T) }
#define T_signedimm(T)	{ AUDIT_ARG_IMMEDIATE, sizeof(T), .sa_flags = AUD_ARG_SIGNED }
#define T_pointer(T)	{ AUDIT_ARG_POINTER, sizeof(T) }
#define T_pointer_rw(T)	{ AUDIT_ARG_POINTER, sizeof(T), .sa_flags = AUD_ARG_INOUT }
#define T_string	{ AUDIT_ARG_STRING }
#define T_path		{ AUDIT_ARG_PATH }
#define T_path_parent	{ AUDIT_ARG_PATH, .sa_flags = AUD_ARG_DIRNAME }
#define T_filedesc	{ AUDIT_ARG_FILEDESC }
#define T_int		T_signedimm(int)
#define T_uint		T_immediate(int)
#define T_long		T_signedimm(long)
#define T_ulong		T_immediate(long)
#define T_off_t		T_signedimm(off_t)
#define T_loff_t	T_signedimm(loff_t)
#define T_mode_t	T_immediate(mode_t)
#define T_size_t	T_immediate(size_t)
#define T_dev_t		T_immediate(dev_t)
#define T_pid_t		T_signedimm(pid_t)
#define T_uid_t		T_immediate(uid_t)
#define T_gid_t		T_immediate(gid_t)
#define T_u16_t		{ AUDIT_ARG_IMMEDIATE, 2 }
#define T_u32_t		{ AUDIT_ARG_IMMEDIATE, 4 }
#define T_u64_t		{ AUDIT_ARG_IMMEDIATE, 8 }
#define T_any_ptr	{ AUDIT_ARG_POINTER, 0 }
#define T_timeval_t	T_pointer(struct timeval)
#define T_timezone_t	T_pointer(struct timezone)
#define T_caphdr_t	T_pointer(struct __user_cap_header_struct)
#define T_capdata_t	T_pointer(struct __user_cap_data_struct)
#define T_sysctl_t	T_pointer(struct __sysctl_args)
#define T_rlimit_t	T_pointer(struct rlimit)
#define T_socklen_t	T_immediate(socklen_t)
#define T_sigset_t	T_pointer(sigset_t)
#define socklen_t	int	/* socklen_t is a user land thing */
#define T_array(itype, index, max) \
			{ AUDIT_ARG_ARRAY, sizeof(itype), index, max }
#define T_opaque_t(idx)	{ AUDIT_ARG_ARRAY, 1, idx, 256 }
#define T_argv		{ AUDIT_ARG_VECTOR, sizeof(char *), .sa_ref = AUDIT_ARG_STRING }

/*
 * This follows from the audit deamon.  32bit system calls have 0 based
 * numbers as on x86 while the 64bit native calls on ia64 start at 1024.
 */
#ifdef __ia64__ 
#define MAX_SYSCALLS 1300
#else
#define MAX_SYSCALLS NR_syscalls
#endif

struct sysent linux_sysent[MAX_SYSCALLS+1] = {
#ifdef __NR_fork
f(fork,		T_void),
f(vfork,	T_void),
#endif
f(clone,	T_uint),
#ifdef __NR_clone2
f(clone2,	T_uint),
#endif
f(execve,	T_path, T_argv, T_any_ptr),
f(exit,		T_int),
f(ptrace,	T_uint, T_pid_t, T_any_ptr, T_any_ptr),
f(uselib,	T_path),
f(kill,		T_pid_t, T_int),
f(tkill,	T_pid_t, T_int),

/*
 * Calls related to process privilege
 */
#ifdef __NR_setuid32
f(setuid,	T_u16_t),
f(setgid,	T_u16_t),
f(setreuid,	T_u16_t, T_u16_t),
f(setregid,	T_u16_t, T_u16_t),
f(setresuid,	T_u16_t, T_u16_t, T_u16_t),
f(setresgid,	T_u16_t, T_u16_t, T_u16_t),
f(setfsuid,	T_u16_t),
f(setfsgid,	T_u16_t),
f(setgroups,	T_size_t, T_array(__u16, 0, NGROUPS)),
f(setuid32,	T_uid_t),
f(setgid32,	T_gid_t),
f(setreuid32,	T_uid_t, T_uid_t),
f(setregid32,	T_gid_t, T_gid_t),
f(setresuid32,	T_uid_t, T_uid_t, T_uid_t),
f(setresgid32,	T_gid_t, T_gid_t, T_gid_t),
f(setfsuid32,	T_uid_t),
f(setfsgid32,	T_uid_t),
f(setgroups32,	T_size_t, T_array(gid_t, 0, NGROUPS)),
#else
/* architectures where uids and gids have been 32 bits all along */
f(setuid,	T_uid_t),
f(setgid,	T_gid_t),
f(setreuid,	T_uid_t, T_uid_t),
f(setregid,	T_gid_t, T_gid_t),
f(setresuid,	T_uid_t, T_uid_t, T_uid_t),
f(setresgid,	T_gid_t, T_gid_t, T_gid_t),
f(setfsuid,	T_uid_t),
f(setfsgid,	T_uid_t),
f(setgroups,	T_size_t, T_array(gid_t, 0, NGROUPS)),
#endif
f(capset,	T_caphdr_t, T_capdata_t),

/*
 * Other per-process state
 */
f(umask,	T_mode_t),
f(chroot,	T_path),
f(chdir,	T_path),
f(fchdir,	T_filedesc),
f(setrlimit,	T_int, T_rlimit_t),
f(setpriority,	T_int, T_int),
f(sched_setaffinity, T_pid_t, T_int, T_pointer(long)),
f(sched_setparam, T_pid_t, T_pointer(struct sched_param)),
f(sched_setscheduler, T_pid_t, T_int, T_pointer(struct sched_param)),
f(brk,		T_any_ptr),
#ifdef __NR_signal
f(signal,	T_int, T_any_ptr),
#endif
#ifdef __NR_sigaction
f(sigaction,	T_int, T_pointer(struct sigaction), T_any_ptr),
f(sigprocmask,	T_int, T_sigset_t, T_any_ptr),
f(sigpending,	T_any_ptr),
f(sigsuspend,	T_pointer(sigset_t)),
f(sigreturn,	T_long),
#endif
f(sigaltstack,	T_pointer(stack_t), T_any_ptr),
f(rt_sigaction,	T_int, T_pointer(struct sigaction), T_any_ptr),
f(rt_sigprocmask, T_int, T_sigset_t, T_any_ptr),
f(rt_sigpending,T_any_ptr),
f(rt_sigqueueinfo,T_void),
f(rt_sigreturn,	T_long),
f(rt_sigsuspend,T_pointer(sigset_t)),
f(rt_sigtimedwait,T_sigset_t, T_any_ptr, T_pointer(struct timespec)),
f(setitimer,	T_int, T_any_ptr, T_any_ptr),
f(setpgid,	T_pid_t, T_pid_t),
f(setsid,	T_void),


/*
 * Calls related to global machine state
 */
f(settimeofday,	T_timeval_t, T_timezone_t),
f(adjtimex,	T_pointer_rw(struct timex)),
#ifdef __NR_stime
f(stime,	T_pointer(int)),
#endif
f(_sysctl,	T_sysctl_t),
f(sethostname,	T_array(char, 1, 256), T_size_t),
f(setdomainname, T_array(char, 1, __NEW_UTS_LEN), T_size_t),
f(reboot,	T_int, T_int, T_int, T_any_ptr),
f(create_module,T_string, T_size_t),
f(init_module,	T_string, T_any_ptr),
f(query_module,	T_string, T_int, T_any_ptr, T_size_t, T_any_ptr),
f(delete_module,T_string),
f(mount,	T_string, T_path, T_string, T_long, T_any_ptr),
#ifdef __NR_umount
f(umount,	T_path),
#endif
#ifdef __NR_umount2
f(umount2,	T_path, T_int),
#endif
f(swapon,	T_path, T_int),
f(swapoff,	T_path),
#ifdef __NR_ioperm
f(ioperm,	T_long, T_long, T_int),
#endif
#ifdef __NR_iopl
f(iopl,		T_int),
#endif
f(syslog,	T_int, T_any_ptr, T_int),
#ifdef __NR_pciconfig_write
f(pciconfig_read, T_ulong, T_ulong, T_ulong, T_ulong, T_any_ptr),
f(pciconfig_write, T_ulong, T_ulong, T_ulong, T_ulong, T_any_ptr),
#ifdef __NR_pciconfig_iobase
f(pciconfig_iobase, T_long, T_ulong, T_ulong),
#endif /* __NR_pciconfig_iobase */
#endif

/*
 * File system operations
 */
f(open,		T_path, T_int, T_mode_t),
f(read,		T_filedesc, T_any_ptr, T_size_t),
f(write,	T_filedesc, T_any_ptr, T_size_t),
f(pread,	T_filedesc, T_any_ptr, T_size_t, T_size_t),
f(pwrite,	T_filedesc, T_any_ptr, T_size_t, T_size_t),
f(close,	T_filedesc),
#ifdef __NR_readv
f(readv,	T_filedesc, T_any_ptr, T_size_t),
f(writev,	T_filedesc, T_any_ptr, T_size_t),
#endif
#ifdef __NR_readdir
f(readdir,	T_filedesc, T_any_ptr, T_size_t),
#endif
#ifdef __NR_sendfile
f(sendfile,	T_filedesc, T_filedesc, T_pointer(off_t), T_size_t),
#endif

f(access,	T_path, T_int),
f(creat,	T_path, T_mode_t),
f(mkdir,	T_path_parent, T_mode_t),
f(mknod,	T_path_parent, T_mode_t, T_dev_t),
f(link,		T_path_parent, T_path_parent),
f(symlink,	T_path_parent, T_path_parent),
f(rename,	T_path_parent, T_path_parent),
f(unlink,	T_path_parent),
f(rmdir,	T_path_parent),
#ifdef __NR_utime
f(utime,	T_path, T_pointer(struct utimbuf)),
#endif
#ifdef __NR_utimes
f(utimes,	T_path, T_timeval_t),
#endif
f(chmod,	T_path, T_mode_t),
#ifdef __NR_chown32
f(chown,	T_path, T_u16_t, T_u16_t),
f(chown32,	T_path, T_uid_t, T_gid_t),
f(lchown,	T_path_parent, T_u16_t, T_u16_t),
f(lchown32,	T_path_parent, T_uid_t, T_gid_t),
f(fchown,	T_filedesc, T_u16_t, T_u16_t),
f(fchown32,	T_filedesc, T_uid_t, T_gid_t),
f(fchmod,	T_filedesc, T_mode_t),
#else
f(chown,	T_path, T_uid_t, T_uid_t),
f(lchown,	T_path_parent, T_uid_t, T_uid_t),
f(fchown,	T_filedesc, T_uid_t, T_uid_t),
f(fchmod,	T_filedesc, T_mode_t),
#endif
f(truncate,	T_path, T_size_t),
#ifdef __NR_truncate64
f(truncate64,	T_path, T_u64_t),
#endif
f(ftruncate,	T_filedesc, T_size_t),
#ifdef __NR_ftruncate64
f(ftruncate64,	T_filedesc, T_u64_t),
#endif
f(setxattr,	T_path, T_string, T_array(char, 3, 2046), T_size_t, T_int),
f(lsetxattr,	T_path_parent, T_string, T_array(char, 3, 2046), T_size_t, T_int),
f(fsetxattr,	T_filedesc, T_string, T_array(char, 3, 2046), T_size_t, T_int),
f(getxattr,	T_path, T_string, T_any_ptr, T_size_t),
f(lgetxattr,	T_path_parent, T_string, T_any_ptr, T_size_t),
f(fgetxattr,	T_filedesc, T_string, T_any_ptr, T_size_t),
f(listxattr,	T_path, T_any_ptr, T_size_t),
f(llistxattr,	T_path_parent, T_any_ptr, T_size_t),
f(flistxattr,	T_filedesc, T_any_ptr, T_size_t),
f(removexattr,	T_path, T_string),
f(lremovexattr,	T_path_parent, T_string),
f(fremovexattr,	T_filedesc, T_string),

/*
 * Network stuff
 */
#ifdef __NR_socketcall
f(socketcall,	T_int, T_any_ptr),
#else
f(socket,	T_int, T_int, T_int),
f(connect,	T_filedesc, T_opaque_t(2), T_socklen_t),
f(accept,	T_filedesc, T_any_ptr, T_pointer(socklen_t)),
f(sendto,	T_filedesc, T_any_ptr, T_size_t, T_int, T_opaque_t(5), T_socklen_t),
f(recvfrom,	T_filedesc, T_any_ptr, T_size_t, T_int, T_any_ptr, T_pointer(socklen_t)),
f(sendmsg,	T_filedesc, T_pointer(struct msghdr), T_int),
f(recvmsg,	T_filedesc, T_pointer(struct msghdr), T_int),
f(shutdown,	T_filedesc, T_int),
f(bind,		T_filedesc, T_opaque_t(2), T_socklen_t),
f(listen,	T_filedesc, T_int),
f(getsockname,	T_filedesc, T_any_ptr, T_pointer(socklen_t)),
f(getpeername,	T_filedesc, T_any_ptr, T_pointer(socklen_t)),
f(socketpair,	T_int, T_int, T_int, T_any_ptr),
f(setsockopt,	T_filedesc, T_int, T_int, T_opaque_t(4), T_socklen_t),
f(getsockopt,	T_filedesc, T_int, T_int, T_opaque_t(4), T_socklen_t),
#endif

/*
 * SysV IPC
 */
#ifdef __NR_ipc
f(ipc,		T_int, T_long, T_long, T_long, T_long, T_long),
#else
f(shmget,	T_int, T_int, T_int),
f(shmat,	T_int, T_any_ptr, T_int, T_pointer(unsigned long)),
f(shmdt,	T_any_ptr),
f(shmctl,	T_int, T_int, T_int, T_any_ptr),
f(semget,	T_int, T_int, T_int),
f(semop,	T_int, T_array(struct sembuf, 2, SEMOPM), T_int),
f(semtimedop,	T_int, T_array(struct sembuf, 2, SEMOPM), T_int, T_pointer(struct timespec)),
f(semctl,	T_int, T_int, T_pointer(struct shmid_ds)),
f(msgget,	T_int, T_int),
f(msgsnd,	T_int, T_pointer(struct msgbuf), T_size_t, T_int),
f(msgrcv,	T_int, T_pointer(struct msgbuf), T_size_t, T_long, T_int),
f(msgctl,	T_int, T_int, T_pointer(struct msqid_ds)),
#endif

/*
 * ioctl.
 * The third ioctl argument is frobbed in audit_get_ioctlargs below
 */
f(ioctl,	T_filedesc, T_uint, T_any_ptr),

};

/*
 * Initialize system call tables
 */
void
audit_init_syscall_table(void)
{
	unsigned int m, n;

	/* Loop over list of syscalls and fill in the number of
	 * arguments */
	for (m = 0; m < MAX_SYSCALLS ; m++) {
		struct sysent *entry = &linux_sysent[m];

		for (n = 0; n < AUDIT_MAXARGS; n++) {
			if (entry->sy_args[n].sa_type == AUDIT_ARG_END)
				break;
		}
		entry->sy_narg = n;
	}
}

/*
 * Get syscall information
 */
struct sysent *
audit_get_syscall_entry(int code)
{

	if (code < 0 || code >= MAX_SYSCALLS )
		return NULL;

	return &linux_sysent[code];
}


#define sc(N, args...)	{ (N), { args } }

/*
 * Architecture specific, but used by more than one architecture
 *
 * First, handle sys_socketcall
 *
 * The system call is
 *	socketcall(int cmd, long *args)
 * where the number of arguments pointed to by args is implicit.
 * To make tracing socketcalls possible, we break up the argument
 * array (at least for those calls we understand).
 * The array below defines the arguments for all these socketcalls.
 * Note that the first argument (i.e. #cmd) remains unchanged; the
 * contents of the #args array are pasted after that.
 */

struct sysent	socketcall_sysent[MAX_SOCKETCALL] = {
[SYS_SOCKET]		= sc(3, T_int, T_int, T_int),
[SYS_BIND]		= sc(3, T_filedesc, T_opaque_t(2), T_socklen_t),
[SYS_CONNECT]		= sc(3, T_filedesc, T_opaque_t(2), T_socklen_t),
[SYS_LISTEN]		= sc(2, T_filedesc, T_int),
[SYS_ACCEPT]		= sc(3, T_filedesc, T_any_ptr, T_pointer(socklen_t)),
[SYS_GETSOCKNAME]	= sc(3, T_filedesc, T_any_ptr, T_pointer(socklen_t)),
[SYS_GETPEERNAME]	= sc(3, T_filedesc, T_any_ptr, T_pointer(socklen_t)),
[SYS_SOCKETPAIR]	= sc(4, T_int, T_int, T_int, T_any_ptr),
[SYS_SHUTDOWN]		= sc(2, T_filedesc, T_int),
[SYS_SETSOCKOPT]	= sc(5, T_filedesc, T_int, T_int,
				T_opaque_t(4), T_socklen_t),
[SYS_GETSOCKOPT]	= sc(5, T_filedesc, T_int, T_int,
				T_any_ptr, T_pointer(socklen_t)),
[SYS_SEND]		= sc(4, T_filedesc, T_any_ptr, T_size_t, T_int),
[SYS_RECV]		= sc(4, T_filedesc, T_any_ptr, T_size_t, T_int),
[SYS_SENDTO]		= sc(4, T_filedesc, T_any_ptr, T_size_t, T_int,
				T_opaque_t(5), T_socklen_t),
[SYS_RECVFROM]		= sc(4, T_filedesc, T_any_ptr, T_size_t, T_int,
				T_any_ptr, T_pointer(socklen_t)),
[SYS_SENDMSG]		= sc(3, T_filedesc, T_pointer(struct msghdr), T_int),
[SYS_RECVMSG]		= sc(3, T_filedesc, T_pointer(struct msghdr), T_int),
};

int
audit_get_socketargs(struct aud_syscall_data *sc)
{
	unsigned int	minor = sc->raw_args[0];
	struct sysent	*entry;
	unsigned int	n, argsize;
	long		args[AUDIT_MAXARGS];

	sc->minor = minor;
	if (minor >= MAX_SOCKETCALL) {
		/* XXX mark as invalid? */
		return 0;
	}

	entry = &socketcall_sysent[minor];

	if (audit_syscall_word_size(sc) == sizeof(long) * 8) {
		/*  32bit call on 32bit platform, or 64bit call on 64bit platform */
		argsize = entry->sy_narg * sizeof(long);
		if (copy_from_user(args, (void *)(unsigned long) sc->raw_args[1], argsize))
			return 0;

		/* Can't memcpy here because raw_args is 64bit and args is a long,
		 * which is not necessarily the same thing */
		for (n = 0; n < entry->sy_narg; n++)
			sc->raw_args[n] = args[n];
	} else {
		/* assume 32bit call on 64bit platform */
		argsize = entry->sy_narg * sizeof(u_int32_t);
		if (copy_from_user(args, (void *)(unsigned long) sc->raw_args[1], argsize))
			return 0;

		/* Can't memcpy here because raw_args is 64bit and args is a long,
		 * which is not necessarily the same thing */
		for (n = 0; n < entry->sy_narg; n++)
			sc->raw_args[n] = ((u_int32_t *)args)[n];
	}

	sc->entry = entry;

	return 0;
}


/*
 * socketcall was almost too easy.
 * Here comes sys_ipc.
 */

#ifndef __NR_ipc
#include <asm-i386/ipc.h>
#endif

static int	ipc_reorder[MAX_IPCCALL+1][AUDIT_MAXARGS] = {
[SEMOP]		= { 1, 4, 2 },
[SEMTIMEDOP]	= { 1, 4, 2, 5 },
[SEMGET]	= { 1, 2, 3 },
[SEMCTL]	= { 1, 2, 3, 4 },
[MSGSND]	= { 1, 4, 2, 3 },
[MSGRCV]	= { 1, 4, 2, 5, 3 },
[MSGGET]	= { 1, 2 },
[MSGCTL]	= { 1, 2, 4 },
[SHMAT]		= { 1, 4, 2, 3 },
[SHMDT]		= { 4 },
[SHMGET]	= { 1, 2, 3 },
[SHMCTL]	= { 1, 2, 4 },
};

struct sysent	ipccall_sysent[MAX_IPCCALL+1] = {
[SEMOP]			= sc(3, T_int, T_array(struct sembuf, 2, SEMOPM), T_int),
[SEMTIMEDOP]		= sc(4, T_int, T_array(struct sembuf, 2, SEMOPM), T_int, T_pointer(struct timespec)),
[SEMGET]		= sc(3, T_int, T_int, T_int),
[SEMCTL]		= sc(4, T_int, T_int, T_int, T_any_ptr),
[MSGSND]		= sc(4, T_int, T_pointer(struct msgbuf), T_size_t, T_int),
[MSGRCV]		= sc(5, T_int, T_pointer(struct msgbuf), T_size_t, T_long, T_int),
[MSGGET]		= sc(2, T_int, T_int),
[MSGCTL]		= sc(3, T_int, T_int, T_pointer(struct msqid_ds)),
[SHMGET]		= sc(3, T_int, T_int, T_int),
[SHMAT]			= sc(4, T_int, T_any_ptr, T_int, T_pointer(unsigned long)),
[SHMDT]			= sc(1, T_any_ptr),
[SHMCTL]		= sc(3, T_int, T_int, T_pointer(struct shmid_ds)),
};

int
audit_get_ipcargs(struct aud_syscall_data *sc)
{
	unsigned int	minor = sc->raw_args[0];
	struct sysent	*entry;
	unsigned int	n, nargs = 0, version;
	int		*reorder;
	long		args[AUDIT_MAXARGS];

	/* i386 sys_ipc has a syscall API version encoded in
	   the top 16 bits */
	version = minor >> 16;
	minor &= 0xffff;

	sc->minor = minor;
	if (minor > MAX_IPCCALL) {
		/* XXX mark as invalid? */
		return 0;
	}

	/* Very special cases */
	if (minor == MSGRCV && version == 0) {
		struct ipc_kludge tmp, *arg;

		/* This is _so_ broken */
		arg = (struct ipc_kludge *)(unsigned long) sc->raw_args[4];
		if (copy_from_user(&tmp, arg, sizeof(tmp)))
			memset(&tmp, 0, sizeof(tmp));
		sc->raw_args[4] = (unsigned long) tmp.msgp;
		sc->raw_args[5] = tmp.msgtyp;
	} else if (minor == SHMAT && version == 1) {
		/* iBCS2 emulator, called from kernel space only;
		 * the difference between ver 0 and ver 1
		 * is that in ver 1, the 3rd argument is in
		 * kernel space. But as that is an output only
		 * argument, we're not concerned. */
	}

	/* Reorder sys_ipc arguments to match system call
	 * signature. We should really do this sort of
	 * crap in user space.
	 */
	reorder = ipc_reorder[minor];
	while (nargs < AUDIT_MAXARGS && reorder[nargs]) {
		args[nargs] = sc->raw_args[reorder[nargs]];
		nargs++;
	}

	for (n = 0; n < nargs; n++)
		sc->raw_args[n] = args[n];

	entry = &ipccall_sysent[minor];
	BUG_ON(entry->sy_narg != nargs);

	sc->entry = entry;
	return 0;
}

/*
 * Get ioctl arguments (well, basically we want the third argument's size,
 * and whether it's a read or write ioctl
 */
static struct ioctl_info {
	int		cmd;
	size_t		size;
} ioctl_info[] = {
      { SIOCSIFNAME,		sizeof(struct ifreq),	},

      { SIOCSIFFLAGS,		sizeof(struct ifreq),	},
      { SIOCSIFADDR,		sizeof(struct ifreq),	},
      { SIOCSIFDSTADDR,		sizeof(struct ifreq),	},
      { SIOCSIFBRDADDR,		sizeof(struct ifreq),	},
      { SIOCSIFNETMASK,		sizeof(struct ifreq),	},
      { SIOCSIFMETRIC,		sizeof(struct ifreq),	},
      { SIOCSIFMTU,		sizeof(struct ifreq),	},
      { SIOCADDMULTI,		sizeof(struct ifreq),	},
      { SIOCDELMULTI,		sizeof(struct ifreq),	},
      {	SIOCADDRT,		sizeof(struct rtentry),	},
      {	SIOCDELRT,		sizeof(struct rtentry),	},

      { SIOCSIFHWADDR,		sizeof(struct ifreq),	},
#ifdef SIOCSIFHWBROADCAST
      { SIOCSIFHWBROADCAST,	sizeof(struct ifreq),	},
#endif
      { SIOCSIFMAP,		sizeof(struct ifreq),	},
      { SIOCSIFMEM,		sizeof(struct ifreq),	},
      { SIOCSIFENCAP,		sizeof(struct ifreq),	},
      { SIOCSIFSLAVE,		sizeof(struct ifreq),	},
      { SIOCSIFPFLAGS,		sizeof(struct ifreq),	},
      { SIOCDIFADDR,		sizeof(struct ifreq),	},
      { SIOCSIFBR,		3 * sizeof(long),	},
      { SIOCGIFBR,		3 * sizeof(long),	},

      { SIOCSARP,		sizeof(struct arpreq)	},
      { SIOCDARP,		sizeof(struct arpreq)	},

      /* SIOCDRARP, SIOCSRARP obsolete */
#ifdef CONFIG_DLCI
      { SIOCADDDLCI,		sizeof(struct dlci_add)	},
      { SIOCDELDLCI,		sizeof(struct dlci_add)	},
#endif

      /* SIOCSIFLINK obsolete? */
      { SIOCSIFLINK,		0,			},
      { SIOCSIFTXQLEN,		sizeof(struct ifreq),	},
      { SIOCBONDENSLAVE,	sizeof(struct ifreq),	},
      { SIOCBONDRELEASE,	sizeof(struct ifreq),	},
      { SIOCBONDSETHWADDR,	sizeof(struct ifreq),	},
      { SIOCBONDCHANGEACTIVE,	sizeof(struct ifreq),	},
      { SIOCETHTOOL,		sizeof(struct ifreq),	},
      { SIOCSMIIREG,		sizeof(struct ifreq),	},

      { -1, 0 },
};

int
audit_get_ioctlargs(struct aud_syscall_data *sc)
{
	struct ioctl_info	*iop;
	int			cmd;
	void			*arg, *p;
	struct sysarg_data	*tgt;
	size_t			len = 0;

	cmd = sc->raw_args[1];
	arg = (void *) (unsigned long) sc->raw_args[2];
	tgt = &sc->args[2];

	if (arg == NULL)
		return 0;

	for (iop = ioctl_info; iop->cmd >= 0; iop++) {
		if (iop->cmd == cmd) {
			len = iop->size;
			break;
		}
	}

	if (len == 0 && (_IOC_DIR(cmd) & _IOC_WRITE)) 
		len = _IOC_SIZE(cmd);

	if (len != 0 && len < MAX_IOCTL_COPY) {
		if ((p = kmalloc(len, GFP_USER)) == NULL)
			return -ENOBUFS;
		if (copy_from_user(p, arg, len)) {
			kfree(p);
			return -EFAULT;
		}
		tgt->at_type = AUDIT_ARG_POINTER;
		tgt->at_data.ptr = p;
		tgt->at_data.len = len;
	}
	return 0;
}
