/*
 * linux/cache_def.h
 * Handling of caches defined in drivers, filesystems, ...
 *
 * Copyright (C) 2002 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

struct cache_definition {
	const char *name;
	int (*shrink)(int, unsigned int);
	struct list_head link;
};

extern void register_cache(struct cache_definition *);
extern void unregister_cache(struct cache_definition *);
extern int shrink_other_caches(unsigned int priority, int gfp_mask);
