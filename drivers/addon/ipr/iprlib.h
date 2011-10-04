/*****************************************************************************/
/* iprlib.h -- driver for IBM Power Linux RAID adapters                      */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprlib.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#ifndef iprlib_h
#define iprlib_h

/******************************************************************/
/* Includes                                                       */
/******************************************************************/

#ifndef iprlits_h
#include "iprlits.h"
#endif

#ifndef iprtypes_h
#include "iprtypes.h"
#endif

/******************************************************************/
/* Macros                                                         */
/******************************************************************/
#define ipr_sense_valid(byte)        ((((byte) & 0x70) >> 4) == 7)

#define IPR_MIN(a,b) (((a) < (b)) ? (a) : (b))

#if IPR_DEBUG
#define IPR_DBG_CMD(CMD)             \
{                                       \
(CMD);                                  \
}
#else
#define IPR_DBG_CMD(CMD)
#endif

#define IPR_SET_MODE(change_mask, cur_val, new_val)  \
{                                                       \
int mod_bits = (cur_val ^ new_val);                     \
if ((change_mask & mod_bits) == mod_bits)               \
{                                                       \
cur_val = new_val;                                      \
}                                                       \
}

#define IPR_GET_CAP_REDUCTION(res_flags) \
(((res_flags).capacity_reduction_hi << 1) | (res_flags).capacity_reduction_lo)

/******************************************************************/
/* Byte swapping macros                                           */
/* Note: These should only be used when initializing static data  */
/*       Normal runtime code should use the inline functions.     */
/******************************************************************/
#define _sw16(x) \
((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8))

#define _sw32(x) \
((((x) & 0x000000ff) << 24) | (((x) & 0x0000ff00) << 8) | \
(((x) & 0x00ff0000) >> 8) | (((x) & 0xff000000) >> 24))

/******************************************************************/
/* Error logging macros                                           */
/******************************************************************/
#ifdef IPR_LIBRARY
#define ipr_printk_i printk
#define ipr_printk_i_tty ipr_print_tty
#else
#define ipr_printk_i printk
#define ipr_printk_i_tty ipr_print_tty
#endif

#define ipr_log_err(...) ipr_printk_i(KERN_ERR IPR_ERR ": "__VA_ARGS__);
#define ipr_log_info(...) ipr_printk_i(KERN_INFO IPR_NAME ": "__VA_ARGS__);
#define ipr_log_crit(...) ipr_printk_i(KERN_CRIT IPR_ERR ": "__VA_ARGS__);
#define ipr_log_warn(...) ipr_printk_i(KERN_WARNING IPR_ERR": "__VA_ARGS__);
#define ipr_dbg_err(...) IPR_DBG_CMD(ipr_printk_i(KERN_ERR IPR_ERR ": "__VA_ARGS__));

#define ipr_log_err_tty(...) ipr_printk_i_tty(KERN_ERR IPR_ERR ": "__VA_ARGS__);
#define ipr_log_info_tty(...) ipr_printk_i_tty(KERN_INFO IPR_NAME ": "__VA_ARGS__);
#define ipr_log_crit_tty(...) ipr_printk_i_tty(KERN_CRIT IPR_ERR ": "__VA_ARGS__);
#define ipr_log_warn_tty(...) ipr_printk_i_tty(KERN_WARNING IPR_ERR": "__VA_ARGS__);
#define ipr_dbg_err_tty(...) IPR_DBG_CMD(ipr_printk_i_tty(KERN_ERR IPR_ERR ": "__VA_ARGS__));

#if IPR_DBG_TRACE
#define ENTER ipr_printk_i(KERN_ERR IPR_NAME": Entering %s"IPR_EOL, __FUNCTION__);
#define LEAVE ipr_printk_i(KERN_ERR IPR_NAME": Leaving %s"IPR_EOL, __FUNCTION__);
#define ipr_dbg ipr_printk_i(KERN_ERR IPR_NAME": %s: %s: Line: %d"IPR_EOL, \
                                   __FILE__, __FUNCTION__, __LINE__);
#else
#define ENTER
#define LEAVE
#define ipr_dbg
#endif

#define ipr_beg_err(level) { \
ipr_printk_i("%s"IPR_ERR": \
begin-entry***********************************************"IPR_EOL, level); \
}
#define ipr_end_err(level) { \
ipr_printk_i("%s"IPR_ERR": \
end-entry*************************************************"IPR_EOL, level); \
}

#define ipr_beg_err_tty(level) { \
ipr_printk_i_tty("%s"IPR_ERR": \
begin-entry***********************************************"IPR_EOL, level); \
}
#define ipr_end_err_tty(level) { \
ipr_printk_i_tty("%s"IPR_ERR": \
end-entry*************************************************"IPR_EOL, level); \
}

#define ipr_hcam_log(format, ...) \
ipr_printk_i("%s"IPR_ERR": "format""IPR_EOL, printk_level, ##__VA_ARGS__)

#define ipr_hcam_log_tty(format, ...) \
ipr_printk_i_tty("%s"IPR_ERR": "format""IPR_EOL, printk_level, ##__VA_ARGS__)

#define ipr_err_separator \
ipr_hcam_log("----------------------------------------------------------")

/******************************************************************/
/* Function prototypes                                            */
/******************************************************************/

/* Wrapper functions for library */
void *ipr_kmalloc(u32 size, u32 flags);
void *ipr_kcalloc(u32 size, u32 flags);

void *ipr_dma_malloc(struct ipr_shared_config *p_shared_cfg,
                        u32 size, ipr_dma_addr *p_dma_addr, u32 flags);
void *ipr_dma_calloc(struct ipr_shared_config *p_shared_cfg,
                        u32 size, ipr_dma_addr *p_dma_addr, u32 flags);

void ipr_kfree(void *ptr, u32 size);

void ipr_dma_free(struct ipr_shared_config *p_shared_cfg,
                     u32 size, void *ptr, ipr_dma_addr dma_addr);

void ipr_print_tty(char *s, ...);

void ipr_udelay(signed long delay);

void ipr_req_reset(struct ipr_shared_config *p_shared_cfg);

void ipr_sleep(signed long delay);

void ipr_ioa_loc_str(struct ipr_location_data *p_location, char *p_buf);
void ipr_dev_loc_str(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_resource_entry *p_resource, char *p_buf);

void ipr_log_ioa_physical_location(struct ipr_location_data *p_location,
                                        char *printk_level);

void ipr_log_dev_physical_location(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_res_addr resource_address,
                                      char *printk_level);

struct ipr_ccb * ipr_allocate_ccb(struct ipr_shared_config *p_shared_cfg);
void ipr_release_ccb(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_ccb *p_sis_ccb);
int ipr_do_req(struct ipr_shared_config *p_shared_cfg,
                  struct ipr_ccb *p_sis_ccb,
                  void (*done) (struct ipr_shared_config *, struct ipr_ccb *),
                  u32 timeout_in_sec);
u32 ipr_send_hcam(struct ipr_shared_config *p_shared_cfg, u8 type,
                     struct ipr_hostrcb *p_hostrcb);

void ipr_get_card_pos(struct ipr_shared_config *ipr_cfg,
                         struct ipr_res_addr resource_addr, char *p_buffer);

int ipr_get_res_addr_fmt0(struct ipr_shared_config *p_shared_cfg,
                             u32 dsa, u32 ua,
                             struct ipr_res_addr *p_res_addr);

int ipr_get_res_addr_fmt1(struct ipr_shared_config *p_shared_cfg,
                             u32 frame, char *p_slot,
                             struct ipr_res_addr *p_res_addr);

int ipr_get_res_addr_fmt2(struct ipr_shared_config *p_shared_cfg,
                              char *p_location, struct ipr_res_addr *p_res_addr);

int ipr_get_res_addr_fmt3(struct ipr_shared_config *p_shared_cfg,
                             u16 pci_bus, u16 pci_device, u8 bus, u8 target, u8 lun,
                             struct ipr_res_addr *p_res_addr);

void ipr_update_location_data(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_resource_entry *p_resource_entry);

/* Library provided functions */
int ipr_alloc_mem (struct ipr_shared_config *ipr_cfg);

int ipr_free_mem (struct ipr_shared_config *ipr_cfg);

int ipr_init_ioa_internal_part1 (struct ipr_shared_config *ipr_cfg);

int ipr_init_ioa_internal_part2 (struct ipr_shared_config *ipr_cfg);

u32 ipr_get_done_ops(struct ipr_shared_config *ipr_cfg,
                        struct ipr_ccb **pp_sis_cmnd);

int ipr_ioa_queue(struct ipr_shared_config *ipr_cfg,
                     struct ipr_ccb *p_sis_cmd);

void ipr_auto_sense(struct ipr_shared_config *ipr_cfg,
                       struct ipr_ccb *p_sis_cmd);

int ipr_queue_internal(struct ipr_shared_config *ipr_cfg,
                          struct ipr_ccb *p_sis_cmd);

void ipr_handle_config_change(struct ipr_shared_config *ipr_cfg,
                                 struct ipr_hostrcb *p_hostrcb);

u16 ipr_dasd_vpids_to_ccin(struct ipr_std_inq_vpids *p_vpids,
                              u16 default_ccin);

void ipr_dasd_vpids_to_ccin_str(struct ipr_std_inq_vpids *p_vpids,
                                   char *p_ccin, char *p_default_ccin);

void ipr_mask_interrupts(struct ipr_shared_config *ipr_cfg);

int ipr_reset_allowed(struct ipr_shared_config *ipr_cfg);
void ipr_reset_alert(struct ipr_shared_config *ipr_cfg);

int ipr_get_ldump_data_section(struct ipr_shared_config *ipr_cfg,
                                  u32 fmt2_start_addr,
                                  u32 *p_dest,
                                  u32 length_in_words);
void ipr_get_internal_trace(struct ipr_shared_config *ipr_cfg,
                               u32 **trace_block_address,
                               u32 *trace_block_length);
void ipr_copy_internal_trace_for_dump(struct ipr_shared_config *ipr_cfg,
                                         u32 *p_buffer,
                                         u32 buffer_len);
void ipr_modify_ioafp_mode_page_28(struct ipr_shared_config *ipr_cfg,
                                      struct ipr_mode_page_28_header *
                                      p_modepage_28_header,
                                      int scsi_bus);
/******************************************************************/
/* Inlines                                                        */
/******************************************************************/

/*---------------------------------------------------------------------------
 * Purpose: Verify resource address has valid bus, target, lun values
 * Lock State: N/A
 * Returns: 0 if invalid resource address
 *          1 if valid resource address
 *---------------------------------------------------------------------------*/
static inline int ipr_is_res_addr_valid(struct ipr_res_addr *res_addr)
{
    if (((res_addr->lun < IPR_MAX_NUM_LUNS_PER_TARGET) ||
         (res_addr->lun == 0xff)) &&
        ((res_addr->bus < IPR_MAX_NUM_BUSES) ||
         (res_addr->bus == 0xff)) &&
        ((res_addr->target < IPR_MAX_NUM_TARGETS_PER_BUS) ||
         (res_addr->target == 0xff)))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Identify Advanced Function DASD present
 * Lock State: N/A
 * Returns: 0 if not AF DASD
 *          1 if AF DASD
 *---------------------------------------------------------------------------*/
static inline int ipr_is_af_dasd_device(struct ipr_resource_entry *p_resource_entry)
{
    if (IPR_IS_DASD_DEVICE(p_resource_entry->std_inq_data) &&
        (!p_resource_entry->is_ioa_resource) &&
        (p_resource_entry->subtype == IPR_SUBTYPE_AF_DASD))
        return 1;
    else
        return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Identify volume set resources
 * Lock State: N/A
 * Returns: 0 if not VSET
 *          1 if VSET
 *---------------------------------------------------------------------------*/
static inline int ipr_is_vset_device(struct ipr_resource_entry *p_resource_entry)
{
    if (IPR_IS_DASD_DEVICE(p_resource_entry->std_inq_data) &&
        (!p_resource_entry->is_ioa_resource) &&
        (p_resource_entry->subtype == IPR_SUBTYPE_VOLUME_SET))
        return 1;
    else
        return 0;
}

/******************************************************************/
/* Sense data inlines                                             */
/******************************************************************/
static inline u8 ipr_sense_key(u8 *p_sense_buffer)
{
    u8 byte0 = p_sense_buffer[0] & 0x7f;

    if ((byte0 == 0x70) || (byte0 == 0x71))
        return p_sense_buffer[2];
    else if ((byte0 == 0x72) || (byte0 == 0x73))
        return p_sense_buffer[1];
    else
        return 0;
}

static inline u8 ipr_sense_code(u8 *p_sense_buffer)
{
    u8 byte0 = p_sense_buffer[0] & 0x7f;

    if ((byte0 == 0x70) || (byte0 == 0x71))
        return p_sense_buffer[12];
    else if ((byte0 == 0x72) || (byte0 == 0x73))
        return p_sense_buffer[2];
    else
        return 0;
}

static inline u8 ipr_sense_qual(u8 *p_sense_buffer)
{
    u8 byte0 = p_sense_buffer[0] & 0x7f;

    if ((byte0 == 0x70) || (byte0 == 0x71))
        return p_sense_buffer[13];
    else if ((byte0 == 0x72) || (byte0 == 0x73))
        return p_sense_buffer[3];
    else
        return 0;
}

#endif
