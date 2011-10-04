/*****************************************************************************/
/* iprdd.c -- driver for IBM Power Linux RAID adapters                       */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprdd.c,v 1.3.2.2 2003/11/10 19:19:51 bjking1 Exp $
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/wait.h>
#include <linux/tqueue.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/ctype.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#ifdef CONFIG_KDB
#include <asm/kdb.h>
#endif

#include <linux/module.h>

MODULE_AUTHOR ("Brian King <bjking1@us.ibm.com>");
MODULE_PARM(trace, "b");
MODULE_PARM_DESC(trace, "IOA command tracing - traces commands issued to IOA");
MODULE_PARM(verbose, "b");
MODULE_PARM_DESC(verbose, "Set to 0 - 2 for increasing verbosity of device driver");
MODULE_PARM(testmode, "b");
MODULE_PARM_DESC(testmode, "Internal use only");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,10)
MODULE_LICENSE("GPL");
#endif

#if !defined(CONFIG_CHR_DEV_SG) && !defined(CONFIG_CHR_DEV_SG_MODULE)
#error This device driver requires CONFIG_CHR_DEV_SG for proper operation
#endif

#ifndef CONFIG_PROC_FS
#error This device driver requires CONFIG_PROC_FS for proper operation
#endif

#include <sd.h>
#include <scsi.h>
#include <hosts.h>
#include <constants.h>

#ifndef iprdd_h
#include "iprdd.h"
#endif

#if defined(CONFIG_PPC_ISERIES)
#ifndef ipr_iseries_h
#include "ipr_iseries.h"
#endif
#elif defined(CONFIG_PPC_PSERIES) && defined(CONFIG_PPC64)
#ifndef ipr_pseries_h
#include "ipr_pseries.h"
#endif
#else
#ifndef ipr_generic_h
#include "ipr_generic.h"
#endif
#endif

#if defined(CONFIG_PPC64)
#define IPR_GFP_DMA          0
#elif defined(CONFIG_IA64) || defined(CONFIG_PPC)
#define IPR_GFP_DMA          GFP_DMA
#else
#define IPR_GFP_DMA          0
#endif

#define IPR_SENSE_BUFFER_COPY_SIZE \
IPR_MIN(IPR_SENSE_BUFFERSIZE, SCSI_SENSE_BUFFERSIZE)

enum ipr_shutdown_type
{
    IPR_SHUTDOWN_NONE,
    IPR_SHUTDOWN_ABBREV,
    IPR_SHUTDOWN_NORMAL,
    IPR_SHUTDOWN_PREPARE_FOR_NORMAL
};

/**************************************************
 *   Global Data
 **************************************************/
static int ipr_num_ctlrs = 0;
static ipr_host_config *ipr_cfg_head = NULL;
static char ipr_buf[2048];
static int ipr_init_finished = 0;
static const int ipr_debug = IPR_DEBUG;
static const int ipr_mem_debug = IPR_MEMORY_DEBUG;
static u8 trace = IPR_TRACE;
static u8 verbose = IPR_DEFAULT_DEBUG_LEVEL;
static u8 testmode = 0;
static wait_queue_head_t ipr_sdt_wait_q;
static int ipr_kmalloced_mem = 0;
static struct ipr_dump_ioa_entry *p_ipr_dump_ioa_entry = NULL;
static struct ipr_dump_driver_header *p_ipr_dump_driver_header = NULL;

static enum ipr_get_sdt_state
{
    INACTIVE,
    WAIT_FOR_DUMP,
    NO_DUMP,
    GET_DUMP,
    DUMP_OBTAINED
} ipr_get_sdt_state = INACTIVE;

static const char ipr_version[] = {IPR_NAME" version="IPR_FULL_VERSION};
extern const char ipr_platform[];

/**************************************************
 *   Function Prototypes
 **************************************************/
static void ipr_add_ioa_to_tail(ipr_host_config *ipr_cfg);
static void ipr_remove_ioa_from_list(ipr_host_config *ipr_cfg);
static void ipr_put_ioctl_cmnd_to_free(ipr_host_config *ipr_cfg,
                                          struct ipr_cmnd* p_sis_cmnd);
static void ipr_ops_done(ipr_host_config *ipr_cfg);
static void ipr_find_ioa(Scsi_Host_Template *, u16, u16);
static int ipr_find_ioa_part2(void);
static u32 ipr_init_ioa(ipr_host_config *ipr_cfg);
static u32 ipr_kill_kernel_thread(ipr_host_config *ipr_cfg);
static u32 ipr_init_ioa_part1(ipr_host_config *ipr_cfg);
static u32 ipr_init_ioa_part2(ipr_host_config *ipr_cfg);
void ipr_isr(int irq, void *devp, struct pt_regs *regs);
static void ipr_fail_all_ops(ipr_host_config *ipr_cfg);
static void ipr_return_failed_ops(ipr_host_config *ipr_cfg) ;
static int ipr_notify_sys(struct notifier_block *this, unsigned long code,
                             void *unused);
static u32 ipr_shutdown_ioa(ipr_host_config *ipr_cfg,
                               enum ipr_shutdown_type type);
static void ipr_ioctl_cmd_done(struct ipr_shared_config *p_shared_cfg,
                                  struct ipr_ccb *p_sis_ccb);
static u32 ipr_send_blocking_ioctl(ipr_host_config *ipr_cfg,
                                      struct ipr_cmnd *p_sis_cmnd,
                                      u32 timeout,
                                      u8 retries);
static void ipr_free_all_resources(ipr_host_config *ipr_cfg,
                                      int free_reboot_notif, int free_chrdev);
static void ipr_handle_log_data(ipr_host_config *ipr_cfg,
                                   struct ipr_hostrcb *p_hostrcb);
static u32 ipr_get_error(u32 ioasc);
static int ipr_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd_in, unsigned long arg);
static int ipr_open(struct inode *inode, struct file *filp);
static int ipr_close(struct inode *inode, struct file *filp);
static void ipr_mailbox(ipr_host_config *ipr_cfg);
static int ipr_ioa_reset(ipr_host_config *ipr_cfg, enum ipr_irq_state irq_state);
static int ipr_reset_reload(ipr_host_config *ipr_cfg,
                               enum ipr_shutdown_type shutdown_type);
static u16 ipr_adjust_urc(u32 error_index,
                             struct ipr_res_addr resource_addr,
                             u32 ioasc,
                             u32 dev_urc,
                             char *p_error_string);
static void ipr_get_ioa_name(ipr_host_config *ipr_cfg,
                                char *dev_name);
static ipr_host_config * ipr_get_host(int dev);
static void ipr_wake_task(ipr_host_config *ipr_cfg);
static int ipr_cancelop(ipr_host_config *ipr_cfg,
                           struct ipr_cmnd *p_sis_cmnd,
                           Scsi_Cmnd *p_scsi_cmd);
static void ipr_unit_check_no_data(ipr_host_config *ipr_cfg);
static ipr_dma_addr ipr_get_hcam_dma_addr(ipr_host_config *ipr_cfg,
                                                struct ipr_hostrcb *p_hostrcb);
static void ipr_get_unit_check_buffer(ipr_host_config *ipr_cfg);
static void ipr_block_all_requests(ipr_host_config *ipr_cfg);
static void ipr_unblock_all_requests(ipr_host_config *ipr_cfg);
static void ipr_block_midlayer_requests(ipr_host_config *ipr_cfg);
static void ipr_unblock_midlayer_requests(ipr_host_config *ipr_cfg);
static int ipr_cancel_all(Scsi_Cmnd *p_scsi_cmd);
static int ipr_alloc_ucode_buffer(u32 buf_len,
                                     struct ipr_dnload_sglist **pp_scatterlist);
static void ipr_free_ucode_buffer(struct ipr_dnload_sglist *p_dnld);
static int ipr_copy_ucode_buffer(struct ipr_dnload_sglist *p_scatterlist,
                                    u8 *p_write_buffer,
                                    u32 buf_len);
static struct ipr_resource_entry *ipr_get_ses_resource(ipr_host_config *ipr_cfg,
                                                             struct ipr_res_addr res_addr);
static void ipr_sleep_no_lock(signed long delay);
static struct ipr_drive_elem_status*
ipr_get_elem_status(struct ipr_encl_status_ctl_pg* p_encl_status_ctl_pg,
                       u8 scsi_id);
static int ipr_conc_maint(ipr_host_config *ipr_cfg,
                             struct ipr_res_addr res_addr, u32 type, u32 delay);
static int ipr_suspend_device_bus(ipr_host_config *ipr_cfg,
                                     struct ipr_res_addr res_addr,
                                     u8 option);
static int ipr_resume_device_bus(ipr_host_config *ipr_cfg,
                                    struct ipr_res_addr res_addr);
static int ipr_ses_receive_diagnostics(ipr_host_config *ipr_cfg,
                                          u8 page,
                                          void *p_buffer,
                                          ipr_dma_addr buffer_dma_addr,
                                          u16 bufflen,
                                          struct ipr_resource_entry *p_resource);
static int ipr_ses_send_diagnostics(ipr_host_config *ipr_cfg,
                                       void *p_buffer,
                                       ipr_dma_addr buffer_dma_addr,
                                       u16 bufflen,
                                       struct ipr_resource_entry *p_resource);
static void ipr_print_sense(u8 cmd, unsigned char *p_buf);
static signed long ipr_sleep_on_timeout(spinlock_t *p_lock,
                                           wait_queue_head_t *p_wait_head, long timeout);
static void ipr_interruptible_sleep_on(spinlock_t *p_lock,
                                          wait_queue_head_t *p_wait_head);
static void ipr_sleep_on(spinlock_t *p_lock,
                            wait_queue_head_t *p_wait_head);
static int ipr_sdt_copy(ipr_host_config *ipr_cfg,
                           unsigned long pci_address, u32 length, u32 swap,
                           unsigned long timeout);
static int ipr_get_ioa_smart_dump(ipr_host_config *ipr_cfg);
static int ipr_copy_sdt_to_user(u8 *p_dest_buffer, u32 length);
static void ipr_start_erp(struct ipr_cmnd *p_sis_cmnd);
static void ipr_end_erp(struct ipr_cmnd *p_sis_cmnd);
static void *ipr_get_free_pages(u32 flags, u32 order);
static void *ipr_get_free_page(u32 flags);
static void ipr_free_pages(void *ptr, u32 order);
static void ipr_free_page(void *ptr);
static u32 ipr_xlate_malloc_flags(u32 flags);

/**************************************************
 *   Internal Data Structures
 **************************************************/
static struct notifier_block ipr_notifier =
{
    ipr_notify_sys,
    NULL,
    0
};

static struct file_operations ipr_fops =
{
    ioctl:      ipr_ioctl,
    open:       ipr_open,
    release:    ipr_close,
};

static const
struct ipr_error_class_table_t ipr_error_class_table[] =
{
    {IPR_NO_ERR_CLASS, KERN_DEBUG, "None"},
    {IPR_ERR_CLASS_PERM, KERN_ERR, "Permanent"},
    {IPR_ERR_CLASS_PRED_ANALYSIS, KERN_NOTICE, "Statistical"},
    {IPR_ERR_CLASS_SERVICE_ACTION_PT, KERN_CRIT, "Threshold"},
    {IPR_ERR_CLASS_TEMP, KERN_WARNING, "Temporary"},
    {IPR_ERR_CLASS_RETRYABLE, KERN_WARNING, "Recoverable"},
    {IPR_ERR_CLASS_INFO, KERN_INFO, "Informational"}
};

static const struct pci_device_id ipr_pci_table[] =
{
    {
        vendor: PCI_VENDOR_ID_MYLEX,
        device: PCI_DEVICE_ID_GEMSTONE,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: IPR_SUBS_DEV_ID_5702,
    },
    {
        vendor: PCI_VENDOR_ID_IBM,
        device: PCI_DEVICE_ID_IBM_SNIPE,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: IPR_SUBS_DEV_ID_2780,
    },
    {
        vendor: PCI_VENDOR_ID_MYLEX,
        device: PCI_DEVICE_ID_GEMSTONE,
        subvendor: PCI_VENDOR_ID_IBM,
        subdevice: IPR_SUBS_DEV_ID_5703,
    },
    { }
};
MODULE_DEVICE_TABLE(pci, ipr_pci_table);

struct ipr_pci_dev_table_t
{
    u16 vendor_id;
    u16 device_id;
    u16 subsystem_id;
    u16 ccin;
    char ccin_str[10];
    u16 num_physical_buses;
    u16 max_cmd_len;
    u8 format_immed:1;
};

static const 
struct ipr_pci_dev_table_t ipr_pci_dev_table[] =
{
    {PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_SNIPE , IPR_SUBS_DEV_ID_2780, 0x2780, "2780", 4, 16, 1},
    {PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_GEMSTONE, IPR_SUBS_DEV_ID_5702, 0x5702, "SCSI IOA", 2, 16, 0},
    {PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_GEMSTONE, IPR_SUBS_DEV_ID_5703, 0x5703, "RAID IOA", 2, 16, 1}
};

/* This table describes the differences between DMA controller chips */
static const
struct ipr_ioa_cfg_t ipr_ioa_cfg[] =
{
    { /* Gemstone based IOAs */
        PCI_VENDOR_ID_MYLEX,
        PCI_DEVICE_ID_GEMSTONE,
        0,
        IPR_GEMSTONE_MAILBOX_OFFSET,
        IPR_FMT2_MBX_BAR_SEL_MASK,
        IPR_FMT2_MKR_BAR_SEL_SHIFT,
        IPR_FMT2_MBX_ADDR_MASK,
        IPR_SDT_REG_SEL_SIZE_1NIBBLE,
        IPR_CPU_RST_SUPPORT_NONE,
        IPR_FIXUPS_NONE,
        0,
        IPR_SET_MODE_PAGE_20,
        0,
        /* Setup the cache line size to 128 bytes and the latency timer to maximum value. */
        0x0000f820
    },
    { /* Snipe based IOAs */
        PCI_VENDOR_ID_IBM,
        PCI_DEVICE_ID_IBM_SNIPE,
        0,
        IPR_SNIPE_MAILBOX_OFFSET,
        IPR_FMT2_MBX_BAR_SEL_MASK,
        IPR_FMT2_MKR_BAR_SEL_SHIFT,
        IPR_FMT2_MBX_ADDR_MASK,
        IPR_SDT_REG_SEL_SIZE_1NIBBLE,
        IPR_CPU_RST_SUPPORT_NONE,
        IPR_FIXUPS_NONE,
        0,
        IPR_SET_MODE_PAGE_20,
        0,
        /* Setup the cache line size to 128 bytes and the latency timer to maximum value. */
      /*  xx 0x0000f820 */
        0x0000f810
    },
};

/*  A constant array of IOASCs/URCs/Error Messages */
static const
struct ipr_error_table_t ipr_error_table[] =
{
    {0x00000000, 0x0000, 0x0000, IPR_ERR_CLASS_PERM, "An unknown error was received."},
    {0x01080000, 0xFFFE, 0x8140, IPR_ERR_CLASS_PRED_ANALYSIS, "Temporary disk bus error"},
    {0x01170600, 0xFFF9, 0x8141, IPR_ERR_CLASS_TEMP, "Temporary disk data error"},
    {0x01170900, 0xFFF7, 0x8141, IPR_ERR_CLASS_TEMP, "Temporary disk data error"},
    {0x01180200, 0x7001, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Temporary disk data error"},
    {0x01180500, 0xFFF9, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Temporary disk data error"},
    {0x01180600, 0xFFF7, 0x8141, IPR_ERR_CLASS_TEMP, "Temporary disk data error"},
    {0x01418000, 0x0000, 0xFF3D, IPR_ERR_CLASS_PRED_ANALYSIS, "Soft IOA error recovered by the IOA"},
    {0x01440000, 0xFFF6, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Disk device detected recoverable error"},
    {0x01448100, 0xFFF6, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Disk device detected recoverable error"},
    {0x01448200, 0x0000, 0xFF3D, IPR_ERR_CLASS_PRED_ANALYSIS, "Soft IOA error recovered by the IOA"},
    {0x01448300, 0xFFFA, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Temporary disk bus error"},
    {0x014A0000, 0xFFF6, 0x8141, IPR_ERR_CLASS_PRED_ANALYSIS, "Temporary disk bus error"},
    {0x015D0000, 0xFFF6, 0x8145, IPR_ERR_CLASS_SERVICE_ACTION_PT, "Disk device detected recoverable error"},
    {0x015D9200, 0x0000, 0x8009, IPR_ERR_CLASS_SERVICE_ACTION_PT, "Impending cache battery pack failure"},
    {0x02040400, 0x0000, 0x34FF, IPR_ERR_CLASS_INFO, "Disk device format in progress"},
    {0x02670100, 0x3020, 0x3400, IPR_ERR_CLASS_PERM, "Storage subsystem configuration error"},
    {0x03110B00, 0xFFF5, 0x3400, IPR_ERR_CLASS_PERM, "Disk sector read error"},
    {0x03110C00, 0x0000, 0x0000, IPR_ERR_CLASS_PERM, "Disk sector read error"}, /* 0x7000, 0x3400 */
    {0x03310000, 0xFFF3, 0x3400, IPR_ERR_CLASS_PERM, "Disk media format bad"},
    {0x04050000, 0x3002, 0x3400, IPR_ERR_CLASS_RETRYABLE, "Addressed device failed to respond to selection"},
    {0x04080000, 0x3100, 0x3100, IPR_ERR_CLASS_PERM, "IOA detected interface error"},
    {0x04080100, 0x3109, 0x3400, IPR_ERR_CLASS_RETRYABLE, "IOA timed out a disk command"},
    {0x04088000, 0x0000, 0x3120, IPR_ERR_CLASS_PERM, "SCSI bus is not operational"},
    {0x04118000, 0x0000, 0x9000, IPR_ERR_CLASS_PERM, "IOA detected device error"},
    {0x04118100, 0x0000, 0x9001, IPR_ERR_CLASS_PERM, "IOA detected device error"},
    {0x04118200, 0x0000, 0x9002, IPR_ERR_CLASS_PERM, "IOA detected device error"},
    {0x04320000, 0x102E, 0x3400, IPR_ERR_CLASS_PERM, "Out of alternate sectors for disk storage"},
    {0x04330000, 0xFFF4, 0x3400, IPR_ERR_CLASS_PERM, "Disk device problem"},
    {0x04338000, 0xFFF4, 0x3400, IPR_ERR_CLASS_PERM, "Disk device problem"},
    {0x043E0100, 0x0000, 0x3400, IPR_ERR_CLASS_PERM, "Permanent IOA failure"},
    {0x04408500, 0xFFF4, 0x3400, IPR_ERR_CLASS_PERM, "Disk device problem"},
    {0x04418000, 0x0000, 0x8150, IPR_ERR_CLASS_PERM, "Permanent IOA failure"},
    {0x04440000, 0xFFF4, 0x3400, IPR_ERR_CLASS_PERM, "Disk device problem"},
    {0x04448200, 0x0000, 0x8150, IPR_ERR_CLASS_PERM, "Permanent IOA failure"},
    {0x04448300, 0x3010, 0x3400, IPR_ERR_CLASS_PERM, "Disk device returned wrong response to IOA"},
    {0x04448400, 0x0000, 0x8151, IPR_ERR_CLASS_PERM, "IOA Licensed Internal Code error"},
    {0x04448600, 0x0000, 0x8157, IPR_ERR_CLASS_PERM, "Hardware Error, IOA error requiring IOA reset to recover"},
    {0x04449200, 0x0000, 0x8008, IPR_ERR_CLASS_PERM, "A permanent cache battery pack failure occurred"},
    {0x0444A000, 0x0000, 0x9090, IPR_ERR_CLASS_PERM, "Disk unit has been modified after the last known status"},
    {0x0444A200, 0x0000, 0x9081, IPR_ERR_CLASS_PERM, "IOA detected device error"},
    {0x0444A300, 0x0000, 0x9082, IPR_ERR_CLASS_PERM, "IOA detected device error"},
    {0x044A0000, 0x3110, 0x3400, IPR_ERR_CLASS_PERM, "Disk bus interface error occurred"},
    {0x04670400, 0x0000, 0x9091, IPR_ERR_CLASS_PERM, "Incorrect hardware configuration change has been detected"},
    {0x046E0000, 0xFFF4, 0x3400, IPR_ERR_CLASS_PERM, "Disk device problem"},
    {0x06040500, 0x0000, 0x9031, IPR_ERR_CLASS_TEMP, "Array protection temporarily suspended"},
    {0x06040600, 0x0000, 0x9040, IPR_ERR_CLASS_TEMP, "Array protection temporarily suspended"},
    {0x060A8000, 0x0000, 0x0000, IPR_ERR_CLASS_PERM, "Not applicable"},
    {0x06288000, 0x0000, 0x3140, IPR_ERR_CLASS_INFO, "SCSI bus is not operational"},
    {0x06290000, 0xFFFB, 0x3400, IPR_ERR_CLASS_INFO, "Temporary disk bus error"},
    {0x06290500, 0xFFFE, 0x8140, IPR_ERR_CLASS_INFO, "SCSI bus transition to single ended"},
    {0x06290600, 0xFFFE, 0x8140, IPR_ERR_CLASS_INFO, "SCSI bus transition to LVD"},
    {0x06298000, 0xFFFB, 0x3400, IPR_ERR_CLASS_INFO, "Temporary disk bus error"},
    {0x06308000, 0x0000, 0x9093, IPR_ERR_CLASS_PERM, "Read cache device not in correct format"},
    {0x063F0300, 0x3029, 0x3400, IPR_ERR_CLASS_INFO, "A device replacement has occurred"},
    {0x063F8000, 0x0000, 0x9014, IPR_ERR_CLASS_SERVICE_ACTION_PT, "Mode jumper overridden due to cache data in conflicting mode"},
    {0x063F8100, 0x0000, 0x9015, IPR_ERR_CLASS_SERVICE_ACTION_PT, "Mode jumper missing"},
    {0x063F8200, 0x0000, 0x0000, IPR_ERR_CLASS_PERM, "TCQing not active - not applicable"}, /* 0x3131 0x3400 */
    {0x064C8000, 0x0000, 0x9051, IPR_ERR_CLASS_PERM, "IOA cache data exists for a missing or failed device"},
    {0x06670100, 0x0000, 0x9025, IPR_ERR_CLASS_PERM, "Disk unit is not supported at its physical location"},
    {0x06670600, 0x0000, 0x3020, IPR_ERR_CLASS_PERM, "IOA detected a SCSI bus configuration error"},
    {0x06678000, 0x0000, 0x3150, IPR_ERR_CLASS_PERM, "SCSI bus configuration error"},
    {0x06690200, 0x0000, 0x9041, IPR_ERR_CLASS_TEMP, "Array protection temporarily suspended"},
    {0x066B0200, 0x0000, 0x9030, IPR_ERR_CLASS_PERM, "Array no longer protected due to missing or failed disk unit"},
    {0x06808000, 0x0000, 0x8012, IPR_ERR_CLASS_PERM, "Attached read cache devices exceed capacity supported by IOA"},
    {0x07278000, 0x0000, 0x9008, IPR_ERR_CLASS_PERM, "IOA does not support functions expected by devices"},
    {0x07278100, 0x0000, 0x9010, IPR_ERR_CLASS_PERM, "Cache data associated with attached devices cannot be found"},
    {0x07278200, 0x0000, 0x9011, IPR_ERR_CLASS_PERM, "Cache data belongs to devices other than those attached"},
    {0x07278400, 0x0000, 0x9020, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278500, 0x0000, 0x9021, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278600, 0x0000, 0x9022, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278700, 0x0000, 0x9023, IPR_ERR_CLASS_PERM, "Array member(s) not at required resource address"},
    {0x07278800, 0x0000, 0x9024, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278900, 0x0000, 0x9026, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278A00, 0x0000, 0x9027, IPR_ERR_CLASS_PERM, "Array not functional due to present hardware configuration"},
    {0x07278B00, 0x0000, 0x9028, IPR_ERR_CLASS_PERM, "Incorrect hardware configuration change has been detected"},
    {0x07278C00, 0x0000, 0x9050, IPR_ERR_CLASS_PERM, "Required cache data cannot be located for a disk unit"},
    {0x07278D00, 0x0000, 0x9052, IPR_ERR_CLASS_PERM, "Cache data exists for a device that has been modified"},
    {0x07278E00, 0x0000, 0x9053, IPR_ERR_CLASS_PERM, "IOA resources not available due to previous problems"},
    {0x07278F00, 0x0000, 0x9054, IPR_ERR_CLASS_PERM, "IOA resources not available due to previous problems"},
    {0x07279100, 0x0000, 0x9092, IPR_ERR_CLASS_PERM, "Disk unit requires initialization before use"},
    {0x07279200, 0x0000, 0x9029, IPR_ERR_CLASS_PERM, "Incorrect hardware configuration change has been detected"},
    {0x07279500, 0x0000, 0x9009, IPR_ERR_CLASS_PERM, "Data Protect, device configuration sector is not convertible"},
    {0x07279600, 0x0000, 0x9060, IPR_ERR_CLASS_PERM, "One or more disk pairs are missing from an array"},
    {0x07279700, 0x0000, 0x9061, IPR_ERR_CLASS_PERM, "One or more disks are missing from an array"},
    {0x07279800, 0x0000, 0x9062, IPR_ERR_CLASS_PERM, "One or more disks are missing from an array"},
    {0x07279900, 0x0000, 0x9063, IPR_ERR_CLASS_PERM, "Maximum number of functional arrays has been exceeded"}
};

/*---------------------------------------------------------------------------
 * Purpose: Get pointer to a lun given a scsi_cmnd pointer
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL struct ipr_lun
*ipr_get_lun_scsi(ipr_host_config *ipr_cfg,
                     Scsi_Cmnd *p_scsi_cmd)
{
    u8 bus;
    struct ipr_lun *p_lun = NULL;

    bus = (u8)(p_scsi_cmd->channel + 1);

    if (bus <= IPR_MAX_NUM_BUSES)
    {
        p_lun = &ipr_cfg->shared.bus[bus].
            target[p_scsi_cmd->target].lun[p_scsi_cmd->lun];
    }

    return p_lun;
}

/*---------------------------------------------------------------------------
 * Purpose: Get pointer to a lun given a resource address
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL struct ipr_lun
*ipr_get_lun_res_addr(ipr_host_config *ipr_cfg,
                         struct ipr_res_addr res_addr)
{
    u8 bus;
    struct ipr_lun *p_lun = NULL;

    if (IPR_GET_PHYSICAL_LOCATOR(res_addr) != IPR_IOA_RESOURCE_ADDRESS)
    {
        bus = (u8)(res_addr.bus + 1);

        if (bus <= IPR_MAX_NUM_BUSES)
        {
            p_lun = &ipr_cfg->shared.bus[bus].
                target[res_addr.target].lun[res_addr.lun];
        }
    }

    return p_lun;
}

/*---------------------------------------------------------------------------
 * Purpose: Puts IOA in linked list of IOAs
 * Context: Task level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_add_ioa_to_tail(ipr_host_config *ipr_cfg)
{
    int i;
    ipr_host_config *p_cur_ioa, *p_prev_ioa;

    /* Iterate through the singly linked list to get to the tail */
    for (i = 0,
         p_cur_ioa = ipr_cfg_head,
         p_prev_ioa = NULL;
         i < ipr_num_ctlrs;
         p_prev_ioa = p_cur_ioa,
         p_cur_ioa = p_cur_ioa->p_next,
         i++)
    {
    }

    if (p_prev_ioa)
        p_prev_ioa->p_next = ipr_cfg;
    else
        ipr_cfg_head = ipr_cfg;

    ipr_cfg->p_next = NULL;

    ipr_num_ctlrs++;
}

/*---------------------------------------------------------------------------
 * Purpose: Removes IOA from linked list of IOAs
 * Context: Task level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_remove_ioa_from_list(ipr_host_config *ipr_cfg)
{
    int i;
    ipr_host_config *p_cur_ioa, *p_prev_ioa;

    for (i = 0,
         p_cur_ioa = ipr_cfg_head,
         p_prev_ioa = NULL;
         i < ipr_num_ctlrs;
         p_prev_ioa = p_cur_ioa,
         p_cur_ioa = p_cur_ioa->p_next,
         i++)
    {
        /* Is this the IOA we want to delete? */
        if (p_cur_ioa == ipr_cfg)
        {
            if (p_prev_ioa)
                p_prev_ioa->p_next = p_cur_ioa->p_next;
            else
                ipr_cfg_head = ipr_cfg_head->p_next;
            ipr_num_ctlrs--;
            return;
        }
    }

    panic(IPR_ERR"IOA not found in list: 0x%p"IPR_EOL, ipr_cfg);
}

/*---------------------------------------------------------------------------
 * Purpose: Puts a SIS Cmnd on the pending queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_put_sis_cmnd_to_pending(ipr_host_config *ipr_cfg,
                                                      struct ipr_cmnd* p_sis_cmnd)
{
    /* Put SIS Cmnd on the pending list */
    if (ipr_cfg->qPendingT != NULL)
    {
        ipr_cfg->qPendingT->p_next = p_sis_cmnd;
        p_sis_cmnd->p_prev = ipr_cfg->qPendingT;
        ipr_cfg->qPendingT = p_sis_cmnd;
    }
    else
    {
        ipr_cfg->qPendingT = ipr_cfg->qPendingH = p_sis_cmnd;
        p_sis_cmnd->p_prev = NULL;
    }

    p_sis_cmnd->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Initialize a SIS Cmnd block
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to command block
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_initialize_sis_cmnd(struct ipr_cmnd* p_sis_cmnd)
{
    u8 *p_sense_buffer;
    ipr_dma_addr sense_buffer_dma;

    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;
    sense_buffer_dma = p_sis_cmnd->ccb.sense_buffer_dma;

    memset(p_sis_cmnd, 0, sizeof(struct ipr_cmnd));
    memset(p_sense_buffer, 0, IPR_SENSE_BUFFERSIZE);
    p_sis_cmnd->ccb.sense_buffer = p_sense_buffer;
    p_sis_cmnd->ccb.sense_buffer_dma = sense_buffer_dma;
    p_sis_cmnd->ccb.sglist = p_sis_cmnd->sglist;

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Get a free SIS Cmnd block.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to command block
 * Notes: Cannot run out - will kernel panic if it does.
 *        Nobody should call this directly - use ipr_get_free_sis_cmnd
 *        and ipr_get_free_sis_cmnd_for_ioctl instead
 *---------------------------------------------------------------------------*/
static IPR_INL struct ipr_cmnd*
ipr_get_free_sis_cmnd_internal(ipr_host_config *ipr_cfg)
{
    struct ipr_cmnd* p_sis_cmnd;

    p_sis_cmnd = ipr_cfg->qFreeH;

    if (p_sis_cmnd == NULL)
        panic(IPR_ERR": Out of command blocks. ipr_cfg: 0x%p"IPR_EOL, ipr_cfg);

    ipr_cfg->qFreeH = ipr_cfg->qFreeH->p_next;

    if (ipr_cfg->qFreeH == NULL)
        ipr_cfg->qFreeT = NULL;
    else
        ipr_cfg->qFreeH->p_prev = NULL;

    ipr_initialize_sis_cmnd(p_sis_cmnd);

    return p_sis_cmnd;
}

/*---------------------------------------------------------------------------
 * Purpose: Get a free SIS Cmnd block.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to command block
 * Notes: Cannot run out - will kernel panic if it does.
 *---------------------------------------------------------------------------*/
static IPR_INL struct ipr_cmnd*
ipr_get_free_sis_cmnd(ipr_host_config *ipr_cfg)
{
    struct ipr_cmnd* p_sis_cmnd;

    p_sis_cmnd = ipr_get_free_sis_cmnd_internal(ipr_cfg);

    return p_sis_cmnd;
}

/*---------------------------------------------------------------------------
 * Purpose: Allocate a SIS Cmnd block for an IOCTL.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O Error
 *---------------------------------------------------------------------------*/
static int ipr_get_free_sis_cmnd_for_ioctl(ipr_host_config *ipr_cfg,
                                              struct ipr_cmnd **pp_sis_cmnd)
{
    struct ipr_cmnd *p_sis_cmnd = NULL;
    int rc = 0;

    /* Are we not accepting new requests? */
    if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
    {
        ipr_dbg_trace;
        return -EIO;
    }

    spin_unlock_irq(&io_request_lock);
    down(&ipr_cfg->ioctl_semaphore);
    spin_lock_irq(&io_request_lock);

    /* We can get a command block */
    if (ipr_cfg->flag & IPR_ALLOW_REQUESTS)
        p_sis_cmnd = ipr_get_free_sis_cmnd_internal(ipr_cfg);
    else
    {
        /* We should not be servicing external requests now */
        up(&ipr_cfg->ioctl_semaphore);
        ipr_dbg_trace;
        rc = -EIO;
    }

    *pp_sis_cmnd = p_sis_cmnd;

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Allocate a SIS Cmnd block for an internal IOCTL.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O Error
 *---------------------------------------------------------------------------*/
static int ipr_get_free_sis_cmnd_for_ioctl_internal(ipr_host_config *ipr_cfg,
                                                       struct ipr_cmnd **pp_sis_cmnd)
{
    struct ipr_cmnd *p_sis_cmnd = NULL;
    int rc = 0;

    /* Should we be talking to the adapter? */
    if ((ipr_cfg->shared.ioa_operational) == 0)
    {
        ipr_dbg_trace;
        return -EIO;
    }

    spin_unlock_irq(&io_request_lock);
    down(&ipr_cfg->ioctl_semaphore);
    spin_lock_irq(&io_request_lock);

    /* Should we be talking to the adapter? */
    if ((ipr_cfg->shared.ioa_operational) == 0)
    {
        ipr_dbg_trace;
        return -EIO;
    }

    p_sis_cmnd = ipr_get_free_sis_cmnd_internal(ipr_cfg);

    *pp_sis_cmnd = p_sis_cmnd;

    return rc;
}


/*---------------------------------------------------------------------------
 * Purpose: Put SIS Cmnd on the free queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_put_sis_cmnd_to_free(ipr_host_config *ipr_cfg,
                                                   struct ipr_cmnd* p_sis_cmnd)
{
    if (p_sis_cmnd->ccb.flags & IPR_UNMAP_ON_DONE)
    {
        if (p_sis_cmnd->ccb.scsi_use_sg > 0)
        {
            pci_unmap_sg(ipr_cfg->pdev, p_sis_cmnd->ccb.request_buffer,
                         p_sis_cmnd->ccb.scsi_use_sg,
                         scsi_to_pci_dma_dir(p_sis_cmnd->ccb.sc_data_direction));
        }
        else if (p_sis_cmnd->ccb.use_sg == 1)
        {
            pci_unmap_single(ipr_cfg->pdev, (dma_addr_t)p_sis_cmnd->ccb.buffer_dma,
                             p_sis_cmnd->ccb.bufflen,
                             scsi_to_pci_dma_dir(p_sis_cmnd->ccb.sc_data_direction));
        }
    }

    /* Put SIS Cmnd back on the free list */
    if(ipr_cfg->qFreeT != NULL)
    {
        ipr_cfg->qFreeT->p_next = p_sis_cmnd;
        p_sis_cmnd->p_prev = ipr_cfg->qFreeT;
        ipr_cfg->qFreeT = p_sis_cmnd;
    }
    else
    {
        ipr_cfg->qFreeH = ipr_cfg->qFreeT = p_sis_cmnd;
        p_sis_cmnd->p_prev = NULL;
    }

    p_sis_cmnd->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Put SIS IOCTL Cmnd on the free queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_put_ioctl_cmnd_to_free(ipr_host_config *ipr_cfg,
                                          struct ipr_cmnd* p_sis_cmnd)
{
    ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
    up(&ipr_cfg->ioctl_semaphore);
}

/*---------------------------------------------------------------------------
 * Purpose: Puts a SIS Cmnd on the error queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_put_sis_cmnd_to_error(ipr_host_config *ipr_cfg,
                                                    struct ipr_cmnd* p_sis_cmnd)
{
    /* Put SIS Cmnd on the error list */
    if (ipr_cfg->qErrorT != NULL)
    {
        ipr_cfg->qErrorT->p_next = p_sis_cmnd;
        p_sis_cmnd->p_prev = ipr_cfg->qErrorT;
        ipr_cfg->qErrorT = p_sis_cmnd;
    }
    else
    {
        ipr_cfg->qErrorT = ipr_cfg->qErrorH = p_sis_cmnd;
        p_sis_cmnd->p_prev = NULL;
    }

    p_sis_cmnd->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Removes a SIS Cmnd from the completed queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_remove_sis_cmnd_from_completed(ipr_host_config *ipr_cfg,
                                                             struct ipr_cmnd* p_sis_cmnd)
{
    if ((p_sis_cmnd == ipr_cfg->qCompletedH) &&
        (p_sis_cmnd == ipr_cfg->qCompletedT))
    {
        ipr_cfg->qCompletedH = ipr_cfg->qCompletedT = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qCompletedH)
    {
        ipr_cfg->qCompletedH = ipr_cfg->qCompletedH->p_next;
        ipr_cfg->qCompletedH->p_prev = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qCompletedT)
    {
        ipr_cfg->qCompletedT = ipr_cfg->qCompletedT->p_prev;
        ipr_cfg->qCompletedT->p_next = NULL;
    }
    else
    {
        p_sis_cmnd->p_next->p_prev = p_sis_cmnd->p_prev;
        p_sis_cmnd->p_prev->p_next = p_sis_cmnd->p_next;
    }

    p_sis_cmnd->p_next = NULL;
    p_sis_cmnd->p_prev = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Removes a SIS Cmnd from the error queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_remove_sis_cmnd_from_error(ipr_host_config *ipr_cfg,
                                                         struct ipr_cmnd* p_sis_cmnd)
{
    if ((p_sis_cmnd == ipr_cfg->qErrorH) &&
        (p_sis_cmnd == ipr_cfg->qErrorT))
    {
        ipr_cfg->qErrorH = ipr_cfg->qErrorT = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qErrorH)
    {
        ipr_cfg->qErrorH = ipr_cfg->qErrorH->p_next;
        ipr_cfg->qErrorH->p_prev = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qErrorT)
    {
        ipr_cfg->qErrorT = ipr_cfg->qErrorT->p_prev;
        ipr_cfg->qErrorT->p_next = NULL;
    }
    else
    {
        p_sis_cmnd->p_next->p_prev = p_sis_cmnd->p_prev;
        p_sis_cmnd->p_prev->p_next = p_sis_cmnd->p_next;
    }

    p_sis_cmnd->p_next = NULL;
    p_sis_cmnd->p_prev = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Removes a SIS Cmnd from the pending queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_remove_sis_cmnd_from_pending(ipr_host_config *ipr_cfg,
                                                           struct ipr_cmnd* p_sis_cmnd)
{
    if ((p_sis_cmnd == ipr_cfg->qPendingH) &&
        (p_sis_cmnd == ipr_cfg->qPendingT))
    {
        ipr_cfg->qPendingH = ipr_cfg->qPendingT = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qPendingH)
    {
        ipr_cfg->qPendingH = ipr_cfg->qPendingH->p_next;
        ipr_cfg->qPendingH->p_prev = NULL;
    }
    else if (p_sis_cmnd == ipr_cfg->qPendingT)
    {
        ipr_cfg->qPendingT = ipr_cfg->qPendingT->p_prev;
        ipr_cfg->qPendingT->p_next = NULL;
    }
    else
    {
        p_sis_cmnd->p_next->p_prev = p_sis_cmnd->p_prev;
        p_sis_cmnd->p_prev->p_next = p_sis_cmnd->p_next;
    }

    p_sis_cmnd->p_next = NULL;
    p_sis_cmnd->p_prev = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Removes a SIS Cmnd from the pending queue and puts it on the
 *          free queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_put_sis_cmnd_from_pending(ipr_host_config *ipr_cfg,
                                                        struct ipr_cmnd* p_sis_cmnd)
{
    ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);
    ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
}

/*---------------------------------------------------------------------------
 * Purpose: Puts a SIS Cmnd on the completed queue
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_put_sis_cmnd_to_completed(ipr_host_config *ipr_cfg,
                                                        struct ipr_cmnd* p_sis_cmnd)
{
    /* Put SIS Cmnd back on the free list */
    if (ipr_cfg->qCompletedT != NULL)
    {
        ipr_cfg->qCompletedT->p_next = p_sis_cmnd;
        p_sis_cmnd->p_prev = ipr_cfg->qCompletedT;
        ipr_cfg->qCompletedT = p_sis_cmnd;
    }
    else
    {
        ipr_cfg->qCompletedT = ipr_cfg->qCompletedH = p_sis_cmnd;
        p_sis_cmnd->p_prev = NULL;
    }

    p_sis_cmnd->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Wake up the kernel thread
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static IPR_INL void ipr_wake_task(ipr_host_config *ipr_cfg)
{
    ENTER;

    wake_up_interruptible(&ipr_cfg->wait_q);

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Build a scatter/gather list and map the buffer
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IBSIS_RC_SUCCESS    - Success
 *          IPR_RC_FAILED    - Failure
 *---------------------------------------------------------------------------*/
static int ipr_build_sglist(ipr_host_config *ipr_cfg,
                               struct ipr_cmnd* p_sis_cmnd)
{
    int i;
    struct ipr_sglist *p_sglist;
    struct ipr_sglist *p_tmp_sglist;
    struct scatterlist *p_tmp_scatterlist;
    u32 length;
    dma_addr_t dma_handle;

    p_sglist = p_sis_cmnd->ccb.sglist;
    length = p_sis_cmnd->ccb.bufflen;

    if (p_sis_cmnd->ccb.scsi_use_sg)
    {
        if (p_sis_cmnd->ccb.sc_data_direction == SCSI_DATA_READ)
            p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
        else if (p_sis_cmnd->ccb.sc_data_direction == SCSI_DATA_WRITE)
            p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
        else
            panic(IPR_ERR": use_sg was set on a command, but sc_data_direction was not. cmd 0x%02x"IPR_EOL,
                  p_sis_cmnd->ccb.cdb[0]);

        p_sis_cmnd->ccb.use_sg = pci_map_sg(ipr_cfg->pdev,
                                            p_sis_cmnd->ccb.request_buffer,
                                            p_sis_cmnd->ccb.scsi_use_sg,
                                            scsi_to_pci_dma_dir(p_sis_cmnd->ccb.sc_data_direction));

        for (i = 0, p_tmp_sglist = p_sglist,
             p_tmp_scatterlist = p_sis_cmnd->ccb.request_buffer;
             i < p_sis_cmnd->ccb.use_sg;
             i++, p_tmp_sglist++, p_tmp_scatterlist++)
        {
            p_tmp_sglist->address = sg_dma_address(p_tmp_scatterlist);
            p_tmp_sglist->length = sg_dma_len(p_tmp_scatterlist);
        }

        if (p_sis_cmnd->ccb.use_sg)
            p_sis_cmnd->ccb.flags |= IPR_UNMAP_ON_DONE;
        else
            ipr_log_err("pci_map_sg failed!"IPR_EOL);
    }
    else
    {
        if (length == 0)
        {
            /* No data to transfer */
            p_sis_cmnd->ccb.data_direction = IPR_DATA_NONE;
            return IPR_RC_SUCCESS;
        }

        /* Does the buffer need to be mapped? */
        if ((p_sis_cmnd->ccb.flags & IPR_BUFFER_MAPPED) == 0)
        {
            if (p_sis_cmnd->ccb.sc_data_direction == SCSI_DATA_READ)
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
            else if (p_sis_cmnd->ccb.sc_data_direction == SCSI_DATA_WRITE)
                p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
            else
            {
                /* No data to transfer */
                p_sis_cmnd->ccb.data_direction = IPR_DATA_NONE;
                return IPR_RC_SUCCESS;
            }

            dma_handle = pci_map_single(ipr_cfg->pdev, p_sis_cmnd->ccb.buffer,
                                        length, scsi_to_pci_dma_dir(p_sis_cmnd->ccb.sc_data_direction));

            if (dma_handle != NO_TCE)
            {
                p_sis_cmnd->ccb.use_sg = 1;
                p_sglist->address = dma_handle;
                p_sglist->length = length;
                p_sis_cmnd->ccb.buffer_dma = dma_handle;
                p_sis_cmnd->ccb.flags |= IPR_UNMAP_ON_DONE;
            }
            else
                ipr_log_err("pci_map_single failed!"IPR_EOL);
        }
        else
        {
            /* The buffer has already been mapped by the caller */
            p_sis_cmnd->ccb.use_sg = 1;
            p_sglist->address = p_sis_cmnd->ccb.buffer_dma;
            p_sglist->length = length;
        }
    }

    if (p_sis_cmnd->ccb.use_sg)
        return IPR_RC_SUCCESS;

    return IPR_RC_FAILED;
}

/*---------------------------------------------------------------------------
 * Purpose: Build a DASD/Tape/Optical command
 * Context: Task or interrupt level 
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to command block
 *          NULL - Command was not built
 *---------------------------------------------------------------------------*/
static IPR_INL struct ipr_cmnd* ipr_build_cmd (ipr_host_config * ipr_cfg, 
                                                        Scsi_Cmnd *p_scsi_cmd,
                                                        struct ipr_resource_entry *p_resource)
{
    struct ipr_cmnd* p_sis_cmnd;
    int rc = IPR_RC_SUCCESS;
    void (*timeout_func) (Scsi_Cmnd *);
    int timeout;

    p_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

    memcpy(p_sis_cmnd->ccb.cdb, p_scsi_cmd->cmnd, p_scsi_cmd->cmd_len);
    p_sis_cmnd->p_scsi_cmd = p_scsi_cmd;
    p_sis_cmnd->ccb.buffer = p_scsi_cmd->buffer;
    p_sis_cmnd->ccb.request_buffer = p_scsi_cmd->request_buffer;
    p_sis_cmnd->ccb.bufflen = p_scsi_cmd->request_bufflen;
    p_sis_cmnd->ccb.cmd_len = p_scsi_cmd->cmd_len;
    p_sis_cmnd->ccb.p_resource = p_resource;
    p_sis_cmnd->ccb.underflow = p_scsi_cmd->underflow;
    p_sis_cmnd->ccb.scsi_use_sg = p_scsi_cmd->use_sg;
    p_sis_cmnd->ccb.sc_data_direction = p_scsi_cmd->sc_data_direction;

    /* Double the timeout value to use as we will use the adapter
     as the primary timing mechanism */
    timeout_func = (void (*)(Scsi_Cmnd *))p_scsi_cmd->eh_timeout.function;
    timeout = p_scsi_cmd->timeout_per_command;

    if (1 == IPR_TIMEOUT_MULTIPLIER)
        p_sis_cmnd->ccb.timeout = IPR_MAX_SIS_TIMEOUT;
    else
        p_sis_cmnd->ccb.timeout = timeout/HZ;

    scsi_add_timer(p_scsi_cmd, timeout * IPR_TIMEOUT_MULTIPLIER,
                   timeout_func);

    if (!p_resource->is_af)
        p_sis_cmnd->ccb.flags |= IPR_GPDD_CMD;

    if (p_scsi_cmd->cmnd[0] == INQUIRY)
    {
        /* Redefine xfer length so IOA doesn't complain of underlength error */
        p_sis_cmnd->ccb.bufflen = p_scsi_cmd->cmnd[4];
    }

    rc = ipr_build_sglist(ipr_cfg, p_sis_cmnd);

    if (rc != IPR_RC_SUCCESS)
    {
        ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);
        ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
        p_sis_cmnd = NULL;
    }

    return p_sis_cmnd;
}

/*---------------------------------------------------------------------------
 * Purpose: Run through completed queue and push op done functions
 * Context: Interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_ops_done (ipr_host_config *ipr_cfg)
{
    struct ipr_cmnd *p_sis_cmnd;
    Scsi_Cmnd *p_scsi_cmd;
    u32 cc;
    struct ipr_hostrcb *p_hostrcb, *p_tmp_hostrcb;
    u8 *p_sense_buffer;
    int i;
    int selection_timeout;

    while ((p_sis_cmnd = ipr_cfg->qCompletedH) != NULL)
    {
        ipr_remove_sis_cmnd_from_completed(ipr_cfg, p_sis_cmnd);
        cc = p_sis_cmnd->ccb.completion;
        p_scsi_cmd = p_sis_cmnd->p_scsi_cmd;
        p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

        /* Did this command originate from the mid-layer SCSI code? */
        if (p_scsi_cmd != NULL)
        {
            p_scsi_cmd->resid = p_sis_cmnd->ccb.residual;

            if (cc == IPR_RC_FAILED)
            {
                if (ipr_sense_valid(p_sense_buffer[0]) &&
                    (ipr_sense_key(p_sense_buffer) == HARDWARE_ERROR) &&
                    (ipr_sense_code(p_sense_buffer) == 0x05) &&
                    (ipr_sense_qual(p_sense_buffer) == 0x00))
                {
                    selection_timeout = 1;
                }
                else
                {
                    selection_timeout = 0;
                }

                if ((p_scsi_cmd->cmnd[0] == INQUIRY) &&
                    ((p_scsi_cmd->cmnd[1] & 0x01) == 0) &&
                    !selection_timeout)
                {
                    /* Do we need to do any ERP? */
                    /* If we got a synchronization required state, and we are
                     not aborting this command, enter erp thread */
                    if (ipr_sense_valid(p_sense_buffer[0]) &&
                        (ipr_sense_key(p_sense_buffer) == NOT_READY) &&
                        (ipr_sense_code(p_sense_buffer) == IPR_SYNC_REQUIRED) &&
                        ((p_sis_cmnd->ccb.flags & IPR_ABORTING) == 0))
                    {
                        ipr_put_sis_cmnd_to_error(ipr_cfg, p_sis_cmnd);

                        ipr_wake_task(ipr_cfg);
                        continue;
                    }

                    /* Return buffered standard inquiry data */
                    /* This MUST be done since the mid-layer does not
                     retry inquiries when polling for devices. */
                    memcpy(p_scsi_cmd->buffer,
                                       &p_sis_cmnd->ccb.p_resource->std_inq_data,
                                       sizeof(p_sis_cmnd->ccb.p_resource->std_inq_data));
                }
                else
                {
                    /* Is this a GPDD op? */
                    if (p_sis_cmnd->ccb.flags & IPR_GPDD_CMD)
                    {
                        /* Do we have a valid sense buffer? */
                        if (ipr_sense_valid(p_sense_buffer[0]))
                        {
                            if ((ipr_sense_key(p_sense_buffer) != ILLEGAL_REQUEST) ||
                                (ipr_sense_code(p_sense_buffer) != IPR_INVALID_RESH))
                            {
                                /* Copy over sense data */
                                memcpy(p_scsi_cmd->sense_buffer, p_sense_buffer,
                                                   IPR_SENSE_BUFFER_COPY_SIZE);
                            }

                            if (ipr_debug)
                            {
                                /* Print out the sense data */
                                print_sense(IPR_NAME, p_scsi_cmd);

                                /* If this is an illegal request, print out the SCSI command for debug */
                                if (ipr_sense_key(p_sense_buffer) == ILLEGAL_REQUEST)
                                    print_Scsi_Cmnd(p_scsi_cmd);
                            }
                        }

                        if (!((p_scsi_cmd->cmnd[0] == INQUIRY) &&
                            ((p_scsi_cmd->cmnd[1] & 0x01) == 0)) &&
                            selection_timeout)
                        {
                            p_scsi_cmd->result = (DID_TIME_OUT << 16);
                        }
                        /* Do we need to do any ERP? */
                        /* If we got a check condition or are in a synchronization
                         required state, and we are not aborting this command,
                         enter erp thread */
                        else if (((status_byte(p_sis_cmnd->ccb.status) == CHECK_CONDITION) ||
                             (ipr_sense_valid(p_sense_buffer[0]) &&
                              (ipr_sense_key(p_sense_buffer) == NOT_READY) &&
                              (ipr_sense_code(p_sense_buffer) == IPR_SYNC_REQUIRED))) &&
                            ((p_sis_cmnd->ccb.flags & IPR_ABORTING) == 0))
                        {
                            ipr_put_sis_cmnd_to_error(ipr_cfg, p_sis_cmnd);

                            ipr_wake_task(ipr_cfg);
                            continue;
                        }
                        else if (p_sis_cmnd->ccb.status &&
                                 (p_sis_cmnd->ccb.flags & IPR_ABORTING) == 0)
                        {
                            p_scsi_cmd->result |= p_sis_cmnd->ccb.status;
                        }
                        else if (ipr_sense_valid(p_sense_buffer[0]) &&
                                 (ipr_sense_key(p_sense_buffer) == ILLEGAL_REQUEST) &&
                                 (ipr_sense_code(p_sense_buffer) == IPR_INVALID_RESH))
                        {
                            p_scsi_cmd->result |= (DID_NO_CONNECT << 16);
                        }
                        else
                        {
                            p_scsi_cmd->result |= (DID_ERROR << 16);
                        }
                    }
                    else /* Not a GPDD op */
                    {
                        /* Do we have a valid sense buffer and is this an illegal request,
                         a not ready or a medium error sense key? */
                        if (ipr_sense_valid(p_sense_buffer[0]) &&
                            ((ipr_sense_key(p_sense_buffer) == ILLEGAL_REQUEST) ||
                             (ipr_sense_key(p_sense_buffer) == NOT_READY) ||
                             (ipr_sense_key(p_sense_buffer) == MEDIUM_ERROR)))
                        {
                            if ((ipr_sense_key(p_sense_buffer) != ILLEGAL_REQUEST) ||
                                (ipr_sense_code(p_sense_buffer) != IPR_INVALID_RESH))
                            {
                                /* Copy over sense data */
                                memcpy(p_scsi_cmd->sense_buffer, p_sense_buffer,
                                                   IPR_SENSE_BUFFER_COPY_SIZE);
                            }

                            if (ipr_debug)
                            {
                                /* Print out the sense data */
                                print_sense(IPR_NAME, p_scsi_cmd);

                                /* If the sense data is valid and this is an illegal request,
                                 print out the SCSI command for debug */
                                if (ipr_sense_key(p_sense_buffer) == ILLEGAL_REQUEST)
                                    print_Scsi_Cmnd(p_scsi_cmd);
                            }
                        }

                        if (ipr_sense_valid(p_sense_buffer[0]) &&
                            ((ipr_sense_key(p_sense_buffer) == MEDIUM_ERROR) ||
                             (ipr_sense_code(p_sense_buffer) == 0x11) ||
                             (ipr_sense_qual(p_sense_buffer) == 0x0C)))
                        {
                            /* Prevent the midlayer from issuing retries */
                            p_scsi_cmd->result |= (DID_PASSTHROUGH << 16);
                        }
                        else if (ipr_sense_valid(p_sense_buffer[0]) &&
                                 (ipr_sense_key(p_sense_buffer) == ILLEGAL_REQUEST) &&
                                 (ipr_sense_code(p_sense_buffer) == IPR_INVALID_RESH))
                        {
                            p_scsi_cmd->result |= (DID_NO_CONNECT << 16);
                        }
                        else
                        {
                            /* Set DID_ERROR to force the mid-layer to do a retry */
                            p_scsi_cmd->result |= (DID_ERROR << 16);
                        }
                    }
                }
            }

            /* Are we currently trying to abort this command? */
            if (p_sis_cmnd->ccb.flags & IPR_ABORTING)
            {
                /* Is there another request tied to this op? */
                if (p_sis_cmnd->p_cancel_op)
                {
                    /* Has the cancel request completed? */
                    if (p_sis_cmnd->p_cancel_op->ccb.flags & IPR_FINISHED)
                    {
                        /* Wake the thread sleeping on the cancel */
                        wake_up(&p_sis_cmnd->p_cancel_op->wait_q);
                    }
                    else /* Need to wait for the cancel request to complete */
                    {
                        /* NULL out the Cancel request's pointer to us */
                        p_sis_cmnd->p_cancel_op->p_cancel_op = NULL;
                    }
                }
            }

            ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);

            /* If we are failing this op back due to a host reset, we can't push the scsi_done
             function yet. The caller will queue up the scsi_cmd structures and send them
             back to the host once we are able to accept new commands */
            if (cc != IPR_RC_DID_RESET)
                p_scsi_cmd->scsi_done (p_scsi_cmd);
        }
        /* This is a 'blocking command', could be a cancel request */
        else if (p_sis_cmnd->ccb.flags & IPR_BLOCKING_COMMAND)
        {
            /* We don't want to touch the command if it timed out */
            if ((p_sis_cmnd->ccb.flags & IPR_TIMED_OUT) == 0)
            {
                /* Mark the command as finished */
                p_sis_cmnd->ccb.flags |= IPR_FINISHED;

                /* If this is a cancel request, the p_cancel_op pointer would
                 have been setup to point to the op being cancelled, otherwise
                 it would be NULL. If that op had completed, it would have
                 NULLed out this pointer. In either case, if the pointer is NULL
                 we don't need to wait for anything and can wake the task
                 sleeping on our completion */
                if (p_sis_cmnd->p_cancel_op == NULL)
                    wake_up(&p_sis_cmnd->wait_q);
            }
            else
            {
                /* command timed out, free command block resource */
                ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
            }
        }
        /* This is an 'ERP' command for a GPDD device */
        else if (p_sis_cmnd->ccb.flags & IPR_ERP_CMD)
        {
            /* Not aborting this command. Put it back on the error queue
             and wake up our kernel thread to continue processing it */
            ipr_put_sis_cmnd_to_error(ipr_cfg, p_sis_cmnd);
            ipr_wake_task(ipr_cfg);
        }
        else if (p_sis_cmnd->ccb.flags & IPR_INTERNAL_REQ)
        {
            /* It is the caller's responsibility to free this command block */
            del_timer(&p_sis_cmnd->timer);
            p_sis_cmnd->done(&ipr_cfg->shared, &p_sis_cmnd->ccb);
        }
        else 
        {/* HCAM */

            p_hostrcb = p_sis_cmnd->ccb.buffer;

            ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);

            if ((cc == IPR_RC_DID_RESET) || ipr_cfg->shared.nr_ioa_microcode)
            {
                /* The HCAM failed due to the IOA being braindead, or we are currently
                 failing back all host ops prior to a reset. In either case we just want
                 to stop sending HCAMs */
            }
            else if (cc != IPR_RC_SUCCESS)
            {
                /* The HCAM failed for some other reason, just log an error and resend
                 the HCAM */

                ipr_log_err("Host RCB failed with SK: 0x%X ASC: 0x%X ASCQ: 0x%X"IPR_EOL,
                               p_sense_buffer[2] & 0xf, p_sense_buffer[12],
                               p_sense_buffer[13]);

                if (p_hostrcb->op_code == IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE)
                {
                    /* Ship it back to the IOA to be re-used */
                    ipr_send_hcam(&ipr_cfg->shared,
                                     IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, p_hostrcb);
                }
                else
                {
                    /* Ship it back to the IOA to be re-used */
                    ipr_send_hcam(&ipr_cfg->shared,
                                     IPR_HCAM_CDB_OP_CODE_LOG_DATA, p_hostrcb);
                }
            }
            else
            {
                /* Put the hostrcb at the end of the done list */
                for (i = 0, p_tmp_hostrcb = ipr_cfg->done_hostrcb[0];
                     (p_tmp_hostrcb != NULL) && (i < IPR_NUM_HCAMS);
                     p_tmp_hostrcb = ipr_cfg->done_hostrcb[++i])
                {
                }

                ipr_cfg->done_hostrcb[i] = p_hostrcb;

                /* Process the HCAM and ship it back to the IOA to be re-used */
                ipr_wake_task(ipr_cfg);
            }
        }
    }
}


/*---------------------------------------------------------------------------
 * Purpose: Get information about the card/driver
 * Context: Task level
 * Lock State: No lock held upon entry
 * Returns: pointer to data buffer
 *---------------------------------------------------------------------------*/
const char *ipr_info(struct Scsi_Host *p_scsi_host)
{
    static char buffer[512];
    ipr_host_config *ipr_cfg;
    unsigned long io_flags = 0;

    spin_lock_irqsave(&io_request_lock, io_flags);

    ipr_cfg = (ipr_host_config *) p_scsi_host->hostdata;

    sprintf (buffer, "IBM %X Storage Adapter: %s",
             ipr_cfg->shared.ccin, ipr_cfg->shared.ioa_host_str);

    spin_unlock_irqrestore(&io_request_lock, io_flags);

    return buffer;
}

/*---------------------------------------------------------------------------
 * Purpose: Detects all SIS IOAs on this system
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Number of IOAs found
 *---------------------------------------------------------------------------*/
int ipr_detect(Scsi_Host_Template *p_host_tmpl)
{
    int count = 0;
    int i;

    p_host_tmpl->proc_name = IPR_NAME;

    ipr_log_info("Driver Version: "IPR_FULL_VERSION ""IPR_EOL);

    init_waitqueue_head(&ipr_sdt_wait_q);

    for (i = 0; i < (sizeof(ipr_ioa_cfg)/sizeof(struct ipr_ioa_cfg_t)); i++)
    {
        ipr_find_ioa(p_host_tmpl, ipr_ioa_cfg[i].vendor_id,
                        ipr_ioa_cfg[i].device_id);
    }

    count = ipr_find_ioa_part2();

    if (ipr_cfg_head)
        ipr_log_info(IPR_NAME"_cfg_head: 0x%p"IPR_EOL, ipr_cfg_head);

    ipr_init_finished = 1;

    return count;
}

/*---------------------------------------------------------------------------
 * Purpose: Walks the PCI bus looking for a matching IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_find_ioa (Scsi_Host_Template * p_host_tmpl,
                             u16 pci_vendor, u16 pci_dev)
{
    ipr_host_config *ipr_cfg;
    struct Scsi_Host *host = NULL;
    unsigned int sis_irq;
    unsigned long sis_regs, sis_regs_pci;
    u32 bar0, rc = PCIBIOS_SUCCESSFUL;
    u16 subsystem_id;
    int i, j, pcix_command_reg;
    DECLARE_COMPLETION(completion);
    struct ipr_location_data *p_location_data = NULL;
    u8 *p_sense_buffer;
    ipr_dma_addr dma_addr;
    struct ipr_hostrcb *p_hostrcb;
    struct ipr_element_desc_page *p_ses_desc_page;
    struct pci_dev *pdev = NULL;
    char ioa_host_str[IPR_MAX_LOCATION_LEN];

    while ((pdev = pci_find_device(pci_vendor, pci_dev, pdev)))
    {
        p_location_data = ipr_get_ioa_location_data(pdev);

        /* Lets release the spinlock since we could go to sleep */
        spin_unlock_irq(&io_request_lock);

        if (p_location_data == NULL)
        {
            /* Grab the spinlock again */
            spin_lock_irq(&io_request_lock);

            ipr_log_err("Call to ipr_get_ioa_location_data failed"IPR_EOL);
            continue;
        }

        if (pci_enable_device(pdev))
        {
            /* Grab the spinlock again */
            spin_lock_irq(&io_request_lock);

            ipr_beg_err(KERN_ERR);
            ipr_log_err("Cannot enable ipr device"IPR_EOL);
            ipr_log_ioa_physical_location(p_location_data, KERN_ERR);
            ipr_end_err(KERN_ERR);
            ipr_kfree(p_location_data, sizeof(struct ipr_location_data));
            continue;
        }

        /* Get physical location string */
        ipr_ioa_loc_str(p_location_data, ioa_host_str);

        /* Get the IRQ */
        sis_irq  = pdev->irq;

        ipr_log_info("IOA found at %s, IRQ: %d"IPR_EOL,
                        ioa_host_str, sis_irq);

        /* Initialize SCSI Host structure */
        host = scsi_register(p_host_tmpl, sizeof (ipr_host_config));

        if (host == NULL)
        {
            spin_lock_irq(&io_request_lock);
            ipr_log_err("call to scsi_register failed! "IPR_EOL);
            ipr_kfree(p_location_data, sizeof(struct ipr_location_data));
            continue;
        }

        /* xx - This is only a temporary hack until the 2.5 work is done */
        scsi_assign_lock(host, &io_request_lock);

        ipr_cfg = (ipr_host_config *) host->hostdata;

        memset(ipr_cfg, 0, sizeof (ipr_host_config));

        /* Copy resource info into structure */
        ipr_cfg->p_next = NULL;
        ipr_cfg->shared.vendor_id = pci_vendor;
        ipr_cfg->shared.device_id = pci_dev;
        ipr_cfg->host = host;
        ipr_cfg->pdev = pdev;
        ipr_cfg->shared.hdw_bar_addr_pci[0] = pci_resource_start (pdev, 0);
        ipr_cfg->shared.hdw_bar_addr_pci[1] = pci_resource_start (pdev, 1);
        ipr_cfg->shared.hdw_bar_addr_pci[2] = pci_resource_start (pdev, 2);
        ipr_cfg->shared.hdw_bar_addr_pci[3] = pci_resource_start (pdev, 3);
        ipr_cfg->shared.p_location = p_location_data;
        ipr_cfg->shared.debug_level = verbose;
        ipr_cfg->shared.trace = trace;
        ipr_cfg->shared.host_no = host->host_no;
        sprintf(ipr_cfg->shared.eye_catcher, IPR_SHARED_LABEL);
        sprintf(ipr_cfg->shared.resource_table_label, IPR_RES_TABLE_LABEL);
        sprintf(ipr_cfg->shared.ses_table_start, IPR_DATA_SES_DATA_START);
        strcpy(ipr_cfg->shared.ioa_host_str, ioa_host_str);
        sprintf(ipr_cfg->shared.end_eye_catcher, IPR_END_SHARED_LABEL);
        sprintf(ipr_cfg->ipr_free_label, IPR_FREEQ_LABEL);
        sprintf(ipr_cfg->ipr_pending_label, IPR_PENDQ_LABEL);
        sprintf(ipr_cfg->ipr_comp_label, IPR_COMPQ_LABEL);
        sprintf(ipr_cfg->ipr_err_label, IPR_ERRQ_LABEL);
        sprintf(ipr_cfg->ipr_hcam_label, IPR_HCAM_LABEL);
        sprintf(ipr_cfg->ipr_sis_cmd_label, IPR_SIS_CMD_LABEL);
        ipr_cfg->shared.p_end = ipr_cfg->shared.end_eye_catcher;

        ipr_cfg->host->irq = sis_irq;
        ipr_cfg->host->io_port = 0;
        ipr_cfg->host->n_io_port = 0;
        ipr_cfg->host->max_id = IPR_MAX_NUM_TARGETS_PER_BUS;
        ipr_cfg->host->max_lun = IPR_MAX_NUM_LUNS_PER_TARGET;
        ipr_cfg->host->max_channel = IPR_MAX_BUS_TO_SCAN;

        /* Initialize our semaphore for IOCTLs */
        sema_init(&ipr_cfg->ioctl_semaphore, IPR_NUM_IOCTL_CMD_BLKS);

        /* Verify we should be talking to the adapter - we could have been loaded as a module,
         unloaded, and on the unload the restore of our PCI config registers failed, in which case
         we cannot talk to this adapter again */
        if (pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0) != PCIBIOS_SUCCESSFUL)
        {
            ipr_ipl_err(ipr_cfg, "Read of config space failed."IPR_EOL);
            goto cleanup_bar0;
        }

        if ((bar0 & IPR_BAR0_ADDRESS_BITS) == 0)
        {
            ipr_ipl_err(ipr_cfg, "Base address registers lost."IPR_EOL);
            goto cleanup_bar0;
        }

        if (pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &subsystem_id) != PCIBIOS_SUCCESSFUL)
        {
            ipr_ipl_err(ipr_cfg, "Read of config space failed."IPR_EOL);
            goto cleanup_bar0;
        }

        ipr_cfg->shared.subsystem_id = subsystem_id;

        /* Fill in a 'temporary ccin'. This will be used if the IOA unit checks
         before we get its inquiry data */
        for (i = 0; i < (sizeof(ipr_pci_dev_table)/sizeof(struct ipr_pci_dev_table_t)); i++)
        {
            if ((ipr_pci_dev_table[i].vendor_id == pci_vendor) &&
                (ipr_pci_dev_table[i].device_id == pci_dev) &&
                (ipr_pci_dev_table[i].subsystem_id == subsystem_id))
            {
                ipr_cfg->shared.ccin = ipr_pci_dev_table[i].ccin;
                strcpy(ipr_cfg->shared.ccin_str, ipr_pci_dev_table[i].ccin_str);
                ipr_cfg->shared.num_physical_buses = ipr_pci_dev_table[i].num_physical_buses;
                ipr_cfg->host->max_cmd_len = ipr_pci_dev_table[i].max_cmd_len;
                ipr_cfg->shared.use_immed_format = ipr_pci_dev_table[i].format_immed;
                break;
            }
        }

        if (ipr_cfg->shared.ccin == 0)
            goto cleanup_bar0;

        ipr_cfg->p_ioa_cfg = NULL;

        for (i = 0; i < (sizeof(ipr_ioa_cfg)/sizeof(struct ipr_ioa_cfg_t)); i++)
        {
            if ((ipr_ioa_cfg[i].vendor_id == pci_vendor) &&
                (ipr_ioa_cfg[i].device_id == pci_dev))
            {
                ipr_cfg->p_ioa_cfg = &ipr_ioa_cfg[i];
                ipr_cfg->shared.set_mode_page_20 = ipr_ioa_cfg[i].set_mode_page_20;
            }
        }

        if (ipr_cfg->p_ioa_cfg == NULL)
        {
            ipr_ipl_err(ipr_cfg, "Cannot determine IOA config: 0x%04X 0x%04X"IPR_EOL,
                           pci_vendor, pci_dev);
            goto cleanup_bar0;
        }

        /* Save away the start of our hardware regs now that we know where they are */
        sis_regs_pci = pci_resource_start (pdev, ipr_cfg->p_ioa_cfg->bar_index);

        /* Request and map PCI memory ranges */
        if (!request_mem_region(sis_regs_pci,
                                pci_resource_len(pdev, ipr_cfg->p_ioa_cfg->bar_index),
                                "ipr"))
        {
            ipr_ipl_err(ipr_cfg, "Couldn't register memory range of registers!"IPR_EOL);
            goto cleanup_bar0;
        }

        sis_regs = (unsigned long)ioremap(sis_regs_pci,
                                          pci_resource_len(pdev, ipr_cfg->p_ioa_cfg->bar_index));

        ipr_cfg->shared.hdw_dma_regs = sis_regs;
        ipr_cfg->shared.hdw_dma_regs_pci = sis_regs_pci;
        ipr_cfg->shared.hdw_bar_addr[ipr_cfg->p_ioa_cfg->bar_index] = sis_regs;

        ipr_cfg->host->unique_id = ipr_get_unique_id(p_location_data);

        ipr_cfg->shared.ioa_mailbox = ipr_cfg->p_ioa_cfg->mailbox + sis_regs;

        if (ipr_cfg->p_ioa_cfg->sdt_reg_sel_size == IPR_SDT_REG_SEL_SIZE_1BYTE)
        {
            if (!request_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[2],
                                    pci_resource_len(pdev, 2), "ipr"))
            {
                ipr_ipl_err(ipr_cfg, "Couldn't register BAR2 memory range!"IPR_EOL);
                goto cleanup_bar2;
            }

            ipr_cfg->shared.hdw_bar_addr[2] =
                (unsigned long)ioremap(ipr_cfg->shared.hdw_bar_addr_pci[2],
                                       pci_resource_len(pdev, 2));

            if (!request_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[3],
                                    pci_resource_len(pdev, 3), "ipr"))
            {
                ipr_ipl_err(ipr_cfg, "Couldn't register BAR3 memory range!"IPR_EOL);
                goto cleanup_bar3;
            }

            ipr_cfg->shared.hdw_bar_addr[3] =
                (unsigned long)ioremap(ipr_cfg->shared.hdw_bar_addr_pci[3],
                                       pci_resource_len(pdev, 3));
        }

        /* Set PCI master mode */
        pci_set_master (pdev);

        /* Save away PCI config space for use following IOA reset */
        for (i = 0; (i < IPR_CONFIG_SAVE_WORDS) && (rc == PCIBIOS_SUCCESSFUL); i++)
            rc = pci_read_config_dword(ipr_cfg->pdev, i*4, &ipr_cfg->pci_cfg_buf[i]);

        if (rc != PCIBIOS_SUCCESSFUL)
        {
            ipr_ipl_err(ipr_cfg, "Read of config space failed."IPR_EOL);
            goto cleanup_nolog;
        }


        if (pci_read_config_byte(ipr_cfg->pdev,
                                 PCI_REVISION_ID,
                                 &ipr_cfg->shared.chip_rev_id) != PCIBIOS_SUCCESSFUL)
        {
            ipr_ipl_err(ipr_cfg, "Read of config space failed."IPR_EOL);
            goto cleanup_nolog;
        }

        /* See if we can find the PCI-X command register */
        pcix_command_reg = pci_find_capability(ipr_cfg->pdev, IPR_PCIX_COMMAND_REG_ID);

        if (pcix_command_reg)
        {
            /* Need to save the PCI-X Command register. */
            if (pci_read_config_word(ipr_cfg->pdev, pcix_command_reg,
                                     &ipr_cfg->saved_pcix_command_reg) != PCIBIOS_SUCCESSFUL)
            {
                ipr_ipl_err(ipr_cfg, "Read of config space failed."IPR_EOL);
                goto cleanup_nolog;
            }

            /* Data parity error recovery */
            ipr_cfg->saved_pcix_command_reg |= IPR_PCIX_CMD_DATA_PARITY_RECOVER;

            /* Enable relaxed ordering */
            ipr_cfg->saved_pcix_command_reg |= IPR_PCIX_CMD_RELAXED_ORDERING;
        }

        /* Allocate resource table */
        ipr_cfg->shared.resource_entry_list = ipr_kcalloc(sizeof(struct ipr_resource_dll) *
                                                                IPR_MAX_PHYSICAL_DEVS,
                                                                IPR_ALLOC_CAN_SLEEP);

        if (ipr_cfg->shared.resource_entry_list == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        ipr_cfg->shared.p_vpd_cbs = ipr_dma_calloc(&ipr_cfg->shared,
                                                         sizeof(struct ipr_vpd_cbs),
                                                         &ipr_cfg->shared.ioa_vpd_dma,
                                                         IPR_ALLOC_CAN_SLEEP);

        if (ipr_cfg->shared.p_vpd_cbs == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        ipr_cfg->shared.p_ioa_vpd = &ipr_cfg->shared.p_vpd_cbs->ioa_vpd;
        ipr_cfg->shared.p_cfc_vpd = &ipr_cfg->shared.p_vpd_cbs->cfc_vpd;
        ipr_cfg->shared.p_ucode_vpd = &ipr_cfg->shared.p_vpd_cbs->page3_data;
        ipr_cfg->shared.p_page0_vpd = &ipr_cfg->shared.p_vpd_cbs->page0_data;
        ipr_cfg->shared.p_dram_vpd = &ipr_cfg->shared.p_vpd_cbs->dram_vpd;

        ipr_cfg->shared.cfc_vpd_dma = ipr_cfg->shared.ioa_vpd_dma +
            sizeof(struct ipr_ioa_vpd);
        ipr_cfg->shared.ucode_vpd_dma = ipr_cfg->shared.cfc_vpd_dma +
            sizeof(struct ipr_cfc_vpd);
        ipr_cfg->shared.page0_vpd_dma = ipr_cfg->shared.ucode_vpd_dma +
            sizeof(struct ipr_inquiry_page3);
        ipr_cfg->shared.dram_vpd_dma = ipr_cfg->shared.page0_vpd_dma +
            sizeof(struct ipr_inquiry_page0);

        /* Allocate mode page 28 reserve */
        ipr_cfg->shared.p_page_28 = ipr_kcalloc(sizeof(struct ipr_page_28_data),
                                                      IPR_ALLOC_CAN_SLEEP);

        if (ipr_cfg->shared.p_page_28 == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        /* Allocate command blocks */
        for (i = 0; i < IPR_NUM_CMD_BLKS; i++)
        {
            ipr_cfg->sis_cmnd_list[i] = ipr_kcalloc(sizeof(struct ipr_cmnd),
                                                          IPR_ALLOC_CAN_SLEEP);
            if (ipr_cfg->sis_cmnd_list[i] == NULL)
            {
                ipr_trace;
                goto cleanup;
            }
        }

        /* Allocate auto-sense buffers */
        p_sense_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                           IPR_SENSE_BUFFERSIZE * IPR_NUM_CMD_BLKS,
                                           &dma_addr, IPR_ALLOC_CAN_SLEEP);

        if (p_sense_buffer == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        /* Put the command blocks on the free list */
        for (i = 0;
             i < IPR_NUM_CMD_BLKS;
             i++, dma_addr += IPR_SENSE_BUFFERSIZE, p_sense_buffer += IPR_SENSE_BUFFERSIZE)
        {
            ipr_cfg->sis_cmnd_list[i]->ccb.sense_buffer = p_sense_buffer;
            ipr_cfg->sis_cmnd_list[i]->ccb.sense_buffer_dma = dma_addr;
            ipr_cfg->sis_cmnd_list[i]->p_scsi_cmd = NULL;
            ipr_cfg->sis_cmnd_list[i]->ccb.flags = 0;
            ipr_put_sis_cmnd_to_free(ipr_cfg, ipr_cfg->sis_cmnd_list[i]);
        }

        /* Allocate our HCAMs */
        p_hostrcb = ipr_dma_calloc(&ipr_cfg->shared,
                                      sizeof(struct ipr_hostrcb) * IPR_NUM_HCAMS,
                                      &dma_addr, IPR_ALLOC_CAN_SLEEP);

        if (p_hostrcb == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        for (i = 0;
             i < IPR_NUM_HCAMS;
             i++, p_hostrcb++, dma_addr += sizeof(struct ipr_hostrcb))
        {
            ipr_cfg->hostrcb[i] = p_hostrcb;
            ipr_cfg->hostrcb_dma[i] = dma_addr;
        }

        /* Allocate DMA buffers for SES data */
        p_ses_desc_page = ipr_dma_calloc(&ipr_cfg->shared,
                                            sizeof(struct ipr_element_desc_page) *
                                            IPR_MAX_NUM_BUSES,
                                            &dma_addr, IPR_ALLOC_CAN_SLEEP);

        if (p_ses_desc_page == NULL)
        {
            ipr_trace;
            goto cleanup;
        }

        for (i = 0;
             i < IPR_MAX_NUM_BUSES;
             i++, p_ses_desc_page++, dma_addr += sizeof(struct ipr_element_desc_page))
        {
            ipr_cfg->shared.p_ses_data[i] = p_ses_desc_page;
            ipr_cfg->shared.ses_data_dma[i] = dma_addr;
        }

        /* Start a task level ERP thread for ourselves */
        ipr_cfg->completion = &completion;

        ipr_cfg->task_pid = kernel_thread(ipr_task_thread, ipr_cfg, 0);

        if (ipr_cfg->task_pid < 0)
        {
            ipr_log_err("Failed to spawn kernel thread."IPR_EOL);
            goto cleanup_nolog;
        }

        /* Wait for the thread to initialize itself */
        wait_for_completion(&completion);

        ipr_cfg->completion = NULL;

        /* Tell the binary to allocate its memory */
        rc = ipr_alloc_mem(&ipr_cfg->shared);

        /* Get the spinlock since the functions below expect us to have it */
        spin_lock_irq(&io_request_lock);

        if (rc)
        {
            ipr_kill_kernel_thread(ipr_cfg);
            spin_unlock_irq(&io_request_lock);
            if (rc == IPR_RC_NOMEM)
                goto cleanup;
            else
                goto cleanup_nolog;
        }

        /* Issue a reset to the adapter to make sure it is in a useable state */
        rc = ipr_ioa_reset(ipr_cfg, IPR_IRQ_DISABLED);

        if (rc)
        {
            ipr_ipl_err(ipr_cfg, "Reset of IOA failed."IPR_EOL);
            ipr_kill_kernel_thread(ipr_cfg);
            ipr_free_mem(&ipr_cfg->shared);
            spin_unlock_irq(&io_request_lock);
            goto cleanup_nolog;
        }

        /* Request our IRQ */
        spin_unlock_irq(&io_request_lock);
        rc = request_irq (sis_irq, ipr_isr, SA_SHIRQ,"ipr", ipr_cfg);
        spin_lock_irq(&io_request_lock);

        if (rc)
        {
            ipr_ipl_err(ipr_cfg, "Couldn't register IRQ %d! rc=%d"IPR_EOL, sis_irq, rc);
            ipr_kill_kernel_thread(ipr_cfg);
            ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);
            ipr_free_mem(&ipr_cfg->shared);
            spin_unlock_irq(&io_request_lock);
            goto cleanup_nolog;
        }

        /* Fill in default IOA resource entry */
        ipr_cfg->shared.ioa_resource.is_ioa_resource = 1;
        ipr_cfg->shared.ioa_resource.nr_ioa_microcode = 1;
        ipr_cfg->shared.ioa_resource.resource_address.bus = IPR_IOA_RES_ADDR_BUS;
        ipr_cfg->shared.ioa_resource.resource_address.target = IPR_IOA_RES_ADDR_TARGET;
        ipr_cfg->shared.ioa_resource.resource_address.lun = IPR_IOA_RES_ADDR_LUN;
        memset(&ipr_cfg->shared.ioa_resource.std_inq_data.vpids,
                          ' ', sizeof(ipr_cfg->shared.ioa_resource.std_inq_data.vpids));
        memcpy(ipr_cfg->shared.ioa_resource.std_inq_data.vpids.vendor_id,
                           "IBM", 3);
        snprintf(ipr_cfg->shared.ioa_resource.std_inq_data.vpids.product_id,
                 8, "%X-%03d", ipr_cfg->shared.ccin, IPR_IOA_DEFAULT_MODEL);
        
        ipr_cfg->shared.ioa_resource.type = ipr_cfg->shared.ccin;
        ipr_cfg->shared.ioa_resource.model = IPR_IOA_DEFAULT_MODEL;
        ipr_cfg->shared.ioa_resource.host_no = ipr_cfg->shared.host_no;
        strcpy(ipr_cfg->shared.ioa_resource.serial_num, "00000000");
        ipr_update_location_data(&ipr_cfg->shared,
                                    &ipr_cfg->shared.ioa_resource);

        rc = ipr_init_ioa_part1(ipr_cfg);

        if (rc)
        {
            /* This is essentially dead code since ipr_init_ioa_part1 can only
             return success today */
            ipr_trace;
            ipr_kill_kernel_thread(ipr_cfg);
            ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);
            ipr_free_all_resources(ipr_cfg, 0, 0);
            continue;
        }

        /* Add this controller to the linked list of controllers */
        ipr_add_ioa_to_tail(ipr_cfg);

        continue;

    cleanup:
        ipr_log_err("Couldn't allocate enough memory for device driver! "IPR_EOL);
    cleanup_nolog:
        ipr_kfree(ipr_cfg->shared.resource_entry_list,
                     sizeof(struct ipr_resource_dll) * IPR_MAX_PHYSICAL_DEVS);

        ipr_dma_free(&ipr_cfg->shared,
                        sizeof(struct ipr_element_desc_page) * IPR_MAX_NUM_BUSES,
                        ipr_cfg->shared.p_ses_data[0], ipr_cfg->shared.ses_data_dma[0]);

        ipr_dma_free(&ipr_cfg->shared, sizeof(struct ipr_vpd_cbs),
                        ipr_cfg->shared.p_vpd_cbs,
                        ipr_cfg->shared.ioa_vpd_dma);

        ipr_kfree(ipr_cfg->shared.p_page_28,
                     sizeof(struct ipr_page_28_data));

        if (ipr_cfg->sis_cmnd_list[0] != NULL)
        {
            ipr_dma_free(&ipr_cfg->shared,
                            IPR_SENSE_BUFFERSIZE * IPR_NUM_CMD_BLKS,
                            ipr_cfg->sis_cmnd_list[0]->ccb.sense_buffer,
                            ipr_cfg->sis_cmnd_list[0]->ccb.sense_buffer_dma);
        }

        for (j=0; j < IPR_NUM_CMD_BLKS; j++)
            ipr_kfree(ipr_cfg->sis_cmnd_list[j], sizeof(struct ipr_cmnd));

        ipr_dma_free(&ipr_cfg->shared,
                        sizeof(struct ipr_hostrcb) * IPR_NUM_HCAMS,
                        ipr_cfg->hostrcb[0], ipr_cfg->hostrcb_dma[0]);

        if (ipr_cfg->p_ioa_cfg->sdt_reg_sel_size == IPR_SDT_REG_SEL_SIZE_1BYTE)
        {
            iounmap((void *)ipr_cfg->shared.hdw_bar_addr[3]);
            release_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[3],
                               pci_resource_len(pdev, 3));
        }
    cleanup_bar3:
        if (ipr_cfg->p_ioa_cfg->sdt_reg_sel_size == IPR_SDT_REG_SEL_SIZE_1BYTE)
        {
            iounmap((void *)ipr_cfg->shared.hdw_bar_addr[2]);
            release_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[2],
                               pci_resource_len(pdev, 2));
        }
    cleanup_bar2:
        iounmap((void *)sis_regs);
        release_mem_region(sis_regs_pci, pci_resource_len(pdev, ipr_cfg->p_ioa_cfg->bar_index));
    cleanup_bar0:
        /* Grab the spinlock again */
        spin_lock_irq(&io_request_lock);
        ipr_kfree(ipr_cfg->shared.p_location, sizeof(struct ipr_location_data));
        scsi_unregister(host);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Take the given adapter offline
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_take_ioa_offline(ipr_host_config *ipr_cfg)
{
    /* Reset the adapter */
    ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);

    ipr_beg_err(KERN_ERR);
    ipr_log_err("IOA taken offline - error recovery failed."IPR_EOL);
    ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_ERR);
    ipr_end_err(KERN_ERR);
    ipr_cfg->shared.ioa_is_dead = 1;
    ipr_cfg->shared.ioa_resource.ioa_dead = 1;
}

/*---------------------------------------------------------------------------
 * Purpose: Initializes IOAs found in ipr_find_ioa(..)
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Number IOAs of given type found
 *---------------------------------------------------------------------------*/
static int ipr_find_ioa_part2 (void)
{
    ipr_host_config *ipr_cfg, *temp_ipr_cfg;
    struct Scsi_Host *host = NULL;
    int rc = PCIBIOS_SUCCESSFUL;
    int major_num = 0;
    int minor_num = 0;
    u16 num_found = 0;

    ipr_cfg = ipr_cfg_head;

    while(ipr_cfg)
    {
        host = ipr_cfg->host;

        ipr_dbg_err("ipr_cfg adx: 0x%p\n", ipr_cfg);

        rc = ipr_init_ioa_part2(ipr_cfg);

        if (rc && (rc != IPR_RC_TIMEOUT))
        {
            if (ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NONE) != SUCCESS)
                rc = IPR_RC_FAILED;
            else
                rc = IPR_RC_SUCCESS;
        }
        else if (rc == IPR_RC_TIMEOUT)
            ipr_take_ioa_offline(ipr_cfg);

        if (ipr_invalid_adapter(ipr_cfg))
        {
            if (!testmode)
            {
                /* Reset the adapter */
                ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);
                ipr_cfg->shared.ioa_is_dead = 1;
                ipr_cfg->shared.ioa_resource.ioa_dead = 1;
            }

            ipr_beg_err(KERN_ERR);
            ipr_log_err("Adapter not supported in this hardware configuration."IPR_EOL);
            ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_ERR);
            ipr_end_err(KERN_ERR);
        }

        /* We only need 1 reboot notifier to shutdown all IOAs */
        if (num_found == 0)
        {
            register_reboot_notifier (&ipr_notifier);

            rc = register_ioctl32_conversion(IPR_IOCTL_SEND_COMMAND, NULL);

            if (rc)
            {
                ipr_log_err("Couldn't register ioctl32 conversion! rc=%d"IPR_EOL, rc);

                /* Lets clean ourselves up for this adapter and try the next one. */
                ipr_kill_kernel_thread(ipr_cfg);
                temp_ipr_cfg = ipr_cfg;
                ipr_cfg = ipr_cfg->p_next;
                ipr_remove_ioa_from_list(temp_ipr_cfg);
                ipr_free_all_resources(temp_ipr_cfg, 1, 0);
                continue;
            }
        }

        /* Register our controller. Use a dynamic major number */
        rc = devfs_register_chrdev (major_num, IPR_NAME, &ipr_fops);

        if (rc < 0)
        {
            ipr_log_err("Couldn't register "IPR_NAME" char device! rc=%d"IPR_EOL, rc);

            /* Lets clean ourselves up for this adapter and try the next one. */
            ipr_kill_kernel_thread(ipr_cfg);
            temp_ipr_cfg = ipr_cfg;
            ipr_cfg = ipr_cfg->p_next;
            ipr_remove_ioa_from_list(temp_ipr_cfg);
            ipr_free_all_resources(temp_ipr_cfg, (num_found == 0), 0);
            continue;
        }
        else
        {
            if (rc > 0)
                major_num = rc;

            ipr_cfg->major_num = major_num;
            ipr_cfg->minor_num = minor_num++;
            num_found++;
        }

        ipr_cfg->block_host_ops = 0;

        ipr_cfg = ipr_cfg->p_next;
    }

    return num_found;
}

/*---------------------------------------------------------------------------
 * Purpose: Kills off the kernel thread
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS
 *---------------------------------------------------------------------------*/
static u32 ipr_kill_kernel_thread(ipr_host_config *ipr_cfg)
{
    ENTER;

    lock_kernel();

    ipr_cfg->flag |= IPR_KILL_KERNEL_THREAD;

    /* Here we kill our kernel thread */
    if (ipr_cfg->task_thread)
    {
        DECLARE_COMPLETION(completion);
        ipr_cfg->completion = &completion;
        send_sig (SIGTERM, ipr_cfg->task_thread, 1);
        spin_unlock_irq(&io_request_lock);
        wait_for_completion (&completion);
        spin_lock_irq(&io_request_lock);
        ipr_cfg->completion = NULL;
    }

    unlock_kernel();

    LEAVE;

    return IPR_RC_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: IPL Part 1 for an IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS - Success
 *---------------------------------------------------------------------------*/
static u32 ipr_init_ioa_part1(ipr_host_config *ipr_cfg)
{
    struct ipr_ioa_vpd *p_ioa_vpd;
    struct ipr_cfc_vpd *p_cfc_vpd;
    struct ipr_inquiry_page3 *p_ucode_vpd;
    struct ipr_resource_dll *p_resource_dll;
    u32 rc = 0;
    int i;

    ENTER;

    p_ioa_vpd = ipr_cfg->shared.p_ioa_vpd;
    p_cfc_vpd = ipr_cfg->shared.p_cfc_vpd;
    p_ucode_vpd = ipr_cfg->shared.p_ucode_vpd;

    memset(ipr_cfg->shared.resource_entry_list,
                      0,
                      sizeof(struct ipr_resource_dll) * IPR_MAX_PHYSICAL_DEVS);

    memset(ipr_cfg->shared.bus, 0,
                      sizeof(struct ipr_bus) * (IPR_MAX_NUM_BUSES + 1));

    /* create resource table free dll */
    ipr_cfg->shared.rsteFreeH = ipr_cfg->shared.resource_entry_list;
    p_resource_dll = ipr_cfg->shared.rsteFreeH;
    p_resource_dll->prev = NULL;

    for (i = 0;
         i < (IPR_MAX_PHYSICAL_DEVS - 1);
         i++, p_resource_dll = p_resource_dll->next)
    {
        p_resource_dll->next = &ipr_cfg->shared.resource_entry_list[i+1];
        p_resource_dll->next->prev = p_resource_dll;
    }

    p_resource_dll->next = NULL;
    ipr_cfg->shared.rsteFreeT = p_resource_dll;
    ipr_cfg->shared.rsteUsedH = NULL;
    ipr_cfg->shared.rsteUsedT = NULL;

    /* set microcode status of IOA to be operational */
    ipr_cfg->shared.nr_ioa_microcode = 0;

    /* Zero out the SES Data table */
    for (i = 0; i < IPR_MAX_NUM_BUSES; i++)
        memset(ipr_cfg->shared.p_ses_data[i], 0,
                          sizeof(struct ipr_element_desc_page));

    /* Zero our HCAMs, put them all on the new list and mark their type */
    for (i = 0; i < IPR_NUM_LOG_HCAMS; i++)
    {
        memset(ipr_cfg->hostrcb[i], 0, sizeof(struct ipr_hostrcb));
        ipr_cfg->new_hostrcb[i] = ipr_cfg->hostrcb[i];
        ipr_cfg->new_hostrcb[i]->op_code = IPR_HOST_RCB_OP_CODE_LOG_DATA;
        ipr_cfg->done_hostrcb[i] = NULL;
    }

    for (; i < IPR_NUM_HCAMS; i++)
    {
        memset(ipr_cfg->hostrcb[i], 0, sizeof(struct ipr_hostrcb));
        ipr_cfg->new_hostrcb[i] = ipr_cfg->hostrcb[i];
        ipr_cfg->new_hostrcb[i]->op_code = IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE;
        ipr_cfg->done_hostrcb[i] = NULL;
    }

    /* Start the IOA off and running */
    rc = ipr_init_ioa_internal_part1(&ipr_cfg->shared);

    LEAVE;

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: IPL Part 2 for an IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS       - Success
 *          IPR_RC_XFER_FAILED   - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ          - No HRRQ interrupt
 *          IPR_IOARRIN_LOST     - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR       - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED - IOA unit checked
 *          IPR_RC_FAILED        - Initialization failed
 *          IPR_RC_TIMEOUT       - IOA timed out
 *          IPR_RC_NOMEM         - Out of memory
 *---------------------------------------------------------------------------*/
static u32 ipr_init_ioa_part2(ipr_host_config *ipr_cfg)
{
    u32 rc = 0, rc2 = PCIBIOS_SUCCESSFUL;
    struct ipr_resource_entry *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;
    struct ipr_lun *p_lun;
    const struct ipr_ioa_cfg_t *p_ioa_cfg = ipr_cfg->p_ioa_cfg;

    ENTER;

    rc = ipr_init_ioa_internal_part2(&ipr_cfg->shared);

    if (!rc)
    {
        /* Interrupts are now enabled and IOA is functional */

        /* Create the SCSI-ID -> Resource Address lookup table */
        for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
             p_resource_dll != NULL;
             p_resource_dll = p_resource_dll->next)
        {
            p_resource_entry = &p_resource_dll->data;

            if (p_resource_entry->is_ioa_resource)
            {
                /* Do nothing - ignore entry */
            }
            else if (((p_resource_entry->resource_address.bus >= IPR_MAX_NUM_BUSES) &&
                      (p_resource_entry->resource_address.bus != 0xff)) ||
                     (p_resource_entry->resource_address.target >= IPR_MAX_NUM_TARGETS_PER_BUS))
            {
                ipr_log_err("Invalid resource address %x\n",
                               IPR_GET_PHYSICAL_LOCATOR(p_resource_entry->resource_address));
            }
            else
            {
                p_lun = ipr_get_lun_res_addr(ipr_cfg,
                                                p_resource_entry->resource_address);

                p_lun->is_valid_entry = 1;
                p_lun->p_resource_entry = p_resource_entry;
                p_lun->stop_new_requests = 0;
            }
        }

        /* Indicate we have IPLed successfully */ 
        ipr_cfg->flag |= IPR_ALLOW_REQUESTS | IPR_ALLOW_HCAMS;

        ipr_mailbox(ipr_cfg);
    }

    switch(rc)
    {
        case IPR_RC_SUCCESS:
            break;
        case IPR_IOA_UNIT_CHECKED:
            if (p_ioa_cfg->cpu_rst_support == IPR_CPU_RST_SUPPORT_CFGSPC_403RST_BIT)
            {
                /* Hold the 403 in reset so we can get the unit check buffer */
                rc2 = pci_write_config_dword(ipr_cfg->pdev, IPR_RESET_403_OFFSET, IPR_RESET_403);
            }
            if (rc2 != PCIBIOS_SUCCESSFUL)
            {
                /* Log an error that the IOA unit checked. Since we couldn't write config
                 space, we probably shouldn't even try to read memory space */
                ipr_unit_check_no_data(ipr_cfg);
            }
            else
            {
                ipr_break_or_die_if_reset_reload_disabled;

                /* Reset the IOA */
                ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);

                /* Process the Unit check buffer */
                ipr_get_unit_check_buffer(ipr_cfg);
            }
        case IPR_RC_XFER_FAILED:
        case IPR_NO_HRRQ:
        case IPR_IOARRIN_LOST:
        case IPR_RC_TIMEOUT:
        case IPR_MMIO_ERROR:
            ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);
            break;
        case IPR_RC_FAILED:
            ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);
            break;
    };

    LEAVE;
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Initializes the IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *          IPR_RC_FAILED            - Initialization failed
 *          IPR_RC_TIMEOUT           - IOA timed out
 *          IPR_RC_NOMEM             - Out of memory
 *---------------------------------------------------------------------------*/
static u32 ipr_init_ioa(ipr_host_config *ipr_cfg)
{
    ipr_init_ioa_part1(ipr_cfg);
    return ipr_init_ioa_part2(ipr_cfg);
}

/*---------------------------------------------------------------------------
 * Purpose: Interrupt service routine
 * Context: Interrupt level only
 * Lock State: no locks assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_isr (int irq, void *devp, struct pt_regs *regs)
{
    ipr_host_config *ipr_cfg;
    struct ipr_cmnd *p_cur_sis_cmnd = NULL;
    unsigned long io_flags = 0;
    u32 rc, rc2 = PCIBIOS_SUCCESSFUL;
    const struct ipr_ioa_cfg_t *p_ioa_cfg;

    ipr_cfg = (ipr_host_config *) devp;

    if (ipr_cfg->host->irq != irq)
        return;

    spin_lock_irqsave(&io_request_lock, io_flags);

    p_ioa_cfg = ipr_cfg->p_ioa_cfg;

    rc = ipr_get_done_ops(&ipr_cfg->shared, (struct ipr_ccb **)&p_cur_sis_cmnd);

    if (rc == IPR_RC_SUCCESS)
    {
        while(p_cur_sis_cmnd)
        {
            /* Pull off of pending queue */
            ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_cur_sis_cmnd);

            /* Put onto completed queue */
            ipr_put_sis_cmnd_to_completed(ipr_cfg, p_cur_sis_cmnd);

            p_cur_sis_cmnd = (struct ipr_cmnd *)p_cur_sis_cmnd->ccb.p_next_done;
        }
    }
    else
    {
        /* Process other interrupts */
        switch(rc)
        {
            case IPR_RC_XFER_FAILED:
            case IPR_NO_HRRQ:
            case IPR_IOARRIN_LOST:
            case IPR_MMIO_ERROR:

                /* Any time these interrupts have been seen we have ended up getting a freeze error.
                 Since we don't have recovery for a freeze error today, we kernel panic and
                 point to the IOA so it can be replaced. */
                panic(IPR_ERR": Permanent IOA failure on %s. rc: 0x%08X"IPR_EOL,
                      ipr_cfg->shared.ioa_host_str, rc);
                break;
            case IPR_IOA_UNIT_CHECKED:

                ipr_dbg_err("IOA Unit checked!!"IPR_EOL);
                IPR_DBG_CMD(ipr_log_ioa_physical_location(ipr_cfg->shared.p_location,
                                                                  KERN_CRIT));

                /* Prevent ourselves from getting any more requests */
                ipr_block_all_requests(ipr_cfg);

                if (p_ioa_cfg->cpu_rst_support == IPR_CPU_RST_SUPPORT_CFGSPC_403RST_BIT)
                {
                    rc2 = pci_write_config_dword(ipr_cfg->pdev,
                                                 IPR_RESET_403_OFFSET, IPR_RESET_403);
                }

                if (rc2 != PCIBIOS_SUCCESSFUL)
                {
                    ipr_log_err("Write of 403 reset register failed"IPR_EOL);

                    ipr_cfg->flag |= IPR_IOA_NEEDS_RESET;

                    /* Log an error that the IOA unit checked. Since we couldn't write config
                     space, we probably shouldn't even try to read memory space */
                    ipr_unit_check_no_data(ipr_cfg);
                }
                else
                    ipr_cfg->flag |= IPR_UNIT_CHECKED;


                /* Wake up task level thread to fetch Unit Check buffer and reset adapter */
                ipr_wake_task(ipr_cfg);
                break;
            case IPR_RESET_ADAPTER:
                /* Prevent ourselves from getting any more requests */
                ipr_block_all_requests(ipr_cfg);

                ipr_cfg->flag |= IPR_IOA_NEEDS_RESET;

                /* Wake up task level thread to reset adapter */
                ipr_wake_task(ipr_cfg);
                break;
            default:
                break;
        }

        spin_unlock_irqrestore(&io_request_lock,io_flags);
        return;
    }

    ipr_ops_done(ipr_cfg);

    spin_unlock_irqrestore(&io_request_lock,io_flags);
}

/*---------------------------------------------------------------------------
 * Purpose: Free resources - Used when unloading module
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: 0 - Success
 *---------------------------------------------------------------------------*/
int ipr_release(struct Scsi_Host *p_scsi_host)
{
    ipr_host_config *ipr_cfg;
    int rc;
    unsigned long io_flags = 0;
    DECLARE_WAIT_QUEUE_HEAD(internal_wait);

    ENTER;

    ipr_cfg = (ipr_host_config *) p_scsi_host->hostdata;

    spin_lock_irqsave(&io_request_lock,io_flags);

    devfs_unregister_chrdev(ipr_cfg->major_num, IPR_NAME);

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        while(1)
        {
            /* Loop forever waiting for IOA to come out of reset/reload, checking once a second */
            if ((ipr_cfg->flag & IPR_IN_RESET_RELOAD) == 0)
                break;
            else 
                ipr_sleep_on_timeout(&io_request_lock, &internal_wait, HZ);
        }
    }

    ipr_init_finished = 0;

    /* Shutdown the IOA */
    rc = ipr_shutdown_ioa(ipr_cfg, IPR_SHUTDOWN_NORMAL);

    ipr_mask_interrupts(&ipr_cfg->shared);

    /* Here we reset the IOA to get it back in a POR state and
     so we can free up the memory we allocated for HCAMs */
    rc = ipr_ioa_reset(ipr_cfg, IPR_IRQ_DISABLED);

    if (rc != IPR_RC_SUCCESS)
    {
        /* Reset to the adapter failed for some reason. If this happens something is
         seriously wrong */
        ipr_log_crit("Reset to adapter failed! %s"IPR_EOL,
                        ipr_cfg->shared.ioa_host_str);
    }

    /* The binary could have issued commands to us that may still be outstanding */
    /* This will return them failed */
    ipr_fail_all_ops(ipr_cfg);

    /* Kill off our kernel thread */
    ipr_kill_kernel_thread(ipr_cfg);

    /* Remove the IOA from the linked list */
    ipr_remove_ioa_from_list(ipr_cfg);

    /* Free all the memory we allocated */
    ipr_free_all_resources(ipr_cfg, 1, 0);

    if (ipr_mem_debug &&
        (ipr_num_ctlrs == 0) &&
        (ipr_kmalloced_mem != 0))
    {
        panic("ipr_kmalloced_mem: %d !!"IPR_EOL, ipr_kmalloced_mem);
    }

    spin_unlock_irqrestore(&io_request_lock, io_flags);

    LEAVE;
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Abort a single op
 * Context: Task level only - mid-layer's ERP thread
 * Lock State: io_request_lock assumed to be held
 * Returns: SUCCESS     - Success
 *          FAILED      - Failure
 * Notes: We return SUCCESS if we cannot find the op.
 *---------------------------------------------------------------------------*/
int ipr_abort(Scsi_Cmnd *p_scsi_cmd)
{
    struct ipr_cmnd *p_sis_cmnd;
    ipr_host_config *ipr_cfg;
    struct ipr_lun *p_lun;
    struct ipr_resource_entry *p_resource;
    int rc;

    if (!p_scsi_cmd)
        return FAILED;

    ENTER;

    ipr_cfg = (ipr_host_config *) p_scsi_cmd->host->hostdata;

    /* If we are currently going through reset/reload, return failed. This will force the
     mid-layer to call ipr_host_reset, which will then go to sleep and wait for the
     reset to complete */
    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        return FAILED;

    if (ipr_cfg->shared.ioa_is_dead)
        return FAILED;

    /* Get a pointer to the LUN structure */
    p_lun = ipr_get_lun_scsi(ipr_cfg, p_scsi_cmd);

    if (p_lun)
    {
        if (p_lun->is_valid_entry)
            p_resource = p_lun->p_resource_entry;
        else /* Not a valid resource - cannot abort the op */
        {
            ipr_trace;
            return SUCCESS;
        }
    }
    else
    {
        ipr_trace;
        return SUCCESS;
    }

    /* If this is a GPDD, lets do a cancel all requests to the device */
    if (!p_resource->is_af)
        return ipr_cancel_all(p_scsi_cmd);

    /* Must be an AF DASD or VSET op */
    p_sis_cmnd = ipr_cfg->qPendingH;

    /* Look for the op on the pending queue */
    while (p_sis_cmnd != NULL)
    {
        if (p_sis_cmnd->p_scsi_cmd == p_scsi_cmd)
            break;

        p_sis_cmnd = p_sis_cmnd->p_next;
    }

    /* Couldn't find it - Lets look on the completed queue */
    if (p_sis_cmnd == NULL)
    {
        p_sis_cmnd = ipr_cfg->qCompletedH;

        while (p_sis_cmnd != NULL)
        {
            if (p_sis_cmnd->p_scsi_cmd == p_scsi_cmd)
                break;

            p_sis_cmnd = p_sis_cmnd->p_next;
        }

        if (p_sis_cmnd != NULL)
        {
            /* Found the op on the completed queue. Send response to the host */
            ipr_ops_done(ipr_cfg);
            return SUCCESS;
        }
        else /* Command not outstanding to the IOA */
            return SUCCESS;
    }

    /* Aborts are not supported to volume sets */
    if (p_resource->subtype == IPR_SUBTYPE_VOLUME_SET)
        return FAILED;

    rc = ipr_cancelop(ipr_cfg, p_sis_cmnd, p_scsi_cmd);

    LEAVE;

    return rc; 
}

/*---------------------------------------------------------------------------
 * Purpose: Cancel the given op (called for LDD ops only)
 * Context: Task level only - mid-layer's ERP thread
 * Lock State: io_request_lock assumed to be held
 * Returns: SUCCESS  - Success
 *          FAILED   - Failure
 * Notes: We return success if we cannot find the op.
 *---------------------------------------------------------------------------*/
static int ipr_cancelop(ipr_host_config *ipr_cfg,
                           struct ipr_cmnd *p_sis_cmnd,
                           Scsi_Cmnd *p_scsi_cmd)
{
    struct ipr_cmnd *p_cancel_sis_cmnd;
    signed long time_left;
    u32 timeout = IPR_CANCEL_TIMEOUT;
    u32 rc;
    char dev_loc_str[IPR_MAX_LOCATION_LEN];

    ENTER;

    /* Get a SIS Cmd block for the Cancel Request and set it up */
    p_cancel_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_cancel_sis_cmnd);

    ipr_dbg_err("Cancelling 0x%02x ipr_cmd: 0x%p, 0x%p"IPR_EOL, p_scsi_cmd->cmnd[0],
                   p_sis_cmnd, p_cancel_sis_cmnd);

    /* Tie these two requests together */
    p_cancel_sis_cmnd->p_cancel_op = p_sis_cmnd;
    p_sis_cmnd->p_cancel_op = p_cancel_sis_cmnd;

    /* Setup command block */
    p_cancel_sis_cmnd->ccb.cdb[0] = IPR_CANCEL_REQUEST;
    *(unsigned long*)&p_cancel_sis_cmnd->ccb.cdb[2] = (unsigned long)p_sis_cmnd;

    p_cancel_sis_cmnd->ccb.p_resource = p_sis_cmnd->ccb.p_resource;

    p_sis_cmnd->ccb.flags |= IPR_ABORTING;
    p_cancel_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND | IPR_IOA_CMD;

    p_cancel_sis_cmnd->ccb.cmd_len = 10;

    /* Queue the cancel request to the adapter */
    rc = ipr_queue_internal(&ipr_cfg->shared, &p_cancel_sis_cmnd->ccb);

    if (rc == IPR_RC_OP_NOT_SENT)
    {
        /* Cancel was not sent to the adapter. This is the return code we receive if
         the op was not found on the lower level's queue. This should be dead code... */
        ipr_trace;
        ipr_put_sis_cmnd_to_free(ipr_cfg, p_cancel_sis_cmnd);
        return FAILED;
    }

    init_waitqueue_head(&p_cancel_sis_cmnd->wait_q);

    /* Sleep on the response and time it */
    time_left = ipr_sleep_on_timeout(&io_request_lock, &p_cancel_sis_cmnd->wait_q, timeout);

    rc = p_cancel_sis_cmnd->ccb.completion;

    if (time_left <= 0)
        p_cancel_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        /* We are going through reset/reload for some reason and should not
         be talking to the adapter */
        return FAILED;
    }
    else if (time_left <= 0)
    {
        ipr_dev_loc_str(&ipr_cfg->shared, p_sis_cmnd->ccb.p_resource, dev_loc_str);

        /* Cancel timed out - reset the adapter */
        ipr_log_err("abort to %s timed out."IPR_EOL,
                       dev_loc_str);
        return ipr_host_reset(p_scsi_cmd);
    }

    ipr_put_sis_cmnd_to_free(ipr_cfg, p_cancel_sis_cmnd);

    LEAVE;

    if (rc == IPR_RC_FAILED)
        return FAILED;
    else
        return SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Reset the host adapter
 * Context: Task level only - mid-layer's ERP thread
 * Lock State: io_request_lock assumed to be held
 * Returns: SUCCESS  - Success
 *          FAILED   - Failure
 *---------------------------------------------------------------------------*/
int ipr_host_reset(Scsi_Cmnd *p_scsi_cmd)
{
    ipr_host_config *ipr_cfg;
    int rc;

    ENTER;

    if (!p_scsi_cmd)
    {
        ipr_trace;
        return FAILED;
    }

    ipr_cfg = (ipr_host_config *) p_scsi_cmd->host->hostdata;

    ipr_beg_err(KERN_ERR);
    ipr_log_err("Adapter being reset as a result of error recovery."IPR_EOL);
    ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_ERR);
    ipr_end_err(KERN_ERR);

    ipr_break_or_die_if_reset_reload_disabled;

    if (WAIT_FOR_DUMP == ipr_get_sdt_state)
        ipr_get_sdt_state = GET_DUMP;

    rc = ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_ABBREV);

    if (ipr_get_sdt_state == DUMP_OBTAINED)
        wake_up_interruptible(&ipr_sdt_wait_q);

    if (rc != SUCCESS)
    {
        printk(IPR_EOL);
        ipr_beg_err(KERN_CRIT);
        ipr_log_crit("Reset of IOA failed."IPR_EOL);
        ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_CRIT);
        ipr_end_err(KERN_CRIT);
    }

    LEAVE;

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Reset/Reload the IOA
 * Context: Task level only 
 * Lock State: io_request_lock assumed to be held
 * Requirements: This function assumes that all new host commands have been
 *               stopped. 
 * Returns: SUCCESS     - Success
 *          FAILED      - Failure
 *---------------------------------------------------------------------------*/
static int ipr_reset_reload(ipr_host_config *ipr_cfg,
                               enum ipr_shutdown_type shutdown_type)
{
    int rc, i;
    DECLARE_WAIT_QUEUE_HEAD(internal_wait);

    if (ipr_cfg->shared.ioa_is_dead)
        return FAILED;

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        while(1)
        {
            /* Loop forever waiting for IOA to come out of reset/reload, checking once a second */
            if ((ipr_cfg->flag & IPR_IN_RESET_RELOAD) == 0)
                break;
            else 
                ipr_sleep_on_timeout(&io_request_lock, &internal_wait, HZ);
        }

        /* If we got hit with a host reset while we were already resetting
         the adapter for some reason, and the reset failed. */
        if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
        {
            ipr_trace;
            return FAILED;
        }
    }
    else
    {
        for (i = 0, rc = IPR_RC_FAILED;
             (i < IPR_NUM_RESET_RELOAD_RETRIES) && (rc != IPR_RC_SUCCESS);
             i++, shutdown_type = IPR_SHUTDOWN_NONE)
        {
            ipr_cfg->flag = (IPR_IN_RESET_RELOAD | IPR_OPERATIONAL);

            ipr_break_if_reset_reload_disabled;

            /* Shutdown the IOA */
            rc = ipr_shutdown_ioa(ipr_cfg, shutdown_type);

            /* Reset the adapter */
            rc = ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);

            if (rc == IPR_RC_SUCCESS)
            {
                /* Fail all ops back to the caller - they will be retried later */
                ipr_fail_all_ops(ipr_cfg);
            }
            else
            {
                panic(IPR_ERR": IOA reset failed on %s. rc: 0x%08X"IPR_EOL,
                      ipr_cfg->shared.ioa_host_str, rc);
            }

            if (GET_DUMP == ipr_get_sdt_state)
            {
                ipr_get_ioa_smart_dump(ipr_cfg);

                /* Reset the adapter */
                rc = ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);

                if (rc != IPR_RC_SUCCESS)
                    panic(IPR_ERR": IOA reset failed on %s. rc: 0x%08X"IPR_EOL,
                          ipr_cfg->shared.ioa_host_str, rc);
            }
            else if (NO_DUMP == ipr_get_sdt_state)
                ipr_get_sdt_state = WAIT_FOR_DUMP;

            if (rc == IPR_RC_SUCCESS)
                rc = ipr_init_ioa(ipr_cfg);

            ipr_cfg->flag &= ~IPR_IN_RESET_RELOAD;
        }

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_take_ioa_offline(ipr_cfg);
            return FAILED;
        }
    }

    return SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Reset the IOA.
 * Context: Task level only 
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *          IPR_RC_FAILED    - Failure
 *---------------------------------------------------------------------------*/
static int ipr_ioa_reset(ipr_host_config *ipr_cfg, enum ipr_irq_state irq_state)
{
    u16 cmd;
    u32 cache_line;
    int rc = IPR_RC_SUCCESS;
    int timeout, i, pcix_command_reg;
    const struct ipr_ioa_cfg_t *p_ioa_cfg = ipr_cfg->p_ioa_cfg;

    ENTER;

    /* Turn appropriate flags off */
    ipr_cfg->flag &= ~(IPR_OPERATIONAL | IPR_ALLOW_HCAMS | IPR_ALLOW_REQUESTS);

    /* Save away PCI command register */
    if (pci_read_config_word(ipr_cfg->pdev, PCI_COMMAND, &cmd) != PCIBIOS_SUCCESSFUL)
        goto failure;

    /* Save away cache line size/latency timer register */
    if (pci_read_config_dword(ipr_cfg->pdev, PCI_CACHE_LINE_SIZE, &cache_line) != PCIBIOS_SUCCESSFUL)
        goto failure;

    cmd |= (PCI_COMMAND_PARITY | PCI_COMMAND_SERR);

    ipr_cfg->shared.ioa_operational = 0;

    if(irq_state == IPR_IRQ_ENABLED)
        ipr_mask_interrupts(&ipr_cfg->shared);

    spin_unlock_irq(&io_request_lock);

    /* We will only wait 2 seconds for permission to reset the IOA */
    timeout = 2000;

    /* Alert the IOA of a pending reset */
    ipr_reset_alert(&ipr_cfg->shared);

    /* Keep looping while reset to the IOA is not allowed and we
     haven't timed out yet */
    while(!ipr_reset_allowed(&ipr_cfg->shared) && timeout)
    {
        /* Sleep for 10 milliseconds */
        ipr_sleep_no_lock(10);
        timeout -= 10;
    }

    if (timeout == 0)
        ipr_log_err("Timed out waiting for permission to reset IOA"IPR_EOL);

    rc = ipr_toggle_reset(ipr_cfg);

    spin_lock_irq(&io_request_lock);

    if (rc)
    {
        ipr_trace;
        goto failure;
    }

    /* Restore the BARs */
    for (i = (PCI_BASE_ADDRESS_0 / 4), rc = PCIBIOS_SUCCESSFUL;
         (i < IPR_CONFIG_SAVE_WORDS) && (rc == PCIBIOS_SUCCESSFUL);
         i++)
    {
        rc = pci_write_config_dword(ipr_cfg->pdev, i*4, ipr_cfg->pci_cfg_buf[i]);
    }

    if (rc != PCIBIOS_SUCCESSFUL)
    {
        ipr_trace;
        goto failure;
    }

    /* Restore the cache line size/latency timer */
    cache_line = (cache_line & ~IPR_CL_SIZE_LATENCY_MASK) |
        (p_ioa_cfg->cl_size_latency_timer & IPR_CL_SIZE_LATENCY_MASK);

    if (pci_write_config_dword(ipr_cfg->pdev, PCI_CACHE_LINE_SIZE,
                               cache_line) != PCIBIOS_SUCCESSFUL)
    {
        ipr_trace;
        goto failure;
    }

    if (pci_write_config_word(ipr_cfg->pdev, PCI_COMMAND, cmd) != PCIBIOS_SUCCESSFUL)
    {
        ipr_trace;
        goto failure;
    }

    pcix_command_reg = pci_find_capability(ipr_cfg->pdev, IPR_PCIX_COMMAND_REG_ID);

    if (pcix_command_reg)
    {
        /* Need to restore the PCI-X Command register. */
        if (pci_write_config_word(ipr_cfg->pdev, pcix_command_reg,
                                  ipr_cfg->saved_pcix_command_reg) != PCIBIOS_SUCCESSFUL)
        {
            ipr_trace;
            goto failure;
        }
    }

    /* Turn the operational flag on */
    ipr_cfg->flag |= IPR_OPERATIONAL;

    LEAVE;

    return IPR_RC_SUCCESS;

failure:
    return IPR_RC_FAILED;
}

/*---------------------------------------------------------------------------
 * Purpose: Reset the device
 * Context: Task level only - mid-layer's ERP thread
 * Lock State: io_request_lock assumed to be held
 * Returns: SUCCESS     - Success
 *          FAILED      - Failure
 * Notes: This command is only supported to GPDD devices
 *---------------------------------------------------------------------------*/
int ipr_dev_reset(Scsi_Cmnd *p_scsi_cmd)
{
    struct ipr_cmnd *p_reset_sis_cmnd;
    ipr_host_config *ipr_cfg;
    signed long time_left;
    struct ipr_lun *p_lun;
    struct ipr_resource_entry *p_resource_entry;
    int rc, rc2, rc3, rc4;
    int retries = 10;
    char dev_loc_str[IPR_MAX_LOCATION_LEN];

    ENTER;

    if (!p_scsi_cmd)
        return FAILED;

    ipr_cfg = (ipr_host_config *) p_scsi_cmd->host->hostdata;

    /* If we are currently going through reset/reload, return failed. This will force the
     mid-layer to call ipr_host_reset, which will then go to sleep and wait for the
     reset to complete */
    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        return FAILED;

    if (ipr_cfg->shared.ioa_is_dead)
        return FAILED;

    p_lun = ipr_get_lun_scsi(ipr_cfg, p_scsi_cmd);

    if (p_lun)
    {
        if (p_lun->is_valid_entry)
            p_resource_entry = p_lun->p_resource_entry;
        else /* Not a valid device */
        {
            ipr_trace;
            return SUCCESS;
        }
    }
    else
    {
        ipr_trace;
        return SUCCESS;
    }

    /* We only support device reset to GPDD devices */
    if (p_resource_entry->is_af)
        return FAILED;

    /* Cancel all outstanding ops to the device. */
    rc = ipr_cancel_all(p_scsi_cmd);

    /* Could not cancel outstanding ops. Cannot return success to mid-layer */
    if (rc != SUCCESS)
    {
        ipr_trace;
        return rc;
    }

    /* Get a command block for device reset command */
    p_reset_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_reset_sis_cmnd);
    p_reset_sis_cmnd->ccb.p_resource = p_resource_entry;
    p_reset_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND | IPR_IOA_CMD;
    p_reset_sis_cmnd->ccb.cdb[0] = IPR_RESET_DEVICE;

    p_reset_sis_cmnd->ccb.cmd_len = 10;

    ipr_queue_internal(&ipr_cfg->shared, &p_reset_sis_cmnd->ccb);

    init_waitqueue_head(&p_reset_sis_cmnd->wait_q);

    /* Sleep on the response and time it */
    time_left = ipr_sleep_on_timeout(&io_request_lock, &p_reset_sis_cmnd->wait_q,
                                        IPR_DEVICE_RESET_TIMEOUT);

    rc2 = p_reset_sis_cmnd->ccb.completion;

    if (time_left <= 0)
        p_reset_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        /* We are going through reset/reload for some reason and should not
         be talking to the adapter */
        ipr_trace;
        return FAILED;
    }
    else if (time_left <= 0)
    {
        ipr_dev_loc_str(&ipr_cfg->shared, p_resource_entry, dev_loc_str);

        /* Reset device timed out - reset the adapter */
        ipr_log_err("reset device to %s timed out."IPR_EOL, dev_loc_str);
        return ipr_host_reset(p_scsi_cmd);
    }

    /* The upper layer device drivers assume that commands can continue as
     soon as we return. Since the device was just reset, it may take some time
     before it is ready for a command again. */
    while(retries--)
    {
        /* Re-initialize the command block for re-use */
        ipr_initialize_sis_cmnd(p_reset_sis_cmnd);

        /* Put this cmd blk back on the pending queue */
        ipr_put_sis_cmnd_to_pending(ipr_cfg, p_reset_sis_cmnd);

        /* Setup resource entry pointer */
        p_reset_sis_cmnd->ccb.p_resource = p_resource_entry;

        /* Send a test unit ready */
        p_reset_sis_cmnd->ccb.cdb[0] = TEST_UNIT_READY;
        p_reset_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND;

        p_reset_sis_cmnd->ccb.cmd_len = 6;

        ipr_queue_internal(&ipr_cfg->shared, &p_reset_sis_cmnd->ccb);

        init_waitqueue_head(&p_reset_sis_cmnd->wait_q);

        /* Sleep on the response and time it */
        time_left = ipr_sleep_on_timeout(&io_request_lock, &p_reset_sis_cmnd->wait_q,
                                            IPR_INTERNAL_TIMEOUT);

        rc3 = p_reset_sis_cmnd->ccb.completion;

        if (time_left <= 0)
            p_reset_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

        if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        {
            /* We are going through reset/reload for some reason and should not
             be talking to the adapter */
            ipr_trace;
            return FAILED;
        }
        else if (time_left <= 0)
        {
            ipr_dev_loc_str(&ipr_cfg->shared, p_resource_entry, dev_loc_str);

            /* Test Unit Ready timed out - reset the adapter */
            ipr_log_err("test unit ready to %s timed out."IPR_EOL, dev_loc_str);
            return ipr_host_reset(p_scsi_cmd);
        }

        /* If we were able to successfully send a test unit ready, the
         device must be ready to accept new commands and we can return
         to the mid-layer */
        if (rc3 == IPR_RC_SUCCESS)
            goto leave;

        /* Re-initialize the command block for re-use */
        ipr_initialize_sis_cmnd(p_reset_sis_cmnd);

        /* Put this cmd blk back on the pending queue */
        ipr_put_sis_cmnd_to_pending(ipr_cfg, p_reset_sis_cmnd);

        /* Setup resource entry pointer */
        p_reset_sis_cmnd->ccb.p_resource = p_resource_entry;

        /* Send the sync-complete */
        p_reset_sis_cmnd->ccb.cdb[0] = IPR_SYNC_COMPLETE;

        p_reset_sis_cmnd->ccb.cmd_len = 10;

        /* Turn on IOA cmd flag */
        p_reset_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND | IPR_IOA_CMD;

        ipr_queue_internal(&ipr_cfg->shared, &p_reset_sis_cmnd->ccb);

        init_waitqueue_head(&p_reset_sis_cmnd->wait_q);

        /* Sleep on the response and time it */
        time_left = ipr_sleep_on_timeout(&io_request_lock, &p_reset_sis_cmnd->wait_q,
                                            IPR_INTERNAL_TIMEOUT);

        rc4 = p_reset_sis_cmnd->ccb.completion;

        if (time_left <= 0)
            p_reset_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

        if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        {
            /* We are going through reset/reload for some reason and should not
             be talking to the adapter */
            ipr_trace;
            return FAILED;
        }
        else if (time_left <= 0)
        {
            ipr_dev_loc_str(&ipr_cfg->shared, p_resource_entry, dev_loc_str);

            /* Sync complete timed out - reset the adapter */
            ipr_log_err("sync complete to %s timed out."IPR_EOL, dev_loc_str);
            return ipr_host_reset(p_scsi_cmd);
        }

        /* Sleep for 2 seconds */
        ipr_sleep(2000);
    }

leave:
    ipr_put_sis_cmnd_to_free(ipr_cfg, p_reset_sis_cmnd);

    LEAVE;

    if (rc2 == IPR_RC_FAILED)
    {
        ipr_trace;
        return FAILED;
    }

    return SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Cancel all requests to a device
 * Context: Task level only 
 * Lock State: io_request_lock assumed to be held
 * Returns: SUCCESS     - Success
 *          FAILED      - Failure
 * Notes: This command is only supported to tape/optical devices
 *---------------------------------------------------------------------------*/
static int ipr_cancel_all(Scsi_Cmnd *p_scsi_cmd)
{
    struct ipr_cmnd *p_sis_cmnd, *p_cancel_sis_cmnd;
    ipr_host_config *ipr_cfg;
    signed long time_left;
    struct ipr_lun *p_lun;
    struct ipr_resource_entry *p_resource_entry;
    int rc, rc2;
    char dev_loc_str[IPR_MAX_LOCATION_LEN];

    ENTER;

    ipr_cfg = (ipr_host_config *) p_scsi_cmd->host->hostdata;

    p_lun = ipr_get_lun_scsi(ipr_cfg, p_scsi_cmd);

    p_resource_entry = p_lun->p_resource_entry;

    /* Now we need to look for all outstanding command for this device and
     mark them as aborting */

    /* Look for ops to this device on the pending queue */
    p_sis_cmnd = ipr_cfg->qPendingH;

    while (p_sis_cmnd != NULL)
    {
        if (p_sis_cmnd->ccb.p_resource == p_resource_entry)
        {
            /* Turn on the aborting bit */
            p_sis_cmnd->ccb.flags |= IPR_ABORTING;

            /* Restore the command if we were doing ERP  */
            if (p_sis_cmnd->ccb.flags & IPR_ERP_CMD)
                ipr_end_erp(p_sis_cmnd);
        }
        p_sis_cmnd = p_sis_cmnd->p_next;
    }

    /* Look for ops to this device on the completed queue */
    p_sis_cmnd = ipr_cfg->qCompletedH;

    while (p_sis_cmnd != NULL)
    {
        if (p_sis_cmnd->ccb.p_resource == p_resource_entry)
        {
            /* Turn on the aborting bit */
            p_sis_cmnd->ccb.flags |= IPR_ABORTING;

            /* Restore the command if we were doing ERP  */
            if (p_sis_cmnd->ccb.flags & IPR_ERP_CMD)
                ipr_end_erp(p_sis_cmnd);
        }
        p_sis_cmnd = p_sis_cmnd->p_next;
    }

    /* Look for ops to this device on the error queue */
    p_sis_cmnd = ipr_cfg->qErrorH;

    while (p_sis_cmnd != NULL)
    {
        if (p_sis_cmnd->ccb.p_resource == p_resource_entry)
        {
            /* Remove from the error queue and put on completed queue */
            ipr_remove_sis_cmnd_from_error(ipr_cfg, p_sis_cmnd);
            ipr_put_sis_cmnd_to_completed(ipr_cfg, p_sis_cmnd);

            /* Mark the op as aborting */
            p_sis_cmnd->ccb.flags |= IPR_ABORTING;

            /* Restore the command if we were doing ERP  */
            if (p_sis_cmnd->ccb.flags & IPR_ERP_CMD)
                ipr_end_erp(p_sis_cmnd);
        }
        p_sis_cmnd = p_sis_cmnd->p_next;
    }

    /* Send all the ops back */
    ipr_ops_done(ipr_cfg);

    /* Get a command block for Cancel All Requests command */
    p_cancel_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_cancel_sis_cmnd);
    p_cancel_sis_cmnd->ccb.p_resource = p_resource_entry;

    /* Note: IPR_CMD_SYNC_OVERRIDE is required on 2748, 2763, and 2778 adapters */
    p_cancel_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND |
        IPR_CMD_SYNC_OVERRIDE | IPR_IOA_CMD;
    p_cancel_sis_cmnd->ccb.cdb[0] = IPR_CANCEL_ALL_REQUESTS;

    p_cancel_sis_cmnd->ccb.cmd_len = 10;

    ipr_queue_internal(&ipr_cfg->shared, &p_cancel_sis_cmnd->ccb);

    init_waitqueue_head(&p_cancel_sis_cmnd->wait_q);

    /* Sleep on the response and time it */
    time_left = ipr_sleep_on_timeout(&io_request_lock,
                                        &p_cancel_sis_cmnd->wait_q,
                                        IPR_CANCEL_ALL_TIMEOUT);

    rc = p_cancel_sis_cmnd->ccb.completion;

    if (time_left <= 0)
        p_cancel_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        ipr_trace;
        return FAILED;
    }
    else if (time_left <= 0)
    {
        ipr_dev_loc_str(&ipr_cfg->shared, p_resource_entry, dev_loc_str);

        /* Cancel all timed out - reset the adapter */
        ipr_log_err("cancel all to %s timed out."IPR_EOL, dev_loc_str);
        return ipr_host_reset(p_scsi_cmd);
    }

    /* Re-initialize the command block for re-use */
    ipr_initialize_sis_cmnd(p_cancel_sis_cmnd);

    /* Put this cmd blk back on the pending queue */
    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_cancel_sis_cmnd);

    p_cancel_sis_cmnd->ccb.p_resource = p_resource_entry;

    /* Send the sync-complete */
    p_cancel_sis_cmnd->ccb.cdb[0] = IPR_SYNC_COMPLETE;

    p_cancel_sis_cmnd->ccb.cmd_len = 10;
    p_cancel_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND | IPR_IOA_CMD;

    ipr_queue_internal(&ipr_cfg->shared, &p_cancel_sis_cmnd->ccb);

    init_waitqueue_head(&p_cancel_sis_cmnd->wait_q);

    /* Sleep on the response and time it */
    time_left = ipr_sleep_on_timeout(&io_request_lock,
                                        &p_cancel_sis_cmnd->wait_q,
                                        IPR_INTERNAL_TIMEOUT);

    rc2 = p_cancel_sis_cmnd->ccb.completion;

    if (time_left <= 0)
        p_cancel_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;

    if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
    {
        ipr_trace;
        return FAILED;
    }
    else if (time_left <= 0)
    {
        ipr_dev_loc_str(&ipr_cfg->shared, p_resource_entry, dev_loc_str);

        /* Sync complete timed out - reset the adapter */
        ipr_log_err("sync complete to %s timed out."IPR_EOL,
                       dev_loc_str);
        return ipr_host_reset(p_scsi_cmd);
    }

    ipr_put_sis_cmnd_to_free(ipr_cfg, p_cancel_sis_cmnd);

    LEAVE;

    if ((rc != IPR_RC_SUCCESS) || (rc2 != IPR_RC_SUCCESS))
    {
        ipr_trace;
        return FAILED;
    }
    else
    {
        return SUCCESS;
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Queue an external request
 * Context: Task level only 
 * Lock State: io_request_lock assumed to be held
 * Returns: 0   - Success
 *          1   - Command could not be queued
 *---------------------------------------------------------------------------*/
int ipr_queue(Scsi_Cmnd *p_scsi_cmd, void (*done) (Scsi_Cmnd *))
{
    ipr_host_config *ipr_cfg = NULL;
    u32 found = 0, rc = 0;
    u8 bus;
    u16 lun_num;
    struct ipr_lun *p_lun = NULL;
    struct ipr_resource_entry *p_resource = NULL;
    struct ipr_cmnd *p_sis_cmnd = NULL;
    struct ipr_std_inq_data std_inq_data;

    p_scsi_cmd->scsi_done = done;

    ipr_cfg = (ipr_host_config *)p_scsi_cmd->host->hostdata;

    /* This MUST be done, since retries following aborts do not
     re-initialize this field */
    p_scsi_cmd->result = DID_OK << 16;

    if (ipr_cfg->block_host_ops)
    {
        /* We are currently blocking all devices due to a host reset 
         We have told the host to stop giving us new requests, but
         retries on failed ops don't count. Therefore, we return success and
         forget about the command, allowing the host to enter ERP when
         the command times out */
        ipr_dbg;
        return 0;
    }

    if (ipr_cfg->shared.ioa_is_dead)
    {
        memset (p_scsi_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
        p_scsi_cmd->result = (DID_NO_CONNECT << 16);
        p_scsi_cmd->scsi_done(p_scsi_cmd);
        return 0;
    }

    /* Look to see if device is attached */
    if (p_scsi_cmd->lun < IPR_MAX_NUM_LUNS_PER_TARGET)
    {
        p_lun = ipr_get_lun_scsi(ipr_cfg, p_scsi_cmd);

        if (p_lun && p_lun->is_valid_entry)
        {
            p_resource = p_lun->p_resource_entry;
            found = 1;
        }
    }

    /* Dummy up a connection timeout response since device is not attached
     or we are hiding the device from the mid-layer */
    if ((found == 0) || p_resource->is_hidden)
    {
        /* First check if multi-lun configuration present for
         responding correctly to inquiry */
        if ((p_scsi_cmd->cmnd[0] == INQUIRY) &&
            ((p_scsi_cmd->cmnd[1] & 0x01) == 0))
        {
            /* Since the SCSI mid-layer does not scan for sparse LUNs,
             we must dummy up inquiry data for for them if they exist */

            bus = p_scsi_cmd->channel + 1;

            for (lun_num = p_scsi_cmd->lun;
                 lun_num < IPR_MAX_NUM_LUNS_PER_TARGET;
                 lun_num++)
            {
                if (bus <= IPR_MAX_NUM_BUSES)
                {
                    p_lun = &ipr_cfg->shared.bus[bus].
                        target[p_scsi_cmd->target].lun[lun_num];

                    if (p_lun->is_valid_entry) 
                    {
                        p_resource = p_lun->p_resource_entry;
                        if (!p_resource->is_hidden)
                        {
                            /* Send back fabricated inquiry data
                             so mid-layer processing continues through
                             lun list to find active lun */
                            memcpy(&std_inq_data, &p_resource->std_inq_data,
                                               sizeof(p_resource->std_inq_data));
                            std_inq_data.peri_qual = 0;
                            std_inq_data.peri_dev_type = 0x1f;

                            /* Zero out the serial number */
                            memset(std_inq_data.serial_num, '0',
                                              IPR_SERIAL_NUM_LEN);

                            memcpy(p_scsi_cmd->buffer, &std_inq_data,
                                               sizeof(struct ipr_std_inq_data));
                            p_scsi_cmd->scsi_done(p_scsi_cmd);
                            return 0;
                        }
                    }
                }
            }
        }

        /* No device to send command to */
        memset (p_scsi_cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
        p_scsi_cmd->result = (DID_NO_CONNECT << 16);
        p_scsi_cmd->scsi_done(p_scsi_cmd);
        return 0;
    }
    else if (p_lun->stop_new_requests)
    {
        /* We are currently blocking requests to this device */
        /* Note: The midlayer has an assumption that if we give them
         a QUEUE_FULL response that we have at least one op queued to
         the device. If not, the device queue will lock up. We can
         guarantee that in this path since we are doing ERP for a GPDD
         op and are holding on to the original op */
        p_scsi_cmd->result |= (QUEUE_FULL << 1);
        p_scsi_cmd->scsi_done(p_scsi_cmd);
        ipr_dbg;
        return 0;
    }

    p_sis_cmnd = ipr_build_cmd(ipr_cfg, p_scsi_cmd, p_resource);

    if (p_sis_cmnd != NULL)
        rc = ipr_queue_internal(&ipr_cfg->shared, &p_sis_cmnd->ccb);
    else
    {
        /* We return busy here rather than queue full or host full
         since we cannot guarantee we actually have any ops outstanding
         at this point. We will only hit this path if the DMA mapping
         fails, which should really never happen, but if it does we
         just want a retry */
        p_scsi_cmd->result |= (DID_BUS_BUSY << 16);
        p_scsi_cmd->scsi_done(p_scsi_cmd);
        return 0;
    }

    if (rc != IPR_RC_SUCCESS)
    {
        /* Copy over the sense data and push scsi done */
        /* Change the return code since the command was successfully built
         and sent, but was unsucessfully executed. */
        memcpy(p_scsi_cmd->sense_buffer, p_sis_cmnd->ccb.sense_buffer,
                           IPR_SENSE_BUFFERSIZE);
        p_scsi_cmd->result |= (DID_ERROR << 16);
        p_scsi_cmd->scsi_done(p_scsi_cmd);

        /* Free up sis cmd block */
        ipr_put_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Return the BIOS parameters for fdisk. We want to make sure we
 *          return something that places partitions on 4k boundaries for
 *          best performance with the IOA
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: 0   - Success
 *---------------------------------------------------------------------------*/
int ipr_biosparam(Disk *p_disk, kdev_t kdev, int *parm)
{
    int heads, sectors, cylinders;

    heads = 128;
    sectors = 32;

    cylinders = (p_disk->capacity / (heads * sectors));

    /* return result */
    parm[0] = heads;
    parm[1] = sectors;
    parm[2] = cylinders;

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Our default /proc entry
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: number of bytes in /proc entry
 * Notes: The format of existing entries cannot be changed easily
 *        without breaking userspace tools
 *        We only support reading out of the /proc entry
 *---------------------------------------------------------------------------*/
int ipr_proc_info(char *buffer, char **start, off_t offset,
                     int length, int hostno, int inout)
{
    u32 len = 0;
    u32 size = 0;
    int i;
    ipr_host_config *ipr_cfg;
    char temp_string[15];
    int cache_size;
    char ioa_name[65];
    struct ipr_inquiry_page3 *p_ucode_vpd;
    unsigned long io_flags = 0;

    /* We only support reading of our /proc file */
    if (inout)
        return 0;

    spin_lock_irqsave(&io_request_lock, io_flags);

    for (i = 0, ipr_cfg = ipr_cfg_head;
         ipr_cfg != NULL;
         ipr_cfg = ipr_cfg->p_next, i++)
    {
        if (ipr_cfg->host->host_no == hostno)
            break;
    }

    if (ipr_cfg == NULL)
    {
        spin_unlock_irqrestore(&io_request_lock, io_flags);
        return 0;
    }

    p_ucode_vpd = ipr_cfg->shared.p_ucode_vpd;

    size = sprintf(buffer + len, "IBM %X Disk Array Controller", ipr_cfg->shared.ccin);
    len += size;
    size = sprintf(buffer + len, " \nDriver Version: "IPR_FULL_VERSION);
    len += size;

    size = sprintf(buffer + len, " \nFirmware Version: %02X%02X%02X%02X",
                   p_ucode_vpd->major_release, p_ucode_vpd->card_type,
                   p_ucode_vpd->minor_release[0], p_ucode_vpd->minor_release[1]);

    len += size;
    ipr_get_ioa_name(ipr_cfg, ioa_name);
    size = sprintf(buffer + len, " \nResource Name: %s", ioa_name);
    len += size;
    size = sprintf(buffer + len, " \nMajor Number: %d",
                   ipr_cfg->major_num);
    len += size;
    size = sprintf(buffer + len, " \nMinor Number: %d",
                   ipr_cfg->minor_num);
    len += size;
    size = sprintf(buffer + len, "\nHost Address: %x", ipr_cfg->host->unique_id);
    len += size;
    size = sprintf(buffer + len, " \nSerial Number: ");
    len += size;
    memcpy(buffer + len, ipr_cfg->shared.ioa_resource.serial_num,
                       IPR_SERIAL_NUM_LEN);
    len += IPR_SERIAL_NUM_LEN;
    size = sprintf(buffer + len, " \nCard Part Number: ");
    len += size;
    memcpy(buffer + len, ipr_cfg->shared.p_ioa_vpd->ascii_part_num, 12);
    size += 12;
    size = sprintf(buffer + len, " \nPlant of Manufacture: ");
    len += size;
    memcpy(buffer + len, ipr_cfg->shared.p_ioa_vpd->ascii_plant_code, 4);
    len += 4;
    strncpy(temp_string, ipr_cfg->shared.p_cfc_vpd->cache_size, 3);
    temp_string[3] = '\0';
    cache_size = simple_strtoul(temp_string, NULL, 16);
    size = sprintf(buffer + len, " \nCache Size: %d MB", cache_size);
    len += size;
    size = sprintf(buffer + len, " \nDRAM Size: %d MB", ipr_cfg->shared.dram_size);
    len += size;
    size = sprintf(buffer + len, " \nMain CB: 0x%p", ipr_cfg);
    len += size;
    size = sprintf(buffer + len, " \nPlatform: %s\n", ipr_platform);
    len += size;
    if (ipr_mem_debug)
    {
        size = sprintf(buffer + len, "ipr_kmalloced_mem: %d\n", ipr_kmalloced_mem);
        len += size;
    }

    spin_unlock_irqrestore(&io_request_lock, io_flags);

    return len;
}

/*---------------------------------------------------------------------------
 * Purpose: Select the queue depth.
 * Context: Task level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_select_q_depth(struct Scsi_Host *p_scsi_host,
                           Scsi_Device *p_scsi_device_list)
{
    u8 bus;
    struct ipr_lun *p_lun = NULL;
    ipr_host_config *ipr_cfg;
    struct ipr_resource_entry *p_resource_entry;
    Scsi_Device *p_scsi_device;

    ipr_cfg = (ipr_host_config *) p_scsi_host->hostdata;

    for (p_scsi_device = p_scsi_device_list;
         p_scsi_device != NULL;
         p_scsi_device = p_scsi_device->next)
    {
        bus = (u8)(p_scsi_device->channel + 1);

        if ((bus <= IPR_MAX_NUM_BUSES) &&
            (p_scsi_device->id < IPR_MAX_NUM_TARGETS_PER_BUS) &&
            (p_scsi_device->lun < IPR_MAX_NUM_LUNS_PER_TARGET))
        {
            p_lun = &ipr_cfg->shared.bus[bus].
                target[p_scsi_device->id].lun[p_scsi_device->lun];
        }

        if (p_lun && p_lun->is_valid_entry)
        {
            p_resource_entry = p_lun->p_resource_entry;

            if (ipr_is_vset_device(p_resource_entry))
                p_scsi_device->queue_depth = IPR_MAX_CMD_PER_VSET;
            else
                p_scsi_device->queue_depth = IPR_MAX_CMD_PER_LUN;
        }
    }

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Fails the op back to the caller.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static Scsi_Cmnd *ipr_fail_op(ipr_host_config *ipr_cfg,
                                 struct ipr_cmnd* p_sis_cmnd,
                                 Scsi_Cmnd *p_prev_scsi_cmd)
{
    Scsi_Cmnd *p_scsi_cmd;

    /* Put onto completed queue */
    ipr_put_sis_cmnd_to_completed(ipr_cfg, p_sis_cmnd);

    p_sis_cmnd->ccb.status = 0;
    p_sis_cmnd->ccb.completion = IPR_RC_DID_RESET;

    if (p_sis_cmnd->ccb.flags & IPR_ERP_CMD)
    {
        ipr_end_erp(p_sis_cmnd);

        p_sis_cmnd->p_scsi_cmd = p_sis_cmnd->p_saved_scsi_cmd;
    }

    p_scsi_cmd = p_sis_cmnd->p_scsi_cmd;

    if (p_scsi_cmd != NULL)
    {
        /* Returning DID_ERROR will force the mid-layer to retry the op */
        p_scsi_cmd->result |= (DID_ERROR << 16);

        if (p_prev_scsi_cmd == NULL)
        {
            ipr_cfg->p_scsi_ops_to_fail =  p_scsi_cmd;
            p_prev_scsi_cmd = p_scsi_cmd;
        }
        else
        {
            p_prev_scsi_cmd->SCp.ptr = (char *)p_scsi_cmd;
            p_prev_scsi_cmd = p_scsi_cmd;
        }
        p_scsi_cmd->SCp.ptr = NULL;
    }

    return p_prev_scsi_cmd;
}


/*---------------------------------------------------------------------------
 * Purpose: Fails all outstanding ops
 *          Any host requests that are outstanding are queued up and
 *          the pointer to the head scsi_cmnd is setup in the global
 *          control block. Calling this function does not call scsi_done for
 *          host requests. It is the caller's responsibility to do this at
 *          the appropriate time.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_fail_all_ops(ipr_host_config *ipr_cfg)
{
    struct ipr_cmnd* p_sis_cmnd;
    Scsi_Cmnd *p_prev_scsi_cmd = NULL;

    ENTER;

    /* Fail all ops on the pending queue */
    while((p_sis_cmnd = ipr_cfg->qPendingH))
    {
        ipr_dbg;

        /* Pull off of pending queue */
        ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);

        /* Fail the op back to the caller */
        p_prev_scsi_cmd = ipr_fail_op(ipr_cfg,
                                         p_sis_cmnd,
                                         p_prev_scsi_cmd);
    }

    /* Fail all ops on the error queue */
    while(ipr_cfg->qErrorH)
    {
        ipr_dbg;

        /* Pull off error queue */
        ipr_remove_sis_cmnd_from_error(ipr_cfg, p_sis_cmnd);

        /* Fail the op back to the caller */
        p_prev_scsi_cmd = ipr_fail_op(ipr_cfg,
                                         p_sis_cmnd,
                                         p_prev_scsi_cmd);
    }

    ipr_ops_done(ipr_cfg);

    LEAVE;

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Push all scsi_done functions to return them to the host
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_return_failed_ops(ipr_host_config *ipr_cfg)
{
    Scsi_Cmnd *p_next_cmnd;
    Scsi_Cmnd *p_scsi_cmnd = ipr_cfg->p_scsi_ops_to_fail;

    ENTER;

    for (; p_scsi_cmnd != NULL; p_scsi_cmnd = p_next_cmnd)
    {
        p_next_cmnd = (Scsi_Cmnd *)p_scsi_cmnd->SCp.ptr;
        p_scsi_cmnd->scsi_done(p_scsi_cmnd);
    }

    ipr_cfg->p_scsi_ops_to_fail = NULL;

    LEAVE;

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Shutdown/Reboot notification
 * Context: Task level only
 * Lock State: No locks assumed to be held
 * Returns: NOTIFY_DONE - Success
 *---------------------------------------------------------------------------*/
static int ipr_notify_sys(struct notifier_block *this, unsigned long code,
                             void *unused)
{
    ipr_host_config *ipr_cfg = ipr_cfg_head;
    unsigned long io_flags = 0;
    u32 rc = 0;
    DECLARE_WAIT_QUEUE_HEAD(internal_wait);
    DECLARE_COMPLETION(completion);

    ENTER;

    spin_lock_irqsave(&io_request_lock, io_flags);

    /* Loop through all the devices and issue prepare for shutdowns to them all */
    while(ipr_cfg != NULL)
    {
        if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        {
            while(1)
            {
                /* Loop forever waiting for IOA to come out of reset/reload, checking once a second */
                if ((ipr_cfg->flag & IPR_IN_RESET_RELOAD) == 0)
                    break;
                else 
                    ipr_sleep_on_timeout(&io_request_lock, &internal_wait, HZ);
            }
        }

        ipr_shutdown_ioa(ipr_cfg, IPR_SHUTDOWN_PREPARE_FOR_NORMAL);

        ipr_cfg = ipr_cfg->p_next;
    }

    ipr_cfg = ipr_cfg_head;

    /* Loop through all the devices and issue shutdowns to them all */
    while(ipr_cfg != NULL)
    {
        if (ipr_cfg->flag & IPR_IN_RESET_RELOAD)
        {
            while(1)
            {
                /* Loop forever waiting for IOA to come out of reset/reload, checking once a second */
                if ((ipr_cfg->flag & IPR_IN_RESET_RELOAD) == 0)
                    break;
                else 
                    ipr_sleep_on_timeout(&io_request_lock, &internal_wait, HZ);
            }
        }

        rc = ipr_shutdown_ioa(ipr_cfg, IPR_SHUTDOWN_NORMAL);

        if (rc)
        {
            /* Delay for 5 seconds - this is to display an error message on the console
             regarding not getting a clean shutdown to the IOA */
            ipr_sleep(5000);
        }

        ipr_mask_interrupts(&ipr_cfg->shared);

        /* Here we reset the IOA to get it back in a POR state */
        ipr_ioa_reset(ipr_cfg, IPR_IRQ_DISABLED);

        ipr_cfg = ipr_cfg->p_next;
    }

    spin_unlock_irqrestore(&io_request_lock,io_flags);

    LEAVE;

    return NOTIFY_DONE;
}

/*---------------------------------------------------------------------------
 * Purpose: Issues an IOA Shutdown command to the given IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Shutdown failed
 *          IPR_RC_QUAL_SUCCESS      - Qualified success
 *          IPR_RC_TIMEOUT           - Shutdown timed out
 *---------------------------------------------------------------------------*/
static u32 ipr_shutdown_ioa(ipr_host_config *ipr_cfg,
                               enum ipr_shutdown_type type)
{
    u32 rc = IPR_RC_SUCCESS;
    struct ipr_cmnd *p_sis_cmnd;
    u8 *p_sense_buffer;
    u32 timeout;
    signed long time_left;
    bool error_log = false;

    ENTER;

    ipr_cfg->flag &= ~(IPR_ALLOW_HCAMS | IPR_ALLOW_REQUESTS);

    if (type != IPR_SHUTDOWN_NONE)
    {
        if (!ipr_cfg->shared.ioa_operational ||
            !ipr_cfg->shared.allow_interrupts ||
            ipr_cfg->shared.ioa_is_dead)
            return IPR_RC_QUAL_SUCCESS;

        p_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

        p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

        p_sis_cmnd->ccb.p_resource = &ipr_cfg->shared.ioa_resource;

        p_sis_cmnd->ccb.cmd_len = 10;

        p_sis_cmnd->ccb.cdb[0] = IPR_IOA_SHUTDOWN;

        if (type == IPR_SHUTDOWN_ABBREV)
        {
            p_sis_cmnd->ccb.cdb[1] = 0x80;
            timeout = IPR_ABBREV_SHUTDOWN_TIMEOUT;
        }
        else if (type == IPR_SHUTDOWN_PREPARE_FOR_NORMAL)
        {
            p_sis_cmnd->ccb.cdb[1] = 0x40;
            timeout = IPR_SHUTDOWN_TIMEOUT;
        }
        else
        {
            timeout = IPR_SHUTDOWN_TIMEOUT;
        }

        p_sis_cmnd->ccb.data_direction = IPR_DATA_NONE;

        p_sis_cmnd->ccb.flags = IPR_BLOCKING_COMMAND;

        ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

        rc = ipr_ioa_queue(&ipr_cfg->shared, &p_sis_cmnd->ccb);

        if (rc == IPR_RC_OP_NOT_SENT)
        {
            ipr_trace;
            return IPR_RC_FAILED;
        }

        init_waitqueue_head(&p_sis_cmnd->wait_q);

        time_left = ipr_sleep_on_timeout(&io_request_lock, &p_sis_cmnd->wait_q, timeout);

        if (time_left <= 0)
        {
            /* The op timed out. The IOA still "owns" the command block, therefore
             we can't free it and can't do retries */
            p_sis_cmnd->ccb.flags |= IPR_TIMED_OUT;
            rc = IPR_RC_TIMEOUT;
            ipr_mask_interrupts(&ipr_cfg->shared);
        }
        else
        {
            /* We have a valid sense buffer */
            if (ipr_sense_valid(p_sense_buffer[0]) &&
                sense_error(p_sense_buffer[2]) == 0)
            {
                /* Op completed successfully with qualified success */
                rc = IPR_RC_QUAL_SUCCESS;
            }
            else
            {
                rc = p_sis_cmnd->ccb.completion;
            }
        }

        if (rc != IPR_RC_SUCCESS)
        {
            error_log = true;

            if (rc == IPR_RC_TIMEOUT)
            {
                ipr_beg_err(KERN_WARNING);
                ipr_log_warn("Shutdown to IOA timed out."IPR_EOL);
            }
            else if (rc == IPR_RC_QUAL_SUCCESS)
            {
                ipr_beg_err(KERN_WARNING);
                ipr_log_warn("Shutdown to IOA did not complete successfully."IPR_EOL);
            }
            else if (sense_error(p_sense_buffer[2]) != 0x05)
            {
                ipr_beg_err(KERN_WARNING);
                ipr_log_warn("Shutdown to IOA failed with RC: 0x%X."IPR_EOL, rc);
                ipr_log_warn("SK: 0x%02X, SC: 0x%02X, SQ: 0x%02X"IPR_EOL,
                                sense_error(p_sense_buffer[2]), p_sense_buffer[12], p_sense_buffer[13]);
            }
            else
            {
                error_log = false;
            }

            if (error_log)
            {
                ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_WARNING);
                ipr_end_err(KERN_WARNING)
            }
        }

        if (rc != IPR_RC_TIMEOUT)
            ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
    }

    LEAVE;
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Op done function for an IOCTL
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_ioctl_cmd_done(struct ipr_shared_config *p_shared_cfg,
                                  struct ipr_ccb *p_sis_ccb)
{
    struct ipr_cmnd *p_sis_cmnd;

    p_sis_cmnd = (struct ipr_cmnd *)p_sis_ccb;

    p_sis_cmnd->ccb.flags |= IPR_FINISHED;

    wake_up(&p_sis_cmnd->wait_q);

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Sends an internal request to the requested resource and sleeps
 *          until a response is received or is timed out.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Shutdown failed
 *          IPR_RC_QUAL_SUCCESS      - Qualified success
 *---------------------------------------------------------------------------*/
static u32 ipr_send_blocking_ioctl(ipr_host_config *ipr_cfg,
                                    struct ipr_cmnd *p_sis_cmnd,
                                    u32 timeout,
                                    u8 retries)
{
    u32 rc = IPR_RC_SUCCESS;
    u8 *p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;
    u16 flags = p_sis_cmnd->ccb.flags;

    for (;retries; retries--)
    {
        p_sis_cmnd->ccb.flags = flags;

        p_sis_cmnd->ccb.completion = IPR_RC_SUCCESS;
        p_sis_cmnd->ccb.status = 0;

        memset(p_sense_buffer, 0, IPR_SENSE_BUFFERSIZE);

        init_waitqueue_head(&p_sis_cmnd->wait_q);

        rc = ipr_do_req(&ipr_cfg->shared, &p_sis_cmnd->ccb,
                           ipr_ioctl_cmd_done, timeout/HZ);

        if (rc)
        {
            ipr_dbg_trace;
            return IPR_RC_FAILED;
        }

        ipr_sleep_on(&io_request_lock, &p_sis_cmnd->wait_q);

        if (p_sis_cmnd->ccb.completion == IPR_RC_DID_RESET)
        {
            ipr_dbg_trace;
            return IPR_RC_FAILED;
        }

        if (p_sis_cmnd->ccb.completion != IPR_RC_SUCCESS)
        {
            /* We have a valid sense buffer */
            if (ipr_sense_valid(p_sense_buffer[0]) &&
                sense_error(p_sense_buffer[2]) == 0)
            {
                /* Op completed successfully with qualified success */
                rc = IPR_RC_QUAL_SUCCESS;
                break;
            }
            else
            {
                ipr_dbg_trace;
                rc = IPR_RC_FAILED;
            }
        }
        else
            break;
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Send an HCAM to the IOA
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *---------------------------------------------------------------------------*/
u32 ipr_send_hcam(struct ipr_shared_config *p_shared_cfg, u8 type,
                     struct ipr_hostrcb *p_hostrcb)
{
    struct ipr_cmnd *p_sis_cmnd;
    ipr_host_config *ipr_cfg = (ipr_host_config *)p_shared_cfg;

    if (ipr_cfg->flag & IPR_ALLOW_HCAMS)
    {
        p_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);
        ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);
        p_sis_cmnd->ccb.p_resource = &ipr_cfg->shared.ioa_resource;
        p_sis_cmnd->ccb.cdb[0] = IPR_HOST_CONTROLLED_ASYNC;
        p_sis_cmnd->ccb.cdb[1] = type;
        p_sis_cmnd->ccb.cdb[7] = (sizeof(struct ipr_hostrcb) >> 8) & 0xff;
        p_sis_cmnd->ccb.cdb[8] = sizeof(struct ipr_hostrcb) & 0xff;
        p_sis_cmnd->ccb.buffer = p_hostrcb;
        p_sis_cmnd->ccb.buffer_dma = ipr_get_hcam_dma_addr(ipr_cfg, p_hostrcb);
        p_sis_cmnd->ccb.bufflen = sizeof(struct ipr_hostrcb);
        p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
        p_sis_cmnd->ccb.cmd_len = 10;
        p_sis_cmnd->ccb.use_sg = 1;
        p_sis_cmnd->ccb.sglist[0].address = (u32)p_sis_cmnd->ccb.buffer_dma;
        p_sis_cmnd->ccb.sglist[0].length = sizeof(struct ipr_hostrcb);

        ipr_ioa_queue(&ipr_cfg->shared, &p_sis_cmnd->ccb);
    }
    return IPR_RC_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Free all allocated resources
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_free_all_resources(ipr_host_config *ipr_cfg,
                                      int free_reboot_notif, int free_chrdev)
{
    struct pci_dev *pdev;
    int i;

    pdev = ipr_cfg->pdev;

    ENTER;

    spin_unlock_irq(&io_request_lock);
    free_irq (ipr_cfg->host->irq, ipr_cfg);
    spin_lock_irq(&io_request_lock);

    ipr_kfree(ipr_cfg->shared.resource_entry_list,
                 sizeof(struct ipr_resource_dll) * IPR_MAX_PHYSICAL_DEVS);
    ipr_dma_free(&ipr_cfg->shared, sizeof(struct ipr_vpd_cbs),
                    ipr_cfg->shared.p_vpd_cbs,
                    ipr_cfg->shared.ioa_vpd_dma);

    ipr_kfree(ipr_cfg->shared.p_page_28,
                 sizeof(struct ipr_page_28_data));

    ipr_dma_free(&ipr_cfg->shared, sizeof(struct ipr_element_desc_page) * IPR_MAX_NUM_BUSES,
                    ipr_cfg->shared.p_ses_data[0], ipr_cfg->shared.ses_data_dma[0]);

    if (ipr_cfg->sis_cmnd_list[0])
    {
        /* Free sense buffers and command blocks */
        ipr_dma_free(&ipr_cfg->shared, IPR_SENSE_BUFFERSIZE * IPR_NUM_CMD_BLKS,
                        ipr_cfg->sis_cmnd_list[0]->ccb.sense_buffer,
                        ipr_cfg->sis_cmnd_list[0]->ccb.sense_buffer_dma);
    }

    for (i=0; i < IPR_NUM_CMD_BLKS; i++)
        ipr_kfree(ipr_cfg->sis_cmnd_list[i],
                     sizeof(struct ipr_cmnd));

    ipr_dma_free(&ipr_cfg->shared, sizeof(struct ipr_hostrcb) * IPR_NUM_HCAMS,
                    ipr_cfg->hostrcb[0], ipr_cfg->hostrcb_dma[0]);

    if (ipr_cfg->p_ioa_cfg->sdt_reg_sel_size == IPR_SDT_REG_SEL_SIZE_1BYTE)
    {
        iounmap((void *)ipr_cfg->shared.hdw_bar_addr[3]);
        release_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[3],
                           pci_resource_len(pdev, 3));

        iounmap((void *)ipr_cfg->shared.hdw_bar_addr[2]);
        release_mem_region(ipr_cfg->shared.hdw_bar_addr_pci[2],
                           pci_resource_len(pdev, 2));
    }

    iounmap((void *)ipr_cfg->shared.hdw_dma_regs);
    release_mem_region(ipr_cfg->shared.hdw_dma_regs_pci,
                       pci_resource_len(pdev,ipr_cfg->p_ioa_cfg->bar_index));

    if (free_chrdev)
        devfs_unregister_chrdev(ipr_cfg->major_num, "ipr");

    ipr_kfree(ipr_cfg->shared.p_location, sizeof(struct ipr_location_data));
    ipr_free_mem(&ipr_cfg->shared);

    if (free_reboot_notif)
    {
        unregister_reboot_notifier(&ipr_notifier);
        unregister_ioctl32_conversion(IPR_IOCTL_SEND_COMMAND);
    }

    scsi_unregister(ipr_cfg->host);

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Log Data Handler
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_handle_log_data(ipr_host_config *ipr_cfg,
                                   struct ipr_hostrcb *p_hostrcb)
{
    u32 error_index;
    u16 ccin = ipr_cfg->shared.ccin;
    int i, j;
    struct ipr_resource_entry *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;
    u16 device_ccin;
    char error_buffer[100];
    int size = 0;
    int len = 0;
    char temp_ccin[5];
    u32 errors_logged;
    struct ipr_hostrcb_device_data_entry *p_dev_entry;
    struct ipr_hostrcb_array_data_entry *p_array_entry;
    int ioa_data_len;
    u16 urc;
    enum ipr_error_class err_class;
    u32 class_index, ioasc;
    char *printk_level;
    char error_string[100];
    u8 service_level = 0;
    struct ipr_inquiry_page3 *p_ucode_vpd;
    u8 *p_end_data;

    temp_ccin[4] = '\0';

    if (p_hostrcb->notificationType == IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY)
    {
        ioasc = sistoh32(p_hostrcb->data.error.failing_dev_ioasc);

        if ((ioasc == IPR_IOASC_BUS_WAS_RESET) ||
            (ioasc == IPR_IOASC_BUS_WAS_RESET_BY_OTHER))
        {
            /* Tell the midlayer we had a bus reset so it will handle the UA properly */
            scsi_report_bus_reset(ipr_cfg->host,
                                  p_hostrcb->data.error.failing_dev_res_addr.bus);
        }

        error_index = ipr_get_error(ioasc);

        err_class = ipr_error_table[error_index].err_class;

        for (class_index = 0; class_index < IPR_ERR_CLASS_MAX_CLASS; class_index++)
            if (err_class == ipr_error_class_table[class_index].err_class)
                break;

        printk_level = ipr_error_class_table[class_index].printk_level;

        if (error_index == 0)
        {
            /* IOASC was not found in the table */
            ipr_beg_err(printk_level);
            ipr_hcam_log("SRC: unknown");
            ipr_hcam_log("Class: %s", ipr_error_class_table[class_index].p_class);
            ipr_hcam_log("%s", ipr_error_table[error_index].p_error);
            strncpy(error_buffer,
                    ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num,
                    sizeof(ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num));
            error_buffer[sizeof(ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num)] = '\0';
            ipr_hcam_log("IOA Serial Number: %s", error_buffer);

            ipr_get_ioa_name(ipr_cfg, error_buffer);
            ipr_hcam_log("IOA is %s", error_buffer);

            ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, printk_level);
        }
        else if (p_hostrcb->data.error.failing_dev_resource_handle == IPR_IOA_RESOURCE_HANDLE)
        {
            urc = ipr_adjust_urc(error_index,
                                    p_hostrcb->data.error.failing_dev_res_addr,
                                    ioasc,
                                    0,
                                    error_string);
            if (urc == 0) 
                return;

            /* If this is an 8151 with this specific PRC and this is a 2780, it probably
             needs a code download - lets eat the error and try to bring up the IOA again */
            if ((urc == 0x8151) && (sistoh32(p_hostrcb->data.error.prc) == 0x14006262u) &&
                !ipr_cfg->shared.needs_download &&
                (ipr_cfg->shared.vendor_id == PCI_VENDOR_ID_IBM) &&
                (ipr_cfg->shared.device_id == PCI_DEVICE_ID_IBM_SNIPE) &&
                (ipr_cfg->shared.subsystem_id == IPR_SUBS_DEV_ID_2780))
            {
                ipr_cfg->shared.needs_download = 1;
                return;
            }

            /* If we are currently running in the wrong mode, ignore 9001 errors */
            if (ipr_cfg->shared.nr_ioa_microcode && (urc == 0x9001))
                return;

            ipr_beg_err(printk_level);
            ipr_hcam_log("SRC: %X %04X", ccin, urc);
            ipr_hcam_log("Class: %s", ipr_error_class_table[class_index].p_class);
            ipr_hcam_log("%s", error_string);

            strncpy(error_buffer,
                    ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num,
                    sizeof(ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num));
            error_buffer[sizeof(ipr_cfg->shared.p_ioa_vpd->std_inq_data.serial_num)] = '\0';
            ipr_hcam_log("IOA Serial Number: %s", error_buffer);

            if ((p_hostrcb->data.error.failing_dev_res_addr.bus <
                 IPR_MAX_NUM_BUSES) &&
                (p_hostrcb->data.error.failing_dev_res_addr.target <
                 IPR_MAX_NUM_TARGETS_PER_BUS) &&
                (p_hostrcb->data.error.failing_dev_res_addr.lun <
                 IPR_MAX_NUM_LUNS_PER_TARGET))
            {
                ipr_log_dev_physical_location(&ipr_cfg->shared,
                                                 p_hostrcb->data.error.failing_dev_res_addr,
                                                 printk_level);
            }
            else
            {
                ipr_log_ioa_physical_location(ipr_cfg->shared.p_location,
                                                 printk_level);
            }
        }
        else
        {
            urc = ipr_adjust_urc(error_index,
                                    p_hostrcb->data.error.failing_dev_res_addr,
                                    ioasc,
                                    1,
                                    error_string);

            if (urc == 0)
                return;

            ipr_beg_err(printk_level);

            device_ccin = 0x6600;
            service_level = 1;

            /* Loop through config table to find device */
            for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
                 p_resource_dll != NULL;
                 p_resource_dll = p_resource_dll->next)
            {
                p_resource_entry = &p_resource_dll->data;

                if (p_resource_entry->resource_handle ==
                    p_hostrcb->data.error.failing_dev_resource_handle)
                {
                    device_ccin = p_resource_entry->type;
                    service_level = p_resource_entry->level;
                    break;
                }
            }

            ipr_hcam_log("SRC: %04X %04X", device_ccin, urc);
            ipr_hcam_log("Class: %s", ipr_error_class_table[class_index].p_class);
            ipr_hcam_log("%s", error_string);

            ipr_log_dev_physical_location(&ipr_cfg->shared,
                                             p_hostrcb->data.error.failing_dev_res_addr,
                                             printk_level);

            if (p_resource_entry != NULL)
            {
                ipr_hcam_log("Device Serial Number: %s", p_resource_entry->serial_num);
                ipr_log_dev_vpd(p_resource_entry, printk_level);
                if (p_resource_entry->is_af && (device_ccin != 0x6600))
                    ipr_hcam_log("Device Service Level: %X", service_level);
            }
        }

        ipr_hcam_log("IOASC: 0x%08X", ioasc);
        ipr_hcam_log("PRC: 0x%08X", sistoh32(p_hostrcb->data.error.prc));

        p_ucode_vpd = ipr_cfg->shared.p_ucode_vpd;

        ipr_hcam_log("Driver version: "IPR_FULL_VERSION);
        ipr_hcam_log("IOA Firmware version: %02X%02X%02X%02X",
                        p_ucode_vpd->major_release, p_ucode_vpd->card_type,
                        p_ucode_vpd->minor_release[0], p_ucode_vpd->minor_release[1]);
        ipr_hcam_log("IOA revision id: %d", ipr_cfg->shared.chip_rev_id);

        switch (p_hostrcb->overlayId)
        {
            case IPR_HOST_RCB_OVERLAY_ID_1:
                ipr_hcam_log("Predictive Analysis Seeks/256 counter: %d",
                                sistoh32(p_hostrcb->data.error.data.type_01_error.seek_counter));
                ipr_hcam_log("Predictive Analysis Sectors Read/256 counter: %d",
                                sistoh32(p_hostrcb->data.error.data.type_01_error.read_counter));

                size = len = 0;
                for (i = 0; i < 32; i++)
                {
                    size = sprintf(error_buffer + len, "%02X ",
                                   p_hostrcb->data.error.data.type_01_error.sense_data[i]);
                     len += size;
                }
                error_buffer[len] = '\0';
                ipr_hcam_log("SCSI Sense Data: %s", error_buffer);

                ioa_data_len = sistoh32(p_hostrcb->length )-
                    ((u8 *)&p_hostrcb->data.error.data.type_01_error.ioa_data -
                     (u8 *)&p_hostrcb->data.error);

                if (ioa_data_len == 0)
                {
                    ipr_end_err(printk_level);
                    break;
                }
                ipr_hcam_log("IOA Error Data:");
                ipr_hcam_log("Offset              0 1 2 3  4 5 6 7  8 9 A B  C D E F");

                /* We print out the hex so we get 4 words per line */
                for (i = 0; i < ioa_data_len/4; i += 4)
                {
                    len = size = 0;
                    for (j = 0; (j < 4) && ((i + j) < ioa_data_len/4); j++)
                    {
                        size = sprintf(error_buffer + len, "%08X ",
                                       sistoh32(p_hostrcb->data.error.data.type_01_error.ioa_data[i + j]));
                        len += size;
                    }

                    printk("%s"IPR_ERR": %08X            %s"IPR_EOL, printk_level, (i * 4),
                           error_buffer);
                }
                ipr_end_err(printk_level);
                break;
            case IPR_HOST_RCB_OVERLAY_ID_2:

                ipr_hcam_log("Current Configuration:");
                ipr_hcam_log("  I/O Processor Information:");
                ipr_print_ioa_vpd(&p_hostrcb->data.error.data.type_02_error.ioa_vpids, printk_level);
                strncpy(error_buffer,
                        p_hostrcb->data.error.data.type_02_error.ioa_sn,
                        sizeof(p_hostrcb->data.error.data.type_02_error.ioa_sn));
                error_buffer[sizeof(p_hostrcb->data.error.data.type_02_error.ioa_sn)] = '\0';
                ipr_hcam_log("   Serial Number: %s", error_buffer);

                ipr_hcam_log("  Cache Adapter Card Information:");
                ipr_print_ioa_vpd(&p_hostrcb->data.error.data.type_02_error.cfc_vpids, printk_level);
                strncpy(error_buffer,
                        p_hostrcb->data.error.data.type_02_error.cfc_sn,
                        sizeof(p_hostrcb->data.error.data.type_02_error.cfc_sn));
                error_buffer[sizeof(p_hostrcb->data.error.data.type_02_error.cfc_sn)] = '\0';
                ipr_hcam_log("   Serial Number: %s", error_buffer);

                ipr_hcam_log("Expected Configuration:");
                ipr_hcam_log("  I/O Processor Information:");
                ipr_print_ioa_vpd(&p_hostrcb->data.error.data.type_02_error.ioa_last_attached_to_cfc_vpids,
                                     printk_level);

                strncpy(error_buffer,
                        p_hostrcb->data.error.data.type_02_error.ioa_last_attached_to_cfc_sn,
                        sizeof(p_hostrcb->data.error.data.type_02_error.ioa_last_attached_to_cfc_sn));
                error_buffer[sizeof(p_hostrcb->data.error.data.type_02_error.ioa_last_attached_to_cfc_sn)] = '\0';
                ipr_hcam_log("   Serial Number: %s", error_buffer);

                ipr_hcam_log("  Cache Adapter Card Information:");
                ipr_print_ioa_vpd(&p_hostrcb->data.error.data.type_02_error.cfc_last_attached_to_ioa_vpids,
                                     printk_level);

                strncpy(error_buffer,
                        p_hostrcb->data.error.data.type_02_error.cfc_last_attached_to_ioa_sn,
                        sizeof(p_hostrcb->data.error.data.type_02_error.cfc_last_attached_to_ioa_sn));
                error_buffer[sizeof(p_hostrcb->data.error.data.type_02_error.cfc_last_attached_to_ioa_sn)] = '\0';
                ipr_hcam_log("   Serial Number: %s", error_buffer);

                ipr_hcam_log("Additional IOA Data: %08X %08X %08X",
                                sistoh32(p_hostrcb->data.error.data.type_02_error.ioa_data[0]),
                                sistoh32(p_hostrcb->data.error.data.type_02_error.ioa_data[1]),
                                sistoh32(p_hostrcb->data.error.data.type_02_error.ioa_data[2]));
                ipr_end_err(printk_level);
                break;
            case IPR_HOST_RCB_OVERLAY_ID_3:

                ipr_hcam_log("Device Errors Detected: %d",
                                sistoh32(p_hostrcb->data.error.data.type_03_error.errors_detected));

                errors_logged = sistoh32(p_hostrcb->data.error.data.type_03_error.errors_logged);
                ipr_hcam_log("Device Errors Logged: %d", errors_logged);

                for (i = 0, p_dev_entry = p_hostrcb->data.error.data.type_03_error.dev_entry;
                     i < errors_logged; i++, p_dev_entry++)
                {
                    ipr_err_separator;
                    ipr_hcam_log("Device %d:", i+1);

                    if (p_dev_entry->dev_res_addr.bus >= IPR_MAX_NUM_BUSES)
                    {
                        ipr_print_unknown_dev_phys_loc(printk_level);
                    }
                    else
                    {
                        ipr_log_dev_physical_location(&ipr_cfg->shared,
                                                         p_dev_entry->dev_res_addr,
                                                         printk_level);
                    }


                    ipr_log_array_dev_vpd(&p_dev_entry->dev_vpids, "6600", printk_level);
                    strncpy(error_buffer, p_dev_entry->dev_sn, sizeof(p_dev_entry->dev_sn));
                    error_buffer[sizeof(p_dev_entry->dev_sn)] = '\0';
                    ipr_hcam_log("    Serial Number: %s", error_buffer);
                    ipr_hcam_log(" New Device Information:");

                    ipr_log_array_dev_vpd(&p_dev_entry->new_dev_vpids, "****", printk_level);

                    strncpy(error_buffer, p_dev_entry->new_dev_sn, sizeof(p_dev_entry->new_dev_sn));
                    error_buffer[sizeof(p_dev_entry->new_dev_sn)] = '\0';
                    ipr_hcam_log("    Serial Number: %s", error_buffer);

                    ipr_hcam_log(" I/O Processor Information:");
                    ipr_print_ioa_vpd(&p_dev_entry->ioa_last_with_dev_vpids, printk_level);
                    strncpy(error_buffer,p_dev_entry->ioa_last_with_dev_sn,
                            sizeof(p_dev_entry->ioa_last_with_dev_sn));
                    error_buffer[sizeof(p_dev_entry->ioa_last_with_dev_sn)] = '\0';
                    ipr_hcam_log("    Serial Number: %s", error_buffer);

                    ipr_hcam_log(" Cache Adapter Card Information:");
                    ipr_print_ioa_vpd(&p_dev_entry->cfc_last_with_dev_vpids, printk_level);

                    strncpy(error_buffer,p_dev_entry->cfc_last_with_dev_sn,
                            sizeof(p_dev_entry->cfc_last_with_dev_sn));
                    error_buffer[sizeof(p_dev_entry->cfc_last_with_dev_sn)] = '\0';
                    ipr_hcam_log("    Serial Number: %s", error_buffer);
                    ipr_hcam_log(" Additional IOA Data: %08X %08X %08X %08X %08X",
                                  sistoh32(p_dev_entry->ioa_data[0]), sistoh32(p_dev_entry->ioa_data[1]),
                                  sistoh32(p_dev_entry->ioa_data[2]), sistoh32(p_dev_entry->ioa_data[3]),
                                  sistoh32(p_dev_entry->ioa_data[4]));
                }
                ipr_end_err(printk_level);
                break;
            case IPR_HOST_RCB_OVERLAY_ID_4:
            case IPR_HOST_RCB_OVERLAY_ID_6:
                ipr_err_separator;
                p_end_data = (u8 *)&p_hostrcb->data.error + sistoh32(p_hostrcb->length);

                for (i = 0,
                     p_array_entry = p_hostrcb->data.error.data.type_04_error.array_member;
                     ((i < 18) && ((unsigned long)p_array_entry < (unsigned long)p_end_data));
                     (++i == 10) ?
                     p_array_entry = p_hostrcb->data.error.data.type_04_error.array_member2:
                     p_array_entry++)
                {
                    strncpy(error_buffer, p_array_entry->serial_num,
                            sizeof(p_array_entry->serial_num));
                    error_buffer[sizeof(p_array_entry->serial_num)] = '\0';

                    if (!strcmp(error_buffer, "00000000"))
                        continue;

                    ipr_hcam_log("Array Member %d:", i);
                    ipr_log_array_dev_vpd(&p_array_entry->vpids, "****", printk_level);
                    ipr_hcam_log("   Serial Number: %s", error_buffer);

                    ipr_log_dev_current_expected_locations(ipr_cfg,
                                                              p_array_entry->dev_res_addr,
                                                              p_array_entry->expected_dev_res_addr,
                                                              printk_level);

                    ipr_err_separator;
                }

                ipr_end_err(printk_level);
                break; 
            case IPR_HOST_RCB_OVERLAY_ID_DEFAULT:
                ipr_hcam_log("IOA Error Data:");
                ipr_hcam_log("Offset              0 1 2 3  4 5 6 7  8 9 A B  C D E F");

                ioa_data_len = sistoh32(p_hostrcb->length )-
                    ((u8 *)&p_hostrcb->data.error.data.type_ff_error.ioa_data -
                     (u8 *)&p_hostrcb->data.error);

                /* We print out the hex they way we do so we get 4 words per line */
                for (i = 0; i < ioa_data_len/4; i += 4)
                {
                    len = size = 0;
                    for (j = 0; (j < 4) && ((i + j) < ioa_data_len/4); j++)
                    {
                        size = sprintf(error_buffer + len, "%08X ",
                                       sistoh32(p_hostrcb->data.error.data.type_ff_error.ioa_data[i + j]));
                        len += size;
                    }
                    ipr_hcam_log("%08X            %s", (i * 4), error_buffer);
                }
                ipr_end_err(printk_level);
                break;
            default:
                break;
        }
    }
    else /* Informational */
    {
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Returns index of ioasc in error structure
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: index       - index of ioasc into error table
 *---------------------------------------------------------------------------*/
static u32 ipr_get_error(u32 ioasc)
{
    int i;

    for (i = 0; i < sizeof(ipr_error_table)/sizeof(struct ipr_error_table_t); i++)
        if (ipr_error_table[i].ioasc == ioasc)
            return i;

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: IOCTL interface 
 * Context: Task level only
 * Lock State: No locks assumed to be held
 * Returns: 0           - Success
 *          -ENXIO      - No such file or device
 *          -EINVAL     - Invalid parameter
 *          -ENOMEM     - Out of memory
 *          others
 *---------------------------------------------------------------------------*/
static int ipr_ioctl(struct inode *inode, struct file *filp,
                            unsigned int cmd, unsigned long arg)
{
    int rc = IPR_RC_SUCCESS;
    ipr_host_config *ipr_cfg;
    char cmnd[MAX_COMMAND_SIZE];
    struct ipr_ioctl_cmd_type2 ioa_cmd;
    int result = 0;
    u32 timeout, length, dsa, ua, frame_id;
    u8 *p_sense_buffer, *p_buffer;
    struct scatterlist *scatterlist;
    struct ipr_dnload_sglist *p_scatterlist;
    struct ipr_cmnd *p_sis_cmnd, *p_loop_cmnd;
    struct ipr_res_addr res_addr;
    int i, j;
    struct ipr_resource_dll *p_resource_dll;
    struct ipr_resource_entry *p_resource, *p_loop_resource;
    void  *p_cmd_buffer;
    ipr_dma_addr p_cmd_buffer_dma;
    u32 bus_num, target_num, lun_num;
    u32 *trace_block_address, trace_block_length;
    struct ipr_lun lun;
    struct ipr_resource_hdr *p_resource_hdr;
    u32 num_entries;
    int copy_length;
    unsigned long io_flags = 0;
    void  *p_page_28_data;
    struct ipr_mode_parm_hdr *p_mode_parm_header;
    struct ipr_mode_page_28_header *p_modepage_28_header;
    int dev_entry_length, mode_data_length;
    struct ipr_mode_page_28_scsi_dev_bus_attr *p_dev_bus_entry;
    struct ipr_driver_cfg *p_driver_cfg;
    char *p_char;
    u8 *p_defect_list_hdr;
    u32 resource_handle, evaluate_quiece_time;
    struct ipr_lun *p_lun;
    struct ipr_dump_ioa_entry *p_dump_ioa_entry;
    struct ipr_dump_driver_header *p_dump_driver_header;
    int offline_dump = 0;

    /* Have we been opened? */
    if (filp->private_data == NULL)
        return -ENXIO;

    ipr_cfg = filp->private_data;

    memset(cmnd, 0, sizeof(char) * MAX_COMMAND_SIZE);

    /* If this is not the IOCTL we expect, fail it */
    if (cmd != IPR_IOCTL_SEND_COMMAND)
        return -EINVAL;

    if ((result = copy_from_user(&ioa_cmd, (const void *)arg, sizeof(struct ipr_ioctl_cmd_type2))))
    {
        ipr_log_err("Copy from user failed"IPR_EOL);
        return result;
    }

    /* Sanity check the callers command block */
    if (ioa_cmd.type != IPR_IOCTL_TYPE_2)
    {
        ipr_log_err("Invalid or deprecated ioctl type. Please update your utilities."IPR_EOL);
        return -EINVAL;
    }

    /* The user's data buffer immediately follows the command block */
    p_buffer = (u8 *)(arg + sizeof(struct ipr_ioctl_cmd_type2));
    cmd = ioa_cmd.cdb[0];

    if (ioa_cmd.device_cmd)
    {
        bus_num = (u8)(ioa_cmd.resource_address.bus + 1);
        target_num = ioa_cmd.resource_address.target;
        lun_num = ioa_cmd.resource_address.lun;

        if ((bus_num < (IPR_MAX_NUM_BUSES + 1)) &&
            (target_num < IPR_MAX_NUM_TARGETS_PER_BUS) &&
            (lun_num < IPR_MAX_NUM_LUNS_PER_TARGET))
        {
            lun = ipr_cfg->shared.bus[bus_num].target[target_num].lun[lun_num];

            if (lun.is_valid_entry)
            {
                p_resource = lun.p_resource_entry;
            }
            else
            {
                ipr_log_err("Invalid resource address: 00%02X%02X%02X"IPR_EOL,
                               ioa_cmd.resource_address.bus,
                               ioa_cmd.resource_address.target,
                               ioa_cmd.resource_address.lun);
                return -EINVAL;
            }
        }
        else
        {
            ipr_log_err("Invalid resource address: 00%02X%02X%02X"IPR_EOL,
                           ioa_cmd.resource_address.bus,
                           ioa_cmd.resource_address.target,
                           ioa_cmd.resource_address.lun);
            return -EINVAL;
        }
    }
    else
    {
        p_resource = &ipr_cfg->shared.ioa_resource;
    }

    if (ioa_cmd.driver_cmd)
    {
        switch (cmd)
        {
            case IPR_DUMP_IOA:
                if (ioa_cmd.buffer_len < IPR_MIN_DUMP_SIZE)
                {
                    ipr_log_err("Invalid buffer length on dump ioa %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    result = -EINVAL;
                }
                else if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on dump ioa failed"IPR_EOL);
                }
                else
                {
                    p_dump_ioa_entry = ipr_kcalloc(sizeof(struct ipr_dump_ioa_entry),
                                                      IPR_ALLOC_CAN_SLEEP);

                    if (p_dump_ioa_entry == NULL)
                    {
                        ipr_log_err("Dump memory allocation failed"IPR_EOL);
                        result = -ENOMEM;
                        break;
                    }

                    p_dump_driver_header = ipr_kcalloc(sizeof(struct ipr_dump_driver_header),
                                                          IPR_ALLOC_CAN_SLEEP);

                    if (p_dump_driver_header == NULL)
                    {
                        ipr_log_err("Dump memory allocation failed"IPR_EOL);
                        result = -ENOMEM;
                        ipr_kfree(p_dump_ioa_entry,
                                     sizeof(struct ipr_dump_ioa_entry));
                        break;
                    }

                    spin_lock_irqsave(&io_request_lock, io_flags);

                    if (INACTIVE != ipr_get_sdt_state)
                    {
                        ipr_log_err("Invalid request, dump ioa already active"IPR_EOL);
                        result = -EIO;
                    }
                    else
                    {
                        p_ipr_dump_ioa_entry = p_dump_ioa_entry;
                        p_ipr_dump_driver_header = p_dump_driver_header;

                        /* check for offline adapters requiring dump to be
                         taken */
                        for (ipr_cfg = ipr_cfg_head;
                             ipr_cfg != NULL;
                             ipr_cfg = ipr_cfg->p_next)
                        {
                            if ((ipr_cfg->shared.ioa_is_dead) &&
                                !(ipr_cfg->shared.offline_dump))
                                
                            {
                                ipr_get_ioa_smart_dump(ipr_cfg);
                                ipr_cfg->shared.offline_dump = 1;
                                offline_dump = 1;
                            }
                        }


                        if (!offline_dump)
                        {
                            init_waitqueue_head(&ipr_sdt_wait_q);

                            ipr_get_sdt_state = WAIT_FOR_DUMP;

                            while (1)
                            {
                                ipr_interruptible_sleep_on(&io_request_lock, &ipr_sdt_wait_q);

                                if (signal_pending(current))
                                {
                                    if (GET_DUMP == ipr_get_sdt_state)
                                    {
                                        /* Simply flush the signal and go back to sleep if we are
                                         currently getting a dump */
                                        md_flush_signals();
                                    }
                                    else
                                        break;
                                }
                                else
                                    break;
                            }
                        }

                        spin_unlock_irqrestore(&io_request_lock, io_flags);

                        copy_length = IPR_MIN(ioa_cmd.buffer_len,
                                                 sistoh32(p_ipr_dump_driver_header->
                                                          header.total_length));

                        result = ipr_copy_sdt_to_user(p_buffer, copy_length);

                        spin_lock_irqsave(&io_request_lock, io_flags);

                        p_ipr_dump_ioa_entry = NULL;
                        p_ipr_dump_driver_header = NULL;

                        ipr_get_sdt_state = INACTIVE;
                    }

                    ipr_kfree(p_dump_ioa_entry,
                                 sizeof(struct ipr_dump_ioa_entry));
                    ipr_kfree(p_dump_driver_header,
                                 sizeof(struct ipr_dump_driver_header));

                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                }
                break;
            case IPR_RESET_HOST_ADAPTER:
                spin_lock_irqsave(&io_request_lock, io_flags);

                ipr_block_all_requests(ipr_cfg);

                if (ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NORMAL) != SUCCESS)
                    result = -EIO;

                ipr_unblock_all_requests(ipr_cfg);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_READ_DRIVER_CFG:
                if (ioa_cmd.buffer_len < sizeof(struct ipr_driver_cfg))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on driver config %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on 0x%02X failed"IPR_EOL, cmd);
                    break;
                }

                p_driver_cfg = ipr_kmalloc(ioa_cmd.buffer_len, IPR_ALLOC_CAN_SLEEP);

                if (p_driver_cfg == NULL)
                {
                    ipr_log_err("Buffer allocation for driver config failed"IPR_EOL);
                    return -ENOMEM;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);
                p_driver_cfg->debug_level = ipr_cfg->shared.debug_level;
                p_driver_cfg->trace_level = ipr_cfg->shared.trace;
                p_driver_cfg->debug_level_max = IPR_ADVANCED_DEBUG;
                p_driver_cfg->trace_level_max = IPR_TRACE;
                spin_unlock_irqrestore(&io_request_lock, io_flags);
                result = copy_to_user(p_buffer, p_driver_cfg,
                                      ioa_cmd.buffer_len);

                ipr_kfree(p_driver_cfg, ioa_cmd.buffer_len);
                break;
            case IPR_WRITE_DRIVER_CFG:
                if (ioa_cmd.buffer_len < sizeof(struct ipr_driver_cfg))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on driver config %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                /* First we need to copy the user's buffer into kernel memory */
                p_driver_cfg = ipr_kmalloc(ioa_cmd.buffer_len, IPR_ALLOC_CAN_SLEEP);

                if (p_driver_cfg == NULL)
                {
                    ipr_log_err("Buffer allocation for driver config failed"IPR_EOL);
                    return -ENOMEM;
                }

                result = copy_from_user(p_driver_cfg, p_buffer, ioa_cmd.buffer_len);

                if (result)
                {
                    ipr_log_err("Unable to access user data"IPR_EOL);
                    ipr_kfree(p_driver_cfg, ioa_cmd.buffer_len);
                    return result;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);
                ipr_cfg->shared.debug_level = p_driver_cfg->debug_level;
                ipr_cfg->shared.trace = p_driver_cfg->trace_level;
                spin_unlock_irqrestore(&io_request_lock, io_flags);

                ipr_kfree(p_driver_cfg, ioa_cmd.buffer_len);
                break;
            case IPR_MODE_SENSE_PAGE_28:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                if ((ioa_cmd.buffer_len < sizeof(struct ipr_page_28)) ||
                    (ioa_cmd.buffer_len > 0xff))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on mode sense %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on 0x%02X failed"IPR_EOL, cmd);
                    break;
                }

                p_page_28_data = ipr_kmalloc(sizeof(struct ipr_page_28), IPR_ALLOC_CAN_SLEEP);

                spin_lock_irqsave(&io_request_lock, io_flags);
                if ((ioa_cmd.cdb[2] >> 6) == IPR_CURRENT_PAGE)
                {
                    memcpy(p_page_28_data,
                                       &ipr_cfg->shared.p_page_28->saved,
                                       sizeof(struct ipr_page_28));
                }
                else if ((ioa_cmd.cdb[2] >> 6) == IPR_CHANGEABLE_PAGE)
                {
                    memcpy(p_page_28_data,
                                       &ipr_cfg->shared.p_page_28->changeable,
                                       sizeof(struct ipr_page_28));
                }
                else if ((ioa_cmd.cdb[2] >> 6) == IPR_DEFAULT_PAGE)
                {
                    memcpy(p_page_28_data,
                                       &ipr_cfg->shared.p_page_28->dflt,
                                       sizeof(struct ipr_page_28));
                }
                else
                {
                    /* invalid request, return canned sense data? */
                    ipr_log_err("Invalid Page Control Field for Mode Sense Page 28"IPR_EOL);
                    result = -EINVAL;
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }
                spin_unlock_irqrestore(&io_request_lock, io_flags);

                /* return  parms for mode page 28 */
                result = copy_to_user(p_buffer,
                                      p_page_28_data,
                                      ioa_cmd.buffer_len);

                ipr_kfree(p_page_28_data, sizeof(struct ipr_page_28));
                break;
            case IPR_MODE_SELECT_PAGE_28:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                if ((ioa_cmd.buffer_len < sizeof(struct ipr_page_28)) ||
                    (ioa_cmd.buffer_len > 0xff))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on mode select %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                /* First we need to copy the user's buffer into kernel memory
                 so it can't get swapped out */
                p_page_28_data = ipr_kmalloc(ioa_cmd.buffer_len, IPR_ALLOC_CAN_SLEEP);

                if (p_page_28_data == NULL)
                {
                    ipr_log_err("Buffer allocation for mode select failed"IPR_EOL);
                    return -ENOMEM;
                }

                result = copy_from_user(p_page_28_data, p_buffer, ioa_cmd.buffer_len);

                if (result)
                {
                    ipr_log_err("Unable to access user data"IPR_EOL);
                    ipr_kfree(p_page_28_data, ioa_cmd.buffer_len);
                    return result;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 IPR_MODE_SENSE_28_SZ,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for mode sense failed"IPR_EOL);
                    break;
                }

                /* need to get the mode sense data */
                spin_lock_irqsave(&io_request_lock, io_flags);

                /* put usr settings to page 28 saved */
                p_mode_parm_header = (struct ipr_mode_parm_hdr *) p_page_28_data;
                p_modepage_28_header = (struct ipr_mode_page_28_header *) (p_mode_parm_header + 1);

                dev_entry_length = sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

                /* Point to first device bus entry */
                p_dev_bus_entry = (struct ipr_mode_page_28_scsi_dev_bus_attr *)
                    (p_modepage_28_header + 1);

                for (i = 0;
                     (i < p_modepage_28_header->num_dev_entries) &&
                         (i < IPR_MAX_NUM_BUSES);
                     i++)
                {
                    for (j = 0;
                         j < ipr_cfg->shared.p_page_28->saved.page_hdr.num_dev_entries;
                         j++)
                    {
                        if (p_dev_bus_entry->res_addr.bus ==
                            ipr_cfg->shared.p_page_28->saved.attr[j].res_addr.bus)
                        {
                            /* verify  max_xfer_rate does not exceed any restrictions */
                            if (sistoh32(p_dev_bus_entry->max_xfer_rate) >
                                sistoh32(ipr_cfg->shared.p_page_28->dflt.attr[j].max_xfer_rate))
                            {
                                ipr_log_err("Bus max transfer rate set higher than allowed, "
                                               "resetting to max allowable"IPR_EOL);
                                p_dev_bus_entry->max_xfer_rate =
                                    ipr_cfg->shared.p_page_28->dflt.attr[j].max_xfer_rate;
                            }

                            /* found saved bus entry, copy to send mode select */
                            memcpy(&ipr_cfg->shared.p_page_28->saved.attr[j],
                                               p_dev_bus_entry,
                                               sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr));
                            break;
                        }
                    }
                    /* Point to next device bus entry */
                    p_dev_bus_entry = (struct ipr_mode_page_28_scsi_dev_bus_attr *)
                        ((char *)p_dev_bus_entry + dev_entry_length);
                }

                ipr_kfree(p_page_28_data, ioa_cmd.buffer_len);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, IPR_MODE_SENSE_28_SZ,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = IPR_MODE_SENSE_28_SZ;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = IPR_MODE_SENSE;
                p_sis_cmnd->ccb.cdb[2] = IPR_PAGE_CODE_28;
                p_sis_cmnd->ccb.cdb[4] = IPR_MODE_SENSE_28_SZ;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_INTERNAL_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    if ((sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST)) ||
                        (ipr_cfg->shared.nr_ioa_microcode))
                        result = -EINVAL;
                    else
                    {
                        ipr_print_sense(cmd, p_sense_buffer);
                        result = -EIO;
                    }
                }

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                if (result)
                {
                    if (result != -EINVAL)
                        ipr_log_err("Mode Sense for Page 28 failed"IPR_EOL);
                    ipr_dma_free(&ipr_cfg->shared, IPR_MODE_SENSE_28_SZ,
                                    p_cmd_buffer, p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                /* put users new data into mode select */
                p_mode_parm_header = (struct ipr_mode_parm_hdr *) p_cmd_buffer;
                p_modepage_28_header = (struct ipr_mode_page_28_header *) (p_mode_parm_header + 1);

                for (bus_num = 0;
                     bus_num < IPR_MAX_NUM_BUSES;
                     bus_num++)
                {
                    /* Modify the IOAFP's Mode page 28 for specified
                     SCSI bus */
                    ipr_modify_ioafp_mode_page_28(&ipr_cfg->shared,
                                                     p_modepage_28_header,
                                                     bus_num);
                }

                /* Determine the amount of data to transfer on the Mode
                 Select */
                /* Note: Need to add 1 since the length field does not
                 include itself. */
                mode_data_length = p_mode_parm_header->length + 1;

                /* Zero length field */
                p_mode_parm_header->length = 0;

                /* send mode select */
                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, IPR_MODE_SENSE_28_SZ,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    return result;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = mode_data_length;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = IPR_MODE_SELECT;
                p_sis_cmnd->ccb.cdb[1] = 0x11;
                p_sis_cmnd->ccb.cdb[4] = mode_data_length;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                                IPR_INTERNAL_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                } 

                ipr_dma_free(&ipr_cfg->shared, IPR_MODE_SENSE_28_SZ,
                                p_cmd_buffer,p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_GET_TRACE:
                if (ioa_cmd.buffer_len <= 0)
                {
                    ipr_log_err("Invalid buffer length on get trace %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    result = -EINVAL;
                }
                else if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on get trace failed"IPR_EOL);
                    break;
                }
                else
                {
                    spin_lock_irqsave(&io_request_lock, io_flags);

                    /* internal trace table entry */
                    ipr_get_internal_trace(&ipr_cfg->shared,
                                              &trace_block_address,
                                              &trace_block_length);

                    spin_unlock_irqrestore(&io_request_lock, io_flags);

                    p_cmd_buffer = ipr_kmalloc(trace_block_length, IPR_ALLOC_CAN_SLEEP);

                    if (p_cmd_buffer == NULL)
                    {
                        result = -ENOMEM;
                        break;
                    }

                    spin_lock_irqsave(&io_request_lock, io_flags);

                    memcpy(p_cmd_buffer, trace_block_address, trace_block_length);

                    spin_unlock_irqrestore(&io_request_lock, io_flags);

                    result = copy_to_user(p_buffer, p_cmd_buffer,
                                          IPR_MIN(ioa_cmd.buffer_len, trace_block_length));

                    ipr_kfree(p_cmd_buffer, trace_block_length);
                }
                break;
            case IPR_RESET_DEV_CHANGED:
                bus_num = (u8)(ioa_cmd.resource_address.bus + 1);
                target_num = ioa_cmd.resource_address.target;
                lun_num = ioa_cmd.resource_address.lun;

                if ((bus_num < (IPR_MAX_NUM_BUSES + 1)) &&
                    (target_num < IPR_MAX_NUM_TARGETS_PER_BUS) &&
                    (lun_num < IPR_MAX_NUM_LUNS_PER_TARGET))
                {
                    spin_lock_irqsave(&io_request_lock, io_flags);

                    lun = ipr_cfg->shared.bus[bus_num].target[target_num].lun[lun_num];

                    if (lun.is_valid_entry)
                    {
                        p_resource = lun.p_resource_entry;
                        p_resource->dev_changed = 0;
                    }
                    else
                    {
                        ipr_log_err("Invalid resource address: 00%02X%02X%02X"IPR_EOL,
                                       ioa_cmd.resource_address.bus,
                                       ioa_cmd.resource_address.target,
                                       ioa_cmd.resource_address.lun);
                        result = -ENXIO;
                    }

                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                }
                else
                {
                    ipr_log_err("Invalid resource address: 00%02X%02X%02X"IPR_EOL,
                                   ioa_cmd.resource_address.bus,
                                   ioa_cmd.resource_address.target,
                                   ioa_cmd.resource_address.lun);
                    return -ENXIO;
                }
                break;
            case IPR_CONC_MAINT:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                spin_lock_irqsave(&io_request_lock, io_flags);

                /* Find the resource address for the device the concurrent maintenance
                 action is directed at */
                if (IPR_CONC_MAINT_GET_FMT(ioa_cmd.cdb[2]) == IPR_CONC_MAINT_FRAME_ID_FMT)
                {
                    frame_id = (ioa_cmd.cdb[4] << 8) | ioa_cmd.cdb[5];

                    result = ipr_get_res_addr_fmt1(&ipr_cfg->shared,
                                                      frame_id, &ioa_cmd.cdb[6],
                                                      &res_addr);

                    if (result)
                    {
                        spin_unlock_irqrestore(&io_request_lock, io_flags);
                        break;
                    }
                }
                else if (IPR_CONC_MAINT_GET_FMT(ioa_cmd.cdb[2]) == IPR_CONC_MAINT_DSA_FMT)
                {
                    dsa = (ioa_cmd.cdb[4] << 24) | (ioa_cmd.cdb[5] << 16) |
                        (ioa_cmd.cdb[6] << 8) | ioa_cmd.cdb[7];
                    ua = (ioa_cmd.cdb[8] << 24) | (ioa_cmd.cdb[9] << 16) |
                        (ioa_cmd.cdb[10] << 8) | ioa_cmd.cdb[11];

                    result = ipr_get_res_addr_fmt0(&ipr_cfg->shared,
                                                      dsa, ua, &res_addr);

                    if (result)
                    {
                        spin_unlock_irqrestore(&io_request_lock, io_flags);
                        break;
                    }
                }
                else if (IPR_CONC_MAINT_GET_FMT(ioa_cmd.cdb[2]) == IPR_CONC_MAINT_PSERIES_FMT)
                {
                    if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > IPR_MAX_PSERIES_LOCATION_LEN))
                    {
                        result = -EINVAL;
                        spin_unlock_irqrestore(&io_request_lock, io_flags);
                        break;
                    }

                    spin_unlock_irqrestore(&io_request_lock, io_flags);

                    /* First we need to copy the user's buffer into kernel memory
                     so we can access it */
                    p_cmd_buffer = ipr_kmalloc(ioa_cmd.buffer_len+1, IPR_ALLOC_CAN_SLEEP);

                    if (p_cmd_buffer == NULL)
                    {
                        ipr_log_err("Buffer allocation for concurrent maintenance failed"IPR_EOL);
                        result = -ENOMEM;
                        break;
                    }

                    result = copy_from_user(p_cmd_buffer, p_buffer, ioa_cmd.buffer_len);

                    p_char = p_cmd_buffer;
                    p_char[ioa_cmd.buffer_len] = '\0';

                    if (result)
                    {
                        ipr_log_err("Unable to access user data"IPR_EOL);
                        ipr_kfree(p_cmd_buffer, ioa_cmd.buffer_len+1);
                        result = -ENOMEM;
                        break;
                    }

                    spin_lock_irqsave(&io_request_lock, io_flags);

                    result = ipr_get_res_addr_fmt2(&ipr_cfg->shared,
                                                      p_cmd_buffer, &res_addr);

                    ipr_kfree(p_cmd_buffer, ioa_cmd.buffer_len+1);

                    if (result)
                    {
                        spin_unlock_irqrestore(&io_request_lock, io_flags);
                        break;
                    }
                }
                else if (IPR_CONC_MAINT_GET_FMT(ioa_cmd.cdb[2]) == IPR_CONC_MAINT_XSERIES_FMT)
                {
                    result = ipr_get_res_addr_fmt3(&ipr_cfg->shared,
                                                      (ioa_cmd.cdb[4] << 8) | ioa_cmd.cdb[5],
                                                      (ioa_cmd.cdb[6] << 8) | ioa_cmd.cdb[7],
                                                      ioa_cmd.cdb[8], ioa_cmd.cdb[9], ioa_cmd.cdb[10],
                                                      &res_addr);

                    if (result)
                    {
                        result = -EINVAL;
                        spin_unlock_irqrestore(&io_request_lock, io_flags);
                        break;
                    }
                }
                else
                {
                    result = -EINVAL;
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                if (ioa_cmd.cdb[1] == IPR_CONC_MAINT_CHECK_ONLY)
                {
                    result = ipr_suspend_device_bus(ipr_cfg, res_addr, IPR_SDB_CHECK_ONLY);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                result = ipr_conc_maint(ipr_cfg,
                                           res_addr,
                                           IPR_CONC_MAINT_GET_TYPE(ioa_cmd.cdb[2]),
                                           ioa_cmd.cdb[12]);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            default:
                result = -EINVAL;
                ipr_log_err("Invalid driver command issued: 0x%02X"IPR_EOL, cmd);
                break;
        }
    }
    else
    {
        switch (cmd)
        {
            case FORMAT_UNIT:
                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 IPR_DEFECT_LIST_HDR_LEN,
                                                 &p_cmd_buffer_dma, IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for 0x%02X failed"IPR_EOL, cmd);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                p_defect_list_hdr = p_cmd_buffer;

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, IPR_DEFECT_LIST_HDR_LEN,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.cmd_len = 6;
                p_sis_cmnd->ccb.cdb[0] = FORMAT_UNIT;
                p_sis_cmnd->ccb.cdb[4] = 1;

                if (ipr_cfg->shared.use_immed_format)
                {
                    p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                    p_sis_cmnd->ccb.cdb[1] = IPR_FORMAT_DATA;
                    p_defect_list_hdr[1] = IPR_FORMAT_IMMED;
                    p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                    p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                    p_sis_cmnd->ccb.bufflen = IPR_DEFECT_LIST_HDR_LEN;
                    p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                }
                else
                    p_sis_cmnd->ccb.sc_data_direction = IPR_DATA_NONE;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd, IPR_FORMAT_UNIT_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                }

                ipr_dma_free(&ipr_cfg->shared, IPR_DEFECT_LIST_HDR_LEN,
                                p_cmd_buffer,p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_EVALUATE_DEVICE:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                resource_handle =
                    htosis32((ioa_cmd.cdb[2] << 24) |
                             (ioa_cmd.cdb[3] << 16) |
                             (ioa_cmd.cdb[4] << 8) |
                             ioa_cmd.cdb[5]);

                /* Block any new requests from getting to us while we delete this device */
                ipr_block_midlayer_requests(ipr_cfg);

                /* Find resource entry we are trying to delete */
                for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
                     p_resource_dll != NULL;
                     p_resource_dll = p_resource_dll->next)
                {
                    p_loop_resource = &p_resource_dll->data;

                    if (p_loop_resource->resource_handle == resource_handle)
                    {
                        p_lun = ipr_get_lun_res_addr(ipr_cfg,
                                                        p_loop_resource->resource_address);

                        if (p_lun && p_lun->is_valid_entry)
                        {
                            ipr_dbg_trace;

                            if (p_loop_resource->in_init)
                                p_loop_resource->redo_init = 1;

                            evaluate_quiece_time = 600;

                            /* Look to see if we have any ops in flight */
                            while(evaluate_quiece_time)
                            {
                                p_loop_cmnd = ipr_cfg->qPendingH;

                                /* Look for the op on the pending queue */
                                while (p_loop_cmnd != NULL)
                                {
                                    if (p_loop_cmnd->ccb.p_resource == p_loop_resource)
                                    {
                                        ipr_dbg_err("Waiting for command to resource 0x%p"IPR_EOL,
                                                       p_loop_cmnd);
                                        ipr_sleep(100);
                                        evaluate_quiece_time--;
                                        break;
                                    }
                                    p_loop_cmnd = p_loop_cmnd->p_next;
                                }

                                /* Couldn't find it - Lets look on the completed queue */
                                if (p_loop_cmnd == NULL)
                                {
                                    p_loop_cmnd = ipr_cfg->qCompletedH;

                                    while (p_loop_cmnd != NULL)
                                    {
                                        if (p_loop_cmnd->ccb.p_resource == p_loop_resource)
                                        {
                                            ipr_dbg_err("Waiting for command to resource 0x%p"IPR_EOL,
                                                           p_loop_cmnd);
                                            ipr_sleep(100);
                                            evaluate_quiece_time--;
                                            break;
                                        }
                                        p_loop_cmnd = p_loop_cmnd->p_next;
                                    }
                                }
                                else
                                    continue;

                                /* Couldn't find it - Lets look on the error queue */
                                if (p_loop_cmnd == NULL)
                                {
                                    p_loop_cmnd = ipr_cfg->qErrorH;

                                    while (p_loop_cmnd != NULL)
                                    {
                                        if (p_loop_cmnd->ccb.p_resource == p_loop_resource)
                                        {
                                            ipr_dbg_err("Waiting for command to resource 0x%p"IPR_EOL,
                                                           p_loop_cmnd);
                                            ipr_sleep(100);
                                            evaluate_quiece_time--;
                                            break;
                                        }
                                        p_loop_cmnd = p_loop_cmnd->p_next;
                                    }
                                }
                                else
                                    continue;

                                break;;
                            }

                            if (evaluate_quiece_time == 0)
                            {
                                result = -EBUSY;
                                ipr_log_err("Timed out waiting for ops to quiesce "
                                               "for evaluate device capabilities"IPR_EOL);
                                goto evaluate_leave;
                            }   
                        }
                        break;
                    }
                }

                if (p_resource_dll == NULL)
                {
                    result = -EINVAL;
                    goto evaluate_leave;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_NONE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = IPR_EVALUATE_DEVICE;
                p_sis_cmnd->ccb.cdb[2] = ioa_cmd.cdb[2];
                p_sis_cmnd->ccb.cdb[3] = ioa_cmd.cdb[3];
                p_sis_cmnd->ccb.cdb[4] = ioa_cmd.cdb[4];
                p_sis_cmnd->ccb.cdb[5] = ioa_cmd.cdb[5];
                p_sis_cmnd->ccb.flags = IPR_IOA_CMD;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_EVALUATE_DEVICE_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                    {
                        ipr_print_sense(cmd, p_sense_buffer);
                        result = -EIO;
                    }
                }

evaluate_leave:
                ipr_unblock_midlayer_requests(ipr_cfg);
                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case START_STOP:
                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_NONE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[4] = ioa_cmd.cdb[4];

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_START_STOP_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                }

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_QUERY_COMMAND_STATUS:
            case IPR_QUERY_RESOURCE_STATE:
            case IPR_QUERY_ARRAY_CONFIG:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on 0x%02X failed"IPR_EOL, cmd);
                    break;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for 0x%02X failed"IPR_EOL, cmd);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = ioa_cmd.cdb[1];
                p_sis_cmnd->ccb.cdb[2] = ioa_cmd.cdb[2];
                p_sis_cmnd->ccb.cdb[7] = (ioa_cmd.buffer_len & 0xff00) >> 8;
                p_sis_cmnd->ccb.cdb[8] = ioa_cmd.buffer_len & 0xff;
                p_sis_cmnd->ccb.flags = IPR_IOA_CMD | IPR_BUFFER_MAPPED;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_ARRAY_CMD_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    if ((sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST)) ||
                        (ipr_cfg->shared.nr_ioa_microcode))
                        result = -EINVAL;
                    else
                    {
                        ipr_print_sense(cmd, p_sense_buffer);
                        result = -EIO;
                    }
                }
                else
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len);
                    spin_lock_irqsave(&io_request_lock, io_flags);
                }

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer,p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case MODE_SENSE:
                if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > 0xff))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on mode sense %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on mode sense failed"IPR_EOL);
                    break;
                }

                /* Block mode sense request to the IOA focal point */
                if (p_resource == &ipr_cfg->shared.ioa_resource)
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid mode sense command"IPR_EOL)
                        break;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for mode sense failed"IPR_EOL);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[2] = ioa_cmd.cdb[2];
                p_sis_cmnd->ccb.cdb[4] = ioa_cmd.buffer_len;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_INTERNAL_DEV_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                }
                else
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len);
                    spin_lock_irqsave(&io_request_lock, io_flags);
                }

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer, p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_IOA_DEBUG:
                if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > IPR_IOA_DEBUG_MAX_XFER))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on IOA debug %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on IOA debug failed"IPR_EOL);
                    break;
                }

                /* Make sure we are going to the IOA focal point */
                if (p_resource != &ipr_cfg->shared.ioa_resource)
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid IOA debug command"IPR_EOL);
                    break;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for IOA debug failed"IPR_EOL);
                    break;
                }

                if (!ioa_cmd.read_not_write)
                {
                    result = copy_from_user(p_cmd_buffer, p_buffer, ioa_cmd.buffer_len);
                    if (result)
                    {
                        ipr_log_err("Unable to access user data"IPR_EOL);
                        ipr_kfree(p_cmd_buffer, ioa_cmd.buffer_len);
                        break;
                    }
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_IOA_CMD | IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                if (ioa_cmd.read_not_write)
                    p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                else
                    p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = IPR_CDB_LEN;
                memcpy(p_sis_cmnd->ccb.cdb, ioa_cmd.cdb, IPR_CDB_LEN);

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_INTERNAL_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                }
                else if (ioa_cmd.read_not_write)
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len);
                    spin_lock_irqsave(&io_request_lock, io_flags);
                }

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer, p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case MODE_SELECT:
                if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > 0xff))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on mode select %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                /* First we need to copy the user's buffer into kernel memory
                 so it can't get swapped out */
                p_cmd_buffer = ipr_dma_malloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    ipr_log_err("Buffer allocation for mode select failed"IPR_EOL);
                    return -ENOMEM;
                }

                result = copy_from_user(p_cmd_buffer, p_buffer, ioa_cmd.buffer_len);

                if (result)
                {
                    ipr_log_err("Unable to access user data"IPR_EOL);
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    return result;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    return result;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = 0x10;
                p_sis_cmnd->ccb.cdb[4] = ioa_cmd.buffer_len;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_INTERNAL_DEV_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                } 

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer,p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case READ_CAPACITY:
            case IPR_SERVICE_ACTION_IN:
            case INQUIRY:
            case LOG_SENSE:
                if ((ioa_cmd.buffer_len == 0) ||
                    (ioa_cmd.buffer_len > 0xffff) ||
                    ((ioa_cmd.buffer_len > 0xff) && (cmd != LOG_SENSE)))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on 0x%X command %d"IPR_EOL,
                                   cmd, ioa_cmd.buffer_len);
                    break;
                }

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on 0x%X command failed"IPR_EOL, cmd);
                    break;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for 0x%X failed"IPR_EOL, cmd);
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer, p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    return result;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                p_sis_cmnd->ccb.cmd_len = COMMAND_SIZE(cmd);
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = ioa_cmd.cdb[1];
                p_sis_cmnd->ccb.cdb[2] = ioa_cmd.cdb[2];

                if (cmd == INQUIRY)
                    p_sis_cmnd->ccb.cdb[4] = ioa_cmd.buffer_len;
                else if (cmd == LOG_SENSE)
                {
                    p_sis_cmnd->ccb.cdb[7] = ioa_cmd.buffer_len >> 8;
                    p_sis_cmnd->ccb.cdb[8] = ioa_cmd.buffer_len & 0xff;
                }

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_INTERNAL_DEV_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                } 
                else
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len);
                    spin_lock_irqsave(&io_request_lock, io_flags);
                }

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer, p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_START_ARRAY_PROTECTION:
            case IPR_STOP_ARRAY_PROTECTION:
            case IPR_REBUILD_DEVICE_DATA:
            case IPR_ADD_ARRAY_DEVICE:
            case IPR_RESYNC_ARRAY_PROTECTION:
                if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > sizeof(struct ipr_array_query_data)))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on %02X: %d"IPR_EOL,
                                   cmd, ioa_cmd.buffer_len);
                    break;
                }

                p_cmd_buffer = ipr_dma_malloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for %02X failed"IPR_EOL, cmd);
                    break;
                }

                result = copy_from_user(p_cmd_buffer, p_buffer, ioa_cmd.buffer_len);

                spin_lock_irqsave(&io_request_lock, io_flags);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    ipr_log_err("copy_from_user for %02X failed"IPR_EOL, cmd);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer,p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    return result;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.flags = IPR_IOA_CMD | IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = ioa_cmd.cdb[1];
                p_sis_cmnd->ccb.cdb[4] = ioa_cmd.cdb[4];
                p_sis_cmnd->ccb.cdb[5] = ioa_cmd.cdb[5];
                p_sis_cmnd->ccb.cdb[6] = ioa_cmd.cdb[6];
                p_sis_cmnd->ccb.cdb[7] = (ioa_cmd.buffer_len & 0xff00) >> 8;
                p_sis_cmnd->ccb.cdb[8] = ioa_cmd.buffer_len & 0xff;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_ARRAY_CMD_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                } 

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer,p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_QUERY_IOA_CONFIG:
                spin_lock_irqsave(&io_request_lock, io_flags);

                num_entries = 1;

                for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
                     p_resource_dll != NULL;
                     p_resource_dll = p_resource_dll->next)
                {
                    if (!p_resource_dll->data.is_ioa_resource)
                        num_entries++;
                }

                length = (num_entries *
                          sizeof(struct ipr_resource_entry)) + sizeof(struct ipr_resource_hdr);

                if (ioa_cmd.buffer_len < length)
                {
                    ipr_log_err("Invalid buffer length size in query IOA config. %d, %d"IPR_EOL,
                                   ioa_cmd.buffer_len, length);
                    result = -EINVAL;
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                }
                else
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);

                    if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                    {
                        ipr_log_err("verify_area on query IOA config failed"IPR_EOL);
                    }
                    else
                    {
                        p_resource_hdr = ipr_kmalloc(length, IPR_ALLOC_CAN_SLEEP);

                        if (p_resource_hdr == NULL)
                        {
                            ipr_log_err("Buffer allocation on query IOA config failed"IPR_EOL);
                            result = -ENOMEM;
                        }
                        else
                        {
                            p_resource_hdr->num_entries = num_entries;

                            copy_length = sizeof(struct ipr_resource_hdr);

                            spin_lock_irqsave(&io_request_lock, io_flags);

                            memcpy(((u8*)p_resource_hdr) + copy_length,
                                               &ipr_cfg->shared.ioa_resource,
                                               sizeof(struct ipr_resource_entry));

                            copy_length += sizeof(struct ipr_resource_entry);

                            for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
                                 p_resource_dll != NULL;
                                 p_resource_dll = p_resource_dll->next)
                            {
                                /* Ignore IOA resource since it is already copied over */
                                if (p_resource_dll->data.is_ioa_resource)
                                    continue;

                                memcpy(((u8*)p_resource_hdr) + copy_length,
                                                   &p_resource_dll->data,
                                                   sizeof(struct ipr_resource_entry));

                                copy_length += sizeof(struct ipr_resource_entry);
                            }

                            spin_unlock_irqrestore(&io_request_lock, io_flags);

                            result = copy_to_user(p_buffer, p_resource_hdr, length);

                            ipr_kfree(p_resource_hdr, length);
                        }
                    }
                }
                break;
            case IPR_RECLAIM_CACHE_STORE:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                if ((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > sizeof(struct ipr_reclaim_query_data)))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on reclaim cache storage: %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                if ((ioa_cmd.cdb[1] & IPR_RECLAIM_ACTION) == IPR_RECLAIM_PERFORM)
                    timeout = IPR_RECLAIM_TIMEOUT;
                else
                    timeout = IPR_INTERNAL_TIMEOUT;

                if ((result = verify_area(VERIFY_WRITE, p_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_log_err("verify_area on reclaim cache storage failed"IPR_EOL);
                    break;
                }

                p_cmd_buffer = ipr_dma_calloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for reclaim cache storage failed"IPR_EOL);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer, p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    break;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = ioa_cmd.cdb[1];
                p_sis_cmnd->ccb.cdb[7] = (ioa_cmd.buffer_len & 0xff00) >> 8;
                p_sis_cmnd->ccb.cdb[8] = ioa_cmd.buffer_len & 0xff;
                p_sis_cmnd->ccb.flags = IPR_IOA_CMD | IPR_BUFFER_MAPPED;

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd, timeout, 1);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    if ((sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST)) ||
                        (ipr_cfg->shared.nr_ioa_microcode))
                        result = -EINVAL;
                    else
                    {
                        ipr_print_sense(cmd, p_sense_buffer);
                        result = -EIO;
                    }
                }
                else
                {
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len);
                    spin_lock_irqsave(&io_request_lock, io_flags);
                }

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer, p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                if (timeout == IPR_RECLAIM_TIMEOUT)
                {
                    ipr_block_all_requests(ipr_cfg);

                    if (ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NORMAL) != SUCCESS)
                        result = -EIO;

                    ipr_unblock_all_requests(ipr_cfg);
                }

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case WRITE_BUFFER:
                if((ioa_cmd.buffer_len == 0) || (ioa_cmd.buffer_len > IPR_MAX_WRITE_BUFFER_SIZE))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length size in write buffer. %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                /* First we need to copy the user's buffer into kernel memory
                 so it can't get swapped out */
                result = ipr_alloc_ucode_buffer(ioa_cmd.buffer_len, &p_scatterlist);

                if (result)
                {
                    ipr_log_err("Microcode buffer allocation failed"IPR_EOL);
                    break;
                }

                result = ipr_copy_ucode_buffer(p_scatterlist,
                                                  p_buffer,
                                                  ioa_cmd.buffer_len);

                if (result)
                {
                    ipr_log_err("Microcode buffer copy to kernel memory failed"IPR_EOL);
                    ipr_free_ucode_buffer(p_scatterlist);
                    break;
                }

                scatterlist = p_scatterlist->scatterlist;

                spin_lock_irqsave(&io_request_lock, io_flags);

                if (p_resource == &ipr_cfg->shared.ioa_resource)
                {
                    ipr_block_all_requests(ipr_cfg);

                    rc = ipr_shutdown_ioa(ipr_cfg, IPR_SHUTDOWN_NORMAL);

                    if ((rc != 0) && (rc != IPR_RC_QUAL_SUCCESS))
                    {
                        ipr_trace;
                        result = -EIO;
                        goto leave_write_buffer_reset;
                    }
                }

                result = ipr_get_free_sis_cmnd_for_ioctl_internal(ipr_cfg, &p_sis_cmnd);

                if (result)
                    goto leave_write_buffer_reset;

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.request_buffer = scatterlist;
                p_sis_cmnd->ccb.scsi_use_sg = p_scatterlist->num_sg;
                p_sis_cmnd->ccb.flags |= IPR_ALLOW_REQ_OVERRIDE;
                p_sis_cmnd->ccb.cdb[0] = WRITE_BUFFER;
                p_sis_cmnd->ccb.cdb[1] = 5;
                p_sis_cmnd->ccb.cdb[6] = (ioa_cmd.buffer_len & 0xff0000) >> 16;
                p_sis_cmnd->ccb.cdb[7] = (ioa_cmd.buffer_len & 0x00ff00) >> 8;
                p_sis_cmnd->ccb.cdb[8]= ioa_cmd.buffer_len & 0x0000ff;
                p_sis_cmnd->ccb.sc_data_direction = SCSI_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = 10;

                /* Note: This command cannot have retries since it uses a scatter/gather
                 list. If we had a retry here we would end up mapping the data once for
                 each retry, and have a TCE leak */
                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                                IPR_WRITE_BUFFER_TIMEOUT, 1);

                if (rc == IPR_RC_FAILED)
                {
                    ipr_print_sense(cmd, p_sis_cmnd->ccb.sense_buffer);
                    result = -EIO;
                }

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

            leave_write_buffer_reset:
                if ((p_resource == &ipr_cfg->shared.ioa_resource) && rc != IPR_RC_TIMEOUT)
                {
                    if (ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NONE) != SUCCESS)
                        result = -EIO;

                    ipr_unblock_all_requests(ipr_cfg);
                }

                ipr_free_ucode_buffer(p_scatterlist);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            case IPR_DISCARD_CACHE_DATA:
                if (ipr_cfg->shared.nr_ioa_microcode)
                    return -EIO;

                if (ioa_cmd.buffer_len != sizeof(struct ipr_discard_cache_data))
                {
                    result = -EINVAL;
                    ipr_log_err("Invalid buffer length on discard cache data %d"IPR_EOL,
                                   ioa_cmd.buffer_len);
                    break;
                }

                p_cmd_buffer = ipr_dma_malloc(&ipr_cfg->shared,
                                                 ioa_cmd.buffer_len,
                                                 &p_cmd_buffer_dma,
                                                 IPR_ALLOC_CAN_SLEEP);

                if (p_cmd_buffer == NULL)
                {
                    result = -ENOMEM;
                    ipr_log_err("Buffer allocation for discard cache data failed"IPR_EOL);
                    break;
                }

                if ((result = copy_to_user(p_buffer, p_cmd_buffer, ioa_cmd.buffer_len)))
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer, p_cmd_buffer_dma);
                    ipr_log_err("Copy to kernel memory failed"IPR_EOL);
                    break;
                }

                spin_lock_irqsave(&io_request_lock, io_flags);

                result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

                if (result)
                {
                    ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                    p_cmd_buffer, p_cmd_buffer_dma);
                    spin_unlock_irqrestore(&io_request_lock, io_flags);
                    return result;
                }

                p_sis_cmnd->ccb.p_resource = p_resource;
                p_sis_cmnd->ccb.flags = IPR_BUFFER_MAPPED;
                p_sis_cmnd->ccb.buffer = p_cmd_buffer;
                p_sis_cmnd->ccb.buffer_dma = p_cmd_buffer_dma;
                p_sis_cmnd->ccb.bufflen = ioa_cmd.buffer_len;
                p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
                p_sis_cmnd->ccb.cmd_len = 10;
                p_sis_cmnd->ccb.cdb[0] = cmd;
                p_sis_cmnd->ccb.cdb[1] = ioa_cmd.cdb[1];

                rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                              IPR_DISCARD_CACHE_DATA_TIMEOUT, 2);

                if (rc == IPR_RC_FAILED)
                {
                    p_sense_buffer = p_sis_cmnd->ccb.sense_buffer;

                    ipr_print_sense(cmd, p_sense_buffer);

                    if (sense_error(p_sense_buffer[2] == ILLEGAL_REQUEST))
                        result = -EINVAL;
                    else
                        result = -EIO;
                } 

                ipr_dma_free(&ipr_cfg->shared, ioa_cmd.buffer_len,
                                p_cmd_buffer, p_cmd_buffer_dma);

                ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                spin_unlock_irqrestore(&io_request_lock, io_flags);
                break;
            default:
                ipr_log_err("Invalid SIS command issued 0x%02X"IPR_EOL, cmd);
                result = -EINVAL;
                break;
        }
    }
    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Perform device concurrent mainenance
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -ENXIO      - No such file or device
 *          -EINVAL     - Invalid parameter
 *          -ENOMEM     - Out of memory
 *          -EIO        - I/O error
 *---------------------------------------------------------------------------*/
static int ipr_conc_maint(ipr_host_config *ipr_cfg,
                             struct ipr_res_addr res_addr, u32 type, u32 delay)
{
    struct ipr_resource_entry *p_resource;
    struct ipr_encl_status_ctl_pg *p_ses_data;
    struct ipr_drive_elem_status *p_drive_elem_status;
    ipr_dma_addr ses_dma_addr;
    int rc, result = -ETIME;
    int loop_count = 0;
    u8 status;
    struct ipr_lun *p_lun;
    int loops_left = (delay * 60) / IPR_SDB_SLEEP_TIME;

    if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
        return -EIO;

    p_resource = ipr_get_ses_resource(ipr_cfg, res_addr);

    if (p_resource == NULL)
        return -ENXIO;

    p_ses_data = ipr_dma_calloc(&ipr_cfg->shared,
                                   sizeof(struct ipr_encl_status_ctl_pg),
                                   &ses_dma_addr, IPR_ALLOC_ATOMIC);

    if (p_ses_data == NULL)
    {
        ipr_trace;
        return -ENOMEM;
    }

    p_lun = ipr_get_lun_res_addr(ipr_cfg, res_addr);

    while (loops_left--)
    {
        /* Get Enclosure status/control page */
        rc = ipr_ses_receive_diagnostics(ipr_cfg, 2, p_ses_data, ses_dma_addr,
                                            sizeof(struct ipr_encl_status_ctl_pg), p_resource);

        /* Receive diagnostics is done twice to conceal a problem found in ses which has
         been fixed but may potentially cause confusion if fix not loaded on target device */
        rc = ipr_ses_receive_diagnostics(ipr_cfg, 2, p_ses_data, ses_dma_addr,
                                            sizeof(struct ipr_encl_status_ctl_pg), p_resource);

        if (rc)
        {
            ipr_trace;
            result = rc;
            goto leave;
        }

        p_drive_elem_status = ipr_get_elem_status(p_ses_data, res_addr.target);

        if (p_drive_elem_status == NULL)
        {
            ipr_trace;
            result = -EIO;
            goto leave;
        }

        status = p_drive_elem_status->status;

        if (loop_count++ > 0)
        {
            switch (type)
            {
                case IPR_CONC_MAINT_INSERT:
                    if (status == IPR_DRIVE_ELEM_STATUS_STATUS_EMPTY)
                    {
                        /* Not done */
                    }
                    else if (status == IPR_DRIVE_ELEM_STATUS_STATUS_POPULATED)
                    {
                        /* Done with concurrent maintenance */
                        result = 0;
                        goto leave;
                    }
                    else
                    {
                        result = -EIO;
                        ipr_trace;
                        goto leave;
                    }
                    break;
                case IPR_CONC_MAINT_REMOVE:
                    if (status == IPR_DRIVE_ELEM_STATUS_STATUS_EMPTY)
                    {
                        /* Done with concurrent maintenance */
                        result = 0;
                        goto leave;
                    }
                    else if (status == IPR_DRIVE_ELEM_STATUS_STATUS_POPULATED)
                    {
                        /* Not done */
                    }
                    else
                    {
                        result = -EIO;
                        ipr_trace;
                        goto leave;
                    }
                    break;
                default:
                    result = -EIO;
                    ipr_trace;
                    goto leave;
                    break;
            };
        }

        switch (type)
        {
            case IPR_CONC_MAINT_INSERT:
                p_lun->expect_ccm = 1;
                p_drive_elem_status->select = 1;
                p_drive_elem_status->insert = 1;
                break;
            case IPR_CONC_MAINT_REMOVE:
                p_drive_elem_status->select = 1;
                p_drive_elem_status->remove = 1;
                break;
            default:
                result = -EIO;
                ipr_trace;
                goto leave;
                break;
        };

        /* set the flag in the overall status to disable SCSI reset 
         upon detecting a device inserted */
        p_ses_data->overall_status_select = 1;
        p_ses_data->overall_status_disable_resets = 1;
        p_ses_data->overall_status_insert = 0;
        p_ses_data->overall_status_remove = 0;
        p_ses_data->overall_status_identify = 0;

        /* Issue a send diagnostics to blink the LED */
        rc = ipr_ses_send_diagnostics(ipr_cfg, p_ses_data,
                                         ses_dma_addr, sizeof(struct ipr_encl_status_ctl_pg),
                                         p_resource);

        if (rc != 0)
        {
            result = rc;
            ipr_trace;
            goto leave;
        }

        /* block requests to prevent timeouts */
        ipr_block_midlayer_requests(ipr_cfg);

        /* Suspend device bus */
        rc = ipr_suspend_device_bus(ipr_cfg, res_addr, IPR_SDB_CHECK_AND_QUIESCE);

        if (rc != 0)
        {
            result = rc;
            ipr_trace;

            /* unblock requests */
            ipr_unblock_midlayer_requests(ipr_cfg);
            goto leave;
        }

        /* Sleep for 15 seconds */
        ipr_sleep(IPR_SDB_SLEEP_TIME * 1000);

        /* Resume device bus */
        rc = ipr_resume_device_bus(ipr_cfg, res_addr);

        /* unblock requests */
        ipr_unblock_midlayer_requests(ipr_cfg);

        if (rc != 0)
        {
            result = rc;
            ipr_trace;
            goto leave;
        }
    }

    leave:

        if (loop_count > 0)
        {
            /* set the flag in the overall status to enable SCSI reset 
             upon detecting a device inserted */
            p_ses_data->overall_status_select = 1;
            p_ses_data->overall_status_disable_resets = 0;
            p_ses_data->overall_status_insert = 0;
            p_ses_data->overall_status_remove = 0;
            p_ses_data->overall_status_identify = 0;

            switch (type)
            {
                case IPR_CONC_MAINT_INSERT:
                    p_drive_elem_status->select = 1;
                    p_drive_elem_status->insert = 0;
                    p_drive_elem_status->remove = 0;
                    break;
                case IPR_CONC_MAINT_REMOVE:
                    p_drive_elem_status->select = 1;
                    p_drive_elem_status->insert = 0;
                    p_drive_elem_status->remove = 0;
                    p_drive_elem_status->identify = 0;
                    break;
                default:
                    ipr_trace;
                    break;
            };

            /* Issue a send diagnostics to stop blinking the LED */
            rc = ipr_ses_send_diagnostics(ipr_cfg, p_ses_data,
                                             ses_dma_addr, sizeof(struct ipr_encl_status_ctl_pg),
                                             p_resource);

            if (rc != 0)
            {
                ipr_trace;
                result = rc;
            }
        }

    ipr_dma_free(&ipr_cfg->shared, sizeof(struct ipr_encl_status_ctl_pg),
                    p_ses_data,ses_dma_addr);

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Issue a request sense to a GPDD
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O error
 *---------------------------------------------------------------------------*/
static int ipr_request_sense(ipr_host_config *ipr_cfg,
                                struct ipr_resource_entry *p_resource,
                                struct ipr_cmnd *p_sis_cmnd)
{
    int rc;

    if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
        return -EIO;

    ipr_initialize_sis_cmnd(p_sis_cmnd);

    p_sis_cmnd->ccb.p_resource = p_resource;
    p_sis_cmnd->ccb.buffer = p_sis_cmnd->ccb.sense_buffer;
    p_sis_cmnd->ccb.buffer_dma = p_sis_cmnd->ccb.sense_buffer_dma;
    p_sis_cmnd->ccb.bufflen = IPR_SENSE_BUFFERSIZE;
    p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
    p_sis_cmnd->ccb.cmd_len = 6;
    p_sis_cmnd->ccb.use_sg = 1;

    p_sis_cmnd->ccb.cdb[0] = REQUEST_SENSE;
    p_sis_cmnd->ccb.flags = IPR_CMD_SYNC_OVERRIDE | IPR_BUFFER_MAPPED;

    init_waitqueue_head(&p_sis_cmnd->wait_q);

    rc = ipr_do_req(&ipr_cfg->shared, &p_sis_cmnd->ccb,
                       ipr_ioctl_cmd_done, IPR_INTERNAL_TIMEOUT/HZ);

    if (rc)
    {
        ipr_trace;
        return -EIO;
    }

    ipr_sleep_on(&io_request_lock, &p_sis_cmnd->wait_q);

    return p_sis_cmnd->ccb.completion;
}

/*---------------------------------------------------------------------------
 * Purpose: Issue a receive diagnostics to a SES
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O error
 *---------------------------------------------------------------------------*/
static int ipr_ses_receive_diagnostics(ipr_host_config *ipr_cfg,
                                          u8 page,
                                          void *p_buffer,
                                          ipr_dma_addr buffer_dma_addr,
                                          u16 bufflen,
                                          struct ipr_resource_entry *p_resource)
{
    struct ipr_cmnd *p_sis_cmnd;
    int result = 0;
    int rc;

    result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

    if (result)
        return result;

    p_sis_cmnd->ccb.p_resource = p_resource;

    /* Send a receive diagnostics to get the enclosure status/control page */
    p_sis_cmnd->ccb.buffer = p_buffer;
    p_sis_cmnd->ccb.buffer_dma = buffer_dma_addr;
    p_sis_cmnd->ccb.bufflen = bufflen;
    p_sis_cmnd->ccb.data_direction = IPR_DATA_READ;
    p_sis_cmnd->ccb.cmd_len = 6;
    p_sis_cmnd->ccb.cdb[0] = RECEIVE_DIAGNOSTIC;
    p_sis_cmnd->ccb.cdb[1] = 0x01;      /* Page Code Valid */
    p_sis_cmnd->ccb.cdb[2] = page;
    p_sis_cmnd->ccb.cdb[3] = (bufflen >> 8) & 0xff;
    p_sis_cmnd->ccb.cdb[4] = bufflen & 0xff;
    p_sis_cmnd->ccb.cdb[5] = 0;
    p_sis_cmnd->ccb.flags = IPR_GPDD_CMD | IPR_CMD_SYNC_OVERRIDE | IPR_BUFFER_MAPPED;
    p_sis_cmnd->ccb.underflow = 0;

    rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                  IPR_INTERNAL_TIMEOUT, 3);

    if ((rc == IPR_RC_FAILED) && (status_byte(p_sis_cmnd->ccb.status) == CHECK_CONDITION))
        ipr_request_sense(ipr_cfg, p_resource, p_sis_cmnd);

    if (rc == IPR_RC_FAILED)
    {
        ipr_log_err("Receive diagnostics to %08X failed with rc 0x%x"IPR_EOL,
                       IPR_GET_PHYSICAL_LOCATOR(p_resource->resource_address), rc);
        ipr_print_sense(RECEIVE_DIAGNOSTIC, p_sis_cmnd->ccb.sense_buffer);
        result = -EIO;
    }

    ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Suspend a given device bus
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O error
 *          -EACCES     - Device cannot be removed
 *---------------------------------------------------------------------------*/
static int ipr_suspend_device_bus(ipr_host_config *ipr_cfg,
                                     struct ipr_res_addr res_addr,
                                     u8 option)
{
    struct ipr_cmnd *p_sis_cmnd;
    int result = 0;
    int rc;
    struct ipr_resource_entry *p_resource;

    p_resource = &ipr_cfg->shared.ioa_resource;

    result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

    if (result)
        return result;

    p_sis_cmnd->ccb.p_resource = p_resource;

    /* Suspend the device bus */
    p_sis_cmnd->ccb.sc_data_direction = SCSI_DATA_NONE;
    p_sis_cmnd->ccb.cmd_len = 10;
    p_sis_cmnd->ccb.flags |= IPR_IOA_CMD | IPR_ALLOW_REQ_OVERRIDE;
    p_sis_cmnd->ccb.cdb[0] = IPR_SUSPEND_DEV_BUS;
    p_sis_cmnd->ccb.cdb[1] = option;
    p_sis_cmnd->ccb.cdb[3] = res_addr.bus;
    p_sis_cmnd->ccb.cdb[4] = res_addr.target;
    p_sis_cmnd->ccb.cdb[5] = res_addr.lun;

    rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                  IPR_SUSPEND_DEV_BUS_TIMEOUT, 3);

    if (rc == IPR_RC_FAILED)
    {
        if (ipr_sense_valid(p_sis_cmnd->ccb.sense_buffer[0]) &&
            (p_sis_cmnd->ccb.sense_buffer[2] == 0x0B) &&
            (p_sis_cmnd->ccb.sense_buffer[12] == 0x53) &&
            (p_sis_cmnd->ccb.sense_buffer[13] == 0x02))
        {
            result = -EACCES;
        }
        else
        {
            ipr_log_err("Suspend device bus to %08X failed."IPR_EOL,
                           IPR_GET_PHYSICAL_LOCATOR(res_addr));
            ipr_print_sense(IPR_SUSPEND_DEV_BUS, p_sis_cmnd->ccb.sense_buffer);
            result = -EIO;
        }
    }

    ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Resume a given device bus
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O error
 *---------------------------------------------------------------------------*/
static int ipr_resume_device_bus(ipr_host_config *ipr_cfg,
                                    struct ipr_res_addr res_addr)
{
    struct ipr_cmnd *p_sis_cmnd;
    int result = 0;
    int rc;
    struct ipr_resource_entry *p_resource;

    p_resource = &ipr_cfg->shared.ioa_resource;

    result = ipr_get_free_sis_cmnd_for_ioctl_internal(ipr_cfg, &p_sis_cmnd);

    if (result)
        return result;

    p_sis_cmnd->ccb.p_resource = p_resource;

    /* Suspend the device bus */
    p_sis_cmnd->ccb.sc_data_direction = SCSI_DATA_NONE;
    p_sis_cmnd->ccb.cmd_len = 10;
    p_sis_cmnd->ccb.flags |= IPR_IOA_CMD | IPR_ALLOW_REQ_OVERRIDE;
    p_sis_cmnd->ccb.cdb[0] = IPR_RESUME_DEVICE_BUS;
    p_sis_cmnd->ccb.cdb[3] = res_addr.bus;
    p_sis_cmnd->ccb.cdb[4] = res_addr.target;
    p_sis_cmnd->ccb.cdb[5] = res_addr.lun;

    rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                  IPR_INTERNAL_TIMEOUT, 3);

    if (rc == IPR_RC_FAILED)
    {
        ipr_log_err("Resume device bus to %08X failed."IPR_EOL, IPR_GET_PHYSICAL_LOCATOR(res_addr));
        ipr_print_sense(IPR_RESUME_DEVICE_BUS, p_sis_cmnd->ccb.sense_buffer);
        result = -EIO;
    }

    ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Issue a send diagnostics to a SES
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          -EIO        - I/O error
 *---------------------------------------------------------------------------*/
static int ipr_ses_send_diagnostics(ipr_host_config *ipr_cfg,
                                       void *p_buffer,
                                       ipr_dma_addr buffer_dma_addr,
                                       u16 bufflen,
                                       struct ipr_resource_entry *p_resource)
{
    struct ipr_cmnd *p_sis_cmnd;
    int result;
    int rc;

    result = 0;

    result = ipr_get_free_sis_cmnd_for_ioctl(ipr_cfg, &p_sis_cmnd);

    if (result)
        return result;

    p_sis_cmnd->ccb.p_resource = p_resource;

    /* Send a send diagnostics to the SES */
    p_sis_cmnd->ccb.buffer = p_buffer;
    p_sis_cmnd->ccb.buffer_dma = buffer_dma_addr;
    p_sis_cmnd->ccb.bufflen = bufflen;
    p_sis_cmnd->ccb.data_direction = IPR_DATA_WRITE;
    p_sis_cmnd->ccb.cmd_len = 6;
    p_sis_cmnd->ccb.cdb[0] = SEND_DIAGNOSTIC;
    p_sis_cmnd->ccb.cdb[1] = 0x10;      /* Page Format */
    p_sis_cmnd->ccb.cdb[2] = 0;
    p_sis_cmnd->ccb.cdb[3] = (bufflen >> 8) & 0xff;
    p_sis_cmnd->ccb.cdb[4] = bufflen & 0xff;
    p_sis_cmnd->ccb.cdb[5] = 0;
    p_sis_cmnd->ccb.flags = IPR_GPDD_CMD | IPR_CMD_SYNC_OVERRIDE | IPR_BUFFER_MAPPED;

    rc = ipr_send_blocking_ioctl(ipr_cfg, p_sis_cmnd,
                                  IPR_INTERNAL_TIMEOUT, 3);

    if ((rc == IPR_RC_FAILED) && (status_byte(p_sis_cmnd->ccb.status) == CHECK_CONDITION))
        ipr_request_sense(ipr_cfg, p_resource, p_sis_cmnd);

    if (rc == IPR_RC_FAILED)
    {
        ipr_log_err("Send diagnostics to %08X failed."IPR_EOL,
                       IPR_GET_PHYSICAL_LOCATOR(p_resource->resource_address));
        ipr_print_sense(SEND_DIAGNOSTIC, p_sis_cmnd->ccb.sense_buffer);
        result = -EIO;
    }

    ipr_put_ioctl_cmnd_to_free(ipr_cfg, p_sis_cmnd);

    return result;
}


/*---------------------------------------------------------------------------
 * Purpose: Find the drive_elem_status entry for the given scsi id
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to drive_elem_status entry
 *          NULL
 *---------------------------------------------------------------------------*/
static struct ipr_drive_elem_status*
ipr_get_elem_status(struct ipr_encl_status_ctl_pg* p_encl_status_ctl_pg,
                       u8 scsi_id)
{
    u32 slot;
    struct ipr_drive_elem_status* p_drive_elem_status = NULL;

    for (slot=0;
         slot<((sistoh16(p_encl_status_ctl_pg->byte_count)-8)/sizeof(struct ipr_drive_elem_status));
         slot++)
    {
        if (scsi_id == p_encl_status_ctl_pg->elem_status[slot].scsi_id)
        {
            p_drive_elem_status = &p_encl_status_ctl_pg->elem_status[slot];
            return p_drive_elem_status;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Allocate a DMA'able buffer in chunks and assembles a
 *          scatter/gather list to use for ucode download
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Note: This function may sleep
 * Returns: 0           - Success
 *          -EINVAL     - Invalid paramter
 *          -ENOMEM     - Out of memory
 *          others
 *---------------------------------------------------------------------------*/
static int ipr_alloc_ucode_buffer(u32 buf_len,
                                     struct ipr_dnload_sglist **pp_scatterlist)
{
    int sg_size, order, result, bsize_elem, num_elem, i, j;
    struct ipr_dnload_sglist *p_dnld;
    struct scatterlist *scatterlist;

    result = 0;

    /* Get the minimum size per scatter/gather element */
    sg_size = buf_len / (IPR_MAX_SGLIST - 1);

    /* Get the actual size per element */
    order = get_order (sg_size);

    if (order > 5)
    {
        ipr_trace;
        return -EINVAL;
    }

    /* Determine the actual number of bytes per element */
    bsize_elem = PAGE_SIZE * (1 << order);

    /* Determine the actual number of sg entries needed */
    if (buf_len % bsize_elem)
        num_elem = (buf_len / bsize_elem) + 1;
    else
        num_elem = buf_len / bsize_elem;

    /* Allocate a scatter/gather list for the DMA */
    p_dnld = ipr_kcalloc(sizeof(struct ipr_dnload_sglist) +
                            (sizeof(struct scatterlist) * (num_elem-1)),
                            IPR_ALLOC_CAN_SLEEP);

    if (p_dnld == NULL)
    {
        ipr_trace;
        return -ENOMEM;
    }

    scatterlist = p_dnld->scatterlist;

    p_dnld->order = order;
    p_dnld->num_sg = num_elem;

    /* Allocate a bunch of sg elements */
    for (i = 0; i < num_elem; i++)
    {
        scatterlist[i].address = ipr_get_free_pages(GFP_KERNEL | IPR_GFP_DMA, order);
        if (scatterlist[i].address == NULL)
        {
            ipr_trace;

            /* Free up what we already allocated */
            for (j = i - 1; j >= 0; j--)
                ipr_free_pages(scatterlist[j].address, order);
            result = -ENOMEM;
            ipr_kfree(p_dnld, sizeof(struct ipr_dnload_sglist) +
                         (sizeof(struct scatterlist) * (num_elem-1)));
            p_dnld = NULL;
            break;
        }

        memset (scatterlist[i].address, 0, bsize_elem);
    }

    *pp_scatterlist = p_dnld;

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Free a DMA'able ucode download buffer previously allocated with
 *          ipr_alloc_ucode_buffer
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: 0           - Success
 *          -EINVAL     - Invalid paramter
 *          -ENOMEM     - Out of memory
 *          others
 *---------------------------------------------------------------------------*/
static void ipr_free_ucode_buffer(struct ipr_dnload_sglist *p_dnld)
{
    int i;
    u32 order = p_dnld->order;
    u32 num_sg = p_dnld->num_sg;
    struct scatterlist *scatterlist = p_dnld->scatterlist;

    for (i = 0; i < num_sg; i++)
        ipr_free_pages(scatterlist[i].address, order);
    ipr_kfree(p_dnld,
                 sizeof(struct ipr_dnload_sglist) +
                 (sizeof(struct scatterlist) * (num_sg-1)));
}

/*---------------------------------------------------------------------------
 * Purpose: Copy IOA or device ucode from userspace to kernel space
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: 0           - Success
 *          others      - Failed
 *---------------------------------------------------------------------------*/
static int ipr_copy_ucode_buffer(struct ipr_dnload_sglist *p_scatterlist,
                                    u8 *p_write_buffer,
                                    u32 buf_len)
{
    int bsize_elem, result, i;
    struct scatterlist *scatterlist;

    /* Determine the actual number of bytes per element */
    bsize_elem = PAGE_SIZE * (1 << p_scatterlist->order);

    scatterlist = p_scatterlist->scatterlist;

    for (i = 0; i < (buf_len / bsize_elem); i++, p_write_buffer += bsize_elem)
    {
        result = copy_from_user(scatterlist[i].address, p_write_buffer, bsize_elem);
        scatterlist[i].length = bsize_elem;

        if (result != 0)
        {
            ipr_trace;
            return result;
        }
    }

    if (buf_len % bsize_elem)
    {
        result = copy_from_user(scatterlist[i].address, p_write_buffer,
                                buf_len % bsize_elem);
        scatterlist[i].length =  buf_len % bsize_elem;
    }

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Open connection ipr char device
 * Context: Task level only
 * Lock State: No locks assumed to be held
 * Returns: 0           - Success
 *          -ENXIO      - No such device
 *---------------------------------------------------------------------------*/
static int ipr_open(struct inode *inode, struct file *filp)
{
    ipr_host_config *ipr_cfg;
    int dev;
    unsigned long io_flags = 0;

    spin_lock_irqsave(&io_request_lock,io_flags);
    if (filp->private_data == NULL)
    {
        dev = MINOR(inode->i_rdev);
        ipr_cfg = ipr_get_host(dev);
        filp->private_data = ipr_cfg;
    }
    else
        ipr_cfg = filp->private_data;

    if (ipr_cfg == NULL)
    {
        spin_unlock_irqrestore(&io_request_lock,io_flags);
        return -ENXIO;
    }

    MOD_INC_USE_COUNT;

    spin_unlock_irqrestore(&io_request_lock,io_flags);
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Use minor number to get pointer to main CB
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to main CB or NULL if does not exist
 *---------------------------------------------------------------------------*/
static ipr_host_config * ipr_get_host(int dev)
{
    ipr_host_config * ipr_cfg = NULL;
    int i;

    if ((ipr_num_ctlrs > 0) && (ipr_cfg_head != NULL))
    {
        for (i = 0, ipr_cfg = ipr_cfg_head;
             i < ipr_num_ctlrs;
             i++, ipr_cfg = ipr_cfg->p_next)
        {
            if (ipr_cfg->minor_num == dev)
                break;
        }
    }
    return ipr_cfg;
}

/*---------------------------------------------------------------------------
 * Purpose: Close connection to ipr char device
 * Context: Task level only
 * Lock State: No locks assumed to be held
 * Returns: 0           - Success
 *          -ENXIO      - No such device
 *---------------------------------------------------------------------------*/
static int ipr_close(struct inode *inode, struct file *filp)
{
    int result = 0;
    unsigned long io_flags = 0;

    if (filp->private_data == NULL)
        return -ENXIO;

    spin_lock_irqsave(&io_request_lock,io_flags);
    MOD_DEC_USE_COUNT;
    spin_unlock_irqrestore(&io_request_lock,io_flags);

    return result;
}

/*---------------------------------------------------------------------------
 * Purpose: Prepare a command block to be used for ERP
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_start_erp(struct ipr_cmnd *p_sis_cmnd)
{
    p_sis_cmnd->p_saved_scsi_cmd = p_sis_cmnd->p_scsi_cmd;
    p_sis_cmnd->p_scsi_cmd = NULL;
    p_sis_cmnd->ccb.saved_data_direction = p_sis_cmnd->ccb.data_direction;
    p_sis_cmnd->ccb.data_direction = 0;
    p_sis_cmnd->ccb.saved_use_sg = p_sis_cmnd->ccb.use_sg;
    p_sis_cmnd->ccb.use_sg = 0;
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Restore command block which was used for ERP
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_end_erp(struct ipr_cmnd *p_sis_cmnd)
{
    p_sis_cmnd->ccb.flags &= ~IPR_ERP_CMD;
    p_sis_cmnd->p_scsi_cmd = p_sis_cmnd->p_saved_scsi_cmd;
    p_sis_cmnd->ccb.data_direction = p_sis_cmnd->ccb.saved_data_direction;
    p_sis_cmnd->ccb.use_sg = p_sis_cmnd->ccb.saved_use_sg;
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Task-Level Entry Point to Send off more HCAMs, process Unit
 *          Check buffer, and do some ERP
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_mailbox(ipr_host_config *ipr_cfg)
{
    int i;
    struct ipr_cmnd *p_sis_cmnd;
    Scsi_Cmnd     *p_scsi_cmd;
    u8 *p_cdb;
    struct ipr_lun *p_lun;
    struct ipr_hostrcb *p_hostrcb;
    u32 delay = 1000;
    const struct ipr_ioa_cfg_t *p_ioa_cfg = ipr_cfg->p_ioa_cfg;

    if (ipr_cfg->flag & IPR_UNIT_CHECKED)
    {
        if (ipr_cfg->flag & IPR_OPERATIONAL)
        {
            ipr_break_or_die_if_reset_reload_disabled;

            /* Reset the IOA */
            ipr_ioa_reset(ipr_cfg, IPR_IRQ_ENABLED);

            if (p_ioa_cfg->cpu_rst_support == IPR_CPU_RST_SUPPORT_CFGSPC_403RST_BIT)
            {
                while ((pci_write_config_dword(ipr_cfg->pdev, IPR_RESET_403_OFFSET, IPR_RESET_403) !=
                        PCIBIOS_SUCCESSFUL) && delay)
                {
                    delay -= 10;
                    ipr_sleep(10);
                }
            }

            if (delay == 0)
            {
                ipr_log_err("Gave up trying to reset 403"IPR_EOL);
                ipr_unit_check_no_data(ipr_cfg);
                ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NONE);
                ipr_unblock_all_requests(ipr_cfg);
            }
            else
            {
                ipr_get_unit_check_buffer(ipr_cfg);
                if (WAIT_FOR_DUMP == ipr_get_sdt_state)
                {
                    ipr_get_sdt_state = GET_DUMP;
                    ipr_get_ioa_smart_dump(ipr_cfg);
                }

                ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NONE);
                ipr_unblock_all_requests(ipr_cfg);
                if (ipr_get_sdt_state == DUMP_OBTAINED)
                    wake_up_interruptible(&ipr_sdt_wait_q);
            }
        }
    }
    else if (ipr_cfg->flag & IPR_IOA_NEEDS_RESET)
    {
        if (ipr_cfg->flag & IPR_OPERATIONAL)
        {
            ipr_break_or_die_if_reset_reload_disabled;
            if (WAIT_FOR_DUMP == ipr_get_sdt_state)
                ipr_get_sdt_state = GET_DUMP;
            ipr_reset_reload(ipr_cfg, IPR_SHUTDOWN_NONE);
            ipr_unblock_all_requests(ipr_cfg);
            if (ipr_get_sdt_state == DUMP_OBTAINED)
                wake_up_interruptible(&ipr_sdt_wait_q);
        }
    }
    else
    {
        /* Note: The path below is only for GPDD ops */
        while(ipr_cfg->qErrorH)
        {
            p_sis_cmnd = ipr_cfg->qErrorH;
            ipr_remove_sis_cmnd_from_error(ipr_cfg, p_sis_cmnd);

            if (p_sis_cmnd->p_scsi_cmd != NULL)
            {
                p_scsi_cmd = p_sis_cmnd->p_scsi_cmd;
                ipr_start_erp(p_sis_cmnd);
            }
            else
                p_scsi_cmd = p_sis_cmnd->p_saved_scsi_cmd;

            p_lun = ipr_get_lun_scsi(ipr_cfg, p_scsi_cmd);

            if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
            {
                /* Put this op back on the pending queue to get failed
                 back with all the others */
                ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

                p_scsi_cmd->result |= (DID_ERROR << 16);
                continue;
            }

            if (p_lun->stop_new_requests &&
                ((p_sis_cmnd->ccb.flags & IPR_ERP_CMD) == 0))
            {
                ipr_end_erp(p_sis_cmnd);

                /* Already doing ERP for this device */
                ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                p_scsi_cmd->result |= (DID_ERROR << 16);

                p_scsi_cmd->scsi_done(p_scsi_cmd);
                continue;
            }

            p_cdb = p_sis_cmnd->ccb.cdb;
            p_sis_cmnd->ccb.flags |= IPR_ERP_CMD;
            p_lun->stop_new_requests = 1;

            /* If the last command we sent with this command block was a
             request sense and it did not fail, copy over the sense buffer */
            if ((p_cdb[0] == REQUEST_SENSE) &&
                (p_sis_cmnd->ccb.completion != IPR_RC_FAILED))
                memcpy(p_scsi_cmd->sense_buffer, p_sis_cmnd->ccb.sense_buffer,
                                   IPR_SENSE_BUFFER_COPY_SIZE);

            /* If the last command issued received a check condition and
             it was not a request sense command, issue a request sense */
            if ((status_byte(p_sis_cmnd->ccb.status) == CHECK_CONDITION) &&
                (p_cdb[0] != REQUEST_SENSE))
            {
                /* Save away check condition status to return to the host */
                p_scsi_cmd->result |= (CHECK_CONDITION << 1);

                /* Put this back on the pending queue */
                ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

                p_sis_cmnd->ccb.completion = IPR_RC_SUCCESS;
                p_sis_cmnd->ccb.status = 0;

                memset(p_cdb, 0, IPR_CCB_CDB_LEN);

                p_cdb[0] = REQUEST_SENSE;

                ipr_auto_sense(&ipr_cfg->shared, &p_sis_cmnd->ccb);
            }
            else if (p_cdb[0] != IPR_SYNC_COMPLETE)
            {
                /* Put this back on the pending queue */
                ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

                memset(p_cdb, 0, IPR_CCB_CDB_LEN);

                /* Re-use the cmd blk to send the sync-complete */
                /* We will not send a response to the host until */
                /* this comes back and the device is ready for another command */
                p_cdb[0] = IPR_SYNC_COMPLETE;

                p_sis_cmnd->ccb.completion = IPR_RC_SUCCESS;
                p_sis_cmnd->ccb.status = 0;

                p_sis_cmnd->ccb.flags |= IPR_IOA_CMD;

                ipr_queue_internal(&ipr_cfg->shared, &p_sis_cmnd->ccb);
            }
            else
            {
                if (status_byte(p_scsi_cmd->result) != CHECK_CONDITION)
                {
                    /* If we only got in here because we got back sync required
                     and never had a check condition, then set the DID_ERROR response
                     to force the host to do a retry */
                    p_scsi_cmd->result |= (DID_ERROR << 16);
                }
                else if (ipr_sense_valid(p_scsi_cmd->sense_buffer[0]))
                {
                    /* We got in here because of a check condition. Do not
                     set the DID_ERROR bit and allow the host to do the right
                     thing based on the SCSI sense data */

                    if (ipr_debug && (sense_error(p_scsi_cmd->sense_buffer[2] ==
                                                     ILLEGAL_REQUEST)))
                        print_Scsi_Cmnd(p_scsi_cmd);

                    /* Print out the sense data */
                    IPR_DBG_CMD(print_sense(IPR_NAME, p_scsi_cmd));
                }
                else
                {
                    /* If we only got in here because of a check condition but something
                     went wrong and we couldn't get the sense data. Set the DID_ERROR
                     bit to force a retry */
                    p_scsi_cmd->result |= (DID_ERROR << 16);
                }

                p_lun->stop_new_requests = 0;

                ipr_end_erp(p_sis_cmnd);

                ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);

                p_scsi_cmd->scsi_done(p_scsi_cmd);
            }
        }
    }

    if (ipr_cfg->flag & IPR_ALLOW_HCAMS)
    {
        /* Send off any new HCAMs */
        for (i = 0; i < IPR_NUM_HCAMS; i++)
        {
            p_hostrcb = ipr_cfg->new_hostrcb[i];

            if (p_hostrcb != NULL)
            {
                if (p_hostrcb->op_code == IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE)
                {
                    /* Ship it back to the IOA to be re-used */
                    ipr_send_hcam(&ipr_cfg->shared,
                                     IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, p_hostrcb);
                }
                else
                {
                    /* Ship it back to the IOA to be re-used */
                    ipr_send_hcam(&ipr_cfg->shared,
                                     IPR_HCAM_CDB_OP_CODE_LOG_DATA, p_hostrcb);
                }

                ipr_cfg->new_hostrcb[i] = NULL;
            }
        }

        /* Send back HCAMs already received */
        for (i = 0; i < IPR_NUM_HCAMS;)
        {
            p_hostrcb = ipr_cfg->done_hostrcb[i];

            if (p_hostrcb != NULL)
            {
                if (p_hostrcb->op_code == IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE)
                {
                    if (ipr_cfg->flag & IPR_ALLOW_HCAMS)
                    {
                        /* Here we do not ship the HCAM back since it is the
                         responsibility of the function below to do so */
                        ipr_handle_config_change(&ipr_cfg->shared, p_hostrcb);
                    }
                }
                else
                {
                    ipr_handle_log_data(ipr_cfg, p_hostrcb);

                    /* Ship it back to the IOA to be re-used */
                    ipr_send_hcam(&ipr_cfg->shared,
                                     IPR_HCAM_CDB_OP_CODE_LOG_DATA, p_hostrcb);
                }

                ipr_cfg->done_hostrcb[i] = NULL;
                i = 0;
            }
            else
                i++;
        }
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Process the unit check buffer
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_get_unit_check_buffer(ipr_host_config *ipr_cfg)
{
    unsigned long mailbox, sdt_start_addr, sdt_register_sel, sdt_entry_word;
    unsigned long unit_check_buffer, dump_entry_length;
    u32 num_table_entries, num_entries_used, sdt_state;
    u32 start_offset, end_offset, swap, *p_buffer;
    u8 sdt_entry_byte;
    struct ipr_hostrcb *p_hostrcb;
    int i, rc;
    struct ipr_uc_sdt smart_dump_table;
    u8 ipr_sdt_fmt;

    ENTER;

    if (ipr_cfg->shared.ioa_mailbox != (unsigned long)NULL)
    {
        mailbox = readl(ipr_cfg->shared.ioa_mailbox);
        sdt_register_sel = (mailbox & ipr_cfg->p_ioa_cfg->mbx_bar_sel_mask) >>
            ipr_cfg->p_ioa_cfg->mkr_bar_sel_shift;
        start_offset = mailbox & ipr_cfg->p_ioa_cfg->mbx_addr_mask;

        /* Figure out where the Smart Dump Table is located */
        switch (sdt_register_sel)
        {
            case IPR_SDT_FMT1_BAR0_SEL:
                sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[0] + start_offset;
                ipr_sdt_fmt = IPR_SDT_FMT1;
                swap = 0;
                break;
            case IPR_SDT_FMT1_BAR2_SEL:
                sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[2] + start_offset;
                ipr_sdt_fmt = IPR_SDT_FMT1;
                swap = 1;
                break;
            case IPR_SDT_FMT1_BAR3_SEL:
                sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[3] + start_offset;
                ipr_sdt_fmt = IPR_SDT_FMT1;
                swap = 1;
                break;
            case IPR_SDT_FMT2_BAR0_SEL:
            case IPR_SDT_FMT2_BAR1_SEL:
            case IPR_SDT_FMT2_BAR2_SEL:
            case IPR_SDT_FMT2_BAR3_SEL:
            case IPR_SDT_FMT2_BAR4_SEL:
                ipr_sdt_fmt = IPR_SDT_FMT2;
                sdt_start_addr = mailbox;
                swap = 0;
                break;
            default:
                ipr_sdt_fmt = IPR_SDT_UNKNOWN;
                sdt_start_addr = 0;
                swap = 0;
                break;
        }

        if (ipr_sdt_fmt == IPR_SDT_FMT1)
        {
            if (swap)
                sdt_state = swab32(readl(sdt_start_addr));
            else
                sdt_state = readl(sdt_start_addr);

            /* Smart Dump table is ready to use and the first entry is valid */
            if (sdt_state == IPR_FMT1_SDT_READY_TO_USE)
            {
                if (swap)
                {
                    num_table_entries = swab32(readl(sdt_start_addr + 4));
                    num_entries_used = swab32(readl(sdt_start_addr + 8));
                }
                else
                {
                    num_table_entries = readl(sdt_start_addr + 4);
                    num_entries_used = readl(sdt_start_addr + 8);
                }

                if ((num_table_entries > 0) && (num_entries_used > 0))
                {
                    if (swap)
                        sdt_entry_byte = swab32(readl(sdt_start_addr + 28)) >> 24;
                    else
                        sdt_entry_byte = readl(sdt_start_addr + 28) >> 24;

                    /* Is Valid bit in first entry on? */
                    if (sdt_entry_byte & 0x20)
                    {
                        p_hostrcb = ipr_cfg->hostrcb[0];
                        memset(p_hostrcb, 0, sizeof(struct ipr_hostrcb));

                        if (swap)
                            sdt_entry_word = swab32(readl(sdt_start_addr + 16));
                        else
                            sdt_entry_word = readl(sdt_start_addr + 16);

                        sdt_register_sel = (sdt_entry_word & IPR_CHUKAR_MBX_BAR_SEL_MASK) >>
                            IPR_CHUKAR_MKR_BAR_SEL_SHIFT;
                        start_offset = sdt_entry_word & IPR_CHUKAR_MBX_ADDR_MASK;

                        if (swap)
                            end_offset = swab32(readl(sdt_start_addr + 20));
                        else
                            end_offset = readl(sdt_start_addr + 20);

                        switch (sdt_register_sel)
                        {
                            case IPR_SDT_FMT1_BAR0_SEL:
                                unit_check_buffer = ipr_cfg->shared.hdw_bar_addr[0] + start_offset;
                                swap = 0;
                                break;
                            case IPR_SDT_FMT1_BAR2_SEL:
                                unit_check_buffer = ipr_cfg->shared.hdw_bar_addr[2] + start_offset;
                                swap = 1;
                                break;
                            case IPR_SDT_FMT1_BAR3_SEL:
                                unit_check_buffer = ipr_cfg->shared.hdw_bar_addr[3] + start_offset;
                                swap = 1;
                                break;
                            default:
                                unit_check_buffer = 0;
                                break;
                        }

                        if (unit_check_buffer != 0)
                        {
                            /* Copy over unit check buffer */
                            for (i = 0, p_buffer = (u32 *)p_hostrcb;
                                 i < IPR_MIN((end_offset - start_offset),1024);
                                 i += 4)
                            {
                                if (swap)
                                    p_buffer[i/4] = swab32(readl(unit_check_buffer + i));
                                else
                                    p_buffer[i/4] = readl(unit_check_buffer + i);
                            }

                            ipr_handle_log_data(ipr_cfg, p_hostrcb);
                        }
                        else /* No unit check buffer */
                        {
                            ipr_dbg;
                            ipr_unit_check_no_data(ipr_cfg);
                        }
                    }
                    else /* SDT not valid */
                    {
                        ipr_dbg;
                        ipr_unit_check_no_data(ipr_cfg);
                    }
                }
                else /* SDT not valid */
                {
                    ipr_dbg;
                    ipr_unit_check_no_data(ipr_cfg);
                }
            }
            else /* SDT not ready to use */
            {
                ipr_dbg;
                ipr_unit_check_no_data(ipr_cfg);
            }
        }
        else if (ipr_sdt_fmt == IPR_SDT_FMT2)
        {
            memset(&smart_dump_table, 0, sizeof(struct ipr_uc_sdt));
            rc = ipr_get_ldump_data_section(&ipr_cfg->shared,
                                               sdt_start_addr,
                                               (u32 *)&smart_dump_table,
                                               (sizeof(struct ipr_uc_sdt))
                                               / sizeof (u32));

            /* If Smart Dump Table state is invalid OR no UC buff entry or not
             valid */
            if ((rc == IPR_RC_FAILED) ||
                (sistoh32(smart_dump_table.sdt_header.dump_state) != IPR_FMT2_SDT_READY_TO_USE) || 
                (smart_dump_table.sdt_entry[0].bar_str_offset == 0)  ||
                (!(smart_dump_table.sdt_entry[0].valid_entry)))
            {
                ipr_dbg;
                ipr_unit_check_no_data(ipr_cfg);
                return;
            }

            /* Find length of the first sdt entry (UC buffer) */
            dump_entry_length =
                (sistoh32(smart_dump_table.sdt_entry[0].end_offset) -
                 sistoh32(smart_dump_table.sdt_entry[0].bar_str_offset)) &
                IPR_FMT2_MBX_ADDR_MASK;

            p_hostrcb = ipr_cfg->hostrcb[0];
            memset(p_hostrcb, 0, sizeof(struct ipr_hostrcb));
            rc = ipr_get_ldump_data_section(&ipr_cfg->shared,
                                               sistoh32(smart_dump_table.sdt_entry[0].bar_str_offset),
                                               (u32 *)p_hostrcb,
                                               IPR_MIN(dump_entry_length/sizeof(u32), 256));

            if (rc == IPR_RC_SUCCESS)
                ipr_handle_log_data(ipr_cfg, p_hostrcb);
            else
            {
                ipr_trace;
                ipr_unit_check_no_data(ipr_cfg);
            }
        }
        else /* No smart dump table */
        {
            ipr_dbg;
            ipr_unit_check_no_data(ipr_cfg);
        }
    }
    else /* No mailbox pointer */
    {
        ipr_dbg;
        ipr_unit_check_no_data(ipr_cfg);
    }
    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Log error for unit check no data
 * Context: Task or interrupt level only
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_unit_check_no_data(ipr_host_config *ipr_cfg)
{
    /* If this is a 2780, it might need a code download -
     lets eat the error and try to bring up the IOA again */
    if (!ipr_cfg->shared.needs_download &&
        (ipr_cfg->shared.vendor_id == PCI_VENDOR_ID_IBM) &&
        (ipr_cfg->shared.device_id == PCI_DEVICE_ID_IBM_SNIPE) &&
        (ipr_cfg->shared.subsystem_id == IPR_SUBS_DEV_ID_2780))
    {
        ipr_cfg->shared.needs_download = 1;
    }
    else
    {
        ipr_beg_err(KERN_CRIT);
        ipr_log_crit("IOA unit check with no data"IPR_EOL);
        ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_CRIT);
        ipr_end_err(KERN_CRIT);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Get DMA address of given HCAM buffer
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: DMA address of HCAM - will panic if it cannot be found
 *---------------------------------------------------------------------------*/
static ipr_dma_addr ipr_get_hcam_dma_addr(ipr_host_config *ipr_cfg,
                                                struct ipr_hostrcb *p_hostrcb)
{
    int i;

    for (i = 0; i < IPR_NUM_HCAMS; i++)
    {
        if (ipr_cfg->hostrcb[i] == p_hostrcb)
            return ipr_cfg->hostrcb_dma[i];
    }
    panic(IPR_ERR": HostRCB was not found!!"IPR_EOL);
}

/*---------------------------------------------------------------------------
 * Purpose: Kernel thread for ERP and HCAMS
 * Context: Task level only
 * Lock State: No lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
int ipr_task_thread(void *data)
{
    unsigned long io_flags;
    ipr_host_config *ipr_cfg = (ipr_host_config *)data;

    lock_kernel();

    daemonize();
    reparent_to_init();

    sprintf(current->comm, IPR_NAME"_%d", ipr_cfg->host->host_no);

    ipr_cfg->task_thread = current;

    siginitsetinv(&current->blocked, SHUTDOWN_SIGS);

    md_flush_signals();

    unlock_kernel();

    complete(ipr_cfg->completion); /* OK for caller to continue */

    spin_lock_irqsave(&io_request_lock, io_flags);

    init_waitqueue_head(&ipr_cfg->wait_q);

    while(1)
    {
        ipr_interruptible_sleep_on(&io_request_lock, &ipr_cfg->wait_q);

        if (signal_pending(current))
        {
            if (ipr_cfg->flag & IPR_KILL_KERNEL_THREAD)
                break;
            else
            {
                /* Ignore the signal */
                md_flush_signals();
                continue;
            }
        }

        ipr_mailbox(ipr_cfg);
    }

    ipr_cfg->task_thread = NULL;

    spin_unlock_irqrestore(&io_request_lock, io_flags);

    if( ipr_cfg->completion != NULL )
        complete(ipr_cfg->completion);

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Munges the URC
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: urc - munged urc
 *---------------------------------------------------------------------------*/
static u16 ipr_adjust_urc(u32 error_index,
                             struct ipr_res_addr resource_addr,
                             u32 ioasc,
                             u32 dev_urc,
                             char *p_error_string)
{
    u16 urc;

    if (dev_urc)
        urc = ipr_error_table[error_index].dev_urc;
    else
        urc = ipr_error_table[error_index].iop_urc;

    strcpy(p_error_string, ipr_error_table[error_index].p_error);

    switch (ioasc)
    {
        case 0x01080000:
            if (!dev_urc) /* 8140 and 813x */
                strcpy(p_error_string, "IOA detected recoverable device bus error");

            if (IPR_GET_PHYSICAL_LOCATOR(resource_addr) != IPR_IOA_RESOURCE_ADDRESS)
            {
                if (dev_urc)
                    urc = 0xfffe;
                else
                    urc = 0x8130 | (resource_addr.bus & 0xf);
            }
            break;
        case 0x015D0000:
            if (!dev_urc)
            {
                if (IPR_GET_PHYSICAL_LOCATOR(resource_addr) != IPR_IOA_RESOURCE_ADDRESS)
                    urc = 0x8146;
                else /* 8145 */
                    strcpy(p_error_string, "A recoverable IOA error occurred");
            }
            break;
        case 0x04080000:
        case 0x04088000:
        case 0x06288000:
        case 0x06678000:
            urc |= (resource_addr.bus & 0xf);
            break;
        case 0x06670600:
            if (!dev_urc)
                urc |= (resource_addr.bus & 0xf);
            break;
        case 0x04080100:
            if (IPR_GET_PHYSICAL_LOCATOR(resource_addr) == IPR_IOA_RESOURCE_ADDRESS)
            {
                if (dev_urc)
                    urc = 0;
                else
                {
                    urc = 0x8150;
                    strcpy(p_error_string, "A permanent IOA failure occurred");
                }
            }
            break;
        default:
            break;
    }

    if (urc == 0x8141)
        strcpy(p_error_string, "IOA detected recoverable device error");
    else if (urc == 0x3400)
        strcpy(p_error_string, "IOA detected device error");
    else if (urc == 0xFFFB)
        strcpy(p_error_string, "SCSI bus reset occurred");

    return urc;
}

/*---------------------------------------------------------------------------
 * Purpose: Interface for us to issue requests internally to resources
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Failure
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
int ipr_do_req(struct ipr_shared_config *p_shared_cfg,
                  struct ipr_ccb *p_sis_ccb,
                  void (*done) (struct ipr_shared_config *, struct ipr_ccb *),
                  u32 timeout_in_sec)
{
    struct ipr_cmnd *p_sis_cmnd;
    u32 rc;
    ipr_host_config *ipr_cfg = (ipr_host_config *)p_shared_cfg;
    struct ipr_lun *p_lun;

    if ((ipr_cfg->shared.ioa_operational == 0) ||
        (((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0) &&
         ((p_sis_ccb->flags & IPR_ALLOW_REQ_OVERRIDE) == 0)))
    {
        ipr_dbg_trace;
        return IPR_RC_OP_NOT_SENT;
    }

    p_sis_cmnd = (struct ipr_cmnd *)p_sis_ccb;

    p_lun = ipr_get_lun_res_addr(ipr_cfg, p_sis_cmnd->ccb.p_resource->resource_address);

    if (p_lun && p_lun->stop_new_requests)
    {
        ipr_dbg_trace;
        return IPR_RC_OP_NOT_SENT;
    }

    ipr_put_sis_cmnd_to_pending(ipr_cfg, p_sis_cmnd);

    rc = ipr_build_sglist(ipr_cfg, p_sis_cmnd);

    if (rc != IPR_RC_SUCCESS)
    {
        ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);
        return rc;
    }

    p_sis_cmnd->done = done;

    p_sis_cmnd->ccb.flags |= IPR_INTERNAL_REQ;

    if (!p_sis_cmnd->ccb.p_resource->is_ioa_resource &&
             !p_sis_cmnd->ccb.p_resource->is_af)
    {
        p_sis_cmnd->ccb.flags |= IPR_GPDD_CMD;

        /* Double the timeout value to use as we will use the adapter
         as the primary timing mechanism */
        if (1 == IPR_TIMEOUT_MULTIPLIER)
            p_sis_cmnd->ccb.timeout = 0x3fff;
        else
        {
            p_sis_cmnd->ccb.timeout = timeout_in_sec;
            timeout_in_sec *= 2;
        }
    }
    else if (p_sis_cmnd->ccb.p_resource->is_ioa_resource &&
             (p_sis_cmnd->ccb.cdb[0] == IPR_SUSPEND_DEV_BUS))
    {
        /* Double the timeout value to use as we will use the adapter
         as the primary timing mechanism */
        if (1 == IPR_TIMEOUT_MULTIPLIER)
            p_sis_cmnd->ccb.timeout = 0x3fff;
        else
        {
            p_sis_cmnd->ccb.timeout = timeout_in_sec;
            timeout_in_sec *= 2;
        }
    }

    init_timer(&p_sis_cmnd->timer);

    p_sis_cmnd->timer.data = (unsigned long)ipr_cfg;
    p_sis_cmnd->timer.expires = jiffies + (timeout_in_sec * HZ);
    p_sis_cmnd->timer.function = (void (*)(unsigned long))ipr_timeout;

    add_timer(&p_sis_cmnd->timer);

    if (p_sis_cmnd->ccb.p_resource == &ipr_cfg->shared.ioa_resource)
        rc = ipr_ioa_queue(&ipr_cfg->shared, &p_sis_cmnd->ccb);
    else
        rc = ipr_queue_internal(&ipr_cfg->shared, &p_sis_cmnd->ccb);

    if (rc != IPR_RC_SUCCESS)
    {
        ipr_dbg_trace;
        del_timer(&p_sis_cmnd->timer);
        ipr_remove_sis_cmnd_from_pending(ipr_cfg, p_sis_cmnd);
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: req a reset of the adapter, log no errors, and take no dump
 * Context: Task or Interrupt level only
 * Lock State: io_request_lock assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
void ipr_req_reset(struct ipr_shared_config *p_shared_cfg)
{
    ipr_host_config *ipr_cfg = (ipr_host_config *)p_shared_cfg;

    ENTER;

    /* Prevent ourselves from getting any more requests */
    ipr_block_all_requests(ipr_cfg);

    ipr_cfg->flag |= IPR_IOA_NEEDS_RESET;

    /* Wake up task level thread to reset adapter */
    ipr_wake_task(ipr_cfg);

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: An internally generated op has timed out
 * Context: Interrupt level only
 * Lock State: no locks assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
void ipr_timeout(ipr_host_config *ipr_cfg)
{
    unsigned long io_flags = 0;

    ENTER;

    spin_lock_irqsave(&io_request_lock, io_flags);

    ipr_beg_err(KERN_ERR);
    ipr_log_err("Adapter being reset as a result of system timeout."IPR_EOL);
    ipr_log_ioa_physical_location(ipr_cfg->shared.p_location, KERN_ERR);
    ipr_end_err(KERN_ERR);

    /* Prevent ourselves from getting any more requests */
    ipr_block_all_requests(ipr_cfg);

    ipr_cfg->flag |= IPR_IOA_NEEDS_RESET;

    /* Wake up task level thread to reset adapter */
    ipr_wake_task(ipr_cfg);

    spin_unlock_irqrestore(&io_request_lock, io_flags);

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Allocate a ccb for an internal request
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Pointer to ccb
 *---------------------------------------------------------------------------*/
struct ipr_ccb * ipr_allocate_ccb(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_cmnd *p_sis_cmnd;
    ipr_host_config *ipr_cfg = (ipr_host_config *)p_shared_cfg;

    if ((ipr_cfg->flag & IPR_ALLOW_REQUESTS) == 0)
        return NULL;

    p_sis_cmnd = ipr_get_free_sis_cmnd(ipr_cfg);

    return &p_sis_cmnd->ccb;
}

/*---------------------------------------------------------------------------
 * Purpose: Free a ccb that was allocated with ipr_allocate_ccb
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_release_ccb(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_ccb *p_sis_ccb)
{
    struct ipr_cmnd *p_sis_cmnd;
    ipr_host_config *ipr_cfg = (ipr_host_config *)p_shared_cfg;

    p_sis_cmnd = (struct ipr_cmnd *)p_sis_ccb;
    ipr_put_sis_cmnd_to_free(ipr_cfg, p_sis_cmnd);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Stop the host from issuing new requests.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_block_all_requests(ipr_host_config *ipr_cfg)
{
    ENTER;

    if (0 == ipr_cfg->block_host_ops++)
    {
        ipr_cfg->flag &= ~(IPR_ALLOW_HCAMS | IPR_ALLOW_REQUESTS);

        /* Stop new requests from coming in */
        scsi_block_requests(ipr_cfg->host);
    }

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Allow the host to send requests again
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_unblock_all_requests(ipr_host_config *ipr_cfg)
{
    ENTER;

    if (0 == --ipr_cfg->block_host_ops)
    {
        spin_unlock_irq(&io_request_lock);
        scsi_unblock_requests(ipr_cfg->host);
        spin_lock_irq(&io_request_lock);

        ipr_cfg->flag |= IPR_ALLOW_HCAMS | IPR_ALLOW_REQUESTS;

        /* Send back any failed ops to the host */
        ipr_return_failed_ops(ipr_cfg);
    }

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Stop the host from issuing new requests.
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_block_midlayer_requests(ipr_host_config *ipr_cfg)
{
    ENTER;

    if (0 == ipr_cfg->block_host_ops++)
    {
        /* Stop new requests from coming in */
        scsi_block_requests(ipr_cfg->host);
    }

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Allow the host to send requests again
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_unblock_midlayer_requests(ipr_host_config *ipr_cfg)
{
    ENTER;

    if (0 == --ipr_cfg->block_host_ops)
    {
        spin_unlock_irq(&io_request_lock);
        scsi_unblock_requests(ipr_cfg->host);
        spin_lock_irq(&io_request_lock);
    }

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Returns the /dev entry name of the IOA
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_get_ioa_name(ipr_host_config *ipr_cfg,
                                char *dev_name)
{
    sprintf(dev_name, "/dev/"IPR_NAME"%d", ipr_cfg->minor_num);
}


/*---------------------------------------------------------------------------
 * Purpose: Find SES resource for given device resource address
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to resource entry or NULL
 *---------------------------------------------------------------------------*/
static struct ipr_resource_entry *ipr_get_ses_resource(ipr_host_config *ipr_cfg,
                                                             struct ipr_res_addr res_addr)
{
    struct ipr_resource_entry *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;

    /* Loop through config table to find device */
    for (p_resource_dll = ipr_cfg->shared.rsteUsedH;
         p_resource_dll != NULL;
         p_resource_dll = p_resource_dll->next)
    {
        p_resource_entry = &p_resource_dll->data;

        if ((p_resource_entry->resource_address.bus == res_addr.bus) &&
            IPR_IS_SES_DEVICE(p_resource_entry->std_inq_data))
        {
            return p_resource_entry;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Copy smart dump data from kernel space to user space.
 * Context: Task level only
 * Lock State: io_request_lock not held
 * Returns: 0           - Success
 *          others      - Failed
 *---------------------------------------------------------------------------*/
static int ipr_copy_sdt_to_user(u8 *p_dest_buffer, u32 length)
{
    u32 *p_src_buffer;
    int page_index = 0;
    int bytes_to_copy = PAGE_SIZE;
    int result = IPR_RC_SUCCESS;

    if ((p_ipr_dump_driver_header == NULL) || (p_ipr_dump_ioa_entry == NULL))
        return -ENOMEM;

    if (ipr_get_sdt_state != DUMP_OBTAINED)
        return -EIO;

    result = copy_to_user(p_dest_buffer, p_ipr_dump_driver_header,
                          sizeof(struct ipr_dump_driver_header));

    p_dest_buffer += sizeof(struct ipr_dump_driver_header);

    length -= sizeof(struct ipr_dump_driver_header);

    if (!result)
    {
        result = copy_to_user(p_dest_buffer, &p_ipr_dump_ioa_entry->header,
                              sizeof(struct ipr_dump_entry_header));

        p_dest_buffer += sizeof(struct ipr_dump_entry_header);

        length -= sizeof(struct ipr_dump_entry_header);
    }

    if (!result)
    {
        result = copy_to_user(p_dest_buffer, &p_ipr_dump_ioa_entry->sdt,
                              sizeof(struct ipr_sdt));

        p_dest_buffer += sizeof(struct ipr_sdt);

        length -= sizeof(struct ipr_sdt);
    }

    while ((p_src_buffer = p_ipr_dump_ioa_entry->p_ioa_data[page_index]))
    {
        if (length)
        {
            if (length > PAGE_SIZE)
                length -= PAGE_SIZE;
            else
            {
                bytes_to_copy = length;
                length = 0;
            }

            if (!result)
            {
                result = copy_to_user(p_dest_buffer, p_src_buffer, bytes_to_copy);
                p_dest_buffer += bytes_to_copy;
            }
        }

        ipr_free_page(p_src_buffer);
        p_ipr_dump_ioa_entry->p_ioa_data[page_index] = NULL;
        page_index++;
    }

    return result;
}


/*---------------------------------------------------------------------------
 * Purpose: Obtain smart dump data
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          others      - Failed
 * Note: Since we could be running on both big and little endian machines
 *       we store the dump as if we were running on a big endian machine
 *       so the analysis tools only have to deal with one format of dump
 *---------------------------------------------------------------------------*/
static int ipr_get_ioa_smart_dump(ipr_host_config *ipr_cfg)
{
    unsigned long mailbox, sdt_start_addr, sdt_register_sel, sdt_entry_word, dump_data_buffer;
    u32 num_table_entries, num_entries_used, start_offset, end_offset, swap;
    u32 rc = PCIBIOS_SUCCESSFUL, ret_val;
    u32 byte_index, bytes_to_copy, bytes_copied;
    u32 *p_buffer;
    const struct ipr_ioa_cfg_t *p_ioa_cfg = ipr_cfg->p_ioa_cfg;
    struct ipr_sdt *p_sdt;
    int sdt_entry_index;
    unsigned long timeout;

    ENTER;

    if ((p_ipr_dump_ioa_entry == NULL) || (p_ipr_dump_driver_header == NULL))
    {
        ipr_trace;
        return -EINVAL;
    }

    if (p_ioa_cfg->cpu_rst_support == IPR_CPU_RST_SUPPORT_CFGSPC_403RST_BIT)
    {
        /* Hold the 403 in reset so we can get the dump */
        rc = pci_write_config_dword(ipr_cfg->pdev, IPR_RESET_403_OFFSET, IPR_RESET_403);
    }

    if (rc != PCIBIOS_SUCCESSFUL)
    {
        ipr_trace;
        return -ENXIO;
    }

    if (ipr_cfg->shared.ioa_mailbox == (unsigned long)NULL)
    {
        ipr_trace;
        return -ENXIO;
    }

    mailbox = readl(ipr_cfg->shared.ioa_mailbox);
    sdt_register_sel = (mailbox & ipr_cfg->p_ioa_cfg->mbx_bar_sel_mask) >>
        ipr_cfg->p_ioa_cfg->mkr_bar_sel_shift;
    start_offset = mailbox & ipr_cfg->p_ioa_cfg->mbx_addr_mask;

    /* Figure out where the Smart Dump Table is located */
    switch (sdt_register_sel)
    {
        case IPR_SDT_FMT1_BAR0_SEL:
            sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[0] + start_offset;
            p_ipr_dump_ioa_entry->format = IPR_SDT_FMT1;
            swap = 0;
            break;
        case IPR_SDT_FMT1_BAR2_SEL:
            sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[2] + start_offset;
            p_ipr_dump_ioa_entry->format = IPR_SDT_FMT1;
            swap = 1;
            break;
        case IPR_SDT_FMT1_BAR3_SEL:
            sdt_start_addr = ipr_cfg->shared.hdw_bar_addr[3] + start_offset;
            p_ipr_dump_ioa_entry->format = IPR_SDT_FMT1;
            swap = 1;
            break;
        case IPR_SDT_FMT2_BAR0_SEL:
        case IPR_SDT_FMT2_BAR1_SEL:
        case IPR_SDT_FMT2_BAR2_SEL:
        case IPR_SDT_FMT2_BAR3_SEL:
        case IPR_SDT_FMT2_BAR4_SEL:
            p_ipr_dump_ioa_entry->format = IPR_SDT_FMT2;
            sdt_start_addr = mailbox;
            swap = 0;
            break;
        default:
            ipr_log_err("Invalid SDT format: %lx"IPR_EOL, sdt_register_sel);
            sdt_start_addr = 0;
            swap = 0;
            break;
    }

    if (sdt_start_addr != 0)
    {
        ipr_log_err("Dump of IOA at %s initiated."IPR_EOL,
                       ipr_cfg->shared.ioa_host_str);

        /* Determine a timeout value to use */
        timeout = jiffies + IPR_MAX_DUMP_FETCH_TIME;

        /* Initialize the overall dump header */
        p_ipr_dump_driver_header->header.total_length =
            htosis32(sizeof(struct ipr_dump_driver_header));

        p_ipr_dump_driver_header->header.num_elems = htosis32(1);

        p_ipr_dump_driver_header->header.first_entry_offset =
            htosis32(sizeof(struct ipr_dump_header));

        p_ipr_dump_driver_header->header.status = htosis32(IPR_RC_SUCCESS);

        /* IOA location data */
        p_ipr_dump_driver_header->location_entry.header.length =
            htosis32(sizeof(struct ipr_dump_location_entry) -
                     sizeof(struct ipr_dump_entry_header));
        p_ipr_dump_driver_header->location_entry.header.id = htosis32(IPR_DUMP_TEXT_ID);

        strcpy(p_ipr_dump_driver_header->location_entry.location,
               ipr_cfg->shared.ioa_host_str);

        /* Internal trace table entry */
        ipr_copy_internal_trace_for_dump(&ipr_cfg->shared,
                                            p_ipr_dump_driver_header->trace_entry.trace,
                                            IPR_DUMP_TRACE_ENTRY_SIZE);

        p_ipr_dump_driver_header->header.num_elems =
            htosis32(sistoh32(p_ipr_dump_driver_header->header.num_elems) + 1);

        p_ipr_dump_driver_header->trace_entry.header.length =
            htosis32(sizeof(struct ipr_dump_trace_entry) -
                     sizeof(struct ipr_dump_entry_header));
        p_ipr_dump_driver_header->trace_entry.header.id =
            htosis32(IPR_DUMP_TRACE_ID);

        /* IOA Dump entry */
        p_ipr_dump_ioa_entry->header.length = 0;
        p_ipr_dump_ioa_entry->header.id = htosis32(IPR_DUMP_IOA_DUMP_ID);

        /* Update dump_header */
        p_ipr_dump_driver_header->header.total_length =
            htosis32(sistoh32(p_ipr_dump_driver_header->header.total_length) +
                     sizeof(struct ipr_dump_entry_header));

        p_ipr_dump_driver_header->header.num_elems =
            htosis32(sistoh32(p_ipr_dump_driver_header->header.num_elems) + 1);

        p_buffer = (u32 *)&p_ipr_dump_ioa_entry->sdt;

        /* Get the IOA Smart Dump Table */
        if (p_ipr_dump_ioa_entry->format == IPR_SDT_FMT1)
        {
            rc = IPR_RC_SUCCESS;

            for (byte_index = 0;
                 byte_index < sizeof(struct ipr_sdt);
                 byte_index += 4)
            {
                if (swap)
                    p_buffer[byte_index/4] = swab32(readl(sdt_start_addr + byte_index));
                else
                    p_buffer[byte_index/4] = readl(sdt_start_addr + byte_index);
            }
        }
        else
        {
            rc = ipr_get_ldump_data_section(&ipr_cfg->shared,
                                               sdt_start_addr, p_buffer,
                                               sizeof(struct ipr_sdt)/sizeof(u32));
        }

        /* First entries in sdt are actually a list of dump addresses and
         lengths to gather the real dump data.  p_sdt represents the pointer
         to the ioa generated dump table.  Dump data will be extracted based
         on entries in this table */
        p_sdt = &p_ipr_dump_ioa_entry->sdt;

        /* Smart Dump table is ready to use and the first entry is valid */
        if  ((rc == IPR_RC_FAILED) ||
             ((sistoh32(p_sdt->sdt_header.dump_state) != IPR_FMT1_SDT_READY_TO_USE) &&
              (sistoh32(p_sdt->sdt_header.dump_state) != IPR_FMT2_SDT_READY_TO_USE)))
        {
            ipr_log_err("Dump of IOA at %s failed. Dump table not valid."IPR_EOL,
                           ipr_cfg->shared.ioa_host_str);
            p_ipr_dump_driver_header->header.status = htosis32(IPR_RC_FAILED);
            ipr_get_sdt_state = DUMP_OBTAINED;
            return IPR_RC_SUCCESS;
        }

        num_table_entries = sistoh32(p_sdt->sdt_header.num_entries);
        num_entries_used = sistoh32(p_sdt->sdt_header.num_entries_used);

        for (sdt_entry_index = 0;
             (sdt_entry_index < num_entries_used) &&
                 (sdt_entry_index < num_table_entries) &&
                 (sdt_entry_index < IPR_NUM_SDT_ENTRIES) &&
                 time_after_eq(timeout, jiffies) &&
                 (sistoh32(p_ipr_dump_ioa_entry->header.length) < IPR_MAX_IOA_DUMP_SIZE);
             sdt_entry_index++)
        {
            if (p_sdt->sdt_entry[sdt_entry_index].valid_entry)
            {
                sdt_entry_word = sistoh32(p_sdt->sdt_entry[sdt_entry_index].bar_str_offset);
                sdt_register_sel = (sdt_entry_word & ipr_cfg->p_ioa_cfg->mbx_bar_sel_mask)
                    >> ipr_cfg->p_ioa_cfg->mkr_bar_sel_shift;
                start_offset = sdt_entry_word & ipr_cfg->p_ioa_cfg->mbx_addr_mask;
                end_offset = sistoh32(p_sdt->sdt_entry[sdt_entry_index].end_offset);

                /* Figure out where the Smart Dump Table is located */
                switch (sdt_register_sel)
                {
                    case IPR_SDT_FMT1_BAR0_SEL:
                        dump_data_buffer = ipr_cfg->shared.hdw_bar_addr[0] + start_offset;
                        swap = 0;
                        break;
                    case IPR_SDT_FMT1_BAR2_SEL:
                        dump_data_buffer = ipr_cfg->shared.hdw_bar_addr[2] + start_offset;
                        swap = 1;
                        break;
                    case IPR_SDT_FMT1_BAR3_SEL:
                        dump_data_buffer = ipr_cfg->shared.hdw_bar_addr[3] + start_offset;
                        swap = 1;
                        break;
                    case IPR_SDT_FMT2_BAR0_SEL:
                    case IPR_SDT_FMT2_BAR1_SEL:
                    case IPR_SDT_FMT2_BAR2_SEL:
                    case IPR_SDT_FMT2_BAR3_SEL:
                    case IPR_SDT_FMT2_BAR4_SEL:
                        dump_data_buffer = sdt_entry_word;
                        swap = 0;
                        break;
                    default:
                        dump_data_buffer = 0;
                        swap = 0;
                        break;
                }

                if (dump_data_buffer != 0)
                {
                    /* Dump_header will be updated after all ioa sdt
                       dump entries have been obtained. */
                    /* Copy data from adapter to driver buffers */
                    bytes_to_copy = (end_offset - start_offset);
                    bytes_copied = ipr_sdt_copy(ipr_cfg, dump_data_buffer,
                                                   bytes_to_copy, swap, timeout);

                    /* Update dump_entry_header length */
                    p_ipr_dump_ioa_entry->header.length =
                        htosis32(sistoh32(p_ipr_dump_ioa_entry->header.length) + bytes_copied);

                    if (bytes_copied != bytes_to_copy)
                    {
                        ipr_log_err("Dump of IOA at %s completed."IPR_EOL,
                                       ipr_cfg->shared.ioa_host_str);
                        p_ipr_dump_driver_header->header.status = htosis32(IPR_RC_QUAL_SUCCESS);

                        if (time_before_eq(timeout, jiffies))
                        {
                            ipr_dbg_trace;
                        }
                        else
                        {
                            ipr_dbg_trace;
                        }

                        p_ipr_dump_driver_header->header.total_length =
                            htosis32(sistoh32(p_ipr_dump_driver_header->header.total_length) +
                            sistoh32(p_ipr_dump_ioa_entry->header.length));
                        ipr_get_sdt_state = DUMP_OBTAINED;
                        return IPR_RC_SUCCESS;
                    }
                }
            }
        }

        ipr_log_err("Dump of IOA at %s completed."IPR_EOL,
                       ipr_cfg->shared.ioa_host_str);

        /* Update dump_header */
        p_ipr_dump_driver_header->header.total_length =
            htosis32(sistoh32(p_ipr_dump_driver_header->header.total_length) +
            sistoh32(p_ipr_dump_ioa_entry->header.length));

        ipr_get_sdt_state = DUMP_OBTAINED;
        ret_val = IPR_RC_SUCCESS;
    }
    else
        ret_val = IPR_RC_FAILED;

    LEAVE;

    return ret_val;
}


/*---------------------------------------------------------------------------
 * Purpose: Copy data from PCI adapter to driver buffer to user space.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Return:  number of bytes copied to copy buffer
 *
 * NOTE:  lengths of requests MUST be 4 byte bounded.
 *---------------------------------------------------------------------------*/
static int ipr_sdt_copy(ipr_host_config *ipr_cfg, unsigned long pci_address,
                           u32 length, u32 swap, unsigned long timeout)
{
    int bytes_copied = 0;
    int current_length, rc;
    u32 *p_page = NULL;

    while ((bytes_copied < length) && time_after_eq(timeout, jiffies) &&
           ((sistoh32(p_ipr_dump_ioa_entry->header.length) + bytes_copied) <
            IPR_MAX_IOA_DUMP_SIZE))
    {
        if ((p_ipr_dump_ioa_entry->page_offset >= PAGE_SIZE) ||
            (p_ipr_dump_ioa_entry->page_offset == 0))
        {
            p_page = ipr_get_free_page(GFP_ATOMIC);

            if (NULL == p_page)
            {
                ipr_trace;
                return bytes_copied;
            }

            p_ipr_dump_ioa_entry->page_offset = 0;
            p_ipr_dump_ioa_entry->p_ioa_data[p_ipr_dump_ioa_entry->next_page_index] = p_page;
            p_ipr_dump_ioa_entry->next_page_index++;
        }
        else
            p_page = p_ipr_dump_ioa_entry->p_ioa_data[p_ipr_dump_ioa_entry->next_page_index - 1];

        if (p_ipr_dump_ioa_entry->format == IPR_SDT_FMT1)
        {
            if (swap)
            {
                p_page[p_ipr_dump_ioa_entry->page_offset/4] =
                    swab32(readl(pci_address + bytes_copied));
            }
            else
            {
                p_page[p_ipr_dump_ioa_entry->page_offset/4] =
                    readl(pci_address + bytes_copied);
            }

            p_ipr_dump_ioa_entry->page_offset += 4;
            bytes_copied += 4;
        }
        else
        {
            /* Copy the min of remaining length to copy and the remaining space in this page */
            current_length = IPR_MIN((length - bytes_copied),
                                        (PAGE_SIZE - p_ipr_dump_ioa_entry->page_offset));

            rc = ipr_get_ldump_data_section(&ipr_cfg->shared,
                                               pci_address + bytes_copied,
                                               (u32*)&p_page[p_ipr_dump_ioa_entry->page_offset/4],
                                               (current_length / sizeof (u32)));

            if (rc == IPR_RC_SUCCESS)
            {
                p_ipr_dump_ioa_entry->page_offset += current_length;
                bytes_copied += current_length;
            }
            else
            {
                ipr_trace;
                break;
            }

            /* Since our dump could take a while, we want to let other people
             have some processor time while we dump */
            ipr_sleep(1);
        }
    }

    return bytes_copied;
}

/*---------------------------------------------------------------------------
 * Purpose: delay usecs
 * Context: Task level only
 * Lock State: io_request_lock can be in any state
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_udelay(signed long delay)
{
    if ((delay/1000) > MAX_UDELAY_MS)
        mdelay(delay/1000);
    else
        udelay(delay);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Sleeps for delay msecs
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_sleep(signed long delay)
{
    spin_unlock_irq(&io_request_lock);
    ipr_sleep_no_lock(delay);
    spin_lock_irq(&io_request_lock);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Sleeps for delay msecs
 * Context: Task level only
 * Lock State: io_request_lock assumed to not be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_sleep_no_lock(signed long delay)
{
    DECLARE_WAIT_QUEUE_HEAD(internal_wait);

    sleep_on_timeout(&internal_wait, (delay * HZ)/1000);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Print to the kernel log and to the current tty if appropriate
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_print_tty(char *s, ...)
{
    va_list args;
    struct tty_struct *p_tty;
    char *endline;
    char *p_buffer;

    va_start(args, s);
    vsprintf(ipr_buf, s, args);
    va_end(args);

    printk(ipr_buf);

    if (!ipr_init_finished)
    {
        /* Print to the tty as well if we can */
        p_tty = current->tty;
        p_buffer = ipr_buf;

        if (p_tty && p_tty->driver.write)
        {
            endline = strchr(p_buffer, '\n');
            if (endline)
            {
                *endline = '\0';
                strcat(p_buffer, "\015\012");
            }

            /* Strip off KERN_ERR part */
            endline = strchr(p_buffer, '<');
            if (endline == p_buffer)
            {
                p_buffer += 3;
            }

            /* Strip off our prefix */
            endline = strstr(p_buffer, IPR_ERR);
            if (endline)
            {
                p_buffer += (IPR_ERR_LEN + 2);
            }
            else
            {
                endline = strstr(p_buffer, IPR_NAME);
                if (endline)
                {
                    p_buffer += (IPR_NAME_LEN + 2);
                }
            }

            p_tty->driver.write(p_tty, 0, p_buffer, strlen(p_buffer));
        }
    }

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Translate ipr malloc flags to kmalloc flags
 * Context: Task or interrupt level
 * Lock State: no locks required
 * Returns: kmalloc flags
 *---------------------------------------------------------------------------*/
static u32 ipr_xlate_malloc_flags(u32 flags)
{
    u32 kmalloc_flags = 0;

    if (flags & IPR_ALLOC_CAN_SLEEP)
        kmalloc_flags |= GFP_KERNEL;
    else if (flags & IPR_ALLOC_ATOMIC)
        kmalloc_flags |= GFP_ATOMIC;
    else if (ipr_mem_debug)
        panic(IPR_ERR"Invalid kmalloc flags: %x"IPR_EOL, flags);
    return kmalloc_flags;
}

/*---------------------------------------------------------------------------
 * Purpose: kmalloc wrapper
 * Context: Task level only
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
void *ipr_kmalloc(u32 size, u32 flags)
{
    void * rc;

    rc = kmalloc(size, ipr_xlate_malloc_flags(flags));
    if (ipr_mem_debug && rc)
        ipr_kmalloced_mem += size;
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: kmalloc memory and zero it
 * Context: Task level only
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
void *ipr_kcalloc(u32 size, u32 flags)
{
    void * rc;
    rc = kmalloc(size, ipr_xlate_malloc_flags(flags));
    if (rc)
    {
        memset(rc, 0, size);
        if (ipr_mem_debug)
            ipr_kmalloced_mem += size;
    }
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: __get_free_pages wrapper
 * Context: Task level only
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
static void *ipr_get_free_pages(u32 flags, u32 order)
{
    void *rc = (void *)__get_free_pages(flags, order);

    if (ipr_mem_debug && rc)
        ipr_kmalloced_mem += ((1u << order) * PAGE_SIZE);
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: __get_free_page wrapper
 * Context: Task level only
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
static void *ipr_get_free_page(u32 flags)
{
    void *rc = (void *)__get_free_page(flags);

    if (ipr_mem_debug && rc)
        ipr_kmalloced_mem += PAGE_SIZE;
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: free_pages wrapper
 * Context: Task level only
 * Lock State: no locks required
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_free_pages(void *ptr, u32 order)
{
    free_pages((unsigned long)ptr, order);
    if (ipr_mem_debug)
        ipr_kmalloced_mem -= ((1u << order) * PAGE_SIZE);
}

/*---------------------------------------------------------------------------
 * Purpose: free_page wrapper
 * Context: Task level only
 * Lock State: no locks required
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_free_page(void *ptr)
{
    free_page((unsigned long)ptr);
    if (ipr_mem_debug)
        ipr_kmalloced_mem -= PAGE_SIZE;
}

/*---------------------------------------------------------------------------
 * Purpose: wrapper function to allocate mapped, DMA-able storage
 * Context: Task or interrupt level
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
void *ipr_dma_malloc(struct ipr_shared_config *p_shared_cfg,
                        u32 size, ipr_dma_addr *p_dma_addr, u32 flags)
{
    ipr_host_config *ipr_cfg;
    void *p_buf;

    ipr_cfg = (ipr_host_config *)p_shared_cfg;

    if (p_dma_addr == NULL)
        return NULL;

    p_buf = kmalloc(size, ipr_xlate_malloc_flags(flags) | IPR_GFP_DMA );

    if (p_buf == NULL)
        return NULL;

    *p_dma_addr = pci_map_single(ipr_cfg->pdev, p_buf, size, PCI_DMA_BIDIRECTIONAL);

    if (*p_dma_addr == NO_TCE)
    {
        kfree(p_buf);
        p_buf = NULL;
    }
    else if (ipr_mem_debug)
        ipr_kmalloced_mem += size;

    return p_buf;
}

/*---------------------------------------------------------------------------
 * Purpose: wrapper function to allocate mapped, zeroed, DMA-able storage
 * Context: Task or interrupt level
 * Lock State: no locks required
 * Returns: pointer to storage or NULL on failure
 *---------------------------------------------------------------------------*/
void *ipr_dma_calloc(struct ipr_shared_config *p_shared_cfg,
                        u32 size, ipr_dma_addr *p_dma_addr, u32 flags)
{
    void *p_buf;

    p_buf = ipr_dma_malloc(p_shared_cfg, size, p_dma_addr, flags);
    if (p_buf)
        memset(p_buf, 0, size);

    return p_buf;
}

/*---------------------------------------------------------------------------
 * Purpose: kfree wrapper
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_kfree(void *ptr, u32 size)
{
    if (ptr)
    {
        kfree(ptr);
        if (ipr_mem_debug)
            ipr_kmalloced_mem -= size;
    }
}

/*---------------------------------------------------------------------------
 * Purpose: wrapper function to free storage allocated with ipr_dma_malloc
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_dma_free(struct ipr_shared_config *p_shared_cfg,
                     u32 size, void *ptr, ipr_dma_addr dma_addr)
{
    ipr_host_config *ipr_cfg;
    ipr_cfg = (ipr_host_config *)p_shared_cfg;

    if (ptr)
    {
        pci_unmap_single(ipr_cfg->pdev, dma_addr, size, PCI_DMA_BIDIRECTIONAL);
        kfree(ptr);
        if (ipr_mem_debug)
            ipr_kmalloced_mem -= size;
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Print out a sense buffer with minimal formatting
 * Returns: nothing
 * Notes: This is primarily used for debug purposes
 *---------------------------------------------------------------------------*/
#define IPR_PRINT_SENSE_BYTES        18
static void ipr_print_sense(u8 cmd, unsigned char *p_buf)
{
    int byte, len;
    unsigned char buffer[(IPR_PRINT_SENSE_BYTES * 3) + 1];

    if (!ipr_debug &&
        (p_buf[2] == NOT_READY) &&
        (p_buf[12] == 0x40) &&  /* Diagnostic Failure */
        (p_buf[13] == 0x85))    /* Component x85 braindead? */
        return;

    if (ipr_sense_valid(p_buf[0]))
    {
        for (byte = 0, len = 0; byte < IPR_PRINT_SENSE_BYTES; byte++)
            len += sprintf(buffer + len, "%02X ", p_buf[byte]);
        ipr_log_err("Cmd 0x%02x failed with Sense buffer: %s"IPR_EOL, cmd, buffer);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Go to sleep on a waitqueue.
 * Context: Task level only
 * Lock State: lock passed in paramter list assumed to be held
 * Returns: timeout
 *---------------------------------------------------------------------------*/
static signed long ipr_sleep_on_timeout(spinlock_t *p_lock,
                                           wait_queue_head_t *p_wait_head, long timeout)
{
    wait_queue_t wait_q_entry;

    init_waitqueue_entry(&wait_q_entry, current);

    /* Set our task's state to sleeping and add ourselves to the wait queue
     prior to releasing the spinlock */
    set_current_state(TASK_UNINTERRUPTIBLE);

    add_wait_queue(p_wait_head, &wait_q_entry);

    spin_unlock_irq(p_lock);

    timeout = schedule_timeout(timeout);

    spin_lock_irq(p_lock);

    remove_wait_queue(p_wait_head, &wait_q_entry);

    return timeout;
}

/*---------------------------------------------------------------------------
 * Purpose: Go to sleep on a waitqueue.
 * Context: Task level only
 * Lock State: lock passed in paramter list assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_interruptible_sleep_on(spinlock_t *p_lock,
                                          wait_queue_head_t *p_wait_head)
{
    wait_queue_t wait_q_entry;

    init_waitqueue_entry(&wait_q_entry, current);

    /* Set our task's state to sleeping and add ourselves to the wait queue
     prior to releasing the spinlock */
    set_current_state(TASK_INTERRUPTIBLE);

    add_wait_queue(p_wait_head, &wait_q_entry);

    spin_unlock_irq(p_lock);

    schedule();

    spin_lock_irq(p_lock);

    remove_wait_queue(p_wait_head, &wait_q_entry);

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Go to sleep on a waitqueue.
 * Context: Task level only
 * Lock State: lock passed in paramter list assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_sleep_on(spinlock_t *p_lock,
                            wait_queue_head_t *p_wait_head)
{
    wait_queue_t wait_q_entry;

    init_waitqueue_entry(&wait_q_entry, current);

    /* Set our task's state to sleeping and add ourselves to the wait queue
     prior to releasing the spinlock */
    set_current_state(TASK_UNINTERRUPTIBLE);

    add_wait_queue(p_wait_head, &wait_q_entry);

    spin_unlock_irq(p_lock);

    schedule();

    spin_lock_irq(p_lock);

    remove_wait_queue(p_wait_head, &wait_q_entry);

    return;
}

void * ipr_mem_copy( void * s, const void * ct, int n)
{
    return memcpy(s, ct, n);
}

void * ipr_mem_set( void * s, int c, int n)
{
    return memset(s, c, n);
}

static Scsi_Host_Template driver_template = IPR;

#include "scsi_module.c"
