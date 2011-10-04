/*
 *  drivers/s390/char/tape_34xx.c
 *    tape device discipline for 3480/3490 tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001,2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 Stefan Bader <shbader@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/tape390.h>

#include "tape.h"
#include "tape_std.h"

#define PRINTK_HEADER "T34xx:"

/*
 * The block ID is the complete marker for a specific tape position.
 * It contains a physical part (wrap, segment, format) and a logical
 * block number.
 */
#define TBI_FORMAT_3480			0x00
#define TBI_FORMAT_3480_2_XF		0x01
#define TBI_FORMAT_3480_XF		0x02
#define TBI_FORMAT_RESERVED		0x03

struct tape_34xx_block_id {
        unsigned int			tbi_wrap	: 1;
        unsigned int			tbi_segment	: 7;
        unsigned int			tbi_format	: 2;
        unsigned int			tbi_block	: 22;
} __attribute__ ((packed));

struct sbid_entry {
	struct list_head		list;
	struct tape_34xx_block_id	bid;
};

struct tape_34xx_discdata {
	/* A list of block id's of the tape segments (for faster seek) */
	struct list_head		sbid_list;
};

/* Internal prototypes */
static void tape_34xx_clear_sbid_list(struct tape_device *);

/* 34xx specific functions */
static void
__tape_34xx_medium_sense_callback(struct tape_request *request, void *data)
{
	unsigned char *sense = request->cpdata;

	request->callback = NULL;

	DBF_EVENT(5, "TO_MSEN[0]: %08x\n", *((unsigned int *) sense));
	DBF_EVENT(5, "TO_MSEN[1]: %08x\n", *((unsigned int *) sense+1));
	DBF_EVENT(5, "TO_MSEN[2]: %08x\n", *((unsigned int *) sense+2));
	DBF_EVENT(5, "TO_MSEN[3]: %08x\n", *((unsigned int *) sense+3));

	if(sense[0] & SENSE_INTERVENTION_REQUIRED) {
		tape_med_state_set(request->device, MS_UNLOADED);
	} else {
		tape_med_state_set(request->device, MS_LOADED);
	}

	if(sense[1] & SENSE_WRITE_PROTECT) {
		request->device->tape_generic_status |= GMT_WR_PROT(~0);
	} else{
		request->device->tape_generic_status &= ~GMT_WR_PROT(~0);
	}

	tape_put_request(request);
}

static int
tape_34xx_medium_sense(struct tape_device *device)
{
	struct tape_request *	request;
	int			rc;

	tape_34xx_clear_sbid_list(device);

	request = tape_alloc_request(1, 32);
	if(IS_ERR(request)) {
		DBF_EXCEPTION(6, "MSN fail\n");
		return PTR_ERR(request);
	}

	request->op = TO_MSEN;
	tape_ccw_end(request->cpaddr, SENSE, 32, request->cpdata);
	request->callback = __tape_34xx_medium_sense_callback;

	rc = tape_do_io_async(device, request);

	return rc;
}

static void
tape_34xx_work_handler(void *data)
{
	struct {
		struct tape_device	*device;
		enum tape_op		 op;
		struct tq_struct	 task;
	} *p = data;

	switch(p->op) {
		case TO_MSEN:
			tape_34xx_medium_sense(p->device);
			break;
		default:
			DBF_EVENT(3, "T34XX: internal error: unknown work\n");
	}

	tape_put_device(p->device);
	kfree(p);
}

/*
 * This function is currently used to schedule a sense for later execution.
 * For example whenever a unsolicited interrupt signals a new tape medium
 * and we can't call tape_do_io from that interrupt handler.
 */
static int
tape_34xx_schedule_work(struct tape_device *device, enum tape_op op)
{
	struct {
		struct tape_device	*device;
		enum tape_op		 op;
		struct tq_struct	 task;
	} *p;

	if ((p = kmalloc(sizeof(*p), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	memset(p, 0, sizeof(*p));
	INIT_LIST_HEAD(&p->task.list);
	p->task.routine = tape_34xx_work_handler;
	p->task.data    = p;

	p->device = tape_clone_device(device);
	p->op     = op;

	schedule_task(&p->task);

	return 0;
}

/*
 * Done Handler is called when dev stat = DEVICE-END (successful operation)
 */
static int
tape_34xx_done(struct tape_device *device, struct tape_request *request)
{
	DBF_EVENT(6, "%s done\n", tape_op_verbose[request->op]);
	// FIXME: Maybe only on assign/unassign
	TAPE_CLEAR_STATE(device, TAPE_STATUS_BOXED);

	return TAPE_IO_SUCCESS;
}

static inline int
tape_34xx_erp_failed(struct tape_device *device,
		     struct tape_request *request, int rc)
{
	DBF_EVENT(3, "Error recovery failed for %s\n",
		  tape_op_verbose[request->op]);
	return rc;
}

static inline int
tape_34xx_erp_succeeded(struct tape_device *device,
		       struct tape_request *request)
{
	DBF_EVENT(3, "Error Recovery successful for %s\n",
		  tape_op_verbose[request->op]);
	return tape_34xx_done(device, request);
}

static inline int
tape_34xx_erp_retry(struct tape_device *device, struct tape_request *request)
{
	DBF_EVENT(3, "xerp retr %s\n",
		  tape_op_verbose[request->op]);
	return TAPE_IO_RETRY;
}

/*
 * This function is called, when no request is outstanding and we get an
 * interrupt
 */
static int
tape_34xx_unsolicited_irq(struct tape_device *device)
{
	if (device->devstat.dstat == 0x85 /* READY */) {
		/* A medium was inserted in the drive. */
		DBF_EVENT(6, "T34xx: tape load\n");
		tape_34xx_schedule_work(device, TO_MSEN);
	} else {
		DBF_EVENT(3, "T34xx: unsol.irq! dev end: %x\n",
			  device->devinfo.irq);
		PRINT_WARN("Unsolicited IRQ (Device End) caught.\n");
		tape_dump_sense(device, NULL);
	}
	return TAPE_IO_SUCCESS;
}

/*
 * Read Opposite Error Recovery Function:
 * Used, when Read Forward does not work
 */
static int
tape_34xx_erp_read_opposite(struct tape_device *device,
			    struct tape_request *request)
{
	if (request->op == TO_RFO) {
		/*
		 * We did read forward, but the data could not be read
		 * *correctly*. We transform the request to a read backward
		 * and try again.
		 */
		tape_std_read_backward(device, request);
		return tape_34xx_erp_retry(device, request);
	}
	if (request->op != TO_RBA)
		PRINT_ERR("read_opposite called with state:%s\n",
			  tape_op_verbose[request->op]);
	/*
	 * We tried to read forward and backward, but hat no
	 * success -> failed.
	 */
	return tape_34xx_erp_failed(device, request, -EIO);
}

static int
tape_34xx_erp_bug(struct tape_device *device,
		  struct tape_request *request, int no)
{
	if (request->op != TO_ASSIGN) {
		PRINT_WARN("An unexpected condition #%d was caught in "
			   "tape error recovery.\n", no);
		PRINT_WARN("Please report this incident.\n");
		if (request)
			PRINT_WARN("Operation of tape:%s\n",
				   tape_op_verbose[request->op]);
		tape_dump_sense(device, request);
	}
	return tape_34xx_erp_failed(device, request, -EIO);
}

/*
 * Handle data overrun between cu and drive. The channel speed might
 * be too slow.
 */
static int
tape_34xx_erp_overrun(struct tape_device *device, struct tape_request *request)
{
	if (device->devstat.ii.sense.data[3] == 0x40) {
		PRINT_WARN ("Data overrun error between control-unit "
			    "and drive. Use a faster channel connection, "
			    "if possible! \n");
		return tape_34xx_erp_failed(device, request, -EIO);
	}
	return tape_34xx_erp_bug(device, request, -1);
}
	
/*
 * Handle record sequence error.
 */
static int
tape_34xx_erp_sequence(struct tape_device *device,
		       struct tape_request *request)
{
	if (device->devstat.ii.sense.data[3] == 0x41) {
		/*
		 * cu detected incorrect block-id sequence on tape.
		 */
		PRINT_WARN("Illegal block-id sequence found!\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	}
	/*
	 * Record sequence error bit is set, but erpa does not
	 * show record sequence error.
	 */
	return tape_34xx_erp_bug(device, request, -2);
}

/*
 * This function analyses the tape's sense-data in case of a unit-check.
 * If possible, it tries to recover from the error. Else the user is
 * informed about the problem.
 */
static int
tape_34xx_unit_check(struct tape_device *device, struct tape_request *request)
{
	int inhibit_cu_recovery;
	__u8* sense;

	inhibit_cu_recovery = (*device->modeset_byte & 0x80) ? 1 : 0;
	sense = device->devstat.ii.sense.data;

#ifdef CONFIG_S390_TAPE_BLOCK
	if (request->op == TO_BLOCK) {
		/*
		 * Recovery for block device requests. Set the block_position
		 * to something invalid and retry.
		 */
		device->blk_data.block_position = -1;
		if (request->retries-- <= 0)
			return tape_34xx_erp_failed(device, request, -EIO);
		else
			return tape_34xx_erp_retry(device, request);
	}
#endif

	if (
		sense[0] & SENSE_COMMAND_REJECT &&
		sense[1] & SENSE_WRITE_PROTECT
	) {
		if (
			request->op == TO_DSE ||
			request->op == TO_WRI ||
			request->op == TO_WTM
		) {
			/* medium is write protected */
			return tape_34xx_erp_failed(device, request, -EACCES);
		} else {
			return tape_34xx_erp_bug(device, request, -3);
		}
	}

	/*
	 * special cases for various tape-states when reaching
	 * end of recorded area
	 */
	/*
	 * FIXME: Maybe a special case of the special case:
	 *        sense[0] == SENSE_EQUIPMENT_CHECK &&
	 *        sense[1] == SENSE_DRIVE_ONLINE    &&
	 *        sense[3] == 0x47 (Volume Fenced)
	 *
	 *        This was caused by continued FSF or FSR after an
	 *        'End Of Data'.
	 */
	if ((
		sense[0] == SENSE_DATA_CHECK      ||
		sense[0] == SENSE_EQUIPMENT_CHECK ||
		sense[0] == SENSE_EQUIPMENT_CHECK + SENSE_DEFERRED_UNIT_CHECK
	) && (
		sense[1] == SENSE_DRIVE_ONLINE ||
		sense[1] == SENSE_BEGINNING_OF_TAPE + SENSE_WRITE_MODE
	)) {
		switch (request->op) {
		/*
		 * sense[0] == SENSE_DATA_CHECK   &&
		 * sense[1] == SENSE_DRIVE_ONLINE
		 * sense[3] == 0x36 (End Of Data)
		 *
		 * Further seeks might return a 'Volume Fenced'.
		 */
		case TO_FSF:
		case TO_FSB:
			/* Trying to seek beyond end of recorded area */
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		case TO_BSB:
			return tape_34xx_erp_retry(device, request);
		/*
		 * sense[0] == SENSE_DATA_CHECK   &&
		 * sense[1] == SENSE_DRIVE_ONLINE &&
		 * sense[3] == 0x36 (End Of Data)
		 */
		case TO_LBL:
			/* Block could not be located. */
			return tape_34xx_erp_failed(device, request, -EIO);
		case TO_RFO:
			/* Read beyond end of recorded area -> 0 bytes read */
			return tape_34xx_erp_failed(device, request, 0);
		default:
			PRINT_ERR("Invalid op %s in %s:%i\n",
				tape_op_verbose[request->op],
				__FUNCTION__, __LINE__);
			return tape_34xx_erp_failed(device, request, 0);
		}
	}

	/* Sensing special bits */
	if (sense[0] & SENSE_BUS_OUT_CHECK)
		return tape_34xx_erp_retry(device, request);

	if (sense[0] & SENSE_DATA_CHECK) {
		/*
		 * hardware failure, damaged tape or improper
		 * operating conditions
		 */
		switch (sense[3]) {
		case 0x23:
			/* a read data check occurred */
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_34xx_erp_bug(device, request, -4);

			/* data check is permanent, CU recovery has failed */
			PRINT_WARN("Permanent read error\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x25:
			// a write data check occurred
			if ((sense[2] & SENSE_TAPE_SYNC_MODE) ||
			    inhibit_cu_recovery)
				// data check is not permanent, may be
				// recovered. We always use async-mode with
				// cu-recovery, so this should *never* happen.
				return tape_34xx_erp_bug(device, request, -5);

			// data check is permanent, cu-recovery has failed
			PRINT_WARN("Permanent write error\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x26:
			/* Data Check (read opposite) occurred. */
			return tape_34xx_erp_read_opposite(device, request);
		case 0x28:
			/* ID-Mark at tape start couldn't be written */
			PRINT_WARN("ID-Mark could not be written.\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		case 0x31:
			/* Tape void. Tried to read beyond end of device. */
			PRINT_WARN("Read beyond end of recorded area.\n");
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		case 0x41:
			/* Record sequence error. */
			PRINT_WARN("Invalid block-id sequence found.\n");
			return tape_34xx_erp_failed(device, request, -EIO);
		default:
			/* all data checks for 3480 should result in one of
			 * the above erpa-codes. For 3490, other data-check
			 * conditions do exist. */
			if (device->discipline->cu_type == 0x3480)
				return tape_34xx_erp_bug(device, request, -6);
		}
	}

	if (sense[0] & SENSE_OVERRUN)
		return tape_34xx_erp_overrun(device, request);
	
	if (sense[1] & SENSE_RECORD_SEQUENCE_ERR)
		return tape_34xx_erp_sequence(device, request);

	/* Sensing erpa codes */
	switch (sense[3]) {
	case 0x00:
		/* Unit check with erpa code 0. Report and ignore. */
		PRINT_WARN("Non-error sense was found. "
			   "Unit-check will be ignored.\n");
		return TAPE_IO_SUCCESS;
	case 0x21:
		/*
		 * Data streaming not operational. CU will switch to
		 * interlock mode. Reissue the command.
		 */
		PRINT_WARN("Data streaming not operational. "
			   "Switching to interlock-mode.\n");
		return tape_34xx_erp_retry(device, request);
	case 0x22:
		/*
		 * Path equipment check. Might be drive adapter error, buffer
		 * error on the lower interface, internal path not usable,
		 * or error during cartridge load.
		 */
		PRINT_WARN("A path equipment check occurred. One of the "
			   "following conditions occurred:\n");
		PRINT_WARN("drive adapter error, buffer error on the lower "
			   "interface, internal path not usable, error "
			   "during cartridge load.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x24:
		/*
		 * Load display check. Load display was command was issued,
		 * but the drive is displaying a drive check message. Can
		 * be threated as "device end".
		 */
		return tape_34xx_erp_succeeded(device, request);
	case 0x27:
		/*
		 * Command reject. May indicate illegal channel program or
		 * buffer over/underrun. Since all channel programs are
		 * issued by this driver and ought be correct, we assume a
		 * over/underrun situation and retry the channel program.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x29:
		/*
		 * Function incompatible. Either the tape is idrc compressed
		 * but the hardware isn't capable to do idrc, or a perform
		 * subsystem func is issued and the CU is not on-line.
		 */
		PRINT_WARN ("Function incompatible. Try to switch off idrc\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x2a:
		/*
		 * Unsolicited environmental data. An internal counter
		 * overflows, we can ignore this and reissue the cmd.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x2b:
		/*
		 * Environmental data present. Indicates either unload
		 * completed ok or read buffered log command completed ok.
		 */
		if (request->op == TO_RUN) {
			tape_med_state_set(device, MS_UNLOADED);
			/* Rewind unload completed ok. */
			return tape_34xx_erp_succeeded(device, request);
		}
		/* tape_34xx doesn't use read buffered log commands. */
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x2c:
		/*
		 * Permanent equipment check. CU has tried recovery, but
		 * did not succeed.
		 */
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x2d:
		/* Data security erase failure. */
		if (request->op == TO_DSE)
			return tape_34xx_erp_failed(device, request, -EIO);
		/* Data security erase failure, but no such command issued. */
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x2e:
		/*
		 * Not capable. This indicates either that the drive fails
		 * reading the format id mark or that that format specified
		 * is not supported by the drive.
		 */
		PRINT_WARN("Drive not capable processing the tape format!");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);
	case 0x30:
		/* The medium is write protected. */
		PRINT_WARN("Medium is write protected!\n");
		return tape_34xx_erp_failed(device, request, -EACCES);
	case 0x32:
		// Tension loss. We cannot recover this, it's an I/O error.
		PRINT_WARN("The drive lost tape tension.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x33:
		/*
		 * Load Failure. The cartridge was not inserted correctly or
		 * the tape is not threaded correctly.
		 */
		PRINT_WARN("Cartridge load failure. Reload the cartridge "
			   "and try again.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x34:
		/*
		 * Unload failure. The drive cannot maintain tape tension
		 * and control tape movement during an unload operation.
		 */
		PRINT_WARN("Failure during cartridge unload. "
			   "Please try manually.\n");
		if (request->op == TO_RUN)
			return tape_34xx_erp_failed(device, request, -EIO);
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x35:
		/*
		 * Drive equipment check. One of the following:
		 * - cu cannot recover from a drive detected error
		 * - a check code message is shown on drive display
		 * - the cartridge loader does not respond correctly
		 * - a failure occurs during an index, load, or unload cycle
		 */
		PRINT_WARN("Equipment check! Please check the drive and "
			   "the cartridge loader.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x36:
		if (device->discipline->cu_type == 0x3490)
			/* End of data. */
			return tape_34xx_erp_failed(device, request, -EIO);
		/* This erpa is reserved for 3480 */
		return tape_34xx_erp_bug(device,request,sense[3]);
	case 0x37:
		/*
		 * Tape length error. The tape is shorter than reported in
		 * the beginning-of-tape data.
		 */
		PRINT_WARN("Tape length error.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x38:
		/*
		 * Physical end of tape. A read/write operation reached
		 * the physical end of tape.
		 */
		if (request->op==TO_WRI ||
		    request->op==TO_DSE ||
		    request->op==TO_WTM)
			return tape_34xx_erp_failed(device, request, -ENOSPC);
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x39:
		/* Backward at Beginning of tape. */
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x3a:
		/* Drive switched to not ready. */
		PRINT_WARN("Drive not ready. Turn the ready/not ready switch "
			   "to ready position and try again.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x3b:
		/* Manual rewind or unload. This causes an I/O error. */
		PRINT_WARN("Medium was rewound or unloaded manually.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x42:
		/*
		 * Degraded mode. A condition that can cause degraded
		 * performance is detected.
		 */
		PRINT_WARN("Subsystem is running in degraded mode.\n");
		return tape_34xx_erp_retry(device, request);
	case 0x43:
		/* Drive not ready. */
		tape_med_state_set(device, MS_UNLOADED);
		/* SMB: some commands do not need a tape inserted */
		if((sense[1] & SENSE_DRIVE_ONLINE)) {
			switch(request->op) {
				case TO_ASSIGN:
				case TO_UNASSIGN:
				case TO_DIS:
					return tape_34xx_done(device, request);
					break;
				default:
					break;
			}
		}
		PRINT_WARN("The drive is not ready.\n");
		return tape_34xx_erp_failed(device, request, -ENOMEDIUM);
	case 0x44:
		/* Locate Block unsuccessful. */
		if (request->op != TO_BLOCK && request->op != TO_LBL)
			/* No locate block was issued. */
			return tape_34xx_erp_bug(device, request, sense[3]);
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x45:
		/* The drive is assigned to a different channel path. */
		PRINT_WARN("The drive is assigned elsewhere.\n");
		TAPE_SET_STATE(device, TAPE_STATUS_BOXED);
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x46:
		/*
		 * Drive not on-line. Drive may be switched offline,
		 * the power supply may be switched off or
		 * the drive address may not be set correctly.
		 */
		PRINT_WARN("The drive is not on-line.");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x47:
		/* Volume fenced. CU reports volume integrity is lost. */
		PRINT_WARN("Volume fenced. The volume integrity is lost because\n");
		PRINT_WARN("assignment or tape position was lost.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x48:
		/* Log sense data and retry request. */
		return tape_34xx_erp_retry(device, request);
	case 0x49:
		/* Bus out check. A parity check error on the bus was found. */
		PRINT_WARN("Bus out check. A data transfer over the bus "
			   "has been corrupted.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4a:
		/* Control unit erp failed. */
		PRINT_WARN("The control unit I/O error recovery failed.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4b:
		/*
		 * CU and drive incompatible. The drive requests micro-program
		 * patches, which are not available on the CU.
		 */
		PRINT_WARN("The drive needs microprogram patches from the "
			   "control unit, which are not available.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x4c:
		/*
		 * Recovered Check-One failure. Cu develops a hardware error,
		 * but is able to recover.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x4d:
		if (device->discipline->cu_type == 0x3490)
			/*
			 * Resetting event received. Since the driver does
			 * not support resetting event recovery (which has to
			 * be handled by the I/O Layer), retry our command.
			 */
			return tape_34xx_erp_retry(device, request);
		/* This erpa is reserved for 3480. */
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x4e:
		if (device->discipline->cu_type == 0x3490) {
			/*
			 * Maximum block size exceeded. This indicates, that
			 * the block to be written is larger than allowed for
			 * buffered mode.
			 */
			PRINT_WARN("Maximum block size for buffered "
				   "mode exceeded.\n");
			return tape_34xx_erp_failed(device, request, -ENOBUFS);
		}
		/* This erpa is reserved for 3480. */
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x50:
		/*
		 * Read buffered log (Overflow). CU is running in extended
		 * buffered log mode, and a counter overflows. This should
		 * never happen, since we're never running in extended
		 * buffered log mode.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x51:
		/*
		 * Read buffered log (EOV). EOF processing occurs while the
		 * CU is in extended buffered log mode. This should never
		 * happen, since we're never running in extended buffered
		 * log mode.
		 */
		return tape_34xx_erp_retry(device, request);
	case 0x52:
		/* End of Volume complete. Rewind unload completed ok. */
		if (request->op == TO_RUN) {
			/* SMB */
			tape_med_state_set(device, MS_UNLOADED);
			return tape_34xx_erp_succeeded(device, request);
		}
		return tape_34xx_erp_bug(device, request, sense[3]);
	case 0x53:
		/* Global command intercept. */
		return tape_34xx_erp_retry(device, request);
	case 0x54:
		/* Channel interface recovery (temporary). */
		return tape_34xx_erp_retry(device, request);
	case 0x55:
		/* Channel interface recovery (permanent). */
		PRINT_WARN("A permanent channel interface error occurred.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x56:
		/* Channel protocol error. */
		PRINT_WARN("A channel protocol error occurred.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x57:
		if (device->discipline->cu_type == 0x3480) {
			/* Attention intercept. */
			PRINT_WARN("An attention intercept occurred, "
				   "which will be recovered.\n");
			return tape_34xx_erp_retry(device, request);
		} else {
			/* Global status intercept. */
			PRINT_WARN("An global status intercept was received, "
				   "which will be recovered.\n");
			return tape_34xx_erp_retry(device, request);
		}
	case 0x5a:
		/*
		 * Tape length incompatible. The tape inserted is too long,
		 * which could cause damage to the tape or the drive.
		 */
		PRINT_WARN("Tape length incompatible [should be IBM Cartridge "
			   "System Tape]. May cause damage to drive or tape.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5b:
		/* Format 3480 XF incompatible */
		if (sense[1] & SENSE_BEGINNING_OF_TAPE)
			/* The tape will get overwritten. */
			return tape_34xx_erp_retry(device, request);
		PRINT_WARN("Tape format is incompatible to the drive, "
			   "which writes 3480-2 XF.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5c:
		/* Format 3480-2 XF incompatible */
		PRINT_WARN("Tape format is incompatible to the drive. "
			   "The drive cannot access 3480-2 XF volumes.\n");
		return tape_34xx_erp_failed(device, request, -EIO);
	case 0x5d:
		/* Tape length violation. */
		PRINT_WARN("Tape length violation [should be IBM Enhanced "
			   "Capacity Cartridge System Tape]. May cause "
			   "damage to drive or tape.\n");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);
	case 0x5e:
		/* Compaction algorithm incompatible. */
		PRINT_WARN("The volume is recorded using an incompatible "
			   "compaction algorithm, which is not supported by "
			   "the control unit.\n");
		return tape_34xx_erp_failed(device, request, -EMEDIUMTYPE);

		/* The following erpas should have been covered earlier. */
	case 0x23: /* Read data check. */
	case 0x25: /* Write data check. */
	case 0x26: /* Data check (read opposite). */
	case 0x28: /* Write id mark check. */
	case 0x31: /* Tape void. */
	case 0x40: /* Overrun error. */
	case 0x41: /* Record sequence error. */
		/* All other erpas are reserved for future use. */
	default:
		return tape_34xx_erp_bug(device, request, sense[3]);
	}
}

/*
 * 3480/3490 interrupt handler
 */
static int
tape_34xx_irq(struct tape_device *device, struct tape_request *request)
{
	if (request == NULL)
		return tape_34xx_unsolicited_irq(device);

	if ((device->devstat.dstat & DEV_STAT_UNIT_EXCEP) &&
	    (device->devstat.dstat & DEV_STAT_DEV_END) &&
	    (request->op == TO_WRI)) {
		/* Write at end of volume */
		PRINT_INFO("End of volume\n"); /* XXX */
		return tape_34xx_erp_failed(device, request, -ENOSPC);
	}

	if ((device->devstat.dstat & DEV_STAT_UNIT_EXCEP) &&
	    (request->op == TO_BSB || request->op == TO_FSB))
		DBF_EVENT(5, "Skipped over tapemark\n");

	if (device->devstat.dstat & DEV_STAT_UNIT_CHECK)
		return tape_34xx_unit_check(device, request);

	if (device->devstat.dstat & DEV_STAT_DEV_END)
		return tape_34xx_done(device, request);

	DBF_EVENT(6, "xunknownirq\n");
	PRINT_ERR("Unexpected interrupt.\n");
	PRINT_ERR("Current op is: %s", tape_op_verbose[request->op]);
	tape_dump_sense(device, request);
	return TAPE_IO_STOP;
}

/*
 * ioctl_overload
 */
static int
tape_34xx_ioctl(struct tape_device *device, unsigned int cmd, unsigned long arg)
{
        if (cmd == TAPE390_DISPLAY) {
		struct display_struct disp;

		if(copy_from_user(&disp, (char *) arg, sizeof(disp)) != 0)
			return -EFAULT;

	        return tape_std_display(device, &disp);
	} else
                return -EINVAL;
}

static int
tape_34xx_setup_device(struct tape_device * device)
{
	struct tape_34xx_discdata *discdata;

	DBF_EVENT(5, "tape_34xx_setup_device(%p)\n", device);
	DBF_EVENT(6, "34xx minor1: %x\n", device->first_minor);
	discdata = kmalloc(sizeof(struct tape_34xx_discdata), GFP_ATOMIC);
	if(discdata) {
		memset(discdata, 0, sizeof(struct tape_34xx_discdata));
		INIT_LIST_HEAD(&discdata->sbid_list);
		device->discdata = discdata;
	}
	tape_34xx_medium_sense(device);
	return 0;
}

static void
tape_34xx_cleanup_device(struct tape_device * device)
{
	if (device->discdata) {
		tape_34xx_clear_sbid_list(device);
		kfree(device->discdata);
		device->discdata = NULL;
	}
}

/*
 * Build up the lookup table...
 */
static void
tape_34xx_add_sbid(struct tape_device *device, struct tape_34xx_block_id bid)
{
	struct tape_34xx_discdata *	discdata = device->discdata;
	struct sbid_entry *		new;
	struct sbid_entry *		cur;
	struct list_head *		l;

	if(discdata == NULL)
		return;
	if((new = kmalloc(sizeof(struct sbid_entry), GFP_ATOMIC)) == NULL)
		return;

	new->bid = bid;
	new->bid.tbi_format = 0;

	/*
	 * Search the position where to insert the new entry. It is possible
	 * that the entry should not be added but the block number has to be
	 * updated to approximate the logical block, where a segment starts.
	 */
	list_for_each(l, &discdata->sbid_list) {
		cur = list_entry(l, struct sbid_entry, list);

		/*
		 * If the current entry has the same segment and wrap, then
		 * there is no new entry needed. Only the block number of the
		 * current entry might be adjusted to reflect an earlier start
		 * of the segment.
		 */
		if(
			(cur->bid.tbi_segment == new->bid.tbi_segment) &&
			(cur->bid.tbi_wrap    == new->bid.tbi_wrap)
		) {
			if(new->bid.tbi_block < cur->bid.tbi_block) {
				cur->bid.tbi_block = new->bid.tbi_block;
			}
			kfree(new);
			break;
		}

		/*
		 * Otherwise the list is sorted by block number because it
		 * is alway ascending while the segment number decreases on
		 * the second wrap.
		 */
		if(cur->bid.tbi_block > new->bid.tbi_block) {
			list_add_tail(&new->list, l);
			break;
		}
	}

	/*
	 * The loop went through without finding a merge or adding an entry
	 * add the new entry to the end of the list.
	 */
	if(l == &discdata->sbid_list) {
		list_add_tail(&new->list, &discdata->sbid_list);
	}

	list_for_each(l, &discdata->sbid_list) {
		cur = list_entry(l, struct sbid_entry, list);

		DBF_EVENT(3, "sbid_list(%03i:%1i:%08i)\n",
			cur->bid.tbi_segment, cur->bid.tbi_wrap,
			cur->bid.tbi_block);
	}

	return;	
}

/*
 * Fill hardware positioning information into the given block id. With that
 * seeks don't have to go back to the beginning of the tape and are done at
 * faster speed because the vicinity of a segment can be located at faster
 * speed.
 *
 * The caller must have set tbi_block.
 */
static void
tape_34xx_merge_sbid(
	struct tape_device *		device,
	struct tape_34xx_block_id *	bid
) {
	struct tape_34xx_discdata *	discdata = device->discdata;
	struct sbid_entry *		cur;
	struct list_head *		l;

	bid->tbi_wrap    = 0;
	bid->tbi_segment = 1;
	bid->tbi_format  = (*device->modeset_byte & 0x08) ?
				TBI_FORMAT_3480_XF : TBI_FORMAT_3480;

	if(discdata == NULL)
		goto tape_34xx_merge_sbid_exit;
	if(list_empty(&discdata->sbid_list))
		goto tape_34xx_merge_sbid_exit;

	list_for_each(l, &discdata->sbid_list) {
		cur = list_entry(l, struct sbid_entry, list);

		if(cur->bid.tbi_block > bid->tbi_block)
			break;
	}

	/* If block comes before first entries block, use seek from start. */
	if(l->prev == &discdata->sbid_list)
		goto tape_34xx_merge_sbid_exit;

	cur = list_entry(l->prev, struct sbid_entry, list);
	bid->tbi_wrap    = cur->bid.tbi_wrap;
	bid->tbi_segment = cur->bid.tbi_segment;

tape_34xx_merge_sbid_exit:
	DBF_EVENT(6, "merged_bid = %08x\n", *((unsigned int *) bid));
	return;
}

static void
tape_34xx_clear_sbid_list(struct tape_device *device)
{
	struct list_head *		l;
	struct list_head *		n;
	struct tape_34xx_discdata *	discdata = device->discdata;

	list_for_each_safe(l, n, &discdata->sbid_list) {
		list_del(l);
		kfree(list_entry(l, struct sbid_entry, list));
	}
}

/*
 * MTTELL: Tell block. Return the number of block relative to current file.
 */
int
tape_34xx_mttell(struct tape_device *device, int mt_count)
{
	struct tape_34xx_block_id	bid;
	int				rc;

	rc = tape_std_read_block_id(device, (unsigned int *) &bid);
	if (rc)
		return rc;

	/*
	 * Build up a lookup table. The format id is ingored.
	 */
	tape_34xx_add_sbid(device, bid);

	return bid.tbi_block;
}

/*
 * MTSEEK: seek to the specified block.
 */
int
tape_34xx_mtseek(struct tape_device *device, int mt_count)
{
	struct tape_34xx_block_id	bid;

	if (mt_count > 0x400000) {
		DBF_EXCEPTION(6, "xsee parm\n");
		return -EINVAL;
	}

	bid.tbi_block   = mt_count;

	/*
	 * Set hardware seek information in the block id.
	 */
	tape_34xx_merge_sbid(device, &bid);

	return tape_std_seek_block_id(device, *((unsigned int *) &bid));
}

/*
 * Tape block read for 34xx.
 */
#ifdef CONFIG_S390_TAPE_BLOCK
struct tape_request *
tape_34xx_bread(struct tape_device *device, struct request *req)
{
	struct tape_request  *request;
	struct buffer_head   *bh;
	ccw1_t               *ccw;
	int                   count;
	int                   size;

	DBF_EVENT(6, "tape_34xx_bread(sector=%u,size=%u)\n",
		req->sector, req->nr_sectors);

	/* Count the number of blocks for the request. */
	count = 0;
	size  = 0;
	for(bh = req->bh; bh; bh = bh->b_reqnext) {
		for(size = 0; size < bh->b_size; size += TAPEBLOCK_HSEC_SIZE)
			count++;
	}

	/* Allocate the ccw request. */
	request = tape_alloc_request(3+count+1, 8);
	if (IS_ERR(request))
		return request;

	/*
	 * Setup the tape block id to start the read from. The block number
	 * is later compared to the current position to decide whether a
	 * locate block is required. If one is needed this block id is used
	 * to locate it.
	 */
	((struct tape_34xx_block_id *) request->cpdata)->tbi_block =
		req->sector >> TAPEBLOCK_HSEC_S2B;

	/* Setup ccws. */
	request->op = TO_BLOCK;
	ccw = request->cpaddr;
	ccw = tape_ccw_cc(ccw, MODE_SET_DB, 1, device->modeset_byte);

	/*
	 * We always setup a nop after the mode set ccw. This slot is
	 * used in tape_std_check_locate to insert a locate ccw if the
	 * current tape position doesn't match the start block to be read.
	 * The second nop will be filled with a read block id which is in
	 * turn used by tape_34xx_free_bread to populate the segment bid
	 * table.
	 */
	ccw = tape_ccw_cc(ccw, NOP, 0, NULL);
	ccw = tape_ccw_cc(ccw, NOP, 0, NULL);

	for(bh = req->bh; bh; bh = bh->b_reqnext) {
		for(size = 0; size < bh->b_size; size += TAPEBLOCK_HSEC_SIZE) {
			ccw->flags    = CCW_FLAG_CC;
			ccw->cmd_code = READ_FORWARD;
			ccw->count    = TAPEBLOCK_HSEC_SIZE;
			set_normalized_cda(ccw, (void *) __pa(bh->b_data+size));
			ccw++;
		}
	}

	ccw = tape_ccw_end(ccw, NOP, 0, NULL);

	return request;
}

void
tape_34xx_free_bread (struct tape_request *request)
{
	ccw1_t* ccw = request->cpaddr;

	if((ccw + 2)->cmd_code == READ_BLOCK_ID) {
		struct {
			struct tape_34xx_block_id	channel_block_id;
			struct tape_34xx_block_id	device_block_id;
		} __attribute__ ((packed)) *rbi_data;

		rbi_data = request->cpdata;

		if(!request->device)
			DBF_EVENT(6, "tape_34xx_free_bread: no device!\n");
		DBF_EVENT(6, "tape_34xx_free_bread: update_sbid\n");
		tape_34xx_add_sbid(
			request->device,
			rbi_data->channel_block_id
		);
	} else {
		DBF_EVENT(3, "tape_34xx_free_bread: no block info\n");
	}

	/* Last ccw is a nop and doesn't need clear_normalized_cda */
	for (ccw = request->cpaddr; ccw->flags & CCW_FLAG_CC; ccw++)
		if (ccw->cmd_code == READ_FORWARD)
			clear_normalized_cda(ccw);
	tape_put_request(request);
}

/*
 * check_locate is called just before the tape request is passed to
 * the common io layer for execution. It has to check the current
 * tape position and insert a locate ccw if it doesn't match the
 * start block for the request.
 */
void
tape_34xx_check_locate(struct tape_device *device, struct tape_request *request)
{
	struct tape_34xx_block_id *id;
	struct tape_34xx_block_id *start;

	id = (struct tape_34xx_block_id *) request->cpdata;

	/*
	 * The tape is already at the correct position. No seek needed.
	 */
	if (id->tbi_block == device->blk_data.block_position)
		return;

	/*
	 * In case that the block device image doesn't start at the beginning
	 * of the tape, adjust the blocknumber for the locate request.
	 */
	start = (struct tape_34xx_block_id *) &device->blk_data.start_block_id;
	if(start->tbi_block)
		id->tbi_block = id->tbi_block + start->tbi_block;

	/*
	 * Merge HW positioning information to the block id. This information
	 * is used by the device for faster seeks.
	 */
	tape_34xx_merge_sbid(device, id);

	/*
	 * Transform the NOP to a LOCATE entry.
	 */
	tape_ccw_cc(request->cpaddr + 1, LOCATE, 4, request->cpdata);
	tape_ccw_cc(request->cpaddr + 2, READ_BLOCK_ID, 8, request->cpdata);

	return;
}
#endif

static int
tape_34xx_mtweof(struct tape_device *device, int count)
{
	tape_34xx_clear_sbid_list(device);
	return tape_std_mtweof(device, count);
}

/*
 * List of 3480/3490 magnetic tape commands.
 */
static tape_mtop_fn tape_34xx_mtop[TAPE_NR_MTOPS] =
{
	[MTRESET]        = tape_std_mtreset,
	[MTFSF]          = tape_std_mtfsf,
	[MTBSF]          = tape_std_mtbsf,
	[MTFSR]          = tape_std_mtfsr,
	[MTBSR]          = tape_std_mtbsr,
	[MTWEOF]         = tape_34xx_mtweof,
	[MTREW]          = tape_std_mtrew,
	[MTOFFL]         = tape_std_mtoffl,
	[MTNOP]          = tape_std_mtnop,
	[MTRETEN]        = tape_std_mtreten,
	[MTBSFM]         = tape_std_mtbsfm,
	[MTFSFM]         = tape_std_mtfsfm,
	[MTEOM]          = tape_std_mteom,
	[MTERASE]        = tape_std_mterase,
	[MTRAS1]         = NULL,
	[MTRAS2]         = NULL,
	[MTRAS3]         = NULL,
	[MTSETBLK]       = tape_std_mtsetblk,
	[MTSETDENSITY]   = NULL,
	[MTSEEK]         = tape_34xx_mtseek,
	[MTTELL]         = tape_34xx_mttell,
	[MTSETDRVBUFFER] = NULL,
	[MTFSS]          = NULL,
	[MTBSS]          = NULL,
	[MTWSM]          = NULL,
	[MTLOCK]         = NULL,
	[MTUNLOCK]       = NULL,
	[MTLOAD]         = tape_std_mtload,
	[MTUNLOAD]       = tape_std_mtunload,
	[MTCOMPRESSION]  = tape_std_mtcompression,
	[MTSETPART]      = NULL,
	[MTMKPART]       = NULL
};

/*
 * Tape discipline structures for 3480 and 3490.
 */
static struct tape_discipline tape_discipline_3480 = {
	.owner = THIS_MODULE,
	.cu_type = 0x3480,
	.setup_device = tape_34xx_setup_device,
	.cleanup_device = tape_34xx_cleanup_device,
	.process_eov = tape_std_process_eov,
	.irq = tape_34xx_irq,
	.read_block = tape_std_read_block,
	.write_block = tape_std_write_block,
	.assign = tape_std_assign,
	.unassign = tape_std_unassign,
#ifdef TAPE390_FORCE_UNASSIGN
	.force_unassign = tape_std_force_unassign,
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
	.bread = tape_34xx_bread,
	.free_bread = tape_34xx_free_bread,
	.check_locate = tape_34xx_check_locate,
#endif
	.ioctl_fn = tape_34xx_ioctl,
	.mtop_array = tape_34xx_mtop
};

static struct tape_discipline tape_discipline_3490 = {
	.owner = THIS_MODULE,
	.cu_type = 0x3490,
	.setup_device = tape_34xx_setup_device,
	.cleanup_device = tape_34xx_cleanup_device,
	.process_eov = tape_std_process_eov,
	.irq = tape_34xx_irq,
	.read_block = tape_std_read_block,
	.write_block = tape_std_write_block,
	.assign = tape_std_assign,
	.unassign = tape_std_unassign,
#ifdef TAPE390_FORCE_UNASSIGN
	.force_unassign = tape_std_force_unassign,
#endif
#ifdef CONFIG_S390_TAPE_BLOCK
	.bread = tape_34xx_bread,
	.free_bread = tape_34xx_free_bread,
	.check_locate = tape_34xx_check_locate,
#endif
	.ioctl_fn = tape_34xx_ioctl,
	.mtop_array = tape_34xx_mtop
};

int
tape_34xx_init (void)
{
	int rc;

	DBF_EVENT(3, "34xx init: $Revision: 1.9 $\n");
	/* Register discipline. */
	rc = tape_register_discipline(&tape_discipline_3480);
	if (rc == 0) {
		rc = tape_register_discipline(&tape_discipline_3490);
		if (rc)
			tape_unregister_discipline(&tape_discipline_3480);
	}
	if (rc)
		DBF_EVENT(3, "34xx init failed\n");
	else
		DBF_EVENT(3, "34xx registered\n");
	return rc;
}

void
tape_34xx_exit(void)
{
	tape_unregister_discipline(&tape_discipline_3480);
	tape_unregister_discipline(&tape_discipline_3490);
}

MODULE_AUTHOR("(C) 2001-2002 IBM Deutschland Entwicklung GmbH");
MODULE_DESCRIPTION("Linux on zSeries channel attached 3480 tape "
		   "device driver ($Revision: 1.9 $)");
MODULE_LICENSE("GPL");

module_init(tape_34xx_init);
module_exit(tape_34xx_exit);
