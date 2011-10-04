#ifndef ISCSI_PROBE_H_
#define ISCSI_PROBE_H_

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
 * $Id: iscsi-probe.h,v 1.5.2.1 2004/08/10 23:04:48 coughlan Exp $
 *
 * iscsi-probe.h
 *
 *    include for iSCSI kernel module LUN probing
 * 
 */

/* various ioctls need these */
extern void iscsi_detect_luns(iscsi_session_t * session);
extern void iscsi_probe_luns(iscsi_session_t * session, uint32_t * lun_bitmap);
extern void iscsi_remove_luns(iscsi_session_t * session);
extern void iscsi_remove_lun(iscsi_session_t * session, int lun);
extern int iscsi_reset_lun_probing(void);

/* we check the done function on commands to distinguish
 * commands created by the driver itself 
 */
extern void iscsi_done(Scsi_Cmnd * sc);

/* timer needs these to know when to start lun probing */
extern void iscsi_possibly_start_lun_probing(void);
extern volatile unsigned long iscsi_lun_probe_start;

#endif
