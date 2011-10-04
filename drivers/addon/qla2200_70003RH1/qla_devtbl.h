#define QLA_MODEL_NAMES         0x1B

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
	"HPQSVS ",	/* 0x10f */
	"HPQSVS ",	/* 0x110 */
	"QLA4010",	/* 0x111 */
	"QLA4010",	/* 0x112 */
	"QLA4010C",	/* 0x113 */
	"QLA4010C",	/* 0x114 */
	"QLA2360",	/* 0x115 */
	"QLA2362",	/* 0x116 */
	" ",		/* 0x117 */
	" ",		/* 0x118 */
	"QLA200",	/* 0x119 */
	"QLA200C"	/* 0x11A */
};

char	*qla2x00_model_desc[QLA_MODEL_NAMES] = {
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
	"Optical- 133MHz to 1Gb iSCSI- networking",	/* 0x111 */
	"Optical- 133MHz to 1Gb iSCSI- storage",	/* 0x112 */
	"Copper- 133MHz to 1Gb iSCSI- networking",	/* 0x113 */
	"Copper- 133MHz to 1Gb iSCSI- storage",		/* 0x114 */
	"133MHz PCI-X to 2Gb FC Single Channel",	/* 0x115 */
	"133MHz PCI-X to 2Gb FC Dual Channel",		/* 0x116 */
	" ",						/* 0x117 */
	" ",						/* 0x118 */
	"133MHz PCI-X to 2Gb FC Optical",		/* 0x119 */
	"133MHz PCI-X to 2Gb FC Copper"			/* 0x11A */
};


struct cfg_device_info {
	const char *vendor;
	const char *model;
	const int  flags;	/* bit 0 (0x1) -- This bit will translate the real 
				   WWNN to the common WWNN for the target AND
				   XP_DEVICE */
				/* bit 1 (0x2) -- MSA 1000  */
				/* bit 2 (0x4) -- EVA  */
				/* bit 3 (0x8) -- DISABLE FAILOVER  */
	const int  notify_type;	/* support the different types: 1 - 4 */
	int	( *fo_combine)(void *,
		 uint16_t, fc_port_t *, uint16_t );
	int	( *fo_detect)(void);
	int	( *fo_notify)(void);
	int	( *fo_select)(void);
};


static struct cfg_device_info cfg_device_list[] = {

	{"COMPAQ", "MSA1000", 2, FO_NOTIFY_TYPE_SPINUP, 
		qla2x00_combine_by_lunid, NULL, NULL, NULL },

/* For testing only
	{"SEAGATE", "ST318453FC", 0, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
*/

	{"HITACHI", "OPEN-", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "OPEN-", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"COMPAQ", "HSV110 (C)COMPAQ", 4, FO_NOTIFY_TYPE_SPINUP,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"HP", "HSV100", 4, FO_NOTIFY_TYPE_SPINUP,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"DEC", "HSG80", 8, FO_NOTIFY_TYPE_NONE,   
		qla2x00_export_target, NULL, NULL, NULL },

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
#define NUM_OF_ISP_DEVICES  6
static struct pci_device_id qla2300_pci_tbl[] =
{
	{QLA2X00_VENDOR_ID, QLA2300_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2312_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA2322_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA6312_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{QLA2X00_VENDOR_ID, QLA6322_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
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
#define ISP63XX_FW_INDEX	6
#define ISP632X_FW_INDEX	8

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
#if defined(FC_IP_SUPPORT)
	{
	   FW_INFO_ADDR_NORMAL, &fw2200ip_code01[0],
	   &fw2200ip_length01, &fw2200ip_addr01,
	},
#else
	{
	   FW_INFO_ADDR_NORMAL, &fw2200tp_code01[0],
	   &fw2200tp_length01, &fw2200tp_addr01,
	},
#endif
	{ FW_INFO_ADDR_NOMORE, },
#endif

#if defined(ISP2300)
	/* 0 - Start of 23xx firmware list */
	{
		FW_INFO_ADDR_NORMAL, &fw2300ipx_code01[0],
		&fw2300ipx_length01, &fw2300ipx_addr01, 
	},

	/* End of 23xx firmware list */
	{ FW_INFO_ADDR_NOMORE, },

	/* 2 - Start of 232x firmware list */
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
	{ FW_INFO_ADDR_NOMORE, },
	/* 6 - Start of 63xx firmware list */
	{
		FW_INFO_ADDR_NORMAL, &fw2300flx_code01[0],
		&fw2300flx_length01, &fw2300flx_addr01, 
	},
	{ FW_INFO_ADDR_NOMORE, },
	/* End of 63xx firmware list */

	/* 8 - Start of 632x firmware list */
	{
		FW_INFO_ADDR_NORMAL, &fw2322flx_code01[0],
		&fw2322flx_length01, &fw2322flx_addr01, 
	},
	{
		FW_INFO_ADDR_EXTENDED, &rseqflx_code01[0],
		&rseqflx_code_length01, 0, &rseqflx_code_addr01,
	},
	{
		FW_INFO_ADDR_EXTENDED, &xseqflx_code01[0],
		&xseqflx_code_length01, 0, &xseqflx_code_addr01,
	},
	{ FW_INFO_ADDR_NOMORE, },
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
		&fw2300flx_version_str[0] , &qla_fw_tbl[ISP63XX_FW_INDEX]
	},

	{"QLA6322 ", QLA6322_DEVICE_ID,           MAX_BUSES,
		&fw2322flx_version_str[0] , &qla_fw_tbl[ISP632X_FW_INDEX]
	},
#endif

#if defined(ISP2200)
	{"QLA2200 ", QLA2200_DEVICE_ID,           MAX_BUSES,
#if defined(FC_IP_SUPPORT)
		&fw2200ip_version_str[0] ,
	},
#else
		&fw2200tp_version_str[0] , &qla_fw_tbl[ISP22XX_FW_INDEX]
	},
#endif
#endif

#if defined(ISP2100)
	{"QLA2100 ", QLA2100_DEVICE_ID,           MAX_BUSES,
		&fw2100tp_version_str[0] , &qla_fw_tbl[ISP21XX_FW_INDEX]
	},
#endif

	{"        ",                 0,           0}
};



