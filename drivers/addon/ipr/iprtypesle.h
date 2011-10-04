/*****************************************************************************/
/* iprtypesle.h -- driver for IBM Power Linux RAID adapters                  */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprtypesle.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

/******************************************************************/
/* Note: Any additions/changes here must be duplicated in         */
/*       iprtypesbe.h                                          */
/******************************************************************/

#ifndef iprtypesle_h
#define iprtypesle_h

/******************************************************************/
/* Macros                                                         */
/******************************************************************/

#define htosis16(x) swab16(x)
#define htosis32(x) swab32(x)
#define sistoh16(x) swab16(x)
#define sistoh32(x) swab32(x)

#define _i16(x) _sw16(x)
#define _i32(x) _sw32(x)

#define IPR_BIG_ENDIAN       0
#define IPR_LITTLE_ENDIAN    1

/******************************************************************/
/* Data Types                                                     */
/* Note: All IOA/device data structures using bitfields MUST be   */
/*       defined here with their bit fields in reverse order      */
/******************************************************************/

/******************************************************************/
/* SES Structures                                                 */
/******************************************************************/

struct ipr_drive_elem_status
{
    u8 status:4;
    u8 swap:1;
    u8 reserved:1;
    u8 predictive_fault:1;
    u8 select:1;

    u8 scsi_id:4;
    u8 reserved2:4;

    u8 identify:1;
    u8 reserved4:1;
    u8 remove:1;
    u8 insert:1;
    u8 reserved3:4;

    u8 reserved6:5;
    u8 fault_sensed:1;
    u8 fault_requested:1;
    u8 reserved5:1;
};

struct ipr_encl_status_ctl_pg
{
    u8 page_code;
    u8 health_status;
    u16 byte_count;
    u8 reserved1[4];

    u8 overall_status_reserved2:4;
    u8 overall_status_swap:1;
    u8 overall_status_reserved:1;
    u8 overall_status_predictive_fault:1;
    u8 overall_status_select:1;

    u8 overall_status_reserved3;

    u8 overall_status_identify:1;
    u8 overall_status_reserved5:1;
    u8 overall_status_remove:1;
    u8 overall_status_insert:1;
    u8 overall_status_reserved4:4;

    u8 overall_status_disable_resets:1;
    u8 overall_status_reserved7:4;
    u8 overall_status_fault_sensed:1;
    u8 overall_status_fault_requested:1;
    u8 overall_status_reserved6:1;

    struct ipr_drive_elem_status elem_status[IPR_NUM_DRIVE_ELEM_STATUS_ENTRIES];
};

/******************************************************************/
/* SCSI/SIS Structures                                            */
/******************************************************************/

struct ipr_mode_page_hdr
{
    u8 page_code:6;
    u8 reserved1:1;
    u8 parms_saveable:1;
    u8 page_length;
};

struct ipr_mode_page_28_scsi_dev_bus_attr
{
    struct ipr_res_addr res_addr;

    u8 reserved2:2;
    u8 lvd_to_se_transition_not_allowed:1;
    u8 target_mode_supported:1;
    u8 term_power_absent:1;
    u8 enable_target_mode:1;
    u8 qas_capability:2;
#define IPR_MODEPAGE28_QAS_CAPABILITY_NO_CHANGE      0  
#define IPR_MODEPAGE28_QAS_CAPABILITY_DISABLE_ALL    1        
#define IPR_MODEPAGE28_QAS_CAPABILITY_ENABLE_ALL     2
/* NOTE:   Due to current operation conditions QAS should
 never be enabled so the change mask will be set to 0 */
#define IPR_MODEPAGE28_QAS_CAPABILITY_CHANGE_MASK    0

    u8 scsi_id;
#define IPR_MODEPAGE28_SCSI_ID_NO_CHANGE             0x80u
#define IPR_MODEPAGE28_SCSI_ID_NO_ID                 0xFFu

    u8 bus_width;
#define IPR_MODEPAGE28_BUS_WIDTH_NO_CHANGE           0

    u8 extended_reset_delay;
#define IPR_EXTENDED_RESET_DELAY                     7

    u32 max_xfer_rate;
#define IPR_MODEPAGE28_MAX_XFR_RATE_NO_CHANGE        0

    u8  min_time_delay;
#define IPR_DEFAULT_SPINUP_DELAY                     0xFFu
#define IPR_INIT_SPINUP_DELAY                        5
    u8  reserved3;
    u16 reserved4;
};

struct ipr_control_mode_page
{
    /* Mode page 0x0A */
    struct ipr_mode_page_hdr header;
    u8 rlec:1;
    u8 gltsd:1;
    u8 reserved1:3;
    u8 tst:3;
    u8 dque:1;
    u8 qerr:2;
    u8 reserved2:1;
    u8 queue_algorithm_modifier:4;
    u8 eaerp:1;
    u8 uaaerp:1;
    u8 raerp:1;
    u8 swp:1;
    u8 reserved4:2;
    u8 rac:1;
    u8 reserved3:1;

    u8 reserved5;
    u16 ready_aen_holdoff_period;
    u16 busy_timeout_period;
    u16 reserved6;
};

/* 44 bytes */
struct ipr_std_inq_data{
    u8 peri_dev_type:5;
    u8 peri_qual:3;

    u8 reserved1:7;
    u8 removeable_medium:1;

    u8 version;

    u8 resp_data_fmt:4;
    u8 hi_sup:1;
    u8 norm_aca:1;
    u8 obsolete1:1;
    u8 aen:1;

    u8 additional_len;

    u8 reserved2:7;
    u8 sccs:1;

    u8 addr16:1;
    u8 obsolete2:2;
    u8 mchngr:1;
    u8 multi_port:1;
    u8 vs:1;
    u8 enc_serv:1;
    u8 bque:1;

    u8 vs2:1;
    u8 cmd_que:1;
    u8 trans_dis:1;
    u8 linked:1;
    u8 sync:1;
    u8 wbus16:1;
    u8 obsolete3:1;
    u8 rel_adr:1;

    /* Vendor and Product ID */
    struct ipr_std_inq_vpids vpids;

    /* ROS and RAM levels */
    u8 ros_rsvd_ram_rsvd[4];

    /* Serial Number */
    u8 serial_num[IPR_SERIAL_NUM_LEN];
};

struct ipr_std_inq_data_long
{
    struct ipr_std_inq_data std_inq_data;
    u8 z1_term[IPR_STD_INQ_Z1_TERM_LEN];
    u8 ius:1;
    u8 qas:1;
    u8 clocking:2;
    u8 reserved:4;
    u8 reserved1[41];
    u8 z2_term[IPR_STD_INQ_Z2_TERM_LEN];
    u8 z3_term[IPR_STD_INQ_Z3_TERM_LEN];
    u8 reserved2;
    u8 z4_term[IPR_STD_INQ_Z4_TERM_LEN];
    u8 z5_term[IPR_STD_INQ_Z5_TERM_LEN];
    u8 part_number[IPR_STD_INQ_PART_NUM_LEN];
    u8 ec_level[IPR_STD_INQ_EC_LEVEL_LEN];
    u8 fru_number[IPR_STD_INQ_FRU_NUM_LEN];
    u8 z6_term[IPR_STD_INQ_Z6_TERM_LEN];
};

/* 1024 bytes */
struct ipr_hostrcb
{
    u8 op_code;

    u8  notificationType;

    u8  notificationsLost;

    u8  reserved0:6;
    u8  error_resp_sent:1;
    u8  internal_oper_flag:1;

    u8  overlayId;

    u8  reserved1[3];
    u32 ilid;
    u32 timeSinceLastIoaReset;
    u32 reserved2;
    u32 length;

    union {
        struct ipr_hostrcb_error error;
        struct ipr_hostrcb_cfg_ch_not ccn;
    }data;
};

struct ipr_array_cap_entry
{
    u8                          prot_level;
#define IPR_DEFAULT_RAID_LVL "5"
    u8                          reserved:7;
    u8                          include_allowed:1;
    u16                         reserved2;
    u8                          reserved3;
    u8                          max_num_array_devices;
    u8                          min_num_array_devices;
    u8                          min_mult_array_devices;
    u16                         reserved4;
    u16                         supported_stripe_sizes;
    u16                         reserved5;
    u16                         recommended_stripe_size;
    u8                          prot_level_str[8];
};

struct ipr_array_record
{
    struct ipr_record_common common;
    u8  reserved:6;
    u8  known_zeroed:1;
    u8  issue_cmd:1;
    u8  reserved1;

    u8  reserved2:5;
    u8  non_func:1;
    u8  exposed:1;
    u8  established:1;

    u8  reserved3:5;
    u8  resync_cand:1;
    u8  stop_cand:1;
    u8  start_cand:1;

    u8  reserved4[3];
    u8  array_id;
    u32 reserved5;
};

struct ipr_array2_record
{
    struct ipr_record_common common;

    u8  reserved1:6;
    u8  known_zeroed:1;
    u8  issue_cmd:1;

    u8  reserved2;

    u8  reserved3:3;
    u8  no_config_entry:1;
    u8  high_avail:1;
    u8  non_func:1;
    u8  exposed:1;
    u8  established:1;

    u8  reserved4:5;
    u8  resync_cand:1;
    u8  stop_cand:1;
    u8  start_cand:1;

    u16 stripe_size;

    u8  raid_level;
    u8  array_id;
    u32 resource_handle;
    u32 resource_address;
    struct ipr_res_addr last_resource_address;
    u8  vendor_id[8];
    u8  product_id[16];
    u8  serial_number[8];
    u32 reserved;
};

struct ipr_resource_flags
{
    u8 capacity_reduction_hi:2;
    u8 reserved2:1;
    u8 aff:1;
    u8 reserved1:1;
    u8 is_array_member:1;
    u8 is_compressed:1;
    u8 is_ioa_resource:1;

    u8 reserved3:7;
    u8 capacity_reduction_lo:1;
};

struct ipr_device_record
{
    struct ipr_record_common common;
    u8  reserved:6;
    u8  known_zeroed:1;
    u8  issue_cmd:1;

    u8  reserved1;

    u8  reserved2:2;
    u8  no_cfgte_dev:1;
    u8  no_cfgte_vol:1;
    u8  is_hot_spare:1;
    u8  is_exposed_device:1;
    u8  has_parity:1;
    u8  array_member:1;

    u8  zero_cand:1;
    u8  rebuild_cand:1;
    u8  exclude_cand:1;
    u8  include_cand:1;
    u8  resync_cand:1;
    u8  stop_cand:1;
    u8  parity_cand:1;
    u8  start_cand:1;

    u8  reserved3:6;
    u8  rmv_hot_spare_cand:1;
    u8  add_hot_spare_cand:1;

    u8  reserved4[2];
    u8  array_id;
    u32 resource_handle;
    u16 reserved5;
    struct ipr_resource_flags resource_flags_to_become;
    u32 user_area_size_to_become;  
};

struct ipr_reclaim_query_data
{
    u8 action_status;
#define IPR_ACTION_SUCCESSFUL               0
#define IPR_ACTION_NOT_REQUIRED             1
#define IPR_ACTION_NOT_PERFORMED            2
    u8 num_blocks_needs_multiplier:1;
    u8 reserved3:1;
    u8 reclaim_unknown_performed:1;
    u8 reclaim_known_performed:1;
    u8 reserved2:2;
    u8 reclaim_unknown_needed:1;
    u8 reclaim_known_needed:1;

    u16 num_blocks;

    u8 rechargeable_battery_type;
#define IPR_BATTERY_TYPE_NO_BATTERY          0
#define IPR_BATTERY_TYPE_NICD                1
#define IPR_BATTERY_TYPE_NIMH                2
#define IPR_BATTERY_TYPE_LIION               3

    u8 rechargeable_battery_error_state;
#define IPR_BATTERY_NO_ERROR_STATE           0
#define IPR_BATTERY_WARNING_STATE            1
#define IPR_BATTERY_ERROR_STATE              2

    u8 reserved4[2];

    u16 raw_power_on_time;
    u16 adjusted_power_on_time;
    u16 estimated_time_to_battery_warning;
    u16 estimated_time_to_battery_failure;

    u8 reserved5[240];
};

struct ipr_vset_res_state
{
    u16 stripe_size;
    u8 prot_level;
    u8 num_devices_in_vset;
    u32 reserved6;
};

struct ipr_dasd_res_state
{
    u32 data_path_width;  /* bits */
    u32 data_xfer_rate;   /* 100 KBytes/second */
};

struct ipr_query_res_state
{
    u8 reserved2:4;
    u8 not_func:1;
    u8 not_ready:1;
    u8 not_oper:1;
    u8 reserved1:1;

    u8 reserved3:7;
    u8 read_write_prot:1;

    u8 reserved4:3;
    u8 service_req:1;
    u8 degraded_oper:1;
    u8 prot_resuming:1;
    u8 prot_suspended:1;
    u8 prot_dev_failed:1;

    u8 reserved5;

    union
    {
        struct ipr_vset_res_state vset;
        struct ipr_dasd_res_state dasd;
    }dev;

    u32 ilid;
    u32 failing_dev_ioasc;
    struct ipr_res_addr failing_dev_res_addr;
    u32 failing_dev_res_handle;
    u8 protection_level_str[8];
};

/* IBM's SIS smart dump table structures */
struct ipr_sdt_entry
{
    u32 bar_str_offset;
    u32 end_offset;
    u8  entry_byte;
    u8  reserved[3];
    u8  reserved2:5;
    u8  valid_entry:1;
    u8  reserved1:1;
    u8  endian:1;
    u8  resv;
    u16 priority;
};

#endif
