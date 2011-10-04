/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */


#ifndef _QLA_GS_H
#define	_QLA_GS_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * FC-GS-4 definitions.
 */
#define	GS4_REVISION		0x01
#define	GS_TYPE_MGMT_SERVER	0xFA
#define	GS_TYPE_DIR_SERVER	0xFC
#define GS_SUBTYPE_FDMI_HBA	0x10

/* FDMI Command Codes. */
#define	FDMI_CC_GRHL	0x100
#define	FDMI_CC_GHAT	0x101
#define	FDMI_CC_GRPL	0x102
#define	FDMI_CC_GPAT	0x110
#define	FDMI_CC_RHBA	0x200
#define	FDMI_CC_RHAT	0x201
#define	FDMI_CC_RPRT	0x210
#define	FDMI_CC_RPA	0x211
#define	FDMI_CC_DHBA	0x300
#define	FDMI_CC_DHAT	0x301
#define	FDMI_CC_DPRT	0x310
#define	FDMI_CC_DPA	0x311

/*
 * CT information unit basic preamble.
 */
typedef struct {
	uint8_t		revision;
	uint8_t		in_id[3];
	uint8_t		gs_type;
	uint8_t		gs_subtype;
	uint8_t		options;
	uint8_t		reserved;
	uint16_t	cmd_rsp_code;
	uint16_t	max_resid_size;
	uint8_t		fragment_id;
	uint8_t		reason;
	uint8_t		explanation;
	uint8_t		vendor_unique;
} ct_iu_preamble_t;

#define FDMI_STAT_OK			0
#define FDMI_STAT_ERR			1
#define FDMI_STAT_ALREADY_REGISTERED	2

#define FDMI_REASON_INVALID_CMD		0x01
#define FDMI_REASON_INVALID_VERSION	0x02
#define FDMI_REASON_LOGICAL_ERR		0x03
#define FDMI_REASON_INVALID_CTIU_SIZE	0x04
#define FDMI_REASON_LOGICAL_BUSY	0x05
#define FDMI_REASON_PROTOCOL_ERR	0x07
#define FDMI_REASON_CANNOT_PERFORM	0x09
#define FDMI_REASON_NOT_SUPPORTED	0x0B
#define FDMI_REASON_HARD_ENF_FAILED	0x0C

#define FDMI_EXPL_NO_ADDITIONAL_EXPLANATION	0x00
#define FDMI_EXPL_HBA_ALREADY_REGISTERED	0x10
#define FDMI_EXPL_HBA_ATTR_NOT_REGISTERED	0x11
#define FDMI_EXPL_HBA_ATTR_MULTI_SAME_TYPE	0x12
#define FDMI_EXPL_INVALID_HBA_ATTR_LEN		0x13
#define FDMI_EXPL_HBA_ATTR_NOT_PRESENT		0x14
#define FDMI_EXPL_PORT_NOT_IN_PORT_LIST		0x15
#define FDMI_EXPL_HBA_ID_NOT_IN_PORT_LIST	0x16
#define FDMI_EXPL_PORT_ATTR_NOT_REGISTERED	0x20
#define FDMI_EXPL_PORT_NOT_REGISTERED		0x21
#define FDMI_EXPL_PORT_ATTR_MULTI_SAME_TYPE	0x22
#define FDMI_EXPL_INVALID_PORT_ATTR_LEN		0x23

/*
 * HBA attribute types.
 */
#define	T_NODE_NAME			1
#define	T_MANUFACTURER			2
#define	T_SERIAL_NUMBER			3
#define	T_MODEL				4
#define	T_MODEL_DESCRIPTION		5
#define	T_HARDWARE_VERSION		6
#define	T_DRIVER_VERSION		7
#define	T_OPTION_ROM_VERSION		8
#define	T_FIRMWARE_VERSION		9
#define	T_OS_NAME_AND_VERSION		0xa
#define	T_MAXIMUM_CT_PAYLOAD_LENGTH	0xb

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[WWN_SIZE];
} hba_nn_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[20];
} hba_man_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[8];
} hba_sn_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[16];
} hba_mod_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[80];
} hba_mod_desc_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[16];
} hba_hv_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[28];
} hba_dv_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[16];
} hba_or_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[16];
} hba_fw_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[16];
} hba_os_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[4];
} hba_maxctlen_attr_t;

/*
 * HBA Attribute Block.
 */
#define	HBA_ATTR_COUNT		10
typedef struct {
	uint32_t		count;
	hba_nn_attr_t		nn;
	hba_man_attr_t		man;
	hba_sn_attr_t		sn;
	hba_mod_attr_t		mod;
	hba_mod_desc_attr_t	mod_desc;
	hba_hv_attr_t		hv;
	hba_dv_attr_t		dv;
	hba_or_attr_t		or;
	hba_fw_attr_t		fw;
	hba_os_attr_t		os;
#if 0
	hba_maxctlen_attr_t	max_ctlen;
#endif
} hba_attr_t;

/*
 * Port attribute types.
 */
#define	T_FC4_TYPES			1
#define	T_SUPPORT_SPEED			2
#define	T_CURRENT_SPEED			3
#define	T_MAX_FRAME_SIZE		4
#define	T_OS_DEVICE_NAME		5
#define	T_HOST_NAME			6

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[32];
} port_fc4_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint32_t		value;
} port_speed_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint32_t		value;
} port_frame_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[24];
} port_os_attr_t;

typedef struct {
	uint16_t		type;
	uint16_t		len;
	uint8_t			value[80];
} port_host_name_attr_t;

/*
 * Port Attribute Block.
 */
#define	PORT_ATTR_COUNT		6
typedef struct {
	uint32_t		count;
	port_fc4_attr_t		fc4_types;
	port_speed_attr_t	sup_speed;
	port_speed_attr_t	cur_speed;
	port_frame_attr_t	max_fsize;
	port_os_attr_t		os_dev_name;
	port_host_name_attr_t	host_name;
} port_attr_t;

/*
 * Registered Port List
 */
typedef struct {
	uint32_t		num_ports;
	uint8_t			port_entry[WWN_SIZE];
} reg_port_list_t;

/*
 * Get HBA Attributes.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_identifier[WWN_SIZE];
} ct_iu_ghat_req_t;

typedef struct {
	ct_iu_preamble_t	hdr;
	reg_port_list_t		plist;
	hba_attr_t		attr;
} ct_iu_ghat_rsp_t;

/*
 * Register HBA.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_identifier[WWN_SIZE];
	reg_port_list_t		plist;
	hba_attr_t		attr;
} ct_iu_rhba_t;

/*
 * Register HBA Attributes.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_identifier[WWN_SIZE];
	hba_attr_t		attr;
} ct_iu_rhat_t;

/*
 * Register Port.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_portname[WWN_SIZE];
	uint8_t			portname[WWN_SIZE];
	port_attr_t		attr;
} ct_iu_rprt_t;

/*
 * Register Port Attributes.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			portname[WWN_SIZE];
	port_attr_t		attr;
} ct_iu_rpa_t;

/*
 * Deregister HBA.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_portname[WWN_SIZE];
} ct_iu_dhba_t;

/*
 * Deregister HBA Attributes.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			hba_portname[WWN_SIZE];
} ct_iu_dhat_t;

/*
 * Deregister Port.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			portname[WWN_SIZE];
} ct_iu_dprt_t;

/*
 * Deregister Port Attributes.
 */
typedef struct {
	ct_iu_preamble_t	hdr;
	uint8_t			portname[WWN_SIZE];
} ct_iu_dpa_t;

typedef struct {
	uint16_t		loop_id;
	uint32_t		response_byte_count;
	uint32_t		command_byte_count;
} ct_iocb_t;


typedef struct ct_fdmi_pkt {
	union {
		ct_iu_ghat_req_t	ghat_req;
		ct_iu_ghat_rsp_t	ghat_rsp;
		ct_iu_rhba_t		rhba;
		ct_iu_rhat_t		rhat;
		ct_iu_rprt_t		rprt;
		ct_iu_rpa_t		rpa;
		ct_iu_dhba_t		dhba;
		ct_iu_dhat_t		dhat;
		ct_iu_dprt_t		dprt;
		ct_iu_dpa_t		dpa;
	} t;
} ct_fdmi_pkt_t;

#ifdef __cplusplus
}
#endif

#endif /* _QLA_GS_H */
