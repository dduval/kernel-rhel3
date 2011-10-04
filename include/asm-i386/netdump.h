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

const static int platform_supports_netdump = 1;
#define platform_machine_type() (EM_386)

extern int page_is_ram (unsigned long);
#define platform_page_is_ram(x) (page_is_ram(x))
#define platform_max_pfn() (num_physpages)

#define platform_timestamp(x) rdtscll(x)

#define platform_freeze_cpu() \
{                             \
        for (;;) __cli();     \
}

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

typedef void (*netdump_func_t)(struct pt_regs *, void *);

static inline void platform_start_netdump(netdump_func_t dumpfunc,
                                          struct pt_regs *regs)
{
	dumpfunc(regs, NULL);
}


static inline unsigned char platform_effective_version(req_t *req)
{
        if (req->from == 0)
                return NETCONSOLE_VERSION;
        else
                return min_t(unsigned char, req->from, NETDUMP_VERSION_MAX);
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
                *jp = (unsigned long long)(((unsigned long)(t1-t0) * 1000UL)/HZ);
	} else {
		if (!mhz)
			mhz = 1000;
        	*mp = (unsigned long long)mhz * 1000000ULL;
        	*jp = (unsigned long long)mhz * (1000000/HZ);
	}
}

#endif /* __KERNEL__ */

#endif /* _ASM_I386_NETDUMP_H */
