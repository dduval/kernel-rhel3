#define QLA_MODEL_NAMES         0x19

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
	" "		/* 0x118 */
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
	" "						/* 0x118 */
};

struct cfg_device_info {
	const char *vendor;
	const char *model;
	const int  flags;	/* bit 0 (0x1) -- This bit will translate the real 
				   WWNN to the common WWNN for the target AND
				   XP_DEVICE */
				/* bit 1 (0x2) -- MSA 1000  */
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
	{"HITACHI", "OPEN-3", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
	{"SEAGATE", "ST318453FC", 0, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },
*/

	{"HP", "OPEN-", 1, FO_NOTIFY_TYPE_NONE,   
		qla2x00_combine_by_lunid, NULL, NULL, NULL },

	/*
	 * Must be at end of list...
	 */
	{NULL, NULL }
};


