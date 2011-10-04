#ifndef _ASM_I386_NETDUMP_H
#define _ASM_I386_NETDUMP_H

/*
 * linux/include/asm-i386/netdump.h
 *
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

extern int page_is_ram (unsigned long);
const static int platform_supports_netdump = 1;

#define platform_timestamp(x) rdtscll(x)

#define platform_fix_regs() \
{                                                                      \
       unsigned long esp;                                              \
       unsigned short ss;                                              \
       esp = (unsigned long) ((char *)regs + sizeof (struct pt_regs)); \
       ss = __KERNEL_DS;                                               \
       if (regs->xcs & 3) {                                            \
               esp = regs->esp;                                        \
               ss = regs->xss & 0xffff;                                \
       }                                                               \
       myregs = *regs;                                                 \
       myregs.esp = esp;                                               \
       myregs.xss = (myregs.xss & 0xffff0000) | ss;                    \
}

#endif /* __KERNEL__ */

#endif /* _ASM_I386_NETDUMP_H */
