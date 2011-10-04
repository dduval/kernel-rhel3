/* 
 * $Id$
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * (C) Copyright IBM Corp. 2002, 2003
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 *
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *      Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *      Aron Zeh
 *      Wolfgang Taphorn
 *      Stefan Bader <stefan.bader@de.ibm.com>
 *      Andreas Herrmann <aherrman@de.ibm.com>
 *      Stefan Voelkel <Stefan.Voelkel@millenux.com>
 */

#ifndef _ZFCP_H_
#define _ZFCP_H_

#define ZFCP_LOW_MEM_CREDITS
#define ZFCP_STAT_REQSIZES
#define ZFCP_STAT_QUEUES

#define ZFCP_PARSE_ERR_BUF_SIZE 100

#include <linux/config.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/completion.h>

#include <asm/types.h>
#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>	/* devreg_t */
#include <asm/debug.h>		/* debug_info_t */
#include <asm/qdio.h>		/* qdio_buffer_t */

#include <linux/blk.h>
#include <../drivers/scsi/scsi.h>
#include <../drivers/scsi/hosts.h>

#include "zfcp_fsf.h"

/* 32 bit for SCSI ID and LUN as long as the SCSI stack uses this type */
typedef u32	scsi_id_t;
typedef u32	scsi_lun_t;

typedef u16	devno_t;
typedef u16	irq_t;

typedef u64	wwn_t;
typedef u32	fc_id_t;
typedef u64	fcp_lun_t;


struct _zfcp_adapter;
struct _zfcp_fsf_req;

/*
 * very simple implementation of an emergency pool:
 * a pool consists of a fixed number of equal elements,
 * for each purpose a different pool should be created
 */
typedef struct _zfcp_mem_pool_element {
	atomic_t use;
	void *buffer;
} zfcp_mem_pool_element_t;

typedef struct _zfcp_mem_pool {
	int entries;
	int size;
	zfcp_mem_pool_element_t *element;
        struct timer_list timer;
} zfcp_mem_pool_t;

typedef struct _zfcp_adapter_mem_pool {
        zfcp_mem_pool_t fsf_req_status_read;
	zfcp_mem_pool_t data_status_read;
        zfcp_mem_pool_t data_gid_pn;
        zfcp_mem_pool_t fsf_req_erp;
        zfcp_mem_pool_t fsf_req_scsi;
} zfcp_adapter_mem_pool_t;

typedef void zfcp_fsf_req_handler_t(struct _zfcp_fsf_req*);

typedef struct {
} zfcp_exchange_config_data_t;

typedef struct {
	struct _zfcp_port *port;
} zfcp_open_port_t;

typedef struct {
	struct _zfcp_port *port;
} zfcp_close_port_t;

typedef struct {
	struct _zfcp_unit *unit;
} zfcp_open_unit_t;

typedef struct {
	struct _zfcp_unit *unit;
} zfcp_close_unit_t;

typedef struct {
	struct _zfcp_port *port;
} zfcp_close_physical_port_t;

typedef struct {
	struct _zfcp_unit *unit;
	Scsi_Cmnd *scsi_cmnd;
	unsigned long start_jiffies;
} zfcp_send_fcp_command_task_t;


typedef struct {
	struct _zfcp_unit *unit;
} zfcp_send_fcp_command_task_management_t;

typedef struct {
	struct _zfcp_fsf_req_t *fsf_req;
	struct _zfcp_unit *unit;
} zfcp_abort_fcp_command_t;

/*
 * FC-GS-2 stuff
 */
#define ZFCP_CT_REVISION		0x01
#define ZFCP_CT_DIRECTORY_SERVICE	0xFC
#define ZFCP_CT_NAME_SERVER		0x02
#define ZFCP_CT_SYNCHRONOUS		0x00
#define ZFCP_CT_GID_PN			0x0121
#define ZFCP_CT_GA_NXT			0x0100
#define ZFCP_CT_MAX_SIZE		0x1020
#define ZFCP_CT_ACCEPT			0x8002

/*
 * FC-FS stuff
 */
#define R_A_TOV				10 /* seconds */
#define ZFCP_ELS_TIMEOUT		(2 * R_A_TOV)

#define ZFCP_LS_RJT			0x01
#define ZFCP_LS_ACC			0x02
#define ZFCP_LS_RTV			0x0E
#define ZFCP_LS_RLS			0x0F
#define ZFCP_LS_PDISC			0x50
#define ZFCP_LS_ADISC			0x52
#define ZFCP_LS_RSCN			0x61
#define ZFCP_LS_RNID			0x78
#define ZFCP_LS_RLIR			0x7A
#define ZFCP_LS_RTV_E_D_TOV_FLAG	0x04000000

/* LS_ACC Reason Codes */
#define ZFCP_LS_RJT_INVALID_COMMAND_CODE	0x01
#define ZFCP_LS_RJT_LOGICAL_ERROR		0x03
#define ZFCP_LS_RJT_LOGICAL_BUSY		0x05
#define ZFCP_LS_RJT_PROTOCOL_ERROR		0x07
#define ZFCP_LS_RJT_UNABLE_TO_PERFORM		0x09
#define ZFCP_LS_RJT_COMMAND_NOT_SUPPORTED	0x0B
#define ZFCP_LS_RJT_VENDOR_UNIQUE_ERROR		0xFF

struct zfcp_ls_rjt {
	u8		code;
	u8		field[3];
	u8		reserved;
	u8		reason_code;
	u8		reason_expl;
	u8		vendor_unique;
} __attribute__ ((packed));

struct zfcp_ls_rtv {
	u8		code;
	u8		field[3];
} __attribute__ ((packed));

struct zfcp_ls_rtv_acc {
	u8		code;
	u8		field[3];
	u32		r_a_tov;
	u32		e_d_tov;
	u32		qualifier;
} __attribute__ ((packed));

struct zfcp_ls_rls {
	u8		code;
	u8		field[3];
	fc_id_t		port_id;
} __attribute__ ((packed));

struct zfcp_ls_rls_acc {
	u8		code;
	u8		field[3];
	u32		link_failure_count;
	u32		loss_of_sync_count;
	u32		loss_of_signal_count;
	u32		prim_seq_prot_error;
	u32		invalid_transmition_word;
	u32		invalid_crc_count;
} __attribute__ ((packed));

struct zfcp_ls_pdisc {
	u8		code;
	u8		field[3];
	u8		common_svc_parm[16];
	wwn_t		wwpn;
	wwn_t		wwnn;
	struct {
		u8	class1[16];
		u8	class2[16];
		u8	class3[16];
	} svc_parm;
	u8		reserved[16];
	u8		vendor_version[16];
} __attribute__ ((packed));

struct zfcp_ls_pdisc_acc {
	u8		code;
	u8		field[3];
	u8		common_svc_parm[16];
	wwn_t		wwpn;
	wwn_t		wwnn;
	struct {
		u8	class1[16];
		u8	class2[16];
		u8	class3[16];
	} svc_parm;
	u8		reserved[16];
	u8		vendor_version[16];
} __attribute__ ((packed));

struct zfcp_ls_adisc {
	u8		code;
	u8		field[3];
	fc_id_t		hard_nport_id;
	wwn_t		wwpn;
	wwn_t		wwnn;
	fc_id_t		nport_id;
} __attribute__ ((packed));

struct zfcp_ls_adisc_acc {
	u8		code;
	u8		field[3];
	fc_id_t		hard_nport_id;
	wwn_t		wwpn;
	wwn_t		wwnn;
	fc_id_t		nport_id;
} __attribute__ ((packed));

struct zfcp_ls_rnid {
	u8		code;
	u8		field[3];
	u8		node_id_format;
	u8		reserved[3];
} __attribute__((packed));

/* common identification data */
struct zfcp_ls_rnid_common_id {
	u64		n_port_name;
	u64		node_name;
} __attribute__((packed));

/* general topology specific identification data */
struct zfcp_ls_rnid_general_topology_id {
	u8		vendor_unique[16];
	u32		associated_type;
	u32		physical_port_number;
	u32		nr_attached_nodes;
	u8		node_management;
	u8		ip_version;
	u16		port_number;
	u8		ip_address[16];
	u8		reserved[2];
	u16		vendor_specific;
} __attribute__((packed));

struct zfcp_ls_rnid_acc {
	u8		code;
	u8		field[3];
	u8		node_id_format;
	u8		common_id_length;
	u8		reserved;
	u8		specific_id_length;
	struct zfcp_ls_rnid_common_id
			common_id;
	struct zfcp_ls_rnid_general_topology_id
			specific_id;
} __attribute__((packed));

/*
 * FC-GS-4 stuff
 */
#define ZFCP_CT_TIMEOUT			(3 * R_A_TOV)


/*
 * header for CT_IU
 */
struct ct_hdr {
	u8 revision;		// 0x01
	u8 in_id[3];		// 0x00
	u8 gs_type;		// 0xFC	Directory Service
	u8 gs_subtype;		// 0x02	Name Server
	u8 options;		// 0x00 single bidirectional exchange
	u8 reserved0;
	u16 cmd_rsp_code;	// 0x0121 GID_PN, or 0x0100 GA_NXT
	u16 max_res_size;	// <= (4096 - 16) / 4
	u8 reserved1;
	u8 reason_code;
	u8 reason_code_expl;
	u8 vendor_unique;
} __attribute__ ((packed));

/*
 * nameserver request CT_IU -- for requests where
 * a port identifier or a port name is required
 */
struct ct_iu_ns_req {
	struct ct_hdr header;
	union {
		wwn_t wwpn;	/* e.g .for GID_PN */
		fc_id_t d_id;	/* e.g. for GA_NXT */
	} data;
} __attribute__ ((packed));

/* FS_ACC IU and data unit for GID_PN nameserver request */
struct ct_iu_gid_pn {
	struct ct_hdr header;
	fc_id_t d_id;
} __attribute__ ((packed));

/* data unit for GA_NXT nameserver request */
struct ns_ga_nxt {
        u8 port_type;
        u8 port_id[3];
        u64 port_wwn;
        u8 port_symbolic_name_length;
        u8 port_symbolic_name[255];
        u64 node_wwn;
        u8 node_symbolic_name_length;
        u8 node_symbolic_name[255];
        u64 initial_process_associator;
        u8 node_ip[16];
        u32 cos;
        u8 fc4_types[32];
        u8 port_ip[16];
        u64 fabric_wwn;
        u8 reserved;
        u8 hard_address[3];
} __attribute__ ((packed));

/* FS_ACC IU and data unit for GA_NXT nameserver request */
struct ct_iu_ga_nxt {
	struct ct_hdr header;
	struct ns_ga_nxt du;
} __attribute__ ((packed));


typedef void (*zfcp_send_ct_handler_t)(unsigned long);

/* used to pass parameters to zfcp_send_ct() */
struct zfcp_send_ct {
	struct _zfcp_port *port;
	struct scatterlist *req;
	struct scatterlist *resp;
	unsigned int req_count;
	unsigned int resp_count;
	zfcp_send_ct_handler_t handler;
	unsigned long handler_data;
	struct _zfcp_mem_pool *pool;
	int timeout;
	struct timer_list *timer;
	struct completion *completion;
	int status;
};

/* used for name server requests in error recovery */
struct zfcp_gid_pn_data {
	struct zfcp_send_ct ct;
	struct scatterlist req;
	struct scatterlist resp;
	struct ct_iu_ns_req ct_iu_req;
	struct ct_iu_gid_pn ct_iu_resp;
};

typedef int (*zfcp_send_els_handler_t)(unsigned long);

/* used to pass parameters to zfcp_send_els() */
struct zfcp_send_els {
	struct _zfcp_port *port;
	struct scatterlist *req;
	struct scatterlist *resp;
	unsigned int req_count;
	unsigned int resp_count;
	zfcp_send_els_handler_t handler;
	unsigned long handler_data;
	struct completion *completion;
	int ls_code;
	int status;
};

typedef struct {
	fsf_status_read_buffer_t *buffer;
} zfcp_status_read_t;

/* request specific data */
typedef union _zfcp_req_data {
	zfcp_exchange_config_data_t	exchange_config_data;
	zfcp_open_port_t		open_port;
	zfcp_close_port_t		close_port;
	zfcp_open_unit_t		open_unit;
	zfcp_close_unit_t		close_unit;
	zfcp_close_physical_port_t	close_physical_port;
	zfcp_send_fcp_command_task_t	send_fcp_command_task;
	zfcp_send_fcp_command_task_management_t
		send_fcp_command_task_management;
	zfcp_abort_fcp_command_t	abort_fcp_command;
	struct zfcp_send_ct		*send_ct;
	struct zfcp_send_els		*send_els;
	zfcp_status_read_t		status_read;
	fsf_qtcb_bottom_port_t		*port_data;
} zfcp_req_data_t;

/* FSF request */
typedef struct _zfcp_fsf_req {
	/* driver wide common magic */
	u32				common_magic;
	/* data structure specific magic */
	u32				specific_magic;
	/* list of FSF requests */
	struct list_head		list;
	/* adapter this request belongs to */
	struct _zfcp_adapter		*adapter;
	/* number of SBALs that can be used */
	u8				sbal_number;
	/* first SBAL for this request */
	u8				sbal_first;
	/* last possible SBAL for this request */
	u8				sbal_last;
	/* current SBAL during creation of request */
	u8				sbal_curr;
	/* current SBALE during creation of request */
	u8				sbale_curr;

	/* can be used by routine to wait for request completion */
	wait_queue_head_t completion_wq;
	/* status of this request */
	volatile u32			status;
	/* copy of FSF Command (avoid to touch SBAL when it is QDIO owned) */
	u32				fsf_command;
	/* address of QTCB*/
	fsf_qtcb_t			*qtcb;
	/* Sequence number used with this request */
	u32				seq_no;
	/* Information fields corresponding to the various types of request */ 
	zfcp_req_data_t			data;
	/* used if this request is issued on behalf of erp */
	struct _zfcp_erp_action		*erp_action;
	/* used if this request is alloacted from emergency pool */
	struct _zfcp_mem_pool		*pool;
} zfcp_fsf_req_t;

typedef struct _zfcp_erp_action {
	struct list_head list;
	/* requested action */
	int action;
	/* thing which should be recovered */
	struct _zfcp_adapter *adapter;
	struct _zfcp_port *port;
	struct _zfcp_unit *unit;
	/* status of recovery */
	volatile u32 status;
	/* step which is currently taken */
	u32 step;
	/* fsf_req which is currently pending for this action */
	struct _zfcp_fsf_req *fsf_req;
	struct timer_list timer;
	/* retry counter, ... ? */
	union {
		/* used for nameserver requests (GID_PN) in error recovery */
		struct zfcp_gid_pn_data *gid_pn;
	} data;
} zfcp_erp_action_t;

/* logical unit */
typedef struct _zfcp_unit {
	/* driver wide common magic */
	u32				common_magic;
	/* data structure specific magic */
	u32				specific_magic;
	/* list of logical units */
	struct list_head		list;
	/* remote port this logical unit belongs to */
	struct _zfcp_port		*port;
	/* status of this logical unit */
	atomic_t			status;
	/* own SCSI LUN */
	scsi_lun_t			scsi_lun;
	/* own FCP_LUN */
	fcp_lun_t			fcp_lun;
	/* handle assigned by FSF */
	u32				handle;
	/* save scsi device struct pointer locally */
	Scsi_Device                     *device;
	/* used for proc_fs support */
	char                            *proc_buffer;
	struct proc_dir_entry		*proc_file;
	struct proc_dir_entry		*proc_dir;
	/* error recovery action pending for this unit (if any) */
	struct _zfcp_erp_action		erp_action;
	atomic_t                        erp_counter;
	/* list of units in order of configuration via mapping */
	struct list_head		map_list;
} zfcp_unit_t;

/* remote port */
typedef struct _zfcp_port {
	/* driver wide common magic */
	u32				common_magic;
	/* data structure specific magic */
	u32				specific_magic;
	/* list of remote ports */
	struct list_head		list;
	/* adapter this remote port accessed */
	struct _zfcp_adapter		*adapter;
	/* head of logical unit list */
	struct list_head		unit_list_head;
	/* lock for critical operations on list of logical units */
	rwlock_t			unit_list_lock;
	/* number of logical units in list */
	u32				units;
	/* status of this remote port */
	atomic_t			status;
	/* own SCSI ID */
	scsi_id_t			scsi_id;
	/* WWNN of node this remote port belongs to (if known) */
	wwn_t				wwnn;
	/* own WWPN */
	wwn_t				wwpn;
	/* D_ID */
	fc_id_t				d_id;
	/* largest SCSI LUN of units attached to this port */
	scsi_lun_t			max_scsi_lun;
	/* handle assigned by FSF */
	u32				handle;
	/* used for proc_fs support */
        char                            *proc_buffer;
	struct proc_dir_entry		*proc_file;
	struct proc_dir_entry		*proc_dir;
	/* error recovery action pending for this port (if any) */
	struct _zfcp_erp_action		erp_action;
        atomic_t                        erp_counter;
} zfcp_port_t;


/* QDIO request/response queue */
typedef struct _zfcp_qdio_queue {
	/* SBALs */
	qdio_buffer_t			*buffer[QDIO_MAX_BUFFERS_PER_Q];
	/* index of next free buffer in queue (only valid if free_count>0) */
	u8				free_index;
	/* number of free buffers in queue */
	atomic_t			free_count;
	/* lock for critical operations on queue */
	rwlock_t			queue_lock;
        /* outbound queue only, SBALs since PCI indication */
        int                             distance_from_int;
} zfcp_qdio_queue_t;


#define ZFCP_PARSE_ERR_BUF_SIZE 100

/* adapter */
typedef struct _zfcp_adapter {
/* elements protected by zfcp_data.adapter_list_lock */
	/* driver wide common magic */
	u32				common_magic;
	/* data structure specific magic */
	u32				specific_magic;
	struct list_head		list;
	/* WWNN */
	wwn_t				wwnn;
	/* WWPN */
	wwn_t				wwpn;
	/* N_Port ID */
	fc_id_t				s_id;
	/* irq (subchannel) */
	irq_t				irq;
	/* device number */
	devno_t				devno;
	/* default FC service class */
	u8				fc_service_class;
	/* topology which this adapter is attached to */
	u32				fc_topology;
	/* FC interface speed */
	u32				fc_link_speed;
	/* Hydra version */
	u32				hydra_version;
	/* Licensed Internal Code version of FSF in adapter */
	u32				fsf_lic_version;
	/* supported features of FCP channel */
	u32				supported_features;
	/* hardware version of FCP channel */
	u32				hardware_version;
	/* serial number of hardware */
	u8				serial_number[32];
	/* SCSI host structure of the mid layer of the SCSI stack */
	struct Scsi_Host		*scsi_host;
	/* Start of packets in flight list */
	Scsi_Cmnd                       *first_fake_cmnd;
	/* lock for the above */
	rwlock_t			fake_list_lock;
	/* starts processing of faked commands */
	struct timer_list               fake_scsi_timer;
	atomic_t			fake_scsi_reqs_active;
	/* name */
	unsigned char			name[9];
	/* elements protected by port_list_lock */
	/* head of remote port list */
	struct list_head		port_list_head;
	/* lock for critical operations on list of remote ports */
	rwlock_t			port_list_lock;
	/* number of remote currently configured */
	u32				ports;
	/* largest SCSI ID of ports attached to this adapter */
	scsi_id_t			max_scsi_id;
	/* largest SCSI LUN of units of ports attached to this adapter */
	scsi_lun_t			max_scsi_lun;
	/* elements protected by fsf_req_list_lock */
	/* head of FSF request list */
	struct list_head		fsf_req_list_head;
	/* lock for critical operations on list of FSF requests */
	rwlock_t			fsf_req_list_lock;
	/* number of existing FSF requests pending */
	atomic_t       			fsf_reqs_active;
	/* elements partially protected by request_queue.lock */
	/* request queue */
	struct _zfcp_qdio_queue		request_queue;
	/* FSF command sequence number */
	u32				fsf_req_seq_no;
	/* can be used to wait for avaliable SBALs in request queue */
	wait_queue_head_t		request_wq;
	/* elements partially protected by response_queue.lock */
	/* response queue */
	struct _zfcp_qdio_queue		response_queue;
	devstat_t			devstat;
	s390_dev_info_t                 devinfo;
	rwlock_t			abort_lock;
	/* number of status reads failed */
	u16				status_read_failed;
	/* elements which various bits are protected by several locks */
	/* status of this adapter */
	atomic_t			status;
	/* for proc_info */
	char                            *proc_buffer;
	/* and here for the extra proc_dir */
	struct proc_dir_entry 		*proc_dir;
	struct proc_dir_entry		*proc_file;
	/* nameserver avaliable via this adapter */
	struct _zfcp_port		*nameserver_port;
	/* error recovery for this adapter and associated devices */
	struct list_head		erp_ready_head;
	struct list_head		erp_running_head;
	rwlock_t			erp_lock;
	struct semaphore		erp_ready_sem;
	wait_queue_head_t		erp_thread_wqh;
	wait_queue_head_t		erp_done_wqh;
	/* error recovery action pending for this adapter (if any) */
	struct _zfcp_erp_action		erp_action;
	atomic_t                        erp_counter;
	debug_info_t                    *erp_dbf;
	debug_info_t			*abort_dbf;
	debug_info_t                    *req_dbf;
	debug_info_t			*in_els_dbf;
	debug_info_t			*cmd_dbf;
	rwlock_t			cmd_dbf_lock;
	zfcp_adapter_mem_pool_t		pool;
	/* SCSI error recovery watch */
	struct timer_list               scsi_er_timer;
	/* Used to handle buffer positioning when reopening queues*/
	atomic_t                        reqs_in_progress;
#ifdef ZFCP_ERP_DEBUG_SINGLE_STEP
	struct				semaphore erp_continue_sem;
#endif /* ZFCP_ERP_DEBUG_SINGLE_STEP */
#ifdef ZFCP_STAT_REQSIZES
	struct list_head		read_req_head;
	struct list_head		write_req_head;
	rwlock_t			stat_lock;
	atomic_t			stat_errors;
	atomic_t			stat_on;
#endif
#ifdef ZFCP_STAT_QUEUES
	atomic_t                        outbound_queue_full;
	atomic_t			outbound_total;
#endif
} zfcp_adapter_t;

/* driver data */
typedef struct _zfcp_data {
	/* SCSI stack data structure storing information about this driver */
	Scsi_Host_Template		scsi_host_template;
	/* head of adapter list */
	struct list_head		adapter_list_head;
	/* lock for critical operations on list of adapters */
	rwlock_t			adapter_list_lock;
	/* number of adapters in list */
	u16				adapters;
	/* data used for dynamic I/O */
	devreg_t			devreg;
	devreg_t			devreg_priv;
	/* driver version number derived from cvs revision */
	u32				driver_version;
	/* serialises proc-fs/configuration changes */
	struct semaphore                proc_sema;
	/* for proc_info */
	char                            *proc_buffer_parm;
	char                            *proc_buffer_map;
	char                            *proc_line;
	unsigned long                   proc_line_length;
	/* and here for the extra proc_dir */
	struct proc_dir_entry		*proc_dir;
	struct proc_dir_entry 		*parm_proc_file;
	struct proc_dir_entry 		*map_proc_file;
	struct proc_dir_entry 		*add_map_proc_file;
	/* buffer for parse error messages (don't want to put it on stack) */
	unsigned char 			perrbuf[ZFCP_PARSE_ERR_BUF_SIZE];
	atomic_t                        mem_count;
	struct notifier_block		reboot_notifier;
	atomic_t			loglevel;
#ifdef ZFCP_LOW_MEM_CREDITS
	atomic_t			lowmem_credit;
#endif
	/* no extra lock here, we have the proc_sema */
	struct list_head		map_list_head;
} zfcp_data_t;

/* struct used by memory pools for fsf_requests */
struct zfcp_fsf_req_pool_buffer {
	struct _zfcp_fsf_req fsf_req;
	struct fsf_qtcb qtcb;
};

/* record generated from parsed conf. lines */
typedef struct _zfcp_config_record {
	int			valid;
	unsigned long		devno;
	unsigned long		scsi_id;
	unsigned long long	wwpn;
	unsigned long		scsi_lun;
	unsigned long long	fcp_lun;
} zfcp_config_record_t;

extern zfcp_data_t zfcp_data;

#ifdef ZFCP_LOW_MEM_CREDITS
/* only substract i from v if v is not equal to no_sub; returns 0 then, 1 otherwise */
static __inline__ int atomic_test_and_sub(int no_sub, int i, atomic_t *v)
{
        int old_val, new_val;
	do {
		old_val = atomic_read(v);
		if (old_val == no_sub)
			return 1;
		new_val = old_val - i;
	} while (atomic_compare_and_swap(old_val, new_val, v));
        return 0;
}

/* only decrement v if v is not equal to no_dec; returns 0 then, 1 otherwise */
static __inline__ int atomic_test_and_dec(int no_dec, atomic_t *v)
{
	return atomic_test_and_sub(no_dec, 1, v);
}
#endif

#ifndef atomic_test_mask
#define atomic_test_mask(mask, target) \
           ((atomic_read(target) & mask) == mask)
#endif

/*
 * Macros used for logging etc.
 */

#define ZFCP_NAME			"zfcp"

/*
 * Logging may be applied on certain kinds of driver operations
 * independently. Besides different log levels are supported for
 * each of these areas.
 */

/* independent areas being subject of logging */
#define ZFCP_LOG_AREA_OTHER	0
#define ZFCP_LOG_AREA_SCSI	1
#define ZFCP_LOG_AREA_FSF	2
#define ZFCP_LOG_AREA_CONFIG	3
#define ZFCP_LOG_AREA_DIO	4
#define ZFCP_LOG_AREA_QDIO	5
#define ZFCP_LOG_AREA_ERP	6

/* values for log level - keep it simple for now */
#define ZFCP_LOG_LEVEL_NORMAL	0
#define ZFCP_LOG_LEVEL_INFO	1
#define ZFCP_LOG_LEVEL_DEBUG	2
#define ZFCP_LOG_LEVEL_TRACE	3

/* default log levels for different log areas */
#define ZFCP_LOG_LEVEL_DEFAULT_OTHER	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_SCSI	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_FSF	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_CONFIG	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_DIO	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_QDIO	ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_LEVEL_DEFAULT_ERP	ZFCP_LOG_LEVEL_NORMAL

/*
 * this allows to remove excluded logs from the code by the preprocessor
 * (this is the last log level compiled in, higher log levels are removed)
 */
#define ZFCP_LOG_LEVEL_LIMIT	ZFCP_LOG_LEVEL_DEBUG

/* nibbles of "loglevel" are used for particular purposes */
#define ZFCP_LOG_VALUE(zfcp_lognibble) \
		((atomic_read(&zfcp_data.loglevel) >> (zfcp_lognibble<<2)) & 0xF)

#define ZFCP_LOG_VALUE_OTHER	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_OTHER)
#define ZFCP_LOG_VALUE_SCSI	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_SCSI)
#define ZFCP_LOG_VALUE_FSF	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_FSF)
#define ZFCP_LOG_VALUE_CONFIG	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_CONFIG)
#define ZFCP_LOG_VALUE_DIO	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_DIO)
#define ZFCP_LOG_VALUE_QDIO	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_QDIO)
#define ZFCP_LOG_VALUE_ERP	ZFCP_LOG_VALUE(ZFCP_LOG_AREA_ERP)

/* all log level defaults put together into log level word */
#define ZFCP_LOG_LEVEL_DEFAULTS \
	((ZFCP_LOG_LEVEL_DEFAULT_OTHER	<< (ZFCP_LOG_AREA_OTHER<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_SCSI	<< (ZFCP_LOG_AREA_SCSI<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_FSF	<< (ZFCP_LOG_AREA_FSF<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_CONFIG	<< (ZFCP_LOG_AREA_CONFIG<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_DIO	<< (ZFCP_LOG_AREA_DIO<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_QDIO	<< (ZFCP_LOG_AREA_QDIO<<2))	| \
	 (ZFCP_LOG_LEVEL_DEFAULT_ERP	<< (ZFCP_LOG_AREA_ERP<<2)))

/* that's the prefix placed at the beginning of each driver message */
#define ZFCP_LOG_PREFIX ZFCP_NAME": "

/* log area specific log prefixes */
#define ZFCP_LOG_AREA_PREFIX_OTHER	""
#define ZFCP_LOG_AREA_PREFIX_SCSI	"SCSI: "
#define ZFCP_LOG_AREA_PREFIX_FSF	"FSF: "
#define ZFCP_LOG_AREA_PREFIX_CONFIG	"config: "
#define ZFCP_LOG_AREA_PREFIX_DIO	"dynamic I/O: "
#define ZFCP_LOG_AREA_PREFIX_QDIO	"QDIO: "
#define ZFCP_LOG_AREA_PREFIX_ERP	"ERP: "

/* check whether we have the right level for logging */
#define ZFCP_LOG_CHECK(ll)	(ZFCP_LOG_VALUE(ZFCP_LOG_AREA)) >= ll

/* As we have two printks it is possible for them to be seperated by another
 * message. This holds true even for printks from within this module.
 * In any case there should only be a small readability hit, however.
 */
#define _ZFCP_LOG(m...) \
		{ \
			printk( "%s%s: ", \
				ZFCP_LOG_PREFIX ZFCP_LOG_AREA_PREFIX, \
				__FUNCTION__); \
			printk(m); \
		}

#define ZFCP_LOG(ll, m...) \
		if (ZFCP_LOG_CHECK(ll)) \
			_ZFCP_LOG(m)
	
#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_NORMAL
#define ZFCP_LOG_NORMAL(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_NORMAL */
#define ZFCP_LOG_NORMAL(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_NORMAL, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_INFO
#define ZFCP_LOG_INFO(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_INFO */
#define ZFCP_LOG_INFO(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_INFO, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_DEBUG
#define ZFCP_LOG_DEBUG(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_DEBUG */
#define ZFCP_LOG_DEBUG(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_DEBUG, m)
#endif

#if ZFCP_LOG_LEVEL_LIMIT < ZFCP_LOG_LEVEL_TRACE
#define ZFCP_LOG_TRACE(m...)
#else	/* ZFCP_LOG_LEVEL_LIMIT >= ZFCP_LOG_LEVEL_TRACE */
#define ZFCP_LOG_TRACE(m...)		ZFCP_LOG(ZFCP_LOG_LEVEL_TRACE, m)
#endif

/**************** memory management wrappers ************************/

#define ZFCP_KMALLOC(params...)		zfcp_kmalloc(params, __FUNCTION__)
#define ZFCP_KFREE(params...)		zfcp_kfree(params, __FUNCTION__)
#define ZFCP_GET_ZEROED_PAGE(params...)	zfcp_get_zeroed_page(params, __FUNCTION__)
#define ZFCP_FREE_PAGE(params...)	zfcp_free_page(params, __FUNCTION__)

static inline void *zfcp_kmalloc(size_t size, int type, char *origin)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        void *ret = NULL;
#if 0
        if (error_counter>=10000) {
                if(error_counter==10000) {
                        printk("********LOW MEMORY********\n");
                }
                error_counter=10001;
                goto out;
        }
#endif

#ifdef ZFCP_LOW_MEM_CREDITS
	if (!atomic_test_and_dec(0, &zfcp_data.lowmem_credit))
		return NULL;
#endif

        ret = kmalloc(size, type);
        if (ret) {
                atomic_add(size, &zfcp_data.mem_count);
		memset(ret, 0, size);
        }
#ifdef ZFCP_MEMORY_DEBUG
	/* FIXME(design): shouldn't this rather be a dbf entry? */
        ZFCP_LOG_NORMAL(
		"origin: %s, addr=0x%lx, size=%li, type=%d\n",
		origin,
		(unsigned long)ret,
		size,
		type);
#endif
        /* out: */
        return ret;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static inline unsigned long zfcp_get_zeroed_page(int flags, char *origin) 
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        unsigned long ret = 0;
#if 0
        if (error_counter>=10000) {
                if(error_counter==10000) {
                        printk("********LOW MEMORY********\n");
                }
                error_counter=10001;
                goto out;
        }
#endif
        ret = get_zeroed_page(flags);
        if (ret) {
                atomic_add(PAGE_SIZE, &zfcp_data.mem_count);
        }

#ifdef ZFCP_MEMORY_DEBUG
	/* FIXME(design): shouldn't this rather be a dbf entry? */
        ZFCP_LOG_NORMAL(
		"origin=%s, addr=0x%lx, type=%d\n",
		origin,
		ret,
		flags);
#endif
        /* out :*/
        return ret;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * Note:
 * 'kfree' may free a different amount of storage than specified here by
 * 'size' since 'kfree' has its own means to figure this number out.
 * Thus, an arbitrary value assigned to 'size' (usage error) will
 * mess up our storage accounting even in cases of no memory leaks.
 */
static inline void zfcp_kfree(void *addr, size_t size, char *origin) 
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        atomic_sub(size, &zfcp_data.mem_count);
#ifdef ZFCP_MEMORY_DEBUG
	/* FIXME(design): shouldn't this rather be a dbf entry? */
        ZFCP_LOG_NORMAL(
		"origin: %s, addr=0x%lx, count=%ld \n",
		origin,
		(unsigned long)addr,
                size);
#endif
        kfree(addr);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static inline void zfcp_free_page(unsigned long addr, char *origin) 
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        atomic_sub(PAGE_SIZE, &zfcp_data.mem_count);
#ifdef ZFCP_MEMORY_DEBUG
        ZFCP_LOG_NORMAL("origin: %s, addr=0x%lx\n",
                        origin,
                        addr);
#endif
        free_page(addr);
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

int	zfcp_config_parse_record_add (zfcp_config_record_t*);

#define ZFCP_FIRST_ENTITY(head,type) \
	( \
		list_empty(head) ? \
		NULL : \
		list_entry((head)->next,type,list) \
	)

#define ZFCP_LAST_ENTITY(head,type) \
	( \
		list_empty(head) ? \
		NULL : \
		list_entry((head)->prev,type,list) \
	)

#define ZFCP_PREV_ENTITY(head,curr,type) \
	( \
		(curr == ZFCP_FIRST_ENTITY(head,type)) ? \
		NULL : \
		list_entry(curr->list.prev,type,list) \
	)

#define ZFCP_NEXT_ENTITY(head,curr,type) \
	( \
		(curr == ZFCP_LAST_ENTITY(head,type)) ? \
		NULL : \
		list_entry(curr->list.next,type,list) \
	)

#define ZFCP_FOR_EACH_ENTITY(head,curr,type) \
	for (curr = ZFCP_FIRST_ENTITY(head,type); \
	     curr; \
	     curr = ZFCP_NEXT_ENTITY(head,curr,type)) 

/*
 * use these macros if you traverse a list and stop iterations after
 * altering the list since changing the list will most likely cause
 * next/previous pointers to become unavailable,
 * usually: examining some list elements, or removing a single
 * element from somewhere in the middle of the list,
 * lock the list by means of the associated rwlock before entering
 * the loop and thus above the macro,
 * unlock the list (the associated rwlock) after leaving the loop
 * belonging to the macro,
 * use read variant of lock if only looking up something without
 * changing the list,
 * use write variant of lock if changing the list (in last iteration !),
 * attention: "upgrading" read lock to write lock is not supported!
 */

#define ZFCP_FOR_EACH_ADAPTER(a) \
	ZFCP_FOR_EACH_ENTITY(&zfcp_data.adapter_list_head,(a),zfcp_adapter_t)

#define ZFCP_FOR_EACH_PORT(a,p) \
	ZFCP_FOR_EACH_ENTITY(&(a)->port_list_head,(p),zfcp_port_t)

#define ZFCP_FOR_EACH_UNIT(p,u) \
	ZFCP_FOR_EACH_ENTITY(&(p)->unit_list_head,(u),zfcp_unit_t)


/* Note, the leftmost status byte is common among adapter, port 
	 and unit
 */
#define ZFCP_COMMON_FLAGS                               0xff000000
#define ZFCP_SPECIFIC_FLAGS                             0x00ffffff

/* common status bits */
#define ZFCP_STATUS_COMMON_TO_BE_REMOVED		0x80000000 
#define ZFCP_STATUS_COMMON_RUNNING			0x40000000 
#define ZFCP_STATUS_COMMON_ERP_FAILED			0x20000000
#define ZFCP_STATUS_COMMON_UNBLOCKED			0x10000000
#define ZFCP_STATUS_COMMON_OPENING                      0x08000000
#define ZFCP_STATUS_COMMON_OPEN                         0x04000000
#define ZFCP_STATUS_COMMON_CLOSING                      0x02000000
#define ZFCP_STATUS_COMMON_ERP_INUSE			0x01000000

/* status of adapter */
#define ZFCP_STATUS_ADAPTER_IRQOWNER			0x00000001
#define ZFCP_STATUS_ADAPTER_QDIOUP			0x00000002
#define ZFCP_STATUS_ADAPTER_REGISTERED			0x00000004
#define ZFCP_STATUS_ADAPTER_XCONFIG_OK			0x00000008
#define ZFCP_STATUS_ADAPTER_HOST_CON_INIT		0x00000010
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_UP		0x00000020
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_DONE		0x00000040
#define ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL		0x00000080
#define ZFCP_STATUS_ADAPTER_ERP_PENDING			0x00000100
#define ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED		0x00000200

#define ZFCP_STATUS_ADAPTER_SCSI_UP			\
		(ZFCP_STATUS_COMMON_UNBLOCKED |	\
		 ZFCP_STATUS_ADAPTER_REGISTERED)

#define ZFCP_DID_NAMESERVER				0xFFFFFC

/* status of remote port */
#define ZFCP_STATUS_PORT_PHYS_OPEN			0x00000001
#define ZFCP_STATUS_PORT_DID_DID			0x00000002
#define ZFCP_STATUS_PORT_PHYS_CLOSING			0x00000004
#define ZFCP_STATUS_PORT_NO_WWPN			0x00000008
#define ZFCP_STATUS_PORT_NO_SCSI_ID			0x00000010
#define ZFCP_STATUS_PORT_INVALID_WWPN			0x00000020

#define ZFCP_STATUS_PORT_NAMESERVER \
		(ZFCP_STATUS_PORT_NO_WWPN | \
		 ZFCP_STATUS_PORT_NO_SCSI_ID)

/* status of logical unit */
#define ZFCP_STATUS_UNIT_NOTSUPPUNITRESET		0x00000001
#define ZFCP_STATUS_UNIT_ASSUMETCQ			0x00000002

/* no common part here */
/* status of FSF request */
#define ZFCP_STATUS_FSFREQ_NOT_INIT			0x00000000
#define ZFCP_STATUS_FSFREQ_POOL  			0x00000001
#define ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT		0x00000002
#define ZFCP_STATUS_FSFREQ_COMPLETED			0x00000004
#define ZFCP_STATUS_FSFREQ_ERROR			0x00000008
#define ZFCP_STATUS_FSFREQ_CLEANUP			0x00000010
#define ZFCP_STATUS_FSFREQ_ABORTING			0x00000020
#define ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED		0x00000040
#define ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED		0x00000080
#define ZFCP_STATUS_FSFREQ_ABORTED			0x00000100
#define ZFCP_STATUS_FSFREQ_TMFUNCFAILED			0x00000200
#define ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP		0x00000400
#define ZFCP_STATUS_FSFREQ_RETRY			0x00000800
#define ZFCP_STATUS_FSFREQ_DISMISSED			0x00001000
#define ZFCP_STATUS_FSFREQ_POOLBUF			0x00002000

#define ZFCP_KNOWN                              0x00000001
#define ZFCP_REQ_AUTO_CLEANUP			0x00000002
#define ZFCP_WAIT_FOR_SBAL			0x00000004
#define ZFCP_REQ_USE_MEMPOOL			0x00000008

/* Mask parameters */
#define ZFCP_SET                                0x00000100
#define ZFCP_CLEAR                              0x00000200

#define ZFCP_INTERRUPTIBLE			1
#define ZFCP_UNINTERRUPTIBLE			0

#define ZFCP_MAX_ERPS                           3

#define ZFCP_ERP_FSFREQ_TIMEOUT			(100 * HZ)
#define ZFCP_ERP_MEMWAIT_TIMEOUT		HZ

#define ZFCP_STATUS_ERP_TIMEDOUT		0x10000000
#define ZFCP_STATUS_ERP_CLOSE_ONLY		0x01000000
#define ZFCP_STATUS_ERP_DISMISSING		0x00100000
#define ZFCP_STATUS_ERP_DISMISSED		0x00200000

#define ZFCP_ERP_STEP_UNINITIALIZED		0x00000000
#define ZFCP_ERP_STEP_FSF_XCONFIG		0x00000001
#define ZFCP_ERP_STEP_PHYS_PORT_CLOSING		0x00000010
#define ZFCP_ERP_STEP_PORT_CLOSING		0x00000100
#define ZFCP_ERP_STEP_NAMESERVER_OPEN		0x00000200
#define ZFCP_ERP_STEP_NAMESERVER_LOOKUP		0x00000400
#define ZFCP_ERP_STEP_PORT_OPENING		0x00000800
#define ZFCP_ERP_STEP_UNIT_CLOSING		0x00001000
#define ZFCP_ERP_STEP_UNIT_OPENING		0x00002000

/* ordered ! */
#define ZFCP_ERP_ACTION_REOPEN_ADAPTER		0x4
#define ZFCP_ERP_ACTION_REOPEN_PORT_FORCED	0x3
#define ZFCP_ERP_ACTION_REOPEN_PORT		0x2
#define ZFCP_ERP_ACTION_REOPEN_UNIT		0x1

#define ZFCP_ERP_ACTION_RUNNING			0x1
#define ZFCP_ERP_ACTION_READY			0x2

#define ZFCP_ERP_SUCCEEDED	0x0
#define ZFCP_ERP_FAILED		0x1
#define ZFCP_ERP_CONTINUES	0x2
#define ZFCP_ERP_EXIT		0x3
#define ZFCP_ERP_DISMISSED	0x4
#define ZFCP_ERP_NOMEM		0x5

/* task attribute values in FCP-2 FCP_CMND IU */
#define SIMPLE_Q	0
#define HEAD_OF_Q	1
#define ORDERED_Q	2
#define ACA_Q		4
#define UNTAGGED	5

/* task management flags in FCP-2 FCP_CMND IU */
#define CLEAR_ACA		0x40
#define TARGET_RESET		0x20
#define LOGICAL_UNIT_RESET	0x10
#define CLEAR_TASK_SET		0x04
#define ABORT_TASK_SET		0x02

#define FCP_CDB_LENGTH		16


/* some magics which may be used to authenticate data structures */
#define ZFCP_MAGIC		0xFCFCFCFC
#define ZFCP_MAGIC_ADAPTER	0xAAAAAAAA
#define ZFCP_MAGIC_PORT		0xBBBBBBBB
#define ZFCP_MAGIC_UNIT		0xCCCCCCCC
#define ZFCP_MAGIC_FSFREQ	0xEEEEEEEE

/* function prototypes */
int zfcp_erp_wait(zfcp_adapter_t*);
int zfcp_fsf_exchange_port_data(zfcp_adapter_t*, fsf_qtcb_bottom_port_t*);
int zfcp_fsf_send_els(struct zfcp_send_els *);
int zfcp_fsf_send_els_sync(struct zfcp_send_els *);
int zfcp_config_parse_record_add(zfcp_config_record_t*);
int zfcp_scsi_command_sync(zfcp_unit_t *, Scsi_Cmnd *);
int zfcp_ns_ga_nxt_request(zfcp_port_t *, struct ct_iu_ga_nxt *);
int zfcp_fsf_send_ct(struct zfcp_send_ct *, zfcp_mem_pool_t *,
		     zfcp_erp_action_t *);

#endif /* _ZFCP_H_ */
