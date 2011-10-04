/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


/****************************************************************************
              Please see revision.notes for revision history.
*****************************************************************************/

/*
* String arrays
*/
#define LINESIZE    256
#define MAXARGS      26

/*
* Include files
*/
#include <linux/config.h>
#include <linux/module.h>

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif  /* LINUX_VERSION_CODE not defined */

/* Restrict compilation to 2.4.0 or greater */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#error "This driver does not support kernel versions earlier than 2.4.0"
#endif

#include "qla_settings.h"

static int num_hosts = 0;       /* ioctl related  */
static int apiHBAInstance = 0;  /* ioctl related keeps track of API HBA Instance */

#if QL_TRACE_MEMORY
static unsigned long mem_trace[1000];
static unsigned long mem_id[1000];
#endif

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>

#define  APIDEV        1

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
/*
* We must always allow SHUTDOWN_SIGS.  Even if we are not a module,
* the host drivers that we are using may be loaded as modules, and
* when we unload these,  we need to ensure that the error handler thread
* can be shut down.
*
* Note - when we unload a module, we send a SIGHUP.  We mustn't
* enable SIGTERM, as this is how the init shuts things down when you
* go to single-user mode.  For that matter, init also sends SIGKILL,
* so we mustn't enable that one either.  We use SIGHUP instead.  Other
* options would be SIGPWR, I suppose.
*/
#define SHUTDOWN_SIGS	(sigmask(SIGHUP))
#include "sd.h"
#include "scsi.h"
#include "hosts.h"

#include "scsi_dump.h"

#if defined(ISP2300)
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include "qla_ip.h"
#endif

#if defined(FC_SCTP_SUPPORT)
#endif

#include "exioct.h"
#include "qla2x00.h"


#define UNIQUE_FW_NAME                     /* unique F/W array names */
#if defined(ISP2100)
#include "ql2100_fw.h"                     /* ISP RISC 2100 TP code */
#endif
#if defined(ISP2200)
#include "ql2200_fw.h"                     /* ISP RISC 2200 TP code */
#endif

#if defined(ISP2300)
#include "ql2300ipx_fw.h"                  /* ISP RISC 2300/6312 IPX code */
#include "ql2322ipx_fw.h"                  /* ISP RISC 2322/6322 IPX code */
#include "ql24xx_fw.h"                     /* ISP RISC 24xx/54xx IPX code */
#endif


#include "qla_cfg.h"
#include "qlfolimits.h"

#include "qla_gbl.h"
#include "qla_devtbl.h"


#if NO_LONG_DELAYS
#define  SYS_DELAY(x)		qla2x00_sleep(x)
#define  QLA2100_DELAY(sec)  qla2x00_sleep(sec * HZ)
#define NVRAM_DELAY() qla2x00_sleep(10) /* 10 microsecond delay */
#define  UDELAY(x)		qla2x00_sleep(x)
#else
#define  SYS_DELAY(x)		udelay(x);barrier()
#define  QLA2100_DELAY(sec)  mdelay(sec * HZ)
#define NVRAM_DELAY() udelay(10) /* 10 microsecond delay */
#define  UDELAY(x)		udelay(x)
#endif

#define  PCI_POSTING(a) (RD_REG_WORD(a))
#define  INVALID_HANDLE    (MAX_OUTSTANDING_COMMANDS+1)

#define  ABORTS_ACTIVE  ((test_bit(LOOP_RESET_NEEDED, &ha->dpc_flags)) || \
			(test_bit(DEVICE_RESET_NEEDED, &ha->dpc_flags)) || \
			(test_bit(DEVICE_ABORT_NEEDED, &ha->dpc_flags)) || \
			(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)))

#define  STATIC static

#define  OFFSET(w)   (((u_long) &w) & 0xFFFF)  /* 256 byte offsets */

/*
 * LOCK MACROS
 */

#define QLA_MBX_REG_LOCK(ha)	\
    spin_lock_irqsave(&(ha)->mbx_reg_lock, mbx_flags);
#define QLA_MBX_REG_UNLOCK(ha)	\
    spin_unlock_irqrestore(&(ha)->mbx_reg_lock, mbx_flags);

#define	WATCH_INTERVAL		1       /* number of seconds */
#define	START_TIMER(f, h, w)	\
{ \
init_timer(&(h)->timer); \
(h)->timer.expires = jiffies + w * HZ;\
(h)->timer.data = (unsigned long) h; \
(h)->timer.function = (void (*)(unsigned long))f; \
add_timer(&(h)->timer); \
(h)->timer_active = 1;\
}

#define	RESTART_TIMER(f, h, w)	\
{ \
mod_timer(&(h)->timer,jiffies + w * HZ); \
}

#define	STOP_TIMER(f, h)	\
{ \
del_timer_sync(&(h)->timer); \
(h)->timer_active = 0;\
}

#define COMPILE 0

#if defined(ISP2100)
#define DRIVER_NAME "qla2100"
#endif
#if defined(ISP2200)
#define DRIVER_NAME "qla2200"
#endif
#if defined(ISP2300)
#define DRIVER_NAME "qla2300"
#endif

#define QLA_DRVR_VERSION_LEN	40
static char qla2x00_version_str[QLA_DRVR_VERSION_LEN];
typedef unsigned long paddr32_t;

/* proc info string processing */
struct info_str {
	char	*buffer;
	int	length;
	off_t	offset;
	int	pos;
};


/*
*  QLogic Driver support Function Prototypes.
*/
STATIC void copy_mem_info(struct info_str *, char *, int);
STATIC int copy_info(struct info_str *, char *, ...);

STATIC uint8_t qla2x00_register_with_Linux(scsi_qla_host_t *ha,
			uint8_t maxchannels);
STATIC int qla2x00_done(scsi_qla_host_t *);
static void qla2x00_select_queue_depth(struct Scsi_Host *, Scsi_Device *);

STATIC void qla2x00_timer(scsi_qla_host_t *);

STATIC uint8_t qla2x00_mem_alloc(scsi_qla_host_t *);

static void qla2x00_dump_regs(struct Scsi_Host *host);
#if STOP_ON_ERROR
static void qla2x00_panic(char *, struct Scsi_Host *host);
#endif
void qla2x00_print_scsi_cmd(Scsi_Cmnd *cmd);

#if 0
STATIC void qla2x00_abort_pending_queue(scsi_qla_host_t *ha, uint32_t stat);
#endif

STATIC void qla2x00_mem_free(scsi_qla_host_t *ha);
void qla2x00_do_dpc(void *p);

static inline void qla2x00_callback(scsi_qla_host_t *ha, Scsi_Cmnd *cmd);

static inline void qla2x00_enable_intrs(scsi_qla_host_t *);
static inline void qla2x00_disable_intrs(scsi_qla_host_t *);

static void qla2x00_extend_timeout(Scsi_Cmnd *cmd, int timeout);

static int  qla2x00_get_tokens(char *line, char **argv, int maxargs );

/*
*  QLogic ISP2x00 Hardware Support Function Prototypes.
*/
STATIC void qla2x00_cfg_persistent_binding(scsi_qla_host_t *ha);
STATIC uint8_t qla2x00_initialize_adapter(scsi_qla_host_t *);
STATIC uint8_t qla2x00_isp_firmware(scsi_qla_host_t *);
STATIC int qla2x00_iospace_config(scsi_qla_host_t *);
STATIC uint8_t qla2x00_pci_config(scsi_qla_host_t *);
STATIC uint8_t qla2x00_set_cache_line(scsi_qla_host_t *);
STATIC uint8_t qla2x00_chip_diag(scsi_qla_host_t *);
STATIC uint8_t qla2x00_setup_chip(scsi_qla_host_t *ha);
STATIC uint8_t qla2x00_init_rings(scsi_qla_host_t *ha);
STATIC void    qla2x00_init_response_q_entries(scsi_qla_host_t *ha);
STATIC uint8_t qla2x00_fw_ready(scsi_qla_host_t *ha);
#if defined(ISP2100)
STATIC uint8_t qla2100_nvram_config(scsi_qla_host_t *);
#else
STATIC uint8_t qla2x00_nvram_config(scsi_qla_host_t *);
#endif
STATIC uint8_t qla2x00_loop_reset(scsi_qla_host_t *ha);
STATIC uint8_t qla2x00_abort_isp(scsi_qla_host_t *);
STATIC uint8_t qla2x00_loop_resync(scsi_qla_host_t *);

STATIC void qla2x00_nv_write(scsi_qla_host_t *, uint16_t);
STATIC void qla2x00_nv_deselect(scsi_qla_host_t *ha);
STATIC void qla2x00_poll(scsi_qla_host_t *);
STATIC void qla2x00_init_tgt_map(scsi_qla_host_t *);
STATIC fc_port_t *qla2x00_alloc_fcport(scsi_qla_host_t *, int);
STATIC void qla2x00_reset_adapter(scsi_qla_host_t *);
STATIC void qla2x00_enable_lun(scsi_qla_host_t *);
STATIC void qla2x00_isp_cmd(scsi_qla_host_t *);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)
STATIC void qla2x00_process_risc_intrs(scsi_qla_host_t *);
#endif
STATIC void qla2x00_isr(scsi_qla_host_t *, uint16_t,  uint8_t *);
STATIC void qla2x00_rst_aen(scsi_qla_host_t *);

STATIC void qla2x00_process_response_queue(scsi_qla_host_t *);
STATIC void qla2x00_status_entry(scsi_qla_host_t *, sts_entry_t *);
STATIC void qla2x00_status_cont_entry(scsi_qla_host_t *, sts_cont_entry_t *);
STATIC void qla2x00_process_good_request(struct scsi_qla_host * , int , int );
STATIC void qla2x00_error_entry(scsi_qla_host_t *, sts_entry_t *);
STATIC void qla2x00_ms_entry(scsi_qla_host_t *, ms_iocb_entry_t *);

STATIC void qla2x00_restart_queues(scsi_qla_host_t *, uint8_t);
STATIC void qla2x00_abort_queues(scsi_qla_host_t *, uint8_t);

STATIC uint16_t qla2x00_get_nvram_word(scsi_qla_host_t *, uint32_t);
STATIC uint16_t qla2x00_nvram_request(scsi_qla_host_t *, uint32_t);
STATIC uint16_t qla2x00_debounce_register(volatile uint16_t *);

STATIC request_t *qla2x00_req_pkt(scsi_qla_host_t *);
STATIC request_t *qla2x00_ms_req_pkt(scsi_qla_host_t *, srb_t *);
STATIC uint8_t qla2x00_configure_hba(scsi_qla_host_t *ha);
STATIC void qla2x00_reset_chip(scsi_qla_host_t *ha);

STATIC void qla2x00_display_fc_names(scsi_qla_host_t *ha);
void qla2x00_dump_requests(scsi_qla_host_t *ha);
static void qla2x00_get_properties(scsi_qla_host_t *ha, char *string);
STATIC uint8_t qla2x00_find_propname(scsi_qla_host_t *ha,
		char *propname, char *propstr, char *db, int siz);
static int qla2x00_get_prop_16chars(scsi_qla_host_t *ha,
		char *propname, char *propval, char *cmdline);
static char *qla2x00_get_line(char *str, char *line);
void qla2x00_check_fabric_devices(scsi_qla_host_t *ha);
#if defined(ISP2300)
static int qla2x00_get_prop_int(scsi_qla_host_t *, char *, int *);
static int qla2x00_mask_hba(scsi_qla_host_t *);
STATIC void qla23xx_blink_led(scsi_qla_host_t *);
STATIC void qla23xx_stop_beacon(scsi_qla_host_t *);
STATIC void qla24xx_blink_led(scsi_qla_host_t *);
STATIC void qla24xx_stop_beacon(scsi_qla_host_t *);
static int qla24xx_setup_chip(scsi_qla_host_t *);
static int qla24xx_nvram_config(scsi_qla_host_t *);
static void qla24xx_isr(scsi_qla_host_t *, uint32_t , uint8_t *);
static uint32_t qla24xx_read_flash_dword(scsi_qla_host_t *, uint32_t );

/* General IP support routines */
static int qla2x00_ip_initialize(scsi_qla_host_t *ha);
static void qla2x00_ip_send_complete(scsi_qla_host_t *ha,
		uint32_t handle, uint16_t comp_status);
static void qla2x00_ip_receive(scsi_qla_host_t *ha, struct ip_rec_entry *);

/* IP device list manipulation routines */
static int qla2x00_convert_to_arp(scsi_qla_host_t *ha, struct send_cb *scb);
static int qla2x00_get_ip_loopid(scsi_qla_host_t *ha,
		struct packet_header *packethdr, uint16_t *loop_id);

/* Entry point network driver */
int qla2x00_ip_inquiry(uint16_t adapter_num, struct bd_inquiry *inq_data);

/* Network driver callback routines */
static int  qla2x00_ip_enable(scsi_qla_host_t *ha,
		struct bd_enable *enable_data);
static void qla2x00_ip_disable(scsi_qla_host_t *ha);
static void qla2x00_add_buffers(scsi_qla_host_t *ha,
		uint16_t rec_count, int ha_locked);
static int  qla2x00_send_packet(scsi_qla_host_t *ha, struct send_cb *scb);
static int  qla2x00_tx_timeout(scsi_qla_host_t *ha);
#endif

static void qla2x00_device_resync(scsi_qla_host_t *);
int
qla2x00_find_new_loop_id(scsi_qla_host_t *ha, fc_port_t *dev);

STATIC uint8_t qla2x00_configure_fabric(scsi_qla_host_t *);
static uint8_t qla2x00_find_all_fabric_devs(scsi_qla_host_t *,
		sns_cmd_rsp_t *, dma_addr_t, struct list_head *);
#if REG_FC4_ENABLED
static uint8_t qla2x00_register_fc4(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t);
static uint8_t qla2x00_register_fc4_feature(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t);
static uint8_t qla2x00_register_nn(scsi_qla_host_t *, sns_cmd_rsp_t *
				,dma_addr_t);
static uint8_t qla2x00_register_snn(scsi_qla_host_t *);
#if  defined(ISP2300)
static uint8_t qla24xx_register_fc4(scsi_qla_host_t *);
static uint8_t qla24xx_register_fc4_feature(scsi_qla_host_t *);
static uint8_t qla24xx_register_nn(scsi_qla_host_t *);
#endif
#endif
static uint8_t qla2x00_gan(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t,
    fc_port_t *);
static uint8_t qla2x00_gid_pt(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t,
    sw_info_t *, uint32_t);
static uint8_t qla2x00_gnn_id(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t,
    sw_info_t *);
static uint8_t qla2x00_gpn_id(scsi_qla_host_t *, sns_cmd_rsp_t *, dma_addr_t,
    sw_info_t *);
#if  defined(ISP2300)
static uint8_t qla24xx_ct_gan(scsi_qla_host_t *, fc_port_t *);
static uint8_t qla24xx_ct_gid_pt(scsi_qla_host_t *, sw_info_t *, uint32_t);
static uint8_t qla24xx_ct_gnn_id(scsi_qla_host_t *, sw_info_t *);
static uint8_t qla24xx_ct_gpn_id(scsi_qla_host_t *, sw_info_t *);
#endif
static uint8_t qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
static uint8_t qla2x00_local_device_login(scsi_qla_host_t *, fc_port_t *);

STATIC uint8_t qla2x00_configure_loop(scsi_qla_host_t *);
static uint8_t qla2x00_configure_local_loop(scsi_qla_host_t *);

STATIC uint8_t qla2x00_32bit_start_scsi(srb_t *sp);
STATIC uint8_t qla2x00_64bit_start_scsi(srb_t *sp);

/* Routines for Failover */
os_tgt_t *qla2x00_tgt_alloc(scsi_qla_host_t *ha, uint16_t t);
#if APIDEV
static int apidev_init(struct Scsi_Host*);
static int apidev_cleanup(void);
#endif
void qla2x00_tgt_free(scsi_qla_host_t *ha, uint16_t t);
os_lun_t *qla2x00_lun_alloc(scsi_qla_host_t *ha, uint16_t t, uint16_t l);

static void qla2x00_lun_free(scsi_qla_host_t *ha, uint16_t t, uint16_t l);
#if  defined(ISP2300)
static inline void
	qla2x00_process_response_queue_in_zio_mode(scsi_qla_host_t *);
#endif
void qla2x00_next(scsi_qla_host_t *vis_ha);
static void qla2x00_config_os(scsi_qla_host_t *ha);
static uint16_t qla2x00_fcport_bind(scsi_qla_host_t *, fc_port_t *);
static os_lun_t *qla2x00_fclun_bind(scsi_qla_host_t *, fc_port_t *, fc_lun_t *);
static int qla2x00_update_fcport(scsi_qla_host_t *ha, fc_port_t *fcport);
static int qla2x00_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport);
static int qla2x00_rpt_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport);
static void qla2x00_cfg_lun(fc_port_t *fcport, uint16_t lun);
STATIC void qla2x00_process_failover(scsi_qla_host_t *ha) ;

STATIC int qla2x00_device_reset(scsi_qla_host_t *, fc_port_t *);

static inline int qla2x00_is_wwn_zero(uint8_t *wwn);
void qla2x00_get_lun_mask_from_config(scsi_qla_host_t *ha, fc_port_t *port,
                                      uint16_t tgt, uint16_t dev_no);
void qla2x00_print_q_info(os_lun_t *q);

STATIC void qla2x00_failover_cleanup(srb_t *);
void qla2x00_flush_failover_q(scsi_qla_host_t *, os_lun_t *);

void qla2x00_chg_endian(uint8_t buf[], size_t size);
STATIC uint8_t qla2x00_check_sense(Scsi_Cmnd *cp, os_lun_t *);

STATIC uint8_t 
__qla2x00_suspend_lun(scsi_qla_host_t *, os_lun_t *, int, int, int);
STATIC uint8_t 
qla2x00_suspend_lun(scsi_qla_host_t *, os_lun_t *, int, int);
STATIC uint8_t
qla2x00_delay_lun(scsi_qla_host_t *, os_lun_t *, int);
STATIC uint8_t
qla2x00_suspend_target(scsi_qla_host_t *, os_tgt_t *, int );

STATIC uint8_t
qla2x00_check_for_devices_online(scsi_qla_host_t *ha);
int qla2x00_test_active_port( fc_port_t *fcport ); 

STATIC void qla2x00_probe_for_all_luns(scsi_qla_host_t *ha); 
void qla2x00_find_all_active_ports(srb_t *sp); 
int qla2x00_test_active_lun( fc_port_t *fcport, fc_lun_t *fclun, uint8_t *);
static fc_port_t *
qla2x00_find_changed_local_port( scsi_qla_host_t *, uint16_t );

#if DEBUG_QLA2100
#if !defined(QL_DEBUG_ROUTINES)
#define QL_DEBUG_ROUTINES
#endif
#endif

static void qla2x00_dump_buffer(uint8_t *, uint32_t);
#if defined(QL_DEBUG_ROUTINES)
/*
*  Driver Debug Function Prototypes.
*/
STATIC uint8_t ql2x_debug_print = 1;
#endif

/* ra 01/03/02 */
#if QLA2100_LIPTEST
STATIC int  mbxtimeout = 0;
#endif

#if DEBUG_GET_FW_DUMP
#if defined(ISP2300) 
STATIC void qla2300_dump_isp(scsi_qla_host_t *ha);
void qla24xx_fw_dump(scsi_qla_host_t *ha, int hardware_locked);
void qla24xx_ascii_fw_dump(scsi_qla_host_t *ha);
void qla24xx_console_fw_dump(scsi_qla_host_t *ha);
#endif
qla2x00_dump_word(uint8_t *, uint32_t, uint32_t);
#endif
#if  NO_LONG_DELAYS
STATIC void qla2x00_sleep_done (struct semaphore * sem);
#endif
STATIC void qla2x00_retry_command(scsi_qla_host_t *, srb_t *);

static inline void qla2x00_add_timer_to_cmd(srb_t *, int);
uint8_t qla2x00_allocate_sp_pool( scsi_qla_host_t *ha);
void qla2x00_free_sp_pool(scsi_qla_host_t *ha );
STATIC srb_t * qla2x00_get_new_sp (scsi_qla_host_t *ha);
STATIC uint8_t qla2x00_check_tgt_status(scsi_qla_host_t *ha, Scsi_Cmnd *cmd);
STATIC uint8_t qla2x00_check_port_status(scsi_qla_host_t *ha,
		fc_port_t *fcport);
STATIC void qla2x00_mark_device_lost(scsi_qla_host_t *, fc_port_t *, int);
STATIC void qla2x00_mark_all_devices_lost( scsi_qla_host_t *ha );
STATIC inline void qla2x00_delete_from_done_queue(scsi_qla_host_t *, srb_t *); 

static inline int qla2x00_marker(scsi_qla_host_t *,
		uint16_t, uint16_t, uint8_t);
STATIC int __qla2x00_marker(scsi_qla_host_t *, uint16_t, uint16_t, uint8_t);
static inline int qla2x00_wait_for_hba_online(scsi_qla_host_t *);
static inline int 
qla2x00_marker(scsi_qla_host_t *ha,
		uint16_t loop_id,
		uint16_t lun,
		uint8_t type)
{
	int ret;
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ret = __qla2x00_marker(ha, loop_id, lun, type);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (ret);
}

/* Flash support routines */
#define FLASH_IMAGE_SIZE	131072

STATIC void qla2x00_flash_enable(scsi_qla_host_t *);
STATIC void qla2x00_flash_disable(scsi_qla_host_t *);
STATIC uint8_t qla2x00_read_flash_byte(scsi_qla_host_t *, uint32_t);
STATIC void qla2x00_write_flash_byte(scsi_qla_host_t *, uint32_t, uint8_t);
STATIC uint8_t qla2x00_poll_flash(scsi_qla_host_t *, uint32_t, uint8_t,
    uint8_t, uint8_t);
STATIC uint8_t qla2x00_program_flash_address(scsi_qla_host_t *, uint32_t,
    uint8_t, uint8_t, uint8_t);
STATIC uint8_t qla2x00_erase_flash(scsi_qla_host_t *, uint8_t, uint8_t);
STATIC uint8_t qla2x00_erase_flash_sector(scsi_qla_host_t *, uint32_t,
    uint32_t, uint8_t, uint8_t);
STATIC void qla2x00_get_flash_manufacturer(scsi_qla_host_t *, uint8_t *,
    uint8_t *);
STATIC uint16_t qla2x00_get_flash_version(scsi_qla_host_t *);
STATIC uint16_t qla2x00_get_fcode_version(scsi_qla_host_t *, uint32_t);
#if defined(NOT_USED_FUNCTION)
STATIC uint16_t qla2x00_get_flash_image(scsi_qla_host_t *, uint8_t *);
#endif
STATIC uint16_t qla2x00_set_flash_image(scsi_qla_host_t *, uint8_t *, uint32_t,
    uint32_t);
STATIC uint16_t
qla2x00_update_or_read_flash(scsi_qla_host_t *ha, uint8_t *, uint32_t, uint32_t, uint8_t );
STATIC uint16_t
qla2x00_read_flash_image( scsi_qla_host_t *ha, uint8_t *, uint32_t, uint32_t );
uint16_t qla24xx_update_or_read_flash(scsi_qla_host_t *, uint8_t *, uint32_t, uint32_t, uint8_t);
uint32_t *qla24xx_read_nvram_data(scsi_qla_host_t *, uint32_t *, uint32_t, uint32_t);

#if defined(ISP2300)
STATIC void
qla2x00_read_flash_data(scsi_qla_host_t *, uint8_t *, uint32_t, uint32_t);
#endif

STATIC uint8_t
qla2x00_cmd_wait(scsi_qla_host_t *); 

/* Some helper functions */
static inline uint32_t qla2x00_normalize_dma_addr(
		dma_addr_t *e_addr,  uint32_t *e_len,
		dma_addr_t *ne_addr, uint32_t *ne_len);

static inline uint16_t qla2x00_check_request_ring(
		scsi_qla_host_t *ha, uint16_t tot_iocbs,
		uint16_t req_ring_index, uint16_t *req_q_cnt);

static inline cont_entry_t *qla2x00_prep_cont_packet(
		scsi_qla_host_t *ha,
		uint16_t *req_ring_index, request_t **request_ring_ptr);

static inline cont_a64_entry_t *qla2x00_prep_a64_cont_packet(
		scsi_qla_host_t *ha,
		uint16_t *req_ring_index, request_t **request_ring_ptr);
STATIC inline void 
qla2x00_free_request_resources(scsi_qla_host_t *dest_ha, srb_t *sp);
 
#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
static int qla2x00_dump_sanity_check(Scsi_Device *);
static void qla2x00_dump_poll(Scsi_Device *);
#endif

#include "qla_inline.h"

/**
 * qla2x00_normalize_dma_addr() - Normalize an DMA address.
 * @e_addr: Raw DMA address
 * @e_len: Raw DMA length
 * @ne_addr: Normalized second DMA address
 * @ne_len: Normalized second DMA length
 *
 * If the address does not span a 4GB page boundary, the contents of @ne_addr
 * and @ne_len are undefined.  @e_len is updated to reflect a normalization.
 *
 * Example:
 *
 * 	ffffabc0ffffeeee	(e_addr) start of DMA address
 * 	0000000020000000	(e_len)  length of DMA transfer
 *	ffffabc11fffeeed	end of DMA transfer
 *
 * Is the 4GB boundary crossed?
 *
 * 	ffffabc0ffffeeee	(e_addr)
 *	ffffabc11fffeeed	(e_addr + e_len - 1)
 *	00000001e0000003	((e_addr ^ (e_addr + e_len - 1))
 *	0000000100000000	((e_addr ^ (e_addr + e_len - 1)) & ~(0xffffffff)
 *
 * Compute start of second DMA segment:
 *
 * 	ffffabc0ffffeeee	(e_addr)
 *	ffffabc1ffffeeee	(0x100000000 + e_addr)
 *	ffffabc100000000	(0x100000000 + e_addr) & ~(0xffffffff)
 *	ffffabc100000000	(ne_addr)
 *	
 * Compute length of second DMA segment:
 *
 *	00000000ffffeeee	(e_addr & 0xffffffff)
 *	0000000000001112	(0x100000000 - (e_addr & 0xffffffff))
 *	000000001fffeeee	(e_len - (0x100000000 - (e_addr & 0xffffffff))
 *	000000001fffeeee	(ne_len)
 *
 * Adjust length of first DMA segment
 *
 * 	0000000020000000	(e_len)
 *	0000000000001112	(e_len - ne_len)
 *	0000000000001112	(e_len)
 *
 * Returns non-zero if the specified address was normalized, else zero.
 */
static inline uint32_t
qla2x00_normalize_dma_addr(
		dma_addr_t *e_addr,  uint32_t *e_len,
		dma_addr_t *ne_addr, uint32_t *ne_len)
{
	uint32_t normalized;

	normalized = 0;
	if ((*e_addr ^ (*e_addr + *e_len - 1)) & ~(0xFFFFFFFFULL)) {
		/* Compute normalized crossed address and len */
		*ne_addr = (0x100000000ULL + *e_addr) & ~(0xFFFFFFFFULL);
		*ne_len = *e_len - (0x100000000ULL - (*e_addr & 0xFFFFFFFFULL));
		*e_len -= *ne_len;

		normalized++;
	}
	return (normalized);
}

void qla2x00_ioctl_error_recovery(scsi_qla_host_t *);	

/* Debug print buffer */
char          debug_buff[LINESIZE*3];

/*
* insmod needs to find the variable and make it point to something
*/
static char *ql2xdevconf = NULL;
static int ql2xdevflag = 0;

#if MPIO_SUPPORT
static int ql2xretrycount = 80;
#else
static int ql2xretrycount = 20;
#endif
static int qla2xenbinq = 1;
static int max_srbs = MAX_SRBS;
#if defined(ISP2200) || defined(ISP2300)
static int ql2xlogintimeout = 20;
static int qlport_down_retry = 0;
#endif
static int ql2xmaxqdepth = 0;
static int ql2xmaxsectors = 0;
static int ql2xmaxsgs = 0;
static int displayConfig = 1;			/* 1- default, 2 - for lunids */
static int retry_gidpt	 = 10; 
static int qfull_retry_count = 16;
static int qfull_retry_delay = 2;
static int extended_error_logging = 0;		/* 0 = off, 1 = log errors */
static int ql2xplogiabsentdevice = 0;
static int ql2xfdmienable = 0;
#if defined(ISP2300)
static int ql2xintrdelaytimer = 3;
#endif

/* Enable for failover */
#if MPIO_SUPPORT
static int ql2xfailover = 1;
#else
static int ql2xfailover = 0;
#endif

#if defined(ISP2200) || defined(ISP2300)
static int qlogin_retry_count = 0;
#endif

static int ConfigRequired = 0;
static int recoveryTime = MAX_RECOVERYTIME;
static int failbackTime = MAX_FAILBACKTIME;

/* Persistent binding type */
static int Bind = BIND_BY_PORT_NAME;

static int ql2xsuspendcount = SUSPEND_COUNT;
static int ql2xioctltimeout = QLA_PT_CMD_TOV;
static int ql2xprocessnotready = 1;
static int ql2xautorestore = 0;

static char *ql2xopts = NULL;

/* insmod qla2100 ql2xopts=verbose" */
/* or */
/* insmod qla2100 ql2xopts="0-0-0..." */

MODULE_PARM(ql2xopts, "s");
MODULE_PARM_DESC(ql2xopts,
		"Additional driver options and persistent binding info.");

MODULE_PARM(ql2xfailover, "i");
MODULE_PARM_DESC(ql2xfailover,
		"Driver failover support: 0 to disable; 1 to enable. "
		"Default behaviour based on compile-time option "
		"MPIO_SUPPORT.");

MODULE_PARM(ql2xmaxqdepth, "i");
MODULE_PARM_DESC(ql2xmaxqdepth,
		"Maximum queue depth to report for target devices,"
		"Default is 32.");

MODULE_PARM(ql2xmaxsectors, "i");
MODULE_PARM_DESC(ql2xmaxsectors,
		"Maximum sectors per request,"
		"Default is 512.");
#define TEMPLATE_MAX_SECTORS	max_sectors: 512,

MODULE_PARM(ql2xmaxsgs, "i");
MODULE_PARM_DESC(ql2xmaxsgs,
		"Maximum scatter/gather entries per request,"
		"Default is 32.");

#if defined(ISP2200) || defined(ISP2300)
MODULE_PARM(ql2xlogintimeout,"i");
MODULE_PARM_DESC(ql2xlogintimeout,
		"Login timeout value in seconds, Default=20");

MODULE_PARM(qlport_down_retry,"i");
MODULE_PARM_DESC(qlport_down_retry,
		"Maximum number of command retries to a port that returns"
		"a PORT-DOWN status.");
#endif

MODULE_PARM(ql2xretrycount,"i");
MODULE_PARM_DESC(ql2xretrycount,
		"Maximum number of mid-layer retries allowed for a command.  "
		"Default value in non-failover mode is 20, "
		"in failover mode, 30.");

MODULE_PARM(max_srbs,"i");
MODULE_PARM_DESC(max_srbs,
		"Maximum number of simultaneous commands allowed for an HBA.");

MODULE_PARM(displayConfig, "i");
MODULE_PARM_DESC(displayConfig,
		"If 1 then display the configuration used in "
		"/etc/modules.conf.");
#if defined(ISP2300)
MODULE_PARM(ql2xintrdelaytimer,"i");
MODULE_PARM_DESC(ql2xintrdelaytimer,
		"ZIO: Waiting time for Firmware before it generates an "
		"interrupt to the host to notify completion of request.");

static int ql2xfwloadbin = 0;
MODULE_PARM(ql2xfwloadbin,"i");
MODULE_PARM_DESC(ql2xfwloadbin,
		" Option to specify to load ISP24XX firmware from flash"
		" or embedded in driver." 
		" 1 -- load firmware from flash."
		" 0 -- load firmware embedded with driver (default).");

static int ql2xoperationmode = 0;
MODULE_PARM(ql2xoperationmode,"i");
MODULE_PARM_DESC(ql2xoperationmode,
		" Option to enable operation mode for ISP23XX/ISP24XX:"
		" 	For ISP23XX set to 0x5 to enable ZIO mode"
		" 	For ISP24XX set to 0x5/0x6 to enable ZIO mode");
#endif

MODULE_PARM(retry_gidpt, "i");
MODULE_PARM_DESC(retry_gidpt,
		"No of times GID_PT to be retried to get the Node Name and"
		 "Portid of the device list.");

MODULE_PARM(ConfigRequired, "i");
MODULE_PARM_DESC(ConfigRequired,
		"If 1, then only configured devices passed in through the"
		"ql2xopts parameter will be presented to the OS");

MODULE_PARM(recoveryTime, "i");
MODULE_PARM_DESC(recoveryTime,
		"Recovery time in seconds before a target device is sent I/O "
		"after a failback is performed.");

MODULE_PARM(failbackTime, "i");
MODULE_PARM_DESC(failbackTime,
		"Delay in seconds before a failback is performed.");

MODULE_PARM(Bind, "i");
MODULE_PARM_DESC(Bind,
		"Target persistent binding method: "
		"0 by Portname (default); 1 by PortID. ");

MODULE_PARM(ql2xsuspendcount,"i");
MODULE_PARM_DESC(ql2xsuspendcount,
		"Number of 6-second suspend iterations to perform while a "
		"target returns a <NOT READY> status.  Default is 10 "
		"iterations.");

MODULE_PARM(ql2xdevflag,"i");
MODULE_PARM_DESC(ql2xdevflag,
		"if set to 1 display abbreviated persistent binding statements.");
MODULE_PARM(qfull_retry_count,"i");
MODULE_PARM_DESC(qfull_retry_count,
		"Number of retries to perform on Queue Full status from device, "
		"Default is 16.");
MODULE_PARM(qfull_retry_delay,"i");
MODULE_PARM_DESC(qfull_retry_delay,
		"Number of seconds to delay on Queue Full status from device, "
		"Default is 2.");
			
MODULE_PARM(extended_error_logging,"i");
MODULE_PARM_DESC(extended_error_logging,
		"Option to enable extended error logging, "
		"Default is 0 - no logging. 1 - log errors.");

MODULE_PARM(ql2xplogiabsentdevice, "i");
MODULE_PARM_DESC(ql2xplogiabsentdevice,
		"Option to enable PLOGI to devices that are not present after "
		"a Fabric scan.  This is needed for several broken switches."
		"Default is 0 - no PLOGI. 1 - perfom PLOGI.");

MODULE_PARM(ql2xfdmienable, "i");
MODULE_PARM_DESC(ql2xfdmienable,
		"Enables FDMI registratons "
		"Default is 0 - no FDMI. 1 - perfom FDMI.");

#if defined(ISP2200) || defined(ISP2300)
MODULE_PARM(qlogin_retry_count,"i");
MODULE_PARM_DESC(qlogin_retry_count,
		"Option to modify the login retry count.");
#endif

MODULE_PARM(ql2xioctltimeout,"i");
MODULE_PARM_DESC(ql2xioctltimeout,
		"IOCTL timeout value in seconds for pass-thur commands, "
		"Default=66");

MODULE_PARM(ql2xprocessnotready,"i");
MODULE_PARM_DESC(ql2xprocessnotready,
		"Option to disable handling of NOT-READY in the driver."
		" Default is 1 - Handled by the driver."
		" Set to 0 - Disable the handling inside the driver");

/* Bit Map Values assigend to these devices are in-sync with
 * values assigned to these devices in the cfg_device_list[]
 * table. To add other devices use the bit map value assigned
 * to it in the above mentioned device table.
 */
MODULE_PARM(ql2xautorestore,"i");
MODULE_PARM_DESC(ql2xautorestore,
		" Option to enable failback for the following list of devices "
		" by OR'ing the bit map value assigned for each device "
		" MSA1000: 0x2, EVA: 0x4, EVA_AA: 0x20, MSA_AA: 0x80"
		" For Ex: passing value of 0x6 will enable autorestore for"
		" both: MSA1000 and EVA."); 

MODULE_DESCRIPTION("QLogic Fibre Channel Host Adapter Driver");
MODULE_AUTHOR(QLOGIC_COMPANY_NAME);
#if defined(MODULE_LICENSE)
	 MODULE_LICENSE("GPL");
#endif

/*
* Just in case someone uses commas to separate items on the insmod
* command line, we define a dummy buffer here to avoid having insmod
* write wild stuff into our code segment
*/
static char dummy_buffer[60] =
		"Please don't add commas in your insmod command!!\n";


static int ql2xuseextopts = 0;
MODULE_PARM(ql2xuseextopts, "i");
MODULE_PARM_DESC(ql2xuseextopts,
		"When non-zero, forces driver to use the extended options "
		"saved in the module object itself even if a string is "
		"defined in ql2xopts."); 

static char *ql2x_extopts = NULL;

#include "listops.h"
#include "qla_fo.cfg"


#if QLA2100_LIPTEST
static int qla2x00_lip = 0;
#endif

#include <linux/ioctl.h>
#include <scsi/scsi_ioctl.h>

/* multi-OS QLOGIC IOCTL definition file */
#include "exioct.h"

#if REG_FDMI_ENABLED
#include "qla_gs.h"
#endif

#if QLA_SCSI_VENDOR_DIR
/* Include routine to set direction for vendor specific commands */
#include "qla_vendor.c"
#endif
/***********************************************************************
* We use the Scsi_Pointer structure that's included with each command
* SCSI_Cmnd as a scratchpad. 
*
* SCp is defined as follows:
*  - SCp.ptr  -- > pointer to the SRB
*  - SCp.this_residual  -- > HBA completion status for ioctl code. 
*
* Cmnd->host_scribble --> Used to hold the hba actived handle (1..255).
***********************************************************************/
#define	CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)
#define CMD_COMPL_STATUS(Cmnd)  ((Cmnd)->SCp.this_residual)
#define	CMD_HANDLE(Cmnd)	((Cmnd)->host_scribble)
/* Additional fields used by ioctl passthru */
#define CMD_RESID_LEN(Cmnd)     ((Cmnd)->SCp.buffers_residual)
#define CMD_SCSI_STATUS(Cmnd)   ((Cmnd)->SCp.Status)
#define CMD_ACTUAL_SNSLEN(Cmnd) ((Cmnd)->SCp.Message)
#define CMD_ENTRY_STATUS(Cmnd)  ((Cmnd)->SCp.have_data_in)

/*
 * Other SCS__Cmnd members we only reference
 */
#define	CMD_XFRLEN(Cmnd)	(Cmnd)->request_bufflen
#define	CMD_CDBLEN(Cmnd)	(Cmnd)->cmd_len
#define	CMD_CDBP(Cmnd)		(Cmnd)->cmnd
#define	CMD_SNSP(Cmnd)		(Cmnd)->sense_buffer
#define	CMD_SNSLEN(Cmnd)	(sizeof (Cmnd)->sense_buffer)
#define	CMD_RESULT(Cmnd)	((Cmnd)->result)
#define	CMD_TIMEOUT(Cmnd)	((Cmnd)->timeout_per_command)

#include "qla_debug.h"

uint8_t copyright[48] = "Copyright 1999-2003, QLogic Corporation";

/****************************************************************************/
/*  LINUX -  Loadable Module Functions.                                     */
/****************************************************************************/

/*
* Stat info for all adpaters
*/
static struct _qla2100stats  {
        unsigned long   mboxtout;            /* mailbox timeouts */
        unsigned long   mboxerr;             /* mailbox errors */
        unsigned long   ispAbort;            /* ISP aborts */
        unsigned long   debugNo;
        unsigned long   loop_resync;
        unsigned long   outarray_full;
        unsigned long   retry_q_cnt;
}
qla2x00_stats;

/*
 * Declare our global semaphores
 */
#if defined(ISP2100)
DECLARE_MUTEX_LOCKED(qla2100_detect_sem);
#endif
#if defined(ISP2200)
DECLARE_MUTEX_LOCKED(qla2200_detect_sem);
#endif
#if defined(ISP2300)
DECLARE_MUTEX_LOCKED(qla2300_detect_sem);
#endif


/*
* Command line options
*/
static unsigned long qla2x00_verbose = 1L;
static unsigned long qla2x00_quiet   = 0L;
static unsigned long qla2x00_reinit = 1L;
static unsigned long qla2x00_req_dmp = 0L;

#if QL_TRACE_MEMORY
extern unsigned long mem_trace[1000];
extern unsigned long mem_id[1000];
int	mem_trace_ptr = 0;
#endif

/*
 * List of host adapters
 */
static scsi_qla_host_t *qla2x00_hostlist = NULL;


STATIC int qla2x00_retryq_dmp = 0;              /* dump retry queue */

#include <linux/ioctl.h>
#include <scsi/scsi_ioctl.h>
#include <asm/uaccess.h>

static inline void qla2x00_config_dma_addressing(scsi_qla_host_t *ha);
/**
 * qla2x00_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 *
 * At exit, the @ha's flags.enable_64bit_addressing set to indicated
 * supported addressing method.
 */
static inline void
qla2x00_config_dma_addressing(scsi_qla_host_t *ha)
{
	/* Assume 32bit DMA address */
	ha->flags.enable_64bit_addressing = 0;

	if (sizeof(dma_addr_t) > 4) {
		/* Update our PCI device dma_mask for full 64 bits */
		if (pci_set_dma_mask(ha->pdev, 0xffffffffffffffffULL) == 0) {
			ha->flags.enable_64bit_addressing = 1;
		} else {
			printk("qla2x00: Failed to set 64 bit PCI mask; using "
			    "32 bit mask.\n");
			pci_set_dma_mask(ha->pdev, 0xffffffff);
		}
	} else {
		pci_set_dma_mask(ha->pdev, 0xffffffff);
	}

	printk(KERN_INFO
	    "scsi(%ld): %d Bit PCI Addressing Enabled.\n", ha->host_no,
	    (ha->flags.enable_64bit_addressing ? 64 : 32));
	DEBUG2(printk(KERN_INFO
	    "scsi(%ld): Scatter/Gather entries= %d\n", ha->host_no,
	    ha->host->sg_tablesize));
}

/*************************************************************************
*   qla2x00_set_info
*
* Description:
*   Set parameters for the driver from the /proc filesystem.
*
* Returns:
*************************************************************************/
int
qla2x00_set_info(char *buffer, int length, scsi_qla_host_t *ha)
{
	if (length < 13 || strncmp("scsi-qla", buffer, 8))
		goto out;

	/*
	 * Usage: echo "scsi-qlascan " > /proc/scsi/<driver-name>/<adapter-id>
	 *
	 * <driver-name> can be either one : qla2100/qla2200/qla2300	
	 *
	 * Ex:- For qla2300 driver: 
	 *	echo "scsi-qlascan " > /proc/scsi/qla2300/<adapter-id>
	 *
	 * <adapter-id> is the instance number of the HBA.
	 *
	 * Scan for all luns on all ports. 
	 */
	if (!strncmp("scan", buffer + 8, 4)) {
		printk("scsi-qla%ld: Scheduling SCAN for new luns.... \n",
		    ha->host_no);
		printk(KERN_INFO
		    "scsi-qla%ld: Scheduling SCAN for new luns.... \n",
		    ha->host_no);
		set_bit(PORT_SCAN_NEEDED, &ha->dpc_flags);
	} else if (!strncmp("lip", buffer + 8, 3)) {
		printk("scsi-qla%ld: Scheduling LIP.... \n", ha->host_no);
		printk(KERN_INFO
		    "scsi-qla%ld: Scheduling LIP.... \n", ha->host_no);
		set_bit(LOOP_RESET_NEEDED, &ha->dpc_flags);
	}
out:
	/* return (-ENOSYS); */  /* Currently this is a no-op */
	return (length);  /* Currently this is a no-op */
}

#include "qla_mbx.c"
#include "qla2x00_ioctl.c"
#if defined(INTAPI)
#include "qla_inioct.c"
#endif


/*
 * The following support functions are adopted to handle
 * the re-entrant qla2x00_proc_info correctly.
 */
STATIC void
copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->offset + info->length)
		len = info->offset + info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}
 
	if (info->pos < info->offset) {
		off_t partial;
 
		partial = info->offset - info->pos;
		data += partial;
		info->pos += partial;
		len  -= partial;
	}
 
	if (len > 0) {
		memcpy(info->buffer, data, len);
		info->pos += len;
		info->buffer += len;
	}
}

STATIC int
copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[256];
	int len;
 
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
 
	copy_mem_info(info, buf, len);

	return (len);
}

/*************************************************************************
* qla2x00_proc_info
*
* Description:
*   Return information to handle /proc support for the driver.
*
* inout : decides the direction of the dataflow and the meaning of the
*         variables
* buffer: If inout==FALSE data is being written to it else read from it
*         (ptr to a page buffer)
* *start: If inout==FALSE start of the valid data in the buffer
* offset: If inout==FALSE starting offset from the beginning of all
*         possible data to return.
* length: If inout==FALSE max number of bytes to be written into the buffer
*         else number of bytes in "buffer"
* Returns:
*         < 0:  error. errno value.
*         >= 0: sizeof data returned.
*************************************************************************/
int
qla2x00_proc_info(char *buffer, char **start, off_t offset,
	          int length, int hostno, int inout)
{
	struct Scsi_Host *host;
	struct info_str	info;
	int             i;
	int             retval = -EINVAL;
	os_lun_t	*up;
	qla_boards_t    *bdp;
	scsi_qla_host_t *ha;
	uint32_t        t, l;
	uint32_t        tmp_sn;
	uint32_t	*flags;
	struct list_head *list, *temp;
	unsigned long	cpu_flags;
	uint8_t		*loop_state;
	fc_port_t	*fcport;
	os_tgt_t	*tq;
#if defined(ISP2300)
	struct qla2x00_special_options special_options;
#endif

#if REQ_TRACE

	Scsi_Cmnd       *cp;
	srb_t           *sp;
#endif

	DEBUG3(printk(KERN_INFO
	    "Entering proc_info buff_in=%p, offset=0x%lx, length=0x%x, "
	    "hostno=%d\n", buffer, offset, length, hostno);)

	host = NULL;

	/* Find the host that was specified */
	for (ha=qla2x00_hostlist; (ha != NULL) && ha->host->host_no != hostno;
	    ha=ha->next) {
		continue;
	}

	/* if host wasn't found then exit */
	if (!ha) {
		DEBUG2_3(printk(KERN_WARNING
		    "%s: Can't find adapter for host number %d\n", 
		    __func__, hostno);)

		return (retval);
	}

	host = ha->host;

	if (inout == TRUE) {
		/* Has data been written to the file? */
		DEBUG3(printk(
		    "%s: has data been written to the file. \n",
		    __func__);)
		return (qla2x00_set_info(buffer, length, ha));
	}

	if (start) {
		*start = buffer;
	}

	info.buffer = buffer;
	info.length = length;
	info.offset = offset;
	info.pos    = 0;


	/* start building the print buffer */
	bdp = &QLBoardTbl_fc[ha->devnum];
#if 1
	copy_info(&info,
	    "QLogic PCI to Fibre Channel Host Adapter for "
	    "%s:\n"
	    "        Firmware version: %2d.%02d.%02d, "
	    "Driver version %s\n",ha->model_number,
	    bdp->fwver[0], bdp->fwver[1], bdp->fwver[2], 
	    qla2x00_version_str);

#endif

	copy_info(&info, "Entry address = %p\n",qla2x00_set_info);

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		copy_info(&info, "\n");
	} else {
		tmp_sn = ((ha->serial0 & 0x1f) << 16) | (ha->serial2 << 8) | 
			    ha->serial1;
		copy_info(&info, "HBA: %s, Serial# %c%05d\n",
		    bdp->bdName, ('A' + tmp_sn/100000), (tmp_sn%100000));
	}

	copy_info(&info,
	    "Request Queue = 0x%lx, Response Queue = 0x%lx\n",
	    (long unsigned int)ha->request_dma,
	    (long unsigned int)ha->response_dma);

	copy_info(&info,
	    "Request Queue count= %ld, Response Queue count= %ld\n",
	    (long)REQUEST_ENTRY_CNT, (long)RESPONSE_ENTRY_CNT);

	copy_info(&info,
	    "Total number of active commands = %ld\n",
	    ha->actthreads);

	copy_info(&info,
	    "Total number of interrupts = %ld\n",
	    (long)ha->total_isr_cnt);

#if defined(ISP2300)
	copy_info(&info,
	    "Total number of active IP commands = %ld\n",
	    ha->ipreq_cnt);
#endif

#if defined(IOCB_HIT_RATE)
	copy_info(&info,
	    "Total number of IOCBs (used/max/#hit) "
	    "= (%d/%d/%d)\n",
	    (int)ha->iocb_cnt,
	    (int)ha->iocb_hiwat,
	    (int)ha->iocb_overflow_cnt);
#else
	copy_info(&info,
	    "Total number of IOCBs (used/max) "
	    "= (%d/%d)\n",
	    (int)ha->iocb_cnt, (int)ha->iocb_hiwat);
#endif


	copy_info(&info,
	    "Total number of queued commands = %d\n",
	    (max_srbs - ha->srb_cnt));

	copy_info(&info,
	    "    Device queue depth = 0x%x\n",
	    (ql2xmaxqdepth == 0) ? 32 : ql2xmaxqdepth);

	copy_info(&info,
	    "Number of free request entries = %d\n", ha->req_q_cnt);

	copy_info(&info,
	    "Number of mailbox timeouts = %ld\n",
	    ha->total_mbx_timeout);

	copy_info(&info,
	    "Number of ISP aborts = %ld\n",ha->total_isp_aborts);

	copy_info(&info,
	    "Number of loop resyncs = %ld\n",
	    ha->total_loop_resync);

	copy_info(&info,
	    "Number of retries for empty slots = %ld\n",
	    qla2x00_stats.outarray_full);

	copy_info(&info,
	    "Number of reqs in pending_q= %ld, retry_q= %d, "
	    "done_q= %ld, scsi_retry_q= %d\n",
	    ha->qthreads, ha->retry_q_cnt,
	    ha->done_q_cnt, ha->scsi_retry_q_cnt);

	if (ha->flags.failover_enabled) {
		copy_info(&info,
		    "Number of reqs in failover_q= %d\n",
		    ha->failover_cnt);
	}

	flags = (uint32_t *)&ha->flags;

	if (atomic_read(&ha->loop_state) == LOOP_DOWN) {
		loop_state = "DOWN";
	} else if (atomic_read(&ha->loop_state) == LOOP_UP) {
		loop_state = "UP";
	} else if (atomic_read(&ha->loop_state) == LOOP_READY) {
		loop_state = "READY";
	} else if (atomic_read(&ha->loop_state) == LOOP_TIMEOUT) {
		loop_state = "TIMEOUT";
	} else if (atomic_read(&ha->loop_state) == LOOP_UPDATE) {
		loop_state = "UPDATE";
	} else if (atomic_read(&ha->loop_state) == LOOP_DEAD) {
		loop_state = "DEAD";
	} else {
		loop_state = "UNKNOWN";
	}

	copy_info(&info, 
	    "Host adapter:loop state= <%s>, flags= 0x%lx\n",
	    loop_state , *flags);

	copy_info(&info, "Dpc flags = 0x%lx\n", ha->dpc_flags);

	copy_info(&info, "MBX flags = 0x%x\n", ha->mbx_flags);

	copy_info(&info, "SRB Free Count = %d\n", ha->srb_cnt);

	copy_info(&info, "Link down Timeout = %3.3d\n",
	    ha->link_down_timeout);

	copy_info(&info, "Port down retry = %3.3d\n",
	    ha->port_down_retry_count);

	copy_info(&info, "Login retry count = %3.3d\n",
	    ha->login_retry_count);

	copy_info(&info,
	    "Commands retried with dropped frame(s) = %d\n",
	    ha->dropped_frame_error_cnt);

#if defined(ISP2300)
	if (!check_24xx_or_54xx_device_ids(ha) && 
				!check_25xx_device_ids(ha)) {
		*((uint16_t *) &special_options) =
		    le16_to_cpu(*((uint16_t *) &ha->init_cb->cb.icb.special_options));

		copy_info(&info,
		    "Configured characteristic impedence: %d ohms\n",
		    special_options.enable_50_ohm_termination ? 50 : 75);

		switch (special_options.data_rate) {
		case 0:
			loop_state = "1 Gb/sec";
			break;

		case 1:
			loop_state = "2 Gb/sec";
			break;

		case 2:
			if (ha->device_id == QLA6312_DEVICE_ID ||
			    ha->device_id == QLA6322_DEVICE_ID)
				loop_state = "2 Gb/sec";
			else
				loop_state = "1-2 Gb/sec auto-negotiate";
			break;

		default:
			loop_state = "unknown";
			break;
		}
	} else {
			loop_state = "4 Gb/sec";
	}
	copy_info(&info, "Configured data rate: %s\n", loop_state);
#endif

	copy_info(&info, "\n");

#if REQ_TRACE
	if (qla2x00_req_dmp) {
		copy_info(&info,
		    "Outstanding Commands on controller:\n");

		for (i = 1; i < MAX_OUTSTANDING_COMMANDS; i++) {
			if ((sp = ha->outstanding_cmds[i]) == NULL) {
				continue;
			}

			if ((cp = sp->cmd) == NULL) {
				continue;
			}

			copy_info(&info, "(%d): Pid=%d, sp flags=0x%lx"
			    ", cmd=0x%p, state=%d\n", 
			    i, 
			    (int)sp->cmd->serial_number, 
			    (long)sp->flags,
			    CMD_SP(sp->cmd),
			    (int)sp->state);

			if (info.pos >= info.offset + info.length) {
				/* No need to continue */
				goto profile_stop;
			}
		}
	}
#endif /* REQ_TRACE */

	if (qla2x00_retryq_dmp) {
		if (!list_empty(&ha->retry_queue)) {
			copy_info(&info,
			    "qla%ld: Retry queue requests:\n",
			    ha->host_no);

			spin_lock_irqsave(&ha->list_lock, cpu_flags);

			i = 0;
			list_for_each_safe(list, temp, &ha->retry_queue) {
				sp = list_entry(list, srb_t, list);
				t = SCSI_TCN_32(sp->cmd);
				l = SCSI_LUN_32(sp->cmd);

				copy_info(&info,
				    "%d: target=%d, lun=%d, "
				    "pid=%ld sp=%p, sp->flags=0x%x,"
				    "sp->state= %d\n", 
				    i, t, l, 
				    sp->cmd->serial_number, sp, 
				    sp->flags, sp->state );

				i++;

				if (info.pos >= info.offset + info.length) {
					/* No need to continue */
					goto profile_stop;
				}
			}

			spin_unlock_irqrestore(&ha->list_lock, cpu_flags);

		} /* if (!list_empty(&ha->retry_queue))*/
	} /* if ( qla2x00_retryq_dmp )  */

	/* 2.25 node/port display to proc */
	/* Display the node name for adapter */
	copy_info(&info, "\nSCSI Device Information:\n");
	copy_info(&info,
	    "scsi-qla%d-adapter-node="
	    "%02x%02x%02x%02x%02x%02x%02x%02x;\n",
	    (int)ha->instance, ha->node_name[0], ha->node_name[1],
	    ha->node_name[2], ha->node_name[3], ha->node_name[4], 
	    ha->node_name[5], ha->node_name[6], ha->node_name[7]);

	/* display the port name for adapter */
	copy_info(&info,
	    "scsi-qla%d-adapter-port="
	    "%02x%02x%02x%02x%02x%02x%02x%02x;\n",
	    (int)ha->instance, ha->port_name[0], ha->port_name[1],
	    ha->port_name[2], ha->port_name[3], ha->port_name[4],
	    ha->port_name[5], ha->port_name[6], ha->port_name[7]);

	for (t = 0; t < MAX_FIBRE_DEVICES; t++) {
		if ((tq = TGT_Q(ha, t)) == NULL)
			continue;
		copy_info(&info,
		"scsi-qla%d-target-%d="
		"%02x%02x%02x%02x%02x%02x%02x%02x;\n",
		(int)ha->instance, t,
		tq->port_name[0], tq->port_name[1],
		tq->port_name[2], tq->port_name[3],
		tq->port_name[4], tq->port_name[5],
		tq->port_name[6], tq->port_name[7]);
	}

	/* Print out device port names */
 	if (ha->flags.failover_enabled) {
		copy_info(&info, "\nFC Port Information:\n");
 		i = 0;
 		list_for_each_entry(fcport, &ha->fcports, list) {
			if(fcport->port_type != FCT_TARGET)
				continue;

				copy_info(&info,
			    	"scsi-qla%d-port-%d="
			    	"%02x%02x%02x%02x%02x%02x%02x%02x:"
			    	"%02x%02x%02x%02x%02x%02x%02x%02x:"
				"%06x:%x;\n",
			    	(int)ha->instance, i,
			    	fcport->node_name[0], fcport->node_name[1],
			    	fcport->node_name[2], fcport->node_name[3],
			    	fcport->node_name[4], fcport->node_name[5],
			    	fcport->node_name[6], fcport->node_name[7],
			    	fcport->port_name[0], fcport->port_name[1],
			    	fcport->port_name[2], fcport->port_name[3],
			    	fcport->port_name[4], fcport->port_name[5],
			    	fcport->port_name[6], fcport->port_name[7],
				fcport->d_id.b24, fcport->loop_id); 
			i++;
		}
	} 

	copy_info(&info, "\nSCSI LUN Information:\n");
	copy_info(&info, "(Id:Lun)  * - indicates lun is not registered with the OS.\n");

	/* scan for all equipment stats */
	for (t = 0; t < MAX_FIBRE_DEVICES; t++) {
		/* scan all luns */
		for (l = 0; l < ha->max_luns; l++) {
			up = (os_lun_t *) GET_LU_Q(ha, t, l);

			if (up == NULL) {
				continue;
			}
			if (up->fclun == NULL) {
				continue;
			}
			if (up->fclun->flags & FC_DISCON_LUN) {
				continue;
			}

			copy_info(&info,
			    "(%2d:%2d): Total reqs %ld,",
			    t,l,up->io_cnt);

			copy_info(&info,
			    " Pending reqs %ld,",
			    up->out_cnt);

			if (up->io_cnt < 3 &&
			    up->fclun->inq0 != 0xc) {
				copy_info(&info,
				    " flags 0x%x*,",
				    (int)up->q_flag);
			} else {
				copy_info(&info,
				    " flags 0x%x,",
				    (int)up->q_flag);
			}

			copy_info(&info, 
			    " %ld:%d:%02x,",
			    up->fclun->fcport->ha->instance,
			    up->fclun->fcport->cur_path,
			    up->fclun->fcport->loop_id);

			copy_info(&info, "\n");

			if (info.pos >= info.offset + info.length) {
				/* No need to continue */
				goto profile_stop;
			}
		}

		if (info.pos >= info.offset + info.length) {
			/* No need to continue */
			break;
		}
	}

profile_stop:

	retval = info.pos > info.offset ? info.pos - info.offset : 0;

	DEBUG3(printk(KERN_INFO 
	    "Exiting proc_info: info.pos=%d, offset=0x%lx, "
	    "length=0x%x\n", info.pos, offset, length);)

#if QLA2100_LIPTEST
	qla2x00_lip = 1;
#endif

	return (retval);

}
 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3)
inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask);

inline int
pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
	if (!pci_dma_supported(dev, mask))
		return -EIO;

	dev->dma_mask = mask;

	return 0;
}	 
#endif


/**************************************************************************
* sp_put
*
* Description:
*   Decrement reference count and call the callback if we're the last
*   owner of the specified sp. Will get io_request_lock before calling
*   the callback.
*
* Input:
*   ha - pointer to the scsi_qla_host_t where the callback is to occur.
*   sp - pointer to srb_t structure to use.
*
* Returns:
*
**************************************************************************/
static inline void
sp_put(struct scsi_qla_host * ha, srb_t *sp)
{
        if (atomic_read(&sp->ref_count) == 0) {
		printk(KERN_INFO
			"%s(): **** SP->ref_count not zero\n",
			__func__);

                return;
	}

        if (!atomic_dec_and_test(&sp->ref_count))
        {
                return;
        }

        qla2x00_callback(ha, sp->cmd);
}

/**************************************************************************
* sp_get
*
* Description:
*   Increment reference count of the specified sp.
*
* Input:
*   sp - pointer to srb_t structure to use.
*
* Returns:
*
**************************************************************************/
static inline void
sp_get(struct scsi_qla_host * ha, srb_t *sp)
{
        atomic_inc(&sp->ref_count);

        if (atomic_read(&sp->ref_count) > 2) {
		printk(KERN_INFO
			"%s(): **** SP->ref_count greater than two\n",
			__func__);

		return;
	}
}

/**************************************************************************
* __sp_put
*
* Description:
*   Decrement reference count and call the callback if we're the last
*   owner of the specified sp. Will NOT get io_request_lock before calling
*   the callback.
*
* Input:
*   ha - pointer to the scsi_qla_host_t where the callback is to occur.
*   sp - pointer to srb_t structure to use.
*
* Returns:
*
**************************************************************************/
static inline void
__sp_put(struct scsi_qla_host * ha, srb_t *sp)
{
        if (atomic_read(&sp->ref_count) == 0) {
		printk(KERN_INFO
			"%s(): **** SP->ref_count not zero\n",
			__func__);

		return;
	}

        if (!atomic_dec_and_test(&sp->ref_count))
        {
                return;
        }

        qla2x00_callback(ha, sp->cmd);
}

/**************************************************************************
*   qla2x00_cmd_timeout
*
* Description:
*       Handles the command if it times out in any state.
*
* Input:
*     sp - pointer to validate
*
* Returns:
* None.
* Note:Need to add the support for if( sp->state == SRB_FAILOVER_STATE).
**************************************************************************/
void
qla2x00_cmd_timeout(srb_t *sp)
{
	int t, l;
	int processed;
	scsi_qla_host_t *vis_ha, *dest_ha;
	Scsi_Cmnd *cmd;
	ulong      flags;
	ulong      cpu_flags;
	fc_port_t	*fcport;

	cmd = sp->cmd;
	vis_ha = (scsi_qla_host_t *) cmd->host->hostdata;

	DEBUG3(printk("cmd_timeout: Entering sp->state = %x\n", sp->state);)

	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	fcport = sp->fclun->fcport;
	dest_ha = sp->ha;

	/*
	 * If IO is found either in retry Queue 
	 *    OR in Lun Queue
	 * Return this IO back to host
	 */
	spin_lock_irqsave(&vis_ha->list_lock, flags);
	processed = 0;
	if (sp->state == SRB_PENDING_STATE) {
		__del_from_pending_queue(vis_ha, sp);
		DEBUG2(printk(KERN_INFO "qla2100%ld: Found in Pending queue "
				"pid %ld, State = %x., "
			 	 "fcport state=%d jiffies=%lx\n",
				vis_ha->host_no,
				sp->cmd->serial_number, sp->state,
				atomic_read(&fcport->state),
				jiffies);)

		/*
		 * If FC_DEVICE is marked as dead return the cmd with
		 * DID_NO_CONNECT status.  Otherwise set the host_byte to
		 * DID_BUS_BUSY to let the OS  retry this cmd.
		 */
		if (atomic_read(&fcport->state) == FC_DEVICE_DEAD ||
		    atomic_read(&fcport->ha->loop_state) == LOOP_DEAD) {
			cmd->result = DID_NO_CONNECT << 16;
			if (atomic_read(&fcport->ha->loop_state) == LOOP_DEAD) 
				sp->err_id = SRB_ERR_LOOP;
			else
				sp->err_id = SRB_ERR_PORT;
		} else {
			cmd->result = DID_BUS_BUSY << 16;
		}
		__add_to_done_queue(vis_ha, sp);
		processed++;
	} 
	spin_unlock_irqrestore(&vis_ha->list_lock, flags);
	if (processed) {
		 if (vis_ha->dpc_wait && !vis_ha->dpc_active) 
		 	 up(vis_ha->dpc_wait);
		 return;
	}

	spin_lock_irqsave(&dest_ha->list_lock, flags);
	if ((sp->state == SRB_RETRY_STATE)  ||
		 (sp->state == SRB_SCSI_RETRY_STATE)  ||
		 (sp->state == SRB_FAILOVER_STATE)) {

		DEBUG2(printk(KERN_INFO "qla2100%ld: Found in (Scsi) Retry queue or "
				"failover Q pid %ld, State = %x., "
				"fcport state=%d jiffies=%lx retried=%d\n",
				dest_ha->host_no,
				sp->cmd->serial_number, sp->state,
				atomic_read(&fcport->state),
				jiffies, sp->cmd->retries);)

		if ((sp->state == SRB_RETRY_STATE)) {
			__del_from_retry_queue(dest_ha, sp);
		} else if ((sp->state == SRB_SCSI_RETRY_STATE)) {
			__del_from_scsi_retry_queue(dest_ha, sp);
		} else if ((sp->state == SRB_FAILOVER_STATE)) {
			__del_from_failover_queue(dest_ha, sp);
		}

		/*
		 * If FC_DEVICE is marked as dead return the cmd with
		 * DID_NO_CONNECT status.  Otherwise set the host_byte to
		 * DID_BUS_BUSY to let the OS  retry this cmd.
		 */
		if (dest_ha->flags.failover_enabled) {
			cmd->result = DID_BUS_BUSY << 16;
		} else {
			if (atomic_read(&fcport->state) == FC_DEVICE_DEAD ||
			    atomic_read(&dest_ha->loop_state) == LOOP_DEAD) {
				qla2x00_extend_timeout(cmd, EXTEND_CMD_TIMEOUT);
				cmd->result = DID_NO_CONNECT << 16;
				if (atomic_read(&dest_ha->loop_state) ==
				    LOOP_DEAD) 
					sp->err_id = SRB_ERR_LOOP;
				else
					sp->err_id = SRB_ERR_PORT;
			} else {
				cmd->result = DID_BUS_BUSY << 16;
			}
		}

		__add_to_done_queue(dest_ha, sp);
		processed++;
	} 
	spin_unlock_irqrestore(&dest_ha->list_lock, flags);
	if (processed) {
		 if (dest_ha->dpc_wait && !dest_ha->dpc_active) 
		 	 up(dest_ha->dpc_wait);
		 return;
	}

	spin_lock_irqsave(&dest_ha->list_lock, cpu_flags);
	if (sp->state == SRB_DONE_STATE) {
		/* IO in done_q  -- leave it */
		DEBUG(printk("qla2100%ld: Found in Done queue pid %ld sp=%p.\n",
				dest_ha->host_no, sp->cmd->serial_number, sp);)
	} else if (sp->state == SRB_SUSPENDED_STATE) {
		DEBUG(printk("qla2100%ld: Found SP %p in suspended state  "
				"- pid %d:\n",
				dest_ha->host_no,sp,
				(int)sp->cmd->serial_number);)
		DEBUG(qla2x00_dump_buffer((uint8_t *)sp, sizeof(srb_t));)
	} else if (sp->state == SRB_ACTIVE_STATE) {
		/*
		 * IO is with ISP find the command in our active list.
		 */
		spin_unlock_irqrestore(&dest_ha->list_lock, cpu_flags); /* 01/03 */
		spin_lock_irqsave(&dest_ha->hardware_lock, flags);
		if (sp == dest_ha->outstanding_cmds
				[(u_long)CMD_HANDLE(sp->cmd)]) {

			DEBUG(printk("cmd_timeout: Found in ISP \n");)

			if (sp->flags & SRB_TAPE) {
				/*
				 * We cannot allow the midlayer error handler
				 * to wakeup and begin the abort process.
				 * Extend the timer so that the firmware can
				 * properly return the IOCB.
				 */
				DEBUG(printk("cmd_timeout: Extending timeout "
				    "of FCP2 tape command!\n"));
				qla2x00_extend_timeout(sp->cmd,
				    EXTEND_CMD_TIMEOUT);
			} else if (sp->flags & SRB_IOCTL) {
				dest_ha->ioctl_err_cmd = sp->cmd;
				set_bit(IOCTL_ERROR_RECOVERY, &dest_ha->dpc_flags);
				if (dest_ha->dpc_wait && !dest_ha->dpc_active) 
					up(dest_ha->dpc_wait);
			}

			sp->state = SRB_ACTIVE_TIMEOUT_STATE;
			spin_unlock_irqrestore(&dest_ha->hardware_lock, flags);
		} else {
			spin_unlock_irqrestore(&dest_ha->hardware_lock, flags);
			printk(KERN_INFO 
				"qla_cmd_timeout: State indicates it is with "
				"ISP, But not in active array\n");
		}
		spin_lock_irqsave(&dest_ha->list_lock, cpu_flags); 	/* 01/03 */
	} else if (sp->state == SRB_ACTIVE_TIMEOUT_STATE) {
		DEBUG(printk("qla2100%ld: Found in Active timeout state"
				"pid %ld, State = %x., \n",
				dest_ha->host_no,
				sp->cmd->serial_number, sp->state);)
	} else {
		/* EMPTY */
		DEBUG3(printk("cmd_timeout%ld: LOST command state = "
				"0x%x, sp=%p\n",
				vis_ha->host_no, sp->state,sp);)

		printk(KERN_INFO
			"cmd_timeout: LOST command state = 0x%x\n", sp->state);
	}
	spin_unlock_irqrestore(&dest_ha->list_lock, cpu_flags);

	DEBUG3(printk("cmd_timeout: Leaving\n");)
}


/**************************************************************************
*   qla2x00_add_timer_to_cmd
*
* Description:
*       Creates a timer for the specified command. The timeout is usually
*       the command time from kernel minus 2 secs.
*
* Input:
*     sp - pointer to validate
*
* Returns:
*     None.
**************************************************************************/
static inline void
qla2x00_add_timer_to_cmd(srb_t *sp, int timeout)
{
	init_timer(&sp->timer);
	sp->timer.expires = jiffies + timeout * HZ;
	sp->timer.data = (unsigned long) sp;
	sp->timer.function = (void (*) (unsigned long))qla2x00_cmd_timeout;
	add_timer(&sp->timer);
}

/**************************************************************************
*   qla2x00_delete_timer_from_cmd
*
* Description:
*       Delete the timer for the specified command.
*
* Input:
*     sp - pointer to validate
*
* Returns:
*     None.
**************************************************************************/
static inline void 
qla2x00_delete_timer_from_cmd(srb_t *sp )
{
	if (sp->timer.function != NULL) {
		del_timer(&sp->timer);
		sp->timer.function =  NULL;
		sp->timer.data = (unsigned long) NULL;
	}
}

/**************************************************************************
* qla2x00_detect
*
* Description:
*    This routine will probe for QLogic FC SCSI host adapters.
*    It returns the number of host adapters of a particular
*    type that were found.	 It also initialize all data necessary for
*    the driver.  It is passed-in the host number, so that it
*    knows where its first entry is in the scsi_hosts[] array.
*
* Input:
*     template - pointer to SCSI template
*
* Returns:
*  num - number of host adapters found.
**************************************************************************/
int
qla2x00_detect(Scsi_Host_Template *template)
{
	int		ret;
	char		tmp_str[80];
	device_reg_t	*reg;
	int		i;
	uint16_t        subsystem_vendor, subsystem_device;
	struct Scsi_Host *host;
	scsi_qla_host_t *ha = NULL, *cur_ha;
	struct _qlaboards  *bdp;
	unsigned long		flags = 0;
	unsigned long		wait_switch = 0;
	struct pci_dev *pdev = NULL;
	int thread_pid;

	ENTER("qla2x00_detect");

	spin_unlock_irq(&io_request_lock);

#if defined(MODULE)
	DEBUG3(printk("DEBUG: qla2x00_set_info starts at address = %p\n",
			qla2x00_set_info);)
	printk(KERN_INFO
		"qla2x00_set_info starts at address = %p\n", qla2x00_set_info);

	/*
	 * If we are called as a module, the qla2100 pointer may not be null
	 * and it would point to our bootup string, just like on the lilo
	 * command line.  IF not NULL, then process this config string with
	 * qla2x00_setup
	 *
	 * Boot time Options To add options at boot time add a line to your
	 * lilo.conf file like:
	 * append="qla2100=verbose,tag_info:{{32,32,32,32},{32,32,32,32}}"
	 * which will result in the first four devices on the first two
	 * controllers being set to a tagged queue depth of 32.
	 */

	/* Increments the usage count of module: qla2[23]00_conf */
#if defined(ISP2200)
	ql2x_extopts =
	    (char *) inter_module_get_request("qla22XX_conf", "qla2200_conf");
#endif
#if defined(ISP2300)
	ql2x_extopts =
	    (char *) inter_module_get_request("qla23XX_conf", "qla2300_conf");
#endif

	DEBUG4(printk("qla2x00_detect: ql2xopts=%p ql2x_extopts=%p "
	    "ql2xuseextopts=%d.\n", ql2xopts, ql2x_extopts, ql2xuseextopts);)

	if (ql2xopts && ql2xuseextopts == 0) {
		DEBUG4(printk(
		    "qla2x00_detect: using old opt.\n");)

		/* Force to use old option. */
		qla2x00_setup(ql2xopts);
		printk(KERN_INFO "qla2x00:Loading driver with config data "
		    " from /etc/modules.conf. Config Data length=0x%lx\n",
		    (ulong)strlen(ql2xopts));

	} else if (ql2x_extopts != NULL && *ql2x_extopts != '\0') {
		DEBUG4(printk( "qla2x00_detect: using new opt:"
			" first_char=%c\n",*ql2x_extopts);)

		ql2xdevconf = ql2x_extopts;
		if (isdigit(*ql2xdevconf)) {
			ql2xdevflag++;
		}
		printk(KERN_INFO "qla2x00: Loading driver with config data "
		    "from config module. Config Data length=0x%lx\n",
		    (ulong)strlen(ql2x_extopts));
	}

	if (dummy_buffer[0] != 'P')
		printk(KERN_WARNING
			"qla2x00: Please read the file "
			"/usr/src/linux/drivers/scsi/README.qla2x00\n"
			"qla2x00: to see the proper way to specify options to "
			"the qla2x00 module\n"
			"qla2x00: Specifically, don't use any commas when "
			"passing arguments to\n"
			"qla2x00: insmod or else it might trash certain memory "
			"areas.\n");
#endif

	if (!pci_present()) {
		printk("scsi: PCI not present\n");
		spin_lock_irq(&io_request_lock);

		return 0;
	} /* end of !pci_present() */

	bdp = &QLBoardTbl_fc[0];
	qla2x00_hostlist = NULL;
	template->proc_name = DRIVER_NAME;
#if defined(SCSI_HOST_VARYIO)
	SCSI_HOST_VARYIO(template) = 1;
#endif
	if( ql2xmaxsectors > 0 && ql2xmaxsectors <= 0xffff ) {
		template->max_sectors = ql2xmaxsectors;
		printk(KERN_INFO
		"scsi-qla: Changing max_sectors=%d\n", ql2xmaxsectors);
	}

	if( ql2xmaxsgs > 0 ) {
		template->sg_tablesize = ql2xmaxsgs;
		printk(KERN_INFO
		"scsi-qla: Changing sg_tablesize=%d\n", ql2xmaxsgs);
	}

#if DEBUG_QLA2100
	sprintf(tmp_str, "%s-debug", QLA2100_VERSION);
#else
	sprintf(tmp_str, "%s", QLA2100_VERSION);
#endif
	if (ql2xfailover) {
		ql2xretrycount = 60;
		sprintf(qla2x00_version_str, "%s-fo", tmp_str);
	} else {
		sprintf(qla2x00_version_str, "%s", tmp_str);
	}

	/* Try and find each different type of adapter we support */
	for (i = 0; bdp->device_id != 0 && i < NUM_OF_ISP_DEVICES;
		i++, bdp++) {

		pdev = NULL;
		/* PCI_SUBSYSTEM_IDS supported */
		while ((pdev = pci_find_subsys(QLA2X00_VENDOR_ID,
						bdp->device_id,
						PCI_ANY_ID, PCI_ANY_ID, 
						pdev))) {

			if (pci_enable_device(pdev))
				continue;

			/* found a adapter */
			printk(KERN_INFO
				"qla2x00: Found  VID=%x DID=%x "
				"SSVID=%x SSDID=%x\n",
				pdev->vendor, 
				pdev->device,
				pdev->subsystem_vendor, 
				pdev->subsystem_device);

			subsystem_vendor = pdev->subsystem_vendor;
			subsystem_device = pdev->subsystem_device;

#if defined(ISP2100)
			template->name = "QLogic Fibre Channel 2100";
#endif
#if defined(ISP2200)
			template->name = "QLogic Fibre Channel 2200";
#endif
#if defined(ISP2300)
			template->name = "QLogic Fibre Channel 2300";
#endif
			spin_lock_irq(&io_request_lock);
			host = scsi_register(template, sizeof(scsi_qla_host_t));
			spin_unlock_irq(&io_request_lock);
			if (host == NULL) {
				printk(KERN_WARNING
				    "qla2x00: couldn't register with scsi "
				    "layer\n");
				goto bailout;
			}

			ha = (scsi_qla_host_t *)host->hostdata;

			/* Clear our data area */
			memset(ha, 0, sizeof(scsi_qla_host_t));

			ha->host_no = host->host_no;
			ha->host = host;
			ha->pdev = pdev;

			/* Configure PCI I/O space */
			ret = qla2x00_iospace_config(ha);
			if (ret != 0) {
				printk(KERN_WARNING
				    "qla2x00: couldn't configure PCI I/O "
				    "space!\n");
				spin_lock_irq(&io_request_lock);
				scsi_unregister(host);
				spin_unlock_irq(&io_request_lock);
	
				continue;
			}

			/* Sanitize the information from PCI BIOS. */
			host->irq = pdev->irq;
			ha->subsystem_vendor = subsystem_vendor;
			ha->subsystem_device = subsystem_device;
printk("%s subsys_vend=0x%x subsys_dev=0x%x\n", __func__,
		ha->subsystem_vendor, ha->subsystem_device);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,4)
			scsi_set_pci_device(host, pdev);
#endif

			ha->device_id = bdp->device_id;
			ha->devnum = i;
			if (qla2x00_verbose) {
				printk(KERN_INFO
				    "scsi(%d): Found a %s @ bus %d, device "
				    "0x%x, irq %d, iobase 0x%p\n",
				    host->host_no, bdp->bdName,
				    ha->pdev->bus->number,
				    PCI_SLOT(ha->pdev->devfn), host->irq,
				    ha->iobase);
			}

			spin_lock_init(&ha->hardware_lock);

#if defined(SH_HAS_HOST_LOCK)
			spin_lock_init(&ha->host_lock);
			host->host_lock = &ha->host_lock;
#endif

			/* 4.23 Initialize /proc/scsi/qla2x00 counters */
			ha->actthreads = 0;
			ha->qthreads   = 0;
			ha->dump_done  = 0;
			ha->total_isr_cnt = 0;
			ha->total_isp_aborts = 0;
			ha->total_lip_cnt = 0;
			ha->total_dev_errs = 0;
			ha->total_ios = 0;
			ha->total_bytes = 0;

			/* Initialized memory allocation pointers */
			INIT_LIST_HEAD(&ha->free_queue);

			INIT_LIST_HEAD(&ha->fcports);

			INIT_LIST_HEAD(&ha->done_queue);
                        INIT_LIST_HEAD(&ha->retry_queue);
                        INIT_LIST_HEAD(&ha->scsi_retry_queue);
                        INIT_LIST_HEAD(&ha->failover_queue);

                        INIT_LIST_HEAD(&ha->pending_queue);

			qla2x00_config_dma_addressing(ha);

			if (qla2x00_mem_alloc(ha)) {
				printk(KERN_WARNING
				    "scsi(%d): [ERROR] Failed to allocate "
				    "memory for adapter\n", host->host_no);
				qla2x00_mem_free(ha);
				pci_release_regions(ha->pdev);

				spin_lock_irq(&io_request_lock);
				scsi_unregister(host);
				spin_unlock_irq(&io_request_lock);

				continue;
			}

			ha->prev_topology = 0;
			ha->ports = bdp->numPorts;

#if defined(ISP2100)
			ha->max_targets = MAX_TARGETS_2100;
#else
			ha->max_targets = MAX_TARGETS_2200;
#endif

			/* load the F/W, read paramaters, and init the H/W */
			ha->instance = num_hosts;

			init_MUTEX_LOCKED(&ha->mbx_intr_sem);

			if (ql2xfailover)
				ha->flags.failover_enabled = 1;
			else
				ha->flags.failover_enabled = 0;


			/*
			 * These locks are used to prevent more than one CPU
			 * from modifying the queue at the same time. The
			 * higher level "io_request_lock" will reduce most
			 * contention for these locks.
			 */

			spin_lock_init(&ha->mbx_bits_lock);
			spin_lock_init(&ha->mbx_reg_lock);
			spin_lock_init(&ha->mbx_q_lock);
			spin_lock_init(&ha->list_lock);

			if (qla2x00_initialize_adapter(ha) &&
				!(ha->device_flags & DFLG_NO_CABLE)) {

				printk(KERN_WARNING
				    "qla2x00: Failed to initialize adapter\n");

				DEBUG2(printk(KERN_INFO
				    "scsi%ld: Failed to initialize adapter - "
				    "Adapter flags %x.\n", ha->host_no,
				    ha->device_flags);)

				qla2x00_mem_free(ha);

				pci_release_regions(ha->pdev);
				spin_lock_irq(&io_request_lock);
				scsi_unregister(host);
				spin_unlock_irq(&io_request_lock);

				continue;
			}

			/*
			 * Startup the kernel thread for this host adapter
			 */
#if defined(ISP2100)
			ha->dpc_notify = &qla2100_detect_sem;
#endif
#if defined(ISP2200)
			ha->dpc_notify = &qla2200_detect_sem;
#endif
#if defined(ISP2300)
			ha->dpc_notify = &qla2300_detect_sem;
#endif
			thread_pid = kernel_thread(
					(int (*)(void *))qla2x00_do_dpc,
					(void *) ha, 0);
			if (thread_pid < 0) {
				printk(KERN_WARNING
				    "qla2x00(%ld): Failed to Create kernel" 
				    " thread thread_pid=%d\n",ha->host_no,
					thread_pid);
				DEBUG2(printk(KERN_WARNING
				    "qla2x00(%ld): Failed to Create kernel" 
				    " thread thread_pid=%d\n",ha->host_no,
					thread_pid);)

				qla2x00_mem_free(ha);

				pci_release_regions(ha->pdev);
				spin_lock_irq(&io_request_lock);
				scsi_unregister(host);
				spin_unlock_irq(&io_request_lock);

				continue;
			}
			/*
			 * Now wait for the kernel dpc thread to initialize
			 * and go to sleep.
			 */
#if defined(ISP2100)
			down(&qla2100_detect_sem);
#endif
#if defined(ISP2200)
			down(&qla2200_detect_sem);
#endif
#if defined(ISP2300)
			down(&qla2300_detect_sem);
#endif

			ha->dpc_notify = NULL;
			ha->next = NULL;
			/* Register our resources with Linux */
			if (qla2x00_register_with_Linux(ha, bdp->numPorts-1)) {
				printk(KERN_WARNING
				    "scsi%ld: Failed to register resources.\n",
				    ha->host_no);

				qla2x00_mem_free(ha);

				pci_release_regions(ha->pdev);
				spin_lock_irq(&io_request_lock);
				scsi_unregister(host);
				spin_unlock_irq(&io_request_lock);

				continue;
			}

			DEBUG2(printk("DEBUG: detect hba %ld at "
			    "address = %p - adding to hba list\n", ha->host_no,
			    ha);)


			/* Disable ISP interrupts. */
			qla2x00_disable_intrs(ha);
			/* Ensure mailbox registers are free. */
#if defined(ISP2300)
			if (check_24xx_or_54xx_device_ids(ha) || 
				check_25xx_device_ids(ha)) {
				struct device_reg_24xx *reg24;
				reg24 = (struct device_reg_24xx *)ha->iobase;
				spin_lock_irqsave(&ha->hardware_lock, flags);
				WRT_REG_DWORD(&reg24->hccr, HCCRX_CLR_HOST_INT);
				WRT_REG_DWORD(&reg24->hccr, HCCRX_CLR_RISC_INT);
				spin_unlock_irqrestore(&ha->hardware_lock, 
								flags);
			} else
#endif
			{
				reg = ha->iobase;
				spin_lock_irqsave(&ha->hardware_lock, flags);
				WRT_REG_WORD(&reg->semaphore, 0);
				WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
				WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);
				PCI_POSTING(&reg->host_cmd);

				/* Enable proper parity */
#if defined(ISP2300) 
				if (check_all_device_ids(ha)) 	    
				/* SRAM, Instruction RAM and GP RAM parity */
					WRT_REG_WORD(&reg->host_cmd,
				    (HC_ENABLE_PARITY + 0x7));
				else
					/* SRAM parity */
					WRT_REG_WORD(&reg->host_cmd,
					    (HC_ENABLE_PARITY + 0x1));
					PCI_POSTING(&reg->host_cmd);

#endif
				spin_unlock_irqrestore(&ha->hardware_lock, 
								flags);
			}

			/*
			 * if failover is enabled read the user configuration
			 */
			if (ha->flags.failover_enabled) {
				if (ConfigRequired > 0)
					mp_config_required = 1;
				else
					mp_config_required = 0;

				DEBUG2(printk("qla2x00_detect: "
				    "qla2x00_cfg_init for hba %ld\n",
				    ha->instance);)

				qla2x00_cfg_init(ha);
			}

			/* Enable chip interrupts. */
			qla2x00_enable_intrs(ha);

			/* Insert new entry into the list of adapters */
			ha->next = NULL;

			if (qla2x00_hostlist == NULL) {
				qla2x00_hostlist = ha;
			} else {
				cur_ha = qla2x00_hostlist;

				while (cur_ha->next != NULL)
					cur_ha = cur_ha->next;

				cur_ha->next = ha;
			}

			/* v2.19.5b6 */
			/*
			 * Wait around max loop_reset_delay secs for the
			 * devices to come on-line. We don't want Linux
			 * scanning before we are ready.
			 */
			wait_switch = jiffies + (ha->loop_reset_delay * HZ);
			for ( ; time_before(jiffies, wait_switch) &&
			    !(ha->device_flags & (DFLG_NO_CABLE |
				DFLG_FABRIC_DEVICES)) &&
			    (ha->device_flags & SWITCH_FOUND); ) {

				qla2x00_check_fabric_devices(ha);

				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(5);
			}

			/* List the target we have found */
			if (displayConfig && (!ha->flags.failover_enabled))
				qla2x00_display_fc_names(ha);

			printk(KERN_INFO"%s num_hosts=%d\n",__func__,num_hosts);
			ha->init_done = 1;
			num_hosts++;
		}
	} /* end of FOR */

	/* Decrement the usage count of module: qla2[23]00_conf */
#if defined(ISP2200)
	if (ql2x_extopts)
		 inter_module_put("qla22XX_conf");
#endif
#if defined(ISP2300)
	if (ql2x_extopts)
		inter_module_put("qla23XX_conf");
#endif

 	if (ql2xfailover) {
		/* remap any paths on other hbas */
		qla2x00_cfg_remap(qla2x00_hostlist);
		if (displayConfig)
			qla2x00_cfg_display_devices(displayConfig == 2);
	}
bailout:
	spin_lock_irq(&io_request_lock);

	LEAVE("qla2x00_detect");

	return num_hosts;
}

/**************************************************************************
*   qla2x00_register_with_Linux
*
* Description:
*   Free the passed in Scsi_Host memory structures prior to unloading the
*   module.
*
* Input:
*     ha - pointer to host adapter structure
*     maxchannels - MAX number of channels.
*
* Returns:
*  0 - Sucessfully reserved resources.
*  1 - Failed to reserved a resource.
**************************************************************************/
STATIC uint8_t
qla2x00_register_with_Linux(scsi_qla_host_t *ha, uint8_t maxchannels)
{
	struct Scsi_Host *host = ha->host;
	int ret;

	host->can_queue = max_srbs;  /* default value:-MAX_SRBS(4096)  */
	host->cmd_per_lun = 1;
	host->select_queue_depths = qla2x00_select_queue_depth;
	host->n_io_port = 0xFF;
	host->base = 0;
#if MEMORY_MAPPED_IO
	host->base = (unsigned long)ha->mmio_address;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,15)
	host->max_cmd_len = MAX_CMDSZ;
#endif
	host->max_channel = maxchannels;
	/* fix: 07/31 host->max_lun = MAX_LUNS-1; */
	host->max_lun = ha->max_luns;
	host->unique_id = ha->instance;
	host->max_id = ha->max_targets;

	/* set our host ID  (need to do something about our two IDs) */
	host->this_id = 255;

	/* Register the IRQ with Linux (sharable) */
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		 ret = request_irq(host->irq, qla24xx_intr_handler,
				    SA_INTERRUPT|SA_SHIRQ, DRIVER_NAME, ha);
	else
#endif 
		ret = request_irq(host->irq, qla2x00_intr_handler,
				    SA_INTERRUPT|SA_SHIRQ, DRIVER_NAME, ha);
	if (ret != 0) {
		printk(KERN_WARNING
		    "qla2x00 : Failed to reserve interrupt %d already in use\n",
		    host->irq);
		return 1;
	}

	/* Initialized the timer */
	START_TIMER(qla2x00_timer, ha, WATCH_INTERVAL);

	return 0;
}


/**************************************************************************
*   qla2x00_release
*
* Description:
*   Free the passed in Scsi_Host memory structures prior to unloading the
*   module.
*
* Input:
*     ha - pointer to host adapter structure
*
* Returns:
*  0 - Always returns good status
**************************************************************************/
int
qla2x00_release(struct Scsi_Host *host)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) host->hostdata;
#if  QL_TRACE_MEMORY
	int t;
#endif

	ENTER("qla2x00_release");

#if !defined(ISP2100) && !defined(ISP2200)
	if (ha->beacon_blink_led) {
		/* Beacon was not turned off?? */
		if ((ha->device_id & 0xff00) == 0x2300 ||
		    (ha->device_id & 0xff00) == 0x6300)
			qla23xx_stop_beacon(ha);
		else if ((ha->device_id & 0xff00) == 0x2400 ||
		    (ha->device_id & 0xff00) == 0x2500)
			qla24xx_stop_beacon(ha);
	}
#endif


	/* Disable timer */
	if (ha->timer_active)
		STOP_TIMER(qla2x00_timer,ha)

	/* Kill the kernel thread for this host */
	if (ha->dpc_handler != NULL ) {

#if defined(ISP2100)
		ha->dpc_notify = &qla2100_detect_sem;
#endif
#if defined(ISP2200)
		ha->dpc_notify = &qla2200_detect_sem;
#endif
#if defined(ISP2300)
		ha->dpc_notify = &qla2300_detect_sem;
#endif

		send_sig(SIGHUP, ha->dpc_handler, 1);

#if defined(ISP2100)
		down(&qla2100_detect_sem);
#endif
#if defined(ISP2200)
		down(&qla2200_detect_sem);
#endif
#if defined(ISP2300)
		down(&qla2300_detect_sem);
#endif

		ha->dpc_notify = NULL;
	}

#if APIDEV
	apidev_cleanup();
#endif
	 /* Stop currently executing firmware. */
	qla2x00_stop_firmware(ha);

	/* turn-off interrupts on the card */
	if (ha->interrupts_on)
		qla2x00_disable_intrs(ha);

	/* Detach interrupts */
	if (host->irq)
		free_irq(host->irq, ha);

	qla2x00_mem_free(ha);

	if (ha->flags.failover_enabled)
		qla2x00_cfg_mem_free(ha);

#if QL_TRACE_MEMORY
	for (t = 0; t < 1000; t++) {
		if (mem_trace[t] == 0L)
			continue;
		printk("mem_trace[%d]=%lx, %lx\n",
			t, mem_trace[t],mem_id[t]);
	}
#endif

	/* release io space registers  */
	pci_release_regions(ha->pdev);

#if MEMORY_MAPPED_IO
	if (ha->mmio_address)
		iounmap(ha->mmio_address);
#endif

	ha->flags.online = FALSE;

	LEAVE("qla2x00_release");

	return 0;
}

/**************************************************************************
*   qla2x00_info
*
* Description:
*
* Input:
*     host - pointer to Scsi host adapter structure
*
* Returns:
*     Return a text string describing the driver.
**************************************************************************/
const char *
qla2x00_info(struct Scsi_Host *host)
{
	static char qla2x00_buffer[255];
	char *bp;
	scsi_qla_host_t *ha;
	qla_boards_t   *bdp;

#if  APIDEV
	/* We must create the api node here instead of qla2x00_detect since we
	 * want the api node to be subdirectory of /proc/scsi/qla2x00 which
	 * will not have been created when qla2x00_detect exits, but which will
	 * have been created by this point.
	 */
	apidev_init(host);
#endif

	bp = &qla2x00_buffer[0];
	ha = (scsi_qla_host_t *)host->hostdata;
	bdp = &QLBoardTbl_fc[ha->devnum];
	memset(bp, 0, sizeof(qla2x00_buffer));
#if 1
	sprintf(bp,
			"QLogic %sPCI to Fibre Channel Host Adapter: "
			"bus %d device %d irq %d\n"
			"        Firmware version: %2d.%02d.%02d, "
			"Driver version %s\n",
			(char *)&bdp->bdName[0], ha->pdev->bus->number,
			PCI_SLOT(ha->pdev->devfn),
			host->irq,
			bdp->fwver[0], bdp->fwver[1], bdp->fwver[2],
			qla2x00_version_str);
#endif

	return bp;
}

/*
 * This routine will alloacte SP from the free queue
 * input:
 *        scsi_qla_host_t *
 * output:
 *        srb_t * or NULL
 */
STATIC srb_t *
qla2x00_get_new_sp(scsi_qla_host_t *ha)
{
	srb_t * sp = NULL;
	ulong  flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	if (!list_empty(&ha->free_queue)) {
		sp = list_entry(ha->free_queue.next, srb_t, list);
		__del_from_free_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	if (sp) {
		DEBUG4(
		if ((int)atomic_read(&sp->ref_count) != 0) {
			/* error */
			printk("qla2x00_get_new_sp: WARNING "
				"ref_count not zero.\n");
		})

		atomic_set(&sp->ref_count, 1);
		        
	}

	return (sp);
}

/**************************************************************************
*   qla2x00_check_tgt_status
*
* Description:
*     Checks to see if the target or loop is down.
*
* Input:
*     cmd - pointer to Scsi cmd structure
*
* Returns:
*   1 - if target is present
*   0 - if target is not present
*
**************************************************************************/
STATIC uint8_t
qla2x00_check_tgt_status(scsi_qla_host_t *ha, Scsi_Cmnd *cmd)
{
	os_lun_t        *lq;
	uint32_t         b, t, l;
	fc_port_t	*fcport;

	/* Generate LU queue on bus, target, LUN */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	if ((lq = GET_LU_Q(ha,t,l)) == NULL) {
		return(QL_STATUS_ERROR);
	}

	fcport = lq->fclun->fcport;

	if (TGT_Q(ha, t) == NULL || 
		l >= ha->max_luns ||
		(atomic_read(&fcport->state) == FC_DEVICE_DEAD) ||
		atomic_read(&fcport->ha->loop_state) == LOOP_DEAD ||
		(!atomic_read(&ha->loop_down_timer) && 
		atomic_read(&ha->loop_state) == LOOP_DOWN)||
		(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) ||
	 	ABORTS_ACTIVE  || 
		atomic_read(&ha->loop_state) != LOOP_READY) {

		DEBUG(printk(KERN_INFO
				"scsi(%ld:%2d:%2d:%2d): %s connection is "
				"down\n",
				ha->host_no,
				b,t,l,
				__func__);)

		CMD_RESULT(cmd) = DID_NO_CONNECT << 16;
		return(QL_STATUS_ERROR);
	}
	return (QL_STATUS_SUCCESS);
}

/**************************************************************************
*   qla2x00_check_port_status
*
* Description:
*     Checks to see if the port or loop is down.
*
* Input:
*     fcport - pointer to fc_port_t structure.
*
* Returns:
*   2 - if port or loop is in a transition state
*   1 - if port is not present
*   0 - if port is present
*
**************************************************************************/
STATIC uint8_t
qla2x00_check_port_status(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	if (fcport == NULL) {
		return (QL_STATUS_ERROR);
	}

	if (atomic_read(&fcport->state) == FC_DEVICE_DEAD ||
	    atomic_read(&fcport->ha->loop_state) == LOOP_DEAD) {
		return (QL_STATUS_ERROR);
	}

	if ((atomic_read(&fcport->state) != FC_ONLINE) ||
	    (!atomic_read(&ha->loop_down_timer) &&
		atomic_read(&ha->loop_state) == LOOP_DOWN) ||
	    (test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) ||
	    test_bit(CFG_ACTIVE, &ha->cfg_flags) ||
	    ABORTS_ACTIVE ||
	    atomic_read(&ha->loop_state) != LOOP_READY) {

		DEBUG(printk(KERN_INFO
				"%s(%ld): connection is down. fcport=%p.\n",
				__func__,
				ha->host_no,
				fcport);)

		return (QL_STATUS_BUSY);
	}
	return (QL_STATUS_SUCCESS);
}


#if defined(SH_HAS_CAN_QUEUE_MASK) 
static void update_host_queue_mask(scsi_qla_host_t *ha)
{
	unsigned long newmask;
	if ((max_srbs - ha->srb_cnt) > (REQUEST_ENTRY_CNT/5))
		newmask = (1 <<  ha->last_irq_cpu);
	else
		newmask = ~0;
		
	if (ha->host->can_queue_mask != newmask)
		ha->host->can_queue_mask = newmask;
}
#endif

/**************************************************************************
* qla2x00_queuecommand
*
* Description:
*     Queue a command to the controller.
*
* Input:
*     cmd - pointer to Scsi cmd structure
*     fn - pointer to Scsi done function
*
* Returns:
*   0 - Always
*
* Note:
* The mid-level driver tries to ensures that queuecommand never gets invoked
* concurrently with itself or the interrupt handler (although the
* interrupt handler may call this routine as part of request-completion
* handling).
**************************************************************************/
int
qla2x00_queuecommand(Scsi_Cmnd *cmd, void (*fn)(Scsi_Cmnd *))
{
	fc_port_t	*fcport;
	os_lun_t	*lq;
	os_tgt_t	*tq;
	scsi_qla_host_t	*ha, *ha2;
	srb_t		*sp;
	struct Scsi_Host	*host;

	uint32_t	b, t, l;
#if  BITS_PER_LONG <= 32
	uint32_t	handle;
#else
	u_long		handle;
#endif
	int pendingempty = 1;

	ENTER(__func__);

	host = cmd->host;
	ha = (scsi_qla_host_t *) host->hostdata;
	
	cmd->scsi_done = fn;
#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock(&io_request_lock);
#else
	spin_unlock(ha->host->host_lock);
#endif

	/*
	 * Allocate a command packet from the "sp" pool.  If we cant get back
	 * one then let scsi layer come back later.
	 */
	if ((sp = qla2x00_get_new_sp(ha)) == NULL) {
		printk(KERN_WARNING
			"%s(): Couldn't allocate memory for sp - retried.\n",
			__func__);

#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif

		LEAVE(__func__);
		return (1);
	}

	sp->cmd = cmd;
	CMD_SP(cmd) = (void *)sp;

	sp->flags = 0;
	sp->fo_retry_cnt = 0;
	sp->iocb_cnt = 0;
	sp->qfull_retry_count = 0;
	sp->err_id = 0;

	/* Generate LU queue on bus, target, LUN */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	/*
	 * Start Command Timer. Typically it will be 2 seconds less than what
	 * is requested by the Host such that we can return the IO before
	 * aborts are called.
	 */
	if ((CMD_TIMEOUT(cmd)/HZ) > QLA_CMD_TIMER_DELTA)
		qla2x00_add_timer_to_cmd(sp,
				(CMD_TIMEOUT(cmd)/HZ) - QLA_CMD_TIMER_DELTA);
	else
		qla2x00_add_timer_to_cmd(sp, (CMD_TIMEOUT(cmd)/HZ));

	if (l >= ha->max_luns) {
		sp->err_id = SRB_ERR_PORT;
		CMD_RESULT(cmd) = DID_NO_CONNECT << 16;
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		__sp_put(ha, sp);
		LEAVE(__func__);
		return (0);
	}

	lq = NULL;
	fcport = NULL;
	ha2 = ha;
	if ((tq = (os_tgt_t *) TGT_Q(ha, t)) != NULL &&
		(lq = (os_lun_t *) LUN_Q(ha, t, l)) != NULL ) {
		if (unlikely(ha->binding_type == BIND_BY_PORT_ID)) {
			if (tq->d_id.b24 == lq->fclun->fcport->d_id.b24) {
				fcport = lq->fclun->fcport;
				ha2 = fcport->ha;
			} else {
				lq = NULL;
			}
		} else {
			fcport = lq->fclun->fcport;
			ha2 = fcport->ha;
		}
 	}

	/* Set an invalid handle until we issue the command to ISP */
	/* then we will set the real handle value.                 */
	handle = INVALID_HANDLE;
	CMD_HANDLE(cmd) = (unsigned char *)handle;

	DEBUG4(printk("scsi(%ld:%2d:%2d): (queuecmd) queue sp = %p, "
			"flags=0x%x fo retry=%d, pid=%ld, cmd flags= 0x%x\n",
			ha->host_no,t,l,sp,sp->flags,sp->fo_retry_cnt,
			cmd->serial_number,cmd->flags);)

	/* Bookkeeping information */
	sp->r_start = jiffies;       /* time the request was recieved */
	sp->u_start = 0;

	/* Setup device queue pointers. */
	sp->tgt_queue = tq;
	sp->lun_queue = lq;

	/*
	 * NOTE : q is NULL
	 *
	 * 1. When device is added from persistent binding but has not been
	 *    discovered yet.The state of loopid == PORT_AVAIL.
	 * 2. When device is never found on the bus.(loopid == UNUSED)
	 *
	 * IF Device Queue is not created, or device is not in a valid state
	 * and link down error reporting is enabled, reject IO.
	 */
	if (fcport == NULL) {
		DEBUG3(printk("scsi(%ld:%2d:%2d): port unavailable\n",
				ha->host_no,t,l);)

		sp->err_id = SRB_ERR_PORT;
		CMD_RESULT(cmd) = DID_NO_CONNECT << 16;
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		__sp_put(ha, sp);
		return (0);
	}

	/* Only modify the allowed count if the target is a *non* tape device */
	if ((fcport->flags & FC_TAPE_DEVICE) == 0) {
		sp->flags &= ~SRB_TAPE;
		if (cmd->allowed < ql2xretrycount) {
			cmd->allowed = ql2xretrycount;
		}
	} else
		sp->flags |= SRB_TAPE;

	DEBUG5(printk("%s(): pid=%ld, opcode=%d, timeout= %d\n",
			__func__,
			cmd->serial_number,
			cmd->cmnd[0],
			CMD_TIMEOUT(cmd));)
	DEBUG5(qla2x00_print_scsi_cmd(cmd);)

	sp->flags &= ~SRB_ISP_COMPLETED;

	sp->fclun = lq->fclun;
	sp->ha = ha2;

	sp->cmd_length = CMD_CDBLEN(cmd);

	if (cmd->sc_data_direction == SCSI_DATA_UNKNOWN &&
		cmd->request_bufflen != 0) {

		DEBUG2(printk(KERN_WARNING
				"%s(): Incorrect data direction - transfer "
				"length=%d, direction=%d, pid=%ld, opcode=%x\n",
				__func__,
				cmd->request_bufflen,
				cmd->sc_data_direction,
				cmd->serial_number,
				cmd->cmnd[0]);)
	}

	/* Final pre-check :
	 *	Either PORT_DOWN_TIMER OR LINK_DOWN_TIMER Expired.
	 */
	if (atomic_read(&fcport->state) == FC_DEVICE_DEAD ||
	    atomic_read(&fcport->ha->loop_state) == LOOP_DEAD) {
		/*
		 * Add the command to the done-queue for later failover
		 * processing.
		 */
		if (atomic_read(&ha->loop_state) == LOOP_DEAD) 
			sp->err_id = SRB_ERR_LOOP;
		else
			sp->err_id = SRB_ERR_PORT;
		CMD_RESULT(cmd) = DID_NO_CONNECT << 16;
		add_to_done_queue(ha, sp);
		qla2x00_done(ha);
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		return (0);
	}

	/* ignore SPINUP commands for MSA1000 */
	if ((fcport->flags & (FC_MSA_DEVICE|FC_EVA_DEVICE)) &&
	    cmd->cmnd[0] == START_STOP) {
		CMD_RESULT(cmd) = DID_OK << 16;
		DEBUG2(printk(KERN_INFO
		    "%s(): Ignoring SPIN_STOP scsi command...\n ", __func__));
		add_to_done_queue(ha, sp);
		qla2x00_done(ha);
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		return (0);
	}

	/* if target suspended put incoming in retry_q */
	if (tq && test_bit(TGT_SUSPENDED, &tq->q_flags) &&
	    (sp->flags & SRB_TAPE) == 0) {
		qla2x00_extend_timeout(sp->cmd, ha->qfull_retry_delay << 2);
		add_to_scsi_retry_queue(ha,sp);
	} else
		pendingempty = add_to_pending_queue(ha, sp);
  
#if defined(ISP2100) || defined(ISP2200)
	if (ha->flags.online) {
		unsigned long flags;
		
		if (ha->response_ring_ptr->signature != RESPONSE_PROCESSED) {
			spin_lock_irqsave(&ha->hardware_lock, flags);	
			qla2x00_process_response_queue(ha);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}
#endif	
  
	/* we submit to the hardware if
	 * 1) we're on the cpu the irq's arrive on or
	 * 2) there are very few io's outstanding.
	 * in all other cases we'll let an irq pick up our IO and submit it
	 * to the controller to improve affinity
	 */
	if (smp_processor_id() == ha->last_irq_cpu ||  /* condition 1 */
	   (((max_srbs - ha->srb_cnt) < REQUEST_ENTRY_CNT/10) && /* less than 10% outstanding io's */
	   (pendingempty)))
		qla2x00_next(ha);
	
#if !defined(SH_HAS_HOST_LOCK) 
	spin_lock_irq(&io_request_lock);
#else
	spin_lock_irq(ha->host->host_lock);
#endif

#if defined(SH_HAS_CAN_QUEUE_MASK) 
	update_host_queue_mask(ha);
#endif

	LEAVE(__func__);
	return (0);
}

/*
 * qla2x00_eh_wait_on_command
 *    Waits for the command to be returned by the Firmware for some
 *    max time.
 *
 * Input:
 *    ha = actual ha whose done queue will contain the command
 *	      returned by firmware.
 *    cmd = Scsi Command to wait on.
 *    flag = Abort/Reset(Bus or Device Reset)
 *
 * Return:
 *    Not Found : 0
 *    Found : 1
 */
STATIC int
qla2x00_eh_wait_on_command(scsi_qla_host_t *ha, Scsi_Cmnd *cmd)
{
#define ABORT_POLLING_PERIOD (HZ)
#define ABORT_WAIT_TIME	(10 * HZ)/ABORT_POLLING_PERIOD /* 10 seconds */

	int		found = 0;
	int		done = 0;
	srb_t		*rp = NULL;
	struct list_head *list, *temp;
	u_long		cpu_flags = 0;
	u_long		max_wait_time = ABORT_WAIT_TIME;

	ENTER(__func__);

	do {
		/* Check on done queue */
		if (!found) {
			spin_lock_irqsave(&ha->list_lock, cpu_flags);
			list_for_each_safe(list, temp, &ha->done_queue) {
				rp = list_entry(list, srb_t, list);

				/*
				* Found command.  Just exit and wait for the
				* cmd sent to OS.
			 	*/
				if (cmd == rp->cmd) {
					found++;
					DEBUG3(printk("%s: found in done "
							"queue.\n", __func__);)
					break;
				}
			}
			spin_unlock_irqrestore(&ha->list_lock, cpu_flags);
		}

		/* Complete the cmd right away */
		if (found) { 
			qla2x00_delete_from_done_queue(ha, rp);
			__sp_put(ha, rp);
			done++;
			break;
		}


#if !defined(SH_HAS_HOST_LOCK)
		spin_unlock_irq_dump(&io_request_lock);
#else
		spin_unlock_irq_dump(ha->host->host_lock);
#endif

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ABORT_POLLING_PERIOD);

#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif

	} while ((max_wait_time--));

	if (done){
	   printk(KERN_INFO "%s: found cmd=%p.\n", __func__, cmd);
	} else if (found) {
		/* Immediately return command to the mid-layer */
		qla2x00_delete_from_done_queue(ha, rp);
		__sp_put(ha, rp);
		done++;
	}

	LEAVE(__func__);

	return(done);
}

/*
 * qla2x00_wait_for_hba_online
 *    Wait till the HBA is online after going through 
 *    <= MAX_RETRIES_OF_ISP_ABORT  or
 *    finally HBA is disabled ie marked offline
 *
 * Input:
 *     ha - pointer to host adapter structure
 * 
 * Note:    
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *
 * Return:
 *    Success (Adapter is online) : 0
 *    Failed  (Adapter is offline/disabled) : 1
 */
static inline int 
qla2x00_wait_for_hba_online(scsi_qla_host_t *ha)
{
	int 	 return_status ;
	unsigned long		wait_online = 0;

	ENTER(__func__);

	 for (wait_online = jiffies + (MAX_LOOP_TIMEOUT *HZ);
		 ((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) ||
		test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) ||
		test_bit(ISP_ABORT_RETRY, &ha->dpc_flags) ||
		(ha->ioctl_err_cmd == NULL && ha->dpc_active)) &&
		time_before(jiffies,wait_online) ; ){

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	/* check Adapter is online/offline */
	if(ha->flags.online == TRUE ) 
		return_status = QL_STATUS_SUCCESS; 
	else
		return_status = QL_STATUS_ERROR;

	DEBUG(printk(KERN_INFO "%s return_status=%d\n",__func__,return_status);)
	LEAVE(__func__);

	return(return_status);
}
/*
 * qla2x00_wait_for_loop_ready
 *    Wait for MAX_LOOP_TIMEOUT(5 min) value for loop
 *    to be in LOOP_READY state.	 
 * Input:
 *     ha - pointer to host adapter structure
 * 
 * Note:    
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *    
 *
 * Return:
 *    Success (LOOP_READY) : 0
 *    Failed  (LOOP_NOT_READY) : 1
 */
static inline int 
qla2x00_wait_for_loop_ready(scsi_qla_host_t *ha)
{
	int 	 return_status = QL_STATUS_SUCCESS ;
	unsigned long loop_timeout ;

	ENTER(__func__);
	/* wait for 5 min at the max for loop to be ready */
	loop_timeout = jiffies + (MAX_LOOP_TIMEOUT * HZ ); 

	while (((test_bit(LOOP_RESET_NEEDED, &ha->dpc_flags)) ||
	    (!atomic_read(&ha->loop_down_timer) &&
		atomic_read(&ha->loop_state) == LOOP_DOWN) ||
	    test_bit(CFG_ACTIVE, &ha->cfg_flags) ||
	    atomic_read(&ha->loop_state) != LOOP_READY)) {

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(3 * HZ);
		if (time_after_eq(jiffies, loop_timeout)) {
			return_status = QL_STATUS_ERROR;
			break;
		}
	}
	DEBUG(printk(KERN_INFO "%s :return_status=%d\n",__func__,return_status);)
	LEAVE(__func__);
	return return_status;	
}

/**************************************************************************
* qla2xxx_eh_abort
*
* Description:
*    The abort function will abort the specified command.
*
* Input:
*    cmd = Linux SCSI command packet to be aborted.
*
* Returns:
*    Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
int
qla2xxx_eh_abort(Scsi_Cmnd *cmd)
{
	int		i;
	int		return_status = FAILED;
	os_lun_t	*q;
	scsi_qla_host_t *ha;
	scsi_qla_host_t *vis_ha;
	srb_t		*sp;
	srb_t		*rp;
	struct list_head *list, *temp;
	struct Scsi_Host *host;
	uint8_t		found = 0;
	uint32_t	b, t, l;
	unsigned long	flags;


	ENTER("qla2xxx_eh_abort");

	/* Get the SCSI request ptr */
	sp = (srb_t *) CMD_SP(cmd);

	/*
	 * If sp is NULL, command is already returned.
	 * sp is NULLED just before we call back scsi_done
	 *
	 */
	if ((sp == NULL)) {
		/* no action - we don't have command */
		printk(KERN_INFO "qla2xxx_eh_abort: cmd already done sp=%p\n"
				,sp);
		DEBUG(printk("qla2xxx_eh_abort: cmd already done sp=%p\n",sp);)
		return(SUCCESS);
	}
	if (sp) {
		DEBUG(printk("qla2xxx_eh_abort: refcount %i \n",
		    atomic_read(&sp->ref_count));)
	}

	vis_ha = (scsi_qla_host_t *) cmd->host->hostdata;
	vis_ha->eh_start = 0;
	if (vis_ha->flags.failover_enabled)
		/* Get Actual HA pointer */
		ha = (scsi_qla_host_t *)sp->ha;
	else
		ha = (scsi_qla_host_t *)cmd->host->hostdata;

	host = ha->host;


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)
	/* Check for possible pending interrupts. */
	qla2x00_process_risc_intrs(ha);
#endif

	/* Generate LU queue on bus, target, LUN */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	q = GET_LU_Q(vis_ha, t, l);

	if (qla2x00_verbose)
		printk(KERN_INFO
			"%s scsi(%ld:%d:%d:%d): cmd_timeout_in_sec=0x%lx.\n",
			__func__,ha->host_no, (int)b, (int)t, (int)l,
			(unsigned long)CMD_TIMEOUT(cmd)/HZ);
	/*
	 * if no LUN queue then something is very wrong!!!
	 */
	if (q == NULL) {
		printk(KERN_WARNING
			"qla2x00: (%x:%x:%x) No LUN queue.\n", b, t, l);

		/* no action - we don't have command */
		return(FAILED);
	}

	DEBUG2(printk(KERN_INFO "scsi(%ld): ABORTing cmd=%p sp=%p jiffies = 0x%lx, "
	    "timeout=%lx, dpc_flags=%lx, vis_ha->dpc_flags=%lx\n",
	    ha->host_no,
	    cmd,
	    sp,
	    jiffies,
	    (unsigned long)CMD_TIMEOUT(cmd)/HZ,
	    ha->dpc_flags,
	    vis_ha->dpc_flags);)
	DEBUG2(qla2x00_print_scsi_cmd(cmd));
	DEBUG2(qla2x00_print_q_info(q);)

#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock_irq(&io_request_lock);
#else
	spin_unlock_irq(ha->host->host_lock);
#endif
	/* Blocking call-Does context switching if abort isp is active etc */  
	if( qla2x00_wait_for_hba_online(ha) != QL_STATUS_SUCCESS){
		DEBUG2(printk(KERN_INFO "%s failed:board disabled\n",__func__);)
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		return(FAILED);
	}

#if !defined(SH_HAS_HOST_LOCK) 
	spin_lock_irq(&io_request_lock);
#else
	spin_lock_irq(ha->host->host_lock);
#endif

	/* Search done queue */
	spin_lock_irqsave(&ha->list_lock,flags);
	list_for_each_safe(list, temp, &ha->done_queue) {
		rp = list_entry(list, srb_t, list);

		if (cmd != rp->cmd)
			continue;

		/*
		 * Found command.Remove it from done list.
		 * And proceed to post completion to scsi mid layer.
		 */
		return_status = SUCCESS;
		found++;
		qla2x00_delete_from_done_queue(ha, sp);

		break;
	} /* list_for_each_safe() */
	spin_unlock_irqrestore(&ha->list_lock, flags);

	/*
	 * Return immediately if the aborted command was already in the done
	 * queue
	 */
	if (found) {
		printk(KERN_INFO "qla2xxx_eh_abort: Returning completed "
			"command=%p sp=%p\n", cmd, sp);
		__sp_put(ha, sp);
		return (return_status);
	}
	

	/*
	 * See if this command is in the retry queue
	 */
	if (!found) {
		DEBUG3(printk("qla2xxx_eh_abort: searching sp %p "
		    "in retry queue.\n", sp);)

		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &ha->retry_queue) {
			rp = list_entry(list, srb_t, list);

			if (cmd != rp->cmd)
				continue;


			DEBUG2(printk(KERN_INFO "qla2xxx_eh_abort: found "
			    "in retry queue. SP=%p\n", sp);)

			__del_from_retry_queue(ha, rp);
			CMD_RESULT(rp->cmd) = DID_ABORT << 16;
			__add_to_done_queue(ha, rp);

			return_status = SUCCESS;
			found++;

			break;

		} /* list_for_each_safe() */
		spin_unlock_irqrestore(&ha->list_lock, flags);
	}

	/*
	 * Search failover queue
	 */
	if (ha->flags.failover_enabled) {
		if (!found) {
			DEBUG3(printk("qla2xxx_eh_abort: searching sp %p "
					"in failover queue.\n", sp);)

			spin_lock_irqsave(&ha->list_lock, flags);
			list_for_each_safe(list, temp, &ha->failover_queue) {
				rp = list_entry(list, srb_t, list);

				if (cmd != rp->cmd)
					continue;

				DEBUG2(printk(KERN_INFO
						"qla2xxx_eh_abort: found "
						"in failover queue. SP=%p\n",
						sp);)

				/* Remove srb from failover queue. */
				__del_from_failover_queue(ha, rp);
				CMD_RESULT(rp->cmd) = DID_ABORT << 16;
				__add_to_done_queue(ha, rp);

				return_status = SUCCESS;
				found++;

				break;

			} /* list_for_each_safe() */
			spin_unlock_irqrestore(&ha->list_lock, flags);
		} /*End of if !found */
	}

	/*
	 * Our SP pointer points at the command we want to remove from the
	 * pending queue providing we haven't already sent it to the adapter.
	 */
	if (!found) {
		DEBUG3(printk("qla2xxx_eh_abort: searching sp %p "
		    "in pending queue.\n", sp);)

		spin_lock_irqsave(&vis_ha->list_lock, flags);
		list_for_each_safe(list, temp, &vis_ha->pending_queue) {
			rp = list_entry(list, srb_t, list);
			if (rp->cmd != cmd)
				continue;

			/* Remove srb from LUN queue. */
			rp->flags |=  SRB_ABORTED;

			DEBUG2(printk(KERN_INFO 
			    "qla2xxx_eh_abort: Cmd in pending queue."
			    " serial_number %ld.\n",
			    sp->cmd->serial_number);)

			__del_from_pending_queue(vis_ha, rp);
			CMD_RESULT(cmd) = DID_ABORT << 16;

			__add_to_done_queue(vis_ha, rp);

			return_status = SUCCESS;

			found++;
			break;
		} /* list_for_each_safe() */
		spin_unlock_irqrestore(&vis_ha->list_lock, flags);
	} /*End of if !found */

	if (!found) {  /* find the command in our active list */
		DEBUG3(printk("qla2xxx_eh_abort: searching sp %p "
		    "in outstanding queue.\n", sp);)

		spin_lock_irqsave(&ha->hardware_lock, flags);
		for (i = 1; i < MAX_OUTSTANDING_COMMANDS; i++) {
			sp = ha->outstanding_cmds[i];

			if (sp == NULL)
				continue;

			if (sp->cmd != cmd)
				continue;


			DEBUG2(printk(
			   KERN_INFO "qla2xxx_eh_abort(%ld): aborting sp %p "
			    "from RISC. pid=%d sp->state=%x\n",
			    ha->host_no, 
			    sp, 
			    (int)sp->cmd->serial_number,
			    sp->state);)
			DEBUG2(printk(KERN_INFO 
			   "qla2xxx_eh_abort(%ld): aborting sp %p "
			    "from RISC. pid=%d sp->state=%x\n",
			    ha->host_no, 
			    sp, 
			    (int)sp->cmd->serial_number,
			    sp->state);)
			DEBUG(qla2x00_print_scsi_cmd(cmd);)
			DEBUG(qla2x00_print_q_info(q);)

			/* Get a reference to the sp and drop the lock.*/
			sp_get(ha,sp);

			spin_unlock_irqrestore(&ha->hardware_lock, flags);
#if !defined(SH_HAS_HOST_LOCK)
			spin_unlock(&io_request_lock);
#else
			spin_unlock(ha->host->host_lock);
#endif

			if (qla2x00_abort_command(ha, sp)) {
				DEBUG2(printk(KERN_INFO 
				"qla2xxx_eh_abort: abort_command "
				    "mbx failed.\n");)
				return_status = FAILED;
			} else {
				DEBUG3(printk("qla2xxx_eh_abort: abort_command "
				    " mbx success.\n");)
				return_status = SUCCESS;
			}

			sp_put(ha,sp);
#if !defined(SH_HAS_HOST_LOCK)
			spin_lock_irq(&io_request_lock);
#else
			spin_lock_irq(ha->host->host_lock);
#endif
			spin_lock_irqsave(&ha->hardware_lock, flags);

			/*
			 * Regardless of mailbox command status, go check on
			 * done queue just in case the sp is already done.
			 */
			break;

		}/*End of for loop */
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

	} /*End of if !found */

	  /*Waiting for our command in done_queue to be returned to OS.*/
	if (qla2x00_eh_wait_on_command(ha, cmd) != 0) {
		DEBUG2(printk(KERN_INFO "qla2xxx_eh_abort: cmd returned back to OS.\n");)
		return_status = SUCCESS;
	}

	if (return_status == FAILED) {
		printk(KERN_INFO "qla2xxx_eh_abort Exiting: status=Failed\n");
		return FAILED;
	}

	DEBUG(printk("qla2xxx_eh_abort: Exiting. return_status=0x%x.\n",
	    return_status));
	DEBUG2(printk(KERN_INFO "qla2xxx_eh_abort: Exiting. return_status=0x%x.\n",
	    return_status));

	LEAVE("qla2xxx_eh_abort");

	return(return_status);
}

/**************************************************************************
* qla2x00_eh_wait_for_pending_target_commands
*
* Description:
*    Waits for all the commands to come back from the specified target.
*
* Input:
*    ha - pointer to scsi_qla_host structure.
*    t  - target 	
* Returns:
*    Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
int
qla2x00_eh_wait_for_pending_target_commands(scsi_qla_host_t *ha, int t)
{
	int	cnt;
	int	status;
	unsigned long	flags;
	srb_t		*sp;
	Scsi_Cmnd	*cmd;

	status = 0;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if (SCSI_TCN_32(cmd) == t) {
				if(qla2x00_eh_wait_on_command(ha, cmd) == 0){
					status = 1;
					break; 
				}
			}
		}
		else {
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}
	return (status);
}


/**************************************************************************
* qla2xxx_eh_device_reset
*
* Description:
*    The device reset function will reset the target and abort any
*    executing commands.
*
*    NOTE: The use of SP is undefined within this context.  Do *NOT*
*          attempt to use this value, even if you determine it is 
*          non-null.
*
* Input:
*    cmd = Linux SCSI command packet of the command that cause the
*          bus device reset.
*
* Returns:
*    SUCCESS/FAILURE (defined as macro in scsi.h).
*
**************************************************************************/
int
qla2xxx_eh_device_reset(Scsi_Cmnd *cmd)
{
	int		return_status;
	uint32_t	b, t, l;
	scsi_qla_host_t	*ha;
	os_tgt_t	*tq;
	os_lun_t	*lq;
	fc_port_t	*fcport_to_reset;
	srb_t		*rp;
	unsigned long	flags;
	struct list_head *list, *temp;


	return_status = FAILED;
	if (cmd == NULL) {
		printk(KERN_INFO
			"%s(): **** SCSI mid-layer passing in NULL cmd\n",
			__func__);

		return (return_status);
	}

	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);
	ha = (scsi_qla_host_t *)cmd->host->hostdata;

	tq = TGT_Q(ha, t);
	if (tq == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL TGT_Q\n",
			__func__);

		return (return_status);
	}
	lq = (os_lun_t *)LUN_Q(ha, t, l);
	if (lq == NULL) {
		printk(KERN_INFO
		    "%s(): **** CMD derives a NULL LUN_Q\n", __func__);

		return (return_status);
	}
	fcport_to_reset = lq->fclun->fcport;

	/*
	 * If we are coming in from the back-door, stall I/O until
	 * completion
	 */
	set_bit(TGT_SUSPENDED, &tq->q_flags);

	ha->eh_start = 0;

#if STOP_ON_RESET
	printk(debug_buff,"Resetting Device= 0x%x\n", (int)cmd);
	qla2x00_panic(__func__, ha->host);
#endif

	if (qla2x00_verbose)
		printk(KERN_INFO
			"scsi(%ld:%d:%d:%d): DEVICE RESET ISSUED.\n",
			ha->host_no, (int)b, (int)t, (int)l);

	DEBUG2(printk(KERN_INFO
	    "scsi(%ld): DEVICE_RESET cmd=%p jiffies = 0x%lx, timeout=%lx, "
	    "dpc_flags=%lx, status=%x allowed=%d cmd.state=%x\n",
	    ha->host_no, cmd, jiffies, (unsigned long)CMD_TIMEOUT(cmd)/HZ,
	    ha->dpc_flags, cmd->result, cmd->allowed, cmd->state);)

 	/*
 	 * Clear commands from the retry queue
 	 */
 	spin_lock_irqsave(&ha->list_lock, flags);
 	list_for_each_safe(list, temp, &ha->retry_queue) {
 		rp = list_entry(list, srb_t, list);
 
 		if( t != SCSI_TCN_32(rp->cmd) ) 
 			continue;
 
 		DEBUG2(printk(KERN_INFO "qla2xxx_eh_reset: found "
 		    "in retry queue. SP=%p\n", rp);)
 
 		__del_from_retry_queue(ha, rp);
 		CMD_RESULT(rp->cmd) = DID_RESET << 16;
 		__add_to_done_queue(ha, rp);
 
 	} /* list_for_each_safe() */
 	spin_unlock_irqrestore(&ha->list_lock, flags);

#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock_irq_dump(&io_request_lock);
#else
	spin_unlock_irq_dump(ha->host->host_lock);
#endif
	/* Blocking call-Does context switching if abort isp is active etc */  
	if (qla2x00_wait_for_hba_online(ha) != QL_STATUS_SUCCESS) {
		DEBUG2(printk(KERN_INFO "%s failed:board disabled\n",__func__);)
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		goto eh_dev_reset_done;
	}

	/* Blocking call-Does context switching if loop is Not Ready */
	if (qla2x00_wait_for_loop_ready(ha) == QL_STATUS_SUCCESS) {
		clear_bit(DEVICE_RESET_NEEDED, &ha->dpc_flags);

		if (qla2x00_device_reset(ha, fcport_to_reset) == 0) {
			return_status = SUCCESS;
		}

#if defined(LOGOUT_AFTER_DEVICE_RESET)
		if (return_status == SUCCESS) {
			if (fcport_to_reset->flags & FC_FABRIC_DEVICE) {
				qla2x00_fabric_logout(ha,
				    fcport_to_reset->loop_id
				    fcport_to_reset->d_id.b.domain,
				    fcport_to_reset->d_id.b.area,
				    fcport_to_reset->d_id.b.al_pa);
				qla2x00_mark_device_lost(ha, fcport_to_reset,
				    1);
			}
		}
#endif
	} else {
		DEBUG2(printk(KERN_INFO
		    "%s failed: loop not ready\n",__func__);)
	}

#if !defined(SH_HAS_HOST_LOCK)
	spin_lock_irq(&io_request_lock);
#else
	spin_lock_irq(ha->host->host_lock);
#endif

	if (return_status == FAILED) {
		DEBUG3(printk("%s(%ld): device reset failed\n",
		    __func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): device reset failed\n",
		    __func__,ha->host_no);

		goto eh_dev_reset_done;
	}

	/*
	 * If we are coming down the EH path, wait for all commands to
	 * complete for the device.
	 */
	if (cmd->host->eh_active) {
		if (qla2x00_eh_wait_for_pending_target_commands(ha, t))
			return_status = FAILED;

		if (return_status == FAILED) {
			DEBUG3(printk("%s(%ld): failed while waiting for "
			    "commands\n", __func__, ha->host_no);)
			printk(KERN_INFO "%s(%ld): failed while waiting for "
			    "commands\n", __func__, ha->host_no); 

			goto eh_dev_reset_done;
		}
	}


	printk(KERN_INFO
		"scsi(%ld:%d:%d:%d): DEVICE RESET SUCCEEDED.\n",
		ha->host_no, (int)b, (int)t, (int)l);

eh_dev_reset_done:

	clear_bit(TGT_SUSPENDED, &tq->q_flags);

	return (return_status);
}

/**************************************************************************
* qla2x00_eh_wait_for_pending_commands
*
* Description:
*    Waits for all the commands to come back from the specified host.
*
* Input:
*    ha - pointer to scsi_qla_host structure.
*
* Returns:
*    1 : SUCCESS
*    0 : FAILED
*
* Note:
**************************************************************************/
int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *ha)
{
	int	cnt;
	int	status;
	unsigned long	flags;
	srb_t		*sp;
	Scsi_Cmnd	*cmd;

	status = 1;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if((status = qla2x00_eh_wait_on_command(ha, cmd)) == 0){
				break;
			}
		}
		else {
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}
	return (status);
}

scsi_qla_host_t *
qla2xxx_find_dest_hba(struct scsi_cmnd *cmd)
{
	os_tgt_t        *tq;
	os_lun_t        *lq;
	scsi_qla_host_t *vis_ha;
	scsi_qla_host_t *dest_ha = NULL;
	unsigned int    t, l;

	vis_ha = (scsi_qla_host_t *)cmd->device->host->hostdata;
	t = cmd->device->id;
	l = cmd->device->lun;

	tq = TGT_Q(vis_ha, t);
	if (tq == NULL) {
		printk(KERN_INFO
		    "%s(%ld): **** CMD derives a NULL TGT_Q\n", __func__, 
		    vis_ha->host_no);

		return (dest_ha);
	}
	lq = (os_lun_t *)LUN_Q(vis_ha, t, l);
	if (lq == NULL) {
		printk(KERN_INFO
		    "%s(): **** CMD derives a NULL LUN_Q\n", __func__);

		return (dest_ha);
	}
	dest_ha = lq->fclun->fcport->ha;

	return (dest_ha);
}


/**************************************************************************
* qla2xxx_eh_bus_reset
*
* Description:
*    The bus reset function will reset the bus and abort any executing
*    commands.
*
* Input:
*    cmd = Linux SCSI command packet of the command that cause the
*          bus reset.
*
* Returns:
*    SUCCESS/FAILURE (defined as macro in scsi.h).
*
**************************************************************************/
int
qla2xxx_eh_bus_reset(Scsi_Cmnd *cmd)
{
	int        return_status = SUCCESS;
	uint32_t   b, t, l;
	srb_t      *sp;
	scsi_qla_host_t *ha, *search_ha = NULL;

	ENTER("qla2xxx_eh_bus_reset");

	if (cmd == NULL) {
		printk(KERN_INFO
			"%s(): **** SCSI mid-layer passing in NULL cmd\n",
			__func__);

		return (FAILED);
	}

	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	ha = (scsi_qla_host_t *) cmd->host->hostdata;
	ha->eh_start=0;
	/* Find actual ha */
	sp = (srb_t *)CMD_SP(cmd);
	if (ha->flags.failover_enabled && sp != NULL && 
		ha->host->eh_active == EH_ACTIVE ) {
		if (sp) {
			ha = sp->ha;
		} else {
			if ((ha = qla2xxx_find_dest_hba(cmd)) == NULL)
				return (FAILED);
		}
	}


	if (ha == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL HA\n",
			__func__);

		return (FAILED);
	}

	for (search_ha = qla2x00_hostlist;
		(search_ha != NULL) && search_ha != ha;
		search_ha = search_ha->next)
		continue;

	if (search_ha == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL search HA\n",
			__func__);

		return (FAILED);
	}

#if  STOP_ON_RESET
	printk("Resetting the Bus= 0x%x\n", (int)cmd);
	qla2x00_print_scsi_cmd(cmd);
	qla2x00_panic("qla2100_reset", ha->host);
#endif

	if (qla2x00_verbose)
		printk(KERN_INFO
			"scsi(%ld:%d:%d:%d): LOOP RESET ISSUED.\n",
			ha->host_no, (int)b, (int)t, (int)l);

#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock_irq(&io_request_lock);
#else
	spin_unlock_irq(ha->host->host_lock);
#endif
	/* Blocking call-Does context switching if abort isp is active etc*/  
	if( qla2x00_wait_for_hba_online(ha) != QL_STATUS_SUCCESS){
		DEBUG2(printk(KERN_INFO "%s failed:board disabled\n",__func__);)
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		return(FAILED);
	}
	/* Blocking call-Does context switching if loop is Not Ready */ 
	if(qla2x00_wait_for_loop_ready(ha) == QL_STATUS_SUCCESS){

		clear_bit(LOOP_RESET_NEEDED, &ha->dpc_flags);

		if (qla2x00_loop_reset(ha) != 0) 
			return_status = FAILED;
	} else {
		return_status = FAILED;
	}
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif

	if (return_status == FAILED) {
		DEBUG3(printk("%s(%ld): reset failed\n",
				       	__func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): reset failed\n",
			       	__func__,ha->host_no);
		return FAILED;
	}

	/* Blocking Call. It goes to sleep waiting for cmd to get to done q */
	 /* Waiting for our command in done_queue to be returned to OS.*/

	if ( qla2x00_eh_wait_for_pending_commands(ha) == 0) {
		return_status = FAILED;
	}

	if(return_status == FAILED) {
		DEBUG3(printk("%s(%ld): reset failed\n",
				       	__func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): reset failed\n",
			       	__func__,ha->host_no);
		return FAILED;
	} else{
		DEBUG3(printk("%s(%ld): reset succeded\n",
				       	__func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): reset succeded\n",
			       	__func__,ha->host_no);
	}

	LEAVE("qla2xxx_eh_bus_reset");

	return (return_status);
}

/**************************************************************************
* qla2xxx_eh_host_reset
*
* Description:
*    The reset function will reset the Adapter.
*
* Input:
*      cmd = Linux SCSI command packet of the command that cause the
*            adapter reset.
*
* Returns:
*      Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
int
qla2xxx_eh_host_reset(Scsi_Cmnd *cmd)
{
	int		return_status = SUCCESS;
	scsi_qla_host_t	*ha; /* actual ha to reset. */
	scsi_qla_host_t	*search_ha;
	srb_t		*sp;
	uint32_t        b, t, l;

	ENTER("qla2xxx_eh_host_reset");

	if (cmd == NULL) {
		printk(KERN_INFO
			"%s(): **** SCSI mid-layer passing in NULL cmd\n",
			__func__);

		return (FAILED);
	}

	ha = (scsi_qla_host_t *)cmd->host->hostdata;
	ha->eh_start= 0;
	/* Find actual ha */
	sp = (srb_t *)CMD_SP(cmd);
	if (ha->flags.failover_enabled && sp != NULL && 
		ha->host->eh_active == EH_ACTIVE ) {
		if (sp) {
			ha = sp->ha;
		} else {
			if ((ha = qla2xxx_find_dest_hba(cmd)) == NULL)
				return (FAILED);
		}
	}

	if (ha == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL HA\n",
			__func__);

		return (FAILED);
	}

	for (search_ha = qla2x00_hostlist;
		(search_ha != NULL) && search_ha != ha;
		search_ha = search_ha->next)
		continue;

	if (search_ha == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL search HA\n",
			__func__);

		return (FAILED);
	}

	/* Display which one we're actually resetting for debug. */
	DEBUG(printk("qla2xxx_eh_host_reset:Resetting scsi(%ld).\n", 
			ha->host_no);)

#if  STOP_ON_RESET
	printk("Host Reset...  Command=\n");
	qla2x00_print_scsi_cmd(cmd);
	qla2x00_panic("qla2xxx_eh_host_reset", ha->host);
#endif

	/*
	 *  Now issue reset.
	 */
	b = SCSI_BUS_32(cmd);
	t = SCSI_TCN_32(cmd);
	l = SCSI_LUN_32(cmd);

	if (qla2x00_verbose) {
		printk(KERN_INFO
			"scsi(%ld:%d:%d:%d): now issue ADAPTER RESET.\n",
			((scsi_qla_host_t *)cmd->host->hostdata)->host_no,
			(int)b, 
			(int)t, 
			(int)l);
	}

	DEBUG2(printk(KERN_INFO
			"scsi(%ld:%d:%d:%d): now issue ADAPTER RESET "
			"to ha %ld.\n",
			((scsi_qla_host_t *)cmd->host->hostdata)->host_no,
			(int)b, (int)t, (int)l, ha->host_no);)

#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock_irq_dump(&io_request_lock);
#else
	spin_unlock_irq_dump(ha->host->host_lock);
#endif
	/* Blocking call-Does context switching if abort isp is active etc*/  
	if( qla2x00_wait_for_hba_online(ha) != QL_STATUS_SUCCESS){
#if !defined(SH_HAS_HOST_LOCK)
		spin_lock_irq(&io_request_lock);
#else
		spin_lock_irq(ha->host->host_lock);
#endif
		printk(KERN_INFO "%s(%ld): failed:board disabled\n",
			       	__func__,ha->host_no);
		return(FAILED);
	} else {
		/* Fixme-may be dpc thread is active and processing
		 * loop_resync,so wait a while for it to 
		 * be completed and then issue big hammer.Otherwise
		 * it may cause I/O failure as big hammer marks the
		 * devices as lost kicking of the port_down_timer
		 * while dpc is stuck for the mailbox to complete.
		 */
		/* Blocking call-Does context switching if loop is Not Ready */
		qla2x00_wait_for_loop_ready(ha);
		set_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
		if (qla2x00_abort_isp(ha)) {
			clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
			/* failed. schedule dpc to try */
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);

			if( qla2x00_wait_for_hba_online(ha)
				!= QL_STATUS_SUCCESS){
				return_status = FAILED;
				printk(KERN_INFO "%s(%ld): failed:board"
					" disabled\n",
					__func__,ha->host_no);
			}
		} 

		clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
	}
#if !defined(SH_HAS_HOST_LOCK)
	spin_lock_irq(&io_request_lock);
#else
	spin_lock_irq(ha->host->host_lock);
#endif
	if ( return_status == FAILED) {
		printk(KERN_INFO "%s(%ld): reset failed\n",
			       	__func__,ha->host_no);
		return FAILED;
	}

    /* Waiting for our command in done_queue to be returned to OS.*/
	if ( qla2x00_eh_wait_for_pending_commands(ha) == 0) {
		return_status = FAILED;
	}

	if(return_status == FAILED) {
		DEBUG3(printk("%s(%ld): reset failed\n",
				       	__func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): reset failed\n",
			       	__func__,ha->host_no);
		return FAILED;
	} else {
		DEBUG3(printk("%s(%ld): reset succeded\n",
				       	__func__,ha->host_no);)
		printk(KERN_INFO "%s(%ld): reset succeded\n",
			       	__func__,ha->host_no);
	}
	LEAVE("qla2xxx_eh_host_reset");

#if EH_DEBUG
	my_reset_success = 1;
#endif

	return(return_status);
}

/**************************************************************************
* qla1200_biosparam
*
* Description:
*   Return the disk geometry for the given SCSI device.
**************************************************************************/
int
qla2x00_biosparam(Disk *disk, kdev_t dev, int geom[])
{
	int heads, sectors, cylinders;
	int     ret;
	struct  buffer_head *bh;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,17)
	bh = bread(MKDEV(MAJOR(dev), MINOR(dev) & ~0xf), 0, block_size(dev));
#else
	bh = bread(MKDEV(MAJOR(dev), MINOR(dev) & ~0xf), 0, 1024);
#endif

	if (bh) {
		ret = scsi_partsize(bh, disk->capacity,
		    &geom[2], &geom[0], &geom[1]);
		brelse(bh);
		if (ret != -1)
			return (ret);
	}
	heads = 64;
	sectors = 32;
	cylinders = disk->capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = disk->capacity / (heads * sectors);
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return (0);
}

/**************************************************************************
* qla2x00_intr_handler
*
* Description:
*   Handles the actual interrupt from the adapter.
*
* Context: Interrupt
**************************************************************************/
void
qla2x00_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags = 0;
	unsigned long mbx_flags = 0;
	scsi_qla_host_t *ha;
	uint16_t    data;
	uint8_t     got_mbx = 0;
	device_reg_t *reg;
	unsigned long		intr_loop = 50; /* don't loop forever, interrupt are OFF */

	ENTER_INTR("qla2x00_intr_handler");

	ha = (scsi_qla_host_t *) dev_id;
	if (!ha) {
		printk(KERN_INFO
			"qla2x00_intr_handler: NULL host ptr\n");

		return;
	}
	reg = ha->iobase;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	while (((data = RD_REG_WORD(&reg->istatus)) & RISC_INT)
			&& intr_loop-- )
#else
	while (((data = RD_REG_WORD(&reg->host_status_lo)) & HOST_STATUS_INT)
			&& intr_loop-- )
#endif
	{
		ha->total_isr_cnt++;
		qla2x00_isr(ha, data, &got_mbx);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	qla2x00_next(ha);
	ha->last_irq_cpu = smp_processor_id();

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
		got_mbx && ha->flags.mbox_int) {
		/* There was a mailbox completion */
		DEBUG3(printk("qla2x00_intr_handler: going to "
				"get mbx reg lock.\n");)

		QLA_MBX_REG_LOCK(ha);
		MBOX_TRACE(ha,BIT_5);
		got_mbx = 0;

		if (ha->mcp == NULL) {
			DEBUG3(printk("qla2x00_intr_handler: error mbx "
					"pointer.\n");)
		} else {
			DEBUG3(printk("qla2x00_intr_handler: going to set mbx "
					"intr flags. cmd=%x.\n",
					ha->mcp->mb[0]);)
		}
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		DEBUG3(printk("qla2x00_intr_handler(%ld): going to wake up "
				"mbx function for completion.\n",
				ha->host_no);)
		MBOX_TRACE(ha,BIT_6);
		up(&ha->mbx_intr_sem);

		DEBUG3(printk("qla2x00_intr_handler: going to unlock mbx "
				"reg.\n");)
		QLA_MBX_REG_UNLOCK(ha);
	}

	if (!list_empty(&ha->done_queue))
		qla2x00_done(ha);

	/* Wakeup the DPC routine */
	if ((!ha->flags.mbox_busy &&
		(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		 test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) ||
		 test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ) ) && 
		ha->dpc_wait && !ha->dpc_active) {  /* v2.19.4 */

		up(ha->dpc_wait);
	}

#if defined(SH_HAS_CAN_QUEUE_MASK) 
 	update_host_queue_mask(ha);
#endif

	LEAVE_INTR("qla2x00_intr_handler");
}

#if defined(ISP2300)
static void
qla24xx_status_entry(scsi_qla_host_t *ha, struct sts_entry_24xx *pkt)
{
	unsigned	b, t, l;
	srb_t		*sp;
	os_lun_t	*lq;
	os_tgt_t	*tq;
	fc_port_t	*fcport;
	struct scsi_cmnd *cp;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint8_t		lscsi_status;
	int32_t		resid;
	uint32_t	sense_sz = 0;
	uint32_t	rsp_info_len;

	/* Fast path completion. */
	if (le16_to_cpu(pkt->comp_status) == CS_COMPLETE &&
	    (le16_to_cpu(pkt->scsi_status) & SS_MASK) == 0) {
		qla2x00_process_good_request(ha, pkt->handle, 0);

		return;
	}

	/* Validate handle. */
	if (pkt->handle < MAX_OUTSTANDING_COMMANDS) {
		sp = ha->outstanding_cmds[pkt->handle];
		ha->outstanding_cmds[pkt->handle] = NULL;
	} else
		sp = NULL;

	if (sp == NULL) {
		DEBUG2(printk("scsi(%ld): Status Entry invalid handle.\n",
		    ha->host_no));

		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);

		return;
	}
	cp = sp->cmd;
	if (cp == NULL) {
		DEBUG2(printk("scsi(%ld): Command already returned back to OS "
		    "pkt->handle=%d sp=%p sp->state:%d\n",
		    ha->host_no, pkt->handle, sp, sp->state));

		return;
	}

	if (ha->actthreads)
		ha->actthreads--;

	if (sp->lun_queue == NULL) {
		DEBUG2(printk("scsi(%ld): Status Entry invalid lun pointer.\n",
		    ha->host_no));

		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);

		return;
	}

	sp->lun_queue->out_cnt--;
	if (sp->fclun->io_cnt > 0)
		sp->fclun->io_cnt--;
	sp->fclun->s_time = jiffies - sp->u_start;

	comp_status = le16_to_cpu(pkt->comp_status);
	/* Mask of reserved bits 12-15, before we examine the scsi status */
	scsi_status = le16_to_cpu(pkt->scsi_status) & SS_MASK;
	lscsi_status = scsi_status & STATUS_MASK;

	CMD_ENTRY_STATUS(cp) = pkt->entry_status;
	CMD_COMPL_STATUS(cp) = comp_status;
	CMD_SCSI_STATUS(cp) = scsi_status;

	/* Generate LU queue on cntrl, target, LUN */
	b = cp->device->channel;
	t = cp->device->id;
	l = cp->device->lun,

	tq = sp->tgt_queue;
	lq = sp->lun_queue;

	/*
	 * If loop is in transient state Report DID_BUS_BUSY
	 */
	if ((comp_status != CS_COMPLETE || scsi_status != 0)) {
		if (!(sp->flags & (SRB_IOCTL | SRB_TAPE)) &&
		    (atomic_read(&ha->loop_down_timer) ||
			atomic_read(&ha->loop_state) != LOOP_READY)) {

			DEBUG2(printk("scsi(%ld:%d:%d:%d): Loop Not Ready - "
			    "pid=%lx.\n",
			    ha->host_no, b, t, l, cp->serial_number));

			qla2x00_extend_timeout(cp, EXTEND_CMD_TIMEOUT);
			add_to_retry_queue(ha, sp);
			return;
		}
	}

	/* Check for any FCP transport errors. */
	rsp_info_len = 0;
	host_to_fcp_swap(pkt->data, sizeof(pkt->data));
	if (scsi_status & SS_RESPONSE_INFO_LEN_VALID) {
		rsp_info_len = le32_to_cpu(pkt->rsp_data_len);
		if (rsp_info_len > 3 && pkt->data[3]) {
			DEBUG2(printk("scsi(%ld:%d:%d:%d) FCP I/O protocol "
			    "failure (%x/%02x%02x%02x%02x%02x%02x%02x%02x)..."
			    "retrying cmd=0x%x\n", ha->host_no, b, t, l,
			    rsp_info_len, pkt->data[0], pkt->data[1],
			    pkt->data[2], pkt->data[3], pkt->data[4],
			    pkt->data[5], pkt->data[6], pkt->data[7],
			    cp->cmnd[0]));

			cp->result = DID_BUS_BUSY << 16;
			add_to_done_queue(ha, sp);
			return;
		}
	}

	/*
	 * Based on Host and scsi status generate status code for Linux
	 */
	switch (comp_status) {
	case CS_COMPLETE:
		if (scsi_status == 0) {
			cp->result = DID_OK << 16;
			break;
		}
		if (scsi_status & (SS_RESIDUAL_UNDER | SS_RESIDUAL_OVER)) {
			resid = le32_to_cpu(pkt->rsp_residual_count);
			cp->resid = resid;
			if (sp->flags & SRB_IOCTL)
				CMD_RESID_LEN(cp) = resid;
			/* Handle mid-layer underflow */
			if (!lscsi_status && (unsigned)(cp->request_bufflen - resid) <
			    cp->underflow) {
				printk(KERN_INFO
				    "scsi(%ld:%d:%d:%d): Mid-layer underflow "
				    "detected (%x of %x bytes)...returning "
				    "error status.\n",
				    ha->host_no, b, t, l, resid,
				    cp->request_bufflen);

				cp->result = DID_ERROR << 16;
				break;
			}
		}
		if (lscsi_status == SS_BUSY_CONDITION) {
			cp->result = DID_BUS_BUSY << 16 | lscsi_status;
			break;
		}

		cp->result = DID_OK << 16 | lscsi_status;

		if (lscsi_status != SS_CHECK_CONDITION)
			break;

		/*
		 * Copy Sense Data into sense buffer
		 */
		memset(cp->sense_buffer, 0, sizeof(cp->sense_buffer));

		if (!(scsi_status & SS_SENSE_LEN_VALID))
			break;

		if (le32_to_cpu(pkt->sense_len) <
		    sizeof(cp->sense_buffer))
			sense_sz = le32_to_cpu(pkt->sense_len);
		else
			sense_sz = sizeof(cp->sense_buffer);

		CMD_ACTUAL_SNSLEN(cp) = sense_sz;
		sp->request_sense_length = sense_sz;
		sp->request_sense_ptr = cp->sense_buffer;

		if (sp->request_sense_length > 32)
			sense_sz = 32;

		memcpy(cp->sense_buffer, pkt->data + rsp_info_len, sense_sz);

		sp->request_sense_ptr += sense_sz;
		sp->request_sense_length -= sense_sz;
		if (sp->request_sense_length != 0)
			ha->status_srb = sp;

		if (!(sp->flags & (SRB_IOCTL | SRB_TAPE)) &&
		    qla2x00_check_sense(cp, lq) == QLA2X00_SUCCESS) {
			/* Throw away status_cont if any */
			ha->status_srb = NULL;
			add_to_scsi_retry_queue(ha, sp);
			return;
		}

		DEBUG5(printk("%s(): Check condition Sense data, "
		    "scsi(%ld:%d:%d:%d) cmd=%p pid=%ld\n",
		    __func__, ha->host_no, b, t, l, cp,
		    cp->serial_number));
#if 0
printk("*** %s(): scsi(%ld:%d:%d:%d) sense data (%x/%x/%x), pid=%ld oxid=%x\n",
    __func__, ha->host_no, b, t, l,
    cp->sense_buffer[2], cp->sense_buffer[12], cp->sense_buffer[13], 
    cp->serial_number,
    le16_to_cpu(pkt->ox_id));
qla2x00_dump_pkt(pkt);
#endif
		if (sense_sz)
			DEBUG5(qla2x00_dump_buffer(cp->sense_buffer,
			    CMD_ACTUAL_SNSLEN(cp)));
		break;

	case CS_DATA_UNDERRUN:
		DEBUG2(printk(KERN_INFO
		    "scsi(%ld:%d:%d) UNDERRUN status detected 0x%x-0x%x.\n",
		    ha->host_no, t, l, comp_status, scsi_status));

		resid = le32_to_cpu(pkt->rsp_residual_count);
		if (scsi_status & SS_RESIDUAL_UNDER) {
			cp->resid = resid;
			if (sp->flags & SRB_IOCTL)
				CMD_RESID_LEN(cp) = resid;
		}

		/*
		 * Check to see if SCSI Status is non zero. If so report SCSI 
		 * Status.
		 */
		if (lscsi_status != 0) {
			if (lscsi_status == SS_BUSY_CONDITION) {
				cp->result = DID_BUS_BUSY << 16 |
				    lscsi_status;
				break;
			}

			cp->result = DID_OK << 16 | lscsi_status;

			if (lscsi_status != SS_CHECK_CONDITION)
				break;

			/* Copy Sense Data into sense buffer */
			memset(cp->sense_buffer, 0, sizeof(cp->sense_buffer));

			if (!(scsi_status & SS_SENSE_LEN_VALID))
				break;

			if (le32_to_cpu(pkt->sense_len) <
			    sizeof(cp->sense_buffer))
				sense_sz = le32_to_cpu(pkt->sense_len);
			else
				sense_sz = sizeof(cp->sense_buffer);

			CMD_ACTUAL_SNSLEN(cp) = sense_sz;
			sp->request_sense_length = sense_sz;
			sp->request_sense_ptr = cp->sense_buffer;

			if (sp->request_sense_length > 32) 
				sense_sz = 32;

			memcpy(cp->sense_buffer, pkt->data + rsp_info_len,
			    sense_sz);

			sp->request_sense_ptr += sense_sz;
			sp->request_sense_length -= sense_sz;
			if (sp->request_sense_length != 0)
				ha->status_srb = sp;

			if (!(sp->flags & (SRB_IOCTL | SRB_TAPE)) &&
			    (qla2x00_check_sense(cp, lq) == QLA2X00_SUCCESS)) {
				ha->status_srb = NULL;
				add_to_scsi_retry_queue(ha, sp);
				return;
			}
			DEBUG5(printk("%s(): Check condition Sense data, "
			    "scsi(%ld:%d:%d:%d) cmd=%p pid=%ld\n",
			    __func__, ha->host_no, b, t, l, cp,
			    cp->serial_number));
#if 0
printk("*** %s(): scsi(%ld:%d:%d:%d) sense data (%x/%x/%x), pid=%ld oxid=%x\n",
    __func__, ha->host_no, b, t, l,
    cp->sense_buffer[2], cp->sense_buffer[12], cp->sense_buffer[13], 
    cp->serial_number,
    le16_to_cpu(pkt->ox_id));
#endif
			if (sense_sz)
				DEBUG5(qla2x00_dump_buffer(cp->sense_buffer,
				    CMD_ACTUAL_SNSLEN(cp)));
		} else {
			/*
			 * If RISC reports underrun and target does not report
			 * it then we must have a lost frame, so tell upper
			 * layer to retry it by reporting a bus busy.
			 */
			if (!(scsi_status & SS_RESIDUAL_UNDER)) {
				DEBUG2(printk("scsi(%ld:%d:%d:%d) Dropped "
				    "frame(s) detected (%x of %x bytes)..."
				    "retrying command.\n",
				    ha->host_no, b, t, l, resid,
				    cp->request_bufflen));

				cp->result = DID_BUS_BUSY << 16;
				ha->dropped_frame_error_cnt++;
				break;
			}

			/* Handle mid-layer underflow */
			if ((unsigned)(cp->request_bufflen - resid) <
			    cp->underflow) {
				printk(KERN_INFO
				    "scsi(%ld:%d:%d:%d): Mid-layer underflow "
				    "detected (%x of %x bytes)...returning "
				    "error status.\n",
				    ha->host_no, b, t, l, resid,
				    cp->request_bufflen);

				cp->result = DID_ERROR << 16;
				break;
			}

			/* Everybody online, looking good... */
			cp->result = DID_OK << 16;
		}
		break;

	case CS_DATA_OVERRUN:
		DEBUG2(printk(KERN_INFO
		    "scsi(%ld:%d:%d): OVERRUN status detected 0x%x-0x%x\n",
		    ha->host_no, t, l, comp_status, scsi_status));
		DEBUG2(printk(KERN_INFO
		    "CDB: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		    cp->cmnd[0], cp->cmnd[1], cp->cmnd[2], cp->cmnd[3],
		    cp->cmnd[4], cp->cmnd[5]));
		DEBUG2(printk(KERN_INFO
		    "PID=0x%lx req=0x%x xtra=0x%x -- returning DID_ERROR "
		    "status!\n",
		    cp->serial_number, cp->request_bufflen,
		    le32_to_cpu(pkt->rsp_residual_count)));

		cp->result = DID_ERROR << 16;
		break;

	case CS_PORT_LOGGED_OUT:
	case CS_PORT_CONFIG_CHG:
	case CS_PORT_BUSY:
	case CS_INCOMPLETE:
	case CS_PORT_UNAVAILABLE:
		/*
		 * If the port is in Target Down state, return all IOs for this
		 * Target with DID_NO_CONNECT ELSE Queue the IOs in the
		 * retry_queue.
		 */
		fcport = sp->fclun->fcport;
		DEBUG2(printk("scsi(%ld:%d:%d): status_entry: Port Down "
		    "pid=%ld, compl status=0x%x, port state=0x%x\n",
		    ha->host_no, t, l, cp->serial_number, comp_status,
		    atomic_read(&fcport->state)));

		if ((sp->flags & (SRB_IOCTL | SRB_TAPE)) ||
		    atomic_read(&fcport->state) == FC_DEVICE_DEAD) {
			cp->result = DID_NO_CONNECT << 16;
			if (atomic_read(&ha->loop_state) == LOOP_DOWN) 
				sp->err_id = SRB_ERR_LOOP;
			else
				sp->err_id = SRB_ERR_PORT;
			add_to_done_queue(ha, sp);
		} else {
			qla2x00_extend_timeout(cp, EXTEND_CMD_TIMEOUT);
			add_to_retry_queue(ha, sp);
		}

		if (atomic_read(&fcport->state) == FC_ONLINE) {
			qla2x00_mark_device_lost(ha, fcport, 1);
		}

		return;
		break;

	case CS_RESET:
		DEBUG2(printk(KERN_INFO
		    "scsi(%ld): RESET status detected 0x%x-0x%x.\n",
		    ha->host_no, comp_status, scsi_status));

		if (sp->flags & (SRB_IOCTL | SRB_TAPE)) {
			cp->result = DID_RESET << 16;
		} else {
			qla2x00_extend_timeout(cp, EXTEND_CMD_TIMEOUT);
			add_to_retry_queue(ha, sp);
			return;
		}
		break;

	case CS_ABORTED:
		/* 
		 * hv2.19.12 - DID_ABORT does not retry the request if we
		 * aborted this request then abort otherwise it must be a
		 * reset.
		 */
		DEBUG2(printk(KERN_INFO
		    "scsi(%ld): ABORT status detected 0x%x-0x%x.\n",
		    ha->host_no, comp_status, scsi_status));

		cp->result = DID_RESET << 16;
		break;

	case CS_TIMEOUT:
		DEBUG4(printk(KERN_INFO
		    "scsi(%ld:%d:%d:%d): TIMEOUT status detected 0x%x-0x%x "
		    "sflags=%x.\n", ha->host_no, b, t, l, comp_status,
		    scsi_status, le16_to_cpu(pkt->status_flags)));
		DEBUG2(printk(KERN_INFO
		    "scsi(%ld:%d:%d:%d): TIMEOUT status detected 0x%x-0x%x "
		    "\n", ha->host_no, b, t, l, comp_status,
		    scsi_status));

		cp->result = DID_BUS_BUSY << 16;

		fcport = lq->fclun->fcport;
#if FIXME
		/* Check to see if logout occurred */
		if ((le16_to_cpu(pkt->status_flags) & SF_LOGOUT_SENT)) {
			qla2x00_mark_device_lost(ha, fcport, 1);
		}
#endif
		break;

	case CS_QUEUE_FULL:
		DEBUG2(printk(KERN_INFO
		       "scsi(%ld:%d:%d): QUEUE FULL status detected "
		       "0x%x-0x%x, pid=%ld.\n",
			ha->host_no, 
			t,
			l,
			comp_status, 
			scsi_status, 
			sp->cmd->serial_number));
		/*
		 * SCSI Mid-Layer handles device queue full
		 */				 
		if (sp->qfull_retry_count <
		    ha->qfull_retry_count) {
			sp->qfull_retry_count++;
			qla2x00_suspend_target(ha, 
				sp->tgt_queue,
			    ha->qfull_retry_delay);
			qla2x00_extend_timeout(sp->cmd, 
				ha->qfull_retry_delay << 2);
			add_to_scsi_retry_queue(ha,sp);
			return;
		} else {
			printk( KERN_INFO
			"scsi(%ld:%d:%d): %s No more QUEUE FULL retries..\n",
			ha->host_no, t,l, __func__);
			clear_bit(TGT_SUSPENDED, &tq->q_flags);
			/* no more scsi retries */
			sp->cmd->retries = sp->cmd->allowed;
			CMD_RESULT(cp) = DID_ERROR << 16;
		}
		break;


	default:
		DEBUG3(printk("scsi(%ld): Error detected (unknown status) "
		    "0x%x-0x%x.\n",
		    ha->host_no, comp_status, scsi_status));
		cp->result = DID_ERROR << 16;
		break;
	}

	/* Place command on done queue. */
	if (ha->status_srb == NULL)
		add_to_done_queue(ha, sp);
}

/**
 * qla24xx_ms_entry() - Process a Management Server entry.
 * @ha: SCSI driver HA context
 * @index: Response queue out pointer
 */
static void
qla24xx_ms_entry(scsi_qla_host_t *ha, struct ct_entry_24xx *pkt) 
{
	srb_t          *sp;

	DEBUG3(printk("%s(%ld): pkt=%p pkthandle=%d.\n",
	    __func__, ha->host_no, pkt, pkt->handle));

	DEBUG9(printk("%s: ct pkt dump:\n", __func__);)
	DEBUG9(qla2x00_dump_buffer((void *)pkt, sizeof(struct ct_entry_24xx));)

	/* Validate handle. */
 	if (pkt->handle < MAX_OUTSTANDING_COMMANDS)
 		sp = ha->outstanding_cmds[pkt->handle];
	else
		sp = NULL;

	if (sp == NULL) {
		DEBUG2(printk("scsi(%ld): MS entry - invalid handle\n",
		    ha->host_no));
		DEBUG10(printk("scsi(%ld): MS entry - invalid handle\n",
		    ha->host_no));
		printk(KERN_WARNING
		    "scsi(%ld): MS entry - invalid handle %d\n",
		    ha->host_no, pkt->handle);

		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		return;
	}

	CMD_COMPL_STATUS(sp->cmd) = le16_to_cpu(pkt->comp_status);
	CMD_ENTRY_STATUS(sp->cmd) = pkt->entry_status;

	/* Free outstanding command slot. */
	ha->outstanding_cmds[pkt->handle] = NULL;

	add_to_done_queue(ha, sp);
}

/**
 * qla24xx_process_response_queue() - Process response queue entries.
 * @ha: SCSI driver HA context
 */
void
qla24xx_process_response_queue(struct scsi_qla_host *ha)
{
	struct device_reg_24xx *reg;
	struct sts_entry_24xx *pkt;

	if (!ha->flags.online)
		return;

	reg = (struct device_reg_24xx *)ha->iobase;
	while (ha->response_ring_ptr->signature != RESPONSE_PROCESSED) {
		pkt = (struct sts_entry_24xx *)ha->response_ring_ptr;

		ha->rsp_ring_index++;
		if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
			ha->rsp_ring_index = 0;
			ha->response_ring_ptr = ha->response_ring;
		} else {
			ha->response_ring_ptr++;
		}

		DEBUG4(printk(KERN_WARNING
		    "scsi(%ld): Received unknown response pkt type %x "
		    "entry status=%x entry count=%x handle=%x.\n",
		    ha->host_no, pkt->entry_type, pkt->entry_status,
		    pkt->entry_count, pkt->handle));

		if (pkt->entry_status != 0) {
			DEBUG2(printk(KERN_INFO
			    "scsi(%ld): Process error entry.\n", ha->host_no));

			//FIXME
			qla2x00_error_entry(ha, (sts_entry_t *) pkt);
			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}

		switch (pkt->entry_type) {
		case STATUS_TYPE:
			qla24xx_status_entry(ha, pkt);
			break;
		case STATUS_CONT_TYPE:
			qla2x00_status_cont_entry(ha, (sts_cont_entry_t *)pkt);
			break;
		case MS_IOCB_TYPE:
			qla24xx_ms_entry(ha, (struct ct_entry_24xx *)pkt);
			break;
#if FIXME
		case MBX_IOCB_TYPE:
			if (pkt->sys_define == SOURCE_ASYNC_IOCB) {
				qla2x00_process_iodesc(ha,
				    (struct mbx_entry_24xx *)pkt);
			} else {
				/* MBX IOCB Type Not Supported. */
				DEBUG4(printk(KERN_WARNING
				    "scsi(%ld): Received unknown MBX "
				    "IOCB response pkt type=%x "
				    "source=%x entry status=%x.\n",
				    ha->host_no, pkt->entry_type,
				    pkt->sys_define,
				    pkt->entry_status));
			}
			break;
#endif
		default:
			/* Type Not Supported. */
			DEBUG4(printk(KERN_WARNING
			    "scsi(%ld): Received unknown response pkt type %x "
			    "entry status=%x.\n",
			    ha->host_no, pkt->entry_type, pkt->entry_status));
			break;
		}
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/* Adjust ring index */
	WRT_REG_DWORD(&reg->rsp_q_out, ha->rsp_ring_index);
}

/*
*  qla24xx_isr
*      Calls I/O done on command completion.
*
* Input:
*      ha           = adapter block pointer.
*      INTR_LOCK must be already obtained.
*/
STATIC void
qla24xx_isr(scsi_qla_host_t *ha, uint32_t data, uint8_t *got_mbx)
{
        struct device_reg_24xx *reg;
	uint32_t     index;
	uint16_t     *iptr, *mptr;
	uint16_t     mailbox[MAILBOX_REGISTER_COUNT];
	uint16_t     cnt, temp1;
	uint16_t     temp2;
	uint8_t      mailbox_int;
	uint16_t     hccr;
	uint8_t      rscn_queue_index;

	ENTER(__func__);
        reg = (struct device_reg_24xx *)ha->iobase;
	/*
	 * Check for a paused RISC -- schedule an isp abort 
	 */
	if (data & HSRX_RISC_PAUSED) {
		hccr = RD_REG_DWORD(&reg->hccr);
		printk(KERN_INFO
			    "%s(%ld): RISC paused, dumping HCCR (%x)"
			    " and schedule an ISP abort (big-hammer)\n",
			    __func__, ha->host_no, hccr);
		printk("%s(%ld): RISC paused, dumping HCCR (%x) and"
			" schedule an ISP abort (big-hammer)\n",
			    __func__, ha->host_no, hccr);

		/*
		 * Issue a "HARD" reset in order for the RISC
		 * interrupt bit to be cleared.  Schedule a big
		 * hammmer to get out of the RISC PAUSED state.
		 */
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET);
		RD_REG_DWORD(&reg->hccr);
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		return;
	}

	/* Check for mailbox interrupt. */
	MBOX_TRACE(ha,BIT_2);
	temp2 = MSW(data);
	mailbox_int = 0;
	switch (data & 0xFF) {
		case ROM_MB_CMD_COMP:
		case ROM_MB_CMD_ERROR:
		case MB_CMD_COMP:
		case MB_CMD_ERROR:
		case ASYNC_EVENT:
			mailbox_int = 1;
			temp1 = temp2;
			break;
		case RESPONSE_QUEUE_INT:
			WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
			PCI_POSTING(&reg->hccr);
			goto response_queue_int;
			break;

		default:
			WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
			PCI_POSTING(&reg->hccr);
			goto isr_end;
			break;
	}

	if (mailbox_int) {
		MBOX_TRACE(ha,BIT_3);
		if (( temp1 == MBA_SCSI_COMPLETION) ||
			((temp1 >= RIO_MBS_CMD_CMP_1_16) && 
			(temp1 <= RIO_MBS_CMD_CMP_5_16))) {
			mailbox[1] = temp2;
			mailbox[2] = RD_REG_WORD(&reg->mailbox2);
			mailbox[3] = RD_REG_WORD(&reg->mailbox3);
			mailbox[5] = RD_REG_WORD(&reg->mailbox5);
			mailbox[6] = RD_REG_WORD(&reg->mailbox6);
			mailbox[7] = RD_REG_WORD(&reg->mailbox7);
		} else {
			MBOX_TRACE(ha,BIT_4);
			mailbox[0] = temp1;
			DEBUG3(printk("%s(): Saving return mbx data\n",
					__func__);)

			/* Get mailbox data. */
			mptr = &mailbox[1];
			iptr = (uint16_t *)&reg->mailbox1;
			for (cnt = 1; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
				*mptr = RD_REG_WORD(iptr);
				mptr++;
				iptr++;
			}
		}
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
		PCI_POSTING(&reg->hccr);

		DEBUG3(printk("%s(): mailbox interrupt mailbox[0] = %x.\n",
				__func__,
				temp1);)

		/* Handle asynchronous event */
		switch (temp1) {

			case MBA_ZIO_UPDATE:
				DEBUG5(printk("%s ZIO update completion\n",
							__func__);)
			        break;
			case MBA_SCSI_COMPLETION:	/* Completion */
				
				DEBUG5(printk("%s(): mailbox response "
						"completion.\n",
						__func__);)

				if (!ha->flags.online)
					break;

				/* Get outstanding command index  */
				index = le32_to_cpu(((uint32_t)(mailbox[2] << 16)) | mailbox[1]);

				qla2x00_process_good_request(ha,
						index, MBA_SCSI_COMPLETION);
				break;

			case RIO_MBS_CMD_CMP_1_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_1_16);
				}
				break;
			case RIO_MBS_CMD_CMP_2_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_2_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_2_16);
				}
				break;
			case RIO_MBS_CMD_CMP_3_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
				}
				break;
			case RIO_MBS_CMD_CMP_4_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[6]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
				}
				break;
			case RIO_MBS_CMD_CMP_5_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[6]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[7]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
				}
				break;
				
			case MBA_RESET:			/* Reset */

				DEBUG2(printk(KERN_INFO "scsi(%ld): %s: asynchronous "
						"RESET.\n",
						ha->host_no,
						__func__);)

				set_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);
				break;

			case MBA_SYSTEM_ERR:		/* System Error */

				printk(KERN_INFO
					"qla2x00: ISP System Error - mbx1=%xh, "
					"mbx2=%xh, mbx3=%xh.", mailbox[1],
					mailbox[2], mailbox[3]);
				if (mailbox[1] == 0 && mailbox[2] == 0) {
					ha->flags.online = FALSE;
					printk(KERN_INFO "Unrecoverable "
						" hardware error : Adapter"
						" being marked offline\n");
				} else
				    set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;

			case MBA_REQ_TRANSFER_ERR:  /* Request Transfer Error */

				printk(KERN_WARNING
					"qla2x00(%ld): ISP Request Transfer "
					"Error mbx1=0x%x.\n",ha->host_no,
					 mailbox[1]);
				DEBUG2(printk(KERN_INFO
					"qla2x00(%ld): ISP Request Transfer "
					"Error mbx1=0x%x.\n",ha->host_no,
					 mailbox[1]);)

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;

			case MBA_RSP_TRANSFER_ERR: /* Response Transfer Error */

				printk(KERN_WARNING
					"qla2100: ISP Response Transfer "
					"Error.\n");

				DEBUG2(printk(KERN_INFO "%s(): ISP Response Transfer "
						"Error.\n",
						__func__);)

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;

			case MBA_ATIOQ_TRASNFER_ERR :/* ATIO queue trasfer 
							error */	

				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"ATIO queue transfer err.\n",
						__func__);)
				break;

			case MBA_LIP_OCCURRED:	/* Loop Initialization	*/
						/*  Procedure		*/

				if (!qla2x00_quiet)
					printk(KERN_INFO
						"scsi(%ld): LIP occurred.\n",
						    ha->host_no);

				DEBUG2(printk(
					KERN_INFO "%s(): asynchronous "
					"MBA_LIP_OCCURRED.\n",
					__func__);)

				/* Save LIP sequence. */
				ha->lip_seq = mailbox[1];
				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
							LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				set_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
				set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);

				ha->flags.management_server_logged_in = 0;

				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LIP_OCCURRED, NULL);
				}

				ha->total_lip_cnt++;

				break;

			case MBA_LOOP_UP:

				printk(KERN_INFO
					"scsi(%ld): LOOP UP detected.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"MBA_LOOP_UP.\n",
						__func__);)

				ha->flags.management_server_logged_in = 0;
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LOOP_UP, NULL);
				}

				/*
				 * Save the current speed for use by ioctl and
				 * IP driver.
				 */
				ha->current_speed = EXT_DEF_PORTSPEED_1GBIT;
				if (mailbox[1] == 1)
					ha->current_speed = EXT_DEF_PORTSPEED_2GBIT;
				if (mailbox[1] == 3)
					ha->current_speed = EXT_DEF_PORTSPEED_4GBIT;
				if (mailbox[1] == 4)
					ha->current_speed = EXT_DEF_PORTSPEED_10GBIT;
				break;

			case MBA_LOOP_DOWN:

				printk(KERN_INFO
					"scsi(%ld): LOOP DOWN detected.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: asynchronous "
						"MBA_LOOP_DOWN.\n",
						ha->host_no, __func__);)
#if 0
				if (!(test_and_set_bit(LOOP_DOWN_IO_RECOVERY,
					    &ha->dpc_flags))) {
					printk("%s(%ld) LOOP_DOWN_IO_RECOVERY set\n",
					    	__func__, ha->host_no);
					printk(KERN_INFO 
					    	"%s(%ld) LOOP_DOWN_IO_RECOVERY set\n",
					    	__func__, ha->host_no);
					ha->cmd_wait_delay = 2;
				}
#endif

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					ha->device_flags |= DFLG_NO_CABLE;
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
					    LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}

				if (ql2xfdmienable)
					clear_bit(FDMI_REGISTER_NEEDED,
					    &ha->fdmi_flags);

				ha->flags.management_server_logged_in = 0;
				ha->current_speed = 0; /* reset value */

				/* no wait 10/19/2000 */
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LOOP_DOWN, NULL);
				}
				break;

			case MBA_LIP_RESET:	/* LIP reset occurred */

				printk(KERN_INFO
					"scsi(%ld): LIP reset occurred.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: "
					"asynchronous MBA_LIP_RESET.\n",
					ha->host_no, __func__);)

				set_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
				set_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
					    LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				ha->operating_mode = LOOP;
				ha->flags.management_server_logged_in = 0;

				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LIP_RESET, NULL);
				}

				ha->total_lip_cnt++;
				break;

			case MBA_LINK_MODE_UP:	/* Link mode up. */

				DEBUG3(printk("scsi(%ld): Link node is up.\n",
						ha->host_no);)

				DEBUG2(printk(KERN_INFO "%s(%ld): asynchronous "
						"MBA_LINK_MODE_UP.\n",
						__func__,
						ha->host_no);)

				/*
				 * Until there's a transition from loop down to
				 * loop up, treat this as loop down only.
				 */
				if (!(test_bit(ABORT_ISP_ACTIVE,
							&ha->dpc_flags))) {
					set_bit(COMMAND_WAIT_NEEDED,
							&ha->dpc_flags);
					set_bit(RESET_MARKER_NEEDED,
							&ha->dpc_flags);
				}
				set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					if (!atomic_read(&ha->loop_down_timer))
						atomic_set(&ha->loop_down_timer,
						    LOOP_DOWN_TIME);

					atomic_set(&ha->loop_state, LOOP_DOWN);
					qla2x00_mark_all_devices_lost(ha);
				}
				break;

			case MBA_UPDATE_CONFIG:      /* Update Configuration. */

				printk(KERN_INFO
					"scsi(%ld): Configuration change "
					"detected: value %d.\n",
					ha->host_no,
					mailbox[1]);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: asynchronous "
						"MBA_UPDATE_CONFIG.\n",
						ha->host_no, __func__);)

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					/* dg - 03/30 */
					atomic_set(&ha->loop_state, LOOP_DOWN);  
					if (!atomic_read(&ha->loop_down_timer))
						atomic_set(&ha->loop_down_timer,
								LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
				break;


			case MBA_PORT_UPDATE:	/* Port database update */

			     /* If PORT UPDATE is global(recieved 
			      * LIP_OCCURED/LIP_RESET event etc earlier 
			      * indicating loop is down) then process
			      * it. 
			      */
				printk(KERN_INFO "scsi(%ld): Port database "
						"changed.\n",
						ha->host_no);
				
			     if (atomic_read(&ha->loop_state) == LOOP_DOWN ||
			         atomic_read(&ha->loop_state) == LOOP_DEAD){

				DEBUG2(printk(KERN_INFO "scsi%ld %s: asynchronous "
						"MBA_PORT_UPDATE.\n",
						ha->host_no, __func__);)

				if (ql2xfdmienable)
					set_bit(FDMI_REGISTER_NEEDED,
					    &ha->fdmi_flags);

				/* dg - 06/19/01
				 *
				 * Mark all devices as missing so we will
				 * login again.
				 */
				ha->flags.rscn_queue_overflow = 1;

				atomic_set(&ha->loop_down_timer, 0);
				atomic_set(&ha->loop_state, LOOP_UP);
				qla2x00_mark_all_devices_lost(ha);
				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);

				/* 9/23
				 *
				 * ha->flags.loop_resync_needed = TRUE;
				 */
				atomic_set(&ha->loop_state, LOOP_UPDATE);
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_PORT_UPDATE, NULL);
				}

			     } else {
				     /* If Port update is not global (mbx1)
				      * and device logged out (mbx2)
				      * schedule relogin. */
				     DEBUG2(printk(KERN_INFO "scsi(%ld) Port"
					 " update mbx1-0x%x mb2-0x%x"
					 " mb3-0x%x\\n", ha->host_no,
					   mailbox[1], mailbox[2],mailbox[3]);)
				     if (mailbox[1] != 0xFFFF && 
					  mailbox[2] != 6 ){
					 fc_port_t *fcport;    
				         fcport = 
					   qla2x00_find_changed_local_port(
						  ha, mailbox[1]);
					 if (fcport && fcport->login_retry == 0)
						qla2x00_mark_device_lost(
							ha, fcport, 1);
					 else
						DEBUG2(printk(KERN_INFO "Port update:"
						    " Cant find the port or"
						    " trying to log back in\n");)
				     } else {
					atomic_set(&ha->loop_down_timer, 0);
						DEBUG2(printk(KERN_INFO "scsi(%ld)" 
						" Port Update ignored\n",	
			     			ha->host_no);)
				     }	
			     }
				break;

			case MBA_SCR_UPDATE:	/* State Change Registration */

				DEBUG2(printk(KERN_INFO "scsi(%ld): RSCN database changed "
						"-0x%x,0x%x.\n",
						ha->host_no,
						mailbox[1],
						mailbox[2]);)
				printk(KERN_INFO "scsi(%ld): RSCN database changed "
						"-0x%x,0x%x.\n",
						ha->host_no,
						mailbox[1],
						mailbox[2]);

				rscn_queue_index = ha->rscn_in_ptr + 1;
				if (rscn_queue_index == MAX_RSCN_COUNT)
					rscn_queue_index = 0;
				if (rscn_queue_index != ha->rscn_out_ptr) {
					ha->rscn_queue[ha->rscn_in_ptr].
						format =
						   (uint8_t)(mailbox[1] >> 8);
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.domain =
						   (uint8_t)mailbox[1];
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.area =
						   (uint8_t)(mailbox[2] >> 8);
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.al_pa =
						   (uint8_t)mailbox[2];
					ha->rscn_in_ptr =
						(uint8_t)rscn_queue_index;
				} else {
					ha->flags.rscn_queue_overflow = 1;
				}

				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(RSCN_UPDATE, &ha->dpc_flags);
				atomic_set(&ha->loop_down_timer, 0);
				ha->flags.management_server_logged_in = 0;

				atomic_set(&ha->loop_state, LOOP_UPDATE);
				if (ha->ioctl->flags &
				    IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
					    MBA_RSCN_UPDATE, &mailbox[0]);
				}
				break;

			case MBA_LOOP_INIT_ERR:
				DEBUG2(printk(KERN_INFO"scsi(%ld) Loop Init"
						" Errors mbx1=0x%x\n",
						ha->host_no, mailbox[1]);)
				break;

			case MBA_FC_SP_UPDATE:
				DEBUG2(printk(KERN_INFO "scsi(%ld):asynchronous"
						"MBA FC-SP security update: "
						"security_status-0x%x.\n",
						 ha->host_no,mailbox[1]);)
				break;

			case MBA_CTIO_COMPLETION:
				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"MBA_CTIO_COMPLETION.\n",
						__func__);)

				break;

			case MBA_RECEIVE_ERR:
				DEBUG2(printk(KERN_INFO"scsi(%ld) Recieve"
				    " Errors mbx1=0x%x mbx2=0x%x"
				    " mbx3=0x%x mbx4=0x%x"
				    " mbx5=0x%x mbx6=0x%x "
				    " mbx7=0x%x\n",
				    ha->host_no, mailbox[1], mailbox[2],
				    mailbox[3], mailbox[4], mailbox[5],
				    mailbox[6], mailbox[7]);)
				break;

			default:

				if (temp1 >= MBA_ASYNC_EVENT)
					break;
				/* mailbox completion */
				*got_mbx = TRUE;
				memcpy((void *)ha->mailbox_out,
					mailbox,
					sizeof(ha->mailbox_out));
				ha->flags.mbox_int = TRUE;
				if (ha->mcp) {
					DEBUG3(printk("%s(): got mailbox "
							"completion. cmd=%x.\n",
							__func__,
							ha->mcp->mb[0]);)
				} else {
					DEBUG2_3(printk(KERN_INFO "%s(): mbx pointer "
							"ERROR.\n",
							__func__);)
				}
				DEBUG3(printk("%s(): Returning mailbox data\n",
						__func__);)
				break;
		}
	} else {
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
		PCI_POSTING(&reg->hccr);
	}
response_queue_int:
	if (ha->flags.online) {
		/* Check for unprocessed commands
		 * in response queue.
		 */
		if (ha->response_ring_ptr->signature !=
				RESPONSE_PROCESSED){
			qla24xx_process_response_queue(ha);
		}
	}

isr_end:
	LEAVE(__func__);
}

void
qla24xx_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags = 0;
	unsigned long mbx_flags = 0;
	scsi_qla_host_t *ha;
	uint32_t    data;
	uint8_t     got_mbx = 0;
        struct device_reg_24xx *reg;
	/* don't loop forever,interrupt are OFF */
	unsigned long	intr_loop = 50; 

	ha = (scsi_qla_host_t *) dev_id;
	if (!ha) {
		printk(KERN_INFO
			"qla2x00_intr_handler: NULL host ptr\n");

		return;
	}
	DEBUG3(printk("%s(%ld) entered\n",__func__, ha->host_no);)
	reg = (struct device_reg_24xx *)ha->iobase;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	/* Check for pending interrupts. */
	while (((data = RD_REG_DWORD(&reg->host_status)) & HSRX_RISC_INT)
			&& intr_loop-- )
	{
		ha->total_isr_cnt++;
		qla24xx_isr(ha, data, &got_mbx);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	qla2x00_next(ha);
	ha->last_irq_cpu = smp_processor_id();

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
		got_mbx && ha->flags.mbox_int) {
		/* There was a mailbox completion */
		DEBUG3(printk("qla2x00_intr_handler: going to "
				"get mbx reg lock.\n");)

		QLA_MBX_REG_LOCK(ha);
		MBOX_TRACE(ha,BIT_5);
		got_mbx = 0;

		if (ha->mcp == NULL) {
			DEBUG3(printk("qla2x00_intr_handler: error mbx "
					"pointer.\n");)
		} else {
			DEBUG3(printk("qla2x00_intr_handler: going to set mbx "
					"intr flags. cmd=%x.\n",
					ha->mcp->mb[0]);)
		}
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		DEBUG3(printk("qla2x00_intr_handler(%ld): going to wake up "
				"mbx function for completion.\n",
				ha->host_no);)
		MBOX_TRACE(ha,BIT_6);
		up(&ha->mbx_intr_sem);

		DEBUG3(printk("qla2x00_intr_handler: going to unlock mbx "
				"reg.\n");)
		QLA_MBX_REG_UNLOCK(ha);
	}

	if (!list_empty(&ha->done_queue))
		qla2x00_done(ha);

	/* Wakeup the DPC routine */
	if ((!ha->flags.mbox_busy &&
		(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		 test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) ||
		 test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ) ) && 
		ha->dpc_wait && !ha->dpc_active) {  /* v2.19.4 */

		up(ha->dpc_wait);
	}

#if defined(SH_HAS_CAN_QUEUE_MASK) 
 	update_host_queue_mask(ha);
#endif

	DEBUG3(printk("%s leaving\n",__func__);)
}
#endif

/*
 * qla2x00_retry_command
 *    Retries the specified command 
 *
 * Input:
 *    ha = actual ha w
 *    cmd = Scsi Command to wait on.
 *
 * Return:
 *	None
 *
 * Locks:
 *	ha->list_lock must be aquired	
 */
STATIC void
qla2x00_retry_command(scsi_qla_host_t *ha, srb_t *sp)
{
	/* restore original timeout */
	qla2x00_extend_timeout(sp->cmd, 
		(CMD_TIMEOUT(sp->cmd)/HZ) - QLA_CMD_TIMER_DELTA);
	qla2x00_free_request_resources(ha,sp);
	__add_to_pending_queue( ha, sp);
}

/**************************************************************************
* qla2x00_do_dpc
*   This kernel thread is a task that is schedule by the interrupt handler
*   to perform the background processing for interrupts.
*
* Notes:
* This task always run in the context of a kernel thread.  It
* is kick-off by the driver's detect code and starts up
* up one per adapter. It immediately goes to sleep and waits for
* some fibre event.  When either the interrupt handler or
* the timer routine detects a event it will one of the task
* bits then wake us up.
**************************************************************************/
void
qla2x00_do_dpc(void *p)
{
	DECLARE_MUTEX_LOCKED(sem);
	fc_port_t	*fcport = NULL;
	os_lun_t        *q;
	os_tgt_t        *tq;
	scsi_qla_host_t *ha = (scsi_qla_host_t *) p;
	srb_t           *sp;
	uint8_t		status;
	uint32_t        t;
	unsigned long	flags = 0;
	struct list_head *list, *templist;
	int	dead_cnt, online_cnt;
	int	retry_cmds;
	uint16_t next_loopid;
	int     rval;

	ENTER(__func__);

#if defined(MODULE)
	siginitsetinv(&current->blocked, SHUTDOWN_SIGS);
#else
	siginitsetinv(&current->blocked, 0);
#endif

	lock_kernel();

	/* Flush resources */
	daemonize();

	/*
	 * FIXME(dg) this is still a child process of the one that did
	 * the insmod.  This needs to be attached to task[0] instead.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
	/* As mentioned in kernel/sched.c(RA).....
	 * Reparent the calling kernel thread to the init task.
	 * 
	 * If a kernel thread is launched as a result of a system call,
	 * or if it ever exists,it should generally reparent itself to init
	 * so that it is correctly cleaned up on exit.
	 *
	 * The various task state such as scheduling policy and priority
	 * may have been inherited from a user process, so we reset them
	 * to sane values here.
	 *
	 * NOTE that reparent_to_init() gives the caller full capabilities.
	 *
	 */
	reparent_to_init();
#endif

	/*
	 * Set the name of this process.
	 */
	sprintf(current->comm, "%s_dpc%ld", DRIVER_NAME, ha->host_no);
	ha->dpc_wait = &sem;

	ha->dpc_handler = current;

	unlock_kernel();

	/*
	 * Wake up the thread that created us.
	 */
	DEBUG(printk("%s(): Wake up parent %d\n",
			__func__,
			ha->dpc_notify->count.counter);)

	up(ha->dpc_notify);

	while (1) {
		/*
		 * If we get a signal, it means we are supposed to go
		 * away and die.  This typically happens if the user is
		 * trying to unload a module.
		 */
		DEBUG3(printk("qla2x00: DPC handler sleeping\n");)

		down_interruptible(&sem);

		if (signal_pending(current))
			break;   /* get out */


		DEBUG3(printk("qla2x00: DPC handler waking up\n");)

		/* Initialization not yet finished. Don't do anything yet. */
		if (!ha->init_done || ha->dpc_active)
			continue;

		DEBUG3(printk("scsi(%ld): DPC handler\n", ha->host_no);)

		/* spin_lock_irqsave(&io_request_lock, ha->cpu_flags);*/
		ha->dpc_active = 1;

		if (!list_empty(&ha->done_queue))
			qla2x00_done(ha);

		/* Determine what action is necessary */

		/* Process commands in retry queue */
		if (test_and_clear_bit(PORT_RESTART_NEEDED, &ha->dpc_flags)) {
			DEBUG3(printk(KERN_INFO "%s(%ld): (1) DPC checking retry_q. "
					"total=%d\n",
					__func__,
					ha->host_no,
					ha->retry_q_cnt);)

			spin_lock_irqsave(&ha->list_lock, flags);
			dead_cnt = online_cnt = 0;
			list_for_each_safe(list, templist, &ha->retry_queue) {
				sp = list_entry(list, srb_t, list);
				q = sp->lun_queue;
				DEBUG3(printk("qla2x00_retry_q: pid=%ld "
						"sp=%p, spflags=0x%x, "
						"q_flag= 0x%lx\n",
						sp->cmd->serial_number,
						sp,
						sp->flags,
						q->q_flag);)

				if (q == NULL)
					continue;
				fcport = q->fclun->fcport;

				if (atomic_read(&fcport->state) ==
				    FC_DEVICE_DEAD ||
				    atomic_read(&fcport->ha->loop_state)
					 == LOOP_DEAD) {

					__del_from_retry_queue(ha, sp);
					CMD_RESULT(sp->cmd) = 
						DID_NO_CONNECT << 16;
					if (atomic_read(&ha->loop_state) ==
					    LOOP_DEAD) 
						sp->err_id = SRB_ERR_LOOP;
					else
						sp->err_id = SRB_ERR_PORT;
					CMD_HANDLE(sp->cmd) = 
						(unsigned char *) NULL;
					__add_to_done_queue(ha, sp);
					dead_cnt++;
				} else if (atomic_read(&fcport->state) != 
						FC_DEVICE_LOST) {

					__del_from_retry_queue(ha, sp);
					CMD_RESULT(sp->cmd) = 
						DID_BUS_BUSY << 16;
					CMD_HANDLE(sp->cmd) = 
						(unsigned char *) NULL;
					__add_to_done_queue(ha, sp);
					online_cnt++;
				} else if ( qla2x00_cfg_is_lbenable(
						q->fclun) ){
					__del_from_retry_queue(ha, sp);
					sp->cmd->result = DID_NO_CONNECT << 16;
					sp->err_id = SRB_ERR_RETRY;
					sp->cmd->host_scribble =
					    (unsigned char *) NULL;
					__add_to_done_queue(ha, sp);
 				}
			} /* list_for_each_safe() */
			spin_unlock_irqrestore(&ha->list_lock, flags);

			DEBUG2(printk(KERN_INFO "%s(%ld): (1) done processing retry queue - "
					"dead=%d, online=%d\n ",
					__func__,
					ha->host_no,
					dead_cnt,
					online_cnt);)
		}
		/* Process commands in scsi retry queue */
		if (test_and_clear_bit(SCSI_RESTART_NEEDED, &ha->dpc_flags)) {
			/*
			 * Any requests we want to delay for some period is put
			 * in the scsi retry queue with a delay added. The
			 * timer will schedule a "scsi_restart_needed" every 
			 * second as long as there are requests in the scsi
			 * queue. 
			 */
			DEBUG2(printk(KERN_INFO "%s(%ld): (2) DPC checking scsi "
					"retry_q.total=%d\n",
					__func__,
					ha->host_no,
					ha->scsi_retry_q_cnt);)

			online_cnt = 0;
			retry_cmds = 0;
			spin_lock_irqsave(&ha->list_lock, flags);
			list_for_each_safe(list,
						templist,
						&ha->scsi_retry_queue) {

				sp = list_entry(list, srb_t, list);
				q = sp->lun_queue;
				tq = sp->tgt_queue;

				DEBUG3(printk("qla2x00_scsi_retry_q: pid=%ld "
						"sp=%p, spflags=0x%x, "
						"q_flag= 0x%lx,q_state=%d\n",
						sp->cmd->serial_number,
						sp,
						sp->flags,
						q->q_flag,
						q->q_state);)

				/* Was this lun suspended */
				if ( (q->q_state != LUN_STATE_WAIT) &&
					 atomic_read(&tq->q_timer) == 0 ) {
				     DEBUG3(printk(KERN_INFO "qla2x00_scsi_retry_q: pid=%ld "
					"sp=%p, spflags=0x%x, "
					"q_flag= 0x%lx,q_state=%d, tgt_flags=0x%lx\n",
					sp->cmd->serial_number,
					sp,
					sp->flags,
					q->q_flag,
					q->q_state, tq->q_flags);)
					online_cnt++;
					__del_from_scsi_retry_queue(ha, sp);
					if( test_bit(TGT_RETRY_CMDS, 
						&tq->q_flags) ) {
						qla2x00_retry_command(ha,sp);
						retry_cmds++;
					} else 
						__add_to_retry_queue(ha,sp);
				}
			}
			spin_unlock_irqrestore(&ha->list_lock, flags);

			/* Clear all Target Unsuspended bits */
			for (t = 0; t < ha->max_targets; t++) {
				if ((tq = ha->otgt[t]) == NULL)
					continue;

				if( test_bit(TGT_RETRY_CMDS, &tq->q_flags) )
					clear_bit(TGT_RETRY_CMDS, &tq->q_flags);
			}

			if( retry_cmds )
				qla2x00_next(ha);

			DEBUG3(if (online_cnt > 0))
			DEBUG3(printk(KERN_INFO "scsi%ld: dpc() (2) found scsi reqs "
					"to retry_q= %d, tgt retry cmds=%d\n",
					ha->host_no, online_cnt, retry_cmds););
		}

		/* Process any pending mailbox commands */
		if (!ha->flags.mbox_busy) {
			if (test_and_clear_bit(ISP_ABORT_NEEDED,
						&ha->dpc_flags)) {

				DEBUG(printk("scsi%ld: dpc: sched "
						"qla2x00_abort_isp ha = %p\n",
						ha->host_no, ha);)
				if (!(test_and_set_bit(ABORT_ISP_ACTIVE,
							&ha->dpc_flags))) {

					if (qla2x00_abort_isp(ha)) {
						/* failed. retry later */
						set_bit(ISP_ABORT_NEEDED,
								&ha->dpc_flags);
					}
					clear_bit(ABORT_ISP_ACTIVE,
							&ha->dpc_flags);
				}
				DEBUG(printk("scsi%ld: dpc: qla2x00_abort_isp "
						"end\n",
						ha->host_no);)
			}

			if (test_and_clear_bit(LOOP_RESET_NEEDED,
						&ha->dpc_flags)) {

				DEBUG(printk("dpc: loop_reset_needed(%ld) "
						"calling loop_reset.\n",
						ha->host_no);)

				qla2x00_loop_reset(ha);
			}
			if (test_and_clear_bit(DEVICE_ABORT_NEEDED,
						&ha->dpc_flags)) {

				DEBUG(printk("dpc: device_abort_needed(%ld) "
						"calling device_abort.\n",
						ha->host_no);)

				t = ha->reset_tgt_id;
				if (ha->otgt[t] && ha->otgt[t]->vis_port)
					qla2x00_abort_device(ha,
						ha->otgt[t]->vis_port->loop_id,
						ha->reset_lun);
			}

			if (test_and_clear_bit(RESET_MARKER_NEEDED,
						&ha->dpc_flags)) {

				if (!(test_and_set_bit(RESET_ACTIVE,
							&ha->dpc_flags))) {

					DEBUG(printk("dpc(%ld): "
						"qla2x00_reset_marker \n",
						ha->host_no);)

					qla2x00_rst_aen(ha);
					clear_bit(RESET_ACTIVE, &ha->dpc_flags);
				}
			}

			/* v2.19.8 Retry each device up to login retry count */
			if ((test_and_clear_bit(RELOGIN_NEEDED,
			    &ha->dpc_flags)) &&
			    !test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) &&
			    atomic_read(&ha->loop_state) != LOOP_DOWN) {

				DEBUG(printk("dpc%ld: qla2x00_port_login\n",
						ha->host_no);)

				next_loopid = 0;
				list_for_each_entry(fcport, &ha->fcports, list) {
					if(fcport->port_type != FCT_TARGET)
						continue;

					/*
					 * If the port is not ONLINE then try
					 * to login to it if we haven't run
					 * out of retries.
					 */
					if (atomic_read(&fcport->state) != FC_ONLINE &&
						fcport->login_retry) {
						fcport->login_retry--;
						if (fcport->flags & FC_FABRIC_DEVICE) {
							if (fcport->flags & FC_TAPE_DEVICE)
							qla2x00_fabric_logout(
							  ha,
							  fcport->loop_id,
							  fcport->d_id.b.domain,
							  fcport->d_id.b.area, 
							  fcport->d_id.b.al_pa);
							if(fcport->loop_id 
								== FC_NO_LOOP_ID) {
								fcport->loop_id = ha->min_external_loopid;
								rval = qla2x00_find_new_loop_id(ha, fcport);
								if (rval != QL_STATUS_SUCCESS)
								     continue;
								}
							        status = qla2x00_fabric_login(ha, fcport, &next_loopid);
						} else 	
							status = qla2x00_local_device_login(ha, fcport);

						if (status == QL_STATUS_SUCCESS) {
							fcport->old_loop_id = fcport->loop_id;

							DEBUG(printk("dpc%ld port login OK: logged in ID 0x%x\n",
									ha->host_no, fcport->loop_id);)
							printk(KERN_INFO "dpc%ld port login OK: logged in ID 0x%x\n",
									ha->host_no, fcport->loop_id);
							
							fcport->login_retry = 0;
							fcport->port_login_retry_count = ha->port_down_retry_count *
												PORT_RETRY_TIME;
							atomic_set(&fcport->state, FC_ONLINE);
							atomic_set(&fcport->port_down_timer,
									ha->port_down_retry_count * PORT_RETRY_TIME);

						} else if (status == 1) {
							set_bit(RELOGIN_NEEDED, &ha->dpc_flags);
							/* retry the login again */
							DEBUG(printk("dpc: Retrying %d login again loop_id 0x%x\n",
									fcport->login_retry, fcport->loop_id);)
						} else {
							fcport->login_retry = 0;
						}
					}
					if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
						break;
				}
				DEBUG(printk("dpc%ld: qla2x00_port_login - end\n",
						ha->host_no);)
			}

			/* v2.19.5 */
			if ((test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags)) &&
			    atomic_read(&ha->loop_state) != LOOP_DOWN) { /* v2.19.5 */

				clear_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags);
				DEBUG(printk("dpc(%ld): qla2x00_login_retry\n",
						ha->host_no);)
					
				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);

				DEBUG(printk("dpc: qla2x00_login_retry end.\n");)
			}

			/* v2.19.5b5 */
			if (test_and_clear_bit(LOOP_RESYNC_NEEDED,
						&ha->dpc_flags)) {

				DEBUG(printk("dpc(%ld): qla2x00_LOOP_RESYNC\n",
						ha->host_no);)

				if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE,
							&ha->dpc_flags))) {

					qla2x00_loop_resync(ha);

					clear_bit(LOOP_RESYNC_ACTIVE,
							&ha->dpc_flags);

				}
				DEBUG(printk("dpc(%ld): qla2x00_LOOP_RESYNC "
						"done\n",
						ha->host_no);)
			}

			if (test_and_clear_bit(PORT_SCAN_NEEDED,
			    &ha->dpc_flags)) {

				DEBUG(printk("dpc(%ld): qla2x00: RESCAN ...\n",
				    ha->host_no);)
				printk(KERN_INFO
				    "dpc(%ld): qla2x00: RESCAN .\n",
				    ha->host_no); 

				if ( !(test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags)) &&
			    		atomic_read(&ha->loop_state) != LOOP_DOWN) { 
					/* suspend new I/O for await */
					atomic_set(&ha->loop_state, LOOP_UPDATE);
					qla2x00_probe_for_all_luns(ha); 

					/* If we found all devices then go ready */
					atomic_set(&ha->loop_state, LOOP_READY);

					if (!ha->flags.failover_enabled)
						qla2x00_config_os(ha);
					else	
						qla2x00_cfg_remap(qla2x00_hostlist);
				}

				DEBUG(printk("dpc(%ld): qla2x00: RESCAN ...done\n",
						ha->host_no);)
				printk(KERN_INFO"dpc(%ld): qla2x00: RESCAN" 
					    "... done.\n", ha->host_no); 
			}
			if (ha->flags.failover_enabled) {
				/*
				 * If we are not processing a ioctl or one of
				 * the ports are still MISSING or need a resync
				 * then process the failover event.
				*/  
				if (!test_bit(CFG_ACTIVE, &ha->cfg_flags)) {

					if (qla2x00_check_for_devices_online(ha)) {
						if (test_and_clear_bit(FAILOVER_EVENT,
								&ha->dpc_flags)) {

							DEBUG2(printk("dpc(%ld): "
								"qla2x00_cfg_event_notify\n",
								ha->host_no);)

							if (ha->flags.online) {
								qla2x00_cfg_event_notify(ha, ha->failover_type);
							}

							DEBUG2(printk("dpc(%ld): "
								"qla2x00_cfg_event_notify - done\n",
								ha->host_no);)
						}
					}

					if (test_and_clear_bit(FAILOVER_NEEDED,
								&ha->dpc_flags)) {

						/*
						 * Get any requests from failover queue
						 */
						DEBUG2(printk("dpc: qla2x00_process "
								"failover\n");)

						qla2x00_process_failover(ha);

						DEBUG2(printk("dpc: qla2x00_process "
								"failover - done\n");)
					}
				}
			}

			if (test_bit(RESTART_QUEUES_NEEDED, &ha->dpc_flags)) {
				DEBUG(printk("dpc: qla2x00_restart_queues\n");)

				qla2x00_restart_queues(ha,FALSE);

				DEBUG(printk("dpc: qla2x00_restart_queues "
						"- done\n");)
			}

			if (test_bit(ABORT_QUEUES_NEEDED, &ha->dpc_flags)) {
				DEBUG(printk("dpc:(%ld) "
					"qla2x00_abort_queues\n", ha->host_no);)
					
				qla2x00_abort_queues(ha, FALSE);
			}

			if (test_and_clear_bit(IOCTL_ERROR_RECOVERY,
			    &ha->dpc_flags)) {
				qla2x00_ioctl_error_recovery(ha);	
			}

			if (!ha->interrupts_on)
				qla2x00_enable_intrs(ha);

		}

		if (!list_empty(&ha->done_queue))
			qla2x00_done(ha);

		/* spin_unlock_irqrestore(&io_request_lock, ha->cpu_flags);*/

#if !defined(ISP2100) && !defined(ISP2200)
		if (test_and_clear_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags)) {
			DEBUG9(printk("scsi(%ld): blink led start.\n",
			    ha->host_no));

			/* beacon function */
			if ((ha->device_id & 0xff00) == 0x2300 ||
			    (ha->device_id & 0xff00) == 0x6300)
				qla23xx_blink_led(ha);
			else if ((ha->device_id & 0xff00) == 0x2400 ||
			    (ha->device_id & 0xff00) == 0x2500)
				qla24xx_blink_led(ha);

			DEBUG9(printk("scsi(%ld): blink led end.\n",
			    ha->host_no));
		}
#endif

		ha->dpc_active = 0;

		/* The spinlock is really needed up to this point. (DB) */
	} /* End of while(1) */

	DEBUG(printk("dpc: DPC handler exiting\n");)

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	ha->dpc_wait = NULL;
	ha->dpc_handler = NULL;
	ha->dpc_active = 0;

	/*
	 * If anyone is waiting for us to exit (i.e. someone trying to unload a
	 * driver), then wake up that process to let them know we are on the
	 * way out the door.  This may be overkill - I *think* that we could
	 * probably just unload the driver and send the signal, and when the
	 * error handling thread wakes up that it would just exit without
	 * needing to touch any memory associated with the driver itself.
	 */
	if (ha->dpc_notify != NULL)
		up(ha->dpc_notify);

	LEAVE(__func__);
}

/**************************************************************************
* qla2x00_device_queue_depth
*   Determines the queue depth for a given device.  There are two ways
*   a queue depth can be obtained for a tagged queueing device.  One
*   way is the default queue depth which is determined by whether
*   If it is defined, then it is used
*   as the default queue depth.  Otherwise, we use either 4 or 8 as the
*   default queue depth (dependent on the number of hardware SCBs).
**************************************************************************/
void
qla2x00_device_queue_depth(scsi_qla_host_t *p, Scsi_Device *device)
{
	int default_depth = 32;

	device->queue_depth = default_depth;
	if (device->tagged_supported) {
		device->tagged_queue = 1;
		device->current_tag = 0;

		if (!(ql2xmaxqdepth == 0 || ql2xmaxqdepth > 255))
			device->queue_depth = ql2xmaxqdepth;

		printk(KERN_INFO
			"scsi(%ld:%d:%d:%d): Enabled tagged queuing, "
			"queue depth %d.\n",
			p->host_no,
			device->channel,
			device->id,
			device->lun, 
			device->queue_depth);
	}

}

/**************************************************************************
*   qla2x00_select_queue_depth
*
* Description:
*   Sets the queue depth for each SCSI device hanging off the input
*   host adapter.  We use a queue depth of 2 for devices that do not
*   support tagged queueing.
**************************************************************************/
static void
qla2x00_select_queue_depth(struct Scsi_Host *host, Scsi_Device *scsi_devs)
{
	Scsi_Device *device;
	scsi_qla_host_t  *p = (scsi_qla_host_t *) host->hostdata;

	ENTER(__func__);

	for (device = scsi_devs; device != NULL; device = device->next) {
		if (device->host == host)
			qla2x00_device_queue_depth(p, device);
	}

	LEAVE(__func__);
}

/**************************************************************************
* ** Driver Support Routines **
*
* qla2x00_enable_intrs
* qla2x00_disable_intrs
**************************************************************************/
static inline void 
qla2x00_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t *reg;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	reg = ha->iobase;
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
#if defined(ISP2300)	
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		struct device_reg_24xx *reg24;
		reg24 = (struct device_reg_24xx *)ha->iobase;
		WRT_REG_DWORD(&reg24->ictrl, ICRX_EN_RISC_INT);
		RD_REG_DWORD(&reg24->ictrl);
	} else 
#endif
	{
		WRT_REG_WORD(&reg->ictrl, (ISP_EN_INT+ ISP_EN_RISC));
		PCI_POSTING(&reg->ictrl);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void 
qla2x00_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t *reg;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	reg = ha->iobase;
	ha->interrupts_on = 0;
#if defined(ISP2300)	
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		struct device_reg_24xx *reg24;
		reg24 = (struct device_reg_24xx *)ha->iobase;
		WRT_REG_DWORD(&reg24->ictrl, 0);
		RD_REG_DWORD(&reg24->ictrl);

	} else 
#endif
	{
		/* disable risc and host interrupts */
		WRT_REG_WORD(&reg->ictrl, 0);
		PCI_POSTING(&reg->ictrl);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

STATIC inline void 
qla2x00_free_request_resources(scsi_qla_host_t *dest_ha, srb_t *sp) 
{
	if (sp->flags & SRB_DMA_VALID) {
		sp->flags &= ~SRB_DMA_VALID;

#ifndef __VMWARE__
		/* Release memory used for this I/O */
		if (sp->cmd->use_sg) {
			pci_unmap_sg(dest_ha->pdev,
					sp->cmd->request_buffer,
					sp->cmd->use_sg,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
		} else if (sp->cmd->request_bufflen) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
			pci_unmap_page(dest_ha->pdev,
					sp->saved_dma_handle,
					sp->cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
#else
			pci_unmap_single(dest_ha->pdev,
					sp->saved_dma_handle,
					sp->cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
#endif
		}
#endif
	}
}

STATIC inline void 
qla2x00_delete_from_done_queue(scsi_qla_host_t *dest_ha, srb_t *sp) 
{
	/* remove command from done list */
	list_del_init(&sp->list);
	dest_ha->done_q_cnt--;
	sp->state = SRB_NO_QUEUE_STATE;

	if (sp->flags & SRB_DMA_VALID) {
		sp->flags &= ~SRB_DMA_VALID;

		/* Release memory used for this I/O */
		if (sp->cmd->use_sg) {
			pci_unmap_sg(dest_ha->pdev,
					sp->cmd->request_buffer,
					sp->cmd->use_sg,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
		} else if (sp->cmd->request_bufflen) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
			pci_unmap_page(dest_ha->pdev,
					sp->saved_dma_handle,
					sp->cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
#else
			pci_unmap_single(dest_ha->pdev,
					sp->saved_dma_handle,
					sp->cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						sp->cmd->sc_data_direction));
#endif
		}
	}
}

/**************************************************************************
* qla2x00_done
*      Process completed commands.
*
* Input:
*      old_ha           = adapter block pointer.
*
* Returns:
* int     
**************************************************************************/
STATIC int
qla2x00_done(scsi_qla_host_t *old_ha)
{
	os_lun_t	*lq;
	Scsi_Cmnd	*cmd;
	unsigned long	flags = 0;
	scsi_qla_host_t	*ha;
	scsi_qla_host_t	*vis_ha;
	int	cnt;
	int	send_marker_once = 0;
	struct list_head	*spl, *sptemp;
	srb_t           *sp;
	struct	list_head local_sp_list;

	ENTER(__func__);

	cnt = 0;

	INIT_LIST_HEAD(&local_sp_list);

	/*
	 * Get into local queue such that we do not wind up calling done queue
	 * takslet for the same IOs from DPC or any other place.
	 */
	spin_lock_irqsave(&old_ha->list_lock,flags);
 	qla_list_splice_init(&old_ha->done_queue, &local_sp_list);
	spin_unlock_irqrestore(&old_ha->list_lock, flags);

	list_for_each_safe(spl, sptemp, &local_sp_list) {
		sp = list_entry(local_sp_list.next, srb_t, list);
		old_ha->done_q_cnt--;
        	sp->state = SRB_NO_QUEUE_STATE;
		list_del_init(&sp->list);

		cnt++;

		cmd = sp->cmd;
		if (cmd == NULL) {
#if  0
			panic("qla2x00_done: SP %p already freed - %s %d.\n",
			    sp, __FILE__,__LINE__);
#else
		 	continue;
#endif
		}

		vis_ha = (scsi_qla_host_t *)cmd->host->hostdata;
		lq = sp->lun_queue;
#if 0
		ha = lq->fclun->fcport->ha;
#else
		ha = sp->ha;
#endif

		if (sp->flags & SRB_DMA_VALID) {
			sp->flags &= ~SRB_DMA_VALID;

			/* 4.10   64 and 32 bit */
			/* Release memory used for this I/O */
			if (cmd->use_sg) {
				pci_unmap_sg(ha->pdev,
				    cmd->request_buffer,
				    cmd->use_sg,
				    scsi_to_pci_dma_dir(
					    cmd->sc_data_direction));
			} else if (cmd->request_bufflen) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
				pci_unmap_page(ha->pdev,
					sp->saved_dma_handle,
					cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						cmd->sc_data_direction));
#else
				pci_unmap_single(ha->pdev,
				    sp->saved_dma_handle,
				    cmd->request_bufflen,
				    scsi_to_pci_dma_dir(
					    cmd->sc_data_direction));
#endif
			}
		}
		if (!(sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) &&
			ha->flags.failover_enabled) {
			/*
			 * This routine checks for DID_NO_CONNECT to decide
			 * whether to failover to another path or not. We only
			 * failover on selection timeout(DID_NO_CONNECT) status.
			 */
			if ( !(lq->fclun->fcport->flags & FC_FAILOVER_DISABLE) &&
			     !(lq->fclun->flags & FC_VISIBLE_LUN) &&
				qla2x00_fo_check(ha,sp)) {
				if ((sp->state != SRB_FAILOVER_STATE)) {
					/*
					 * Retry the command on this path
					 * several times before selecting a new
					 * path.
					 */
					add_to_pending_queue_head(vis_ha, sp);
					qla2x00_next(vis_ha);
				}
				else {
					/* we failover this path */
					qla2x00_extend_timeout(sp->cmd,
							EXTEND_CMD_TIMEOUT);
				}
				continue;
			}
			
		}

		switch ((CMD_RESULT(cmd)>>16)) {

			case DID_OK:
			case DID_ERROR:
				break; 

			case DID_RESET:
				/*
				 * set marker needed, so we don't have to
				 * send multiple markers
				 */

				/* ra 01/10/02 */
				if (!send_marker_once) {
					ha->marker_needed = 1;
					send_marker_once++;
				}

				/*
				 * WORKAROUND
				 *
				 * A backdoor device-reset requires different
				 * error handling.  This code differentiates
				 * between normal error handling and the
				 * backdoor method.
				 *
				 */
				if (ha->host->eh_active != EH_ACTIVE)
					CMD_RESULT(sp->cmd) =
						DID_BUS_BUSY << 16;
				break;


			case DID_ABORT:
				sp->flags &= ~SRB_ABORT_PENDING;
				sp->flags |= SRB_ABORTED;

				if (sp->flags & SRB_TIMEOUT)
					CMD_RESULT(cmd)= DID_TIME_OUT << 16;

				break;

			default:
				DEBUG(printk("scsi(%ld:%d:%d) %s: did_error "
				    "= %d, pid=%ld, comp-scsi= 0x%x-0x%x "
				    "fcport_state=0x%x sp_flags=0%x.\n",
				    vis_ha->host_no,
				    SCSI_TCN_32(cmd),
				    SCSI_LUN_32(cmd),
				    __func__,
				    (CMD_RESULT(cmd)>>16),
				    cmd->serial_number,
				    CMD_COMPL_STATUS(cmd),
				    CMD_SCSI_STATUS(cmd),
				    atomic_read(&sp->fclun->fcport->state),
				    sp->flags);)
				break;
		}

		/*
		 * Call the mid-level driver interrupt handler -- via sp_put()
		 */
		sp_put(ha, sp);

		if (vis_ha != old_ha)
			qla2x00_next(vis_ha);

	} /* end of while */
	qla2x00_next(old_ha);

	LEAVE(__func__);

	return (cnt);
}

STATIC uint8_t
qla2x00_suspend_lun(scsi_qla_host_t *ha, os_lun_t *lq, int time, int count)
{
	return (__qla2x00_suspend_lun(ha, lq, time, count, 0));
}

STATIC uint8_t
qla2x00_delay_lun(scsi_qla_host_t *ha, os_lun_t *lq, int time)
{
	return (__qla2x00_suspend_lun(ha, lq, time, 1, 1));
}
/*
 *  qla2x00_unsuspend_all_target
 *	Unsuspend all target. 
 *
 * Input:
 *	ha = visable adapter block pointer.
 *
 * Return:
 *
 * Context:
 *	Process context.
 */

STATIC void
qla2x00_unsuspend_all_target(scsi_qla_host_t *ha)
{
	os_tgt_t *tq;
	int 	 t;

	for (t = 0; t < ha->max_targets; t++) {
		if ((tq = ha->otgt[t]) == NULL)
			continue;

		clear_bit(TGT_SUSPENDED, &tq->q_flags); 
	}
}


/*
 *  qla2x00_suspend_all_target
 *	Suspend all target indefinitely. Caller need to make sure
 *	to explicitly unsuspend it later on.
 *
 * Input:
 *	ha = visable adapter block pointer.
 *  target = target queue
 *  time = time in seconds
 *
 * Return:
 *     QL_STATUS_SUCCESS  -- suspended lun 
 *     QL_STATUS_ERROR  -- Didn't suspend lun
 *
 * Context:
 *	qla2x00_suspend_target can be called in Interrupt context.
 *	Hold the hardware lock for synchronisation.
 */

STATIC uint8_t
qla2x00_suspend_all_target(scsi_qla_host_t *ha)
{
	os_tgt_t *tq;
	int 	 t, time;
	uint8_t  status = 0;	
	unsigned long cpu_flags = 0;

	/* Suspend the Target untill explicitly cleared */
	time = 0;

	for (t = 0; t < ha->max_targets; t++) {
		if ((tq = ha->otgt[t]) == NULL)
			continue;

		spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
		status = qla2x00_suspend_target(ha, tq, time);
		spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);
	}

	return status;
}

/*
 *  qla2x00_suspend_target
 *	Suspend target
 *
 * Input:
 *	ha = visable adapter block pointer.
 *  target = target queue
 *  time = time in seconds
 *
 * Return:
 *     QL_STATUS_SUCCESS  -- suspended lun 
 *     QL_STATUS_ERROR  -- Didn't suspend lun
 *
 * Context:
 *	Interrupt context.
 */
STATIC uint8_t
qla2x00_suspend_target(scsi_qla_host_t *ha,
		os_tgt_t *tq, int time)
{
	srb_t *sp;
	struct list_head *list, *temp;
	unsigned long flags;
	uint8_t	status;

	if ( !(test_bit(TGT_SUSPENDED, &tq->q_flags)) ){

		/* now suspend the lun */
		set_bit(TGT_SUSPENDED, &tq->q_flags);

		atomic_set(&tq->q_timer, time);

		DEBUG2(printk( KERN_INFO
			"scsi%ld: Starting - suspend target for %d secs\n",
			ha->host_no, time);)
		/*
		 * Remove all (TARGET) pending commands from request queue and put them
		 * in the scsi_retry queue.
		 */
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &ha->pending_queue) {
			sp = list_entry(list, srb_t, list);
			if (sp->tgt_queue != tq)
				continue;

			DEBUG3(printk(
			"scsi%ld: %s requeue for suspended target %p\n",
			ha->host_no, __func__, sp);)
			__del_from_pending_queue(ha, sp);
			__add_to_scsi_retry_queue(ha,sp);

		} /* list_for_each_safe */
		spin_unlock_irqrestore(&ha->list_lock, flags);
		status = QL_STATUS_SUCCESS;
	} else  {
		status = QL_STATUS_ERROR;
	}
	return( status );
}

/*
 *  qla2x00_suspend_lun
 *	Suspend lun and start port down timer
 *
 * Input:
 *	ha = visable adapter block pointer.
 *  lq = lun queue
 *  cp = Scsi command pointer 
 *  time = time in seconds
 *  count = number of times to let time expire
 *  delay_lun = non-zero, if lun should be delayed rather than suspended
 *
 * Return:
 *     QL_STATUS_SUCCESS  -- suspended lun 
 *     QL_STATUS_ERROR  -- Didn't suspend lun
 *
 * Context:
 *	Interrupt context.
 */
STATIC uint8_t
__qla2x00_suspend_lun(scsi_qla_host_t *ha,
		os_lun_t *lq, int time, int count, int delay_lun)
{
	srb_t *sp;
	struct list_head *list, *temp;
	unsigned long flags;
	uint8_t	status;

	/* if the lun_q is already suspended then don't do it again */
	if (lq->q_state == LUN_STATE_READY ||
		lq->q_state == LUN_STATE_RUN) {

		spin_lock_irqsave(&lq->q_lock, flags);
		if (lq->q_state == LUN_STATE_READY) {
			lq->q_max = count;
			lq->q_count = 0;
		}
		/* Set the suspend time usually 6 secs */
		atomic_set(&lq->q_timer, time);

		/* now suspend the lun */
		lq->q_state = LUN_STATE_WAIT;

		if (delay_lun) {
			set_bit(LUN_EXEC_DELAYED, &lq->q_flag);
			DEBUG(printk(KERN_INFO 
					"scsi%ld: Delay lun execution for %d "
					"secs, count=%d, max count=%d, "
					"state=%d\n",
					ha->host_no,
					time,
					lq->q_count,
					lq->q_max,
					lq->q_state);)
		} else {
			DEBUG(printk(KERN_INFO 
					"scsi%ld: Suspend lun for %d secs, "
					"count=%d, max count=%d, state=%d\n",
					ha->host_no,
					time,
					lq->q_count,
					lq->q_max,
					lq->q_state);)
		}
		spin_unlock_irqrestore(&lq->q_lock, flags);

		/*
		 * Remove all pending commands from request queue and  put them
		 * in the scsi_retry queue.
		 */
		spin_lock_irqsave(&ha->list_lock, flags);
		list_for_each_safe(list, temp, &ha->pending_queue) {
			sp = list_entry(list, srb_t, list);
			if (sp->lun_queue != lq)
				continue;

			__del_from_pending_queue(ha, sp);

			if( sp->cmd->allowed < count)
				sp->cmd->allowed = count;
			__add_to_scsi_retry_queue(ha,sp);

		} /* list_for_each_safe */
		spin_unlock_irqrestore(&ha->list_lock, flags);
		status = QL_STATUS_SUCCESS;
	} else
		status = QL_STATUS_ERROR;
	return( status );

}

/*
 *  qla2x00_flush_failover_queue
 *	Return cmds of a "specific" LUN from the failover queue with
 *      DID_BUS_BUSY status.
 *
 * Input:
 *	ha = adapter block pointer.
 *      q  = lun queue.
 *
 * Context:
 *	Interrupt context.
 */
void
qla2x00_flush_failover_q(scsi_qla_host_t *ha, os_lun_t *q)
{
	srb_t  *sp;
	struct list_head *list, *temp;
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(list, srb_t, list);
		/*
		 * If request originated from the same lun_q then delete it
		 * from the failover queue 
		 */
		if (q == sp->lun_queue) {
			/* Remove srb from failover queue. */
			__del_from_failover_queue(ha,sp);
			//CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
			//CMD_HANDLE(sp->cmd) = (unsigned char *) NULL;
			qla2x00_failover_cleanup(sp);
			__add_to_done_queue(ha, sp);
		}
	} /* list_for_each_safe() */
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 *  qla2x00_check_sense
 *
 * Input:
 * cp = SCSI command structure
 * lq = lun queue
 *
 * Return:
 *     QL_STATUS_SUCCESS  -- Lun suspended 
 *     QL_STATUS_ERROR  -- Lun not suspended
 *
 * Context:
 *	Interrupt context.
 */
STATIC uint8_t 
qla2x00_check_sense(Scsi_Cmnd *cp, os_lun_t *lq)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) cp->host->hostdata;
	srb_t		*sp;
	fc_port_t	*fcport;

	ha = ha;
	if (((cp->sense_buffer[0] & 0x70) >> 4) != 7) {
		return QL_STATUS_ERROR;
	}

	sp = (srb_t * )CMD_SP(cp);
	sp->flags |= SRB_GOT_SENSE;

	switch (cp->sense_buffer[2] & 0xf) {
		case RECOVERED_ERROR:
			CMD_RESULT(cp)  = DID_OK << 16;
			cp->sense_buffer[0] = 0;
			break;

		case NOT_READY:
			if (ql2xprocessnotready == 0){
				DEBUG(printk("%s : Disabled Not Ready"						" handling inside the driver\n"
					,__func__);)		
				break;
			}
			/*
			 * if current suspend count is greater than max suspend
			 * count then no more suspends. 
			 */
			fcport = lq->fclun->fcport;
			/*
			 * Suspend the lun only for hard disk device type.
			 */
			if (!(fcport->flags & FC_TAPE_DEVICE) &&
				lq->q_state != LUN_STATE_TIMEOUT) {

#if defined(COMPAQ)
				/* COMPAQ*/
				if ((lq->q_flag & LUN_SCSI_SCAN_DONE)) {
					DEBUG(printk(
						"scsi%ld: check_sense: "
						"lun%d, suspend count="
						"%d, max count=%d\n",
						ha->host_no,
						(int)SCSI_LUN_32(cp),
						lq->q_count,
						lq->q_max);)

					/*
					 * HSG80 can take awhile to
					 * become ready.
					 */
					if (cp->allowed != HSG80_SUSPEND_COUNT)
						cp->allowed =
							HSG80_SUSPEND_COUNT;
					qla2x00_suspend_lun(ha, lq, 6,
							HSG80_SUSPEND_COUNT);

					return (QL_STATUS_SUCCESS);
				}
#else
				/* non-COMPAQ*/
				/*
				 * if target is "in process of being 
				 * ready then suspend lun for 6 secs and
				 * retry all the commands.
				 */
				if ((cp->sense_buffer[12] == 0x4 &&
					cp->sense_buffer[13] == 0x1)) {

					/* Suspend the lun for 6 secs */
					qla2x00_suspend_lun(ha, lq, 6,
					    ql2xsuspendcount);

					return (QL_STATUS_SUCCESS);
				}
#endif /* COMPAQ */

			} /* EO if (lq->q_state != LUN_STATE_TIMEOUT )*/

			break;
	} /* end of switch */

	return (QL_STATUS_ERROR);
}

#if !defined(ISP2100) && !defined(ISP2200)
/**************************************************************************
 * qla23xx_blink_led
 *
 * Description:
 *   This function sets the colour of the LED while preserving the
 *   unsued GPIO pins every sec.
 *
 * Input:
 *       ha - Host adapter structure
 *      
 * Return:
 * 	None
 *
 * Context: qla2x00_timer() Interrupt
 ***************************************************************************/
inline void
qla23xx_flip_colors(scsi_qla_host_t *ha, uint16_t *pflags)
{
	if (ha->device_id == QLA2322_DEVICE_ID) {
		/* flip all colors */
		if (ha->beacon_color_state == QLA_LED_RGA_ON) {
			/* turn off */
			ha->beacon_color_state = 0;
			*pflags = 0;
		} else {
			/* turn on */
			ha->beacon_color_state = QLA_LED_RGA_ON;
			*pflags = LED_RGA_ON;
		}
	} else {
		/* flip green led only */
		if (ha->beacon_color_state == QLA_LED_GRN_ON) {
			/* turn off */
			ha->beacon_color_state = 0;
			*pflags = LED_GREEN_OFF_AMBER_OFF;
		} else {
			/* turn on */
			ha->beacon_color_state = QLA_LED_GRN_ON;
			*pflags = LED_GREEN_ON_AMBER_OFF;
		}
	}
}

STATIC void
qla23xx_blink_led(scsi_qla_host_t *ha)
{
	uint16_t	gpio_enable, gpio_data;
	uint16_t	led_color = 0;
	unsigned long	cpu_flags = 0;
	device_reg_t	*reg = ha->iobase;


	ENTER(__func__);

	if (ha->pio_address) 
		reg = (device_reg_t *)ha->pio_address;

	/* Save the Original GPIOE */ 
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
	if (ha->pio_address) {
		gpio_enable = RD_REG_WORD_PIO(&reg->gpioe);
		gpio_data   = RD_REG_WORD_PIO(&reg->gpiod);
	} else {
		gpio_enable = RD_REG_WORD(&reg->gpioe);
		gpio_data   = RD_REG_WORD(&reg->gpiod);
	}

	DEBUG9(printk("%s Original data of gpio_enable_reg=0x%x"
	    " gpio_data_reg=0x%x\n",
	    __func__,gpio_enable,gpio_data);)	

	gpio_enable |= LED_MASK;
	DEBUG9(printk("%s Before writing enable : gpio_enable_reg=0x%x"
	    " gpio_data_reg=0x%x led_color=0x%x\n",
	    __func__,gpio_enable,gpio_data,led_color);)	

	/* Set the modified gpio_enable values */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpioe,gpio_enable);
	} else {
		WRT_REG_WORD(&reg->gpioe,gpio_enable);
		PCI_POSTING(&reg->gpioe);
	}

	qla23xx_flip_colors(ha, &led_color);

	/* Clear out the previously set LED colour */
	gpio_data &= ~LED_MASK;
	/* Set the new input LED colour to GPIOD */
	gpio_data |= led_color;

	DEBUG9(printk("%s Before writing data: gpio_enable_reg=0x%x"
	    " gpio_data_reg=0x%x led_color=0x%x\n",
	    __func__,gpio_enable,gpio_data,led_color);)	

	/* Set the modified gpio_data values */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpiod,gpio_data);
	} else {
		WRT_REG_WORD(&reg->gpiod,gpio_data);
		PCI_POSTING(&reg->gpiod);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

        LEAVE(__func__);
}

STATIC void
qla23xx_stop_beacon(scsi_qla_host_t *ha)
{
	uint16_t	mb_stat;


	ha->beacon_blink_led = 0;
	/* Set the on flag so when it gets flipped it will be off */
	if (ha->device_id == QLA2322_DEVICE_ID)
		ha->beacon_color_state = QLA_LED_RGA_ON;
	else
		ha->beacon_color_state = QLA_LED_GRN_ON;

	qla23xx_blink_led(ha);

	if (qla2x00_get_firmware_options(ha, &ha->fw_options1, &ha->fw_options2,
	    &ha->fw_options3, &mb_stat) != QL_STATUS_SUCCESS){

		DEBUG9_10(printk("%s(%ld): inst=%ld get_firmware"
		    " options failed.\n",
		    __func__, ha->host_no, ha->instance);)
		return;
	}

	DEBUG9(printk("%s(%ld): inst=%ld get_firmware"
	    " options success fw_options1=0x%x"
	    " fw_options2=0x%x fw_options3=0x%x.\n",
	    __func__, ha->host_no, ha->instance,
	    ha->fw_options1,ha->fw_options2,ha->fw_options3);)

	/* Clear BIT_8 to not set Output Emphasis 
	 * and Output Swing values again.
	 */ 
	ha->fw_options1 &= ~BIT_8;
	ha->fw_options1 &= ~DISABLE_GPIO; /* Enable GPIO pins */

	if (qla2x00_set_firmware_options(ha, ha->fw_options1, ha->fw_options2,
	    ha->fw_options3, 0, 0, &mb_stat) != QL_STATUS_SUCCESS){

		DEBUG9_10(printk("%s(%ld): inst=%ld set" 
		    "firmware  options failed.\n",
		    __func__, ha->host_no, ha->instance);)
	}
	return;
}

/**************************************************************************
 * qla24xx_blink_led
 *
 * Description:
 *   This function is same as qla2x00_blink_led but used for 24xx and 25xx.
 *
 * Input:
 *       ha - Host adapter structure
 *
 * Return:
 *      None
 *
 * Context: qla2x00_timer() Interrupt
 ***************************************************************************/
inline void
qla24xx_flip_colors(scsi_qla_host_t *ha, uint16_t *pflags)
{
	/* flip all colors */
	if (ha->beacon_color_state == QLA_LED_BCN_ON) {
		/* turn off */
		ha->beacon_color_state = 0;
		*pflags = 0;
	} else {
		/* turn on */
		ha->beacon_color_state = QLA_LED_BCN_ON;
		*pflags = GPDX_LED_YELLOW_ON | GPDX_LED_AMBER_ON;
	}
}

void
qla24xx_blink_led(scsi_qla_host_t *ha)
{
	uint16_t        led_color = 0;
	uint32_t        gpio_data;
	unsigned long   cpu_flags = 0;
	struct device_reg_24xx *reg24 = (struct device_reg_24xx *)ha->iobase;


	/* Save the Original GPIOD */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
	gpio_data = RD_REG_DWORD(&reg24->gpiod);

	DEBUG9(printk("%s Original data of gpio_data_reg=0x%x\n",
	    __func__, gpio_data);)

	/* Enable the gpio_data reg for update */
	gpio_data |= GPDX_LED_UPDATE_MASK;

	DEBUG9(printk("%s(%ld): Before writing enable, gpio_data_reg=0x%x.\n",
	    __func__, ha->host_no, gpio_data);)

	WRT_REG_DWORD(&reg24->gpiod, gpio_data);
	gpio_data = RD_REG_DWORD(&reg24->gpiod);

	DEBUG9(printk("%s(%ld): Before writing color: gpio_data_reg=0x%x.\n",
	    __func__, ha->host_no, gpio_data);)

	/* Set the color bits */
	qla24xx_flip_colors(ha, &led_color);

	/* Clear out any previously set LED color */
	gpio_data &= ~GPDX_LED_COLOR_MASK;

	/* Set the new input LED color to GPIOD */
	gpio_data |= led_color;

	/* Set the modified gpio_data values */
	WRT_REG_DWORD(&reg24->gpiod, gpio_data);
	gpio_data = RD_REG_DWORD(&reg24->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	DEBUG9(printk("%s(%ld): After writing color: gpio_data_reg=0x%x.\n",
	    __func__, ha->host_no, gpio_data);)

	return;
}

STATIC void
qla24xx_stop_beacon(scsi_qla_host_t *ha)
{
	uint16_t	mb_stat;
	uint32_t        gpio_data;
	unsigned long   cpu_flags = 0;
	struct device_reg_24xx *reg24 = (struct device_reg_24xx *)ha->iobase;


	ha->beacon_blink_led = 0;
	/* Set the on flag so when it gets flipped it will be off */
	ha->beacon_color_state = QLA_LED_BCN_ON;

	qla24xx_blink_led(ha);

	if (qla2x00_get_firmware_options(ha, &ha->fw_options1, &ha->fw_options2,
	    &ha->fw_options3, &mb_stat) != QL_STATUS_SUCCESS){

		DEBUG9_10(printk("%s(%ld): inst=%ld get_firmware"
		    " options failed.\n",
		    __func__, ha->host_no, ha->instance);)
		return;
	}

	DEBUG9(printk("%s(%ld): inst=%ld get_firmware options success fw_options1=0x%x"
	    " fw_options2=0x%x fw_options3=0x%x.\n",
	    __func__, ha->host_no, ha->instance, ha->fw_options1, ha->fw_options2,
	    ha->fw_options3);)

	/* give control back to firmware */
	spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
	gpio_data = RD_REG_DWORD(&reg24->gpiod);

	/* Disable the gpio_data reg for update */
	gpio_data &= ~GPDX_LED_UPDATE_MASK;
	WRT_REG_DWORD(&reg24->gpiod, gpio_data);
	RD_REG_DWORD(&reg24->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

	ha->fw_options1 &= ~ADD_FO1_DISABLE_GPIO_LED_CTRL;

	if (qla2x00_set_firmware_options(ha, ha->fw_options1, ha->fw_options2,
	    ha->fw_options3, 0, 0, &mb_stat) != QL_STATUS_SUCCESS){
		DEBUG9_10(printk("%s(%ld): inst=%ld set"
		    "firmware options failed.\n",
		    __func__, ha->host_no, ha->instance);)
		return;
	}

	return;
}

#endif 

/**************************************************************************
*   qla2x00_timer
*
* Description:
*   One second timer
*
* Context: Interrupt
***************************************************************************/
STATIC void
qla2x00_timer(scsi_qla_host_t *ha)
{
	int		t,l;
	unsigned long	cpu_flags = 0;
	fc_port_t	*fcport;
	os_lun_t *lq;
	os_tgt_t *tq;
	int		start_dpc = 0;
	int		index;
	srb_t		*sp;

	/*
	 * We try and restart any request in the retry queue every second.
	 */
	if (!list_empty(&ha->retry_queue)) {
		set_bit(PORT_RESTART_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}

	/*
	 * We try and restart any request in the scsi_retry queue every second.
	 */
	if (!list_empty(&ha->scsi_retry_queue)) {
		set_bit(SCSI_RESTART_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}
#if !defined(ISP2100) && !defined(ISP2200)
	/* Check if LED needs to be blinked */
	if (ha->beacon_blink_led){
		set_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}
#endif

	/*
	 * We try and failover any request in the failover queue every second.
	 */
	if (!list_empty(&ha->failover_queue)) {
		set_bit(FAILOVER_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}

	/*
	 * Ports - Port down timer.
	 *
	 * Whenever, a port is in the LOST state we start decrementing its port
	 * down timer every second until it reaches zero. Once  it reaches zero
	 * the port it marked DEAD. 
	 */
	t = 0;
	list_for_each_entry(fcport, &ha->fcports, list) {
		if(fcport->port_type != FCT_TARGET)
			continue;

		if (atomic_read(&fcport->state) == FC_DEVICE_LOST) {

			if (atomic_read(&fcport->port_down_timer) == 0)
				continue;

			if (atomic_dec_and_test(&fcport->port_down_timer)
				       	!= 0) {
				atomic_set(&fcport->state, FC_DEVICE_DEAD);
				DEBUG2(printk(" scsi(%ld): Port num %d marked DEAD"
 		    " at portid=%02x%02x%02x.\n",
 		    ha->host_no, t, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa); )
			}
			
			DEBUG2(printk("scsi(%ld): fcport-%d - port retry count "
					":%d remainning\n",
					ha->host_no, 
					t,
					atomic_read(&fcport->port_down_timer));)
		}
		t++;
	} /* End of for fcport  */

	/*
	 * LUNS - lun suspend timer.
	 *
	 * Whenever, a lun is suspended the timer starts decrementing its
	 * suspend timer every second until it reaches zero. Once  it reaches
	 * zero the lun retry count is decremented. 
	 */

	/*
	 * FIXME(dg) - Need to convert this linear search of luns into a search
	 * of a list of suspended luns.
	 */
	for (t = 0; t < ha->max_targets; t++) {
		if ((tq = ha->otgt[t]) == NULL)
			continue;

		if ( atomic_read(&tq->q_timer) != 0) {
			DEBUG3(printk( KERN_INFO
				"scsi%ld: target%d - timer %d\n ",
						ha->host_no, 
						t, 
						(int)atomic_read(&tq->q_timer)));
			if (atomic_dec_and_test(&tq->q_timer) != 0) {
				DEBUG2(printk( KERN_INFO
					"scsi%ld: Ending - target %d suspension.\n",
					ha->host_no, t);)
				clear_bit(TGT_SUSPENDED, &tq->q_flags); 
				/* retry the commands */
				set_bit(TGT_RETRY_CMDS, &tq->q_flags); 
				start_dpc++;
			}
		}

		for (l = 0; l < ha->max_luns; l++) {
			if ((lq = (os_lun_t *) tq->olun[l]) == NULL)
				continue;

			spin_lock_irqsave(&lq->q_lock, cpu_flags);
			if (lq->q_state == LUN_STATE_WAIT &&
				atomic_read(&lq->q_timer) != 0) {

				if (atomic_dec_and_test(&lq->q_timer) != 0) {
					/*
					 * A delay should immediately
					 * transition to a READY state
					 */
					if (test_and_clear_bit(LUN_EXEC_DELAYED,
								&lq->q_flag)) {
						lq->q_state = LUN_STATE_READY;
					}
					else {
						lq->q_count++;
						if (lq->q_count == lq->q_max)
							lq->q_state =
							      LUN_STATE_TIMEOUT;
						else
							lq->q_state =
								LUN_STATE_RUN;
					}
				}
				DEBUG3(printk("scsi%ld: lun%d - timer %d, "
						"count=%d, max=%d, state=%d\n",
						ha->host_no, 
						l, 
						atomic_read(&lq->q_timer),
						lq->q_count,
						lq->q_max,
						lq->q_state);)
			}
			spin_unlock_irqrestore(&lq->q_lock, cpu_flags);
		} /* End of for luns  */
	} /* End of for targets  */

	/* Loop down handler. */
	if (atomic_read(&ha->loop_down_timer) > 0 && 
		!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) &&
		ha->flags.online) {

		/* dg 10/30 if (atomic_read(&ha->loop_down_timer) == LOOP_DOWN_TIME) { */
		if ( atomic_read(&ha->loop_down_timer) == 
				ha->loop_down_abort_time ) {
			DEBUG(printk("qla%ld: Loop Down - aborting the queues "
					"before time expire\n",
					ha->instance);)
#if !defined(ISP2100)
			if (ha->link_down_timeout) {
				atomic_set(&ha->loop_state, LOOP_DEAD); 
				printk(KERN_INFO
					"scsi(%ld): LOOP DEAD detected.\n",
					ha->host_no);
				DEBUG2(printk(
					"scsi(%ld): LOOP DEAD detected.\n",
					ha->host_no);)
			}
#endif
			/* Schedule an ISP abort to return any tape commands. */
			spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
			for (index = 1; index < MAX_OUTSTANDING_COMMANDS;
			    index++) {
				sp = ha->outstanding_cmds[index];
				if (!sp)
					continue;
				if (!(sp->fclun->fcport->flags &
				    FC_TAPE_DEVICE))
					continue;

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;
			}
			spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

			set_bit(ABORT_QUEUES_NEEDED, &ha->dpc_flags);
			start_dpc++;
		}

		/* if the loop has been down for 4 minutes, reinit adapter */
		if (atomic_dec_and_test(&ha->loop_down_timer) != 0) {
			DEBUG(printk("qla%ld: Loop down exceed 4 mins - "
					"restarting queues.\n",
					ha->instance);)

			set_bit(RESTART_QUEUES_NEEDED, &ha->dpc_flags);
			start_dpc++;
			if (!(ha->device_flags & DFLG_NO_CABLE) &&
			     qla2x00_reinit && !ha->flags.failover_enabled) {
				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			DEBUG(printk("qla%ld: Loop down - aborting ISP.\n",
					ha->instance);)
			}
		}
		if (!(ha->device_flags & DFLG_NO_CABLE))
			 DEBUG2(printk(KERN_INFO
			     "qla%ld: Loop Down - seconds remainning %d\n",
				ha->instance, 
				atomic_read(&ha->loop_down_timer));)
	}

	/*
	 * Done Q Handler -- dgFIXME This handler will kick off doneq if we
	 * haven't process it in 2 seconds.
	 */
	if (!list_empty(&ha->done_queue))
		qla2x00_done(ha);

#if QLA2100_LIPTEST
	/*
	 * This block is used to periodically schedule isp abort after
	 * qla2x00_lip flag is set. 
	 */

	
	   /*if (qla2x00_lip && (ha->forceLip++) == (60*2)) {
		   printk("%s: schedule isp abort.\n",__func__);
		   set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		   ha->forceLip = 0;
	   }*/
	 

	/*
	 * This block is used to periodically schedule mailbox cmd timeout
	 * simulation
	 */
	if (qla2x00_lip && (ha->forceLip++) == (60*6)) {
		printk("qla2x00_timer: Going to force mbx timeout\n");

		ha->forceLip = 0;
		mbxtimeout = 1;
	}
#endif

#if defined(EH_WAKEUP_WORKAROUND)
	if (ha->host->in_recovery &&
#if defined(EH_WAKEUP_WORKAROUND_REDHAT)
		(atomic_read(&(ha->host->host_busy)) ==
		      ha->host->host_failed) &&
#else
		(ha->host->host_busy == ha->host->host_failed) &&
#endif
		!ha->host->eh_active) {	

		if ((ha->eh_start++) == 60) {
			if (ha->host->eh_wait)
				up(ha->host->eh_wait);
			ha->eh_start=0;
			printk("qla%ld: !!! Waking up error handler "
				"for scsi layer\n",
				ha->host_no);
		}
	}
#endif /* EH_WAKEUP_WORKAROUND */

	if (test_bit(FAILOVER_EVENT_NEEDED, &ha->dpc_flags)) {
		if (ha->failback_delay)  {
			ha->failback_delay--;
			if (ha->failback_delay == 0)  {
				set_bit(FAILOVER_EVENT, &ha->dpc_flags);
				clear_bit(FAILOVER_EVENT_NEEDED,
						&ha->dpc_flags);
			}
		} else {
			set_bit(FAILOVER_EVENT, &ha->dpc_flags);
			clear_bit(FAILOVER_EVENT_NEEDED, &ha->dpc_flags);
		}
	}
#if 0
	/* Workaround for I/O not being retruned by FW after LOOP DOWN */
	if (test_bit(LOOP_DOWN_IO_RECOVERY, &ha->dpc_flags)) {
		if (ha->cmd_wait_delay)  {
			ha->cmd_wait_delay--;
			if (ha->cmd_wait_delay == 0)  {
				if (ha->actthreads) {
					DEBUG2(printk("%s(%ld)  scheduling big"
					    " hammer since active_io=%ld after"
					    " loop down event\n", __func__, 
					    ha->host_no, ha->actthreads);)	
					printk(KERN_INFO "%s(%ld)  scheduling big"
					    " hammer since active_io=%ld after"
					    " loop down event\n", __func__, 
					    ha->host_no, ha->actthreads);	
					set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				}
				clear_bit(LOOP_DOWN_IO_RECOVERY, &ha->dpc_flags);
				printk("%s(%ld) LOOP_DOWN_IO_RECOVERY cleared active_io=%ld\n", __func__,
				    	ha->host_no, ha->actthreads);
				printk(KERN_INFO "%s(%ld) LOOP_DOWN_IO_RECOVERY cleared active_io=%ld\n", __func__,
				    	ha->host_no, ha->actthreads);
			}
		} 
	}
#endif

	/* Schedule the DPC routine if needed */
	if ((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
		start_dpc ||
		test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags) ||
		test_bit(RELOGIN_NEEDED, &ha->dpc_flags) ||
		test_bit(FAILOVER_EVENT, &ha->dpc_flags) ||
		test_bit(FAILOVER_NEEDED, &ha->dpc_flags) ||
		test_bit(PORT_SCAN_NEEDED, &ha->dpc_flags) ||
		test_bit(LOOP_RESET_NEEDED, &ha->dpc_flags) ||
		test_bit(IOCTL_ERROR_RECOVERY, &ha->dpc_flags) ||
		test_bit(MAILBOX_CMD_NEEDED, &ha->dpc_flags)) &&
		ha->dpc_wait && !ha->dpc_active ){   /* v2.19.4 */

		up(ha->dpc_wait);
	}

	RESTART_TIMER(qla2x00_timer,ha,WATCH_INTERVAL);
}


#if  NO_LONG_DELAYS
/*
 * This would normally need to get the IO request lock, but as it doesn't
 * actually touch anything that needs to be locked we can avoid the lock here..
 */
STATIC void 
qla2x00_sleep_done(struct semaphore * sem)
{
	if (sem != NULL)
	{
		up(sem);
	}
}
#endif

/*
* qla2x00_callback
*      Returns the completed SCSI command to LINUX.
*
* Input:
*	ha -- Host adapter structure
*	cmd -- SCSI mid-level command structure.
* Returns:
*      None
* Note:From failover point of view we always get the sp
*      from vis_ha pool in queuecommand.So when we put it 
*      back to the pool it has to be the vis_ha.	 
*      So rely on Scsi_Cmnd to get the vis_ha and not on sp. 		 	
*/
static inline void
qla2x00_callback(scsi_qla_host_t *ha, Scsi_Cmnd *cmd)
{
	srb_t *sp = (srb_t *) CMD_SP(cmd);
	scsi_qla_host_t *vis_ha;
	os_lun_t *lq;
	uint8_t is_fdmi_cmnd;
	uint8_t got_sense;
	unsigned long	cpu_flags = 0;

	ENTER(__func__);

	CMD_HANDLE(cmd) = (unsigned char *) NULL;
	vis_ha = (scsi_qla_host_t *) cmd->host->hostdata;

	if (sp == NULL) {
		printk(KERN_INFO
			"%s(): **** CMD derives a NULL SP\n",
			__func__);
		return;
	}

	/*
	 * If command status is not DID_BUS_BUSY then go ahead and freed sp.
	 */
	/*
	 * Cancel command timeout
	 */
	qla2x00_delete_timer_from_cmd(sp);

	/*
	 * Put SP back in the free queue
	 */
	sp->cmd   = NULL;
	CMD_SP(cmd) = NULL;
	lq = sp->lun_queue;
	is_fdmi_cmnd = (sp->flags & SRB_FDMI_CMD) ? 1 : 0;
	got_sense = (sp->flags & SRB_GOT_SENSE)? 1: 0;
#if REG_FDMI_ENABLED
	if (is_fdmi_cmnd) {
		DEBUG13(printk("%s(%ld): going to free fdmi srb tmpmem. "
		    "result=%d.\n",
		    __func__, vis_ha->host_no, CMD_RESULT(cmd)>>16);)
		/* free some tmp buffers saved in sp */
		qla2x00_fdmi_srb_tmpmem_free(sp);
	}
#endif
	add_to_free_queue(vis_ha, sp);

	if ((CMD_RESULT(cmd)>>16) == DID_OK) {
		/* device ok */
		if (!is_fdmi_cmnd) {
			/* keep IO stats for SCSI commands only. */
			ha->total_bytes += cmd->bufflen;

			if (cmd->bufflen) {
				if (sp->dir & __constant_cpu_to_le16(CF_READ))
					ha->total_input_bytes += cmd->bufflen;
				else
					ha->total_output_bytes += cmd->bufflen;
			}
		}

		if (!got_sense) {
			/* COMPAQ*/
#if defined(COMPAQ)
			/*
			 * When we detect the first good Read capability scsi
			 * command we assume the SCSI layer finish the scan.
			 */
			if (cmd->cmnd[0] == 0x25 &&
				!(lq->q_flag & LUN_SCSI_SCAN_DONE)) {
				/* mark lun with finish scan */
				lq->q_flag |= LUN_SCSI_SCAN_DONE;
			}
#endif /* COMPAQ */
			/*
			 * If lun was suspended then clear retry count.
			 */
			spin_lock_irqsave(&lq->q_lock, cpu_flags);
			if (!test_bit(LUN_EXEC_DELAYED, &lq->q_flag))
				lq->q_state = LUN_STATE_READY;
			spin_unlock_irqrestore(&lq->q_lock, cpu_flags);
		}
	} else if ((CMD_RESULT(cmd)>>16) == DID_ERROR) {
		/* device error */
		ha->total_dev_errs++;
	}

	if (cmd->flags & IS_RESETTING) {
		CMD_RESULT(cmd) = (int)DID_RESET << 16;
	}

	/* Call the mid-level driver interrupt handler */
	(*(cmd)->scsi_done)(cmd);

	LEAVE(__func__);
}

#if DEBUG_GET_FW_DUMP
#if defined(ISP2300)
static void
qla2x00_alloc_fw_dump(scsi_qla_host_t *ha)
{
	if (!check_24xx_or_54xx_device_ids(ha) && !check_25xx_device_ids(ha))
		return;

	ha->fw_dumped = 0;
	ha->fw_dump24_len = sizeof(struct qla24xx_fw_dump);
	ha->fw_dump24_len += (ha->fw_memory_size - 0x100000) * sizeof(uint32_t);
	DEBUG2(printk("%s fw_dump24_len=0x%x memory_size=0x%x  alloc_size=0x%x\n", __func__ , 
		sizeof(struct qla24xx_fw_dump), ha->fw_memory_size, 
		ha->fw_dump24_len);)
	ha->fw_dump24 = vmalloc(ha->fw_dump24_len);
	if (ha->fw_dump24)
		printk(KERN_INFO "%s(%ld)Allocated (%d KB) for firmware "
		    "dump...\n", __func__, ha->host_no, ha->fw_dump24_len / 1024);
	else
		printk(KERN_WARNING "%s(%ld)Unable to allocate (%d KB) for "
		    "firmware dump!!!\n",  __func__, ha->host_no, ha->fw_dump24_len / 1024);
}
#endif
#endif





/*
* qla2x00_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      1  = failure.
*/
static uint8_t
qla2x00_mem_alloc(scsi_qla_host_t *ha)
{
	uint8_t   status = 1;
	uint8_t   i;
	int	retry= 10;
	mbx_cmdq_t	*ptmp;
	mbx_cmdq_t	*tmp_q_head;
	mbx_cmdq_t	*tmp_q_tail;

	ENTER(__func__);

	do {
		/*
		 * This will loop only once if everything goes well, else some
		 * number of retries will be performed to get around a kernel
		 * bug where available mem is not allocated until after a
		 * little delay and a retry.
		 */

		if( retry != 10 )
			printk( KERN_INFO
				"scsi(%ld): Memory Allocation retry %d \n",
				ha->host_no, retry);
			
#if defined(ISP2300)
		ha->risc_rec_q = pci_alloc_consistent(ha->pdev,
					((IP_BUFFER_QUEUE_DEPTH) * 
					 (sizeof(struct risc_rec_entry))),
					&ha->risc_rec_q_dma);
		if (ha->risc_rec_q == NULL) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"risc_rec_q\n",
				ha->host_no);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}
#endif	/* #if defined(ISP2300) */

		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
			ha->request_ring = pci_alloc_consistent(ha->pdev,
					((REQUEST_ENTRY_CNT_24XX + 1) * 
					 (sizeof(request_t))),
					&ha->request_dma);
		else 
			ha->request_ring = pci_alloc_consistent(ha->pdev,
					((REQUEST_ENTRY_CNT + 1) * 
					 (sizeof(request_t))),
					&ha->request_dma);
		if (ha->request_ring == NULL) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"request_ring\n",
				ha->host_no);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		ha->response_ring = pci_alloc_consistent(ha->pdev,
					((RESPONSE_ENTRY_CNT + 1) * 
					 (sizeof(response_t))),
					&ha->response_dma);
		if (ha->response_ring == NULL) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"response_ring\n",
				ha->host_no);
			qla2x00_mem_free(ha);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		/* get consistent memory allocated for init control block */
		ha->init_cb = pci_alloc_consistent(ha->pdev,
				sizeof(struct init_cb),
				&ha->init_cb_dma);
		if (ha->init_cb == NULL) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"init_cb\n",
				ha->host_no);
			qla2x00_mem_free(ha);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}
		memset(ha->init_cb, 0, sizeof(struct init_cb));

		/* Allocate ioctl related memory. */
		if (qla2x00_alloc_ioctl_mem(ha)) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"ioctl_mem\n",
				ha->host_no);
			qla2x00_mem_free(ha);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		if (qla2x00_allocate_sp_pool(ha)) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"qla2x00_allocate_sp_pool\n",
				ha->host_no);
			qla2x00_mem_free(ha);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}

		/*
		 * Allocate an initial list of mailbox semaphore queue to be
		 * used for serialization of the mailbox commands.
		 */
		tmp_q_head = (void *)KMEM_ZALLOC(sizeof(mbx_cmdq_t), 20);
		if (tmp_q_head == NULL) {
			/* error */
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - "
				"mbx_cmd_q",
				ha->host_no);
			qla2x00_mem_free(ha);
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/10);
			continue;
		}
		ha->mbx_sem_pool_head = tmp_q_head;
		tmp_q_tail = tmp_q_head;
		/* Now try to allocate more */
		for (i = 1; i < MBQ_INIT_LEN; i++) {
			ptmp = (void *)KMEM_ZALLOC(sizeof(mbx_cmdq_t), 20 + i);
			if (ptmp == NULL) {
				/*
				 * Error. Just exit. If more is needed later
				 * they will be allocated at that time.
				 */
				break;
			}
			tmp_q_tail->pnext = ptmp;
			tmp_q_tail = ptmp;
		}
		ha->mbx_sem_pool_tail = tmp_q_tail;

		/* Done all allocations without any error. */
		status = 0;

	} while (retry-- && status != 0);

	if (status) {
		printk(KERN_WARNING
			"%s(): **** FAILED ****\n", __func__);
	}

	LEAVE(__func__);

	return(status);
}

/*
* qla2x00_mem_free
*      Frees all adapter allocated memory.
*
* Input:
*      ha = adapter block pointer.
*/
STATIC void
qla2x00_mem_free(scsi_qla_host_t *ha)
{
	uint32_t	t;
	fc_port_t	*fcport, *fcptemp;
	fc_lun_t	*fclun, *fcltemp;
	mbx_cmdq_t	*ptmp;
	mbx_cmdq_t	*tmp_q_head;
	unsigned long	wtime;/* max wait time if mbx cmd is busy. */

	ENTER(__func__);

	if (ha == NULL) {
		/* error */
		DEBUG2(printk(KERN_INFO "%s(): ERROR invalid ha pointer.\n", __func__);)
		return;
	}

	/* Free the target queues */
	for (t = 0; t < MAX_TARGETS; t++) {
		qla2x00_tgt_free(ha, t);
	}

	/* Make sure all other threads are stopped. */
	wtime = 60 * HZ;
	while ((ha->dpc_wait != NULL || 
		ha->mbx_q_head != NULL) && 
		wtime) {

		set_current_state(TASK_INTERRUPTIBLE);
		wtime = schedule_timeout(wtime);
	}

	/* Now free the mbx sem pool */
	tmp_q_head = ha->mbx_sem_pool_head;
	while (tmp_q_head != NULL) {
		ptmp = tmp_q_head->pnext;
		KMEM_FREE(tmp_q_head, sizeof(mbx_cmdq_t));
		tmp_q_head = ptmp;
	}
	ha->mbx_sem_pool_head = NULL;

	/* free ioctl memory */
	qla2x00_free_ioctl_mem(ha);

	/* free sp pool */
	qla2x00_free_sp_pool(ha);

#if DEBUG_GET_FW_DUMP
#if defined(ISP2300)
	/* Free the ISP24XX FW DUMP Buffer */
	 if (ha->fw_dump24) {
		 vfree(ha->fw_dump24);
		 ha->fw_dump24 = NULL;
	 }
#endif
#endif

	/* 4.10 */
	/* free memory allocated for init_cb */
	if (ha->init_cb) {
		pci_free_consistent(ha->pdev, 
				sizeof(struct init_cb),
				ha->init_cb, 
				ha->init_cb_dma);
	}

	if (ha->request_ring) {
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
			pci_free_consistent(ha->pdev,
				((REQUEST_ENTRY_CNT_24XX + 1) * 
				 (sizeof(request_t))),
				ha->request_ring, 
				ha->request_dma);
		else	
			pci_free_consistent(ha->pdev,
				((REQUEST_ENTRY_CNT + 1) * 
				 (sizeof(request_t))),
				ha->request_ring, 
				ha->request_dma);
	}

	if (ha->response_ring) {
		pci_free_consistent(ha->pdev,
				((RESPONSE_ENTRY_CNT + 1) * 
				 (sizeof(response_t))),
				ha->response_ring, 
				ha->response_dma);
	}

#if defined(ISP2300)
	if (ha->risc_rec_q) {
		pci_free_consistent(ha->pdev,
				((IP_BUFFER_QUEUE_DEPTH) * 
				 (sizeof(struct risc_rec_entry))),
				ha->risc_rec_q, 
				ha->risc_rec_q_dma);
	}
	ha->risc_rec_q = NULL;
	ha->risc_rec_q_dma = 0;
#endif

	ha->init_cb = NULL;
	ha->request_ring = NULL;
	ha->request_dma = 0;
	ha->response_ring = NULL;
	ha->response_dma = 0;

 	list_for_each_entry_safe(fcport, fcptemp, &ha->fcports, list) {
		list_for_each_entry_safe(fclun, fcltemp, &fcport->fcluns,
		    list) {
 			list_del_init(&fclun->list);
			kfree(fclun);
		}
		list_del_init(&fcport->list);
		kfree(fcport);
	}
 	INIT_LIST_HEAD(&ha->fcports);

	LEAVE(__func__);
}

#if defined(ISP2300)
static int
qla2x00_mask_hba(scsi_qla_host_t *ha)
{
	int rval;
	int instance;
	int mask;
	uint8_t *propbuf;
	uint8_t pn[WWN_SIZE];
	int propval;

	mask = 0;
	if (!ql2xdevconf)
		return mask;

	propbuf = kmalloc(LINESIZE, GFP_KERNEL);
	if (!propbuf)
		return mask;

	for (instance = 0; instance < MAX_HOST_COUNT; instance++) {
		sprintf(propbuf, "scsi-qla%d-adapter-port", instance);
		rval = qla2x00_get_prop_16chars(ha, propbuf, pn, ha->cmdline);
		if (rval != QLA2X00_SUCCESS)
			break;

		/* Portname match? */
		if (memcmp(ha->port_name, pn, WWN_SIZE))
			continue;

		/* Check for disable directive. */
		propval = 0;
		sprintf(propbuf, "scsi-qla%d-adapter-disable", instance);
		rval = qla2x00_get_prop_int(ha, propbuf, &propval);
		if (!rval)
			break;
		if (propval)
			mask = 1;
		break;
	}
	kfree(propbuf);

	return mask;
}
#endif

#if 0
/*
*  qla2x00_abort_pending_queue
*      Abort all commands on the pending queue.
*
* Input:
*      ha = adapter block pointer.
*/
STATIC void
qla2x00_abort_pending_queue(scsi_qla_host_t *ha, uint32_t stat)
{
	unsigned long		flags;
	struct list_head	*list, *temp;

	ENTER("qla2x00_abort_pending_queue");

	DEBUG5(printk("Abort pending queue ha(%d)\n", ha->host_no);)

	/* abort all commands on LUN queue. */
	spin_lock_irqsave(&ha->list_lock, flags);
	list_for_each_safe(list, temp, &ha->pending_queue) {
		srb_t *sp;

		sp = list_entry(list, srb_t, list);
		__del_from_pending_queue(ha, sp);
		CMD_RESULT(sp->cmd) = stat << 16;
		__add_to_done_queue(ha, sp);
	} /* list_for_each_safe */
	spin_unlock_irqrestore(&ha->list_lock, flags);

	LEAVE("qla2x00_abort_pending_queue");
}
#endif


/****************************************************************************/
/*                QLogic ISP2x00 Hardware Support Functions.                */
/****************************************************************************/

/*
* qla2x00_initialize_adapter
*      Initialize board.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
uint8_t
qla2x00_initialize_adapter(scsi_qla_host_t *ha)
{
	device_reg_t *reg;
	uint8_t      status;
	uint8_t      isp_init = 0;
	uint8_t      restart_risc = 0;
	uint8_t      retry;
#if 0
	unsigned long	wait_device = 0;
#endif

	ENTER(__func__);

	/* Clear adapter flags. */
	ha->forceLip = 0;
	ha->flags.online = FALSE;
	ha->flags.disable_host_adapter = FALSE;
	ha->flags.reset_active = FALSE;
	atomic_set(&ha->loop_down_timer, LOOP_DOWN_TIME);
	atomic_set(&ha->loop_state, LOOP_DOWN);
	ha->device_flags = 0;
	ha->sns_retry_cnt = 0;
	ha->device_flags = 0;
	ha->dpc_flags = 0;
	ha->sns_retry_cnt = 0;
	ha->failback_delay = 0;
	ha->iocb_cnt = 0;
	ha->iocb_overflow_cnt = 0;
	/* 4.11 */
	ha->flags.management_server_logged_in = 0;
	/* ra 11/27/01 */
	ha->marker_needed = 0;
	ha->mbx_flags = 0;
	ha->isp_abort_cnt = 0;
#if defined(ISP2300)
	ha->beacon_blink_led = 0; /* Blink off */	
#endif

	DEBUG(printk("Configure PCI space for adapter...\n"));

	if (!(status = qla2x00_pci_config(ha))) {
		reg = ha->iobase;

		qla2x00_reset_chip(ha);

		/* Initialize target map database. */
		qla2x00_init_tgt_map(ha);

		/* Get Flash Version */
		qla2x00_get_flash_version(ha);

		if (qla2x00_verbose)
			printk("scsi(%ld): Configure NVRAM parameters...\n",
				ha->host_no);

#if defined(ISP2100)
		qla2100_nvram_config(ha);
#else
		qla2x00_nvram_config(ha);
#endif

#if USE_PORTNAME
		ha->flags.port_name_used =1;
#else
		ha->flags.port_name_used =0;
#endif

		if (qla2x00_verbose)
			printk("scsi(%ld): Verifying loaded RISC code...\n",
				ha->host_no);

		qla2x00_set_cache_line(ha);

		/*
		 * If the user specified a device configuration on the command
		 * line then use it as the configuration.  Otherwise, we scan
		 * for all devices.
		 */
		if (ql2xdevconf) {
			ha->cmdline = ql2xdevconf;
#if defined(ISP2300)
			if (qla2x00_mask_hba(ha)) {
				/* Mask the HBA. */
				printk(KERN_INFO "scsi(%ld)Masking HBA WWPN "
				    "%02x%02x%02x%02x%02x%02x%02x%02x\n",
				    ha->host_no,
				    ha->port_name[0], ha->port_name[1],
				    ha->port_name[2], ha->port_name[3],
				    ha->port_name[4], ha->port_name[5],
				    ha->port_name[6], ha->port_name[7]);
				status = 1;	
				return status;
			}
#endif
			if (!ha->flags.failover_enabled)
				qla2x00_get_properties(ha, ql2xdevconf);
		}

		retry = QLA2XXX_LOOP_RETRY_COUNT;
		/*
		 * Try configure the loop.
		 */
		do {
			restart_risc = 0;
			isp_init = 0;
			DEBUG(printk("%s(): check if firmware needs to be "
					"loaded\n",
					__func__);)

			/* If firmware needs to be loaded */
			if (qla2x00_isp_firmware(ha)) {
				if (qla2x00_verbose)
					printk("scsi(%ld): Verifying chip...\n",
						ha->host_no);

				if (!(status = qla2x00_chip_diag(ha)))
					status = qla2x00_setup_chip(ha);

				if (!status) {
					DEBUG2(printk("scsi(%ld): Chip verified "
							"and RISC loaded...\n",
							ha->host_no));
				}
			}
			if (!status && !(status = qla2x00_init_rings(ha))) {

				/* dg - 7/3/1999
				 *
				 * Wait for a successful LIP up to a maximum 
				 * of (in seconds): RISC login timeout value,
				 * RISC retry count value, and port down retry
				 * value OR a minimum of 4 seconds OR If no 
				 * cable, only 5 seconds.
				 */
				DEBUG2(printk("qla2x00_init_rings OK, call "
						"qla2x00_fw_ready...\n");)

check_fw_ready_again:
				if (!qla2x00_fw_ready(ha)) {
					clear_bit(RESET_MARKER_NEEDED,
							&ha->dpc_flags);
					clear_bit(COMMAND_WAIT_NEEDED,
							&ha->dpc_flags);

					/*
					 * Go setup flash database devices 
					 * with proper Loop ID's.
					 */
					do {
						clear_bit(LOOP_RESYNC_NEEDED,
								&ha->dpc_flags);
						status = qla2x00_configure_loop(ha);

						if (test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ){
							clear_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
							DEBUG2(printk("%s(%ld) need to reset hba dpc_flags=0x%lx\n", 
							    	__func__, ha->host_no, ha->dpc_flags);)
							restart_risc = 1;
							break;
						}

						/* if loop state change while we were discoverying devices
							then wait for LIP to complete */
						 /* If (atomic_read(&ha->loop_state) == LOOP_DOWN && retry--) { */
						if (atomic_read(&ha->loop_state) != LOOP_READY && retry--) {
							clear_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags);
							DEBUG2(printk("%s(%ld) LOOP NOT_READY  dpc_flags=0x%lx retry=%d\n",
							    	__func__, ha->host_no, ha->dpc_flags, retry);)
							goto check_fw_ready_again;
						}

#if 0  /* i'm not sure this is needed anymore */
						/*
						 * Temp code: delay a while for certain
						 * slower devices to become ready.
						 */
						for ((wait_device = jiffies + HZ);
							!time_after_eq(jiffies,wait_device);) {
							qla2x00_check_fabric_devices(ha);

							set_current_state(TASK_UNINTERRUPTIBLE);
							schedule_timeout(5);
						}
#endif

					} while (!atomic_read(&ha->loop_down_timer) && 
						retry &&
						(test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)) );

				}
#if 0
				if (ha->flags.update_config_needed) {
					struct qla2x00_additional_firmware_options additional_firmware_options;

					*((uint16_t *) &additional_firmware_options) =
					    le16_to_cpu(*((uint16_t *) &ha->init_cb->additional_firmware_options));

					additional_firmware_options.connection_options = ha->operating_mode;

					*((uint16_t *) &ha->init_cb->additional_firmware_options) =
					    cpu_to_le16( *((uint16_t *) &additional_firmware_options));

					restart_risc = 1;
				}
#endif

				if (ha->mem_err) {
					DEBUG(printk("%s(%ld) mem _err failed\n", __func__, ha->host_no);)
					restart_risc = 1;
					ha->mem_err = 0;
				}
				isp_init = 1;

			}
		} while (restart_risc && retry--);

		if (isp_init) {
			clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);
			ha->marker_needed = 1;
			qla2x00_marker(ha, 0, 0, MK_SYNC_ALL);
			ha->marker_needed = 0;

			ha->flags.online = TRUE;

			/* Enable target response to SCSI bus. */
			if (ha->flags.enable_target_mode)
				qla2x00_enable_lun(ha);
		}

	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("%s(): **** FAILED ****\n", __func__);
#endif
	if (status)
		DEBUG2(printk("%s(): **** FAILED ****\n", __func__);)

	LEAVE(__func__);

	return (status);
}

/*
* ISP Firmware Test
*      Checks if present version of RISC firmware is older than
*      driver firmware.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = firmware does not need to be loaded.
*/
STATIC uint8_t
qla2x00_isp_firmware(scsi_qla_host_t *ha)
{
	uint8_t  status = 1; /* assume loading risc code */

	ENTER(__func__);
	if (ha->flags.disable_risc_code_load ) {
		/* Verify checksum of loaded RISC code. */
		status = qla2x00_verify_checksum(ha,
				check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)
					?  RISC_SADDRESS :
				*QLBoardTbl_fc[ha->devnum].fwinfo->lfwstart);
		printk(KERN_INFO "%s RISC CODE NOT loaded\n",__func__);

	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("%s: **** Load RISC code ****\n", __func__);
#endif

	LEAVE(__func__);

	return (status);
}


STATIC int
qla2x00_iospace_config(scsi_qla_host_t *ha)
{
	unsigned long	pio, pio_len, pio_flags;
	unsigned long	mmio, mmio_len, mmio_flags;

	pio = pci_resource_start(ha->pdev, 0);
	pio_len = pci_resource_len(ha->pdev, 0);
	pio_flags = pci_resource_flags(ha->pdev, 0);

	mmio = pci_resource_start(ha->pdev, 1);
	mmio_len = pci_resource_len(ha->pdev, 1);
	mmio_flags = pci_resource_flags(ha->pdev, 1);

#if MEMORY_MAPPED_IO
	if (!(mmio_flags & IORESOURCE_MEM)) {
		printk(KERN_ERR
		    "scsi(%ld): region #0 not an MMIO resource (%s), "
		    "aborting\n",
		    ha->host_no, ha->pdev->slot_name);
		goto iospace_error_exit;
	}
	if (mmio_len < MIN_IOBASE_LEN) {
		printk(KERN_ERR
		    "scsi(%ld): Invalid PCI mem region size (%s), aborting\n",
		    ha->host_no, ha->pdev->slot_name);
		goto iospace_error_exit;
	}
#else
	if (!(pio_flags & IORESOURCE_IO)) {
		printk(KERN_ERR
		    "scsi(%ld): region #0 not a PIO resource (%s), aborting\n",
		    ha->host_no, ha->pdev->slot_name);
		goto iospace_error_exit;
	}
	if (pio_len < MIN_IOBASE_LEN) {
		printk(KERN_ERR
		    "scsi(%ld): Invalid PCI I/O region size (%s), aborting\n",
		    ha->host_no, ha->pdev->slot_name);
		goto iospace_error_exit;
	}
#endif

	if (pci_request_regions(ha->pdev, DRIVER_NAME)) {
		printk(KERN_WARNING
		    "scsi(%ld): Failed to reserve PIO/MMIO regions (%s)\n", 
		    ha->host_no, ha->pdev->slot_name);

		goto iospace_error_exit;
	}

	/* Assume PIO */
	ha->iobase = (device_reg_t *) pio;
	ha->pio_address = pio;
	ha->pio_length = pio_len;
	ha->mmio_address = NULL;
#if MEMORY_MAPPED_IO
	ha->mmio_address = ioremap(mmio, MIN_IOBASE_LEN);
	if (!ha->mmio_address) {
		printk(KERN_ERR
		    "scsi(%ld): cannot remap MMIO (%s), aborting\n",
		    ha->host_no, ha->pdev->slot_name);

		pci_release_regions(ha->pdev);
		goto iospace_error_exit;
	}
	ha->iobase = (device_reg_t *) ha->mmio_address;
	ha->mmio_length = mmio_len;
#endif

	return (0);

iospace_error_exit:
	return (-ENOMEM);
}


/*
* (08/05/99)
*
* PCI configuration
*      Setup device PCI configuration registers.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_pci_config(scsi_qla_host_t *ha)
{
	uint8_t		status = 1;
	int		pci_ret;
	uint16_t	buf_wd;
	uint32_t	buf_dwd;

	ENTER(__func__);

	/* 
	 * Turn on PCI master; for system BIOSes that don't turn it on by
	 * default.
	 */

	pci_set_master(ha->pdev);
	pci_read_config_word(ha->pdev, PCI_REVISION_ID, &buf_wd);
	ha->revision = buf_wd;

#if !MEMORY_MAPPED_IO
	if (ha->iobase)
		return 0;
#endif
	do { /* Quick exit */
		/* Get command register. */
		pci_ret = pci_read_config_word(ha->pdev, PCI_COMMAND, &buf_wd);
		if (pci_ret != PCIBIOS_SUCCESSFUL)
			break;
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			buf_wd &= ~BIT_10;
			int pcix_cmd_reg, pcie_dctl_reg;
#if 0
			struct device_reg_24xx *reg24 =
			    (struct device_reg_24xx *)ha->iobase;
			unsigned long  flags;	

			/* Get PCI bus information. */
			spin_lock_irqsave(&ha->hardware_lock, flags);
			ha->pci_attr = RD_REG_DWORD(&reg24->ctrl_status);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
#endif

			/* PCI-X -- adjust Maximum Memory Read Byte Count (2048). */
			/* Applies for ISP2422 */
			pcix_cmd_reg = pci_find_capability(ha->pdev, PCI_CAP_ID_PCIX);
			if (pcix_cmd_reg) {
				uint16_t pcix_cmd;

				pcix_cmd_reg += PCI_X_CMD;
				pci_read_config_word(ha->pdev, pcix_cmd_reg,
				    &pcix_cmd);
				pcix_cmd &= ~PCI_X_CMD_MAX_READ;
				pcix_cmd |= 0x0008;
				pci_write_config_word(ha->pdev, pcix_cmd_reg,
				    pcix_cmd);
			}

			/* PCIe -- adjust Maximum Read Request Size (2048). */
			/* Applies for ISP2432 */
			pcie_dctl_reg = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
			if (pcie_dctl_reg) {
				uint16_t pcie_dctl;

				pcie_dctl_reg += 0x08;	//PCI_EXP_DEVCTL;
				pci_read_config_word(ha->pdev, pcie_dctl_reg,
				    &pcie_dctl);
				pcie_dctl &= ~0x7000;	//~PCI_EXP_DEVCTL_READRQ;
				pcie_dctl |= 0x4000;
				pci_write_config_word(ha->pdev, pcie_dctl_reg,
				    pcie_dctl);
			}
		}

		/* PCI Specification Revision 2.3 changes */
		if (check_device_id(ha)) { 
			/* Command Register
			 *  -- Reset Interrupt Disable -- BIT_10
			 */
			buf_wd &= ~BIT_10;
		}

		pci_ret = pci_write_config_word(ha->pdev, PCI_COMMAND, buf_wd);
		if (pci_ret != PCIBIOS_SUCCESSFUL)
			printk(KERN_WARNING
				"%s(): Could not write config word.\n",
				__func__);

		/* Get expansion ROM address. */
		pci_ret = pci_read_config_dword(ha->pdev,
				PCI_ROM_ADDRESS, &buf_dwd);
		if (pci_ret != PCIBIOS_SUCCESSFUL)
			break;

		/* Reset expansion ROM address decode enable */
		buf_dwd &= ~PCI_ROM_ADDRESS_ENABLE;

		pci_ret = pci_write_config_dword(ha->pdev, 
					PCI_ROM_ADDRESS, buf_dwd);
		if (pci_ret != PCIBIOS_SUCCESSFUL) {
			printk("failed to disable PCI Rom Add Enab\n");
			break;
		}

		status = 0;
	} while (0);

	LEAVE(__func__);

	return (status);
}

/*
* qla2x00_set_cache_line
*      Sets PCI cache line parameter.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
static uint8_t
qla2x00_set_cache_line(struct scsi_qla_host * ha)
{
	unsigned char cache_size;

	ENTER(__func__);

	/* Set the cache line. */
	if (!ha->flags.set_cache_line_size_1) {
		LEAVE(__func__);
		return 0;
	}

	/* taken from drivers/net/acenic.c */
	pci_read_config_byte(ha->pdev, PCI_CACHE_LINE_SIZE, &cache_size);
	cache_size <<= 2;
	if (cache_size != SMP_CACHE_BYTES) {
		printk(KERN_INFO
			"  PCI cache line size set incorrectly (%d bytes) by "
			"BIOS/FW, ",
			cache_size);

		if (cache_size > SMP_CACHE_BYTES) {
			printk("expecting %d.\n", SMP_CACHE_BYTES);
		} else {
			printk("correcting to %d.\n", SMP_CACHE_BYTES);
			pci_write_config_byte(ha->pdev,
						PCI_CACHE_LINE_SIZE,
						SMP_CACHE_BYTES >> 2);
		}
	}

	LEAVE(__func__);

	return 0;
}

#if defined(ISP2300)
/*
* Chip diagnostics
*      Test chip for proper operation.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla24xx_chip_diag(scsi_qla_host_t *ha)
{
	struct device_reg_24xx *reg24 = (struct device_reg_24xx *)ha->iobase;
	uint32_t d2, cnt;
	unsigned long	flags = 0;
	int rval = 0;
	uint16_t        mb[5], wd;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Reset RISC */
	WRT_REG_DWORD(&reg24->ctrl_status, CSRX_DMA_SHUTDOWN | MWB_4096_BYTES);
	udelay(20);
	for (cnt = 0; cnt < 30000 ; cnt++) {
		if ((RD_REG_DWORD(&reg24->ctrl_status) & CSRX_DMA_ACTIVE) == 0)
			break;	
		udelay(10);
	}

	WRT_REG_DWORD(&reg24->ctrl_status, CSRX_ISP_SOFT_RESET |
			 CSRX_DMA_SHUTDOWN | MWB_4096_BYTES);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	udelay(100);
	d2 = RD_REG_DWORD(&reg24->ctrl_status);
	for (cnt = 6000000 ; cnt && (d2 & CSRX_ISP_SOFT_RESET); cnt--) {
		udelay(5);
		d2 = RD_REG_DWORD(&reg24->ctrl_status);
		barrier();
	}

	if(cnt  == 0) {
		rval = 1;
		printk("%s failed to recover from reset\n",__func__);
		return rval;
	}
		

	WRT_REG_DWORD(&reg24->hccr, HCCRX_SET_RISC_RESET);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	WRT_REG_DWORD(&reg24->hccr, HCCRX_REL_RISC_PAUSE);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	WRT_REG_DWORD(&reg24->hccr, HCCRX_CLR_RISC_RESET);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
	for (cnt = 6000000 ; cnt && (d2 == MBS_BUSY); cnt--) {
		udelay(5);
		d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
		barrier();
	}

	ha->fw_transfer_size = REQUEST_ENTRY_SIZE * 1024;
	if (cnt) {
		/* Check product ID of chip */
		DEBUG3(printk("%s(): Checking product ID of chip\n",
		    __func__);)
		mb[1] = RD_REG_WORD(&reg24->mailbox1);
		mb[2] = RD_REG_WORD(&reg24->mailbox2);
		mb[3] = RD_REG_WORD(&reg24->mailbox3);
		mb[4] = qla2x00_debounce_register(&reg24->mailbox4);

		if (mb[1] != PROD_ID_1 || (mb[2] != PROD_ID_2a)) {
			printk(KERN_WARNING
			    "qla2x00: Wrong product ID = "
			    "0x%x,0x%x,0x%x,0x%x\n",
			    RD_REG_WORD(&reg24->mailbox1),
			    RD_REG_WORD(&reg24->mailbox2),
			    RD_REG_WORD(&reg24->mailbox3),
			    RD_REG_WORD(&reg24->mailbox4));
			rval = 1;
		} else {
			ha->product_id[0] = mb[1];
			ha->product_id[1] = mb[2];
			ha->product_id[2] = mb[3];
			ha->product_id[3] = mb[4];
			spin_unlock_irqrestore(&ha->hardware_lock,
				    flags);

			DEBUG3(printk("%s(): Checking mailboxes.\n",
				    __func__);)

				/* Wrap Incoming Mailboxes Test. */
				rval = qla2x00_mbx_reg_test(ha);
				if (rval) {
					printk(KERN_WARNING
					    "%s(): failed mailbox send "
					    "register test\n",
					    __func__);
					DEBUG(printk("%s(): Failed mailbox "
					    "send register test\n",
					    __func__);)
				}
				spin_lock_irqsave(&ha->hardware_lock, flags);
			}

	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return rval;

}
#endif


/*
* Chip diagnostics
*      Test chip for proper operation.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_chip_diag(scsi_qla_host_t *ha)
{
	uint8_t		status = 0;
	device_reg_t	*reg;
	unsigned long	flags = 0;
#if defined(ISP2300) 
	uint16_t	buf_wd;
#endif
	uint16_t	data;
	uint32_t	cnt;
	uint16_t        mb[5];


	ENTER(__func__);

#if defined(ISP2300) 
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) 
		return qla24xx_chip_diag(ha);
#endif
	reg = ha->iobase;
	DEBUG3(printk("%s(): testing device at %lx.\n",
	    __func__,
	    (u_long)&reg->flash_address);)


	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	PCI_POSTING(&reg->ctrl_status);
	/* Delay after reset, for chip to recover. */
	udelay(20);
	data = qla2x00_debounce_register(&reg->ctrl_status);
	for (cnt = 6000000 ; cnt && (data & CSR_ISP_SOFT_RESET); cnt--) {
		udelay(5);
		data = RD_REG_WORD(&reg->ctrl_status);
		barrier();
	}

	if (cnt) {
		DEBUG3(printk("%s(): reset register cleared by chip reset\n",
		    __func__);)

#if defined(ISP2300) 
		pci_read_config_word(ha->pdev, PCI_COMMAND, &buf_wd);
		buf_wd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
		data = RD_REG_WORD(&reg->mailbox6);
		if (check_all_device_ids(ha)) 	    
			/* Enable Memory Write and Invalidate. */
			buf_wd |= PCI_COMMAND_INVALIDATE;
		else
			buf_wd &= ~PCI_COMMAND_INVALIDATE;

		pci_write_config_word(ha->pdev, PCI_COMMAND, buf_wd);
#endif
		/* Reset RISC processor. */
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
		PCI_POSTING(&reg->host_cmd);
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
		PCI_POSTING(&reg->host_cmd);

#if defined(ISP2300) 
		/* Workaround for QLA2312 PCI parity error */
		if (check_all_device_ids(ha)) { 	    
			udelay(10);
		} else {
			data = qla2x00_debounce_register(&reg->mailbox0);

			for (cnt = 6000000; cnt && (data == MBS_BUSY); cnt--) {
				udelay(5);
				data = RD_REG_WORD(&reg->mailbox0);
				barrier(); 
			}
		}
#else
		data = qla2x00_debounce_register(&reg->mailbox0);

		for (cnt = 6000000; cnt && (data == MBS_BUSY); cnt--) {
			udelay(5);
			data = RD_REG_WORD(&reg->mailbox0);
			barrier(); 
		}
#endif

		if (cnt) {
			/* Check product ID of chip */
			DEBUG3(printk("%s(): Checking product ID of chip\n",
			    __func__);)
			mb[1] = RD_REG_WORD(&reg->mailbox1);
			mb[2] = RD_REG_WORD(&reg->mailbox2);
			mb[3] = RD_REG_WORD(&reg->mailbox3);
			mb[4] = qla2x00_debounce_register(&reg->mailbox4);

			if (mb[1] != PROD_ID_1 || (mb[2]  != PROD_ID_2 &&
			    mb[2] != PROD_ID_2a) || mb[3] != PROD_ID_3) {
				printk(KERN_WARNING
				    "qla2x00: Wrong product ID = "
				    "0x%x,0x%x,0x%x,0x%x\n",
				    RD_REG_WORD(&reg->mailbox1),
				    RD_REG_WORD(&reg->mailbox2),
				    RD_REG_WORD(&reg->mailbox3),
				    RD_REG_WORD(&reg->mailbox4));
				status = 1;
			} else {

				ha->product_id[0] = mb[1];
				ha->product_id[1] = mb[2];
				ha->product_id[2] = mb[3];
				ha->product_id[3] = mb[4];
#if defined(ISP2200)
				/* Now determine if we have a 2200A board */
				if ((ha->device_id == QLA2200_DEVICE_ID ||
				    ha->device_id == QLA2200A_DEVICE_ID) &&
				    RD_REG_WORD(&reg->mailbox7) ==
				    QLA2200A_RISC_ROM_VER) {
					ha->device_id = QLA2200A_DEVICE_ID;

					DEBUG3(printk("%s(): Found QLA2200A "
					    "chip.\n",
					    __func__);)
				}
#endif
				spin_unlock_irqrestore(&ha->hardware_lock,
				    flags);

				DEBUG3(printk("%s(): Checking mailboxes.\n",
				    __func__);)

				/* Wrap Incoming Mailboxes Test. */
				status = qla2x00_mbx_reg_test(ha);
				if (status) {
					printk(KERN_WARNING
					    "%s(): failed mailbox send "
					    "register test\n",
					    __func__);
					DEBUG(printk("%s(): Failed mailbox "
					    "send register test\n",
					    __func__);)
				}
				spin_lock_irqsave(&ha->hardware_lock, flags);
			}
		} else
			status = 1;
	} else
		status = 1;

	if (status){
		DEBUG2_3(printk(KERN_INFO "%s(): **** FAILED ****\n", __func__);)
		printk("%s(): **** FAILED ****\n", __func__);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE(__func__);

	return(status);
}


/*
* Setup chip
*      Load and start RISC firmware.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_setup_chip(scsi_qla_host_t *ha)
{
	uint8_t		status = 0;
	uint16_t	cnt;
	uint16_t	*risc_code_address;
	unsigned long   risc_address;
	unsigned long	risc_code_size;
	int		num;
	struct qla_fw_info      *fw_iter;
	int i;
	uint16_t *req_ring;
	qla_boards_t   *bdp = NULL; /* to get the FW version */

	ENTER(__func__);

	/*
	 * Save active FC4 type depending on firmware support. This info is
	 * needed by ioctl cmd.
	 */
	ha->active_fc4_types = EXT_DEF_FC4_TYPE_SCSI;
#if defined(ISP2300)
	if (ha->flags.enable_ip)
		ha->active_fc4_types |= EXT_DEF_FC4_TYPE_IP;

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_setup_chip(ha);
#endif

	fw_iter = QLBoardTbl_fc[ha->devnum].fwinfo;

#if defined(FC_SCTP_SUPPORT)
	risc_address = *fw_iter->fwstart;
	if (risc_address == fw2300sctp_code01)
		ha->active_fc4_types |= EXT_DEF_FC4_TYPE_SCTP;
#endif

	/* Load firmware sequences */
	while (fw_iter->addressing != FW_INFO_ADDR_NOMORE) {
		risc_code_address = fw_iter->fwcode;
		risc_code_size = *fw_iter->fwlen;

		if (fw_iter->addressing == FW_INFO_ADDR_NORMAL) {
			risc_address = *fw_iter->fwstart;
			DEBUG7(printk(KERN_INFO "%s risc_address=%lx" 
			    "address=%d\n",__func__,risc_address,
			    fw_iter->addressing);)
		} else {
			/* Extended address */
			risc_address = *fw_iter->lfwstart;
			DEBUG7(printk(KERN_INFO "%s risc_address=%lx" 
			    "address=%d\n",__func__,risc_address,
			    fw_iter->addressing);)
		}

		num = 0;
		while (risc_code_size > 0 && !status) {
			cnt = REQUEST_ENTRY_SIZE * REQUEST_ENTRY_CNT >> 1;
#if defined(ISP2200)
			/* for 2200A set transfer size to 128 bytes */
			if (ha->device_id == QLA2200A_DEVICE_ID)
				cnt = 128 >> 1;
#endif

			if (cnt > risc_code_size)
				cnt = risc_code_size;

			DEBUG7(printk("%s(): loading risc segment@ addr %p," 
			    " number of bytes 0x%x, offset 0x%lx.\n",
			    __func__, risc_code_address, cnt, risc_address);)

			req_ring = (uint16_t *)ha->request_ring;
			for (i = 0; i < cnt; i++)
				req_ring[i] = cpu_to_le16(risc_code_address[i]);

			/*
			* Flush written firmware to the ha->request_ring 
			* buffer before DMA */
			flush_cache_all();
			if (fw_iter->addressing == FW_INFO_ADDR_NORMAL) {
				status = qla2x00_load_ram(ha,
				    ha->request_dma, risc_address, cnt);
			} else {
				status = qla2x00_load_ram_ext(ha,
				    ha->request_dma, risc_address, cnt);
			}

			if (status) {
				qla2x00_dump_regs(ha->host);
				printk(KERN_WARNING
				    "qla2x00: [ERROR] Failed to load segment "
				    "%d of FW\n", num);
				DEBUG2(printk("%s(): Failed to load segment %d" 
				    " of FW\n", __func__, num);)
					break;
			}

			risc_address += cnt;
			risc_code_size -= cnt;
			risc_code_address += cnt;
			num++;
		}
		/* Next firmware sequence */
		fw_iter++;
	}

	/* Verify checksum of loaded RISC code. */
	if (!status) {
		DEBUG2(printk("%s(): Verifying Check Sum of loaded RISC code.\n",
				__func__);)

		status = (uint8_t)qla2x00_verify_checksum(ha,
				*QLBoardTbl_fc[ha->devnum].fwinfo->fwstart);

		if (status == QL_STATUS_SUCCESS) {
			/* Start firmware execution. */
			DEBUG2(printk("%s(): CS Ok, Start firmware running\n",
					__func__);)
			status = qla2x00_execute_fw(ha,
				   *QLBoardTbl_fc[ha->devnum].fwinfo->fwstart);
		}
#if defined(QL_DEBUG_LEVEL_2)
		else {
			printk(KERN_INFO
				"%s(): ISP FW Failed Check Sum\n", __func__);
		}
#endif
	}

	if (status) {
		DEBUG2_3(printk(KERN_INFO "%s(): **** FAILED ****\n", __func__);)
	} else {

		/* Firmware is good, for 2G copy the FW version from bdp to 
		 *  ha->fw_version
		 */ 
		bdp = &QLBoardTbl_fc[ha->devnum];
		if (bdp != NULL) {
			ha->fw_version[0] = bdp->fwver[0];
			ha->fw_version[1] = bdp->fwver[1];
			ha->fw_version[2] = bdp->fwver[2];
		} else {
			DEBUG3(printk("%s(): Failed to get FW version\n", __func__);)
		}
		DEBUG3(printk("%s(): Returning Good Status\n", __func__);)
	}

	return (status);
}

/*
 * qla2x00_init_response_q_entries
 *      Initializes response queue entries.
 *   
 * Input:
 *      ha    = adapter block pointer.
 * 
 * Returns:
 *      None.
 */
STATIC void
qla2x00_init_response_q_entries(scsi_qla_host_t *ha)
{
	response_t *pkt;
	uint16_t cnt;

	pkt = ha->response_ring_ptr;
	for (cnt = 0; cnt < RESPONSE_ENTRY_CNT; cnt++){
		pkt->signature = RESPONSE_PROCESSED;
		pkt++;
	}

}

static void
qla24xx_update_fw_options(scsi_qla_host_t *ha)
{
	int rval;

	/* Update Serial Link options. */
      if ((le16_to_cpu(ha->fw_seriallink_options24[0]) & BIT_0) == 0)
	      return;

      rval = qla2x00_set_serdes_params(ha,
		le16_to_cpu(ha->fw_seriallink_options24[1]),
		le16_to_cpu(ha->fw_seriallink_options24[2]),
		le16_to_cpu(ha->fw_seriallink_options24[3]));

	if (rval != QLA2X00_SUCCESS) {
		printk(KERN_WARNING "%s(%ld)Unable to update Serial"
			" Link options (%x).\n", __func__, ha->host_no, rval);
	}
}

static void
qla2x00_update_fw_options(scsi_qla_host_t *ha)
{
	uint16_t	mb_stat = 0;
#if defined(ISP2300)
	/* Setup seriallink options */
	uint16_t	opt10 = 0; 
	uint16_t	opt11 = 0;
#endif
	qla2x00_get_firmware_options(ha,
	    &ha->fw_options1, &ha->fw_options2, &ha->fw_options3,
	    &mb_stat);

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_update_fw_options(ha);

#if defined(ISP2300)
	DEBUG3(printk("%s(%ld): Serial link options:\n",
	    __func__, ha->host_no);)
	DEBUG3(qla2x00_dump_buffer(
	    (uint8_t *)&ha->fw_seriallink_options,
	    sizeof(ha->fw_seriallink_options));)

	ha->fw_options1 &= ~BIT_8;
	if (ha->fw_seriallink_options.output_enable) {
		ha->fw_options1 |= BIT_8;

		/* 1G settings */
		opt10 = (ha->fw_seriallink_options.output_emphasis_1g << 14) |
		    (ha->fw_seriallink_options.output_swing_1g << 8);
		if (ha->device_id == QLA2300_DEVICE_ID || 
			ha->device_id == QLA2312_DEVICE_ID ||
			ha->device_id == QLA6312_DEVICE_ID)  {

			 if (ha->fw_seriallink_options.rx_sens_1g == 0)
				ha->fw_seriallink_options.rx_sens_1g = 0x3;
                         opt10 |= (ha->fw_seriallink_options.tx_sens_1g << 4) | 
					ha->fw_seriallink_options.rx_sens_1g;

		} else if (check_device_id(ha)) {
                        opt10 |= BIT_5 |
                            ((ha->fw_seriallink_options.rx_sens_1g & 
				(BIT_1 | BIT_0)) << 2) |
                            (ha->fw_seriallink_options.tx_sens_1g &
				 (BIT_1 | BIT_0));
		}

		/* 2G settings */
		opt11 = (ha->fw_seriallink_options.output_emphasis_2g << 14) |
		    (ha->fw_seriallink_options.output_swing_2g << 8);
		if (ha->device_id == QLA2300_DEVICE_ID || 
			ha->device_id == QLA2312_DEVICE_ID ||
			ha->device_id == QLA6312_DEVICE_ID)  {
			 if (ha->fw_seriallink_options.rx_sens_2g == 0)
				ha->fw_seriallink_options.rx_sens_2g = 0x3;
                         opt11 |= (ha->fw_seriallink_options.tx_sens_2g << 4) | 
					ha->fw_seriallink_options.rx_sens_2g;
		} else if (check_device_id(ha)) {
                        opt11 |= BIT_5 |
                            ((ha->fw_seriallink_options.rx_sens_2g & 
				(BIT_1 | BIT_0)) << 2) |
                            (ha->fw_seriallink_options.tx_sens_2g &
				 (BIT_1 | BIT_0));
		}
	}

	/* TAPE FIX */
        /*  Return command IOCBs without waiting for an ABTS to complete. */
	ha->fw_options3 |= BIT_13;

	/* Cluster fix, so new request don't get returned 
		with reservation conflict before TM func completes */
        /*  Wait for FCP_RSP before completing the mbox Task Mngt func */
	ha->fw_options3 |= BIT_10;

	 /* LED scheme. */
        if (ha->flags.enable_led_scheme)
                ha->fw_options2 |= BIT_12;

	qla2x00_set_firmware_options(ha, ha->fw_options1,
	    ha->fw_options2, ha->fw_options3, opt10, opt11,
	    &mb_stat);
#endif

}
/*
* qla2x00_init_rings
*      Initializes firmware.
*
*      Beginning of request ring has initialization control block
*      already built by nvram config routine.
*
* Input:
*      ha                = adapter block pointer.
*      ha->request_ring  = request ring virtual address
*      ha->response_ring = response ring virtual address
*      ha->request_dma   = request ring physical address
*      ha->response_dma  = response ring physical address
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_init_rings(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	uint8_t  status;
	int cnt;
	device_reg_t *reg = ha->iobase;

	ENTER(__func__);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Clear outstanding commands array. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS; cnt++)
		ha->outstanding_cmds[cnt] = 0;

	ha->current_outstanding_cmd = 0;

	/* Clear RSCN queue. */
	ha->rscn_in_ptr = 0;
	ha->rscn_out_ptr = 0;

	/* Initialize firmware. */
	ha->request_ring_ptr  = ha->request_ring;
	ha->req_ring_index    = 0;
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		ha->req_q_cnt = REQUEST_ENTRY_CNT_24XX;  
	else 
		ha->req_q_cnt = REQUEST_ENTRY_CNT;
	ha->response_ring_ptr = ha->response_ring;
	ha->rsp_ring_index    = 0;

	/* Initialize response queue entries */
	qla2x00_init_response_q_entries(ha);

#if defined(ISP2300) 
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                struct device_reg_24xx * reg24;
                reg24 = (struct device_reg_24xx *) ha->iobase;

                WRT_REG_DWORD(&reg24->req_q_in, 0);
                WRT_REG_DWORD(&reg24->req_q_out, 0);
                WRT_REG_DWORD(&reg24->rsp_q_in, 0);
                WRT_REG_DWORD(&reg24->rsp_q_out, 0);
	} else {
		WRT_REG_WORD(&reg->req_q_in, 0);
		WRT_REG_WORD(&reg->req_q_out, 0);
		WRT_REG_WORD(&reg->rsp_q_in, 0);
		WRT_REG_WORD(&reg->rsp_q_out, 0);
	}
	PCI_POSTING(&reg->rsp_q_out);
#else
	WRT_REG_WORD(&reg->mailbox4, 0);
	WRT_REG_WORD(&reg->mailbox4, 0);
	WRT_REG_WORD(&reg->mailbox5, 0);
	WRT_REG_WORD(&reg->mailbox5, 0);
	PCI_POSTING(&reg->mailbox5);
#endif

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	qla2x00_update_fw_options(ha);

	DEBUG(printk("%s(%ld): issue init firmware.\n",
			__func__,
			ha->host_no);)
	status = qla2x00_init_firmware(ha, sizeof(struct init_cb));

	if (status) {
		DEBUG2_3(printk(KERN_INFO "%s(%ld): **** FAILED ****.\n",
				__func__, ha->host_no);)
	} else {
		DEBUG3(printk("%s(%ld): exiting normally.\n", __func__,
		    ha->host_no));
	}


	return (status);
}

/*
* qla2x00_fw_ready
*      Waits for firmware ready.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_fw_ready(scsi_qla_host_t *ha)
{
	uint8_t  status = 0;
	uint8_t  loop_forever = 1;
	unsigned long wtime, mtime;
	uint16_t min_wait; /* minimum wait time if loop is down */
	uint16_t wait_time;/* wait time if loop is becoming ready */
	uint16_t pause_time;
	uint16_t fw_state;

	ENTER(__func__);
	if (!ha->init_done)
		min_wait = 20;		/* 60 seconds for loop down. */
	else
		min_wait = 20;		/* 20 seconds for loop down. */

	/*
	 * Firmware should take at most one RATOV to login, plus 5 seconds for
	 * our own processing.
	 */
	if ((wait_time = (ha->retry_count*ha->login_timeout) + 5) < min_wait) {
		wait_time = min_wait;
	}
	pause_time = 1000;	/* 1000 usec */

	/* min wait time if loop down */
	mtime = jiffies + (min_wait * HZ);

	/* wait time before firmware ready */
	wtime = jiffies + (wait_time * HZ);

	/* Wait for ISP to finish LIP */
	if (!qla2x00_quiet)
		printk(KERN_INFO
			"scsi(%ld): Waiting for LIP to complete...\n",
			ha->host_no);

	DEBUG3(printk("scsi(%ld): Waiting for LIP to complete...\n",
			ha->host_no);)
	printk("scsi(%ld): Waiting for LIP to complete...\n",
			ha->host_no);

	do {
		status = qla2x00_get_firmware_state(ha, &fw_state);

		if (status == QL_STATUS_SUCCESS) {
			if (fw_state < FSTATE_LOSS_OF_SYNC) {
				ha->device_flags &= ~DFLG_NO_CABLE;
			}
			if (fw_state == FSTATE_READY) {
				qla2x00_get_retry_cnt(ha, 
						&ha->retry_count,
						&ha->login_timeout);
				status = QL_STATUS_SUCCESS;

				DEBUG(printk("%s(%ld): F/W Ready - OK \n",
						__func__,
						ha->host_no);)

				break;
			}

			status = QL_STATUS_ERROR;

			if (atomic_read(&ha->loop_down_timer) &&
			    ((fw_state >= FSTATE_LOSS_OF_SYNC) ||
				(fw_state == FSTATE_WAIT_AL_PA))) {
				/* Loop down. Timeout on min_wait 
				 * for states other than Wait for
				 * Login. 
				 */	
				if (time_after_eq(jiffies, mtime)) {
					printk(KERN_INFO
						"scsi(%ld): Cable is "
						"unplugged...\n",
						ha->host_no);
					ha->device_flags |= DFLG_NO_CABLE;
					break;
				}
			}
		} else {
			/* Mailbox cmd failed. Timeout on min_wait. */
			if (time_after_eq(jiffies, mtime))
				break;
		}

		if (time_after_eq(jiffies, wtime))
			break;

		/* Delay for a while */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / HZ);

		DEBUG3(printk("%s(): fw_state=%x curr time=%lx.\n",
				__func__,
				fw_state,
				jiffies);)
	} while (loop_forever);

	DEBUG(printk("%s(%ld): fw_state=%x curr time=%lx.\n", __func__,
			ha->host_no, fw_state, jiffies);)

	if (status) {
		DEBUG2_3(printk(KERN_INFO "%s(%ld): **** FAILED ****.\n",
					__func__,
					ha->host_no);)
	} else {
		DEBUG3(printk("%s(%ld): exiting normally.\n",
					__func__,
					ha->host_no);)
	}

	return (status);
}

/*
*  qla2x00_configure_hba
*      Setup adapter context.
*
* Input:
*      ha = adapter state pointer.
*
* Returns:
*      0 = success
*
* Context:
*      Kernel context.
*/
STATIC uint8_t
qla2x00_configure_hba(scsi_qla_host_t *ha)
{
	uint8_t       rval;
	uint16_t      loop_id;
	uint16_t      topo;
	uint8_t       al_pa;
	uint8_t       area;
	uint8_t       domain;
	char		connect_type[22];

	ENTER(__func__);

	/* Get host addresses. */
	rval = qla2x00_get_adapter_id(ha,
			&loop_id, &al_pa, &area, &domain, &topo);
	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2(printk(KERN_WARNING "%s(%ld): ERROR Get host loop ID.\n",
			__func__, ha->host_no);)
		if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
		    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		    (atomic_read(&ha->loop_state) == LOOP_DOWN) ||
		    atomic_read(&ha->loop_down_timer) ||
		    (rval == QL_STATUS_MBX_CMD_ERR && loop_id == 0x7)) {
			DEBUG2(printk("%s(%ld) Loop is in a transition state\n",
			    	__func__, ha->host_no);)
		} else {
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		}
		return (rval);
	}

	if (topo == 4) {
		printk(KERN_INFO
			"scsi(%ld): Cannot get topology - retrying.\n",
			ha->host_no);
		return (QL_STATUS_ERROR);
	}

	ha->loop_id = loop_id;

#if defined(ISP2100)
	/* Make sure 2100 only has loop, in case of any firmware bug. */
	topo = 0;
#endif

	/* initialize */
	ha->min_external_loopid = SNS_FIRST_LOOP_ID;
	ha->operating_mode = LOOP;

	switch (topo) {
		case 0:
			DEBUG3(printk("qla2x00(%ld): HBA in NL topology.\n",
					ha->host_no);)
			ha->current_topology = ISP_CFG_NL;
			strcpy(connect_type, "(Loop)");
			break;

		case 1:
			DEBUG3(printk("qla2x00(%ld): HBA in FL topology.\n",
					ha->host_no);)
			ha->current_topology = ISP_CFG_FL;
			strcpy(connect_type, "(FL_Port)");
			break;

		case 2:
			DEBUG3(printk("qla2x00(%ld): HBA in N P2P topology.\n",
					ha->host_no);)
			ha->operating_mode = P2P;
			ha->current_topology = ISP_CFG_N;
			strcpy(connect_type, "(N_Port-to-N_Port)");
			break;

		case 3:
			DEBUG3(printk("qla2x00(%ld): HBA in F P2P topology.\n",
					ha->host_no);)
			ha->operating_mode = P2P;
			ha->current_topology = ISP_CFG_F;
			strcpy(connect_type, "(F_Port)");
			break;

		default:
			DEBUG3(printk("qla2x00(%ld): HBA in unknown "
					"topology %x. Using NL.\n", 
					ha->host_no, topo);)
			ha->current_topology = ISP_CFG_NL;
			strcpy(connect_type, "(Loop)");
			break;
	}

	/* Save Host port and loop ID. */
	/* byte order - Big Endian */
	ha->d_id.b.domain = domain;
	ha->d_id.b.area = area;
	ha->d_id.b.al_pa = al_pa;

	if (!qla2x00_quiet)
		printk(KERN_INFO
			"scsi(%ld): Topology - %s, Host Loop address 0x%x\n",
			ha->host_no, connect_type, ha->loop_id);

	if (rval != 0) {
		/* Empty */
		DEBUG2_3(printk(KERN_INFO "%s(%ld): FAILED.\n", __func__, ha->host_no);)
	} else {
		/* Empty */
		DEBUG3(printk("%s(%ld): exiting normally.\n",
				__func__,
				ha->host_no);)
	}

	return(rval);
}
#if defined(ISP2300)
static void
qla2x00_alias_hba(scsi_qla_host_t *ha)
{
	int rval;
	int instance;
	uint8_t *propbuf;
	init_cb_t *icb;
#if 0
	struct init_cb_24xx *icb24;
#endif
	uint8_t pn[WWN_SIZE];

	if (!ql2xdevconf)
		return;

	propbuf = kmalloc(LINESIZE, GFP_KERNEL);
	if (!propbuf)
		return;

	icb = (init_cb_t *) ha->init_cb;
#if 0
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		icb = (struct init_cb_24xx *)ha->init_cb;
#endif

	for (instance = 0; instance < MAX_HOST_COUNT; instance++) {
		sprintf(propbuf, "scsi-qla%d-adapter-port", instance);
		rval = qla2x00_get_prop_16chars(ha, propbuf, pn, ql2xdevconf);
		if (rval)
			break;

		/* Portname match? */
		if (memcmp(icb->port_name, pn, WWN_SIZE))
			continue;

		/* Check for portname alias. */
		sprintf(propbuf, "scsi-qla%d-aliased-port", instance);
		rval = qla2x00_get_prop_16chars(ha, propbuf, pn, ql2xdevconf);
		if (rval)
			break;

		/* Use aliased portname. */
		if (qla2x00_verbose)
			printk("scsi(%ld): Using aliased WWPN "
			    "%02x%02x%02x%02x%02x%02x%02x%02x -> "
			    "%02x%02x%02x%02x%02x%02x%02x%02x\n", ha->host_no,
			    icb->port_name[0], icb->port_name[1],
			    icb->port_name[2], icb->port_name[3],
			    icb->port_name[4], icb->port_name[5],
			    icb->port_name[6], icb->port_name[7],
			    pn[0], pn[1], pn[2], pn[3], pn[4], pn[5], pn[6],
			    pn[7]);

		memcpy(icb->port_name, pn, WWN_SIZE);

		break;
	}
	kfree(propbuf);
}
#endif

#if defined(ISP2100)
/*
* NVRAM configuration for 2100.
*
* Input:
*      ha                = adapter block pointer.
*      ha->request_ring  = request ring virtual address
*      ha->response_ring = response ring virtual address
*      ha->request_dma   = request ring physical address
*      ha->response_dma  = response ring physical address
*
* Output:
*      initialization control block in response_ring
*      host adapters parameters in host adapter block
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2100_nvram_config(scsi_qla_host_t *ha)
{
	uint8_t   status = 0;
	uint16_t  cnt;
	//init_cb_t *icb   = ha->init_cb;
	init_cb_t *icb = (init_cb_t *)ha->init_cb;
	nvram21_t *nv    = (nvram21_t *)ha->request_ring;
	uint16_t  *wptr  = (uint16_t *)ha->request_ring;
	uint8_t   chksum = 0;

	ENTER(__func__);

	/* Verify valid NVRAM checksum. */
	for (cnt = 0; cnt < sizeof(nvram21_t)/2; cnt++) {
		*wptr = qla2x00_get_nvram_word(ha, cnt);
		chksum += (uint8_t)*wptr;
		chksum += (uint8_t)(*wptr >> 8);
		wptr++;
	}

#if  DEBUG_PRINT_NVRAM
	printk("%s(): Contents of NVRAM\n", __func__);
	qla2x00_dump_buffer((uint8_t *)ha->request_ring, sizeof(nvram21_t));
#endif

	/* Bad NVRAM data, set defaults parameters. */
	if (chksum ||
		nv->id[0] != 'I' ||
		nv->id[1] != 'S' ||
		nv->id[2] != 'P' ||
		nv->id[3] != ' ' ||
		nv->nvram_version < 1) {

		/* Reset NVRAM data. */
		DEBUG(printk("Using defaults for NVRAM: \n"));
		DEBUG(printk("checksum=0x%x, Id=%c, version=0x%x\n",
				chksum,
				nv->id[0],
				nv->nvram_version));

		memset(nv, 0, sizeof(nvram21_t));

		/*
		 * Set default initialization control block.
		 */
		nv->parameter_block_version = ICB_VERSION;
		nv->firmware_options.enable_fairness = 1;
		nv->firmware_options.enable_fast_posting = 1;
		nv->firmware_options.enable_full_login_on_lip = 1;

		nv->frame_payload_size  = 1024;
		nv->max_iocb_allocation = 256;
		nv->execution_throttle  = 16;
		nv->retry_count         = 8;
		nv->retry_delay         = 1;
		nv->node_name[0]        = 32;
		nv->node_name[3]        = 224;
		nv->node_name[4]        = 139;
		nv->login_timeout       = 4;

		/*
		 * Set default host adapter parameters
		 */
		nv->host_p.enable_lip_full_login = 1;
		nv->reset_delay = 5;
		nv->port_down_retry_count = 8;
		nv->maximum_luns_per_target = 8;
		status = 1;
	}
	/* Model Number */
        sprintf(ha->model_number,"QLA2100");

	/*
	 * Copy over NVRAM RISC parameter block to initialization control
	 * block.
	 */
	cnt = (uint8_t *)&nv->host_p - (uint8_t *)&nv->parameter_block_version;
	memcpy((uint8_t *)icb,
			(uint8_t *)&nv->parameter_block_version, cnt);

	/* HBA node name 0 correction */
	for (cnt=0 ; cnt < 8 ; cnt++) {
		if (icb->node_name[cnt] != 0)
			break;
	}
	if (cnt == 8) {
		for (cnt= 0 ; cnt < 8 ; cnt++)
			icb->node_name[cnt] = icb->port_name[cnt];
		icb->node_name[0] = icb->node_name[0] & ~BIT_0;
		icb->port_name[0] = icb->port_name[0] |  BIT_0;
	}

	/*
	 * Setup driver firmware options.
	 */
	icb->firmware_options.enable_target_mode       = 0;
	icb->firmware_options.disable_initiator_mode   = 0;
	icb->firmware_options.enable_port_update_event = 1;
	icb->firmware_options.enable_full_login_on_lip = 1;

	/*
	 * Set host adapter parameters
	 */
	ha->flags.enable_target_mode = icb->firmware_options.enable_target_mode;
	ha->flags.disable_luns            = nv->host_p.disable_luns;
	ha->flags.disable_risc_code_load  = nv->host_p.disable_risc_code_load;
	ha->flags.set_cache_line_size_1   = nv->host_p.set_cache_line_size_1;

	if (nv->host_p.enable_extended_logging)
		extended_error_logging = 1 ;

	ha->flags.link_down_error_enable  = 1;

	ha->flags.enable_lip_reset        = nv->host_p.enable_lip_reset;
	ha->flags.enable_lip_full_login   = nv->host_p.enable_lip_full_login;
	ha->flags.enable_target_reset     = nv->host_p.enable_target_reset;
	ha->flags.enable_flash_db_update  = nv->host_p.enable_database_storage;

	/* new for IOCTL support of APIs */
	memcpy(ha->node_name, icb->node_name, WWN_SIZE);
	memcpy(ha->port_name, icb->port_name, WWN_SIZE);
	ha->nvram_version = nv->nvram_version;
	/* empty data for QLA2100s OEM stuff */
	for (cnt= 0 ; cnt < 8 ; cnt++) {
		ha->oem_fru[cnt]    = 0; 
		ha->oem_ec[cnt]     = 0; 
	}

	ha->hiwat               = icb->iocb_allocation;
	ha->execution_throttle  = nv->execution_throttle;

	ha->retry_count         = nv->retry_count;
	ha->login_timeout       = nv->login_timeout;
	/* Set minimum login_timeout to 4 seconds. */
	if (ha->login_timeout < 4)
		ha->login_timeout = 4;
	ha->port_down_retry_count = nv->port_down_retry_count;
	ha->minimum_timeout = (ha->login_timeout * ha->retry_count)
				+ ha->port_down_retry_count;
	ha->loop_reset_delay = nv->reset_delay;

	/* Will get the value from nvram. */
	ha->loop_down_timeout     = LOOP_DOWN_TIMEOUT;
	ha->loop_down_abort_time  = LOOP_DOWN_TIME - ha->loop_down_timeout;

	/* save HBA serial number */
	ha->serial0 = nv->node_name[5];
	ha->serial1 = nv->node_name[6];
	ha->serial2 = nv->node_name[7];

	ha->max_probe_luns = le16_to_cpu(nv->maximum_luns_per_target);
	if (ha->max_probe_luns == 0)
		ha->max_probe_luns = MIN_LUNS;

	/* High-water mark of IOCBs */
	ha->iocb_hiwat = MAX_IOCBS_AVAILBALE;

#if  USE_BIOS_MAX_LUNS
	if (!nv->maximum_luns_per_target)
		ha->max_luns = MAX_LUNS-1;
	else
		ha->max_luns = nv->maximum_luns_per_target;
#else
	ha->max_luns = MAX_LUNS-1;
#endif

	ha->binding_type = Bind;
	if ((ha->binding_type != BIND_BY_PORT_NAME) &&
		(ha->binding_type != BIND_BY_PORT_ID)) {

		printk(KERN_WARNING
			"scsi(%ld): Invalid binding type specified "
			"(%d), defaulting to BIND_BY_PORT_NAME!!!\n",
			ha->host_no,
			ha->binding_type);
		ha->binding_type = BIND_BY_PORT_NAME;
	}

	/*
	 * Setup ring parameters in initialization control block
	 */
	icb->request_q_outpointer  = 0;
	icb->response_q_inpointer  = 0;
	icb->request_q_length      = REQUEST_ENTRY_CNT;
	icb->response_q_length     = RESPONSE_ENTRY_CNT;
	icb->request_q_address[0]  = LSD(ha->request_dma);
	icb->request_q_address[1]  = MSD(ha->request_dma);
	icb->response_q_address[0] = LSD(ha->response_dma);
	icb->response_q_address[1] = MSD(ha->response_dma);


	ha->qfull_retry_count = qfull_retry_count;
	ha->qfull_retry_delay = qfull_retry_delay;

	ha->flags.nvram_config_done = 1;

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk(KERN_WARNING
			"%s(): **** FAILED ****\n", __func__);
#endif

	LEAVE(__func__);

	return(status);
}
#else


/*
* NVRAM configuration for the 2200/2300/2312
*
* Input:
*      ha                = adapter block pointer.
*      ha->request_ring  = request ring virtual address
*      ha->response_ring = response ring virtual address
*      ha->request_dma   = request ring physical address
*      ha->response_dma  = response ring physical address
*
* Output:
*      initialization control block in response_ring
*      host adapters parameters in host adapter block
*
* Returns:
*      0 = success.
*/
STATIC uint8_t
qla2x00_nvram_config(scsi_qla_host_t *ha)
{
#if defined(ISP2300) 
	device_reg_t *reg = ha->iobase;
	uint16_t  data;
#endif
	struct qla2xxx_host_p host_p;
	struct qla2x00_firmware_options firmware_options;
	struct qla2x00_additional_firmware_options additional_firmware_options;
	struct qla2x00_seriallink_firmware_options serial_options; 
	struct qla2x00_special_options special_options; 

	uint8_t   status = 0;
	uint8_t   chksum = 0;
	uint16_t  cnt, base;
	uint8_t   *dptr1, *dptr2;
	init_cb_t *icb   = &ha->init_cb->cb.icb;
	nvram22_t *nv    = (nvram22_t *)ha->request_ring;
	uint16_t  *wptr  = (uint16_t *)ha->request_ring;

	ENTER(__func__);

#if defined(ISP2300) 
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_nvram_config(ha);

		if (check_all_device_ids(ha)) {
			data = RD_REG_WORD(&reg->ctrl_status);
			if ((data >> 14) == 1)
				base = 0x80;
			else
				base = 0;
			data = RD_REG_WORD(&reg->nvram);
			while (data & NV_BUSY) {
				UDELAY(100);
				data = RD_REG_WORD(&reg->nvram);
			}

			/* Lock resource */
			WRT_REG_WORD(&reg->host_semaphore, 0x1);
			UDELAY(5);
			data = RD_REG_WORD(&reg->host_semaphore);
			while ((data & BIT_0) == 0) {
				/* Lock failed */
				UDELAY(100);
				WRT_REG_WORD(&reg->host_semaphore, 0x1);
				UDELAY(5);
				data = RD_REG_WORD(&reg->host_semaphore);
			}
		} else
			base = 0;
#else
		base = 0;
#endif
		/* Verify valid NVRAM checksum. */
		for (cnt = 0; cnt < sizeof(nvram22_t)/2; cnt++) {
	 	 	*wptr = cpu_to_le16(
			    qla2x00_get_nvram_word(ha, (cnt+base)));
			chksum += (uint8_t)*wptr;
			chksum += (uint8_t)(*wptr >> 8);
			wptr++;
		}
#if defined(ISP2300) 
		if (check_all_device_ids(ha)) { 	    
			/* Unlock resource */
			WRT_REG_WORD(&reg->host_semaphore, 0);
			PCI_POSTING(&reg->host_semaphore);
		}
#endif

#if  DEBUG_PRINT_NVRAM
		printk("%s(): Contents of NVRAM\n", __func__);
		qla2x00_dump_buffer((uint8_t *)ha->request_ring,
					sizeof(nvram22_t));
#endif
		/* Bad NVRAM data, set defaults parameters. */
		if (chksum ||
			nv->id[0] != 'I' || 
			nv->id[1] != 'S' || 
			nv->id[2] != 'P' ||
			nv->id[3] != ' ' || 
			nv->nvram_version < 1) {

			/* Reset NVRAM data. */
			DEBUG(printk("Using defaults for NVRAM: \n"));
			DEBUG(printk("checksum=0x%x, Id=%c, version=0x%x\n",
					chksum,
					nv->id[0],
					nv->nvram_version));

			memset(nv, 0, sizeof(nvram22_t));

			/*
			 * Set default initialization control block.
			 */
			nv->parameter_block_version = ICB_VERSION;

			*((uint16_t *) &firmware_options) =
			    le16_to_cpu(*((uint16_t *) &nv->firmware_options));

			firmware_options.enable_fairness = 1;
			firmware_options.enable_fast_posting = 1;
			firmware_options.enable_full_login_on_lip = 1;
			firmware_options.expanded_ifwcb = 1;

			*((uint16_t *) &nv->firmware_options) =
			    cpu_to_le16(*((uint16_t *) &firmware_options));

			nv->frame_payload_size  = __constant_cpu_to_le16(1024);
			nv->max_iocb_allocation = __constant_cpu_to_le16(256);
			nv->execution_throttle  = __constant_cpu_to_le16(16);

			nv->retry_count         = 8;
			nv->retry_delay         = 1;
			nv->port_name[0]        = 32;
			nv->port_name[3]        = 224;
			nv->port_name[4]        = 139;
			nv->login_timeout       = 4;

			*((uint16_t *) &additional_firmware_options) =
			    le16_to_cpu(*((uint16_t *)
				&nv->additional_firmware_options));

			additional_firmware_options.connection_options =
#if defined(ISP2200)
					P2P_LOOP;
#else
					LOOP_P2P;
#endif

			*((uint16_t *) &nv->additional_firmware_options) =
			    cpu_to_le16(*((uint16_t *)
				&additional_firmware_options));

			/*
			 * Set default host adapter parameters
			 */

			*((uint16_t *) &host_p) =
			    le16_to_cpu(*((uint16_t *) &nv->host_p));

			host_p.enable_lip_full_login = 1;

			*((uint16_t *) &nv->host_p) =
			    cpu_to_le16(*((uint16_t *) &host_p));

			nv->reset_delay = 5;
			nv->port_down_retry_count = 8;
			nv->maximum_luns_per_target = __constant_cpu_to_le16(8);
			nv->link_down_timeout = 60;
			status = 1;
		}

#if defined(ISP2200)
		/* Model Number */
		sprintf(ha->model_number,"QLA22xx");
#endif

#if defined(ISP2300) 
		/* Sub System Id (QLA2300/QLA2310): 0x9 */
		if (ha->device_id == QLA2300_DEVICE_ID &&
		    ha->pdev->subsystem_device  == 0x9 ) {
			/* Model Number */
			sprintf(ha->model_number,"QLA2300/2310");
		} else
		{
			/* This should be later versions of 23xx, with NVRAM
			 * support of hardware ID.
			 */
			strncpy(ha->hw_id_version, nv->hw_id, NVRAM_HW_ID_SIZE);

			/* Get the Model Number from the NVRAM. If
			 * the string is empty then lookup the table. 
			 */	
			if(status == 0 && 
				memcmp(nv->model_number,
					BINZERO, NVRAM_MODEL_SIZE) != 0) {
				/* found nvram model */
				strncpy(ha->model_number, nv->model_number,
					NVRAM_MODEL_SIZE);
			} else {
				uint16_t	index;
			       	/* Look up in the table */
				index = (ha->pdev->subsystem_device & 0xff );
				if( index < QLA_MODEL_NAMES){
					/* found in the table */
					sprintf(ha->model_number,
						qla2x00_model_name[index]);
				} else {
					set_model_number(ha);
				}
			}
		}
		
#endif
    
		sprintf(ha->model_desc,"QLogic %s PCI Fibre Channel Adapter",
		    ha->model_number);

		/* Reset NVRAM data. */
		memset(icb, 0, sizeof(init_cb_t));

		/*
		 * Copy over NVRAM RISC parameter block to initialization
		 * control block.
		 */
		dptr1 = (uint8_t *)icb;
		dptr2 = (uint8_t *)&nv->parameter_block_version;
		cnt = (uint8_t *)&nv->additional_firmware_options - 
			(uint8_t *)&nv->parameter_block_version;
		while (cnt--)
			*dptr1++ = *dptr2++;

		dptr1 += (uint8_t *)&icb->additional_firmware_options - 
				(uint8_t *)&icb->request_q_outpointer;
		cnt = (uint8_t *)&nv->serial_options - 
			(uint8_t *)&nv->additional_firmware_options;
		while (cnt--)
			*dptr1++ = *dptr2++;

		/*
		 * Get the three bit fields.
		 */
		*((uint16_t *) &firmware_options) =
		    le16_to_cpu(*((uint16_t *) &icb->firmware_options));

		*((uint16_t *) &additional_firmware_options) =
		    le16_to_cpu(*((uint16_t *)
			&icb->additional_firmware_options));

		*((uint16_t *) &host_p) =
		    le16_to_cpu(*((uint16_t *) &nv->host_p));
        
		*((uint16_t *) &special_options) =
		    le16_to_cpu(*((uint16_t *) &icb->special_options));

#if defined(ISP2300)
		qla2x00_alias_hba(ha);
#endif
        
		if (!firmware_options.node_name_option) {
			/*
			 * Firmware will apply the following mask if the
			 * nodename was not provided.
			 */
			memcpy(icb->node_name, icb->port_name, WWN_SIZE);
			icb->node_name[0] &= 0xF0;
		}


		/*
		 * Setup driver firmware options.
		 */

		firmware_options.enable_full_duplex       = 0;
		firmware_options.enable_target_mode       = 0;
		firmware_options.disable_initiator_mode   = 0;
		firmware_options.enable_port_update_event = 1;
		firmware_options.enable_full_login_on_lip = 1;
#if defined(ISP2300)
		firmware_options.enable_fast_posting = 0;

		/* Enable FC-Tape support */
		firmware_options.expanded_ifwcb = 1;
		additional_firmware_options.enable_fc_tape = 1;
		additional_firmware_options.enable_fc_confirm = 1;
#endif
#if defined(ISP2200)
 		additional_firmware_options.operation_mode = 4;
 		icb->response_accum_timer = 3;
 		icb->interrupt_delay_timer = 5;
#endif
#if defined(ISP2300)
		if (additional_firmware_options.operation_mode == ZIO_MODE) {
			icb->interrupt_delay_timer = ql2xintrdelaytimer;
			DEBUG2(printk(KERN_INFO "%s ZIO enabled:" 
				    " intr_timer_delay=%d\n", __func__,
				    ql2xintrdelaytimer);)
			printk(KERN_INFO "%s ZIO enabled:intr_timer_delay=%d\n",
					__func__,ql2xintrdelaytimer);
			ha->flags.process_response_queue = 1;
		}
		if (ql2xoperationmode & (BIT_0 | BIT_2)) {
			additional_firmware_options.operation_mode = ZIO_MODE;
			icb->interrupt_delay_timer = ql2xintrdelaytimer;
			ha->flags.process_response_queue = 1;

			DEBUG2(printk("%s(%ld) setting 23XX operation mode to"
				" =0x%x timer delay =0x%x us\n", __func__, ha->host_no, 
				ql2xoperationmode, ql2xintrdelaytimer);)
		}
#endif
		/*
		 * Set host adapter parameters
		 */
		ha->flags.enable_target_mode = firmware_options.enable_target_mode;
		ha->flags.disable_luns = host_p.disable_luns;
		if (check_device_id(ha)) 
			host_p.disable_risc_code_load = 0;
		ha->flags.disable_risc_code_load = host_p.disable_risc_code_load;
		ha->flags.set_cache_line_size_1 = host_p.set_cache_line_size_1;
		if(host_p.enable_extended_logging)
			extended_error_logging = 1 ;

		ha->flags.enable_lip_reset = host_p.enable_lip_reset;
		ha->flags.enable_lip_full_login = host_p.enable_lip_full_login;
		ha->flags.enable_target_reset = host_p.enable_target_reset;
		ha->flags.enable_flash_db_update = host_p.enable_database_storage;
		if(special_options.unused_12)
			ha->flags.enable_led_scheme = 1;
		ha->operating_mode = additional_firmware_options.connection_options;
		DEBUG2(printk("%s(%ld):operating mode=%d\n",__func__,
			    ha->host_no, ha->operating_mode);)
		/*
		 * Set serial firmware options
		 */
		*((uint16_t *) &serial_options) = 
			le16_to_cpu(*((uint16_t *) &nv->serial_options));
		ha->fw_seriallink_options = serial_options;

		/*
		 * Put back any changes made to the bit fields.
		 */
		*((uint16_t *) &icb->firmware_options) =
		    cpu_to_le16(*((uint16_t *) &firmware_options));

		*((uint16_t *) &icb->additional_firmware_options) =
		    cpu_to_le16(*((uint16_t *) &additional_firmware_options));

		*((uint16_t *) &icb->special_options) =
		    cpu_to_le16(*((uint16_t *) &special_options));

		/* new for IOCTL support of APIs */
		memcpy(ha->node_name, icb->node_name, WWN_SIZE);
		memcpy(ha->port_name, icb->port_name, WWN_SIZE);
		ha->nvram_version = nv->nvram_version;

		ha->hiwat = le16_to_cpu(icb->iocb_allocation);
		ha->execution_throttle = le16_to_cpu(nv->execution_throttle);
#if defined(ISP2200) || defined(ISP2300)
		if (nv->login_timeout < ql2xlogintimeout)
			nv->login_timeout = ql2xlogintimeout;
#endif

		icb->execution_throttle = __constant_cpu_to_le16(0xFFFF);
		ha->retry_count = nv->retry_count;
		/* Set minimum login_timeout to 4 seconds. */
		if (nv->login_timeout < 4)
			nv->login_timeout = 4;
		ha->login_timeout = nv->login_timeout;
		icb->login_timeout = nv->login_timeout;

		/* Need enough time to try and get the port back. */
		ha->port_down_retry_count = nv->port_down_retry_count;
		if (ha->port_down_retry_count < 30)
			ha->port_down_retry_count = 30;
#if defined(ISP2200) || defined(ISP2300)
		if (qlport_down_retry)
			ha->port_down_retry_count = qlport_down_retry;
#endif
#if defined(COMPAQ)
		else if (ha->port_down_retry_count < HSG80_PORT_RETRY_COUNT)
			ha->port_down_retry_count = HSG80_PORT_RETRY_COUNT;
#endif
		ha->minimum_timeout = (ha->login_timeout * ha->retry_count) +
		    ha->port_down_retry_count;
		ha->loop_reset_delay = nv->reset_delay;
		/* Will get the value from nvram. */
		ha->loop_down_timeout = LOOP_DOWN_TIMEOUT;

		/* Link Down Timeout = 0 :
		 *	When Port Down timer expires we will start returning
		 *	I/O's to OS with  "DID_NO_CONNECT".
		 *
		 * Link Down Timeout != 0 :
		 *	 is the time driver waits for the link
		 * to come up after link down before returning I/Os to
	 	 * OS with "DID_NO_CONNECT".
		 */						
		if (nv->link_down_timeout == 0){
			ha->loop_down_abort_time = ( LOOP_DOWN_TIME - 
							ha->loop_down_timeout);
		} else {
			ha->link_down_timeout =	 nv->link_down_timeout;
			ha->loop_down_abort_time =  ( LOOP_DOWN_TIME -
                                                        ha->link_down_timeout);
                } 
		DEBUG2(printk("%s link_down_timeout=0x%x\n",__func__,
						ha->link_down_timeout);)
		/* save HBA serial number */
		ha->serial0 = nv->port_name[5];
		ha->serial1 = nv->port_name[6];
		ha->serial2 = nv->port_name[7];
		ha->flags.link_down_error_enable  = 1;
		/* save OEM related items for QLA2200s and QLA2300s */
		for (cnt = 0; cnt < 8; cnt++) {
			ha->oem_fru[cnt] = nv->oem_fru[cnt];
			ha->oem_ec[cnt] = nv->oem_ec[cnt];
		}

#if defined(ISP2300)
		memcpy(ha->ip_port_name, nv->port_name, WWN_SIZE);
#endif

		ha->max_probe_luns = le16_to_cpu(nv->maximum_luns_per_target);
		if (ha->max_probe_luns == 0)
			ha->max_probe_luns = MIN_LUNS;

		/* High-water mark of IOCBs */
		ha->iocb_hiwat = MAX_IOCBS_AVAILBALE;

#if USE_BIOS_MAX_LUNS
		if (!nv->maximum_luns_per_target)
			ha->max_luns = MAX_LUNS;
		else if (nv->maximum_luns_per_target < MAX_LUNS)
			ha->max_luns = le16_to_cpu(nv->maximum_luns_per_target);
		else
			ha->max_luns = MAX_LUNS;
#else
		ha->max_luns = MAX_LUNS;
#endif

		ha->binding_type = Bind;
		if ((ha->binding_type != BIND_BY_PORT_NAME) &&
			(ha->binding_type != BIND_BY_PORT_ID)) {

			printk(KERN_WARNING
				"scsi(%ld): Invalid binding type specified "
				"(%d), defaulting to BIND_BY_PORT_NAME!!!\n",
				ha->host_no,
				ha->binding_type);
			ha->binding_type = BIND_BY_PORT_NAME;
		}

		/* Set login_retry_count */
		ha->login_retry_count  = nv->retry_count;
		if (ha->port_down_retry_count == nv->port_down_retry_count &&
		    ha->port_down_retry_count > 3)
			ha->login_retry_count = ha->port_down_retry_count;
		else if (ha->port_down_retry_count > ha->login_retry_count)
			ha->login_retry_count = ha->port_down_retry_count;
#if defined(ISP2200) || defined(ISP2300)
		if (qlogin_retry_count)
			ha->login_retry_count = qlogin_retry_count;
#endif

		/*
		 * Setup ring parameters in initialization control block
		 */
		icb->request_q_outpointer  = __constant_cpu_to_le16(0);
		icb->response_q_inpointer  = __constant_cpu_to_le16(0);
		icb->request_q_length      =
			__constant_cpu_to_le16(REQUEST_ENTRY_CNT);
		icb->response_q_length     =
			__constant_cpu_to_le16(RESPONSE_ENTRY_CNT);
		icb->request_q_address[0]  = cpu_to_le32(LSD(ha->request_dma));
		icb->request_q_address[1]  = cpu_to_le32(MSD(ha->request_dma));
		icb->response_q_address[0] = cpu_to_le32(LSD(ha->response_dma));
		icb->response_q_address[1] = cpu_to_le32(MSD(ha->response_dma));

		icb->lun_enables = __constant_cpu_to_le16(0);
		icb->command_resource_count = 0;
		icb->immediate_notify_resource_count = 0;
		icb->timeout = __constant_cpu_to_le16(0);
		icb->reserved_3 = __constant_cpu_to_le16(0);

		ha->qfull_retry_count = qfull_retry_count;
		ha->qfull_retry_delay = qfull_retry_delay;

		ha->flags.nvram_config_done = 1;

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk(KERN_WARNING
			"%s(): **** FAILED ****\n", __func__);
#endif

	LEAVE(__func__);

	return (status);
}

#if defined(ISP2300)

static inline uint32_t
nvram_to_access_addr(uint32_t naddr)
{
#if 0
        WARN_ON(naddr & (BIT_1|BIT_0));
#endif                                                                                                          
        return (FARX_ACCESS_NVRAM_DATA | naddr);
}


uint32_t
qla24xx_read_flash_dword(scsi_qla_host_t *ha, uint32_t addr)
{
        int rval;
        uint32_t cnt, data;
        struct device_reg_24xx *reg;
                                                                                                               
        reg = (struct device_reg_24xx  *)ha->iobase;
        WRT_REG_DWORD(&reg->flash_addr, addr & ~FARX_DATA_FLAG);
        /* Wait for READ cycle to complete. */
        rval = QLA2X00_SUCCESS;
        for (cnt = 3000;
            (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) == 0 &&
            rval == QLA2X00_SUCCESS; cnt--) {
                if (cnt)
                        udelay(10);
                else
                        rval = QLA2X00_FUNCTION_FAILED;
        }
                                                                                                               
        /* TODO: What happens if we time out? */
        data = 0xDEADDEAD;
        if (rval == QLA2X00_SUCCESS)
                data = RD_REG_DWORD(&reg->flash_data);
                                                                                                               
        return (data);
}
                                                                                                               


uint32_t *
qla24xx_read_nvram_data(scsi_qla_host_t *ha, uint32_t *buf, uint32_t snaddr,
    uint32_t len)
{
        uint32_t i;
        uint32_t naddr;

	/* Dword reads to flash. */
        for (i = 0, naddr = snaddr; i < len >> 2; i++, naddr++)
                buf[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
                    nvram_to_access_addr(naddr)));

        return buf;
}

static int
qla24xx_nvram_config(scsi_qla_host_t *ha)
{
	int   rval;
	struct init_cb_24xx *icb;
	struct init_cb_24xx *icb24;
	struct nvram_24xx *nv = (struct nvram_24xx *)ha->request_ring;
	uint32_t *dptr  = (uint32_t *)ha->request_ring;
	uint8_t   *dptr1, *dptr2;
	struct device_reg_24xx *reg;
	uint32_t chksum;
	uint16_t  cnt;

	rval = QLA2X00_SUCCESS;
	icb = (struct init_cb_24xx *)ha->init_cb;
	reg = (struct device_reg_24xx *)ha->iobase;
	nv = (struct nvram_24xx *)ha->request_ring;

	/* Determine NVRAM starting address. */
	ha->nvram_size = sizeof(struct nvram_24xx);
#if 0
	ha->nvram_base = 0x200;
	if (PCI_FUNC(ha->pdev->devfn))
		ha->nvram_base = 0x600;
#else 
	ha->nvram_base = FA_NVRAM_FUNC0_ADDR;
	if (PCI_FUNC(ha->pdev->devfn))
		ha->nvram_base = FA_NVRAM_FUNC1_ADDR;
#endif

	/* Get NVRAM data and calculate checksum. */
	dptr  = (uint32_t *)ha->request_ring;
	qla24xx_read_nvram_data(ha, dptr, ha->nvram_base,
	    sizeof(struct nvram_24xx));

	for (cnt = 0, chksum = 0; cnt < sizeof(struct nvram_24xx) >> 2; cnt++)
		chksum += le32_to_cpu(*dptr++);

	DEBUG5(printk("scsi(%ld): Contents of NVRAM\n", ha->host_no));
	DEBUG5(qla2x00_dump_buffer((uint8_t *)ha->request_ring,
	    sizeof(struct nvram_24xx)));

	/* Bad NVRAM data, set defaults parameters. */
	if (chksum || nv->id[0] != 'I' || nv->id[1] != 'S' || nv->id[2] != 'P'
	    || nv->id[3] != ' ' ||
	    nv->nvram_version < __constant_cpu_to_le16(ICB_VERSION)) {
		/* Reset NVRAM data. */
		printk(KERN_WARNING "Inconsistent NVRAM detected: "
		    "checksum=0x%x id=%c version=0x%x.\n", chksum, nv->id[0],
		    le16_to_cpu(nv->nvram_version));
		printk(KERN_WARNING "Falling back to functioning (yet "
		    "invalid -- WWPN) defaults.\n");

		/*
		 * Set default initialization control block.
		 */
		memset(nv, 0, sizeof(struct nvram_24xx));
		nv->nvram_version = __constant_cpu_to_le16(ICB_VERSION);
		nv->version = __constant_cpu_to_le16(ICB_VERSION);
		nv->frame_payload_size = __constant_cpu_to_le16(2048);
		nv->execution_throttle = __constant_cpu_to_le16(0xFFFF);
		nv->exchange_count = __constant_cpu_to_le16(0);
		nv->hard_address = __constant_cpu_to_le16(124);
		nv->port_name[0] = 0x21;
		nv->port_name[1] = 0x00 + PCI_FUNC(ha->pdev->devfn);
		nv->port_name[2] = 0x00;
		nv->port_name[3] = 0xe0;
		nv->port_name[4] = 0x8b;
		nv->port_name[5] = 0x1c;
		nv->port_name[6] = 0x55;
		nv->port_name[7] = 0x86;
		nv->node_name[0] = 0x20;
		nv->node_name[1] = 0x00;
		nv->node_name[2] = 0x00;
		nv->node_name[3] = 0xe0;
		nv->node_name[4] = 0x8b;
		nv->node_name[5] = 0x1c;
		nv->node_name[6] = 0x55;
		nv->node_name[7] = 0x86;
		nv->login_retry_count = __constant_cpu_to_le16(8);
		nv->link_down_timeout = __constant_cpu_to_le16(200);
		nv->interrupt_delay_timer = __constant_cpu_to_le16(0);
		nv->login_timeout = __constant_cpu_to_le16(0);
		nv->firmware_options_1 = __constant_cpu_to_le32(BIT_2|BIT_1);
		nv->firmware_options_2 = __constant_cpu_to_le32(2 << 4);
		nv->firmware_options_3 = __constant_cpu_to_le32(2 << 13);

		nv->host_p = 0; //FIXME
		nv->efi_parameters = 0; //FIXME
		nv->reset_delay = 5;
		nv->max_luns_per_target = __constant_cpu_to_le16(8);
		nv->port_down_retry_count = __constant_cpu_to_le16(30);
		nv->link_down_timeout = __constant_cpu_to_le16(30);
	
		rval = 1;
	}

	/* Reset Initialization control block */
	memset(icb, 0, sizeof(struct init_cb_24xx));

	/* Copy 1st segment. */
	dptr1 = (uint8_t *)icb;
	dptr2 = (uint8_t *)&nv->version;
	cnt = (uint8_t *)&icb->response_q_inpointer - (uint8_t *)&icb->version;
	while (cnt--)
		*dptr1++ = *dptr2++;

	icb->login_retry_count = nv->login_retry_count;
	icb->link_down_timeout = nv->link_down_timeout;

	/* Copy 2nd segment. */
	dptr1 = (uint8_t *)&icb->interrupt_delay_timer;
	dptr2 = (uint8_t *)&nv->interrupt_delay_timer;
	cnt = (uint8_t *)&icb->reserved_3 -
	    (uint8_t *)&icb->interrupt_delay_timer;
	while (cnt--)
		*dptr1++ = *dptr2++;
#if 0
	icb->firmware_options_1 = __constant_cpu_to_le32(BIT_2|BIT_1);
	icb->firmware_options_2 = __constant_cpu_to_le32(1 << 4);
	//icb->firmware_options_2 = __constant_cpu_to_le32(0 << 4);	LOOP
	//icb->firmware_options_2 = __constant_cpu_to_le32(1 << 4);	P2P
	//LOOP/P2P
	icb->firmware_options_2 = __constant_cpu_to_le32(2 << 4);	
	//icb->firmware_options_3 = __constant_cpu_to_le32(1 << 13);
	//icb->firmware_options_3 = __constant_cpu_to_le32(0 << 13);	1GB
	//icb->firmware_options_3 = __constant_cpu_to_le32(1 << 13);	2GB
	// AUTO
	icb->firmware_options_3 = __constant_cpu_to_le32(2 << 13);	
	//icb->firmware_options_3 = __constant_cpu_to_le32(3 << 13);	4GB
#endif
	//icb->firmware_options_2 = __constant_cpu_to_le32(1 << 4);	P2P
	/*
	 * Setup driver NVRAM options.
	 */
	/* Add the code to get the model number from the NVRAM.
 	 * If none present then default to something 
	 */
	if (memcmp(nv->model_name, BINZERO, sizeof(nv->model_name)) != 0) {
		char *st, *en;
		uint16_t index;

		strncpy(ha->model_number, nv->model_name,
		    sizeof(nv->model_name));
		st = en = ha->model_number;
		en += sizeof(nv->model_name) - 1;
		while (en > st) {
			if (*en != 0x20 && *en != 0x00)
				break;
			*en-- = '\0';
		}

		index = (ha->pdev->subsystem_device & 0xff);
		if (index < QLA_MODEL_NAMES)
			sprintf(ha->model_desc,"QLogic %s PCI Fibre Channel Adapter",
			    ha->model_number);
#if 0
			ha->model_desc = qla2x00_model_desc[index];
#endif
	} else
		strcpy(ha->model_number, "QLA2462");

	qla2x00_alias_hba(ha);


	qla2x00_alias_hba(ha);


	/* Prepare nodename */
	if ((le32_to_cpu(icb->firmware_options_1) & BIT_14) == 0) {
		/*
		 * Firmware will apply the following mask if the nodename was
		 * not provided.
		 */
		memcpy(icb->node_name, icb->port_name, WWN_SIZE);
		icb->node_name[0] &= 0xF0;
	}
	if (ha->device_id == QLA2422_DEVICE_ID || 
	    ha->device_id == QLA2432_DEVICE_ID) {
		/* ZIO Mode 5 or ZIO Mode 6 */
		if ((le32_to_cpu(icb->firmware_options_2) == (BIT_0 | BIT_2)) ||
			le32_to_cpu(icb->firmware_options_2 == (BIT_1 | BIT_2))) {
			ha->flags.process_response_queue = 1;
			DEBUG2(printk("%s(%ld)  24XX operation mode set to"
				" =0x%x\n", __func__, ha->host_no, 
				le32_to_cpu(icb->firmware_options_2));)
		}

		/* ZIO Mode 5 or ZIO Mode 6 */
		if ((ql2xoperationmode & (BIT_0 | BIT_2)) ||
			(ql2xoperationmode & (BIT_1 | BIT_2))) {
			icb->firmware_options_2 |=  cpu_to_le32(ql2xoperationmode);
			icb->interrupt_delay_timer = cpu_to_le16((uint16_t)ql2xintrdelaytimer);
			ha->flags.process_response_queue = 1;

			DEBUG2(printk("%s(%ld) setting 24XX operation mode to"
				" =0x%x timer delay =0x%x us\n", __func__, ha->host_no, 
				ql2xoperationmode, ql2xintrdelaytimer);)
		}
	} 

	/* Set host adapter parameters. */
	/*  Always load RISC code on non ISP2[12]00 chips. */
	//ha->flags.disable_risc_code_load = 0;
	ha->flags.enable_lip_reset = 1;
	ha->flags.enable_lip_full_login = 1;
	ha->flags.enable_target_reset = 1;
	ha->flags.enable_led_scheme = 0;

	ha->operating_mode =
	    (le32_to_cpu(icb->firmware_options_2) & (BIT_6 | BIT_5 | BIT_4)) >> 4;

	// FILL IN
	memcpy(ha->fw_seriallink_options24, nv->seriallink_options,
	    sizeof(ha->fw_seriallink_options24));

	/* save HBA serial number */
	ha->serial0 = icb->port_name[5];
	ha->serial1 = icb->port_name[6];
	ha->serial2 = icb->port_name[7];
	memcpy(ha->node_name, icb->node_name, WWN_SIZE);
	memcpy(ha->port_name, icb->port_name, WWN_SIZE);
	
	icb->execution_throttle = __constant_cpu_to_le16(0xFFFF);
	
	ha->retry_count = le16_to_cpu(nv->login_retry_count);

	/* Set minimum login_timeout to 4 seconds. */
	if (le16_to_cpu(nv->login_timeout) < ql2xlogintimeout)
		nv->login_timeout = cpu_to_le16(ql2xlogintimeout);
	if (le16_to_cpu(nv->login_timeout) < 4)
		nv->login_timeout = __constant_cpu_to_le16(4);
	ha->login_timeout = le16_to_cpu(nv->login_timeout);
	icb->login_timeout = cpu_to_le16(nv->login_timeout);

	/* Set minimum RATOV to 200 tenths of a second. */
#if 0
	ha->r_a_tov = 200;
#endif
	ha->minimum_timeout = (ha->login_timeout * ha->retry_count) +
	    le16_to_cpu(nv->port_down_retry_count);
	ha->loop_reset_delay = nv->reset_delay;

	/* Will get the value from NVRAM. */
	ha->loop_down_timeout = LOOP_DOWN_TIMEOUT;


	/* Link Down Timeout = 0:
	 *
	 * 	When Port Down timer expires we will start returning
	 *	I/O's to OS with "DID_NO_CONNECT".
	 *
	 * Link Down Timeout != 0:
	 *
	 *	 The driver waits for the link to come up after link down
	 *	 before returning I/Os to OS with "DID_NO_CONNECT".
	 */						
	if (le16_to_cpu(nv->link_down_timeout) == 0) {
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - ha->loop_down_timeout);
	} else {
		ha->link_down_timeout =	le16_to_cpu(nv->link_down_timeout);
		ha->loop_down_abort_time =
		    (LOOP_DOWN_TIME - ha->link_down_timeout);
	} 

	ha->max_luns = MAX_LUNS;
	ha->max_probe_luns = le16_to_cpu(nv->max_luns_per_target);
	if (ha->max_probe_luns == 0)
		ha->max_probe_luns = MIN_LUNS;

	/* Need enough time to try and get the port back. */
	ha->port_down_retry_count = le16_to_cpu(nv->port_down_retry_count);
	if (qlport_down_retry)
		ha->port_down_retry_count = qlport_down_retry;

	/* Set login_retry_count */
	ha->login_retry_count  = le16_to_cpu(nv->login_retry_count);
	if (ha->port_down_retry_count ==
	    le16_to_cpu(nv->port_down_retry_count) &&
	    ha->port_down_retry_count > 3)
		ha->login_retry_count = ha->port_down_retry_count;
	else if (ha->port_down_retry_count > (int)ha->login_retry_count)
		ha->login_retry_count = ha->port_down_retry_count;
	if (qlogin_retry_count)
		ha->login_retry_count = qlogin_retry_count;
	

	icb24 = &ha->init_cb->cb.icb24;
	icb24->request_q_outpointer = __constant_cpu_to_le16(0);
	icb24->response_q_inpointer = __constant_cpu_to_le16(0);
	/* FixMe: check the size of req and resp queue */
	icb24->request_q_length =  __constant_cpu_to_le16( REQUEST_ENTRY_CNT_24XX);
	icb24->response_q_length =  __constant_cpu_to_le16(RESPONSE_ENTRY_CNT);
	icb24->request_q_address[0] = cpu_to_le32(LSD(ha->request_dma));
        icb24->request_q_address[1] = cpu_to_le32(MSD(ha->request_dma));
        icb24->response_q_address[0] = cpu_to_le32(LSD(ha->response_dma));
	icb24->response_q_address[1] = cpu_to_le32(MSD(ha->response_dma));

	ha->binding_type = Bind;
	if (ha->binding_type != BIND_BY_PORT_NAME &&
	    ha->binding_type != BIND_BY_PORT_ID) {
		printk(KERN_WARNING "Invalid binding type specified (%d), "
		    "defaulting to BIND_BY_PORT_NAME!!!\n", ha->binding_type);

		ha->binding_type = BIND_BY_PORT_NAME;
	}

	if (rval) {
		DEBUG2_3(printk(KERN_WARNING
		    "scsi(%ld): NVRAM configuration failed!\n", ha->host_no));
	}
	return (rval);
}



uint32_t *
qla24xx_read_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
        uint32_t i;
                                                                                                               
        /* Dword reads to flash. */
        for (i = 0; i < dwords; i++, faddr++)
                dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
                    flash_data_to_access_addr(faddr)));
                                                                                                               
        return dwptr;
}


static int
qla24xx_load_risc(scsi_qla_host_t *ha, uint32_t *srisc_addr)
{
	int		rval;
	uint32_t	i, cnt;
	uint32_t	*risc_code;
	uint32_t risc_address;
	uint32_t risc_code_size;
	int		num;
	uint32_t	*req_ring;
	struct qla_fw_info *fw_iter;

	rval = QLA2X00_SUCCESS;

	/* Load firmware sequences */
	fw_iter = QLBoardTbl_fc[ha->devnum].fwinfo;
	*srisc_addr = *((uint32_t *) fw_iter->lfwstart);
	while (fw_iter->addressing != FW_INFO_ADDR_NOMORE) {
		risc_code = (uint32_t *)fw_iter->fwcode;
		risc_code_size = *((uint32_t *) fw_iter->fwlen);
		risc_address = *((uint32_t *) fw_iter->lfwstart);

		num = 0;
		rval = 0;
		while (risc_code_size > 0 && !rval) {
			cnt = (REQUEST_ENTRY_SIZE * 
					REQUEST_ENTRY_CNT_24XX) >> 2;
			if (cnt > risc_code_size)
				cnt = risc_code_size;

			DEBUG2(printk("scsi(%ld): Loading risc segment@ "
			    "addr %p, number of bytes 0x%x, offset 0x%x.\n",
			    ha->host_no, risc_code, cnt, risc_address));

			req_ring = (uint32_t *)ha->request_ring;
			for (i = 0; i < cnt; i++)
				req_ring[i] = cpu_to_le32(risc_code[i]);

			flush_cache_all();
			rval = qla2x00_load_ram_ext(ha, ha->request_dma,
			    risc_address, cnt);
			if (rval) {
				DEBUG(printk("scsi(%ld): [ERROR] Failed to "
				    "load segment %d of firmware\n",
				    ha->host_no, num));
				printk(KERN_WARNING
				    "[ERROR] Failed to load segment %d of "
				    "firmware\n", num);

				break;
			}

			risc_code += cnt;
			risc_address += cnt;
			risc_code_size -= cnt;
			num++;
		}

		/* Next firmware sequence */
		fw_iter++;
	}
	return (rval);
}

int
qla24xx_load_risc_flash(scsi_qla_host_t *ha, uint32_t *srisc_addr)
{
	int	rval;
	int	segments, fragment;
	uint32_t faddr;
	uint32_t *dcode, dlen;
	uint32_t risc_addr;
	uint32_t risc_size;
	uint32_t i;

	rval = QLA2X00_SUCCESS;

	DEBUG2(printk("%s rval=%d\n",__func__,rval);)
	segments = FA_RISC_CODE_SEGMENTS;
	faddr = FA_RISC_CODE_ADDR;
	dcode = (uint32_t *)ha->request_ring;
	*srisc_addr = 0;

	/* NOTE:
	 * Firmware image is written to flash DWORD swab'd and in
	 * little-endian format.
	 */

	/* Validate firmware image by checking version. */
	qla24xx_read_flash_data(ha, dcode, faddr + 4, 4);
	for (i = 0; i < 4; i++)
		dcode[i] = le32_to_cpu(swab32(dcode[i]));
	if ((dcode[0] == 0xffffffff && dcode[1] == 0xffffffff &&
	    dcode[2] == 0xffffffff && dcode[3] == 0xffffffff) ||
	    (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
		dcode[3] == 0)) {
		printk(KERN_WARNING
		    "Unable to verify integrity of flash firmware image!\n");
		printk(KERN_WARNING
		    "Firmware data: %08x %08x %08x %08x!\n", dcode[0],
		    dcode[1], dcode[2], dcode[3]);

		return QLA2X00_FUNCTION_FAILED;
	}

	while (segments && rval == QLA2X00_SUCCESS) {
		/* Read segment's load information. */	
		qla24xx_read_flash_data(ha, dcode, faddr, 4);

		risc_addr = le32_to_cpu(swab32(dcode[2]));
		*srisc_addr = *srisc_addr == 0 ? risc_addr : *srisc_addr;
		risc_size = le32_to_cpu(swab32(dcode[3]));

		fragment = 0;
		while (risc_size > 0 && rval == QLA2X00_SUCCESS) {
			dlen = REQUEST_ENTRY_SIZE * REQUEST_ENTRY_CNT_24XX >> 2;
			if (dlen > risc_size)
				dlen = risc_size;

			DEBUG7(printk("scsi(%ld): Loading risc segment@ risc "
			    "addr %x, number of dwords 0x%x, offset 0x%x.\n",
			    ha->host_no, risc_addr, dlen, faddr));

			qla24xx_read_flash_data(ha, dcode, faddr, dlen);
			for (i = 0; i < dlen; i++)
				dcode[i] = swab32(dcode[i]);

			flush_cache_all();
			rval = qla2x00_load_ram_ext(ha, ha->request_dma,
			    risc_addr, dlen);
			if (rval) {
				DEBUG(printk("scsi(%ld):[ERROR] Failed to load "
				    "segment %d of firmware\n", ha->host_no,
				    fragment));
				printk(KERN_WARNING
				    "[ERROR] Failed to load segment %d of "
				    "firmware\n", fragment);

				qla2x00_dump_regs(ha->host);
				break;
			}

			faddr += dlen;
			risc_addr += dlen;
			risc_size -= dlen;
			fragment++;
		}

		/* Next segment. */
		segments--;
	}

	DEBUG3(printk("%s rval=%d\n",__func__,rval);)
	return (rval);
}

static int
qla24xx_setup_chip(scsi_qla_host_t *ha)
{
	int		status;
        uint32_t srisc_addr = 0;
	qla_boards_t    *bdp;

	status = QLA2X00_SUCCESS;

	/* Load firmware sequences */
	  if (ql2xfwloadbin == 1)
		status = qla24xx_load_risc_flash(ha, &srisc_addr);
          else	                     
                status = qla24xx_load_risc(ha, &srisc_addr);

	/* Verify checksum of loaded RISC code. */
	if (!status) {
		DEBUG2(printk("%s(): Verifying Check Sum of loaded RISC code.\n",
				__func__);)

		status = (uint8_t)qla2x00_verify_checksum(ha,srisc_addr);
				

		if (status == QL_STATUS_SUCCESS) {
			/* Start firmware execution. */
			DEBUG2(printk("%s(): CS Ok, Start firmware running\n",
					__func__);)
			status = qla2x00_execute_fw(ha,srisc_addr);
			/* Retrieve firmware information. */
			if (status == QLA2X00_SUCCESS && 
				ha->fw_version[0] == 0) {
				bdp = &QLBoardTbl_fc[ha->devnum];
				bdp->fwver = &ha->fw_version[0];
				qla2x00_get_fw_version(ha,
				    &ha->fw_version[0],
				    &ha->fw_version[1],
				    &ha->fw_version[2],
				    &ha->fw_attributes, &ha->fw_memory_size);
#if DEBUG_GET_FW_DUMP
#if defined(ISP2300)
				qla2x00_alloc_fw_dump(ha);	
#endif
#endif
			}
			
				   
		}
#if defined(QL_DEBUG_LEVEL_2)
		else {
			printk(KERN_INFO
				"%s(): ISP FW Failed Check Sum\n", __func__);
		}
#endif
	}

	if (status) {
		DEBUG2_3(printk(KERN_INFO "%s(): **** FAILED ****\n", __func__);)
	} else {
		DEBUG3(printk("%s(): Returning Good Status\n", __func__);)
	}

	return (status);
}
#if 0
int
qla24xx_setup_chip(scsi_qla_host_t *ha)
{
	int	rval;
	int	segments, fragment;
	uint32_t faddr;
	uint32_t *dcode, dlen;
	uint32_t srisc_addr, risc_addr;
	uint32_t risc_size;

	rval = QLA2X00_SUCCESS;

	/*
	 * RISC code begins at offset 512KB within flash. Consisting of two
	 * contiguous RISC code segments.
	 */	
	segments = 2;
	faddr = 512 * 1024;
	dcode = (uint32_t *)ha->request_ring;
	srisc_addr = 0;

	rval = QLA2X00_SUCCESS;
	while (segments && rval == QLA2X00_SUCCESS) {
		/* Read segment's load information. */	
		qla24xx_read_flash_data(ha, dcode, faddr, 4);

		risc_addr = le32_to_cpu(dcode[2]);
		srisc_addr = srisc_addr == 0 ? risc_addr : srisc_addr;
		risc_size = le32_to_cpu(dcode[3]);

		fragment = 0;
		while (risc_size > 0 && rval == QLA2X00_SUCCESS) {
			dlen = (uint32_t)(ha->fw_transfer_size >> 2);
			if (dlen > risc_size)
				dlen = risc_size;

			DEBUG7(printk("scsi(%ld): Loading risc segment@ risc "
			    "addr %x, number of dwords 0x%x, offset 0x%x.\n",
			    ha->host_no, risc_addr, cnt, faddr));

			qla24xx_read_flash_data(ha, dcode, faddr, dlen);

			rval = qla2x00_load_ram_ext(ha, ha->request_dma,
			    risc_addr, dlen);
			if (rval) {
				DEBUG(printk("scsi(%ld):[ERROR] Failed to load "
				    "segment %d of firmware\n", ha->host_no,
				    fragment));
				printk(KERN_WARNING "[ERROR] Failed to load segment %d of "
				    "firmware\n", fragment);

				qla2x00_dump_regs(ha);
				break;
			}

			faddr += dlen << 2;
			risc_addr += dlen;
			risc_size -= dlen;
			fragment++;
		}

		/* Next segment. */
		segments--;
	}

	/* Verify checksum of loaded RISC code. */
	if (rval == QLA2X00_SUCCESS) {
		DEBUG(printk("scsi(%ld): Verifying Checksum of loaded RISC "
		    "code.\n", ha->host_no));

		rval = qla2x00_verify_checksum(ha, srisc_addr);
		if (rval == QLA2X00_SUCCESS) {
			/* Start firmware execution. */
			DEBUG(printk("scsi(%ld): Checksum OK, start "
			    "firmware.\n", ha->host_no));

			rval = qla2x00_execute_fw(ha, srisc_addr);
			/* Retrieve firmware information. */
			if (rval == QLA2X00_SUCCESS && ha->fw_major_version == 0) {
				qla2x00_get_fw_version(ha,
				    &ha->fw_major_version,
				    &ha->fw_minor_version,
				    &ha->fw_subminor_version,
				    &ha->fw_attributes, &ha->fw_memory_size);
				qla2x00_resize_request_q(ha);
			}
		} else {
			DEBUG2(printk(KERN_INFO
			    "scsi(%ld): ISP Firmware failed checksum.\n",
			    ha->host_no));
		}
	}

	if (rval) {
		DEBUG2_3(printk("scsi(%ld): Setup chip **** FAILED ****.\n",
		    ha->host_no));
	}

	return (rval);
}
#endif	
#endif	
#endif	/* #if defined(ISP2100) */

/*
* Get NVRAM data word
*      Calculates word position in NVRAM and calls request routine to
*      get the word from NVRAM.
*
* Input:
*      ha      = adapter block pointer.
*      address = NVRAM word address.
*
* Returns:
*      data word.
*/
STATIC uint16_t
qla2x00_get_nvram_word(scsi_qla_host_t *ha, uint32_t address)
{
	uint32_t nv_cmd;
	uint16_t data;

#if defined(QL_DEBUG_ROUTINES)
	uint8_t  saved_print_status = ql2x_debug_print;
#endif

	DEBUG4(printk("qla2100_get_nvram_word: entered\n");)

	nv_cmd = address << 16;
	nv_cmd |= NV_READ_OP;

#if defined(QL_DEBUG_ROUTINES)
	ql2x_debug_print = FALSE;
#endif

	data = qla2x00_nvram_request(ha, nv_cmd);
#if defined(QL_DEBUG_ROUTINES)
	ql2x_debug_print = saved_print_status;
#endif

	DEBUG4(printk("qla2100_get_nvram_word: exiting normally "
			"NVRAM data=%lx.\n",
			(u_long)data);)

	return(data);
}

/*
* NVRAM request
*      Sends read command to NVRAM and gets data from NVRAM.
*
* Input:
*      ha     = adapter block pointer.
*      nv_cmd = Bit 26     = start bit
*               Bit 25, 24 = opcode
*               Bit 23-16  = address
*               Bit 15-0   = write data
*
* Returns:
*      data word.
*/
STATIC uint16_t
qla2x00_nvram_request(scsi_qla_host_t *ha, uint32_t nv_cmd)
{
	uint8_t      cnt;
	device_reg_t *reg = ha->iobase;
	uint16_t     data = 0;
	uint16_t     reg_data;

	/* Send command to NVRAM. */
	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NV_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */
	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, NV_SELECT+NV_CLOCK);
		PCI_POSTING(&reg->nvram);
		/* qla2x00_nv_delay(ha); */
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NV_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NV_SELECT);
		PCI_POSTING(&reg->nvram);
		/* qla2x00_nv_delay(ha); */
		NVRAM_DELAY();
	}

	/* Deselect chip. */
	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	PCI_POSTING(&reg->nvram);
	/* qla2x00_nv_delay(ha); */
	NVRAM_DELAY();

	return(data);
}

STATIC void
qla2x00_nv_write(scsi_qla_host_t *ha, uint16_t data)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_WR_ENABLE);
	PCI_POSTING(&reg->nvram);
	NVRAM_DELAY();

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_CLOCK | NV_WR_ENABLE);
	PCI_POSTING(&reg->nvram);
	NVRAM_DELAY();

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_WR_ENABLE);
	PCI_POSTING(&reg->nvram);
	NVRAM_DELAY();
}

STATIC void
qla2x00_nv_deselect(scsi_qla_host_t *ha)
{
	device_reg_t *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	PCI_POSTING(&reg->nvram);
	NVRAM_DELAY();
}

/*
* qla2x00_poll
*      Polls ISP for interrupts.
*
* Input:
*      ha = adapter block pointer.
*/
STATIC void
qla2x00_poll(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t *reg   = ha->iobase;
	uint8_t     discard;
	uint32_t     data;

	ENTER(__func__);
	data = 0;
	/* Acquire interrupt specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	data = RD_REG_WORD(&reg->istatus);
	if (data & RISC_INT)
		qla2x00_isr(ha, data, &discard);
#else
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                struct device_reg_24xx * reg24;
		reg24 = (struct device_reg_24xx *)ha->iobase;
		data = RD_REG_DWORD(&reg24->host_status);
		if (data & HSRX_RISC_INT)
			qla24xx_isr(ha, data, &discard);
	} else {
		if (check_all_device_ids(ha)) {
			data = RD_REG_WORD(&reg->istatus);
			if (data & RISC_INT) {
				data = RD_REG_WORD(&reg->host_status_lo);
				qla2x00_isr(ha, data, &discard);
			}

		} else {
			data = RD_REG_WORD(&reg->host_status_lo);
			if (data & HOST_STATUS_INT)
				qla2x00_isr(ha, data, &discard);
		}
	}
#endif
	/* Release interrupt specific lock */
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (!list_empty(&ha->done_queue))
		qla2x00_done(ha);

	LEAVE(__func__);
}

/*
*  qla2x00_restart_isp
*      restarts the ISP after a reset
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
int
qla2x00_restart_isp(scsi_qla_host_t *ha)
{
	uint8_t		status = 0;
#if defined(ISP2300)
	device_reg_t	*reg;
	unsigned long	flags = 0;
#endif

	/* If firmware needs to be loaded */
	if (qla2x00_isp_firmware(ha)) {
		ha->flags.online = FALSE;
		if (!(status = qla2x00_chip_diag(ha))) {
#if defined(ISP2300)
			reg = ha->iobase;
			if (!check_24xx_or_54xx_device_ids(ha) && 
				!check_25xx_device_ids(ha)) {
				spin_lock_irqsave(&ha->hardware_lock, flags);
			        /* Disable SRAM, Instruction RAM and 
					GP RAM parity. */
				WRT_REG_WORD(&reg->host_cmd, 
						(HC_ENABLE_PARITY + 0x0));
				PCI_POSTING(&reg->host_cmd);
				spin_unlock_irqrestore(&ha->hardware_lock, 
								flags);
			}
#endif

			status = qla2x00_setup_chip(ha);

#if defined(ISP2300)
			if (!check_24xx_or_54xx_device_ids(ha) && 
				!check_25xx_device_ids(ha)) {
			    spin_lock_irqsave(&ha->hardware_lock, flags);
			    /* Enable proper parity */
			    if (check_all_device_ids(ha)) 	    
				/* SRAM, Instruction RAM and GP RAM parity */
				WRT_REG_WORD(&reg->host_cmd,
				    (HC_ENABLE_PARITY + 0x7));
			    else
				/* SRAM parity */
				WRT_REG_WORD(&reg->host_cmd,
				    (HC_ENABLE_PARITY + 0x1));
			    PCI_POSTING(&reg->host_cmd);
			    spin_unlock_irqrestore(&ha->hardware_lock, flags);
			}
#endif
		}
	}
	if (!status && !(status = qla2x00_init_rings(ha))) {
		clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);
		clear_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
		if (!(status = qla2x00_fw_ready(ha))) {
			DEBUG(printk("%s(%ld): Start configure loop, "
					"status = %d\n",
					__func__,ha->host_no,
					status);)
			ha->flags.online = TRUE;
			do {
				clear_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				qla2x00_configure_loop(ha);
			} while (!atomic_read(&ha->loop_down_timer) &&
				!(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) &&
				(test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)));
		}

		/* if no cable then assume it's good */
		if ((ha->device_flags & DFLG_NO_CABLE)) 
			status = 0;

		DEBUG(printk("%s(): Configure loop done, status = 0x%x\n",
				__func__,
				status);)
	}
	return (status);
}

/*
*  qla2x00_abort_isp
*      Resets ISP and aborts all outstanding commands.
*
* Input:
*      ha           = adapter block pointer.
*
* Returns:
*      0 = success
*/
STATIC uint8_t
qla2x00_abort_isp(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	uint16_t       cnt;
	srb_t          *sp;
	uint8_t        status = 0;

	ENTER("qla2x00_abort_isp");

	if (ha->flags.online) {
		ha->flags.online = FALSE;
		clear_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
		clear_bit(COMMAND_WAIT_ACTIVE, &ha->dpc_flags);
		clear_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		qla2x00_stats.ispAbort++;
		ha->total_isp_aborts++;  /* used by ioctl */
		ha->sns_retry_cnt = 0;

		printk(KERN_INFO
		    "qla2x00(%ld): Performing ISP error recovery - ha= %p.\n",
		    ha->host_no,ha);
		qla2x00_reset_chip(ha);

		atomic_set(&ha->loop_down_timer, LOOP_DOWN_TIME);
		if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
			atomic_set(&ha->loop_state, LOOP_DOWN);
			qla2x00_mark_all_devices_lost(ha);
		} 

#if defined(ISP2300)
		/* Return all IP send packets */
		for (cnt = 0; cnt < MAX_SEND_PACKETS; cnt++) {
			if (ha->active_scb_q[cnt] != NULL) {
				/* Via IP callback */
				(*ha->send_completion_routine)
					(ha->active_scb_q[cnt]);

				ha->active_scb_q[cnt] = NULL;
			}
		}
#endif

		spin_lock_irqsave(&ha->hardware_lock, flags);
		/* Requeue all commands in outstanding command list. */
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
			sp = ha->outstanding_cmds[cnt];
			if (sp) {
				ha->outstanding_cmds[cnt] = 0;
				if( ha->actthreads )
					ha->actthreads--;
				sp->lun_queue->out_cnt--;
				if (sp->fclun->io_cnt > 0)
					sp->fclun->io_cnt--;
				sp->fclun->s_time = jiffies - sp->u_start;
				ha->iocb_cnt -= sp->iocb_cnt;

				if (sp->flags & SRB_TAPE) {
					CMD_RESULT(sp->cmd) =
					    DID_NO_CONNECT << 16;
				} else {
					/* 
					 * Set the cmd host_byte status
					 * depending on whether the
					 * scsi_error_handler is active or not.
					 */
					if (ha->host->eh_active != EH_ACTIVE)
						CMD_RESULT(sp->cmd) =
						    DID_BUS_BUSY << 16;
					else
						CMD_RESULT(sp->cmd) =
						    DID_RESET << 16;
				}
				CMD_HANDLE(sp->cmd) = (unsigned char *) NULL;
				add_to_done_queue(ha, sp);
			}
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Get Flash Version */
		qla2x00_get_flash_version(ha);

#if defined(ISP2100)
		qla2100_nvram_config(ha);
#else
		qla2x00_nvram_config(ha);
#endif

		if (!qla2x00_restart_isp(ha)) {
			clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);

			if (!atomic_read(&ha->loop_down_timer)) {
				/*
				 * Issue marker command only when we are going
				 * to start the I/O .
				 */
				ha->marker_needed = 1;
			}

			ha->flags.online = TRUE;

			/* Enable target response to SCSI bus. */
			if (ha->flags.enable_target_mode)
				qla2x00_enable_lun(ha);

#if defined(ISP2300)
			/* Reenable IP support */
			if (ha->flags.enable_ip) {
				set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);
				qla2x00_ip_initialize(ha);
			}
#endif
			/* Enable ISP interrupts. */
			qla2x00_enable_intrs(ha);

			/* v2.19.5b6 Return all commands */
			qla2x00_abort_queues(ha, TRUE);

			/* Restart queues that may have been stopped. */
			qla2x00_restart_queues(ha,TRUE);
			ha->isp_abort_cnt = 0; 
			clear_bit(ISP_ABORT_RETRY, &ha->dpc_flags);
		} else {	/* failed the ISP abort */
			ha->flags.online = TRUE;
			if( test_bit(ISP_ABORT_RETRY, &ha->dpc_flags) ){
				if( ha->isp_abort_cnt == 0 ){
					printk(KERN_WARNING
					"qla2x00(%ld): ISP error recovery failed - "
					"board disabled\n",ha->host_no);
					/* 
					 * The next call disables the board
					 * completely.
					 */
					qla2x00_reset_adapter(ha);
					qla2x00_abort_queues(ha, FALSE);
					ha->flags.online = FALSE;
					clear_bit(ISP_ABORT_RETRY, &ha->dpc_flags);
					status = 0;
				} else { /* schedule another ISP abort */
					ha->isp_abort_cnt--;
					DEBUG(printk("qla%ld: ISP abort - retry remainning %d\n",
					ha->host_no, 
					ha->isp_abort_cnt);)
					status = 1;
				}
			} else {
				ha->isp_abort_cnt = MAX_RETRIES_OF_ISP_ABORT;
				DEBUG(printk( "qla2x00(%ld): ISP error recovery - "
				"retrying (%d) more times\n",ha->host_no,
				ha->isp_abort_cnt);)
				set_bit(ISP_ABORT_RETRY, &ha->dpc_flags);
				status = 1;
			}
		}
		       
	}

	if (status) {
		printk(KERN_INFO
			"qla2x00_abort_isp(%ld): **** FAILED ****\n",
			ha->host_no);
	} else {
		DEBUG(printk(KERN_INFO
				"qla2x00_abort_isp(%ld): exiting.\n",
				ha->host_no);)
	}

	return(status);
}

/*
* qla2x00_init_tgt_map
*      Initializes target map.
*
* Input:
*      ha = adapter block pointer.
*
* Output:
*      TGT_Q initialized
*/
STATIC void
qla2x00_init_tgt_map(scsi_qla_host_t *ha)
{
	uint32_t t;

	ENTER(__func__);

	for (t = 0; t < MAX_TARGETS; t++)
		TGT_Q(ha, t) = (os_tgt_t *) NULL;

	LEAVE(__func__);
}


/**
 * qla2x00_alloc_fcport() - Allocate a generic fcport.
 * @ha: HA context
 * @flags: allocation flags
 *
 * Returns a pointer to the allocated fcport, or NULL, if none available.
 */
STATIC fc_port_t *
qla2x00_alloc_fcport(scsi_qla_host_t *ha, int flags)
{
	fc_port_t *fcport;

	fcport = kmalloc(sizeof(fc_port_t), flags);
	if (fcport == NULL)
		return (fcport);

	/* Setup fcport template structure. */
	memset(fcport, 0, sizeof (fc_port_t));
	fcport->ha = ha;
	fcport->port_type = FCT_UNKNOWN;
	fcport->loop_id = FC_NO_LOOP_ID;
	atomic_set(&fcport->state, FC_DEVICE_DEAD);
	fcport->flags = FC_SUPPORT_RPT_LUNS;
	fcport->dev_id = MAX_TARGETS;
	INIT_LIST_HEAD(&fcport->fcluns);

	return (fcport);
}


/*
* qla2x00_reset_adapter
*      Reset adapter.
*
* Input:
*      ha = adapter block pointer.
*/
STATIC void
qla2x00_reset_adapter(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t *reg = ha->iobase;

	ENTER(__func__);

	ha->flags.online = FALSE;
	qla2x00_disable_intrs(ha);
	/* WRT_REG_WORD(&reg->ictrl, 0); */
	/* Reset RISC processor. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		struct device_reg_24xx *reg24 =
		    (struct device_reg_24xx *)ha->iobase;
		WRT_REG_DWORD(&reg24->hccr, HCCRX_SET_RISC_RESET);
		RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */
		WRT_REG_DWORD(&reg24->hccr, HCCRX_REL_RISC_PAUSE);
		RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */
	} else 
#endif
	{
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
		PCI_POSTING(&reg->host_cmd);
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
		PCI_POSTING(&reg->host_cmd);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE(__func__);
}

/*
* qla2x00_loop_reset
*      Issue loop reset.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
STATIC uint8_t
qla2x00_loop_reset(scsi_qla_host_t *ha)
{
	uint8_t  status = QL_STATUS_SUCCESS;
	uint16_t t;
	os_tgt_t        *tq;

	ENTER(__func__);

	if (ha->flags.enable_lip_reset) {
		status = qla2x00_lip_reset(ha);
	}

	if (status == QL_STATUS_SUCCESS && ha->flags.enable_target_reset) {
		for (t = 0; t < MAX_FIBRE_DEVICES; t++) {
			if ((tq = TGT_Q(ha, t)) == NULL)
				continue;

			if (tq->vis_port == NULL)
				continue;

			status = qla2x00_target_reset(ha, 0, t);
		}
	}

	if ((!ha->flags.enable_target_reset && !ha->flags.enable_lip_reset) ||
		ha->flags.enable_lip_full_login) {

		status = qla2x00_full_login_lip(ha);
	}

	/* Issue marker command only when we are going to start the I/O */
	ha->marker_needed = 1;

	if (status) {
		/* Empty */
		DEBUG2_3(printk(KERN_INFO "%s(%ld): **** FAILED ****\n",
				__func__,
				ha->host_no);)
	} else {
		/* Empty */
		DEBUG3(printk("%s(%ld): exiting normally.\n",
				__func__,
				ha->host_no);)
	}

	LEAVE(__func__);

	return(status);
}

/*
 * qla2x00_device_reset
 *	Issue bus device reset message to the target.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Context:
 *	Kernel context.
 */
STATIC int
qla2x00_device_reset(scsi_qla_host_t *ha, fc_port_t *reset_fcport)
{
	uint8_t		status = 0;

	/* Abort Target command will clear Reservation */
	status = qla2x00_abort_target(reset_fcport);

	return( status );
}

/*
 *  Issue marker command.
 *	Function issues marker IOCB.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop ID
 *	lun = LUN
 *	type = marker modifier
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
STATIC int
__qla2x00_marker(scsi_qla_host_t *ha, uint16_t loop_id, 
		uint16_t lun, uint8_t type)
{
	mrk_entry_t	*pkt;
	struct mrk_entry_24xx *mrk24;

	ENTER(__func__);

        mrk24 = NULL;
	pkt = (mrk_entry_t *)qla2x00_req_pkt(ha);
	if (pkt == NULL) {
		DEBUG2_3(printk(KERN_INFO "%s(): *** FAILED ***\n", __func__);)
		return (QLA2X00_FUNCTION_FAILED);
	}

	pkt->entry_type = MARKER_TYPE;
	pkt->modifier = type;

	if (type != MK_SYNC_ALL) {
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			mrk24 = (struct mrk_entry_24xx *) pkt;
                        mrk24->nport_handle = cpu_to_le16(loop_id);
                        mrk24->lun[1] = LSB(lun);
                        mrk24->lun[2] = MSB(lun);
		} else {
			pkt->lun = cpu_to_le16(lun);
#if defined(EXTENDED_IDS)
			pkt->target = cpu_to_le16(loop_id);
#else
			pkt->target = (uint8_t)loop_id;
#endif
		}
	}
	wmb();
	DEBUG2(qla2x00_dump_buffer((uint8_t *)pkt,
					REQUEST_ENTRY_SIZE);)

	/* Issue command to ISP */
	qla2x00_isp_cmd(ha);

	LEAVE(__func__);

	return (QLA2X00_SUCCESS);
}


/**
 * qla2x00_check_request_ring() - Checks request ring for additional IOCB space.
 * @ha: HA context
 * @tot_iocbs: Number of IOCBs required
 * @req_ring_index: Current index to request ring
 * @req_q_cnt: Number of free request entries
 *
 * Returns non-zero if no additional room available on request ring, else zero.
 */
static inline uint16_t
qla2x00_check_request_ring(
		scsi_qla_host_t *ha, uint16_t tot_iocbs,
		uint16_t req_ring_index, uint16_t *req_q_cnt)
{
	uint16_t	status;
	uint16_t	cnt;
	device_reg_t	*reg;

	reg = ha->iobase;

	/*
	 * If room for request in request ring for at least N IOCB
	 */
	status = 0;
	if ((tot_iocbs + 2) >= *req_q_cnt) {
		/*
		 * Calculate number of free request entries.
		 */
#if defined(ISP2100) || defined(ISP2200)
		cnt = RD_REG_WORD(&reg->mailbox4);
#else
		cnt = RD_REG_WORD(&reg->req_q_out);
#endif
		if (req_ring_index < cnt)
			*req_q_cnt = cnt - req_ring_index;
		else
			*req_q_cnt = REQUEST_ENTRY_CNT - (req_ring_index - cnt);
	}
	if ((tot_iocbs + 2) >= *req_q_cnt) {
		DEBUG5(printk("%s(): in-ptr=%x req_q_cnt=%x tot_iocbs=%x.\n",
				__func__,
				req_ring_index,
				*req_q_cnt,
				tot_iocbs);)

		status = 1;
	}
	if ((ha->iocb_cnt + tot_iocbs) >= ha->iocb_hiwat) {
		DEBUG5(printk("%s(): Not Enough IOCBS for request. "
				"iocb_cnt=%x, tot_iocbs=%x, hiwat=%x.\n",
				__func__,
				ha->iocb_cnt,
				tot_iocbs,
				ha->iocb_hiwat);)
#if defined(IOCB_HIT_RATE)
		ha->iocb_overflow_cnt++;
#endif
		status = 1;
	}
	return (status);
}

/**
 * qla2x00_prep_cont_packet() - Initialize a continuation packet.
 * @ha: HA context
 * @req_ring_index: Current index to request ring
 * @req_ring_ptr: Current pointer to request ring
 *
 * Returns a pointer to the continuation packet.
 */
static inline cont_entry_t *
qla2x00_prep_cont_packet(
		scsi_qla_host_t *ha,
		uint16_t *req_ring_index, request_t **request_ring_ptr)
{
	cont_entry_t *cont_pkt;

	/* Adjust ring index. */
	*req_ring_index += 1;
	if (*req_ring_index == REQUEST_ENTRY_CNT) {
		*req_ring_index = 0;
		*request_ring_ptr = ha->request_ring;
	} else
		*request_ring_ptr += 1;

	cont_pkt = (cont_entry_t *)(*request_ring_ptr);

	/* Load packet defaults. */
	*((uint32_t *)(&cont_pkt->entry_type)) =
		__constant_cpu_to_le32(CONTINUE_TYPE);
	//cont_pkt->entry_type = CONTINUE_TYPE;
	//cont_pkt->entry_count = 0;
	//cont_pkt->sys_define = (uint8_t)req_ring_index;

	return (cont_pkt);
}

/**
 * qla2x00_prep_a64_cont_packet() - Initialize an A64 continuation packet.
 * @ha: HA context
 * @req_ring_index: Current index to request ring
 * @req_ring_ptr: Current pointer to request ring
 *
 * Returns a pointer to the continuation packet.
 */
static inline cont_a64_entry_t *
qla2x00_prep_a64_cont_packet(
		scsi_qla_host_t *ha,
		uint16_t *req_ring_index, request_t **request_ring_ptr)
{
	cont_a64_entry_t *cont_pkt;

	/* Adjust ring index. */
	*req_ring_index += 1;
	if (*req_ring_index == REQUEST_ENTRY_CNT) {
		*req_ring_index = 0;
		*request_ring_ptr = ha->request_ring;
	} else
		*request_ring_ptr += 1;

	cont_pkt = (cont_a64_entry_t *)(*request_ring_ptr);

	/* Load packet defaults. */
	*((uint32_t *)(&cont_pkt->entry_type)) =
		__constant_cpu_to_le32(CONTINUE_A64_TYPE);
	//cont_pkt->entry_type = CONTINUE_A64_TYPE;
	//cont_pkt->entry_count = 0;
	//cont_pkt->sys_define = (uint8_t)req_ring_index;

	return (cont_pkt);
}

/**
 * qla2x00_64bit_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occured, else zero.
 */
STATIC uint8_t
qla2x00_64bit_start_scsi(srb_t *sp)
{
	unsigned long   flags;
	uint16_t        failed;
	scsi_qla_host_t	*ha;
	fc_lun_t	*fclun;
	Scsi_Cmnd	*cmd;
	uint16_t	req_q_cnt;
	uint16_t	req_ring_index;
	request_t	*request_ring_ptr;
	uint32_t	*clr_ptr;
	uint32_t	found;
	uint32_t        index;
	uint32_t	handle;
	uint16_t	tot_iocbs;
	uint16_t	tot_dsds;
	uint16_t	avail_dsds;
	uint32_t	*cur_dsd;
	uint16_t        cdb_len;
	uint8_t		*cdb;
	cmd_a64_entry_t		*cmd_pkt;
	cont_a64_entry_t	*cont_pkt;
	uint32_t        timeout;

	device_reg_t	*reg;

	ENTER(__func__);

	/* Setup device pointers. */
	//fclun = sp->lun_queue->fclun;
	fclun = sp->fclun;
	ha = fclun->fcport->ha;

	cmd = sp->cmd;
	reg = ha->iobase;

	DEBUG3(printk("64bit_start: cmd=%p sp=%p CDB=%x\n",
			cmd, sp, cmd->cmnd[0]);)

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if(qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA2X00_SUCCESS) {
			return (1);
		}
		ha->marker_needed = 0;
	}


	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Save ha fields for post-update */
	req_ring_index = ha->req_ring_index;
	request_ring_ptr = ha->request_ring_ptr;
	req_q_cnt = ha->req_q_cnt;

	tot_dsds = 0;
	tot_iocbs = 1;

	/* Allocate space for an additional IOCB */
	failed = qla2x00_check_request_ring(ha,
			tot_iocbs, req_ring_index, &req_q_cnt);
	if (failed)
		goto queuing_error_64;

	/* Check for room in outstanding command list. */
	found = 0;
	handle = ha->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (ha->outstanding_cmds[handle] == 0) {
			found = 1;
			ha->current_outstanding_cmd = handle;
			break;
		}
	}
	if (!found) {
		DEBUG5(printk("%s(): NO ROOM IN OUTSTANDING ARRAY. "
				"req_q_cnt=%lx.\n",
				__func__,
				(u_long)ha->req_q_cnt);)
		goto queuing_error_64;
	}

	/*
	 * Build command packet.
	 */
	cmd_pkt = request_ring_ptr;

	*((uint32_t *)(&cmd_pkt->entry_type)) = 
			 __constant_cpu_to_le32(COMMAND_A64_TYPE);
	//cmd_pkt->entry_type = COMMAND_A64_TYPE;
	//cmd_pkt->entry_count = (uint8_t)tot_iocbs;
	//cmd_pkt->sys_define = (uint8_t)ha->req_ring_index;
	//cmd_pkt->entry_status = 0;

	cmd_pkt->handle = handle;

	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	for (index = 2; index < REQUEST_ENTRY_SIZE / 4; index++)
		*clr_ptr++ = 0;

	/* Two DSDs are available in the command IOCB */
	avail_dsds = 2;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Set target ID */
#if defined(EXTENDED_IDS)
        cmd_pkt->target = cpu_to_le16(fclun->fcport->loop_id);
#else
        cmd_pkt->target = (uint8_t)fclun->fcport->loop_id;
#endif

	/* Set LUN number*/
#if VSA
	if ((cmd->cmnd[0] == 0x26) || (cmd->cmnd[0] == 0xA0) ||
	    (cmd->cmnd[0] == 0xCB) ) {
		cmd_pkt->lun = cpu_to_le16(fclun->lun);
	} else if ((fclun->fcport->flags & FC_VSA))
		cmd_pkt->lun = cpu_to_le16(fclun->lun | 0x4000);
	else
		cmd_pkt->lun = cpu_to_le16(fclun->lun);
#else
	cmd_pkt->lun = cpu_to_le16(fclun->lun);
#endif

	/* Update tagged queuing modifier */
	cmd_pkt->control_flags = __constant_cpu_to_le16(CF_SIMPLE_TAG);
	if (cmd->device->tagged_queue) {
		switch (cmd->tag) {
			case HEAD_OF_QUEUE_TAG:
				cmd_pkt->control_flags =
					__constant_cpu_to_le16(CF_HEAD_TAG);
				break;
			case ORDERED_QUEUE_TAG:
				cmd_pkt->control_flags =
					__constant_cpu_to_le16(CF_ORDERED_TAG);
				break;
		}
	}

	/*
	 * Allocate at least 5 (+ QLA_CMD_TIMER_DELTA) seconds for RISC timeout.
	 */
	timeout = (uint32_t) CMD_TIMEOUT(cmd)/HZ;
	if (timeout > 65535)
		cmd_pkt->timeout = __constant_cpu_to_le16(0);
	if (timeout > 25)
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout -
				(5 + QLA_CMD_TIMER_DELTA));
	else
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout);

	/* Load SCSI command packet. */
	cdb_len = (uint16_t)CMD_CDBLEN(cmd);
	if (cdb_len > MAX_COMMAND_SIZE)
		cdb_len = MAX_COMMAND_SIZE;
	cdb = (uint8_t *) &(CMD_CDBP(cmd));
	memcpy(cmd_pkt->scsi_cdb, cdb, cdb_len);
	if (sp->cmd_length > MAX_COMMAND_SIZE) {
		for (index = MAX_COMMAND_SIZE; index < MAX_CMDSZ; index++) {
			cmd_pkt->scsi_cdb[index] =
				sp->more_cdb[index - MAX_COMMAND_SIZE];
		}
	}

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	if (cmd->request_bufflen == 0 ||
	    cmd->sc_data_direction == SCSI_DATA_NONE) {
		/* No data transfer */
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		DEBUG5(printk("%s(): No data, command packet data - "
				"b%dt%dd%d\n",
				__func__,
				(uint32_t)SCSI_BUS_32(cmd),
				(uint32_t)SCSI_TCN_32(cmd),
				(uint32_t)SCSI_LUN_32(cmd));)
		DEBUG5(qla2x00_dump_buffer((uint8_t *)cmd_pkt,
						REQUEST_ENTRY_SIZE);)
	} else {
		switch (cmd->cmnd[0]) {
			case FORMAT_UNIT:
			case WRITE_6:
			case MODE_SELECT:
			case SEND_DIAGNOSTIC:
			case WRITE_10:
			case WRITE_BUFFER:
			case WRITE_LONG:
			case WRITE_SAME:
			case MODE_SELECT_10:
			case WRITE_12:
			case WRITE_VERIFY:
			case WRITE_VERIFY_12:
			case SEND_VOLUME_TAG:
				cmd_pkt->control_flags |=
					__constant_cpu_to_le16(CF_WRITE);
				break;
			default:
				if (cmd->sc_data_direction == SCSI_DATA_WRITE)
					cmd_pkt->control_flags |=
					   __constant_cpu_to_le16(CF_WRITE);
				else
					cmd_pkt->control_flags |=
					   __constant_cpu_to_le16(CF_READ);
				break;
		}

		sp->dir = cmd_pkt->control_flags &
				  __constant_cpu_to_le16(CF_READ | CF_WRITE);

		/* Load data segments */
		if (cmd->use_sg != 0) {
			struct	scatterlist *cur_seg;
			struct	scatterlist *end_seg;
			int	nseg;

			cur_seg = (struct scatterlist *)cmd->request_buffer;
			nseg = pci_map_sg(ha->pdev, cur_seg, cmd->use_sg,
				scsi_to_pci_dma_dir(cmd->sc_data_direction));
			if (nseg == 0)
				goto queuing_error_64;
			end_seg = cur_seg + nseg;

			while (cur_seg < end_seg) {
				dma_addr_t	sle_dma;
				uint32_t	sle_len;
				dma_addr_t	nml_dma;
				uint32_t	nml_len;
				uint32_t	normalized;

				/* Allocate additional continuation packets? */
				if (avail_dsds == 0) {
					tot_iocbs++;
					failed = qla2x00_check_request_ring(ha,
							tot_iocbs,
							req_ring_index,
							&req_q_cnt);
					if (failed) {
						goto mapped_queuing_error_64;
					}

					cont_pkt = qla2x00_prep_a64_cont_packet(
							ha,
							&req_ring_index,
							&request_ring_ptr);

					cur_dsd = (uint32_t *)
						&cont_pkt->dseg_0_address;
					avail_dsds = 5;
				}

				sle_dma = sg_dma_address(cur_seg);
				sle_len = sg_dma_len(cur_seg);

				normalized = qla2x00_normalize_dma_addr(
						&sle_dma, &sle_len,
						&nml_dma, &nml_len);

				/* One entry always consumed */
				*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
				*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
				*cur_dsd++ = cpu_to_le32(sle_len);
				tot_dsds++;
				avail_dsds--;

				if (normalized) {
					/*
					 * Allocate additional continuation
					 * packets?
					 */
					if (avail_dsds == 0) {
						tot_iocbs++;
						failed =
						  qla2x00_check_request_ring(ha,
								tot_iocbs,
								req_ring_index,
								&req_q_cnt);
						if (failed)
							goto
							   mapped_queuing_error_64;

						cont_pkt =
						  qla2x00_prep_a64_cont_packet(
							ha,
							&req_ring_index,
							&request_ring_ptr);

						cur_dsd = (uint32_t *)
						  &cont_pkt->dseg_0_address;
						avail_dsds = 5;
					}

					*cur_dsd++ = cpu_to_le32(LSD(nml_dma));
					*cur_dsd++ = cpu_to_le32(MSD(nml_dma));
					*cur_dsd++ = cpu_to_le32(nml_len);
					tot_dsds++;
					avail_dsds--;
				}
				cur_seg++;
			}
		}
		else {
			/*
			 * No more than 1 (one) IOCB is needed for this type
			 * of request, even if the DMA address spans the 4GB
			 * page boundary.
			 *
			 * @tot_dsds == 1 if non-spanning, else 2
			 */
			dma_addr_t	req_dma;
			uint32_t	req_len;
			dma_addr_t	nml_dma;
			uint32_t	nml_len;
			uint32_t	normalized;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
			struct page *page = virt_to_page(cmd->request_buffer);
			unsigned long offset = ((unsigned long)
						 cmd->request_buffer 
						 & ~PAGE_MASK);

			req_dma = pci_map_page(ha->pdev,
					page,
					offset,
					cmd->request_bufflen,
					scsi_to_pci_dma_dir(
					cmd->sc_data_direction));
#else
			req_dma = pci_map_single(ha->pdev,
					cmd->request_buffer,
					cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						cmd->sc_data_direction));
#endif
			req_len = cmd->request_bufflen;

			sp->saved_dma_handle = req_dma;

			normalized = qla2x00_normalize_dma_addr(
					&req_dma, &req_len,
					&nml_dma, &nml_len);

			/* One entry always consumed */
			*cur_dsd++ = cpu_to_le32(LSD(req_dma));
			*cur_dsd++ = cpu_to_le32(MSD(req_dma));
			*cur_dsd++ = cpu_to_le32(req_len);
			tot_dsds++;

			if (normalized) {
				*cur_dsd++ = cpu_to_le32(LSD(nml_dma));
				*cur_dsd++ = cpu_to_le32(MSD(nml_dma));
				*cur_dsd++ = cpu_to_le32(nml_len);
				tot_dsds++;
			}

		}
	}

	/* Set total data segment count. */
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);
	cmd_pkt->entry_count = (uint8_t)tot_iocbs;
	wmb();

	/* Update ha fields */
	ha->req_ring_index = req_ring_index;
	ha->request_ring_ptr = request_ring_ptr;
	ha->req_q_cnt = req_q_cnt;
	ha->req_q_cnt -= tot_iocbs;
	ha->iocb_cnt += tot_iocbs;

	sp->iocb_cnt = tot_iocbs;

	/* Add command to the active array */
	ha->outstanding_cmds[handle] = sp;
	CMD_HANDLE(sp->cmd) = (unsigned char *)(u_long)handle;

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	ha->actthreads++;
	ha->total_ios++;

	if (cmd_pkt->control_flags & __constant_cpu_to_le16(CF_WRITE) &&
	    cmd->request_bufflen != 0) {
		ha->total_output_cnt++;
	} else if (cmd_pkt->control_flags & __constant_cpu_to_le16(CF_READ)) {
		ha->total_input_cnt++;
	} else {
		ha->total_ctrl_cnt++;
	}

	sp->ha = ha;
	sp->lun_queue->out_cnt++;
	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;
	sp->u_start = jiffies;

	/* Set chip new ring index. */
#if WATCH_THREADS_SIZE
	DEBUG3(printk("%s(): actthreads=%ld.\n", 
			__func__,
			ha->actthreads);)
#endif

#if defined(ISP2100) || defined(ISP2200)
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
	PCI_POSTING(&reg->mailbox4);
#else
	WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
	PCI_POSTING(&reg->req_q_in);
#endif

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (0);

mapped_queuing_error_64:
	pci_unmap_sg(ha->pdev, (struct scatterlist *)cmd->request_buffer,
		cmd->use_sg, scsi_to_pci_dma_dir(cmd->sc_data_direction));

queuing_error_64:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (1);
}

/*
* qla2x00_32bit_start_scsi
*      The start SCSI is responsible for building request packets on
*      request ring and modifying ISP input pointer.
*
*      The QLogic firmware interface allows every queue slot to have a SCSI
*      command and up to 4 scatter/gather (SG) entries.  If we need more
*      than 4 SG entries, then continuation entries are used that can
*      hold another 7 entries each.  The start routine determines if there
*      is eought empty slots then build the combination of requests to
*      fulfill the OS request.
*
* Input:
*      ha = adapter block pointer.
*      sp = SCSI Request Block structure pointer.
*
* Returns:
*      0 = success, was able to issue command.
*/
STATIC uint8_t
qla2x00_32bit_start_scsi(srb_t *sp)
{
	unsigned long   flags;
	uint16_t        failed;
	scsi_qla_host_t	*ha;
	fc_lun_t	*fclun;
	Scsi_Cmnd	*cmd;
	uint16_t	req_q_cnt;
	uint16_t	req_ring_index;
	request_t	*request_ring_ptr;
	uint32_t	*clr_ptr;
	uint32_t	found;
	uint32_t        index;
	uint32_t	handle;
	uint16_t	tot_iocbs;
	uint16_t	tot_dsds;
	uint16_t	avail_dsds;
	uint32_t	*cur_dsd;
	uint16_t        cdb_len;
	uint8_t		*cdb;
	cmd_entry_t	*cmd_pkt;
	cont_entry_t	*cont_pkt;
	uint32_t        timeout;

	device_reg_t	*reg;

	ENTER(__func__);

	/* Setup device pointers. */
	//fclun = sp->lun_queue->fclun;
	fclun = sp->fclun;
	ha = fclun->fcport->ha;

	cmd = sp->cmd;
	reg = ha->iobase;

	DEBUG3(printk("32bit_start: cmd=%p sp=%p CDB=%x\n",
			cmd,
			sp,
			cmd->cmnd[0]);)

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if(qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA2X00_SUCCESS) {
			return (1);
		}
		ha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Save ha fields for post-update */
	req_ring_index = ha->req_ring_index;
	request_ring_ptr = ha->request_ring_ptr;
	req_q_cnt = ha->req_q_cnt;

	tot_dsds = 0;
	tot_iocbs = 1;

	/* Allocate space for an additional IOCB */
	failed = qla2x00_check_request_ring(ha,
			tot_iocbs, req_ring_index, &req_q_cnt);
	if (failed)
		goto queuing_error_32;

	/* Check for room in outstanding command list. */
	found = 0;
	handle = ha->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (ha->outstanding_cmds[handle] == 0) {
			found = 1;
			ha->current_outstanding_cmd = handle;
			break;
		}
	}
	if (!found) {
		DEBUG5(printk("%s(): NO ROOM IN OUTSTANDING ARRAY. "
				"req_q_cnt=%lx.\n",
				__func__,
				(u_long)ha->req_q_cnt);)
		goto queuing_error_32;
	}

	/*
	 * Build command packet.
	 */
	cmd_pkt = (cmd_entry_t *)request_ring_ptr;

	*((uint32_t *)(&cmd_pkt->entry_type)) = 
			 __constant_cpu_to_le32(COMMAND_TYPE);
	//cmd_pkt->entry_type = COMMAND_TYPE;
	//cmd_pkt->entry_count = (uint8_t)tot_iocbs;
	//cmd_pkt->sys_define = (uint8_t)ha->req_ring_index;
	//cmd_pkt->entry_status = 0;

	cmd_pkt->handle = handle;

	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	for (index = 2; index < REQUEST_ENTRY_SIZE / 4; index++)
		*clr_ptr++ = 0;

	/* Three DSDs are available in the command IOCB */
	avail_dsds = 3;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Set target ID */
#if defined(EXTENDED_IDS)
	cmd_pkt->target = cpu_to_le16(fclun->fcport->loop_id);
#else
	cmd_pkt->target = (uint8_t)fclun->fcport->loop_id;
#endif

	/* Set LUN number*/
#if VSA
	if ((cmd->cmnd[0] == 0x26) || (cmd->cmnd[0] == 0xA0) ||
	    (cmd->cmnd[0] == 0xCB) ) {
		cmd_pkt->lun = cpu_to_le16(fclun->lun);
	} else if ((fclun->fcport->flags & FC_VSA))
		cmd_pkt->lun = cpu_to_le16(fclun->lun | 0x4000);
	else
		cmd_pkt->lun = cpu_to_le16(fclun->lun);
#else
	cmd_pkt->lun = cpu_to_le16(fclun->lun);
#endif

	/* Update tagged queuing modifier */
	cmd_pkt->control_flags = __constant_cpu_to_le16(CF_SIMPLE_TAG);
	if (cmd->device->tagged_queue) {
		switch (cmd->tag) {
			case HEAD_OF_QUEUE_TAG:
				cmd_pkt->control_flags =
					__constant_cpu_to_le16(CF_HEAD_TAG);
				break;
			case ORDERED_QUEUE_TAG:
				cmd_pkt->control_flags =
					__constant_cpu_to_le16(CF_ORDERED_TAG);
				break;
		}
	}

	/*
	 * Allocate at least 5 (+ QLA_CMD_TIMER_DELTA) seconds for RISC timeout.
	 */
	timeout = (uint32_t) CMD_TIMEOUT(cmd)/HZ;
	if (timeout > 65535)
		cmd_pkt->timeout = __constant_cpu_to_le16(0);
	if (timeout > 25)
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout -
				(5 + QLA_CMD_TIMER_DELTA));
	else
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout);

	/* Load SCSI command packet. */
	cdb_len = (uint16_t)CMD_CDBLEN(cmd);
	if (cdb_len > MAX_COMMAND_SIZE)
		cdb_len = MAX_COMMAND_SIZE;
	cdb = (uint8_t *) &(CMD_CDBP(cmd));
	memcpy(cmd_pkt->scsi_cdb, cdb, cdb_len);
	if (sp->cmd_length > MAX_COMMAND_SIZE) {
		for (index = MAX_COMMAND_SIZE; index < MAX_CMDSZ; index++) {
			cmd_pkt->scsi_cdb[index] =
				sp->more_cdb[index - MAX_COMMAND_SIZE];
		}
	}

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	if (cmd->request_bufflen == 0 ||
		cmd->sc_data_direction == SCSI_DATA_NONE) {
		/* No data transfer */
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		DEBUG5(printk("%s(): No data, command packet data - "
				"b%dt%dd%d\n",
				__func__,
				(uint32_t)SCSI_BUS_32(cmd),
				(uint32_t)SCSI_TCN_32(cmd),
				(uint32_t)SCSI_LUN_32(cmd));)
		DEBUG5(qla2x00_dump_buffer((uint8_t *)cmd_pkt,
						REQUEST_ENTRY_SIZE);)
	} else {
		switch (cmd->cmnd[0]) {
			case FORMAT_UNIT:
			case WRITE_6:
			case MODE_SELECT:
			case SEND_DIAGNOSTIC:
			case WRITE_10:
			case WRITE_BUFFER:
			case WRITE_LONG:
			case WRITE_SAME:
			case MODE_SELECT_10:
			case WRITE_12:
			case WRITE_VERIFY:
			case WRITE_VERIFY_12:
			case SEND_VOLUME_TAG:
				cmd_pkt->control_flags |=
					__constant_cpu_to_le16(CF_WRITE);
				break;
			default:
				if (cmd->sc_data_direction == SCSI_DATA_WRITE)
					cmd_pkt->control_flags |=
					   __constant_cpu_to_le16(CF_WRITE);
				else
					cmd_pkt->control_flags |=
					   __constant_cpu_to_le16(CF_READ);
				break;
		}
		sp->dir = cmd_pkt->control_flags &
				  __constant_cpu_to_le16(CF_READ | CF_WRITE);

		/* Load data segments */
		if (cmd->use_sg != 0) {
			struct	scatterlist *cur_seg;
			struct	scatterlist *end_seg;
			int	nseg;

			cur_seg = (struct scatterlist *)cmd->request_buffer;
			nseg = pci_map_sg(ha->pdev, cur_seg, cmd->use_sg,
				scsi_to_pci_dma_dir(cmd->sc_data_direction));
			if (nseg == 0)
				goto queuing_error_32;
			end_seg = cur_seg + nseg;

			while (cur_seg < end_seg) {
				/* Allocate additional continuation packets? */
				if (avail_dsds == 0) {
					tot_iocbs++;
					failed = qla2x00_check_request_ring(ha,
							tot_iocbs,
							req_ring_index,
							&req_q_cnt);
					if (failed) {
						goto mapped_queuing_error_32;
					}

					cont_pkt = qla2x00_prep_cont_packet(
							ha,
							&req_ring_index,
							&request_ring_ptr);

					cur_dsd = (uint32_t *)
						&cont_pkt->dseg_0_address;
					avail_dsds = 7;
				}

				/* One entry always consumed */
				*cur_dsd++ =
				    cpu_to_le32(sg_dma_address(cur_seg));
				*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
				tot_dsds++;
				avail_dsds--;

				cur_seg++;
			}
		}
		else {
			/*
			 * No more than 1 (one) IOCB is needed for this type
			 * of request.
			 */
			dma_addr_t	req_dma;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,13)
			struct page *page = virt_to_page(cmd->request_buffer);
			unsigned long offset = ((unsigned long)
						 cmd->request_buffer 
						 & ~PAGE_MASK);

			req_dma = pci_map_page(ha->pdev,
					page,
					offset,
					cmd->request_bufflen,
					scsi_to_pci_dma_dir(
					cmd->sc_data_direction));
#else
			req_dma = pci_map_single(ha->pdev,
					cmd->request_buffer,
					cmd->request_bufflen,
					scsi_to_pci_dma_dir(
						cmd->sc_data_direction));
#endif
			sp->saved_dma_handle = req_dma;

			/* One entry always consumed */
			*cur_dsd++ = cpu_to_le32(req_dma);
			*cur_dsd++ = cpu_to_le32(cmd->request_bufflen);
			tot_dsds++;
		}
	}

	/* Set total data segment count. */
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);
	cmd_pkt->entry_count = (uint8_t)tot_iocbs;
	wmb();

	/* Update ha fields */
	ha->req_ring_index = req_ring_index;
	ha->request_ring_ptr = request_ring_ptr;
	ha->req_q_cnt = req_q_cnt;
	ha->req_q_cnt -= tot_iocbs;
	ha->iocb_cnt += tot_iocbs;

	sp->iocb_cnt = tot_iocbs;

	/* Add command to the active array */
	ha->outstanding_cmds[handle] = sp;
	CMD_HANDLE(sp->cmd) = (unsigned char *)(u_long)handle;

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	ha->actthreads++;
	ha->total_ios++;

	if (cmd_pkt->control_flags & __constant_cpu_to_le16(CF_WRITE) &&
	    cmd->request_bufflen != 0) {
		ha->total_output_cnt++;
	} else if (cmd_pkt->control_flags & __constant_cpu_to_le16(CF_READ)) {
		ha->total_input_cnt++;
	} else {
		ha->total_ctrl_cnt++;
	}

	sp->ha = ha;
	sp->lun_queue->out_cnt++;
	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;
	sp->u_start = jiffies;

	/* Set chip new ring index. */
#if WATCH_THREADS_SIZE
	DEBUG3(printk("%s(): actthreads=%ld.\n",
			__func__,
			ha->actthreads);)
#endif

#if defined(ISP2100) || defined(ISP2200)
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
	PCI_POSTING(&reg->mailbox4);
#else
	WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
	PCI_POSTING(&reg->req_q_in);
#endif

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (0);

mapped_queuing_error_32:
	pci_unmap_sg(ha->pdev, (struct scatterlist *)cmd->request_buffer,
		cmd->use_sg, scsi_to_pci_dma_dir(cmd->sc_data_direction));

queuing_error_32:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (1);
}

#if defined(ISP2300)
/**
 * qla2x00_prep_cont_type1_iocb() - Initialize a Continuation Type 1 IOCB.
 * @ha: HA context
 *
 * Returns a pointer to the continuation type 1 IOCB packet.
 */
static inline cont_a64_entry_t *
qla2x00_prep_cont_type1_iocb(scsi_qla_host_t *ha)
{
	cont_a64_entry_t *cont_pkt;

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT_24XX) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else {
		ha->request_ring_ptr++;
	}

	cont_pkt = (cont_a64_entry_t *)ha->request_ring_ptr;

	/* Load packet defaults. */
	*((uint32_t *)(&cont_pkt->entry_type)) =
	    __constant_cpu_to_le32(CONTINUE_A64_TYPE);

	return (cont_pkt);
}
/**
 * qla24xx_calc_iocbs() - Determine number of Command Type 3 and
 * Continuation Type 1 IOCBs to allocate.
 *
 * @dsds: number of data segment decriptors needed
 *
 * Returns the number of IOCB entries needed to store @dsds.
 */
static inline uint16_t
qla24xx_calc_iocbs(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 1) {
		iocbs += (dsds - 1) / 5;
		if ((dsds - 1) % 5)
			iocbs++;
	}
	return (iocbs);
}

/**
 * qla24xx_build_scsi_iocbs() - Build IOCB command utilizing Command Type 7
 * IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 3 IOCB
 * @tot_dsds: Total number of segments to transfer
 */
static inline void
qla24xx_build_scsi_iocbs(srb_t *sp, struct cmd_type_7 *cmd_pkt,
    uint16_t tot_dsds)
{
	uint16_t	avail_dsds;
	uint32_t	*cur_dsd;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;

	cmd = sp->cmd;

	/* Update entry type to indicate Command Type 3 IOCB */
	*((uint32_t *)(&cmd_pkt->entry_type)) =
	    __constant_cpu_to_le32(COMMAND_TYPE_7);

	/* No data transfer */
	if (cmd->request_bufflen == 0 || cmd->sc_data_direction 
		== PCI_DMA_NONE) {
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		return;
	}

	ha = sp->ha;

	/* Set transfer direction */
	if (cmd->sc_data_direction == PCI_DMA_TODEVICE)
		cmd_pkt->task_mgmt_flags = 
				    __constant_cpu_to_le16(TMF_WRITE_DATA);
	else if (cmd->sc_data_direction == PCI_DMA_FROMDEVICE)
		cmd_pkt->task_mgmt_flags = 
			__constant_cpu_to_le16(TMF_READ_DATA);


	/* One DSD is available in the Command Type 3 IOCB */
	avail_dsds = 1;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Load data segments */
	if (cmd->use_sg != 0) {
		struct	scatterlist *cur_seg;
		struct	scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			dma_addr_t	sle_dma;
			cont_a64_entry_t *cont_pkt;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				/*
				 * Five DSDs are available in the Continuation
				 * Type 1 IOCB.
				 */
				cont_pkt = qla2x00_prep_cont_type1_iocb(ha);
				cur_dsd = (uint32_t *)cont_pkt->dseg_0_address;
				avail_dsds = 5;
			}

			sle_dma = sg_dma_address(cur_seg);
			*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_seg++;
		}
	} else {
		dma_addr_t	req_dma;
		struct page	*page;
		unsigned long	offset;

		page = virt_to_page(cmd->request_buffer);
		offset = ((unsigned long)cmd->request_buffer & ~PAGE_MASK);
		req_dma = pci_map_page(ha->pdev, page, offset,
		    cmd->request_bufflen, cmd->sc_data_direction);

		sp->saved_dma_handle = req_dma;

		*cur_dsd++ = cpu_to_le32(LSD(req_dma));
		*cur_dsd++ = cpu_to_le32(MSD(req_dma));
		*cur_dsd++ = cpu_to_le32(cmd->request_bufflen);
	}
}

/**
 * qla24xx_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occured, else zero.
 */
int
qla24xx_start_scsi(srb_t *sp)
{
	int		ret;
	unsigned long   flags;
	scsi_qla_host_t	*ha;
	fc_lun_t	*fclun;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t        index;
	uint32_t	handle;
	struct cmd_type_7 *cmd_pkt;
	uint32_t        timeout;
	struct scatterlist *sg;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_24xx *reg;

	/* Setup device pointers. */
	ret = 0;
	//fclun = sp->lun_queue->fclun;
	fclun = sp->fclun;
	ha = fclun->fcport->ha;
	reg = (struct device_reg_24xx *)ha->iobase;
	cmd = sp->cmd;

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if (qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA2X00_SUCCESS) {
			return (QLA2X00_FUNCTION_FAILED);
		}
		ha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in outstanding command list. */
	handle = ha->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (ha->outstanding_cmds[handle] == 0)
			break;
	}
	if (index == MAX_OUTSTANDING_COMMANDS)
		goto queuing_error;

	/* Calculate the number of request entries needed. */
	/* Finally, we have enough space, now perform mappings. */
	tot_dsds = 0;
	sg = NULL;
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
	    tot_dsds++;
	}
	req_cnt = qla24xx_calc_iocbs(tot_dsds);
	if (ha->req_q_cnt < (req_cnt + 2)) {
		cnt = (uint16_t) RD_REG_DWORD(&reg->req_q_out);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = REQUEST_ENTRY_CNT_24XX -
			    (ha->req_ring_index - cnt);
	}
	if (ha->req_q_cnt < (req_cnt + 2)) {
		if (cmd->use_sg)
			pci_unmap_sg(ha->pdev, sg,
					cmd->use_sg,
					scsi_to_pci_dma_dir(
						cmd->sc_data_direction));

		goto queuing_error;
	}

	/* Build command packet. */
	ha->current_outstanding_cmd = handle;
	ha->outstanding_cmds[handle] = sp;
	sp->ha = ha;
	sp->cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	ha->req_q_cnt -= req_cnt;

	cmd_pkt = (struct cmd_type_7 *)ha->request_ring_ptr;
	clr_ptr = (uint32_t *)cmd_pkt;
	memset(clr_ptr, 0, sizeof(struct cmd_type_7));
	cmd_pkt->handle = handle;

	cmd_pkt->nport_handle = cpu_to_le16(fclun->fcport->loop_id);
	cmd_pkt->port_id[0] = fclun->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = fclun->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = fclun->fcport->d_id.b.domain;

	/* Update timeout. */
	timeout = (uint32_t)(cmd->timeout_per_command / HZ);
	/* Setting Timeout value > FW_MAX_TIMEOUT is not
	 * allowed by the firmware and will reject
	 * the iocb with invalid parameter.
	 */
	if (timeout > FW_MAX_TIMEOUT)
		cmd_pkt->timeout =
		    __constant_cpu_to_le16(FW_MAX_TIMEOUT);
	else if (timeout > 25)
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout -
		    (5 + QLA_CMD_TIMER_DELTA));
	else
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout);

	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Set LUN number*/
	cmd_pkt->lun[1] = LSB(fclun->lun);
	cmd_pkt->lun[2] = MSB(fclun->lun);
	host_to_fcp_swap(cmd_pkt->lun, sizeof(cmd_pkt->lun));

	/* Update tagged queuing modifier */
	cmd_pkt->task = TSK_SIMPLE;
	if (cmd->device->tagged_queue) {
		switch (cmd->tag) {
			case HEAD_OF_QUEUE_TAG:
				cmd_pkt->task = TSK_HEAD_OF_QUEUE;
				break;
			case ORDERED_QUEUE_TAG:
				cmd_pkt->task = TSK_ORDERED;
				break;
		}
	}
	/* Load SCSI command packet. */
	memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
	host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	/* Build IOCB segments */
	qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT_24XX) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;
	DEBUG3(qla2x00_dump_buffer((uint8_t *)cmd_pkt, REQUEST_ENTRY_SIZE);)
	ha->actthreads++;
	ha->total_ios++;
	sp->lun_queue->out_cnt++;
	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;
	sp->u_start = jiffies;
	/* Set chip new ring index. */
	WRT_REG_DWORD(&reg->req_q_in, ha->req_ring_index);
	RD_REG_DWORD(&reg->req_q_in);		/* PCI Posting. */

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (QLA2X00_SUCCESS);

queuing_error:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (QLA2X00_FUNCTION_FAILED);
}
#endif

/*
* qla2x00_ms_req_pkt
*      Function is responsible for locking ring and
*      getting a zeroed out Managment Server request packet.
*
* Input:
*      ha  = adapter block pointer.
*      sp  = srb_t pointer to handle post function call
* Returns:
*      0 = failed to get slot.
*
* Note: Need to hold the hardware lock before calling this routine.
*/
STATIC request_t *
qla2x00_ms_req_pkt(scsi_qla_host_t *ha, srb_t  *sp)
{
	device_reg_t *reg = ha->iobase;
#if !defined(ISP2100) && !defined(ISP2200)
	struct device_reg_24xx *reg24 =
	    (struct device_reg_24xx *) ha->iobase;
#endif
	request_t    *pkt = 0;
	uint16_t     cnt, i, index;
	uint32_t     *dword_ptr;
	uint32_t     timer;
	uint8_t      found = 0;
	uint16_t     req_cnt = 1;

	ENTER(__func__);

	/* Wait 1 second for slot. */
	for (timer = HZ; timer; timer--) {
		/* Acquire ring specific lock */

		if ((uint16_t)(req_cnt + 2) >= ha->req_q_cnt) {
			/* Calculate number of free request entries. */
#if defined(ISP2100) || defined(ISP2200)
			cnt = qla2x00_debounce_register(&reg->mailbox4);
#else
			if (check_24xx_or_54xx_device_ids(ha) ||
			    check_25xx_device_ids(ha))
				cnt = (uint16_t)RD_REG_DWORD(&reg24->req_q_out);
			else
				cnt = qla2x00_debounce_register(
				    &reg->req_q_out);
#endif

			if (ha->req_ring_index < cnt) {
				ha->req_q_cnt = cnt - ha->req_ring_index;
			} else {
				ha->req_q_cnt = REQUEST_ENTRY_CNT -
				    (ha->req_ring_index - cnt);
			}
		}

		/* Check for room in outstanding command list. */
		cnt = ha->current_outstanding_cmd;
		for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
			cnt++;
			if (cnt == MAX_OUTSTANDING_COMMANDS)
				cnt = 1;

			if (ha->outstanding_cmds[cnt] == 0) {
				found = 1;
				ha->current_outstanding_cmd = cnt;
				break;
			}
		}

		/* If room for request in request ring. */
		if (found && (uint16_t)(req_cnt + 2) < ha->req_q_cnt) {

			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			dword_ptr = (uint32_t *)pkt;
			for( i = 0; i < REQUEST_ENTRY_SIZE/4; i++ )
				*dword_ptr++ = 0;

			DEBUG5(printk("%s(): putting sp=%p in "
			    "outstanding_cmds[%x]\n",
			    __func__, sp,cnt);)

			ha->outstanding_cmds[cnt] = sp;

			/* save the handle */
			CMD_HANDLE(sp->cmd) = (unsigned char *) (u_long) cnt;
			CMD_SP(sp->cmd) = (void *)sp;

			ha->req_q_cnt--;
			pkt->handle = (uint32_t)cnt;

			/* Set system defined field. */
			pkt->sys_define = (uint8_t)ha->req_ring_index;
			pkt->entry_status = 0;

			break;
		}

		/* Release ring specific lock */
		spin_unlock(&ha->hardware_lock);
		udelay(20);

		/* Check for pending interrupts. */
		qla2x00_poll(ha);
		spin_lock_irq(&ha->hardware_lock);
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("%s(): **** FAILED ****\n", __func__);
#endif

	LEAVE(__func__);

	return (pkt);
}

/*
* qla2x00_req_pkt
*      Function is responsible for locking ring and
*      getting a zeroed out request packet.
*
* Input:
*      ha  = adapter block pointer.
*
* Returns:
*      0 = failed to get slot.
*/
STATIC request_t *
qla2x00_req_pkt(scsi_qla_host_t *ha)
{
	device_reg_t *reg = ha->iobase;
	request_t    *pkt = 0;
	uint16_t     cnt;
	uint32_t     *dword_ptr;
	uint32_t     timer;
	uint16_t     req_cnt = 1;

	ENTER(__func__);

	/* Wait 1 second for slot. */
	for (timer = HZ; timer; timer--) {
		/* Acquire ring specific lock */

		if ((uint16_t)(req_cnt + 2) >= ha->req_q_cnt) {
			/* Calculate number of free request entries. */
#if defined(ISP2100) || defined(ISP2200)
			cnt = qla2x00_debounce_register(&reg->mailbox4);
#else
			cnt = qla2x00_debounce_register(&reg->req_q_out);
#endif
			if  (ha->req_ring_index < cnt)
				ha->req_q_cnt = cnt - ha->req_ring_index;
			else
				ha->req_q_cnt = REQUEST_ENTRY_CNT - 
					(ha->req_ring_index - cnt);
		}
		/* If room for request in request ring. */
		if ((uint16_t)(req_cnt + 2) < ha->req_q_cnt) {
			ha->req_q_cnt--;
			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			dword_ptr = (uint32_t *)pkt;
			for (cnt = 0; cnt < REQUEST_ENTRY_SIZE/4; cnt++)
				*dword_ptr++ = 0;

			/* Set system defined field. */
			pkt->sys_define = (uint8_t)ha->req_ring_index;

			/* Set entry count. */
			pkt->entry_count = 1;

			break;
		}

		/* Release ring specific lock */
		spin_unlock(&ha->hardware_lock);

		udelay(2);   /* 2 us */

		/* Check for pending interrupts. */
		/* During init we issue marker directly */
		if (!ha->marker_needed)
			qla2x00_poll(ha);

		spin_lock_irq(&ha->hardware_lock);
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("%s(): **** FAILED ****\n", __func__);
#endif

	LEAVE(__func__);

	return(pkt);
}

/*
* qla2x00_isp_cmd
*      Function is responsible for modifying ISP input pointer.
*      Releases ring lock.
*
* Input:
*      ha  = adapter block pointer.
*/
STATIC void
qla2x00_isp_cmd(scsi_qla_host_t *ha)
{
	device_reg_t *reg = ha->iobase;

	ENTER(__func__);

	DEBUG5(printk("%s(): IOCB data:\n", __func__);)
	DEBUG5(qla2x00_dump_buffer((uint8_t *)ha->request_ring_ptr,
	    REQUEST_ENTRY_SIZE);)

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
#if defined(ISP2100) || defined(ISP2200)
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);
	PCI_POSTING(&reg->mailbox4);
#else
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		struct device_reg_24xx *reg24 =
		    (struct device_reg_24xx *) ha->iobase;

		WRT_REG_DWORD(&reg24->req_q_in, ha->req_ring_index);
		RD_REG_DWORD(&reg24->req_q_in);		/* PCI Posting. */
	} else {
		WRT_REG_WORD(&reg->req_q_in, ha->req_ring_index);
		PCI_POSTING(&reg->req_q_in);
	}
#endif

	LEAVE(__func__);
}

/*
* qla2x00_enable_lun
*      Issue enable LUN entry IOCB.
*
* Input:
*      ha = adapter block pointer.
*/
STATIC void
qla2x00_enable_lun(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	elun_entry_t *pkt;

	ENTER("qla2x00_enable_lun");

	spin_lock_irqsave(&ha->hardware_lock, flags);
	/* Get request packet. */
	if ((pkt = (elun_entry_t *)qla2x00_req_pkt(ha)) != NULL) {
		pkt->entry_type = ENABLE_LUN_TYPE;
		pkt->command_count = 32;
		pkt->immed_notify_count = 1;
		pkt->timeout = __constant_cpu_to_le16(0xFFFF);

		/* Issue command to ISP */
		qla2x00_isp_cmd(ha);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (!pkt)
		printk("qla2100_enable_lun: **** FAILED ****\n");
#endif

	LEAVE("qla2x00_enable_lun");
}


/*
 * qla2x00_process_good_request
 * Mark request denoted by "index" in the outstanding commands array
 * as complete and handle the stuff needed for that.
 *
 * Input:
 *      ha   = adapter block pointer.
 *      index = srb handle.
 *      async_event_status_code 
 *
 * Note: To be called from the ISR only.
 */
STATIC void
qla2x00_process_good_request(struct scsi_qla_host * ha, int index, 
					int async_event_status_code)
{
	srb_t *sp;
	struct scsi_qla_host *vis_ha;

	ENTER(__func__);

	/* Validate handle. */
	if (index < MAX_OUTSTANDING_COMMANDS) {
		sp = ha->outstanding_cmds[index];
	} else {
		DEBUG2(printk(KERN_INFO "%s(%ld): invalid scsi completion handle %d.\n",
				__func__,
				ha->host_no, 
				index);)
		sp = NULL;
	}

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[index] = 0;
		ha->iocb_cnt -= sp->iocb_cnt;
		vis_ha =(scsi_qla_host_t *)sp->cmd->host->hostdata;
		if( ha->actthreads )
			ha->actthreads--;
		sp->lun_queue->out_cnt--;
		if (sp->fclun->io_cnt > 0)
			sp->fclun->io_cnt--;
		sp->fclun->s_time = jiffies - sp->u_start;
		sp->flags |= SRB_ISP_COMPLETED;
		CMD_COMPL_STATUS(sp->cmd) = 0L;
		CMD_SCSI_STATUS(sp->cmd) = 0L;

		/* Save ISP completion status */
		CMD_RESULT(sp->cmd) = DID_OK << 16;
		sp->fo_retry_cnt = 0;
		add_to_done_queue(ha,sp);
	} else {
		DEBUG2(printk(KERN_INFO "scsi(%ld): %s(): ISP invalid handle\n",
				ha->host_no,
				__func__);)
		printk(KERN_WARNING
			"%s(): ISP invalid handle", __func__);

		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	}

	LEAVE(__func__);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,7)
/*
*  qla2x00_process_risc_intrs
*      Check and process multiple pending interrupts.
*
* Input:
*      ha           = adapter block pointer.
*      io_request_lock must be already obtained.
*      
*/
STATIC void
qla2x00_process_risc_intrs(scsi_qla_host_t *ha)
{
	unsigned long mbx_flags = 0 , flags = 0;
	uint16_t    data;
	uint8_t     got_mbx = 0;
	device_reg_t *reg;

	reg = ha->iobase;

	DEBUG(printk("%s(): check and process pending intrs.\n", __func__);)

	spin_lock_irqsave(&ha->hardware_lock, flags);
	/* Check and process pending interrupts. */
#if defined(ISP2100) || defined(ISP2200)
	while (!(ha->flags.in_isr) &&
		((data = RD_REG_WORD(&reg->istatus)) & RISC_INT))
#else
	while (!(ha->flags.in_isr) &&
		((data = RD_REG_WORD(&reg->host_status_lo)) & HOST_STATUS_INT))
#endif
	{
		ha->total_isr_cnt++;
		qla2x00_isr(ha, data, &got_mbx);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
		 got_mbx && ha->flags.mbox_int) {
		/* There was a mailbox completion */
		DEBUG3(printk("%s(): going to get mbx reg lock.\n", __func__);)

		QLA_MBX_REG_LOCK(ha);
		MBOX_TRACE(ha,BIT_5);
		got_mbx = 0;

		if (ha->mcp == NULL) {
			DEBUG3(printk("%s(): error mbx pointer.\n", __func__);)
		} else {
			DEBUG3(printk("%s(): going to set mbx intr flags. "
					"cmd=%x.\n",
					__func__,
					ha->mcp->mb[0]);)
		}
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		DEBUG3(printk("%s(%ld): going to wake up mbx function for "
				"completion.\n",
				__func__,
				ha->host_no);)
		MBOX_TRACE(ha,BIT_6);
		up(&ha->mbx_intr_sem);

		DEBUG3(printk("%s: going to unlock mbx reg.\n", __func__);)
		QLA_MBX_REG_UNLOCK(ha);
	}

	LEAVE(__func__);
}
#endif

/****************************************************************************/
/*                        Interrupt Service Routine.                        */
/****************************************************************************/

/*
*  qla2x00_isr
*      Calls I/O done on command completion.
*
* Input:
*      ha           = adapter block pointer.
*      INTR_LOCK must be already obtained.
*/
STATIC void
qla2x00_isr(scsi_qla_host_t *ha, uint16_t data, uint8_t *got_mbx)
{
	device_reg_t *reg = ha->iobase;
	uint32_t     index;
	uint16_t     *iptr, *mptr;
	uint16_t     mailbox[MAILBOX_REGISTER_COUNT];
	uint16_t     cnt, temp1;
#if defined(ISP2100) || defined(ISP2200)
	uint16_t     response_index = RESPONSE_ENTRY_CNT;
#endif
#if defined(ISP2300) 
	uint16_t     temp2;
	uint8_t      mailbox_int;
	uint16_t     hccr;
#endif
	uint8_t      rscn_queue_index;

	ENTER(__func__);

#if defined(ISP2300)
	/*
	 * Check for a paused RISC -- schedule an isp abort 
	 */
	if (data & BIT_8) {
		hccr = RD_REG_WORD(&reg->host_cmd);
		printk(KERN_INFO
		    "%s(%ld): RISC paused, dumping HCCR (%x) and schedule "
		    "an ISP abort (big-hammer)\n",
		    __func__,
		    ha->host_no,
		    hccr);
		printk("%s(%ld): RISC paused, dumping HCCR (%x) and schedule "
		    "an ISP abort (big-hammer)\n",
		    __func__,
		    ha->host_no,
		    hccr);

		/* Issuing a "HARD" reset in order for the RISC interrupt
		 * bit to be cleared and scheduling a big hammmer to
		 * get out of the RISC PAUSED state.
		 */
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
		PCI_POSTING(&reg->host_cmd);
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	}
#endif

	/* Check for mailbox interrupt. */
	MBOX_TRACE(ha,BIT_2);
#if defined(ISP2100) || defined(ISP2200)
	response_index = qla2x00_debounce_register(&reg->mailbox5);
	temp1 = RD_REG_WORD(&reg->semaphore);
	if (temp1 & BIT_0) {
		temp1 = RD_REG_WORD(&reg->mailbox0);
#else
	temp2 = RD_REG_WORD(&reg->host_status_hi);
	mailbox_int = 0;
	switch (data & 0xFF) {
		case ROM_MB_CMD_COMP:
		case ROM_MB_CMD_ERROR:
		case MB_CMD_COMP:
		case MB_CMD_ERROR:
		case ASYNC_EVENT:
			mailbox_int = 1;
			temp1 = temp2;
			break;
		case FAST_SCSI_COMP:
			mailbox_int = 1;
			temp1 = MBA_SCSI_COMPLETION;
			break;
		case RESPONSE_QUEUE_INT:
			WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			PCI_POSTING(&reg->host_cmd);
			goto response_queue_int;
			break;
		default:
			WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
			PCI_POSTING(&reg->host_cmd);
			goto isr_end;
			break;
	}

	if (mailbox_int) {
		MBOX_TRACE(ha,BIT_3);
#endif
		/*
		   if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
		   QLA_MBX_REG_LOCK(ha);
		 */
		if (( temp1 == MBA_SCSI_COMPLETION) ||
			((temp1 >= RIO_MBS_CMD_CMP_1_16) && (temp1 <= RIO_MBS_CMD_CMP_5_16))) {
#if defined(ISP2100) || defined(ISP2200)
			mailbox[1] = RD_REG_WORD(&reg->mailbox1);
#else
			mailbox[1] = temp2;
#endif

			mailbox[2] = RD_REG_WORD(&reg->mailbox2);
			mailbox[3] = RD_REG_WORD(&reg->mailbox3);
			mailbox[5] = RD_REG_WORD(&reg->mailbox5);
			mailbox[6] = RD_REG_WORD(&reg->mailbox6);
			mailbox[7] = RD_REG_WORD(&reg->mailbox7);
		} else {
			MBOX_TRACE(ha,BIT_4);
			mailbox[0] = temp1;
			DEBUG3(printk("%s(): Saving return mbx data\n",
					__func__);)

			/* Get mailbox data. */
			mptr = &mailbox[1];
			iptr = (uint16_t *)&reg->mailbox1;
			for (cnt = 1; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
#if defined(ISP2200)
				if (cnt == 8)
					iptr = (uint16_t *)&reg->mailbox8;
#endif
				if (cnt == 4 || cnt == 5)
					*mptr = qla2x00_debounce_register(iptr);
				else
					*mptr = RD_REG_WORD(iptr);
				mptr++;
				iptr++;
			}
		}

		/*
		   if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
		   QLA_MBX_REG_UNLOCK(ha);
		 */
		/* Release mailbox registers. */
		WRT_REG_WORD(&reg->semaphore, 0);
		PCI_POSTING(&reg->semaphore);
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
		PCI_POSTING(&reg->host_cmd);

		DEBUG5(printk("%s(): mailbox interrupt mailbox[0] = %x.\n",
				__func__,
				temp1);)

		/* Handle asynchronous event */
		switch (temp1) {

			case MBA_ZIO_UPDATE:
				DEBUG5(printk("%s ZIO update completion\n",
							__func__);)
			        break;
			case MBA_SCSI_COMPLETION:	/* Completion */
				
				DEBUG5(printk("%s(): mailbox response "
						"completion.\n",
						__func__);)

				if (!ha->flags.online)
					break;

				/* Get outstanding command index  */
				index = le32_to_cpu(((uint32_t)(mailbox[2] << 16)) | mailbox[1]);

				qla2x00_process_good_request(ha,
						index, MBA_SCSI_COMPLETION);
				break;

			case RIO_MBS_CMD_CMP_1_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_1_16);
				}
				break;
			case RIO_MBS_CMD_CMP_2_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_2_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_2_16);
				}
				break;
			case RIO_MBS_CMD_CMP_3_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_3_16);
				}
				break;
			case RIO_MBS_CMD_CMP_4_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
					index = (uint32_t) (mailbox[6]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_4_16);
				}
				break;
			case RIO_MBS_CMD_CMP_5_16:	/* Mitigated Response completion */
				DEBUG5(printk("qla2100_isr: mailbox response completion\n"));
				if (ha->flags.online) {
					/* Get outstanding command index. */
					index = (uint32_t) (mailbox[1]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[2]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[3]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[6]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
					index = (uint32_t) (mailbox[7]);
					qla2x00_process_good_request(ha, index, RIO_MBS_CMD_CMP_5_16);
				}
				break;
				
			case MBA_RESET:			/* Reset */

				DEBUG2(printk(KERN_INFO "scsi(%ld): %s: asynchronous "
						"RESET.\n",
						ha->host_no,
						__func__);)

				set_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);
				break;

			case MBA_SYSTEM_ERR:		/* System Error */

				printk(KERN_INFO
					"qla2x00: ISP System Error - mbx1=%xh, "
					"mbx2=%xh, mbx3=%xh.", mailbox[1],
					mailbox[2], mailbox[3]);
				/* F/W ER33873 - v3.02.21 modified to clearly
				 * indicate unrecoverable ISP hardware error */
				if (mailbox[1] == 0) {
					ha->flags.online = FALSE;
					printk(KERN_INFO "Unrecoverable "
						" hardware error : Adapter"
						" being marked offline\n");
				} else
				    set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);

				break;

			case MBA_REQ_TRANSFER_ERR:  /* Request Transfer Error */

				printk(KERN_WARNING
					"qla2x00(%ld): ISP Request Transfer "
					"Error mbx1=0x%x.\n",ha->host_no,
					 mailbox[1]);
				DEBUG2(printk(KERN_INFO
					"qla2x00(%ld): ISP Request Transfer "
					"Error mbx1=0x%x.\n",ha->host_no,
					 mailbox[1]);)

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;

			case MBA_RSP_TRANSFER_ERR: /* Response Transfer Error */

				printk(KERN_WARNING
					"qla2100: ISP Response Transfer "
					"Error.\n");

				DEBUG2(printk(KERN_INFO "%s(): ISP Response Transfer "
						"Error.\n",
						__func__);)

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;

			case MBA_ATIOQ_TRASNFER_ERR :/* ATIO queue trasfer 
							error */	

				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"ATIO queue transfer err.\n",
						__func__);)
				break;

			case MBA_LIP_OCCURRED:	/* Loop Initialization	*/
						/*  Procedure		*/

				if (!qla2x00_quiet)
					printk(KERN_INFO
						"scsi(%ld): LIP occurred.\n",
						    ha->host_no);

				DEBUG2(printk(
					KERN_INFO "%s(): asynchronous "
					"MBA_LIP_OCCURRED.\n",
					__func__);)

				/* Save LIP sequence. */
				ha->lip_seq = mailbox[1];
				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
							LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				set_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
				set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);

				ha->flags.management_server_logged_in = 0;

				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LIP_OCCURRED, NULL);
				}

				ha->total_lip_cnt++;

				break;

			case MBA_LOOP_UP:

				printk(KERN_INFO
					"scsi(%ld): LOOP UP detected.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"MBA_LOOP_UP.\n",
						__func__);)

				ha->flags.management_server_logged_in = 0;
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LOOP_UP, NULL);
				}

				/*
				 * Save the current speed for use by ioctl and
				 * IP driver.
				 */
				ha->current_speed = EXT_DEF_PORTSPEED_1GBIT;
#if defined(ISP2300)
				if (mailbox[1] == 1)
					ha->current_speed =
						EXT_DEF_PORTSPEED_2GBIT;
#endif
				break;

			case MBA_LOOP_DOWN:

				printk(KERN_INFO
					"scsi(%ld): LOOP DOWN detected.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: asynchronous "
						"MBA_LOOP_DOWN.\n",
						ha->host_no, __func__);)

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					ha->device_flags |= DFLG_NO_CABLE;
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
					    LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}

				if (ql2xfdmienable)
					clear_bit(FDMI_REGISTER_NEEDED,
					    &ha->fdmi_flags);

				ha->flags.management_server_logged_in = 0;
				ha->current_speed = 0; /* reset value */

				/* no wait 10/19/2000 */
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LOOP_DOWN, NULL);
				}
				break;

			case MBA_LIP_RESET:	/* LIP reset occurred */

				printk(KERN_INFO
					"scsi(%ld): LIP reset occurred.\n",
					ha->host_no);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: "
					"asynchronous MBA_LIP_RESET.\n",
					ha->host_no, __func__);)

				set_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
				set_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					atomic_set(&ha->loop_state, LOOP_DOWN);
					atomic_set(&ha->loop_down_timer,
					    LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				ha->operating_mode = LOOP;
				ha->flags.management_server_logged_in = 0;

				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_LIP_RESET, NULL);
				}

				ha->total_lip_cnt++;
				break;

#if !defined(ISP2100)
			case MBA_LINK_MODE_UP:	/* Link mode up. */

				DEBUG(printk("scsi(%ld): Link node is up.\n",
						ha->host_no);)

				DEBUG2(printk(KERN_INFO "%s(%ld): asynchronous "
						"MBA_LINK_MODE_UP.\n",
						__func__,
						ha->host_no);)

				/*
				 * Until there's a transition from loop down to
				 * loop up, treat this as loop down only.
				 */
				if (!(test_bit(ABORT_ISP_ACTIVE,
							&ha->dpc_flags))) {
					set_bit(COMMAND_WAIT_NEEDED,
							&ha->dpc_flags);
					set_bit(RESET_MARKER_NEEDED,
							&ha->dpc_flags);
				}
				set_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags);

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					if (!atomic_read(&ha->loop_down_timer))
						atomic_set(&ha->loop_down_timer,
						    LOOP_DOWN_TIME);

					atomic_set(&ha->loop_state, LOOP_DOWN);
					qla2x00_mark_all_devices_lost(ha);
				}
				break;

			case MBA_UPDATE_CONFIG:      /* Update Configuration. */

				printk(KERN_INFO
					"scsi(%ld): Configuration change "
					"detected: value %d.\n",
					ha->host_no,
					mailbox[1]);

				DEBUG2(printk(KERN_INFO "scsi(%ld) %s: asynchronous "
						"MBA_UPDATE_CONFIG.\n",
						ha->host_no, __func__);)

				if (atomic_read(&ha->loop_state) != LOOP_DOWN) {
					/* dg - 03/30 */
					atomic_set(&ha->loop_state, LOOP_DOWN);  
					if (!atomic_read(&ha->loop_down_timer))
						atomic_set(&ha->loop_down_timer,
								LOOP_DOWN_TIME);
					qla2x00_mark_all_devices_lost(ha);
				}
				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
				break;

#endif	/* #if !defined(ISP2100) */

			case MBA_PORT_UPDATE:	/* Port database update */

			     /* If PORT UPDATE is global(recieved 
			      * LIP_OCCURED/LIP_RESET event etc earlier 
			      * indicating loop is down) then process
			      * it. 
			      */
				printk(KERN_INFO "scsi(%ld): Port database "
						"changed.\n",
						ha->host_no);
				
			     if (atomic_read(&ha->loop_state) == LOOP_DOWN ||
			         atomic_read(&ha->loop_state) == LOOP_DEAD){

				DEBUG2(printk(KERN_INFO "scsi%ld %s: asynchronous "
						"MBA_PORT_UPDATE.\n",
						ha->host_no, __func__);)

				if (ql2xfdmienable)
					set_bit(FDMI_REGISTER_NEEDED,
					    &ha->fdmi_flags);

				/* dg - 06/19/01
				 *
				 * Mark all devices as missing so we will
				 * login again.
				 */
				ha->flags.rscn_queue_overflow = 1;

				atomic_set(&ha->loop_down_timer, 0);
				atomic_set(&ha->loop_state, LOOP_UP);
				qla2x00_mark_all_devices_lost(ha);
				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);

				/* 9/23
				 *
				 * ha->flags.loop_resync_needed = TRUE;
				 */
				atomic_set(&ha->loop_state, LOOP_UPDATE);
				if (ha->ioctl->flags &
						IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
							MBA_PORT_UPDATE, NULL);
				}

			     } else {
				     /* If Port update is not global (mbx1)
				      * and device logged out (mbx2)
				      * schedule relogin. */
				     DEBUG2(printk(KERN_INFO "scsi(%ld) Port"
					 " update mbx1-0x%x mb2-0x%x"
					 " mb3-0x%x\\n", ha->host_no,
					   mailbox[1], mailbox[2],mailbox[3]);)
				     if (mailbox[1] != 0xFFFF && 
					  mailbox[2] != 6 ){
					 fc_port_t *fcport;    
				         fcport = 
					   qla2x00_find_changed_local_port(
						  ha, mailbox[1]);
					 if (fcport && (atomic_read(&fcport->state) == FC_ONLINE) &&
						fcport->login_retry == 0)
						qla2x00_mark_device_lost(
							ha, fcport, 1);
					 else
						DEBUG2(printk(KERN_INFO "Port update:"
						    " Cant find the port or"
						    " trying to log back in");)
				     } else {
					atomic_set(&ha->loop_down_timer, 0);
						DEBUG2(printk(KERN_INFO "scsi(%ld)" 
						" Port Update ignored\n",	
			     			ha->host_no);)
				     }	
			     }
				break;

			case MBA_SCR_UPDATE:	/* State Change Registration */

				DEBUG2(printk(KERN_INFO "scsi(%ld): RSCN database changed "
						"-0x%x,0x%x.\n",
						ha->host_no,
						mailbox[1],
						mailbox[2]);)
				printk(KERN_INFO "scsi(%ld): RSCN database changed "
						"-0x%x,0x%x.\n",
						ha->host_no,
						mailbox[1],
						mailbox[2]);

				rscn_queue_index = ha->rscn_in_ptr + 1;
				if (rscn_queue_index == MAX_RSCN_COUNT)
					rscn_queue_index = 0;
				if (rscn_queue_index != ha->rscn_out_ptr) {
					ha->rscn_queue[ha->rscn_in_ptr].
						format =
						   (uint8_t)(mailbox[1] >> 8);
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.domain =
						   (uint8_t)mailbox[1];
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.area =
						   (uint8_t)(mailbox[2] >> 8);
					ha->rscn_queue[ha->rscn_in_ptr].
						d_id.b.al_pa =
						   (uint8_t)mailbox[2];
					ha->rscn_in_ptr =
						(uint8_t)rscn_queue_index;
				} else {
					ha->flags.rscn_queue_overflow = 1;
				}

				set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
				set_bit(RSCN_UPDATE, &ha->dpc_flags);
				atomic_set(&ha->loop_down_timer, 0);
				ha->flags.management_server_logged_in = 0;

				atomic_set(&ha->loop_state, LOOP_UPDATE);
				if (ha->ioctl->flags &
				    IOCTL_AEN_TRACKING_ENABLE) {
					/* Update AEN queue. */
					qla2x00_enqueue_aen(ha,
					    MBA_RSCN_UPDATE, &mailbox[0]);
				}
				break;

			case MBA_LOOP_INIT_ERR:
				DEBUG2(printk(KERN_INFO"scsi(%ld) Loop Init"
				    " Errors mbx1=0x%x\n", ha->host_no,
				    mailbox[1]);)
				break;

			case MBA_FC_SP_UPDATE:
				DEBUG2(printk(KERN_INFO "scsi(%ld):asynchronous"
				    "MBA FC-SP security update: "
				    "security_status-0x%x.\n", ha->host_no,
				    mailbox[1]);)
				break;

			case MBA_CTIO_COMPLETION:
				DEBUG2(printk(KERN_INFO "%s(): asynchronous "
						"MBA_CTIO_COMPLETION.\n",
						__func__);)
				break;

			case MBA_RECEIVE_ERR:
				DEBUG2(printk(KERN_INFO"scsi(%ld) Recieve"
				    " Errors mbx1=0x%x mbx2=0x%x"
				    " mbx3=0x%x mbx4=0x%x"
				    " mbx5=0x%x mbx6=0x%x "
				    " mbx7=0x%x\n",
				    ha->host_no, mailbox[1], mailbox[2],
				    mailbox[3], mailbox[4], mailbox[5],
				    mailbox[6], mailbox[7]);)
				break;

			case MBA_IP_LOW_WATER_MARK:
				DEBUG12(printk("%s(): asynchronous "
				   "MBA_IP_LOW_WATER_MARK.\n", __func__));
				break;

			case MBA_IP_RCV_BUFFER_EMPTY:
				DEBUG12(printk("%s(): asynchronous "
				   "MBA_IP_RCV_BUFFER_EMPTY.\n", __func__));
				break;

			default:

				if (temp1 >= MBA_ASYNC_EVENT)
					break;

				/* mailbox completion */
				*got_mbx = TRUE;
				memcpy((void *)ha->mailbox_out,
					mailbox,
					sizeof(ha->mailbox_out));
				ha->flags.mbox_int = TRUE;
				if (ha->mcp) {
					DEBUG3(printk("%s(): got mailbox "
							"completion. cmd=%x.\n",
							__func__,
							ha->mcp->mb[0]);)
				} else {
					DEBUG2_3(printk(KERN_INFO "%s(): mbx pointer "
							"ERROR.\n",
							__func__);)
				}
				DEBUG3(printk("%s(): Returning mailbox data\n",
						__func__);)
				break;
		}
	} else {
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
		PCI_POSTING(&reg->host_cmd);
	}
#if defined(ISP2300)
response_queue_int:
#endif
	if (ha->flags.online) {
		/* Check for unprocessed commands
		 * in response queue.
		 */
		if (ha->response_ring_ptr->signature !=
				RESPONSE_PROCESSED){
			qla2x00_process_response_queue(ha);
		}
	}

#if defined(ISP2300)
isr_end:
#endif

	LEAVE(__func__);
}

/*
*  qla2x00_rst_aen
*      Processes asynchronous reset.
*
* Input:
*      ha  = adapter block pointer.
*/
STATIC void
qla2x00_rst_aen(scsi_qla_host_t *ha) 
{
	ENTER(__func__);

	if (ha->flags.online && !ha->flags.reset_active &&
		!atomic_read(&ha->loop_down_timer) && 
		!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) ) {
		/* 10/15 ha->flags.reset_active = TRUE; */
		do {
			clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);

			/*
			 * Issue marker command only when we are going to start
			 * the I/O .
			 */
			ha->marker_needed = 1;
		} while (!atomic_read(&ha->loop_down_timer) &&
			(test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags)) );
		/* 10/15 ha->flags.reset_active = FALSE; */
	}

	LEAVE(__func__);
}

static void qla2x00_handle_RIO_type2_iocb(struct scsi_qla_host * ha, response_t *pkt)
{
	struct rio_iocb_type2_entry *rio;
	int i;
	ENTER("qla2x00_handle_RIO_type2_iocb");
	
	rio = (struct rio_iocb_type2_entry *) pkt;

	if (rio->handle_count > 30) {
		printk(KERN_INFO "Invalid packet 22 count: %i \n",
			 rio->handle_count);
	}

	for (i=0; i < rio->handle_count; i++) 
		qla2x00_process_good_request(ha, rio->handle[i], 0x22);
	LEAVE("qla2x00_handle_RIO_type2_iocb");
}

static void qla2x00_handle_RIO_type1_iocb(struct scsi_qla_host * ha, response_t *pkt)
{
	struct rio_iocb_type1_entry *rio;
	int i;
	ENTER("qla2x00_handle_RIO_type1_iocb");

	rio = (struct rio_iocb_type1_entry *) pkt;

	if (rio->handle_count > 15) {
		printk(KERN_INFO "Invalid packet 21 count! %i\n", 
			rio->handle_count);
	}

	for (i=0; i < rio->handle_count; i++) 
		qla2x00_process_good_request(ha, rio->handle[i], 0x22);
	LEAVE("qla2x00_handle_RIO_type1_iocb");
	
}

/*
 *  qla2x00_process_response_queue
 *      Processes Response Queue.
 *
 * Input:
 *      ha  = adapter block pointer.
 */
STATIC void
qla2x00_process_response_queue(scsi_qla_host_t *ha)
{
	device_reg_t	*reg = ha->iobase;
	sts_entry_t	*pkt;

	ENTER(__func__);

	while (ha->response_ring_ptr->signature != RESPONSE_PROCESSED ) {
		pkt = ( sts_entry_t *) ha->response_ring_ptr;

		DEBUG5(printk("%s(): ha->rsp_ring_index=%ld.\n",
				__func__,
				(u_long)ha->rsp_ring_index));
		DEBUG5(printk("%s(): response packet data:", __func__);)
		DEBUG5(qla2x00_dump_buffer((uint8_t *)pkt,
				RESPONSE_ENTRY_SIZE);)

		ha->rsp_ring_index++;
		if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
			ha->rsp_ring_index = 0;
			ha->response_ring_ptr = ha->response_ring;
		} else {
			ha->response_ring_ptr++;
		}

#if defined(ISP2300)
		/*
		 * This code is temporary until FW is fixed.  FW is mistakenly
		 * setting bit 6 on Mailbox IOCB response
		 */
		pkt->entry_status &= 0x3f;
#endif

		if (pkt->entry_status != 0) {
			DEBUG3(printk(KERN_INFO
					"%s(): process error entry.\n",
					__func__);)
			qla2x00_error_entry(ha, pkt);
			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}

		DEBUG3(printk(KERN_INFO
				"%s(): process response entry.\n",
				__func__);)

		switch (pkt->entry_type) {
			case STATUS_TYPE:
				qla2x00_status_entry(ha, (sts_entry_t *)pkt);
				break;

			case STATUS_CONT_TYPE:
				qla2x00_status_cont_entry(ha,
						(sts_cont_entry_t *)pkt);
				break;

			case MS_IOCB_TYPE:
				qla2x00_ms_entry(ha, (ms_iocb_entry_t *)pkt);
				break;

#if defined(ISP2300)
			case ET_IP_COMMAND_64:
				/* Handle IP send completion */
				qla2x00_ip_send_complete(ha,
						pkt->handle,
						le16_to_cpu(pkt->comp_status));
				break;

			case ET_IP_RECEIVE:
				/* Handle IP receive packet */
				qla2x00_ip_receive(ha,
						(struct ip_rec_entry *)pkt);
				break;
#endif
			case RIO_IOCB_TYPE1:
				qla2x00_handle_RIO_type1_iocb(ha, (response_t*)pkt);
				break;
			case RIO_IOCB_TYPE2:
				qla2x00_handle_RIO_type2_iocb(ha, (response_t*)pkt);
				break;

			default:
				/* Type Not Supported. */
				DEBUG4(printk(KERN_WARNING
						"%s(): received unknown "
						"response pkt type %x "
						"entry status=%x.\n",
						__func__,
						pkt->entry_type, 
						pkt->entry_status);)
				break;
		}
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	} 

	/* Adjust ring index -- once, instead of for all entries. */
#if defined(ISP2100) || defined(ISP2200)
	WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
	PCI_POSTING(&reg->mailbox5);
#else
	WRT_REG_WORD(&reg->rsp_q_out, ha->rsp_ring_index);
	PCI_POSTING(&reg->rsp_q_out);
#endif

	LEAVE(__func__);
}

static inline void qla2x00_filter_command(scsi_qla_host_t *ha, srb_t *sp);
static inline void
qla2x00_filter_command(scsi_qla_host_t *ha, srb_t *sp)
{
	Scsi_Cmnd	*cp = sp->cmd;
	uint8_t		*strp;

	/*
	 * Special case considertaion on an Inquiry command (0x12) for Lun 0,
	 * device responds with no devices (0x7F), then Linux will not scan
	 * further Luns. While reporting that some device exists on Lun 0 Linux
	 * will scan all devices on this target.
	 */
	if (qla2xenbinq && (cp->cmnd[0] == INQUIRY) && (cp->lun == 0)) {
		strp = (uint8_t *)cp->request_buffer;
		if (*strp == 0x7f) {
			/* Make lun unassigned and processor type */
			*strp = 0x23;
		}
	}
}

/*
 *  qla2x00_status_entry
 *      Processes received ISP status entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 */
STATIC void
qla2x00_status_entry(scsi_qla_host_t *ha, sts_entry_t *pkt ) 
{
	uint32_t	b, l;
	uint32_t	t; /*target*/
	uint8_t		sense_sz = 0;
	srb_t		*sp;
	os_lun_t	*lq;
	os_tgt_t	*tq;
	uint32_t	resid;
	Scsi_Cmnd	*cp;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint8_t		lscsi_status;
	fc_port_t	*fcport;
	scsi_qla_host_t	*vis_ha;
	uint16_t	rsp_info_len;


	ENTER(__func__);

	/* Validate handle. */
	if (pkt->handle < MAX_OUTSTANDING_COMMANDS) {
		sp = ha->outstanding_cmds[pkt->handle];
		/* Free outstanding command slot. */
		ha->outstanding_cmds[pkt->handle] = 0;
	} else
		sp = NULL;

	if (sp == NULL) {
		printk(KERN_WARNING
			"qla2x00: Status Entry invalid handle.\n");

		DEBUG2(printk(KERN_INFO "qla2x00: Status Entry invalid handle.\n");)
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);
		return;
	}

	cp = sp->cmd;
	if (cp == NULL) {
		printk(KERN_WARNING 
			"%s(): cmd is NULL: already returned to OS (sp=%p)\n",
			__func__,
			sp);
		DEBUG2(printk(KERN_INFO "%s(): cmd already returned back to OS "
				"pkt->handle:%d sp=%p sp->state:%d\n",
				__func__,
				pkt->handle,
				sp,
				sp->state);)
		return;
	}

	/*
	 * Set the visible adapter for lun Q access.
	 */
	vis_ha = (scsi_qla_host_t *)cp->host->hostdata;
	if (ha->actthreads)
		ha->actthreads--;

	if (sp->lun_queue == NULL) {
		printk(KERN_WARNING
			"qla2x00: Status Entry invalid lun pointer.\n");
		DEBUG2(printk(KERN_INFO "qla2x00: Status Entry invalid lun pointer.\n");)
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);
		return;
	}

	sp->lun_queue->out_cnt--;
	if (sp->fclun->io_cnt > 0)
		sp->fclun->io_cnt--;
	sp->fclun->s_time = jiffies - sp->u_start;
	ha->iocb_cnt -= sp->iocb_cnt;

	comp_status = le16_to_cpu(pkt->comp_status);
	/* Mask of reserved bits 12-15.  Before we examine the scsi status */
	scsi_status = le16_to_cpu(pkt->scsi_status) & SS_MASK;
	lscsi_status = scsi_status & STATUS_MASK;

	CMD_ENTRY_STATUS(cp) = pkt->entry_status;
	CMD_COMPL_STATUS(cp) = comp_status;
	CMD_SCSI_STATUS(cp) = scsi_status;

	/* dg 10/11 */
	sp->flags |= SRB_ISP_COMPLETED;

	/* Generate LU queue on cntrl, target, LUN */
	b = SCSI_BUS_32(cp);
	t = SCSI_TCN_32(cp);
	l = SCSI_LUN_32(cp);
	tq = sp->tgt_queue;
	lq = sp->lun_queue;

	/*
	 * If loop is in transient state Report DID_BUS_BUSY
	 */
	if (!(sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) &&
	    (atomic_read(&ha->loop_down_timer) ||
		atomic_read(&ha->loop_state) != LOOP_READY) &&
	    (comp_status != CS_COMPLETE || scsi_status != 0)) {

		DEBUG2(printk(KERN_INFO "scsi(%ld:%d:%d:%d): Loop Not Ready - pid=%lx.\n",
				ha->host_no, 
				b, t, l, 
				sp->cmd->serial_number);)
#if DG
		CMD_RESULT(cp) = DID_BUS_BUSY << 16;
		add_to_done_queue(ha, sp);
#else
		qla2x00_extend_timeout(sp->cmd, EXTEND_CMD_TIMEOUT);
		add_to_retry_queue(ha, sp);
#endif
		return;
	}

	/* Check for any FCP transport errors. */
	if (scsi_status & SS_RESPONSE_INFO_LEN_VALID) {
		rsp_info_len = le16_to_cpu(pkt->rsp_info_len);
		if (rsp_info_len > 3 && pkt->rsp_info[3]) {
			DEBUG2(printk("scsi(%ld:%d:%d:%d) FCP I/O protocol "
			    "failure (%x/%02x%02x%02x%02x%02x%02x%02x%02x)..."
			    "retrying command\n", ha->host_no, b, t, l,
			    rsp_info_len, pkt->rsp_info[0], pkt->rsp_info[1],
			    pkt->rsp_info[2], pkt->rsp_info[3],
			    pkt->rsp_info[4], pkt->rsp_info[5],
			    pkt->rsp_info[6], pkt->rsp_info[7]));

			cp->result = DID_BUS_BUSY << 16;
			add_to_done_queue(ha, sp);
			return;
		}
	}

	/*
	 * Based on Host and scsi status generate status code for Linux
	 */
	switch (comp_status) {
		case CS_COMPLETE:
			/*
			 * Host complted command OK.  Check SCSI Status to
			 * determine the correct Host status.
			 */
			if (scsi_status == 0) {
				CMD_RESULT(cp) = DID_OK << 16;

				/*
				 * Special case consideration On an Inquiry
				 * command (0x12) for Lun 0, device responds
				 * with no devices (0x7F), then Linux will not
				 * scan further Luns. While reporting that some
				 * device exists on Lun 0 Linux will scan all
				 * devices on this target.
				 */
				/* Perform any post command processing */
				qla2x00_filter_command(ha, sp);
			} else {   /* Check for non zero scsi status */
				if (scsi_status & (SS_RESIDUAL_UNDER | SS_RESIDUAL_OVER)) {
					resid = le32_to_cpu(pkt->residual_length);
					cp->resid = resid;
					if (sp->flags & SRB_IOCTL)
						CMD_RESID_LEN(cp) = resid;
					/* Handle mid-layer underflow */
					if (!lscsi_status && (unsigned)(cp->request_bufflen - resid) <
					    cp->underflow) {
						printk(KERN_INFO
						    "scsi(%ld:%d:%d:%d): Mid-layer underflow "
						    "detected (%x of %x bytes)...returning "
						    "error status.\n",
						    ha->host_no, b, t, l, resid,
						    cp->request_bufflen);

						cp->result = DID_ERROR << 16;
						break;
					}
				}

				if (lscsi_status == SS_BUSY_CONDITION) {
					CMD_RESULT(cp) = DID_BUS_BUSY << 16 |
							 lscsi_status;
				} else {
					CMD_RESULT(cp) = DID_OK << 16 |
							 lscsi_status;

					if (lscsi_status != SS_CHECK_CONDITION)
						break;

					/*
					 * Copy Sense Data into sense buffer
					 */
					memset(cp->sense_buffer, 0, 
						sizeof(cp->sense_buffer));

					if (!(scsi_status & SS_SENSE_LEN_VALID))
						break;

					if (le16_to_cpu(pkt->req_sense_length) <
							CMD_SNSLEN(cp))
						sense_sz = le16_to_cpu(
							pkt->req_sense_length);
					else
						sense_sz = CMD_SNSLEN(cp);

					CMD_ACTUAL_SNSLEN(cp) = sense_sz;
					sp->request_sense_length = sense_sz;
				       	sp->request_sense_ptr =
					       	(void *)cp->sense_buffer;

				       	if (sp->request_sense_length > 32) 
						sense_sz = 32;

					memcpy(cp->sense_buffer,
							pkt->req_sense_data,
							sense_sz);

					sp->request_sense_ptr += sense_sz;
					sp->request_sense_length -= sense_sz;
					if (sp->request_sense_length != 0)
						ha->status_srb = sp;

					if (!(sp->flags & (SRB_IOCTL |
					    SRB_TAPE | SRB_FDMI_CMD)) &&
					    qla2x00_check_sense(cp, lq) ==
					    QL_STATUS_SUCCESS) {
						/*
						 * Throw away status_cont
						 * if any
						 */
					       	ha->status_srb = NULL;
						add_to_scsi_retry_queue(ha, sp);
						return;
					}
#if defined(QL_DEBUG_LEVEL_2)
					if (sense_sz) {
					printk("%s(): Check condition Sense "
						"data, scsi(%ld:%d:%d:%d) "
						"cmd=%p pid=%ld\n",
						__func__,
						ha->host_no, 
						b, t, l,
						cp, cp->serial_number);
						qla2x00_dump_buffer(
							cp->sense_buffer,
							CMD_ACTUAL_SNSLEN(cp));
					}
#endif
				}
			}
			break;

		case CS_DATA_UNDERRUN:
			DEBUG2(printk(KERN_INFO
					"qla%ld:%d:%d UNDERRUN status detected "
					"0x%x-0x%x.\n",
					ha->host_no, 
					t,l,
					comp_status, 
					scsi_status);)
			resid = le32_to_cpu(pkt->residual_length);
			if (sp->flags & SRB_IOCTL)
				CMD_RESID_LEN(cp) = resid;

			/*
			 * Check to see if SCSI Status is non zero.  If so
			 * report SCSI Status
			 */
			if (lscsi_status != 0) {
				if (lscsi_status == SS_BUSY_CONDITION) {
					CMD_RESULT(cp) = DID_BUS_BUSY << 16 |
						 lscsi_status;
				} else {
					CMD_RESULT(cp) = DID_OK << 16 |
						 lscsi_status;

					if (lscsi_status != SS_CHECK_CONDITION)
						break;

					/*
					 * Copy Sense Data into sense buffer
					 */
					memset(cp->sense_buffer, 0, 
						sizeof(cp->sense_buffer));

					if (!(scsi_status & SS_SENSE_LEN_VALID))
						break;

					if (le16_to_cpu(pkt->req_sense_length) <
							CMD_SNSLEN(cp))
						sense_sz = le16_to_cpu(
							pkt->req_sense_length);
					else
						sense_sz = CMD_SNSLEN(cp) - 1;

					CMD_ACTUAL_SNSLEN(cp) = sense_sz;
					sp->request_sense_length = sense_sz;
				       	sp->request_sense_ptr =
					       	(void *)cp->sense_buffer;

				       	if (sp->request_sense_length > 32) 
						sense_sz = 32;

					memcpy(cp->sense_buffer,
							pkt->req_sense_data,
							sense_sz);

					sp->request_sense_ptr += sense_sz;
					sp->request_sense_length -= sense_sz;
					if (sp->request_sense_length != 0)
						ha->status_srb = sp;

					if (!(sp->flags & (SRB_IOCTL |
					    SRB_TAPE | SRB_FDMI_CMD)) &&
					    (qla2x00_check_sense(cp, lq) ==
					    QL_STATUS_SUCCESS)) {
						ha->status_srb = NULL;
						add_to_scsi_retry_queue(ha,sp);
						return;
					}
#if defined(QL_DEBUG_LEVEL_2)
					if (sense_sz) {
					printk("scsi: Check condition Sense "
						"data, scsi(%ld:%d:%d:%d)\n",
						ha->host_no, b, t, l);
						qla2x00_dump_buffer(
							cp->sense_buffer,
							CMD_ACTUAL_SNSLEN(cp));
					}
#endif
				}
			} else {
				/*
				 * If RISC reports underrun and target does not
				 * report it then we must have a lost frame, so
				 * tell upper layer to retry it by reporting a
				 * bus busy.
				 */
				if (!(scsi_status & SS_RESIDUAL_UNDER)) {
					ha->dropped_frame_error_cnt++;
					CMD_RESULT(cp) = DID_BUS_BUSY << 16;
					DEBUG2(printk(KERN_INFO "scsi(%ld): Dropped "
						"frame(s) detected (%x of %x "
						"bytes)...retrying command.\n",
						ha->host_no,
						resid,
						CMD_XFRLEN(cp));)
					break;
				}

				/*
				 * Handle mid-layer underflow???
				 *
				 * For kernels less than 2.4, the driver must
				 * return an error if an underflow is detected.
				 * For kernels equal-to and above 2.4, the
				 * mid-layer will appearantly handle the
				 * underflow by detecting the residual count --
				 * unfortunately, we do not see where this is
				 * actually being done.  In the interim, we
				 * will return DID_ERROR.
				 */
				cp->resid = resid;
				if ((unsigned)(CMD_XFRLEN(cp) - resid) <
							cp->underflow) {
					CMD_RESULT(cp) = DID_ERROR << 16;
					printk(KERN_INFO 
						"scsi(%ld): Mid-layer "
						"underflow detected "
						"(%x of %x bytes) wanted "
						"%x bytes...returning "
						"DID_ERROR status!\n",
						ha->host_no,
						resid,
						CMD_XFRLEN(cp),
						cp->underflow);
					break;
				}

				/* Everybody online, looking good... */
				CMD_RESULT(cp) = DID_OK << 16;

				/*
				 * Special case consideration On an Inquiry
				 * command (0x12) for Lun 0, device responds
				 * with no devices (0x7F), then Linux will not
				 * scan further Luns. While reporting that some
				 * device exists on Lun 0 Linux will scan all
				 * devices on this target.
				 */
				/* Perform any post command processing */
				qla2x00_filter_command(ha, sp);
			}
			break;

		case CS_PORT_LOGGED_OUT:
		case CS_PORT_CONFIG_CHG:
		case CS_PORT_BUSY:
		case CS_INCOMPLETE:
		case CS_PORT_UNAVAILABLE:
			/*
			 * If the port is in Target Down state, return all IOs
			 * for this Target with DID_NO_CONNECT ELSE Queue the
			 * IOs in the retry_queue
			 */
			fcport = sp->fclun->fcport;
			DEBUG2(printk(KERN_INFO "scsi(%ld:%2d:%2d): status_entry: "
					"Port Down pid=%ld, compl "
					"status=0x%x, port state=0x%x\n",
					ha->host_no,
					t, l,
					sp->cmd->serial_number,
					comp_status,
					atomic_read(&fcport->state));)
			DEBUG2(printk( "scsi(%ld:%2d:%2d): status_entry: "
					"Port Down pid=%ld, compl "
					"status=0x%x, port state=0x%x\n",
					ha->host_no,
					t, l,
					sp->cmd->serial_number,
					comp_status,
					atomic_read(&fcport->state));)
 
			if ((sp->flags & (SRB_IOCTL | SRB_TAPE |
			    SRB_FDMI_CMD)) || (atomic_read(&fcport->state) ==
			    FC_DEVICE_DEAD)) {
				CMD_RESULT(cp) = DID_NO_CONNECT << 16;
				add_to_done_queue(ha, sp);
				if (atomic_read(&ha->loop_state) == LOOP_DEAD) 
					sp->err_id = SRB_ERR_LOOP;
				else
					sp->err_id = SRB_ERR_PORT;
			} else {
				qla2x00_extend_timeout(cp,
						EXTEND_CMD_TIMEOUT);
				add_to_retry_queue(ha, sp);
			}

			if (atomic_read(&fcport->state) == FC_ONLINE) {
				qla2x00_mark_device_lost(ha, fcport, 1);
			}

			return;
			break;

		case CS_RESET:
			DEBUG2(printk(KERN_INFO 
					"scsi(%ld): RESET status detected "
					"0x%x-0x%x.\n",
					ha->host_no, 
					comp_status, 
					scsi_status);)

			if (sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) {
				CMD_RESULT(cp) = DID_RESET << 16;
			}
			else {
				qla2x00_extend_timeout(cp,
						EXTEND_CMD_TIMEOUT);
				add_to_retry_queue(ha, sp);
				return;
			}
			break;

		case CS_ABORTED:
			/* 
			 * hv2.19.12 - DID_ABORT does not retry the request if
			 * we aborted this request then abort otherwise it must
			 * be a reset 
			 */
			DEBUG2(printk(KERN_INFO 
					"scsi(%ld): ABORT status detected "
					"0x%x-0x%x.\n",
					ha->host_no, 
					comp_status, 
					scsi_status);)
			CMD_RESULT(cp) = DID_RESET << 16;
			break;

		case CS_TIMEOUT:
			DEBUG2(printk(KERN_INFO
					"qla%ld TIMEOUT status detected "
					"0x%x-0x%x status_flag=0x%x.\n",
					ha->host_no, comp_status, scsi_status,
					le16_to_cpu(pkt->status_flags));)

			fcport = lq->fclun->fcport;
			CMD_RESULT(cp) = DID_BUS_BUSY << 16;

			/* 
			 * v2.19.8 if timeout then check to see if logout
			 * occurred
			 */
			t = SCSI_TCN_32(cp);
			if ((le16_to_cpu(pkt->status_flags) &
						IOCBSTAT_SF_LOGO)) {

				DEBUG2(printk(KERN_INFO "scsi: Timeout occurred with "
						"Logo, status flag (%x) with "
						"public device loop id (%x), "
						"attempt new recovery\n",
						le16_to_cpu(pkt->status_flags), 
						fcport->loop_id);)
				qla2x00_mark_device_lost(ha, fcport, 1);
			}
			break;

		case CS_QUEUE_FULL:


			DEBUG2(printk(KERN_INFO
			       "scsi(%ld:%d:%d): QUEUE FULL status detected "
			       "0x%x-0x%x, pid=%ld.\n",
				ha->host_no, 
				t,
				l,
				comp_status, 
				scsi_status, 
				sp->cmd->serial_number));
			/*
			 * SCSI Mid-Layer handles device queue full
			 */				 
			if (sp->qfull_retry_count <
			    ha->qfull_retry_count) {
				sp->qfull_retry_count++;
				qla2x00_suspend_target(ha, 
					sp->tgt_queue,
				    ha->qfull_retry_delay);
				qla2x00_extend_timeout(sp->cmd, 
					ha->qfull_retry_delay << 2);
				add_to_scsi_retry_queue(ha,sp);
				return;
			} else {
				printk( KERN_INFO
				"scsi(%ld:%d:%d): %s No more QUEUE FULL retries..\n",
				ha->host_no, t,l, __func__);
				clear_bit(TGT_SUSPENDED, &tq->q_flags);
				/* no more scsi retries */
				sp->cmd->retries = sp->cmd->allowed;
				CMD_RESULT(cp) = DID_ERROR << 16;
			}
			break;

		default:
			printk(KERN_INFO
				"scsi(%ld): Unknown status detected "
				"0x%x-0x%x.\n",
				ha->host_no, 
				comp_status, 
				scsi_status);
			DEBUG3(printk("scsi: Error detected 0x%x-0x%x.\n",
					comp_status, 
					scsi_status);)

			CMD_RESULT(cp) = DID_ERROR << 16;

			break;
	} /* end of switch comp_status */
	/* Place command on done queue. */
	if (ha->status_srb == NULL){

		add_to_done_queue(ha, sp);
	}

	LEAVE(__func__);
}

/*
 *  qla2x00_status_cont_entry
 *      Processes status continuation entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *
 * Context:
 *      Interrupt context.
 */
STATIC void
qla2x00_status_cont_entry(scsi_qla_host_t *ha, sts_cont_entry_t *pkt )
{
	uint8_t    sense_sz = 0;
	srb_t      *sp = ha->status_srb;
	Scsi_Cmnd      *cp;

	ENTER(__func__);

	if (sp != NULL && sp->request_sense_length != 0) {
		cp = sp->cmd;
		if (cp == NULL) {
			printk(KERN_INFO
				"%s(): cmd is NULL: already returned to OS "
				"(sp=%p)\n",
				__func__,
				sp); 
			DEBUG2(printk(KERN_INFO "%s(): cmd already returned back to OS "
					"sp=%p sp->state:%d\n",
					__func__,
					sp,
					sp->state);)
			ha->status_srb = NULL;
			return;
		}

		if (sp->request_sense_length > sizeof (pkt->req_sense_data)) {
			sense_sz = sizeof (pkt->req_sense_data);
		} else {
			sense_sz = sp->request_sense_length;
		}

		/* Move sense data. */
		memcpy(sp->request_sense_ptr, pkt->req_sense_data, sense_sz);
		DEBUG5(qla2x00_dump_buffer(sp->request_sense_ptr, sense_sz);)

		sp->request_sense_ptr += sense_sz;
		sp->request_sense_length -= sense_sz;

		/* Place command on done queue. */
		if (sp->request_sense_length == 0) {
			add_to_done_queue(ha, sp);
			ha->status_srb = NULL;
		}
	}

	LEAVE(__func__);
}


/*
*  qla2x00_error_entry
*      Processes error entry.
*
* Input:
*      ha           = adapter block pointer.
*      pkt          = entry pointer.
*/
STATIC void
qla2x00_error_entry(scsi_qla_host_t *ha, sts_entry_t *pkt) 
{
	srb_t *sp;

	ENTER(__func__);

#if defined(QL_DEBUG_LEVEL_2)
	if (pkt->entry_status & RF_INV_E_ORDER)
		printk("%s: Invalid Entry Order\n", __func__);
	else if (pkt->entry_status & RF_INV_E_COUNT)
		printk("%s: Invalid Entry Count\n", __func__);
	else if (pkt->entry_status & RF_INV_E_PARAM)
		printk("%s: Invalid Entry Parameter\n", __func__);
	else if (pkt->entry_status & RF_INV_E_TYPE)
		printk("%s: Invalid Entry Type\n", __func__);
	else if (pkt->entry_status & RF_BUSY)
		printk("%s: Busy\n", __func__);
	else
		printk("%s: UNKNOWN flag error\n", __func__);
#endif

	/* Validate handle. */
	if (pkt->handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[pkt->handle];
	else
		sp = NULL;

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[pkt->handle] = 0;
		if (ha->actthreads)
			ha->actthreads--;
		sp->lun_queue->out_cnt--;
		if (sp->fclun->io_cnt > 0)
			sp->fclun->io_cnt--;
		sp->fclun->s_time = jiffies - sp->u_start;
		ha->iocb_cnt -= sp->iocb_cnt;

		sp->flags |= SRB_ISP_COMPLETED;

		/* Bad payload or header */
		if (pkt->entry_status &
			(RF_INV_E_ORDER | RF_INV_E_COUNT |
			 RF_INV_E_PARAM | RF_INV_E_TYPE)) {
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		} else if (pkt->entry_status & RF_BUSY) {
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
		} else {
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		}
		/* Place command on done queue. */
		add_to_done_queue(ha, sp);

	} else if (pkt->entry_type == COMMAND_A64_TYPE ||
			pkt->entry_type == COMMAND_TYPE) {

		DEBUG2(printk(KERN_INFO "%s(): ISP Invalid handle\n", __func__);)
		printk(KERN_WARNING
			"qla2x00: Error Entry invalid handle");
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		if (ha->dpc_wait && !ha->dpc_active) 
			up(ha->dpc_wait);
	}

	LEAVE(__func__);
}


STATIC void
qla2x00_ms_entry(scsi_qla_host_t *ha, ms_iocb_entry_t *pkt) 
{
	srb_t          *sp;

	ENTER(__func__);

	DEBUG3(printk("%s(): pkt=%p pkthandle=%d.\n",
	    __func__, pkt, pkt->handle1);)
	
	DEBUG9(printk("%s: ct pkt dump:\n", __func__);)
	DEBUG9(qla2x00_dump_buffer((void *)pkt, sizeof(ms_iocb_entry_t));)

	/* Validate handle. */
	if (pkt->handle1 < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[pkt->handle1];
	else
		sp = NULL;

	if (sp == NULL) {
		printk(KERN_WARNING
			"qla2x00: MS Entry invalid handle.\n");

		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		return;
	}

	CMD_COMPL_STATUS(sp->cmd) = le16_to_cpu(pkt->status);
	CMD_ENTRY_STATUS(sp->cmd) = pkt->entry_status;

	/* Free outstanding command slot. */
	ha->outstanding_cmds[pkt->handle1] = 0;
	sp->flags |= SRB_ISP_COMPLETED;

	add_to_done_queue(ha, sp);

	LEAVE(__func__);
}

/*
 *  qla2x00_restart_queues
 *	Restart device queues.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
void
qla2x00_restart_queues(scsi_qla_host_t *ha, uint8_t flush) 
{
	srb_t  		*sp;
	int		retry_q_cnt = 0;
	int		pending_q_cnt = 0;
	struct list_head *list, *temp;
	unsigned long flags = 0;
	scsi_qla_host_t *vis_ha;

	ENTER(__func__);

	clear_bit(RESTART_QUEUES_NEEDED, &ha->dpc_flags);

	/*
	 * start pending queue
	 */
	pending_q_cnt = ha->qthreads;
	if (flush) {
		spin_lock_irqsave(&ha->list_lock,flags);
		list_for_each_safe(list, temp, &ha->pending_queue) {
			sp = list_entry(list, srb_t, list);

	    		if ((sp->flags & SRB_TAPE))
				continue;
			/* 
			 * When time expire return request back to OS as BUSY 
			 */
			__del_from_pending_queue(ha, sp);
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
			CMD_HANDLE(sp->cmd) = (unsigned char *)NULL;
			__add_to_done_queue(ha, sp);
		}
		spin_unlock_irqrestore(&ha->list_lock, flags);
	} else {
		if (!list_empty(&ha->pending_queue))
			qla2x00_next(ha);
	}

	/*
	 * Clear out our retry queue
	 */
	if (flush) {
		spin_lock_irqsave(&ha->list_lock, flags);
		retry_q_cnt = ha->retry_q_cnt;
		list_for_each_safe(list, temp, &ha->retry_queue) {
			sp = list_entry(list, srb_t, list);
			/* when time expire return request back to OS as BUSY */
			__del_from_retry_queue(ha, sp);
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
			CMD_HANDLE(sp->cmd) = (unsigned char *) NULL;
			__add_to_done_queue(ha, sp);
		}
		spin_unlock_irqrestore(&ha->list_lock, flags);

		DEBUG2(printk(KERN_INFO "%s(%ld): callback %d commands.\n",
				__func__,
				ha->host_no,
				retry_q_cnt);)
	}

	DEBUG2(printk(KERN_INFO "%s(%ld): active=%ld, retry=%d, pending=%d, "
			"done=%ld, failover=%d, scsi retry=%d commands.\n",
			__func__,
			ha->host_no,
			ha->actthreads,
			ha->retry_q_cnt,
			pending_q_cnt,
			ha->done_q_cnt,
			ha->failover_cnt,
			ha->scsi_retry_q_cnt);)

	if (ha->flags.failover_enabled) {
		/* Try and start all visible adapters */
		for (vis_ha=qla2x00_hostlist;
				(vis_ha != NULL); vis_ha=vis_ha->next) {

			if (!list_empty(&vis_ha->pending_queue))
				qla2x00_next(vis_ha);

#if 0
			DEBUG2(printk(KERN_INFO "host(%ld):Commands active=%d busy=%d "
					"failed=%d\nin_recovery=%d "
					"eh_active=%d\n ",
					vis_ha->host_no,
					atomic_read(&vis_ha->host->host_active),
					atomic_read(&vis_ha->host->host_busy),
					vis_ha->host->host_failed,
					vis_ha->host->in_recovery,
					vis_ha->host->eh_active);)	
#endif
		}
	}

	if (!list_empty(&ha->done_queue))
		qla2x00_done(ha);

	LEAVE(__func__);
}

/*
 *  qla2x00_abort_queues
 *	Abort all commands on queues on device
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
STATIC void
qla2x00_abort_queues(scsi_qla_host_t *ha, uint8_t doneqflg) 
{

	srb_t       *sp;
	struct list_head *list, *temp;
	unsigned long flags;

	ENTER(__func__);

	clear_bit(ABORT_QUEUES_NEEDED, &ha->dpc_flags);

	/* Return all commands device queues. */
	spin_lock_irqsave(&ha->list_lock,flags);
	list_for_each_safe(list, temp, &ha->pending_queue) {
		sp = list_entry(list, srb_t, list);

		if (sp->flags & SRB_ABORTED)
			continue;

		/* Remove srb from LUN queue. */
		__del_from_pending_queue(ha, sp);

		/* Set ending status. */
		CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;

		__add_to_done_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	LEAVE(__func__);
}

void
qla2x00_ioctl_error_recovery(scsi_qla_host_t *ha)	
{
	int return_status; 
	unsigned long flags;

	printk(KERN_INFO
	    "%s(%ld) issuing device reset\n", __func__,ha->host_no);
	if (!ha->ioctl_err_cmd) {
		printk("%s(%ld) should not occur\n", __func__, ha->host_no);
		return;
	}

#if !defined(SH_HAS_HOST_LOCK)
	spin_lock_irqsave(&io_request_lock, flags);
#else
	spin_lock_irqsave(ha->host->host_lock, flags);
#endif
	return_status = qla2xxx_eh_device_reset(ha->ioctl_err_cmd);
	if (return_status != SUCCESS){
		printk("%s(%ld) elevation to host_reset\n",
		    __func__, ha->host_no);
		return_status = qla2xxx_eh_host_reset(ha->ioctl_err_cmd);
		printk("%s(%ld) return_status=%x\n", __func__, ha->host_no,
		    return_status);
	}
	ha->ioctl_err_cmd = NULL ;
#if !defined(SH_HAS_HOST_LOCK)
	spin_unlock_irqrestore(&io_request_lock, flags);
#else
	spin_unlock_irqrestore(ha->host->host_lock, flags);
#endif
}


/*
 * qla2x00_reset_lun_fo_counts
 *	Reset failover retry counts
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
void 
qla2x00_reset_lun_fo_counts(scsi_qla_host_t *ha, os_lun_t *lq) 
{
	srb_t		*tsp;
	os_lun_t	*orig_lq;
	struct list_head *list;
	unsigned long	flags ;
	uint16_t        path_id;
	struct fo_information	*mp_info;

	spin_lock_irqsave(&ha->list_lock, flags);
	/*
	 * the pending queue.
	 */
	list_for_each(list,&ha->pending_queue) {
		tsp = list_entry(list, srb_t, list);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}
	/*
	 * the retry queue.
	 */
	list_for_each(list,&ha->retry_queue) {
		tsp = list_entry(list, srb_t, list);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}

	/*
	 * the done queue.
	 */
	list_for_each(list, &ha->done_queue) {
		tsp = list_entry(list, srb_t, list);
		orig_lq = tsp->lun_queue;
		if (orig_lq == lq)
			tsp->fo_retry_cnt = 0;
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);

	/* reset the failover retry count on all the paths */
	mp_info = (struct fo_information * ) lq->fo_info;
	for (path_id = 0; path_id < MAX_PATHS_PER_DEVICE ; path_id++) {
		mp_info->fo_retry_cnt[path_id] = 0;
	}
}

/*
 *  qla2x00_failover_cleanup
 *	Cleanup queues after a failover.
 *
 * Input:
 *	sp = command pointer
 *
 * Context:
 *	Interrupt context.
 */
STATIC void
qla2x00_failover_cleanup(srb_t *sp) 
{

	CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
	CMD_HANDLE(sp->cmd) = (unsigned char *) NULL;
	if( (sp->flags & SRB_GOT_SENSE ) ) {
		 sp->flags &= ~SRB_GOT_SENSE;
		 sp->cmd->sense_buffer[0] = 0;
	}

	/* turn-off all failover flags */
	sp->flags = sp->flags & ~(SRB_RETRY|SRB_FAILOVER|SRB_FO_CANCEL);
}


void qla2x00_find_all_active_ports(srb_t *sp) 
{
	scsi_qla_host_t *ha = qla2x00_hostlist;
	fc_port_t *fcport;
	fc_lun_t	*fclun;
	fc_lun_t	*orig_fclun;
	uint8_t		wait_for_transition = 1;

	DEBUG2(printk(KERN_INFO "%s: Scanning for active ports... %d\n",
			__func__, sp->lun_queue->fclun->lun);)
	orig_fclun = sp->lun_queue->fclun;
	for ( ; (ha != NULL); ha=ha->next) {
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->port_type != FCT_TARGET )
				continue;
			if (atomic_read(&fcport->state) == FC_DEVICE_DEAD ||
				  atomic_read(&fcport->ha->loop_state) == 
				  	LOOP_DEAD)
				continue;
       		 	if ( (fcport->flags & (FC_EVA_DEVICE|FC_MSA_DEVICE)) ) {
				list_for_each_entry(fclun, &fcport->fcluns, list) {
				 	if (fclun->flags & FC_VISIBLE_LUN)
					 	continue;
					if (orig_fclun->lun != fclun->lun)
					 	continue;
				 	qla2x00_test_active_lun(fcport,fclun, (uint8_t *)NULL);
				}
			}
       		 	if ( (fcport->flags & FC_MSA_DEVICE) )
				 qla2x00_test_active_port(fcport);
			
 			 /* Devices which support tgt port grp */
 			if (fcport->fo_target_port) {
 				DEBUG(printk("%s(%ld) issue report tgt port"
					" group\n",__func__,ha->host_no);)
 				list_for_each_entry(fclun, &fcport->fcluns,
 				    list) {
					/* check for luns not supporting Alua*/
 					if (fclun->flags & FC_VISIBLE_LUN)
 						continue;
 					if (orig_fclun->lun != fclun->lun)
 						continue;
					/* For DSXXX device do a quick check to see if 
					 * implicit failover is taking place.
					 */
					if ((fcport->flags & FC_DSXXX_DEVICE) && wait_for_transition) {
						  if (qla2x00_wait_for_tpg_ready(fclun)
                                                        == QLA2X00_FUNCTION_FAILED ) {
							DEBUG2(printk("%s(%ld) lun=%d"
								"Waiting for transition state - failed\n",
                                                                __func__,ha->host_no, fclun->lun);)
                                                        break;  /* don't waste time on the other luns */
                                                }
						wait_for_transition = 0;
					}
                                         /* set the tgt port grp state */
		   			if ( fcport->flags & FC_AA_MSA_DEVICE )
 					   fcport->fo_target_port(fcport, fclun, 0);
					else 
 					   fcport->fo_target_port(fcport, fclun, 1);
 				}
 			}
 			
		}
	}
	DEBUG2(printk(KERN_INFO "%s: Scanning ports...Done\n",
			__func__);)
}


int qla2x00_suspend_failover_targets(scsi_qla_host_t *ha) 
{
	unsigned long flags;
	struct list_head *list, *temp;
	srb_t       *sp;
	int 	 count;
	os_tgt_t	*tq;

	spin_lock_irqsave(&ha->list_lock, flags);
        count = ha->failover_cnt;
	list_for_each_safe(list, temp, &ha->failover_queue) {
		sp = list_entry(ha->failover_queue.next, srb_t, list);
		tq = sp->tgt_queue;
		if( !(test_bit(TGT_SUSPENDED, &tq->q_flags)) )
			set_bit(TGT_SUSPENDED, &tq->q_flags);
	}
	spin_unlock_irqrestore(&ha->list_lock,flags);

	return count;
}

srb_t *
qla2x00_failover_next_request(scsi_qla_host_t *ha) 
{
	unsigned long flags;
	srb_t       *sp = NULL;

	spin_lock_irqsave(&ha->list_lock, flags);
	if (!list_empty(&ha->failover_queue)) {
		sp = list_entry(ha->failover_queue.next, srb_t, list);
		__del_from_failover_queue(ha, sp);
	}
	spin_unlock_irqrestore(&ha->list_lock, flags);
	return( sp );
}

/*
 *  qla2x00_process_failover
 *	Process any command on the failover queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Interrupt context.
 */
STATIC void
qla2x00_process_failover(scsi_qla_host_t *ha) 
{

	os_tgt_t	*tq;
	os_lun_t	*lq;
	srb_t       *sp;
	fc_port_t *fcport;
	uint32_t    t, l;
	scsi_qla_host_t *vis_ha = ha;
	int 	 count, i;

	DEBUG2(printk(KERN_INFO "%s: hba %ld active=%ld, retry=%d, "
			"done=%ld, failover=%d, scsi retry=%d commands.\n",
			__func__,
			ha->host_no,
			ha->actthreads,
			ha->retry_q_cnt,
			ha->done_q_cnt,
			ha->failover_cnt,
			ha->scsi_retry_q_cnt);)


	/* Prevent acceptance of new I/O requests for failover target. */
	count = qla2x00_suspend_failover_targets(ha);

	/*
	 * Process all the commands in the failover queue. Attempt to failover
	 * then either complete the command as is or requeue for retry.
	 */
	for( i = 0; i < count ; i++ ) {
		sp = qla2x00_failover_next_request(ha); 
		if( sp == NULL )
			break;
		qla2x00_extend_timeout(sp->cmd, 360);
		if( i == 0 ) {
		 	vis_ha = (scsi_qla_host_t *) sp->cmd->host->hostdata;
		}
		tq = sp->tgt_queue;
		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;
		DEBUG(printk("%s(): pid %ld retrycnt=%d,"
		    	 	 "fcport =%p, state=0x%x, \nloop state=0x%x"
		    	 	 " fclun=%p, lq fclun=%p, lq=%p, lun=%d\n",
				__func__,
				sp->cmd->serial_number,
				sp->cmd->retries,
				fcport,
		  		 atomic_read(&fcport->state),
				 atomic_read(&ha->loop_state),
			 	 sp->fclun, lq->fclun, lq, lq->fclun->lun);)
		if ( sp->err_id == SRB_ERR_DEVICE &&
		     sp->fclun == lq->fclun && 
		     atomic_read(&fcport->state) == FC_ONLINE) {
		    if( !(qla2x00_test_active_lun(fcport, sp->fclun, (uint8_t *)NULL))  ) { 
			 DEBUG2(printk("scsi(%ld) %s Detected INACTIVE Port 0x%02x \n",
				ha->host_no,__func__,fcport->loop_id);)
			 sp->err_id = SRB_ERR_OTHER;
		 	 sp->cmd->sense_buffer[2] = 0;
		 	 CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
		    }	
		}
		if( (sp->flags & SRB_GOT_SENSE ) ) {
		 	 sp->flags &= ~SRB_GOT_SENSE;
		 	 sp->cmd->sense_buffer[0] = 0;
		 	 CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
		 	 CMD_HANDLE(sp->cmd) = (unsigned char *) NULL;
		}
		/*** Select an alternate path ***/

		/* if load balancing is enabled then adjust lq->fclun */
		if( qla2x00_cfg_is_lbenable(sp->fclun) )
			lq->fclun = sp->fclun;

		/* 
		 * If the path has already been change by a previous request
		 * sp->fclun != lq->fclun
		 */
		 if ( sp->err_id != SRB_ERR_OTHER ) {
			 if (atomic_read(&fcport->ha->loop_state) == LOOP_DEAD)
				 sp->err_id = SRB_ERR_LOOP;
			 else
				sp->err_id = SRB_ERR_PORT;
		 }

		if (sp->fclun != lq->fclun || 
			( sp->err_id != SRB_ERR_OTHER &&
			  atomic_read(&fcport->ha->loop_state)
					 != LOOP_DEAD &&
		  	atomic_read(&fcport->state) != FC_DEVICE_DEAD) ) {

			qla2x00_failover_cleanup(sp);
		} else if (qla2x00_cfg_failover(ha, lq->fclun,
						tq, sp) == NULL) {
			/*
			 * We ran out of paths, so just retry the status which
			 * is already set in the cmd. We want to serialize the 
			 * failovers, so we make them go thur visible HBA.
			 */
			printk(KERN_INFO
				"%s(): Ran out of paths - pid %ld - retrying\n",
				__func__,
				sp->cmd->serial_number);
		} else {
			/* 
			 * if load balancing is enabled then we need to flush the 
			 * other requests for the same lun 
			 */
			if( qla2x00_cfg_is_lbenable(sp->fclun) ){
				DEBUG2(printk("%s(): Flushing fo queue"
					" for lq=%p\n",
					__func__,
					lq);)
				qla2x00_flush_failover_q(vis_ha, lq);
			}
			qla2x00_failover_cleanup(sp);

		}
		add_to_done_queue(ha, sp);
	} 

	for (t = 0; t < vis_ha->max_targets; t++) {
		if ((tq = vis_ha->otgt[t]) == NULL)
			continue;
		if( test_and_clear_bit(TGT_SUSPENDED, &tq->q_flags) ) {
			/* EMPTY */
			DEBUG2(printk("%s(): remove suspend for "
					"target %d\n",
					__func__,
					t);)
		}
		for (l = 0; l < vis_ha->max_luns; l++) {
			if ((lq = (os_lun_t *) tq->olun[l]) == NULL)
				continue;

			if( test_and_clear_bit(LUN_MPIO_BUSY, &lq->q_flag) ) {
				/* EMPTY */
				DEBUG(printk("%s(): remove suspend for "
						"lun %d\n",
						__func__,
						lq->fclun->lun);)
			}
		}
	    }

	qla2x00_restart_queues(ha, FALSE);

	DEBUG2(printk("%s() - done\n", __func__);)
}

/*
 *  qla2x00_loop_resync
 *      Resync with fibre channel devices.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
STATIC uint8_t
qla2x00_loop_resync(scsi_qla_host_t *ha) 
{
	uint8_t   status;

	ENTER(__func__);

	DEBUG(printk("%s(): entered\n", __func__);)

	atomic_set(&ha->loop_state, LOOP_UPDATE);
	qla2x00_stats.loop_resync++;
	ha->total_loop_resync++;
	clear_bit(ISP_ABORT_RETRY, &ha->dpc_flags);
	if (ha->flags.online) {
		if (!(status = qla2x00_fw_ready(ha))) {
			do {
				/* v2.19.05b6 */
				atomic_set(&ha->loop_state,  LOOP_UPDATE);

				/*
				 * Issue marker command only when we are going
				 * to start the I/O .
				 */
				ha->marker_needed = 1;

				/* Remap devices on Loop. */
				clear_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);

				qla2x00_configure_loop(ha);

			} while (!atomic_read(&ha->loop_down_timer) &&
				!(test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) &&
				(test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)));
		}
		qla2x00_restart_queues(ha,TRUE);
	} else
		status = 0;

	if (test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) {
		return (1);
	}

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3)
	if (status)
		printk("%s(): **** FAILED ****\n", __func__);
#endif

	LEAVE(__func__);

	return(status);
}

/*
 * qla2x00_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
STATIC uint16_t
qla2x00_debounce_register(volatile uint16_t *addr) 
{
	uint16_t ret;
	uint16_t ret2;

	do {
		ret = RD_REG_WORD(addr);
		barrier();
		cpu_relax();
		ret2 = RD_REG_WORD(addr);
	} while (ret != ret2);

	return(ret);
}

#if defined(ISP2300)
/*
 * qla24xx_reset_chip
 *      Reset ISP chip.
 *
 * Input:
 *      ha = adapter block pointer.
 */
STATIC void
qla24xx_reset_chip(scsi_qla_host_t *ha) 
{
	struct device_reg_24xx *reg24 = (struct device_reg_24xx *)ha->iobase;
	uint32_t d2, cnt;
	unsigned long   flags = 0;
	uint16_t wd;

	/* Disable ISP interrupts. */
	qla2x00_disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Reset RISC */
	WRT_REG_DWORD(&reg24->ctrl_status, CSRX_DMA_SHUTDOWN | MWB_4096_BYTES);
	for (cnt = 0; cnt < 30000 ; cnt++) {
		if ((RD_REG_DWORD(&reg24->ctrl_status) & CSRX_DMA_ACTIVE) == 0)
			break;	
		udelay(10);
	}

	WRT_REG_DWORD(&reg24->ctrl_status, CSRX_ISP_SOFT_RESET |
			 CSRX_DMA_SHUTDOWN | MWB_4096_BYTES);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	/*
         * It is necessary to delay here since the card doesn't respond to PCI
         * reads during a reset. On some architectures this will result in an
         * MCA.
         */
        udelay(100);
        /* Wait for firmware to complete NVRAM accesses. */
        d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
        for (cnt = 10000 ; cnt && d2; cnt--) {
                udelay(5);
                d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
                barrier();
        }
 
        /* Wait for soft-reset to complete. */
	d2 = RD_REG_DWORD(&reg24->ctrl_status);
	for (cnt = 6000000 ; cnt && (d2 & CSRX_ISP_SOFT_RESET); cnt--) {
		udelay(5);
		d2 = RD_REG_DWORD(&reg24->ctrl_status);
		barrier();
	}

	WRT_REG_DWORD(&reg24->hccr, HCCRX_SET_RISC_RESET);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	WRT_REG_DWORD(&reg24->hccr, HCCRX_REL_RISC_PAUSE);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	WRT_REG_DWORD(&reg24->hccr, HCCRX_CLR_RISC_RESET);
	RD_REG_DWORD(&reg24->hccr);		/* PCI Posting. */

	d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
	for (cnt = 6000000 ; cnt && d2; cnt--) {
		udelay(5);
		d2 = (uint32_t) RD_REG_WORD(&reg24->mailbox0);
		barrier();
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return;

}
#endif
/*
 * qla2x00_reset_chip
 *      Reset ISP chip.
 *
 * Input:
 *      ha = adapter block pointer.
 */
STATIC void
qla2x00_reset_chip(scsi_qla_host_t *ha) 
{
	unsigned long   flags = 0;
	device_reg_t	*reg = ha->iobase;
	uint32_t	cnt;
	unsigned long	mbx_flags = 0;
	uint16_t        cmd;

	ENTER(__func__);

#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		qla24xx_reset_chip(ha);
		return;
	}	 
#endif

	/* Disable ISP interrupts. */
	qla2x00_disable_intrs(ha);
	/* WRT_REG_WORD(&reg->ictrl, 0); */

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Turn off master enable */
	cmd = 0;
	pci_read_config_word(ha->pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MASTER;
	pci_write_config_word(ha->pdev, PCI_COMMAND, cmd);

/* ??? -- Safely remove??? */
#if 1
	/* Pause RISC. */
	WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
#if defined(ISP2300)
	if (check_all_device_ids(ha)) { 	    
		PCI_POSTING(&reg->host_cmd);
		UDELAY(10);
	} else {
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->host_cmd) & HC_RISC_PAUSE) != 0)
				break;
			else
				UDELAY(100);
		}
	}
#else
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((RD_REG_WORD(&reg->host_cmd) & HC_RISC_PAUSE) != 0)
			break;
		else
			UDELAY(100);
	}
#endif

	/* Select FPM registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0x20);
	PCI_POSTING(&reg->ctrl_status);

	/* FPM Soft Reset. */
	WRT_REG_WORD(&reg->fpm_diag_config, 0x100);
	PCI_POSTING(&reg->fpm_diag_config);
#if defined(ISP2300)
	WRT_REG_WORD(&reg->fpm_diag_config, 0x0); /* Toggle Fpm Reset */
	PCI_POSTING(&reg->fpm_diag_config);
#endif
	/* Select frame buffer registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0x10);
	PCI_POSTING(&reg->ctrl_status);

	/* Reset frame buffer FIFOs. */
#if defined(ISP2200)
	WRT_REG_WORD(&reg->fb_cmd, 0xa000);
	PCI_POSTING(&reg->fb_cmd);
#else
	WRT_REG_WORD(&reg->fb_cmd, 0x00fc);

	/* Read back fb_cmd until zero or 3 seconds max */
	for (cnt = 0; cnt < 3000; cnt++) {
		if ((RD_REG_WORD(&reg->fb_cmd) & 0xff) == 0)
			break;
		udelay(100);
	}


#endif

	/* Select RISC module registers. */
	WRT_REG_WORD(&reg->ctrl_status, 0);
	PCI_POSTING(&reg->ctrl_status);

	WRT_REG_WORD(&reg->semaphore, 0);
	PCI_POSTING(&reg->semaphore);

	WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	PCI_POSTING(&reg->host_cmd);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);
	PCI_POSTING(&reg->host_cmd);

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);

#if defined(ISP2300)
	if (check_all_device_ids(ha)) { 	    
		PCI_POSTING(&reg->ctrl_status);
		UDELAY(10);
	} else {
		/* Wait for RISC to recover from reset. */
		for (cnt = 30000; cnt; cnt--) {
			if (!(RD_REG_WORD(&reg->ctrl_status) &
						CSR_ISP_SOFT_RESET))
				break;
			UDELAY(100);
		}
	}
#else
	/* Wait for RISC to recover from reset. */
	for (cnt = 30000; cnt; cnt--) {
		if (!(RD_REG_WORD(&reg->ctrl_status) & CSR_ISP_SOFT_RESET))
			break;
		UDELAY(100);
	}
#endif

	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
	PCI_POSTING(&reg->host_cmd);
	WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
	PCI_POSTING(&reg->host_cmd);

#if defined(ISP2300)
	if (check_all_device_ids(ha)) { 	    
		UDELAY(10);
	} else {
		for (cnt = 0; cnt < 30000; cnt++) {
			/* ra 12/30/01 */
			if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
				QLA_MBX_REG_LOCK(ha);

			if (RD_REG_WORD(&reg->mailbox0) != MBS_BUSY) {
				if (!(test_bit(ABORT_ISP_ACTIVE,
							&ha->dpc_flags)))
					QLA_MBX_REG_UNLOCK(ha);
				break;
			}

			if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
				QLA_MBX_REG_UNLOCK(ha);

			UDELAY(100);
		}
	}
#else
	for (cnt = 0; cnt < 30000; cnt++) {
		/* ra 12/30/01 */
		if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
			QLA_MBX_REG_LOCK(ha);

		if (RD_REG_WORD(&reg->mailbox0) != MBS_BUSY) {
			if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
				QLA_MBX_REG_UNLOCK(ha);
			break;
		}

		if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
			QLA_MBX_REG_UNLOCK(ha);

		UDELAY(100);
	}
#endif

#if defined(ISP2200) || defined(ISP2300)
	/* Disable RISC pause on FPM parity error. */
	WRT_REG_WORD(&reg->host_cmd, HC_DISABLE_PARITY_PAUSE);
#endif

#else
	/* Insure mailbox registers are free. */
	WRT_REG_WORD(&reg->semaphore, 0);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);
	PCI_POSTING(&reg->host_cmd);

	/* clear mailbox busy */

	ha->flags.mbox_busy = FALSE;

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	/* Delay after reset, for chip to recover. */
	udelay(20);

	for (cnt = 30000; cnt; cnt--) {
		if (!(RD_REG_WORD(&reg->ctrl_status) & CSR_ISP_SOFT_RESET))
			break;
		UDELAY(100);
	}

	/* Reset RISC processor. */
	WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
	PCI_POSTING(&reg->host_cmd);
	WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
	PCI_POSTING(&reg->host_cmd);
	for (cnt = 30000; cnt; cnt--) {
		if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
			QLA_MBX_REG_LOCK(ha);
		if (RD_REG_WORD(&reg->mailbox0) != MBS_BUSY ) {
			if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
				QLA_MBX_REG_UNLOCK(ha);
			break;
		}
		if (!(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)))
			QLA_MBX_REG_UNLOCK(ha);
		UDELAY(100);
	}
#endif

	/* Turn on master enable */
	cmd |= PCI_COMMAND_MASTER;
	pci_write_config_word(ha->pdev, PCI_COMMAND, cmd);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	LEAVE(__func__);
}

/*
 * This routine will wait for fabric devices for
 * the reset delay.
 */
void qla2x00_check_fabric_devices(scsi_qla_host_t *ha) 
{
	uint16_t	fw_state;

	qla2x00_get_firmware_state(ha, &fw_state);
}

/*
 * qla2x00_extend_timeout
 *      This routine will extend the timeout to the specified value.
 *
 * Input:
 *      cmd = SCSI command structure
 *
 * Returns:
 *      None.
 */
static void 
qla2x00_extend_timeout(Scsi_Cmnd *cmd, int timeout) 
{
	srb_t *sp = (srb_t *) CMD_SP(cmd);

    	sp->ext_history= 0; 
	sp->e_start = jiffies;
        if (del_timer(&cmd->eh_timeout)) {
		u_long our_jiffies;

		our_jiffies = (timeout * HZ) + cmd->eh_timeout.expires;
		mod_timer(&cmd->eh_timeout,our_jiffies);
    	 	sp->ext_history |= 1;
	}

	if (del_timer(&sp->timer)) {

		u_long our_jiffies;
		/* 
		 * Our internal timer should timeout before the midlayer has a
		 * chance begin the abort process
		 */
		our_jiffies = (timeout * HZ) + sp->timer.expires; 
		mod_timer(&sp->timer,our_jiffies - (QLA_CMD_TIMER_DELTA * HZ));
    	 	sp->ext_history |= 2;
	}
}

/*
* qla2x00_display_fc_names
*      This routine will the node names of the different devices found
*      after port inquiry.
*
* Input:
*      cmd = SCSI command structure
*
* Returns:
*      None.
*/
STATIC void
qla2x00_display_fc_names(scsi_qla_host_t *ha) 
{
	uint16_t	tgt;
	os_tgt_t	*tq;

	/* Display the node name for adapter */
	printk(KERN_INFO
		"scsi-qla%d-adapter-node=%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
		(int)ha->instance, ha->node_name[0], ha->node_name[1],
		ha->node_name[2], ha->node_name[3], ha->node_name[4],
		ha->node_name[5], ha->node_name[6], ha->node_name[7]);

	/* display the port name for adapter */
	printk(KERN_INFO
		"scsi-qla%d-adapter-port=%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
		(int)ha->instance, ha->port_name[0], ha->port_name[1],
		ha->port_name[2], ha->port_name[3], ha->port_name[4],
		ha->port_name[5], ha->port_name[6], ha->port_name[7]);

	/* Print out device port names */
	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if ((tq = ha->otgt[tgt]) == NULL)
			continue;

		if (tq->vis_port == NULL)
			continue;

		switch (ha->binding_type) {
			case BIND_BY_PORT_NAME:
				printk(KERN_INFO
					"scsi-qla%d-tgt-%d-di-0-port="
					"%02x%02x%02x%02x%02x%02x%02x%02x\\;\n",
					(int)ha->instance, 
					tgt,
					tq->port_name[0], 
					tq->port_name[1],
					tq->port_name[2], 
					tq->port_name[3],
					tq->port_name[4], 
					tq->port_name[5],
					tq->port_name[6], 
					tq->port_name[7]);

				break;

			case BIND_BY_PORT_ID:
				printk(KERN_INFO
					"scsi-qla%d-tgt-%d-di-0-pid=%06x\\;\n",
					(int)ha->instance, 
					tgt,
					tq->d_id.b24);
				break;
		}

#if VSA
		printk(KERN_INFO
			"scsi-qla%d-target-%d-vsa=01;\n",
			(int)ha->instance, tgt);
#endif
	}
}

#if defined(ISP2300)
static int
qla2x00_get_prop_int(scsi_qla_host_t *ha, char *propname, int *propval)
{
	char		*propstr;
	int		rval = -1;
	char		buf[LINESIZE];

	/* Get the requested property string */
	rval = qla2x00_find_propname(ha, propname, buf, ha->cmdline,
	    LINESIZE/2);
	if (!rval)
		return -1;

	propstr = &buf[0];
	if (*propstr == '=')
		propstr++;   /* ignore equal sign */

	rval = sscanf(propstr, "%d", propval);
	return rval;
}
#endif

/*
 * qla2x00_find_propname
 *	Get property in database.
 *
 * Input:
 *	ha = adapter structure pointer.
 *      db = pointer to database
 *      propstr = pointer to dest array for string
 *	propname = name of property to search for.
 *	siz = size of property
 *
 * Returns:
 *	0 = no property
 *      size = index of property
 *
 * Context:
 *	Kernel context.
 */
STATIC uint8_t
qla2x00_find_propname(scsi_qla_host_t *ha, 
			char *propname, char *propstr, 
			char *db, int siz) 
{
	char	*cp;

	/* find the specified string */
	if (db) {
		/* find the property name */
		if ((cp = strstr(db,propname)) != NULL) {
			while ((*cp)  && *cp != '=')
				cp++;
			if (*cp) {
				strncpy(propstr, cp, siz+1);
				propstr[siz+1] = '\0';
				DEBUG(printk("qla2x00_find_propname: found "
						"property = {%s}\n",
						propstr);)
				return (siz);   /* match */
			}
		}
	}

	return (0);
}


/*
 * qla2x00_get_prop_16chars
 *	Get an 8-byte property value for the specified property name by
 *      converting from the property string found in the configuration file.
 *      The resulting converted value is in big endian format (MSB at byte0).
 *
 * Input:
 *	ha = adapter state pointer.
 *	propname = property name pointer.
 *	propval  = pointer to location for the converted property val.
 *      db = pointer to database
 *
 * Returns:
 *	0 = value returned successfully.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_get_prop_16chars(scsi_qla_host_t *ha,
				char *propname, char *propval, char *db) 
{
	char		*propstr;
	int		i, k;
	int		rval;
	uint8_t		nval;
	uint8_t		*pchar;
	uint8_t		*ret_byte;
	uint8_t		*tmp_byte;
	uint8_t		*retval = (uint8_t*)propval;
	uint8_t		tmpval[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	uint16_t	max_byte_cnt = 8; /* 16 chars = 8 bytes */
	uint16_t	max_strlen = 16;
	static char	buf[LINESIZE];

	rval = qla2x00_find_propname(ha, propname, buf, db, max_strlen);

	propstr = &buf[0];
	if (*propstr == '=')
		propstr++;   /* ignore equal sign */

	if (rval == 0) {
		return (1);
	}

	/* Convert string to numbers. */
	pchar = (uint8_t *)propstr;
	tmp_byte = (uint8_t *)tmpval;

	rval = 0;
	for (i = 0; i < max_strlen; i++) {
		/*
		 * Check for invalid character, two at a time,
		 * then convert them starting with first byte.
		 */

		if ((pchar[i] >= '0') && (pchar[i] <= '9')) {
			nval = pchar[i] - '0';
		} else if ((pchar[i] >= 'A') && (pchar[i] <= 'F')) {
			nval = pchar[i] - 'A' + 10;
		} else if ((pchar[i] >= 'a') && (pchar[i] <= 'f')) {
			nval = pchar[i] - 'a' + 10;
		} else {
			/* invalid character */
			rval = 1;
			break;
		}

		if (i & BIT_0) {
			*tmp_byte = *tmp_byte | nval;
			tmp_byte++;
		} else {
			*tmp_byte = *tmp_byte | nval << 4;
		}
	}

	if (rval != 0) {
		/* Encountered invalid character. */
		return (rval);
	}

	/* Copy over the converted value. */
	ret_byte = retval;
	tmp_byte = tmpval;

	i = max_byte_cnt;
	k = 0;
	while (i--) {
		*ret_byte++ = *tmp_byte++;
	}

	/* big endian retval[0]; */
	return (0);
}

/*
* qla2x00_get_properties
*	Find all properties for the specified adapeter in
*      command line.
*
* Input:
*	ha = adapter block pointer.
*	cmdline = pointer to command line string
*
* Context:
*	Kernel context.
*/
static void
qla2x00_get_properties(scsi_qla_host_t *ha, char *cmdline) 
{
	int rval;
	static char propbuf[LINESIZE];
	uint8_t	tmp_name[8];

	/* Adapter FC node names. */
	sprintf(propbuf, "scsi-qla%d-adapter-node", (int) ha->instance);

	if( !ql2xdevflag )
		sprintf(propbuf, "scsi-qla%d-adapter-node", (int) ha->instance);
	else
		sprintf(propbuf, "%d-h", (int) ha->instance);

	rval = qla2x00_get_prop_16chars (ha, propbuf, tmp_name, cmdline);
	if (!rval)
		memcpy(ha->node_name, tmp_name, WWN_SIZE);

	/* DG 04/07 check portname of adapter */
	sprintf(propbuf, "scsi-qla%d-adapter-port", (int) ha->instance);

	if( !ql2xdevflag )
		sprintf(propbuf, "scsi-qla%d-adapter-port", (int) ha->instance);
	else
		sprintf(propbuf, "%d-w", (int) ha->instance);

	rval = qla2x00_get_prop_16chars (ha, propbuf, tmp_name, cmdline);
	if (!rval && memcmp(ha->port_name, tmp_name, 8) != 0) {
		/*
		 * Adapter port name is WWN, and cannot be changed.
		 * Inform users of the mismatch, then just continue driver
		 * loading using the original adapter port name in NVRAM.
		 */
		printk(KERN_WARNING
			"qla2x00: qla%ld found mismatch in "
			"adapter port names.\n",
			ha->instance);
		printk(KERN_INFO
			"       qla%ld port name found in NVRAM "
			"-> %02x%02x%02x%02x%02x%02x%02x%02x\n",
			ha->instance, ha->port_name[0], ha->port_name[1],
			ha->port_name[2], ha->port_name[3], ha->port_name[4],
			ha->port_name[5], ha->port_name[6], ha->port_name[7]);
		printk(KERN_INFO
			"      qla%ld port name found on command line "
			"-> %02x%02x%02x%02x%02x%02x%02x%02x\n",
			ha->instance,
			tmp_name[0],
			tmp_name[1],
			tmp_name[2],
			tmp_name[3],
			tmp_name[4],
			tmp_name[5],
			tmp_name[6],
			tmp_name[7]);
		printk(KERN_INFO
			"      Using port name from NVRAM.\n");
	}

	qla2x00_cfg_persistent_binding(ha);
}

/*
 * qla2x00_device_resync
 *	Marks devices in the database that needs resynchronization.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_device_resync(scsi_qla_host_t *ha) 
{
	uint32_t mask;
	rscn_t dev;
	fc_port_t *fcport;

	ENTER(__func__);

	while (ha->rscn_out_ptr != ha->rscn_in_ptr ||
			ha->flags.rscn_queue_overflow) {

		memcpy(&dev, &ha->rscn_queue[ha->rscn_out_ptr], sizeof(rscn_t));

		DEBUG(printk("qla%ld: device_resync: rscn_queue[%d], "
				"portID=%06x\n",
				ha->instance,
				ha->rscn_out_ptr,
				ha->rscn_queue[ha->rscn_out_ptr].d_id.b24);)

		ha->rscn_out_ptr++;
		if (ha->rscn_out_ptr == MAX_RSCN_COUNT)
			ha->rscn_out_ptr = 0;

		/* Queue overflow, set switch default case. */
		if (ha->flags.rscn_queue_overflow) {
			DEBUG(printk("device_resync: rscn overflow\n");)

			dev.format = 3;
			ha->flags.rscn_queue_overflow = 0;
		}

		switch (dev.format) {
			case 0:
				mask = 0xffffff;
				break;
			case 1:
				mask = 0xffff00;
				break;
			case 2:
				mask = 0xff0000;
				break;
			default:
				mask = 0x0;
				dev.d_id.b24 = 0;
				ha->rscn_out_ptr = ha->rscn_in_ptr;
				break;
		}

		/* Mark target devices indicated by RSCN for later processing */
		list_for_each_entry(fcport, &ha->fcports, list) {
			if ((fcport->flags & FC_FABRIC_DEVICE) == 0 ||
			    (fcport->d_id.b24 & mask) != dev.d_id.b24 ||
			    fcport->port_type == FCT_BROADCAST)
				continue;

			if (atomic_read(&fcport->state) == FC_ONLINE) {
				if (dev.format != 3 ||
				    fcport->port_type != FCT_INITIATOR) {
					atomic_set(&fcport->state,
					    FC_DEVICE_LOST);
				}
			}
		}
	}

	LEAVE(__func__);
}

/*
 * qla2x00_find_new_loop_id
 *	Scan through our port list and find a new usable loop ID.
 *
 * Input:
 *	ha:	adapter state pointer.
 *	dev:	port structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_find_new_loop_id(scsi_qla_host_t *ha, fc_port_t *dev)
{
	int	rval;
	int	found;
	fc_port_t *fcport;
	uint16_t first_loop_id;

	rval = QL_STATUS_SUCCESS;

	/* Save starting loop ID. */
	first_loop_id = dev->loop_id;

	for (;;) {
		/* Skip loop ID if already used by adapter. */
		if (dev->loop_id == ha->loop_id) {
			dev->loop_id++;
		}

		/* Skip reserved loop IDs. */
		while (RESERVED_LOOP_ID(dev->loop_id)) {
			dev->loop_id++;
		}

		/* Reset loop ID if passed the end. */
		if (dev->loop_id > SNS_LAST_LOOP_ID) {
			/* first loop ID. */
			dev->loop_id = ha->min_external_loopid;
		}

		/* Check for loop ID being already in use. */
		found = 0;
		fcport = NULL;
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->loop_id == dev->loop_id && fcport != dev) {
				/* ID possibly in use */
				found++;
				break;
			}
		}

		/* If not in use then it is free to use. */
		if (!found) {
			break;
		}

		/* ID in use. Try next value. */
		dev->loop_id++;

		/* If wrap around. No free ID to use. */
		if (dev->loop_id == first_loop_id) {
			dev->loop_id = FC_NO_LOOP_ID;
			rval = QL_STATUS_ERROR;
			break;
		}
	}

	return (rval);
}


/*
 * qla2x00_find_changed_local_port
 *	Find the specified port by loop id.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	loopid:		loop id of port.
 *			
 *
 * Returns:
 *	Pointer to port found, or NULL if not found
 *
 * Context:
 *	Interrupt context.
 */
static fc_port_t *
qla2x00_find_changed_local_port( scsi_qla_host_t *ha, uint16_t loopid)
{
	fc_port_t *fcport = NULL;

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->loop_id == loopid) {
			DEBUG2(printk(KERN_INFO "%s found fcport=%p for" 
			    " loopid=0x%x\n",__func__,fcport,loopid);)
			return (fcport);
		}
	}
	return (NULL);	
}


/*
 * qla2x00_fabric_dev_login
 *	Login fabric target device and update FC port database.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		port structure list pointer.
 *	next_loopid:	contains value of a new loop ID that can be used
 *			by the next login attempt.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_fabric_dev_login(scsi_qla_host_t *ha, fc_port_t *fcport,
    uint16_t *next_loopid)
{
	int	rval;
	int	retry;

	rval = QL_STATUS_SUCCESS;
	retry = 0;

	rval = qla2x00_fabric_login(ha, fcport, next_loopid);
	if (rval == QL_STATUS_SUCCESS) {
		rval = qla2x00_get_port_database(ha, fcport, 0);
		if (rval != QL_STATUS_SUCCESS) {
			qla2x00_fabric_logout(ha, fcport->loop_id,
			 fcport->d_id.b.domain, fcport->d_id.b.area,
			 fcport->d_id.b.al_pa);
		} else {
			rval = qla2x00_update_fcport(ha, fcport);
		}
	}

	return (rval);
}

/*
 * qla2x00_fabric_login
 *	Issue fabric login command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	device = pointer to FC device type structure.
 *
 * Returns:
 *      0 - Login successfully
 *      1 - Login failed
 *      2 - Initiator device
 *      3 - Fatal error
 */
static uint8_t
qla2x00_fabric_login(scsi_qla_host_t *ha, fc_port_t *fcport,
    uint16_t *next_loopid)
{
	int	rval;
	int	retry;
	uint16_t tmp_loopid;
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	retry = 0;
	tmp_loopid = 0;

	for (;;) {
		DEBUG(printk("scsi(%ld): Trying Fabric Login w/loop id 0x%04x "
 		    "for port %02x%02x%02x.\n",
 		    ha->host_no, fcport->loop_id, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa));

		/* Login fcport on switch. */
		qla2x00_login_fabric(ha, fcport->loop_id,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mb, BIT_0);
		if (mb[0] == MBS_PORT_ID_USED) {
			/*
			 * Device has another loop ID.  The firmware team
			 * recommends us to perform an implicit login with the
			 * specified ID again. The ID we just used is save here
			 * so we return with an ID that can be tried by the
			 * next login.
			 */
			retry++;
			tmp_loopid = fcport->loop_id;
			fcport->loop_id = mb[1];

			DEBUG(printk("Fabric Login: port in use - next "
 			    "loop id=0x%04x, port Id=%02x%02x%02x.\n",
			    fcport->loop_id, fcport->d_id.b.domain,
			    fcport->d_id.b.area, fcport->d_id.b.al_pa));

		} else if (mb[0] == MBS_COMMAND_COMPLETE) {
			/*
			 * Login succeeded.
			 */
			if (retry) {
				/* A retry occurred before. */
				*next_loopid = tmp_loopid;
			} else {
				/*
				 * No retry occurred before. Just increment the
				 * ID value for next login.
				 */
				*next_loopid = (fcport->loop_id + 1);
			}

			if (mb[1] & BIT_0) {
				fcport->port_type = FCT_INITIATOR;
			} else {
				fcport->port_type = FCT_TARGET;
				if (mb[1] & BIT_1) {
					fcport->flags |= FC_TAPE_DEVICE;
				}
			}

			rval = QL_STATUS_SUCCESS;
			break;
		} else if (mb[0] == MBS_LOOP_ID_USED) {
			/*
			 * Loop ID already used, try next loop ID.
			 */
			fcport->loop_id++;
			rval = qla2x00_find_new_loop_id(ha, fcport);
			if (rval != QL_STATUS_SUCCESS) {
				/* Ran out of loop IDs to use */
				break;
			}
		} else if (mb[0] == MBS_CMD_ERR) {
			/*
			 * Firmware possibly timed out during login. If NO
			 * retries are left to do then the device is declared
			 * dead.
			 */
			*next_loopid = fcport->loop_id;
			qla2x00_fabric_logout(ha, fcport->loop_id,
			 fcport->d_id.b.domain, fcport->d_id.b.area,
			 fcport->d_id.b.al_pa);
			fcport->loop_id = FC_NO_LOOP_ID;
			if (mb[1] ==  MBS_SC_TOPOLOGY_ERR){
				printk(KERN_INFO "%s:HBA trying to log "
				    "through FL_Port\n", __func__);
				DEBUG2(printk(KERN_INFO "%s:HBA trying to log "
				    "through FL_Port\n", __func__);)

				atomic_set(&fcport->state, FC_DEVICE_DEAD);
			} else {
				/* Schedule relogin if its not already done */ 
				if (fcport->login_retry == 0) {
					DEBUG2(printk("%s schedule relogin for"
						"fcport =%p\n",__func__,fcport);) 
					qla2x00_mark_device_lost(ha, fcport, 1);
				}
			}

			rval = 1;
			break;
		} else {
			/*
			 * unrecoverable / not handled error
			 */
			DEBUG2(printk("%s(%ld): failed=%x port_id=%02x%02x%02x "
 			    "loop_id=%x jiffies=%lx.\n", 
 			    __func__, ha->host_no, mb[0], 
			    fcport->d_id.b.domain, fcport->d_id.b.area,
			    fcport->d_id.b.al_pa, fcport->loop_id, jiffies));

    			/* Trying to log into more than 8 Target */
    			if(mb[0] == MBS_ALL_LOOP_IDS_IN_USE){
				printk(KERN_INFO "%s:No more loop ids\n"
				    ,__func__);
				DEBUG2(printk("%s:No more loop ids\n"
				    ,__func__);)
			}
			*next_loopid = fcport->loop_id;
			qla2x00_fabric_logout(ha, fcport->loop_id,
			 fcport->d_id.b.domain, fcport->d_id.b.area,
			 fcport->d_id.b.al_pa);
			fcport->loop_id = FC_NO_LOOP_ID;
			fcport->login_retry = 0;

			rval = 3;
			break;
		}
	}

	return (rval);
}

/*
 * qla2x00_configure_fabric
 *      Setup SNS devices with loop ID's.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 *      BIT_0 = error
 *      BIT_1 = database was full and device was not configured.
 */
#define MAX_PUBLIC_LOOP_IDS SNS_LAST_LOOP_ID + 1

STATIC uint8_t
qla2x00_configure_fabric(scsi_qla_host_t *ha) 
{
	uint8_t     rval = 0;
	uint8_t     rval1;
	int	rval2;
	sns_cmd_rsp_t  *sns;
	uint8_t     tmp_name[8];
	dma_addr_t  phys_address = 0;
	uint16_t    tmp_loop_id, loop_id;
	uint16_t    tmp_topo;
	fc_port_t	*fcport, *fcptemp;
	uint16_t	next_loopid;
	LIST_HEAD(new_fcports);
#if REG_FC4_ENABLED
	uint16_t	mb[MAILBOX_REGISTER_COUNT];
#endif

	ENTER(__func__);

	DEBUG2(printk(KERN_INFO "scsi(%ld): Enter qla2x00_configure_fabric:" 
			    "hba=%p\n", ha->host_no, ha);)

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		loop_id = NPH_F_PORT;
	else
		loop_id = SNS_FL_PORT;
	
	/* If FL port exists, then SNS is present */
	rval1 = qla2x00_get_port_name(ha, loop_id, tmp_name, 0);
	if (rval1 || qla2x00_is_wwn_zero(tmp_name)) {
		DEBUG2(printk(KERN_INFO "%s(): MBC_GET_PORT_NAME Failed, No FL Port\n",
				__func__);)

		ha->device_flags &= ~SWITCH_FOUND;
		return (0);
	}

	ha->device_flags |= SWITCH_FOUND;

	/* Get adapter port ID. */
	rval = qla2x00_get_adapter_id(ha, &tmp_loop_id, &ha->d_id.b.al_pa,
			&ha->d_id.b.area, &ha->d_id.b.domain, &tmp_topo);

	if (rval != QL_STATUS_SUCCESS) {
		DEBUG2(printk(KERN_WARNING
			"%s(%ld): ERROR Get host loop ID.\n", __func__,
			ha->host_no);)
		if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
		    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		    (atomic_read(&ha->loop_state) == LOOP_DOWN) ||
		    atomic_read(&ha->loop_down_timer) ||
		    (rval == QL_STATUS_MBX_CMD_ERR && loop_id == 0x7)) {
			DEBUG2(printk("%s(%ld) Loop is in a transition state\n",
			    	__func__, ha->host_no);)
		} else {
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		}
		return (0);
	}

	sns = pci_alloc_consistent(ha->pdev, 
			sizeof(sns_cmd_rsp_t), 
			&phys_address);
	if (sns == NULL) {
		printk(KERN_WARNING
			"qla(%ld): Memory Allocation failed - sns.\n",
			ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}

	memset(sns, 0, sizeof(sns_cmd_rsp_t));

	/* Mark devices that need re-synchronization. */
	qla2x00_device_resync(ha);
	do {
#if REG_FDMI_ENABLED
		/* FDMI support */
		/* login to management server */
		if (ql2xfdmienable) {
			if (test_and_clear_bit(FDMI_REGISTER_NEEDED,
			    &ha->fdmi_flags)) {
				if (qla2x00_mgmt_svr_login(ha) !=
				    QL_STATUS_SUCCESS) {
					DEBUG2(printk("%s(%ld): failed MS "
					    "server login.\n", __func__,
					    ha->host_no);)
				} else {
					/* use mbx commands to send commands */
					qla2x00_fdmi_register(ha);
				}
			}
		}
#endif

#if REG_FC4_ENABLED
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
			loop_id = NPH_SNS;
		else
			loop_id = SIMPLE_NAME_SERVER;
		/* Ensure we are logged into the SNS. */
		qla2x00_login_fabric(ha, loop_id, 0xff, 0xff, 0xfc,
		    mb, BIT_0);
		if (mb[0] != MBS_COMMAND_COMPLETE) {
			printk(KERN_INFO "scsi(%ld): Failed SNS login: "
			    "loop_id=%x mb[0]=%x mb[1]=%x mb[2]=%x mb[6]=%x "
			    "mb[7]=%x\n", ha->host_no, SIMPLE_NAME_SERVER,
			    mb[0], mb[1], mb[2], mb[6], mb[7]);
			return (QL_STATUS_ERROR);
		}
		if (test_and_clear_bit(REGISTER_FC4_NEEDED, &ha->dpc_flags)) {
			if (qla2x00_register_fc4(ha, sns, phys_address)) {
				/* EMPTY */
				DEBUG2(printk(KERN_INFO
				    "%s(%ld): register_fc4 failed.\n",
				    __func__,
				    ha->host_no);)
			}
			if (qla2x00_register_fc4_feature(ha, sns,
			    phys_address)) {
				/* EMPTY */
				DEBUG2(printk(KERN_INFO
				    "%s(%ld): register_fc4_feature failed.\n",
				    __func__,
				    ha->host_no);)
			}

			if (qla2x00_register_nn(ha, sns, phys_address)){
				/* EMPTY */
				DEBUG2(printk("%s(%ld): register_nodename"
				    " failed.\n", __func__,
				    ha->host_no);)
				
			} else {
				if (qla2x00_register_snn(ha)){
				/* EMPTY */
				DEBUG2(printk("%s(%ld): register_symbolic_"
				    "node_name failed.\n", __func__,
				    ha->host_no);)
				}
			}
		}
#endif
		rval = qla2x00_find_all_fabric_devs(ha, sns, phys_address,
		    &new_fcports);
		if (rval != 0)
			break;

		/*
		 * Logout all previous fabric devices marked lost, except
		 * tape devices.
		 */
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
				break;

			if ((fcport->flags & FC_FABRIC_DEVICE) == 0)
				continue;

			if (atomic_read(&fcport->state) == FC_DEVICE_LOST) {
				qla2x00_mark_device_lost(ha, fcport,
				    ql2xplogiabsentdevice);

				if (fcport->loop_id != FC_NO_LOOP_ID &&
				    (fcport->flags & FC_TAPE_DEVICE) == 0 &&
				    fcport->port_type != FCT_INITIATOR &&
				    fcport->port_type != FCT_BROADCAST) {

					qla2x00_fabric_logout(ha,
					    fcport->loop_id,
					 fcport->d_id.b.domain, 
					 fcport->d_id.b.area,
					 fcport->d_id.b.al_pa);
					fcport->loop_id = FC_NO_LOOP_ID;
				}
			}
		}

		/* Starting free loop ID. */
		next_loopid = ha->min_external_loopid;

		/*
		 * Scan through our port list and login entries that need to be
		 * logged in.
		 */
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (atomic_read(&ha->loop_down_timer) ||
			    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
				break;

			if ((fcport->flags & FC_FABRIC_DEVICE) == 0 ||
			    (fcport->flags & FC_LOGIN_NEEDED) == 0)
				continue;

			if (fcport->loop_id == FC_NO_LOOP_ID) {
				fcport->loop_id = next_loopid;
				rval = qla2x00_find_new_loop_id(ha, fcport);
				if (rval != QL_STATUS_SUCCESS) {
					/* Ran out of IDs to use */
					break;
				}
			}

			/* Login and update database */
			rval2 = qla2x00_fabric_dev_login(ha, fcport,
			    &next_loopid);
			if ((rval2 & BIT_0) && ha->mem_err) {		
				rval = QL_STATUS_RESOURCE_ERROR;
				break;
			}
		}

		/* Exit if out of loop IDs. */
		if (rval != QL_STATUS_SUCCESS) {
			break;
		}

		/*
		 * Login and add the new devices to our port list.
		 */
		list_for_each_entry_safe(fcport, fcptemp, &new_fcports, list) {
			if (atomic_read(&ha->loop_down_timer) ||
			    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
				break;

			/* Find a new loop ID to use. */
			fcport->loop_id = next_loopid;
			rval = qla2x00_find_new_loop_id(ha, fcport);
			if (rval != QL_STATUS_SUCCESS) {
				/* Ran out of IDs to use */
				break;
			}

			/* Login and update database */
			rval2 = qla2x00_fabric_dev_login(ha, fcport,
			    &next_loopid);
			if ((rval2 & BIT_0) && ha->mem_err) {		
				rval = QL_STATUS_RESOURCE_ERROR;
				break;
			}

			/* Remove device from the new list and add it to DB */
			list_del(&fcport->list);
			list_add_tail(&fcport->list, &ha->fcports);
		}
	} while (0);

	pci_free_consistent(ha->pdev, sizeof(sns_cmd_rsp_t), sns, phys_address);

	/* Free all new device structures not processed. */
	list_for_each_entry_safe(fcport, fcptemp, &new_fcports, list) {
		list_del(&fcport->list);
		kfree(fcport);
	}

	if (rval) {
		DEBUG2(printk(KERN_INFO "%s(%ld): error exit: rval=%d\n",
				__func__,
				ha->host_no,
				rval);)
	} else {
		/* EMPTY */
		DEBUG2(printk(KERN_INFO "scsi%ld: %s: exit\n", ha->host_no, __func__);)
	}

	LEAVE(__func__);

	return(rval);
}


/*
 * qla2x00_find_all_fabric_devs
 *	Issue GID_PT (GNN_ID, GPN_ID) to find the list of all fabric devices.
 *	If any one of this fails then go through GAN list to find all
 *	fabric devices.  Will perform necessary logout of previously
 *	existed devices that have changed and save new devices in a new
 *	device list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dev = database device entry pointer.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = error.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_find_all_fabric_devs(scsi_qla_host_t *ha, sns_cmd_rsp_t *sns,
    dma_addr_t phys_addr, struct list_head *new_fcports)
{
	uint8_t		rval = 0;
	uint16_t	i;
	uint16_t	public_count;

	fc_port_t	*fcport, *new_fcport, *tmp_fcport = NULL;
	int		found;
	sw_info_t	*swl;
	int		swl_idx;
	int		first_dev, last_dev;
	port_id_t	wrap;


	ENTER(__func__);

	/* Try GID_PT to get the list of SCSI type devices */
	swl = kmalloc(sizeof(sw_info_t) * MAX_FIBRE_DEVICES, GFP_ATOMIC);
	if (swl == NULL) {
		/*EMPTY*/
		DEBUG2(printk("scsi(%ld): GID_PT allocations failed, fallback "
		    "on GAN\n", ha->host_no));
	} else {
		memset(swl, 0 ,MAX_FIBRE_DEVICES * sizeof (sw_info_t));
		if (qla2x00_gid_pt(ha, sns, phys_addr, swl, GID_NX_PORT) !=
		    QL_STATUS_SUCCESS) {
			kfree(swl);
			swl = NULL;
		} else if (qla2x00_gnn_id(ha, sns, phys_addr, swl) !=
		    QL_STATUS_SUCCESS) {
			kfree(swl);
			swl = NULL;
		} else if (qla2x00_gpn_id(ha, sns, phys_addr, swl) !=
		    QL_STATUS_SUCCESS) {
			kfree(swl);
			swl = NULL;
		}
	}

	/* Allocate temporary fcport for any new fcports discovered. */
	new_fcport = qla2x00_alloc_fcport(ha, GFP_KERNEL);
	if (new_fcport == NULL) {
		if (swl)
			kfree(swl);
		return (QL_STATUS_ERROR);
	}
	new_fcport->flags |= FC_FABRIC_DEVICE;
	new_fcport->flags |= FC_LOGIN_NEEDED;

#if defined(ISP2100)
	ha->max_public_loop_ids = SNS_LAST_LOOP_ID - SNS_FIRST_LOOP_ID + 1;
#else
	ha->max_public_loop_ids = MAX_PUBLIC_LOOP_IDS;
#endif

	/*
	 * Loop getting devices from switch.  Issue GAN to find all devices out
	 * there.  Logout the devices that were in our database but changed
	 * port ID.
	 */
	/* Calculate the max number of public ports */
#if defined(ISP2100)
	public_count = ha->max_public_loop_ids;
#else
	public_count = ha->max_public_loop_ids - ha->min_external_loopid + 2;
#endif

	/* Set start port ID scan at adapter ID. */
	swl_idx = 0;
	first_dev = 1;
	last_dev = 0;

	DEBUG2(printk(KERN_INFO "%s(%ld): dpc_flags=0x%lx\n",
	    __func__, ha->host_no, ha->dpc_flags);)

	for (i = 0; 
	    i < public_count && !atomic_read(&ha->loop_down_timer) &&
	    !(test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)); i++) {

		if (swl != NULL) {
			if (last_dev) {
				wrap.b24 = new_fcport->d_id.b24;
			} else {
				new_fcport->d_id.b24 = swl[swl_idx].d_id.b24;
				memcpy(new_fcport->node_name,
				    swl[swl_idx].node_name, WWN_SIZE);
				memcpy(new_fcport->port_name,
				    swl[swl_idx].port_name, WWN_SIZE);

				if (swl[swl_idx].d_id.b.rsvd_1 != 0) {
					last_dev = 1;
				}

				swl_idx++;
			}
		} else {
			/* Send GAN to the switch */
			rval = 0;
			if (qla2x00_gan(ha, sns, phys_addr, new_fcport)) {
				rval = rval | BIT_0;
				break;
			}
		}

		/* If wrap on switch device list, exit. */
		if (first_dev) {
			wrap.b24 = new_fcport->d_id.b24;
			first_dev = 0;
		} else if (new_fcport->d_id.b24 == wrap.b24){
			DEBUG(printk("%s switch device list wrapped\n"
			    ,__func__);)
			break;
		}

		DEBUG(printk("scsi(%ld): found fabric(%d) - "
				"port Id=%06x\n", 
				ha->host_no, 
				i, 
				new_fcport->d_id.b24);)

		/* Bypass if host adapter. */
		if (new_fcport->d_id.b24 == ha->d_id.b24)
			continue;

		/* Bypass if same domain and area of adapter. */
		if (((new_fcport->d_id.b24 & 0xffff00) ==
			(ha->d_id.b24 & 0xffff00)) && ha->current_topology ==
				ISP_CFG_FL)
			continue;
		/* Bypass reserved domain fields. */
		if ((new_fcport->d_id.b.domain & 0xf0) == 0xf0)
			continue;

		/* Locate matching device in database. */
		found = 0;
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (memcmp(new_fcport->port_name, fcport->port_name,
			    WWN_SIZE))
				continue;

			found++;

			/*
			 * If address the same and state FC_ONLINE, nothing
			 * changed.
			 */
			if (fcport->d_id.b24 == new_fcport->d_id.b24 &&
			    atomic_read(&fcport->state) == FC_ONLINE) {
				break;
			}

			/*
			 * If device was not a fabric device before.
			 */
			if ((fcport->flags & FC_FABRIC_DEVICE) == 0) {
				fcport->d_id.b24 = new_fcport->d_id.b24;
				fcport->loop_id = FC_NO_LOOP_ID;
				fcport->flags |= FC_LOGIN_NEEDED;
				fcport->flags |= FC_FABRIC_DEVICE;
				break;
			}

			/*
			 * Port ID changed or device was marked to be updated;
			 * Log it out if still logged in and mark it for
			 * relogin later.
			 */
			fcport->d_id.b24 = new_fcport->d_id.b24;
			fcport->flags |= FC_LOGIN_NEEDED;
			if (fcport->loop_id != FC_NO_LOOP_ID &&
			    (fcport->flags & FC_TAPE_DEVICE) == 0 &&
			    fcport->port_type != FCT_INITIATOR &&
			    fcport->port_type != FCT_BROADCAST) {
				qla2x00_fabric_logout(ha, fcport->loop_id,
				 fcport->d_id.b.domain, fcport->d_id.b.area,
				 fcport->d_id.b.al_pa);
				fcport->loop_id = FC_NO_LOOP_ID;
			}

			break;
		}

		if (found)
			continue;

		/* If device was not in our fcports list, then add it. */
		list_add_tail(&new_fcport->list, new_fcports);
		tmp_fcport = new_fcport;

		/* Allocate a new replacement fcport. */
		new_fcport = qla2x00_alloc_fcport(ha, GFP_KERNEL);
		if (new_fcport == NULL) {
			if (swl)
				kfree(swl);
			return (QL_STATUS_ERROR);
		}
		new_fcport->flags |= FC_FABRIC_DEVICE;
		new_fcport->flags |= FC_LOGIN_NEEDED;
		new_fcport->d_id.b24 = tmp_fcport->d_id.b24;
	}

	if (swl)
		kfree(swl);

	if (new_fcport)
		kfree(new_fcport);

	if (!list_empty(new_fcports))
		ha->device_flags |= DFLG_FABRIC_DEVICES;

	DEBUG(printk("%s(%ld): exit. rval=%d dpc_flags=0x%lx" 
	    " loop_down_timer=%i\n",
	     __func__,ha->host_no,rval,ha->dpc_flags,
	    atomic_read(&ha->loop_down_timer));)
	DEBUG2(printk(KERN_INFO "%s(%ld): exit. rval=%d dpc_flags=0x%lx" 
	    " loop_down_timer=%i\n",
	     __func__,ha->host_no,rval,ha->dpc_flags,
	    atomic_read(&ha->loop_down_timer)));

	LEAVE(__func__);

	return (rval);
}


#if REG_FDMI_ENABLED
#include "qla_gs.c"
#endif

static inline void *
qla24xx_prep_ms_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
 /**
 * qla24xx_prep_ms_iocb() - Prepare common CT IOCB fields for SNS CT query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
static inline void *
qla24xx_prep_ms_iocb(scsi_qla_host_t *ha, uint32_t req_size, uint32_t rsp_size)
{
	struct ct_entry_24xx *ct_pkt;

	ct_pkt = (struct ct_entry_24xx *)ha->ms_iocb;
	memset(ct_pkt, 0, sizeof(struct ct_entry_24xx));

	ct_pkt->entry_type = CT_IOCB_TYPE;
	ct_pkt->entry_count = 1;
	ct_pkt->nport_handle = __constant_cpu_to_le16(NPH_SNS);
	ct_pkt->timeout = __constant_cpu_to_le16(25);
	ct_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_dsd_count = __constant_cpu_to_le16(1);
	ct_pkt->rsp_byte_count = cpu_to_le32(rsp_size);
	ct_pkt->cmd_byte_count = cpu_to_le32(req_size);

	ct_pkt->dseg_0_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ct_pkt->dseg_0_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));
	ct_pkt->dseg_0_len = ct_pkt->cmd_byte_count;

	ct_pkt->dseg_1_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ct_pkt->dseg_1_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));
	ct_pkt->dseg_1_len = ct_pkt->rsp_byte_count;

	return (ct_pkt);
}


static __inline__ ms_iocb_entry_t *
qla2x00_prep_ms_iocb(scsi_qla_host_t *, uint32_t, uint32_t);
/**
 * qla2x00_prep_ms_iocb() - Prepare common MS IOCB fields for SNS CT query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
static __inline__ ms_iocb_entry_t *
qla2x00_prep_ms_iocb(scsi_qla_host_t *ha, uint32_t req_size, uint32_t rsp_size)
{
	ms_iocb_entry_t *ms_pkt;

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_prep_ms_iocb(ha, req_size, rsp_size);

	ms_pkt = ha->ms_iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
#if defined(EXTENDED_IDS)
	ms_pkt->loop_id = __constant_cpu_to_le16(SIMPLE_NAME_SERVER);
#else
	ms_pkt->loop_id = SIMPLE_NAME_SERVER;
#endif
	ms_pkt->control_flags =
	    __constant_cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = __constant_cpu_to_le16(25);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_size);

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ha->ct_iu_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ha->ct_iu_dma));
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

	return (ms_pkt);
}

static __inline__ void 
	qla2x00_prep_nsrv_ct_cmd_hdr(struct ct_sns_req *, uint16_t , uint16_t );
/**
 * qla2x00_prep_nsrv_ct_cmd_hdr() - Prepare common CT command header fields 
 * 				    for Name Server.
 * @ct_req: CT Req ptr
 * @cmd:Command code 
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
static __inline__ void 
	qla2x00_prep_nsrv_ct_cmd_hdr(struct ct_sns_req *ct_req, uint16_t cmd, 
			uint16_t rsp_size)
{
	memset(ct_req, 0, sizeof(struct ct_sns_pkt));

	ct_req->header.revision = 0x01;
	ct_req->header.gs_type = 0xFC;
	ct_req->header.gs_subtype = 0x02;
	ct_req->command = cpu_to_be16(cmd);
	ct_req->max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

}
#if REG_FC4_ENABLED
/*
 * qla2x00_register_fc4
 *	Register adapter as FC4 device to the switch, so the switch won't
 *	need to login to us later which generates an RSCN event.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_register_fc4(scsi_qla_host_t *ha, 
		sns_cmd_rsp_t *sns, dma_addr_t phys_addr) 
{
	uint8_t rval;
	uint16_t	wc;

	ENTER(__func__);
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		 return qla24xx_register_fc4(ha);
	}

#endif
	
	/* Get port ID for device on SNS. */
	memset(sns, 0, sizeof(sns_cmd_rsp_t));
	wc = RFT_DATA_SIZE / 2;
	sns->p.cmd.buffer_length = cpu_to_le16(wc);
	sns->p.cmd.buffer_address[0] = cpu_to_le32(LSD(phys_addr));
	sns->p.cmd.buffer_address[1] = cpu_to_le32(MSD(phys_addr));
	sns->p.cmd.subcommand_length = __constant_cpu_to_le16(22);
	sns->p.cmd.subcommand = __constant_cpu_to_le16(0x217);
	wc = (RFT_DATA_SIZE - 16) / 4;
	sns->p.cmd.size = cpu_to_le16(wc);
	sns->p.cmd.param[0] = ha->d_id.b.al_pa;
	sns->p.cmd.param[1] = ha->d_id.b.area;
	sns->p.cmd.param[2] = ha->d_id.b.domain;

#if defined(ISP2300)
	if (ha->flags.enable_ip)
		sns->p.cmd.param[4] = 0x20;	/* Set type 5 code for IP */
#endif
	sns->p.cmd.param[5] = 0x01;		/* SCSI - FCP */

	rval = BIT_0;
	if (!qla2x00_send_sns(ha, phys_addr, 30, sizeof(sns_cmd_rsp_t))) {
		if (sns->p.rft_rsp[8] == 0x80 && sns->p.rft_rsp[9] == 0x2) {
			DEBUG2(printk(KERN_INFO "%s(%ld): exiting normally.\n", 
					__func__,
					ha->host_no);)
			rval = 0;
		}
	}

	if (rval != 0) {
		/* EMPTY */
		DEBUG2_3(printk(KERN_INFO "%s(%ld): failed.\n",
				__func__,
				ha->host_no);)
	}

	LEAVE(__func__);

	return (rval);
}


/*
 * qla2x00_register_fc4_feature
 *	Register adapter as FC4 feature to the name server, so the name
 *	server won't need to login to us later which generates an RSCN 
 *	event.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_register_fc4_feature(scsi_qla_host_t *ha, 
		sns_cmd_rsp_t *sns, dma_addr_t phys_addr) 
{
	uint8_t rval;
	uint16_t	wc;

	ENTER(__func__);

#if defined(ISP2300)
 	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		return qla24xx_register_fc4_feature(ha);
        }

#endif

	/* Get port ID for device on SNS. */
	memset(sns, 0, sizeof(sns_cmd_rsp_t));
	wc = RFF_DATA_SIZE / 2;
	sns->p.cmd.buffer_length = cpu_to_le16(wc);
	sns->p.cmd.buffer_address[0] = cpu_to_le32(LSD(phys_addr));
	sns->p.cmd.buffer_address[1] = cpu_to_le32(MSD(phys_addr));
	sns->p.cmd.subcommand_length = __constant_cpu_to_le16(8);
	sns->p.cmd.subcommand = __constant_cpu_to_le16(0x21f);
	wc = (RFF_DATA_SIZE - 16) / 4;
	sns->p.cmd.size = cpu_to_le16(wc);
	sns->p.cmd.param[0] = ha->d_id.b.al_pa;
	sns->p.cmd.param[1] = ha->d_id.b.area;
	sns->p.cmd.param[2] = ha->d_id.b.domain;

	sns->p.cmd.param[5] = BIT_1;		/* fc4 feature */
	sns->p.cmd.param[6] = 0x08;		/* SCSI - FCP */
	if (!ha->flags.enable_target_mode)
		sns->p.cmd.param[7] = 0x02;	/* SCSI Initiator */

	rval = BIT_0;
	if (!qla2x00_send_sns(ha, phys_addr, 16, sizeof(sns_cmd_rsp_t))) {
		if (sns->p.rff_rsp[8] == 0x80 && sns->p.rff_rsp[9] == 0x2) {
			DEBUG2(printk(KERN_INFO "%s(%ld): exiting normally.\n", 
					__func__,
					ha->host_no);)
			rval = 0;
		}
	}

	if (rval != 0) {
		/* EMPTY */
		DEBUG2_3(printk(KERN_INFO "%s(%ld): failed.\n",
				__func__,
				ha->host_no);)
	}

	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_register_nn
 *	Register node name of the HBA with the name server for the
 *	specified port identifier of HBA.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_register_nn(scsi_qla_host_t *ha,
		sns_cmd_rsp_t *sns, dma_addr_t phys_addr)
{
	uint8_t rval;
	uint16_t	wc;

	ENTER(__func__);

#if defined(ISP2300)
 	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		 return qla24xx_register_nn(ha);
        }
#endif

	
	/* Get port ID for device on SNS. */
	memset(sns, 0, sizeof(sns_cmd_rsp_t));
	wc = RNN_DATA_SIZE / 2;
	sns->p.cmd.buffer_length = cpu_to_le16(wc);
	sns->p.cmd.buffer_address[0] = cpu_to_le32(LSD(phys_addr));
	sns->p.cmd.buffer_address[1] = cpu_to_le32(MSD(phys_addr));
	sns->p.cmd.subcommand_length = __constant_cpu_to_le16(10);
	sns->p.cmd.subcommand = __constant_cpu_to_le16(0x213);
	wc = (RNN_DATA_SIZE - 16) / 4;
	sns->p.cmd.size = cpu_to_le16(wc);
	sns->p.cmd.param[0] = ha->d_id.b.al_pa;
	sns->p.cmd.param[1] = ha->d_id.b.area;
	sns->p.cmd.param[2] = ha->d_id.b.domain;

	sns->p.cmd.param[4] = ha->node_name[7];
	sns->p.cmd.param[5] = ha->node_name[6];
	sns->p.cmd.param[6] = ha->node_name[5];
	sns->p.cmd.param[7] = ha->node_name[4];
	sns->p.cmd.param[8] = ha->node_name[3];
	sns->p.cmd.param[9] = ha->node_name[2];
	sns->p.cmd.param[10] = ha->node_name[1];
	sns->p.cmd.param[11] = ha->node_name[0];

	rval = BIT_0;

	if (!qla2x00_send_sns(ha, phys_addr, RNN_CMD_SIZE / 2 , 
				sizeof(sns_cmd_rsp_t))) {
		if (sns->p.rnn_rsp[8] == 0x80 && sns->p.rnn_rsp[9] == 0x2) {
			DEBUG2(printk("%s(%ld): exiting normally.\n", 
					__func__,
					ha->host_no);)
			rval = 0;
		}
	}
	if (rval != 0) {
		/* EMPTY */
		DEBUG2_3(printk("%s(%ld): failed.\n", __func__, ha->host_no);)
	}

	LEAVE(__func__);

	return (rval);

}

/*
 * qla2x00_register_snn
 *	Register symbolic node name of the HBA with the name server for the
 *	specified port identifier of HBA.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_register_snn(scsi_qla_host_t *ha)
{
	int             rval;
	uint8_t         *snn;
	uint8_t         version[20];

	ms_iocb_entry_t *ms_pkt;
	struct ct_sns_pkt	*ct_iu;
	struct ct_sns_req       *ct_req;
	struct ct_sns_rsp       *ct_rsp;
	qla_boards_t    *bdp;

	ENTER(__func__);


	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

	/* Prepare common MS IOCB- Request size adjusted
	 * after CT preparation */

	ms_pkt = qla2x00_prep_ms_iocb(ha, 0, RSNN_NN_RSP_SIZE);

	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;

	/* Initialize Name Server CT-Command header */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req,RSNN_NN_CMD,RSNN_NN_RSP_SIZE);

	/* Prepare CT arguments -- node_name, symbolic node_name, size */
	memcpy(ct_req->req.rsnn_nn.node_name, ha->node_name, WWN_SIZE);
	/* Prepare the Symbolic Node Name */
	/* Board type */
	snn = ct_req->req.rsnn_nn.sym_node_name;
	strcpy(snn, ha->model_number);

	/* Firmware version */
	strcat(snn, " FW:v");
	bdp = &QLBoardTbl_fc[ha->devnum];
#if 1
	sprintf(version, "%d.%02d.%02d", bdp->fwver[0],
			 bdp->fwver[1], bdp->fwver[2] );
#endif
	strcat(snn, version);

	/* Driver version */
	strcat(snn, " DVR:v");
	strcat(snn, qla2x00_version_str);

	/* Calculate SNN length */
	ct_req->req.rsnn_nn.name_len = (uint8_t)strlen(snn);

	/* Update MS IOCB request */
	ms_pkt->req_bytecount =
	    cpu_to_le32(24 + 1 + ct_req->req.rsnn_nn.name_len);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha,
	    ha->ms_iocb, ha->ms_iocb_dma, sizeof(ms_iocb_entry_t));
	if (rval != QL_STATUS_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RSNN_NN issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): RSNN_NN failed, rejected "
		    "request, rsnn_id_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QL_STATUS_ERROR;
	} else {
		DEBUG2(printk("%s(%ld): exiting normally.\n",
		    __func__ ,ha->host_no));
	}
	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

	LEAVE(__func__);

	return (rval);

}

/* 24xx functions */
#if  defined(ISP2300)

static int
qla2x00_chk_ms_status(scsi_qla_host_t *ha, ms_iocb_entry_t *ms_pkt,
    struct ct_sns_rsp *ct_rsp, const char *routine)
{
        int rval;
        uint16_t comp_status;

        rval = QLA2X00_FUNCTION_FAILED;
        if (ms_pkt->entry_status != 0) {
                DEBUG2_3(printk("scsi(%ld): %s failed, error status (%x).\n",
                    ha->host_no, routine, ms_pkt->entry_status));
        } else {
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
                        comp_status =
                            ((struct ct_entry_24xx *)ms_pkt)->comp_status;
                else
                        comp_status = le16_to_cpu(ms_pkt->status);
                switch (comp_status) {
                case CS_COMPLETE:
                case CS_DATA_UNDERRUN:
                case CS_DATA_OVERRUN:           /* Overrun? */
                        if (ct_rsp->header.response !=
                            __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
                                DEBUG2_3(printk("scsi(%ld): %s failed, "
                                    "rejected request:\n", ha->host_no,
                                    routine));
                                DEBUG2_3(qla2x00_dump_buffer(
                                    (uint8_t *)&ct_rsp->header,
                                    sizeof(struct ct_rsp_hdr)));
                        } else
                                rval = QLA2X00_SUCCESS;
                        break;
                default:
                        DEBUG2_3(printk("scsi(%ld): %s failed, completion "
                            "status (%x).\n", ha->host_no, routine,
                            comp_status));
                        break;
                }
        }
        return rval;
}



/*
 * qla24xx_register_fc4
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_register_fc4(scsi_qla_host_t *ha) 
{

	uint8_t             rval;

        ms_iocb_entry_t *ms_pkt;
	struct ct_sns_pkt	*ct_iu;
        struct ct_sns_req       *ct_req;
        struct ct_sns_rsp       *ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

        /* Issue RFT_ID */
        /* Prepare common MS IOCB */
        ms_pkt = qla2x00_prep_ms_iocb(ha, RFT_ID_REQ_SIZE, RFT_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;

	/* Prepare CT request */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, RFT_ID_CMD, RFT_ID_RSP_SIZE);

        /* Prepare CT arguments -- port_id, FC-4 types */
        ct_req->req.rft_id.port_id[0] = ha->d_id.b.domain;
        ct_req->req.rft_id.port_id[1] = ha->d_id.b.area;
        ct_req->req.rft_id.port_id[2] = ha->d_id.b.al_pa;

        ct_req->req.rft_id.fc4_types[2] = 0x01;         /* FCP-3 */
        if (ha->flags.enable_ip)
                ct_req->req.rft_id.fc4_types[3] = 0x20; /* IP */

        /* Execute MS IOCB */
        rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
            sizeof(ms_iocb_entry_t));
        if (rval != QLA2X00_SUCCESS) {
                /*EMPTY*/
                DEBUG2_3(printk("scsi(%ld): RFT_ID issue IOCB failed (%d).\n",
                    ha->host_no, rval));
		rval = 1;
        } else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RFT_ID") !=
            QLA2X00_SUCCESS) {
                rval = 1;
        } else {
                DEBUG2(printk("scsi(%ld): RFT_ID exiting normally.\n",
                    ha->host_no));
		rval = 0;
        }

	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

        return (rval);
}


/*
 * qla24xx_register_fc4_feature
 *	Register adapter as FC4 feature to the name server, so the name
 *	server won't need to login to us later which generates an RSCN 
 *	event.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_register_fc4_feature(scsi_qla_host_t *ha)

{
	uint8_t             rval;

        ms_iocb_entry_t *ms_pkt;
	struct ct_sns_pkt	*ct_iu;
        struct ct_sns_req       *ct_req;
        struct ct_sns_rsp       *ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

        /* Issue RFF_ID */
        /* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, RFF_ID_REQ_SIZE, RFF_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;

	/* Prepare CT request */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, RFF_ID_CMD, RFF_ID_RSP_SIZE);

	/* Prepare CT arguments -- port_id, FC-4 feature, FC-4 type */
        ct_req->req.rff_id.port_id[0] = ha->d_id.b.domain;
        ct_req->req.rff_id.port_id[1] = ha->d_id.b.area;
        ct_req->req.rff_id.port_id[2] = ha->d_id.b.al_pa;

        ct_req->req.rff_id.fc4_feature = BIT_1;
        ct_req->req.rff_id.fc4_type = 0x08;             /* SCSI - FCP */

        /* Execute MS IOCB */
        rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
            sizeof(ms_iocb_entry_t));
        if (rval != QLA2X00_SUCCESS) {
                /*EMPTY*/
                DEBUG2_3(printk("scsi(%ld): RFF_ID issue IOCB failed (%d).\n",
                    ha->host_no, rval));
		rval = 1;
        } else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RFF_ID") !=
            QLA2X00_SUCCESS) {
                rval = 1;
        } else {
                DEBUG2(printk("scsi(%ld): RFF_ID exiting normally.\n",
                    ha->host_no));
		rval = 0;
        }

	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

        return (rval);

}

/*
 * qla24xx_register_nn
 *	Register node name of the HBA with the name server for the
 *	specified port identifier of HBA.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr = DMA buffer address.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_register_nn(scsi_qla_host_t *ha)

{
	uint8_t             rval;

        ms_iocb_entry_t *ms_pkt;
	struct ct_sns_pkt	*ct_iu;
        struct ct_sns_req       *ct_req;
        struct ct_sns_rsp       *ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

        /* Issue RNN_ID */
        /* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, RNN_ID_REQ_SIZE, RNN_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;

	/* Prepare CT request */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, RNN_ID_CMD, RNN_ID_RSP_SIZE);

	 /* Prepare CT arguments -- port_id, node_name */
        ct_req->req.rnn_id.port_id[0] = ha->d_id.b.domain;
        ct_req->req.rnn_id.port_id[1] = ha->d_id.b.area;
        ct_req->req.rnn_id.port_id[2] = ha->d_id.b.al_pa;

        memcpy(ct_req->req.rnn_id.node_name, ha->node_name, WWN_SIZE);

        /* Execute MS IOCB */
        rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
            sizeof(ms_iocb_entry_t));
        if (rval != QLA2X00_SUCCESS) {
                /*EMPTY*/
                DEBUG2_3(printk("scsi(%ld): RNN_ID issue IOCB failed (%d).\n",
                    ha->host_no, rval));
		rval = 1;
        } else if (qla2x00_chk_ms_status(ha, ms_pkt, ct_rsp, "RNN_ID") !=
            QLA2X00_SUCCESS) {
                rval = 1;
        } else {
                DEBUG2(printk("scsi(%ld): RNN_ID exiting normally.\n",
                    ha->host_no));
		rval = 0;
        }

	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

        return (rval);

}

#endif /* QLA2300 */
#endif /* FC4 */

/*
 * qla2x00_gan
 *	Issue Get All Next (GAN) Simple Name Server (SNS) command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	dev = FC device type pointer.
 *
 * Returns:
 *	qla2100 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla2x00_gan(scsi_qla_host_t *ha, sns_cmd_rsp_t *sns, dma_addr_t phys_addr,
    fc_port_t *fcport)
{
	uint8_t		rval;
	uint16_t	wc;

	ENTER(__func__);

#if  defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_ct_gan(ha, fcport);
#endif

	/* Get port ID for device on SNS. */
	memset(sns, 0, sizeof(sns_cmd_rsp_t));
	wc = GAN_DATA_SIZE / 2;
	sns->p.cmd.buffer_length = cpu_to_le16(wc);
	sns->p.cmd.buffer_address[0] = cpu_to_le32(LSD(phys_addr));
	sns->p.cmd.buffer_address[1] = cpu_to_le32(MSD(phys_addr));
	sns->p.cmd.subcommand_length = __constant_cpu_to_le16(6);
	sns->p.cmd.subcommand = __constant_cpu_to_le16(0x100);	/* GA_NXT */
	wc = (GAN_DATA_SIZE - 16) / 4;
	sns->p.cmd.size = cpu_to_le16(wc);
	sns->p.cmd.param[0] = fcport->d_id.b.al_pa;
	sns->p.cmd.param[1] = fcport->d_id.b.area;
	sns->p.cmd.param[2] = fcport->d_id.b.domain;

	rval = BIT_0;
	if (!qla2x00_send_sns(ha, phys_addr, 14, sizeof(sns_cmd_rsp_t))) {
		if (sns->p.gan_rsp[8] == 0x80 && sns->p.gan_rsp[9] == 0x2) {
			fcport->d_id.b.al_pa = sns->p.gan_rsp[19];
			fcport->d_id.b.area = sns->p.gan_rsp[18];
			fcport->d_id.b.domain = sns->p.gan_rsp[17];

			memcpy(fcport->node_name, &sns->p.gan_rsp[284],
			    WWN_SIZE);
			memcpy(fcport->port_name, &sns->p.gan_rsp[20],
			    WWN_SIZE);

			/* If port type not equal to N or NL port, skip it. */
			if (sns->p.gan_rsp[16] != 1 &&
			    sns->p.gan_rsp[16] != 2)
				fcport->d_id.b.domain = 0xf0;

			DEBUG2(printk(KERN_INFO "qla2x00: gan entry - portname "
					"%02x%02x%02x%02x%02x%02x%02x%02x "
					"port Id=%06x\n",
					sns->p.gan_rsp[20], sns->p.gan_rsp[21],
					sns->p.gan_rsp[22], sns->p.gan_rsp[23],
					sns->p.gan_rsp[24], sns->p.gan_rsp[25],
					sns->p.gan_rsp[26], sns->p.gan_rsp[27], 
					fcport->d_id.b24));
			rval = 0;
		}
	}

#if defined(QL_DEBUG_LEVEL_2)
	if (rval != 0)
		printk("%s(): exit, rval = %d\n", __func__, rval);
#endif

	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_gid_pt
 *	Issue GID_PT(Get ID) to get the list of Port Id's from the name server.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr  = Buffer address
 *	new_dev_list  = new device list pointer		
 * 	pt = port type of devices to get
 *
 * Returns:
 *	0 : Success
 *
 * Context:
 *	Kernel context.
 */
static uint8_t 
qla2x00_gid_pt(scsi_qla_host_t  *ha, sns_cmd_rsp_t  *sns, dma_addr_t phys_addr,
    sw_info_t *swl , uint32_t port_type)
{
	uint8_t		rval = BIT_0 ;
	uint16_t	wc;
	uint8_t		retry_count = 0;

	ENTER(__func__);

#if  defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_ct_gid_pt(ha, swl, port_type);
#endif

	/* Retry GID_PT till valid list or retries exhausted - Default value of
	 * retry_gidpt: 10 */
	while (retry_count++ < retry_gidpt) {
		memset(sns, 0, sizeof(sns_cmd_rsp_t));
		wc = GID_DATA_SIZE / 2;	/* Size in 16 bit words*/
		sns->p.cmd.buffer_length = cpu_to_le16(wc);
		sns->p.cmd.buffer_address[0] = cpu_to_le32(LSD(phys_addr));
		sns->p.cmd.buffer_address[1] = cpu_to_le32(MSD(phys_addr));
		sns->p.cmd.subcommand_length = __constant_cpu_to_le16(6);
		sns->p.cmd.subcommand = 
		    __constant_cpu_to_le16(0x1A1);	/* GID_PT */
		wc = (GID_DATA_SIZE - 16) / 4; /* Size in 32 bit words */
		sns->p.cmd.size = cpu_to_le16(wc);
		sns->p.cmd.param[0] =  port_type;

		rval = BIT_0;
		if (!qla2x00_send_sns(ha, phys_addr, GID_CMD_SIZE / 2,
		    sizeof(sns_cmd_rsp_t))) {
			if (sns->p.gid_rsp[8] == 0x80 &&
			    sns->p.gid_rsp[9] == 0x2) {

				uint32_t	i,j;

				/*
				 * Set port IDs and Node Name in new device
				 * list.
				 */
				for (i = 16, j = 0; i < GID_DATA_SIZE;
				    i += 4, j++) {
					swl[j].d_id.b.domain =
					    sns->p.gid_rsp[i + 1];
					swl[j].d_id.b.area = 
					    sns->p.gid_rsp[i + 2];
					swl[j].d_id.b.al_pa = 
					    sns->p.gid_rsp[i + 3];

					/* Last one exit. */
					if (sns->p.gid_rsp[i] & BIT_7) {
						swl[j].d_id.b.rsvd_1 = 
						    sns->p.gid_rsp[i];
						rval = 0;
						break;
					}
				}
				/* Successfully completed,no need to
				 * retry any more */
				break;
			} else{
				DEBUG2(printk(KERN_INFO
				    "%s(%ld): GID_PT retrying retry_count=%d\n",
				    __func__,ha->host_no,retry_count));

				DEBUG5(qla2x00_dump_buffer(
				    (uint8_t *)sns->p.gid_rsp,
				    GID_DATA_SIZE);)
			}
		}

		/* Wait for 1ms before retrying */
		udelay(10000);
	} /* end of while */

#if defined(QL_DEBUG_LEVEL_2)
	if (rval != 0)
		printk("%s(): exit, rval = %d\n", __func__, rval);
#endif

	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_gnn_id
 *	Issue Get Node Name (GNN_ID) .
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr  = Buffer address
 *	new_dev_list  = new device list pointer		
 *
 * Returns:
 *	0 : Success
 *
 * Context:
 *	Kernel context.
 */
static uint8_t 
qla2x00_gnn_id(scsi_qla_host_t *ha, sns_cmd_rsp_t *sns, dma_addr_t phys_addr,
    sw_info_t *swl)
{
	uint8_t		rval = BIT_0 ;
	uint16_t	wc;
	uint16_t	i;

	ENTER(__func__);

#if  defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_ct_gnn_id(ha, swl);
#endif

	for( i = 0; i < MAX_FIBRE_DEVICES; i++ ) {
		uint8_t	retry_gnnid = 2;
		while (retry_gnnid--) {
			/* Get Node Name and Port Id for device on SNS. */
			memset(sns, 0, sizeof(sns_cmd_rsp_t));
			wc = GNN_DATA_SIZE / 2;	/* Size in 16 bit words*/
			sns->p.cmd.buffer_length = cpu_to_le16(wc);
			sns->p.cmd.buffer_address[0] =
			    cpu_to_le32(LSD(phys_addr));
			sns->p.cmd.buffer_address[1] =
			    cpu_to_le32(MSD(phys_addr));
			sns->p.cmd.subcommand_length 
					= __constant_cpu_to_le16(6);
			sns->p.cmd.subcommand 
					= __constant_cpu_to_le16(0x113);
								/* GNN_ID */
			wc = (GNN_DATA_SIZE - 16) / 4; 
						 /* Size in 32 bit words */
			sns->p.cmd.size = cpu_to_le16(wc);
			sns->p.cmd.param[0] = swl[i].d_id.b.al_pa;
			sns->p.cmd.param[1] = swl[i].d_id.b.area;
			sns->p.cmd.param[2] = swl[i].d_id.b.domain;

			rval = BIT_0;
			if (!qla2x00_send_sns(ha, phys_addr, GNN_CMD_SIZE / 2,
			       	sizeof(sns_cmd_rsp_t))) {
				if (sns->p.gnn_rsp[8] == 0x80 &&
				    sns->p.gnn_rsp[9] == 0x2) {
					/* Extract Nortname */
					memcpy(swl[i].node_name,
					    &sns->p.gnn_rsp[16], WWN_SIZE);

					rval = 0;
					break;
				}
			} else {
				DEBUG2(printk(KERN_INFO
				    "%s(%ld): GNN_ID retrying retry_count=%d\n",
				    __func__,ha->host_no,retry_gnnid);)
			}
		} /* end of while */

		/* Last one exit. */
		if (swl[i].d_id.b.rsvd_1 != 0) {
			break;
		}
	} /* end of for */

#if defined(QL_DEBUG_LEVEL_2)
	if (rval != 0)
		printk("%s(): exit, rval = %d\n", __func__, rval);
#endif

	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_gpn_id
 *	Issue Get Port Name (GPN_ID) .
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr  = Buffer address
 *	new_dev_list  = new device list pointer		
 *
 * Returns:
 *	0 : Success
 *
 * Context:
 *	Kernel context.
 */
static uint8_t 
qla2x00_gpn_id(scsi_qla_host_t *ha, sns_cmd_rsp_t *sns, dma_addr_t phys_addr,
    sw_info_t *swl)
{
	uint8_t		rval = BIT_0 ;
	uint16_t	wc;
	uint16_t	i;

	ENTER(__func__);

#if  defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_ct_gpn_id(ha, swl);
#endif

	for( i = 0; i < MAX_FIBRE_DEVICES; i++ ) {
		uint8_t	retry_gpnid = 2;
		while( retry_gpnid-- ) {
			/* Get Port Name and Port Id for device on SNS. */
			memset(sns, 0, sizeof(sns_cmd_rsp_t));
			wc = GPN_DATA_SIZE / 2;	/* Size in 16 bit words*/
			sns->p.cmd.buffer_length = cpu_to_le16(wc);
			sns->p.cmd.buffer_address[0] =
			    cpu_to_le32(LSD(phys_addr));
			sns->p.cmd.buffer_address[1] =
			    cpu_to_le32(MSD(phys_addr));
			sns->p.cmd.subcommand_length 
					= __constant_cpu_to_le16(6);
			sns->p.cmd.subcommand 
					= __constant_cpu_to_le16(0x112);
								/* GPN_ID */
			wc = (GPN_DATA_SIZE - 16) / 4; 
						 /* Size in 32 bit words */
			sns->p.cmd.size = cpu_to_le16(wc);
			sns->p.cmd.param[0] = swl[i].d_id.b.al_pa;
			sns->p.cmd.param[1] = swl[i].d_id.b.area;
			sns->p.cmd.param[2] = swl[i].d_id.b.domain;

			rval = BIT_0;
			if (!qla2x00_send_sns(ha, phys_addr, GPN_CMD_SIZE / 2,
			       	sizeof(sns_cmd_rsp_t))) {
				if (sns->p.gpn_rsp[8] == 0x80 &&
				    sns->p.gpn_rsp[9] == 0x2) {
					/* Extract Portname */
					memcpy(swl[i].port_name,
					    &sns->p.gpn_rsp[16], WWN_SIZE);

					rval = 0;
					break;

				}
			} else {
				DEBUG2(printk(KERN_INFO
				    "%s(%ld): GPN_ID retrying retry_count=%d\n",
				    __func__,ha->host_no,retry_gpnid);)
			}
		} /* end of while */

		/* Last one exit. */
		if (swl[i].d_id.b.rsvd_1 != 0) {
			break;
		}
	} /* end of for */

#if defined(QL_DEBUG_LEVEL_2)
	if (rval != 0)
		printk("%s(): exit, rval = %d\n", __func__, rval);
#endif

	LEAVE(__func__);

	return (rval);
}

#if  defined(ISP2300)
/*
 * qla24xx_ct_gan
 *	Issue Get All Next (GAN) Simple Name Server (SNS) command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	dev = FC device type pointer.
 *
 * Returns:
 *	qla2100 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_ct_gan(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_pkt	*ct_iu;
	struct ct_sns_req       *ct_req;
	struct ct_sns_rsp       *ct_rsp;

	ENTER(__func__);

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));
 
	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

	/* Prepare common MS IOCB- Request size adjusted
	 * after CT preparation */

	ms_pkt = qla2x00_prep_ms_iocb(ha, GAN_CMD_SIZE, GAN_DATA_SIZE);

	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;


	/* Issue GA_NXT */
	/* Prepare common MS IOCB */

	/* Prepare CT request */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, GAN_CMD, GAN_DATA_SIZE);

	/* Prepare CT arguments -- port_id */
	ct_req->req.port_id.port_id[0] = fcport->d_id.b.domain;
	ct_req->req.port_id.port_id[1] = fcport->d_id.b.area;
	ct_req->req.port_id.port_id[2] = fcport->d_id.b.al_pa;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GA_NXT issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): GA_NXT failed, rejected request, "
		    "ga_nxt_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA2X00_FUNCTION_FAILED;
	} else {
		/* Populate fc_port_t entry. */
		fcport->d_id.b.domain = ct_rsp->rsp.ga_nxt.port_id[0];
		fcport->d_id.b.area = ct_rsp->rsp.ga_nxt.port_id[1];
		fcport->d_id.b.al_pa = ct_rsp->rsp.ga_nxt.port_id[2];

		memcpy(fcport->node_name, ct_rsp->rsp.ga_nxt.node_name,
		    WWN_SIZE);
		memcpy(fcport->port_name, ct_rsp->rsp.ga_nxt.port_name,
		    WWN_SIZE);

		if (ct_rsp->rsp.ga_nxt.port_type != 0x1 &&
		    ct_rsp->rsp.ga_nxt.port_type != 0x2)
			fcport->d_id.b.domain = 0xf0;

		DEBUG2_3(printk("scsi(%ld): GA_NXT entry - "
		    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "portid=%02x%02x%02x.\n",
		    ha->host_no,
		    fcport->node_name[0], fcport->node_name[1],
		    fcport->node_name[2], fcport->node_name[3],
		    fcport->node_name[4], fcport->node_name[5],
		    fcport->node_name[6], fcport->node_name[7],
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7],
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa));
	}
	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

	return (rval);
}

static uint8_t
qla24xx_ct_gid_pt(scsi_qla_host_t *ha, sw_info_t *swl, uint32_t port_type)
{
	int		rval;
	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_pkt	*ct_iu;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));

	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

	/* Issue GID_PT */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, GID_CMD_SIZE,
		    GID_DATA_SIZE);
	/* Prepare CT request */
	ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
	ct_req = &ct_iu->p.req;
	ct_rsp = &ct_iu->p.rsp;

	/* Prepare CT request */
	qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, GID_CMD, GID_DATA_SIZE);
	ct_req->req.gid_pt.port_type = port_type;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA2X00_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GID_PT issue IOCB failed "
		    "(%d).\n", ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): GID_PT failed, rejected "
		    "request, gid_pt_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA2X00_FUNCTION_FAILED;
	} else {
		uint32_t	i;

		/*
		 * Set port IDs in new device list.
		 */
		for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
			swl[i].d_id.b.domain =
			    ct_rsp->rsp.gid_pt.entries[i].port_id[0];
			swl[i].d_id.b.area =
			    ct_rsp->rsp.gid_pt.entries[i].port_id[1];
			swl[i].d_id.b.al_pa =
			    ct_rsp->rsp.gid_pt.entries[i].port_id[2];

			DEBUG2(printk(KERN_INFO
			    "qla2x00: gid_pt entry - port Id=%06x\n",
			    swl[i].d_id.b24));

			/* Last one exit. */
			if (ct_rsp->rsp.gid_pt.entries[i].control_byte & BIT_7) {
				swl[i].d_id.b.rsvd_1 =
				   ct_rsp->rsp.gid_pt.entries[i].control_byte;
				break;
			}
		}
	}

	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

	return (rval);
}

/*
 * qla24xx_ct_gnn_id
 *	Issue Get Node Name (GNN_ID) .
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr  = Buffer address
 *	new_dev_list  = new device list pointer	
 *
 * Returns:
 *	0 : Success
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_ct_gnn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_pkt	*ct_iu;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));

	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GNN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla2x00_prep_ms_iocb(ha, GNN_CMD_SIZE,
			    GNN_DATA_SIZE);
		/* Prepare CT request */
		ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
		ct_req = &ct_iu->p.req;
		ct_rsp = &ct_iu->p.rsp;

		/* Prepare CT request */
		qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, GNN_CMD, GNN_DATA_SIZE);

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA2X00_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GNN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (ct_rsp->header.response !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_3(printk("scsi(%ld): GNN_ID failed, rejected "
			    "request, gnn_id_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
			    sizeof(struct ct_rsp_hdr)));
			rval = QLA2X00_FUNCTION_FAILED;
		} else {
			/* Save portname */
			memcpy(list[i].node_name,
			    ct_rsp->rsp.gnn_id.node_name, WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}
	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

	return (rval);

}

/*
 * qla24xx_ct_gpn_id
 *	Issue Get Port Name (GPN_ID) .
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer to buffer for sns command.
 *	phys_addr  = Buffer address
 *	new_dev_list  = new device list pointer	
 *
 * Returns:
 *	0 : Success
 *
 * Context:
 *	Kernel context.
 */
static uint8_t
qla24xx_ct_gpn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_pkt	*ct_iu;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Get consistent memory allocated for MS IOCB */
	if (ha->ms_iocb == NULL){
		ha->ms_iocb = pci_alloc_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), &ha->ms_iocb_dma);
	}

	if (ha->ms_iocb == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ms_iocb\n",
		    ha->host_no);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}
	memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));

	/* Get consistent memory allocated for CT SNS command */
	if (ha->ct_iu == NULL) {
		ha->ct_iu = pci_alloc_consistent(ha->pdev,
		    sizeof(struct ct_sns_pkt), &ha->ct_iu_dma);
	}

	if( ha->ct_iu == NULL){
		 /* error */
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - ct_sns\n",
		    ha->host_no);
		pci_free_consistent(ha->pdev,
		    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);
		rval = QL_STATUS_ERROR;
		return (rval) ;
	}

        memset(ha->ct_iu, 0, sizeof(struct ct_sns_pkt));

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla2x00_prep_ms_iocb(ha, GPN_CMD_SIZE,
			    GPN_DATA_SIZE);
		/* Prepare CT request */
		ct_iu = (struct ct_sns_pkt *)ha->ct_iu;
		ct_req = &ct_iu->p.req;
		ct_rsp = &ct_iu->p.rsp;

		/* Prepare CT request */
		qla2x00_prep_nsrv_ct_cmd_hdr(ct_req, GPN_CMD, GPN_DATA_SIZE);

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA2X00_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GPN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (ct_rsp->header.response !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_3(printk("scsi(%ld): GPN_ID failed, rejected "
			    "request, gpn_id_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
			    sizeof(struct ct_rsp_hdr)));
			rval = QLA2X00_FUNCTION_FAILED;
		} else {
			/* Save portname */
			memcpy(list[i].port_name,
			    ct_rsp->rsp.gpn_id.port_name, WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}
	pci_free_consistent(ha->pdev,
	    sizeof(struct ct_sns_pkt), ha->ct_iu, ha->ct_iu_dma);
	pci_free_consistent(ha->pdev,
	    sizeof(ms_iocb_entry_t), ha->ms_iocb, ha->ms_iocb_dma);

	ha->ct_iu = NULL;
        ha->ms_iocb = NULL;

	return (rval);

}
#endif

/*
 * qla2x00_local_device_login
 *	Issue local device login command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop id of device to login to.
 *
 * Returns (Where's the #define!!!!):
 *      0 - Login successfully
 *      1 - Login failed
 *      3 - Fatal error
 */
static uint8_t
qla2x00_local_device_login(scsi_qla_host_t *ha, fc_port_t  *fcport)
{
	int		rval;
	uint16_t	mb[MAILBOX_REGISTER_COUNT];

	memset(mb, 0, sizeof(mb));
	rval = qla2x00_login_local_device(ha, fcport, mb, BIT_0);
	if (rval == QL_STATUS_SUCCESS) {
		/* Interrogate mailbox registers for any errors */
		if (mb[0] == 0x4005)
			rval = 1;
		else if (mb[0] == 0x4006)
			/* device not in PCB table */
			rval = 3;
	}
	return rval;
}

/*
 * qla2x00_configure_loop
 *      Updates Fibre Channel Device Database with what is actually on loop.
 *
 * Input:
 *      ha                = adapter block pointer.
 *
 * Output:
 *
 * Returns:
 *      0 = success.
 *      1 = error.
 *      2 = database was full and device was not configured.
 */
STATIC uint8_t
qla2x00_configure_loop(scsi_qla_host_t *ha) 
{
	uint8_t  rval = 0;
	uint8_t  rval1 = 0;
	unsigned long  flags, save_flags;
	unsigned long sflags;

	DEBUG3(printk("%s(%ld): entered\n", __func__, ha->host_no);)

	/* Get Initiator ID */
	if (qla2x00_configure_hba(ha)) {
		DEBUG(printk("scsi%ld: qla2x00_configure_loop: "
				"configure hba failed.\n",
				ha->host_no);)
		return(1);
	}

	DEBUG(printk("%s(): dpc flags =0x%lx\n", __func__, ha->dpc_flags);)

	spin_lock_irqsave(&ha->hardware_lock, sflags);
	save_flags = flags = ha->dpc_flags;
	/*
	 * If we have both an RSCN and PORT UPDATE pending then handle them
	 * both at the same time.
	 */
	clear_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
	clear_bit(RSCN_UPDATE, &ha->dpc_flags);
	spin_unlock_irqrestore(&ha->hardware_lock, sflags);

	ha->mem_err = 0 ;

	/* Determine what we need to do */
	if (ha->current_topology == ISP_CFG_FL &&
		(test_bit(LOCAL_LOOP_UPDATE, &flags))) {

		ha->flags.rscn_queue_overflow = TRUE;
		set_bit(RSCN_UPDATE, &flags);

	} else if (ha->current_topology == ISP_CFG_F &&
		(test_bit(LOCAL_LOOP_UPDATE, &flags))) {

		ha->flags.rscn_queue_overflow = TRUE;
		set_bit(RSCN_UPDATE, &flags);
		clear_bit(LOCAL_LOOP_UPDATE, &flags);

	} else if (!ha->flags.online ||
		(test_bit(ABORT_ISP_ACTIVE, &flags))) {

		ha->flags.rscn_queue_overflow = TRUE;
		set_bit(RSCN_UPDATE, &flags);
		set_bit(LOCAL_LOOP_UPDATE, &flags);
	}

	do {
		if (test_bit(LOCAL_LOOP_UPDATE, &flags)) {
			rval = qla2x00_configure_local_loop(ha);
		}
		if (rval == QLA2X00_SUCCESS) {
			 /*
		          * No point in continuing if the loop is in a volatile state --
			  * reschedule LOCAL_LOOP_UPDATE for later processing
			  */
			if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
			    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
			    atomic_read(&ha->loop_state) == LOOP_DOWN) {
				rval = 1;
				break;
			}
		}

		if (test_bit(RSCN_UPDATE, &flags)) {
			rval1 = qla2x00_configure_fabric(ha);
			if ((rval1 & BIT_0) && ha->sns_retry_cnt < 8) {
				ha->sns_retry_cnt++;
				set_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags);
			}
		}

		/* Isolate error status. */
		if (rval & BIT_0) {
			rval = 1;
		} else {
			rval = QLA2X00_SUCCESS;
		}

	} while (rval != QLA2X00_SUCCESS);

	if (!atomic_read(&ha->loop_down_timer) && 
		!(test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))) {

		if (!ha->flags.failover_enabled){
			qla2x00_config_os(ha);
		}

		/* If we found all devices then go ready */
		if (!(test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags))) {
			atomic_set(&ha->loop_state, LOOP_READY);
			if (ha->flags.failover_enabled) {
				DEBUG(printk("%s(%ld): schedule "
						"FAILBACK EVENT\n", 
						__func__,
						ha->host_no);)
				if (!(test_and_set_bit(FAILOVER_EVENT_NEEDED,
							&ha->dpc_flags))) {
					ha->failback_delay = failbackTime;
				}
				set_bit(COMMAND_WAIT_NEEDED, &ha->dpc_flags);
				ha->failover_type = MP_NOTIFY_LOOP_UP;
			}

			DEBUG2(printk("%s(%ld): LOOP READY\n", 
						__func__,
						ha->host_no);)

		} else {
			if (test_bit(LOCAL_LOOP_UPDATE, &save_flags))
				set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
			if (test_bit(RSCN_UPDATE, &save_flags))
				set_bit(RSCN_UPDATE, &ha->dpc_flags);
		}
	
	} else {
		DEBUG(printk("%s(%ld): Loop down counter running= %d or "
				"Resync needed- dpc flags= %ld\n",
				__func__,
				ha->host_no,
				atomic_read(&ha->loop_down_timer), 
				ha->dpc_flags);)
			/* ???? dg 02/26/02  rval = 1; */
	}

	if (rval) {
		DEBUG2_3(printk(KERN_INFO "%s(%ld): *** FAILED ***\n",
				__func__,
				ha->host_no);)
	} else {
		DEBUG2(printk("%s: exiting normally\n", __func__);)
	}

	/* Restore state if a resync event occured during processing */
	if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)) {
		if (test_bit(LOCAL_LOOP_UPDATE, &save_flags))
			set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
		if (test_bit(RSCN_UPDATE, &save_flags))
			set_bit(RSCN_UPDATE, &ha->dpc_flags);
	}
	DEBUG(printk("%s(%ld) rval=%d exit\n", __func__, ha->host_no, rval);)

	return(rval);
}
/*
 * qla2x00_config_os
 *	Setup OS target and LUN structures.
 *
 * Input:
 *	ha = adapter state pointer.
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_config_os(scsi_qla_host_t *ha) 
{
	fc_port_t	*fcport;
	fc_lun_t	*fclun;

	DEBUG3(printk("%s(%ld): entered.\n", __func__, ha->host_no);)

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (atomic_read(&fcport->state) != FC_ONLINE ||
		    fcport->port_type == FCT_INITIATOR ||
		    fcport->port_type == FCT_BROADCAST) {
			fcport->dev_id = MAX_TARGETS;
			continue;
		}

		/* Bind FC port to OS target number. */
		if (qla2x00_fcport_bind(ha, fcport) == MAX_TARGETS) {
			continue;
		}

		/* Bind FC LUN to OS LUN number. */
		list_for_each_entry(fclun, &fcport->fcluns, list) {
			qla2x00_fclun_bind(ha, fcport, fclun);
		}
	}

	DEBUG3(printk("%s(%ld): exiting normally.\n", __func__, ha->host_no);)
}

/*
 * qla2x00_fcport_bind
 *	Locates a target number for FC port.
 *
 * Input:
 *	ha = adapter state pointer.
 *	fcport = FC port structure pointer.
 *
 * Returns:
 *	target number
 *
 * Context:
 *	Kernel context.
 */
static uint16_t
qla2x00_fcport_bind(scsi_qla_host_t *ha, fc_port_t *fcport) 
{
	uint16_t	tgt;
	os_tgt_t	*tq, *otq;
	uint8_t		rval;

	/* Check for persistent binding. */
	for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
		if ((tq = TGT_Q(ha, tgt)) == NULL)
			continue;
		rval = 1;
		switch (ha->binding_type) {
			case BIND_BY_PORT_ID:
				if(fcport->d_id.b24 == tq->d_id.b24){
					memcpy(tq->node_name, fcport->node_name,
					    WWN_SIZE);
					memcpy(tq->port_name, fcport->port_name,
					    WWN_SIZE);
					rval = 0;
			        }
			    break;

			case BIND_BY_PORT_NAME:    
				if (memcmp(fcport->port_name, tq->port_name,
					    WWN_SIZE) == 0) {
				/* In case of persistent binding, update 
				 * the WWNN */
					memcpy(tq->node_name, fcport->node_name,
					    WWN_SIZE);
					rval = 0;
				}
				break;
		}
		if(rval == 0)
		    break;	
	}

	if ( ConfigRequired == 0 && tgt == MAX_TARGETS) {
		/* Check if targetID 0 available. */
		tgt = 0;

		if (TGT_Q(ha, tgt) != NULL) {
			/* Locate first free target for device. */
			for (tgt = 0; tgt < MAX_TARGETS; tgt++) {
				if (TGT_Q(ha, tgt) == NULL) {
					break;
				}
			}
		}
		if (tgt != MAX_TARGETS) {
			if ((tq = qla2x00_tgt_alloc(ha, tgt)) != NULL) {
				memcpy(tq->node_name, fcport->node_name,
				    WWN_SIZE);
				memcpy(tq->port_name, fcport->port_name,
				    WWN_SIZE);
				tq->d_id.b24 = fcport->d_id.b24;
			}
		}
	}

	/* Clean any old bindings. */
	if (ha->binding_type == BIND_BY_PORT_ID &&
	    fcport->dev_id != MAX_TARGETS) {
		otq = TGT_Q(ha, fcport->dev_id);
		if (otq && otq->vis_port) {
			otq->vis_port = NULL;
			memset(otq->node_name, 0, WWN_SIZE);
			memset(otq->port_name, 0, WWN_SIZE);
		}
	}

	/* Reset target numbers incase it changed. */
	fcport->dev_id = tgt;
	if (tgt != MAX_TARGETS && tq != NULL) {
		DEBUG2(printk("scsi(%ld): Assigning target ID=%02d @ %p to "
		    "loop id=0x%04x, port state=0x%x, port down retry=%d\n",
		    ha->host_no, tgt, tq, fcport->loop_id,
		    atomic_read(&fcport->state),
		    atomic_read(&fcport->port_down_timer)));

		tq->vis_port = fcport;
		tq->port_down_retry_count = ha->port_down_retry_count;

		if (!ha->flags.failover_enabled)
			qla2x00_get_lun_mask_from_config(ha, fcport, tgt, 0);
	}

	if ( ConfigRequired == 0 && tgt == MAX_TARGETS) {
		printk(KERN_WARNING
		    "scsi(%ld): Unable to bind fcport, loop_id=%x\n",
		    ha->host_no, fcport->loop_id);
	}

	return (tgt);
}

/*
 * qla2x00_fclun_bind
 *	Binds all FC device LUNS to OS LUNS.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *
 * Returns:
 *	target number
 *
 * Context:
 *	Kernel context.
 */
static os_lun_t *
qla2x00_fclun_bind(scsi_qla_host_t *ha, fc_port_t *fcport, fc_lun_t *fclun)
{
	os_lun_t	*lq;
	uint16_t	tgt;
	uint16_t	lun;

	tgt = fcport->dev_id;
	lun = fclun->lun;

	/* Allocate LUNs */
	if (lun >= MAX_LUNS) {
		DEBUG2(printk("scsi(%ld): Unable to bind lun, invalid "
		    "lun=(%x).\n", ha->host_no, lun));
		return (NULL);
	}

	/* Always alloc LUN 0 so kernel will scan past LUN 0. */
	if (lun != 0 && (EXT_IS_LUN_BIT_SET(&(fcport->lun_mask), lun))) {
		return (NULL);
	}

	if ((lq = qla2x00_lun_alloc(ha, tgt, lun)) == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Unable to bind fclun, loop_id=%x lun=%x\n",
		    ha->host_no, fcport->loop_id, lun);
		return (NULL);
	}

	lq->fclun = fclun;

	return (lq);
}

/*
 * qla2x00_mark_device_lost
 *	Updates fcport state when device goes offline.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
STATIC void
qla2x00_mark_device_lost(scsi_qla_host_t *ha, fc_port_t *fcport, int do_login) 
{
	/* 
	 * We may need to retry the login, so don't change the
	 * state of the port but do the retries.
	 */
	if (atomic_read(&fcport->state) != FC_DEVICE_DEAD)
		atomic_set(&fcport->state, FC_DEVICE_LOST);
 
	if (!do_login)
		return;

#if defined(PORT_LOGIN_4xWAY)
	if (PORT_LOGIN_RETRY(fcport) > 0) {
		PORT_LOGIN_RETRY(fcport)--;
		DEBUG(printk("scsi%ld: Port login retry: "
				"%02x%02x%02x%02x%02x%02x%02x%02x, "
				"id = 0x%04x retry cnt=%d\n",
				ha->host_no,
				fcport->port_name[0],
				fcport->port_name[1],
				fcport->port_name[2],
				fcport->port_name[3],
				fcport->port_name[4],
				fcport->port_name[5],
				fcport->port_name[6],
				fcport->port_name[7],
				fcport->loop_id,
				PORT_LOGIN_RETRY(fcport));)
			
		set_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags);
	}
#else
	if (fcport->login_retry == 0) {
		fcport->login_retry = ha->login_retry_count;

		DEBUG2(printk("scsi%ld: Port login retry: "
				"%02x%02x%02x%02x%02x%02x%02x%02x, "
				"id = 0x%04x retry cnt=%d\n",
				ha->host_no,
				fcport->port_name[0],
				fcport->port_name[1],
				fcport->port_name[2],
				fcport->port_name[3],
				fcport->port_name[4],
				fcport->port_name[5],
				fcport->port_name[6],
				fcport->port_name[7],
				fcport->loop_id,
				fcport->login_retry ); )
		set_bit(RELOGIN_NEEDED, &ha->dpc_flags);
	}
#endif
}

/*
 * qla2x00_mark_all_devices_lost
 *	Updates fcport state when device goes offline.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
STATIC void
qla2x00_mark_all_devices_lost(scsi_qla_host_t *ha) 
{
	struct list_head	*fcpl;
	fc_port_t		*fcport;

	list_for_each(fcpl, &ha->fcports) {
		fcport = list_entry(fcpl, fc_port_t, list);
		if(fcport->port_type != FCT_TARGET)
			continue;

		/*
		 * No point in marking the device as lost, if the device is
		 * already DEAD.
		 */
		if (atomic_read(&fcport->state) == FC_DEVICE_DEAD)
			continue;

		atomic_set(&fcport->state, FC_DEVICE_LOST);
	}
}

/*
 * qla2x00_check_for_devices_online
 *
 *	Check fcport state of all devices to make sure online.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
STATIC uint8_t
qla2x00_check_for_devices_online(scsi_qla_host_t *ha) 
{
	fc_port_t	*fcport;
	int		found, cnt;

	found = 0;
	cnt = 0;
 	list_for_each_entry(fcport, &ha->fcports, list) {
		if(fcport->port_type != FCT_TARGET)
			continue;

		if ((atomic_read(&fcport->state) == FC_ONLINE) ||
		     (fcport->flags & FC_FAILBACK_DISABLE) ||
			(atomic_read(&fcport->state) == FC_DEVICE_DEAD))
			found++;

		cnt++;
	}
	if (cnt == found) {
		DEBUG5(printk("%s(%ld): all online\n",
				__func__,
				ha->host_no);)
		return 1;
	} else
		return 0;
}

STATIC void
qla2x00_probe_for_all_luns(scsi_qla_host_t *ha) 
{
	fc_port_t	*fcport;

	qla2x00_mark_all_devices_lost(ha); 
 	list_for_each_entry(fcport, &ha->fcports, list) {
		if(fcport->port_type != FCT_TARGET)
			continue;

		qla2x00_update_fcport(ha, fcport); 
	}
}

/*
 * qla2x00_update_fcport
 *	Updates device on list.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_update_fcport(scsi_qla_host_t *ha, fc_port_t *fcport) 
{
	int		rval;
	unsigned long	index, flags;
	srb_t		*sp;

	DEBUG4(printk("%s(): entered, loop_id = %d\n",
		__func__, fcport->loop_id);)

	fcport->port_login_retry_count = ha->port_down_retry_count *
	    PORT_RETRY_TIME;
	fcport->flags &= ~FC_LOGIN_NEEDED;
	atomic_set(&fcport->state, FC_ONLINE);
	fcport->login_retry = 0;
	fcport->ha = ha;
	atomic_set(&fcport->port_down_timer, ha->port_down_retry_count *
	    PORT_RETRY_TIME);

	if (fcport->port_type != FCT_TARGET)
		return (QLA2X00_SUCCESS);

	/*
	 * Check for outstanding cmd on tape. Bypass LUN discovery if active
	 * command on tape.
	 */
	if (fcport->flags & FC_TAPE_DEVICE) {
		for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
			spin_lock_irqsave(&ha->hardware_lock, flags);
			if ((sp = ha->outstanding_cmds[index]) != 0) {
				if (sp->fclun->fcport == fcport) {
					spin_unlock_irqrestore(
					    &ha->hardware_lock, flags);
					atomic_set(&fcport->state, FC_ONLINE);
					return (QLA2X00_SUCCESS);
				}
			}
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}

	/* Do LUN discovery. */
	rval = qla2x00_lun_discovery(ha, fcport);
	if ((fcport->flags & (FC_MSA_DEVICE | FC_EVA_DEVICE |FC_AA_EVA_DEVICE |
		FC_DSXXX_DEVICE| FC_DFXXX_DEVICE | FC_AA_MSA_DEVICE)))
		qla2x00_test_active_port(fcport); 

	return (rval);
}

int
qla2x00_issue_scsi_inquiry(scsi_qla_host_t *ha, 
	fc_port_t *fcport, fc_lun_t *fclun )
{
	inq_cmd_rsp_t	*pkt;
	int		rval;
	dma_addr_t	phys_address = 0;
	int		retry;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	int		ret = 0;
	uint16_t	lun = fclun->lun;
        uint16_t        *cstatus, *sstatus;


	pkt = pci_alloc_consistent(ha->pdev,
				sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
			"scsi(%ld): Memory Allocation failed - INQ\n",
			ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                cstatus = &pkt->p.rsp24.comp_status;
                sstatus = &pkt->p.rsp24.scsi_status;
        } else {
                cstatus = &pkt->p.rsp.comp_status;
                sstatus = &pkt->p.rsp.scsi_status;
        }

	DEBUG2(printk("%s entered\n",__func__);)
	retry = 2;
	do {
		memset(pkt, 0, sizeof(inq_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                        pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
                        pkt->p.cmd24.entry_count = 1;
                        pkt->p.cmd24.nport_handle = fcport->loop_id;
                        pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
                        pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
                        pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
                        pkt->p.cmd24.lun[1] = LSB(lun);
                        pkt->p.cmd24.lun[2] = MSB(lun);
                        host_to_fcp_swap(pkt->p.cmd24.lun,
                            sizeof(pkt->p.cmd24.lun));
                        pkt->p.cmd24.task_mgmt_flags =
                            __constant_cpu_to_le16(TMF_READ_DATA);
                        pkt->p.cmd24.task = TSK_SIMPLE;
                        pkt->p.cmd24.fcp_cdb[0] = INQUIRY;
                        pkt->p.cmd24.fcp_cdb[4] = INQ_DATA_SIZE;
                        host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
                            sizeof(pkt->p.cmd24.fcp_cdb));
                        pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(1);
                        pkt->p.cmd24.timeout = __constant_cpu_to_le16(3);
                        pkt->p.cmd24.byte_count =
                            __constant_cpu_to_le32(INQ_DATA_SIZE);
                        pkt->p.cmd24.dseg_0_address[0] = cpu_to_le32(
                            LSD(phys_address + sizeof(struct sts_entry_24xx)));
                        pkt->p.cmd24.dseg_0_address[1] = cpu_to_le32(
                            MSD(phys_address + sizeof(struct sts_entry_24xx)));
                        pkt->p.cmd24.dseg_0_len =
                            __constant_cpu_to_le32(INQ_DATA_SIZE);
                } else {

			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = cpu_to_le16(lun);
	#if defined(EXTENDED_IDS)
			pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
	#else
			pkt->p.cmd.target = (uint8_t)fcport->loop_id;
	#endif
			pkt->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_READ | CF_SIMPLE_TAG);
			pkt->p.cmd.scsi_cdb[0] = INQ_SCSI_OPCODE;
			pkt->p.cmd.scsi_cdb[4] = INQ_DATA_SIZE;
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(1);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(3);
			pkt->p.cmd.byte_count =
				__constant_cpu_to_le32(INQ_DATA_SIZE);
			pkt->p.cmd.dseg_0_address[0] = cpu_to_le32(
			      LSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_address[1] = cpu_to_le32(
			      MSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_length =
				__constant_cpu_to_le32(INQ_DATA_SIZE);
		}
		DEBUG2(printk("scsi(%ld:0x%x:%d) %s: Inquiry - fcport=%p,"
			" lun (%d)\n", 
			ha->host_no, fcport->loop_id, lun,
			__func__,fcport, 
			lun);)

		rval = qla2x00_issue_iocb(ha, pkt,
				phys_address, sizeof(inq_cmd_rsp_t));

		comp_status = le16_to_cpu(*cstatus);
		scsi_status = le16_to_cpu(*sstatus);

	} while ((rval != QLA2X00_SUCCESS ||
		comp_status != CS_COMPLETE) && 
		retry--);

	if (rval != QLA2X00_SUCCESS ||
		comp_status != CS_COMPLETE ||
		(scsi_status & SS_CHECK_CONDITION)) {

		DEBUG2(printk("%s: Failed lun inquiry - "
			"inq[0]= 0x%x, comp status 0x%x, "
			"scsi status 0x%x. loop_id=%d\n",
			__func__,pkt->inq[0], 
			comp_status,
			scsi_status, 
			fcport->loop_id);)
		ret = 1;
	} else {
		fclun->inq0 = pkt->inq[0];
		/* ALUA or TPGS field of inquiry data */
		fclun->asymm_support = ((pkt->inq[5] >> 4) & 3);	
		DEBUG5(printk("%s lun=%d asymm_support=%d, inq[0]=0x%x\n",
			__func__, fclun->lun, fclun->asymm_support,fclun->inq0);)
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), pkt, phys_address);

	return( ret );
}


int
qla2x00_test_active_lun( fc_port_t *fcport, fc_lun_t *fclun, uint8_t *wait_for_transition ) 
{
	tur_cmd_rsp_t	*pkt;
	int		rval = 0 ; 
	dma_addr_t	phys_address = 0;
	int		retry;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	scsi_qla_host_t *ha;
	uint16_t	lun = 0;
        uint16_t        *cstatus, *sstatus;
        uint8_t         *sense_data;

	ENTER(__func__);


	ha = fcport->ha;
	if (atomic_read(&fcport->state) == FC_DEVICE_DEAD){
		DEBUG2(printk("scsi(%ld) %s leaving: Port loop_id 0x%02x is marked DEAD\n",
			ha->host_no,__func__,fcport->loop_id);)
		return rval;
	}
	
	if ( fclun == NULL ){
		DEBUG2(printk("scsi(%ld) %s Bad fclun ptr on entry.\n",
			ha->host_no,__func__);)
		return rval;
	}
	
	lun = fclun->lun;

	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(tur_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - TUR\n",
		    ha->host_no);
		ha->mem_err++;
		return rval;
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                cstatus = &pkt->p.rsp24.comp_status;
                sstatus = &pkt->p.rsp24.scsi_status;
                sense_data = pkt->p.rsp24.data;
        } else {
                cstatus = &pkt->p.rsp.comp_status;
                sstatus = &pkt->p.rsp.scsi_status;
                sense_data = pkt->p.rsp.req_sense_data;
        }

	retry = 4;
	do {
		memset(pkt, 0, sizeof(tur_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                        pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
                        pkt->p.cmd24.entry_count = 1;
                        pkt->p.cmd24.nport_handle = cpu_to_le16(fcport->loop_id);
                        pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
                        pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
                        pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
                        pkt->p.cmd24.lun[1] = LSB(lun);
                        pkt->p.cmd24.lun[2] = MSB(lun);
                        host_to_fcp_swap(pkt->p.cmd24.lun,
                            sizeof(pkt->p.cmd24.lun));
                        pkt->p.cmd24.task = TSK_SIMPLE;
                        pkt->p.cmd24.fcp_cdb[0] = TEST_UNIT_READY;
                        host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
                            sizeof(pkt->p.cmd24.fcp_cdb));
                        pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(0);
                        pkt->p.cmd24.timeout = __constant_cpu_to_le16(3);
                } else {
			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = cpu_to_le16(lun);
#if defined(EXTENDED_IDS)
			pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
#else
			pkt->p.cmd.target = (uint8_t)fcport->loop_id;
#endif
			/* no direction for this command */
			pkt->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_SIMPLE_TAG);
			pkt->p.cmd.scsi_cdb[0] = TEST_UNIT_READY;
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(0);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(3);
			pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);
		}	

		rval = qla2x00_issue_iocb(ha, pkt,
			    phys_address, sizeof(tur_cmd_rsp_t));

		comp_status = le16_to_cpu(*cstatus);
		scsi_status = le16_to_cpu(*sstatus);

		/* Port Logged Out, so don't retry */
		if( 	comp_status == CS_PORT_LOGGED_OUT  ||
			comp_status == CS_PORT_CONFIG_CHG ||
			comp_status == CS_PORT_BUSY ||
			comp_status == CS_INCOMPLETE ||
			comp_status == CS_PORT_UNAVAILABLE )
			break;

		DEBUG(printk("scsi(%ld:%04x:%d) "
		       "%s: TEST UNIT READY - "
		    " comp status 0x%x, "
		    "scsi status 0x%x, rval=%d\n",ha->host_no,
			fcport->loop_id,
			lun,__func__,
		    comp_status, scsi_status, rval);)
		if( (scsi_status & SS_CHECK_CONDITION)  ) {
			DEBUG2(printk("%s: check status bytes =  0x%02x 0x%02x 0x%02x\n", 
			 __func__, sense_data[2], sense_data[12] , sense_data[13]);)

			if (sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
				 sense_data[13] == 0x2 ) 
				break;
 	     		if (fcport->flags & FC_DSXXX_DEVICE) {
				/* Wait for transition to complete */
				if (sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
				    sense_data[13] == 0xa )  {
					 rval = 1;
					 break;
				}
				/* Already switched status - active on other controller */
				if (sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
				    (sense_data[13] == 0xb || sense_data[13] == 0x00))  {
					 if(wait_for_transition)
						 *wait_for_transition = 0;
					 rval = 1;
					 break;
				}
			}
		}
	} while ((rval != QLA2X00_SUCCESS ||
	           comp_status != CS_COMPLETE ||
		   (scsi_status & SS_CHECK_CONDITION)) && 
		--retry);


	if (rval == QLA2X00_SUCCESS && retry != 0 &&
		( !( (scsi_status & SS_CHECK_CONDITION) && 
			(sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
			 sense_data[13] == 0x2 ) ) && 
	    comp_status == CS_COMPLETE) ) {
		DEBUG2(printk("scsi(%ld) %s - Lun (0x%02x:%d) set to ACTIVE.\n",
			ha->host_no, __func__,
			(uint8_t)fcport->loop_id,lun);)
		/* We found an active path */
			fclun->flags |= FC_ACTIVE_LUN;
		rval = 1;
	} else {
		rval = 0;
		DEBUG2(printk("scsi(%ld) %s - Lun (0x%02x:%d) set to INACTIVE .\n",
			ha->host_no, __func__,
			(uint8_t)fcport->loop_id,lun);)
	   		/* fcport->flags &= ~(FC_MSA_PORT_ACTIVE); */
			fclun->flags &= ~(FC_ACTIVE_LUN);
	}

	pci_free_consistent(ha->pdev, sizeof(tur_cmd_rsp_t), 
	    			pkt, phys_address);

	LEAVE(__func__);

	return rval;

}


static fc_lun_t *
qla2x00_find_data_lun( fc_port_t *fcport ) 
{
	scsi_qla_host_t *ha;
	fc_lun_t	*fclun, *ret_fclun;

	ha = fcport->ha;
	ret_fclun = NULL;

DEBUG2(printk("%s entered\n",__func__);)
	/* Go thur all luns and find a good data lun */
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		fclun->flags &= ~FC_VISIBLE_LUN;
		if (fclun->inq0 == 0xff)
			qla2x00_issue_scsi_inquiry(ha, fcport, fclun);
		if (fclun->inq0 == 0xc)
			fclun->flags |= FC_VISIBLE_LUN;
		else if (fclun->inq0 == 0 ) {
			ret_fclun = fclun;
		}
	}
	return (ret_fclun);
}

/*
 * qla2x00_test_active_port
 *	Determines if the port is in active or standby mode. First, we
 *	need to locate a storage lun then do a TUR on it. 
 *
 * Input:
 *	fcport = port structure pointer.
 *	
 *
 * Return:
 *	0  - Standby or error
 *  1 - Active
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_test_active_port( fc_port_t *fcport ) 
{
	tur_cmd_rsp_t	*pkt;
	int		fn_rval = 0, tmp_rval = 0 ; 
	dma_addr_t	phys_address = 0;
	int		retry;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	scsi_qla_host_t *ha;
	uint16_t	lun = 0;
	fc_lun_t	*fclun;
        uint16_t        *cstatus, *sstatus;
        uint8_t         *sense_data;

	ENTER(__func__);
	DEBUG2(printk("%s entered\n",__func__);)

	ha = fcport->ha;
	if (atomic_read(&fcport->state) == FC_DEVICE_DEAD){
		DEBUG2(printk("scsi(%ld) %s leaving: Port 0x%02x is marked DEAD\n",
			ha->host_no,__func__,fcport->loop_id);)
		return  fn_rval;
	}
		

	if( (fclun = qla2x00_find_data_lun( fcport )) == NULL ) {
		DEBUG2(printk(KERN_INFO "%s leaving: Couldn't find data lun\n",__func__);)
		return fn_rval;
	} 
	lun = fclun->lun;

	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(tur_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - TUR\n",
		    ha->host_no);
		ha->mem_err++;
		return fn_rval;
	}
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
		cstatus = &pkt->p.rsp24.comp_status;
		sstatus = &pkt->p.rsp24.scsi_status;
		sense_data = pkt->p.rsp24.data;
	} else {
		cstatus = &pkt->p.rsp.comp_status;
		sstatus = &pkt->p.rsp.scsi_status;
		sense_data = pkt->p.rsp.req_sense_data;
	}


	retry = 4;
	do {
		memset(pkt, 0, sizeof(tur_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                        pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
                        pkt->p.cmd24.entry_count = 1;
                        pkt->p.cmd24.nport_handle = cpu_to_le16(fcport->loop_id);
                        pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
                        pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
                        pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
                        pkt->p.cmd24.lun[1] = LSB(lun);
                        pkt->p.cmd24.lun[2] = MSB(lun);
                        host_to_fcp_swap(pkt->p.cmd24.lun,
                            sizeof(pkt->p.cmd24.lun));
                        pkt->p.cmd24.task = TSK_SIMPLE;
                        pkt->p.cmd24.fcp_cdb[0] = TEST_UNIT_READY;
                        host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
                            sizeof(pkt->p.cmd24.fcp_cdb));
                        pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(0);
                        pkt->p.cmd24.timeout = __constant_cpu_to_le16(3);
                } else {
			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			pkt->p.cmd.lun = cpu_to_le16(lun);
			/* pkt->p.cmd.lun = lun; */
#if defined(EXTENDED_IDS)
			pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
#else
			pkt->p.cmd.target = (uint8_t)fcport->loop_id;
#endif
			/* no direction for this command */
			pkt->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_SIMPLE_TAG);
			pkt->p.cmd.scsi_cdb[0] = TEST_UNIT_READY;
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(0);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(3);
			pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);
		}

		tmp_rval = qla2x00_issue_iocb(ha, pkt,
			    phys_address, sizeof(tur_cmd_rsp_t));

		comp_status = le16_to_cpu(*cstatus);
		scsi_status = le16_to_cpu(*sstatus);

 		/* Port Logged Out, so don't retry */
		if( 	comp_status == CS_PORT_LOGGED_OUT  ||
			comp_status == CS_PORT_CONFIG_CHG ||
			comp_status == CS_PORT_BUSY ||
			comp_status == CS_INCOMPLETE ||
			comp_status == CS_PORT_UNAVAILABLE )
			break;

		DEBUG(printk("scsi(%ld:%04x:%d) "
		       "%s: TEST UNIT READY - "
		    " comp status 0x%x, "
		    "scsi status 0x%x, rval=%d\n",ha->host_no,
			fcport->loop_id,
			lun,__func__,
		    comp_status, scsi_status, tmp_rval);)
		if( (scsi_status & SS_CHECK_CONDITION)  ) {
			DEBUG2(printk("%s: check status bytes =  0x%02x 0x%02x 0x%02x\n", 
			 __func__, pkt->p.rsp.req_sense_data[2], sense_data[12] ,
			 sense_data[13]);)

			if (sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
				 sense_data[13] == 0x2 ) 
				break;
		}
	} while ( (tmp_rval != QLA2X00_SUCCESS ||
	           comp_status != CS_COMPLETE ||
		   (scsi_status & SS_CHECK_CONDITION)) && 
		--retry);

	if (tmp_rval == QLA2X00_SUCCESS && retry != 0 &&
		( !( (scsi_status & SS_CHECK_CONDITION) && 
			(sense_data[2] == NOT_READY && sense_data[12] == 0x4 &&
			 sense_data[13] == 0x2 ) ) && 
	    comp_status == CS_COMPLETE) ) {
		DEBUG2(printk("scsi(%ld) %s - Port (0x%04x) set to ACTIVE.\n",
			ha->host_no, __func__,
			fcport->loop_id);)
		/* We found an active path */
       		fcport->flags |= FC_MSA_PORT_ACTIVE;
		fn_rval = 1;
	} else {
		DEBUG2(printk("scsi(%ld) %s - Port (0x%04x) set to INACTIVE.\n",
			ha->host_no, __func__,
			fcport->loop_id);)
       		fcport->flags &= ~(FC_MSA_PORT_ACTIVE);
	}

	pci_free_consistent(ha->pdev, sizeof(tur_cmd_rsp_t), 
	    			pkt, phys_address);

	LEAVE(__func__);

	return fn_rval;

}

/* 
 * Enable Auto Restore enables auto failback capability.
 */
static inline void
qla2x00_enable_auto_restore(fc_port_t *fcport)
{
	if (fcport->cfg_id == -1){
	 	DEBUG2(printk("%s(%ld) this device: loop_id=0x%x is not in"
			" the cfg_device_list[] database\n", __func__,
			fcport->ha->host_no, fcport->loop_id);)
		return;
	}

	if (fcport->flags & (FC_MSA_DEVICE|FC_EVA_DEVICE)) {
		DEBUG2(printk("%s(%ld) support disabled for MSA/EVA Active/Passive device\n",
			    __func__, fcport->ha->host_no);)
		return;    
	}

	if (cfg_device_list[fcport->cfg_id].flags & ql2xautorestore) {
		if (fcport->flags & FC_FAILBACK_DISABLE)
			fcport->flags &= ~FC_FAILBACK_DISABLE;
		else
			fcport->flags |= FC_FAILBACK_DISABLE;

		if (fcport->flags & FC_FAILBACK_DISABLE) {
			DEBUG2(printk("%s(%ld): disabled autorestore for loop_id=0x%x "
				 "fcport_flags=0x%x\n", __func__, fcport->ha->host_no,
				  fcport->loop_id, fcport->flags);)
		} else {
			DEBUG2(printk("%s(%ld): enabled autorestore for loop_id=0x%x "
				 "fcport_flags=0x%x\n", __func__, fcport->ha->host_no,
				  fcport->loop_id, fcport->flags);)
		}
	}

}

void
qla2x00_set_device_flags(scsi_qla_host_t *ha, 
	fc_port_t *fcport )
{

	if (fcport->cfg_id != -1){
 	     fcport->flags &= ~(FC_XP_DEVICE|FC_MSA_DEVICE|FC_EVA_DEVICE|
 				FC_DSXXX_DEVICE | FC_AA_EVA_DEVICE |
				FC_DFXXX_DEVICE | FC_AA_MSA_DEVICE);
	   if ( (cfg_device_list[fcport->cfg_id].flags & BIT_0) ){
		printk(KERN_INFO 
		"scsi(%ld) :Loop id 0x%04x is an XP device\n",
		ha->host_no,
		fcport->loop_id);
                fcport->flags |= FC_XP_DEVICE;
	   } else if ( (cfg_device_list[fcport->cfg_id].flags & BIT_1) ){
		printk(KERN_INFO 
		"scsi(%ld) :Loop id 0x%04x is a MSA1000 device\n",
		ha->host_no,
		fcport->loop_id);
                fcport->flags |= FC_MSA_DEVICE;
		fcport->flags |= FC_FAILBACK_DISABLE;
	   } else if ( (cfg_device_list[fcport->cfg_id].flags & BIT_2) ){
		printk(KERN_INFO 
		"scsi(%ld) :Loop id 0x%04x is a EVA device\n",
		ha->host_no,
		fcport->loop_id);
                fcport->flags |= FC_EVA_DEVICE;
		fcport->flags |= FC_FAILBACK_DISABLE;
	   } else if ((cfg_device_list[fcport->cfg_id].flags & BIT_4)) {
                 printk(KERN_INFO
                     "scsi(%ld) :Loop id 0x%04x is an DS400 device\n",
                          ha->host_no, fcport->loop_id);
                 fcport->flags |= FC_DSXXX_DEVICE;
           } else if ((cfg_device_list[fcport->cfg_id].flags & BIT_5)) {
                 printk(KERN_INFO
                     "scsi(%ld) :Loop id 0x%04x is an EVA_XL (AA) device\n",
                          ha->host_no, fcport->loop_id);
                 fcport->flags |= FC_AA_EVA_DEVICE;
 		 fcport->flags |= FC_FAILBACK_DISABLE;
	   } else if ((cfg_device_list[fcport->cfg_id].flags & BIT_7)) {
		   printk(KERN_INFO
		       "scsi(%ld) :Loop id 0x%04x is an MSA (AA) device\n",
		       ha->host_no, fcport->loop_id);
		   fcport->flags |= FC_AA_MSA_DEVICE;
 		   fcport->flags |= FC_FAILBACK_DISABLE;
           } else if ((cfg_device_list[fcport->cfg_id].flags & BIT_8)) {
                  printk(KERN_INFO
                      "scsi(%ld) :Loop id 0x%04x is an DF700 device\n",
                           ha->host_no, fcport->loop_id);
                  fcport->flags |= FC_DFXXX_DEVICE;
 		  fcport->fo_target_port = qla2x00_get_vol_access_path;
	   }

	   if ( (cfg_device_list[fcport->cfg_id].flags & 8) ){
		fcport->flags |= FC_FAILOVER_DISABLE;
		printk(KERN_INFO 
		"scsi(%ld) :Loop id 0x%04x has FAILOVERS disabled.\n",
		ha->host_no,
		fcport->loop_id);
	   }

	   /* Enable Failback for the devices specified 
	    * on the cmd line.
	    */			
	   qla2x00_enable_auto_restore(fcport);
	}
}

/*
 * qla2x00_lun_discovery
 *	Issue SCSI inquiry command for LUN discovery.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = FC port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport) 
{
	inq_cmd_rsp_t	*pkt;
	int		rval;
	uint16_t	lun;
	struct list_head	*fcll;
	fc_lun_t	*fclun;
	int		found;
	dma_addr_t	phys_address = 0;
	int		disconnected;
	int		retry;
	int		rlc_succeeded, first;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint16_t	next_loopid;
	uint16_t *cstatus, *sstatus;
        uint8_t *sense_data;
	

	ENTER(__func__);

	/* 
	 * Immediately issue a RLC to the fcport
	 */
	rlc_succeeded = 0;
	if (qla2x00_rpt_lun_discovery(ha, fcport) == QLA2X00_SUCCESS) {
		/* 
		 * We always need at least LUN 0 to be present in our fclun
		 * list if RLC succeeds.
		 */
		qla2x00_cfg_lun(fcport, 0);
		/* 
		 * At least do an inquiry on LUN 0 to determine peripheral
		 * qualifier type.
		 */
		rlc_succeeded = 1;
	}
	/*
	 * RLC failed for some reason, try basic inquiries
	 */
	pkt = pci_alloc_consistent(ha->pdev,
				sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
			"scsi(%ld): Memory Allocation failed - INQ\n",
			ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                cstatus = &pkt->p.rsp24.comp_status;
                sstatus = &pkt->p.rsp24.scsi_status;
                sense_data = pkt->p.rsp24.data;
        } else {
                cstatus = &pkt->p.rsp.comp_status;
                sstatus = &pkt->p.rsp.scsi_status;
                sense_data = pkt->p.rsp.req_sense_data;
        }

	first = 0;
	for (lun = 0; lun < ha->max_probe_luns; lun++) {
		retry = 2;
		do {
			memset(pkt, 0, sizeof(inq_cmd_rsp_t));
			if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
				pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
				pkt->p.cmd24.entry_count = 1;
				pkt->p.cmd24.nport_handle = cpu_to_le16(fcport->loop_id);
				pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
				pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
				pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
				pkt->p.cmd24.lun[1] = LSB(lun);
				pkt->p.cmd24.lun[2] = MSB(lun);
				host_to_fcp_swap(pkt->p.cmd24.lun,
				    sizeof(pkt->p.cmd24.lun));
				pkt->p.cmd24.task_mgmt_flags =
				    __constant_cpu_to_le16(TMF_READ_DATA);
				pkt->p.cmd24.task = TSK_SIMPLE;
				pkt->p.cmd24.fcp_cdb[0] = INQ_SCSI_OPCODE;
				pkt->p.cmd24.fcp_cdb[4] = INQ_DATA_SIZE;
				host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
				    sizeof(pkt->p.cmd24.fcp_cdb));
				pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(1);
				pkt->p.cmd24.timeout = __constant_cpu_to_le16(10);
				pkt->p.cmd24.byte_count =
				    __constant_cpu_to_le32(INQ_DATA_SIZE);
				pkt->p.cmd24.dseg_0_address[0] = cpu_to_le32(
				    LSD(phys_address + sizeof(struct sts_entry_24xx)));
				pkt->p.cmd24.dseg_0_address[1] = cpu_to_le32(
				    MSD(phys_address + sizeof(struct sts_entry_24xx)));
				pkt->p.cmd24.dseg_0_len =
				    __constant_cpu_to_le32(INQ_DATA_SIZE);
			} else {
		
				// FIXME: dma_addr_t could be 64bits in length!
				pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
				pkt->p.cmd.entry_count = 1;
				pkt->p.cmd.lun = cpu_to_le16(lun);
				#if defined(EXTENDED_IDS)
					pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
				#else
					pkt->p.cmd.target = (uint8_t)fcport->loop_id;
				#endif
				pkt->p.cmd.control_flags =
					__constant_cpu_to_le16(CF_READ | CF_SIMPLE_TAG);
				pkt->p.cmd.scsi_cdb[0] = INQ_SCSI_OPCODE;
				pkt->p.cmd.scsi_cdb[4] = INQ_DATA_SIZE;
				pkt->p.cmd.dseg_count = __constant_cpu_to_le16(1);
				pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
				pkt->p.cmd.byte_count =
					__constant_cpu_to_le32(INQ_DATA_SIZE);
				pkt->p.cmd.dseg_0_address[0] = cpu_to_le32(
				      LSD(phys_address + sizeof(sts_entry_t)));
				pkt->p.cmd.dseg_0_address[1] = cpu_to_le32(
				      MSD(phys_address + sizeof(sts_entry_t)));
				pkt->p.cmd.dseg_0_length =
					__constant_cpu_to_le32(INQ_DATA_SIZE);
			}

			DEBUG(printk("lun_discovery: Lun Inquiry - fcport=%p,"
					" lun (%d)\n", 
					fcport, 
					lun);)
		
			DEBUG3(qla2x00_dump_buffer((uint8_t *)&pkt->p.cmd,
						REQUEST_ENTRY_SIZE);)
			rval = qla2x00_issue_iocb(ha, pkt,
					phys_address, sizeof(inq_cmd_rsp_t));

			comp_status = le16_to_cpu(*cstatus);
			scsi_status = le16_to_cpu(*sstatus);

			DEBUG2(printk("lun_discovery: lun (%d) inquiry - "
					"inq[0]= 0x%x, comp status 0x%x, "
					"scsi status 0x%x, rval=%d\n",
					lun, pkt->inq[0], 
					comp_status,
					scsi_status, 
					rval);)

			/* if port not logged in then try and login */
			if (rval == QLA2X00_SUCCESS && 
			    comp_status == CS_PORT_LOGGED_OUT &&
			    atomic_read(&fcport->state) != FC_DEVICE_DEAD) {
				if (fcport->flags & FC_FABRIC_DEVICE) {
					/* login and update database */
					next_loopid = 0;
					qla2x00_fabric_login(ha, fcport,
					    &next_loopid);
				} else {
					/* Loop device gone but no LIP... */
					rval = QL_STATUS_ERROR;
					break;
				}
			}
		} while ((rval != QLA2X00_SUCCESS ||
				comp_status != CS_COMPLETE) && 
				retry--);

		if (rval != QLA2X00_SUCCESS ||
			comp_status != CS_COMPLETE ||
			(scsi_status & SS_CHECK_CONDITION)) {

			    /* Skip past any FCP RESPONSE data. */
			if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
				host_to_fcp_swap(sense_data,
				    sizeof(pkt->p.rsp24.data));
					if (scsi_status &
                                            SS_RESPONSE_INFO_LEN_VALID)
                                                sense_data += le32_to_cpu(
                                                    pkt->p.rsp24.rsp_data_len);
                                }
			DEBUG2(printk("lun_discovery: Failed lun inquiry - "
					"inq[0]= 0x%x, comp status 0x%x, "
					"scsi status 0x%x. loop_id=%d\n",
					pkt->inq[0], 
					comp_status,
					scsi_status, 
					fcport->loop_id);)

			break;
		}

		disconnected = 0;

		/*
		 * We only need to issue an inquiry on LUN 0 to determine the
		 * port's peripheral qualifier type
		 */
		if (rlc_succeeded == 1) {
			if (pkt->inq[0] == 0 || pkt->inq[0] == 0xc) {
				fcport->flags &= ~(FC_TAPE_DEVICE);
			} else if (pkt->inq[0] == 1 || pkt->inq[0] == 8) {
				fcport->flags |= FC_TAPE_DEVICE;
			}
			/* Does this port require special failover handling? */
			if (ha->flags.failover_enabled) {
				fcport->cfg_id = qla2x00_cfg_lookup_device(
					&pkt->inq[0]);
				qla2x00_set_device_flags(ha, fcport);
			}
			/* Stop the scan */
			break;
		}

		/* inq[0] ==:
		 *	 0x0- Hard Disk.
		 *	 0xc- is a processor device.	
		 *	 0x1- is a Tape Device.
		 *       0x8- is a medium changer device
		 * 	      which is basically a Tape device.
		 */
		if (pkt->inq[0] == 0 || pkt->inq[0] == 0xc) {
			fcport->flags &= ~(FC_TAPE_DEVICE);
		} else if (pkt->inq[0] == 1 || pkt->inq[0] == 8) {
			fcport->flags |= FC_TAPE_DEVICE;
		} else if (pkt->inq[0] == 0x20 || pkt->inq[0] == 0x7f) {
			disconnected++;
		} else {
			continue;
		}
		
		/* Does this port require special failover handling? */
		if (ha->flags.failover_enabled && !first) {
		   	fcport->cfg_id = qla2x00_cfg_lookup_device(&pkt->inq[0]);
		   	qla2x00_set_device_flags(ha,fcport);
				first++;
		}
		/* Allocate LUN if not already allocated. */
		found = 0;
		list_for_each(fcll, &fcport->fcluns) {
			fclun = list_entry(fcll, fc_lun_t, list);

			if (fclun->lun == lun) {
				found++;
				break;
			}
		}
		if (found)
			continue;

		/* Add this lun to our list */
		fclun = kmalloc(sizeof(fc_lun_t), GFP_ATOMIC);
		if (fclun != NULL) {
			fcport->lun_cnt++;
			/* Setup LUN structure. */
			memset(fclun, 0, sizeof(fc_lun_t));

			DEBUG5(printk("lun_discovery: Allocated fclun %p, "
					"disconnected=%d\n", 
					fclun,
					disconnected);)

			fclun->fcport = fcport;
			fclun->lun = lun;
			fclun->inq0 = 0xff;

			if (disconnected)
				fclun->flags |= FC_DISCON_LUN;

			list_add_tail(&fclun->list, &fcport->fcluns);


	 	 	DEBUG5(printk("lun_discvery: Allocated fclun %p, "
					"fclun.lun=%d\n", 
					fclun, fclun->lun););
		} else {
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - FCLUN\n",
				ha->host_no);
			ha->mem_err++;
			pci_free_consistent(ha->pdev,
						 sizeof(inq_cmd_rsp_t),
						 pkt,
						 phys_address);
			return BIT_0;
		}

	}

	DEBUG2(printk("lun_discovery(%ld): fcport lun count=%d, fcport= %p\n", 
			ha->host_no,
			fcport->lun_cnt, 
			fcport);)

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), pkt, phys_address);

	LEAVE(__func__);

	return 0;
}

/*
 * qla2x00_rpt_lun_discovery
 *	Issue SCSI report LUN command for LUN discovery.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_rpt_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport) 
{
	rpt_lun_cmd_rsp_t	*pkt;
	dma_addr_t		phys_address = 0;
	int			rval;
	uint32_t		len, cnt;
	uint8_t			retries;
	uint16_t		lun;
	uint16_t		comp_status;
	uint16_t		scsi_status;
	uint16_t 		*cstatus, *sstatus;
	uint16_t		next_loopid;
        uint8_t 		*sense_data;

	ENTER(__func__);

	/* Assume a failed status */
	rval = QLA2X00_FAILED;

	/* No point in continuing if the device doesn't support RLC */
	if (!(fcport->flags & FC_SUPPORT_RPT_LUNS))
		return (rval);

	pkt = pci_alloc_consistent(ha->pdev,
			sizeof(rpt_lun_cmd_rsp_t),
			&phys_address);
	if (pkt == NULL) {
		printk(KERN_WARNING
			"scsi(%ld): Memory Allocation failed - RLC",
			ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}

	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                cstatus = &pkt->p.rsp24.comp_status;
                sstatus = &pkt->p.rsp24.scsi_status;
                sense_data = pkt->p.rsp24.data;
        } else {
                cstatus = &pkt->p.rsp.comp_status;
                sstatus = &pkt->p.rsp.scsi_status;
                sense_data = pkt->p.rsp.req_sense_data;
        }


	for (retries = 4; retries; retries--) {
		// FIXME: dma_addr_t could be 64bits in length!
		memset(pkt, 0, sizeof(rpt_lun_cmd_rsp_t));
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			pkt->p.cmd24.entry_type = COMMAND_TYPE_7;
                        pkt->p.cmd24.entry_count = 1;
                        pkt->p.cmd24.nport_handle = cpu_to_le16(fcport->loop_id);
                        pkt->p.cmd24.port_id[0] = fcport->d_id.b.al_pa;
                        pkt->p.cmd24.port_id[1] = fcport->d_id.b.area;
                        pkt->p.cmd24.port_id[2] = fcport->d_id.b.domain;
                        pkt->p.cmd24.task_mgmt_flags =
                            __constant_cpu_to_le16(TMF_READ_DATA);
                        pkt->p.cmd24.task = TSK_SIMPLE;
                        pkt->p.cmd24.fcp_cdb[0] = RPT_LUN_SCSI_OPCODE;
                        pkt->p.cmd24.fcp_cdb[8] = MSB(sizeof(rpt_lun_lst_t));
                        pkt->p.cmd24.fcp_cdb[9] = LSB(sizeof(rpt_lun_lst_t));
                        host_to_fcp_swap(pkt->p.cmd24.fcp_cdb,
                            sizeof(pkt->p.cmd24.fcp_cdb));
                        pkt->p.cmd24.dseg_count = __constant_cpu_to_le16(1);
                        pkt->p.cmd24.timeout = __constant_cpu_to_le16(10);
                        pkt->p.cmd24.byte_count =
                            __constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
                                                                                                               
                        pkt->p.cmd24.dseg_0_address[0] = cpu_to_le32(
                            LSD(phys_address + sizeof(struct sts_entry_24xx)));
                        pkt->p.cmd24.dseg_0_address[1] = cpu_to_le32(
                            MSD(phys_address + sizeof(struct sts_entry_24xx)));
                        pkt->p.cmd24.dseg_0_len =
                            __constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
                } else {
			pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
			pkt->p.cmd.entry_count = 1;
			#if defined(EXTENDED_IDS)
				pkt->p.cmd.target = cpu_to_le16(fcport->loop_id);
			#else
				pkt->p.cmd.target = (uint8_t)fcport->loop_id;
			#endif
			pkt->p.cmd.control_flags =
				__constant_cpu_to_le16(CF_READ | CF_SIMPLE_TAG);
			pkt->p.cmd.scsi_cdb[0] = RPT_LUN_SCSI_OPCODE;
			pkt->p.cmd.scsi_cdb[8] = MSB(sizeof(rpt_lun_lst_t));
			pkt->p.cmd.scsi_cdb[9] = LSB(sizeof(rpt_lun_lst_t));
			pkt->p.cmd.dseg_count = __constant_cpu_to_le16(1);
			pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
			pkt->p.cmd.byte_count = 
				__constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
			pkt->p.cmd.dseg_0_address[0] = cpu_to_le32(
				LSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_address[1] = cpu_to_le32(
				MSD(phys_address + sizeof(sts_entry_t)));
			pkt->p.cmd.dseg_0_length =
				__constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
		}
		rval = qla2x00_issue_iocb(ha, pkt, phys_address,
				sizeof(rpt_lun_cmd_rsp_t));

		comp_status = le16_to_cpu(*cstatus);
		scsi_status = le16_to_cpu(*sstatus);
		/* if port not logged in then try and login */
		if (rval == QLA2X00_SUCCESS && 
		    comp_status == CS_PORT_LOGGED_OUT &&
		    atomic_read(&fcport->state) != FC_DEVICE_DEAD) {
			if (fcport->flags & FC_FABRIC_DEVICE) {
				/* login and update database */
				next_loopid = 0;
				qla2x00_fabric_login(ha, fcport,
				    &next_loopid);
				continue;
			} else {
				/* Loop device gone but no LIP... */
				rval = QL_STATUS_ERROR;
				break;
			}
		}

		if (rval != QLA2X00_SUCCESS ||
			comp_status != CS_COMPLETE ||
			scsi_status & SS_CHECK_CONDITION) {

			/* Device underrun, treat as OK. */
			if (comp_status == CS_DATA_UNDERRUN &&
				scsi_status & SS_RESIDUAL_UNDER) {

				rval = QLA2X00_SUCCESS;
				break;
			}

			DEBUG(printk("%s(%ld): FAILED, issue_iocb fcport = %p "
					"rval = %x cs = %x ss = %x\n",
					__func__,
					ha->host_no,
					fcport,
					rval,
					comp_status,
					scsi_status);)

			rval = QLA2X00_FAILED;
			if (scsi_status & SS_CHECK_CONDITION) {
				    /* Skip past any FCP RESPONSE data. */
				if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
                                        host_to_fcp_swap(sense_data,
                                            sizeof(pkt->p.rsp24.data));
                                        if (scsi_status &
                                            SS_RESPONSE_INFO_LEN_VALID)
                                                sense_data += le32_to_cpu(
                                                    pkt->p.rsp24.rsp_data_len);
                                }

				DEBUG2(printk(KERN_INFO "%s(%ld): SS_CHECK_CONDITION "
						"Sense Data "
						"%02x %02x %02x %02x "
						"%02x %02x %02x %02x\n",
						__func__,
						ha->host_no, sense_data[0], sense_data[1],
						sense_data[2], sense_data[3], sense_data[4],
						sense_data[5], sense_data[6], sense_data[7]);)
				/* No point in retrying if ILLEGAL REQUEST */
				if (sense_data[2] == ILLEGAL_REQUEST) {
					/* Clear RLC support flag */
					fcport->flags &= ~(FC_SUPPORT_RPT_LUNS);
					break;
				}
			}
		} else {
			break;
		}
	}

	/* Test for report LUN failure. */
	if (rval == QLA2X00_SUCCESS) {
		/* Configure LUN list. */
		len = be32_to_cpu(pkt->list.hdr.len);
		len /= 8;
		if (len == 0) {
			rval = QLA2X00_FAILED;
		} else {
			for (cnt = 0; cnt < len; cnt++) {
				lun = CHAR_TO_SHORT(pkt->list.lst[cnt].lsb,
						pkt->list.lst[cnt].msb.b);

				DEBUG3(printk("%s(%ld): lun = (%d)\n",
						__func__,
						ha->host_no,
						lun);)

				/* We only support 0 through MAX_LUNS-1 range */
				if (lun < MAX_LUNS) {
					qla2x00_cfg_lun(fcport, lun);
				}
			}
			rval = QLA2X00_SUCCESS;
		}
	} else {
		rval = QLA2X00_FAILED;
	}

	pci_free_consistent(ha->pdev, sizeof(rpt_lun_cmd_rsp_t),
			pkt, phys_address);


	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_cfg_lun
 *	Configures LUN into fcport LUN list.
 *
 * Input:
 *	fcport:		FC port structure pointer.
 *	lun:		LUN number.
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_cfg_lun(fc_port_t *fcport, uint16_t lun) 
{
	int found;
	fc_lun_t		*fclun;

	/* Allocate LUN if not already allocated. */
 	found = 0;
 	list_for_each_entry(fclun, &fcport->fcluns, list) {
		if (fclun->lun == lun) {
			found++;
			break;
		}
	}
	if (!found) {
		fclun = kmalloc(sizeof(fc_lun_t), GFP_ATOMIC);
		if (fclun != NULL) {
			fcport->lun_cnt++;

			/* Setup LUN structure. */
			memset(fclun, 0, sizeof(fc_lun_t));
			fclun->fcport = fcport;
			fclun->lun = lun;
			fclun->inq0 = 0xff;

 			list_add_tail(&fclun->list, &fcport->fcluns);
		} else {
			printk(KERN_WARNING
				"%s(): Memory Allocation failed - FCLUN\n",
				__func__);
		}
	}
}

/*
 * Qla2x00_configure_local_loop
 *	Updates Fibre Channel Device Database with local loop devices.
 *
 * Input:
 *	ha = adapter block pointer.
 *	enable_slot_reuse = allows the use of PORT_AVAILABLE slots.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = error.
 *	BIT_1 = database was full and a device was not configured.
 */
static uint8_t
qla2x00_configure_local_loop(scsi_qla_host_t *ha) 
{
	uint8_t  rval;
	int  rval2;
	uint16_t localdevices;

	uint16_t	index;
	uint16_t	entries;
	uint16_t	loop_id;
	struct dev_id  *id_list;
	dma_addr_t	id_list_dma;

	int		found;
	fc_port_t	*fcport, *new_fcport;
	uint8_t		domain, area, al_pa;
	char		*id_iter;

	ENTER(__func__);
	localdevices = 0;
	new_fcport = NULL;

	/*
	 * No point in continuing if the loop is in a volatile state -- 
	 * reschedule LOCAL_LOOP_UPDATE for later processing
	 */
	if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
	    atomic_read(&ha->loop_state) == LOOP_DOWN) {
		set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
		return (0);
	}

	entries = MAX_FIBRE_DEVICES;
	id_list = pci_alloc_consistent(ha->pdev, MAX_ID_LIST_SIZE,
	    &id_list_dma);
	if (id_list == NULL) {
		DEBUG2(printk("scsi(%ld): Failed to allocate memory, No local "
		    "loop\n", ha->host_no));

		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - port_list",
		    ha->host_no);

		ha->mem_err++;
		return (BIT_0);
	}
	memset(id_list, 0, MAX_ID_LIST_SIZE);

	DEBUG3(printk("scsi(%ld): Getting FCAL position map\n", ha->host_no));
	DEBUG3(qla2x00_get_fcal_position_map(ha, NULL));

	/* Get list of logged in devices. */
	rval = qla2x00_get_id_list(ha, id_list, id_list_dma, &entries);
	if (rval) {
		rval = BIT_0;
		goto cleanup_allocation;
	}

	DEBUG3(printk("scsi(%ld): Entries in ID list (%d)\n",
	    ha->host_no, entries));
	DEBUG3(qla2x00_dump_buffer((uint8_t *)id_list,
	    entries * sizeof(struct dev_id)));

	/* Allocate temporary fcport for any new fcports discovered. */
	new_fcport = qla2x00_alloc_fcport(ha, GFP_KERNEL);
	if (new_fcport == NULL) {
		rval = BIT_0;
		goto cleanup_allocation;
	}

	/* Mark all local ports LOST first */
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (!(fcport->flags & FC_FABRIC_DEVICE)) {
			/*
			 * No point in marking the device as lost, if the
			 * device is already DEAD.
			 */
			if (atomic_read(&fcport->state) == FC_DEVICE_DEAD)
				continue;

			atomic_set(&fcport->state, FC_DEVICE_LOST);
		}
	}

	/* Add devices to port list. */
	id_iter = (char *) id_list;
	for (index = 0; index < entries; index++) {
		/* Loop in transition */
		if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
		    test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
		    atomic_read(&ha->loop_state) == LOOP_DOWN) {
			set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
			rval = 0;
			goto cleanup_allocation;
		}
		domain = ((struct dev_id *)id_iter)->domain;
		area = ((struct dev_id *)id_iter)->area;
		al_pa = ((struct dev_id *)id_iter)->al_pa;
		/* Bypass reserved domain fields. */
		if ((domain & 0xf0) == 0xf0)
			continue;

		/* Bypass if not same domain and area of adapter.  */
		if (area != 0 && domain != 0) {
			if (area != ha->d_id.b.area || domain != ha->d_id.b.domain) 
				continue;
		}


		/* Bypass invalid local loop ID. */
#if defined(EXTENDED_IDS)
		if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha)) {
			loop_id = le16_to_cpu(
			    ((struct dev_id *)id_iter)->loop_id);
			id_iter += 8;
		} else {
			loop_id = le16_to_cpu(
			    ((struct dev_id *)id_iter)->loop_id);
			id_iter += 6;
		}
#else
		loop_id = ((struct dev_id *) id_iter)->loop_id;
#endif
		if (loop_id > LAST_LOCAL_LOOP_ID)
			continue;

		/* Fill in member data. */
		new_fcport->d_id.b.domain = domain;
		new_fcport->d_id.b.area = area;
		new_fcport->d_id.b.al_pa = al_pa;
		new_fcport->loop_id = loop_id;
		rval2 = qla2x00_get_port_database(ha, new_fcport, 0);
		if (rval2 != QL_STATUS_SUCCESS) {
			DEBUG2(printk("scsi(%ld): Failed to retrieve fcport "
			    "information -- get_port_database=%x, "
			    "loop_id=0x%04x\n",
			    ha->host_no, rval2, new_fcport->loop_id));
			continue;
		}

		/* Check for matching device in port list. */
		found = 0;
		fcport = NULL;
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (memcmp(new_fcport->port_name, fcport->port_name,
			    WWN_SIZE))
				continue;

			fcport->flags &= ~FC_FABRIC_DEVICE;
			fcport->loop_id = new_fcport->loop_id;
			fcport->port_type = new_fcport->port_type;
			fcport->d_id.b24 = new_fcport->d_id.b24;
			memcpy(fcport->node_name, new_fcport->node_name,
			    WWN_SIZE);

			found++;
			break;
		}

		if (!found) {
			/* New device, add to fcports list. */
			list_add_tail(&new_fcport->list, &ha->fcports);

			/* Allocate a new replacement fcport. */
			fcport = new_fcport;
			new_fcport = qla2x00_alloc_fcport(ha, GFP_KERNEL);
			if (new_fcport == NULL) {
				rval = BIT_0;
				goto cleanup_allocation;
			}
			DEBUG(printk("%s(%ld) found a tgt port fcport=%p\n",
			    	__func__, ha->host_no, new_fcport);)
		}

		rval2 = qla2x00_update_fcport(ha, fcport);
		if ((rval2 & BIT_0) && ha->mem_err) {		
			rval = BIT_0;
			goto cleanup_allocation;
		}

		localdevices++;
	}

cleanup_allocation:
	pci_free_consistent(ha->pdev, MAX_ID_LIST_SIZE, id_list, id_list_dma);

	if (new_fcport)
		kfree(new_fcport);

	if (rval & BIT_0) {
		DEBUG2(printk("scsi(%ld): Configure local loop error exit: "
		    "rval=%x\n", ha->host_no, rval));
	} else
		DEBUG2(printk("%s(%ld) successful\n", __func__, ha->host_no);)

	if (localdevices > 0) {
		ha->device_flags |= DFLG_LOCAL_DEVICES;
		ha->device_flags &= ~DFLG_RETRY_LOCAL_DEVICES;
	}
	DEBUG(printk("%s(%ld) rval=%d localdevices=%d\n", __func__, ha->host_no, rval,localdevices);)
	return (rval);
}


/*
 * qla2x00_tgt_alloc
 *	Allocate and pre-initialize target queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 */
os_tgt_t *
qla2x00_tgt_alloc(scsi_qla_host_t *ha, uint16_t t) 
{
	os_tgt_t	*tq;

	ENTER(__func__);

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS) {
		DEBUG2(printk(KERN_INFO "%s(%ld): *** Invalid target number, exiting ***",
				__func__,
				ha->host_no);)
		return (NULL);
	}

	tq = TGT_Q(ha, t);
	if (tq == NULL) {
		tq = kmalloc(sizeof(os_tgt_t), GFP_ATOMIC);
		if (tq != NULL) {
			DEBUG(printk("Alloc Target %d @ %p\n", t, tq);)

			memset(tq, 0, sizeof(os_tgt_t));
			tq->flags = TGT_TAGGED_QUEUE;
			tq->ha = ha;

			TGT_Q(ha, t) = tq;
		}
	}
	if (tq != NULL) {
		tq->port_down_retry_count = ha->port_down_retry_count;
	} else {
		printk(KERN_WARNING
			"%s(%ld): Failed to allocate target\n",
			__func__,
			ha->host_no);
		ha->mem_err++;
	}

	LEAVE(__func__);

	return (tq);
}

/*
 * qla2x00_tgt_free
 *	Frees target and LUN queues.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_tgt_free(scsi_qla_host_t *ha, uint16_t t) 
{
	os_tgt_t	*tq;
	uint16_t	l;

	ENTER(__func__);

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS) {
		DEBUG2(printk(KERN_INFO "%s(): **** FAILED exiting ****", __func__);)

		return;
	}

	tq = TGT_Q(ha, t);
	if (tq != NULL) {
		DEBUG(printk("Dealloc target @ %p -- deleted\n", tq);)

		/* Free LUN structures. */
		for (l = 0; l < MAX_LUNS; l++)
			qla2x00_lun_free(ha, t, l);

		TGT_Q(ha, t) = NULL;
		kfree(tq);
	}

	LEAVE(__func__);

	return;
}

/*
 * qla2x00_lun_alloc
 *	Allocate and initialize LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *	l = LUN number.
 *
 * Returns:
 *	NULL = failure
 *
 * Context:
 *	Kernel context.
 */
os_lun_t *
qla2x00_lun_alloc(scsi_qla_host_t *ha, uint16_t t, uint16_t l) 
{
	os_lun_t	*lq;

	ENTER(__func__);

	/*
	 * If SCSI addressing OK, allocate LUN queue.
	 */
	if (t >= MAX_TARGETS || 
		l >= MAX_LUNS || 
		TGT_Q(ha, t) == NULL) {

		DEBUG2(printk(KERN_INFO "%s(): tgt=%d, tgt_q= %p, lun=%d, "
				"instance=%ld **** FAILED exiting ****\n",
				__func__,
				t,
				TGT_Q(ha,t),
				l,
				ha->instance);)

		return (NULL);
	}

	lq = LUN_Q(ha, t, l);
	if (lq == NULL) {
		lq = kmalloc(sizeof(os_lun_t), GFP_ATOMIC);
		if (lq != NULL) {

			DEBUG5(printk("Alloc Lun %d @ %p \n",l,lq);)
			
			memset(lq, 0, sizeof(os_lun_t));
			LUN_Q(ha, t, l) = lq;
			/*
			 * The following lun queue initialization code
			 * must be duplicated in alloc_ioctl_mem function
			 * for ioctl_lq.
			 */
			lq->q_state = LUN_STATE_READY;
			spin_lock_init(&lq->q_lock);

			if (ha->flags.failover_enabled) {
				lq->fo_info = kmalloc(sizeof(struct fo_information), GFP_ATOMIC);
				if (lq->fo_info) {
					memset(lq->fo_info, 0, sizeof(struct fo_information));
				} else {
					printk(KERN_WARNING "%s failed to"
						" alloc fo_retry_cnt buffer\n",
						__func__); 
				}
			}
		} else {
			/*EMPTY*/
			DEBUG2(printk(KERN_INFO "%s(): Failed to allocate lun %d ***\n",
					__func__,
					l);)
			printk(KERN_WARNING
				"scsi(%ld): Memory Allocation failed - FCLUN\n",
				ha->host_no);
			ha->mem_err++;
		}
	}

	if (lq == NULL) {
		DEBUG2(printk(KERN_INFO "%s(): **** FAILED exiting ****\n", __func__);)
	} else {
		LEAVE(__func__);
	}

	return (lq);
}

/*
 * qla2x00_lun_free
 *	Frees LUN queue.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI target number.
 *
 * Context:
 *	Kernel context.
 */
static void
qla2x00_lun_free(scsi_qla_host_t *ha, uint16_t t, uint16_t l) 
{
	os_lun_t	*lq;

	ENTER(__func__);

	/*
	 * If SCSI addressing OK, allocate TGT queue and lock.
	 */
	if (t >= MAX_TARGETS || l >= MAX_LUNS) {
		DEBUG2(printk(KERN_INFO "%s(): **** FAILED exiting ****", __func__);)

		return;
	}

	if (TGT_Q(ha, t) != NULL && 
		(lq = LUN_Q(ha, t, l)) != NULL) {

		LUN_Q(ha, t, l) = NULL;
		if (lq->fo_info)
			kfree(lq->fo_info);
		kfree(lq);

		DEBUG3(printk("Dealloc lun @ %p -- deleted\n", lq);)
	}

	LEAVE(__func__);

	return;
}

#if  defined(ISP2300)
/*
 * qla2x00_process_response_queue_in_zio_mode
 *	Process response queue completion as fast as possible
 *	to achieve Zero Interrupt Opertions-ZIO
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
static inline void
qla2x00_process_response_queue_in_zio_mode(scsi_qla_host_t *ha)
{
	unsigned long flags;

     /* Check for completed commands in response queue. */
        if (ha->flags.process_response_queue){
                if (ha->flags.online) {
                        /* Check for unprocessed commands in response queue. */
                        if (ha->response_ring_ptr->signature 
					!= RESPONSE_PROCESSED){
                                spin_lock_irqsave(&ha->hardware_lock,flags);
                                qla2x00_process_response_queue(ha);
                                spin_unlock_irqrestore(&ha->hardware_lock
						, flags);
			}
                }
        }

}
#endif

/*
 * qla2x00_next
 *	Retrieve and process next job in the LUN queue.
 *
 * Input:
 *	tq = SCSI target queue pointer.
 *	lq = SCSI LUN queue pointer.
 *	TGT_LOCK must be already obtained.
 *
 * Output:
 *	Releases TGT_LOCK upon exit.
 *
 * Context:
 *	Kernel/Interrupt context.
 * 
 * Note: This routine will always try to start I/O from visible HBA.
 */
void
qla2x00_next(scsi_qla_host_t *vis_ha) 
{
	scsi_qla_host_t *dest_ha = NULL;
	fc_port_t	*fcport;
	srb_t		*sp;
	int		rval;
	unsigned long   flags;

	ENTER(__func__);

	spin_lock_irqsave(&vis_ha->list_lock, flags);
	while (!list_empty(&vis_ha->pending_queue)) {
		sp = list_entry(vis_ha->pending_queue.next, srb_t, list);

		/* Select the path to route the I/O */
		if (vis_ha->flags.failover_enabled &&
		    !(sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) &&
		    !(sp->lun_queue->fclun->fcport->flags 
			& FC_FAILOVER_DISABLE) &&
		    !(sp->lun_queue->fclun->flags & FC_VISIBLE_LUN)) {
			qla2x00_cfg_select_route(sp);
		} else {
		 	sp->fclun = sp->lun_queue->fclun;
		}
		fcport = sp->fclun->fcport;
		dest_ha = fcport->ha;

		/* Check if command can be started, exit if not. */
	    	if (!(sp->flags & SRB_TAPE) && LOOP_TRANSITION(dest_ha)) {
			break;
		}

		__del_from_pending_queue(vis_ha, sp);

	

		/* If device is dead then send request back to OS */
		if ((dest_ha->flags.link_down_error_enable &&
			atomic_read(&fcport->state) == FC_DEVICE_DEAD)) {

			CMD_RESULT(sp->cmd) = DID_NO_CONNECT << 16;

			if (atomic_read(&dest_ha->loop_state) == LOOP_DEAD) {
				sp->err_id = SRB_ERR_LOOP;
			} else {
				sp->err_id = SRB_ERR_PORT;
			}

			DEBUG3(printk("scsi(%ld): loop/port is down - "
					"pid=%ld, sp=%p err_id %d, loopid=0x%x queued "
					"to dest HBA scsi%ld.\n", 
					dest_ha->host_no,
					sp->cmd->serial_number,
					sp,
					sp->err_id,
					fcport->loop_id,
					dest_ha->host_no);)
			/* 
			 * Initiate a failover - done routine will initiate.
			 */
			__add_to_done_queue(vis_ha, sp);

			continue;
		}

		/*
		 * SCSI Kluge: Whenever, we need to wait for an event such as
		 * loop down (i.e. loop_down_timer ) or port down (i.e.  LUN
		 * request qeueue is suspended) then we will recycle new
		 * commands back to the SCSI layer.  We do this because this is
		 * normally a temporary condition and we don't want the
		 * mid-level scsi.c driver to get upset and start aborting
		 * commands.  The timeout value is extracted from the command
		 * minus 1-second and put on a retry queue (watchdog). Once the
		 * command timeout it is returned to the mid-level with a BUSY
		 * status, so the mid-level will retry it. This process
		 * continues until the LOOP DOWN time expires or the condition
		 * goes away.
		 */
	 	if (!(sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) &&
		    (atomic_read(&fcport->state) != FC_ONLINE ||
		    test_bit(CFG_FAILOVER, &dest_ha->cfg_flags) || 
			test_bit(ABORT_ISP_ACTIVE, &dest_ha->dpc_flags) ||
			(atomic_read(&dest_ha->loop_state) != LOOP_READY)
			|| (sp->flags & SRB_FAILOVER))) {

			DEBUG3(printk("scsi(%ld): port=(0x%x) retry_q(%d) loop "
					"state = %d, loop counter = 0x%x"
					" dpc flags = 0x%lx\n",
					dest_ha->host_no,
					fcport->loop_id,
					atomic_read(&fcport->state),
					atomic_read(&dest_ha->loop_state),
					atomic_read(&dest_ha->loop_down_timer),
					dest_ha->dpc_flags);)

			qla2x00_extend_timeout(sp->cmd, EXTEND_CMD_TIMEOUT);
			__add_to_retry_queue(vis_ha, sp);
			continue;
		} 

		/*
		 * if this request's lun is suspended then put the request on
		 * the  scsi_retry queue. 
		 */
	 	if (!(sp->flags & (SRB_IOCTL | SRB_TAPE | SRB_FDMI_CMD)) &&
			sp->lun_queue->q_state == LUN_STATE_WAIT) {
			DEBUG3(printk("%s(): lun wait state - pid=%ld, "
					"opcode=%d, allowed=%d, retries=%d\n",
					__func__,
					sp->cmd->serial_number,
					sp->cmd->cmnd[0],
					sp->cmd->allowed,
					sp->cmd->retries);)
				
			__add_to_scsi_retry_queue(vis_ha, sp);
			continue;
		}

		sp->lun_queue->io_cnt++;

		/* Release target queue lock */
		spin_unlock_irqrestore(&vis_ha->list_lock, flags);
#if defined(ISP2300)
		if (check_24xx_or_54xx_device_ids(dest_ha) || 
			check_25xx_device_ids(dest_ha)) {
				rval = qla24xx_start_scsi(sp);
		} else 
#endif
		{
			if (dest_ha->flags.enable_64bit_addressing)
				rval = qla2x00_64bit_start_scsi(sp);
			else
				rval = qla2x00_32bit_start_scsi(sp);
		}

		spin_lock_irqsave(&vis_ha->list_lock, flags);

		if (rval != QLA2X00_SUCCESS) {
			/* Place request back on top of device queue */
			/* add to the top of queue */
			__add_to_pending_queue_head(vis_ha, sp);

			sp->lun_queue->io_cnt--;
			break;
		}
	}
	spin_unlock_irqrestore(&vis_ha->list_lock, flags);

#if  defined(ISP2300)
	/* Process response_queue if ZIO support is enabled*/ 
	qla2x00_process_response_queue_in_zio_mode(vis_ha);

	if (dest_ha && dest_ha->flags.failover_enabled)
		qla2x00_process_response_queue_in_zio_mode(dest_ha);
#endif	


	LEAVE(__func__);
}

/*
 * qla2x00_is_wwn_zero
 *
 * Input:
 *      wwn = Pointer to WW name to check
 *
 * Returns:
 *      TRUE if name is 0 else FALSE
 *
 * Context:
 *      Kernel context.
 */
static inline int
qla2x00_is_wwn_zero(uint8_t *wwn) 
{
	int cnt;

	/* Check for zero node name */
	for (cnt = 0; cnt < WWN_SIZE ; cnt++, wwn++) {
		if (*wwn != 0)
			break;
	}
	/* if zero return TRUE */
	if (cnt == WWN_SIZE)
		return (TRUE);
	else
		return (FALSE);
}

/*
 * qla2x00_get_lun_mask_from_config
 *      Get lun mask from the configuration parameters.
 *      Bit order is little endian.
 *
 * Input:
 * ha  -- Host adapter
 * tgt  -- target/device number
 * port -- pointer to port
 */
void
qla2x00_get_lun_mask_from_config(scsi_qla_host_t *ha, 
		fc_port_t *port, uint16_t tgt, uint16_t dev_no) 
{
	char		propbuf[60]; /* size of search string */
	int		rval, lun, l;
	lun_bit_mask_t	lun_mask, *mask_ptr = &lun_mask;

	/* Get "target-N-device-N-lun-mask" as a 256 bit lun_mask*/
	PERSIST_STRING("scsi-qla%ld-tgt-%d-di-%d-lun-disabled", "%ld-%d-%d-d");

	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *)&lun_mask,
			sizeof(lun_bit_mask_t));
	if (rval != -1 && 
		(rval == sizeof(lun_bit_mask_t))) {

		DEBUG3(printk("%s(%ld): lun mask for port %p from file:\n",
				__func__,
				ha->host_no, 
				port);)
		DEBUG3(qla2x00_dump_buffer((uint8_t *)&port->lun_mask,
					sizeof(lun_bit_mask_t));)

		for (lun = 8 * sizeof(lun_bit_mask_t) - 1, l = 0; 
			lun >= 0; 
			lun--, l++) {

			if (EXT_IS_LUN_BIT_SET(mask_ptr, lun))
				EXT_SET_LUN_BIT((&port->lun_mask),l);
			else
				EXT_CLR_LUN_BIT((&port->lun_mask),l);
		}

		DEBUG3(printk("%s(%ld): returning lun mask for port "
				"%02x%02x%02x%02x%02x%02x%02x%02x:\n",
				__func__,
				ha->host_no, 
				port->port_name[0], port->port_name[1],
				port->port_name[2], port->port_name[3],
				port->port_name[4], port->port_name[5],
				port->port_name[6], port->port_name[7]);)
		DEBUG3(qla2x00_dump_buffer((uint8_t *)&port->lun_mask,
				sizeof(lun_bit_mask_t));)
	}
}

/*
 * qla2x00_bstr_to_hex
 *	Convert hex byte string to number.
 *
 * Input:
 *	s = byte string pointer.
 *	bp = byte pointer for number.
 *	size = number of bytes.
 *
 * Context:
 *	Kernel/Interrupt context.
 */
static int
qla2x00_bstr_to_hex(char *s, uint8_t *bp, int size) 
{
	int		cnt;
	uint8_t		n;

	ENTER(__func__);

	for (cnt = 0; *s != '\0' && cnt / 2 < size; cnt++) {
		if (*s >= 'A' && *s <= 'F') {
			n = (*s++ - 'A') + 10;
		} else if (*s >= 'a' && *s <= 'f') {
			n = (*s++ - 'a') + 10;
		} else if (*s >= '0' && *s <= '9') {
			n = *s++ - '0';
		} else {
			cnt = 0;
			break;
		}

		if (cnt & BIT_0)
			*bp++ |= n;
		else
			*bp = n << 4;
	}
	/* fixme(dg) Need to swap data little endian */

	LEAVE(__func__);

	return (cnt / 2);
}

/*
 * qla2x00_get_prop_xstr
 *      Get a string property value for the specified property name and
 *      convert from the property string found in the configuration file,
 *      which are ASCII characters representing nibbles, 2 characters represent
 *      the hexdecimal value for a byte in the byte array.
 *      The byte array is initialized to zero.
 *      The resulting converted value is in big endian format (MSB at byte0).
 *
 * Input:
 *      ha = adapter state pointer.
 *      propname = property name pointer.
 *      propval  = pointer where to store converted property val.
 *      size = max or expected size of 'propval' array.
 *
 * Returns:
 *      0 = empty value string or invalid character in string
 *      >0 = count of characters converted
 *      -1 = property not found
 *
 * Context:
 *      Kernel context.
 */
int
qla2x00_get_prop_xstr(scsi_qla_host_t *ha, 
		char *propname, uint8_t *propval, int size) 
{
	char		*propstr;
	int		rval = -1;
	static char	buf[LINESIZE];

	ENTER(__func__);

	/* Get the requested property string */
	rval = qla2x00_find_propname(ha, propname, buf, ha->cmdline, size*2);
	DEBUG3(printk("%s(): Ret rval from find propname = %d\n",
			__func__,
			rval);)

	propstr = &buf[0];
	if (*propstr == '=')
		propstr++;   /* ignore equal sign */

	if (rval == 0) {  /* not found */
		LEAVE(__func__);
		return (-1);
	}

	rval = qla2x00_bstr_to_hex(propstr, (uint8_t *)propval, size);
	if (rval == 0) {
		/* Invalid character in value string */
		printk(KERN_INFO
			"%s(): %s Invalid hex string for property\n",
			__func__,
			propname);
		printk(KERN_INFO
			" Invalid string - %s\n", 
			propstr);
	}

	LEAVE(__func__);

	return (rval);
}

/*
 * qla2x00_chg_endian
 *	Change endianess of byte array.
 *
 * Input:
 *	buf = array pointer.
 *	size = size of array in bytes.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_chg_endian(uint8_t buf[], size_t size) 
{
	uint8_t byte;
	size_t cnt1;
	size_t cnt;

	cnt1 = size - 1;
	for (cnt = 0; cnt < size / 2; cnt++) {
		byte = buf[cnt1];
		buf[cnt1] = buf[cnt];
		buf[cnt] = byte;
		cnt1--;
	}
}

/*
 * qla2x00_allocate_sp_pool
 * 	 This routine is called during initialization to allocate
 *  	 memory for local srb_t.
 *
 * Input:
 *	 ha   = adapter block pointer.
 *
 * Context:
 *      Kernel context.
 * 
 * Note: Sets the ref_count for non Null sp to one.
 */
uint8_t
qla2x00_allocate_sp_pool(scsi_qla_host_t *ha) 
{
	srb_t   *sp;
	int  i;
	uint8_t      status = QL_STATUS_SUCCESS;

	ENTER(__func__);
	
	DEBUG4(printk("%s(): Entered.\n", __func__);)

	/*
	 * Note: Need to alloacte each SRB as Kernel 2.4 seems to have error
	 * when allocating large amount of memory.
	 */
	/*
	 * FIXME(dg) - Need to allocated the SRBs by pages instead of each SRB
	 * object.
	 */
	/* INIT_LIST_HEAD(&ha->free_queue); */
	ha->srb_alloc_cnt = 0;
	for (i=0; i < max_srbs; i++) {
		sp =  kmalloc(sizeof(srb_t), GFP_KERNEL);
		if (sp == NULL) {
			printk("%s(%ld): failed to allocate memory, "
				"count = %d\n", 
				__func__,
				ha->host_no, 
				i);
		} else {
			memset(sp, 0, sizeof(srb_t));
			__add_to_free_queue (ha, sp);
			sp->magic = SRB_MAGIC;
			sp->ref_num = ha->srb_alloc_cnt;
			sp->host_no = ha->host_no;
			ha->srb_alloc_cnt++;
			atomic_set(&sp->ref_count, 0);
		}
	}
	/*
	 * If we fail to allocte memory return an error
	 */
	if (ha->srb_alloc_cnt == 0)
		status = QL_STATUS_ERROR;

	printk(KERN_DEBUG
		"scsi(%ld): Allocated %d SRB(s).\n",
		ha->host_no,
		ha->srb_alloc_cnt);

	LEAVE(__func__);

	return( status );
}

/*
 *  This routine frees all adapter allocated memory.
 *  
 */
void
qla2x00_free_sp_pool( scsi_qla_host_t *ha) 
{
	struct list_head *list, *temp;
	srb_t         *sp;
	int cnt_free_srbs = 0;

	list_for_each_safe(list, temp, &ha->free_queue) {
		sp = list_entry(list, srb_t, list);
		/* Remove srb from LUN queue. */
		__del_from_free_queue(ha,sp);
		kfree(sp);
		cnt_free_srbs++;
	}
	INIT_LIST_HEAD(&ha->free_queue);

	if (cnt_free_srbs != ha->srb_alloc_cnt ) {
		DEBUG(printk("qla2x00 (%ld): Did not free all srbs,"
				" Free count = %d, Alloc Count = %d\n",
				ha->host_no, 
				cnt_free_srbs, 
				ha->srb_alloc_cnt);)
		printk(KERN_INFO
			"qla2x00 (%ld): Did not free all srbs, Free count = "
			"%d, Alloc Count = %d\n",
			ha->host_no, 
			cnt_free_srbs, 
			ha->srb_alloc_cnt);
	}
}

/* Flash support routines */

/**
 * qla2x00_flash_enable() - Setup flash for reading and writing.
 * @ha: HA context
 */
STATIC void
qla2x00_flash_enable(scsi_qla_host_t *ha)
{
	uint16_t	data;
	device_reg_t	*reg = ha->iobase;

	data = RD_REG_WORD(&reg->ctrl_status);
	data |= CSR_FLASH_ENABLE;
	WRT_REG_WORD(&reg->ctrl_status, data);
	PCI_POSTING(&reg->ctrl_status);
}

/**
 * qla2x00_flash_disable() - Disable flash and allow RISC to run.
 * @ha: HA context
 */
STATIC void
qla2x00_flash_disable(scsi_qla_host_t *ha)
{
	uint16_t	data;
	device_reg_t	*reg = ha->iobase;

	data = RD_REG_WORD(&reg->ctrl_status);
	data &= ~(CSR_FLASH_ENABLE);
	WRT_REG_WORD(&reg->ctrl_status, data);
	PCI_POSTING(&reg->ctrl_status);
}

/**
 * qla2x00_read_flash_byte() - Reads a byte from flash
 * @ha: HA context
 * @addr: Address in flash to read
 *
 * A word is read from the chip, but, only the lower byte is valid.
 *
 * Returns the byte read from flash @addr.
 */
STATIC uint8_t
qla2x00_read_flash_byte(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t	data;
	uint16_t	bank_select;
	device_reg_t	*reg = ha->iobase;

	bank_select = RD_REG_WORD(&reg->ctrl_status);

#if defined(ISP2300) 
	if (ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		PCI_POSTING(&reg->ctrl_status);

		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		data = RD_REG_WORD(&reg->flash_data);
		return ((uint8_t)data);
	}
#endif
	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
	} else if (((addr & BIT_16) == 0) &&
			(bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
	}
	PCI_POSTING(&reg->ctrl_status);

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO
	 * in 133 MHZ slot. */
	if (ha->pio_address) {
		uint16_t data2;

		reg = (device_reg_t *)ha->pio_address;
		WRT_REG_WORD_PIO(&reg->flash_address, (uint16_t)addr);
		do {
			data = RD_REG_WORD_PIO(
			    	(unsigned long)(&reg->flash_data));
			barrier();
			cpu_relax();
			data2 = RD_REG_WORD_PIO(
			    	 (unsigned long)(&reg->flash_data));
		} while (data != data2);
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		data = qla2x00_debounce_register(&reg->flash_data);
	}
	return ((uint8_t)data);
}

/**
 * qla2x00_write_flash_byte() - Write a byte to flash
 * @ha: HA context
 * @addr: Address in flash to write
 * @data: Data to write
 */
STATIC void
qla2x00_write_flash_byte(scsi_qla_host_t *ha, uint32_t addr, uint8_t data)
{
	uint16_t	bank_select;
	device_reg_t	*reg = ha->iobase;

	bank_select = RD_REG_WORD(&reg->ctrl_status);

#if defined(ISP2300) 
	if (ha->device_id == QLA2322_DEVICE_ID ||
	    ha->device_id == QLA6322_DEVICE_ID) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		PCI_POSTING(&reg->ctrl_status);

		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		PCI_POSTING(&reg->ctrl_status);
		WRT_REG_WORD(&reg->flash_data, (uint16_t)data);
		PCI_POSTING(&reg->ctrl_status);
		return;
	}
#endif
	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
	} else if (((addr & BIT_16) == 0) &&
			(bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
	}
	PCI_POSTING(&reg->ctrl_status);

	/* The ISP2312 v2 chip cannot access the FLASH registers via MMIO
	 * in 133 MHZ slot. */
	if (ha->pio_address) {
		reg = (device_reg_t *)ha->pio_address;
		WRT_REG_WORD_PIO(&reg->flash_address, (uint16_t)addr);
		WRT_REG_WORD_PIO(&reg->flash_data, (uint16_t)data);
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		PCI_POSTING(&reg->flash_data);
		WRT_REG_WORD(&reg->flash_data, (uint16_t)data);
		PCI_POSTING(&reg->flash_data);
	}
}

/**
 * qla2x00_poll_flash() - Polls flash for completion.
 * @ha: HA context
 * @addr: Address in flash to poll
 * @poll_data: Data to be polled
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * This function polls the device until bit 7 of what is read matches data
 * bit 7 or until data bit 5 becomes a 1.  If that hapens, the flash ROM timed
 * out (a fatal error).  The flash book recommeds reading bit 7 again after
 * reading bit 5 as a 1.
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint8_t
qla2x00_poll_flash(scsi_qla_host_t *ha, uint32_t addr, uint8_t poll_data,
    uint8_t man_id, uint8_t flash_id)
{
	uint8_t		status;
	uint8_t		flash_data;
	uint32_t	cnt;

	status = 1;
	/* Wait for 30 seconds for command to finish. */
	poll_data &= BIT_7;
	for (cnt = 3000000; cnt; cnt--) {
		flash_data = qla2x00_read_flash_byte(ha, addr);
		if ((flash_data & BIT_7) == poll_data) {
			status = 0;
			break;
		}
		if (man_id != 0x40 && man_id != 0xda) {
			if ((flash_data & BIT_5) && cnt > 2)
				cnt = 2;
		}
		udelay(10);
		barrier();
	}
	return (status);
}

/**
 * qla2x00_program_flash_address() - Programs a flash address
 * @ha: HA context
 * @addr: Address in flash to program
 * @data: Data to be written in flash
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint8_t
qla2x00_program_flash_address(scsi_qla_host_t *ha, uint32_t addr, uint8_t data,
    uint8_t man_id, uint8_t flash_id)
{
	/* Write Program Command Sequence */
	if (qla2x00_is_guadalupe(ha)) {
		// First send a sequence of commands to the chip
		qla2x00_write_flash_byte(ha, 0xAAA,0xAA);
		qla2x00_write_flash_byte(ha, 0x555,0x55);
		qla2x00_write_flash_byte(ha, 0xAAA,0xA0);

		// Next, send the addr, and data
		qla2x00_write_flash_byte(ha, addr, data);
	} else {
		if (man_id == 0xda && flash_id == 0xc1) {
			qla2x00_write_flash_byte(ha, addr, data);
			if (addr & 0x7e)
				return 0;
		} else {
			qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
			qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
			qla2x00_write_flash_byte(ha, 0x5555, 0xa0);
			qla2x00_write_flash_byte(ha, addr, data);
		}
	}

	udelay(150);

	/* Wait for write to complete. */
	return (qla2x00_poll_flash(ha, addr, data, man_id, flash_id));
}

/**
 * qla2x00_erase_flash() - Erase the flash.
 * @ha: HA context
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint8_t
qla2x00_erase_flash(scsi_qla_host_t *ha, uint8_t man_id,
    uint8_t flash_id)
{
	if (qla2x00_is_guadalupe(ha)) {
		qla2x00_write_flash_byte(ha, 0xAAA,0xAA);
		qla2x00_write_flash_byte(ha, 0x555,0x55);
		qla2x00_write_flash_byte(ha, 0xAAA,0x80);
		qla2x00_write_flash_byte(ha, 0xAAA,0xAA);
		qla2x00_write_flash_byte(ha, 0x555,0x55);
		qla2x00_write_flash_byte(ha, 0xAAA, 0x10);

	} else {
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x80);
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x10);
	}

	udelay(150);

	/* Wait for erase to complete. */
	return (qla2x00_poll_flash(ha, 0x00, 0x80, man_id, flash_id));
}

/**
 * qla2x00_erase_flash_sector() - Erase a flash sector.
 * @ha: HA context
 * @addr: Flash sector to erase
 * @sec_mask: Sector address mask
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint8_t
qla2x00_erase_flash_sector(scsi_qla_host_t *ha, uint32_t addr,
    uint32_t sec_mask, uint8_t man_id, uint8_t flash_id)
{
	/* Individual Sector Erase Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x80);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	if (man_id == 0x1f && flash_id == 0x13)
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x10);
        else
                qla2x00_write_flash_byte(ha, addr & sec_mask, 0x30);

	udelay(150);

	/* Wait for erase to complete. */
	return (qla2x00_poll_flash(ha, addr, 0x80, man_id, flash_id));
}

/**
 * qla2x00_get_flash_manufacturer() - Read manufacturer info from flash chip.
 * @ha: HA context
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 */
STATIC void
qla2x00_get_flash_manufacturer(scsi_qla_host_t *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x90);
	*man_id = qla2x00_read_flash_byte(ha, 0x0000);
	*flash_id = qla2x00_read_flash_byte(ha, 0x0001);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0xf0);
}
#if defined(ISP2300)
int
qla24xx_get_flash_version(scsi_qla_host_t *ha, uint8_t *ptmp_mem)
{
	int		ret = QLA2X00_SUCCESS;
	uint8_t		*bcode;
	uint8_t		code_type, last_image;
	uint8_t		i;
	uint32_t	*dcode;
	uint32_t	pcihdr, pcids;

	if (ptmp_mem == NULL) {
		/* error */
		return(QLA2X00_FUNCTION_FAILED);
	}

	dcode = (uint32_t *)ptmp_mem;

	/* Begin with first PCI expansion ROM header. */
	pcihdr = 0;
	last_image = 1;
	do {
		/* Verify PCI expansion ROM header. */
		qla24xx_read_flash_data(ha, dcode, pcihdr >> 2, 0x20);
		bcode = (uint8_t *)ptmp_mem + (pcihdr % 4);
		if (bcode[0x0] != 0x55 || bcode[0x1] != 0xaa) {
			/* No signature */
			DEBUG2(printk("scsi(%ld): No matching ROM signature.\n",
			    ha->host_no));
			ret = QLA2X00_FUNCTION_FAILED;
			break;
		}

		/* Locate PCI data structure. */
		pcids = pcihdr + ((bcode[0x19] << 8) | bcode[0x18]);

		qla24xx_read_flash_data(ha, dcode, pcids >> 2, 0x20);
		bcode = (uint8_t *)ptmp_mem + (pcihdr % 4);

		/* Validate signature of PCI data structure. */
		if (bcode[0x0] != 'P' || bcode[0x1] != 'C' ||
		    bcode[0x2] != 'I' || bcode[0x3] != 'R') {
			/* Incorrect header. */
			DEBUG2(printk("%s(): PCI data struct not found "
			    "pcir_adr=%x.\n",
			    __func__, pcids));
			ret = QLA2X00_FUNCTION_FAILED;
			break;
		}

		/* Read version */
		code_type = bcode[0x14];
		switch (code_type) {
		case ROM_CODE_TYPE_BIOS:
			/* Intel x86, PC-AT compatible. */
			set_bit(ROM_CODE_TYPE_BIOS, &ha->code_types);
			ha->bios_revision[0] = bcode[0x12];
			ha->bios_revision[1] = bcode[0x13];
			DEBUG3(printk("%s(): read BIOS %d.%d.\n", __func__,
			    ha->bios_revision[1], ha->bios_revision[0]));
			break;
		case ROM_CODE_TYPE_FCODE:
			/* Open Firmware standard for PCI (FCode). */
			set_bit(ROM_CODE_TYPE_FCODE, &ha->code_types);
			ha->fcode_revision[0] = bcode[0x12];
			ha->fcode_revision[1] = bcode[0x13];
			DEBUG9(printk("%s(): read FCODE %d.%d.\n", __func__,
			    ha->fcode_revision[1], ha->fcode_revision[0]);)
			break;
		case ROM_CODE_TYPE_EFI:
			/* Extensible Firmware Interface (EFI). */
			set_bit(ROM_CODE_TYPE_EFI, &ha->code_types);
			ha->efi_revision[0] = bcode[0x12];
			ha->efi_revision[1] = bcode[0x13];
			DEBUG3(printk("%s(): read EFI %d.%d.\n", __func__,
			    ha->efi_revision[1], ha->efi_revision[0]));
			break;
		default:
			DEBUG2(printk("%s(): Unrecognized code type %x at "
			    "pcids %x.\n", __func__, code_type, pcids));
			break;
		}

		last_image = bcode[0x15] & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((bcode[0x11] << 8) | bcode[0x10]) * 512;
	} while (!last_image);

	/* Read firmware image information. */
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));
	dcode = (uint32_t *)ptmp_mem;

	qla24xx_read_flash_data(ha, dcode, FA_RISC_CODE_ADDR + 4, 4);
	for (i = 0; i < 4; i++)
		dcode[i] = be32_to_cpu(dcode[i]);

	if ((dcode[0] == 0xffffffff && dcode[1] == 0xffffffff &&
	    dcode[2] == 0xffffffff && dcode[3] == 0xffffffff) ||
            (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
            dcode[3] == 0)) {
		DEBUG10(printk("%s(): Unrecognized fw revision at %x.\n",
		    __func__, FA_RISC_CODE_ADDR));
	} else {
		ha->fw_revision[0] = dcode[0];
		ha->fw_revision[1] = dcode[1];
		ha->fw_revision[2] = dcode[2];
		ha->fw_revision[3] = dcode[3];
	}

	return ret;
}

int
qla24xx_refresh_flash_version(scsi_qla_host_t *ha, uint8_t *ptmp_mem)
{
	unsigned long	flags;
	int		ret = 0;
	int		status;

	/* suspend targets */
	qla2x00_suspend_all_target(ha);

	/* wait for big hammer to complete if it fails */
	status = qla2x00_cmd_wait(ha);
	if (status)
		qla2x00_wait_for_hba_online(ha);

	/* Dont process mailbox cmd until flash
	 * operation is done.
	 */
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	qla2x00_disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (qla24xx_get_flash_version(ha, ptmp_mem)) {
		ret = QLA2X00_FUNCTION_FAILED;
		DEBUG9_10(printk( "%s: ERROR reading flash versions.\n",
		    __func__);)
	}
	spin_unlock_irqrestore(&ha->hardware_lock,flags);

	qla2x00_enable_intrs(ha);
	clear_bit(MBX_UPDATE_FLASH_ACTIVE,
	    &ha->mbx_cmd_flags);
	qla2x00_unsuspend_all_target(ha);

	return (ret);
}
#endif

/**
 * qla2x00_get_flash_version() - Read version information from flash.
 * @ha: HA context
 *
 * Returns QL_STATUS_SUCCESS on successful retrieval of flash version.
 */

uint16_t
qla2x00_get_flash_version(scsi_qla_host_t *ha)
{
	uint8_t		code_type, last_image;
	uint16_t	ret = QL_STATUS_SUCCESS;
	uint32_t	pcihdr, pcids;

#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_get_flash_version(ha,
		    (uint8_t *)ha->request_ring);
#endif

	qla2x00_flash_enable(ha);

	/* Begin with first PCI expansion ROM header. */
	pcihdr = 0;
	last_image = 1;
	do {
		/* Verify PCI expansion ROM header. */
		if (qla2x00_read_flash_byte(ha, pcihdr) != 0x55 ||
		    qla2x00_read_flash_byte(ha, pcihdr + 0x01) != 0xaa) {
			/* No signature */
			DEBUG2(printk("scsi(%ld): No matching ROM signature.\n",
			    ha->host_no));
			ret = QL_STATUS_ERROR;
			break;
		}

		/* Locate PCI data structure. */
		pcids = pcihdr +
		    ((qla2x00_read_flash_byte(ha, pcihdr + 0x19) << 8) |
			qla2x00_read_flash_byte(ha, pcihdr + 0x18));

		/* Validate signature of PCI data structure. */
		if (qla2x00_read_flash_byte(ha, pcids) != 'P' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x1) != 'C' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x2) != 'I' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x3) != 'R') {
			/* Incorrect header. */
			DEBUG2(printk("%s(): PCI data struct not found "
			    "pcir_adr=%x.\n",
			    __func__, pcids));
			ret = QL_STATUS_ERROR;
			break;
		}

		/* Read version */
		code_type = qla2x00_read_flash_byte(ha, pcids + 0x14);
		switch (code_type) {
		case ROM_CODE_TYPE_BIOS:
			/* Intel x86, PC-AT compatible. */
			set_bit(ROM_CODE_TYPE_BIOS, &ha->code_types);
			ha->bios_revision[0] =
			    qla2x00_read_flash_byte(ha, pcids + 0x12);
			ha->bios_revision[1] =
			    qla2x00_read_flash_byte(ha, pcids + 0x13);
			DEBUG3(printk("%s(): read BIOS %d.%d.\n", __func__,
			    ha->bios_revision[1], ha->bios_revision[0]));
			break;
		case ROM_CODE_TYPE_FCODE:
			/* Open Firmware standard for PCI (FCode). */
			/* Eeeewww... */
			if (qla2x00_get_fcode_version(ha, pcids) ==
			    QL_STATUS_SUCCESS)
				set_bit(ROM_CODE_TYPE_FCODE, &ha->code_types);
			break;
		case ROM_CODE_TYPE_EFI:
			/* Extensible Firmware Interface (EFI). */
			set_bit(ROM_CODE_TYPE_EFI, &ha->code_types);
			ha->efi_revision[0] =
			    qla2x00_read_flash_byte(ha, pcids + 0x12);
			ha->efi_revision[1] =
			    qla2x00_read_flash_byte(ha, pcids + 0x13);
			DEBUG3(printk("%s(): read EFI %d.%d.\n", __func__,
			    ha->efi_revision[1], ha->efi_revision[0]));
			break;
		default:
			DEBUG2(printk("%s(): Unrecognized code type %x at "
			    "pcids %x.\n", __func__, code_type, pcids));
			break;
		}

		last_image = qla2x00_read_flash_byte(ha, pcids + 0x15) & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((qla2x00_read_flash_byte(ha, pcids + 0x11) << 8) |
		    qla2x00_read_flash_byte(ha, pcids + 0x10)) * 512;
	} while (!last_image);

#if defined(ISP2300)
	if (ha->device_id == QLA2322_DEVICE_ID) {
		uint8_t		dbyte[8];
		uint8_t		length;
		uint16_t	*dcode;

		length = sizeof(dbyte);

		/* Read firmware image information. */
		memset(ha->fw_revision, 0, sizeof(ha->fw_revision));
		memset(dbyte, 0, sizeof(dbyte));
		dcode = (uint16_t *)dbyte;

		qla2x00_read_flash_data(ha, dbyte, FA_RISC_CODE_ADDR*4 + 10,
		    length);
		DEBUG9(printk("%s(%ld): dumping fw ver from flash:\n",
		    __func__, ha->host_no);)
		DEBUG9(qla2x00_dump_buffer((uint8_t *)dbyte, length);)

		if ((dcode[0] == 0xffff && dcode[1] == 0xffff &&
		    dcode[2] == 0xffff && dcode[3] == 0xffff) ||
		    (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
		    dcode[3] == 0)) {
			DEBUG10(printk(
			    "%s(): Unrecognized fw revision at %x.\n",
			    __func__, FA_RISC_CODE_ADDR*4));
		} else {
			/* values are in big endian */
			ha->fw_revision[0] = dbyte[0] << 16 | dbyte[1];
			ha->fw_revision[1] = dbyte[2] << 16 | dbyte[3];
			ha->fw_revision[2] = dbyte[4] << 16 | dbyte[5];
		}
	}
#endif

	qla2x00_flash_disable(ha);

	return (ret);
}

/**
 * qla2x00_get_fcode_version() - Determine an FCODE image's version.
 * @ha: HA context
 * @pcids: Pointer to the FCODE PCI data structure
 *
 * The process of retrieving the FCODE version information is at best
 * described as interesting.
 *
 * Within the first 100h bytes of the image an ASCII string is present
 * which contains several pieces of information including the FCODE
 * version.  Unfortunately it seems the only reliable way to retrieve
 * the version is by scanning for another sentinel within the string,
 * the FCODE build date:
 *
 *	... 2.00.02 10/17/02 ...
 *
 * Returns QL_STATUS_SUCCESS on successful retrieval of version.
 */
static uint16_t
qla2x00_get_fcode_version(scsi_qla_host_t *ha, uint32_t pcids)
{
	uint16_t	ret = QL_STATUS_ERROR;
	uint32_t	istart, iend, iter, vend;
	uint8_t		do_next, rbyte, *vbyte;

	memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));

	/* Skip the PCI data structure. */
	istart = pcids +
	    ((qla2x00_read_flash_byte(ha, pcids + 0x0B) << 8) |
		qla2x00_read_flash_byte(ha, pcids + 0x0A));
	iend = istart + 0x100;
	do {
		/* Scan for the sentinel date string...eeewww. */
		do_next = 0;
		iter = istart;
		while ((iter < iend) && !do_next) {
			iter++;
			if (qla2x00_read_flash_byte(ha, iter) == '/') {
				if (qla2x00_read_flash_byte(ha, iter + 2) ==
				    '/')
					do_next++;
				else if (qla2x00_read_flash_byte(ha,
				    iter + 3) == '/')
					do_next++;
			}
		}
		if (!do_next)
			break;

		/* Backtrack to previous ' ' (space). */
		do_next = 0;
		while ((iter > istart) && !do_next) {
			iter--;
			if (qla2x00_read_flash_byte(ha, iter) == ' ')
				do_next++;
		}
		if (!do_next)
			break;

		/*
		 * Mark end of version tag, and find previous ' ' (space) or
		 * string length (recent FCODE images -- major hack ahead!!!).
		 */
		vend = iter - 1;
		do_next = 0;
		while ((iter > istart) && !do_next) {
			iter--;
			rbyte = qla2x00_read_flash_byte(ha, iter);
			if (rbyte == ' ' || rbyte == 0xd || rbyte == 0x10)
				do_next++;
		}
		if (!do_next)
			break;

		/* Mark beginning of version tag, and copy data. */
		iter++;
		if ((vend - iter) &&
		    ((vend - iter) < sizeof(ha->fcode_revision))) {
			vbyte = ha->fcode_revision;
			while (iter <= vend) {
				*vbyte++ = qla2x00_read_flash_byte(ha, iter);
				iter++;
			}
			ret = QL_STATUS_SUCCESS;	
		}
	} while (0);

	return ret;
}

#if defined(NOT_USED_FUNCTION)
/**
 * qla2x00_get_flash_image() - Read image from flash chip.
 * @ha: HA context
 * @image: Buffer to receive flash image
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint16_t
qla2x00_get_flash_image(scsi_qla_host_t *ha, uint8_t *image)
{
	uint32_t	addr;
	uint32_t	midpoint;
	uint8_t		*data;
	device_reg_t	*reg = ha->iobase;

	midpoint = FLASH_IMAGE_SIZE / 2;

	qla2x00_flash_enable(ha);
	WRT_REG_WORD(&reg->nvram, 0);
	PCI_POSTING(&reg->nvram);
	for (addr = 0, data = image; addr < FLASH_IMAGE_SIZE; addr++, data++) {
		if (addr == midpoint) {
			WRT_REG_WORD(&reg->nvram, NV_SELECT);
			PCI_POSTING(&reg->nvram);
		}

		*data = qla2x00_read_flash_byte(ha, addr);
	}
	qla2x00_flash_disable(ha);

	return (0);
}
#endif

/* qla2x00_cmd_wait
*	Stall driver until all outstanding commands are returned.
*
* Input:
*	ha = adapter state pointer.
*
* Return;
*  0 -- Done
*  1 -- cmds still outstanding
*
* Context:
*  This routine must be called without hardware_lock held.
*/
STATIC uint8_t
qla2x00_cmd_wait(scsi_qla_host_t *ha) 
{
	uint16_t index = 0;
	uint8_t status = 0;
    	int wait_cnt = 30; 
	unsigned long  cpu_flags;

	DEBUG(printk("%s(%ld): Started\n",__func__,ha->host_no);)
       	while (wait_cnt) {
	      /* Find a command that hasn't completed. */
	      for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		    spin_lock_irqsave(&ha->hardware_lock, cpu_flags);  
		    if (ha->outstanding_cmds[index] != NULL){
			    spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags); 
			    break;
		    }
		    spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);  
              }
	      /* if No Commands are pending wait is complete */                
	      if (index == MAX_OUTSTANDING_COMMANDS ) {
			break;
              }

              /* if we timed out on waiting for commands to come back
                 Reset the ISP */
              wait_cnt--;
	      if( wait_cnt == 0 ) {
		   set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		   status = 1;
	           DEBUG(printk("%s(%ld): ISP abort - handle %d\n",
		       __func__, ha->host_no, index);)
	      }
              else {
	      	   /* sleep a second */
	          set_current_state(TASK_UNINTERRUPTIBLE);
	      	  schedule_timeout(HZ);
             }
        } /* End of While (wait_cnt) */

	DEBUG(printk("%s(%ld): Done waiting on commands - ind=%d\n",
	    	__func__,ha->host_no,index);)
        return status ;
}
#if defined(ISP2300)

void
qla24xx_get_flash_manufacturer(scsi_qla_host_t *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	uint32_t ids;

	ids = qla24xx_read_flash_dword(ha, flash_data_to_access_addr(0xD03AB));
	*man_id = LSB(ids);
	*flash_id = MSB(ids);
}

int
qla24xx_write_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	int ret;
	uint32_t liter;
	uint32_t sec_mask, rest_addr, conf_addr;
	uint32_t fdata;
	uint8_t	man_id, flash_id;
	struct device_reg_24xx *reg = (struct device_reg_24xx *)ha->iobase;

	ret = QLA2X00_SUCCESS;

	qla24xx_get_flash_manufacturer(ha, &man_id, &flash_id);
	DEBUG9(printk("%s(%ld): Flash man_id=%d flash_id=%d\n", __func__,
	    ha->host_no, man_id, flash_id));

	conf_addr = flash_conf_to_access_addr(0x03d8);
	switch (man_id) {
	case 0xbf: // STT flash
		rest_addr = 0x1fff;
		sec_mask = 0x3e000;
		if (flash_id == 0x80 || flash_id == 0x8e)
			conf_addr = flash_conf_to_access_addr(0x0352);
		break;   
	case 0x13: // ST M25P80
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;   
	default:
		// Default to 64 kb sector size  
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;
	}

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable flash write-protection. */
	qla24xx_write_flash_dword(ha, flash_conf_to_access_addr(0x101), 0);

	do {    /* Loop once to provide quick error exit. */
		for (liter = 0; liter < dwords; liter++, faddr++, dwptr++) {
			/* Are we at the beginning of a sector? */
			if ((faddr & rest_addr) == 0) {
				fdata = (faddr & sec_mask) << 2;
				ret = qla24xx_write_flash_dword(ha, conf_addr,
				    (fdata & 0xff00) |((fdata << 16) &
				    0xff0000) | ((fdata >> 16) & 0xff));
				    if (ret != QLA2X00_SUCCESS) {
					DEBUG9(printk("%s(%ld) Unable to flash "
					    "sector: address=%x.\n", __func__,
					    ha->host_no, faddr));
					break;
				}
			}

			ret = qla24xx_write_flash_dword(ha,
			   flash_data_to_access_addr(faddr), 
			   cpu_to_le32(*dwptr));
			if (ret !=  QLA2X00_SUCCESS) {
				DEBUG9(printk("%s(%ld) Unable to program flash "
				    "address=%x data=%x.\n", __func__,
				    ha->host_no, faddr, *dwptr));
				break;
			}
		}
	} while (0);

	/* Enable flash write-protection. */
	qla24xx_write_flash_dword(ha, flash_conf_to_access_addr(0x101), 0x9c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}

uint16_t
qla24xx_update_or_read_flash(scsi_qla_host_t *ha, uint8_t *image,
    uint32_t saddr, uint32_t length, uint8_t direction)
{
	uint32_t status;	
	struct device_reg_24xx *reg;

	reg = (struct device_reg_24xx *)ha->iobase;

	/* Not setting the timer so that tgt remains unsuspended */
	qla2x00_suspend_all_target(ha);

	/* wait for big hammer to complete if it fails */
	status = qla2x00_cmd_wait(ha);
	if (status)
		qla2x00_wait_for_hba_online(ha);

	/* Dont process mailbox cmd until flash operation is done */
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	qla2x00_disable_intrs(ha);

	switch (direction) {
	case QLA2X00_READ:
		DEBUG9(printk("%s(%ld): Reading image=%p saddr=0x%x "
		    "length=0x%x\n", __func__, ha->host_no, image, saddr >> 2,
		    length >> 2));		
		qla24xx_read_flash_data(ha, (uint32_t *)image, saddr >> 2,
		    length >> 2);
		break;

	case QLA2X00_WRITE:
		DEBUG9(printk("%s(%ld): Writing image=%p saddr=0x%x "
		    "length=0x%x\n", __func__, ha->host_no, image, saddr >> 2,
		    length >> 2));		
		status = qla24xx_write_flash_data(ha, (uint32_t *)image,
		    saddr >> 2, length >> 2);
		break;
	default:
		printk(KERN_INFO "%s unknown operation\n", __func__);
		break;
	}

	/* FixMe : Add the code to release mbx access */
	/* Reset the HBA to load the firmware from the flash */
	 if (ql2xfwloadbin == 1 && direction == QLA2X00_WRITE) {
		/* Use REGION_ALL instead off the size to determine whether 
		 * we need to reset the chip for new firmware to take affect.
	 	*/   	
		if (length == INT_OPT_ROM_SIZE_2422) {
			scsi_qla_host_t	*other_hba = NULL;	
			/* Reset the first function */
		      DEBUG2(printk(KERN_INFO "%s(%ld) resetting first function\n",
						__func__, ha->host_no);) 	
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			up(ha->dpc_wait);
			qla2x00_wait_for_hba_online(ha);
			/* Reset the second function if exist */
			for (other_hba=qla2x00_hostlist; (other_hba != NULL); other_hba=other_hba->next) {
			    if (PCI_SLOT(other_hba->pdev->devfn) == PCI_SLOT(ha->pdev->devfn) &&
				 PCI_FUNC(other_hba->pdev->devfn) != PCI_FUNC(ha->pdev->devfn))  {
				      DEBUG2(printk(KERN_INFO "%s(%ld) resetting second function\n",
						__func__, other_hba->host_no);) 	
				      set_bit(ISP_ABORT_NEEDED, &other_hba->dpc_flags);
				      up(other_hba->dpc_wait);
				      qla2x00_wait_for_hba_online(other_hba);
				       break;
			   }
			} 
		 }
	}

	qla2x00_enable_intrs(ha);

	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	
	qla2x00_unsuspend_all_target(ha);

	return status;
}

#endif

/**
 * qla2x00_update_flash() - Write image to flash chip.
 * @ha: HA context
 * @image: Source image to write to flash
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint16_t
qla2x00_update_or_read_flash(scsi_qla_host_t *ha, uint8_t *image, uint32_t saddr,
    uint32_t length, uint8_t direction)
{
	uint16_t    data;
	uint8_t     status;	
	unsigned long flags;
	uint32_t	cnt;
	device_reg_t	*reg = ha->iobase;
#if defined(ISP2300)
	if (check_24xx_or_54xx_device_ids(ha) || check_25xx_device_ids(ha))
		return qla24xx_update_or_read_flash(ha, image, saddr, length,
			    direction);
#endif
                                                                                                               

	/* Not setting the timer so that tgt remains unsuspended */
	qla2x00_suspend_all_target(ha);

	/* wait for big hammer to complete if it fails */
	status = qla2x00_cmd_wait(ha);
	if(status)
		qla2x00_wait_for_hba_online(ha);

	/* Dont process mailbox cmd until flash operation is done */
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	qla2x00_disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	/* Pause RISC. */
	WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
	PCI_POSTING(&reg->host_cmd);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
#if defined(ISP2300)
	if (check_all_device_ids(ha)) { 	    
		UDELAY(10);
	} else {
		for (cnt = 0; cnt < 30000; cnt++) {
			spin_lock_irqsave(&ha->hardware_lock, flags);
			data = (RD_REG_WORD(&reg->host_cmd) & HC_RISC_PAUSE);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

			if ( data != 0)
				break;
			else
				UDELAY(100);
		}
	}
#else
	for (cnt = 0; cnt < 30000; cnt++) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		data = (RD_REG_WORD(&reg->host_cmd) & HC_RISC_PAUSE);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		if ( data != 0)
			break;
		else
			UDELAY(100);
	}
#endif
	switch(direction) {
		case QLA2X00_READ :
			DEBUG9(printk("%s(%ld): Reading image=%p saddr=0x%x "
			    "length=0x%x\n", __func__, ha->host_no, image,
			    saddr, length));		
			status = qla2x00_read_flash_image(ha,image,saddr,length);
			break;
		case QLA2X00_WRITE :
			DEBUG9(printk("%s(%ld): Writing image=%p saddr=0x%x "
			    "length=0x%x\n", __func__, ha->host_no, image,
			    saddr, length));		
			status = qla2x00_set_flash_image(ha, image, saddr, length);
			break;
		default :
			printk(KERN_INFO "%s unknown operation\n",__func__);
			break;
	}

	/* Schedule DPC to restart the RISC */
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	up(ha->dpc_wait);
	qla2x00_wait_for_hba_online(ha);

	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	/* wake up the pending mbx cmd on the queue issued
	 * if any through the eh_paths while updating flash */
	if(test_bit(MBX_CMD_WANT, &ha->mbx_cmd_flags) && 
	   test_bit(MBX_CMD_ACTIVE, &ha->mbx_cmd_flags) == 0)
		qla2x00_release_mbx_access(ha, 0);
	
	qla2x00_unsuspend_all_target(ha);

	return status;
}

STATIC uint16_t
qla2x00_read_flash_image( scsi_qla_host_t *ha, uint8_t *usr_tmp, uint32_t saddr, uint32_t length)
{
	device_reg_t	*reg = ha->iobase;
	uint32_t	midpoint;
	uint8_t		data;
	uint32_t	ilength;
	uint16_t	status = 0;

	midpoint = length / 2;
	qla2x00_flash_enable(ha);
	WRT_REG_WORD(&reg->nvram, 0);
	PCI_POSTING(&reg->nvram);
	for (ilength = 0; ilength < length; saddr++, ilength++, usr_tmp++) {
		if (ilength == midpoint){
			WRT_REG_WORD(&reg->nvram, NV_SELECT);
			PCI_POSTING(&reg->nvram);
		}
		data = qla2x00_read_flash_byte(ha, saddr);
		if (saddr % 100)
			udelay(10);
		__put_user(data, usr_tmp);
	}
	qla2x00_flash_disable(ha);
	
	return(status);
}

#if defined(ISP2300)
STATIC void
qla2x00_read_flash_data( scsi_qla_host_t *ha, uint8_t *tmp_buf, uint32_t saddr,
    uint32_t length)
{
	device_reg_t	*reg = ha->iobase;
	uint32_t	midpoint;
	uint8_t		data;
	uint32_t	ilength;

	midpoint = length / 2;
	qla2x00_flash_enable(ha);

	WRT_REG_WORD(&reg->nvram, 0);
	PCI_POSTING(&reg->nvram);
	for (ilength = 0; ilength < length; saddr++, ilength++, tmp_buf++) {
		if (ilength == midpoint){
			WRT_REG_WORD(&reg->nvram, NV_SELECT);
			PCI_POSTING(&reg->nvram);
		}
		data = qla2x00_read_flash_byte(ha, saddr);
		if (saddr % 100)
			udelay(10);
		*tmp_buf = data;
	}

	qla2x00_flash_disable(ha);
}
#endif

/**
 * qla2x00_set_flash_image() - Write image to flash chip.
 * @ha: HA context
 * @image: Source image to write to flash
 *
 * Returns 0 on success, else non-zero.
 */
STATIC uint16_t
qla2x00_set_flash_image(scsi_qla_host_t *ha, uint8_t *image, uint32_t saddr,
    uint32_t length)
{
	uint16_t	status;
	uint32_t	addr;
	uint32_t	liter;
	uint32_t	midpoint;
	uint32_t	sec_mask;
	uint32_t	rest_addr;
	uint8_t		man_id, flash_id;
	uint8_t		sec_number;
	uint8_t		data;
	device_reg_t	*reg = ha->iobase;

	status = 0;
	sec_number = 0;

	/* Reset ISP chip. */
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	/* Delay after reset, for chip to recover. */
	udelay(20);

	qla2x00_flash_enable(ha);
	do {	/* Loop once to provide quick error exit */
		/* Structure of flash memory based on manufacturer */
		if (qla2x00_is_guadalupe(ha)) {
		   // Note: We cannot get ManufactureId of Guadalupe flash chip
			DEBUG(printk("%s(%ld): Guadalupe found\n",
				__func__,ha->host_no);)
			man_id = flash_id = 0;
			rest_addr = 0xffff;
			sec_mask   = 0x10000; 
			goto  update_flash;
		}
		qla2x00_get_flash_manufacturer(ha, &man_id, &flash_id);
		DEBUG9(printk("%s(%ld): Flash man_id=%d flash_id=%d\n",
		    __func__, ha->host_no, man_id, flash_id));
		switch (man_id) {
		case 0x20: // ST flash
			if (flash_id == 0xd2 || flash_id == 0xe3){
				// ST m29w008at part - 64kb sector size with
				// 32kb,8kb,8kb,16kb sectors at memory address
				// 0xf0000
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;   
			}
			// ST m29w010b part - 16kb sector size  
			// Default to 16kb sectors      
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;   
		case 0x40: // Mostel flash
			// Mostel v29c51001 part - 512 byte sector size  
			rest_addr = 0x1ff;
			sec_mask = 0x1fe00;
			break;   
		case 0xbf: // SST flash
			// SST39sf10 part - 4kb sector size   
			rest_addr = 0xfff;
			sec_mask = 0x1f000;
			break;
		case 0xda: // Winbond flash
			// Winbond W29EE011 part - 256 byte sector size   
			rest_addr = 0x7f;
			sec_mask = 0x1ff80;
			break;
		case 0xc2:   // Macronix flash
		  // 64k sector size    
		  if (flash_id == 0x38 || flash_id == 0x4f) {
		      rest_addr = 0xffff;
		      sec_mask = 0x10000;
		      break;
		   }   
		case 0x1f:   // Atmel flash
		  // 512k sector size    
		   if (flash_id == 0x13) {
		      rest_addr = 0x7fffffff;
		      sec_mask =   0x80000000;
		      break;
		   }   
		case 0x01: // AMD flash 
			if (flash_id == 0x38 || flash_id == 0x40 ||
			    flash_id == 0x4f) {
				// Am29LV081 part - 64kb sector size   
				// Am29LV002BT part - 64kb sector size   
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x3e) {
				// Am29LV008b part - 64kb sector size with
				// 32kb,8kb,8kb,16kb sector at memory address
				// 0xf0000
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x20 || flash_id == 0x6e) {
				// Am29LV010 part or AM29f010 - 16kb sector
				// size   
				rest_addr = 0x3fff;
				sec_mask = 0x1c000;
				break;
			} else if (flash_id == 0x6d) {
				// Am29LV001 part - 8kb sector size   
				rest_addr = 0x1fff;
				sec_mask = 0x1e000;
				break;
			}   
		default:
			// Default to 16 kb sector size  
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;
		}
update_flash:

		if (ha->device_id == QLA2322_DEVICE_ID ||
		    ha->device_id == QLA6322_DEVICE_ID) {
			if (qla2x00_erase_flash(ha, man_id, flash_id)) {
				status = 1;
				break;
			}
		}

		midpoint = FLASH_IMAGE_SIZE / 2;
		for (addr = saddr, liter = 0; liter < length; liter++, addr++)
		{
			data = image[liter];
			/* Are we at the beginning of a sector? */
			if ((addr & rest_addr) == 0) {
#if defined(ISP2300) 
				if (ha->device_id == QLA2322_DEVICE_ID ||
				    ha->device_id == QLA6322_DEVICE_ID) {
					if (addr >= 0x10000UL) {
						if (((addr >> 12) & 0xf0) &&
						    ((man_id == 0x01 && flash_id == 0x3e) ||
						    (man_id == 0x20 && flash_id == 0xd2))) {
							sec_number++;
							if (sec_number == 1) {   
								rest_addr = 0x7fff;
								sec_mask = 0x18000;
							} else if (sec_number == 2 ||
							    sec_number == 3) {
								rest_addr = 0x1fff;
								sec_mask = 0x1e000;
							} else if (sec_number == 4) {
								rest_addr = 0x3fff;
								sec_mask = 0x1c000;
							}         
						}                           
					}    
				} else
#endif
				if (addr == FLASH_IMAGE_SIZE / 2) {
					WRT_REG_WORD(&reg->nvram, NV_SELECT);
					PCI_POSTING(&reg->nvram);
				}

				if (flash_id == 0xda && man_id == 0xc1) {
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xaa);
					qla2x00_write_flash_byte(ha, 0x2aaa,
					    0x55);
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xa0);
				} else if (ha->device_id != QLA2322_DEVICE_ID &&
				    ha->device_id != QLA6322_DEVICE_ID) {
					/* Then erase it */
					if (qla2x00_erase_flash_sector(ha, addr,
					    sec_mask, man_id, flash_id)) {
						DEBUG9(printk("%s(%ld) Unable "
						    "to erase flash sector "
						    "addr=%x mask=%x.\n",
						    __func__, ha->host_no, addr,
						    sec_mask));
						status = 1;
						break;
					}
					if (man_id == 0x01 && flash_id == 0x6d)
						sec_number++;
				}
			}

			if (man_id == 0x01 && flash_id == 0x6d) {
				if (sec_number == 1 &&
				    addr == (rest_addr - 1)) {
					rest_addr = 0x0fff;
					sec_mask   = 0x1f000;
				} else if (sec_number == 3 && (addr & 0x7ffe)) {
					rest_addr = 0x3fff;
					sec_mask   = 0x1c000;
				}
			}
			if (qla2x00_program_flash_address(ha, addr, data,
			    man_id, flash_id)) {
				DEBUG9(printk("%s(%ld) Unable "
				    "to program flash address=%x data=%x.\n",
				    __func__, ha->host_no, addr, data));
				status = 1;
				break;
			}
		}
	} while (0);
	qla2x00_flash_disable(ha);

	return (status);
}

/*
* Declarations for load module
*/
#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
static struct scsi_dump_ops qla2x00_dump_ops = {
	.sanity_check	= qla2x00_dump_sanity_check,
	.poll		= qla2x00_dump_poll
};

static  Scsi_Host_Template_dump driver_template_dump = {
	.hostt		= QLA2100_LINUX_TEMPLATE,
	.dump_ops	= &qla2x00_dump_ops
};
#define driver_template	(driver_template_dump.hostt)
#else
static  Scsi_Host_Template driver_template = QLA2100_LINUX_TEMPLATE;
#endif
#include "scsi_module.c"

/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

static void
qla2x00_dump_buffer(uint8_t * b, uint32_t size) 
{
	uint32_t cnt;
	uint8_t c;

	printk(" 0   1   2   3   4   5   6   7   8   9  "
	    "Ah  Bh  Ch  Dh  Eh  Fh\n");
	printk("----------------------------------------"
	    "----------------------\n");
	for (cnt = 0; cnt < size;) {
		c = *b++;
		printk("%02x",(uint32_t) c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else
			printk("  ");
	}
	printk("\n");
}

/**************************************************************************
 *   qla2x00_print_scsi_cmd
 *	 Dumps out info about the scsi cmd and srb.
 *   Input	 
 *	 cmd : Scsi_Cmnd
 **************************************************************************/
void
qla2x00_print_scsi_cmd(Scsi_Cmnd * cmd) 
{
	struct scsi_qla_host *ha;
	struct Scsi_Host *host = cmd->host;
	srb_t *sp;
	struct os_lun *lq;
	fc_port_t *fcport;

	int i;
	ha = (struct scsi_qla_host *) host->hostdata;

	sp = (srb_t *) CMD_SP(cmd);
	printk("SCSI Command @= 0x%p, Handle=0x%08lx\n", 
			cmd, (u_long) CMD_HANDLE(cmd));
	printk("  chan=%d, target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
			cmd->channel, cmd->target, cmd->lun, cmd->cmd_len);
	printk(" CDB = ");
	for (i = 0; i < cmd->cmd_len; i++) {
		printk("0x%02x ", cmd->cmnd[i]);
	}
	printk("\n  seg_cnt =%d, retries=%d, serial_number_at_timeout=0x%lx\n",
			cmd->use_sg,
			cmd->retries, cmd->serial_number_at_timeout);
	printk("  request buffer=0x%p, request buffer len=0x%x\n", 
			cmd->request_buffer,
			cmd->request_bufflen);
	printk("  tag=%d, flags=0x%x, transfersize=0x%x \n", 
			cmd->tag, cmd->flags, cmd->transfersize);
	printk("  serial_number=%lx, SP=%p\n", cmd->serial_number,sp); 
	printk("  data direction=%d\n", cmd->sc_data_direction);
	if (sp) {
		printk("  sp flags=0x%x\n", sp->flags);
		printk("  r_start=0x%lx, u_start=0x%lx, "
				"f_start=0x%lx, state=%d\n", 
				sp->r_start, sp->u_start,
				sp->f_start, sp->state);

		lq = sp->lun_queue;
		fcport = lq->fclun->fcport;
		printk(" e_start= 0x%lx, ext_history= %d, "
				"fo retry=%d, loopid =%x, port path=%d\n", 
				sp->e_start, sp->ext_history,
				sp->fo_retry_cnt,
				fcport->loop_id, 
				fcport->cur_path);
	}
}

/*
 * qla2x00_print_q_info
 * 	 Prints queue info
 * Input
 *      q: lun queue	 
 */ 
void 
qla2x00_print_q_info(struct os_lun *q) 
{
	printk("Queue info: flags=0x%lx\n", q->q_flag);
}

#if defined(QL_DEBUG_ROUTINES)
/*
 * qla2x00_formatted_dump_buffer
 *       Prints string plus buffer.
 *
 * Input:
 *       string  = Null terminated string (no newline at end).
 *       buffer  = buffer address.
 *       wd_size = word size 8, 16, 32 or 64 bits
 *       count   = number of words.
 */
void
qla2x00_formatted_dump_buffer(char *string, uint8_t * buffer, 
				uint8_t wd_size, uint32_t count) 
{
	uint32_t cnt;
	uint16_t *buf16;
	uint32_t *buf32;

	if (ql2x_debug_print != TRUE)
		return;

	if (strcmp(string, "") != 0)
		printk("%s\n",string);

	switch (wd_size) {
		case 8:
			printk(" 0    1    2    3    4    5    6    7    "
				"8    9    Ah   Bh   Ch   Dh   Eh   Fh\n");
			printk("-----------------------------------------"
				"-------------------------------------\n");

			for (cnt = 1; cnt <= count; cnt++, buffer++) {
				printk("%02x",*buffer);
				if (cnt % 16 == 0)
					printk("\n");
				else
					printk("  ");
			}
			if (cnt % 16 != 0)
				printk("\n");
			break;
		case 16:
			printk("   0      2      4      6      8      Ah "
				"	Ch     Eh\n");
			printk("-----------------------------------------"
				"-------------\n");

			buf16 = (uint16_t *) buffer;
			for (cnt = 1; cnt <= count; cnt++, buf16++) {
				printk("%4x",*buf16);

				if (cnt % 8 == 0)
					printk("\n");
				else if (*buf16 < 10)
					printk("   ");
				else
					printk("  ");
			}
			if (cnt % 8 != 0)
				printk("\n");
			break;
		case 32:
			printk("       0          4          8          Ch\n");
			printk("------------------------------------------\n");

			buf32 = (uint32_t *) buffer;
			for (cnt = 1; cnt <= count; cnt++, buf32++) {
				printk("%8x", *buf32);

				if (cnt % 4 == 0)
					printk("\n");
				else if (*buf32 < 10)
					printk("   ");
				else
					printk("  ");
			}
			if (cnt % 4 != 0)
				printk("\n");
			break;
		default:
			break;
	}
}

#endif
/**************************************************************************
*   qla2x00_dump_regs
**************************************************************************/
static void 
qla2x00_dump_regs(struct Scsi_Host *host) 
{
	printk("Mailbox registers:\n");
	printk("qla2x00 : mbox 0 0x%04x \n", inw(host->io_port + 0x10));
	printk("qla2x00 : mbox 1 0x%04x \n", inw(host->io_port + 0x12));
	printk("qla2x00 : mbox 2 0x%04x \n", inw(host->io_port + 0x14));
	printk("qla2x00 : mbox 3 0x%04x \n", inw(host->io_port + 0x16));
	printk("qla2x00 : mbox 4 0x%04x \n", inw(host->io_port + 0x18));
	printk("qla2x00 : mbox 5 0x%04x \n", inw(host->io_port + 0x1a));
}


#if STOP_ON_ERROR
/**************************************************************************
*   qla2x00_panic
*
**************************************************************************/
static void 
qla2x00_panic(char *cp, struct Scsi_Host *host) 
{
	struct scsi_qla_host *ha;
	long *fp;

	ha = (struct scsi_qla_host *) host->hostdata;
	DEBUG2(ql2x_debug_print = 1;);
	printk("qla2100 - PANIC:  %s\n", cp);
	printk("Current time=0x%lx\n", jiffies);
	printk("Number of pending commands =0x%lx\n", ha->actthreads);
	printk("Number of queued commands =0x%lx\n", ha->qthreads);
	printk("Number of free entries = (%d)\n", ha->req_q_cnt);
	printk("Request Queue @ 0x%lx, Response Queue @ 0x%lx\n",
			       ha->request_dma, ha->response_dma);
	printk("Request In Ptr %d\n", ha->req_ring_index);
	fp = (long *) &ha->flags;
	printk("HA flags =0x%lx\n", *fp);
	qla2x00_dump_requests(ha);
	qla2x00_dump_regs(host);
	cli();
	for (;;) {
		udelay(2);
		barrier();
		/* cpu_relax();*/
	}
	sti();
}

#endif

/**************************************************************************
*   qla2x00_dump_requests
*
**************************************************************************/
void
qla2x00_dump_requests(scsi_qla_host_t *ha) 
{

	Scsi_Cmnd       *cp;
	srb_t           *sp;
	int i;

	printk("Outstanding Commands on controller:\n");

	for (i = 1; i < MAX_OUTSTANDING_COMMANDS; i++) {
		if ((sp = ha->outstanding_cmds[i]) == NULL)
			continue;
		if ((cp = sp->cmd) == NULL)
			continue;

		printk("(%d): Pid=%d, sp flags=0x%lx, cmd=0x%p\n", 
			i, 
			(int)sp->cmd->serial_number, 
			(long)sp->flags,CMD_SP(sp->cmd));
	}
}


/**************************************************************************
*   qla2x00_setup
*
*   Handle Linux boot parameters. This routine allows for assigning a value
*   to a parameter with a ';' between the parameter and the value.
*   ie. qla2x00=arg0;arg1;...;argN;<properties .... properties>  OR
*   via the command line.
*   ie. qla2x00 ql2xopts=arg0;arg1;...;argN;<properties .... properties>
**************************************************************************/
#if !defined(MODULE)
static int __init
qla2x00_setup (char *s)
#else
void 
qla2x00_setup(char *s)
#endif	
{
	char		*cp, *np;
	char		*slots[MAXARGS];
	char		**argv = &slots[0];
	static char	buf[LINESIZE];
	int		argc, opts;

	if (s == NULL || *s == '\0') {
		DEBUG2(printk(KERN_INFO "qla2x00_setup: got NULL string.\n");)
#if !defined(MODULE)
		return 0;
#else
		return;
#endif
	}

	/*
	 * Determine if we have any properties.
	 */
	cp = s;
	opts = 1;
	while (*cp && (np = qla2x00_get_line(cp, buf)) != NULL) {
		if ( *cp ) {
			if( isdigit(*cp) ) {
			  ql2xdevflag++; 
			  opts = 0;
			  DEBUG(printk("qla2x00: abbreviated fmt devconf=%s\n",cp);)
			ql2xdevconf = cp;
			(opts > 0)? opts-- : 0;
			break;
			}
			else {
			if (strncmp("scsi-qla",buf,8) == 0) {
			    DEBUG(printk("qla2x00: devconf=%s\n",cp);)
			    ql2xdevconf = cp;
			    (opts > 0)? opts-- : 0;
			    break;
			}
			}
		}
		opts++;
		cp = np;
	}
	/*
	 * Parse the args before the properties
	 */
	if (opts) {
		opts = (opts > MAXARGS-1)? MAXARGS-1: opts;
		argc = qla2x00_get_tokens(s, argv, opts);
		while (argc > 0) {
			cp = *argv;
			DEBUG(printk("scsi: found cmd arg =[%s]\n", cp);)

			if (strcmp(cp, "verbose") == 0) {
				DEBUG(printk("qla2x00: verbose\n");)
				qla2x00_verbose++;
			} else if (strcmp(cp, "quiet") == 0) {
				qla2x00_quiet = 1;
			} else if (strcmp(cp, "reinit_on_loopdown") == 0) {
				qla2x00_reinit++;
				DEBUG(printk("qla2x00: reinit_on_loopdown\n");)
			}
			argc--, argv++;
		}
	}

#if !defined(MODULE)
	if (ql2xdevconf)
		return 1;
	else
		return 0;
#endif

}

#if !defined(MODULE)
__setup("ql2xopts=", qla2x00_setup);
#endif

/********************** qla2x00_get_line *********************
* qla2x00_get_line
* Copy a substring from the specified string. The substring
* consists of any number of chars seperated by white spaces (i.e. spaces)
* and ending with a newline '\n' or a semicolon ';'.
*
* Enter:
* str - orig string
* line - substring
*
* Returns:
*   cp - pointer to next string
*     or
*   null - End of string
*************************************************************/
static char *
qla2x00_get_line(char *str, char *line) 
{
	register	char 	*cp = str;
	register	char 	*sp = line;

	/* skip preceeding spaces */
	while (*cp && *cp == ' ')
		++cp;
	while ((*cp) && *cp != '\n' && *cp != ';')   /* end of line */
		*sp++ = *cp++;

	*sp = '\0';

	DEBUG5(printk("%s(): %s\n", __func__, line);)

	if( (*cp) ) {
		cp++;
		return (cp);
	}

	return (NULL);
}


/**************************** get_tokens *********************
* Parse command line into argv1, argv2, ... argvX
* Arguments are seperated by white spaces and colons and end
* with a NULL.
*************************************************************/
static int 
qla2x00_get_tokens(char *line, char **argv, int maxargs ) 
{
	register	char 	*cp = line;
	int	count = 0;

	while (*cp && count < maxargs) {
		/* skip preceeding spaces */
		while ((*cp) && *cp == ' ')
			++cp;
		/* symbol starts here */
		argv[count++] = cp;
		/* skip symbols */
		while ((*cp) && !(*cp == ' ' || *cp == ';' || *cp == ':'))
			cp++;
		/* replace comma or space with a null */
		if((*cp) && (*cp ==' ' ) && argv[count-1] != cp)
			*cp++ = '\0';
	}
	return (count);
}

#if VSA
/* XXX: There is no fc_db member in HA. */
/*
 * qla2x00_get_vsa_opt_from_config
 *      Get VSA option from the configuration parameters.
 *      Bit order is little endian.
 *
 * Input:
 * ha  -- Host adapter
 * tgt  -- target/device number
 */
void
qla2x00_get_vsa_opt_from_config(scsi_qla_host_t *ha,
				uint16_t tgt, uint16_t dev_no) 
{

	char		propbuf[60]; /* size of search string */
	int		rval;
	char		vsa;

	/* Get "target-N-device-N-vsa" as a 1 bit value */
	PERSIST_STRING("scsi-qla%ld-tgt-%d-di-%d-vsa", "%ld-%d-%d-v");

	rval = qla2x00_get_prop_xstr(ha, propbuf, (uint8_t *)&vsa,1);
	if (rval != -1 && rval == 1) {
		ha->fc_db[tgt].flag |= DEV_FLAG_VSA;

		DEBUG(printk("cfg: scsi-qla%d-target-%d-vsa=1\n",
				(int) ha->instance,  tgt);)
	}
}
#endif

/*
 * qla2x00_cfg_persistent_binding
 *	Get driver configuration file target persistent binding entries.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Context:
 *	Kernel context.
 */
STATIC void
qla2x00_cfg_persistent_binding(scsi_qla_host_t *ha) 
{
	int		rval;
	static char	propbuf[LINESIZE];
	uint16_t	tgt;
	uint16_t	dev_no = 0; /* not used */
	char		*cmdline = ha->cmdline;
	port_id_t	d_id, *pd_id;
	uint8_t		portid[3];
	uint8_t		node_name[8], *pnn;
	uint8_t		port_name[8], *ppn;
	os_tgt_t	*tq;

	ENTER(__func__);

	/* FC name for devices */
	for (tgt = 0; tgt < MAX_FIBRE_DEVICES; tgt++) {

		/*
		 * Retrive as much information as possible (PN/PID/NN).
		 *
		 * Based on binding type, skip incomplete entries.
		 */
		ppn = port_name;
		PERSIST_STRING("scsi-qla%ld-tgt-%d-di-%d-port", "%ld-%d-%d-p");
		rval = qla2x00_get_prop_16chars(ha, propbuf, ppn, cmdline);
		if (rval != 0)
			ppn = NULL;
		if (ha->binding_type == BIND_BY_PORT_NAME && rval != 0)
			continue;

		pd_id = &d_id;
		PERSIST_STRING("scsi-qla%ld-tgt-%d-di-%d-pid", "%ld-%d-%d-i");
		rval = qla2x00_get_prop_xstr(ha,
				propbuf, portid, sizeof(portid));
		if (rval == -1 || rval != sizeof(portid))
			pd_id = NULL;
		if (ha->binding_type == BIND_BY_PORT_ID &&
			(rval == -1 || rval != sizeof(portid)))
			continue;

		pnn = node_name;
		PERSIST_STRING("scsi-qla%ld-tgt-%d-di-%d-node", "%ld-%d-%d-n");
		rval = qla2x00_get_prop_16chars(ha, propbuf, pnn, cmdline);
		if (rval != 0)
			pnn = NULL;

		tq = qla2x00_tgt_alloc(ha, tgt);
		if (tq == NULL) {
			printk(KERN_WARNING
				"%s(): Unable to allocate memory for target\n",
				__func__);
			continue;
		} 
			

		if (ppn != NULL) {
			memcpy(tq->port_name, ppn, WWN_SIZE);
		}
		if (pd_id != NULL) {
			/*
			 * The portid is read in big-endian format, convert 
			 * before updating information
			 */
			pd_id->r.d_id[0] = portid[2];
			pd_id->r.d_id[1] = portid[1];
			pd_id->r.d_id[2] = portid[0];
			tq->d_id.b24 = pd_id->b24;
		}
		if (pnn != NULL) {
			memcpy(tq->node_name, pnn, WWN_SIZE);
		}

		DEBUG(printk("Target %03d - configured by user: ",tgt);)
		switch (ha->binding_type) {
			case BIND_BY_PORT_NAME:
				DEBUG(printk("**bind tgt by port-%03d="
					"%02x%02x%02x%02x%02x%02x%02x%02x\n",
					tgt,
					ppn[0], ppn[1], ppn[2], ppn[3],
					ppn[4], ppn[5], ppn[6], ppn[7]);)
				break;

			case BIND_BY_PORT_ID:
				DEBUG(printk("**bind tgt by port-id-%03d=%06x\n",
					tgt,
					pd_id->b24);)
				break;
		}
		/* look for VSA */
#if VSA
		qla2x00_get_vsa_opt_from_config(ha, tgt, dev_no);
#endif
	}

	LEAVE(__func__);
}


/*
 * kmem_zalloc
 * Allocate and zero out the block of memory
 */
inline void *
kmem_zalloc( int siz, int code, int id) 
{
	uint8_t *bp;

	if ((bp = kmalloc(siz, code)) != NULL) {
		memset(bp, 0, siz);
	}
#if QL_TRACE_MEMORY
	if (mem_trace_ptr == 1000)
		mem_trace_ptr = 0;
	mem_trace[mem_trace_ptr] = (u_long ) bp;
	mem_id[mem_trace_ptr++] = (u_long ) id;
#endif

	return ((void *)bp);
}

#if 0
/*
 * kmem_free
 * Deallocate the block of memory
 */
inline void 
kmem_free(void *ptr) 
{
#if QL_TRACE_MEMORY
	int	i;

	for (i =0; i < 1000; i++)
		if (mem_trace[i] == (unsigned long) ptr) {
			mem_trace[i]  = (unsigned long) NULL;
			break;
		}
#endif
	kfree(ptr);
}
#endif

#if defined(ISP2300)
/* Include routines for supporting IP */
#include "qla_ip.c"
#endif

/*
 * Declarations for failover
 */

#include "qla_cfg.c"
#include "qla_fo.c"

#if APIDEV
/****************************************************************************/
/* Create character driver "HbaApiDev" w dynamically allocated major number */
/* and create "/proc/scsi/qla2x00/HbaApiNode" as the device node associated */
/* with the major number.                                                   */
/****************************************************************************/

#define APIDEV_NODE  "HbaApiNode"
#define APIDEV_NAME  "HbaApiDev"

static int apidev_major = 0;
static struct Scsi_Host *apidev_host = 0;

static int 
apidev_open(struct inode *inode, struct file *file) 
{
	DEBUG9(printk(KERN_INFO
			"%s(): open MAJOR number = %d, MINOR number = %d\n",
			__func__,
			MAJOR(inode->i_rdev), MINOR(inode->i_rdev));)
	return 0;
}

static int 
apidev_close(struct inode *inode, struct file *file) 
{
	DEBUG9(printk(KERN_INFO "%s(): closed\n", __func__);)

	return 0;
}

static int 
apidev_ioctl(struct inode *inode, struct file *fp, 
		unsigned int cmd, unsigned long arg) 
{
	/* Since this var is not really used, use static type to
	 * conserve stack space.
	 */
	static Scsi_Device dummy_scsi_device;

	dummy_scsi_device.host = apidev_host;

	return (qla2x00_ioctl(&dummy_scsi_device, (int)cmd, (void*)arg));
}

static struct file_operations apidev_fops = {
	owner:
		THIS_MODULE,
	ioctl:
		apidev_ioctl,
	open:
		apidev_open,
	release:
		apidev_close
};

#if defined(QLA_CONFIG_COMPAT)
#include "qla_ppc64.c"
#endif

static int 
apidev_init(struct Scsi_Host *host) 
{

	if (apidev_host) {
		return 0;
	}

	apidev_major = register_chrdev(0, APIDEV_NAME, &apidev_fops);
	if (0 > apidev_major) {
		DEBUG(printk("%s(): register_chrdev rc=%d\n",
				__func__,
				apidev_major);)

		return apidev_major;
	}

	apidev_host = host;

	DEBUG(printk("%s(): Creating (%s) %s/%s major=%d\n",
			__func__,
			host->hostt->proc_name,
			host->hostt->proc_dir->name, 
			APIDEV_NODE, apidev_major);)

	proc_mknod(APIDEV_NODE, 0600+S_IFCHR, host->hostt->proc_dir,
			(kdev_t)MKDEV(apidev_major, 0));

#if defined(QLA_CONFIG_COMPAT)
	apidev_init_ppc64();
#endif

	return 0;
}

static int apidev_cleanup() 
{

	if (!apidev_host)
		return 0;

	unregister_chrdev(apidev_major,APIDEV_NAME);
	remove_proc_entry(APIDEV_NODE,apidev_host->hostt->proc_dir);
	apidev_host = 0;

#if defined(QLA_CONFIG_COMPAT)
	apidev_cleanup_ppc64();
#endif

	return 0;
}
#endif /* APIDEV */

#if defined(QL_DEBUG_ROUTINES)
#if DEBUG_GET_FW_DUMP
#if defined(ISP2300)
#include  "x2300dbg.c"
#endif
#endif
#endif

#if defined(CONFIG_DISKDUMP) || defined(CONFIG_DISKDUMP_MODULE)
static int
qla2x00_dump_sanity_check(Scsi_Device *device)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) device->host->hostdata;
	fc_port_t	*fcport;
	os_lun_t	*lq;

	if (ha == NULL)
		return -ENXIO;
		
#if !defined(SCSI_HAS_HOST_LOCK)
	/* message request lock is busy */
	if (spin_is_locked(&io_request_lock))
		return -EBUSY;
#else
	/* message host lock is busy */
	if (spin_is_locked(ha->host->host_lock))
		return -EBUSY;
#endif
	/* A link down judgment */
	if((lq = GET_LU_Q(ha, device->id, device->lun)) == NULL)
		return -ENXIO;

	fcport = lq->fclun->fcport;
	if (atomic_read(&fcport->state) != FC_ONLINE)
		return -ENXIO;

	return 0;
}

static void
qla2x00_dump_poll(Scsi_Device *device)
{
	scsi_qla_host_t *ha = (scsi_qla_host_t *) device->host->hostdata;

	if (ha == NULL)
		return;

	/* check interrupt pending */
	qla2x00_intr_handler(0, ha, NULL);
}
#endif

EXPORT_NO_SYMBOLS;
