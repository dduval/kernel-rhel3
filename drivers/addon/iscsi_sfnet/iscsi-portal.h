#ifndef ISCSI_PORTAL_H_
#define ISCSI_PORTAL_H_
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
 * $Id: iscsi-portal.h,v 1.5.2.1 2004/08/10 23:04:49 coughlan Exp $
 *
 * portal info structure used in ioctls and the kernel module
 *
 */

typedef struct iscsi_portal_info {
    int login_timeout;
    int auth_timeout;
    int active_timeout;
    int idle_timeout;
    int ping_timeout;
    int abort_timeout;
    int reset_timeout;
    int replacement_timeout;	/* FIXME: should this be
				 * per-session rather than
				 * per-portal? 
				 */
    unsigned int disk_command_timeout;	/* FIXME: should
					 * this be
					 * per-session
					 * rather than
					 * per-portal? 
					 */
    int InitialR2T;
    int ImmediateData;
    int MaxRecvDataSegmentLength;
    int FirstBurstLength;
    int MaxBurstLength;
    int DefaultTime2Wait;
    int DefaultTime2Retain;
    int HeaderDigest;
    int DataDigest;
    int ip_length;
    unsigned char ip_address[16];
    int port;
    int tag;
    int tcp_window_size;
    int type_of_service;
    int preference;		/* preference relative to
				 * other portals, higher is
				 * better 
				 */
} iscsi_portal_info_t;

#endif
