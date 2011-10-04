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
 * $Id: lpfc_logmsg.c 369 2005-07-08 23:29:48Z sf_support $
 */
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/pci.h>


#include <linux/blk.h>
#include <scsi.h>

#include "lpfc_hw.h"
#include "lpfc_mem.h"
#include "lpfc_sli.h"
#include "lpfc_sched.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_logmsg.h"
#include "lpfc_cfgparm.h"
#include "lpfc_crtn.h"

/*
 * Define the message logging functions
 */

#define  MAX_IO_SIZE 32 * 2	/* iobuf cache size */
#define  MAX_TBUFF   18 * 2	/* temp buffer size */

extern char *lpfc_drvr_name;
extern lpfcDRVR_t lpfcDRVR;

int
lpfc_printf_log(int brdno, msgLogDef * msg,	/* Pointer to LOG msg
						   structure */
	       void *control, ...)
{
	uint8_t str2[MAX_IO_SIZE + MAX_TBUFF];	/* extra room to convert
						   numbers */
	int iocnt;
	va_list args;
	va_start(args, control);

	if (lpfc_log_chk_msg_disabled(brdno, msg))
		return (0);	/* This LOG message disabled */

	/* If LOG message is disabled via any SW method, we SHOULD NOT get this
	   far!  We should have taken the above return.
	 */

	str2[0] = '\0';
	iocnt = vsprintf(str2, control, args);
	va_end(args);
	return (lpfc_printf_log_msgblk(brdno, msg, (char *)str2));
}

int
lpfc_log_chk_msg_disabled(int brdno, msgLogDef * msg)
{				/* Pointer to LOG msg structure */
	lpfcHBA_t *phba;
	lpfcCfgParam_t *clp;
	int verbose;

	verbose = 0;

	if (msg->msgOutput == LPFC_MSG_OPUT_DISA)
		return (1);	/* This LOG message disabled */

        if ((phba = lpfc_get_phba_by_inst(brdno)) != NULL) {
		clp = &phba->config[0];
		verbose = clp[LPFC_CFG_LOG_VERBOSE].a_current;
	}

	if (msg->msgOutput == LPFC_MSG_OPUT_FORCE) {
		return (0);	/* This LOG message enabled */
	}

	if ((msg->msgType == LPFC_LOG_MSG_TYPE_INFO) ||
	    (msg->msgType == LPFC_LOG_MSG_TYPE_WARN)) {
		/* LOG msg is INFO or WARN */
		if ((msg->msgMask & verbose) == 0)
			return (1);	/* This LOG message disabled */
	}

	return (0);		/* This LOG message enabled */
}

int
lpfc_printf_log_msgblk(int brdno, msgLogDef * msg, char *str)
{				/* String formatted by caller */
	int ddiinst;
	lpfcHBA_t *phba;

        if ((phba = lpfc_get_phba_by_inst(brdno)) == NULL) {	
		/* Remove: This case should not occur. Sanitize anyway. More
		   testing needed */
		printk(KERN_WARNING "%s%d:%04d:%s\n", lpfc_drvr_name, brdno,
		       msg->msgNum, str);
		return 1;
	}

	ddiinst = brdno;	/* Board number = instance in LINUX */
	switch (msg->msgType) {
	case LPFC_LOG_MSG_TYPE_INFO:
	case LPFC_LOG_MSG_TYPE_WARN:
		/* These LOG messages appear in LOG file only */
		printk(KERN_INFO "%s%d:%04d:%s\n", lpfc_drvr_name, ddiinst,
		       msg->msgNum, str);
		break;
	case LPFC_LOG_MSG_TYPE_ERR_CFG:
	case LPFC_LOG_MSG_TYPE_ERR:
		/* These LOG messages appear on the monitor and in the LOG
		   file */
		printk(KERN_WARNING "%s%d:%04d:%s\n", lpfc_drvr_name, ddiinst,
		       msg->msgNum, str);
		break;
	case LPFC_LOG_MSG_TYPE_PANIC:
		panic("%s%d:%04d:%s\n", lpfc_drvr_name, ddiinst, msg->msgNum,
		      str);
		break;
	default:
		return (0);
	}
	return (1);
}

/* ELS Log Message Preamble Strings - 100 */
char lpfc_msgPreambleELi[] = "ELi:";	/* ELS Information */
char lpfc_msgPreambleELw[] = "ELw:";	/* ELS Warning */
char lpfc_msgPreambleELe[] = "ELe:";	/* ELS Error */
char lpfc_msgPreambleELp[] = "ELp:";	/* ELS Panic */

/* DISCOVERY Log Message Preamble Strings - 200 */
char lpfc_msgPreambleDIi[] = "DIi:";	/* Discovery Information */
char lpfc_msgPreambleDIw[] = "DIw:";	/* Discovery Warning */
char lpfc_msgPreambleDIe[] = "DIe:";	/* Discovery Error */
char lpfc_msgPreambleDIp[] = "DIp:";	/* Discovery Panic */

/* MAIBOX Log Message Preamble Strings - 300 */
/* SLI Log Message Preamble Strings    - 300 */
char lpfc_msgPreambleMBi[] = "MBi:";	/* Mailbox Information */
char lpfc_msgPreambleMBw[] = "MBw:";	/* Mailbox Warning */
char lpfc_msgPreambleMBe[] = "MBe:";	/* Mailbox Error */
char lpfc_msgPreambleMBp[] = "MBp:";	/* Mailbox Panic */
char lpfc_msgPreambleSLw[] = "SLw:";	/* SLI Warning */
char lpfc_msgPreambleSLe[] = "SLe:";	/* SLI Error */
char lpfc_msgPreambleSLi[] = "SLi:";	/* SLI Information */

/* INIT Log Message Preamble Strings - 400, 500 */
char lpfc_msgPreambleINi[] = "INi:";	/* INIT Information */
char lpfc_msgPreambleINw[] = "INw:";	/* INIT Warning */
char lpfc_msgPreambleINc[] = "INc:";	/* INIT Error Config */
char lpfc_msgPreambleINe[] = "INe:";	/* INIT Error */
char lpfc_msgPreambleINp[] = "INp:";	/* INIT Panic */

/* IP Log Message Preamble Strings - 600 */
char lpfc_msgPreambleIPi[] = "IPi:";	/* IP Information */
char lpfc_msgPreambleIPw[] = "IPw:";	/* IP Warning */
char lpfc_msgPreambleIPe[] = "IPe:";	/* IP Error */
char lpfc_msgPreambleIPp[] = "IPp:";	/* IP Panic */

/* FCP Log Message Preamble Strings - 700, 800 */
char lpfc_msgPreambleFPi[] = "FPi:";	/* FP Information */
char lpfc_msgPreambleFPw[] = "FPw:";	/* FP Warning */
char lpfc_msgPreambleFPe[] = "FPe:";	/* FP Error */
char lpfc_msgPreambleFPp[] = "FPp:";	/* FP Panic */

/* NODE Log Message Preamble Strings - 900 */
char lpfc_msgPreambleNDi[] = "NDi:";	/* Node Information */
char lpfc_msgPreambleNDe[] = "NDe:";	/* Node Error */
char lpfc_msgPreambleNDp[] = "NDp:";	/* Node Panic */

/* MISC Log Message Preamble Strings - 1200 */
char lpfc_msgPreambleMIi[] = "MIi:";	/* MISC Information */
char lpfc_msgPreambleMIw[] = "MIw:";	/* MISC Warning */
char lpfc_msgPreambleMIc[] = "MIc:";	/* MISC Error Config */
char lpfc_msgPreambleMIe[] = "MIe:";	/* MISC Error */
char lpfc_msgPreambleMIp[] = "MIp:";	/* MISC Panic */

/* Link Log Message Preamble Strings - 1300 */
char lpfc_msgPreambleLKi[] = "LKi:";	/* Link Information */
char lpfc_msgPreambleLKw[] = "LKw:";	/* Link Warning */
char lpfc_msgPreambleLKe[] = "LKe:";	/* Link Error */
char lpfc_msgPreambleLKp[] = "Lkp:";	/* Link Panic */

/* Libdfc Log Message Preamble Strings - 1600 */
char lpfc_msgPreambleLDi[] = "LDi:";	/* Libdfc Information */
char lpfc_msgPreambleLDw[] = "LDw:";	/* Libdfc Warning */
char lpfc_msgPreambleLDe[] = "LDe:";	/* Libdfc Error */
char lpfc_msgPreambleLDp[] = "LDp:";	/* Libdfc Panic */

/* 
 * The format of all code below this point must meet rules specified by 
 * the ultility MKLOGRPT.
 */

/*
 *  Begin ELS LOG message structures
 */

/*
msgName: lpfc_mes0100
message:  FLOGI failure
descript: An ELS FLOGI command that was sent to the fabric failed.
data:     (1) ulpStatus (2) ulpWord[4]
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0100[] = "%sFLOGI failure Data: x%x x%x";
msgLogDef lpfc_msgBlk0100 = {
	LPFC_LOG_MSG_EL_0100,
	lpfc_mes0100,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0101
message:  FLOGI completes successfully
descript: An ELS FLOGI command that was sent to the fabric succeeded.
data:     (1) ulpWord[4] (2) e_d_tov (3) r_a_tov (4) edtovResolution
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0101[] = "%sFLOGI completes sucessfully Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0101 = {
	LPFC_LOG_MSG_EL_0101,
	lpfc_mes0101,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0102
message:  PLOGI completes to NPort <nlp_DID>
descript: The HBA performed a PLOGI into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) disc (4) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0102[] = "%sPLOGI completes to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0102 = {
	LPFC_LOG_MSG_EL_0102,
	lpfc_mes0102,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0103
message:  PRLI completes to NPort <nlp_DID>
descript: The HBA performed a PRLI into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0103[] = "%sPRLI completes to NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0103 = {
	LPFC_LOG_MSG_EL_0103,
	lpfc_mes0103,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0104
message:  ADISC completes to NPort <nlp_DID>
descript: The HBA performed a ADISC into a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) disc (4) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0104[] = "%sADISC completes to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0104 = {
	LPFC_LOG_MSG_EL_0104,
	lpfc_mes0104,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0105
message:  LOGO completes to NPort <nlp_DID>
descript: The HBA performed a LOGO to a remote NPort
data:     (1) ulpStatus (2) ulpWord[4] (3) num_disc_nodes
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0105[] = "%sLOGO completes to NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0105 = {
	LPFC_LOG_MSG_EL_0105,
	lpfc_mes0105,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0106
message:  ELS cmd tag <ulpIoTag> completes
descript: The specific ELS command was completed by the firmware.
data:     (1) ulpStatus (2) ulpWord[4]
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0106[] = "%sELS cmd tag x%x completes Data: x%x x%x";
msgLogDef lpfc_msgBlk0106 = {
	LPFC_LOG_MSG_EL_0106,
	lpfc_mes0106,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0107
message:  Retry ELS command <elsCmd> to remote NPORT <did>
descript: The driver is retrying the specific ELS command.
data:     (1) retry (2) delay
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0107[] =
    "%sRetry ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0107 = {
	LPFC_LOG_MSG_EL_0107,
	lpfc_mes0107,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0108
message:  No retry ELS command <elsCmd> to remote NPORT <did>
descript: The driver decided not to retry the specific ELS command that failed.
data:     (1) retry (2) nlp_flag
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0108[] =
    "%sNo retry ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0108 = {
	LPFC_LOG_MSG_EL_0108,
	lpfc_mes0108,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0109
message:  ACC to LOGO completes to NPort <nlp_DID>
descript: The driver received a LOGO from a remote NPort and successfully
          issued an ACC response.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0109[] = "%sACC to LOGO completes to NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0109 = {
	LPFC_LOG_MSG_EL_0109,
	lpfc_mes0109,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0110
message:  ELS response tag <ulpIoTag> completes
descript: The specific ELS response was completed by the firmware.
data:     (1) ulpStatus (2) ulpWord[4] (3) nlp_DID (4) nlp_flag (5) nlp_state
          (6) nlp_rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0110[] =
    "%sELS response tag x%x completes Data: x%x x%x x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0110 = {
	LPFC_LOG_MSG_EL_0110,
	lpfc_mes0110,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0111
message:  Dropping received ELS cmd
descript: The driver decided to drop an ELS Response ring entry
data:     (1) ulpStatus (2) ulpWord[4]
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0111[] = "%sDropping received ELS cmd Data: x%x x%x";
msgLogDef lpfc_msgBlk0111 = {
	LPFC_LOG_MSG_EL_0111,
	lpfc_mes0111,
	lpfc_msgPreambleELe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0112
message:  ELS command <elsCmd> received from NPORT <did> 
descript: Received the specific ELS command from a remote NPort.
data:     (1) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0112[] = "%sELS command x%x received from NPORT x%x Data: x%x";
msgLogDef lpfc_msgBlk0112 = {
	LPFC_LOG_MSG_EL_0112,
	lpfc_mes0112,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0113
message:  An FLOGI ELS command <elsCmd> was received from DID <did> in Loop
          Mode
descript: While in Loop Mode an unknown or unsupported ELS commnad 
          was received.
data:     None
severity: Error
log:      Always
action:   Check device DID
*/
char lpfc_mes0113[] =
    "%sAn FLOGI ELS command x%x was received from DID x%x in Loop Mode";
msgLogDef lpfc_msgBlk0113 = {
	LPFC_LOG_MSG_EL_0113,
	lpfc_mes0113,
	lpfc_msgPreambleELe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0114
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0114[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0114 = {
	LPFC_LOG_MSG_EL_0114,
	lpfc_mes0114,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0115
message:  Unknown ELS command <elsCmd> received from NPORT <did> 
descript: Received an unsupported ELS command from a remote NPORT.
data:     None
severity: Error
log:      Always
action:   Check remote NPORT for potential problem.
*/
char lpfc_mes0115[] = "%sUnknown ELS command x%x received from NPORT x%x";
msgLogDef lpfc_msgBlk0115 = {
	LPFC_LOG_MSG_EL_0115,
	lpfc_mes0115,
	lpfc_msgPreambleELe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0116
message:  Xmit ELS command <elsCmd> to remote NPORT <did>
descript: Xmit ELS command to remote NPORT 
data:     (1) icmd->ulpIoTag (2) binfo->fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0116[] = "%sXmit ELS command x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0116 = {
	LPFC_LOG_MSG_EL_0116,
	lpfc_mes0116,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0117
message:  Xmit ELS response <elsCmd> to remote NPORT <did>
descript: Xmit ELS response to remote NPORT 
data:     (1) icmd->ulpIoTag (2) size
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0117[] =
    "%sXmit ELS response x%x to remote NPORT x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0117 = {
	LPFC_LOG_MSG_EL_0117,
	lpfc_mes0117,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0118
message:  Xmit CT response on exchange <xid>
descript: Xmit a CT response on the appropriate exchange.
data:     (1) ulpIoTag (2) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0118[] = "%sXmit CT response on exchange x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0118 = {
	LPFC_LOG_MSG_EL_0118,
	lpfc_mes0118,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0119
message:  Issue GEN REQ IOCB for NPORT <did>
descript: Issue a GEN REQ IOCB for remote NPORT.  These are typically
          used for CT request. 
data:     (1) ulpIoTag (2) fc_ffstate
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0119[] = "%sIssue GEN REQ IOCB for NPORT x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0119 = {
	LPFC_LOG_MSG_EL_0119,
	lpfc_mes0119,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0120
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0120[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0120 = {
	LPFC_LOG_MSG_EL_0120,
	lpfc_mes0120,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0121
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0121[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0121 = {
	LPFC_LOG_MSG_EL_0121,
	lpfc_mes0121,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0122
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0122[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0122 = {
	LPFC_LOG_MSG_EL_0122,
	lpfc_mes0122,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0123
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0123[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0123 = {
	LPFC_LOG_MSG_EL_0123,
	lpfc_mes0123,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0124
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0124[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0124 = {
	LPFC_LOG_MSG_EL_0124,
	lpfc_mes0124,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0125
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0125[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0125 = {
	LPFC_LOG_MSG_EL_0125,
	lpfc_mes0125,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0126
message:  PLOGI chkparm OK
descript: Received a PLOGI from a remote NPORT and its Fibre Channel service 
          parameters match this HBA. Request can be accepted.
data:     (1) nlp_DID (2) nlp_state (3) nlp_flag (4) nlp_Rpi
severity: Information
log:      LOG_ELS verbose
action:   No action needed, informational
*/
char lpfc_mes0126[] = "%sPLOGI chkparm OK Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0126 = {
	LPFC_LOG_MSG_EL_0126,
	lpfc_mes0126,
	lpfc_msgPreambleELi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_ELS,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0127
message:  ELS timeout
descript: An ELS IOCB command was posted to a ring and did not complete
          within ULP timeout seconds.
data:     (1) elscmd (2) did (3) ulpcommand (4) iotag
severity: Error
log:      Always
action:   If no ELS command is going through the adapter, reboot the system;
          If problem persists, contact Technical Support.
*/
char lpfc_mes0127[] = "%sELS timeout Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0127 = {
	LPFC_LOG_MSG_EL_0127,
	lpfc_mes0127,
	lpfc_msgPreambleELe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_ELS,
	ERRID_LOG_TIMEOUT
};

/*
 *  Begin DSCOVERY LOG Message Structures
 */

/*
msgName: lpfc_mes0200
message:  CONFIG_LINK bad hba state <hba_state>
descript: A CONFIG_LINK mbox command completed and the driver was not in the
          right state.
data:     none
severity: Error
log:      Always
action:   Software driver error.
          If this problem persists, report these errors to Technical Support.
*/
char lpfc_mes0200[] = "%sCONFIG_LINK bad hba state x%x";
msgLogDef lpfc_msgBlk0200 = {
	LPFC_LOG_MSG_DI_0200,
	lpfc_mes0200,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0201
message:  Abort outstanding I/O on NPort <nlp_DID>
descript: All outstanding I/Os are cleaned up on the specified remote NPort.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0201[] = "%sAbort outstanding I/O on NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0201 = {
	LPFC_LOG_MSG_DI_0201,
	lpfc_mes0201,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0202
message:  Start Discovery hba state <hba_state>
descript: Device discovery / rediscovery after FLOGI, FAN or RSCN has started.
data:     (1) tmo (2) fc_plogi_cnt (3) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0202[] = "%sStart Discovery hba state x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0202 = {
	LPFC_LOG_MSG_DI_0202,
	lpfc_mes0202,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0204
message:  Create SCSI Target <tgt>
descript: A mapped FCP target was discovered and the driver has allocated
          resources for it.
data:     none
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0204[] = "%sCreate SCSI Target %d";
msgLogDef lpfc_msgBlk0204 = {
	LPFC_LOG_MSG_DI_0204,
	lpfc_mes0204,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0205
message:  Create SCSI LUN <lun> on Target <tgt>
descript: A LUN on a mapped FCP target was discovered and the driver has
          allocated resources for it.
data:     none
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0205[] = "%sCreate SCSI LUN %d on Target %d";
msgLogDef lpfc_msgBlk0205 = {
	LPFC_LOG_MSG_DI_0205,
	lpfc_mes0205,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0206
message:  Report Lun completes on NPort <nlp_DID>
descript: The driver issued a REPORT_LUN SCSI command to a FCP target and it
          completed.
data:     (1) ulpStatus (2) rspStatus2 (3) rspStatus3 (4) nlp_failMask
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0206[] =
    "%sReport Lun completes on NPort x%x status: x%x status2: x%x status3: x%x failMask: x%x";
msgLogDef lpfc_msgBlk0206 = {
	LPFC_LOG_MSG_DI_0206,
	lpfc_mes0206,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0207
message:  Issue Report LUN on NPort <nlp_DID>
descript: The driver issued a REPORT_LUN SCSI command to a FCP target.
data:     (1) nlp_failMask (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0207[] = "%sIssue Report LUN on NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0207 = {
	LPFC_LOG_MSG_DI_0207,
	lpfc_mes0207,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0208
message:  Failmask change on NPort <nlp_DID>
descript: An event was processed that indicates the driver may not be able to
          communicate with the remote NPort.
data:     (1) nlp_failMask (2) bitmask (3) flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0208[] = "%sFailmask change on NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0208 = {
	LPFC_LOG_MSG_DI_0208,
	lpfc_mes0208,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0209
message:  RFT request completes ulpStatus <ulpStatus> CmdRsp <CmdRsp>
descript: A RFT request that was sent to the fabric completed.
data:     (1) nlp_failMask (2) bitmask (3) flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0209[] = "%sRFT request completes ulpStatus x%x CmdRsp x%x";
msgLogDef lpfc_msgBlk0209 = {
	LPFC_LOG_MSG_DI_0209,
	lpfc_mes0209,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0210
message:  Continue discovery with <num_disc_nodes> ADISCs to go
descript: Device discovery is in progress
data:     (1) fc_adisc_cnt (2) fc_flag (3) phba->hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0210[] =
    "%sContinue discovery with %d ADISCs to go Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0210 = {
	LPFC_LOG_MSG_DI_0210,
	lpfc_mes0210,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0211
message:  DSM in event <evt> on NPort <nlp_DID> in state <cur_state>
descript: The driver Discovery State Machine is processing an event.
data:     (1) nlp_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0211[] = "%sDSM in event x%x on NPort x%x in state %d Data: x%x";
msgLogDef lpfc_msgBlk0211 = {
	LPFC_LOG_MSG_DI_0211,
	lpfc_mes0211,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0212
message:  DSM out state <rc> on NPort <nlp_DID>
descript: The driver Discovery State Machine completed processing an event.
data:     (1) nlp_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0212[] = "%sDSM out state %d on NPort x%x Data: x%x";
msgLogDef lpfc_msgBlk0212 = {
	LPFC_LOG_MSG_DI_0212,
	lpfc_mes0212,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0213
message:  Reassign scsi id <sid> to NPort <nlp_DID>
descript: A previously bound FCP Target has been rediscovered and reassigned a
          scsi id.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0213[] =
    "%sReassign scsi id x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0213 = {
	LPFC_LOG_MSG_DI_0213,
	lpfc_mes0213,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0214
message:  RSCN received
descript: A RSCN ELS command was received from a fabric.
data:     (1) fc_flag (2) i (3) *lp (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0214[] = "%sRSCN received Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0214 = {
	LPFC_LOG_MSG_DI_0214,
	lpfc_mes0214,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0215
message:  RSCN processed
descript: A RSCN ELS command was received from a fabric and processed.
data:     (1) fc_flag (2) cnt (3) fc_rscn_id_cnt (4) fc_ffstate
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0215[] = "%sRSCN processed Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0215 = {
	LPFC_LOG_MSG_DI_0215,
	lpfc_mes0215,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0216
message:  Assign scandown scsi id <sid> to NPort <nlp_DID>
descript: A scsi id is assigned due to BIND_ALPA.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0216[] =
    "%sAssign scandown scsi id x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0216 = {
	LPFC_LOG_MSG_DI_0216,
	lpfc_mes0216,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0217
message:  Unknown Identifier in RSCN payload
descript: Typically the identifier in the RSCN payload specifies 
          a domain, area or a specific NportID. If neither of 
          these are specified, a warning will be recorded. 
data:     (1) didp->un.word
detail:   (1) Illegal identifier
severity: Error
log:      Always
action:   Potential problem with Fabric. Check with Fabric vendor.
*/
char lpfc_mes0217[] = "%sUnknown Identifier in RSCN payload Data: x%x";
msgLogDef lpfc_msgBlk0217 = {
	LPFC_LOG_MSG_DI_0217,
	lpfc_mes0217,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0218
message:  FDMI Request
descript: The driver is sending an FDMI request to the fabric.
data:     (1) fc_flag (2) hba_state (3) cmdcode
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0218[] = "%sFDMI Request Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0218 = {
	LPFC_LOG_MSG_DI_0218,
	lpfc_mes0218,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0219
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char lpfc_mes0219[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef lpfc_msgBlk0219 = {
	LPFC_LOG_MSG_DI_0219,
	lpfc_mes0219,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0220
message:  FDMI rsp failed
descript: An error response was received to FDMI request
data:     (1) be16_to_cpu(fdmi_cmd)
severity: Information
log:      LOG_DISCOVERY verbose
action:   The fabric does not support FDMI, check fabric configuration.
*/
char lpfc_mes0220[] = "%sFDMI rsp failed Data: x%x";
msgLogDef lpfc_msgBlk0220 = {
	LPFC_LOG_MSG_DI_0220,
	lpfc_mes0220,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0221
message:  FAN timeout
descript: A link up event was received without the login bit set, 
          so the driver waits E_D_TOV for the Fabric to send a FAN. 
          If no FAN if received, a FLOGI will be sent after the timeout. 
data:     None
severity: Warning
log:      LOG_DISCOVERY verbose
action:   None required. The driver recovers from this condition by 
          issuing a FLOGI to the Fabric.
*/
char lpfc_mes0221[] = "%sFAN timeout";
msgLogDef lpfc_msgBlk0221 = {
	LPFC_LOG_MSG_DI_0221,
	lpfc_mes0221,
	lpfc_msgPreambleDIw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0222
message:  Initial FLOGI timeout
descript: The driver sent the initial FLOGI to fabric and never got a response
          back.
data:     None
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char lpfc_mes0222[] = "%sInitial FLOGI timeout";
msgLogDef lpfc_msgBlk0222 = {
	LPFC_LOG_MSG_DI_0222,
	lpfc_mes0222,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0223
message:  Timeout while waiting for NameServer login 
descript: Our login request to the NameServer was not acknowledged 
          within RATOV.
data:     None
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char lpfc_mes0223[] = "%sTimeout while waiting for NameServer login";
msgLogDef lpfc_msgBlk0223 = {
	LPFC_LOG_MSG_DI_0223,
	lpfc_mes0223,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0224
message:  NameServer Query timeout
descript: Node authentication timeout, node Discovery timeout. A NameServer 
          Query to the Fabric or discovery of reported remote NPorts is not 
          acknowledged within R_A_TOV. 
data:     (1) fc_ns_retry (2) fc_max_ns_retry
severity: Error
log:      Always
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char lpfc_mes0224[] = "%sNameServer Query timeout Data: x%x x%x";
msgLogDef lpfc_msgBlk0224 = {
	LPFC_LOG_MSG_DI_0224,
	lpfc_mes0224,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0225
message:  Device Discovery completes
descript: This indicates successful completion of device 
          (re)discovery after a link up. 
data:     None
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0225[] = "%sDevice Discovery completes";
msgLogDef lpfc_msgBlk0225 = {
	LPFC_LOG_MSG_DI_0225,
	lpfc_mes0225,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0226
message:  Device Discovery completion error
descript: This indicates an uncorrectable error was encountered 
          during device (re)discovery after a link up. Fibre 
          Channel devices will not be accessible if this message 
          is displayed.
data:     None
severity: Error
log:      Always
action:   Reboot system. If problem persists, contact Technical 
          Support. Run with verbose mode on for more details.
*/
char lpfc_mes0226[] = "%sDevice Discovery completion error";
msgLogDef lpfc_msgBlk0226 = {
	LPFC_LOG_MSG_DI_0226,
	lpfc_mes0226,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0227
message:  Node Authentication timeout
descript: The driver has lost track of what NPORTs are being authenticated.
data:     None
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char lpfc_mes0227[] = "%sNode Authentication timeout";
msgLogDef lpfc_msgBlk0227 = {
	LPFC_LOG_MSG_DI_0227,
	lpfc_mes0227,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0228
message:  CLEAR LA timeout
descript: The driver issued a CLEAR_LA that never completed
data:     None
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char lpfc_mes0228[] = "%sCLEAR LA timeout";
msgLogDef lpfc_msgBlk0228 = {
	LPFC_LOG_MSG_DI_0228,
	lpfc_mes0228,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0229
message:  Assign scsi ID <sid> to NPort <nlp_DID>
descript: The driver assigned a scsi id to a discovered mapped FCP target.
data:     (1) nlp_bind_type (2) nlp_flag (3) nlp_state (4) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0229[] = "%sAssign scsi ID x%x to NPort x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0229 = {
	LPFC_LOG_MSG_DI_0229,
	lpfc_mes0229,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0230
message:  Cannot assign scsi ID on NPort <nlp_DID>
descript: The driver cannot assign a scsi id to a discovered mapped FCP target.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   Check persistent binding information
*/
char lpfc_mes0230[] = "%sCannot assign scsi ID on NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0230 = {
	LPFC_LOG_MSG_DI_0230,
	lpfc_mes0230,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0231
message:  RSCN timeout
descript: The driver has lost track of what NPORTs have RSCNs pending.
data:     (1) fc_ns_retry (2) fc_max_ns_retry
severity: Error
log:      Always
action:   None required. Driver should recover from this event.
*/
char lpfc_mes0231[] = "%sRSCN timeout Data: x%x x%x";
msgLogDef lpfc_msgBlk0231 = {
	LPFC_LOG_MSG_DI_0231,
	lpfc_mes0231,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0232
message:  Continue discovery with <num_disc_nodes> PLOGIs to go
descript: Device discovery is in progress
data:     (1) fc_plogi_cnt (2) fc_flag (3) phba->hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0232[] =
    "%sContinue discovery with %d PLOGIs to go Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0232 = {
	LPFC_LOG_MSG_DI_0232,
	lpfc_mes0232,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0234
message:  ReDiscovery RSCN
descript: The number / type of RSCNs has forced the driver to go to 
          the nameserver and re-discover all NPORTs.
data:     (1) fc_defer_rscn.q_cnt (2) fc_flag (3) hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0234[] = "%sReDiscovery RSCN Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0234 = {
	LPFC_LOG_MSG_DI_0234,
	lpfc_mes0234,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0235
message:  Deferred RSCN
descript: The driver has received multiple RSCNs and has deferred the 
          processing of the most recent RSCN.
data:     (1) fc_defer_rscn.q_cnt (2) fc_flag (3) hba_state
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0235[] = "%sDeferred RSCN Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0235 = {
	LPFC_LOG_MSG_DI_0235,
	lpfc_mes0235,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0236
message:  NameServer Req
descript: The driver is issuing a nameserver request to the fabric.
data:     (1) cmdcode (2) fc_flag (3) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0236[] = "%sNameServer Req Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0236 = {
	LPFC_LOG_MSG_DI_0236,
	lpfc_mes0236,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0237
message:  Pending Link Event during Discovery
descript: Received link event during discovery. Causes discovery restart.
data:     (1) hba_state (2) ulpIoTag (3) ulpStatus (4) ulpWord[4]
severity: Warning
log:      LOG_DISCOVERY verbose
action:   None required unless problem persist. If persistent check cabling.
*/
char lpfc_mes0237[] =
    "%sPending Link Event during Discovery Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0237 = {
	LPFC_LOG_MSG_DI_0237,
	lpfc_mes0237,
	lpfc_msgPreambleDIw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0238
message:  NameServer Rsp
descript: The driver received a nameserver response.
data:     (1) Did (2) nlp_flag (3) fc_flag (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0238[] = "%sNameServer Rsp Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0238 = {
	LPFC_LOG_MSG_DI_0238,
	lpfc_mes0238,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0239
message:  NameServer Rsp
descript: The driver received a nameserver response.
data:     (1) Did (2) ndlp (3) fc_flag (4) fc_rscn_id_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0239[] = "%sNameServer Rsp Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0239 = {
	LPFC_LOG_MSG_DI_0239,
	lpfc_mes0239,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0240
message:  NameServer Rsp Error
descript: The driver received a nameserver response containig a status error.
data:     (1) CommandResponse.bits.CmdRsp (2) ReasonCode (3) Explanation 
          (4) fc_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char lpfc_mes0240[] = "%sNameServer Rsp Error Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0240 = {
	LPFC_LOG_MSG_DI_0240,
	lpfc_mes0240,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0241
message:  NameServer Rsp Error
descript: The driver received a nameserver response containig a status error.
data:     (1) CommandResponse.bits.CmdRsp (2) ReasonCode (3) Explanation 
          (4) fc_flag
severity: Information
log:      LOG_DISCOVERY verbose
action:   Check Fabric configuration. The driver recovers from this and 
          continues with device discovery.
*/
char lpfc_mes0241[] = "%sNameServer Rsp Error Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0241 = {
	LPFC_LOG_MSG_DI_0241,
	lpfc_mes0241,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0242
message:  Abort outstanding I/O to the Fabric
descript: All outstanding I/Os to the fabric are cleaned up.
data:     (1) Fabric_DID
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0242[] = "%sAbort outstanding I/O to the Fabric x%x";
msgLogDef lpfc_msgBlk0242 = {
	LPFC_LOG_MSG_DI_0242,
	lpfc_mes0242,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0243
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char lpfc_mes0243[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef lpfc_msgBlk0243 = {
	LPFC_LOG_MSG_DI_0243,
	lpfc_mes0243,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0244
message:  Issue FDMI request failed
descript: Cannot issue FDMI request to HBA.
data:     (1) cmdcode
severity: Information
log:      LOG_Discovery verbose
action:   No action needed, informational
*/
char lpfc_mes0244[] = "%sIssue FDMI request failed Data: x%x";
msgLogDef lpfc_msgBlk0244 = {
	LPFC_LOG_MSG_DI_0244,
	lpfc_mes0244,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0245
message:  ALPA based bind method used on an HBA which is in a nonloop topology
descript: ALPA based bind method used on an HBA which is not
          in a loop topology.
data:     (1) topology
severity: Warning
log:      LOG_DISCOVERY verbose
action:   Change the bind method configuration parameter of the HBA to
          1(WWNN) or 2(WWPN) or 3(DID)
*/
char lpfc_mes0245[] =
    "%sALPA based bind method used on an HBA which is in a nonloop topology Data: x%x";
msgLogDef lpfc_msgBlk0245 = {
	LPFC_LOG_MSG_DI_0245,
	lpfc_mes0245,
	lpfc_msgPreambleDIw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0246
message:  RegLogin failed 
descript: Firmware returned failure for the specified RegLogin 
data:     Did, mbxStatus, hbaState 
severity: Error
log:      Always 
action:   This message indicates that the firmware could not do
          RegLogin for the specified Did. It could be because
          there is a limitation on how many nodes an HBA can see. 
*/
char lpfc_mes0246[] = "%sRegLogin failed Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0246 = {
	LPFC_LOG_MSG_DI_0246,
	lpfc_mes0246,
	lpfc_msgPreambleDIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0247
message:  Start Discovery Timer state <hba_state>
descript: Start device discovery / RSCN rescue timer
data:     (1) tmo (2) disctmo (3) fc_plogi_cnt (4) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0247[] = "%sStart Discovery Timer state x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0247 = {
	LPFC_LOG_MSG_DI_0247,
	lpfc_mes0247,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0248
message:  Cancel Discovery Timer state <hba_state>
descript: Cancel device discovery / RSCN rescue timer
data:     (1) fc_flag (2) rc (3) fc_plogi_cnt (4) fc_adisc_cnt
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0248[] = "%sCancel Discovery Timer state x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0248 = {
	LPFC_LOG_MSG_DI_0248,
	lpfc_mes0248,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0249
message: Unsupported Addressing Mode <i> on NPort <nlp_DID> Tgt <sid>
descript: The driver issued a REPORT_LUN SCSI command to a FCP target.
data:     None
severity: Warning
log:      LOG_DISCOVERY | LOG_FCP verbose
action:   Check configuration of target. Driver will default to peripheral
          addressing mode.
*/
char lpfc_mes0249[] = "%sUnsupported Addressing Mode %d on NPort x%x Tgt %d";
msgLogDef lpfc_msgBlk0249 = {
	LPFC_LOG_MSG_DI_0249,
	lpfc_mes0249,
	lpfc_msgPreambleDIw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_DISCOVERY | LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0250
message:  EXPIRED nodev timer
descript: A device disappeared for greater than the configuration parameter 
          (lpfc_nodev_tmo) seconds. All I/O associated with this device 
          will be failed.  
data:     (1) dev_did (2) scsi_id (3) rpi 
severity: Error
log:      Always
action:   Check physical connections to Fibre Channel network and the 
          state of the remote PortID.
*/
char      lpfc_mes0250[] = "%sEXPIRED nodev timer Data: x%x x%x x%x"; 
msgLogDef lpfc_msgBlk0250 = {
          LPFC_LOG_MSG_DI_0250,
          lpfc_mes0250,
          lpfc_msgPreambleDIe,
          LPFC_MSG_OPUT_GLOB_CTRL,
          LPFC_LOG_MSG_TYPE_ERR,
          LOG_DISCOVERY,
          ERRID_LOG_UNEXPECT_EVENT 
};

/*
msgName: lpfc_mes0256
message:  Start nodev timer
descript: A target disappeared from the Fibre Channel network. If the 
          target does not return within nodev-tmo timeout all I/O to 
          the target will fail.
data:     (1) nlp_DID (2) nlp_flag (3) nlp_state (4) nlp
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0256[] = "%sStart nodev timer Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0256 = {
	LPFC_LOG_MSG_DI_0256,
	lpfc_mes0256,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0260
message:  Stop Nodev timeout on NPort <nlp_DID>
descript: The FCP target was rediscovered and I/O can be resumed.
data:     (1) nlp_DID (2) nlp_flag (3) nlp_state (4) nlp
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0260[] = "%sStop nodev timeout on NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0260 = {
	LPFC_LOG_MSG_DI_0260,
	lpfc_mes0260,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0261
message:  FAN received
descript: A FAN was received from the fabric
data:     NONE
severity: Information
log:      LOG_DISCOVERY verbose
action:   No action needed, informational
*/
char lpfc_mes0261[] = "%sFAN received";
msgLogDef lpfc_msgBlk0261 = {
	LPFC_LOG_MSG_DI_0261,
	lpfc_mes0261,
	lpfc_msgPreambleDIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};


/*
 *  Begin MAILBOX LOG Message Structures
 */

/*
msgName: lpfc_mes0300
message:  READ_LA: no buffers
descript: The driver attempted to issue READ_LA mailbox command to the HBA
          but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char lpfc_mes0300[] = "%sREAD_LA: no buffers";
msgLogDef lpfc_msgBlk0300 = {
	LPFC_LOG_MSG_MB_0300,
	lpfc_mes0300,
	lpfc_msgPreambleMBw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};




/*
msgName: lpfc_mes0301
message:  READ_SPARAM: no buffers
descript: The driver attempted to issue READ_SPARAM mailbox command to the 
          HBA but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char lpfc_mes0301[] = "%sREAD_SPARAM: no buffers";
msgLogDef lpfc_msgBlk0301 = {
	LPFC_LOG_MSG_MB_0301,
	lpfc_mes0301,
	lpfc_msgPreambleMBw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0302
message:  REG_LOGIN: no buffers
descript: The driver attempted to issue REG_LOGIN mailbox command to the HBA
          but there were no buffer available.
data:     None
severity: Warning
log:      LOG_MBOX verbose
action:   This message indicates (1) a possible lack of memory resources. Try 
          increasing the lpfc 'num_bufs' configuration parameter to allocate 
          more buffers. (2) A possible driver buffer management problem. If 
          this problem persists, report these errors to Technical Support.
*/
char lpfc_mes0302[] = "%sREG_LOGIN: no buffers Data x%x x%x";
msgLogDef lpfc_msgBlk0302 = {
	LPFC_LOG_MSG_MB_0302,
	lpfc_mes0302,
	lpfc_msgPreambleMBw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0304
message:  Stray Mailbox Interrupt, mbxCommand <cmd> mbxStatus <status>.
descript: Received a mailbox completion interrupt and there are no 
          outstanding mailbox commands.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0304[] = "%sStray Mailbox Interrupt mbxCommand x%x mbxStatus x%x";
msgLogDef lpfc_msgBlk0304 = {
	LPFC_LOG_MSG_MB_0304,
	lpfc_mes0304,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0305
message:  Mbox cmd cmpl error - RETRYing
descript: A mailbox command completed with an error status that causes the 
          driver to reissue the mailbox command.
data:     (1) mbxCommand (2) mbxStatus (3) word1 (4) hba_state
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char lpfc_mes0305[] = "%sMbox cmd cmpl error - RETRYing Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0305 = {
	LPFC_LOG_MSG_MB_0305,
	lpfc_mes0305,
	lpfc_msgPreambleMBi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0306
message:  CONFIG_LINK mbxStatus error <mbxStatus> HBA state <hba_state>
descript: The driver issued a CONFIG_LINK mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0306[] = "%sCONFIG_LINK mbxStatus error x%x HBA state x%x";
msgLogDef lpfc_msgBlk0306 = {
	LPFC_LOG_MSG_MB_0306,
	lpfc_mes0306,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0307
message:  Mailbox cmd <cmd> cmpl <mbox_cmpl> <pmbox> <varWord> <varWord>
          <varWord> <varWord> <varWord> <varWord> <varWord> <varWord>
descript: A mailbox command completed.
data:     none
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char lpfc_mes0307[] = "%sMailbox cmd x%x cmpl x%p, x%x x%x x%x x%x x%x x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0307 = {
	LPFC_LOG_MSG_MB_0307,
	lpfc_mes0307,
	lpfc_msgPreambleMBi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0308
message:  Mbox cmd issue - BUSY
descript: The driver attempted to issue a mailbox command while the mailbox 
          was busy processing the previous command. The processing of the 
          new command will be deferred until the mailbox becomes available.
data:     (1) mbxCommand (2) hba_state (3) sli_flag (4) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char lpfc_mes0308[] = "%sMbox cmd issue - BUSY Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0308 = {
	LPFC_LOG_MSG_MB_0308,
	lpfc_mes0308,
	lpfc_msgPreambleMBi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0309
message:  Mailbox cmd <cmd> issue
descript: The driver is in the process of issuing a mailbox command.
data:     (1) hba_state (2) sli_flag (3) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char lpfc_mes0309[] = "%sMailbox cmd x%x issue Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0309 = {
	LPFC_LOG_MSG_MB_0309,
	lpfc_mes0309,
	lpfc_msgPreambleMBi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0310
message:  Mailbox command <cmd> timeout
descript: A Mailbox command was posted to the adapter and did 
          not complete within 30 seconds.
data:     (1) hba_state (2) sli_flag (3) mbox_active
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If no I/O is going through the adapter, reboot 
          the system. If these problems persist, report these 
          errors to Technical Support.
*/
char lpfc_mes0310[] = "%sMailbox command x%x timeout Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0310 = {
	LPFC_LOG_MSG_MB_0310,
	lpfc_mes0310,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_TIMEOUT
};

/*
msgName: lpfc_mes0311
message:  Mailbox command <cmd> cannot issue
descript: Driver is in the wrong state to issue the specified command
data:     (1) hba_state (2) sli_flag (3) flag
severity: Information
log:      LOG_MBOX verbose
action:   No action needed, informational
*/
char lpfc_mes0311[] = "%sMailbox command x%x cannot issue Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0311 = {
	LPFC_LOG_MSG_MB_0311,
	lpfc_mes0311,
	lpfc_msgPreambleMBi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0312
message:  Ring <ringno> handler: portRspPut <portRspPut> is bigger then rsp
          ring <portRspMax> 
descript: Port rsp ring put index is > size of rsp ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0312[] = "%sRing %d handler: portRspPut %d is bigger then rsp ring %d";
msgLogDef lpfc_msgBlk0312 = {
	LPFC_LOG_MSG_MB_0312,
	lpfc_mes0312,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0313
message:  Ring <ringno> handler: unexpected Rctl <Rctl> Type <Type> received 
descript: The Rctl/Type of a received frame did not match any for the
          configured masks for the specified ring.           
data:     (1) Ring number (2) rctl (3) type
severity: Warning
log:      LOG_MBOX verbose
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0313[] ="%sRing %d handler: unexpected Rctl x%x Type x%x received ";
msgLogDef lpfc_msgBlk0313 = {
	LPFC_LOG_MSG_MB_0313,
	lpfc_mes0313,
	lpfc_msgPreambleSLw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0314
message:  Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring
          <portCmdMax> 
descript: Port cmd ring get index is > size of cmd ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0314[] =
    "%sRing %d issue: portCmdGet %d is bigger then cmd ring %d";
msgLogDef lpfc_msgBlk0314 = {
	LPFC_LOG_MSG_MB_0314,
	lpfc_mes0314,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0315
message:  Ring <ringno> issue: portCmdGet <portCmdGet> is bigger then cmd ring
          <portCmdMax> 
descript: Port cmd ring get index is > size of cmd ring
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0315[] =
    "%sRing %d issue: portCmdGet %d is bigger then cmd ring %d";
msgLogDef lpfc_msgBlk0315 = {
	LPFC_LOG_MSG_MB_0315,
	lpfc_mes0315,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0316
message:  Cmd ring <ringno> put: iotag <iotag> greater then configured max
          <fast_iotag> wd0 <icmd>
descript: The assigned I/O iotag is > the max allowed
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0316[] =
    "%sCmd ring %d put: iotag x%x greater then configured max x%x wd0 x%x";
msgLogDef lpfc_msgBlk0316 = {
	LPFC_LOG_MSG_MB_0316,
	lpfc_mes0316,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0317
message:  Rsp ring <ringno> get: iotag <iotag> greater then configured
          max <fast_iotag> wd0 <irsp>
descript: The assigned I/O iotag is > the max allowed
data:     None
severity: Error
log:      Always
action:   This error could indicate a software driver
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0317[] =
    "%sRsp ring %d get: iotag x%x greater then configured max x%x wd0 x%x";
msgLogDef lpfc_msgBlk0317 = {
	LPFC_LOG_MSG_MB_0317,
	lpfc_mes0317,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0318
message:  Outstanding I/O count for ring <ringno> is at max <fast_iotag>
descript: We cannot assign an I/O tag because none are available. Max allowed
          I/Os are currently outstanding.
data:     None
severity: Information
log:      LOG_SLI verbose
action:   This message indicates the adapter hba I/O queue is full. 
          Typically this happens if you are running heavy I/O on a
          low-end (3 digit) adapter. Suggest you upgrade to our high-end
          adapter.
*/
char lpfc_mes0318[] = "%sOutstanding I/O count for ring %d is at max x%x";
msgLogDef lpfc_msgBlk0318 = {
	LPFC_LOG_MSG_MB_0318,
	lpfc_mes0318,
	lpfc_msgPreambleSLi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0319
descript: The driver issued a READ_SPARAM mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0319[] = "%sREAD_SPARAM mbxStatus error x%x hba state x%x>";
msgLogDef lpfc_msgBlk0319 = {
	LPFC_LOG_MSG_MB_0319,
	lpfc_mes0319,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0320
message:  CLEAR_LA mbxStatus error <mbxStatus> hba state <hba_state>
descript: The driver issued a CLEAR_LA mbox command to the HBA that failed.
data:     none
severity: Error
log:      Always
action:   This error could indicate a firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0320[] = "%sCLEAR_LA mbxStatus error x%x hba state x%x";
msgLogDef lpfc_msgBlk0320 = {
	LPFC_LOG_MSG_MB_0320,
	lpfc_mes0320,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0321
message:  Unknown IOCB command
descript: Received an unknown IOCB command completion.
data:     (1) ulpCommand (2) ulpStatus (3) ulpIoTag (4) ulpContext)
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If these problems persist, report these errors 
          to Technical Support.
*/
char lpfc_mes0321[] = "%sUnknown IOCB command Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0321 = {
	LPFC_LOG_MSG_MB_0321,
	lpfc_mes0321,
	lpfc_msgPreambleSLe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0322
message:  Ring <ringno> handler: unexpected completion IoTag <IoTag>
descript: The driver could not find a matching command for the completion
          received on the specified ring.           
data:     (1) ulpStatus (2) ulpWord[4] (3) ulpCommand (4) ulpContext
severity: Warning
log:      LOG_SLI verbose
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0322[] =
    "%sRing %d handler: unexpected completion IoTag x%x Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0322 = {
	LPFC_LOG_MSG_MB_0322,
	lpfc_mes0322,
	lpfc_msgPreambleSLw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0323
message:  Unknown Mailbox command <cmd> Cmpl 
descript: A unknown mailbox command completed.. 
data:     (1) Mailbox Command
severity: Error
log:      Always
action:   This error could indicate a software driver, firmware or hardware
          problem. Report these errors to Technical Support.
*/
char lpfc_mes0323[] = "%sUnknown Mailbox command %x Cmpl";
msgLogDef lpfc_msgBlk0323 = {
	LPFC_LOG_MSG_MB_0323,
	lpfc_mes0323,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX | LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0324
message:  Adapter initialization error, mbxCmd <cmd> READ_NVPARM, mbxStatus
          <status>
descript: A read nvparams mailbox command failed during config port.
data:     (1) Mailbox Command (2) Mailbox Command Status
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0324[] =
    "%sConfig Port initialization error, mbxCmd x%x READ_NVPARM, mbxStatus x%x";
msgLogDef lpfc_msgBlk0324 = {
	LPFC_LOG_MSG_MB_0324,
	lpfc_mes0324,
	lpfc_msgPreambleMBe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MBOX,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0325
message:  Rsp ring <ringno> error: IOCB
descript: Received an IOCB response error
data:     (1) wd0 (2) wd1 (3) wd2 (4) wd3 (5) wd4 (6) wd5 (7) wd6 (8) wd7
severity: Warning
log:      LOG_SLI verbose
action:   This error could indicate a software driver
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0325[] =
    "%sRsp ring %d error: IOCB Data: x%x x%x x%x x%x x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0325 = {
	LPFC_LOG_MSG_MB_0325,
	lpfc_mes0325,
	lpfc_msgPreambleSLw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0326
message:  Reset HBA
descript: The HBA has been reset
data:     (1) hba_state (2) sli_flag
severity: Information
log:      LOG_SLI verbose
action:   No action needed, informational
*/
char lpfc_mes0326[] =
    "%sReset HBA Data:x%x x%x";
msgLogDef lpfc_msgBlk0326 = {
	LPFC_LOG_MSG_MB_0326,
	lpfc_mes0326,
	lpfc_msgPreambleSLi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_SLI,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin INIT LOG Message Structures
 */

/*
msgName: lpfc_mes0405
message:  Service Level Interface (SLI) 2 selected
descript: A CONFIG_PORT (SLI2) mailbox command was issued. 
data:     None
severity: Information
log:      LOG_INIT verbose
action:   No action needed, informational
*/
char lpfc_mes0405[] = "%sService Level Interface (SLI) 2 selected";
msgLogDef lpfc_msgBlk0405 = {
	LPFC_LOG_MSG_IN_0405,
	lpfc_mes0405,
	lpfc_msgPreambleINi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0410
message:  Cannot find virtual addr for mapped buf on ring <num>
descript: The driver cannot find the specified buffer in its 
          mapping table. Thus it cannot find the virtual address 
          needed to access the data.
data:     (1) first (2) q_first (3) q_last (4) q_cnt
severity: Error
log:      Always
action:   This error could indicate a software driver or firmware 
          problem. If problems persist report these errors to 
          Technical Support.
*/
char lpfc_mes0410[] =
    "%sCannot find virtual addr for mapped buf on ring %d Data x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0410 = {
	LPFC_LOG_MSG_IN_0410,
	lpfc_mes0410,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_NO_RESOURCE
};

/*
msgName: lpfc_mes0411
message:  fcp_bind_method is 4 with Persistent binding - ignoring
          fcp_bind_method
descript: The configuration parameter for fcp_bind_method conflicts with 
          Persistent binding parameter.
data:     (1) a_current (2) fcp_mapping
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0411[] =
    "%sfcp_bind_method is 4 with Persistent binding - ignoring fcp_bind_method Data: x%x x%x";
msgLogDef lpfc_msgBlk0411 = {
	LPFC_LOG_MSG_IN_0411,
	lpfc_mes0411,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0412
message:  Scan-down is out of range - ignoring scan-down
descript: The configuration parameter for Scan-down is out of range.
data:     (1) clp[CFG_SCAN_DOWN].a_current (2) fcp_mapping
severity: Error
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0412[] =
    "%sScan-down is out of range - ignoring scan-down Data: x%x x%x";
msgLogDef lpfc_msgBlk0412 = {
	LPFC_LOG_MSG_IN_0412,
	lpfc_mes0412,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0413
message:  Configuration parameter out of range, resetting to default value
descript: User is attempting to set a configuration parameter to a value not 
          supported by the driver. Resetting the configuration parameter to the
          default value.
data:     (1) a_string (2) a_low (3) a_hi (4) a_default
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0413[] =
    "%sConfiguration parameter lpfc_%s out of range [%d,%d]. Using default value %d";
msgLogDef lpfc_msgBlk0413 = {
	LPFC_LOG_MSG_IN_0413,
	lpfc_mes0413,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0430
message:  WWPN binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing WWPN binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0430[] = "%sWWPN binding entry %d: Syntax error code %d";
msgLogDef lpfc_msgBlk0430 = {
	LPFC_LOG_MSG_IN_0430,
	lpfc_mes0430,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0431
message:  WWNN binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing WWNN binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0431[] = "%sWWNN binding entry %d: Syntax error code %d";
msgLogDef lpfc_msgBlk0431 = {
	LPFC_LOG_MSG_IN_0431,
	lpfc_mes0431,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0432
message:  WWPN binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char lpfc_mes0432[] = "%sWWPN binding entry: node table full";
msgLogDef lpfc_msgBlk0432 = {
	LPFC_LOG_MSG_IN_0432,
	lpfc_mes0432,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0433
message:  WWNN binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char lpfc_mes0433[] = "%sWWNN binding entry: node table full";
msgLogDef lpfc_msgBlk0433 = {
	LPFC_LOG_MSG_IN_0433,
	lpfc_mes0433,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0434
message:  DID binding entry <num>: Syntax error code <code>
descript: A syntax error occured while parsing DID binding 
          configuration information.
data:     None
detail:   Binding syntax error codes
          0  FC_SYNTAX_OK
          1  FC_SYNTAX_OK_BUT_NOT_THIS_BRD
          2  FC_SYNTAX_ERR_ASC_CONVERT
          3  FC_SYNTAX_ERR_EXP_COLON
          4  FC_SYNTAX_ERR_EXP_LPFC
          5  FC_SYNTAX_ERR_INV_LPFC_NUM
          6  FC_SYNTAX_ERR_EXP_T
          7  FC_SYNTAX_ERR_INV_TARGET_NUM
          8  FC_SYNTAX_ERR_EXP_D
          9  FC_SYNTAX_ERR_INV_DEVICE_NUM
          10 FC_SYNTAX_ERR_INV_RRATIO_NUM
          11 FC_SYNTAX_ERR_EXP_NULL_TERM
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes0434[] = "%sDID binding entry %d: Syntax error code %d";
msgLogDef lpfc_msgBlk0434 = {
	LPFC_LOG_MSG_IN_0434,
	lpfc_mes0434,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0435
message:  DID binding entry: node table full
descript: More bindings entries were configured than the driver can handle. 
data:     None
severity: Error config
log:      Always
action:   Make necessary changes to lpfc configuration file such that 
          fewer bindings are configured.
*/
char lpfc_mes0435[] = "%sDID binding entry: node table full";
msgLogDef lpfc_msgBlk0435 = {
	LPFC_LOG_MSG_IN_0435,
	lpfc_mes0435,
	lpfc_msgPreambleINc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0436
message:  Adapter failed to init, timeout, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0436[] = "%sAdapter failed to init, timeout, status reg x%x";
msgLogDef lpfc_msgBlk0436 = {
	LPFC_LOG_MSG_IN_0436,
	lpfc_mes0436,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0437
message:  Adapter failed to init, chipset, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0437[] = "%sAdapter failed to init, chipset, status reg x%x";
msgLogDef lpfc_msgBlk0437 = {
	LPFC_LOG_MSG_IN_0437,
	lpfc_mes0437,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0438
message:  Adapter failed to init, chipset, status reg <status>
descript: The adapter failed during powerup diagnostics after it was reset.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0438[] = "%sAdapter failed to init, chipset, status reg x%x";
msgLogDef lpfc_msgBlk0438 = {
	LPFC_LOG_MSG_IN_0438,
	lpfc_mes0438,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0439
message:  Adapter failed to init, mbxCmd <cmd> READ_REV, mbxStatus <status>
descript: Adapter initialization failed when issuing READ_REV mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0439[] =
    "%sAdapter failed to init, mbxCmd x%x READ_REV, mbxStatus x%x";
msgLogDef lpfc_msgBlk0439 = {
	LPFC_LOG_MSG_IN_0439,
	lpfc_mes0439,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0440
message:  Adapter failed to init, mbxCmd <cmd> READ_REV detected outdated
          firmware
descript: Outdated firmware was detected during initialization. 
data:     (1) read_rev_reset
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. Update 
          firmware. If problems persist report these errors to Technical 
          Support.
*/
char lpfc_mes0440[] =
    "%sAdapter failed to init, mbxCmd x%x READ_REV detected outdated firmware Data: x%x";
msgLogDef lpfc_msgBlk0440 = {
	LPFC_LOG_MSG_IN_0440,
	lpfc_mes0440,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0441
message:  VPD not present on adapter, mbxCmd <cmd> DUMP VPD, mbxStatus <status>
descript: DUMP_VPD mailbox command failed.
data:     None
severity: Information
log:      LOG_INIT verbose
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these to Technical Support.
*/
char lpfc_mes0441[] =
    "%sVPD not present on adapter, mbxCmd x%x DUMP VPD, mbxStatus x%x";
msgLogDef lpfc_msgBlk0441 = {
	LPFC_LOG_MSG_IN_0441,
	lpfc_mes0441,
	lpfc_msgPreambleINi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0442
message:  Adapter failed to init, mbxCmd <cmd> CONFIG_PORT, mbxStatus <status>
descript: Adapter initialization failed when issuing CONFIG_PORT mailbox 
          command.
data:     (1) hbainit
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0442[] =
    "%sAdapter failed to init, mbxCmd x%x CONFIG_PORT, mbxStatus x%x Data: x%x";
msgLogDef lpfc_msgBlk0442 = {
	LPFC_LOG_MSG_IN_0442,
	lpfc_mes0442,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0443
message:  Failed to attach to lpfc adapter: bus <bus> device <device> irq <irq>
descript: An lpfc adapter was found in the pci config but the lpfc driver failed
          to attach.
data:     (1) bus (2) device (3) irq
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0443[] =
    "%sFailed to attach to lpfc adapter: bus %02x device %02x irq %d";
msgLogDef lpfc_msgBlk0443 = {
	LPFC_LOG_MSG_IN_0443,
	lpfc_mes0443,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0446
message:  Adapter failed to init, mbxCmd <cmd> CFG_RING, mbxStatus <status>,
          ring <num>
descript: Adapter initialization failed when issuing CFG_RING mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0446[] =
    "%sAdapter failed to init, mbxCmd x%x CFG_RING, mbxStatus x%x, ring %d";
msgLogDef lpfc_msgBlk0446 = {
	LPFC_LOG_MSG_IN_0446,
	lpfc_mes0446,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0447
message:  Adapter failed init, mbxCmd <cmd> CONFIG_LINK mbxStatus <status>
descript: Adapter initialization failed when issuing CONFIG_LINK mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0447[] =
    "%sAdapter failed init, mbxCmd x%x CONFIG_LINK mbxStatus x%x";
msgLogDef lpfc_msgBlk0447 = {
	LPFC_LOG_MSG_IN_0447,
	lpfc_mes0447,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0448
message:  Adapter failed to init, mbxCmd <cmd> READ_SPARM mbxStatus <status>
descript: Adapter initialization failed when issuing READ_SPARM mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0448[] =
    "%sAdapter failed init, mbxCmd x%x READ_SPARM mbxStatus x%x";
msgLogDef lpfc_msgBlk0448 = {
	LPFC_LOG_MSG_IN_0448,
	lpfc_mes0448,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};


/*
msgName: lpfc_mes0451
message:  Enable interrupt handler failed
descript: The driver attempted to register the HBA interrupt service 
          routine with the host operating system but failed.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or driver problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0451[] = "%sEnable interrupt handler failed";
msgLogDef lpfc_msgBlk0451 = {
	LPFC_LOG_MSG_IN_0451,
	lpfc_mes0451,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0453
message:  Adapter failed to init, mbxCmd <cmd> READ_CONFIG, mbxStatus <status>
descript: Adapter initialization failed when issuing READ_CONFIG mailbox 
          command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0453[] =
    "%sAdapter failed to init, mbxCmd x%x READ_CONFIG, mbxStatus x%x";
msgLogDef lpfc_msgBlk0453 = {
	LPFC_LOG_MSG_IN_0453,
	lpfc_mes0453,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0454
message:  Adapter failed to init, mbxCmd <cmd> INIT_LINK, mbxStatus <status>
descript: Adapter initialization failed when issuing INIT_LINK mailbox command.
data:     None
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0454[] =
    "%sAdapter failed to init, mbxCmd x%x INIT_LINK, mbxStatus x%x";
msgLogDef lpfc_msgBlk0454 = {
	LPFC_LOG_MSG_IN_0454,
	lpfc_mes0454,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0455
message:  Vital Product
descript: Vital Product Data (VPD) contained in HBA flash.
data:     (1) vpd[0] (2) vpd[1] (3) vpd[2] (4) vpd[3]
severity: Information
log:      LOG_INIT verbose
action:   No action needed, informational
*/
char lpfc_mes0455[] = "%sVital Product Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0455 = {
	LPFC_LOG_MSG_IN_0455,
	lpfc_mes0455,
	lpfc_msgPreambleINi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0457
message:  Adapter Hardware Error
descript: The driver received an interrupt indicting a possible hardware 
          problem.
data:     (1) status (2) status1 (3) status2
severity: Error
log:      Always
action:   This error could indicate a hardware or firmware problem. If 
          problems persist report these errors to Technical Support.
*/
char lpfc_mes0457[] = "%sAdapter Hardware Error Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0457 = {
	LPFC_LOG_MSG_IN_0457,
	lpfc_mes0457,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
msgName: lpfc_mes0458
message:  Bring Adapter online
descript: The FC driver has received a request to bring the adapter 
          online. This may occur when running lputil.
data:     None
severity: Warning
log:      LOG_INIT verbose
action:   None required
*/
char lpfc_mes0458[] = "%sBring Adapter online";
msgLogDef lpfc_msgBlk0458 = {
	LPFC_LOG_MSG_IN_0458,
	lpfc_mes0458,
	lpfc_msgPreambleINw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0460
message:  Bring Adapter offline
descript: The FC driver has received a request to bring the adapter 
          offline. This may occur when running lputil.
data:     None
severity: Warning
log:      LOG_INIT verbose
action:   None required
*/
char lpfc_mes0460[] = "%sBring Adapter offline";
msgLogDef lpfc_msgBlk0460 = {
	LPFC_LOG_MSG_IN_0460,
	lpfc_mes0460,
	lpfc_msgPreambleINw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_INIT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0462
message:  Too many cmd / rsp ring entries in SLI2 SLIM
descript: The configuration parameter for Scan-down is out of range.
data:     (1) totiocb (2) MAX_SLI2_IOCB
severity: Error
log:      Always
action:   Software driver error.
          If this problem persists, report these errors to Technical Support.
*/
char lpfc_mes0462[] =
    "%sToo many cmd / rsp ring entries in SLI2 SLIM Data: x%x x%x";
msgLogDef lpfc_msgBlk0462 = {
	LPFC_LOG_MSG_IN_0462,
	lpfc_mes0462,
	lpfc_msgPreambleINe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_INIT,
	ERRID_LOG_INIT
};

/*
 *  Begin IP LOG Message Structures
 */

/*
msgName: lpfc_mes0600
message:  FARP-RSP received from DID <did>.
descript: A FARP ELS command response was received.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char lpfc_mes0600[] = "%sFARP-RSP received from DID x%x";
msgLogDef lpfc_msgBlk0600 = {
	LPFC_LOG_MSG_IP_0600,
	lpfc_mes0600,
	lpfc_msgPreambleIPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0601
message:  FARP-REQ received from DID <did>
descript: A FARP ELS command request was received.
data:     None
severity: Information
log:      LOG_IP verbose
action:   No action needed, informational
*/
char lpfc_mes0601[] = "%sFARP-REQ received from DID x%x";
msgLogDef lpfc_msgBlk0601 = {
	LPFC_LOG_MSG_IP_0601,
	lpfc_mes0601,
	lpfc_msgPreambleIPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};


/*
msgName: lpfc_mes0610
message:  FARP Request sent to remote DID
descript: A send to a remote IP address has no node in the driver's nodelists.
          Send a FARP request to obtain the node's HW address.
data:     (1) IEEE[0] (2) IEEE[1] (3) IEEE[2] (4) IEEE[3] (5) IEEE[4]
          (6) IEEE[5] 
severity: Information
log:      LOG_IP verbose
action:   Issue FARP and wait for PLOGI from remote node.
*/
char lpfc_mes0610[] =
    "%sFARP Request sent to remote HW Address %02x-%02x-%02x-%02x-%02x-%02x";
msgLogDef lpfc_msgBlk0610 = {
	LPFC_LOG_MSG_IP_0610,
	lpfc_mes0610,
	lpfc_msgPreambleIPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_IP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin FCP LOG Message Structures
 */

/*
msgName: lpfc_mes0701
message:  Issue Abort Task Set to TGT <num> LUN <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific device. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) scsi_id (2) lun_id (1) (3) rpi (4) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of device in question. 
*/
char lpfc_mes0701[] = "%sIssue Abort Task Set to TGT x%x LUN x%llx Data: x%x x%x";
msgLogDef lpfc_msgBlk0701 = {
	LPFC_LOG_MSG_FP_0701,
	lpfc_mes0701,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0702
message:  Issue Target Reset to TGT <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific target. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) rpi (2) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of target in question. 
*/
char lpfc_mes0702[] = "%sIssue Target Reset to TGT %d Data: x%x x%x";
msgLogDef lpfc_msgBlk0702 = {
	LPFC_LOG_MSG_FP_0702,
	lpfc_mes0702,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0703
message:  Issue LUN Reset to TGT <num> LUN <num>
descript: The SCSI layer detected that it needs to abort all I/O 
          to a specific device. This results in an FCP Task 
          Management command to abort the I/O in progress. 
data:     (1) scsi_id (2) lun_id (3) rpi (4) flags
severity: Information
log:      LOG_FCP verbose
action:   Check state of device in question. 
*/
char lpfc_mes0703[] = "%sIssue LUN Reset to TGT x%x LUN x%llx Data: x%x x%x";
msgLogDef lpfc_msgBlk0703 = {
	LPFC_LOG_MSG_FP_0703,
	lpfc_mes0703,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0712
message:  SCSI layer issued abort device
descript: The SCSI layer is requesting the driver to abort 
          I/O to a specific device.
data:     (1) target (2) lun (3)
severity: Error
log:      Always
action:   Check state of device in question.
*/
char lpfc_mes0712[] = "%sSCSI layer issued abort device Data: x%x x%llx";
msgLogDef lpfc_msgBlk0712 = {
	LPFC_LOG_MSG_FP_0712,
	lpfc_mes0712,
	lpfc_msgPreambleFPe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0713
message:  SCSI layer issued Target Reset
descript: The SCSI layer is requesting the driver to abort 
          I/O to a specific target.
data:     (1) target (2) lun 
severity: Error
log:      Always
action:   Check state of target in question.
*/
char lpfc_mes0713[] = "%sSCSI layer issued Target Reset Data: x%x x%llx";
msgLogDef lpfc_msgBlk0713 = {
	LPFC_LOG_MSG_FP_0713,
	lpfc_mes0713,
	lpfc_msgPreambleFPe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0714
message:  SCSI layer issued Bus Reset
descript: The SCSI layer is requesting the driver to abort 
          all I/Os to all targets on this HBA.
data:     (1) tgt (2) lun (3) rc - success / failure
severity: Error
log:      Always
action:   Check state of targets in question.
*/
char lpfc_mes0714[] = "%sSCSI layer issued Bus Reset Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0714 = {
	LPFC_LOG_MSG_FP_0714,
	lpfc_mes0714,
	lpfc_msgPreambleFPe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0716
message:  FCP Read Underrun, expected <len>, residual <resid>
descript: FCP device provided less data than was requested.
data:     (1) fcpi_parm (2) cmnd[0] (3) underflow 
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0716[] =
    "%sFCP Read Underrun, expected %d, residual %d Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0716 = {
	LPFC_LOG_MSG_FP_0716,
	lpfc_mes0716,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0717
message:  FCP command <cmd> residual underrun converted to error
descript: The driver convert this underrun condition to an error based 
          on the underflow field in the SCSI cmnd.
data:     (1) len (2) resid (3) underflow
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0717[] =
    "%sFCP command x%x residual underrun converted to error Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0717 = {
	LPFC_LOG_MSG_FP_0717,
	lpfc_mes0717,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0729
message:  FCP cmd <cmnd> failed <target>/<lun>
descript: The specifed device failed an FCP command. 
data:     (1) cmnd (2) scsi_id (3) lun_id (4) status (5) result (6) xri (7) iotag
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char lpfc_mes0729[] =
    "%sFCP cmd x%x failed, x%x x%llx, status: x%x result: x%x Data: x%x x%x";
msgLogDef lpfc_msgBlk0729 = {
	LPFC_LOG_MSG_FP_0729,
	lpfc_mes0729,
	lpfc_msgPreambleFPw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0730
message:  FCP command failed: RSP
descript: The FCP command failed with a response error.
data:     (1) Status2 (2) Status3 (3) ResId (4) SnsLen (5) RspLen (6) Info3
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char lpfc_mes0730[] = "%sFCP command failed: RSP Data: x%x x%x x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0730 = {
	LPFC_LOG_MSG_FP_0730,
	lpfc_mes0730,
	lpfc_msgPreambleFPw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0734
message:  FCP Read Check Error
descript: The issued FCP command returned a Read Check Error
data:     (1) fcpDl (2) rspResId (3) fcpi_parm (4) cdb[0]
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char lpfc_mes0734[] = "%sFCP Read Check Error Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0734 = {
	LPFC_LOG_MSG_FP_0734,
	lpfc_mes0734,
	lpfc_msgPreambleFPw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_FCP,
	ERRID_LOG_HDW_ERR
};

/*
msgName: lpfc_mes0735
message:  FCP Read Check Error with Check Condition
descript: The issued FCP command returned a Read Check Error and a 
          Check condition.
data:     (1) fcpDl (2) rspResId (3) fcpi_parm (4) cdb[0]
severity: Warning
log:      LOG_FCP verbose
action:   Check the state of the target in question.
*/
char lpfc_mes0735[] =
    "%sFCP Read Check Error with Check Condition Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0735 = {
	LPFC_LOG_MSG_FP_0735,
	lpfc_mes0735,
	lpfc_msgPreambleFPw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_FCP | LOG_CHK_COND,
	ERRID_LOG_HDW_ERR
};

/*
msgName: lpfc_mes0737
message:  <ASC ASCQ> Check condition received
descript: The issued FCP command resulted in a Check Condition.
data:     (1) CFG_CHK_COND_ERR (2) CFG_DELAY_RSP_ERR (3) *lp
severity: Information
log:      LOG_FCP | LOG_CHK_COND verbose
action:   No action needed, informational
*/
char lpfc_mes0737[] = "%sx%x Check condition received Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0737 = {
	LPFC_LOG_MSG_FP_0737,
	lpfc_mes0737,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP | LOG_CHK_COND,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0738
message:  Scheduler received Queue Full status from FCP device <tgt> <lun>.
descript: Scheduler received a Queue Full error status from specified FCP
          device.
data:     (1) qfull_retry_count (2) qfull_retries (3) currentOutstanding
          (4) maxOutstanding
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0738[] =
    "%sScheduler received Queue Full status from FCP device %d %d Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0738 = {
	LPFC_LOG_MSG_FP_0738,
	lpfc_mes0738,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0747
message:  Cmpl Target Reset
descript: Target Reset completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0747[] = "%sCmpl Target Reset Data: x%x x%llx x%x x%x x%x"; 
msgLogDef lpfc_msgBlk0747 = {
	LPFC_LOG_MSG_FP_0747,
	lpfc_mes0747,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0748
message:  Cmpl LUN Reset
descript: LUN Reset completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0748[] = "%sCmpl LUN Reset Data: x%x x%llx x%x x%x x%x"; 
msgLogDef lpfc_msgBlk0748 = {
	LPFC_LOG_MSG_FP_0748,
	lpfc_mes0748,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0749
message:  Cmpl Abort Task Set
descript: Abort Task Set completed.
data:     (1) scsi_id (2) lun_id (3) Error (4) statLocalError (5) *cmd + WD7
severity: Information
log:      LOG_FCP verbose
action:   No action needed, informational
*/
char lpfc_mes0749[] = "%sCmpl Abort Task Set Data: x%x x%llx x%x x%x x%x"; 
msgLogDef lpfc_msgBlk0749 = {
	LPFC_LOG_MSG_FP_0749,
	lpfc_mes0749,
	lpfc_msgPreambleFPi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_FCP,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0754
message:  SCSI timeout
descript: An FCP IOCB command was posted to a ring and did not complete 
          within ULP timeout seconds.
data:     (1) did (2) sid (3) command (4) iotag
severity: Error
log:      Always
action:   If no I/O is going through the adapter, reboot the system; 
          If problem persists, contact Technical Support.
*/
char lpfc_mes0754[] = "%sSCSI timeout Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0754 = {
	LPFC_LOG_MSG_FP_0754,
	lpfc_mes0754,
	lpfc_msgPreambleFPe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_FCP,
	ERRID_LOG_TIMEOUT
};

/*
 *  Begin NODE LOG Message Structures
 */

/*
msgName: lpfc_mes0900
message:  Cleanup node for NPort <nlp_DID>
descript: The driver node table entry for a remote NPort was removed.
data:     (1) nlp_flag (2) nlp_state (3) nlp_rpi
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0900[] = "%sCleanup node for NPort x%x Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk0900 = {
	LPFC_LOG_MSG_ND_0900,
	lpfc_mes0900,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0901
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0901[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0901 = {
	LPFC_LOG_MSG_ND_0901,
	lpfc_mes0901,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0902
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0902[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0902 = {
	LPFC_LOG_MSG_ND_0902,
	lpfc_mes0902,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0903
message:  Add scsiid <sid> to BIND list 
descript: The driver is putting the node table entry on the binding list.
data:     (1) bind_cnt (2) nlp_DID (3) bind_type (4) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0903[] = "%sAdd scsiid %d to BIND list Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0903 = {
	LPFC_LOG_MSG_ND_0903,
	lpfc_mes0903,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0904
message:  Add NPort <did> to PLOGI list
descript: The driver is putting the node table entry on the plogi list.
data:     (1) plogi_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0904[] = "%sAdd NPort x%x to PLOGI list Data: x%x x%x";
msgLogDef lpfc_msgBlk0904 = {
	LPFC_LOG_MSG_ND_0904,
	lpfc_mes0904,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0905
message:  Add NPort <did> to ADISC list
descript: The driver is putting the node table entry on the adisc list.
data:     (1) adisc_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0905[] = "%sAdd NPort x%x to ADISC list Data: x%x x%x";
msgLogDef lpfc_msgBlk0905 = {
	LPFC_LOG_MSG_ND_0905,
	lpfc_mes0905,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0906
message:  Add NPort <did> to UNMAP list
descript: The driver is putting the node table entry on the unmap list.
data:     (1) unmap_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0906[] = "%sAdd NPort x%x to UNMAP list Data: x%x x%x";
msgLogDef lpfc_msgBlk0906 = {
	LPFC_LOG_MSG_ND_0906,
	lpfc_mes0906,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0907
message:  Add NPort <did> to MAP list scsiid <sid>
descript: The driver is putting the node table entry on the mapped list.
data:     (1) map_cnt (2) blp
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0907[] = "%sAdd NPort x%x to MAP list scsiid %d Data: x%x x%x";
msgLogDef lpfc_msgBlk0907 = {
	LPFC_LOG_MSG_ND_0907,
	lpfc_mes0907,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0908
message:  FIND node DID bind
descript: The driver is searching for a node table entry, on the 
          binding list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0908[] = "%sFIND node DID bind Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0908 = {
	LPFC_LOG_MSG_ND_0908,
	lpfc_mes0908,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0910
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0910[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0910 = {
	LPFC_LOG_MSG_ND_0910,
	lpfc_mes0910,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0911
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0911[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0911 = {
	LPFC_LOG_MSG_ND_0911,
	lpfc_mes0911,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0929
message:  FIND node DID unmapped
descript: The driver is searching for a node table entry, on the 
          unmapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0929[] = "%sFIND node DID unmapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0929 = {
	LPFC_LOG_MSG_ND_0929,
	lpfc_mes0929,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0930
message:  FIND node DID mapped
descript: The driver is searching for a node table entry, on the 
          mapped node list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0930[] = "%sFIND node DID mapped Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0930 = {
	LPFC_LOG_MSG_ND_0930,
	lpfc_mes0930,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0931
message:  FIND node DID bind
descript: The driver is searching for a node table entry, on the 
          binding list, based on DID.
data:     (1) nlp (2) nlp_DID (3) nlp_flag (4) data1
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0931[] = "%sFIND node DID bind Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk0931 = {
	LPFC_LOG_MSG_ND_0931,
	lpfc_mes0931,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes0932
message:  FIND node did <did> NOT FOUND
descript: The driver was searching for a node table entry based on DID 
          and the entry was not found.
data:     (1) order
severity: Information
log:      LOG_NODE verbose
action:   No action needed, informational
*/
char lpfc_mes0932[] = "%sFIND node did x%x NOT FOUND Data: x%x";
msgLogDef lpfc_msgBlk0932 = {
	LPFC_LOG_MSG_ND_0932,
	lpfc_mes0932,
	lpfc_msgPreambleNDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_NODE,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin MISC LOG message structures
 */

/*
msgName: lpfc_mes1208
descript: The CT response returned more data than the user buffer could hold. 
message:  C_CT Request error
data:     (1) dfc_flag (2) 4096
severity: Information
log:      LOG_MISC verbose
action:   Modify user application issuing CT request to allow for a larger 
          response buffer.
*/
char lpfc_mes1208[] = "%sC_CT Request error Data: x%x x%x";
msgLogDef lpfc_msgBlk1208 = {
	LPFC_LOG_MSG_MI_1208,
	lpfc_mes1208,
	lpfc_msgPreambleMIi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1210
message:  Convert ASC to hex. Input byte cnt < 1
descript: ASCII string to hex conversion failed. Input byte count < 1.
data:     none
severity: Error
log:      Always
action:   This error could indicate a software driver problem. 
          If problems persist report these errors to Technical Support.
*/
char lpfc_mes1210[] = "%sConvert ASC to hex. Input byte cnt < 1";
msgLogDef lpfc_msgBlk1210 = {
	LPFC_LOG_MSG_MI_1210,
	lpfc_mes1210,
	lpfc_msgPreambleMIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1212
message:  Convert ASC to hex. Output buffer to small 
descript: ASCII string to hex conversion failed. The output buffer byte 
          size is less than 1/2 of input byte count. Every 2 input chars 
          (bytes) require 1 output byte.
data:     none
severity: Error
log:      Always
action:   This error could indicate a software driver problem. 
          If problems persist report these errors to Technical Support.
*/
char lpfc_mes1212[] = "%sConvert ASC to hex. Output buffer too small";
msgLogDef lpfc_msgBlk1212 = {
	LPFC_LOG_MSG_MI_1212,
	lpfc_mes1212,
	lpfc_msgPreambleMIe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1213
message:  Convert ASC to hex. Input char seq not ASC hex.
descript: The ASCII hex input string contains a non-ASCII hex characters
data:     none
severity: Error configuration
log:      Always
action:   Make necessary changes to lpfc configuration file.
*/
char lpfc_mes1213[] = "%sConvert ASC to hex. Input char seq not ASC hex.";
msgLogDef lpfc_msgBlk1213 = {
	LPFC_LOG_MSG_MI_1213,
	lpfc_mes1213,
	lpfc_msgPreambleMIc,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR_CFG,
	LOG_MISC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin LINK LOG Message Structures
 */

/*
msgName: lpfc_mes1300
message:  Re-establishing Link, timer expired
descript: The driver detected a condition where it had to re-initialize 
          the link.
data:     (1) fc_flag (2) fc_ffstate
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char lpfc_mes1300[] = "%sRe-establishing Link, timer expired Data: x%x x%x";
msgLogDef lpfc_msgBlk1300 = {
	LPFC_LOG_MSG_LK_1300,
	lpfc_mes1300,
	lpfc_msgPreambleLKe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1301
message:  Re-establishing Link
descript: The driver detected a condition where it had to re-initialize 
          the link.
data:     (1) status (2) status1 (3) status2
severity: Information
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char lpfc_mes1301[] = "%sRe-establishing Link Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1301 = {
	LPFC_LOG_MSG_LK_1301,
	lpfc_mes1301,
	lpfc_msgPreambleLKi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1302
message:  Reset link speed to auto. 1G HBA cfg'd for 2G
descript: The driver is reinitializing the link speed to auto-detect.
data:     (1) current link speed
severity: Warning
log:      LOG_LINK_EVENT verbose
action:   None required
*/
char lpfc_mes1302[] =
    "%s Invalid speed for this board: Reset link speed to auto: x%x";
msgLogDef lpfc_msgBlk1302 = {
	LPFC_LOG_MSG_LK_1302,
	lpfc_mes1302,
	lpfc_msgPreambleLKw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1303
message:  Link Up Event <eventTag> received
descript: A link up event was received. It is also possible for 
          multiple link events to be received together. 
data:     (1) fc_eventTag (2) granted_AL_PA (3) UlnkSpeed (4) alpa_map[0]
detail:   If link events received, log (1) last event number 
          received, (2) ALPA granted, (3) Link speed 
          (4) number of entries in the loop init LILP ALPA map. 
          An ALPA map message is also recorded if LINK_EVENT 
          verbose mode is set. Each ALPA map message contains 
          16 ALPAs. 
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char lpfc_mes1303[] = "%sLink Up Event x%x received Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk1303 = {
	LPFC_LOG_MSG_LK_1303,
	lpfc_mes1303,
	lpfc_msgPreambleLKe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1304
message:  Link Up Event ALPA map
descript: A link up event was received.
data:     (1) wd1 (2) wd2 (3) wd3 (4) wd4
severity: Warning
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char lpfc_mes1304[] = "%sLink Up Event ALPA map Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk1304 = {
	LPFC_LOG_MSG_LK_1304,
	lpfc_mes1304,
	lpfc_msgPreambleLKw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1305
message:  Link Down Event <eventTag> received
descript: A link down event was received.
data:     (1) fc_eventTag (2) hba_state (3) fc_flag
severity: Error
log:      Always
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network.
*/
char lpfc_mes1305[] = "%sLink Down Event x%x received Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1305 = {
	LPFC_LOG_MSG_LK_1305,
	lpfc_mes1305,
	lpfc_msgPreambleLKe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1306
message:  Link Down timeout
descript: The link was down for greater than the configuration parameter 
          (lpfc_linkdown_tmo) seconds. All I/O associated with the devices
          on this link will be failed.  
data:     (1) hba_state (2) fc_flag (3) fc_ns_retry
severity: Warning
log:      LOG_LINK_EVENT | LOG_DISCOVERY verbose
action:   Check HBA cable/connection to Fibre Channel network.
*/
char lpfc_mes1306[] = "%sLink Down timeout Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1306 = {
	LPFC_LOG_MSG_LK_1306,
	lpfc_mes1306,
	lpfc_msgPreambleLKw,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_WARN,
	LOG_LINK_EVENT | LOG_DISCOVERY,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1307
message:  READ_LA mbox error <mbxStatus> state <hba_state>
descript: The driver cannot determine what type of link event occurred.
data:     None
severity: Information
log:      LOG_LINK_EVENT verbose
action:   If numerous link events are occurring, check physical 
          connections to Fibre Channel network. Could indicate
          possible hardware or firmware problem.
*/
char lpfc_mes1307[] = "%sREAD_LA mbox error x%x state x%x";
msgLogDef lpfc_msgBlk1307 = {
	LPFC_LOG_MSG_LK_1307,
	lpfc_mes1307,
	lpfc_msgPreambleLKi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LINK_EVENT,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
 *  Begin XXX LOG Message Structures
 */

/*
 *  Begin Libdfc Message Structures
 */

/*
msgName: lpfc_mes1600
message:  libdfc debug entry
descript: Entry point for processing debug diagnostic routines 
data:     (1) c_cmd (2) c_arg1 (3) c_arg2 (4) c_outsz
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1600[] = "%slibdfc debug entry Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk1600 = {
	LPFC_LOG_MSG_IO_1600,
	lpfc_mes1600,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1601
message:  libdfc debug exit
descript: Exit point for processing debug diagnostic routines
data:     (1) rc (2) c_outsz (3) c_dataout
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1601[] = "%slibdfc debug exit Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1601 = {
	LPFC_LOG_MSG_IO_1601,
	lpfc_mes1601,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1602
message:  libdfc hbaapi entry
descript: Entry point for processing hbaapi diagnostic routines 
data:     (1) c_cmd (2) c_arg1 (3) c_arg2 (4) c_outsz
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1602[] = "%slibdfc hbaapi entry Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk1602 = {
	LPFC_LOG_MSG_IO_1602,
	lpfc_mes1602,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1603
message:  libdfc hbaapi exit
descript: Exit point for processing hbaapi diagnostic routines
data:     (1) rc (2) c_outsz (3) c_dataout
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1603[] = "%slibdfc hbaapi exit Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1603 = {
	LPFC_LOG_MSG_IO_1603,
	lpfc_mes1603,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1604
message:  libdfc:error
descript: SCSI send request buffer size limited exceeded
data:     (1) error number index
severity: Error
log:      Always
action:   Reduce application program's SCSI send request buffer size to < 320K
          bytes.  
*/
char lpfc_mes1604[] = "%slibdfc error Data: %d";
msgLogDef lpfc_msgBlk1604 = {
	LPFC_LOG_MSG_IO_1604,
	lpfc_mes1604,
	lpfc_msgPreambleLDe,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_ERR,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1606
message:  libdfc util entry
descript: Entry point for processing util diagnostic routines 
data:     (1) c_cmd (2) c_arg1 (3) c_arg2 (4) c_outsz
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1606[] = "%slibdfc util entry Data: x%x x%x x%x x%x";
msgLogDef lpfc_msgBlk1606 = {
	LPFC_LOG_MSG_IO_1606,
	lpfc_mes1606,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

/*
msgName: lpfc_mes1607
message:  libdfc util exit
descript: Exit point for processing util diagnostic routines
data:     (1) rc (2) c_outsz (3) c_dataout
severity: Information
log:      LOG_LIBDFC verbose
action:   No action needed, informational
*/
char lpfc_mes1607[] = "%slibdfc util exit Data: x%x x%x x%x";
msgLogDef lpfc_msgBlk1607 = {
	LPFC_LOG_MSG_IO_1607,
	lpfc_mes1607,
	lpfc_msgPreambleLDi,
	LPFC_MSG_OPUT_GLOB_CTRL,
	LPFC_LOG_MSG_TYPE_INFO,
	LOG_LIBDFC,
	ERRID_LOG_UNEXPECT_EVENT
};

