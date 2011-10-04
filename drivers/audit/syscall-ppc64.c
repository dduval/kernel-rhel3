/*
 * syscall-ppc64.c
 *
 * ppc64 specific system call information for the audit daemon.
 *
 * Copyright (C) 2003 Paul Mackerras, IBM Corp.
 *
 * Derived from syscall-i386.c:
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

#include <linux/audit.h>
#include "audit-private.h"

/* XXX must be the same as in syscall.c, yuck */
#define MAX_SOCKETCALL	20

extern struct sysent	socketcall_sysent[];
extern int		audit_get_socketargs(struct aud_syscall_data *);
extern int		audit_get_ipcargs(struct aud_syscall_data *);
static int		audit_get_socketargs32(struct aud_syscall_data *sc);

int			audit_arch = AUDIT_ARCH_PPC64;


/*
 * Given the set of registers, extract the syscall code and
 * arguments array
 */
int
audit_get_args(struct pt_regs *regs, struct aud_syscall_data *syscall)
{
	struct sysent	*entry;
	int		code;
	int		i;

	syscall->entry = NULL;
	syscall->major = 0;
	syscall->minor = 0;

	if (regs == NULL)		/* "can't happen"  -- paulus. */
		return -EPERM;

	code = regs->gpr[0];

	/* XXX may need to define and use a linux_sysent_32
	   for 32-bit processes  -- paulus. */
	entry = audit_get_syscall_entry(code);

	/* XXX should we try to log calls to invalid syscalls? */
	if (entry == NULL)
		return -ENOSYS;

	if (audit_policy_ignore(code))
		return 0;

	syscall->major = code;
	syscall->entry = entry;

	if (current->thread.flags & PPC_FLAG_32BIT) {
		/* 32-bit process */
		syscall->arch  = AUDIT_ARCH_PPC;
		for (i = 0; i < entry->sy_narg; ++i) {
			unsigned long val = regs->gpr[3+i] & 0xFFFFFFFFUL;

			/* sign-extend if necessary */
			if (entry->sy_args[i].sa_flags & AUD_ARG_SIGNED)
				val = (int) val;
			syscall->raw_args[i] = val;
		}

		/* Special treatment for socket call */
		if (code == __NR_socketcall)
			return audit_get_socketargs32(syscall);

		if (code == __NR_truncate64
		 || code == __NR_ftruncate64) {
			syscall->raw_args[1] =
				(((u_int64_t) regs->gpr[4]) << 32) | regs->gpr[5];
		}
	} else {
		/* 64-bit process */
		syscall->arch  = AUDIT_ARCH_PPC64;
		for (i = 0; i < entry->sy_narg; ++i)
			syscall->raw_args[i] = regs->gpr[3+i];

		/* Special treatment for socket call */
		if (code == __NR_socketcall)
			return audit_get_socketargs(syscall);
	}

	/* Special treatment for IPC syscalls */
	if (code == __NR_ipc)
		return audit_get_ipcargs(syscall);
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

	if (n > 5)
		return -EINVAL;
	if (code == __NR_ioctl || code == __NR_socketcall || code == __NR_ipc)
		return -EINVAL;

	sc->raw_args[n] = newval;
	regs->gpr[3+n] = newval;
	return 0;
}

/*
 * Get the return value of a system call
 */
long int
audit_get_result(struct pt_regs *regs)
{
	return regs->result;
}

int
audit_get_socketargs32(struct aud_syscall_data *sc)
{
	unsigned int	minor = sc->raw_args[0];
	struct sysent	*entry;
	unsigned int	argsize;
	unsigned int	args[AUDIT_MAXARGS];
	int		i;

	sc->minor = minor;
	if (minor >= MAX_SOCKETCALL) {
		/* XXX mark as invalid? */
		return 0;
	}

	entry = &socketcall_sysent[minor];

	argsize = entry->sy_narg * sizeof(unsigned int);
	if (copy_from_user(args, (void *)sc->raw_args[1], argsize))
		return 0;

	for (i = 0; i < entry->sy_narg; ++i) {
		unsigned long val = args[i];

		/* sign-extend if necessary */
		if (entry->sy_args[i].sa_flags & AUD_ARG_SIGNED)
			val = (int) val;
		sc->raw_args[i] = val;
	}

	sc->entry = entry;

	return 0;
}
