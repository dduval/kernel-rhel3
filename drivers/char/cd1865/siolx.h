/* -*- linux-c -*- */
#ifndef _SIOLX_H_
#define _SIOLX_H_

/*
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
 */

#define AURASUBSYSTEM_VENDOR_ID		0x125c
#define AURASUBSYSTEM_MPASYNCPCI	0x0640
#define AURASUBSYSTEM_MPASYNCcPCI	0x0641

/*
 * Aurora Cirrus CL-CD180/1865 Async Driver (sio16)
 *
 */

/*
 * Register sets.  These must match the order of the registers specified
 * in the prom on the  board!
 */

#define MPASYNC_REG_CSR			1
#define MPASYNC_REG_CD			2

#define MPASYNC_CHIP1_OFFSET		0x080
#define MPASYNC_CHIP2_OFFSET		0x100

#define MPASYNC_REG_NO_OBP_CSR		1
#define MPASYNC_REG_NO_OBP_CD		3

#define TX_FIFO		0x8		/* how deep is the chip fifo */

/*
 * state flags
 */

/*
 * the following defines the model types
 */

#define OREGANO_MODEL(mod)	((mod) == BD_16000P || (mod) == BD_8000P)
#define MACE_MODEL(mod)		((mod) == BD_16000C || (mod) == BD_8000C)

/*
 * I/O options:
 */

#define MACE8_STD		0x0		/* 8000CP -- standard I/O */
#define MACE8_RJ45		0x1		/* 8000CP -- rear RJ45 I/O */

#define MACE16_STD		0x0		/* 16000CP -- standard I/O */
#define MACE16_RJ45		0x1		/* 16000CP -- rear RJ45 I/O */

#define SE2_CLK	((unsigned int) 11059200)	/* 11.0592 MHz */
#define SE_CLK	((unsigned int) 14745600)	/* 14.7456 MHz */
#define SE3_CLK	((unsigned int) 33000000)	/* 33.3333 MHz */

/* divide x by y, rounded */
#define ROUND_DIV(x, y)		(((x) + ((y) >> 1)) / (y))

/* Calculate a 16 bit baud rate divisor for the given "encoded"
 *  (multiplied by two) baud rate.
 */

/* chip types: */
#define CT_UNKNOWN	0x0	/* unknown */
#define CT_CL_CD180	0x1	/* Cirrus Logic CD-180 */
#define CT_CL_CD1864	0x2	/* Cirrus Logic CD-1864 */
#define CT_CL_CD1865	0x3	/* Cirrus Logic CD-1864 */

/* chip revisions: */
#define CR_UNKNOWN	0x0	/* unknown */
#define CR_REVA		0x1	/* revision A */
#define CR_REVB		0x2	/* revision B */
#define CR_REVC		0x3	/* revision C */
/* ...and so on ... */

#endif
