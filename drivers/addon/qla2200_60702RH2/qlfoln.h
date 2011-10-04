/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.4.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/


#define QLMULTIPATH_MAGIC 'y'
#define	QL_IOCTL_BASE(idx)	\
    _IOWR(QLMULTIPATH_MAGIC, idx, sizeof(EXT_IOCTL))

#if CONFIG_PPC64 || CONFIG_X86_64
#define	QL_IOCTL_CMD(idx)	(QL_IOCTL_BASE(idx) - 0x40000)
#else
#define	QL_IOCTL_CMD(idx)	QL_IOCTL_BASE(idx)
#endif

/*************************************************************
 * Failover ioctl command codes range from 0xc0 to 0xdf.
 * The foioctl command code end index must be updated whenever
 * adding new commands. 
 *************************************************************/
#define FO_CC_START_IDX 	0xc8	/* foioctl cmd start index */

#define FO_CC_GET_PARAMS_OS             \
    QL_IOCTL_CMD(0xc8)
#define FO_CC_SET_PARAMS_OS             \
    QL_IOCTL_CMD(0xc9)
#define FO_CC_GET_PATHS_OS              \
    QL_IOCTL_CMD(0xca)
#define FO_CC_SET_CURRENT_PATH_OS       \
    QL_IOCTL_CMD(0xcb)
#define FO_CC_GET_HBA_STAT_OS           \
    QL_IOCTL_CMD(0xcc)
#define FO_CC_RESET_HBA_STAT_OS         \
    QL_IOCTL_CMD(0xcd)
#define FO_CC_GET_LUN_DATA_OS           \
    QL_IOCTL_CMD(0xce)
#define FO_CC_SET_LUN_DATA_OS           \
    QL_IOCTL_CMD(0xcf)
#define FO_CC_GET_TARGET_DATA_OS        \
    QL_IOCTL_CMD(0xd0)
#define FO_CC_SET_TARGET_DATA_OS        \
    QL_IOCTL_CMD(0xd1)
#define FO_CC_GET_FO_DRIVER_VERSION_OS  \
    QL_IOCTL_CMD(0xd2)

#define FO_CC_END_IDX 	0xd2	/* foioctl cmd end index */


#define BOOLEAN uint8_t
#define MAX_LUNS_OS	256

/* Driver attributes bits */
#define DRVR_FO_ENABLED		0x1	/* bit 0 */


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

