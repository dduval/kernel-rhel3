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
 * $Id: lpfc_fcp.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef H_LPFC_DFC
#define H_LPFC_DFC

#define MAX_LPFC_SNS      128

#define FCP_CONTINUE    0x01	/* flag for issue_fcp_cmd */
#define FCP_REQUEUE     0x02	/* flag for issue_fcp_cmd */
#define FCP_EXIT        0x04	/* flag for issue_fcp_cmd */

typedef struct _FCP_RSP {
	uint32_t rspRsvd1;	/* FC Word 0, byte 0:3 */
	uint32_t rspRsvd2;	/* FC Word 1, byte 0:3 */

	uint8_t rspStatus0;	/* FCP_STATUS byte 0 (reserved) */
	uint8_t rspStatus1;	/* FCP_STATUS byte 1 (reserved) */
	uint8_t rspStatus2;	/* FCP_STATUS byte 2 field validity */
#define RSP_LEN_VALID  0x01	/* bit 0 */
#define SNS_LEN_VALID  0x02	/* bit 1 */
#define RESID_OVER     0x04	/* bit 2 */
#define RESID_UNDER    0x08	/* bit 3 */
	uint8_t rspStatus3;	/* FCP_STATUS byte 3 SCSI status byte */
#define SCSI_STAT_GOOD        0x00
#define SCSI_STAT_CHECK_COND  0x02
#define SCSI_STAT_COND_MET    0x04
#define SCSI_STAT_BUSY        0x08
#define SCSI_STAT_INTERMED    0x10
#define SCSI_STAT_INTERMED_CM 0x14
#define SCSI_STAT_RES_CNFLCT  0x18
#define SCSI_STAT_CMD_TERM    0x22
#define SCSI_STAT_QUE_FULL    0x28

	uint32_t rspResId;	/* Residual xfer if residual count field set in
				   fcpStatus2 */
	/* Received in Big Endian format */
	uint32_t rspSnsLen;	/* Length of sense data in fcpSnsInfo */
	/* Received in Big Endian format */
	uint32_t rspRspLen;	/* Length of FCP response data in fcpRspInfo */
	/* Received in Big Endian format */

	uint8_t rspInfo0;	/* FCP_RSP_INFO byte 0 (reserved) */
	uint8_t rspInfo1;	/* FCP_RSP_INFO byte 1 (reserved) */
	uint8_t rspInfo2;	/* FCP_RSP_INFO byte 2 (reserved) */
	uint8_t rspInfo3;	/* FCP_RSP_INFO RSP_CODE byte 3 */

#define RSP_NO_FAILURE       0x00
#define RSP_DATA_BURST_ERR   0x01
#define RSP_CMD_FIELD_ERR    0x02
#define RSP_RO_MISMATCH_ERR  0x03
#define RSP_TM_NOT_SUPPORTED 0x04	/* Task mgmt function not supported */
#define RSP_TM_NOT_COMPLETED 0x05	/* Task mgmt function not performed */

	uint32_t rspInfoRsvd;	/* FCP_RSP_INFO bytes 4-7 (reserved) */

	uint8_t rspSnsInfo[MAX_LPFC_SNS];
#define SNS_ILLEGAL_REQ 0x05	/* sense key is byte 3 ([2]) */
#define SNSCOD_BADCMD 0x20	/* sense code is byte 13 ([12]) */
} FCP_RSP, *PFCP_RSP;

typedef struct _FCP_CMND {
	uint32_t fcpLunMsl;	/* most  significant lun word (32 bits) */
	uint32_t fcpLunLsl;	/* least significant lun word (32 bits) */
	/* # of bits to shift lun id to end up in right
	 * payload word, little endian = 8, big = 16.
	 */
#if __BIG_ENDIAN
#define FC_LUN_SHIFT         16
#define FC_ADDR_MODE_SHIFT   24
#else	/*  __LITTLE_ENDIAN */
#define FC_LUN_SHIFT         8
#define FC_ADDR_MODE_SHIFT   0
#endif

	uint8_t fcpCntl0;	/* FCP_CNTL byte 0 (reserved) */
	uint8_t fcpCntl1;	/* FCP_CNTL byte 1 task codes */
#define  SIMPLE_Q        0x00
#define  HEAD_OF_Q       0x01
#define  ORDERED_Q       0x02
#define  ACA_Q           0x04
#define  UNTAGGED        0x05
	uint8_t fcpCntl2;	/* FCP_CTL byte 2 task management codes */
#define  ABORT_TASK_SET  0x02	/* Bit 1 */
#define  CLEAR_TASK_SET  0x04	/* bit 2 */
#define  BUS_RESET       0x08	/* bit 3 */
#define  LUN_RESET       0x10	/* bit 4 */
#define  TARGET_RESET    0x20	/* bit 5 */
#define  CLEAR_ACA       0x40	/* bit 6 */
#define  TERMINATE_TASK  0x80	/* bit 7 */
	uint8_t fcpCntl3;
#define  WRITE_DATA      0x01	/* Bit 0 */
#define  READ_DATA       0x02	/* Bit 1 */

	uint8_t fcpCdb[16];	/* SRB cdb field is copied here */
	uint32_t fcpDl;		/* Total transfer length */

} FCP_CMND, *PFCP_CMND;

/* SCSI CDB command codes */
#define FCP_SCSI_FORMAT_UNIT                  0x04
#define FCP_SCSI_INQUIRY                      0x12
#define FCP_SCSI_MODE_SELECT                  0x15
#define FCP_SCSI_MODE_SENSE                   0x1A
#define FCP_SCSI_PAUSE_RESUME                 0x4B
#define FCP_SCSI_PLAY_AUDIO                   0x45
#define FCP_SCSI_PLAY_AUDIO_EXT               0xA5
#define FCP_SCSI_PLAY_AUDIO_MSF               0x47
#define FCP_SCSI_PLAY_AUDIO_TRK_INDX          0x48
#define FCP_SCSI_PREVENT_ALLOW_REMOVAL        0x1E
#define FCP_SCSI_READ                         0x08
#define FCP_SCSI_READ_BUFFER                  0x3C
#define FCP_SCSI_READ_CAPACITY                0x25
#define FCP_SCSI_READ_DEFECT_LIST             0x37
#define FCP_SCSI_READ_EXTENDED                0x28
#define FCP_SCSI_READ_HEADER                  0x44
#define FCP_SCSI_READ_LONG                    0xE8
#define FCP_SCSI_READ_SUB_CHANNEL             0x42
#define FCP_SCSI_READ_TOC                     0x43
#define FCP_SCSI_REASSIGN_BLOCK               0x07
#define FCP_SCSI_RECEIVE_DIAGNOSTIC_RESULTS   0x1C
#define FCP_SCSI_RELEASE_UNIT                 0x17
#define FCP_SCSI_REPORT_LUNS                  0xa0
#define FCP_SCSI_REQUEST_SENSE                0x03
#define FCP_SCSI_RESERVE_UNIT                 0x16
#define FCP_SCSI_REZERO_UNIT                  0x01
#define FCP_SCSI_SEEK                         0x0B
#define FCP_SCSI_SEEK_EXTENDED                0x2B
#define FCP_SCSI_SEND_DIAGNOSTIC              0x1D
#define FCP_SCSI_START_STOP_UNIT              0x1B
#define FCP_SCSI_TEST_UNIT_READY              0x00
#define FCP_SCSI_VERIFY                       0x2F
#define FCP_SCSI_WRITE                        0x0A
#define FCP_SCSI_WRITE_AND_VERIFY             0x2E
#define FCP_SCSI_WRITE_BUFFER                 0x3B
#define FCP_SCSI_WRITE_EXTENDED               0x2A
#define FCP_SCSI_WRITE_LONG                   0xEA
#define FCP_SCSI_RELEASE_LUNR                 0xBB
#define FCP_SCSI_RELEASE_LUNV                 0xBF
#endif
