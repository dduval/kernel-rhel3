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
 *
 * $Id: iscsi-version.h,v 1.3.4.2 2004/09/22 15:03:38 coughlan Exp $
 *
 *      controls the version number printed by the iSCSI driver
 *
 */

#define DRIVER_MAJOR_VERSION 3
#define DRIVER_MINOR_VERSION 6
#define DRIVER_PATCH_VERSION 2
#define DRIVER_INTERNAL_VERSION 1

/* DRIVER_EXTRAVERSION is intended to be customized by Linux
 * distributors, similar to the kernel Makefile's EXTRAVERSION.  This
 * string will be appended to all version numbers displayed by the
 * driver.  RPMs that patch the driver are encouraged to also patch
 * this string to indicate to users that the driver has been patched,
 * and may behave differently than a driver tarball from SourceForge.
 */

#define DRIVER_EXTRAVERSION ""

#define ISCSI_DATE	"22-Sep-2004"

/* Distributors may also set BUILD_STR to a string, which will be
 * logged by the kernel module after it loads and displays the version
 * number.  It is currently used as part of the driver development
 * process, to mark tarballs built by developers containing code 
 * not yet checked into CVS.  Publically available tarballs on
 * SourceForge should always have BUILD_STR set to NULL, since
 * all code should be checked in prior to making a public release.
 */

#define BUILD_STR	NULL
