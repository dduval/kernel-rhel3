/*****************************************************************************/
/* iprlibtypesle.h -- driver for IBM Power Linux RAID adapters               */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/lib/iprlibtypesle.h,v 1.2 2003/10/24 20:52:18 bjking1 Exp $
 */

#ifndef iprlibtypesle_h
#define iprlibtypesle_h

/******************************************************************/
/* Note: Any additions/changes here must be duplicated in         */
/*       iprlibtypesbe.h                                       */
/******************************************************************/

#ifndef iprlib_h
#include "iprlib.h"
#endif

/**************************************************
 *   Internal Data Structures
 **************************************************/

/* 64 bytes */
struct ipr_config_table_entry{
    u8 service_level;
    u8 array_id;
    u8 capacity_reduction_hi:2;
    u8 reserved2:2;
    u8 is_hot_spare:1;
    u8 is_array_member:1;
    u8 is_compressed:1;
    u8 is_ioa_resource:1;
    u8 subtype:4;
    u8 reserved3:3;
    u8 capacity_reduction_lo:1;

#define IPRLIB_GET_CAP_REDUCTION(cfgte) \
(((cfgte).capacity_reduction_hi << 1) | (cfgte).capacity_reduction_lo)

    struct ipr_res_addr resource_address;
    u32 resource_handle;
    u32 reserved4;
    u32 reserved5;
    struct ipr_std_inq_data std_inq_data;
};

struct ipr_internal_trace_entry
{
    u32 time;

    u16 host_ioarcb_index;
    u8 device_type:4;
#define IPR_TRACE_DASD       0x0
#define IPR_TRACE_GEN        0x1
#define IPR_TRACE_IOA        0x2
    u8 type:4;
#define IPR_TRACE_START      0x0
#define IPR_TRACE_FINISH     0xf
    u8 op_code;

    u32 xfer_len;

    union {
        u32 ioasc;
        u32 res_addr;
    }data;
};

struct ipr_vendor_unique_page
{
    /* Page code 0x00 */
    struct ipr_mode_page_hdr header;

    u8 arhes:1;
    u8 reserved1:4;
    u8 dwd:1;
    u8 uqe:1;
    u8 qpe:1;

    u8 cpe:1;
    u8 rrnde:1;
    u8 reserved3:1;
    u8 dotf:1;
    u8 rpfae:1;
    u8 cmdac:1;
    u8 reserved2:1;
    u8 asdpe:1;

    u8 dlro:1;
    u8 dwlro:1;
    u8 reserved4:6;

    u8 ovple:1;
    u8 caen:1;
    u8 wpen:1;
    u8 dpsdp:1;
    u8 frdd:1;
    u8 dsn:1;
    u8 reserved5:2;

    u8 reserved7[2];

    u8 led_mode:4;
    u8 drd:1;
    u8 qemc:1;
    u8 adc:1;
    u8 reserved8:1;

    u8 temp_threshold;

    u8 cmd_aging_limit_hi;

    u8 cmd_aging_limit_lo;

    u8 qpe_read_threshold;

    u8 reserved10;

    u8 reserved12:3;
    u8 ffmt:1;
    u8 rarr:1;
    u8 reserved11:1;
    u8 dnr:1;
    u8 drrt:1;

    u8 ivr:1;
    u8 irt:1;
    u8 dsf:1;
    u8 drpdv:1;
    u8 reserved13:1;
    u8 fcert:1;
    u8 rrc:1;
    u8 rtp:1;
};

struct ipr_rw_err_mode_page
{
    /* Page code 0x01 */
    struct ipr_mode_page_hdr header;
    u8 dcr:1;
    u8 dte:1;
    u8 per:1;
    u8 eer:1;
    u8 rc:1;
    u8 tb:1;
    u8 arre:1;
    u8 awre:1;
    u8 read_retry_count;
    u8 correction_span;
    u8 head_offset_count;
    u8 data_strobe_offset_count;
    u8 reserved1;
    u8 write_retry_count;
    u8 reserved2;
    u16 recovery_time_limit;
};

struct ipr_disc_reconn_page
{
    /* Page code 0x02 */
    struct ipr_mode_page_hdr header;
    u8 buffer_full_ratio;
    u8 buffer_empty_ratio;
    u16 bus_inactivity_limit;
    u16 disconnect_time_limit;
    u16 connect_time_limit;
    u16 maximum_burst_size;
    u8 reserved1;
    u8 dtdc:3;
    u8 dimm:1;
    u8 fair_arbitration:3;
    u8 emdp:1;
    u16 first_burst_size;
};

struct ipr_format_device_page
{
    /* Page code 0x03 */
    struct ipr_mode_page_hdr header;
    u16 tracks_per_zone;
    u16 alt_sectors_per_zone;
    u16 alt_tracks_per_zone;
    u16 sectors_per_track;
    u16 data_bytes_per_phys_sector;
    u16 interleave;
    u16 track_skew_factor;
    u16 cylinder_skew_factor;
    u8 reserved:4;
    u8 surf:1;
    u8 rmb:1;
    u8 hsec:1;
    u8 ssec:1;
    u8 reserved2[3];
};

struct ipr_verify_err_rec_page
{
    /* Page code 0x07 */
    struct ipr_mode_page_hdr header;
    u8 dcr:1;
    u8 dte:1;
    u8 per:1;
    u8 eer:1;
    u8 reserved1:4;
    u8 verify_retry_count;
    u8 verify_correction_span;
    u8 reserved2[5];
    u16 verify_recovery_time;
};

struct ipr_caching_page
{
    /* Page code 0x08 */
    struct ipr_mode_page_hdr header;
    u8 rcd:1;
    u8 mf:1;
    u8 wce:1;
    u8 size:1;
    u8 disc:1;
    u8 cap:1;
    u8 abpf:1;
    u8 ic:1;
    u8 write_retention_priority:4;
    u8 demand_read_retention_priority:4;
    u16 disable_pre_fetch_xfer_len;
    u16 min_pre_fetch;
    u16 max_pre_fetch;
    u16 max_pre_fetch_ceiling;
    u8 reserved1:3;
    u8 vendor_spec:2;
    u8 dra:1;
    u8 lbcss:1;
    u8 fsw:1;
    u8 num_cache_segments;
    u16 cache_segment_size;
    u8 reserved2;
    u8 non_cache_segment_size_hi;
    u8 non_cache_segment_size_mid;
    u8 non_cache_segment_size_lo;
};

struct ipr_ioa_dasd_page_20
{
    /* Mode page 0x20 */
    struct ipr_mode_page_hdr header;
    u8 reserved:6;
    u8 disable_idle_time_diag:1;
    u8 auto_alloc_on_write:1;
    u8 max_TCQ_depth;
};

/* SIS command packet structure */
struct ipr_cmd_pkt
{
    u16 reserved;               /* Reserved by IOA */
    u8 request_type;
#define IPR_RQTYPE_SCSICDB     0x00
#define IPR_RQTYPE_IOACMD      0x01
#define IPR_RQTYPE_HCAM        0x02

    u8 luntar_luntrn;

    u8 reserved3:4;
    u8 cmd_sync_override:1;
    u8 no_underlength_checking:1;
    u8 reserved2:1;
    u8 write_not_read:1;

    u8 reserved4;

    u8 cdb[16];
    u16 cmd_timeout;
};

#endif
