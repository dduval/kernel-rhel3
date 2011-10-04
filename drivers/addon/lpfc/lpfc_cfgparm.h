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
 * $Id: lpfc_cfgparm.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef _H_LPFC_CFGPARAM
#define _H_LPFC_CFGPARAM

#define LPFC_DFT_XMT_QUE_SIZE           256
#define LPFC_MIN_XMT_QUE_SIZE           128
#define LPFC_MAX_XMT_QUE_SIZE           10240
#define LPFC_DFT_NUM_IOCBS              256
#define LPFC_MIN_NUM_IOCBS              128
#define LPFC_MAX_NUM_IOCBS              10240
#define LPFC_DFT_NUM_BUFS               128
#define LPFC_MIN_NUM_BUFS               64
#define LPFC_MAX_NUM_BUFS               4096
#define LPFC_DFT_NUM_NODES              510
#define LPFC_MIN_NUM_NODES              64
#define LPFC_MAX_NUM_NODES              4096
#define LPFC_DFT_TOPOLOGY               0
#define LPFC_DFT_FC_CLASS               3

#define LPFC_DFT_NO_DEVICE_DELAY        1	/* 1 sec */
#define LPFC_MAX_NO_DEVICE_DELAY        30	/* 30 sec */
#define LPFC_DFT_EXTRA_IO_TIMEOUT       0
#define LPFC_MAX_EXTRA_IO_TIMEOUT       255	/* 255 sec */
#define LPFC_DFT_LNKDWN_TIMEOUT         30
#define LPFC_MAX_LNKDWN_TIMEOUT         255	/* 255 sec */
#define LPFC_DFT_NODEV_TIMEOUT          30
#define LPFC_MAX_NODEV_TIMEOUT          255	/* 255 sec */
#define LPFC_DFT_RSCN_NS_DELAY          0
#define LPFC_MAX_RSCN_NS_DELAY          255	/* 255 sec */

#define LPFC_MAX_HBA_Q_DEPTH            10240	/* max cmds allowed per hba */
#define LPFC_DFT_HBA_Q_DEPTH            2048	/* max cmds per hba */
#define LPFC_LC_HBA_Q_DEPTH             1024	/* max cmds per low cost hba */
#define LPFC_LP101_HBA_Q_DEPTH          128	/* max cmds per low cost hba */

#define LPFC_MAX_TGT_Q_DEPTH            10240	/* max cmds allowed per tgt */
#define LPFC_DFT_TGT_Q_DEPTH            0	/* default max cmds per tgt */

#define LPFC_MAX_LUN_Q_DEPTH            128	/* max cmds to allow per lun */
#define LPFC_DFT_LUN_Q_DEPTH            30	/* default max cmds per lun */

#define LPFC_MAX_DQFULL_THROTTLE        1	/* Boolean (max value) */

#define LPFC_MAX_DISC_THREADS           64	/* max outstanding discovery els
						   requests */
#define LPFC_DFT_DISC_THREADS           1	/* default outstanding discovery
						   els requests */

#define LPFC_MAX_NS_RETRY               3	/* Try to get to the NameServer
						   3 times and then give up. */

#define LPFC_MAX_SCSI_REQ_TMO           255	/* Max timeout value for SCSI
						   passthru requests */
#define LPFC_DFT_SCSI_REQ_TMO           30	/* Default timeout value for
						   SCSI passthru requests */

#define LPFC_MAX_TARGET                 256	/* max nunber of targets
						   supported */
#define LPFC_DFT_MAX_TARGET             256	/* default max number of targets
						   supported */

#define LPFC_MAX_LUN                    256	/* max nunber of LUNs
						   supported */
#define LPFC_DFT_MAX_LUN                256	/* default max number of LUNs
						   supported */

/* LPFC specific parameters start at LPFC_CORE_NUM_OF_CFG_PARAM */
#define LPFC_CFG_LOG_VERBOSE             0	/* log-verbose */
#define LPFC_CFG_DFT_TGT_Q_DEPTH         1	/* tgt_queue_depth */
#define LPFC_CFG_DFT_LUN_Q_DEPTH         2	/* lun_queue_depth */
#define LPFC_CFG_EXTRA_IO_TMO            3	/* extra-io-tmo */
#define LPFC_CFG_NO_DEVICE_DELAY         4	/* no-device-delay */
#define LPFC_CFG_LINKDOWN_TMO            5	/* linkdown-tmo */
#define LPFC_CFG_HOLDIO                  6	/* nodev-holdio */
#define LPFC_CFG_DELAY_RSP_ERR           7	/* delay-rsp-err */
#define LPFC_CFG_CHK_COND_ERR            8	/* check-cond-err */
#define LPFC_CFG_NODEV_TMO               9	/* nodev-tmo */
#define LPFC_CFG_DQFULL_THROTTLE_UP_TIME 10	/* dqfull-throttle-up-time */
#define LPFC_CFG_DQFULL_THROTTLE_UP_INC  11	/* dqfull-throttle-up-inc */
#define LPFC_CFG_MAX_LUN                 12	/* max-lun */
#define LPFC_CFG_DFT_HBA_Q_DEPTH         13	/* dft_hba_q_depth */
#define LPFC_CFG_LUN_SKIP		 14	/* lun_skip */
#define LPFC_CFG_AUTOMAP                 15	/* automap */
#define LPFC_CFG_FCP_CLASS               16	/* fcp-class */
#define LPFC_CFG_USE_ADISC               17	/* use-adisc */
#define LPFC_CFG_XMT_Q_SIZE              18	/* xmt-que-size */
#define LPFC_CFG_ACK0                    19	/* ack0 */
#define LPFC_CFG_TOPOLOGY                20	/* topology */
#define LPFC_CFG_SCAN_DOWN               21	/* scan-down */
#define LPFC_CFG_LINK_SPEED              22	/* link-speed */
#define LPFC_CFG_CR_DELAY                23	/* cr-delay */
#define LPFC_CFG_CR_COUNT                24	/* cr-count */
#define LPFC_CFG_FDMI_ON                 25	/* fdmi-on-count */
#define LPFC_CFG_BINDMETHOD              26	/* fcp-bind-method */
#define LPFC_CFG_DISC_THREADS            27	/* discovery-threads */
#define LPFC_CFG_SCSI_REQ_TMO            28	/* timeout value for SCSI pass*/
#define LPFC_CFG_MAX_TARGET              29	/* max-target */


/* Note: The following define LPFC_TOTAL_NUM_OF_CFG_PARAM represents the total
	 number of user configuration params. This define is used to specify the
	 number of entries in the array lpfc_icfgparam[].
 */
#define LPFC_TOTAL_NUM_OF_CFG_PARAM      30

/* The order of the icfgparam[] entries must match that of LPFC_CORE_CFG defs */
#ifdef LPFC_DEF_ICFG
iCfgParam lpfc_icfgparam[LPFC_TOTAL_NUM_OF_CFG_PARAM] = {
	/* The driver now exports the cfg name. So it needs to be consistent
	   with lpfc.conf param name */

	/* general driver parameters */
	{"log_verbose",
	 0, 0xffff, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Verbose logging bit-mask"},

	{"tgt_queue_depth",
	 0, LPFC_MAX_TGT_Q_DEPTH, LPFC_DFT_TGT_Q_DEPTH, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific target"},

	{"lun_queue_depth",
	 1, LPFC_MAX_LUN_Q_DEPTH, LPFC_DFT_LUN_Q_DEPTH, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific LUN"},

	{"extra_io_tmo",
	 0, LPFC_MAX_EXTRA_IO_TIMEOUT, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Extra FCP command timeout"},

	{"no_device_delay",
	 0, LPFC_MAX_NO_DEVICE_DELAY, LPFC_DFT_NO_DEVICE_DELAY, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Length of interval in seconds for FCP device I/O failure"},

	{"linkdown_tmo",
	 0, LPFC_MAX_LNKDWN_TIMEOUT, LPFC_DFT_LNKDWN_TIMEOUT, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Seconds driver will wait before deciding link is really down"},

	{"nodev_holdio",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Hold I/O errors if device disappears "},

	{"delay_rsp_err",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Delay FCP error return for FCP RSP error and Check Condition"},

	{"check_cond_err",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Treat special Check Conditions as a FCP error"},

	{"nodev_tmo",
	 0, LPFC_MAX_NODEV_TIMEOUT, LPFC_DFT_NODEV_TIMEOUT, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Seconds driver will hold I/O waiting for a device to come back"},

	{"dqfull_throttle_up_time",
	 0, 30, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "When to increment the current Q depth "},

	{"dqfull_throttle_up_inc",
	 0, LPFC_MAX_LUN_Q_DEPTH, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Increment the current Q depth by dqfull-throttle-up-inc"},

	{"max_lun",
	 1, LPFC_MAX_LUN, LPFC_DFT_MAX_LUN, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "The maximun number of LUNs a target can support"},

	{"hba_queue_depth",
	 0, LPFC_MAX_HBA_Q_DEPTH, 0, 0,
	 (ushort) (CFG_IGNORE),
	 (ushort) CFG_RESTART,
	 "Max number of FCP commands we can queue to a specific HBA"},

	{"lun_skip",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable SCSI layer to scan past lun holes"},

	/* Start of product specific (lpfc) config params */

	{"automap",
	 0, 1, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Automatically bind FCP devices as they are discovered"},

	{"fcp_class",
	 2, 3, LPFC_DFT_FC_CLASS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Select Fibre Channel class of service for FCP sequences"},

	{"use_adisc",
	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Use ADISC on rediscovery to authenticate FCP devices"},

	/* IP specific parameters */
	{"xmt_que_size",
	 LPFC_MIN_XMT_QUE_SIZE, LPFC_MAX_XMT_QUE_SIZE, LPFC_DFT_XMT_QUE_SIZE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Number of outstanding IP cmds for an adapter"},

	/* Fibre Channel specific parameters */
	{"ack0",

	 0, 1, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable ACK0 support"},

	{"topology",
	 0, 6, LPFC_DFT_TOPOLOGY, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select Fibre Channel topology"},

	{"scan_down",
	 0, 1, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Start scanning for devices from highest ALPA to lowest"},

	{"link_speed",
	 0, 4, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select link speed"},

	{"cr_delay",
	 0, 63, 0, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "A count of milliseconds after which an interrupt response is "
	 "generated"},

	{"cr_count",
	 1, 255, 1, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "A count of I/O completions after which an interrupt response is "
	 "generated"},

	{"fdmi_on",
	 0, 2, FALSE, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Enable FDMI support"},

	{"fcp_bind_method",
	 1, 4, 2, 2,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Select the bind method to be used."},

	{"discovery_threads",
	 1, LPFC_MAX_DISC_THREADS, LPFC_DFT_DISC_THREADS, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "Maximum number of ELS commands during discovery"},

	{"scsi_req_tmo",
	 0, LPFC_MAX_SCSI_REQ_TMO, LPFC_DFT_SCSI_REQ_TMO, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_DYNAMIC,
	 "Timeout value for SCSI passthru requests"},

	{"max_target",
	 1, LPFC_MAX_TARGET, LPFC_DFT_MAX_TARGET, 0,
	 (ushort) (CFG_EXPORT),
	 (ushort) CFG_RESTART,
	 "The maximun number of targets an adapter can support"},
};
#endif				/* LPFC_DEF_ICFG */

#endif				/* _H_LPFC_CFGPARAM */
