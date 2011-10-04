/*****************************************************************************/
/* iprlits.h -- driver for IBM Power Linux RAID adapters                     */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/iprlits.h,v 1.3.2.2 2003/11/10 19:19:51 bjking1 Exp $
 */

#ifndef iprlits_h
#define iprlits_h

/******************************************************************/
/* Literals                                                       */
/******************************************************************/

#define IPR_DISABLE_RESET_RELOAD  0

#ifdef IPR_DEBUG_ALL

#define IPR_INL
#ifndef IPR_DEBUG
#define IPR_DEBUG                2
#endif
#ifndef IPR_DBG_TRACE
#define IPR_DBG_TRACE            1
#endif
#ifndef IPR_MEMORY_DEBUG
#define IPR_MEMORY_DEBUG         1
#endif

#else

#define IPR_INL                  inline
#ifndef IPR_DEBUG
#define IPR_DEBUG                0
#endif
#ifndef IPR_DBG_TRACE
#define IPR_DBG_TRACE            0
#endif
#ifndef IPR_MEMORY_DEBUG
#define IPR_MEMORY_DEBUG         0
#endif

#endif

#ifndef PCI_VENDOR_ID_IBM
#define PCI_VENDOR_ID_IBM           0x1014
#endif

#ifndef PCI_DEVICE_ID_IBM_SNIPE
#define PCI_DEVICE_ID_IBM_SNIPE     0x0180
#endif

#ifndef PCI_DEVICE_ID_GEMSTONE
#define PCI_DEVICE_ID_GEMSTONE      0xB166
#endif

#ifndef PCI_VENDOR_ID_MYLEX
#define PCI_VENDOR_ID_MYLEX         0x1069
#endif

#define IPR_PCIX_COMMAND_REG_ID              0x07
#define IPR_PCIX_CMD_DATA_PARITY_RECOVER     0x0001
#define IPR_PCIX_CMD_RELAXED_ORDERING        0x0002

#define IPR_SUBS_DEV_ID_2780 0x0264
#define IPR_SUBS_DEV_ID_5702 0x0266
#define IPR_SUBS_DEV_ID_5703 0x0278

#define IPR_NAME                 "ipr"
#define IPR_NAME_LEN             3
#define IPR_ERR                  "ipr-err"
#define IPR_ERR_LEN              7

#ifdef CONFIG_SMP
#define IPR_FULL_VERSION IPR_VERSION_STR" SMP"
#else
#define IPR_FULL_VERSION IPR_VERSION_STR" UP"
#endif

/******************************************************************/
/* Return codes                                                   */
/******************************************************************/
#define IPR_RC_SUCCESS                 0
#define IPR_RC_DID_RESET               0xffff0000
#define IPR_RC_UNKNOWN                 0xfefefeff
#define IPR_RC_FAILED                  0xffffffff
#define IPR_RC_TIMEOUT                 0x04080100
#define IPR_RC_NOMEM                   0x00000001
#define IPR_RC_QUAL_SUCCESS            0x00000002
#define IPR_RC_ABORTED                 0x00000003
#define IPR_RC_OP_NOT_SENT             0xff000001
#define IPR_RC_XFER_FAILED             0xf0000001
#define IPR_NO_HRRQ                    0xf0000002
#define IPR_IOARRIN_LOST               0xf0000003
#define IPR_MMIO_ERROR                 0xf0000004
#define IPR_IOA_UNIT_CHECKED           0xf0000005
#define IPR_403_ERR_STATE              0xf0000006
#define IPR_SPURIOUS_INT               0xf0000007
#define IPR_RESET_ADAPTER              0xf0000008
#define IPR_RC_QUAL_SUCCESS_SHUTDOWN   0x00808000

/******************************************************************/
/* IOASCs                                                         */
/******************************************************************/
#define IPR_IOASC_RCV_RECOMMEND_REALLOC   0x01180500
#define IPR_IOASC_SYNC_REQUIRED           0x023f0000
#define IPR_IOASC_NR_IOA_MICROCODE        0x02408500
#define IPR_IOASC_MED_RECOMMEND_REALLOC   0x03110B00
#define IPR_IOASC_MED_DO_NOT_REALLOC      0x03110C00
#define IPR_IOASC_HW_SEL_TIMEOUT          0x04050000
#define IPR_IOASC_TIME_OUT                0x04080100
#define IPR_IOASC_BUS_WAS_RESET           0x06290000
#define IPR_IOASC_BUS_WAS_RESET_BY_OTHER  0x06298000

#define IPR_NUM_LOG_HCAMS                 2
#define IPR_NUM_CFG_CHG_HCAMS             2
#define IPR_NUM_HCAMS   (IPR_NUM_LOG_HCAMS + IPR_NUM_CFG_CHG_HCAMS)
#define IPR_MAX_NUM_TARGETS_PER_BUS     0x10
#define IPR_MAX_NUM_LUNS_PER_TARGET     8

/* IPR_MAX_CMD_PER_LUN MUST be < 8. If more than 8 commands are queued to a
 tape or optical device, the IOA will unit check. This includes any ERP commands that
 might be sent to the device, like cancel, cancel all, or device reset. */
#define IPR_MAX_CMD_PER_LUN            6

#define IPR_MAX_CMD_PER_VSET           64

/* We need resources for HCAMS, IOA shutdown, and ERP */
#define IPR_NUM_INTERNAL_CMD_BLKS   (IPR_NUM_HCAMS + 2 + 4)

/* This is the number of command blocks we allocate for the mid-layer to use */
#define IPR_NUM_BASE_CMD_BLKS       100

/* This is maximum number of IOCTLs we can have at any one time */
#define IPR_NUM_IOCTL_CMD_BLKS      (24 + 1)

#define IPR_MAX_COMMANDS            IPR_NUM_BASE_CMD_BLKS
#define IPR_NUM_CMD_BLKS            (IPR_NUM_BASE_CMD_BLKS + \
                                        IPR_NUM_INTERNAL_CMD_BLKS +\
                                        IPR_NUM_IOCTL_CMD_BLKS)

#define IPR_MAX_BUS_TO_SCAN             255
#define IPR_MAX_NUM_BUSES               4

#define IPR_MAX_PHYSICAL_DEVS           192

#define IPR_MAX_SGLIST                  64
#define IPR_MAX_SECTORS                 512

#define IPR_DEFAULT_MAX_BUS_SPEED       320

#define IPR_IOA_RESOURCE_HANDLE         0xffffffff
#define IPR_IOA_RESOURCE_ADDRESS        0x00ffffff
#define IPR_IOA_RES_ADDR_BUS            0xff
#define IPR_IOA_RES_ADDR_TARGET         0xff
#define IPR_IOA_RES_ADDR_LUN            0xff
#define IPR_IOA_DEFAULT_MODEL           1

#define IPR_MAX_WRITE_BUFFER_SIZE       (4 * 1024 * 1024)

#define IPR_NUM_RESET_RELOAD_RETRIES    3

#define IPR_MAX_LOCATION_LEN            64
#define IPR_MAX_PSERIES_LOCATION_LEN    48

#define IPR_QAC_BUFFER_SIZE             16000
#define IPR_IOCTL_SEND_COMMAND          0xf1f1

/******************************************************************/
/* SIS Commands                                                   */
/******************************************************************/
#define IPR_TEST_UNIT_READY                  0x00u
#define IPR_REQUEST_SENSE                    0x03u
#define IPR_FORMAT_UNIT                      0x04u
#define IPR_REASSIGN_BLOCKS                  0x07u
#define IPR_INQUIRY                          0x12u
#define IPR_MODE_SELECT                      0x15u
#define IPR_MODE_SENSE                       0x1Au
#define IPR_START_STOP                       0x1Bu
#define  IPR_START_STOP_START                0x01u
#define  IPR_START_STOP_STOP                 0x00u
#define IPR_RECEIVE_DIAGNOSTIC               0x1Cu
#define IPR_SEND_DIAGNOSTIC                  0x1Du
#define IPR_READ_CAPACITY                    0x25u
#define IPR_READ_6                           0x08u
#define IPR_READ_10                          0x28u
#define IPR_READ_16                          0x88u
#define IPR_WRITE_6                          0x0Au
#define IPR_WRITE_10                         0x2Au
#define IPR_WRITE_16                         0x8Au
#define IPR_WRITE_VERIFY                     0x2Eu
#define IPR_WRITE_VERIFY_16                  0x8Eu
#define IPR_VERIFY                           0x2Fu
#define IPR_WRITE_BUFFER                     0x3Bu
#define  IPR_WR_BUF_DOWNLOAD_AND_SAVE        0x05u
#define IPR_INVALID_RESH                     0x25u
#define IPR_SYNC_REQUIRED                    0x3Fu
#define IPR_WRITE_SAME                       0x41u
#define IPR_LOG_SENSE                        0x4Du
#define IPR_SERVICE_ACTION_IN                0x9Eu
#define  IPR_READ_CAPACITY_16                0x10u
#define IPR_REPORT_LUNS                      0xA0u
#define IPR_CANCEL_REQUEST                   0xC0u
#define IPR_SYNC_COMPLETE                    0xC1u
#define IPR_QUERY_RESOURCE_STATE             0xC2u
#define IPR_RESET_DEVICE                     0xC3u
#define IPR_QUERY_IOA_CONFIG                 0xC5u

#define IPR_SUSPEND_DEV_BUS                  0xC8u
#define  IPR_SDB_CHECK_AND_QUIESCE           0x00u
#define  IPR_SDB_CHECK_ONLY                  0x40u
#define  IPR_SDB_QUIESE_ONLY                 0x80u
#define  IPR_SDB_SLEEP_TIME                  15

#define IPR_CONC_MAINT                       0xC8u
#define  IPR_CONC_MAINT_CHECK_AND_QUIESCE    IPR_SDB_CHECK_AND_QUIESCE
#define  IPR_CONC_MAINT_CHECK_ONLY           IPR_SDB_CHECK_ONLY
#define  IPR_CONC_MAINT_QUIESE_ONLY          IPR_SDB_QUIESE_ONLY

#define  IPR_CONC_MAINT_FMT_MASK             0x0Fu
#define  IPR_CONC_MAINT_FMT_SHIFT            0
#define  IPR_CONC_MAINT_GET_FMT(fmt) \
((fmt & IPR_CONC_MAINT_FMT_MASK) >> IPR_CONC_MAINT_FMT_SHIFT)
#define  IPR_CONC_MAINT_DSA_FMT              0x00u
#define  IPR_CONC_MAINT_FRAME_ID_FMT         0x01u
#define  IPR_CONC_MAINT_PSERIES_FMT          0x02u
#define  IPR_CONC_MAINT_XSERIES_FMT          0x03u

#define  IPR_CONC_MAINT_TYPE_MASK            0x30u
#define  IPR_CONC_MAINT_TYPE_SHIFT           4
#define  IPR_CONC_MAINT_GET_TYPE(type) \
((type & IPR_CONC_MAINT_TYPE_MASK) >> IPR_CONC_MAINT_TYPE_SHIFT)
#define  IPR_CONC_MAINT_INSERT               0x0u
#define  IPR_CONC_MAINT_REMOVE               0x1u

#define IPR_RESUME_DEVICE_BUS                0xC9u
#define IPR_QUERY_COMMAND_STATUS             0xCBu
#define IPR_CANCEL_ALL_REQUESTS              0xCEu
#define IPR_HOST_CONTROLLED_ASYNC            0xCFu
#define IPR_EVALUATE_DEVICE                  0xE4u
#define IPR_ZERO_UNIT                        0xEDu
#define IPR_QUERY_ARRAY_CONFIG               0xF0u
#define IPR_START_ARRAY_PROTECTION           0xF1u
#define IPR_STOP_ARRAY_PROTECTION            0xF2u
#define IPR_RESYNC_ARRAY_PROTECTION          0xF3u
#define IPR_ADD_ARRAY_DEVICE                 0xF4u
#define IPR_REMOVE_ARRAY_DEVICE              0xF5u
#define IPR_REBUILD_DEVICE_DATA              0xF6u
#define IPR_IOA_SHUTDOWN                     0xF7u
#define IPR_RECLAIM_CACHE_STORE              0xF8u
#define  IPR_RECLAIM_ACTION                  0x60u
#define  IPR_RECLAIM_PERFORM                 0x00u
#define  IPR_RECLAIM_EXTENDED_INFO           0x10u
#define  IPR_RECLAIM_QUERY                   0x20u
#define  IPR_RECLAIM_RESET                   0x40u
#define  IPR_RECLAIM_FORCE_BATTERY_ERROR     0x60u
#define  IPR_RECLAIM_UNKNOWN_PERM            0x80u
#define IPR_DISCARD_CACHE_DATA               0xF9u
#define  IPR_PROHIBIT_CORR_INFO_UPDATE       0x80u

#define IPR_IOA_DEBUG                        0xDDu
#define   IPR_IOA_DEBUG_READ_IOA_MEM         0x00u
#define   IPR_IOA_DEBUG_WRITE_IOA_MEM        0x01u
#define   IPR_IOA_DEBUG_READ_FLIT            0x03u
#define IPR_IOA_DEBUG_MAX_XFER               (16*1024*1024)
#define IPR_ZERO_ARRAY_DEVICE_DATA           0xFFu
#define IPR_INVALID_ARRAY_ID                 0xFFu

/******************************************************************/
/* Driver Commands                                                */
/******************************************************************/
#define IPR_GET_TRACE                        0xE1u
#define IPR_RESET_DEV_CHANGED                0xE8u
#define IPR_DUMP_IOA                         0xD7u
#define IPR_MODE_SENSE_PAGE_28               0xD8u
#define IPR_MODE_SELECT_PAGE_28              0xD9u
#define IPR_RESET_HOST_ADAPTER               0xDAu
#define IPR_READ_DRIVER_CFG                  0xDBu
#define IPR_WRITE_DRIVER_CFG                 0xDCu

/******************************************************************/
/* Timeouts                                                       */
/******************************************************************/

#define IPR_TIMEOUT_MULTIPLIER               2
#define IPR_MAX_SIS_TIMEOUT                  0x3fff            /* 4.5 hours */
#define IPR_SHUTDOWN_TIMEOUT                 (10 * 60 * HZ)    /* 10 minutes */
#define IPR_ABBREV_SHUTDOWN_TIMEOUT          (10 * HZ)         /* 10 seconds */
#define IPR_DEVICE_RESET_TIMEOUT             (2 * 60 * HZ)     /* 2 minutes */
#define IPR_CANCEL_TIMEOUT                   (3 * 60 * HZ)     /* 3 minutes */
#define IPR_CANCEL_ALL_TIMEOUT               (2 * 60 * HZ)     /* 2 minutes */
#define IPR_INTERNAL_TIMEOUT                 (30 * HZ)         /* 30 seconds */
#define IPR_INTERNAL_DEV_TIMEOUT             (2 * 60 * HZ)     /* 2 minutes */
#define IPR_SUSPEND_DEV_BUS_TIMEOUT          (35 * HZ)         /* 35 seconds */
#define IPR_RECLAIM_TIMEOUT                  (10 * 60 * HZ)    /* 10 minutes */
#define IPR_FORMAT_UNIT_TIMEOUT              (4 * 60 * 60 * HZ)  /* 4 hours */
#define IPR_WRITE_BUFFER_TIMEOUT             (10 * 60 * HZ)    /* 10 minutes */
#define IPR_ARRAY_CMD_TIMEOUT                (2 * 60 * HZ)     /* 2 minutes */
#define IPR_QUERY_CMD_STAT_TIMEOUT           (30 * HZ)         /* 30 seconds */
#define IPR_DISCARD_CACHE_DATA_TIMEOUT       (30 * HZ)         /* 30 seconds */
#define IPR_EVALUATE_DEVICE_TIMEOUT          (2 * 60 * HZ)     /* 2 minutes */
#define IPR_START_STOP_TIMEOUT               IPR_SHUTDOWN_TIMEOUT

/******************************************************************/
/* SES Related Literals                                           */
/******************************************************************/
#define IPR_DRIVE_ELEM_STATUS_STATUS_EMPTY           5
#define IPR_DRIVE_ELEM_STATUS_STATUS_POPULATED       1
#define IPR_NUM_DRIVE_ELEM_STATUS_ENTRIES            16
#define IPR_MAX_NUM_ELEM_DESCRIPTORS                 8
#define IPR_GLOBAL_DESC_12BYTES                      0xC
#define IPR_GLOBAL_DESC_13BYTES                      0xD


/******************************************************************/
/* SIS Literals                                                   */
/******************************************************************/
#define  IPR_PERI_TYPE_DISK            0x00u
#define  IPR_PERI_TYPE_SES             0x0Du

#define  IPR_IS_DASD_DEVICE(std_inq_data) \
    ((((std_inq_data).peri_dev_type) == IPR_PERI_TYPE_DISK) && !((std_inq_data).removeable_medium))

#define  IPR_IS_SES_DEVICE(std_inq_data) \
    (((std_inq_data).peri_dev_type) == IPR_PERI_TYPE_SES)

#define IPR_HOST_RCB_OP_CODE_CONFIG_CHANGE        (u8)0xE1
#define IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE        (u8)0x01
#define IPR_HOST_RCB_OP_CODE_LOG_DATA             (u8)0xE2
#define IPR_HCAM_CDB_OP_CODE_LOG_DATA             (u8)0x02

#define IPR_HOST_RCB_NOTIF_TYPE_EXISTING_CHANGED  (u8)0x00
#define IPR_HOST_RCB_NOTIF_TYPE_NEW_ENTRY         (u8)0x01
#define IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY         (u8)0x02
#define IPR_HOST_RCB_NOTIF_TYPE_ERROR_LOG_ENTRY   (u8)0x10
#define IPR_HOST_RCB_NOTIF_TYPE_INFORMATION_ENTRY (u8)0x11

#define IPR_HOST_RCB_NO_NOTIFICATIONS_LOST        (u8)0
#define IPR_HOST_RCB_NOTIFICATIONS_LOST           BIT0OF8

#define IPR_HOST_RCB_OVERLAY_ID_1                 (u8)0x01
#define IPR_HOST_RCB_OVERLAY_ID_2                 (u8)0x02
#define IPR_HOST_RCB_OVERLAY_ID_3                 (u8)0x03
#define IPR_HOST_RCB_OVERLAY_ID_4                 (u8)0x04
#define IPR_HOST_RCB_OVERLAY_ID_6                 (u8)0x06
#define IPR_HOST_RCB_OVERLAY_ID_DEFAULT           (u8)0xFF

#define IPR_NO_REDUCTION             0
#define IPR_HALF_REDUCTION           1
#define IPR_QUARTER_REDUCTION        2
#define IPR_EIGHTH_REDUCTION         4
#define IPR_SIXTEENTH_REDUCTION      6
#define IPR_UNKNOWN_REDUCTION        7

#define IPR_RECLAIM_NUM_BLOCKS_MULTIPLIER    256

#define IPR_VENDOR_ID_LEN            8
#define IPR_PROD_ID_LEN              16
#define IPR_SERIAL_NUM_LEN           8

#define IPR_MAX_NUM_SUPP_INQ_PAGES   8
#define IPR_SUBTYPE_AF_DASD          0x0
#define IPR_SUBTYPE_GENERIC_SCSI     0x1
#define IPR_SUBTYPE_VOLUME_SET       0x2

#define IPR_HOST_SPARE_MODEL         90
#define IPR_VSET_MODEL_NUMBER        200

#define IPR_SENSE_BUFFERSIZE         64

#define IPR_SCSI_SENSE_INFO          0

/******************************************************************/
/* Hardware literals                                              */
/******************************************************************/
#define IPR_CHUKAR_MAILBOX_OFFSET            0x18D78
#define IPR_SNIPE_MAILBOX_OFFSET             0x0052C
#define IPR_GEMSTONE_MAILBOX_OFFSET          0x0042C
#define IPR_RESET_403_OFFSET                 0x44
#define IPR_RESET_403                        0x00000001
#define IPR_CHUKAR_MBX_ADDR_MASK             0x00ffffff
#define IPR_CHUKAR_MBX_BAR_SEL_MASK          0xff000000
#define IPR_CHUKAR_MKR_BAR_SEL_SHIFT         24
#define IPR_FMT2_MBX_ADDR_MASK               0x0fffffff
#define IPR_FMT2_MBX_BAR_SEL_MASK            0xf0000000
#define IPR_FMT2_MKR_BAR_SEL_SHIFT           28
#define IPR_SDT_FMT1_BAR0_SEL                0x10
#define IPR_SDT_FMT1_BAR1_SEL                0x14
#define IPR_SDT_FMT1_BAR2_SEL                0x18
#define IPR_SDT_FMT1_BAR3_SEL                0x1C
#define IPR_SDT_FMT1_BAR4_SEL                0x30
#define IPR_SDT_FMT2_BAR0_SEL                0x0
#define IPR_SDT_FMT2_BAR1_SEL                0x1
#define IPR_SDT_FMT2_BAR2_SEL                0x2
#define IPR_SDT_FMT2_BAR3_SEL                0x3
#define IPR_SDT_FMT2_BAR4_SEL                0x4
#define IPR_FMT1_SDT_READY_TO_USE            0xC4D4E3C2
#define IPR_FMT2_SDT_READY_TO_USE            0xC4D4E3F2
#define IPR_BAR0_ADDRESS_BITS                0xfffe0000

/******************************************************************/
/* Dump literals                                                  */
/******************************************************************/
#define IPR_DUMP_TRACE_ENTRY_SIZE            8192
#define IPR_MIN_DUMP_SIZE                    (1 * 1024 * 1024)
#define IPR_MAX_IOA_DUMP_SIZE                (4 * 1024 * 1024)
#define IPR_MAX_DUMP_FETCH_TIME              (30 * HZ)
#define IPR_NUM_SDT_ENTRIES                  511

/******************************************************************/
/* Misc literals                                                  */
/******************************************************************/
#define IPR_EOL                              "\n"
#define SHUTDOWN_SIGS                           (sigmask(SIGTERM))

#define IPR_ALLOC_ATOMIC            0x00000001
#define IPR_ALLOC_CAN_SLEEP         0x00000002

#define IPR_ALIGNED_2               0x00000001
#define IPR_ALIGNED_4               0x00000003
#define IPR_ALIGNED_8               0x00000007
#define IPR_ALIGNED_16              0x0000000F

#define IPR_BYTE_ALIGN_2            0x00000001
#define IPR_BYTE_ALIGN_4            0x00000002
#define IPR_BYTE_ALIGN_8            0x00000004
#define IPR_BYTE_ALIGN_16           0x00000008

#define IPR_COPY_1                  0x00000001
#define IPR_COPY_2                  0x00000002
#define IPR_COPY_4                  0x00000004
#define IPR_COPY_8                  0x00000008
#define IPR_COPY_16                 0x00000010
#define IPR_COPY_32                 0x00000020

#define IPR_COMPARE_1               IPR_COPY_1
#define IPR_COMPARE_2               IPR_COPY_2
#define IPR_COMPARE_4               IPR_COPY_4
#define IPR_COMPARE_8               IPR_COPY_8
#define IPR_COMPARE_16              IPR_COPY_16
#define IPR_COMPARE_32              IPR_COPY_32

#define IPR_PAGE_CODE_MASK          0x3F
#define IPR_PAGE_CODE_28            0x28
#define IPR_MODE_SENSE_28_SZ        0xff
#define IPR_CURRENT_PAGE            0x0
#define IPR_CHANGEABLE_PAGE         0x1
#define IPR_DEFAULT_PAGE            0x2

#define IPR_STD_INQ_Z0_TERM_LEN      8
#define IPR_STD_INQ_Z1_TERM_LEN      12
#define IPR_STD_INQ_Z2_TERM_LEN      4
#define IPR_STD_INQ_Z3_TERM_LEN      5
#define IPR_STD_INQ_Z4_TERM_LEN      4
#define IPR_STD_INQ_Z5_TERM_LEN      2
#define IPR_STD_INQ_Z6_TERM_LEN      10
#define IPR_STD_INQ_PART_NUM_LEN     12
#define IPR_STD_INQ_EC_LEVEL_LEN     10
#define IPR_STD_INQ_FRU_NUM_LEN      12

#define IPR_DEFECT_LIST_HDR_LEN      4
#define IPR_FORMAT_IMMED             2
#define IPR_FORMAT_DATA              0x10u

#define IPR_ARCH_GENERIC             0
#define IPR_ARCH_ISERIES             1
#define IPR_ARCH_PSERIES             2

#endif
