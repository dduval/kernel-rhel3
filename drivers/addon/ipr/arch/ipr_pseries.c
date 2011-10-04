/*****************************************************************************/
/* ipr_pseries.c -- driver for IBM Power Linux RAID adapters                 */
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
/* pSeries architecture dependent utilties                        */
/******************************************************************/ 

/*
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/arch/ipr_pseries.c,v 1.3 2003/10/24 20:52:17 bjking1 Exp $
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
#include <asm/processor.h>
#include <scsi.h>
#include <hosts.h>

#ifndef iprdd_h
#include "iprdd.h"
#endif

#ifndef ipr_pseries_h
#include "ipr_pseries.h"
#endif

#ifdef MODULE
#include <linux/module.h>

MODULE_SUPPORTED_DEVICE("IBM pSeries storage adapters");
MODULE_DESCRIPTION ("IBM Power Linux RAID driver");
#endif

const char ipr_platform[] = "pSeries";
const int ipr_arch = IPR_ARCH_PSERIES;

/*---------------------------------------------------------------------------
 * Purpose: Get the bus number for use in the pSeries location code
 * Context: Task level or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: bus number
 *---------------------------------------------------------------------------*/
static IPR_INL u8 ipr_get_loc_bus(struct ipr_shared_config *p_shared_cfg,
                                        struct ipr_res_addr resource_address)
{
    u8 bus = resource_address.bus;

    if (bus == 0xff)
        bus = p_shared_cfg->num_physical_buses + 1;
    else
        bus++;
    return bus;
}

/*---------------------------------------------------------------------------
 * Purpose: Get the physical location of the IOA
 * Context: Task or interrupt level
 * Lock State: no locks assumed to be held
 * Returns: Location data structure or NULL on failure
 *---------------------------------------------------------------------------*/
struct ipr_location_data *ipr_get_ioa_location_data(struct pci_dev *p_dev)
{
    char *p_buf;
    struct device_node* p_dev_node;
    int len;
    struct ipr_location_data *p_location = NULL;

    p_dev_node = (struct device_node*)p_dev->sysdata;

    p_buf = (char*)get_property(p_dev_node,"ibm,loc-code",&len);

    len = IPR_MIN(len, strlen(p_buf));

    if (p_buf && (len > 3) && ((len+1) < IPR_MAX_PSERIES_LOCATION_LEN))
    {
        p_location = ipr_kcalloc(sizeof(struct ipr_location_data),
                                    IPR_ALLOC_CAN_SLEEP);

        if (p_location != NULL)
        {
            strncpy(p_location->of_location, p_buf, len);
            strncpy(p_location->location, p_buf, len);

            strcpy(&p_location->of_location[len], "/Z");
            p_location->location[len] = '\0';

            p_location->pci_bus_number = p_dev->bus->number;
            p_location->pci_slot = PCI_SLOT(p_dev->devfn);
            p_location->pci_function = PCI_FUNC(p_dev->devfn);
        }
    }

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
    sprintf(p_buf, "PCI Bus: %d, Device: %3d Location %s",
            p_location->pci_bus_number,
            p_location->pci_slot,
            p_location->location);
}

/*---------------------------------------------------------------------------
 * Purpose: Provide device location data
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing.
 *--------------------------------------------------------------------------*/
void ipr_dev_loc_str(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_resource_entry *p_resource, char *p_buf)
{
    if (p_resource->resource_address.lun == 0)
    {
        sprintf(p_buf, "Device Location: %s%d-A%x",
                p_shared_cfg->p_location->of_location,
                ipr_get_loc_bus(p_shared_cfg, p_resource->resource_address),
                p_resource->resource_address.target);
    }
    else
    {
        sprintf(p_buf, "Device Location: %s%d-A%x.%d",
                p_shared_cfg->p_location->of_location,
                ipr_get_loc_bus(p_shared_cfg, p_resource->resource_address),
                p_resource->resource_address.target,
                p_resource->resource_address.lun);
    }
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
    ipr_hcam_log("PCI Bus: %d, Device: %3d Location %s",
                    p_location->pci_bus_number,
                    p_location->pci_slot,
                    p_location->location);
}

/*---------------------------------------------------------------------------
 * Purpose: Print the physical location of the IOA
 * Context: Task or interrupt level
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_log_dev_physical_location(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_res_addr resource_address,
                                      char *printk_level)
{
    struct ipr_location_data *p_location;

    p_location = p_shared_cfg->p_location;

    ipr_hcam_log("IOA Location: PCI Bus: %d, Device: %3d Location %s",
                    p_shared_cfg->p_location->pci_bus_number,
                    p_shared_cfg->p_location->pci_slot,
                    p_shared_cfg->p_location->location);

    if (resource_address.lun == 0)
    {
        ipr_hcam_log("Device Location: %s%d-A%x",
                        p_shared_cfg->p_location->of_location,
                        ipr_get_loc_bus(p_shared_cfg, resource_address),
                        resource_address.target);
    }
    else
    {
        ipr_hcam_log("Device Location: %s%d-A%x.%d",
                        p_shared_cfg->p_location->of_location,
                        ipr_get_loc_bus(p_shared_cfg, resource_address),
                        resource_address.target, resource_address.lun);
    }
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
    char *p_char;
    char string[3];
    int ioa_loc_len = strlen(p_shared_cfg->p_location->location);

    string[1] = '\0';

    if (!strncmp(p_location, p_shared_cfg->p_location->location,
                 strlen(p_shared_cfg->p_location->location)))
    {
        /* Look for bus */
        p_char = strchr(&p_location[ioa_loc_len], 'Z');

        if (!p_char)
            p_char = strchr(&p_location[ioa_loc_len], 'Q');

        if (!p_char)
            return -ENXIO;

        string[0] = p_char[1];
        p_res_addr->bus = simple_strtoul(string, NULL, 10) - 1;

        /* Look for target */
        p_char = strchr(&p_location[ioa_loc_len], 'A');

        if (!p_char)
            return -ENXIO;

        string[0] = p_char[1];
        p_res_addr->target = simple_strtoul(string, NULL, 16);

        /* Look for lun */
        p_char = strchr(&p_location[ioa_loc_len], '.');

        if (!p_char)
        {
            p_res_addr->lun = 0;
            return 0;
        }

        string[0] = p_char[1];
        p_res_addr->lun = simple_strtoul(string, NULL, 10);
    }
    else
        return -ENXIO;

    return 0;
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

    p_resource_entry->dsa = 0;
    p_resource_entry->frame_id[0] = '\0';
    p_resource_entry->unit_address = 0;
    p_resource_entry->slot_label[0] = '\0';

    if (p_resource_entry->is_ioa_resource)
    {
        sprintf(p_resource_entry->pseries_location, "%s",
                p_location->location);
    }
    else
    {
        if (p_resource_entry->resource_address.lun == 0)
        {
            sprintf(p_resource_entry->pseries_location, "%s%d-A%x",
                    p_location->of_location,
                    ipr_get_loc_bus(p_shared_cfg, p_resource_entry->resource_address),
                    p_resource_entry->resource_address.target);
        }
        else
        {
            sprintf(p_resource_entry->pseries_location, "%s%d-A%x.%d",
                    p_location->of_location,
                    ipr_get_loc_bus(p_shared_cfg, p_resource_entry->resource_address),
                    p_resource_entry->resource_address.target,
                    p_resource_entry->resource_address.lun);
        }
    }

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
 * Returns: unique id
 *---------------------------------------------------------------------------*/
void ipr_log_dev_vpd(struct ipr_resource_entry *p_resource,
                        char *printk_level)
{
    u8 vendor_id[IPR_VENDOR_ID_LEN+1];
    u8 product_id[IPR_PROD_ID_LEN+1];
    u8 *p_char;

    memcpy(vendor_id, p_resource->std_inq_data.vpids.vendor_id,
           IPR_VENDOR_ID_LEN);
    vendor_id[IPR_VENDOR_ID_LEN] = '\0';
    memcpy(product_id, p_resource->std_inq_data.vpids.product_id,
           IPR_PROD_ID_LEN);
    product_id[IPR_PROD_ID_LEN] = '\0';

    ipr_hcam_log("Device Manufacturer: %s", vendor_id);
    ipr_hcam_log("Device Machine Type and Model: %s", product_id);

    if (p_resource->subtype != IPR_SUBTYPE_VOLUME_SET)
    {
        ipr_hcam_log("Device FRU Number: %s", p_resource->fru_number);
        ipr_hcam_log("Device EC Level: %s", p_resource->ec_level);
        ipr_hcam_log("Device Part Number: %s", p_resource->part_number);
        /* xxx    ipr_hcam_log("Device Specific (Z0): %02X%02X%02X%02X%02X%02X%02X%02X",
         p_char[0], p_char[1], p_char[2],
         p_char[3], p_char[4], p_char[5],
         p_char[6], p_char[7]);
         */        ipr_hcam_log("Device Specific (Z1): %s", p_resource->z1_term);
         ipr_hcam_log("Device Specific (Z2): %s", p_resource->z2_term);
         ipr_hcam_log("Device Specific (Z3): %s", p_resource->z3_term);
         ipr_hcam_log("Device Specific (Z4): %s", p_resource->z4_term);
         ipr_hcam_log("Device Specific (Z5): %s", p_resource->z5_term);
         ipr_hcam_log("Device Specific (Z6): %s", p_resource->z6_term);
    }
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

    ipr_hcam_log(" Physical Location: ");

    if (current_res_addr.bus == 0xff)
        ipr_hcam_log("         Current: unknown");
    else
    {
        if (current_res_addr.lun == 0)
        {
            ipr_hcam_log("         Current: %s%d-A%x",
                            ipr_cfg->shared.p_location->of_location,
                            ipr_get_loc_bus(&ipr_cfg->shared, current_res_addr),
                            current_res_addr.target);
        }
        else
        {
            ipr_hcam_log("         Current: %s%d-A%x.%d",
                            ipr_cfg->shared.p_location->of_location,
                            ipr_get_loc_bus(&ipr_cfg->shared, current_res_addr),
                            current_res_addr.target,
                            current_res_addr.lun);
        }
    }

    if (expected_res_addr.bus == 0xff)
        ipr_hcam_log("        Expected: unknown");
    else
    {
        if (expected_res_addr.lun == 0)
        {
            ipr_hcam_log("        Expected: %s%d-A%x",
                            ipr_cfg->shared.p_location->of_location,
                            ipr_get_loc_bus(&ipr_cfg->shared, expected_res_addr),
                            expected_res_addr.target);
        }
        else
        {
            ipr_hcam_log("        Expected: %s%d-A%x.%d",
                            ipr_cfg->shared.p_location->of_location,
                            ipr_get_loc_bus(&ipr_cfg->shared, expected_res_addr),
                            expected_res_addr.target,
                            expected_res_addr.lun);
        }
    }
};

static u16 ipr_blocked_processors[] =
{
    PV_NORTHSTAR,
    PV_PULSAR,
    PV_POWER4,
    PV_ICESTAR,
    PV_SSTAR,
    PV_POWER4p,
    PV_630,
    PV_630p
};

/*---------------------------------------------------------------------------
 * Purpose: Determine if this adapter is supported on this arch
 * Context: Task level
 * Lock State: io_request_lock assumed to be held
 * Returns: 0 if adapter is supported
 *---------------------------------------------------------------------------*/
int ipr_invalid_adapter(ipr_host_config *ipr_cfg)
{
    u8 rev_id;
    int i, rc = 0;

    if ((ipr_cfg->shared.vendor_id == PCI_VENDOR_ID_MYLEX) &&
        (ipr_cfg->shared.device_id == PCI_DEVICE_ID_GEMSTONE) &&
        (ipr_cfg->shared.ccin == 0x5702))
    {
        if (pci_read_config_byte(ipr_cfg->pdev,
                                 PCI_REVISION_ID, &rev_id) == PCIBIOS_SUCCESSFUL)
        {
            if (rev_id < 4)
            {
                for (i = 0; i < sizeof(ipr_blocked_processors)/sizeof(u16); i++)
                {
                    if (__is_processor(ipr_blocked_processors[i]))
                    {
                        rc = 1;
                        break;
                    }
                }
            }
        }
    }

    return rc;
}
