#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H

#include <asm/poll.h>

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/worktodo.h>

struct poll_table_page;

/* 
 * structures and helpers for f_op->poll implementations
 */
typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);
struct kiocb;

typedef struct poll_table_struct {
#ifdef CONFIG_EPOLL
	poll_queue_proc		qproc;		/* must be first field */
#endif
	int			error;
	struct poll_table_page	*table;
	struct kiocb		*iocb;		/* iocb for async poll */
} poll_table;

extern void __pollwait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p);

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && wait_address)
		__pollwait(filp, wait_address, p);
}

static inline void poll_initwait(poll_table* pt)
{
#ifdef CONFIG_EPOLL
	pt->qproc = NULL;
#endif
	pt->error = 0;
	pt->table = NULL;
	pt->iocb = NULL;
}

#ifdef CONFIG_EPOLL
static inline void init_poll_funcptr(poll_table *pt, poll_queue_proc qproc)
{
	pt->qproc = qproc;
}
#endif

extern void poll_freewait(poll_table* pt);

/*
 * Scaleable version of the fd_set.
 */

typedef struct {
	unsigned long *in, *out, *ex;
	unsigned long *res_in, *res_out, *res_ex;
} fd_set_bits;

/*
 * How many longwords for "nr" bits?
 */
#define FDS_BITPERLONG	(8*sizeof(long))
#define FDS_LONGS(nr)	(((nr)+FDS_BITPERLONG-1)/FDS_BITPERLONG)
#define FDS_BYTES(nr)	(FDS_LONGS(nr)*sizeof(long))

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 *
 * Use "unsigned long" accesses to let user-mode fd_set's be long-aligned.
 */
static inline
int get_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	nr = FDS_BYTES(nr);
	if (ufdset) {
		int error;
		error = verify_area(VERIFY_WRITE, ufdset, nr);
		if (!error && __copy_from_user(fdset, ufdset, nr))
			error = -EFAULT;
		return error;
	}
	memset(fdset, 0, nr);
	return 0;
}

static inline
void set_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	if (ufdset)
		__copy_to_user(ufdset, fdset, FDS_BYTES(nr));
}

static inline
void zero_fd_set(unsigned long nr, unsigned long *fdset)
{
	memset(fdset, 0, FDS_BYTES(nr));
}

extern int do_select(int n, fd_set_bits *fds, long *timeout);

#endif /* KERNEL */

#endif /* _LINUX_POLL_H */
