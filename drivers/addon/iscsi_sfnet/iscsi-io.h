#ifndef ISCSI_IO_H_
#define ISCSI_IO_H_

/*
 * iSCSI driver for Linux
 * Copyright (C) 2002 Cisco Systems, Inc.
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
 * $Id: iscsi-io.h,v 1.4.4.1 2004/08/10 23:04:49 coughlan Exp $
 *
 * iscsi-io.h
 *
 *   define the PDU I/O functions needed by the login library
 * 
 */

# include "iscsi-protocol.h"
# include "iscsi-session.h"
# include "iscsi-platform.h"

extern int iscsi_connect(iscsi_session_t * session);
extern void iscsi_disconnect(iscsi_session_t * session);

/* functions used in iscsi-login.c that must be implemented for each platform */
extern int iscsi_send_pdu(iscsi_session_t * session, struct IscsiHdr *header,
			  char *data, int timeout);
extern int iscsi_recv_pdu(iscsi_session_t * session, struct IscsiHdr *header,
			  int max_header_length, char *data,
			  int max_data_length, int timeout);

#endif
