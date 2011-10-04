/*****************************************************************************/
/* iprtypes.h -- driver for IBM Power Linux RAID adapters                    */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprtypes.h,v 1.2 2003/10/24 20:52:17 bjking1 Exp $
 */

#ifndef iprtypes_h
#define iprtypes_h

struct ipr_std_inq_vpids
{
    u8 vendor_id[IPR_VENDOR_ID_LEN];          /* Vendor ID */
    u8 product_id[IPR_PROD_ID_LEN];           /* Product ID */
};

struct ipr_res_addr
{
    u8 reserved;
    u8 bus;
    u8 target;
    u8 lun;
#define IPR_GET_PHYSICAL_LOCATOR(res_addr) \
(((res_addr).bus << 16) | ((res_addr).target << 8) | (res_addr).lun)
};

struct ipr_read_cap
{
    u32 max_user_lba;
    u32 block_length;
};

struct ipr_read_cap16
{
    u32 max_user_lba_hi;
    u32 max_user_lba_lo;
    u32 block_length;
};

struct ipr_hostrcb_device_data_entry
{
    struct ipr_std_inq_vpids dev_vpids;
    u8 dev_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_res_addr dev_res_addr;
    struct ipr_std_inq_vpids new_dev_vpids;
    u8 new_dev_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids ioa_last_with_dev_vpids;
    u8 ioa_last_with_dev_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids cfc_last_with_dev_vpids;
    u8 cfc_last_with_dev_sn[IPR_SERIAL_NUM_LEN];
    u32 ioa_data[5];
};

struct ipr_hostrcb_array_data_entry
{
    struct ipr_std_inq_vpids vpids;
    u8 serial_num[IPR_SERIAL_NUM_LEN];
    struct ipr_res_addr expected_dev_res_addr;
    struct ipr_res_addr dev_res_addr;
};

struct ipr_hostrcb_type_ff_error
{
    u32 ioa_data[246];
};

struct ipr_hostrcb_type_01_error
{
    u32 seek_counter;
    u32 read_counter;
    u8  sense_data[32];
    u32 ioa_data[236];
};

struct ipr_hostrcb_type_02_error
{
    struct ipr_std_inq_vpids ioa_vpids;
    u8 ioa_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids cfc_vpids;
    u8 cfc_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids ioa_last_attached_to_cfc_vpids;
    u8 ioa_last_attached_to_cfc_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids cfc_last_attached_to_ioa_vpids;
    u8 cfc_last_attached_to_ioa_sn[IPR_SERIAL_NUM_LEN];
    u32 ioa_data[3];
    u8 reserved[844];
};

struct ipr_hostrcb_type_03_error
{
    struct ipr_std_inq_vpids ioa_vpids;
    u8 ioa_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids cfc_vpids;
    u8 cfc_sn[IPR_SERIAL_NUM_LEN];
    u32 errors_detected;
    u32 errors_logged;
    u8 ioa_data[12];
    struct ipr_hostrcb_device_data_entry dev_entry[3];
    u8 reserved[444];
};

struct ipr_hostrcb_type_04_error
{
    struct ipr_std_inq_vpids ioa_vpids;
    u8 ioa_sn[IPR_SERIAL_NUM_LEN];
    struct ipr_std_inq_vpids cfc_vpids;
    u8 cfc_sn[IPR_SERIAL_NUM_LEN];
    u8 ioa_data[12];
    struct ipr_hostrcb_array_data_entry array_member[10];
    u32 exposed_mode_adn;
    u32 array_id;
    struct ipr_std_inq_vpids incomp_dev_vpids;
    u8 incomp_dev_sn[IPR_SERIAL_NUM_LEN];
    u32 ioa_data2;

    struct ipr_hostrcb_array_data_entry array_member2[8];
    struct ipr_res_addr last_functional_vset_res_addr;
    u8 vset_serial_num[IPR_SERIAL_NUM_LEN];
    u8 protection_level[8];
    u8 reserved[124];
};

struct ipr_hostrcb_error
{
    u32 failing_dev_ioasc;
    struct ipr_res_addr failing_dev_res_addr;
    u32 failing_dev_resource_handle;
    u32 prc;
    union {
        struct ipr_hostrcb_type_ff_error type_ff_error;
        struct ipr_hostrcb_type_01_error type_01_error;
        struct ipr_hostrcb_type_02_error type_02_error;
        struct ipr_hostrcb_type_03_error type_03_error;
        struct ipr_hostrcb_type_04_error type_04_error;
    }data;
};

struct ipr_hostrcb_cfg_ch_not
{
    u8 reserved[1024];
};

struct ipr_record_common
{
    u16 record_id;
    u16 record_len;
};

struct ipr_supported_arrays
{
    struct ipr_record_common common;
    u16                         num_entries;
    u16                         entry_length;
    u8                          data[0];
};

#if (defined(__KERNEL__) && defined(__LITTLE_ENDIAN)) || \
(!defined(__KERNEL__) && (__BYTE_ORDER == __LITTLE_ENDIAN))
#ifndef iprtypesle_h
#include "iprtypesle.h"
#endif
#elif (defined(__KERNEL__) && defined(__BIG_ENDIAN)) || \
(!defined(__KERNEL__) && (__BYTE_ORDER == __BIG_ENDIAN))
#ifndef iprtypesbe_h
#include "iprtypesbe.h"
#endif
#else
#error "Neither __LITTLE_ENDIAN nor __BIG_ENDIAN defined"
#endif

#define IPR_RECORD_ID_SUPPORTED_ARRAYS       _i16((u16)0)
#define IPR_RECORD_ID_ARRAY_RECORD           _i16((u16)1)
#define IPR_RECORD_ID_DEVICE_RECORD          _i16((u16)2)
#define IPR_RECORD_ID_COMP_RECORD            _i16((u16)3)
#define IPR_RECORD_ID_ARRAY2_RECORD          _i16((u16)4)
#define IPR_RECORD_ID_DEVICE2_RECORD         _i16((u16)5)

typedef u32 ipr_dma_addr;

#ifdef __KERNEL__
enum boolean
{
    false = 0,
    true = 1
};

typedef enum boolean bool;
#endif

/* NOTE: The structure below is a shared structure with user-land tools */
/* We need to make sure we don't put pointers in here as the
 utilities could be running in 32 bit mode on a 64 bit kernel. */
struct ipr_ioctl_cmd_type2
{
    u32 type:8;  /* type is used to distinguish between ioctl_cmd structure formats */
#define IPR_IOCTL_TYPE_2     0x03
    u32 reserved:21;
    u32 read_not_write:1; /* data direction */
    u32 device_cmd:1; /* used to pass commands to specific devices identified by resource address */
    u32 driver_cmd:1; /* used exclusively to pass commands to the device driver, 0 otherwise */
    struct ipr_res_addr resource_address;
#define IPR_CDB_LEN          16
    u8 cdb[IPR_CDB_LEN];
    u32 buffer_len;
    u8 buffer[0];
};

/* The structures below are deprecated and should not be used. Use the
 structure above to send ioctls instead */
struct ipr_ioctl_cmd_internal
{
    u32 read_not_write:1;
    u32 device_cmd:1;
    u32 driver_cmd:1;
    u32 reserved:29;
    struct ipr_res_addr resource_address;
#define IPR_CDB_LEN     16
    u8 cdb[IPR_CDB_LEN];
    void *buffer;
    u32 buffer_len;
};

struct ipr_driver_cfg
{
    u16 debug_level;
    u16 trace_level;
    u16 debug_level_max;
    u16 trace_level_max;
};

struct ipr_drive_global_desc_hdr
{
    u32 length;
};

/******************************************************************/
/* SES Structures                                                 */
/******************************************************************/

struct ipr_drive_global_desc_c
{
    u8 fru_label_hdr[3];
    u8 fru_label[4];
    u8 frame_id_hdr[3];
    u8 frame_id[2];
};

struct ipr_drive_global_desc_d
{
    u8 fru_label_hdr[3];
    u8 fru_label[5];
    u8 frame_id_hdr[3];
    u8 frame_id[2];
};

struct ipr_ses_slot_map
{
    u8 scsi_id;
    u8 label[3];
    u8 slot_populated;
    u8 carrier_info;
    u8 bus_info;
    u8 slot_info;
    u8 left_pitch;
    u8 right_pitch;
    u8 reserved[6];
};

struct ipr_drive_element_desc
{
    u8     hdr_bytes[4];
    struct ipr_ses_slot_map slot_map;
};

struct ipr_element_desc_page_c /* Diag page to get Element descriptors (vpd info) */
{
    struct ipr_drive_global_desc_c  drive_global_desc;
    struct ipr_drive_element_desc drive_elem_desc[IPR_MAX_NUM_ELEM_DESCRIPTORS];
};

struct ipr_element_desc_page_d /* Diag page to get Element descriptors (vpd info) */
{
    struct ipr_drive_global_desc_d  drive_global_desc;
    struct ipr_drive_element_desc drive_elem_desc[IPR_MAX_NUM_ELEM_DESCRIPTORS];
};

struct ipr_element_desc_page /* Diag page to get Element descriptors (vpd info) */
{
    u8                    pageCode;            /* Byte 0 */
    u8                    reserved;            /* Byte 1 */
    u8                    byte_count_hi;       /* Byte 2 */
    u8                    byte_count_lo;       /* Byte 3 */
    u8                    reserved1[4];        /* Byte 4-7 */
    struct ipr_drive_global_desc_hdr global_desc_hdr;
    union {
        struct ipr_element_desc_page_c  drive_elem_desc_c;
        struct ipr_element_desc_page_d  drive_elem_desc_d;
    }desc;
};

/******************************************************************/
/* SCSI Structures                                                */
/******************************************************************/

struct ipr_scsi_command {
    u8 cmd;
    u8 data[4];
    u8 control;
};

struct ipr_mode_parm_hdr
{
    u8 length;
    u8 medium_type;
    u8 device_spec_parms;
    u8 block_desc_len;
};

struct ipr_block_desc {
    u8 num_blocks[4];
    u8 density_code;
    u8 block_length[3];
};

struct ipr_mode_page_28_header
{
    struct ipr_mode_page_hdr header;
    u8 num_dev_entries;
    u8 dev_entry_length;
};

struct ipr_lun
{
    struct ipr_resource_entry *p_resource_entry;
    u32 is_valid_entry:1;
    u32 stop_new_requests:1;
    u32 expect_ccm:1;
    u32 reserved:1;
    u32 dev_changed:1;
    u32 reserved1:27;
    u32 reserved_pad;
};

struct ipr_target
{
    struct ipr_lun lun[IPR_MAX_NUM_LUNS_PER_TARGET];
};

struct ipr_bus
{
    struct ipr_target target[IPR_MAX_NUM_TARGETS_PER_BUS];
};

struct ipr_std_inq_vpids_sn
{
    struct ipr_std_inq_vpids vpids;
    u8 serial_num[IPR_SERIAL_NUM_LEN];
};

struct ipr_ioa_vpd {
    struct ipr_std_inq_data std_inq_data;
    u8 ascii_part_num[12];
    u8 reserved[40];
    u8 ascii_plant_code[4];
};

struct ipr_cfc_vpd {
    u8 peri_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 add_page_len;
    u8 ascii_len;
    u8 cache_size[3];
    struct ipr_std_inq_vpids vpids;
    u8 model_num[3];
    u8 reserved2[9];
    u8 revision_level[4];
    u8 serial_num[IPR_SERIAL_NUM_LEN];
    u8 ascii_part_num[12];
    u8 reserved3[40];
    u8 ascii_plant_code[4];
};

struct ipr_dram_vpd {
    u8 peri_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 add_page_len;
    u8 ascii_len;
    u8 dram_size[3];
};

struct ipr_inquiry_page0  /* Supported Vital Product Data Pages */
{
    u8 peri_qual_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 page_length;
    u8 supported_page_codes[IPR_MAX_NUM_SUPP_INQ_PAGES];
};

struct ipr_inquiry_page3
{
    u8 peri_qual_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 page_length;
    u8 ascii_len;
    u8 reserved2[3];
    u8 load_id[4];
    u8 major_release;
    u8 card_type;
    u8 minor_release[2];
    u8 ptf_number[4];
    u8 patch_number[4];
};

struct ipr_dasd_inquiry_page3
{
    u8 peri_qual_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 page_length;
    u8 ascii_len;
    u8 reserved2[3];
    u8 load_id[4];
    u8 release_level[4];
};

struct ipr_dasd_ucode_header
{
    u8 length[3];
    u8 load_id[4];
    u8 modification_level[4];
    u8 ptf_number[4];
    u8 patch_number[4];
};

struct ipr_software_inq_lid_info
{
    u32  load_id;
    u32  timestamp[3];
};

struct ipr_inquiry_page_cx  /* Extended Software Inquiry  */
{
    u8 peri_qual_dev_type;
    u8 page_code;
    u8 reserved1;
    u8 page_length;
    u8 ascii_length;
    u8 reserved2[3];

    struct ipr_software_inq_lid_info lidinfo[15];
};

struct ipr_log_sense_supported_pages
{
    u8 page_code;
    u8 reserved;
    u16 page_length;
    u8 page[100];
};

struct ipr_log_sense_perf_page /* DASD Performance counters */
{
    u8 page_code;
    u8 reserved;
    u16 page_length;
    u8 reserved2[3];
    u8 parm_length;
    u16 num_seeks_zero_len;
    u16 num_seeks_gt_two_thirds;
    u16 num_seeks_one_third_two_thirds;
    u16 num_seeks_one_sixth_one_third;
    u16 num_seeks_one_twelfth_one_sixth;
    u16 num_seeks_zero_one_twelfth;
    u32 reserved3;
    u16 num_dev_read_buffer_overruns;
    u16 num_dev_write_buffer_underruns;
    u32 num_dev_cache_read_hits;
    u32 num_dev_cache_partial_read_hits;
    u32 num_dev_cache_write_hits;
    u32 num_dev_cache_fast_writes;
    u32 reserved4[2];
    u32 num_dev_read_ops;
    u32 num_dev_write_ops;
    u32 num_ioa_cache_read_hits;
    u32 num_ioa_cache_partial_read_hits;
    u32 num_ioa_cache_write_hits;
    u32 num_ioa_cache_fast_writes;
    u32 num_ioa_emulated_read_cache_hits;
    u32 ioa_idle_loop_count[2];
    u32 ioa_idle_count_value;
    u8 ioa_idle_count_value_units;
    u8 reserved5[3];
};

struct ipr_resource_entry
{
    u8 is_ioa_resource:1;
    u8 is_compressed:1;
    u8 is_array_member:1;
    u8 format_allowed:1;
    u8 dev_changed:1;
    u8 in_init:1;
    u8 redo_init:1;
    u8 rw_protected:1;

    u8 level;
    u8 array_id;
    u8 subtype;

    struct ipr_res_addr resource_address;

    u16 type;

    u16 model;

    /* The following two fields are used only for DASD */
    u32 sw_load_id;

    u32 sw_release_level;

    char serial_num[IPR_SERIAL_NUM_LEN+1]; /* Null terminated ascii */

    u8 is_hidden:1;
    u8 is_af:1;
    u8 is_hot_spare:1;
    u8 supports_qas:1;
    u8 nr_ioa_microcode:1;
    u8 ioa_dead:1;
    u8 reserved4:2;

    u16 host_no;

    u32 resource_handle;                /* In big endian byteorder */

    /* NOTE: DSA/UA and frame_id/slot_label are only valid in iSeries Linux */
    /*       In other architectures, DSA/UA will be set to zero and         */
    /*       frame_id/slot_label will be initialized to ASCII spaces        */
    u32 dsa;
#define IPR_SYS_IPR_BUS_MASK              0xffff0000
#define IPR_SYS_CARD_MASK                    0x0000ff00
#define IPR_IO_ADAPTER_MASK                  0x000000ff
#define IPR_GET_SYS_BUS(dsa)                                        \
    ((dsa & IPR_SYS_IPR_BUS_MASK) >> 16)
#define IPR_GET_SYS_CARD(dsa)                                       \
    ((dsa & IPR_SYS_CARD_MASK) >> 8)
#define IPR_GET_IO_ADAPTER(dsa)                                     \
    (dsa & IPR_IO_ADAPTER_MASK)

    u32 unit_address;
#define IPR_IO_BUS_MASK                     0x0f000000
#define IPR_CTL_MASK                        0x00ff0000
#define IPR_DEV_MASK                        0x0000ff00
#define IPR_GET_IO_BUS(ua)                                          \
    ((ua & IPR_IO_BUS_MASK) >> 24)
#define IPR_GET_CTL(ua)                                             \
    ((ua & IPR_CTL_MASK) >> 16)
#define IPR_GET_DEV(ua)                                             \
    ((ua & IPR_DEV_MASK) >> 8)

    u32 pci_bus_number;
    u32 pci_slot;

    u8 frame_id[3];
    u8 slot_label[4];
    u8 pseries_location[IPR_MAX_PSERIES_LOCATION_LEN+1];

    u8 part_number[IPR_STD_INQ_PART_NUM_LEN+1];
    u8 ec_level[IPR_STD_INQ_EC_LEVEL_LEN+1];
    u8 fru_number[IPR_STD_INQ_FRU_NUM_LEN+1];
    u8 z1_term[IPR_STD_INQ_Z1_TERM_LEN+1];
    u8 z2_term[IPR_STD_INQ_Z2_TERM_LEN+1];
    u8 z3_term[IPR_STD_INQ_Z3_TERM_LEN+1];
    u8 z4_term[IPR_STD_INQ_Z4_TERM_LEN+1];
    u8 z5_term[IPR_STD_INQ_Z5_TERM_LEN+1];
    u8 z6_term[IPR_STD_INQ_Z6_TERM_LEN+1];

    struct ipr_std_inq_data std_inq_data;
};

struct ipr_resource_dll
{
    struct ipr_resource_entry data;
    struct ipr_resource_dll *next;
    struct ipr_resource_dll *prev;
};

struct ipr_resource_hdr
{
    u16 num_entries;
    u16 reserved;
};

struct ipr_resource_table
{
    struct ipr_resource_hdr   hdr;
    struct ipr_resource_entry dev[IPR_MAX_PHYSICAL_DEVS];
};

struct ipr_array_query_data
{
    u16 resp_len;
    u8  reserved;
    u8  num_records;
    u8 data[IPR_QAC_BUFFER_SIZE];
};

struct ipr_discard_cache_data
{
    u32 length;
    union { 
        struct ipr_std_inq_vpids_sn vpids_sn;
        u32 add_cmd_parms[10];
    }data;
};

struct ipr_cmd_status_record
{
    u16 reserved1;
    u16 length;
    u8 array_id;
    u8 command_code;
    u8 status;
#define IPR_CMD_STATUS_SUCCESSFUL            0
#define IPR_CMD_STATUS_IN_PROGRESS           2
#define IPR_CMD_STATUS_ATTRIB_CHANGE         3
#define IPR_CMD_STATUS_FAILED                4
#define IPR_CMD_STATUS_INSUFF_DATA_MOVED     5

    u8 percent_complete;
    struct ipr_res_addr failing_dev_res_addr;
    u32 failing_dev_res_handle;
    u32 failing_dev_ioasc;
    u32 ilid;
    u32 resource_handle;
};

struct ipr_cmd_status
{
    u16 resp_len;
    u8  reserved;
    u8  num_records;
    struct ipr_cmd_status_record record[100];
};

/* The addresses in here must be PCI addresses */
struct ipr_sglist
{
    u32 address;
    u32 length;
};

struct ipr_ccb
{
    struct ipr_ccb *p_next_done;
    struct ipr_resource_entry *p_resource;

    u32 completion;
    /* Valid Values: IPR_RC_SUCCESS      */
    /*               IPR_RC_DID_RESET    */
    /*               IPR_RC_FAILED       */


    u8 status;          /* SCSI status byte */
    u8 task_attributes:3;
#define IPR_UNTAGGED_TASK            0x0u
#define IPR_SIMPLE_TASK              0x1u
#define IPR_ORDERED_TASK             0x2u
#define IPR_HEAD_OF_QUEUE_TASK       0x3u
#define IPR_ACA_TASK                 0x4u

    u8 reserved:5;

    u16 flags;
#define IPR_BLOCKING_COMMAND         0x0001
#define IPR_ABORTING                 0x0002
#define IPR_TIMED_OUT                0x0004
#define IPR_GPDD_CMD                 0x0008
#define IPR_FINISHED                 0x0010
#define IPR_ERP_CMD                  0x0020
#define IPR_UNMAP_ON_DONE            0x0040
#define IPR_INTERNAL_REQ             0x0080
#define IPR_IOA_CMD                  0x0100
#define IPR_CMD_SYNC_OVERRIDE        0x0200
#define IPR_BUFFER_MAPPED            0x0400
#define IPR_ALLOW_REQ_OVERRIDE       0x0800

#define IPR_CCB_CDB_LEN      16
    u8 cdb[IPR_CCB_CDB_LEN];
    u8 cmd_len;                         /* Length of CDB */

    u8 data_direction;                  /* Used by iprlib */
#define IPR_DATA_UNKNOWN       0
#define IPR_DATA_WRITE         1
#define IPR_DATA_READ          2
#define IPR_DATA_NONE          3

    u8 saved_data_direction;            /* Used during ERP for GPDD */

    u8 sc_data_direction;               /* Copy of data direction from midlayer */
                                        /* iprlib should not touch this field */

    u8 use_sg;                          /* Used by iprlib */

    u8 saved_use_sg;                    /* Used during ERP for GPDD */

    u8 scsi_use_sg;                     /* Copy of data direction from midlayer */
                                        /* iprlib should not touch this field */

    u8 job_step;                        /* Used during device bringup jobs */

    u32 bufflen;                        /* Total length of data buffer */

    void *buffer;                       /* Pointer to data buffer if scatter/gather not used */
    void *request_buffer;               /* Pointer to scatter/gather list */
    ipr_dma_addr buffer_dma;         /* DMA address of data buffer if scatter/gather not used */
    u32 underflow;                      /* Return an error if less that underflow bytes transfered */
    u32 residual;                       /* Residual byte count */
    u32 timeout;                        /* Timeout is seconds */
    u32 reserved2;
    ipr_dma_addr sense_buffer_dma;   /* DMA address of SCSI sense buffer */
    u8 *sense_buffer;                   /* SCSI sense buffer */
    void *p_scratch;
    struct ipr_sglist *sglist;       /* Pointer to mapped scatter/gather list */
};

struct ipr_shared_config
{
    char eye_catcher[16];
#define IPR_SHARED_LABEL     "sis_start_share"

    void *p_data;
    void *p_end;

    u32 ioa_operational:1;
    u32 set_mode_page_20:1;
    u32 allow_interrupts:1;
    u32 use_immed_format:1;
    u32 nr_ioa_microcode:1;
    u32 ioa_is_dead:1;
    u32 needs_download:1;
    u32 offline_dump:1;
    u32 reserved:24;
    u32 dram_size;

    unsigned long hdw_bar_addr[4];
    unsigned long hdw_bar_addr_pci[4];
    unsigned long hdw_dma_regs;         /* iomapped PCI memory space (Registers) */
    unsigned long hdw_dma_regs_pci;     /* raw PCI memory space (Registers) */
    unsigned long ioa_mailbox;

    struct ipr_location_data *p_location;

    u8 debug_level;
#define IPR_ADVANCED_DEBUG        4
#define IPR_DEFAULT_DEBUG_LEVEL   2
    u8 trace;
#define IPR_TRACE            1
    u16 ccin;
    char ccin_str[10];
    u16 vendor_id;
    u16 device_id;
    u16 subsystem_id;
    u8 chip_rev_id;
    u8 reserved2[3];
    u16 host_no;
    u16 num_physical_buses;

    char resource_table_label[8];
#define IPR_RES_TABLE_LABEL          "res_tbl"
    struct ipr_resource_dll *resource_entry_list;
    struct ipr_resource_dll *rsteFreeH;
    struct ipr_resource_dll *rsteFreeT;
    struct ipr_resource_dll *rsteUsedH;
    struct ipr_resource_dll *rsteUsedT;

    char ses_table_start[8];
#define IPR_DATA_SES_DATA_START      "ses"
    struct ipr_element_desc_page *p_ses_data[IPR_MAX_NUM_BUSES];
    ipr_dma_addr ses_data_dma[IPR_MAX_NUM_BUSES];

    struct ipr_ioa_vpd *p_ioa_vpd;
    struct ipr_cfc_vpd *p_cfc_vpd;
    ipr_dma_addr cfc_vpd_dma;
    ipr_dma_addr ioa_vpd_dma;

    struct ipr_inquiry_page3 *p_ucode_vpd;
    struct ipr_inquiry_page0 *p_page0_vpd;
    ipr_dma_addr ucode_vpd_dma;
    ipr_dma_addr page0_vpd_dma;

    struct ipr_dram_vpd *p_dram_vpd;
    ipr_dma_addr dram_vpd_dma;
    u32 reserved_pad;
    struct ipr_vpd_cbs *p_vpd_cbs;
    struct ipr_page_28_data *p_page_28;

    /* The size of this is + 1 to allow for volume sets on
       converged adapters.  Volume sets are identified with
       a bus of 0xff so to avoid a large array, any reference
       to this data array will require the actual bus # + 1
       as the index.  It is expected the 0xff index will
       then become 0x00 */
    struct ipr_bus bus[IPR_MAX_NUM_BUSES + 1];

    struct ipr_resource_entry ioa_resource;

    char ioa_host_str[IPR_MAX_LOCATION_LEN];

    char end_eye_catcher[16];
#define IPR_END_SHARED_LABEL         "sis_end_share"
};

struct ipr_vpd_cbs
{
    struct ipr_ioa_vpd ioa_vpd;
    struct ipr_cfc_vpd cfc_vpd;
    struct ipr_inquiry_page3 page3_data;
    struct ipr_inquiry_page0 page0_data;
    struct ipr_dram_vpd dram_vpd;
};

struct ipr_page_28
{
    struct ipr_mode_parm_hdr parm_hdr;
    struct ipr_mode_page_28_header page_hdr;
    struct ipr_mode_page_28_scsi_dev_bus_attr attr[IPR_MAX_NUM_BUSES];
};

struct ipr_page_28_data
{
    struct ipr_page_28 dflt;
    struct ipr_page_28 saved;
    struct ipr_page_28 changeable;
};

struct ipr_ioa_cfg_t
{
    u16 vendor_id;
    u16 device_id;
    u32 bar_index;
    u32 mailbox;
    u32 mbx_bar_sel_mask;
    u32 mkr_bar_sel_shift;
    u32 mbx_addr_mask;

    u32 sdt_reg_sel_size:3;
#define IPR_SDT_REG_SEL_SIZE_NONE                    0x4
#define IPR_SDT_REG_SEL_SIZE_1NIBBLE                 0x2
#define IPR_SDT_REG_SEL_SIZE_1BYTE                   0x1
    u32 cpu_rst_support:2;
#define IPR_CPU_RST_SUPPORT_NONE                     0x1
#define IPR_CPU_RST_SUPPORT_CFGSPC_403RST_BIT        0x2
    u32 fixups_required:3;
#define IPR_FIXUPS_NONE                              0x0
#define IPR_ENDIAN_SWAP_FIXUP                        0x1
    u32 bar_size_reg:1;
    u32 set_mode_page_20:1;
#define IPR_IGNORE_MODE_PAGE_20                      0x0
#define IPR_SET_MODE_PAGE_20                         0x1
    u32 reserved_flags:22;

    u32 cl_size_latency_timer;
};

struct ipr_sense_buffer
{
    u8 byte[IPR_SENSE_BUFFERSIZE];
};

struct ipr_sdt_header
{
    u32  dump_state;
    u32  num_entries;
    u32  num_entries_used;
    u32  dump_size;
};

struct ipr_sdt
{
    struct ipr_sdt_header sdt_header;
    struct ipr_sdt_entry sdt_entry[IPR_NUM_SDT_ENTRIES];
};

struct ipr_uc_sdt
{
    struct ipr_sdt_header sdt_header;
    struct ipr_sdt_entry sdt_entry[1];
};

struct ipr_dump_header
{
    u32 total_length;
    u32 num_elems;
    u32 first_entry_offset;
    u32 status;
};

struct ipr_dump_entry_header
{
    u32 length;  /* MUST be the first member of the structure */
    u32 id;
#define IPR_DUMP_IOA_DUMP_ID 2
#define IPR_DUMP_TEXT_ID     3
#define IPR_DUMP_TRACE_ID    4
};

struct ipr_dump_location_entry
{
    struct ipr_dump_entry_header header;
    u8 location[IPR_MAX_LOCATION_LEN];
};

struct ipr_dump_trace_entry
{
    struct ipr_dump_entry_header header;
    u32 trace[IPR_DUMP_TRACE_ENTRY_SIZE/sizeof(u32)];
};

struct ipr_dump_driver_header
{
    struct ipr_dump_header header;
    struct ipr_dump_location_entry location_entry;
    struct ipr_dump_trace_entry trace_entry;
};

enum ipr_irq_state
{
    IPR_IRQ_ENABLED = 0,
    IPR_IRQ_DISABLED = 1
};

struct ipr_byte_pat_6
{
    u32 pat1;
    u16 pat2;
};

struct ipr_byte_pat_8
{
    u32 pat1;
    u32 pat2;
};

struct ipr_byte_pat_10
{
    u32 pat1;
    u32 pat2;
    u16 pat3;
};

struct ipr_byte_pat_16
{
    u32        pat1;
    u32        pat2;
    u32        pat3;
    u32        pat4;
};

struct ipr_byte_pat_32
{
    struct ipr_byte_pat_16 pat1;
    struct ipr_byte_pat_16 pat2;
};

#endif
