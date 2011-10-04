/*
 *  drivers/s390/char/tape_devmap.c
 *    device mapping for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 *
 * Device mapping and tape= parameter parsing functions. All devmap
 * functions may not be called from interrupt context. In particular
 * tape_get_device is a no-no from interrupt context.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/ctype.h>
#include <linux/init.h>

#include <asm/debug.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "tape_devmap:"

#include "tape.h"

struct tape_devmap {
	struct list_head list;
	unsigned int devindex;
	unsigned short devno;
	devreg_t devreg;
	struct tape_device *device;
};

struct tape_discmap {
	struct list_head list;
	devreg_t devreg;
	struct tape_discipline *discipline;
};

/*
 * List of all registered tapes and disciplines.
 */
static struct list_head tape_devreg_list = LIST_HEAD_INIT(tape_devreg_list);
static struct list_head tape_disc_devreg_list = LIST_HEAD_INIT(tape_disc_devreg_list);
int tape_max_devindex = 0;

/*
 * Single spinlock to protect devmap structures and lists.
 */
static spinlock_t tape_devmap_lock = SPIN_LOCK_UNLOCKED;

/*
 * Module/Kernel Parameter Handling. The syntax of tape= is:
 *   <devno>		: (0x)?[0-9a-fA-F]+
 *   <range>		: <devno>(-<devno>)?
 *   <tape>		: <range>(,<range>)*
 */
int tape_autodetect = 0;	/* is true, when autodetection is active */

/*
 * char *tape[] is intended to hold the ranges supplied by the tape= statement
 * it is named 'tape' to directly be filled by insmod with the comma separated
 * strings when running as a module.
 */
static char *tape[256];
MODULE_PARM (tape, "1-" __MODULE_STRING (256) "s");

#ifndef MODULE
/*
 * The parameter parsing functions for builtin-drivers are called
 * before kmalloc works. Store the pointers to the parameters strings
 * into tape[] for later processing.
 */
static int __init
tape_call_setup (char *str)
{
	static int count = 0;

	if (count < 256)
		tape[count++] = str;
	return 1;
}

__setup("tape=", tape_call_setup);
#endif	 /* not defined MODULE */

/*
 * Add a range of devices and create the corresponding devreg_t
 * structures. The order of the ranges added by this function
 * will define the kdevs for the individual devices.
 */
int
tape_add_range(int from, int to)
{
	struct tape_devmap *devmap, *tmp;
	struct list_head *l;
	int devno;

	if (from > to) {
		PRINT_ERR("Invalid device range %04x-%04x", from, to);
		return -EINVAL;
	}
	spin_lock(&tape_devmap_lock);
	for (devno = from; devno <= to; devno++) {
		devmap = NULL;
		list_for_each(l, &tape_devreg_list) {
			tmp = list_entry(l, struct tape_devmap, list);
			if (tmp->devno == devno) {
				devmap = tmp;
				break;
			}
		}
		if (devmap == NULL) {
			/* This devno is new. */
			devmap = (struct tape_devmap *)
				kmalloc(sizeof(struct tape_devmap),
					GFP_KERNEL);
			if (devmap == NULL)
				return -ENOMEM;
			memset(devmap, 0, sizeof(struct tape_devmap));
			devmap->devno = devno;
			devmap->devindex = tape_max_devindex++;
			list_add(&devmap->list, &tape_devreg_list);
			devmap->devreg.ci.devno = devno;
			devmap->devreg.flag = DEVREG_TYPE_DEVNO;
			devmap->devreg.oper_func = tape_oper_handler;
			s390_device_register(&devmap->devreg);
		}
	}
	spin_unlock(&tape_devmap_lock);
	return 0;
}

/*
 * Read device number from string. The number is always is hex,
 * a leading 0x is accepted (and has to be removed for simple_stroul
 * to work).
 */
static inline int
tape_devno(char *str, char **endp)
{
	/* remove leading '0x' */
	if (*str == '0') {
		str++;
		if (*str == 'x')
			str++;
	}
	if (!isxdigit(*str))
		return -EINVAL;
	return simple_strtoul(str, endp, 16); /* interpret anything as hex */
}

/*
 * Parse Kernel/Module Parameters and create devregs for dynamic attach/detach
 */
static int
tape_parm_parse (char *str)
{
	int from, to, rc;

	while (1) {
		to = from = tape_devno(str, &str);
		if (*str == '-') {
			str++;
			to = tape_devno(str, &str);
		}
		/* Negative numbers in from/to indicate errors. */
		if (from >= 0 && to >= 0) {
			rc = tape_add_range(from, to);
			if (rc)
				return rc;
		}
		if (*str != ',')
			break;
		str++;
	}
	if (*str != '\0') {
		PRINT_WARN("junk at end of tape parameter string: %s\n", str);
		return -EINVAL;
	}
	return 0;
}

/*
 * Parse parameters stored in tape[].
 */
static int
tape_parse(void)
{
	int rc, i;

	if (*tape == NULL) {
		/* No parameters present */
		PRINT_INFO ("No parameters supplied, enabling auto detect "
			    "mode for all supported devices.\n");
		tape_autodetect = 1;
		return 0;
	}
	PRINT_INFO("Using ranges supplied in parameters, "
		   "disabling auto detect mode.\n");
	rc = 0;
	for (i = 0; i < 256; i++) {
		if (tape[i] == NULL)
			break;
		rc = tape_parm_parse(tape[i]);
		if (rc) {
			PRINT_ERR("Invalid tape parameter found.\n");
			break;
		}
	}
	return rc;
}

/*
 * Create a devreg for a discipline. This is only done if no explicit
 * tape range is given. The tape_oper_handler will call tape_add_range
 * for each device that appears.
 */
static int
tape_add_disc_devreg(struct tape_discipline *discipline)
{
	struct tape_discmap *discmap;

	discmap = (struct tape_discmap *) kmalloc(sizeof(struct tape_discmap),
						  GFP_KERNEL);
	if (discmap == NULL) {
		PRINT_WARN("Could not alloc devreg: Out of memory\n"
			   "Dynamic attach/detach will not work!\n");
		return -ENOMEM;
	}
	spin_lock(&tape_devmap_lock);
	discmap->devreg.ci.hc.ctype = discipline->cu_type;
	discmap->devreg.flag = DEVREG_MATCH_CU_TYPE | DEVREG_TYPE_DEVCHARS;
	discmap->devreg.oper_func = tape_oper_handler;
	s390_device_register(&discmap->devreg);
	list_add(&discmap->list, &tape_disc_devreg_list);
	spin_unlock(&tape_devmap_lock);
	return 0;
}

/*
 * Free devregs for a discipline.
 */
static void
tape_del_disc_devreg(struct tape_discipline *discipline)
{
	struct list_head *l;
	struct tape_discmap *discmap;

	spin_lock(&tape_devmap_lock);
	list_for_each(l, &tape_disc_devreg_list) {
		discmap = list_entry(l, struct tape_discmap, list);
		if (discmap->discipline == discipline) {
			s390_device_unregister(&discmap->devreg);
			list_del(&discmap->list);
			kfree(discmap);
			break;
		}
	}
	spin_unlock(&tape_devmap_lock);
}


/*
 * Forget all about device numbers and disciplines.
 * This may only be called at module unload or system shutdown.
 */
static void
tape_forget_devregs(void)
{
	struct list_head *l, *n;
	struct tape_devmap *devmap;
	struct tape_discmap *discmap;

	spin_lock(&tape_devmap_lock);
        list_for_each_safe(l, n, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		if (devmap->device != NULL)
			BUG();
		s390_device_unregister(&devmap->devreg);
		list_del(&devmap->list);
		kfree(devmap);
	}
	list_for_each_safe(l, n, &tape_disc_devreg_list) {
		discmap = list_entry(l, struct tape_discmap, list);
		s390_device_unregister(&discmap->devreg);
		list_del(&discmap->list);
		kfree(discmap);
	}
	spin_unlock(&tape_devmap_lock);
}

/*
 * Allocate memory for a new device structure.
 */
static struct tape_device *
tape_alloc_device(void)
{
	struct tape_device *device;

	device = (struct tape_device *)
		kmalloc(sizeof(struct tape_device), GFP_KERNEL);
	if (device == NULL) {
		DBF_EXCEPTION(2, "ti:no mem\n");
		PRINT_INFO ("can't allocate memory for "
			    "tape info structure\n");
		return ERR_PTR(-ENOMEM);
	}
	memset(device, 0, sizeof(struct tape_device));
	device->modeset_byte = (char *) kmalloc(1, GFP_KERNEL | GFP_DMA);
	if (device->modeset_byte == NULL) {
		DBF_EXCEPTION(2, "ti:no mem\n");
		PRINT_INFO("can't allocate memory for modeset byte\n");
		kfree(device);
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&device->req_queue);
	init_waitqueue_head(&device->state_change_wq);
	spin_lock_init(&device->assign_lock);
	atomic_set(&device->ref_count, 1);
	TAPE_SET_STATE(device, TAPE_STATUS_INIT);
	device->medium_state  = MS_UNKNOWN;
	*device->modeset_byte = 0;

	return device;
}

/*
 * Create a device structure. 
 */
static struct tape_device *
tape_create_device(int devno)
{
	struct list_head *l;
	struct tape_devmap *devmap, *tmp;
	struct tape_device *device;
	int rc;

	DBF_EVENT(4, "tape_create_device(0x%04x)\n", devno);

	device = tape_alloc_device();
	if (IS_ERR(device))
		return device;
	/* Get devinfo from the common io layer. */
	rc = get_dev_info_by_devno(devno, &device->devinfo);
	if (rc) {
		tape_put_device(device);
		return ERR_PTR(rc);
	}
	spin_lock(&tape_devmap_lock);
	devmap = NULL;
	list_for_each(l, &tape_devreg_list) {
		tmp = list_entry(l, struct tape_devmap, list);
		if (tmp->devno == devno) {
			devmap = tmp;
			break;
		}
	}
	if (devmap != NULL && devmap->device == NULL) {
		devmap->device = tape_clone_device(device);
		device->first_minor = devmap->devindex * TAPE_MINORS_PER_DEV;
	} else if (devmap == NULL) {
		/* devno not in tape range. */
		DBF_EVENT(4, "No devmap for entry 0x%04x\n", devno);
		tape_put_device(device);
		device = ERR_PTR(-ENODEV);
	} else {
		/* Should not happen. */
		DBF_EVENT(4, "A devmap entry for 0x%04x already exists\n",
			devno);
		tape_put_device(device);
		device = ERR_PTR(-EEXIST);
	}
	spin_unlock(&tape_devmap_lock);

	return device;
}

struct tape_device *
tape_clone_device(struct tape_device *device)
{
	DBF_EVENT(4, "tape_clone_device(%p) = %i\n", device,
		atomic_inc_return(&device->ref_count));
	return device;
}

/*
 * Find tape device by a device index.
 */
struct tape_device *
tape_get_device(int devindex)
{
	struct list_head *l;
	struct tape_devmap *devmap;
	struct tape_device *device;

	DBF_EVENT(5, "tape_get_device(%i)\n", devindex);

	device = ERR_PTR(-ENODEV);
	spin_lock(&tape_devmap_lock);
	/* Find devmap for device with device number devno. */
	list_for_each(l, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		if (devmap->devindex == devindex) {
			if (devmap->device != NULL) {
				device = tape_clone_device(devmap->device);
			}
			break;
		}
	}
	spin_unlock(&tape_devmap_lock);
	return device;
}

/*
 * Find tape handle by a devno.
 */
struct tape_device *
tape_get_device_by_devno(int devno)
{
	struct list_head	*l;
	struct tape_devmap	*devmap;
	struct tape_device	*device;

	DBF_EVENT(5, "tape_get_device_by_devno(0x%04x)\n", devno);

	device = ERR_PTR(-ENODEV);
	spin_lock(&tape_devmap_lock);

	list_for_each(l, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		if(devmap->device != NULL && devmap->devno == devno) {
			device = tape_clone_device(devmap->device);
			break;
		}
	}
	spin_unlock(&tape_devmap_lock);

	return device;
}

/*
 * Find tape handle by a device irq.
 */
struct tape_device *
tape_get_device_by_irq(int irq)
{
	struct list_head	*l;
	struct tape_devmap	*devmap;
	struct tape_device	*device;

	DBF_EVENT(5, "tape_get_device_by_irq(0x%02x)\n", irq);

	device = ERR_PTR(-ENODEV);
	spin_lock(&tape_devmap_lock);
	/* Find devmap for device with device number devno. */
	list_for_each(l, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		if (devmap->device != NULL && 
		    devmap->device->devinfo.irq == irq) {
			device = tape_clone_device(devmap->device);
			break;
		}
	}
	spin_unlock(&tape_devmap_lock);
	return device;
}

/*
 * Decrease the reference counter of a devices structure. If the
 * reference counter reaches zero free the device structure and
 * wake up sleepers.
 */
void
tape_put_device(struct tape_device *device)
{
	int remain;

	DBF_EVENT(4, "tape_put_device(%p)\n", device);

	if ((remain = atomic_dec_return(&device->ref_count)) > 0) {
		DBF_EVENT(5, "remaining = %i\n", remain);
		return;
	}

	/*
	 * Reference counter dropped to zero. This means
	 * that the device is deleted and the last user
	 * of the device structure is gone. That is what
	 * tape_delete_device is waiting for. Do a wake up.
	 */
	if(remain < 0) {
		PRINT_ERR("put device without reference\n");
		return;
	}

	/*
	 * Free memory of a device structure.
	 */
	kfree(device->modeset_byte);
	kfree(device);
}

/*
 * Scan the device range for devices with matching cu_type, create
 * their device structures and enable them.
 */
void
tape_add_devices(struct tape_discipline *discipline)
{
	struct list_head *l;
	struct tape_devmap *devmap;
	struct tape_device *device;

	/*
	 * Scan tape devices for matching cu type.
	 */
	list_for_each(l, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		device = tape_create_device(devmap->devno);
		if (IS_ERR(device))
			continue;

		if (device->devinfo.sid_data.cu_type == discipline->cu_type) {
			DBF_EVENT(4, "tape_add_devices(%p)\n", discipline);
			DBF_EVENT(4, "det irq:  %x\n", device->devinfo.irq);
			DBF_EVENT(4, "cu     :  %x\n", discipline->cu_type);
			tape_enable_device(device, discipline);
		} else {
			devmap->device = NULL;
			tape_put_device(device);
		}
		tape_put_device(device);
	}
	if (tape_autodetect)
		tape_add_disc_devreg(discipline);
}

/*
 * Scan the device range for devices with matching cu_type, disable them
 * and remove their device structures.
 */
void
tape_remove_devices(struct tape_discipline *discipline)
{
	struct list_head *l;
	struct tape_devmap *devmap;
	struct tape_device *device;

	if (tape_autodetect)
		tape_del_disc_devreg(discipline);
	/*
	 * Go through our tape info list and disable, deq and free
	 * all devices with matching discipline
	 */
	list_for_each(l, &tape_devreg_list) {
		devmap = list_entry(l, struct tape_devmap, list);
		device = devmap->device;
		if (device == NULL)
			continue;
		if (device->discipline == discipline) {
			tape_disable_device(device);
			tape_put_device(device);
			devmap->device = NULL;
		}
	}
}

/*
 * Auto detect tape devices.
 */
void
tape_auto_detect(void)
{
	struct tape_device *device;
	struct tape_discipline *discipline;
	s390_dev_info_t dinfo;
	int irq, devno;

	if (!tape_autodetect)
		return;
	for (irq = get_irq_first(); irq != -ENODEV; irq = get_irq_next(irq)) {
		/* Get device info block. */
		devno = get_devno_by_irq(irq);
		if (get_dev_info_by_irq(irq, &dinfo) < 0)
			continue;
		/* Search discipline with matching cu_type */
		discipline = tape_get_discipline(dinfo.sid_data.cu_type);
		if (discipline == NULL)
			continue;
		DBF_EVENT(4, "tape_auto_detect()\n");
		DBF_EVENT(4, "det irq:  %x\n", irq);
		DBF_EVENT(4, "cu     :  %x\n", dinfo.sid_data.cu_type);
		if (tape_add_range(dinfo.devno, dinfo.devno) == 0) {
			device = tape_create_device(devno);
			if (!IS_ERR(device)) {
				tape_enable_device(device, discipline);
				tape_put_device(device);
			}
		}
		tape_put_discipline(discipline);
	}
}

/*
 * Private task queue for oper/noper handling...
 */
static DECLARE_TASK_QUEUE(tape_cio_tasks);

/*
 * Oper Handler is called from Ingo's I/O layer when a new tape device is
 * attached.
 */
static void
do_tape_oper_handler(void *data)
{
	struct {
		int			devno;
		int			cu_type;
		struct tq_struct	task;
	} *p;
	struct tape_device       *device;
	struct tape_discipline   *discipline;
	unsigned long		  flags;

	p = (void *) data;

	/*
	 * Handling the path revalidation scheme or common IO. Devices that
	 * were detected before will be reactivated.
	 */
	if(!IS_ERR(device = tape_get_device_by_devno(p->devno))) {
		spin_lock_irqsave(get_irq_lock(device->devinfo.irq), flags);
		if (!TAPE_NOACCESS(device)) {
			PRINT_ERR(
				"Oper handler for irq %d called, "
				"which is (still) internally used.\n",
				device->devinfo.irq);
		} else {
			DBF_EVENT(3,
				"T390(%04x): resume processing\n",
				p->devno);
			TAPE_CLEAR_STATE(device, TAPE_STATUS_NOACCESS);
			tape_schedule_bh(device);
		}
		spin_unlock_irqrestore(
			get_irq_lock(device->devinfo.irq), flags);

		tape_put_device(device);
		kfree(p);
		return;
	}

	/* If we get here device is NULL. */	
	if (tape_autodetect && tape_add_range(p->devno, p->devno) != 0) {
		kfree(p);
		return;
	}

	/* Find discipline for this device. */
	discipline = tape_get_discipline(p->cu_type);
	if (discipline == NULL) {
		/* Strange. Should not happen. */
		kfree(p);
		return;
	}

	device = tape_create_device(p->devno);
	if (IS_ERR(device)) {
		tape_put_discipline(discipline);
		kfree(p);
		return;
	}
	tape_enable_device(device, discipline);
	tape_put_device(device);
	tape_put_discipline(discipline);
	kfree(p);
}

int
tape_oper_handler(int irq, devreg_t *devreg)
{
	struct {
		int			devno;
		int			cu_type;
		struct tq_struct	task;
	} *p;
	s390_dev_info_t dinfo;
	int rc;

	rc = get_dev_info_by_irq (irq, &dinfo);
	if (rc < 0)
		return rc;

	/* No memory, we loose. */
	if ((p = kmalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	p->devno   = dinfo.devno;
	p->cu_type = dinfo.sid_data.cu_type;
	memset(&p->task, 0, sizeof(struct tq_struct));
	p->task.routine = do_tape_oper_handler;
	p->task.data    = p;

	/* queue call to do_oper_handler. */
	queue_task(&p->task, &tape_cio_tasks);
	run_task_queue(&tape_cio_tasks);

	return 0;
}


/*
 * Not Oper Handler is called from Ingo's IO layer, when a tape device
 * is detached.
 */
static void
do_tape_noper_handler(void *data)
{
	struct {
		int			irq;
		int			status;
		struct tq_struct	task;
	} *p;
	struct tape_device	*device;
	struct list_head	*l;
	struct tape_devmap	*devmap;
	unsigned long		 flags;

	p = data;

	/*
	 * find out devno of leaving device: CIO has already deleted
	 * this information so we need to find it by irq!
	 */
	device = tape_get_device_by_irq(p->irq);
	if (IS_ERR(device)) {
		kfree(p);
		return;
	}

	/*
	 * Handle the new path revalidation scheme of the common IO layer.
	 */
	switch(p->status) {
		case DEVSTAT_DEVICE_GONE:
		case DEVSTAT_REVALIDATE: /* FIXME: What to do? */
			tape_disable_device(device);

			/*
			 * Remove the device reference from the device map.
			 */
			spin_lock(&tape_devmap_lock);
			list_for_each(l, &tape_devreg_list) {
				devmap = list_entry(
						l, struct tape_devmap, list
					);
				if (devmap->device == device) {
					tape_put_device(device);
					devmap->device = NULL;
					break;
				}
			}
			spin_unlock(&tape_devmap_lock);
			break;
		case DEVSTAT_NOT_ACC:
			/*
			 * Device shouldn't be accessed at the moment. The
			 * currently running request will complete.
			 */
			spin_lock_irqsave(
				get_irq_lock(device->devinfo.irq), flags
			);
			DBF_EVENT(3, "T390(%04x): suspend processing\n",
				device->devinfo.devno);
			TAPE_SET_STATE(device, TAPE_STATUS_NOACCESS);
			spin_unlock_irqrestore(
				get_irq_lock(device->devinfo.irq), flags
			);
			break;
		case DEVSTAT_NOT_ACC_ERR: {
			struct tape_request *request;

			/*
			 * Device shouldn't be accessed at the moment. The
			 * request that was running is lost.
			 */
			spin_lock_irqsave(
				get_irq_lock(device->devinfo.irq), flags
			);

			request = list_entry(device->req_queue.next,
					struct tape_request, list);
			if(
				!list_empty(&device->req_queue)
				&&
				request->status == TAPE_REQUEST_IN_IO
			) {
				/* Argh! Might better belong to tape_core.c */
				list_del(&request->list);
				request->rc     = -EIO;
				request->status = TAPE_REQUEST_DONE;
				if (request->callback != NULL) {
					request->callback(
						request,
						request->callback_data
					);
					request->callback = NULL;
				}
			}
			DBF_EVENT(3, "T390(%04x): suspend processing\n",
				device->devinfo.devno);
			DBF_EVENT(3, "T390(%04x): request lost\n",
				device->devinfo.devno);
			TAPE_SET_STATE(device, TAPE_STATUS_NOACCESS);
			spin_unlock_irqrestore(
				get_irq_lock(device->devinfo.irq), flags
			);
			break;
		}
		default:
			PRINT_WARN("T390(%04x): no operation handler called "
				"with unknown status(0x%x)\n",
				device->devinfo.devno, p->status);
			tape_disable_device(device);

			/*
			 * Remove the device reference from the device map.
			 */
			spin_lock(&tape_devmap_lock);
			list_for_each(l, &tape_devreg_list) {
				devmap = list_entry(
						l, struct tape_devmap, list
					);
				if (devmap->device == device) {
					tape_put_device(device);
					devmap->device = NULL;
					break;
				}
			}
			spin_unlock(&tape_devmap_lock);
	}

	tape_put_device(device);
	kfree(p);
}

void
tape_noper_handler(int irq, int status)
{
	struct {
		int			irq;
		int			status;
		struct tq_struct	task;
	} *p;

	/* No memory, we loose. */
	if ((p = kmalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return;

	p->irq  = irq;
	p->status = status;
	memset(&p->task, 0, sizeof(struct tq_struct));
	p->task.routine = do_tape_noper_handler;
	p->task.data    = p;
	
	/* queue call to do_oper_handler. */
	queue_task(&p->task, &tape_cio_tasks);
	run_task_queue(&tape_cio_tasks);
}


int
tape_devmap_init(void)
{
	return tape_parse();
}

void
tape_devmap_exit(void)
{
	tape_forget_devregs();
}

EXPORT_SYMBOL(tape_get_device);
EXPORT_SYMBOL(tape_get_device_by_irq);
EXPORT_SYMBOL(tape_get_device_by_devno);
EXPORT_SYMBOL(tape_put_device);
EXPORT_SYMBOL(tape_clone_device);
