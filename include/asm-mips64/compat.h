#ifndef _ASM_MIPS64_COMPAT_H
#define _ASM_MIPS64_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_suseconds_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

#endif /* _ASM_MIPS64_COMPAT_H */
