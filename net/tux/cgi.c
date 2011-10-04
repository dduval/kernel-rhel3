/*
 * TUX - Integrated Application Protocols Layer and Object Cache
 *
 * Copyright (C) 2000, 2001, Ingo Molnar <mingo@redhat.com>
 *
 * cgi.c: user-space CGI (and other) code execution.
 */

#define __KERNEL_SYSCALLS__

#include <net/tux.h>

/****************************************************************
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

/*
 * Define our own execve() syscall - the unistd.h one uses
 * errno which is not an exported symbol. (but removing it
 * breaks old userspace tools.)
 */
#ifdef __i386__

static int tux_execve (const char *file, char **argv, char **envp)
{
	int ret;
	__asm__ volatile ("int $0x80"
		: "=a" (ret)
		: "0" (__NR_execve),
		  "b" ((int)file),
		  "c" ((int)argv),
		  "d" ((long)envp));
	return ret;
}

#else
#ifdef CONFIG_ALPHA

static int tux_execve (const char *arg1, char **arg2, char **arg3)
{
	long _sc_ret, _sc_err;
	{
		register long _sc_0 __asm__("$0");
		register long _sc_16 __asm__("$16");
		register long _sc_17 __asm__("$17");
		register long _sc_18 __asm__("$18");
		register long _sc_19 __asm__("$19");

		_sc_0 = __NR_execve;
		_sc_16 = (long) (arg1);
		_sc_17 = (long) (arg2);
		_sc_18 = (long) (arg3);
		__asm__("callsys # %0 %1 %2 %3 %4 %5"
			: "=r"(_sc_0), "=r"(_sc_19)
			: "0"(_sc_0), "r"(_sc_16), "r"(_sc_17),
			  "r"(_sc_18)
			: _syscall_clobbers);
		_sc_ret = _sc_0, _sc_err = _sc_19;
	}
	return _sc_err;
}
#else
#ifdef CONFIG_PPC
static int tux_execve (const char *arg1, char **arg2, char **arg3)
{
	unsigned long __sc_ret, __sc_err;
	{
		register unsigned long __sc_0 __asm__ ("r0");
		register unsigned long __sc_3 __asm__ ("r3");
		register unsigned long __sc_4 __asm__ ("r4");
		register unsigned long __sc_5 __asm__ ("r5");

		__sc_3 = (unsigned long) (arg1);
		__sc_4 = (unsigned long) (arg2);
		__sc_5 = (unsigned long) (arg3);
		__sc_0 = __NR_execve;
		__asm__ __volatile__
			("sc       \n\t"
			 "mfcr %1      "
			: "=&r" (__sc_3), "=&r" (__sc_0)
			: "0"   (__sc_3), "1"   (__sc_0),
			  "r"   (__sc_4),
			  "r"   (__sc_5)
			: "r8", "r9", "r10", "r11", "r12");
		__sc_ret = __sc_3;
		__sc_err = __sc_0;
	}
	return __sc_err;
}
#else
#ifdef CONFIG_ARCH_S390	/* OK for both 31 and 64 bit mode */
static int tux_execve(const char *file, char **argv, char **envp)
{
        register const char * __arg1 asm("2") = file;
        register char ** __arg2 asm("3") = argv;
        register char ** __arg3 asm("4") = envp;
        register long __svcres asm("2");
        __asm__ __volatile__ (
                "    svc %b1\n"
                : "=d" (__svcres)
                : "i" (__NR_execve),
                  "0" (__arg1),
                  "d" (__arg2),
                  "d" (__arg3)
                : "cc", "memory");
        return __svcres;
}
#else
# define tux_execve execve
#endif
#endif
#endif
#endif

static int exec_usermode(char *program_path, char *argv[], char *envp[])
{
	int i, err;

	err = tux_chroot(tux_cgiroot);
	if (err) {
		printk(KERN_ERR "TUX: CGI chroot returned %d, /proc/sys/net/tux/cgiroot is probably set up incorrectly! Aborting CGI execution.\n", err);
		return err;
	}

	/* Allow execve args to be in kernel space. */
	set_fs(KERNEL_DS);

	spin_lock_irq(&current->sighand->siglock);
	flush_signals(current);
	flush_signal_handlers(current);
	spin_unlock_irq(&current->sighand->siglock);

	for (i = 3; i < current->files->max_fds; i++ )
		if (current->files->fd[i])
			sys_close(i);

	err = tux_execve(program_path, argv, envp);
	if (err < 0)
		return err;
	return 0;
}

static int exec_helper (void * data)
{
	exec_param_t *param = data;
	char **tmp;
	int ret;

	sprintf(current->comm,"doexec - %d", current->pid);
#if CONFIG_SMP
	if (!tux_cgi_inherit_cpu) {
		unsigned int mask = cpu_online_map & tux_cgi_cpu_mask;

		if (mask)
			set_cpus_allowed(current, mask);
		else
			set_cpus_allowed(current, cpu_online_map);
	}
#endif

	if (!param)
		TUX_BUG();
	Dprintk("doing exec(%s).\n", param->command);

	Dprintk("argv: ");
	tmp = param->argv;
	while (*tmp) {
		Dprintk("{%s} ", *tmp);
		tmp++;
	}
	Dprintk("\n");
	Dprintk("envp: ");
	tmp = param->envp;
	while (*tmp) {
		Dprintk("{%s} ", *tmp);
		tmp++;
	}
	Dprintk("\n");
	/*
	 * Set up stdin, stdout and stderr of the external
	 * CGI application.
	 */
	if (param->pipe_fds) {
		sys_close(1);
		sys_close(2);
		sys_close(4);
		if (sys_dup(3) != 1)
			TUX_BUG();
		if (sys_dup(5) != 2)
			TUX_BUG();
		sys_close(3);
		sys_close(5);
		// do not close on exec.
		sys_fcntl(0, F_SETFD, 0);
		sys_fcntl(1, F_SETFD, 0);
		sys_fcntl(2, F_SETFD, 0);
	}
	ret = exec_usermode(param->command, param->argv, param->envp);
	if (ret < 0)
		Dprintk("bug: exec() returned %d.\n", ret);
	else
		Dprintk("exec()-ed successfully!\n");
	return 0;
}

pid_t tux_exec_process (char *command, char **argv,
			char **envp, int pipe_fds,
				exec_param_t *param, int wait)
{
	exec_param_t param_local;
	pid_t pid;
	struct k_sigaction *ka;

	ka = current->sighand->action + SIGCHLD-1;
	ka->sa.sa_handler = SIG_IGN;

	if (!param && wait)
		param = &param_local;

	param->command = command;
	param->argv = argv;
	param->envp = envp;
	param->pipe_fds = pipe_fds;

repeat_fork:
	pid = kernel_thread(exec_helper, (void*) param, CLONE_SIGHAND|SIGCHLD);
	Dprintk("kernel thread created PID %d.\n", pid);
	if (pid < 0) {
		printk(KERN_ERR "TUX: could not create new CGI kernel thread due to %d... retrying.\n", pid);
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ);
		goto repeat_fork;
	}
	return pid;
}
