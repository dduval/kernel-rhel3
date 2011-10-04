/*****************************************************************************/
/* iprlibtypes.h -- driver for IBM Power Linux RAID adapters                 */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/lib/iprlibtypes.h,v 1.2 2003/10/24 20:52:18 bjking1 Exp $
 */

#ifndef iprlibtypes_h
#define iprlibtypes_h

#if (defined(__KERNEL__) && defined(__LITTLE_ENDIAN)) || \
(!defined(__KERNEL__) && (__BYTE_ORDER == __LITTLE_ENDIAN))
#ifndef iprlibtypesle_h
#include "iprlibtypesle.h"
#endif
#elif (defined(__KERNEL__) && defined(__BIG_ENDIAN)) || \
(!defined(__KERNEL__) && (__BYTE_ORDER == __BIG_ENDIAN))
#ifndef iprlibtypesbe_h
#include "iprlibtypesbe.h"
#endif
#else
#error "Neither __LITTLE_ENDIAN nor __BIG_ENDIAN defined"
#endif

struct ipr_error_int_decode_t {
    u32 interrupt;
    char *p_error;
};

struct ipr_error_rc_decode_t {
    u32 rc;
    char *p_error;
};

struct ipr_config_table{
    u8 num_entries;
    u8 reserved1;
    u16 reserved2;
    struct ipr_config_table_entry dev[IPR_MAX_PHYSICAL_DEVS];
};

struct ipr_hostrcb_cfg_ch_not_bin
{
    struct ipr_config_table_entry cfgte;
    u8 reserved[936];
};

struct ipr_ssd_header
{
    u16 data_length;
    u8 reserved;
    u8 num_records;
};

struct ipr_supported_device
{
    struct ipr_std_inq_vpids vpids;
    u8  ebcdic_as400_device_type[4];
    u8  ebcdic_as400_rctt;
    u8  ebcdic_space;
    u8  modifier_flags[2];
    u8  ebcdic_spaces[8];
};

struct ipr_dasd_timeout_record
{
    u8 op_code;
    u8 reserved;
    u16 timeout; /* Timeout in seconds */
};

/* IOA Request Control Block    128 bytes  */
struct ipr_ioarcb
{
    u32 ioarcb_host_pci_addr;
    u32 reserved;
    u32 ioa_res_handle;
    u32 host_response_handle;
    u32 reserved1;
    u32 reserved2;
    u32 reserved3;

    u32 write_data_transfer_length;
    u32 read_data_transfer_length;
    u32 write_ioadl_addr;
    u32 write_ioadl_len;
    u32 read_ioadl_addr;
    u32 read_ioadl_len;

    u32 ioasa_host_pci_addr;
    u16 ioasa_len;   
    u16 reserved4;

    struct ipr_cmd_pkt ioarcb_cmd_pkt;

    u32 add_cmd_parms_len;
    u32 add_cmd_parms[10];
};

/* 8 bytes */
struct ipr_ioadl_desc {
    u32 flags_and_data_len;
#define IPR_IOADL_FLAGS_MASK                         0xff000000
#define IPR_IOADL_DATA_LEN_MASK                      0x00ffffff
#define IPR_IOADL_FLAGS_HOST_READ_BUF                0x48000000
#define IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA      0x49000000
#define IPR_IOADL_FLAGS_HOST_WR_BUF                  0x68000000
#define IPR_IOADL_FLAGS_HOST_WR_LAST_DATA            0x69000000

    u32 address;
};

/* 128 bytes */
struct ipr_ioasa
{
    u32 ioasc;
#define IPR_IOASC_SENSE_KEY(ioasc) ((ioasc) >> 24)
#define IPR_IOASC_SENSE_CODE(ioasc) (((ioasc) & 0x00ff0000) >> 16)
#define IPR_IOASC_SENSE_QUAL(ioasc) (((ioasc) & 0x0000ff00) >> 8)
#define IPR_IOASC_SENSE_STATUS(ioasc) ((ioasc) & 0x000000ff)

    u16 ret_stat_len;           /* Length of the returned IOASA */

    u16 avail_stat_len;         /* Total Length of status available. */

    u32 residual_data_len;      /* number of bytes in the host data */
    /* buffers that were not used by the IOARCB command. */

    u32 ilid;
#define IPR_NO_ILID             0x00000000u 

    u32 fd_ioasc;

    u32 fd_phys_locator;

    u32 fd_res_handle;

    u32 ioasc_specific;      /* status code specific field */
#define IPR_IOASC_SPECIFIC_MASK  0x00ffffff
#define IPR_FIELD_POINTER_VALID  (0x80000000 >> 8)
#define IPR_FIELD_POINTER_MASK   0x0000ffff
    u32 failing_lba_hi;
    u32 failing_lba_lo;
    u32 reserved[22];
};

/* Before changing this, make sure that the IOADL is 8 byte aligned */
/* IOARCB and IOASA must be 4 byte aligned */
struct ipr_host_ioarcb {                     /* 32 bit */            /* 64 bit */
    struct ipr_ioarcb ioarcb;                /* 128 bytes */         /* 128 bytes */
    struct ipr_ioadl_desc *p_ioadl;          /* 4 bytes */           /* 8 bytes */
    struct ipr_ioasa *p_ioasa;               /* 4 bytes */           /* 8 bytes */
    struct ipr_host_ioarcb *p_next;          /* 4 bytes */           /* 8 bytes */
    struct ipr_host_ioarcb *p_prev;          /* 4 bytes */           /* 8 bytes */
    struct ipr_ccb *p_sis_cmd;               /* 4 bytes */           /* 8 bytes */
    void *reserved;                             /* 4 bytes */           /* 8 bytes */
    u32 host_ioarcb_index;                      /* 4 bytes */           /* 4 bytes */
    ipr_dma_addr ioarcb_dma;                 /* 4 bytes */           /* 4 bytes */
    ipr_dma_addr ioadl_dma;                  /* 4 bytes */           /* 4 bytes */
    ipr_dma_addr ioasa_dma;                  /* 4 bytes */           /* 4 bytes */
};                                              /* 168 bytes */         /* 192 bytes */

struct ipr_host_ioarcb_alloc {
    struct ipr_host_ioarcb host_ioarcb;
    struct ipr_ioadl_desc ioadl[IPR_NUM_IOADL_ENTRIES];
    struct ipr_ioasa ioasa;
};

struct ipr_interrupts
{
    unsigned long set_interrupt_mask_reg;
    unsigned long clr_interrupt_mask_reg;
    unsigned long sense_interrupt_mask_reg;
    unsigned long clr_interrupt_reg;

    unsigned long sense_interrupt_reg;
    unsigned long ioarrin_reg;
    unsigned long sense_uproc_interrupt_reg;
    unsigned long set_uproc_interrupt_reg;
    unsigned long clr_uproc_interrupt_reg;
};

struct ipr_interrupt_table_t
{
    u16 vendor_id;
    u16 device_id;
    struct ipr_interrupts regs;
};

struct ipr_ioa_parms_t
{
    u16 vendor_id;
    u16 device_id;
    u16 subsystem_id;
    u16 scsi_id_changeable:1;
    u16 reserverd:15;
    u32 max_bus_speed_limit; /* MB/sec limit for this IOA. Should be 0 if no limit */
};

struct ipr_data
{
    char eye_catcher[16];
#define IPR_DATA_EYE_CATCHER         "sis_cfg"

    char cfg_table_start[8];
#define IPR_DATA_CFG_TBL_START       "cfg"
    struct ipr_config_table *p_config_table;

#define IPR_NUM_TRACE_INDEX_BITS     9
#define IPR_NUM_TRACE_ENTRIES        (1 << IPR_NUM_TRACE_INDEX_BITS)
    char trace_start[8];
#define IPR_DATA_TRACE_START         "trace"
    struct ipr_internal_trace_entry *trace;
    u32 trace_index:IPR_NUM_TRACE_INDEX_BITS;
    u32 reserved_index:(32-IPR_NUM_TRACE_INDEX_BITS);
    ipr_dma_addr config_table_dma;

    char free_start[8];
#define IPR_FREEQ_START              "free"
    struct ipr_host_ioarcb *qFreeH;
    struct ipr_host_ioarcb *qFreeT;

    char pendq_start[8];
#define IPR_PENDQ_START              "pend"
    struct ipr_host_ioarcb *qPendingH;
    struct ipr_host_ioarcb *qPendingT;

    volatile u32 *host_rrq_start_addr;
    volatile u32 *host_rrq_end_addr;
    volatile u32 *host_rrq_curr_ptr;
    volatile u32 toggle_bit;
    u32 reserved_pad;

    char hrrq_label[8];
#define IPR_HRRQ_LABEL               "hrrq"
    u32 *host_rrq;
    ipr_dma_addr host_rrq_dma;
#define IPR_HRRQ_REQ_RESP_HANDLE_MASK 0xfffffffc
#define IPR_HRRQ_RESP_BIT_SET         0x00000002
#define IPR_HRRQ_TOGGLE_BIT           0x00000001
#define IPR_HRRQ_REQ_RESP_HANDLE_SHIFT 2
    u32 non15k_ses;

    struct ipr_dasd_init_bufs *p_dasd_init_buf[IPR_NUM_CFG_CHG_HCAMS];
    ipr_dma_addr dasd_init_buf_dma[IPR_NUM_CFG_CHG_HCAMS];

    struct ipr_dasd_init_bufs *free_init_buf_head;

    struct ipr_ssd_header *p_ssd_header;
    ipr_dma_addr ssd_header_dma;
    u32 reserved_pad2;

    const struct ipr_ioa_parms_t *p_ioa_cfg;

    struct ipr_interrupts regs;      /* 32 bytes */

    char ioarcb_label[8];
#define IPR_IOARCB_LABEL             "ioarcbs"
    struct ipr_host_ioarcb *host_ioarcb_list[IPR_NUM_CMD_BLKS];
    ipr_dma_addr host_ioarcb_list_dma[IPR_NUM_CMD_BLKS];
};

struct ipr_backplane_table_entry
{
    char product_id[17];
    char compare_product_id_byte[16];
    u32 max_bus_speed_limit; /* MB/sec limit for this backplane */
    u32 block_15k_devices:1;
    u32 reserved:31;
};

#endif
