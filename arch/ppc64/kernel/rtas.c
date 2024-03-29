/*
 *
 * Procedures for interfacing to the RTAS on CHRP machines.
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <asm/init.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/semaphore.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/abs_addr.h>
#include <asm/udbg.h>
#include <asm/uaccess.h>

struct proc_dir_entry *rtas_proc_dir;	/* /proc/ppc64/rtas dir */
struct flash_block_list_header rtas_firmware_flash_list = {0, 0};
struct errinjct_token ei_token_list[MAX_ERRINJCT_TOKENS];

extern int slabpages; /* from mm/slab.c to see if slab is up & running */

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the address
 * we're currently running at minus the address we were linked at.
 * (Note that strings count as static variables.)
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls should be done within prom_init(), and prom_init()
 * and all routines called within it must be careful to relocate
 * references as necessary.
 *
 * Note that the bss is cleared *after* prom_init runs, so we have
 * to make sure that any static or extern variables it accesses
 * are put in the data segment.
 */

struct rtas_t rtas = {
	.lock = SPIN_LOCK_UNLOCKED
};

char rtas_err_buf[RTAS_DATA_BUF_SIZE];

extern unsigned long reloc_offset(void);

spinlock_t rtas_data_buf_lock = SPIN_LOCK_UNLOCKED;
char rtas_data_buf[RTAS_DATA_BUF_SIZE]__page_aligned;

void
phys_call_rtas(int token, int nargs, int nret, ...)
{
	va_list list;
	unsigned long offset = reloc_offset();
	struct rtas_args *rtas = PTRRELOC(&(get_paca()->xRtas));
	int i;

	rtas->token = token;
	rtas->nargs = nargs;
	rtas->nret  = nret;
	rtas->rets  = (rtas_arg_t *)PTRRELOC(&(rtas->args[nargs]));

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
	  rtas->args[i] = (rtas_arg_t)LONG_LSW(va_arg(list, ulong));
	va_end(list);

	enter_rtas(rtas);	
}

void
phys_call_rtas_display_status(char c)
{
	unsigned long offset = reloc_offset();
	struct rtas_args *rtas = PTRRELOC(&(get_paca()->xRtas));

	rtas->token = 10;
	rtas->nargs = 1;
	rtas->nret  = 1;
	rtas->rets  = (rtas_arg_t *)PTRRELOC(&(rtas->args[1]));
	rtas->args[0] = (int)c;

	enter_rtas(rtas);	
}

void
call_rtas_display_status(char c)
{
	struct rtas_args *rargs = &(get_paca()->xRtas);
	unsigned long flags;

	spin_lock_irqsave(&rtas.lock, flags);

	rargs->token = 10;
	rargs->nargs = 1;
	rargs->nret  = 1;
	rargs->rets  = (rtas_arg_t *)(&(rargs->args[1]));
	rargs->args[0] = (int)c;

	enter_rtas((void *)__pa((unsigned long)rargs));
	spin_unlock_irqrestore(&rtas.lock, flags);
}

__openfirmware
int
rtas_token(const char *service)
{
	int *tokp;
	if (rtas.dev == NULL) {
		PPCDBG(PPCDBG_RTAS,"\tNo rtas device in device-tree...\n");
		return RTAS_UNKNOWN_SERVICE;
	}
	tokp = (int *) get_property(rtas.dev, service, NULL);
	return tokp ? *tokp : RTAS_UNKNOWN_SERVICE;
}

/** Return a copy of the detailed error text associated with the
 *  most recent failed call to rtas.  Because the error text
 *  might go stale if there are any other intervening rtas calls,
 *  this routine must be called atomically with whatever produced
 *  the error (i.e. with rtas.lock still held from the previous call).
 */
static int
__fetch_rtas_last_error(void)
{
	struct rtas_args err_args, save_args;
	u32 bufsz;

	bufsz = rtas_token ("rtas-error-log-max");
	if ((bufsz == RTAS_UNKNOWN_SERVICE) ||
	    (bufsz > RTAS_ERROR_LOG_MAX)) {
		printk (KERN_WARNING "RTAS: bad log buffer size %d\n", bufsz);
		bufsz = RTAS_ERROR_LOG_MAX;
	}

	err_args.token = rtas_token("rtas-last-error");
	err_args.nargs = 2;
	err_args.nret = 1;

	err_args.args[0] = (rtas_arg_t)__pa(rtas_err_buf);
	err_args.args[1] = bufsz;
	err_args.args[2] = 0;

	save_args = rtas.args;
	rtas.args = err_args;
	rtas.args.rets = (rtas_arg_t *)&(rtas.args.args[2]);

	PPCDBG(PPCDBG_RTAS, "\tentering rtas with 0x%lx\n",
	       __pa(&err_args));
	enter_rtas((void *)__pa((unsigned long)(&rtas.args)));
	PPCDBG(PPCDBG_RTAS, "\treturned from rtas ...\n");

	err_args = rtas.args;
	rtas.args = save_args;

	err_args.rets = (rtas_arg_t *)&(err_args.args[2]);
	return err_args.rets[0];
}


__openfirmware
long
rtas_call(int token, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i, logit = 0;
	unsigned long flags;
	struct rtas_args *rtas_args;
	char * buff_copy = NULL;
	unsigned long retval;

	PPCDBG(PPCDBG_RTAS, "Entering rtas_call\n");
	PPCDBG(PPCDBG_RTAS, "\ttoken    = 0x%x\n", token);
	PPCDBG(PPCDBG_RTAS, "\tnargs    = %d\n", nargs);
	PPCDBG(PPCDBG_RTAS, "\tnret     = %d\n", nret);
	PPCDBG(PPCDBG_RTAS, "\t&outputs = 0x%lx\n", outputs);
	if (token == RTAS_UNKNOWN_SERVICE)
		return -1;

	spin_lock_irqsave(&rtas.lock, flags);
	rtas_args = &(get_paca()->xRtas);

	rtas_args->token = token;
	rtas_args->nargs = nargs;
	rtas_args->nret  = nret;
	rtas_args->rets  = (rtas_arg_t *)&(rtas_args->args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i) {
		rtas_args->args[i] = (rtas_arg_t)LONG_LSW(va_arg(list, ulong));
		PPCDBG(PPCDBG_RTAS, "\tnarg[%d] = 0x%lx\n", i, rtas_args->args[i]);
	}
	va_end(list);

	for (i = 0; i < nret; ++i)
	  rtas_args->rets[i] = 0;

	PPCDBG(PPCDBG_RTAS, "\tentering rtas with 0x%lx\n",
		(void *)__pa((unsigned long)rtas_args));
	enter_rtas((void *)__pa((unsigned long)rtas_args));
	PPCDBG(PPCDBG_RTAS, "\treturned from rtas ...\n");

	/* A -1 return code indicates that the last command couldn't
	 * be completed due to a hardware error. */
	if (rtas_args->rets[0] == -1)
		logit = (__fetch_rtas_last_error() == 0);

	ifppcdebug(PPCDBG_RTAS) {
		for(i=0; i < nret ;i++)
			udbg_printf("\tnret[%d] = 0x%lx\n", i, (ulong)rtas_args->rets[i]);
	}

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = rtas_args->rets[i+1];
	retval = (ulong)((nret > 0) ? rtas_args->rets[0] : 0);

	/* Log the error in the unlikely case that there was one. */
	if (unlikely(logit)) {
		/* Can't call kmalloc if VM subsystem is not yet up. */
		if (slabpages>0) {
			buff_copy = kmalloc(RTAS_ERROR_LOG_MAX, GFP_ATOMIC);
			if (buff_copy) {
				memcpy(buff_copy, rtas_err_buf, RTAS_ERROR_LOG_MAX);
			}
		}
	}

	spin_unlock_irqrestore(&rtas.lock, flags);

	if (buff_copy) {
		log_error(buff_copy, ERR_TYPE_RTAS_LOG, 0);
		kfree(buff_copy);
	}
	return retval;
}

/* Given an RTAS status code of 990n perform the hinted delay of 10^n
 * (last digit) milliseconds.  For now we bound at n=5 (100 secs). 
 */
int
rtas_do_extended_delay(int status)
{
	int order = status - 9900;
	unsigned long ms;
	unsigned long jiffies;

	if (order < 0)
		order = 0;	/* RTC depends on this for -2 clock busy */
	else if (order > 5)
		order = 5;	/* bound */

	/* Use microseconds for reasonable accuracy */
	for (ms=1; order > 0; order--)
		ms *= 10;       

	jiffies = (ms * HZ) / 1000;
	
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(jiffies);
	if (signal_pending(current))
		return RTAS_DELAY_INTR;

	return 0;
}

#define FLASH_BLOCK_LIST_VERSION (1UL)
static void
rtas_flash_firmware(void)
{
	unsigned long image_size;
	struct flash_block_list *f, *next, *flist;
	unsigned long rtas_block_list;
	int i, status, update_token;

	update_token = rtas_token("ibm,update-flash-64-and-reboot");
	if (update_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_ALERT "FLASH: ibm,update-flash-64-and-reboot is not available -- not a service partition?\n");
		printk(KERN_ALERT "FLASH: firmware will not be flashed\n");
		return;
	}

	/* NOTE: the "first" block list is a global var with no data
	 * blocks in the kernel data segment.  We do this because
	 * we want to ensure this block_list addr is under 4GB.
	 */
	rtas_firmware_flash_list.num_blocks = 0;
	flist = (struct flash_block_list *)&rtas_firmware_flash_list;
	rtas_block_list = virt_to_absolute((unsigned long)flist);
	if (rtas_block_list >= (4UL << 20)) {
		printk(KERN_ALERT "FLASH: kernel bug...flash list header addr above 4GB\n");
		return;
	}

	printk(KERN_ALERT "FLASH: preparing saved firmware image for flash\n");
	/* Update the block_list in place. */
	image_size = 0;
	for (f = flist; f; f = next) {
		/* Translate data addrs to absolute */
		for (i = 0; i < f->num_blocks; i++) {
			f->blocks[i].data = (char *)virt_to_absolute((unsigned long)f->blocks[i].data);
			image_size += f->blocks[i].length;
		}
		next = f->next;
		/* Don't translate final NULL pointer */
		if (next)
			f->next = (struct flash_block_list *)virt_to_absolute((unsigned long)next);

		/* make num_blocks into the version/length field */
		f->num_blocks = (FLASH_BLOCK_LIST_VERSION << 56) | ((f->num_blocks+1)*16);
	}

	printk(KERN_ALERT "FLASH: flash image is %ld bytes\n", image_size);
	printk(KERN_ALERT "FLASH: performing flash and reboot\n");
	ppc_md.progress("Flashing        \n", 0x0);
	ppc_md.progress("Please Wait...  ", 0x0);
	printk(KERN_ALERT "FLASH: this will take several minutes.  Do not power off!\n");
	status = rtas_call(update_token, 1, 1, NULL, rtas_block_list);
	switch (status) {	/* should only get "bad" status */
	    case 0:
		printk(KERN_ALERT "FLASH: success\n");
		break;
	    case -1:
		printk(KERN_ALERT "FLASH: hardware error.  Firmware may not be not flashed\n");
		break;
	    case -3:
		printk(KERN_ALERT "FLASH: image is corrupt or not correct for this platform.  Firmware not flashed\n");
		break;
	    case -4:
		printk(KERN_ALERT "FLASH: flash failed when partially complete.  System may not reboot\n");
		break;
	    default:
		printk(KERN_ALERT "FLASH: unknown flash return code %d\n", status);
		break;
	}
}

void rtas_flash_bypass_warning(void)
{
	printk(KERN_ALERT "FLASH: firmware flash requires a reboot\n");
	printk(KERN_ALERT "FLASH: the firmware image will NOT be flashed\n");
}


void __chrp
rtas_restart(char *cmd)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_firmware();

        printk("RTAS system-reboot returned %ld\n",
	       rtas_call(rtas_token("system-reboot"), 0, 1, NULL));
        for (;;);
}

void __chrp
rtas_power_off(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
        /* allow power on only with power button press */
        printk("RTAS power-off returned %ld\n",
               rtas_call(rtas_token("power-off"), 2, 1, NULL,0xffffffff,0xffffffff));
        for (;;);
}

void __chrp
rtas_halt(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
        rtas_power_off();
}

int
rtas_errinjct_open(void)
{
	u32 ret[2];
	int open_token;
	int rc;

	/* The rc and open_token values are backwards due to a misprint in
	 * the RPA */ 
	open_token = rtas_call(rtas_token("ibm,open-errinjct"), 0, 2, (void *) &ret);
	rc = ret[0];

	if (rc < 0) {
		printk(KERN_WARNING "error: ibm,open-errinjct failed (%d)\n", rc);
		return rc;
	}

	return open_token;
}

int
rtas_errinjct(unsigned int open_token, char * ei_token, char * in_workspace)
{
	struct errinjct_token * ei;
	int rtas_ei_token = -1;
	int rc;
	int i;

	ei = ei_token_list;
	for (i = 0; i < MAX_ERRINJCT_TOKENS && ei->name; i++) {
		if (strcmp(ei_token, ei->name) == 0) {
			rtas_ei_token = ei->value;
			break;
		}
		ei++;
	}
	if (rtas_ei_token == -1) {
		return -EINVAL;
	}

	spin_lock(&rtas_data_buf_lock);

	if (in_workspace) 
		memcpy(rtas_data_buf, in_workspace, RTAS_DATA_BUF_SIZE);

	rc = rtas_call(rtas_token("ibm,errinjct"), 3, 1, NULL, rtas_ei_token,
		       open_token, __pa(rtas_data_buf));   

	spin_unlock(&rtas_data_buf_lock);

	return rc;
}

int
rtas_errinjct_close(unsigned int open_token)
{
	int rc;

	rc = rtas_call(rtas_token("ibm,close-errinjct"), 1, 1, NULL, open_token);
	if (rc != 0) {
		printk(KERN_WARNING "error: ibm,close-errinjct failed (%d)\n", rc);
		return rc;
	}

	return 0;
}

unsigned long rtas_rmo_buf = 0;

asmlinkage int ppc64_rtas(struct rtas_args *uargs)
{
	struct rtas_args args;
	unsigned long flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&args, uargs, 3 * sizeof(u32)) != 0)
		return -EFAULT;

	if (args.nargs > ARRAY_SIZE(args.args)
	    || args.nret > ARRAY_SIZE(args.args)
	    || args.nargs + args.nret > ARRAY_SIZE(args.args))
		return -EINVAL;

	/* Copy in args. */
	if (copy_from_user(args.args, uargs->args,
			   args.nargs * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	spin_lock_irqsave(&rtas.lock, flags);
	get_paca()->xRtas = args;
	enter_rtas((void *)__pa((unsigned long)&get_paca()->xRtas));
	args = get_paca()->xRtas;
	spin_unlock_irqrestore(&rtas.lock, flags);

	/* Copy out args. */
	if (copy_to_user(uargs->args + args.nargs,
			 args.args + args.nargs,
			 args.nret * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	return 0;
}


#ifdef CONFIG_PPC_PSERIES
static int __init rtas_errinjct_init(void)
{
	char * token_array;
	char * end_array;
	int array_len = 0;
	int len;
	int i, j;

	token_array = (char *) get_property(rtas.dev, "ibm,errinjct-tokens",
					    &array_len);    
	/* if token is not found, then we fall through loop */
	end_array = token_array + array_len;
	for (i = 0, j = 0; i < MAX_ERRINJCT_TOKENS && token_array < end_array; i++) {

		len = strnlen(token_array, ERRINJCT_TOKEN_LEN) + 1;
		ei_token_list[i].name = (char *) kmalloc(len, GFP_KERNEL);
		if (!ei_token_list[i].name) {
			printk(KERN_WARNING "error: kmalloc failed\n");
			return -ENOMEM;
		}

		strcpy(ei_token_list[i].name, token_array);
		token_array += len;

		ei_token_list[i].value = *(int *)token_array;
		token_array += sizeof(int);
	}
	for (; i < MAX_ERRINJCT_TOKENS; i++) {
		ei_token_list[i].name = 0;
		ei_token_list[i].value = 0;
	}
	return 0;
}

__initcall(rtas_errinjct_init);
#endif
