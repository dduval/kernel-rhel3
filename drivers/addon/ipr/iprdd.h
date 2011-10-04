/*****************************************************************************/
/* iprdd.h -- driver for IBM Power Linux RAID adapters                       */
/*                                                                           */
/* Written By: Brian King, IBM Corporation                                   */
/*                                                                           */
/* Copyright (C) 2003 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/*****************************************************************************/

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprdd.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#ifndef iprdd_h
#define iprdd_h

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#include <scsi/scsi.h>
#include <asm/semaphore.h>
#include <linux/raid/md_compatible.h>

#ifndef iprlib_h
#include "iprlib.h"
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)
#define init_completion(x)              init_MUTEX_LOCKED(x)
#define DECLARE_COMPLETION(x)           DECLARE_MUTEX_LOCKED(x)
#define wait_for_completion(x)          down(x)
#define complete(x)                     up(x)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#define scsi_assign_lock(host, lock)
#endif

#define ipr_flush_signals md_flush_signals

#define IPR \
{\
name:                           "IPR",               /* Driver Name               */\
proc_info:                      ipr_proc_info,       /* /proc driver info         */\
detect:                         ipr_detect,          /* Detect Host Adapter       */\
release:                        ipr_release,         /* Release Host Adapter      */\
info:                           ipr_info,            /* Driver Info Function      */\
queuecommand:                   ipr_queue,           /* Queue Command Function    */\
eh_abort_handler:               ipr_abort,           /* Abort Command Function    */\
eh_device_reset_handler:        ipr_dev_reset,       /* Device Reset Function     */\
eh_host_reset_handler:          ipr_host_reset,      /* Host Reset Function       */\
bios_param:                     ipr_biosparam,       /* Disk BIOS Parameters      */\
select_queue_depths:            ipr_select_q_depth,  /* Select Queue depth        */\
can_queue:                      IPR_MAX_COMMANDS,    /* Can Queue                 */\
this_id:                        -1,                  /* HBA Target ID             */\
sg_tablesize:                   IPR_MAX_SGLIST,      /* Scatter/Gather Table Size */\
max_sectors:                    IPR_MAX_SECTORS,     /* Max size per op           */\
cmd_per_lun:                    IPR_MAX_CMD_PER_LUN, /* SCSI Commands per LUN     */\
present:                        0,                   /* Present                   */\
unchecked_isa_dma:              0,                   /* Default Unchecked ISA DMA */\
use_clustering:                 ENABLE_CLUSTERING,   /* Enable Clustering         */\
use_new_eh_code:                1                    /* Use the new EH code       */\
}

#define IPR_MAX_NUM_DUMP_PAGES               ((IPR_MAX_IOA_DUMP_SIZE / PAGE_SIZE) + 1)

struct ipr_dump_ioa_entry
{
    struct ipr_dump_entry_header header;
    u32 next_page_index;
    u32 page_offset;
    u32 format;
#define IPR_SDT_FMT1    1
#define IPR_SDT_FMT2    2
#define IPR_SDT_UNKNOWN 3
    u32 reserved;
    struct ipr_sdt sdt;
    u32 *p_ioa_data[IPR_MAX_NUM_DUMP_PAGES];
};

enum ipr_error_class
{
    IPR_NO_ERR_CLASS = 0,
    IPR_ERR_CLASS_PERM,
    IPR_ERR_CLASS_PRED_ANALYSIS,
    IPR_ERR_CLASS_SERVICE_ACTION_PT,
    IPR_ERR_CLASS_TEMP,
    IPR_ERR_CLASS_RETRYABLE,
    IPR_ERR_CLASS_INFO,
    IPR_ERR_CLASS_MAX_CLASS
};

struct ipr_error_class_table_t
{
    enum ipr_error_class err_class;
    char *printk_level;
    char *p_class;
};

struct ipr_error_table_t
{
    u32 ioasc;
    u16 dev_urc;
    u16 iop_urc;
    enum ipr_error_class err_class;
    char *p_error;
};

struct ipr_cmnd
{
    struct ipr_ccb ccb;
    struct ipr_cmnd *p_next;
    struct ipr_cmnd *p_prev;
    struct ipr_cmnd *p_cancel_op;
    Scsi_Cmnd *p_scsi_cmd;
    Scsi_Cmnd *p_saved_scsi_cmd;
    wait_queue_head_t wait_q;
    struct timer_list timer;
    void (*done) (struct ipr_shared_config *, struct ipr_ccb *);
    struct ipr_sglist sglist[IPR_MAX_SGLIST];
};

struct ipr_ioctl_cmnd
{
    struct ipr_cmnd sis_cmd;
    struct ipr_ioctl_cmnd *p_next;
    struct ipr_ioctl_cmnd *p_prev;
};

struct ipr_dnload_sglist
{
    u32 order;
    u32 num_sg;
    struct scatterlist scatterlist[1];
};

/* Per-controller data */
typedef struct _sis_host_config {
    struct ipr_shared_config shared;

    struct _sis_host_config *p_next;

    u32 flag;
#define IPR_OPERATIONAL              0x00000001      /* We can talk to the IOA */
#define IPR_UNIT_CHECKED             0x00000002      /* The IOA has unit checked */
#define IPR_IOA_NEEDS_RESET          0x00000004      /* IOA fatal error, not UC */
#define IPR_KILL_KERNEL_THREAD       0x00000008      /* Kill the kernel thread */
#define IPR_ALLOW_HCAMS              0x00000010      /* Allow HCAMS to be processed/sent to IOA */
#define IPR_IN_RESET_RELOAD          0x00000020      /* IOA in reset/reload processing */
#define IPR_ALLOW_REQUESTS           0x00000040      /* ipr_do_req can accept requests */

    u16 minor_num;
    u16 major_num;

    /* Queue for free command blocks that we can use for incoming ops */
    char ipr_free_label[8];
#define IPR_FREEQ_LABEL      "free-q"
    struct ipr_cmnd *qFreeH;
    struct ipr_cmnd *qFreeT;

    /* Queue for command blocks that are currently outstanding to the adapter */
    char ipr_pending_label[8];
#define IPR_PENDQ_LABEL      "pend-q"
    struct ipr_cmnd *qPendingH;
    struct ipr_cmnd *qPendingT;

    /* Queue for command blocks that have been taken off the HRRQ, but not processed yet */
    char ipr_comp_label[8];
#define IPR_COMPQ_LABEL      "comp-q"
    struct ipr_cmnd *qCompletedH;
    struct ipr_cmnd *qCompletedT;

    /* Queue for command blocks that need ERP - put on at interrupt level and
     taken off at task level. This queue is only used for GPDD ops. */
    char ipr_err_label[8];
#define IPR_ERRQ_LABEL       "err-q"
    struct ipr_cmnd *qErrorH;
    struct ipr_cmnd *qErrorT;

    struct Scsi_Host *host;
    struct pci_dev *pdev;

    struct task_struct *task_thread; /* our kernel thread */
    int task_pid;
    u32 block_host_ops;

    struct semaphore ioctl_semaphore;

    struct scsi_cmnd *p_scsi_ops_to_fail;
    const struct ipr_ioa_cfg_t *p_ioa_cfg;

    char ipr_hcam_label[8];
#define IPR_HCAM_LABEL       "hcams"
    struct ipr_hostrcb *hostrcb[IPR_NUM_HCAMS];
    ipr_dma_addr hostrcb_dma[IPR_NUM_HCAMS];

    struct ipr_hostrcb *new_hostrcb[IPR_NUM_HCAMS];
    struct ipr_hostrcb *done_hostrcb[IPR_NUM_HCAMS];

    void *completion;

    wait_queue_head_t wait_q;

#define IPR_CONFIG_SAVE_WORDS 64
    u32 pci_cfg_buf[IPR_CONFIG_SAVE_WORDS];
    u32 bar_size_reg;
    u16 saved_pcix_command_reg;
    u16 reserved;

    char ipr_sis_cmd_label[8];
#define IPR_SIS_CMD_LABEL    "sis_cmd"
    struct ipr_cmnd *sis_cmnd_list[IPR_NUM_CMD_BLKS];

} ipr_host_config;

const char *ipr_info(struct Scsi_Host *);
int ipr_detect(Scsi_Host_Template *);
int ipr_release(struct Scsi_Host *);
int ipr_abort(Scsi_Cmnd *);
int ipr_dev_reset(Scsi_Cmnd *);
int ipr_host_reset(Scsi_Cmnd *);
int ipr_queue(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int ipr_biosparam(Disk *, kdev_t, int *);
int ipr_proc_info(char *buffer, char **start, off_t offset,
                     int length, int hostno, int inout);
void ipr_select_q_depth(struct Scsi_Host *, Scsi_Device *);

int ipr_task_thread(void *data);
void ipr_timeout(ipr_host_config *ipr_cfg);
int ipr_toggle_reset(ipr_host_config *ipr_cfg) ;
u32 ipr_get_unique_id(struct ipr_location_data *p_location);
struct ipr_location_data *ipr_get_ioa_location_data(struct pci_dev *p_dev);
void ipr_print_unknown_dev_phys_loc(char *printk_level);
void ipr_log_dev_vpd(struct ipr_resource_entry *p_resource, char *printk_level);
void ipr_log_array_dev_vpd(struct ipr_std_inq_vpids *p_vpids,
                                char *default_ccin,
                                char *printk_level);
void ipr_print_ioa_vpd(struct ipr_std_inq_vpids *p_vpids,
                          char *printk_level);
void ipr_log_dev_current_expected_locations(ipr_host_config *ipr_cfg,
                                               struct ipr_res_addr current_res_addr,
                                               struct ipr_res_addr expected_res_addr,
                                               char *printk_level);
int ipr_invalid_adapter(ipr_host_config *ipr_cfg);

#define ipr_ipl_err(ipr_cfg, ...) \
{ \
printk(KERN_ERR IPR_ERR\
": begin-entry***********************************************"IPR_EOL); \
printk(KERN_ERR IPR_ERR ": "__VA_ARGS__); \
ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_ERR); \
printk(KERN_ERR IPR_ERR\
": end-entry*************************************************"IPR_EOL); \
}

#define ipr_breakpoint_data KERN_ERR IPR_NAME\
": %s: %s: Line: %d ipr_cfg: %p"IPR_EOL, __FILE__, \
__FUNCTION__, __LINE__, ipr_cfg

#define ipr_trace { \
printk(KERN_ERR IPR_NAME\
": %s: %s: Line: %d"IPR_EOL, __FILE__, __FUNCTION__, __LINE__); \
}

#if defined(CONFIG_KDB) && !defined(CONFIG_PPC_ISERIES)
#define ipr_breakpoint {printk(ipr_breakpoint_data); KDB_ENTER();}
#define ipr_breakpoint_or_die {printk(ipr_breakpoint_data); KDB_ENTER();}
#else
#define ipr_breakpoint
#define ipr_breakpoint_or_die panic(ipr_breakpoint_data)
#endif

#if IPR_DISABLE_RESET_RELOAD == 1
#define ipr_break_if_reset_reload_disabled ipr_breakpoint
#define ipr_break_or_die_if_reset_reload_disabled ipr_breakpoint_or_die
#else
#define ipr_break_if_reset_reload_disabled
#define ipr_break_or_die_if_reset_reload_disabled
#endif

#define ipr_dbg_trace IPR_DBG_CMD(ipr_trace)
#endif
