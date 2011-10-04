#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <asm/param.h>	/* for HZ */

extern unsigned long event;

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/binfmts.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/times.h>
#include <linux/timex.h>
#include <linux/rbtree.h>

#include <asm/system.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/mmu.h>

#include <linux/smp.h>
#include <linux/tty.h>
#include <linux/sem.h>
#include <linux/signal.h>
#include <linux/securebits.h>
#include <linux/fs_struct.h>
#include <linux/pid.h>
#include <linux/kernel_stat.h>

struct exec_domain;
extern int exec_shield;
extern int exec_shield_randomize;
extern int panic_timeout;
extern int print_fatal_signals;

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
#define CLONE_VM	0x00000100	/* set if VM shared between processes */
#define CLONE_FS	0x00000200	/* set if fs info shared between processes */
#define CLONE_FILES	0x00000400	/* set if open files shared between processes */
#define CLONE_SIGHAND	0x00000800	/* set if signal handlers and blocked signals shared */
#define CLONE_IDLETASK	0x00001000	/* set if new pid should be 0 (kernel only)*/
#define CLONE_PTRACE	0x00002000	/* set if we want to let tracing continue on the child too */
#define CLONE_VFORK	0x00004000	/* set if the parent wants the child to wake it up on mm_release */
#define CLONE_PARENT	0x00008000	/* set if we want to have the same parent as the cloner */
#define CLONE_THREAD	0x00010000	/* Same thread group? */
#define CLONE_NEWNS	0x00020000	/* New namespace group? */
#define CLONE_SYSVSEM	0x00040000	/* share system V SEM_UNDO semantics */
#define CLONE_SETTLS	0x00080000	/* create a new TLS for the child */
#define CLONE_PARENT_SETTID	0x00100000	/* set the TID in the parent */
#define CLONE_CHILD_CLEARTID	0x00200000	/* clear the TID in the child */
#define CLONE_DETACHED		0x00400000	/* parent wants no child-exit signal */
#define CLONE_UNTRACED		0x00800000	/* set if the tracing process can't force CLONE_PTRACE on this clone */
#define CLONE_CHILD_SETTID	0x01000000	/* set the TID in the child */
#define CLONE_STOPPED		0x02000000	/* start in stopped state */

/*
 * List of flags we want to share for kernel threads,
 * if only because they are not used by them anyway.
 */
#define CLONE_KERNEL	(CLONE_FS | CLONE_FILES | CLONE_SIGHAND)

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ)		/* 5 sec intervals */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */

#define CALC_LOAD(load,exp,n) \
	load *= exp; \
	load += n*(FIXED_1-exp); \
	load >>= FSHIFT;

#define CT_TO_SECS(x)	((x) / HZ)
#define CT_TO_USECS(x)	(((x) % HZ) * 1000000/HZ)

extern int nr_threads;
extern int last_pid;
extern unsigned long nr_running(void);
extern unsigned long nr_iowait(void);
extern unsigned long nr_uninterruptible(void);

//#include <linux/fs.h>
#include <linux/time.h>
#include <linux/param.h>
#include <linux/resource.h>
#ifdef __KERNEL__
#include <linux/timer.h>
#endif

#include <asm/processor.h>

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_STOPPED		4
#define TASK_ZOMBIE		8
#define TASK_DEAD		16

#define __set_task_state(tsk, state_value)		\
	do { (tsk)->state = (state_value); } while (0)
#define set_task_state(tsk, state_value)		\
	set_mb((tsk)->state, (state_value))

#define __set_current_state(state_value)			\
	do { current->state = (state_value); } while (0)
#define set_current_state(state_value)		\
	set_mb(current->state, (state_value))

/*
 * Scheduling policies
 */
#define SCHED_NORMAL		0
#define SCHED_FIFO		1
#define SCHED_RR		2

struct sched_param {
	int sched_priority;
};

struct completion;

#ifdef __KERNEL__

#include <linux/spinlock.h>

/*
 * This serializes "schedule()" and also protects
 * the run-queue from deletions/modifications (but
 * _adding_ to the beginning of the run-queue has
 * a separate lock).
 */
extern rwlock_t tasklist_lock;
extern spinlock_t mmlist_lock;

typedef struct task_struct task_t;

extern void __put_task_struct(struct task_struct *tsk);
#define get_task_struct(tsk) do { atomic_inc(&(tsk)->usage); } while(0)
#define put_task_struct(tsk) \
do { if (atomic_dec_and_test(&(tsk)->usage)) __put_task_struct(tsk); } while(0)

extern void sched_init(void);
extern void init_idle(task_t *idle, int cpu);
extern void sched_map_runqueue(int cpu1, int cpu2);
extern void show_state(void);
extern void show_stack(unsigned long * esp);
extern void cpu_init (void);
extern void trap_init(void);
extern void update_process_times(int user);
extern void update_process_time_intertick(task_t *,
					  struct kernel_stat_tick_times *);
#ifdef CONFIG_NO_IDLE_HZ
extern void update_process_times_us(int user, int system);
#endif
extern void update_one_process(task_t *p, struct kernel_stat_tick_times *,
			       int cpu);
extern void scheduler_tick(int timer_tick);
extern void update_kstatpercpu(task_t *, struct kernel_stat_tick_times *time);
extern int migration_init(void);
extern unsigned long cache_decay_ticks;

#define	MAX_SCHEDULE_TIMEOUT	LONG_MAX
extern signed long FASTCALL(schedule_timeout(signed long timeout));
asmlinkage void schedule(void);
void io_schedule(void);
long io_schedule_timeout(long timeout);


extern int schedule_task(struct tq_struct *task);
extern void flush_scheduled_tasks(void);
extern int start_context_thread(void);
extern int current_is_keventd(void);

#if CONFIG_SMP
extern void set_cpus_allowed(struct task_struct *p, unsigned long new_mask);
#else
# define set_cpus_allowed(p, new_mask) do { } while (0)
#endif

/*
 * Is there a way to do this via Kconfig?
 */
#if CONFIG_NR_SIBLINGS_2
# define CONFIG_NR_SIBLINGS 2
#elif CONFIG_NR_SIBLINGS_4
# define CONFIG_NR_SIBLINGS 4
#else
# define CONFIG_NR_SIBLINGS 0
#endif

#if CONFIG_NR_SIBLINGS
# define CONFIG_SHARE_RUNQUEUE 1
#else
# define CONFIG_SHARE_RUNQUEUE 0
#endif
extern void sched_map_runqueue(int cpu1, int cpu2);

/*
 * Priority of a process goes from 0..MAX_PRIO-1, valid RT
 * priority is 0..MAX_RT_PRIO-1, and SCHED_NORMAL tasks are
 * in the range MAX_RT_PRIO..MAX_PRIO-1. Priority values
 * re inverted: lower p->prio value means higher priority.
 *
 * The MAX_RT_USER_PRIO value allows the actual maximum
 * RT priority to be separate from the value exported to
 * user-space.  This allows kernel threads to set their
 * priority to a value higher than any user task. Note:
 * MAX_RT_PRIO must not be smaller than MAX_USER_RT_PRIO.
 */

#define MAX_USER_RT_PRIO        100
#define MAX_RT_PRIO             MAX_USER_RT_PRIO

#define MAX_PRIO                (MAX_RT_PRIO + 40)

/*
 * The default fd array needs to be at least BITS_PER_LONG,
 * as this is the granularity returned by copy_fdset().
 */
#define NR_OPEN_DEFAULT BITS_PER_LONG

struct namespace;
/*
 * Open file table structure
 */
struct files_struct {
	atomic_t count;
	rwlock_t file_lock;	/* Protects all the below members.  Nests inside tsk->alloc_lock */
	int max_fds;
	int max_fdset;
	int next_fd;
	struct file ** fd;	/* current fd array */
	fd_set *close_on_exec;
	fd_set *open_fds;
	fd_set close_on_exec_init;
	fd_set open_fds_init;
	struct file * fd_array[NR_OPEN_DEFAULT];
};

#define INIT_FILES \
{ 							\
	count:		ATOMIC_INIT(1), 		\
	file_lock:	RW_LOCK_UNLOCKED, 		\
	max_fds:	NR_OPEN_DEFAULT, 		\
	max_fdset:	__FD_SETSIZE, 			\
	next_fd:	0, 				\
	fd:		&init_files.fd_array[0], 	\
	close_on_exec:	&init_files.close_on_exec_init, \
	open_fds:	&init_files.open_fds_init, 	\
	close_on_exec_init: { { 0, } }, 		\
	open_fds_init:	{ { 0, } }, 			\
	fd_array:	{ NULL, } 			\
}

/* Maximum number of active map areas.. This is a random (large) number */
#define DEFAULT_MAX_MAP_COUNT	(65536)

extern int max_map_count;

struct kioctx;
struct mm_struct {
	struct vm_area_struct * mmap;		/* list of VMAs */
	rb_root_t mm_rb;
	struct vm_area_struct * mmap_cache;	/* last find_vma result */
	unsigned long free_area_cache;		/* first hole */
	unsigned long non_executable_cache;	/* last hole top */
	pgd_t * pgd;
	atomic_t mm_users;			/* How many users with user space? */
	atomic_t mm_count;			/* How many references to "struct mm_struct" (users count as 1) */
	int map_count;				/* number of VMAs */
	struct rw_semaphore mmap_sem;
	spinlock_t page_table_lock;		/* Protects task page tables and mm->rss */

	struct list_head mmlist;		/* List of all active mm's.  These are globally strung
						 * together off init_mm.mmlist, and are protected
						 * by mmlist_lock
						 */

	unsigned long task_size;
	unsigned long start_code, end_code, start_data, end_data;
	unsigned long start_brk, brk, start_stack;
	unsigned long arg_start, arg_end, env_start, env_end;
	unsigned long rss, total_vm, locked_vm;
	unsigned long def_flags;
	unsigned long cpu_vm_mask;
	unsigned long rlimit_rss;

	unsigned dumpable:1;

	struct {
		unsigned long	lrs;
		unsigned long	drs;
		unsigned long	trs;
		unsigned long	rss;
		unsigned long	notpresent;
		unsigned long	present;
		unsigned long	sharable;
		unsigned long	writable;
	}		mm_stat;

#ifdef CONFIG_HUGETLB_PAGE
	int used_hugetlb;
#endif
	/* Architecture-specific MM context */
	mm_context_t context;

	/* coredumping support */
	int core_waiters;
	struct completion *core_startup_done, core_done;

	struct kioctx   *ioctx_list;
	unsigned long   new_ioctx_id;
};

extern int mmlist_nr;

#define INIT_MM(name) \
{			 				\
	mm_rb:		RB_ROOT,			\
	pgd:		swapper_pg_dir, 		\
	mm_users:	ATOMIC_INIT(2), 		\
	mm_count:	ATOMIC_INIT(1), 		\
	mmap_sem:	__RWSEM_INITIALIZER(name.mmap_sem), \
	page_table_lock: SPIN_LOCK_UNLOCKED, 		\
	mmlist:		LIST_HEAD_INIT(name.mmlist),	\
	rlimit_rss:	RLIM_INFINITY,			\
}

#define NON_EXECUTABLE_CACHE(task)     (((task)->rlim[RLIMIT_STACK].rlim_cur>TASK_SIZE?0: \
                                       TASK_SIZE-(task)->rlim[RLIMIT_STACK].rlim_cur)&PAGE_MASK)

#ifndef STACK_BUFFER_SPACE
#define STACK_BUFFER_SPACE	((unsigned long)(128 * 1024 * 1024))
#endif

extern void show_stack(unsigned long *esp);

extern int __broadcast_thread_group(struct task_struct *p, int sig);

struct sighand_struct {
	atomic_t		count;
	struct k_sigaction	action[_NSIG];
	spinlock_t		siglock;
};

/*
 * NOTE! "signal_struct" does not have it's own
 * locking, because a shared signal_struct always
 * implies a shared sighand_struct, so locking
 * sighand_struct is always a proper superset of
 * the locking of signal_struct.
 */
struct signal_struct {
	atomic_t		count;

	/* current thread group signal load-balancing target: */
	task_t			*curr_target;

	/* shared signal handling: */
	struct sigpending	shared_pending;

	/* thread group exit support */
	int			group_exit;
	int			group_exit_code;
	struct task_struct	*group_exit_task;

	/* thread group stop support, overloads group_exit_code too */
	int			group_stop_count;
};


#define INIT_SIGNALS(sig) {	\
	.count		= ATOMIC_INIT(1), 		\
	.shared_pending	= { NULL, &sig.shared_pending.head, {{0}}}, \
}

#define INIT_SIGHAND(sighand) {	\
	.count		= ATOMIC_INIT(1), 		\
	.action		= { {{0,}}, }, 			\
	.siglock	= SPIN_LOCK_UNLOCKED, 		\
}

/*
 * Some day this will be a full-fledged user tracking system..
 */
struct user_struct {
	atomic_t __count;	/* reference count */
	atomic_t processes;	/* How many processes does this user have? */
	atomic_t files;		/* How many open files does this user have? */

	/* Hash table maintenance information */
	struct list_head uidhash_list;
	uid_t uid;
};

#define get_current_user() ({ 				\
	struct user_struct *__user = current->user;	\
	atomic_inc(&__user->__count);			\
	__user; })

extern struct user_struct *find_user(uid_t);

extern struct user_struct root_user;
#define INIT_USER (&root_user)

typedef struct prio_array prio_array_t;

struct task_struct {

	/* --- start of hardcoded fields - touch with care --- */

	volatile long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	unsigned long flags;	/* per process flags, defined below */
	int sigpending;
	mm_segment_t addr_limit;	/* thread address space:
					 	0-0xBFFFFFFF for user-thead
						0-0xFFFFFFFF for kernel-thread
					 */
	struct exec_domain *exec_domain;
	volatile long need_resched;
	unsigned long ptrace;

	int lock_depth;		/* Lock depth */

	/*
	 * offset 32 begins here on 32-bit platforms.
	 */
	unsigned int cpu;
	void *real_stack, *virtual_stack, *user_pgd;

	/* ------- end of hardcoded fields ---------------- */

	int prio, static_prio;

	struct list_head run_list;
	prio_array_t *array;

	unsigned long sleep_avg;
	unsigned long last_run;

	unsigned long policy;
	unsigned long cpus_allowed;
	unsigned int time_slice, first_time_slice;

	atomic_t usage;

	struct list_head tasks;
	struct list_head ptrace_children;
	struct list_head ptrace_list;

	struct mm_struct *mm, *active_mm;

/* task state */
	struct linux_binfmt *binfmt;
	int exit_code, exit_signal;
	int pdeath_signal;  /*  The signal sent when the parent dies  */
	/* ??? */
	unsigned long personality;
	int did_exec:1;
	unsigned task_dumpable:1;
	pid_t pid;
	pid_t pgrp;
	pid_t tty_old_pgrp;
	pid_t session;
	pid_t tgid;
	/* boolean value for session group leader */
	int leader;
	/* 
	 * pointers to (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with 
	 * p->parent->pid)
	 */
	struct task_struct *real_parent; /* real parent process (when being debugged) */
	struct task_struct *parent;	/* parent process */
	struct list_head children;	/* list of my children */
	struct list_head sibling;	/* linkage in my parent's children list */
	struct task_struct *group_leader;

	/* PID/PID hash table linkage. */
	struct pid_link pids[PIDTYPE_MAX];

	wait_queue_head_t wait_chldexit;	/* for wait4() */
	struct completion *vfork_done;		/* for vfork() */
	int *set_child_tid;			/* CLONE_CHILD_SETTID */
	int *clear_child_tid;			/* CLONE_CHILD_CLEARTID */

	unsigned long rt_priority;
	unsigned long it_real_value, it_prof_value, it_virt_value;
	unsigned long it_real_incr, it_prof_incr, it_virt_incr;
	struct timer_list real_timer;
	struct kernel_timeval utime, stime, cutime, cstime;
	struct kernel_timeval group_utime, group_stime;
	struct kernel_timeval group_cutime, group_cstime;
	struct process_timing_state timing_state;
	unsigned int last_sigxcpu;	/* second of cpu time at which we sent
					   our last sigxcpu signal */

	unsigned long start_time;
	struct kernel_timeval per_cpu_utime[NR_CPUS], per_cpu_stime[NR_CPUS];
/* mm fault and swap info: this can arguably be seen as either mm-specific or thread-specific */
	unsigned long min_flt, maj_flt, nswap, cmin_flt, cmaj_flt, cnswap;
	int swappable:1;
/* process credentials */
	uid_t uid,euid,suid,fsuid;
	gid_t gid,egid,sgid,fsgid;
	int ngroups;
	gid_t	groups[NGROUPS];
	kernel_cap_t   cap_effective, cap_inheritable, cap_permitted;
	int keep_capabilities:1;
	struct user_struct *user;
/* limits */
	struct rlimit rlim[RLIM_NLIMITS];
	unsigned int used_math;
	char comm[16];
/* file system info */
	int link_count, total_link_count;
	struct tty_struct *tty; /* NULL if no tty */
	unsigned int locks; /* How many file locks are being held */
/* ipc stuff */
	struct sem_undo *semundo;
	struct sem_queue *semsleeping;
/* CPU-specific state of this task */
	struct thread_struct thread;
/* filesystem information */
	struct fs_struct *fs;
/* open file information */
	struct files_struct *files;
/* namespace */
	struct namespace *namespace;
/* signal handlers */
	struct signal_struct *signal;
	struct sighand_struct *sighand;

	sigset_t blocked, real_blocked;
	struct sigpending pending;

	unsigned long sas_ss_sp;
	size_t sas_ss_size;
	int (*notifier)(void *priv);
	void *notifier_data;
	sigset_t *notifier_mask;

	/* TUX state */
	void *tux_info;
	void (*tux_exit)(void);

	
/* Thread group tracking */
   	u32 parent_exec_id;
   	u32 self_exec_id;
/* Protection of (de-)allocation: mm, files, fs, tty */
	spinlock_t alloc_lock;
/* context-switch lock */
	spinlock_t switch_lock;

/* journalling filesystem info */
	void *journal_info;

	unsigned long ptrace_message;
	siginfo_t *last_siginfo; /* For ptrace use.  */

#ifndef __GENKSYMS__ /* preserve KMI/ABI ksyms compatibility for mod linkage */
#if defined(CONFIG_IA64) && defined(CONFIG_IA32_SUPPORT)
	struct desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
#endif
#if defined(CONFIG_AUDIT) || defined(CONFIG_AUDIT_MODULE)
	void *audit;
#endif
#endif /* !__GENKSYMS__ */
};

/*
 * Per process flags
 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/
#define PF_STARTING	0x00000002	/* being created */
#define PF_EXITING	0x00000004	/* getting shut down */
#define PF_TIMING	0x00000008	/* enable accurate process accounting */
#define PF_FORKNOEXEC	0x00000040	/* forked but didn't exec */
#define PF_SUPERPRIV	0x00000100	/* used super-user privileges */
#define PF_DUMPCORE	0x00000200	/* dumped core */
#define PF_SIGNALED	0x00000400	/* killed by a signal */
#define PF_MEMALLOC	0x00000800	/* Allocating memory */
#define PF_MEMDIE	0x00001000	/* Killed for out-of-memory */
#define PF_FREE_PAGES	0x00002000	/* per process page freeing */
#define PF_NOIO		0x00004000	/* avoid generating further I/O */
#define PF_USEDFPU	0x00100000	/* task used FPU this quantum (SMP) */
#define PF_FSTRANS	0x00200000	/* inside a filesystem transaction */
#define PF_RELOCEXEC	0x00400000	/* relocate shared libraries */

/*
 * Ptrace flags
 */

#define PT_PTRACED	0x00000001
#define PT_TRACESYS	0x00000002
#define PT_TRACESYSGOOD	0x00000004
#define PT_PTRACE_CAP	0x00000008      /* ptracer can follow suid-exec */
#define PT_TRACE_FORK	0x00000010
#define PT_TRACE_VFORK	0x00000020
#define PT_TRACE_CLONE	0x00000040
#define PT_TRACE_EXEC	0x00000080
#define PT_TRACE_VFORK_DONE 0x00000100
#define PT_TRACE_EXIT   0x00000200
#define PT_DTRACE	0x00000400      /* delayed trace (used on m68k, i386) */
#define PT_AUDITED	0x00000800      /* being audited */
#define PT_SINGLESTEP	0x00001000	/* PTRACE_SINGLESTEP in progress */

#define is_dumpable(tsk)    ((tsk)->task_dumpable && (tsk)->mm && (tsk)->mm->dumpable)

/*
 * Limit the stack by to some sane default: root can always
 * increase this limit if needed..  10MB seems reasonable.
 */
#define _STK_LIM	(10*1024*1024)

#if CONFIG_SMP
extern void set_cpus_allowed(task_t *p, unsigned long new_mask);
#else
#define set_cpus_allowed(p, new_mask)	do { } while (0)
#endif

extern void set_user_nice(task_t *p, long nice);
extern int task_prio(task_t *p);
extern int task_nice(task_t *p);
extern int task_curr(task_t *p);
extern int idle_cpu(int cpu);
extern task_t *cpu_idle_ptr(int cpu);
extern task_t *cpu_curr_ptr(int cpu);


/* Reevaluate whether the task has signals pending delivery.
 * This is required every time the blocked sigset_t changes.
 * callers must hold sig->siglock.  */

extern FASTCALL(void recalc_sigpending_tsk(struct task_struct *t));
extern void recalc_sigpending(void);
extern void signal_wake_up(struct task_struct *t, int resume_stopped);
extern void print_signals(struct task_struct *t);


/*
 *  * Wrappers for p->cpu access. No-op on UP.
 *   */
#ifdef CONFIG_SMP

#define cpu_online(cpu) ((cpu) < smp_num_cpus)

static inline unsigned int task_cpu(struct task_struct *p)
{
	return p->cpu;
}

#else



static inline unsigned int task_cpu(struct task_struct *p)
{
	return 0;
}

#endif /* CONFIG_SMP */


extern void yield(void);

/*
 * The default (Linux) execution domain.
 */
extern struct exec_domain	default_exec_domain;

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
    state:		0,						\
    flags:		0,						\
    sigpending:		0,						\
    addr_limit:		KERNEL_DS,					\
    exec_domain:	&default_exec_domain,				\
    lock_depth:		-1,						\
    prio:		MAX_PRIO-20,					\
    static_prio:	MAX_PRIO-20,					\
    policy:		SCHED_NORMAL,					\
    cpus_allowed:	~0UL,						\
    mm:			NULL,						\
    active_mm:		&init_mm,					\
    run_list:		LIST_HEAD_INIT(tsk.run_list),			\
    time_slice:		HZ,						\
    tasks:		LIST_HEAD_INIT(tsk.tasks),			\
    ptrace_children:	LIST_HEAD_INIT(tsk.ptrace_children),		\
    ptrace_list:	LIST_HEAD_INIT(tsk.ptrace_list),		\
    real_parent:	&tsk,						\
    parent:		&tsk,						\
    children:		LIST_HEAD_INIT(tsk.children),			\
    sibling:		LIST_HEAD_INIT(tsk.sibling),			\
    group_leader:	&tsk,						\
    wait_chldexit:	__WAIT_QUEUE_HEAD_INITIALIZER(tsk.wait_chldexit),\
    real_timer:		{						\
	function:		it_real_fn				\
    },									\
    cap_effective:	CAP_INIT_EFF_SET,				\
    cap_inheritable:	CAP_INIT_INH_SET,				\
    cap_permitted:	CAP_FULL_SET,					\
    keep_capabilities:	0,						\
    rlim:		INIT_RLIMITS,					\
    user:		INIT_USER,					\
    comm:		"swapper",					\
    thread:		INIT_THREAD,					\
    fs:			&init_fs,					\
    files:		&init_files,					\
    signal:		&init_signals,					\
    sighand:		&init_sighand,					\
    pending:		{ NULL, &tsk.pending.head, {{0}}},		\
    blocked:		{{0}},						\
    alloc_lock:		SPIN_LOCK_UNLOCKED,				\
    switch_lock:	SPIN_LOCK_UNLOCKED,				\
    journal_info:	NULL,						\
    real_stack:		&tsk,						\
}


#ifndef INIT_TASK_SIZE
# define INIT_TASK_SIZE	2048*sizeof(long)
#endif

union task_union {
	task_t task;
	unsigned long stack[INIT_TASK_SIZE/sizeof(long)];
};

extern union task_union init_task_union;

extern struct   mm_struct init_mm;

extern struct task_struct *find_task_by_pid(int pid);

/* per-UID process charging. */
extern struct user_struct * alloc_uid(uid_t);
extern void free_uid(struct user_struct *);
extern void switch_uid(struct user_struct *);

#include <asm/current.h>

extern unsigned long volatile jiffies;
extern unsigned long itimer_ticks;
extern unsigned long itimer_next;
extern struct timeval xtime;
extern void do_timer(struct pt_regs *);
#ifdef CONFIG_NO_IDLE_HZ
extern void do_timer_ticks(int ticks);
#endif

#define CURRENT_TIME (xtime.tv_sec)

/*
 *	These inlines deal with timer wrapping correctly. You are 
 *	strongly encouraged to use them
 *	1. Because people otherwise forget
 *	2. Because if the timer wrap changes in future you won't have to
 *	   alter your driver code.
 *
 * time_after(a,b) returns true if the time a is after time b.
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 */
#define time_after(a,b)		\
	(typecheck(unsigned long, a) && \
	 typecheck(unsigned long, b) && \
	 ((long)(b) - (long)(a) < 0))
#define time_before(a,b)	time_after(b,a)

#define time_after_eq(a,b)	\
	(typecheck(unsigned long, a) && \
	 typecheck(unsigned long, b) && \
	 ((long)(a) - (long)(b) >= 0))
#define time_before_eq(a,b)	time_after_eq(b,a)

extern int FASTCALL(wake_up_state(struct task_struct * tsk, unsigned int state));
extern void FASTCALL(wake_up_filtered(wait_queue_head_t *, void *));
extern void FASTCALL(__wake_up(wait_queue_head_t *q, unsigned int mode, int nr));
extern void FASTCALL(__wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr));
extern void FASTCALL(sleep_on(wait_queue_head_t *q));
extern long FASTCALL(sleep_on_timeout(wait_queue_head_t *q,
				      signed long timeout));
extern void FASTCALL(interruptible_sleep_on(wait_queue_head_t *q));
extern long FASTCALL(interruptible_sleep_on_timeout(wait_queue_head_t *q,
						    signed long timeout));
extern int FASTCALL(wake_up_process(task_t * p));
extern int FASTCALL(wake_up_process_kick(task_t * p));
extern void FASTCALL(wake_up_forked_process(task_t * p));
extern void FASTCALL(sched_exit(task_t * p));

#define wake_up(x)			__wake_up((x),TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 1)
#define wake_up_nr(x, nr)		__wake_up((x),TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, nr)
#define wake_up_all(x)			__wake_up((x),TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 0)
#define wake_up_interruptible(x)	__wake_up((x),TASK_INTERRUPTIBLE, 1)
#define wake_up_interruptible_nr(x, nr)	__wake_up((x),TASK_INTERRUPTIBLE, nr)
#define wake_up_interruptible_all(x)	__wake_up((x),TASK_INTERRUPTIBLE, 0)
#define wake_up_interruptible_sync(x)   __wake_up_sync((x),TASK_INTERRUPTIBLE, 1)

asmlinkage long sys_wait4(pid_t pid,unsigned int * stat_addr, int options, struct rusage * ru);

extern int in_group_p(gid_t);
extern int in_egroup_p(gid_t);

extern void proc_caches_init(void);
extern void flush_signals(task_t *);
extern void flush_signal_handlers(task_t *);
extern int dequeue_signal(sigset_t *, siginfo_t *);
extern void block_all_signals(int (*notifier)(void *priv), void *priv,
			      sigset_t *mask);
extern void unblock_all_signals(void);
extern int send_sig_info(int, struct siginfo *, task_t *);
extern int force_sig_info(int, struct siginfo *, task_t *);
extern int group_send_sig_info(int, struct siginfo *, task_t *);
extern int __kill_pg_info(int sig, struct siginfo *info, pid_t pgrp);
extern int kill_pg_info(int, struct siginfo *, pid_t);
extern int kill_sl_info(int, struct siginfo *, pid_t);
extern int kill_proc_info(int, struct siginfo *, pid_t);
extern int kill_proc_info_as_uid(int, struct siginfo *, pid_t, uid_t, uid_t);
extern void notify_parent(task_t *, int);
extern void do_notify_parent(task_t *, int);
extern void force_sig(int, task_t *);
extern void force_sig_specific(int, struct task_struct *);
extern int send_sig(int, task_t *, int);
extern void zap_other_threads(struct task_struct *p);
extern int kill_pg(pid_t, int, int);
extern int kill_sl(pid_t, int, int);
extern int kill_proc(pid_t, int, int);
extern int do_sigaction(int, const struct k_sigaction *, struct k_sigaction *);
extern int do_sigaltstack(const stack_t *, stack_t *, unsigned long);

static inline int signal_pending(task_t *p)
{
	return (p->sigpending != 0);
}

/* True if we are on the alternate signal stack.  */

static inline int on_sig_stack(unsigned long sp)
{
	return (sp - current->sas_ss_sp < current->sas_ss_size);
}

static inline int sas_ss_flags(unsigned long sp)
{
	return (current->sas_ss_size == 0 ? SS_DISABLE
		: on_sig_stack(sp) ? SS_ONSTACK : 0);
}

extern int request_irq(unsigned int,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long, const char *, void *);
extern void free_irq(unsigned int, void *);

/*
 * This has now become a routine instead of a macro, it sets a flag if
 * it returns true (to do BSD-style accounting where the process is flagged
 * if it uses root privs). The implication of this is that you should do
 * normal permissions checks first, and check suser() last.
 *
 * [Dec 1997 -- Chris Evans]
 * For correctness, the above considerations need to be extended to
 * fsuser(). This is done, along with moving fsuser() checks to be
 * last.
 *
 * These will be removed, but in the mean time, when the SECURE_NOROOT 
 * flag is set, uids don't grant privilege.
 */
static inline int suser(void)
{
	if (!issecure(SECURE_NOROOT) && current->euid == 0) { 
		current->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}

static inline int fsuser(void)
{
	if (!issecure(SECURE_NOROOT) && current->fsuid == 0) {
		current->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}

/*
 * capable() checks for a particular capability.  
 * New privilege checks should use this interface, rather than suser() or
 * fsuser(). See include/linux/capability.h for defined capabilities.
 */

static inline int capable(int cap)
{
#if 1 /* ok now */
	if (cap_raised(current->cap_effective, cap))
#else
	if (cap_is_fs_cap(cap) ? current->fsuid == 0 : current->euid == 0)
#endif
	{
		current->flags |= PF_SUPERPRIV;
		return 1;
	}
	return 0;
}

/*
 * Routines for handling mm_structs
 */
extern struct mm_struct * mm_alloc(void);

extern struct mm_struct * start_lazy_tlb(void);
extern void end_lazy_tlb(struct mm_struct *mm);

/* mmdrop drops the mm and the page tables */
extern inline void FASTCALL(__mmdrop(struct mm_struct *));
static inline void mmdrop(struct mm_struct * mm)
{
	if (atomic_dec_and_test(&mm->mm_count))
		__mmdrop(mm);
}

/* mmput gets rid of the mappings and all user-space */
extern void mmput(struct mm_struct *);
/* Remove the current tasks stale references to the old mm_struct */
extern void mm_release(void);

/*
 * Routines for handling the fd arrays
 */
extern struct file ** alloc_fd_array(int);
extern int expand_fd_array(struct files_struct *, int nr);
extern void free_fd_array(struct file **, int);

extern fd_set *alloc_fdset(int);
extern int expand_fdset(struct files_struct *, int nr);
extern void free_fdset(fd_set *, int);

extern int unshare_files(void);

extern int  copy_thread(int, unsigned long, unsigned long, unsigned long, task_t *, struct pt_regs *);
extern void flush_thread(void);
extern void exit_thread(void);

extern void exit_mm(task_t *);
extern void exit_files(task_t *);
extern void exit_signal(struct task_struct *);
extern void __exit_signal(struct task_struct *);
extern void exit_sighand(struct task_struct *);
extern void __exit_sighand(struct task_struct *);

extern NORET_TYPE void do_group_exit(int);

extern void reparent_to_init(void);
extern void daemonize(void);
extern void __set_special_pids(pid_t session, pid_t pgrp);
extern void set_special_pids(pid_t session, pid_t pgrp);
extern task_t *child_reaper;

extern int do_execve(char *, char **, char **, struct pt_regs *);
extern int do_fork(unsigned long, unsigned long, struct pt_regs *, unsigned long, int *, int *);
extern void reap_thread(task_t *p);
extern void release_task(struct task_struct * p);

extern void set_task_comm(struct task_struct *tsk, char *from);
extern void get_task_comm(char *to, struct task_struct *tsk);

extern void FASTCALL(add_wait_queue(wait_queue_head_t *q, wait_queue_t * wait));
extern void FASTCALL(add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t * wait));
extern void FASTCALL(add_wait_queue_exclusive_lifo(wait_queue_head_t *q, wait_queue_t * wait));
extern void FASTCALL(remove_wait_queue(wait_queue_head_t *q, wait_queue_t * wait));

#ifdef CONFIG_SMP
extern void wait_task_inactive(task_t * p);
#else
#define wait_task_inactive(p) do {} while (0)
#endif

extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

#define __wait_event(wq, condition) 					\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		schedule();						\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event(wq, condition) 					\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event(wq, condition);					\
} while (0)

#define __wait_event_interruptible(wq, condition, ret)			\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_INTERRUPTIBLE);			\
		if (condition)						\
			break;						\
		if (!signal_pending(current)) {				\
			schedule();					\
			continue;					\
		}							\
		ret = -ERESTARTSYS;					\
		break;							\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)
	
#define wait_event_interruptible(wq, condition)				\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__wait_event_interruptible(wq, condition, __ret);	\
	__ret;								\
})

#define remove_parent(p)	list_del_init(&(p)->sibling)
#define add_parent(p, parent)	list_add_tail(&(p)->sibling,&(parent)->children)

#if 0

#define REMOVE_LINKS(p) do {			\
	if (thread_group_leader(p))		\
		list_del_init(&(p)->tasks);	\
	remove_parent(p);			\
	} while (0)

#define SET_LINKS(p) do {					\
	if (thread_group_leader(p))				\
		list_add_tail(&(p)->tasks,&init_task.tasks);	\
	add_parent(p, (p)->parent);				\
	} while (0)

#define next_task(p)	list_entry((p)->tasks.next, struct task_struct, tasks)
#define prev_task(p)	list_entry((p)->tasks.prev, struct task_struct, tasks)

#define __for_each_process(p) \
	for ( ; (p = next_task(p)) != &init_task ; )

#define for_each_process(p) \
	for (p = &init_task ; (p = next_task(p)) != &init_task ; )

/*
 * Careful: do_each_thread/while_each_thread is a double loop so
 *          'break' will not work as expected - use goto instead.
 */
#define do_each_thread(g, t) \
	for (g = t = &init_task ; (g = t = next_task(g)) != &init_task ; ) do

#define while_each_thread(g, t) \
	while ((t = next_thread(t)) != g)
#else

extern void check_tasklist_locked(void);

#define REMOVE_LINKS(p) do {					\
	check_tasklist_locked();				\
	BUG_ON(list_empty(&(p)->tasks));			\
	list_del_init(&(p)->tasks);				\
	remove_parent(p);					\
	} while (0)

#define SET_LINKS(p) do {					\
	check_tasklist_locked();				\
	list_add_tail(&(p)->tasks,&init_task.tasks);		\
	add_parent(p, (p)->parent);				\
	} while (0)

#define next_task(p)	({ check_tasklist_locked(); list_entry((p)->tasks.next, struct task_struct, tasks); })
#define prev_task(p)	({ check_tasklist_locked(); list_entry((p)->tasks.prev, struct task_struct, tasks); })

#define __for_each_process(p) \
	for ( ; (p = next_task(p)) != &init_task ; )

#define for_each_process(p) \
	for (p = &init_task ; (p = next_task(p)) != &init_task ; )

#define do_each_thread(g, t) \
	for (t = &init_task ; (t = next_task(t)) != &init_task ; )

#define while_each_thread(g, t)

#endif

extern task_t * FASTCALL(next_thread(task_t *p));

#define thread_group_leader(p)	(p->pid == p->tgid)

static inline int thread_group_empty(task_t *p)
{
	struct pid *pid = p->pids[PIDTYPE_TGID].pidptr;

	return pid->task_list.next->next == &pid->task_list;
}

#define delay_group_leader(p) \
		(thread_group_leader(p) && !thread_group_empty(p))

extern void unhash_process(struct task_struct *p);

/* Protects ->fs, ->files, ->mm, and synchronises with wait4().  Nests inside tasklist_lock */
static inline void task_lock(task_t *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(task_t *p)
{
	spin_unlock(&p->alloc_lock);
}

/**
 * get_task_mm - acquire a reference to the task's mm
 *
 * Returns %NULL if the task has no mm. User must release
 * the mm via mmput() after use.
 */
static inline struct mm_struct * get_task_mm(struct task_struct * task)
{
	struct mm_struct * mm;
 
	task_lock(task);
	mm = task->mm;
	if (mm)
		atomic_inc(&mm->mm_users);
	task_unlock(task);

	return mm;
}

/* write full pathname into buffer and return start of pathname */
static inline char * d_path(struct dentry *dentry, struct vfsmount *vfsmnt,
				char *buf, int buflen)
{
	char *res;
	struct vfsmount *rootmnt;
	struct dentry *root;
	read_lock(&current->fs->lock);
	rootmnt = mntget(current->fs->rootmnt);
	root = dget(current->fs->root);
	read_unlock(&current->fs->lock);
	spin_lock(&dcache_lock);
	res = __d_path(dentry, vfsmnt, root, rootmnt, buf, buflen);
	spin_unlock(&dcache_lock);
	dput(root);
	mntput(rootmnt);
	return res;
}

static inline void set_need_resched(void)
{
	current->need_resched = 1;
}

static inline void clear_need_resched(void)
{
	current->need_resched = 0;
}

static inline void set_tsk_need_resched(task_t *tsk)
{
	tsk->need_resched = 1;
}

static inline void clear_tsk_need_resched(task_t *tsk)
{
	tsk->need_resched = 0;
}

static inline int need_resched(void)
{
	return (unlikely(current->need_resched));
}

extern void __cond_resched(void);
static inline void cond_resched(void)
{
	if (need_resched())
		__cond_resched();
}

#endif /* __KERNEL__ */

#endif
