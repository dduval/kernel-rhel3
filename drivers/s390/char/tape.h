/*
 *  drivers/s390/char/tape.h
 *    tape device driver for 3480/3490E/3590 tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *		 Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 */

#ifndef _TAPE_H
#define _TAPE_H

#include <linux/config.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/mtio.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <asm/debug.h>
#include <asm/idals.h>
#include <asm/s390dyn.h>
#ifdef CONFIG_DEVFS_FS
#include <linux/devfs_fs_kernel.h>
#endif

/*
 * macros s390 debug feature (dbf)
 */
#define DBF_EVENT(d_level, d_str...) \
do { \
	debug_sprintf_event(tape_dbf_area, d_level, d_str); \
} while (0)

#define DBF_EXCEPTION(d_level, d_str...) \
do { \
	debug_sprintf_exception(tape_dbf_area, d_level, d_str); \
} while (0)

#define TAPE_VERSION_MAJOR 2
#define TAPE_VERSION_MINOR 0
#define TAPE_MAGIC "tape"

#define TAPE_MINORS_PER_DEV 2       /* two minors per device */
#define TAPEBLOCK_HSEC_SIZE	2048
#define TAPEBLOCK_HSEC_S2B	2
#define TAPEBLOCK_RETRIES	5

/* Event types for hotplug */
#define TAPE_HOTPLUG_CHAR_ADD     1
#define TAPE_HOTPLUG_BLOCK_ADD    2
#define TAPE_HOTPLUG_CHAR_REMOVE  3
#define TAPE_HOTPLUG_BLOCK_REMOVE 4

enum tape_medium_state {
	MS_UNKNOWN,
	MS_LOADED,
	MS_UNLOADED,
	MS_SIZE
};

enum tape_op {
	TO_BLOCK,	/* Block read */
	TO_BSB,		/* Backward space block */
	TO_BSF,		/* Backward space filemark */
	TO_DSE,		/* Data security erase */
	TO_FSB,		/* Forward space block */
	TO_FSF,		/* Forward space filemark */
	TO_LBL,		/* Locate block label */
	TO_NOP,		/* No operation */
	TO_RBA,		/* Read backward */
	TO_RBI,		/* Read block information */
	TO_RFO,		/* Read forward */
	TO_REW,		/* Rewind tape */
	TO_RUN,		/* Rewind and unload tape */
	TO_WRI,		/* Write block */
	TO_WTM,		/* Write tape mark */
	TO_MSEN,	/* Medium sense */
	TO_LOAD,	/* Load tape */
	TO_READ_CONFIG, /* Read configuration data */
	TO_READ_ATTMSG, /* Read attention message */
	TO_DIS,		/* Tape display */
	TO_ASSIGN,	/* Assign tape to channel path */
	TO_UNASSIGN,	/* Unassign tape from channel path */
	TO_BREAKASS,    /* Break the assignment of another host */
	TO_SIZE		/* #entries in tape_op_t */
};

/* Forward declaration */
struct tape_device;

/* The tape device list lock */
extern rwlock_t	  tape_dev_lock;

/* Tape CCW request */
struct tape_request {
	struct list_head list;		/* list head for request queueing. */
	struct tape_device *device;	/* tape device of this request */
	ccw1_t *cpaddr;			/* address of the channel program. */
	void *cpdata;			/* pointer to ccw data. */
	char status;			/* status of this request */
	int options;			/* options for execution. */
	int retries;			/* retry counter for error recovery. */

	/*
	 * This timer can be used to automatically cancel a request after
	 * some time. Specifically the assign request seems to lockup under
	 * certain circumstances.
	 */
	struct timer_list	timeout;

	enum			tape_op op;
	int			rc;
	atomic_t		ref_count;

	/* Callback for delivering final status. */
	void (*callback)(struct tape_request *, void *);
	void *callback_data;
};

/* tape_request->status can be: */
#define TAPE_REQUEST_INIT	0x00	/* request is ready to be processed */
#define TAPE_REQUEST_QUEUED	0x01	/* request is queued to be processed */
#define TAPE_REQUEST_IN_IO	0x02	/* request is currently in IO */
#define TAPE_REQUEST_DONE	0x03	/* request is completed. */

/* Function type for magnetic tape commands */
typedef int (*tape_mtop_fn)(struct tape_device *, int);

/* Size of the array containing the mtops for a discipline */
#define TAPE_NR_MTOPS (MTMKPART+1)

/* Tape Discipline */
struct tape_discipline {
	struct list_head list;
	struct module *owner;
	unsigned int cu_type;
	int  (*setup_device)(struct tape_device *);
	void (*cleanup_device)(struct tape_device *);
	int (*assign)(struct tape_device *);
	int (*unassign)(struct tape_device *);
	int (*force_unassign)(struct tape_device *);
	int (*irq)(struct tape_device *, struct tape_request *);
	struct tape_request *(*read_block)(struct tape_device *, size_t);
	struct tape_request *(*write_block)(struct tape_device *, size_t);
	void (*process_eov)(struct tape_device*);
	/* Block device stuff. */
	struct tape_request *(*bread)(struct tape_device *, struct request *);
	void (*check_locate)(struct tape_device *, struct tape_request *);
	void (*free_bread)(struct tape_request *);
	/* ioctl function for additional ioctls. */
	int (*ioctl_fn)(struct tape_device *, unsigned int, unsigned long);
	/* Array of tape commands with TAPE_NR_MTOPS entries */
	tape_mtop_fn *mtop_array;
};

/*
 * The discipline irq function either returns an error code (<0) which
 * means that the request has failed with an error or one of the following:
 */
#define TAPE_IO_SUCCESS 0	/* request successful */
#define TAPE_IO_PENDING 1	/* request still running */
#define TAPE_IO_RETRY	2	/* retry to current request */
#define TAPE_IO_STOP	3	/* stop the running request */

/* Char Frontend Data */
struct tape_char_data {
	/* Idal buffer to temporaily store character data */
	struct idal_buffer *	idal_buf;
	/* Block size (in bytes) of the character device (0=auto) */
	int			block_size;
#ifdef CONFIG_DEVFS_FS
	/* tape/<DEVNO>/char subdirectory in devfs */
	devfs_handle_t		devfs_char_dir;
	/* tape/<DEVNO>/char/nonrewinding entry in devfs */
	devfs_handle_t		devfs_nonrewinding;
	/* tape/<DEVNO>/char/rewinding entry in devfs */
	devfs_handle_t		devfs_rewinding;
#endif /* CONFIG_DEVFS_FS */
};

#ifdef CONFIG_S390_TAPE_BLOCK
/* Block Frontend Data */
struct tape_blk_data
{
	/* Block device request queue. */
	request_queue_t		request_queue;
	/* Block frontend tasklet */
	struct tasklet_struct	tasklet;
	/* Current position on the tape. */
	unsigned int		block_position;
	/* The start of the block device image file */
	unsigned int		start_block_id;
#ifdef CONFIG_DEVFS_FS
	/* tape/<DEVNO>/block subdirectory in devfs */
	devfs_handle_t		devfs_block_dir;
	/* tape/<DEVNO>/block/disc entry in devfs */
	devfs_handle_t		devfs_disc;
#endif /* CONFIG_DEVFS_FS */
};
#endif

#define TAPE_STATUS_INIT		0x00000001
#define TAPE_STATUS_ASSIGN_M		0x00000002
#define TAPE_STATUS_ASSIGN_A		0x00000004
#define TAPE_STATUS_OPEN		0x00000008
#define TAPE_STATUS_BLOCKDEV		0x00000010
#define TAPE_STATUS_BOXED		0x20000000
#define TAPE_STATUS_NOACCESS		0x40000000
#define TAPE_STATUS_NOT_OPER		0x80000000

#define TAPE_SET_STATE(td,st) \
	do { \
		tape_state_set(td, td->tape_status | (st)); \
	} while(0)
#define TAPE_CLEAR_STATE(td,st) \
	do { \
		tape_state_set(td, td->tape_status & ~(st)); \
	} while(0)

#define TAPE_UNUSED(td)		(!TAPE_OPEN(td))
#define TAPE_INIT(td)		(td->tape_status & TAPE_STATUS_INIT)
#define TAPE_ASSIGNED(td)	( \
					td->tape_status & ( \
						TAPE_STATUS_ASSIGN_M | \
						TAPE_STATUS_ASSIGN_A \
					) \
				)
#define TAPE_OPEN(td)		(td->tape_status & TAPE_STATUS_OPEN)
#define TAPE_BLOCKDEV(td)	(td->tape_status & TAPE_STATUS_BLOCKDEV)
#define TAPE_BOXED(td)		(td->tape_status & TAPE_STATUS_BOXED)
#define TAPE_NOACCESS(td)	(td->tape_status & TAPE_STATUS_NOACCESS)
#define TAPE_NOT_OPER(td)	(td->tape_status & TAPE_STATUS_NOT_OPER)

/* Tape Info */
struct tape_device {
	/* Device discipline information. */
	struct tape_discipline *discipline;
	void *			discdata;

	/* Generic status bits */
	long			tape_generic_status;
	unsigned int		tape_status;
	enum tape_medium_state	medium_state;

	/* Number of tapemarks required for correct termination */
	int			required_tapemarks;

	/* Waitqueue for state changes and device flags */
	wait_queue_head_t	state_change_wq;
	unsigned char *		modeset_byte;

	/* Reference count. */
	atomic_t		ref_count;

	/* For persistent assign */
	spinlock_t		assign_lock;

	/* Request queue. */
	struct list_head	req_queue;
	atomic_t		bh_scheduled;
	struct tq_struct	bh_task;

	/* Common i/o stuff. */
	s390_dev_info_t		devinfo;
	devstat_t		devstat;

	/* each tape device has two minors */
	int			first_minor;

#ifdef CONFIG_DEVFS_FS
	/* Toplevel devfs directory. */
	devfs_handle_t		devfs_dir;
#endif /* CONFIG_DEVFS_FS */
	/* Character device frontend data */
	struct tape_char_data	char_data;
#ifdef CONFIG_S390_TAPE_BLOCK
	/* Block dev frontend data */
	struct tape_blk_data	blk_data;
#endif
};

/* Externals from tape_core.c */
struct tape_request *tape_alloc_request(int cplength, int datasize);
struct tape_request *tape_put_request(struct tape_request *);
struct tape_request *tape_clone_request(struct tape_request *);
int tape_do_io(struct tape_device *, struct tape_request *);
int tape_do_io_async(struct tape_device *, struct tape_request *);
int tape_do_io_interruptible(struct tape_device *, struct tape_request *);
void tape_schedule_bh(struct tape_device *);
void tape_hotplug_event(struct tape_device *, int, int);

static inline int
tape_do_io_free(struct tape_device *device, struct tape_request *request)
{
	int rc;

	rc = tape_do_io(device, request);
	tape_put_request(request);
	return rc;
}

int tape_oper_handler(int irq, devreg_t *devreg);
void tape_noper_handler(int irq, int status);
int tape_open(struct tape_device *);
int tape_release(struct tape_device *);
int tape_assign(struct tape_device *, int type);
int tape_unassign(struct tape_device *, int type);
int tape_mtop(struct tape_device *, int, int);

/* Externals from tape_devmap.c */
int tape_devmap_init(void);
void tape_devmap_exit(void);

struct tape_device *tape_get_device(int devindex);
struct tape_device *tape_get_device_by_devno(int devno);
struct tape_device *tape_clone_device(struct tape_device *);
void tape_put_device(struct tape_device *);

void tape_auto_detect(void);
void tape_add_devices(struct tape_discipline *);
void tape_remove_devices(struct tape_discipline *);

extern int tape_max_devindex;

/* Externals from tape_char.c */
int tapechar_init(void);
void tapechar_exit(void);
int  tapechar_setup_device(struct tape_device *);
void tapechar_cleanup_device(struct tape_device *);

/* Externals from tape_block.c */
int tapeblock_init (void);
void tapeblock_exit(void);
int tapeblock_setup_device(struct tape_device *);
void tapeblock_cleanup_device(struct tape_device *);
void tapeblock_medium_change(struct tape_device *);

/* Discipline functions */
int tape_register_discipline(struct tape_discipline *);
void tape_unregister_discipline(struct tape_discipline *);
struct tape_discipline *tape_get_discipline(int cu_type);
void tape_put_discipline(struct tape_discipline *);
int tape_enable_device(struct tape_device *, struct tape_discipline *);
void tape_disable_device(struct tape_device *device);

/* tape initialisation functions */
void tape_proc_init (void);
void tape_proc_cleanup (void);

/* a function for dumping device sense info */
void tape_dump_sense(struct tape_device *, struct tape_request *);
void tape_dump_sense_dbf(struct tape_device *, struct tape_request *);

/* functions for handling the status of a device */
inline void tape_state_set (struct tape_device *, unsigned int status);
inline void tape_med_state_set(struct tape_device *, enum tape_medium_state);
const char *tape_state_string(struct tape_device *);

/* Tape 3480/3490 init/exit functions. */
int tape_34xx_init(void);
void tape_34xx_exit(void);

/* The debug area */
extern debug_info_t *tape_dbf_area;

/* functions for building ccws */
static inline ccw1_t *
tape_ccw_cc(ccw1_t *ccw, __u8 cmd_code, __u16 memsize, void *cda)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = memsize;
	ccw->cda = (__u32)(addr_t) cda;
	return ccw + 1;
}

static inline ccw1_t *
tape_ccw_end(ccw1_t *ccw, __u8 cmd_code, __u16 memsize, void *cda)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = 0;
	ccw->count = memsize;
	ccw->cda = (__u32)(addr_t) cda;
	return ccw + 1;
}

static inline ccw1_t *
tape_ccw_cmd(ccw1_t *ccw, __u8 cmd_code)
{
	ccw->cmd_code = cmd_code;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (__u32)(addr_t) &ccw->cmd_code;
	return ccw + 1;
}

static inline ccw1_t *
tape_ccw_repeat(ccw1_t *ccw, __u8 cmd_code, int count)
{
	while (count-- > 0) {
		ccw->cmd_code = cmd_code;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (__u32)(addr_t) &ccw->cmd_code;
		ccw++;
	}
	return ccw;
}

extern inline ccw1_t*
tape_ccw_cc_idal(ccw1_t *ccw, __u8 cmd_code, struct idal_buffer *idal)
{
	ccw->cmd_code = cmd_code;
	ccw->flags    = CCW_FLAG_CC;
	idal_buffer_set_cda(idal, ccw);
	return ccw++;
}

extern inline ccw1_t*
tape_ccw_end_idal(ccw1_t *ccw, __u8 cmd_code, struct idal_buffer *idal)
{
	ccw->cmd_code = cmd_code;
	ccw->flags    = 0;
	idal_buffer_set_cda(idal, ccw);
	return ccw++;
}

/* Global vars */
extern const char *tape_op_verbose[];

#endif /* for ifdef tape.h */
