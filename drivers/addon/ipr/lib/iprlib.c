/*****************************************************************************/
/* iprlib.c -- driver for IBM Power Linux RAID adapters                      */
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
 * $Header: /afs/rchland.ibm.com/usr8/ibmsis/devel/ipr/ipr/src/lib/iprlib.c,v 1.5.2.1 2003/10/27 20:05:00 bjking1 Exp $
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include <linux/wait.h>
#include <linux/tqueue.h>
#include <linux/spinlock.h>
#include <linux/pci_ids.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/ctype.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/semaphore.h>
#include <asm/page.h>
#ifdef CONFIG_KDB
#include <asm/kdb.h>
#endif

#ifndef iprliblits_h
#include "iprliblits.h"
#endif

#ifndef iprlibtypes_h
#include "iprlibtypes.h"
#endif

#ifndef iprlib_h
#include "iprlib.h"
#endif

extern const int ipr_arch;

static const int ipr_debug = IPR_DEBUG;

static const
struct ipr_error_int_decode_t ipr_error_int_decode [] = {
    {IPR_PCII_IOARCB_XFER_FAILED,    "IOARCB transfer failed"},
    {IPR_PCII_IOA_UNIT_CHECKED,      "IOA Unit checked"},
    {IPR_PCII_NO_HOST_RRQ,           "No Host Request/Response queue"},
    {IPR_PCII_IOARRIN_LOST,          "IOARRIN lost"},
    {IPR_PCII_MMIO_ERROR,            "MMIO error"},
    {IPR_PCII_PROC_ERR_STATE,        "Processor in error state"},
    {0xffffffff,                        "Unknown error"}
};

static const
struct ipr_error_rc_decode_t ipr_error_rc_decode [] = {
    {IPR_RC_FAILED,          "Op failed"},
    {IPR_RC_TIMEOUT,         "Timeout"},
    {IPR_RC_XFER_FAILED,     "IOARCB transfer failed"},
    {IPR_IOA_UNIT_CHECKED,   "IOA Unit checked"},
    {IPR_NO_HRRQ,            "No Host Request/Response queue"},
    {IPR_IOARRIN_LOST,       "IOARRIN lost"},
    {IPR_MMIO_ERROR,         "MMIO error"},
    {IPR_403_ERR_STATE,      "Processor in error state"},
    {IPR_RESET_ADAPTER,      "Adapter reset requested"},
    {IPR_RC_UNKNOWN,         "Unknown error"}
};

/*  A constant array of Supported Device Entries */
#define IPR_NUM_NON15K_DASD 9
#define IPR_NUM_15K_DASD    7
static const struct
{
    struct ipr_supported_device dev_non15k[IPR_NUM_NON15K_DASD];
    struct ipr_supported_device dev_15k[IPR_NUM_15K_DASD];
} ipr_supported_dev_list = 
{
    {
        { /* Starfire Fast/Wide 4G - DFHSS4W - 6607 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'F', 'H', 'S', 'S', '4', 'W', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF6, 0xF0, 0xF7}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Scorpion Fast/Wide 8G - DCHS09W - 6713 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'C', 'H', 'S', '0', '9', 'W', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF7, 0xF1, 0xF3}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Marlin Fast/Wide 17G - DGHS18U - 6714 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'G', 'H', 'S', '1', '8', 'U', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF7, 0xF1, 0xF4}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Thresher 8G - DGVS09U - 6717 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'G', 'V', 'S', '0', '9', 'U', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF7, 0xF1, 0xF7}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Solid State DASD 1 GB - QUANTUM SSQD107 - 6730 */
            {{'Q', 'U', 'A', 'N', 'T', 'U', 'M', ' '},
            {'S', 'S', 'Q', 'D', '1', '0', '7', ' ', ' ', ' ', ' ', ' ', ' ', '(', 'C', ')'}},
            {0xF6, 0xF7, 0xF3, 0xF0}, 0xF0, 0x40, {0x80, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Solid State DASD 1.6 GB - QUANTUM SSQD160 - 6731 */
            {{'Q', 'U', 'A', 'N', 'T', 'U', 'M', ' '},
            {'S', 'S', 'Q', 'D', '1', '6', '0', ' ', ' ', ' ', ' ', ' ', ' ', '(', 'C', ')'}},
            {0xF6, 0xF7, 0xF3, 0xF1}, 0xF0, 0x40, {0x80, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Hammerhead DASD 18 GB - DRVS18D - 6718 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'R', 'V', 'S', '1', '8', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF7, 0xF1, 0xF8}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Discovery 36 GB, 10k RPM - DDYS36M - 6719 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'D', 'D', 'Y', 'S', '3', '6', 'M', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF6, 0xF7, 0xF1, 0xF9}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Daytona 70 GB, 10k RPM - UCD2070 - 4320 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'U', 'C', 'D', '2', '0', '7', '0', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF0}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },
    },
    {
        { /* Piranha 18 GB, 15k RPM 160 MB/s - UCPR018 - 4322 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'U', 'C', 'P', 'R', '0', '1', '8', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF2}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Piranha 18 GB, 15k RPM 320 MB/s - XCPR018 - 4325 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'X', 'C', 'P', 'R', '0', '1', '8', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF5}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        }, 

        { /* Piranha 35 GB, 15k RPM 160 MB/s - UCPR036 - 4323 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'U', 'C', 'P', 'R', '0', '3', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF3}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        }, 

        { /* Piranha 35 GB, 15k RPM 320 MB/s - XCPR036 - 4326 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'X', 'C', 'P', 'R', '0', '3', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF6}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Monza 70 GB, 15k RPM 320 MB/s - XCPR073 - 4327 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'X', 'C', 'P', 'R', '0', '7', '3', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF7}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Monza 141 GB, 15k RPM 320 MB/s - XCPR146 - 4328 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'X', 'C', 'P', 'R', '1', '4', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF8}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        },

        { /* Monaco 282 GB, 15k RPM 320 MB/s - XCPR282 - 4329 */
            {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
            {'X', 'C', 'P', 'R', '2', '8', '2', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
            {0xF4, 0xF3, 0xF2, 0xF9}, 0xF1, 0x40, {0x00, 0x00},
            {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40}
        }

        /* IMPORTANT NOTE :
         IF A NEW DEVICE IS TO BE SUPPORTED, MAKE SURE IT IS ADDED TO
         THE RIGHT POSITION WITHIN THE ABOVE LIST.
         IF IT IS NOT A 15K DRIVE, INSERT IT IN FRONT OF THE FIRST 15K DEVICE
         (18G PIRANHA).
         IF THE NEW DEVICE IS A 15K DRIVE, APPEND IT TO THE BOTTOM OF THE LIST.
         AND INCREMENT THE FOLLOWING CONSTANTS: IPR_NUM_15K_DASD */
    }
};

struct ipr_supported_device *ipr_supported_dev_list_ptr =
(struct ipr_supported_device *)&ipr_supported_dev_list;

struct ipr_dev_config
{
    struct ipr_std_inq_vpids vpids;
    void (*set_page0x00)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_vendor_unique_page *p_ch,
                         struct ipr_vendor_unique_page *p_buf);

    void (*set_page0x01)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_rw_err_mode_page *p_ch,
                         struct ipr_rw_err_mode_page *p_buf);

    void (*set_page0x02)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_disc_reconn_page *p_ch,
                         struct ipr_disc_reconn_page *p_buf);

    void (*set_page0x07)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_verify_err_rec_page *p_ch,
                         struct ipr_verify_err_rec_page *p_buf);

    void (*set_page0x08)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_caching_page *p_ch,
                         struct ipr_caching_page *p_buf);

    void (*set_page0x0a)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_control_mode_page *p_ch,
                         struct ipr_control_mode_page *p_buf);

    void (*set_page0x20)(struct ipr_resource_entry *p_resource_entry,
                         struct ipr_ioa_dasd_page_20 *p_buf);

    u8 is_15k_device:1;
    u8 enable_qas:1;
    u8 reserved:6;
    u8 reserved2;
    u16 vpd_len;
};

/*  A constant array of DASD Timeouts */
static const
struct ipr_dasd_timeout_record ipr_dasd_timeout_list[] =
{
    {IPR_TEST_UNIT_READY,    0x00, _i16(30)},
    {IPR_REQUEST_SENSE,      0x00, _i16(30)},
    {IPR_INQUIRY,            0x00, _i16(30)},
    {IPR_MODE_SELECT,        0x00, _i16(30)},
    {IPR_MODE_SENSE,         0x00, _i16(30)},
    {IPR_READ_CAPACITY,      0x00, _i16(30)},
    {IPR_READ_10,            0x00, _i16(30)},
    {IPR_WRITE_10,           0x00, _i16(30)},
    {IPR_WRITE_VERIFY,       0x00, _i16(30)},
    {IPR_FORMAT_UNIT,        0x00, _i16(7200)},  /* 2 Hours */
    {IPR_REASSIGN_BLOCKS,    0x00, _i16(600)},   /* 10 minutes */
    {IPR_START_STOP,         0x00, _i16(120)},
    {IPR_SEND_DIAGNOSTIC,    0x00, _i16(300)},   /* 5 minutes */
    {IPR_VERIFY,             0x00, _i16(300)},   /* 5 minutes */
    {IPR_WRITE_BUFFER,       0x00, _i16(300)},   /* 5 minutes */
    {IPR_WRITE_SAME,         0x00, _i16(14400)}, /* 4 hours */
    {IPR_LOG_SENSE,          0x00, _i16(30)},
    {IPR_REPORT_LUNS,        0x00, _i16(30)},
    {IPR_SKIP_READ,          0x00, _i16(30)},
    {IPR_SKIP_WRITE,         0x00, _i16(30)}
};

struct ipr_dasd_timeouts
{
    u32 length;
    struct ipr_dasd_timeout_record
        record[sizeof(ipr_dasd_timeout_list) / sizeof(struct ipr_dasd_timeout_record)];
};

struct ipr_dasd_init_bufs
{
    struct ipr_dasd_timeouts dasd_timeouts;
    ipr_dma_addr dasd_timeouts_dma;
    struct ipr_mode_parm_hdr mode_pages;
    char pad1[255 - sizeof(struct ipr_mode_parm_hdr)];
    ipr_dma_addr mode_pages_dma;
    struct ipr_mode_parm_hdr changeable_parms;
    char pad2[255 - sizeof(struct ipr_mode_parm_hdr)];
    ipr_dma_addr changeable_parms_dma;
    struct ipr_dasd_inquiry_page3 page3_inq;
    ipr_dma_addr page3_inq_dma;
    struct ipr_std_inq_data_long std_inq;
    ipr_dma_addr std_inq_dma;
    struct ipr_ssd_header ssd_header;
    struct ipr_supported_device supported_device;
    ipr_dma_addr ssd_header_dma;
    struct ipr_query_res_state res_query;
    ipr_dma_addr res_query_dma;
    struct ipr_dasd_init_bufs *p_next;
    struct ipr_hostrcb *p_hostrcb;
    struct ipr_resource_entry *p_dev_res;
};

/**********************************************************************/
/*                                                                    */
/* this  array is used to translate ebcdic to ascii.  the ebcdic char */
/* is used as an index into the array, and the value at that index is */
/* the  ascii representation of  that  character.  a  SP  (space)  is */
/* returned if there is no translation for the given character.       */
/*                                                                    */
/**********************************************************************/
static const char
ipr_etoa[] = {
    0x00, 0x01, 0x02, 0x03, 0x20, 0x09, 0x20, 0x7f,       /* 00-07 done */
    /*NULL,SOH,  STX, ETX,  N/A,  HT,   N/A,  DEL                       */
    0x20, 0x20, 0x20, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,       /* 08-0f done */
    /*N/A,N/A,  N/A,  VT,   FF,   CR,   SO,   SI                        */
    0x10, 0x11, 0x12, 0x13, 0x20, 0x20, 0x08, 0x20,       /* 10-17 done */
    /*DLE,DC1,  DC2,  DC3,  N/A,  N/A,  BS,   N/A                       */
    0x18, 0x19, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 18-1f done */
    /*CAN,  EM, N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x0a, 0x17, 0x1b,       /* 20-27 done */
    /*N/A,N/A,  N/A,  N/A,  N/A,  LF,   ETB,  ESC                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x05, 0x06, 0x07,       /* 28-2f done */
    /*N/A,N/A,  N/A,  N/A,  N/A,  ENQ,  ACK,  BEL                       */
    0x20, 0x20, 0x16, 0x20, 0x20, 0x20, 0x20, 0x04,       /* 30-37 done */
    /*N/A,N/A,  SYN,  N/A,  N/A,  N/A,  N/A,  EOT                       */
    0x20, 0x20, 0x20, 0x20, 0x14, 0x15, 0x20, 0x1a,       /* 38-3f done */
    /*N/A,N/A,  N/A,  N/A,  DC4,  NAK,  N/A,  SUB                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 40-47 done */
    /*SP, N/A,  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x20, 0x2e, 0x3c, 0x28, 0x2b, 0x20,       /* 48-4f done */
    /*N/A,N/A,  N/A,  ".",  "<",  "(",  "+",  N/A                       */
    0x26, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 50-57 done */
    /*"&",N/A,  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e,       /* 58-5f done */
    /*N/A,N/A,  "!",  "$",  "*",  ")",  ";",  "^"                       */
    0x2d, 0x2f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 60-67 done */
    /*"-","/",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x7c, 0x2c, 0x25, 0x5f, 0x3e, 0x3f,       /* 68-6f done */
    /*N/A,N/A,  "|",  ",",  "%",  "_",  ">",  "?"                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 70-77 done */
    /*N/A,N/A,  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22,       /* 78-7f done */
    /*N/A,"`",  ":",  "#",  "@",  "'",  "=",  """                       */
    0x20, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,       /* 80-87 done */
    /*N/A,"a",  "b",  "c",  "d",  "e",  "f",  "g"                       */
    0x68, 0x69, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 88-8f done */
    /*"h","i",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,       /* 90-97 done */
    /*N/A,"j",  "k",  "l",  "m",  "n",  "o",  "p"                       */
    0x71, 0x72, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* 98-9f done */
    /*"q","r",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x7e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,       /* a0-a7 done */
    /*N/A,"~",  "s",  "t",  "u",  "v",  "w",  "x"                       */
    0x79, 0x7a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* a8-af done */
    /*"y","z",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* b0-b7 done */
    /*N/A,N/A,  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* b8-bf done */
    /*N/A,N/A,  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x7b, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,       /* c0-c7 done */
    /*"{","A",  "B",  "C",  "D",  "E",  "F",  "G"                       */
    0x48, 0x49, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* c8-cf done */
    /*"H","I",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x7d, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,       /* d0-d7 done */
    /*"}","J",  "K",  "L",  "M",  "N",  "O",  "P"                       */
    0x51, 0x52, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* d8-df done */
    /*"Q","R",  N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x5c, 0x20, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,       /* e0-e7 done */
    /*"\",N/A,  "S",  "T",  "U",  "V",  "W",  "X"                       */
    0x59, 0x5a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* e8-ef done */
    /*"Y", "Z", N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,       /* f0-f7 done */
    /*"0", "1", "2",  "3",  "4",  "5",  "6",  "7"                       */
    0x38, 0x39, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,       /* f8-ff done */
    /*"8", "9", N/A,  N/A,  N/A,  N/A,  N/A,  N/A                       */
};

static const struct ipr_interrupt_table_t ipr_interrupt_table [] =
{
    {PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_SNIPE,
    {0x00288, 0x0028C, 0x00288, 0x00284, 0x00280, 0x00504, 0x00290, 0x00290, 0x00294}},

    {PCI_VENDOR_ID_MYLEX, PCI_DEVICE_ID_GEMSTONE,
    {0x0022C, 0x00230, 0x0022C, 0x00228, 0x00224, 0x00404, 0x00214, 0x00214, 0x00218}}
};

/* This structure is used to correlate operational parameters based on PCI Vendor/Device/Subsystem ID */
static const
struct ipr_ioa_parms_t ipr_ioa_parms[] =
{
    /* Gemstone based IOAs */
    {
        vendor_id: PCI_VENDOR_ID_MYLEX, device_id: PCI_DEVICE_ID_GEMSTONE,
        subsystem_id: IPR_SUBS_DEV_ID_5702,
        scsi_id_changeable:1, max_bus_speed_limit: 320
    },
    {
        vendor_id: PCI_VENDOR_ID_MYLEX, device_id: PCI_DEVICE_ID_GEMSTONE,
        subsystem_id: IPR_SUBS_DEV_ID_5703,
        scsi_id_changeable:0, max_bus_speed_limit: 320
    },
    /* Snipe based IOAs */
    {
        vendor_id: PCI_VENDOR_ID_IBM, device_id: PCI_DEVICE_ID_IBM_SNIPE,
        subsystem_id: IPR_SUBS_DEV_ID_2780,
        scsi_id_changeable:0, max_bus_speed_limit: 320
    }
};

static u32 ipr_poll_isr(struct ipr_shared_config *p_shared_cfg, u32 timeout);
static u32 ipr_send_blocking_cmd(struct ipr_shared_config *p_shared_cfg,
                                    u8 sis_cmd,
                                    u32 timeout,
                                    u8 parm,
                                    void *p_data,
                                    ipr_dma_addr dma_addr,
                                    u32 xfer_len);
static int ipr_set_mode_page28(struct ipr_shared_config *p_shared_cfg);
static int ipr_build_slot_map(struct ipr_shared_config *p_shared_cfg);
static int ipr_build_slot_map_runtime(struct ipr_shared_config *p_shared_cfg,
                                         struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_hostrcb *p_hostrcb);
static void ipr_gen_sense(struct ipr_resource_entry *p_resource,
                             u8 *p_sense_buffer, struct ipr_ioasa *p_ioasa);
static void ipr_dump_ioasa(struct ipr_shared_config *p_shared_cfg,
                              struct ipr_resource_entry *p_resource,
                              struct ipr_ioasa *p_ioasa);
static u32 ipr_get_model(struct ipr_config_table_entry *p_cfgte);
static void ipr_update_resource(struct ipr_shared_config *p_shared_cfg,
                                   struct ipr_resource_entry *p_resource_entry,
                                   struct ipr_config_table_entry *p_cfgte,
                                   u32 device_changed);
static struct ipr_resource_dll *ipr_get_resource_entry(struct ipr_shared_config *p_shared_cfg);
static void ipr_put_resource_entry(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_resource_dll *p_resource_dll);
static void ipr_log_config_error(struct ipr_shared_config *p_shared_cfg,
                                    int is_ipl, struct ipr_resource_entry *p_resource);
static void ipr_print_dev(struct ipr_shared_config *p_shared_cfg,
                             struct ipr_resource_entry *p_resource);
static u32 ipr_init_devices(struct ipr_shared_config *p_shared_cfg);
static u32 ipr_init_single_dev(struct ipr_shared_config *p_shared_cfg,
                                  struct ipr_resource_entry *p_resource_entry);
static void *ipr_get_mode_page(struct ipr_mode_parm_hdr *p_mode_parm, u32 page_code, u32 len);
static int ipr_blocking_dasd_cmd(struct ipr_shared_config *p_shared_cfg,
                                    struct ipr_resource_entry *p_resource_entry,
                                    ipr_dma_addr dma_buffer,
                                    u8 cmd, u8 parm, u16 alloc_len);
static int ipr_blocking_vset_cmd(struct ipr_shared_config *p_shared_cfg,
                                    struct ipr_resource_entry *p_resource_entry,
                                    ipr_dma_addr dma_buffer,
                                    u8 cmd, u8 parm, u16 alloc_len);
static int ipr_dasd_req(struct ipr_shared_config *p_shared_cfg,
                           struct ipr_resource_entry *p_resource_entry,
                           struct ipr_dasd_init_bufs *p_dasd_init_buf,
                           void *p_buffer,
                           ipr_dma_addr dma_addr,
                           u8 cmd, u8 parm, u16 alloc_len, u8 job_step);
static int ipr_vset_req(struct ipr_shared_config *p_shared_cfg,
                           struct ipr_resource_entry *p_resource_entry,
                           struct ipr_dasd_init_bufs *p_dasd_init_buf,
                           void *p_buffer,
                           ipr_dma_addr dma_addr,
                           u8 cmd, u8 parm, u16 alloc_len, u8 job_step);
static int ipr_ioa_req(struct ipr_shared_config *p_shared_cfg,
                          void (*done) (struct ipr_shared_config *, struct ipr_ccb *),
                          void *p_scratch,
                          void *p_buffer,
                          ipr_dma_addr dma_addr,
                          u8 cmd, u8 parm, u16 alloc_len, u8 job_step);
static void ipr_ebcdic_to_ascii(const u8 *ebcdic, u32 length,  u8 *ascii);
static struct ipr_host_ioarcb* ipr_build_ioa_cmd(struct ipr_shared_config *p_shared_cfg,
                                                       u8 sis_cmd,
                                                       struct ipr_ccb *p_sis_cmd,
                                                       u8 parm,
                                                       void *p_data,
                                                       ipr_dma_addr dma_addr,
                                                       u32 xfer_len);
static const struct ipr_dev_config *ipr_get_dev_config(struct ipr_resource_entry *p_resource_entry);
static void ipr_set_page0x00(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_vendor_unique_page *p_ch,
                                struct ipr_vendor_unique_page *p_buf);
static void ipr_set_page0x00_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_vendor_unique_page *p_ch,
                                         struct ipr_vendor_unique_page *p_buf);
static void ipr_set_page0x00_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_vendor_unique_page *p_ch,
                                      struct ipr_vendor_unique_page *p_buf);
static void ipr_set_page0x00_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_vendor_unique_page *p_ch,
                                    struct ipr_vendor_unique_page *p_buf);
static void ipr_set_page0x01(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_rw_err_mode_page *p_ch,
                                struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_rw_err_mode_page *p_ch,
                                         struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_rw_err_mode_page *p_ch,
                                      struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_rw_err_mode_page *p_ch,
                                    struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_rw_err_mode_page *p_ch,
                                             struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_rw_err_mode_page *p_ch,
                                                struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_discovery_36Gb(struct ipr_resource_entry *p_resource_entry,
                                               struct ipr_rw_err_mode_page *p_ch,
                                               struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x01_daytona_70Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_rw_err_mode_page *p_ch,
                                             struct ipr_rw_err_mode_page *p_buf);
static void ipr_set_page0x02(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_disc_reconn_page *p_ch,
                                struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_disc_reconn_page *p_ch,
                                         struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_disc_reconn_page *p_ch,
                                      struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_noop(struct ipr_resource_entry *p_resource_entry,
                                     struct ipr_disc_reconn_page *p_ch,
                                     struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_starfire_4Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_scorpion_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_marlin_17Gb(struct ipr_resource_entry *p_resource_entry,
                                            struct ipr_disc_reconn_page *p_ch,
                                            struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x02_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_disc_reconn_page *p_ch,
                                                struct ipr_disc_reconn_page *p_buf);
static void ipr_set_page0x07(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_verify_err_rec_page *p_ch,
                                struct ipr_verify_err_rec_page *p_buf);
static void ipr_set_page0x07_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_verify_err_rec_page *p_ch,
                                         struct ipr_verify_err_rec_page *p_buf);
static void ipr_set_page0x07_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_verify_err_rec_page *p_ch,
                                      struct ipr_verify_err_rec_page *p_buf);
static void ipr_set_page0x08(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_caching_page *p_ch,
                                struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_caching_page *p_ch,
                                         struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_caching_page *p_ch,
                                      struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_caching_page *p_ch,
                                    struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_scorpion_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_marlin_17Gb(struct ipr_resource_entry *p_resource_entry,
                                            struct ipr_caching_page *p_ch,
                                            struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_caching_page *p_ch,
                                                struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_discovery_36Gb(struct ipr_resource_entry *p_resource_entry,
                                               struct ipr_caching_page *p_ch,
                                               struct ipr_caching_page *p_buf);
static void ipr_set_page0x08_daytona_70Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf);
static void ipr_set_page0x0a(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_control_mode_page *p_ch,
                                struct ipr_control_mode_page *p_buf);
static void ipr_set_page0x0a_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_control_mode_page *p_ch,
                                         struct ipr_control_mode_page *p_buf);
static void ipr_set_page0x0a_noTCQ(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_control_mode_page *p_ch,
                                      struct ipr_control_mode_page *p_buf);
static void ipr_set_page0x0a_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_control_mode_page *p_ch,
                                      struct ipr_control_mode_page *p_buf);
static void ipr_set_page0x20_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_ioa_dasd_page_20 *p_buf);
static void ipr_set_page0x20_noTCQ(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_ioa_dasd_page_20 *p_buf);
static void ipr_set_page0x20(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_ioa_dasd_page_20 *p_buf);
static u8 ipr_set_mode_pages(struct ipr_shared_config *p_shared_cfg,
                                struct ipr_resource_entry *p_resource_entry,
                                struct ipr_mode_parm_hdr *p_mode_parm,
                                struct ipr_mode_parm_hdr *p_changeable_pages);
static u32 ipr_init_single_dev_runtime(struct ipr_shared_config *p_shared_cfg,
                                          struct ipr_resource_entry *p_resource_entry,
                                          u32 is_ndn,
                                          struct ipr_hostrcb *p_hostrcb);
static void ipr_dasd_init_job(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_ccb *p_sis_ccb);
static void ipr_vset_init_job(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_ccb *p_sis_ccb);
static void ipr_bus_init_job(struct ipr_shared_config *p_shared_cfg,
                                struct ipr_ccb *p_sis_ccb);
static struct ipr_dasd_init_bufs *ipr_get_dasd_init_buffer(struct ipr_data *ipr_cfg);

static void ipr_put_dasd_init_buffer(struct ipr_data *ipr_cfg,
                                        struct ipr_dasd_init_bufs
                                        *p_dasd_init_buffer);

static void ipr_check_backplane(struct ipr_shared_config *p_shared_cfg,
                                   struct ipr_config_table_entry *cfgte);

#ifdef IPR_DEBUG_MODE_PAGES
static void ipr_print_mode_sense_buffer(struct ipr_mode_parm_hdr *p_mode_parm);
#endif

static const struct ipr_dev_config ipr_dev_cfg_table[] =
{
    { /* Starfire 4 Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'F', 'H', 'S', 'S', '4', 'W', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}}, 
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_as400,
        set_page0x02: ipr_set_page0x02_starfire_4Gb,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_as400,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Scorpion 8Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'C', 'H', 'S', '0', '9', 'W', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_as400,
        set_page0x02: ipr_set_page0x02_scorpion_8Gb,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_scorpion_8Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Thresher 8 Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'G', 'V', 'S', '0', '9', 'U', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_thresher_8Gb,
        set_page0x02: ipr_set_page0x02_thresher_8Gb,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_thresher_8Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Marlin 17 Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'G', 'H', 'S', '1', '8', 'U', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_as400,
        set_page0x02: ipr_set_page0x02_marlin_17Gb,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_marlin_17Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Hammerhead 18 Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'R', 'V', 'S', '1', '8', 'D', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_hammerhead_18Gb,
        set_page0x02: ipr_set_page0x02_hammerhead_18Gb,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_hammerhead_18Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Discovery 36 Gb */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'D', 'D', 'Y', 'S', '3', '6', 'M', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_discovery_36Gb,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_discovery_36Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Daytona 70 GB, 10k RPM */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'U', 'C', 'D', '2', '0', '7', '0', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_daytona_70Gb,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_daytona_70Gb,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Piranha 18 GB, 15k RPM 160 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'U', 'C', 'P', 'R', '0', '1', '8', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Piranha 18 GB, 15k RPM 320 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'X', 'C', 'P', 'R', '0', '1', '8', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        enable_qas:    1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Piranha 35 GB, 15k RPM 160 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'U', 'C', 'P', 'R', '0', '3', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Piranha 35 GB, 15k RPM 320 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'X', 'C', 'P', 'R', '0', '3', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        enable_qas:    1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },

    { /* Monza 70 GB, 15k RPM 320 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'X', 'C', 'P', 'R', '0', '7', '3', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        enable_qas:    1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Monza 141 GB, 15k RPM 320 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'X', 'C', 'P', 'R', '1', '4', '6', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        enable_qas:    1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Monaco 282 GB, 15k RPM 320 MB/s */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {'X', 'C', 'P', 'R', '2', '8', '2', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_TCQ,
        set_page0x01: ipr_set_page0x01_TCQ,
        set_page0x02: ipr_set_page0x02_noop,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_TCQ,
        set_page0x0a: ipr_set_page0x0a_as400,
        is_15k_device: 1,
        enable_qas:    1,
        vpd_len: sizeof(struct ipr_std_inq_vpids)
    },
    { /* Default AS/400 DASD */
        vpids: {{'I', 'B', 'M', 'A', 'S', '4', '0', '0'},
        {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '}},
        set_page0x00: ipr_set_page0x00_as400,
        set_page0x01: ipr_set_page0x01_as400,
        set_page0x02: ipr_set_page0x02_as400,
        set_page0x07: ipr_set_page0x07_as400,
        set_page0x08: ipr_set_page0x08_as400,
        set_page0x0a: ipr_set_page0x0a_noTCQ,
        set_page0x20: ipr_set_page0x20_noTCQ,
        vpd_len: IPR_VENDOR_ID_LEN
    }
};

static const struct ipr_backplane_table_entry ipr_backplane_table [] =
{
    {
        product_id: {"2104-DL1        "},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80              /* 80 MB/sec limit */
    },
    {
        product_id: {"2104-TL1        "},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80              /* 80 MB/sec limit */
    },
    /* Hidive 7 slot */
    {   /*                 H S B P      0 7 M       P   U 2     S C S I*/
        product_id: {"HSBP07M P U2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80,             /* 80 MB/sec limit */
        block_15k_devices:    1              /* block 15k devices */
    },
    /* Hidive 5 slot */
    {
        product_id: {"HSBP05M P U2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80,             /* 80 MB/sec limit */
        block_15k_devices:    1              /* block 15k devices */
    },
    /* Bowtie */
    {
        product_id: {"HSBP05M S U2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80,             /* 80 MB/sec limit */
        block_15k_devices:    1              /* block 15k devices */
    },
    /* MartinFenning */
    {
        product_id: {"HSBP06E ASU2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit: 80,             /* 80 MB/sec limit */
        block_15k_devices:    1              /* block 15k devices */
    },
    {
        product_id: {"2104-DU3        "},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"2104-TU3        "},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"HSBP04C RSU2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"HSBP06E RSU2SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"St  V1S2        "},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"HSBPD4M  PU3SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    },
    {
        product_id: {"VSBPD1H   U3SCSI"},
        compare_product_id_byte:{1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        max_bus_speed_limit:160             /* 160 MB/sec limit */
    }
};

/*---------------------------------------------------------------------------
 * Purpose: Adds a trace entry to the driver trace
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static IPR_INL
void ipr_trc_hook(struct ipr_shared_config *p_shared_cfg,
                     u8 op_code, u8 type, u8 device_type, u16 host_ioarcb_index,
                     u32 xfer_len, u32 add_data)
{
    struct ipr_internal_trace_entry *p_trace_entry;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    if (p_shared_cfg->trace)
    {
        p_trace_entry = &ipr_cfg->trace[ipr_cfg->trace_index++];
        p_trace_entry->time = jiffies;
        p_trace_entry->op_code = op_code;
        p_trace_entry->type = type;
        p_trace_entry->device_type = device_type;
        p_trace_entry->host_ioarcb_index = host_ioarcb_index;
        p_trace_entry->xfer_len = xfer_len;
        p_trace_entry->data.ioasc = add_data;
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Removes a host IOARCB from the pending queue
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static IPR_INL
void ipr_remove_host_ioarcb_from_pending(struct ipr_data *ipr_cfg,
                                            struct ipr_host_ioarcb* p_host_ioarcb)
{
    if ((p_host_ioarcb == ipr_cfg->qPendingH) &&
        (p_host_ioarcb == ipr_cfg->qPendingT))
    {
        ipr_cfg->qPendingH = ipr_cfg->qPendingT = NULL;
    }
    else if (p_host_ioarcb == ipr_cfg->qPendingH)
    {
        ipr_cfg->qPendingH = ipr_cfg->qPendingH->p_next;
        ipr_cfg->qPendingH->p_prev = NULL;
    }
    else if (p_host_ioarcb == ipr_cfg->qPendingT)
    {
        ipr_cfg->qPendingT = ipr_cfg->qPendingT->p_prev;
        ipr_cfg->qPendingT->p_next = NULL;
    }
    else
    {
        p_host_ioarcb->p_next->p_prev = p_host_ioarcb->p_prev;
        p_host_ioarcb->p_prev->p_next = p_host_ioarcb->p_next;
    }

    p_host_ioarcb->p_next = NULL;
    p_host_ioarcb->p_prev = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Puts a host ioarcb on the pending queue
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static IPR_INL
void ipr_put_host_ioarcb_to_pending(struct ipr_data *ipr_cfg,
                                       struct ipr_host_ioarcb* p_host_ioarcb)
{
    /* Put IOARCB on the pending list */
    if (ipr_cfg->qPendingT != NULL)
    {
        ipr_cfg->qPendingT->p_next = p_host_ioarcb;
        p_host_ioarcb->p_prev = ipr_cfg->qPendingT;
        ipr_cfg->qPendingT = p_host_ioarcb;
    }
    else
    {
        ipr_cfg->qPendingT = ipr_cfg->qPendingH = p_host_ioarcb;
        p_host_ioarcb->p_prev = NULL;
    }
    p_host_ioarcb->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Get a free host IOARCB and put it on the pending queue
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to host ioarcb
 * Notes: Cannot run out - will kernel panic if it does.
 *        Zeroes IOARCB, then initializes common fields
 *---------------------------------------------------------------------------*/
static IPR_INL
struct ipr_host_ioarcb* ipr_get_free_host_ioarcb(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_host_ioarcb* tmp_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioasa *p_ioasa;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    tmp_host_ioarcb = ipr_cfg->qFreeH;

    if (tmp_host_ioarcb == NULL)
        panic(IPR_ERR": Out of host_ioarcb's"IPR_EOL);

    ipr_cfg->qFreeH = ipr_cfg->qFreeH->p_next;

    if (ipr_cfg->qFreeH == NULL)
        ipr_cfg->qFreeT = NULL;
    else
        ipr_cfg->qFreeH->p_prev = NULL;

    p_ioarcb = &tmp_host_ioarcb->ioarcb;
    p_ioasa = tmp_host_ioarcb->p_ioasa;

    memset(&p_ioarcb->ioarcb_cmd_pkt, 0, sizeof(struct ipr_cmd_pkt));
    p_ioarcb->write_data_transfer_length = 0;
    p_ioarcb->read_data_transfer_length = 0;
    p_ioarcb->write_ioadl_addr = 0;
    p_ioarcb->write_ioadl_len = 0;
    p_ioarcb->read_ioadl_addr = 0;
    p_ioarcb->read_ioadl_len = 0;
    tmp_host_ioarcb->p_sis_cmd = NULL;

    p_ioasa->ioasc = 0;
    p_ioasa->residual_data_len = 0;

    ipr_put_host_ioarcb_to_pending(ipr_cfg, tmp_host_ioarcb);

    return tmp_host_ioarcb;
}

/*---------------------------------------------------------------------------
 * Purpose: Puts a host ioarcb on the free queue
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static IPR_INL
void ipr_put_host_ioarcb_to_free(struct ipr_data *ipr_cfg,
                                    struct ipr_host_ioarcb* p_host_ioarcb)
{
    /* Put IOARCB back on the free list */
    if(ipr_cfg->qFreeT != NULL)
    {
        ipr_cfg->qFreeT->p_next = p_host_ioarcb;
        p_host_ioarcb->p_prev = ipr_cfg->qFreeT;
        ipr_cfg->qFreeT = p_host_ioarcb;
    }
    else
    {
        ipr_cfg->qFreeH = ipr_cfg->qFreeT = p_host_ioarcb;
        p_host_ioarcb->p_prev = NULL;
    }
    p_host_ioarcb->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Builds an IOA data list
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/

static void ipr_build_ioadl(struct ipr_shared_config *p_shared_cfg,
                               struct ipr_host_ioarcb* p_host_ioarcb)
{
    struct ipr_ccb * p_sis_cmd;
    int i;
    struct ipr_ioadl_desc *p_ioadl;
    struct ipr_ioadl_desc *tmp_ioadl;
    struct ipr_sglist *p_tmp_sglist;
    struct ipr_ioarcb *p_ioarcb;
    u32 length;
    u32 ioadl_length;
    u32 ioadl_address;

    p_sis_cmd = p_host_ioarcb->p_sis_cmd;
    p_ioarcb = &p_host_ioarcb->ioarcb;
    p_ioadl = p_host_ioarcb->p_ioadl;
    length = p_sis_cmd->bufflen;

    if (p_sis_cmd->use_sg)
    {
        if (p_sis_cmd->data_direction == IPR_DATA_READ)
        {
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);

            tmp_ioadl = p_ioadl;

            for (i = 0, p_tmp_sglist = p_sis_cmd->sglist;
                 i < (p_sis_cmd->use_sg);
                 i++, tmp_ioadl++, p_tmp_sglist++)
            {
                ioadl_address = p_tmp_sglist->address;
                ioadl_length = p_tmp_sglist->length;

                tmp_ioadl->flags_and_data_len = 
                    htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF | ioadl_length);
                tmp_ioadl->address = htosis32(ioadl_address);
            }

            (tmp_ioadl - 1)->flags_and_data_len = 
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | ioadl_length);

            p_ioarcb->read_data_transfer_length = htosis32(length);
            p_ioarcb->read_ioadl_len =
                htosis32(sizeof(struct ipr_ioadl_desc) * p_sis_cmd->use_sg);

        }
        else if (p_sis_cmd->data_direction == IPR_DATA_WRITE)
        {
            p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);

            p_ioarcb->write_data_transfer_length = htosis32(length);
            p_ioarcb->write_ioadl_len =
                htosis32(sizeof(struct ipr_ioadl_desc) * p_sis_cmd->use_sg);
            tmp_ioadl = p_ioadl;

            for (i = 0, p_tmp_sglist = p_sis_cmd->sglist;
                 i < (p_sis_cmd->use_sg - 1);
                 i++, tmp_ioadl++, p_tmp_sglist++)
            {
                tmp_ioadl->flags_and_data_len = 
                    htosis32(IPR_IOADL_FLAGS_HOST_WR_BUF | p_tmp_sglist->length);
                tmp_ioadl->address = htosis32(p_tmp_sglist->address);
            }

            tmp_ioadl->flags_and_data_len = 
                htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | p_tmp_sglist->length);
            tmp_ioadl->address = htosis32(p_tmp_sglist->address);

            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;
        }
        else
        {
            panic(IPR_ERR": use_sg was set on a command, but data_direction was not "IPR_EOL);
        }
    }
    else
    { /* Not using scatter-gather */

        if ((p_sis_cmd->data_direction == IPR_DATA_READ) ||
            (p_sis_cmd->data_direction == IPR_DATA_WRITE))
        {
            panic("build_ioadl called without use_sg set"IPR_EOL);
        }
        else
        {
            /* No data to transfer */
        }
    }      
}

/*---------------------------------------------------------------------------
 * Purpose: Allocate memory for an adapter
 * Context: Task level only
 * Lock State: no locks assumed to be held
 * Returns: IPR_RC_NOMEM  - Out of memory
 *          IPR_RC_SUCCESS - Success
 *          IPR_RC_FAILED  - Failure
 *---------------------------------------------------------------------------*/
int ipr_alloc_mem (struct ipr_shared_config *p_shared_cfg)
{
    int i, j, rc;
    struct ipr_host_ioarcb *p_tmp_host_ioarcb;
    struct ipr_data *ipr_cfg;
    ipr_dma_addr dma_addr;
    struct ipr_host_ioarcb_alloc *p_host_ioarcb_alloc;
    int found = 0;
    struct ipr_dasd_init_bufs *p_dasd_init_buf;

    ENTER;

    /* Allocate a zeroed buffer for our main control block */
    ipr_cfg = (struct ipr_data *)ipr_kcalloc(sizeof(struct ipr_data),
                                                   IPR_ALLOC_CAN_SLEEP);
    p_shared_cfg->p_data = ipr_cfg;

    if (ipr_cfg == NULL)
    {
        ipr_trace;
        return IPR_RC_NOMEM;
    }

    /* Allocate a zeroed HRRQ buffer */
    ipr_cfg->host_rrq = ipr_dma_calloc(p_shared_cfg,
                                          sizeof(u32) * IPR_NUM_CMD_BLKS,
                                          &ipr_cfg->host_rrq_dma,
                                          IPR_ALLOC_CAN_SLEEP);

    if (ipr_cfg->host_rrq == NULL)
    {
        ipr_trace;
        rc = IPR_RC_NOMEM;
        goto leave_hrrq;
    }

    for (i = 0; i < IPR_NUM_CMD_BLKS; i++)
    {
        /* Allocate zeroed memory for an IOARCB/IOASA/IOADL command block */
        p_host_ioarcb_alloc =
            ipr_dma_calloc(p_shared_cfg,
                              sizeof(struct ipr_host_ioarcb_alloc),
                              &dma_addr, IPR_ALLOC_CAN_SLEEP);

        if (p_host_ioarcb_alloc == NULL)
        {
            ipr_trace;
            rc = IPR_RC_NOMEM;
            goto leave_ioarcbs;
        }

        p_tmp_host_ioarcb = (struct ipr_host_ioarcb *)p_host_ioarcb_alloc;
        ipr_cfg->host_ioarcb_list[i] = p_tmp_host_ioarcb;
        ipr_cfg->host_ioarcb_list_dma[i] = dma_addr;
        p_tmp_host_ioarcb->ioarcb_dma = dma_addr;

        p_tmp_host_ioarcb->p_ioadl = p_host_ioarcb_alloc->ioadl;
        p_tmp_host_ioarcb->ioadl_dma = dma_addr + sizeof(struct ipr_host_ioarcb);

        p_tmp_host_ioarcb->p_ioasa = &p_host_ioarcb_alloc->ioasa;
        p_tmp_host_ioarcb->ioasa_dma = p_tmp_host_ioarcb->ioadl_dma + sizeof(p_host_ioarcb_alloc->ioadl);
        p_tmp_host_ioarcb->ioarcb.ioasa_len = htosis16(sizeof(struct ipr_ioasa));

        p_tmp_host_ioarcb->host_ioarcb_index = i;
        p_tmp_host_ioarcb->ioarcb.host_response_handle = htosis32(i << 2);
        p_tmp_host_ioarcb->ioarcb.ioasa_host_pci_addr = htosis32((u32)p_tmp_host_ioarcb->ioasa_dma);
        p_tmp_host_ioarcb->ioarcb.ioarcb_host_pci_addr = htosis32(p_tmp_host_ioarcb->ioarcb_dma);
    }

    /* Allocate zeroed, DMA-able config table */
    ipr_cfg->p_config_table = ipr_dma_calloc(p_shared_cfg,
                                                sizeof(struct ipr_config_table),
                                                &ipr_cfg->config_table_dma,
                                                IPR_ALLOC_CAN_SLEEP);

    for (i = 0; i < IPR_NUM_CFG_CHG_HCAMS; i++)
    {
        /* Allocate a DMA buffer for the DASD init job */
        ipr_cfg->p_dasd_init_buf[i] = ipr_dma_calloc(p_shared_cfg,
                                                        sizeof(struct ipr_dasd_init_bufs),
                                                        &ipr_cfg->dasd_init_buf_dma[i],
                                                        IPR_ALLOC_CAN_SLEEP);

        if (ipr_cfg->p_dasd_init_buf[i] == NULL)
        {
            ipr_trace;
            rc = IPR_RC_NOMEM;
            goto leave_all;
        }
    }

    /* Allocate zeroed memory for the internal trace */
    ipr_cfg->trace = ipr_kcalloc(sizeof(struct ipr_internal_trace_entry) *
                                    IPR_NUM_TRACE_ENTRIES,
                                    IPR_ALLOC_CAN_SLEEP);

    if (ipr_cfg->trace == NULL)
    {
        ipr_trace;
        rc = IPR_RC_NOMEM;
        goto leave_all;
    }

    /* Allocate a DMA buffer for the Set Supported Devices table */
    ipr_cfg->p_ssd_header = ipr_dma_malloc(p_shared_cfg, sizeof(ipr_supported_dev_list) +
                                              sizeof(struct ipr_ssd_header),
                                              &ipr_cfg->ssd_header_dma,
                                              IPR_ALLOC_CAN_SLEEP);

    if ((ipr_cfg->p_config_table == NULL) || (ipr_cfg->p_ssd_header == NULL))
    {
        ipr_trace;
        rc = IPR_RC_NOMEM;
        goto leave_all;
    }

    ipr_cfg->trace_index = 0;

    ipr_cfg->free_init_buf_head = NULL;

    memcpy(ipr_cfg->p_ssd_header + 1,
           ipr_supported_dev_list_ptr, sizeof(ipr_supported_dev_list));

    ipr_cfg->p_ssd_header->data_length =
        htosis16(sizeof(ipr_supported_dev_list) + sizeof(struct ipr_ssd_header));
    ipr_cfg->p_ssd_header->reserved = 0;
    ipr_cfg->p_ssd_header->num_records =
        sizeof(ipr_supported_dev_list) / sizeof(struct ipr_supported_device);

    for (i = 0; i < IPR_NUM_CFG_CHG_HCAMS; i++)
    {
        p_dasd_init_buf = ipr_cfg->p_dasd_init_buf[i];

        /* Setup the DASD init DMA addresses */
        p_dasd_init_buf->dasd_timeouts_dma = ipr_cfg->dasd_init_buf_dma[i];
        p_dasd_init_buf->mode_pages_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->mode_pages - (unsigned long)p_dasd_init_buf);
        p_dasd_init_buf->changeable_parms_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->changeable_parms - (unsigned long)p_dasd_init_buf);
        p_dasd_init_buf->page3_inq_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->page3_inq - (unsigned long)p_dasd_init_buf);
        p_dasd_init_buf->std_inq_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->std_inq - (unsigned long)p_dasd_init_buf);
        p_dasd_init_buf->ssd_header_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->ssd_header - (unsigned long)p_dasd_init_buf);
        p_dasd_init_buf->res_query_dma = p_dasd_init_buf->dasd_timeouts_dma +
            ((unsigned long)&p_dasd_init_buf->res_query - (unsigned long)p_dasd_init_buf);

        ipr_put_dasd_init_buffer(ipr_cfg, p_dasd_init_buf);
    }

    /* Initialize register pointers */
    for (i = 0; i < (sizeof(ipr_interrupt_table)/sizeof(struct ipr_interrupt_table_t)); i++)
    {
        if ((ipr_interrupt_table[i].vendor_id == p_shared_cfg->vendor_id) &&
            (ipr_interrupt_table[i].device_id == p_shared_cfg->device_id))
        {
            ipr_cfg->regs = ipr_interrupt_table[i].regs;
            found = 1;
            break;
        }
    }

    if (found == 0)
    {
        rc = IPR_RC_FAILED;
        ipr_beg_err(KERN_ERR);
        ipr_log_err("Unknown IOA type: 0x%04X 0x%04X"IPR_EOL, p_shared_cfg->vendor_id,
                       p_shared_cfg->device_id);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
        goto leave_all;
    }

    ipr_cfg->regs.set_interrupt_mask_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.clr_interrupt_mask_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.sense_interrupt_mask_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.clr_interrupt_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.sense_interrupt_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.ioarrin_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.sense_uproc_interrupt_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.set_uproc_interrupt_reg += p_shared_cfg->hdw_dma_regs;
    ipr_cfg->regs.clr_uproc_interrupt_reg += p_shared_cfg->hdw_dma_regs;

    return IPR_RC_SUCCESS;

    leave_all:

        for (i = 0; i < IPR_NUM_CFG_CHG_HCAMS; i++)
        {
            ipr_dma_free(p_shared_cfg, sizeof(struct ipr_dasd_init_bufs),
                            ipr_cfg->p_dasd_init_buf[i],
                            ipr_cfg->dasd_init_buf_dma[i]);
        }

    ipr_dma_free(p_shared_cfg, sizeof(struct ipr_config_table),
                    ipr_cfg->p_config_table,
                    ipr_cfg->config_table_dma);

    ipr_dma_free(p_shared_cfg, sizeof(ipr_supported_dev_list) + sizeof(struct ipr_ssd_header),
                    ipr_cfg->p_ssd_header, ipr_cfg->ssd_header_dma);
    ipr_kfree(ipr_cfg->trace,
                 sizeof(struct ipr_internal_trace_entry) *
                 IPR_NUM_TRACE_ENTRIES);

    leave_ioarcbs:
        for (j=0; j < IPR_NUM_CMD_BLKS; j++)
        {
            ipr_dma_free(p_shared_cfg, sizeof(struct ipr_host_ioarcb_alloc),
                            ipr_cfg->host_ioarcb_list[j], ipr_cfg->host_ioarcb_list_dma[j]);
        }

    ipr_dma_free(p_shared_cfg, sizeof(u32) * IPR_NUM_CMD_BLKS,
                    ipr_cfg->host_rrq, ipr_cfg->host_rrq_dma);

    leave_hrrq:
        ipr_kfree(ipr_cfg, sizeof(struct ipr_data));

    LEAVE;

    return rc;

}

/*---------------------------------------------------------------------------
 * Purpose: Frees memory allocated by the binary
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *---------------------------------------------------------------------------*/
int ipr_free_mem (struct ipr_shared_config *p_shared_cfg)
{
    int i;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    for (i=0; i < IPR_NUM_CMD_BLKS; i++)
    {
        ipr_dma_free(p_shared_cfg, sizeof(struct ipr_host_ioarcb_alloc),
                        ipr_cfg->host_ioarcb_list[i], ipr_cfg->host_ioarcb_list_dma[i]);
    }

    for (i = 0; i < IPR_NUM_CFG_CHG_HCAMS; i++)
    {
        ipr_dma_free(p_shared_cfg, sizeof(struct ipr_dasd_init_bufs),
                        ipr_cfg->p_dasd_init_buf[i],
                        ipr_cfg->dasd_init_buf_dma[i]);
    }

    ipr_dma_free(p_shared_cfg, sizeof(u32) * IPR_NUM_CMD_BLKS,
                    ipr_cfg->host_rrq, ipr_cfg->host_rrq_dma);

    ipr_dma_free(p_shared_cfg, sizeof(struct ipr_config_table),
                    ipr_cfg->p_config_table,
                    ipr_cfg->config_table_dma);

    ipr_dma_free(p_shared_cfg, sizeof(ipr_supported_dev_list) + sizeof(struct ipr_ssd_header),
                    ipr_cfg->p_ssd_header, ipr_cfg->ssd_header_dma);

    ipr_kfree(ipr_cfg->trace,
                 sizeof(struct ipr_internal_trace_entry) *
                 IPR_NUM_TRACE_ENTRIES);
    ipr_kfree(ipr_cfg, sizeof(struct ipr_data));

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Part 1 of IOA initialization
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *          IPR_RC_FAILED            - Failed to write destruct. diag.
 *---------------------------------------------------------------------------*/
int ipr_init_ioa_internal_part1 (struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    int i;
    volatile u32 temp_reg;

    ENTER;

    /* Zero out the HRRQ */
    memset(ipr_cfg->host_rrq, 0, sizeof(u32)*IPR_NUM_CMD_BLKS);

    /* Setup our eyecatchers for debug */
    sprintf(ipr_cfg->eye_catcher, IPR_DATA_EYE_CATCHER);
    sprintf(ipr_cfg->cfg_table_start, IPR_DATA_CFG_TBL_START);
    sprintf(ipr_cfg->trace_start, IPR_DATA_TRACE_START);
    sprintf(ipr_cfg->free_start, IPR_FREEQ_START);
    sprintf(ipr_cfg->pendq_start, IPR_PENDQ_START);
    sprintf(ipr_cfg->hrrq_label, IPR_HRRQ_LABEL);
    sprintf(ipr_cfg->ioarcb_label, IPR_IOARCB_LABEL);

    /* Initialize Host RRQ pointers */
    ipr_cfg->host_rrq_start_addr = ipr_cfg->host_rrq;
    ipr_cfg->host_rrq_end_addr = &ipr_cfg->host_rrq[IPR_NUM_CMD_BLKS - 1];
    ipr_cfg->host_rrq_curr_ptr = ipr_cfg->host_rrq_start_addr;
    ipr_cfg->toggle_bit = 1;

    /* Zero queue pointers */
    ipr_cfg->qFreeH = NULL;
    ipr_cfg->qFreeT = NULL;
    ipr_cfg->qPendingH = NULL;
    ipr_cfg->qPendingT = NULL;

    /* Zero out config table */
    memset(ipr_cfg->p_config_table, 0, sizeof(struct ipr_config_table));

    /* Put all the host ioarcbs on the free list */
    for (i = 0; i < IPR_NUM_CMD_BLKS; i++)
    {
        ipr_put_host_ioarcb_to_free(ipr_cfg, ipr_cfg->host_ioarcb_list[i]);
        ipr_cfg->host_ioarcb_list[i]->p_sis_cmd = NULL;
    }

    /* Mask all interrupts to allow polling */
    p_shared_cfg->allow_interrupts = 0;
    writel(~0, ipr_cfg->regs.set_interrupt_mask_reg);
    temp_reg = readl(ipr_cfg->regs.sense_interrupt_mask_reg);

    /* initialize old ses status information for 15k dasd limitations */
    ipr_cfg->non15k_ses = 0;

    for (i = 0; i < sizeof(ipr_ioa_parms)/sizeof(struct ipr_ioa_parms_t); i++)
    {
        if ((p_shared_cfg->vendor_id == ipr_ioa_parms[i].vendor_id) &&
            (p_shared_cfg->device_id == ipr_ioa_parms[i].device_id) &&
            (p_shared_cfg->subsystem_id == ipr_ioa_parms[i].subsystem_id))
        {
            ipr_cfg->p_ioa_cfg = &ipr_ioa_parms[i];

            /* Enable destructive diagnostics on IOA */
            writel (IPR_DOORBELL, ipr_cfg->regs.set_uproc_interrupt_reg);

            LEAVE;
            return IPR_RC_SUCCESS;
        }
    }

    LEAVE;
    return IPR_RC_FAILED;
}

/*---------------------------------------------------------------------------
 * Purpose: Query whether or not IOA can be reset
 * Context: Task level or interrupt level.
 * Lock State: no locks assumed to be held
 * Returns: TRUE        - IOA can be reset
 *          FALSE       - IOA is in a critical operation
 *---------------------------------------------------------------------------*/
int ipr_reset_allowed(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    volatile u32 temp_reg;

    temp_reg = readl(ipr_cfg->regs.sense_interrupt_reg);

    return ((temp_reg & IPR_PCII_CRITICAL_OPERATION) == 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Alert the IOA of a pending reset
 * Context: Task level or interrupt level.
 * Lock State: no locks assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_reset_alert(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    writel(IPR_403I_RESET_ALERT, ipr_cfg->regs.set_uproc_interrupt_reg);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Part 2 of IOA initialization
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS       - Success
 *          IPR_RC_XFER_FAILED   - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ          - No HRRQ interrupt
 *          IPR_IOARRIN_LOST     - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR       - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED - IOA unit checked
 *          IPR_RC_FAILED        - Initialization failed
 *          IPR_RC_TIMEOUT       - IOA timed out
 *---------------------------------------------------------------------------*/
int ipr_init_ioa_internal_part2 (struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    volatile u32 temp_reg;
    u32 delay = 0;
    u32 rc = IPR_RC_SUCCESS;
    char dram_size[4];
    struct ipr_ioa_vpd *p_ioa_vpd;
    struct ipr_cfc_vpd *p_cfc_vpd;
    struct ipr_dram_vpd *p_dram_vpd;
    struct ipr_inquiry_page0 *p_supp_vpd;
    struct ipr_inquiry_page3 *p_ucode_vpd;
    struct ipr_config_table_entry *p_cfgte;
    struct ipr_resource_dll *p_sis_resource_dll;
    int i, current_command;
    char temp_ccin[5];

    ENTER;

    ipr_log_info_tty("Waiting for IBM %s at %s to come operational",
                        p_shared_cfg->ccin_str, p_shared_cfg->ioa_host_str);

    while(delay < IPR_OPERATIONAL_TIMEOUT)
    {
        temp_reg = readl(ipr_cfg->regs.sense_interrupt_reg);

        if (temp_reg & IPR_PCII_ERROR_INTERRUPTS)
        {
            if (temp_reg & IPR_PCII_IOARCB_XFER_FAILED)
                rc = IPR_RC_XFER_FAILED;
            else if (temp_reg & IPR_PCII_NO_HOST_RRQ)
                rc = IPR_NO_HRRQ;
            else if (temp_reg & IPR_PCII_IOARRIN_LOST)
                rc = IPR_IOARRIN_LOST;
            else if (temp_reg & IPR_PCII_MMIO_ERROR)
                rc = IPR_MMIO_ERROR;
            else if (temp_reg & IPR_PCII_IOA_UNIT_CHECKED)
                rc = IPR_IOA_UNIT_CHECKED;
            else
                rc = IPR_RC_FAILED;
            break;
        }
        else if (temp_reg & IPR_PCII_IOA_TRANS_TO_OPER)
        {
            break;
        }
        else
        {
            /* Sleep for 1 second */
            ipr_sleep(1000);
            ipr_print_tty(".");
            delay += 1; 
        }
    }

    ipr_print_tty(IPR_EOL);

    /* Timeout the IOA if it's not coming operational */
    if (delay >= IPR_OPERATIONAL_TIMEOUT)
    {
        ipr_beg_err(KERN_ERR);
        ipr_log_err_tty("IOA timed out coming operational"IPR_EOL);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
        return IPR_RC_TIMEOUT;
    }
    else if (rc)
    {
        for (i=0;
             i<(sizeof(ipr_error_int_decode)/sizeof(struct ipr_error_int_decode_t));
             i++)
        {
            if ((ipr_error_int_decode[i].interrupt & temp_reg) ||
                (ipr_error_int_decode[i].interrupt == 0xffffffff))
                break;
        }

        if (ipr_error_int_decode[i].interrupt != IPR_PCII_IOA_UNIT_CHECKED)
        {
            ipr_beg_err(KERN_ERR);
            ipr_log_err_tty("IOA failed initialization sequence. %s"IPR_EOL,
                               ipr_error_int_decode[i].p_error);
            ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
            ipr_end_err(KERN_ERR);
        }
        return rc;
    }

    delay = 0;

    p_ioa_vpd = p_shared_cfg->p_ioa_vpd;
    p_cfc_vpd = p_shared_cfg->p_cfc_vpd;
    p_ucode_vpd = p_shared_cfg->p_ucode_vpd;
    p_supp_vpd = p_shared_cfg->p_page0_vpd;
    p_dram_vpd = p_shared_cfg->p_dram_vpd;

    /* Zero DMA buffer for our IOA Inquiry data */
    memset(p_ioa_vpd, 0, sizeof(struct ipr_ioa_vpd));

    /* Zero DMA buffer for our CFC Inquiry data */
    memset(p_cfc_vpd, 0, sizeof(struct ipr_cfc_vpd));

    memset(p_dram_vpd, 0, sizeof(struct ipr_dram_vpd));
    memset(p_ucode_vpd, 0, sizeof(struct ipr_inquiry_page3));
    memset(p_supp_vpd, 0, sizeof(struct ipr_inquiry_page0));
    strcpy(p_dram_vpd->dram_size, "00");

    if (!rc)
    {
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_ID_HOST_RR_Q,
                                      IPR_INTERNAL_TIMEOUT,
                                      0, NULL, 0, 0);
        current_command = IPR_ID_HOST_RR_Q;
    }

    if (!rc) {
        /* IOA has transitioned to operational - we can try to send a shutdown to it if we
         need to */
        p_shared_cfg->ioa_operational = 1;


        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_INQUIRY,
                                      IPR_INTERNAL_TIMEOUT, 0xff, p_ioa_vpd,
                                      p_shared_cfg->ioa_vpd_dma,
                                      sizeof(struct ipr_ioa_vpd));
        current_command = IPR_INQUIRY;
    }

    if (!rc)
    {
        /* Grab the CCIN out of the VPD and store it away */
        memcpy(temp_ccin, p_ioa_vpd->std_inq_data.vpids.product_id, 4);
        temp_ccin[4] = '\0';

        p_shared_cfg->ccin = simple_strtoul((char *)temp_ccin, NULL, 16);

        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_INQUIRY,
                                      IPR_INTERNAL_TIMEOUT, 0, p_supp_vpd,
                                      p_shared_cfg->page0_vpd_dma, sizeof(struct ipr_inquiry_page0));
    }

    if (!rc && (p_supp_vpd->page_length <= IPR_MAX_NUM_SUPP_INQ_PAGES))
    {
        for (i = 0; (i < p_supp_vpd->page_length) && !rc; i++)
        {
            switch (p_supp_vpd->supported_page_codes[i])
            {
                case 0x01:
                    ipr_send_blocking_cmd(p_shared_cfg, IPR_INQUIRY,
                                             IPR_INTERNAL_TIMEOUT, 0x01,
                                             p_cfc_vpd, p_shared_cfg->cfc_vpd_dma,
                                             sizeof(struct ipr_cfc_vpd));
                    break;
                case 0x02:
                    rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_INQUIRY,
                                                  IPR_INTERNAL_TIMEOUT, 0x02,
                                                  p_dram_vpd, p_shared_cfg->dram_vpd_dma,
                                                  sizeof(struct ipr_dram_vpd));
                    break;
                default:
                    /* Ignore this page - either unknown or required */
                    break;
            }
        }
    }

    if (!rc)
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_INQUIRY,
                                      IPR_INTERNAL_TIMEOUT, 0x03,
                                      p_ucode_vpd, p_shared_cfg->ucode_vpd_dma,
                                      sizeof(struct ipr_inquiry_page3));

    if (!rc)
    {
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_QUERY_IOA_CONFIG,
                                      IPR_INTERNAL_TIMEOUT, 0, ipr_cfg->p_config_table,
                                      ipr_cfg->config_table_dma,
                                      sizeof(struct ipr_config_table));
        current_command = IPR_QUERY_IOA_CONFIG;
    }

    if (!rc)
    {
        for (i = 0; i < ipr_cfg->p_config_table->num_entries; i++)
        {
            p_cfgte = &ipr_cfg->p_config_table->dev[i];

            /* check backplane to determine if 15K dasd exclusion applies. */
            ipr_check_backplane(p_shared_cfg, p_cfgte);
        }
    }

    if (!rc)
    {
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_SET_SUPPORTED_DEVICES,
                                      IPR_SET_SUP_DEVICE_TIMEOUT, 0, ipr_cfg->p_ssd_header,
                                      ipr_cfg->ssd_header_dma,
                                      sistoh16(ipr_cfg->p_ssd_header->data_length));
        current_command = IPR_SET_SUPPORTED_DEVICES;
    }

    if (!rc || (rc == IPR_IOASC_NR_IOA_MICROCODE))
    {
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_QUERY_IOA_CONFIG,
                                      IPR_INTERNAL_TIMEOUT, 0, ipr_cfg->p_config_table,
                                      ipr_cfg->config_table_dma, sizeof(struct ipr_config_table));
        current_command = IPR_QUERY_IOA_CONFIG;
    }

    if (!rc && (ipr_cfg->p_config_table->num_entries == 0))
        ipr_cfg->p_config_table->num_entries = 1;

    if (!rc)
    {
        strncpy(dram_size, p_dram_vpd->dram_size, 3);
        dram_size[3] = '\0';

        p_shared_cfg->dram_size = simple_strtoul(dram_size, NULL, 16);

        for (i = 0; i < ipr_cfg->p_config_table->num_entries; i++)
        {
            p_cfgte = &ipr_cfg->p_config_table->dev[i];

            if (!ipr_is_res_addr_valid(&p_cfgte->resource_address))
            {
                /* invalid resource address, log message and ignore entry */
                ipr_log_err("Invalid resource address reported: 0x%08X"IPR_EOL,
                               IPR_GET_PHYSICAL_LOCATOR(p_cfgte->resource_address));
                continue;
            }

            p_sis_resource_dll = ipr_get_resource_entry(p_shared_cfg);

            if (p_sis_resource_dll == NULL)
            {
                ipr_beg_err(KERN_ERR);
                ipr_log_err("Too many devices attached"IPR_EOL);
                ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
                ipr_end_err(KERN_ERR);
                break;
            }

            ipr_update_resource(p_shared_cfg, &p_sis_resource_dll->data, p_cfgte, 0);
        }
    }

    if (!rc)
    {
        rc = ipr_set_mode_page28(p_shared_cfg);
        current_command = IPR_MODE_SENSE;
    }

    if ((!rc || (rc == IPR_IOASC_NR_IOA_MICROCODE)) && (ipr_arch == IPR_ARCH_ISERIES))
    {
        rc = ipr_build_slot_map(p_shared_cfg);
        current_command = IPR_RECEIVE_DIAGNOSTIC;
    }

    if (!rc || (rc == IPR_IOASC_NR_IOA_MICROCODE))
    {
        rc = ipr_init_devices(p_shared_cfg);
        current_command = IPR_SET_DASD_TIMEOUTS;
    }

    if (!rc)
    {
        ipr_log_config_error(p_shared_cfg, 1, NULL);

        p_shared_cfg->allow_interrupts = 1;

        /* Clear interrupt mask to allow interrupts */
        writel(IPR_PCII_OPER_INTERRUPTS, ipr_cfg->regs.clr_interrupt_mask_reg);

        /* Re-read the PCI interrupt mask reg to force clear */
        temp_reg = readl(ipr_cfg->regs.sense_interrupt_mask_reg);
    }
    else
    {
        for (i=0;
             i<((sizeof(ipr_error_rc_decode)/sizeof(struct ipr_error_rc_decode_t)) - 1);
             i++)
        {
            if ((ipr_error_rc_decode[i].rc == rc))
                break;
        }

        ipr_beg_err(KERN_ERR);
        ipr_log_err_tty("IOA failed to come operational on command %x, %s"IPR_EOL,
                           current_command, ipr_error_rc_decode[i].p_error);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
    }

    LEAVE;

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Determine if the xfer speed should be limited for a
 *          given SCSI bus.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: 0 for no speed limit, else MB/sec limit
 *---------------------------------------------------------------------------*/
static u32 ipr_scsi_bus_speed_limit(struct ipr_shared_config *p_shared_cfg,
                                       int scsi_bus)
{
    int backplane_entry_count, matches, j;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    struct ipr_resource_entry *p_rte;
    struct ipr_resource_dll *p_resource_dll;
    const struct ipr_backplane_table_entry *p_bte;

    /* Loop through each config table entry in the config table buffer */
    for (p_resource_dll = p_shared_cfg->rsteUsedH;
         p_resource_dll != NULL;
         p_resource_dll = p_resource_dll->next)
    {
        p_rte = &p_resource_dll->data;

        if (scsi_bus != p_rte->resource_address.bus)
            continue;

        if (!(IPR_IS_SES_DEVICE(p_rte->std_inq_data)))
            continue;

        for /*! Loop through entries of the backplane table */
            (backplane_entry_count = 0,
             p_bte = &ipr_backplane_table[0];
             backplane_entry_count <
                 (sizeof(ipr_backplane_table) /
                  sizeof(struct ipr_backplane_table_entry));
             backplane_entry_count++, p_bte++)
        {
            /* Does the Product ID for this SCSI device match this entry in
             the backplane table */

            for (j = 0, matches = 0; j < IPR_PROD_ID_LEN; j++)
            {
                if (p_bte->compare_product_id_byte[j])
                {
                    if (p_rte->std_inq_data.vpids.product_id[j] == p_bte->product_id[j])
                        matches++;
                    else
                        break;
                }
                else
                    matches++;
            }

            if (matches == IPR_PROD_ID_LEN)
            {
                /* Return the max bus speed limit from the table entry */
                return IPR_MIN(p_bte->max_bus_speed_limit,
                                  ipr_cfg->p_ioa_cfg->max_bus_speed_limit);
            }
        }
    }

    /* Return 0 to indicate no backplane speed limitations */
    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Set changeable parms for mode page 28 bus attributes.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: void
 *---------------------------------------------------------------------------*/
static void ipr_set_page_28_changeable(struct ipr_shared_config *p_shared_cfg,
                                          struct ipr_mode_page_28_scsi_dev_bus_attr *p_chg_dev_bus_entry,
                                          struct ipr_mode_page_28_scsi_dev_bus_attr *p_dev_bus_entry)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    if (ipr_cfg->p_ioa_cfg->scsi_id_changeable)
        p_chg_dev_bus_entry->scsi_id = 0xFF;

    p_chg_dev_bus_entry->bus_width = 0xFF;
    p_chg_dev_bus_entry->max_xfer_rate = 0xFFFFFFFF;
    p_chg_dev_bus_entry->min_time_delay = 0xFF;
}

/*---------------------------------------------------------------------------
 * Purpose: Routine to modify the IOAFP's mode page 28 for
 *          a specified SCSI bus.
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: void
 *---------------------------------------------------------------------------*/
void ipr_modify_ioafp_mode_page_28(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_mode_page_28_header *
                                      p_modepage_28_header,
                                      int scsi_bus)
{
    int i, j, found;
    int dev_entry_length;
    u32 max_bus_speed;
    u32 max_bus_speed_limit;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    struct ipr_mode_page_28_scsi_dev_bus_attr *p_dev_bus_entry;

    dev_entry_length = p_modepage_28_header->dev_entry_length;

    /* Point to first device bus entry */
    p_dev_bus_entry = (struct ipr_mode_page_28_scsi_dev_bus_attr *)
        (p_modepage_28_header + 1);

    /* Loop for each device bus entry */
    for (i = 0;
         i < p_modepage_28_header->num_dev_entries;
         i++,
         p_dev_bus_entry = (struct ipr_mode_page_28_scsi_dev_bus_attr *)
         ((char *)p_dev_bus_entry + dev_entry_length))
    {
        if (scsi_bus != p_dev_bus_entry->res_addr.bus)
            continue;

        for (j = 0,
             found = 0;
             j < p_shared_cfg->p_page_28->saved.page_hdr.num_dev_entries;
             j++)
        {
            if (p_dev_bus_entry->res_addr.bus ==
                p_shared_cfg->p_page_28->saved.attr[j].res_addr.bus)
            {
                found = 1;

                /* found saved bus entry, copy to send mode select */
                memcpy(p_dev_bus_entry,
                       &p_shared_cfg->p_page_28->saved.attr[j],
                       sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr));
                break;
            }
        }

        if (!found)
        {
            /* In case the adapter gave us bad data, initialize this to wide */
            /* If this were zero, we would end up dividing by zero below */
            if (p_dev_bus_entry->bus_width < 8)
                p_dev_bus_entry->bus_width = 16;

            /* Set Maximum transfer rate */
            max_bus_speed = (sistoh32(p_dev_bus_entry->max_xfer_rate) *
                             (p_dev_bus_entry->bus_width / 8))/10;

            if ((max_bus_speed_limit = ipr_scsi_bus_speed_limit(p_shared_cfg,
                                                                scsi_bus)))
            {
                if (max_bus_speed_limit < max_bus_speed)
                    max_bus_speed = max_bus_speed_limit;
            }

            p_dev_bus_entry->max_xfer_rate =
                htosis32(max_bus_speed * 10 / (p_dev_bus_entry->bus_width / 8));

            p_dev_bus_entry->qas_capability =
                IPR_MODEPAGE28_QAS_CAPABILITY_DISABLE_ALL;

            /* New bus is being reported in page_28, need to save
             off this new information */
            memcpy(&p_shared_cfg->p_page_28->saved.attr[j],
                   p_dev_bus_entry,
                   sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr));

            memcpy(&p_shared_cfg->p_page_28->dflt.attr[j],
                   p_dev_bus_entry,
                   sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr));

            ipr_set_page_28_changeable(p_shared_cfg,
                                       &p_shared_cfg->p_page_28->changeable.attr[j],
                                       &p_shared_cfg->p_page_28->dflt.attr[j]);

            /* Check if max_xfer_rate has been limited */
            if (!max_bus_speed_limit)
            {
                p_shared_cfg->p_page_28->dflt.attr[j].max_xfer_rate =
                    htosis32(ipr_cfg->p_ioa_cfg->max_bus_speed_limit * 10 /
                             (p_dev_bus_entry->bus_width / 8));
            }
            else
            {
                p_shared_cfg->p_page_28->dflt.attr[j].max_xfer_rate =
                    htosis32(max_bus_speed_limit * 10 /
                             (p_dev_bus_entry->bus_width / 8));
            }

            p_shared_cfg->p_page_28->saved.page_hdr.num_dev_entries++;
        }
    }

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Get and set mode page 28 parameters
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_TIMEOUT           - IOA timed out
 *---------------------------------------------------------------------------*/
static int ipr_set_mode_page28(struct ipr_shared_config *p_shared_cfg)
{
    int    bus_num, rc;
    struct ipr_mode_parm_hdr *p_mode_parm_header;
    struct ipr_mode_page_28_header *p_modepage_28_header;
    void   *p_mode_page_28;
    ipr_dma_addr p_mode_page_28_dma;
    u32 mode_data_length;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    rc = IPR_RC_SUCCESS;

    p_mode_page_28 = &ipr_cfg->p_dasd_init_buf[0]->mode_pages;
    p_mode_page_28_dma = ipr_cfg->p_dasd_init_buf[0]->mode_pages_dma;

    /* issue mode sense for page 28 to configure device bus attributes */
    rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_MODE_SENSE,
                                  IPR_INTERNAL_TIMEOUT,
                                  IPR_PAGE_CODE_28, p_mode_page_28,
                                  p_mode_page_28_dma, 255);

    if (!rc)
    {
        /* Point to Mode data and Mode page 28 */
        p_mode_parm_header = (struct ipr_mode_parm_hdr *) p_mode_page_28;
        p_modepage_28_header = (struct ipr_mode_page_28_header *) (p_mode_parm_header + 1);

        for (bus_num = 0;
             bus_num < IPR_MAX_NUM_BUSES;
             bus_num++)
        {
            ipr_modify_ioafp_mode_page_28(p_shared_cfg,
                                             p_modepage_28_header,
                                             bus_num);
        }

        /* copy over header information to page_28 saved */
        memcpy(&p_shared_cfg->p_page_28->saved,
               p_mode_page_28,
               sizeof(struct ipr_mode_parm_hdr) +
               sizeof(struct ipr_mode_page_28_header));
        p_shared_cfg->p_page_28->saved.page_hdr.dev_entry_length =
            sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

        /* copy over header information to page_28 changeable */
        memcpy(&p_shared_cfg->p_page_28->changeable,
               p_mode_page_28,
               sizeof(struct ipr_mode_parm_hdr) +
               sizeof(struct ipr_mode_page_28_header));
        p_shared_cfg->p_page_28->changeable.page_hdr.dev_entry_length =
            sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

        /* copy over header information to page_28 default */
        memcpy(&p_shared_cfg->p_page_28->dflt,
               p_mode_page_28,
               sizeof(struct ipr_mode_parm_hdr) +
               sizeof(struct ipr_mode_page_28_header));
        p_shared_cfg->p_page_28->dflt.page_hdr.dev_entry_length =
            sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

        mode_data_length = p_mode_parm_header->length + 1;

        /* Zero length field */
        p_mode_parm_header->length = 0;

        /* Send IOAFP Mode Select command to IOA */
        rc = ipr_send_blocking_cmd(p_shared_cfg, IPR_MODE_SELECT,
                                      IPR_INTERNAL_TIMEOUT,
                                      0x11, p_mode_page_28,
                                      p_mode_page_28_dma,
                                      mode_data_length);
    }
    else
    {
        if (rc == IPR_RC_FAILED)
            rc = IPR_RC_SUCCESS;
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Get slot/map data from the SES
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *          IPR_RC_TIMEOUT   - IOA timed out
 *---------------------------------------------------------------------------*/
static int ipr_build_slot_map(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_resource_entry *p_rte, *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll, *p_resource_dll_ses;
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioadl_desc *p_ioadl;
    int bus_num, length, rc;
    int failed;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    u32 ioasc;

    rc = IPR_RC_SUCCESS;

    for (p_resource_dll_ses = p_shared_cfg->rsteUsedH;
         p_resource_dll_ses != NULL;
         p_resource_dll_ses = p_resource_dll_ses->next)
    {
        p_resource_entry = &p_resource_dll_ses->data;

        if (IPR_IS_SES_DEVICE(p_resource_entry->std_inq_data))
        {
            bus_num = p_resource_entry->resource_address.bus;

            if (bus_num > IPR_MAX_NUM_BUSES)
            {
                ipr_log_err("Invalid resource address 0x%08X returned for SES"IPR_EOL,
                               IPR_GET_PHYSICAL_LOCATOR(p_resource_entry->resource_address));
                continue;
            }

            p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);
            p_ioarcb = &p_host_ioarcb->ioarcb; 
            p_ioadl = p_host_ioarcb->p_ioadl;

            length = sizeof(struct ipr_element_desc_page);

            /* Setup IOARCB */
            p_ioarcb->ioa_res_handle = p_resource_entry->resource_handle;
            p_ioarcb->ioarcb_cmd_pkt.cdb[0] = IPR_RECEIVE_DIAGNOSTIC;
            p_ioarcb->ioarcb_cmd_pkt.cdb[1] = 0x01; /* Page Code Valid */
            p_ioarcb->ioarcb_cmd_pkt.cdb[2] = 7;    /* Page Code 7 */
            p_ioarcb->ioarcb_cmd_pkt.cdb[3] = (length >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = length & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[5] = 0;

            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 0;
            p_ioarcb->ioarcb_cmd_pkt.cmd_sync_override = 1;
            p_ioarcb->ioarcb_cmd_pkt.no_underlength_checking = 1;
            p_ioarcb->ioarcb_cmd_pkt.cmd_timeout = htosis16(IPR_INTERNAL_TIMEOUT);

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | length);
            p_ioadl->address = htosis32((u32)p_shared_cfg->ses_data_dma[bus_num]);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(length);

            /* This will allow 1 retry. */
            failed = 2; 

            while(failed)
            {
                /* Zero out IOASA */
                memset(p_host_ioarcb->p_ioasa, 0,
                       sizeof(struct ipr_ioasa));

                /* Poke the IOARRIN with the PCI address of the IOARCB */
                writel(sistoh32(p_ioarcb->ioarcb_host_pci_addr),
                              ipr_cfg->regs.ioarrin_reg);

                rc = ipr_poll_isr(p_shared_cfg, IPR_INTERNAL_TIMEOUT*2);

                /* If we timed the op out we want to return to the caller and have
                 them do the appropriate recovery */
                if (rc == IPR_RC_TIMEOUT)
                    return rc;

                ioasc = sistoh32(p_host_ioarcb->p_ioasa->ioasc);

                if (rc == IPR_RC_SUCCESS)
                {
                    if (IPR_IOASC_SENSE_KEY(ioasc) == 0)
                        break;
                    else
                        failed--;
                }
                else
                {
                    /* This means we got a nasty interrupt back from the IOA */
                    return rc;
                }

                if (failed == 0)
                {
                    /* Command failed and we are out of retries */
                    ipr_beg_err(KERN_ERR);
                    ipr_log_err("Could not get Slot Map data. IOASC: 0x%08x"IPR_EOL, ioasc);
                    ipr_log_dev_physical_location(p_shared_cfg,
                                                     p_resource_entry->resource_address,
                                                     KERN_ERR);
                    ipr_end_err(KERN_ERR);
                }
            }

            ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
            ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);

            if (rc == IPR_RC_SUCCESS)
            {
                for (p_resource_dll = p_shared_cfg->rsteUsedH;
                     p_resource_dll != NULL;
                     p_resource_dll = p_resource_dll->next)
                {
                    p_rte = &p_resource_dll->data;

                    if (p_rte->resource_address.bus == bus_num)
                    {
                        if ((IPR_IS_DASD_DEVICE(p_rte->std_inq_data)) &&
                            (!p_rte->is_ioa_resource))
                            ipr_get_card_pos(p_shared_cfg, p_rte->resource_address, p_rte->slot_label);
                    }
                }
            }
        }
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Get slot/map data from the SES
 * Context: Task level only
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *          IPR_RC_TIMEOUT   - IOA timed out
 *---------------------------------------------------------------------------*/
static int ipr_build_slot_map_runtime(struct ipr_shared_config *p_shared_cfg,
                                         struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_hostrcb *p_hostrcb)
{
    int bus_num, length, rc;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_ccb *p_sis_ccb;
    int timeout = IPR_INTERNAL_TIMEOUT;
    struct ipr_dasd_init_bufs *p_dasd_init_buf;

    rc = IPR_RC_SUCCESS;

    bus_num = p_resource_entry->resource_address.bus;

    if (bus_num > IPR_MAX_NUM_BUSES)
    {
        ipr_log_err("Invalid resource address returned for SES"IPR_EOL);
        ipr_log_err("Resource address: 0x%08x"IPR_EOL,
                       IPR_GET_PHYSICAL_LOCATOR(p_resource_entry->resource_address));
        ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, p_hostrcb);
        return IPR_RC_FAILED;
    }

    length = sizeof(struct ipr_element_desc_page);

    p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

    if (p_sis_ccb == NULL)
    {
        /* Requests must not be allowed right now - we are probably going through
         reset/reload right now */
        ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, p_hostrcb);
        return IPR_RC_FAILED;
    }

    p_dasd_init_buf = ipr_get_dasd_init_buffer(ipr_cfg);

    if (p_dasd_init_buf == NULL)
    {
        ipr_log_err("Failed to allocate dasd init buffer"IPR_EOL);
        ipr_release_ccb(p_shared_cfg, p_sis_ccb);
        ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE, p_hostrcb);
        return IPR_RC_FAILED;
    }

    p_dasd_init_buf->p_hostrcb = p_hostrcb;
    p_dasd_init_buf->p_dev_res = p_resource_entry;

    p_sis_ccb->p_resource = p_resource_entry;
    p_sis_ccb->p_scratch = p_dasd_init_buf;
    p_sis_ccb->job_step = IPR_SINIT_START;
    p_sis_ccb->bufflen = length;
    p_sis_ccb->buffer = p_shared_cfg->p_ses_data[bus_num];
    p_sis_ccb->buffer_dma = (u32)p_shared_cfg->ses_data_dma[bus_num];
    p_sis_ccb->scsi_use_sg = 0;
    p_sis_ccb->cdb[0] = IPR_RECEIVE_DIAGNOSTIC;
    p_sis_ccb->cdb[1] = 0x01; /* Page Code Valid */
    p_sis_ccb->cdb[2] = 7;    /* Page Code 7 */
    p_sis_ccb->cdb[3] = (length >> 8) & 0xff;
    p_sis_ccb->cdb[4] = length & 0xff;
    p_sis_ccb->cdb[5] = 0;
    p_sis_ccb->cmd_len = 6;
    p_sis_ccb->flags = IPR_BUFFER_MAPPED | IPR_CMD_SYNC_OVERRIDE;
    p_sis_ccb->data_direction = IPR_DATA_READ;

    rc = ipr_do_req(p_shared_cfg, p_sis_ccb,
                       ipr_bus_init_job, timeout);
    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Job router for runtime SES initialization
 * Lock State: io_request_lock assumed to be held
 * Context: Interrupt level
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_bus_init_job(struct ipr_shared_config *p_shared_cfg,
                                struct ipr_ccb *p_sis_ccb)
{
    struct ipr_resource_entry *p_rte, *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;
    u32 rc = IPR_RC_SUCCESS;
    int bus_num;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_dasd_init_bufs *p_dasd_init_buf = p_sis_ccb->p_scratch;
    struct ipr_mode_parm_hdr *p_mode_parm_header;
    struct ipr_mode_page_28_header *p_modepage_28_header;
    void   *p_mode_page_28;
    u32 mode_data_length;
    int done = 0;

    rc = p_sis_ccb->completion;

    if (rc != IPR_RC_SUCCESS)
    {
        p_resource_entry = p_sis_ccb->p_resource;
        bus_num = p_resource_entry->resource_address.bus;
        if (ipr_sense_valid(p_sis_ccb->sense_buffer[0]))
        {
            /* We have a valid sense buffer */
            if ((p_sis_ccb->sense_buffer[2] & 0xf) != 0)
            {
                rc = IPR_RC_FAILED;
                IPR_DBG_CMD(ipr_beg_err(KERN_ERR));
                ipr_dbg_err("0x%02x failed with SK: 0x%X ASC: 0x%X ASCQ: 0x%X"IPR_EOL,
                               p_sis_ccb->cdb[0], (p_sis_ccb->sense_buffer[2] & 0xf),
                               p_sis_ccb->sense_buffer[12], p_sis_ccb->sense_buffer[13]);
                ipr_dbg_err("Failing device res_addr: %02X %02X %02X"IPR_EOL,
                               bus_num, p_resource_entry->resource_address.target,
                               p_resource_entry->resource_address.lun);
                IPR_DBG_CMD(ipr_log_dev_physical_location(p_shared_cfg,
                                                                p_resource_entry->resource_address,
                                                                KERN_ERR));
                ipr_dbg_err("Controlling IOA"IPR_EOL);
                IPR_DBG_CMD(ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR));
                IPR_DBG_CMD(ipr_end_err(KERN_ERR));
            }
        }
        ipr_release_ccb(p_shared_cfg, p_sis_ccb);
        done = 1;
    }
    else
    {
        p_resource_entry = p_dasd_init_buf->p_dev_res;
        bus_num = p_resource_entry->resource_address.bus;

        switch (p_sis_ccb->job_step)
        {
            case IPR_SINIT_START:
                for (p_resource_dll = p_shared_cfg->rsteUsedH;
                     p_resource_dll != NULL;
                     p_resource_dll = p_resource_dll->next)
                {
                    p_rte = &p_resource_dll->data;

                    if (p_rte->resource_address.bus == bus_num)
                    {
                        if ((IPR_IS_DASD_DEVICE(p_rte->std_inq_data)) &&
                            (!p_rte->is_ioa_resource))
                            ipr_get_card_pos(p_shared_cfg, p_rte->resource_address, p_rte->slot_label);
                    }
                }

                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                /* sense mode sense for page 28 */
                p_mode_page_28 = &p_dasd_init_buf->mode_pages;

                /* issue mode sense for page 28 to configure device bus attributes */
                rc = ipr_ioa_req(p_shared_cfg, ipr_bus_init_job,
                                    p_dasd_init_buf,
                                    p_mode_page_28,
                                    p_dasd_init_buf->mode_pages_dma,
                                    IPR_MODE_SENSE, 0x28, 255,
                                    IPR_SINIT_MODE_SENSE);
                if (rc != IPR_RC_SUCCESS)
                {
                    ipr_log_err("Mode Sense Page 28 failed"IPR_EOL);
                    done = 1;
                }
                break;
            case IPR_SINIT_MODE_SENSE:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                /* Point to Mode data and Mode page 28 */
                p_mode_page_28 = &p_dasd_init_buf->mode_pages;
                p_mode_parm_header = (struct ipr_mode_parm_hdr *)
                    p_mode_page_28;
                p_modepage_28_header = (struct ipr_mode_page_28_header *)
                    (p_mode_parm_header + 1);

                /* Modify the IOAFP's Mode page 28 for specified
                 SCSI bus */
                ipr_modify_ioafp_mode_page_28(p_shared_cfg,
                                                 p_modepage_28_header,
                                                 bus_num);

                /* copy over header information to page_28 saved */
                memcpy(&p_shared_cfg->p_page_28->saved,
                       p_mode_page_28,
                       sizeof(struct ipr_mode_parm_hdr) +
                       sizeof(struct ipr_mode_page_28_header));
                p_shared_cfg->p_page_28->saved.page_hdr.dev_entry_length =
                    sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

                /* copy over header information to page_28 changeable */
                memcpy(&p_shared_cfg->p_page_28->changeable,
                       p_mode_page_28,
                       sizeof(struct ipr_mode_parm_hdr) +
                       sizeof(struct ipr_mode_page_28_header));
                p_shared_cfg->p_page_28->changeable.page_hdr.dev_entry_length =
                    sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

                /* copy over header information to page_28 default */
                memcpy(&p_shared_cfg->p_page_28->dflt,
                       p_mode_page_28,
                       sizeof(struct ipr_mode_parm_hdr) +
                       sizeof(struct ipr_mode_page_28_header));
                p_shared_cfg->p_page_28->dflt.page_hdr.dev_entry_length =
                    sizeof(struct ipr_mode_page_28_scsi_dev_bus_attr);

                /* Determine the amount of data to transfer on the Mode
                 Select */
                /* Note: Need to add 1 since the length field does not
                 include itself. */
                mode_data_length = p_mode_parm_header->length + 1;

                /* Zero Mode parameter header */
                /* Note: This is done to zero the Mode data length, which
                 is reserved on the Mode Select. */
                p_mode_parm_header->length = 0;

                /* Send IOAFP Mode Select command to IOA */
                rc = ipr_ioa_req(p_shared_cfg, ipr_bus_init_job,
                                    p_dasd_init_buf,
                                    p_mode_page_28,
                                    p_dasd_init_buf->mode_pages_dma,
                                    IPR_MODE_SELECT, 0x11, mode_data_length,
                                    IPR_SINIT_MODE_SELECT);

                if (rc != IPR_RC_SUCCESS)
                {
                    ipr_log_err("Mode Select page 28 failed"IPR_EOL);
                    done = 1;
                }
                break;
            case IPR_SINIT_MODE_SELECT:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);
                done = 1;
                break;
            default:
                break;
        }
    }

    if (done)
    {
        ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                         p_dasd_init_buf->p_hostrcb);
        ipr_put_dasd_init_buffer(ipr_cfg, p_dasd_init_buf);
    }

    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Peel ops off HRRQ
 * Context: Interrupt level.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS       - Success
 *          IPR_RC_XFER_FAILED   - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ          - No HRRQ interrupt
 *          IPR_IOARRIN_LOST     - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR       - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED - IOA unit checked
 *          IPR_SPURIOUS_INT     - IOA had a spurious interrupt
 *          IPR_RESET_ADAPTER        - Adapter requests a reset
 *---------------------------------------------------------------------------*/
u32 ipr_get_done_ops(struct ipr_shared_config *p_shared_cfg,
                        struct ipr_ccb **pp_sis_cmnd)
{
    volatile u32 temp_pci_reg, temp_mask_reg;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_ccb *p_doneq = NULL;
    u32 rc = IPR_RC_SUCCESS;
    u32 ioasc, host_ioarcb_index;
    struct ipr_host_ioarcb *p_host_ioarcb;
    unsigned char *p_sense_buffer;
    u8 device_type;

    /* If interrupts are disabled, ignore the interrupt */
    if (!p_shared_cfg->allow_interrupts)
        return IPR_SPURIOUS_INT;

    temp_mask_reg = readl(ipr_cfg->regs.sense_interrupt_mask_reg);
    temp_pci_reg = readl(ipr_cfg->regs.sense_interrupt_reg);

    temp_pci_reg &= ~temp_mask_reg;

    /* If an interrupt on the adapter did not occur, ignore it */
    if ((temp_pci_reg & IPR_PCII_OPER_INTERRUPTS) == 0)
        return IPR_SPURIOUS_INT;

    *pp_sis_cmnd = NULL;

    while (1)
    {
        p_host_ioarcb = NULL;

        while((sistoh32(*ipr_cfg->host_rrq_curr_ptr) & IPR_HRRQ_TOGGLE_BIT) == ipr_cfg->toggle_bit)
        {
            host_ioarcb_index =
                (sistoh32(*ipr_cfg->host_rrq_curr_ptr) & IPR_HRRQ_REQ_RESP_HANDLE_MASK) >>
                IPR_HRRQ_REQ_RESP_HANDLE_SHIFT;

            if (host_ioarcb_index >= IPR_NUM_CMD_BLKS)
            {
                ipr_log_err("Invalid response handle from IOA"IPR_EOL);

                ipr_mask_interrupts(p_shared_cfg);

                return IPR_RESET_ADAPTER;
            }

            p_host_ioarcb = ipr_cfg->host_ioarcb_list[host_ioarcb_index];

            if (p_doneq == NULL)
            {
                p_doneq = p_host_ioarcb->p_sis_cmd;
                *pp_sis_cmnd = p_doneq;
            }
            else
            {
                p_doneq->p_next_done = p_host_ioarcb->p_sis_cmd;
                p_doneq = p_doneq->p_next_done;
            }

            p_doneq->p_next_done = NULL;
            ioasc = sistoh32(p_host_ioarcb->p_ioasa->ioasc);

            if (p_doneq->p_resource->is_ioa_resource)
                device_type = IPR_TRACE_IOA;
            else if (p_doneq->flags & IPR_GPDD_CMD)
                device_type = IPR_TRACE_GEN;
            else
                device_type = IPR_TRACE_DASD;

            ipr_trc_hook(p_shared_cfg,
                            p_doneq->cdb[0],
                            IPR_TRACE_FINISH,
                            device_type,
                            host_ioarcb_index,
                            p_doneq->bufflen,
                            ioasc);

            if (p_doneq->flags & IPR_GPDD_CMD)
            {
                p_doneq->residual = sistoh32(p_host_ioarcb->p_ioasa->residual_data_len);

                if (IPR_IOASC_SENSE_KEY(ioasc) < 2)
                {
                    /* Command completed successfully */
                }
                else if (((ioasc & 0xffffff00) == 0x04448500))
                {
                    /* Device bus status error */
                    p_doneq->completion = IPR_RC_FAILED;
                    p_doneq->status = IPR_IOASC_SENSE_STATUS(ioasc);
                }
                else
                {
                    /* Error, not a device bus status error */
                    p_doneq->completion = IPR_RC_FAILED;
                    ipr_gen_sense(p_doneq->p_resource,
                                     p_doneq->sense_buffer, p_host_ioarcb->p_ioasa);

                    if (p_shared_cfg->debug_level >= 2)
                        ipr_dump_ioasa(p_shared_cfg, p_doneq->p_resource,
                                          p_host_ioarcb->p_ioasa);
                }
            }
            else /* Non GPDD command */
            {
                if (IPR_IOASC_SENSE_KEY(ioasc) >= 2)
                {
                    if (p_shared_cfg->debug_level >= 3)
                        ipr_dump_ioasa(p_shared_cfg, p_doneq->p_resource,
                                          p_host_ioarcb->p_ioasa);

                    /* Set residual byte count to entire length of the op since
                     what is in the IOA may have been computed with a different
                     sector size */
                    p_doneq->residual = p_doneq->bufflen;

                    p_sense_buffer = p_doneq->sense_buffer;

                    ipr_gen_sense(p_doneq->p_resource,
                                     p_sense_buffer, p_host_ioarcb->p_ioasa);

                    p_doneq->completion = IPR_RC_FAILED;

                    ipr_dbg_err("Op failed with opcode: 0x%02X, ioasc: 0x%08X"IPR_EOL,
                                   p_doneq->cdb[0], ioasc);
                }
            }

            /* Pull off of pending queue */
            ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
            ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);

            if (ipr_cfg->host_rrq_curr_ptr < ipr_cfg->host_rrq_end_addr)
            {
                ipr_cfg->host_rrq_curr_ptr++;
            }
            else
            {
                ipr_cfg->host_rrq_curr_ptr = ipr_cfg->host_rrq_start_addr;
                ipr_cfg->toggle_bit ^= 1u;
            }
        }

        if (p_host_ioarcb != NULL)
        {
            /* Clear the PCI interrupt */
            writel (IPR_PCII_HOST_RRQ_UPDATED, ipr_cfg->regs.clr_interrupt_reg);

            /* Re-read the PCI interrupt reg to force clear */
            temp_pci_reg = (readl(ipr_cfg->regs.sense_interrupt_reg) & ~temp_mask_reg);
        }
        else
            break;
    }

    if (*pp_sis_cmnd == NULL)
    {
        if (temp_pci_reg & IPR_PCII_IOARCB_XFER_FAILED)
            rc = IPR_RC_XFER_FAILED;
        else if (temp_pci_reg & IPR_PCII_NO_HOST_RRQ)
            rc = IPR_NO_HRRQ;
        else if (temp_pci_reg & IPR_PCII_IOARRIN_LOST)
            rc = IPR_IOARRIN_LOST;
        else if (temp_pci_reg & IPR_PCII_MMIO_ERROR)
            rc = IPR_MMIO_ERROR;
        else if (temp_pci_reg & IPR_PCII_IOA_UNIT_CHECKED)
            rc = IPR_IOA_UNIT_CHECKED;
        else
            rc = IPR_SPURIOUS_INT;

        if (rc != IPR_SPURIOUS_INT)
            ipr_mask_interrupts(p_shared_cfg);

        /* Clear the PCI interrupt */
        writel (temp_pci_reg, ipr_cfg->regs.clr_interrupt_reg);

        /* Re-read the PCI interrupt reg to force clear */
        temp_pci_reg = readl(ipr_cfg->regs.sense_interrupt_reg);
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Generate SCSI sense data based on IOASC
 * Context: Interrupt or task level.
 * Lock State: io_request_lock assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
static void ipr_gen_sense(struct ipr_resource_entry *p_resource,
                             u8 *p_sense_buffer, struct ipr_ioasa *p_ioasa)
{
    u32 ioasc = sistoh32(p_ioasa->ioasc);
    u32 failing_lba;

    memset(p_sense_buffer, 0, IPR_SENSE_BUFFERSIZE);

    if (ipr_is_vset_device(p_resource) &&
        (ioasc == IPR_IOASC_MED_DO_NOT_REALLOC) &&
        (p_ioasa->failing_lba_hi != 0))
    {
        /* Logically Bad and the failing LBA does not fit in 32 bits of LBA */
        /* We must use the new sense data format to report the failing LBA */
        p_sense_buffer[0] = 0x72;
        p_sense_buffer[1] = IPR_IOASC_SENSE_KEY(ioasc);
        p_sense_buffer[2] = IPR_IOASC_SENSE_CODE(ioasc);
        p_sense_buffer[3] = IPR_IOASC_SENSE_QUAL(ioasc);

        p_sense_buffer[7] = 12;
        p_sense_buffer[8] = IPR_SCSI_SENSE_INFO;
        p_sense_buffer[9] = 0x0A;
        p_sense_buffer[10] = 0x80;

        failing_lba = sistoh32(p_ioasa->failing_lba_hi);

        p_sense_buffer[12] = ((failing_lba & 0xff000000) >> 24);
        p_sense_buffer[13] = ((failing_lba & 0x00ff0000) >> 16);
        p_sense_buffer[14] = ((failing_lba & 0x0000ff00) >> 8);
        p_sense_buffer[15] = (failing_lba & 0x000000ff);

        failing_lba = sistoh32(p_ioasa->failing_lba_lo);

        p_sense_buffer[16] = ((failing_lba & 0xff000000) >> 24);
        p_sense_buffer[17] = ((failing_lba & 0x00ff0000) >> 16);
        p_sense_buffer[18] = ((failing_lba & 0x0000ff00) >> 8);
        p_sense_buffer[19] = (failing_lba & 0x000000ff);
    }
    else
    {
        p_sense_buffer[0] = 0x70;
        p_sense_buffer[2] = IPR_IOASC_SENSE_KEY(ioasc);
        p_sense_buffer[12] = IPR_IOASC_SENSE_CODE(ioasc);
        p_sense_buffer[13] = IPR_IOASC_SENSE_QUAL(ioasc);

        /* Illegal request */
        if ((IPR_IOASC_SENSE_KEY(ioasc) == 0x05) &&
            (sistoh32(p_ioasa->ioasc_specific) & IPR_FIELD_POINTER_VALID))
        {
            p_sense_buffer[7] = 10;  /* additional length */

            /* IOARCB was in error */
            if (IPR_IOASC_SENSE_CODE(ioasc) == 0x24)
                p_sense_buffer[15] = 0xC0;
            else /* Parameter data was invalid */
                p_sense_buffer[15] = 0x80;

            p_sense_buffer[16] = ((IPR_FIELD_POINTER_MASK & sistoh32(p_ioasa->ioasc_specific)) >> 8) & 0xff;
            p_sense_buffer[17] = (IPR_FIELD_POINTER_MASK & sistoh32(p_ioasa->ioasc_specific)) & 0xff;
        }
        else
        {
            if ((ioasc == IPR_IOASC_RCV_RECOMMEND_REALLOC) ||
                (ioasc == IPR_IOASC_MED_RECOMMEND_REALLOC) ||
                (ioasc == IPR_IOASC_MED_DO_NOT_REALLOC))
            {
                if (ipr_is_vset_device(p_resource))
                    failing_lba = sistoh32(p_ioasa->failing_lba_lo);
                else
                    failing_lba = sistoh32(p_ioasa->failing_lba_hi);

                p_sense_buffer[0] |= 0x80;  /* Or in the Valid bit */
                p_sense_buffer[3] = ((failing_lba & 0xff000000) >> 24);
                p_sense_buffer[4] = ((failing_lba & 0x00ff0000) >> 16);
                p_sense_buffer[5] = ((failing_lba & 0x0000ff00) >> 8);
                p_sense_buffer[6] = (failing_lba & 0x000000ff);
            }

            p_sense_buffer[7] = 6;  /* additional length */
        }
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Dump contents of IOASA for debug
 * Context: Interrupt or task level.
 * Lock State: io_request_lock assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
static void ipr_dump_ioasa(struct ipr_shared_config *p_shared_cfg,
                              struct ipr_resource_entry *p_resource,
                              struct ipr_ioasa *p_ioasa)
{
    static u8 buffer[1024];
    u32 i = 0;
    u32 j;
    u32 *p_ioasa_data = (u32 *)p_ioasa;
    u16 data_len;
    u32 count = 0;
    u32 ioasc = sistoh32(p_ioasa->ioasc);

    if (0 == ioasc)
        return;

    /* Don't bother dumping sync required IOASAs */
    if (IPR_IOASC_SYNC_REQUIRED == ioasc)
        return;

    if (p_shared_cfg->debug_level < IPR_ADVANCED_DEBUG)
    {
        /* Don't bother dumping selection timeout IOASAs */
        if (IPR_IOASC_HW_SEL_TIMEOUT == ioasc)
            return;

        /* Don't dump recovered errors */
        if (IPR_IOASC_SENSE_KEY(ioasc) < 2)
            return;

        /* Don't log an error if the IOA already logged one */
        if (p_ioasa->ilid != 0)
            return;
    }

    if (sizeof(struct ipr_ioasa) < sistoh16(p_ioasa->ret_stat_len))
        data_len = sizeof(struct ipr_ioasa);
    else
        data_len = sistoh16(p_ioasa->ret_stat_len);

    ipr_beg_err(KERN_ERR);
    ipr_log_err("Device error"IPR_EOL);
    ipr_log_dev_physical_location(p_shared_cfg,
                                     p_resource->resource_address,
                                     KERN_ERR);

    while (i < data_len/4)
    { 
        count += sprintf(buffer + count, KERN_ERR""IPR_ERR": ioasa[0x%02x] ",i*4);
        for (j=0; (j < 4) && (i < data_len/4); j++,i++)
            count += sprintf(buffer + count, "%08x ",sistoh32(p_ioasa_data[i]));
        count += sprintf(buffer + count,IPR_EOL);
        if (count > (1024 - 128)) break; /* be sure not to exceed buffer limit */
    }

    printk(buffer);
    ipr_end_err(KERN_ERR);
}

/*---------------------------------------------------------------------------
 * Purpose: Queue an op to the IOA Focal Point
 * Context: Task or interrupt level.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
int ipr_ioa_queue(struct ipr_shared_config *p_shared_cfg, struct ipr_ccb *p_sis_cmd)
{
    struct ipr_host_ioarcb* p_host_ioarcb;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    p_host_ioarcb = ipr_build_ioa_cmd(p_shared_cfg,
                                         p_sis_cmd->cdb[0],
                                         p_sis_cmd,
                                         0,
                                         p_sis_cmd->buffer,
                                         p_sis_cmd->buffer_dma,
                                         p_sis_cmd->bufflen);

    if (p_host_ioarcb != NULL)
    {
        ipr_trc_hook(p_shared_cfg,
                        p_sis_cmd->cdb[0],
                        IPR_TRACE_START,
                        IPR_TRACE_IOA,
                        p_host_ioarcb->host_ioarcb_index,
                        p_sis_cmd->bufflen,
                        IPR_IOA_RESOURCE_ADDRESS);

        writel(sistoh32(p_host_ioarcb->ioarcb.ioarcb_host_pci_addr),
                      ipr_cfg->regs.ioarrin_reg);
    }
    else
        return IPR_RC_OP_NOT_SENT;

    return IPR_RC_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Send a request sense to a GPDD
 * Context: Task level.
 * Lock State: io_request_lock assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
void ipr_auto_sense(struct ipr_shared_config *p_shared_cfg,
                       struct ipr_ccb *p_sis_cmd)
{
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_cmd_pkt *p_cmd_pkt;
    struct ipr_ioadl_desc *p_ioadl;
    u8 *p_cdb;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_resource_entry *p_resource = p_sis_cmd->p_resource;

    p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);
    p_ioarcb = &p_host_ioarcb->ioarcb;
    p_host_ioarcb->p_sis_cmd = p_sis_cmd;
    p_ioadl = p_host_ioarcb->p_ioadl;
    p_cmd_pkt = &p_ioarcb->ioarcb_cmd_pkt;
    p_cdb = p_cmd_pkt->cdb;
    p_ioarcb->ioa_res_handle = p_resource->resource_handle;
    p_cmd_pkt->reserved = 0;

    /* Send off a request sense */
    p_cdb[0] = IPR_REQUEST_SENSE;
    p_cdb[1] = p_cdb[2] = p_cdb[3] = p_cdb[5] = 0;
    p_cdb[4] = IPR_SENSE_BUFFERSIZE;
    p_cmd_pkt->cmd_sync_override = 1;
    p_cmd_pkt->no_underlength_checking = 1;
    p_cmd_pkt->write_not_read = 0;

    p_cmd_pkt->cmd_timeout = htosis16(IPR_REQUEST_SENSE_TIMEOUT);

    p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);

    p_ioadl->flags_and_data_len = 
        htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | IPR_SENSE_BUFFERSIZE);
    p_ioadl->address = htosis32((u32)p_sis_cmd->sense_buffer_dma);

    p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
    p_ioarcb->read_data_transfer_length = htosis32(IPR_SENSE_BUFFERSIZE);

    memset(p_host_ioarcb->p_ioasa, 0, sizeof(struct ipr_ioasa));

    ipr_trc_hook(p_shared_cfg,
                    p_cdb[0],
                    IPR_TRACE_START,
                    IPR_TRACE_GEN,
                    p_host_ioarcb->host_ioarcb_index,
                    IPR_SENSE_BUFFERSIZE,
                    IPR_GET_PHYSICAL_LOCATOR(p_resource->resource_address));

    /* Poke the IOARRIN with the PCI address of the IOARCB */
    writel(sistoh32(p_ioarcb->ioarcb_host_pci_addr),
                  ipr_cfg->regs.ioarrin_reg);
}

/*---------------------------------------------------------------------------
 * Purpose: Queue an op to a device
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
int ipr_queue_internal(struct ipr_shared_config *p_shared_cfg,
                          struct ipr_ccb *p_sis_cmd)
{
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioadl_desc *p_ioadl;
    unsigned char *p_sense_buffer;
    u8 cmnd;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_resource_entry *p_resource = p_sis_cmd->p_resource;

    cmnd = p_sis_cmd->cdb[0];

    /* Are we read/write protecting the device? */
    if (p_resource->rw_protected)
    {
        if ((cmnd == IPR_READ_6) || (cmnd == IPR_WRITE_6) ||
            (cmnd == IPR_READ_10) || (cmnd == IPR_WRITE_10) ||
            (cmnd == IPR_WRITE_VERIFY) || (cmnd == IPR_READ_16) ||
            (cmnd == IPR_WRITE_16) || (cmnd == IPR_WRITE_VERIFY_16))
        {
            /* Device is blocked and op is a read or write */
            /* Send back a data protect error */
            p_sense_buffer = p_sis_cmd->sense_buffer;
            memset(p_sense_buffer, 0, IPR_SENSE_BUFFERSIZE);

            p_sense_buffer[0] = 0xf0;
            p_sense_buffer[2] = 0x07;
            p_sense_buffer[3] = p_sis_cmd->cdb[2];
            p_sense_buffer[4] = p_sis_cmd->cdb[3];
            p_sense_buffer[5] = p_sis_cmd->cdb[4];
            p_sense_buffer[6] = p_sis_cmd->cdb[5];
            p_sense_buffer[7] = 6;
            p_sense_buffer[12] = 0x27;
            p_sense_buffer[13] = 0x00;

            return IPR_RC_OP_NOT_SENT;
        }
    }

    p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);

    p_ioarcb = &p_host_ioarcb->ioarcb;
    p_ioadl = p_host_ioarcb->p_ioadl;
    p_host_ioarcb->p_sis_cmd = p_sis_cmd;

    p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
    p_ioarcb->ioa_res_handle = p_resource->resource_handle;

    memcpy(p_ioarcb->ioarcb_cmd_pkt.cdb, p_sis_cmd->cdb, IPR_CCB_CDB_LEN);

    /* Is this a GPDD op? */
    if (!p_resource->is_af)
    {
        ipr_trc_hook(p_shared_cfg,
                        cmnd,
                        IPR_TRACE_START,
                        IPR_TRACE_GEN,
                        p_host_ioarcb->host_ioarcb_index,
                        p_sis_cmd->bufflen,
                        IPR_GET_PHYSICAL_LOCATOR(p_resource->resource_address));

        if (p_sis_cmd->flags & IPR_IOA_CMD)
        {
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioarcb->ioarcb_cmd_pkt.reserved = 0;
            p_ioarcb->ioarcb_cmd_pkt.cmd_timeout = 0;
        }
        else
            p_ioarcb->ioarcb_cmd_pkt.cmd_timeout = htosis16(p_sis_cmd->timeout);

        /* Setup the IOADL */
        ipr_build_ioadl(p_shared_cfg, p_host_ioarcb);

        if (p_sis_cmd->data_direction == IPR_DATA_WRITE)
            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;

        if (p_sis_cmd->underflow == 0)
            p_ioarcb->ioarcb_cmd_pkt.no_underlength_checking = 1;

        if (p_sis_cmd->flags & IPR_CMD_SYNC_OVERRIDE)
            p_ioarcb->ioarcb_cmd_pkt.cmd_sync_override = 1;
    }
    else /* AF DASD or VSET op */
    {
        ipr_trc_hook(p_shared_cfg,
                        cmnd,
                        IPR_TRACE_START,
                        IPR_TRACE_DASD,
                        p_host_ioarcb->host_ioarcb_index,
                        p_sis_cmd->bufflen,
                        IPR_GET_PHYSICAL_LOCATOR(p_resource->resource_address));

        if ((cmnd == IPR_READ_10) ||
            (cmnd == IPR_WRITE_10) ||
            (cmnd == IPR_WRITE_VERIFY) ||
            (cmnd == IPR_READ_16) ||
            (cmnd == IPR_WRITE_16) ||
            (cmnd == IPR_WRITE_VERIFY_16))
        {
            if (p_sis_cmd->bufflen > IPR_MAX_OP_SIZE)
                panic(IPR_ERR": Too large of an op issued!!!"IPR_EOL);

            ipr_build_ioadl(p_shared_cfg, p_host_ioarcb);
        }
        else
        {
            if (p_sis_cmd->flags & IPR_IOA_CMD)
                p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

            ipr_build_ioadl(p_shared_cfg, p_host_ioarcb);
        }
    }

    writel(sistoh32(p_ioarcb->ioarcb_host_pci_addr),
           ipr_cfg->regs.ioarrin_reg);

    return 0;
}

/*---------------------------------------------------------------------------
 * Purpose: Return the model number for a given device
 * Lock State: io_request_lock assumed to be held
 * Returns: DASD model number
 * Note: If updating this, you must also update the same function in sisconfig
 *---------------------------------------------------------------------------*/
static u32 ipr_get_model(struct ipr_config_table_entry *p_cfgte)
{
    u32 model_number;

    if (p_cfgte->is_ioa_resource)
        return 1;

    if (p_cfgte->is_array_member)
    {
        model_number = 70;

        switch (IPRLIB_GET_CAP_REDUCTION(*p_cfgte))
        {
            case IPR_HALF_REDUCTION:
                model_number += 8;
                break;
            case IPR_QUARTER_REDUCTION:
                model_number += 4;
                break;
            case IPR_EIGHTH_REDUCTION:
                model_number += 2;
                break;
            case IPR_SIXTEENTH_REDUCTION:
                model_number += 1;
                break;
            case IPR_UNKNOWN_REDUCTION:
                model_number += 9;
                break;
        }
    }
    else if (p_cfgte->is_hot_spare)
        model_number = IPR_HOST_SPARE_MODEL;
    else if (p_cfgte->subtype == IPR_SUBTYPE_AF_DASD)
        model_number = 50;
    else if (p_cfgte->subtype == IPR_SUBTYPE_GENERIC_SCSI)
        model_number = 20;
    else /* Volume set resource */
        model_number = IPR_VSET_MODEL_NUMBER;

    if (p_cfgte->is_compressed)
        model_number += 10;

    return model_number;
}

/*---------------------------------------------------------------------------
 * Purpose: Do CCN processing
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_handle_config_change(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_hostrcb *p_hostrcb)
{
    struct ipr_resource_entry *p_resource_entry = NULL;
    struct ipr_resource_dll *p_resource_dll;
    struct ipr_config_table_entry *p_cfgte_buf;
    struct ipr_lun *p_lun;
    u8  bus_num;
    u32 dev_changed = 0;
    u32 is_ndn = 1;

    p_cfgte_buf = &((struct ipr_hostrcb_cfg_ch_not_bin *)
                    &p_hostrcb->data.ccn)->cfgte;

    for (p_resource_dll = p_shared_cfg->rsteUsedH;
         p_resource_dll != NULL;
         p_resource_dll = p_resource_dll->next)
    {
        p_resource_entry = &p_resource_dll->data;

        if (p_resource_entry->resource_handle == p_cfgte_buf->resource_handle)
        {
            is_ndn = 0;

            if (!ipr_is_res_addr_valid(&p_cfgte_buf->resource_address))
            {
                /* invalid resource address, log message and ignore entry */
                ipr_log_err("Invalid resource address reported: 0x%08X"IPR_EOL,
                               IPR_GET_PHYSICAL_LOCATOR(p_cfgte_buf->resource_address));

                ipr_send_hcam(p_shared_cfg,
                                 IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                                 p_hostrcb);
                return;
            }

            bus_num = p_cfgte_buf->resource_address.bus + 1;
            p_lun = &p_shared_cfg->bus[bus_num].
                target[p_cfgte_buf->resource_address.target].
                lun[p_cfgte_buf->resource_address.lun];

            p_lun->p_resource_entry = p_resource_entry;

            if ((p_resource_entry->is_array_member !=
                 p_cfgte_buf->is_array_member) ||
                (p_resource_entry->is_compressed !=
                 p_cfgte_buf->is_compressed) ||
                (p_lun->expect_ccm))
            {
                dev_changed = 1;
            }
            break;
        }
    }

    if (is_ndn)
    {
        /* check if resource address is valid */
        if (!ipr_is_res_addr_valid(&p_cfgte_buf->resource_address))
        {
            /* invalid resource address, log message and ignore entry */
            ipr_log_err("Invalid resource address reported: 0x%08X"IPR_EOL,
                           IPR_GET_PHYSICAL_LOCATOR(p_cfgte_buf->resource_address));

            ipr_send_hcam(p_shared_cfg,
                             IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                             p_hostrcb);
            return;
        }

        p_resource_dll = ipr_get_resource_entry(p_shared_cfg);

        if (p_resource_dll == NULL)
        {
            ipr_send_hcam(p_shared_cfg,
                             IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                             p_hostrcb);
            return;
        }

        p_resource_entry = &p_resource_dll->data;

        bus_num = p_cfgte_buf->resource_address.bus + 1;
        p_lun = &p_shared_cfg->bus[bus_num].
            target[p_cfgte_buf->resource_address.target].
            lun[p_cfgte_buf->resource_address.lun];

        p_lun->is_valid_entry = 1;
        p_lun->p_resource_entry = p_resource_entry;
        p_lun->stop_new_requests = 0;
        p_lun->dev_changed = 1;

        dev_changed = 1;
    }

    if (p_hostrcb->notificationType == IPR_HOST_RCB_NOTIF_TYPE_REM_ENTRY)
    {
        p_resource_entry = &p_resource_dll->data;

        bus_num = p_resource_entry->resource_address.bus + 1;
        p_lun = &p_shared_cfg->bus[bus_num].
            target[p_resource_entry->resource_address.target].
            lun[p_resource_entry->resource_address.lun];

        p_lun->is_valid_entry = 0;
        p_lun->p_resource_entry = NULL;
        p_lun->stop_new_requests = 0;
        p_lun->dev_changed = 0;

        ipr_put_resource_entry(p_shared_cfg, p_resource_dll);
        ipr_send_hcam(p_shared_cfg,
                         IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                         p_hostrcb);
    }
    else
    {
        ipr_check_backplane(p_shared_cfg, p_cfgte_buf);
        ipr_update_resource(p_shared_cfg,
                               p_resource_entry,
                               p_cfgte_buf,
                               dev_changed);
        ipr_init_single_dev_runtime(p_shared_cfg,
                                       p_resource_entry,
                                       is_ndn, p_hostrcb);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Update the given device resource
 * Lock State: io_request_lock assumed to be held
 * Returns: None
 *---------------------------------------------------------------------------*/
static void ipr_update_resource(struct ipr_shared_config *p_shared_cfg,
                                   struct ipr_resource_entry *p_resource_entry,
                                   struct ipr_config_table_entry *p_cfgte,
                                   u32 device_changed)
{
    const struct ipr_dev_config *p_dev_cfg;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    char level[2];

    p_resource_entry->is_ioa_resource = p_cfgte->is_ioa_resource;
    p_resource_entry->is_compressed = p_cfgte->is_compressed;
    p_resource_entry->is_array_member = p_cfgte->is_array_member;
    p_resource_entry->is_hot_spare = p_cfgte->is_hot_spare;
    p_resource_entry->subtype = p_cfgte->subtype;
    p_resource_entry->resource_address.bus = p_cfgte->resource_address.bus;
    p_resource_entry->resource_address.target = p_cfgte->resource_address.target;
    p_resource_entry->resource_address.lun = p_cfgte->resource_address.lun;
    level[0] = p_cfgte->service_level;
    level[1] = '\0';
    p_resource_entry->level = simple_strtoul(level, NULL, 16);
    p_resource_entry->array_id = p_cfgte->array_id;
    p_resource_entry->type = ipr_dasd_vpids_to_ccin(&p_cfgte->std_inq_data.vpids, 0x6600);
    p_resource_entry->model = ipr_get_model(p_cfgte);
    memcpy(p_resource_entry->serial_num, p_cfgte->std_inq_data.serial_num,
           IPR_SERIAL_NUM_LEN);
    p_resource_entry->serial_num[IPR_SERIAL_NUM_LEN] = '\0';
    p_resource_entry->resource_handle = p_cfgte->resource_handle;
    p_resource_entry->host_no = p_shared_cfg->host_no;
    p_resource_entry->std_inq_data = p_cfgte->std_inq_data;

    ipr_update_location_data(p_shared_cfg, p_resource_entry);

    p_resource_entry->is_hidden = 0;

    if (ipr_is_af_dasd_device(p_resource_entry) ||
        IPR_IS_SES_DEVICE(p_resource_entry->std_inq_data))
    {
        p_resource_entry->is_hidden = 1;
    }

    p_resource_entry->is_af =
        IPR_IS_DASD_DEVICE(p_resource_entry->std_inq_data) &&
        (!p_resource_entry->is_ioa_resource) &&
        ((p_resource_entry->subtype == IPR_SUBTYPE_AF_DASD) ||
         (p_resource_entry->subtype == IPR_SUBTYPE_VOLUME_SET));

    p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_dev_cfg != NULL)
    {
        p_resource_entry->rw_protected = (p_dev_cfg->is_15k_device && ipr_cfg->non15k_ses);
        p_resource_entry->format_allowed = (!p_dev_cfg->is_15k_device || !ipr_cfg->non15k_ses);
    }
    else
    {
        p_resource_entry->rw_protected = 0;
        p_resource_entry->format_allowed = 1;
    }

    if (p_resource_entry->is_ioa_resource)
    {
        p_resource_entry->type = p_shared_cfg->ccin;
        p_resource_entry->nr_ioa_microcode = p_shared_cfg->nr_ioa_microcode;
        p_shared_cfg->ioa_resource = *p_resource_entry;
    }

    if (device_changed)
    {
        p_resource_entry->dev_changed = 1;
        if (p_resource_entry->rw_protected)
            ipr_log_config_error(p_shared_cfg, 0, p_resource_entry);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Log a configuration error - unsupported attribute
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_log_config_error(struct ipr_shared_config *p_shared_cfg,
                                    int is_ipl, struct ipr_resource_entry *p_resource)
{
    struct ipr_resource_entry *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;
    int found = 0;

    if (is_ipl)
    {
        for (p_resource_dll = p_shared_cfg->rsteUsedH;
             p_resource_dll != NULL;
             p_resource_dll = p_resource_dll->next)
        {
            p_resource_entry = &p_resource_dll->data;

            if (p_resource_entry->is_af &&
                p_resource_entry->rw_protected)
            {
                if (found == 0)
                {
                    ipr_beg_err(KERN_ERR);
                    ipr_log_err("Configuration Error. The following devices are"IPR_EOL);
                    ipr_log_err("not supported in this hardware configuration"IPR_EOL);
                    ipr_log_err("Refer to the appropriate service documents"IPR_EOL);
                    ipr_log_err("%-45sSerial #"IPR_EOL, "Device");
                }

                found = 1;
                ipr_print_dev(p_shared_cfg, p_resource_entry);
            }
        }

        if (found)
            ipr_end_err(KERN_ERR);
    }
    else
    {
        ipr_beg_err(KERN_ERR);
        ipr_log_err("Configuration Error. The following devices are"IPR_EOL);
        ipr_log_err("not supported in this hardware configuration"IPR_EOL);
        ipr_log_err("Refer to the appropriate service documents"IPR_EOL);
        ipr_log_err("%-45sSerial #"IPR_EOL, "Device");

        ipr_print_dev(p_shared_cfg, p_resource);
        ipr_end_err(KERN_ERR);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Print out a string describing a given device
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_print_dev(struct ipr_shared_config *p_shared_cfg,
                             struct ipr_resource_entry *p_resource)
{
    char line[100];
    u32 size, len;
    char dev_loc_str[IPR_MAX_LOCATION_LEN];

    ipr_dev_loc_str(p_shared_cfg, p_resource, dev_loc_str);

    size = 0;

    len = sprintf(line, "%-45s", dev_loc_str);
    size += len;
    len = sprintf(line + size, "%-11s", p_resource->serial_num);
    size += len;

    ipr_log_err("%s"IPR_EOL, line);
}

/*---------------------------------------------------------------------------
 * Purpose: Prepares devices to run
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Op completed sucessfully
 *          IPR_RC_FAILED            - Op failed
 *          IPR_RC_TIMEOUT           - Op timed out
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *---------------------------------------------------------------------------*/
static u32 ipr_init_devices(struct ipr_shared_config *p_shared_cfg)
{
    u32 rc = IPR_RC_SUCCESS;
    struct ipr_resource_entry *p_resource_entry;
    struct ipr_resource_dll *p_resource_dll;

    /* Loop through all the devices */
    for (p_resource_dll = p_shared_cfg->rsteUsedH;
         p_resource_dll != NULL;
         p_resource_dll = p_resource_dll->next)
    {
        p_resource_entry = &p_resource_dll->data;

        rc = ipr_init_single_dev(p_shared_cfg, p_resource_entry);

        /* If we get a completion other than success and other than
         failed, there is a serious problem with the adapter and
         we want to stop talking to it */
        if ((rc != IPR_RC_SUCCESS) && (rc != IPR_RC_FAILED))
        {
            ipr_trace;
            return rc;
        }
    }

    return IPR_RC_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Initialize the supported device structure to default values.
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *--------------------------------------------------------------------------*/
static void ipr_set_sup_dev_dflt(struct ipr_supported_device
                                    *p_supported_device,
                                    struct ipr_std_inq_vpids *vpids)
{
    memset(p_supported_device, 0,
           sizeof(struct ipr_supported_device));
    memcpy(&p_supported_device->vpids, vpids,
           sizeof(struct ipr_std_inq_vpids));
}

/*---------------------------------------------------------------------------
 * Purpose: Initialize a single device
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Op completed sucessfully
 *          IPR_RC_FAILED            - Op failed
 *          IPR_RC_TIMEOUT           - Op timed out
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *---------------------------------------------------------------------------*/
static u32 ipr_init_single_dev(struct ipr_shared_config *p_shared_cfg,
                                  struct ipr_resource_entry *p_resource_entry)
{
    struct ipr_dasd_timeouts *p_dasd_timeouts;
    ipr_dma_addr dasd_timeouts_dma;
    struct ipr_mode_parm_hdr *p_mode_parm, *p_changeable_pages;
    struct ipr_query_res_state *p_res_query;
    ipr_dma_addr mode_pages_dma, changeable_pages_dma, page3_inq_dma, res_query_dma;
    struct ipr_dasd_inquiry_page3 *p_page3_inq;
    u32 rc = IPR_RC_SUCCESS;
    u8 alloc_len;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_std_inq_data_long *p_std_inq;
    struct ipr_ssd_header *p_ssd_header;
    ipr_dma_addr ssd_header_dma, std_inq_dma;
    u32 i, in_ssd_list;
    struct ipr_supported_device *p_supported_device;

    p_dasd_timeouts = &ipr_cfg->p_dasd_init_buf[0]->dasd_timeouts;
    dasd_timeouts_dma = ipr_cfg->p_dasd_init_buf[0]->dasd_timeouts_dma;

    memcpy(p_dasd_timeouts->record, ipr_dasd_timeout_list,
           sizeof(ipr_dasd_timeout_list));
    p_dasd_timeouts->length = htosis32(sizeof(ipr_dasd_timeout_list)
                                       + sizeof(u32));
    p_mode_parm = &ipr_cfg->p_dasd_init_buf[0]->mode_pages;
    mode_pages_dma = ipr_cfg->p_dasd_init_buf[0]->mode_pages_dma;
    p_changeable_pages = &ipr_cfg->p_dasd_init_buf[0]->changeable_parms;
    changeable_pages_dma = ipr_cfg->p_dasd_init_buf[0]->changeable_parms_dma;
    p_std_inq = &ipr_cfg->p_dasd_init_buf[0]->std_inq;
    std_inq_dma = ipr_cfg->p_dasd_init_buf[0]->std_inq_dma;
    p_ssd_header = &ipr_cfg->p_dasd_init_buf[0]->ssd_header;
    ssd_header_dma = ipr_cfg->p_dasd_init_buf[0]->ssd_header_dma;
    p_page3_inq = &ipr_cfg->p_dasd_init_buf[0]->page3_inq;
    page3_inq_dma = ipr_cfg->p_dasd_init_buf[0]->page3_inq_dma;
    p_res_query = &ipr_cfg->p_dasd_init_buf[0]->res_query;
    res_query_dma = ipr_cfg->p_dasd_init_buf[0]->res_query_dma;

    if (ipr_is_af_dasd_device(p_resource_entry))
    { /* If this is a DASD */

        memset(p_std_inq, 0, sizeof(struct ipr_std_inq_data_long));

        /* Send Inquiry */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry,
                                      std_inq_dma,
                                      IPR_INQUIRY, 0xff,
                                      sizeof(struct ipr_std_inq_data_long));

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        if (p_std_inq->std_inq_data.version >= 4)
            p_resource_entry->supports_qas = p_std_inq->qas;
        else
            p_resource_entry->supports_qas = 0;

        /* Fill in additional VPD information */
        memcpy(p_resource_entry->part_number, p_std_inq->part_number,
               IPR_STD_INQ_PART_NUM_LEN);
        p_resource_entry->part_number[IPR_STD_INQ_PART_NUM_LEN] = '\0';

        memcpy(p_resource_entry->ec_level, p_std_inq->ec_level,
               IPR_STD_INQ_EC_LEVEL_LEN);
        p_resource_entry->ec_level[IPR_STD_INQ_EC_LEVEL_LEN] = '\0';

        memcpy(p_resource_entry->fru_number, p_std_inq->fru_number,
               IPR_STD_INQ_FRU_NUM_LEN);
        p_resource_entry->fru_number[IPR_STD_INQ_FRU_NUM_LEN] = '\0';

        memcpy(p_resource_entry->z1_term, p_std_inq->z1_term,
               IPR_STD_INQ_Z1_TERM_LEN);
        p_resource_entry->z1_term[IPR_STD_INQ_Z1_TERM_LEN] = '\0';

        memcpy(p_resource_entry->z2_term, p_std_inq->z2_term,
               IPR_STD_INQ_Z2_TERM_LEN);
        p_resource_entry->z2_term[IPR_STD_INQ_Z2_TERM_LEN] = '\0';

        memcpy(p_resource_entry->z3_term, p_std_inq->z3_term,
               IPR_STD_INQ_Z3_TERM_LEN);
        p_resource_entry->z3_term[IPR_STD_INQ_Z3_TERM_LEN] = '\0';

        memcpy(p_resource_entry->z4_term, p_std_inq->z4_term,
               IPR_STD_INQ_Z4_TERM_LEN);
        p_resource_entry->z4_term[IPR_STD_INQ_Z4_TERM_LEN] = '\0';

        memcpy(p_resource_entry->z5_term, p_std_inq->z5_term,
               IPR_STD_INQ_Z5_TERM_LEN);
        p_resource_entry->z5_term[IPR_STD_INQ_Z5_TERM_LEN] = '\0';

        memcpy(p_resource_entry->z6_term, p_std_inq->z6_term,
               IPR_STD_INQ_Z6_TERM_LEN);
        p_resource_entry->z6_term[IPR_STD_INQ_Z6_TERM_LEN] = '\0';

        /* Check if device in default ssd list */
        for (i = 0,
             in_ssd_list = 0;
             i < sizeof(ipr_supported_dev_list)/
                 sizeof(struct ipr_supported_device);
             i++)
        {
            if (memcmp(&ipr_supported_dev_list_ptr[i].vpids,
                              &p_std_inq->std_inq_data.vpids,
                              sizeof(struct ipr_std_inq_vpids)) == 0)
            {
                in_ssd_list = 1;
                break;
            }
        }

        /* Send Set Supported Devices command only if device
         is not in Set Supported Devices table. Sending it down all
         the time would break the 15K blocking code. */
        if (!in_ssd_list)
        {
            /* Send Set Supported Device */
            p_ssd_header->num_records = 1;
            p_ssd_header->data_length =
                htosis16(sizeof(struct ipr_supported_device) +
                         sizeof(struct ipr_ssd_header));
            p_ssd_header->reserved = 0;

            p_supported_device = &ipr_cfg->p_dasd_init_buf[0]->supported_device;

            ipr_set_sup_dev_dflt(p_supported_device,
                                    &p_std_inq->std_inq_data.vpids);

            rc = ipr_send_blocking_cmd(p_shared_cfg,
                                          IPR_SET_SUPPORTED_DEVICES,
                                          IPR_SET_SUP_DEVICE_TIMEOUT,
                                          0, p_ssd_header,
                                          ssd_header_dma,
                                          sistoh16(p_ssd_header->data_length));
            if (rc != IPR_RC_SUCCESS)
            {
                ipr_trace;
                goto leave;
            }
        }

        /* Issue a Set DASD Timeouts */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry,
                                      dasd_timeouts_dma,
                                      IPR_SET_DASD_TIMEOUTS, 0,
                                      sistoh32(p_dasd_timeouts->length));

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        memset(p_page3_inq, 0, sizeof(struct ipr_dasd_inquiry_page3));

        /* Issue a page 3 inquiry for software VPD */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry, page3_inq_dma,
                                      IPR_INQUIRY, 3, sizeof(struct ipr_dasd_inquiry_page3));

        if (rc == IPR_RC_SUCCESS)
        {
            /* Fill in the software load id and release level */
            p_resource_entry->sw_load_id =
                (p_page3_inq->load_id[0] << 24) |
                (p_page3_inq->load_id[1] << 16) |
                (p_page3_inq->load_id[2] << 8) |
                (p_page3_inq->load_id[3]);

            p_resource_entry->sw_release_level =
                (p_page3_inq->release_level[0] << 24) |
                (p_page3_inq->release_level[1] << 16) |
                (p_page3_inq->release_level[2] << 8) |
                (p_page3_inq->release_level[3]);
        }

        memset(p_mode_parm, 0, 255);

        /* Issue a mode sense to get current mode pages */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry, mode_pages_dma,
                                      IPR_MODE_SENSE, 0x3f, 255);

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        memset(p_changeable_pages, 0, 255);

        /* Issue mode sense to get the changeable parms */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry, changeable_pages_dma,
                                      IPR_MODE_SENSE, 0x7f, 255);

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        /* Modify mode pages */
        alloc_len = ipr_set_mode_pages(p_shared_cfg, p_resource_entry,
                                          p_mode_parm, p_changeable_pages);

        /* Issue mode select */
        rc = ipr_blocking_dasd_cmd(p_shared_cfg, p_resource_entry, mode_pages_dma,
                                      IPR_MODE_SELECT, 0x11, alloc_len);

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }
    }
    else if (ipr_is_vset_device(p_resource_entry))
    {
        /* Issue start unit */
        rc = ipr_blocking_vset_cmd(p_shared_cfg, p_resource_entry,
                                      0, IPR_START_STOP,
                                      IPR_START_STOP_START, 0);

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        /* Issue query resource state */
        rc = ipr_blocking_vset_cmd(p_shared_cfg, p_resource_entry,
                                      res_query_dma, IPR_QUERY_RESOURCE_STATE,
                                      0, sizeof(struct ipr_query_res_state));

        if (rc != IPR_RC_SUCCESS)
        {
            ipr_trace;
            goto leave;
        }

        p_resource_entry->model = IPR_VSET_MODEL_NUMBER +
            simple_strtoul(p_res_query->protection_level_str, NULL, 10);
    }

    leave:
        return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Initialize a single device - first job step
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Failure
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
static u32 ipr_init_single_dev_runtime(struct ipr_shared_config *p_shared_cfg,
                                          struct ipr_resource_entry *p_resource_entry,
                                          u32 is_ndn,
                                          struct ipr_hostrcb *p_hostrcb)
{
    struct ipr_ccb *p_sis_ccb;
    u32 rc = IPR_RC_SUCCESS;
    struct ipr_dasd_init_bufs *p_dasd_init_buf;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    if (ipr_is_af_dasd_device(p_resource_entry) ||
        ipr_is_vset_device(p_resource_entry))
    { /* If this is a AF DASD or VSET */

        if (p_resource_entry->in_init)
        {
            /* We got another config change for this device while we were in
             our bringup job. This will simply force us to start over from the beginning */
            p_resource_entry->redo_init = 1;
            ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                             p_hostrcb);
        }
        else
        {
            p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

            if (p_sis_ccb == NULL)
            {
                /* Requests must not be allowed right now - we are probably going through
                 reset/reload right now */
                ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                                 p_hostrcb);
                return IPR_RC_FAILED;
            }

            p_dasd_init_buf = ipr_get_dasd_init_buffer(ipr_cfg);

            if (p_dasd_init_buf == NULL)
            {
                /* This should be dead code - we should not be able to hit this */
                ipr_log_err("Failed to allocate dasd init buffer"IPR_EOL);
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);
                ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                                 p_hostrcb);
                return IPR_RC_FAILED;
            }

            p_resource_entry->in_init = 1;
            p_sis_ccb->p_resource = p_resource_entry;
            p_sis_ccb->p_scratch = p_dasd_init_buf;
            p_dasd_init_buf->p_hostrcb = p_hostrcb;
            p_dasd_init_buf->p_dev_res = p_resource_entry;

            if (ipr_is_af_dasd_device(p_resource_entry))
            {
                p_sis_ccb->job_step = IPR_DINIT_START;
                ipr_dasd_init_job(p_shared_cfg, p_sis_ccb);
            }
            else
            {
                p_sis_ccb->job_step = IPR_VINIT_START;
                ipr_vset_init_job(p_shared_cfg, p_sis_ccb);
            }

        }
    }
    else if
        (IPR_IS_SES_DEVICE(p_resource_entry->std_inq_data) && is_ndn &&
         (ipr_arch == IPR_ARCH_ISERIES))
    {
        rc = ipr_build_slot_map_runtime(p_shared_cfg, p_resource_entry,
                                           p_hostrcb);
    }
    else
    {
        ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                         p_hostrcb);
    }

    return rc;
}

#ifdef IPR_DEBUG_MODE_PAGES
/*---------------------------------------------------------------------------
 * Purpose: Prints out a mode sense buffer for debug purposes
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_print_mode_sense_buffer(struct ipr_mode_parm_hdr *p_mode_parm)
{
    int i;
    static u8 buffer[4096];
    u8 *p_buf = (u8*)p_mode_parm;
    u32 count = 0;

    for (i = 0; i < p_mode_parm->length + 1; i++)
        count += sprintf(buffer + count, " %02x", p_buf[i]);
    printk("Mode sense buffer: %s"IPR_EOL, buffer);
}
#endif

/*---------------------------------------------------------------------------
 * Purpose: Send an op to a DASD resource and sleep until its completion
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Op completed sucessfully
 *          IPR_RC_FAILED            - Op failed
 *          IPR_RC_TIMEOUT           - Op timed out
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *---------------------------------------------------------------------------*/
static int ipr_blocking_dasd_cmd(struct ipr_shared_config *p_shared_cfg,
                                    struct ipr_resource_entry *p_resource_entry,
                                    ipr_dma_addr dma_buffer,
                                    u8 cmd, u8 parm, u16 alloc_len)
{
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioadl_desc *p_ioadl;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    int rc = IPR_RC_SUCCESS;
    int timeout = IPR_INTERNAL_DEV_TIMEOUT;
    char cmd_str[50];

    p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);
    p_ioarcb = &p_host_ioarcb->ioarcb; 
    p_ioadl = p_host_ioarcb->p_ioadl;
    p_ioarcb->ioarcb_cmd_pkt.cdb[0] = cmd;
    p_ioarcb->ioa_res_handle = p_resource_entry->resource_handle;
    p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

    switch(cmd)
    {
        case IPR_MODE_SENSE:
            p_ioarcb->ioarcb_cmd_pkt.cdb[2] = parm; 
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = alloc_len;
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | alloc_len);
            p_ioadl->address = htosis32((u32)dma_buffer);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(alloc_len);
            strcpy(cmd_str, "Mode Sense");
            break;
        case IPR_MODE_SELECT:
            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;
            p_ioarcb->ioarcb_cmd_pkt.cdb[1] = parm;
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = alloc_len;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | alloc_len);
            p_ioadl->address = htosis32((u32)dma_buffer);
            p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->write_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->write_data_transfer_length = htosis32(alloc_len);
            strcpy(cmd_str, "Mode Select");
            break;
        case IPR_SET_DASD_TIMEOUTS:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | alloc_len);
            p_ioadl->address = htosis32((u32)dma_buffer);
            p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->write_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->write_data_transfer_length = htosis32(alloc_len);
            p_ioarcb->ioarcb_cmd_pkt.cdb[7] = (alloc_len >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[8] = alloc_len & 0xff;
            strcpy(cmd_str, "Set Dasd Timeouts");
            break;
        case IPR_INQUIRY:
            if (parm != 0xff)
            {
                p_ioarcb->ioarcb_cmd_pkt.cdb[1] = 0x01;
                p_ioarcb->ioarcb_cmd_pkt.cdb[2] = parm; 
            }
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = alloc_len;
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | alloc_len);
            p_ioadl->address = htosis32((u32)dma_buffer);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(alloc_len);
            strcpy(cmd_str, "Inquiry");
            break;
        default:
            panic(IPR_ERR": Invalid blocking dasd command 0x%02X"IPR_EOL, cmd);
            break;
    };

    ipr_trc_hook(p_shared_cfg,
                    cmd,
                    IPR_TRACE_START,
                    IPR_TRACE_DASD,
                    p_host_ioarcb->host_ioarcb_index,
                    alloc_len,
                    IPR_GET_PHYSICAL_LOCATOR(p_resource_entry->resource_address));

    /* Poke the IOARRIN with the PCI address of the IOARCB */
    writel(sistoh32(p_ioarcb->ioarcb_host_pci_addr), ipr_cfg->regs.ioarrin_reg);

    rc = ipr_poll_isr(p_shared_cfg, timeout);

    ipr_trc_hook(p_shared_cfg,
                    cmd,
                    IPR_TRACE_FINISH,
                    IPR_TRACE_DASD,
                    p_host_ioarcb->host_ioarcb_index,
                    alloc_len,
                    sistoh32(p_host_ioarcb->p_ioasa->ioasc));

    if (rc == IPR_RC_TIMEOUT)
    {
        ipr_beg_err(KERN_ERR);
        ipr_log_err("%s timed out!!"IPR_EOL, cmd_str);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
        return rc;
    }
    else if (sistoh32(p_host_ioarcb->p_ioasa->ioasc) != 0)
    {
        /* Don't log an error if page 3 inquiry fails */
        if (ipr_debug && ((cmd != IPR_INQUIRY) || (parm != 3)))
        {
            ipr_beg_err(KERN_ERR);
            ipr_log_err("%s failed with IOASC: 0x%08X"IPR_EOL,
                           cmd_str, sistoh32(p_host_ioarcb->p_ioasa->ioasc));
            ipr_log_err("Controlling IOA:"IPR_EOL);
            ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
            ipr_log_err("Failing device:"IPR_EOL);
            ipr_log_dev_physical_location(p_shared_cfg,
                                             p_resource_entry->resource_address,
                                             KERN_ERR);
            ipr_end_err(KERN_ERR);
        }

        if (rc == IPR_RC_SUCCESS)
            rc = IPR_RC_FAILED;
    }

    ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
    ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);

    return rc;

}

/*---------------------------------------------------------------------------
 * Purpose: Send an op to a VSET resource and sleep until its completion
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Op completed sucessfully
 *          IPR_RC_FAILED            - Op failed
 *          IPR_RC_TIMEOUT           - Op timed out
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *---------------------------------------------------------------------------*/
static int ipr_blocking_vset_cmd(struct ipr_shared_config *p_shared_cfg,
                                    struct ipr_resource_entry *p_resource_entry,
                                    ipr_dma_addr dma_buffer,
                                    u8 cmd, u8 parm, u16 alloc_len)
{
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioadl_desc *p_ioadl;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    int rc = IPR_RC_SUCCESS;
    int timeout = IPR_INTERNAL_DEV_TIMEOUT;
    char cmd_str[50];

    p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);
    p_ioarcb = &p_host_ioarcb->ioarcb; 
    p_ioadl = p_host_ioarcb->p_ioadl;
    p_ioarcb->ioarcb_cmd_pkt.cdb[0] = cmd;
    p_ioarcb->ioa_res_handle = p_resource_entry->resource_handle;
    p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

    switch(cmd)
    {
        case IPR_START_STOP:
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = parm;
            strcpy(cmd_str, "Start/Stop unit");
            break;
        case IPR_QUERY_RESOURCE_STATE:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioarcb->ioarcb_cmd_pkt.cdb[7] = (alloc_len >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[8] = alloc_len & 0xff;
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | alloc_len);
            p_ioadl->address = htosis32((u32)dma_buffer);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(alloc_len);
            strcpy(cmd_str, "Query resource state");
            break;
        default:
            panic(IPR_ERR": Invalid blocking vset command 0x%02X"IPR_EOL, cmd);
            break;
    };

    ipr_trc_hook(p_shared_cfg,
                    cmd,
                    IPR_TRACE_START,
                    IPR_TRACE_DASD,
                    p_host_ioarcb->host_ioarcb_index,
                    alloc_len,
                    IPR_GET_PHYSICAL_LOCATOR(p_resource_entry->resource_address));

    /* Poke the IOARRIN with the PCI address of the IOARCB */
    writel(sistoh32(p_ioarcb->ioarcb_host_pci_addr), ipr_cfg->regs.ioarrin_reg);

    rc = ipr_poll_isr(p_shared_cfg, timeout);

    ipr_trc_hook(p_shared_cfg,
                    cmd,
                    IPR_TRACE_FINISH,
                    IPR_TRACE_DASD,
                    p_host_ioarcb->host_ioarcb_index,
                    alloc_len,
                    sistoh32(p_host_ioarcb->p_ioasa->ioasc));

    if (rc == IPR_RC_TIMEOUT)
    {
        ipr_beg_err(KERN_ERR);
        ipr_log_err("%s timed out!!"IPR_EOL, cmd_str);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
        return rc;
    }
    else if (ipr_debug && (sistoh32(p_host_ioarcb->p_ioasa->ioasc) != 0))
    {
        ipr_beg_err(KERN_ERR);
        ipr_log_err("%s failed with IOASC: 0x%08X"IPR_EOL,
                       cmd_str, sistoh32(p_host_ioarcb->p_ioasa->ioasc));
        ipr_log_err("Controlling IOA:"IPR_EOL);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_log_err("Failing device:"IPR_EOL);
        ipr_log_dev_physical_location(p_shared_cfg,
                                         p_resource_entry->resource_address,
                                         KERN_ERR);
        ipr_end_err(KERN_ERR);

        if (rc == IPR_RC_SUCCESS)
            rc = IPR_RC_FAILED;
    }

    ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
    ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);

    return rc;

}

/*---------------------------------------------------------------------------
 * Purpose: Job router for DASD initialization
 * Lock State: io_request_lock assumed to be held
 * Context: Interrupt level
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_dasd_init_job(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_ccb *p_sis_ccb)
{
    struct ipr_mode_parm_hdr *p_mode_parm, *p_changeable_pages;
    struct ipr_dasd_inquiry_page3 *p_page3_inq;
    struct ipr_dasd_timeouts *p_dasd_timeouts;
    struct ipr_query_res_state *p_res_query;
    u8 alloc_len;
    struct ipr_resource_entry *p_resource_entry;
    u32 rc = IPR_RC_SUCCESS;
    int done = 0;
    struct ipr_dasd_init_bufs *p_dasd_init_buf = p_sis_ccb->p_scratch;
    struct ipr_hostrcb *p_hostrcb = p_dasd_init_buf->p_hostrcb;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_std_inq_data_long *p_std_inq;
    struct ipr_ssd_header *p_ssd_header;
    struct ipr_supported_device *p_supported_device;
    int i, in_ssd_list;
    bool continue_with_job = true;

    rc = p_sis_ccb->completion;

    if (rc != IPR_RC_SUCCESS)
    {
        p_resource_entry = p_sis_ccb->p_resource;

        /* We have a valid sense buffer */
        if (ipr_sense_valid(p_sis_ccb->sense_buffer[0]))
        {
            if ((p_sis_ccb->sense_buffer[2] & 0xf) != 0)
            {
                rc = IPR_RC_FAILED;
                IPR_DBG_CMD(ipr_beg_err(KERN_ERR));
                ipr_dbg_err("0x%02x failed with SK: 0x%X ASC: 0x%X ASCQ: 0x%X"IPR_EOL,
                               p_sis_ccb->cdb[0], (p_sis_ccb->sense_buffer[2] & 0xf),
                               p_sis_ccb->sense_buffer[12], p_sis_ccb->sense_buffer[13]);
                ipr_dbg_err("Failing device"IPR_EOL);
                IPR_DBG_CMD(ipr_log_dev_physical_location(p_shared_cfg,
                                                                p_resource_entry->resource_address,
                                                                KERN_ERR));
                ipr_dbg_err("Controlling IOA"IPR_EOL);
                IPR_DBG_CMD(ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR));
                IPR_DBG_CMD(ipr_end_err(KERN_ERR));
            }
        }
    }

    p_resource_entry = p_dasd_init_buf->p_dev_res;

    while (continue_with_job)
    {
        continue_with_job = false;

        switch (p_sis_ccb->job_step)
        {
            case IPR_DINIT_START:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    /* Send standard inquiry */
                    p_std_inq = &p_dasd_init_buf->std_inq;

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_std_inq,
                                         p_dasd_init_buf->std_inq_dma,
                                         IPR_INQUIRY, 0xff,
                                         sizeof(struct ipr_std_inq_data_long),
                                         IPR_DINIT_STD_INQUIRY);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_DINIT_STD_INQUIRY:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_std_inq = &p_dasd_init_buf->std_inq;

                    if (p_std_inq->std_inq_data.version >= 4)
                        p_resource_entry->supports_qas = p_std_inq->qas;
                    else
                        p_resource_entry->supports_qas = 0;

                    /* Fill in additional VPD information */
                    memcpy(p_resource_entry->part_number, p_std_inq->part_number,
                           IPR_STD_INQ_PART_NUM_LEN);
                    p_resource_entry->part_number[IPR_STD_INQ_PART_NUM_LEN] = '\0';

                    memcpy(p_resource_entry->ec_level, p_std_inq->ec_level,
                           IPR_STD_INQ_EC_LEVEL_LEN);
                    p_resource_entry->ec_level[IPR_STD_INQ_EC_LEVEL_LEN] = '\0';

                    memcpy(p_resource_entry->fru_number, p_std_inq->fru_number,
                           IPR_STD_INQ_FRU_NUM_LEN);
                    p_resource_entry->fru_number[IPR_STD_INQ_FRU_NUM_LEN] = '\0';

                    memcpy(p_resource_entry->z1_term, p_std_inq->z1_term,
                           IPR_STD_INQ_Z1_TERM_LEN);
                    p_resource_entry->z1_term[IPR_STD_INQ_Z1_TERM_LEN] = '\0';

                    memcpy(p_resource_entry->z2_term, p_std_inq->z2_term,
                           IPR_STD_INQ_Z2_TERM_LEN);
                    p_resource_entry->z2_term[IPR_STD_INQ_Z2_TERM_LEN] = '\0';

                    memcpy(p_resource_entry->z3_term, p_std_inq->z3_term,
                           IPR_STD_INQ_Z3_TERM_LEN);
                    p_resource_entry->z3_term[IPR_STD_INQ_Z3_TERM_LEN] = '\0';

                    memcpy(p_resource_entry->z4_term, p_std_inq->z4_term,
                           IPR_STD_INQ_Z4_TERM_LEN);
                    p_resource_entry->z4_term[IPR_STD_INQ_Z4_TERM_LEN] = '\0';

                    memcpy(p_resource_entry->z5_term, p_std_inq->z5_term,
                           IPR_STD_INQ_Z5_TERM_LEN);
                    p_resource_entry->z5_term[IPR_STD_INQ_Z5_TERM_LEN] = '\0';

                    memcpy(p_resource_entry->z6_term, p_std_inq->z6_term,
                           IPR_STD_INQ_Z6_TERM_LEN);
                    p_resource_entry->z6_term[IPR_STD_INQ_Z6_TERM_LEN] = '\0';

                    p_res_query = &p_dasd_init_buf->res_query;

                    /* Issue a query resource state to determine the state of the device */
                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_res_query,
                                         p_dasd_init_buf->res_query_dma,
                                         IPR_QUERY_RESOURCE_STATE, 0,
                                         sizeof(struct ipr_query_res_state),
                                         IPR_DINIT_QUERY_RESOURCE_STATE);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_DINIT_QUERY_RESOURCE_STATE:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_res_query = &p_dasd_init_buf->res_query;

                    /* If the DASD is not healthy, then lets not send the rest of these
                     bringup commands. In order for the host to talk to the device it will
                     need to issue a recovery command which we would end up getting a config
                     change for which would force us through here again anyway. */
                    if (p_res_query->not_oper || p_res_query->not_ready ||
                        p_res_query->read_write_prot)
                    {
                        done = 1;
                        ipr_trace;
                    }
                    else
                    {
                        /* Check if device on current ssd list */
                        p_std_inq = &p_dasd_init_buf->std_inq;

                        for (i = 0,
                             in_ssd_list = 0;
                             i < sizeof(ipr_supported_dev_list)/
                                 sizeof(struct ipr_supported_device);
                             i++)
                        {
                            if (memcmp(&ipr_supported_dev_list_ptr[i].vpids,
                                              &p_std_inq->std_inq_data.vpids,
                                              sizeof(struct ipr_std_inq_vpids)) == 0)
                            {
                                in_ssd_list = 1;
                                break;
                            }
                        }

                        /* Send Set Supported Devices command only if device
                         is not in Set Supported Devices table. Sending it
                         down all the time would break the 15K blocking code.*/
                        if (!in_ssd_list)
                        {
                            p_ssd_header = &p_dasd_init_buf->ssd_header;
                            p_ssd_header->num_records = 1;
                            p_ssd_header->data_length =
                                htosis16(sizeof(struct ipr_supported_device) +
                                         sizeof(struct ipr_ssd_header));
                            p_ssd_header->reserved = 0;

                            p_supported_device = &p_dasd_init_buf->supported_device;

                            ipr_set_sup_dev_dflt(p_supported_device,
                                                    &p_std_inq->std_inq_data.vpids);

                            rc = ipr_ioa_req(p_shared_cfg,
                                                ipr_dasd_init_job,
                                                p_dasd_init_buf,
                                                p_ssd_header,
                                                p_dasd_init_buf->ssd_header_dma,
                                                IPR_SET_SUPPORTED_DEVICES, 0,
                                                sistoh16(p_ssd_header->data_length),
                                                IPR_DINIT_SET_SUPPORTED_DEVICE);

                            if (rc != IPR_RC_SUCCESS)
                                done = 1;
                        }
                        else
                        {
                            p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

                            if (p_sis_ccb == NULL)
                                done = 1;
                            else
                            {
                                p_sis_ccb->job_step = IPR_DINIT_SET_SUPPORTED_DEVICE;
                                continue_with_job = true;
                            }
                        }

                        if (rc != IPR_RC_SUCCESS)
                            done = 1;
                    }
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_DINIT_SET_SUPPORTED_DEVICE:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_dasd_timeouts = &p_dasd_init_buf->dasd_timeouts;

                    memcpy(p_dasd_timeouts->record, ipr_dasd_timeout_list,
                           sizeof(ipr_dasd_timeout_list));
                    p_dasd_timeouts->length = htosis32(sizeof(ipr_dasd_timeout_list) + sizeof(u32));

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_dasd_timeouts,
                                         p_dasd_init_buf->dasd_timeouts_dma,
                                         IPR_SET_DASD_TIMEOUTS, 0, sistoh32(p_dasd_timeouts->length),
                                         IPR_DINIT_DASD_INIT_SET_DASD_TIMEOUTS);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_DINIT_DASD_INIT_SET_DASD_TIMEOUTS:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_page3_inq = &p_dasd_init_buf->page3_inq;

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_page3_inq,
                                         p_dasd_init_buf->page3_inq_dma,
                                         IPR_INQUIRY, 3, sizeof(struct ipr_dasd_inquiry_page3),
                                         IPR_DINIT_PAGE3_INQ);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_DINIT_PAGE3_INQ:
                p_page3_inq = &p_dasd_init_buf->page3_inq;
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    /* Fill in the software load id and release level */
                    p_resource_entry->sw_load_id =
                        (p_page3_inq->load_id[0] << 24) |
                        (p_page3_inq->load_id[1] << 16) |
                        (p_page3_inq->load_id[2] << 8) |
                        (p_page3_inq->load_id[3]);

                    p_resource_entry->sw_release_level =
                        (p_page3_inq->release_level[0] << 24) |
                        (p_page3_inq->release_level[1] << 16) |
                        (p_page3_inq->release_level[2] << 8) |
                        (p_page3_inq->release_level[3]);
                }

                /* Some xSeries DASD may not handle Page 3 inquiry, so
                 we want to keep going if this is the case */
                if ((rc != IPR_RC_DID_RESET) && !p_resource_entry->redo_init)
                {
                    p_mode_parm = &p_dasd_init_buf->mode_pages;

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_mode_parm,
                                         p_dasd_init_buf->mode_pages_dma,
                                         IPR_MODE_SENSE, 0x3f, 255,
                                         IPR_DINIT_MODE_SENSE_CUR);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    ipr_trace;
                    done = 1;
                }
                break;
            case IPR_DINIT_MODE_SENSE_CUR:
                p_mode_parm = &p_dasd_init_buf->mode_pages;
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_changeable_pages = &p_dasd_init_buf->changeable_parms;

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_changeable_pages,
                                         p_dasd_init_buf->changeable_parms_dma,
                                         IPR_MODE_SENSE, 0x7f, 255,
                                         IPR_DINIT_MODE_SENSE_CHANGEABLE);

                    if (rc != IPR_RC_SUCCESS)
                    {
                        ipr_trace;
                        done = 1;
                    }
                }
                else
                {
                    ipr_trace;
                    done = 1;
                }
                break;
            case IPR_DINIT_MODE_SENSE_CHANGEABLE:
                p_mode_parm = &p_dasd_init_buf->mode_pages;
                p_changeable_pages = &p_dasd_init_buf->changeable_parms;

                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    /* Modify mode pages */
                    alloc_len = ipr_set_mode_pages(p_shared_cfg, p_resource_entry,
                                                      p_mode_parm, p_changeable_pages);

                    rc = ipr_dasd_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_mode_parm,
                                         p_dasd_init_buf->mode_pages_dma,
                                         IPR_MODE_SELECT, 0x11, alloc_len,
                                         IPR_DINIT_MODE_SELECT);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    ipr_trace;
                    done = 1;
                }
                break;
            case IPR_DINIT_MODE_SELECT:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);
                done = 1;
                break;
            default:
                break;
        }
    }

    if (done)
    {
        p_resource_entry->in_init = 0;

        ipr_put_dasd_init_buffer(ipr_cfg, p_dasd_init_buf);

        if (p_resource_entry->redo_init)
        {
            p_resource_entry->redo_init = 0;
            ipr_init_single_dev_runtime(p_shared_cfg, p_resource_entry, 0, p_hostrcb);
        }
        else
        {
            ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                             p_hostrcb);
        }
    }
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Job router for VSET initialization
 * Lock State: io_request_lock assumed to be held
 * Context: Interrupt level
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_vset_init_job(struct ipr_shared_config *p_shared_cfg,
                                 struct ipr_ccb *p_sis_ccb)
{
    struct ipr_query_res_state *p_res_query;
    struct ipr_resource_entry *p_resource_entry;
    u32 rc = IPR_RC_SUCCESS;
    int done = 0;
    struct ipr_dasd_init_bufs *p_dasd_init_buf = p_sis_ccb->p_scratch;
    struct ipr_hostrcb *p_hostrcb = p_dasd_init_buf->p_hostrcb;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    bool continue_with_job = true;

    rc = p_sis_ccb->completion;

    if (rc != IPR_RC_SUCCESS)
    {
        p_resource_entry = p_sis_ccb->p_resource;

        /* We have a valid sense buffer */
        if (ipr_sense_valid(p_sis_ccb->sense_buffer[0]))
        {
            if ((p_sis_ccb->sense_buffer[2] & 0xf) != 0)
            {
                rc = IPR_RC_FAILED;
                IPR_DBG_CMD(ipr_beg_err(KERN_ERR));
                ipr_dbg_err("0x%02x failed with SK: 0x%X ASC: 0x%X ASCQ: 0x%X"IPR_EOL,
                               p_sis_ccb->cdb[0], (p_sis_ccb->sense_buffer[2] & 0xf),
                               p_sis_ccb->sense_buffer[12], p_sis_ccb->sense_buffer[13]);
                ipr_dbg_err("Failing device"IPR_EOL);
                IPR_DBG_CMD(ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR));
                IPR_DBG_CMD(ipr_end_err(KERN_ERR));
            }
        }
    }

    p_resource_entry = p_dasd_init_buf->p_dev_res;

    while (continue_with_job)
    {
        continue_with_job = false;

        switch (p_sis_ccb->job_step)
        {
            case IPR_VINIT_START:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_res_query = &p_dasd_init_buf->res_query;

                    /* Issue a query resource state to determine the state of the device */
                    rc = ipr_vset_req(p_shared_cfg, p_resource_entry,
                                         p_dasd_init_buf,
                                         p_res_query,
                                         p_dasd_init_buf->res_query_dma,
                                         IPR_QUERY_RESOURCE_STATE, 0,
                                         sizeof(struct ipr_query_res_state),
                                         IPR_VINIT_QUERY_RESOURCE_STATE);

                    if (rc != IPR_RC_SUCCESS)
                        done = 1;
                }
                else
                {
                    done = 1;
                    ipr_trace;
                }
                break;
            case IPR_VINIT_QUERY_RESOURCE_STATE:
                ipr_release_ccb(p_shared_cfg, p_sis_ccb);

                done = 1;

                if ((rc == IPR_RC_SUCCESS) && !p_resource_entry->redo_init)
                {
                    p_res_query = &p_dasd_init_buf->res_query;

                    p_resource_entry->model = IPR_VSET_MODEL_NUMBER +
                        simple_strtoul(p_res_query->protection_level_str, NULL, 10);
                }
                else
                    ipr_trace;
                break;
            default:
                break;
        }
    }

    if (done)
    {
        p_resource_entry->in_init = 0;

        ipr_put_dasd_init_buffer(ipr_cfg, p_dasd_init_buf);

        if (p_resource_entry->redo_init)
        {
            p_resource_entry->redo_init = 0;
            ipr_init_single_dev_runtime(p_shared_cfg, p_resource_entry, 0, p_hostrcb);
        }
        else
        {
            ipr_send_hcam(p_shared_cfg, IPR_HCAM_CDB_OP_CODE_CONFIG_CHANGE,
                             p_hostrcb);
        }
    }
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Send an op to a DASD resource.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Failure
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
static int ipr_dasd_req(struct ipr_shared_config *p_shared_cfg,
                           struct ipr_resource_entry *p_resource_entry,
                           struct ipr_dasd_init_bufs *p_dasd_init_buf,
                           void *p_buffer,
                           ipr_dma_addr dma_addr,
                           u8 cmd, u8 parm, u16 alloc_len, u8 job_step)
{
    struct ipr_ccb *p_sis_ccb;
    int rc = IPR_RC_SUCCESS;
    int timeout = IPR_INTERNAL_DEV_TIMEOUT;

    p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

    if (p_sis_ccb == NULL)
    {
        /* Requests must not be allowed right now - we are probably going through
         reset/reload right now */
        return IPR_RC_FAILED;
    }

    p_sis_ccb->p_resource = p_resource_entry;
    p_sis_ccb->p_scratch = p_dasd_init_buf;
    p_sis_ccb->bufflen = alloc_len;
    p_sis_ccb->buffer = p_buffer;
    p_sis_ccb->buffer_dma = (u32)dma_addr;
    p_sis_ccb->scsi_use_sg = 0;
    p_sis_ccb->cdb[0] = cmd;
    p_sis_ccb->cmd_len = 6;
    p_sis_ccb->job_step = job_step;
    p_sis_ccb->flags = IPR_BUFFER_MAPPED;

    switch(cmd)
    {
        case IPR_MODE_SENSE:
            p_sis_ccb->cdb[2] = parm; 
            p_sis_ccb->cdb[4] = alloc_len & 0xff;
            p_sis_ccb->data_direction = IPR_DATA_READ;
            memset(p_buffer, 0, alloc_len);
            break;
        case IPR_MODE_SELECT:
            p_sis_ccb->data_direction = IPR_DATA_WRITE;
            p_sis_ccb->cdb[1] = parm;
            p_sis_ccb->cdb[4] = alloc_len & 0xff;
            break;
        case IPR_SET_DASD_TIMEOUTS:
            p_sis_ccb->flags |= IPR_IOA_CMD;
            p_sis_ccb->data_direction = IPR_DATA_WRITE;
            p_sis_ccb->cdb[7] = (alloc_len >> 8) & 0xff;
            p_sis_ccb->cdb[8] = alloc_len & 0xff;
            p_sis_ccb->cmd_len = 10;
            break;
        case IPR_INQUIRY:
            p_sis_ccb->data_direction = IPR_DATA_READ;

            if (parm != 0xff)
            {
                p_sis_ccb->cdb[1] = 0x01;
                p_sis_ccb->cdb[2] = parm; 
            }

            p_sis_ccb->cdb[4] = alloc_len & 0xff;
            memset(p_buffer, 0, alloc_len);
            break;
        case IPR_QUERY_RESOURCE_STATE:
            p_sis_ccb->flags |= IPR_IOA_CMD;
            p_sis_ccb->data_direction = IPR_DATA_READ;
            p_sis_ccb->cdb[7] = (alloc_len >> 8) & 0xff;
            p_sis_ccb->cdb[8] = alloc_len & 0xff;
            p_sis_ccb->cmd_len = 10;
            memset(p_buffer, 0, alloc_len);
            break;
        default:
            rc = IPR_RC_FAILED;
            goto leave;
            break;
    };

    rc = ipr_do_req(p_shared_cfg, p_sis_ccb,
                       ipr_dasd_init_job, timeout);

    leave:
        if (rc != IPR_RC_SUCCESS)
            ipr_release_ccb(p_shared_cfg, p_sis_ccb);

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Send an op to a VSET resource.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Failure
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
static int ipr_vset_req(struct ipr_shared_config *p_shared_cfg,
                           struct ipr_resource_entry *p_resource_entry,
                           struct ipr_dasd_init_bufs *p_dasd_init_buf,
                           void *p_buffer,
                           ipr_dma_addr dma_addr,
                           u8 cmd, u8 parm, u16 alloc_len, u8 job_step)
{
    struct ipr_ccb *p_sis_ccb;
    int rc = IPR_RC_SUCCESS;
    int timeout = IPR_INTERNAL_DEV_TIMEOUT;

    p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

    if (p_sis_ccb == NULL)
    {
        /* Requests must not be allowed right now - we are probably going through
         reset/reload right now */
        return IPR_RC_FAILED;
    }

    p_sis_ccb->p_resource = p_resource_entry;
    p_sis_ccb->p_scratch = p_dasd_init_buf;
    p_sis_ccb->bufflen = alloc_len;
    p_sis_ccb->buffer = p_buffer;
    p_sis_ccb->buffer_dma = (u32)dma_addr;
    p_sis_ccb->scsi_use_sg = 0;
    p_sis_ccb->cdb[0] = cmd;
    p_sis_ccb->cmd_len = 6;
    p_sis_ccb->job_step = job_step;
    p_sis_ccb->flags = IPR_BUFFER_MAPPED;

    switch(cmd)
    {
        case IPR_QUERY_RESOURCE_STATE:
            p_sis_ccb->flags |= IPR_IOA_CMD;
            p_sis_ccb->data_direction = IPR_DATA_READ;
            p_sis_ccb->cdb[7] = (alloc_len >> 8) & 0xff;
            p_sis_ccb->cdb[8] = alloc_len & 0xff;
            p_sis_ccb->cmd_len = 10;
            memset(p_buffer, 0, alloc_len);
            break;
        default:
            rc = IPR_RC_FAILED;
            goto leave;
            break;
    };

    rc = ipr_do_req(p_shared_cfg, p_sis_ccb,
                       ipr_vset_init_job, timeout);

    leave:
        if (rc != IPR_RC_SUCCESS)
            ipr_release_ccb(p_shared_cfg, p_sis_ccb);

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Send an op to the IOA resource.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS           - Success
 *          IPR_RC_FAILED            - Failure
 *          IPR_RC_OP_NOT_SENT       - Op was not sent to the device
 *---------------------------------------------------------------------------*/
static int ipr_ioa_req(struct ipr_shared_config *p_shared_cfg,
                          void (*done) (struct ipr_shared_config *, struct ipr_ccb *),
                          void *p_scratch,
                          void *p_buffer,
                          ipr_dma_addr dma_addr,
                          u8 cmd, u8 parm, u16 alloc_len, u8 job_step)
{
    struct ipr_ccb *p_sis_ccb;
    int rc = IPR_RC_SUCCESS;
    int timeout = IPR_INTERNAL_TIMEOUT;

    p_sis_ccb = ipr_allocate_ccb(p_shared_cfg);

    if (p_sis_ccb == NULL)
    {
        /* Requests must not be allowed right now - we are probably going through
         reset/reload right now */
        return IPR_RC_FAILED;
    }

    p_sis_ccb->p_resource = &p_shared_cfg->ioa_resource;
    p_sis_ccb->p_scratch = p_scratch;
    p_sis_ccb->bufflen = alloc_len;
    p_sis_ccb->buffer = p_buffer;
    p_sis_ccb->buffer_dma = (u32)dma_addr;
    p_sis_ccb->scsi_use_sg = 0;
    p_sis_ccb->cdb[0] = cmd;
    p_sis_ccb->cmd_len = 6;
    p_sis_ccb->job_step = job_step;
    p_sis_ccb->flags = IPR_BUFFER_MAPPED;

    switch(cmd)
    {
        case IPR_MODE_SENSE:
            p_sis_ccb->cdb[2] = parm; 
            p_sis_ccb->cdb[4] = alloc_len;
            p_sis_ccb->data_direction = IPR_DATA_READ;
            break;
        case IPR_MODE_SELECT:
            p_sis_ccb->data_direction = IPR_DATA_WRITE;
            p_sis_ccb->cdb[1] = parm;
            p_sis_ccb->cdb[4] = alloc_len;
            break;
        case IPR_INQUIRY:
            p_sis_ccb->data_direction = IPR_DATA_READ;

            if (parm != 0xff)
            {
                p_sis_ccb->cdb[1] = 0x01;
                p_sis_ccb->cdb[2] = parm; 
            }
            p_sis_ccb->cdb[4] = alloc_len;
            break;
        case IPR_SET_SUPPORTED_DEVICES:
            p_sis_ccb->flags |= IPR_IOA_CMD;
            p_sis_ccb->cmd_len = 10;
            p_sis_ccb->data_direction = IPR_DATA_WRITE;
            p_sis_ccb->cdb[7] = (alloc_len >> 8) & 0xff;
            p_sis_ccb->cdb[8] = alloc_len & 0xff;
            timeout = IPR_SET_SUP_DEVICE_TIMEOUT;
            break;
        default:
            rc = IPR_RC_FAILED;
            goto leave;
            break;
    };

    rc = ipr_do_req(p_shared_cfg, p_sis_ccb,
                       done, timeout);

    leave:
        if (rc != IPR_RC_SUCCESS)
            ipr_release_ccb(p_shared_cfg, p_sis_ccb);

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Get the specified mode page
 * Lock State: io_request_lock assumed to be held
 * Returns: Pointer to mode page or NULL if mode page not found
 *---------------------------------------------------------------------------*/
static void *ipr_get_mode_page(struct ipr_mode_parm_hdr *p_mode_parm,
                                  u32 page_code, u32 len)
{
    struct ipr_mode_page_hdr *p_mode_hdr;
    u32 page_length;
    u32 length;

    if ((p_mode_parm == NULL) || (p_mode_parm->length == 0))
        return NULL;

    length = (p_mode_parm->length + 1) - 4 - p_mode_parm->block_desc_len;
    p_mode_hdr = (struct ipr_mode_page_hdr*)((unsigned long)p_mode_parm +
                                                4 + p_mode_parm->block_desc_len);

    while(length)
    {
        if (p_mode_hdr->page_code == page_code)
        {
            if (p_mode_hdr->page_length >= (len - sizeof(struct ipr_mode_page_hdr)))
                return p_mode_hdr;
            break;
        }
        else
        {
            page_length = (sizeof(struct ipr_mode_page_hdr) + p_mode_hdr->page_length);
            length -= page_length;
            p_mode_hdr = (struct ipr_mode_page_hdr*)((unsigned long)p_mode_hdr + page_length);
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Convert EBCDIC string to ASCII string
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_ebcdic_to_ascii(const u8 *ebcdic, u32 length,  u8 *ascii)
{
    u8 ebcdic_char;
    int i;

    for (i = 0; i < length; i++, ebcdic++, ascii++ )
    {
        ebcdic_char = *ebcdic;
        *ascii = ipr_etoa[ebcdic_char];
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Convert DASD Vendor/Product ID to CCIN
 * Lock State: io_request_lock assumed to be held
 * Returns: CCIN of DASD or default_ccin if not in supported device list
 *---------------------------------------------------------------------------*/
u16 ipr_dasd_vpids_to_ccin(struct ipr_std_inq_vpids *p_vpids,
                              u16 default_ccin)
{
    int j;
    char p_ccin[5];
    u32 num_supported_devs = sizeof(ipr_supported_dev_list)/sizeof(struct ipr_supported_device);

    for (j = 0; j < num_supported_devs; j++)
    {
        if (memcmp(&ipr_supported_dev_list_ptr[j].vpids, p_vpids,
                          sizeof(struct ipr_std_inq_vpids)) == 0)
        {
            ipr_ebcdic_to_ascii((u8 *)&ipr_supported_dev_list_ptr[j].ebcdic_as400_device_type,
                                   4, p_ccin);
            p_ccin[4] = '\0';
            return simple_strtoul(p_ccin, NULL, 16);
        }
    }

    return default_ccin;
}

/*---------------------------------------------------------------------------
 * Purpose: Convert DASD Vendor/Product ID to CCIN
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_dasd_vpids_to_ccin_str(struct ipr_std_inq_vpids *p_vpids,
                                   char *p_ccin, char *p_default_ccin)
{
    u16 ccin = ipr_dasd_vpids_to_ccin(p_vpids, 0);

    if (ccin)
        sprintf(p_ccin, "%X", ccin);
    else
        sprintf(p_ccin, "%s", p_default_ccin);
    return;
}

/*---------------------------------------------------------------------------
 * Purpose: Print the command packet for debug purposes
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_print_cmd_pkt(struct ipr_cmd_pkt *p_cmd_pkt)
{
    u8 *p_byte = (u8 *)p_cmd_pkt;
    char string[(sizeof(struct ipr_cmd_pkt) * 2) + 1];
    int i, len;

    for (i = 0, len = 0; i < sizeof(struct ipr_cmd_pkt); i++)
        len += sprintf(&string[len], "%02X", p_byte[i]);

    string[(sizeof(struct ipr_cmd_pkt) * 2)] = '\0';

    ipr_log_err("Cmd pkt: %s"IPR_EOL, string);
}

/*---------------------------------------------------------------------------
 * Purpose: Sends an internal request to the Focal Point
 *          and polls isr until a response is received or is
 *          timed out.
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS                   - Op completed successfully
 *          IPR_RC_TIMEOUT                   - Op timed out
 *          IPR_RC_XFER_FAILED               - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ                      - No HRRQ interrupt
 *          IPR_IOARRIN_LOST                 - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR                   - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED             - IOA unit checked
 *          IPR_RC_FAILED                    - Op failed
 *          IPR_IOASC_NR_IOA_MICROCODE       - IOA needs ucode download
 *---------------------------------------------------------------------------*/
static u32 ipr_send_blocking_cmd(struct ipr_shared_config *p_shared_cfg,
                                    u8 sis_cmd,
                                    u32 timeout,
                                    u8 parm,
                                    void *p_data,
                                    ipr_dma_addr dma_addr,
                                    u32 xfer_len)
{
    u32 pci_addr_ioarcb;
    u32 ioasc;
    u32 host_ioarcb_index;
    u32 rc = IPR_RC_SUCCESS;
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    p_host_ioarcb = ipr_build_ioa_cmd(p_shared_cfg,
                                         sis_cmd,
                                         NULL,
                                         parm,
                                         p_data,
                                         dma_addr,
                                         xfer_len);

    host_ioarcb_index = p_host_ioarcb->host_ioarcb_index;

    ipr_trc_hook(p_shared_cfg,
                    sis_cmd,
                    IPR_TRACE_START,
                    IPR_TRACE_IOA,
                    host_ioarcb_index,
                    xfer_len,
                    IPR_IOA_RESOURCE_ADDRESS);

    pci_addr_ioarcb = (u32)p_host_ioarcb->ioarcb_dma;

    /* Poke the IOARRIN with the PCI address of the IOARCB */
    writel(pci_addr_ioarcb, ipr_cfg->regs.ioarrin_reg);

    rc = ipr_poll_isr(p_shared_cfg, timeout);

    if (rc == IPR_RC_SUCCESS)
    {
        ioasc = sistoh32(p_host_ioarcb->p_ioasa->ioasc);

        if ((ioasc != 0) && (ioasc != IPR_IOASC_NR_IOA_MICROCODE))
        {
            rc = IPR_RC_FAILED;

            if ((IPR_IOASC_SENSE_KEY(ioasc) != 0x05) ||
                (ipr_debug))
            {
                ipr_log_err("Op failed: op: 0x%x. IOASC: 0x%08x. Parm: %d"IPR_EOL,
                               sis_cmd, ioasc, parm);
                ipr_log_err("IOASA: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x"IPR_EOL,
                               ioasc, sistoh32(*((u32 *)&p_host_ioarcb->p_ioasa->ret_stat_len)),
                               sistoh32(p_host_ioarcb->p_ioasa->residual_data_len),
                               sistoh32(p_host_ioarcb->p_ioasa->ilid),
                               sistoh32(p_host_ioarcb->p_ioasa->fd_ioasc),
                               sistoh32(p_host_ioarcb->p_ioasa->fd_phys_locator),
                               sistoh32(p_host_ioarcb->p_ioasa->fd_res_handle));

                if (IPR_IOASC_SENSE_KEY(ioasc) == 0x05)
                    ipr_print_cmd_pkt(&p_host_ioarcb->ioarcb.ioarcb_cmd_pkt);
            }
        }
        else if (ioasc == IPR_IOASC_NR_IOA_MICROCODE)
        {
            p_shared_cfg->nr_ioa_microcode = 1;
            rc = IPR_IOASC_NR_IOA_MICROCODE;
        }

        ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
        ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);
    }
    else
        ipr_dbg_err("Blocking command failed: 0x%02x 0x%08x"IPR_EOL, sis_cmd, rc);

    ipr_trc_hook(p_shared_cfg,
                    sis_cmd,
                    IPR_TRACE_FINISH,
                    IPR_TRACE_IOA,
                    host_ioarcb_index,
                    xfer_len,
                    rc);

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Builds up an IOARCB for a Focal Point command
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to a host ioarcb or NULL if command was invalid
 *          If the command was invalid, sense data will have been filled in
 *---------------------------------------------------------------------------*/
static struct ipr_host_ioarcb* ipr_build_ioa_cmd(struct ipr_shared_config *p_shared_cfg,
                                                       u8 sis_cmd,
                                                       struct ipr_ccb *p_sis_cmd,
                                                       u8 parm,
                                                       void *p_data,
                                                       ipr_dma_addr dma_addr,
                                                       u32 xfer_len)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_host_ioarcb *p_host_ioarcb;
    struct ipr_ioarcb *p_ioarcb;
    struct ipr_ioadl_desc *p_ioadl;
    unsigned char *p_sense_buffer;
    struct ipr_discard_cache_data *p_discard_data;

    p_host_ioarcb = ipr_get_free_host_ioarcb(p_shared_cfg);
    p_ioarcb = &p_host_ioarcb->ioarcb; 
    p_ioadl = p_host_ioarcb->p_ioadl;
    p_host_ioarcb->p_sis_cmd = p_sis_cmd;

    /* Setup IOARCB */
    p_ioarcb->ioarcb_cmd_pkt.cdb[0] = sis_cmd;
    p_ioarcb->ioa_res_handle = IPR_IOA_RESOURCE_HANDLE;

    if (p_sis_cmd)
        memcpy(p_ioarcb->ioarcb_cmd_pkt.cdb, p_sis_cmd->cdb, p_sis_cmd->cmd_len);

    switch(sis_cmd)
    {
        case IPR_ID_HOST_RR_Q:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioarcb->ioarcb_cmd_pkt.cdb[2] = ((u32)ipr_cfg->host_rrq_dma >> 24) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[3] = ((u32)ipr_cfg->host_rrq_dma >> 16) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = ((u32)ipr_cfg->host_rrq_dma >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[5] = ((u32)ipr_cfg->host_rrq_dma) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[7] = ((sizeof(u32) * IPR_NUM_CMD_BLKS) >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[8] = (sizeof(u32) * IPR_NUM_CMD_BLKS) & 0xff;
            break;
        case IPR_QUERY_IOA_CONFIG:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            p_ioarcb->ioarcb_cmd_pkt.cdb[7] = (xfer_len >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[8] = xfer_len & 0xff;
            break;
        case IPR_INQUIRY:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;

            if ((p_sis_cmd == NULL) && (parm != 0xff))
            {
                p_ioarcb->ioarcb_cmd_pkt.cdb[1] = 1;
                p_ioarcb->ioarcb_cmd_pkt.cdb[2] = parm;
            }

            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = xfer_len;
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            break;
        case IPR_HOST_CONTROLLED_ASYNC:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_HCAM;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            break;
        case IPR_SET_SUPPORTED_DEVICES:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->write_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->write_data_transfer_length = htosis32(xfer_len);
            p_ioarcb->ioarcb_cmd_pkt.cdb[7] = (xfer_len >> 8) & 0xff;
            p_ioarcb->ioarcb_cmd_pkt.cdb[8] = xfer_len & 0xff;
            break;
        case IPR_IOA_SHUTDOWN:
        case IPR_EVALUATE_DEVICE:
        case IPR_START_ARRAY_PROTECTION:
        case IPR_STOP_ARRAY_PROTECTION:
        case IPR_REBUILD_DEVICE_DATA:
        case IPR_ADD_ARRAY_DEVICE:
        case IPR_RESYNC_ARRAY_PROTECTION:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
        case IPR_WRITE_BUFFER:
            ipr_build_ioadl(p_shared_cfg, p_host_ioarcb);
            break;
        case IPR_QUERY_ARRAY_CONFIG:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            break;
        case IPR_RECLAIM_CACHE_STORE:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            break;
        case IPR_QUERY_COMMAND_STATUS:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            break;
        case IPR_SUSPEND_DEV_BUS:
            p_ioarcb->ioarcb_cmd_pkt.cmd_timeout = htosis16(p_sis_cmd->timeout);
        case IPR_RESUME_DEVICE_BUS:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            break;
        case IPR_DISCARD_CACHE_DATA:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;
            p_discard_data = p_sis_cmd->buffer;

            p_ioarcb->add_cmd_parms_len = htosis32(p_discard_data->length);
            memcpy(p_ioarcb->add_cmd_parms,
                   p_discard_data->data.add_cmd_parms,
                   p_discard_data->length);
            break;
        case IPR_MODE_SENSE:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_SCSICDB;
            if (p_sis_cmd == NULL)
                p_ioarcb->ioarcb_cmd_pkt.cdb[2] = parm;
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = xfer_len;
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
            break;
        case IPR_MODE_SELECT:
            p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;
            if (p_sis_cmd == NULL)
                p_ioarcb->ioarcb_cmd_pkt.cdb[1] = parm;
            p_ioarcb->ioarcb_cmd_pkt.cdb[4] = xfer_len;

            p_ioadl->flags_and_data_len =
                htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | xfer_len);
            p_ioadl->address = htosis32((u32)dma_addr);
            p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
            p_ioarcb->write_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
            p_ioarcb->write_data_transfer_length = htosis32(xfer_len);
            break;
        case IPR_IOA_DEBUG:
            p_ioarcb->ioarcb_cmd_pkt.request_type = IPR_RQTYPE_IOACMD;

            if (p_sis_cmd->data_direction == IPR_DATA_READ)
            {
                p_ioadl->flags_and_data_len =
                    htosis32(IPR_IOADL_FLAGS_HOST_READ_BUF_LAST_DATA | xfer_len);
                p_ioadl->address = htosis32((u32)dma_addr);
                p_ioarcb->read_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
                p_ioarcb->read_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
                p_ioarcb->read_data_transfer_length = htosis32(xfer_len);
                break;
            }
            else if (p_sis_cmd->data_direction == IPR_DATA_WRITE)
            {
                p_ioarcb->ioarcb_cmd_pkt.write_not_read = 1;
                p_ioadl->flags_and_data_len =
                    htosis32(IPR_IOADL_FLAGS_HOST_WR_LAST_DATA | xfer_len);
                p_ioadl->address = htosis32((u32)dma_addr);
                p_ioarcb->write_ioadl_addr = htosis32((u32)p_host_ioarcb->ioadl_dma);
                p_ioarcb->write_ioadl_len = htosis32(sizeof(struct ipr_ioadl_desc));
                p_ioarcb->write_data_transfer_length = htosis32(xfer_len);
                break;
            }
        default:
            if (p_sis_cmd)
            {
                /* Generate sense data: Illegal Request, Invalid Command Op Code */
                p_sense_buffer = p_sis_cmd->sense_buffer;
                memset(p_sense_buffer, 0, IPR_SENSE_BUFFERSIZE);
                p_sense_buffer[0] = 0x70;
                p_sense_buffer[2] = 0x05;
                p_sense_buffer[7] = 6;
                p_sense_buffer[12] = 0x20;
                p_sense_buffer[13] = 0x00;
                p_sis_cmd->status = 0;
            }
            else
                panic(IPR_ERR": Invalid blocking command issued: 0x%X"IPR_EOL,
                             sis_cmd);

            ipr_remove_host_ioarcb_from_pending(ipr_cfg, p_host_ioarcb);
            ipr_put_host_ioarcb_to_free(ipr_cfg, p_host_ioarcb);
            p_host_ioarcb = NULL;
            break;
    }

    return p_host_ioarcb;

}

/*---------------------------------------------------------------------------
 * Polls the interrupt register for op completion
 * Returns: IPR_RC_SUCCESS           - Op completed sucessfully
 *          IPR_RC_TIMEOUT           - Op timed out
 *          IPR_RC_XFER_FAILED       - IOARCB xfer failed interrupt
 *          IPR_NO_HRRQ              - No HRRQ interrupt
 *          IPR_IOARRIN_LOST         - IOARRIN lost interrupt
 *          IPR_MMIO_ERROR           - MMIO error interrupt
 *          IPR_IOA_UNIT_CHECKED     - IOA unit checked
 *          IPR_RC_FAILED            - Other error
 * Notes: Timeout value is in seconds
 *---------------------------------------------------------------------------*/
static u32 ipr_poll_isr(struct ipr_shared_config *p_shared_cfg, u32 timeout)
{
    volatile u32 temp_pci_reg;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    int rc = IPR_RC_SUCCESS;

    /* Convert unit to 10 msecs ticks */
    timeout *= 100;

    while (timeout)
    {
        temp_pci_reg = readl(ipr_cfg->regs.sense_interrupt_reg);

        if (temp_pci_reg & IPR_PCII_HOST_RRQ_UPDATED)
            break;
        else if (temp_pci_reg & IPR_PCII_ERROR_INTERRUPTS)
        {
            if (temp_pci_reg & IPR_PCII_IOARCB_XFER_FAILED)
                rc = IPR_RC_XFER_FAILED;
            else if (temp_pci_reg & IPR_PCII_NO_HOST_RRQ)
                rc = IPR_NO_HRRQ;
            else if (temp_pci_reg & IPR_PCII_IOARRIN_LOST)
                rc = IPR_IOARRIN_LOST;
            else if (temp_pci_reg & IPR_PCII_MMIO_ERROR)
                rc = IPR_MMIO_ERROR;
            else if (temp_pci_reg & IPR_PCII_IOA_UNIT_CHECKED)
                rc = IPR_IOA_UNIT_CHECKED;
            else
                rc = IPR_RC_FAILED;
            break;
        }
        else
        {
            /* Sleep for 10 msecs */
            ipr_sleep(10);
            timeout--;
        }
    }

    if (timeout)
    {
        if (rc == IPR_RC_SUCCESS)
        {
            if (ipr_cfg->host_rrq_curr_ptr < ipr_cfg->host_rrq_end_addr)
            {
                ipr_cfg->host_rrq_curr_ptr++;
            }
            else
            {
                ipr_cfg->host_rrq_curr_ptr = ipr_cfg->host_rrq_start_addr;
                ipr_cfg->toggle_bit ^= 1u;
            }

            /* Clear the PCI interrupt */
            writel (IPR_PCII_HOST_RRQ_UPDATED,
                           ipr_cfg->regs.clr_interrupt_reg);

            /* Re-read the PCI interrupt reg to force clear */
            temp_pci_reg = readl(ipr_cfg->regs.sense_interrupt_reg);
        }
        else
            ipr_dbg_err("Op failed due to interrupt=0x%08x"IPR_EOL, temp_pci_reg);
    }
    else
    {
        ipr_dbg_err("Op timed out. temp_pci_reg=0x%08x"IPR_EOL, temp_pci_reg);
        rc = IPR_RC_TIMEOUT;
    }

    return rc;
}

/*---------------------------------------------------------------------------
 * Purpose: Mask all interrupts on the adapter
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
void ipr_mask_interrupts(struct ipr_shared_config *p_shared_cfg)
{
    volatile u32 temp_pci_reg;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    /* Stop new interrupts */
    p_shared_cfg->allow_interrupts = 0;

    /* Set interrupt mask to stop all new interrupts */
    writel(~0, ipr_cfg->regs.set_interrupt_mask_reg);

    /* Re-read the PCI interrupt mask reg to force clear */
    temp_pci_reg = readl(ipr_cfg->regs.sense_interrupt_mask_reg);
}

/*---------------------------------------------------------------------------
 * Purpose: Get pointer to device config for a device
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer
 *---------------------------------------------------------------------------*/
static const
struct ipr_dev_config *ipr_get_dev_config(struct ipr_resource_entry *p_resource_entry)
{
    int i;

    if (p_resource_entry)
    {
        for (i = 0;
             i < sizeof(ipr_dev_cfg_table)/sizeof(struct ipr_dev_config);
             i++)
        {
            if (memcmp(&ipr_dev_cfg_table[i].vpids,
                              &p_resource_entry->std_inq_data.vpids,
                              ipr_dev_cfg_table[i].vpd_len) == 0)
            {
                return &ipr_dev_cfg_table[i];
            }
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x00 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x00(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_vendor_unique_page *p_ch,
                                struct ipr_vendor_unique_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x00)
            p_dev_cfg->set_page0x00(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x00_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x00 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x00_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_vendor_unique_page *p_ch,
                                         struct ipr_vendor_unique_page *p_buf)
{
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x00 to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x00_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_vendor_unique_page *p_ch,
                                      struct ipr_vendor_unique_page *p_buf)
{
    ipr_set_page0x00_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->qpe, p_buf->qpe, 1);
    IPR_SET_MODE(p_ch->uqe, p_buf->uqe, 0);
    IPR_SET_MODE(p_ch->dwd, p_buf->dwd, 0);
    IPR_SET_MODE(p_ch->asdpe, p_buf->asdpe, 1);
    IPR_SET_MODE(p_ch->cmdac, p_buf->cmdac, 1);
    IPR_SET_MODE(p_ch->rpfae, p_buf->rpfae, 0);
    IPR_SET_MODE(p_ch->dsn, p_buf->dsn, 1);
    IPR_SET_MODE(p_ch->frdd, p_buf->frdd, 1);
    IPR_SET_MODE(p_ch->dpsdp, p_buf->dpsdp, 0);
    IPR_SET_MODE(p_ch->wpen, p_buf->wpen, 0);
    IPR_SET_MODE(p_ch->drd, p_buf->drd, 0);
    IPR_SET_MODE(p_ch->rrnde, p_buf->rrnde, 0);
    IPR_SET_MODE(p_ch->led_mode, p_buf->led_mode, 0);

    /* Completely clear byte 15 */
    IPR_SET_MODE(p_ch->rtp, p_buf->rtp, 0);
    IPR_SET_MODE(p_ch->rrc, p_buf->rrc, 0);
    IPR_SET_MODE(p_ch->fcert, p_buf->fcert, 0);
    IPR_SET_MODE(p_ch->reserved13, p_buf->reserved13, 0);
    IPR_SET_MODE(p_ch->drpdv, p_buf->drpdv, 0);
    IPR_SET_MODE(p_ch->dsf, p_buf->dsf, 0);
    IPR_SET_MODE(p_ch->irt, p_buf->irt, 0);
    IPR_SET_MODE(p_ch->ivr, p_buf->ivr, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x00 to the default AS/400 TCQ settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x00_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_vendor_unique_page *p_ch,
                                    struct ipr_vendor_unique_page *p_buf)
{
    ipr_set_page0x00_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->qpe, p_buf->qpe, 1);
    IPR_SET_MODE(p_ch->rrnde, p_buf->rrnde, 0);
    IPR_SET_MODE(p_ch->irt, p_buf->irt, 0);
    IPR_SET_MODE(p_ch->ivr, p_buf->ivr, 0);
    IPR_SET_MODE(p_ch->asdpe, p_buf->asdpe, 1);
    IPR_SET_MODE(p_ch->cmdac, p_buf->cmdac, 1);
    IPR_SET_MODE(p_ch->frdd, p_buf->frdd, 1);
    IPR_SET_MODE(p_ch->ffmt, p_buf->ffmt, 0);
    IPR_SET_MODE(p_ch->led_mode, p_buf->led_mode, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_rw_err_mode_page *p_ch,
                                struct ipr_rw_err_mode_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x01)
            p_dev_cfg->set_page0x01(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x01_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_rw_err_mode_page *p_ch,
                                         struct ipr_rw_err_mode_page *p_buf)
{
    IPR_SET_MODE(p_ch->awre, p_buf->awre, 1);
    IPR_SET_MODE(p_ch->arre, p_buf->arre, 1);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_rw_err_mode_page *p_ch,
                                      struct ipr_rw_err_mode_page *p_buf)
{
    ipr_set_page0x01_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->tb, p_buf->tb, 0);
    IPR_SET_MODE(p_ch->rc, p_buf->rc, 0);
    IPR_SET_MODE(p_ch->per, p_buf->per, 1);
    IPR_SET_MODE(p_ch->dte, p_buf->dte, 0);
    IPR_SET_MODE(p_ch->dcr, p_buf->dcr, 0);
    IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, 1);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 to the default AS/400 TCQ settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_rw_err_mode_page *p_ch,
                                    struct ipr_rw_err_mode_page *p_buf)
{
    ipr_set_page0x01_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->tb, p_buf->tb, 0);
    IPR_SET_MODE(p_ch->rc, p_buf->rc, 0);
    IPR_SET_MODE(p_ch->per, p_buf->per, 1);
    IPR_SET_MODE(p_ch->dte, p_buf->dte, 0);
    IPR_SET_MODE(p_ch->dcr, p_buf->dcr, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_rw_err_mode_page *p_ch,
                                             struct ipr_rw_err_mode_page *p_buf)
{
    ipr_set_page0x01_defaults(p_resource_entry, p_ch, p_buf);

    /* Is this a Seagate 10K 8 GB drive? */
    if (p_resource_entry->sw_load_id == 0xA17002B4)
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, 0x0B);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_rw_err_mode_page *p_ch,
                                                struct ipr_rw_err_mode_page *p_buf)
{
    u8 retry_cnt = 0;

    if ((p_resource_entry->sw_load_id == 0xA17002BA) ||
        (p_resource_entry->sw_load_id == 0xA17002C0)) /* Seagate 10 17GB drive */
    {
        retry_cnt = p_buf->read_retry_count;
    }

    ipr_set_page0x01_as400(p_resource_entry, p_ch, p_buf);

    if (p_resource_entry->sw_load_id == 0xA17002B5)  /* Seagate 17GB, Apollo, 10kRPM */
    {
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, 0x0B);
    }
    else if ((p_resource_entry->sw_load_id == 0xA17002BA) ||
             (p_resource_entry->sw_load_id == 0xA17002C0)) /* Seagate 10 17GB drive */
    {
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, retry_cnt);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_discovery_36Gb(struct ipr_resource_entry *p_resource_entry,
                                               struct ipr_rw_err_mode_page *p_ch,
                                               struct ipr_rw_err_mode_page *p_buf)
{
    u8 retry_cnt = 0;

    if ((p_resource_entry->sw_load_id == 0xA17002BB) || /* Seagate 35GB, Gemini, 10kRPM */
        (p_resource_entry->sw_load_id == 0xA17002C1))   /* Seagate 35GB,  ?, 10kRPM */
    {
        retry_cnt = p_buf->read_retry_count;
    }

    ipr_set_page0x01_as400(p_resource_entry, p_ch, p_buf);

    if (p_resource_entry->sw_load_id == 0xA17002B6)  /* Seagate 35GB, Apollo, 10kRPM */
    {
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, 0x0B);
    }
    else if ((p_resource_entry->sw_load_id == 0xA17002BB) || /* Seagate 35GB, Gemini, 10kRPM */
             (p_resource_entry->sw_load_id == 0xA17002C1))   /* Seagate 35GB,  ?, 10kRPM */
    {
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, retry_cnt);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x01 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x01_daytona_70Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_rw_err_mode_page *p_ch,
                                             struct ipr_rw_err_mode_page *p_buf)
{
    u8 retry_cnt = 0;

    if ((p_resource_entry->sw_load_id == 0xA17002C2) || /* Seagate 70GB, Jupiter, 10kRPM */
        (p_resource_entry->sw_load_id == 0xA17002C3))   /* Seagate 70GB, Gemini, 10kRPM */
    {
        retry_cnt = p_buf->read_retry_count;
    }

    ipr_set_page0x01_as400(p_resource_entry, p_ch, p_buf);

    if ((p_resource_entry->sw_load_id == 0xA17002C2) || /* Seagate 70GB, Jupiter, 10kRPM */
        (p_resource_entry->sw_load_id == 0xA17002C3))   /* Seagate 70GB, Gemini, 10kRPM */
    {
        IPR_SET_MODE(p_ch->read_retry_count, p_buf->read_retry_count, retry_cnt);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_disc_reconn_page *p_ch,
                                struct ipr_disc_reconn_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x02)
            p_dev_cfg->set_page0x02(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x02_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_disc_reconn_page *p_ch,
                                         struct ipr_disc_reconn_page *p_buf)
{
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_disc_reconn_page *p_ch,
                                      struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->buffer_full_ratio, p_buf->buffer_full_ratio, 0x90);
    IPR_SET_MODE(p_ch->buffer_empty_ratio, p_buf->buffer_empty_ratio, 0x80);
    IPR_SET_MODE(p_ch->maximum_burst_size, p_buf->maximum_burst_size, htosis32(0x0030));
}

/*---------------------------------------------------------------------------
 * Purpose: Use the drive's default mode settings for mode page 0x02
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_noop(struct ipr_resource_entry *p_resource_entry,
                                     struct ipr_disc_reconn_page *p_ch,
                                     struct ipr_disc_reconn_page *p_buf)
{
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_starfire_4Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_as400(p_resource_entry, p_ch, p_buf);

    /* Is this a Sailfin or Marlin drive? */
    if (p_resource_entry->sw_load_id == 0xA090061B)
    {
        IPR_SET_MODE(p_ch->dimm, p_buf->dimm, 1);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_scorpion_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_as400(p_resource_entry, p_ch, p_buf);

    /* Is this a Sailfin or Marlin drive? */
    if (p_resource_entry->sw_load_id == 0xA090061B)
    {
        IPR_SET_MODE(p_ch->dimm, p_buf->dimm, 1);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_marlin_17Gb(struct ipr_resource_entry *p_resource_entry,
                                            struct ipr_disc_reconn_page *p_ch,
                                            struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->dimm, p_buf->dimm, 1);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_disc_reconn_page *p_ch,
                                             struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->maximum_burst_size, p_buf->maximum_burst_size, htosis32(0));
    IPR_SET_MODE(p_ch->dimm, p_buf->dimm, 1);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x02 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x02_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_disc_reconn_page *p_ch,
                                                struct ipr_disc_reconn_page *p_buf)
{
    ipr_set_page0x02_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->maximum_burst_size, p_buf->maximum_burst_size, htosis32(0));
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x07 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x07(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_verify_err_rec_page *p_ch,
                                struct ipr_verify_err_rec_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x07)
            p_dev_cfg->set_page0x07(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x07_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x07 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x07_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_verify_err_rec_page *p_ch,
                                         struct ipr_verify_err_rec_page *p_buf)
{
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x07 to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x07_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_verify_err_rec_page *p_ch,
                                      struct ipr_verify_err_rec_page *p_buf)
{
    ipr_set_page0x07_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->per, p_buf->per, 1);
    IPR_SET_MODE(p_ch->dcr, p_buf->dcr, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_caching_page *p_ch,
                                struct ipr_caching_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x08)
            p_dev_cfg->set_page0x08(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x08_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_caching_page *p_ch,
                                         struct ipr_caching_page *p_buf)
{
    IPR_SET_MODE(p_ch->wce, p_buf->wce, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_caching_page *p_ch,
                                      struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->mf, p_buf->mf, 0);
    IPR_SET_MODE(p_ch->rcd, p_buf->rcd, 0);
    IPR_SET_MODE(p_ch->demand_read_retention_priority,
                    p_buf->demand_read_retention_priority, 1);
    IPR_SET_MODE(p_ch->write_retention_priority,
                    p_buf->write_retention_priority, 1);
    IPR_SET_MODE(p_ch->disable_pre_fetch_xfer_len,
                    p_buf->disable_pre_fetch_xfer_len, htosis32(0xffff));
    IPR_SET_MODE(p_ch->max_pre_fetch, p_buf->max_pre_fetch, htosis32(0xffff));
    IPR_SET_MODE(p_ch->max_pre_fetch_ceiling,
                    p_buf->max_pre_fetch_ceiling, htosis32(0xffff));
    IPR_SET_MODE(p_ch->num_cache_segments,
                    p_buf->num_cache_segments, 0x08);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 to the default AS/400 TCQ settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_TCQ(struct ipr_resource_entry *p_resource_entry,
                                    struct ipr_caching_page *p_ch,
                                    struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->rcd, p_buf->rcd, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_scorpion_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->num_cache_segments, p_buf->num_cache_segments, 0x10);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_marlin_17Gb(struct ipr_resource_entry *p_resource_entry,
                                            struct ipr_caching_page *p_ch,
                                            struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->num_cache_segments, p_buf->num_cache_segments, 0x10);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_thresher_8Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->num_cache_segments, p_buf->num_cache_segments, 0x18);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_hammerhead_18Gb(struct ipr_resource_entry *p_resource_entry,
                                                struct ipr_caching_page *p_ch,
                                                struct ipr_caching_page *p_buf)
{
    ipr_set_page0x08_as400(p_resource_entry, p_ch, p_buf);
    IPR_SET_MODE(p_ch->num_cache_segments, p_buf->num_cache_segments, 0x18);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_discovery_36Gb(struct ipr_resource_entry *p_resource_entry,
                                               struct ipr_caching_page *p_ch,
                                               struct ipr_caching_page *p_buf)
{
    IPR_SET_MODE(p_ch->wce, p_buf->wce, 0);
    IPR_SET_MODE(p_ch->mf, p_buf->mf, 0);
    IPR_SET_MODE(p_ch->rcd, p_buf->rcd, 0);

    IPR_SET_MODE(p_ch->demand_read_retention_priority,
                    p_buf->demand_read_retention_priority, 1);
    IPR_SET_MODE(p_ch->write_retention_priority,
                    p_buf->write_retention_priority, 1);
    IPR_SET_MODE(p_ch->disable_pre_fetch_xfer_len,
                    p_buf->disable_pre_fetch_xfer_len, htosis32(0xffff));
    IPR_SET_MODE(p_ch->max_pre_fetch, p_buf->max_pre_fetch, htosis32(0xffff));
    IPR_SET_MODE(p_ch->max_pre_fetch_ceiling,
                    p_buf->max_pre_fetch_ceiling, htosis32(0xffff));
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x08 for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x08_daytona_70Gb(struct ipr_resource_entry *p_resource_entry,
                                             struct ipr_caching_page *p_ch,
                                             struct ipr_caching_page *p_buf)
{
    IPR_SET_MODE(p_ch->wce, p_buf->wce, 0);
    IPR_SET_MODE(p_ch->rcd, p_buf->rcd, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x0a buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x0a(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_control_mode_page *p_ch,
                                struct ipr_control_mode_page *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_ch && p_buf)
    {
        if (p_dev_cfg && p_dev_cfg->set_page0x0a)
            p_dev_cfg->set_page0x0a(p_resource_entry, p_ch, p_buf);
        else
            ipr_set_page0x0a_defaults(p_resource_entry, p_ch, p_buf);
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x0a to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x0a_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_control_mode_page *p_ch,
                                         struct ipr_control_mode_page *p_buf)
{
    IPR_SET_MODE(p_ch->queue_algorithm_modifier,
                    p_buf->queue_algorithm_modifier, 1);
    IPR_SET_MODE(p_ch->qerr, p_buf->qerr, 1);
    IPR_SET_MODE(p_ch->dque, p_buf->dque, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x0a to the default TCQ AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x0a_noTCQ(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_control_mode_page *p_ch,
                                      struct ipr_control_mode_page *p_buf)
{
    ipr_set_page0x0a_defaults(p_resource_entry, p_ch, p_buf);

    IPR_SET_MODE(p_ch->queue_algorithm_modifier,
                    p_buf->queue_algorithm_modifier, 0);
    IPR_SET_MODE(p_ch->qerr, p_buf->qerr, 0);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x0a to the default AS/400 settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x0a_as400(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_control_mode_page *p_ch,
                                      struct ipr_control_mode_page *p_buf)
{
    ipr_set_page0x0a_defaults(p_resource_entry, p_ch, p_buf);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x20 to the default settings for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x20_defaults(struct ipr_resource_entry *p_resource_entry,
                                         struct ipr_ioa_dasd_page_20 *p_buf)
{
    p_buf->max_TCQ_depth = 64;
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x20 to the default settings for mode select
 *          for Tagged Command Queuing
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x20_noTCQ(struct ipr_resource_entry *p_resource_entry,
                                      struct ipr_ioa_dasd_page_20 *p_buf)
{
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page 0x20 buffer for mode select
 * Lock State: io_request_lock assumed to be held
 * Returns: nothing
 *---------------------------------------------------------------------------*/
static void ipr_set_page0x20(struct ipr_resource_entry *p_resource_entry,
                                struct ipr_ioa_dasd_page_20 *p_buf)
{
    const struct ipr_dev_config *p_dev_cfg = ipr_get_dev_config(p_resource_entry);

    if (p_dev_cfg && p_dev_cfg->set_page0x20)
        p_dev_cfg->set_page0x20(p_resource_entry, p_buf);
    else
        ipr_set_page0x20_defaults(p_resource_entry, p_buf);
}

/*---------------------------------------------------------------------------
 * Purpose: Setup mode page buffer for a mode select to a DASD
 * Lock State: io_request_lock assumed to be held
 * Returns: mode page allocation length
 *---------------------------------------------------------------------------*/
static u8 ipr_set_mode_pages(struct ipr_shared_config *p_shared_cfg,
                                struct ipr_resource_entry *p_resource_entry,
                                struct ipr_mode_parm_hdr *p_mode_parm,
                                struct ipr_mode_parm_hdr *p_changeable_pages)
{
    struct ipr_vendor_unique_page *p_vendor_unique_pg, *p_ch_vendor_unique;
    struct ipr_rw_err_mode_page *p_rw_err_mode_pg, *p_ch_rw_err_mode_pg;
    struct ipr_disc_reconn_page *p_disc_reconn_pg, *p_ch_disc_reconn_pg;
    struct ipr_verify_err_rec_page *p_verify_err_rec_pg, *p_ch_verify_err_rec_pg;
    struct ipr_caching_page *p_caching_pg, *p_ch_caching_pg;
    struct ipr_control_mode_page *p_control_pg, *p_ch_control_pg;
    struct ipr_ioa_dasd_page_20 *p_ioa_dasd_pg_20;
    struct ipr_mode_page_hdr *p_mode_hdr;
    int i;
    u8 alloc_len;

#ifdef IPR_DEBUG_MODE_PAGES
    printk("Before modification: for 0x%04X"IPR_EOL, p_resource_entry->type);
    ipr_print_mode_sense_buffer(p_mode_parm);

    printk("Change mask: for 0x%04X"IPR_EOL, p_resource_entry->type);
    ipr_print_mode_sense_buffer(p_changeable_pages);
#endif

    /* Get pages ready for mode select */
    for (i = 0; i < 0x3f; i++)
    {
        p_mode_hdr = ipr_get_mode_page(p_mode_parm, i, sizeof(struct ipr_mode_page_hdr));

        if (p_mode_hdr != NULL)
            p_mode_hdr->parms_saveable = 0;
    }

    p_vendor_unique_pg = ipr_get_mode_page(p_mode_parm, 0x00,
                                              sizeof(struct ipr_vendor_unique_page));
    p_ch_vendor_unique = ipr_get_mode_page(p_changeable_pages, 0x00,
                                              sizeof(struct ipr_vendor_unique_page));

    ipr_set_page0x00(p_resource_entry, p_ch_vendor_unique, p_vendor_unique_pg);

    p_rw_err_mode_pg = ipr_get_mode_page(p_mode_parm, 0x01,
                                            sizeof(struct ipr_rw_err_mode_page));
    p_ch_rw_err_mode_pg = ipr_get_mode_page(p_changeable_pages, 0x01,
                                               sizeof(struct ipr_rw_err_mode_page));

    ipr_set_page0x01(p_resource_entry, p_ch_rw_err_mode_pg, p_rw_err_mode_pg);

    p_disc_reconn_pg = ipr_get_mode_page(p_mode_parm, 0x02, sizeof(struct ipr_disc_reconn_page));
    p_ch_disc_reconn_pg = ipr_get_mode_page(p_changeable_pages, 0x02, sizeof(struct ipr_disc_reconn_page));

    ipr_set_page0x02(p_resource_entry, p_ch_disc_reconn_pg, p_disc_reconn_pg);

    p_verify_err_rec_pg = ipr_get_mode_page(p_mode_parm, 0x07,
                                               sizeof(struct ipr_verify_err_rec_page));
    p_ch_verify_err_rec_pg = ipr_get_mode_page(p_changeable_pages, 0x07,
                                                  sizeof(struct ipr_verify_err_rec_page));

    ipr_set_page0x07(p_resource_entry, p_ch_verify_err_rec_pg, p_verify_err_rec_pg);

    p_caching_pg = ipr_get_mode_page(p_mode_parm, 0x08, sizeof(struct ipr_caching_page));
    p_ch_caching_pg = ipr_get_mode_page(p_changeable_pages, 0x08, sizeof(struct ipr_caching_page));

    ipr_set_page0x08(p_resource_entry, p_ch_caching_pg, p_caching_pg);

    p_control_pg = ipr_get_mode_page(p_mode_parm, 0x0a,
                                        sizeof(struct ipr_control_mode_page));
    p_ch_control_pg = ipr_get_mode_page(p_changeable_pages, 0x0a,
                                           sizeof(struct ipr_control_mode_page));

    ipr_set_page0x0a(p_resource_entry, p_ch_control_pg, p_control_pg);

    if (p_shared_cfg->set_mode_page_20)
    {
        p_ioa_dasd_pg_20 = ipr_get_mode_page(p_mode_parm, 0x20, sizeof(struct ipr_ioa_dasd_page_20));
        ipr_set_page0x20(p_resource_entry, p_ioa_dasd_pg_20);
    }

#ifdef IPR_DEBUG_MODE_PAGES
    printk("After modification: for 0x%04X"IPR_EOL, p_resource_entry->type);
    ipr_print_mode_sense_buffer(p_mode_parm);
#endif

    alloc_len = p_mode_parm->length + 1;

    p_mode_parm->length = 0;
    p_mode_parm->medium_type = 0;
    p_mode_parm->device_spec_parms = 0;

    return alloc_len;
}


/*---------------------------------------------------------------------------
 * Purpose: Get a buffer for use in the DASD init job
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to the buffer
 *---------------------------------------------------------------------------*/
static struct ipr_dasd_init_bufs
*ipr_get_dasd_init_buffer(struct ipr_data *ipr_cfg)
{
    struct ipr_dasd_init_bufs *p_cur_buf = ipr_cfg->free_init_buf_head;

    if (p_cur_buf == NULL)
        return NULL;

    ipr_cfg->free_init_buf_head = p_cur_buf->p_next;
    p_cur_buf->p_next = NULL;
    return p_cur_buf;
}

/*---------------------------------------------------------------------------
 * Purpose: Frees a buffer for use in the DASD init job
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_put_dasd_init_buffer(struct ipr_data *ipr_cfg,
                                        struct ipr_dasd_init_bufs *p_dasd_init_buffer)
{
    struct ipr_dasd_init_bufs *p_cur_buf = ipr_cfg->free_init_buf_head;

    if (ipr_cfg->free_init_buf_head == NULL)
        ipr_cfg->free_init_buf_head = p_dasd_init_buffer;
    else
    {
        for (p_cur_buf = ipr_cfg->free_init_buf_head;
             p_cur_buf->p_next != NULL;
             p_cur_buf = p_cur_buf->p_next)
        {
        }
        p_cur_buf->p_next = p_dasd_init_buffer;
    }
    p_dasd_init_buffer->p_next = NULL;
}

/*---------------------------------------------------------------------------
 * Purpose: Wait for an IODEBUG ACK from the IOA
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS   - Success
 *          IPR_RC_FAILED    - Failed
 *---------------------------------------------------------------------------*/
static int ipr_wait_iodebug_ack(struct ipr_data *ipr_cfg,
                                   int max_delay)
{
    volatile u32 pcii_reg;
    int delay = 1;
    int rc = IPR_RC_FAILED;  /* initialize rc to failed in case of timeout */

    /* Read interrupt reg until IOA signals IO Debug Acknowledge */
    while (delay < max_delay)
    {
        pcii_reg = readl(ipr_cfg->regs.sense_interrupt_reg);

        if (pcii_reg & IPR_PCII_IO_DEBUG_ACKNOWLEDGE)
        {
            rc = IPR_RC_SUCCESS;
            break;
        }

        /* Delay and then double delay time for next iteration */
        ipr_udelay(delay);
        delay += delay;
    }
    return (rc);
}

/*---------------------------------------------------------------------------
 * Purpose: Internal routine for obtaining a continuous section of LDUMP data
 * Lock State: io_request_lock assumed to be held
 * Returns: IPR_RC_SUCCESS
 *          IPR_RC_FAILED
 *---------------------------------------------------------------------------*/
int ipr_get_ldump_data_section(struct ipr_shared_config *p_shared_cfg,
                                  u32 fmt2_start_addr,
                                  u32 *p_dest,
                                  u32 length_in_words)
{
    u32 temp_pcii_reg;
    int i, delay = 0;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    /* Write IOA interrupt reg starting LDUMP state  */
    writel((IPR_UPROCI_RESET_ALERT | IPR_UPROCI_IO_DEBUG_ALERT),
           ipr_cfg->regs.set_uproc_interrupt_reg);

    /* Wait for IO debug acknowledge */
    if (IPR_RC_FAILED ==
        ipr_wait_iodebug_ack(ipr_cfg,
                             IPR_LDUMP_MAX_LONG_ACK_DELAY_IN_USEC))
    {
        ipr_log_err("IOA long data transfer timeout"IPR_EOL);
        return IPR_RC_FAILED;
    }

    /* Signal LDUMP interlocked - clear IO debug ack */
    writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
           ipr_cfg->regs.clr_interrupt_reg);

    /* Write Mailbox with starting address */
    writel(fmt2_start_addr,
           p_shared_cfg->ioa_mailbox);

    /* Signal address valid - clear IOA Reset alert */
    writel(IPR_UPROCI_RESET_ALERT,
           ipr_cfg->regs.clr_uproc_interrupt_reg);

    for (i=0; i<length_in_words; i++)
    {
        /* Wait for IO debug acknowledge*/
        if (IPR_RC_FAILED ==
            ipr_wait_iodebug_ack(ipr_cfg,
                                 IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC))
        {
            ipr_log_err("IOA short data transfer timeout"IPR_EOL);
            return IPR_RC_FAILED;
        }

        /* Read data from mailbox and increment destination pointer*/
        *p_dest = htosis32(readl(p_shared_cfg->ioa_mailbox));
        p_dest++;

        /* For all but the last word of data, signal data received */
        if (i < (length_in_words-1))
            /* Signal dump data received - Clear IO debug Ack */
            writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
                   ipr_cfg->regs.clr_interrupt_reg);
    }

    /* Signal end of block transfer. Set reset alert then clear IO debug ack */
    writel(IPR_UPROCI_RESET_ALERT,
           ipr_cfg->regs.set_uproc_interrupt_reg);

    writel(IPR_UPROCI_IO_DEBUG_ALERT,
           ipr_cfg->regs.clr_uproc_interrupt_reg);

    /* Signal dump data received - Clear IO debug Ack */
    writel(IPR_PCII_IO_DEBUG_ACKNOWLEDGE,
           ipr_cfg->regs.clr_interrupt_reg);

    /* Wait for IOA to signal LDUMP exit - IOA reset alert will be cleared */
    while (delay < IPR_LDUMP_MAX_SHORT_ACK_DELAY_IN_USEC)
    {
        temp_pcii_reg = readl(ipr_cfg->regs.sense_uproc_interrupt_reg);

        if (!(temp_pcii_reg & IPR_UPROCI_RESET_ALERT))
            break;

        /* Delay 10 usecs. */
        ipr_udelay(10);
        delay += (10);
    }

    return IPR_RC_SUCCESS;
}

/*---------------------------------------------------------------------------
 * Purpose: Return a pointer to the internal trace and its length
 * Lock State: io_request_lock assumed to be held
 * Returns: pointer to internal trace, length
 *---------------------------------------------------------------------------*/
void ipr_get_internal_trace(struct ipr_shared_config *p_shared_cfg,
                               u32 **trace_block_address,
                               u32 *trace_block_length)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;

    if (ipr_cfg->trace == NULL)
        *trace_block_length = 0;
    else
        *trace_block_length = (sizeof(struct ipr_internal_trace_entry) *
                               IPR_NUM_TRACE_ENTRIES);

    *trace_block_address = (u32 *)ipr_cfg->trace;
}

/*---------------------------------------------------------------------------
 * Purpose: Copy the internal trace to caller's buffer
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
void ipr_copy_internal_trace_for_dump(struct ipr_shared_config *p_shared_cfg,
                                         u32 *p_buffer,
                                         u32 buffer_len)
{
    int i;
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    struct ipr_internal_trace_entry *p_temp_trace_entry;

    ENTER;

    if (ipr_cfg->trace == NULL)
        return;

    p_temp_trace_entry = (struct ipr_internal_trace_entry *)p_buffer;

    for (i = 0;
         (i < IPR_NUM_TRACE_ENTRIES) && (buffer_len >= sizeof(struct ipr_internal_trace_entry));
         i++, buffer_len -= sizeof(struct ipr_internal_trace_entry), p_temp_trace_entry++)
    {
        p_temp_trace_entry->time = htosis32(ipr_cfg->trace[i].time);
        p_temp_trace_entry->op_code = ipr_cfg->trace[i].op_code;
        p_temp_trace_entry->type = ipr_cfg->trace[i].type;
        p_temp_trace_entry->device_type = ipr_cfg->trace[i].device_type;
        p_temp_trace_entry->host_ioarcb_index = htosis16(ipr_cfg->trace[i].host_ioarcb_index);
        p_temp_trace_entry->xfer_len = htosis32(ipr_cfg->trace[i].xfer_len);
        p_temp_trace_entry->data.res_addr = htosis32(ipr_cfg->trace[i].data.res_addr);
    }

    LEAVE;
}

/*---------------------------------------------------------------------------
 * Purpose: Check the backplane to see if 15K devices need to be blocked
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_check_backplane(struct ipr_shared_config *p_shared_cfg,
                                   struct ipr_config_table_entry *cfgte)
{
    struct ipr_data *ipr_cfg = p_shared_cfg->p_data;
    const struct ipr_backplane_table_entry *p_bte;
    int bus_num, ii;

    if (IPR_IS_SES_DEVICE(cfgte->std_inq_data))
    {
        bus_num = cfgte->resource_address.bus;

        if (bus_num > IPR_MAX_NUM_BUSES)
        {
            ipr_log_err("Invalid resource address returned for SES"IPR_EOL);
            ipr_log_err("Resource address: 0x%08x"IPR_EOL,
                           IPR_GET_PHYSICAL_LOCATOR(cfgte->resource_address));
            return;
        }

        for /*! Loop through entries of the backplane table */
            (ii = 0,
             p_bte = &ipr_backplane_table[0];
             ii < (sizeof(ipr_backplane_table)/
                   sizeof(struct ipr_backplane_table_entry));
             ii++, p_bte++)
        {
            /* Does the Product ID for this SCSI device match this entry in
             the backplane table */
            if (memcmp(cfgte->std_inq_data.vpids.product_id,
                              p_bte->product_id, IPR_PROD_ID_LEN) == 0)
            {
                if (p_bte->block_15k_devices)
                {
                    /* Set the bit corresponding to the bus number */
                    ipr_cfg->non15k_ses |= 1 << bus_num;
                    ipr_cfg->p_ssd_header->num_records = IPR_NUM_NON15K_DASD;
                    ipr_cfg->p_ssd_header->data_length =
                        htosis16(sizeof(ipr_supported_dev_list.dev_non15k) +
                                 sizeof(struct ipr_ssd_header));
                }
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * Purpose: Get a resource entry for use in the resource table
 * Lock State: io_request_lock assumed to be held
 * Returns: Pointer to the resource entry
 *---------------------------------------------------------------------------*/
static struct ipr_resource_dll
*ipr_get_resource_entry(struct ipr_shared_config *p_shared_cfg)
{
    struct ipr_resource_dll *p_resource_dll;

    /* Grab an available resource entry */
    p_resource_dll = p_shared_cfg->rsteFreeH;

    if (p_resource_dll == NULL)
    {
        /* No resources left */
        ipr_beg_err(KERN_ERR);
        ipr_log_err("Max number of devices allowed for adapter exceeded"IPR_EOL);
        ipr_log_ioa_physical_location(p_shared_cfg->p_location, KERN_ERR);
        ipr_end_err(KERN_ERR);
        return NULL;
    }

    p_shared_cfg->rsteFreeH  = p_resource_dll->next;

    if (p_shared_cfg->rsteFreeH == NULL)
        p_shared_cfg->rsteFreeT = NULL;

    /* Now put that resource entry in allocated list */
    if (p_shared_cfg->rsteUsedT == NULL)
    {
        /* if the tail is NULL, the head MUST be NULL as well
         which allows the entry to be locked to the head */
        p_shared_cfg->rsteUsedH = p_resource_dll;
    }
    else
    {
        p_shared_cfg->rsteUsedT->next = p_resource_dll;
    }

    p_resource_dll->prev = p_shared_cfg->rsteUsedT;
    p_shared_cfg->rsteUsedT = p_resource_dll;
    p_shared_cfg->rsteUsedT->next = NULL;

    memset(&p_resource_dll->data, 0, sizeof(struct ipr_resource_entry));
    return p_resource_dll;
}

/*---------------------------------------------------------------------------
 * Purpose: Free a resource entry
 * Lock State: io_request_lock assumed to be held
 * Returns: Nothing
 *---------------------------------------------------------------------------*/
static void ipr_put_resource_entry(struct ipr_shared_config *p_shared_cfg,
                                      struct ipr_resource_dll *p_resource_dll)
{

    /* Remove from allocated list */
    if ((p_resource_dll == p_shared_cfg->rsteUsedH) &&
        (p_resource_dll == p_shared_cfg->rsteUsedT))
    {
        p_shared_cfg->rsteUsedH = NULL;
        p_shared_cfg->rsteUsedT = NULL;
    }    
    else if (p_resource_dll == p_shared_cfg->rsteUsedH)
    {
        p_shared_cfg->rsteUsedH = p_resource_dll->next;
        p_shared_cfg->rsteUsedH->prev = NULL;
    }
    else if (p_resource_dll == p_shared_cfg->rsteUsedT)
    {
        p_shared_cfg->rsteUsedT = p_resource_dll->prev;
        p_shared_cfg->rsteUsedT->next = NULL;
    }
    else
    {
        p_resource_dll->prev->next = p_resource_dll->next;  
        p_resource_dll->next->prev = p_resource_dll->prev;
    }

    /* Now add this resource entry to available list */
    if (p_shared_cfg->rsteFreeT == NULL)
    {
        /* if the tail is NULL, the head MUST be NULL as well
         which allows the entry to be locked to the head */
        p_shared_cfg->rsteFreeH = p_resource_dll;
    }
    else
    {
        p_shared_cfg->rsteFreeT->next = p_resource_dll;
    }
    p_resource_dll->prev = p_shared_cfg->rsteFreeT;
    p_shared_cfg->rsteFreeT = p_resource_dll;
    p_shared_cfg->rsteFreeT->next = NULL;
}
