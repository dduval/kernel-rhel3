/*
 * FCP adapter driver for IBM eServer zSeries
 *
 * (C) Copyright IBM Corp. 2002, 2003
 *
 * Authors:
 *	Martin Peschke <mpeschke@de.ibm.com>
 *      Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *	Aron Zeh
 *      Wolfgang Taphorn
 *      Stefan Bader <stefan.bader@de.ibm.com>
 *      Andreas Herrmann <aherrman@de.ibm.com>
 *      Stefan Voelkel <Stefan.Voelkel@millenux.com>
 */

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_REVISION		"$Revision: 5.31.2.11 $"

#define ZFCP_QTCB_VERSION	FSF_QTCB_CURRENT_VERSION

#define ZFCP_PRINT_FLAGS

#undef ZFCP_CAUSE_ERRORS

#undef ZFCP_MEMORY_DEBUG

#undef ZFCP_MEM_POOL_ONLY

#define ZFCP_DEBUG_REQUESTS	/* fsf_req tracing */

#define ZFCP_DEBUG_COMMANDS	/* host_byte tracing */

#define ZFCP_DEBUG_ABORTS	/* scsi_cmnd abort tracing */

#define ZFCP_DEBUG_INCOMING_ELS	/* incoming ELS tracing */

#undef ZFCP_RESID

#define ZFCP_STAT_REQSIZES
#define ZFCP_STAT_QUEUES

// current implementation does not work due to proc_sema
#undef ZFCP_ERP_DEBUG_SINGLE_STEP

#ifdef ZFCP_CAUSE_ERRORS
struct timer_list zfcp_force_error_timer;
#endif

/* ATTENTION: value must not be used by hardware */
#define FSF_QTCB_UNSOLICITED_STATUS		0x6305

#define ZFCP_FAKE_SCSI_COMPLETION_TIME	        (HZ / 3)

#define ZFCP_SCSI_LOW_MEM_TIMEOUT               (100*HZ)

#define ZFCP_SCSI_ER_TIMEOUT                    (100*HZ)

#define ZFCP_SCSI_RETRY_TIMEOUT			(120*HZ)

/********************* QDIO SPECIFIC DEFINES *********************************/

/* allow as much chained SBALs as supported by hardware */
#define ZFCP_MAX_SBALS_PER_REQ		FSF_MAX_SBALS_PER_REQ
#define ZFCP_MAX_SBALS_PER_CT_REQ	FSF_MAX_SBALS_PER_REQ
#define ZFCP_MAX_SBALS_PER_ELS_REQ	FSF_MAX_SBALS_PER_ELS_REQ
/* DMQ bug workaround: don't use last SBALE */
#define ZFCP_MAX_SBALES_PER_SBAL	(QDIO_MAX_ELEMENTS_PER_BUFFER - 1)
/* index of last SBALE (with respect to DMQ bug workaround) */
#define ZFCP_LAST_SBALE_PER_SBAL	(ZFCP_MAX_SBALES_PER_SBAL - 1)
/* max. number of (data buffer) SBALEs in largest SBAL chain */
#define ZFCP_MAX_SBALES_PER_REQ		\
	(ZFCP_MAX_SBALS_PER_REQ * ZFCP_MAX_SBALES_PER_SBAL \
	 - 2)	/* request ID + QTCB in SBALE 0 + 1 of first SBAL in chain */

/* FIXME(tune): free space should be one max. SBAL chain plus what? */
#define ZFCP_QDIO_PCI_INTERVAL		(QDIO_MAX_BUFFERS_PER_Q - (ZFCP_MAX_SBALS_PER_REQ + 4))

#define ZFCP_SBAL_TIMEOUT (5 * HZ)

#define ZFCP_STATUS_READ_FAILED_THRESHOLD	3

/* parsing stuff */
#define ZFCP_PARSE_SPACE_CHARS		" \t"
#define ZFCP_PARSE_RECORD_DELIM_CHARS	";\n"
#define ZFCP_PARSE_DELIM_CHARS		":"
#define ZFCP_PARSE_COMMENT_CHARS	"#"
#define ZFCP_PARSE_ADD		1
#define ZFCP_PARSE_DEL		0

#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/time.h>

#include <linux/ioctl.h>
#include <linux/major.h>
#include <linux/miscdevice.h>

#include "../../fc4/fc.h"

#include <linux/module.h>

#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <asm/ebcdic.h>
#include <asm/cpcmd.h>               /* Debugging only */
#include <asm/processor.h>           /* Debugging only */
#include <asm/div64.h>
#include <asm/ebcdic.h>

#include "zfcp.h"

/* Cosmetics */
#define ZFCP_FSFREQ_CLEANUP_TIMEOUT	HZ/10

#define ZFCP_TYPE2_RECOVERY_TIME        8*HZ

#ifdef ZFCP_STAT_REQSIZES
#define ZFCP_MAX_PROC_SIZE              3 * PAGE_SIZE
#else
#define ZFCP_MAX_PROC_SIZE              PAGE_SIZE
#endif

#define ZFCP_64BIT			(BITS_PER_LONG == 64)
#define ZFCP_31BIT			(!ZFCP_64BIT)

#define QDIO_SCSI_QFMT			1	/* 1 for FSF */

/* queue polling (values in microseconds) */
#define ZFCP_MAX_INPUT_THRESHOLD 	5000	/* FIXME: tune */
#define ZFCP_MAX_OUTPUT_THRESHOLD 	1000	/* FIXME: tune */
#define ZFCP_MIN_INPUT_THRESHOLD 	1	/* ignored by QDIO layer */
#define ZFCP_MIN_OUTPUT_THRESHOLD 	1	/* ignored by QDIO layer */

#define ZFCP_PARM_FILE                  "mod_parm"
#define ZFCP_MAP_FILE                   "map"
#define ZFCP_ADD_MAP_FILE               "add_map"
#define ZFCP_STATUS_FILE                "status"
#define ZFCP_MAX_PROC_LINE              1024

#define ZFCP_RESET_ERP			"reset erp"
#define ZFCP_SET_OFFLINE		"set offline"
#define ZFCP_SET_ONLINE			"set online"
#define ZFCP_RTV			"rtv"
#define ZFCP_RLS			"rls"
#define ZFCP_PDISC			"pdisc"
#define ZFCP_ADISC			"adisc"
#define ZFCP_STAT_ON			"stat on"
#define ZFCP_STAT_OFF			"stat off"
#define ZFCP_STAT_RESET			"stat reset"

#define ZFCP_DID_MASK                   0x00ffffff

/* Adapter Identification Parameters */
#define ZFCP_CONTROL_UNIT_TYPE	0x1731
#define ZFCP_CONTROL_UNIT_MODEL	0x03
#define ZFCP_DEVICE_TYPE	0x1732
#define ZFCP_DEVICE_MODEL	0x03
#define ZFCP_DEVICE_MODEL_PRIV	0x04
 
#define ZFCP_FC_SERVICE_CLASS_DEFAULT	FSF_CLASS_3

/* timeout for name-server lookup (in seconds) */
/* FIXME(tune) */
#define ZFCP_NS_GID_PN_TIMEOUT		10
#define ZFCP_NS_GA_NXT_TIMEOUT		120

#define ZFCP_EXCHANGE_CONFIG_DATA_RETRIES	6
#define ZFCP_EXCHANGE_CONFIG_DATA_SLEEP		50

#define ZFCP_STATUS_READS_RECOM		FSF_STATUS_READS_RECOM

/* largest SCSI command we can process */
/* FCP-2 (FCP_CMND IU) allows up to (255-3+16) */
#define ZFCP_MAX_SCSI_CMND_LENGTH	255
/* maximum number of commands in LUN queue */
#define ZFCP_CMND_PER_LUN               32

/* debug feature entries per adapter */
#define ZFCP_ERP_DBF_INDEX	1 
#define ZFCP_ERP_DBF_AREAS	2
#define ZFCP_ERP_DBF_LENGTH	16
#define ZFCP_ERP_DBF_LEVEL	3
#define ZFCP_ERP_DBF_NAME	"zfcp_erp"

#define ZFCP_REQ_DBF_INDEX	1
#define ZFCP_REQ_DBF_AREAS	1
#define ZFCP_REQ_DBF_LENGTH	8
#define ZFCP_REQ_DBF_LEVEL	DEBUG_OFF_LEVEL
#define ZFCP_REQ_DBF_NAME	"zfcp_req"

#define ZFCP_CMD_DBF_INDEX	2
#define ZFCP_CMD_DBF_AREAS	1
#define ZFCP_CMD_DBF_LENGTH	8
#define ZFCP_CMD_DBF_LEVEL	3
#define ZFCP_CMD_DBF_NAME	"zfcp_cmd"

#define ZFCP_ABORT_DBF_INDEX	2
#define ZFCP_ABORT_DBF_AREAS	1
#define ZFCP_ABORT_DBF_LENGTH	8
#define ZFCP_ABORT_DBF_LEVEL	6
#define ZFCP_ABORT_DBF_NAME	"zfcp_abt"

#define ZFCP_IN_ELS_DBF_INDEX	2
#define ZFCP_IN_ELS_DBF_AREAS	1
#define ZFCP_IN_ELS_DBF_LENGTH	8
#define ZFCP_IN_ELS_DBF_LEVEL	6
#define ZFCP_IN_ELS_DBF_NAME	"zfcp_els"

/*
 * paranoia: some extra checks ensuring driver consistency and probably
 * reducing performance,
 * should be compiled in and defined per default during development
 * should be compiled in and disabled per default in beta program
 * should not be compiled in field
 */

/* do (not) compile in paranoia code (by means of "dead code") */
#undef ZFCP_PARANOIA_DEAD_CODE 

/* enable/disable paranoia checks if compiled in */
#define ZFCP_PARANOIA_PER_DEFAULT

/* paranoia status (override by means of module parameter allowed) */
#ifdef ZFCP_PARANOIA_PER_DEFAULT
unsigned char zfcp_paranoia = 1;
#else
unsigned char zfcp_paranoia = 0;
#endif

/*
 * decide whether paranoia checks are (1) dead code,
 * (2) active code + disabled, or (3) active code + enabled
 */
#ifdef ZFCP_PARANOIA_DEAD_CODE
#define ZFCP_PARANOIA		if (0)
#else
#define ZFCP_PARANOIA		if (zfcp_paranoia)
#endif

/* association between FSF command and FSF QTCB type */
static u32 fsf_qtcb_type[] = {
  [ FSF_QTCB_FCP_CMND ]			= FSF_IO_COMMAND,
  [ FSF_QTCB_ABORT_FCP_CMND ]		= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_OPEN_PORT_WITH_DID ]	= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_OPEN_LUN ]			= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_CLOSE_LUN ]		= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_CLOSE_PORT ]		= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_CLOSE_PHYSICAL_PORT ]	= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_SEND_ELS ]			= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_SEND_GENERIC ]		= FSF_SUPPORT_COMMAND,
  [ FSF_QTCB_EXCHANGE_CONFIG_DATA ]	= FSF_CONFIG_COMMAND,
};

/* accumulated log level (module parameter) */
static u32 loglevel = ZFCP_LOG_LEVEL_DEFAULTS;

unsigned long debug_addr;
unsigned long debug_len;

const char zfcp_topologies[5][25] = {
	{"<error>"},
	{"point-to-point"},
	{"fabric"}, 
	{"arbitrated loop"},
	{"fabric (virt. adapter)"}
};

const char zfcp_act_subtable_type[5][8] = {
	{"unknown"}, {"OS"}, {"WWPN"}, {"DID"}, {"LUN"}
};

inline void _zfcp_hex_dump(char *addr, int count)
{
	int i;
       	for (i = 0; i < count; i++) {
       	        printk("%02x", addr[i]);
        	if ((i % 4) == 3)
       	        	printk(" ");
               	if ((i % 32) == 31)
                	printk("\n");
	}
	if ((i % 32) != 31)
		printk("\n");
}
 
#define ZFCP_HEX_DUMP(level, addr, count) \
		if (ZFCP_LOG_CHECK(level)) { \
			_zfcp_hex_dump(addr, count); \
		}

static int proc_debug=1;

/*
 * buffer struct used for private_data entry (proc interface)
 */

typedef struct { 
     int len;
     char *buf;
} procbuf_t;


/*
 * not yet optimal but useful:
 * waits until condition is met or timeout is expired,
 * condition might be a function call which allows to
 * execute some additional instructions aside from check
 * (e.g. get a lock without race if condition is met),
 * timeout is modified and holds the remaining time,
 * thus timeout is zero if timeout is expired,
 * result value zero indicates that condition has not been met
 */
#define __ZFCP_WAIT_EVENT_TIMEOUT(timeout, condition) \
do { \
	set_current_state(TASK_UNINTERRUPTIBLE); \
	while (!(condition) && timeout) \
		timeout = schedule_timeout(timeout); \
	current->state = TASK_RUNNING; \
} while (0);

#define ZFCP_WAIT_EVENT_TIMEOUT(waitqueue, timeout, condition) \
do { \
	wait_queue_t entry; \
	init_waitqueue_entry(&entry, current); \
	add_wait_queue(&waitqueue, &entry); \
	__ZFCP_WAIT_EVENT_TIMEOUT(timeout, condition) \
	remove_wait_queue(&waitqueue, &entry); \
} while (0);


/* General type defines */

typedef long long unsigned int  llui_t;

/* QDIO request identifier */
typedef u64	qdio_reqid_t;


/* FCP(-2) FCP_CMND IU */
typedef struct _fcp_cmnd_iu {
	/* FCP logical unit number */
	fcp_lun_t	fcp_lun;
	/* command reference number */
	u8		crn;
	/* reserved */
	u8		reserved0:5;
	/* task attribute */
	u8		task_attribute:3;
	/* task management flags */
	u8		task_management_flags;
	/* additional FCP_CDB length */
	u8		add_fcp_cdb_length:6;
	/* read data */
	u8		rddata:1;
	/* write data */
	u8		wddata:1;
	/* */
	u8		fcp_cdb[FCP_CDB_LENGTH];
	/* variable length fields (additional FCP_CDB, FCP_DL) */
} __attribute__((packed)) fcp_cmnd_iu_t;

/* data length field may be at variable position in FCP-2 FCP_CMND IU */
typedef u32	fcp_dl_t;


#define RSP_CODE_GOOD			0
#define RSP_CODE_LENGTH_MISMATCH	1
#define RSP_CODE_FIELD_INVALID		2
#define RSP_CODE_RO_MISMATCH		3
#define RSP_CODE_TASKMAN_UNSUPP		4
#define RSP_CODE_TASKMAN_FAILED		5

/* see fc-fs */
#define LS_FAN 0x60000000
#define LS_RSCN 0x61040000

typedef struct _fcp_rscn_head {
        u8  command;
        u8  page_length; /* always 0x04 */
        u16 payload_len;
} __attribute__((packed)) fcp_rscn_head_t;

typedef struct _fcp_rscn_element {
        u8  reserved:2;
        u8  event_qual:4;
        u8  addr_format:2;
        u32 nport_did:24;
} __attribute__((packed)) fcp_rscn_element_t;

#define ZFCP_PORT_ADDRESS   0x0
#define ZFCP_AREA_ADDRESS   0x1
#define ZFCP_DOMAIN_ADDRESS 0x2
#define ZFCP_FABRIC_ADDRESS 0x3

#define ZFCP_PORTS_RANGE_PORT   0xFFFFFF
#define ZFCP_PORTS_RANGE_AREA   0xFFFF00
#define ZFCP_PORTS_RANGE_DOMAIN 0xFF0000
#define ZFCP_PORTS_RANGE_FABRIC 0x000000

#define ZFCP_NO_PORTS_PER_AREA    0x100
#define ZFCP_NO_PORTS_PER_DOMAIN  0x10000
#define ZFCP_NO_PORTS_PER_FABRIC  0x1000000

typedef struct _fcp_fan {
        u32 command;
        u32 fport_did;
        wwn_t fport_wwpn;
        wwn_t fport_wwname;
} __attribute__((packed)) fcp_fan_t;

/* see fc-ph */
typedef struct _fcp_logo {
        u32 command;
        u32 nport_did;
        wwn_t nport_wwpn;
} __attribute__((packed)) fcp_logo_t;


/* FCP(-2) FCP_RSP IU */
typedef struct _fcp_rsp_iu {
	/* reserved */
	u8		reserved0[10];
	union {
		struct {
			/* reserved */
			u8		reserved1:3;
			/* */
			u8		fcp_conf_req:1;
			/* */
			u8		fcp_resid_under:1;
			/* */
			u8		fcp_resid_over:1;
			/* */
			u8		fcp_sns_len_valid:1;
			/* */
			u8		fcp_rsp_len_valid:1;
		} bits;
		u8		value;
	} validity;
	/* */
	u8		scsi_status;
	/* */
	u32		fcp_resid;
	/* */
	u32		fcp_sns_len;
	/* */
	u32		fcp_rsp_len;
	/* variable length fields: FCP_RSP_INFO, FCP_SNS_INFO */
} __attribute__((packed)) fcp_rsp_iu_t;


inline char *zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu_t *fcp_rsp_iu)
{
        char *fcp_rsp_info_ptr = NULL;
        fcp_rsp_info_ptr=
                (unsigned char*)fcp_rsp_iu + (sizeof(fcp_rsp_iu_t));

        return fcp_rsp_info_ptr;
}


inline char *zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu_t *fcp_rsp_iu)
{
	char *fcp_sns_info_ptr = NULL;
        fcp_sns_info_ptr =
                (unsigned char*)fcp_rsp_iu + (sizeof(fcp_rsp_iu_t));
          // NOTE:fcp_rsp_info is really only a part of the whole as 
          // defined in FCP-2 documentation
        if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid) 
                fcp_sns_info_ptr = (char *)fcp_sns_info_ptr +
                        fcp_rsp_iu->fcp_rsp_len;

	return fcp_sns_info_ptr;
}

#ifdef ZFCP_STAT_REQSIZES
typedef struct _zfcp_statistics {
        struct list_head list;
        u32 num;
        u32 hits;
} zfcp_statistics_t;
#endif


/*********************** LIST DEFINES ****************************************/

#define ZFCP_FIRST_ADAPTER \
	ZFCP_FIRST_ENTITY(&zfcp_data.adapter_list_head,zfcp_adapter_t)

#define ZFCP_FIRST_PORT(a) \
	ZFCP_FIRST_ENTITY(&(a)->port_list_head,zfcp_port_t)

#define ZFCP_FIRST_UNIT(p) \
	ZFCP_FIRST_ENTITY(&(p)->unit_list_head,zfcp_unit_t)

#define ZFCP_FIRST_SCSIREQ(a) \
	ZFCP_FIRST_ENTITY(&(a)->scsi_req_list_head,zfcp_scsi_req_t)

#define ZFCP_FIRST_FSFREQ(a) \
	ZFCP_FIRST_ENTITY(&(a)->fsf_req_list_head,zfcp_fsf_req_t)

#define ZFCP_LAST_ADAPTER \
	ZFCP_LAST_ENTITY(&zfcp_data.adapter_list_head,zfcp_adapter_t)

#define ZFCP_LAST_PORT(a) \
	ZFCP_LAST_ENTITY(&(a)->port_list_head,zfcp_port_t)

#define ZFCP_LAST_UNIT(p) \
	ZFCP_LAST_ENTITY(&(p)->unit_list_head,zfcp_unit_t)

#define ZFCP_LAST_SCSIREQ(a) \
	ZFCP_LAST_ENTITY(&(a)->scsi_req_list_head,zfcp_scsi_req_t)

#define ZFCP_LAST_FSFREQ(a) \
	ZFCP_LAST_ENTITY(&(a)->fsf_req_list_head,zfcp_fsf_req_t)

#define ZFCP_PREV_ADAPTER(a) \
	ZFCP_PREV_ENTITY(&zfcp_data.adapter_list_head,(a),zfcp_adapter_t)

#define ZFCP_PREV_PORT(p) \
	ZFCP_PREV_ENTITY(&(p)->adapter->port_list_head,(p),zfcp_port_t)

#define ZFCP_PREV_UNIT(u) \
	ZFCP_PREV_ENTITY(&(u)->port->unit_list_head,(u),zfcp_unit_t)

#define ZFCP_PREV_SCSIREQ(s) \
	ZFCP_PREV_ENTITY(&(s)->adapter->scsi_req_list_head,(s),zfcp_scsi_req_t)

#define ZFCP_PREV_FSFREQ(o) \
	ZFCP_PREV_ENTITY(&(o)->adapter->fsf_req_list_head,(o), \
				zfcp_fsf_req_t)

#define ZFCP_NEXT_ADAPTER(a) \
	ZFCP_NEXT_ENTITY(&zfcp_data.adapter_list_head,(a),zfcp_adapter_t)

#define ZFCP_NEXT_PORT(p) \
	ZFCP_NEXT_ENTITY(&(p)->adapter->port_list_head,(p),zfcp_port_t)

#define ZFCP_NEXT_UNIT(u) \
	ZFCP_NEXT_ENTITY(&(u)->port->unit_list_head,(u),zfcp_unit_t)

#define ZFCP_NEXT_SCSIREQ(s) \
	ZFCP_NEXT_ENTITY(&(s)->adapter->scsi_req_list_head,(s),zfcp_scsi_req_t)

#define ZFCP_NEXT_FSFREQ(o) \
	ZFCP_NEXT_ENTITY(&(o)->adapter->fsf_req_list_head,(o), \
				zfcp_fsf_req_t)

#define ZFCP_FOR_EACH_FSFREQ(a,o) \
	ZFCP_FOR_EACH_ENTITY(&(a)->fsf_req_list_head,(o),zfcp_fsf_req_t)

/*
 * use these macros if you traverse a list and do not stop after
 * altering the list,
 * attention: do not modify "tmp" (last) arg during iterations,
 * usually: removing several elements from somewhere in the middle of the list,
 * lock the list by means of the associated rwlock before entering
 * the loop and thus above the macro,
 * unlock the list (the associated rwlock) after leaving the loop
 * belonging to the macro,
 * use write variant of lock
 */

#define ZFCP_FOR_NEXT_ENTITY(head,curr,type,tmp) \
	for (curr = ZFCP_FIRST_ENTITY(head,type), \
	     tmp = ZFCP_NEXT_ENTITY(head,curr,type); \
	     curr; \
	     curr = tmp, \
	     tmp = ZFCP_NEXT_ENTITY(head,curr,type))

#define ZFCP_FOR_NEXT_ADAPTER(a,n) \
	ZFCP_FOR_NEXT_ENTITY(&zfcp_data.adapter_list_head,(a), \
				zfcp_adapter_t,(n))

#define ZFCP_FOR_NEXT_PORT(a,p,n) \
	ZFCP_FOR_NEXT_ENTITY(&(a)->port_list_head,(p),zfcp_port_t,(n))

#define ZFCP_FOR_NEXT_UNIT(p,u,n) \
	ZFCP_FOR_NEXT_ENTITY(&(p)->unit_list_head,(u),zfcp_unit_t,(n))

#define ZFCP_FOR_NEXT_SCSIREQ(a,s,n) \
	ZFCP_FOR_NEXT_ENTITY(&(a)->scsi_req_list_head,(s),zfcp_scsi_req_t,(n))

#define ZFCP_FOR_NEXT_FSFREQ(a,o,n) \
	ZFCP_FOR_NEXT_ENTITY(&(a)->fsf_req_list_head,(o), \
				zfcp_fsf_req_t,(n))

/*
 * use these macros for loops do not walking through lists but*
 * changing the list at their heads,
 * attention: without changing the head of the list or any other
 * break condition this will become an endless loop,
 * next and previous pointers may become invalid !!!
 * usually: removing all elements in lists
 * lock the list by means of the associated rwlock before entering
 * the loop and thus
 * above the macro,
 * unlock the list (the associated rwlock) after leaving the loop
 * belonging to the macro,
 * use write variant of lock
 */
#define ZFCP_WHILE_ENTITY(head,curr,type) \
	while (((curr) = ZFCP_FIRST_ENTITY(head,type)))

#define ZFCP_WHILE_ADAPTER(a) \
	ZFCP_WHILE_ENTITY(&zfcp_data.adapter_list_head,(a),zfcp_adapter_t)

#define ZFCP_WHILE_PORT(a,p) \
	ZFCP_WHILE_ENTITY(&(a)->port_list_head,(p),zfcp_port_t)

#define ZFCP_WHILE_UNIT(p,u) \
	ZFCP_WHILE_ENTITY(&(p)->unit_list_head,(u),zfcp_unit_t)

#define ZFCP_WHILE_SCSIREQ(a,s) \
	ZFCP_WHILE_ENTITY(&(a)->scsi_req_list_head,(s),zfcp_scsi_req_t)

#define ZFCP_WHILE_FSFREQ(a,o) \
	ZFCP_WHILE_ENTITY(&(a)->fsf_req_list_head,(o),zfcp_fsf_req_t)

/* prototypes for functions which could kernel lib functions (but aren't) */
static size_t	strnspn(const char*, const char*, size_t);
char*		strnpbrk(const char*, const char*, size_t);
char*		strnchr(const char*, int, size_t);
char*		strnrchr(const char*, int, size_t);

/* prototypes for functions written against the modul interface */
static int __init	zfcp_module_init(void);
static void __exit	zfcp_module_exit(void);

int zfcp_reboot_handler (struct notifier_block*, unsigned long, void*);

/* prototypes for functions written against the SCSI stack HBA driver interface */
int		zfcp_scsi_detect (Scsi_Host_Template*);
int		zfcp_scsi_revoke (Scsi_Device*);
int		zfcp_scsi_release (struct Scsi_Host*);
int		zfcp_scsi_queuecommand (Scsi_Cmnd*, void (*done)(Scsi_Cmnd*));
int		zfcp_scsi_eh_abort_handler (Scsi_Cmnd*);
int		zfcp_scsi_eh_device_reset_handler (Scsi_Cmnd*);
int		zfcp_scsi_eh_bus_reset_handler (Scsi_Cmnd*);
int		zfcp_scsi_eh_host_reset_handler (Scsi_Cmnd*);
void            zfcp_scsi_select_queue_depth(struct Scsi_Host*, Scsi_Device*);

/* prototypes for functions written against the FSF interface */
static int zfcp_fsf_req_send(zfcp_fsf_req_t*, struct timer_list*);
static int zfcp_fsf_req_create(zfcp_adapter_t *, u32, int, zfcp_mem_pool_t *,
                               unsigned long *, zfcp_fsf_req_t **);
static int zfcp_fsf_req_free(zfcp_fsf_req_t*);
static int	zfcp_fsf_exchange_config_data (zfcp_erp_action_t*);
static int	zfcp_fsf_open_port (zfcp_erp_action_t*);
static int	zfcp_fsf_close_port (zfcp_erp_action_t*);
static int	zfcp_fsf_open_unit (zfcp_erp_action_t*);
static int	zfcp_fsf_close_unit (zfcp_erp_action_t*);
static int	zfcp_fsf_close_physical_port (zfcp_erp_action_t*);
static int	zfcp_fsf_send_fcp_command_task (zfcp_unit_t*, Scsi_Cmnd *);
static zfcp_fsf_req_t*	zfcp_fsf_send_fcp_command_task_management
			(zfcp_adapter_t*, zfcp_unit_t*, u8, int);
static zfcp_fsf_req_t*	zfcp_fsf_abort_fcp_command
			(unsigned long, zfcp_adapter_t*, zfcp_unit_t*, int);
static void zfcp_fsf_start_scsi_er_timer(zfcp_adapter_t*);
static void zfcp_fsf_scsi_er_timeout_handler(unsigned long);
static int	zfcp_fsf_status_read (zfcp_adapter_t*, int);
static int	zfcp_fsf_exchange_config_data_handler
			(zfcp_fsf_req_t*);
static int	zfcp_fsf_open_port_handler (zfcp_fsf_req_t*);
static int      zfcp_fsf_close_port_handler (zfcp_fsf_req_t*);
static int      zfcp_fsf_close_physical_port_handler (zfcp_fsf_req_t*);
static int	zfcp_fsf_open_unit_handler (zfcp_fsf_req_t*);
static int      zfcp_fsf_close_unit_handler (zfcp_fsf_req_t*);
static int	zfcp_fsf_send_fcp_command_handler (zfcp_fsf_req_t*);
static int	zfcp_fsf_send_fcp_command_task_handler
			(zfcp_fsf_req_t*);
static int	zfcp_fsf_send_fcp_command_task_management_handler
			(zfcp_fsf_req_t*);
static int	zfcp_fsf_abort_fcp_command_handler (zfcp_fsf_req_t*);
static int	zfcp_fsf_send_ct_handler (zfcp_fsf_req_t*);
static int      zfcp_fsf_status_read_handler (zfcp_fsf_req_t*);
void		zfcp_fsf_incoming_els (zfcp_fsf_req_t *); 

static inline int
		zfcp_fsf_req_create_sbal_check
			(unsigned long*, zfcp_qdio_queue_t*, int);

static int	zfcp_fsf_send_els_handler(zfcp_fsf_req_t *);

static inline int
		zfcp_mem_pool_element_alloc (zfcp_mem_pool_t*, int);
static inline int
		zfcp_mem_pool_element_free (zfcp_mem_pool_t*, int);
static inline void*
		zfcp_mem_pool_element_get (zfcp_mem_pool_t*, int);
static inline int
		zfcp_mem_pool_element_put (zfcp_mem_pool_t*, int);

static inline int
		zfcp_mem_pool_create (zfcp_mem_pool_t*, int, int,
                                      void (*function)(unsigned long),
                                      unsigned long);
static inline int
		zfcp_mem_pool_destroy (zfcp_mem_pool_t*);
static inline void*
		zfcp_mem_pool_find (zfcp_mem_pool_t*);
static inline int
		zfcp_mem_pool_return (void*, zfcp_mem_pool_t*);



static void	zfcp_scsi_low_mem_buffer_timeout_handler
			(unsigned long);

static zfcp_fsf_req_t* zfcp_fsf_req_alloc(zfcp_mem_pool_t *, int, int);
static int	zfcp_fsf_req_cleanup (zfcp_fsf_req_t*);
static int	zfcp_fsf_req_wait_and_cleanup
			(zfcp_fsf_req_t*, int, u32*);
static int	zfcp_fsf_req_complete (zfcp_fsf_req_t*);
static int	zfcp_fsf_protstatus_eval (zfcp_fsf_req_t*);
static int	zfcp_fsf_fsfstatus_eval (zfcp_fsf_req_t*);
static int      zfcp_fsf_fsfstatus_qual_eval(zfcp_fsf_req_t *);
static int 	zfcp_fsf_req_dispatch (zfcp_fsf_req_t*);
static int	zfcp_fsf_req_dismiss (zfcp_fsf_req_t*);
static int	zfcp_fsf_req_dismiss_all (zfcp_adapter_t*);

/* prototypes for FCP related functions */
static int	zfcp_nameserver_enqueue (zfcp_adapter_t*);
static int zfcp_ns_gid_pn_request(zfcp_erp_action_t*);
static void zfcp_ns_gid_pn_handler(unsigned long);
static void zfcp_ns_ga_nxt_handler(unsigned long);

/* prototypes for functions written against the QDIO layer interface */
qdio_handler_t	zfcp_qdio_request_handler;
qdio_handler_t	zfcp_qdio_response_handler;

int             zfcp_qdio_handler_error_check
			(zfcp_adapter_t*, unsigned int, unsigned int,
			 unsigned int);

/* prototypes for functions written against the Dynamic I/O layer interface */
static int	zfcp_dio_oper_handler (int, devreg_t *);
static void	zfcp_dio_not_oper_handler (int, int);

/* prototypes for functions written against the Common I/O layer interface */
static void	zfcp_cio_handler (int, void *, struct pt_regs *);

/* prototypes for other functions */
static int	zfcp_task_management_function (zfcp_unit_t*, u8);

static void	zfcp_config_parse_error(
			unsigned char *, unsigned char *, const char *, ...)
			__attribute__ ((format (printf, 3, 4)));
static int	zfcp_config_parse_record(
			unsigned char*, int, zfcp_config_record_t*);
static int	zfcp_config_parse_record_list (unsigned char*, int, int);
//static int	zfcp_config_parse_record_del (zfcp_config_record_t*);
static int	zfcp_config_cleanup (void);

static int      zfcp_adapter_enqueue (devno_t, zfcp_adapter_t**);
static int      zfcp_port_enqueue (zfcp_adapter_t*, scsi_id_t, wwn_t, u32, 
                                   zfcp_port_t**);
static int      zfcp_unit_enqueue (zfcp_port_t*, scsi_lun_t, fcp_lun_t,
                                   zfcp_unit_t**);

static int	zfcp_adapter_dequeue (zfcp_adapter_t*);
static int	zfcp_port_dequeue (zfcp_port_t*);
static int	zfcp_unit_dequeue (zfcp_unit_t*);

static int	zfcp_adapter_detect(zfcp_adapter_t*);
static int	zfcp_adapter_irq_register(zfcp_adapter_t*);
static int	zfcp_adapter_irq_unregister(zfcp_adapter_t*);
static int	zfcp_adapter_scsi_register(zfcp_adapter_t*);
static int	zfcp_adapter_scsi_register_all (void);
static int	zfcp_adapter_shutdown_all (void);

static u32	zfcp_derive_driver_version (void);

static inline int
		zfcp_qdio_reqid_check(zfcp_adapter_t*, void*);

static inline void zfcp_qdio_sbal_limit(zfcp_fsf_req_t*, int);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbale_get
		(zfcp_qdio_queue_t*, int, int);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbale_req
		(zfcp_fsf_req_t*, int, int);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbale_resp
		(zfcp_fsf_req_t*, int, int);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbale_curr
		(zfcp_fsf_req_t*);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbal_chain
		(zfcp_fsf_req_t*, unsigned long);
static inline volatile qdio_buffer_element_t*
	zfcp_qdio_sbale_next
		(zfcp_fsf_req_t*, unsigned long);
static inline int
	zfcp_qdio_sbals_zero
		(zfcp_qdio_queue_t*, int, int);
static inline int
	zfcp_qdio_sbals_wipe
		(zfcp_fsf_req_t*);
static inline void
	zfcp_qdio_sbale_fill
		(zfcp_fsf_req_t*, unsigned long, void*, int);
static inline int
	zfcp_qdio_sbals_from_segment
		(zfcp_fsf_req_t*, unsigned long, void*, unsigned long);
static inline int zfcp_qdio_sbals_from_buffer(zfcp_fsf_req_t *, unsigned long,
                                              void *, unsigned long, int);
static inline int zfcp_qdio_sbals_from_sg(zfcp_fsf_req_t*, unsigned long,
                                          struct scatterlist*, int, int);
static inline int
	zfcp_qdio_sbals_from_scsicmnd
		(zfcp_fsf_req_t*, unsigned long, struct scsi_cmnd*);
static inline void
		zfcp_zero_sbals(qdio_buffer_t**, int, int);

static zfcp_unit_t*
		zfcp_unit_lookup (zfcp_adapter_t*, int, int, int);

/* prototypes for functions faking callbacks of lower layers */
inline void     zfcp_scsi_process_and_clear_fake_queue(unsigned long);
inline void     zfcp_scsi_insert_into_fake_queue(zfcp_adapter_t *,
                                                 Scsi_Cmnd *);
void		zfcp_fake_outbound_callback (unsigned long);
void		zfcp_fake_inbound_callback (unsigned long);

/* prototypes for proc-file interfacing stuff */
unsigned long zfcp_find_forward(char**, 
                                unsigned long*,
                                char**,
                                unsigned long*);
unsigned long zfcp_find_backward(char**, 
                                 unsigned long*,
                                 char**,
                                 unsigned long*);
int zfcp_create_root_proc(void);
int zfcp_delete_root_proc(void);
int zfcp_create_data_procs(void);
int zfcp_delete_data_procs(void);
int zfcp_proc_map_open(struct inode*, struct file*);
int zfcp_proc_map_close(struct inode*, struct file*);
int zfcp_open_parm_proc(struct inode*, struct file*);
int zfcp_close_parm_proc(struct inode*, struct file*);
int zfcp_open_add_map_proc(struct inode*, struct file*);
int zfcp_close_add_map_proc(struct inode*, struct file*);
int zfcp_adapter_proc_open(struct inode*, struct file*);
int zfcp_adapter_proc_close(struct inode*, struct file*);
int zfcp_port_proc_open(struct inode*, struct file*);
int zfcp_port_proc_close(struct inode*, struct file*);
int zfcp_unit_proc_open(struct inode*, struct file*);
int zfcp_unit_proc_close(struct inode*, struct file*);
ssize_t zfcp_parm_proc_read(struct file*, 
                             char*, 
                             size_t, 
                             loff_t*);
ssize_t zfcp_parm_proc_write(struct file*, 
                             const char*, 
                             size_t, 
                             loff_t*);
ssize_t zfcp_add_map_proc_write(struct file*, 
                             const char*, 
                             size_t, 
                             loff_t*);
ssize_t zfcp_proc_map_read(struct file*, 
                             char*, 
                             size_t, 
                             loff_t*);
ssize_t zfcp_adapter_proc_read(struct file*,
                             char*,
                             size_t,
                             loff_t*);
ssize_t zfcp_adapter_proc_write(struct file*,
                             const char*,
                             size_t,
                             loff_t*);
ssize_t zfcp_port_proc_read(struct file*,
                             char*,
                             size_t,
                             loff_t*);
ssize_t zfcp_port_proc_write(struct file*,
                             const char*,
                             size_t,
                             loff_t*);
ssize_t zfcp_unit_proc_read(struct file*,
                             char*,
                             size_t,
                             loff_t*);
ssize_t zfcp_unit_proc_write(struct file*,
                             const char*,
                             size_t,
                             loff_t*);
int zfcp_create_adapter_proc(zfcp_adapter_t*);
int zfcp_delete_adapter_proc(zfcp_adapter_t*);
int zfcp_create_port_proc(zfcp_port_t*);
int zfcp_delete_port_proc(zfcp_port_t*);
int zfcp_create_unit_proc(zfcp_unit_t*);
int zfcp_delete_unit_proc(zfcp_unit_t*);

/* prototypes for initialisation functions */
static int zfcp_dio_register (void); 

/* prototypes for extended link services functions */
static int zfcp_els(zfcp_port_t*, u8);
static int zfcp_els_handler(unsigned long);
static int zfcp_test_link(zfcp_port_t*);

/* prototypes for error recovery functions */
static int zfcp_erp_adapter_reopen (zfcp_adapter_t*, int);
static int zfcp_erp_port_forced_reopen (zfcp_port_t*, int);
static int zfcp_erp_port_reopen (zfcp_port_t*, int);
static int zfcp_erp_unit_reopen (zfcp_unit_t*, int);

static int zfcp_erp_adapter_reopen_internal (zfcp_adapter_t*, int);
static int zfcp_erp_port_forced_reopen_internal (zfcp_port_t*, int);
static int zfcp_erp_port_reopen_internal (zfcp_port_t*, int);
static int zfcp_erp_unit_reopen_internal (zfcp_unit_t*, int);

static int zfcp_erp_port_reopen_all (zfcp_adapter_t*, int);
static int zfcp_erp_port_reopen_all_internal (zfcp_adapter_t*, int);
static int zfcp_erp_unit_reopen_all_internal (zfcp_port_t*, int);

static inline int zfcp_erp_adapter_shutdown (zfcp_adapter_t*, int);
static inline int zfcp_erp_port_shutdown (zfcp_port_t*, int);
static inline int zfcp_erp_port_shutdown_all (zfcp_adapter_t*, int);
static inline int zfcp_erp_unit_shutdown (zfcp_unit_t*, int);

static int zfcp_erp_adapter_block (zfcp_adapter_t*, int);
static int zfcp_erp_adapter_unblock (zfcp_adapter_t*);
static int zfcp_erp_port_block (zfcp_port_t*, int);
static int zfcp_erp_port_unblock (zfcp_port_t*);
static int zfcp_erp_unit_block (zfcp_unit_t*, int);
static int zfcp_erp_unit_unblock (zfcp_unit_t*);

static int  zfcp_erp_thread (void*);
static int  zfcp_erp_thread_setup (zfcp_adapter_t*);
static void zfcp_erp_thread_setup_task (void*);
static int  zfcp_erp_thread_kill (zfcp_adapter_t*);

static int zfcp_erp_strategy (zfcp_erp_action_t*);

static int zfcp_erp_strategy_do_action (zfcp_erp_action_t*);
static int zfcp_erp_strategy_memwait (zfcp_erp_action_t*);
static int zfcp_erp_strategy_check_target (zfcp_erp_action_t*, int);
static int zfcp_erp_strategy_check_unit (zfcp_unit_t*, int);
static int zfcp_erp_strategy_check_port (zfcp_port_t*, int);
static int zfcp_erp_strategy_check_adapter (zfcp_adapter_t*, int);
static int zfcp_erp_strategy_statechange
		(int, u32, zfcp_adapter_t*, zfcp_port_t*, zfcp_unit_t*, int);
static inline int zfcp_erp_strategy_statechange_detected (atomic_t*, u32);
static int zfcp_erp_strategy_followup_actions
		(int, zfcp_adapter_t*, zfcp_port_t*, zfcp_unit_t*, int);
static int zfcp_erp_strategy_check_queues (zfcp_adapter_t*);
static int zfcp_erp_strategy_check_dismissed(zfcp_erp_action_t *);

static int zfcp_erp_adapter_strategy (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_generic (zfcp_erp_action_t*, int);
static int zfcp_erp_adapter_strategy_close (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_close_irq (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_close_qdio (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_close_fsf (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open_irq (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open_qdio (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open_fsf (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open_fsf_xconfig (zfcp_erp_action_t*);
static int zfcp_erp_adapter_strategy_open_fsf_statusread (zfcp_erp_action_t*);

static int zfcp_erp_port_forced_strategy (zfcp_erp_action_t*);
static int zfcp_erp_port_forced_strategy_close (zfcp_erp_action_t*);

static int zfcp_erp_port_strategy (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_clearstati (zfcp_port_t*);
static int zfcp_erp_port_strategy_close (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open_nameserver (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open_nameserver_wakeup (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open_common (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open_common_lookup (zfcp_erp_action_t*);
static int zfcp_erp_port_strategy_open_port (zfcp_erp_action_t*);

static int zfcp_erp_unit_strategy (zfcp_erp_action_t*);
static int zfcp_erp_unit_strategy_clearstati (zfcp_unit_t*);
static int zfcp_erp_unit_strategy_close (zfcp_erp_action_t*);
static int zfcp_erp_unit_strategy_open (zfcp_erp_action_t*);

static void zfcp_erp_modify_adapter_status(zfcp_adapter_t*, u32, int);
static void zfcp_erp_modify_port_status(zfcp_port_t*, u32, int);
static void zfcp_erp_modify_unit_status(zfcp_unit_t*, u32, int);
static void zfcp_erp_adapter_failed(zfcp_adapter_t*);
static void zfcp_erp_port_failed(zfcp_port_t*);
static void zfcp_erp_unit_failed(zfcp_unit_t*);

static int zfcp_erp_action_dismiss_adapter (zfcp_adapter_t*);
static int zfcp_erp_action_dismiss_port(zfcp_port_t*);
/* zfcp_erp_action_dismiss_unit not needed */
static int zfcp_erp_action_dismiss (zfcp_erp_action_t*);

static int zfcp_erp_action_enqueue
	(int, zfcp_adapter_t*, zfcp_port_t*, zfcp_unit_t*);
static int zfcp_erp_action_dequeue (zfcp_erp_action_t*);

static int zfcp_erp_action_ready (zfcp_erp_action_t*);
static int zfcp_erp_action_exists (zfcp_erp_action_t*);

static inline void zfcp_erp_action_to_ready (zfcp_erp_action_t*);
static inline void zfcp_erp_action_to_running (zfcp_erp_action_t*);
static inline void zfcp_erp_from_one_to_other
	(struct list_head*, struct list_head*);

static void zfcp_erp_fsf_req_handler (zfcp_fsf_req_t*);
static void zfcp_erp_memwait_handler (unsigned long);
static void zfcp_erp_timeout_handler (unsigned long);
static int zfcp_erp_timeout_init (zfcp_erp_action_t*);

int zfcp_erp_wait (zfcp_adapter_t*);


#ifdef ZFCP_STAT_REQSIZES
static int zfcp_statistics_clear
	(zfcp_adapter_t*, struct list_head*);
static int zfcp_statistics_print
	(zfcp_adapter_t*, struct list_head*, char*, char*, int, int);
static void zfcp_statistics_inc
	(zfcp_adapter_t*, struct list_head*, u32);
static inline void zfcp_statistics_new
	(zfcp_adapter_t*, struct list_head*, u32);
static inline void zfcp_statistics_sort
	(struct list_head*, struct list_head*, zfcp_statistics_t*);
#endif


/* driver data */
static struct file_operations zfcp_parm_fops =
{
     open: zfcp_open_parm_proc, 
     read: zfcp_parm_proc_read,
     write: zfcp_parm_proc_write, 
     release: zfcp_close_parm_proc, 
};

static struct file_operations zfcp_map_fops =
{
     open:	zfcp_proc_map_open,
     read:	zfcp_proc_map_read,
     release:	zfcp_proc_map_close,
};

static struct file_operations zfcp_add_map_fops =
{
     open: zfcp_open_add_map_proc,
     write: zfcp_add_map_proc_write,
     release: zfcp_close_add_map_proc,
};

static struct file_operations zfcp_adapter_fops =
{
     open: zfcp_adapter_proc_open,
     read: zfcp_adapter_proc_read,
     write: zfcp_adapter_proc_write,
     release: zfcp_adapter_proc_close,
};

static struct file_operations zfcp_port_fops =
{
     open: zfcp_port_proc_open,
     read: zfcp_port_proc_read,
     write: zfcp_port_proc_write,
     release: zfcp_port_proc_close,
};

static struct file_operations zfcp_unit_fops =
{
     open: zfcp_unit_proc_open,
     read: zfcp_unit_proc_read,
     write: zfcp_unit_proc_write,
     release: zfcp_unit_proc_close,
};

zfcp_data_t zfcp_data = {
	{	/* Scsi Host Template */
                name:                   ZFCP_NAME,
                proc_name:              "dummy",
                proc_info:              NULL, /* we don't need scsi proc info */
		detect:			zfcp_scsi_detect,
		revoke:			zfcp_scsi_revoke,
		release:		zfcp_scsi_release,
		queuecommand:		zfcp_scsi_queuecommand,
		eh_abort_handler:	zfcp_scsi_eh_abort_handler,
		eh_device_reset_handler:zfcp_scsi_eh_device_reset_handler,
		eh_bus_reset_handler:	zfcp_scsi_eh_bus_reset_handler,
		eh_host_reset_handler:	zfcp_scsi_eh_host_reset_handler,
		/* FIXME(openfcp): Tune */
		can_queue:	   	4096,
		this_id:		0,
		/*
		 * FIXME:
		 * one less? can zfcp_create_sbale cope with it?
		 */
                sg_tablesize:	        ZFCP_MAX_SBALES_PER_REQ,
		/* some moderate value for the moment */
		cmd_per_lun:		ZFCP_CMND_PER_LUN,
		/* no requirement on the addresses of data buffers */
		unchecked_isa_dma:	0,
		/* maybe try it later */
		use_clustering:		1,
		/* we are straight forward */
		use_new_eh_code:	1
	}
	/* rest initialized with zeros */
};


inline fcp_dl_t *zfcp_get_fcp_dl_ptr(fcp_cmnd_iu_t *fcp_cmd)
{
	int additional_length = fcp_cmd->add_fcp_cdb_length << 2;
	fcp_dl_t *fcp_dl_addr=
			(fcp_dl_t *)(
				(unsigned char*)fcp_cmd +				
                                sizeof(fcp_cmnd_iu_t) +
				additional_length);    
	/*
	 * fcp_dl_addr = start address of fcp_cmnd structure + 
	 * size of fixed part + size of dynamically sized add_dcp_cdb field
	 * SEE FCP-2 documentation
	 */
	return fcp_dl_addr;
}


inline fcp_dl_t zfcp_get_fcp_dl(fcp_cmnd_iu_t *fcp_cmd)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
     
	ZFCP_LOG_TRACE("enter (fcp_cmd=0x%lx)\n",
                       (unsigned long) fcp_cmd);
	ZFCP_LOG_TRACE("exit 0x%lx\n",
                       (unsigned long)*zfcp_get_fcp_dl_ptr(fcp_cmd));
	return *zfcp_get_fcp_dl_ptr(fcp_cmd);
#undef ZFCP_LOG_AREA       
#undef ZFCP_LOG_AREA_PREFIX
}


inline void zfcp_set_fcp_dl(fcp_cmnd_iu_t *fcp_cmd, fcp_dl_t fcp_dl)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
     ZFCP_LOG_TRACE("enter (fcp_cmd=0x%lx), (fcp_dl=0x%x)\n",
                    (unsigned long)fcp_cmd,
                    fcp_dl);
     *zfcp_get_fcp_dl_ptr(fcp_cmd)=fcp_dl;
     ZFCP_LOG_TRACE("exit\n");
#undef ZFCP_LOG_AREA       
#undef ZFCP_LOG_AREA_PREFIX
}  


#ifdef MODULE
/* declare driver module init/cleanup functions */
module_init(zfcp_module_init);
module_exit(zfcp_module_exit);

MODULE_AUTHOR(
	"Martin Peschke <mpeschke@de.ibm.com>, "
        "Raimund Schroeder <raimund.schroeder@de.ibm.com>, "
	"Aron Zeh <arzeh@de.ibm.com>, "
	"IBM Deutschland Entwicklung GmbH");
/* what this driver module is about */
MODULE_DESCRIPTION(
	"FCP (SCSI over Fibre Channel) HBA driver for IBM eServer zSeries, " ZFCP_REVISION);
MODULE_LICENSE("GPL");
/* log level may be provided as a module parameter */
MODULE_PARM(loglevel, "i");
/* short explaination of the previous module parameter */
MODULE_PARM_DESC(loglevel,
	"log levels, 8 nibbles: "
	"(unassigned) ERP QDIO DIO Config FSF SCSI Other, "
	"levels: 0=none 1=normal 2=devel 3=trace");
#endif /* MODULE */

#ifdef ZFCP_PRINT_FLAGS
static u32 flags_dump=0;
MODULE_PARM(flags_dump, "i");
#define ZFCP_LOG_FLAGS(ll, m...) \
		if (ll<=flags_dump) \
			_ZFCP_LOG(m)
#else
#define ZFCP_LOG_FLAGS(ll, m...)
#endif

static char *map = NULL;
#ifdef MODULE
MODULE_PARM(map, "s");
MODULE_PARM_DESC(map,
	"Initial FC to SCSI mapping table");

/* enable/disable paranoia (extra checks to ensure driver consistency) */
MODULE_PARM(zfcp_paranoia, "b");
/* short explaination of the previous module parameter */
MODULE_PARM_DESC(zfcp_paranoia,
	"extra checks to ensure driver consistency, "
	"0=disabled other !0=enabled");
#else

/* zfcp_map boot parameter */
static int __init zfcp_map_setup(char *str)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	/* don't parse trailing " */
	map = str + 1;		
	/* don't parse final " */
	map[strlen(map) - 1] = ZFCP_PARSE_SPACE_CHARS[0];
	ZFCP_LOG_INFO("map is %s\n", map);
	return 1;	/* why just 1? */

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
__setup("zfcp_map=", zfcp_map_setup);

/* zfcp_loglevel boot_parameter */
static int __init zfcp_loglevel_setup(char *str)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	loglevel = simple_strtoul(str, NULL, 0);
	//ZFCP_LOG_NORMAL("loglevel is 0x%x\n", loglevel);
	return 1;	/* why just 1? */

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
__setup("zfcp_loglevel=", zfcp_loglevel_setup);

#endif /* MODULE */

#ifdef ZFCP_CAUSE_ERRORS
void zfcp_force_error(unsigned long data)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP
        
        zfcp_adapter_t *adapter;

        ZFCP_LOG_NORMAL("Cause error....\n");
        adapter = ZFCP_FIRST_ADAPTER;
        printk("adater reopen\n");
        zfcp_erp_adapter_reopen(adapter, 0);
        printk("adater close\n");
	zfcp_erp_adapter_shutdown(adapter, 0);
        /*
	zfcp_force_error_timer.function = zfcp_force_error;
        zfcp_force_error_timer.data = 0;
        zfcp_force_error_timer.expires = jiffies + 60*HZ;
	add_timer(&zfcp_force_error_timer);
        */
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
#endif //ZFCP_CAUSE_ERRORS


static inline int zfcp_fsf_req_is_scsi_cmnd(zfcp_fsf_req_t *fsf_req)
{
	return ((fsf_req->fsf_command == FSF_QTCB_FCP_CMND) &&
		!(fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT));
}


static inline void zfcp_cmd_dbf_event_fsf(
		const char *text,
		zfcp_fsf_req_t *fsf_req,
		void *add_data,
		int add_length)
{
#ifdef ZFCP_DEBUG_COMMANDS
	zfcp_adapter_t *adapter = fsf_req->adapter;
	Scsi_Cmnd *scsi_cmnd;
	int level = 3;
	int i;
	unsigned long flags;

	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);
	if (zfcp_fsf_req_is_scsi_cmnd(fsf_req)) {
		scsi_cmnd = fsf_req->data.send_fcp_command_task.scsi_cmnd;
		debug_text_event(adapter->cmd_dbf, level, "fsferror");
		debug_text_event(adapter->cmd_dbf, level, text);
		debug_event(adapter->cmd_dbf, level, &fsf_req, sizeof(unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no, sizeof(u32));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd, sizeof(unsigned long));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd->cmnd,
			    min(ZFCP_CMD_DBF_LENGTH, (int)scsi_cmnd->cmd_len));
		for (i = 0; i < add_length; i += ZFCP_CMD_DBF_LENGTH)
			debug_event(
				adapter->cmd_dbf,
				level,
				(char*)add_data + i,
				min(ZFCP_CMD_DBF_LENGTH, add_length - i));
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}


static inline void zfcp_cmd_dbf_event_scsi(
		const char *text,
		zfcp_adapter_t *adapter,
		Scsi_Cmnd *scsi_cmnd)
{
#ifdef ZFCP_DEBUG_COMMANDS
	zfcp_fsf_req_t *fsf_req = (zfcp_fsf_req_t*) scsi_cmnd->host_scribble;
	int level = ((host_byte(scsi_cmnd->result) != 0) ? 1 : 5);
	unsigned long flags;

	write_lock_irqsave(&adapter->cmd_dbf_lock, flags);	
	debug_text_event(adapter->cmd_dbf, level, "hostbyte");
	debug_text_event(adapter->cmd_dbf, level, text);
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->result, sizeof(u32));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd, sizeof(unsigned long));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->cmnd,
		    min(ZFCP_CMD_DBF_LENGTH, (int)scsi_cmnd->cmd_len));
	if (fsf_req) {
		debug_event(adapter->cmd_dbf, level, &fsf_req, sizeof(unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no, sizeof(u32));
	} else	{
		debug_text_event(adapter->cmd_dbf, level, "");
		debug_text_event(adapter->cmd_dbf, level, "");
	}
	write_unlock_irqrestore(&adapter->cmd_dbf_lock, flags);
#endif
}


static inline void zfcp_in_els_dbf_event(
		zfcp_adapter_t *adapter,
		const char *text,
		fsf_status_read_buffer_t *status_buffer,
		int length)
{
#ifdef ZFCP_DEBUG_INCOMING_ELS
	int level = 1;
	int i;

	debug_text_event(adapter->in_els_dbf, level, text);
	debug_event(adapter->in_els_dbf, level, &status_buffer->d_id, 8);
	for (i = 0; i < length; i += ZFCP_IN_ELS_DBF_LENGTH)
		debug_event(
			adapter->in_els_dbf,
			level,
			(char*)status_buffer->payload + i,
			min(ZFCP_IN_ELS_DBF_LENGTH, length - i));
#endif
}


/****************************************************************/


/*
 * function:	zfcp_module_init
 *
 * purpose:	driver module initialization routine
 *
 * locks:       initialises zfcp_data.proc_sema, zfcp_data.adapter_list_lock
 *              zfcp_data.proc_sema is taken and released within this 
 *              function
 *
 * returns:	0	success
 *		!0	failure
 */
static int __init zfcp_module_init(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	atomic_set(&zfcp_data.loglevel, loglevel);

#ifdef ZFCP_LOW_MEM_CREDITS
	atomic_set(&zfcp_data.lowmem_credit, 0);
#endif
	
	ZFCP_LOG_DEBUG(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n"); 
	ZFCP_LOG_TRACE("enter\n");

        ZFCP_LOG_TRACE(
		"Start Address of module: 0x%lx\n",
		(unsigned long) &zfcp_module_init);

	/* derive driver version number from cvs revision string */
	zfcp_data.driver_version = zfcp_derive_driver_version();
	ZFCP_LOG_NORMAL(
		"driver version 0x%x\n",
		zfcp_data.driver_version);

	/* initialize adapter list */
	rwlock_init(&zfcp_data.adapter_list_lock);
	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);

	/* initialize map list */
	INIT_LIST_HEAD(&zfcp_data.map_list_head);

        /* Initialise proc semaphores */
        sema_init(&zfcp_data.proc_sema,1);
	down(&zfcp_data.proc_sema); /* config changes protected by proc_sema */

#ifdef CONFIG_PROC_FS
	retval = zfcp_create_root_proc();
	if (retval) {
		ZFCP_LOG_NORMAL(
			"Error: Proc fs startup failed\n");
		goto failed_root_proc;
	}

	retval = zfcp_create_data_procs();
	if (retval) {
                ZFCP_LOG_NORMAL(
                        "Error: Proc fs startup failed\n");
		goto failed_data_procs;
        }
#endif

	/* always succeeds for now */
	/* FIXME: set priority? */
	zfcp_data.reboot_notifier.notifier_call = zfcp_reboot_handler;
	register_reboot_notifier(&zfcp_data.reboot_notifier);

	/*
	 * parse module parameter string for valid configurations and create
	 * entries for configured adapters, remote ports and logical units
	 */
	if (map) {
		retval = zfcp_config_parse_record_list(
				map,
				strlen(map),
				ZFCP_PARSE_ADD);

		if (retval < 0)
			goto failed_parse;	/* some entries may have been created */
	}

	/* save address of data structure managing the driver module */
	zfcp_data.scsi_host_template.module = THIS_MODULE;

	/*
	 * register driver module with SCSI stack
	 * we do this last to avoid the need to revert this step
	 * if other init stuff goes wrong
	 * (scsi_unregister_module() does not work here!)
	 */
        retval = scsi_register_module(
			MODULE_SCSI_HA,
			&zfcp_data.scsi_host_template);
	if (retval) {
		ZFCP_LOG_NORMAL(
			"error: Registration of the driver module "
			"with the Linux SCSI stack failed.\n");
		goto failed_scsi;
	}

        /* setup dynamic I/O */
        retval = zfcp_dio_register();
        if (retval) {
                ZFCP_LOG_NORMAL(
                        "warning: Dynamic attach/detach facilities for "
                        "the adapter(s) could not be started. \n");
		retval = 0;
        }

	up(&zfcp_data.proc_sema); /* release procfs */

#ifdef ZFCP_CAUSE_ERRORS
	init_timer(&zfcp_force_error_timer);
	zfcp_force_error_timer.function = zfcp_force_error;
        zfcp_force_error_timer.data = 0;
        zfcp_force_error_timer.expires = jiffies + 60*HZ;
	add_timer(&zfcp_force_error_timer);
#endif

	/* we did it, skip all cleanups related to failures */
        goto out;

 failed_scsi:
 failed_parse:
	/* FIXME: might there be a race between module unload and shutdown? */
	unregister_reboot_notifier(&zfcp_data.reboot_notifier);
	zfcp_adapter_shutdown_all();
	zfcp_config_cleanup();
	/*
	 * FIXME(design):
	 * We need a way to cancel all proc usage at this point.
	 * Just having a semaphore is not sufficient since this
	 * semaphore makes exploiters sleep in our proc code.
	 * If we wake them then we do not know when they actually
	 * left the proc path. They must left the proc path before
	 * we are allowed to delete proc entries. We need a kind of
	 * handshaking to ensure that all proc-users are really
	 * gone. Even if we have this then we can't ensure
	 * that another proc-user enters the proc-path before
	 * we delete proc-entries.
	 */
	zfcp_delete_data_procs();

 failed_data_procs:
	zfcp_delete_root_proc();
        
 failed_root_proc:
	 ZFCP_LOG_NORMAL("error: Module could not be loaded.\n");

 out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

__initcall(zfcp_module_init);

/*
 * function:	zfcp_module_exit
 *
 * purpose:	driver module cleanup routine
 *
 * locks:       zfcp_data.proc_sema is acquired prior to calling 
 *                zfcp_config_cleanup and released afterwards
 *
 * returns:	void
 */
static void __exit zfcp_module_exit(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER
        int temp_ret=0;

	ZFCP_LOG_TRACE("enter\n");

	/* FIXME: might there be a race between module unload and shutdown? */
	unregister_reboot_notifier(&zfcp_data.reboot_notifier);

	/* unregister driver module from SCSI stack */
	scsi_unregister_module(MODULE_SCSI_HA, &zfcp_data.scsi_host_template);

	/* shutdown all adapters (incl. those not registered in SCSI stack) */
	zfcp_adapter_shutdown_all();

	zfcp_delete_data_procs();
	/* unregister from Dynamic I/O */
	temp_ret = s390_device_unregister(&zfcp_data.devreg);
        ZFCP_LOG_TRACE(
		"s390_device_unregister returned %i\n", 
		temp_ret);
	temp_ret = s390_device_unregister(&zfcp_data.devreg_priv);
        ZFCP_LOG_TRACE(
		"s390_device_unregister returned %i (privileged subchannel)\n",
		temp_ret);
        ZFCP_LOG_TRACE("Before zfcp_config_cleanup\n");

	/* free all resources dynamically allocated */

	/* block proc access to config */
        down(&zfcp_data.proc_sema);
	temp_ret=zfcp_config_cleanup();
        up(&zfcp_data.proc_sema);

        if (temp_ret) {
                ZFCP_LOG_NORMAL("bug: Could not free all memory "
                               "(debug info %d)\n",
                               temp_ret);
	}

        zfcp_delete_root_proc();

#ifdef ZFCP_CAUSE_ERRORS
        del_timer(&zfcp_force_error_timer);
#endif	

        ZFCP_LOG_TRACE("exit\n");
	ZFCP_LOG_DEBUG("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_reboot_handler
 *
 * purpose:	This function is called automatically by the kernel whenever
 *              a reboot or a shut-down is initiated and zfcp is still
 *              loaded
 *
 * locks:       zfcp_data.proc_sema is taken prior to shutting down the module
 *              and removing all structures
 *
 * returns:     NOTIFY_DONE in all cases
 */
int zfcp_reboot_handler(struct notifier_block *notifier, unsigned long code, void *ptr)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = NOTIFY_DONE;

	ZFCP_LOG_TRACE("enter\n");

	/* block proc access to config (for rest of lifetime of this Linux) */
        down(&zfcp_data.proc_sema);
	zfcp_adapter_shutdown_all();

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_adapter_shutdown_all
 *
 * purpose:	recursively calls zfcp_erp_adapter_shutdown to stop all
 *              IO on each adapter, return all outstanding packets and 
 *              relinquish all IRQs
 *              Note: This function waits for completion of all shutdowns
 *
 * returns:     0 in all cases
 */
static int zfcp_adapter_shutdown_all(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE("enter\n");

	/*
	 * no adapter list lock since list won't change (proc is blocked),
	 * this allows sleeping while iterating the list
	 */
	ZFCP_FOR_EACH_ADAPTER(adapter)
		zfcp_erp_adapter_shutdown(adapter, 0);
	/* start all shutdowns first before any waiting to allow for concurreny */
	ZFCP_FOR_EACH_ADAPTER(adapter)
		zfcp_erp_wait(adapter);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_erp_port_shutdown_all
 *
 * purpose:	wrapper around zfcp_erp_port_reopen_all setting all the 
 *              required parameters to close a port
 *
 * returns:     0 in all cases
 */
static int zfcp_erp_port_shutdown_all(zfcp_adapter_t *adapter, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n",
                       (unsigned long)adapter);

        zfcp_erp_port_reopen_all(adapter, 
                                 ZFCP_STATUS_COMMON_RUNNING |
                                 ZFCP_STATUS_COMMON_ERP_FAILED |
                                 clear_mask);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/*
 * function:  zfcp_dio_register
 *
 * purpose:   Registers the FCP-adapter specific device number with the common
 *            io layer. All oper/not_oper calls will only be presented for 
 *            devices that match the below criteria.	
 *
 * returns:   0 on success
 *            -error code on failure
 */
static int zfcp_dio_register()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_DIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_DIO

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

	/* register handler for Dynamic I/O */
        zfcp_data.devreg.ci.hc.ctype = ZFCP_CONTROL_UNIT_TYPE;
        zfcp_data.devreg.ci.hc.cmode = ZFCP_CONTROL_UNIT_MODEL;
        zfcp_data.devreg.ci.hc.dtype = ZFCP_DEVICE_TYPE;
        zfcp_data.devreg.ci.hc.dmode = ZFCP_DEVICE_MODEL;
        zfcp_data.devreg.flag = DEVREG_TYPE_DEVCHARS | DEVREG_EXACT_MATCH;
        zfcp_data.devreg.oper_func = &zfcp_dio_oper_handler;

	retval = s390_device_register(&zfcp_data.devreg);
        if (retval < 0) {
                ZFCP_LOG_NORMAL(
			"bug: The FSF device type could not "
			"be registered with the S/390 i/o layer "
			"(debug info %d)",
			retval);
        }

        zfcp_data.devreg_priv.ci.hc.ctype = ZFCP_CONTROL_UNIT_TYPE;
        zfcp_data.devreg_priv.ci.hc.cmode = ZFCP_CONTROL_UNIT_MODEL;
        zfcp_data.devreg_priv.ci.hc.dtype = ZFCP_DEVICE_TYPE;
        zfcp_data.devreg_priv.ci.hc.dmode = ZFCP_DEVICE_MODEL_PRIV;
        zfcp_data.devreg_priv.flag = DEVREG_TYPE_DEVCHARS | DEVREG_EXACT_MATCH;
        zfcp_data.devreg_priv.oper_func = &zfcp_dio_oper_handler;

	retval = s390_device_register(&zfcp_data.devreg_priv);
        if (retval < 0) {
                ZFCP_LOG_NORMAL(
			"bug: The FSF privileged device type could not "
			"be registered with the S/390 i/o layer "
			"(debug info %d)",
			retval);
         }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_config_cleanup
 *
 * purpose:	must only be called after all adapters are properly shut down
 *              Frees all device structs (unit, port, adapter) and removes them 
 *              from the lists
 *
 * returns:	0 - 	no error occured during cleanup
 *		!0 - 	one or more errors occured during cleanup
 *			(retval is not guaranteed to be a valid -E*)
 *
 * context:	called on failure of module_init and from module_exit
 *
 * locks:       zfcp_data.proc_sema needs to be held on function entry
 *              adapter->port_list_lock,
 *                port->unit_list_lock are held when walking the 
 *                respective lists
 */
static int zfcp_config_cleanup(void)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_OTHER
 
	int retval = 0;
 	zfcp_adapter_t *adapter, *tmp_adapter;
	zfcp_port_t *port, *tmp_port;
	zfcp_unit_t *unit, *tmp_unit;
	unsigned long flags = 0;

	ZFCP_LOG_TRACE("enter\n");

        /* Note: no adapter_list_lock is needed as we have the proc_sema */ 
	ZFCP_FOR_NEXT_ADAPTER (adapter, tmp_adapter) {
		write_lock_irqsave(&adapter->port_list_lock, flags);
		ZFCP_FOR_NEXT_PORT (adapter, port, tmp_port) {
			write_lock(&port->unit_list_lock);
			ZFCP_FOR_NEXT_UNIT (port, unit, tmp_unit){ 
				retval |= zfcp_unit_dequeue(unit);
                        }
			write_unlock(&port->unit_list_lock);
			retval |= zfcp_port_dequeue(port);
 		}
		write_unlock_irqrestore(&adapter->port_list_lock, flags);
		retval |= zfcp_adapter_dequeue(adapter);
	}
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_derive_driver_version
 *
 * purpose:	generates a 32 bit value from the cvs mantained revision,
 *
 * returns:	!0 - driver version
 *			format:	0 .. 7		8 .. 15		16 .. 31
 *				(reserved)	major	.	minor
 *		0 - if no version string could be assembled
 */
static u32 zfcp_derive_driver_version(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	char *revision = ZFCP_REVISION;
	u32 version = 0;
	char *d;

	ZFCP_LOG_TRACE("enter\n");

	/* major */
	for (d = revision; !isdigit(d[0]); d++) {}
	version |= (simple_strtoul(d, &d, 10) & 0xFF) << 16; 

	/* dot */
	if (d[0] != '.') {
		ZFCP_LOG_NORMAL(
			"bug: Revision number generation from string "
                        "unsuccesfull. Setting revision number to 0 and "
                        "continuing (debug info %s).\n",
			revision);
		version = 0;
		goto out;
	}
	d++;

	/* minor */
	version |= simple_strtoul(d, NULL, 10) & 0xFFFF;

out:
	ZFCP_LOG_TRACE("exit (0x%x)\n", version);

	return version;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_buffers_enqueue
 *
 * purpose:	allocates BUFFER memory to each of the pointers of
 *              the qdio_buffer_t array in the adapter struct
 *
 * returns:	number of buffers allocated
 *
 * comments:    cur_buf is the pointer array and count can be any
 *              number of required buffers, the page-fitting arithmetic is
 *              done entirely within this funciton
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
int zfcp_buffers_enqueue(qdio_buffer_t **cur_buf, int count)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

        int buf_pos;
        int qdio_buffers_per_page;
        int page_pos = 0;
        qdio_buffer_t *first_in_page = NULL;
        
	ZFCP_LOG_TRACE(
		"enter cur_buf 0x%lx\n",
                (unsigned long)cur_buf);

        qdio_buffers_per_page = PAGE_SIZE / sizeof(qdio_buffer_t);
        ZFCP_LOG_TRACE(
		"Buffers per page %d.\n",
                qdio_buffers_per_page);

        for (buf_pos = 0; buf_pos < count; buf_pos++) {
                if (page_pos == 0) {
                        cur_buf[buf_pos] = (qdio_buffer_t*) ZFCP_GET_ZEROED_PAGE(GFP_KERNEL);
                        if (cur_buf[buf_pos] == NULL) {
                                ZFCP_LOG_INFO(
					"error: Could not allocate "
                                        "memory for qdio transfer structures.\n");
                                goto out;
                        }
                        first_in_page = cur_buf[buf_pos];
                } else {
                        cur_buf[buf_pos] = first_in_page + page_pos;

                }
                /* was initialised to zero */
                page_pos++;
                page_pos %= qdio_buffers_per_page;
        } // for (buf_pos = 0; buf_pos < count; buf_pos++)
 out:
	ZFCP_LOG_TRACE("exit (%d)\n", buf_pos);
        return buf_pos; 

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_buffers_dequeue
 *
 * purpose:	frees BUFFER memory for each of the pointers of
 *              the qdio_buffer_t array in the adapter struct
 *
 * returns:	sod all
 *
 * comments:    cur_buf is the pointer array and count can be any
 *              number of buffers in the array that should be freed
 *              starting from buffer 0
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
void zfcp_buffers_dequeue(qdio_buffer_t **cur_buf, int count)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

        int buf_pos;
        int qdio_buffers_per_page;

	ZFCP_LOG_TRACE("enter cur_buf 0x%lx count %d\n",
                       (unsigned long)cur_buf,
                       count);
        
        qdio_buffers_per_page = PAGE_SIZE / sizeof(qdio_buffer_t);
        ZFCP_LOG_TRACE(
		"Buffers per page %d.\n",
                qdio_buffers_per_page);

        for (buf_pos = 0; buf_pos < count; buf_pos += qdio_buffers_per_page) {
                ZFCP_FREE_PAGE((unsigned long)cur_buf[buf_pos]);
        }

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_allocate_qdio_queues
 *
 * purpose:	wrapper around zfcp_buffers_enqueue with possible calls
 *              to zfcp_buffers_dequeue in the error case. Deals with
 *              request and response queues
 *
 * returns:	0 on success
 *              -EIO if allocation of buffers failed
 *               (all buffers are guarranteed to be un-allocated in this case)
 *
 * comments:    called only from adapter_enqueue
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
int zfcp_allocate_qdio_queues(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

        int buffer_count;
        int retval=0;

        ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n",
                       (unsigned long)adapter);

        buffer_count = zfcp_buffers_enqueue(
                            &(adapter->request_queue.buffer[0]),
                            QDIO_MAX_BUFFERS_PER_Q);
        if (buffer_count < QDIO_MAX_BUFFERS_PER_Q) {
                ZFCP_LOG_DEBUG("error: Out of memory allocating "
                               "request queue, only %d buffers got. "
                               "Binning them.\n",
                                buffer_count);
                zfcp_buffers_dequeue(
                     &(adapter->request_queue.buffer[0]),
                     buffer_count);
                retval = -ENOMEM;
                goto out;
        }

        buffer_count = zfcp_buffers_enqueue(
                            &(adapter->response_queue.buffer[0]),
                            QDIO_MAX_BUFFERS_PER_Q);
        if (buffer_count < QDIO_MAX_BUFFERS_PER_Q) {
                ZFCP_LOG_DEBUG("error: Out of memory allocating "
                               "response queue, only %d buffers got. "
                               "Binning them.\n",
                               buffer_count);
                zfcp_buffers_dequeue(
                     &(adapter->response_queue.buffer[0]),
                     buffer_count);
                ZFCP_LOG_TRACE("Deallocating request_queue Buffers.\n");
                zfcp_buffers_dequeue(
                     &(adapter->request_queue.buffer[0]),
                     QDIO_MAX_BUFFERS_PER_Q);
                retval = -ENOMEM;
                goto out;
        }
 out:
	ZFCP_LOG_TRACE("exit (%d)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_free_qdio_queues
 *
 * purpose:	wrapper around zfcp_buffers_dequeue for request and response
 *              queues
 *
 * returns:	sod all
 *
 * comments:    called only from adapter_dequeue
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
void zfcp_free_qdio_queues(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", 
                       (unsigned long)adapter);
        
        ZFCP_LOG_TRACE("Deallocating request_queue Buffers.\n");
        zfcp_buffers_dequeue(
             &(adapter->request_queue.buffer[0]),
             QDIO_MAX_BUFFERS_PER_Q);

        ZFCP_LOG_TRACE("Deallocating response_queue Buffers.\n");
        zfcp_buffers_dequeue(
             &(adapter->response_queue.buffer[0]),
             QDIO_MAX_BUFFERS_PER_Q);

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_free_low_mem_buffers
 *
 * purpose:	frees all static memory in the pools previously allocated by
 *              zfcp_allocate_low_mem buffers
 *
 * returns:     sod all
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
static void zfcp_free_low_mem_buffers(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	ZFCP_LOG_TRACE("enter (adapter 0x%lx)\n",
                       (unsigned long)adapter);

	zfcp_mem_pool_destroy(&adapter->pool.fsf_req_status_read);
	zfcp_mem_pool_destroy(&adapter->pool.data_status_read);
	zfcp_mem_pool_destroy(&adapter->pool.data_gid_pn);
	zfcp_mem_pool_destroy(&adapter->pool.fsf_req_erp);
	zfcp_mem_pool_destroy(&adapter->pool.fsf_req_scsi);
        
        ZFCP_LOG_TRACE("exit\n");
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_allocate_low_mem_buffers
 *
 * purpose:	The pivot for the static memory buffer pool generation.
 *              Called only from zfcp_adapter_enqueue in order to allocate
 *              a combined QTCB/fsf_req buffer for erp actions and fcp/SCSI
 *              commands.
 *              It also genrates fcp-nameserver request/response buffer pairs
 *              and unsolicited status read fsf_req buffers by means of 
 *              function calls to the apropriate handlers.
 *
 * returns:     0 on success
 *              -ENOMEM on failure (some buffers might be allocated)
 *
 * locks:       must only be called with zfcp_data.proc_sema taken
 */
static int zfcp_allocate_low_mem_buffers(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter 0x%lx)\n",
                       (unsigned long)adapter);

	retval = zfcp_mem_pool_create(&adapter->pool.fsf_req_erp, 1,
                                      sizeof(struct zfcp_fsf_req_pool_buffer),
                                      0, 0);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: FCP command buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(&adapter->pool.data_gid_pn, 1,
                                      sizeof(struct zfcp_gid_pn_data), 0, 0);

	if (retval) {
		ZFCP_LOG_INFO(
			"error: Nameserver buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(&adapter->pool.fsf_req_status_read,
                                      ZFCP_STATUS_READS_RECOM,
                                      sizeof(zfcp_fsf_req_t), 0, 0);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Status read request pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(&adapter->pool.data_status_read,
                                      ZFCP_STATUS_READS_RECOM,
                                      sizeof(fsf_status_read_buffer_t), 0, 0);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Status read buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(&adapter->pool.fsf_req_scsi,
                                      1, sizeof(struct zfcp_fsf_req_pool_buffer),
                                      zfcp_scsi_low_mem_buffer_timeout_handler,
                                      (unsigned long) &adapter);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: FCP command buffer pool allocation failed\n");
		goto out;
	}

out:
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_adapter_enqueue
 *
 * purpose:	enqueues an adapter at the end of the adapter list
 *		in the driver data
 *              all adapter internal structures are set up
 *              proc-fs entries are also created
 *
 * returns:	0 if a new adapter was successfully enqueued
 *              ZFCP_KNOWN if an adapter with this devno was already present
 *		-ENOMEM if alloc failed
 *	   
 * locks:	proc_sema must be held to serialise chnages to the adapter list
 *              zfcp_data.adapter_list_lock is taken and released several times
 *              within the function (must not be held on entry)
 */
static int zfcp_adapter_enqueue(devno_t devno, zfcp_adapter_t **adapter_p)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;
        zfcp_adapter_t *adapter;
	unsigned long flags;
        char dbf_name[20];
        
        ZFCP_LOG_TRACE(
		"enter (devno=0x%04x ,adapter_p=0x%lx)\n", 
		devno,
		(unsigned long)adapter_p);

	read_lock_irqsave(&zfcp_data.adapter_list_lock, flags);
	ZFCP_FOR_EACH_ADAPTER(adapter) {
		if (adapter->devno == devno) {
			ZFCP_LOG_TRACE(
				"Adapter with devno 0x%04x "
				"already exists.\n",
				 devno);
                        retval = ZFCP_KNOWN;
                        break;
		}
        }
        read_unlock_irqrestore(&zfcp_data.adapter_list_lock, flags);
        if (retval == ZFCP_KNOWN)
                goto known_adapter;

        /*
	 * Note: It is safe to release the list_lock, as any list changes 
         * are protected by the proc_sema, which must be held to get here
         */

	/* try to allocate new adapter data structure (zeroed) */
	adapter = ZFCP_KMALLOC(sizeof(zfcp_adapter_t), GFP_KERNEL);
	if (!adapter) {
		ZFCP_LOG_INFO(
			"error: Allocation of base adapter "
			"structure failed\n");
                retval = -ENOMEM;
                goto adapter_alloc_failed;
	}

	retval = zfcp_allocate_qdio_queues(adapter);        
        if (retval)
                goto queues_alloc_failed;
 
        retval = zfcp_allocate_low_mem_buffers(adapter);
        if (retval)
                goto failed_low_mem_buffers;

        /* initialise list of ports */
        rwlock_init(&adapter->port_list_lock);
        INIT_LIST_HEAD(&adapter->port_list_head);
        
        /* initialize list of fsf requests */
        rwlock_init(&adapter->fsf_req_list_lock);
	INIT_LIST_HEAD(&adapter->fsf_req_list_head);
        
        /* initialize abort lock */
        rwlock_init(&adapter->abort_lock);

        /* initialise scsi faking structures */
        rwlock_init(&adapter->fake_list_lock);
        init_timer(&adapter->fake_scsi_timer);

	/* initialise some erp stuff */
        init_waitqueue_head(&adapter->erp_thread_wqh);
        init_waitqueue_head(&adapter->erp_done_wqh);
        
        /* initialize lock of associated request queue */
        rwlock_init(&adapter->request_queue.queue_lock);

        /* intitialise SCSI ER timer */
	init_timer(&adapter->scsi_er_timer);

        /* save devno */
        adapter->devno = devno;
        
        /* set FC service class used per default */
        adapter->fc_service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT; 

#ifdef ZFCP_DEBUG_REQUESTS
	/* debug feature area which records fsf request sequence numbers */
	sprintf(dbf_name, ZFCP_REQ_DBF_NAME"0x%04x",adapter->devno);
	adapter->req_dbf = debug_register(
				dbf_name,
				ZFCP_REQ_DBF_INDEX,
				ZFCP_REQ_DBF_AREAS,
				ZFCP_REQ_DBF_LENGTH);
	if (!adapter->req_dbf) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Request debug feature for "
			"adapter with devno 0x%04x could not be generated.\n",
			adapter->devno);
		retval = -ENOMEM;
		goto failed_req_dbf;
	}
	debug_register_view(adapter->req_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->req_dbf, ZFCP_REQ_DBF_LEVEL);
	debug_text_event(adapter->req_dbf, 1, "zzz");
#endif /* ZFCP_DEBUG_REQUESTS */

#ifdef ZFCP_DEBUG_COMMANDS
	/* debug feature area which records SCSI command failures (hostbyte) */
	rwlock_init(&adapter->cmd_dbf_lock);
	sprintf(dbf_name, ZFCP_CMD_DBF_NAME"0x%04x", adapter->devno);
	adapter->cmd_dbf = debug_register(
				dbf_name, 
				ZFCP_CMD_DBF_INDEX,
				ZFCP_CMD_DBF_AREAS,
				ZFCP_CMD_DBF_LENGTH);
	if (!adapter->cmd_dbf) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Command debug feature for "
			"adapter with devno 0x%04x could not be generated.\n",
			adapter->devno);
		retval = -ENOMEM;
		goto failed_cmd_dbf;
	}
	debug_register_view(adapter->cmd_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->cmd_dbf, ZFCP_CMD_DBF_LEVEL);
#endif /* ZFCP_DEBUG_COMMANDS */

#ifdef ZFCP_DEBUG_ABORTS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_ABORT_DBF_NAME"0x%04x", adapter->devno);
	adapter->abort_dbf = debug_register(
				dbf_name, 
				ZFCP_ABORT_DBF_INDEX,
				ZFCP_ABORT_DBF_AREAS,
				ZFCP_ABORT_DBF_LENGTH);
	if (!adapter->abort_dbf) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Abort debug feature for "
			"adapter with devno 0x%04x could not be generated.\n",
			adapter->devno);
		retval = -ENOMEM;
		goto failed_abort_dbf;
	}
	debug_register_view(adapter->abort_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->abort_dbf, ZFCP_ABORT_DBF_LEVEL);
#endif /* ZFCP_DEBUG_ABORTS */

#ifdef ZFCP_DEBUG_INCOMING_ELS
	/* debug feature area which records SCSI command aborts */
	sprintf(dbf_name, ZFCP_IN_ELS_DBF_NAME"0x%04x", adapter->devno);
	adapter->in_els_dbf = debug_register(
				dbf_name,
				ZFCP_IN_ELS_DBF_INDEX,
				ZFCP_IN_ELS_DBF_AREAS,
				ZFCP_IN_ELS_DBF_LENGTH);
	if (!adapter->in_els_dbf) {
		ZFCP_LOG_INFO(
			"error: Out of resources. ELS debug feature for "
			"adapter with devno 0x%04x could not be generated.\n",
			adapter->devno);
		retval = -ENOMEM;
		goto failed_in_els_dbf;
	}
	debug_register_view(adapter->in_els_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->in_els_dbf, ZFCP_IN_ELS_DBF_LEVEL);
#endif /* ZFCP_DEBUG_INCOMING_ELS */

	sprintf(dbf_name, ZFCP_ERP_DBF_NAME"0x%04x", adapter->devno);
	adapter->erp_dbf = debug_register(
				dbf_name, 
				ZFCP_ERP_DBF_INDEX, 
				ZFCP_ERP_DBF_AREAS,
				ZFCP_ERP_DBF_LENGTH);
	if (!adapter->erp_dbf) { 
		ZFCP_LOG_INFO(
			"error: Out of resources. ERP debug feature for "
			"adapter with devno 0x%04x could not be generated.\n",
			adapter->devno);
		retval = -ENOMEM;
		goto failed_erp_dbf;
	}
	debug_register_view(adapter->erp_dbf, &debug_hex_ascii_view);
	debug_set_level(adapter->erp_dbf, ZFCP_ERP_DBF_LEVEL);

        /* Init proc structures */
#ifdef CONFIG_PROC_FS
        ZFCP_LOG_TRACE("Generating proc entry....\n");
	retval = zfcp_create_adapter_proc(adapter);
        if (retval) {
                ZFCP_LOG_INFO(
			"error: Out of resources. "
			"proc-file entries for adapter with "
			"devno 0x%04x could not be generated\n",
			adapter->devno);
                goto proc_failed;
        }
        ZFCP_LOG_TRACE("Proc entry created.\n");
#endif

	retval = zfcp_erp_thread_setup(adapter);
        if (retval) {
                ZFCP_LOG_INFO(
			"error: out of resources. "
			"error recovery thread for the adapter with "
			"devno 0x%04x could not be started\n",
			adapter->devno);
                goto thread_failed;
        }

#ifndef ZFCP_PARANOIA_DEAD_CODE
        /* set magics */
        adapter->common_magic = ZFCP_MAGIC;
        adapter->specific_magic = ZFCP_MAGIC_ADAPTER;
#endif

#ifdef ZFCP_STAT_REQSIZES
	rwlock_init(&adapter->stat_lock);
	atomic_set(&adapter->stat_on, 0);
	atomic_set(&adapter->stat_errors, 0);
	INIT_LIST_HEAD(&adapter->read_req_head);
	INIT_LIST_HEAD(&adapter->write_req_head);
#endif

        /* put allocated adapter at list tail */
	write_lock_irqsave(&zfcp_data.adapter_list_lock, flags);
        list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	zfcp_data.adapters++;
	write_unlock_irqrestore(&zfcp_data.adapter_list_lock, flags);

	sprintf(adapter->name, "0x%04x", adapter->devno);
	ASCEBC(adapter->name, strlen(adapter->name));

	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &adapter->status);

	ZFCP_LOG_TRACE(
		"adapter allocated at 0x%lx, %i adapters in list\n",
		(unsigned long)adapter,
		zfcp_data.adapters);
        goto out;

thread_failed:
	zfcp_delete_adapter_proc(adapter);

proc_failed:
	debug_unregister(adapter->erp_dbf);

failed_erp_dbf:
#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
failed_in_els_dbf:
#endif

#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
failed_abort_dbf:
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
failed_cmd_dbf:
#endif

#ifdef ZFCP_DEBUG_REQUESTS
        debug_unregister(adapter->req_dbf);
failed_req_dbf:
#endif

failed_low_mem_buffers:
        zfcp_free_low_mem_buffers(adapter);
        zfcp_free_qdio_queues(adapter);

queues_alloc_failed:
        ZFCP_LOG_TRACE(
		"freeing adapter struct 0x%lx\n",
		(unsigned long) adapter);
	/* 'typeof' works as well */
        ZFCP_KFREE(adapter, sizeof(typeof(adapter)));

adapter_alloc_failed:
	adapter = NULL;

known_adapter:
out:
	*adapter_p = adapter;
        ZFCP_LOG_TRACE("exit (%d)\n", retval);
        
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_adapter_dequeue
 *
 * purpose:	dequeues the specified adapter from the list in the driver data
 *
 * returns:	0 - zfcp_adapter_t data structure successfully removed
 *		!0 - zfcp_adapter_t data structure could not be removed
 *			(e.g. still used)
 *
 * locks:	adapter list write lock is assumed to be held by caller
 *              adapter->fsf_req_list_lock is taken and released within this 
 *              function and must not be held on entry
 */
static int zfcp_adapter_dequeue(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;
	unsigned long flags;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", (unsigned long)adapter);

	/*
	 * sanity check:
	 * I/O interrupt should be disabled, leave if not
	 */

        /* Note: no adapter_list_lock is needed as we have the proc_sema */ 

	/* sanity check: valid adapter data structure address */
	if (!adapter) {
		ZFCP_LOG_NORMAL(
			"bug: Pointer to an adapter struct is a null "
                        "pointer\n");
		retval = -EINVAL;
		goto out;
	}

	/* sanity check: no remote ports pending */
	if (adapter->ports) {
		ZFCP_LOG_NORMAL(
			"bug: Adapter with devno 0x%04x is still in use, "
			"%i remote ports are still existing "
                        "(debug info 0x%lx)\n",
			adapter->devno, 
                        adapter->ports,
                        (unsigned long)adapter);
		retval = -EBUSY;
		goto out;
	}

	/* sanity check: no pending FSF requests */
	read_lock_irqsave(&adapter->fsf_req_list_lock, flags);

	retval = !list_empty(&adapter->fsf_req_list_head);

	read_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
	if (retval) {
		ZFCP_LOG_NORMAL(
			"bug: Adapter with devno 0x%04x is still in use, "
			"%i requests are still outstanding "
                        "(debug info 0x%lx)\n",
			adapter->devno, 
                        atomic_read(&adapter->fsf_reqs_active),
                        (unsigned long)adapter);
		retval = -EBUSY;
		goto out;
	}

	/* remove specified adapter data structure from list */
	list_del(&adapter->list);

	/* decrease number of adapters in list */
	zfcp_data.adapters--;

	ZFCP_LOG_TRACE(
		"adapter 0x%lx removed from list, "
		"%i adapters still in list\n",
		(unsigned long)adapter,
		zfcp_data.adapters);

        retval = zfcp_erp_thread_kill(adapter);

#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear(adapter, &adapter->read_req_head);
	zfcp_statistics_clear(adapter, &adapter->write_req_head);
#endif

        zfcp_delete_adapter_proc(adapter);
        ZFCP_LOG_TRACE("Proc entry removed.\n");

        debug_unregister(adapter->erp_dbf);

#ifdef ZFCP_DEBUG_REQUESTS
        debug_unregister(adapter->req_dbf);
#endif

#ifdef ZFCP_DEBUG_COMMANDS
	debug_unregister(adapter->cmd_dbf);
#endif

#ifdef ZFCP_DEBUG_ABORTS
	debug_unregister(adapter->abort_dbf);
#endif

#ifdef ZFCP_DEBUG_INCOMING_ELS
	debug_unregister(adapter->in_els_dbf);
#endif


        zfcp_free_low_mem_buffers(adapter);
	/* free memory of adapter data structure and queues */
        zfcp_free_qdio_queues(adapter);
        ZFCP_LOG_TRACE("Freeing adapter structure.\n");
        ZFCP_KFREE(adapter, sizeof(zfcp_adapter_t));

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;	/* succeed */

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_port_enqueue
 *
 * purpose:	enqueues an remote port at the end of the port list
 *		associated with the specified adapter
 *              all port internal structures are set-up and the proc-fs
 *              entry is also allocated
 *              some SCSI-stack structures are modified for the port
 *
 * returns:	0 if a new port was successfully enqueued
 *              ZFCP_KNOWN if a port with the requested wwpn already exists
 *              -ENOMEM if allocation failed
 *              -EINVAL if at least one of the specified parameters was wrong
 *
 * locks:       proc_sema must be held to serialise changes to the port list
 *              adapter->port_list_lock is taken and released several times
 *              within this function (must not be held on entry)
 */
static int 
	zfcp_port_enqueue(
		zfcp_adapter_t *adapter,
		scsi_id_t scsi_id,
		wwn_t wwpn,
		u32 status,
                zfcp_port_t **port_p)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	zfcp_port_t *port = NULL;
	int check_scsi_id, check_wwpn;
        unsigned long flags;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx scsi_id=%i wwpn=0x%Lx status=0x%x)\n",
		(unsigned long)adapter,
		scsi_id,
		(llui_t)wwpn,
		status);

        /* to check that there is not a port with either this 
	 * SCSI ID or WWPN already in list
	 */
	check_scsi_id = !(status & ZFCP_STATUS_PORT_NO_SCSI_ID);
	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);

	if (check_scsi_id && check_wwpn) {
		read_lock_irqsave(&adapter->port_list_lock, flags);
		ZFCP_FOR_EACH_PORT(adapter, port) {
			if ((port->scsi_id != scsi_id) && (port->wwpn != wwpn))
				continue;
			if ((port->scsi_id == scsi_id) && (port->wwpn == wwpn)) {
				ZFCP_LOG_TRACE(
					"Port with SCSI ID 0x%x and WWPN 0x%016Lx already in list\n",
					scsi_id, (llui_t)wwpn);
				retval = ZFCP_KNOWN;
				read_unlock_irqrestore(&adapter->port_list_lock, flags);
				goto known_port;
			}
			ZFCP_LOG_NORMAL(
				"user error: new mapping 0x%x:0x%016Lx "
				"does not match existing mapping 0x%x:0x%016Lx "
				"(adapter devno 0x%04x)\n",
				scsi_id,
				(llui_t)wwpn,
				port->scsi_id,
				(llui_t)port->wwpn,
				port->adapter->devno);
			retval = -EINVAL;
			read_unlock_irqrestore(&adapter->port_list_lock, flags);
			goto match_failed;
		}
		read_unlock_irqrestore(&adapter->port_list_lock, flags);
	}

        /*
	 * Note: It is safe to release the list_lock, as any list changes 
         * are protected by the proc_sema, which must be held to get here
         */

	/* try to allocate new port data structure (zeroed) */
	port = ZFCP_KMALLOC(sizeof(zfcp_port_t), GFP_KERNEL);
	if (!port) {
		ZFCP_LOG_INFO(
			"error: Allocation of port struct failed. "
			"Out of memory.\n");
                retval = -ENOMEM;
                goto port_alloc_failed;
        }

        /* initialize unit list */
        rwlock_init(&port->unit_list_lock);
        INIT_LIST_HEAD(&port->unit_list_head);

        /* save pointer to "parent" adapter */
        port->adapter = adapter;

        /* save SCSI ID */
	if (check_scsi_id)
		port->scsi_id = scsi_id;

        /* save WWPN */
	if (check_wwpn)
		port->wwpn = wwpn;

        /* save initial status */
        atomic_set_mask(status, &port->status);
 
#ifndef ZFCP_PARANOIA_DEAD_CODE
        /* set magics */
        port->common_magic = ZFCP_MAGIC;
        port->specific_magic = ZFCP_MAGIC_PORT;
#endif

	/* Init proc structures */
#ifdef CONFIG_PROC_FS
        ZFCP_LOG_TRACE("Generating proc entry....\n");
	retval = zfcp_create_port_proc(port);
        if (retval)
                goto proc_failed;
        ZFCP_LOG_TRACE("Proc entry created.\n");
#endif
      
        if (check_scsi_id) {
	        /*
        	 * update max. SCSI ID of remote ports attached to
	         * "parent" adapter if necessary
        	 * (do not care about the adapters own SCSI ID)
	         */
		if (adapter->max_scsi_id < scsi_id) {
			adapter->max_scsi_id = scsi_id;
			ZFCP_LOG_TRACE(
				"max. SCSI ID of adapter 0x%lx now %i\n",
				(unsigned long)adapter,
				scsi_id);
		}
		/*
		 * update max. SCSI ID of remote ports attached to
		 * "parent" host (SCSI stack) if necessary
		 */
		if (adapter->scsi_host &&
		    (adapter->scsi_host->max_id < (scsi_id + 1))) {
			adapter->scsi_host->max_id = scsi_id + 1;
			ZFCP_LOG_TRACE(
				"max. SCSI ID of ports attached "
				"via host # %d now %i\n",
				adapter->scsi_host->host_no,
				adapter->scsi_host->max_id);
        	}
        }

        /* Port is allocated, enqueue it*/
        write_lock_irqsave(&adapter->port_list_lock,flags);
        list_add_tail(&port->list, &adapter->port_list_head);
	adapter->ports++;
        write_unlock_irqrestore(&adapter->port_list_lock,flags);
        
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);

        ZFCP_LOG_TRACE(
		"port allocated at 0x%lx, %i ports in list "
		"of adapter 0x%lx\n",
		(unsigned long)port,
		adapter->ports,
		(unsigned long)adapter);
        goto out;

proc_failed:
        ZFCP_KFREE(port, sizeof(zfcp_port_t));
        ZFCP_LOG_TRACE(
		"freeing port struct 0x%lx\n",
		(unsigned long) port);

port_alloc_failed:
match_failed:
	port = NULL;

known_port:
out:
	*port_p = port;
	ZFCP_LOG_TRACE("exit (%d)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_port_dequeue
 *
 * purpose:	dequeues the specified port from the list of the
 *		 "parent" adapter
 *
 * returns:	0 - zfcp_port_t data structure successfully removed
 *		!0 - zfcp_port_t data structure could not be removed
 *			(e.g. still used)
 *
 * locks :	port list write lock is assumed to be held by caller
 */
static int zfcp_port_dequeue(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;

	ZFCP_LOG_TRACE("enter (port=0x%lx)\n", (unsigned long)port);

	/* sanity check: valid port data structure address (simple check) */
	if (!port) {
		ZFCP_LOG_NORMAL(
			"bug: Pointer to a port struct is a null "
                        "pointer\n");
		retval = -EINVAL;
		goto out;
	}

	/*
	 * sanity check:
	 * leave if required list lock is not held,
	 * do not know whether it is held by the calling routine (required!)
	 * protecting this critical area or someone else (must not occur!),
	 * but a lock not held by anyone is definetely wrong
	 */
	if (!spin_is_locked(&port->adapter->port_list_lock)) {
		ZFCP_LOG_NORMAL("bug: Port list lock not held "
                               "(debug info 0x%lx)\n",
                               (unsigned long) port);
		retval = -EPERM;
		goto out;
	}

	/* sanity check: no logical units pending */
	if (port->units) {
		ZFCP_LOG_NORMAL(
			"bug: Port with SCSI-id 0x%x is still in use, "
			"%i units (LUNs) are still existing "
                        "(debug info 0x%lx)\n",
			port->scsi_id, 
                        port->units,
                        (unsigned long)port);
		retval = -EBUSY;
		goto out;
	}

	/* remove specified port data structure from list */
	list_del(&port->list);

	/* decrease number of ports in list */
	port->adapter->ports--;

	ZFCP_LOG_TRACE(
		"port 0x%lx removed from list of adapter 0x%lx, "
		"%i ports still in list\n",
		(unsigned long)port,
		(unsigned long)port->adapter,
		port->adapter->ports);

	/* free memory of port data structure */
        ZFCP_LOG_TRACE("Deleting proc entry......\n");
        zfcp_delete_port_proc(port);
        ZFCP_LOG_TRACE("Proc entry removed.\n");
	ZFCP_KFREE(port, sizeof(zfcp_port_t));

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;	/* succeed */

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:     zfcp_scsi_low_mem_buffer_timeout_handler
 *
 * purpose:      This function needs to be called whenever the SCSI command
 *               in the low memory buffer does not return.
 *               Re-opening the adapter means that the command can be returned
 *               by zfcp (it is guarranteed that it does not return via the
 *               adapter anymore). The buffer can then be used again.
 *    
 * returns:      sod all
 */
static void zfcp_scsi_low_mem_buffer_timeout_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	zfcp_adapter_t *adapter = (zfcp_adapter_t *)data   ;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", 
                       (unsigned long) data);
        /*DEBUG*/
        ZFCP_LOG_INFO("*****************************mem_timeout******************************\n");
        zfcp_erp_adapter_reopen(adapter, 0);
	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_fsf_scsi_er_timeout_handler
 *
 * purpose:     This function needs to be called whenever a SCSI error recovery
 *              action (abort/reset) does not return.
 *              Re-opening the adapter means that the command can be returned
 *              by zfcp (it is guarranteed that it does not return via the
 *              adapter anymore). The buffer can then be used again.
 *    
 * returns:     sod all
 */
static void zfcp_fsf_scsi_er_timeout_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	zfcp_adapter_t *adapter = (zfcp_adapter_t *)data;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", 
                       (unsigned long) data);
        /*DEBUG*/
        ZFCP_LOG_INFO("*****************************er_timeout******************************\n");
        zfcp_erp_adapter_reopen(adapter, 0);
	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * memory pool implementation
 * the first four functions (element_alloc, element_release, element_get, element_put)
 * are for internal use,
 * the other four functions (create, destroy, find, free) are the external interface
 * which should be used by exploiter of the memory pool
 */

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

/* associate a buffer with the specified memory pool element */
static inline int zfcp_mem_pool_element_alloc(
		zfcp_mem_pool_t *pool,
		int index)
{
	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, index=%i)\n",
		(unsigned long)pool,
		index);

	pool->element[index].buffer = ZFCP_KMALLOC(pool->size, GFP_KERNEL);
	if (!pool->element[index].buffer) {
		retval = -ENOMEM;
        };

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}


/* release the buffer associated with the specified memory pool element */
static inline int zfcp_mem_pool_element_free(
		zfcp_mem_pool_t *pool,
		int index)
{
	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, index=%i)\n",
		(unsigned long)pool,
		index);

	if (atomic_read(&pool->element[index].use) != 0) {
		ZFCP_LOG_NORMAL("bug: memory pool is in use\n");
		retval = -EINVAL;
	} else if (pool->element[index].buffer)
		ZFCP_KFREE(pool->element[index].buffer, pool->size);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}


/* try to get hold of buffer associated with the specified memory pool element */
static inline void *zfcp_mem_pool_element_get(
		zfcp_mem_pool_t *pool,
		int index)
{
	void *buffer;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, index=%i)\n",
		(unsigned long)pool,
		index);

	ZFCP_LOG_DEBUG("buffer=0x%lx\n",
                       (unsigned long)pool->element[index].buffer);
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)pool->element,
		pool->entries * sizeof(zfcp_mem_pool_element_t));

	if (atomic_compare_and_swap(0, 1, &pool->element[index].use))
		buffer = NULL;
	else {
                memset(pool->element[index].buffer, 0, pool->size);
                buffer = pool->element[index].buffer;
        }


	ZFCP_LOG_TRACE("exit (0x%lx)\n", (unsigned long)buffer);

	return buffer;
}


/* mark buffer associated with the specified memory pool element as available */
static inline int zfcp_mem_pool_element_put(
		zfcp_mem_pool_t *pool,
		int index)
{
	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, index=%i)\n",
		(unsigned long)pool,
		index);

	if (atomic_compare_and_swap(1, 0, &pool->element[index].use)) {
		ZFCP_LOG_NORMAL("bug: memory pool is broken (element not in use)\n");
		retval = -EINVAL;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}


/*
 * creation of a new memory pool including setup of management data structures
 * as well as allocation of memory pool elements
 * (this routine does not cleanup partially set up pools, instead the corresponding
 * destroy routine should be called)
 */
static inline int zfcp_mem_pool_create(zfcp_mem_pool_t *pool,
                                       int entries, int size,
                                       void (*function) (unsigned long),
                                       unsigned long data)
{
	int retval = 0;
	int i;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, entries=%i, size=%i)\n",
		(unsigned long)pool,
		entries,
		size);

	if (pool->element || pool->entries) {
		ZFCP_LOG_NORMAL("bug: memory pool is broken (pool is in use)\n");
		retval = -EINVAL;
		goto out;
	}

	pool->element = ZFCP_KMALLOC(entries * sizeof(zfcp_mem_pool_element_t),
                                     GFP_KERNEL);
	if (!pool->element) {
		ZFCP_LOG_NORMAL("warning: memory pool not avalaible\n");
		retval = -ENOMEM;
		goto out;
	}
        /* Ensure that the use flag is 0. */

        memset(pool->element, 0, entries * sizeof(zfcp_mem_pool_element_t)); 
	pool->entries = entries;
	pool->size = size;

	for (i = 0; i < entries; i++) {
		retval = zfcp_mem_pool_element_alloc(pool, i);
		if (retval) {
			ZFCP_LOG_NORMAL("warning: memory pool not avalaible\n");
                        retval = -ENOMEM;
			goto out;
		}
	}
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)pool->element,
		entries * sizeof(zfcp_mem_pool_element_t));

	init_timer(&pool->timer);
	pool->timer.function = function;
	pool->timer.data = data;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}


/*
 * give up memory pool with all its memory pool elements as well as
 * data structures used for management purposes
 * (this routine is able to handle partially alloacted memory pools)
 */
static inline int zfcp_mem_pool_destroy(
		zfcp_mem_pool_t *pool)
{
	int retval = 0;
	int i;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx)\n",
		(unsigned long)pool);

	for (i = 0; i < pool->entries; i++)
		retval |= zfcp_mem_pool_element_free(pool, i);

	if (pool->element)
		ZFCP_KFREE(pool->element, pool->entries);

	pool->element = NULL;
	pool->entries = 0;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}


/*
 * try to find next available element in the specified memory pool,
 * on success get hold of buffer associated with the selected element
 */
static inline void* zfcp_mem_pool_find(
		zfcp_mem_pool_t *pool)
{
	void *buffer = NULL;
	int i;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx)\n",
		(unsigned long)pool);

	for (i = 0; i < pool->entries; i++) {
		buffer = zfcp_mem_pool_element_get(pool, i);
		if (buffer)
			break;
	}

        if ((buffer != 0) && (pool->timer.function != 0)) {
                /*
                 * watch low mem buffer
                 * Note: Take care if more than 1 timer is active.
                 * The first expired timer has to delete all other
                 * timers. (See ZFCP_SCSI_LOW_MEM_TIMEOUT and
                 * ZFCP_SCSI_ER_TIMEOUT)
                 */
                pool->timer.expires = jiffies + ZFCP_SCSI_LOW_MEM_TIMEOUT;
                add_timer(&pool->timer);
        }

	ZFCP_LOG_TRACE("exit (0x%lx)\n", (unsigned long)buffer);

	return buffer;
}


/*
 * make buffer available to memory pool again,
 * (since buffers are specified by their own address instead of the
 * memory pool element they are associated with a search for the
 * right element of the given memory pool)
 */ 
static inline int zfcp_mem_pool_return(void *buffer, zfcp_mem_pool_t *pool)
{
	int retval = 0;
	int i;

	ZFCP_LOG_TRACE(
		"enter (buffer=0x%lx, pool=0x%lx)\n",
		(unsigned long)buffer,
		(unsigned long)pool);

        if (pool->timer.function) {
                del_timer(&pool->timer);
        }

	for (i = 0; i < pool->entries; i++) {
		if (buffer == pool->element[i].buffer) {
			retval = zfcp_mem_pool_element_put(pool, i);
			goto out;
		}
	}

	if (i == pool->entries) {
		ZFCP_LOG_NORMAL("bug: memory pool is broken (buffer not found)\n");
		retval = -EINVAL;
	}

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX

/* end of memory pool implementation */

/*
 * function:	zfcp_fsf_req_alloc
 *
 * purpose:     Obtains an fsf_req and potentially a qtcb (for all but 
 *              unsolicited requests) via helper functions
 *              Does some initial fsf request set-up.
 *              
 * returns:	pointer to allocated fsf_req if successfull
 *              NULL otherwise
 *
 * locks:       none
 *
 */
static zfcp_fsf_req_t *zfcp_fsf_req_alloc(zfcp_mem_pool_t *pool, int flags,
                                          int kmalloc_flags)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF
        
        zfcp_fsf_req_t *fsf_req = NULL;

        if (!(flags & ZFCP_REQ_USE_MEMPOOL)) {
                fsf_req = ZFCP_KMALLOC(sizeof(struct zfcp_fsf_req_pool_buffer),
                                       kmalloc_flags);
        }

	if ((fsf_req == 0) && (pool != 0)) {
		fsf_req = zfcp_mem_pool_find(pool);
		if (fsf_req){
			fsf_req->status |= ZFCP_STATUS_FSFREQ_POOL;
                        fsf_req->pool = pool;
                }
	}

	if (fsf_req == 0) {
		ZFCP_LOG_DEBUG("error: Out of memory. Allocation of FSF "
                              "request structure failed\n");
	}

	return fsf_req;	
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_fsf_req_free
 *
 * purpose:     Frees the memory of an fsf_req (and potentially a qtcb) or
 *              returns it into the pool via helper functions.
 *
 * returns:     sod all
 *
 * locks:       none
 */
static inline int zfcp_fsf_req_free(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL) {
                retval = zfcp_mem_pool_return(fsf_req, fsf_req->pool);
        } else {
                ZFCP_KFREE(fsf_req, sizeof(struct zfcp_fsf_req_pool_buffer));
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_unit_enqueue
 *
 * purpose:	enqueues a logical unit at the end of the unit list
 *		associated with the specified port
 *              also sets up unit internal structures
 *
 * returns:	0 if a new unit was successfully enqueued
 *              -ENOMEM if the allocation failed
 *              -EINVAL if at least one specified parameter was faulty    
 *
 * locks:	proc_sema must be held to serialise changes to the unit list
 *              port->unit_list_lock is taken and released several times
 */
static int
	zfcp_unit_enqueue(
		zfcp_port_t *port,
		scsi_lun_t scsi_lun,
		fcp_lun_t fcp_lun,
                zfcp_unit_t **unit_p)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	zfcp_unit_t *unit;
        int retval = 0;
        unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx scsi_lun=%i fcp_lun=0x%Lx)\n",
		(unsigned long)port, scsi_lun, (llui_t)fcp_lun);

	/*
	 * check that there is no unit with either this
	 * SCSI LUN or FCP_LUN already in list
         * Note: Unlike for the adapter and the port, this is an error
	 */
	read_lock_irqsave(&port->unit_list_lock, flags);
	ZFCP_FOR_EACH_UNIT(port, unit) {
		if (unit->scsi_lun == scsi_lun) {
			ZFCP_LOG_NORMAL(
				"Warning: A Unit with SCSI LUN 0x%x already "
				"exists. Skipping record.\n",
				scsi_lun);
                        retval = -EINVAL;
                        break;
                } else if (unit->fcp_lun == fcp_lun) {
                        ZFCP_LOG_NORMAL(
                              "Warning: A Unit with FCP_LUN 0x%016Lx is already present. "
                              "Record was ignored\n",
                              (llui_t)fcp_lun);
                        retval = -EINVAL;
                        break;
		}
	}
        read_unlock_irqrestore(&port->unit_list_lock, flags);
        if (retval == -EINVAL)
                goto known_unit;

	/* try to allocate new unit data structure (zeroed) */
	unit = ZFCP_KMALLOC(sizeof(zfcp_unit_t), GFP_KERNEL);
	if (!unit) {
		ZFCP_LOG_INFO("error: Allocation of unit struct failed. "
                                "Out of memory.\n");
                retval = -ENOMEM;
                goto unit_alloc_failed;
        }
        
        /* save pointer to "parent" port */
        unit->port = port;
        
        /* save SCSI LUN */
        unit->scsi_lun = scsi_lun;
        
        /* save FCP_LUN */
        unit->fcp_lun = fcp_lun;
 
#ifndef ZFCP_PARANOIA_DEAD_CODE
        /* set magics */
        unit->common_magic = ZFCP_MAGIC;
        unit->specific_magic = ZFCP_MAGIC_UNIT;
#endif

        /* Init proc structures */
#ifdef CONFIG_PROC_FS
        ZFCP_LOG_TRACE("Generating proc entry....\n");
	retval = zfcp_create_unit_proc(unit);
        if (retval) {
                ZFCP_LOG_TRACE(
			"freeing unit struct 0x%lx\n",
			(unsigned long) unit);
                goto proc_failed;
        }
        ZFCP_LOG_TRACE("Proc entry created.\n");
#endif
       
        /*
         * update max. SCSI LUN of logical units attached to 
         * "parent" remote port if necessary
         */
        if (port->max_scsi_lun < scsi_lun) {
                port->max_scsi_lun = scsi_lun;
                ZFCP_LOG_TRACE(
			"max. SCSI LUN of units of remote "
			"port 0x%lx now %i\n",
			(unsigned long)port,
			scsi_lun);
        }
        
        /*
         * update max. SCSI LUN of logical units attached to
         * "parent" adapter if necessary
         */
        if (port->adapter->max_scsi_lun < scsi_lun) {
                port->adapter->max_scsi_lun = scsi_lun;
                ZFCP_LOG_TRACE(
			"max. SCSI LUN of units attached "
			"via adapter with devno 0x%04x now %i\n",
			port->adapter->devno,
			scsi_lun);
        }

        /*
         * update max. SCSI LUN of logical units attached to
         * "parent" host (SCSI stack) if necessary
         */
        if (port->adapter->scsi_host &&
            (port->adapter->scsi_host->max_lun < (scsi_lun + 1))) {
                port->adapter->scsi_host->max_lun = scsi_lun + 1;
                ZFCP_LOG_TRACE(
			"max. SCSI LUN of units attached "
			"via host # %d now %i\n",
			port->adapter->scsi_host->host_no,
			port->adapter->scsi_host->max_lun);
        }

        /* Unit is new and needs to be added to list */
        write_lock_irqsave(&port->unit_list_lock, flags);
        list_add_tail(&unit->list, &port->unit_list_head);
	port->units++;
        write_unlock_irqrestore(&port->unit_list_lock, flags);
        
	/* also add unit to map list to get them in order of addition */
	list_add_tail(&unit->map_list, &zfcp_data.map_list_head);
         
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);

        ZFCP_LOG_TRACE(
		"unit allocated at 0x%lx, %i units in "
		"list of port 0x%lx\n",
		(unsigned long)unit,
		port->units,
		(unsigned long)port);
        goto out;                

proc_failed:
        ZFCP_KFREE(unit, sizeof(zfcp_unit_t));

unit_alloc_failed:
	unit = NULL;

known_unit:
out:
	*unit_p = unit;
	ZFCP_LOG_TRACE("exit (%d)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_unit_dequeue
 *
 * purpose:	dequeues the specified logical unit from the list of
 *		the "parent" port
 *
 * returns:	0 - zfcp_unit_t data structure successfully removed
 *		!0 - zfcp_unit_t data structure could not be removed
 *			(e.g. still used)
 *
 * locks :	unit list write lock is assumed to be held by caller
 */
static int zfcp_unit_dequeue(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;

	ZFCP_LOG_TRACE("enter (unit=0x%lx)\n", (unsigned long)unit);

	/* sanity check: valid unit data structure address (simple check) */
	if (!unit) {
		ZFCP_LOG_NORMAL(
			"bug: Pointer to a unit struct is a null "
                        "pointer\n");
                retval = -EINVAL;
		goto out;
	}

	/*
	 * sanity check:
	 * leave if required list lock is not held,
	 * do not know whether it is held by the calling routine (required!)
	 * protecting this critical area or someone else (must not occur!),
	 * but a lock not held by anyone is definetely wrong
	 */
	if (!spin_is_locked(&unit->port->unit_list_lock)) {
		ZFCP_LOG_NORMAL("bug: Unit list lock not held "
                               "(debug info 0x%lx)\n",
                               (unsigned long) unit);
		retval = -EPERM;
		goto out;
	}

	/* remove specified unit data structure from list */
	list_del(&unit->list);

	/* decrease number of units in list */
	unit->port->units--;

	ZFCP_LOG_TRACE(
		"unit 0x%lx removed, %i units still in list of port 0x%lx\n",
		(unsigned long)unit,
		unit->port->units,
		(unsigned long)unit->port);

        ZFCP_LOG_TRACE("Deleting proc entry......\n");
        zfcp_delete_unit_proc(unit);
        ZFCP_LOG_TRACE("Proc entry removed.\n");

	/* free memory of unit data structure */
	ZFCP_KFREE(unit, sizeof(zfcp_unit_t));

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;	/* succeed */

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:   zfcp_create_unit_proc
 *
 * purpose:    creates proc-dir and status file for the unit passed in
 *
 * returns:    0      if all entries could be created properly
 *             -EPERM if at least one entry could not be created
 *                    (all entries are guarranteed to be freed in this 
 *                     case)
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_create_unit_proc(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     char unit_scsi_lun[20];
     int length = 0;
     int retval = 0;
     
     ZFCP_LOG_TRACE("enter (unit=0x%lx)\n",
                    (unsigned long)unit);

     length += sprintf(&unit_scsi_lun[length],"lun0x%x", unit->scsi_lun);
     unit_scsi_lun[length]='\0';
     unit->proc_dir = proc_mkdir (unit_scsi_lun, 
                                  unit->port->proc_dir);
     if (!unit->proc_dir) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the unit "
                          "with SCSI LUN 0x%x failed. Out of resources.\n",
                          unit_scsi_lun,
                          unit->scsi_lun);
          retval=-EPERM;
          goto out;
     }
     unit->proc_file=create_proc_entry(ZFCP_STATUS_FILE, 
                                       S_IFREG|S_IRUGO|S_IWUSR,
                                       unit->proc_dir);
     if (!unit->proc_file) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the unit "
                          "with SCSI LUN 0x%x failed. Out of resources.\n",
                          ZFCP_STATUS_FILE,
                          unit->scsi_lun);
          remove_proc_entry (unit_scsi_lun, unit->port->proc_dir);          
          retval=-EPERM;
          goto out;
     }

     unit->proc_file->proc_fops = &zfcp_unit_fops;
     unit->proc_file->data=(void *)unit;
     
     ZFCP_LOG_TRACE("exit (%i)\n", retval);
out:
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_delete_unit_proc
 *
 * purpose:    deletes proc-dir and status file for the unit passed in
 *
 * returns:    0 in all cases
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_delete_unit_proc(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     char unit_scsi_lun[20];
     int length = 0;     
     int retval = 0;

     ZFCP_LOG_TRACE("enter (unit=0x%lx)\n",
                    (unsigned long)unit);
     
     remove_proc_entry (ZFCP_STATUS_FILE, 
                        unit->proc_dir);
     length += sprintf(&unit_scsi_lun[length],"lun0x%x", unit->scsi_lun);
     unit_scsi_lun[length]='\0';
     remove_proc_entry (unit_scsi_lun, unit->port->proc_dir);

     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_create_port_proc
 *
 * purpose:    creates proc-dir and status file for the port passed in
 *
 * returns:    0      if all entries could be created properly
 *             -EPERM if at least one entry could not be created
 *                    (all entries are guarranteed to be freed in this 
 *                     case)
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_create_port_proc(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     char port_scsi_id[20];
     int length = 0;
     int retval = 0;
     
     length +=sprintf(&port_scsi_id[length],"id0x%x", port->scsi_id);
     port_scsi_id[length]='\0';
     port->proc_dir = proc_mkdir (port_scsi_id, 
                                  port->adapter->proc_dir);
     if (!port->proc_dir) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the port "
                          "with SCSI-id 0x%x failed. Out of resources.\n",
                          port_scsi_id,
                          port->scsi_id);
          retval=-EPERM;
          goto out;
     }
     ZFCP_LOG_TRACE("enter (port=0x%lx)\n",
                    (unsigned long)port);
     port->proc_file=create_proc_entry(ZFCP_STATUS_FILE, 
                                       S_IFREG|S_IRUGO|S_IWUSR,
                                       port->proc_dir);
     if (!port->proc_file) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the port "
                          "with SCSI-id 0x%x failed. Out of resources.\n",
                          ZFCP_STATUS_FILE,
                          port->scsi_id);
          remove_proc_entry (port_scsi_id, port->adapter->proc_dir);
          retval=-EPERM;
          goto out;
     }

     port->proc_file->proc_fops = &zfcp_port_fops;
     port->proc_file->data=(void *)port;
     
out:
     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_delete_port_proc
 *
 * purpose:    deletes proc-dir and status file for the port passed in
 *
 * returns:    0 in all cases
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_delete_port_proc(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     char port_scsi_id[20];
     int length = 0;     
     int retval = 0;

     ZFCP_LOG_TRACE("enter (port=0x%lx)\n",
                    (unsigned long)port);
     
     remove_proc_entry (ZFCP_STATUS_FILE, port->proc_dir);
     length = 0;
     length +=sprintf(&port_scsi_id[length],"id0x%x", port->scsi_id);
     port_scsi_id[length]='\0';
     remove_proc_entry (port_scsi_id, port->adapter->proc_dir);
     
     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_create_adapter_proc
 *
 * purpose:    creates proc-dir and status file for the adapter passed in
 *
 * returns:    0      if all entries could be created properly
 *             -EPERM if at least one entry could not be created
 *                    (all entries are guarranteed to be freed in this 
 *                     case)
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_create_adapter_proc(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     char devno[20];
     int length = 0;
     int retval = 0;

     ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n",
                    (unsigned long)adapter);

     length +=sprintf(&devno[length],"devno0x%04x", adapter->devno);
     devno[length]='\0';
     adapter->proc_dir = proc_mkdir (devno, zfcp_data.proc_dir);
     if (!adapter->proc_dir) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the adapter "
                          "with devno 0x%04x failed. Out of resources.\n",
                          devno,
                          adapter->devno);
          retval=-EPERM;
          goto out;
     }
     adapter->proc_file=create_proc_entry(ZFCP_STATUS_FILE, 
                                          S_IFREG|S_IRUGO|S_IWUSR,
                                          adapter->proc_dir);
     if (!adapter->proc_file) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for the adapter "
                          "with devno 0x%04x failed. Out of resources.\n",
                          ZFCP_STATUS_FILE,
                          adapter->devno);
          remove_proc_entry (devno, zfcp_data.proc_dir);
          retval=-EPERM;
          goto out;
     }

     adapter->proc_file->proc_fops = &zfcp_adapter_fops;

     adapter->proc_file->data=(void *)adapter;

     out:

     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_delete_adapter_proc
 *
 * purpose:    deletes proc-dir and status file for the adapter passed in
 *
 * returns:    0 in all cases
 *
 * locks:      proc_sema must be held on call and throughout the function  
 */
int zfcp_delete_adapter_proc(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
        char devno[20];
        int length = 0;
        int retval = 0;
        
        ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n",
                       (unsigned long)adapter);
        
        remove_proc_entry (ZFCP_STATUS_FILE, adapter->proc_dir);
        length += sprintf(&devno[length],"devno0x%04x", adapter->devno);
        devno[length]='\0';
        remove_proc_entry (devno, zfcp_data.proc_dir);
        
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:   zfcp_open_parm_proc
 *
 * purpose:    sets-up and fills the contents of the parm proc_entry
 *             during a read access
 *
 * retval:     0 if successfull
 *             -ENOMEM if at least one buffer could not be allocated
 *              (all buffers will be freed on exit)
 */
int zfcp_open_parm_proc(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI
	
/*
 * Note: modified proc fs utilization (instead of using ..._generic):
 *
 *  - to avoid (SMP) races, allocate buffers for output using
 *    the private_data member in the respective file struct
 *    such that read() just has to copy out of this buffer
 *
 */
	int len = 0;
	procbuf_t *pbuf;
        int retval=0;

	ZFCP_LOG_TRACE("enter (inode=0x%lx, file=0x%lx)\n",
		(unsigned long)inode,
		(unsigned long) file);

#if 0
	/* DEBUG: force an abort which is being hung than, usage of mod_parm dismisses pending fsf_req */
	ZFCP_LOG_NORMAL("try to recover forced and hung abort\n");
	zfcp_erp_adapter_reopen(ZFCP_FIRST_ADAPTER, 0);
#endif

	pbuf = ZFCP_KMALLOC(sizeof(procbuf_t), GFP_KERNEL);
	if (pbuf == NULL) {
		ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
		retval = -ENOMEM;
                goto out;
	} else {
		file->private_data = ( void * ) pbuf;
	}

	pbuf->buf = ZFCP_KMALLOC(ZFCP_MAX_PROC_SIZE, GFP_KERNEL);
	if (pbuf->buf == NULL) {
		ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
		ZFCP_KFREE(pbuf, sizeof(*pbuf));
		retval = -ENOMEM;
                goto out;
	}

	ZFCP_LOG_TRACE("Memory for proc parm output allocated.\n");

	MOD_INC_USE_COUNT;

	len += sprintf(pbuf->buf+len,"\n");

	len += sprintf(pbuf->buf+len,"Module Information: \n");

	len += sprintf(pbuf->buf+len,"\n");

	len += sprintf(pbuf->buf+len,"Module Version %s running in mode: ",
                                ZFCP_REVISION);

	len += sprintf(pbuf->buf+len,"FULL FEATURED\n");

	len += sprintf(pbuf->buf+len,"Debug proc output enabled: %s\n",
                                     proc_debug ? "  YES" : "   NO");

	len += sprintf(pbuf->buf+len,"\n");

	len += sprintf(pbuf->buf+len,
                                "Full log-level is:    0x%08x which means:\n",
                                atomic_read(&zfcp_data.loglevel));

        len += sprintf(pbuf->buf+len,
                       "ERP log-level:                 %01x\n",
                       (atomic_read(&zfcp_data.loglevel) >> 6*4) & 0xf);
        len += sprintf(pbuf->buf+len,
                                "QDIO log-level:                %01x     "
                                "Dynamic IO log-level:               %01x\n",
                                (atomic_read(&zfcp_data.loglevel) >> 5*4) & 0xf,
                                (atomic_read(&zfcp_data.loglevel) >> 4*4) & 0xf);
        len += sprintf(pbuf->buf+len,
                                "Configuration log-level:       %01x     "
                                "FSF log-level:                      %01x\n",
                                (atomic_read(&zfcp_data.loglevel) >> 3*4) & 0xf,
                                (atomic_read(&zfcp_data.loglevel) >> 2*4) & 0xf);
        len += sprintf(pbuf->buf+len,
                                "SCSI log-level:                %01x     "
                                "Other log-level:                    %01x\n",
                                (atomic_read(&zfcp_data.loglevel) >> 1*4) & 0xf,
                                atomic_read(&zfcp_data.loglevel) & 0xf);
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                       "Registered Adapters:       %5d\n",
                       zfcp_data.adapters);
        len += sprintf(pbuf->buf+len,"\n");

	if (proc_debug != 0) {
	        len += sprintf(pbuf->buf+len,
                                        "Data Structure information:\n");
	        len += sprintf(pbuf->buf+len,
                               "Data struct at:       0x%08lx\n",
                               (unsigned long) &zfcp_data);
	       	len += sprintf(pbuf->buf+len,"\n");

		len += sprintf(pbuf->buf+len,
                                        "Adapter list head at: 0x%08lx\n",
                                        (unsigned long) &(zfcp_data.adapter_list_head));
		len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) zfcp_data.adapter_list_head.next,
                                        (unsigned long) zfcp_data.adapter_list_head.prev);
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        zfcp_data.adapter_list_lock.lock,
                                        zfcp_data.adapter_list_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

		len += sprintf(pbuf->buf+len,
                                        "Total memory used(bytes): 0x%08x\n",
                                        atomic_read(&zfcp_data.mem_count));
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "DEVICE REGISTRATION INFO (devreg):\n");
                len += sprintf(pbuf->buf+len,
                                        "Control Unit Type:        0x%04x     "
                                        "Control Unit Mode:               0x%02x\n",
                                        zfcp_data.devreg.ci.hc.ctype,
                                        zfcp_data.devreg.ci.hc.cmode);
                len += sprintf(pbuf->buf+len,
                                        "Channel Status:           0x%04x     "
                                        "Device Status:                   0x%02x\n",
                                        zfcp_data.devreg.ci.hc.dtype,
                                        zfcp_data.devreg.ci.hc.dmode);
                len += sprintf(pbuf->buf+len,
                                        "Flags:                0x%08x\n",
                                        zfcp_data.devreg.flag);
                len += sprintf(pbuf->buf+len,"\n");
                len += sprintf(pbuf->buf+len,
                                        "PRIVILEGED DEVICE REGISTRATION INFO (devreg):\n");
                len += sprintf(pbuf->buf+len,
                                        "Control Unit Type:        0x%04x     "
                                        "Control Unit Model:              0x%02x\n",
                                        zfcp_data.devreg_priv.ci.hc.ctype,
                                        zfcp_data.devreg_priv.ci.hc.cmode);
                len += sprintf(pbuf->buf+len,
                                        "Device Type:              0x%04x     "
                                        "Device Model:                    0x%02x\n",
                                        zfcp_data.devreg_priv.ci.hc.dtype,
                                        zfcp_data.devreg_priv.ci.hc.dmode);
                len += sprintf(pbuf->buf+len,
                                        "Flags:                0x%08x\n",
                                        zfcp_data.devreg_priv.flag);
                len += sprintf(pbuf->buf+len,"\n");
	}// if (proc_debug != 0)

        pbuf->len = len;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:   zfcp_close_parm_proc
 *
 * purpose:    releases the memory allocated by zfcp_open_parm_proc
 *
 * retval:     0 in all cases
 *
 */
int zfcp_close_parm_proc(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int rc=0;
	procbuf_t *pbuf = (procbuf_t *) file->private_data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) file);

	if (pbuf) {
		if (pbuf->buf) {
                        ZFCP_LOG_TRACE("Freeing pbuf->buf\n");
			ZFCP_KFREE(pbuf->buf, ZFCP_MAX_PROC_SIZE);
                } else {
                        ZFCP_LOG_DEBUG("No procfile buffer found to be freed\n");
                }
                ZFCP_LOG_TRACE("Freeing pbuf\n");
		ZFCP_KFREE(pbuf, sizeof(*pbuf));
	} else {
                ZFCP_LOG_DEBUG("No procfile buffer found to be freed.\n");
        }

        ZFCP_LOG_TRACE("exit (%i)\n", rc);

        MOD_DEC_USE_COUNT;

        return rc;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/* 
 * function:   zfcp_open_add_map_proc
 *
 * purpose:    allocates memory for proc_line, intitalises count
 *
 * retval:     0 if successfull
 *             -ENOMEM if memory coud not be obtained
 *
 * locks:     grabs the zfcp_data.sema_map semaphore
 *            it is released upon exit of zfcp_close_add_map_proc
 */
int zfcp_open_add_map_proc(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;
        
        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) buffer);

	down(&zfcp_data.proc_sema);

	zfcp_data.proc_line = ZFCP_KMALLOC(ZFCP_MAX_PROC_LINE, GFP_KERNEL);
        if (zfcp_data.proc_line == NULL) {
		/* release semaphore on memory shortage */
		up(&zfcp_data.proc_sema);

                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                               " input. Input will be ignored.\n");

                retval = -ENOMEM;
                goto out;
        }

        /* This holds the length of the part acutally containing data, not the
           size of the buffer */
        zfcp_data.proc_line_length=0;
        
        MOD_INC_USE_COUNT;

        ZFCP_LOG_TRACE("proc_line buffer allocated...\n");
out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/* 
 * function:   zfcp_close_add_map_proc
 *
 * purpose:    parses any remaining string in proc_line, then
 *             releases memory for proc_line, then calls
 *             zfcp_adapter_scsi_register_all to tell the SCSI stack about
 *             possible new devices
 *
 * retval:     0 in all cases
 *
 * locks:      upon exit of zfcp_close_add_map_proc, releases the proc_sema
 */
int zfcp_close_add_map_proc(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) buffer);

	if (zfcp_data.proc_line == NULL)
		goto out;

        if (zfcp_data.proc_line_length > 0) {
                ZFCP_LOG_TRACE("Passing leftover line to parser\n");
                retval=zfcp_config_parse_record_list(
                             zfcp_data.proc_line,
                             zfcp_data.proc_line_length,
                             ZFCP_PARSE_ADD);
                if(retval<0) {
                        ZFCP_LOG_NORMAL("Warning: One or several mapping "
                                        "entries were not added to the "
                                        "module configuration.\n");
                }
        }
        ZFCP_KFREE(zfcp_data.proc_line, ZFCP_MAX_PROC_LINE);
        ZFCP_LOG_TRACE("proc_line buffer released...\n");
        zfcp_data.proc_line=NULL;
        zfcp_data.proc_line_length=0;

	zfcp_adapter_scsi_register_all();

	/* release semaphore */
	up(&zfcp_data.proc_sema);

        MOD_DEC_USE_COUNT;
out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/* 
 * function:   zfcp_create_root_proc
 *
 * purpose:    creates the main proc-directory for the zfcp driver
 *
 * retval:     0 if successfull
 *             -EPERM if the proc-directory could not be created
 *
 * locks:      the proc_sema is held on entry and throughout this function
 */
int zfcp_create_root_proc()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     int retval = 0;

     ZFCP_LOG_TRACE("enter\n");

     zfcp_data.proc_dir = proc_mkdir (ZFCP_NAME, proc_scsi);
     if (!zfcp_data.proc_dir) {
          ZFCP_LOG_INFO("error: Allocation of proc-fs directory %s for the  "
                        "zfcp-driver failed.\n",
                        ZFCP_NAME);
          retval = -EPERM;
     }
     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/* 
 * function:   zfcp_create_data_procs
 *
 * purpose:    creates the module-centric proc-entries
 *
 * retval:     0 if successfull
 *             -EPERM if the proc-entries could not be created
 *              (all entries are removed on exit)
 *
 * locks:      the proc_sema is held on entry and throughout this function
 */
int zfcp_create_data_procs()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
     int retval = 0;

     ZFCP_LOG_TRACE("enter\n");
     /* parm_file */
     zfcp_data.parm_proc_file=create_proc_entry(ZFCP_PARM_FILE, 
                                            S_IFREG|S_IRUGO|S_IWUSR,
                                            zfcp_data.proc_dir);
     if (!zfcp_data.parm_proc_file) {
             ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for module "
                             "configuration failed. Out of resources.\n",
                             ZFCP_PARM_FILE);
             retval = -EPERM;
             goto out;
     }
     zfcp_data.parm_proc_file->proc_fops=&zfcp_parm_fops;

     /* map file */
     zfcp_data.map_proc_file=create_proc_entry(ZFCP_MAP_FILE, 
                                            S_IFREG|S_IRUGO,
                                            zfcp_data.proc_dir);
     if (!zfcp_data.map_proc_file) {
             ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for module "
                             "configuration failed. Out of resources.\n",
                             ZFCP_MAP_FILE);
          retval = -EPERM;
          goto fail_map_proc_file;
     }
     zfcp_data.map_proc_file->proc_fops=&zfcp_map_fops;

     /* add_map file */
     zfcp_data.add_map_proc_file=create_proc_entry(ZFCP_ADD_MAP_FILE, 
                                            S_IFREG|S_IWUSR,
                                            zfcp_data.proc_dir);
     if (!zfcp_data.map_proc_file) {
             ZFCP_LOG_INFO("error: Allocation of proc-fs entry %s for module "
                             "configuration failed. Out of resources.\n",
                             ZFCP_ADD_MAP_FILE);
             retval = -EPERM;
             goto fail_add_map_proc_file;
     }
     zfcp_data.add_map_proc_file->proc_fops=&zfcp_add_map_fops;
     goto out;

 fail_add_map_proc_file:
     remove_proc_entry (ZFCP_MAP_FILE, zfcp_data.proc_dir);
 fail_map_proc_file:
     remove_proc_entry (ZFCP_PARM_FILE, zfcp_data.proc_dir);
     
 out:

     ZFCP_LOG_TRACE("exit (%i)\n", retval);
     return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/* 
 * function:   zfcp_delete_root_proc
 *
 * purpose:    deletes the main proc-directory for the zfcp driver
 *
 * retval:     0 in all cases
 */
int zfcp_delete_root_proc()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
        int retval = 0;
        
        ZFCP_LOG_TRACE("enter\n");
        
        remove_proc_entry (ZFCP_NAME, proc_scsi);
        
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
};



/* 
 * function:   zfcp_delete_data_proc
 *
 * purpose:    deletes the module-specific proc-entries for the zfcp driver
 *
 * retval:     0 in all cases
 */
int zfcp_delete_data_procs()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_CONFIG
        int retval = 0;
        
        ZFCP_LOG_TRACE("enter\n");
        
        remove_proc_entry (ZFCP_MAP_FILE, zfcp_data.proc_dir);
        remove_proc_entry (ZFCP_ADD_MAP_FILE, zfcp_data.proc_dir);
        remove_proc_entry (ZFCP_PARM_FILE, zfcp_data.proc_dir);
        
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
};



/*
 * function:	zfcp_parm_proc_read
 *
 * purpose:	Provides information about module settings as proc-output
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 */
ssize_t zfcp_parm_proc_read(struct file *file, 
                            char *user_buf, 
                            size_t user_len, 
                            loff_t *offset)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
        
	loff_t len;
	procbuf_t *pbuf = (procbuf_t *) file->private_data;
	loff_t pos = *offset;

	ZFCP_LOG_TRACE(
          "enter (file=0x%lx  user_buf=0x%lx "
          "user_length=%li *offset=0x%lx)\n",
          (unsigned long)file,
          (unsigned long)user_buf,
          user_len,
          (unsigned long)pos);

	if (pos < 0 || pos >= pbuf->len)
		return 0;

	len = min(user_len, (unsigned long)(pbuf->len - pos));
	if (copy_to_user(user_buf, &(pbuf->buf[pos]), len))
		return -EFAULT;

	*offset = pos + len;

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n",
                       ((unsigned long)(pbuf->len - pos)),
                       user_len);

        ZFCP_LOG_TRACE("exit (%Li)\n", len);
        
        return len;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:   zfcp_parm_proc_write
 *
 * purpose:    parses write requests to parm procfile
 *
 * returns:    number of characters passed into function
 *             -<error code> on failure
 *
 * known bugs: does not work when small buffers are used
 */

ssize_t zfcp_parm_proc_write(struct file *file,
			const char *user_buf,
			size_t user_len,
			loff_t *offset)

{

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	char *buffer, *tmp = NULL;
	char *buffer_start = NULL;
	char *pos;
	size_t my_count = user_len;
	u32 value;
	int retval = user_len;

	ZFCP_LOG_TRACE(
		"enter (file=0x%lx  user_buf=0x%lx "
		"user_length=%li *offset=0x%lx)\n",
		(unsigned long)file,
		(unsigned long)user_buf,
		user_len,
		(unsigned long)*offset);

	buffer = ZFCP_KMALLOC(my_count + 1, GFP_KERNEL);
	if (!buffer) {
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                                " input. Input will be ignored.\n");
		retval = -ENOMEM;
		goto out;
	}
	buffer_start=buffer;
	ZFCP_LOG_TRACE("buffer allocated...\n");

	copy_from_user(buffer, user_buf, my_count);

	buffer[my_count] = '\0';

	ZFCP_LOG_TRACE("user_len= %ld, strlen= %ld, buffer=%s<\n", 
		user_len, strlen("loglevel=0x00000000"), buffer);

	/* look for new loglevel */
	pos = strstr(buffer, "loglevel=");
	if (pos) {
		tmp = pos + strlen("loglevel=");
		value = simple_strtoul(tmp, &pos, 0);
		if (pos == tmp) {
			ZFCP_LOG_INFO(
				"warning: Log-level could not be changed, syntax faulty."
				"\nSyntax is loglevel=0xueqdcfso, see device driver "
				"documentation for details.\n");
			retval = -EFAULT;
		} else	{
			ZFCP_LOG_TRACE(
				"setting new loglevel (old is 0x%x, new is 0x%x)\n",
				atomic_read(&zfcp_data.loglevel), value);
			atomic_set(&zfcp_data.loglevel, value);
		}
	}

#ifdef ZFCP_LOW_MEM_CREDITS
	/* look for low mem trigger/credit */
	pos = strstr(buffer, "lowmem=");
	if (pos) {
		tmp = pos + strlen("lowmem=");
		value = simple_strtoul(tmp, &pos, 0);
		if (pos == tmp) {
			ZFCP_LOG_INFO("warning: lowmem credit faulty.");
			retval = -EFAULT;
		} else	{
			ZFCP_LOG_INFO("setting lowmem credit to %d\n", value);
			atomic_set(&zfcp_data.lowmem_credit, value);
		}
	}
#endif

	ZFCP_LOG_TRACE("freeing buffer..\n");
	ZFCP_KFREE(buffer_start, my_count + 1);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
/* 
 * function:   zfcp_open_proc_map
 *
 * purpose:    allocates memory for proc_buffer_map
 *
 * retval:     0 if successfull
 *             -ENOMEM if memory coud not be obtained
 *
 * locks:     grabs the zfcp_data.sema_map semaphore 
 *            it is released upon exit of zfcp_close_proc_map
 */
int zfcp_proc_map_open(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int retval = 0;

        ZFCP_LOG_TRACE(
                "enter (inode=0x%lx, buffer=0x%lx)\n",
                (unsigned long)inode,
                (unsigned long) buffer);

        /* block access */
        down(&zfcp_data.proc_sema);

        zfcp_data.proc_buffer_map = ZFCP_KMALLOC(
                                        ZFCP_MAX_PROC_SIZE,
                                        GFP_KERNEL);
        if (!zfcp_data.proc_buffer_map) {
                /* release semaphore on memory shortage */
                up(&zfcp_data.proc_sema);
                ZFCP_LOG_NORMAL(
                        "error: Not enough free memory for procfile"
                        " output. No output will be given.\n");
                retval = -ENOMEM;
        } else  MOD_INC_USE_COUNT;

        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:   zfcp_close_proc_map
 *
 * purpose:    releases memory for proc_buffer_map
 *
 * retval:     0 in all cases
 *
 * locks:      upon exit releases zfcp_close_proc_map
 */
int zfcp_proc_map_close(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;

        ZFCP_LOG_TRACE(
                "enter (inode=0x%lx, buffer=0x%lx)\n",
                (unsigned long)inode,
                (unsigned long) buffer);

        if (zfcp_data.proc_buffer_map) {
                ZFCP_LOG_TRACE("Freeing zfcp_data.proc_buffer_map.\n");
                ZFCP_KFREE(zfcp_data.proc_buffer_map, ZFCP_MAX_PROC_SIZE);
                up(&zfcp_data.proc_sema);
                MOD_DEC_USE_COUNT;
        }

        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_proc_map_read
 *
 * purpose:	Provides a list of all configured devices in identical format
 *		to expected configuration input as proc-output
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 *
 * locks:       proc_sema must be held on entry and throughout function
 */
ssize_t zfcp_proc_map_read(
		struct file *file, 
		char *user_buf, 
		size_t user_len, 
		loff_t *offset)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	static size_t item_size = 0;
	size_t real_len = 0;
	size_t print_len = 0;
	loff_t line_offset = 0;
	u64 current_unit = 0;
	zfcp_unit_t *unit;
	int i = 0;
	loff_t pos = *offset;

	ZFCP_LOG_TRACE(
		"enter (file=0x%lx  user_buf=0x%lx "
		"user_length=%li, *offset=%Ld)\n",
		(unsigned long)file,
		(unsigned long)user_buf,
		user_len,
		pos);

	if (pos < 0)
		return 0;

	if (pos) {
		/*
		 * current_unit: unit that needs to be printed (might be remainder)
		 * line_offset: bytes of current_unit that have already been printed
		 */
		current_unit = pos;
		line_offset = do_div(current_unit, item_size);
		ZFCP_LOG_TRACE(
			"item_size %ld, current_unit %Ld, line_offset %Ld\n",
			item_size,
			(llui_t)current_unit,
			line_offset);
	}

	list_for_each_entry(unit, &zfcp_data.map_list_head, map_list) {
		/* skip all units that have already been completely printed */
		if (i < current_unit) {
			i++;
			continue;
		}
		/* a unit to be printed (at least partially) */
		ZFCP_LOG_TRACE("unit=0x%lx\n", (unsigned long)unit);
		/* assumption: item_size <= ZFCP_MAX_PROC_SIZE */
		item_size = sprintf(
				&zfcp_data.proc_buffer_map[real_len],
				"0x%04x 0x%08x:0x%016Lx 0x%08x:0x%016Lx\n",
				unit->port->adapter->devno,
				unit->port->scsi_id,
				(llui_t)(unit->port->wwpn),
				unit->scsi_lun,
				(llui_t)(unit->fcp_lun));
		/* re-calculate used bytes in kernel buffer */
		real_len += item_size;
		/* re-calculate bytes to be printed */
		print_len = real_len - line_offset;
		/* stop if there is not enough user buffer space left */
		if (print_len > user_len) {
			/* adjust number of bytes to be printed */
			print_len = user_len;
			break;
		}
		/* stop if there is not enough kernel buffer space left */
		if (real_len + item_size > ZFCP_MAX_PROC_SIZE)
			break;
	}

	/* print if there is something in buffer */
	if (print_len) {
		ZFCP_LOG_TRACE(
			"Trying to do output (line_offset=%Ld, print_len=%ld, "
			"real_len=%ld, user_len=%ld).\n",
			line_offset, print_len, real_len, user_len);
		if (copy_to_user(
				user_buf,
				&zfcp_data.proc_buffer_map[line_offset],
				print_len)) {
			ZFCP_LOG_NORMAL(
				"bug: Copying proc-file output to user space "
				"failed (debug info %ld)",
				print_len);
			print_len = -EFAULT;
		} else	/* re-calculate offset in proc-output for next call */
			*offset = pos + print_len;
	}

	ZFCP_LOG_TRACE("exit (%li)\n", print_len);

	return print_len;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/* why is such a function not provided by the kernel? */
static size_t strnspn(const char *string, const char *chars, size_t limit)
{
	size_t pos = 0;
	const char *s = string, *c;

	while ((*s != '\0') && (pos < limit)) {
		c = chars;
		do {
			if (*c == '\0')
				goto out;
		} while (*c++ != *s);
		s++;
		pos++;
	}

out:
	return pos;
}


/* why is such a function not provided by the kernel? */
char* strnchr(const char *string, int character, size_t count)
{
	char *s = (char*) string;

	for (;; s++, count--) {
		if (!count)
			return NULL;
		if (*s == character)
			return s;
		if (*s == '\0')
			return NULL;
	}
}


/* why is such a function not provided by the kernel? */
char* strnpbrk(const char *string, const char *chars, size_t count)
{
	char *s = (char*) string;

	for (;; s++, count--) {
		if (!count)
			return NULL;
		if (strnspn(s, chars, 1))
			return s;
		if (*s == '\0')
			return NULL;
	}
}


/*
 * function:	zfcp_find_forward
 *
 * purpose:	Scans buffer for '\n' to a max length of *buffer_length
 *		buffer is incremented to after the first occurance of
 *              '\n' and *buffer_length decremented to reflect the new
 *              buffer length.
 *              fragment is a pointer to the original buffer start address
 *              and contains the initial fragment string of length 
 *              *fragment_length
 *
 * returns:     0 if found
 *              -1 otherwise
 */
unsigned long zfcp_find_forward(char **buffer, 
                      unsigned long *buffer_length,
                      char **fragment,
                      unsigned long *fragment_length)
{
                        
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
        
        unsigned long retval=0;
        
	ZFCP_LOG_TRACE(
                       "enter (*buffer=0x%lx, *buffer_length=%ld, "
                       "*fragment=0x%lx, *fragment_length=%ld)\n",
                       (unsigned long)*buffer,
                       *buffer_length,
                       (unsigned long)*fragment,
                       *fragment_length);

        *fragment = *buffer;
        for(;*buffer < (*fragment + *buffer_length);){
                if (**buffer=='\n') break;
                (*buffer)++;
        }
        if(*buffer >= (*fragment + *buffer_length)){
                *fragment_length = *buffer_length;
                *buffer_length = 0;
                retval = -1;
                goto out;
        }
        (*buffer)++;
        *fragment_length = *buffer - *fragment;
        *buffer_length -= *fragment_length;

 out:
        ZFCP_LOG_TRACE("exit (%li)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:	zfcp_find_backward
 *
 * purpose:	Scans buffer for '\n' backwards to a max length of 
 *              *buffer_length. Buffer is left unchanged, but 
 *              *buffer_length is decremented to reflect the new
 *              buffer length.
 *              rest points to the part of the string past the last
 *              occurrence of '\n' in the original buffer contents
 *              rest_length is the length of this part
 *
 * returns:     0 if found
 *              -1 otherwise
 */
unsigned long zfcp_find_backward(char **buffer, 
                      unsigned long *buffer_length,
                      char **rest,
                      unsigned long *rest_length)
{
                        
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
        
        unsigned long retval=0;
        
	ZFCP_LOG_TRACE(
                       "enter (*buffer=0x%lx, *buffer_length=%ld, "
                       "*rest=0x%lx, *rest_length=%ld)\n",
                       (unsigned long)*buffer,
                       *buffer_length,
                       (unsigned long)*rest,
                       *rest_length);

        *rest = *buffer + *buffer_length - 1;
        /*
          n    n+1    n+2    n+3    n+4    n+5     n+6   n+7    n+8
          ^                                               ^     ^(*buffer+*buffer_length)
          *buffer                                 *rest (buffer end)        
        */
        for(;*rest!=*buffer;){
                if (**rest=='\n') break;
                (*rest)--;
        }
        if (*rest <= *buffer) {
                *rest_length = *buffer_length;
                *buffer_length = 0;
                retval = -1;
                goto out;
        }
        (*rest)++;
        /*
          n    n+1    n+2    n+3     n+4    n+5    n+6   n+7   n+8
          ^                           ^     ^             ^    ^(*buffer+*buffer_length)
          *buffer                     '\n'  *rest      (buffer end)        
        */
        *rest_length = (*buffer + *buffer_length) - *rest;
        *buffer_length -= *rest_length;

 out:
        ZFCP_LOG_TRACE("*rest= 0x%lx\n",
                       (unsigned long)*rest);

        ZFCP_LOG_TRACE("exit (%li)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_add_map_proc_write
 *
 * purpose:	Breaks down the input map entries in user_buf into lines
 *              to be parsed by zfcp_config_parse_record_list.
 *              Also takes care of recombinations, multiple calls, etc.
 *
 * returns:     user_len as passed in
 *
 * locks:       proc_sema must be held on entry and throughout function
 */
ssize_t zfcp_add_map_proc_write(struct file *file, 
                             const char *user_buf, 
                             size_t user_len, 
                             loff_t *offset)
{

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        char *buffer = NULL;
        char *buffer_start = NULL; /* buffer is modified, this isn't (see kfree) */
        char *frag = NULL;
        size_t frag_length = 0;
        size_t my_count = user_len;
        int temp_ret = 0;

	ZFCP_LOG_TRACE(
		"enter (file=0x%lx  user_buf=0x%lx "
		"user_length=%li *offset=0x%lx)\n",
		(unsigned long)file,
		(unsigned long)user_buf,
		user_len,
		(unsigned long)*offset);


        buffer = ZFCP_KMALLOC(my_count, GFP_KERNEL);
	if (!buffer) {
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                                " input. Input will be ignored.\n");
		user_len = -ENOMEM;
		goto out;
	}
        buffer_start=buffer;
	ZFCP_LOG_TRACE("buffer allocated...\n");
     
	copy_from_user(buffer, user_buf, my_count);

	if (zfcp_data.proc_line_length > 0) {
		ZFCP_LOG_TRACE(
			"Remnants were present...(%ld)\n",
			zfcp_data.proc_line_length);
		temp_ret = zfcp_find_forward(
				&buffer, &my_count, &frag, &frag_length);
		ZFCP_LOG_TRACE(
			"fragment = 0x%lx, length= %ld\n",
			(unsigned long) frag,
			frag_length);

		if ((zfcp_data.proc_line_length + frag_length) >
		    (ZFCP_MAX_PROC_LINE - 1)) {
			ZFCP_LOG_INFO(
				"Maximum line length exceeded while parsing (%ld)\n",
				zfcp_data.proc_line_length + frag_length);
			zfcp_data.proc_line_length = 0;
                        user_len= -EINVAL;
			goto free_buffer;
		} 

		if (frag_length > 0) {
			memcpy(	zfcp_data.proc_line + zfcp_data.proc_line_length,
				frag, 
				frag_length);
			zfcp_data.proc_line_length += frag_length;
		}

		if(temp_ret) {
			ZFCP_LOG_TRACE("\"\\n\" was not found \n");
			goto free_buffer;
		}

		ZFCP_LOG_TRACE(
			"my_count= %ld, buffer=0x%lx text: \"%s\"\n",
			my_count,
			(unsigned long) buffer,
			buffer);

		/* process line combined from several buffers */
                if (zfcp_config_parse_record_list(
			zfcp_data.proc_line,
			zfcp_data.proc_line_length,
                        ZFCP_PARSE_ADD) < 0) {
                        user_len=-EINVAL;
                        /* Do not try another parse in close_proc */
                        zfcp_data.proc_line_length = 0;
                        ZFCP_LOG_NORMAL("Warning: One or several mapping "
                                        "entries were not added to the "
                                        "module configuration.\n");
                }
		zfcp_data.proc_line_length = 0;
	}// if(zfcp_data.proc_line_length > 0)

	temp_ret = zfcp_find_backward(&buffer, &my_count, &frag, &frag_length);
	ZFCP_LOG_TRACE(
		"fragment length = %ld\n",
		frag_length);
	if (frag_length > (ZFCP_MAX_PROC_LINE - 1)) {
                ZFCP_LOG_NORMAL(
                       "warning: Maximum line length exceeded while parsing "
                       "input. Length is already %ld. Some part of the input "
                       "will be ignored.\n",
			frag_length);
		zfcp_data.proc_line_length = 0;
                user_len = -EINVAL;
		goto free_buffer;
	}

	if (frag_length > 0) {
		memcpy(zfcp_data.proc_line, frag, frag_length);
		zfcp_data.proc_line_length += frag_length;
	}

	if (temp_ret) {
		ZFCP_LOG_TRACE("\"\\n\" was not found \n");
		goto free_buffer;
	}

	ZFCP_LOG_TRACE(
		"my_count= %ld, buffer=0x%lx text: \"%s\"\n",
		my_count,
		(unsigned long) buffer,
		buffer);
	if (zfcp_config_parse_record_list(
                   buffer,
                   my_count,
                   ZFCP_PARSE_ADD) < 0) {
                user_len=-EINVAL;
                /* Do not try another parse in close_proc */
                zfcp_data.proc_line_length = 0;
                ZFCP_LOG_NORMAL("Warning: One or several mapping "
                                "entries were not added to the "
                                "module configuration.\n");
        }
free_buffer:
	ZFCP_LOG_TRACE("freeing buffer..\n");
	ZFCP_KFREE(buffer_start, my_count + 1);
out:
	ZFCP_LOG_TRACE("exit (%li)\n", user_len);
	return (user_len);
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * zfcp_adapter_proc_open
 *
 * modified proc fs utilization (instead of using ..._generic):
 *
 *  - to avoid (SMP) races, allocate buffers for output using
 *    the private_data member in the respective file struct
 *    such that read() just has to copy out of this buffer
 *
 */

int zfcp_adapter_proc_open(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int len = 0;
        procbuf_t *pbuf;
        int retval=0;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
	zfcp_adapter_t *adapter = dp->data;
	int i;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, file=0x%lx)\n",
                (unsigned long)inode,
                (unsigned long) file);

#if 0
        /* DEBUG: force an abort which is being hung than, usage of mod_parm dismisses pending fsf_req */
        ZFCP_LOG_NORMAL("try to recover forced and hung abort\n");
        zfcp_erp_adapter_reopen(ZFCP_FIRST_ADAPTER, 0);
#endif

        pbuf = ZFCP_KMALLOC(sizeof(procbuf_t), GFP_KERNEL);
        if (pbuf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                retval = -ENOMEM;
                goto out;
        } else {
                file->private_data = ( void * ) pbuf;
        }

        pbuf->buf = ZFCP_KMALLOC(ZFCP_MAX_PROC_SIZE, GFP_KERNEL);
        if (pbuf->buf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
                retval = -ENOMEM;
                goto out;
        }

        ZFCP_LOG_TRACE("Memory for adapter proc output allocated.\n");

        MOD_INC_USE_COUNT;

        len += sprintf(pbuf->buf+len,
                                "\nFCP adapter\n\n");

        len += sprintf(pbuf->buf+len,
                                "FCP driver %s "
                                "(or for cryptography's sake 0x%08x)\n\n",
                                ZFCP_REVISION,
                                zfcp_data.driver_version);

        len += sprintf(pbuf->buf+len,
				"device number:            0x%04x     "
                                "registered on irq:             0x%04x\n",
                                adapter->devno,
                                adapter->irq);
        len += sprintf(pbuf->buf+len,
                                "WWNN:         0x%016Lx\n",
                                (llui_t)adapter->wwnn);
        len += sprintf(pbuf->buf+len,
				"WWPN:         0x%016Lx     "
				"S_ID:                        0x%06x\n",
                                (llui_t)adapter->wwpn,
				adapter->s_id);
        len += sprintf(pbuf->buf+len,
                                "HW version:               0x%04x     "
                                "LIC version:               0x%08x\n",
                                adapter->hydra_version,
                                adapter->fsf_lic_version);
        len += sprintf(pbuf->buf+len,
                                "FC link speed:            %d Gb/s     "
                                "FC service class:                   %d\n",
				adapter->fc_link_speed,
                                adapter->fc_service_class);
        len += sprintf(pbuf->buf+len,
                       "Hardware Version:     0x%08x\n"
                       "Serial Number: %17s\n",
                       adapter->hardware_version,
                       adapter->serial_number);
        len += sprintf(pbuf->buf+len,
                                "FC topology:   %s\n",
                                zfcp_topologies[adapter->fc_topology]);
#if 0
	if (adapter->fc_topology == FSF_TOPO_P2P)
        len += sprintf(pbuf->buf+len,
				"D_ID of peer:         0x%06x\n",
				adapter->peer_d_id);
#endif
        len += sprintf(pbuf->buf+len,
                                "SCSI host number:     0x%08x\n",
                                adapter->scsi_host->host_no);
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "Attached ports:       %10d     "
                                "QTCB size (bytes):         %10ld\n",
                                adapter->ports,
                                sizeof(fsf_qtcb_t));
        len += sprintf(pbuf->buf+len,
                                "Max SCSI ID of ports: 0x%08x     "
                                "Max SCSI LUN of ports:     0x%08x\n",
                                adapter->max_scsi_id,
                                adapter->max_scsi_lun);
        len += sprintf(pbuf->buf+len,
                                "FSF req seq. no:      0x%08x     "
                                "FSF reqs active:           %10d\n",
                                adapter->fsf_req_seq_no,
                                atomic_read(&adapter->fsf_reqs_active));
        len += sprintf(pbuf->buf+len,
                                "Scatter-gather table-size: %5d     "
                                "Max no of queued commands: %10d\n",
                                zfcp_data.scsi_host_template.sg_tablesize,
                                zfcp_data.scsi_host_template.can_queue);
        len += sprintf(pbuf->buf+len,
                                "Uses clustering:               %1d     "
                                "Uses New Error-Handling Code:       %1d\n",
                                zfcp_data.scsi_host_template.use_clustering,
                                zfcp_data.scsi_host_template.use_new_eh_code);
        len += sprintf(pbuf->buf+len,
                                "ERP counter:          0x%08x     ",
                                atomic_read(&adapter->erp_counter));
        len += sprintf(pbuf->buf+len,
                                "Adapter Status:            0x%08x\n",
                                atomic_read(&adapter->status));
	len += sprintf(pbuf->buf+len,
				"SCSI commands delayed: %10d\n",
				atomic_read(&adapter->fake_scsi_reqs_active));
        len += sprintf(pbuf->buf+len,"\n");

        if (proc_debug != 0) {
		len += sprintf(pbuf->buf+len,
                                        "Adapter Structure information:\n");
                len += sprintf(pbuf->buf+len,
                                        "Common Magic:         0x%08x     "
                                        "Specific Magic:            0x%08x\n",
                                        adapter->common_magic,
                                        adapter->specific_magic);
                len += sprintf(pbuf->buf+len,
                                        "Adapter struct at:    0x%08lx     "
                                        "List head at:              0x%08lx\n",
                                        (unsigned long) adapter,
                                        (unsigned long) &(adapter->list));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) adapter->list.next,
                                        (unsigned long) adapter->list.prev);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Scsi_Host struct at:  0x%08lx\n",
                                        (unsigned long) adapter->scsi_host);
                len += sprintf(pbuf->buf+len,
                                        "Port list head at:    0x%08lx\n",
                                        (unsigned long) &(adapter->port_list_head));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) adapter->port_list_head.next,
                                        (unsigned long) adapter->port_list_head.prev);
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        adapter->port_list_lock.lock,
                                        adapter->port_list_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "O-FCP req list head:  0x%08lx\n",
                                        (unsigned long) &(adapter->fsf_req_list_head));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) adapter->fsf_req_list_head.next,
                                        (unsigned long) adapter->fsf_req_list_head.prev);
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        adapter->fsf_req_list_lock.lock,
                                        adapter->fsf_req_list_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Request queue at:     0x%08lx\n",
                                        (unsigned long)&(adapter->request_queue));
                len += sprintf(pbuf->buf+len,
                                        "Free index:                  %03d     "
                                        "Free count:                       %03d\n",
                                        adapter->request_queue.free_index,
                                        atomic_read(&adapter->request_queue.free_count));
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        adapter->request_queue.queue_lock.lock,
                                        adapter->request_queue.queue_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Response queue at:    0x%08lx\n",
                                        (unsigned long)&(adapter->response_queue));
                len += sprintf(pbuf->buf+len,
                                        "Free index:                  %03d     "
                                        "Free count:                       %03d\n",
                                        adapter->response_queue.free_index,
                                        atomic_read(&adapter->response_queue.free_count));
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        adapter->response_queue.queue_lock.lock,
                                        adapter->response_queue.queue_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,"DEVICE INFORMATION (devinfo):\n");
                len += sprintf(pbuf->buf+len,"Status: ");
                switch(adapter->devinfo.status) {
                        case 0:
                                len += sprintf(pbuf->buf+len,
                                                "\"OK\"\n");
                        break;
                        case DEVSTAT_NOT_OPER:
                                len += sprintf(pbuf->buf+len,
                                                "\"DEVSTAT_NOT_OPER\"\n");
                                break;
                        case DEVSTAT_DEVICE_OWNED:
                                len += sprintf(pbuf->buf+len,
                                                "\"DEVSTAT_DEVICE_OWNED\"\n");
                                break;
                        case DEVSTAT_UNKNOWN_DEV:
                                len += sprintf(pbuf->buf+len,
                                                "\"DEVSTAT_UNKNOWN_DEV\"\n");
                                break;
                        default:
                                len += sprintf(pbuf->buf+len,
                                                "UNSPECIFIED STATE (value is 0x%x)\n",
                                        adapter->devinfo.status);
                                break;
                }
                len += sprintf(pbuf->buf+len,
                                        "Control Unit Type:        0x%04x     "
                                        "Control Unit Model:              0x%02x\n",
                                        adapter->devinfo.sid_data.cu_type,
                                        adapter->devinfo.sid_data.cu_model);
                len += sprintf(pbuf->buf+len,
                                        "Device Type:              0x%04x     "
                                        "Device Model:                    0x%02x\n",
                                        adapter->devinfo.sid_data.dev_type,
                                        adapter->devinfo.sid_data.dev_model);
                len += sprintf(pbuf->buf+len,
                                        "CIWs:       ");
                for(i=0;i<4;i++){
                                len += sprintf(pbuf->buf+len,
                                                "0x%08x ",
                                                *(unsigned int *)(&adapter->devinfo.sid_data.ciw[i]));
                }
                len += sprintf(pbuf->buf+len,"\n            ");
                for(i=4;i<8;i++){
                                len += sprintf(pbuf->buf+len,
                                                "0x%08x ",
                                                *(unsigned int *)(&adapter->devinfo.sid_data.ciw[i]));
                }
                len += sprintf(pbuf->buf+len,"\n");
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,"DEVICE INFORMATION (devstat):\n");
                len += sprintf(pbuf->buf+len,
                                        "Interrupt Parameter:  0x%08lx     "
                                        "Last path used mask:             0x%02x\n",
                                        adapter->devstat.intparm,
                                        adapter->devstat.lpum);
                len += sprintf(pbuf->buf+len,
                                        "Channel Status:             0x%02x     "
                                        "Device Status:                   0x%02x\n",
                                        adapter->devstat.cstat,
                                        adapter->devstat.dstat);
		len += sprintf(pbuf->buf+len,
                                        "Flag:                 0x%08x     "
                                        "CCW address (from irb):    0x%08lx\n",
                                        adapter->devstat.flag,
                                        (unsigned long)adapter->devstat.cpa);
                len += sprintf(pbuf->buf+len,
                                        "Response count:       0x%08x     "
                                        "Sense Count:               0x%08x\n",
                                        adapter->devstat.rescnt,
                                        adapter->devstat.scnt);
                len += sprintf(pbuf->buf+len,
                                "IRB:        ");
                for(i=0;i<4;i++){
                                len += sprintf(pbuf->buf+len,
                                                "0x%08x ",
                                                *((unsigned int *)(&adapter->devstat.ii.irb)+i));
                }
                len += sprintf(pbuf->buf+len,"\n");
                len += sprintf(pbuf->buf+len,
                                        "Sense Data: ");
                for(i=0;i<4;i++){
                                len += sprintf(pbuf->buf+len,
                                                "0x%08x ",
                                                *((unsigned int *)(&adapter->devstat.ii.sense.data)+i));
                }
                        len += sprintf(pbuf->buf+len,"\n");
        }

#ifdef ZFCP_STAT_QUEUES
        len += sprintf(pbuf->buf + len, "\nOutbound queue full:  0x%08x     ",
                       atomic_read(&adapter->outbound_queue_full));
	len += sprintf(pbuf->buf + len, "Outbound requests:    0x%08x\n\n",
			atomic_read(&adapter->outbound_total));
#endif
#ifdef ZFCP_STAT_REQSIZES
	len += sprintf(pbuf->buf + len, "missed stats 0x%x\n",
			atomic_read(&adapter->stat_errors));
	len = zfcp_statistics_print(
			adapter, &adapter->read_req_head,
			"rr", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(
			adapter, &adapter->write_req_head,
			"wr", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
#endif

	ZFCP_LOG_TRACE("stored %d bytes in proc buffer\n", len);

        pbuf->len = len;

out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

int zfcp_adapter_proc_close(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int rc=0;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) file);

        if (pbuf) {
                if (pbuf->buf) {
                        ZFCP_LOG_TRACE("Freeing pbuf->buf\n");
                        ZFCP_KFREE(pbuf->buf, ZFCP_MAX_PROC_SIZE);
                } else {
                        ZFCP_LOG_DEBUG("No procfile buffer found to be freed\n");
                }
                ZFCP_LOG_TRACE("Freeing pbuf\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
        } else {
                ZFCP_LOG_DEBUG("No procfile buffer found to be freed.\n");
        }

        ZFCP_LOG_TRACE("exit (%i)\n", rc);

        MOD_DEC_USE_COUNT;

        return rc;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:    zfcp_adapter_proc_read
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 */
ssize_t zfcp_adapter_proc_read(struct file *file,
                            char *user_buf,
                            size_t user_len,
                            loff_t *offset)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        loff_t len;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;
        loff_t pos = *offset;

        ZFCP_LOG_TRACE(
          "enter (file=0x%lx  user_buf=0x%lx "
          "user_length=%li *offset=0x%lx)\n",
          (unsigned long)file,
          (unsigned long)user_buf,
          user_len,
          (unsigned long)pos);

        if (pos < 0 || pos >= pbuf->len)
                return 0;

        len = min(user_len, (unsigned long)(pbuf->len - pos));
        if (copy_to_user(user_buf, &(pbuf->buf[pos]), len))
                return -EFAULT;

        *offset = pos + len;

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n",
                       ((unsigned long)(pbuf->len - pos)),
                       user_len);

        ZFCP_LOG_TRACE("exit (%Li)\n", len);

        return len;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function: zfcp_adapter_proc_write
 *
 * known bugs: does not work when small buffers are used
 *
 */

ssize_t zfcp_adapter_proc_write(struct file *file,
                        const char *user_buf,
                        size_t user_len,
                        loff_t *offset)

{

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        char *buffer = NULL;
        size_t my_count = user_len;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
	zfcp_adapter_t *adapter = dp->data;

        ZFCP_LOG_TRACE(
                "enter (file=0x%lx  user_buf=0x%lx "
                "user_length=%li *offset=0x%lx)\n",
                (unsigned long)file,
                (unsigned long)user_buf,
                user_len,
                (unsigned long)*offset);

        buffer = ZFCP_KMALLOC(my_count + 1, GFP_KERNEL);
        if (!buffer) {
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                                " input. Input will be ignored.\n");
                user_len = -ENOMEM;
                goto out;
        }
        ZFCP_LOG_TRACE("buffer allocated...\n");

        copy_from_user(buffer, user_buf, my_count);

        buffer[my_count] = '\0'; /* for debugging */

        ZFCP_LOG_TRACE("user_len= %ld, buffer=>%s<\n",
                user_len, buffer);

	if ((strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) ||
	    (strncmp(ZFCP_SET_ONLINE, buffer, strlen(ZFCP_SET_ONLINE)) == 0)) {
		ZFCP_LOG_NORMAL(
			"user triggered (re)start of all operations on the "
			"adapter with devno 0x%04x\n",
			adapter->devno);
		zfcp_erp_modify_adapter_status(
			adapter,
			ZFCP_STATUS_COMMON_RUNNING,
			ZFCP_SET);
		zfcp_erp_adapter_reopen(
			adapter,
			ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_erp_wait(adapter);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_SET_OFFLINE, buffer, strlen(ZFCP_SET_OFFLINE)) == 0) {
                ZFCP_LOG_NORMAL(
			"user triggered shutdown of all operations on the "
			"adapter with devno 0x%04x\n",
			adapter->devno);
		zfcp_erp_adapter_shutdown(adapter, 0);
		zfcp_erp_wait(adapter);
		user_len = strlen(buffer);
	} else	if (strncmp(ZFCP_STAT_RESET, buffer, strlen(ZFCP_STAT_RESET)) == 0) {
#ifdef ZFCP_STAT_REQSIZES
		ZFCP_LOG_NORMAL(
			"user triggered reset of all statisticss for the "
			"adapter with devno 0x%04x\n",
			adapter->devno);
		atomic_compare_and_swap(1, 0, &adapter->stat_on);
		zfcp_statistics_clear(adapter, &adapter->read_req_head);
		zfcp_statistics_clear(adapter, &adapter->write_req_head);
		atomic_set(&adapter->stat_errors, 0);
		atomic_compare_and_swap(0, 1, &adapter->stat_on);
#endif
		user_len = strlen(buffer);
	} else	if (strncmp(ZFCP_STAT_OFF, buffer, strlen(ZFCP_STAT_OFF)) == 0) {
#ifdef ZFCP_STAT_REQSIZES
		if (atomic_compare_and_swap(1, 0, &adapter->stat_on)) {
			ZFCP_LOG_NORMAL(
				"warning: all statistics for the adapter "
				"with devno 0x%04x already off\n ",
			adapter->devno);
		} else	{
			ZFCP_LOG_NORMAL(
				"user triggered shutdown of all statistics for the "
				"adapter with devno 0x%04x\n",
			adapter->devno);
			zfcp_statistics_clear(adapter, &adapter->read_req_head);
			zfcp_statistics_clear(adapter, &adapter->write_req_head);
		}
#endif
		user_len = strlen(buffer);
	} else	if (strncmp(ZFCP_STAT_ON, buffer, strlen(ZFCP_STAT_ON)) == 0) {
#ifdef ZFCP_STAT_REQSIZES
		if (atomic_compare_and_swap(0, 1, &adapter->stat_on)) {
			ZFCP_LOG_NORMAL(
				"warning: all statistics for the adapter "
				"with devno 0x%04x already on\n ",
			adapter->devno);
		} else	{
			ZFCP_LOG_NORMAL(
				"user triggered (re)start of all statistics for the "
				"adapter with devno 0x%04x\n",
			adapter->devno);
		}
#endif
		user_len = strlen(buffer);
	} else	{
		ZFCP_LOG_INFO("error: unknown procfs command\n");
		user_len = -EINVAL;
	}

        ZFCP_LOG_TRACE("freeing buffer..\n");
        ZFCP_KFREE(buffer, my_count + 1);
out:
        ZFCP_LOG_TRACE("exit (%li)\n", user_len);
        return (user_len);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

int zfcp_port_proc_close(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int rc=0;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) file);

        if (pbuf) {
                if (pbuf->buf) {
                        ZFCP_LOG_TRACE("Freeing pbuf->buf\n");
                        ZFCP_KFREE(pbuf->buf, ZFCP_MAX_PROC_SIZE);
                } else {
                        ZFCP_LOG_DEBUG("No procfile buffer found to be freed\n");
                }
                ZFCP_LOG_TRACE("Freeing pbuf\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
        } else {
                ZFCP_LOG_DEBUG("No procfile buffer found to be freed.\n");
        }

        ZFCP_LOG_TRACE("exit (%i)\n", rc);

        MOD_DEC_USE_COUNT;

        return rc;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:    zfcp_port_proc_read
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 */
ssize_t zfcp_port_proc_read(struct file *file,
                            char *user_buf,
                            size_t user_len,
                            loff_t *offset)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        loff_t len;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;
        loff_t pos = *offset;

        ZFCP_LOG_TRACE(
          "enter (file=0x%lx  user_buf=0x%lx "
          "user_length=%li *offset=0x%lx)\n",
          (unsigned long)file,
          (unsigned long)user_buf,
          user_len,
          (unsigned long)pos);

        if (pos < 0 || pos >= pbuf->len)
                return 0;

        len = min(user_len, (unsigned long)(pbuf->len - pos));
        if (copy_to_user(user_buf, &(pbuf->buf[pos]), len))
                return -EFAULT;

        *offset = pos + len;

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n",
                       ((unsigned long)(pbuf->len - pos)),
                       user_len);

        ZFCP_LOG_TRACE("exit (%Li)\n", len);

        return len;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function: zfcp_port_proc_write
 *
 * known bugs: does not work when small buffers are used
 *
 */

ssize_t zfcp_port_proc_write(struct file *file,
                        const char *user_buf,
                        size_t user_len,
                        loff_t *offset)

{

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        char *buffer = NULL;
        size_t my_count = user_len;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
        zfcp_port_t *port = dp->data;
	zfcp_adapter_t *adapter = port->adapter;

        ZFCP_LOG_TRACE(
                "enter (file=0x%lx  user_buf=0x%lx "
                "user_length=%li *offset=0x%lx)\n",
                (unsigned long)file,
                (unsigned long)user_buf,
                user_len,
                (unsigned long)*offset);

        buffer = ZFCP_KMALLOC(my_count + 1, GFP_KERNEL);
        if (!buffer) {
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                                " input. Input will be ignored.\n");
                user_len = -ENOMEM;
                goto out;
        }
        ZFCP_LOG_TRACE("buffer allocated...\n");

        copy_from_user(buffer, user_buf, my_count);

        buffer[my_count] = '\0'; /* for debugging */

        ZFCP_LOG_TRACE("user_len= %ld, buffer=>%s<\n",
                user_len, buffer);

	if ((strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) ||
	    (strncmp(ZFCP_SET_ONLINE, buffer, strlen(ZFCP_SET_ONLINE)) == 0)) {
		ZFCP_LOG_NORMAL(
			"user triggered (re)start of all operations on the "
			"port with WWPN 0x%016Lx on the adapter with devno "
			"0x%04x\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_erp_modify_port_status(
			port,
			ZFCP_STATUS_COMMON_RUNNING,
			ZFCP_SET);
		zfcp_erp_port_reopen(
			port,
			ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_erp_wait(adapter);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_SET_OFFLINE, buffer, strlen(ZFCP_SET_OFFLINE)) == 0) {
		ZFCP_LOG_NORMAL(
			"user triggered shutdown of all operations on the "
			"port with WWPN 0x%016Lx on the adapter with devno "
			"0x%04x\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_erp_port_shutdown(port, 0);
		zfcp_erp_wait(adapter);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_RTV, buffer, strlen(ZFCP_RTV)) == 0) {
		ZFCP_LOG_NORMAL(
			"Read timeout value (RTV) ELS "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_els(port, ZFCP_LS_RTV);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_RLS, buffer, strlen(ZFCP_RLS)) == 0) {
		ZFCP_LOG_NORMAL(
			"Read link status (RLS) ELS "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_els(port, ZFCP_LS_RLS);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_PDISC, buffer, strlen(ZFCP_PDISC)) == 0) {
		ZFCP_LOG_NORMAL(
			"Port discovery (PDISC) ELS "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_els(port, ZFCP_LS_PDISC);
		user_len = strlen(buffer);
	} else  if (strncmp(ZFCP_ADISC, buffer, strlen(ZFCP_ADISC)) == 0) {
		ZFCP_LOG_NORMAL(
			"Address discovery (ADISC) ELS "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(llui_t)port->wwpn,
			adapter->devno);
		zfcp_els(port, ZFCP_LS_ADISC);
	} else	{
		ZFCP_LOG_INFO("error: unknown procfs command\n");
		user_len = -EINVAL;
	}

        ZFCP_LOG_TRACE("freeing buffer..\n");
        ZFCP_KFREE(buffer, my_count + 1);
out:
        ZFCP_LOG_TRACE("exit (%li)\n", user_len);
        return (user_len);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * zfcp_port_proc_open
 *
 * modified proc fs utilization (instead of using ..._generic):
 *
 *  - to avoid (SMP) races, allocate buffers for output using
 *    the private_data member in the respective file struct
 *    such that read() just has to copy out of this buffer
 *
 */

int zfcp_port_proc_open(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int len = 0;
        procbuf_t *pbuf;
        int retval=0;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
        zfcp_port_t *port = dp->data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, file=0x%lx)\n",
                (unsigned long)inode,
                (unsigned long) file);

        pbuf = ZFCP_KMALLOC(sizeof(procbuf_t), GFP_KERNEL);
        if (pbuf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                retval = -ENOMEM;
                goto out;
        } else {
                file->private_data = ( void * ) pbuf;
        }

        pbuf->buf = ZFCP_KMALLOC(ZFCP_MAX_PROC_SIZE, GFP_KERNEL);
        if (pbuf->buf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
                retval = -ENOMEM;
                goto out;
        }

        ZFCP_LOG_TRACE("Memory for port proc output allocated.\n");

        MOD_INC_USE_COUNT;

        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                               "Port Information: \n");
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "WWNN:         0x%016Lx     "
                                "WWPN:              0x%016Lx\n",
                                (llui_t)port->wwnn,
                                (llui_t)port->wwpn);
        len += sprintf(pbuf->buf+len,
                                "SCSI ID:              0x%08x     "
                                "Max SCSI LUN:              0x%08x\n",
                                port->scsi_id,
                                port->max_scsi_lun);
        len += sprintf(pbuf->buf+len,
                                "D_ID:                   0x%06x\n",
                                port->d_id);
        len += sprintf(pbuf->buf+len,
                                "Handle:               0x%08x\n",
                                port->handle);
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "Attached units:       %10d\n",
                                port->units);
        len += sprintf(pbuf->buf+len,
                                "ERP counter:          0x%08x\n",
                                atomic_read(&port->erp_counter));
        len += sprintf(pbuf->buf+len,
                                "Port Status:          0x%08x\n",
                                atomic_read(&port->status));
        len += sprintf(pbuf->buf+len,"\n");

        if (proc_debug != 0) {
		len += sprintf(pbuf->buf+len,
                                        "Port Structure information:\n");
                len += sprintf(pbuf->buf+len,
                                        "Common Magic:         0x%08x     "
                                        "Specific Magic:            0x%08x\n",
                                        port->common_magic,
                                        port->specific_magic);
                len += sprintf(pbuf->buf+len,
                                        "Port struct at:       0x%08lx     "
                                        "List head at:              0x%08lx\n",
                                        (unsigned long) port,
                                        (unsigned long) &(port->list));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) port->list.next,
                                        (unsigned long) port->list.prev);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Unit list head at:    0x%08lx\n",
                                        (unsigned long) &(port->unit_list_head));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) port->unit_list_head.next,
                                        (unsigned long) port->unit_list_head.prev);
                len += sprintf(pbuf->buf+len,
                                        "List lock:            0x%08lx     "
                                        "List lock owner PC:        0x%08lx\n",
                                        port->unit_list_lock.lock,
                                        port->unit_list_lock.owner_pc);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Parent adapter at:    0x%08lx\n",
                                        (unsigned long) port->adapter);
        }

        ZFCP_LOG_TRACE("stored %d bytes in proc buffer\n", len);

        pbuf->len = len;

out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * zfcp_unit_proc_open
 *
 *  - to avoid (SMP) races, allocate buffers for output using
 *    the private_data member in the respective file struct
 *    such that read() just has to copy out of this buffer
 *
 */

int zfcp_unit_proc_open(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int len = 0;
        procbuf_t *pbuf;
        int retval=0;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
        zfcp_unit_t *unit = dp->data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, file=0x%lx)\n",
                (unsigned long)inode,
                (unsigned long) file);

        pbuf = ZFCP_KMALLOC(sizeof(procbuf_t), GFP_KERNEL);
        if (pbuf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                retval = -ENOMEM;
                goto out;
        } else {
                file->private_data = ( void * ) pbuf;
        }

        pbuf->buf = ZFCP_KMALLOC(ZFCP_MAX_PROC_SIZE, GFP_KERNEL);
        if (pbuf->buf == NULL) {
                ZFCP_LOG_NORMAL("error: Not enough memory available for "
                               "proc-fs action. Action will be ignored.\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
                retval = -ENOMEM;
                goto out;
        }

        ZFCP_LOG_TRACE("Memory for unit proc output allocated.\n");

        MOD_INC_USE_COUNT;

        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "Unit Information: \n");
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "SCSI LUN:             0x%08x     "
                                "FCP_LUN:           0x%016Lx\n",
                                unit->scsi_lun,
                                (llui_t)unit->fcp_lun);
        len += sprintf(pbuf->buf+len,
                                "Handle:               0x%08x\n",
                                unit->handle);
        len += sprintf(pbuf->buf+len,"\n");

        len += sprintf(pbuf->buf+len,
                                "ERP counter:          0x%08x\n",
                                atomic_read(&unit->erp_counter));
        len += sprintf(pbuf->buf+len,
                                "Unit Status:          0x%08x\n",
                                atomic_read(&unit->status));
        len += sprintf(pbuf->buf+len,"\n");

        if (proc_debug != 0) {
        	len += sprintf(pbuf->buf+len,
                                        "Unit Structure information:\n");
                len += sprintf(pbuf->buf+len,
                                        "Common Magic:         0x%08x     "
                                        "Specific Magic:            0x%08x\n",
                                        unit->common_magic,
                                        unit->specific_magic);
                len += sprintf(pbuf->buf+len,
                                        "Unit struct at:       0x%08lx     "
                                        "List head at:              0x%08lx\n",
                                        (unsigned long) unit,
                                        (unsigned long) &(unit->list));
                len += sprintf(pbuf->buf+len,
                                        "Next list head:       0x%08lx     "
                                        "Previous list head:        0x%08lx\n",
                                        (unsigned long) unit->list.next,
                                        (unsigned long) unit->list.prev);
                len += sprintf(pbuf->buf+len,"\n");

                len += sprintf(pbuf->buf+len,
                                        "Parent port at:       0x%08lx     "
                                        "SCSI dev struct at:        0x%08lx\n",
                                        (unsigned long) unit->port,
                                        (unsigned long) unit->device);
	}

        ZFCP_LOG_TRACE("stored %d bytes in proc buffer\n", len);

        pbuf->len = len;

out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

int zfcp_unit_proc_close(struct inode *inode, struct file *file)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int rc=0;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) file);

        if (pbuf) {
                if (pbuf->buf) {
                        ZFCP_LOG_TRACE("Freeing pbuf->buf\n");
                        ZFCP_KFREE(pbuf->buf, ZFCP_MAX_PROC_SIZE);
                } else {
                        ZFCP_LOG_DEBUG("No procfile buffer found to be freed\n");
                }
                ZFCP_LOG_TRACE("Freeing pbuf\n");
                ZFCP_KFREE(pbuf, sizeof(*pbuf));
        } else {
                ZFCP_LOG_DEBUG("No procfile buffer found to be freed.\n");
        }

        ZFCP_LOG_TRACE("exit (%i)\n", rc);

        MOD_DEC_USE_COUNT;

        return rc;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:    zfcp_unit_proc_read
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 */
ssize_t zfcp_unit_proc_read(struct file *file,
                            char *user_buf,
                            size_t user_len,
                            loff_t *offset)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        loff_t len;
        procbuf_t *pbuf = (procbuf_t *) file->private_data;
        loff_t pos = *offset;

        ZFCP_LOG_TRACE(
          "enter (file=0x%lx  user_buf=0x%lx "
          "user_length=%li *offset=0x%lx)\n",
          (unsigned long)file,
          (unsigned long)user_buf,
          user_len,
          (unsigned long)pos);

        if (pos < 0 || pos >= pbuf->len)
                return 0;

        len = min(user_len, (unsigned long)(pbuf->len - pos));
        if (copy_to_user(user_buf, &(pbuf->buf[pos]), len))
                return -EFAULT;

        *offset = pos + len;

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n",
                       ((unsigned long)(pbuf->len - pos)),
                       user_len);

        ZFCP_LOG_TRACE("exit (%Li)\n", len);

        return len;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function: zfcp_unit_proc_write
 *
 */

ssize_t zfcp_unit_proc_write(struct file *file,
                        const char *user_buf,
                        size_t user_len,
                        loff_t *offset)

{

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        char *buffer = NULL;
        size_t my_count = user_len;
        const struct inode *ino = file->f_dentry->d_inode;
        const struct proc_dir_entry *dp = ino->u.generic_ip;
        zfcp_unit_t *unit = dp->data;

        ZFCP_LOG_TRACE(
                "enter (file=0x%lx  user_buf=0x%lx "
                "user_length=%li *offset=0x%lx)\n",
                (unsigned long)file,
                (unsigned long)user_buf,
                user_len,
                (unsigned long)*offset);

        buffer = ZFCP_KMALLOC(my_count + 1, GFP_KERNEL);
        if (!buffer) {
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                                " input. Input will be ignored.\n");
                user_len = -ENOMEM;
                goto out;
        }
        ZFCP_LOG_TRACE("buffer allocated...\n");

        copy_from_user(buffer, user_buf, my_count);

        buffer[my_count] = '\0'; /* for debugging */

        ZFCP_LOG_TRACE("user_len= %ld, buffer=>%s<\n",
                user_len, buffer);

	if ((strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) ||
	    (strncmp(ZFCP_SET_ONLINE, buffer, strlen(ZFCP_SET_ONLINE)) == 0)) {
		ZFCP_LOG_NORMAL(
			"user triggered (re)start of all operations on the "
			"unit with FCP_LUN 0x%016Lx on the port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			unit->port->adapter->devno);
		zfcp_erp_modify_unit_status(
			unit,
			ZFCP_STATUS_COMMON_RUNNING,
			ZFCP_SET);
		zfcp_erp_unit_reopen(unit, ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_erp_wait(unit->port->adapter);
		user_len = strlen(buffer);
	} else	if (strncmp(ZFCP_SET_OFFLINE, buffer, strlen(ZFCP_SET_OFFLINE)) == 0) {
		ZFCP_LOG_NORMAL(
			"user triggered shutdown of all operations on the "
			"unit with FCP_LUN 0x%016Lx on the port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			unit->port->adapter->devno);
		zfcp_erp_unit_shutdown(unit, 0);
		zfcp_erp_wait(unit->port->adapter);
		user_len = strlen(buffer);
	} else  {
		ZFCP_LOG_INFO("error: unknown procfs command\n");
		user_len = -EINVAL;
	}

        ZFCP_LOG_TRACE("freeing buffer..\n");
        ZFCP_KFREE(buffer, my_count + 1);
out:
        ZFCP_LOG_TRACE("exit (%li)\n", user_len);
        return (user_len);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:	zfcp_scsi_detect
 *
 * purpose:	This routine is called by the SCSI stack mid layer
 *		to query detected host bus adapters.
 *
 * returns:	number of detcted HBAs (0, if no HBAs detected)
 */
int zfcp_scsi_detect(Scsi_Host_Template *shtpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
     
	int adapters = 0;

	ZFCP_LOG_TRACE(
		"enter (shtpnt =0x%lx)\n",
		(unsigned long) shtpnt);

	spin_unlock_irq(&io_request_lock);
	adapters = zfcp_adapter_scsi_register_all();
	spin_lock_irq(&io_request_lock);

	ZFCP_LOG_TRACE(
		"exit (adapters =%d)\n",
		adapters);

	return adapters;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	for all adapters which are not yet registered with SCSI stack:
 *		wait for finish of erp and register adapter with SCSI stack then 
 *
 * returns:	number of adapters registered with SCSI stack
 *
 * FIXME(design):	/proc/scsi/zfcp/add-del_map must be locked as long as we
 *			are in such a loop as implemented here.
 *			We need a guarantee that no adapter will (dis)sappear.
 *			Otherwise list corruption may be caused.
 *			(We can't hold the lock all the time due to possible
 *			 calls to schedule())
 */
static int zfcp_adapter_scsi_register_all()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;
	unsigned long flags;
	zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE("enter\n");

	read_lock_irqsave(&zfcp_data.adapter_list_lock, flags);
	adapter = ZFCP_FIRST_ADAPTER;
	while (adapter) {
		read_unlock_irqrestore(&zfcp_data.adapter_list_lock, flags);
		if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status)) {
			ZFCP_LOG_DEBUG(
				"adapter with devno 0x%04x needs "
				"to be registered with SCSI stack, "
				"waiting for erp to settle\n",
				adapter->devno);
			zfcp_erp_wait(adapter);
			if (zfcp_adapter_scsi_register(adapter) == 0);
				retval++;
		}
		read_lock_irqsave(&zfcp_data.adapter_list_lock, flags);
		adapter = ZFCP_NEXT_ADAPTER(adapter);
	}
	read_unlock_irqrestore(&zfcp_data.adapter_list_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_scsi_select_queue_depth(struct Scsi_Host *host, Scsi_Device *dev_list)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        zfcp_adapter_t *adapter = (zfcp_adapter_t *)host->hostdata[0];
        zfcp_port_t *port = NULL;
        zfcp_unit_t *unit = NULL;
        unsigned long flags=0;
        
        ZFCP_LOG_TRACE("enter (host =0x%lx, dev_list=0x%lx)\n",
                       (unsigned long) host,
                       (unsigned long) dev_list);
        
        read_lock_irqsave(&adapter->port_list_lock, flags);
        ZFCP_FOR_EACH_PORT(adapter, port) {
                read_lock(&port->unit_list_lock);
                ZFCP_FOR_EACH_UNIT(port, unit) {
                        ZFCP_LOG_DEBUG("Determinig if unit 0x%lx"
                                       " supports tagging\n",
                                       (unsigned long) unit);
                        if (!unit->device) 
                                continue; 

                        if (unit->device->tagged_supported) {
                                ZFCP_LOG_DEBUG("Enabling tagging for "
                                               "unit 0x%lx \n",
                                               (unsigned long) unit);
                                unit->device->tagged_queue = 1;
                                unit->device->current_tag = 0;
                                unit->device->queue_depth = ZFCP_CMND_PER_LUN;
				atomic_set_mask(ZFCP_STATUS_UNIT_ASSUMETCQ, &unit->status);
                        } else {
                                ZFCP_LOG_DEBUG("Disabling tagging for "
                                               "unit 0x%lx \n",
                                               (unsigned long) unit);
                                unit->device->tagged_queue = 0;
                                unit->device->current_tag = 0;
                                unit->device->queue_depth = 1;
				atomic_clear_mask(ZFCP_STATUS_UNIT_ASSUMETCQ, &unit->status);
                        }
		}
		read_unlock(&port->unit_list_lock);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

        ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_scsi_revoke
 *
 * purpose:
 *
 * returns:
 */
int zfcp_scsi_revoke(Scsi_Device *sdpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;
	zfcp_unit_t *unit = (zfcp_unit_t*) sdpnt->hostdata;
#if 0
	zfcp_port_t *port = unit->port;
#endif

	ZFCP_LOG_TRACE("enter (sdpnt=0x%lx)\n", (unsigned long)sdpnt);

	if (!unit) {
		ZFCP_LOG_INFO(
			"no unit associated with SCSI device at "
			"address 0x%lx\n",
			(unsigned long)sdpnt);
		goto out;
	}

#if 0
	/* Shutdown entire port if we are going to shutdown the last unit. */
	if (port->units == 1) {
                zfcp_erp_port_shutdown(port, 0);
                zfcp_erp_wait(port->adapter);
        } else {
                zfcp_erp_unit_shutdown(unit, 0);
                zfcp_erp_wait(port->adapter);
        }
#endif
	sdpnt->hostdata = NULL;
	unit->device = NULL;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_scsi_release
 *
 * purpose:	called from SCSI stack mid layer to make this driver
 *		cleanup I/O and resources for this adapter
 *
 * returns:
 */
int zfcp_scsi_release(struct Scsi_Host *shpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;

	ZFCP_LOG_TRACE("enter (shpnt=0x%lx)\n", (unsigned long)shpnt);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_scsi_insert_into_fake_queue
 *
 * purpose:
 *		
 *
 * returns:
 */
inline void zfcp_scsi_insert_into_fake_queue(zfcp_adapter_t *adapter, Scsi_Cmnd *new_cmnd)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        unsigned long flags;
        Scsi_Cmnd *current_cmnd;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx, cmnd=0x%lx)\n", 
                       (unsigned long)adapter,
                       (unsigned long)new_cmnd);

        ZFCP_LOG_DEBUG("Faking SCSI command:\n");
        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                      (char*)new_cmnd->cmnd,
                      new_cmnd->cmd_len);
        
        new_cmnd->host_scribble = NULL;

        write_lock_irqsave(&adapter->fake_list_lock,flags);
        if(adapter->first_fake_cmnd==NULL) {
                adapter->first_fake_cmnd = new_cmnd;
                adapter->fake_scsi_timer.function = 
                        zfcp_scsi_process_and_clear_fake_queue;
                adapter->fake_scsi_timer.data = 
                        (unsigned long)adapter;
                adapter->fake_scsi_timer.expires = 
                        jiffies + ZFCP_FAKE_SCSI_COMPLETION_TIME;
                add_timer(&adapter->fake_scsi_timer);
        } else {        
                for(current_cmnd=adapter->first_fake_cmnd;
                    current_cmnd->host_scribble != NULL;
                    current_cmnd = (Scsi_Cmnd *)(current_cmnd->host_scribble));
                current_cmnd->host_scribble = (char *)new_cmnd;
        }
	atomic_inc(&adapter->fake_scsi_reqs_active);
        write_unlock_irqrestore(&adapter->fake_list_lock,flags);

	ZFCP_LOG_TRACE("exit\n");
 
	return;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_scsi_process_and_clear_fake_queue
 *
 * purpose:
 *		
 *
 * returns:
 */
inline void zfcp_scsi_process_and_clear_fake_queue(unsigned long data)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        unsigned long flags;
        Scsi_Cmnd *current_cmnd;
        Scsi_Cmnd *next_cmnd;
        zfcp_adapter_t *adapter=(zfcp_adapter_t *)data;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", data);

        /*
         * We need a common lock for scsi_req on command completion
         * as well as on command abort to avoid race conditions
         * during completions and aborts taking place at the same time.
         * It needs to be the outer lock as in the eh_abort_handler.
         */
        read_lock_irqsave(&adapter->abort_lock, flags);
        write_lock(&adapter->fake_list_lock);
        if(adapter->first_fake_cmnd == NULL) {
                ZFCP_LOG_DEBUG("Processing of fake-queue called "
                               "for an empty queue.\n");
        } else {        
                current_cmnd=adapter->first_fake_cmnd;
                do {
                        next_cmnd=(Scsi_Cmnd *)(current_cmnd->host_scribble);
                        current_cmnd->host_scribble = NULL;
#if 0
			zfcp_cmd_dbf_event_scsi("clrfake", adapter, current_cmnd);
#endif
                        current_cmnd->scsi_done(current_cmnd);
#ifdef ZFCP_DEBUG_REQUESTS
                        debug_text_event(adapter->req_dbf, 2, "fk_done:");
                        debug_event(adapter->req_dbf, 2, &current_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
			atomic_dec(&adapter->fake_scsi_reqs_active);
                        current_cmnd=next_cmnd;
                } while (next_cmnd != NULL);
                /* Set list to empty */
                adapter->first_fake_cmnd = NULL;
        }
        write_unlock(&adapter->fake_list_lock);
        read_unlock_irqrestore(&adapter->abort_lock, flags);

	ZFCP_LOG_TRACE("exit\n");
 
	return;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static void zfcp_scsi_command_fail(
		zfcp_unit_t *unit,
		Scsi_Cmnd *scsi_cmnd,
		int result)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

	zfcp_adapter_t *adapter = unit->port->adapter;

#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 2, "de_done:");
	debug_event(adapter->req_dbf, 2, &scsi_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

	scsi_cmnd->SCp.ptr = (char*)0;
	scsi_cmnd->result = result;

	zfcp_cmd_dbf_event_scsi("failing", adapter, scsi_cmnd);

	scsi_cmnd->scsi_done(scsi_cmnd);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

 
static void zfcp_scsi_command_fake(
		zfcp_unit_t *unit,
		Scsi_Cmnd *scsi_cmnd)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

	if (scsi_cmnd->SCp.ptr) {
		if (((unsigned long)scsi_cmnd->SCp.ptr + ZFCP_SCSI_RETRY_TIMEOUT)
		    < jiffies) {
			/* leave it to the SCSI stack eh */
			zfcp_scsi_command_fail(unit, scsi_cmnd, DID_TIME_OUT << 16);
			return;
		}
	} else	scsi_cmnd->SCp.ptr = (char*)jiffies;
	scsi_cmnd->retries--;	/* -1 is ok */
	scsi_cmnd->result |= DID_SOFT_ERROR << 16
			     | SUGGEST_RETRY << 24;
	zfcp_scsi_insert_into_fake_queue(unit->port->adapter, scsi_cmnd);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/**
 * zfcp_scsi_command_async - worker for zfcp_scsi_queuecommand and
 * zfcp_scsi_command_sync
 */
int zfcp_scsi_command_async(
		zfcp_unit_t *unit,
		Scsi_Cmnd *scsi_cmnd,
		void (* done)(Scsi_Cmnd *))
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(unit->port->adapter->req_dbf, 3, "q_scpnt");
	debug_event(unit->port->adapter->req_dbf, 3, &scsi_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

	scsi_cmnd->scsi_done = done;
	scsi_cmnd->result = 0;

	if (!unit) {
		zfcp_scsi_command_fail(unit, scsi_cmnd, DID_NO_CONNECT << 16);
		goto out;
	}

	if (atomic_test_mask(
			ZFCP_STATUS_COMMON_ERP_FAILED,
			&unit->status)) {
		zfcp_scsi_command_fail(unit, scsi_cmnd, DID_ERROR << 16);
		goto out;
	}

	if (!atomic_test_mask(
			ZFCP_STATUS_COMMON_RUNNING,
			&unit->status)) {
		zfcp_scsi_command_fail(unit, scsi_cmnd, DID_ERROR << 16);
		goto out;
	}

	if (!atomic_test_mask(
			ZFCP_STATUS_COMMON_UNBLOCKED,
			&unit->status))  {
		zfcp_scsi_command_fake(unit, scsi_cmnd);
		goto out;
	}

	if (zfcp_fsf_send_fcp_command_task(unit, scsi_cmnd) < 0)
		zfcp_scsi_command_fake(unit, scsi_cmnd);

out:
	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_scsi_command_sync_handler(Scsi_Cmnd *scsi_cmnd)
{
	struct completion *wait = (struct completion*) scsi_cmnd->bh_next;
	complete(wait);
}


/**
 * zfcp_scsi_command_sync - send a SCSI command and wait for completion
 * returns 0, errors are indicated by scsi_cmnd->result
 */
int zfcp_scsi_command_sync(
		zfcp_unit_t *unit,
		Scsi_Cmnd *scsi_cmnd)
{
	DECLARE_COMPLETION(wait);

	scsi_cmnd->bh_next = (void*) &wait;  /* silent re-use */
        zfcp_scsi_command_async(
			unit,
			scsi_cmnd,
			zfcp_scsi_command_sync_handler);
	wait_for_completion(&wait);

	return 0;
}


 
/*
 * function:	zfcp_scsi_queuecommand
 *
 * purpose:	enqueues a SCSI command to the specified target device
 *
 * note:        The scsi_done midlayer function may be called directly from
 *              within queuecommand provided queuecommand returns with success (0)
 *              If it fails, it is expected that the command could not be sent
 *              and is still available for processing.
 *              As we ensure that queuecommand never fails, we have the choice 
 *              to call done directly wherever we please.
 *              Thus, any kind of send errors other than those indicating
 *              'infinite' retries will be reported directly.
 *              Retry requests are put into a list to be processed under timer 
 *              control once in a while to allow for other operations to
 *              complete in the meantime.
 *
 * returns:	0 - success, SCSI command enqueued
 *		!0 - failure, note that we never allow this to happen as the 
 *              SCSI stack would block indefinitely should a non-zero return
 *              value be reported if there are no outstanding commands
 *              (as in when the queues are down)
 */
int zfcp_scsi_queuecommand(
		Scsi_Cmnd *scsi_cmnd,
		void (* done)(Scsi_Cmnd *))
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	zfcp_unit_t *unit;
	zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE(
		"enter (scsi_cmnd=0x%lx done=0x%lx)\n",
		(unsigned long)scsi_cmnd,
		(unsigned long)done);

	spin_unlock_irq(&io_request_lock);

	/*
	 * figure out adapter
	 * (previously stored there by the driver when
	 * the adapter was registered)
	 */
	adapter = (zfcp_adapter_t*) scsi_cmnd->host->hostdata[0];

	/*
	 * figure out target device
	 * (stored there by the driver when the first command
	 * is sent to this target device)
	 * ATTENTION: assumes hostdata initialized to NULL by
	 * mid layer (see scsi_scan.c)
	 */
	if (!scsi_cmnd->device->hostdata) {
		unit = zfcp_unit_lookup(
				adapter,
				scsi_cmnd->device->channel,
				scsi_cmnd->device->id,
				scsi_cmnd->device->lun);
		/* Is specified unit configured? */
		if (unit) {
			scsi_cmnd->device->hostdata = unit;
			unit->device = scsi_cmnd->device;
			ZFCP_LOG_DEBUG(
				"logical unit address (0x%lx) saved "
				"for direct lookup and scsi_stack "
				"pointer 0x%lx saved in unit structure\n",
				(unsigned long)unit,
				(unsigned long)unit->device);
		}
	} else	unit = (zfcp_unit_t*) scsi_cmnd->device->hostdata;

	zfcp_scsi_command_async(unit, scsi_cmnd, done);

	spin_lock_irq(&io_request_lock);

	ZFCP_LOG_TRACE("exit (%i)\n", 0);

	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_unit_lookup
 *
 * purpose:
 *
 * returns:
 *
 * context:	
 */
static zfcp_unit_t* zfcp_unit_lookup(
	zfcp_adapter_t *adapter,
	int channel,
	int id,
	int lun)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_OTHER

	zfcp_port_t *port; 
	zfcp_unit_t *unit = NULL;
	unsigned long flags;
 
	ZFCP_LOG_TRACE(
		"enter (adapter devno=0x%04x, channel=%i, id=%i, lun=%i)\n",
		adapter->devno,
		channel,
		id,
		lun);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if ((scsi_id_t)id != port->scsi_id)
			continue;
		read_lock(&port->unit_list_lock);
		ZFCP_FOR_EACH_UNIT(port, unit) {
			if ((scsi_lun_t)lun == unit->scsi_lun) {
				ZFCP_LOG_TRACE("found unit\n");
				break;
			}
		}
		read_unlock(&port->unit_list_lock);
		if (unit)
			break;
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
 
	ZFCP_LOG_TRACE("exit (0x%lx)\n", (unsigned long)unit);
 
	return unit;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_scsi_potential_abort_on_fake
 *
 * purpose:
 *
 * returns:     0 - no fake request aborted
 *              1 - fake request was aborted
 *
 * context:	both the adapter->abort_lock and the 
 *              adapter->fake_list_lock are assumed to be held write lock
 *              irqsave
 */
inline int zfcp_scsi_potential_abort_on_fake(zfcp_adapter_t *adapter, Scsi_Cmnd *cmnd)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        Scsi_Cmnd *current_cmnd, *prev_cmnd;
	unsigned long flags;
        int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx, cmnd=0x%lx)\n",
                       (unsigned long)adapter,
                       (unsigned long)cmnd);

	write_lock_irqsave(&adapter->fake_list_lock, flags);

        current_cmnd=adapter->first_fake_cmnd;

	if (!current_cmnd)
		goto out;

        if(current_cmnd==cmnd) {
                adapter->first_fake_cmnd=(Scsi_Cmnd *)cmnd->host_scribble;
                cmnd->host_scribble=NULL;
                if(adapter->first_fake_cmnd==NULL) {
                        /* No need to wake anymore  */
                        /* Note: It does not matter if the timer has already
                         * expired, the fake_list_lock takes care of 
                         * potential races 
                         */
                        del_timer(&adapter->fake_scsi_timer);
                }
		atomic_dec(&adapter->fake_scsi_reqs_active);
                retval=1;
                goto out;
        } 
        do {
                prev_cmnd = current_cmnd;
                current_cmnd = (Scsi_Cmnd *)(current_cmnd->host_scribble);
                if (current_cmnd==cmnd) {
                        prev_cmnd->host_scribble=current_cmnd->host_scribble;
                        current_cmnd->host_scribble=NULL;
			atomic_dec(&adapter->fake_scsi_reqs_active);
                        retval=1;
                        goto out;
                }
        } while (current_cmnd->host_scribble != NULL);         

out:
	write_unlock_irqrestore(&adapter->fake_list_lock, flags);

	ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
                }        


/*
 * function:	zfcp_scsi_eh_abort_handler
 *
 * purpose:	tries to abort the specified (timed out) SCSI command
 *
 * note: 	We do not need to care for a SCSI command which completes
 *		normally but late during this abort routine runs.
 *		We are allowed to return late commands to the SCSI stack.
 *		It tracks the state of commands and will handle late commands.
 *		(Usually, the normal completion of late commands is ignored with
 *		respect to the running abort operation. Grep for 'done_late'
 *		in the SCSI stacks sources.)
 *
 * returns:	SUCCESS	- command has been aborted and cleaned up in internal
 *			  bookkeeping,
 *			  SCSI stack won't be called for aborted command
 *		FAILED	- otherwise
 */
int zfcp_scsi_eh_abort_handler(Scsi_Cmnd *scpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = SUCCESS;
	zfcp_fsf_req_t *new_fsf_req, *old_fsf_req;
	zfcp_adapter_t *adapter = (zfcp_adapter_t*) scpnt->host->hostdata[0];
	zfcp_unit_t *unit = (zfcp_unit_t*) scpnt->device->hostdata;
	zfcp_port_t *port = unit->port;
	unsigned long flags;
	u32 status = 0;
#ifdef ZFCP_DEBUG_ABORTS
	/* the components of a abort_dbf record (fixed size record) */
	u64		dbf_scsi_cmnd	= (unsigned long)scpnt;
	char		dbf_opcode[ZFCP_ABORT_DBF_LENGTH];
	wwn_t		dbf_wwn		= port->wwpn;
	fcp_lun_t	dbf_fcp_lun	= unit->fcp_lun;
	u64		dbf_retries	= scpnt->retries;
	u64		dbf_allowed	= scpnt->allowed;
	u64		dbf_timeout	= 0;
	u64		dbf_fsf_req	= 0;
	u64		dbf_fsf_status	= 0;
	u64		dbf_fsf_qual[2]	= { 0, 0 };
	char		dbf_result[ZFCP_ABORT_DBF_LENGTH]
					= { "##undef" };

	memset(dbf_opcode, 0, ZFCP_ABORT_DBF_LENGTH);
	memcpy(	dbf_opcode,
		scpnt->cmnd,
		min(scpnt->cmd_len, (unsigned char)ZFCP_ABORT_DBF_LENGTH));
#endif

        /*TRACE*/
	ZFCP_LOG_TRACE("enter (scpnt=0x%lx)\n", (unsigned long)scpnt);

	ZFCP_LOG_INFO(
		"Aborting for adapter=0x%lx, devno=0x%04x, scsi_cmnd=0x%lx\n",
		(unsigned long)adapter,
		adapter->devno,
		(unsigned long)scpnt);

	spin_unlock_irq(&io_request_lock);
#if 0
               /* DEBUG */
        retval=FAILED;
        goto out;
#endif

	/*
	 * Race condition between normal (late) completion and abort has
	 * to be avoided.
	 * The entirity of all accesses to scsi_req have to be atomic.
	 * scsi_req is usually part of the fsf_req (for requests which
	 * are not faked) and thus we block the release of fsf_req
	 * as long as we need to access scsi_req.
	 * For faked commands we use the same lock even if they are not
	 * put into the fsf_req queue. This makes implementation
	 * easier. 
	 */
	write_lock_irqsave(&adapter->abort_lock, flags);

	/*
	 * Check if we deal with a faked command, which we may just forget
	 * about from now on
	 */
	if (zfcp_scsi_potential_abort_on_fake(adapter, scpnt)) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##faked", ZFCP_ABORT_DBF_LENGTH);
#endif
		retval = SUCCESS;
		goto out;
	}

	/*
	 * Check whether command has just completed and can not be aborted.
	 * Even if the command has just been completed late, we can access
	 * scpnt since the SCSI stack does not release it at least until
	 * this routine returns. (scpnt is parameter passed to this routine
	 * and must not disappear during abort even on late completion.)
	 */
	old_fsf_req = (zfcp_fsf_req_t*) scpnt->host_scribble;
	if (!old_fsf_req) {
		ZFCP_LOG_DEBUG("late command completion overtook abort\n");
		/*
		 * That's it.
		 * Do not initiate abort but return SUCCESS.
		 */
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		retval = SUCCESS;
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##late1", ZFCP_ABORT_DBF_LENGTH);
#endif
		goto out;
	}
#ifdef ZFCP_DEBUG_ABORTS
	dbf_fsf_req = (unsigned long)old_fsf_req;
	dbf_timeout = (jiffies - old_fsf_req->data.send_fcp_command_task.start_jiffies) / HZ;
#endif

        old_fsf_req->data.send_fcp_command_task.scsi_cmnd = NULL;
	/* mark old request as being aborted */
	old_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTING;
	/*
	 * We have to collect all information (e.g. unit) needed by 
	 * zfcp_fsf_abort_fcp_command before calling that routine
	 * since that routine is not allowed to access
	 * fsf_req which it is going to abort.
	 * This is because of we need to release fsf_req_list_lock
	 * before calling zfcp_fsf_abort_fcp_command.
	 * Since this lock will not be held, fsf_req may complete
	 * late and may be released meanwhile.
	 */
	ZFCP_LOG_DEBUG(
		"unit=0x%lx, unit_fcp_lun=0x%Lx\n",
		(unsigned long)unit,
		(llui_t)unit->fcp_lun);

	/*
	 * We block (call schedule)
	 * That's why we must release the lock and enable the
	 * interrupts before.
	 * On the other hand we do not need the lock anymore since
	 * all critical accesses to scsi_req are done.
	 */
	write_unlock_irqrestore(&adapter->abort_lock, flags);
	/* call FSF routine which does the abort */
	new_fsf_req = zfcp_fsf_abort_fcp_command(
				(unsigned long)old_fsf_req, adapter, unit, 0);
	ZFCP_LOG_DEBUG(
		"new_fsf_req=0x%lx\n",
		(unsigned long) new_fsf_req);
	if (!new_fsf_req) {
		retval = FAILED;
		ZFCP_LOG_DEBUG(
			"warning: Could not abort SCSI command "
			"at 0x%lx\n",
			(unsigned long)scpnt);
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##nores", ZFCP_ABORT_DBF_LENGTH);
#endif
		goto out;
	}

	/* wait for completion of abort */
	ZFCP_LOG_DEBUG("Waiting for cleanup....\n");
#ifdef ZFCP_DEBUG_ABORTS
	/* FIXME: copying zfcp_fsf_req_wait_and_cleanup code is not really nice */
	__wait_event(
		new_fsf_req->completion_wq,
		new_fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	status = new_fsf_req->status;
	dbf_fsf_status = new_fsf_req->qtcb->header.fsf_status;
	/*
	 * Ralphs special debug load provides timestamps in the FSF
	 * status qualifier. This might be specified later if being
	 * useful for debugging aborts.
	 */
	dbf_fsf_qual[0] = *(u64*)&new_fsf_req->qtcb->header.fsf_status_qual.word[0];
	dbf_fsf_qual[1] = *(u64*)&new_fsf_req->qtcb->header.fsf_status_qual.word[2];
	retval = zfcp_fsf_req_cleanup(new_fsf_req);
#else
	retval = zfcp_fsf_req_wait_and_cleanup(
			new_fsf_req,
			ZFCP_UNINTERRUPTIBLE,
			&status);
#endif
	ZFCP_LOG_DEBUG(
		"Waiting for cleanup complete, status=0x%x\n",
		status);
	/* status should be valid since signals were not permitted */
	if (status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED) {
		retval = SUCCESS;
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##succ", ZFCP_ABORT_DBF_LENGTH);
#endif
	} else	if (status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED) {
		retval = SUCCESS;
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##late2", ZFCP_ABORT_DBF_LENGTH);
#endif
	} else	{
		retval = FAILED;
#ifdef ZFCP_DEBUG_ABORTS
		strncpy(dbf_result, "##fail", ZFCP_ABORT_DBF_LENGTH);
#endif
	}

out:
#ifdef ZFCP_DEBUG_ABORTS
	debug_event(adapter->abort_dbf, 1, &dbf_scsi_cmnd, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_opcode, ZFCP_ABORT_DBF_LENGTH);
	debug_event(adapter->abort_dbf, 1, &dbf_wwn, sizeof(wwn_t));
	debug_event(adapter->abort_dbf, 1, &dbf_fcp_lun, sizeof(fcp_lun_t));
	debug_event(adapter->abort_dbf, 1, &dbf_retries, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_allowed, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_timeout, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_req, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_status, sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_qual[0], sizeof(u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_qual[1], sizeof(u64));
	debug_text_event(adapter->abort_dbf, 1, dbf_result);
#endif

	spin_lock_irq(&io_request_lock);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_scsi_eh_device_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int zfcp_scsi_eh_device_reset_handler(Scsi_Cmnd *scpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval;
	zfcp_unit_t *unit = (zfcp_unit_t*) scpnt->device->hostdata;
        /*TRACE*/
	ZFCP_LOG_TRACE("enter (scpnt=0x%lx)\n", (unsigned long)scpnt);

        spin_unlock_irq(&io_request_lock);
	/*
	 * We should not be called to reset a target which we 'sent' faked SCSI
	 * commands since the abort of faked SCSI commands should always
	 * succeed (simply delete timer). 
	 */
	if (!unit) {
		ZFCP_LOG_NORMAL(
			"bug: Tried to reset a non existant unit.\n");
		retval = SUCCESS;
		goto out;
	}
	ZFCP_LOG_NORMAL(
		"Resetting SCSI device "
		"(unit with FCP_LUN 0x%016Lx on the port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x)\n",
		(llui_t)unit->fcp_lun,
		(llui_t)unit->port->wwpn,
		unit->port->adapter->devno);

	/*
	 * If we do not know whether the unit supports 'logical unit reset'
	 * then try 'logical unit reset' and proceed with 'target reset'
	 * if 'logical unit reset' fails.
	 * If the unit is known not to support 'logical unit reset' then
	 * skip 'logical unit reset' and try 'target reset' immediately.
	 */
	if (!atomic_test_mask(ZFCP_STATUS_UNIT_NOTSUPPUNITRESET, &unit->status)) { 
		retval = zfcp_task_management_function(unit, LOGICAL_UNIT_RESET);
		if (retval) {
			ZFCP_LOG_DEBUG(
				"logical unit reset failed (unit=0x%lx)\n",
				(unsigned long)unit);
			if (retval == -ENOTSUPP)
				atomic_set_mask(ZFCP_STATUS_UNIT_NOTSUPPUNITRESET, 
                                                &unit->status); 
			/* fall through and try 'target reset' next */
		} else	{
			ZFCP_LOG_DEBUG(
				"logical unit reset succeeded (unit=0x%lx)\n",
				(unsigned long)unit);
			/* avoid 'target reset' */
			retval = SUCCESS;
			goto out;
		}
	}
	retval = zfcp_task_management_function(unit, TARGET_RESET);
	if (retval) {
		ZFCP_LOG_DEBUG(
			"target reset failed (unit=0x%lx)\n",
			(unsigned long)unit);
		retval = FAILED;
	} else	{
		ZFCP_LOG_DEBUG(
			"target reset succeeded (unit=0x%lx)\n",
			(unsigned long)unit);
		retval = SUCCESS;
	}

out:
	spin_lock_irq(&io_request_lock);
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_task_management_function(zfcp_unit_t *unit, u8 tm_flags)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	zfcp_adapter_t *adapter = unit->port->adapter;
	int retval;
	int status;
	zfcp_fsf_req_t *fsf_req;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx tm_flags=0x%x)\n",
		(unsigned long)unit,
		tm_flags);

	/* issue task management function */	
	fsf_req = zfcp_fsf_send_fcp_command_task_management
	 		(adapter, unit, tm_flags, 0);
	if (!fsf_req) {
                ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
			"task management (abort, reset, etc) request "
			"for the unit with FCP_LUN 0x%016Lx connected to "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			adapter->devno);
		retval = -ENOMEM;
		goto out;
	}

	retval = zfcp_fsf_req_wait_and_cleanup(
			fsf_req,
                        ZFCP_UNINTERRUPTIBLE,
			&status);
	/*
	 * check completion status of task management function
	 * (status should always be valid since no signals permitted)
	 */
        if (status & ZFCP_STATUS_FSFREQ_TMFUNCFAILED)
		retval = -EIO;
	else	if (status & ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP)
			retval = -ENOTSUPP;
		else	retval = 0;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_scsi_eh_bus_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int zfcp_scsi_eh_bus_reset_handler(Scsi_Cmnd *scpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;
        zfcp_unit_t *unit;

	ZFCP_LOG_TRACE("enter (scpnt=0x%lx)\n", (unsigned long)scpnt);
        spin_unlock_irq(&io_request_lock);

        unit = (zfcp_unit_t *)scpnt->device->hostdata; 
        /*DEBUG*/
	ZFCP_LOG_NORMAL(
		"Resetting SCSI bus "
		"(unit with FCP_LUN 0x%016Lx on the port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x)\n",
		(llui_t)unit->fcp_lun,
		(llui_t)unit->port->wwpn,
		unit->port->adapter->devno);
        zfcp_erp_adapter_reopen(unit->port->adapter, 0);
	zfcp_erp_wait(unit->port->adapter);
        retval = SUCCESS;

	spin_lock_irq(&io_request_lock);
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_scsi_eh_host_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int zfcp_scsi_eh_host_reset_handler(Scsi_Cmnd *scpnt)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;
        zfcp_unit_t *unit;

	ZFCP_LOG_TRACE("enter (scpnt=0x%lx)\n", (unsigned long)scpnt);
        spin_unlock_irq(&io_request_lock);

        unit = (zfcp_unit_t *)scpnt->device->hostdata; 
        /*DEBUG*/
	ZFCP_LOG_NORMAL(
		"Resetting SCSI host "
		"(unit with FCP_LUN 0x%016Lx on the port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x)\n",
		(llui_t)unit->fcp_lun,
		(llui_t)unit->port->wwpn,
		unit->port->adapter->devno);
        zfcp_erp_adapter_reopen(unit->port->adapter, 0);
	zfcp_erp_wait(unit->port->adapter);
        retval=SUCCESS;

	spin_lock_irq(&io_request_lock);
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_adapter_detect
 *
 * purpose:	checks whether the specified zSeries device is
 *		a supported adapter
 *
  * returns:	0 - for supported adapter
 *		!0 - for unsupported devices
 */
int zfcp_adapter_detect(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_OTHER
     
        int retval = 0;

	ZFCP_LOG_TRACE("enter: (adapter=0x%lx)\n", (unsigned long)adapter);
        retval = get_dev_info_by_devno(adapter->devno, &adapter->devinfo);
        if (retval) {
		ZFCP_LOG_INFO(
			"warning: Device information for the adapter "
			"with devno 0x%04x could not be determined. "
			"The attempt returned %d. It is probable that "
			"no device with this devno exists.\n",
			adapter->devno,
			retval);
		goto out;
        }
        
        if (adapter->devinfo.status == 0){
		ZFCP_LOG_TRACE(
			"Adapter returned \"OK\", " 
			"devno is 0x%04x.\n", 
			(unsigned int) adapter->devno);
		goto ok;
        }
        if (adapter->devinfo.status & DEVSTAT_NOT_OPER) {
                ZFCP_LOG_INFO(
			"error: Adapter with devno 0x%04x is not "
			"operational.\n",
			(unsigned int) adapter->devno);
		retval = -EBUSY;
        }
        if (adapter->devinfo.status & DEVSTAT_DEVICE_OWNED) {
		ZFCP_LOG_INFO(
			"error: Adapter with devno 0x%04x is already "
			"owned by another driver.\n",
			(unsigned int) adapter->devno);
		retval = -EACCES;
        }
        if (adapter->devinfo.status & DEVSTAT_UNKNOWN_DEV) {
		ZFCP_LOG_INFO(
			"error: Adapter with devno 0x%04x is not "
			"an FCP card.\n",
			(unsigned int) adapter->devno);
		retval = -EACCES;
        }
        if (adapter->devinfo.status & (~(DEVSTAT_NOT_OPER |
                                        DEVSTAT_DEVICE_OWNED |
                                        DEVSTAT_UNKNOWN_DEV))){
		ZFCP_LOG_NORMAL(
			"bug: Adapter with devno 0x%04x returned an "
			"unexpected condition during the identification "
			"phase. (debug info %d)\n",
			(unsigned int) adapter->devno,
			adapter->devinfo.status);
		retval = -ENODEV;
        }
        if (retval < 0)
		goto out;
ok:
        if ((adapter->devinfo.sid_data.cu_type != ZFCP_CONTROL_UNIT_TYPE) ||
            (adapter->devinfo.sid_data.cu_model != ZFCP_CONTROL_UNIT_MODEL) ||
            (adapter->devinfo.sid_data.dev_type != ZFCP_DEVICE_TYPE) ||
           ((adapter->devinfo.sid_data.dev_model != ZFCP_DEVICE_MODEL) &&
            (adapter->devinfo.sid_data.dev_model != ZFCP_DEVICE_MODEL_PRIV))) {
		ZFCP_LOG_NORMAL(
			"error: Adapter with devno 0x%04x is not "
			"an FCP card.\n",
			(unsigned int) adapter->devno);
		retval = -ENODEV;
        }
        
out:
        ZFCP_LOG_TRACE(
		"CU type, model, dev type, model" 
		" 0x%x, 0x%x, 0x%x, 0x%x.\n", 
		adapter->devinfo.sid_data.cu_type,
		adapter->devinfo.sid_data.cu_model,
		adapter->devinfo.sid_data.dev_type,
		adapter->devinfo.sid_data.dev_model);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_adapter_irq_register(zfcp_adapter_t* adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	signed int tmp_irq;     /* adapter->irq is unsigned 16 bit! */

	ZFCP_LOG_TRACE("enter\n");

	/* find out IRQ */
	tmp_irq = get_irq_by_devno(adapter->devno);

        if (tmp_irq < 0 || tmp_irq > 0x0FFFF) {
		ZFCP_LOG_NORMAL(
			"bug: The attempt to identify the irq for the "
			"adapter with devno 0x%04x failed. All map entries "
			"containing this devno are ignored. "
			"(debug info 0x%x)\n",
			adapter->devno,
			tmp_irq);
		retval = -ENXIO;
		goto out;
        }
	ZFCP_LOG_TRACE(
		"get_irq_by_devno returned irq=0x%x.\n",
		tmp_irq);
	adapter->irq = tmp_irq;

	/* request IRQ */
	retval = s390_request_irq_special(
			adapter->irq,
			(void *)zfcp_cio_handler,
			zfcp_dio_not_oper_handler,
			0,
			zfcp_data.scsi_host_template.name,
			(void *)&adapter->devstat);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not allocate irq %i to the adapter "
                        "with devno 0x%04x (debug info %i).\n",
			adapter->irq,
                        adapter->devno,
			retval);
		goto out;
	}
	atomic_set_mask(ZFCP_STATUS_ADAPTER_IRQOWNER, &adapter->status);
        ZFCP_LOG_DEBUG("request irq %i successfull\n", adapter->irq);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_adapter_irq_unregister(zfcp_adapter_t* adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", (unsigned long)adapter);

        if(!atomic_test_mask(ZFCP_STATUS_ADAPTER_IRQOWNER, &adapter->status)) {
                ZFCP_LOG_DEBUG("Adapter with devno 0x%04x does not own "
                               "an irq, skipping over freeing attempt.\n",
                               adapter->devno);
                goto out;    
        }
        /* Note: There exists no race condition when the irq is given up by some
           other agency while at this point. The CIO layer will still handle the
           subsequent free_irq correctly.
        */
	free_irq(adapter->irq, (void *) &adapter->devstat);
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_IRQOWNER, &adapter->status);
	ZFCP_LOG_DEBUG("gave up irq=%i\n", adapter->irq);
 out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_adapter_scsi_register(zfcp_adapter_t* adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx)\n",
		(unsigned long)adapter);

	/* register adapter as SCSI host with mid layer of SCSI stack */
	adapter->scsi_host = scsi_register(
				&zfcp_data.scsi_host_template,
				sizeof(zfcp_adapter_t*));
	if (!adapter->scsi_host) {
		ZFCP_LOG_NORMAL(
			"error: Not enough free memory. "
			"Could not register host-adapter with "
			"devno 0x%04x with the SCSI-stack.\n",
			adapter->devno);
		retval = -EIO;
		goto out;
	} 
	atomic_set_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);
	ZFCP_LOG_DEBUG(
		"host registered, scsi_host at 0x%lx\n",
		(unsigned long)adapter->scsi_host);

	/* tell the SCSI stack some characteristics of this adapter */
	adapter->scsi_host->max_id = adapter->max_scsi_id + 1;
	adapter->scsi_host->max_lun = adapter->max_scsi_lun + 1;
	adapter->scsi_host->max_channel = 0;
	adapter->scsi_host->irq = adapter->irq;
	adapter->scsi_host->unique_id = adapter->devno;
	adapter->scsi_host->max_cmd_len = ZFCP_MAX_SCSI_CMND_LENGTH;
	adapter->scsi_host->loaded_as_module
		= (zfcp_data.scsi_host_template.module ? 1 : 0);
	adapter->scsi_host->select_queue_depths
		= zfcp_scsi_select_queue_depth;

	/*
	 * save a pointer to our own adapter data structure within
	 * hostdata field of SCSI host data structure
	 */
	adapter->scsi_host->hostdata[0] = (unsigned long)adapter;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


int zfcp_initialize_with_0copy(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO

        int retval = 0;
        qdio_initialize_t init_data;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", (unsigned long)adapter);
                
        init_data.irq = adapter->irq;
        init_data.q_format = QDIO_SCSI_QFMT;
        memcpy(init_data.adapter_name,&adapter->name,8);
        init_data.qib_param_field_format = 0;
        init_data.qib_param_field = NULL;
        init_data.input_slib_elements = NULL;
        init_data.output_slib_elements = NULL;
        init_data.min_input_threshold = ZFCP_MIN_INPUT_THRESHOLD;
        init_data.max_input_threshold = ZFCP_MAX_INPUT_THRESHOLD;
        init_data.min_output_threshold = ZFCP_MIN_OUTPUT_THRESHOLD;
        init_data.max_output_threshold = ZFCP_MAX_OUTPUT_THRESHOLD;
        init_data.no_input_qs = 1;
        init_data.no_output_qs = 1;
        init_data.input_handler = zfcp_qdio_response_handler;
        init_data.output_handler = zfcp_qdio_request_handler;
        init_data.int_parm = (unsigned long)adapter;
        init_data.flags = QDIO_INBOUND_0COPY_SBALS|
                QDIO_OUTBOUND_0COPY_SBALS|
                QDIO_USE_OUTBOUND_PCIS;
        init_data.input_sbal_addr_array = 
                (void **)(adapter->response_queue.buffer);
        init_data.output_sbal_addr_array = 
                (void **)(adapter->request_queue.buffer);
        ZFCP_LOG_TRACE("Before qdio_initialise\n");
	retval = qdio_initialize(&init_data);
        ZFCP_LOG_TRACE("After qdio_initialise\n");
        
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * note:	qdio queues shall be down (no ongoing inbound processing)
 */
static int zfcp_fsf_req_dismiss_all(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;
	zfcp_fsf_req_t *fsf_req, *next_fsf_req;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx)\n",
		(unsigned long)adapter);

	ZFCP_FOR_NEXT_FSFREQ(adapter, fsf_req, next_fsf_req)
		zfcp_fsf_req_dismiss(fsf_req);
	while (!list_empty(&adapter->fsf_req_list_head)) {
		ZFCP_LOG_DEBUG(
			"fsf req list of adapter with "
			"devno 0x%04x not yet empty\n",
			adapter->devno);
		/* wait for woken intiators to clean up their requests */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ZFCP_FSFREQ_CLEANUP_TIMEOUT);
	}

	/* consistency check */
        if (atomic_read(&adapter->fsf_reqs_active)) {
                ZFCP_LOG_NORMAL(
			"bug: There are still %d FSF requests pending "
			"on the adapter with devno 0x%04x after "
			"cleanup.\n",
			atomic_read(&adapter->fsf_reqs_active),
			adapter->devno);
		atomic_set(&adapter->fsf_reqs_active, 0);
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_fsf_req_dismiss(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
	zfcp_fsf_req_complete(fsf_req);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   	zfcp_qdio_handler_error_check
 *
 * purpose:     called by the response handler to determine error condition
 *
 * returns:	error flag
 *
 */
inline int zfcp_qdio_handler_error_check(
        zfcp_adapter_t *adapter,
	unsigned int status,
	unsigned int qdio_error,
	unsigned int siga_error)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO

     int retval=0;

     ZFCP_LOG_TRACE(
          "enter (adapter=0x%lx, status=%i qdio_error=%i siga_error=%i\n",
          (unsigned long) adapter,
          status,
          qdio_error,
          siga_error);

     if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_TRACE)){     
             if (status &  QDIO_STATUS_INBOUND_INT){
                     ZFCP_LOG_TRACE("status is"
                                    " QDIO_STATUS_INBOUND_INT \n");
             }
             if (status & QDIO_STATUS_OUTBOUND_INT){
                     ZFCP_LOG_TRACE("status is"
                                    " QDIO_STATUS_OUTBOUND_INT \n");
             }
     }// if (ZFCP_LOG_CHECK(ZFCP_LOG_LEVEL_TRACE))
     if (status & QDIO_STATUS_LOOK_FOR_ERROR){
             retval=-EIO;
             
             ZFCP_LOG_FLAGS(1,"QDIO_STATUS_LOOK_FOR_ERROR \n");

             ZFCP_LOG_INFO("A qdio problem occured. The status, qdio_error and "
                           "siga_error are 0x%x, 0x%x and 0x%x\n", 
                           status,
                           qdio_error,
                           siga_error); 

             if (status & QDIO_STATUS_ACTIVATE_CHECK_CONDITION){
                     ZFCP_LOG_FLAGS(2, "QDIO_STATUS_ACTIVATE_CHECK_CONDITION\n");
             }
             if (status & QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR){
                     ZFCP_LOG_FLAGS(2, "QDIO_STATUS_MORE_THAN_ONE_QDIO_ERROR\n");
             }
             if (status & QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR){
                     ZFCP_LOG_FLAGS(2, "QDIO_STATUS_MORE_THAN_ONE_SIGA_ERROR\n");
             }
             
             if (siga_error & QDIO_SIGA_ERROR_ACCESS_EXCEPTION) {
                     ZFCP_LOG_FLAGS(2, "QDIO_SIGA_ERROR_ACCESS_EXCEPTION\n"); 
             }
             
             if (siga_error & QDIO_SIGA_ERROR_B_BIT_SET) {
                     ZFCP_LOG_FLAGS(2, "QDIO_SIGA_ERROR_B_BIT_SET\n"); 
             }

             switch (qdio_error) {
             case 0:
                     ZFCP_LOG_FLAGS(3, "QDIO_OK");
                     break;
             case SLSB_P_INPUT_ERROR :
                     ZFCP_LOG_FLAGS(1, "SLSB_P_INPUT_ERROR\n");
                     break;
             case SLSB_P_OUTPUT_ERROR :
                     ZFCP_LOG_FLAGS(1, "SLSB_P_OUTPUT_ERROR\n"); 
                     break;
             default :
                     ZFCP_LOG_NORMAL("bug: Unknown qdio error reported "
                                     "(debug info 0x%x)\n",
                                     qdio_error);
                     break;
             }
             /* Restarting IO on the failed adapter from scratch */
             debug_text_event(adapter->erp_dbf,1,"qdio_err");
		/*
		 * Since we have been using this adapter, it is save to assume
		 * that it is not failed but recoverable. The card seems to
		 * report link-up events by self-initiated queue shutdown.
		 * That is why we need to clear the the link-down flag
		 * which is set again in case we have missed by a mile.
		 */
		zfcp_erp_adapter_reopen(
			adapter, 
			ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
			ZFCP_STATUS_COMMON_ERP_FAILED);
     } // if(status & QDIO_STATUS_LOOK_FOR_ERROR)

     ZFCP_LOG_TRACE("exit (%i)\n", retval);             
     return retval;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_qdio_request_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in request queue
 *
 * returns:	(void)
 */
void zfcp_qdio_request_handler(
	int irq,
	unsigned int status,
	unsigned int qdio_error,
	unsigned int siga_error,
	unsigned int queue_number,
	int first_element,
	int elements_processed,
	unsigned long int_parm)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO

	zfcp_adapter_t *adapter;
	zfcp_qdio_queue_t *queue;

	ZFCP_LOG_TRACE(
		"enter (irq=%i status=%i qdio_error=%i siga_error=%i "
		"queue_number=%i first_element=%i elements_processed=%i "
		"int_parm=0x%lx)\n",
		irq,
		status,
		qdio_error,
		siga_error,
		queue_number,
		first_element,
		elements_processed,
		int_parm);

	adapter = (zfcp_adapter_t*)int_parm;
      	queue = &adapter->request_queue;

        ZFCP_LOG_DEBUG("devno=0x%04x, first=%d, count=%d\n",
                        adapter->devno,
                        first_element,
                        elements_processed);

	if (zfcp_qdio_handler_error_check(adapter, status, qdio_error, siga_error))
		goto out;        

	/* cleanup all SBALs being program-owned now */
	zfcp_zero_sbals(
	        queue->buffer, 
		first_element, 
		elements_processed);

	/* increase free space in outbound queue */
	atomic_add(elements_processed, &queue->free_count);
        ZFCP_LOG_DEBUG("free_count=%d\n",
                       atomic_read(&queue->free_count));
        wake_up(&adapter->request_wq);
	ZFCP_LOG_DEBUG(
		"Elements_processed = %d, free count=%d \n",
		elements_processed,
		atomic_read(&queue->free_count));

out: 
	ZFCP_LOG_TRACE("exit\n");

	return;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   	zfcp_qdio_response_handler
 *
 * purpose:	is called by QDIO layer for completed SBALs in response queue
 *
 * returns:	(void)
 */
void zfcp_qdio_response_handler(
	int irq,
	unsigned int status,
	unsigned int qdio_error,
	unsigned int siga_error,
	unsigned int queue_number,
	int first_element,
	int elements_processed,
	unsigned long int_parm)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO

	zfcp_adapter_t *adapter;
	zfcp_qdio_queue_t *queue;
	int buffer_index;
	int i;
	qdio_buffer_t *buffer;
	int retval = 0;
	u8 count;
	u8 start;
        volatile qdio_buffer_element_t *buffere=NULL;
	int buffere_index;

	ZFCP_LOG_TRACE(
		"enter (irq=0x%x status=0x%x qdio_error=0x%x siga_error=0x%x "
		"queue_number=%i first_element=%i elements_processed=%i "
		"int_parm=0x%lx)\n",
		irq,
		status,
		qdio_error,
		siga_error,
		queue_number,
		first_element,
		elements_processed,
		int_parm);
        
	adapter = (zfcp_adapter_t*)int_parm;
      	queue = &adapter->response_queue;

        if (zfcp_qdio_handler_error_check(adapter, status, qdio_error, siga_error))
                goto out;        

        buffere = &(queue->buffer[first_element]->element[0]);
        ZFCP_LOG_DEBUG("first BUFFERE flags=0x%x \n",
                       buffere->flags);
	/*
	 * go through all SBALs from input queue currently
	 * returned by QDIO layer
         */ 

	for (i = 0; i < elements_processed; i++) {

		buffer_index = first_element + i;
		buffer_index %= QDIO_MAX_BUFFERS_PER_Q;
		buffer = queue->buffer[buffer_index];

		/* go through all SBALEs of SBAL */
                for(buffere_index = 0;
                    buffere_index < QDIO_MAX_ELEMENTS_PER_BUFFER;
                    buffere_index++) {

			/* look for QDIO request identifiers in SB */
			buffere = &buffer->element[buffere_index];
			retval = zfcp_qdio_reqid_check(adapter, 
                                                       (void *)buffere->addr);

			if (retval) {
				ZFCP_LOG_NORMAL(
					"bug: Inbound packet seems not to have "
					"been sent at all. It will be ignored."
					"(debug info 0x%lx, 0x%lx, %d, %d, 0x%x)\n",
					(unsigned long)buffere->addr,
					(unsigned long)&(buffere->addr),
                                        first_element,
                                        elements_processed,
					adapter->devno);

				ZFCP_LOG_NORMAL(
					"Dump of inbound BUFFER %d BUFFERE %d "
					"at address 0x%lx\n",
					buffer_index,
                                        buffere_index, 
					(unsigned long)buffer);
				ZFCP_HEX_DUMP(
					ZFCP_LOG_LEVEL_NORMAL,
					(char*)buffer,
					SBAL_SIZE);
			}
                        if (buffere->flags & SBAL_FLAGS_LAST_ENTRY)
				break;                        
		};

                if (!(buffere->flags & SBAL_FLAGS_LAST_ENTRY)) {
			ZFCP_LOG_NORMAL("bug: End of inbound data not marked!\n");
                } 
	}

	/*
	 * put range of SBALs back to response queue
	 * (including SBALs which have already been free before)
	 */
	count = atomic_read(&queue->free_count) + elements_processed;
	start = queue->free_index;

        ZFCP_LOG_TRACE(
		"Calling do QDIO irq=0x%x,flags=0x%x, queue_no=%i, "
		"index_in_queue=%i, count=%i, buffers=0x%lx\n",
		irq,
		QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
		0,
		start,
		count,
		(unsigned long)&queue->buffer[start]);	

        retval = do_QDIO(
                        irq,
                        QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
                        0,
                        start,
                        count,
                        NULL);
        if (retval) {
		atomic_set(&queue->free_count, count);
                ZFCP_LOG_DEBUG(
			"Inbound data regions could not be cleared "
                        "Transfer queues may be down. "
                        "(info %d, %d, %d)\n",
			count,
			start,
			retval);
        } else  {
		queue->free_index += count;
		queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;
		atomic_set(&queue->free_count, 0);
                ZFCP_LOG_TRACE(
			"%i buffers successfully enqueued to response queue "
			"starting at position %i\n",
			count,
			start);
        }

out:
        /*
          ZFCP_LOG_DEBUG("response_queue->free_count=%i,response_queue->free_index=%i\n",
          atomic_read(&queue->free_count),
          queue->free_index) ;
        */
	ZFCP_LOG_TRACE("exit\n");
 
	return;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_qdio_reqid_check
 *
 * purpose:	checks for valid reqids or unsolicited status
 *
 * returns:	0 - valid request id or unsolicited status
 *		!0 - otherwise
 */
static inline int zfcp_qdio_reqid_check(zfcp_adapter_t *adapter, void *sbale_addr)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_QDIO

	zfcp_fsf_req_t *fsf_req;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (sbale_addr=0x%lx)\n",
		(unsigned long)sbale_addr);

#ifdef ZFCP_DEBUG_REQUESTS
        /* Note: seq is entered later */
        debug_text_event(adapter->req_dbf, 1, "i:a/seq");
        debug_event(adapter->req_dbf, 1, &sbale_addr, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

	/* invalid (per convention used in this driver) */
	if (!sbale_addr) {
		ZFCP_LOG_NORMAL(
			"bug: Inbound data faulty, contains null-pointer!\n");
		retval = -EINVAL;
		goto out;
        }

	/* valid request id and thus (hopefully :) valid fsf_req address */
	fsf_req = (zfcp_fsf_req_t*)sbale_addr;

	ZFCP_PARANOIA {
		if ((fsf_req->common_magic != ZFCP_MAGIC)
                    ||(fsf_req->specific_magic != ZFCP_MAGIC_FSFREQ)) {
			ZFCP_LOG_NORMAL(
				"bug: An inbound FSF acknowledgement was "
				"faulty (debug info 0x%x, 0x%x, 0x%lx)\n",
				fsf_req->common_magic,
				fsf_req->specific_magic,
				(unsigned long)fsf_req);
			retval = -EINVAL;
                        //                        panic("void of grace");
			goto out;
		}

		if (adapter != fsf_req->adapter) {
			ZFCP_LOG_NORMAL(
				"bug: An inbound FSF acknowledgement was not "
				"correct (debug info 0x%lx, 0x%lx, 0%lx) \n",
				(unsigned long)fsf_req,
				(unsigned long)fsf_req->adapter,
                                (unsigned long)adapter);
			retval = -EINVAL;
			goto out;
		}
	}

#ifdef ZFCP_DEBUG_REQUESTS
	/* debug feature stuff (test for QTCB: remember new unsol. status!) */
	if (fsf_req->qtcb) {
                debug_event(adapter->req_dbf, 1, &fsf_req->qtcb->prefix.req_seq_no,
                            sizeof(u32));
	}
#endif /* ZFCP_DEBUG_REQUESTS */

	ZFCP_LOG_TRACE(
		"fsf_req at 0x%lx, QTCB at 0x%lx\n",
		(unsigned long)fsf_req,
		(unsigned long)fsf_req->qtcb);
	if (fsf_req->qtcb) {
		ZFCP_LOG_TRACE("HEX DUMP OF 1ST BUFFERE PAYLOAD (QTCB):\n");
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(char*)fsf_req->qtcb,
			sizeof(fsf_qtcb_t));
	}

	/* finish the FSF request */
	zfcp_fsf_req_complete(fsf_req);
          
out:
	ZFCP_LOG_TRACE("exit \n");

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_activate_adapter
 *
 * purpose:
 *
 * returns:
 */
inline static void zfcp_activate_adapter(int irq)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_DIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_DIO

	int devno;
	zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE("enter (irq=%i)\n", irq);

	devno = get_devno_by_irq(irq);
	ZFCP_LOG_TRACE("devno is 0x%04x\n",devno);

	/* Find the new adapter and open it */
	ZFCP_FOR_EACH_ADAPTER(adapter) {
		if (adapter->devno == devno) {
			ZFCP_LOG_INFO(
				"The adapter with devno 0x%04x "
				"will now be activated.\n",
				devno);
			debug_text_event(adapter->erp_dbf,1,"activate");
			zfcp_erp_modify_adapter_status(
				adapter,
				ZFCP_STATUS_COMMON_RUNNING,
				ZFCP_SET);
			zfcp_erp_adapter_reopen(
				adapter,
				ZFCP_STATUS_COMMON_ERP_FAILED);
		}
	}
	if (!adapter)
		ZFCP_LOG_DEBUG(
			"An unconfigured adapter has become "
			"active, it's devno 0x%04x.\n",
			devno);

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_dio_oper_handler
 *
 * purpose:
 *
 * returns:
 */
static int zfcp_dio_oper_handler(int irq, devreg_t *dreg)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_DIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_DIO

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (irq=%i, dreg=0x%lx)\n",
		irq, (unsigned long)dreg);

        zfcp_activate_adapter(irq);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_dio_not_oper_handler
 *
 * purpose:
 *
 * returns:
 */
static void zfcp_dio_not_oper_handler(int irq,  int status)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_DIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_DIO
        
        zfcp_adapter_t *adapter;
        int known=0;

	ZFCP_LOG_TRACE(
		"enter (irq=%i, status=%i)\n",
		irq, status);

        ZFCP_FOR_EACH_ADAPTER(adapter) {
                if(atomic_test_mask(ZFCP_STATUS_ADAPTER_IRQOWNER, &adapter->status) &&
                   (adapter->irq==irq)) {
                        known=1;
                        break;
                }
        }

        switch (status) {
        case DEVSTAT_DEVICE_GONE:
                ZFCP_LOG_FLAGS(1,"DEVSTAT_DEVICE_GONE\n");
        case DEVSTAT_NOT_OPER:
                ZFCP_LOG_FLAGS(1,"DEVSTAT_NOT_OPER\n");
                if (!known) {
                        ZFCP_LOG_DEBUG("An unconfigured or an already "
                                       "disabled adapter became "
                                       "unoperational on irq 0x%x.\n",
                                       irq);
                        goto out;
                }
                ZFCP_LOG_INFO("The adapter with devno 0x%04x became "
                              "unoperational.\n",
                              adapter->devno);
                /* shut-down the adapter and wait for completion */
                debug_text_event(adapter->erp_dbf,1,"not_oper");
                zfcp_erp_adapter_shutdown(adapter, ZFCP_STATUS_ADAPTER_IRQOWNER);
                zfcp_erp_wait(adapter);
                break;
        case DEVSTAT_REVALIDATE:
                ZFCP_LOG_FLAGS(1,"DEVSTAT_REVALIDATE\n");
                /* The irq should still be that of the old adapter */
                if(known) {
                        ZFCP_LOG_INFO("The adapter with devno 0x%04x became "
                                      "unoperational.\n",
                                      adapter->devno);
                        /* shut-down the adapter and wait for completion */
                        /* Note: This adapter is not the real IRQ-owner anymore 
                         * The ERP strategy requires the IRQ to be freed somehow
                         * though 
                         */
                        debug_text_event(adapter->erp_dbf,1,"reval");
                        zfcp_erp_adapter_shutdown(adapter, 0);
                        zfcp_erp_wait(adapter);
                } else {
                        ZFCP_LOG_DEBUG("An unconfigured adapter was the "
                                       "origin of a VM define, it's irq 0x%x.\n",
                                       irq);
                }
                /* The new adapter already owns the irq and needs to be activated  */
                zfcp_activate_adapter(irq);
                break;
        default:
                ZFCP_LOG_NORMAL("bug: Common I/O layer presented information  "
                               "unknown to the zfcp module (debug info "
                               "0x%x, 0x%x)\n",
                               irq,
                               status);
        }      
 out:
        ZFCP_LOG_TRACE("exit\n");
	
        return;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_cio_handler
 *
 * purpose:
 *
 * returns:
 */
static void zfcp_cio_handler(int irq, void *devstat, struct pt_regs *rgs)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_DIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_DIO

	ZFCP_LOG_TRACE(
		"enter (irq=%i, devstat=0%lx, pt_regs=0%lx)\n",
		irq, (unsigned long)devstat,
		(unsigned long)rgs);
        ZFCP_LOG_DEBUG("Normally, this function would never be called. "
                       "(info 0x%x, 0x%lx, 0x%lx)\n",
                       irq,
                       (unsigned long)devstat,
                       (unsigned long)rgs);
	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_req_complete
 *
 * purpose:	Updates active counts and timers for openfcp-reqs
 *              May cleanup request after req_eval returns
 *
 * returns:	0 - success
 *		!0 - failure
 *
 * context:	
 */
static int zfcp_fsf_req_complete(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
        int cleanup;
        zfcp_adapter_t *adapter = fsf_req->adapter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	/* do some statistics */
	atomic_dec(&adapter->fsf_reqs_active);

	if (fsf_req->fsf_command == FSF_QTCB_UNSOLICITED_STATUS) {
                ZFCP_LOG_DEBUG("Status read response received\n");
                /* Note: all cleanup handling is done in the callchain of 
                   the function call-chain below.
                */
		zfcp_fsf_status_read_handler(fsf_req);
                goto out;
	} else	zfcp_fsf_protstatus_eval(fsf_req);

        /*
	 * fsf_req may be deleted due to waking up functions, so 
         * cleanup is saved here and used later 
         */
        if (fsf_req->status & ZFCP_STATUS_FSFREQ_CLEANUP) 
                cleanup = 1;
        else    cleanup = 0;        

        fsf_req->status |= ZFCP_STATUS_FSFREQ_COMPLETED;
        
        /* cleanup request if requested by initiator */
        if (cleanup) {
                ZFCP_LOG_TRACE(
			"removing FSF request 0x%lx\n",
			(unsigned long)fsf_req);
		/*
		 * lock must not be held here since it will be
		 * grabed by the called routine, too
		 */
		if (zfcp_fsf_req_cleanup(fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
				"(debug info 0x%lx).\n",
				(unsigned long)fsf_req);
		}
        } else {
                /* notify initiator waiting for the requests completion */
                ZFCP_LOG_TRACE(
                    "waking initiator of FSF request 0x%lx\n",
                    (unsigned long)fsf_req);
                wake_up(&fsf_req->completion_wq);
        }
        
 out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
        return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_protstatus_eval
 *
 * purpose:	evaluates the QTCB of the finished FSF request
 *		and initiates appropriate actions
 *		(usually calling FSF command specific handlers)
 *
 * returns:	
 *
 * context:	
 *
 * locks:
 */
static int zfcp_fsf_protstatus_eval(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
     
	int retval = 0;
	zfcp_adapter_t *adapter = fsf_req->adapter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	ZFCP_LOG_DEBUG(
		"QTCB is at 0x%lx\n",
		(unsigned long)fsf_req->qtcb);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		ZFCP_LOG_DEBUG(
			"fsf_req 0x%lx has been dismissed\n",
			(unsigned long)fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
				   ZFCP_STATUS_FSFREQ_RETRY;	/* only for SCSI cmnds. */
		zfcp_cmd_dbf_event_fsf("dismiss", fsf_req, NULL, 0);
		goto skip_protstatus;
	}

	/* log additional information provided by FSF (if any) */
	if (fsf_req->qtcb->header.log_length) {
		/* do not trust them ;-) */
		if (fsf_req->qtcb->header.log_start > sizeof(fsf_qtcb_t)) {
			ZFCP_LOG_NORMAL(
				"bug: ULP (FSF logging) log data starts "
                                "beyond end of packet header. Ignored. "
				"(start=%i, size=%li)\n",
				fsf_req->qtcb->header.log_start,
				sizeof(fsf_qtcb_t));
			goto forget_log;
		}
		if ((size_t)(fsf_req->qtcb->header.log_start +
		     fsf_req->qtcb->header.log_length)
		    > sizeof(fsf_qtcb_t)) {
			ZFCP_LOG_NORMAL(
				"bug: ULP (FSF logging) log data ends "
                                "beyond end of packet header. Ignored. "
				"(start=%i, length=%i, size=%li)\n",
				fsf_req->qtcb->header.log_start,
				fsf_req->qtcb->header.log_length,
				sizeof(fsf_qtcb_t));
			goto forget_log;
		}
		ZFCP_LOG_TRACE("ULP log data: \n");
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(char*)fsf_req->qtcb + fsf_req->qtcb->header.log_start,
			fsf_req->qtcb->header.log_length);
	}
forget_log:

	/* evaluate FSF Protocol Status */
	switch (fsf_req->qtcb->prefix.prot_status) {

		case FSF_PROT_GOOD :
			ZFCP_LOG_TRACE("FSF_PROT_GOOD\n");
			break;

		case FSF_PROT_FSF_STATUS_PRESENTED :
			ZFCP_LOG_TRACE("FSF_PROT_FSF_STATUS_PRESENTED\n");
			break;

		case FSF_PROT_QTCB_VERSION_ERROR :
			ZFCP_LOG_FLAGS(0, "FSF_PROT_QTCB_VERSION_ERROR\n");
			/* DEBUG */
			ZFCP_LOG_NORMAL(
				"fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long)fsf_req,
				(unsigned long)fsf_req->qtcb,
				((unsigned long)fsf_req) & 0xFFFFFF00,
				(unsigned long)((zfcp_fsf_req_t*)(((unsigned long)fsf_req) & 0xFFFFFF00))->qtcb);
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_NORMAL,
				(char*)(((unsigned long)fsf_req) & 0xFFFFFF00),
				sizeof(zfcp_fsf_req_t));
			ZFCP_LOG_NORMAL(
				"error: The adapter with devno 0x%04x contains "
                                "microcode of version 0x%x, the device driver "
                                "only supports 0x%x. Aborting.\n",
				adapter->devno,
				fsf_req->qtcb->prefix.prot_status_qual.version_error.fsf_version,
				ZFCP_QTCB_VERSION);
			/* stop operation for this adapter */
                        debug_text_exception(adapter->erp_dbf,0,"prot_ver_err");
                        zfcp_erp_adapter_shutdown(adapter, 0);
			zfcp_cmd_dbf_event_fsf(
				"qverserr", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_PROT_SEQ_NUMB_ERROR :
			ZFCP_LOG_FLAGS(0, "FSF_PROT_SEQ_NUMB_ERROR\n");
			ZFCP_LOG_NORMAL(
				"bug: Sequence number mismatch between "
				"driver (0x%x) and adapter of devno 0x%04x "
				"(0x%x). Restarting all operations on this "
                                "adapter.\n",
				fsf_req->qtcb->prefix.req_seq_no,
                                adapter->devno,
				fsf_req->qtcb->prefix.prot_status_qual.sequence_error.exp_req_seq_no);
#ifdef ZFCP_DEBUG_REQUESTS
                        debug_text_event(adapter->req_dbf, 1, "exp_seq!");
                        debug_event(adapter->req_dbf, 1, &fsf_req->qtcb->prefix.prot_status_qual.sequence_error.exp_req_seq_no, 4);
                        debug_text_event(adapter->req_dbf, 1, "qtcb_seq!");
                        debug_exception(adapter->req_dbf, 1, &fsf_req->qtcb->prefix.req_seq_no, 4);
#endif /* ZFCP_DEBUG_REQUESTS */
                        debug_text_exception(adapter->erp_dbf,0,"prot_seq_err");
			/* restart operation on this adapter */
                        zfcp_erp_adapter_reopen(adapter,0);
			zfcp_cmd_dbf_event_fsf(
				"seqnoerr", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_PROT_UNSUPP_QTCB_TYPE :
			ZFCP_LOG_FLAGS(0, "FSF_PROT_UNSUP_QTCB_TYPE\n");
			ZFCP_LOG_NORMAL("error: Packet header type used by the "
                                        "device driver is incompatible with "
                                        "that used on the adapter with devno "
                                        "0x%04x. "
                                        "Stopping all operations on this adapter.\n", 
                                        adapter->devno);
			ZFCP_LOG_NORMAL(
				"fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long)fsf_req,
				(unsigned long)fsf_req->qtcb,
				((unsigned long)fsf_req) & 0xFFFFFF00,
				(unsigned long)((zfcp_fsf_req_t*)(((unsigned long)fsf_req) & 0xFFFFFF00))->qtcb);
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_NORMAL,
				(char*)(((unsigned long)fsf_req) & 0xFFFFFF00),
				sizeof(zfcp_fsf_req_t));
                        debug_text_exception(adapter->erp_dbf,0,"prot_unsup_qtcb");
                        zfcp_erp_adapter_shutdown(adapter, 0);
			zfcp_cmd_dbf_event_fsf(
				"unsqtcbt", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_PROT_HOST_CONNECTION_INITIALIZING :
			ZFCP_LOG_FLAGS(1, "FSF_PROT_HOST_CONNECTION_INITIALIZING\n");
			zfcp_cmd_dbf_event_fsf(
				"hconinit", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT, 
                                        &(adapter->status));
                        debug_text_event(adapter->erp_dbf,4,"prot_con_init");
			break;

		case FSF_PROT_DUPLICATE_REQUEST_ID :
			ZFCP_LOG_FLAGS(0, "FSF_PROT_DUPLICATE_REQUEST_IDS\n");
			if (fsf_req->qtcb) {
				ZFCP_LOG_NORMAL(
					"bug: The request identifier 0x%Lx "
                                	"to the adapter with devno 0x%04x is " 
					"ambiguous. "
                                        "Stopping all operations on this adapter.\n",
        	                        *(llui_t*)(&fsf_req->qtcb->bottom.support.req_handle),
					adapter->devno);
			} else	{
				ZFCP_LOG_NORMAL(
					"bug: The request identifier 0x%lx "
                                	"to the adapter with devno 0x%04x is " 
					"ambiguous. "
                                        "Stopping all operations on this adapter. "
                                        "(bug: got this for an unsolicited "
					"status read request)\n",
					(unsigned long)fsf_req,
					adapter->devno);
			}
                        debug_text_exception(adapter->erp_dbf,0,"prot_dup_id");
                        zfcp_erp_adapter_shutdown(adapter, 0);
			zfcp_cmd_dbf_event_fsf(
				"dupreqid", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                        
		case FSF_PROT_LINK_DOWN :
			ZFCP_LOG_FLAGS(1, "FSF_PROT_LINK_DOWN\n");
			/*
			 * 'test and set' is not atomic here -
			 * it's ok as long as calls to our response queue handler
			 * (and thus execution of this code here) are serialized
			 * by the qdio module
			 */
			if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED, 
                                               &adapter->status)) {
				switch (fsf_req->qtcb->prefix.prot_status_qual.locallink_error.code) {
				case FSF_PSQ_LINK_NOLIGHT :
					ZFCP_LOG_INFO(
						"The local link to the adapter with "
						"devno 0x%04x is down"
						"(no light detected).\n",
						adapter->devno);
					break;
				case FSF_PSQ_LINK_WRAPPLUG :
					ZFCP_LOG_INFO(
						"The local link to the adapter with "
						"devno 0x%04x is down"
						"(wrap plug detected).\n",
						adapter->devno);
					break;
				case FSF_PSQ_LINK_NOFCP :
					ZFCP_LOG_INFO(
						"The local link to the adapter with "
						"devno 0x%04x is down"
						"(the adjacent node on the link "
						"does not support FCP).\n",
						adapter->devno);
					break;
				default :
					ZFCP_LOG_INFO(
						"The local link to the adapter with "
						"devno 0x%04x is down"
						"(warning: unknown reason code).\n",
						adapter->devno);
					break;

				}
				/*
				 * Due to the 'erp failed' flag the adapter won't
				 * be recovered but will be just set to 'blocked'
				 * state. All subordinary devices will have state
				 * 'blocked' and 'erp failed', too.
				 * Thus the adapter is still able to provide
				 * 'link up' status without being flooded with
				 * requests.
				 * (note: even 'close port' is not permitted)
				 */
				ZFCP_LOG_INFO(
					"Stopping all operations for the adapter "
					"with devno 0x%04x.\n",
					adapter->devno);
				atomic_set_mask(
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					&adapter->status);
				zfcp_erp_adapter_reopen(adapter, 0);
                                debug_text_event(adapter->erp_dbf,1,"prot_link_down");
                        }
			zfcp_cmd_dbf_event_fsf(
				"linkdown", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_PROT_REEST_QUEUE :
			ZFCP_LOG_FLAGS(1, "FSF_PROT_REEST_QUEUE\n"); 
                        debug_text_event(adapter->erp_dbf,1,"prot_reest_queue");
                        ZFCP_LOG_INFO("The local link to the adapter with "
                                      "devno 0x%04x was re-plugged. "
                                      "Re-starting operations on this adapter.\n", 
                                      adapter->devno);
                        /* All ports should be marked as ready to run again */
                        zfcp_erp_modify_adapter_status(
                                       adapter,
                                       ZFCP_STATUS_COMMON_RUNNING,
                                       ZFCP_SET);
                        zfcp_erp_adapter_reopen(
                                       adapter, 
                                       ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
                                       | ZFCP_STATUS_COMMON_ERP_FAILED);
			zfcp_cmd_dbf_event_fsf(
				"reestque", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        break;
			
		case FSF_PROT_ERROR_STATE :
			ZFCP_LOG_FLAGS(0, "FSF_PROT_ERROR_STATE\n");
			ZFCP_LOG_NORMAL(
				"error: The adapter with devno 0x%04x "
				"has entered the error state. "
                                "Restarting all operations on this "
                                "adapter.\n",
                                adapter->devno);
                        debug_text_event(adapter->erp_dbf,0,"prot_err_sta");
			/* restart operation on this adapter */
                        zfcp_erp_adapter_reopen(adapter,0);
			zfcp_cmd_dbf_event_fsf(
				"proterrs", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: Transfer protocol status information "
				"provided by the adapter with devno 0x%04x "
                                "is not compatible with the device driver. "
                                "Stopping all operations on this adapter. "
                                "(debug info 0x%x).\n",
                                adapter->devno,
				fsf_req->qtcb->prefix.prot_status);
			ZFCP_LOG_NORMAL(
				"fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long)fsf_req,
				(unsigned long)fsf_req->qtcb,
				((unsigned long)fsf_req) & 0xFFFFFF00,
				(unsigned long)((zfcp_fsf_req_t*)(((unsigned long)fsf_req) & 0xFFFFFF00))->qtcb);
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_NORMAL,
				(char*)(((unsigned long)fsf_req) & 0xFFFFFF00),
				sizeof(zfcp_fsf_req_t));
                        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL, (char *)fsf_req->qtcb, sizeof(fsf_qtcb_t));
                        debug_text_event(adapter->erp_dbf,0,"prot_inval:");
                        debug_exception(adapter->erp_dbf,0,
                                        &fsf_req->qtcb->prefix.prot_status,
                                        sizeof(u32));
                        //                        panic("it was pity");
                        zfcp_erp_adapter_shutdown(adapter, 0);
			zfcp_cmd_dbf_event_fsf(
				"undefps", fsf_req,
				&fsf_req->qtcb->prefix.prot_status_qual, sizeof(fsf_prot_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
	}
        
 skip_protstatus:
	/*
	 * always call specific handlers to give them a chance to do
	 * something meaningful even in error cases
	 */
	zfcp_fsf_fsfstatus_eval(fsf_req);
        
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_fsf_fsfstatus_eval
 *
 * purpose:	evaluates FSF status of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int zfcp_fsf_fsfstatus_eval(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		goto skip_fsfstatus;
	}

	/* evaluate FSF Status */
	switch (fsf_req->qtcb->header.fsf_status) {
		case FSF_UNKNOWN_COMMAND :
			ZFCP_LOG_FLAGS(0, "FSF_UNKNOWN_COMMAND\n");
			ZFCP_LOG_NORMAL("bug: Command issued by the device driver is "
                                        "not known by the adapter with devno 0x%04x "
                                        "Stopping all operations on this adapter. "
                                        "(debug info 0x%x).\n",
                                        fsf_req->adapter->devno,
                                        fsf_req->qtcb->header.fsf_command);
                        debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_s_unknown");
                        zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
			zfcp_cmd_dbf_event_fsf(
				"unknownc", fsf_req,
				&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
                        
                case FSF_FCP_RSP_AVAILABLE :
                        ZFCP_LOG_FLAGS(2, "FSF_FCP_RSP_AVAILABLE\n");
                        ZFCP_LOG_DEBUG("FCP Sense data will be presented to the "
                                      "SCSI stack.\n");
                        debug_text_event(fsf_req->adapter->erp_dbf,4,"fsf_s_rsp");
                        break;

		case FSF_ADAPTER_STATUS_AVAILABLE :
			ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                        debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_astatus");
                        zfcp_fsf_fsfstatus_qual_eval(fsf_req);
                        break;

		default :
			break;
	}

skip_fsfstatus:
	/*
         * always call specific handlers to give them a chance to do
         * something meaningful even in error cases
         */ 
	zfcp_fsf_req_dispatch(fsf_req);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/*
 * function:	zfcp_fsf_fsfstatus_qual_eval
 *
 * purpose:	evaluates FSF status-qualifier of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int zfcp_fsf_fsfstatus_qual_eval(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

        switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
        case FSF_SQ_FCP_RSP_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_SQ_FCP_RSP_AVAILABLE\n");
                debug_text_event(fsf_req->adapter->erp_dbf,4,"fsf_sq_rsp");
                break;
        case FSF_SQ_RETRY_IF_POSSIBLE :
                ZFCP_LOG_FLAGS(2, "FSF_SQ_RETRY_IF_POSSIBLE\n");
                /* The SCSI-stack may now issue retries or escalate */
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_sq_retry");
		zfcp_cmd_dbf_event_fsf(
			"sqretry", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
        case FSF_SQ_COMMAND_ABORTED :
                ZFCP_LOG_FLAGS(2, "FSF_SQ_COMMAND_ABORTED\n");
		/* Carry the aborted state on to upper layer */
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_sq_abort");
		zfcp_cmd_dbf_event_fsf(
			"sqabort", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTED;
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
        case FSF_SQ_NO_RECOM :
                ZFCP_LOG_FLAGS(0, "FSF_SQ_NO_RECOM\n");
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_sq_no_rec");
                ZFCP_LOG_NORMAL("bug: No recommendation could be given for a"
                                "problem on the adapter with devno 0x%04x "
                                "Stopping all operations on this adapter. ",
                                fsf_req->adapter->devno);
                zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"sqnrecom", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
        case FSF_SQ_ULP_PROGRAMMING_ERROR :
                ZFCP_LOG_FLAGS(0, "FSF_SQ_ULP_PROGRAMMING_ERROR\n");
                ZFCP_LOG_NORMAL("bug: An illegal amount of data was attempted "
                                "to be sent to the adapter with devno 0x%04x "
                                "Stopping all operations on this adapter. ",
                                fsf_req->adapter->devno);
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_sq_ulp_err");
                zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"squlperr", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
        case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
        case FSF_SQ_NO_RETRY_POSSIBLE :
        case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                /* dealt with in the respective functions */
                break;
        default:
                ZFCP_LOG_NORMAL("bug: Additional status info could "
                                "not be interpreted properly.\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                debug_exception(fsf_req->adapter->erp_dbf,0,
                                &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                sizeof(u32));
		zfcp_cmd_dbf_event_fsf(
			"squndef", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_fsf_req_dispatch
 *
 * purpose:	calls the appropriate command specific handler
 *
 * returns:	
 */
static int zfcp_fsf_req_dispatch(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		ZFCP_LOG_TRACE(
			"fsf_req=0x%lx, QTCB=0x%lx\n",
			(unsigned long)fsf_req,
			(unsigned long)(fsf_req->qtcb));
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(char *)fsf_req->qtcb,
			sizeof(fsf_qtcb_t));
	}

	switch (fsf_req->fsf_command) {

		case FSF_QTCB_FCP_CMND :
			ZFCP_LOG_FLAGS(3, "FSF_QTCB_FCP_CMND\n");
			zfcp_fsf_send_fcp_command_handler(fsf_req);
			break;

		case FSF_QTCB_ABORT_FCP_CMND :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_ABORT_FCP_CMND\n");
			zfcp_fsf_abort_fcp_command_handler(fsf_req);
			break;

		case FSF_QTCB_SEND_GENERIC :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_SEND_GENERIC\n");
			zfcp_fsf_send_ct_handler(fsf_req);
			break;

		case FSF_QTCB_OPEN_PORT_WITH_DID :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_PORT_WITH_DID\n");
			zfcp_fsf_open_port_handler(fsf_req);
			break;

		case FSF_QTCB_OPEN_LUN :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_LUN\n");
			zfcp_fsf_open_unit_handler(fsf_req);
			break;

		case FSF_QTCB_CLOSE_LUN :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_LUN\n");
			zfcp_fsf_close_unit_handler(fsf_req);
			break;

		case FSF_QTCB_CLOSE_PORT :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PORT\n");
			zfcp_fsf_close_port_handler(fsf_req);
			break;

                case FSF_QTCB_CLOSE_PHYSICAL_PORT :
                        ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PHYSICAL_PORT\n");
                        zfcp_fsf_close_physical_port_handler(fsf_req);
                        break;

		case FSF_QTCB_EXCHANGE_CONFIG_DATA :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_EXCHANGE_CONFIG_DATA\n");
			zfcp_fsf_exchange_config_data_handler(fsf_req);
                        break;

		case FSF_QTCB_SEND_ELS :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_SEND_ELS\n");
			zfcp_fsf_send_els_handler(fsf_req);
			break;

		default :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_UNKNOWN\n");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			ZFCP_LOG_NORMAL(
				"bug: Command issued by the device driver is "
                                "not supported by the adapter with devno 0x%04x "
                                "(debug info 0x%lx 0x%x).\n",
                                fsf_req->adapter->devno,
				(unsigned long)fsf_req,
				fsf_req->fsf_command);
			if (fsf_req->fsf_command !=
			    fsf_req->qtcb->header.fsf_command)
                                ZFCP_LOG_NORMAL(
                                     "bug: Command issued by the device driver differs "
                                     "from the command returned by the adapter with devno "
                                     "0x%04x (debug info 0x%x, 0x%x).\n",
                                     fsf_req->adapter->devno,
                                     fsf_req->fsf_command,
                                     fsf_req->qtcb->header.fsf_command);
	}

        zfcp_erp_fsf_req_handler(fsf_req);
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_status_read
 *
 * purpose:	initiates a Status Read command at the specified adapter
 *
 * returns:
 */
static int zfcp_fsf_status_read(
	zfcp_adapter_t *adapter,
	int req_flags)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	zfcp_fsf_req_t *fsf_req;
	fsf_status_read_buffer_t *status_buffer;
	unsigned long lock_flags;
	volatile qdio_buffer_element_t *sbale;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx, req_flags=0x%x)\n", 
		(unsigned long)adapter,
		req_flags);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			adapter,
			FSF_QTCB_UNSOLICITED_STATUS,
			req_flags | ZFCP_REQ_USE_MEMPOOL,
                        &adapter->pool.fsf_req_status_read,
			&lock_flags,
			&fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
			"unsolicited status buffer for "
			"the adapter with devno 0x%04x.\n",
			adapter->devno);
                goto failed_req_create;
        }

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
        sbale[2].flags |= SBAL_FLAGS_LAST_ENTRY;
        fsf_req->sbale_curr = 2;

	status_buffer = zfcp_mem_pool_find(&adapter->pool.data_status_read);
	if (!status_buffer) {
		ZFCP_LOG_NORMAL("bug: could not get some buffer\n");
		goto failed_buf;
	}
	fsf_req->data.status_read.buffer = status_buffer;

	/* insert pointer to respective buffer */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->addr = (void *)status_buffer;
	sbale->length = sizeof(fsf_status_read_buffer_t);

	/* start QDIO request for this FSF request */
        retval = zfcp_fsf_req_send(fsf_req, NULL);
        if (retval) {
                ZFCP_LOG_DEBUG(
                        "error: Could not set-up unsolicited status "
                        "environment.\n");
                goto failed_req_send;
        }

        ZFCP_LOG_TRACE(
                "Status Read request initiated "
                "(adapter devno=0x%04x)\n",
                adapter->devno);
#ifdef ZFCP_DEBUG_REQUESTS
        debug_text_event(adapter->req_dbf, 1, "unso");
#endif

	goto out;

failed_req_send:
	zfcp_mem_pool_return(status_buffer, &adapter->pool.data_status_read);

failed_buf:
	if (zfcp_fsf_req_free(fsf_req)) {
		ZFCP_LOG_NORMAL(
			"bug: Could not remove one FSF "
			"request. Memory leakage possible. "
			"(debug info 0x%lx).\n",
			(unsigned long)fsf_req);
	};

failed_req_create:
out:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
        
	ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_fsf_status_read_port_closed(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	fsf_status_read_buffer_t *status_buffer = fsf_req->data.status_read.buffer;
	zfcp_adapter_t *adapter = fsf_req->adapter;
	unsigned long flags;
	zfcp_port_t *port;

	write_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT (adapter, port)
		if (port->d_id == (status_buffer->d_id & ZFCP_DID_MASK))
			break;
	write_unlock_irqrestore(&adapter->port_list_lock, flags);

	if (!port) {
		ZFCP_LOG_NORMAL(
			"bug: Re-open port indication received for the "
			"non-existing port with DID 0x%06x, on the adapter "
			"with devno 0x%04x. Ignored.\n",
			status_buffer->d_id & ZFCP_DID_MASK,
			adapter->devno);
		goto out;
	}

	switch (status_buffer->status_subtype) {

		case FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT:
			ZFCP_LOG_FLAGS(2, "FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT\n");
			debug_text_event(adapter->erp_dbf, 3, "unsol_pc_phys:");
			zfcp_erp_port_reopen(port, 0);
			break;

		case FSF_STATUS_READ_SUB_ERROR_PORT:
			ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_SUB_ERROR_PORT\n");
			debug_text_event(adapter->erp_dbf, 1, "unsol_pc_err:");
			zfcp_erp_port_shutdown(port, 0);
			break;

		default:
			debug_text_event(adapter->erp_dbf, 0, "unsol_unk_sub:");
			debug_exception(
				adapter->erp_dbf, 0,
				&status_buffer->status_subtype, sizeof(u32));
			ZFCP_LOG_NORMAL(
				"bug: Undefined status subtype received "
				"for a re-open indication on the port with "
				"DID 0x%06x, on the adapter with devno "
				"0x%04x. Ignored. (debug info 0x%x)\n",
				status_buffer->d_id,
				adapter->devno,
				status_buffer->status_subtype);
	}

out:
	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_status_read_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int zfcp_fsf_status_read_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
        zfcp_adapter_t *adapter = fsf_req->adapter;
        fsf_status_read_buffer_t *status_buffer = fsf_req->data.status_read.buffer; 

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		zfcp_mem_pool_return(status_buffer, &adapter->pool.data_status_read);
                if (zfcp_fsf_req_cleanup(fsf_req)) {
                        ZFCP_LOG_NORMAL("bug: Could not remove one FSF "
                                        "request. Memory leakage possible. "
                                        "(debug info 0x%lx).\n",
                                        (unsigned long)fsf_req);
                }
		goto out;
        }

	switch (status_buffer->status_type) {

	case FSF_STATUS_READ_PORT_CLOSED:
                ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_PORT_CLOSED\n");
                debug_text_event(adapter->erp_dbf,3,"unsol_pclosed:");
                debug_event(adapter->erp_dbf,3,
                            &status_buffer->d_id,
                            sizeof(u32));
		zfcp_fsf_status_read_port_closed(fsf_req);
		break;

	case FSF_STATUS_READ_INCOMING_ELS:
                ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_INCOMING_ELS\n");
                debug_text_event(adapter->erp_dbf,3,"unsol_els:");
                zfcp_fsf_incoming_els(fsf_req);
		break;

        case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
                ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_BIT_ERROR_THRESHOLD\n");
                debug_text_event(adapter->erp_dbf,3,"unsol_bit_err:");
                ZFCP_LOG_NORMAL("Bit error threshold data received:\n");
                ZFCP_HEX_DUMP(
                              ZFCP_LOG_LEVEL_NORMAL, 
                              (char*)status_buffer,
                              sizeof(fsf_status_read_buffer_t));
		break;
                
        case FSF_STATUS_READ_LINK_DOWN:
                ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_LINK_DOWN\n");
		debug_text_event(adapter->erp_dbf, 0, "unsol_link_down:");
		ZFCP_LOG_INFO(
			"Local link to adapter with devno 0x%04x is down\n",
			adapter->devno);
		atomic_set_mask(
			ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
			&adapter->status);
		zfcp_erp_adapter_failed(adapter);
                break;
                

	case FSF_STATUS_READ_CFDC_UPDATED:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_CFDC_UPDATED\n");
		debug_text_event(adapter->erp_dbf, 2, "unsol_cfdc_upd:");
		ZFCP_LOG_NORMAL(
			"CFDC has been updated on the FCP adapter "
			"(devno=0x%04x)\n",
			adapter->devno);
		break;
                 
	case FSF_STATUS_READ_CFDC_HARDENED:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_CFDC_HARDENED\n");
		debug_text_event(adapter->erp_dbf, 2, "unsol_cfdc_harden:");
		switch (status_buffer->status_subtype) {
		case FSF_STATUS_READ_SUB_CFDC_HARDENED_ON_SE:
			ZFCP_LOG_NORMAL(
				"CFDC has been saved on the SE "
				"(devno=0x%04x)\n",
				adapter->devno);
			break;
		case FSF_STATUS_READ_SUB_CFDC_HARDENED_ON_SE2:
			ZFCP_LOG_NORMAL(
				"CFDC has been copied to the secondary SE "
				"(devno=0x%04x)\n",
				adapter->devno);
			break;
		default:
			ZFCP_LOG_NORMAL(
				"CFDC has been hardened on the FCP adapter "
				"(devno=0x%04x)\n",
				adapter->devno);
		}
		break;

        case FSF_STATUS_READ_LINK_UP:
                ZFCP_LOG_FLAGS(1,"FSF_STATUS_READ_LINK_UP\n");
                debug_text_event(adapter->erp_dbf,2,"unsol_link_up:");
                ZFCP_LOG_INFO("The local link to the adapter with "
                              "devno 0x%04x was re-plugged. "
                              "Re-starting operations on this adapter..\n", 
                              adapter->devno);
                /* All ports should be marked as ready to run again */
                zfcp_erp_modify_adapter_status(
                                  adapter,
                                  ZFCP_STATUS_COMMON_RUNNING,
                                  ZFCP_SET);
                zfcp_erp_adapter_reopen(
                                  adapter, 
                                  ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
                                  | ZFCP_STATUS_COMMON_ERP_FAILED);
                break;
                
	default:
                debug_text_event(adapter->erp_dbf,0,"unsol_unknown:");
                debug_exception(adapter->erp_dbf,0,
                                &status_buffer->status_type,
                                sizeof(u32));
		ZFCP_LOG_NORMAL("bug: An unsolicited status packet of unknown "
                               "type was received by the zfcp-driver "
                               "(debug info 0x%x)\n",
                                status_buffer->status_type);
                ZFCP_LOG_DEBUG("Dump of status_read_buffer 0x%lx:\n", 
                              (unsigned long)status_buffer);
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG, 
                              (char*)status_buffer,
                              sizeof(fsf_status_read_buffer_t));
                break;
	}

	zfcp_mem_pool_return(status_buffer, &adapter->pool.data_status_read);
        if (zfcp_fsf_req_cleanup(fsf_req)) {
                ZFCP_LOG_NORMAL("bug: Could not remove one FSF "
                                "request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)fsf_req);
        }
        /* recycle buffer and start new request 
         * repeat until outbound queue is empty or adapter shutdown is requested*/

        /* FIXME(qdio) - we may wait in the req_create for 5s during shutdown, so 
           qdio_cleanup will have to wait at least that long before returning with
           failure to allow us a proper cleanup under all circumstances
        */
	/* FIXME: allocation failure possible? (Is this code needed?) */
	retval = zfcp_fsf_status_read(adapter, 0);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"Outbound queue busy. "
			"Could not create use an "
			"unsolicited status read request for "
			"the adapter with devno 0x%04x.\n",
			adapter->devno);
		/* temporary fix to avoid status read buffer shortage */
		adapter->status_read_failed++;
		if ((ZFCP_STATUS_READS_RECOM - adapter->status_read_failed)
		    < ZFCP_STATUS_READ_FAILED_THRESHOLD) {
			ZFCP_LOG_INFO(
				"restart adapter due to status read "
				"buffer shortage (devno 0x%04x)\n",
				adapter->devno);
				zfcp_erp_adapter_reopen(adapter, 0);
		}
	}

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_fsf_incoming_els_rscn(
		zfcp_adapter_t *adapter,
		fsf_status_read_buffer_t *status_buffer) 
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
	fcp_rscn_head_t *fcp_rscn_head
		= (fcp_rscn_head_t *) status_buffer->payload;
        fcp_rscn_element_t *fcp_rscn_element
		= (fcp_rscn_element_t *) status_buffer->payload;

        unsigned long flags;
        zfcp_port_t *port;
        int i;
        int known=0;
        int no_notifications=0;
        int range_mask=0;
        int reopen_unknown=0;
        /* see FC-FS */
        int no_entries=(fcp_rscn_head->payload_len / 4);

	zfcp_in_els_dbf_event(adapter, "##rscn", status_buffer, fcp_rscn_head->payload_len);

        for (i=1; i < no_entries; i++) {
                /* skip head and start with 1st element */
                fcp_rscn_element++;
                switch (fcp_rscn_element->addr_format) {
                case ZFCP_PORT_ADDRESS:
                        ZFCP_LOG_FLAGS(1,"ZFCP_PORT_ADDRESS\n");
                        range_mask=ZFCP_PORTS_RANGE_PORT;
                        no_notifications=1;
                        break;
                case ZFCP_AREA_ADDRESS:
                        ZFCP_LOG_FLAGS(1,"ZFCP_AREA_ADDRESS\n");
			/* skip head and start with 1st element */
                        range_mask=ZFCP_PORTS_RANGE_AREA;
                        no_notifications = ZFCP_NO_PORTS_PER_AREA;
                        break;
                case ZFCP_DOMAIN_ADDRESS:
                        ZFCP_LOG_FLAGS(1,"ZFCP_DOMAIN_ADDRESS\n");
                        range_mask=ZFCP_PORTS_RANGE_DOMAIN;
                        no_notifications = ZFCP_NO_PORTS_PER_DOMAIN;
                        break;
                case ZFCP_FABRIC_ADDRESS:
                        ZFCP_LOG_FLAGS(1,"ZFCP_FABRIC_ADDRESS\n");
                        range_mask=ZFCP_PORTS_RANGE_FABRIC;
                        no_notifications = ZFCP_NO_PORTS_PER_FABRIC;
                        break;
                }
                known=0;
                write_lock_irqsave(&adapter->port_list_lock, flags);
                ZFCP_FOR_EACH_PORT (adapter, port) {
                        if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status))
                                continue;
                        if(((u32)port->d_id & range_mask) 
                           == (u32)(fcp_rscn_element->nport_did & range_mask)) {
                                known++;
#if 0
                                printk("known=%d, reopen did 0x%x\n",
                                       known,
                                       fcp_rscn_element->nport_did);
#endif
				debug_text_event(adapter->erp_dbf,1,"unsol_els_rscnk:");
				zfcp_test_link(port);
                        }
                }
                write_unlock_irqrestore(&adapter->port_list_lock, flags);
#if 0
                printk("known %d, no_notifications %d\n",
                       known, no_notifications);
#endif
                if(known<no_notifications) {
                        ZFCP_LOG_DEBUG("At least one unknown port changed state. "
                                      "Unknown ports need to be reopened.\n");
                        reopen_unknown=1;
                }
        } // for (i=1; i < no_entries; i++)
        
        if(reopen_unknown) {
                ZFCP_LOG_DEBUG("At least one unknown did "
                              "underwent a state change.\n");
                write_lock_irqsave(&adapter->port_list_lock, flags);
                ZFCP_FOR_EACH_PORT (adapter, port) {
			if (atomic_test_mask(ZFCP_STATUS_PORT_NAMESERVER, &port->status))
				continue;
			if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status)) {
                                ZFCP_LOG_INFO("Received state change notification."
                                                "Trying to open the port with WWPN "
                                                "0x%016Lx. Hope it's there now.\n",
                                                (llui_t)port->wwpn);
				debug_text_event(adapter->erp_dbf,1,"unsol_els_rscnu:");
                                zfcp_erp_port_reopen(port, 
                                                     ZFCP_STATUS_COMMON_ERP_FAILED);
                        }
                }
                write_unlock_irqrestore(&adapter->port_list_lock, flags);
        }

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_fsf_incoming_els_plogi(
		zfcp_adapter_t *adapter,
		fsf_status_read_buffer_t *status_buffer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	logi *els_logi = (logi*) status_buffer->payload;
	zfcp_port_t *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##plogi", status_buffer, 28);

	write_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (port->wwpn == (*(wwn_t *)&els_logi->nport_wwn))
			break;
	}
	write_unlock_irqrestore(&adapter->port_list_lock, flags);

	if (!port) {
		ZFCP_LOG_DEBUG(
			"Re-open port indication received "
			"for the non-existing port with D_ID "
			"0x%06x, on the adapter with devno "
			"0x%04x. Ignored.\n",
			status_buffer->d_id,
			adapter->devno);
	} else	{
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_plogi:");
		debug_event(adapter->erp_dbf, 1, &els_logi->nport_wwn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_fsf_incoming_els_logo(
		zfcp_adapter_t *adapter,
		fsf_status_read_buffer_t *status_buffer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	fcp_logo_t *els_logo = (fcp_logo_t*) status_buffer->payload;
	zfcp_port_t *port;
	unsigned long flags;

	zfcp_in_els_dbf_event(adapter, "##logo", status_buffer, 16);

	write_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (port->wwpn == els_logo->nport_wwpn)
			break;
	}
	write_unlock_irqrestore(&adapter->port_list_lock, flags);

	if (!port) {
		ZFCP_LOG_DEBUG(
			"Re-open port indication received "
			"for the non-existing port with D_ID "
			"0x%06x, on the adapter with devno "
			"0x%04x. Ignored.\n",
			status_buffer->d_id,
			adapter->devno);
	} else	{
		debug_text_event(adapter->erp_dbf, 1, "unsol_els_logo:");
		debug_event(adapter->erp_dbf, 1, &els_logo->nport_wwpn, 8);
		zfcp_erp_port_forced_reopen(port, 0);
	}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_fsf_incoming_els_unknown(
		zfcp_adapter_t *adapter,
		fsf_status_read_buffer_t *status_buffer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	zfcp_in_els_dbf_event(adapter, "##undef", status_buffer, 24);
	ZFCP_LOG_NORMAL(
		"warning: Unknown incoming ELS (0x%x) received "
		"for the adapter with devno 0x%04x\n",
		*(u32*)(status_buffer->payload),
		adapter->devno);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


void zfcp_fsf_incoming_els(zfcp_fsf_req_t *fsf_req) 
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	fsf_status_read_buffer_t *status_buffer = fsf_req->data.status_read.buffer; 
	u32 els_type = *(u32*)(status_buffer->payload);
	zfcp_adapter_t *adapter = fsf_req->adapter;

	if (els_type == LS_PLOGI)
        	zfcp_fsf_incoming_els_plogi(adapter, status_buffer);
	else if (els_type == LS_LOGO)
		zfcp_fsf_incoming_els_logo(adapter, status_buffer);
	else if ((els_type & 0xffff0000) == LS_RSCN)
		/* we are only concerned with the command, not the length */
		zfcp_fsf_incoming_els_rscn(adapter, status_buffer);
	else	zfcp_fsf_incoming_els_unknown(adapter, status_buffer);

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_start_scsi_er_timer
 *
 * purpose:     sets up the timer to watch over SCSI error recovery
 *              actions and starts it
 *
 */
static void zfcp_fsf_start_scsi_er_timer(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
	ZFCP_LOG_TRACE("enter (adapter=0x%lx\n",
                       (unsigned long)adapter);
        adapter->scsi_er_timer.function = 
                zfcp_fsf_scsi_er_timeout_handler;
        adapter->scsi_er_timer.data = 
                (unsigned long)adapter;
        adapter->scsi_er_timer.expires = 
                jiffies + ZFCP_SCSI_ER_TIMEOUT;
        add_timer(&adapter->scsi_er_timer);

        ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/*
 * function:    zfcp_fsf_abort_fcp_command
 *
 * purpose:	tells FSF to abort a running SCSI command
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 *
 * FIXME(design) shouldn't this be modified to return an int
 *               also...don't know how though
 */
static zfcp_fsf_req_t * zfcp_fsf_abort_fcp_command(
		unsigned long old_req_id,
		zfcp_adapter_t *adapter,
		zfcp_unit_t *unit,
		int req_flags)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	volatile qdio_buffer_element_t *sbale;
	zfcp_fsf_req_t *fsf_req = NULL;
	int retval = 0;
	unsigned long lock_flags;
 
	ZFCP_LOG_TRACE(
		"enter (old_req_id=0x%lx, adapter=0x%lx, "
		"unit=0x%lx, req_flags=0x%x)\n",
		old_req_id,
		(unsigned long)adapter,
		(unsigned long)unit,
		req_flags);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			adapter,
			FSF_QTCB_ABORT_FCP_CMND,
			req_flags,
                        &adapter->pool.fsf_req_scsi,
			&lock_flags,
			&fsf_req);
	if (retval < 0) {
                ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
                        "abort command request on the device with "
                        "the FCP_LUN 0x%016Lx connected to "
                        "the port with WWPN 0x%016Lx connected to "
                        "the adapter with devno 0x%04x.\n",
                        (llui_t)unit->fcp_lun,
                        (llui_t)unit->port->wwpn,
                        adapter->devno);
		goto out;
	}
        
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	fsf_req->data.abort_fcp_command.unit = unit;

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* set handle of request which should be aborted */
        fsf_req->qtcb->bottom.support.req_handle = (u64)old_req_id;

#if 0
	/* DEBUG */
	goto out;
#endif

	/* start QDIO request for this FSF request */
        
        zfcp_fsf_start_scsi_er_timer(adapter);
        retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
                del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO(
			"error: Could not send an abort command request "
			"for a command on the adapter with devno 0x%04x, "
                        "port WWPN 0x%016Lx and unit FCP_LUN 0x%016Lx\n",
			adapter->devno,
			(llui_t)unit->port->wwpn,
			(llui_t)unit->fcp_lun);
		if (zfcp_fsf_req_free(fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)fsf_req);
                };
		fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG(
		"Abort FCP Command request initiated "
		"(adapter devno=0x%04x, port D_ID=0x%06x, "
		"unit FCP_LUN=0x%016Lx, old_req_id=0x%lx)\n",
		adapter->devno,
		unit->port->d_id,
		(llui_t)unit->fcp_lun,
		old_req_id);
	
out:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_DEBUG("exit (0x%lx)\n", (unsigned long)fsf_req);
 
	return fsf_req;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_abort_fcp_command_handler
 *
 * purpose:	is called for finished Abort FCP Command request
 *
 * returns:	
 */
static int zfcp_fsf_abort_fcp_command_handler(
		zfcp_fsf_req_t *new_fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
	zfcp_unit_t *unit = new_fsf_req->data.abort_fcp_command.unit;
        unsigned char status_qual = new_fsf_req->qtcb->header.fsf_status_qual.word[0];

	ZFCP_LOG_TRACE(
		"enter (new_fsf_req=0x%lx)\n",
		(unsigned long)new_fsf_req);

        del_timer(&new_fsf_req->adapter->scsi_er_timer);

	if (new_fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}
     
	/* evaluate FSF status in QTCB */
	switch (new_fsf_req->qtcb->header.fsf_status) {
                
        case FSF_PORT_HANDLE_NOT_VALID :
                if(status_qual>>4 != status_qual%0xf) {
                        ZFCP_LOG_FLAGS(2, "FSF_PORT_HANDLE_NOT_VALID\n");
                        debug_text_event(new_fsf_req->adapter->erp_dbf,3,"fsf_s_phand_nv0");
                        /* In this case a command that was sent prior to a port
                         * reopen was aborted (handles are different). This is fine.
                         */
                } else {
                        ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
                        ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
                                        "for the port with WWPN 0x%016Lx connected to "
                                        "the adapter of devno 0x%04x is "
                                        "not valid. This may happen occasionally.\n",
                                        unit->port->handle,
                                        (llui_t)unit->port->wwpn,
                                        unit->port->adapter->devno);
                        ZFCP_LOG_INFO("status qualifier:\n");
                        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
                                      (char*)&new_fsf_req->qtcb->header.fsf_status_qual,
                                      16);
                        /* Let's hope this sorts out the mess */
                        debug_text_event(new_fsf_req->adapter->erp_dbf,1,"fsf_s_phand_nv1");
                        zfcp_erp_adapter_reopen(unit->port->adapter, 0);
                        new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                }
                break;
                
        case FSF_LUN_HANDLE_NOT_VALID :
                if(status_qual>>4 != status_qual%0xf) {
                        /* 2 */
                        ZFCP_LOG_FLAGS(0, "FSF_LUN_HANDLE_NOT_VALID\n");
                        debug_text_event(new_fsf_req->adapter->erp_dbf,3,"fsf_s_lhand_nv0");
                        /* In this case a command that was sent prior to a unit
                         * reopen was aborted (handles are different). This is fine.
                         */
                } else {
                        ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
                        ZFCP_LOG_INFO("Warning: Temporary LUN identifier (handle) 0x%x "
                                      "of the logical unit with FCP_LUN 0x%016Lx at "
                                      "the remote port with WWPN 0x%016Lx connected "
                                      "to the adapter with devno 0x%04x is " 
                                      "not valid. This may happen in rare cases."
                                      "Trying to re-establish link.\n",
                                      unit->handle,
                                      (llui_t)unit->fcp_lun,
                                      (llui_t)unit->port->wwpn,
                                      unit->port->adapter->devno);
                        ZFCP_LOG_DEBUG("Status qualifier data:\n");
                        ZFCP_HEX_DUMP(
                                      ZFCP_LOG_LEVEL_DEBUG,
                                      (char*)&new_fsf_req->qtcb->header.fsf_status_qual,
                                      16);
                        /* Let's hope this sorts out the mess */
                        debug_text_event(new_fsf_req->adapter->erp_dbf,1,"fsf_s_lhand_nv1");
                        zfcp_erp_port_reopen(unit->port, 0);
                        new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                }
                break;

        case FSF_FCP_COMMAND_DOES_NOT_EXIST :
                ZFCP_LOG_FLAGS(2, "FSF_FCP_COMMAND_DOES_NOT_EXIST\n");
                retval = 0;
#ifdef ZFCP_DEBUG_REQUESTS
                  /* debug feature area which records fsf request sequence numbers */
	        debug_text_event(new_fsf_req->adapter->req_dbf, 3, "no_exist");
	        debug_event(new_fsf_req->adapter->req_dbf, 3, 
        	            &new_fsf_req->qtcb->bottom.support.req_handle,
	                    sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
	        debug_text_event(new_fsf_req->adapter->erp_dbf,3,"fsf_s_no_exist");
                new_fsf_req->status
                        |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
                break;
                
        case FSF_PORT_BOXED :
                /* 2 */
                ZFCP_LOG_FLAGS(0, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened\n",
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno);
                debug_text_event(new_fsf_req->adapter->erp_dbf,2,"fsf_s_pboxed");
                zfcp_erp_port_reopen(unit->port, 0);
                new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;
                        
        case FSF_ADAPTER_STATUS_AVAILABLE :
                /* 2 */
                ZFCP_LOG_FLAGS(0, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (new_fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        debug_text_event(new_fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                        /* reopening link to port */
                        zfcp_erp_port_reopen(unit->port, 0);
                        new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                        /* SCSI stack will escalate */
                        debug_text_event(new_fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                        new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                default:
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
                                        new_fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        debug_text_event(new_fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                        debug_exception(new_fsf_req->adapter->erp_dbf,0,
                                        &new_fsf_req->qtcb->header.fsf_status_qual.word[0],
                                        sizeof(u32));
                        break;
                }
                break;
                
        case FSF_GOOD :
                /* 3 */
                ZFCP_LOG_FLAGS(2, "FSF_GOOD\n");
                retval = 0;
                new_fsf_req->status
                        |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
                break;
                
        default :
                ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
                                "(debug info 0x%x)\n",
                                new_fsf_req->qtcb->header.fsf_status);
                debug_text_event(new_fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                debug_exception(new_fsf_req->adapter->erp_dbf,0,
                                &new_fsf_req->qtcb->header.fsf_status,
                                sizeof(u32));
                break;
	}
        
 skip_fsfstatus:

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
     
	return retval;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_nameserver_enqueue
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_nameserver_enqueue(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	zfcp_port_t *port;

	ZFCP_LOG_TRACE("enter\n");

	/* generate port structure */
	retval = zfcp_port_enqueue(
			adapter,
			0,
			0,
			ZFCP_STATUS_PORT_NAMESERVER,
                        &port);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not establish a connection to the "
                        "fabric name server connected to the "
			"adapter with devno 0x%04x\n",
			adapter->devno);
		goto out;
	}
	/* set special D_ID */
	port->d_id = ZFCP_DID_NAMESERVER;
        /* enter nameserver port into adapter struct */
        adapter->nameserver_port=port;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 *
 */
static void zfcp_gid_pn_buffers_free(struct zfcp_gid_pn_data *gid_pn)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	ZFCP_LOG_TRACE("enter\n");
        if ((gid_pn->ct.pool != 0)) {
		zfcp_mem_pool_return(gid_pn, gid_pn->ct.pool);
        } else {
                ZFCP_KFREE(gid_pn, sizeof(struct zfcp_gid_pn_data));
        }

	ZFCP_LOG_TRACE("exit\n");
	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 *
 */
static int zfcp_gid_pn_buffers_alloc(struct zfcp_gid_pn_data **gid_pn,
                                     zfcp_mem_pool_t *pool)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

#ifdef ZFCP_MEM_POOL_ONLY
	*gid_pn = NULL;
#else
	*gid_pn = ZFCP_KMALLOC(sizeof(struct zfcp_gid_pn_data), GFP_KERNEL);
#endif
	if ((*gid_pn == 0) && (pool != 0))
		*gid_pn = zfcp_mem_pool_find(pool);

        if (*gid_pn == 0)
                return -ENOMEM;

        (*gid_pn)->ct.req = &(*gid_pn)->req;
        (*gid_pn)->ct.resp = &(*gid_pn)->resp;
	(*gid_pn)->ct.req_count = (*gid_pn)->ct.resp_count = 1;
        (*gid_pn)->req.address = (char *) &(*gid_pn)->ct_iu_req;
        (*gid_pn)->resp.address = (char *) &(*gid_pn)->ct_iu_resp;
        (*gid_pn)->req.length = sizeof(struct ct_iu_ns_req);
        (*gid_pn)->resp.length = sizeof(struct ct_iu_gid_pn);

	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 *
 */
static int zfcp_ns_gid_pn_request(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        zfcp_adapter_t *adapter = erp_action->adapter;
        struct zfcp_gid_pn_data *gid_pn = 0;
        struct ct_iu_ns_req *ct_iu_req;
	int retval = 0;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);
	if (!adapter->nameserver_port) {
		ZFCP_LOG_NORMAL("bug: no nameserver available\n");
		retval = -EINVAL;
		goto out;
	}

        retval = zfcp_gid_pn_buffers_alloc(&gid_pn, &adapter->pool.data_gid_pn);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of memory. Could not allocate "
                              "buffers for nameserver request GID_PN. "
                              "(adapter: 0x%04x)\n", adapter->devno);
		goto out;
	}

	/* setup nameserver request */
        ct_iu_req = (struct ct_iu_ns_req *) gid_pn->ct.req->address;
        ct_iu_req->header.revision = ZFCP_CT_REVISION;
        ct_iu_req->header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        ct_iu_req->header.gs_subtype = ZFCP_CT_NAME_SERVER;
        ct_iu_req->header.options = ZFCP_CT_SYNCHRONOUS;
        ct_iu_req->header.cmd_rsp_code = ZFCP_CT_GID_PN;
        ct_iu_req->header.max_res_size = ZFCP_CT_MAX_SIZE;
	ct_iu_req->data.wwpn = erp_action->port->wwpn;

        /* setup parameters for send generic command */
        gid_pn->ct.port = adapter->nameserver_port;
	gid_pn->ct.handler = zfcp_ns_gid_pn_handler;
	gid_pn->ct.handler_data = (unsigned long) erp_action;
        gid_pn->ct.timeout = ZFCP_NS_GID_PN_TIMEOUT;
        gid_pn->ct.timer = &erp_action->timer;
        erp_action->data.gid_pn = gid_pn;

	retval = zfcp_fsf_send_ct(&gid_pn->ct,
                                  &erp_action->adapter->pool.fsf_req_erp,
                                  erp_action);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send nameserver request GID_PN "
                              "via adapter with devno 0x%04x\n",
                              adapter->devno);
                zfcp_gid_pn_buffers_free(gid_pn);
                erp_action->data.gid_pn = 0;
	}

 out:
	ZFCP_LOG_TRACE("exit (%d)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/**
 * 
 */
static void zfcp_ns_gid_pn_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        zfcp_erp_action_t *erp_action = (zfcp_erp_action_t *) data;
	zfcp_port_t *port = erp_action->port;
        struct zfcp_send_ct *ct = &erp_action->data.gid_pn->ct;
	struct ct_iu_ns_req *ct_iu_req =
                (struct ct_iu_ns_req *) ct->req->address;
	struct ct_iu_gid_pn *ct_iu_resp =
                (struct ct_iu_gid_pn *) ct->resp->address;

	ZFCP_LOG_TRACE("enter\n");

        if (ct_iu_resp->header.revision != ZFCP_CT_REVISION)
		goto failed;
        if (ct_iu_resp->header.gs_type != ZFCP_CT_DIRECTORY_SERVICE)
		goto failed;
        if (ct_iu_resp->header.gs_subtype != ZFCP_CT_NAME_SERVER)
		goto failed;
        if (ct_iu_resp->header.options != ZFCP_CT_SYNCHRONOUS)
		goto failed;
        if (ct_iu_resp->header.cmd_rsp_code != ZFCP_CT_ACCEPT) {
		/* FIXME: do we need some specific erp entry points */
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto failed;
	}
	/* paranoia */
	if (ct_iu_req->data.wwpn != port->wwpn) {
		ZFCP_LOG_NORMAL(
			"bug: Port WWPN returned by nameserver lookup "
                        "does not correspond to "
                        "the expected value on the adapter with devno 0x%04x. "
			"(debug info 0x%016Lx, 0x%016Lx)\n",
                        port->adapter->devno,
			(llui_t)port->wwpn,
                        (llui_t)ct_iu_req->data.wwpn);
		goto failed;
	}

	/* looks like a valid d_id */
        port->d_id = ZFCP_DID_MASK & ct_iu_resp->d_id;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
	ZFCP_LOG_DEBUG(
		"devno 0x%04x:  WWPN=0x%016Lx ---> D_ID=0x%06x\n",
		port->adapter->devno,
		(llui_t)port->wwpn,
		port->d_id);
	goto out;

failed:
	ZFCP_LOG_NORMAL(
		"warning: WWPN 0x%016Lx not found by nameserver lookup "
		"using the adapter with devno 0x%04x\n", 
		(llui_t)port->wwpn,
		port->adapter->devno);
	ZFCP_LOG_DEBUG("CT IUs do not match:\n");
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)ct_iu_req,
		sizeof(struct ct_iu_ns_req));
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)ct_iu_resp,
		sizeof(struct ct_iu_gid_pn));

out:
        zfcp_gid_pn_buffers_free(erp_action->data.gid_pn);
        erp_action->data.gid_pn = 0;
	ZFCP_LOG_TRACE("exit\n");
	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/**
 * FIXME: document
 * FIXME: check for FS_RJT IU and set appropriate return code
 */
int zfcp_ns_ga_nxt_request(zfcp_port_t *port, struct ct_iu_ga_nxt *ct_iu_resp)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        struct ct_iu_ns_req *ct_iu_req;
        struct zfcp_send_ct *ct;
        zfcp_adapter_t *adapter = port->adapter;
	int ret = 0;

	DECLARE_COMPLETION(wait);

        memset(ct_iu_resp, 0, sizeof(*ct_iu_resp));

	ZFCP_LOG_TRACE("enter\n");

	if (!adapter->nameserver_port) {
		ZFCP_LOG_NORMAL("bug: no nameserver available\n");
		ret = -EINVAL;
		goto out;
	}

        if ((ct_iu_req =
             ZFCP_KMALLOC(sizeof(struct ct_iu_ns_req), GFP_KERNEL)) == 0) {
                ZFCP_LOG_INFO("error: Out of memory. Unable to create "
                              "CT request (FC-GS), adapter devno 0x%04x.\n",
                              adapter->devno);
                ret = -ENOMEM;
                goto out;
        }

        if ((ct =
             ZFCP_KMALLOC(sizeof(struct zfcp_send_ct), GFP_KERNEL)) == 0) {
                ZFCP_LOG_INFO("error: Out of memory. Unable to create "
                              "CT request (FC-GS), adapter devno 0x%04x.\n",
                              adapter->devno);
                ret = -ENOMEM;
                goto free_ct_iu_req;
        }

        if ((ct->req =
             ZFCP_KMALLOC(sizeof(struct scatterlist), GFP_KERNEL)) == 0) {
                ZFCP_LOG_INFO("error: Out of memory. Unable to create "
                              "CT request (FC-GS), adapter devno 0x%04x.\n",
                              adapter->devno);
                ret = -ENOMEM;
                goto free_ct;
        }

        if ((ct->resp =
             ZFCP_KMALLOC(sizeof(struct scatterlist), GFP_KERNEL)) == 0) {
                ZFCP_LOG_INFO("error: Out of memory. Unable to create "
                              "CT request (FC-GS), adapter devno 0x%04x.\n",
                              adapter->devno);
                ret = -ENOMEM;
                goto free_req;
        }

	/* setup nameserver request */
        ct_iu_req->header.revision = ZFCP_CT_REVISION;
        ct_iu_req->header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        ct_iu_req->header.gs_subtype = ZFCP_CT_NAME_SERVER;
        ct_iu_req->header.options = ZFCP_CT_SYNCHRONOUS;
        ct_iu_req->header.cmd_rsp_code = ZFCP_CT_GA_NXT;
        ct_iu_req->header.max_res_size = ZFCP_CT_MAX_SIZE;
	ct_iu_req->data.d_id = ZFCP_DID_MASK & (port->d_id - 1);

	ct->completion = &wait;
        ct->req->address = (char *) ct_iu_req;
        ct->resp->address = (char *) ct_iu_resp;
        ct->req->length = sizeof(*ct_iu_req);
        ct->resp->length = sizeof(*ct_iu_resp);
        ct->req_count = ct->resp_count = 1;

        /* setup parameters for send generic command */
        ct->port = adapter->nameserver_port;
	ct->handler = zfcp_ns_ga_nxt_handler;
	ct->handler_data = (unsigned long) ct;

        ct->timeout = ZFCP_NS_GA_NXT_TIMEOUT;

	ret = zfcp_fsf_send_ct(ct, NULL, NULL);
	if (ret) {
		ZFCP_LOG_INFO("error: Could not send nameserver request GA_NXT "
                              "via adapter with devno 0x%04x\n",
                              adapter->devno);
                goto free_resp;
	}
        wait_for_completion(&wait);
        ret = ct->status;

 free_resp:
        ZFCP_KFREE(ct->resp, sizeof(struct scatterlist));
 free_req:
        ZFCP_KFREE(ct->req, sizeof(struct scatterlist));
 free_ct:
        ZFCP_KFREE(ct, sizeof(struct zfcp_send_ct));
 free_ct_iu_req:
        ZFCP_KFREE(ct_iu_req, sizeof(struct ct_iu_ns_req));
 out:
	ZFCP_LOG_TRACE("exit (%d)\n", ret);
	return ret;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * FIXME: document
 * FIXME: check for FS_RJT IU and return appropriate status
 */
static void zfcp_ns_ga_nxt_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

        struct zfcp_send_ct *ct = (struct zfcp_send_ct *) data;
	struct ct_iu_ns_req *ct_iu_req =
                (struct ct_iu_ns_req *) ct->req[0].address;
	struct ct_iu_ga_nxt *ct_iu_resp =
                (struct ct_iu_ga_nxt *) ct->resp[0].address;

	ZFCP_LOG_TRACE("enter\n");

        if (ct_iu_resp->header.revision != ZFCP_CT_REVISION)
		goto failed;
        if (ct_iu_resp->header.gs_type != ZFCP_CT_DIRECTORY_SERVICE)
		goto failed;
        if (ct_iu_resp->header.gs_subtype != ZFCP_CT_NAME_SERVER)
		goto failed;
        if (ct_iu_resp->header.options != ZFCP_CT_SYNCHRONOUS)
		goto failed;
        if (ct_iu_resp->header.cmd_rsp_code != ZFCP_CT_ACCEPT)
		goto failed;

	goto out;

failed:
        ct->status = -EIO;
	ZFCP_LOG_DEBUG("CT IU headers do not match:\n");
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)ct_iu_req,
		sizeof(struct ct_iu_ns_req));
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)ct_iu_resp,
		sizeof(struct ct_iu_gid_pn));
out:
	if (ct->completion != NULL) {
                complete(ct->completion);
        }
	ZFCP_LOG_TRACE("exit\n");
	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * checks whether req buffer and resp bother fit into one SBALE each
 */
static inline int
zfcp_use_one_sbal(struct scatterlist *req, int req_count,
                  struct scatterlist *resp, int resp_count)
{
        return ((req_count == 1) && (resp_count == 1) &&
                (((unsigned long) req[0].address & PAGE_MASK) ==
                 ((unsigned long) (req[0].address +
                                   req[0].length - 1) & PAGE_MASK)) &&
                (((unsigned long) resp[0].address & PAGE_MASK) ==
                 ((unsigned long) (resp[0].address +
                                  resp[0].length - 1) & PAGE_MASK)));
}

/**
 * FIXME: doc
 */
int zfcp_fsf_send_ct(struct zfcp_send_ct *ct, zfcp_mem_pool_t *pool,
                     zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
	volatile qdio_buffer_element_t *sbale;
	zfcp_port_t *port = ct->port;
	zfcp_adapter_t *adapter = port->adapter;
        zfcp_fsf_req_t *fsf_req;
        unsigned long lock_flags;
        int bytes;

	ZFCP_LOG_TRACE("enter\n");

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_GENERIC,
                                     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                                     pool, &lock_flags, &fsf_req);
	if (retval < 0) {
                ZFCP_LOG_INFO("error: Out of resources. "
                              "Could not create a CT request (FC-GS), "
                              "destination port D_ID is 0x%06x "
                              "at the adapter with devno 0x%04x.\n",
                              ct->port->d_id, adapter->devno);
		goto failed_req;
	}

        if (erp_action != 0) {
                erp_action->fsf_req = fsf_req;
                fsf_req->erp_action = erp_action;
        }
                
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        if (zfcp_use_one_sbal(ct->req, ct->req_count,
                              ct->resp, ct->resp_count)){
                /* both request buffer and response buffer
                   fit into one sbale each */
                sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
                sbale[2].addr = ct->req[0].address;
                sbale[2].length = ct->req[0].length;
                sbale[3].addr = ct->resp[0].address;
                sbale[3].length = ct->resp[0].length;
                sbale[3].flags |= SBAL_FLAGS_LAST_ENTRY;
        } else if (adapter->supported_features &
                   FSF_FEATURE_ELS_CT_CHAINED_SBALS) {
                /* try to use chained SBALs */
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                ct->req, ct->req_count,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: Out of resources (outbuf). "
                                      "Could not create a CT request (FC-GS), "
                                      "destination port D_ID is 0x%06x "
                                      "at the adapter with devno 0x%04x.\n",
                                      ct->port->d_id, adapter->devno);
                        if (bytes == 0) {
                                retval = -ENOMEM;
                        } else {
                                retval = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.req_buf_length = bytes;
                fsf_req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                ct->resp, ct->resp_count,
                                                ZFCP_MAX_SBALS_PER_CT_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: Out of resources (inbuf). "
                                      "Could not create a CT request (FC-GS), "
                                      "destination port D_ID is 0x%06x "
                                      "at the adapter with devno 0x%04x.\n",
                                      ct->port->d_id, adapter->devno);
                        if (bytes == 0) {
                                retval = -ENOMEM;
                        } else {
                                retval = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.resp_buf_length = bytes;
        } else {
                /* reject send generic request */
		ZFCP_LOG_INFO(
			"error: microcode does not support chained SBALs."
                        "CT request (FC-GS) too big."
                        "Destination port D_ID is 0x%06x "
			"at the adapter with devno 0x%04x.\n",
			port->d_id, adapter->devno);
                retval = -EOPNOTSUPP;
                goto failed_send;
        }

	/* settings in QTCB */
	fsf_req->qtcb->header.port_handle = port->handle;
	fsf_req->qtcb->bottom.support.service_class = adapter->fc_service_class;
	fsf_req->qtcb->bottom.support.timeout = ct->timeout;
        fsf_req->data.send_ct = ct;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, ct->timer);
	if (retval) {
		ZFCP_LOG_DEBUG("error: Out of resources. Could not send a "
                               "generic services command via adapter with "
                               "devno 0x%04x, port WWPN 0x%016Lx\n",
                               adapter->devno,	(llui_t) port->wwpn);
		goto failed_send;
	} else {
                ZFCP_LOG_DEBUG("Send Generic request initiated "
                               "(adapter devno=0x%04x, port D_ID=0x%06x)\n",
                               adapter->devno, port->d_id);
                goto out;
        }

 failed_send:
	if (zfcp_fsf_req_free(fsf_req)) {
                ZFCP_LOG_NORMAL("bug: Could not remove one FSF request. Memory "
                                "leakage possible. (debug info 0x%lx).\n",
                                (unsigned long)fsf_req);
                retval = -EINVAL;
	};
        if (erp_action != 0) {
                erp_action->fsf_req = NULL;
        }
 failed_req:
 out:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock,
                                     lock_flags);
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_ct_handler
 *
 * purpose:	is called for finished Send Generic request
 *
 * returns:	
 */
static int zfcp_fsf_send_ct_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
	zfcp_port_t *port = fsf_req->data.send_ct->port;
	fsf_qtcb_header_t *header = &fsf_req->qtcb->header;
	u16 subtable, rule, counter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

        case FSF_PORT_HANDLE_NOT_VALID :
                ZFCP_LOG_FLAGS(1,"FSF_PORT_HANDLE_NOT_VALID\n");
                ZFCP_LOG_DEBUG("Temporary port identifier (handle) 0x%x "
				"for the port with WWPN 0x%016Lx connected to "
                                "the adapter of devno 0x%04x is "
				"not valid. This may happen occasionally.\n",
				port->handle,
				(llui_t)port->wwpn,
                                port->adapter->devno);
                ZFCP_LOG_INFO("status qualifier:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_phandle_nv");
                zfcp_erp_adapter_reopen(port->adapter, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                
        case FSF_SERVICE_CLASS_NOT_SUPPORTED :
                ZFCP_LOG_FLAGS(0, "FSF_SERVICE_CLASS_NOT_SUPPORTED\n");
                if(fsf_req->adapter->fc_service_class <= 3) {
                        ZFCP_LOG_NORMAL("error: The adapter with devno=0x%04x does "
                                        "not support fibre-channel class %d.\n",
                                        port->adapter->devno,
                                        fsf_req->adapter->fc_service_class);
                } else {
                        ZFCP_LOG_NORMAL( "bug: The fibre channel class at the adapter "
                                        "with devno 0x%04x is invalid. "
                                        "(debug info %d)\n",
                                        port->adapter->devno,
                                        fsf_req->adapter->fc_service_class);
                }
                /* stop operation for this adapter */
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_s_class_nsup");
                zfcp_erp_adapter_shutdown(port->adapter, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

	case FSF_ACCESS_DENIED :
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot send generic command "
				"(devno=0x%04x wwpn=0x%016Lx)\n",
				port->adapter->devno,
				(llui_t)port->wwpn);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_GENERIC_COMMAND_REJECTED :
                ZFCP_LOG_FLAGS(1,"FSF_GENERIC_COMMAND_REJECTED\n");
                ZFCP_LOG_INFO("warning: The port with WWPN 0x%016Lx connected to "
                              "the adapter of devno 0x%04x has "
                              "rejected a generic services command.\n",
                              (llui_t)port->wwpn,
                              port->adapter->devno);
                ZFCP_LOG_INFO("status qualifier:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_gcom_rej");
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

	case FSF_REQUEST_BUF_NOT_VALID :
		ZFCP_LOG_FLAGS(1, "FSF_REQUEST_BUF_NOT_VALID\n");
		ZFCP_LOG_NORMAL(
			"error: The port with WWPN 0x%016Lx connected to "
			"the adapter of devno 0x%04x has "
			"rejected a generic services command "
			"due to invalid request buffer.\n",
			(llui_t)port->wwpn,
			port->adapter->devno);
		debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_reqiv");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_RESPONSE_BUF_NOT_VALID :
		ZFCP_LOG_FLAGS(1, "FSF_RESPONSE_BUF_NOT_VALID\n");
		ZFCP_LOG_NORMAL(
			"error: The port with WWPN 0x%016Lx connected to "
			"the adapter of devno 0x%04x has "
			"rejected a generic services command "
			"due to invalid response buffer.\n",
			(llui_t)port->wwpn,
			port->adapter->devno);
		debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_resiv");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_BOXED :
                ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened\n",
                               (llui_t)port->wwpn,
                               port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_pboxed");
                zfcp_erp_port_reopen(port, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;
                        
        case FSF_ADAPTER_STATUS_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        /* reopening link to port */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
			zfcp_test_link(port);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        /* ERP strategy will escalate */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                
                default:
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
                                        fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        break;
                }
                break;
                
        case FSF_GOOD :
                ZFCP_LOG_FLAGS(2,"FSF_GOOD\n");
                retval = 0;
                break;

        default :
                ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
                                "(debug info 0x%x)\n",
                                fsf_req->qtcb->header.fsf_status);          
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                debug_exception(fsf_req->adapter->erp_dbf,0,
                                &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                sizeof(u32));
                break;
	}

skip_fsfstatus:
	/* callback */
	if (fsf_req->data.send_ct->handler != 0) {
                (fsf_req->data.send_ct->handler)
                        (fsf_req->data.send_ct->handler_data);
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
     
	return retval;
     
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_els_handler
 *
 * purpose:     Handler for the Send ELS FSF requests
 *
 * returns:     0       - FSF request processed successfuly
 *              -EINVAL - FSF status is not 0
 */
static int zfcp_fsf_send_els_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	zfcp_adapter_t *adapter = fsf_req->adapter;
	zfcp_port_t *port = fsf_req->data.send_els->port;
	fsf_qtcb_header_t *header = &fsf_req->qtcb->header;
	fsf_qtcb_bottom_support_t *bottom = &fsf_req->qtcb->bottom.support;
	struct zfcp_send_els *send_els = fsf_req->data.send_els;
	u16 subtable, rule, counter;
	int retval = 0;

	ZFCP_LOG_TRACE("enter (fsf_req=0x%lx)\n", (unsigned long)fsf_req);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR)
		goto skip_fsfstatus;

	switch (header->fsf_status) {

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(2, "FSF_GOOD\n");
		ZFCP_LOG_INFO(
			"The FSF request has been successfully completed "
			"(devno=0x%04x fsf_req.seq_no=%d)\n",
			adapter->devno,
			fsf_req->seq_no);
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_FLAGS(2, "FSF_SERVICE_CLASS_NOT_SUPPORTED\n");
		if (adapter->fc_service_class <= 3) {
			ZFCP_LOG_INFO(
				"error: The adapter with devno=0x%04x does "
				"not support fibre-channel class %d\n",
				adapter->devno,
				adapter->fc_service_class);
		} else {
			ZFCP_LOG_INFO(
				"bug: The fibre channel class at the adapter "
				"with devno 0x%04x is invalid "
				"(debug info %d)\n",
				adapter->devno,
				adapter->fc_service_class);
		}
		debug_text_exception(adapter->erp_dbf, 0, "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	case FSF_ACCESS_DENIED:
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot send ELS "
				"(devno=0x%04x wwpn=0x%016Lx)\n",
				adapter->devno,
				(llui_t)port->wwpn);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	case FSF_ELS_COMMAND_REJECTED:
		ZFCP_LOG_FLAGS(2, "FSF_ELS_COMMAND_REJECTED\n");
		ZFCP_LOG_INFO(
			"The ELS command has been rejected because "
			"a command filter in the FCP channel prohibited "
			"sending of the ELS to the SAN "
			"(devno=0x%04x wwpn=0x%016Lx)\n",
			adapter->devno,
			(llui_t)port->wwpn);
		retval = -EINVAL;
		break;

	case FSF_PAYLOAD_SIZE_MISMATCH:
		ZFCP_LOG_FLAGS(2, "FSF_PAYLOAD_SIZE_MISMATCH\n");
		ZFCP_LOG_INFO(
			"ELS request size and ELS response size must be either "
			"both 0, or both greater than 0 "
			"(devno=0x%04x req_buf_length=%d resp_buf_length=%d)\n",
			adapter->devno,
			bottom->req_buf_length,
			bottom->resp_buf_length);
		retval = -EINVAL;
		break;

	case FSF_REQUEST_SIZE_TOO_LARGE:
		ZFCP_LOG_FLAGS(2, "FSF_REQUEST_SIZE_TOO_LARGE\n");
		ZFCP_LOG_INFO(
			"Length of the ELS request buffer, "
			"specified in QTCB bottom, "
			"exceeds the size of the buffers "
			"that have been allocated for ELS request data "
			"(devno=0x%04x req_buf_length=%d)\n",
			adapter->devno,
			bottom->req_buf_length);
		retval = -EINVAL;
		break;

	case FSF_RESPONSE_SIZE_TOO_LARGE:
		ZFCP_LOG_FLAGS(2, "FSF_RESPONSE_SIZE_TOO_LARGE\n");
		ZFCP_LOG_INFO(
			"Length of the ELS response buffer, "
			"specified in QTCB bottom, "
			"exceeds the size of the buffers "
			"that have been allocated for ELS response data "
			"(devno=0x%04x resp_buf_length=%d)\n",
			adapter->devno,
			bottom->resp_buf_length);
		retval = -EINVAL;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (header->fsf_status_qual.word[0]){

		case FSF_SQ_RETRY_IF_POSSIBLE:
			ZFCP_LOG_FLAGS(2, "FSF_SQ_RETRY_IF_POSSIBLE\n");
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_retry");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			debug_text_event(adapter->erp_dbf, 1, "fsf_sq_ltest");
			if (send_els->ls_code != ZFCP_LS_ADISC)
				zfcp_test_link(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		default:
			ZFCP_LOG_INFO(
				"bug: Wrong status qualifier 0x%x arrived.\n",
				header->fsf_status_qual.word[0]);
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
				(char*)header->fsf_status_qual.word, 16);
		}
		retval = -EINVAL;
		break;

	case FSF_UNKNOWN_COMMAND:
		ZFCP_LOG_FLAGS(2, "FSF_UNKNOWN_COMMAND\n");
		ZFCP_LOG_INFO(
			"FSF command 0x%x is not supported by FCP adapter "
			"(devno=0x%04x)\n",
			fsf_req->fsf_command,
			adapter->devno);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;

	default:
		ZFCP_LOG_NORMAL(
			"bug: An unknown FSF Status was presented "
			"(devno=0x%04x fsf_status=0x%08x)\n",
			adapter->devno,
			header->fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_sq_inval");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
			&header->fsf_status_qual.word[0], sizeof(u32));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		retval = -EINVAL;
		break;
	}

skip_fsfstatus:
	send_els->status = retval;

	if (send_els->handler != 0)
		send_els->handler(send_els->handler_data);

	if (send_els->completion != NULL)
		complete(send_els->completion);

	ZFCP_KFREE(send_els, sizeof(struct zfcp_send_els));

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/**
 * zfcp_fsf_send_els - Send an ELS
 * @*els: to send
 * Returns: 0 on success, -E* code else
 *
 * Create a FSF request from an ELS and queue it for sending. Chaining is used
 * if needed and supported (in that order).
 */
int zfcp_fsf_send_els(struct zfcp_send_els *els)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	unsigned long lock_flags;
	int retval;
	zfcp_fsf_req_t *fsf_req;
	zfcp_port_t *port = els->port;
	zfcp_adapter_t *adapter = port->adapter;
	volatile struct qdio_buffer_element_t *sbale;
        int bytes;

        retval = zfcp_fsf_req_create(adapter, FSF_QTCB_SEND_ELS,
                                     ZFCP_WAIT_FOR_SBAL|ZFCP_REQ_AUTO_CLEANUP,
                                     NULL, &lock_flags, &fsf_req);
	if (retval < 0) {
                ZFCP_LOG_INFO("error: Out of resources. "
                              "Could not create an ELS request, "
                              "destination port D_ID is 0x%06x "
                              "at the adapter with devno 0x%04x.\n",
                              port->d_id, adapter->devno);
                goto failed_req;
	}

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
        if (zfcp_use_one_sbal(els->req, els->req_count,
                              els->resp, els->resp_count)){
                /* both request buffer and response buffer
                   fit into one sbale each */
                sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
                sbale[2].addr = els->req[0].address;
                sbale[2].length = els->req[0].length;
                sbale[3].addr = els->resp[0].address;
                sbale[3].length = els->resp[0].length;
                sbale[3].flags |= SBAL_FLAGS_LAST_ENTRY;
        } else if (adapter->supported_features &
                   FSF_FEATURE_ELS_CT_CHAINED_SBALS) {
                /* try to use chained SBALs */
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                els->req, els->req_count,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: Out of resources (outbuf). "
                                      "Could not create an ELS request, "
                                      "destination port D_ID is 0x%06x "
                                      "at the adapter with devno 0x%04x.\n",
                                      port->d_id, adapter->devno);
                        if (bytes == 0) {
                                retval = -ENOMEM;
                        } else {
                                retval = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.req_buf_length = bytes;
                fsf_req->sbale_curr = ZFCP_LAST_SBALE_PER_SBAL;
                bytes = zfcp_qdio_sbals_from_sg(fsf_req,
                                                SBAL_FLAGS0_TYPE_WRITE_READ,
                                                els->resp, els->resp_count,
                                                ZFCP_MAX_SBALS_PER_ELS_REQ);
                if (bytes <= 0) {
                        ZFCP_LOG_INFO("error: Out of resources (inbuf). "
                                      "Could not create an ELS request, "
                                      "destination port D_ID is 0x%06x "
                                      "at the adapter with devno 0x%04x.\n",
                                      port->d_id, adapter->devno);
                        if (bytes == 0) {
                                retval = -ENOMEM;
                        } else {
                                retval = bytes;
                        }
                        goto failed_send;
                }
                fsf_req->qtcb->bottom.support.resp_buf_length = bytes;
        } else {
                /* reject request */
		ZFCP_LOG_INFO("error: microcode does not support chained SBALs."
                              "ELS request too big."
                              "Destination port D_ID is 0x%06x "
                              "at the adapter with devno 0x%04x.\n",
                              port->d_id, adapter->devno);
                retval = -EOPNOTSUPP;
                goto failed_send;
        }

	/* settings in QTCB */
	fsf_req->qtcb->bottom.support.d_id = port->d_id;
	fsf_req->qtcb->bottom.support.service_class = adapter->fc_service_class;
	fsf_req->qtcb->bottom.support.timeout = ZFCP_ELS_TIMEOUT;
	fsf_req->data.send_els = els;

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		ZFCP_LOG_DEBUG("error: Out of resources. Could not send an "
                               "ELS command via adapter with "
                               "devno 0x%04x, port WWPN 0x%016Lx\n",
                               adapter->devno,	(llui_t) port->wwpn);
		goto failed_send;
	} else {
                ZFCP_LOG_DEBUG("ELS request initiated "
                               "(adapter devno=0x%04x, port D_ID=0x%06x)\n",
                               adapter->devno, port->d_id);
                goto out;
        }

 failed_send:
	if (zfcp_fsf_req_free(fsf_req)) {
                ZFCP_LOG_NORMAL("bug: Could not remove one FSF request. Memory "
                                "leakage possible. (debug info 0x%lx).\n",
                                (unsigned long)fsf_req);
                retval = -EINVAL;
	};
 failed_req:
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock,
                                     lock_flags);

        return retval;
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/**
 * zfcp_fsf_send_els_sync - Send an els command and wait for the reply
 * @*els: pointer to needed data
 * Returns: 0 on success, -E* code else
 *
 * Waits on a completion until the response arrives.
 */
int zfcp_fsf_send_els_sync(struct zfcp_send_els *els)
{
	int ret;
	DECLARE_COMPLETION(complete);

	els->completion = &complete;
	
	ret = zfcp_fsf_send_els(els);
	
	if (0 == ret) {
		wait_for_completion(&complete);
	}

	return ret;
}


static inline volatile qdio_buffer_element_t * zfcp_qdio_sbale_get(
		zfcp_qdio_queue_t *queue,
		int sbal,
		int sbale)
{
	return &queue->buffer[sbal]->element[sbale];
}


static inline volatile qdio_buffer_element_t * zfcp_qdio_sbale_req(
		zfcp_fsf_req_t *fsf_req,
		int sbal,
		int sbale)
{
	return zfcp_qdio_sbale_get(
			&fsf_req->adapter->request_queue,
			sbal,
			sbale);
}


static inline volatile qdio_buffer_element_t * zfcp_qdio_sbale_resp(
		zfcp_fsf_req_t *fsf_req,
		int sbal,
		int sbale)
{
	return zfcp_qdio_sbale_get(
			&fsf_req->adapter->response_queue,
			sbal,
			sbale);
}


/* the following routines work on outbound queues */
static inline volatile qdio_buffer_element_t * zfcp_qdio_sbale_curr(
		zfcp_fsf_req_t *fsf_req)
{
	return zfcp_qdio_sbale_req(
			fsf_req,
			fsf_req->sbal_curr,
			fsf_req->sbale_curr);
}


/* can assume at least one free SBAL in outbound queue when called */
static inline void zfcp_qdio_sbal_limit(zfcp_fsf_req_t *fsf_req, int max_sbals)
{
	int count = atomic_read(&fsf_req->adapter->request_queue.free_count);
	count = min(count, max_sbals);
	fsf_req->sbal_last  = fsf_req->sbal_first;
	fsf_req->sbal_last += (count - 1);
	fsf_req->sbal_last %= QDIO_MAX_BUFFERS_PER_Q;
}


static inline volatile qdio_buffer_element_t * zfcp_qdio_sbal_chain(
		zfcp_fsf_req_t *fsf_req,
		unsigned long sbtype)
{
	volatile qdio_buffer_element_t *sbale;

	/* set last entry flag in current SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* don't exceed last allowed SBAL */
	if (fsf_req->sbal_curr == fsf_req->sbal_last)
		return NULL;

	/* set chaining flag in first SBALE of current SBAL */
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale->flags |= SBAL_FLAGS0_MORE_SBALS;

	/* calculate index of next SBAL */
	fsf_req->sbal_curr++;
	fsf_req->sbal_curr %= QDIO_MAX_BUFFERS_PER_Q;

	/* keep this requests number of SBALs up-to-date */
	fsf_req->sbal_number++;

	/* start at first SBALE of new SBAL */
	fsf_req->sbale_curr = 0;

	/* set storage-block type for new SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= sbtype;

	return sbale;
}


static inline volatile qdio_buffer_element_t * zfcp_qdio_sbale_next(
		zfcp_fsf_req_t *fsf_req, unsigned long sbtype)
{
	if (fsf_req->sbale_curr == ZFCP_LAST_SBALE_PER_SBAL)
		return zfcp_qdio_sbal_chain(fsf_req, sbtype);

	fsf_req->sbale_curr++;

	return zfcp_qdio_sbale_curr(fsf_req);
}


static inline int zfcp_qdio_sbals_zero(
		zfcp_qdio_queue_t *queue,
		int first,
		int last)
{
	qdio_buffer_t **buf = queue->buffer;
	int curr = first;
	int count = 0;

	for(;;) {
		curr %= QDIO_MAX_BUFFERS_PER_Q;
		count++;
		memset(buf[curr], 0, sizeof(qdio_buffer_t));
		if (curr == last)
			break;
		curr++;
	}
	return count;
}


static inline int zfcp_qdio_sbals_wipe(
		zfcp_fsf_req_t *fsf_req)
{
	return zfcp_qdio_sbals_zero(
			&fsf_req->adapter->request_queue,
			fsf_req->sbal_first,
			fsf_req->sbal_curr);
}


static inline void zfcp_qdio_sbale_fill(
		zfcp_fsf_req_t *fsf_req,
		unsigned long sbtype,
		void *addr,
		int length)
{
	volatile qdio_buffer_element_t *sbale = zfcp_qdio_sbale_curr(fsf_req);

	sbale->addr = addr;
	sbale->length = length;
}


static inline int zfcp_qdio_sbals_from_segment(
		zfcp_fsf_req_t *fsf_req,
		unsigned long sbtype,
		void* start_addr,
		unsigned long total_length)
{
	unsigned long remaining, length;
	void *addr;

	/* split segment up heeding page boundaries */
	for (addr = start_addr,
	     remaining = total_length;
	     remaining;
	     addr += length,
	     remaining -= length) {
		/* get next free SBALE for new piece */
		if (!zfcp_qdio_sbale_next(fsf_req, sbtype)) {
			/* no SBALE left, clean up and leave */
			zfcp_qdio_sbals_wipe(fsf_req);
			return -EINVAL;
		}
		/* calculate length of new piece */
		length = min(remaining,
			     (PAGE_SIZE - ((unsigned long)addr & (PAGE_SIZE - 1))));
		/* fill current SBALE with calculated piece */
		zfcp_qdio_sbale_fill(fsf_req, sbtype, addr, length);
	}
	return total_length;
}


/* for exploiters with a scatter-gather list ready at hand */
static inline int
zfcp_qdio_sbals_from_sg(zfcp_fsf_req_t *fsf_req, unsigned long sbtype,
                        struct scatterlist *sg,	int sg_count, int max_sbals)
{
	int sg_index;
	struct scatterlist *sg_segment;
	int bytes, retval;
	volatile qdio_buffer_element_t *sbale;
	zfcp_adapter_t *adapter;

	/* figure out last allowed SBAL */
	zfcp_qdio_sbal_limit(fsf_req, max_sbals);

	/* set storage-block type for current SBAL */
	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale->flags |= sbtype;

	/* process all segements of scatter-gather list */
	for (sg_index = 0, sg_segment = sg, bytes = 0;
	     sg_index < sg_count;
	     sg_index++, sg_segment++) {
		retval = zfcp_qdio_sbals_from_segment(
				fsf_req,
				sbtype,
				sg_segment->address,
				sg_segment->length);
		if (retval < 0)
			return retval;
		bytes += retval;
	}
	/* assume that no other SBALEs are to follow in the same SBAL */
	sbale = zfcp_qdio_sbale_curr(fsf_req);
	sbale->flags |= SBAL_FLAGS_LAST_ENTRY;

#ifdef ZFCP_STAT_REQSIZES
	adapter = fsf_req->adapter;
	if (sbtype == SBAL_FLAGS0_TYPE_READ)
		zfcp_statistics_inc(adapter, &adapter->read_req_head, bytes);
	else	zfcp_statistics_inc(adapter, &adapter->write_req_head, bytes);
#endif
	return bytes;
}


/* for exploiters with just a buffer ready at hand */
static inline int zfcp_qdio_sbals_from_buffer(
		zfcp_fsf_req_t *fsf_req,
		unsigned long sbtype,
		void *buffer,
		unsigned long length,
		int max_sbals)
{
	struct scatterlist sg_segment;

	sg_segment.address = buffer;
	sg_segment.length = length;

	return zfcp_qdio_sbals_from_sg(fsf_req, sbtype, &sg_segment, 1,
                                       max_sbals);
}


/* for exploiters with a SCSI command ready at hand */
static inline int zfcp_qdio_sbals_from_scsicmnd(
		zfcp_fsf_req_t *fsf_req,
		unsigned long sbtype,
		struct scsi_cmnd *scsi_cmnd)
{
	if (scsi_cmnd->use_sg)
		return zfcp_qdio_sbals_from_sg(fsf_req,	sbtype,
                                               (struct scatterlist *)
                                               scsi_cmnd->request_buffer,
                                               scsi_cmnd->use_sg,
                                               ZFCP_MAX_SBALS_PER_REQ);
	else
                return zfcp_qdio_sbals_from_buffer(fsf_req, sbtype,
                                                   scsi_cmnd->request_buffer,
                                                   scsi_cmnd->request_bufflen,
                                                   ZFCP_MAX_SBALS_PER_REQ);
}

/*
 * function:
 *
 * purpose:
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 */
static int zfcp_fsf_exchange_config_data(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	volatile qdio_buffer_element_t *sbale;
	int retval = 0;
	unsigned long lock_flags;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_EXCHANGE_CONFIG_DATA,
			ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
	if (retval < 0) {
                ZFCP_LOG_INFO(
			 "error: Out of resources. Could not create an "
                         "exchange configuration data request for"
                         "the adapter with devno 0x%04x.\n",
                         erp_action->adapter->devno);
		goto out;
	}


	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->erp_action = erp_action;
	erp_action->fsf_req->qtcb->bottom.config.feature_selection =
		FSF_FEATURE_CFDC;

	/* start QDIO request for this FSF request */
        retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
        if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not send an exchange configuration data "
			"command on the adapter with devno 0x%04x\n",
			erp_action->adapter->devno);
                if (zfcp_fsf_req_free(erp_action->fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)erp_action->fsf_req);
                }
		erp_action->fsf_req = NULL;
                goto out;
        }
 
        ZFCP_LOG_DEBUG(
                "Exchange Configuration Data request initiated "
                "(adapter devno=0x%04x)\n",
                erp_action->adapter->devno);

out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

        ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/**
 * zfcp_fsf_exchange_config_evaluate
 * @fsf_req: fsf_req which belongs to xchg config data request
 * @xchg_ok: specifies if xchg config data was incomplete or complete (0/1)
 *
 * returns: -EIO on error, 0 otherwise
 */
static int
zfcp_fsf_exchange_config_evaluate(zfcp_fsf_req_t *fsf_req, int xchg_ok)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	fsf_qtcb_bottom_config_t *bottom;
	zfcp_adapter_t *adapter = fsf_req->adapter;

	bottom = &fsf_req->qtcb->bottom.config;
	ZFCP_LOG_DEBUG(
		"low/high QTCB version 0x%x/0x%x of FSF\n",
		bottom->low_qtcb_version, bottom->high_qtcb_version);
	adapter->fsf_lic_version = bottom->lic_version;
	adapter->supported_features = bottom->supported_features;

	if (xchg_ok) {
		adapter->wwnn = bottom->nport_serv_param.wwnn;
		adapter->wwpn = bottom->nport_serv_param.wwpn;
		adapter->s_id = bottom->s_id & ZFCP_DID_MASK;
		adapter->fc_topology = bottom->fc_topology;
		adapter->fc_link_speed = bottom->fc_link_speed;
		adapter->hydra_version = bottom->adapter_type;
	} else {
		adapter->wwnn = 0;
		adapter->wwpn = 0;
		adapter->s_id = 0;
		adapter->fc_topology = 0;
		adapter->fc_link_speed = 0;
		adapter->hydra_version = 0;
	}

	if (adapter->supported_features & FSF_FEATURE_HBAAPI_MANAGEMENT) {
		adapter->hardware_version = bottom->hardware_version;
		memcpy(adapter->serial_number, bottom->serial_number, 17);
		EBCASC(adapter->serial_number, sizeof(adapter->serial_number));
	}

	ZFCP_LOG_INFO(
		"The adapter with devno=0x%04x reported "
		"the following characteristics:\n"
		"WWNN 0x%016Lx, WWPN 0x%016Lx, S_ID 0x%08x,\n"
		"adapter version 0x%x, LIC version 0x%x, "
		"FC link speed %d Gb/s\n",
		adapter->devno,
		(llui_t) adapter->wwnn, (llui_t) adapter->wwpn,
		(unsigned int) adapter->s_id,
		adapter->hydra_version,
		adapter->fsf_lic_version,
		adapter->fc_link_speed);
	if (ZFCP_QTCB_VERSION < bottom->low_qtcb_version) {
		ZFCP_LOG_NORMAL(
			"error: the adapter with devno 0x%04x "
			"only supports newer control block "
			"versions in comparison to this device "
			"driver (try updated device driver)\n",
			adapter->devno);
		debug_text_event(adapter->erp_dbf, 0, "low_qtcb_ver");
		zfcp_erp_adapter_shutdown(adapter, 0);
		return -EIO;
	}
	if (ZFCP_QTCB_VERSION > bottom->high_qtcb_version) {
		ZFCP_LOG_NORMAL(
			"error: the adapter with devno 0x%04x "
			"only supports older control block "
			"versions than this device driver uses"
			"(consider a microcode upgrade)\n",
			adapter->devno);
		debug_text_event(adapter->erp_dbf, 0, "high_qtcb_ver");
		zfcp_erp_adapter_shutdown(adapter, 0);
		return -EIO;
	}
	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_exchange_config_data_handler
 *
 * purpose:     is called for finished Exchange Configuration Data command
 *
 * returns:
 */
int zfcp_fsf_exchange_config_data_handler
	(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	int retval = -EIO;
	fsf_qtcb_bottom_config_t *bottom;
	zfcp_adapter_t *adapter = fsf_req->adapter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
                (unsigned long)fsf_req);

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't set any value, stay with the old (unitialized) ones */ 
                goto skip_fsfstatus;
        }

	switch (fsf_req->qtcb->header.fsf_status) {

        case FSF_GOOD :
                ZFCP_LOG_FLAGS(2,"FSF_GOOD\n");
		if (zfcp_fsf_exchange_config_evaluate(fsf_req, 1))
			goto skip_fsfstatus;
                switch (adapter->fc_topology) {
                case FSF_TOPO_P2P:
                        ZFCP_LOG_FLAGS(1,"FSF_TOPO_P2P\n");
                        ZFCP_LOG_NORMAL("error: Point-to-point fibre-channel "
                                      "configuration detected "
                                      "at the adapter with devno "
                                      "0x%04x, not supported, shutting down adapter\n",
                                      adapter->devno);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"top-p-to-p");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
                case FSF_TOPO_AL:
                        ZFCP_LOG_FLAGS(1,"FSF_TOPO_AL\n");
                        ZFCP_LOG_NORMAL("error: Arbitrated loop fibre-channel "
                                      "topology detected "
                                      "at the adapter with devno "
                                      "0x%04x, not supported, shutting down adapter\n",
                                      adapter->devno);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"top-al");
			zfcp_erp_adapter_shutdown(adapter, 0);
                        goto skip_fsfstatus;
                case FSF_TOPO_FABRIC:
                        ZFCP_LOG_FLAGS(1,"FSF_TOPO_FABRIC\n");
                        ZFCP_LOG_INFO("Switched fabric fibre-channel "
                                      "network detected "
                                      "at the adapter with devno "
                                      "0x%04x\n",
                                      adapter->devno);
                        break;
                default:
                        ZFCP_LOG_NORMAL("bug: The fibre-channel topology "
                                        "reported by the exchange "
                                        "configuration command for "
                                        "the adapter with devno "
                                        "0x%04x is not "
                                        "of a type known to the zfcp "
                                        "driver, shutting down adapter\n",
                                        adapter->devno);
                        debug_text_exception(fsf_req->adapter->erp_dbf,0,
                                             "unknown-topo");
			zfcp_erp_adapter_shutdown(adapter, 0);
                        goto skip_fsfstatus;
                }
		bottom = &fsf_req->qtcb->bottom.config;
                if (bottom->max_qtcb_size < sizeof(fsf_qtcb_t)) {
                        ZFCP_LOG_NORMAL("bug: Maximum QTCB size (%d bytes) "
                                        "allowed by the adapter with devno "
                                        "0x%04x is lower than the minimum "
                                        "required by the driver (%ld bytes).\n",
                                        bottom->max_qtcb_size,
                                        adapter->devno,
                                        sizeof(fsf_qtcb_t));
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"qtcb-size");
                        debug_event(fsf_req->adapter->erp_dbf,0,&bottom->max_qtcb_size,
                                    sizeof(u32)); 
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
                }
                atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);
                retval = 0;

                break;

	case FSF_EXCHANGE_CONFIG_DATA_INCOMPLETE:
		debug_text_event(adapter->erp_dbf, 0, "xchg-inco");

		if (zfcp_fsf_exchange_config_evaluate(fsf_req, 0))
			goto skip_fsfstatus;

		ZFCP_LOG_INFO(
			"Local link to adapter with devno 0x%04x is down\n",
			adapter->devno);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK |
				ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				&adapter->status);
		zfcp_erp_adapter_failed(adapter);
		break;

        default:
                /* retval is -EIO by default */
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf-stat-ng");
                debug_event(fsf_req->adapter->erp_dbf,0,
                            &fsf_req->qtcb->header.fsf_status,
                            sizeof(u32)); 
                zfcp_erp_adapter_shutdown(adapter, 0);
	}
        
 skip_fsfstatus:
        
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:    zfcp_fsf_open_port
 *
 * purpose:	
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated 
 */
static int zfcp_fsf_open_port(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	volatile qdio_buffer_element_t *sbale;
	int retval = 0;
	unsigned long lock_flags;
 
	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_OPEN_PORT_WITH_DID,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
			"open port request for "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->qtcb->bottom.support.d_id = erp_action->port->d_id;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->port->status);
	erp_action->fsf_req->data.open_port.port = erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
                ZFCP_LOG_INFO(
			"error: Could not send an "
                        "open port request for "
                        "the port with WWPN 0x%016Lx connected to "
                        "the adapter with devno 0x%04x.\n",
                        (llui_t)erp_action->port->wwpn,
                        erp_action->adapter->devno);
		if (zfcp_fsf_req_free(erp_action->fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)erp_action->fsf_req);
                        retval=-EINVAL;
                };
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG(
		"Open Port request initiated "
		"(adapter devno=0x%04x, port WWPN=0x%016Lx)\n",
		erp_action->adapter->devno,
		(llui_t)erp_action->port->wwpn);
	
out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_open_port_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int zfcp_fsf_open_port_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
	zfcp_port_t *port;
	fsf_plogi_t *plogi;
	fsf_qtcb_header_t *header = &fsf_req->qtcb->header;
	u16 subtable, rule, counter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);
     
	port = fsf_req->data.open_port.port;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}
     
	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {
          
        case FSF_PORT_ALREADY_OPEN :
                ZFCP_LOG_FLAGS(0, "FSF_PORT_ALREADY_OPEN\n");
                ZFCP_LOG_NORMAL("bug: The remote port with WWPN=0x%016Lx "
                                "connected to the adapter with "
                                "devno=0x%04x is already open.\n",
                                (llui_t)port->wwpn,
                                port->adapter->devno);
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_s_popen");
                /* This is a bug, however operation should continue normally
                 * if it is simply ignored */
                break;
                
	case FSF_ACCESS_DENIED :
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot open port "
				"(devno=0x%04x wwpn=0x%016Lx)\n",
				port->adapter->devno,
				(llui_t)port->wwpn);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_port_failed(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED :
                ZFCP_LOG_FLAGS(1, "FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED\n");
                ZFCP_LOG_INFO("error: The FSF adapter is out of resources. "
                              "The remote port with WWPN=0x%016Lx "
                              "connected to the adapter with "
                              "devno=0x%04x could not be opened. "
                              "Disabling it.\n",
                              (llui_t)port->wwpn,
                              port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_max_ports");
                zfcp_erp_port_failed(port);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

        case FSF_ADAPTER_STATUS_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                        /* ERP strategy will escalate */
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        /* ERP strategy will escalate */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;  
                case FSF_SQ_NO_RETRY_POSSIBLE :
                        ZFCP_LOG_FLAGS(0, "FSF_SQ_NO_RETRY_POSSIBLE\n");
                        ZFCP_LOG_NORMAL("The remote port with WWPN=0x%016Lx "
                                        "connected to the adapter with "
                                        "devno=0x%04x could not be opened. "
                                        "Disabling it.\n",
                                        (llui_t)port->wwpn,
                                        port->adapter->devno);
                        debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_sq_no_retry");
                        zfcp_erp_port_failed(port);
                        zfcp_cmd_dbf_event_fsf("sqnretry", fsf_req,
                                               &fsf_req->qtcb->header.fsf_status_qual,
                                               sizeof(fsf_status_qual_t));
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        break;
                default:
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
                                        fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                        sizeof(u32));
                        break;
                }
                break;

        case FSF_GOOD :
                ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
                /* save port handle assigned by FSF */
                port->handle = fsf_req->qtcb->header.port_handle;
                ZFCP_LOG_INFO("The remote port (WWPN=0x%016Lx) via adapter "
                              "(devno=0x%04x) was opened, it's "
                              "port handle is 0x%x\n",
                              (llui_t)port->wwpn,
                              port->adapter->devno,
                              port->handle);
                /* mark port as open */
                atomic_set_mask(
			ZFCP_STATUS_COMMON_OPEN |
			ZFCP_STATUS_PORT_PHYS_OPEN,
			&port->status);
                retval = 0;
		/* check whether D_ID has changed during open */
		/*
		 * FIXME: This check is not airtight, as the FCP channel does
		 * not monitor closures of target port connections caused on
		 * the remote side. Thus, they might miss out on invalidating
		 * locally cached WWPNs (and other N_Port parameters) of gone
		 * target ports. So, our heroic attempt to make things safe
		 * could be undermined by 'open port' response data tagged with
		 * obsolete WWPNs. Another reason to monitor potential
		 * connection closures ourself at least (by interpreting
		 * incoming ELS' and unsolicited status). It just crosses my
		 * mind that one should be able to cross-check by means of
		 * another GID_PN straight after a port has been opened.
		 * Alternately, an ADISC/PDISC ELS should suffice, as well.
		 */
		plogi = (fsf_plogi_t*) fsf_req->qtcb->bottom.support.els;
		if (!atomic_test_mask(ZFCP_STATUS_PORT_NO_WWPN, &port->status)) {
			if (fsf_req->qtcb->bottom.support.els1_length <
			    ((((unsigned long)&plogi->serv_param.wwpn) -
			      ((unsigned long)plogi)) +
			     sizeof(fsf_wwn_t))) {
				ZFCP_LOG_INFO(
					"warning: insufficient length of PLOGI payload (%i)\n",
					fsf_req->qtcb->bottom.support.els1_length);
				debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_short_plogi:");
				/* skip sanity check and assume wwpn is ok */
			} else	{
				if (plogi->serv_param.wwpn != port->wwpn) {
					ZFCP_LOG_INFO(
						"warning: D_ID of port with WWPN 0x%016Lx changed "
						"during open\n",
						(llui_t)port->wwpn);
					debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_did_change:");
					atomic_clear_mask(
						ZFCP_STATUS_PORT_DID_DID,
						&port->status);
				}
			}
		}
                break;
                
	default :
		ZFCP_LOG_NORMAL(
			"bug: An unknown FSF Status was presented "
			"(debug info 0x%x)\n",
			fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf,0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof(u32));
		break;
	}

skip_fsfstatus:

	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &port->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_port
 *
 * purpose:     submit FSF command "close port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
static int zfcp_fsf_close_port(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	volatile qdio_buffer_element_t *sbale;
        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_PORT,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
        if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
			"close port request for WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		goto out;
        }

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

        atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->port->status);
        erp_action->fsf_req->data.close_port.port = erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;
        erp_action->fsf_req->qtcb->header.port_handle = erp_action->port->handle;

        /* start QDIO request for this FSF request */
        retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
        if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not send a "
			"close port request for WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		if (zfcp_fsf_req_free(erp_action->fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
				"(debug info 0x%lx).\n",
				(unsigned long)erp_action->fsf_req);
                        retval=-EINVAL;
		};
		erp_action->fsf_req = NULL;
		goto out;
        }

        ZFCP_LOG_TRACE(
                "Close Port request initiated "
                "(adapter devno=0x%04x, port WWPN=0x%016Lx)\n",
                erp_action->adapter->devno,
                (llui_t)erp_action->port->wwpn);

out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

        ZFCP_LOG_TRACE("exit (%d)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_port_handler
 *
 * purpose:     is called for finished Close Port FSF command
 *
 * returns:
 */
static int zfcp_fsf_close_port_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

        int retval = -EINVAL;
        zfcp_port_t *port;

        ZFCP_LOG_TRACE(
                "enter (fsf_req=0x%lx)\n",
                (unsigned long)fsf_req);

        port = fsf_req->data.close_port.port; 

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
                /* don't change port status in our bookkeeping */
                goto skip_fsfstatus;
        }

        /* evaluate FSF status in QTCB */
        switch (fsf_req->qtcb->header.fsf_status) {

		case FSF_PORT_HANDLE_NOT_VALID :
			ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO(
				"Temporary port identifier (handle) 0x%x "
				"for the port with WWPN 0x%016Lx connected to "
				"the adapter of devno 0x%04x is "
				"not valid. This may happen occasionally.\n",
				port->handle,
				(llui_t)port->wwpn,
				port->adapter->devno);
			ZFCP_LOG_DEBUG("status qualifier:\n");
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_DEBUG,
				(char*)&fsf_req->qtcb->header.fsf_status_qual,
				16);
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_phand_nv");
			zfcp_erp_adapter_reopen(port->adapter, 0);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		case FSF_ADAPTER_STATUS_AVAILABLE :
			ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                        /* Note: FSF has actually closed the port in this case.
                         * The status code is just daft. Fingers crossed for a change
                         */
                        retval=0;
                        break;
#if 0
			switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                        case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                                ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                                /* This will now be escalated by ERP */
                                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                                break;
                        case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                                ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                                /* ERP strategy will escalate */
                                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                                break;
                        default:
                                ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
						fsf_req->qtcb->header.fsf_status_qual.word[0]);
                                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                                debug_exception(fsf_req->adapter->erp_dbf,0,
                                                &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                                sizeof(u32));
                                break;
			}
			break;
#endif

		case FSF_GOOD :
			ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
			ZFCP_LOG_TRACE(
				"remote port (WWPN=0x%016Lx) via adapter "
				"(devno=0x%04x) closed, "
				"port handle 0x%x\n",
				(llui_t)port->wwpn,
				port->adapter->devno,
				port->handle);
			zfcp_erp_modify_port_status(
				port,
				ZFCP_STATUS_COMMON_OPEN,
				ZFCP_CLEAR);
			retval = 0;
			break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status,
                                        sizeof(u32));
                        break;
	}

skip_fsfstatus:
        atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &port->status);

        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_physical_port
 *
 * purpose:     submit FSF command "close physical port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
static int zfcp_fsf_close_physical_port(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	volatile qdio_buffer_element_t *sbale;
        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_PHYSICAL_PORT,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
        if (retval < 0) {
                ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
                        "close physical port request for "
                        "the port with WWPN 0x%016Lx connected to "
                        "the adapter with devno 0x%04x.\n",
                        (llui_t)erp_action->port->wwpn,
                        erp_action->adapter->devno);
                goto out;
        }

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

        /* mark port as being closed */
        atomic_set_mask(ZFCP_STATUS_PORT_PHYS_CLOSING, &erp_action->port->status);
        /* save a pointer to this port */
        erp_action->fsf_req->data.close_physical_port.port = erp_action->port;
        /* port to be closeed */
        erp_action->fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	erp_action->fsf_req->erp_action = erp_action;

        /* start QDIO request for this FSF request */
        retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
        if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not send an "
			"close physical port request for "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		if (zfcp_fsf_req_free(erp_action->fsf_req)){
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
				"(debug info 0x%lx).\n",
				(unsigned long)erp_action->fsf_req);
                        retval=-EINVAL;
		};
		erp_action->fsf_req = NULL;
		goto out;
        }

        ZFCP_LOG_TRACE(
                "Close Physical Port request initiated "
                "(adapter devno=0x%04x, port WWPN=0x%016Lx)\n",
                erp_action->adapter->devno,
                (llui_t)erp_action->port->wwpn);

out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

        ZFCP_LOG_TRACE("exit (%d)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_physical_port_handler
 *
 * purpose:     is called for finished Close Physical Port FSF command
 *
 * returns:
 */
static int zfcp_fsf_close_physical_port_handler(zfcp_fsf_req_t *fsf_req){
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP

        int retval = -EINVAL;
        zfcp_port_t *port;
        zfcp_unit_t *unit;
        unsigned long flags;
	fsf_qtcb_header_t *header = &fsf_req->qtcb->header;
	u16 subtable, rule, counter;

        ZFCP_LOG_TRACE(
                "enter (fsf_req=0x%lx)\n",
                (unsigned long)fsf_req);

        port = fsf_req->data.close_physical_port.port;

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
                /* don't change port status in our bookkeeping */
                goto skip_fsfstatus;
        }

        /* evaluate FSF status in QTCB */
        switch (fsf_req->qtcb->header.fsf_status) {

		case FSF_PORT_HANDLE_NOT_VALID :
			ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO(
				"Temporary port identifier (handle) 0x%x "
				"for the port with WWPN 0x%016Lx connected to "
				"the adapter of devno 0x%04x is "
				"not valid. This may happen occasionally.\n",
				port->handle,
				(llui_t)port->wwpn,
				port->adapter->devno);
			ZFCP_LOG_DEBUG("status qualifier:\n");
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_DEBUG,
				(char*)&fsf_req->qtcb->header.fsf_status_qual,
				16);
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_phand_nv");
                        zfcp_erp_adapter_reopen(port->adapter, 0);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        //                        panic("for ralph");
			break;

	case FSF_ACCESS_DENIED :
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot close physical port "
				"(devno=0x%04x wwpn=0x%016Lx)\n",
				port->adapter->devno,
				(llui_t)port->wwpn);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_BOXED :
                ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened but "
                               "it was attempted to close it physically.\n",
                               (llui_t)port->wwpn,
                               port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_pboxed");
                zfcp_erp_port_reopen(port, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;


		case FSF_ADAPTER_STATUS_AVAILABLE :
			ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
			switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                        case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                                ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                                /* This will now be escalated by ERP */
                                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                                break;
                        case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                                ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                                /* ERP strategy will escalate */
                                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                                break;
                        default:
                                ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
						fsf_req->qtcb->header.fsf_status_qual.word[0]);
                                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                                debug_exception(fsf_req->adapter->erp_dbf,0,
                                                &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                                sizeof(u32));
                                break;
			}
			break;
	
		case FSF_GOOD :
			ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
			ZFCP_LOG_DEBUG(
				"Remote port (WWPN=0x%016Lx) via adapter "
				"(devno=0x%04x) physically closed, "
				"port handle 0x%x\n",
				(llui_t)port->wwpn,
				port->adapter->devno,
				port->handle);
                        /* can't use generic zfcp_erp_modify_port_status because
                         * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
                         */
			atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, 
                                          &port->status);
                        read_lock_irqsave(&port->unit_list_lock, flags);
                        ZFCP_FOR_EACH_UNIT(port, unit) {
                                atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, 
                                                  &unit->status);
                        }
                        read_unlock_irqrestore(&port->unit_list_lock, flags);
			retval = 0;
			break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status,
                                        sizeof(u32));
                        break;
        }

skip_fsfstatus:
        atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_CLOSING, &port->status);
        
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_open_unit
 *
 * purpose:
 *
 * returns:
 *
 * assumptions:	This routine does not check whether the associated
 *		remote port has already been opened. This should be
 *		done by calling routines. Otherwise some status
 *		may be presented by FSF
 */
static int zfcp_fsf_open_unit(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	volatile qdio_buffer_element_t *sbale;
	int retval = 0;
	unsigned long lock_flags;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_OPEN_LUN,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
			"open unit request for FCP_LUN 0x%016Lx connected to "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->unit->fcp_lun,
			(llui_t)erp_action->unit->port->wwpn,
			erp_action->adapter->devno);
		goto out;
	}

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	erp_action->fsf_req->qtcb->header.port_handle =
                erp_action->port->handle;
	erp_action->fsf_req->qtcb->bottom.support.fcp_lun =
                erp_action->unit->fcp_lun;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->unit->status);
	erp_action->fsf_req->data.open_unit.unit = erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not send an open unit request "
			"on the adapter with devno 0x%04x, "
			"port WWPN 0x%016Lx for unit FCP_LUN 0x%016Lx\n",
			erp_action->adapter->devno,
			(llui_t)erp_action->port->wwpn,
			(llui_t)erp_action->unit->fcp_lun);
		if (zfcp_fsf_req_free(erp_action->fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
				"(debug info 0x%lx).\n",
				(unsigned long)erp_action->fsf_req);
                        retval=-EINVAL;
		};
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE(
		"Open LUN request initiated "
		"(adapter devno=0x%04x, port WWPN=0x%016Lx, unit FCP_LUN=0x%016Lx)\n",
		erp_action->adapter->devno,
		(llui_t)erp_action->port->wwpn,
		(llui_t)erp_action->unit->fcp_lun);

out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/*
 * function:    zfcp_fsf_open_unit_handler
 *
 * purpose:	is called for finished Open LUN command
 *
 * returns:	
 */
static int zfcp_fsf_open_unit_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
	zfcp_adapter_t *adapter;
	zfcp_unit_t *unit;
	fsf_qtcb_header_t *header;
	u16 subtable, rule, counter;
 
	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	adapter = fsf_req->adapter;
	unit = fsf_req->data.open_unit.unit;
	header = &fsf_req->qtcb->header;

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change unit status in our bookkeeping */ 
                goto skip_fsfstatus;
        }

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

        case FSF_PORT_HANDLE_NOT_VALID :
                ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
                ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
                              "for the port with WWPN 0x%016Lx connected to "
                              "the adapter of devno 0x%04x is "
                              "not valid. This may happen occasionally.\n",
                              unit->port->handle,
                              (llui_t)unit->port->wwpn,
                              unit->port->adapter->devno);
                ZFCP_LOG_DEBUG("status qualifier:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_ph_nv");
                zfcp_erp_adapter_reopen(unit->port->adapter, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                
        case FSF_LUN_ALREADY_OPEN :
                ZFCP_LOG_FLAGS(0, "FSF_LUN_ALREADY_OPEN\n");
                ZFCP_LOG_NORMAL("bug: Attempted to open the logical unit "
				"with FCP_LUN 0x%016Lx at "
				"the remote port with WWPN 0x%016Lx connected "
				"to the adapter with devno 0x%04x twice.\n", 
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_s_uopen");
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

	case FSF_ACCESS_DENIED :
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot open unit 0x%016Lx "
				"on the remote port 0x%016Lx "
				"on adapter with devno 0x%04x\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				adapter->devno);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(adapter->erp_dbf, 1, "fsf_s_access");
		zfcp_erp_unit_failed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_PORT_BOXED :
                ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened\n",
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_pboxed");
                zfcp_erp_port_reopen(unit->port, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;
                        
	case FSF_LUN_SHARING_VIOLATION :
		ZFCP_LOG_FLAGS(2, "FSF_LUN_SHARING_VIOLATION\n");
		if (header->fsf_status_qual.word[0] != 0) {
			ZFCP_LOG_NORMAL("FCP-LUN 0x%Lx at the remote port with "
					"WWPN 0x%Lx connected to the adapter "
					"with devno 0x%04x is already in use "
					"in LPAR%d\n",
					(llui_t)unit->fcp_lun,
					(llui_t)unit->port->wwpn,
					adapter->devno,
					header->fsf_status_qual.fsf_queue_designator.hla);
		} else {
			subtable = header->fsf_status_qual.halfword[4];
			rule = header->fsf_status_qual.halfword[5];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_NORMAL("Access to FCP-LUN 0x%Lx at the "
						"remote port with WWPN 0x%Lx "
						"connected to the adapter "
						"with devno 0x%04x "
						"is denied (%s rule %d)\n",
						(llui_t)unit->fcp_lun,
						(llui_t)unit->port->wwpn,
						adapter->devno,
						zfcp_act_subtable_type[subtable],
						rule);
				break;
			}
		}
		ZFCP_LOG_DEBUG("Additional sense data is presented:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			(char*)&header->fsf_status_qual,
			sizeof(fsf_status_qual_t));
		debug_text_event(adapter->erp_dbf,2,"fsf_s_l_sh_vio");
		zfcp_erp_unit_failed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED :
                ZFCP_LOG_FLAGS(1, "FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED\n");
                ZFCP_LOG_INFO("error: The adapter ran out of resources. "
                              "There is no handle (temporary port identifier) "
                              "available for the unit with "
                              "FCP_LUN 0x%016Lx at the remote port with WWPN 0x%016Lx "
                              "connected to the adapter with devno 0x%04x\n",
                              (llui_t)unit->fcp_lun,
                              (llui_t)unit->port->wwpn,
                              unit->port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_max_units");
                zfcp_erp_unit_failed(unit);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                
        case FSF_ADAPTER_STATUS_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        /* Re-establish link to port */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                        zfcp_erp_port_reopen(unit->port, 0);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                        /* ERP strategy will escalate */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                default:
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
					fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                        sizeof(u32));
                }
                break;

        case FSF_GOOD :
                ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
                /* save LUN handle assigned by FSF */
                unit->handle = fsf_req->qtcb->header.lun_handle;
			ZFCP_LOG_TRACE("unit (FCP_LUN=0x%016Lx) of remote port "
                                       "(WWPN=0x%016Lx) via adapter (devno=0x%04x) opened, "
                                       "port handle 0x%x \n",
                                       (llui_t)unit->fcp_lun,
                                       (llui_t)unit->port->wwpn,
                                       unit->port->adapter->devno,
                                       unit->handle);
			/* mark unit as open */
			atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
			retval = 0;
			break;
                        
        default :
                ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                debug_exception(fsf_req->adapter->erp_dbf,0,
                                &fsf_req->qtcb->header.fsf_status,
                                sizeof(u32));
                break;
	}
        
skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &unit->status); 
        
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_unit
 *
 * purpose:
 *
 * returns:	address of fsf_req - request successfully initiated
 *		NULL - 
 *
 * assumptions: This routine does not check whether the associated
 *              remote port/lun has already been opened. This should be
 *              done by calling routines. Otherwise some status
 *              may be presented by FSF
 */
static int zfcp_fsf_close_unit(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	volatile qdio_buffer_element_t *sbale;
        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_LUN,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
                        &erp_action->adapter->pool.fsf_req_erp,
			&lock_flags,
			&erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
			"close unit request for FCP_LUN 0x%016Lx connected to "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->unit->fcp_lun,
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		goto out;
        }

	sbale = zfcp_qdio_sbale_req(erp_action->fsf_req,
                                    erp_action->fsf_req->sbal_curr, 0);
        sbale[0].flags |= SBAL_FLAGS0_TYPE_READ;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY;

        erp_action->fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	erp_action->fsf_req->qtcb->header.lun_handle = erp_action->unit->handle;
        atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->unit->status);
        erp_action->fsf_req->data.close_unit.unit = erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

        /* start QDIO request for this FSF request */
        retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Could not send a "
			"close unit request for FCP_LUN 0x%016Lx connected to "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->unit->fcp_lun,
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		if (zfcp_fsf_req_free(erp_action->fsf_req)){
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
				"(debug info 0x%lx).\n",
				(unsigned long)erp_action->fsf_req);
                        retval = -EINVAL;
		};
		erp_action->fsf_req = NULL;
		goto out;
	}

        ZFCP_LOG_TRACE(
                "Close LUN request initiated "
                "(adapter devno=0x%04x, port WWPN=0x%016Lx, unit FCP_LUN=0x%016Lx)\n",
                erp_action->adapter->devno,
                (llui_t)erp_action->port->wwpn,
                (llui_t)erp_action->unit->fcp_lun);

out:
        write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock, lock_flags);

        ZFCP_LOG_TRACE("exit (%d)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_close_unit_handler
 *
 * purpose:     is called for finished Close LUN FSF command
 *
 * returns:
 */
static int zfcp_fsf_close_unit_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

        int retval = -EINVAL;
        zfcp_unit_t *unit;

        ZFCP_LOG_TRACE(
                "enter (fsf_req=0x%lx)\n",
                (unsigned long)fsf_req);

        unit = fsf_req->data.close_unit.unit; /* restore unit */

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
                /* don't change unit status in our bookkeeping */
                goto skip_fsfstatus;
        }

        /* evaluate FSF status in QTCB */
        switch (fsf_req->qtcb->header.fsf_status) {

                case FSF_PORT_HANDLE_NOT_VALID :
			ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO(
				"Temporary port identifier (handle) 0x%x "
				"for the port with WWPN 0x%016Lx connected to "
                                "the adapter of devno 0x%04x is "
				"not valid. This may happen in rare "
                                "circumstances\n",
				unit->port->handle,
				(llui_t)unit->port->wwpn,
                                unit->port->adapter->devno);
			ZFCP_LOG_DEBUG("status qualifier:\n");
                        ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_DEBUG,
				(char*)&fsf_req->qtcb->header.fsf_status_qual,
				16);
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_phand_nv");
                        zfcp_erp_adapter_reopen(unit->port->adapter, 0);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        break;

                case FSF_LUN_HANDLE_NOT_VALID :
			ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO(
                                "Temporary LUN identifier (handle) 0x%x "
				"of the logical unit with FCP_LUN 0x%016Lx at "
				"the remote port with WWPN 0x%016Lx connected "
                                "to the adapter with devno 0x%04x is " 
				"not valid. This may happen occasionally.\n",
				unit->handle,
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
			ZFCP_LOG_DEBUG("Status qualifier data:\n");
                        ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_DEBUG,
				(char*)&fsf_req->qtcb->header.fsf_status_qual,
				16);
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_lhand_nv");
                        zfcp_erp_port_reopen(unit->port, 0);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        break;

        case FSF_PORT_BOXED :
                ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened\n",
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_pboxed");
                zfcp_erp_port_reopen(unit->port, 0);
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;

        case FSF_ADAPTER_STATUS_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        /* re-establish link to port */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                        zfcp_erp_port_reopen(unit->port, 0);
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                        /* ERP strategy will escalate */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                default:
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
                                        fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                        sizeof(u32));
                        break;
                }
                break;
                
        case FSF_GOOD :
                ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
                ZFCP_LOG_TRACE("unit (FCP_LUN=0x%016Lx) of remote port "
                               "(WWPN=0x%016Lx) via adapter (devno=0x%04x) closed, "
                               "port handle 0x%x \n",
                                (llui_t)unit->fcp_lun,
                               (llui_t)unit->port->wwpn,
                                unit->port->adapter->devno,
                               unit->handle);
                /* mark unit as closed */
                atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
                retval = 0;
                break;
                
        default :
                ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
                                "(debug info 0x%x)\n",
                                fsf_req->qtcb->header.fsf_status);
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                debug_exception(fsf_req->adapter->erp_dbf,0,
                                &fsf_req->qtcb->header.fsf_status,
                                sizeof(u32));
                break;
        }

skip_fsfstatus:

        atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &unit->status);

        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

#ifdef ZFCP_RESID
/*
 * function:    zfcp_scsi_truncte_command
 *
 * purpose:
 *
 * returns:
 */
inline int zfcp_scsi_truncate_command(unsigned char *command_struct,
                                      unsigned long original_byte_length,
                                      unsigned long new_byte_length)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;
        unsigned long factor, new_block_size;
        u8 *len_6;
        u16 *len_10;
        u32 *len_12;
        /* trace */
        ZFCP_LOG_NORMAL(
                 "enter command_struct = 0x%lx, "
                 "original_byte_length = %ld "
                 "new_byte_length = %ld\n",
                 (unsigned long)command_struct,
                 original_byte_length,
                 new_byte_length);
        
        /*trace*/
        ZFCP_LOG_NORMAL("original SCSI command:\n");
        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                      command_struct,
                      12);

        switch(command_struct[0]) {
        case WRITE_6:
        case READ_6:
                len_6 = &command_struct[4];
                factor = (unsigned long)(original_byte_length
                                       / *len_6);
                new_block_size = new_byte_length / factor;
                if(new_byte_length % factor) {
                        ZFCP_LOG_NORMAL("bug: Recalculation of command size "
                                        "failed. "
                                        "(debug info %d, %ld, %d, %ld, %ld)\n",
                                        6,
                                        original_byte_length,
                                        *len_6,
                                        new_byte_length,
                                        factor);
                        goto error;
                }
                /* trace */
                ZFCP_LOG_NORMAL("*len_6=%d, factor= %ld, new_byte_length= %ld\n",
                               *len_6, factor, new_byte_length);
                *len_6=(u8)new_block_size;
                /* trace */
                ZFCP_LOG_NORMAL("new *len_6=%d\n",
                                *len_6);
                break;
        case WRITE_10:
        case READ_10:
        case WRITE_VERIFY:
                len_10= (u16 *)&command_struct[7];
                factor = (unsigned long)(original_byte_length
                                       / *len_10);
                new_block_size = new_byte_length / factor;
                if(new_byte_length % factor) {
                        ZFCP_LOG_NORMAL("bug: Recalculation of command size "
                                        "failed. "
                                        "(debug info %d, %ld, %d, %ld, %ld)\n",
                                        10,
                                        original_byte_length,
                                        *len_10,
                                        new_byte_length,
                                        factor);
                        goto error;
                }
                /* TRACE */
                ZFCP_LOG_NORMAL("*len_10 = %d, factor = %ld, new_byte_length = %ld\n",
                               *len_10, factor, new_byte_length);
                *len_10=(u16)new_block_size;
                /* trace */
                ZFCP_LOG_NORMAL("new *len_10=%d\n",
                                *len_10);
                break;
        case WRITE_12:
        case READ_12:
        case WRITE_VERIFY_12:
                len_12= (u32 *)&command_struct[7];
                factor = (unsigned long)(original_byte_length
                                       / *len_12);
                new_block_size = new_byte_length / factor;
                if(new_byte_length % factor) {
                        ZFCP_LOG_NORMAL("bug: Recalculation of command size "
                                        "failed. "
                                        "(debug info %d, %ld, %d, %ld, %ld)\n",
                                        12,
                                        original_byte_length,
                                        *len_12,
                                        new_byte_length,
                                        factor);
                        goto error;
                }
                /* TRACE */
                ZFCP_LOG_NORMAL("*len_12 = %d, factor = %ld, new_byte_length = %ld\n",
                               *len_12, factor, new_byte_length);
                *len_12=(u32)new_block_size;
                /* trace */
                ZFCP_LOG_NORMAL("new *len_12=%d\n",
                                *len_12);
                break;
        default:
                /* INFO */
                ZFCP_LOG_NORMAL("Command to be truncated is not in the list of "
                              "known objects.\n");
                goto error;
                break;
        }
                goto out;
                
        error:
                retval=1;
        out:                
        /*trace*/
        ZFCP_LOG_NORMAL("truncated SCSI command:\n");
        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                      command_struct,
                      12);

        /* TRACE */
        ZFCP_LOG_NORMAL("exit (%i)\n", retval);
        
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
#endif // ZFCP_RESID

/*
 * function:    zfcp_fsf_send_fcp_command_task
 *
 * purpose:
 *
 * returns:
 *
 * note: we do not employ linked commands (not supported by HBA anyway)
 */
static int
	zfcp_fsf_send_fcp_command_task(
		zfcp_unit_t *unit,
                Scsi_Cmnd *scsi_cmnd)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
  
	zfcp_fsf_req_t *fsf_req = NULL;
	fcp_cmnd_iu_t *fcp_cmnd_iu;
	zfcp_adapter_t *adapter = unit->port->adapter;
	unsigned int sbtype;
	unsigned long lock_flags;
	int real_bytes = 0;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter devno=0x%04x, unit=0x%lx)\n",
		adapter->devno,
		(unsigned long)unit);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			adapter,
			FSF_QTCB_FCP_CMND,
			ZFCP_REQ_AUTO_CLEANUP,
                        &adapter->pool.fsf_req_scsi,
			&lock_flags,
			&fsf_req);
	if (retval < 0) {
		ZFCP_LOG_DEBUG(
			"error: Out of resources. Could not create an "
			"FCP command request for FCP_LUN 0x%016Lx connected to "
			"the port with WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			adapter->devno);
		goto failed_req_create;
	}

	/*
	 * associate FSF request with SCSI request
	 * (need this for look up on abort)
	 */
	scsi_cmnd->host_scribble = (char*) &(fsf_req->data);

	/*
	 * associate SCSI command with FSF request
	 * (need this for look up on normal command completion)
	 */
	fsf_req->data.send_fcp_command_task.scsi_cmnd = scsi_cmnd;
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 3, "fsf/sc");
	debug_event(adapter->req_dbf, 3, &fsf_req, sizeof(unsigned long));
	debug_event(adapter->req_dbf, 3, &scsi_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
#ifdef ZFCP_DEBUG_ABORTS
	fsf_req->data.send_fcp_command_task.start_jiffies = jiffies;
#endif

	fsf_req->data.send_fcp_command_task.unit = unit;
        ZFCP_LOG_DEBUG("unit=0x%lx, unit_fcp_lun=0x%Lx\n",
                       (unsigned long)unit,
                       (llui_t)unit->fcp_lun);

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* FSF does not define the structure of the FCP_CMND IU */
	fcp_cmnd_iu = (fcp_cmnd_iu_t*)
			&(fsf_req->qtcb->bottom.io.fcp_cmnd);

	/*
	 * set depending on data direction:
	 *	data direction bits in SBALE (SB Type)
	 * 	data direction bits in QTCB
	 *	data direction bits in FCP_CMND IU
         */
	switch (scsi_cmnd->sc_data_direction) {
		case SCSI_DATA_NONE:
                        ZFCP_LOG_FLAGS(3, "SCSI_DATA_NONE\n");
			fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
			/*
			 * FIXME(qdio):
			 * what is the correct type for commands
			 * without 'real' data buffers?
			 */
			sbtype = SBAL_FLAGS0_TYPE_READ;
			break;
		case SCSI_DATA_READ:
                        ZFCP_LOG_FLAGS(3, "SCSI_DATA_READ\n");
			fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_READ;
			sbtype = SBAL_FLAGS0_TYPE_READ;
			fcp_cmnd_iu->rddata = 1;
			break;
		case SCSI_DATA_WRITE:
                        ZFCP_LOG_FLAGS(3, "SCSI_DATA_WRITE\n");
			fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_WRITE;
			sbtype = SBAL_FLAGS0_TYPE_WRITE;
			fcp_cmnd_iu->wddata = 1;
			break;
		case SCSI_DATA_UNKNOWN:
			ZFCP_LOG_FLAGS(0, "SCSI_DATA_UNKNOWN not supported\n");
		default:
			/*
			 * dummy, catch this condition earlier
			 * in zfcp_scsi_queuecommand
			 */
			goto failed_scsi_cmnd;
	}

	/* set FC service class in QTCB (3 per default) */
	fsf_req->qtcb->bottom.io.service_class = adapter->fc_service_class;

	/* set FCP_LUN in FCP_CMND IU in QTCB */
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;

	/* set task attributes in FCP_CMND IU in QTCB */
	if ((scsi_cmnd->device && scsi_cmnd->device->tagged_queue) ||
	    atomic_test_mask(ZFCP_STATUS_UNIT_ASSUMETCQ, &unit->status)) {
		fcp_cmnd_iu->task_attribute = SIMPLE_Q;
		ZFCP_LOG_TRACE("setting SIMPLE_Q task attribute\n");
	} else	{
		fcp_cmnd_iu->task_attribute = UNTAGGED;
		ZFCP_LOG_TRACE("setting UNTAGGED task attribute\n");
	}

	/* set additional length of FCP_CDB in FCP_CMND IU in QTCB, if needed */
	if (scsi_cmnd->cmd_len > FCP_CDB_LENGTH) {
		fcp_cmnd_iu->add_fcp_cdb_length
			= (scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;
		ZFCP_LOG_TRACE("SCSI CDB length is 0x%x, "
				"additional FCP_CDB length is 0x%x "
				"(shifted right 2 bits)\n",
				scsi_cmnd->cmd_len,
				fcp_cmnd_iu->add_fcp_cdb_length);
	}
	/*
	 * copy SCSI CDB (including additional length, if any) to
	 * FCP_CDB in FCP_CMND IU in QTCB
	 */ 
	memcpy( fcp_cmnd_iu->fcp_cdb,
		scsi_cmnd->cmnd,
		scsi_cmnd->cmd_len);

	/* FCP CMND IU length in QTCB */
	fsf_req->qtcb->bottom.io.fcp_cmnd_length 
		= sizeof(fcp_cmnd_iu_t) +
		  fcp_cmnd_iu->add_fcp_cdb_length +
		  sizeof(fcp_dl_t);

	/* generate SBALEs from data buffer */
	real_bytes = zfcp_qdio_sbals_from_scsicmnd(
			fsf_req,
			sbtype,
			scsi_cmnd);
	if (real_bytes < 0) {
		if (fsf_req->sbal_number < ZFCP_MAX_SBALS_PER_REQ) {
			ZFCP_LOG_DEBUG(
				"Data did not fit into available buffer(s), "
				"waiting for more...\n");
			retval = -EIO;
        	} else	{
                	ZFCP_LOG_NORMAL(
				"error: Too large SCSI data buffer. "
				"Shutting down unit "
				"(devno=0x%04x, WWPN=0x%016Lx, FCP_LUN=0x%016Lx)\n",
				unit->port->adapter->devno,
				(llui_t)unit->port->wwpn,
				(llui_t)unit->fcp_lun);
			zfcp_erp_unit_shutdown(unit, 0);
			retval = -EINVAL;
		}
		goto no_fit;
        }

        /* set length of FCP data length in FCP_CMND IU in QTCB */
	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

        ZFCP_LOG_DEBUG("Sending SCSI command:\n");
        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                      (char *)scsi_cmnd->cmnd,
                      scsi_cmnd->cmd_len);

	/*
	 * start QDIO request for this FSF request
	 *  covered by an SBALE)
	 */
	{
		int i, pos;
		ZFCP_LOG_DEBUG(
			"opcode=0x%x, sbal_first=%d, "
			"sbal_curr=%d, sbal_last=%d, "
			"sbal_number=%d, sbale_curr=%d\n",
			scsi_cmnd->cmnd[0],
			fsf_req->sbal_first,
			fsf_req->sbal_curr,
			fsf_req->sbal_last,
			fsf_req->sbal_number,
			fsf_req->sbale_curr);
		for (i = 0; i < fsf_req->sbal_number; i++) {
			pos = (fsf_req->sbal_first + i) % QDIO_MAX_BUFFERS_PER_Q;
			ZFCP_HEX_DUMP(
				ZFCP_LOG_LEVEL_DEBUG,
				(char*)adapter->request_queue.buffer[pos],
				sizeof(qdio_buffer_t));
		}
	}
        retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Could not send an FCP command request "
			"for a command on the adapter with devno 0x%04x, "
                        "port WWPN 0x%016Lx and unit FCP_LUN 0x%016Lx\n",
			adapter->devno,
			(llui_t)unit->port->wwpn,
			(llui_t)unit->fcp_lun);
                goto send_failed;
	}

        ZFCP_LOG_TRACE(
		"Send FCP Command initiated "
		"(adapter devno=0x%04x, port WWPN=0x%016Lx, unit FCP_LUN=0x%016Lx)\n",
		adapter->devno,
		(llui_t)unit->port->wwpn,
		(llui_t)unit->fcp_lun);
        goto success;

send_failed:
no_fit:
failed_scsi_cmnd:
	/* dequeue new FSF request previously enqueued */
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 3, "fail_sc");
	debug_event(adapter->req_dbf, 3, &scsi_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
         
        if (zfcp_fsf_req_free(fsf_req)) {
                ZFCP_LOG_INFO(
			"error: Could not remove an FSF request from "
			"the otubound (send) list (debug info 0x%lx)\n",
			(unsigned long)fsf_req);
        }
        fsf_req = NULL;

success:
failed_req_create:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_fcp_command_task_management
 *
 * purpose:
 *
 * returns:
 *
 * FIXME(design) shouldn't this be modified to return an int
 *               also...don't know how though

 */
static zfcp_fsf_req_t*
	zfcp_fsf_send_fcp_command_task_management(
		zfcp_adapter_t *adapter,
		zfcp_unit_t *unit,
                u8 tm_flags,
		int req_flags)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
  
	zfcp_fsf_req_t *fsf_req = NULL;
	int retval = 0;
	fcp_cmnd_iu_t *fcp_cmnd_iu;
	unsigned long lock_flags;

	volatile qdio_buffer_element_t *sbale;

	ZFCP_LOG_TRACE(
		"enter (adapter devno=0x%04x, unit=0x%lx, tm_flags=0x%x, "
		"req_flags=0x%x)\n",
		adapter->devno,
		(unsigned long)unit,
		tm_flags,
		req_flags);


	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			adapter,
			FSF_QTCB_FCP_CMND,
			req_flags,
                        &adapter->pool.fsf_req_scsi,
			&lock_flags,
			&fsf_req);
	if (retval < 0) {
                ZFCP_LOG_INFO("error: Out of resources. Could not create an "
                              "FCP command (task management) request for "
                              "the adapter with devno 0x%04x, port with "
                              "WWPN 0x%016Lx and FCP_LUN 0x%016Lx.\n",
                              adapter->devno,
                              (llui_t)unit->port->wwpn,
                              (llui_t)unit->fcp_lun);
                goto out;
	}

        /* Used to decide on proper handler in the return path,
         * could be either zfcp_fsf_send_fcp_command_task_handler or
         * zfcp_fsf_send_fcp_command_task_management_handler */
        fsf_req->status|=ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT;
	/*
	 * hold a pointer to the unit being target of this
	 * task management request
	 */
	fsf_req->data.send_fcp_command_task_management.unit = unit;

	/* set FSF related fields in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;
	fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	fsf_req->qtcb->bottom.io.service_class
		= adapter->fc_service_class;
	fsf_req->qtcb->bottom.io.fcp_cmnd_length
		= sizeof(fcp_cmnd_iu_t) + sizeof(fcp_dl_t);

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);
	sbale[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
        sbale[1].flags |= SBAL_FLAGS_LAST_ENTRY; 

	/* set FCP related fields in FCP_CMND IU in QTCB */
	fcp_cmnd_iu = (fcp_cmnd_iu_t*)
			&(fsf_req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;

	/* start QDIO request for this FSF request */
        zfcp_fsf_start_scsi_er_timer(adapter);
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
                del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO(
                        "error: Could not send an FCP-command (task management) "
                        "on the adapter with devno 0x%04x, "
                        "port WWPN 0x%016Lx for unit FCP_LUN 0x%016Lx\n",
			adapter->devno,
			(llui_t)unit->port->wwpn,
			(llui_t)unit->fcp_lun);
                if (zfcp_fsf_req_free(fsf_req)){
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)fsf_req);
                        retval=-EINVAL;
                };
		fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE(
		"Send FCP Command (task management function) initiated "
		"(adapter devno=0x%04x, port WWPN=0x%016Lx, unit FCP_LUN=0x%016Lx, "
		"tm_flags=0x%x)\n",
		adapter->devno,
		(llui_t)unit->port->wwpn,
		(llui_t)unit->fcp_lun,
		tm_flags);

out:
        write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_TRACE("exit (0x%lx)\n", (unsigned long)fsf_req);
 
	return fsf_req;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_fcp_command_handler
 *
 * purpose:	is called for finished Send FCP Command
 *
 * returns:	
 */
static int
	zfcp_fsf_send_fcp_command_handler(
		zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
        zfcp_unit_t *unit;
	fsf_qtcb_header_t *header = &fsf_req->qtcb->header;
	u16 subtable, rule, counter;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT) 
		unit = fsf_req->data.send_fcp_command_task_management.unit;
	else	unit = fsf_req->data.send_fcp_command_task.unit;
        
	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* go directly to calls of special handlers */ 
                goto skip_fsfstatus;
        }

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

        case FSF_PORT_HANDLE_NOT_VALID:
                ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
                ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
				"for the port with WWPN 0x%016Lx connected to "
                                "the adapter of devno 0x%04x is not valid.\n",
				unit->port->handle,
				(llui_t)unit->port->wwpn,
                                unit->port->adapter->devno);
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_phand_nv");
                zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"porthinv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                
        case FSF_LUN_HANDLE_NOT_VALID:
                ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
                ZFCP_LOG_INFO("Temporary LUN identifier (handle) 0x%x "
                              "of the logical unit with FCP_LUN 0x%016Lx at "
                              "the remote port with WWPN 0x%016Lx connected "
                              "to the adapter with devno 0x%04x is " 
                              "not valid. This may happen occasionally.\n",
                              unit->handle,
                              (llui_t)unit->fcp_lun,
                              (llui_t)unit->port->wwpn,
                              unit->port->adapter->devno);
                ZFCP_LOG_NORMAL("Status qualifier data:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_uhand_nv");
                zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf(
			"lunhinv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

        case FSF_HANDLE_MISMATCH:
                ZFCP_LOG_FLAGS(0, "FSF_HANDLE_MISMATCH\n");
                ZFCP_LOG_NORMAL("bug: The port handle (temporary port "
                                "identifier) 0x%x has changed unexpectedly. " 
				"This was detected upon receiveing the response "
                                "of a command send to the unit with FCP_LUN "
                                "0x%016Lx at the remote port with WWPN 0x%016Lx "
                                "connected to the adapter with devno 0x%04x.\n",
				unit->port->handle,
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
                ZFCP_LOG_NORMAL("status qualifier:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_hand_mis");
                zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"handmism", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

        case FSF_SERVICE_CLASS_NOT_SUPPORTED :
                ZFCP_LOG_FLAGS(0, "FSF_SERVICE_CLASS_NOT_SUPPORTED\n");
                if(fsf_req->adapter->fc_service_class <= 3) {
                        ZFCP_LOG_NORMAL( "error: The adapter with devno=0x%04x does "
                                         "not support fibre-channel class %d.\n",
                                         unit->port->adapter->devno,
                                         fsf_req->adapter->fc_service_class);
                } else {
                        ZFCP_LOG_NORMAL(
                                        "bug: The fibre channel class at the adapter "
                                        "with devno 0x%04x is invalid. "
                                        "(debug info %d)\n",
                                        unit->port->adapter->devno,
                                        fsf_req->adapter->fc_service_class);
                }
                /* stop operation for this adapter */
                debug_text_exception(fsf_req->adapter->erp_dbf,0,"fsf_s_class_nsup");
                zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"unsclass", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

        case FSF_FCPLUN_NOT_VALID:
                ZFCP_LOG_FLAGS(0, "FSF_FCPLUN_NOT_VALID\n");
                ZFCP_LOG_NORMAL("bug: The FCP_LUN 0x%016Lx behind the remote port "
				"of WWPN 0x%016Lx via the adapter with "
                                "devno 0x%04x does not have the correct unit "
                                "handle (temporary unit identifier) 0x%x\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno,
				unit->handle);
                ZFCP_LOG_DEBUG("status qualifier:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_s_fcp_lun_nv");
                zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf(
			"fluninv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                

	case FSF_ACCESS_DENIED :
		ZFCP_LOG_FLAGS(2, "FSF_ACCESS_DENIED\n");
		ZFCP_LOG_NORMAL("Access denied, cannot send FCP command "
				"(devno=0x%04x wwpn=0x%016Lx lun=0x%016Lx)\n",
				unit->port->adapter->devno,
				(llui_t)unit->port->wwpn,
				(llui_t)unit->fcp_lun);
		for (counter = 0; counter < 2; counter++) {
			subtable = header->fsf_status_qual.halfword[counter * 2];
			rule = header->fsf_status_qual.halfword[counter * 2 + 1];
			switch (subtable) {
			case FSF_SQ_CFDC_SUBTABLE_OS:
			case FSF_SQ_CFDC_SUBTABLE_PORT_WWPN:
			case FSF_SQ_CFDC_SUBTABLE_PORT_DID:
			case FSF_SQ_CFDC_SUBTABLE_LUN:
				ZFCP_LOG_INFO("Access denied (%s rule %d)\n",
					zfcp_act_subtable_type[subtable], rule);
				break;
			}
		}
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_access");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

        case FSF_DIRECTION_INDICATOR_NOT_VALID:
                ZFCP_LOG_FLAGS(0, "FSF_DIRECTION_INDICATOR_NOT_VALID\n");
                ZFCP_LOG_INFO("bug: Invalid data direction given for the unit "
                              "with FCP_LUN 0x%016Lx at the remote port with "
                              "WWPN 0x%016Lx via the adapter with devno 0x%04x "
                              "(debug info %d)\n",
                              (llui_t)unit->fcp_lun,
                              (llui_t)unit->port->wwpn,
                              unit->port->adapter->devno,
                              fsf_req->qtcb->bottom.io.data_direction);
                /* stop operation for this adapter */
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_dir_ind_nv");
                zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"dirinv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

	/* FIXME: this should be obsolete, isn' it? */
        case FSF_INBOUND_DATA_LENGTH_NOT_VALID:
                ZFCP_LOG_FLAGS(0, "FSF_INBOUND_DATA_LENGTH_NOT_VALID\n");
                ZFCP_LOG_NORMAL("bug: An invalid inbound data length field "
				"was found in a command for the unit with "
                                "FCP_LUN 0x%016Lx of the remote port "
				"with WWPN 0x%016Lx via the adapter with "
                                "devno 0x%04x\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
                /* stop operation for this adapter */
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_in_dl_nv");
                zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"odleninv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

	/* FIXME: this should be obsolete, isn' it? */
        case FSF_OUTBOUND_DATA_LENGTH_NOT_VALID:
                ZFCP_LOG_FLAGS(0, "FSF_OUTBOUND_DATA_LENGTH_NOT_VALID\n");
                ZFCP_LOG_NORMAL("bug: An invalid outbound data length field "
				"was found in a command for the unit with "
                                "FCP_LUN 0x%016Lx of the remote port "
				"with WWPN 0x%016Lx via the adapter with "
                                "devno 0x%04x\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
                /* stop operation for this adapter */
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_out_dl_nv");
                zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"idleninv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;

        case FSF_CMND_LENGTH_NOT_VALID:
                ZFCP_LOG_FLAGS(0, "FSF_CMND_LENGTH_NOT_VALID\n");
                ZFCP_LOG_NORMAL("bug: An invalid control-data-block length field "
				"was found in a command for the unit with "
                                "FCP_LUN 0x%016Lx of the remote port "
				"with WWPN 0x%016Lx via the adapter with "
                                "devno 0x%04x (debug info %d)\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno,
				fsf_req->qtcb->bottom.io.fcp_cmnd_length);
                /* stop operation for this adapter */
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_cmd_len_nv");
                zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf(
			"cleninv", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                break;
                
        case FSF_PORT_BOXED :
                ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
                ZFCP_LOG_DEBUG("The remote port "
                               "with WWPN 0x%016Lx on the adapter with "
                               "devno 0x%04x needs to be reopened\n",
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno);
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_pboxed");
                zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf(
			"portbox", fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
                        | ZFCP_STATUS_FSFREQ_RETRY;
                break;
                        

	case FSF_LUN_BOXED :
		ZFCP_LOG_FLAGS(0, "FSF_LUN_BOXED\n");
		ZFCP_LOG_NORMAL(
			"The remote unit needs to be reopened "
			"(devno=0x%04x wwpn=0x%016Lx lun=0x%016Lx)\n",
			unit->port->adapter->devno,
			(llui_t)unit->port->wwpn,
			(llui_t)unit->fcp_lun);
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_lboxed");
		zfcp_erp_unit_reopen(unit, 0);
		zfcp_cmd_dbf_event_fsf(
			"unitbox",
			fsf_req,
			&fsf_req->qtcb->header.fsf_status_qual,
			sizeof(fsf_status_qual_t));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
			| ZFCP_STATUS_FSFREQ_RETRY;
		break;

        case FSF_ADAPTER_STATUS_AVAILABLE :
                ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
                switch (fsf_req->qtcb->header.fsf_status_qual.word[0]){
                case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE :
                        ZFCP_LOG_FLAGS(2, "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
                        /* re-establish link to port */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ltest");
                        zfcp_erp_port_reopen(unit->port, 0);
			zfcp_cmd_dbf_event_fsf(
				"sqltest", fsf_req,
				&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;                        
                        break;
                case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED :
                        ZFCP_LOG_FLAGS(3, "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
                        /* FIXME(hw) need proper specs for proper action */
                        /* let scsi stack deal with retries and escalation */
                        debug_text_event(fsf_req->adapter->erp_dbf,1,"fsf_sq_ulp");
			zfcp_cmd_dbf_event_fsf(
				"sqdeperp", fsf_req,
				&fsf_req->qtcb->header.fsf_status_qual, sizeof(fsf_status_qual_t));
                        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
                        break;
                default:
			/* FIXME: shall we consider this a successful transfer? */
                        ZFCP_LOG_NORMAL("bug: Wrong status qualifier 0x%x arrived.\n",
                                        fsf_req->qtcb->header.fsf_status_qual.word[0]);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_sq_inval:");
                        debug_exception(fsf_req->adapter->erp_dbf,0,
                                        &fsf_req->qtcb->header.fsf_status_qual.word[0],
                                        sizeof(u32));
                        break;
                }
                break;
                
	case FSF_GOOD:
                ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		break;

        case FSF_FCP_RSP_AVAILABLE:
                ZFCP_LOG_FLAGS(2, "FSF_FCP_RSP_AVAILABLE\n");
                break;

	default :
                debug_text_event(fsf_req->adapter->erp_dbf,0,"fsf_s_inval:");
                debug_exception(fsf_req->adapter->erp_dbf,0,
                                &fsf_req->qtcb->header.fsf_status,
                                sizeof(u32));
                break;
	}

skip_fsfstatus:
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT) {
		retval = zfcp_fsf_send_fcp_command_task_management_handler(
				fsf_req);
	} else	{
		retval = zfcp_fsf_send_fcp_command_task_handler(
				fsf_req);
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_fcp_command_task_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int zfcp_fsf_send_fcp_command_task_handler(
		zfcp_fsf_req_t *fsf_req)

{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;

	Scsi_Cmnd *scpnt;
	fcp_rsp_iu_t *fcp_rsp_iu =
		(fcp_rsp_iu_t*)
		&(fsf_req->qtcb->bottom.io.fcp_rsp);
	fcp_cmnd_iu_t *fcp_cmnd_iu =
		(fcp_cmnd_iu_t*)
		&(fsf_req->qtcb->bottom.io.fcp_cmnd);
	u32 sns_len;
        char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	unsigned long flags;
        zfcp_unit_t *unit = fsf_req->data.send_fcp_command_task.unit;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	read_lock_irqsave(&fsf_req->adapter->abort_lock, flags);
        scpnt = fsf_req->data.send_fcp_command_task.scsi_cmnd;
        if (!scpnt) {
                ZFCP_LOG_DEBUG("Command with fsf_req 0x%lx is not associated to "
                               "a scsi command anymore. Aborted?\n",
                               (unsigned long)fsf_req);
                goto out;
        }

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTED) {
		/* FIXME: (design) mid-layer should handle DID_ABORT like
		 *        DID_SOFT_ERROR by retrying the request for devices
		 *        that allow retries.
		 */
                ZFCP_LOG_DEBUG("Setting DID_SOFT_ERROR and SUGGEST_RETRY\n");
		scpnt->result |= DID_SOFT_ERROR << 16 |
				 SUGGEST_RETRY << 24;
		goto skip_fsfstatus;
        }

        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
                ZFCP_LOG_DEBUG("Setting DID_ERROR\n");
		scpnt->result |= DID_ERROR << 16;
                goto skip_fsfstatus;
        }

	/* set message byte of result in SCSI command */
	scpnt->result |= COMMAND_COMPLETE << 8;

	/*
	 * copy SCSI status code of FCP_STATUS of FCP_RSP IU to status byte
	 * of result in SCSI command
	 */
	scpnt->result |= fcp_rsp_iu->scsi_status;
        if(fcp_rsp_iu->scsi_status) {
                /* DEBUG */
                ZFCP_LOG_DEBUG("status for SCSI Command:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                              scpnt->cmnd,
                              scpnt->cmd_len);
                ZFCP_LOG_DEBUG("SCSI status code 0x%x\n",
                               fcp_rsp_iu->scsi_status);
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
                              (void *)fcp_rsp_iu,
                              sizeof(fcp_rsp_iu_t));
                ZFCP_HEX_DUMP(
                              ZFCP_LOG_LEVEL_DEBUG,
                              zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu),
                              fcp_rsp_iu->fcp_sns_len);
        }

	/* check FCP_RSP_INFO */
	if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid) {
		ZFCP_LOG_DEBUG("rsp_len is valid\n");
		switch (fcp_rsp_info[3]) {
			case RSP_CODE_GOOD:
                                ZFCP_LOG_FLAGS(3, "RSP_CODE_GOOD\n");
				/* ok, continue */
				ZFCP_LOG_TRACE(
					"no failure or Task Management "
					"Function complete\n");
				scpnt->result |= DID_OK << 16;
				break;
			case RSP_CODE_LENGTH_MISMATCH:
                                ZFCP_LOG_FLAGS(0, "RSP_CODE_LENGTH_MISMATCH\n");
				/* hardware bug */
				ZFCP_LOG_NORMAL(
					"bug: FCP response code indictates "
                                        " that the fibre-channel protocol data "
                                        "length differs from the burst "
                                        "length. The problem occured on the unit "
                                        "with FCP_LUN 0x%016Lx connected to the "
                                        "port with WWPN 0x%016Lx at the adapter with "
                                        "devno 0x%04x\n",
                                        (llui_t)unit->fcp_lun,
                                        (llui_t)unit->port->wwpn,
                                        unit->port->adapter->devno);
				/* dump SCSI CDB as prepared by zfcp */
                                ZFCP_HEX_DUMP(
					ZFCP_LOG_LEVEL_DEBUG,
					(char*)&fsf_req->qtcb->
					bottom.io.fcp_cmnd,
					FSF_FCP_CMND_SIZE);
				zfcp_cmd_dbf_event_fsf("clenmism", fsf_req, NULL, 0);
				scpnt->result |= DID_ERROR << 16;
                                goto skip_fsfstatus;
			case RSP_CODE_FIELD_INVALID:
                                ZFCP_LOG_FLAGS(0, "RSP_CODE_FIELD_INVALID\n");
				/* driver or hardware bug */
				ZFCP_LOG_NORMAL(
					"bug: FCP response code indictates "
                                        "that the fibre-channel protocol data "
                                        "fields were incorrectly set-up. "
                                        "The problem occured on the unit "
                                        "with FCP_LUN 0x%016Lx connected to the "
                                        "port with WWPN 0x%016Lx at the adapter with "
                                        "devno 0x%04x\n",
                                        (llui_t)unit->fcp_lun,
                                        (llui_t)unit->port->wwpn,
                                        unit->port->adapter->devno);
				/* dump SCSI CDB as prepared by zfcp */
                                ZFCP_HEX_DUMP(
					ZFCP_LOG_LEVEL_DEBUG,
					(char*)&fsf_req->qtcb->
					bottom.io.fcp_cmnd,
					FSF_FCP_CMND_SIZE);
				zfcp_cmd_dbf_event_fsf("codeinv", fsf_req, NULL, 0);
				scpnt->result |= DID_ERROR << 16;
                                goto skip_fsfstatus;
			case RSP_CODE_RO_MISMATCH:
                                ZFCP_LOG_FLAGS(0, "RSP_CODE_RO_MISMATCH\n");
				/* hardware bug */
				ZFCP_LOG_NORMAL(
					"bug: The FCP response code indicates "
                                        "that conflicting  values for the "
                                        "fibre-channel payload offset from the "
                                        "header were found. "
                                        "The problem occured on the unit "
                                        "with FCP_LUN 0x%016Lx connected to the "
                                        "port with WWPN 0x%016Lx at the adapter with "
                                        "devno 0x%04x\n",
                                        (llui_t)unit->fcp_lun,
                                        (llui_t)unit->port->wwpn,
                                        unit->port->adapter->devno);
				/* dump SCSI CDB as prepared by zfcp */
                                ZFCP_HEX_DUMP(
					ZFCP_LOG_LEVEL_DEBUG,
					(char*)&fsf_req->qtcb->
					bottom.io.fcp_cmnd,
					FSF_FCP_CMND_SIZE);
				zfcp_cmd_dbf_event_fsf("codemism", fsf_req, NULL, 0);
				scpnt->result |= DID_ERROR << 16;
                                goto skip_fsfstatus;
			default :
				ZFCP_LOG_NORMAL(
                                      "bug: An invalid FCP response "
                                      "code was detected for a command. "
                                      "The problem occured on the unit "
                                      "with FCP_LUN 0x%016Lx connected to the "
                                      "port with WWPN 0x%016Lx at the adapter with "
                                      "devno 0x%04x (debug info 0x%x)\n",
                                      (llui_t)unit->fcp_lun,
                                      (llui_t)unit->port->wwpn,
                                      unit->port->adapter->devno,
                                      fcp_rsp_info[3]);
				/* dump SCSI CDB as prepared by zfcp */
                                ZFCP_HEX_DUMP(
					ZFCP_LOG_LEVEL_DEBUG,
					(char*)&fsf_req->qtcb->
					bottom.io.fcp_cmnd,
					FSF_FCP_CMND_SIZE);
				zfcp_cmd_dbf_event_fsf("undeffcp", fsf_req, NULL, 0);
				scpnt->result |= DID_ERROR << 16;
		}
	}

	/* check for sense data */
	if (fcp_rsp_iu->validity.bits.fcp_sns_len_valid) {
		sns_len = FSF_FCP_RSP_SIZE -
			  sizeof(fcp_rsp_iu_t) +
			  fcp_rsp_iu->fcp_rsp_len;
		ZFCP_LOG_TRACE(
			"room for %i bytes sense data in QTCB\n",
			sns_len);
		sns_len = min(sns_len, (u32)SCSI_SENSE_BUFFERSIZE);
		ZFCP_LOG_TRACE(
			"room for %i bytes sense data in SCSI command\n",
			SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);
                ZFCP_LOG_TRACE("scpnt->result =0x%x, command was:\n",
                               scpnt->result);
                ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(void *)&scpnt->cmnd,
                        scpnt->cmd_len);

		ZFCP_LOG_TRACE(
			"%i bytes sense data provided by FCP\n",
			fcp_rsp_iu->fcp_sns_len);
		memcpy(	&scpnt->sense_buffer,
			zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu),
			sns_len);
                ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(void *)&scpnt->sense_buffer,
                        sns_len);
	}

	/* check for overrun */
	if (fcp_rsp_iu->validity.bits.fcp_resid_over) {
		ZFCP_LOG_INFO(
                        "A data overrun was detected for a command. "
                        "This happened for a command to the unit "
                        "with FCP_LUN 0x%016Lx connected to the "
                        "port with WWPN 0x%016Lx at the adapter with "
                        "devno 0x%04x. The response data length is "
                        "%d, the original length was %d.\n",
                        (llui_t)unit->fcp_lun,
                        (llui_t)unit->port->wwpn,
                        unit->port->adapter->devno,
			fcp_rsp_iu->fcp_resid,
                        zfcp_get_fcp_dl(fcp_cmnd_iu));
	}

	/* check for underrun */ 
	if (fcp_rsp_iu->validity.bits.fcp_resid_under) {
		ZFCP_LOG_DEBUG(
                        "A data underrun was detected for a command. "
                        "This happened for a command to the unit "
                        "with FCP_LUN 0x%016Lx connected to the "
                        "port with WWPN 0x%016Lx at the adapter with "
                        "devno 0x%04x. The response data length is "
                        "%d, the original length was %d.\n",
                        (llui_t)unit->fcp_lun,
                        (llui_t)unit->port->wwpn,
                        unit->port->adapter->devno,
			fcp_rsp_iu->fcp_resid,
                        zfcp_get_fcp_dl(fcp_cmnd_iu));
                /*
                 * It may not have been possible to send all data and the
                 * underrun on send may already be in scpnt->resid, so it's add
                 * not equals in the below statement.
                 */
		scpnt->resid += fcp_rsp_iu->fcp_resid;
                ZFCP_LOG_TRACE("scpnt->resid=0x%x\n",
                                scpnt->resid);
	}

skip_fsfstatus:
#if 0
	/*
	 * This nasty chop at the problem is not working anymore
	 * as we do not adjust the retry count anylonger in order
	 * to have a number of retries that avoids I/O errors.
	 * The manipulation of the retry count has been removed
	 * in favour of a safe tape device handling. We must not
	 * sent SCSI commands more than once to a device if no
	 * retries are permitted by the high level driver. Generally
	 * speaking, it was a mess to change retry counts. So it is
	 * fine that this sort of workaround is gone.
	 * Then, we had to face a certain number of immediate retries in case of
	 * busy and queue full conditions (see below).
	 * This is not acceptable
	 * for the latter. Queue full conditions are used
	 * by devices to indicate to a host that the host can rely
	 * on the completion (or timeout) of at least one outstanding
	 * command as a suggested trigger for command retries.
	 * Busy conditions require a different trigger since
	 * no commands are outstanding for that initiator from the
	 * devices perspective.
	 * The drawback of mapping a queue full condition to a
	 * busy condition is the chance of wasting all retries prior
	 * to the time when the device indicates that a command
	 * rejected due to a queue full condition should be re-driven.
	 * This case would lead to unnecessary I/O errors that
	 * have to be considered fatal if for example ext3's
	 * journaling would be torpedoed by such an avoidable
	 * I/O error.
	 * So, what issues are there with not mapping a queue-full
	 * condition to a busy condition?
	 * Due to the 'exclusive LUN'
	 * policy enforced by the zSeries FCP channel, this 
	 * Linux instance is the only initiator with regard to
	 * this adapter. It is safe to rely on the information
	 * 'don't disturb me now ... and btw. no other commands
	 * pending for you' (= queue full) sent by the LU,
	 * since no other Linux can use this LUN via this adapter
	 * at the same time. If there is a potential race
	 * introduced by the FCP channel by not inhibiting Linux A
	 * to give up a LU with commands pending while Linux B
	 * grabs this LU and sends commands  - thus providing
	 * an exploit at the 'exclusive LUN' policy - then this
	 * issue has to be considered a hardware problem. It should
	 * be tracked as such if it really occurs. Even if the
	 * FCP Channel spec. begs exploiters to wait for the
	 * completion of all request sent to a LU prior to
	 * closing this LU connection.
	 * This spec. statement in conjunction with
	 * the 'exclusive LUN' policy is not consistent design.
	 * Another issue is how resource constraints for SCSI commands
	 * might be handled by the FCP channel (just guessing for now).
	 * If the FCP channel would always map resource constraints,
	 * e.g. no free FC exchange ID due to I/O stress caused by
	 * other sharing Linux instances, to faked queue-full
	 * conditions then this would be a misinterpretation and
	 * violation of SCSI standards.
	 * If there are SCSI stack races as indicated below
	 * then they need to be fixed just there.
	 * Providing all issue above are not applicable or will
	 * be fixed appropriately, removing the following hack
	 * is the right thing to do.
	 */

	/*
	 * Note: This is a rather nasty chop at the problem. We cannot 
	 * risk adding to the mlqueue however as this will block the 
	 * device. If it is the last outstanding command for this host
	 * it will remain blocked indefinitely. This would be quite possible
	 * on the zSeries FCP adapter.
	 * Also, there exists a race with scsi_insert_special relying on 
	 * scsi_request_fn to recalculate some command data which may not 
	 * happen when q->plugged is true in scsi_request_fn
	 */
	if (status_byte(scpnt->result) == QUEUE_FULL) {
		ZFCP_LOG_DEBUG("Changing QUEUE_FULL to BUSY....\n");
		scpnt->result &= ~(QUEUE_FULL << 1);
		scpnt->result |= (BUSY << 1);
	}
#endif

        ZFCP_LOG_DEBUG("scpnt->result =0x%x\n",
                       scpnt->result);

	zfcp_cmd_dbf_event_scsi("response", fsf_req->adapter, scpnt);

	/* cleanup pointer (need this especially for abort) */
	scpnt->host_scribble = NULL;
	scpnt->SCp.ptr = (char*)0;

	/*
	 * NOTE:
	 * according to the outcome of a discussion on linux-scsi we
	 * don't need to grab the io_request_lock here since we use
	 * the new eh
	 */
	/* always call back */
#ifdef ZFCP_DEBUG_REQUESTS
        debug_text_event(fsf_req->adapter->req_dbf, 2, "ok_done:");
        debug_event(fsf_req->adapter->req_dbf, 2, &scpnt, sizeof(unsigned long));
        debug_event(fsf_req->adapter->req_dbf, 2, &scpnt->scsi_done, 
                      sizeof(unsigned long));
        debug_event(fsf_req->adapter->req_dbf, 2, &fsf_req, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
        (scpnt->scsi_done)(scpnt);
 	/*
	 * We must hold this lock until scsi_done has been called.
	 * Otherwise we may call scsi_done after abort regarding this
	 * command has completed.
	 * Note: scsi_done must not block!
	 */
out:
	read_unlock_irqrestore(&fsf_req->adapter->abort_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_fcp_command_task_management_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int zfcp_fsf_send_fcp_command_task_management_handler(
     zfcp_fsf_req_t *fsf_req)

{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
	fcp_rsp_iu_t *fcp_rsp_iu =
		(fcp_rsp_iu_t*)
		&(fsf_req->qtcb->bottom.io.fcp_rsp);
        char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
        zfcp_unit_t *unit = fsf_req->data.send_fcp_command_task_management.unit;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

        del_timer(&fsf_req->adapter->scsi_er_timer);
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED; 
                goto skip_fsfstatus;
        }

	/* check FCP_RSP_INFO */
	switch (fcp_rsp_info[3]) {
		case RSP_CODE_GOOD:
                        ZFCP_LOG_FLAGS(3, "RSP_CODE_GOOD\n");
			/* ok, continue */
			ZFCP_LOG_DEBUG(
				"no failure or Task Management "
				"Function complete\n");
			break;
		case RSP_CODE_TASKMAN_UNSUPP:
                        ZFCP_LOG_FLAGS(0, "RSP_CODE_TASKMAN_UNSUPP\n");
			ZFCP_LOG_NORMAL(
				"bug: A reuested task management function "
                                "is not supported on the target device "
                                "The corresponding device is the unit with "
                                "FCP_LUN 0x%016Lx at the port "
                                "with WWPN 0x%016Lx at the adapter with devno "
                                "0x%04x\n",
                                (llui_t)unit->fcp_lun,
                                (llui_t)unit->port->wwpn,
                                unit->port->adapter->devno);
                        fsf_req->status
				|= ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP;
			break;
		case RSP_CODE_TASKMAN_FAILED:
                        ZFCP_LOG_FLAGS(0, "RSP_CODE_TASKMAN_FAILED\n");
			ZFCP_LOG_NORMAL(
				"bug: A reuested task management function "
                                "failed to complete successfully. "
                                "The corresponding device is the unit with "
                                "FCP_LUN 0x%016Lx at the port "
                                "with WWPN 0x%016Lx at the adapter with devno "
                                "0x%04x\n",
                                (llui_t)unit->fcp_lun,
                                (llui_t)unit->port->wwpn,
                                unit->port->adapter->devno);
			fsf_req->status
				|= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
			break;
		default :
                        ZFCP_LOG_NORMAL(
                               "bug: An invalid FCP response "
                               "code was detected for a command. "
                               "The problem occured on the unit "
                               "with FCP_LUN 0x%016Lx connected to the "
                               "port with WWPN 0x%016Lx at the adapter with "
                               "devno 0x%04x (debug info 0x%x)\n",
                               (llui_t)unit->fcp_lun,
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno,
                               fcp_rsp_info[3]);
			fsf_req->status
                                |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
	}

skip_fsfstatus:

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_req_wait_and_cleanup
 *
 * purpose:
 *
 * FIXME(design): signal seems to be <0 !!!
 * returns:	0	- request completed (*status is valid), cleanup succeeded
 *		<0	- request completed (*status is valid), cleanup failed
 *		>0	- signal which interrupted waiting (*status is not valid),
 *			  request not completed, no cleanup
 *
 *		*status is a copy of status of completed fsf_req
 */
static int zfcp_fsf_req_wait_and_cleanup(
		zfcp_fsf_req_t *fsf_req,
		int interruptible,
		u32 *status)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
	int signal = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx,"
		"interruptible=%d, *status=0x%x\n",
		(unsigned long)fsf_req,
		interruptible,
		*status);

	if (interruptible) {
		__wait_event_interruptible(
			fsf_req->completion_wq,
			fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED,
			signal);
		if (signal) {
			ZFCP_LOG_DEBUG(
				"Caught signal %i while waiting for the "
                                "completion of the request at 0x%lx\n",
				signal,
				(unsigned long)fsf_req);
			retval = signal;
			goto out;
		}
	} else	{
		__wait_event(
			fsf_req->completion_wq,
			fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	}

	*status = fsf_req->status;

	/* cleanup request */
	retval = zfcp_fsf_req_cleanup(fsf_req);
out: 
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static inline int zfcp_fsf_req_create_sbal_check(
		unsigned long *flags,
		zfcp_qdio_queue_t *queue,
		int needed)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

        write_lock_irqsave(&queue->queue_lock, *flags);
	if (atomic_read(&queue->free_count) >= needed) 
		return 1;
        write_unlock_irqrestore(&queue->queue_lock, *flags);
	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * set qtcb pointer in fsf_req and initialize QTCB
 */
static inline void zfcp_fsf_req_qtcb_init(zfcp_fsf_req_t *fsf_req, u32 fsf_cmd)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
	if (fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS) {
                struct zfcp_fsf_req_pool_buffer *data =
                        (struct zfcp_fsf_req_pool_buffer *) fsf_req;
                fsf_req->qtcb = &data->qtcb;
        }

	if (fsf_req->qtcb) {
	        ZFCP_LOG_TRACE("fsf_req->qtcb=0x%lx\n",
                               (unsigned long ) fsf_req->qtcb);
		fsf_req->qtcb->prefix.req_id = (unsigned long)fsf_req;
		fsf_req->qtcb->prefix.ulp_info = zfcp_data.driver_version;
		fsf_req->qtcb->prefix.qtcb_type = fsf_qtcb_type[fsf_cmd];
		fsf_req->qtcb->prefix.qtcb_version = ZFCP_QTCB_VERSION;
		fsf_req->qtcb->header.req_handle = (unsigned long)fsf_req;
		fsf_req->qtcb->header.fsf_command = fsf_cmd;
		/* Request Sequence Number is set later when the request is
                   actually sent. */
	}
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * try to get needed SBALs in request queue
 * (get queue lock on success)
 */
static int zfcp_fsf_req_sbal_get(zfcp_adapter_t *adapter, int req_flags,
                                 unsigned long *lock_flags)
{
        int condition;
        unsigned long timeout = ZFCP_SBAL_TIMEOUT;
        zfcp_qdio_queue_t *req_queue = &adapter->request_queue;

        if (req_flags & ZFCP_WAIT_FOR_SBAL) {
                ZFCP_WAIT_EVENT_TIMEOUT(adapter->request_wq, timeout,
                                        (condition =
                                         (zfcp_fsf_req_create_sbal_check)
                                         (lock_flags, req_queue, 1)));
                if (!condition)
                        return -EIO;
        } else if (!zfcp_fsf_req_create_sbal_check(lock_flags, req_queue, 1))
                return -EIO;

        return 0;
}


/*
 * function:    zfcp_fsf_req_create
 *
 * purpose:	create an FSF request at the specified adapter and
 *		setup common fields
 *
 * returns:	-ENOMEM if there was insufficient memory for a request
 *              -EIO if no qdio buffers could be allocate to the request
 *              -EINVAL/-EPERM on bug conditions in req_dequeue
 *              0 in success
 *
 * note:        The created request is returned by reference.
 *
 * locks:	lock of concerned request queue must not be held,
 *		but is held on completion (write, irqsave)
 */
static int zfcp_fsf_req_create(zfcp_adapter_t *adapter, u32 fsf_cmd,
                               int req_flags, zfcp_mem_pool_t *mem_pool,
                               unsigned long *lock_flags,
                               zfcp_fsf_req_t **fsf_req_p)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
	zfcp_fsf_req_t *fsf_req = NULL;
       	int retval=0;
        zfcp_qdio_queue_t *req_queue = &adapter->request_queue;
        volatile qdio_buffer_element_t *sbale;

        ZFCP_LOG_TRACE("enter (adapter=0x%lx fsf_cmd=0x%x *lock_flags=0x%lx "
                       "req_flags=0x%x)\n", (unsigned long)adapter,
                       fsf_cmd, *lock_flags, req_flags);

        atomic_inc(&adapter->reqs_in_progress);

	/* allocate new FSF request */
	fsf_req = zfcp_fsf_req_alloc(mem_pool, req_flags, GFP_ATOMIC);
	if (!fsf_req) {
                ZFCP_LOG_DEBUG(
			"error: Could not put an FSF request into"
                        "the outbound (send) queue.\n");
                retval=-ENOMEM;
		goto failed_fsf_req;
	}

        zfcp_fsf_req_qtcb_init(fsf_req, fsf_cmd);

	/* initialize waitqueue which may be used to wait on 
	   this request completion */
	init_waitqueue_head(&fsf_req->completion_wq);

        retval = zfcp_fsf_req_sbal_get(adapter, req_flags, lock_flags);
        if(retval < 0)
                goto failed_sbals;

        /*
         * We hold queue_lock here. Check if QDIOUP is set and let request fail
         * if it is not set (see also *_open_qdio and *_close_qdio).
         */

        if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
                write_unlock_irqrestore(&req_queue->queue_lock, *lock_flags);
                goto failed_sbals;
        }

#ifndef ZFCP_PARANOIA_DEAD_CODE
	/* set magics */
	fsf_req->common_magic = ZFCP_MAGIC;
	fsf_req->specific_magic = ZFCP_MAGIC_FSFREQ;
#endif
	fsf_req->adapter = adapter;
	fsf_req->fsf_command = fsf_cmd;
	fsf_req->sbal_number = 1;
	fsf_req->sbal_first = req_queue->free_index;
	fsf_req->sbal_curr = req_queue->free_index;
        fsf_req->sbale_curr = 1;

	if (req_flags & ZFCP_REQ_AUTO_CLEANUP)
		fsf_req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;

	sbale = zfcp_qdio_sbale_req(fsf_req, fsf_req->sbal_curr, 0);

	/* setup common SBALE fields */
	sbale[0].addr = fsf_req;
	sbale[0].flags |= SBAL_FLAGS0_COMMAND;
	if (fsf_req->qtcb != 0) {
		sbale[1].addr = (void *)fsf_req->qtcb;
		sbale[1].length = sizeof(fsf_qtcb_t);
	}

	ZFCP_LOG_TRACE("got %i free BUFFERs starting at index %i\n",
                       fsf_req->sbal_number, fsf_req->sbal_first);

	goto success;

 failed_sbals:
#ifdef ZFCP_STAT_QUEUES
        atomic_inc(&adapter->outbound_queue_full);
#endif
        /* dequeue new FSF request previously enqueued */
        zfcp_fsf_req_free(fsf_req);
        fsf_req = NULL;
        
 failed_fsf_req:
        //failed_running:
        write_lock_irqsave(&req_queue->queue_lock, *lock_flags);

 success: 
        *fsf_req_p = fsf_req;
        ZFCP_LOG_TRACE("exit (%d)\n", retval);
	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static inline int zfcp_qdio_determine_pci(zfcp_qdio_queue_t *req_queue, 
                                          zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_QDIO
        int new_distance_from_int;
        int pci_pos;
	volatile qdio_buffer_element_t *sbale;

        ZFCP_LOG_TRACE("enter (0x%lx, 0x%lx)\n", 
                       (unsigned long)req_queue,
                       (unsigned long)fsf_req);

        new_distance_from_int = req_queue->distance_from_int +
                fsf_req->sbal_number;

        if (new_distance_from_int >= ZFCP_QDIO_PCI_INTERVAL) {
                new_distance_from_int %= ZFCP_QDIO_PCI_INTERVAL;
                pci_pos  = fsf_req->sbal_first;
		pci_pos += fsf_req->sbal_number;
		pci_pos -= new_distance_from_int;
		pci_pos -= 1;
		pci_pos %= QDIO_MAX_BUFFERS_PER_Q;
		sbale = zfcp_qdio_sbale_req(fsf_req, pci_pos, 0);
		sbale->flags |= SBAL_FLAGS0_PCI;
                ZFCP_LOG_DEBUG(
			"Setting PCI flag at pos %d (0x%lx)\n",
			pci_pos,
			(unsigned long)sbale);
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_TRACE,
			(char*)sbale,
			sizeof(qdio_buffer_t));
        }

        ZFCP_LOG_TRACE("exit (%d)\n", new_distance_from_int);
	return new_distance_from_int;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}



/*
 * function:    zfcp_fsf_req_send
 *
 * purpose:	start transfer of FSF request via QDIO
 *
 * returns:	0 - request transfer succesfully started
 *		!0 - start of request transfer failed
 */
static int zfcp_fsf_req_send(zfcp_fsf_req_t *fsf_req, struct timer_list *timer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
        zfcp_adapter_t *adapter = fsf_req->adapter;
        zfcp_qdio_queue_t *req_queue = &adapter->request_queue;
        volatile qdio_buffer_element_t* sbale;
	int inc_seq_no = 1;
        int new_distance_from_int;
	unsigned long flags;
	int test_count;

	u8 sbal_index = fsf_req->sbal_first;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx timer=0x%lx)\n",
		(unsigned long)fsf_req,
		(unsigned long)timer);
        
	/* FIXME(debug): remove it later */
	sbale = zfcp_qdio_sbale_req(fsf_req, sbal_index, 0);
	ZFCP_LOG_DEBUG(
		"SBALE0 flags=0x%x\n",
		sbale[0].flags);
	ZFCP_LOG_TRACE("HEX DUMP OF SBALE1 PAYLOAD:\n");
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_TRACE,
		(char*)sbale[1].addr,
		sbale[1].length);

	test_count = (fsf_req->sbal_curr - fsf_req->sbal_first) + 1;
	test_count += QDIO_MAX_BUFFERS_PER_Q; /* no module of <0 */
	test_count %= QDIO_MAX_BUFFERS_PER_Q;
	if (fsf_req->sbal_number != test_count)
		ZFCP_LOG_NORMAL(
			"error: inconsistent SBAL count in request "
			"(%d, %d, %d, %d, %d)\n",
			fsf_req->sbal_first,
			fsf_req->sbal_curr,
			fsf_req->sbal_last,
			fsf_req->sbal_number,
			test_count);
        
	/* set sequence counter in QTCB */
	if (fsf_req->qtcb) {
		fsf_req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
		fsf_req->seq_no = adapter->fsf_req_seq_no;
		ZFCP_LOG_TRACE(
			"FSF request 0x%lx of adapter 0x%lx gets "
			"FSF sequence counter value of %i\n",
			(unsigned long)fsf_req,
			(unsigned long)adapter,
			fsf_req->qtcb->prefix.req_seq_no);
	} else
                inc_seq_no = 0;

        /* put allocated FSF request at list tail */
	write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
        list_add_tail(&fsf_req->list,
                      &adapter->fsf_req_list_head);
	write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);

	/* figure out expiration time of timeout and start timeout */
	if (timer) {
		timer->expires += jiffies;
		add_timer(timer);
	}

	ZFCP_LOG_TRACE(
		"request queue of adapter with devno=0x%04x: "
		"next free SBAL is %i, %i free SBALs\n",
		adapter->devno,
		req_queue->free_index,
		atomic_read(&req_queue->free_count));

        ZFCP_LOG_DEBUG(
		"Calling do QDIO irq=0x%x, flags=0x%x, queue_no=%i, "
		"index_in_queue=%i, count=%i, buffers=0x%lx\n",
		adapter->irq,
		QDIO_FLAG_SYNC_OUTPUT,
		0,
		fsf_req->sbal_first,
		fsf_req->sbal_number,
		(unsigned long)&req_queue->buffer[sbal_index]);	

        /*
         * adjust the number of free SBALs in request queue as well as
         * position of first one
         */
        atomic_sub(fsf_req->sbal_number, &req_queue->free_count);
        ZFCP_LOG_TRACE("free_count=%d\n",
                       atomic_read(&req_queue->free_count));
        req_queue->free_index += fsf_req->sbal_number;	/* increase */
        req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;    /* wrap if needed */
        new_distance_from_int = zfcp_qdio_determine_pci(req_queue, fsf_req);
	retval = do_QDIO(
			adapter->irq,
			QDIO_FLAG_SYNC_OUTPUT,
                        0,
			fsf_req->sbal_first,
                        fsf_req->sbal_number,
                        NULL);

	if (retval) {
                /* Queues are down..... */
                retval=-EIO;
		/* FIXME(potential race): timer might be expired (absolutely unlikely) */
		if (timer)
			del_timer_sync(timer);
		write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
        	list_del(&fsf_req->list);
		write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
                /*
                 * adjust the number of free SBALs in request queue as well as
                 * position of first one
                 */
                zfcp_zero_sbals(
			req_queue->buffer,
			fsf_req->sbal_first,
			fsf_req->sbal_number);
                atomic_add(fsf_req->sbal_number, &req_queue->free_count);
                req_queue->free_index += QDIO_MAX_BUFFERS_PER_Q;
                req_queue->free_index -= fsf_req->sbal_number;
                req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;

		ZFCP_LOG_DEBUG(
			"error: do_QDIO failed. Buffers could not be enqueued "
                        "to request queue.\n");
        } else {
                req_queue->distance_from_int = new_distance_from_int;
#ifdef ZFCP_DEBUG_REQUESTS
                debug_text_event(adapter->req_dbf, 1, "o:a/seq");
                debug_event(adapter->req_dbf, 1, &fsf_req,
                            sizeof(unsigned long));
                if (inc_seq_no)
                        debug_event(adapter->req_dbf, 1,
                                    &adapter->fsf_req_seq_no, sizeof(u32));
                else
                        debug_text_event(adapter->req_dbf, 1, "nocb");
                debug_event(adapter->req_dbf, 4, &fsf_req->fsf_command,
                            sizeof(fsf_req->fsf_command));
                if (fsf_req->qtcb)
                        debug_event(adapter->req_dbf, 5, &fsf_req->qtcb,
                                    sizeof(unsigned long));
                if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL))
                        debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif /* ZFCP_DEBUG_REQUESTS */
		/*
		 * increase FSF sequence counter -
		 * this must only be done for request successfully enqueued to QDIO
		 * this rejected requests may be cleaned up by calling routines
		 * resulting in missing sequence counter values otherwise,
		 */
                /* Don't increase for unsolicited status */ 
                if (inc_seq_no) {
                        adapter->fsf_req_seq_no++;
			ZFCP_LOG_TRACE(
				"FSF sequence counter value of adapter 0x%lx "
				"increased to %i\n",
				(unsigned long)adapter,
				adapter->fsf_req_seq_no);
		}
		/* count FSF requests pending */
		atomic_inc(&adapter->fsf_reqs_active);
#ifdef ZFCP_STAT_QUEUES
		atomic_inc(&adapter->outbound_total);
#endif
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_req_cleanup
 *
 * purpose:	cleans up an FSF request and removes it from the specified list
 *
 * returns:
 *
 * assumption:	no pending SB in SBALEs other than QTCB
 */
static int zfcp_fsf_req_cleanup(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval;
        zfcp_adapter_t *adapter = fsf_req->adapter;
	unsigned long flags;

	ZFCP_LOG_TRACE("enter (fsf_req=0x%lx)\n", (unsigned long)fsf_req);

	write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
        list_del(&fsf_req->list);
	write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
        retval = zfcp_fsf_req_free(fsf_req);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:	zfcp_zero_sbals
 *
 * purpose:	zeros specified range of SBALs
 *
 * returns:
 */
static inline void zfcp_zero_sbals(qdio_buffer_t *buf[], int first, int clean_count)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_QDIO

        int cur_pos;
        int index;
        
        
	ZFCP_LOG_TRACE(
		"enter (buf=0x%lx, first=%i, clean_count=%i\n",
		(unsigned long)buf, first, clean_count);
        
        for (cur_pos = first; cur_pos < (first + clean_count); cur_pos++){
                index = cur_pos % QDIO_MAX_BUFFERS_PER_Q;
                memset(buf[index], 0, sizeof(qdio_buffer_t));
		ZFCP_LOG_TRACE(
			"zeroing BUFFER %d at address 0x%lx\n",
                        index,
			(unsigned long) buf[index]);
        }

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static void zfcp_config_parse_error(
	unsigned char	*s,		/* complete mapping string */
	unsigned char	*err_pos,	/* position of error in mapping string */
	const char	*err_msg,	/* error message */
	...)				/* additional arguments to be integrated into error message */
{
#define ZFCP_LOG_AREA		ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX	ZFCP_LOG_AREA_PREFIX_CONFIG

	int buf_l;
	va_list args;
	unsigned char *pos;
	unsigned char c;

	ZFCP_LOG_TRACE(
		"enter (s=0x%lx, err_pos=0x%lx, err_msg=0x%lx\n",
		(unsigned long)s, 
                (unsigned long)err_pos, 
                (unsigned long)err_msg);

	/* integrate additional arguments into error message */
	va_start(args, err_msg);
	buf_l = vsprintf(zfcp_data.perrbuf, err_msg, args);
	va_end(args);
	if (buf_l > ZFCP_PARSE_ERR_BUF_SIZE) {
		ZFCP_LOG_NORMAL("Buffer overflow while parsing error message\n");
		/* truncate error message */
		zfcp_data.perrbuf[ZFCP_PARSE_ERR_BUF_SIZE - 1] = '\0';
		buf_l = ZFCP_PARSE_ERR_BUF_SIZE;
	}

	/* calculate and print substring of mapping followed by error info */
	pos = min((s + strlen(s) - 1), (err_pos + 1));
	c = *pos;
	*pos = '\0';
	ZFCP_LOG_NORMAL("\"%s\" <- %s\n", s, zfcp_data.perrbuf);
	*pos = c;

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* these macros implement the logic of the following 3 functions */
#define ZFCP_PARSE_CHECK(condition, err_msg...) \
		if (condition) { \
			zfcp_config_parse_error(s, s + s_l - ts_l, err_msg); \
			retval = -EINVAL; \
			goto out; \
		}

#define ZFCP_PARSE_CHECKEND \
		ZFCP_PARSE_CHECK(!ts_l, "syntax error: unexpected end of record")

#define ZFCP_PARSE_TRUNCATE \
		ts += count; ts_l -= count;

#define ZFCP_PARSE_SKIP_CHARS(characters, min, max) \
		count = strnspn(ts, characters, ts_l); \
		ZFCP_PARSE_CHECK((size_t)count < (size_t)min, "syntax error: missing \"%c\" or equivalent character", *characters) \
		ZFCP_PARSE_CHECK((size_t)count > (size_t)max, "syntax error: extranous \"%c\" or equivalent character", *characters) \
		ZFCP_PARSE_TRUNCATE

#define ZFCP_PARSE_SKIP_COMMENT \
		count = strnspn(ts, ZFCP_PARSE_COMMENT_CHARS, ts_l); \
		if (count) { \
			char *tmp; \
			ZFCP_PARSE_TRUNCATE \
			tmp = strnpbrk(ts, ZFCP_PARSE_RECORD_DELIM_CHARS, ts_l); \
			if (tmp) \
				count = (unsigned long)tmp - (unsigned long)ts; \
			else	count = ts_l; \
			ZFCP_PARSE_TRUNCATE \
		}

#define ZFCP_PARSE_NUMBER(func, value, add_cond, msg...) \
		value = func(ts, &endp, 0); \
		count = (unsigned long)endp - (unsigned long)ts; \
		ZFCP_PARSE_CHECK(!count || (add_cond), msg) \
		ZFCP_PARSE_TRUNCATE
		
#define ZFCP_PARSE_UL(value, cond, msg...) \
		ZFCP_PARSE_NUMBER(simple_strtoul, value, cond, msg)

#define ZFCP_PARSE_ULL(value, cond, msg...) \
		ZFCP_PARSE_NUMBER(simple_strtoull, value, cond, msg)


static int zfcp_config_parse_record_list(unsigned char *s, int s_l, int flags)
{
#define ZFCP_LOG_AREA           ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX    ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval;
	int count;
	zfcp_config_record_t rec;
	int ts_l = s_l;
	unsigned char *ts = s;

	ZFCP_LOG_TRACE(
		"enter (s=0x%lx, s_l=%i, flags=%i)\n",
		(unsigned long)s, s_l, flags);

	while (ts_l) {
		/* parse single line */
		count = zfcp_config_parse_record(ts, ts_l, &rec);
		if (count < 0) {
			retval = count;
			goto out;
		}
		ZFCP_PARSE_TRUNCATE;

		/* create configuration according to parsed line */
		if (rec.valid) {
			if (flags & ZFCP_PARSE_ADD) {
				retval = zfcp_config_parse_record_add(&rec);
                        } else {	
                                /* FIXME (implement) switch in when record_del works again */
#if 0
                                retval = zfcp_config_parse_record_del(&rec);
#endif
                                ZFCP_LOG_TRACE("DEL\n");
                                retval = -1;
                        }
			if (retval < 0)
				goto out;
		} /* else we parsed an empty line or a comment */
                if (ts_l > 0) {
                        /* skip expected 'new line' */
			ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_RECORD_DELIM_CHARS, 1, ts_l);
                }
	}
	retval = s_l;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_config_parse_record(
	unsigned char	*s,
	int		s_l,
	zfcp_config_record_t *rec)
{
#define ZFCP_LOG_AREA           ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX    ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval;
	int count = 0;
	char *endp;
	unsigned char *ts = s;
	int ts_l = s_l;

	ZFCP_LOG_TRACE(
		"enter (s=0x%lx, s_l=%i, rec=0x%lx)\n",
		(unsigned long)s, 
                s_l,
                (unsigned long)rec);

	rec->valid = 0;

	/* skip any leading spaces + tabs */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_SPACE_CHARS, 0, -1UL);

	/* allow for comments */
	ZFCP_PARSE_SKIP_COMMENT;

	/* allow 'empty' line */
	if (strnspn(ts, ZFCP_PARSE_RECORD_DELIM_CHARS, 1))
		goto calculate;

	/* parse device number of host */
	ZFCP_PARSE_UL(rec->devno, rec->devno > 0xFFFF, "no valid device number");
	ZFCP_LOG_TRACE("devno \"0x%lx\"\n", rec->devno);
	ZFCP_PARSE_CHECKEND;

	/* skip delimiting spaces + tabs (at least 1 character is mandatory */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_SPACE_CHARS, 1, -1UL);
	ZFCP_PARSE_CHECKEND;

	/* parse scsi id of remote port */
	ZFCP_PARSE_UL(rec->scsi_id, 0, "no valid SCSI ID");
	ZFCP_LOG_TRACE("SCSI ID \"0x%lx\"\n", rec->scsi_id);
	ZFCP_PARSE_CHECKEND;

	/* skip delimiting character */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_DELIM_CHARS, 1, 1);
	ZFCP_PARSE_CHECKEND;

	/* parse wwpn of remote port */
	ZFCP_PARSE_ULL(rec->wwpn, 0, "no valid WWPN");
	ZFCP_LOG_TRACE("WWPN \"0x%016Lx\"\n", rec->wwpn);
	ZFCP_PARSE_CHECKEND;

	/* skip delimiting spaces + tabs (at least 1 character is mandatory */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_SPACE_CHARS, 1, -1UL);
	ZFCP_PARSE_CHECKEND;

	/* parse scsi lun of logical unit */
	ZFCP_PARSE_UL(rec->scsi_lun, 0, "no valid SCSI LUN");
	ZFCP_LOG_TRACE("SCSI LUN \"0x%lx\"\n", rec->scsi_lun);
	ZFCP_PARSE_CHECKEND;

	/* skip delimiting character */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_DELIM_CHARS, 1, 1);
	ZFCP_PARSE_CHECKEND;

	/* parse fcp_lun of logical unit */
	ZFCP_PARSE_ULL(rec->fcp_lun, 0, "no valid FCP_LUN");
	ZFCP_LOG_TRACE("FCP_LUN \"0x%016Lx\"\n", rec->fcp_lun);

        /* skip any ending spaces + tabs */
	ZFCP_PARSE_SKIP_CHARS(ZFCP_PARSE_SPACE_CHARS, 0, -1UL);

	/* allow for comments */
	ZFCP_PARSE_SKIP_COMMENT;

	/* this is something valid */
	rec->valid = 1;

calculate:
	/* length of string which has been parsed */
	retval = s_l - ts_l;

out:
        ZFCP_LOG_TRACE("exit %d\n",
                       retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


#define ZFCP_PRINT_FAILED_RECORD(rec, log_func) \
	log_func( \
		"warning: unable to add record: " \
		"0x%04lx %li:0x%016Lx %li:0x%016Lx\n", \
		rec->devno, \
		rec->scsi_id, rec->wwpn, \
		rec->scsi_lun, rec->fcp_lun);


/*
 * function:	zfcp_config_parse_record_add
 *
 * purpose:	Alloctes the required adapter, port and unit structs
 *              and puts them into their respective lists
 *
 * returns:	0 on success
 *              -E* on failure (depends on called routines)
 */
int zfcp_config_parse_record_add(zfcp_config_record_t *rec)
{
#define ZFCP_LOG_AREA		ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX	ZFCP_LOG_AREA_PREFIX_SCSI

	int retval;
	zfcp_adapter_t *adapter;
	zfcp_port_t *port;
	zfcp_unit_t *unit;

	ZFCP_LOG_TRACE("enter (rec=0x%lx)\n", (unsigned long)rec);

        /* don't allow SCSI ID 0 for any port since it is reserved for adapters */
        if (rec->scsi_id == 0) {
                ZFCP_LOG_NORMAL(
                        "warning: SCSI ID 0 is not allowed for ports as it is "
                        "reserved for adapters\n");
		retval = -EINVAL;
                goto failed_record;
        }
	/* check for adapter and configure it if needed */
        retval = zfcp_adapter_enqueue(rec->devno, &adapter);
        if (retval < 0)
                goto failed_record;

	/*
	 * no explicit adapter reopen necessary,
	 * will be escalated by unit reopen if required
	 */

        retval = zfcp_port_enqueue(
			adapter,
			rec->scsi_id,
			rec->wwpn,
			0,
			&port);
        if (retval < 0)
                goto failed_record;

	/*
	 * no explicit port reopen necessary,
	 * will be escalated by unit reopen if required
	 */

        retval = zfcp_unit_enqueue(
			port,
			rec->scsi_lun,
			rec->fcp_lun,
			&unit);
        if (retval < 0)
                goto failed_record;

	zfcp_erp_unit_reopen(unit, 0);

	/* processed record successfully */
	goto out;

failed_record:
	ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_NORMAL);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


#if 0
/* FIXME(design): rewrite necessary */
static int zfcp_config_parse_record_del(zfcp_config_record_t *rec)
{
#define ZFCP_LOG_AREA		ZFCP_LOG_AREA_CONFIG
#define ZFCP_LOG_AREA_PREFIX	ZFCP_LOG_AREA_PREFIX_CONFIG

	int retval = 0;
	unsigned long flags;
	zfcp_adapter_t *adapter;
	zfcp_port_t *port;
	zfcp_unit_t *unit;

	ZFCP_LOG_TRACE(
		"enter (rec=0x%lx)\n",
		(unsigned long)rec);

	/* check for adapter */
	write_lock_irqsave(&zfcp_data.adapter_list_lock, flags);
	ZFCP_FOR_EACH_ADAPTER(adapter) {
                if (adapter->devno == rec->devno)
			break;
	}
	if (!adapter) {
		ZFCP_LOG_NORMAL(
			"warning: Could not delete a record. "
                        "The adapter with devno 0x%04x does not exist.\n",
			adapter->devno);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_DEBUG);
		goto unlock_adapter;
	}

	/* check for remote port */
	write_lock(&adapter->port_list_lock);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (port->scsi_id == rec->scsi_id)
			break;
	}
	if (!port) {
		ZFCP_LOG_NORMAL(
                               "warning: Could not delete a record. "
                               "The port with SCSI ID %i does not exist.\n",
                               port->scsi_id);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_DEBUG);
		goto unlock_port;
	}
	if (port->wwpn != rec->wwpn) {
		ZFCP_LOG_NORMAL(
			"error: The port WWPN 0x%016Lx "
			"does not match the already configured WWPN 0x%016Lx\n",
			rec->wwpn,
			(llui_t)port->wwpn);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_INFO);
		goto unlock_port;
	}

	/* check for logical unit */
	write_lock(&port->unit_list_lock);
	ZFCP_FOR_EACH_UNIT(port, unit) {
		if (unit->scsi_lun == rec->scsi_lun)
			break;
	}
	if (!unit) {
		ZFCP_LOG_NORMAL(
			"warning: Could not delete a record. "
                        "The unit with SCSI LUN %i does not exist\n",
			unit->scsi_lun);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_DEBUG);
		goto unlock_unit;
	}
	if (unit->fcp_lun != rec->fcp_lun) {
		ZFCP_LOG_NORMAL(
			"error: The record for the FCP_LUN 0x%016Lx "
			"does not match that of the already "
                        "configured FCP_LUN 0x%016Lx\n",
			rec->fcp_lun,
			(llui_t)unit->fcp_lun);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_INFO);
		goto unlock_unit;
	}

	/* FIXME: do more work here: CLOSE UNIT */
	retval = zfcp_unit_dequeue(unit);
	if (retval == -EBUSY) {
		ZFCP_LOG_NORMAL("warning: Attempt to remove active unit with "
                               "FCP_LUN 0x%016Lx, at the port with WWPN 0x%016Lx of the "
                               "adapter with devno 0x%04x was ignored. Unit "
                               "is still in use.\n",
                               (llui_t)unit->fcp_lun,
                               (llui_t)unit->port->wwpn,
                               unit->port->adapter->devno);
		ZFCP_PRINT_FAILED_RECORD(rec, ZFCP_LOG_INFO);
		goto unlock_unit;
	}

	/* FIXME: do more work here: CLOSE PORT */
	retval = zfcp_port_dequeue(port);
	if (retval == -EBUSY) {
		retval = 0;
		goto unlock_unit;
	}

	/* FIXME: do more work here: shutdown adapter */
	retval = zfcp_adapter_dequeue(adapter);
	if (retval == -EBUSY)
		retval = 0;

unlock_unit:
	write_unlock(&port->unit_list_lock);
unlock_port:
	write_unlock(&adapter->port_list_lock);
unlock_adapter:
	write_unlock_irqrestore(&zfcp_data.adapter_list_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}
#endif



/*
 * function:	
 *
 * purpose:	called if an adapter failed,
 *		initiates adapter recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_adapter_reopen_internal(
		zfcp_adapter_t *adapter,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

        debug_text_event(adapter->erp_dbf,5,"a_ro");
        ZFCP_LOG_DEBUG(
		"Reopen on the adapter with devno 0x%04x\n",
		adapter->devno);

	zfcp_erp_adapter_block(adapter, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status)) {
		ZFCP_LOG_DEBUG(
			"skipped reopen on the failed adapter with devno 0x%04x\n",
			adapter->devno);
                debug_text_event(adapter->erp_dbf,5,"a_ro_f");
		/* ensure propagation of failed status to new devices */
		zfcp_erp_adapter_failed(adapter);
		retval = -EIO;
		goto out;
	}
	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_ADAPTER,
			adapter,
			NULL,
			NULL);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_adapter_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_adapter_reopen(
		zfcp_adapter_t *adapter,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

        write_lock_irqsave(&adapter->erp_lock, flags);
        retval = zfcp_erp_adapter_reopen_internal(adapter, clear_mask);
        write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline int zfcp_erp_adapter_shutdown(zfcp_adapter_t* adapter, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

        retval=zfcp_erp_adapter_reopen(
			adapter,
			ZFCP_STATUS_COMMON_RUNNING |
			ZFCP_STATUS_COMMON_ERP_FAILED |
			clear_mask);	

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline int zfcp_erp_port_shutdown(zfcp_port_t* port, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

	retval = zfcp_erp_port_reopen(
			port,
			ZFCP_STATUS_COMMON_RUNNING |
			ZFCP_STATUS_COMMON_ERP_FAILED |
			clear_mask);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline int zfcp_erp_unit_shutdown(zfcp_unit_t* unit, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx clear_mask=0x%x)\n",
		(unsigned long)unit,
		clear_mask);

	retval = zfcp_erp_unit_reopen(
			unit,
			ZFCP_STATUS_COMMON_RUNNING |
			ZFCP_STATUS_COMMON_ERP_FAILED |
			clear_mask);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_els
 *
 * purpose:     Originator of the ELS commands
 *
 * returns:     0       - Operation completed successfuly
 *              -EINVAL - Unknown IOCTL command or invalid sense data record
 *              -ENOMEM - Insufficient memory
 *              -EPERM  - Cannot create or queue FSF request
 */
static int zfcp_els(zfcp_port_t *port, u8 ls_code)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	struct zfcp_send_els *send_els;
	struct zfcp_ls_rls *rls;
	struct zfcp_ls_pdisc *pdisc;
	struct zfcp_ls_adisc *adisc;
	void *page = NULL;
	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx ls_code=0x%02x)\n",
		(unsigned long)port, ls_code);

	send_els = (struct zfcp_send_els*)ZFCP_KMALLOC(
		sizeof(struct zfcp_send_els), GFP_ATOMIC);
	if (send_els == NULL)
		goto nomem;

	send_els->req = (struct scatterlist*)ZFCP_KMALLOC(
		sizeof(struct scatterlist), GFP_ATOMIC);
	if (send_els->req == NULL)
		goto nomem;
	send_els->req_count = 1;

	send_els->resp = (struct scatterlist*)ZFCP_KMALLOC(
		sizeof(struct scatterlist), GFP_ATOMIC);
	if (send_els->resp == NULL)
		goto nomem;
	send_els->resp_count = 1;

	page = (void*)ZFCP_GET_ZEROED_PAGE(GFP_ATOMIC);
	if (page == NULL)
		goto nomem;
	send_els->req->address = (char*)page;
	send_els->resp->address = (char*)(page + (PAGE_SIZE >> 1));

	send_els->port = port;
	send_els->ls_code = ls_code;
	send_els->handler = zfcp_els_handler;
	send_els->handler_data = (unsigned long)send_els;

	*(u32*)page = 0;
	*(u8*)page = ls_code;

	switch (ls_code) {

	case ZFCP_LS_RTV:
		send_els->req->length = sizeof(struct zfcp_ls_rtv);
		send_els->resp->length = sizeof(struct zfcp_ls_rtv_acc);
		ZFCP_LOG_NORMAL(
			"RTV request from sid 0x%06x to did 0x%06x\n",
			port->adapter->s_id, port->d_id);
		break;

	case ZFCP_LS_RLS:
		send_els->req->length = sizeof(struct zfcp_ls_rls);
		send_els->resp->length = sizeof(struct zfcp_ls_rls_acc);
		rls = (struct zfcp_ls_rls*)send_els->req->address;
		rls->port_id = port->adapter->s_id;
		ZFCP_LOG_NORMAL(
			"RLS request from sid 0x%06x to did 0x%06x "
			"payload(port_id=0x%06x)\n",
			port->adapter->s_id, port->d_id, rls->port_id);
		break;

	case ZFCP_LS_PDISC:
		send_els->req->length = sizeof(struct zfcp_ls_pdisc);
		send_els->resp->length = sizeof(struct zfcp_ls_pdisc_acc);
		pdisc = (struct zfcp_ls_pdisc*)send_els->req->address;
		pdisc->wwpn = port->adapter->wwpn;
		pdisc->wwnn = port->adapter->wwnn;
		ZFCP_LOG_NORMAL(
			"PDISC request from sid 0x%06x to did 0x%06x "
			"payload(wwpn=0x%016Lx wwnn=0x%016Lx)\n",
			port->adapter->s_id, port->d_id,
			(unsigned long long)pdisc->wwpn,
			(unsigned long long)pdisc->wwnn);
		break;

	case ZFCP_LS_ADISC:
		send_els->req->length = sizeof(struct zfcp_ls_adisc);
		send_els->resp->length = sizeof(struct zfcp_ls_adisc_acc);
		adisc = (struct zfcp_ls_adisc*)send_els->req->address;
		adisc->hard_nport_id = port->adapter->s_id;
		adisc->wwpn = port->adapter->wwpn;
		adisc->wwnn = port->adapter->wwnn;
		adisc->nport_id = port->adapter->s_id;
		ZFCP_LOG_NORMAL(
			"ADISC request from sid 0x%06x to did 0x%06x "
			"payload(wwpn=0x%016Lx wwnn=0x%016Lx "
			"hard_nport_id=0x%06x nport_id=0x%06x)\n",
			port->adapter->s_id, port->d_id,
			(unsigned long long)adisc->wwpn,
			(unsigned long long)adisc->wwnn,
			adisc->hard_nport_id, adisc->nport_id);
		break;

	default:
		ZFCP_LOG_NORMAL(
			"ELS command code 0x%02x is not supported\n", ls_code);
		retval = -EINVAL;
		goto invalid_ls_code;
	}

	retval = zfcp_fsf_send_els(send_els);
	if (retval != 0) {
		ZFCP_LOG_NORMAL(
			"ELS request could not be processed "
			"(sid=0x%06x did=0x%06x)\n",
			port->adapter->s_id, port->d_id);
		retval = -EPERM;
	}

	goto out;

nomem:
	ZFCP_LOG_INFO("Out of memory!\n");
	retval = -ENOMEM;

invalid_ls_code:
	if (page != NULL)
		ZFCP_FREE_PAGE((unsigned long)page);
	if (send_els != NULL) {
		if (send_els->req != NULL)
			ZFCP_KFREE(send_els->req, sizeof(struct scatterlist));
		if (send_els->resp != NULL)
			ZFCP_KFREE(send_els->resp, sizeof(struct scatterlist));
		ZFCP_KFREE(send_els, sizeof(struct zfcp_send_els));
	}

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_els_handler
 *
 * purpose:     Handler for all kind of ELSs
 *
 * returns:     0       - Operation completed successfuly
 *              -ENXIO  - ELS has been rejected
 *              -EPERM  - Port forced reopen failed
 */
static int zfcp_els_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	struct zfcp_send_els *send_els = (struct zfcp_send_els*)data;
	zfcp_port_t *port = send_els->port;
	zfcp_adapter_t *adapter = port->adapter;
	u8 req_code = *(u8*)send_els->req->address;
	u8 resp_code = *(u8*)send_els->resp->address;
	struct zfcp_ls_rjt *rjt;
	struct zfcp_ls_rtv_acc *rtv;
	struct zfcp_ls_rls_acc *rls;
	struct zfcp_ls_pdisc_acc *pdisc;
	struct zfcp_ls_adisc_acc *adisc;
	int retval = 0;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", data);

	if (send_els->status != 0) {
		ZFCP_LOG_NORMAL(
			"ELS request timed out, force physical port reopen "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(unsigned long long)port->wwpn,
			adapter->devno);
		debug_text_event(adapter->erp_dbf, 3, "forcreop");
		retval = zfcp_erp_port_forced_reopen(port, 0);
		if (retval != 0) {
			ZFCP_LOG_NORMAL(
				"Cannot reopen a remote port "
				"(wwpn=0x%016Lx devno=0x%04x)\n",
				(unsigned long long)port->wwpn,
				adapter->devno);
			retval = -EPERM;
		}
		goto skip_fsfstatus;
	}

	switch (resp_code) {

	case ZFCP_LS_RJT:
		rjt = (struct zfcp_ls_rjt*)send_els->resp->address;

		switch (rjt->reason_code) {

		case ZFCP_LS_RJT_INVALID_COMMAND_CODE:
			ZFCP_LOG_NORMAL(
				"Invalid command code "
				"(wwpn=0x%016Lx command=0x%02x)\n",
				(unsigned long long)port->wwpn,
				req_code);
			break;

		case ZFCP_LS_RJT_LOGICAL_ERROR:
			ZFCP_LOG_NORMAL(
				"Logical error "
				"(wwpn=0x%016Lx reason_explanation=0x%02x)\n",
				(unsigned long long)port->wwpn,
				rjt->reason_expl);
			break;

		case ZFCP_LS_RJT_LOGICAL_BUSY:
			ZFCP_LOG_NORMAL(
				"Logical busy "
				"(wwpn=0x%016Lx reason_explanation=0x%02x)\n",
				(unsigned long long)port->wwpn,
				rjt->reason_expl);
			break;

		case ZFCP_LS_RJT_PROTOCOL_ERROR:
			ZFCP_LOG_NORMAL(
				"Protocol error "
				"(wwpn=0x%016Lx reason_explanation=0x%02x)\n",
				(unsigned long long)port->wwpn,
				rjt->reason_expl);
			break;

		case ZFCP_LS_RJT_UNABLE_TO_PERFORM:
			ZFCP_LOG_NORMAL(
				"Unable to perform command requested "
				"(wwpn=0x%016Lx reason_explanation=0x%02x)\n",
				(unsigned long long)port->wwpn,
				rjt->reason_expl);
			break;

		case ZFCP_LS_RJT_COMMAND_NOT_SUPPORTED:
			ZFCP_LOG_NORMAL(
				"Command not supported "
				"(wwpn=0x%016Lx command=0x%02x)\n",
				(unsigned long long)port->wwpn,
				req_code);
			break;

		case ZFCP_LS_RJT_VENDOR_UNIQUE_ERROR:
			ZFCP_LOG_NORMAL(
				"Vendor unique error "
				"(wwpn=0x%016Lx vendor_unique=0x%02x)\n",
				(unsigned long long)port->wwpn,
				rjt->vendor_unique);
			break;

		default:
			ZFCP_LOG_NORMAL(
				"ELS has been rejected "
				"(devno=0x%04x wwpn=0x%016Lx reason_code=0x%02x)\n",
				adapter->devno,
				(unsigned long long)port->wwpn,
				rjt->reason_code);
		}
		retval = -ENXIO;
		break;

	case ZFCP_LS_ACC:
		switch (req_code) {

		case ZFCP_LS_RTV:
			rtv = (struct zfcp_ls_rtv_acc*)send_els->resp->address;
			ZFCP_LOG_NORMAL(
				"RTV response from did 0x%06x to sid 0x%06x "
				"with payload(R_A_TOV=%ds E_D_TOV=%d%cs)\n",
				port->d_id, port->adapter->s_id,
				rtv->r_a_tov, rtv->e_d_tov,
				rtv->qualifier & ZFCP_LS_RTV_E_D_TOV_FLAG ?
					'n' : 'm');
			break;

		case ZFCP_LS_RLS:
			rls = (struct zfcp_ls_rls_acc*)send_els->resp->address;
			ZFCP_LOG_NORMAL(
				"RLS response from did 0x%06x to sid 0x%06x "
				"with payload(link_failure_count=%u "
				"loss_of_sync_count=%u "
				"loss_of_signal_count=%u "
				"primitive_sequence_protocol_error=%u "
				"invalid_transmition_word=%u "
				"invalid_crc_count=%u)\n",
				port->d_id, port->adapter->s_id,
				rls->link_failure_count,
				rls->loss_of_sync_count,
				rls->loss_of_signal_count,
				rls->prim_seq_prot_error,
				rls->invalid_transmition_word,
				rls->invalid_crc_count);
			break;

		case ZFCP_LS_PDISC:
			pdisc = (struct zfcp_ls_pdisc_acc*)send_els->resp->address;
			ZFCP_LOG_NORMAL(
				"PDISC response from did 0x%06x to sid 0x%06x "
				"with payload(wwpn=0x%016Lx wwnn=0x%016Lx "
				"vendor='%-16s')\n",
				port->d_id, port->adapter->s_id,
				(unsigned long long)pdisc->wwpn,
				(unsigned long long)pdisc->wwnn,
				pdisc->vendor_version);
			break;

		case ZFCP_LS_ADISC:
			adisc = (struct zfcp_ls_adisc_acc*)send_els->resp->address;
			ZFCP_LOG_NORMAL(
				"ADISC response from did 0x%06x to sid 0x%06x "
				"with payload(wwpn=0x%016Lx wwnn=0x%016Lx "
				"hard_nport_id=0x%06x nport_id=0x%06x)\n",
				port->d_id, port->adapter->s_id,
				(unsigned long long)adisc->wwpn,
				(unsigned long long)adisc->wwnn,
				adisc->hard_nport_id, adisc->nport_id);
			/* FIXME: missing wwnn value in port struct */
			if (port->wwnn == 0)
				port->wwnn = adisc->wwnn;
			break;
		}
		break;

	default:
		ZFCP_LOG_NORMAL(
			"Unknown payload code 0x%02x received on a request "
			"0x%02x from sid 0x%06x to did 0x%06x, "
			"port needs to be reopened\n",
			req_code, resp_code, port->adapter->s_id, port->d_id);
		retval = zfcp_erp_port_forced_reopen(port, 0);
		if (retval != 0) {
			ZFCP_LOG_NORMAL(
				"Cannot reopen a remote port "
				"(wwpn=0x%016Lx devno=0x%04x)\n",
				(unsigned long long)port->wwpn,
				port->adapter->devno);
			retval = -EPERM;
		}
	}

skip_fsfstatus:
	ZFCP_FREE_PAGE((unsigned long)send_els->req->address);
	ZFCP_KFREE(send_els->req, sizeof(struct scatterlist));
	ZFCP_KFREE(send_els->resp, sizeof(struct scatterlist));

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_test_link
 *
 * purpose:     Test a status of a link to a remote port using the ELS command ADISC
 *
 * returns:     0       - Link is OK
 *              -EPERM  - Port forced reopen failed
 */
static int zfcp_test_link(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval;

	ZFCP_LOG_TRACE("enter (port=0x%lx)\n", (unsigned long)port);

	retval = zfcp_els(port, ZFCP_LS_ADISC);
	if (retval != 0) {
		ZFCP_LOG_NORMAL(
			"Port needs to be reopened "
			"(wwpn=0x%016Lx devno=0x%04x)\n",
			(unsigned long long)port->wwpn,
			port->adapter->devno);
		retval = zfcp_erp_port_forced_reopen(port, 0);
		if (retval != 0) {
			ZFCP_LOG_NORMAL(
				"Cannot reopen a remote port "
				"(wwpn=0x%016Lx devno=0x%04x)\n",
				(unsigned long long)port->wwpn,
				port->adapter->devno);
			retval = -EPERM;
		}
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	called if a port failed to be opened normally
 *		initiates Forced Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_port_forced_reopen_internal(
		zfcp_port_t *port,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
	zfcp_adapter_t *adapter = port->adapter;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

        debug_text_event(adapter->erp_dbf,5,"pf_ro");
        debug_event(adapter->erp_dbf,5,&port->wwpn,
                    sizeof(wwn_t));

        ZFCP_LOG_DEBUG(
		"Forced reopen of the port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x\n",
		(llui_t)port->wwpn,
		adapter->devno);

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG(
			"skipped forced reopen on the failed port "
			"with WWPN 0x%016Lx on the adapter with devno 0x%04x\n",
			(llui_t)port->wwpn,
			adapter->devno);
                debug_text_event(adapter->erp_dbf,5,"pf_ro_f");
                debug_event(adapter->erp_dbf,5,&port->wwpn,
                            sizeof(wwn_t));
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
			adapter,
			port,
			NULL);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_port_forced_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_port_forced_reopen(
		zfcp_port_t *port,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long flags;
        zfcp_adapter_t *adapter = port->adapter;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=x0%x)\n",
		(unsigned long)port,
		clear_mask);

        write_lock_irqsave(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_forced_reopen_internal(port, clear_mask);
        write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	called if a port is to be opened
 *		initiates Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_port_reopen_internal(
		zfcp_port_t *port,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
	zfcp_adapter_t *adapter = port->adapter;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

        debug_text_event(adapter->erp_dbf, 5, "p_ro");
        debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));

        ZFCP_LOG_DEBUG(
		"Reopen of the port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x\n",
		(llui_t)port->wwpn,
		adapter->devno);

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG(
			"skipped reopen on the failed port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)port->wwpn,
			adapter->devno);
                debug_text_event(adapter->erp_dbf, 5, "p_ro_f");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* ensure propagation of failed status to new devices */
		zfcp_erp_port_failed(port);
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_PORT,
			adapter,
			port,
			NULL);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_port_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_port_reopen(
		zfcp_port_t *port,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long flags;
        zfcp_adapter_t *adapter = port->adapter;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

        write_lock_irqsave(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_reopen_internal(port, clear_mask);
        write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	called if a unit is to be opened
 *		initiates Reopen recovery which is done
 *		asynchronously
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_unit_reopen_internal(
		zfcp_unit_t *unit,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
	zfcp_adapter_t *adapter = unit->port->adapter;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx clear_mask=0x%x)\n",
		(unsigned long)unit,
		clear_mask);

        debug_text_event(adapter->erp_dbf,5,"u_ro");
        debug_event(adapter->erp_dbf,5,&unit->fcp_lun,
                    sizeof(fcp_lun_t));
        ZFCP_LOG_DEBUG(
		"Reopen of the unit with FCP_LUN 0x%016Lx on the "
		"port with WWPN 0x%016Lx "
		"on the adapter with devno 0x%04x\n",
		(llui_t)unit->fcp_lun,
		(llui_t)unit->port->wwpn,
		adapter->devno);

	zfcp_erp_unit_block(unit, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)) {
		ZFCP_LOG_DEBUG(
			"skipped reopen on the failed unit with FCP_LUN 0x%016Lx on the "
			"port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			adapter->devno);
                debug_text_event(adapter->erp_dbf,5,"u_ro_f");
                debug_event(adapter->erp_dbf,5,&unit->fcp_lun,
                            sizeof(fcp_lun_t));
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_UNIT,
			unit->port->adapter,
			unit->port,
			unit);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_unit_reopen_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_unit_reopen(
		zfcp_unit_t *unit,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long flags;
        zfcp_adapter_t *adapter = unit->port->adapter;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx clear_mask=0x%x)\n",
		(unsigned long)unit,
		clear_mask);

        write_lock_irqsave(&adapter->erp_lock, flags);
        retval = zfcp_erp_unit_reopen_internal(unit, clear_mask);
        write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static int zfcp_erp_adapter_block(zfcp_adapter_t *adapter, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

        debug_text_event(adapter->erp_dbf,6,"a_bl");

	zfcp_erp_modify_adapter_status(
                   adapter,
                   ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
                   ZFCP_CLEAR);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static int zfcp_erp_adapter_unblock(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf,6,"a_ubl");
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static int zfcp_erp_port_block(zfcp_port_t *port, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=port->adapter;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

        debug_text_event(adapter->erp_dbf,6,"p_bl");
        debug_event(adapter->erp_dbf,6,&port->wwpn,
                    sizeof(wwn_t));

	zfcp_erp_modify_port_status(
                   port,
                   ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
                   ZFCP_CLEAR);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static int zfcp_erp_port_unblock(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=port->adapter;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf,6,"p_ubl");
        debug_event(adapter->erp_dbf,6,&port->wwpn,
                    sizeof(wwn_t));
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	disable I/O,
 *		return any open requests and clean them up,
 *		aim: no pending and incoming I/O
 *
 * returns:
 */
static int zfcp_erp_unit_block(zfcp_unit_t *unit, int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=unit->port->adapter;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx clear_mask=0x%x\n",
		(unsigned long)unit,
		clear_mask);

        debug_text_event(adapter->erp_dbf,6,"u_bl");
        debug_event(adapter->erp_dbf,6,&unit->fcp_lun,
                    sizeof(fcp_lun_t));

	zfcp_erp_modify_unit_status(
                   unit,
                   ZFCP_STATUS_COMMON_UNBLOCKED | clear_mask,
                   ZFCP_CLEAR);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	enable I/O
 *
 * returns:
 */
static int zfcp_erp_unit_unblock(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=unit->port->adapter;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf,6,"u_ubl");
        debug_event(adapter->erp_dbf,6,&unit->fcp_lun,
                    sizeof(fcp_lun_t));
	atomic_set_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_action_ready(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

        debug_text_event(adapter->erp_dbf, 4, "a_ar");
        debug_event(adapter->erp_dbf, 4, &erp_action->action, sizeof(int));

	zfcp_erp_action_to_ready(erp_action);
	ZFCP_LOG_DEBUG(
		"Waking erp_thread of adapter with devno 0x%04x\n",
		adapter->devno);
	up(&adapter->erp_ready_sem);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:
 *
 * returns:	<0			erp_action not found in any list
 *		ZFCP_ERP_ACTION_READY	erp_action is in ready list
 *		ZFCP_ERP_ACTION_RUNNING	erp_action is in running list
 *
 * locks:	erp_lock must be held
 */
static int zfcp_erp_action_exists(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = -EINVAL;
	struct list_head *entry;
	zfcp_erp_action_t *entry_erp_action;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

	/* search in running list */
	list_for_each(entry, &adapter->erp_running_head) {
		entry_erp_action = list_entry(entry, zfcp_erp_action_t, list);
		if (entry_erp_action == erp_action) {
			retval = ZFCP_ERP_ACTION_RUNNING;
			goto out;
		}
	}
	/* search in ready list */
	list_for_each(entry, &adapter->erp_ready_head) {
		entry_erp_action = list_entry(entry, zfcp_erp_action_t, list);
		if (entry_erp_action == erp_action) {
			retval = ZFCP_ERP_ACTION_READY;
			goto out;
		}
	}

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_fsf_req_decouple(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

	/*
	 * don't need fsf_req_list lock since fsf_req
	 * can't be released concurrently because we get an
	 * fsf_req-completion-callback first which is
	 * serialized to this code by means of the erp_lock
	 * (simple, isn't it?)
	 */
        ZFCP_LOG_TRACE("erp_action 0x%lx, fsf-req 0x%lx, erp_action 0x%lx\n",
                       (unsigned long) erp_action,
                       (unsigned long) erp_action->fsf_req,
                       (unsigned long) erp_action->fsf_req->erp_action);
               
	erp_action->fsf_req->erp_action = NULL;
	erp_action->fsf_req = NULL;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	is called for finished FSF requests related to erp,
 *		moves concerned erp action to 'ready' queue and
 *		signals erp thread to process it,
 *		besides it cancels a timeout
 *		(don't need to cleanup handler pointer and associated
 *		data in fsf_req since fsf_req will be cleaned
 *		up as a whole shortly after leaving this routine)
 *
 * note:	if the timeout could not be canceled then we let it run
 *		to ensure that the timeout routine accesses a valid
 *		erp_action
 *		(then we do not move erp_action into the 'ready queue'
 *		to avoid that it may be removed by the thread before
 *		the timeout routine accesses it;
 *		even if this case is very, very unlikely - it would be
 *		at least a theoretical race) 
 *
 * returns:
 */
static void zfcp_erp_fsf_req_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	zfcp_adapter_t *adapter = fsf_req->adapter;
	zfcp_erp_action_t *erp_action;
	unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	write_lock_irqsave(&adapter->erp_lock, flags);
	erp_action = fsf_req->erp_action;
	if (erp_action != 0) {
		debug_text_event(adapter->erp_dbf, 5, "a_frh_norm");
		debug_event(adapter->erp_dbf, 2, &erp_action->action,
                            sizeof(int));
		/* don't care for timer status - timer routine is prepared */
		del_timer(&erp_action->timer);
		zfcp_erp_fsf_req_decouple(erp_action);
		zfcp_erp_action_ready(erp_action);
	} else if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
                /* timeout (?) or dismiss ran - nothing to do */
                debug_text_event(adapter->erp_dbf, 3, "a_frh_tfin");
        }
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:
 *
 * returns:
 */
static void zfcp_erp_memwait_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	unsigned long flags;
	zfcp_erp_action_t *erp_action = (zfcp_erp_action_t*) data;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", data);

	write_lock_irqsave(&adapter->erp_lock, flags);
	if (zfcp_erp_action_exists(erp_action) != ZFCP_ERP_ACTION_RUNNING) {
		/* action is ready or gone - nothing to do */
		debug_text_event(adapter->erp_dbf, 3, "a_mwh_nrun");
		debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
		goto unlock;
	}
	debug_text_event(adapter->erp_dbf, 2, "a_mwh_run");
	debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof(int));
	zfcp_erp_action_ready(erp_action);
unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	is called if an asynchronous erp step timed out,
 *		cleanup handler pointer and associated data in fsf_req
 *		to indicate that we don't want to be called back anymore
 *		for this fsf_req,
 *
 * returns:
 */
static void zfcp_erp_timeout_handler(unsigned long data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	unsigned long flags;
	zfcp_erp_action_t *erp_action = (zfcp_erp_action_t*) data;
	zfcp_adapter_t *adapter = erp_action->adapter;
	int need_reopen = 0;

	ZFCP_LOG_TRACE("enter (data=0x%lx)\n", data);

	write_lock_irqsave(&adapter->erp_lock, flags);
	if (zfcp_erp_action_exists(erp_action) != ZFCP_ERP_ACTION_RUNNING) {
		/* action is ready or gone - nothing to do */
		debug_text_event(adapter->erp_dbf, 3, "a_th_nrun");
		debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
		goto unlock;
	}
	debug_text_event(adapter->erp_dbf, 2, "a_th_tout");
	debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof(int));
	ZFCP_LOG_NORMAL(
		"error: Error Recovery Procedure step timed out. "
		"The action flag is 0x%x. The FSF request "
		"is at 0x%lx\n",
		erp_action->action,
		(unsigned long)erp_action->fsf_req);
	erp_action->status |= ZFCP_STATUS_ERP_TIMEDOUT;
	/*
	 * don't need fsf_req_list lock since fsf_req
	 * can't be released concurrently because we get an
	 * fsf_req-completion-callback first which is
	 * serialized to this code by means of the erp_lock
	 * (simple, isn't it?)
	 */
        if (erp_action->fsf_req->status & ZFCP_STATUS_FSFREQ_POOL)
		need_reopen = 1;
	/*
	 * FIXME(potential race):
	 * completed fsf_req might be evaluated concurrently,
	 * this might change some status bits which erp
	 * relies on to decide about necessary steps
	 */
	erp_action->fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
	zfcp_erp_fsf_req_decouple(erp_action);
	zfcp_erp_action_ready(erp_action);
	if (need_reopen) {
		ZFCP_LOG_NORMAL(
			"error: The error recovery action using the "
			"low memory pool timed out. Restarting IO on "
			"the adapter with devno 0x%04x to free it.\n",
			adapter->devno);
		zfcp_erp_adapter_reopen_internal(adapter, 0);
	}
		
unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);
	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * locks:	erp_lock (port_list_lock, unit_list_lock sometimes) held
 *
 * context:	any (thus no call to schedule)
 */
static int zfcp_erp_action_dismiss(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action 0x%lx)\n",
		(unsigned long) erp_action);

	if (zfcp_erp_action_exists(erp_action) != ZFCP_ERP_ACTION_RUNNING) {
		/* action is ready or gone - nothing to do */
		debug_text_event(adapter->erp_dbf, 3, "a_adis_nrun");
		debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 5, "a_adis_norm");
	debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof(int));
	/* don't care for timer status - timer routine is prepared */
	del_timer(&erp_action->timer);
	erp_action->status |= ZFCP_STATUS_ERP_DISMISSED;
	/*
	 * don't need fsf_req_list lock since fsf_req
	 * can't be released concurrently because we get an
	 * fsf_req-completion-callback first which is
	 * serialized to this code by means of the erp_lock
	 * (simple, isn't it?)
	 */
	if (erp_action->fsf_req) {
		debug_text_event(adapter->erp_dbf, 5, "a_adis_fexist");
		debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof(int));
		/*
		 * FIXME(potential race):
		 * completed fsf_req might be evaluated concurrently,
		 * this might change some status bits which erp
		 * relies on to decide about necessary steps
		 */
		erp_action->fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
		zfcp_erp_fsf_req_decouple(erp_action);
	}
	zfcp_erp_action_ready(erp_action);
out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_erp_thread_setup(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

        int retval = 0;
        struct tq_struct *erp_task = NULL;

	ZFCP_LOG_TRACE(
               "enter (adapter=0x%lx)\n",
               (unsigned long)adapter);

        erp_task = ZFCP_KMALLOC(sizeof(struct tq_struct), GFP_KERNEL); 
        if (!erp_task) {
                ZFCP_LOG_INFO(
			"error: Not enough memory for the error handler "
			"of the adapter with devno 0x%04x, leaving.\n",
			adapter->devno);
                retval = -ENOMEM;
                goto out;
        }

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_DONE, &adapter->status);

	rwlock_init(&adapter->erp_lock);
	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);
	sema_init(&adapter->erp_ready_sem, 0);
#ifdef ZFCP_ERP_DEBUG_SINGLE_STEP
	sema_init(&adapter->erp_continue_sem, 0);
#endif // ZFCP_ERP_DEBUG_SINGLE_STEP

        INIT_TQUEUE(erp_task, 
                    zfcp_erp_thread_setup_task, 
                    adapter); 
        schedule_task(erp_task);

	__wait_event_interruptible(
		adapter->erp_thread_wqh,
		atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_DONE, &adapter->status),
		retval);

	if (retval) {
		ZFCP_LOG_INFO(
			"error: The error recovery procedure thread creation "
			"for the adapter with devno 0x%04x was aborted. An "
                        "OS signal was receiveved.\n",
			adapter->devno);
		if (atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, 
                                     &adapter->status)) {
			zfcp_erp_thread_kill(adapter);
                }
	}

	ZFCP_KFREE(erp_task, sizeof(*erp_task));
 
        debug_text_event(adapter->erp_dbf, 5, "a_thset_ok");
out:
	ZFCP_LOG_TRACE("exit %d\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static void zfcp_erp_thread_setup_task(void *data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	zfcp_adapter_t *adapter = (zfcp_adapter_t *)data;
	int retval;

	ZFCP_LOG_TRACE(
               "enter (data=0x%lx)\n",
               (unsigned long)data);

	retval = kernel_thread(zfcp_erp_thread, adapter, SIGCHLD);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
                        "error recovery procedure  thread "
			"for the adapter with devno 0x%04x\n",
			adapter->devno);
		atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_DONE, &adapter->status);
		wake_up_interruptible(&adapter->erp_thread_wqh);
	} else	{
		ZFCP_LOG_DEBUG(
			"created erp thread "
			"for the adapter with devno 0x%04x\n",
			adapter->devno);
                debug_text_event(adapter->erp_dbf,5,"a_thset_t_ok");
	}
		
	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * context:	process (i.e. proc-fs or rmmod/insmod)
 *
 * note:	The caller of this routine ensures that the specified
 *		adapter has been shut down and that this operation
 *		has been completed. Thus, there are no pending erp_actions
 *		which would need to be handled here.
 */
static int zfcp_erp_thread_kill(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;

	ZFCP_LOG_TRACE(
               "enter (adapter=0x%lx)\n",
               (unsigned long)adapter);

	ZFCP_LOG_DEBUG(
		"Killing erp_thread for the adapter with devno 0x%04x\n",
		adapter->devno);
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL, &adapter->status);
	up(&adapter->erp_ready_sem);
	wait_event(
		adapter->erp_thread_wqh,
		!atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status));
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL, &adapter->status);

        debug_text_event(adapter->erp_dbf,5,"a_thki_ok");

	ZFCP_LOG_DEBUG(
		"Killed erp_thread for the adapter with devno 0x%04x\n",
		adapter->devno);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	should be run as a thread,
 *		goes through list of error recovery actions of associated adapter
 *		and delegates single action to execution
 *
 * returns:
 */
/* FIXME(design): static or not? */
static int zfcp_erp_thread(void *data)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	zfcp_adapter_t *adapter = (zfcp_adapter_t*)data;
	struct list_head *next;
	zfcp_erp_action_t *erp_action;
	unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (data=0x%lx)\n",
		(unsigned long)data);

	__sti();
	daemonize();
        /* disable all signals */
	siginitsetinv(&current->blocked, 0);

	sprintf(current->comm, "zfcp_erp_0x%04x", adapter->devno);

	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	ZFCP_LOG_DEBUG(
		"erp thread for adapter with devno 0x%04x is up.\n",
		adapter->devno);
        debug_text_event(adapter->erp_dbf, 5, "a_th_run");
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_DONE, &adapter->status);
	wake_up_interruptible(&adapter->erp_thread_wqh);

	/* (nearly) infinite loop */
	for (;;) {
		/* sleep as long as there is no action in 'ready' queue */
		down_interruptible(&adapter->erp_ready_sem);
#ifdef ZFCP_ERP_DEBUG_SINGLE_STEP
		down(&adapter->erp_continue_sem);
#endif // ZFCP_ERP_DEBUG_SINGLE_STEP
		ZFCP_LOG_TRACE(
			"erp thread woken on adapter with devno 0x%04x\n",
			adapter->devno);

		/* killing this thread */
		if (atomic_test_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_KILL, &adapter->status)) {
                        debug_text_event(adapter->erp_dbf,5,"a_th_kill");
			ZFCP_LOG_DEBUG(
				"Recognized kill flag for the erp_thread of "
				"the adapter with devno 0x%04x\n",
				adapter->devno);
			read_lock_irqsave(&adapter->erp_lock, flags);
			retval = !list_empty(&adapter->erp_ready_head) ||
				 !list_empty(&adapter->erp_running_head);
			read_unlock_irqrestore(&adapter->erp_lock, flags);
			if (retval) {
				debug_text_exception(adapter->erp_dbf, 1, "a_th_bkill");
				ZFCP_LOG_NORMAL(
					"bug: error recovery thread is "
					"shutting down although there are "
					"error recovery actions pending at "
					"adapter with devno 0x%04x\n",
					adapter->devno);
				/* don't exit erp to avoid potential system crash */
			} else	break;
		}

		ZFCP_PARANOIA {
			/* there should be something in 'ready' queue */
			/*
			 * need lock since list_empty checks for entry at
			 * lists head while lists head is subject to
			 * modification when another action is put to this
			 * queue (only list tail won't be modified then)
			 */
			read_lock_irqsave(&adapter->erp_lock, flags);
			retval = list_empty(&adapter->erp_ready_head);
			read_unlock_irqrestore(&adapter->erp_lock, flags);
			if (retval) {
                                debug_text_exception(adapter->erp_dbf, 1, "a_th_empt");
				ZFCP_LOG_NORMAL(
					"bug: Error recovery procedure thread "
                                        "woken for empty action list on the "
					"adapter with devno 0x%04x.\n",
					adapter->devno);
				/* sleep until next try */
				continue;
			}
		}

		/*
		 * get next action to be executed; FIFO -> head.prev
		 * (don't need lock since there is an action at lists tail and
		 * lists tail won't be modified concurrently; only lists head
		 * would be modified if another action is put to this queue)
		 */
		next = adapter->erp_ready_head.prev;
		erp_action = list_entry(next, zfcp_erp_action_t, list);
		/* process action (incl. [re]moving it from 'ready' queue) */
		retval = zfcp_erp_strategy(erp_action);
	}

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_THREAD_UP, &adapter->status);
	ZFCP_LOG_DEBUG(
		"erp thread for adapter with devno 0x%04x is down.\n",
		adapter->devno);

	wake_up(&adapter->erp_thread_wqh);

        debug_text_event(adapter->erp_dbf, 5, "a_th_stop");

	ZFCP_LOG_TRACE("exit %d\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	drives single error recovery action and schedules higher and
 *		subordinate actions, if necessary
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully (action dequeued)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully (action dequeued)
 *		ZFCP_ERP_EXIT		- action finished (action dequeued), target offline
 *		ZFCP_ERP_DISMISSED	- action canceled (action dequeued)
 */
static int zfcp_erp_strategy(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	int temp_retval;
	zfcp_adapter_t *adapter = erp_action->adapter;
	zfcp_port_t *port = erp_action->port;
	zfcp_unit_t *unit = erp_action->unit;
	int action = erp_action->action;
	u32 status = erp_action->status;
	unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

	/* don't process dismissed erp action, just dequeue it */
	write_lock_irqsave(&adapter->erp_lock, flags);
	retval = zfcp_erp_strategy_check_dismissed(erp_action);
	/* leave if this action is gone */
	if (retval == ZFCP_ERP_DISMISSED) {
                debug_text_event(adapter->erp_dbf, 4, "a_st_dis");
		goto unlock;
        }

	/*
	 * move action to 'running' queue before processing it
	 * (to avoid a race condition regarding moving the
	 * action to the 'running' queue and back)
	 */
	zfcp_erp_action_to_running(erp_action);
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	/* no lock to allow for blocking operations (kmalloc, qdio, ...) */

	/* try to process action as far as possible */
	retval = zfcp_erp_strategy_do_action(erp_action);
	switch (retval) {
		case ZFCP_ERP_NOMEM :
			/* no memory to continue immediately, let it sleep */
			debug_text_event(adapter->erp_dbf, 2, "a_st_memw");
			retval = zfcp_erp_strategy_memwait(erp_action);
			/* fall through, waiting for memory means action continues */
		case ZFCP_ERP_CONTINUES :
			/* leave since this action runs asynchronously */
			debug_text_event(adapter->erp_dbf, 6, "a_st_cont");
			goto out;
        }

	/* ok, finished action (whatever its result is) */

	/*
	 * the following happens under lock for several reasons:
	 * - dequeueing of finished action and enqueueing of
	 *   follow-up actions must be atomic so that
	 *   any other reopen-routine does not believe there is
	 *   is nothing to do and that it is safe to enqueue
	 *   something else,
	 * - we want to force any control thread which is dismissing
	 *   actions to finish this before we decide about
	 *   necessary steps to be taken here further
	 */
	write_lock_irqsave(&adapter->erp_lock, flags);

	/* still not dismissed? */
	temp_retval = zfcp_erp_strategy_check_dismissed(erp_action);
	/* leave if this action is gone */
	if (temp_retval == ZFCP_ERP_DISMISSED) {
                debug_text_event(adapter->erp_dbf, 6, "a_st_dis2");
		retval = temp_retval;
		goto unlock;
	}

	/* check for unrecoverable targets */
	retval = zfcp_erp_strategy_check_target(erp_action, retval);

	/* action must be dequeued (here to allow for further ones) */
	zfcp_erp_action_dequeue(erp_action);

	/*
	 * put this target through the erp mill again if someone has
	 * requested to change the status of a target being online 
	 * to offline or the other way around
	 * (old retval is preserved if nothing has to be done here)
	 */
	retval = zfcp_erp_strategy_statechange(
			action, status, adapter, port, unit, retval);

	/*
	 * leave if target is in permanent error state or if
	 * action is repeated in order to process state change
	 */
	if (retval == ZFCP_ERP_EXIT) {
                debug_text_event(adapter->erp_dbf, 2, "a_st_exit");
                goto unlock;
        }

	/* trigger follow up actions */
	zfcp_erp_strategy_followup_actions(
		action, adapter, port, unit, retval);

unlock:
	write_unlock_irqrestore(&adapter->erp_lock, flags);

out:
	/*
	 * 2 things we want to check finally if the erp queues will be empty
	 * (don't do that if the last action evaluated was dismissed
	 * since this clearly indicates that there is more to come) :
	 * - close the name server port if it is open yet (enqueues another final action)
	 * - otherwise, wake up whoever wants to be woken when we are done with erp
	 */
	if (retval != ZFCP_ERP_DISMISSED)
		zfcp_erp_strategy_check_queues(adapter);

        debug_text_event(adapter->erp_dbf, 6, "a_st_done");
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_check_dismissed(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

	if (erp_action->status & ZFCP_STATUS_ERP_DISMISSED) {
		debug_text_event(adapter->erp_dbf, 3, "a_stcd_dis");
		debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
		ZFCP_LOG_DEBUG(
			"releasing aborted erp_action of class %d "
			"at 0x%lx of adapter with devno 0x%04x\n",
			erp_action->action,
			(unsigned long)erp_action,
			erp_action->adapter->devno);
		zfcp_erp_action_dequeue(erp_action);
		retval = ZFCP_ERP_DISMISSED;
	} else {
                debug_text_event(adapter->erp_dbf, 5, "a_stcd_nodis");
                debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof(int));
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_do_action(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_FAILED;
        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);
	/*
	 * no lock in subsequent stratetgy routines
	 * (this allows these routine to call schedule, e.g.
	 * kmalloc with such flags or qdio_initialize & friends)
	 */

	if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
		/* DEBUG */
		//unsigned long timeout = 1000 * HZ;
       
		debug_text_event(adapter->erp_dbf, 3, "a_stda_tim");
		debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
	
		/* DEBUG */
		//__ZFCP_WAIT_EVENT_TIMEOUT(timeout, 0);
        }
        /* Note: in case of timeout, the seperate strategies will fail
           anyhow. No need for a special action. Even worse, a nameserver
           failure would not wake up waiting ports without the call.
        */ 
        /* try to execute/continue action as far as possible */
        switch (erp_action->action) {
        case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
                retval = zfcp_erp_adapter_strategy(erp_action);
                break;
        case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
                retval = zfcp_erp_port_forced_strategy(erp_action);
                break;
        case ZFCP_ERP_ACTION_REOPEN_PORT :
                retval = zfcp_erp_port_strategy(erp_action);
                break;
        case ZFCP_ERP_ACTION_REOPEN_UNIT :
                retval = zfcp_erp_unit_strategy(erp_action);
                break;
        default :
                debug_text_exception(adapter->erp_dbf, 1, "a_stda_bug");
                debug_event(adapter->erp_dbf, 1, &erp_action->action, sizeof(int));
                ZFCP_LOG_NORMAL("bug: Unknown error recovery procedure "
                                "action requested on the adapter with "
                                "devno 0x%04x (debug info %d)\n",
                                erp_action->adapter->devno,
                                erp_action->action);
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	triggers retry of this action after a certain amount of time
 *		by means of timer provided by erp_action
 *
 * returns:	ZFCP_ERP_CONTINUES - erp_action sleeps in erp running queue
 */
static int zfcp_erp_strategy_memwait(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_CONTINUES;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

        debug_text_event(adapter->erp_dbf, 6, "a_mwinit");
        debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof(int));
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_memwait_handler;
	erp_action->timer.data = (unsigned long)erp_action;
	erp_action->timer.expires = jiffies + ZFCP_ERP_MEMWAIT_TIMEOUT;
	add_timer(&erp_action->timer);
	
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:    zfcp_erp_adapter_failed
 *
 * purpose:     sets the adapter and all underlying devices to ERP_FAILED
 *
 */
static void zfcp_erp_adapter_failed(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP


        ZFCP_LOG_TRACE(
                "enter (adapter=0x%lx)\n",
                (unsigned long)adapter);

        zfcp_erp_modify_adapter_status(adapter,
                                       ZFCP_STATUS_COMMON_ERP_FAILED,
                                       ZFCP_SET);
        ZFCP_LOG_NORMAL(
                "Adapter recovery failed on the "
                "adapter with devno 0x%04x.\n",
                adapter->devno);
        debug_text_event(adapter->erp_dbf, 2, "a_afail");


        ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:    zfcp_erp_port_failed
 *
 * purpose:     sets the port and all underlying devices to ERP_FAILED
 *
 */
static void zfcp_erp_port_failed(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP


        ZFCP_LOG_TRACE("enter (port=0x%lx)\n",
                       (unsigned long)port);
        
        zfcp_erp_modify_port_status(port,
                                    ZFCP_STATUS_COMMON_ERP_FAILED,
                                    ZFCP_SET);
        ZFCP_LOG_NORMAL("Port recovery failed on the "
                        "port with WWPN 0x%016Lx at the "
                        "adapter with devno 0x%04x.\n",
                        (llui_t)port->wwpn,
                        port->adapter->devno);
        debug_text_event(port->adapter->erp_dbf, 2, "p_pfail");
        debug_event(port->adapter->erp_dbf, 2, &port->wwpn, sizeof(wwn_t));
        
        ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/* 
 * function:    zfcp_erp_unit_failed
 *
 * purpose:     sets the unit to ERP_FAILED
 *
 */
static void zfcp_erp_unit_failed(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP


        ZFCP_LOG_TRACE(
                "enter (unit=0x%lx)\n",
                (unsigned long)unit);

        zfcp_erp_modify_unit_status(unit,
                                    ZFCP_STATUS_COMMON_ERP_FAILED,
                                    ZFCP_SET);
        ZFCP_LOG_NORMAL(
                "Unit recovery failed on the unit with FCP_LUN 0x%016Lx "
                "connected to the port with WWPN 0x%016Lx at the "
                "adapter with devno 0x%04x.\n",
                (llui_t)unit->fcp_lun,
                (llui_t)unit->port->wwpn,
                unit->port->adapter->devno);
        debug_text_event(unit->port->adapter->erp_dbf, 2, "u_ufail");
        debug_event(unit->port->adapter->erp_dbf, 2, 
                    &unit->fcp_lun, sizeof(fcp_lun_t));

        ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_erp_strategy_check_target
 *
 * purpose:	increments the erp action count on the device currently in recovery if
 *              the action failed or resets the count in case of success. If a maximum
 *              count is exceeded the device is marked as ERP_FAILED.
 *		The 'blocked' state of a target which has been recovered successfully is reset.
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (not considered)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully 
 *		ZFCP_ERP_EXIT		- action failed and will not continue
 */
static int zfcp_erp_strategy_check_target(
		zfcp_erp_action_t *erp_action, 
		int result)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	zfcp_adapter_t *adapter = erp_action->adapter;
	zfcp_port_t *port = erp_action->port;
	zfcp_unit_t *unit = erp_action->unit;

        ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx, result=%d)\n",
		(unsigned long)erp_action,
		result);
        
        debug_text_event(adapter->erp_dbf, 5, "a_stct_norm");
        debug_event(adapter->erp_dbf, 5, &erp_action->action, sizeof(int));
        debug_event(adapter->erp_dbf, 5, &result, sizeof(int));

	switch (erp_action->action) {

		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			result = zfcp_erp_strategy_check_unit(unit, result);
			break;

		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
		case ZFCP_ERP_ACTION_REOPEN_PORT :
			result = zfcp_erp_strategy_check_port(port, result);
			break;

		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			result = zfcp_erp_strategy_check_adapter(adapter, result);
			break;
	}
        
	ZFCP_LOG_TRACE("exit (%d)\n", result);
        
	return result;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_statechange(
		int action,
		u32 status,
                zfcp_adapter_t *adapter,
                zfcp_port_t *port,
                zfcp_unit_t *unit,
                int retval)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (action=%d status=0x%x adapter=0x%lx port=0x%lx "
		"unit=0x%lx retval=0x%x)\n",
		action,
		status,
		(unsigned long)adapter,
		(unsigned long)port,
		(unsigned long)unit,
		retval);
        debug_text_event(adapter->erp_dbf, 5, "a_stsc");
        debug_event(adapter->erp_dbf, 5, &action, sizeof(int));

	switch (action) {

		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			if (zfcp_erp_strategy_statechange_detected(&adapter->status, status)) {
				zfcp_erp_adapter_reopen_internal(
					adapter,
					ZFCP_STATUS_COMMON_ERP_FAILED);
				retval = ZFCP_ERP_EXIT;
			}
			break;

		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
		case ZFCP_ERP_ACTION_REOPEN_PORT :
			if (zfcp_erp_strategy_statechange_detected(&port->status, status)) {
				zfcp_erp_port_reopen_internal(
					port,
					ZFCP_STATUS_COMMON_ERP_FAILED);
				retval = ZFCP_ERP_EXIT;
			}
			break;

		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			if (zfcp_erp_strategy_statechange_detected(&unit->status, status)) {
				zfcp_erp_unit_reopen_internal(
					unit,
					ZFCP_STATUS_COMMON_ERP_FAILED);
				retval = ZFCP_ERP_EXIT;
			}
			break;

		default :
			debug_text_exception(adapter->erp_dbf, 1, "a_stsc_bug");
			debug_event(adapter->erp_dbf, 1, &action, sizeof(int));
			ZFCP_LOG_NORMAL(
				"bug: Unknown error recovery procedure "
				"action requested on the adapter with "
				"devno 0x%04x (debug info %d)\n",
				adapter->devno,
				action);
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static inline int zfcp_erp_strategy_statechange_detected(atomic_t *target_status, u32 erp_status)
{
	return
		/* take it online */
		(atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, target_status) &&
		 (ZFCP_STATUS_ERP_CLOSE_ONLY & erp_status)) ||
		/* take it offline */
		(!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, target_status) &&
		 !(ZFCP_STATUS_ERP_CLOSE_ONLY & erp_status));
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_check_unit(zfcp_unit_t *unit, int result)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx result=%d)\n",
		(unsigned long)unit,
		result);

	debug_text_event(unit->port->adapter->erp_dbf, 5, "u_stct");
	debug_event(unit->port->adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&unit->erp_counter, 0);
		zfcp_erp_unit_unblock(unit);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&unit->erp_counter);
		if (atomic_read(&unit->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_unit_failed(unit);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)) {
		zfcp_erp_unit_block(unit, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", result);

	return result;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_check_port(zfcp_port_t *port, int result)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx result=%d\n",
		(unsigned long)port,
		result);

	debug_text_event(port->adapter->erp_dbf, 5, "p_stct");
	debug_event(port->adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&port->erp_counter, 0);
		zfcp_erp_port_unblock(port);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&port->erp_counter);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_port_failed(port);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		zfcp_erp_port_block(port, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", result);

	return result;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_check_adapter(zfcp_adapter_t *adapter, int result)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx result=%d)\n",
		(unsigned long)adapter,
		result);

	debug_text_event(adapter->erp_dbf, 5, "a_stct");

	switch (result) {
	case ZFCP_ERP_SUCCEEDED :
		atomic_set(&adapter->erp_counter, 0);
		zfcp_erp_adapter_unblock(adapter);
		break;
	case ZFCP_ERP_FAILED :
		atomic_inc(&adapter->erp_counter);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS)
			zfcp_erp_adapter_failed(adapter);
		break;
	case ZFCP_ERP_EXIT :
		/* nothing */
		break;
	}

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status)) {
		zfcp_erp_adapter_block(adapter, 0); /* for ZFCP_ERP_SUCCEEDED */
		result = ZFCP_ERP_EXIT;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", result);

	return result;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	remaining things in good cases,
 *		escalation in bad cases
 *
 * returns:
 */
static int zfcp_erp_strategy_followup_actions(
		int action,
		zfcp_adapter_t *adapter,
		zfcp_port_t *port,
		zfcp_unit_t *unit,
		int status)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (action=%d adapter=0x%lx port=0x%lx "
		"unit=0x%lx status=0x%x)\n",
		action,
		(unsigned long)adapter,
		(unsigned long)port,
		(unsigned long)unit,
		status);
        debug_text_event(adapter->erp_dbf, 5, "a_stfol");
        debug_event(adapter->erp_dbf, 5, &action, sizeof(int));

	/* initiate follow-up actions depending on success of finished action */
	switch (action) {

		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			if (status == ZFCP_ERP_SUCCEEDED)
				zfcp_erp_port_reopen_all_internal(adapter, 0);
			else	zfcp_erp_adapter_reopen_internal(adapter, 0);
			break;

		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
			if (status == ZFCP_ERP_SUCCEEDED)
				zfcp_erp_port_reopen_internal(port, 0);
			else	zfcp_erp_adapter_reopen_internal(adapter, 0);
			break;

		case ZFCP_ERP_ACTION_REOPEN_PORT :
			if (status == ZFCP_ERP_SUCCEEDED)
				zfcp_erp_unit_reopen_all_internal(port, 0);
			else	zfcp_erp_port_forced_reopen_internal(port, 0);
			break;
		
		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			if (status == ZFCP_ERP_SUCCEEDED)
				;/* no further action */
			else	zfcp_erp_port_reopen_internal(unit->port, 0);
			break;

		default :
			debug_text_exception(adapter->erp_dbf, 1, "a_stda_bug");
			debug_event(adapter->erp_dbf, 1, &action, sizeof(int));
			ZFCP_LOG_NORMAL(
				"bug: Unknown error recovery procedure "
				"action requested on the adapter with "
				"devno 0x%04x (debug info %d)\n",
				adapter->devno,
				action);
	}

	ZFCP_LOG_TRACE("exit\n");

	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/**
 * FIXME: document
 */
static int zfcp_erp_strategy_check_queues(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	unsigned long flags;

	ZFCP_LOG_TRACE("enter\n");

	read_lock_irqsave(&adapter->erp_lock, flags);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
                debug_text_event(adapter->erp_dbf, 4, "a_cq_wake");
                atomic_clear_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING,
                                  &adapter->status);
                wake_up(&adapter->erp_done_wqh);
	} else	debug_text_event(adapter->erp_dbf, 5, "a_cq_notempty");
	read_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
int zfcp_erp_wait(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

	wait_event(
		adapter->erp_done_wqh,
		!atomic_test_mask(
			ZFCP_STATUS_ADAPTER_ERP_PENDING,
			&adapter->status));

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_erp_modify_adapter_status
 *
 * purpose:	
 *
 */
static void zfcp_erp_modify_adapter_status(zfcp_adapter_t *adapter, 
                                           u32 mask,
                                           int set_or_clear)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

        zfcp_port_t *port;
        unsigned long flags;
        u32 common_mask=mask & ZFCP_COMMON_FLAGS;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx)\n",
		(unsigned long)adapter);

        if(set_or_clear==ZFCP_SET) {
                atomic_set_mask(mask, &adapter->status);
                debug_text_event(adapter->erp_dbf, 3, "a_mod_as_s");
        } else {
                atomic_clear_mask(mask, &adapter->status);
                if(mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
                        atomic_set(&adapter->erp_counter, 0);
                }
                debug_text_event(adapter->erp_dbf, 3, "a_mod_as_c");
        }
	debug_event(adapter->erp_dbf, 3, &mask, sizeof(u32));

        if(!common_mask) goto out;
        /* Deal with all underlying devices, only pass common_mask */
	read_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
                zfcp_erp_modify_port_status(port,
                                            common_mask,
                                            set_or_clear);
        }
	read_unlock_irqrestore(&adapter->port_list_lock, flags);
 out:
	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_erp_modify_port_status
 *
 * purpose:	sets the port and all underlying devices to ERP_FAILED
 *
 */
static void zfcp_erp_modify_port_status(zfcp_port_t *port,
                                        u32 mask,
                                        int set_or_clear)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

        zfcp_unit_t *unit;
        unsigned long flags;
        u32 common_mask=mask & ZFCP_COMMON_FLAGS;
 
	ZFCP_LOG_TRACE(
		"enter (port=0x%lx)\n",
		(unsigned long)port);

        if(set_or_clear==ZFCP_SET) {
                atomic_set_mask(mask, &port->status);
                debug_text_event(port->adapter->erp_dbf, 3, 
                                 "p_mod_ps_s");
        } else {
                atomic_clear_mask(mask, &port->status);
                if(mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
                        atomic_set(&port->erp_counter, 0);
                }
                debug_text_event(port->adapter->erp_dbf, 3, 
                                 "p_mod_ps_c");
        }
        debug_event(port->adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));
	debug_event(port->adapter->erp_dbf, 3, &mask, sizeof(u32));
       
        if(!common_mask) goto out;
        /* Modify status of all underlying devices, only pass common mask */
	read_lock_irqsave(&port->unit_list_lock, flags);
	ZFCP_FOR_EACH_UNIT(port, unit) {
                zfcp_erp_modify_unit_status(unit,
                                            common_mask,
                                            set_or_clear);
        }
	read_unlock_irqrestore(&port->unit_list_lock, flags);
 out:
	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_erp_modify_unit_status
 *
 * purpose:	sets the unit to ERP_FAILED
 *
 */
static void zfcp_erp_modify_unit_status(zfcp_unit_t *unit,
                                        u32 mask,
                                        int set_or_clear)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx)\n",
		(unsigned long)unit);

        if(set_or_clear==ZFCP_SET) {
                atomic_set_mask(mask, &unit->status);
                debug_text_event(unit->port->adapter->erp_dbf, 3, "u_mod_us_s");
        } else {
                atomic_clear_mask(mask, &unit->status);
                if(mask & ZFCP_STATUS_COMMON_ERP_FAILED) {
                        atomic_set(&unit->erp_counter, 0);
                }
                debug_text_event(unit->port->adapter->erp_dbf, 3, "u_mod_us_c");
        }
        debug_event(unit->port->adapter->erp_dbf, 3, &unit->fcp_lun, sizeof(fcp_lun_t));
	debug_event(unit->port->adapter->erp_dbf, 3, &mask, sizeof(u32));

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	Wrappper for zfcp_erp_port_reopen_all_internal
 *              used to ensure the correct locking
 *
 * returns:	0	- initiated action succesfully
 *		<0	- failed to initiate action
 */
static int zfcp_erp_port_reopen_all(
		zfcp_adapter_t *adapter,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

        write_lock_irqsave(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_reopen_all_internal(adapter, clear_mask);
        write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_port_reopen_all_internal(
		zfcp_adapter_t *adapter,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	unsigned long flags;
	zfcp_port_t *port;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx clear_mask=0x%x)\n",
		(unsigned long)adapter,
		clear_mask);

	read_lock_irqsave(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (!atomic_test_mask(ZFCP_STATUS_PORT_NAMESERVER, &port->status)) 
			zfcp_erp_port_reopen_internal(port, clear_mask);
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_unit_reopen_all_internal(
		zfcp_port_t *port,
		int clear_mask)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	unsigned long flags;
	zfcp_unit_t *unit;

	ZFCP_LOG_TRACE(
		"enter (port=0x%lx clear_mask=0x%x)\n",
		(unsigned long)port,
		clear_mask);

	read_lock_irqsave(&port->unit_list_lock, flags);
	ZFCP_FOR_EACH_UNIT(port, unit)
		zfcp_erp_unit_reopen_internal(unit, clear_mask);
	read_unlock_irqrestore(&port->unit_list_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Adapter' action
 *		(the entire action is processed synchronously, since
 *		there are no actions which might be run concurrently
 *		per definition)
 *
 * returns:	ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_adapter_strategy(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        unsigned long timeout;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");
        
	retval = zfcp_erp_adapter_strategy_close(erp_action);
	if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		retval = ZFCP_ERP_EXIT;
        else	retval = zfcp_erp_adapter_strategy_open(erp_action);

        debug_text_event(adapter->erp_dbf, 3, "a_ast/ret");
        debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
        debug_event(adapter->erp_dbf, 3, &retval, sizeof(int));

        if(retval==ZFCP_ERP_FAILED) {
                /*INFO*/
                ZFCP_LOG_NORMAL("Waiting to allow the adapter with devno "
                              "0x%04x to recover itself\n",
                              adapter->devno);
		/*
		 * SUGGESTION: substitute by
		 * timeout = ZFCP_TYPE2_RECOVERY_TIME;
		 * __ZFCP_WAIT_EVENT_TIMEOUT(timeout, 0);
		 */
                timeout=ZFCP_TYPE2_RECOVERY_TIME;
                set_current_state(TASK_UNINTERRUPTIBLE);
                do { 
                        timeout=schedule_timeout(timeout);
                } while (timeout);
		/* FIXME: why no  current->state = TASK_RUNNING ? */
        }

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_SUCCEEDED      - action finished successfully
 *              ZFCP_ERP_FAILED         - action finished unsuccessfully
 */
static int zfcp_erp_adapter_strategy_close(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE("enter\n");

        atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->adapter->status);
	retval = zfcp_erp_adapter_strategy_generic(erp_action, 1);
        atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->adapter->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_SUCCEEDED      - action finished successfully
 *              ZFCP_ERP_FAILED         - action finished unsuccessfully
 */
static int zfcp_erp_adapter_strategy_open(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE("enter\n");

        atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->adapter->status);
	retval = zfcp_erp_adapter_strategy_generic(erp_action, 0);
        atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->adapter->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_register_adapter
 *
 * purpose:	allocate the irq associated with this devno and register
 *		the FSF adapter with the SCSI stack
 *
 * returns:	
 */
static int zfcp_erp_adapter_strategy_generic(zfcp_erp_action_t *erp_action, int close)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP
 
	int retval = ZFCP_ERP_SUCCEEDED;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

	if (close)
		goto close_only;

	retval = zfcp_erp_adapter_strategy_open_irq(erp_action);
	if (retval != ZFCP_ERP_SUCCEEDED) {
		ZFCP_LOG_INFO(
			"error: Could not setup irq, "
			"adapter with devno 0x%04x. ",
			adapter->devno);
		goto failed_irq;
	}
	ZFCP_LOG_DEBUG(
		"got irq %d (adapter devno=0x%04x)\n",
		adapter->irq,
		adapter->devno);

	/* setup QDIO for this adapter */
	retval = zfcp_erp_adapter_strategy_open_qdio(erp_action);
	if (retval != ZFCP_ERP_SUCCEEDED) {
		ZFCP_LOG_INFO(
			"error: Could not start QDIO (data transfer mechanism) "
                        "adapter with devno 0x%04x.\n",
			adapter->devno);
		goto failed_qdio;
	}
	ZFCP_LOG_DEBUG("QDIO started (adapter devno=0x%04x)\n", adapter->devno);

	/* setup FSF for this adapter */
	retval = zfcp_erp_adapter_strategy_open_fsf(erp_action);
	if (retval != ZFCP_ERP_SUCCEEDED) {
		ZFCP_LOG_INFO(
			"error: Could not communicate with the adapter with "
                        "devno 0x%04x. Card may be busy.\n",
			adapter->devno);
		goto failed_openfcp;
	}
	ZFCP_LOG_DEBUG("FSF started (adapter devno=0x%04x)\n", adapter->devno);

        /* Success */
        atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &erp_action->adapter->status);
        goto out;

close_only:
        atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, 
                          &erp_action->adapter->status); 
failed_openfcp:
	zfcp_erp_adapter_strategy_close_qdio(erp_action);
	zfcp_erp_adapter_strategy_close_fsf(erp_action);
        
failed_qdio:
	zfcp_erp_adapter_strategy_close_irq(erp_action);
        
failed_irq:
                
        /* NOP */
out:
                
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
 return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	gets irq associated with devno and requests irq
 *
 * returns:
 */
static int zfcp_erp_adapter_strategy_open_irq(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_FAILED;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

#if 0
        retval = zfcp_adapter_detect(adapter)
        if (retval == -ENOMEM) {
                retval = ZFCP_ERP_NOMEM;
                goto out;
        }
        if (retval != 0) {
                retval = ZFCP_ERP_FAILED;
                goto out;
        }

        retval = zfcp_adapter_detect(adapter)
        if (retval == -ENOMEM) {
                retval = ZFCP_ERP_NOMEM;
                goto out;
        }
        if (retval != 0) {
                retval = ZFCP_ERP_FAILED;
                goto out;
        }
        retval = ZFCP_ERP_SUCCEEDED;
#endif

	if (zfcp_adapter_detect(adapter) == 0)
		if (zfcp_adapter_irq_register(adapter) == 0)
			retval = ZFCP_ERP_SUCCEEDED;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	releases owned irq
 *
 * returns:
 */
static int zfcp_erp_adapter_strategy_close_irq(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_SUCCEEDED;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_adapter_irq_unregister(erp_action->adapter);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_qdio_init
 *
 * purpose:	setup QDIO operation for specified adapter
 *
 * returns:	0 - successful setup
 *		!0 - failed setup
 */
int zfcp_erp_adapter_strategy_open_qdio(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
	zfcp_adapter_t *adapter = erp_action->adapter;
	int i;
	volatile qdio_buffer_element_t *buffere;
	
        ZFCP_LOG_TRACE("enter\n");

	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		ZFCP_LOG_NORMAL(
			"bug: QDIO (data transfer mechanism) start-up on "
                        "adapter with devno 0x%04x attempted twice. "
                        "Second attempt ignored.",
			adapter->devno);
			goto failed_sanity;
	}

	if (zfcp_initialize_with_0copy(adapter) != 0) {
		ZFCP_LOG_INFO(
			"error: Could not establish queues for QDIO (data "
                        "transfer mechanism) operation on adapter with devno "
                        "0x%04x.\n",
                        adapter->devno);
		goto failed_qdio_initialize;
	}
	ZFCP_LOG_DEBUG("queues established\n");

        /* activate QDIO request and response queue */
	if (qdio_activate(adapter->irq, 0) != 0) {
		ZFCP_LOG_INFO(
			"error: Could not activate queues for QDIO (data "
                        "transfer mechanism) operation on adapter with devno "
                        "0x%04x.\n",
                        adapter->devno);
		goto failed_qdio_activate;
	}
	ZFCP_LOG_DEBUG("queues activated\n");

	/*
	 * put buffers into response queue,
	 */
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; i++) {
		buffere =  &(adapter->response_queue.buffer[i]->element[0]);
		buffere->length = 0;
        	buffere->flags = SBAL_FLAGS_LAST_ENTRY;
		buffere->addr = 0;
	}

        ZFCP_LOG_TRACE(
		"Calling do QDIO irq=0x%x,flags=0x%x, queue_no=%i, "
		"index_in_queue=%i, count=%i\n",
		adapter->irq,
		QDIO_FLAG_SYNC_INPUT,
		0,
		0,
		QDIO_MAX_BUFFERS_PER_Q);	

	retval = do_QDIO(
			adapter->irq,
			QDIO_FLAG_SYNC_INPUT,
			0,
			0,
			QDIO_MAX_BUFFERS_PER_Q,
			NULL);

	if (retval) {
		ZFCP_LOG_NORMAL(
			"bug: QDIO (data transfer mechanism) inobund transfer "
                        "structures could not be set-up (debug info %d)\n",
			retval);
		goto failed_do_qdio;
	} else	{
		adapter->response_queue.free_index = 0;
		atomic_set(&adapter->response_queue.free_count, 0);
		ZFCP_LOG_DEBUG(
			"%i buffers successfully enqueued to response queue\n",
			QDIO_MAX_BUFFERS_PER_Q);
	}

	/* set index of first avalable SBALS / number of available SBALS */
	adapter->request_queue.free_index = 0;
	atomic_set(&adapter->request_queue.free_count, QDIO_MAX_BUFFERS_PER_Q);
        adapter->request_queue.distance_from_int = 0;

	/* initialize waitqueue used to wait for free SBALs in requests queue */
	init_waitqueue_head(&adapter->request_wq);

	/* ok, we did it - skip all cleanups for different failures */
	atomic_set_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
	retval = ZFCP_ERP_SUCCEEDED;

	goto out;

failed_do_qdio:
	/* NOP */

failed_qdio_activate:
        debug_text_event(adapter->erp_dbf, 3, "qdio_down1a");
        while (qdio_cleanup(adapter->irq,
                            QDIO_FLAG_CLEANUP_USING_CLEAR) == -EINPROGRESS) {
                set_current_state(TASK_UNINTERRUPTIBLE);
                schedule_timeout(HZ);
        }
        debug_text_event(adapter->erp_dbf, 3, "qdio_down1b");

	/*
	 * First we had to stop QDIO operation.
	 * Now it is safe to take the following actions.
	 */

failed_qdio_initialize:
failed_sanity:
	retval = ZFCP_ERP_FAILED;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_qdio_cleanup
 *
 * purpose:	cleans up QDIO operation for the specified adapter
 *
 * returns:	0 - successful cleanup
 *		!0 - failed cleanup
 */
int zfcp_erp_adapter_strategy_close_qdio(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP
 
	int retval = ZFCP_ERP_SUCCEEDED;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status)) {
		ZFCP_LOG_DEBUG(
			"Termination of QDIO (data transfer operation) "
                        "attempted for an inactive qdio on the "
                        "adapter with devno 0x%04x....ignored.\n",
			adapter->devno);
		retval = ZFCP_ERP_FAILED;
		goto out;
	}

        /*
         * Get queue_lock and clear QDIOUP flag. Thus it's guaranteed that
         * do_QDIO won't be called while qdio_shutdown is in progress.
         */

        write_lock_irq(&adapter->request_queue.queue_lock);
        atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);
        write_unlock_irq(&adapter->request_queue.queue_lock);

        debug_text_event(adapter->erp_dbf, 3, "qdio_down2a");
        while (qdio_cleanup(adapter->irq,
                            QDIO_FLAG_CLEANUP_USING_CLEAR) == -EINPROGRESS) {
                set_current_state(TASK_UNINTERRUPTIBLE);
                schedule_timeout(HZ);
        }
        debug_text_event(adapter->erp_dbf, 3, "qdio_down2b");

	/*
	 * First we had to stop QDIO operation.
	 * Now it is safe to take the following actions.
	 */

	zfcp_zero_sbals(
		adapter->request_queue.buffer,
		0,
		QDIO_MAX_BUFFERS_PER_Q);
        adapter->response_queue.free_index = 0;
        atomic_set(&adapter->response_queue.free_count, 0);
        adapter->request_queue.free_index = 0;
        atomic_set(&adapter->request_queue.free_count, 0);
        adapter->request_queue.distance_from_int = 0;
out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_init
 *
 * purpose:	initializes FSF operation for the specified adapter
 *
 * returns:	0 - succesful initialization of FSF operation
 *		!0 - failed to initialize FSF operation
 */
static int zfcp_erp_adapter_strategy_open_fsf(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP
 
	int retval;

	ZFCP_LOG_TRACE("enter\n");

	/* do 'exchange configuration data' */
	retval = zfcp_erp_adapter_strategy_open_fsf_xconfig(erp_action);
	if (retval == ZFCP_ERP_FAILED)
		goto out;

	/* start the desired number of Status Reads */
	retval = zfcp_erp_adapter_strategy_open_fsf_statusread(erp_action);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_adapter_strategy_open_fsf_xconfig(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_SUCCEEDED;
	int retries;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

        atomic_clear_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);
	retries = ZFCP_EXCHANGE_CONFIG_DATA_RETRIES;

	do {
		atomic_clear_mask(
			ZFCP_STATUS_ADAPTER_HOST_CON_INIT, 
			&adapter->status);
	        ZFCP_LOG_DEBUG("Doing exchange config data\n");
		zfcp_erp_timeout_init(erp_action);
		if (zfcp_fsf_exchange_config_data(erp_action)) {
			retval = ZFCP_ERP_FAILED;
        	        debug_text_event(adapter->erp_dbf, 5, "a_fstx_xf");
			ZFCP_LOG_INFO(
				"error: Out of resources. Could not "
				"start exchange of configuration data "
				"between the adapter with devno "
				"0x%04x and the device driver.\n",
				adapter->devno);
			break;
		}
		debug_text_event(adapter->erp_dbf, 6, "a_fstx_xok");
		ZFCP_LOG_DEBUG("Xchange underway\n");

		/*
		 * Why this works:
		 * Both the normal completion handler as well as the timeout
		 * handler will do an 'up' when the 'exchange config data'
		 * request completes or times out. Thus, the signal to go on
		 * won't be lost utilizing this semaphore.
		 * Furthermore, this 'adapter_reopen' action is
		 * guaranteed to be the only action being there (highest action
		 * which prevents other actions from being created).
		 * Resulting from that, the wake signal recognized here
		 * _must_ be the one belonging to the 'exchange config
		 * data' request.
		 */
		down_interruptible(&adapter->erp_ready_sem);
		if (erp_action->status & ZFCP_STATUS_ERP_TIMEDOUT) {
			ZFCP_LOG_INFO(
				"error: Exchange of configuration data between "
				"the adapter with devno 0x%04x and the device "
				"driver timed out\n",
				adapter->devno);
			break;
		}
		if (atomic_test_mask(
				ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&adapter->status)) {
                        ZFCP_LOG_DEBUG(
				"Host connection still initialising... "
				"waiting and retrying....\n");
			/* sleep a little bit before retry */
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(ZFCP_EXCHANGE_CONFIG_DATA_SLEEP);
		}
	} while ((retries--) &&
		 atomic_test_mask(
			ZFCP_STATUS_ADAPTER_HOST_CON_INIT, 
			&adapter->status));

	if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status)) {
                ZFCP_LOG_INFO(
			"error: Exchange of configuration data between "
			"the adapter with devno 0x%04x and the device "
			"driver failed.\n",
			adapter->devno);
		retval = ZFCP_ERP_FAILED;;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_adapter_strategy_open_fsf_statusread(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_SUCCEEDED;
        int temp_ret;
	zfcp_adapter_t *adapter = erp_action->adapter;
	int i;

	ZFCP_LOG_TRACE("enter\n");

	adapter->status_read_failed = 0;
	for (i = 0; i < ZFCP_STATUS_READS_RECOM; i++) {
		ZFCP_LOG_TRACE("issuing status read request #%d...\n", i);
		temp_ret = zfcp_fsf_status_read(
				adapter,
				ZFCP_WAIT_FOR_SBAL);
		if (temp_ret < 0) {
        	        ZFCP_LOG_INFO(
				"error: Out of resources. Could not "
				"set-up the infrastructure for "
				"unsolicited status presentation "
				"for the adapter with devno "
				"0x%04x.\n",
				adapter->devno);
			retval = ZFCP_ERP_FAILED;
			i--;
			break;
		}
	}
	ZFCP_LOG_DEBUG("started %i status reads\n", i);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_cleanup
 *
 * purpose:	cleanup FSF operation for specified adapter
 *
 * returns:	0 - FSF operation successfully cleaned up
 *		!0 - failed to cleanup FSF operation for this adapter
 */
static int zfcp_erp_adapter_strategy_close_fsf(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_ERP
 
	int retval = ZFCP_ERP_SUCCEEDED;
	zfcp_adapter_t *adapter = erp_action->adapter;
	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", (unsigned long)adapter);

	/*
	 * wake waiting initiators of requests,
	 * return SCSI commands (with error status),
	 * clean up all requests (synchronously)
	 */
        zfcp_fsf_req_dismiss_all(adapter);
       	/* reset FSF request sequence number */
	adapter->fsf_req_seq_no = 0;
	/* all ports and units are closed */
	zfcp_erp_modify_adapter_status(
		adapter,
		ZFCP_STATUS_COMMON_OPEN,
		ZFCP_CLEAR);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Physical Port' action
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_forced_strategy(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_FAILED;
	zfcp_port_t *port = erp_action->port;
	zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

	switch (erp_action->step) {

		/* FIXME: the ULP spec. begs for waiting for oustanding commands */
		case ZFCP_ERP_STEP_UNINITIALIZED :
			zfcp_erp_port_strategy_clearstati(port);
			/*
			 * it would be sufficient to test only the normal open flag
			 * since the phys. open flag cannot be set if the normal
			 * open flag is unset - however, this is for readabilty ...
			 */
			if (atomic_test_mask(
					(ZFCP_STATUS_PORT_PHYS_OPEN |
					 ZFCP_STATUS_COMMON_OPEN),
				 	&port->status)) {
				ZFCP_LOG_DEBUG(
					"Port WWPN=0x%016Lx is open -> trying close physical\n",
					(llui_t)port->wwpn);
				retval = zfcp_erp_port_forced_strategy_close(erp_action);
			} else	retval = ZFCP_ERP_FAILED;
			break;

		case ZFCP_ERP_STEP_PHYS_PORT_CLOSING :
			if (atomic_test_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status)) { 
				ZFCP_LOG_DEBUG(
					"failed to close physical port WWPN=0x%016Lx\n",
					(llui_t)port->wwpn);
				retval = ZFCP_ERP_FAILED;
			} else	retval = ZFCP_ERP_SUCCEEDED;
			break;
	}

        debug_text_event(adapter->erp_dbf, 3, "p_pfst/ret");
        debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));
        debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
        debug_event(adapter->erp_dbf, 3, &retval, sizeof(int));
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Port' action
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_strategy(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_FAILED;
	zfcp_port_t *port = erp_action->port;
        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

	switch (erp_action->step) {

		/* FIXME: the ULP spec. begs for waiting for oustanding commands */
		case ZFCP_ERP_STEP_UNINITIALIZED :
			zfcp_erp_port_strategy_clearstati(port);
			if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) {
				ZFCP_LOG_DEBUG(
					"port WWPN=0x%016Lx is open -> trying close\n",
					(llui_t)port->wwpn);
				retval = zfcp_erp_port_strategy_close(erp_action);
				goto out;
			} /* else it's already closed, open it */
			break;

		case ZFCP_ERP_STEP_PORT_CLOSING :
                        if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) { 
				ZFCP_LOG_DEBUG(
					"failed to close port WWPN=0x%016Lx\n",
					(llui_t)port->wwpn);
				retval = ZFCP_ERP_FAILED;
				goto out;
			} /* else it's closed now, open it */
			break;
	}
        if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
		retval = ZFCP_ERP_EXIT;
	else	retval = zfcp_erp_port_strategy_open(erp_action);

out:
        debug_text_event(adapter->erp_dbf, 3, "p_pst/ret");
        debug_event(adapter->erp_dbf, 3, &port->wwpn, sizeof(wwn_t));
        debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
        debug_event(adapter->erp_dbf, 3, &retval, sizeof(int));
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_port_strategy_open(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;

	ZFCP_LOG_TRACE("enter\n");

	if (atomic_test_mask(ZFCP_STATUS_PORT_NAMESERVER, &erp_action->port->status))
		retval = zfcp_erp_port_strategy_open_nameserver(erp_action);
	else	retval = zfcp_erp_port_strategy_open_common(erp_action);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * FIXME(design):	currently only prepared for fabric (nameserver!)
 */
static int zfcp_erp_port_strategy_open_common(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	zfcp_adapter_t *adapter = erp_action->adapter;
	zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	switch (erp_action->step) {

		case ZFCP_ERP_STEP_UNINITIALIZED :
		case ZFCP_ERP_STEP_PHYS_PORT_CLOSING :
		case ZFCP_ERP_STEP_PORT_CLOSING :
			if (!(adapter->nameserver_port)) {
				retval = zfcp_nameserver_enqueue(adapter);
				if (retval == -ENOMEM) {
					retval = ZFCP_ERP_NOMEM;
					break;
				}
				if (retval != 0) {
					ZFCP_LOG_NORMAL(
						"error: nameserver port not available "
						"(adapter with devno 0x%04x)\n",
						adapter->devno);
					retval = ZFCP_ERP_FAILED;
					break;
				}
			}
			if (!atomic_test_mask(
					ZFCP_STATUS_COMMON_UNBLOCKED, 
					&adapter->nameserver_port->status)) {
				ZFCP_LOG_DEBUG(
					"nameserver port is not open -> open nameserver port\n");
                                /* nameserver port may live again */
				atomic_set_mask(
					ZFCP_STATUS_COMMON_RUNNING,
					&adapter->nameserver_port->status); 
				if (zfcp_erp_port_reopen(adapter->nameserver_port, 0) >= 0) {
					erp_action->step = ZFCP_ERP_STEP_NAMESERVER_OPEN;
					retval = ZFCP_ERP_CONTINUES;
				} else	retval = ZFCP_ERP_FAILED;
				break;
			} /* else nameserver port is already open, fall through */

		case ZFCP_ERP_STEP_NAMESERVER_OPEN :
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, 
                                              &adapter->nameserver_port->status)) {
				ZFCP_LOG_DEBUG("failed to open nameserver port\n");
				retval = ZFCP_ERP_FAILED;
			} else	{
				ZFCP_LOG_DEBUG(
					"nameserver port is open -> "
					"ask nameserver for current D_ID of port with WWPN 0x%016Lx\n",
					(llui_t)port->wwpn);
				retval = zfcp_erp_port_strategy_open_common_lookup(erp_action);
			}
			break;

		case ZFCP_ERP_STEP_NAMESERVER_LOOKUP :
			if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status)) { 
				if (atomic_test_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status)) {
					ZFCP_LOG_DEBUG(
						"failed to look up the D_ID of the port WWPN=0x%016Lx "
						"(misconfigured WWPN?)\n",
						(llui_t)port->wwpn);
                                        zfcp_erp_port_failed(port);
					retval = ZFCP_ERP_EXIT;
				} else	{
					ZFCP_LOG_DEBUG(
						"failed to look up the D_ID of the port WWPN=0x%016Lx\n",
						(llui_t)port->wwpn);
					retval = ZFCP_ERP_FAILED;
				}
			} else	{
				ZFCP_LOG_DEBUG(
					"port WWPN=0x%016Lx has D_ID=0x%06x -> trying open\n",
					(llui_t)port->wwpn,
					port->d_id);
				retval = zfcp_erp_port_strategy_open_port(erp_action);
			}
			break;

		case ZFCP_ERP_STEP_PORT_OPENING :
			if (atomic_test_mask(
					(ZFCP_STATUS_COMMON_OPEN |
					 ZFCP_STATUS_PORT_DID_DID),/* D_ID might have changed during open */
					&port->status)) {
				ZFCP_LOG_DEBUG(
					"port WWPN=0x%016Lx is open ",
					(llui_t)port->wwpn);
				retval = ZFCP_ERP_SUCCEEDED;
			} else	{
				ZFCP_LOG_DEBUG(
					"failed to open port WWPN=0x%016Lx\n",
					(llui_t)port->wwpn);
				retval = ZFCP_ERP_FAILED;
			}
			break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: unkown erp step 0x%x\n",
				erp_action->step);
			retval = ZFCP_ERP_FAILED;
	}
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_port_strategy_open_nameserver(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
	zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	switch (erp_action->step) {

		case ZFCP_ERP_STEP_UNINITIALIZED :
		case ZFCP_ERP_STEP_PHYS_PORT_CLOSING :
		case ZFCP_ERP_STEP_PORT_CLOSING :
			ZFCP_LOG_DEBUG(
				"port WWPN=0x%016Lx has D_ID=0x%06x -> trying open\n",
				(llui_t)port->wwpn,
				port->d_id);
			retval = zfcp_erp_port_strategy_open_port(erp_action);
			break;

		case ZFCP_ERP_STEP_PORT_OPENING :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &port->status)) {
				ZFCP_LOG_DEBUG("nameserver port is open\n");
				retval = ZFCP_ERP_SUCCEEDED;
			} else	{
				ZFCP_LOG_DEBUG("failed to open nameserver port\n");
				retval = ZFCP_ERP_FAILED;
			}
			/* this is needed anyway (dont care for retval of wakeup) */
			ZFCP_LOG_DEBUG("continue other open port operations\n");
			zfcp_erp_port_strategy_open_nameserver_wakeup(erp_action);
			break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: unkown erp step 0x%x\n",
				erp_action->step);
			retval = ZFCP_ERP_FAILED;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	makes the erp thread continue with reopen (physical) port
 *		actions which have been paused until the name server port
 *		is opened (or failed)
 *
 * returns:	0	(a kind of void retval, its not used)
 */
static int zfcp_erp_port_strategy_open_nameserver_wakeup(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	unsigned long flags;
	zfcp_adapter_t *adapter = erp_action->adapter;
	struct list_head *entry, *temp_entry;
	zfcp_erp_action_t *tmp_erp_action;

	ZFCP_LOG_TRACE("enter\n");

	write_lock_irqsave(&adapter->erp_lock, flags);
	list_for_each_safe(entry, temp_entry, &adapter->erp_running_head) {
		tmp_erp_action = list_entry(entry, zfcp_erp_action_t, list);
		debug_text_event(adapter->erp_dbf, 3, "p_pstnsw_n");
		debug_event(adapter->erp_dbf, 3, &tmp_erp_action->port->wwpn, sizeof(wwn_t));
		if (tmp_erp_action->step == ZFCP_ERP_STEP_NAMESERVER_OPEN) {
			debug_text_event(adapter->erp_dbf, 3, "p_pstnsw_w");
			debug_event(adapter->erp_dbf, 3, &tmp_erp_action->port->wwpn, sizeof(wwn_t));
			if (atomic_test_mask(
					ZFCP_STATUS_COMMON_ERP_FAILED,
					&adapter->nameserver_port->status))
				zfcp_erp_port_failed(tmp_erp_action->port);
			zfcp_erp_action_ready(tmp_erp_action);
		}
	}
	write_unlock_irqrestore(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_forced_strategy_close(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_physical_port(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "o_pfstc_nomem");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PHYS_PORT_CLOSING;
	if (retval != 0) {
                debug_text_event(adapter->erp_dbf, 5, "o_pfstc_cpf");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "o_pfstc_cpok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof(wwn_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_port_strategy_clearstati(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter = port->adapter;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf, 5, "p_pstclst");
        debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));

	atomic_clear_mask(
		ZFCP_STATUS_COMMON_OPENING |
		ZFCP_STATUS_COMMON_CLOSING |
		ZFCP_STATUS_PORT_DID_DID |
		ZFCP_STATUS_PORT_PHYS_CLOSING |
		ZFCP_STATUS_PORT_INVALID_WWPN,
		&port->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_strategy_close(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_port(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "p_pstc_nomem");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PORT_CLOSING;
	if (retval != 0) {
		debug_text_event(adapter->erp_dbf, 5, "p_pstc_cpf");
		debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* could not send 'close', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_pstc_cpok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof(wwn_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_strategy_open_port(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_open_port(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "p_psto_nomem");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_PORT_OPENING;
	if (retval != 0) {
                debug_text_event(adapter->erp_dbf, 5, "p_psto_opf");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_psto_opok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof(wwn_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_port_strategy_open_common_lookup(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_port_t *port = erp_action->port;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_ns_gid_pn_request(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "p_pstn_nomem");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_NAMESERVER_LOOKUP;
	if (retval != 0) {
                debug_text_event(adapter->erp_dbf, 5, "p_pstn_ref");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* could not send nameserver request, fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "p_pstn_reok");
	debug_event(adapter->erp_dbf, 6, &port->wwpn, sizeof(wwn_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	this routine executes the 'Reopen Unit' action
 *		currently no retries
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_SUCCEEDED	- action finished successfully
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_unit_strategy(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = ZFCP_ERP_FAILED;
	zfcp_unit_t *unit = erp_action->unit;
        zfcp_adapter_t *adapter=erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");

	switch (erp_action->step) {

		/* FIXME: the ULP spec. begs for waiting for oustanding commands */
		case ZFCP_ERP_STEP_UNINITIALIZED :
			zfcp_erp_unit_strategy_clearstati(unit);
			if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) {
				ZFCP_LOG_DEBUG(
					"unit FCP_LUN=0x%016Lx is open -> trying close\n",
					(llui_t)unit->fcp_lun);
				retval = zfcp_erp_unit_strategy_close(erp_action);
				break;
			} /* else it's already closed, fall through */

		case ZFCP_ERP_STEP_UNIT_CLOSING :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) {
				ZFCP_LOG_DEBUG(
					"failed to close unit FCP_LUN=0x%016Lx\n",
					(llui_t)unit->fcp_lun);
				retval = ZFCP_ERP_FAILED;
			} else	{
				if (erp_action->status & ZFCP_STATUS_ERP_CLOSE_ONLY)
			                retval = ZFCP_ERP_EXIT;
				else	{
					ZFCP_LOG_DEBUG(
						"unit FCP_LUN=0x%016Lx is not open -> trying open\n",
						(llui_t)unit->fcp_lun);
					retval = zfcp_erp_unit_strategy_open(erp_action);
				}
			}
			break;

		case ZFCP_ERP_STEP_UNIT_OPENING :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status)) { 
				ZFCP_LOG_DEBUG(
					"unit FCP_LUN=0x%016Lx is open\n",
					(llui_t)unit->fcp_lun);
				retval = ZFCP_ERP_SUCCEEDED;
			} else	{
				ZFCP_LOG_DEBUG(
					"failed to open unit FCP_LUN=0x%016Lx\n",
					(llui_t)unit->fcp_lun);
				retval = ZFCP_ERP_FAILED;
			}
			break;
	}

        debug_text_event(adapter->erp_dbf, 3, "u_ust/ret");
        debug_event(adapter->erp_dbf, 3, &unit->fcp_lun, sizeof(fcp_lun_t));
        debug_event(adapter->erp_dbf, 3, &erp_action->action, sizeof(int));
        debug_event(adapter->erp_dbf, 3, &retval, sizeof(int));
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_unit_strategy_clearstati(zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=unit->port->adapter;

	ZFCP_LOG_TRACE("enter\n");
       
        debug_text_event(adapter->erp_dbf,5,"u_ustclst");
        debug_event(adapter->erp_dbf,5,&unit->fcp_lun,
                    sizeof(fcp_lun_t));

	atomic_clear_mask(
		ZFCP_STATUS_COMMON_OPENING |
		ZFCP_STATUS_COMMON_CLOSING,
		&unit->status);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_unit_strategy_close(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_unit_t *unit = erp_action->unit;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_close_unit(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "u_ustc_nomem");
                debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_UNIT_CLOSING;
	if (retval != 0) {
                debug_text_event(adapter->erp_dbf, 5, "u_ustc_cuf");
                debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));
		/* could not send 'close', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "u_ustc_cuok");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof(fcp_lun_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:	ZFCP_ERP_CONTINUES	- action continues (asynchronously)
 *		ZFCP_ERP_FAILED		- action finished unsuccessfully
 */
static int zfcp_erp_unit_strategy_open(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval;
        zfcp_adapter_t *adapter = erp_action->adapter;
        zfcp_unit_t *unit = erp_action->unit;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_erp_timeout_init(erp_action);
	retval = zfcp_fsf_open_unit(erp_action);
	if (retval == -ENOMEM) {
                debug_text_event(adapter->erp_dbf, 5, "u_usto_nomem");
                debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));
		retval = ZFCP_ERP_NOMEM;
		goto out;
	}
	erp_action->step = ZFCP_ERP_STEP_UNIT_OPENING;
	if (retval != 0) {
                debug_text_event(adapter->erp_dbf, 5, "u_usto_ouf");
                debug_event(adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));
		/* could not send 'open', fail */
		retval = ZFCP_ERP_FAILED;
		goto out;
	}
	debug_text_event(adapter->erp_dbf, 6, "u_usto_ouok");
	debug_event(adapter->erp_dbf, 6, &unit->fcp_lun, sizeof(fcp_lun_t));
	retval = ZFCP_ERP_CONTINUES;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static /*inline*/ int zfcp_erp_timeout_init(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter=erp_action->adapter;

	ZFCP_LOG_TRACE("enter\n");
        
        debug_text_event(adapter->erp_dbf, 6, "a_timinit");
        debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof(int));
	init_timer(&erp_action->timer);
	erp_action->timer.function = zfcp_erp_timeout_handler;
	erp_action->timer.data = (unsigned long)erp_action;
        /* jiffies will be added in zfcp_fsf_req_send */
	erp_action->timer.expires = ZFCP_ERP_FSFREQ_TIMEOUT;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	enqueue the specified error recovery action, if needed
 *
 * returns:
 */
static int zfcp_erp_action_enqueue(
	int action,
	zfcp_adapter_t *adapter,
	zfcp_port_t *port,
	zfcp_unit_t *unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 1;
	zfcp_erp_action_t *erp_action = NULL;
	int stronger_action = 0;
	u32 status = 0;

	ZFCP_LOG_TRACE(
		"enter (action=%d adapter=0x%lx "
		"port=0x%lx unit=0x%lx)\n",
		action,
		(unsigned long)adapter,
		(unsigned long)port,
		(unsigned long)unit);

	/*
	 * We need some rules here which check whether we really need
	 * this action or whether we should just drop it.
	 * E.g. if there is a unfinished 'Reopen Port' request then we drop a
	 * 'Reopen Unit' request for an associated unit since we can't
	 * satisfy this request now. A 'Reopen Port' action will trigger
	 * 'Reopen Unit' actions when it completes.
	 * Thus, there are only actions in the queue which can immediately be
	 * executed. This makes the processing of the action queue more
	 * efficient.
	 */

        debug_text_event(adapter->erp_dbf, 4, "a_actenq");
        debug_event(adapter->erp_dbf, 4, &action, sizeof(int));
	/* check whether we really need this */
	switch (action) {
		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status)) {
                                debug_text_event(adapter->erp_dbf, 4, "u_actenq_drp");
                                debug_event(adapter->erp_dbf, 4, &unit->fcp_lun, sizeof(fcp_lun_t));
				ZFCP_LOG_DEBUG(
					"drop: erp action %i on unit "
					"FCP_LUN=0x%016Lx "
					"(erp action %i already exists)\n",
					action,
					(llui_t)unit->fcp_lun,
					unit->erp_action.action);
				goto out;
			}
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &port->status)) { 
				stronger_action = ZFCP_ERP_ACTION_REOPEN_PORT;
				unit = NULL;
			}
			/* fall through !!! */

		case ZFCP_ERP_ACTION_REOPEN_PORT :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status)) {
                                debug_text_event(adapter->erp_dbf, 4, "p_actenq_drp");
                                debug_event(adapter->erp_dbf, 4, &port->wwpn, sizeof(wwn_t));
				ZFCP_LOG_DEBUG(
					"drop: erp action %i on port "
					"WWPN=0x%016Lx "
					"(erp action %i already exists)\n",
					action,
					(llui_t)port->wwpn,
					port->erp_action.action);
				goto out;
			}
			/* fall through !!! */

		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status) &&
			    port->erp_action.action == ZFCP_ERP_ACTION_REOPEN_PORT_FORCED) {
                                debug_text_event(adapter->erp_dbf, 4, "pf_actenq_drp");
                                debug_event(adapter->erp_dbf, 4, &port->wwpn, sizeof(wwn_t));
				ZFCP_LOG_DEBUG(
					"drop: erp action %i on port "
					"WWPN=0x%016Lx "
					"(erp action %i already exists)\n",
					action,
					(llui_t)port->wwpn,
					port->erp_action.action);
				goto out;
			}
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &adapter->status)) {
				stronger_action = ZFCP_ERP_ACTION_REOPEN_ADAPTER;
				port = NULL;
			}
			/* fall through !!! */

		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status)) {
                                debug_text_event(adapter->erp_dbf, 4, "a_actenq_drp");
				ZFCP_LOG_DEBUG(
					"drop: erp action %i on adapter "
					"devno=0x%04x "
					"(erp action %i already exists)\n",
					action,
					adapter->devno,
					adapter->erp_action.action);
				goto out;
			}
			break;

		default :
                        debug_text_exception(adapter->erp_dbf, 1, "a_actenq_bug");
                        debug_event(adapter->erp_dbf, 1, &action, sizeof(int));
                        ZFCP_LOG_NORMAL(
				"bug: Unknown error recovery procedure "
				"action requested on the adapter with "
				"devno 0x%04x "
				"(debug info %d)\n",
				adapter->devno,
				action);
			goto out;
	}

	/* check whether we need something stronger first */
	if (stronger_action) {
                debug_text_event(adapter->erp_dbf, 4, "a_actenq_str");
                debug_event(adapter->erp_dbf, 4, &stronger_action, sizeof(int));
		ZFCP_LOG_DEBUG(
			"shortcut: need erp action %i before "
			"erp action %i (adapter devno=0x%04x)\n",
			stronger_action,
			action,
			adapter->devno);
		action = stronger_action;
	}

	/* mark adapter to have some error recovery pending */
	atomic_set_mask(ZFCP_STATUS_ADAPTER_ERP_PENDING, &adapter->status);

	/* setup error recovery action */
	switch (action) {

		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status);
			erp_action = &unit->erp_action;
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status))
				status = ZFCP_STATUS_ERP_CLOSE_ONLY;
			break;

		case ZFCP_ERP_ACTION_REOPEN_PORT :
		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
			zfcp_erp_action_dismiss_port(port);
			atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status);
			erp_action = &port->erp_action;
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status))
				status = ZFCP_STATUS_ERP_CLOSE_ONLY;
			break;

		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			zfcp_erp_action_dismiss_adapter(adapter);
			atomic_set_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status);
			erp_action = &adapter->erp_action;
			if (!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &adapter->status))
				status = ZFCP_STATUS_ERP_CLOSE_ONLY;
			break;
	}

	memset(erp_action, 0, sizeof(zfcp_erp_action_t));
	erp_action->adapter = adapter;
	erp_action->port = port;
	erp_action->unit = unit;
	erp_action->action = action;
	erp_action->status = status;

	/* finally put it into 'ready' queue and kick erp thread */
	list_add(&erp_action->list, &adapter->erp_ready_head);
	ZFCP_LOG_DEBUG(
		"waking erp_thread of the adapter with devno=0x%04x\n",
		adapter->devno);
	up(&adapter->erp_ready_sem);
	retval = 0;

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_action_dequeue(
                zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx)\n",
		(unsigned long)erp_action);

        debug_text_event(adapter->erp_dbf, 4, "a_actdeq");
        debug_event(adapter->erp_dbf, 4, &erp_action->action, sizeof(int));
        list_del(&erp_action->list);
	switch (erp_action->action) {
		case ZFCP_ERP_ACTION_REOPEN_UNIT :
			atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &erp_action->unit->status);
			break;
		case ZFCP_ERP_ACTION_REOPEN_PORT_FORCED :
		case ZFCP_ERP_ACTION_REOPEN_PORT :
			atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &erp_action->port->status);
			break;
		case ZFCP_ERP_ACTION_REOPEN_ADAPTER :
			atomic_clear_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &erp_action->adapter->status);
			break;
		default :
			/* bug */
			break;
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_action_dismiss_adapter(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	zfcp_port_t *port;
	unsigned long flags;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf, 5, "a_actab");
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status))
		/* that's really all in this case */
		zfcp_erp_action_dismiss(&adapter->erp_action);
	else	{
		/* have a deeper look */
		read_lock_irqsave(&adapter->port_list_lock, flags);
		ZFCP_FOR_EACH_PORT(adapter, port) {
			zfcp_erp_action_dismiss_port(port);
		}
		read_unlock_irqrestore(&adapter->port_list_lock, flags);	
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_action_dismiss_port(zfcp_port_t *port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	zfcp_unit_t *unit;
        zfcp_adapter_t *adapter = port->adapter;
	unsigned long flags;

	ZFCP_LOG_TRACE("enter\n");

        debug_text_event(adapter->erp_dbf, 5, "p_actab");
        debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &port->status))
		/* that's really all in this case */
		zfcp_erp_action_dismiss(&port->erp_action);
	else	{
		/* have a deeper look */
		read_lock_irqsave(&port->unit_list_lock, flags);
		ZFCP_FOR_EACH_UNIT(port, unit) {
                        if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status)) {
                                zfcp_erp_action_dismiss(&unit->erp_action);
                        }
		}
		read_unlock_irqrestore(&port->unit_list_lock, flags);	
	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	moves erp_action to 'erp running list'
 *
 * returns:
 */
static inline void zfcp_erp_action_to_running(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx\n",
		(unsigned long)erp_action);

        debug_text_event(adapter->erp_dbf, 6, "a_toru");
        debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof(int));
        zfcp_erp_from_one_to_other(
		&erp_action->list,
		&erp_action->adapter->erp_running_head);

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	moves erp_action to 'erp ready list'
 *
 * returns:
 */
static inline void zfcp_erp_action_to_ready(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

        zfcp_adapter_t *adapter = erp_action->adapter;

	ZFCP_LOG_TRACE(
		"enter (erp_action=0x%lx\n",
		(unsigned long)erp_action);

        debug_text_event(adapter->erp_dbf, 6, "a_tore");
        debug_event(adapter->erp_dbf, 6, &erp_action->action, sizeof(int));
	zfcp_erp_from_one_to_other(
		&erp_action->list,
		&erp_action->adapter->erp_ready_head);

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	
 *
 * purpose:	moves a request from one erp_action list to the other
 *
 * returns:
 */
static inline void zfcp_erp_from_one_to_other(
	struct list_head *entry,
	struct list_head *head)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	ZFCP_LOG_TRACE(
		"enter entry=0x%lx, head=0x%lx\n",
		(unsigned long)entry,
                (unsigned long)head);

	list_del(entry);
	list_add(entry, head);

	ZFCP_LOG_TRACE("exit\n");

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


#ifdef ZFCP_STAT_REQSIZES

static int zfcp_statistics_clear(
		zfcp_adapter_t *adapter,
		struct list_head *head)
{
	int retval = 0;
	unsigned long flags;
	struct list_head *entry, *next_entry;
	zfcp_statistics_t *stat;

	write_lock_irqsave(&adapter->stat_lock, flags);
	list_for_each_safe(entry, next_entry, head) {
		stat = list_entry(entry, zfcp_statistics_t, list);
		list_del(entry);
		kfree(stat);
	}
	write_unlock_irqrestore(&adapter->stat_lock, flags);

	return retval;
}


static inline void zfcp_statistics_new(
		zfcp_adapter_t *adapter,
		struct list_head *head,
		u32 num)
{
	zfcp_statistics_t *stat;

	stat = ZFCP_KMALLOC(sizeof(zfcp_statistics_t), GFP_ATOMIC);
	if (stat) {
		stat->num = num;
		stat->hits = 1;
		list_add_tail(&stat->list, head);
	} else	atomic_inc(&adapter->stat_errors);
}

/**
 * list_for_some_prev   -       iterate over a list backwards
 * 				starting somewhere in the middle
 *				of the list
 * @pos:        the &list_t to use as a loop counter.
 * @middle:	the &list_t pointing to the antecessor to start at
 * @head:       the head for your list.
 */
#define list_for_some_prev(pos, middle, head) \
	for (pos = (middle)->prev, prefetch(pos->prev); pos != (head); \
		pos = pos->prev, prefetch(pos->prev))

/*
 * Sort list if necessary to find frequently used entries quicker.
 * Since counters are only increased by one, sorting can be implemented
 * in a quite efficient way. It usually comprimises swapping positions
 * of the given entry with its antecessor, if at all. In rare cases
 * (= if there is a series of antecessors with identical counter values
 * which are in turn less than the value hold by the current entry)
 * searching for the position where we want to move the current entry to
 * takes more than one hop back through the list. As to the overall
 * performance of our statistics this is not a big deal.
 * As a side-effect, we provide statistics sorted by hits to the user.
 */
static inline void zfcp_statistics_sort(
		struct list_head *head,
		struct list_head *entry,
		zfcp_statistics_t *stat)
{
	zfcp_statistics_t *stat_sort = NULL;
	struct list_head *entry_sort = NULL;

	list_for_some_prev(entry_sort, entry, head) {
		stat_sort = list_entry(entry_sort, zfcp_statistics_t, list);
		if (stat_sort->hits >= stat->hits)
			break;
	}
	if (stat_sort &&
	    entry->prev != entry_sort)
		list_move(entry, entry_sort);
}


static void zfcp_statistics_inc(
		zfcp_adapter_t *adapter,
                struct list_head *head,
		u32 num)
{
        unsigned long flags;
        zfcp_statistics_t *stat;
        struct list_head *entry;

	if (atomic_read(&adapter->stat_on) == 0)
		return;

        write_lock_irqsave(&adapter->stat_lock, flags);
        list_for_each(entry, head) {
                stat = list_entry(entry, zfcp_statistics_t, list);
                if (stat->num == num) {
                        stat->hits++;
			zfcp_statistics_sort(head, entry, stat);
                        goto unlock;
                }
        }
        /* hits is initialized to 1 */
        zfcp_statistics_new(adapter, head, num);
unlock:
        write_unlock_irqrestore(&adapter->stat_lock, flags);
}


static int zfcp_statistics_print(
		zfcp_adapter_t *adapter,
                struct list_head *head,
		char *prefix,
		char *buf,
		int len,
		int max)
{
        unsigned long flags;
        zfcp_statistics_t *stat;
        struct list_head *entry;

        write_lock_irqsave(&adapter->stat_lock, flags);
	list_for_each(entry, head) {
		if (len > max - 26)
			break;
		stat = list_entry(entry, zfcp_statistics_t, list);
		len += sprintf(buf + len, "%s 0x%08x: 0x%08x\n",
			       prefix, stat->num, stat->hits);
        }
        write_unlock_irqrestore(&adapter->stat_lock, flags);

        return len;
}

#endif // ZFCP_STAT_REQSIZES


//EXPORT_SYMBOL(zfcp_data);

/*
 * Overrides for Emacs so that we get a uniform tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
