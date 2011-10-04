/* -*- linux-c -*- */
/*
 *	This file was modified from
 *      linux/drivers/char/siolx_io8.h  -- 
 *                                   Siolx IO8+ multiport serial driver.
 *
 *      Copyright (C) 1997 Roger Wolff (R.E.Wolff@BitWizard.nl)
 *      Copyright (C) 1994-1996  Dmitry Gorodchanin (pgmdsg@ibi.com)
 *	Modifications (C) 2002 Telford Tools, Inc. (martillo@telfordtools.com)
 *
 *
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
 * */

#ifndef __LINUX_SIOLX_H
#define __LINUX_SIOLX_H

#include <linux/serial.h>

#ifdef __KERNEL__

#define SIOLX_NBOARD		8

/* eight ports per chip. */
#define SIOLX_NPORT        	8
#define SIOLX_PORT(line)	((line) & (SIOLX_NPORT - 1))

#define MHz *1000000	/* I'm ashamed of myself. */

/* On-board oscillator frequency */
#define SIOLX_OSCFREQ      (33 MHz) 
/* oregano is in /1 which mace 66Mhz is in /2 mode */

/* Ticks per sec. Used for setting receiver timeout and break length */
#define SIOLX_TPS		4000

/* Yeah, after heavy testing I decided it must be 6.
 * Sure, You can change it if needed.
 */
#define SIOLX_RXFIFO		6	/* Max. receiver FIFO size (1-8) */

#define SIOLX_MAGIC		0x0907

#define SIOLX_CCR_TIMEOUT 	10000   /* CCR timeout. You may need to wait upto
					   10 milliseconds before the internal
					   processor is available again after
					   you give it a command */
#define SIOLX_NUMINTS 		32

struct siolx_board 
{
	unsigned long   	flags;
	unsigned long		base;
	unsigned char 		irq;
	unsigned char		DTR;
	unsigned long		vaddr;
	unsigned long		plx_vaddr;
	unsigned long		intstatus;
	struct siolx_board 	*next_by_chain;	/* chains are circular */
	struct siolx_board	*next_by_interrupt; /* only chip 0 */
	struct siolx_board	*next_by_global_list; /*  all boards not circular */
	struct siolx_port	*portlist;
	struct pci_dev 		pdev;
	unsigned int		chipnumber; /* for 8000X this structure really defines the board
					     * for 16000X the chain corresponds to a board and each
					     * structure corresponds to a dhip on a single board */
	unsigned int		boardnumber; /* same for all boards/chips in a board chain */
	unsigned int		boardtype;
	unsigned int		chiptype;
	unsigned int		chiprev;
	unsigned int		reario;
	unsigned int		rj45;
};

#define DRIVER_DEBUG() (siolx_debug)
#define DEBUGPRINT(arg) if(DRIVER_DEBUG()) printk arg

struct siolx_port 
{
	int			magic;
	int			baud_base;
	int			flags;
	struct tty_struct 	* tty;
	int			count;
	int			blocked_open;
	int			event;
	int			timeout;
	int			close_delay;
	long			session;
	long			pgrp;
	unsigned char 		* xmit_buf;
	int			custom_divisor;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
	struct termios          normal_termios;
	struct termios		callout_termios;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	struct tq_struct	tqueue;
	struct tq_struct	tqueue_hangup;
	short			wakeup_chars;
	short			break_length;
	unsigned short		closing_wait;
	unsigned char		mark_mask;
	unsigned char		IER;
	unsigned char		MSVR;
	unsigned char		COR2;
#ifdef SIOLX_REPORT_OVERRUN
	unsigned long		overrun;
#endif	
#ifdef SIOLX_REPORT_FIFO
	unsigned long		hits[10];
#endif
	struct siolx_port	*next_by_global_list;
	struct siolx_port	*next_by_board;
	struct siolx_board	*board;
	unsigned int		boardport; /* relative to chain 0-15 for 16000X */
	unsigned int		driverport; /* maps to minor device number */
};

#endif /* __KERNEL__ */
#endif /* __LINUX_SIOLX_H */
