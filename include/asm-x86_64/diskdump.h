#ifndef _ASM_X86_64_DISKDUMP_H
#define _ASM_X86_64_DISKDUMP_H

/*
 * linux/include/asm-x86_64/diskdump.h
 *
 * Copyright (c) 2004 NEC Corporation
 * Copyright (c) 2004 FUJITSU LIMITED
 * Copyright (c) 2003 Red Hat, Inc. All rights reserved.
 */
/*
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

extern int page_is_ram(unsigned long);

const static int platform_supports_diskdump = 1;

#define platform_fix_regs() \
{                                                                      \
       unsigned long rsp;                                              \
       unsigned short ss;                                              \
                                                                       \
       rsp = (unsigned long) ((char *)regs + sizeof (struct pt_regs)); \
       if (regs->rsp < TASK_SIZE) {                                    \
                rsp = regs->rsp;                                       \
       }                                                               \
       myregs = *regs;                                                 \
       myregs.rsp = rsp;                                               \
                                                                       \
}

struct disk_dump_sub_header {
	elf_gregset_t		elf_regs;
};

#define platform_timestamp(x) rdtscll(x)

#define size_of_sub_header()	((sizeof(struct disk_dump_sub_header) + PAGE_SIZE - 1) / DUMP_BLOCK_SIZE)

#define write_sub_header() \
({								\
 	int ret;						\
	struct disk_dump_sub_header *header;			\
								\
	header = (struct disk_dump_sub_header *)scratch;	\
	ELF_CORE_COPY_REGS(header->elf_regs, (&myregs));	\
	if ((ret = write_blocks(dump_part, 2, scratch, 1)) >= 0)\
		ret = 1; /* size of sub header in page */;	\
	ret;							\
})

#define platform_freeze_cpu() \
{                             \
	for (;;) __cli();     \
}

#define platform_start_diskdump(func, regs)	\
{						\
	func(regs, NULL);			\
}

#endif /* __KERNEL__ */

#endif /* _ASM_X86-64_DISKDUMP_H */
