/*****************************************************************************/
/* ipr_generic.h -- driver for IBM Power Linux RAID adapters                 */
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
/* Generic architecture dependent header file                     */
/******************************************************************/ 

#ifndef ipr_generic_h
#define ipr_generic_h

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_generic.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */


/******************************************************************/
/* Literals                                                       */
/******************************************************************/
#define NO_TCE ((dma_addr_t)-1)
#define IPR_CL_SIZE_LATENCY_MASK 0x000000FF  /* only modify Cache line size */

/******************************************************************/
/* Function Prototypes                                            */
/******************************************************************/
static IPR_INL int register_ioctl32_conversion(unsigned int cmd,
                                                  int (*handler)(unsigned int,
                                                                 unsigned int,
                                                                 unsigned long,
                                                                 struct file *))
{
    return 0;
}
static IPR_INL int unregister_ioctl32_conversion(unsigned int cmd)
{
    return 0;
}


/******************************************************************/
/* Structures                                                     */
/******************************************************************/

struct ipr_location_data
{
    unsigned int pci_bus_number;
    unsigned int pci_slot;
    unsigned int pci_function;
};

#endif
