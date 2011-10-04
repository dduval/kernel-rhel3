/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_scsi.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef _H_LPFC_SCSI
#define _H_LPFC_SCSI

/*
 * SCSI node structure for each open Fibre Channel node
 * used by scsi transport.
 */

struct lpfcHBA;

typedef struct lpfcScsiTarget {
	struct lpfcHBA *pHba;	/* adapter structure ptr */
	struct list_head   lunlist;
	void *pcontext;		/* LPFC_NODELIST_t * for device */
	struct timer_list tmofunc;
	struct timer_list rptlunfunc;
	struct list_head listentry; /* scsi target queue entry */
	LPFC_SCHED_TARGET_t targetSched; /* Scheduling Info for this target */
	uint16_t max_lun;	/* max lun supported */
	uint16_t scsi_id;	/* SCSI ID of this device */

	uint16_t rpi;

	uint16_t targetFlags;
#define FC_NODEV_TMO        0x1	/* nodev-tmo tmr started and expired */
#define FC_FCP2_RECOVERY    0x2	/* set FCP2 Recovery for commands */
#define FC_RETRY_RPTLUN     0x4	/* Report Lun has been retried */
#define FC_NPR_ACTIVE       0x10	/* NPort Recovery active */

	uint16_t addrMode;	/* SCSI address method */
#define PERIPHERAL_DEVICE_ADDRESSING    0
#define VOLUME_SET_ADDRESSING           1
#define LOGICAL_UNIT_ADDRESSING         2

	uint16_t rptLunState;	/* For report lun SCSI command */
#define REPORT_LUN_REQUIRED     0
#define REPORT_LUN_ONGOING      1
#define REPORT_LUN_COMPLETE     2
#define REPORT_LUN_ERRORED      3

	DMABUF_t *RptLunData;

	void *pTargetProto;	/* target struc for driver type */
	void *pTargetOSEnv;

	union {
		uint32_t dev_did;	/* SCSI did */
	} un;

} LPFCSCSITARGET_t;

#define LPFC_SCSI_BUF_SZ        1024  /* used for driver generated scsi cmds */
#define LPFC_SCSI_PAGE_BUF_SZ   4096  /* used for driver RPTLUN cmds */
#define LPFC_INQSN_SZ           64    /* Max size of Inquiry serial number */

struct fcPathId;
struct fcRouteId;

struct lpfcScsiLun {
	struct list_head list;	/* Used for list of LUNs on this node */
	LPFC_NODELIST_t *pnode;	/* Pointer to the node structure. */
	struct lpfcHBA *pHBA;	/* Pointer to the HBA with
				   which this LUN is
				   associated. */
	LPFCSCSITARGET_t *pTarget;	/* Pointer to the target structure */
	struct lpfcScsiLun *pnextLun;	/* Used for list of LUNs on this node */

	uint64_t lun_id;		/* LUN ID of this device */
	uint32_t qcmdcnt;
	uint32_t iodonecnt;
	uint32_t errorcnt;

	void *pLunOSEnv;

	/*
	 *  A command lives in a pending queue until it is sent to the HBA.
	 *  Throttling constraints apply:
	 *          No more than N commands total to a single target
	 *          No more than M commands total to a single LUN on that target
	 *
	 *  A command that has left the pending queue and been sent to the HBA
	 *  is an "underway" command.  We count underway commands, per-LUN,
	 *  to obey the LUN throttling constraint.
	 *
	 *  Because we only allocate enough fc_buf_t structures to handle N
	 *  commands, per target, we implicitly obey the target throttling
	 *  constraint by being unable to send a command when we run out of
	 *  free fc_buf_t structures.
	 *
	 *  We count the number of pending commands to determine whether the
	 *  target has I/O to be issued at all.
	 *
	 *  We use next_pending to rotor through the LUNs, issuing one I/O at
	 *  a time for each LUN.  This mechanism guarantees a fair distribution
	 *  of I/Os across LUNs in the face of a target queue_depth lower than
	 *  #LUNs*fcp_lun_queue_depth.
	 */
	LPFC_SCHED_LUN_t lunSched;	/* Used to schedule I/O to HBA */
	uint16_t fcp_lun_queue_depth;	/* maximum # cmds to each lun */
	uint8_t stop_send_io;	/* stop sending any io to this dev */
	uint32_t lunFlag;	/* flags for the drive */
#define SCSI_TQ_HALTED        0x0001	/* The transaction Q is halted */
#define SCSI_TQ_CLEARING      0x0002	/* The transaction Q is clearing */
#define SCSI_TQ_CLEAR_ACA     0x0004	/* a CLEAR_ACA is PENDING      */
#define SCSI_LUN_RESET        0x0008	/* sent LUN_RESET not of TARGET_RESET */
#define SCSI_ABORT_TSET       0x0010	/* BDR requested but not yet sent */
#define SCSI_TARGET_RESET     0x0020	/* a SCSI BDR is active for device */
#define CHK_SCSI_ABDR         0x0038	/* value used to check tm flags */
#define QUEUED_FOR_ABDR       0x0040	/* dev_ptr is on ABORT_BDR queue */
#define NORPI_RESET_DONE      0x0100	/* BOGUS_RPI Bus Reset attempted */
#define LUN_BLOCKED           0x0200	/* if flag is set, this lun has been
					   blocked */
#define SCSI_BUMP_QDEPTH      0x0800	/* bump qdepth to max after cmpl */
#define SCSI_SEND_INQUIRY_SN  0x1000	/* Serial number inq should be sent */
#define SCSI_INQUIRY_SN       0x2000	/* Serial number inq has been sent */
#define SCSI_INQUIRY_P0       0x4000	/* Page 0 inq has been sent */
#define SCSI_INQUIRY_CMD      0x6000	/* Serial number or Page 0 inq sent */
#define SCSI_P0_INFO          0x20000	/* device has good P0 info */

	uint16_t qfull_retries;	/* # of retries on qfull condition */
	struct timer_list qfull_tmo_id;

	uint32_t failMask;	/* failure mask for device */

	uint8_t InquirySN[LPFC_INQSN_SZ];	/* serial number from Inquiry */
	uint8_t Vendor[8];	/* From Page 0 Inquiry */
	uint8_t Product[16];	/* From Page 0 Inquiry */
	uint8_t Rev[4];		/* From Page 0 Inquiry */
	uint8_t sizeSN;		/* size of InquirySN */
	struct list_head listentry; /* scsi lun queue entry */
};

typedef struct lpfcScsiLun LPFCSCSILUN_t;

#define LPFC_MIN_QFULL    1	/* lowest we can decrement throttle */

struct lpfc_scsi_buf {
	struct list_head listentry;
	uint32_t scsitmo;	/* IN */
	uint32_t timeout;	/* IN */
	struct lpfcHBA *scsi_hba;	/* IN */
	uint8_t scsi_bus;	/* IN */
	uint16_t scsi_target;	/* IN */
	uint64_t scsi_lun;	/* IN */

	struct scsi_cmnd *pCmd;	/* IN */

	uint32_t qfull_retry_count;	/* internal to scsi xport */
	uint16_t flags;		/* flags for this cmd */
#define DATA_MAPPED     0x0001	/* data buffer has been D_MAPed */
#define FCBUF_ABTS      0x0002	/* ABTS has been sent for this cmd */
#define FCBUF_ABTS2     0x0004	/* ABTS has been sent twice */
#define FCBUF_INTERNAL  0x0008	/* Internal generated driver command */
#define LPFC_SCSI_ERR    0x0010
	uint16_t IOxri;		/* From IOCB Word 6- ulpContext */
	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */

	int        datadir;	/* Data direction as requested in the scsi
				   command. */
	uint32_t   seg_cnt;	/* Number of scatter-gather segments returned by
				 * pci_map_sg.  The driver needs this for calls
				 * to pci_unmap_sg. */
	dma_addr_t nonsg_phys;	/* Non scatter-gather physical address. */

	LPFCSCSILUN_t *pLun;
	struct timer_list delayIodoneFunc;

	/* dma_ext has both virt, phys to dma-able buffer
	 * which contains fcp_cmd, fcp_rsp and scatter gather list fro upto 
	 * 68 (LPFC_SCSI_BPL_SIZE) BDE entries,
	 * xfer length, cdb, data direction....
	 */
	DMABUF_t *dma_ext;
	struct _FCP_CMND *fcp_cmnd;
	struct _FCP_RSP *fcp_rsp;
	ULP_BDE64 *fcp_bpl;

	/* cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here 
	 */
	LPFC_IOCBQ_t cur_iocbq;

	void (*cmd_cmpl) (struct lpfcHBA *, struct lpfc_scsi_buf *);	/* IN */
};

typedef struct lpfc_scsi_buf LPFC_SCSI_BUF_t;

#define LPFC_SCSI_INITIAL_BPL_SIZE  3	/* Number of scsi buf BDEs in fcp_bpl */

#define FAILURE -1
#define LPFC_CMD_STATUS_ABORTED -1

#define LPFC_INTERNAL_RESET   0	/* internal reset */
#define LPFC_EXTERNAL_RESET   1	/* external reset, scsi layer */
#define LPFC_ISSUE_LUN_RESET  2	/* flag for reset routine to issue LUN_RESET */
#define LPFC_ISSUE_ABORT_TSET 4	/* flag for reset routine to issue ABORT_TSET */

#define LPFC_SCSI_DMA_EXT_SIZE 256
#define LPFC_BPL_SIZE          1024

#define MDAC_DIRECT_CMD                  0x22

#endif				/* _H_LPFC_SCSI */
