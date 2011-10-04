/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2003-2005 Emulex.  All rights reserved.           *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc.h 483 2006-03-22 00:27:31Z sf_support $
 */

#ifndef _H_LPFC
#define _H_LPFC

#include <linux/interrupt.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_scsi.h"
#include "lpfc_disc.h"
#include "lpfc_mem.h"

#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

/* Define the SLIM2 page size. */
#define LPFC_SLIM2_PAGE_AREA  8192

/* Define macros for handling the driver lock. */
#define LPFC_DRVR_LOCK(phba, flag)   lpfc_drvr_lock(phba, &flag)
#define LPFC_DRVR_UNLOCK(phba, flag) lpfc_drvr_unlock(phba, &flag)

/* Define macros for 64 bit support */
#define putPaddrLow(addr)    ((uint32_t) (0xffffffff & (u64)(addr)))
#define putPaddrHigh(addr)   ((uint32_t) (0xffffffff & (((u64)(addr))>>32)))
#define getPaddr(high, low)  ((dma_addr_t)( \
			     (( (u64)(high)<<16 ) << 16)|( (u64)(low))))

/* Provide maximum configuration definitions. */
#define LPFC_DRVR_TIMEOUT   16		/* driver iocb timeout value in sec. */
#define MAX_FC_BINDINGS    64		/* max number of persistent bindings */
#define MAX_FCP_TARGET     256		/* max number of FCP targets supported */
#define MAX_FCP_LUN        255		/* max lun number supported */
#define MAX_FCP_CMDS       16384	/* max number of FCP cmds supported */
#define FC_MAX_ADPTMSG     64

#define MAX_HBAEVT 32
/***************************************************************************/
/*
 * This is the global device driver control structure
 */
/***************************************************************************/

struct lpfc_drvr {
	unsigned long loadtime;
	struct list_head hba_list_head;
	uint16_t num_devs;	/* count of devices configed */
};
typedef struct lpfc_drvr lpfcDRVR_t;

#if __LITTLE_ENDIAN

#define putLunLow(lunlow, lun)              \
   {                                        \
   lunlow = 0;                              \
   }

#define putLunHigh(lunhigh, lun)            \
   {                                        \
   lunhigh = (uint32_t)(lun << 8);          \
   }

#else				/* BIG_ENDIAN_HOST */

#define putLunLow(lunlow, lun)              \
   {                                        \
   lunlow = 0;                              \
   }

#define putLunHigh(lunhigh, lun)            \
   {                                        \
   lunhigh = (uint32_t)(lun << 16);         \
   }
#endif

#define SWAP_ALWAYS(x)  ((((x) & 0xFF)<<24) | (((x) & 0xFF00)<<8) | \
			(((x) & 0xFF0000)>>8) | (((x) & 0xFF000000)>>24))

#define SWAP_ALWAYS16(x) ((((x) & 0xFF) << 8) | ((x) >> 8))

/****************************************************************************/
/*      Device VPD save area                                                */
/****************************************************************************/
typedef struct lpfc_vpd {
	uint32_t status;	/* vpd status value */
	uint32_t length;	/* number of bytes actually returned */
	struct {
		uint32_t rsvd1;	/* Revision numbers */
		uint32_t biuRev;
		uint32_t smRev;
		uint32_t smFwRev;
		uint32_t endecRev;
		uint16_t rBit;
		uint8_t fcphHigh;
		uint8_t fcphLow;
		uint8_t feaLevelHigh;
		uint8_t feaLevelLow;
		uint32_t postKernRev;
		uint32_t opFwRev;
		uint8_t opFwName[16];
		uint32_t sli1FwRev;
		uint8_t sli1FwName[16];
		uint32_t sli2FwRev;
		uint8_t sli2FwName[16];
	} rev;
} lpfc_vpd_t;

typedef struct lpfc_cfgparam {
	char *a_string;
	uint32_t a_low;
	uint32_t a_hi;
	uint32_t a_default;
	uint32_t a_current;
	uint16_t a_flag;
	uint16_t a_changestate;
	char *a_help;
} lpfcCfgParam_t;

struct lpfcScsiLun;
struct lpfc_scsi_buf;

/* This should correspond with the HBA API event structure */
typedef struct hbaevt {
	uint32_t fc_eventcode;
	uint32_t fc_evdata1;
	uint32_t fc_evdata2;
	uint32_t fc_evdata3;
	uint32_t fc_evdata4;
} HBAEVT_t;

/*
 * lpfc stat counters
 */
struct lpfc_stats {
	/* Statistics for ELS commands */
	uint32_t elsLogiCol;
	uint32_t elsRetryExceeded;
	uint32_t elsXmitRetry;
	uint32_t elsDelayRetry;
	uint32_t elsRcvDrop;
	uint32_t elsRcvFrame;
	uint32_t elsRcvRSCN;
	uint32_t elsRcvRNID;
	uint32_t elsRcvFARP;
	uint32_t elsRcvFARPR;
	uint32_t elsRcvFLOGI;
	uint32_t elsRcvPLOGI;
	uint32_t elsRcvADISC;
	uint32_t elsRcvPDISC;
	uint32_t elsRcvFAN;
	uint32_t elsRcvLOGO;
	uint32_t elsRcvPRLO;
	uint32_t elsRcvPRLI;
	uint32_t elsRcvRRQ;
	uint32_t elsXmitFLOGI;
	uint32_t elsXmitPLOGI;
	uint32_t elsXmitPRLI;
	uint32_t elsXmitADISC;
	uint32_t elsXmitLOGO;
	uint32_t elsXmitSCR;
	uint32_t elsXmitRNID;
	uint32_t elsXmitFARP;
	uint32_t elsXmitFARPR;
	uint32_t elsXmitACC;
	uint32_t elsXmitLSRJT;

	uint32_t frameRcvBcast;
	uint32_t frameRcvMulti;
	uint32_t strayXmitCmpl;
	uint32_t frameXmitDelay;
	uint32_t xriCmdCmpl;
	uint32_t xriStatErr;
	uint32_t LinkUp;
	uint32_t LinkDown;
	uint32_t LinkMultiEvent;
	uint32_t NoRcvBuf;
	uint32_t fcpCmd;
	uint32_t fcpCmpl;
	uint32_t fcpRspErr;
	uint32_t fcpRemoteStop;
	uint32_t fcpPortRjt;
	uint32_t fcpPortBusy;
	uint32_t fcpError;
	uint32_t fcpLocalErr;
};
typedef struct lpfc_stats LPFC_STAT_t;

typedef struct lpfcHBA {
	uint8_t intr_inited;		/* flag for interrupt registration */
	uint8_t no_timer;
	struct list_head hba_list;	/* List of hbas/ports */      
	uint32_t hba_flag;		/* device flags */
#define FC_SCHED_CFG_INIT   0x2		/* schedule a call to fc_cfg_init() */
#define FC_STOP_IO          0x8		/* set for offline call */
#define FC_POLL_CMD         0x10	/* indicate to poll for command
					   completion */
#define FC_LFR_ACTIVE       0x20	/* Link Failure recovery activated */
#define FC_NDISC_ACTIVE     0x40	/* Node discovery mode activated */

	struct lpfc_sli sli;
	DMABUF_t slim2p;
	uint32_t slim_size;

	uint32_t hba_state;

#define LPFC_INIT_START           1	/* Initial state after board reset */
#define LPFC_INIT_MBX_CMDS        2	/* Initialize HBA with mbox commands */
#define LPFC_LINK_DOWN            3	/* HBA initialized, link is down */
#define LPFC_LINK_UP              4	/* Link is up  - issue READ_LA */
#define LPFC_LOCAL_CFG_LINK       5	/* local NPORT Id configured */
#define LPFC_FLOGI                6	/* FLOGI sent to Fabric */
#define LPFC_FABRIC_CFG_LINK      7	/* Fabric assigned NPORT Id
					   configured */
#define LPFC_NS_REG               8	/* Register with NameServer */
#define LPFC_NS_QRY               9	/* Query NameServer for NPort ID list */
#define LPFC_BUILD_DISC_LIST      10	/* Build ADISC and PLOGI lists for
					 * device authentication / discovery */
#define LPFC_DISC_AUTH            11	/* Processing ADISC list */
#define LPFC_CLEAR_LA             12	/* authentication cmplt - issue
					   CLEAR_LA */
#define LPFC_HBA_READY            32
#define LPFC_HBA_ERROR            0xff

	int32_t stopped;   /* HBA has not been restarted since last ERATT */
	uint8_t fc_linkspeed;	/* Link speed after last READ_LA */

	uint32_t fc_eventTag;	/* event tag for link attention */
	uint32_t fc_prli_sent;	/* cntr for outstanding PRLIs */
	uint8_t phys_addr[8];	/* actual network address in use */

	uint32_t disc_state;	/*in addition to hba_state */
	uint32_t num_disc_nodes;	/*in addition to hba_state */

	uint8_t fcp_mapping;	/* Map FCP devices based on WWNN WWPN or DID */
#define FCP_SEED_WWNN   0x1
#define FCP_SEED_WWPN   0x2
#define FCP_SEED_DID    0x4
#define FCP_SEED_MASK   0x7
#define FCP_SEED_AUTO   0x8	/* binding was created by auto mapping */

	struct timer_list fc_estabtmo;	/* link establishment timer */
	struct timer_list fc_disctmo;	/* Discovery rescue timer */
	struct timer_list fc_linkdown;	/* link down timer */
	struct timer_list fc_fdmitmo;	/* fdmi timer */


	void *fc_evt_head;	/* waiting for event queue */
	void *fc_evt_tail;	/* waiting for event queue */

	uint16_t hba_event_put;	/* hbaevent event put word anchor */
	uint16_t hba_event_get;	/* hbaevent event get word anchor */
	uint32_t hba_event_missed;	/* hbaevent missed event word anchor */
	uint32_t sid_cnt;	/* SCSI ID counter */

	HBAEVT_t hbaevt[MAX_HBAEVT];

#define FC_CPQ_LUNMAP   0x1	/* SCSI passthru interface LUN 0 mapping */

	/* These fields used to be binfo */
	NAME_TYPE fc_nodename;	/* fc nodename */
	NAME_TYPE fc_portname;	/* fc portname */
	uint32_t fc_pref_DID;	/* preferred D_ID */
	uint8_t fc_pref_ALPA;	/* preferred AL_PA */
	uint8_t fc_deferip;	/* defer IP processing */
	uint8_t ipAddr[16];	/* For RNID support */
	uint16_t ipVersion;	/* For RNID support */
	uint16_t UDPport;	/* For RNID support */
	uint32_t fc_edtov;	/* E_D_TOV timer value */
	uint32_t fc_arbtov;	/* ARB_TOV timer value */
	uint32_t fc_ratov;	/* R_A_TOV timer value */
	uint32_t fc_rttov;	/* R_T_TOV timer value */
	uint32_t fc_altov;	/* AL_TOV timer value */
	uint32_t fc_crtov;	/* C_R_TOV timer value */
	uint32_t fc_citov;	/* C_I_TOV timer value */
	uint32_t fc_myDID;	/* fibre channel S_ID */
	uint32_t fc_prevDID;	/* previous fibre channel S_ID */

	SERV_PARM fc_sparam;	/* buffer for our service parameters */
	SERV_PARM fc_fabparam;	/* fabric service parameters buffer */
	uint8_t alpa_map[128];	/* AL_PA map from READ_LA */

	uint8_t fc_ns_retry;	/* retries for fabric nameserver */
	uint32_t fc_nlp_cnt;	/* outstanding NODELIST requests */
	uint32_t fc_rscn_id_cnt;	/* count of RSCNs payloads in list */
	DMABUF_t *fc_rscn_id_list[FC_MAX_HOLD_RSCN];

	uint32_t lmt;
	uint32_t fc_flag;	/* FC flags */
#define FC_FCP_WWNN             0x0	/* Match FCP targets on WWNN */
#define FC_FCP_WWPN             0x1	/* Match FCP targets on WWPN */
#define FC_FCP_DID              0x2	/* Match FCP targets on DID */
#define FC_FCP_MATCH            0x3	/* Mask for match FCP targets */
#define FC_PENDING_RING0        0x4	/* Defer ring 0 IOCB processing */
#define FC_LNK_DOWN             0x8	/* Link is down */
#define FC_PT2PT                0x10	/* pt2pt with no fabric */
#define FC_PT2PT_PLOGI          0x20	/* pt2pt initiate PLOGI */
#define FC_DELAY_DISC           0x40	/* Delay discovery till after cfglnk */
#define FC_PUBLIC_LOOP          0x80	/* Public loop */
#define FC_INTR_THREAD          0x100	/* In interrupt code */
#define FC_LBIT                 0x200	/* LOGIN bit in loopinit set */
#define FC_RSCN_MODE            0x400	/* RSCN cmd rcv'ed */
#define FC_RSCN_DISC_TMR        0x800	/* wait edtov before processing RSCN */
#define FC_NLP_MORE             0x1000	/* More node to process in node tbl */
#define FC_OFFLINE_MODE         0x2000	/* Interface is offline for diag */
#define FC_LD_TIMER             0x4000	/* Linkdown timer has been started */
#define FC_LD_TIMEOUT           0x8000	/* Linkdown timeout has occurred */
#define FC_FABRIC               0x10000	/* We are fabric attached */
#define FC_DELAY_PLOGI          0x20000	/* Delay login till unreglogin */
#define FC_SLI2                 0x40000	/* SLI-2 CONFIG_PORT cmd completed */
#define FC_INTR_WORK            0x80000	/* Was there work last intr */
#define FC_NO_ROOM_IP           0x100000	/* No room on IP xmit queue */
#define FC_NO_RCV_BUF           0x200000	/* No Rcv Buffers posted IP
						   ring */
#define FC_BUS_RESET            0x400000	/* SCSI BUS RESET */
#define FC_ESTABLISH_LINK       0x800000	/* Reestablish Link */
#define FC_SCSI_RLIP            0x1000000	/* SCSI rlip routine called */
#define FC_DELAY_NSLOGI         0x2000000	/* Delay NameServer till
						   ureglogin */
#define FC_NSLOGI_TMR           0x4000000	/* NameServer in process of
						   logout */
#define FC_DELAY_RSCN           0x8000000	/* Delay RSCN till ureg/reg
						   login */
#define FC_RSCN_DISCOVERY       0x10000000	/* Authenticate all devices
						   after RSCN */
#define FC_POLL_MODE        0x40000000	/* [SYNC] I/O is in the polling mode */
#define FC_BYPASSED_MODE        0x80000000	/* Interface is offline for
						   diag */

	uint32_t reset_pending;
	uint32_t fc_topology;	/* link topology, from LINK INIT */
	LPFC_NODELIST_t fc_nlp_bcast;	/* used for IP bcast's */

	LPFC_STAT_t fc_stat;

	uint32_t fc_ipfarp_timeout;	/* timeout in seconds for farp req
					   completion. */
	uint32_t fc_ipxri_timeout;	/* timeout in seconds for ip XRI create
					   completions. */
	struct list_head fc_node_farp_list;

	/* These are the head/tail pointers for the bind, plogi, adisc, unmap,
	 *  and map lists.  Their counters are immediately following.
	 */
	struct list_head fc_nlpbind_list;
	struct list_head fc_plogi_list;
	struct list_head fc_adisc_list;
	struct list_head fc_nlpunmap_list;
	struct list_head fc_nlpmap_list;

	/* Keep counters for the number of entries in each list. */
	uint16_t fc_bind_cnt;
	uint16_t fc_plogi_cnt;
	uint16_t fc_adisc_cnt;
	uint16_t fc_unmap_cnt;
	uint16_t fc_map_cnt;
	LPFC_NODELIST_t fc_fcpnodev;	/* nodelist entry for no device */
	uint32_t nport_event_cnt;	/* timestamp for nlplist entry */

	LPFCSCSITARGET_t *device_queue_hash[MAX_FCP_TARGET];
#define LPFC_RPI_HASH_SIZE     64
#define LPFC_RPI_HASH_FUNC(x)  ((x) & (0x3f))
	LPFC_NODELIST_t *fc_nlplookup[LPFC_RPI_HASH_SIZE]; /* ptr to active D_ID
								   / RPIs */
	uint32_t wwnn[2];
	uint32_t RandomData[7];
	uint32_t hbainitEx[5];

	lpfcCfgParam_t *config;	/* Configuration parameters */

	lpfc_vpd_t vpd;		/* vital product data */

	unsigned long iflag;	/* used to hold context for drvr lock, if
				   needed */

	struct Scsi_Host *host;
	struct pci_dev *pcidev;
	spinlock_t drvrlock;
	spinlock_t hiprilock;
	struct tasklet_struct task_run;
	struct list_head      task_disc;
	uint16_t              task_discq_cnt;

	atomic_t cmnds_in_flight;


	unsigned long pci_bar0_map;     /* Physical address for PCI BAR0 */
	unsigned long pci_bar2_map;     /* Physical address for PCI BAR0 */
	void *slim_memmap_p;	        /* Kernel memory mapped address for PCI
					   BAR0 */
	void *ctrl_regs_memmap_p;	/* Kernel memory mapped address for PCI
					   BAR2 */

	void *MBslimaddr;	/* virtual address for mbox cmds */
	void *HAregaddr;	/* virtual address for host attn reg */
	void *CAregaddr;	/* virtual address for chip attn reg */
	void *HSregaddr;	/* virtual address for host status reg */
	void *HCregaddr;	/* virtual address for host ctl reg */
	wait_queue_head_t linkevtwq;
	wait_queue_head_t rscnevtwq;
	wait_queue_head_t ctevtwq;

	struct scsi_cmnd *cmnd_retry_list;
	int in_retry;

	uint8_t brd_no;		/* FC board number */
	uint8_t fc_busflag;	/* bus access flags */
	LPFC_SCHED_HBA_t hbaSched;

	uint32_t fcp_timeout_offset;

	char adaptermsg[FC_MAX_ADPTMSG];	/* adapter printf messages */

	char SerialNumber[32];		/* adapter Serial Number */
	char OptionROMVersion[32];	/* adapter BIOS / Fcode version */
	char ModelDesc[256];		/* Model Description */
	char ModelName[80];		/* Model Name */
	char ProgramType[256];		/* Program type */
	char Port[20];			/* Port No */
	uint8_t vpd_flag;               /* VPD data flag */

#define VPD_MODEL_DESC      0x1         /* valid vpd model description */
#define VPD_MODEL_NAME      0x2         /* valid vpd model name */
#define VPD_PROGRAM_TYPE    0x4         /* valid vpd program type */
#define VPD_PORT            0x8         /* valid vpd port data */
#define VPD_MASK            0xf         /* mask for any vpd data */

	struct lpfcScsiLun *(*lpfc_tran_find_lun) (struct lpfc_scsi_buf *);
	struct timer_list dqfull_clk;
	struct timer_list els_tmofunc;
	struct timer_list ip_tmofunc;
	struct timer_list scsi_tmofunc;
	struct timer_list buf_tmo;

	struct list_head delay_list;
	struct list_head free_buf_list;

	/*
	 * HBA API 2.0 specific counters
	 */
	uint64_t fc4InputRequests;
	uint64_t fc4OutputRequests;
	uint64_t fc4ControlRequests;

	/* pci_mem_pools */
	struct pci_pool *lpfc_scsi_dma_ext_pool;
	struct pci_pool *lpfc_mbuf_pool;
	struct pci_pool *lpfc_page_pool;
	struct lpfc_dma_pool lpfc_mbuf_saftey_pool;
	struct lpfc_dma_pool lpfc_page_saftey_pool;
	struct lpfc_mem_pool lpfc_mem_saftey_pool;
	struct list_head timerList;

} lpfcHBA_t;

struct clk_data {
	struct list_head listLink;
	struct timer_list *timeObj;
	lpfcHBA_t     *phba; 
	unsigned long clData1;
	unsigned long clData2;
	unsigned long flags;
#define TM_CANCELED	0x1	/* timer has been canceled */
};

typedef struct fcEVT {		/* Kernel level Event structure */
	uint32_t evt_handle;
	uint32_t evt_mask;
	uint32_t evt_data0;
	uint16_t evt_sleep;
	uint16_t evt_flags;
	void    *evt_type;
	void    *evt_next;
	void    *evt_data1;
	void    *evt_data2;
} fcEVT_t;

typedef struct fcEVTHDR {	/* Kernel level Event Header */
	uint32_t e_handle;
	uint32_t e_mask;
	uint16_t e_mode;
#define E_SLEEPING_MODE     0x0001
	uint16_t e_refcnt;
	uint16_t e_flag;
#define E_GET_EVENT_ACTIVE  0x0001
	fcEVT_t *e_head;
	fcEVT_t *e_tail;
	void    *e_next_header;
	void    *e_type;
} fcEVTHDR_t;
#endif				/* _H_LPFC */
