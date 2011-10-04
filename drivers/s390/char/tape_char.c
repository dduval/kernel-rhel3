/*
 *  drivers/s390/char/tape_char.c
 *    character device frontend for tape device driver
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
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/mtio.h>

#include <asm/irq.h>
#include <asm/s390dyn.h>
#include <asm/uaccess.h>

#include "tape.h"
#include "tape_std.h"

#define PRINTK_HEADER "TCHAR:"

#define TAPECHAR_DEVFSMODE	0020644	/* crwxrw-rw- */
#define TAPECHAR_MAJOR		0	/* get dynamic major */

int tapechar_major = TAPECHAR_MAJOR;

/*
 * Prototypes for file operation functions
 */
static ssize_t tapechar_read(struct file *, char *, size_t, loff_t *);
static ssize_t tapechar_write(struct file *, const char *, size_t, loff_t *);
static int tapechar_open(struct inode *,struct file *);
static int tapechar_release(struct inode *,struct file *);
static int tapechar_ioctl(struct inode *, struct file *, unsigned int,
			  unsigned long);

/*
 * File operation structure for tape character frontend
 */
static struct file_operations tape_fops =
{
	.read = tapechar_read,
	.write = tapechar_write,
	.ioctl = tapechar_ioctl,
	.open = tapechar_open,
	.release = tapechar_release,
};

#ifdef CONFIG_DEVFS_FS
/*
 * Create Char directory with (non)rewinding entries
 */
static int
tapechar_mkdevfstree(struct tape_device *device)
{
	device->char_data.devfs_char_dir =
		devfs_mk_dir(device->devfs_dir, "char", device);
	if (device->char_data.devfs_char_dir == NULL)
		return -ENOENT;
	device->char_data.devfs_nonrewinding =
		devfs_register(device->char_data.devfs_char_dir,
			       "nonrewinding", DEVFS_FL_DEFAULT,
			       tapechar_major, device->first_minor,
			       TAPECHAR_DEVFSMODE, &tape_fops, device);
	if (device->char_data.devfs_nonrewinding == NULL) {
		devfs_unregister(device->char_data.devfs_char_dir);
		return -ENOENT;
	}
	device->char_data.devfs_rewinding =
		devfs_register(device->char_data.devfs_char_dir,
			       "rewinding", DEVFS_FL_DEFAULT,
			       tapechar_major, device->first_minor + 1,
			       TAPECHAR_DEVFSMODE, &tape_fops, device);
	if (device->char_data.devfs_rewinding == NULL) {
		devfs_unregister(device->char_data.devfs_nonrewinding);
		devfs_unregister(device->char_data.devfs_char_dir);
		return -ENOENT;
	}
	return 0;
}

/*
 * Remove devfs entries
 */
static void
tapechar_rmdevfstree (struct tape_device *device)
{
	if (device->char_data.devfs_nonrewinding) 
		devfs_unregister(device->char_data.devfs_nonrewinding);
	if (device->char_data.devfs_rewinding)
		devfs_unregister(device->char_data.devfs_rewinding);
	if (device->char_data.devfs_char_dir)
		devfs_unregister(device->char_data.devfs_char_dir);
}
#endif

/*
 * This function is called for every new tapedevice
 */
int
tapechar_setup_device(struct tape_device * device)
{
#ifdef CONFIG_DEVFS_FS
	int rc;

	rc = tapechar_mkdevfstree(device);
	if (rc)
		return rc;
#endif

	tape_hotplug_event(device, tapechar_major, TAPE_HOTPLUG_CHAR_ADD);
	return 0;
	
}

void
tapechar_cleanup_device(struct tape_device* device)
{
#ifdef CONFIG_DEVFS_FS
	tapechar_rmdevfstree(device);
#endif
	tape_hotplug_event(device, tapechar_major, TAPE_HOTPLUG_CHAR_REMOVE);
}

static inline int
tapechar_check_idalbuffer(struct tape_device *device, size_t block_size)
{
	struct idal_buffer *new;

	/* Idal buffer must be the same size as the requested block size! */
	if (device->char_data.idal_buf != NULL &&
	    device->char_data.idal_buf->size == block_size)
		return 0;

	/* The current idal buffer is not big enough. Allocate a new one. */
	new = idal_buffer_alloc(block_size, 0);
	if (new == NULL)
		return -ENOMEM;
	if (device->char_data.idal_buf != NULL)
		idal_buffer_free(device->char_data.idal_buf);	
	device->char_data.idal_buf = new;
	return 0;
}

/*
 * Tape device read function
 */
ssize_t
tapechar_read (struct file *filp, char *data, size_t count, loff_t *ppos)
{
	struct tape_device *device;
	struct tape_request *request;
	size_t block_size;
	loff_t pos = *ppos;
	int rc;

        DBF_EVENT(6, "TCHAR:read\n");
	device = (struct tape_device *) filp->private_data;

	/* Check position. */
	if (ppos != &filp->f_pos) {
		/*
		 * "A request was outside the capabilities of the device."
		 * This check uses internal knowledge about how pread and
		 * read work...
		 */
	        DBF_EVENT(6, "TCHAR:ppos wrong\n");
		return -EOVERFLOW;
	}

	/*
	 * If the tape isn't terminated yet, do it now. And since we then
	 * are at the end of the tape there wouldn't be anything to read
	 * anyways. So we return immediatly.
	 */
	if(device->required_tapemarks) {
		return tape_std_terminate_write(device);
	}

	/* Find out block size to use */
	if (device->char_data.block_size != 0) {
		if (count < device->char_data.block_size) {
			DBF_EVENT(3, "TCHAR:read smaller than block "
				     "size was requested\n");
			return -EINVAL;
		}
		block_size = device->char_data.block_size;
	} else {
		block_size = count;
		rc = tapechar_check_idalbuffer(device, block_size);
		if (rc)
			return rc;
	}
	DBF_EVENT(6, "TCHAR:nbytes: %lx\n", block_size);
	/* Let the discipline build the ccw chain. */
	request = device->discipline->read_block(device, block_size);
	if (IS_ERR(request))
		return PTR_ERR(request);
	/* Execute it. */
	rc = tape_do_io(device, request);
	if (rc == 0) {
		rc = block_size - device->devstat.rescnt;
		DBF_EVENT(6, "TCHAR:rbytes:  %x\n", rc);
		pos += rc;
		/* Copy data from idal buffer to user space. */
		if (idal_buffer_to_user(device->char_data.idal_buf,
					data, rc) != 0)
			rc = -EFAULT;
		else
			*ppos = pos;
	}
	tape_put_request(request);
	return rc;
}

/*
 * Tape device write function
 */
ssize_t
tapechar_write(struct file *filp, const char *data, size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	struct tape_device *device;
	struct tape_request *request;
	size_t block_size;
	size_t written;
	int nblocks;
	int i, rc;

	DBF_EVENT(6, "TCHAR:write\n");
	device = (struct tape_device *) filp->private_data;
	/* Check position */
	if (ppos != &filp->f_pos) {
		/* "A request was outside the capabilities of the device." */
	        DBF_EVENT(6, "TCHAR:ppos wrong\n");
		return -EOVERFLOW;
	}
	/* Find out block size and number of blocks */
	if (device->char_data.block_size != 0) {
		if (count < device->char_data.block_size) {
			DBF_EVENT(3, "TCHAR:write smaller than block "
				    "size was requested\n");
			return -EINVAL;
		}
		block_size = device->char_data.block_size;
		nblocks = count / block_size;
	} else {
		block_size = count;
		rc = tapechar_check_idalbuffer(device, block_size);
		if (rc)
			return rc;
		nblocks = 1;
	}
	DBF_EVENT(6,"TCHAR:nbytes: %lx\n", block_size);
        DBF_EVENT(6, "TCHAR:nblocks: %x\n", nblocks);
	/* Let the discipline build the ccw chain. */
	request = device->discipline->write_block(device, block_size);
	if (IS_ERR(request))
		return PTR_ERR(request);
	rc = 0;
	written = 0;
	for (i = 0; i < nblocks; i++) {
		/* Copy data from user space to idal buffer. */
		if (idal_buffer_from_user(device->char_data.idal_buf,
					  data, block_size)) {
			rc = -EFAULT;
			break;
		}
		rc = tape_do_io(device, request);
		if (rc)
			break;
	        DBF_EVENT(6, "TCHAR:wbytes: %lx\n",
			  block_size - device->devstat.rescnt); 
		pos += block_size - device->devstat.rescnt;
		written += block_size - device->devstat.rescnt;
		if (device->devstat.rescnt != 0)
			break;
		data += block_size;
	}
	tape_put_request(request);

	if (rc == -ENOSPC) {
		/*
		 * Ok, the device has no more space. It has NOT written
		 * the block.
		 */
		if (device->discipline->process_eov)
			device->discipline->process_eov(device);
		if (written > 0)
			rc = 0;
	}

	/*
	 * After doing a write we always need two tapemarks to correctly
	 * terminate the tape (one to terminate the file, the second to
	 * flag the end of recorded data.
	 * Since process_eov positions the tape in front of the written
	 * tapemark it doesn't hurt to write two marks again.
	 */
	if(!rc) {
		device->required_tapemarks = 2;
		*ppos = pos;
	}

	return rc ? rc : written;
}

/*
 * Character frontend tape device open function.
 */
int
tapechar_open (struct inode *inode, struct file *filp)
{
	struct tape_device *device;
	int minor, rc;

	MOD_INC_USE_COUNT;
	if (major(filp->f_dentry->d_inode->i_rdev) != tapechar_major)
		return -ENODEV;
	minor = minor(filp->f_dentry->d_inode->i_rdev);
	device = tape_get_device(minor / TAPE_MINORS_PER_DEV);
	if (IS_ERR(device)) {
		MOD_DEC_USE_COUNT;
		return PTR_ERR(device);
	}
	DBF_EVENT(6, "TCHAR:open: %x\n", minor(inode->i_rdev));
	rc = tape_open(device);
	if (rc == 0) {
		rc = tape_assign(device, TAPE_STATUS_ASSIGN_A);
		if (rc == 0) {
			filp->private_data = device;
			return 0;
		}
		tape_release(device);
	}
	tape_put_device(device);
	MOD_DEC_USE_COUNT;
	return rc;
}

/*
 * Character frontend tape device release function.
 */

int
tapechar_release(struct inode *inode, struct file *filp)
{
	struct tape_device *device;

	device = (struct tape_device *) filp->private_data;
	DBF_EVENT(6, "TCHAR:release: %x\n", minor(inode->i_rdev));
			
	/*
	 * If this is the rewinding tape minor then rewind. In that case we
	 * write all required tapemarks. Otherwise only one to terminate the
	 * file.
	 */
	if ((minor(inode->i_rdev) & 1) != 0) {
		if(device->required_tapemarks)
			tape_std_terminate_write(device);
		tape_mtop(device, MTREW, 1);
	} else {
		if(device->required_tapemarks > 1) {
			if(tape_mtop(device, MTWEOF, 1) == 0)
				device->required_tapemarks--;
		}
	}

	if (device->char_data.idal_buf != NULL) {
		idal_buffer_free(device->char_data.idal_buf);
		device->char_data.idal_buf = NULL;
	}
	tape_unassign(device, TAPE_STATUS_ASSIGN_A);
	tape_release(device);
	filp->private_data = NULL; tape_put_device(device);
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Tape device io controls.
 */
static int
tapechar_ioctl(struct inode *inp, struct file *filp,
	   unsigned int no, unsigned long data)
{
	struct tape_device *device;
	int rc;

	DBF_EVENT(6, "TCHAR:ioct(%x)\n", no);

	device = (struct tape_device *) filp->private_data;

	if (no == MTIOCTOP) {
		struct mtop op;

		if (copy_from_user(&op, (char *) data, sizeof(op)) != 0)
			return -EFAULT;
		if (op.mt_count < 0)
			return -EINVAL;

		/*
		 * Operations that change tape position should write final
		 * tapemarks
		 */
		switch(op.mt_op) {
			case MTFSF:
			case MTBSF:
			case MTFSR:
			case MTBSR:
			case MTREW:
			case MTOFFL:
			case MTEOM:
			case MTRETEN:
			case MTBSFM:
			case MTFSFM:
			case MTSEEK:
				if(device->required_tapemarks)
					tape_std_terminate_write(device);
			default:
				;
		}
		rc = tape_mtop(device, op.mt_op, op.mt_count);

		if(op.mt_op == MTWEOF && rc == 0) {
			if(op.mt_count > device->required_tapemarks)
				device->required_tapemarks = 0;
			else
				device->required_tapemarks -= op.mt_count;
		}
		return rc;
	}
	if (no == MTIOCPOS) {
		/* MTIOCPOS: query the tape position. */
		struct mtpos pos;

		rc = tape_mtop(device, MTTELL, 1);
		if (rc < 0)
			return rc;
		pos.mt_blkno = rc;
		if (copy_to_user((char *) data, &pos, sizeof(pos)) != 0)
			return -EFAULT;
		return 0;
	}
	if (no == MTIOCGET) {
		/* MTIOCGET: query the tape drive status. */
		struct mtget get;

		memset(&get, 0, sizeof(get));
		get.mt_type = MT_ISUNKNOWN;
		get.mt_resid = device->devstat.rescnt;
		get.mt_dsreg = device->tape_status;
		/* FIXME: mt_erreg, mt_fileno */
		get.mt_gstat = device->tape_generic_status;

		if(device->medium_state == MS_LOADED) {
			rc = tape_mtop(device, MTTELL, 1);

			if(rc < 0)
				return rc;

			if(rc == 0)
				get.mt_gstat |= GMT_BOT(~0);

			get.mt_blkno = rc;
		}
		get.mt_erreg = 0;
		if (copy_to_user((char *) data, &get, sizeof(get)) != 0)
			return -EFAULT;
		return 0;
	}
	/* Try the discipline ioctl function. */
	if (device->discipline->ioctl_fn == NULL)
		return -EINVAL;
	return device->discipline->ioctl_fn(device, no, data);
}

/*
 * Initialize character device frontend.
 */
int
tapechar_init (void)
{
	int rc;

	/* Register the tape major number to the kernel */
#ifdef CONFIG_DEVFS_FS
	if (tapechar_major == 0)
		tapechar_major = devfs_alloc_major(DEVFS_SPECIAL_CHR);
#endif
        rc = register_chrdev(tapechar_major, "tape", &tape_fops);
	if (rc < 0) {
		PRINT_ERR("can't get major %d\n", tapechar_major);
		DBF_EVENT(3, "TCHAR:initfail\n");
		return rc;
	}
	if (tapechar_major == 0)
		tapechar_major = rc;  /* accept dynamic major number */
	PRINT_INFO("Tape gets major %d for char device\n", tapechar_major);
	DBF_EVENT(3, "Tape gets major %d for char device\n", rc);
        DBF_EVENT(3, "TCHAR:init ok\n");
	return 0;
}

/*
 * cleanup
 */
void
tapechar_exit(void)
{
	unregister_chrdev (tapechar_major, "tape");
}
