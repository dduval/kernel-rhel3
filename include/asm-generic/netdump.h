#ifndef _ASM_GENERIC_NETDUMP_H_
#define _ASM_GENERIC_NETDUMP_H_

/*
 * linux/include/asm-generic/netdump.h
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
 */

#ifdef __KERNEL__

#warning netdump is not supported on this platform
const static int platform_supports_netdump = 0;

static inline int page_is_ram(unsigned long x) { return 0; }

#define platform_timestamp(x) do { (x) = 0; } while (0)  

#define platform_fix_regs() do { } while (0)

#undef ELF_CORE_COPY_REGS
#define ELF_CORE_COPY_REGS(x, y) do { struct pt_regs *z; z = (y); } while (0)

#define show_mem() do {} while (0)

#define show_state() do {} while (0)

#define show_regs(x) do { struct pt_regs *z; z = (x); } while (0)

#define platform_start_netdump(dumpfunc, regs) do { } while (0)
#define platform_freeze_cpu() do { } while (0)
#define platform_machine_type() (EM_NONE)
#define platform_effective_version(x) (0)
#define platform_max_pfn() (0)
#define platform_get_regs(x,y) (0)
#define platform_page_is_ram(x) (0)
#define platform_next_available(x) ((u32)0)
#define platform_cycles(x,y,z) do { } while (0)

#endif /* __KERNEL__ */

#endif /* _ASM_GENERIC_NETDUMP_H */
