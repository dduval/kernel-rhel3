/*
 *  linux/drivers/scsi/scsi_dump.c
 *
 *  Copyright (C) 2004  FUJITSU LIMITED
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
 * Some codes are derived from drivers/scsi/sd.c
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <linux/blk.h>
#include <linux/blkpg.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include "scsi_dump.h"
#include <scsi/scsi_ioctl.h>
#include "constants.h"

#include <linux/genhd.h>
#include <linux/utsname.h>
#include <linux/crc32.h>
#include <linux/diskdump.h>
#include <linux/diskdumplib.h>
#include <linux/delay.h>

#define MAX_RETRIES 5
#define SD_TIMEOUT (60 * HZ)

#define DEBUG 0
#if DEBUG
# define Dbg(x, ...) printk(KERN_INFO "scsi_dump:" x "\n", ## __VA_ARGS__)
#else
# define Dbg(x...)
#endif

#define Err(x, ...)	printk(KERN_ERR "scsi_dump: " x "\n", ## __VA_ARGS__);
#define Warn(x, ...)	printk(KERN_WARNING "scsi_dump: " x "\n", ## __VA_ARGS__)
#define Info(x, ...)	printk(x "\n", ## __VA_ARGS__)

/* blocks to 512byte sectors */
#define BLOCK_SECTOR(s)	((s) << (DUMP_BLOCK_SHIFT - 9))


static int qcmnd_timeout = 5000000;
static int io_timeout = 30000000;
static int scmnd_timeout = 60000000;
MODULE_PARM(qcmnd_timeout, "i");
MODULE_PARM(io_timeout, "i");
MODULE_PARM(scmnd_timeout, "i");

static int quiesce_ok = 0;
static Scsi_Cmnd scsi_dump_cmnd;
static uint32_t module_crc;

static void rw_intr(Scsi_Cmnd * scmd)
{
	diskdump_del_timer(&scmd->eh_timeout);
	scmd->done = NULL;
}

static void eh_timeout(unsigned long data)
{
}

static struct scsi_dump_ops *get_dump_ops(Scsi_Host_Template *hostt)
{
	Scsi_Host_Template_dump *hostt_dump = (Scsi_Host_Template_dump *)hostt;
	struct scsi_dump_ops *ops;

	if (hostt->disk_dump)
		return hostt_dump->dump_ops;

	return scsi_dump_find_template(hostt);
}

/*
 * Common code to make Scsi_Cmnd
 */
static void init_scsi_command(Scsi_Device *sdev, Scsi_Cmnd *scmd, void *buf, int len, unsigned char direction, int set_lun)
{
	struct Scsi_Host *host	= scmd->host;

	scmd->sc_magic	= SCSI_CMND_MAGIC;
	scmd->owner	= SCSI_OWNER_MIDLEVEL;
	scmd->device	= sdev;
	scmd->host	= sdev->host;
	scmd->target	= sdev->id;
	scmd->lun	= sdev->lun;
	scmd->channel	= sdev->channel;
	scmd->buffer	= scmd->request_buffer = buf;
	scmd->bufflen	= scmd->request_bufflen = len;


	scmd->sc_data_direction = direction;

	memcpy(scmd->data_cmnd, scmd->cmnd, sizeof(scmd->cmnd));
	scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
	scmd->old_cmd_len = scmd->cmd_len;


	if (set_lun)
		scmd->cmnd[1] |= (sdev->scsi_level <= SCSI_2) ?
				  ((scmd->lun << 5) & 0xe0) : 0;

	scmd->transfersize = sdev->sector_size;
	if (direction == SCSI_DATA_WRITE)
		scmd->underflow = len;

	scmd->allowed = MAX_RETRIES;
	scmd->timeout_per_command = SD_TIMEOUT;

	/*
	 * This is the completion routine we use.  This is matched in terms
	 * of capability to this function.
	 */
	scmd->done = rw_intr;

	/*
	 * Some low driver put eh_timeout into the timer list.
	 */
	init_timer(&scmd->eh_timeout);
	scmd->eh_timeout.data		= (unsigned long)scmd;
	scmd->eh_timeout.function	= eh_timeout;
}

/* MODE SENSE */
static void init_mode_sense_command(Scsi_Device *sdev, Scsi_Cmnd *scmd, void *buf)
{
	memset(scmd, 0, sizeof(*scmd));
	scmd->cmnd[0] = MODE_SENSE;
	scmd->cmnd[1] = 0x00;		/* DBD=0 */
	scmd->cmnd[2] = 0x08;		/* PCF=0 Page 8(Cache) */
	scmd->cmnd[4] = 255;

	init_scsi_command(sdev, scmd, buf, 256, SCSI_DATA_READ, 1);
}

/* MODE SELECT */
static void init_mode_select_command(Scsi_Device *sdev, Scsi_Cmnd *scmd, void *buf, int len)
{
	memset(scmd, 0, sizeof(*scmd));
	scmd->cmnd[0] = MODE_SELECT;
	scmd->cmnd[1] = 0x10;		/* PF=1 SP=0 */
	scmd->cmnd[4] = len;

	init_scsi_command(sdev, scmd, buf, len, SCSI_DATA_WRITE, 1);
}

/* SYNCHRONIZE CACHE */
static void init_sync_command(Scsi_Device *sdev, Scsi_Cmnd * scmd)
{
	memset(scmd, 0, sizeof(*scmd));
	scmd->cmnd[0] = SYNCHRONIZE_CACHE;

	init_scsi_command(sdev, scmd, NULL, 0, SCSI_DATA_NONE, 0);
}

/* REQUEST SENSE */
static void init_sense_command(Scsi_Device *sdev, Scsi_Cmnd *scmd, void *buf)
{
	memset(scmd, 0, sizeof(*scmd));
	scmd->cmnd[0] = REQUEST_SENSE;
	scmd->cmnd[4] = 255;

	init_scsi_command(sdev, scmd, buf, 256, SCSI_DATA_READ, 1);
}

/* READ/WRITE */
static int init_rw_command(struct disk_dump_partition *dump_part, Scsi_Device *sdev, Scsi_Cmnd * scmd, int rw, int block, void *buf, unsigned int len)
{
	struct Scsi_Host *host	= scmd->host;
	struct disk_dump_device *dump_device = dump_part->device;
	int this_count = len >> 9;
	int target;

	memset(scmd, 0, sizeof(*scmd));

	if (block + this_count > dump_part->nr_sects) {
		Err("block number %d is larger than %lu",
				block + this_count, dump_part->nr_sects);
		return -EFBIG;
	}

	block += dump_part->start_sect;

	/*
	 * If we have a 1K hardware sectorsize, prevent access to single
	 * 512 byte sectors.  In theory we could handle this - in fact
	 * the scsi cdrom driver must be able to handle this because
	 * we typically use 1K blocksizes, and cdroms typically have
	 * 2K hardware sectorsizes.  Of course, things are simpler
	 * with the cdrom, since it is read-only.  For performance
	 * reasons, the filesystems should be able to handle this
	 * and not force the scsi disk driver to use bounce buffers
	 * for this.
	 */
	if (sdev->sector_size == 1024) {
		block = block >> 1;
		this_count = this_count >> 1;
	}
	if (sdev->sector_size == 2048) {
		block = block >> 2;
		this_count = this_count >> 2;
	}
	if (sdev->sector_size == 4096) {
		block = block >> 3;
		this_count = this_count >> 3;
	}
	switch (rw) {
	case WRITE:
		if (!sdev->writeable) {
			Err("writable media");
			return 0;
		}
		scmd->cmnd[0] = WRITE_10;
		break;
	case READ:
		scmd->cmnd[0] = READ_10;
		break;
	default:
		Err("Unknown command %d", scmd->request.cmd);
		return -EINVAL;
	}

	if (this_count > 0xffff)
		this_count = 0xffff;

	scmd->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
	scmd->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
	scmd->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
	scmd->cmnd[5] = (unsigned char) block & 0xff;
	scmd->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
	scmd->cmnd[8] = (unsigned char) this_count & 0xff;

	init_scsi_command(sdev, scmd, buf, len,
			(rw == WRITE ? SCSI_DATA_WRITE : SCSI_DATA_READ), 1);
	return 0;
}

/*
 * Check the status of scsi command and determine whether it is
 * success, fail, or retriable.
 *
 * Return code
 * 	> 0: should retry
 * 	= 0: success
 * 	< 0: fail
 */
static int cmd_result(Scsi_Cmnd *scmd)
{
	int status;

	status = status_byte(scmd->result);

	switch (scsi_decide_disposition(scmd)) {
	case FAILED:
		break;
	case NEEDS_RETRY:
	case ADD_TO_MLQUEUE:
		return 1 /* retry */;
	case SUCCESS:
		if (host_byte(scmd->result) == DID_RESET) {
			Err("host_byte(scmd->result) set to : DID_RESET");
			return 1;  /* retry */
		} else if ((status == CHECK_CONDITION) &&
			   ((scmd->sense_buffer[2] == UNIT_ATTENTION) ||
			    (scmd->sense_buffer[2] == NOT_READY))) {
			/*
			 * If we are expecting a CC/UA because of a bus reset that we
			 * performed, treat this just as a retry.  Otherwise this is
			 * information that we should pass up to the upper-level driver
			 * so that we can deal with it there.
			 */
			Err("CHECK_CONDITION and UNIT_ATTENTION");
			if (scmd->device->expecting_cc_ua) {
				Err("expecting_cc_ua is set. setting it to zero");
				scmd->device->expecting_cc_ua = 0;
				return 1; /* retry */
			}

			/* Retry if ASC is reset code */
			if (scmd->sense_buffer[12] == 0x29) {
				Err("ASC is 0x29");
				return 1; /* retry */
			}

			/*
			 * if the device is in the process of becoming ready, we
			 * should retry.
			 */
			if ((scmd->sense_buffer[12] == 0x04) &&
			    (scmd->sense_buffer[13] == 0x01)) {
				Err("device is in the process of becoming ready..");
				return 1; /* retry */
			}
		} else if (host_byte(scmd->result) != DID_OK) {
			Err("some undefined error");
			Err("host_byte(scmd->result) : %d", host_byte(scmd->result));
			break;
		}

		if (status == GOOD ||
		    status == CONDITION_GOOD ||
		    status == INTERMEDIATE_GOOD ||
		    status == INTERMEDIATE_C_GOOD)
			return 0;
		if (status == CHECK_CONDITION &&
		    scmd->sense_buffer[2] == RECOVERED_ERROR)
			return 0;
		break;
	default:
		Err("bad disposition: %d", scmd->result);
		return -EIO;
	}

	Err("command %x failed with 0x%x", scmd->cmnd[0], scmd->result);
	return -EIO;
}

static inline int send_command_wait(int *timeout, int *total_timeout)
{
	int wait = 100;

	udelay(wait);
	*timeout -= wait;
	*total_timeout -= wait;
	diskdump_update();

	return *timeout >= 0;
}

static int send_command(Scsi_Cmnd *scmd)
{
	struct Scsi_Host *host = scmd->host;
	Scsi_Device *sdev = scmd->device;
	struct scsi_dump_ops *dump_ops = get_dump_ops(host->hostt);
	int ret;
	int qcmnd_tmout, io_tmout, scmnd_tmout;

	BUG_ON(!dump_ops);

	scmnd_tmout = scmnd_timeout;
	do {
		qcmnd_tmout = qcmnd_timeout;
		io_tmout = io_timeout;

		if (!sdev->online) {
			Err("Scsi disk is not online");
			return -EIO;
		}
		if (sdev->changed) {
			Err("SCSI disk has been changed. Prohibiting further I/O");
			return -EIO;
		}

		for (;;) {
			spin_lock(host->host_lock);
			ret = host->hostt->queuecommand(scmd, rw_intr);
			spin_unlock(host->host_lock);
			if (ret == 0)
				break;
			dump_ops->poll(scmd->device);
			if (!send_command_wait(&qcmnd_tmout, &scmnd_tmout)) {
				ret = -EIO;
				goto retry_out;
			}
		}

		while (scmd->done != NULL) {
			dump_ops->poll(scmd->device);
			if (!send_command_wait(&io_tmout, &scmnd_tmout)) {
				ret = -EIO;
				goto retry_out;
			}
		}
		scmd->done = rw_intr;

		if ((ret = cmd_result(scmd)) <= 0)
			break;

		if (!send_command_wait(&qcmnd_tmout, &scmnd_tmout)) {
			ret = -EIO;
			goto retry_out;
		}
	} while (scmnd_tmout >= 0);

retry_out:
	return ret;
}

/*
 * If Write Cache Enable of disk device is not set, write I/O takes
 * long long time.  So enable WCE temporary and issue SYNCHRONIZE CACHE
 * after all write I/Os are done, Following system reboot will reset
 * WCE bit to original value.
 */
static void
enable_write_cache(Scsi_Device *sdev)
{
	char buf[256];
	int ret;
	int data_len;

	Dbg("enable write cache");
	memset(buf, 0, 256);

	init_mode_sense_command(sdev, &scsi_dump_cmnd, buf);
	if ((ret = send_command(&scsi_dump_cmnd)) < 0) {
		Warn(KERN_WARNING "MODE SENSE failed");
		return;
	}

	if (buf[14] & 0x04)		/* WCE is already set */
		return;

	data_len = buf[0] + 1; /* Data length in mode parameter header */
	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[12] &= 0x7f;		/* clear PS */
	buf[14] |= 0x04;		/* set WCE */

	init_mode_select_command(sdev, &scsi_dump_cmnd, buf, data_len);
	if ((ret = send_command(&scsi_dump_cmnd)) < 0) {
		Warn("MODE SELECT failed");

		init_sense_command(sdev, &scsi_dump_cmnd, buf);
		if ((ret = send_command(&scsi_dump_cmnd)) < 0) {
			Err("sense failed");
		}
	}
}

/*
 * Check whether the dump device is sane enough to handle I/O.
 *
 * Return value:
 * 	0:	the device is ok
 * 	< 0:	the device is not ok
 * 	> 0:	Cannot determine
 */
static int
scsi_dump_sanity_check(struct disk_dump_device *dump_device)
{
	Scsi_Device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;
	struct scsi_dump_ops *dump_ops = get_dump_ops(host->hostt);
	int adapter_sanity = 0;
	int sanity = 0;

	BUG_ON(!dump_ops);

	if (!check_crc_module()) {
		Err("checksum error. scsi dump module may be compromised.");
		return -EINVAL;
	}

	if (!sdev->online) {
		Warn("device not online: host %d channel %d id %d lun %d",
			host->host_no, sdev->channel, sdev->id, sdev->lun);
		return -EIO;
	}
	if (sdev->changed) {
		Err("SCSI disk has been changed. Prohibiting further I/O: host %d channel %d id %d lun %d",
			host->host_no, sdev->channel, sdev->id, sdev->lun);
		return -EIO;
	}

	if (dump_ops->sanity_check) {
		adapter_sanity = dump_ops->sanity_check(sdev);
		if (adapter_sanity < 0) {
			Warn("adapter status is not sane");
			return adapter_sanity;
		}
	}

	/*
	 * If host's spinlock is already taken, assume it's part
	 * of crash and skip it.
	 */
	if (!spin_is_locked(host->host_lock)) {
		sanity = 0;
	} else {
		Warn("host_lock is held: host %d channel %d id %d lun %d",
			host->host_no, sdev->channel, sdev->id, sdev->lun);
		if (host->host_lock == &io_request_lock)
			sanity = 1;
		else
			return -EIO;
	}
	return sanity + adapter_sanity;
}

static void reset_done(Scsi_Cmnd *scmd)
{
}

/*
 * Try to reset the host adapter. If the adapter does not have its host reset
 * handler, use its bus device reset handler.
 */
static int scsi_dump_reset(Scsi_Device *sdev)
{
	struct Scsi_Host *host = sdev->host;
	Scsi_Host_Template *hostt = host->hostt;
	char buf[256];
	int ret, i;

	init_sense_command(sdev, &scsi_dump_cmnd, buf);

	if (hostt->use_new_eh_code) {
		if (hostt->eh_host_reset_handler) {
			spin_lock(host->host_lock);
			ret = hostt->eh_host_reset_handler(&scsi_dump_cmnd);
			spin_unlock(host->host_lock);
		} else if (hostt->eh_bus_reset_handler) {
			spin_lock(host->host_lock);
			ret = hostt->eh_bus_reset_handler(&scsi_dump_cmnd);
			spin_unlock(host->host_lock);
		} else {
			return 0;
		}

		if (ret != SUCCESS) {
			Err("adapter reset failed");
			return -EIO;
		}
	} else {
		scsi_dump_cmnd.scsi_done = reset_done;

		spin_lock(host->host_lock);
		ret = hostt->reset(&scsi_dump_cmnd,
			SCSI_RESET_SYNCHRONOUS|SCSI_RESET_SUGGEST_HOST_RESET);
		spin_unlock(host->host_lock);
		if ((ret & SCSI_RESET_ACTION) != SCSI_RESET_SUCCESS) {
			Err("adapter reset failed");
			return -EIO;
		}
	}

	/* bus reset settle time. 5sec for old disk devices */
	for (i = 0; i < 5000; i++) {
		diskdump_update();
		mdelay(1);
	}

	Dbg("request sense");
	if ((ret = send_command(&scsi_dump_cmnd)) < 0)
		Warn("sense failed");
	return 0;
}

static int
scsi_dump_quiesce(struct disk_dump_device *dump_device)
{
	Scsi_Device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;
	struct scsi_dump_ops *dump_ops = get_dump_ops(host->hostt);
	int ret;
	int enable_cache;

	BUG_ON(!dump_ops);

	enable_cache = !dump_ops->no_write_cache_enable;

	if (dump_ops->quiesce) {
		ret = dump_ops->quiesce(sdev);
		if (ret < 0)
			return ret;
	}

	diskdump_register_poll(sdev, (void (*)(void *))dump_ops->poll);

	Dbg("do bus reset");
	if ((ret = scsi_dump_reset(sdev)) < 0)
		return ret;

	if (enable_cache && sdev->scsi_level >= SCSI_2)
		enable_write_cache(sdev);

	quiesce_ok = 1;
	return 0;
}

static int scsi_dump_rw_block(struct disk_dump_partition *dump_part, int rw, unsigned long dump_block_nr, void *buf, int len)
{
	struct disk_dump_device *dump_device = dump_part->device;
	Scsi_Device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;
	int block_nr = BLOCK_SECTOR(dump_block_nr);
	int ret;

	if (!quiesce_ok) {
		Err("quiesce not called");
		return -EIO;
	}

	ret = init_rw_command(dump_part, sdev, &scsi_dump_cmnd, rw,
					block_nr, buf, DUMP_BLOCK_SIZE * len);
	if (ret < 0) {
		Err("init_rw_command failed");
		return ret;
	}
	return send_command(&scsi_dump_cmnd);
}

static int
scsi_dump_shutdown(struct disk_dump_device *dump_device)
{
	Scsi_Device *sdev = dump_device->device;
	struct Scsi_Host *host = sdev->host;
	struct scsi_dump_ops *dump_ops = get_dump_ops(host->hostt);
	int ret;
	int enable_cache;

	BUG_ON(!dump_ops);

	enable_cache = !dump_ops->no_write_cache_enable;

	if (enable_cache && sdev->scsi_level >= SCSI_2) {
		init_sync_command(sdev, &scsi_dump_cmnd);
		send_command(&scsi_dump_cmnd);
	}

	if (dump_ops->shutdown)
		return dump_ops->shutdown(sdev);

	return 0;
}

static void *scsi_dump_probe(kdev_t dev)
{
	Scsi_Device *sdev;

	sdev = sd_find_scsi_device(dev);
	if (sdev == NULL)
		return NULL;
	if (!get_dump_ops(sdev->host->hostt))
		return NULL;

	return sdev;
}


struct disk_dump_device_ops scsi_dump_device_ops = {
	.sanity_check	= scsi_dump_sanity_check,
	.rw_block	= scsi_dump_rw_block,
	.quiesce	= scsi_dump_quiesce,
	.shutdown	= scsi_dump_shutdown,
};

static int scsi_dump_add_device(struct disk_dump_device *dump_device)
{
	Scsi_Device *sdev = dump_device->device;

	if (!get_dump_ops(sdev->host->hostt))
		return -ENOTSUPP;

	if (sdev->host->hostt->module)
		__MOD_INC_USE_COUNT(sdev->host->hostt->module);

	memcpy(&dump_device->ops, &scsi_dump_device_ops, sizeof(scsi_dump_device_ops));
	if (sdev->host->max_sectors) {
		dump_device->max_blocks = (sdev->sector_size * sdev->host->max_sectors) >> DUMP_BLOCK_SHIFT;
	}
	return 0;
}

static void scsi_dump_remove_device(struct disk_dump_device *dump_device)
{
	Scsi_Device *sdev = dump_device->device;

	if (sdev->host->hostt->module)
		__MOD_DEC_USE_COUNT(sdev->host->hostt->module);

}

static void scsi_dump_compute_cksum(void)
{
	set_crc_modules();
}

static struct disk_dump_type scsi_dump_type = {
	.probe		= scsi_dump_probe,
	.add_device	= scsi_dump_add_device,
	.remove_device	= scsi_dump_remove_device,
	.compute_cksum	= scsi_dump_compute_cksum,
	.owner		= THIS_MODULE,
};

static int init_scsi_dump(void)
{
	int ret;

	if ((ret = register_disk_dump_type(&scsi_dump_type)) < 0) {
		Err("register failed");
		return ret;
	}
	return ret;
}

static void cleanup_scsi_dump(void)
{
	if (unregister_disk_dump_type(&scsi_dump_type) < 0)
		Err("register failed");
}

module_init(init_scsi_dump);
module_exit(cleanup_scsi_dump);
MODULE_LICENSE("GPL");
