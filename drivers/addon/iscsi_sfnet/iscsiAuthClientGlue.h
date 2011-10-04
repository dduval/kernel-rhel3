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
 * $Id: iscsiAuthClientGlue.h,v 1.6.2.1 2004/08/10 23:04:45 coughlan Exp $ 
 *
 */

#ifndef ISCSIAUTHCLIENTGLUE_H
#define ISCSIAUTHCLIENTGLUE_H

#include "iscsi-platform.h"
#include "md5.h"

typedef struct MD5Context IscsiAuthMd5Context;

#ifdef __cplusplus
extern "C" {
#endif

    extern int iscsiAuthIscsiServerHandle;
    extern int iscsiAuthIscsiClientHandle;

#ifdef __cplusplus
}
#endif
#endif				/* #ifndef ISCSIAUTHCLIENTGLUE_H */
