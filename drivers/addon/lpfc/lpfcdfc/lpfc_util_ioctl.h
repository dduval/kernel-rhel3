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
 * $Id: lpfc_util_ioctl.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef  _H_LPFC_UTIL_IOCTL
#define _H_LPFC_UTIL_IOCTL

int lpfc_process_ioctl_util(lpfcHBA_t *phba, LPFCCMDINPUT_t *cip);
int lpfc_ioctl_write_pci(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_read_pci(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_write_mem(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_read_mem(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_write_ctlreg(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_read_ctlreg(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_setdiag(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_send_scsi_fcp(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_send_mgmt_rsp(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_send_mgmt_cmd(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_mbox(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_linkinfo(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_ioinfo(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_nodeinfo(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int);
int lpfc_ioctl_getcfg(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_setcfg(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_hba_get_event(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int);
int lpfc_ioctl_hba_set_event(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_add_bind(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_del_bind(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_list_bind(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int *);
int lpfc_ioctl_get_vpd(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int *);
int dfc_rsp_data_copy(lpfcHBA_t *, uint8_t *, DMABUFEXT_t *, uint32_t);
DMABUFEXT_t *dfc_cmd_data_alloc(lpfcHBA_t *, char *, ULP_BDE64 *, uint32_t);
int dfc_cmd_data_free(lpfcHBA_t *, DMABUFEXT_t *);

#endif
