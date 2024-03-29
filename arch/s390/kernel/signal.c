/*
 *  arch/s390/kernel/signal.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *    Based on Intel version
 * 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))


typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE];
	struct sigcontext sc;
	_sigregs sregs;
	__u8 retcode[S390_SYSCALL_SIZE];
} sigframe;

typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE];
	__u8 retcode[S390_SYSCALL_SIZE];
	struct siginfo info;
	struct ucontext uc;
} rt_sigframe;

asmlinkage int FASTCALL(do_signal(struct pt_regs *regs, sigset_t *oldset));

int copy_siginfo_to_user(siginfo_t *to, siginfo_t *from)
{
	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0)
		return __copy_to_user(to, from, sizeof(siginfo_t));
	else {
		int err;

		/* If you change siginfo_t structure, please be sure
		   this code is fixed accordingly.
		   It should never copy any pad contained in the structure
		   to avoid security leaks, but must copy the generic
		   3 ints plus the relevant union member.  */
		err = __put_user(from->si_signo, &to->si_signo);
		err |= __put_user(from->si_errno, &to->si_errno);
		err |= __put_user((short)from->si_code, &to->si_code);
		/* First 32bits of unions are always present.  */
		err |= __put_user(from->si_pid, &to->si_pid);
		switch (from->si_code >> 16) {
		case __SI_FAULT >> 16:
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		default:
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		/* case __SI_RT: This is not generated by the kernel as of now.  */
		}
		return err;
	}
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(struct pt_regs * regs,int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	regs->gprs[2] = -EINTR;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(struct pt_regs * regs,sigset_t *unewset, size_t sigsetsize)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	regs->gprs[2] = -EINTR;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int
sys_sigaltstack(const stack_t *uss, stack_t *uoss, struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->gprs[15]);
}




static int save_sigregs(struct pt_regs *regs,_sigregs *sregs)
{
	int err;
	s390_fp_regs fpregs;
  
	err = __copy_to_user(&sregs->regs,regs,sizeof(_s390_regs_common));
	if(!err)
	{
		save_fp_regs(&fpregs);
		err=__copy_to_user(&sregs->fpregs,&fpregs,sizeof(fpregs));
	}
	return(err);
	
}

static int restore_sigregs(struct pt_regs *regs,_sigregs *sregs)
{
	int err;
	s390_fp_regs fpregs;
	psw_t saved_psw=regs->psw;
	err=__copy_from_user(regs,&sregs->regs,sizeof(_s390_regs_common));
	if(!err)
	{
		regs->trap = -1;		/* disable syscall checks */
		regs->psw.mask=(saved_psw.mask&~PSW_MASK_DEBUGCHANGE)|
		(regs->psw.mask&PSW_MASK_DEBUGCHANGE);
		regs->psw.addr=(saved_psw.addr&~PSW_ADDR_DEBUGCHANGE)|
		(regs->psw.addr&PSW_ADDR_DEBUGCHANGE);
		err=__copy_from_user(&fpregs,&sregs->fpregs,sizeof(fpregs));
		if(!err)
			restore_fp_regs(&fpregs);
	}
	return(err);
}

asmlinkage long sys_sigreturn(struct pt_regs *regs)
{
	sigframe *frame = (sigframe *)regs->gprs[15];
	sigset_t set;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set.sig, &frame->sc.oldmask, _SIGMASK_COPY_SIZE))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigregs(regs, &frame->sregs))
		goto badframe;

	return regs->gprs[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage long sys_rt_sigreturn(struct pt_regs *regs)
{
	rt_sigframe *frame = (rt_sigframe *)regs->gprs[15];
	sigset_t set;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set.sig, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigregs(regs, &frame->uc.uc_mcontext))
		goto badframe;

	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&frame->uc.uc_stack, NULL, regs->gprs[15]);
	return regs->gprs[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */


/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->gprs[15];

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else if (!user_mode(regs) &&
		 !(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		sp = (unsigned long) ka->sa.sa_restorer;
	}

	return (void *)((sp - frame_size) & -8ul);
}

static inline int map_signal(int sig)
{
	if (current->exec_domain
	    && current->exec_domain->signal_invmap
	    && sig < 32)
		return current->exec_domain->signal_invmap[sig];
	else
		return sig;
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs * regs)
{
	sigframe *frame = get_sigframe(ka, regs, sizeof(sigframe));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(sigframe)))
		goto give_sigsegv;

	if (__copy_to_user(&frame->sc.oldmask, &set->sig, _SIGMASK_COPY_SIZE))
		goto give_sigsegv;

	if (save_sigregs(regs, &frame->sregs))
		goto give_sigsegv;
	if (__put_user(&frame->sregs, &frame->sc.sregs))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
                regs->gprs[14] = FIX_PSW(ka->sa.sa_restorer);
	} else {
                regs->gprs[14] = FIX_PSW(frame->retcode);
		if (__put_user(S390_SYSCALL_OPCODE | __NR_sigreturn, 
	                       (u16 *)(frame->retcode)))
			goto give_sigsegv;
	}

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (addr_t *) frame))
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->gprs[15] = (addr_t)frame;
	regs->psw.addr = FIX_PSW(ka->sa.sa_handler);
	regs->psw.mask = _USER_PSW_MASK;

	regs->gprs[2] = map_signal(sig);
	regs->gprs[3] = (addr_t)&frame->sc;

	/* We forgot to include these in the sigcontext.
	   To avoid breaking binary compatibility, they are passed as args. */
	regs->gprs[4] = current->thread.trap_no;
	regs->gprs[5] = current->thread.prot_addr;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	int err = 0;
	rt_sigframe *frame = get_sigframe(ka, regs, sizeof(rt_sigframe));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(rt_sigframe)))
		goto give_sigsegv;

	if (copy_siginfo_to_user(&frame->info, info))
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->gprs[15]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= save_sigregs(regs, &frame->uc.uc_mcontext);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
                regs->gprs[14] = FIX_PSW(ka->sa.sa_restorer);
	} else {
                regs->gprs[14] = FIX_PSW(frame->retcode);
		err |= __put_user(S390_SYSCALL_OPCODE | __NR_rt_sigreturn, 
	                          (u16 *)(frame->retcode));
	}

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (addr_t *) frame))
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->gprs[15] = (addr_t)frame;
	regs->psw.addr = FIX_PSW(ka->sa.sa_handler);
	regs->psw.mask = _USER_PSW_MASK;

	regs->gprs[2] = map_signal(sig);
	regs->gprs[3] = (addr_t)&frame->info;
	regs->gprs[4] = (addr_t)&frame->uc;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	struct k_sigaction *ka = &current->sighand->action[sig-1];

	/* Are we from a system call? */
	if (regs->trap == __LC_SVC_OLD_PSW) {
		/* If so, check system call restarting.. */
		switch (regs->gprs[2]) {
			case -ERESTARTNOHAND:
				regs->gprs[2] = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->gprs[2] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->gprs[2] = regs->orig_gpr2;
				regs->psw.addr -= 2;
		}
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
}


/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, regs);
	if (signr > 0) {

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if ( regs->trap == __LC_SVC_OLD_PSW /* System Call! */ ) {
		/* Restart the system call - no handlers present */
		if (regs->gprs[2] == -ERESTARTNOHAND ||
		    regs->gprs[2] == -ERESTARTSYS ||
		    regs->gprs[2] == -ERESTARTNOINTR) {
			regs->gprs[2] = regs->orig_gpr2;
			regs->psw.addr -= 2;
		}
	}
	return 0;
}
