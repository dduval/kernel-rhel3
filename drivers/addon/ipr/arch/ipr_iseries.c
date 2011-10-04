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
/* iSeries architecture dependent utilties                        */
/******************************************************************/ 

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_iseries.c,v 1.3 2003/10/24 20:52:17 bjking1 Exp $
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

#ifndef ipr_iseries_h
#include "ipr_iseries.h"
#endif

#ifdef MODULE
#include <linux/module.h>

MODULE_SUPPORTED_DEVICE("IBM iSeries 2763, 2748, 2778, 2757, 2780, 2782, 5702, and 5703 storage adapters");
MODULE_DESCRIPTION ("IBM Power Linux RAID driver");
#endif

const char ipr_platform[] = "iSeries";
const int ipr_arch = IPR_ARCH_ISERIES;

/*---------------------------------------------------------------------------
 * Purpose: Get the physical location of the IOA
 * Context: Task or interrupt level
 * Lock State: no locks assumed to be held
 * Returns: Location data structure or NULL on failure
 *---------------------------------------------------------------------------*/
struct ipr_location_data *ipr_get_ioa_location_data(struct pci_dev *p_dev)
{
    struct LocationDataStruct *p_location_data = NULL;
    struct ipr_location_data *p_location = NULL;

    p_location_data = iSeries_GetLocationData(p_dev);

    if (p_location_data == NULL)
        return NULL;

    p_location = ipr_kcalloc(sizeof(struct ipr_location_data), IPR_ALLOC_CAN_SLEEP);

    if (p_location == NULL)
    {
        ipr_kfree(p_location_data, sizeof(struct LocationDataStruct));
        return NULL;
    }

    p_location->sys_bus = p_location_data->Bus;
    p_location->sys_card = p_location_data->Card;
    p_location->io_adapter = p_location_data->Board;
    p_location->io_bus = 0xF;
    p_location->ctl = 0xFF;
    p_location->dev = 0xFF;
    p_location->frame_id = p_location_data->FrameId;

    strcpy(p_location->slot, p_location_data->CardLocation);

    p_location->pci_bus_number = p_dev->bus->number;
    p_location->pci_slot = PCI_SLOT(p_dev->devfn);
    p_location->pci_function = PCI_FUNC(p_dev->devfn);

    kfree(p_location_data);

    return p_location;
}

/*---------------------------------------------------------------------------
 * Purpose: Provide Host location data
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: string containing ioa's host name.
 *---------------------------------------------------------------------------*/
void ipr_ioa_loc_str(struct ipr_location_data *p_location, char *p_buf)
{
    sprintf(p_buf, "Frame ID: %d, Card Position: %s",
            p_location->frame_id, p_location->slot);
}

/*---------------------------------------------------------------------------
 * Purpose: Provide device location data
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing.
 *---------------------------------------------------------------------------*/
void ipr_dev_loc_str(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_resource_entry *p_resource, char *p_buf)
{
    sprintf(p_buf, "Frame ID: %s, Card Position: %s",
            p_resource->frame_id, p_resource->slot_label);
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
    ipr_hcam_log("DSA/UA: %04X%02X%02X/%X%X%02X%02X%02X",
                    p_location->sys_bus,
                    p_location->sys_card,
                    p_location->io_adapter,
                    0,
                    p_location->io_bus,
                    p_location->ctl,
                    p_location->dev,
                    0xFF);
    ipr_hcam_log("Frame ID: %d", p_location->frame_id);
    ipr_hcam_log("Card Position: %s", p_location->slot);
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
    char error_buffer[10];

    ipr_hcam_log("DSA/UA: %04X%02X%02X/%X%X%02X%02X%02X",
                    p_shared_cfg->p_location->sys_bus,
                    p_shared_cfg->p_location->sys_card,
                    p_shared_cfg->p_location->io_adapter,
                    0,
                    resource_address.bus,
                    resource_address.target ^ 7,
                    resource_address.lun,
                    0xFF);

    ipr_hcam_log("Frame ID: %d", p_shared_cfg->p_location->frame_id);

    ipr_get_card_pos(p_shared_cfg, resource_address, error_buffer);

    ipr_hcam_log("Card Position: %s", error_buffer);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the physical location of a device
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_print_unknown_dev_phys_loc(char *printk_level)
{
    ipr_hcam_log("DSA/UA: unknown");
    ipr_hcam_log("Frame ID: unknown");
    ipr_hcam_log("Card Position: unknown");
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
    /* Hold reset line down for .5 second then wait for 3 seconds after active */
    return iSeries_Device_ToggleReset(ipr_cfg->pdev, 5, 30);
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
    int i, j, bus;
    struct ipr_drive_element_desc *p_drive_elem_desc;
    struct ipr_element_desc_page *p_desc_page;

    *p_buffer = '\0';

    bus = resource_addr.bus;
    if (bus > (IPR_MAX_NUM_BUSES - 1))
        return;

    p_desc_page = p_shared_cfg->p_ses_data[bus];

    /* check if pageCode is zero, if zero, presume SES does not exist or that it has not
     shown up yet. */
    if (p_desc_page->pageCode == 0)
        return;

    for (i=0; i < IPR_MAX_NUM_ELEM_DESCRIPTORS; i++)
    {
        if (sistoh32(p_desc_page->global_desc_hdr.length) == IPR_GLOBAL_DESC_12BYTES)
        {
            p_drive_elem_desc = &p_desc_page->desc.drive_elem_desc_c.drive_elem_desc[i];
        }
        else if (sistoh32(p_desc_page->global_desc_hdr.length) == IPR_GLOBAL_DESC_13BYTES)
        {
            p_drive_elem_desc = &p_desc_page->desc.drive_elem_desc_d.drive_elem_desc[i];
        }
        else
        {
            ipr_log_err("Unknown backplane. Global descriptor is %d bytes"IPR_EOL,
                           p_desc_page->global_desc_hdr.length);
            continue;
        }

        if (resource_addr.target == p_drive_elem_desc->slot_map.scsi_id)
        {
            for (j=0; j < 3; j++)
                p_buffer[j] = p_drive_elem_desc->slot_map.label[j];
            p_buffer[3] = '\0';
            return;
        }
    }
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
    int result = 0;

    if (dsa != p_shared_cfg->ioa_resource.dsa)
        result = -ENXIO;
    else
    {
        p_res_addr->reserved = 0;
        p_res_addr->bus = IPR_GET_IO_BUS(ua);
        p_res_addr->target = IPR_GET_CTL(ua) ^ 7;
        p_res_addr->lun = IPR_GET_DEV(ua);
    }

    return result;
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
    int i, bus;
    struct ipr_drive_element_desc *p_drive_elem_desc;
    struct ipr_element_desc_page *p_desc_page;

    if (p_shared_cfg->p_location->frame_id == frame)
    {
        for (bus = 0; bus < IPR_MAX_NUM_BUSES; bus++)
        {
            p_desc_page = p_shared_cfg->p_ses_data[bus];

            /* check if pageCode is zero, if zero, presume SES does not exist or that it has not
             shown up yet. */
            if (p_desc_page->pageCode == 0)
                continue;

            for (i = 0; i < IPR_MAX_NUM_ELEM_DESCRIPTORS; i++)
            {
                if (sistoh32(p_desc_page->global_desc_hdr.length) == IPR_GLOBAL_DESC_12BYTES)
                    p_drive_elem_desc = &p_desc_page->desc.drive_elem_desc_c.drive_elem_desc[i];
                else if (sistoh32(p_desc_page->global_desc_hdr.length) == IPR_GLOBAL_DESC_13BYTES)
                    p_drive_elem_desc = &p_desc_page->desc.drive_elem_desc_d.drive_elem_desc[i];
                else
                    continue;

                if ((p_drive_elem_desc->slot_map.label[0] == p_slot[0]) &&
                    (p_drive_elem_desc->slot_map.label[1] == p_slot[1]) &&
                    (p_drive_elem_desc->slot_map.label[2] == p_slot[2]))
                {
                    p_res_addr->reserved = 0;
                    p_res_addr->bus = bus;
                    p_res_addr->target = p_drive_elem_desc->slot_map.scsi_id;
                    p_res_addr->lun = 0;

                    return 0;
                }
            }
        }
    }
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
    struct ipr_location_data *p_location = p_shared_cfg->p_location;

    p_resource_entry->dsa = ((p_location->sys_bus << 16) |
                             (p_location->sys_card << 8) |
                             (p_location->io_adapter));

    sprintf(p_resource_entry->frame_id, "%d",
            p_location->frame_id);

    if (p_resource_entry->is_ioa_resource)
    {
        p_resource_entry->unit_address = 0x0FFFFFFF;
        strncpy(p_resource_entry->slot_label,
                p_location->slot, 3);
        p_location->slot[3] = '\0';
    }
    else
    {
        p_resource_entry->unit_address =
            ((p_resource_entry->resource_address.bus << 24) |
             ((p_resource_entry->resource_address.target ^ 7) << 16) |
             (p_resource_entry->resource_address.lun << 8) | 0xFF);
    }

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
    return (((p_location->sys_bus & 0xffff) << 16) |
            ((p_location->sys_card & 0xff) << 8) |
            p_location->io_adapter);
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
    return;
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
    char device_ccin_str[5];

    ipr_dasd_vpids_to_ccin_str(p_vpids, device_ccin_str, default_ccin);
    ipr_hcam_log("             Type: %s", device_ccin_str);
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
    char temp_ccin[5];

    temp_ccin[4] = '\0';

    memcpy(temp_ccin, p_vpids->product_id, 4);
    ipr_hcam_log("             Type: %s", temp_ccin);
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
    char error_buffer[10];

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

    ipr_hcam_log(" DSA/UA: ");

    if (current_res_addr.bus == 0xff)
        ipr_hcam_log("         Current: unknown");
    else
    {
        ipr_hcam_log("         Current: %04X%02X%02X/%X%X%02X%02X%02X",
                        ipr_cfg->shared.p_location->sys_bus,
                        ipr_cfg->shared.p_location->sys_card,
                        ipr_cfg->shared.p_location->io_adapter,
                        0,
                        current_res_addr.bus,
                        current_res_addr.target ^ 7,
                        current_res_addr.lun,
                        0xFF);
    }

    if (expected_res_addr.bus == 0xff)
        ipr_hcam_log("        Expected: unknown");
    else
    {
        ipr_hcam_log("        Expected: %04X%02X%02X/%X%X%02X%02X%02X",
                        ipr_cfg->shared.p_location->sys_bus,
                        ipr_cfg->shared.p_location->sys_card,
                        ipr_cfg->shared.p_location->io_adapter,
                        0,
                        expected_res_addr.bus,
                        expected_res_addr.target ^ 7,
                        expected_res_addr.lun,
                        0xFF);
    }

    ipr_hcam_log(" Frame ID/Card Position: ");

    if (current_res_addr.bus == 0xff)
        ipr_hcam_log("         Current: unknown");
    else
    {
        ipr_get_card_pos(&ipr_cfg->shared,
                            current_res_addr, error_buffer);
        ipr_hcam_log("         Current: %d/%s",
                        ipr_cfg->shared.p_location->frame_id,
                        error_buffer);
    }

    if (expected_res_addr.bus == 0xff)
        ipr_hcam_log("        Expected: unknown");
    else
    {
        ipr_get_card_pos(&ipr_cfg->shared,
                            expected_res_addr,
                            error_buffer);
        ipr_hcam_log("        Expected: %d/%s",
                        ipr_cfg->shared.p_location->frame_id,
                        error_buffer);
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
