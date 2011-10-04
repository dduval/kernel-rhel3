#define QLA_MODEL_NAMES         0x50

/*
 * Adapter model names.
 */
char	*qla2x00_model_name[QLA_MODEL_NAMES] = {
	"QLA2340",	/* 0x100 */
	"QLA2342",	/* 0x101 */
	"QLA2344",	/* 0x102 */
	"QCP2342",	/* 0x103 */
	"QSB2340",	/* 0x104 */
	"QSB2342",	/* 0x105 */
	"QLA2310",	/* 0x106 */
	"QLA2332",	/* 0x107 */
	"QCP2332",	/* 0x108 */
	"QCP2340",	/* 0x109 */
	"QLA2342",	/* 0x10a */
	"QCP2342",	/* 0x10b */
	"QLA2350",	/* 0x10c */
	"QLA2352",	/* 0x10d */
	"QLA2352",	/* 0x10e */
	"HPQ SVS",	/* 0x10f */
	"HPQ SVS",	/* 0x110 */
	" ",		/* 0x111 */
	" ",		/* 0x112 */
	" ",		/* 0x113 */
	" ",		/* 0x114 */
 	"QLA2360",	/* 0x115 */
 	"QLA2362",	/* 0x116 */
	"QLE2360",	/* 0x117 */
	"QLE2362",	/* 0x118 */
 	"QLA200",	/* 0x119 */
	"QLA200C",	/* 0x11a */
	"QLA200P",	/* 0x11b */
	"QLA200P",	/* 0x11c */
	" ",		/* 0x11d */
	" ",		/* 0x11e */
	" ",		/* 0x11f */
	" ",		/* 0x120 */
	" ",		/* 0x121 */
	" ",		/* 0x122 */
	" ",		/* 0x123 */
	" ",		/* 0x124 */
	" ",		/* 0x125 */
	" ",		/* 0x126 */
	" ",		/* 0x127 */
	" ",		/* 0x128 */
	" ",		/* 0x129 */
	" ",		/* 0x12a */
	" ",		/* 0x12b */
	" ",		/* 0x12c */
	" ",		/* 0x12d */
	" ",		/* 0x12e */
	"QLA210",	/* 0x12f */
	"EMC 250",	/* 0x130 */
	"HP A7538A",	/* 0x131 */

	"QLA210",	/* 0x132 */
	"QLA2460",	/* 0x133 */
	"QLA2462",	/* 0x134 */
	"QMC2462",	/* 0x135 */
	"QMC2462S",	/* 0x136 */
	"QLE2460",	/* 0x137 */
	"QLE2462",	/* 0x138 */
	"QME2462",	/* 0x139 */
	"QLA2440",	/* 0x13a */
	"QLA2442",	/* 0x13b */
	"QSM2442",	/* 0x13c */
	"QSM2462",	/* 0x13d */
	"QLE210",	/* 0x13e */
	"QLE220",	/* 0x13f */
	"QLA2460",	/* 0x140 */
	"QLA2462",	/* 0x141 */
	"QLE2460",	/* 0x142 */
	"QLE2462",	/* 0x143 */
	"QEM2462",	/* 0x144 */
	"QLE2440", 	/* 0x145 */
	"QLE2464",	/* 0x146 */
	"QLA2440",	/* 0x147 */
	" ",		/* 0x148 */
	"QLA2340"	/* 0x149 */

};

static char	*qla2x00_model_desc[QLA_MODEL_NAMES] = {
	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x100 */
	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x101 */
	"133MHz PCI-X to 2Gb FC, Quad Channel",		/* 0x102 */
	" ",						/* 0x103 */
	" ",						/* 0x104 */
	" ",						/* 0x105 */
	" ",						/* 0x106 */
	" ",						/* 0x107 */
	" ",						/* 0x108 */
	" ",						/* 0x109 */
	" ",						/* 0x10a */
	" ",						/* 0x10b */
	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x10c */
	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x10d */
 	" ",						/* 0x10e */
 	"HPQ SVS HBA- Initiator device",		/* 0x10f */
 	"HPQ SVS HBA- Target device",			/* 0x110 */
	" ",						/* 0x111 */
	" ",						/* 0x112 */
	" ",						/* 0x113 */
	" ",						/* 0x114 */
 	"133MHz PCI-X to 2Gb FC Single Channel",	/* 0x115 */
 	"133MHz PCI-X to 2Gb FC Dual Channel",		/* 0x116 */
	"PCI-Express to 2Gb FC, Single Channel",	/* 0x117 */
	"PCI-Express to 2Gb FC, Dual Channel",		/* 0x118 */
 	"133MHz PCI-X to 2Gb FC Optical",		/* 0x119 */
	"133MHz PCI-X to 2Gb FC Copper",		/* 0x11a */
	"133MHz PCI-X to 2Gb FC SFP",			/* 0x11b */
	"133MHz PCI-X to 2Gb FC SFP",			/* 0x11c */
	" ",						/* 0x11d */
	" ",						/* 0x11e */
	" ",						/* 0x11f */
	" ",						/* 0x120 */
	" ",						/* 0x121 */
	" ",						/* 0x122 */
	" ",						/* 0x123 */
	" ",						/* 0x124 */
	" ",						/* 0x125 */
	" ",						/* 0x126 */
	" ",						/* 0x127 */
	" ",						/* 0x128 */
	" ",						/* 0x129 */
	" ",						/* 0x12a */
	" ",						/* 0x12b */
	" ",						/* 0x12c */
	" ",						/* 0x12d */
	" ",						/* 0x12e */
	"133MHz PCI-X to 2Gb FC SFF",			/* 0x12f */
	"133MHz PCI-X to 2Gb FC SFF",			/* 0x130 */
	"HP 1p2g QLA2340"				/* 0x131 */
	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x132 */
	"PCI-X 2.0 to 4Gb FC, Single Channel",		/* 0x133 */
	"PCI-X 2.0 to 4Gb FC, Dual Channel",		/* 0x134 */
	"IBM eServer BC 4Gb FC Expansion Card",		/* 0x135 */
	"IBM eServer BC 4Gb FC Expansion Card SFF",	/* 0x136 */
	"PCI-Express to 4Gb FC, Single Channel",	/* 0x137 */
	"PCI-Express to 4Gb FC, Dual Channel",		/* 0x138 */
	"Dell PCI-Express to 4Gb FC, Dual Channel",	/* 0x139 */
	"PCI-X 1.0 to 4Gb FC, Single Channel",		/* 0x13a */
	"PCI-X 1.0 to 4Gb FC, Dual Channel",		/* 0x13b */
	"Server I/O Module 4Gb FC, Single Channel",	/* 0x13c */
	"Server I/O Module 4Gb FC, Single Channel",	/* 0x13d */
	"PCI-Express to 2Gb FC, Single Channel",	/* 0x13e */
	"PCI-Express to 4Gb FC, Single Channel"		/* 0x13f */
	"Sun PCI-X 2.0 to 4Gb FC, Single Channel",	/* 0x140 */
	"Sun PCI-X 2.0 to 4Gb FC, Dual Channel",	/* 0x141 */
	"Sun PCI-Express to 2Gb FC, Single Channel",	/* 0x142 */
	"Sun PCI-Express to 4Gb FC, Single Channel",	/* 0x143 */
	"Qlogic Server I/O Module 4Gb FC, Dual Channel",/* 0x144 */
	"PCI-E to 4Gb FC, Single Channel",		/* 0x145 */
	"PCI-E to 4Gb FC, Quad Channel",		/* 0x146 */
	"PCI-X 2.0 to 4Gb FC, Single Channel",		/* 0x147 */
	" ",						/* 0x148 */
	"SUN - 133MHz PCI-X to 2Gb FC, single channel"	/* 0x149 */

};


struct cfg_device_info {
	const char *vendor;
	const char *model;
	const int  flags;	/* bit 0 (0x1) -- This bit will translate the real 
				   WWNN to the common WWNN for the target AND
				   XP_DEVICE */
				/* bit 1 -- MSA 1000  */
				/* bit 2 -- EVA  */
				/* bit 3 -- DISABLE FAILOVER  */
				/* bit 4 -- Adaptec failover */
				/* bit 5 -- EVA AA failover */
				/* bit 6 -- IBM */
				/* bit 7 -- MSA AA failover */
				/* bit 8 -- HDS */
	const int  notify_type;	/* support the different types: 1 - 4 */
	int	( *fo_combine)(void *,
		 uint16_t, fc_port_t *, uint16_t );
	 /* Devices which support Report Target Port Groups */
        int     (*fo_target_port) (fc_port_t *, fc_lun_t *, int);
	int	( *fo_detect)(void);
	int	( *fo_notify)(void);
	int	( *fo_select)(void);
};


static struct cfg_device_info cfg_device_list[] = {

	{"HP", "MSA CONTROLLER", BIT_7, FO_NOTIFY_TYPE_TPGROUP_CDB,
		qla2x00_combine_by_lunid, qla2x00_get_target_ports,
		NULL, NULL, NULL},
	{"HP", "MSA VOLUME", BIT_7, FO_NOTIFY_TYPE_TPGROUP_CDB,
		qla2x00_combine_by_lunid, qla2x00_get_target_ports,
		NULL, NULL, NULL},
	{"COMPAQ", "MSA1000", BIT_1, FO_NOTIFY_TYPE_SPINUP,
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HITACHI", "OPEN-", BIT_0, FO_NOTIFY_TYPE_NONE,
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "OPEN-", BIT_0, FO_NOTIFY_TYPE_NONE,
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"COMPAQ", "HSV110 (C)COMPAQ", 4, FO_NOTIFY_TYPE_SPINUP,
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "HSV100", BIT_2, FO_NOTIFY_TYPE_SPINUP,
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"DEC", "HSG80", BIT_3, FO_NOTIFY_TYPE_NONE,
		qla2x00_export_target, NULL, NULL, NULL },
	{"IBM", "DS400", BIT_4, FO_NOTIFY_TYPE_TPGROUP_CDB,
		qla2x00_combine_by_lunid, qla2x00_get_target_ports,
		NULL, NULL, NULL},
	{"HP", "HSV210", BIT_5, FO_NOTIFY_TYPE_TPGROUP_CDB,
		qla2x00_combine_by_lunid, qla2x00_get_target_ports,
		NULL, NULL, NULL},
	{"COMPAQ", "HSV111 (C)COMPAQ", BIT_5, FO_NOTIFY_TYPE_TPGROUP_CDB,
                qla2x00_combine_by_lunid, qla2x00_get_target_ports,
                NULL, NULL, NULL},
	{"HP", "HSV101", BIT_5, FO_NOTIFY_TYPE_TPGROUP_CDB,
                qla2x00_combine_by_lunid, qla2x00_get_target_ports,
                NULL, NULL, NULL},
	{"HP", "HSV200", BIT_5, FO_NOTIFY_TYPE_TPGROUP_CDB,
                qla2x00_combine_by_lunid, qla2x00_get_target_ports,
                NULL, NULL, NULL},
 	{"HITACHI", "DF600", BIT_8, FO_NOTIFY_TYPE_NONE,   
 		qla2x00_combine_by_lunid, NULL, NULL, NULL },

	/*
	    * Must be at end of list...
	 */
	{NULL, NULL }
};

/*****************************************/
/*   ISP Boards supported by this driver */
/*****************************************/
#define QLA2X00_VENDOR_ID   0x1077
#define QLA2100_DEVICE_ID   0x2100
#define QLA2200_DEVICE_ID   0x2200
#define QLA2200A_DEVICE_ID  0x2200A
#define QLA2300_DEVICE_ID   0x2300
#define QLA2312_DEVICE_ID   0x2312
#define QLA2322_DEVICE_ID   0x2322
#define QLA6312_DEVICE_ID   0x6312
#define QLA6322_DEVICE_ID   0x6322
#define QLA2400_DEVICE_ID   0x2400
#define QLA2422_DEVICE_ID   0x2422
#define QLA2432_DEVICE_ID   0x2432
#define QLA2512_DEVICE_ID   0x2512
#define QLA2522_DEVICE_ID   0x2522
#define QLA5422_DEVICE_ID   0x5422
#define QLA5432_DEVICE_ID   0x5432

//#define QLAFBLITE_DEVICE_ID   	   /* Not Known yet */	
#define QLA2200A_RISC_ROM_VER  4
#define FPM_2300            6
#define FPM_2310            7

#if defined(ISP2100)
#define NUM_OF_ISP_DEVICES  2
static struct pci_device_id qla2100_pci_tbl[] =
{
	{QLA2X00_VENDOR_ID, QLA2100_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};
MODULE_DEVICE_TABLE(pci, qla2100_pci_tbl);
#endif
#if defined(ISP2200)
#define NUM_OF_ISP_DEVICES  2
static struct pci_device_id qla2200_pci_tbl[] =
{
	{QLA2X00_VENDOR_ID, QLA2200_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};
MODULE_DEVICE_TABLE(pci, qla2200_pci_tbl);
#endif

#if defined(ISP2300)
#define NUM_OF_ISP_DEVICES  10
static struct pci_device_id qla2300_pci_tbl[] =
{
	{QLA2X00_VENDOR_ID, QLA2300_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2312_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2322_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA6312_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA6322_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2422_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2432_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA5422_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA5432_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};
MODULE_DEVICE_TABLE(pci, qla2300_pci_tbl);
#endif

struct qla_fw_info {
	unsigned short addressing;      /* addressing method used to load fw */
#define FW_INFO_ADDR_NORMAL     0
#define FW_INFO_ADDR_EXTENDED   1
#define FW_INFO_ADDR_NOMORE     0xffff
	unsigned short *fwcode;         /* pointer to FW array */
	unsigned short *fwlen;          /* number of words in array */
	unsigned short *fwstart;        /* start address for F/W */
	unsigned long *lfwstart;        /* start address (long) for 
	    				 * extended F/W Load */
};

/*
 * PCI driver interface definitions
 */
#define ISP21XX_FW_INDEX	0
#define ISP22XX_FW_INDEX	0
#define ISP23XX_FW_INDEX	0
#define ISP232X_FW_INDEX	2
#define ISP24XX_FW_INDEX	6

typedef struct _qlaboards
{
        unsigned char   bdName[9];       /* Board ID String             */
        unsigned long   device_id;       /* Device ID                   */
        int   numPorts;                  /* number of loops on adapter  */
        unsigned char   *fwver;          /* Ptr to F/W version array    */
	struct qla_fw_info *fwinfo;
}  qla_boards_t;


static struct qla_fw_info qla_fw_tbl[] = {
#if defined(ISP2100)
	/* Start of 21xx firmware list */
	{
	 FW_INFO_ADDR_NORMAL, &fw2100tp_code01[0],
	 &fw2100tp_length01, &fw2100tp_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
#endif

#if defined(ISP2200)
	/* Start of 22xx firmware list */
	{
	   FW_INFO_ADDR_NORMAL, &fw2200tp_code01[0],
	   &fw2200tp_length01, &fw2200tp_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
#endif

#if defined(ISP2300)
	/* 0 - Start of 23xx/6312 firmware list */
	{
		FW_INFO_ADDR_NORMAL, &fw2300ipx_code01[0],
		&fw2300ipx_length01, &fw2300ipx_addr01, 
	},

	/* End of 23xx/6312 firmware list */
	{ FW_INFO_ADDR_NOMORE, },

	/* 2 - Start of 232x/6322 firmware list */
	{
		FW_INFO_ADDR_NORMAL, &fw2322ipx_code01[0],
		&fw2322ipx_length01, &fw2322ipx_addr01,
	},
	{
		FW_INFO_ADDR_EXTENDED, &rseqipx_code01[0],
		&rseqipx_code_length01, 0, &rseqipx_code_addr01,
	},
	{
		FW_INFO_ADDR_EXTENDED, &xseqipx_code01[0],
		&xseqipx_code_length01, 0, &xseqipx_code_addr01,
	},
	/* End of 232x/6322 firmware list */
	{ FW_INFO_ADDR_NOMORE, },

 	/* 8 - Start of 24xx/54xx firmware list */
 	{
 		FW_INFO_ADDR_EXTENDED, (unsigned short *)&fw2400_code01[0],
                 (unsigned short *)&fw2400_length01, 0,   (unsigned long *)&fw2400_addr01,
 	},
 	{
 		FW_INFO_ADDR_EXTENDED, (unsigned short *)&fw2400_code02[0],
                 (unsigned short *)&fw2400_length02, 0, (unsigned long *)&fw2400_addr02,
 	},
 	{ FW_INFO_ADDR_NOMORE, },
	/* End of 24xx/54xx firmware list */
	/* End of firmware list */
#endif
};

static struct _qlaboards   QLBoardTbl_fc[NUM_OF_ISP_DEVICES] =
{
	/* Name ,  Board PCI Device ID,         Number of ports */
#if defined(ISP2300)
	{"QLA2322 ", QLA2322_DEVICE_ID,           MAX_BUSES,
		&fw2322ipx_version_str[0] , &qla_fw_tbl[ISP232X_FW_INDEX]
	},

	{"QLA2312 ", QLA2312_DEVICE_ID,           MAX_BUSES,
		&fw2300ipx_version_str[0] , &qla_fw_tbl[ISP23XX_FW_INDEX]
	},
	{"QLA2300 ", QLA2300_DEVICE_ID,           MAX_BUSES,
		&fw2300ipx_version_str[0] , &qla_fw_tbl[ISP23XX_FW_INDEX]
	},

	{"QLA6312 ", QLA6312_DEVICE_ID,           MAX_BUSES,
		&fw2300ipx_version_str[0] , &qla_fw_tbl[ISP23XX_FW_INDEX]
	},

	{"QLA6322 ", QLA6322_DEVICE_ID,           MAX_BUSES,
		&fw2322ipx_version_str[0] , &qla_fw_tbl[ISP232X_FW_INDEX]
	},

	{"QLA2422 ", QLA2422_DEVICE_ID,           MAX_BUSES,
		(char *)&fw2400_version_str[0] , &qla_fw_tbl[ISP24XX_FW_INDEX]
	},

	{"QLA2432 ", QLA2432_DEVICE_ID,           MAX_BUSES,
		(char *)&fw2400_version_str[0] , &qla_fw_tbl[ISP24XX_FW_INDEX]
	},
	{"QLA5422 ", QLA5422_DEVICE_ID,           MAX_BUSES,
		(char *)&fw2400_version_str[0] , &qla_fw_tbl[ISP24XX_FW_INDEX]
	},

	{"QLA5432 ", QLA5432_DEVICE_ID,           MAX_BUSES,
		(char *)&fw2400_version_str[0] , &qla_fw_tbl[ISP24XX_FW_INDEX]
	},
#endif

#if defined(ISP2200)
	{"QLA2200 ", QLA2200_DEVICE_ID,           MAX_BUSES,
		&fw2200tp_version_str[0] , &qla_fw_tbl[ISP22XX_FW_INDEX]
	},
#endif

#if defined(ISP2100)
	{"QLA2100 ", QLA2100_DEVICE_ID,           MAX_BUSES,
		&fw2100tp_version_str[0] , &qla_fw_tbl[ISP21XX_FW_INDEX]
	},
#endif

	{"        ",                 0,           0}
};



