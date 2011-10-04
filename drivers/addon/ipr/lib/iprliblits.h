/*****************************************************************************/
/* iprliblits.h -- driver for IBM Power Linux RAID adapters                  */
/*                                                                           */
/* Written By: Brian King, IBM Corporation                                   */
/*                                                                           */
/* Copyright (C) 2003 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/*****************************************************************************/

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/lib/iprliblits.h,v 1.2.2.1 2003/10/29 14:04:26 bjking1 Exp $
 */

#ifndef iprliblits_h
#define iprliblits_h

#ifndef iprlits_h
#include "iprlits.h"
#endif

#if IPR_DBG_TRACE
#define ipr_trace printk(KERN_ERR IPR_NAME": %s: %s: Line: %d"\
IPR_EOL, __FILE__, __FUNCTION__, __LINE__);
#else
#define ipr_trace
#endif

/******************************************************************/
/* Literals                                                       */
/******************************************************************/

#define IPR_DOORBELL                         0x82800000

#define IPR_PCII_IOA_TRANS_TO_OPER           (0x80000000 >> 0)
#define IPR_PCII_IOARCB_XFER_FAILED          (0x80000000 >> 3)
#define IPR_PCII_IOA_UNIT_CHECKED            (0x80000000 >> 4)
#define IPR_PCII_NO_HOST_RRQ                 (0x80000000 >> 5)
#define IPR_PCII_CRITICAL_OPERATION          (0x80000000 >> 6)
#define IPR_PCII_IO_DEBUG_ACKNOWLEDGE        (0x80000000 >> 7)
#define IPR_PCII_IOARRIN_LOST                (0x80000000 >> 27)
#define IPR_PCII_MMIO_ERROR                  (0x80000000 >> 28)
#define IPR_PCII_PROC_ERR_STATE              (0x80000000 >> 29)
#define IPR_PCII_HOST_RRQ_UPDATED            (0x80000000 >> 30)
#define IPR_PCII_CORE_ISSUED_RST_REQ         (0x80000000 >> 31)

#define IPR_PCII_ERROR_INTERRUPTS            (IPR_PCII_IOARCB_XFER_FAILED | \
                                                 IPR_PCII_IOA_UNIT_CHECKED | \
                                                 IPR_PCII_NO_HOST_RRQ | \
                                                 IPR_PCII_IOARRIN_LOST | \
                                                 IPR_PCII_MMIO_ERROR)

#define IPR_PCII_OPER_INTERRUPTS             (IPR_PCII_ERROR_INTERRUPTS | \
                                                 IPR_PCII_HOST_RRQ_UPDATED)

#define IPR_403I_RESET_ALERT                 (0x80000000 >> 7)
#define IPR_UPROCI_RESET_ALERT               (0x80000000 >> 7)
#define IPR_UPROCI_IO_DEBUG_ALERT            (0x80000000 >> 9)

#define IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC       1000000   /* 1 second */
#define IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC       500000   /* 500 ms */

#define IPR_SET_SUP_DEVICE_TIMEOUT           120             /* 120 seconds */
#define IPR_SET_DASD_TIMEOUTS_TIMEOUT        120             /* 120 seconds */
#define IPR_REQUEST_SENSE_TIMEOUT            30              /* 30 seconds  */
#define IPR_OPERATIONAL_TIMEOUT              (12 * 60)       /* 12 minutes  */

#define IPR_MAX_OP_SIZE                      (256 * 1024)
#define IPR_NUM_IOADL_ENTRIES                IPR_MAX_SGLIST

/**************************************************
 *   DASD initialization job steps
 **************************************************/
#define IPR_DINIT_START                              1
#define IPR_DINIT_STD_INQUIRY                        2
#define IPR_DINIT_QUERY_RESOURCE_STATE               3
#define IPR_DINIT_SET_SUPPORTED_DEVICE               4
#define IPR_DINIT_DASD_INIT_SET_DASD_TIMEOUTS        5
#define IPR_DINIT_PAGE3_INQ                          6
#define IPR_DINIT_MODE_SENSE_CUR                     7
#define IPR_DINIT_MODE_SENSE_CHANGEABLE              8
#define IPR_DINIT_MODE_SELECT                        9

#define IPR_VINIT_START                              1
#define IPR_VINIT_QUERY_RESOURCE_STATE               2

#define IPR_RETURN_FROM_JOB                          0
#define IPR_CONTINUE_WITH_JOB                        1

/**************************************************
 *   SES initialization job steps
 **************************************************/
#define IPR_SINIT_START              1
#define IPR_SINIT_MODE_SENSE         2
#define IPR_SINIT_MODE_SELECT        3

/**************************************************
 *   SIS Commands
 **************************************************/
#define IPR_ID_HOST_RR_Q             0xC4u
#define IPR_SKIP_READ                0xE8u
#define IPR_SKIP_WRITE               0xEAu
#define IPR_SET_DASD_TIMEOUTS        0xECu
#define IPR_SET_SUPPORTED_DEVICES    0xFBu

#endif
