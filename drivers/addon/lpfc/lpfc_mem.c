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
 * $Id: lpfc_mem.c 328 2005-05-03 15:20:43Z sf_support $
 */

#include <linux/version.h>
#include <linux/pci.h>
#include <linux/blk.h>
#include <scsi.h>


#include "lpfc_hw.h"
#include "lpfc_mem.h"
#include "lpfc_sli.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_scsi.h"
#include "lpfc_crtn.h"

void lpfc_mem_saftey_pool_create(lpfcHBA_t *, uint32_t );
void lpfc_mem_saftey_pool_destroy(lpfcHBA_t *);
void lpfc_mbuf_saftey_pool_create(lpfcHBA_t *, uint32_t );
void lpfc_mbuf_saftey_pool_destroy(lpfcHBA_t *);
void lpfc_page_saftey_pool_create(lpfcHBA_t *, uint32_t );
void lpfc_page_saftey_pool_destroy(lpfcHBA_t *);
void *lpfc_mbuf_alloc(lpfcHBA_t *, int, dma_addr_t *);
void lpfc_mbuf_free(lpfcHBA_t *, void *, dma_addr_t);
void *lpfc_page_alloc(lpfcHBA_t *, int, dma_addr_t *);
void lpfc_page_free(lpfcHBA_t *, void *, dma_addr_t);

static void lpfc_mem_saftey_pool_free(lpfcHBA_t *, void *);
static void *lpfc_mem_saftey_pool_alloc(lpfcHBA_t *);
static uint32_t lpfc_mem_saftey_pool_check(lpfcHBA_t *, void *);

int
lpfc_mem_alloc(lpfcHBA_t * phba)
{

	phba->lpfc_scsi_dma_ext_pool = 0;
	phba->lpfc_mbuf_pool = 0;
	phba->lpfc_page_pool = 0;

	phba->lpfc_scsi_dma_ext_pool = 
		pci_pool_create("lpfc_scsi_dma_ext_pool", 
				phba->pcidev, 
				LPFC_SCSI_DMA_EXT_SIZE, 
				8,
				0, 
				GFP_KERNEL);

	phba->lpfc_mbuf_pool = 
		pci_pool_create("lpfc_mbuf_pool", 
				phba->pcidev, 
				LPFC_BPL_SIZE, 
				8,
				0, 
				GFP_KERNEL);

	phba->lpfc_page_pool = 
		pci_pool_create("lpfc_page_pool", 
				phba->pcidev, 
				LPFC_SCSI_PAGE_BUF_SZ, 
				8,
				0, 
				GFP_KERNEL);


	if ((!phba->lpfc_scsi_dma_ext_pool) || (!phba->lpfc_mbuf_pool) ||
	   (!phba->lpfc_page_pool)) {
		lpfc_mem_free(phba);
		return (0);
	}

	lpfc_mbuf_saftey_pool_create(phba, LPFC_MBUF_POOL_SIZE);
	lpfc_page_saftey_pool_create(phba, LPFC_PAGE_POOL_SIZE);
	lpfc_mem_saftey_pool_create(phba, LPFC_MEM_POOL_SIZE);

	return (1);
}

int
lpfc_mem_free(lpfcHBA_t * phba)
{
	LPFC_MBOXQ_t *mbox;
	LPFC_SLI_t *psli;
	struct list_head *curr, *next;

	/* free the mapped address match area for each ring */
	psli = &phba->sli;


	/* Free everything on mbox queue */
	list_for_each_safe(curr, next, &psli->mboxq) {
		mbox = list_entry(curr, LPFC_MBOXQ_t, list);
		list_del_init(&mbox->list);
		lpfc_mbox_free(phba, mbox);
	}

	psli->sliinit.sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	if (psli->mbox_active) {
		lpfc_mbox_free(phba, psli->mbox_active);
		psli->mbox_active = 0;
	}

	lpfc_mbuf_saftey_pool_destroy(phba);
	lpfc_page_saftey_pool_destroy(phba);
	lpfc_mem_saftey_pool_destroy(phba);

	if (phba->lpfc_scsi_dma_ext_pool)
		pci_pool_destroy(phba->lpfc_scsi_dma_ext_pool);

	if (phba->lpfc_mbuf_pool)
		pci_pool_destroy(phba->lpfc_mbuf_pool);

	if (phba->lpfc_page_pool)
		pci_pool_destroy(phba->lpfc_page_pool);

	return (1);
}

void
lpfc_mem_saftey_pool_create(lpfcHBA_t *phba, uint32_t count)
{

	int number_of_pages;
	int i, j;
	struct lpfc_mem_pool *pool = &(phba->lpfc_mem_saftey_pool);
	void *obj_ptr;

	pool->page_count = 0;
	pool->max_count  = 0;
	pool->curr_count = 0;

	number_of_pages =  LPFC_MEM_POOL_PAGE_SIZE / LPFC_MEM_POOL_OBJ_SIZE ;
	number_of_pages = count / number_of_pages + 1;

	pool->saftey_mempool_pages = kmalloc( number_of_pages * sizeof(void *),
					      GFP_KERNEL);
	INIT_LIST_HEAD(&(pool->obj_list));

	if (!pool->saftey_mempool_pages)
		return;

	for (i=0; i<number_of_pages; i++) {
		pool->saftey_mempool_pages[i] = kmalloc(LPFC_MEM_POOL_PAGE_SIZE,
							GFP_KERNEL);

		if (!pool->saftey_mempool_pages[i])
			return;

		pool->page_count++;
		obj_ptr = pool->saftey_mempool_pages[i];

		for ( j=0; j < LPFC_MEM_POOL_PAGE_SIZE/LPFC_MEM_POOL_OBJ_SIZE;
		      j++) {
			list_add((struct list_head *) obj_ptr,
				 &(pool->obj_list));
			pool->max_count++;
			pool->curr_count++;
			obj_ptr += LPFC_MEM_POOL_OBJ_SIZE;
		}
	}
	return;
}


void
lpfc_mem_saftey_pool_destroy(lpfcHBA_t *phba) {


	struct lpfc_mem_pool *pool = &(phba->lpfc_mem_saftey_pool);
	int i;

	if ( pool->max_count != pool->curr_count )
		printk("Memory leaked in lpfc memory saftey pool \n");

	/* Make the object list empty */
	INIT_LIST_HEAD(&(pool->obj_list));

	/* Free the pages */
	for (i=0; i<pool->page_count; i++)
		kfree(pool->saftey_mempool_pages[i]);

	kfree(pool->saftey_mempool_pages);
	return;
}

void
lpfc_mem_saftey_pool_free(lpfcHBA_t *phba, void *obj) {
	struct lpfc_mem_pool *pool = &(phba->lpfc_mem_saftey_pool);
	if(!obj)
		return;

	list_add((struct list_head *) obj,
		 &(pool->obj_list));
	pool->curr_count++;
	return;
}

void *
lpfc_mem_saftey_pool_alloc(lpfcHBA_t *phba) {
	struct lpfc_mem_pool *pool = &(phba->lpfc_mem_saftey_pool);
	void *ret = 0;

	if ( (pool->curr_count) && 
	     (!list_empty(&(pool->obj_list)))) {
		ret = (void *) pool->obj_list.next ;
		list_del(pool->obj_list.next);
		pool->curr_count--;
	}
	return ret;
}

uint32_t
lpfc_mem_saftey_pool_check(lpfcHBA_t *phba, void *obj) {
	unsigned long address = (unsigned long) obj;
	unsigned long page_addr;
	int i;
	struct lpfc_mem_pool *pool = &(phba->lpfc_mem_saftey_pool);

	if ( pool->curr_count == pool->max_count )
		return 0;

	for (i=0; i< pool->page_count; i++) {
		page_addr = (unsigned long) pool->saftey_mempool_pages[i];
		if ((page_addr < address) &&
		    (page_addr + LPFC_MEM_POOL_PAGE_SIZE > address))
			return 1;
	}

	return 0;
}

void
lpfc_mbuf_saftey_pool_create(lpfcHBA_t * phba, uint32_t count)
{
	int i;

	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_mbuf_saftey_pool);

	pool->elements = kmalloc(sizeof(DMABUF_t) * count,
				 GFP_KERNEL);
	pool->max_count = 0;
	pool->current_count = 0;
	for ( i=0; i<count; i++) {
		pool->elements[i].virt =
			pci_pool_alloc(phba->lpfc_mbuf_pool, 
				       GFP_KERNEL, 
				       &(pool->elements[i].phys));

		if (!pool->elements[i].virt)
			break;
		pool->max_count++;
		pool->current_count++;
	}
}
 
void
lpfc_mbuf_saftey_pool_destroy(lpfcHBA_t * phba)
{
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_mbuf_saftey_pool);
	int i;

	if ( pool->max_count != pool->current_count)
		printk("Memory leaked in mbuf saftey pool \n");

	for (i=0; i< pool->current_count; i++) {
		pci_pool_free(phba->lpfc_mbuf_pool, 
			      pool->elements[i].virt,
			      pool->elements[i].phys);
	}

	kfree(pool->elements);
}


void
lpfc_page_saftey_pool_create(lpfcHBA_t * phba, uint32_t count)
{
	int i;

	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_page_saftey_pool);

	pool->elements = kmalloc(sizeof(DMABUF_t) * count,
				 GFP_KERNEL);
	pool->max_count = 0;
	pool->current_count = 0;
	for ( i=0; i<count; i++) {
		pool->elements[i].virt =
			pci_pool_alloc(phba->lpfc_page_pool, 
				       GFP_KERNEL, 
				       &(pool->elements[i].phys));

		if (!pool->elements[i].virt)
			break;
		pool->max_count++;
		pool->current_count++;
	}
}
 
void
lpfc_page_saftey_pool_destroy(lpfcHBA_t * phba)
{
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_page_saftey_pool);
	int i;

	if ( pool->max_count != pool->current_count)
		printk("Memory leaked in page saftey pool \n");

	for (i=0; i< pool->current_count; i++) {
		pci_pool_free(phba->lpfc_page_pool, 
			      pool->elements[i].virt,
			      pool->elements[i].phys);
	}

	kfree(pool->elements);
}

void *
lpfc_mbuf_alloc(lpfcHBA_t * phba, int mem_flags, dma_addr_t * handle)
{
	void *ret;
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_mbuf_saftey_pool);

	ret = pci_pool_alloc(phba->lpfc_mbuf_pool, GFP_ATOMIC, handle);
	/* 
	   If we are low in memory and is priority memory allocation
	   use saftey pool 
	*/
	if ((!ret) && ( mem_flags & MEM_PRI) 
	    && (pool->current_count)) {
		pool->current_count--;
		ret = pool->elements[pool->current_count].virt;
		*handle = pool->elements[pool->current_count].phys;
	}
	return ret;
}

void
lpfc_mbuf_free(lpfcHBA_t * phba, void *virt, dma_addr_t dma)
{
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_mbuf_saftey_pool);

	if ( pool->current_count < pool->max_count) {
		pool->elements[pool->current_count].virt = virt;
		pool->elements[pool->current_count].phys = dma;
		pool->current_count++;
		return;
	}

	pci_pool_free(phba->lpfc_mbuf_pool, virt, dma);
	return;
}

void *
lpfc_page_alloc(lpfcHBA_t * phba, int mem_flags, dma_addr_t * handle)
{
	void *ret;
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_page_saftey_pool);

	ret = pci_pool_alloc(phba->lpfc_page_pool, GFP_ATOMIC, handle);
	/* 
	   If we are low in memory and is priority memory allocation
	   use saftey pool 
	*/
	if ((!ret) && ( mem_flags & MEM_PRI) 
	    && (pool->current_count)) {
		pool->current_count--;
		ret = pool->elements[pool->current_count].virt;
		*handle = pool->elements[pool->current_count].phys;
	}
	return ret;
}

void
lpfc_page_free(lpfcHBA_t * phba, void *virt, dma_addr_t dma)
{
	struct lpfc_dma_pool *pool = 
		&(phba->lpfc_page_saftey_pool);

	if ( pool->current_count < pool->max_count) {
		pool->elements[pool->current_count].virt = virt;
		pool->elements[pool->current_count].phys = dma;
		pool->current_count++;
		return;
	}

	pci_pool_free(phba->lpfc_page_pool, virt, dma);
	return;
}

LPFC_MBOXQ_t *
lpfc_mbox_alloc(lpfcHBA_t * phba, int mem_flags)
{
	LPFC_MBOXQ_t *ret;

	ret = (LPFC_MBOXQ_t *) kmalloc(sizeof (LPFC_MBOXQ_t), GFP_ATOMIC);

	/* if kmalloc fails and is a priority allocation use saftey pool */
	if ((!ret) && (mem_flags & MEM_PRI))
		ret = lpfc_mem_saftey_pool_alloc(phba);

	return ret;
}

void
lpfc_mbox_free(lpfcHBA_t * phba, LPFC_MBOXQ_t * virt)
{
	/* Check if the object belongs to saftey pool */
	if (lpfc_mem_saftey_pool_check(phba,virt)) {
		lpfc_mem_saftey_pool_free(phba,virt);
		return;
	}

	kfree(virt);
	return;
}


LPFC_IOCBQ_t *
lpfc_iocb_alloc(lpfcHBA_t * phba, int mem_flags)
{
	LPFC_IOCBQ_t *ret;
	ret = (LPFC_IOCBQ_t *) kmalloc(sizeof (LPFC_IOCBQ_t), GFP_ATOMIC);
	
	/* if kmalloc fails and is a priority allocation use saftey pool */
	if ((!ret) && (mem_flags & MEM_PRI))
		ret = lpfc_mem_saftey_pool_alloc(phba);
	return ret;
}

void
lpfc_iocb_free(lpfcHBA_t * phba, LPFC_IOCBQ_t * virt)
{

	/* Check if the object belongs to saftey pool */
	if (lpfc_mem_saftey_pool_check(phba,virt)) {
		lpfc_mem_saftey_pool_free(phba,virt);
		return;
	}

	kfree(virt);
	return;
}


LPFC_NODELIST_t *
lpfc_nlp_alloc(lpfcHBA_t * phba, int mem_flags)
{
	LPFC_NODELIST_t *ret;

	ret = (LPFC_NODELIST_t *)
		kmalloc(sizeof (LPFC_NODELIST_t), GFP_ATOMIC);

	/* if kmalloc fails and is a priority allocation use saftey pool */
	if ((!ret) && (mem_flags & MEM_PRI))
		ret = lpfc_mem_saftey_pool_alloc(phba);
	return ret;
}

void
lpfc_nlp_free(lpfcHBA_t * phba, LPFC_NODELIST_t * virt)
{

	/* Check if the object belongs to saftey pool */
	if (lpfc_mem_saftey_pool_check(phba,virt)) {
		lpfc_mem_saftey_pool_free(phba,virt);
		return;
	}

	kfree(virt);
	return;
}

LPFC_BINDLIST_t *
lpfc_bind_alloc(lpfcHBA_t * phba, int mem_flags)
{
	LPFC_BINDLIST_t *ret;

	ret = (LPFC_BINDLIST_t *)
		kmalloc(sizeof (LPFC_BINDLIST_t), GFP_ATOMIC);

	/* if kmalloc fails and is a priority allocation use saftey pool */
	if ((!ret) && (mem_flags & MEM_PRI))
		ret = lpfc_mem_saftey_pool_alloc(phba);
	return ret;
}

void
lpfc_bind_free(lpfcHBA_t * phba, LPFC_BINDLIST_t * virt)
{
	/* Check if the object belongs to saftey pool */
	if (lpfc_mem_saftey_pool_check(phba,virt)) {
		lpfc_mem_saftey_pool_free(phba,virt);
		return;
	}

	kfree(virt);
	return;
}

