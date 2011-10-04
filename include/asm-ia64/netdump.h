#ifndef _ASM_IA64_NETDUMP_H_
#define _ASM_IA64_NETDUMP_H_

/*
 * ./include/asm-ia64/netdump.h
 *
 * Copyright (c) 2003, 2004 Red Hat, Inc. All rights reserved.
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

#include <linux/elf.h>
#include <asm/unwind.h>

const static int platform_supports_netdump = 1;

#define platform_machine_type() (EM_IA_64)

extern int page_is_ram (unsigned long);
#define platform_page_is_ram(x) (page_is_ram(x))

#define platform_max_pfn() ((__pa(high_memory)) / PAGE_SIZE)

#define platform_timestamp(x) ({ x = ia64_get_itc(); })

#define platform_fix_regs()                                     \
{                                                               \
        struct unw_frame_info *info = platform_arg;             \
                                                                \
        current->thread.ksp = (__u64)info->sw - 16;             \
        myregs = *regs;                                         \
}

extern void ia64_freeze_cpu(struct unw_frame_info *, void *arg);
static struct switch_stack *sw[NR_CPUS];

#define platform_freeze_cpu()                                   \
{                                                               \
        unw_init_running(ia64_freeze_cpu,                       \
                &sw[smp_processor_id()]);			\
}

extern void ia64_start_dump(struct unw_frame_info *, void *arg);

typedef void (*netdump_func_t)(struct pt_regs *, void *);

static inline void platform_start_netdump(netdump_func_t dumpfunc,
                                          struct pt_regs *regs)
{
        struct dump_call_param param;

        param.func = dumpfunc;
        param.regs = regs;
        unw_init_running(ia64_start_dump, &param);
}

static inline unsigned char platform_effective_version(req_t *req)
{
        if (req->from > 0)
                return min_t(unsigned char, req->from, NETDUMP_VERSION_MAX);
        else
                return 0;
}

extern unsigned long next_ram_page(unsigned long);

static inline u32 platform_next_available(unsigned long pfn)
{
        unsigned long pgnum = next_ram_page(pfn);

        if (pgnum < platform_max_pfn()) 
                return (u32)pgnum;

        return 0;
}

static inline unsigned int platform_get_regs(char *tmp, struct pt_regs *myregs)
{
        char *tmp2;

        tmp2 = tmp + sprintf(tmp, "Sending register info.\n");
        memcpy(tmp2, myregs, sizeof(struct pt_regs));

        return(strlen(tmp) + sizeof(struct pt_regs));
}

static inline void platform_cycles(unsigned int mhz, unsigned long long *jp, unsigned long long *mp)
{
        unsigned long long t0, t1;

        platform_timestamp(t0);
        mdelay(1);
        platform_timestamp(t1);
        if (t1 > t0) {
		*mp = (t1-t0) * 1000ULL;
                *jp = ((t1-t0) * 1000ULL)/HZ;
	} else {
                if (!mhz)
                        mhz = 1000;
                *mp = (unsigned long long)mhz * 1000000ULL;
                *jp = (unsigned long long)mhz * (1000000/HZ);
        }
}

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_NETDUMP_H */
