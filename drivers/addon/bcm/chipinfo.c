
/*
 * Broadcom Cryptonet Driver software is distributed as is, without any warranty
 * of any kind, either express or implied as further specified in the GNU Public
 * License. This software may be used and distributed according to the terms of
 * the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */

/******************************************************************************
 *
 * Copyright 2001
 * Broadcom Corporation
 * 16215 Alton Parkway
 * PO Box 57013
 * Irvine CA 92619-7013
 *
 *****************************************************************************/

/*******************************************************************************
 *
 * File: Linux/chipinfo.c
 * 
 * What: obsolete_chipinfo returns the maximum key size for the installed h/w. 
 *
 * Description: 
 *              
 * Revision History:
 *                   When       Who   What
 *                   09/07/01   RJT   Renamed from ubsec_chipinfo. This routine
 *                                    will be phased out in favor of the new
 *                                    ubsec_chipinfo function in the SRL
 *                   06/28/01   DNA   Removed SRL call, uses Linux #define now.
 *                   03/13/01   DNA   Created.
 *
 ******************************************************************************/

#include "cdevincl.h"

/**************************************************************************
 *
 * Function:     obsolete_chipinfo
 * 
 * Called from:  ubsec_ioctl() in Linux/dispatch.c
 *
 * Description: 
 *
 * Return Values: 
 *                == 0   =>   Success
 *                != 0   =>   Failure
 *
 *************************************************************************/

int
obsolete_chipinfo(
	       ubsec_DeviceContext_t   pContext,
	       linux_chipinfo_io_t    *pIOInfo
	       ) {
  
  linux_chipinfo_io_t   IOInfo;
  
  copy_from_user(&IOInfo, pIOInfo, sizeof(linux_chipinfo_io_t));
  
#ifdef UBSEC_5820
  IOInfo.max_key_len = 2048;
#else
  IOInfo.max_key_len = 1024;
#endif
  
  copy_to_user(pIOInfo, &IOInfo, sizeof(linux_chipinfo_io_t));
  
  IOInfo.result_status = UBSEC_STATUS_SUCCESS;
  
  return(IOInfo.result_status);
}

