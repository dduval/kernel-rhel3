/*
 * This is where we statically allocate and initialize the initial
 * task.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

/*
 * Initial task structure.
 *
 * We need to make sure that this is page aligned due to the way
 * process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */


/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_IA64_TASK(tsk)	\
{									\
    state:              0,                                              \
    flags:              0,                                              \
    sigpending:         0,                                              \
    addr_limit:         KERNEL_DS,                                      \
    exec_domain:        &default_exec_domain,                           \
    lock_depth:         -1,                                             \
    prio:               MAX_PRIO-20,                                    \
    static_prio:        MAX_PRIO-20,                                    \
    policy:             SCHED_NORMAL,                                   \
    cpus_allowed:       ~0UL,                                           \
    mm:                 NULL,                                           \
    active_mm:          &init_mm,                                       \
    run_list:           LIST_HEAD_INIT(tsk.run_list),                   \
    time_slice:         HZ,                                             \
    tasks:              LIST_HEAD_INIT(tsk.tasks),                      \
    ptrace_children:    LIST_HEAD_INIT(tsk.ptrace_children),            \
    ptrace_list:        LIST_HEAD_INIT(tsk.ptrace_list),                \
    real_parent:        &tsk,                                           \
    parent:             &tsk,                                           \
    children:           LIST_HEAD_INIT(tsk.children),                   \
    sibling:            LIST_HEAD_INIT(tsk.sibling),                    \
    group_leader:       &tsk,                                           \
    wait_chldexit:      __WAIT_QUEUE_HEAD_INITIALIZER(tsk.wait_chldexit),\
    real_timer:         {                                               \
        function:               it_real_fn                              \
    },                                                                  \
    cap_effective:      CAP_INIT_EFF_SET,                               \
    cap_inheritable:    CAP_INIT_INH_SET,                               \
    cap_permitted:      CAP_FULL_SET,                                   \
   keep_capabilities:  0,                                              \
    rlim:               INIT_RLIMITS,                                   \
    user:               INIT_USER,                                      \
    comm:               "swapper",                                      \
    thread:             INIT_THREAD,                                    \
    fs:                 &init_fs,                                       \
    files:              &init_files,                                    \
    signal:             &init_signals,                                  \
    sighand:            &init_sighand,                                  \
    pending:            { NULL, &tsk.pending.head, {{0}}},              \
    blocked:            {{0}},                                          \
    alloc_lock:         SPIN_LOCK_UNLOCKED,                             \
    switch_lock:        SPIN_LOCK_UNLOCKED,                             \
    journal_info:       NULL                                            \
}

union task_union init_task_union
	__attribute__((section("init_task"))) =
		{ INIT_IA64_TASK(init_task_union.task) };


