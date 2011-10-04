/*
 * control.c
 *
 * Linux Audit Subsystem
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
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

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sys.h>
#include <linux/miscdevice.h>
#include <linux/personality.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/unistd.h>
#include <linux/audit.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/ipc.h>

#include "audit-private.h"

#define AUDIT_VERSION		"0.1"
#define AUDIT_MINOR		224

static int			audit_id = 0;
static struct aud_policy	audit_policy[__AUD_MAX_POLICY];

static DECLARE_RWSEM(audit_lock);

/* These are accessible through sysctl */
int				audit_debug = 0;
int				audit_all_processes = 0;
unsigned int			audit_max_messages = 1024;
int				audit_allow_suspend = 1;
int				audit_paranoia = 0;
#ifdef __ia64__
int				audit_disable_32bit = 0;
#endif

static int	__audit_attach(struct task_struct *, int, struct aud_process *);
static void	audit_attach_all(void);
static void	audit_detach_all(void);

static struct file_operations audit_fops = {
	read:    &auditf_read,
	write:   &auditf_write,
	ioctl:   &auditf_ioctl,
	release: &auditf_release,
	open:    &auditf_open,
	poll:    &auditf_poll
};

static struct miscdevice audit_dev = {
	AUDIT_MINOR,
	"audit",
	&audit_fops
};


#ifdef CONFIG_AUDIT_MODULE
#ifdef __ia64__
static int	__audit_intercept(struct pt_regs *, unsigned long *);
#else
static int	__audit_intercept(struct pt_regs *);
#endif
static void	__audit_result(struct pt_regs *);
static void	__audit_fork(struct task_struct *, struct task_struct *);
static void	__audit_exit(struct task_struct *, long code);
static void	__audit_netlink_msg(struct sk_buff *, int);
static int	__audit_control(const int ioctl, const int result);

#define audit_intercept	__audit_intercept
#define	audit_result	__audit_result
#define audit_exit	__audit_exit
#define audit_fork	__audit_fork
#define audit_netlink_msg __audit_netlink_msg

#define audit_control __audit_control

static struct audit_hooks audit_hooks = {
	__audit_intercept,
	__audit_result,
	__audit_fork,
	__audit_exit,
	__audit_netlink_msg,
};
#endif


static int __init
init_audit(void)
{
	memset(audit_policy, 0, sizeof(audit_policy));

	/* We cannot simply use the NR_syscalls define in linux/audit.h
	 * because user space may use a different header with different
	 * values. */
	if (__AUD_POLICY_LAST_SYSCALL <= NR_syscalls) {
		printk(KERN_ERR "not enough syscall slots reserved, "
				"please change __AUD_POLICY_LAST_SYSCALL "
				"and recompile.\n");
		return -EINVAL;
	}

	audit_init_syscall_table();

	if (misc_register(&audit_dev) < 0) {
		printk(KERN_INFO "audit: unable to register device\n");
		return -EIO;
	}

	if (audit_sysctl_register() < 0)
		goto fail_unregister;

	if (audit_register_ioctl_converters() < 0)
		goto fail_unregister;

#ifdef CONFIG_AUDIT_MODULE
	if (audit_register(&audit_hooks) < 0)
		goto fail_unregister;
#endif

	printk(KERN_INFO "audit subsystem ver %s initialized\n",
		AUDIT_VERSION);

	return (0);

fail_unregister:
	audit_unregister_ioctl_converters();
	audit_sysctl_unregister();
	misc_deregister(&audit_dev);
	return -EIO;
}

static void __exit 
exit_audit(void)
{
	/* Detach all audited processes */
	audit_detach_all();

#ifdef CONFIG_AUDIT_MODULE
	audit_unregister();
#endif
	audit_unregister_ioctl_converters();
	misc_deregister(&audit_dev);
	audit_sysctl_unregister();
	audit_policy_clear();
	audit_filter_clear();
}

int
auditf_open(struct inode *inode, struct file *file)
{
	struct aud_context *ctx;
	int error = 0;

	DPRINTF("opened by pid %d\n", current->pid);
	if ((ctx = kmalloc(sizeof(*ctx), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "audit: Failed to allocate kernel memory.\n");
		return -ENOBUFS;
	}

	memset(ctx, 0, sizeof(*ctx));
	file->private_data = ctx;

	MOD_INC_USE_COUNT;
	return (error);
}

int
auditf_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct aud_context *ctx = (struct aud_context *) file->private_data;
	int error = 0;
	int ctlerror = 0;

	DPRINTF("ctx=%p, cmd=0x%x\n", ctx, cmd);
	if (!capable(CAP_SYS_ADMIN)) {
		error=-EPERM;

		down_read(&audit_lock);
		ctlerror = audit_control(cmd, error);
		up_read(&audit_lock);

		goto err;
	}

	switch (cmd) {
	case AUIOCIAMAUDITD:
		down_write(&audit_lock);
		error = audit_msg_enable();
		if (error >= 0) {
			printk(KERN_DEBUG
				"Audit daemon registered (process %d)\n",
				current->pid);
			/* Suspend auditing for this process */
			if (current->audit)
				((struct aud_process *) current->audit)->suspended++;
			ctx->reader = 1;
		}
		ctlerror = audit_control(cmd, error);
		if (audit_all_processes)
			audit_attach_all();
		up_write(&audit_lock);
		break;
	case AUIOCATTACH:
		down_write(&audit_lock);
		/* Attach process. If we're the audit daemon,
		 * suspend auditing for us. */
		error = audit_attach(ctx->reader);
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCDETACH:
		down_write(&audit_lock);
		error = audit_detach();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCSUSPEND:
		down_write(&audit_lock);
		error = audit_suspend();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCRESUME:
		down_write(&audit_lock);
		error = audit_resume();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCCLRPOLICY:
		down_write(&audit_lock);
		error = audit_policy_clear();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCCLRFILTER:
		down_write(&audit_lock);
		error = audit_filter_clear();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCSETFILTER:
		down_write(&audit_lock);
		error = audit_filter_add((void *) arg);
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCSETPOLICY:
		down_write(&audit_lock);
		error = audit_policy_set((void *) arg);
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCSETAUDITID:
		down_write(&audit_lock);
		error = audit_setauditid();
		ctlerror = audit_control(cmd, error);
		up_write(&audit_lock);
		break;
	case AUIOCLOGIN:
		down_read(&audit_lock);
		error = audit_login((void *) arg);
		ctlerror = audit_control(cmd, error);
		up_read(&audit_lock);
		break;
	case AUIOCUSERMESSAGE:
		down_read(&audit_lock);
		error = audit_user_message((void *) arg);
		ctlerror = audit_control(cmd, error);
		up_read(&audit_lock);
		break;

	default:
		error = -EINVAL;
		break;
	}

err:
	if (ctlerror < 0) {
		printk("Error auditing control event %d: %d\n", cmd, ctlerror);
	}

	DPRINTF("done, result=%d\n", error);
	return (error);
}

unsigned int
auditf_poll(struct file *file, struct poll_table_struct *wait)
{
	if (audit_msg_poll(file, wait))
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 * Compute statistics
 */
ssize_t
auditf_read(struct file *filp, char *buf, size_t count, loff_t *off)
{
	struct aud_context *ctx = (struct aud_context *) filp->private_data;
	struct aud_msg_head *msgh;
	size_t len, max_len, copied = 0;
	int block, nmsgs = 0;

	DPRINTF("called.\n");
	if (!ctx->reader)
		return -EPERM;

	/* Get messages from the message queue.
	 * The first time around, extract the first message, no
	 * matter its size.
	 * For subsequent messages, make sure it fits into the buffer.
	 */
	block = !(filp->f_flags & O_NONBLOCK);
	max_len = 0;

	while (copied < count) {
		msgh = audit_msg_get(block, max_len);
		if (IS_ERR(msgh)) {
			if (copied)
				break;
			return PTR_ERR(msgh);
		}

		if ((len = msgh->body.msg_size) > count - copied) {
			printk(KERN_NOTICE "auditf_read: truncated audit message (%u > %u; max_len=%u)\n",
					len, count - copied, max_len);
			len = count - copied;
		}

		if (audit_debug > 1) {
			DPRINTF("copying msg %d type %d size %d\n",
				msgh->body.msg_seqnr, msgh->body.msg_type, len);
		}
		if (copy_to_user(buf + copied, &msgh->body, len)) {
			printk(KERN_ERR "Dropped audit message when "
					"copying to audit daemon\n");
			audit_msg_release(msgh);
			return -EFAULT;
		}
		audit_msg_release(msgh);
		copied += len;
		nmsgs++;

		max_len = count - copied;
		block = 0;
	}

	DPRINTF("copied %d messages, %u bytes total\n", nmsgs, copied);
	return copied;
}

ssize_t
auditf_write(struct file *filp, const char *buf, size_t count, loff_t *off)
{
	return (-ENOTSUPP);
}

int
auditf_release(struct inode *inode, struct file *filp)
{
	struct aud_context *ctx = filp->private_data;

	DPRINTF("called.\n");

	if (ctx->reader) {
		struct aud_msg_head	*msgh;

		DPRINTF("Audit daemon closed audit file; auditing disabled\n");
		audit_msg_disable();

		/* Drop all messages already queued */
		while (1) {
			msgh = audit_msg_get(0, 0);
			if (IS_ERR(msgh))
				break;
			audit_msg_release(msgh);
		}

		/* When we announced being auditd, our
		 * suspend count was bumped */
		audit_resume();
	}

	filp->private_data = NULL;
	kfree(ctx);

	MOD_DEC_USE_COUNT;
	return (0);
}

/*
 * Process intercepted system call and result
 */
static void
__audit_syscall_return(struct aud_process *pinfo, int result)
{
	struct aud_event_data	ev;
	struct aud_syscall_data *sc = &pinfo->syscall;
	int			action, error;

	/* System call ignored, or not supported */
	if (sc->entry == NULL)
		return;

	/* Work-around setfs[ug]id weirdness - these syscalls
	 * always return the previous uid/gid instead of
	 * an error code. */
	switch (sc->major) {
	case __NR_setfsuid:
#ifdef __NR_setfsuid32
	case __NR_setfsuid32:
#endif
		if (current->fsuid != (uid_t) sc->raw_args[0])
			result = -EPERM;
		break;
	case __NR_setfsgid:
#ifdef __NR_setfsgid32
	case __NR_setfsgid32:
#endif
		if (current->fsgid != (gid_t) sc->raw_args[0])
			result = -EPERM;
		break;
	default: ;
	}

	sc->result = result;

	memset(&ev, 0, sizeof(ev));
	ev.syscall = sc;
	action = audit_policy_check(sc->major, &ev);

	if ((action & AUDIT_LOG) && audit_message_enabled) {
		sc->flags = action;
		error = audit_msg_syscall(pinfo, ev.name, sc);
		/* ENODEV means the audit daemon has gone away.
		 * continue as if we weren't auditing */
		if (error < 0 && error != -ENODEV) {
			printk("audit: error %d when processing syscall %d\n",
					error, sc->major);
		}
	}

	/* If we copied any system call arguments to user
	 * space, release them now.
	 */
	audit_release_arguments(pinfo);

	/* For now, we always invalidate the fileset's cached
	 * dentry pointers.
	 * We could optimize this (e.g. open(2) without O_CREAT
	 * does not change the file system)
	 */
	audit_fileset_unlock(1);

	memset(sc, 0, sizeof(*sc));
}


/*
 * This function is executed in the context of the parent
 * process, with the child process still sleeping
 */
void
audit_fork(struct task_struct *parent, struct task_struct *child)
{
	struct aud_process *parent_info, *pinfo;

	DPRINTF("called.\n");

	/* pointer and flags copied from parent */
	child->audit = NULL;
	child->ptrace &= ~PT_AUDITED;

	if ((parent_info = parent->audit) == NULL)
		return;

	if (__audit_attach(child, 0, parent_info) != 0) {
		printk(KERN_ERR "audit: failed to enable auditing for child process!\n");
		return;
	}
	pinfo = child->audit;

	pinfo->audit_id = parent_info->audit_id;
	pinfo->audit_uid = parent_info->audit_uid;
}

void
audit_exit(struct task_struct *p, long code)
{
	struct aud_process *pinfo;
	int		action;

	/* Notify auditd that we're gone */
	if ((pinfo = p->audit) != NULL) {
		DPRINTF("process exiting, code=%ld\n", code);
		if (!pinfo->suspended) {
			struct aud_event_data	ev;

			__audit_syscall_return(pinfo, 0);

			memset(&ev, 0, sizeof(ev));
			ev.exit_status = code;
			action = audit_policy_check(AUD_POLICY_EXIT, &ev);
			if (action & AUDIT_LOG)
				audit_msg_exit(pinfo, ev.name, code);
		}
		audit_detach();
	}
}

/*
 * Intercept system call
 */
#ifdef __ia64__
int
audit_intercept(struct pt_regs *regs, unsigned long *bsp)
#else
int
audit_intercept(struct pt_regs *regs)
#endif
{
	struct aud_syscall_data	*sc;
	struct aud_process	*pinfo;
	int			error = -EL3RST; /* initialized below */

	/* Oops, process not attached? */
	if ((pinfo = current->audit) == NULL) {
		printk(KERN_NOTICE "audit_intercept: current->audit == NULL, weird\n");
		return 0;
	}

	/* Check if we have system call data we haven't processed
	 * yet, in case there was no call to audit_result.
	 * This happens e.g. for execve(). */
	__audit_syscall_return(pinfo, 0);

	if (pinfo->suspended || !audit_message_enabled)
		return 0;

	sc = &pinfo->syscall;
	sc->regs = regs;
#ifdef __ia64__
	sc->bsp = bsp;
#endif
	sc->personality = personality(current->personality);
	if ((error = audit_get_args(regs, sc)) < 0)
		goto failed;

	/* Don't dig any deeper if we're not interested in this call */
	if (!sc->entry)
		return 0;

	/* Raw, unoptimized -
	 *
	 * We need to protect against two-man con games here,
	 * where one thread enters audit_intercept with say
	 * a pathname of "/ftc/bar", which we don't audit, and
	 * a second thread modifies that to "/etc/bar" before
	 * we actually call the real syscall.
	 *
	 * This is where the "auditing by system call intercept"
	 * concept breaks down quite badly; but that is the price
	 * you pay for an unintrusive patch.
	 */
	if (audit_paranoia) {
		switch (sc->major) {
#ifdef __NR_ipc
		case __NR_ipc:
			if (sc->minor != SHMAT && sc->minor != SHMDT)
				goto lock_args;
			/* fallthru */
#else
		case __NR_shmat:
		case __NR_shmdt:
#endif
		case __NR_mmap:
		case __NR_munmap:
		case __NR_mremap:
		case __NR_mprotect:
		case __NR_io_setup:
		case __NR_madvise:
		case __NR_mlock:
		case __NR_mlockall:
		case __NR_munlock:
			/* These calls mess with the process VM.
			 * Make sure no other thread sharing this VM is
			 * doing any audited call at this time. */
			error = audit_lock_arguments(sc, AUD_F_VM_LOCKED_W);
			break;
		case __NR_execve:
			/* Same as above, except we need to preserve
			 * arguments for posterity. */
			error = audit_lock_arguments(sc, AUD_F_VM_LOCKED_W);
			if (error >= 0)
				error = audit_copy_arguments(sc);
			break;
		default:
		lock_args:
			error = audit_lock_arguments(sc, AUD_F_VM_LOCKED_R);
			break;
		}
	} else {
		/* For some system calls, we need to copy one or more arguments
		 * before the call itself:
		 * execve	Never returns, and by the time we get around to
		 *		assembling the audit message, the process image
		 *		is gone.
		 * unlink	Resolve pathnames before file is gone
		 * close        Resolve pathname for fd before it is closed
		 * rename	First pathname needs to be resolved before
		 *		the call; afterwards it's gone already.
		 * chroot	Pathname must be interpreted relative to
		 *		original fs->root.
		 * sendfile     changes the *offset argument
		 */
		switch (sc->major) {
			void *p;
		case __NR_execve:
		case __NR_unlink:
		case __NR_chroot:
		case __NR_close:
			error = audit_copy_arguments(sc);
			break;
		case __NR_rename:
			p = audit_get_argument(sc, 0);
			if (IS_ERR(p)) error = PTR_ERR(p);
			break;
		case __NR_sendfile:
			p = audit_get_argument(sc, 2);
			if (IS_ERR(p)) error = PTR_ERR(p);
			break;
		}
	}

	if (error >= 0)
		return 0;

failed:	/* An error occurred while copying arguments from user
	 * space. This could either be a simple address fault
	 * (which is alright in some cases, and should just elicit
	 * an error), or it's an internal problem of the audit
	 * subsystem.
	 */
	/* For now, we choose to kill the task. If audit is a module,
	 * we return and let the stub handler do this (because we
	 * need to release the stub lock first)
	 */
#ifndef CONFIG_AUDIT_MODULE
	audit_kill_process(error);
	/* NOTREACHED */
#else
	return error;
#endif
}

/*
 * Intercept system call result
 */
void
audit_result(struct pt_regs *regs)
{
	struct aud_process *pinfo;

	if (!audit_message_enabled)
		return;

	if ((pinfo = current->audit) == NULL)
		return;

	/* report return value to audit daemon */
	__audit_syscall_return(pinfo, audit_get_result(regs));
}

/*
 * Netlink message - probably network configuration change
 */
void
audit_netlink_msg(struct sk_buff *skb, int res)
{
	struct nlmsghdr		*nlh;
	struct aud_event_data	ev;
	struct aud_process	*pinfo;
	int			action;

	DPRINTF("called\n");

	if (!audit_message_enabled)
		return;

	/* Ignore netlink replies for now */
	nlh = (struct nlmsghdr *) skb->data;
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST))
		return;

	if (!(pinfo = current->audit) || pinfo->suspended)
		return;

	memset(&ev, 0, sizeof(ev));
	ev.netconf = skb;

	action = audit_policy_check(AUD_POLICY_NETLINK, &ev);

	if (action & AUDIT_LOG)
		audit_msg_netlink(pinfo, ev.name, skb, res);
}

/*
 * Clear the audit policy table.
 * We hold the audit_lock when we get here.
 */
int
audit_policy_clear(void)
{
	struct aud_policy *policy = audit_policy;
	int		i;

	for (i = 0; i < __AUD_MAX_POLICY; i++, policy++) {
		audit_filter_put(policy->filter);
		policy->action = AUDIT_IGNORE;
		policy->filter = NULL;
	}
	return 0;
}

/*
 * Set an audit policy
 * We hold the audit_lock when we get here.
 */
int
audit_policy_set(void *arg)
{
	struct aud_policy	*policy;
	struct audit_policy	pol;
	struct aud_filter	*f = NULL;

	if (!arg)
		return -EINVAL;

	if (copy_from_user(&pol, arg, sizeof(pol)) != 0)
		return -EFAULT;

	DPRINTF("code %u, action %d, filter %d\n",
		       	pol.code, pol.action, pol.filter);
	if (pol.code >= __AUD_MAX_POLICY)
		return -EINVAL;

	policy = &audit_policy[pol.code];
	if (pol.filter > 0 && !(f = audit_filter_get(pol.filter)))
		return -EINVAL;

	audit_filter_put(policy->filter);
	policy->action = pol.action;
	policy->filter = f;
	return 0;
}

/*
 * Check whether we ignore this system call.
 * Called to find out whether we should bother with
 * decoding arguments etc.
 */
int
audit_policy_ignore(int code)
{
	struct aud_policy *policy;
	int		result = 1;

	if (0 <= code && code < __AUD_MAX_POLICY) {
		down_read(&audit_lock);

		policy = &audit_policy[code];
		if (policy->filter
		 || policy->action != AUDIT_IGNORE)
			result = 0;

		up_read(&audit_lock);
	}

	return result;
}

/*
 * Check policy
 */
static int
__audit_policy_check(int code, struct aud_event_data *ev)
{
	struct aud_policy *policy;
	int		result = AUDIT_IGNORE;

	if (0 <= code && code < __AUD_MAX_POLICY) {
		policy = &audit_policy[code];
		if (policy->filter)
			result = audit_filter_eval(policy->filter, ev);
		else
			result = policy->action;
	}

	return result;
}

int
audit_policy_check(int code, struct aud_event_data *ev)
{
	int	result;

	down_read(&audit_lock);
	result = __audit_policy_check(code, ev);
	up_read(&audit_lock);

	return result;
}

/*
 * Attach/detach audit context to process
 */
static int
__audit_attach(struct task_struct *task, int suspended, struct aud_process *parent)
{
	struct aud_process *pinfo;
	int		res = 0;

	if ((pinfo = kmalloc(sizeof(*pinfo), GFP_KERNEL)) == NULL)
		return -ENOBUFS;

	task_lock(task);
	if (task->audit) {
		DPRINTF("Cannot attach process %d; auditing already enabled\n", task->pid);
		task_unlock(task);
		kfree(pinfo);
		res = -EBUSY;
	} else {
		DPRINTF("Attaching process %d\n", task->pid);

		memset(pinfo, 0, sizeof(*pinfo));
		pinfo->audit_uid = (uid_t) -1;
		pinfo->suspended = suspended;

		/* turn on syscall intercept */
		task->audit = pinfo;
		task->ptrace |= PT_AUDITED;
		task_unlock(task);

		if (parent == NULL)
			audit_init_vm(pinfo);
		else
			audit_copy_vm(pinfo, parent);
	}
	return res;
}

int
audit_attach(int suspended)
{
	/* Don't allow attach if auditd is not there
	 *
	 * XXX: For more robustness, shouldn't we allow the attach to
	 * succeed even if the daemon isn't running? This may happen
	 * if it was restarted due to a crash.
	 */
	if (!audit_message_enabled)
		return -ENODEV;

	return __audit_attach(current, suspended, NULL);
}

static int
__audit_detach(task_t *task)
{
	struct aud_process *pinfo;
	int		res = 0;

	task_lock(task);
	if ((pinfo = task->audit) == NULL) {
		res = -EUNATCH;
		task_unlock(task);
	} else {
		/* turn off system call intercept */
		task->ptrace &= ~PT_AUDITED;
		task->audit = NULL;
		task_unlock(task);

		/* Free any memory we may have allocated for
	   	 * argument data, and release VM scratch memory */
		audit_release_arguments(pinfo);
		audit_release_vm(pinfo);
		kfree(pinfo);
	}
	return res;
}

int
audit_detach(void)
{
	DPRINTF("detaching process %d\n", current->pid);
	return __audit_detach(current);
}

/*
 * Attach/detach all processes
 */
void
audit_attach_all(void)
{
	task_t *g, *p;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (p->audit == NULL
		 && p != current
		 && p->mm != NULL
		 /* If audit_all_processes > 1, also attach init */
		 && (p->pid != 1 || audit_all_processes > 1))
			__audit_attach(p, 0, NULL);
	}
	read_unlock(&tasklist_lock);
}

void
audit_detach_all(void)
{
	task_t *g, *p;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		__audit_detach(p);
	}
	read_unlock(&tasklist_lock);
}

/*
 * Suspend system call auditing for this process
 */
int
audit_suspend(void)
{
	struct aud_process *pinfo;

	DPRINTF("process %d suspends auditing\n", current->pid);
	if ((pinfo = current->audit) == NULL)
		return -EUNATCH;
	if (!audit_allow_suspend)
		return -EACCES;
	pinfo->suspended++;
	return 0;
}

/*
 * Resume auditing
 */
int
audit_resume(void)
{
	struct aud_process *pinfo;

	DPRINTF("process %d resumes auditing\n", current->pid);
	if ((pinfo = current->audit) == NULL)
		return -EUNATCH;
	pinfo->suspended--;
	return 0;
}

/*
 * Assign an audit ID
 */
int
audit_setauditid(void)
{
	struct aud_process 	*pinfo;

	if (!(pinfo = current->audit))
		return -EUNATCH;

	if (pinfo->audit_id > 0)
		return -EACCES;

	/* XXX protect against counter wrap-around? */
	pinfo->audit_id = audit_id++;

	DPRINTF("process %d assigned audit id %d\n",
		       	current->pid, pinfo->audit_id);
	return 0;
}

/*
 * Process login message from user land
 */
int
audit_login(void *arg)
{
	struct aud_process	*pinfo;
	struct audit_login	*login;
	struct aud_event_data	ev;
	int			action, err;

	if (!(pinfo = current->audit))
		return -EUNATCH;

	/* Make sure LOGIN works just once */
	if (pinfo->audit_uid != (uid_t) -1)
		return -EACCES;

	if (!(login = kmalloc(sizeof(*login), GFP_KERNEL)))
		return -ENOBUFS;

	err = -EFAULT;
	if (copy_from_user(login, arg, sizeof(*login)))
		goto out;

	err = -EINVAL;
	if (login->uid == (uid_t) -1)
		goto out;

	/* Copy the login uid and keep it */
	pinfo->audit_uid = login->uid;

	/* Notify audit daemon */
	memset(&ev, 0, sizeof(ev));
	strcpy(ev.name, "AUDIT_login");

	action = __audit_policy_check(AUD_POLICY_LOGIN, &ev);
	if (action & AUDIT_LOG)
		err = audit_msg_login(pinfo, ev.name, login);
	else
		err = 0;

out:
	kfree(login);
	return err;
}

/*
 * Pass an audit message generated by user space, and fill in
 * the blanks
 */
int
audit_user_message(void *arg)
{
	struct aud_process	*pinfo;
	struct aud_msg_head	*msgh;
	struct audit_message	user_msg;
	struct aud_event_data	ev;
	int			action;

	/* Beware, may be NULL. We still want to allow
	 * un-audited processes to log audit messages. */
	pinfo = current->audit;

	if (copy_from_user(&user_msg, arg, sizeof(user_msg)))
		return -EFAULT;

	if (user_msg.msg_type < AUDIT_MSG_USERBASE)
		return -EACCES;

	memset(&ev, 0, sizeof(ev));
	strncpy(ev.name, user_msg.msg_evname, sizeof(ev.name)-1);

	action = __audit_policy_check(AUD_POLICY_USERMSG, &ev);
	if (!(action & AUDIT_LOG))
		return 0;

	msgh = audit_msg_new(pinfo, user_msg.msg_type,
				user_msg.msg_evname,
				user_msg.msg_size);
	if (IS_ERR(msgh))
		return PTR_ERR(msgh);

	if (copy_from_user(&msgh->body.msg_data, user_msg.msg_data, user_msg.msg_size)) {
		audit_msg_release(msgh);
		return -EFAULT;
	}

	audit_msg_insert(msgh);
	return 0;
}

/*
 * Process an audit control event
 */
static int
__audit_control(const int ioctl, const int result)
{
	struct aud_event_data	ev;
	int			action;

	memset(&ev, 0, sizeof(ev));

	action = __audit_policy_check(AUD_POLICY_CONTROL, &ev);
	if (action & AUDIT_LOG)
		return audit_msg_control(current->audit, ioctl, result);
	else
		return 0;
}

/*
 * Debugging stuff
 */
#ifdef AUDIT_DEBUG_LOCKS
void
_debug_locks(char *ltype, char *var, int lock)
{
	#define NLOCKS 6
	static spinlock_t _debug_lock = SPIN_LOCK_UNLOCKED;
	static int max_cpu = 0;
	static char locks[NR_CPUS][NLOCKS];
	static char out[NR_CPUS * NLOCKS + NR_CPUS];
	int locknum, cpu, p;

	do_spin_lock(&_debug_lock);

	cpu = current->cpu;
	if (cpu > max_cpu) max_cpu = cpu;

	/* get lock state before update */
	for (p=0; p <= max_cpu; ++p) {
		int l;
		for (l=0; l<NLOCKS; ++l) {
			int c;

			c = locks[p][l];
			if (!c) c='.';

			out[p * (NLOCKS+1) + l] = c;
		}
		out[p*(NLOCKS+1)+NLOCKS] = '|';
	}
	out[max_cpu * NLOCKS + max_cpu - 1] = 0;

	if (!strcmp(var, "&audit_lock")) {
		locknum = 0;
	} else if (!strcmp(var, "&audit_message_lock")) {
		locknum = 1;
	} else if (!strcmp(var, "&hook_lock")) {
		locknum = 2;
	} else if (!strcmp(var, "&tasklist_lock")) {
		locknum = 3;
	} else if (!strcmp(var, "task")) {
		locknum = 4;
	} else {
		locknum = 5;
		printk(KERN_DEBUG "unknown lock %s %s %d\n", ltype, var, lock);
	}

	/* mark changed lock w/ capital letter */
	if (lock) {
		int c='l';
		int cp='L';

		if (locks[cpu][locknum]) printk(KERN_DEBUG "double lock?\n");

		if        (!strcmp(ltype, "read")) {
			c='r'; cp='R';
		} else if (!strcmp(ltype, "write")) {
			c='w'; cp='W';
		} else if (!strcmp(ltype, "spin")) {
			c='s'; cp='S';
		} else if (!strcmp(ltype, "task")) {
			c='t'; cp='T';
		}
		
		locks[cpu][locknum] = c;
		out[cpu * (NLOCKS+1) + locknum] = cp;
	} else {
		if (!locks[cpu][locknum]) printk(KERN_DEBUG "double unlock?\n");
		locks[cpu][locknum] = 0;
		out[cpu * (NLOCKS+1) + locknum] = '-';
	}

	printk(KERN_DEBUG "lock state: [%s]\n", out);
	do_spin_unlock(&_debug_lock);
}
#endif


module_init(init_audit);
module_exit(exit_audit);

MODULE_LICENSE("GPL");
EXPORT_NO_SYMBOLS;
