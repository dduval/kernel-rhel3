/*
 *	Written by Benjamin LaHaise.
 *
 *	Copyright 2000-2001 Red Hat, Inc.
 *
 *	#include "gpl.h"
 *
 *	Basic design idea from Jeff Merkey.
 *	Stack based on ideas from Ingo Molnar.
 */
#ifndef __LINUX__WORKTODO_H
#define __LINUX__WORKTODO_H

#ifndef _LINUX_WAIT_H
#include <linux/wait.h>
#endif
#ifndef _LINUX_TQUEUE_H
#include <linux/tqueue.h>
#endif

struct wtd_stack {
	void	(*fn)(void *data);
	void	*data;
};

struct worktodo {
	wait_queue_t		wait;
	struct tq_struct	tq;

	void			*data;	/* for use by the wtd_ primatives */

	int			sp;
	struct wtd_stack	stack[3];
};

/* FIXME NOTE: factor from kernel/context.c */
#define wtd_init(wtd, routine) do {			\
	INIT_TQUEUE(&(wtd)->tq, (routine), (wtd));	\
	(wtd)->data = 0;				\
	(wtd)->sp = 0;					\
} while (0)

#define wtd_queue(wtd)	schedule_task(&(wtd)->tq)

#define wtd_push(wtd, action, wtddata)			\
do {							\
	(wtd)->stack[(wtd)->sp].fn = (wtd)->tq.routine;	\
	(wtd)->stack[(wtd)->sp++].data = (wtd)->tq.data;\
	(wtd)->tq.routine = action;			\
	(wtd)->tq.data = wtddata;			\
} while (0)

static inline void wtd_pop(struct worktodo *wtd)
{
	if (wtd->sp) {
		wtd->sp--;
		wtd->tq.routine = wtd->stack[wtd->sp].fn;
		wtd->tq.data = wtd->stack[wtd->sp].data;
	}
}

#define wtd_set_action(wtd, action, wtddata)	INIT_TQUEUE(&(wtd)->tq, action, wtddata)

struct page;
struct buffer_head;
struct semaphore;
extern int wtd_lock_page(struct worktodo *wtd, struct page *page);
extern int wtd_wait_on_buffer(struct worktodo *wtd, struct buffer_head *bh);

#if 0	/* not implemented yet */
extern int wtd_down(struct worktodo *wtd, struct semaphore *sem);
extern void wtd_down_write(struct worktodo *wtd, struct rw_semaphore *sem);
extern void wtd_down_read(struct worktodo *wtd, struct rw_semaphore *sem);
#endif

#endif /* __LINUX__WORKTODO_H */
