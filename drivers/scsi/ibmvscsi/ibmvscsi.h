/* ------------------------------------------------------------
 * ibmvscsi.h
 * (C) Copyright IBM Corporation 1994, 2003
 * Authors: Colin DeVilbiss (devilbis@us.ibm.com)
 *          Santiago Leon (santil@us.ibm.com)
 *          Dave Boutcher (sleddog@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * Emulation of a SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */
#ifndef IBMVSCSI_H
#define IBMVSCSI_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>   
#include <linux/string.h>    
#include <linux/errno.h>     
#include <linux/init.h>
#include <linux/module.h>    
#include <linux/blkdev.h>    
#include <linux/interrupt.h> 
#include <scsi/scsi.h>
#include "../scsi.h"
#include "../hosts.h"
#include "viosrp.h"

/* For 2.6.x compatibility */
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)

struct device;

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)
#define DMA_BIDIRECTIONAL       PCI_DMA_BIDIRECTIONAL
#define DMA_TO_DEVICE           PCI_DMA_TODEVICE
#define DMA_FROM_DEVICE         PCI_DMA_FROMDEVICE
#define DMA_NONE                PCI_DMA_NONE

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return (dma_addr == DMA_ERROR_CODE);
}

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
			 int flag);

void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		       dma_addr_t dma_handle);

dma_addr_t dma_map_single(struct device *dev, void *cpu_addr, size_t size,
			  int direction);


void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		      int direction);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       int direction);

/**
 * Work out the number of scatter/gather buffers we support
 */
static const struct SRP_CMD *fake_srp_cmd = NULL;
enum {
	MAX_INDIRECT_BUFS = (sizeof(fake_srp_cmd->additional_data) -
			     sizeof(struct indirect_descriptor)) /
	    sizeof(struct memory_descriptor)
};

/* ------------------------------------------------------------
 * Data Structures
 */
/* an RPA command/response transport queue */
struct crq_queue {
	struct VIOSRP_CRQ *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

/* a unit of work for the hosting partition */
struct srp_event_struct {
	union VIOSRP_IU *evt;
	struct scsi_cmnd *cmnd;
	struct list_head list;
	void (*done) (struct srp_event_struct *);
	struct VIOSRP_CRQ crq;
	struct ibmvscsi_host_data *hostdata;
	atomic_t in_use;
	struct SRP_CMD cmd;
	void (*cmnd_done) (struct scsi_cmnd *);
	struct completion comp;
};

/* a pool of event structs for use */
struct event_pool {
	struct srp_event_struct *events;
	u32 size;
	int next;
	union VIOSRP_IU *iu_storage;
	dma_addr_t iu_token;
};

/* all driver data associated with a host adapter */
struct ibmvscsi_host_data {
	atomic_t request_limit;
	struct device *dev;
	struct event_pool pool;
	struct crq_queue queue;
	struct tasklet_struct srp_task;
	struct list_head sent;
	struct Scsi_Host *host;
	struct MAD_ADAPTER_INFO_DATA madapter_info;
	struct list_head hostlist;
};

/* routines for managing a command/response queue */
int ibmvscsi_init_crq_queue(struct crq_queue *queue,
			    struct ibmvscsi_host_data *hostdata,
			    int max_requests);
void ibmvscsi_release_crq_queue(struct crq_queue *queue,
				struct ibmvscsi_host_data *hostdata,
				int max_requests);
void ibmvscsi_reset_crq_queue(struct crq_queue *queue,
			      struct ibmvscsi_host_data *hostdata);

void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq,
			 struct ibmvscsi_host_data *hostdata);
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata,
		      u64 word1, u64 word2);

/* Probe/remove routines */
struct ibmvscsi_host_data *ibmvscsi_probe(struct device *dev);
void ibmvscsi_remove(struct ibmvscsi_host_data *hostdata);

int ibmvscsi_detect(Scsi_Host_Template * host_template);
int ibmvscsi_release(struct Scsi_Host *host);
#endif				/* IBMVSCSI_H */
