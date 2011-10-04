/*****************************************************************************/
/* ipr_iseries.c -- driver for IBM Power Linux RAID adapters                 */
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
/* iSeries architecture dependent header file                     */
/******************************************************************/ 

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_iseries.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#ifndef ipr_iseries_h
#define ipr_iseries_h

/******************************************************************/
/* Includes                                                       */
/******************************************************************/
#ifndef CONFIG_PPC64
#include <asm/iSeries/iSeries_VpdInfo.h>
#endif

#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/iSeries_dma.h>

/******************************************************************/
/* Function Prototypes                                            */
/******************************************************************/
#ifdef CONFIG_PPC64
extern int register_ioctl32_conversion(unsigned int cmd,
                                       int (*handler)(unsigned int,
                                                      unsigned int,
                                                      unsigned long,
                                                      struct file *));
extern int unregister_ioctl32_conversion(unsigned int cmd);
#else
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
#endif


/******************************************************************/
/* Literals                                                       */
/******************************************************************/
#define IPR_CL_SIZE_LATENCY_MASK 0xFFFFFFFF


/******************************************************************/
/* Structures                                                     */
/******************************************************************/

struct ipr_location_data
{
    u32 frame_id;
    u8 slot[4];
    u16 sys_bus;
    u8 sys_card;
    u8 io_adapter;
    u8 ioa_num:4; /* Always 0 */
    u8 io_bus:4;
    u8 ctl;
    u8 dev;
    u8 reserved;
    unsigned int pci_bus_number;
    unsigned int pci_slot;
    unsigned int pci_function;
};

#endif
