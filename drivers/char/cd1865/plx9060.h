#ifndef _PLX9060_H_
#define _PLX9060_H_
/*
 * Aurora Cirrus CL-CD180/1865 Async Driver (sio16)
 *
 * This module contains the definitions for the PLX
 *  9060SD PCI controller chip.
 *
 * COPYRIGHT (c) 1996-1998 BY AURORA TECHNOLOGIES, INC., WALTHAM, MA.
 * Modifications Copyright (C) 2002 By Telford Tools, Inc., Boston, MA.
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
 *
 *	file: plx9060.h
 *	author: cmw
 *	created: 11/21/1996
 *	info: $Id: plx9060.h,v 1.2 2002/06/11 02:50:02 martillo Exp $
 */

/*
 * $Log: plx9060.h,v $
 * Revision 1.2  2002/06/11 02:50:02  martillo
 * using silx_ and SILX_ instead of sx_ and SX_
 *
 * Revision 1.1  2002/05/21 17:30:16  martillo
 * first pass for the sio16 driver.
 *
 * Revision 1.4  1999/02/12 15:38:13  bkd
 * Changed PLX_ECNTUSER0 to PLX_ECNTLUSERO and added PLX_ECNTLUSERI.
 *
 * Revision 1.3  1998/03/23 19:35:42  bkd
 * Added definitions for all of the missing PLX9060SD registers.
 *
 * Revision 1.2  1998/03/13 21:02:16  bkd
 * Updated copyright date to include 1998.
 *
 * Revision 1.1  1996/11/23 01:07:46  bkd
 * cmw/bkd (PCI port):
 * Initial check-in.
 *
 */

/*
 * Register definitions
 */

#define PLX_LAS0RR	0x00
#define PLX_LAS0BAR	0x04
#define PLX_LAR		0x08
#define PLX_ENDR	0x0c
#define PLX_EROMRR	0x10
#define PLX_EROMBAR	0x14
#define PLX_LAS0BRD	0x18
#define PLX_LAS1RR	0x30
#define PLX_LAS1BAR	0x34
#define PLX_LAS1BRD	0x38

#define PLX_MBR0	0x40
#define PLX_MBR1	0x44
#define PLX_MBR2	0x48
#define PLX_MBR3	0x4c
#define PLX_PCI2LCLDBR	0x60
#define PLX_LCL2PCIDBR	0x64
#define	PLX_ICSR	0x68
#define	PLX_ECNTL	0x6c

/*
 * Bit definitions
 */

#define	PLX_ECNTLUSERO	    0x00010000	/* turn on user output */
#define PLX_ECNTLUSERI	    0x00020000	/* user input */
#define	PLX_ECNTLLDREG	    0x20000000	/* reload configuration registers */
#define	PLX_ECNTLLCLRST	    0x40000000	/* local bus reset */
#define	PLX_ECNTLINITSTAT   0x80000000	/* mark board init'ed */


#define	PLX_ICSRLSERR_ENA   0x00000001	/* enable local bus LSERR# */
#define	PLX_ICSRLSERRP_ENA  0x00000002	/* enable local bus LSERR# PCI */
#define	PLX_ICSRPCIINTS	    0x00000100	/* enable PCI interrupts */
#define PLX_ICSRLCLINTPCI   0x00000800
#define	PLX_ICSRINTACTIVE   0x00008000	/* RO: local interrupt active */

#endif
