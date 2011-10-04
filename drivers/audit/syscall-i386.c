/*
 * syscall-i386.c
 *
 * i386 specific system call information for the audit daemon.
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
extern int		audit_get_ioctlargs(struct aud_syscall_data *);

int			audit_arch = AUDIT_ARCH_I386;

/*
 * Given the set of registers, extract the syscall code and
 * arguments array
 */
int
audit_get_args(struct pt_regs *regs, struct aud_syscall_data *sc)
{
	struct sysent	*entry;
	int		code;

	sc->entry = NULL;
	sc->major = 0;
	sc->minor = 0;
	sc->arch  = AUDIT_ARCH_I386;

	if (regs == NULL)
		return -EPERM;

	code = regs->orig_eax;

	/* XXX should we try to log calls to invalid syscalls? */
	if (code < 0 || code >= NR_syscalls)
		return -ENOSYS;

	if (audit_policy_ignore(code))
		return 0;

	entry = &linux_sysent[code];
	switch (entry->sy_narg) {
	case 6:	sc->raw_args[5] = regs->ebp; /* correct? */
	case 5:	sc->raw_args[4] = regs->edi;
	case 4: sc->raw_args[3] = regs->esi;
	case 3: sc->raw_args[2] = regs->edx;
	case 2: sc->raw_args[1] = regs->ecx;
	case 1: sc->raw_args[0] = regs->ebx;
	case 0: break;
	default:
		printk("audit: invalid argument count?!\n");
		BUG();
	}

	sc->major = code;
	sc->entry = entry;

	/* Special treatment for special functions */
	switch (code) {
	case __NR_truncate64:
	case __NR_ftruncate64:
		/* 64bit values are actually passed as two 32bit
		 * registers, lower one first */
		sc->raw_args[1] |= ((u_int64_t) regs->edx) << 32;
		break;
	case __NR_ioctl:
		return audit_get_ioctlargs(sc);
	case __NR_socketcall:
		return audit_get_socketargs(sc);
	case __NR_ipc:
		return audit_get_ipcargs(sc);
	}

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
	switch (n) {
	case 5: regs->ebp = newval; break;
	case 4: regs->edi = newval; break;
	case 3: regs->esi = newval; break;
	case 2: regs->edx = newval; break;
	case 1: regs->ecx = newval; break;
	case 0: regs->ebx = newval; break;
	}

	return 0;
}

/*
 * Get the return value of a system call
 */
long
audit_get_result(struct pt_regs *regs)
{
	return regs->eax;
}
