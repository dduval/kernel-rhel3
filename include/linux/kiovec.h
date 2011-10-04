#ifndef __LINUX__KIOVEC_H
#define __LINUX__KIOVEC_H

struct page;
#include <linux/list.h>

struct kveclet {
	struct page	*page;
	unsigned	offset;
	unsigned	length;
};

struct kvec {
	unsigned	max_nr;
	unsigned	nr;
	struct kveclet	veclet[0];
};

struct kvec_cb {
	struct kvec	*vec;
	void		(*fn)(void *data, struct kvec *vec, ssize_t res);
	void		*data;
};

struct kvec_cb_list {
	struct list_head	list;
	struct kvec_cb		cb;
};

#ifndef _LINUX_TYPES_H
#include <linux/types.h>
#endif
#ifndef _LINUX_KDEV_T_H
#include <linux/kdev_t.h>
#endif
#ifndef _ASM_KMAP_TYPES_H
#include <asm/kmap_types.h>
#endif

extern struct kvec *FASTCALL(map_user_kvec(int rw, unsigned long va, size_t len));
extern struct kvec *FASTCALL(mm_map_user_kvec(struct mm_struct *, int rw,
				     unsigned long va, size_t len));
extern void FASTCALL(unmap_kvec(struct kvec *, int dirtied));
extern void FASTCALL(free_kvec(struct kvec *));

/* brw_kvec_async:
 *	Performs direct io to/from disk into cb.vec.  Count is the number
 *	of sectors to read, sector_shift is the blocksize (which must be
 *	compatible with the kernel's current idea of the device's sector
 *	size) in log2.  blknr is the starting sector offset on dev.
 *
 */
extern int brw_kvec_async(int rw, kvec_cb_t cb, kdev_t dev, unsigned count,
			  unsigned long blknr, int sector_shift);

/* Memory copy helpers usage:
 * void foo(... struct kveclet *veclet...)
 *
 *	struct kvec_dst	dst;
 *
 *	kvec_dst_init(&dst, KM_USER0);			-- resets type
 *	kvec_dst_set(&dst, veclet);			-- set target & clear offset
 *	kvec_dst_map(&dst);				-- activates kmap
 *	for (...)
 *		memcpy_to_kvec_dst(&dst, data, size);	-- each copy appends
 *	kvec_dst_unmap(&dst);				-- releases kmap
 *
 * Note that scheduling is not permitted between kvec_dst_map() and
 * kvec_dst_unmap().  This is because internally the routines make use
 * of an atomic kmap.
 */
struct kvec_dst {
	char		*addr;
	char		*dst;
	struct kveclet	*let;
	int		space;
	int		offset;
	enum km_type	type;
};


#define kvec_dst_set(Xdst, Xlet)					\
	do {								\
		struct kvec_dst *_dst = (Xdst);				\
		struct kveclet *_let = (Xlet);				\
		_dst->let = _let;					\
		_dst->space = _let->length;				\
		_dst->offset = 0;					\
	} while(0)

#define kvec_dst_map(Xdst)						\
	do {								\
		struct kvec_dst *_dst = (Xdst);				\
		struct kveclet *_let = _dst->let;			\
		_dst->dst = _dst->addr = kmap_atomic(_let->page, _dst->type);\
		_dst->dst += _let->offset + _dst->offset;		\
		_dst->space = _let->length - _dst->offset;		\
		_dst->offset = 0;					\
	} while(0)

#define kvec_dst_init(Xdst, Xtype)					\
	do {								\
		(Xdst)->space = 0;					\
		(Xdst)->addr = 0;					\
		(Xdst)->offset = 0;					\
		(Xdst)->type = Xtype;					\
	} while(0)

#define	kvec_dst_unmap(Xdst)						\
	do {								\
		struct kvec_dst *_dst = (Xdst);				\
		kunmap_atomic(_dst->addr, _dst->type);			\
		_dst->offset = _dst->dst - _dst->addr;			\
		_dst->offset -= _dst->let->offset;			\
		_dst->addr = NULL;					\
	} while(0)

extern void FASTCALL(memcpy_to_kvec_dst(struct kvec_dst *dst,
					const char *from, long len));
extern void FASTCALL(memcpy_from_kvec_dst(char *to,
					  struct kvec_dst *from, long len));
extern int FASTCALL(copy_user_to_kvec(struct kvec *to, size_t offset, const char *from, size_t len));


#endif /* __LINUX__KIOVEC_H */
