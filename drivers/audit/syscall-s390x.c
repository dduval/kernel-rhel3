/*
 * syscall-s390x.c
 *
 * s390x specific system call information for the audit daemon.
 *
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

extern struct sysent	linux_sysent[];
extern int		audit_get_socketargs(struct aud_syscall_data *);
extern int		audit_get_ipcargs(struct aud_syscall_data *);

int			audit_arch = AUDIT_ARCH_S390;

/*
 * Given the set of registers, extract the syscall code and
 * arguments array
 */
int
audit_get_args(struct pt_regs *regs, struct aud_syscall_data *syscall)
{
	struct sysent	*entry;
	unsigned long	code;

	syscall->entry = NULL;
	syscall->major = 0;
	syscall->minor = 0;
	syscall->arch  = AUDIT_ARCH_S390X;

	if (regs == NULL)
		return -EPERM;

	code = regs->gprs[2];

	/* XXX should we try to log calls to invalid syscalls? */
	if (code >= NR_syscalls)
		return -ENOSYS;

	if (audit_policy_ignore(code))
		return 0;

	entry = &linux_sysent[code];
	switch (entry->sy_narg) {
	case 6: syscall->raw_args[5] = regs->gprs[7];
	case 5:	syscall->raw_args[4] = regs->gprs[6];
	case 4: syscall->raw_args[3] = regs->gprs[5];
	case 3: syscall->raw_args[2] = regs->gprs[4];
	case 2: syscall->raw_args[1] = regs->gprs[3];
	case 1: syscall->raw_args[0] = regs->orig_gpr2;
	case 0: break;
	default:
		printk("audit: invalid argument count?!\n");
		BUG();
	}

	syscall->major = code;
	syscall->entry = entry;

	/* Special treatment for special functions */
	if (code == __NR_socketcall)
		return audit_get_socketargs(syscall);
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
	if (n == 0)
		regs->orig_gpr2 = newval;
	else
		regs->gprs[2+n] = newval;
	return 0;
}

/*
 * Get the return value of a system call
 */
long
audit_get_result(struct pt_regs *regs)
{
	return regs->gprs[2];
}
