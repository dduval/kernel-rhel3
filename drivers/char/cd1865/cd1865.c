/* -*- linux-c -*- */
/*
 *	This code was modified from
 *      specialix.c  -- specialix IO8+ multiport serial driver.
 *
 *      Copyright (C) 1997  Roger Wolff (R.E.Wolff@BitWizard.nl)
 *      Copyright (C) 1994-1996  Dmitry Gorodchanin (pgmdsg@ibi.com)
 *	Modifications (C) 2002 Telford Tools, Inc. (martillo@telfordtools.com)
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *      USA.
 *
 */

#define VERSION "2.11"

#include <linux/config.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/fcntl.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/tqueue.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <asm/uaccess.h>

#include "cdsiolx.h"
#include "../cd1865.h" 		/* will move all files up one level */
#include "siolx.h"
#include "plx9060.h"

#define SIOLX_NORMAL_MAJOR  254		/* One is needed */
#define SIOLX_ID	    0x10
#define CD186x_MSMR	    0x61	/* modem/timer iack */
#define CD186x_TSMR         0x62	/* tx iack     */
#define CD186x_RSMR         0x63	/* rx iack     */

/* Configurable options: */

/* Am I paranoid or not ? ;-) */
#define SIOLX_PARANOIA_CHECK

/* Do I trust the IRQ from the card? (enabeling it doesn't seem to help)
   When the IRQ routine leaves the chip in a state that is keeps on
   requiring attention, the timer doesn't help either. */
#undef SIOLX_TIMER
/* 
 * The following defines are mostly for testing purposes. But if you need
 * some nice reporting in your syslog, you can define them also.
 */
#undef SIOLX_REPORT_FIFO
#undef SIOLX_REPORT_OVERRUN

#ifdef CONFIG_SIOLX_RTSCTS	/* may need to set this */
#define SIOLX_CRTSCTS(bla) 1
#else
#define SIOLX_CRTSCTS(tty) C_CRTSCTS(tty)
#endif

/* Used to be outb (0xff, 0x80); */
#define short_pause() udelay (1)

#define SIOLX_LEGAL_FLAGS \
	(ASYNC_HUP_NOTIFY   | ASYNC_SAK          | ASYNC_SPLIT_TERMIOS   | \
	 ASYNC_SPD_HI       | ASYNC_SPEED_VHI    | ASYNC_SESSION_LOCKOUT | \
	 ASYNC_PGRP_LOCKOUT | ASYNC_CALLOUT_NOHUP)

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

DECLARE_TASK_QUEUE(tq_siolx);

#undef RS_EVENT_WRITE_WAKEUP
#define RS_EVENT_WRITE_WAKEUP	0

#define SIOLX_TYPE_NORMAL	1
#define SIOLX_TYPE_CALLOUT	2

#define BD_8000P		1
#define BD_16000P		2
#define BD_8000C		3
#define BD_16000C		4
#define BD_MAX			BD_16000C

static struct siolx_board *SiolxIrqRoot[SIOLX_NUMINTS]; 

static char *sio16_board_type[] =
{
	"unknown",
	" 8000P ",
	"16000P ",
	" 8000C ",
	"16000C "
};
static struct tty_driver siolx_driver, siolx_callout_driver;
static int    siolx_refcount;
static unsigned char * tmp_buf;
static DECLARE_MUTEX(tmp_buf_sem);
static unsigned long baud_table[] =  
{
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0, 
};
static int siolx_debug = 0;	/* turns on lots of */
				/* debugging messages*/
static int siolx_major = SIOLX_NORMAL_MAJOR;
#ifdef MODULE
static int siolx_minorstart = 256;
#endif
static int siolx_vendor_id = 			PCI_VENDOR_ID_PLX;
static int siolx_device_id = 			PCI_DEVICE_ID_PLX_9060SD;
static int siolx_subsystem_vendor = 		AURASUBSYSTEM_VENDOR_ID;
static int siolx_subsystem_pci_device = 	AURASUBSYSTEM_MPASYNCPCI;
static int siolx_subsystem_cpci_device = 	AURASUBSYSTEM_MPASYNCcPCI;
static int siolx_bhindex = SIOLX_BH; /* if this softinterrupt slot is filled */

MODULE_PARM(siolx_vendor_id, "i");
MODULE_PARM(siolx_device_id, "i");
#ifdef MODULE
MODULE_PARM(siolx_minorstart, "i");
#endif
MODULE_PARM(siolx_major, "i");
MODULE_PARM(siolx_subsystem_vendor, "i");
MODULE_PARM(siolx_subsystem_pci_device, "i");
MODULE_PARM(siolx_subsystem_cpci_device, "i");
MODULE_PARM(siolx_bhindex, "i");

static struct siolx_board 	*siolx_board_root;
static struct siolx_board 	*siolx_board_last;
static struct siolx_port 	*siolx_port_root;
static struct siolx_port 	*siolx_port_last;
static unsigned int 		NumSiolxPorts;
static struct tty_struct 	**siolx_table;	/* make dynamic */
static struct termios 		**siolx_termios;
static struct termios 		**siolx_termios_locked;
static int 			siolx_driver_registered;
static int 			siolx_callout_driver_registered;

#ifdef SIOLX_TIMER
static struct timer_list missed_irq_timer;
static void siolx_interrupt(int irq, void * dev_id, struct pt_regs * regs);
#endif

extern struct tty_driver *get_tty_driver(kdev_t device);

static inline int port_No_by_chip (struct siolx_port const * port)
{
	return SIOLX_PORT(port->boardport);
}

/* Describe the current board and port configuration */

static int siolx_read_proc(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	struct siolx_port *port = siolx_port_root;
	off_t begin = 0;
	int len = 0;
	unsigned int typeno;
	char *revision = "$Revision: 1.11 $";
	
	len += sprintf(page, "SIOLX Version %s. %s\n", VERSION, revision);
	len += sprintf(page+len, "TTY MAJOR = %d, CUA MAJOR = %d.\n", 
		       siolx_driver.major, siolx_callout_driver.major);

	for (port = siolx_port_root; port != NULL; port = port->next_by_global_list) 
	{
		typeno = port->board->boardtype;
		if(typeno > BD_MAX)
		{
			typeno = 0;
		}
		len += sprintf(page+len, 
			       "%3.3d: bd %2.2d: %s: ch %d: pt %2.2d/%d: tp %4.4d%c: bs %2.2d: sl %2.2d: ir %2.2d: fl %c%c%c%c%c\n", 
			       siolx_driver.minor_start + port->driverport,
			       port->board->boardnumber,
			       sio16_board_type[typeno],
			       port->board->chipnumber,
			       port->boardport,
			       port_No_by_chip(port), /* port relative to chip */
			       port->board->chiptype,
			       port->board->chiprev,
			       port->board->pdev.bus->number,
			       PCI_SLOT(port->board->pdev.devfn),
			       port->board->irq,
			       (port->flags & ASYNC_INITIALIZED) ? 'I' : ' ',
			       (port->flags & ASYNC_CALLOUT_ACTIVE) ? 'D' : ' ',
			       (port->flags & ASYNC_NORMAL_ACTIVE) ? 'T' : ' ',
			       (port->flags & ASYNC_CLOSING) ? 'C' : ' ',
			       port->board->reario ? 'R' : ' ');
		if (len+begin > off+count)
		{
			goto done;
		}
		if (len+begin < off) 
		{
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
 done:
	if (off >= len+begin)
	{
		return 0;
	}
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

#ifndef MODULE
static int GetMinorStart(void)	/* minor start can be determined on fly when driver linked to kernel */
{
	struct tty_driver *ttydriver;
	int minor_start = 0;
	kdev_t device;
	
	device = MKDEV(siolx_major, minor_start);
	while(ttydriver = get_tty_driver(device), ttydriver != NULL)
	{
		minor_start += ttydriver->num;
		device = MKDEV(TTY_MAJOR, minor_start);
	}
	return minor_start;
	
}
#endif

/* only once per board chain */
void SiolxResetBoard(struct siolx_board * bp, struct pci_dev *pdev)
{
	register unsigned int regvalue;
	unsigned char savedvalue;
	/*
	 * Yuch.  Here's the deal with the reset bits in the
	 *  ECNTL register of the 9060SD.
	 *
	 * It appears that LCLRST resets the PLX local configuration
	 *  registers (not the PCI configuration registers) to their
	 *  default values.  We need to use LCLRST because it
	 *  is the command (I think) that pulls the local reset
	 *  line on the local bus side of the 9060SD.
	 *
	 * Unfortunately, by resetting the PLX local configuration
	 *  registers, we can't use the damn board.  So we must
	 *  reinitialize them.  The easiest way to do that is to run
	 *  the LDREG command.  Unfortunately, it has the side effect
	 *  of reinitializing the PCI configuration registers.  It seems,
	 *  however that only the value stowed in ILINE gets choked; all
	 *  of the others seem to be properly preserved.
	 *
	 * So, what the code does now is to get a copy of ILINE by
	 *  hand, and then restore it after reloading the registers.
	 */

	bp->pdev = *pdev;
	bp->plx_vaddr = (unsigned long) ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if(bp->plx_vaddr)
	{
		regvalue = readl(bp->plx_vaddr + PLX_ECNTL);
		regvalue &= ~PLX_ECNTLLDREG;
		regvalue |= PLX_ECNTLLCLRST;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		udelay(200);
		regvalue &= ~PLX_ECNTLLCLRST;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &savedvalue);
		regvalue |= PLX_ECNTLLDREG;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		udelay(200);
		regvalue &= ~PLX_ECNTLLDREG;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, savedvalue);
		regvalue |= PLX_ECNTLINITSTAT;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		writel(0, bp->plx_vaddr + PLX_ICSR);
	}
}

void SiolxShutdownBoard(struct siolx_board * bp)
{
	register unsigned int regvalue;
	unsigned char savedvalue;
	struct pci_dev *pdev;

	if(bp->chipnumber == 0) /* only shutdown first in a chain */
	{
		pdev = &bp->pdev;

		writel(0, bp->plx_vaddr + PLX_ICSR);
		regvalue = readl(bp->plx_vaddr + PLX_ECNTL);
		regvalue &= ~PLX_ECNTLLDREG;
		regvalue |= PLX_ECNTLLCLRST;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		udelay(200);
		regvalue &= ~PLX_ECNTLLCLRST;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &savedvalue);
		regvalue |= PLX_ECNTLLDREG;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		udelay(200);
		regvalue &= ~PLX_ECNTLLDREG;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, savedvalue);
		regvalue |= PLX_ECNTLINITSTAT;
		writel(regvalue, bp->plx_vaddr + PLX_ECNTL);
		writel(0, bp->plx_vaddr + PLX_ICSR);
		iounmap((void*)bp->plx_vaddr);
		bp->plx_vaddr = 0;
	}
}

static inline int siolx_paranoia_check(struct siolx_port const * port,
				       kdev_t device, const char *routine)
{
#ifdef SIOLX_PARANOIA_CHECK
	static const char *badmagic =
		KERN_ERR "siolx: Warning: bad siolx port magic number for device %s in %s\n";
	static const char *badinfo =
		KERN_ERR "siolx: Warning: null siolx port for device %s in %s\n";
 
	if (!port) 
	{
		printk(badinfo, kdevname(device), routine);
		return 1;
	}
	if (port->magic != SIOLX_MAGIC) 
	{
		printk(badmagic, kdevname(device), routine);
		return 1;
	}
#endif
	return 0;
}


/*
 * 
 *  Service functions for siolx Aurora Asynchronous Adapter driver.
 * 
 */

/* Get board number from pointer */
static inline int board_No (struct siolx_board * bp)
{
	return bp->boardnumber;	/* note same for all chips/boards in a chain */
}


/* Get port number from pointer */
static inline int port_No (struct siolx_port const * port)
{
	return port->driverport; /* offset from minor start */
}

/* Get pointer to board from pointer to port */
static inline struct siolx_board * port_Board(struct siolx_port const * port)
{
	return port->board;	/* same for ports on both chips on a board */
}


/* Input Byte from CL CD186x register */
static inline unsigned char siolx_in(struct siolx_board  * bp, unsigned short reg)
{
	return readb (bp->base + reg);
}


/* Output Byte to CL CD186x register */
static inline void siolx_out(struct siolx_board  * bp, unsigned short reg,
				 unsigned char val)
{
	writeb(val, bp->base + reg);
}


/* Wait for Channel Command Register ready */
static int siolx_wait_CCR(struct siolx_board  * bp)
{
	unsigned long delay;

	for (delay = SIOLX_CCR_TIMEOUT; delay; delay--) 
	{
		udelay(1);
		if (!siolx_in(bp, CD186x_CCR))
		{
			return 0;
		}
	}
	printk(KERN_ERR "siolx:board %d: timeout waiting for CCR.\n", board_No(bp));
	return -1;
}

/* Wait for ready */
static int siolx_wait_GIVR(struct siolx_board  * bp)
{
	unsigned long delay;

	for (delay = SIOLX_CCR_TIMEOUT; delay; delay--) 
	{
		udelay(1);
		if (siolx_in(bp, CD186x_GIVR) == (unsigned char) 0xff)
		{
			return 0;
		}
	}
	printk(KERN_ERR "siolx: board %d: timeout waiting for GIVR.\n", board_No(bp));
	return -1;
}

static inline void siolx_release_io_range(struct siolx_board * bp)
{
	if((bp->chipnumber == 0) && bp->vaddr)	/* only release from first board in a chain */
	{
		iounmap((void*)bp->vaddr);
		bp->vaddr = 0;
	}
}
	
/* Must be called with enabled interrupts */

static inline void siolx_long_delay(unsigned long delay)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(delay);	
}

/* Reset and setup CD186x chip */
static int siolx_init_CD186x(struct siolx_board  * bp)
{
	unsigned long flags;
	int scaler;
	int rv = 1;
	int rev;
	int chip;

	save_flags(flags); 	/* not sure of need to turn off ints */
	cli();
	if(siolx_wait_CCR(bp))
	{
		restore_flags(flags);
		return 0;			   /* Wait for CCR ready        */
	}
	siolx_out(bp, CD186x_CAR, 0);
	siolx_out(bp, CD186x_GIVR, 0);
	siolx_out(bp, CD186x_CCR, CCR_HARDRESET);      /* Reset CD186x chip          */
	if(siolx_wait_GIVR(bp))
	{
		restore_flags(flags);
		return 0;
	}
	sti();
	siolx_long_delay(HZ/20);                      /* Delay 0.05 sec            */
	cli();
	siolx_out(bp, CD186x_GIVR, SIOLX_ID | (bp->chipnumber ? 0x80 : 0)); /* Set ID for this chip      */
#if 0
	siolx_out(bp, CD186x_GICR, 0);                 /* Clear all bits            */
#endif
	scaler =  SIOLX_OSCFREQ/1000;
	siolx_out(bp, CD186x_PPRH, scaler >> 8);
	siolx_out(bp, CD186x_PPRL, scaler & 0xff);

	/* Chip           revcode   pkgtype
	                  GFRCR     SRCR bit 7
	   CD180 rev B    0x81      0
	   CD180 rev C    0x82      0
	   CD1864 rev A   0x82      1
	   CD1865 rev A   0x83      1  -- Do not use!!! Does not work. 
	   CD1865 rev B   0x84      1
	 -- Thanks to Gwen Wang, Cirrus Logic.
	 */

	switch (siolx_in(bp, CD186x_GFRCR)) 
	{
	case 0x82:
		chip = 1864;
		rev='A';
		break;
	case 0x83:
		chip = 1865;
		rev='A';
		break;
	case 0x84:
		chip = 1865;
		rev='B';
		break;
	case 0x85:
		chip = 1865;
		rev='C';
		break; /* Does not exist at this time */
	default:
		chip=-1;
		rev='x';
		break;
	}

#if SIOLX_DEBUG > 2
	printk (KERN_DEBUG " GFCR = 0x%02x\n", siolx_in(bp, CD186x_GFRCR) );
#endif

	siolx_out(bp, CD186x_MSMR, CD186x_MRAR); /* load up match regs with address regs */
	siolx_out(bp, CD186x_TSMR, CD186x_TRAR);      
	siolx_out(bp, CD186x_RSMR, CD186x_RRAR);      
	
#if 0
	DEBUGPRINT((KERN_ALERT "match reg values are msmr %x, tsmr %x, rsmr %x.\n", 
		    siolx_in(bp, CD186x_MSMR),
		    siolx_in(bp, CD186x_TSMR),
		    siolx_in(bp, CD186x_RSMR)));
#endif

	siolx_out(bp, CD186x_SRCR, SRCR_AUTOPRI | SRCR_GLOBPRI | SRCR_REGACKEN);
	/* Setting up prescaler. We need 4 ticks per 1 ms */

	printk(KERN_INFO"siolx: CD%4.4d%c detected at 0x%lx, IRQ %d, on Aurora asynchronous adapter board %d, chip number %d.\n",
	       chip, rev, bp->base, bp->irq, board_No(bp), bp->chipnumber);

	bp->chiptype = chip;
	bp->chiprev = rev;

	restore_flags(flags);
	return rv;
}


#ifdef SIOLX_TIMER
void missed_irq (unsigned long data)
{
	if (siolx_in ((struct siolx_board *)data, CD186x_SRSR) &  
	    (SRSR_RREQint |
	     SRSR_TREQint |
	     SRSR_MREQint)) 
	{
		printk (KERN_INFO "Missed interrupt... Calling int from timer. \n");
		siolx_interrupt (((struct siolx_board *)data)->irq, 
				 NULL, NULL);
	}
	missed_irq_timer.expires = jiffies + HZ;
	add_timer (&missed_irq_timer);
}
#endif

/* Main probing routine, also sets irq. */
static int siolx_probe(struct siolx_board *bp)
{
	unsigned char val1, val2;
	
	/* Are the I/O ports here ? */
	siolx_out(bp, CD186x_PPRL, 0x5a);
	short_pause ();
	val1 = siolx_in(bp, CD186x_PPRL);
	
	siolx_out(bp, CD186x_PPRL, 0xa5);
	short_pause ();
	val2 = siolx_in(bp, CD186x_PPRL);
	
	if ((val1 != 0x5a) || (val2 != 0xa5)) 
	{
		printk(KERN_INFO 
		       "siolx: cd serial chip not found at base %ld.\n", 
		       bp->base);
		return 1;
	}

	/* Reset CD186x */
	if (!siolx_init_CD186x(bp)) 
	{
		return -EIO;
	}

#ifdef SIOLX_TIMER
	init_timer (&missed_irq_timer);
	missed_irq_timer.function = missed_irq;
	missed_irq_timer.data = (unsigned long) bp;
	missed_irq_timer.expires = jiffies + HZ;
	add_timer (&missed_irq_timer);
#endif
	return 0;
}

/* 
 * 
 *  Interrupt processing routines.
 * */

static inline void siolx_mark_event(struct siolx_port * port, int event)
{
	/* 
	 * I'm not quite happy with current scheme all serial
	 * drivers use their own BH routine.
	 * It seems this easily can be done with one BH routine
	 * serving for all serial drivers.
	 * For now I must introduce another one - SIOLX_BH.
	 * Still hope this will be changed in near future.
	 * -- Dmitry.
	 */
	/* I use a module parameter that can be set at module
	 * load time so that this driver can be downloaded into
	 * a kernel where the value of SIOLX_BX has been allocated
	 * to something else.  This kludge was not necessary
	 * in the ASLX driver because AURORA_BH had already
	 * been allocated for the sparc and there was no
	 * similar driver for x86 while the ASLX driver probably
	 * will not work for the SPARC and is not guaranteed to
	 * do so (at some point I should clean this situation up) -- Joachim*/
	set_bit(event, &port->event);
	queue_task(&port->tqueue, &tq_siolx);
	mark_bh(siolx_bhindex);
}

static inline struct siolx_port * siolx_get_port(struct siolx_board * bp,
					       unsigned char const * what)
{
	unsigned char channel;
	struct siolx_port * port;
	
	channel = siolx_in(bp, CD186x_GICR) >> GICR_CHAN_OFF;
	if (channel < CD186x_NCH) 
	{
		port = bp->portlist;
		while(port)
		{
			if(channel == 0)
			{
				break;
			}
			port = port->next_by_board;
			--channel;
		}
		
		if(port && (port->flags & ASYNC_INITIALIZED)) /* port should be opened */
		{
			return port;
		}
	}
	printk(KERN_INFO "sx%d: %s interrupt from invalid port %d\n", 
	       board_No(bp), what, channel);
	return NULL;
}


static inline void siolx_receive_exc(struct siolx_board * bp)
{
	struct siolx_port *port;
	struct tty_struct *tty;
	unsigned char status;
	unsigned char ch;

	if (!(port = siolx_get_port(bp, "Receive")))
		return;

	tty = port->tty;
	if (tty->flip.count >= TTY_FLIPBUF_SIZE) 
	{
		printk(KERN_INFO "sx%d: port %d: Working around flip buffer overflow.\n",
		       board_No(bp), port_No(port));
		return;
	}
	
#ifdef SIOLX_REPORT_OVERRUN	
	status = siolx_in(bp, CD186x_RCSR);
	if (status & RCSR_OE) 
	{
		port->overrun++;
#if SIOLX_DEBUG 
		printk(KERN_DEBUG "sx%d: port %d: Overrun. Total %ld overruns.\n", 
		       board_No(bp), port_No(port), port->overrun);
#endif		
	}
	status &= port->mark_mask;
#else	
	status = siolx_in(bp, CD186x_RCSR) & port->mark_mask;
#endif	
	ch = siolx_in(bp, CD186x_RDR);
	if (!status) 
	{
		return;
	}
	if (status & RCSR_TOUT) 
	{
		printk(KERN_INFO "siolx: board %d: chip %d: port %d: Receiver timeout. Hardware problems ?\n", 
		       board_No(bp), bp->chipnumber, port_No(port));
		return;
		
	} 
	else if (status & RCSR_BREAK) 
	{
#ifdef SIOLX_DEBUG
		printk(KERN_DEBUG "siolx: board %d: chip %d: port %d: Handling break...\n",
		       board_No(bp), bp->chipnumber, port_No(port));
#endif
		*tty->flip.flag_buf_ptr++ = TTY_BREAK;
		if (port->flags & ASYNC_SAK)
		{
			do_SAK(tty);
		}
		
	} 
	else if (status & RCSR_PE) 
	{
		*tty->flip.flag_buf_ptr++ = TTY_PARITY;
	}
	else if (status & RCSR_FE)
	{ 
		*tty->flip.flag_buf_ptr++ = TTY_FRAME;
	}
	
	else if (status & RCSR_OE)
	{
		*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
	}
	
	else
	{
		*tty->flip.flag_buf_ptr++ = 0;
	}
	
	*tty->flip.char_buf_ptr++ = ch;
	tty->flip.count++;
	queue_task(&tty->flip.tqueue, &tq_timer);
}


static inline void siolx_receive(struct siolx_board * bp)
{
	struct siolx_port *port;
	struct tty_struct *tty;
	unsigned char count;
	
	if (!(port = siolx_get_port(bp, "Receive")))
		return;
	
	tty = port->tty;
	
	count = siolx_in(bp, CD186x_RDCR);
	
#ifdef SIOLX_REPORT_FIFO
	port->hits[count > 8 ? 9 : count]++;
#endif	
	
	while (count--) 
	{
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) 
		{
			printk(KERN_INFO "siolx: board %d: chip %d: port %d: Working around flip buffer overflow.\n",
			       board_No(bp), bp->chipnumber, port_No(port));
			break;
		}
		*tty->flip.char_buf_ptr++ = siolx_in(bp, CD186x_RDR);
		*tty->flip.flag_buf_ptr++ = 0;
		tty->flip.count++;
	}
	queue_task(&tty->flip.tqueue, &tq_timer);
}

static inline void siolx_transmit(struct siolx_board * bp)
{
	struct siolx_port *port;
	struct tty_struct *tty;
	unsigned char count;
	
	if (!(port = siolx_get_port(bp, "Transmit")))
		return;
	
	tty = port->tty;
	
	if(port->IER & IER_TXEMPTY) 
	{
		/* FIFO drained */
#if 0
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
#endif
		port->IER &= ~IER_TXEMPTY;
		siolx_out(bp, CD186x_IER, port->IER);
		return;
	}
	
	if(((port->xmit_cnt <= 0) && !port->break_length) || 
	    tty->stopped || tty->hw_stopped) 
	{
#if 0
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
#endif
		port->IER &= ~IER_TXRDY;
		siolx_out(bp, CD186x_IER, port->IER);
		return;
	}
	
	if (port->break_length) 
	{
		if (port->break_length > 0) 
		{
			if (port->COR2 & COR2_ETC) 
			{
				siolx_out(bp, CD186x_TDR, CD186x_C_ESC);
				siolx_out(bp, CD186x_TDR, CD186x_C_SBRK);
				port->COR2 &= ~COR2_ETC;
			}
			count = MIN(port->break_length, 0xff);
			siolx_out(bp, CD186x_TDR, CD186x_C_ESC);
			siolx_out(bp, CD186x_TDR, CD186x_C_DELAY);
			siolx_out(bp, CD186x_TDR, count);
			if (!(port->break_length -= count))
			{
				port->break_length--;
			}
		} 
		else 
		{
			siolx_out(bp, CD186x_TDR, CD186x_C_ESC);
			siolx_out(bp, CD186x_TDR, CD186x_C_EBRK);
			siolx_out(bp, CD186x_COR2, port->COR2);
			siolx_wait_CCR(bp);
			siolx_out(bp, CD186x_CCR, CCR_CORCHG2);
			port->break_length = 0;
		}
		return;
	}
	
	count = CD186x_NFIFO;
	do 
	{
		siolx_out(bp, CD186x_TDR, port->xmit_buf[port->xmit_tail++]);
		port->xmit_tail = port->xmit_tail & (SERIAL_XMIT_SIZE-1);
		if (--port->xmit_cnt <= 0)
		{
			break;
		}
	} while (--count > 0);
	
	if (port->xmit_cnt <= 0) 
	{
#if 0
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
#endif
		port->IER &= ~IER_TXRDY;
		siolx_out(bp, CD186x_IER, port->IER);
	}
	if (port->xmit_cnt <= port->wakeup_chars)
	{
		siolx_mark_event(port, RS_EVENT_WRITE_WAKEUP);
	}
}


static inline void siolx_check_modem(struct siolx_board * bp)
{
	struct siolx_port *port;
	struct tty_struct *tty;
	unsigned char mcr;

#ifdef SIOLX_DEBUG
	printk (KERN_DEBUG "Modem intr. ");
#endif
	if (!(port = siolx_get_port(bp, "Modem")))
	{
		return;
	}
	
	tty = port->tty;
	
	mcr = siolx_in(bp, CD186x_MCR);
	DEBUGPRINT((KERN_ALERT "mcr = %02x.\n", mcr));

	if ((mcr & MCR_CDCHG)) 
	{
#ifdef SIOLX_DEBUG 
		DEBUGPRINT((KERN_DEBUG "CD just changed... "));
#endif
		if (siolx_in(bp, CD186x_MSVR) & MSVR_CD) 
		{
#ifdef SIOLX_DEBUG
			DEBUGPRINT(( "Waking up guys in open.\n"));
#endif
			wake_up_interruptible(&port->open_wait); /* note no linefeed in previous print */
		}
		else if (!((port->flags & ASYNC_CALLOUT_ACTIVE) &&
		           (port->flags & ASYNC_CALLOUT_NOHUP))) 
		{
#ifdef SIOLX_DEBUG
			DEBUGPRINT(( "Sending HUP.\n")); /* note no linefeed in previous print */
#endif
			MOD_INC_USE_COUNT;
			if (schedule_task(&port->tqueue_hangup) == 0)
			{
				MOD_DEC_USE_COUNT;
			}
		} 
		else 
		{
#ifdef SIOLX_DEBUG
			DEBUGPRINT(("Don't need to send HUP.\n")); /* note no linefeed in previous print */
#endif
		}
	}
	
#ifdef SIOLX_BRAIN_DAMAGED_CTS
	if (mcr & MCR_CTSCHG) 
	{
		if (siolx_in(bp, CD186x_MSVR) & MSVR_CTS) 
		{
			tty->hw_stopped = 0;
			port->IER |= IER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
				siolx_mark_event(port, RS_EVENT_WRITE_WAKEUP);
		} 
		else 
		{
			tty->hw_stopped = 1;
			port->IER &= ~IER_TXRDY;
		}
		siolx_out(bp, CD186x_IER, port->IER);
	}
	if (mcr & MCR_DSSXHG) 
	{
		if (siolx_in(bp, CD186x_MSVR) & MSVR_DSR) 
		{
			tty->hw_stopped = 0;
			port->IER |= IER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
			{
				siolx_mark_event(port, RS_EVENT_WRITE_WAKEUP);
			}
		} 
		else 
		{
			tty->hw_stopped = 1;
			port->IER &= ~IER_TXRDY;
		}
		siolx_out(bp, CD186x_IER, port->IER);
	}
#endif /* SIOLX_BRAIN_DAMAGED_CTS */
	
	/* Clear change bits */
	siolx_out(bp, CD186x_MCR, 0);
}

/* The main interrupt processing routine */
static void siolx_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	unsigned char status;
	unsigned char rcsr;
	struct siolx_board *bp;

	if((irq < 0) || (irq >= SIOLX_NUMINTS))
	{
		printk(KERN_ALERT "siolx: bad interrupt value %i.\n", irq);
		return;
	}
	/* walk through all the cards on the interrupt that occurred. */
	for(bp = SiolxIrqRoot[irq]; bp != NULL; bp = bp->next_by_interrupt)

	{
		while((readl(bp->intstatus) & PLX_ICSRINTACTIVE) != 0) /* work on on board */
		{
			status = siolx_in(bp, CD186x_SRSR);
			
			if(status & SRSR_RREQint) 
			{
				siolx_in(bp, CD186x_RRAR);
				rcsr = siolx_in(bp, CD186x_RCSR);
				if(rcsr == 0)
				{
					siolx_receive(bp);
				}
				else
				{
					siolx_receive_exc(bp);
				}
			}
			else if (status & SRSR_TREQint) 
			{
				siolx_in(bp, CD186x_TRAR);
				siolx_transmit(bp);
			} 
			else if (status & SRSR_MREQint) 
			{
				siolx_in(bp, CD186x_MRAR);
				siolx_check_modem(bp);
			}
			siolx_out(bp, CD186x_EOIR, 1); /* acknowledge the interrupt */
			bp = bp->next_by_chain;	/* go to next chip on card -- maybe this one */
		} /* it does not matter if bp changes all in a chain have same next by interrupt */
	}
}
	

/*
 * Setting up port characteristics. 
 * Must be called with disabled interrupts
 */
static void siolx_change_speed(struct siolx_board *bp, struct siolx_port *port)
{
	struct tty_struct *tty;
	unsigned long baud;
	long tmp;
	unsigned char cor1 = 0, cor3 = 0;
	unsigned char mcor1 = 0, mcor2 = 0;
	static int again;

	tty = port->tty;
	
	if(!tty || !tty->termios)
	{
		return;
	}

	port->IER  = 0;
	port->COR2 = 0;
	/* Select port on the board */
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));

	/* The Siolx board doens't implement the RTS lines.
	   They are used to set the IRQ level. Don't touch them. */
	/* Must check how to apply these to sio16 boards */
	if (SIOLX_CRTSCTS(tty))
	{
		port->MSVR = (MSVR_DTR | (siolx_in(bp, CD186x_MSVR) & MSVR_RTS));
	}
	else
	{
		port->MSVR = (siolx_in(bp, CD186x_MSVR) & MSVR_RTS);
	}
#ifdef DEBUG_SIOLX
	DEBUGPRINT((KERN_DEBUG "siolx: got MSVR=%02x.\n", port->MSVR));
#endif
	baud = C_BAUD(tty);
	
	if (baud & CBAUDEX) 
	{
		baud &= ~CBAUDEX;
		if((baud < 1) || (baud > 2))
		{
			port->tty->termios->c_cflag &= ~CBAUDEX;
		}
		else
		{
			baud += 15;
		}
	}
	if (baud == 15) 
	{
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
		{
			baud ++;
		}
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
		{
			baud += 2;
		}
	}
	
	
	if (!baud_table[baud]) 
	{
		/* Drop DTR & exit */
#ifdef SIOLX_DEBUG
		DEBUGPRINT((KERN_DEBUG "siolx: Dropping DTR...  Hmm....\n"));
#endif
		if (!SIOLX_CRTSCTS (tty)) 
		{
			port->MSVR &= ~ MSVR_DTR;
			siolx_out(bp, CD186x_MSVR, port->MSVR );
		} 
#ifdef DEBUG_SIOLX
		else
		{
			DEBUGPRINT((KERN_DEBUG "siolx: Can't drop DTR: no DTR.\n"));
		}
#endif
		return;
	} 
	else 
	{
		/* Set DTR on */
		if (!SIOLX_CRTSCTS (tty)) 
		{
			port ->MSVR |= MSVR_DTR;
		}
	}
	
	/*
	 * Now we must calculate some speed depended things 
	 */

	/* Set baud rate for port */
	tmp = port->custom_divisor ;
	if(tmp)
	{
		DEBUGPRINT((KERN_INFO "siolx: board %d: chip %d: port %d: Using custom baud rate divisor %ld. \n"
			    "This is an untested option, please be carefull.\n",
			    board_No(bp),
			    bp->chipnumber,
			    port_No(port), tmp));
	}
	else
	{
		tmp = (((SIOLX_OSCFREQ + baud_table[baud]/2) / baud_table[baud] +
		         CD186x_TPC/2) / CD186x_TPC);
	}

	if ((tmp < 0x10) && time_before(again, jiffies)) 
	{ 
		again = jiffies + HZ * 60;
		/* Page 48 of version 2.0 of the CL-CD1865 databook */
		if (tmp >= 12) 
		{
			DEBUGPRINT((KERN_INFO "siolx: board %d: chip %d: port %d:Baud rate divisor is %ld. \n"
				    "Performance degradation is possible.\n"
				    "Read siolx.txt for more info.\n",
				    board_No(bp), bp->chipnumber,
				    port_No (port), tmp));
		} else 
		{
			DEBUGPRINT((KERN_INFO "siolx: board %d: chip %d: port %d: Baud rate divisor is %ld. \n"
				    "    Warning: overstressing Cirrus chip. "
				    "    This might not work.\n"
				    "    Read siolx.txt for more info.\n", 
				    board_No(bp), bp->chipnumber, port_No (port), tmp));
		}
	}

	siolx_out(bp, CD186x_RBPRH, (tmp >> 8) & 0xff); 
	siolx_out(bp, CD186x_TBPRH, (tmp >> 8) & 0xff); 
	siolx_out(bp, CD186x_RBPRL, tmp & 0xff); 
	siolx_out(bp, CD186x_TBPRL, tmp & 0xff);

	if (port->custom_divisor) 
	{
		baud = (SIOLX_OSCFREQ + port->custom_divisor/2) / port->custom_divisor;
		baud = ( baud + 5 ) / 10;
	} else 
	{
		baud = (baud_table[baud] + 5) / 10;   /* Estimated CPS */
	}
	
	/* Two timer ticks seems enough to wakeup something like SLIP driver */
	tmp = ((baud + HZ/2) / HZ) * 2 - CD186x_NFIFO;		
	port->wakeup_chars = (tmp < 0) ? 0 : ((tmp >= SERIAL_XMIT_SIZE) ?
					      SERIAL_XMIT_SIZE - 1 : tmp);
	
	/* Receiver timeout will be transmission time for 1.5 chars */
	tmp = (SIOLX_TPS + SIOLX_TPS/2 + baud/2) / baud;
	tmp = (tmp > 0xff) ? 0xff : tmp;
	siolx_out(bp, CD186x_RTPR, tmp);
	
	switch (C_CSIZE(tty)) 
	{
	case CS5:
		cor1 |= COR1_5BITS;
		break;
	case CS6:
		cor1 |= COR1_6BITS;
		break;
	case CS7:
		cor1 |= COR1_7BITS;
		break;
	case CS8:
		cor1 |= COR1_8BITS;
		break;
	}
	
	if (C_CSTOPB(tty)) 
	{
		cor1 |= COR1_2SB;
	}
	
	cor1 |= COR1_IGNORE;
	if (C_PARENB(tty)) 
	{
		cor1 |= COR1_NORMPAR;
		if (C_PARODD(tty)) 
		{
			cor1 |= COR1_ODDP;
		}
		if (I_INPCK(tty)) 
		{
			cor1 &= ~COR1_IGNORE;
		}
	}
	/* Set marking of some errors */
	port->mark_mask = RCSR_OE | RCSR_TOUT;
	if (I_INPCK(tty)) 
	{
		port->mark_mask |= RCSR_FE | RCSR_PE;
	}
	if (I_BRKINT(tty) || I_PARMRK(tty)) 
	{
		port->mark_mask |= RCSR_BREAK;
	}
	if (I_IGNPAR(tty)) 
	{
		port->mark_mask &= ~(RCSR_FE | RCSR_PE);
	}
	if (I_IGNBRK(tty)) 
	{
		port->mark_mask &= ~RCSR_BREAK;
		if (I_IGNPAR(tty)) 
		{
			/* Real raw mode. Ignore all */
			port->mark_mask &= ~RCSR_OE;
		}
	}
	/* Enable Hardware Flow Control */
	if (C_CRTSCTS(tty)) 
	{
#ifdef SIOLX_BRAIN_DAMAGED_CTS
		port->IER |= IER_DSR | IER_CTS;
		mcor1 |= MCOR1_DSRZD | MCOR1_CTSZD;
		mcor2 |= MCOR2_DSROD | MCOR2_CTSOD;
		tty->hw_stopped = !(siolx_in(bp, CD186x_MSVR) & (MSVR_CTS|MSVR_DSR));
#else
		port->COR2 |= COR2_CTSAE; 
#endif
	}
	/* Enable Software Flow Control. FIXME: I'm not sure about this */
	/* Some people reported that it works, but I still doubt it */
	if (I_IXON(tty)) 
	{
		port->COR2 |= COR2_TXIBE;
		cor3 |= (COR3_FCT | COR3_SCDE);
		if (I_IXANY(tty))
		{
			port->COR2 |= COR2_IXM;
		}
		siolx_out(bp, CD186x_SCHR1, START_CHAR(tty));
		siolx_out(bp, CD186x_SCHR2, STOP_CHAR(tty));
		siolx_out(bp, CD186x_SCHR3, START_CHAR(tty));
		siolx_out(bp, CD186x_SCHR4, STOP_CHAR(tty));
	}
	if (!C_CLOCAL(tty)) 
	{
		/* Enable CD check */
		port->IER |= IER_CD;
		mcor1 |= MCOR1_CDZD;
		mcor2 |= MCOR2_CDOD;
	}
	
	if (C_CREAD(tty)) 
	{
		/* Enable receiver */
		port->IER |= IER_RXD;
	}
	
	/* Set input FIFO size (1-8 bytes) */
	cor3 |= SIOLX_RXFIFO; 
	/* Setting up CD186x channel registers */
	siolx_out(bp, CD186x_COR1, cor1);
	siolx_out(bp, CD186x_COR2, port->COR2);
	siolx_out(bp, CD186x_COR3, cor3);
	/* Make CD186x know about registers change */
	siolx_wait_CCR(bp);
	siolx_out(bp, CD186x_CCR, CCR_CORCHG1 | CCR_CORCHG2 | CCR_CORCHG3);
	/* Setting up modem option registers */
#ifdef DEBUG_SIOLX
	DEBUGPRINT((KERN_ALERT "siolx:  Mcor1 = %02x, mcor2 = %02x.\n", mcor1, mcor2));
#endif
	siolx_out(bp, CD186x_MCOR1, mcor1);
	siolx_out(bp, CD186x_MCOR2, mcor2);
	/* Enable CD186x transmitter & receiver */
	siolx_wait_CCR(bp);
	siolx_out(bp, CD186x_CCR, CCR_TXEN | CCR_RXEN);
	/* Enable interrupts */
	siolx_out(bp, CD186x_IER, port->IER);
	/* And finally set the modem lines... */
	siolx_out(bp, CD186x_MSVR, port->MSVR);
}


/* Must be called with interrupts enabled */
static int siolx_setup_port(struct siolx_board *bp, struct siolx_port *port)
{
	unsigned long flags;
	
	if (port->flags & ASYNC_INITIALIZED)
	{
		return 0;
	}
	
	if (!port->xmit_buf) 
	{
		/* We may sleep in get_free_page() */
		unsigned long tmp;
		
		if (!(tmp = get_free_page(GFP_KERNEL)))
		{
			return -ENOMEM;
		}

		if (port->xmit_buf) 
		{
			free_page(tmp);
			return -ERESTARTSYS;
		}
		port->xmit_buf = (unsigned char *) tmp;
	}
		
	save_flags(flags); cli();
		
	if (port->tty) 
	{
		clear_bit(TTY_IO_ERROR, &port->tty->flags);
	}
		
	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	siolx_change_speed(bp, port);
	port->flags |= ASYNC_INITIALIZED;
		
	restore_flags(flags);
	return 0;
}


/* Must be called with interrupts disabled */
static void siolx_shutdown_port(struct siolx_board *bp, struct siolx_port *port)
{
	struct tty_struct *tty;
	
	if (!(port->flags & ASYNC_INITIALIZED)) 
	{
		return;
	}
	
#ifdef SIOLX_REPORT_OVERRUN
	DEBUGPRINT((KERN_INFO "siolx: board %d: chip %d: port %d: Total %ld overruns were detected.\n",
		    board_No(bp), bp->chipnumber, port_No(port), port->overrun));
#endif	
#ifdef SIOLX_REPORT_FIFO
	{
		int i;
		
		DEBUGPRINT((KERN_INFO "siolx: board %d: chip %d: port %d: FIFO hits [ ",
			    board_No(bp), bp->chipnumber, port_No(port)));
		for (i = 0; i < 10; i++) 
		{
			DEBUGPRINT(("%ld ", port->hits[i]));
		}
		DEBUGPRINT(("].\n"));
	}
#endif	
	if (port->xmit_buf) 
	{
		free_page((unsigned long) port->xmit_buf);
		port->xmit_buf = NULL;
	}

	/* Select port */
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));

	if (!(tty = port->tty) || C_HUPCL(tty)) 
	{
		/* Drop DTR */
		siolx_out(bp, CD186x_MSVDTR, 0);
	}
	
	/* Reset port */
	siolx_wait_CCR(bp);
	siolx_out(bp, CD186x_CCR, CCR_SOFTRESET);
	/* Disable all interrupts from this port */
	port->IER = 0;
	siolx_out(bp, CD186x_IER, port->IER);
	
	if (tty)
	{
		set_bit(TTY_IO_ERROR, &tty->flags);
	}
	port->flags &= ~ASYNC_INITIALIZED;
	
	/*
	 * If this is the last opened port on the board
	 * shutdown whole board
	 */
	MOD_DEC_USE_COUNT;
}

	
static int block_til_ready(struct tty_struct *tty, struct file * filp,
                           struct siolx_port *port)
{
	DECLARE_WAITQUEUE(wait,  current);
	struct siolx_board *bp = port_Board(port);
	int    retval;
	int    do_clocal = 0;
	int    CD;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) || port->flags & ASYNC_CLOSING) 
	{
		interruptible_sleep_on(&port->close_wait);
		if (port->flags & ASYNC_HUP_NOTIFY)
		{
			return -EAGAIN;
		}
		else
		{
			return -ERESTARTSYS;
		}
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SIOLX_TYPE_CALLOUT) 
	{
		if (port->flags & ASYNC_NORMAL_ACTIVE)
		{
			return -EBUSY;
		}
		if ((port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (port->flags & ASYNC_SESSION_LOCKOUT) &&
		    (port->session != current->session))
		{
			return -EBUSY;
		}
		if ((port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (port->flags & ASYNC_PGRP_LOCKOUT) &&
		    (port->pgrp != current->pgrp))
		{
			return -EBUSY;
		}
		port->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) 
	{
		if (port->flags & ASYNC_CALLOUT_ACTIVE)
		{
			return -EBUSY;
		}
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (port->flags & ASYNC_CALLOUT_ACTIVE) 
	{
		if (port->normal_termios.c_cflag & CLOCAL) 
		{
			do_clocal = 1;
		}
	} 
	else 
	{
		if (C_CLOCAL(tty))
		{
			do_clocal = 1;
		}
	}
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&port->open_wait, &wait);
	cli();
	if (!tty_hung_up_p(filp))
	{
		port->count--;
	}
	sti();
	port->blocked_open++;
	while (1) 
	{
		cli();
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
		CD = siolx_in(bp, CD186x_MSVR) & MSVR_CD;
		if (!(port->flags & ASYNC_CALLOUT_ACTIVE)) 
		{
			if (SIOLX_CRTSCTS (tty)) 
			{
				/* Activate RTS */
				port->MSVR |= MSVR_DTR;
				siolx_out (bp, CD186x_MSVR, port->MSVR);
			} 
			else 
			{
				/* Activate DTR */
				port->MSVR |= MSVR_DTR;
				siolx_out (bp, CD186x_MSVR, port->MSVR);
			} 
		}
		sti();
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) || !(port->flags & ASYNC_INITIALIZED)) 
		{
			if (port->flags & ASYNC_HUP_NOTIFY)
			{
				retval = -EAGAIN;
			}
			else
			{
				retval = -ERESTARTSYS;	
			}
			break;
		}
		if (!(port->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(port->flags & ASYNC_CLOSING) &&
		    (do_clocal || CD))
		{
			break;
		}
		if (signal_pending(current)) 
		{
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&port->open_wait, &wait);
	if (!tty_hung_up_p(filp))
	{
		port->count++;
	}
	port->blocked_open--;
	if (retval)
	{
		return retval;
	}
	
	port->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}	

static inline struct siolx_port *siolx_portstruc(register int line)
{
	register struct siolx_port *pp;

	line -= siolx_driver.minor_start;
	for(pp = siolx_port_root; (pp != NULL) && (line >= 0); --line, pp = pp->next_by_global_list)
	{
		if(line == 0)
		{
			return pp;
		}
	}
	return NULL;
}


static int siolx_open(struct tty_struct * tty, struct file * filp)
{
	int error;
	struct siolx_port * port;
	struct siolx_board * bp;
	unsigned long flags;
	
	port = siolx_portstruc(MINOR(tty->device));

	if(port == NULL)
	{
		return -ENODEV;
	}
	bp = port->board;
	if(bp == NULL)
	{
		return -ENODEV;
	}
	
#ifdef DEBUG_SIOLX
	printk (KERN_DEBUG "Board = %d, bp = %p, port = %p, portno = %d.\n", 
	        bp->boardnumber, bp, port, siolx_portstruc(MINOR(tty->device)));
#endif

	if (siolx_paranoia_check(port, tty->device, "siolx_open"))
		return -ENODEV;

	MOD_INC_USE_COUNT;

	port->count++;
	tty->driver_data = port;
	port->tty = tty;

	if ((error = siolx_setup_port(bp, port))) 
		return error;
	
	if ((error = block_til_ready(tty, filp, port)))
		return error;

	if ((port->count == 1) && (port->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SIOLX_TYPE_NORMAL)
			*tty->termios = port->normal_termios;
		else
			*tty->termios = port->callout_termios;
		save_flags(flags); cli();
		siolx_change_speed(bp, port);
		restore_flags(flags);
	}

	port->session = current->session;
	port->pgrp = current->pgrp;
	return 0;
}


static void siolx_close(struct tty_struct * tty, struct file * filp)
{
	struct siolx_port *port = (struct siolx_port *) tty->driver_data;
	struct siolx_board *bp;
	unsigned long flags;
	unsigned long timeout;
	
	if (!port || siolx_paranoia_check(port, tty->device, "close"))
		return;
	
	save_flags(flags); cli();
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
	
	bp = port_Board(port);
	if ((atomic_read(&tty->count) == 1) && (port->count != 1)) {
		printk(KERN_ERR "sx%d: siolx_close: bad port count;"
		       " tty->count is 1, port count is %d\n",
		       board_No(bp), port->count);
		port->count = 1;
	}
	if (--port->count < 0) {
		printk(KERN_ERR "sx%d: siolx_close: bad port count for tty%d: %d\n",
		       board_No(bp), port_No(port), port->count);
		port->count = 0;
	}
	if (port->count) {
		restore_flags(flags);
		return;
	}
	port->flags |= ASYNC_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (port->flags & ASYNC_NORMAL_ACTIVE)
		port->normal_termios = *tty->termios;
	if (port->flags & ASYNC_CALLOUT_ACTIVE)
		port->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (port->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, port->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	port->IER &= ~IER_RXD;
	if (port->flags & ASYNC_INITIALIZED) {
		port->IER &= ~IER_TXRDY;
		port->IER |= IER_TXEMPTY;
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
		siolx_out(bp, CD186x_IER, port->IER);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies+HZ;
		while(port->IER & IER_TXEMPTY) {
			current->state = TASK_INTERRUPTIBLE;
 			schedule_timeout(port->timeout);
			if (time_after(jiffies, timeout)) {
				printk (KERN_INFO "siolx: Timeout waiting for close\n");
				break;
			}
		}

	}
	siolx_shutdown_port(bp, port);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	tty_ldisc_flush(tty);
	tty->closing = 0;
	port->event = 0;
	port->tty = 0;
	if (port->blocked_open) {
		if (port->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(port->close_delay);
		}
		wake_up_interruptible(&port->open_wait);
	}
	port->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
			 ASYNC_CLOSING);
	wake_up_interruptible(&port->close_wait);
	restore_flags(flags);
}


static int siolx_write(struct tty_struct * tty, int from_user, 
                    const unsigned char *buf, int count)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
	int c, total = 0;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_write"))
		return 0;
	
	bp = port_Board(port);

	if (!tty || !port->xmit_buf || !tmp_buf)
		return 0;

	save_flags(flags);
	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			c = MIN(count, MIN(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
					   SERIAL_XMIT_SIZE - port->xmit_head));
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!total)
					total = -EFAULT;
				break;
			}

			cli();
			c = MIN(c, MIN(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
				       SERIAL_XMIT_SIZE - port->xmit_head));
			memcpy(port->xmit_buf + port->xmit_head, tmp_buf, c);
			port->xmit_head = (port->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
			port->xmit_cnt += c;
			restore_flags(flags);

			buf += c;
			count -= c;
			total += c;
		}
		up(&tmp_buf_sem);
	} else {
		while (1) {
			cli();
			c = MIN(count, MIN(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
					   SERIAL_XMIT_SIZE - port->xmit_head));
			if (c <= 0) {
				restore_flags(flags);
				break;
			}
			memcpy(port->xmit_buf + port->xmit_head, buf, c);
			port->xmit_head = (port->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
			port->xmit_cnt += c;
			restore_flags(flags);

			buf += c;
			count -= c;
			total += c;
		}
	}

	cli();
	if (port->xmit_cnt && !tty->stopped && !tty->hw_stopped &&
	    !(port->IER & IER_TXRDY)) {
		port->IER |= IER_TXRDY;
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
		siolx_out(bp, CD186x_IER, port->IER);
	}
	restore_flags(flags);
	return total;
}


static void siolx_put_char(struct tty_struct * tty, unsigned char ch)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	unsigned long flags;

	if (siolx_paranoia_check(port, tty->device, "siolx_put_char"))
		return;

	if (!tty || !port->xmit_buf)
		return;

	save_flags(flags); cli();
	
	if (port->xmit_cnt >= SERIAL_XMIT_SIZE - 1) {
		restore_flags(flags);
		return;
	}

	port->xmit_buf[port->xmit_head++] = ch;
	port->xmit_head &= SERIAL_XMIT_SIZE - 1;
	port->xmit_cnt++;
	restore_flags(flags);
}


static void siolx_flush_chars(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_flush_chars"))
		return;
	
	if (port->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !port->xmit_buf)
		return;

	save_flags(flags); cli();
	port->IER |= IER_TXRDY;
	siolx_out(port_Board(port), CD186x_CAR, port_No_by_chip(port));
	siolx_out(port_Board(port), CD186x_IER, port->IER);
	restore_flags(flags);
}


static int siolx_write_room(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	int	ret;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_write_room"))
		return 0;

	ret = SERIAL_XMIT_SIZE - port->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}


static int siolx_chars_in_buffer(struct tty_struct *tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_chars_in_buffer"))
		return 0;
	
	return port->xmit_cnt;
}


static void siolx_flush_buffer(struct tty_struct *tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_flush_buffer"))
		return;

	save_flags(flags); cli();
	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	restore_flags(flags);
	
	tty_wakeup(tty);
}


static int siolx_get_modem_info(struct siolx_port * port, unsigned int *value)
{
	struct siolx_board * bp;
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	bp = port_Board(port);
	save_flags(flags); cli();
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	status = siolx_in(bp, CD186x_MSVR);
	restore_flags(flags);
#ifdef DEBUG_SIOLX
	printk (KERN_DEBUG "Got msvr[%d] = %02x, car = %d.\n", 
		port_No(port), status, siolx_in (bp, CD186x_CAR));
	printk (KERN_DEBUG "siolx_port = %p, port = %p\n", siolx_port, port);
#endif
	if (SIOLX_CRTSCTS(port->tty)) {
		result  = /*   (status & MSVR_RTS) ? */ TIOCM_DTR /* : 0) */ 
		          |   ((status & MSVR_DTR) ? TIOCM_RTS : 0)
		          |   ((status & MSVR_CD)  ? TIOCM_CAR : 0)
		          |/* ((status & MSVR_DSR) ? */ TIOCM_DSR /* : 0) */
		          |   ((status & MSVR_CTS) ? TIOCM_CTS : 0);
	} else {
		result  = /*   (status & MSVR_RTS) ? */ TIOCM_RTS /* : 0) */ 
		          |   ((status & MSVR_DTR) ? TIOCM_DTR : 0)
		          |   ((status & MSVR_CD)  ? TIOCM_CAR : 0)
		          |/* ((status & MSVR_DSR) ? */ TIOCM_DSR /* : 0) */
		          |   ((status & MSVR_CTS) ? TIOCM_CTS : 0);
	}
	put_user(result,(unsigned int *) value);
	return 0;
}


static int siolx_set_modem_info(struct siolx_port * port, unsigned int cmd,
                             unsigned int *value)
{
	int error;
	unsigned int arg;
	unsigned long flags;
	struct siolx_board *bp = port_Board(port);

	error = verify_area(VERIFY_READ, value, sizeof(int));
	if (error) 
		return error;

	get_user(arg, (unsigned long *) value);
	switch (cmd) {
	case TIOCMBIS: 
	   /*	if (arg & TIOCM_RTS) 
			port->MSVR |= MSVR_RTS; */
	   /*   if (arg & TIOCM_DTR)
			port->MSVR |= MSVR_DTR; */

		if (SIOLX_CRTSCTS(port->tty)) {
			if (arg & TIOCM_RTS)
				port->MSVR |= MSVR_DTR; 
		} else {
			if (arg & TIOCM_DTR)
				port->MSVR |= MSVR_DTR; 
		}	     
		break;
	case TIOCMBIC:
	  /*	if (arg & TIOCM_RTS)
			port->MSVR &= ~MSVR_RTS; */
	  /*    if (arg & TIOCM_DTR)
			port->MSVR &= ~MSVR_DTR; */
		if (SIOLX_CRTSCTS(port->tty)) {
			if (arg & TIOCM_RTS)
				port->MSVR &= ~MSVR_DTR;
		} else {
			if (arg & TIOCM_DTR)
				port->MSVR &= ~MSVR_DTR;
		}
		break;
	case TIOCMSET:
	  /* port->MSVR = (arg & TIOCM_RTS) ? (port->MSVR | MSVR_RTS) : 
						 (port->MSVR & ~MSVR_RTS); */
	  /* port->MSVR = (arg & TIOCM_DTR) ? (port->MSVR | MSVR_DTR) : 
						 (port->MSVR & ~MSVR_DTR); */
		if (SIOLX_CRTSCTS(port->tty)) {
	  		port->MSVR = (arg & TIOCM_RTS) ? 
			                         (port->MSVR |  MSVR_DTR) : 
			                         (port->MSVR & ~MSVR_DTR);
		} else {
			port->MSVR = (arg & TIOCM_DTR) ?
			                         (port->MSVR |  MSVR_DTR):
			                         (port->MSVR & ~MSVR_DTR);
		}
		break;
	default:
		return -EINVAL;
	}
	save_flags(flags); cli();
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	siolx_out(bp, CD186x_MSVR, port->MSVR);
	restore_flags(flags);
	return 0;
}


static inline void siolx_send_break(struct siolx_port * port, unsigned long length)
{
	struct siolx_board *bp = port_Board(port);
	unsigned long flags;
	
	save_flags(flags); cli();
	port->break_length = SIOLX_TPS / HZ * length;
	port->COR2 |= COR2_ETC;
	port->IER  |= IER_TXRDY;
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	siolx_out(bp, CD186x_COR2, port->COR2);
	siolx_out(bp, CD186x_IER, port->IER);
	siolx_wait_CCR(bp);
	siolx_out(bp, CD186x_CCR, CCR_CORCHG2);
	siolx_wait_CCR(bp);
	restore_flags(flags);
}


static inline int siolx_set_serial_info(struct siolx_port * port,
                                     struct serial_struct * newinfo)
{
	struct serial_struct tmp;
	struct siolx_board *bp = port_Board(port);
	int change_speed;
	unsigned long flags;
	int error;
	
	error = verify_area(VERIFY_READ, (void *) newinfo, sizeof(tmp));
	if (error)
		return error;

	if (copy_from_user(&tmp, newinfo, sizeof(tmp)))
		return -EFAULT;
	
#if 0	
	if ((tmp.irq != bp->irq) ||
	    (tmp.port != bp->base) ||
	    (tmp.type != PORT_CIRRUS) ||
	    (tmp.baud_base != (SIOLX_OSCFREQ + CD186x_TPC/2) / CD186x_TPC) ||
	    (tmp.custom_divisor != 0) ||
	    (tmp.xmit_fifo_size != CD186x_NFIFO) ||
	    (tmp.flags & ~SIOLX_LEGAL_FLAGS))
		return -EINVAL;
#endif	

	change_speed = ((port->flags & ASYNC_SPD_MASK) !=
			(tmp.flags & ASYNC_SPD_MASK));
	change_speed |= (tmp.custom_divisor != port->custom_divisor);
	
	if (!capable(CAP_SYS_ADMIN)) {
		if ((tmp.close_delay != port->close_delay) ||
		    (tmp.closing_wait != port->closing_wait) ||
		    ((tmp.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
		                  (tmp.flags & ASYNC_USR_MASK));
		port->custom_divisor = tmp.custom_divisor;
	} else {
		port->flags = ((port->flags & ~ASYNC_FLAGS) |
		                  (tmp.flags & ASYNC_FLAGS));
		port->close_delay = tmp.close_delay;
		port->closing_wait = tmp.closing_wait;
		port->custom_divisor = tmp.custom_divisor;
	}
	if (change_speed) {
		save_flags(flags); cli();
		siolx_change_speed(bp, port);
		restore_flags(flags);
	}
	return 0;
}


static inline int siolx_get_serial_info(struct siolx_port * port,
					struct serial_struct * retinfo)
{
	struct serial_struct tmp;
	struct siolx_board *bp = port_Board(port);
	int error;
	
	error = verify_area(VERIFY_WRITE, (void *) retinfo, sizeof(tmp));
	if (error)
		return error;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type = PORT_CIRRUS;
	tmp.line = port->driverport;
	tmp.port = bp->base;
	tmp.irq  = bp->irq;
	tmp.flags = port->flags;
	tmp.baud_base = (SIOLX_OSCFREQ + CD186x_TPC/2) / CD186x_TPC;
	tmp.close_delay = port->close_delay * HZ/100;
	tmp.closing_wait = port->closing_wait * HZ/100;
	tmp.custom_divisor =  port->custom_divisor;
	tmp.xmit_fifo_size = CD186x_NFIFO;
	if (copy_to_user(retinfo, &tmp, sizeof(tmp)))
		return -EFAULT;
	return 0;
}


static int siolx_ioctl(struct tty_struct * tty, struct file * filp, 
                    unsigned int cmd, unsigned long arg)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	int error;
	int retval;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_ioctl"))
		return -ENODEV;
	
	switch (cmd) {
	 case TCSBRK:	/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			siolx_send_break(port, HZ/4);	/* 1/4 second */
		return 0;
	 case TCSBRKP:	/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		tty_wait_until_sent(tty, 0);
		siolx_send_break(port, arg ? arg*(HZ/10) : HZ/4);
		return 0;
	 case TIOCGSOFTCAR:
		error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long));
		if (error)
			return error;
		put_user(C_CLOCAL(tty) ? 1 : 0,
		         (unsigned long *) arg);
		return 0;
	 case TIOCSSOFTCAR:
		get_user(arg, (unsigned long *) arg);
		tty->termios->c_cflag =
			((tty->termios->c_cflag & ~CLOCAL) |
			(arg ? CLOCAL : 0));
		return 0;
	 case TIOCMGET:
		error = verify_area(VERIFY_WRITE, (void *) arg,
		                    sizeof(unsigned int));
		if (error)
			return error;
		return siolx_get_modem_info(port, (unsigned int *) arg);
	 case TIOCMBIS:
	 case TIOCMBIC:
	 case TIOCMSET:
		return siolx_set_modem_info(port, cmd, (unsigned int *) arg);
	 case TIOCGSERIAL:	
		return siolx_get_serial_info(port, (struct serial_struct *) arg);
	 case TIOCSSERIAL:	
		return siolx_set_serial_info(port, (struct serial_struct *) arg);
	 default:
		return -ENOIOCTLCMD;
	}
	return 0;
}


static void siolx_throttle(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_throttle"))
		return;
	
	bp = port_Board(port);
	
	save_flags(flags); cli();

	/* Use DTR instead of RTS ! */
	if (SIOLX_CRTSCTS (tty)) 
	{
		port->MSVR &= ~MSVR_DTR;
	}
	else 
	{
		/* Auch!!! I think the system shouldn't call this then. */
		/* Or maybe we're supposed (allowed?) to do our side of hw
		   handshake anyway, even when hardware handshake is off. 
		   When you see this in your logs, please report.... */
		printk (KERN_ERR "sx%d: Need to throttle, but can't (hardware hs is off)\n",
			port_No (port));
	}
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	if (I_IXOFF(tty)) 
	{
		siolx_wait_CCR(bp);
		siolx_out(bp, CD186x_CCR, CCR_SSCH2);
		siolx_wait_CCR(bp);
	}
	siolx_out(bp, CD186x_MSVR, port->MSVR);
	restore_flags(flags);
}


static void siolx_unthrottle(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_unthrottle"))
		return;
	
	bp = port_Board(port);
	
	save_flags(flags); cli();
	/* XXXX Use DTR INSTEAD???? */
	if (SIOLX_CRTSCTS(tty)) {
		port->MSVR |= MSVR_DTR;
	} /* Else clause: see remark in "siolx_throttle"... */

	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	if (I_IXOFF(tty)) {
		siolx_wait_CCR(bp);
		siolx_out(bp, CD186x_CCR, CCR_SSCH1);
		siolx_wait_CCR(bp);
	}
	siolx_out(bp, CD186x_MSVR, port->MSVR);
	restore_flags(flags);
}


static void siolx_stop(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_stop"))
		return;
	
	bp = port_Board(port);
	
	save_flags(flags); cli();
	port->IER &= ~IER_TXRDY;
	siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
	siolx_out(bp, CD186x_IER, port->IER);
	restore_flags(flags);
}


static void siolx_start(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_start"))
		return;
	
	bp = port_Board(port);
	
	save_flags(flags); cli();
	if (port->xmit_cnt && port->xmit_buf && !(port->IER & IER_TXRDY)) {
		port->IER |= IER_TXRDY;
		siolx_out(bp, CD186x_CAR, port_No_by_chip(port));
		siolx_out(bp, CD186x_IER, port->IER);
	}
	restore_flags(flags);
}


/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_siolx_hangup() -> tty->hangup() -> siolx_hangup()
 * 
 */
static void do_siolx_hangup(void *private_)
{
	struct siolx_port	*port = (struct siolx_port *) private_;
	struct tty_struct	*tty;
	
	tty = port->tty;
	if (tty)
	{
		tty_hangup(tty);	/* FIXME: module removal race here */
	}
	MOD_DEC_USE_COUNT;
}


static void siolx_hangup(struct tty_struct * tty)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	struct siolx_board *bp;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_hangup"))
		return;
	
	bp = port_Board(port);
	
	siolx_shutdown_port(bp, port);
	port->event = 0;
	port->count = 0;
	port->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	port->tty = 0;
	wake_up_interruptible(&port->open_wait);
}


static void siolx_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	struct siolx_port *port = (struct siolx_port *)tty->driver_data;
	unsigned long flags;
				
	if (siolx_paranoia_check(port, tty->device, "siolx_set_termios"))
		return;
	
	if (tty->termios->c_cflag == old_termios->c_cflag &&
	    tty->termios->c_iflag == old_termios->c_iflag)
		return;

	save_flags(flags); cli();
	siolx_change_speed(port_Board(port), port);
	restore_flags(flags);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		siolx_start(tty);
	}
}


static void do_siolx_bh(void)
{
	 run_task_queue(&tq_siolx);
}


static void do_softint(void *private_)
{
	struct siolx_port	*port = (struct siolx_port *) private_;
	struct tty_struct	*tty;
	
	if(!(tty = port->tty)) 
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &port->event)) {
		tty_wakeup(tty);
	}
}

static int siolx_finish_init_drivers(void)
{
	register struct siolx_board *bp;
	register unsigned int count;
	unsigned int maxport;
	struct siolx_port *port;
	struct siolx_port *lastport;
	int error;

	bp = siolx_board_root;

	while(bp)
	{
		if(bp->chipnumber == 0)
		{
			maxport = SIOLX_NPORT;
		}
		else if((bp->boardtype == BD_16000C) && bp->reario) /* must be second chip of 16000C */
		{
			maxport = SIOLX_NPORT/2;
		}
		else
		{
			maxport = SIOLX_NPORT; /* must be second chip of 16000P */
		}

		port = NULL;	/* probably unnecessary */
		lastport = NULL;
		for(count = 0; count < maxport; ++count)
		{
			port = (struct siolx_port*)kmalloc(sizeof(struct siolx_port), GFP_KERNEL);
			if(port == NULL)
			{
				printk(KERN_ALERT
				       "siolx: Failed to create port structure on board %p.\n", bp);
				break; /* no memory available */
			}
			memset(port, 0, sizeof(struct siolx_port));
			
			port->callout_termios = siolx_callout_driver.init_termios;
			port->normal_termios  = siolx_driver.init_termios;
			port->magic = SIOLX_MAGIC;
			port->tqueue.routine = do_softint;
			port->tqueue.data = port;
			port->tqueue_hangup.routine = do_siolx_hangup;
			port->tqueue_hangup.data = port;
			port->close_delay = 50 * HZ/100;
			port->closing_wait = 3000 * HZ/100;
			init_waitqueue_head(&port->open_wait);
			init_waitqueue_head(&port->close_wait);
			
			port->board = bp;
			port->driverport = NumSiolxPorts;
			port->boardport = (count + (port->board->chipnumber*SIOLX_NPORT)); /* 0-16 */

			if(count == 0)
			{
				bp->portlist = port;
			}
			else if(lastport) /* if count != 0 lastport should be non-null */
			{
				lastport->next_by_board = port;
			}
			if(siolx_port_root == NULL)
			{
				siolx_port_root = port;
				siolx_port_last = port;
			}
			else
			{
				siolx_port_last->next_by_global_list = port;
				siolx_port_last = port;
			}
			lastport = port;
			++NumSiolxPorts;
		}
		bp = bp->next_by_global_list;
	}

	siolx_driver.num = NumSiolxPorts;

	siolx_table = (struct tty_struct **) kmalloc(NumSiolxPorts*sizeof(struct tty_struct *), GFP_KERNEL);
	if(siolx_table == NULL)
	{
		printk(KERN_ALERT "siolx:  Could not allocate memory for siolx_table.\n");
		return 1;
	}
	memset(siolx_table, 0, NumSiolxPorts*sizeof(struct tty_struct *));

	siolx_termios = (struct termios **) kmalloc(NumSiolxPorts*sizeof(struct termios *), GFP_KERNEL);
	if(siolx_termios == NULL)
	{
		printk(KERN_ALERT "siolx:  Could not allocate memory for siolx_termios.\n");
		return 1;
	}
	memset(siolx_termios, 0, NumSiolxPorts*sizeof(struct termios *));

	siolx_termios_locked = (struct termios **) kmalloc(NumSiolxPorts*sizeof(struct termios *), GFP_KERNEL);
	if(siolx_termios_locked == NULL)
	{
		printk(KERN_ALERT "siolx:  Could not allocate memory for siolx_termios_locked.\n");
		return 1;
	}
	memset(siolx_termios_locked, 0, NumSiolxPorts*sizeof(struct termios *));

	siolx_driver.table = siolx_table; /* will be changed */
	siolx_driver.termios = siolx_termios; /* will be changed */
	siolx_driver.termios_locked = siolx_termios_locked; /* 	will be changed */

	if ((error = tty_register_driver(&siolx_driver))) 
	{
		if(tmp_buf)
		{
			free_page((unsigned long)tmp_buf);
			tmp_buf = 0;
		}
		printk(KERN_ERR "siolx: Couldn't register Aurora Asynchronous Adapter driver, error = %d\n",
		       error);
		return 1;
	}
	if ((error = tty_register_driver(&siolx_callout_driver))) 
	{
		if(tmp_buf)
		{
			free_page((unsigned long)tmp_buf);
			tmp_buf = NULL;
		}
		tty_unregister_driver(&siolx_driver);
		printk(KERN_ERR "siolx: Couldn't register Aurora Asynchronous Adapter callout driver, error = %d\n",
		       error);
		return 1;
	}
	siolx_driver_registered = 1;
	siolx_callout_driver_registered = 1;
	return 0;		/* success */
}

static int siolx_init_drivers(void)
{
	if (!(tmp_buf = (unsigned char *) get_free_page(GFP_KERNEL))) 
	{
		printk(KERN_ERR "siolx: Couldn't get free page.\n");
		return 1;
	}
	init_bh(siolx_bhindex, do_siolx_bh);
	memset(&siolx_driver, 0, sizeof(siolx_driver));
	siolx_driver.magic = TTY_DRIVER_MAGIC;
	
	siolx_driver.driver_name = "aurasiolx";
	siolx_driver.name = "ttyS";
	siolx_driver.major = siolx_major;
#ifdef MODULE
	siolx_driver.minor_start = siolx_minorstart; /* changed from command line */
#else
	siolx_driver.minor_start = GetMinorStart();
#endif
	siolx_driver.num = 0;	/* will be changed */

	siolx_driver.type = TTY_DRIVER_TYPE_SERIAL;
	siolx_driver.subtype = SIOLX_TYPE_NORMAL;
	siolx_driver.init_termios = tty_std_termios;
	siolx_driver.init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	siolx_driver.flags = TTY_DRIVER_REAL_RAW;
	siolx_driver.refcount = &siolx_refcount;

	siolx_driver.table = siolx_table; /* will be changed */
	siolx_driver.termios = siolx_termios; /* will be changed */
	siolx_driver.termios_locked = siolx_termios_locked; /* 	will be changed */

	siolx_driver.open  = siolx_open;
	siolx_driver.close = siolx_close;
	siolx_driver.write = siolx_write;
	siolx_driver.put_char = siolx_put_char;
	siolx_driver.flush_chars = siolx_flush_chars;
	siolx_driver.write_room = siolx_write_room;
	siolx_driver.chars_in_buffer = siolx_chars_in_buffer;
	siolx_driver.flush_buffer = siolx_flush_buffer;
	siolx_driver.ioctl = siolx_ioctl;
	siolx_driver.throttle = siolx_throttle;
	siolx_driver.unthrottle = siolx_unthrottle;
	siolx_driver.set_termios = siolx_set_termios;
	siolx_driver.stop = siolx_stop;
	siolx_driver.start = siolx_start;
	siolx_driver.hangup = siolx_hangup;

	siolx_callout_driver = siolx_driver;
	siolx_callout_driver.name = "cuw";
	siolx_callout_driver.major = (siolx_major+1);
	siolx_callout_driver.subtype = SIOLX_TYPE_CALLOUT;
	
	siolx_driver.read_proc = siolx_read_proc;
	return 0;
}


static void siolx_release_drivers(void)
{
	unsigned int intr_val;
	struct siolx_board *bp;

	if(tmp_buf)
	{
		free_page((unsigned long)tmp_buf);
		tmp_buf = NULL;
	}
	if(siolx_driver_registered)
	{
		tty_unregister_driver(&siolx_driver);
		siolx_driver_registered = 0;
	}
	if(siolx_callout_driver_registered)
	{
		tty_unregister_driver(&siolx_callout_driver);
		siolx_callout_driver_registered = 0;
	}
	/* unallocate and turn off ints */
	for(intr_val = 0; intr_val < SIOLX_NUMINTS; ++intr_val)
	{
		if(SiolxIrqRoot[intr_val] != NULL)
		{
			for(bp = SiolxIrqRoot[intr_val]; bp != NULL; 
			    bp = bp->next_by_interrupt)
			{
				SiolxShutdownBoard(bp);	/* turn off int; release the plx vaddr space */
			}
			free_irq(intr_val, &SiolxIrqRoot[intr_val]); 
		}
	}

}

static void siolx_release_memory(void)
{
	register struct siolx_board *bp;
	register struct siolx_port *port;

	while(siolx_board_root)
	{
		bp = siolx_board_root;
		siolx_board_root = bp->next_by_global_list;
		siolx_release_io_range(bp); /* releases the chip vaddr */
		kfree(bp);
	}
	while(siolx_port_root)
	{
		port = siolx_port_root;
		if(port->xmit_buf)
		{		/* should have been done when port shutdown */
			free_page((unsigned long) port->xmit_buf);
			port->xmit_buf = NULL;
		}
		siolx_port_root = port->next_by_global_list;
		kfree(port);
	}
	if(siolx_table)
	{
		kfree(siolx_table);
		siolx_table = NULL;
	}
	if(siolx_termios)
	{
		kfree(siolx_termios);
		siolx_termios = NULL;
	}
	if(siolx_termios_locked)
	{
		kfree(siolx_termios_locked);
		siolx_termios_locked = NULL;
	}

#ifdef SIOLX_TIMER
	del_timer (&missed_irq_timer);
#endif
}


static void siolx_cleanup(void)
{
	siolx_release_drivers();
	siolx_release_memory();
}

/* 
 * This routine must be called by kernel at boot time 
 */

static int __init siolx_init(void) 
{
	unsigned char bus;
	unsigned char devfn;
	struct siolx_board *bp;
	struct siolx_board *bp2;
	unsigned int boardcount;
	struct pci_dev *pdev = NULL;
	unsigned int ecntl;
	unsigned int intr_val;

	printk(KERN_ALERT "aurora interea miseris mortalibus almam extulerat lucem\n");
	printk(KERN_ALERT "        referens opera atque labores\n"); 
	printk(KERN_INFO "siolx: Siolx Aurora Asynchronous Adapter driver v" VERSION ", (c) Telford Tools, Inc.\n");
#ifdef CONFIG_SIOLX_RTSCTS
	printk (KERN_INFO "siolx: DTR/RTS pin is always RTS.\n");
#else
	printk (KERN_INFO "siolx: DTR/RTS pin is RTS when CRTSCTS is on.\n");
#endif
	memset(SiolxIrqRoot, 0, sizeof(SiolxIrqRoot));
	tmp_buf = NULL;
	siolx_board_root = NULL; /* clear out the global pointers */
	siolx_board_last = NULL;
	siolx_port_root = NULL;
	siolx_port_last = NULL;
	NumSiolxPorts = 0;
	siolx_table = NULL;	/* make dynamic */
	siolx_termios = NULL;
	siolx_termios_locked = NULL;
	siolx_driver_registered = 0;
	siolx_callout_driver_registered = 0;

	boardcount = 0;
	
	if (siolx_init_drivers()) 
	{
		printk(KERN_INFO "siolx: Could not initialize drivers.\n");
		return -EIO;
	}

	if (!pci_present()) 
	{
		printk(KERN_INFO "siolx: Could not find PCI bus.\n");
		return -EIO;	/* no PCI bus no Aurora cards */
	}
	
	while(1)
	{
		pdev = pci_find_device (siolx_vendor_id, siolx_device_id, pdev);
		if (!pdev) 
		{
			break; /* found no devices */
		}

		DEBUGPRINT((KERN_ALERT "%s\n", pdev->name));
		DEBUGPRINT((KERN_ALERT "subsystem vendor is %x.\n", 
			    pdev->subsystem_vendor));
		DEBUGPRINT((KERN_ALERT "subsystem device is %x.\n", 
			    pdev->subsystem_device));
		DEBUGPRINT((KERN_ALERT 
			    "BAR0 = %lx\nBAR1 = %lx\nBAR2 = %lx\nBAR3 = %lx\nBAR4 = %lx\nBAR5 = %lx\n", 
			    pci_resource_start(pdev, 0), 
			    pci_resource_start(pdev, 1),
			    pci_resource_start(pdev, 2),
			    pci_resource_start(pdev, 3),
			    pci_resource_start(pdev, 4),
			    pci_resource_start(pdev, 5)));
		DEBUGPRINT((KERN_ALERT 
			    "LAS0 = %lx\nLAS1 = %lx\nLAS2 = %lx\nLAS3 = %lx\nLAS4 = %lx\nLAS5 = %lx\n", 
			    pci_resource_len(pdev, 0), 
			    pci_resource_len(pdev, 1),
			    pci_resource_len(pdev, 2),
			    pci_resource_len(pdev, 3),
			    pci_resource_len(pdev, 4),
			    pci_resource_len(pdev, 5)));
		
		if(pdev->subsystem_vendor == siolx_subsystem_vendor)
		{
			if(pdev->subsystem_device == siolx_subsystem_pci_device)
			{
				bp = (struct siolx_board*)kmalloc(sizeof(struct siolx_board), GFP_KERNEL);
				if(bp == NULL)
				{
					printk(KERN_ALERT "siolx: Failed to create board structure on board %d.\n", boardcount);
					break; /* no memory available */
				}
				memset(bp, 0, sizeof(struct siolx_board));
				bp->boardtype = BD_8000P;
			}
			else if(pdev->subsystem_device == siolx_subsystem_cpci_device)
			{
				bp = (struct siolx_board*)kmalloc(sizeof(struct siolx_board), GFP_KERNEL);
				if(bp == NULL)
				{
					printk(KERN_ALERT
					       "siolx: Failed to create board structure on board%p.\n", bp);
					break; /* no memory available */
				}
				memset(bp, 0, sizeof(struct siolx_board));
				bp->boardtype = BD_8000C;
			}
			else
			{
				continue;
			}
		}
		else
		{
			continue;
		}
		
		DEBUGPRINT((KERN_ALERT "siolx: interrupt is %i.\n", pdev->irq));
		bus = pdev->bus->number;
		devfn = pdev->devfn;
		DEBUGPRINT((KERN_ALERT "siolx: bus is %x, slot is %x.\n", bus, PCI_SLOT(devfn)));
		
		if (pci_enable_device(pdev))
		{
			kfree(bp);
			continue; /* enable failed */
		}
		pci_set_master(pdev);
		
		bp->irq = pdev->irq;
		SiolxResetBoard(bp, pdev); /* make sure the board is in a known state */
		if(bp->plx_vaddr == 0)
		{
			printk(KERN_ALERT "siolx: failed to remap plx address space.\n");
			kfree(bp);
			continue;
		}
		bp->vaddr = (unsigned long) ioremap_nocache(pci_resource_start(pdev, 2), 
							    pci_resource_len(pdev, 2));
		if(bp->vaddr)
		{
			bp->base = (bp->vaddr + MPASYNC_CHIP1_OFFSET);
			bp->boardnumber = boardcount;
			if (siolx_probe(bp)) /* failure is nonzero */
			{
				iounmap((void*)bp->plx_vaddr);
				bp->plx_vaddr = 0;
				iounmap((void*)bp->vaddr);
				bp->vaddr = 0;
				kfree(bp); /* something wrong with board */
				continue;
			}
			intr_val = bp->irq;
			if((intr_val < 0) || (intr_val >= SIOLX_NUMINTS))
			{
				printk(KERN_ALERT "siolx:  bad interrupt %i board %p.\n", intr_val, bp);
				iounmap((void*)bp->plx_vaddr); /* but plx space was remapped */
				bp->plx_vaddr = 0;
				iounmap((void*)bp->vaddr); /* release chip space */
				bp->vaddr = 0;
				kfree(bp); /* release the board structure */
				continue;
			}
			bp->next_by_interrupt = SiolxIrqRoot[intr_val];
			SiolxIrqRoot[intr_val] = bp;
			if(siolx_board_last == NULL)
			{
				siolx_board_root = bp;
				siolx_board_last = bp;
			}
			else
			{
				siolx_board_last->next_by_global_list = bp;
				siolx_board_last = bp;
			}
			bp->chipnumber = 0;
			bp->intstatus = bp->plx_vaddr + PLX_ICSR;
			bp->next_by_chain = bp; /* one item chain */
			ecntl = readl(bp->plx_vaddr + PLX_ECNTL);
			boardcount++;	/* added a board */
			if(pci_resource_len(pdev, 2) > MPASYNC_CHIP2_OFFSET)
			{
				++(bp->boardtype); /* works because how types are defined 8000X --> 16000X*/
				if(bp->boardtype == BD_16000C)
				{
					if((ecntl & PLX_ECNTLUSERI) == 0)
					{
						bp->reario = 1;
					}
				}
				bp2 = (struct siolx_board*)kmalloc(sizeof(struct siolx_board), GFP_KERNEL);
				if(bp2 == NULL)
				{
					printk(KERN_ALERT
					       "siolx: Failed to create second board structure on board %p.\n", bp);
					/* fall through because must turn on ints for other chip */
				}
				else
				{
					memset(bp2, 0, sizeof(struct siolx_board)); /* unnecessary */
					*bp2 = *bp; /* note that all guys in chain point to same next_by interrupt */
					bp->next_by_chain = bp2; /* circular list */
					bp2->next_by_chain = bp;/*  now chain two elements*/
					++(bp2->chipnumber); /* chipnumber 1 */
					bp2->base = (bp2->vaddr + MPASYNC_CHIP2_OFFSET);
					if(siolx_probe(bp2))
					{
						printk(KERN_ALERT "siolx: Failed to probe second board structure on board %p.\n", bp);
						kfree(bp2);
						/* fall through because must turn on ints for other chip */
						/* don't release pci memory remap -- still works for other chip */
					}
					else if(siolx_board_last == NULL)
					{
						siolx_board_root = bp2; /* this case should not occur */
						siolx_board_last = bp2;
					}
					else
					{
						siolx_board_last->next_by_global_list = bp2;
						siolx_board_last = bp2;
					}
					/* don't increment boardnumber */
				}
			}
		}
		else		/* could not remap the cd18xx space */
		{
			iounmap((void*)bp->plx_vaddr); /* but plx space was remapped */
			bp->plx_vaddr = 0;
			kfree(bp);
		}
	}
	if (boardcount == 0) 
	{
		printk(KERN_INFO "siolx: No Aurora Asynchronous Adapter boards detected.\n");
		siolx_cleanup(); /* don't need any allocated memory */
		return -EIO;
	}
	if (siolx_finish_init_drivers()) 
	{
		printk(KERN_INFO "siolx: Could not finish driver initialization.\n");
		siolx_cleanup();
		return -EIO;
	}

	for(intr_val = 0; intr_val < SIOLX_NUMINTS; ++intr_val) /* trying to install as few int handlers as possible */
	{	     /* one for each group of boards (actually chips) on a given irq */
		if(SiolxIrqRoot[intr_val] != NULL)
		{
			if (request_irq(intr_val, siolx_interrupt, SA_SHIRQ, "siolx Aurora Asynchronous Adapter",
					&SiolxIrqRoot[intr_val]) == 0) 
				/* interrupts on perboard basis
				 * cycle through chips and then
				 * ports */
				/* NOTE PLX INTS ARE OFF -- so turn them on */
			{
				for(bp = SiolxIrqRoot[intr_val]; bp != NULL; bp = bp->next_by_interrupt)
				{
					writel(PLX_ICSRLCLINTPCI | PLX_ICSRPCIINTS, bp->plx_vaddr + PLX_ICSR); /* enable interrupts */
				}
			}
			else
			{
				printk(KERN_ALERT "siolx:  Unable to get interrupt, board set up not complete %i.\n", intr_val);
				/* no interrupts but on all lists */
			}
		}
	}
	return 0;
}

module_init(siolx_init);
module_exit(siolx_cleanup);
MODULE_DESCRIPTION("multiport Aurora asynchronous driver");
MODULE_AUTHOR("Joachim Martillo <martillo@telfordtools.com>");
MODULE_LICENSE("GPL");
