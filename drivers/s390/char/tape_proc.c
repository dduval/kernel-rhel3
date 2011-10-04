/*
 *  drivers/s390/char/tape.c
 *    tape device driver for S/390 and zSeries tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 * PROCFS Functions
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/seq_file.h>
#include <asm/irq.h>
#include <asm/s390io.h>

#include "tape.h"

#define PRINTK_HEADER "T390:"

static const char *tape_med_st_verbose[MS_SIZE] =
{
	[MS_UNKNOWN]  = "UNKNOWN ",
	[MS_LOADED]   = "LOADED  ",
	[MS_UNLOADED] = "UNLOADED"
};

/* our proc tapedevices entry */
static struct proc_dir_entry *tape_proc_devices;

static int tape_proc_show(struct seq_file *m, void *v) {
	struct tape_device  *device;
	struct tape_request *request;
	unsigned long        n;

	n = ((unsigned long) v - 1);

	if(n == 0) {
		seq_printf(m,
			"TapeNo\tDevNo\tCuType\tCuModel\tDevType\t"
			"DevMod\tBlkSize\tState\tOp\tMedState\n"
		);
	}

	device = tape_get_device(n);
	if(IS_ERR(device))
		return 0;

	spin_lock_irq(get_irq_lock(device->devinfo.irq));

	seq_printf(m,
		"%d\t%04X\t%04X\t%02X\t%04X\t%02X\t",
		device->first_minor/TAPE_MINORS_PER_DEV,
		device->devinfo.devno,
		device->devinfo.sid_data.cu_type,
		device->devinfo.sid_data.cu_model,
		device->devinfo.sid_data.dev_type,
		device->devinfo.sid_data.dev_model
	);

	/*
	 * the blocksize is either 'auto' or the blocksize as a decimal number
	 */
	if(device->char_data.block_size == 0)
		seq_printf(m, "auto\t");
	else
		seq_printf(m, "%i\t", device->char_data.block_size);

	seq_printf(m, "%s\t", tape_state_string(device));

	/*
	 * verbose desciption of current tape operation
	 */
	if(!list_empty(&device->req_queue)) {
		request = list_entry(
			device->req_queue.next, struct tape_request, list
		);

		seq_printf(m, "%s\t", tape_op_verbose[request->op]);
	} else {
		seq_printf(m, "---\t");
	}
	
	seq_printf(m, "%s\n", tape_med_st_verbose[device->medium_state]);

	spin_unlock_irq(get_irq_lock(device->devinfo.irq));
	tape_put_device(device);

	return 0;
}

static void *tape_proc_start(struct seq_file *m, loff_t *pos) {
	if(*pos < tape_max_devindex)
		return (void *) ((unsigned long) (*pos) + 1);
	return NULL;
}

static void tape_proc_stop(struct seq_file *m, void *v) {
}

static void *tape_proc_next(struct seq_file *m, void *v, loff_t *pos) {
	(*pos)++;
	return tape_proc_start(m, pos);
}

static struct seq_operations tape_proc_seq = {
	.start  = tape_proc_start,
	.next   = tape_proc_next,
	.stop   = tape_proc_stop,
	.show   = tape_proc_show,
};

static int tape_proc_open(struct inode *inode, struct file *file) {
	return seq_open(file, &tape_proc_seq);
}

static int
tape_proc_assign(int devno)
{
	int			 rc;
	struct tape_device	*device;

	if(IS_ERR(device = tape_get_device_by_devno(devno))) {
		DBF_EVENT(3, "TAPE(%04x): assign invalid device\n", devno);
		PRINT_ERR("TAPE(%04x): assign invalid device\n", devno);
		return PTR_ERR(device);
	}

	rc = tape_assign(device, TAPE_STATUS_ASSIGN_M);

	tape_put_device(device);

	return rc;
}

static int
tape_proc_unassign(int devno)
{
	int			 rc;
	struct tape_device	*device;

	if(IS_ERR(device = tape_get_device_by_devno(devno))) {
		DBF_EVENT(3, "TAPE(%04x): unassign invalid device\n", devno);
		PRINT_ERR("TAPE(%04x): unassign invalid device\n", devno);
		return PTR_ERR(device);
	}

	rc = tape_unassign(device, TAPE_STATUS_ASSIGN_M);

	tape_put_device(device);

	return rc;
}

#ifdef SMB_DEBUG_BOX
static int
tape_proc_put_into_box(int devno)
{
	struct tape_device	*device;

	if(IS_ERR(device = tape_get_device_by_devno(devno))) {
		DBF_EVENT(3, "TAPE(%04x): invalid device\n", devno);
		PRINT_ERR("TAPE(%04x): invalid device\n", devno);
		return PTR_ERR(device);
	}

	TAPE_SET_STATE(device, TAPE_STATUS_BOXED);

	tape_put_device(device);

	return 0;
}
#endif

#ifdef TAPE390_FORCE_UNASSIGN
static int
tape_proc_force_unassign(int devno)
{
	int			 rc;
	struct tape_device	*device;

	if(IS_ERR(device = tape_get_device_by_devno(devno))) {
		DBF_EVENT(3, "TAPE(%04x): force unassign invalid device\n",
			devno);
		PRINT_ERR("TAPE(%04x): force unassign invalid device\n",
			devno);
		return PTR_ERR(device);
	}

	if (!TAPE_BOXED(device)) {
		DBF_EVENT(3, "TAPE(%04x): forced unassignment only allowed for"
			" boxed device\n", devno);
		PRINT_ERR("TAPE(%04x): forced unassignment only allowed for"
			" boxed device\n", devno);
		rc = -EPERM;
	} else if(device->discipline->force_unassign == NULL) {
		DBF_EVENT(3, "TAPE(%04x: force unassign is not supported on"
			" this device\n", devno);
		PRINT_ERR("TAPE(%04x: force unassign is not supported on"
			" this device\n", devno);
		rc = -EPERM;
	} else { 
		rc = device->discipline->force_unassign(device);
		if(rc == 0)
			spin_lock_irq(get_irq_lock(device->devinfo.irq));
			TAPE_CLEAR_STATE(
				device,
				TAPE_STATUS_BOXED
				| TAPE_STATUS_ASSIGN_A
				| TAPE_STATUS_ASSIGN_M
			);
			spin_unlock_irq(get_irq_lock(device->devinfo.irq));
	}

	tape_put_device(device);
	return rc;
}
#endif

/*
 * Skips over all characters to the position after a newline or beyond the
 * last character of the string.
 * Returns the number of characters skiped.
 */
static size_t
tape_proc_skip_eol(const char *buf, size_t len, loff_t *off)
{
	loff_t start = *off;

	while((*off - start) < len) {
		if(*(buf+*off) == '\n') {
			*off += 1;
			break;
		}
		*off += 1;
	}

	return (size_t) (*off - start);
}

/*
 * Skips over whitespace characters and returns the number of characters
 * that where skiped.
 */
static size_t
tape_proc_skip_ws(const char *buf, size_t len, loff_t *off)
{
	loff_t start = *off;

	while((*off - start) < len) {
		if(*(buf + *off) != ' ' && *(buf + *off) != '\t')
			break;
		*off += 1;
	}

	return (size_t) (*off - start);
}

static size_t
tape_proc_get_hexvalue(char *buf, size_t len, loff_t *off, unsigned int *hex)
{
	int          hexdigit;
	loff_t       start    = *off;

	/* Skip possible space characters */
	tape_proc_skip_ws(buf, len, off);

	/* The hexvalue might start with '0x' or '0X' */
	if((*off - start)+1 < len && *(buf + *off) == '0')
		if(*(buf + *off + 1) == 'x' || *(buf + *off + 1) == 'X')
			*off += 2;

	*hex = 0;
	while((*off - start) < len) {
		if(*(buf + *off) >= '0' && *(buf + *off) <= '9') {
			hexdigit = *(buf + *off) - '0';
		} else if(*(buf + *off) >= 'a' && *(buf + *off) <= 'f') {
			hexdigit = *(buf + *off) - 'a' + 10;
		} else if(*(buf + *off) >= 'A' && *(buf + *off) <= 'F') {
			hexdigit = *(buf + *off) - 'A' + 10;
		} else {
			break;
		}
		*hex  = (*hex << 4) + hexdigit;
		*off += 1;
	}

	return (size_t) (*off - start);
}

static ssize_t tape_proc_write(
	struct file *file,
	const char  *buf,
	size_t       len,
	loff_t      *off
) {
	loff_t  start = *off;
	int     devno;
	char   *s;

	if(PAGE_SIZE < len)
		return -EINVAL;

	if((s = kmalloc(len, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	if(copy_from_user(s, buf, len) != 0) {
		kfree(s);
		return -EFAULT;
	}

	if(strncmp(s+*off, "assign", 6) == 0) {
		(*off) += 6;
		tape_proc_get_hexvalue(s, len - 6, off, &devno);
		if(devno > 0)
			tape_proc_assign(devno);
	} else if(strncmp(s+*off, "unassign", 8) == 0) {
		(*off) += 8;
		tape_proc_get_hexvalue(s, len - (*off - start), off, &devno);
		if(devno > 0)
			tape_proc_unassign(devno);
#ifdef TAPE390_FORCE_UNASSIGN
	} else if(strncmp(s+*off, "forceunassign", 13) == 0) {
		(*off) += 13;
		tape_proc_get_hexvalue(s, len - (*off - start), off, &devno);
		if(devno > 0)
			tape_proc_force_unassign(devno);
#endif
#ifdef SMB_DEBUG_BOX
	} else if(strncmp(s+*off, "putintobox", 10) == 0) {
		(*off) += 10;
		tape_proc_get_hexvalue(s, len - (*off - start), off, &devno);
		if(devno > 0)
			tape_proc_put_into_box(devno);
#endif
	} else {
		DBF_EVENT(3, "tape_proc_write() parse error\n");
		PRINT_ERR("Invalid /proc/tapedevices command.\n");
	}
	tape_proc_skip_eol(s, len - (*off - start), off);

	kfree(s);

	/* Just pretend to have processed all the stuff */
	return len;
}

static struct file_operations tape_proc_ops =
{
	.open    = tape_proc_open,
	.read    = seq_read,
	.write   = tape_proc_write,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/* 
 * Initialize procfs stuff on startup
 */
void tape_proc_init(void) {
	tape_proc_devices = create_proc_entry(
		"tapedevices", S_IFREG | S_IRUGO | S_IWUSR, &proc_root);

	if (tape_proc_devices == NULL) {
		PRINT_WARN("tape: Cannot register procfs entry tapedevices\n");
		return;
	}
	tape_proc_devices->proc_fops = &tape_proc_ops;
	tape_proc_devices->owner     = THIS_MODULE;
}

/*
 * Cleanup all stuff registered to the procfs
 */
void tape_proc_cleanup(void) {
	if(tape_proc_devices != NULL)
		remove_proc_entry ("tapedevices", &proc_root);
}
