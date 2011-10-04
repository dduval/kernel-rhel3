/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/interrupt.h>
#include <linux/nmi.h>
#include <linux/vt_kern.h>
#include <linux/pc_keyb.h>

asmlinkage void sys_sync(void);	/* it's really int */

int panic_timeout;
int panic_on_oops = 1;

struct notifier_block *panic_notifier_list;

static int __init panic_setup(char *str)
{
	panic_timeout = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("panic=", panic_setup);


#if (defined(CONFIG_X86) && defined(CONFIG_VT)) || defined(CONFIG_PC_KEYB)
#define do_blink(x) pckbd_blink(x)
#else
#define do_blink(x) 0
#endif

#ifdef CONFIG_PANIC_MORSE

static int blink_setting = 1;

static const unsigned char morsetable[] = {
	0122, 0, 0310, 0, 0, 0163,			/* "#$%&' */
	055, 0155, 0, 0, 0163, 0141, 0152, 0051, 	/* ()*+,-./ */
	077, 076, 074, 070, 060, 040, 041, 043, 047, 057, /* 0-9 */
	0107, 0125, 0, 0061, 0, 0114, 0, 		/* :;<=>?@ */
	006, 021, 025, 011, 002, 024, 013, 020, 004,	/* A-I */
	036, 015, 022, 007, 005, 017, 026, 033, 012,	/* J-R */
	010, 003, 014, 030, 016, 031, 035, 023,		/* S-Z */
	0, 0, 0, 0, 0154				/* [\]^_ */
};

__inline__ unsigned char tomorse(char c) {
	if (c >= 'a' && c <= 'z')
		c = c - 'a' + 'A';
	if (c >= '"' && c <= '_') {
		return morsetable[c - '"'];
	} else
		return 0;
}


#define DITLEN (HZ / 5)
#define DAHLEN 3 * DITLEN
#define SPACELEN 7 * DITLEN

#define FREQ 844

/* Tell the user who may be running in X and not see the console that we have 
   panic'ed. This is to distingush panics from "real" lockups. 
   Could in theory send the panic message as morse, but that is left as an
   exercise for the reader.  
   And now it's done! LED and speaker morse code by Andrew Rodland 
   <arodland@noln.com>, with improvements based on suggestions from
   linux@horizon.com and a host of others.
*/ 

void panic_blink(char *buf)
{ 
	static unsigned long next_jiffie = 0;
	static char * bufpos = 0;
	static unsigned char morse = 0;
	static char state = 1;
	
	if (!blink_setting) 
		return;

	if (!buf)
		buf="Panic lost?";


	if (bufpos && time_after (next_jiffie, jiffies)) {
		return; /* Waiting for something. */
	}

	if (state) { /* Coming off of a blink. */
		if (blink_setting & 0x01)
			do_blink(0);

		state = 0;

		if(morse > 1) { /* Not done yet, just a one-dit pause. */
			next_jiffie = jiffies + DITLEN;
		} else { /* Get a new char, and figure out how much space. */
			
			if(!bufpos)
				bufpos = (char *)buf; /* First time through */

			if(!*bufpos) {
				bufpos = (char *)buf; /* Repeating */
				next_jiffie = jiffies + SPACELEN;
			} else {
				/* Inter-letter space */
				next_jiffie = jiffies + DAHLEN; 
			}

			if (! (morse = tomorse(*bufpos) )) {
				next_jiffie = jiffies + SPACELEN;
				state = 1; /* And get us back here */
			}
			bufpos ++;
		}
	} else { /* Starting a new blink. We have valid code in morse. */
		int len;

		len = (morse & 001) ? DAHLEN : DITLEN;

		if (blink_setting & 0x02)
			kd_mksound(FREQ, len);
		
		next_jiffie = jiffies + len;

		if (blink_setting & 0x01)
			do_blink(1);
		state = 1;
		morse >>= 1;
	}
}  

#else /* CONFIG_PANIC_MORSE */

static int blink_setting = HZ / 2; /* Over here, it's jiffies between blinks. */

/* This is the "original" 2.4-ac panic_blink, rewritten to use my
 * sorta-arch-independent do_blink stuff.
 */
void panic_blink(char *buf) {
	static char state = 0;
	static unsigned long next_jiffie = 0;

	if (!blink_setting)
		return;

	if (jiffies >= next_jiffie) {
		state ^= 1;
		do_blink(state);
		next_jiffie = jiffies + blink_setting;
	}

	return;
}

#endif /* CONFIG_PANIC_MORSE */

static int __init panicblink_setup(char *str)
{
    int par;
    if (get_option(&str,&par)) 
	    blink_setting = par;
    return 1;
}

/* panicblink=0 disables the blinking as it caused problems with some console
   switches. */
__setup("panicblink=", panicblink_setup);


/**
 *	panic - halt the system
 *	@fmt: The text string to print
 *
 *	Display a message, then perform cleanups. Functions in the panic
 *	notifier list are called after the filesystem cache is flushed (when possible).
 *
 *	This function never returns.
 */

int netdump_mode = 0;
int diskdump_mode = 0;
 
NORET_TYPE void panic(const char * fmt, ...)
{
	static char buf[1024];
	va_list args;
#if defined(CONFIG_ARCH_S390)
        unsigned long caller = (unsigned long) __builtin_return_address(0);
#endif

	bust_spinlocks(1);
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	printk(KERN_EMERG "Kernel panic: %s\n",buf);
	if (netdump_func || diskdump_func)
		BUG();
	if (in_interrupt())
		printk(KERN_EMERG "In interrupt handler - not syncing\n");
	else if (!current->pid)
		printk(KERN_EMERG "In idle task - not syncing\n");
	else
		sys_sync();
	bust_spinlocks(0);
	printk("\n");

#ifdef CONFIG_SMP
	smp_send_stop();
#endif

	notifier_call_chain(&panic_notifier_list, 0, buf);

	if (panic_timeout > 0)
	{
		int i;
		/*
	 	 * Delay timeout seconds before rebooting the machine. 
		 * We can't use the "normal" timers since we just panicked..
	 	 */
		printk(KERN_EMERG "Rebooting in %d second%s..",
			panic_timeout, "s" + (panic_timeout == 1));
		for (i = 0; i < panic_timeout; i++) {
			touch_nmi_watchdog();
			mdelay(1000);
		}
		printk("\n");
		/*
		 *	Should we run the reboot notifier. For the moment Im
		 *	choosing not too. It might crash, be corrupt or do
		 *	more harm than good for other reasons.
		 */
		machine_restart(NULL);
	}
#ifdef __sparc__
	{
		extern int stop_a_enabled;
		/* Make sure the user can actually press L1-A */
		stop_a_enabled = 1;
		printk("Press L1-A to return to the boot prom\n");
	}
#endif
#ifdef CONFIG_PPC_PSERIES
	{
		long status;
		char *str = "OS panic";
		status = rtas_call(rtas_token("ibm,os-term"), 1, 1, NULL,
				   __pa(str));
		if (status != 0)
			printk(KERN_EMERG "ibm,os-term call failed %d\n",
				status);
	}
#endif
#if defined(CONFIG_ARCH_S390)
        disabled_wait(caller);
#endif
	local_irq_enable();
	for(;;) {
		panic_blink(buf); 
		CHECK_EMERGENCY_SYNC
	}
}

/**
 *	print_tainted - return a string to represent the kernel taint state.
 *
 *	The string is overwritten by the next call to print_taint().
 */
 
const char *print_tainted()
{
	static char buf[20];
	if (tainted) {
		snprintf(buf, sizeof(buf), "Tainted: %c%c",
			tainted & 1 ? 'P' : 'G',
			tainted & 2 ? 'F' : ' ');
	}
	else
		snprintf(buf, sizeof(buf), "Not tainted");
	return(buf);
}

int tainted = 0;

/*
 * A BUG() call in an inline function in a header should be avoided,
 * because it can seriously bloat the kernel.  So here we have
 * helper functions.
 * We lose the BUG()-time file-and-line info this way, but it's
 * usually not very useful from an inline anyway.  The backtrace
 * tells us what we want to know.
 */

void __out_of_line_bug(int line)
{
	printk("kernel BUG in header file at line %d\n", line);

	BUG();

	/* Satisfy __attribute__((noreturn)) */
	for ( ; ; )
		;
}

/*
 * Try crashdump. Diskdump is first, netdump is second.
 * We clear diskdump_func before call of diskdump_func, so
 * If double panic would occur in diskdump, netdump can handle
 * it.
 */
#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
#include <asm/diskdump.h>
#endif
void try_crashdump(struct pt_regs *regs)
{
#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
	if (diskdump_func) {
		void (*func)(struct pt_regs *, void *);
		func = diskdump_func;
		diskdump_func = NULL;
		platform_start_diskdump(func, regs);
	}
#endif
	if (netdump_func)
		netdump_func(regs);
	netdump_func = NULL;
}
