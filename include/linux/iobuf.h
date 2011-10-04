/*
 * iobuf.h
 *
 * Defines the structures used to track abstract kernel-space io buffers.
 *
 */

#ifndef __LINUX_IOBUF_H
#define __LINUX_IOBUF_H

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <asm/atomic.h>

/*
 * The kiobuf structure describes a physical set of pages reserved
 * locked for IO.  The reference counts on each page will have been
 * incremented, and the flags field will indicate whether or not we have
 * pre-locked all of the pages for IO.
 *
 * kiobufs may be passed in arrays to form a kiovec, but we must
 * preserve the property that no page is present more than once over the
 * entire iovec.
 */

#define KIO_MAX_ATOMIC_IO	512 /* in kb */
#define KIO_STATIC_PAGES	4
#define KIO_MAX_SECTORS		(KIO_MAX_ATOMIC_IO * 2)

/* The main kiobuf struct used for all our IO! */

struct kiobuf 
{
	int		nr_pages;	/* Pages actually referenced */
	int		array_len;	/* Space in the allocated map lists */
	int		bh_len;		/* Nr of bhes currently allocated */
	int		offset;		/* Offset to start of valid data */
	int		length;		/* Number of valid bytes of data */

	unsigned int	locked : 1,	/* If set, pages has been locked */
			initialized:1,  /* If set, done initialize */
			varyio : 1;	/* If set, do variable size IO */

	struct page **  maplist;
	struct buffer_head ** bh;
	unsigned long * blocks;

	/* Dynamic state for IO completion: */
	atomic_t	io_count;	/* IOs still in progress */
	int		transferred;	/* Number of bytes of completed IO at the beginning of the buffer */
	int		errno;		/* Status of completed IO */
	void		(*end_io) (struct kiobuf *); /* Completion callback */
	void		*end_io_data;
	wait_queue_head_t wait_queue;
};


/* mm/memory.c */

int	map_user_kiobuf(int rw, struct kiobuf *, unsigned long va, size_t len);
void	unmap_kiobuf(struct kiobuf *iobuf);
int	lock_kiovec(int nr, struct kiobuf *iovec[], int wait);
int	unlock_kiovec(int nr, struct kiobuf *iovec[]);
void	mark_dirty_kiobuf(struct kiobuf *iobuf, int bytes);

/* fs/iobuf.c */

void	end_kio_request(struct kiobuf *, int);
void	simple_wakeup_kiobuf(struct kiobuf *);
int	alloc_kiovec(int nr, struct kiobuf **);
void	free_kiovec(int nr, struct kiobuf **);
int	expand_kiobuf(struct kiobuf *, int);
void	kiobuf_wait_for_io(struct kiobuf *);
extern int alloc_kiobuf_bhs(struct kiobuf *, int nr, int gfp_mask);
extern void free_kiobuf_bhs(struct kiobuf *);
extern kmem_cache_t *kiobuf_cachep;

/* fs/buffer.c */

int	brw_kiovec_async(int rw, int nr, struct kiobuf *iovec[], 
		   kdev_t dev, int nr_blocks, unsigned long b[], int size);
int	brw_kiovec(int rw, int nr, struct kiobuf *iovec[], 
		   kdev_t dev, unsigned long b[], int size);

#endif /* __LINUX_IOBUF_H */
