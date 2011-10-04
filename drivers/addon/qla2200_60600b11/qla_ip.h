/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 IP network driver for Linux 2.4.x
 * Copyright (C) 2003 Qlogic Corporation
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

/****************************************************************************
              Please see revision.notes for revision history.
*****************************************************************************/

#if !defined(_QLA_IP_H_)
#define _QLA_IP_H_

#define MAX_SEND_PACKETS		32	/* Maximum # send packets */
#define MAX_RECEIVE_BUFFERS		64	/* Maximum # receive buffers */
#define MIN_RECEIVE_BUFFERS		8	/* Minimum # receive buffers */
#define IP_BUFFER_QUEUE_DEPTH		(MAX_RECEIVE_BUFFERS+1)

/* Async notification types */
#define NOTIFY_EVENT_LINK_DOWN		1	/* Link went down */
#define NOTIFY_EVENT_LINK_UP		2	/* Link is back up */
#define NOTIFY_EVENT_RESET_DETECTED	3	/* Reset detected */

/* QLogic subroutine status definitions */
#define QL_STATUS_SUCCESS		0
#define QL_STATUS_ERROR			1
#define QL_STATUS_FATAL_ERROR		2
#define QL_STATUS_RESOURCE_ERROR	3
#define QL_STATUS_LOOP_ID_IN_USE	4
#define QL_STATUS_NO_DATA		5

/************************************************************************/
/* RFC 2625 -- networking structure definitions                         */
/************************************************************************/

/* Network header definitions */
struct network_address
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u16	unused:12,
		naa:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u16	naa:4,
		unused:12;
#else
#error  "Please fix <asm/byteorder.h>"
#endif

#define NAA_IEEE_MAC_TYPE	1		/* NAA code - IEEE MAC addr */

	__u8	addr[6];
};

struct network_header
{
	union
	{
		struct network_address	na;
		__u8	fcaddr[8];
	} d;

	union
	{
		struct network_address	na;
		__u8	fcaddr[8];
	} s;
};

/* SNAP header definitions */
/* from linux/if_fc.h */
struct snap_header
{
	__u8  dsap;				/* destination SAP */
	__u8  ssap;				/* source SAP */
#define LLC_SAP_IEEE_802DOT2	0xAA		/* LLC SAP code - IEEE 802.2 */

	__u8  llc;				/* LLC control field */
#define LLC_CONTROL		0x03		/* LLC control code */

	__u8  protid[3];			/* protocol id */
#define SNAP_OUI		0x00		/* SNAP OUI code */

	__u16 ethertype;			/* ether type field */
};

/* Packet header definitions */
struct packet_header
{
	struct network_header	networkh;
	struct snap_header	snaph;
};

/* ARP header definitions */
/* from linux/if_arp.h */
struct arp_header
{
	struct arphdr	arph;
	__u8		ar_sha[ETH_ALEN];	/* sender hardware address */
	__u32		ar_sip;			/* sender IP address */
        __u8		ar_tha[ETH_ALEN];	/* target hardware address */
	__u32		ar_tip;			/* target IP address */
};

/* IP header definitions */
struct ip_header
{
	struct iphdr	iph;
	__u32		options;		/* IP packet options */
};


/************************************************************************/
/* Support structures.                                                  */
/************************************************************************/

/* Definitions for IP support */
#define LOOP_ID_MASK		0x00FF
#define PLE_NOT_SCSI_DEVICE	0x8000		/* Upper bit of loop ID set */
						/*  if not SCSI */

/* Receive buffer control block definitions */
struct buffer_cb
{
	uint16_t	handle;			/* ISP buffer handle */
	uint16_t	comp_status;		/* completion status from FW */

	uint32_t	state;			/* Buffer CB state */
#define BCB_RISC_OWNS_BUFFER	1

	struct sk_buff	*skb;			/* Socket buffer */
	uint8_t		*skb_data;		/* Socket buffer data */
	dma_addr_t	skb_data_dma;		/* SKB data physical address */
	uint32_t	rec_data_size;		/* Size of received data */
	uint32_t	packet_size;		/* Size of packet received */

	uint16_t	linked_bcb_cnt;		/* # of linked CBs for packet */
	uint16_t	unused2;
	struct buffer_cb *next_bcb;		/* Next buffer CB */
};

/* Send control block definitions */
struct send_cb
{
	uint16_t	comp_status;		/* completion status from FW */
#define SCB_CS_COMPLETE		0x0		
#define SCB_CS_INCOMPLETE	0x1
#define SCB_CS_RESET		0x4
#define SCB_CS_ABORTED		0x5
#define SCB_CS_TIMEOUT		0x6
#define SCB_CS_PORT_UNAVAILABLE	0x28
#define SCB_CS_PORT_LOGGED_OUT	0x29
#define SCB_CS_PORT_CONFIG_CHG	0x2A
#define SCB_CS_FW_RESOURCE_UNAVAILABLE	0x2C

	uint16_t	unused1;

	void		*qdev;			/* netdev private structure */

	struct packet_header *header;		/* Network and SNAP headers */
	dma_addr_t	header_dma;		/* Header physical address */

	struct sk_buff	*skb;			/* socket buffer to send */
	dma_addr_t	skb_data_dma;		/* skb data physical address */
};

/* IP device data structure */
#define QLLAN_MAX_IP_DEVICES	64	/* Maximum number of IP devices */

struct ip_device
{
	uint16_t	index;
	uint16_t	unused1;

	uint32_t	flags;
#define IP_DEV_FLAG_PRESENT		0x00000001 
#define IP_DEV_FLAG_PUBLIC_DEVICE	0x00000002

	uint16_t	loop_id;
#define PUBLIC_LOOP_DEVICE		((uint16_t)-1)

	uint16_t	unused2;
	uint8_t		port_id[3];
	uint8_t		unused3;
	uint8_t		port_name[8]; 

	struct ip_device *next;
	struct ip_device *last;
};

/************************************************************************/
/* Definitions for Backdoor Inquiry.                                   */
/************************************************************************/

struct bd_inquiry
{
	uint16_t	length;			/* Length of structure */
#define BDI_LENGTH		sizeof(struct bd_inquiry)

	uint16_t	version;		/* Structure version number */
/* NOTE: Update this value anytime the structure changes */
#define BDI_VERSION		2

						/* Exports */
	uint32_t	options;		/*  supported options */
#define BDI_IP_SUPPORT		1		/*   IP supported */
#define BDI_64BIT_ADDRESSING	2		/*   64bit address supported */

	void		*ha;			/*  Driver ha pointer */
	void		*risc_rec_q;		/*  RISC receive queue */
	uint16_t	risc_rec_q_size;	/*   size */

	uint16_t	link_speed;		/* Current link speed */
#define BDI_1GBIT_PORTSPEED	1		/*   operating at 1GBIT */
#define BDI_2GBIT_PORTSPEED	2		/*   operating at 2GBIT */
#define BDI_10GBIT_PORTSPEED	4		/*   operating at 10GBIT */

	uint8_t		port_name[8];		/*  Adapter port name */

	struct pci_dev	*pdev;			/* PCI device information */

	/* Pointers to SCSI-backdoor callbacks */
	void            *ip_enable_routine;
	void            *ip_disable_routine;
	void            *ip_add_buffers_routine;
	void            *ip_send_packet_routine;
	void            *ip_tx_timeout_routine;

	uint32_t        unused2[9];
};

/************************************************************************/
/* Definitions for Backdoor Enable.                                    */
/************************************************************************/

struct bd_enable
{
	uint16_t	length;			/* Length of structure */
#define BDE_LENGTH		sizeof(struct bd_enable)

	uint16_t	version;		/* Structure version number */
/* NOTE: Update this value anytime the structure changes */
#define BDE_VERSION		2

						/* Imports */
	uint32_t	options;		/*  supported options */
#define BDE_NOTIFY_ROUTINE	1		/*  notify routine imported */

	uint32_t	mtu;			/*  maximum transfer size */
	uint16_t	header_size;		/*  split header size */
	uint16_t	unused1;

	void		*receive_buffers;	/*  receive buffers array */
	uint16_t	max_receive_buffers;	/*  max # receive buffers */
	uint16_t	unused2;
	uint32_t	receive_buff_data_size;	/*  buffer size */

	/* Pointers to IP-backdoor callbacks */
	void		*notify_routine;
	void		*notify_context;
	void		*send_completion_routine;
	void		*receive_packets_routine;
	void		*receive_packets_context;

	uint32_t        unused3[9];
};


/************************************************************************/
/* RISC interface structures                                            */
/************************************************************************/

/* IP mailbox commands */
#define MBC_INITIALIZE_IP               0x0077
#define MBC_DISABLE_IP                  0x0079

/* IP async events */
#define MBA_IP_TRANSMIT_COMPLETE        0x8022
#define MBA_IP_RECEIVE_COMPLETE         0x8023
#define MBA_IP_BROADCAST_RECEIVED       0x8024
#define MBA_IP_RECEIVE_BUFFERS_LOW      0x8025
#define MBA_IP_OUT_OF_BUFFERS           0x8026
#define MBA_IP_RECEIVE_COMPLETE_SPLIT   0x8027

/* IP fast post completions for 2300 */
#define RHS_IP_SEND_COMPLETE            0x18
#define RHS_IP_RECV_COMPLETE            0x19
#define RHS_IP_RECV_DA_COMPLETE         0x1B

/* RISC IP receive buffer queue entry structure */
struct risc_rec_entry
{
	uint32_t        data_addr_low;
	uint32_t        data_addr_high;
	uint16_t        handle;
	uint16_t        unused;
};

/* Firmware IP initialization control block definitions */
struct ip_init_cb
{
	uint8_t		version;
#define IPICB_VERSION				0x01

	uint8_t		reserved_1;

	uint16_t	firmware_options;
#define IPICB_OPTION_64BIT_ADDRESSING		0x0001
#define IPICB_OPTION_NO_BROADCAST_FASTPOST	0x0002
#define IPICB_OPTION_OUT_OF_BUFFERS_EVENT	0x0004

	uint16_t	header_size;
	uint16_t	mtu;
	uint16_t	receive_buffer_size;
	uint16_t	reserved_2;
	uint16_t	reserved_3;
	uint16_t	reserved_4;
	uint16_t	reserved_5;

	uint16_t	receive_queue_size;
	uint16_t	low_water_mark;
#define IPICB_LOW_WATER_MARK			0

	uint16_t	receive_queue_addr[4];
	uint16_t	receive_queue_in;
	uint16_t	fast_post_count;
#define IPICB_FAST_POST_COUNT			4

	uint16_t	container_count;
#define IPICB_BUFFER_CONTAINER_COUNT		64

	uint16_t	resource_allocation;
#define IPICB_IOCB_RESERVE_COUNT		250
};


/* IP IOCB types */

/* Data segment descriptor structures */
struct data_seg
{
	uint32_t	address;
	uint32_t	length;
};

struct data_seg_a64
{
	uint32_t	address[2];
	uint32_t	length;
};

/* IP Command IOCB structure */
struct ip_cmd_entry
{
	uint8_t		entry_type;
#define ET_IP_COMMAND_32		0x13
#define ET_IP_COMMAND_64		0x1B

	uint8_t		entry_count;
	uint8_t		sys_define;
	uint8_t		entry_status;

	uint32_t	handle;
	uint8_t		reserved_1;
	uint8_t		loop_id;
	uint16_t	comp_status;
	uint16_t	control_flags;
	uint16_t	reserved_2;
	uint16_t	timeout;
	uint16_t	data_seg_count;
	uint16_t	service_class;
	uint16_t	reserved_3[7];
	uint32_t	byte_count;
	union
	{
		struct data_seg		data_segs[3];
		struct data_seg_a64	data_segs64[2];
	} ds;
};

/* IP Receive IOCB structure */
#define ET_IP_RECEIVE_CONTINUATION  0x2B

struct ip_rec_entry
{
	uint8_t		entry_type;
#define ET_IP_RECEIVE			0x23

	uint8_t		entry_count;
	uint8_t		segment_count;
	uint8_t		entry_status;

	uint16_t	s_idlow;
	uint8_t		s_idhigh;
	uint8_t		reserved_1;
	uint8_t		reserved_2;
	uint8_t		loop_id;
	uint16_t	comp_status;
#define IPREC_STATUS_SPLIT_BUFFER	0x0001

	uint16_t	service_class;
	uint16_t	sequence_length;

#define IPREC_MAX_HANDLES		24
	uint16_t	buffer_handles[IPREC_MAX_HANDLES];
};

/* Mailbox command IOCB structure */
struct mbx_entry
{
	uint8_t		entry_type;
#define ET_MAILBOX_COMMAND		0x39

	uint8_t		entry_count;
	uint8_t		sys_define1;
/* Use sys_define1 for source type */
#define SOURCE_SCSI			0x00
#define SOURCE_IP			0x01
#define SOURCE_VI			0x02
#define SOURCE_SCTP			0x03
#define SOURCE_MP			0x04
#define SOURCE_MPIOCTL			0x05

	uint8_t		entry_status;

	uint32_t	handle;
	uint8_t		reserved_1;
	uint8_t		loop_id;

	uint16_t	status;
	uint16_t	state_flags;
	uint16_t	status_flags;
	uint16_t	sys_define2[4];
	uint16_t	mb0;
/* Mailbox command completion status */
#define MBS_PORT_ID_IN_USE		0x4007
#define MBS_LOOP_ID_IN_USE		0x4008
#define MBS_ALL_LOOP_IDS_IN_USE		0x4009
#define MBS_NAME_SERVER_NOT_LOGGED_IN	0x400A

	uint16_t	mb1;
/* Fabric login mailbox command option bits */
#define MBC_NO_PLOGI_IF_LOGGED_IN	0x01
#define MBC_NO_PROCESS_LOGIN		0x02

	uint16_t	mb2;
	uint16_t	mb3;
	uint16_t	mb6;
	uint16_t	mb7;
	uint32_t	reserved_2[7];
};
#endif /* _QLA_IP_H_ */
