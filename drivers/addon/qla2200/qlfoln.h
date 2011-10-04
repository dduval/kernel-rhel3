/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */



#define QLMULTIPATH_MAGIC 'y'
#define	QL_IOCTL_BASE(idx)	\
    _IOWR(QLMULTIPATH_MAGIC, idx, sizeof(EXT_IOCTL))

#if !defined(QL_IOCTL_CMD)
#if defined(QLA_CONFIG_COMPAT)
#define	QL_IOCTL_CMD(idx)	(QL_IOCTL_BASE(idx) - 0x40000)
#else
#define	QL_IOCTL_CMD(idx)	QL_IOCTL_BASE(idx)
#endif
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

#define FO_CC_GET_LBTYPE_OS        	\
    QL_IOCTL_CMD(0xd3)				/* 0xd3 */	
#define FO_CC_SET_LBTYPE_OS        	\
    QL_IOCTL_CMD(0xd4)				/* 0xd4 */
#define FO_CC_END_IDX		0xd4		/* fo ioctl end idx */


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

