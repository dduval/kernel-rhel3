/*
 * Audit subsystem module hooks
 *
 * Copyright (C) 2003, SuSE Linux AG
 * Written by okir@suse.de
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sys.h>
//#include <linux/syscall.h>
#include <linux/audit.h>
#include <asm/semaphore.h>

#include <linux/audit.h>

#define DONT_DEBUG_LOCKS
#include "audit-private.h"

unsigned int			audit_intercept_enabled = 0;

#ifdef CONFIG_AUDIT_MODULE

static struct audit_hooks	audit;
static DECLARE_RWSEM(hook_lock);

int
audit_register(struct audit_hooks *hooks)
{
	int	res = 0;

	if (!hooks->intercept
	 || !hooks->result
	 || !hooks->fork
	 || !hooks->exit)
		return -EINVAL;

	down_write(&hook_lock);
	if (audit.intercept) {
		res = -EEXIST;
	} else {
		audit = *hooks;
		audit_intercept_enabled = 1;
	}
	mb();
	up_write(&hook_lock);

	return res;
}

void
audit_unregister(void)
{
	down_write(&hook_lock);
	memset(&audit, 0, sizeof(audit));
	audit_intercept_enabled = 0;
	mb();
	up_write(&hook_lock);
}

#ifdef __ia64__
int
audit_intercept(struct pt_regs *regs, unsigned long *bsp)
#else
int
audit_intercept(struct pt_regs *regs)
#endif
{
	int res = 0;

	down_read(&hook_lock);
	if (audit.intercept)
#ifdef __ia64__
		res = audit.intercept(regs, bsp);
#else
		res = audit.intercept(regs);
#endif
	up_read(&hook_lock);
	if (res < 0)
		audit_kill_process(res);
	return res;
}

void
audit_result(struct pt_regs *regs)
{
	down_read(&hook_lock);
	if (audit.result)
		audit.result(regs);
	up_read(&hook_lock);
}

void
audit_fork(struct task_struct *parent, struct task_struct *child)
{
	down_read(&hook_lock);
	if (audit.fork)
		audit.fork(parent, child);
	up_read(&hook_lock);
}

void
audit_exit(struct task_struct *task, long code)
{
	down_read(&hook_lock);
	if (audit.exit)
		audit.exit(task, code);
	up_read(&hook_lock);
}

void
audit_netlink_msg(struct sk_buff *skb, int res)
{
	down_read(&hook_lock);
	if (audit.netlink_msg)
		audit.netlink_msg(skb, res);
	up_read(&hook_lock);
}

EXPORT_SYMBOL(audit_register);
EXPORT_SYMBOL(audit_unregister);

#endif /* CONFIG_AUDIT_MODULE */
