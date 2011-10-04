#ifndef ISCSI_SESSION_H_
#define ISCSI_SESSION_H_

/*
 * iSCSI driver for Linux
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
 * $Id: iscsi-session.h,v 1.24.2.2 2004/09/21 09:00:29 krishmnc Exp $
 *
 * iscsi-session.h
 *
 *   define the iSCSI session structure needed by the login library
 * 
 */

#include "iscsi-common.h"
#include "iscsiAuthClient.h"

#if defined(__KERNEL__)

# include <linux/version.h>
# include <linux/blkdev.h>
# include <linux/sched.h>
# include <linux/uio.h>
# include <asm/current.h>
# include <asm/uaccess.h>
# include <linux/smp_lock.h>
# if ( LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0) )
#   include <asm/semaphore.h>
# else
#   include <asm/spinlock.h>
# endif
# include <scsi/sg.h>

/* these are from $(TOPDIR)/drivers/scsi, not $(TOPDIR)/include */
# include "scsi.h"
# include "hosts.h"

# include "iscsi-limits.h"
# include "iscsi-kernel.h"
# include "iscsi-task.h"
# include "iscsi-portal.h"

#define LUN_BITMAP_SIZE ((ISCSI_MAX_LUN + BITS_PER_LONG - 1) / (BITS_PER_LONG))

/* used for replying to NOPs */
typedef struct iscsi_nop_info {
    struct iscsi_nop_info *next;
    uint32_t ttt;
    unsigned int dlength;
    unsigned char lun[8];
    unsigned char data[1];
} iscsi_nop_info_t;

typedef struct iscsi_session {
    atomic_t refcount;
    int this_is_root_disk;
    volatile unsigned long generation;
    struct iscsi_session *next;
    struct iscsi_session *prev;
    struct iscsi_session *probe_next;
    struct iscsi_session *probe_prev;
    struct iscsi_hba *hba;
    struct socket *socket;
    int iscsi_bus;
    unsigned int host_no;
    unsigned int channel;
    unsigned int target_id;
    unsigned long luns_found[LUN_BITMAP_SIZE];
    unsigned long luns_detected[LUN_BITMAP_SIZE];
    unsigned long luns_activated[LUN_BITMAP_SIZE];
    unsigned long luns_unreachable[LUN_BITMAP_SIZE];
    unsigned long luns_checked[LUN_BITMAP_SIZE];
    unsigned long luns_delaying_commands[LUN_BITMAP_SIZE];
    unsigned long luns_timing_out[LUN_BITMAP_SIZE];
    unsigned long luns_needing_recovery[LUN_BITMAP_SIZE];
    unsigned long luns_delaying_recovery[LUN_BITMAP_SIZE];
    unsigned long luns_doing_recovery[LUN_BITMAP_SIZE];
    uint32_t luns_allowed[LUN_BITMAP_SIZE];
    uint32_t num_luns;
    int probe_order;
    int lun_being_probed;
    struct semaphore probe_sem;
    int ip_length;
    unsigned char ip_address[16];
    int port;
    int local_ip_address[16];
    int local_ip_length;
    int tcp_window_size;
    struct semaphore config_mutex;
    uint32_t config_number;
    char *username;
    unsigned char *password;
    int password_length;
    char *username_in;
    unsigned char *password_in;
    int password_length_in;
    unsigned char *InitiatorName;
    unsigned char *InitiatorAlias;
    unsigned char TargetName[TARGET_NAME_MAXLEN + 1];
    unsigned char TargetAlias[TARGET_NAME_MAXLEN + 1];
    unsigned char *log_name;
    mode_t dir_mode;
    int bidirectional_auth;
    IscsiAuthClient *auth_client_block;
    IscsiAuthStringBlock *auth_recv_string_block;
    IscsiAuthStringBlock *auth_send_string_block;
    IscsiAuthLargeBinary *auth_recv_binary_block;
    IscsiAuthLargeBinary *auth_send_binary_block;
    int num_auth_buffers;
    IscsiAuthBufferDesc auth_buffers[5];
    spinlock_t portal_lock;
    iscsi_portal_info_t *portals;
    unsigned int num_portals;
    int portal_failover;
    unsigned int current_portal;
    unsigned int requested_portal;
    unsigned int fallback_portal;
    unsigned char preferred_portal[16];
    unsigned char preferred_subnet[16];
    unsigned int preferred_subnet_mask;
    unsigned int preferred_portal_bitmap;
    unsigned int preferred_subnet_bitmap;
    unsigned int tried_portal_bitmap;
    unsigned int auth_failures;
    int ever_established;
    int session_alive;
    int commands_queued;
    int (*update_address) (struct iscsi_session * session, char *address);
    /* the queue of SCSI commands that we need to send on this session */
    spinlock_t scsi_cmnd_lock;
    Scsi_Cmnd *retry_cmnd_head;
    Scsi_Cmnd *retry_cmnd_tail;
    atomic_t num_retry_cmnds;
    Scsi_Cmnd *scsi_cmnd_head;
    Scsi_Cmnd *scsi_cmnd_tail;
    atomic_t num_cmnds;
    Scsi_Cmnd *deferred_cmnd_head;
    Scsi_Cmnd *deferred_cmnd_tail;
    unsigned int num_deferred_cmnds;
    int ignore_lun;
    unsigned int ignore_completions;
    unsigned int ignore_aborts;
    unsigned int ignore_abort_task_sets;
    unsigned int ignore_lun_resets;
    unsigned int ignore_warm_resets;
    unsigned int ignore_cold_resets;
    int reject_lun;
    unsigned int reject_aborts;
    unsigned int reject_abort_task_sets;
    unsigned int reject_lun_resets;
    unsigned int reject_warm_resets;
    unsigned int reject_cold_resets;
    unsigned int fake_read_header_mismatch;
    unsigned int fake_write_header_mismatch;
    unsigned int fake_read_data_mismatch;
    unsigned int fake_write_data_mismatch;
    unsigned int fake_not_ready;
    int fake_status_lun;
    unsigned int fake_status_unreachable;
    unsigned int fake_status_busy;
    unsigned int fake_status_queue_full;
    unsigned int fake_status_aborted;
    unsigned int print_cmnds;
    struct timer_list busy_task_timer;
    struct timer_list busy_command_timer;
    struct timer_list immediate_reject_timer;
    struct timer_list retry_timer;
    unsigned int num_luns_delaying_commands;
    uint8_t isid[6];
    uint16_t tsih;
    unsigned int CmdSn;
    volatile uint32_t ExpCmdSn;
    volatile uint32_t MaxCmdSn;
    volatile uint32_t last_peak_window_size;
    volatile uint32_t current_peak_window_size;
    unsigned long window_peak_check;
    int ImmediateData;
    int InitialR2T;
    int MaxRecvDataSegmentLength;	/* the value we declare */
    int MaxXmitDataSegmentLength;	/* the value declared by the target */
    int FirstBurstLength;
    int MaxBurstLength;
    int DataPDUInOrder;
    int DataSequenceInOrder;
    int DefaultTime2Wait;
    int DefaultTime2Retain;
    int HeaderDigest;
    int DataDigest;
    int type;
    int current_stage;
    int next_stage;
    int partial_response;
    int portal_group_tag;
    uint32_t itt;
    int ping_test_data_length;
    int ping_test_rx_length;
    unsigned long ping_test_start;
    unsigned long ping_test_rx_start;
    unsigned char *ping_test_tx_buffer;
    volatile unsigned long last_rx;
    volatile unsigned long last_ping;
    unsigned long last_window_check;
    unsigned long last_kill;
    unsigned long login_phase_timer;
    unsigned long window_full;
    unsigned long window_closed;
    int vendor_specific_keys;
    unsigned int irrelevant_keys_bitmap;
    int send_async_text;
    int login_timeout;
    int auth_timeout;
    int active_timeout;
    int idle_timeout;
    int ping_timeout;
    int abort_timeout;
    int reset_timeout;
    int replacement_timeout;
    unsigned int disk_command_timeout;
    /* the following fields may have to move if we decide to
     * implement multiple connections, per session, and
     * decide to have threads for each connection rather
     * than for each session.
     */
    /* the queue of SCSI commands that have been sent on
     * this session, and for which we're waiting for a
     * reply 
     */
    spinlock_t task_lock;
    iscsi_task_t *preallocated_task;
    iscsi_task_collection_t arrival_order;
    iscsi_task_collection_t tx_tasks;
    atomic_t num_active_tasks;
    unsigned int tasks_allocated;
    unsigned int tasks_freed;
    iscsi_nop_info_t nop_reply;
    iscsi_nop_info_t *nop_reply_head;
    iscsi_nop_info_t *nop_reply_tail;
    uint32_t mgmt_itt;
    wait_queue_head_t tx_wait_q;
    wait_queue_head_t tx_blocked_wait_q;
    wait_queue_head_t login_wait_q;
    volatile unsigned long control_bits;
    volatile uint32_t warm_reset_itt;
    volatile uint32_t cold_reset_itt;
    volatile pid_t rx_pid;
    volatile pid_t tx_pid;
    volatile unsigned long session_drop_time;
    volatile unsigned long session_established_time;
    /* the following fields are per-connection, not per
     * session, and will need to move if we decide to
     * support multiple connections per session.
     */
    unsigned long task_mgmt_response_deadline;
    unsigned long reset_response_deadline;
    unsigned long logout_deadline;
    unsigned long logout_response_deadline;
    uint32_t logout_itt;
    long time2wait;
    unsigned int ExpStatSn;
    struct iovec rx_iov[(ISCSI_MAX_SG + 1 + 1)];	/* all data + pad + 
							 * digest 
							 */
    struct iovec crc_rx_iov[(ISCSI_MAX_SG + 1 + 1)];	/* all data + pad + 
							 * digest for CRC 
							 * calculations 
							 */
    unsigned char rx_buffer[ISCSI_RXCTRL_SIZE];
    struct iovec tx_iov[(1 + 1 + ISCSI_MAX_SG + 1 + 1)];	/* header + 
								 * digest + 
								 * all data + 
								 * pad + 
								 * digest 
								 */
} iscsi_session_t;

/* session control bits */
#define TX_WAKE       0
#define TX_PING       1		/* NopOut, reply requested */
#define TX_PING_DATA  2		/* NopOut, reply requested, with data */
#define TX_NOP_REPLY  3		/* reply to a Nop-in from the target */

#define TX_SCSI_COMMAND 4
#define TX_DATA         5
#define TX_ABORT        6
#define TX_LUN_RESET    7

#define TX_LOGOUT            8

#define TX_THREAD_BLOCKED    12
#define SESSION_PROBING_LUNS 15

#define SESSION_REPLACEMENT_TIMEDOUT 16
#define SESSION_TASK_MGMT_TIMEDOUT   17
#define SESSION_COMMAND_TIMEDOUT     18
#define SESSION_TASK_TIMEDOUT        19

#define SESSION_ESTABLISHED          20
#define SESSION_DROPPED              21
#define SESSION_TASK_ALLOC_FAILED    22
#define SESSION_RETRY_COMMANDS       23

#define SESSION_RESET_REQUESTED      24
#define SESSION_RESETTING            25
#define SESSION_RESET                26
#define SESSION_LOGOUT_REQUESTED     27

#define SESSION_LOGGED_OUT       28
#define SESSION_WINDOW_CLOSED    29
#define SESSION_TERMINATING      30
#define SESSION_TERMINATED       31

#else

/* daemon's session structure */
typedef struct iscsi_session {
    int socket_fd;
    int login_timeout;
    int auth_timeout;
    int active_timeout;
    int idle_timeout;
    int ping_timeout;
    int vendor_specific_keys;
    unsigned int irrelevant_keys_bitmap;
    int send_async_text;
    uint32_t itt;
    uint32_t CmdSn;
    uint32_t ExpCmdSn;
    uint32_t MaxCmdSn;
    uint32_t ExpStatSn;
    int ImmediateData;
    int InitialR2T;
    int MaxRecvDataSegmentLength;	/* the value we declare */
    int MaxXmitDataSegmentLength;	/* the value declared by the target */
    int FirstBurstLength;
    int MaxBurstLength;
    int DataPDUInOrder;
    int DataSequenceInOrder;
    int DefaultTime2Wait;
    int DefaultTime2Retain;
    int HeaderDigest;
    int DataDigest;
    int type;
    int current_stage;
    int next_stage;
    int partial_response;
    int portal_group_tag;
    uint8_t isid[6];
    uint16_t tsih;
    int iscsi_bus;
    int target_id;
    char TargetName[TARGET_NAME_MAXLEN + 1];
    char TargetAlias[TARGET_NAME_MAXLEN + 1];
    char *InitiatorName;
    char *InitiatorAlias;
    int ip_length;
    uint8_t ip_address[16];
    int port;
    int tcp_window_size;
    int (*update_address) (struct iscsi_session * session, char *address);
    IscsiAuthStringBlock auth_recv_string_block;
    IscsiAuthStringBlock auth_send_string_block;
    IscsiAuthLargeBinary auth_recv_binary_block;
    IscsiAuthLargeBinary auth_send_binary_block;
    IscsiAuthClient auth_client_block;
    IscsiAuthClient *auth_client;
    int num_auth_buffers;
    IscsiAuthBufferDesc auth_buffers[5];
    int bidirectional_auth;
    char username[iscsiAuthStringMaxLength];
    uint8_t password[iscsiAuthStringMaxLength];
    int password_length;
    char username_in[iscsiAuthStringMaxLength];
    uint8_t password_in[iscsiAuthStringMaxLength];
    int password_length_in;
} iscsi_session_t;

#endif				/* __KERNEL__ */

#endif
