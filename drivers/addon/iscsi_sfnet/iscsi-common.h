#ifndef ISCSI_COMMON_H_
#define ISCSI_COMMON_H_

/*
 * iSCSI connection daemon
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
 * $Id: iscsi-common.h,v 1.6.2.1 2004/08/10 23:04:49 coughlan Exp $
 *
 * include for common info needed by both the daemon and kernel module
 *
 */

#define ISCSI_MAX_HBAS                   1

/* these are limited by the packing of the GET_IDLUN ioctl */
#define ISCSI_MAX_CHANNELS_PER_HBA       256
#define ISCSI_MAX_TARGET_IDS_PER_BUS     256
#define ISCSI_MAX_LUNS_PER_TARGET        256

/* iSCSI bus numbers are a 1:1 mapping of the Linux HBA/channel combos onto
 * non-negative integers, so that we don't have to care what number
 * the OS assigns to each HBA, and we don't care if they're non-contiguous.
 * We use the ordering of each HBA in the iSCSI kernel module's hba_list, 
 * and number the channels on each HBA sequentially.
 */
#define ISCSI_MAX_BUS_IDS            (ISCSI_MAX_HBAS * ISCSI_MAX_CHANNELS_PER_HBA)

/* compatibility names */
#define ISCSI_MAX_TARGETS	   ISCSI_MAX_TARGET_IDS_PER_BUS
#define ISCSI_MAX_LUN	           ISCSI_MAX_LUNS_PER_TARGET

#ifndef __cplusplus
typedef enum boolean {
    false = 0,
    true = 1
} bool;
#endif

#endif
