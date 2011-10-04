
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
 * Copyright 2000
 * Broadcom Corporation
 * 16215 Alton Parkway
 * PO Box 57013
 * Irvine CA 92619-7013
 *
 *****************************************************************************/

/* 
 * Broadcom Corporation uBSec SDK 
 */

/*
 * ubsinit.c:  ubsec initialization and cleanup modules.
 */

/*
 * Revision History:
 *
 * 09/xx/99 SOR Created.
 * 07/26/00 SOR Added Number of Key MCRs
 */

#include "ubsincl.h"

/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 * ubsstats.c:  ubsec Statistics information.
 */
/*
 * Revision History:
 *
 * 08/09/00 SOR Created
 */

#include "ubsincl.h"


/*
 * Ubsec get statistical information function.
 */
UBSECAPI ubsec_Status_t 
ubsec_GetStatistics(ubsec_DeviceContext_t Context,
	      ubsec_Statistics_pt Dest)
{

#ifdef UBSEC_STATS
DeviceInfo_pt pDevice;

pDevice = (DeviceInfo_pt)Context;
RTL_Memcpy(Dest,&pDevice->Statistics,sizeof(ubsec_Statistics_t));
return(UBSEC_STATUS_SUCCESS);
#else
return(UBSEC_STATUS_INVALID_PARAMETER);
#endif
}


