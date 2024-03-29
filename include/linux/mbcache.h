/*
  File: linux/mbcache.h

  (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

/* Hardwire the number of additional indexes */
#define MB_CACHE_INDEXES_COUNT 1

struct mb_cache_entry;

struct mb_cache_op {
	int (*free)(struct mb_cache_entry *, int);
};

struct mb_cache {
	struct list_head		c_cache_list;
	const char			*c_name;
	struct mb_cache_op		c_op;
	atomic_t			c_entry_count;
	int				c_bucket_count;
#ifndef MB_CACHE_INDEXES_COUNT
	int				c_indexes_count;
#endif
	kmem_cache_t			*c_entry_cache;
	struct list_head		*c_block_hash;
	struct list_head		*c_indexes_hash[0];
};

struct mb_cache_entry_index {
	struct list_head		o_list;
	unsigned int			o_key;
};

struct mb_cache_entry {
	struct list_head		e_lru_list;
	struct mb_cache			*e_cache;
	unsigned short			e_used;
	unsigned short			e_queued;
	kdev_t				e_dev;
	unsigned long			e_block;
	struct list_head		e_block_list;
	struct mb_cache_entry_index	e_indexes[0];
};

/* Functions on caches */

struct mb_cache * mb_cache_create(const char *, struct mb_cache_op *, size_t,
				  int, int);
int mb_cache_shrink(struct mb_cache *, kdev_t);
void mb_cache_destroy(struct mb_cache *);

/* Functions on cache entries */

struct mb_cache_entry *mb_cache_entry_alloc(struct mb_cache *);
int mb_cache_entry_insert(struct mb_cache_entry *, kdev_t, unsigned long,
			  unsigned int[]);
void mb_cache_entry_rehash(struct mb_cache_entry *, unsigned int[]);
void mb_cache_entry_release(struct mb_cache_entry *);
void mb_cache_entry_takeout(struct mb_cache_entry *);
void mb_cache_entry_free(struct mb_cache_entry *);
struct mb_cache_entry *mb_cache_entry_get(struct mb_cache *, kdev_t,
					  unsigned long);
#if !defined(MB_CACHE_INDEXES_COUNT) || (MB_CACHE_INDEXES_COUNT > 0)
struct mb_cache_entry *mb_cache_entry_find_first(struct mb_cache *cache, int,
						 kdev_t, unsigned int);
struct mb_cache_entry *mb_cache_entry_find_next(struct mb_cache_entry *, int,
						kdev_t, unsigned int);
#endif
