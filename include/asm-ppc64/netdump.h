#ifndef _ASM_PPC64_NETDUMP_H
#define _ASM_PPC64_NETDUMP_H

/*
 * linux/include/asm-ppc64/netdump.h
 *
 * Copyright (c) 2003 Red Hat, Inc. All rights reserved.
 * Copyright (C) 2004 IBM Corporation
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
#include <asm/time.h>

#ifdef __KERNEL__

#ifdef CONFIG_PPC_PSERIES
const static int platform_supports_netdump = 1;
#else
const static int platform_supports_netdump = 0;
#endif /* CONFIG_PPC_PSERIES */


#define platform_machine_type() (EM_PPC64)

extern int page_is_ram (unsigned long);
#define platform_page_is_ram(x) (page_is_ram(x))
#define platform_max_pfn() (num_physpages)

#define platform_timestamp(x) (x = get_tb())

#define platform_freeze_cpu() 			\
{                             			\
	unsigned long sp;			\
	asm("mr %0,1" : "=r" (sp) :);		\
	current->thread.ksp = sp;		\
        for (;;) __cli();     			\
}

#define platform_fix_regs() memcpy(&myregs, regs, sizeof(struct pt_regs))

typedef void (*netdump_func_t)(struct pt_regs *, void *);

static inline void platform_start_netdump(netdump_func_t dumpfunc,
                                          struct pt_regs *regs)
{
	dumpfunc(regs, NULL);
}


static inline unsigned char platform_effective_version(req_t *req)
{
	if (req->from > 0)
		return min_t(unsigned char, req->from, NETDUMP_VERSION_MAX);
	else
		return 0;
}

static inline unsigned int platform_get_regs(char *tmp, struct pt_regs *myregs)
{
        elf_gregset_t elf_regs;
        char *tmp2;

        tmp2 = tmp + sprintf(tmp, "Sending register info.\n");
        ELF_CORE_COPY_REGS(elf_regs, myregs);
        memcpy(tmp2, &elf_regs, sizeof(elf_regs));

        return(strlen(tmp) + sizeof(elf_regs));
}

extern unsigned long next_ram_page(unsigned long);

static inline u32 platform_next_available(unsigned long pfn)
{
        unsigned long pgnum = next_ram_page(pfn);

        if (pgnum < platform_max_pfn()) 
                return (u32)pgnum;

        return 0;
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

#endif /* _ASM_PPC64_NETDUMP_H */
