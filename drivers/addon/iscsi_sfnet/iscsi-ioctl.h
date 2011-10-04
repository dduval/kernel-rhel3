#ifndef ISCSI_IOCTL_H_
#define ISCSI_IOCTL_H_

/*
 * iSCSI connection daemon
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi-devel@lists.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi-ioctl.h,v 1.14.2.2 2004/09/21 09:00:29 krishmnc Exp $
 *
 * include for ioctl calls between the daemon and the kernel module
 *
 */

#include "iscsi-protocol.h"
#include "iscsiAuthClient.h"
#include "iscsi-portal.h"

/*
 * ioctls
 */
#define ISCSI_ESTABLISH_SESSION 0x00470301
#define ISCSI_TERMINATE_SESSION 0x00470302
#define ISCSI_SHUTDOWN          0x00470303
#define ISCSI_GETTRACE	        0x00470304
#define ISCSI_PROBE_LUNS        0x00470305
#define ISCSI_RESET_PROBING     0x00470306
#define ISCSI_DEVICE_INFO       0x00470307
#define ISCSI_LS_TARGET_INFO    0x00470308
#define ISCSI_LS_PORTAL_INFO    0x00470309
#define ISCSI_SET_INBP_INFO     0x0047030a
#define ISCSI_CHECK_INBP_BOOT   0x0047030b

#define INBP_BUF_SIZE 1024
#define SCANAREA 1024
#define SIGNATURE "Cisco PiXiE Dust"
#define DADDLEN 6

typedef struct sapiNBP {
    uint8_t signature[20];	/* "Cisco PiXiE Dust" */
    uint32_t targetipaddr;	// iSCSI target IPv4 address
    uint32_t myipmask;		// lan netmask
    uint32_t ripaddr;		// gateway IPv4 address
    uint8_t tgtethaddr[DADDLEN];	// target ethernet address
    uint8_t structVersion;	// version number of this struct
    uint8_t pad1;		// pad for windows driver
    uint16_t tcpport;		// tcp port to use
    uint16_t slun;		// boot disk lun
    uint8_t targetstring[256];	// boot disk target
    uint32_t ntbootdd_routine;
    uint32_t myipaddr;		// Our IPv4 address
    uint8_t myethaddr[DADDLEN];	// Our ethernet address
    uint8_t pad2[2];		// pad for windows driver
    uint8_t bootPartitionNumber;	// boot partition number of c:
    uint8_t numberLocalDisks;
    uint8_t nbpVersion[32];	// NBP version string
    uint8_t root_disk;
} sapiNBP_t;

typedef struct iscsi_nic_info {
    int ip_length;
    char ip_address[16];
} iscsi_nic_info_t;

typedef struct iscsi_session_ioctl {
    uint32_t ioctl_size;
    uint32_t ioctl_version;
    uint32_t config_number;
    int probe_luns;
    int update;
    uint8_t isid[6];
    int iscsi_bus;
    int target_id;
    int probe_order;
    int password_length;
    char username[iscsiAuthStringMaxLength];
    unsigned char password[iscsiAuthStringMaxLength];
    int password_length_in;
    char username_in[iscsiAuthStringMaxLength];
    unsigned char password_in[iscsiAuthStringMaxLength];
    unsigned char TargetName[TARGET_NAME_MAXLEN + 1];
    unsigned char InitiatorName[TARGET_NAME_MAXLEN + 1];
    unsigned char InitiatorAlias[TARGET_NAME_MAXLEN + 1];
    uint32_t lun_bitmap[8];
    int host_number;		/* returned from the kernel */
    int channel;		/* returned from the kernel */
    unsigned char TargetAlias[TARGET_NAME_MAXLEN + 1];	/* returned from the 
							 * kernel 
							 */
    int portal_failover;
    unsigned char preferred_portal[16];
    unsigned char preferred_subnet[16];
    uint32_t preferred_subnet_mask;
    iscsi_nic_info_t nic_info;	/* nic to be used for this session */
    uint32_t num_portals;
    uint32_t portal_info_size;
    iscsi_portal_info_t portals[1];	/* 1 or more portals for this session 
					 * to use 
					 */
} iscsi_session_ioctl_t;

#define ISCSI_SESSION_IOCTL_VERSION 21

typedef struct iscsi_terminate_session_ioctl {
    uint32_t ioctl_size;
    uint32_t ioctl_version;
    int iscsi_bus;
    int target_id;
} iscsi_terminate_session_ioctl_t;

#define ISCSI_TERMINATE_SESSION_IOCTL_VERSION 1

typedef struct iscsi_probe_luns_ioctl {
    uint32_t ioctl_size;
    uint32_t ioctl_version;
    int iscsi_bus;
    int target_id;
    uint32_t lun_bitmap[8];
} iscsi_probe_luns_ioctl_t;

#define ISCSI_PROBE_LUNS_IOCTL_VERSION 1

/* request info for a particular session by host,channel,target */
typedef struct iscsi_get_session_info_ioctl {
    uint32_t ioctl_size;
    uint32_t ioctl_version;
    int host;
    int channel;
    int target_id;
    char TargetName[TARGET_NAME_MAXLEN + 1];
    char TargetAlias[TARGET_NAME_MAXLEN + 1];
    uint8_t isid[6];
    uint32_t lun_bitmap[8];
    uint8_t ip_address[16];	/* current address, may not
				 * match any portal after a
				 * temp redirect 
				 */
    int ip_length;		/* current address, may not
				 * match any portal after a
				 * temp redirect 
				 */
    uint32_t num_portals;
    uint32_t portal_info_size;
    iscsi_portal_info_t portals[1];	/* 1 or more portals
					 * this session can
					 * use 
					 */
} iscsi_get_session_info_ioctl_t;

#define ISCSI_GET_SESSION_INFO_IOCTL_VERSION 1

typedef struct iscsi_portal_list {
    int bus_id;
    int target_id;
    iscsi_portal_info_t *portals;
} portal_list_t;

typedef struct iscsi_ls_session {
    int conn_status;
    uint8_t isid[6];
    uint16_t tsih;
    int InitialR2T;
    int ImmediateData;
    int HeaderDigest;
    int DataDigest;
    int FirstBurstLength;
    int MaxBurstLength;
    int MaxRecvDataSegmentLength;
    int MaxXmitDataSegmentLength;
    int login_timeout;
    int auth_timeout;
    int active_timeout;
    int idle_timeout;
    int ping_timeout;
    time_t establishment_time;
    time_t session_drop_time;
    int ever_established;
    int session_alive;
    int addr[4];		/* peer address */
    int port;			/* peer port number */
} iscsi_ls_session_t;

typedef struct target_info {
    unsigned char target_name[TARGET_NAME_MAXLEN + 1];
    unsigned char target_alias[TARGET_NAME_MAXLEN + 1];
    int host_no;
    int channel;
    int target_id;
    int num_portals;
    iscsi_ls_session_t session_data;
} target_info_t;

#endif
