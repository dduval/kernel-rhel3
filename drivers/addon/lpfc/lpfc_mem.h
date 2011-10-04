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
 * $Id: lpfc_mem.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef _H_LPFC_MEM
#define _H_LPFC_MEM


struct lpfc_dmabuf {
	struct list_head list;
	void *virt;		/* virtual address ptr */
	dma_addr_t phys;	/* mapped address */
};
typedef struct lpfc_dmabuf DMABUF_t;

struct lpfc_dmabufext {
	DMABUF_t dma;
	uint32_t size;
	uint32_t flag;
};
typedef struct lpfc_dmabufext DMABUFEXT_t;

struct lpfc_dmabufip {
	DMABUF_t dma;
	struct sk_buff *ipbuf;
};
typedef struct lpfc_dmabufip DMABUFIP_t;

struct lpfc_dma_pool {
	DMABUF_t   *elements;
	uint32_t    max_count;
	uint32_t    current_count;
}; 

struct lpfc_mem_pool {
	void     **saftey_mempool_pages;
	uint32_t   page_count;
	uint32_t   max_count;
	uint32_t   curr_count;
	struct list_head obj_list;
};

#define MEM_PRI             0x100	/* Priority bit: set to exceed low
					   water */


#define LPFC_MEM_ERR          0x1	/* return error memflag */
#define LPFC_MEM_GETMORE      0x2	/* get more memory memflag */
#define LPFC_MEM_DMA          0x4	/* blocks are for DMA */
#define LPFC_MEM_LOWHIT       0x8	/* low water mark was hit */
#define LPFC_MEMPAD           0x10	/* offset used for a FC_MEM_DMA
					   buffer */
#define LPFC_MEM_ATTACH_IPBUF 0x20	/* attach a system IP buffer */
#define LPFC_MEM_BOUND        0x40	/* has a upper bound */


#define LPFC_PAGE_POOL_SIZE      4      /* max elements in page saftey pool */
#define LPFC_MBUF_POOL_SIZE     64      /* max elements in MBUF saftey pool */
#define LPFC_MEM_POOL_SIZE      64      /* max elements in non DMA saftey
					   pool */
#define LPFC_MEM_POOL_PAGE_SIZE 8192    
#define LPFC_MEM_POOL_OBJ_SIZE  ((sizeof(LPFC_MBOXQ_t) + 0x7) & (~0x7))

#endif				/* _H_LPFC_MEM */
