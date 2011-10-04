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
 * $Id: lpfc_debug_ioctl.h 328 2005-05-03 15:20:43Z sf_support $
 */

#ifndef H_LPFC_DFC_IOCTL
#define H_LPFC_DFC_IOCTL
int lpfc_process_ioctl_dfc(lpfcHBA_t * phba, LPFCCMDINPUT_t * cip);
int lpfc_ioctl_lip(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_outfcpio(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_send_els(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_inst(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_listn(lpfcHBA_t *, LPFCCMDINPUT_t *,  void *, int);
int lpfc_ioctl_read_bplist(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int);
int lpfc_ioctl_reset(lpfcHBA_t *, LPFCCMDINPUT_t *);
int lpfc_ioctl_read_hba(lpfcHBA_t *, LPFCCMDINPUT_t *, void *, int);
int lpfc_ioctl_stat(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_ioctl_devp(lpfcHBA_t *, LPFCCMDINPUT_t *, void *);
int lpfc_reset_dev_q_depth(lpfcHBA_t *);
int lpfc_fcp_abort(lpfcHBA_t *, int, int, int);
#endif
