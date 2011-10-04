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
 * $Id: lpfc_dfc.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef _H_LPFC_DFC
#define _H_LPFC_DFC

#define LPFC_MAX_RING_MASK  4	/* max num of rctl/type masks allowed per
				   ring */
#define LPFC_MAX_RING       4	/* max num of SLI rings used by driver */

#define LPFC_INQSN_SZ      64   /* Max size of Inquiry serial number */


/* Defines for RegisterForEvent mask */
#define FC_REG_LINK_EVENT       0x1	/* Register for link up / down events */
#define FC_REG_RSCN_EVENT       0x2	/* Register for RSCN events */
#define FC_REG_CT_EVENT         0x4	/* Register for CT request events */

#define FC_REG_EVENT_MASK       0x2f	/* event mask */
#define FC_REG_ALL_PORTS        0x80	/* Register for all ports */

#define MAX_FC_EVENTS 8		/* max events user process can wait for per
				   HBA */
#define FC_FSTYPE_ALL 0xffff	/* match on all fsTypes */

/* Defines for error codes */
#define FC_ERROR_BUFFER_OVERFLOW          0xff
#define FC_ERROR_RESPONSE_TIMEOUT         0xfe
#define FC_ERROR_LINK_UNAVAILABLE         0xfd
#define FC_ERROR_INSUFFICIENT_RESOURCES   0xfc
#define FC_ERROR_EXISTING_REGISTRATION    0xfb
#define FC_ERROR_INVALID_TAG              0xfa
#define FC_ERROR_INVALID_WWN              0xf9
#define FC_ERROR_CREATEVENT_FAILED        0xf8

/* values for a_flag */
#define CFG_EXPORT      0x1	/* Export this parameter to the end user */
#define CFG_IGNORE      0x2	/* Ignore this parameter */
#define CFG_DEFAULT     0x8000	/* Reestablishing Link */

/* values for a_changestate */
#define CFG_REBOOT      0x0	/* Changes effective after ystem reboot */
#define CFG_DYNAMIC     0x1	/* Changes effective immediately */
#define CFG_RESTART     0x2	/* Changes effective after driver restart */

/* the icfgparam structure - internal use only */
typedef struct ICFGPARAM {
	char *a_string;
	uint32_t a_low;
	uint32_t a_hi;
	uint32_t a_default;
	uint32_t a_current;
	uint16_t a_flag;
	uint16_t a_changestate;
	char *a_help;
} iCfgParam;

/* User Library level Event structure */
typedef struct reg_evt {
	uint32_t e_mask;
	uint32_t e_gstype;
	uint32_t e_pid;
	uint32_t e_firstchild;
	uint32_t e_outsz;
	uint32_t e_pad;
	void (*e_func) (uint32_t, ...);
	void *e_ctx;
	void *e_out;
} RegEvent;

/* Defines for portid for CT interface */
#define CT_FabricCntlServer ((uint32_t)0xfffffd)
#define CT_NameServer       ((uint32_t)0xfffffc)
#define CT_TimeServer       ((uint32_t)0xfffffb)
#define CT_MgmtServer       ((uint32_t)0xfffffa)

#define IOCB_ENTRY(ring,slot) ((IOCB_t *)(((char *)(ring)) + ((slot) * 32)))
#endif				/* _H_LPFC_DIAG */
