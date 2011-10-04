/*
 * FCP adapter driver for IBM eServer zSeries
 *
 * Authors:
 *	Martin Peschke <mpeschke@de.ibm.com>
 *      Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *	Aron Zeh <arzeh@de.ibm.com>
 *      Wolfgang Taphorn <taphorn@de.ibm.com>
 *      Stefan Bader <stefan.bader@de.ibm.com>
 *
 * Copyright (C) 2002 IBM Entwicklung GmbH, IBM Corporation
 */

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_REVISION		"$Revision: 3.157.6.5 $"

#define ZFCP_QTCB_VERSION	FSF_QTCB_CURRENT_VERSION

#define ZFCP_PRINT_FLAGS

#undef ZFCP_CAUSE_ERRORS

#undef ZFCP_MEMORY_DEBUG

#undef ZFCP_MEM_POOL_ONLY

#define ZFCP_LOW_MEM_CREDITS

#define ZFCP_DEBUG_REQUESTS	/* fsf_req tracing */

#define ZFCP_DEBUG_COMMANDS	/* host_byte tracing */

#define ZFCP_DEBUG_ABORTS	/* scsi_cmnd abort tracing */

#define ZFCP_DEBUG_INCOMING_ELS	/* incoming ELS tracing */

#undef ZFCP_RESID

#if 0
unsigned long error_counter=0;
#endif

#define ZFCP_STAT_REQSIZES
#define ZFCP_STAT_QUEUES
#define ZFCP_STAT_REQ_QUEUE_LOCK

// current implementation does not work due to proc_sema
#undef ZFCP_ERP_DEBUG_SINGLE_STEP

#ifdef ZFCP_CAUSE_ERRORS
struct timer_list zfcp_force_error_timer;
#endif

/* ATTENTION: value must not be used by hardware */
#define FSF_QTCB_UNSOLICITED_STATUS		0x6305

/************************ DEBUG FLAGS *****************************************/
/* enables a faked SCSI command
 completion, via timer */
#undef	ZFCP_FAKE_SCSI_COMPLETION
#define ZFCP_FAKE_SCSI_COMPLETION_TIME	        (HZ / 3)

#define ZFCP_SCSI_LOW_MEM_TIMEOUT               (100*HZ)

#define ZFCP_SCSI_ER_TIMEOUT                    (100*HZ)

/********************* QDIO SPECIFIC DEFINES *********************************/

/* allow as much chained SBALs as supported by hardware */
#define ZFCP_MAX_SBALS_PER_REQ		FSF_MAX_SBALS_PER_REQ
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

/********************** INCLUDES *********************************************/
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
#include <linux/blk.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/time.h>

#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "../../fc4/fc.h"

#include <linux/module.h>

#include <asm/fsf.h>			/* FSF SW Interface */

#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/qdio.h>
#include <asm/uaccess.h>

#include <asm/ebcdic.h>
#include <asm/s390dyn.h>
#include <asm/cpcmd.h>               /* Debugging only */
#include <asm/processor.h>           /* Debugging only */
#include <asm/debug.h>
#include <asm/div64.h>

/* Cosmetics */
#ifndef atomic_test_mask
#define atomic_test_mask(mask, target) \
           ((atomic_read(target) & mask) == mask)
#endif

#define ZFCP_FSFREQ_CLEANUP_TIMEOUT	HZ/10

#define ZFCP_TYPE2_RECOVERY_TIME        8*HZ

#ifdef ZFCP_STAT_REQSIZES
#define ZFCP_MAX_PROC_SIZE              4 * PAGE_SIZE
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

#define ZFCP_NAME			"zfcp"
#define ZFCP_PARM_FILE                  "mod_parm"
#define ZFCP_MAP_FILE                   "map"
#define ZFCP_ADD_MAP_FILE               "add_map"
#define ZFCP_STATUS_FILE                "status"
#define ZFCP_MAX_PROC_LINE              1024

#define ZFCP_RESET_ERP			"reset erp"

#define ZFCP_DID_MASK                   0x00ffffff

/* Adapter Identification Parameters */
#define ZFCP_CONTROL_UNIT_TYPE  0x1731
#define ZFCP_CONTROL_UNIT_MODEL 0x03
#define ZFCP_DEVICE_TYPE        0x1732
#define ZFCP_DEVICE_MODEL       0x03
 
#define ZFCP_FC_SERVICE_CLASS_DEFAULT	FSF_CLASS_3

/* timeout for name-server lookup (in seconds) */
/* FIXME(tune) */
#define ZFCP_NAMESERVER_TIMEOUT		10

#define ZFCP_EXCHANGE_CONFIG_DATA_RETRIES	6
#define ZFCP_EXCHANGE_CONFIG_DATA_SLEEP		50

#define ZFCP_STATUS_READS_RECOM		FSF_STATUS_READS_RECOM

#define ZFCP_QTCB_SIZE		(sizeof(fsf_qtcb_t) + FSF_QTCB_LOG_SIZE)
#define ZFCP_QTCB_AND_REQ_SIZE	(sizeof(zfcp_fsf_req_t) + ZFCP_QTCB_SIZE)

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
#define ZFCP_REQ_DBF_LEVEL	1
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
#define ZFCP_LOG_LEVEL_LIMIT	ZFCP_LOG_LEVEL_TRACE

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


#define ZFCP_READ_LOCK(lock) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"rlock"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
	read_lock(lock);  \
        }
          
#define ZFCP_READ_UNLOCK(lock) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"rulock"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
	read_unlock(lock); \
        }

#define ZFCP_READ_LOCK_IRQSAVE(lock, flags) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"rlocki"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
	read_lock_irqsave(lock, flags); \
        }

#define ZFCP_READ_UNLOCK_IRQRESTORE(lock, flags) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"rulocki"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
	read_unlock_irqrestore(lock, flags); \
        }


#define ZFCP_WRITE_LOCK(lock) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"wlock"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
        write_lock(lock); \
        }

#define ZFCP_WRITE_UNLOCK(lock) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"wulock"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
        write_unlock(lock); \
        }

#define ZFCP_WRITE_LOCK_IRQSAVE(lock, flags) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"wlocki"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
        write_lock_irqsave(lock, flags); \
        }

#define ZFCP_WRITE_UNLOCK_IRQRESTORE(lock, flags) \
        { \
        void *__sp_lock_addr=lock; \
        debug_text_event(zfcp_data.spinlock_dbf,3,"wulocki"); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sp_lock_addr, \
                    sizeof(unsigned long)); \
        write_unlock_irqrestore(lock, flags); \
        }


#define ZFCP_UP(sema) \
        { \
        void *__sema_addr=sema; \
        char action[8]; \
        sprintf(action, "u%d", atomic_read(sema.count)); \
        debug_text_event(zfcp_data.spinlock_dbf,3,action); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sema_addr, \
                    sizeof(unsigned long)); \
        up(sema); \
        }

#define ZFCP_DOWN(sema) \
        { \
        void *__sema_addr=sema; \
        char action[8]; \
        sprintf(action, "d%d", atomic_read(sema.count)); \
        debug_text_event(zfcp_data.spinlock_dbf,3,action); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sema_addr, \
                    sizeof(unsigned long)); \
        down(sema); \
        }

#define ZFCP_DOWN_INTERRUPTIBLE(sema) \
        { \
        void *__sema_addr=sema; \
        char action[8]; \
        sprintf(action, "di%d", atomic_read(sema.count)); \
        debug_text_event(zfcp_data.spinlock_dbf,3,action); \
        debug_event(zfcp_data.spinlock_dbf,3,&__sema_addr, \
                    sizeof(unsigned long)); \
        down_interruptible(sema); \
        }


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


/* record generated from parsed conf. lines */
typedef struct _zfcp_config_record {
	int			valid;
	unsigned long		devno;
	unsigned long		scsi_id;
	unsigned long long	wwpn;
	unsigned long		scsi_lun;
	unsigned long long	fcp_lun;
} zfcp_config_record_t;

/* General type defines */

typedef long long unsigned int  llui_t;

/* 32 bit for SCSI ID and LUN as long as the SCSI stack uses this type */
typedef u32	scsi_id_t;
typedef u32	scsi_lun_t;

typedef u16	devno_t;
typedef u16	irq_t;

typedef u64	wwn_t;
typedef u32	fc_id_t;
typedef u64	fcp_lun_t;

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


typedef struct _fc_ct_iu {
	u8	revision;	// 0x01
	u8	in_id[3];	// 0x00
	u8	gs_type;	// 0xFC	Directory Service
	u8	gs_subtype;	// 0x02	Name Server
	u8	options;	// 0x10 synchronous/single exchange
	u8	reserved0;
	u16	cmd_rsp_code;	// 0x0121 GID_PN
	u16	max_res_size;	// <= (4096 - 16) / 4
	u8	reserved1;
	u8	reason_code;
	u8	reason_code_expl;
	u8	vendor_unique;
	union {
		wwn_t	wwpn;
		fc_id_t	d_id;
	} data;
} __attribute__ ((packed)) fc_ct_iu_t;

#define ZFCP_CT_REVISION		0x01
#define ZFCP_CT_DIRECTORY_SERVICE	0xFC
#define ZFCP_CT_NAME_SERVER		0x02
#define ZFCP_CT_SYNCHRONOUS		0x00
#define ZFCP_CT_GID_PN			0x0121
#define ZFCP_CT_MAX_SIZE		0x1020
#define ZFCP_CT_ACCEPT			0x8002

struct _zfcp_fsf_req;
typedef void zfcp_send_generic_handler_t(struct _zfcp_fsf_req*);


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
} zfcp_mem_pool_t;

typedef struct _zfcp_adapter_mem_pool {
        zfcp_mem_pool_t status_read_fsf;
	zfcp_mem_pool_t status_read_buf;
        zfcp_mem_pool_t nameserver;
        zfcp_mem_pool_t erp_fsf;
        zfcp_mem_pool_t fcp_command_fsf;
        struct timer_list fcp_command_fsf_timer;
} zfcp_adapter_mem_pool_t;


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
	struct _zfcp_fsf_req *fsf_req;
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

typedef struct {
        struct _zfcp_port *port;
	char *outbuf;
	char *inbuf;
	int outbuf_length;
	int inbuf_length;
	zfcp_send_generic_handler_t *handler;
	unsigned long handler_data;
} zfcp_send_generic_t;

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
	zfcp_send_generic_t		send_generic;
	zfcp_status_read_t		status_read;
} zfcp_req_data_t;


typedef void zfcp_fsf_req_handler_t(struct _zfcp_fsf_req*);

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
	/* number of SBALs in list */
	u8				sbal_count;
	/* actual position of first SBAL in queue */
	u8				sbal_index;
	/* can be used by routine to wait for request completion */
	wait_queue_head_t		completion_wq;
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


#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
typedef struct _zfcp_lock_meter {
	unsigned long long		time;
	rwlock_t			lock;
} zfcp_lock_meter_t;
#endif


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
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	zfcp_lock_meter_t		lock_meter;
#endif
        /* outbound queue only, SBALs since PCI indication */
        int                             distance_from_int;
} zfcp_qdio_queue_t;

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
	/* SCSI host structure of the mid layer of the SCSI stack */
	struct Scsi_Host		*scsi_host;
        /* Start of packets in flight list */
        Scsi_Cmnd                       *first_fake_cmnd;
        /* lock for the above */
	rwlock_t			fake_list_lock;
        /* starts processing of faked commands */
	struct timer_list               fake_scsi_timer;
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
#endif // ZFCP_ERP_DEBUG_SINGLE_STEP
} zfcp_adapter_t;

#define ZFCP_PARSE_ERR_BUF_SIZE 100

/* driver data */
typedef struct _zfcp_data {
	/* SCSI stack data structure storing information about this driver */
	Scsi_Host_Template		scsi_host_template;
	/* head of adapter list */
        unsigned                        fake_scsi_reqs_active;
	struct list_head		adapter_list_head;
	/* lock for critical operations on list of adapters */
	rwlock_t			adapter_list_lock;
	/* number of adapters in list */
	u16				adapters;
	/* data used for dynamic I/O */
	devreg_t			devreg;
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
	/* Debug Feature */
	debug_info_t 			*spinlock_dbf;
        atomic_t                        mem_count;
#ifdef ZFCP_STAT_REQSIZES
	struct list_head		read_req_head;
	struct list_head		write_req_head;
	struct list_head		read_sg_head;
	struct list_head		write_sg_head;
	struct list_head		read_sguse_head;
	struct list_head		write_sguse_head;
	unsigned long			stat_errors;
	rwlock_t			stat_lock;
#endif
#ifdef ZFCP_STAT_QUEUES
        atomic_t                        outbound_queue_full;
	atomic_t			outbound_total;
#endif
	struct notifier_block		reboot_notifier;
	atomic_t			loglevel;
#ifdef ZFCP_LOW_MEM_CREDITS
	atomic_t			lowmem_credit;
#endif
} zfcp_data_t;


#ifdef ZFCP_STAT_REQSIZES
typedef struct _zfcp_statistics {
        struct list_head list;
        u32 num;
        u32 occurrence;
} zfcp_statistics_t;
#endif


/*********************** LIST DEFINES ****************************************/

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

#define ZFCP_FOR_EACH_ENTITY(head,curr,type) \
	for (curr = ZFCP_FIRST_ENTITY(head,type); \
	     curr; \
	     curr = ZFCP_NEXT_ENTITY(head,curr,type)) 

#define ZFCP_FOR_EACH_ADAPTER(a) \
	ZFCP_FOR_EACH_ENTITY(&zfcp_data.adapter_list_head,(a),zfcp_adapter_t)

#define ZFCP_FOR_EACH_PORT(a,p) \
	ZFCP_FOR_EACH_ENTITY(&(a)->port_list_head,(p),zfcp_port_t)

#define ZFCP_FOR_EACH_UNIT(p,u) \
	ZFCP_FOR_EACH_ENTITY(&(p)->unit_list_head,(u),zfcp_unit_t)

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
static int	zfcp_fsf_exchange_config_data (zfcp_erp_action_t*);
static int	zfcp_fsf_open_port (zfcp_erp_action_t*);
static int	zfcp_fsf_close_port (zfcp_erp_action_t*);
static int	zfcp_fsf_open_unit (zfcp_erp_action_t*);
static int	zfcp_fsf_close_unit (zfcp_erp_action_t*);
static int	zfcp_fsf_close_physical_port (zfcp_erp_action_t*);
static int	zfcp_fsf_send_fcp_command_task
			(zfcp_adapter_t*, zfcp_unit_t*, Scsi_Cmnd *, int);
static zfcp_fsf_req_t*	zfcp_fsf_send_fcp_command_task_management
			(zfcp_adapter_t*, zfcp_unit_t*, u8, int);
static zfcp_fsf_req_t*	zfcp_fsf_abort_fcp_command
			(unsigned long, zfcp_adapter_t*, zfcp_unit_t*, int);
static void zfcp_fsf_start_scsi_er_timer(zfcp_adapter_t*);
static void zfcp_fsf_scsi_er_timeout_handler(unsigned long);
static int	zfcp_fsf_send_generic
			(zfcp_fsf_req_t*, unsigned char, unsigned long*,
			 struct timer_list*);
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
static int	zfcp_fsf_send_generic_handler (zfcp_fsf_req_t*);
static int      zfcp_fsf_status_read_handler (zfcp_fsf_req_t*);
void		zfcp_fsf_incoming_els (zfcp_fsf_req_t *); 
static inline int
		zfcp_fsf_req_create_sbal_check
			(unsigned long*, zfcp_qdio_queue_t*, int);
static int	zfcp_fsf_req_create
			(zfcp_adapter_t*, u32, unsigned long*, int,
                         zfcp_fsf_req_t**);

static inline int
		zfcp_mem_pool_element_alloc (zfcp_mem_pool_t*, int);
static inline int
		zfcp_mem_pool_element_free (zfcp_mem_pool_t*, int);
static inline void*
		zfcp_mem_pool_element_get (zfcp_mem_pool_t*, int);
static inline int
		zfcp_mem_pool_element_put (zfcp_mem_pool_t*, int);

static inline int
		zfcp_mem_pool_create (zfcp_mem_pool_t*, int, int);
static inline int
		zfcp_mem_pool_destroy (zfcp_mem_pool_t*);
static inline void*
		zfcp_mem_pool_find (zfcp_mem_pool_t*);
static inline int
		zfcp_mem_pool_return (void*, zfcp_mem_pool_t*);



static void	zfcp_scsi_low_mem_buffer_timeout_handler
			(unsigned long);

static int	zfcp_fsf_req_free (zfcp_fsf_req_t*);
static zfcp_fsf_req_t*
		zfcp_fsf_req_get (int, zfcp_mem_pool_t*);
static zfcp_fsf_req_t*
		zfcp_fsf_req_alloc(zfcp_adapter_t*, u32, int);
static int	zfcp_fsf_req_send (zfcp_fsf_req_t*, struct timer_list*);
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
static int
		zfcp_nameserver_request (zfcp_erp_action_t*);
static void	zfcp_nameserver_request_handler (zfcp_fsf_req_t*);

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
static int	zfcp_config_parse_record_add (zfcp_config_record_t*);
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

static int 	zfcp_create_sbals_from_sg
			(zfcp_fsf_req_t*, Scsi_Cmnd*, char, int, int);
static inline int
		zfcp_create_sbales_from_segment
			(unsigned long, int, int*, int, int, int*, int*,
			 int, int, qdio_buffer_t**, char);
static inline int
		zfcp_create_sbale
			(unsigned long, int, int*, int, int, int*, int,
			 int, int*, qdio_buffer_t**, char);
static inline void
		zfcp_zero_sbals(qdio_buffer_t**, int, int);

static zfcp_unit_t*
		zfcp_unit_lookup (zfcp_adapter_t*, int, int, int);

/* prototypes for functions performing paranoia checks */
static int	zfcp_paranoia_fsf_reqs(zfcp_adapter_t*, zfcp_fsf_req_t*);

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
int zfcp_open_proc_map(struct inode*, struct file*);
int zfcp_close_proc_map(struct inode*, struct file*);
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
ssize_t zfcp_map_proc_read(struct file*, 
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
static int zfcp_dbf_register (void);
static void zfcp_dbf_unregister (void);
static int zfcp_dio_register (void); 

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

static int zfcp_erp_wait (zfcp_adapter_t*);


#ifdef ZFCP_STAT_REQSIZES
static int zfcp_statistics_init_all (void);
static int zfcp_statistics_clear_all (void);
static int zfcp_statistics_clear (struct list_head*);
static int zfcp_statistics_new (struct list_head*, u32);
static int zfcp_statistics_inc (struct list_head*, u32);
static int zfcp_statistics_print (struct list_head*, char*, char*, int, int);
#endif

#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
#if 0
static inline unsigned long long 
	zfcp_adjust_tod
		(unsigned long long);
#endif
static inline unsigned long long
	zfcp_lock_meter_init
		(zfcp_lock_meter_t*);
static inline unsigned long long
	zfcp_lock_meter_add
		(zfcp_lock_meter_t*,
		 unsigned long long);
static inline int
	zfcp_lock_meter_print_tod
		(char*);
static inline int
	zfcp_lock_meter_print_time
		(zfcp_lock_meter_t*,
		 char*);
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
     open: zfcp_open_proc_map,
     read: zfcp_map_proc_read,
     release: zfcp_close_proc_map,
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


/**************** memory management wrappers ************************/

#define ZFCP_KMALLOC(params...)		zfcp_kmalloc(params, __FUNCTION__)
#define ZFCP_KFREE(params...)		zfcp_kfree(params, __FUNCTION__)
#define ZFCP_GET_ZEROED_PAGE(params...)	zfcp_get_zeroed_page(params, __FUNCTION__)
#define ZFCP_FREE_PAGE(params...)	zfcp_free_page(params, __FUNCTION__)

inline void *zfcp_kmalloc(size_t size, int type, char *origin)
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
        // out:
        return ret;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


inline unsigned long zfcp_get_zeroed_page(int flags, char *origin) 
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
        //out:
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
inline void zfcp_kfree(void *addr, size_t size, char *origin) 
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


inline void zfcp_free_page(unsigned long addr, char *origin) 
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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->cmd_dbf_lock, flags);
	if (zfcp_fsf_req_is_scsi_cmnd(fsf_req)) {
		scsi_cmnd = fsf_req->data.send_fcp_command_task.scsi_cmnd;
		debug_text_event(adapter->cmd_dbf, level, "fsferror");
		debug_text_event(adapter->cmd_dbf, level, text);
		debug_event(adapter->cmd_dbf, level, &fsf_req, sizeof(unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no, sizeof(u32));
		debug_event(adapter->cmd_dbf, level, &scsi_cmnd, sizeof(unsigned long));
		for (i = 0; i < add_length; i += ZFCP_CMD_DBF_LENGTH)
			debug_event(
				adapter->cmd_dbf,
				level,
				(char*)add_data + i,
				min(ZFCP_CMD_DBF_LENGTH, add_length - i));
	}
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->cmd_dbf_lock, flags);
#endif
}


static inline void zfcp_cmd_dbf_event_scsi(
		const char *text,
		Scsi_Cmnd *scsi_cmnd)
{
#ifdef ZFCP_DEBUG_COMMANDS
	zfcp_adapter_t *adapter = (zfcp_adapter_t*) scsi_cmnd->host->hostdata[0];
	zfcp_req_data_t *req_data = (zfcp_req_data_t*) scsi_cmnd->host_scribble;
        zfcp_fsf_req_t *fsf_req = (req_data ? req_data->send_fcp_command_task.fsf_req : NULL);
	int level = ((host_byte(scsi_cmnd->result) != 0) ? 1 : 5);
	unsigned long flags;

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->cmd_dbf_lock, flags);	
	debug_text_event(adapter->cmd_dbf, level, "hostbyte");
	debug_text_event(adapter->cmd_dbf, level, text);
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd->result, sizeof(u32));
	debug_event(adapter->cmd_dbf, level, &scsi_cmnd, sizeof(unsigned long));
	if (fsf_req) {
		debug_event(adapter->cmd_dbf, level, &fsf_req, sizeof(unsigned long));
		debug_event(adapter->cmd_dbf, level, &fsf_req->seq_no, sizeof(u32));
	} else	{
		debug_text_event(adapter->cmd_dbf, level, "");
		debug_text_event(adapter->cmd_dbf, level, "");
	}
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->cmd_dbf_lock, flags);
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

	/* create S/390 debug feature entries */
	if (zfcp_dbf_register()) {
                ZFCP_LOG_INFO(
			"warning: Could not allocate memory for "
			"s390 debug-logging facility (debug feature), "
                        "continuing without.\n");
	}

#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_init_all();
#endif

        /* Initialise proc semaphores */
        sema_init(&zfcp_data.proc_sema,1);
	ZFCP_DOWN(&zfcp_data.proc_sema); /* config changes protected by proc_sema */

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

	ZFCP_UP(&zfcp_data.proc_sema); /* release procfs */

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
	zfcp_dbf_unregister();
#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
#endif

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
        ZFCP_LOG_TRACE("Before zfcp_config_cleanup\n");

	/* free all resources dynamically allocated */

	/* block proc access to config */
        ZFCP_DOWN(&zfcp_data.proc_sema);
	temp_ret=zfcp_config_cleanup();
        ZFCP_UP(&zfcp_data.proc_sema);

        if (temp_ret) {
                ZFCP_LOG_NORMAL("bug: Could not free all memory "
                               "(debug info %d)\n",
                               temp_ret);
	}

        zfcp_delete_root_proc();

	zfcp_dbf_unregister();

#ifdef ZFCP_CAUSE_ERRORS
        del_timer(&zfcp_force_error_timer);
#endif	

#ifdef ZFCP_STAT_REQSIZES
	zfcp_statistics_clear_all();
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
        ZFCP_DOWN(&zfcp_data.proc_sema);
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
                ZFCP_LOG_NORMAL("bug: The FSF device type could not "
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
 * function:    zfcp_dbf_register
 *
 * purpose:	registers the module-wide debug feature entries and sets
 *              their respective level of detail
 *
 * returns:     0       on success
 *              -ENOMEM on failure of at least one dbf
 */
static int zfcp_dbf_register()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

        zfcp_data.spinlock_dbf = debug_register("zfcp_lock", 4, 1, 8);
        if (zfcp_data.spinlock_dbf) { 
                debug_register_view(
			zfcp_data.spinlock_dbf,
			&debug_hex_ascii_view);
                debug_set_level(zfcp_data.spinlock_dbf, 4);
        } else	retval = -ENOMEM;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_dbf_unregister
 *
 * purpose:	Removes module-wide debug feature entries and frees their 
 *              memory
 *
 * returns:     (void)
 */
static void zfcp_dbf_unregister()
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER


	ZFCP_LOG_TRACE("enter\n");

        if (zfcp_data.spinlock_dbf)
		debug_unregister(zfcp_data.spinlock_dbf);

	ZFCP_LOG_TRACE("exit\n");

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
		ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
		ZFCP_FOR_NEXT_PORT (adapter, port, tmp_port) {
			ZFCP_WRITE_LOCK(&port->unit_list_lock);
			ZFCP_FOR_NEXT_UNIT (port, unit, tmp_unit){ 
				retval |= zfcp_unit_dequeue(unit);
                        }
			ZFCP_WRITE_UNLOCK(&port->unit_list_lock);
			retval |= zfcp_port_dequeue(port);
 		}
		ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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

	zfcp_mem_pool_destroy(&adapter->pool.status_read_fsf);
	zfcp_mem_pool_destroy(&adapter->pool.status_read_buf);
	zfcp_mem_pool_destroy(&adapter->pool.nameserver);
	zfcp_mem_pool_destroy(&adapter->pool.erp_fsf);
	zfcp_mem_pool_destroy(&adapter->pool.fcp_command_fsf);
        
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

	retval = zfcp_mem_pool_create(
			&adapter->pool.erp_fsf,
			1,
			ZFCP_QTCB_AND_REQ_SIZE);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: FCP command buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(
			&adapter->pool.nameserver,
			1,
			2 * sizeof(fc_ct_iu_t));
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Nameserver buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(
			&adapter->pool.status_read_fsf,
			ZFCP_STATUS_READS_RECOM,
			sizeof(zfcp_fsf_req_t));
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Status read request pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(
			&adapter->pool.status_read_buf,
			ZFCP_STATUS_READS_RECOM,
			sizeof(fsf_status_read_buffer_t));
	if (retval) {
		ZFCP_LOG_INFO(
			"error: Status read buffer pool allocation failed\n");
		goto out;
	}

	retval = zfcp_mem_pool_create(
			&adapter->pool.fcp_command_fsf,
			1,
			ZFCP_QTCB_AND_REQ_SIZE);
	if (retval) {
		ZFCP_LOG_INFO(
			"error: FCP command buffer pool allocation failed\n");
		goto out;
	}
	init_timer(&adapter->pool.fcp_command_fsf_timer);
	adapter->pool.fcp_command_fsf_timer.function = 
		zfcp_scsi_low_mem_buffer_timeout_handler;
	adapter->pool.fcp_command_fsf_timer.data = 
		(unsigned long)adapter;

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

	ZFCP_READ_LOCK_IRQSAVE(&zfcp_data.adapter_list_lock, flags);
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
        ZFCP_READ_UNLOCK_IRQRESTORE(&zfcp_data.adapter_list_lock, flags);
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
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	zfcp_lock_meter_init(&adapter->request_queue.lock_meter);
#endif

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

        /* put allocated adapter at list tail */
	ZFCP_WRITE_LOCK_IRQSAVE(&zfcp_data.adapter_list_lock, flags);
        list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	zfcp_data.adapters++;
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&zfcp_data.adapter_list_lock, flags);

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
	ZFCP_READ_LOCK_IRQSAVE(&adapter->fsf_req_list_lock, flags);

	retval = !list_empty(&adapter->fsf_req_list_head);

	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->fsf_req_list_lock, flags);
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

	/* paranoia checks */
	ZFCP_PARANOIA{
		zfcp_adapter_t *tmp;
		/* consistency of adapter list in general */
		/* is specified adapter in list ? */
		ZFCP_FOR_EACH_ADAPTER(tmp)
			if (tmp == adapter)
				break;
		if (tmp != adapter) {
			/* inconsistency */
			ZFCP_LOG_NORMAL(
				"bug: Adapter struct with devno 0x%04x not "
                                "in global adapter struct list "
                                "(debug info 0x%lx)\n",
                                adapter->devno,
				(unsigned long)adapter);
			retval = -EINVAL;
			goto out;
		}
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

	/* sanity check: valid adapter data structure address */
	ZFCP_PARANOIA {
		if (!adapter) {
			ZFCP_LOG_NORMAL(
				"bug: Pointer to the adapter struct is a null "
                        	"pointer\n");
	                retval = -EINVAL;
			goto paranoia_failed;
		}
	}

        /* to check that there is not a port with either this 
	 * SCSI ID or WWPN already in list
	 */
	check_scsi_id = !(status & ZFCP_STATUS_PORT_NO_SCSI_ID);
	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);

	if (check_scsi_id && check_wwpn) {
		ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
		ZFCP_FOR_EACH_PORT(adapter, port) {
			if ((port->scsi_id != scsi_id) && (port->wwpn != wwpn))
				continue;
			if ((port->scsi_id == scsi_id) && (port->wwpn == wwpn)) {
				ZFCP_LOG_TRACE(
					"Port with SCSI ID 0x%x and WWPN 0x%016Lx already in list\n",
					scsi_id, (llui_t)wwpn);
				retval = ZFCP_KNOWN;
				ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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
			ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
			goto match_failed;
		}
		ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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
        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock,flags);
        list_add_tail(&port->list, &adapter->port_list_head);
	adapter->ports++;
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock,flags);
        
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
paranoia_failed:
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

	/* paranoia check: valid "parent" adapter (simple check) */
	ZFCP_PARANOIA {
		if (!port->adapter) {
			ZFCP_LOG_NORMAL(
				"bug: Port struct for port with SCSI-id 0x%x "
                                "contains an invalid pointer to its adapter "
                                "struct(debug info 0x%lx) \n",
				port->scsi_id,
				(unsigned long)port);
			retval = -EINVAL;
			goto out;
		}
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

	/* paranoia checks */
	ZFCP_PARANOIA {
		zfcp_port_t *tmp;
		/* is specified port in list ? */
		ZFCP_FOR_EACH_PORT(port->adapter, tmp)
			if (tmp == port)
				break;
		if (tmp != port) {
			/* inconsistency */
			ZFCP_LOG_NORMAL(
                                "bug: Port struct with SCSI-id 0x%x not "
                                "in port struct list "
                                "(debug info 0x%lx, 0x%lx)\n",
                                port->scsi_id,
				(unsigned long)port,
				(unsigned long)port->adapter);

			retval = -EINVAL;
			goto out;
		}
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
	void *buffer = pool->element[index].buffer;

	ZFCP_LOG_TRACE(
		"enter (pool=0x%lx, index=%i)\n",
		(unsigned long)pool,
		index);

	ZFCP_LOG_DEBUG("buffer=0x%lx\n", (unsigned long)buffer);
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)pool->element,
		pool->entries * sizeof(zfcp_mem_pool_element_t));

	if (atomic_compare_and_swap(0, 1, &pool->element[index].use))
		buffer = NULL;
	else	memset(pool->element[index].buffer, 0, pool->size);

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
static inline int zfcp_mem_pool_create(
		zfcp_mem_pool_t *pool, int entries, int size)
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

	pool->element = ZFCP_KMALLOC(entries * sizeof(zfcp_mem_pool_element_t), GFP_KERNEL);
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

	ZFCP_LOG_TRACE("exit (0x%lx)\n", (unsigned long)buffer);

	return buffer;
}


/*
 * make buffer available to memory pool again,
 * (since buffers are specified by their own address instead of the
 * memory pool element they are associated with a search for the
 * right element of the given memory pool)
 */ 
static inline int zfcp_mem_pool_return(
		void *buffer, zfcp_mem_pool_t *pool)
{
	int retval = 0;
	int i;

	ZFCP_LOG_TRACE(
		"enter (buffer=0x%lx, pool=0x%lx)\n",
		(unsigned long)buffer,
		(unsigned long)pool);

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
 * try to allocate fsf_req with QTCB,
 * alternately try to get hold of fsf_req+QTCB provided by the specified memory pool element,
 * this routine is called for all kinds of fsf requests other than status read
 * since status read does neither require kmalloc involvement nor a QTCB
 */
static zfcp_fsf_req_t *zfcp_fsf_req_get(
		int kmalloc_flags,
		zfcp_mem_pool_t *pool)
{
	zfcp_fsf_req_t *fsf_req;

#ifdef ZFCP_MEM_POOL_ONLY
	fsf_req = NULL;
#else
	fsf_req = ZFCP_KMALLOC(ZFCP_QTCB_AND_REQ_SIZE, kmalloc_flags);
#endif
	if (!fsf_req) {
		fsf_req = zfcp_mem_pool_find(pool);
		if (fsf_req)
			fsf_req->status |= ZFCP_STATUS_FSFREQ_POOL;
	}
	if (fsf_req)
		fsf_req->qtcb = (fsf_qtcb_t*) ((unsigned long)fsf_req + sizeof(zfcp_fsf_req_t));

	return fsf_req;
}


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
static zfcp_fsf_req_t *zfcp_fsf_req_alloc(
		     zfcp_adapter_t *adapter,
                     u32 fsf_cmd,
                     int kmalloc_flags)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF
        
        zfcp_fsf_req_t *fsf_req = NULL;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx, fsf_command 0x%x, "
                       "kmalloc_flags 0x%x)\n", 
                       (unsigned long)adapter,
                       fsf_cmd,
                       kmalloc_flags);

        switch(fsf_cmd) {

        case FSF_QTCB_FCP_CMND :
        case FSF_QTCB_ABORT_FCP_CMND :
		fsf_req = zfcp_fsf_req_get(
				kmalloc_flags,
				&adapter->pool.fcp_command_fsf);
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL)) {
			/*
			 * watch low mem buffer
			 * Note: If the command is reset or aborted, two timeouts
			 * (this and the SCSI ER one) will be started for 
			 * the command. There is no problem however as
			 * the first expired timer will call adapter_reopen
			 * which will delete the other 
			 */
			adapter->pool.fcp_command_fsf_timer.expires = 
				jiffies + ZFCP_SCSI_LOW_MEM_TIMEOUT;
			add_timer(&adapter->pool.fcp_command_fsf_timer);
		}
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_fcp");
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL))
			debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif /* ZFCP_DEBUG_REQUESTS */
		break;

        case FSF_QTCB_OPEN_PORT_WITH_DID :
        case FSF_QTCB_OPEN_LUN :
        case FSF_QTCB_CLOSE_LUN :
        case FSF_QTCB_CLOSE_PORT :
        case FSF_QTCB_CLOSE_PHYSICAL_PORT :
        case FSF_QTCB_SEND_ELS :
        case FSF_QTCB_EXCHANGE_CONFIG_DATA :
        case FSF_QTCB_SEND_GENERIC :
		fsf_req = zfcp_fsf_req_get(kmalloc_flags, &adapter->pool.erp_fsf);
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_erp");
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL))
			debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif /* ZFCP_DEBUG_REQUESTS */
                break;

	case FSF_QTCB_UNSOLICITED_STATUS :
		fsf_req = zfcp_mem_pool_find(&adapter->pool.status_read_fsf);
		if (fsf_req)
			fsf_req->status |= ZFCP_STATUS_FSFREQ_POOL;
		else	ZFCP_LOG_NORMAL("bug: could not find free fsf_req\n");
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_sr");
		debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif /* ZFCP_DEBUG_REQUESTS */
		break;

        default :
                ZFCP_LOG_NORMAL("bug: An attempt to send an unsupported "
                                "command has been detected. "
                                "(debug info 0x%x)\n",
                                fsf_cmd);
        } //switch(fsf_cmd)

	if (!fsf_req) {
		ZFCP_LOG_DEBUG("error: Out of memory. Allocation of FSF "
                              "request structure failed\n");
	} else	{
    		ZFCP_LOG_TRACE(
			"FSF request allocated at 0x%lx, "
			"adapter 0x%lx (0x%x)\n",
			(unsigned long)fsf_req,
			(unsigned long)adapter,
			adapter->devno);
	}

#ifdef ZFCP_DEBUG_REQUESTS
	debug_event(adapter->req_dbf, 5, &fsf_req, sizeof(unsigned long));
	if (fsf_req->qtcb)
		debug_event(adapter->req_dbf, 5, &fsf_req->qtcb, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

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
inline int zfcp_fsf_req_free(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;
	zfcp_adapter_t *adapter = fsf_req->adapter;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

        switch (fsf_req->fsf_command) {

        case FSF_QTCB_FCP_CMND :
        case FSF_QTCB_ABORT_FCP_CMND :
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL) {
			del_timer(&adapter->pool.fcp_command_fsf_timer);
			retval = zfcp_mem_pool_return(fsf_req, &adapter->pool.fcp_command_fsf);
		} else	ZFCP_KFREE(fsf_req, ZFCP_QTCB_AND_REQ_SIZE);
                break;

        case FSF_QTCB_OPEN_PORT_WITH_DID :
        case FSF_QTCB_OPEN_LUN :
        case FSF_QTCB_CLOSE_LUN :
        case FSF_QTCB_CLOSE_PORT :
        case FSF_QTCB_CLOSE_PHYSICAL_PORT :
        case FSF_QTCB_SEND_ELS :
        case FSF_QTCB_EXCHANGE_CONFIG_DATA :
        case FSF_QTCB_SEND_GENERIC :
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL)
			retval = zfcp_mem_pool_return(fsf_req, &adapter->pool.erp_fsf);
		else	ZFCP_KFREE(fsf_req, ZFCP_QTCB_AND_REQ_SIZE);
                break;

	case FSF_QTCB_UNSOLICITED_STATUS :
		retval = zfcp_mem_pool_return(fsf_req, &adapter->pool.status_read_fsf);
		break;
 	}

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_paranoia_fsf_reqs
 *
 * purpose:	check sanity of FSF request list of specified adapter
 *
 * returns:	0 - no error condition
 *		!0 - error condition
 *
 * locks:	adapter->fsf_req_list_lock of the associated adapter is
 *		assumed to be held by the calling routine
 */
static int zfcp_paranoia_fsf_reqs(zfcp_adapter_t *adapter, zfcp_fsf_req_t *req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

	zfcp_fsf_req_t *current_req = NULL;
	int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx)\n", (unsigned long)adapter);

	/*
	 * sanity check:
	 * leave if required list lock is not held,
	 * do not know whether it is held by the calling routine (required!)
	 * protecting this critical area or someone else (must not occur!),
	 * but a lock not held by anyone is definetely wrong
	 */
	if (!spin_is_locked(&adapter->fsf_req_list_lock)) {
		ZFCP_LOG_NORMAL(
			"bug: FSF request list lock not held\n");
		retval = -EPERM;
		goto out;
	}

	/* go through list, check magics and count fsf_reqs */
	ZFCP_FOR_EACH_FSFREQ(adapter, current_req) {
		if (current_req == req)
			goto out;
	}
	ZFCP_LOG_NORMAL(
		"fsf_req 0x%lx not found in list of adapter with devno 0x%04x\n",
		(unsigned long)req,
		adapter->devno);
	retval = -EINVAL;

out:
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

	/* sanity check: valid port data structure address */
	ZFCP_PARANOIA {
		if (!port) {
			ZFCP_LOG_NORMAL(
				"bug: Pointer to a port struct is a null "
                        	"pointer\n");
	                retval = -EINVAL;
			goto paranoia_failed;
		}
	}

	/*
	 * check that there is no unit with either this
	 * SCSI LUN or FCP_LUN already in list
         * Note: Unlike for the adapter and the port, this is an error
	 */
	ZFCP_READ_LOCK_IRQSAVE(&port->unit_list_lock, flags);
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
        ZFCP_READ_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);
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
        ZFCP_WRITE_LOCK_IRQSAVE(&port->unit_list_lock, flags);
        list_add_tail(&unit->list, &port->unit_list_head);
	port->units++;
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);
        
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
paranoia_failed:
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

	/* paranoia check: valid "parent" port (simple check) */
	ZFCP_PARANOIA {
		if (!unit->port) {
			ZFCP_LOG_NORMAL(
				"bug: Unit struct for unit with SCSI LUN 0x%x "
                                "contains an invalid pointer to its adapter "
                                "struct(debug info 0x%lx, 0x%lx) \n",
				unit->scsi_lun,
				(unsigned long)unit,
				(unsigned long)unit->port);
			retval = -EINVAL;
			goto out;
		}
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

	/* paranoia checks */
	ZFCP_PARANOIA {
		zfcp_unit_t *tmp;
		/* is specified unit in list ? */
		ZFCP_FOR_EACH_UNIT(unit->port, tmp)
			if (tmp == unit)
				break;
		if (tmp != unit) {
			/* inconsistency */
			ZFCP_LOG_NORMAL(
                                "bug: Unit struct with SCSI LUN 0x%x not "
                                "in unit struct list "
                                "(debug info 0x%lx, 0x%lx)\n",
                                unit->scsi_lun,
				(unsigned long)unit,
				(unsigned long)unit->port);
			retval = -EINVAL;
			goto out;
		}
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
                       "Registered Adapters:       %5d     "
                       "Fake SCSI reqs active      %10d\n\n",
                       zfcp_data.adapters,
                       zfcp_data.fake_scsi_reqs_active);
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
	}// if (proc_debug != 0)

#ifdef ZFCP_STAT_QUEUES
        len += sprintf(pbuf->buf + len, "Outbound queue full:  0x%08x\n",
                       atomic_read(&zfcp_data.outbound_queue_full));
	len += sprintf(pbuf->buf + len, "Outbound requests:    0x%08x\n",
			atomic_read(&zfcp_data.outbound_total));
#endif
#ifdef ZFCP_STAT_REQSIZES
	len += sprintf(pbuf->buf+len, "missed stats 0x%lx\n", zfcp_data.stat_errors);
	len = zfcp_statistics_print(&zfcp_data.read_req_head, "rr", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(&zfcp_data.write_req_head, "wr", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(&zfcp_data.read_sg_head, "re", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(&zfcp_data.write_sg_head, "we", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(&zfcp_data.read_sguse_head, "rn", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
	len = zfcp_statistics_print(&zfcp_data.write_sguse_head, "wn", pbuf->buf, len, ZFCP_MAX_PROC_SIZE);
#endif

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
int zfcp_open_proc_map(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;

        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) buffer);

        MOD_INC_USE_COUNT;

	/* block access */
	ZFCP_DOWN(&zfcp_data.proc_sema);

        ZFCP_LOG_TRACE(
		"Trying to allocate memory for "
		"zfcp_data.proc_buffer_map.\n");
        zfcp_data.proc_buffer_map = ZFCP_KMALLOC(ZFCP_MAX_PROC_SIZE, GFP_KERNEL);
        if (zfcp_data.proc_buffer_map == NULL) {
                ZFCP_LOG_NORMAL(
			"error: Not enough free memory for procfile"
			" output. No output will be given .\n");
                retval = -ENOMEM;
                goto out;
        } 
         
        ZFCP_LOG_TRACE(
		"Memory for zfcp_data.proc_buffer_map "
		"allocated.\n");
 out:        
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
int zfcp_close_proc_map(struct inode *inode, struct file *buffer)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval=0;
        ZFCP_LOG_TRACE("enter (inode=0x%lx, buffer=0x%lx)\n",
                       (unsigned long)inode,
                       (unsigned long) buffer);

        ZFCP_LOG_TRACE("Freeing zfcp_data.proc_buffer_map.\n");
        ZFCP_KFREE(zfcp_data.proc_buffer_map, ZFCP_MAX_PROC_SIZE);

	/* release proc_sema */
	ZFCP_UP(&zfcp_data.proc_sema);

        MOD_DEC_USE_COUNT;

        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

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

        MOD_INC_USE_COUNT;

	ZFCP_DOWN(&zfcp_data.proc_sema);

	zfcp_data.proc_line = ZFCP_KMALLOC(ZFCP_MAX_PROC_LINE, GFP_KERNEL);
        if (!zfcp_data.proc_line){
                ZFCP_LOG_NORMAL("error: Not enough free memory for procfile"
                               " input. Input will be ignored.\n");
                retval=-ENOMEM;

		/* release semaphore on memory shortage */
		ZFCP_UP(&zfcp_data.proc_sema);
                goto out;
        }
        else {
                ZFCP_LOG_TRACE("proc_line buffer allocated...\n");
        }
        /* This holds the length of the part acutally containing data, not the
           size of the buffer */
        zfcp_data.proc_line_length=0;
        
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
	ZFCP_UP(&zfcp_data.proc_sema);

        MOD_DEC_USE_COUNT;

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

	if (pos < 0 || pos >= pbuf->len) {
		return 0;
	} else {
		len = min(user_len, (unsigned long)(pbuf->len - pos));
		if (copy_to_user( user_buf, &(pbuf->buf[pos]), len))
			return -EFAULT;
		*offset = pos + len;
		return len;
	}

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n ",
                       ((unsigned long)(pbuf->len  - pos)),
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
 * function:   zfcp_next_unit
 *
 * purpose:    obtains the next unit from the unit list
 *
 * retval:     0  if next unit was found
 *             -1 if end of list was reached
 *
 * locks:      proc_sema must be held on entry and throughout function
 */
inline int  zfcp_next_unit(struct list_head **unit_head, zfcp_unit_t **unit, zfcp_port_t **port)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval = 0;
        
        ZFCP_LOG_TRACE(
             "enter (*unit_head=0x%lx  *unit=0x%lx "
             "*port=0x%lx)\n",
             (unsigned long)*unit_head,
             (unsigned long)*unit,
             (unsigned long)*port);
        
        
        *unit_head = (*unit_head)->next;        
        if (*unit_head == &((*port)->unit_list_head)) { 
                retval = -1;
        } else {
                *unit = list_entry(*unit_head, zfcp_unit_t, list);
        }
     
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;
        
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_next_port
 *
 * purpose:    obtains the next port from the port list
 *
 * retval:     0  if next port was found
 *             -1 if end of list was reached
 *
 * locks:      proc_sema must be held on entry and throughout function
 */
inline int  zfcp_next_port(struct list_head **port_head, 
                           struct list_head **unit_head,
                           zfcp_port_t **port, 
                           zfcp_adapter_t **adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval = 0;

     ZFCP_LOG_TRACE(
          "enter (*port_head=0x%lx  *port=0x%lx "
          "*adapter=0x%lx)\n",
          (unsigned long)*port_head,
          (unsigned long)*port,
          (unsigned long)*adapter);

        *port_head = (*port_head)->next;        
        if (*port_head == &((*adapter)->port_list_head)) { 
                retval = -1;
        } else {
                *port = list_entry(*port_head, zfcp_port_t, list);
                /* point strait at head as the while loop will use the 
                 * next pointer 
                 */
                *unit_head=&((*port)->unit_list_head);
        }

        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_next_adapter
 *
 * purpose:    obtains the next adapter from the adapter list
 *
 * retval:     0  if next adapter was found
 *             -1 if end of list was reached
 *
 * locks:      proc_sema must be held on entry and throughout function
 */
inline int  zfcp_next_adapter(struct list_head **adapter_head, 
                              struct list_head **port_head,
                              zfcp_adapter_t **adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

        int retval = 0;

        ZFCP_LOG_TRACE(
           "enter (*adapter_head=0x%lx  *adapter=0x%lx)\n",
           (unsigned long)*adapter_head,
           (unsigned long)*adapter);
        
        *adapter_head = (*adapter_head)->next;        
        if (*adapter_head == &(zfcp_data.adapter_list_head)) {
                retval = -1;
        } else {
                *adapter = list_entry(*adapter_head, zfcp_adapter_t, list);
                /* point strait at head as the while loop will use the 
                 * next pointer 
                 */
                *port_head=&((*adapter)->port_list_head);
        }
        
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_get_next_unit
 *
 * purpose:    obtains the next unit from the conglumeration of all adapter, port 
 *             and unit lists throughout the module
 *
 * retval:     0  if next unit was found
 *             -1 if end of all lists was reached
 *
 * locks:      proc_sema must be held on entry and throughout function
 */
int zfcp_get_next_unit (zfcp_adapter_t **adapter,
                        zfcp_port_t **port,
                        zfcp_unit_t **unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
        
        int retval=0;
        struct list_head *adapter_head = &((*adapter)->list);
        struct list_head *port_head = &((*port)->list);
        struct list_head *unit_head = &((*unit)->list);

     ZFCP_LOG_TRACE(
          "enter (*adapter=0x%lx  *port=0x%lx "
          "*unit=0x%lx)\n",
          (unsigned long)*adapter,
          (unsigned long)*port,
          (unsigned long)*unit);

        ZFCP_LOG_TRACE("&port->unit_list_head=0x%lx\n",
                       (unsigned long)&((*port)->unit_list_head));
        ZFCP_LOG_TRACE("unit_head=0x%lx\n",
                       (unsigned long)unit_head);
        ZFCP_LOG_TRACE("*unit=0x%lx\n",
                       (unsigned long)*unit);

        
        while(zfcp_next_unit(&unit_head,unit,port) != 0) {
                while(zfcp_next_port(&port_head,&unit_head,port,adapter) != 0) {
                        if(zfcp_next_adapter(&adapter_head,&port_head,adapter) != 0) {
                                /* Last adapter, i.e. end of list reached */
                                retval=-1;
                                goto out;
                        }
                }
                ZFCP_LOG_TRACE("*unit=0x%lx\n",
                               (unsigned long)*unit);
                ZFCP_LOG_TRACE("*port=0x%lx\n",
                               (unsigned long)*port);
                ZFCP_LOG_TRACE("*adapter=0x%lx \n",
                               (unsigned long)*adapter);
        }

 out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:   zfcp_get_unit
 *
 * purpose:    obtains the unit at position desired_unit from the 
 *             conglumeration of all adapter, port and unit lists throughout 
 *             the module
 *
 * retval:     0  if the required unit was found
 *             -1 if end of all lists was reached trying to find unit
 *
 * locks:      proc_sema must be held on entry and throughout function
 */
int zfcp_get_unit (zfcp_adapter_t **adapter,
                   zfcp_port_t **port,
                   zfcp_unit_t **unit,
                   u64 desired_unit)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
     
        int retval=0;
        u64 i;
        struct list_head *adapter_head;
        struct list_head *port_head;
        struct list_head *unit_head;

     ZFCP_LOG_TRACE(
          "enter (*adapter=0x%lx  *port=0x%lx "
          "*unit=0x%lx, desired_unit=%Ld)\n",
          (unsigned long)*adapter,
          (unsigned long)*port,
          (unsigned long)*unit,
          (llui_t)desired_unit);


        if(list_empty(&(zfcp_data.adapter_list_head))) {
                /* sod-all to put out */
                retval = -ENXIO;
                goto out;
        }
        /* Setting up lists */
        adapter_head = zfcp_data.adapter_list_head.next;
        ZFCP_LOG_TRACE("adapter_head=0x%lx,\n",
                       (unsigned long)adapter_head);
        *adapter = list_entry(adapter_head, zfcp_adapter_t, list);
        ZFCP_LOG_TRACE("*adapter=0x%lx \n",
                       (unsigned long)*adapter);
        port_head = (*adapter)->port_list_head.next;
        ZFCP_LOG_TRACE("port_head=0x%lx\n",
                       (unsigned long)port_head);
        *port = list_entry(port_head, zfcp_port_t, list);
        ZFCP_LOG_TRACE("*port=0x%lx\n",
                       (unsigned long)*port);
        unit_head = (*port)->unit_list_head.next;
        ZFCP_LOG_TRACE("unit_head=0x%lx\n",
                       (unsigned long)unit_head);
        *unit = list_entry(unit_head, zfcp_unit_t, list);
        ZFCP_LOG_TRACE("*unit=0x%lx\n",
                       (unsigned long)*unit);

        if (desired_unit==0) {
                ZFCP_LOG_TRACE("Initial unit\n");
                goto out;
        }

        for (i=0; i<desired_unit; i++) {
                retval=zfcp_get_next_unit(adapter, port, unit);
                if(retval<0) {
                        ZFCP_LOG_TRACE("Desired unit %Ld not found, "
                                       "current index %Ld\n",
                                       (llui_t)desired_unit,
                                       (llui_t)i);
                        goto out;
                }
                ZFCP_LOG_TRACE("i=%Ld, desired_unit=%Ld\n", 
                               (llui_t)i, 
                               (llui_t)desired_unit);
        }

 out:
        ZFCP_LOG_TRACE("exit (%i)\n", retval);
        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_map_proc_read
 *
 * purpose:	Provides a list of all configured devices in identical format
 *		to expected configuration input as proc-output
 *
 * returns:     number of characters copied to user-space
 *              - <error-type> otherwise
 *
 * locks:       proc_sema must be held on entry and throughout function
 */
ssize_t zfcp_map_proc_read(struct file *file, 
                             char *user_buf, 
                             size_t user_len, 
                             loff_t *offset)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
     
     size_t size = 0;
     loff_t line_offset;
     u64 current_unit;
     zfcp_adapter_t *adapter=NULL;
     zfcp_port_t *port=NULL;
     zfcp_unit_t *unit=NULL;
     loff_t pos = *offset;
     
     /* Is used when we don't have a 0 offset, which cannot happen
      * during the first call
      */
     static size_t item_size = 0;

     ZFCP_LOG_TRACE(
          "enter (file=0x%lx  user_buf=0x%lx "
          "user_length=%li, *offset=%Ld)\n",
          (unsigned long)file,
          (unsigned long)user_buf,
          user_len,
          pos);

     /* Do not overwrite proc-buffer */
     user_len = min(user_len, ZFCP_MAX_PROC_SIZE);
     size=0;
     if(pos==0) {
             current_unit=0;
             line_offset=0;
     } else {
		current_unit = pos;
		line_offset = do_div(current_unit, item_size);
             ZFCP_LOG_TRACE("item_size %ld, current_unit %Ld, line_offset %Ld\n",
                            item_size,
                            (llui_t)current_unit,
                            line_offset);
     }
     if (zfcp_get_unit(&adapter, &port, &unit, current_unit) < 0) {
             ZFCP_LOG_TRACE("End of proc output reached.\n");
             user_len=0;
             goto out;
     }

     while (1) {
             item_size=sprintf(&zfcp_data.proc_buffer_map[size],
                               "0x%04x "
                               "0x%08x:0x%016Lx "
                               "0x%08x:0x%016Lx\n",
                               adapter->devno,
                               port->scsi_id,
                               (llui_t)(port->wwpn),			       	
                               unit->scsi_lun,
			       (llui_t)(unit->fcp_lun));
             if ((size-line_offset) + item_size > user_len) {
                     /* Note: line_offset is always subtracted later on,
                        hence the addition 
                     */
                     size = user_len+line_offset;
                     break;
             }  else  size += item_size;
             current_unit++;
             if (zfcp_get_next_unit(&adapter, &port, &unit) < 0) {
             ZFCP_LOG_TRACE("No unit found at current_unit=%Ld\n",
                            (llui_t)current_unit);
             break;
             }
     } // while 1
     
     user_len = size-line_offset;

     ZFCP_LOG_TRACE("Trying to do output (my_offset=%Ld, size=%ld, user_len=%ld).\n",
                    line_offset,
                    size,
                    user_len);

     if (copy_to_user(user_buf, &zfcp_data.proc_buffer_map[line_offset], 
                      user_len)){
             user_len=-EFAULT;
             ZFCP_LOG_NORMAL("bug: Copying proc-file output to user space "
                            "failed (debug info %ld)",
                            user_len);
     }

 out:        
     *offset = pos + user_len;

     ZFCP_LOG_TRACE("exit (%li)\n", user_len);

     return user_len;
     
     
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
                       "*fragment=0x%lx, *fragment_length=%ld)\n ",
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
                       "*rest=0x%lx, *rest_length=%ld)\n ",
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
                                ZFCP_QTCB_SIZE);
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
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
		len += sprintf(pbuf->buf+len,
					"current TOD:          ");
		len += zfcp_lock_meter_print_tod(
					pbuf->buf+len);
		len += sprintf(pbuf->buf+len,"\n");
		len += sprintf(pbuf->buf+len,
					"time lock held:       ");
		len += zfcp_lock_meter_print_time(
					&adapter->request_queue.lock_meter,
					pbuf->buf+len);
		len += sprintf(pbuf->buf+len,"\n");
#endif
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

        if ( pos != (unsigned long)pos || pos >= pbuf->len) {
                return 0;
        } else {
                len = min(user_len, (unsigned long)(pbuf->len - pos));
                if (copy_to_user( user_buf, &(pbuf->buf[pos]), len))
                        return -EFAULT;
                *offset = pos + len;
                return len;
        }

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n ",
                       ((unsigned long)(pbuf->len  - *offset)),
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

	if (strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) {
                ZFCP_LOG_INFO(ZFCP_RESET_ERP " command received...\n");
                zfcp_erp_modify_adapter_status(
                                adapter,
                                ZFCP_STATUS_COMMON_RUNNING,
                                ZFCP_SET);
                zfcp_erp_adapter_reopen(
                                adapter,
                                ZFCP_STATUS_COMMON_ERP_FAILED);
                user_len = strlen(buffer);
        } else  {
                ZFCP_LOG_TRACE("unknown procfs command\n");
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

        if (pos != (unsigned long)pos || pos >= pbuf->len) {
                return 0;
        } else {
                len = min(user_len, (unsigned long)(pbuf->len - pos));
                if (copy_to_user( user_buf, &(pbuf->buf[pos]), len))
                        return -EFAULT;
                *offset = pos + len;
                return len;
        }

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n ",
                       ((unsigned long)(pbuf->len  - *offset)),
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

        if (strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) {
                ZFCP_LOG_INFO(ZFCP_RESET_ERP " command received...\n");
                zfcp_erp_modify_port_status(
                                port,
                                ZFCP_STATUS_COMMON_RUNNING,
                                ZFCP_SET);
                zfcp_erp_port_reopen(
                                port,
                                ZFCP_STATUS_COMMON_ERP_FAILED);
                user_len = strlen(buffer);
        } else  {
                ZFCP_LOG_TRACE("unknown procfs command\n");
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

        if (pos < 0 || pos >= pbuf->len) {
                return 0;
        } else {
                len = min(user_len, (unsigned long)(pbuf->len - pos));
                if (copy_to_user( user_buf, &(pbuf->buf[pos]), len))
                        return -EFAULT;
                *offset = pos + len;
                return len;
        }

        ZFCP_LOG_TRACE("Size-offset is %ld, user_len is %ld\n ",
                       ((unsigned long)(pbuf->len  - pos)),
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

        if (strncmp(ZFCP_RESET_ERP, buffer, strlen(ZFCP_RESET_ERP)) == 0) {
                ZFCP_LOG_INFO(ZFCP_RESET_ERP " command received...\n");
                zfcp_erp_modify_unit_status(
                                unit,
                                ZFCP_STATUS_COMMON_RUNNING,
                                ZFCP_SET);
                zfcp_erp_unit_reopen(unit,
                                     ZFCP_STATUS_COMMON_ERP_FAILED);
                user_len = strlen(buffer);
        } else  {
                ZFCP_LOG_TRACE("unknown procfs command\n");
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
		"enter (shtpnt =0x%lx)\n ",
		(unsigned long) shtpnt);

	spin_unlock_irq(&io_request_lock);
	adapters = zfcp_adapter_scsi_register_all();
	spin_lock_irq(&io_request_lock);

	ZFCP_LOG_TRACE(
		"exit (adapters =%d)\n ",
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

	ZFCP_READ_LOCK_IRQSAVE(&zfcp_data.adapter_list_lock, flags);
	adapter = ZFCP_FIRST_ADAPTER;
	while (adapter) {
		ZFCP_READ_UNLOCK_IRQRESTORE(&zfcp_data.adapter_list_lock, flags);
		if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status)) {
			ZFCP_LOG_DEBUG(
				"adapter with devno 0x%04x needs "
				"to be registered with SCSI stack, "
				"waiting for erp to settle\n",
				adapter->devno);
                        debug_text_event(zfcp_data.spinlock_dbf,6,"wait"); 
                        debug_event(zfcp_data.spinlock_dbf,6,&adapter->devno,sizeof(u32)); 
			zfcp_erp_wait(adapter);
                        debug_text_event(zfcp_data.spinlock_dbf,6,"ewait"); 
                        debug_event(zfcp_data.spinlock_dbf,6,&adapter->devno,sizeof(u32)); 
			if (zfcp_adapter_scsi_register(adapter) == 0);
				retval++;
		}else {
                        debug_text_event(zfcp_data.spinlock_dbf,6,"nowait"); 
                        debug_event(zfcp_data.spinlock_dbf,6,&adapter->devno,sizeof(u32)); 
                }
		ZFCP_READ_LOCK_IRQSAVE(&zfcp_data.adapter_list_lock, flags);
		adapter = ZFCP_NEXT_ADAPTER(adapter);
	}
	ZFCP_READ_UNLOCK_IRQRESTORE(&zfcp_data.adapter_list_lock, flags);

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
        
        ZFCP_LOG_TRACE("enter (host =0x%lx, dev_list=0x%lx)\n ",
                       (unsigned long) host,
                       (unsigned long) dev_list);
        
        ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
        ZFCP_FOR_EACH_PORT(adapter, port) {
                ZFCP_READ_LOCK(&port->unit_list_lock);
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
                        } else {
                                ZFCP_LOG_DEBUG("Disabling tagging for "
                                               "unit 0x%lx \n",
                                               (unsigned long) unit);
                                unit->device->tagged_queue = 0;
                                unit->device->current_tag = 0;
                                unit->device->queue_depth = 1;
                        }
		}
		ZFCP_READ_UNLOCK(&port->unit_list_lock);
	}
	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);

        ZFCP_LOG_TRACE("exit\n ");

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

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->fake_list_lock,flags);
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
        zfcp_data.fake_scsi_reqs_active++;
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->fake_list_lock,flags);

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
        ZFCP_READ_LOCK_IRQSAVE(&adapter->abort_lock, flags);
        ZFCP_WRITE_LOCK(&adapter->fake_list_lock);
        if(adapter->first_fake_cmnd == NULL) {
                ZFCP_LOG_DEBUG("Processing of fake-queue called "
                               "for an empty queue.\n");
        } else {        
                current_cmnd=adapter->first_fake_cmnd;
                do {
                        next_cmnd=(Scsi_Cmnd *)(current_cmnd->host_scribble);
                        current_cmnd->host_scribble = NULL;
			zfcp_cmd_dbf_event_scsi("clrfake", current_cmnd);
                        current_cmnd->scsi_done(current_cmnd);
#ifdef ZFCP_DEBUG_REQUESTS
                        debug_text_event(adapter->req_dbf, 2, "fk_done:");
                        debug_event(adapter->req_dbf, 2, &current_cmnd, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
                        zfcp_data.fake_scsi_reqs_active--;
                        current_cmnd=next_cmnd;
                } while (next_cmnd != NULL);
                /* Set list to empty */
                adapter->first_fake_cmnd = NULL;
        }
        ZFCP_WRITE_UNLOCK(&adapter->fake_list_lock);
        ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->abort_lock, flags);

	ZFCP_LOG_TRACE("exit\n");
 
	return;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
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
int zfcp_scsi_queuecommand(Scsi_Cmnd *scpnt, void (* done)(Scsi_Cmnd *))
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI

#ifndef ZFCP_FAKE_SCSI_COMPLETION
        int temp_ret;
	zfcp_unit_t *unit;
#endif
	zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE(
		"enter (scpnt=0x%lx done=0x%lx)\n",
		(unsigned long)scpnt,
		(unsigned long)done);

	spin_unlock_irq(&io_request_lock);
        /* reset the status for this request */
        scpnt->result=0;
	/* save address of mid layer call back function */
	scpnt->scsi_done = done;
	/*
	 * figure out adapter
	 * (previously stored there by the driver when
	 * the adapter was registered)
	 */
	adapter = (zfcp_adapter_t*) scpnt->host->hostdata[0];
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 3, "q_scpnt");
	debug_event(adapter->req_dbf, 3, &scpnt, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

        
#ifdef ZFCP_FAKE_SCSI_COMPLETION
        goto stop;
#else
	/*
	 * figure out target device
	 * (stored there by the driver when the first command
	 * is sent to this target device)
	 * ATTENTION: assumes hostdata initialized to NULL by
	 * mid layer (see scsi_scan.c)
	 */
	if (scpnt->device->hostdata) {
		/* quick lookup, we stored it there before */
		unit = (zfcp_unit_t*) scpnt->device->hostdata;
		ZFCP_LOG_TRACE(
			"direct lookup of logical unit address (0x%lx)\n",
			(unsigned long)unit);
	} else	{
		/*
		 * first time a command is sent to this target device via this
		 * adapter, store address of our unit data structure in target
		 * device data structure of mid layer for quick lookup on
		 * further commands
		 */
		unit = zfcp_unit_lookup(
				adapter,
				scpnt->device->channel,
				scpnt->device->id,
				scpnt->device->lun);
		/* Is specified unit configured? */
		if (unit) {
			scpnt->device->hostdata = unit;
                        unit->device = scpnt->device;
			ZFCP_LOG_TRACE(
				"logical unit address (0x%lx) saved "
				"for direct lookup and scsi_stack "
                                "pointer 0x%lx saved in unit structure\n",
				(unsigned long)unit,
                                (unsigned long)unit->device);
		} else	{
			ZFCP_LOG_DEBUG(
				"logical unit (%i %i %i %i) not configured\n",
				scpnt->host->host_no,
				scpnt->device->channel,
				scpnt->device->id,
				scpnt->device->lun);
			/*
			 * must fake SCSI command execution and scsi_done
			 * callback for non-configured logical unit
			 */
			/* return this as long as we are unable to process requests */
			scpnt->result = DID_NO_CONNECT << 16;
			zfcp_cmd_dbf_event_scsi("notconf", scpnt);
                        scpnt->scsi_done(scpnt);
#ifdef ZFCP_DEBUG_REQUESTS
                        debug_text_event(adapter->req_dbf, 2, "nc_done:");
                        debug_event(adapter->req_dbf, 2, &scpnt, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */

			goto out;
		}
	}
        if(atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)
           || !atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status)) {
                /* See log at stop */
                goto stop;
        }
        if(!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status)) { 
		ZFCP_LOG_DEBUG(
			"adapter with devno 0x%04x not ready or unit with FCP_LUN 0x%016Lx "
                        "on the port with WWPN 0x%016Lx in recovery.\n",
			adapter->devno,
                        (llui_t)unit->fcp_lun,
                        (llui_t)unit->port->wwpn);
		goto fake;
	}

        //        error_counter++;
#if 0
        if (error_counter>=50000) {
                if(error_counter==50000)
                        ZFCP_LOG_NORMAL("*****NOT PROCESSING *****\n");
                if(error_counter>50000) {
                        if(error_counter<51000) goto fake;
                        else {
                                error_counter=0;
                                ZFCP_LOG_NORMAL("***** PROCESSING AGAIN*****\n");
                        }
                }
        }
#endif
        //        if(error_counter % 2) {
	temp_ret = zfcp_fsf_send_fcp_command_task(adapter,
                                                  unit,
                                                  scpnt,
                                                  ZFCP_REQ_AUTO_CLEANUP);
        //        } else temp_ret=-1;
        if (temp_ret<0) {
                ZFCP_LOG_DEBUG("error: Could not send a Send FCP Command\n");
                goto fake;
        } 
	goto out;

stop:
        ZFCP_LOG_INFO("Stopping SCSI IO on the unit with FCP_LUN 0x%016Lx "
                      "connected to the port with WWPN 0x%016Lx at the "
                      "adapter with devno 0x%04x.\n",
                      (llui_t)unit->fcp_lun,
                      (llui_t)unit->port->wwpn,
                      adapter->devno);
        /* Always pass through to upper layer */
        scpnt->retries = scpnt->allowed - 1;
        scpnt->result |= DID_ERROR << 16;
	zfcp_cmd_dbf_event_scsi("stopping", scpnt);
        /* return directly */
        scpnt->scsi_done(scpnt);
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 2, "de_done:");
	debug_event(adapter->req_dbf, 2, &scpnt, sizeof(unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
        goto out;

fake:
        ZFCP_LOG_DEBUG("Looping SCSI IO on the unit with FCP_LUN 0x%016Lx "
                       "connected to the port with WWPN 0x%016Lx at the "
                       "adapter with devno 0x%04x.\n",
                       (llui_t)unit->fcp_lun,
                       (llui_t)unit->port->wwpn,
                       adapter->devno);
        /* 
         * Reset everything for devices with retries, allow at least one retry
         * for others, e.g. tape.
         */
        scpnt->retries = 0;
        if (scpnt->allowed == 1) {
                scpnt->allowed = 2;
        }
        scpnt->result |= DID_SOFT_ERROR << 16 
                | SUGGEST_RETRY << 24;
        zfcp_scsi_insert_into_fake_queue(adapter,scpnt);

out:
#endif	/* !ZFCP_FAKE_SCSI_COMPLETION */

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

	ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (id != port->scsi_id)
			continue;
		ZFCP_READ_LOCK(&port->unit_list_lock);
		ZFCP_FOR_EACH_UNIT(port, unit) {
			if (lun == unit->scsi_lun) {
				ZFCP_LOG_TRACE("found unit\n");
				break;
			}
		}
		ZFCP_READ_UNLOCK(&port->unit_list_lock);
		if (unit)
			break;
	}
	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
 
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
        int retval = 0;

	ZFCP_LOG_TRACE("enter (adapter=0x%lx, cmnd=0x%lx)\n",
                       (unsigned long)adapter,
                       (unsigned long)cmnd);

        current_cmnd=adapter->first_fake_cmnd;
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
                retval=1;
                goto out;
        } 
        do {
                prev_cmnd = current_cmnd;
                current_cmnd = (Scsi_Cmnd *)(current_cmnd->host_scribble);
                if (current_cmnd==cmnd) {
                        prev_cmnd->host_scribble=current_cmnd->host_scribble;
                        current_cmnd->host_scribble=NULL;
                        retval=1;
                        goto out;
                }
        } while (current_cmnd->host_scribble != NULL);         

 out:
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
        zfcp_req_data_t *req_data = NULL;
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
	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->abort_lock, flags);

	/*
	 * Check if we deal with a faked command, which we may just forget
	 * about from now on
	 */
        ZFCP_WRITE_LOCK(&adapter->fake_list_lock);
        /* only need to go through list if there are faked requests */
	if (adapter->first_fake_cmnd != NULL) {
		if (zfcp_scsi_potential_abort_on_fake(adapter, scpnt)) {
			zfcp_data.fake_scsi_reqs_active--;
			ZFCP_WRITE_UNLOCK(&adapter->fake_list_lock);
			ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->abort_lock, flags);
			ZFCP_LOG_INFO("A faked command was aborted\n");
			retval = SUCCESS;
			strncpy(dbf_result, "##faked", ZFCP_ABORT_DBF_LENGTH);
			goto out;
		}
	}
        ZFCP_WRITE_UNLOCK(&adapter->fake_list_lock);

	/*
	 * Check whether command has just completed and can not be aborted.
	 * Even if the command has just been completed late, we can access
	 * scpnt since the SCSI stack does not release it at least until
	 * this routine returns. (scpnt is parameter passed to this routine
	 * and must not disappear during abort even on late completion.)
	 */
	req_data = (zfcp_req_data_t*) scpnt->host_scribble;
        /* DEBUG */
	ZFCP_LOG_DEBUG(
		"req_data=0x%lx\n",
		(unsigned long)req_data);
	if (!req_data) {
		ZFCP_LOG_DEBUG("late command completion overtook abort\n");
		/*
		 * That's it.
		 * Do not initiate abort but return SUCCESS.
		 */
		ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->abort_lock, flags);
		retval = SUCCESS;
		strncpy(dbf_result, "##late1", ZFCP_ABORT_DBF_LENGTH);
		goto out;
	}

	/* Figure out which fsf_req needs to be aborted. */
	old_fsf_req = req_data->send_fcp_command_task.fsf_req;
#ifdef ZFCP_DEBUG_ABORTS
	dbf_fsf_req = (unsigned long)old_fsf_req;
	dbf_timeout = (jiffies - req_data->send_fcp_command_task.start_jiffies) / HZ;
#endif
        /* DEBUG */
	ZFCP_LOG_DEBUG(
		"old_fsf_req=0x%lx\n",
		(unsigned long)old_fsf_req);
	if (!old_fsf_req) {
		ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->abort_lock, flags);
		ZFCP_LOG_NORMAL("bug: No old fsf request found.\n");
		ZFCP_LOG_NORMAL("req_data:\n");
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_NORMAL,
			(char*)req_data,
			sizeof(zfcp_req_data_t));
		ZFCP_LOG_NORMAL("scsi_cmnd:\n");
		ZFCP_HEX_DUMP(
			ZFCP_LOG_LEVEL_NORMAL,
			(char*)scpnt,
			sizeof(struct scsi_cmnd));
		retval = FAILED;
		strncpy(dbf_result, "##bug:r", ZFCP_ABORT_DBF_LENGTH);
		goto out;
	}
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
	 * The 'Abort FCP Command' routine may block (call schedule)
	 * because it may wait for a free SBAL.
	 * That's why we must release the lock and enable the
	 * interrupts before.
	 * On the other hand we do not need the lock anymore since
	 * all critical accesses to scsi_req are done.
	 */
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->abort_lock, flags);
	/* call FSF routine which does the abort */
	new_fsf_req = zfcp_fsf_abort_fcp_command(
				(unsigned long)old_fsf_req,
				adapter,
				unit,
				ZFCP_WAIT_FOR_SBAL);
	ZFCP_LOG_DEBUG(
		"new_fsf_req=0x%lx\n",
		(unsigned long) new_fsf_req);
	if (!new_fsf_req) {
		retval = FAILED;
		ZFCP_LOG_DEBUG(
			"warning: Could not abort SCSI command "
			"at 0x%lx\n",
			(unsigned long)scpnt);
		strncpy(dbf_result, "##nores", ZFCP_ABORT_DBF_LENGTH);
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
		strncpy(dbf_result, "##succ", ZFCP_ABORT_DBF_LENGTH);
	} else	if (status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED) {
		retval = SUCCESS;
		strncpy(dbf_result, "##late2", ZFCP_ABORT_DBF_LENGTH);
	} else	{
		retval = FAILED;
		strncpy(dbf_result, "##fail", ZFCP_ABORT_DBF_LENGTH);
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
	 		(adapter, unit, tm_flags, ZFCP_WAIT_FOR_SBAL);
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
            (adapter->devinfo.sid_data.dev_model != ZFCP_DEVICE_MODEL)) {
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
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_paranoia_qdio_queue(
	zfcp_adapter_t *adapter,
	int irq,
	int queue_number,
	int first_element,
	int elements_processed,
	int queue_flag)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_QDIO
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_QDIO

	int retval = -EINVAL;
#if 0
	int local_index;
	int local_count;
	int expected_first_element;
	int max_expected_elements_processed;
#endif

	zfcp_qdio_queue_t *queue;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx queue_number=%i "
		"first_element=%i elements_processed=%i\n",
		(unsigned long)adapter,
		queue_number,
		first_element,
		elements_processed);

	if (!adapter) {
		ZFCP_LOG_NORMAL(
			"bug: Pointer to an adapter struct is a null "
                        "pointer\n");
		goto out;
	}

	if (queue_number != 0) {
		ZFCP_LOG_NORMAL(
			"bug: A QDIO (data transfer mechanism) queue has the "
                        "wrong identification number (debug info %d).\n",
                        queue_number);
		goto out;
	}

	if ((first_element < 0) || (first_element >= QDIO_MAX_BUFFERS_PER_Q)) {
		ZFCP_LOG_NORMAL(
			"bug: A QDIO (data transfer mechanism) queue index is "
                        "out of bounds (debug info %d).\n",
			first_element);
		goto out;
	}

	if (adapter->irq != irq) {
		ZFCP_LOG_NORMAL(
			"bug: Interrupt received on irq 0x%x, for the adapter "
                        "with irq 0x%x and devno 0x%04x\n",
			irq,
			adapter->irq,
                        adapter->devno);
		goto out;
	}

	if (queue_flag) {
	  queue = &adapter->response_queue;
	  ZFCP_LOG_TRACE("It's a response queue.\n");
	}
	else	{
	  queue = &adapter->request_queue;
	  ZFCP_LOG_TRACE("It's a request queue.\n");
	}
	retval = 0;
out:
        if(retval != 0) 
                ZFCP_LOG_NORMAL("exit %i\n",
                               retval);
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
	int retval;

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
	/*
	 * we stored address of zfcp_adapter_t data structure
	 * associated with irq in int_parm
	 */
     
	/* paranoia checks */
	ZFCP_PARANOIA {
		retval = zfcp_paranoia_qdio_queue(
				adapter,
				irq,
				queue_number,
				first_element,
				elements_processed,
				0);

		if (retval)
			goto out;
	}


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

	/*
	 * we stored address of zfcp_adapter_t data structure
	 * associated with irq in int_parm
	 */
	/* paranoia check */
	ZFCP_PARANOIA {
		retval = zfcp_paranoia_qdio_queue(
				adapter,
				irq,
				queue_number,
				first_element,
				elements_processed,
				1);
		if (retval)
			goto out;
	}
        
        buffere = &(queue->buffer[first_element]->element[0]);
        ZFCP_LOG_DEBUG("first BUFFERE flags=0x%x \n ",
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

                if (!buffere->flags & SBAL_FLAGS_LAST_ENTRY) {
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
        unsigned long flags;

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

	/* DEBUG: force an abort which is being hung than, usage of mod_parm dismisses pending fsf_req */

#if 0
	if (fsf_req->qtcb->prefix.rcd ..
eq_seq_no == 0x100) {
		ZFCP_LOG_NORMAL("*******************force abort*****************\n");
		goto out;
	}

	if (fsf_req->fsf_command == FSF_QTCB_ABORT_FCP_CMND) {
		ZFCP_LOG_NORMAL("***********ignoring abort completion************\n");
                goto out;
        }
	if (fsf_req->fsf_command == FSF_QTCB_FCP_CMND) {
                if((jiffies & 0xfff)<=0x40) {
                        printk( "*************************************"
				"Debugging:Ignoring return of SCSI command"
				"********************************\n");
                        goto out;
               }
	}
#endif

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

		ZFCP_READ_LOCK_IRQSAVE(&adapter->fsf_req_list_lock, flags);
		retval = zfcp_paranoia_fsf_reqs(adapter, fsf_req);
		ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->fsf_req_list_lock, flags);
		if (retval) {
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
			ZFCP_QTCB_SIZE);
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
		if (fsf_req->qtcb->header.log_start > ZFCP_QTCB_SIZE) {
			ZFCP_LOG_NORMAL(
				"bug: ULP (FSF logging) log data starts "
                                "beyond end of packet header. Ignored. "
				"(start=%i, size=%li)\n",
				fsf_req->qtcb->header.log_start,
				ZFCP_QTCB_SIZE);
			goto forget_log;
		}
		if ((fsf_req->qtcb->header.log_start +
		     fsf_req->qtcb->header.log_length)
		    > ZFCP_QTCB_SIZE) {
			ZFCP_LOG_NORMAL(
				"bug: ULP (FSF logging) log data ends "
                                "beyond end of packet header. Ignored. "
				"(start=%i, length=%i, size=%li)\n",
				fsf_req->qtcb->header.log_start,
				fsf_req->qtcb->header.log_length,
				ZFCP_QTCB_SIZE);
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
                        ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL, (char *)fsf_req->qtcb, ZFCP_QTCB_SIZE);
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
			ZFCP_QTCB_SIZE);
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
			zfcp_fsf_send_generic_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
			break;

		case FSF_QTCB_OPEN_PORT_WITH_DID :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_PORT_WITH_DID\n");
			zfcp_fsf_open_port_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
			break;

		case FSF_QTCB_OPEN_LUN :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_LUN\n");
			zfcp_fsf_open_unit_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
			break;

		case FSF_QTCB_CLOSE_LUN :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_LUN\n");
			zfcp_fsf_close_unit_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
			break;

		case FSF_QTCB_CLOSE_PORT :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PORT\n");
			zfcp_fsf_close_port_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
			break;

                case FSF_QTCB_CLOSE_PHYSICAL_PORT :
                        ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PHYSICAL_PORT\n");
                        zfcp_fsf_close_physical_port_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
                        break;

		case FSF_QTCB_EXCHANGE_CONFIG_DATA :
			ZFCP_LOG_FLAGS(2, "FSF_QTCB_EXCHANGE_CONFIG_DATA\n");
			zfcp_fsf_exchange_config_data_handler(fsf_req);
			zfcp_erp_fsf_req_handler(fsf_req);
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
	volatile qdio_buffer_element_t *buffere;
        zfcp_qdio_queue_t *req_queue = &adapter->request_queue;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx, req_flags=0x%x)\n", 
		(unsigned long)adapter,
		req_flags);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			adapter,
			FSF_QTCB_UNSOLICITED_STATUS,
			&lock_flags,
			req_flags,
			&fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create an "
			"unsolicited status buffer for "
			"the adapter with devno 0x%04x.\n",
			adapter->devno);
                goto failed_req_create;
        }

	status_buffer = zfcp_mem_pool_find(&adapter->pool.status_read_buf);
	if (!status_buffer) {
		ZFCP_LOG_NORMAL("bug: could not get some buffer\n");
		goto failed_buf;
	}
	fsf_req->data.status_read.buffer = status_buffer;

	/* insert pointer to respective buffer */
	buffere = req_queue->buffer[fsf_req->sbal_index]->element;
	buffere[2].addr = (void *)status_buffer;
	buffere[2].length = sizeof(fsf_status_read_buffer_t);

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
	zfcp_mem_pool_return(status_buffer, &adapter->pool.status_read_buf);

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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->request_queue.queue_lock, lock_flags);
        
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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT (adapter, port)
		if (port->d_id == (status_buffer->d_id & ZFCP_DID_MASK))
			break;
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);

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
		zfcp_mem_pool_return(status_buffer, &adapter->pool.status_read_buf);
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
                /* Unneccessary, ignoring.... */
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

	zfcp_mem_pool_return(status_buffer, &adapter->pool.status_read_buf);
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
                default:
                        /* cannot happen */
                        break;
                }
                known=0;
                ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
                ZFCP_FOR_EACH_PORT (adapter, port) {
                        if (!atomic_test_mask(ZFCP_STATUS_PORT_DID_DID, &port->status))
                                continue;
                        if((port->d_id & range_mask) 
                           == (fcp_rscn_element->nport_did & range_mask)) {
                                known++;
#if 0
                                printk("known=%d, reopen did 0x%x\n",
                                       known,
                                       fcp_rscn_element->nport_did);
#endif
				debug_text_event(adapter->erp_dbf,1,"unsol_els_rscnk:");
                                zfcp_erp_port_reopen(port, 0);
                        }
                }
                ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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
                ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
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
                ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (port->wwpn == (*(wwn_t *)&els_logi->nport_wwn))
			break;
	}
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);

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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
		if (port->wwpn == els_logo->nport_wwpn)
			break;
	}
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);

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
 
	zfcp_fsf_req_t *new_fsf_req = NULL;
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
			&lock_flags,
			req_flags,
			&new_fsf_req);
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
        
	new_fsf_req->data.abort_fcp_command.unit = unit;

	/* set handles of unit and its parent port in QTCB */
	new_fsf_req->qtcb->header.lun_handle = unit->handle;
	new_fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* set handle of request which should be aborted */
        new_fsf_req->qtcb->bottom.support.req_handle = (u64)old_req_id;

#if 0
	/* DEBUG */
	goto out;
#endif

	/* start QDIO request for this FSF request */
        
        zfcp_fsf_start_scsi_er_timer(adapter);
        retval = zfcp_fsf_req_send(new_fsf_req, NULL);
	if (retval) {
                del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO(
			"error: Could not send an abort command request "
			"for a command on the adapter with devno 0x%04x, "
                        "port WWPN 0x%016Lx and unit FCP_LUN 0x%016Lx\n",
			adapter->devno,
			(llui_t)unit->port->wwpn,
			(llui_t)unit->fcp_lun);
		if (zfcp_fsf_req_free(new_fsf_req)) {
			ZFCP_LOG_NORMAL(
				"bug: Could not remove one FSF "
				"request. Memory leakage possible. "
                                "(debug info 0x%lx).\n",
                                (unsigned long)new_fsf_req);
                };
		new_fsf_req = NULL;
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->request_queue.queue_lock, lock_flags);

	ZFCP_LOG_DEBUG("exit (0x%lx)\n", (unsigned long)new_fsf_req);
 
	return new_fsf_req;
 
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
                ZFCP_LOG_FLAGS(0, "FSF_GOOD\n");
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
 * function:	zfcp_release_nameserver_buffers
 *
 * purpose:	
 *
 * returns:
 */
static void zfcp_release_nameserver_buffers(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	zfcp_adapter_t *adapter = fsf_req->adapter;
	void *buffer = fsf_req->data.send_generic.outbuf;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx\n",
		(unsigned long)fsf_req);
	/* FIXME: not sure about appeal of this new flag (martin) */
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOLBUF)
		zfcp_mem_pool_return(buffer, &adapter->pool.nameserver);
	else	ZFCP_KFREE(buffer, 2 * sizeof(fc_ct_iu_t));

	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_get_nameserver_buffers
 *
 * purpose:	
 *
 * returns:
 *
 * locks:       fsf_request_list_lock is held when doing buffer pool 
 *              operations
 */
static int zfcp_get_nameserver_buffers(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	zfcp_send_generic_t *data = &fsf_req->data.send_generic;
	zfcp_adapter_t *adapter = fsf_req->adapter;
        int retval = 0;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx\n",
		(unsigned long)fsf_req);

#ifdef ZFCP_MEM_POOL_ONLY
	data->outbuf = NULL;
#else
	data->outbuf = ZFCP_KMALLOC(2 * sizeof(fc_ct_iu_t), GFP_ATOMIC);
#endif
	if (!data->outbuf) {
		ZFCP_LOG_DEBUG(
			"Out of memory. Could not allocate at "
                        "least one of the buffers "
			"required for a name-server request on the"
			"adapter with devno 0x%04x directly.. "
                        "trying emergency pool\n",
			adapter->devno);
		data->outbuf = zfcp_mem_pool_find(&adapter->pool.nameserver);
                if (!data->outbuf) {
                        ZFCP_LOG_DEBUG(
                             "Out of memory. Could not get emergency "
                             "buffer required for a name-server request on the"
                             "adapter with devno 0x%04x. All buffers"
                             "are in use.\n",
                             adapter->devno);
                        retval = -ENOMEM;
                        goto out;
                }
		fsf_req->status |= ZFCP_STATUS_FSFREQ_POOLBUF;
	}
	data->outbuf_length = sizeof(fc_ct_iu_t);
	data->inbuf_length = sizeof(fc_ct_iu_t);
	data->inbuf = (char*)((unsigned long)data->outbuf + sizeof(fc_ct_iu_t));
out:
	ZFCP_LOG_TRACE("exit (%d)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_nameserver_request
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_nameserver_request(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	fc_ct_iu_t *fc_ct_iu;
	unsigned long lock_flags;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);
	ZFCP_PARANOIA {
		if (!erp_action->adapter->nameserver_port) {
			ZFCP_LOG_NORMAL("bug: no nameserver available\n");
			retval = -EINVAL;
			goto out;
		}
	}

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_SEND_GENERIC,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
			"name server registration request for "
			"the adapter with devno 0x%04x.\n",
			erp_action->adapter->devno);
		goto failed_req;
	}
        retval = zfcp_get_nameserver_buffers(erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of memory. Could not allocate one of "
                        "the buffers "
			"required for a name-server request on the"
			"adapter with devno 0x%04x.\n",
			erp_action->adapter->devno);
		goto failed_buffers;
	}

	/* setup name-server request in first page */
	fc_ct_iu = (fc_ct_iu_t*)erp_action->fsf_req->data.send_generic.outbuf;
        fc_ct_iu->revision = ZFCP_CT_REVISION;
        fc_ct_iu->gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        fc_ct_iu->gs_subtype = ZFCP_CT_NAME_SERVER;
        fc_ct_iu->options = ZFCP_CT_SYNCHRONOUS;
        fc_ct_iu->cmd_rsp_code = ZFCP_CT_GID_PN;
        fc_ct_iu->max_res_size = ZFCP_CT_MAX_SIZE;
	fc_ct_iu->data.wwpn = erp_action->port->wwpn;

	erp_action->fsf_req->data.send_generic.handler = zfcp_nameserver_request_handler;
	erp_action->fsf_req->data.send_generic.handler_data = (unsigned long)erp_action->port;
	erp_action->fsf_req->data.send_generic.port = erp_action->adapter->nameserver_port;
	erp_action->fsf_req->erp_action = erp_action;

	/* send this one */
	retval = zfcp_fsf_send_generic(
			erp_action->fsf_req,
			ZFCP_NAMESERVER_TIMEOUT, /* in seconds */
			&lock_flags,
			&erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO(
                        "error: Could not send a"
                        "nameserver request command to the "
                        "adapter with devno 0x%04x\n",
                        erp_action->adapter->devno);
                goto failed_send;
	}

	goto out;

failed_send:
        zfcp_release_nameserver_buffers(erp_action->fsf_req);

failed_buffers:
	if (zfcp_fsf_req_free(erp_action->fsf_req)) {
                ZFCP_LOG_NORMAL(
			"bug: Could not remove one FSF "
			"request. Memory leakage possible. "
			"(debug info 0x%lx).\n",
			(unsigned long)erp_action->fsf_req);
                retval = -EINVAL;
	};
	erp_action->fsf_req = NULL;

failed_req:
out:
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);
	ZFCP_LOG_TRACE("exit (%d)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:	zfcp_nameserver_request_handler
 *
 * purpose:	
 *
 * returns:
 */
static void zfcp_nameserver_request_handler(zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	fc_ct_iu_t *fc_ct_iu_resp = (fc_ct_iu_t*)(fsf_req->data.send_generic.inbuf);
	fc_ct_iu_t *fc_ct_iu_req = (fc_ct_iu_t*)(fsf_req->data.send_generic.outbuf);
	zfcp_port_t *port = (zfcp_port_t*)fsf_req->data.send_generic.handler_data;

	ZFCP_LOG_TRACE("enter\n");

        if (fc_ct_iu_resp->revision != ZFCP_CT_REVISION)
		goto failed;
        if (fc_ct_iu_resp->gs_type != ZFCP_CT_DIRECTORY_SERVICE)
		goto failed;
        if (fc_ct_iu_resp->gs_subtype != ZFCP_CT_NAME_SERVER)
		goto failed;
        if (fc_ct_iu_resp->options != ZFCP_CT_SYNCHRONOUS)
		goto failed;
        if (fc_ct_iu_resp->cmd_rsp_code != ZFCP_CT_ACCEPT) {
		/* FIXME: do we need some specific erp entry points */
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto failed;
	}
	/* paranoia */
	if (fc_ct_iu_req->data.wwpn != port->wwpn) {
		ZFCP_LOG_NORMAL(
			"bug: Port WWPN returned by nameserver lookup "
                        "does not correspond to "
                        "the expected value on the adapter with devno 0x%04x. "
			"(debug info 0x%016Lx, 0x%016Lx)\n",
                        port->adapter->devno,
			(llui_t)port->wwpn,
                        (llui_t)fc_ct_iu_req->data.wwpn);
		goto failed;
	}

	/* looks like a valid d_id */
        port->d_id = ZFCP_DID_MASK & fc_ct_iu_resp->data.d_id;
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
		(char*)fc_ct_iu_req,
		sizeof(fc_ct_iu_t));
	ZFCP_HEX_DUMP(
		ZFCP_LOG_LEVEL_DEBUG,
		(char*)fc_ct_iu_resp,
		sizeof(fc_ct_iu_t));

out:
	zfcp_release_nameserver_buffers(fsf_req);

	ZFCP_LOG_TRACE("exit\n");

	return;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_generic
 *
 * purpose:	sends a FC request according to FC-GS-3
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated 
 */
static int zfcp_fsf_send_generic(
		zfcp_fsf_req_t *fsf_req,
		unsigned char timeout,
		unsigned long *lock_flags,
		struct timer_list *timer)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = 0;
	qdio_buffer_t *buffer;
	volatile qdio_buffer_element_t *buffer_element = NULL;
	zfcp_port_t *port = fsf_req->data.send_generic.port;
	zfcp_adapter_t *adapter = port->adapter;
 
	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx "
		"timeout=%i *lock_flags=0x%lx)\n",
		(unsigned long)fsf_req,
		timeout,
		*lock_flags);

	/* put buffers to the 2 SBALEs after the QTCB */
	buffer = (adapter->request_queue.buffer[fsf_req->sbal_index]);
	buffer_element = &(buffer->element[2]);
	buffer_element->addr = fsf_req->data.send_generic.outbuf;
	buffer_element->length = fsf_req->data.send_generic.outbuf_length;
	buffer_element++;
	buffer_element->addr = fsf_req->data.send_generic.inbuf;
	buffer_element->length = fsf_req->data.send_generic.inbuf_length;
	buffer_element->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* settings in QTCB */
	fsf_req->qtcb->header.port_handle = port->handle;
	fsf_req->qtcb->bottom.support.service_class
		= adapter->fc_service_class;
	fsf_req->qtcb->bottom.support.timeout = timeout;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, timer);
	if (retval) {
		ZFCP_LOG_DEBUG(
			"error: Out of resources. could not send a "
                        "generic services "
			"command via the adapter with devno 0x%04x, port "
                        "WWPN 0x%016Lx\n",
			adapter->devno,
			(llui_t) port->wwpn);
                /* fsf_req structure will be cleaned up by higher layer handler */
		goto out;
	}

	ZFCP_LOG_DEBUG(
		"Send Generic request initiated "
		"(adapter devno=0x%04x, port D_ID=0x%06x)\n",
		adapter->devno,
		port->d_id);

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_fsf_send_generic_handler
 *
 * purpose:	is called for finished Send Generic request
 *
 * returns:	
 */
static int zfcp_fsf_send_generic_handler(
		zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval = -EINVAL;
	zfcp_port_t *port = fsf_req->data.send_generic.port;

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

        case FSF_GENERIC_COMMAND_REJECTED :
                ZFCP_LOG_FLAGS(1,"FSF_GENERIC_COMMAND_REJECTED\n");
                ZFCP_LOG_INFO("warning: The port with WWPN 0x%016Lx connected to "
                              "the adapter of devno 0x%04x is"
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
			"the adapter of devno 0x%04x is"
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
			"the adapter of devno 0x%04x is"
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
                        zfcp_erp_port_forced_reopen(port, 0);
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
	(fsf_req->data.send_generic.handler)(fsf_req);

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
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 */
static int zfcp_fsf_exchange_config_data(zfcp_erp_action_t *erp_action)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	int retval = 0;
	unsigned long lock_flags;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_EXCHANGE_CONFIG_DATA,
			&lock_flags,
			ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
	if (retval < 0) {
                ZFCP_LOG_INFO(
			 "error: Out of resources. Could not create an "
                         "exchange configuration data request for"
                         "the adapter with devno 0x%04x.\n",
                         erp_action->adapter->devno);
		goto out;
	}

	erp_action->fsf_req->erp_action = erp_action;
	/* no information from us to adapter, set nothing */

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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

        ZFCP_LOG_TRACE("exit (%d)\n", retval);
 
        return retval;

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
static int zfcp_fsf_exchange_config_data_handler
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

#if 0        
        /* DEBUGGING */
        fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
        atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT, 
                        &(fsf_req->adapter->status));
#endif
        if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't set any value, stay with the old (unitialized) ones */ 
                goto skip_fsfstatus;
        }

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

        case FSF_GOOD :
                ZFCP_LOG_FLAGS(2,"FSF_GOOD\n");
                bottom = &fsf_req->qtcb->bottom.config;
                /* only log QTCB versions for now */
                ZFCP_LOG_DEBUG("low QTCB version 0x%x of FSF, "
                               "high QTCB version 0x%x of FSF, \n",
                               bottom->low_qtcb_version,
                               bottom->high_qtcb_version);
                adapter->wwnn = bottom->nport_serv_param.wwnn;
                adapter->wwpn = bottom->nport_serv_param.wwpn;
		adapter->s_id = bottom->s_id & ZFCP_DID_MASK;
		adapter->hydra_version = bottom->adapter_type;
                adapter->fsf_lic_version = bottom->lic_version;
                adapter->fc_topology = bottom->fc_topology;
		adapter->fc_link_speed = bottom->fc_link_speed;
		ZFCP_LOG_INFO(
			"The adapter with devno 0x%04x reported "
			"the following characteristics:\n"
			"WWNN 0x%016Lx, WWPN 0x%016Lx, S_ID 0x%06x,\n"
			"adapter version 0x%x, LIC version 0x%x, FC link speed %d Gb/s\n",
			adapter->devno,
			(llui_t)adapter->wwnn,
			(llui_t)adapter->wwpn,
			adapter->s_id,
			adapter->hydra_version,
			adapter->fsf_lic_version,
			adapter->fc_link_speed);
		if (ZFCP_QTCB_VERSION < bottom->low_qtcb_version) {
			ZFCP_LOG_NORMAL(
				"error: the adapter with devno 0x%04x "
				"only supports newer control block versions "
				"in comparison to this device driver "
				"(try updated device driver)\n",
				adapter->devno);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"low_qtcb_ver");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
		if (ZFCP_QTCB_VERSION > bottom->high_qtcb_version) {
			ZFCP_LOG_NORMAL(
				"error: the adapter with devno 0x%04x "
				"only supports older control block versions "
				"than this device driver uses"
				"(consider a microcode upgrade)\n",
				adapter->devno);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"high_qtcb_ver");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
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
                if(bottom->max_qtcb_size < ZFCP_QTCB_SIZE) {
                        ZFCP_LOG_NORMAL("bug: Maximum QTCB size (%d bytes) "
                                        "allowed by the adapter with devno "
                                        "0x%04x is lower than the minimum "
                                        "required by the driver (%ld bytes).\n",
                                        bottom->max_qtcb_size,
                                        adapter->devno,
                                        ZFCP_QTCB_SIZE);
                        debug_text_event(fsf_req->adapter->erp_dbf,0,"qtcb-size");
                        debug_event(fsf_req->adapter->erp_dbf,0,&bottom->max_qtcb_size,
                                    sizeof(u32)); 
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
                }
                atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK, &adapter->status);
                retval = 0;
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
 
	int retval = 0;
	unsigned long lock_flags;
 
	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_OPEN_PORT_WITH_DID,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

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

        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_PORT,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
        if (retval < 0) {
		ZFCP_LOG_INFO(
			"error: Out of resources. Could not create a "
			"close port request for WWPN 0x%016Lx connected to "
			"the adapter with devno 0x%04x.\n",
			(llui_t)erp_action->port->wwpn,
			erp_action->adapter->devno);
		goto out;
        }

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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

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
				"the adapter of devno 0x%04x is"
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

        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_PHYSICAL_PORT,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

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
				"the adapter of devno 0x%04x is"
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
                        ZFCP_READ_LOCK_IRQSAVE(&port->unit_list_lock, flags);
                        ZFCP_FOR_EACH_UNIT(port, unit) {
                                atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, 
                                                  &unit->status);
                        }
                        ZFCP_READ_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);
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
 
	int retval = 0;
	unsigned long lock_flags;

	ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_OPEN_LUN,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
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

	erp_action->fsf_req->qtcb->header.port_handle = erp_action->port->handle;
	*(fcp_lun_t*)&(erp_action->fsf_req->qtcb->bottom.support.fcp_lun)
		= erp_action->unit->fcp_lun;
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

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
	zfcp_unit_t *unit;
 
	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	unit = fsf_req->data.open_unit.unit;

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
                              "the adapter of devno 0x%04x is"
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
                        
        case FSF_LUN_IN_USE :
                ZFCP_LOG_FLAGS(0, "FSF_LUN_IN_USE\n");
                ZFCP_LOG_NORMAL("error: FCP_LUN 0x%016Lx at "
				"the remote port with WWPN 0x%016Lx connected "
				"to the adapter with devno 0x%04x "
				"is already owned by another operating system "
				"instance (LPAR or VM guest)\n",
				(llui_t)unit->fcp_lun,
				(llui_t)unit->port->wwpn,
				unit->port->adapter->devno);
                ZFCP_LOG_NORMAL("Additional sense data is presented:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              (char*)&fsf_req->qtcb->header.fsf_status_qual,
                              16);
                debug_text_event(fsf_req->adapter->erp_dbf,2,"fsf_s_l_in_use");
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

        int retval = 0;
	unsigned long lock_flags;

        ZFCP_LOG_TRACE("enter (erp_action=0x%lx)\n", (unsigned long)erp_action);

        /* setup new FSF request */
        retval = zfcp_fsf_req_create(
			erp_action->adapter,
			FSF_QTCB_CLOSE_LUN,
			&lock_flags,
			ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
			&(erp_action->fsf_req));
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&erp_action->adapter->request_queue.queue_lock, lock_flags);

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
                                "the adapter of devno 0x%04x is"
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
		zfcp_adapter_t *adapter,
		zfcp_unit_t *unit,
                Scsi_Cmnd *scsi_cmnd,
		int req_flags)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
  
	zfcp_fsf_req_t *fsf_req = NULL;
	fcp_cmnd_iu_t *fcp_cmnd_iu;
	volatile qdio_buffer_element_t *buffere;
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
			&lock_flags,
			req_flags,
			&(fsf_req));
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
	fsf_req->data.send_fcp_command_task.fsf_req = fsf_req;
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
	buffere = &(adapter->request_queue.buffer[fsf_req->sbal_index]->element[0]);
	buffere->flags |= sbtype;

	/* set FC service class in QTCB (3 per default) */
	fsf_req->qtcb->bottom.io.service_class = adapter->fc_service_class;

	/* set FCP_LUN in FCP_CMND IU in QTCB */
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;

	/* set task attributes in FCP_CMND IU in QTCB */
	if (scsi_cmnd->device->tagged_queue) {
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
	real_bytes = zfcp_create_sbals_from_sg(
			fsf_req,
			scsi_cmnd,
			sbtype,
			0,
			ZFCP_MAX_SBALS_PER_REQ);
        /* Note: >= and not = because the combined scatter-gather entries
         * may be larger than request_bufflen according to the mailing list
         */
        if (real_bytes >= scsi_cmnd->request_bufflen) {
                ZFCP_LOG_TRACE("Data fits\n");
        } else if (real_bytes == 0) {
                ZFCP_LOG_DEBUG(
			"Data did not fit into available buffer(s), "
			"waiting for more...\n");
                retval=-EIO;
                goto no_fit;
        } else	{
                ZFCP_LOG_NORMAL("error: No truncation implemented but "
                                "required. Shutting down unit "
				"(devno=0x%04x, WWPN=0x%016Lx, FCP_LUN=0x%016Lx)\n",
				unit->port->adapter->devno,
				(llui_t)unit->port->wwpn,
				(llui_t)unit->fcp_lun);
                //                panic("invalid sg_index");
		zfcp_erp_unit_shutdown(unit, 0);
                retval=-EINVAL;
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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->request_queue.queue_lock, lock_flags);

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

	volatile qdio_buffer_element_t *buffere;

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
			&lock_flags,
			req_flags,
			&(fsf_req));
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

	buffere = &(adapter->request_queue.buffer[fsf_req->sbal_index]->element[0]);
	buffere[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
        buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY; 

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
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->request_queue.queue_lock, lock_flags);

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
                                "the adapter of devno 0x%04x is"
				"not valid.\n",
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

	ZFCP_READ_LOCK_IRQSAVE(&fsf_req->adapter->abort_lock, flags);
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
                ZFCP_LOG_NORMAL("status for SCSI Command:\n");
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              scpnt->cmnd,
                              scpnt->cmd_len);
                              
                ZFCP_LOG_NORMAL("SCSI status code 0x%x\n",
                               fcp_rsp_iu->scsi_status);
                ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
                              (void *)fcp_rsp_iu,
                              sizeof(fcp_rsp_iu_t));
                ZFCP_HEX_DUMP(
                              ZFCP_LOG_LEVEL_NORMAL,
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

	zfcp_cmd_dbf_event_scsi("response", scpnt);

	/* cleanup pointer (need this especially for abort) */
	scpnt->host_scribble = NULL;

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
	ZFCP_READ_UNLOCK_IRQRESTORE(&fsf_req->adapter->abort_lock, flags);

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

#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	unsigned long long time;
	time = get_clock();
#endif
        ZFCP_WRITE_LOCK_IRQSAVE(&queue->queue_lock, *flags);
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	zfcp_lock_meter_add(&queue->lock_meter, time);
#endif
	if (atomic_read(&queue->free_count) >= needed) 
		return 1;
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&queue->queue_lock, *flags);
	return 0;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
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
static int zfcp_fsf_req_create(
	zfcp_adapter_t *adapter,
	u32 fsf_cmd,
	unsigned long *lock_flags,
	int req_flags,
        zfcp_fsf_req_t **fsf_req_p)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF

	zfcp_fsf_req_t *fsf_req = NULL;
       	int retval=0;
        zfcp_qdio_queue_t *req_queue = &adapter->request_queue;
        volatile qdio_buffer_element_t *buffere; 
        unsigned long timeout;
        int condition;
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
        unsigned long long time;
#endif

        ZFCP_LOG_TRACE(
		"enter (adapter=0x%lx fsf_cmd=0x%x *lock_flags=0x%lx "
		"req_flags=0x%x)\n",
		(unsigned long)adapter,
		fsf_cmd,
		*lock_flags,
		req_flags);

#if 0
        /* FIXME (design) is this ever usefull */
	if (!atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &adapter->status)) {
		retval = -ENOTSUPP;
		goto failed_running;
	}
#endif

        atomic_inc(&adapter->reqs_in_progress);
	/* allocate new FSF request */
	fsf_req = zfcp_fsf_req_alloc(adapter, fsf_cmd, GFP_ATOMIC);
	if (!fsf_req) {
                ZFCP_LOG_DEBUG(
			"error: Could not put an FSF request into"
                        "the outbound (send) queue.\n");
                retval=-ENOMEM;
		goto failed_fsf_req;
	}
	/* save pointer to "parent" adapter */
	fsf_req->adapter = adapter;

	/* initialize waitqueue which may be used to wait on 
	   this request completion */
	init_waitqueue_head(&fsf_req->completion_wq);

#ifndef ZFCP_PARANOIA_DEAD_CODE
	/* set magics */
	fsf_req->common_magic = ZFCP_MAGIC;
	fsf_req->specific_magic = ZFCP_MAGIC_FSFREQ;
#endif

	fsf_req->fsf_command = fsf_cmd;
	if (req_flags & ZFCP_REQ_AUTO_CLEANUP)
		fsf_req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;

	/* initialize QTCB */
	if (fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS) {
	        ZFCP_LOG_TRACE(
			"fsf_req->qtcb=0x%lx\n",
			(unsigned long ) fsf_req->qtcb);
		fsf_req->qtcb->prefix.req_id = (unsigned long)fsf_req;
		fsf_req->qtcb->prefix.ulp_info = zfcp_data.driver_version;
		fsf_req->qtcb->prefix.qtcb_type = fsf_qtcb_type[fsf_cmd];
		fsf_req->qtcb->prefix.qtcb_version = ZFCP_QTCB_VERSION;
		fsf_req->qtcb->header.req_handle = (unsigned long)fsf_req;
		fsf_req->qtcb->header.fsf_command = fsf_cmd;
		/*
		 * Request Sequence Number is set later when the request is
		 * actually sent.
		 */
	}
	
	/* try to get needed SBALs in request queue (get queue lock on success) */
	ZFCP_LOG_TRACE("try to get free BUFFER in request queue\n");
        if (req_flags & ZFCP_WAIT_FOR_SBAL) {
                timeout = ZFCP_SBAL_TIMEOUT;
                ZFCP_WAIT_EVENT_TIMEOUT(
                         adapter->request_wq,
                         timeout,
                         (condition=(zfcp_fsf_req_create_sbal_check)
				(lock_flags, req_queue, 1)));
                if (!condition) {
                        retval = -EIO;
                        goto failed_sbals;
                }
        } else	{
                if (!zfcp_fsf_req_create_sbal_check(lock_flags, req_queue, 1)) {
                        retval = -EIO;
                        goto failed_sbals;
                }
        }
	fsf_req->sbal_count = 1;
	fsf_req->sbal_index = req_queue->free_index;

	ZFCP_LOG_TRACE(
		"got %i free BUFFERs starting at index %i\n",
		fsf_req->sbal_count, fsf_req->sbal_index);
	buffere = req_queue->buffer[fsf_req->sbal_index]->element;
	/* setup common SBALE fields */
	buffere[0].addr = fsf_req;
	buffere[0].flags |= SBAL_FLAGS0_COMMAND;
	if (fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS) {
		buffere[1].addr = (void *)fsf_req->qtcb;
		buffere[1].length = ZFCP_QTCB_SIZE;
	}

	/* set specific common SBALE and QTCB fields */
	switch (fsf_cmd) {
		case FSF_QTCB_FCP_CMND :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_FCP_CMND\n");
			/*
			 * storage-block type depends on actual
			 * SCSI command and is set by calling
			 * routine according to transfer direction
			 * of data buffers associated with SCSI
			 * command
			 */
			break;
		case FSF_QTCB_ABORT_FCP_CMND :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_ABORT_FCP_CMND\n");
		case FSF_QTCB_OPEN_PORT_WITH_DID :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_OPEN_PORT_WITH_DID\n");
		case FSF_QTCB_OPEN_LUN :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_OPEN_LUN\n");
		case FSF_QTCB_CLOSE_LUN :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_CLOSE_LUN\n");
		case FSF_QTCB_CLOSE_PORT :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_CLOSE_PORT\n");
                case FSF_QTCB_CLOSE_PHYSICAL_PORT :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_CLOSE_PHYSICAL_PORT\n");
		case FSF_QTCB_SEND_ELS :	/* FIXME: ELS needs separate case */
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_SEND_ELS\n");
			/*
			 * FIXME(qdio):
			 * what is the correct type for commands
			 * without 'real' data buffers?
			 */
			buffere[0].flags |= SBAL_FLAGS0_TYPE_READ;
			buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY;
			break;
		case FSF_QTCB_EXCHANGE_CONFIG_DATA :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_EXCHANGE_CONFIG_DATA\n");
			buffere[0].flags |= SBAL_FLAGS0_TYPE_READ;
			buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY;
			break;

		case FSF_QTCB_SEND_GENERIC :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_SEND_GENERIC\n");
			buffere[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
			break;

		case FSF_QTCB_UNSOLICITED_STATUS :
                        ZFCP_LOG_FLAGS(3, "FSF_QTCB_UNSOLICITED_STATUS\n");
			buffere[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
                        buffere[2].flags |= SBAL_FLAGS_LAST_ENTRY;
                        break;

		default :
			ZFCP_LOG_NORMAL(
				"bug: An attempt to send an unsupported "
                                "command has been detected. "
                                "(debug info 0x%x)\n",
				fsf_cmd);
			goto unsupported_fsf_cmd;
	}

	/* yes, we did it - skip all cleanups for different failures */
	goto out;

 unsupported_fsf_cmd:
        
 failed_sbals:
#ifdef ZFCP_STAT_QUEUES
        atomic_inc(&zfcp_data.outbound_queue_full);        
#endif
/* dequeue new FSF request previously enqueued */
        zfcp_fsf_req_free(fsf_req);
        fsf_req = NULL;
        
failed_fsf_req:
        //failed_running:
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
        time = get_clock();
#endif
        ZFCP_WRITE_LOCK_IRQSAVE(&req_queue->queue_lock, *lock_flags);
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	zfcp_lock_meter_add(&req_queue->lock_meter, time);
#endif

out: 
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

        ZFCP_LOG_TRACE("enter (0x%lx, 0x%lx)\n", 
                       (unsigned long)req_queue,
                       (unsigned long)fsf_req);

        new_distance_from_int = req_queue->distance_from_int + fsf_req->sbal_count;
        if (new_distance_from_int >= ZFCP_QDIO_PCI_INTERVAL) {
                new_distance_from_int %= ZFCP_QDIO_PCI_INTERVAL;
                pci_pos  = fsf_req->sbal_index;
		pci_pos += fsf_req->sbal_count;
		pci_pos -= new_distance_from_int;
		pci_pos -= 1;
		pci_pos %= QDIO_MAX_BUFFERS_PER_Q;
                req_queue->buffer[pci_pos]->element[0].flags |= SBAL_FLAGS0_PCI;
                ZFCP_LOG_TRACE("Setting PCI flag at pos %d\n",
                               pci_pos);
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
        volatile qdio_buffer_element_t* buffere;
	int inc_seq_no = 1;
        int new_distance_from_int;
	unsigned long flags;

	u8 sbal_index = fsf_req->sbal_index;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx timer=0x%lx)\n",
		(unsigned long)fsf_req,
		(unsigned long)timer);
        
	/* FIXME(debug): remove it later */
        buffere = &(req_queue->buffer[sbal_index]->element[0]);
        ZFCP_LOG_DEBUG("zeroeth BUFFERE flags=0x%x \n ",
                       buffere->flags);
        buffere = &(req_queue->buffer[sbal_index]->element[1]);
	ZFCP_LOG_TRACE("HEX DUMP OF 0eth BUFFERE PAYLOAD:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE, (char *)buffere->addr, buffere->length);
        
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
	} else	inc_seq_no = 0;

        /* put allocated FSF request at list tail */
	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->fsf_req_list_lock, flags);
        list_add_tail(&fsf_req->list,
                      &adapter->fsf_req_list_head);
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->fsf_req_list_lock, flags);

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
		sbal_index,
		fsf_req->sbal_count,
		(unsigned long)&req_queue->buffer[sbal_index]);	

        /*
         * adjust the number of free SBALs in request queue as well as
         * position of first one
         */
        atomic_sub(fsf_req->sbal_count, &req_queue->free_count);
        ZFCP_LOG_TRACE("free_count=%d\n",
                       atomic_read(&req_queue->free_count));
        req_queue->free_index += fsf_req->sbal_count;	/* increase */
        req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;    /* wrap if needed */
        new_distance_from_int = zfcp_qdio_determine_pci(req_queue, fsf_req);
	retval = do_QDIO(
			adapter->irq,
			QDIO_FLAG_SYNC_OUTPUT,
                        0,
			fsf_req->sbal_index,
                        fsf_req->sbal_count,
                        NULL);

	if (retval) {
                /* Queues are down..... */
                retval=-EIO;
		/* FIXME(potential race): timer might be expired (absolutely unlikely) */
		if (timer)
			del_timer_sync(timer);
		ZFCP_WRITE_LOCK_IRQSAVE(&adapter->fsf_req_list_lock, flags);
        	list_del(&fsf_req->list);
		ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->fsf_req_list_lock, flags);
                /*
                 * adjust the number of free SBALs in request queue as well as
                 * position of first one
                 */
                zfcp_zero_sbals(
			req_queue->buffer,
			fsf_req->sbal_index,
			fsf_req->sbal_count);
                atomic_add(fsf_req->sbal_count, &req_queue->free_count);
                req_queue->free_index -= fsf_req->sbal_count;	/* increase */
                req_queue->free_index += QDIO_MAX_BUFFERS_PER_Q; 
                req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;    /* wrap if needed */
		ZFCP_LOG_DEBUG(
			"error: do_QDIO failed. Buffers could not be enqueued "
                        "to request queue.\n");
        } else {
                req_queue->distance_from_int = new_distance_from_int;
#ifdef ZFCP_DEBUG_REQUESTS
                debug_text_event(adapter->req_dbf, 1, "o:a/seq");
                debug_event(adapter->req_dbf, 1, &fsf_req, sizeof(unsigned long));
                if (inc_seq_no) {
                        debug_event(adapter->req_dbf, 1, &adapter->fsf_req_seq_no,
                                    sizeof(u32));
                } else {
                        debug_text_event(adapter->req_dbf, 1, "nocb");
                }
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
		atomic_inc(&zfcp_data.outbound_total);
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
static int zfcp_fsf_req_cleanup(
		zfcp_fsf_req_t *fsf_req)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_FSF
 
	int retval;
        zfcp_adapter_t *adapter = fsf_req->adapter;
	unsigned long flags;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx)\n",
		(unsigned long)fsf_req);

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->fsf_req_list_lock, flags);
        list_del(&fsf_req->list);
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->fsf_req_list_lock, flags);
        retval = zfcp_fsf_req_free(fsf_req);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_create_sbals_from_sg
 *
 * purpose:	walks through scatter-gather list of specified SCSI command
 *		and creates a corresponding list of SBALs
 *
 * returns:	size of generated buffer in bytes 
 *
 * context:	
 */
static int zfcp_create_sbals_from_sg(
	zfcp_fsf_req_t *fsf_req,
	Scsi_Cmnd *scpnt,
	char sbtype,		/* storage-block type */
	int length_min,		/* roll back if generated buffer than this */
	int buffer_max)		/* do not use more BUFFERs than this */
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI
 
	int length_total = 0;
	int buffer_index = 0;
	int buffer_last = 0;
        int buffere_index=1; /* elements 0 and 1 are req-id and qtcb */
        volatile qdio_buffer_element_t *buffere=NULL;
	zfcp_qdio_queue_t *req_q = NULL;
	int length_max = scpnt->request_bufflen;

	ZFCP_LOG_TRACE(
		"enter (fsf_req=0x%lx, scpnt=0x%lx, "
		"sbtype=%i, length_min=%i, sbal_max=%i)\n",
		(unsigned long)fsf_req,
		(unsigned long)scpnt,
		sbtype,
		length_min,
		buffer_max);

	req_q = &fsf_req->adapter->request_queue;
        
	buffer_index = req_q->free_index;
	buffer_last = req_q->free_index + 
			min(buffer_max, atomic_read(&req_q->free_count)) - 1;
	buffer_last %= QDIO_MAX_BUFFERS_PER_Q;

	ZFCP_LOG_TRACE("total SCSI data buffer size is (scpnt->request_bufflen) %i\n",
			scpnt->request_bufflen);
	ZFCP_LOG_TRACE("BUFFERs from (buffer_index)%i to (buffer_last)%i available\n",
			buffer_index,
			buffer_last);
        ZFCP_LOG_TRACE("buffer_max=%d, req_q->free_count=%d\n",
                       buffer_max,
                       atomic_read(&req_q->free_count));

	if (scpnt->use_sg) {
		int sg_index;
		struct scatterlist *list
			= (struct scatterlist *) scpnt->request_buffer;

		ZFCP_LOG_DEBUG(
			"%i (scpnt->use_sg) scatter-gather segments\n",
			scpnt->use_sg);
                
                //                length_max+=0x2100;

#ifdef ZFCP_STAT_REQSIZES
		if (sbtype == SBAL_FLAGS0_TYPE_READ)
			zfcp_statistics_inc(&zfcp_data.read_sguse_head, scpnt->use_sg);
		else	zfcp_statistics_inc(&zfcp_data.write_sguse_head, scpnt->use_sg);
#endif

		for (sg_index = 0;
		     sg_index < scpnt->use_sg;
		     sg_index++, list++) {
			if (zfcp_create_sbales_from_segment(
					(unsigned long)list->address,
					list->length,
					&length_total,
					length_min,
					length_max,
					&buffer_index,
                                        &buffere_index,
					req_q->free_index,
					buffer_last,
					req_q->buffer,
					sbtype))
				break;
		}
	} else	{
                ZFCP_LOG_DEBUG("no scatter-gather list\n");
                debug_len=scpnt->request_bufflen;
                debug_addr=(unsigned long)scpnt->request_buffer;

#ifdef ZFCP_STAT_REQSIZES
		if (sbtype == SBAL_FLAGS0_TYPE_READ)
			zfcp_statistics_inc(&zfcp_data.read_sguse_head, 1);
		else	zfcp_statistics_inc(&zfcp_data.write_sguse_head, 1);
#endif

		zfcp_create_sbales_from_segment(
			(unsigned long)scpnt->request_buffer,
			scpnt->request_bufflen,
			&length_total,
			length_min,
			length_max,
                        &buffer_index,
                        &buffere_index,
			req_q->free_index,
                        buffer_last,
			req_q->buffer,
			sbtype);
	}

	fsf_req->sbal_index = req_q->free_index;

        if (buffer_index >= fsf_req->sbal_index) {
                fsf_req->sbal_count = (buffer_index - 
                                           fsf_req->sbal_index) + 1;
        } else {
                fsf_req->sbal_count = 
                        (QDIO_MAX_BUFFERS_PER_Q - fsf_req->sbal_index) +
                        buffer_index + 1;
        }
        /* HACK */
        if ((scpnt->request_bufflen != 0) && (length_total == 0))
		goto out;

#ifdef ZFCP_STAT_REQSIZES
	if (sbtype == SBAL_FLAGS0_TYPE_READ)
		zfcp_statistics_inc(&zfcp_data.read_req_head, length_total);
	else	zfcp_statistics_inc(&zfcp_data.write_req_head, length_total);
#endif

        buffere = &(req_q->buffer[buffer_index]->element[buffere_index]);
        buffere->flags |= SBAL_FLAGS_LAST_ENTRY;
 out:
	ZFCP_LOG_DEBUG(
		"%i BUFFER(s) from %i to %i needed\n",
		fsf_req->sbal_count,
		fsf_req->sbal_index,
		buffer_index);

	ZFCP_LOG_TRACE(
		"total QDIO data buffer size is %i\n",
		length_total);

	ZFCP_LOG_TRACE("exit (%i)\n", length_total);
 
	return length_total;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


/*
 * function:    zfcp_create_sbales_from_segment
 *
 * purpose:	creates SBALEs (if needed in several SBALs)
 *
 * returns:	
 *
 * context:	
 */
static inline int zfcp_create_sbales_from_segment(
	unsigned long addr,	/* begin of this buffer segment */
	int length_seg,		/* length of this buffer segment */
	int *length_total,	/* total length of buffer */
	int length_min,	/* roll back if generated buffer smaller than this */
	int length_max,	/* sum of all SBALEs (count) not larger than this */
	int *buffer_index,	/* position of current BUFFER */
        int *buffere_index,     /* position of current BUFFERE */
	int buffer_first,	/* first BUFFER used for this buffer */
	int buffer_last,	/* last BUFFER in request queue allowed */ 
	qdio_buffer_t *buffer[], /* begin of SBAL array of request queue */
	char sbtype)		/* storage-block type */
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI
 
	int retval = 0;
	int length = 0;

	ZFCP_LOG_TRACE(
		"enter (addr=0x%lx, length_seg=%i, length_total=%i, "
		"length_min=%i, length_max=%i, "
		"buffer_index=%i, buffere_index=%i, buffer_first=%i, "
		"buffer_last=%i, buffer=0x%lx, sbtype=%i)\n",
		addr, length_seg, *length_total, length_min, length_max,
		*buffer_index, *buffere_index, buffer_first, buffer_last,
		(unsigned long)buffer, sbtype);

	ZFCP_LOG_TRACE(
		"SCSI data buffer segment with %i bytes from 0x%lx to 0x%lx\n",
		length_seg,
		addr,
		(addr + length_seg) - 1);

	if (!length_seg)
		goto out;

	if (addr & (PAGE_SIZE - 1)) {
		length = min((int)(PAGE_SIZE - (addr & (PAGE_SIZE - 1))), length_seg);
                ZFCP_LOG_TRACE("address 0x%lx not on page boundary, length=0x%x\n",
                               (unsigned long)addr,
                               length); 
		retval = zfcp_create_sbale(
				addr,
				length,
				length_total,
				length_min,
				length_max,
				buffer_index,
				buffer_first,
				buffer_last,
				buffere_index,
				buffer,
				sbtype);
		if (retval) {
			/* no resources */
			goto out;
		}
		addr += length;
		length = length_seg - length;
	} else	length = length_seg;

        while(length > 0) {
		retval = zfcp_create_sbale(
				addr,
				min((int)PAGE_SIZE, length),
				length_total,
				length_min,
				length_max,
				buffer_index,
				buffer_first,
				buffer_last,
				buffere_index,
				buffer,
				sbtype);
                if (*buffere_index > ZFCP_LAST_SBALE_PER_SBAL) 
                        ZFCP_LOG_NORMAL("bug: Filling output buffers with SCSI "
                                       "data failed. Index ran out of bounds. "
                                       "(debug info %d)\n",
                                       *buffere_index);
		if (retval) {
			/* no resources */
			goto out;
		}
                length -= PAGE_SIZE; 
                addr += PAGE_SIZE;
	}

out:
	ZFCP_LOG_TRACE("exit (%i)\n", retval);
 
	return retval;
 
#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

/*
 * function:    zfcp_create_sbale
 *
 * purpose:	creates a single SBALE
 *
 * returns:	0 - SBALE added (if needed new SBAL as well)
 *		!0 - failed, missing resources
 *
 * context:	
 */
static inline int zfcp_create_sbale(
	unsigned long addr,	/* begin of this buffer segment */
	int length,		/* length of this buffer segment */
	int *length_total,	/* total length of buffer */
	int length_min,	/* roll back if generated buffer smaller than this */
	int length_max,	/* sum of all SBALEs (count) not larger than this */
	int *buffer_index,	/* position of current BUFFER */
	int buffer_first,	/* first BUFFER used for this buffer */
	int buffer_last,	/* last BUFFER allowed for this buffer */
	int *buffere_index,	/* position of current BUFFERE of current BUFFER */
	qdio_buffer_t *buffer[],	/* begin of SBAL array of request queue */
	char sbtype)		/* storage-block type  */
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_SCSI
 
	int retval = 0;
	int length_real, residual;
        int buffers_used;

	volatile qdio_buffer_element_t *buffere = 
		&(buffer[*buffer_index]->element[*buffere_index]);
 
	ZFCP_LOG_TRACE(
		"enter (addr(of scsi data)=0x%lx, length=%i, "
		"length_total=%i, length_min=%i, length_max=%i, "
		"buffer_index=%i, buffer_first=%i, buffer_last=%i, "
		"buffere_index=%i, buffer=0x%lx, sbtype=%i)\n",
		addr, length, *length_total, length_min, length_max, 
		*buffer_index, buffer_first, buffer_last,
		*buffere_index, (unsigned long)buffer, sbtype);

	/* check whether we hit the limit */
	residual = length_max - *length_total;
	if (residual == 0) {
		ZFCP_LOG_TRACE(
			"skip remaining %i bytes since length_max hit\n",
			length);
		goto out;
	}
	length_real = min(length, residual);

	/*
	 * figure out next BUFFERE
	 * (first BUFFERE of first BUFFER is skipped - 
	 * this is ok since it is reserved for the QTCB)
	 */
	if (*buffere_index == ZFCP_LAST_SBALE_PER_SBAL) {
		/* last BUFFERE in this BUFFER */
		buffere->flags |= SBAL_FLAGS_LAST_ENTRY;
		/* need further BUFFER */
		if (*buffer_index == buffer_last) {
			/* queue full or last allowed BUFFER*/
                        buffers_used = (buffer_last - buffer_first) + 1;
			/* avoid modulo operation on negative value */
			buffers_used += QDIO_MAX_BUFFERS_PER_Q;
			buffers_used %= QDIO_MAX_BUFFERS_PER_Q;
			ZFCP_LOG_DEBUG(
				"reached limit of number of BUFFERs "
				"allowed for this request\n");
                        /* FIXME (design) - This check is wrong and enforces the
                         * use of one SBALE less than possible 
                         */
			if ((*length_total < length_min)   
                            || (buffers_used < ZFCP_MAX_SBALS_PER_REQ)) {
				ZFCP_LOG_DEBUG("Rolling back SCSI command as "
                                               "there are insufficient buffers "
                                               "to cover the minimum required "
                                               "amount of data\n");
				/*
				 * roll back complete list of BUFFERs generated
				 * from the scatter-gather list associated
				 * with this SCSI command
				 */
				zfcp_zero_sbals(
					buffer,
					buffer_first,
					buffers_used);
				*length_total = 0;
			} else	{
                                /* DEBUG */
				ZFCP_LOG_NORMAL(
					"Not enough buffers available. "
                                        "Can only transfer %i bytes of data\n",
					*length_total);
			}
			retval = -ENOMEM;
			goto out;
		} else	{ /* *buffer_index != buffer_last */
			/* chain BUFFERs */
			*buffere_index = 0;
			buffere = &(buffer[*buffer_index]->element[*buffere_index]);
			buffere->flags |= SBAL_FLAGS0_MORE_SBALS;
                        (*buffer_index)++;
                        *buffer_index %= QDIO_MAX_BUFFERS_PER_Q;
			buffere = &(buffer[*buffer_index]->element[*buffere_index]);
                        buffere->flags |= sbtype;
			ZFCP_LOG_DEBUG(
                                "Chaining previous BUFFER %i to BUFFER %i\n",
                                ((*buffer_index !=0) ? *buffer_index-1 : QDIO_MAX_BUFFERS_PER_Q-1), 
                                *buffer_index);
		}
	} else	{ /* *buffere_index != (QDIO_MAX_ELEMENTS_PER_BUFFER - 1) */
		(*buffere_index)++;
		buffere = &(buffer[*buffer_index]->element[*buffere_index]);
	}

	/* ok, found a place for this piece, put it there */
	buffere->addr = (void *)addr;
	buffere->length = length_real;

#ifdef ZFCP_STAT_REQSIZES
	if (sbtype == SBAL_FLAGS0_TYPE_READ)
		zfcp_statistics_inc(&zfcp_data.read_sg_head, length_real);
	else	zfcp_statistics_inc(&zfcp_data.write_sg_head, length_real);
#endif

	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE, (char*)addr, length_real);
	ZFCP_LOG_TRACE(
		"BUFFER no %i (0x%lx) BUFFERE no %i (0x%lx): BUFFERE data addr 0x%lx, "
		"BUFFERE length %i, BUFFER type %i\n",
		*buffer_index,
		(unsigned long)&buffer[*buffer_index],
		*buffere_index,
		(unsigned long)buffere,
		addr,
		length_real,
		sbtype);

	*length_total += length_real;

out:
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
		ZFCP_PARSE_CHECK(count < min, "syntax error: missing \"%c\" or equivalent character", *characters) \
		ZFCP_PARSE_CHECK(count > max, "syntax error: extranous \"%c\" or equivalent character", *characters) \
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
static int zfcp_config_parse_record_add(zfcp_config_record_t *rec)
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
	ZFCP_WRITE_LOCK_IRQSAVE(&zfcp_data.adapter_list_lock, flags);
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
	ZFCP_WRITE_LOCK(&adapter->port_list_lock);
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
	ZFCP_WRITE_LOCK(&port->unit_list_lock);
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
	ZFCP_WRITE_UNLOCK(&port->unit_list_lock);
unlock_port:
	ZFCP_WRITE_UNLOCK(&adapter->port_list_lock);
unlock_adapter:
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&zfcp_data.adapter_list_lock, flags);

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

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
        retval = zfcp_erp_adapter_reopen_internal(adapter, clear_mask);
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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
        zfcp_adapter_t *adapter=port->adapter;

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
		port->adapter->devno);

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG(
			"skipped forced reopen on the failed port "
			"with WWPN 0x%016Lx on the adapter with devno 0x%04x\n",
			(llui_t)port->wwpn,
			port->adapter->devno);
                debug_text_event(adapter->erp_dbf,5,"pf_ro_f");
                debug_event(adapter->erp_dbf,5,&port->wwpn,
                            sizeof(wwn_t));
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_PORT_FORCED,
			port->adapter,
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

	ZFCP_PARANOIA {
	        if (!adapter) {
			ZFCP_LOG_DEBUG("bug: No adapter specified (null pointer)\n");
			retval = -EINVAL;
                	goto out;
		}
        } 

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_forced_reopen_internal(port, clear_mask);
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
out:
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
		port->adapter->devno);

	zfcp_erp_port_block(port, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status)) {
		ZFCP_LOG_DEBUG(
			"skipped reopen on the failed port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)port->wwpn,
			port->adapter->devno);
                debug_text_event(adapter->erp_dbf, 5, "p_ro_f");
                debug_event(adapter->erp_dbf, 5, &port->wwpn, sizeof(wwn_t));
		/* ensure propagation of failed status to new devices */
		zfcp_erp_port_failed(port);
		retval = -EIO;
		goto out;
	}

	retval = zfcp_erp_action_enqueue(
			ZFCP_ERP_ACTION_REOPEN_PORT,
			port->adapter,
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

	ZFCP_PARANOIA {
	        if (!adapter) {
			ZFCP_LOG_DEBUG("bug: No adapter specified (null pointer)\n");
			retval = -EINVAL;
                	goto out;
		}
        } 

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_reopen_internal(port, clear_mask);
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
out:
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
		unit->port->adapter->devno);

	zfcp_erp_unit_block(unit, clear_mask);

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)) {
		ZFCP_LOG_DEBUG(
			"skipped reopen on the failed unit with FCP_LUN 0x%016Lx on the "
			"port with WWPN 0x%016Lx "
			"on the adapter with devno 0x%04x\n",
			(llui_t)unit->fcp_lun,
			(llui_t)unit->port->wwpn,
			unit->port->adapter->devno);
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
        zfcp_adapter_t *adapter;

	ZFCP_LOG_TRACE(
		"enter (unit=0x%lx clear_mask=0x%x)\n",
		(unsigned long)unit,
		clear_mask);

	ZFCP_PARANOIA {
	        if (!unit->port) {
        	        ZFCP_LOG_DEBUG("bug: No port specified (null pointer)\n");
			retval = -EINVAL;
                	goto out;
	        }
		if (!unit->port->adapter) {
        	        ZFCP_LOG_DEBUG("bug: No adapter specified (null pointer)\n");
			retval = -EINVAL;
                	goto out;
	        }
	}
        adapter = unit->port->adapter;

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
        retval = zfcp_erp_unit_reopen_internal(unit, clear_mask);
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
out:
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
	ZFCP_UP(&adapter->erp_ready_sem);

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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
	erp_action = fsf_req->erp_action;
	if (erp_action) {
		debug_text_event(adapter->erp_dbf, 5, "a_frh_norm");
		debug_event(adapter->erp_dbf, 2, &erp_action->action, sizeof(int));
		/* don't care for timer status - timer routine is prepared */
		del_timer(&erp_action->timer);
		zfcp_erp_fsf_req_decouple(erp_action);
		zfcp_erp_action_ready(erp_action);
	} else	/* timeout or dismiss ran - nothing to do */
		debug_text_event(adapter->erp_dbf, 3, "a_frh_tfin");
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
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
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
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
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);
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
	ZFCP_UP(&adapter->erp_ready_sem);
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
		ZFCP_DOWN_INTERRUPTIBLE(&adapter->erp_ready_sem);
#ifdef ZFCP_ERP_DEBUG_SINGLE_STEP
		ZFCP_DOWN(&adapter->erp_continue_sem);
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
			ZFCP_PARANOIA {
				ZFCP_READ_LOCK_IRQSAVE(&adapter->erp_lock, flags);
				retval = !list_empty(&adapter->erp_ready_head) ||
					 !list_empty(&adapter->erp_running_head);
				ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);
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
			} else	break;
		}

		/* paranoia check */
		ZFCP_PARANOIA {
			/* there should be something in 'ready' queue */
			/*
			 * need lock since list_empty checks for entry at
			 * lists head while lists head is subject to
			 * modification when another action is put to this
			 * queue (only list tail won't be modified then)
			 */
			ZFCP_READ_LOCK_IRQSAVE(&adapter->erp_lock, flags);
			retval = list_empty(&adapter->erp_ready_head);
			ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);
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
	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
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
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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
	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);

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
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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
		"enter (unit=0x%lx\n result=%d\n",
		(unsigned long)unit,
		result);

	debug_text_event(unit->port->adapter->erp_dbf, 5, "u_stct");
	debug_event(unit->port->adapter->erp_dbf, 5, &unit->fcp_lun, sizeof(fcp_lun_t));

	if (result == ZFCP_ERP_SUCCEEDED) {
		atomic_set(&unit->erp_counter, 0);
		zfcp_erp_unit_unblock(unit);
	} else	{
                /* ZFCP_ERP_FAILED or ZFCP_ERP_EXIT */
		atomic_inc(&unit->erp_counter);
		if (atomic_read(&unit->erp_counter) > ZFCP_MAX_ERPS) {
			zfcp_erp_unit_failed(unit);
			result = ZFCP_ERP_EXIT;
		}
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

	if (result == ZFCP_ERP_SUCCEEDED) {
		atomic_set(&port->erp_counter, 0);
		zfcp_erp_port_unblock(port);
	} else	{
                /* ZFCP_ERP_FAILED or ZFCP_ERP_EXIT */
		atomic_inc(&port->erp_counter);
		if (atomic_read(&port->erp_counter) > ZFCP_MAX_ERPS) {
			zfcp_erp_port_failed(port);
			result = ZFCP_ERP_EXIT;
		}
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
		"enter (adapter=0x%lx\n result=%d\n",
		(unsigned long)adapter,
		result);

	debug_text_event(adapter->erp_dbf, 5, "a_stct");

	if (result == ZFCP_ERP_SUCCEEDED) {
		atomic_set(&adapter->erp_counter, 0);
		zfcp_erp_adapter_unblock(adapter);
	} else	{
                /* ZFCP_ERP_FAILED or ZFCP_ERP_EXIT */
		atomic_inc(&adapter->erp_counter);
		if (atomic_read(&adapter->erp_counter) > ZFCP_MAX_ERPS) {
			zfcp_erp_adapter_failed(adapter);
			result = ZFCP_ERP_EXIT;
		}
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


/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_erp_strategy_check_queues(zfcp_adapter_t *adapter)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_ERP
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_ERP

	int retval = 0;
	unsigned long flags;
	zfcp_port_t *nport = adapter->nameserver_port;

	ZFCP_LOG_TRACE("enter\n");

	ZFCP_READ_LOCK_IRQSAVE(&adapter->erp_lock, flags);
	if (list_empty(&adapter->erp_ready_head) &&
	    list_empty(&adapter->erp_running_head)) {
		if (nport && atomic_test_mask(ZFCP_STATUS_COMMON_OPEN, &nport->status)) {
			debug_text_event(adapter->erp_dbf, 4, "a_cq_nspsd");
                        /* taking down nameserver port */
                        zfcp_erp_port_reopen_internal(
				nport,
				ZFCP_STATUS_COMMON_RUNNING |
				ZFCP_STATUS_COMMON_ERP_FAILED);
		} else	{
			debug_text_event(adapter->erp_dbf, 4, "a_cq_wake");
			atomic_clear_mask(
				ZFCP_STATUS_ADAPTER_ERP_PENDING,
				&adapter->status);
			wake_up(&adapter->erp_done_wqh);
		}
	} else	debug_text_event(adapter->erp_dbf, 5, "a_cq_notempty");
	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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
static int zfcp_erp_wait(zfcp_adapter_t *adapter)
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
	ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
                zfcp_erp_modify_port_status(port,
                                            common_mask,
                                            set_or_clear);
        }
	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);
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
	ZFCP_READ_LOCK_IRQSAVE(&port->unit_list_lock, flags);
	ZFCP_FOR_EACH_UNIT(port, unit) {
                zfcp_erp_modify_unit_status(unit,
                                            common_mask,
                                            set_or_clear);
        }
	ZFCP_READ_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);
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

	ZFCP_PARANOIA {
	        if (!adapter) {
			ZFCP_LOG_DEBUG("bug: No adapter specified (null pointer)\n");
			retval = -EINVAL;
                	goto out;
		}
        } 

        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
        retval = zfcp_erp_port_reopen_all_internal(adapter, clear_mask);
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);
        
out:
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

	ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
	ZFCP_FOR_EACH_PORT(adapter, port) {
                if (!atomic_test_mask(ZFCP_STATUS_PORT_NAMESERVER, &port->status)) 
			zfcp_erp_port_reopen_internal(port, clear_mask);
        }
	ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);

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

	ZFCP_READ_LOCK_IRQSAVE(&port->unit_list_lock, flags);
	ZFCP_FOR_EACH_UNIT(port, unit)
		zfcp_erp_unit_reopen_internal(unit, clear_mask);
	ZFCP_READ_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);

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
	int retval_cleanup = 0;
	//unsigned long timeout = 300 * HZ;
	
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
	/* DEBUG */
	//__ZFCP_WAIT_EVENT_TIMEOUT(timeout, 0);
	/* cleanup queues previously established */
	retval_cleanup = qdio_cleanup(adapter->irq, QDIO_FLAG_CLEANUP_USING_CLEAR);
	if (retval_cleanup) {
		ZFCP_LOG_NORMAL(
			"bug: Could not clean QDIO (data transfer mechanism) "
                        "queues. (debug info %i).\n",
			retval_cleanup);
	}
#ifdef ZFCP_DEBUG_REQUESTS
          else  debug_text_event(adapter->req_dbf, 1, "q_clean");
#endif /* ZFCP_DEBUG_REQUESTS */

	/*
	 * First we had to stop QDIO operation.
	 * Now it is safe to take the following actions.
	 */

failed_qdio_initialize:
	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);

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
        int first_used;
        int used_count;
	zfcp_adapter_t *adapter = erp_action->adapter;
#if 0
        unsigned long flags;
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
        unsigned long long time;
#endif
#endif

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

	/* cleanup queues previously established */

	/*
	 * MUST NOT LOCK - qdio_cleanup might call schedule
	 * FIXME: need another way to make cleanup safe
	 */
        /* Note:
         * We need the request_queue lock here, otherwise there exists the 
         * following race:
         * 
         * queuecommand calls create_fcp_commmand_task...calls req_create, 
         * gets sbal x to x+y - meanwhile adapter reopen is called, completes 
         * - req_send calls do_QDIO for sbal x to x+y, i.e. wrong indices.
         *
         * with lock:
         * queuecommand calls create_fcp_commmand_task...calls req_create, 
         * gets sbal x to x+y - meanwhile adapter reopen is called, waits 
         * - req_send calls do_QDIO for sbal x to x+y, i.e. wrong indices 
         * but do_QDIO fails as adapter_reopen is still waiting for the lock
         * OR
         * queuecommand calls create_fcp_commmand_task...calls req_create 
         * - meanwhile adapter reopen is called...completes,
         * - gets sbal 0 to 0+y, - req_send calls do_QDIO for sbal 0 to 0+y, 
         * i.e. correct indices...though an fcp command is called before 
         * exchange config data...that should be fine, however
         */
#if 0
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	time = get_clock();
#endif
        ZFCP_WRITE_LOCK_IRQSAVE(&adapter->request_queue.queue_lock, flags);
#ifdef ZFCP_STAT_REQ_QUEUE_LOCK
	zfcp_lock_meter_add(&adapter->request_queue.lock_meter, time);
#endif
#endif	//0

	if (qdio_cleanup(adapter->irq, QDIO_FLAG_CLEANUP_USING_CLEAR) != 0) {
		/*
		 * FIXME(design):
		 * What went wrong? What to do best? Proper retval?
		 */
		ZFCP_LOG_NORMAL(
			"error: Clean-up of QDIO (data transfer mechanism) "
                        "structures failed for adapter with devno 0x%04x.\n",
			adapter->devno);
	} else	{
		ZFCP_LOG_DEBUG("queues cleaned up\n");
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 1, "q_clean");
#endif /* ZFCP_DEBUG_REQUESTS */
	}

	/*
	 * First we had to stop QDIO operation.
	 * Now it is safe to take the following actions.
	 */
        
        /* Cleanup only necessary when there are unacknowledged buffers */
        if (atomic_read(&adapter->request_queue.free_count)
				< QDIO_MAX_BUFFERS_PER_Q){
                first_used = (adapter->request_queue.free_index +
                              atomic_read(&adapter->request_queue.free_count)) 
				% QDIO_MAX_BUFFERS_PER_Q;
                used_count = QDIO_MAX_BUFFERS_PER_Q -
				atomic_read(&adapter->request_queue.free_count);
                zfcp_zero_sbals(adapter->request_queue.buffer, 
                                first_used,
                                used_count);
        }
        adapter->response_queue.free_index = 0;
        atomic_set(&adapter->response_queue.free_count, 0);
        adapter->request_queue.free_index = 0;
        atomic_set(&adapter->request_queue.free_count, 0);
        adapter->request_queue.distance_from_int = 0;

	atomic_clear_mask(ZFCP_STATUS_ADAPTER_QDIOUP, &adapter->status);

#if 0
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->request_queue.queue_lock, flags);
#endif
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
		ZFCP_DOWN_INTERRUPTIBLE(&adapter->erp_ready_sem);
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


int nomem = 2;
#define ZFCP_ERP_NOMEM_DEBUG(retval, call) \
	do { \
		if (nomem > 0) { \
			nomem--; \
			retval = -ENOMEM; \
		} else retval = call; \
	} while (0);

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
				// ZFCP_ERP_NOMEM_DEBUG(retval, zfcp_nameserver_enqueue(adapter))
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

	ZFCP_WRITE_LOCK_IRQSAVE(&adapter->erp_lock, flags);
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
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&adapter->erp_lock, flags);

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
	retval = zfcp_nameserver_request(erp_action);
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
	ZFCP_UP(&adapter->erp_ready_sem);
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
		ZFCP_READ_LOCK_IRQSAVE(&adapter->port_list_lock, flags);
		ZFCP_FOR_EACH_PORT(adapter, port) {
			zfcp_erp_action_dismiss_port(port);
		}
		ZFCP_READ_UNLOCK_IRQRESTORE(&adapter->port_list_lock, flags);	
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
		ZFCP_READ_LOCK_IRQSAVE(&port->unit_list_lock, flags);
		ZFCP_FOR_EACH_UNIT(port, unit) {
                        if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &unit->status)) {
                                zfcp_erp_action_dismiss(&unit->erp_action);
                        }
		}
		ZFCP_READ_UNLOCK_IRQRESTORE(&port->unit_list_lock, flags);	
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

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static int zfcp_statistics_clear(struct list_head *head)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	unsigned long flags;
	struct list_head *entry, *next_entry;
	zfcp_statistics_t *stat;

	ZFCP_LOG_TRACE("enter\n");

	ZFCP_WRITE_LOCK_IRQSAVE(&zfcp_data.stat_lock, flags);
	list_for_each_safe(entry, next_entry, head) {
		stat = list_entry(entry, zfcp_statistics_t, list);
		list_del(entry);
		kfree(stat);
	}
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&zfcp_data.stat_lock, flags);

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
static int zfcp_statistics_new(
		struct list_head *head,
		u32 num)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;
	zfcp_statistics_t *stat;

	ZFCP_LOG_TRACE("enter\n");

	stat = ZFCP_KMALLOC(sizeof(zfcp_statistics_t), GFP_ATOMIC);
	if (stat) {
		stat->num = num;
		stat->occurrence = 1;
		list_add_tail(&stat->list, head);
	} else	zfcp_data.stat_errors++;

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_statistics_inc(
                struct list_head *head,
		u32 num)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_OTHER

        int retval = 0;
        unsigned long flags;
        zfcp_statistics_t *stat;
        struct list_head *entry;

        ZFCP_LOG_TRACE("enter\n");

        ZFCP_WRITE_LOCK_IRQSAVE(&zfcp_data.stat_lock, flags);
        list_for_each(entry, head) {
                stat = list_entry(entry, zfcp_statistics_t, list);
                if (stat->num == num) {
                        stat->occurrence++;
                        goto unlock;
                }
        }
        /* occurrence must be initialized to 1 */
        zfcp_statistics_new(head, num);
unlock:
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&zfcp_data.stat_lock, flags);

        ZFCP_LOG_TRACE("exit (%i)\n", retval);

        return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}


static int zfcp_statistics_print(
                struct list_head *head,
		char *prefix,
		char *buf,
		int len,
		int max)
{
#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX            ZFCP_LOG_AREA_PREFIX_OTHER

        unsigned long flags;
        zfcp_statistics_t *stat;
        struct list_head *entry;

        ZFCP_LOG_TRACE("enter\n");

        ZFCP_WRITE_LOCK_IRQSAVE(&zfcp_data.stat_lock, flags);
	list_for_each(entry, head) {
		if (len > max - 26)
			break;
		stat = list_entry(entry, zfcp_statistics_t, list);
		len += sprintf(buf + len, "%s 0x%08x: 0x%08x\n", prefix, stat->num, stat->occurrence);
        }
        ZFCP_WRITE_UNLOCK_IRQRESTORE(&zfcp_data.stat_lock, flags);

        ZFCP_LOG_TRACE("exit (%i)\n", len);

        return len;

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
static int zfcp_statistics_init_all(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

	rwlock_init(&zfcp_data.stat_lock);
	INIT_LIST_HEAD(&zfcp_data.read_req_head);
	INIT_LIST_HEAD(&zfcp_data.write_req_head);
	INIT_LIST_HEAD(&zfcp_data.read_sg_head);
	INIT_LIST_HEAD(&zfcp_data.write_sg_head);
	INIT_LIST_HEAD(&zfcp_data.read_sguse_head);
	INIT_LIST_HEAD(&zfcp_data.write_sguse_head);

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
static int zfcp_statistics_clear_all(void)
{
#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_OTHER

	int retval = 0;

	ZFCP_LOG_TRACE("enter\n");

	zfcp_statistics_clear(&zfcp_data.read_req_head);
	zfcp_statistics_clear(&zfcp_data.write_req_head);
	zfcp_statistics_clear(&zfcp_data.read_sg_head);
	zfcp_statistics_clear(&zfcp_data.write_sg_head);
	zfcp_statistics_clear(&zfcp_data.read_sguse_head);
	zfcp_statistics_clear(&zfcp_data.write_sguse_head);

	ZFCP_LOG_TRACE("exit (%i)\n", retval);

	return retval;

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
}

#endif // ZFCP_STAT_REQSIZES


#ifdef ZFCP_STAT_REQ_QUEUE_LOCK

static inline unsigned long long zfcp_lock_meter_init(
		zfcp_lock_meter_t *meter)
{
	unsigned long flags;

	rwlock_init(&meter->lock);
	ZFCP_WRITE_LOCK_IRQSAVE(&meter->lock, flags);
	meter->time = 0;
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&meter->lock, flags);

	return 0;
}

static inline unsigned long long zfcp_lock_meter_add(
		zfcp_lock_meter_t *meter,
		unsigned long long time)
{
	unsigned long flags;

	time = get_clock() - time;
	ZFCP_WRITE_LOCK_IRQSAVE(&meter->lock, flags);
	meter->time += time;
	ZFCP_WRITE_UNLOCK_IRQRESTORE(&meter->lock, flags);

	return time;
}

extern void tod_to_timeval(uint64_t todval, struct timeval *xtime);

/* According to Martin Schwidefsky this is not not recommended */
#if 0
static inline unsigned long long zfcp_adjust_tod(
		unsigned long long time)
{
	time -= 0x8126d60e46000000LL - (0x3c26700LL * 1000000 * 4096);
	return time;
}

static inline int zfcp_lock_meter_print(
		zfcp_lock_meter_t *meter,
		unsigned long long *time_ptr,
		char *buf)
{
	unsigned long flags;
	unsigned long long time;
	struct timeval tval;
	int len;

	ZFCP_READ_LOCK_IRQSAVE(&meter->lock, flags);
	time = *time_ptr;
	ZFCP_READ_UNLOCK_IRQRESTORE(&meter->lock, flags);
	time = zfcp_adjust_tod(time);
	tod_to_timeval(time, &tval);
	len = sprintf(buf, "%011lu:%06lu", tval.tv_sec, tval.tv_usec);

	return len;
}
#endif

static inline int zfcp_lock_meter_print_tod(
		char *buf)
{
	return sprintf(buf, "%Lu", get_clock());
}

static inline int zfcp_lock_meter_print_time(
		zfcp_lock_meter_t *meter,
		char *buf)
{
	unsigned long flags;
	unsigned long long time;
	int len;

	ZFCP_READ_LOCK_IRQSAVE(&meter->lock, flags);
	time = meter->time;
	ZFCP_READ_UNLOCK_IRQRESTORE(&meter->lock, flags);
	len = sprintf(buf, "%Lu", time);

	return len;
}

#endif // ZFCP_STAT_REQ_QUEUE_LOCK



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
