/*
   CIPE - encrypted IP over UDP tunneling

   module.c - kernel module interface stuff

   Copyright 1996 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#include "cipe.h"
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/config.h>
/* We put this all here so that none of the other source files needs
   to include <linux/module.h>, which could lead to collisions. */

#ifdef LINUX_21
MODULE_AUTHOR("Olaf Titz <olaf@bigred.inka.de>");
MODULE_DESCRIPTION("Encrypting IP-over-UDP tunnel");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE(DEVNAME);
MODULE_PARM(cipe_maxdev,"i");
MODULE_PARM_DESC(cipe_maxdev,"Maximum device number supported");
#ifdef DEBUG
MODULE_PARM(cipe_debug,"i");
MODULE_PARM_DESC(cipe_debug,"Debugging level");
#endif
#endif

void cipe_use_module(void)
{
    MOD_INC_USE_COUNT;
}

void cipe_unuse_module(void)
{
    MOD_DEC_USE_COUNT;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,0,30)
/* older kernel not always exported this */

int bad_user_access_length(void)
{
    panic("bad_user_access_length in " DEVNAME);
}

#endif

/* HACK: sanity check on SMP/non-SMP.
   Is this really necessary? */
int cipe_check_kernel(void)
{
    int s=0;
    const char *p=system_utsname.version;
    while (p[0] && p[1] && p[2]) {
	if (p[0]=='S' && p[1]=='M' && p[2]=='P') {
	    s=1;
	    break;
	}
	++p;
    }
    if (
#ifdef CONFIG_SMP
	!
#endif
         s) {
	printk(KERN_ERR
	       DEVNAME ": driver ("
#ifndef CONFIG_SMP
	       "not "
#endif
	       "SMP) "
	       "mismatches kernel ("
#ifdef CONFIG_SMP
	       "not "
#endif
	       "SMP)\n");
	return -EINVAL;
    }
    return 0;
}
