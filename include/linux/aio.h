#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/tqueue.h>
#include <linux/kiovec.h>
#include <linux/list.h>
#include <asm/atomic.h>

#include <linux/aio_abi.h>

#define AIO_MAXSEGS		4
#define AIO_KIOGRP_NR_ATOMIC	8

struct kioctx;

/* Notes on cancelling a kiocb:
 *	If a kiocb is cancelled, aio_complete may return 0 to indicate 
 *	that cancel has not yet disposed of the kiocb.  All cancel 
 *	operations *must* call aio_put_req to dispose of the kiocb 
 *	to guard against races with the completion code.
 */
#define KIOCB_C_CANCELLED	0x01
#define KIOCB_C_COMPLETE	0x02

struct kiocb {
	struct list_head	list;
	struct file	*filp;
	struct kioctx	*ctx;
	void		*user_obj;
	__u64		user_data;
	loff_t		pos;
	unsigned long	buf;
	size_t		nr_transferred;	/* used for chunking */
	size_t		size;
	size_t		this_size;
	unsigned	key;		/* id of this request */
	int		(*cancel)(struct kiocb *kiocb, struct io_event *ev);
	void		*data;		/* for use by the the async op */
	int		users;
	union {
		struct tq_struct	tq;	/* argh. */
		struct list_head	list;
	} u;
	unsigned long		rlim_fsize;
};

#define AIO_RING_MAGIC			0xa10a10a1
#define AIO_RING_COMPAT_FEATURES	1
#define AIO_RING_INCOMPAT_FEATURES	0

struct aio_ring {
	unsigned	id;	/* kernel internal index number */
	unsigned	nr;	/* number of io_events */
	unsigned	head;
	unsigned	tail;

	unsigned	magic;
	unsigned	compat_features;
	unsigned	incompat_features;
	unsigned	header_length;	/* size of aio_ring */
	unsigned	padding[24];	/* pad up to 128 bytes */


	struct io_event		io_events[0];
}; /* total ring size - header_length */

#define aio_ring_avail(info, ring)	(((ring)->head + (info)->nr - 1 - (ring)->tail) % (info)->nr)

#define AIO_RING_PAGES	8
struct aio_ring_info {
	//struct file		*mmap_file;
	struct kvec		*kvec;
	unsigned long		mmap_base;
	unsigned long		mmap_size;

	struct page		**ring_pages;
	spinlock_t		ring_lock;
	unsigned		nr_pages;

	unsigned		nr, tail;

	struct page		*internal_pages[AIO_RING_PAGES];
};

struct kioctx {
	atomic_t		users;
	int			dead;
	struct mm_struct	*mm;

	/* This needs improving */
	unsigned long		user_id;
	struct kioctx		*next;

	wait_queue_head_t	wait;

	spinlock_t		lock;

	int			reqs_active;
	struct list_head	free_reqs;
	struct list_head	active_reqs;	/* used for cancellation */

	unsigned		max_reqs;

	struct aio_ring_info	ring_info;
};

/* prototypes */
extern unsigned aio_max_size;

extern int FASTCALL(aio_put_req(struct kiocb *iocb));
extern int FASTCALL(aio_complete(struct kiocb *iocb, long res, long res2));
extern void FASTCALL(__put_ioctx(struct kioctx *ctx));
struct mm_struct;
extern void FASTCALL(exit_aio(struct mm_struct *mm));

#define get_ioctx(kioctx)	do { if (unlikely(atomic_read(&(kioctx)->users) <= 0)) BUG(); atomic_inc(&(kioctx)->users); } while (0)
#define put_ioctx(kioctx)	do { if (unlikely(atomic_dec_and_test(&(kioctx)->users))) __put_ioctx(kioctx); else if (unlikely(atomic_read(&(kioctx)->users) < 0)) BUG(); } while (0)

#include <linux/aio_abi.h>

static inline struct kiocb *list_kiocb(struct list_head *h)
{
	return list_entry(h, struct kiocb, list);
}

struct file;
extern ssize_t generic_aio_poll(struct file *file, struct kiocb *req, struct iocb *iocb);
extern ssize_t generic_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb, size_t min_size);
extern ssize_t generic_aio_write(struct file *file, struct kiocb *req, struct iocb *iocb, size_t min_size);
extern ssize_t generic_file_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb);
extern ssize_t generic_file_aio_write(struct file *file, struct kiocb *req, struct iocb *iocb);
extern ssize_t generic_sock_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb);

/* for sysctl: */
extern unsigned aio_nr, aio_max_nr, aio_max_size, aio_max_pinned;

#endif /* __LINUX__AIO_H */
