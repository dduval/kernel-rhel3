/*****************************************************************************/
/* ipr_generic.c -- driver for IBM Power Linux RAID adapters                 */
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
/* Generic architecture dependent utilties                        */
/******************************************************************/ 

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_generic.c,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/blk.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <scsi.h>
#include <hosts.h>

#ifndef iprdd_h
#include "iprdd.h"
#endif

#ifndef ipr_generic_h
#include "ipr_generic.h"
#endif

#ifdef MODULE
#include <linux/module.h>

MODULE_SUPPORTED_DEVICE("IBM storage adapters");
MODULE_DESCRIPTION ("IBM SCSI device driver");
#endif

const char ipr_platform[] = "generic";
const int ipr_arch = IPR_ARCH_GENERIC;

/*---------------------------------------------------------------------------
 * Purpose: Get the physical location of the IOA
 * Context: Task or interrupt level
 * Lock State: no locks assumed to be held
 * Returns: Location data structure or NULL on failure
 *---------------------------------------------------------------------------*/
struct ipr_location_data *ipr_get_ioa_location_data(struct pci_dev *p_dev)
{
    struct ipr_location_data *p_location = NULL;

    p_location = ipr_kcalloc(sizeof(struct ipr_location_data), IPR_ALLOC_CAN_SLEEP);

    if (p_location == NULL)
        return NULL;

    p_location->pci_bus_number = p_dev->bus->number;
    p_location->pci_slot = PCI_SLOT(p_dev->devfn);
    p_location->pci_function = PCI_FUNC(p_dev->devfn);

    return p_location;
}

/*---------------------------------------------------------------------------
 * Purpose: Provide Host location data
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing.
 *---------------------------------------------------------------------------*/
void ipr_ioa_loc_str(struct ipr_location_data *p_location, char *p_buf)
{
    sprintf(p_buf, "PCI Bus: %d device: %d",
            p_location->pci_bus_number,
            p_location->pci_slot);
}

/*---------------------------------------------------------------------------
 * Purpose: Provide device location data
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed
 * Returns: Nothing.
 *---------------------------------------------------------------------------*/
void ipr_dev_loc_str(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_resource_entry *p_resource, char *p_buf)
{
    sprintf(p_buf, "PCI: %d:%d, Channel: %2d Id: %2d Lun: %2d",
            p_shared_cfg->p_location->pci_bus_number,
            p_shared_cfg->p_location->pci_slot,
            p_resource->resource_address.bus,
            p_resource->resource_address.target,
            p_resource->resource_address.lun);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the physical location of the IOA
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_log_ioa_physical_location(struct ipr_location_data *p_location,
                                      char *printk_level)
{
    ipr_hcam_log("PCI Bus: %d device: %d",
                    p_location->pci_bus_number,
                    p_location->pci_slot);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the physical location of a device
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_log_dev_physical_location(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_res_addr resource_address,
                                      char *printk_level)
{
    ipr_hcam_log("IOA Location: PCI Bus: %d device: %d",
                    p_shared_cfg->p_location->pci_bus_number,
                    p_shared_cfg->p_location->pci_slot);

    ipr_hcam_log("Device Location: Channel: %2d Id: %2d Lun: %2d",
                    resource_address.bus,
                    resource_address.target,
                    resource_address.lun);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the physical location of a device
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_print_unknown_dev_phys_loc(char *printk_level)
{
    ipr_hcam_log("Device Location: unknown");
}

/*---------------------------------------------------------------------------
 * Purpose: Toggle reset on the IOA
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: 0           - Success
 *          non-zero    - failure
 *---------------------------------------------------------------------------*/
int ipr_toggle_reset(ipr_host_config *ipr_cfg)
{
    int rc;

    /* Start BIST and wait 2 seconds for completion */
    rc = pci_write_config_dword(ipr_cfg->pdev, 0x0C, 0x40000000);
    set_current_state(TASK_UNINTERRUPTIBLE);  
    schedule_timeout(2*HZ);
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Return slot position of a device
 * Context: Task level or interrupt level.
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_get_card_pos(struct ipr_shared_config *p_shared_cfg,
                         struct ipr_res_addr resource_addr, char *p_buffer)
{
    *p_buffer = '\0';
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Return resource address of a DSA/UA
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: -ENXIO      - Physical location does not exist under this IOA
 *          0           - Success
 *---------------------------------------------------------------------------*/
int ipr_get_res_addr_fmt0(struct ipr_shared_config *p_shared_cfg,
                             u32 dsa, u32 ua,
                             struct ipr_res_addr *p_res_addr)
{
    return -ENXIO;
}

/*---------------------------------------------------------------------------
 * Purpose: Return resource address of a Frame ID/Card Position
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: -ENXIO      - Physical location does not exist under this IOA
 *          0           - Success
 *---------------------------------------------------------------------------*/
int ipr_get_res_addr_fmt1(struct ipr_shared_config *p_shared_cfg,
                             u32 frame, char *p_slot,
                             struct ipr_res_addr *p_res_addr)
{
    return -ENXIO;
}

/*---------------------------------------------------------------------------
 * Purpose: Return resource address of a pSeries location
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: -ENXIO      - Physical location does not exist under this IOA
 *          0           - Success
 *---------------------------------------------------------------------------*/
int ipr_get_res_addr_fmt2(struct ipr_shared_config *p_shared_cfg,
                             char *p_location, struct ipr_res_addr *p_res_addr)
{
    return -ENXIO;
}

/*---------------------------------------------------------------------------
 * Purpose: Return resource address of a device location
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: -ENXIO      - Physical location does not exist under this IOA
 *          0           - Success
 *---------------------------------------------------------------------------*/
int ipr_get_res_addr_fmt3(struct ipr_shared_config *p_shared_cfg,
                             u16 pci_bus, u16 pci_device, u8 bus, u8 target, u8 lun,
                             struct ipr_res_addr *p_res_addr)
{
    if ((pci_bus != p_shared_cfg->p_location->pci_bus_number) ||
        (pci_device != p_shared_cfg->p_location->pci_slot))
    {
        return -ENXIO;
    }

    p_res_addr->reserved = 0;
    p_res_addr->bus = bus;
    p_res_addr->target = target;
    p_res_addr->lun = lun;

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Update the location information for a resource
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_update_location_data(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_resource_entry *p_resource_entry)
{
    p_resource_entry->dsa = 0;
    p_resource_entry->frame_id[0] = '\0';
    p_resource_entry->unit_address = 0;
    p_resource_entry->slot_label[0] = '\0';
    p_resource_entry->pseries_location[0] = '\0';
    p_resource_entry->pci_bus_number = p_shared_cfg->p_location->pci_bus_number;
    p_resource_entry->pci_slot = p_shared_cfg->p_location->pci_slot;

    if ((IPR_IS_DASD_DEVICE(p_resource_entry->std_inq_data)) &&
        (!p_resource_entry->is_ioa_resource))
    {
        ipr_get_card_pos(p_shared_cfg, p_resource_entry->resource_address,
                            p_resource_entry->slot_label);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Get a unique host identifier
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: unique id
 * Note: This is used by scsidev to generate device names so we want to
 *       generate something that doesn't change.
 *---------------------------------------------------------------------------*/
u32 ipr_get_unique_id(struct ipr_location_data *p_location)
{
    return (((p_location->pci_bus_number & 0xffff) << 16) |
            ((p_location->pci_slot & 0xff) << 8) |
            p_location->pci_function);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the VPD for a device
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_log_dev_vpd(struct ipr_resource_entry *p_resource,
                        char *printk_level)
{
    u8 vendor_id[IPR_VENDOR_ID_LEN+1];
    u8 product_id[IPR_PROD_ID_LEN+1];

    memcpy(vendor_id, p_resource->std_inq_data.vpids.vendor_id,
           IPR_VENDOR_ID_LEN);
    vendor_id[IPR_VENDOR_ID_LEN] = '\0';
    memcpy(product_id, p_resource->std_inq_data.vpids.product_id,
           IPR_PROD_ID_LEN);
    product_id[IPR_PROD_ID_LEN] = '\0';

    ipr_hcam_log("Device Vendor ID: %s", vendor_id);
    ipr_hcam_log("Device Product ID: %s", product_id);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the VPD for an array member
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_log_array_dev_vpd(struct ipr_std_inq_vpids *p_vpids,
                              char *default_ccin,
                              char *printk_level)
{
    u8 vendor_id[IPR_VENDOR_ID_LEN+1];
    u8 product_id[IPR_PROD_ID_LEN+1];

    memcpy(vendor_id, p_vpids->vendor_id, IPR_VENDOR_ID_LEN);
    vendor_id[IPR_VENDOR_ID_LEN] = '\0';
    memcpy(product_id, p_vpids->product_id, IPR_PROD_ID_LEN);
    product_id[IPR_PROD_ID_LEN] = '\0';

    ipr_hcam_log("        Vendor ID: %s", vendor_id);
    ipr_hcam_log("       Product ID: %s", product_id);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the VPD for an IOA
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_print_ioa_vpd(struct ipr_std_inq_vpids *p_vpids,
                          char *printk_level)
{
    u8 vendor_id[IPR_VENDOR_ID_LEN+1];
    u8 product_id[IPR_PROD_ID_LEN+1];

    memcpy(vendor_id, p_vpids->vendor_id, IPR_VENDOR_ID_LEN);
    vendor_id[IPR_VENDOR_ID_LEN] = '\0';
    memcpy(product_id, p_vpids->product_id, IPR_PROD_ID_LEN);
    product_id[IPR_PROD_ID_LEN] = '\0';

    ipr_hcam_log("        Vendor ID: %s", vendor_id);
    ipr_hcam_log("       Product ID: %s", product_id);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the current and expected locations for a device
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_log_dev_current_expected_locations(ipr_host_config *ipr_cfg,
                                               struct ipr_res_addr current_res_addr,
                                               struct ipr_res_addr expected_res_addr,
                                               char *printk_level)
{
    ipr_hcam_log(" PCI/SCSI Address: ");

    if (current_res_addr.bus == 0xff)
        ipr_hcam_log("         Current: unknown");
    else
    {
        ipr_hcam_log("         Current: %02X:%02X/%02X%02X%02X",
                        ipr_cfg->shared.p_location->pci_bus_number,
                        ipr_cfg->shared.p_location->pci_slot,
                        current_res_addr.bus,
                        current_res_addr.target,
                        current_res_addr.lun);
    }

    if (expected_res_addr.bus == 0xff)
        ipr_hcam_log("        Expected: unknown");
    else
    {
        ipr_hcam_log("        Expected: %02X:%02X/%02X%02X%02X",
                        ipr_cfg->shared.p_location->pci_bus_number,
                        ipr_cfg->shared.p_location->pci_slot,
                        expected_res_addr.bus,
                        expected_res_addr.target,
                        expected_res_addr.lun);
    }
};

/*---------------------------------------------------------------------------
 * Purpose: Determine if this adapter is supported on this arch
 * Context: Task level
 * Lock State: io_request_lock assumed to be held
 * Returns: 0 if adapter is supported
 *---------------------------------------------------------------------------*/
int ipr_invalid_adapter(ipr_host_config *ipr_cfg)
{
    return 0;
}
