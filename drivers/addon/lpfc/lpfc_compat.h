/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_compat.h 328 2005-05-03 15:20:43Z sf_support $
 *
 * This file provides macros to aid compilation in the Linux 2.4 kernel
 * over various platform architectures.
 */

#ifndef _H_LPFC_COMPAT
#define  _H_LPFC_COMPAT

/*******************************************************************
Note: dma_mapping_error() was added in 2.6.5 so 
provide dma_mapping_error() for earlier kernels.
This version is from asm-ppc64/dma-mapping.h but will also fit with
asm-i386/dma-mapping.h since that version always returns 0.
 *******************************************************************/

#ifndef DMA_ERROR_CODE
#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)
#endif

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return (dma_addr == DMA_ERROR_CODE);
}


/*******************************************************************
Note: HBA's SLI memory contains little-endian LW.
Thus to access it from a little-endian host,
memcpy_toio() and memcpy_fromio() can be used.
However on a big-endian host, copy 4 bytes at a time,
using writel() and readl().
 *******************************************************************/

#if __BIG_ENDIAN

static inline void
lpfc_memcpy_to_slim( void *dest, void *src, unsigned int bytes)
{
	uint32_t *dest32;
	uint32_t *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t *) dest;
	src32  = (uint32_t *) src;

	/* write input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		writel( *src32, dest32);
		readl(dest32); /* flush */
		dest32++;
		src32++;
	}

	return;
}

static inline void
lpfc_memcpy_from_slim( void *dest, void *src, unsigned int bytes)
{
	uint32_t *dest32;
	uint32_t *src32;
	unsigned int four_bytes;


	dest32  = (uint32_t *) dest;
	src32  = (uint32_t *) src;

	/* read input bytes, 4 bytes at a time */
	for (four_bytes = bytes /4; four_bytes > 0; four_bytes--) {
		*dest32 = readl( src32);
		dest32++;
		src32++;
	}

	return;
}

#else

static inline void
lpfc_memcpy_to_slim( void *dest, void *src, unsigned int bytes)
{
	/* actually returns 1 byte past dest */
	memcpy_toio( dest, src, bytes);
}

static inline void
lpfc_memcpy_from_slim( void *dest, void *src, unsigned int bytes)
{
	/* actually returns 1 byte past dest */
	memcpy_fromio( dest, src, bytes);
}

#endif /* __BIG_ENDIAN */


/*******************************************************************
Indicate whether "SLIM POINTER" feature can be used to set 
"Host Group Ring Pointers" to point to HBA SLIM area.

PowerPC cannot use this feature, so it must use the host memory
instead. Likewise for SPARC SBUS.
 *******************************************************************/

#ifdef CONFIG_PPC64
#define USE_HGP_HOST_SLIM	1
#endif



/* Linux kernels before 2.4.23 did not provide these definitions */
#ifndef IRQ_NONE
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#endif				/* ifndef IRQ_NONE */

#endif				/*  _H_LPFC_COMPAT */
