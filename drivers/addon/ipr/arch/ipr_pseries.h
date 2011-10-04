/*****************************************************************************/
/* ipr_pseries.h -- driver for IBM Power Linux RAID adapters                 */
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

/******************************************************************/ 
/* pSeries architecture dependent header file                     */
/******************************************************************/ 

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_pseries.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#ifndef ipr_pseries_h
#define ipr_pseries_h

/******************************************************************/
/* Literals                                                       */
/******************************************************************/

#ifndef NO_TCE
#define NO_TCE ((dma_addr_t)-1)
#endif

#define IPR_CL_SIZE_LATENCY_MASK 0x000000FF  /* only modify Cache line size */

/******************************************************************/
/* Structures                                                     */
/******************************************************************/

struct ipr_location_data
{
    char location[IPR_MAX_PSERIES_LOCATION_LEN];
    char of_location[IPR_MAX_PSERIES_LOCATION_LEN];
    unsigned int pci_bus_number;
    unsigned int pci_slot;
    unsigned int pci_function;
};

/******************************************************************/
/* Function Prototypes                                            */
/******************************************************************/

extern int register_ioctl32_conversion(unsigned int cmd,
                                       int (*handler)(unsigned int,
                                                      unsigned int,
                                                      unsigned long,
                                                      struct file *));
extern int unregister_ioctl32_conversion(unsigned int cmd);

#endif
