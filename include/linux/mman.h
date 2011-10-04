#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

#include <asm/mman.h>

#define MREMAP_MAYMOVE	1
#define MREMAP_FIXED	2

extern int vm_enough_memory(long pages);
extern void vm_unacct_memory(long pages);
extern void vm_validate_enough(char *x);
extern atomic_t vm_committed_space;
extern int sysctl_overcommit_ratio;

#ifndef MAP_POPULATE
# define MAP_POPULATE 0x8000
#endif

#ifndef MAP_NONBLOCK
# define MAP_NONBLOCK 0x10000
#endif

#endif /* _LINUX_MMAN_H */
