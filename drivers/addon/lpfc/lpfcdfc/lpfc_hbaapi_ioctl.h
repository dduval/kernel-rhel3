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
 * $Id: lpfc_hbaapi_ioctl.h 484 2006-03-27 16:26:51Z sf_support $
 */

#include "hbaapi.h"
#ifndef H_LPFC_HBAAPI_IOCTL
#define H_LPFC_HBAAPI_IOCTL
int lpfc_process_ioctl_hbaapi(lpfcHBA_t *phba, LPFCCMDINPUT_t *cip);
int lpfc_ioctl_hba_adapterattributes(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_portattributes(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_portstatistics(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_wwpnportattributes(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_discportattributes(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_indexportattributes(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_setmgmtinfo(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_hba_getmgmtinfo(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_refreshinfo(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_rnid(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_getevent(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_hba_fcptargetmapping(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int, int *);
int lpfc_ioctl_hba_fcpbinding(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int, int *);
int lpfc_ioctl_port_attrib(lpfcHBA_t *, void *);
int lpfc_ioctl_found_port(lpfcHBA_t *, LPFC_NODELIST_t *, void *, MAILBOX_t *, HBA_PORTATTRIBUTES *);
void lpfc_decode_firmware_rev(lpfcHBA_t *, char *, int);
void lpfc_get_hba_sym_node_name(lpfcHBA_t *, uint8_t *);
#endif
