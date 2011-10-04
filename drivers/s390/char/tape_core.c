/*
 *  drivers/s390/char/tape_core.c
 *    basic function of the tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               Stefan Bader <shbader@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>	     // for kernel parameters
#include <linux/kmod.h>	     // for requesting modules
#include <linux/spinlock.h>  // for locks
#include <linux/vmalloc.h>

#include <asm/types.h>	     // for variable types
#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>

#include "tape.h"
#include "tape_std.h"

#ifdef CONFIG_S390_TAPE_3590
#include "tape_3590.h"
#endif

#define PRINTK_HEADER "T390:"

/*
 * Prototypes for some static functions.
 */
static void __tape_do_irq (int, void *, struct pt_regs *);
static void __tape_remove_request(struct tape_device *, struct tape_request *);
static void tape_timeout_io (unsigned long);

/*
 * List of tape disciplines guarded by tape_discipline_lock.
 */
static struct list_head	tape_disciplines = LIST_HEAD_INIT(tape_disciplines);
static spinlock_t tape_discipline_lock   = SPIN_LOCK_UNLOCKED;

/*
 * Pointer to debug area.
 */
debug_info_t *tape_dbf_area = NULL;

const char *tape_op_verbose[TO_SIZE] =
{
	[TO_BLOCK]		= "BLK",
	[TO_BSB]		= "BSB",
	[TO_BSF]		= "BSF",
	[TO_DSE]		= "DSE",
	[TO_FSB]		= "FSB",
	[TO_FSF]		= "FSF",
	[TO_LBL]		= "LBL",
	[TO_NOP]		= "NOP",
	[TO_RBA]		= "RBA",
	[TO_RBI]		= "RBI",
	[TO_RFO]		= "RFO",
	[TO_REW]		= "REW",
	[TO_RUN]		= "RUN",
	[TO_WRI]		= "WRI",
	[TO_WTM]		= "WTM",
	[TO_MSEN]		= "MSN",
	[TO_LOAD]		= "LOA",
	[TO_READ_CONFIG]	= "RCF",
	[TO_READ_ATTMSG]	= "RAT",
	[TO_DIS]		= "DIS",
	[TO_ASSIGN]		= "ASS",
	[TO_UNASSIGN]		= "UAS",
	[TO_BREAKASS]		= "BRK"
};

/*
 * Inline functions, that have to be defined.
 */

/*
 * I/O helper function. Adds the request to the request queue
 * and starts it if the tape is idle. Has to be called with
 * the device lock held.
 */
static inline int
__do_IO(struct tape_device *device, struct tape_request *request)
{
	int rc = 0;

	if(request->cpaddr == NULL)
		BUG();

	if(request->timeout.expires > 0) {
		/* Init should be done by caller */
		DBF_EVENT(6, "(%04x): starting timed request\n",
			device->devstat.devno);

		request->timeout.function = tape_timeout_io;
		request->timeout.data     = (unsigned long)
			tape_clone_request(request);
		add_timer(&request->timeout);
	}

	rc = do_IO(device->devinfo.irq, request->cpaddr,
		(unsigned long) request, 0x00, request->options);

	return rc;
}

static void
__tape_process_queue(void *data)
{
	struct tape_device *device = (struct tape_device *) data;
	struct list_head *l, *n;
	struct tape_request *request;
	int rc;

	DBF_EVENT(6, "tape_process_queue(%p)\n", device);

	/*
	 * We were told to be quiet. Do nothing for now.
	 */
	if (TAPE_NOACCESS(device)) {
		return;
	}

	/*
	 * Try to start each request on request queue until one is
	 * started successful.
	 */
	list_for_each_safe(l, n, &device->req_queue) {
		request = list_entry(l, struct tape_request, list);

		/* Happens when new request arrive while still doing one. */
		if (request->status == TAPE_REQUEST_IN_IO)
			break;

#ifdef CONFIG_S390_TAPE_BLOCK
		if (request->op == TO_BLOCK)
			device->discipline->check_locate(device, request);
#endif
		switch(request->op) {
			case TO_MSEN:
			case TO_ASSIGN:
			case TO_UNASSIGN:
			case TO_BREAKASS:
				break;
			default:
				if (TAPE_OPEN(device))
					break;
				DBF_EVENT(3,
					"TAPE(%04x): REQ in UNUSED state\n",
					device->devstat.devno);
		}

		rc = __do_IO(device, request);
		if (rc == 0) {
			DBF_EVENT(6, "tape: do_IO success\n");
			request->status = TAPE_REQUEST_IN_IO;
			break;
		}
		/* Start failed. Remove request and indicate failure. */
		DBF_EVENT(1, "tape: DOIO failed with er = %i\n", rc);

		/* Set final status and remove. */
		request->rc = rc;
		__tape_remove_request(device, request);
	}
}

static void
tape_process_queue(void *data)
{
	unsigned long		flags;
	struct tape_device *	device;

	device = (struct tape_device *) data;
	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	atomic_set(&device->bh_scheduled, 0);
	__tape_process_queue(device);
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
}

void
tape_schedule_bh(struct tape_device *device)
{
	/* Protect against rescheduling, when already running. */
	if (atomic_compare_and_swap(0, 1, &device->bh_scheduled))
		return;

	INIT_LIST_HEAD(&device->bh_task.list);
	device->bh_task.sync    = 0;
	device->bh_task.routine = tape_process_queue;
	device->bh_task.data    = device;

	queue_task(&device->bh_task, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	return;
}

/*
 * Stop running ccw. Has to be called with the device lock held.
 */
static inline int
__tape_halt_io(struct tape_device *device, struct tape_request *request)
{
	int retries;
	int rc;

	/* SMB: This should never happen */
	if(request->cpaddr == NULL)
		BUG();

	/* Check if interrupt has already been processed */
	if (request->callback == NULL)
		return 0;

	/* Stop a possibly running timer */
	if(request->timeout.expires) {
		if(del_timer(&request->timeout) > 0) {
			tape_put_request(request);
			request->timeout.data = 0L;
		}
	}

	rc = 0;
	for (retries = 0; retries < 5; retries++) {
		if (retries < 2)
			rc = halt_IO(device->devinfo.irq,
				     (long) request, request->options);
		else
			rc = clear_IO(device->devinfo.irq,
				      (long) request, request->options);
		if (rc == 0)
			break;		/* termination successful */
		if (rc == -ENODEV)
			DBF_EXCEPTION(2, "device gone, retry\n");
		else if (rc == -EIO)
			DBF_EXCEPTION(2, "I/O error, retry\n");
		else if (rc == -EBUSY)
			DBF_EXCEPTION(2, "device busy, retry later\n");
		else
			BUG();
	}
	if (rc == 0)
		request->status = TAPE_REQUEST_DONE;
	return rc;
}

static void
__tape_remove_request(struct tape_device *device, struct tape_request *request)
{
	/* First remove the request from the queue. */
	list_del(&request->list);

	/* This request isn't processed any further. */
	request->status = TAPE_REQUEST_DONE;

	/* Finally, if the callback hasn't been called, do it now. */
	if (request->callback != NULL) {
		request->callback(request, request->callback_data);
		request->callback = NULL;
	}
}

/*
 * Tape state functions
 */
/*
 * Printable strings for tape enumerations.
 */
const char *tape_state_string(struct tape_device *device) {
	char *s = " ???? ";

	if (TAPE_NOT_OPER(device)) {
		s = "NOT_OP";
	} else if (TAPE_NOACCESS(device)) {
		s = "NO_ACC";
	} else if (TAPE_BOXED(device)) {
		s = "BOXED ";
	} else if (TAPE_OPEN(device)) {
		s = "IN_USE";
	} else if (TAPE_ASSIGNED(device)) {
		s = "ASSIGN";
	} else if (TAPE_INIT(device)) {
		s = "INIT  ";
	} else if (TAPE_UNUSED(device)) {
		s = "UNUSED";
	}

	return s;
}

void
tape_state_set(struct tape_device *device, unsigned int status)
{
	const char *str;

	/* Maybe nothing changed. */
	if (device->tape_status == status)
		return;

	DBF_EVENT(4, "ts. dev:  %x\n", device->first_minor);
	str = tape_state_string(device);
	DBF_EVENT(4, "old ts:  0x%08x %s\n", device->tape_status, str);

	device->tape_status = status;

	str = tape_state_string(device);
	DBF_EVENT(4, "new ts:  0x%08x %s\n", status, str);

	wake_up(&device->state_change_wq);
}

void
tape_med_state_set(struct tape_device *device, enum tape_medium_state newstate)
{
	if (device->medium_state == newstate)
		return;

	switch(newstate){
	case MS_UNLOADED:
		device->tape_generic_status |= GMT_DR_OPEN(~0);
		PRINT_INFO("(%04x): Tape is unloaded\n",
			   device->devstat.devno);
		break;
	case MS_LOADED:
		device->tape_generic_status &= ~GMT_DR_OPEN(~0);
		PRINT_INFO("(%04x): Tape has been mounted\n",
			   device->devstat.devno);
		break;
	default:
		// print nothing
		break;
	}
#ifdef CONFIG_S390_TAPE_BLOCK
	tapeblock_medium_change(device);
#endif
	device->medium_state = newstate;
	wake_up(&device->state_change_wq);
}
	
static void
tape_timeout_io(unsigned long data)
{
	struct tape_request	*request;
	struct tape_device	*device;
	unsigned long		 flags;

	request = (struct tape_request *) data;
	device  = request->device;

	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	if(request->callback != NULL) {
		DBF_EVENT(3, "TAPE(%04x): %s timeout\n",
			device->devstat.devno, tape_op_verbose[request->op]);
		PRINT_ERR("TAPE(%04x): %s timeout\n",
			device->devstat.devno, tape_op_verbose[request->op]);

		if(__tape_halt_io(device, request) == 0)
			DBF_EVENT(6, "tape_timeout_io: success\n");
		else {
			DBF_EVENT(2, "tape_timeout_io: halt_io failed\n");
			PRINT_ERR("tape_timeout_io: halt_io failed\n");
		}
		request->rc = -EIO;

		/* Remove from request queue. */
		__tape_remove_request(device, request);

		/* Start next request. */
		if (!list_empty(&device->req_queue))
			tape_schedule_bh(device);
	}
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
	tape_put_request(request);
}

/*
 * DEVFS Functions
 */
#ifdef CONFIG_DEVFS_FS
devfs_handle_t tape_devfs_root_entry;

/*
 * Create devfs root entry (devno in hex) for device td
 */
static int
tape_mkdevfsroot (struct tape_device* device)
{
	char devno [5];

	sprintf(devno, "%04x", device->devinfo.devno);
	device->devfs_dir = devfs_mk_dir(tape_devfs_root_entry, devno, device);
	return (device->devfs_dir == NULL) ? -ENOMEM : 0;
}

/*
 * Remove devfs root entry for a device
 */
static void
tape_rmdevfsroot (struct tape_device *device)
{
	if (device->devfs_dir) {
		devfs_unregister(device->devfs_dir);
		device->devfs_dir = NULL;
	}
}
#endif

/*
 * Enable tape device
 */
int
tape_enable_device(struct tape_device *device,
		   struct tape_discipline *discipline)
{
	int rc;

	if (!TAPE_INIT(device))
		return -EINVAL;

	/* Register IRQ. */
	rc = s390_request_irq_special(device->devinfo.irq, __tape_do_irq,
				      tape_noper_handler, SA_DOPATHGROUP,
				      TAPE_MAGIC, &device->devstat);
	if (rc)
		return rc;

	s390_set_private_data(device->devinfo.irq, tape_clone_device(device));

	device->discipline = discipline;

	/* Let the discipline have a go at the device. */
	rc = discipline->setup_device(device);
	if (rc) {
		s390_set_private_data(device->devinfo.irq, NULL);
		tape_put_device(device);
		free_irq(device->devinfo.irq, &device->devstat);
		return rc;
	}

#ifdef CONFIG_DEVFS_FS
	/* Create devfs entries */
	rc = tape_mkdevfsroot(device);
	if (rc){
		PRINT_WARN ("Cannot create a devfs directory for "
			    "device %04x\n", device->devinfo.devno);
		device->discipline->cleanup_device(device);
		s390_set_private_data(device->devinfo.irq, NULL);
		tape_put_device(device);
		free_irq(device->devinfo.irq, &device->devstat);
		return rc;
	}
#endif
	rc = tapechar_setup_device(device);
	if (rc) {
#ifdef CONFIG_DEVFS_FS
		tape_rmdevfsroot(device);
#endif
		device->discipline->cleanup_device(device);
		s390_set_private_data(device->devinfo.irq, NULL);
		tape_put_device(device);
		free_irq(device->devinfo.irq, &device->devstat);
		return rc;
	}
#ifdef CONFIG_S390_TAPE_BLOCK
	rc = tapeblock_setup_device(device);
	if (rc) {
		tapechar_cleanup_device(device);
#ifdef CONFIG_DEVFS_FS
		tape_rmdevfsroot(device);
#endif
		device->discipline->cleanup_device(device);
		s390_set_private_data(device->devinfo.irq, NULL);
		tape_put_device(device);
		free_irq(device->devinfo.irq, &device->devstat);
		return rc;
	}
#endif

	TAPE_CLEAR_STATE(device, TAPE_STATUS_INIT);

	return 0;
}

/*
 * Disable tape device. Check if there is a running request and
 * terminate it. Post all queued requests with -EIO.
 */
void
tape_disable_device(struct tape_device *device)
{
	struct list_head *l, *n;
	struct tape_request *request;

	spin_lock_irq(get_irq_lock(device->devinfo.irq));
	/* Post remaining requests with -EIO */
	list_for_each_safe(l, n, &device->req_queue) {
		request = list_entry(l, struct tape_request, list);
		if (request->status == TAPE_REQUEST_IN_IO)
			__tape_halt_io(device, request);

		request->rc = -EIO;
		__tape_remove_request(device, request);
	}

	if (TAPE_ASSIGNED(device)) {
		spin_unlock(get_irq_lock(device->devinfo.irq));
		if(
			tape_unassign(
				device,
				TAPE_STATUS_ASSIGN_M|TAPE_STATUS_ASSIGN_A
			) == 0
		) {
			printk(KERN_WARNING "%04x: automatically unassigned\n",
				device->devinfo.devno);
		}
		spin_lock_irq(get_irq_lock(device->devinfo.irq));
	}

	TAPE_SET_STATE(device, TAPE_STATUS_NOT_OPER);
	spin_unlock_irq(get_irq_lock(device->devinfo.irq));

	s390_set_private_data(device->devinfo.irq, NULL);
	tape_put_device(device);

#ifdef CONFIG_S390_TAPE_BLOCK
	tapeblock_cleanup_device(device);
#endif
	tapechar_cleanup_device(device);
#ifdef CONFIG_DEVFS_FS
	tape_rmdevfsroot(device);
#endif
	device->discipline->cleanup_device(device);
	device->discipline = NULL;
	free_irq(device->devinfo.irq, &device->devstat);
}

/*
 * Find discipline by cu_type.
 */
struct tape_discipline *
tape_get_discipline(int cu_type)
{
	struct list_head *l;
	struct tape_discipline *discipline, *tmp;

	discipline = NULL;
	spin_lock(&tape_discipline_lock);
	list_for_each(l, &tape_disciplines) {
		tmp = list_entry(l, struct tape_discipline, list);
		if (tmp->cu_type == cu_type) {
			discipline = tmp;
			break;
		}
	}
	if (discipline->owner != NULL) {
		if (!try_inc_mod_count(discipline->owner))
			/* Discipline is currently unloaded! */
			discipline = NULL;
	}
	spin_unlock(&tape_discipline_lock);
	return discipline;
}

/*
 * Decrement usage count for discipline.
 */
void
tape_put_discipline(struct tape_discipline *discipline)
{
	spin_lock(&tape_discipline_lock);
	if (discipline->owner)
		__MOD_DEC_USE_COUNT(discipline->owner);
	spin_unlock(&tape_discipline_lock);
}

/*
 * Register backend discipline
 */
int
tape_register_discipline(struct tape_discipline *discipline)
{
	if (!try_inc_mod_count(THIS_MODULE))
		/* Tape module is currently unloaded! */
		return -ENOSYS;
	spin_lock(&tape_discipline_lock);
	list_add_tail(&discipline->list, &tape_disciplines);
	spin_unlock(&tape_discipline_lock);
	/* Now add the tape devices with matching cu_type. */
	tape_add_devices(discipline);
	return 0;
}

/*
 * Unregister backend discipline
 */
void
__tape_unregister_discipline(struct tape_discipline *discipline)
{
	list_del(&discipline->list);
	/* Remove tape devices with matching cu_type. */
	tape_remove_devices(discipline);
	MOD_DEC_USE_COUNT;
}

void
tape_unregister_discipline(struct tape_discipline *discipline)
{
	struct list_head *l;

	spin_lock(&tape_discipline_lock);
	list_for_each(l, &tape_disciplines) {
		if (list_entry(l, struct tape_discipline, list) == discipline){
			__tape_unregister_discipline(discipline);
			break;
		}
	}
	spin_unlock(&tape_discipline_lock);
}

/*
 * Allocate a new tape ccw request
 */
struct tape_request *
tape_alloc_request(int cplength, int datasize)
{
	struct tape_request *request;

	if (datasize > PAGE_SIZE || (cplength*sizeof(ccw1_t)) > PAGE_SIZE)
		BUG();

	DBF_EVENT(5, "tape_alloc_request(%d,%d)\n", cplength, datasize);

	request = (struct tape_request *)
		kmalloc(sizeof(struct tape_request), GFP_KERNEL);
	if (request == NULL) {
		DBF_EXCEPTION(1, "cqra nomem\n");
		return ERR_PTR(-ENOMEM);
	}
	memset(request, 0, sizeof(struct tape_request));
	INIT_LIST_HEAD(&request->list);
	atomic_set(&request->ref_count, 1);

	/* allocate channel program */
	if (cplength > 0) {
		request->cpaddr =
			kmalloc(cplength*sizeof(ccw1_t), GFP_ATOMIC | GFP_DMA);
		if (request->cpaddr == NULL) {
			DBF_EXCEPTION(1, "cqra nomem\n");
			kfree(request);
			return ERR_PTR(-ENOMEM);
		}
		memset(request->cpaddr, 0, cplength*sizeof(ccw1_t));
	}
	/* alloc small kernel buffer */
	if (datasize > 0) {
		request->cpdata = kmalloc(datasize, GFP_KERNEL | GFP_DMA);
		if (request->cpdata == NULL) {
			DBF_EXCEPTION(1, "cqra nomem\n");
			if (request->cpaddr != NULL)
				kfree(request->cpaddr);
			kfree(request);
			return ERR_PTR(-ENOMEM);
		}
		memset(request->cpdata, 0, datasize);
	}

	DBF_EVENT(5, "request=%p(%p/%p)\n", request, request->cpaddr,
		request->cpdata);

	return request;
}

/*
 * Free tape ccw request
 */
void
tape_free_request (struct tape_request * request)
{
	DBF_EVENT(5, "tape_free_request(%p)\n", request);

	if (request->device != NULL) {
		tape_put_device(request->device);
		request->device = NULL;
	}
	if (request->cpdata != NULL) {
		kfree(request->cpdata);
	}
	if (request->cpaddr != NULL) {
		kfree(request->cpaddr);
	}
	kfree(request);
}

struct tape_request *
tape_clone_request(struct tape_request *request)
{
	DBF_EVENT(5, "tape_clone_request(%p) = %i\n", request,
		atomic_inc_return(&request->ref_count));
	return request;
}

struct tape_request *
tape_put_request(struct tape_request *request)
{
	int remain;

	DBF_EVENT(4, "tape_put_request(%p)\n", request);
	if((remain = atomic_dec_return(&request->ref_count)) > 0) {
		DBF_EVENT(5, "remaining = %i\n", remain);
	} else {
		tape_free_request(request);
	}

	return NULL;
}

/*
 * Write sense data to console/dbf
 */
void
tape_dump_sense(struct tape_device* device, struct tape_request *request)
{
	devstat_t *stat;
	unsigned int *sptr;

	stat = &device->devstat;
	PRINT_INFO("-------------------------------------------------\n");
	PRINT_INFO("DSTAT : %02x  CSTAT: %02x	CPA: %04x\n",
		   stat->dstat, stat->cstat, stat->cpa);
	PRINT_INFO("DEVICE: %04x\n", device->devinfo.devno);
	if (request != NULL)
		PRINT_INFO("OP	  : %s\n", tape_op_verbose[request->op]);
		
	sptr = (unsigned int *) stat->ii.sense.data;
	PRINT_INFO("Sense data: %08X %08X %08X %08X \n",
		   sptr[0], sptr[1], sptr[2], sptr[3]);
	PRINT_INFO("Sense data: %08X %08X %08X %08X \n",
		   sptr[4], sptr[5], sptr[6], sptr[7]);
	PRINT_INFO("--------------------------------------------------\n");
}

/*
 * Write sense data to dbf
 */
void
tape_dump_sense_dbf(struct tape_device *device, struct tape_request *request)
{
	devstat_t *stat = &device->devstat;
	unsigned int *sptr;
	const char* op;

	if (request != NULL)
		op = tape_op_verbose[request->op];
	else
		op = "---";
	DBF_EVENT(3, "DSTAT : %02x   CSTAT: %02x\n", stat->dstat,stat->cstat);
	DBF_EVENT(3, "DEVICE: %04x OP\t: %s\n", device->devinfo.devno,op);
	sptr = (unsigned int *) stat->ii.sense.data;
	DBF_EVENT(3, "%08x %08x\n", sptr[0], sptr[1]);
	DBF_EVENT(3, "%08x %08x\n", sptr[2], sptr[3]);
	DBF_EVENT(3, "%08x %08x\n", sptr[4], sptr[5]);
	DBF_EVENT(3, "%08x %08x\n", sptr[6], sptr[7]);
}

static inline int
__tape_do_io(struct tape_device *device, struct tape_request *request)
{
	/* Some operations may happen even on an unused tape device */
	switch(request->op) {
		case TO_MSEN:
		case TO_ASSIGN:
		case TO_UNASSIGN:
		case TO_BREAKASS:
			break;
		default:
			if (!TAPE_OPEN(device))
				return -ENODEV;
	}

	/* Add reference to device to the request. This increases the reference
	   count. */
	request->device = tape_clone_device(device);
	request->status = TAPE_REQUEST_QUEUED;

	list_add_tail(&request->list, &device->req_queue);
	__tape_process_queue(device);

	return 0;
}

/*
 * Add the request to the request queue, try to start it if the
 * tape is idle. Return without waiting for end of i/o.
 */
int
tape_do_io_async(struct tape_device *device, struct tape_request *request)
{
	int  rc;
	long flags;

	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	/* Add request to request queue and try to start it. */
	rc = __tape_do_io(device, request);
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
	return rc;
}

/*
 * tape_do_io/__tape_wake_up
 * Add the request to the request queue, try to start it if the
 * tape is idle and wait uninterruptible for its completion.
 */
static void
__tape_wake_up(struct tape_request *request, void *data)
{
	request->callback = NULL;
	wake_up((wait_queue_head_t *) data);
}

int
tape_do_io(struct tape_device *device, struct tape_request *request)
{
	wait_queue_head_t	 wq;
	long			 flags;
	int			 rc;

	DBF_EVENT(5, "tape: tape_do_io(%p, %p)\n", device, request);

	init_waitqueue_head(&wq);
	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	/* Setup callback */
	request->callback = __tape_wake_up;
	request->callback_data = &wq;
	/* Add request to request queue and try to start it. */
	rc = __tape_do_io(device, request);
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
	if (rc)
		return rc;
	/* Request added to the queue. Wait for its completion. */
	wait_event(wq, (request->callback == NULL));
	/* Get rc from request */
	return request->rc;
}

/*
 * tape_do_io_interruptible/__tape_wake_up_interruptible
 * Add the request to the request queue, try to start it if the
 * tape is idle and wait uninterruptible for its completion.
 */
static void
__tape_wake_up_interruptible(struct tape_request *request, void *data)
{
	request->callback = NULL;
	wake_up_interruptible((wait_queue_head_t *) data);
}

int
tape_do_io_interruptible(struct tape_device *device,
			 struct tape_request *request)
{
	wait_queue_head_t	 wq;
	long			 flags;
	int			 rc;

	DBF_EVENT(5, "tape: tape_do_io_int(%p, %p)\n", device, request);

	init_waitqueue_head(&wq);
	// debug paranoia
	if(!device) BUG();
	if(!request) BUG();

	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	/* Setup callback */
	request->callback = __tape_wake_up_interruptible;
	request->callback_data = &wq;
	rc = __tape_do_io(device, request);
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
	if (rc)
		return rc;
	/* Request added to the queue. Wait for its completion. */
	rc = wait_event_interruptible(wq, (request->callback == NULL));
	if (rc != -ERESTARTSYS)
		/* Request finished normally. */
		return request->rc;
	/* Interrupted by a signal. We have to stop the current request. */
	spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
	rc = __tape_halt_io(device, request);
	if (rc == 0) {
		DBF_EVENT(3, "IO stopped on irq %d\n", device->devinfo.irq);
		rc = -ERESTARTSYS;
	}
	spin_unlock_irqrestore(get_irq_lock(device->devinfo.irq), flags);
	return rc;
}


/*
 * Tape interrupt routine, called from Ingo's I/O layer
 */
static void
__tape_do_irq (int irq, void *ds, struct pt_regs *regs)
{
	struct tape_device *device;
	struct tape_request *request;
	devstat_t *devstat;
	int final;
	int rc;

	devstat = (devstat_t *) ds;
	device = (struct tape_device *) s390_get_private_data(irq);
	if (device == NULL) {
		PRINT_ERR("could not get device structure for irq %d "
			  "in interrupt\n", irq);
		return;
	}
	request = (struct tape_request *) devstat->intparm;

	DBF_EVENT(5, "tape: __tape_do_irq(%p, %p)\n", device, request);

	if(request && request->timeout.expires) {
		/*
		 * If the timer was not yet startet the reference to the
		 * request has to be dropped here. Otherwise it will be
		 * dropped by the timeout handler.
		 */
		if(del_timer(&request->timeout) > 0)
			request->timeout.data = (unsigned long)
				tape_put_request(request);
	}

	if (device->devstat.cstat & SCHN_STAT_INCORR_LEN)
		DBF_EVENT(4, "tape: incorrect blocksize\n");

	if (device->devstat.dstat != 0x0c){
		/*
		 * Any request that does not come back with channel end
		 * and device end is unusual. Log the sense data.
		 */
		DBF_EVENT(3,"-- Tape Interrupthandler --\n");
		tape_dump_sense_dbf(device, request);
	}
	if (TAPE_NOT_OPER(device)) {
		DBF_EVENT(6, "tape:device is not operational\n");
		return;
	}

	/* Some status handling */
	if(devstat && devstat->dstat & DEV_STAT_UNIT_CHECK) {
		unsigned char *sense = devstat->ii.sense.data;

		if(!(sense[1] & SENSE_DRIVE_ONLINE))
			device->tape_generic_status &= ~GMT_ONLINE(~0);
	} else {
		device->tape_generic_status |= GMT_ONLINE(~0);
	}

	rc = device->discipline->irq(device, request);
	/*
	 * rc < 0 : request finished unsuccessfully.
	 * rc == TAPE_IO_SUCCESS: request finished successfully.
	 * rc == TAPE_IO_PENDING: request is still running. Ignore rc.
	 * rc == TAPE_IO_RETRY: request finished but needs another go.
	 * rc == TAPE_IO_STOP: request needs to get terminated. 
	 */
	final = 0;
	switch (rc) {
		case TAPE_IO_SUCCESS:
			final = 1;
			break;
		case TAPE_IO_PENDING:
			break;
		case TAPE_IO_RETRY:
#ifdef CONFIG_S390_TAPE_BLOCK
			if (request->op == TO_BLOCK)
				device->discipline->check_locate(device, request);
#endif
			rc = __do_IO(device, request);
			if (rc) {
				DBF_EVENT(1, "tape: DOIO failed with er = %i\n", rc);
				final = 1;
			}
			break;
		case TAPE_IO_STOP:
			__tape_halt_io(device, request);
			rc = -EIO;
			final = 1;
			break;
		default:
			if (rc > 0) {
				DBF_EVENT(6, "xunknownrc\n");
				PRINT_ERR("Invalid return code from discipline "
				  "interrupt function.\n");
				rc = -EIO;
			}
			final = 1;
			break;
	}
	if (final) {
		/* This might be an unsolicited interrupt (no request) */
		if(request != NULL) {
			/* Set ending status. */
			request->rc = rc;
			__tape_remove_request(device, request);
		}
		/* Start next request. */
		if (!list_empty(&device->req_queue))
			tape_schedule_bh(device);
	}
}

/*
 * Lock a shared tape for our exclusive use.
 */
int
tape_assign(struct tape_device *device, int type)
{
	int rc;

	spin_lock_irq(&device->assign_lock);

	/* The device is already assigned */
	rc = 0;
	if (!TAPE_ASSIGNED(device)) {
		rc = device->discipline->assign(device);

		spin_lock(get_irq_lock(device->devinfo.irq));
		if (rc) {
	        	PRINT_WARN(
				"(%04x): assign failed - "
				"device might be busy\n",
				device->devstat.devno);
	        	DBF_EVENT(3,
				"(%04x): assign failed "
				"- device might be busy\n",
				device->devstat.devno);
			TAPE_SET_STATE(device, TAPE_STATUS_BOXED);
		} else {
			DBF_EVENT(3, "(%04x): assign lpum = %02x\n",
				device->devstat.devno, device->devstat.lpum);
			tape_state_set(
				device,
				(device->tape_status | type) &
				(~TAPE_STATUS_BOXED)
			);
		}
	} else {
		spin_lock(get_irq_lock(device->devinfo.irq));
		TAPE_SET_STATE(device, type);
	}
	spin_unlock(get_irq_lock(device->devinfo.irq));
	spin_unlock_irq(&device->assign_lock);

	return rc;
}

/*
 * Unlock a shared tape.
 */
int
tape_unassign(struct tape_device *device, int type)
{
	int	 	rc;

	spin_lock_irq(&device->assign_lock);

	rc = 0;
	spin_lock(get_irq_lock(device->devinfo.irq));
	if (!TAPE_ASSIGNED(device)) {
		spin_unlock(get_irq_lock(device->devinfo.irq));
		spin_unlock_irq(&device->assign_lock);
		return 0;
	}
	TAPE_CLEAR_STATE(device, type);
	spin_unlock(get_irq_lock(device->devinfo.irq));

	if (!TAPE_ASSIGNED(device)) {
		rc = device->discipline->unassign(device);
		if (rc) {
			PRINT_WARN("(%04x): unassign failed\n",
				device->devstat.devno);
			DBF_EVENT(3, "(%04x): unassign failed\n",
				device->devstat.devno);
		} else {
			DBF_EVENT(3, "(%04x): unassign lpum = %02x\n",
				device->devstat.devno, device->devstat.lpum);
		}
	}

	spin_unlock_irq(&device->assign_lock);
	return rc;
}

/*
 * Tape device open function used by tape_char & tape_block frontends.
 */
int
tape_open(struct tape_device *device)
{
	int rc;

	spin_lock_irq(&tape_discipline_lock);
	spin_lock(get_irq_lock(device->devinfo.irq));
	if (TAPE_NOT_OPER(device)) {
		DBF_EVENT(6, "TAPE:nodev\n");
		rc = -ENODEV;
	} else if (TAPE_OPEN(device)) {
		DBF_EVENT(6, "TAPE:dbusy\n");
		rc = -EBUSY;
	} else if (device->discipline != NULL &&
		   !try_inc_mod_count(device->discipline->owner)) {
		DBF_EVENT(6, "TAPE:nodisc\n");
		rc = -ENODEV;
	} else {
		TAPE_SET_STATE(device, TAPE_STATUS_OPEN);
		rc = 0;
	}
	spin_unlock(get_irq_lock(device->devinfo.irq));
	spin_unlock_irq(&tape_discipline_lock);
	return rc;
}

/*
 * Tape device release function used by tape_char & tape_block frontends.
 */
int
tape_release(struct tape_device *device)
{
	spin_lock_irq(&tape_discipline_lock);
	spin_lock(get_irq_lock(device->devinfo.irq));

	if (TAPE_OPEN(device)) {
		TAPE_CLEAR_STATE(device, TAPE_STATUS_OPEN);

		if (device->discipline->owner)
			__MOD_DEC_USE_COUNT(device->discipline->owner);	
	}
	spin_unlock(get_irq_lock(device->devinfo.irq));
	spin_unlock_irq(&tape_discipline_lock);

	return 0;
}

/*
 * Execute a magnetic tape command a number of times.
 */
int
tape_mtop(struct tape_device *device, int mt_op, int mt_count)
{
	tape_mtop_fn fn;
	int rc;

	DBF_EVENT(6, "TAPE:mtio\n");
	DBF_EVENT(6, "TAPE:ioop: %x\n", mt_op);
	DBF_EVENT(6, "TAPE:arg:  %x\n", mt_count);

	if (mt_op < 0 || mt_op >= TAPE_NR_MTOPS)
		return -EINVAL;
	fn = device->discipline->mtop_array[mt_op];
	if(fn == NULL)
		return -EINVAL;

	/* We assume that the backends can handle count up to 500. */
	if (mt_op == MTBSR  || mt_op == MTFSR  || mt_op == MTFSF  ||
	    mt_op == MTBSF  || mt_op == MTFSFM || mt_op == MTBSFM) {
		rc = 0;
		for (; mt_count > 500; mt_count -= 500)
			if ((rc = fn(device, 500)) != 0)
				break;
		if (rc == 0)
			rc = fn(device, mt_count);
	} else
		rc = fn(device, mt_count);
	return rc;

}

void
tape_init_disciplines(void)
{
#ifdef	CONFIG_S390_TAPE_34XX
	tape_34xx_init();
#endif
#ifdef CONFIG_S390_TAPE_34XX_MODULE
	request_module("tape_34xx");
#endif

#ifdef CONFIG_S390_TAPE_3590
	tape_3590_init();
#endif
#ifdef CONFIG_S390_TAPE_3590_MODULE
	request_module("tape_3590");
#endif
	tape_auto_detect();
}

/*
 * Tape init function.
 */
static int
tape_init (void)
{
	tape_dbf_area = debug_register ( "tape", 1, 2, 4*sizeof(long));
	debug_register_view(tape_dbf_area, &debug_sprintf_view);
	debug_set_level(tape_dbf_area, 6); /* FIXME */
	DBF_EVENT(3, "tape init: ($Revision: 1.6 $)\n");
#ifdef CONFIG_DEVFS_FS
	tape_devfs_root_entry = devfs_mk_dir (NULL, "tape", NULL);
#endif /* CONFIG_DEVFS_FS */
	DBF_EVENT(3, "dev detect\n");
	/* Parse the parameters. */
	tape_devmap_init();
#ifdef CONFIG_PROC_FS
	tape_proc_init();
#endif /* CONFIG_PROC_FS */
	tapechar_init();
#ifdef CONFIG_S390_TAPE_BLOCK
	tapeblock_init();
#endif
	tape_init_disciplines();
	return 0;
}

/*
 * Tape exit function.
 */
void
tape_exit(void)
{
	struct list_head *l, *n;
	struct tape_discipline *discipline;

	DBF_EVENT(6, "tape exit\n");

	/* Cleanup registered disciplines. */
	spin_lock(&tape_discipline_lock);
	list_for_each_safe(l, n, &tape_disciplines) {
		discipline = list_entry(l, struct tape_discipline, list);
	       __tape_unregister_discipline(discipline);
	}
	spin_unlock(&tape_discipline_lock);

	/* Get rid of the frontends */
	tapechar_exit();
#ifdef CONFIG_S390_TAPE_BLOCK
	tapeblock_exit();
#endif
#ifdef CONFIG_PROC_FS
	tape_proc_cleanup();
#endif
	tape_devmap_exit();
#ifdef CONFIG_DEVFS_FS
	devfs_unregister (tape_devfs_root_entry); /* devfs checks for NULL */
#endif /* CONFIG_DEVFS_FS */
	debug_unregister (tape_dbf_area);
}

/*
 * Issue an hotplug event
 */
void tape_hotplug_event(struct tape_device *device, int devmaj, int action) {
#ifdef CONFIG_HOTPLUG
	char *argv[3];
	char *envp[8];
	char  devno[20];
	char  major[20];
	char  minor[20];

	sprintf(devno, "DEVNO=%04x", device->devinfo.devno);
	sprintf(major, "MAJOR=%d",   devmaj);
	sprintf(minor, "MINOR=%d",   device->first_minor);

	argv[0] = hotplug_path;
	argv[1] = "tape";
	argv[2] = NULL;

	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	switch(action) {
		case TAPE_HOTPLUG_CHAR_ADD:
		case TAPE_HOTPLUG_BLOCK_ADD:
			envp[2] = "ACTION=add";
			break;
		case TAPE_HOTPLUG_CHAR_REMOVE:
		case TAPE_HOTPLUG_BLOCK_REMOVE:
			envp[2] = "ACTION=remove";
			break;
		default:
			BUG();
	}
	switch(action) {
		case TAPE_HOTPLUG_CHAR_ADD:
		case TAPE_HOTPLUG_CHAR_REMOVE:
			envp[3] = "INTERFACE=char";
			break;
		case TAPE_HOTPLUG_BLOCK_ADD:
		case TAPE_HOTPLUG_BLOCK_REMOVE:
			envp[3] = "INTERFACE=block";
			break;
	}
	envp[4] = devno;
	envp[5] = major;
	envp[6] = minor;
	envp[7] = NULL;

	call_usermodehelper(argv[0], argv, envp);
#endif
}

MODULE_AUTHOR("(C) 2001 IBM Deutschland Entwicklung GmbH by Carsten Otte and "
	      "Michael Holzheu (cotte@de.ibm.com,holzheu@de.ibm.com)");
MODULE_DESCRIPTION("Linux on zSeries channel attached "
		   "tape device driver ($Revision: 1.6 $)");
MODULE_LICENSE("GPL");

module_init(tape_init);
module_exit(tape_exit);

EXPORT_SYMBOL(tape_dbf_area);
EXPORT_SYMBOL(tape_state_string);
EXPORT_SYMBOL(tape_op_verbose);
EXPORT_SYMBOL(tape_state_set);
EXPORT_SYMBOL(tape_med_state_set);
EXPORT_SYMBOL(tape_register_discipline);
EXPORT_SYMBOL(tape_unregister_discipline);
EXPORT_SYMBOL(tape_alloc_request);
EXPORT_SYMBOL(tape_put_request);
EXPORT_SYMBOL(tape_clone_request);
EXPORT_SYMBOL(tape_dump_sense);
EXPORT_SYMBOL(tape_dump_sense_dbf);
EXPORT_SYMBOL(tape_do_io);
EXPORT_SYMBOL(tape_do_io_free);
EXPORT_SYMBOL(tape_do_io_async);
EXPORT_SYMBOL(tape_do_io_interruptible);
EXPORT_SYMBOL(tape_mtop);
EXPORT_SYMBOL(tape_hotplug_event);

