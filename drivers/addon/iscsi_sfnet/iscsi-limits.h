#ifndef ISCSI_LIMITS_H_
#define ISCSI_LIMITS_H_

/*
 * iSCSI driver for Linux
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi-devel@lists.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi-limits.h,v 1.4.2.1 2004/08/10 23:04:48 coughlan Exp $ 
 *
 * iscsi-limits.h
 *
 */

#define ISCSI_CMDS_PER_LUN       12
#define ISCSI_CANQUEUE           64
#define ISCSI_PREALLOCATED_TASKS 64
#define ISCSI_MAX_SG             64
#define ISCSI_MAX_CMD_LEN        12
#define ISCSI_MAX_TASKS_PER_SESSION (ISCSI_CMDS_PER_LUN * ISCSI_MAX_LUN)

/* header plus alignment plus login pdu size + pad */
#define ISCSI_RXCTRL_SIZE        ((2 * sizeof(struct IscsiHdr)) + 4096 + 4)

#endif
