#ifndef _ASM_IA64_DISKDUMP_H
#define _ASM_IA64_DISKDUMP_H

/*
 * linux/include/asm-ia64/diskdump.h
 *
 * Copyright (c) 2004 FUJITSU LIMITED
 * Copyright (c) 2003 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef __KERNEL__

#include <linux/elf.h>
#include <asm/unwind.h>

extern void ia64_do_copy_regs(struct unw_frame_info *, void *arg);
extern void ia64_freeze_cpu(struct unw_frame_info *, void *arg);
extern void ia64_start_dump(struct unw_frame_info *, void *arg);
extern int page_is_ram(unsigned long);

const static int platform_supports_diskdump = 1;

#define platform_fix_regs()					\
{								\
	struct unw_frame_info *info = platform_arg;		\
								\
	current->thread.ksp = (__u64)info->sw - 16;		\
	myregs = *regs;						\
}

struct disk_dump_sub_header {
	elf_gregset_t		 elf_regs;
	struct switch_stack	*sw[NR_CPUS];
};

#define platform_timestamp(x) ({ x = ia64_get_itc(); })

#define size_of_sub_header()	((sizeof(struct disk_dump_sub_header) + PAGE_SIZE - 1) / DUMP_BLOCK_SIZE)

#define write_sub_header() \
({									\
 	int ret;							\
	struct unw_frame_info *info = platform_arg;			\
	extern int init_dump;						\
									\
	if (!init_dump)							\
		ia64_do_copy_regs(info, &dump_sub_header.elf_regs);	\
	dump_sub_header.sw[smp_processor_id()] = info->sw;		\
	clear_page(scratch);						\
	memcpy(scratch, &dump_sub_header, sizeof(dump_sub_header));	\
 									\
	if ((ret = write_blocks(dump_part, 2, scratch, 1)) >= 0)	\
		ret = 1; /* size of sub header in page */;		\
	ret;								\
})

#define platform_freeze_cpu() 					\
{								\
	unw_init_running(ia64_freeze_cpu,			\
		&dump_sub_header.sw[smp_processor_id()]);	\
}

#define platform_start_diskdump(func, regs)			\
{								\
	struct dump_call_param param;				\
								\
	param.func = func;					\
	param.regs = regs;					\
	unw_init_running(ia64_start_dump, &param);		\
}

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_DISKDUMP_H */
