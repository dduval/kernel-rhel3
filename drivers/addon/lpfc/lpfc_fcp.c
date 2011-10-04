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
 * $Id: lpfc_fcp.c 432 2005-10-28 22:40:25Z sf_support $
 */


/* This is to export entry points needed for IP interface */
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif
#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/utsname.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/if_arp.h>
#include <linux/spinlock.h>


/* From drivers/scsi */
#include <sd.h>
#include <hosts.h>
#include <scsi.h>
#include <linux/ctype.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_mem.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_fcp.h"
#include "lpfc_scsi.h"
#include "lpfc_version.h"

#include <linux/rtnetlink.h>
#include <asm/byteorder.h>
#include <linux/module.h>

/* Configuration parameters defined */
#define LPFC_DEF_ICFG
#include "lpfc_dfc.h"
#include "lpfc_cfgparm.h"
#include "lpfc_module_param.h"
#include "lpfc.conf"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"


char *lpfc_drvr_name = LPFC_DRIVER_NAME;
char *lpfc_release_version = LPFC_DRIVER_VERSION;

MODULE_DESCRIPTION("Emulex LightPulse Fibre Channel driver - Open Source");
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");

#define FC_EXTEND_TRANS_A 1
#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)

int lpfc_detect(Scsi_Host_Template *);
int lpfc_detect_instance(int, struct pci_dev *, uint32_t, Scsi_Host_Template *);
int lpfc_linux_attach(int, Scsi_Host_Template *, struct pci_dev *);
int lpfc_get_bind_type(lpfcHBA_t *);

int lpfc_release(struct Scsi_Host *host);
int lpfc_linux_detach(int);

void lpfc_select_queue_depth(struct Scsi_Host *, struct scsi_device *);

const char *lpfc_info(struct Scsi_Host *);

int lpfc_device_queue_depth(lpfcHBA_t *, struct scsi_device *);
int lpfc_reset_bus_handler(struct scsi_cmnd *cmnd);

int lpfc_memmap(lpfcHBA_t *);
int lpfc_unmemmap(lpfcHBA_t *);
int lpfc_pcimap(lpfcHBA_t *);

int lpfc_config_setup(lpfcHBA_t *);
int lpfc_bind_setup(lpfcHBA_t *);
int lpfc_sli_setup(lpfcHBA_t *);
int lpfc_bind_wwpn(lpfcHBA_t *, char **, u_int);
int lpfc_bind_wwnn(lpfcHBA_t *, char **, u_int);
int lpfc_bind_did(lpfcHBA_t *, char **, u_int);
LPFCSCSILUN_t *lpfc_tran_find_lun(LPFC_SCSI_BUF_t *);

extern int lpfc_biosparam(Disk *, kdev_t, int[]);

/* Binding Definitions: Max string size  */
#define FC_MAX_DID_STRING       6
#define FC_MAX_WW_NN_PN_STRING 16


#if VARYIO == 20
#define VARYIO_ENTRY .can_do_varyio = 1,
#elif VARYIO == 3
#define VARYIO_ENTRY .vary_io =1,
#else
#define VARYIO_ENTRY
#endif

#if defined CONFIG_HIGHMEM
#if USE_HIGHMEM_IO ==2		// i386 & Redhat 2.1
#define HIGHMEM_ENTRY .can_dma_32 = 1,
#define SINGLE_SG_OK .single_sg_ok = 1,
#else
#if USE_HIGHMEM_IO ==3		// Redhat 3.0, Suse
#define HIGHMEM_ENTRY .highmem_io = 1,
#define SINGLE_SG_OK
#else				// any other
#define HIGHMEM_ENTRY
#define SINGLE_SG_OK
#endif
#endif
#else				// no highmem config
#define HIGHMEM_ENTRY
#define SINGLE_SG_OK
#endif

static Scsi_Host_Template driver_template = {
	.module = THIS_MODULE,
	.name = LPFC_DRIVER_NAME,
	.info = lpfc_info,
	.queuecommand = lpfc_queuecommand,

	.eh_abort_handler = lpfc_abort_handler,
	.eh_device_reset_handler = lpfc_reset_lun_handler,
	.eh_bus_reset_handler = lpfc_reset_bus_handler,

	.detect = lpfc_detect,
	.release = lpfc_release,
	.use_new_eh_code = 1,
	VARYIO_ENTRY
	HIGHMEM_ENTRY
	SINGLE_SG_OK

	.bios_param = lpfc_biosparam,
	.proc_info = lpfc_proc_info,
	.proc_name = LPFC_DRIVER_NAME,

	.can_queue = LPFC_DFT_HBA_Q_DEPTH,
	.this_id = -1,

	.sg_tablesize = SG_ALL,
	.cmd_per_lun = 30,
	.use_clustering = DISABLE_CLUSTERING,
};



int lpfc_nethdr = 1;

uint16_t lpfc_num_nodes = 128;	/* default number of NPort structs to alloc */

lpfcDRVR_t lpfcDRVR;

extern char *lpfc_fcp_bind_WWPN[];
extern char *lpfc_fcp_bind_WWNN[];
extern char *lpfc_fcp_bind_DID[];


static struct pci_device_id lpfc_id_table[] = {
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_VIPER,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_THOR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PEGASUS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_CENTAUR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_DRAGONFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_SUPERFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_RFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_NEPTUNE,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_NEPTUNE_SCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_NEPTUNE_DCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_HELIOS,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_HELIOS_SCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_HELIOS_DCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_BMID,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_BSMB,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZEPHYR,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZEPHYR_SCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZEPHYR_DCSP,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZMID,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_ZSMB,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_TFLY,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP101,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP10000S,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LP11000S,
		PCI_ANY_ID, PCI_ANY_ID, },
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_LPE11000S,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, lpfc_id_table);

int
lpfc_detect(Scsi_Host_Template * tmpt)
{
	struct pci_dev *pdev = 0;
	int instance = 0;
	int i;

#if VARYIO == 21
#ifdef SCSI_HOST_VARYIO
	SCSI_HOST_VARYIO(tmpt) = 1;
#endif
#endif
	printk(LPFC_MODULE_DESC "\n");
	printk(LPFC_COPYRIGHT "\n");

	/* Release the io_request_lock lock and reenable interrupts allowing
	 * the driver to sleep if necessary.
	 */
	spin_unlock_irq(&io_request_lock);

	memset((char *)&lpfcDRVR, 0, sizeof (lpfcDRVR_t));
	lpfcDRVR.loadtime = jiffies;

	/* Initialize list head for hba/port list */
	INIT_LIST_HEAD(&lpfcDRVR.hba_list_head);

	/* Search for all Device IDs supported */
	i = 0;
	while (lpfc_id_table[i].vendor) {
		instance = lpfc_detect_instance(instance, pdev, lpfc_id_table[i].device, tmpt );
		i++;
	}


	/* reacquire io_request_lock as the midlayer was holding it when it
	   called us */
	spin_lock_irq(&io_request_lock);
	return (instance);
}

int
lpfc_detect_instance(int instance,
		     struct pci_dev *pdev, uint type, Scsi_Host_Template * tmpt)
{

	/* PCI_SUBSYSTEM_IDS supported */
	while ((pdev = pci_find_subsys(PCI_VENDOR_ID_EMULEX, type,
				       PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (pci_enable_device(pdev)) {
			continue;
		}
		if (pci_request_regions(pdev, LPFC_DRIVER_NAME)) {
			printk("lpfc pci I/O region is already in use. \n");
			printk("a driver for lpfc is already loaded on this "
			       "system\n");
			continue;
		}

		if (lpfc_linux_attach(instance, tmpt, pdev)) {
			pci_release_regions(pdev);

			/* Failed to attach to lpfc adapter: bus <bus>
			   device <device> irq <irq> */
			lpfc_printf_log(instance, &lpfc_msgBlk0443,
					lpfc_mes0443,
					lpfc_msgBlk0443.msgPreambleStr,
					pdev->bus->number,
					PCI_SLOT(pdev->devfn),
					pdev->irq);

			continue;
		}
		instance++;
	}

	return (instance);
}


int
lpfc_release(struct Scsi_Host *host)
{
	lpfcHBA_t *phba;
	int instance;
	phba = (lpfcHBA_t *) host->hostdata[0];
	instance = phba->brd_no;

	/*
	 * detach the board 
	 */
	lpfc_linux_detach(instance);

	return (0);
}


int
lpfc_linux_attach(int instance, Scsi_Host_Template * tmpt, struct pci_dev *pdev)
{
	struct Scsi_Host *host;
	lpfcHBA_t *phba;
	lpfcCfgParam_t *clp;
	struct clk_data *clkData;
	int rc, i;
	unsigned long iflag;
	uint32_t timeout;

	/*
	 * must have a valid pci_dev
	 */
	if (!pdev)
		return (1);

	/* 
	 * Allocate space for adapter info structure
	 */
	if (!(phba = kmalloc(sizeof (lpfcHBA_t), GFP_ATOMIC))) {
		return (1);
	}

	/* By default, the driver expects this attach to succeed. */
	rc = 0;

	memset(phba, 0, sizeof (lpfcHBA_t));
	INIT_LIST_HEAD(&phba->timerList);

	/* Initialize default values for configuration parameters */
	if (!(phba->config = kmalloc(sizeof (lpfc_icfgparam), GFP_ATOMIC))) {
		goto error_1;
	}
	memset(phba->config, 0, sizeof (lpfc_icfgparam));

	memcpy(&phba->config[0], (uint8_t *) & lpfc_icfgparam[0],
	       sizeof (lpfc_icfgparam));

	clp = &phba->config[0];

	/* Set everything to the defaults */
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++)
		clp[i].a_current = clp[i].a_default;
	
	phba->brd_no = instance;

	/* Add adapter structure to list */
	list_add_tail(&phba->hba_list, &lpfcDRVR.hba_list_head);

	/* Initialize all internally managed lists. */
	INIT_LIST_HEAD(&phba->fc_nlpmap_list);
	INIT_LIST_HEAD(&phba->fc_nlpunmap_list);
	INIT_LIST_HEAD(&phba->fc_plogi_list);
	INIT_LIST_HEAD(&phba->fc_adisc_list);
	INIT_LIST_HEAD(&phba->fc_nlpbind_list);
	INIT_LIST_HEAD(&phba->delay_list);
	INIT_LIST_HEAD(&phba->free_buf_list);

	/* Initialize plxhba - LINUX specific */
	phba->pcidev = pdev;
	init_waitqueue_head(&phba->linkevtwq);
	init_waitqueue_head(&phba->rscnevtwq);
	init_waitqueue_head(&phba->ctevtwq);

	if ((rc = lpfc_pcimap(phba))) {
		goto error_2;
	}

	if ((rc = lpfc_memmap(phba))) {
		goto error_2;
	}

	lpfcDRVR.num_devs++;
	lpfc_config_setup(phba);	/* Setup configuration parameters */

	/*
	 * If the t.o value is not set, set it to 30
	 */
	if (clp[LPFC_CFG_SCSI_REQ_TMO].a_current == 0) {
		clp[LPFC_CFG_SCSI_REQ_TMO].a_current = 30;
	}

	if (clp[LPFC_CFG_DISC_THREADS].a_current) {
		/*
		 * Set to FC_NLP_REQ if automap is set to 0 since order of
		 * discovery does not matter if everything is persistently
		 * bound. 
		 */
		if (clp[LPFC_CFG_AUTOMAP].a_current == 0) {
			clp[LPFC_CFG_DISC_THREADS].a_current =
			    LPFC_MAX_DISC_THREADS;
		}
	}


  	/* 
	 * Register this board
	 */

	host = scsi_register(tmpt, sizeof (unsigned long));
	if (host) {
		phba->host = host;
		host->can_queue = clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current - 10;
	}
	else {
		printk (KERN_WARNING
			"%s%d: scsi_host_alloc failed during attach\n", 
			lpfc_drvr_name, phba->brd_no);
		goto error_3;
	}

	/*
	 * Adjust the number of id's
	 * Although max_id is an int, target id's are unsined chars
	 * Do not exceed 255, otherwise the device scan will wrap around
	 */
	if (clp[LPFC_CFG_MAX_TARGET].a_current > LPFC_MAX_TARGET) {
		clp[LPFC_CFG_MAX_TARGET].a_current = LPFC_DFT_MAX_TARGET;
	}
	host->max_id = clp[LPFC_CFG_MAX_TARGET].a_current;
	host->unique_id = instance;

	if (clp[LPFC_CFG_MAX_LUN].a_current > LPFC_MAX_LUN) {
		clp[LPFC_CFG_MAX_LUN].a_current = LPFC_DFT_MAX_LUN;
	}
	host->max_lun = clp[LPFC_CFG_MAX_LUN].a_current;

	/* Adapter ID - tell midlayer not to reserve an ID for us */
	host->this_id = -1;



	/* Initialize all per HBA locks */
	lpfc_drvr_init_lock(phba);
	spin_lock_init(&phba->hiprilock);

	/* Set up the HBA specific LUN device lookup routine */
	phba->lpfc_tran_find_lun = lpfc_tran_find_lun;

	lpfc_sli_setup(phba);	/* Setup SLI Layer to run over lpfc HBAs */
	lpfc_sli_queue_setup(phba);	/* Initialize the SLI Layer */

	if (lpfc_mem_alloc(phba) == 0) {
          	scsi_unregister(host);
		goto error_3;
	}

	lpfc_bind_setup(phba);	/* Setup binding configuration parameters */

	/* Initialize HBA structure */
	phba->fc_edtov = FF_DEF_EDTOV;
	phba->fc_ratov = FF_DEF_RATOV;
	phba->fc_altov = FF_DEF_ALTOV;
	phba->fc_arbtov = FF_DEF_ARBTOV;

	/* Set the FARP and XRI timeout values now since they depend on
	   fc_ratov. */
	phba->fc_ipfarp_timeout = (3 * phba->fc_ratov);
	phba->fc_ipxri_timeout = (3 * phba->fc_ratov);

	tasklet_init(&phba->task_run, (void *)lpfc_tasklet,
		(unsigned long) phba);
	INIT_LIST_HEAD(&phba->task_disc);


	/*
	 * Setup the scsi timeout handler with a 
	 * timeout value = greater of (2*RATOV, 5).
	 */                     	

	timeout = (phba->fc_ratov << 1) > 5 ? (phba->fc_ratov << 1) : 5;
	lpfc_start_timer(phba, timeout, &phba->scsi_tmofunc, 
		lpfc_scsi_timeout_handler, (unsigned long)timeout, 0);


	LPFC_DRVR_LOCK(phba, iflag);

	if ((rc = lpfc_sli_hba_setup(phba)) != 0) {	/* Initialize the HBA */
		LPFC_DRVR_UNLOCK(phba, iflag);
		scsi_unregister(host);
		goto error_5;
	}

	/* 
	   This is to support HBAs returning max_xri less than configured
	   HBA_Q_DEPTH 
	*/
	host->can_queue = clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current - 10;

	lpfc_sched_init_hba(phba, clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current - 10);

	LPFC_DRVR_UNLOCK(phba, iflag);



	/*
	 * Starting with 2.4.0 kernel, Linux can support commands longer
	 * than 12 bytes. However, scsi_register() always sets it to 12.
	 * For it to be useful to the midlayer, we have to set it here.
	 */
	host->max_cmd_len = 16;

	/*
	 * Queue depths per lun
	 */
	host->cmd_per_lun = 1;

	host->select_queue_depths = lpfc_select_queue_depth;

	/*
	 * Save a pointer to device control in host and increment board
	 */
	host->hostdata[0] = (unsigned long)phba;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,4)
	scsi_set_pci_device(host, pdev);
#endif

	goto out;

 error_5:
	LPFC_DRVR_LOCK(phba, iflag);
	lpfc_sli_hba_down(phba);
	LPFC_DRVR_UNLOCK(phba, iflag);


 error_3:
	/* Stop any timers that were started during this attach. */
	LPFC_DRVR_LOCK(phba, iflag);
	while (!list_empty(&phba->timerList)) {
		clkData = (struct clk_data *)(phba->timerList.next);
		if (clkData) {
			lpfc_stop_timer(clkData);
		}
	}
	LPFC_DRVR_UNLOCK(phba, iflag);
	lpfc_tasklet((unsigned long)phba);
	tasklet_kill(&phba->task_run);
	lpfc_mem_free(phba);
	lpfc_unmemmap(phba);
	lpfcDRVR.num_devs--;

 error_2:
	kfree(phba->config);

 error_1:
	/* Remove from list of active HBAs */
	list_del_init(&phba->hba_list);

	kfree(phba);

	/* Any error in the attach routine will end up here.  Set the return
 	 * code to 1 and exit. 
	 */
	rc = 1;

 out:	
	return rc;
}

/*
 * Retrieve instance (board no) matching phba
 * If valid hba return 1
 * If invalid hba return 0 
 */
int
lpfc_check_valid_phba(lpfcHBA_t * phba)
{
	struct list_head * pos;
	lpfcHBA_t        * tphba = NULL;
	int    found = 0;

	list_for_each(pos, &lpfcDRVR.hba_list_head) {
		tphba = list_entry(pos, lpfcHBA_t, hba_list);
		if (tphba == phba) {
			found = 1;
			break;
		}
	}
	
	return(found);
}

/*
 * Retrieve instance (board no) matching phba
 * If found return board number
 * If not found return -1 
 */
int
lpfc_get_inst_by_phba(lpfcHBA_t * phba)
{
	struct list_head * pos;
	lpfcHBA_t        * tphba = NULL;
	int    found = 0;

	list_for_each(pos, &lpfcDRVR.hba_list_head) {
		tphba = list_entry(pos, lpfcHBA_t, hba_list);
		if (tphba == phba) {
			found = 1;
			break;
		}
	}
	
	if (!found) 
		return(-1);
	else
		return(tphba->brd_no);
	
}

/*
 * Retrieve lpfcHBA * matching instance (board no)
 * If found return lpfcHBA *
 * If not found return NULL 
 */
lpfcHBA_t *
lpfc_get_phba_by_inst(int inst)
{
	struct list_head * pos;
	lpfcHBA_t * phba;
	int    found = 0;

	phba = NULL;
	list_for_each(pos, &lpfcDRVR.hba_list_head) {
		phba = list_entry(pos, lpfcHBA_t, hba_list);
		if (phba->brd_no == inst) {
			found = 1;
			break;
		}
	}
	
	if (!found) 
		return(NULL);
	else
		return(phba);
	
}

int
lpfc_linux_detach(int instance)
{
	lpfcHBA_t        * phba;
	LPFC_SLI_t       * psli;
	struct clk_data  * clkData;
	unsigned long      iflag;
	LPFC_SCSI_BUF_t  * lpfc_cmd;
	struct lpfc_dmabuf *cur_buf;
	struct list_head *curr, *next;
	struct timer_list *ptimer;

	if ((phba = lpfc_get_phba_by_inst(instance)) == NULL) {
		return(0);
	}

	psli = &phba->sli;

	scsi_unregister(phba->host);

	LPFC_DRVR_LOCK(phba, iflag);
	phba->no_timer = 1;
	list_for_each_safe(curr, next, &phba->timerList) {
		clkData = list_entry(curr, struct clk_data, listLink);
		if (clkData) {
			ptimer = clkData->timeObj;
			if (timer_pending(ptimer)) {
				lpfc_stop_timer(clkData);	
			}
		}
	}

	LPFC_DRVR_UNLOCK(phba, iflag);
	while (!list_empty(&phba->timerList)) {
	}
	LPFC_DRVR_LOCK(phba, iflag);

	lpfc_sli_hba_down(phba);	/* Bring down the SLI Layer */
	if (phba->intr_inited) {
		/* Clear all interrupt enable conditions */
		writel(0, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
		/* Clear all pending interrupts */
		writel(0xffffffff, phba->HAregaddr);
		readl(phba->HAregaddr); /* flush */
		LPFC_DRVR_UNLOCK(phba, iflag);
		free_irq(phba->pcidev->irq, phba);
		LPFC_DRVR_LOCK(phba, iflag);
		phba->intr_inited = 0;
	}

	if (phba->pcidev) {
		pci_release_regions(phba->pcidev);
		pci_disable_device(phba->pcidev);
	}

	/* Complete any scheduled delayed i/o completions */
	while(!list_empty(&phba->delay_list)) {
		lpfc_cmd = list_entry(phba->delay_list.next, LPFC_SCSI_BUF_t, listentry);
		list_del(&lpfc_cmd->listentry);
		lpfc_iodone(phba, lpfc_cmd);
	}

	while(!list_empty(&phba->free_buf_list)) {
		cur_buf = list_entry(phba->free_buf_list.next, DMABUF_t, list);
		list_del(&cur_buf->list);
		lpfc_mbuf_free(phba, cur_buf->virt, cur_buf->phys);
		kfree((void *)cur_buf);
	}

	LPFC_DRVR_UNLOCK(phba, iflag);
	lpfc_tasklet((unsigned long)phba);
	tasklet_kill(&phba->task_run);
	LPFC_DRVR_LOCK(phba, iflag);

	lpfc_cleanup(phba, 0);
	lpfc_scsi_free(phba);

	LPFC_DRVR_UNLOCK(phba, iflag);
	lpfc_mem_free(phba);
	lpfc_unmemmap(phba);
	LPFC_DRVR_LOCK(phba, iflag);



	if (phba->config)
		kfree(phba->config);

	LPFC_DRVR_UNLOCK(phba, iflag);

	/* Remove from list of active HBAs */
	list_del(&phba->hba_list);

	kfree(phba);

	lpfcDRVR.num_devs--;

	return (0);
}

const char *
lpfc_info(struct Scsi_Host *host)
{
	lpfcHBA_t    *phba = (lpfcHBA_t *) host->hostdata[0];
	int len;
	static char  lpfcinfobuf[320];
	
        memset(lpfcinfobuf,0,320);
	if(phba && phba->pcidev){
		strncpy(lpfcinfobuf, phba->ModelDesc, 320);
		len = strlen(lpfcinfobuf);	
		snprintf(lpfcinfobuf + len,
			320-len,
	       		" on PCI bus %02x device %02x irq %d",
			phba->pcidev->bus->number,
		 	phba->pcidev->devfn,
			phba->pcidev->irq);
	}
	return lpfcinfobuf;
}

int
lpfc_proc_info(char *buf,
	       char **start, off_t offset, int length, int hostnum, int rw)
{

	lpfcHBA_t *phba = NULL;
	struct pci_dev *pdev;
	char fwrev[32];
	lpfc_vpd_t *vp;
	struct list_head *pos;
	LPFC_NODELIST_t *ndlp;
	int  i, j, incr;
	char hdw[9];
	int len = 0, found = 0;
	
	/* Sufficient bytes to hold a port or node name. */
	uint8_t name[sizeof (NAME_TYPE)];

	/* If rw = 0, then read info
	 * If rw = 1, then write info (NYI)
	 */
	if (rw)
		return -EINVAL;

	
	list_for_each(pos, &lpfcDRVR.hba_list_head) {
		phba = list_entry(pos, lpfcHBA_t, hba_list);
		if (phba->host->host_no == hostnum) {
			found = 1;
			break;
		}
	}
	

	if (!found) {
		return sprintf(buf, "Cannot find adapter for requested host "
			       "number.\n");
	}

	vp = &phba->vpd;
	pdev = phba->pcidev;

	len += snprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");

	len += snprintf(buf + len, PAGE_SIZE-len, "%s\n",
			lpfc_info(phba->host));

	len += snprintf(buf + len, PAGE_SIZE-len, "SerialNum: %s\n",
			phba->SerialNumber);

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	len += snprintf(buf + len, PAGE_SIZE-len, "Firmware Version: %s\n",
			fwrev);

	len += snprintf(buf + len, PAGE_SIZE-len, "Hdw: ");
	/* Convert JEDEC ID to ascii for hardware version */
	incr = vp->rev.biuRev;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = (char)((uint8_t) 0x30 + (uint8_t) j);
		else
			hdw[7 - i] =
			    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	len += snprintf(buf + len, PAGE_SIZE-len, hdw);

	len += snprintf(buf + len, PAGE_SIZE-len, "\nVendorId: 0x%x\n",
		       ((((uint32_t) pdev->device) << 16) |
			(uint32_t) (pdev->vendor)));

	/* A Fibre Channel node or port name is 8 octets long and delimited by 
	 * colons.
	 */
	len += snprintf(buf + len, PAGE_SIZE-len, "Portname: ");
	memcpy (&name[0], &phba->fc_portname, sizeof (NAME_TYPE));
	len += snprintf(buf + len, PAGE_SIZE-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			name[0], name[1], name[2], name[3], name[4], name[5],
			name[6], name[7]);

	len += snprintf(buf + len, PAGE_SIZE-len, "   Nodename: ");
	memcpy (&name[0], &phba->fc_nodename, sizeof (NAME_TYPE));
	len += snprintf(buf + len, PAGE_SIZE-len,
			"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			name[0], name[1], name[2], name[3], name[4], name[5],
			name[6], name[7]);

	switch (phba->hba_state) {
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
		len += snprintf(buf + len, PAGE_SIZE-len, "\n\nLink Down\n");
		break;
	case LPFC_LINK_UP:
	case LPFC_LOCAL_CFG_LINK:
		len += snprintf(buf + len, PAGE_SIZE-len, "\n\nLink Up\n");
		break;
	case LPFC_FLOGI:
	case LPFC_FABRIC_CFG_LINK:
	case LPFC_NS_REG:
	case LPFC_NS_QRY:
	case LPFC_BUILD_DISC_LIST:
	case LPFC_DISC_AUTH:
	case LPFC_CLEAR_LA:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"\n\nLink Up - Discovery\n");
		break;
	case LPFC_HBA_READY:
		len += snprintf(buf + len, PAGE_SIZE-len,
				"\n\nLink Up - Ready:\n");
		len += snprintf(buf + len, PAGE_SIZE-len, "   PortID 0x%x\n",
				phba->fc_myDID);
		if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (phba->fc_flag & FC_PUBLIC_LOOP)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Public Loop\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Private Loop\n");
		} else {
			if (phba->fc_flag & FC_FABRIC)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Fabric\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Point-2-Point\n");
		}

		if (phba->fc_linkspeed == LA_4GHZ_LINK)
			len += snprintf(buf + len, PAGE_SIZE-len,
					"   Current speed 4G\n");
		else
		if (phba->fc_linkspeed == LA_2GHZ_LINK)
			len += snprintf(buf + len, PAGE_SIZE-len,
					"   Current speed 2G\n");
		else
		if (phba->fc_linkspeed == LA_1GHZ_LINK)
			len += snprintf(buf + len, PAGE_SIZE-len,
					"   Current speed 1G\n\n");
		else
			len += snprintf(buf + len, PAGE_SIZE-len,
					"   Current speed unknown\n\n");

		/* Loop through the list of mapped nodes and dump the known node
		   information. */
		list_for_each(pos, &phba->fc_nlpmap_list) {
			ndlp = list_entry(pos, LPFC_NODELIST_t, nlp_listp);
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE){
				len += snprintf(buf + len, PAGE_SIZE -len, 
						"lpfc%dt%02x DID %06x WWPN ",
						ndlp->nlp_Target->pHba->brd_no,
						ndlp->nlp_sid, ndlp->nlp_DID);

				/* A Fibre Channel node or port name is 8 octets
				 * long and delimited by colons.
				 */
				memcpy (&name[0], &ndlp->nlp_portname,
					sizeof (NAME_TYPE));
				len += snprintf(buf + len, PAGE_SIZE-len,
						"%02x:%02x:%02x:%02x:%02x:%02x:"
						"%02x:%02x",
						name[0], name[1], name[2],
						name[3], name[4], name[5],
						name[6], name[7]);

				len += snprintf(buf + len, PAGE_SIZE-len,
						" WWNN ");
				memcpy (&name[0], &ndlp->nlp_nodename,
					sizeof (NAME_TYPE));
				len += snprintf(buf + len, PAGE_SIZE-len,
						"%02x:%02x:%02x:%02x:%02x:%02x:"
						"%02x:%02x\n",
						name[0], name[1], name[2],
						name[3], name[4], name[5],
						name[6], name[7]);

			}
			if(PAGE_SIZE - len < 90)
				break;
		}

		if(pos != &phba->fc_nlpmap_list) 
			len += snprintf(buf+len, PAGE_SIZE-len, "...\n");

	}
	return (len);
}

int
lpfc_reset_bus_handler(struct scsi_cmnd *cmnd)
{
	lpfcHBA_t *phba;
	LPFC_SCSI_BUF_t *lpfc_cmd;
	unsigned long iflag;
	int rc, tgt, lun;

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);

	phba = (lpfcHBA_t *) cmnd->host->hostdata[0];
	tgt = cmnd->target;
	lun = cmnd->lun;
	LPFC_DRVR_LOCK(phba, iflag);

	rc = 0;
	if ((lpfc_cmd = lpfc_get_scsi_buf(phba))) {
		rc = lpfc_scsi_hba_reset(phba, lpfc_cmd);
		lpfc_free_scsi_buf(lpfc_cmd);
	}

	/* SCSI layer issued Bus Reset */
	lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0714,
		        lpfc_mes0714,
		        lpfc_msgBlk0714.msgPreambleStr,
		        tgt, lun, rc);

	LPFC_DRVR_UNLOCK(phba, iflag);

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);
	
	return rc == 1? SUCCESS: FAILED;

}				/* lpfc_reset_bus_handler */


void
lpfc_select_queue_depth(struct Scsi_Host *host, struct scsi_device *scsi_devs)
{
	struct scsi_device *device;
	lpfcHBA_t *phba;

	phba = (lpfcHBA_t *) host->hostdata[0];
	for (device = scsi_devs; device != 0; device = device->next) {
		if (device->host == host)
			lpfc_device_queue_depth(phba, device);
	}
}

int
lpfc_device_queue_depth(lpfcHBA_t * phba, struct scsi_device *device)
{
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];

	if (device->tagged_supported) {
		device->tagged_queue = 1;
		device->current_tag = 0;
		device->queue_depth = clp[LPFC_CFG_DFT_LUN_Q_DEPTH].a_current;
	} else {
		device->queue_depth = 16;
	}
	return (device->queue_depth);
}



int
lpfc_memmap(lpfcHBA_t * phba)
{
	unsigned long bar0map_len, bar2map_len;

	if (phba->pcidev == 0)
		return (1);

	/* Configure DMA attributes. */
	if (pci_set_dma_mask(phba->pcidev, 0xffffffffffffffffULL)) {
		if (pci_set_dma_mask(phba->pcidev, 0xffffffffULL)) {
			return (1);
		}
	}

	/* 
	 * Get the physical address of Bar0 and Bar2 and the number of bytes
	 * required by each mapping.
	 */
	phba->pci_bar0_map = pci_resource_start(phba->pcidev, 0);
	bar0map_len        = pci_resource_len(phba->pcidev, 0);
	
	phba->pci_bar2_map = pci_resource_start(phba->pcidev, 2);
	bar2map_len        = pci_resource_len(phba->pcidev, 2);

	/* Map HBA SLIM and Control Registers to a kernel virtual address. */
	phba->slim_memmap_p      = ioremap(phba->pci_bar0_map, bar0map_len);
	phba->ctrl_regs_memmap_p = ioremap(phba->pci_bar2_map, bar2map_len);

	/* Setup SLI2 interface */
	if (phba->slim2p.virt == 0) {
		/*
		 * Allocate memory for SLI-2 structures
		 */
		phba->slim2p.virt = pci_alloc_consistent(phba->pcidev,
							 sizeof (SLI2_SLIM_t),
							 &(phba->slim2p.phys));
		
		if (phba->slim2p.virt == 0) {
			/* Cleanup adapter SLIM and Control Register
			   mappings. */
			iounmap(phba->ctrl_regs_memmap_p);
			iounmap(phba->slim_memmap_p);
			return (1);
		}

		/* The SLIM2 size is stored in the next field */ 
		phba->slim_size = sizeof (SLI2_SLIM_t);
		memset((char *)phba->slim2p.virt, 0, sizeof (SLI2_SLIM_t));
	}
	return (0);
}

int
lpfc_unmemmap(lpfcHBA_t * phba)
{
	struct pci_dev *pdev;

	pdev = phba->pcidev;

	/* unmap adapter SLIM and Control Registers */
	iounmap(phba->ctrl_regs_memmap_p);
	iounmap(phba->slim_memmap_p);
	
	/* Free resources associated with SLI2 interface */
	if (phba->slim2p.virt) {

		
		pci_free_consistent(pdev, 
				    phba->slim_size, 
				    phba->slim2p.virt, 
				    phba->slim2p.phys);

	}
	return (0);
}

int
lpfc_pcimap(lpfcHBA_t * phba)
{
	struct pci_dev *pdev;
	int ret_val;

	/*
	 * PCI for board
	 */
	pdev = phba->pcidev;
	if (!pdev)
		return (1);

	/* The LPFC HBAs are bus-master capable.  Call the kernel and have this
	 * functionality enabled.  Note that setting pci bus master also sets
	 * the latency value as well.  Also turn on MWI so that the cache line
	 * size is set to match the host pci bridge.
	 */

	pci_set_master (pdev);
	ret_val = pci_set_mwi (pdev);
	if (ret_val != 0) {
		/* The mwi set operation failed.  This is not a fatal error
		 * so don't return an error.
		 */
	}

	return (0);
}

void
lpfc_setup_slim_access(lpfcHBA_t * arg)
{
	lpfcHBA_t *phba;

	phba = (lpfcHBA_t *) arg;
	phba->MBslimaddr = phba->slim_memmap_p;
	phba->HAregaddr = (uint32_t *) (phba->ctrl_regs_memmap_p) +
		HA_REG_OFFSET;
	phba->HCregaddr = (uint32_t *) (phba->ctrl_regs_memmap_p) +
		HC_REG_OFFSET;
	phba->CAregaddr = (uint32_t *) (phba->ctrl_regs_memmap_p) +
		CA_REG_OFFSET;
	phba->HSregaddr = (uint32_t *) (phba->ctrl_regs_memmap_p) +
		HS_REG_OFFSET;
	return;
}


uint32_t
lpfc_intr_prep(lpfcHBA_t * phba)
{
	LPFC_SLI_t *psli;
	uint32_t ha_copy;

	/* Ignore all interrupts during initialization. */
	if (phba->hba_state < LPFC_LINK_DOWN) {
		return (0);
	}

	psli = &phba->sli;
	/* Read host attention register to determine interrupt source */
	ha_copy = readl(phba->HAregaddr);

	/* Clear Attention Sources, except ERATT (to preserve status) & LATT
	 *    (ha_copy & ~(HA_ERATT | HA_LATT));
	 */
	writel((ha_copy & ~(HA_LATT | HA_ERATT)), phba->HAregaddr);
	readl(phba->HAregaddr);
	return (ha_copy);
}				/* lpfc_intr_prep */

int
lpfc_valid_lun(LPFCSCSITARGET_t * targetp, uint64_t lun)
{
	uint32_t rptLunLen;
	uint32_t *datap32;
	uint32_t lunvalue, i;

	if (targetp->rptLunState != REPORT_LUN_COMPLETE) {
		return 1;
	}

	if (targetp->RptLunData) {
		datap32 = (uint32_t *) targetp->RptLunData->virt;
		rptLunLen = be32_to_cpu(*datap32);

		for (i = 0; i < rptLunLen; i += 8) {
			datap32 += 2;
			lunvalue = (((*datap32) >> FC_LUN_SHIFT) & 0xff);
			if (lunvalue == (uint32_t) lun)
				return 1;
		}
		return 0;
	} else {
		return 1;
	}
}

void
lpfc_nodev_unsol_event(lpfcHBA_t * phba,
		      LPFC_SLI_RING_t * pring, LPFC_IOCBQ_t * piocbq)
{
	return;
}

int
lpfc_sli_setup(lpfcHBA_t * phba)
{
	int i, totiocb;
	LPFC_SLI_t *psli;
	LPFC_RING_INIT_t *pring;
	lpfcCfgParam_t *clp;

	psli = &phba->sli;
	psli->sliinit.num_rings = MAX_CONFIGURED_RINGS;
	psli->fcp_ring = LPFC_FCP_RING;
	psli->next_ring = LPFC_FCP_NEXT_RING;
	psli->ip_ring = LPFC_IP_RING;

	clp = &phba->config[0];

	totiocb = 0;
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->sliinit.ringinit[i];
		switch (i) {
		case LPFC_FCP_RING:	/* ring 0 - FCP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R0_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R0_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R1XTRA_ENTRIES;
			pring->numCiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_RSP_R3XTRA_ENTRIES;
			pring->iotag_ctr = 0;
			pring->iotag_max =
			    (clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current * 2);
			pring->fast_iotag = pring->iotag_max;
			pring->num_mask = 0;
			break;
		case LPFC_IP_RING:	/* ring 1 - IP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R1_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R1_ENTRIES;
			pring->num_mask = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = clp[LPFC_CFG_XMT_Q_SIZE].a_current;
			pring->fast_iotag = 0;
			break;
		case LPFC_ELS_RING:	/* ring 2 - ELS / CT */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R2_ENTRIES;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->num_mask = 4;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_ELS_REQ;
			pring->prt[0].type = FC_ELS_DATA;
			pring->prt[0].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[1].profile = 0;	/* Mask 1 */
			pring->prt[1].rctl = FC_ELS_RSP;
			pring->prt[1].type = FC_ELS_DATA;
			pring->prt[1].lpfc_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[2].profile = 0;	/* Mask 2 */
			/* NameServer Inquiry */
			pring->prt[2].rctl = FC_UNSOL_CTL;
			/* NameServer */
			pring->prt[2].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[2].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			pring->prt[3].profile = 0;	/* Mask 3 */
			/* NameServer response */
			pring->prt[3].rctl = FC_SOL_CTL;
			/* NameServer */
			pring->prt[3].type = FC_COMMON_TRANSPORT_ULP;
			pring->prt[3].lpfc_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			break;
		}
		totiocb += (pring->numCiocb + pring->numRiocb);
	}
	if (totiocb > MAX_SLI2_IOCB) {
		/* Too many cmd / rsp ring entries in SLI2 SLIM */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0462,
			        lpfc_mes0462,
			        lpfc_msgBlk0462.msgPreambleStr,
			        totiocb, MAX_SLI2_IOCB);
	}

	psli->sliinit.sli_flag = 0;

	return (0);
}

irqreturn_t
lpfc_intr_handler(int irq, void *dev_id, struct pt_regs * regs)
{
	lpfcHBA_t *phba;
	unsigned long iflag;

	/* Sanity check dev_id parameter */
	phba = (lpfcHBA_t *) dev_id;
	if (!phba) {
		return IRQ_NONE;
	}

	/* More sanity checks on dev_id parameter.
	 * We have seen our interrupt service routine being called
	 * with the dev_id of another PCI card in the system.
	 * Here we verify the dev_id is really ours!
	 */
	if(!lpfc_check_valid_phba(phba))
		return IRQ_NONE;

	LPFC_DRVR_LOCK(phba, iflag);

	/* Call SLI Layer to process interrupt */
	lpfc_sli_intr(phba);

	LPFC_DRVR_UNLOCK(phba, iflag);
	return IRQ_HANDLED;
} /* lpfc_intr_handler */

int
lpfc_bind_setup(lpfcHBA_t * phba)
{
	lpfcCfgParam_t *clp;
	char **arrayp = 0;
	u_int cnt = 0;

	/* 
	 * Check if there are any WWNN / scsid bindings
	 */
	clp = &phba->config[0];

	lpfc_get_bind_type(phba);

	switch (phba->fcp_mapping) {
	case FCP_SEED_WWNN:
		arrayp = lpfc_fcp_bind_WWNN;
		cnt = 0;
		while (arrayp[cnt] != 0)
			cnt++;
		if (cnt && (*arrayp != 0)) {
			lpfc_bind_wwnn(phba, arrayp, cnt);
		}
		break;

	case FCP_SEED_WWPN:
		arrayp = lpfc_fcp_bind_WWPN;
		cnt = 0;
		while (arrayp[cnt] != 0)
			cnt++;
		if (cnt && (*arrayp != 0)) {
			lpfc_bind_wwpn(phba, arrayp, cnt);
		}
		break;

	case FCP_SEED_DID:
		if (clp[LPFC_CFG_BINDMETHOD].a_current != 4) {
			arrayp = lpfc_fcp_bind_DID;
			cnt = 0;
			while (arrayp[cnt] != 0)
				cnt++;
			if (cnt && (*arrayp != 0)) {
				lpfc_bind_did(phba, arrayp, cnt);
			}
		}
		break;
	}

	if (cnt && (*arrayp != 0) &&
	    (clp[LPFC_CFG_BINDMETHOD].a_current == 4)) {
		/* Using ALPA map with Persistent binding - ignoring ALPA map */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0411,
			        lpfc_mes0411, lpfc_msgBlk0411.msgPreambleStr,
			        clp[LPFC_CFG_BINDMETHOD].a_current,
				phba->fcp_mapping);
	}

	if (clp[LPFC_CFG_SCAN_DOWN].a_current > 1) {
		/* Scan-down is out of range - ignoring scan-down */
		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0412,
			        lpfc_mes0412, lpfc_msgBlk0412.msgPreambleStr,
			        clp[LPFC_CFG_BINDMETHOD].a_current,
				phba->fcp_mapping);
		clp[LPFC_CFG_SCAN_DOWN].a_current = 0;
	}
	return (0);
}

/******************************************************************************
* Function name : lpfc_config_setup
*
* Description   : Called from attach to setup configuration parameters for 
*                 adapter 
*                 The goal of this routine is to fill in all the a_current 
*                 members of the CfgParam structure for all configuration 
*                 parameters.
* Example:
* clp[LPFC_CFG_XXX].a_current = (uint32_t)value;
* value might be a define, a global variable, clp[LPFC_CFG_XXX].a_default,
* or some other enviroment specific way of initializing config parameters.
******************************************************************************/

int
lpfc_config_setup(lpfcHBA_t * phba)
{
	lpfcCfgParam_t *clp;
	LPFC_SLI_t *psli;
	int i;
	int brd;

	clp = &phba->config[0];
	psli = &phba->sli;
	brd = phba->brd_no;

	/*
	 * Read the configuration parameters. Also set to default if
	 * parameter value is out of allowed range.
	 */
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		clp[i].a_current = fc_get_cfg_param(brd, i);

		if (i == LPFC_CFG_DFT_HBA_Q_DEPTH)
			continue;

		if ((clp[i].a_current >= clp[i].a_low) &&
		    (clp[i].a_current <= clp[i].a_hi)) {
			/* we continue if the range check is satisfied
			 * however LPFC_CFG_TOPOLOGY has holes and
			 * LPFC_CFG_FCP_CLASS needs to readjusted iff
			 * it satisfies the range check
			 */
			if (i == LPFC_CFG_TOPOLOGY) {
				/* odd values 1,3,5 are out */
				if (!(clp[i].a_current & 1))
					continue;
			} else if (i == LPFC_CFG_FCP_CLASS) {
				switch (clp[i].a_current) {
				case 2:
					/* CLASS2 = 1 */
					clp[i].a_current = CLASS2;
					break;
				case 3:
					/* CLASS3 = 2 */
					clp[i].a_current = CLASS3;
					break;
				}
				continue;
			} else
				continue;
		}

		/* The cr_count feature is disabled if cr_delay is set to 0.  So
		   do not bother user with messages about cr_count if cr_delay
		   is 0 */
		if (i == LPFC_CFG_CR_COUNT)
			if (clp[LPFC_CFG_CR_DELAY].a_current == 0)
				continue;

		lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0413,
				lpfc_mes0413,
				lpfc_msgBlk0413.msgPreambleStr,
				clp[i].a_string, clp[i].a_low, clp[i].a_hi,
				clp[i].a_default);

		clp[i].a_current = clp[i].a_default;

	}

	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_LP101:
	case PCI_DEVICE_ID_BSMB:
	case PCI_DEVICE_ID_ZSMB:
		clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current =
			LPFC_LP101_HBA_Q_DEPTH;
		break;
	case PCI_DEVICE_ID_RFLY:
	case PCI_DEVICE_ID_PFLY:
	case PCI_DEVICE_ID_BMID:
	case PCI_DEVICE_ID_ZMID:
	case PCI_DEVICE_ID_TFLY:
		clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_LC_HBA_Q_DEPTH;
		break;
	default:
		clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_DFT_HBA_Q_DEPTH;
	}

	if (clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current > LPFC_MAX_HBA_Q_DEPTH) {
		clp[LPFC_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_MAX_HBA_Q_DEPTH;
	}

	phba->sli.ring[LPFC_IP_RING].txq_max =
	    clp[LPFC_CFG_XMT_Q_SIZE].a_current;


	return (0);
}

int
lpfc_bind_wwpn(lpfcHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	NAME_TYPE pn;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	phba->fcp_mapping = FCP_SEED_WWPN;
	np = (uint8_t *) & pn;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in WWNN & WWPN */
		for (i = 0; i < FC_MAX_WW_NN_PN_STRING; i++) {
			if (!isxdigit(datap[i]))
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (NAME_TYPE),
							LPFC_BIND_WW_NN_PN,
							&sum, entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD)
				continue;

			/* For syntax error code definitions see
			   LPFC_SYNTAX_ERR_ defines. */
			/* WWPN binding entry <num>: Syntax error code <code> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0430,
					lpfc_mes0430,
					lpfc_msgBlk0430.msgPreambleStr,
					entry, rstatus);
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp = lpfc_bind_alloc(phba, 0)) == 0) {
			/* WWPN binding entry: node table full */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0432,
					lpfc_mes0432,
					lpfc_msgBlk0432.msgPreambleStr);
			goto out;
		}
		memset(blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_WWPN;
		blp->nlp_sid = (sum & 0xff);
		memcpy(&blp->nlp_portname, (uint8_t *) & pn,
		       sizeof (NAME_TYPE));

		lpfc_nlp_bind(phba, blp);

	      out:
		np = (uint8_t *) & pn;
	}
	return (0);
}				/* lpfc_bind_wwpn */

int
lpfc_get_bind_type(lpfcHBA_t * phba)
{
	int bind_type;
	lpfcCfgParam_t *clp;

	clp = &phba->config[0];

	bind_type = clp[LPFC_CFG_BINDMETHOD].a_current;

	switch (bind_type) {
	case 1:
		phba->fcp_mapping = FCP_SEED_WWNN;
		break;

	case 2:
		phba->fcp_mapping = FCP_SEED_WWPN;
		break;

	case 3:
		phba->fcp_mapping = FCP_SEED_DID;
		break;

	case 4:
		phba->fcp_mapping = FCP_SEED_DID;
		break;
	}

	return 0;
}

int
lpfc_bind_wwnn(lpfcHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	NAME_TYPE pn;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	phba->fcp_mapping = FCP_SEED_WWNN;
	np = (uint8_t *) & pn;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in WWNN & WWPN */
		for (i = 0; i < FC_MAX_WW_NN_PN_STRING; i++) {
			if (!isxdigit(datap[i]))
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (NAME_TYPE),
							LPFC_BIND_WW_NN_PN,
							&sum, entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD) {
				continue;
			}

			/* For syntax error code definitions see
			   LPFC_SYNTAX_ERR_ defines. */
			/* WWNN binding entry <num>: Syntax error code <code> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0431,
					lpfc_mes0431,
					lpfc_msgBlk0431.msgPreambleStr,
					entry, rstatus);
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp = lpfc_bind_alloc(phba, 0)) == 0) {
			/* WWNN binding entry: node table full */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0433,
					lpfc_mes0433,
					lpfc_msgBlk0433.msgPreambleStr);
			goto out;
		}
		memset(blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_WWNN;
		blp->nlp_sid = (sum & 0xff);
		memcpy(&blp->nlp_nodename, (uint8_t *) & pn,
		       sizeof (NAME_TYPE));
		lpfc_nlp_bind(phba, blp);

	      out:
		np = (uint8_t *) & pn;
	}			/* for loop */
	return (0);
}				/* lpfc_bind_wwnn */

int
lpfc_bind_did(lpfcHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	D_ID ndid;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	phba->fcp_mapping = FCP_SEED_DID;
	ndid.un.word = 0;
	np = (uint8_t *) & ndid.un.word;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in DID */
		for (i = 0; i < FC_MAX_DID_STRING; i++) {
			if (!isxdigit(datap[i]))
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (D_ID),
							LPFC_BIND_DID, &sum,
							entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD)
				continue;

			/* For syntax error code definitions see
			   LPFC_SYNTAX_ERR_ defines. */
			/* DID binding entry <num>: Syntax error code <code> */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0434,
				        lpfc_mes0434,
				        lpfc_msgBlk0434.msgPreambleStr,
				        entry, rstatus);
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp = lpfc_bind_alloc(phba, 0)) == 0) {
			/* DID binding entry: node table full */
			lpfc_printf_log(phba->brd_no, &lpfc_msgBlk0435,
				        lpfc_mes0435,
				        lpfc_msgBlk0435.msgPreambleStr);
			goto out;
		}
		memset(blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_DID;
		blp->nlp_sid = (sum & 0xff);
		blp->nlp_DID = be32_to_cpu(ndid.un.word);

		lpfc_nlp_bind(phba, blp);

	      out:

		np = (uint8_t *) & ndid.un.word;
	}
	return (0);
}

void
lpfc_wakeup_event(lpfcHBA_t * phba, fcEVTHDR_t * ep)
{
	ep->e_mode &= ~E_SLEEPING_MODE;
	switch (ep->e_mask) {
	case FC_REG_LINK_EVENT:
		wake_up_interruptible(&phba->linkevtwq);
		break;
	case FC_REG_RSCN_EVENT:
		wake_up_interruptible(&phba->rscnevtwq);
		break;
	case FC_REG_CT_EVENT:
		wake_up_interruptible(&phba->ctevtwq);
		break;
	}
	return;
}

int
lpfc_put_event(lpfcHBA_t * phba,
	      uint32_t evcode, uint32_t evdata0, void *evdata1, void *evdata2)
{
	fcEVT_t *ep;
	fcEVT_t *oep;
	fcEVTHDR_t *ehp = 0;
	int found;
	DMABUF_t *mp;
	void *fstype;
	SLI_CT_REQUEST *ctp;

	ehp = (fcEVTHDR_t *) phba->fc_evt_head;
	fstype = 0;
	switch (evcode) {
	case FC_REG_CT_EVENT:
		mp = (DMABUF_t *) evdata1;
		ctp = (SLI_CT_REQUEST *) mp->virt;
		fstype = (void *)(ulong) (ctp->FsType);
		break;
	}

	while (ehp) {
		if ((ehp->e_mask == evcode) && (ehp->e_type == fstype))
			break;
		
		ehp = (fcEVTHDR_t *) ehp->e_next_header;
	}

	if (!ehp) {
		return (0);
	}

	ep = ehp->e_head;
	oep = 0;
	found = 0;

	while (ep && !(found)) {
		if (ep->evt_sleep) {
			switch (evcode) {
			case FC_REG_CT_EVENT:
				if ((ep->evt_type ==
				     (void *)(ulong) FC_FSTYPE_ALL)
				    || (ep->evt_type == fstype)) {
					found++;
					ep->evt_data0 = evdata0; /* tag */
					ep->evt_data1 = evdata1; /* buffer
								    ptr */
					ep->evt_data2 = evdata2; /* count */
					ep->evt_sleep = 0;
					if (ehp->e_mode & E_SLEEPING_MODE) {
						ehp->e_flag |=
						    E_GET_EVENT_ACTIVE;
						lpfc_wakeup_event(phba, ehp);
					}
					/* For FC_REG_CT_EVENT just give it to
					   first one found */
				}
				break;
			default:
				found++;
				ep->evt_data0 = evdata0;
				ep->evt_data1 = evdata1;
				ep->evt_data2 = evdata2;
				ep->evt_sleep = 0;
				if ((ehp->e_mode & E_SLEEPING_MODE)
				    && !(ehp->e_flag & E_GET_EVENT_ACTIVE)) {
					ehp->e_flag |= E_GET_EVENT_ACTIVE;
					lpfc_wakeup_event(phba, ehp);
				}
				/* For all other events, give it to every one
				   waiting */
				break;
			}
		}
		oep = ep;
		
		ep = ep->evt_next;
	}
	return (found);
}

int
lpfc_hba_put_event(lpfcHBA_t * phba, uint32_t evcode, uint32_t evdata1,
		  uint32_t evdata2, uint32_t evdata3, uint32_t evdata4)
{
	HBAEVT_t *rec;

	rec = &phba->hbaevt[phba->hba_event_put];
	rec->fc_eventcode = evcode;

	rec->fc_evdata1 = evdata1;
	rec->fc_evdata2 = evdata2;
	rec->fc_evdata3 = evdata3;
	rec->fc_evdata4 = evdata4;
	phba->hba_event_put++;
	if (phba->hba_event_put >= MAX_HBAEVT) {
		phba->hba_event_put = 0;
	}
	if (phba->hba_event_put == phba->hba_event_get) {
		phba->hba_event_missed++;
		phba->hba_event_get++;
		if (phba->hba_event_get >= MAX_HBAEVT) {
			phba->hba_event_get = 0;
		}
	}

	return (0);
}


LPFCSCSILUN_t *
lpfc_tran_find_lun(LPFC_SCSI_BUF_t * lpfc_cmd)
{
	lpfcHBA_t *phba;
	LPFCSCSILUN_t *lunp;

	phba = lpfc_cmd->scsi_hba;
	lunp = lpfc_find_lun(phba, lpfc_cmd->scsi_target, lpfc_cmd->scsi_lun,
			     1);
	return (lunp);
}

int
lpfc_utsname_nodename_check(void)
{
	if (system_utsname.nodename[0] == '\0')
		return (1);

	return (0);
}



#define HBA_SPECIFIC_CFG_PARAM(hba)                                           \
	switch (param) {                                                      \
	case LPFC_CFG_LOG_VERBOSE:	/* log-verbose */                     \
		value = lpfc_log_verbose;                                     \
		if (lpfc##hba##_log_verbose != -1)                            \
			value = lpfc##hba##_log_verbose;                      \
		break;                                                        \
	case LPFC_CFG_AUTOMAP:	/* automap */                                 \
		value = lpfc_automap;                                         \
		if (lpfc##hba##_automap != -1)                                \
			value = lpfc##hba##_automap;                          \
		break;                                                        \
	case LPFC_CFG_BINDMETHOD:	/* bind-method */                     \
		value = lpfc_fcp_bind_method;                                 \
		if (lpfc##hba##_fcp_bind_method != -1)                        \
			value = lpfc##hba##_fcp_bind_method;                  \
		break;                                                        \
	case LPFC_CFG_CR_DELAY:	/* cr_delay */                                \
		value = lpfc_cr_delay;                                        \
		if (lpfc##hba##_cr_delay != -1)                               \
			value = lpfc##hba##_cr_delay;                         \
		break;                                                        \
	case LPFC_CFG_CR_COUNT:	/* cr_count */                                \
		value = lpfc_cr_count;                                        \
		if (lpfc##hba##_cr_count != -1)                               \
			value = lpfc##hba##_cr_count;                         \
		break;                                                        \
	case LPFC_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */                 \
		value = lpfc_tgt_queue_depth;                                 \
		if (lpfc##hba##_tgt_queue_depth != -1)                        \
			value = lpfc##hba##_tgt_queue_depth;                  \
		break;                                                        \
	case LPFC_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */                 \
		value = lpfc_lun_queue_depth;                                 \
		if (lpfc##hba##_lun_queue_depth != -1)                        \
			value = lpfc##hba##_lun_queue_depth;                  \
		break;                                                        \
	case LPFC_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */                   \
		value = lpfc_extra_io_tmo;                                    \
		if (lpfc##hba##_extra_io_tmo != -1)                           \
			value = lpfc##hba##_extra_io_tmo;                     \
		break;                                                        \
	case LPFC_CFG_FCP_CLASS:	/* fcp-class */                       \
		value = lpfc_fcp_class;                                       \
		if (lpfc##hba##_fcp_class != -1)                              \
			value = lpfc##hba##_fcp_class;                        \
		break;                                                        \
	case LPFC_CFG_USE_ADISC:	/* use-adisc */                       \
		value = lpfc_use_adisc;                                       \
		if (lpfc##hba##_use_adisc != -1)                              \
			value = lpfc##hba##_use_adisc;                        \
		break;                                                        \
	case LPFC_CFG_NO_DEVICE_DELAY:	/* no-device-delay */                 \
		value = lpfc_no_device_delay;                                 \
		if (lpfc##hba##_no_device_delay != -1)                        \
			value = lpfc##hba##_no_device_delay;                  \
		break;                                                        \
	case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */                    \
		value = lpfc_xmt_que_size;                                    \
		if (lpfc##hba##_xmt_que_size != -1)                           \
			value = lpfc##hba##_xmt_que_size;                     \
		break;                                                        \
	case LPFC_CFG_ACK0:	/* ack0 */                                    \
		value = lpfc_ack0;                                            \
		if (lpfc##hba##_ack0 != -1)                                   \
			value = lpfc##hba##_ack0;                             \
		break;                                                        \
	case LPFC_CFG_TOPOLOGY:	/* topology */                                \
		value = lpfc_topology;                                        \
		if (lpfc##hba##_topology != -1)                               \
			value = lpfc##hba##_topology;                         \
		break;                                                        \
	case LPFC_CFG_SCAN_DOWN:	/* scan-down */                       \
		value = lpfc_scan_down;                                       \
		if (lpfc##hba##_scan_down != -1)                              \
			value = lpfc##hba##_scan_down;                        \
		break;                                                        \
	case LPFC_CFG_LINKDOWN_TMO:	/* linkdown-tmo */                    \
		value = lpfc_linkdown_tmo;                                    \
		if (lpfc##hba##_linkdown_tmo != -1)                           \
			value = lpfc##hba##_linkdown_tmo;                     \
		break;                                                        \
	case LPFC_CFG_HOLDIO:	/* nodev-holdio */                            \
		value = lpfc_nodev_holdio;                                    \
		if (lpfc##hba##_nodev_holdio != -1)                           \
			value = lpfc##hba##_nodev_holdio;                     \
		break;                                                        \
	case LPFC_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */                   \
		value = lpfc_delay_rsp_err;                                   \
		if (lpfc##hba##_delay_rsp_err != -1)                          \
			value = lpfc##hba##_delay_rsp_err;                    \
		break;                                                        \
	case LPFC_CFG_CHK_COND_ERR:	/* check-cond-err */                  \
		value = lpfc_check_cond_err;                                  \
		if (lpfc##hba##_check_cond_err != -1)                         \
			value = lpfc##hba##_check_cond_err;                   \
		break;                                                        \
	case LPFC_CFG_NODEV_TMO:	/* nodev-tmo */                       \
		value = lpfc_nodev_tmo;                                       \
		if (lpfc##hba##_nodev_tmo != -1)                              \
			value = lpfc##hba##_nodev_tmo;                        \
		break;                                                        \
	case LPFC_CFG_LINK_SPEED:	/* link-speed */                      \
		value = lpfc_link_speed;                                      \
		if (lpfc##hba##_link_speed != -1)                             \
			value = lpfc##hba##_link_speed;                       \
		break;                                                        \
	case LPFC_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */ \
		value = lpfc_dqfull_throttle_up_time;                         \
		if (lpfc##hba##_dqfull_throttle_up_time != -1)                \
			value = lpfc##hba##_dqfull_throttle_up_time;          \
		break;                                                        \
	case LPFC_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */  \
		value = lpfc_dqfull_throttle_up_inc;                          \
		if (lpfc##hba##_dqfull_throttle_up_inc != -1)                 \
			value = lpfc##hba##_dqfull_throttle_up_inc;           \
		break;                                                        \
	case LPFC_CFG_FDMI_ON:	/* fdmi-on */                                 \
		value = lpfc_fdmi_on;                                         \
		if (lpfc##hba##_fdmi_on != -1)                                \
			value = lpfc##hba##_fdmi_on;                          \
		break;                                                        \
	case LPFC_CFG_MAX_LUN:	/* max-lun */                                 \
		value = lpfc_max_lun;                                         \
		if (lpfc##hba##_max_lun != -1)                                \
			value = lpfc##hba##_max_lun;                          \
		break;                                                        \
	case LPFC_CFG_DISC_THREADS:	/* discovery-threads */               \
		value = lpfc_discovery_threads;                               \
		if (lpfc##hba##_discovery_threads != -1)                      \
			value = lpfc##hba##_discovery_threads;                \
		break;                                                        \
	case LPFC_CFG_MAX_TARGET:	/* max-target */                      \
		value = lpfc_max_target;                                      \
		if (lpfc##hba##_max_target != -1)                             \
			value = lpfc##hba##_max_target;                       \
		break;                                                        \
	case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */                    \
		value = lpfc_scsi_req_tmo;                                    \
		if (lpfc##hba##_scsi_req_tmo != -1)                           \
			value = lpfc##hba##_scsi_req_tmo;                     \
		break;                                                        \
	case LPFC_CFG_LUN_SKIP:	/* lun-skip */                                \
		value = lpfc_lun_skip;                                        \
		if (lpfc##hba##_lun_skip != -1)                               \
			value = lpfc##hba##_lun_skip;                         \
		break;                                                        \
	default:                                                              \
		break;                                                        \
	}

uint32_t
fc_get_cfg_param(int brd, int param)
{
	uint32_t value = (uint32_t)-1;

	switch (brd) {
	case 0:		/* HBA 0 */
		HBA_SPECIFIC_CFG_PARAM(0);
		break;
	case 1:		/* HBA 1 */
		HBA_SPECIFIC_CFG_PARAM(1);
		break;
	case 2:		/* HBA 2 */
		HBA_SPECIFIC_CFG_PARAM(2);
		break;
	case 3:		/* HBA 3 */
		HBA_SPECIFIC_CFG_PARAM(3);
		break;
	case 4:		/* HBA 4 */
		HBA_SPECIFIC_CFG_PARAM(4);
		break;
	case 5:		/* HBA 5 */
		HBA_SPECIFIC_CFG_PARAM(5);
		break;
	case 6:		/* HBA 6 */
		HBA_SPECIFIC_CFG_PARAM(6);
		break;
	case 7:		/* HBA 7 */
		HBA_SPECIFIC_CFG_PARAM(7);
		break;
	case 8:		/* HBA 8 */
		HBA_SPECIFIC_CFG_PARAM(8);
		break;
	case 9:		/* HBA 9 */
		HBA_SPECIFIC_CFG_PARAM(9);
		break;
	case 10:	/* HBA 10 */
		HBA_SPECIFIC_CFG_PARAM(10);
		break;
	case 11:	/* HBA 11 */
		HBA_SPECIFIC_CFG_PARAM(11);
		break;
	case 12:	/* HBA 12 */
		HBA_SPECIFIC_CFG_PARAM(12);
		break;
	case 13:	/* HBA 13 */
		HBA_SPECIFIC_CFG_PARAM(13);
		break;
	case 14:	/* HBA 14 */
		HBA_SPECIFIC_CFG_PARAM(14);
		break;
	case 15:	/* HBA 15 */
		HBA_SPECIFIC_CFG_PARAM(15);
		break;
	case 16:	/* HBA 16 */
		HBA_SPECIFIC_CFG_PARAM(16);
		break;
	case 17:	/* HBA 17 */
		HBA_SPECIFIC_CFG_PARAM(17);
		break;
	case 18:	/* HBA 18 */
		HBA_SPECIFIC_CFG_PARAM(18);
		break;
	case 19:	/* HBA 19 */
		HBA_SPECIFIC_CFG_PARAM(19);
		break;
	case 20:	/* HBA 20 */
		HBA_SPECIFIC_CFG_PARAM(20);
		break;
	case 21:	/* HBA 21 */
		HBA_SPECIFIC_CFG_PARAM(21);
		break;
	case 22:	/* HBA 22 */
		HBA_SPECIFIC_CFG_PARAM(22);
		break;
	case 23:	/* HBA 23 */
		HBA_SPECIFIC_CFG_PARAM(23);
		break;
	case 24:	/* HBA 24 */
		HBA_SPECIFIC_CFG_PARAM(24);
		break;
	case 25:	/* HBA 25 */
		HBA_SPECIFIC_CFG_PARAM(25);
		break;
	case 26:	/* HBA 26 */
		HBA_SPECIFIC_CFG_PARAM(26);
		break;
	case 27:	/* HBA 27 */
		HBA_SPECIFIC_CFG_PARAM(27);
		break;
	case 28:	/* HBA 28 */
		HBA_SPECIFIC_CFG_PARAM(28);
		break;
	case 29:	/* HBA 29 */
		HBA_SPECIFIC_CFG_PARAM(29);
		break;
	case 30:	/* HBA 30 */
		HBA_SPECIFIC_CFG_PARAM(30);
		break;
	case 31:	/* HBA 31 */
		HBA_SPECIFIC_CFG_PARAM(31);
		break;
	default:
		break;
	}
	return (value);
}



void
lpfc_sleep_ms(lpfcHBA_t * phba, int cnt)
{
	if (in_interrupt())
		mdelay(cnt);
	else {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((cnt * HZ / 1000) + 1);
	}
	return;
}

void
lpfc_drvr_init_lock(lpfcHBA_t * phba)
{

	spin_lock_init(&phba->drvrlock);
	return;
}

void
lpfc_drvr_lock(lpfcHBA_t * phba, unsigned long *iflag)
{
	unsigned long flag;

	flag = 0;
	spin_lock_irqsave(&phba->drvrlock, flag);
	*iflag = flag;
	phba->iflag = flag;
	return;
}

void
lpfc_drvr_unlock(lpfcHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;

	flag = phba->iflag;
	spin_unlock_irqrestore(&phba->drvrlock, flag);
	return;
}

int
lpfc_biosparam(Disk * disk, kdev_t n, int ip[])
{
	int size = disk->capacity;

	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
	if (ip[2] > 1024) {
		ip[0] = 255;
		ip[1] = 63;
		ip[2] = size / (ip[0] * ip[1]);
#ifndef FC_EXTEND_TRANS_A
		if (ip[2] > 1023)
			ip[2] = 1023;
#endif
	}
	return (0);
}


void
lpfc_nodev(unsigned long l)
{
	return;
}


int
lpfc_sleep(lpfcHBA_t * phba, void *wait_q_head, long tmo)
{
	wait_queue_t wq_entry;
	unsigned long iflag = phba->iflag;
	int rc = 1;
	long left;

	init_waitqueue_entry(&wq_entry, current);
	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue((wait_queue_head_t *) wait_q_head, &wq_entry);
	if (tmo > 0) {
		LPFC_DRVR_UNLOCK(phba, iflag);
		left = schedule_timeout(tmo * HZ);
		LPFC_DRVR_LOCK(phba, iflag);
	} else {
		LPFC_DRVR_UNLOCK(phba, iflag);
		schedule();
		LPFC_DRVR_LOCK(phba, iflag);
		left = 0;
	}
	remove_wait_queue((wait_queue_head_t *) wait_q_head, &wq_entry);

	if (signal_pending(current))
		return (EINTR);
	if (rc > 0)
		return (0);
	else
		return (ETIMEDOUT);
}

void
lpfc_discq_tasklet(lpfcHBA_t * phba, LPFC_DISC_EVT_t * evtp)
{

	/* Queue the cmnd to the iodone tasklet to be scheduled later */
	list_add_tail(&evtp->evt_listp, &phba->task_disc);
	phba->task_discq_cnt++;
	tasklet_schedule(&phba->task_run);
	return;
}

int
lpfc_discq_post_event(lpfcHBA_t * phba, void *arg1, void *arg2, uint32_t evt)
{
	LPFC_DISC_EVT_t  *evtp;

	/* All Mailbox completions and LPFC_ELS_RING rcv ring events will be
	 * queued to tasklet for processing
	 */
	evtp = (LPFC_DISC_EVT_t *) kmalloc(sizeof(LPFC_DISC_EVT_t), GFP_ATOMIC);
	if(evtp == 0) {
		return (0);
	}
	evtp->evt_arg1  = arg1;
	evtp->evt_arg2  = arg2;
	evtp->evt       = evt;
	evtp->evt_listp.next = 0;
	evtp->evt_listp.prev = 0;
	lpfc_discq_tasklet(phba, evtp);
	return (1);
}

void
lpfc_flush_disc_evtq(lpfcHBA_t * phba) {

	LPFC_SLI_t       * psli;
	struct list_head * pos, * pos_tmp;
	struct list_head * cur, * next;
	LPFC_DISC_EVT_t  * evtp;
	IOCB_t           * cmd = 0;
	LPFC_IOCBQ_t     * cmdiocbp;
	LPFC_IOCBQ_t     * rspiocbp;
	LPFC_IOCBQ_t     * saveq;
	LPFC_RING_MASK_t * func;

	psli = &phba->sli;

	list_for_each_safe(pos, pos_tmp, &phba->task_disc) {
		evtp = list_entry(pos, LPFC_DISC_EVT_t, evt_listp);

		if ((evtp->evt == LPFC_EVT_SOL_IOCB)) {
			list_del(&evtp->evt_listp);
			phba->task_discq_cnt--;

			cmdiocbp = (LPFC_IOCBQ_t *)(evtp->evt_arg1);
			saveq = (LPFC_IOCBQ_t *)(evtp->evt_arg2);
			cmd = &cmdiocbp->iocb;
			cmd->ulpStatus = IOSTAT_LOCAL_REJECT;
			cmd->un.ulpWord[4] = IOERR_SLI_ABORTED;
			(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
			/* Free up iocb buffer chain for command just
			   processed */
			list_for_each_safe(cur, next, &saveq->list) {
				rspiocbp = list_entry(cur, LPFC_IOCBQ_t, list);
				list_del(&rspiocbp->list);
				lpfc_iocb_free(phba, rspiocbp);
			}
			lpfc_iocb_free(phba, saveq);
			kfree(evtp);
			continue;
		}
		if ((evtp->evt == LPFC_EVT_UNSOL_IOCB)) {
                        list_del(&evtp->evt_listp);
                        phba->task_discq_cnt--;

                        func = (LPFC_RING_MASK_t *)(evtp->evt_arg1);
                        saveq = (LPFC_IOCBQ_t *)(evtp->evt_arg2);
                        (func->lpfc_sli_rcv_unsol_event) (phba,
							  &psli->ring[LPFC_ELS_RING], saveq);
                        /* Free up iocb buffer chain for command just
                           processed */
                        list_for_each_safe(cur, next, &saveq->list) {
                                rspiocbp = list_entry(cur, LPFC_IOCBQ_t, list);
                                list_del(&rspiocbp->list);
                                lpfc_iocb_free(phba, rspiocbp);
                        }
                        lpfc_iocb_free(phba, saveq);
                        kfree(evtp);
                        continue;
			
                }

		else
			continue;
	}
}

void
lpfc_tasklet(unsigned long p)
{
	lpfcHBA_t        * phba = (lpfcHBA_t *)p;
	LPFC_SLI_t       * psli;
	LPFC_DISC_EVT_t  * evtp;
	struct list_head * cur, * next;
	LPFC_MBOXQ_t     * pmb;
	LPFC_IOCBQ_t     * cmdiocbp;
	LPFC_IOCBQ_t     * rspiocbp;
	LPFC_IOCBQ_t     * saveq;
	LPFC_RING_MASK_t * func;
	unsigned long      flags;

	psli = &phba->sli;
	LPFC_DRVR_LOCK(phba, flags);

	/* check discovery event list */
	while (!list_empty(&phba->task_disc)) {
		evtp = list_entry(phba->task_disc.next, LPFC_DISC_EVT_t, evt_listp);
		list_del(&evtp->evt_listp);
		phba->task_discq_cnt--;

		switch(evtp->evt) {
		case LPFC_EVT_MBOX:
			pmb = (LPFC_MBOXQ_t *)(evtp->evt_arg1);
			if (pmb->mbox_cmpl) {
				(pmb->mbox_cmpl) (phba, pmb);
			}
			else {
				lpfc_mbox_free(phba, pmb);
			}
			break;

		case LPFC_EVT_SOL_IOCB:
			cmdiocbp = (LPFC_IOCBQ_t *)(evtp->evt_arg1);
			saveq = (LPFC_IOCBQ_t *)(evtp->evt_arg2);
			(cmdiocbp->iocb_cmpl) (phba, cmdiocbp, saveq);
			/* Free up iocb buffer chain for command just
			   processed */
			list_for_each_safe(cur, next, &saveq->list) {
				rspiocbp = list_entry(cur, LPFC_IOCBQ_t, list);
				list_del(&rspiocbp->list);
				lpfc_iocb_free(phba, rspiocbp);
			}
			lpfc_iocb_free(phba, saveq);
			break;
		case LPFC_EVT_UNSOL_IOCB:
			func = (LPFC_RING_MASK_t *)(evtp->evt_arg1);
			saveq = (LPFC_IOCBQ_t *)(evtp->evt_arg2);
			(func->lpfc_sli_rcv_unsol_event) (phba, 
	 		&psli->ring[LPFC_ELS_RING], saveq);
			/* Free up iocb buffer chain for command just
			   processed */
			list_for_each_safe(cur, next, &saveq->list) {
				rspiocbp = list_entry(cur, LPFC_IOCBQ_t, list);
				list_del(&rspiocbp->list);
				lpfc_iocb_free(phba, rspiocbp);
			}
			lpfc_iocb_free(phba, saveq);
			break;
		}
		kfree(evtp);
	}
	LPFC_DRVR_UNLOCK(phba, flags);
	return;
}

#include <scsi_module.c>
MODULE_LICENSE("GPL");

/*
 * Note: PPC64 architecture has function descriptors,
 * so insmod on 2.4 does not automatically export all symbols.
 */
EXPORT_SYMBOL(lpfc_add_bind);
EXPORT_SYMBOL(lpfc_block_requests);
EXPORT_SYMBOL(lpfc_build_scsi_cmd);
EXPORT_SYMBOL(lpfc_decode_firmware_rev);
EXPORT_SYMBOL(lpfc_del_bind);
EXPORT_SYMBOL(lpfcDRVR);
EXPORT_SYMBOL(lpfc_drvr_name);
EXPORT_SYMBOL(lpfc_drvr_lock);
EXPORT_SYMBOL(lpfc_drvr_unlock);
EXPORT_SYMBOL(lpfc_els_free_iocb);
EXPORT_SYMBOL(lpfc_find_lun);
EXPORT_SYMBOL(lpfc_findnode_did);
EXPORT_SYMBOL(lpfc_findnode_scsiid);
EXPORT_SYMBOL(lpfc_findnode_wwnn);
EXPORT_SYMBOL(lpfc_findnode_wwpn);
EXPORT_SYMBOL(lpfc_free_scsi_buf);
EXPORT_SYMBOL(lpfc_geportname);
EXPORT_SYMBOL(lpfc_get_hba_sym_node_name);
EXPORT_SYMBOL(lpfc_get_scsi_buf);
EXPORT_SYMBOL(lpfc_init_link);
EXPORT_SYMBOL(lpfc_iocb_alloc);
EXPORT_SYMBOL(lpfc_iocb_free);
EXPORT_SYMBOL(lpfc_issue_ct_rsp);
EXPORT_SYMBOL(lpfc_issue_els_adisc);
EXPORT_SYMBOL(lpfc_issue_els_logo);
EXPORT_SYMBOL(lpfc_issue_els_plogi);
EXPORT_SYMBOL(lpfc_mbox_alloc);
EXPORT_SYMBOL(lpfc_mbox_free);
EXPORT_SYMBOL(lpfc_mbuf_alloc);
EXPORT_SYMBOL(lpfc_mbuf_free);
EXPORT_SYMBOL(lpfc_page_alloc);
EXPORT_SYMBOL(lpfc_page_free);
EXPORT_SYMBOL(lpfc_nlp_alloc);
EXPORT_SYMBOL(lpfc_nlp_free);
EXPORT_SYMBOL(lpfc_nlp_bind);
EXPORT_SYMBOL(lpfc_nlp_plogi);
EXPORT_SYMBOL(lpfc_offline);
EXPORT_SYMBOL(lpfc_online);
EXPORT_SYMBOL(lpfc_prep_els_iocb);
EXPORT_SYMBOL(lpfc_release_version);
EXPORT_SYMBOL(lpfc_scsi_lun_reset);
EXPORT_SYMBOL(lpfc_scsi_tgt_reset);
EXPORT_SYMBOL(lpfc_sleep);
EXPORT_SYMBOL(lpfc_sleep_ms);
EXPORT_SYMBOL(lpfc_sli_brdreset);
EXPORT_SYMBOL(lpfc_sli_issue_iocb_wait);
EXPORT_SYMBOL(lpfc_sli_issue_mbox);
EXPORT_SYMBOL(lpfc_sli_issue_mbox_wait);
EXPORT_SYMBOL(lpfc_sli_next_iotag);
EXPORT_SYMBOL(lpfc_sli_pcimem_bcopy);
EXPORT_SYMBOL(lpfc_unblock_requests);
EXPORT_SYMBOL(lpfc_get_phba_by_inst);

EXPORT_SYMBOL(lpfc_sched_continue_hba);
EXPORT_SYMBOL(lpfc_sched_pause_hba);
